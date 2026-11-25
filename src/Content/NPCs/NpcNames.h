// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Content.dll TU42, the NPC name part (06f31650-06f32000): the name request, the name
// builder over the TU48 faction name tables (FactionProps.h), and the reputation handle an
// NPC ship gets from the name (vtable 06fb940c). The rest of TU42 (npcships.ini, [NPC]
// blocks, name value readers) is NpcShips.h.

#include "Core/ContentPlayerState.h"   // FContentPlayerStateBase
#include "common_text.h"          // FmtStr

struct FMsnNpc;                   // NpcShips.h

// The name request read by BuildNpcName (0x20 bytes).
struct FNpcNameRequest
{
    // FUN_06f31650.
    FNpcNameRequest();
    // FUN_06f31670 (only caller TU29; the argument order of the retail call). +0x14..+0x1c
    // are left unwritten, as in retail.
    FNpcNameRequest(int InNameIndex, int InFormationSize, int InEscortNumber, int InDesig,
                    int InDesigNumber);

    // +0x00: index into FFactionNames::NameIds; non-zero builds the ship-type name
    // (FFactionNames::GetNameId), zero the pilot name.
    int NameIndex;
    int Desig;                  // +0x04 formation designation; 0 picks one (PickFormationDesig)
    int DesigNumber;            // +0x08 its number (used with a non-zero Desig)
    // +0x0c: the number of ships in the formation (TU29 NameFormationBlocks passes the block
    // list's size). 1 (a lone ship) selects the lone-ship patterns in BuildNpcName.
    int FormationSize;
    // +0x10: the ship's index in its formation, 0 = the leader. Non-zero selects 0x3f1d /
    // 0x3f1e, the latter appending it as the escort number (resources.dll 0x3f1e =
    // "%s0 %d0 %s1 %s2" with %s0 = 0x3f1f "Escort").
    int EscortNumber;
    unsigned char Field_0x14;   // +0x14 0 from FUN_06f31650 only; no reader in TU42
    int RankLevel;              // +0x18 FFactionNames::GetRankDesig; non-zero -> 0x3f22
    unsigned int ShipArchId;    // +0x1c pub::GetType / pub::GetShipArchSTRID
};

// The name built by BuildNpcName and turned into a reputation by CreateNpcNameReputation
// (0x38 bytes on the callers' stacks).
struct FNpcNameParams
{
    FmtStr First;                       // +0x00 the Reputation::Alloc name
    FmtStr Second;                      // +0x18 the Reputation::Alloc nickname
    // +0x30: 1 male / 2 female; passed to PickPilotName and PickSpaceCostume.
    int Gender = 0;
    unsigned int Affiliation = 0xffffffffu;   // +0x34 written by BuildNpcName
};

// The reputation handle of a named NPC (0xc bytes, vtable 06fb940c).
class FNpcNameReputation : public FContentPlayerStateBase
{
public:
    // Inlined in FUN_06f31910 and FUN_06f31d40, inside the new-expression's EH states.
    explicit FNpcNameReputation(FNpcNameParams const* Params);
    // FUN_06f316c0 (called by the sdd 06f316a0): the name's pool entries are queued for
    // release (not immediate).
    ~FNpcNameReputation() override;

    // The three readers of GCS FUN_06eeb210; each reads the reputation's name back.
    // FUN_06f31ec0 (the FUN_06f32350 body: 1 and false for a pattern without one).
    bool GetFormationDesig(int* Out) const;
    // FUN_06f32020 (the FUN_06f323c0 body: -1 and false for a pattern without one).
    bool GetFormationDesigNumber(int* Out) const;
    // FUN_06f32190: the name is fetched but not read; *Out = rand() * 5 / 32768 + 1
    // (FUN_06f32420); false.
    bool RollNameValue(int* Out) const;
};

// DAT_06fd0884: the number of live FNpcNameReputation objects (written only).
extern int GNpcNameReputationCount;

// FUN_06f319b0: the faction record is not null-tested.
bool __cdecl PickNpcNameGender(unsigned int Affiliation, FNpcNameParams* Params);

// FUN_06f319d0: GetFactionNames is not null-tested; the picked names are reserved. Always
// true.
bool __cdecl BuildNpcName(unsigned int Affiliation, FNpcNameRequest const* Request,
                          FNpcNameParams* Out, bool bNamedLeader);

// FUN_06f31910: true when the handle was created.
bool __cdecl CreateNpcNameReputation(FNpcNameParams const* Params, FContentPlayerStateBase** Out);

// FUN_06f31d40: the same from a mission [NPC] block: an empty name and the nickname mad lib
// Npc->IndividualName.
bool __cdecl CreateMsnNpcReputation(FMsnNpc const* Npc, FContentPlayerStateBase** Out);
