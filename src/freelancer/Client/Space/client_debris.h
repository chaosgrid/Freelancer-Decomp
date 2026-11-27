// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// freelancer.exe Debris.cpp -- retail E:\FL\Scratch\Source\Client\space\Debris.cpp: the [Debris]
// archetype table (debris= in DATA\freelancer.ini's [Data], DAT_006754f4) and the Debris
// gameobj (RTTI .?AVDebris@@) that carries a flying wreck piece until it explodes or vanishes.
//
//   Debris : GameObject   ctor inlined in FUN_00530620 / FUN_005306e0   0xc0
//                         IObjRW 005dddd4, IObjAffect 005ddd54 (GameObject's entries),
//                         CBase 005ddd4c -> FUN_00530130 (body FUN_00530160)
//
// The piece's CObject is either handed over (FUN_00530620: explosion shatter pieces and parts
// torn off an EquippedObj) or created from a simple archetype (FUN_005306e0: explosion
// innards).

#include "Space/client_gameobj.h"
#include "common_archetype.h"
#include "common_id.h"

class INI_Reader;

// One [Debris] section (FUN_00530a20 reads it). The retail map node keeps the nickname at
// +0x0c and this record at +0x10; 0x24 bytes (nine dwords copied into the node).
struct FDebrisArch
{
    // The defaults both retail builders store inline -- the section reader's local and the
    // static initialiser of the default record (LAB_00530960).
    FDebrisArch();

    float LifetimeMin;    // +0x00  lifetime = min[, max]; default 0
    float LifetimeMax;     // +0x04  default FLT_MAX (0x7f7fffff); max < min -> min
    int DeathMethod;       // +0x08  death_method: "exploding" 0, "vanishing" 1; default 1
    ID_String Explosion;   // +0x0c  explosion = nickname (CreateID)
    ID_String Trail;       // +0x10  trail = effect nickname (CreateID)
    float LinearDrag;      // +0x14  linear_drag
    Vector AngularDrag;    // +0x18  angular_drag = x, y, z
};
static_assert(sizeof(FDebrisArch) == 0x24, "Debris archetype 0x24");

// FUN_00530a20 -- read one [Debris] section. A nickname already in the table is kept and,
// unless bQuiet, FDUMPs the duplicate warning. True only when the section was added.
bool ReadDebrisArch(INI_Reader* Reader, bool bQuiet);
// FUN_00530d20 -- read every [Debris] section of Path. Returns how many entries the table
// gained. Caller: DataConfig's debris= (bQuiet = false).
int LoadDebrisArchFile(char const* Path, bool bQuiet);
// FUN_00530fa0 -- empty the table (main.cpp's shutdown).
void ShutdownDebrisArchDb();
// FUN_005309c0 -- the entry named *Nickname, null when there is none.
FDebrisArch* FindDebrisArch(ID_String const* Nickname);
// FUN_00530a10 -- the default record (DAT_006754d0).
FDebrisArch* GetDefaultDebrisArch();

class FClientDebrisGameobj : public FClientGameobj
{
public:
    // Inlined in the two factories. +0xa8..+0xb0 are left for Init.
    FClientDebrisGameobj();
    // FUN_00530160 (this = the CBase sub-object); deleting wrapper FUN_00530130.
    ~FClientDebrisGameobj() override;

    // IObjInspect slot 34 (+0x88), LAB_00530610.
    int get_type(uint& OutType) const override;

    // Slot 90 (+0x168), FUN_005304b0.
    void create(Archetype::Root* Arch) override;
    // Slot 91 (+0x16c), FUN_0053f330 (the body Beam shares): returns false.
    bool find(unsigned const& Id) override;
    // Slot 93 (+0x174), LAB_005304d0: the object's update, GameObject's update, then the
    // lifetime countdown -- explode (slot 108) or vanish (slot 87) at 0.
    void update(float Dt) override;
    // Slot 95 (+0x17c), FUN_005300f0: GameObject's LOD pick; a picked LOD whose low word is
    // below 3, or whose high word is 2 or more, clears Field_0xbc.
    unsigned* PickLod(unsigned* OutLod, FSpaceCamera* Camera, unsigned const* CachedLod) override;
    // Slot 108 (+0x1b0), LAB_005305a0: with an explosion, Action_Explode into the action list;
    // without one, GameObject's slot 108.
    void Explode(Vector const* Pos) override;

    // FUN_005301d0: bind the piece to its [Debris] record -- GameObject's slot 92, the
    // record's lifetime / death method / explosion, the trail effect, the drag, the nebula.
    void Init(FDebrisArch const* Arch);

    // Retail keeps the death method as the dword at +0xa8, over GameObject's bWatchListed
    // byte; the port's GameObject owns that byte, so the dword moves to +0xac and Init also
    // stores its low byte into bWatchListed the way the retail store does.
    int DeathMethod;                     // retail +0xa8  FDebrisArch +0x08
    float LifetimeMin;                   // retail +0xac  FDebrisArch +0x00
    float LifetimeMax;                   // retail +0xb0  FDebrisArch +0x04
    Archetype::Explosion* Explosion;     // retail +0xb4  GetExplosion(record +0x0c)
    float Remaining;                     // retail +0xb8  rand between min and max, counts down
    unsigned char Field_0xbc;            // retail +0xbc  ctor 1; FUN_005300f0 clears it when the
                                         //  picked LOD's low word < 3 or high word >= 2; update
                                         //  (DeathMethod 1) ends the piece early while clear
    unsigned char Padbd[3];
};

// FUN_00530620 -- a Debris gameobj around an existing object. Callers: the explosion shatter
// FUN_00536800 and EquippedObj FUN_00533d10.
FClientDebrisGameobj* CreateDebrisGameobjForObject(CObject* Object, FDebrisArch const* Arch);
// FUN_005306e0 -- a Debris gameobj built from a simple archetype at Pos, with its own physics
// body. Caller: the explosion innards FUN_00536fb0.
FClientDebrisGameobj* CreateDebrisGameobjFromArch(Archetype::Root* Arch,
                                                  FDebrisArch const* DebrisArch,
                                                  Vector const* Pos);
