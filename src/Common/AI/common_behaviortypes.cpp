// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll -- the behavior type registry: the twenty-one BehaviorTypeDesc descriptors and
// their load-time registry (GBehaviorRegistryInit, dynamic initialisation -- keep this TU's
// globals together and in this order), BehaviorTypes::get_behavior_id/get_behavior_name and
// CREATE_BEHAVIOR.

#include "common_iface.h"
#include "common_behavior.h" // class Behavior; also FormationBehavior, the id-10 registry entry

// The twenty concrete behavior classes the id 0..20 registry entries construct. BehaviorTypes'
// create_behavior() has to see every one of them, so this is the one translation unit that
// includes the whole family. FormationBehavior lives in common_behavior.h above.
#include "common_buzzbehavior.h"
#include "common_cruisebehavior.h"
#include "common_delaybehavior.h"
#include "common_dockbehavior.h"
#include "common_drasticevadebehavior.h"
#include "common_evadebehavior.h"
#include "common_facebehavior.h"
#include "common_fleebehavior.h"
#include "common_followbehavior.h"
#include "common_freeflightbehavior.h"
#include "common_gotobehavior.h"
#include "common_guidebehavior.h"
#include "common_idlebehavior.h"
#include "common_itlbehavior.h"
#include "common_largeshipbehavior.h"
#include "common_launchbehavior.h"
#include "common_lootbehavior.h"
#include "common_nullbehavior.h"
#include "common_strafebehavior.h"
#include "common_trailbehavior.h"

#include <cstring>
#include <new>

// ============================================================================
// BehaviorTypes
// ============================================================================

// ----------------------------------------------------------------------------
// Behavior type registry — retail DAT_063fc540 / DAT_063fc8e0 / DAT_063fc8e4.
//
// From the twenty-one .CRT$XC static initialisers in the module init table at
// .data:063ea310..063ea360, one per behavior type (e.g. id 20 "Delay" at 062d15b0): each sets
// the vptr and id, copies the name (strlen clamped to 0x1f, NUL at len), stores the length,
// then registry[id] = &descriptor and ++count. So a descriptor is
// {vptr, int Id, int NameLength, char Name[32]} = 0x2C bytes, the stride of the
// 0x063fc540..0x063fc8dc block (laid out in descending id order: id 20 first, id 0 "NULL" last
// at 0x063fc8b0). DAT_063fc8e0 is the count and DAT_063fc8e4 the 21-entry pointer array
// (zeroed by the initialiser at 062d0b90), running to 0x063fc938.
//
// Each descriptor's vtable is a three-slot .rdata block 0x0C bytes ahead of its own name
// literal (e.g. 063a002c for "NULL" at 063a0038):
//   slot 0 (+0x00): get_type_id()     — ICF-folded onto TractorArm::GetMode (`return *(this+4)`)
//   slot 1 (+0x04): create_behavior() — the only per-type slot (FUN_062d16d0 for NULL,
//                                       FUN_062d1760 for Buzz, ...)
//   slot 2 (+0x08): get_name()        — ICF-folded onto LAB_062d1ae0, shared by all 21
//                                       vtables (`return this+0x0C`)
//
// Each factory body (.text:062d16d0..062d1f90, not evenly spaced) is operator_new(size), a
// null test, then the type's ctor with the descriptor's Id: exactly `new(nothrow) T(TypeId)`.
// The ids, retail sizes and constructors, in registry order:
//     0  NULL             0x2c   (base ctor FUN_062d2020 + vptr 063a0244 inline)
//     1  Buzz             0x354  FUN_062c5af0      11 Large Ship (move) 0x180 FUN_062cfe20
//     2  Goto             0x3fc  FUN_062fd620      12 Cruise            0x2c  FUN_062ce360
//     3  Trail            0x248  FUN_062ca350      13 Strafe            0x1bc FUN_062c9540
//     4  Flee             0xe8   FUN_062c7020      14 Guide             0x124 FUN_062cf230
//     5  Evade            0x2c0  FUN_062c7790      15 Face              0x1dc FUN_062c88e0
//     6  Idle             0x40   FUN_062cd530      16 Loot              0x180 FUN_062ce400
//     7  Dock             0x470  FUN_062c2e60      17 Follow            0x2bc FUN_062ccaf0
//     8  Launch           0x234  FUN_062cd640      18 DrasticEvade      0x44  FUN_062c8730
//     9  InstantTradeLane 0x5c   FUN_062cee50      19 FreeFlight        0x400 FUN_062c6d10
//    10  Formation        0x2b8  FUN_062cbfe0      20 Delay             0x48  FUN_062d0a10
// Each size is pinned by a static_assert in the class's own header. Id 8's name is not inline
// in its .rdata row at 063a00e8 (Ghidra shows an empty string): the initialiser at
// .text:062d0fba copies "Launch" from .rdata:0639a06c.
//
// The per-type slot: retail has twenty-one descriptor vtables, identical except for +0x04.
// The port uses one template subclass per behavior type (TBehaviorTypeDesc<T>), adding no data
// and no new virtual, so every descriptor stays the same 0x2C object.
class BehaviorTypeDesc;

// DAT_063fc8e0 -- the number of registered descriptors; DAT_063fc8e4 -- registry[id] =
// &descriptor, a fixed 0x15-entry static. Retail's first initialiser 062d0b90 zeroes both;
// static zero-initialisation does the same here, ahead of every dynamic initialiser in this TU.
static int GBehaviorCount;
static BehaviorTypeDesc* GBehaviorRegistry[0x15];

class BehaviorTypeDesc
{
public:
    explicit BehaviorTypeDesc(int InTypeId, char const* InName)
        : TypeId(InTypeId), NameLength(0)
    {
        int Length = static_cast<int>(strlen(InName));
        if (Length > 0x1F)
            Length = 0x1F;
        memcpy(Name, InName, static_cast<size_t>(Length));
        Name[Length] = '\0';
        NameLength = Length;
        GBehaviorRegistry[TypeId] = this;
        ++GBehaviorCount;
    }
    // No virtual destructor, deliberately: retail's vtable is exactly three slots
    // (063a002c..063a0034) with get_type_id first; a dtor would shift get_name off +0x08.

    virtual int get_type_id() const { return TypeId; }           // vtable +0x00
    // Unreachable through the registry (only TBehaviorTypeDesc<T> instances register); kept
    // non-pure so the class is constructible.
    virtual Behavior* create_behavior() { return nullptr; }      // vtable +0x04
    virtual char const* get_name() const { return Name; }        // vtable +0x08

    int TypeId;         // +0x04
    int NameLength;     // +0x08
    char Name[32];      // +0x0C
};

// One C++ type per retail descriptor vtable; sizeof stays 0x2C for every T.
//
// `new (std::nothrow)`: retail's MSVCRT `operator new` returns null on failure, and every
// factory body tests for it.
template <class BehaviorClass>
class TBehaviorTypeDesc : public BehaviorTypeDesc
{
public:
    TBehaviorTypeDesc(int InTypeId, char const* InName)
        : BehaviorTypeDesc(InTypeId, InName)
    {
    }

    Behavior* create_behavior() override
    {
        return new (std::nothrow) BehaviorClass(TypeId);
    }
};
static_assert(sizeof(TBehaviorTypeDesc<NullBehavior>) == sizeof(BehaviorTypeDesc),
              "a typed descriptor must stay the same 0x2C object retail's initialisers build");

// Retail id order; each line's comment is its static initialiser.
static TBehaviorTypeDesc<NullBehavior>             GDescNull(BEHAVIOR_NULL, "NULL");                          // 062d0bb0
static TBehaviorTypeDesc<BuzzBehavior>             GDescBuzz(BEHAVIOR_BUZZ, "Buzz");                          // 062d0c30
static TBehaviorTypeDesc<GotoBehavior>             GDescGoto(BEHAVIOR_GOTO, "Goto");                          // 062d0cb0
static TBehaviorTypeDesc<TrailBehavior>            GDescTrail(BEHAVIOR_TRAIL, "Trail");                       // 062d0d30
static TBehaviorTypeDesc<FleeBehavior>             GDescFlee(BEHAVIOR_FLEE, "Flee");                          // 062d0db0
static TBehaviorTypeDesc<EvadeBehavior>            GDescEvade(BEHAVIOR_EVADE, "Evade");                       // 062d0e30
static TBehaviorTypeDesc<IdleBehavior>             GDescIdle(BEHAVIOR_IDLE, "Idle");                          // 062d0eb0
static TBehaviorTypeDesc<DockBehavior>             GDescDock(BEHAVIOR_DOCK, "Dock");                          // 062d0f30
static TBehaviorTypeDesc<LaunchBehavior>           GDescLaunch(BEHAVIOR_LAUNCH, "Launch");                    // 062d0fb0
static TBehaviorTypeDesc<InstantTradeLaneBehavior> GDescItl(BEHAVIOR_INSTANT_TRADE_LANE, "InstantTradeLane"); // 062d1030
static TBehaviorTypeDesc<FormationBehavior>        GDescFormation(BEHAVIOR_FORMATION, "Formation");           // 062d10b0
static TBehaviorTypeDesc<LargeShipBehavior>        GDescLargeShip(BEHAVIOR_LARGE_SHIP, "Large Ship (move)");  // 062d1130
static TBehaviorTypeDesc<CruiseBehavior>           GDescCruise(BEHAVIOR_CRUISE, "Cruise");                    // 062d11b0
static TBehaviorTypeDesc<StrafeBehavior>           GDescStrafe(BEHAVIOR_STRAFE, "Strafe");                    // 062d1230
static TBehaviorTypeDesc<GuideBehavior>            GDescGuide(BEHAVIOR_GUIDE, "Guide");                       // 062d12b0
static TBehaviorTypeDesc<FaceBehavior>             GDescFace(BEHAVIOR_FACE, "Face");                          // 062d1330
static TBehaviorTypeDesc<LootBehavior>             GDescLoot(BEHAVIOR_LOOT, "Loot");                          // 062d13b0
static TBehaviorTypeDesc<FollowBehavior>           GDescFollow(BEHAVIOR_FOLLOW, "Follow");                    // 062d1430
static TBehaviorTypeDesc<DrasticEvadeBehavior>     GDescDrasticEvade(BEHAVIOR_DRASTIC_EVADE, "DrasticEvade"); // 062d14b0
static TBehaviorTypeDesc<FreeFlightBehavior>       GDescFreeFlight(BEHAVIOR_FREE_FLIGHT, "FreeFlight");       // 062d1530
static TBehaviorTypeDesc<DelayBehavior>            GDescDelay(BEHAVIOR_DELAY, "Delay");                       // 062d15b0

// DACOM.DLL!stricmp (Common IAT 0639902c), common_dacom_stricmp.cpp.
int CallDacomStricmp(const char* Left, const char* Right);

// The three lookups below index the registry by id with no range test, as retail does: every
// id that reaches them is 0..20 (BehaviorManager's CREATE_BEHAVIOR loop) or -1 (handled by
// get_behavior_name). Retail's array is a fixed 0x15-dword static followed by a float at
// 063fc938, so an out-of-range id there reads a junk pointer; the port has the same hole.

int __cdecl BehaviorTypes::get_behavior_id(char const* Name)
{
    // .text:062d1680: the count is re-read every pass; no null test on the entry; the entry's
    // name goes first to stricmp.
    for (int I = 0; I < GBehaviorCount; ++I)
    {
        if (CallDacomStricmp(GBehaviorRegistry[I]->get_name(), Name) == 0)
            return I;
    }
    return -1;
}

char const* __cdecl BehaviorTypes::get_behavior_name(int Id)
{
    // .text:062d1650. Id -1 returns the string DAT_063ec08c points at.
    if (Id == -1)
        return "BEHAVIOR_ID_NONE";
    BehaviorTypeDesc* Desc = GBehaviorRegistry[Id];
    if (Desc == nullptr)
        return nullptr;
    return Desc->get_name();                                   // JMP [vt+0x08]
}

Behavior* __cdecl CREATE_BEHAVIOR(int TypeId)
{
    // .text:062d1630.
    BehaviorTypeDesc* Desc = GBehaviorRegistry[TypeId];
    if (Desc == nullptr)
        return nullptr;
    return Desc->create_behavior();
}
