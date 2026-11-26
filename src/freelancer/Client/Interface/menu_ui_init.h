// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The UI sound table (DAT_0067a4a8[0x2d]) of the post-DataConfig bootstrap.

// The slots of the UI sound table (DAT_006149e8, FUN_00564520 ..): the sound nickname each slot
// resolves to is in the comment.
enum EUiSoundSlot
{
    kUiSoundSelectItem = 0,                   // "ui_select_item"
    kUiSoundItemSelect = 1,                   // "ui_item_select"
    kUiSoundWindowOpen = 2,                   // "ui_window_open"
    kUiSoundOpenInfocard = 3,                 // "ui_open_infocard"
    kUiSoundFilterSelect = 4,                 // "ui_filter_select"
    kUiSoundNavMapExpand = 5,                 // "ui_nav_map_expand"
    kUiSoundHudZoomIn = 6,                    // "hud_zoom_in"
    kUiSoundHudZoomOut = 7,                   // "hud_zoom_out"
    kUiSoundNavMapFade = 8,                   // "ui_nav_map_fade"
    kUiSoundNavMapSubmenu = 9,                // "ui_nav_map_submenu"
    kUiSoundHudWaypoint = 10,                 // "hud_waypoint"
    kUiSoundTypingCharacters = 11,            // "ui_typing_characters"
    kUiSoundNnExpansioncontraction = 12,      // "ui_nn_expansioncontraction"
    kUiSoundBeginScan = 13,                   // "ui_begin_scan"
    kUiSoundEndScan = 14,                     // "ui_end_scan"
    kUiSoundNewStoryStar = 15,                // "ui_new_story_star"
    kUiSoundLoadCargo = 16,                   // "ui_load_cargo"
    kUiSoundEquipDetach = 17,                 // "ui_equip_detach"
    kUiSoundEquipMount01 = 18,                // "ui_equip_mount01"
    kUiSoundEquipMount02 = 19,                // "ui_equip_mount02"
    kUiSoundEquipMount03 = 20,                // "ui_equip_mount03"
    kUiSoundEquipMount04 = 21,                // "ui_equip_mount04"
    kUiSoundEquipMount05 = 22,                // "ui_equip_mount05"
    kUiSoundHudWeaponSelect = 23,             // "hud_weapon_select"
    kUiSoundHudSubtarget = 24,                // "hud_subtarget"
    kUiSoundIncomingMissile = 25,             // "ui_incoming_missile"
    kUiSoundExecuteTransaction = 26,          // "ui_execute_transaction"
    kUiSoundMotionSwish = 27,                 // "ui_motion_swish"
    kUiSoundToggleMouseFlight = 28,           // "ui_toggle_mouse_flight"
    kUiSoundRollover = 29,                    // "ui_rollover"
    kUiSoundVoiceTest = 30,                   // "ui_voice_test"
    kUiSoundEffectsTest = 31,                 // "ui_effects_test"
    kUiSoundMusicTest = 32,                   // "ui_music_test"
    kUiSoundInterfaceTest = 33,               // "ui_interface_test"
    kUiSoundAmbianceTest = 34,                // "ui_ambiance_test"
    kUiSoundTargetChange = 35,                // "ui_target_change"
    kUiSoundTargetExpandBrackets = 36,        // "ui_target_expand_brackets"
    kUiSoundBuyCommodity = 37,                // "ui_buy_commodity"
    kUiSoundReceiveMoney = 38,                // "ui_receive_money"
    kUiSoundGainLevel = 39,                   // "ui_gain_level"
    kUiSoundUseNanobotsSuccess = 40,          // "ui_use_nanobots_success"
    kUiSoundUseShieldBatterySuccess = 41,     // "ui_use_shield_battery_success"
    kUiSoundUseNanobotsFailure = 42,          // "ui_use_nanobots_failure"
    kUiSoundUseShieldBatteryFailure = 43,     // "ui_use_shield_battery_failure"
    kUiSoundHudCommWindow = 44                // "hud_comm_window"
};

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
