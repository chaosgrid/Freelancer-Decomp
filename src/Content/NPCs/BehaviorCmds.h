// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Content.dll TU50 (06f56f50-06f57cb0): the ship base class shared by NPC ships (TU43,
// ctor FUN_06f336a0, 0x5b0 bytes) and the player ship (TU46, ctor FUN_06f471d0, 0x190
// bytes). It derives from the TU20 space object (NpcBase.h) and from pub::AI::DirectiveCallback
// at +0x118, and runs a list of [ObjList] directives (ObjListDirective.h).
//
// Retail: ctor FUN_06f571a0, dtor body FUN_06f33d20 (in TU43's range), size 0x174.
// Primary vtable 06fbba08, secondary at +0x118 06fbba04. The player ship's vtables
// (06fbb040/06fbb044) reuse every slot but the dtor.

#include "NPCs/NpcBase.h"
#include "Missions/ObjListDirective.h"   // FObjListDirective, FMissionDirectiveList (+0x158)
#include "common_pub.h"   // pub::AI::BaseOp

#include <list>
#include <utility>
#include <vector>

struct FFormation;         // Encounters.h (TU31 formations.ini record, FUN_06efd1a0)

// FContentShipBase::FormationId (+0x154) of a ship in no formation group; AllocateFormationId
// (FUN_06efeb00) hands out the others.
unsigned int const GNoFormationId = 0xffffffffu;

// Parameter block read by FUN_06f571a0 (retail 0x78 bytes). No constructor of its own: the
// NPC ship's FUN_06f33500 (TU43) and the player ship's FUN_06efff70 (GameSupport) initialise
// it inline.
struct FContentShipParams : public FContentSpaceObjParams
{
    FFormation* Formation = nullptr;       // +0x44 -> ship +0x150
    unsigned int FormationSlot = 0xffffffffu; // +0x48 -> ship +0x14c
    unsigned int FormationId = GNoFormationId; // +0x4c -> ship +0x154
    std::vector<unsigned int> ClassIds;    // +0x50 -> ship +0x128
    // +0x60: the directives the NPC ship starts with (handed to SetDirectives). Its
    // out-of-line copy ctor is FUN_06ef7e30.
    FMissionDirectiveList Directives;
    unsigned int ControllerId = 0;         // +0x6c -> ship +0x170
    int Priority = 0;                      // +0x70 -> ship +0x168
    unsigned int ShipArchId = 0;           // +0x74 -> ship +0x138

    FContentShipParams() = default;
    // FUN_06f73680 is the implicit copy.
    FContentShipParams(FContentShipParams const& Other) = default;
    FContentShipParams& operator=(FContentShipParams const&) = delete;
    // FUN_06ef4ab0 is the implicit dtor.
    ~FContentShipParams() = default;
};

// The +0x118 secondary base is pub::AI::DirectiveCallback, dispatched by Common's
// IBehaviorManager when a queued AI directive ends (retail base vtable 06fbba1c). TU50
// implements it with FUN_06f57820, TU43 with 06f33f90.
class FContentShipBase : public FContentSpaceObj, public pub::AI::DirectiveCallback
{
public:
    // FUN_06f571a0.
    explicit FContentShipBase(FContentShipParams const* Params);
    // FUN_06f33d20 (called by the sdd 06f572f0).
    ~FContentShipBase() override;

    // Slot 1 (06ea1480; TU43 overrides with 06ea1460 = true): only FContentNpcShip answers
    // true, and the callers cast to it on that answer.
    virtual bool IsNpcShip();
    // Slot 2 (06f33cf0): byte +0x11c.
    virtual bool IsInCombat();
    // Slot 3 (06f57c30): &ActiveDirective.
    virtual FObjListDirective** GetActiveDirectiveSlot();
    // Slot 4 (06f57c40): Release and clear ActiveDirective.
    virtual void ReleaseActiveDirective();

    // DirectiveCallback slot 0 (06f57820): if the directive list is not empty, advance it.
    void OnDirectiveComplete(int CompletedBehavior, int DirectiveResult) override;

    // FUN_06f33e40 (in TU43's range): the reputation rank, truncated (ftol).
    int GetReputationRank() const;
    // FUN_06f57150.
    unsigned int GetCargoHoldSize() const;
    // FUN_06f57170.
    unsigned int GetCargoSpaceOccupied() const;
    // FUN_06f57310.
    void AddDamage(unsigned int const* AttackerId, float Amount);
    // FUN_06f57500.
    void GetTopDamagers(unsigned int* OutTopPlayer, unsigned int* OutTopAny) const;
    // FUN_06f575a0.
    bool HasClassId(unsigned int const* ClassId) const;
    // FUN_06f575d0: any member of the TU51 ship class is in ClassIds.
    bool IsInShipClass(unsigned int const* ShipClassId) const;
    // FUN_06f57640: the first member of this ship's formation (FUN_06f66220) when it is a
    // wingman, else this ship.
    FContentShipBase* GetFormationLeader();
    // FUN_06f576c0.
    void SetFormation(unsigned int InFormationId, unsigned int InFormationSlot,
                      FFormation* InFormation);
    // FUN_06f576f0. GameSupport msg 0x16 sets {entry ring, last or first ring of the lane in
    // the travel direction}; msg 0x17 clears both.
    void SetTradeLaneRings(unsigned int const* Value120, unsigned int const* Value124);
    // FUN_06f57710.
    bool IsInTradeLane() const;
    // FUN_06f57720.
    void GetTradeLaneRings(unsigned int* Out120, unsigned int* Out124) const;
    // FUN_06f57740: cancel the AI directive, release and drop every queued directive.
    void CancelDirectives();
    // FUN_06f577d0.
    void AddDirective(FObjListDirective* Directive);
    // FUN_06f57840: replace the directive list and the priority, then start it.
    void SetDirectives(std::list<FObjListDirective*> const* NewList, int InPriority);
    // FUN_06f57b00: drop the current directive and start the next one.
    void AdvanceDirective();
    // FUN_06f57c00.
    void RestartDirectives();
    // FUN_06f579c0: cancel the AI directive, take Leader's directive list (shared) and start
    // it; a leading Dock directive is pointed at this ship's own docking terminal when it is
    // in the docking list.
    void FollowDirectivesOf(FContentShipBase* Leader);
    // FUN_06f14400 (in TU38's range): each copied directive AddRef'ed (the caller releases
    // them).
    std::list<FObjListDirective*> CopyDirectives() const;
    // FUN_06f57c20.
    bool HasNoDirectives() const;
    // FUN_06f57c60.
    bool SubmitDirective(pub::AI::BaseOp* Op);
    // FUN_06f57c80.
    int GetPriority() const;
    // FUN_06f57c90.
    void SetPriority(int const* InPriority);

    // +0x11c: NPC: set from combat_state_notification; player: set while hostile contacts
    // exist.
    unsigned char bInCombat = 0;
    // +0x11d: the ship (formation leader) is claimed as a patrol cargo-scan target; cleared
    // for a player entering a system.
    unsigned char bScanTarget = 0;
    unsigned int TradeLaneEntryRing = 0;                          // +0x120 0 = not in a lane
    unsigned int TradeLaneEndRing = 0;                            // +0x124
    std::vector<unsigned int> ClassIds;                           // +0x128
    unsigned int ShipArchId;                                      // +0x138
    std::vector<std::pair<unsigned int, float>> Damage;           // +0x13c
    // +0x14c: the ship's slot in Formation, 0 = the leader (CollectFormationMembers sorts by
    // it).
    unsigned int FormationSlot;
    // +0x150.
    FFormation* Formation;
    // +0x154: the id of the formation group, -1 = none (from AllocateFormationId
    // FUN_06efeb00; CollectFormationMembers gathers a group by it).
    unsigned int FormationId;
    // +0x158: the queued directives, one reference held on each (retail unwind FUN_06ecfa90).
    // Torn down after the dtor body's ActiveDirective release, before Damage / ClassIds.
    FMissionDirectiveList Directives;                             // +0x158
    // +0x164: Deviation: retail leaves it unset in the ctor (every user stores end() first);
    // the port starts it at end() so the iterator is never singular.
    std::list<FObjListDirective*>::iterator CurrentDirective;
    int Priority;                                                 // +0x168
    FObjListDirective* ActiveDirective = nullptr;                 // +0x16c
    // +0x170: the owning controller (mission, SpacePop, ...), the target of the NPC ship's
    // 0x1006/0x100a/0x1011 messages.
    unsigned int ControllerId;
};
