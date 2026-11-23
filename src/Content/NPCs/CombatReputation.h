// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Content.dll TU56 (06f6fd90-06f70dd0): reputation reactions to combat, driven by the
// SpaceSupport controller (TU53):
//   - message 0xd (sub-object damage): a player hitting an object builds up hostility in the
//     attacker slots of the player's ship; past 25 / 50 the victim side is told (message 0x1005
//     to controller 0) and at 50 the victim (and its formation) turn hostile (FUN_06f6ff90);
//   - message 0x39 (object destroyed): the killer player's reputation with the victim's faction
//     changes by that faction's event rate 0 (FUN_06f704a0);
//   - the heartbeat (message 0x20; SpaceSupport 06f5d6c6): NPCs near each player that like a
//     second NPC turn hostile to whoever that NPC is hostile to (FUN_06f70580, message 0x1003
//     to SpaceSupport).
// The TU's data (the scan table DAT_06fd1d58 and DAT_06fd1d50) is private to
// CombatReputation.cpp.

class FContentSpaceObj;          // NpcBase.h (TU20)
struct FSubObjDamageEvent;       // server_gameobject.h (message 0xd)

// FUN_06f6ff90.
void __cdecl HandleDamageReputation(FSubObjDamageEvent const* Event);

// FUN_06f70490: a thiscall member of the SpaceSupport controller in retail (this not read).
// FSpaceSupport is private to SpaceSupport.cpp, so the port has it as a free function.
void __cdecl OnDamageReputation(FSubObjDamageEvent const* Event);

// FUN_06f704a0: a thiscall member of the SpaceSupport controller in retail (this not read).
// KillerId is the killer (or the top damaging player) chosen by the caller.
void __cdecl ApplyKillReputation(FContentSpaceObj const* Ship, unsigned int const* KillerId);

// FUN_06f70580: the argument is not read.
void __cdecl UpdateNpcReputations(float Unused);

// FUN_06f70480: a tail jump to FUN_06f70580 (called with the heartbeat period DAT_06fce770).
void __cdecl OnHeartbeatReputation(float HeartbeatPeriod);
