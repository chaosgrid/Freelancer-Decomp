// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Content.dll TU20 (06eb6c00-06eb7360): the base class of every space object Content
// tracks per system — solar NPCs (TU24) and the TU50 ship base (NPC ships TU43, the player
// ship TU46).
//
// Retail: vtable 06fb464c has one slot (scalar deleting dtor FUN_06eb7210), size 0x118.
// No RTTI in Content; the class name is the port's.

#include "common_ini.h"       // Vector
#include "common_csys.h"      // Matrix
#include "common_tstring.h"   // TString

#include <vector>

struct FContentPlayerStateBase;   // ContentPlayerState.h: refcounted reputation handle (FUN_06eb6be0)
struct FContentBase;              // BaseDB.h (TU17 record, FUN_06eac0c0)
struct FSystemInfo;               // SystemInfo.h (TU58)

// Construction parameters. Derived parameter blocks embed it: TU50's at +0, TU24's at +0x48.
struct FContentSpaceObjParams
{
    FContentPlayerStateBase* ReputationHandle = nullptr;   // +0x00
    FContentBase* Base = nullptr;                          // +0x04
    // +0x08: the ctor zeroes Length and Name[0] only.
    TString<0x30> Nickname;                                // +0x08
    FSystemInfo* System = nullptr;                         // +0x3c
    // +0x40: -1 = default hit points. Written by the mission ship spawn from the slot's
    // hit_points, read by the TU43 ship create as ShipInfo::HitPts.
    int HitPoints = -1;

    // FUN_06eb7260.
    FContentSpaceObjParams();
    // Every parameter block's dtor ends with FUN_06f28e10 on its +0: the +0 member is a
    // releasing handle; the space object copies it raw (no AddRef).
    ~FContentSpaceObjParams();
    // Inlined at the head of the TU50 block copy ctor FUN_06f73680.
    FContentSpaceObjParams(FContentSpaceObjParams const& Other);
    FContentSpaceObjParams& operator=(FContentSpaceObjParams const&) = delete;
};

static_assert(sizeof(FContentSpaceObjParams) == 0x44, "FContentSpaceObjParams size");

// One of the eight attacker slots at +0x14 (0x18 bytes). TU56 (CombatReputation.cpp) fills it
// on the ATTACKING player's ship, keyed by the damaged object.
struct FSpaceObjAttacker
{
    unsigned int VictimId;     // +0x00 the damaged object's id; 0 = free slot
    // +0x04: +RelativeDamage * 50 per hit, +10 when more than a second passed since
    // HostilityTime.
    float Hostility;
    float FirstTime;           // +0x08 time the attacker was first recorded
    float LastTime;            // +0x0c time of the latest hit; the lowest is recycled
    float HostilityTime;       // +0x10 time of the last +10 step
    unsigned char bWarned;     // +0x14 Hostility > 25 reported (message 0x1005, word 0 = 0)
    unsigned char bProvoked;   // +0x15 Hostility >= 50 reported (word 0 = 1)
};

static_assert(sizeof(FSpaceObjAttacker) == 0x18, "FSpaceObjAttacker size");

// +0x108 vector element (0x10 bytes): one line of a mission dialog (TU28, the +0x6c part of
// a dialog line). The whole vector is assigned by StartMissionDialog and TU58.
struct FSpaceObjPendingComm
{
    unsigned int Key;          // +0x00 the dialog line after which this one is spoken
    unsigned int TextId;       // +0x04 the line's id, SendComm's TextId
    unsigned int TargetId;     // +0x08 the addressee's SpaceObj id, SendComm's TargetId
    unsigned int Field_0x0c;   // +0x0c not read by TU20 (TU28 loader writes 0)
};

static_assert(sizeof(FSpaceObjPendingComm) == 0x10, "FSpaceObjPendingComm size");

// Message 0x1001 sent to controller 0 by the combat_state_notification overrides of the solar
// NPC (TU24) and the NPC ship (TU43): the three callback arguments, then the system id.
struct FSpaceObjCombatStateMsg
{
    unsigned int Type;         // +0x00 CSNType
    unsigned int Param1;       // +0x04
    unsigned int Param2;       // +0x08
    unsigned int SystemId;     // +0x0c
};

static_assert(sizeof(FSpaceObjCombatStateMsg) == 0x10, "FSpaceObjCombatStateMsg size");

// FUN_06eb70a0 output (0x30 bytes): orientation first, then position.
struct FSpaceObjPlacement
{
    Matrix Orientation;        // +0x00
    Vector Position;           // +0x24
};

static_assert(sizeof(FSpaceObjPlacement) == 0x30, "FSpaceObjPlacement size");

class FContentSpaceObj
{
public:
    // FUN_06eb7190.
    explicit FContentSpaceObj(FContentSpaceObjParams const* Params);
    // FUN_06eb7230; slot 0 FUN_06eb7210.
    virtual ~FContentSpaceObj();
    // Retail's vtable has only the destructor. FContentShipBase defines virtual IsNpcShip() at
    // +0x04; FSolarNpc defines virtual IsInCombat() at +0x04.

    // FUN_06eb6e00 (only the second argument is read).
    void SendPendingComm(unsigned int Unused1, unsigned int Key, unsigned int Unused3);
    // FUN_06eb7070: -1 without a handle.
    unsigned int GetAffiliation() const;
    // FUN_06eb7080: -1 without a handle.
    int GetReputation() const;
    // FUN_06eb7090.
    void SetSystem(FSystemInfo* InSystem);
    // FUN_06eea860 (in TU25's range, ICF-shared getter).
    FSystemInfo* GetSystem() const
    {
        return System;
    }
    // FUN_06eb70a0.
    void GetPlacement(FSpaceObjPlacement* Out) const;
    // FUN_06eb70f0.
    void GetPosition(Vector* Out) const;
    // FUN_06eb7130.
    void GetOrientation(Matrix* Out) const;
    // FUN_06eb7170.
    void GetMotion(Vector& OutLinear, Vector& OutAngular) const;
    // FUN_06f34670 (in TU43's range).
    void DestroySpaceObj();
    // FUN_06eb7280: always returns true.
    bool FindOrAllocAttacker(unsigned int VictimId, FSpaceObjAttacker** Out);

    unsigned int SpaceObjId;                              // +0x04 0 from the ctor
    FContentPlayerStateBase* ReputationHandle;            // +0x08 not released by the dtor
    FContentBase* Base;                                   // +0x0c
    FSystemInfo* System;                                  // +0x10
    FSpaceObjAttacker Attackers[8];                       // +0x14
    TString<0x30> Nickname;                               // +0xd4 (chars at +0xd8)
    std::vector<FSpaceObjPendingComm> PendingComms;       // +0x108
};
