// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Content.dll TU24 (06ed5390-06ed5a70): solar NPCs — the [Object] entries of a system ini
// that carry a pilot / behavior / reputation. SystemInfo's full load (TU58 FUN_06f748a0)
// builds one per [Object] and appends it to FSystemInfo +0x88.

#include "NPCs/NpcBase.h"
#include "Core/ContentPlayerState.h"   // FContentPlayerStateBase
#include "common_net.h"           // CommReferrable
#include "common_pub.h"           // pub::AI::Personality, pub::AI::ContentCallback

class INI_Reader;

// Parameter block of one [Object] (retail 0x470 bytes, built inline by TU58): CommReferrable
// at +0 (vtable 06fbbf30, no overrides), the space object parameters at +0x48. Its only
// cleanup is the parameters' handle release.
struct FSolarNpcDesc : public CommReferrable, public FContentSpaceObjParams
{
    int StateGraph = -1;                     // +0x8c "behavior" (pub::StateGraph::get_state_graph)
    pub::AI::Personality Personality;        // +0x90 "pilot"
    bool bHasPersonality = false;            // +0x468 set when "pilot" resolved
    unsigned int ReputationGroup = 0xffffffffu; // +0x46c "reputation" (GetReputationGroup)
};

// FUN_06ed5590: returns false only when no key matched (the CommReferrable fallback
// included).
bool __cdecl ParseSolarNpcKey(INI_Reader* Reader, FSolarNpcDesc* Desc);

// The reputation handle a solar NPC creates for itself: base ctor FUN_06eb6be0, own vtable
// 06fb61a0. No members of its own. The inlined ctor runs GetReputation and SetAffiliation
// inside the new-expression's EH state, so a throw from either frees the block.
struct FSolarNpcReputation : public FContentPlayerStateBase
{
    FSolarNpcReputation(char const* Nickname, unsigned int const& Group);
    ~FSolarNpcReputation() override = default;
};

// Retail 0x548 bytes, vtable 06fb61e0; CommReferrable at +0x118 (no overrides);
// ContentCallback at +0x160 (only combat_state_notification overridden).
class FSolarNpc : public FContentSpaceObj, public CommReferrable, public pub::AI::ContentCallback
{
public:
    // FUN_06ed5720.
    explicit FSolarNpc(FSolarNpcDesc const* Desc);
    // sdd 06ed5910 -> FUN_06eb7230 (the TU20 dtor only).
    ~FSolarNpc() override = default;

    // Slot 1, 06ed59f0 (not disassembled by Ghidra). TODO: slot name unknown.
    virtual bool IsInCombat();

    // ContentCallback +0x1c, 06ed5a00.
    void combat_state_notification(CSNType Type, uint Param1, uint Param2) override;

    // FUN_06ed5940.
    void DeleteReputationHandle();
    // FUN_06ed5960.
    void SubmitPersonalityState(bool bSubmit);

    // +0x164: 0 from the ctor; combat_state_notification stores (Type == 0).
    // TODO: meaning of the CSN type values unknown.
    bool bInCombat = false;
    int StateGraph;                          // +0x168
    pub::AI::Personality Personality;        // +0x16c
    bool bHasPersonality;                    // +0x544
};
