// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The UI sound table (DAT_0067a4a8[0x2d]) of the post-DataConfig bootstrap.

// FUN_00564520.
void InitUiSoundTable();
// FUN_00564610.
int FindUiSoundSlot(const char* Name);
// FUN_00564570 -- start only if no live source plays this slot.
void PlayUiSoundSlot(unsigned int Slot);
// FUN_00564650 -- as PlayUiSoundSlot, with a play key whose +4 flag byte has bit 0 set.
// Callers: NN_Preferences::SetSliderOption and one more site (0x0058b147).
void PlayUiSoundSlotFlag1(unsigned int Slot);
// FUN_005645c0 -- rewind a live source of this slot, else start one.
void PlayOrRewindUiSoundSlot(unsigned int Slot);
// FUN_005646e0 -- stop every live source of the cached archetype (an empty slot is passed
// through as null, like retail).
void StopUiSoundSlot(unsigned int Slot);
// FUN_00564700 -- "ui_rollover" (slot 0x1d), only while the intro menu is the current state or
// the local server tick is held.
void PlayUiRolloverSound();
// FUN_00564760 -- true when a live source plays any cached UI archetype. Caller: the
// MissionBriefing state's update.
bool IsAnyUiSoundPlaying();
const char* GetUiSoundSlotName(unsigned int Slot);
// FUN_00564540 -- release every cached archetype reference and null the table.
void ShutdownUiSoundTable();
