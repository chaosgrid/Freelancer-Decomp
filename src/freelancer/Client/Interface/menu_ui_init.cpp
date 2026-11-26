// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/menu_ui_init.h"

#include "Common/input_manager.h"
#include "Common/Audio/menu_sound_arch.h"
#include "Interface/menu_ui_widgets.h"
#include "Common/Graphics/texture_shapes.h"
#include "Interface/win3d_engine_submit.h"

#include "Core/client_state.h"
#include "common_id.h"
#include "Core/globals.h"
#include "DALib.h"
#include "soundstreamer.h"
#include "dacom.h"
#include "Space/Cameras/space_camera.h"

#include <io.h>
#include <string.h>
#include <windows.h>

namespace
{
    static const int kUiSoundTableCount = 0x2d;

    // DAT_006149e8.
    static const char* kUiSoundNames[kUiSoundTableCount] = {
        "ui_select_item",                    // 0
        "ui_item_select",                    // 1
        "ui_window_open",                    // 2
        "ui_open_infocard",                  // 3
        "ui_filter_select",                  // 4
        "ui_nav_map_expand",                 // 5
        "hud_zoom_in",                       // 6
        "hud_zoom_out",                      // 7
        "ui_nav_map_fade",                   // 8
        "ui_nav_map_submenu",                // 9
        "hud_waypoint",                      // 10
        "ui_typing_characters",              // 11
        "ui_nn_expansioncontraction",        // 12
        "ui_begin_scan",                     // 13
        "ui_end_scan",                       // 14
        "ui_new_story_star",                 // 15
        "ui_load_cargo",                     // 16
        "ui_equip_detach",                   // 17
        "ui_equip_mount01",                  // 18
        "ui_equip_mount02",                  // 19
        "ui_equip_mount03",                  // 20
        "ui_equip_mount04",                  // 21
        "ui_equip_mount05",                  // 22
        "hud_weapon_select",                 // 23
        "hud_subtarget",                     // 24
        "ui_incoming_missile",               // 25
        "ui_execute_transaction",            // 26
        "ui_motion_swish",                   // 27
        "ui_toggle_mouse_flight",            // 28
        "ui_rollover",                       // 29
        "ui_voice_test",                     // 30
        "ui_effects_test",                   // 31
        "ui_music_test",                     // 32
        "ui_interface_test",                 // 33
        "ui_ambiance_test",                  // 34
        "ui_target_change",                  // 35
        "ui_target_expand_brackets",         // 36
        "ui_buy_commodity",                  // 37
        "ui_receive_money",                  // 38
        "ui_gain_level",                     // 39
        "ui_use_nanobots_success",           // 40
        "ui_use_shield_battery_success",     // 41
        "ui_use_nanobots_failure",           // 42
        "ui_use_shield_battery_failure",     // 43
        "hud_comm_window",                   // 44
    };

    // DAT_0067a4a8 -- cached SoundArch pointers.
    void* GUiSoundTable[kUiSoundTableCount] = {};

}

void InitUiSoundTable()
{
    // FUN_00564520.
    for (int Index = 0; Index < kUiSoundTableCount; ++Index)
        GUiSoundTable[Index] = nullptr;
}

int FindUiSoundSlot(const char* Name)
{
    // FUN_00564610 (cdecl). No test of Name.
    for (int Index = 0; Index < kUiSoundTableCount; ++Index)
    {
        if (_stricmp(kUiSoundNames[Index], Name) == 0)
            return Index;
    }
    return -1;
}

const char* GetUiSoundSlotName(unsigned int Slot)
{
    if (Slot >= static_cast<unsigned int>(kUiSoundTableCount))
        return nullptr;
    return kUiSoundNames[Slot];
}

void PlayUiSoundSlot(unsigned int Slot)
{
    // FUN_00564570: no range check and no init latch. An empty cache slot is filled lazily, with
    // no null test on the lookup.
    if (GUiSoundTable[Slot] == nullptr)
    {
        char const* const Name = kUiSoundNames[Slot];
        ID_String NicknameId;
        NicknameId.Id = CreateID(Name);
        GUiSoundTable[Slot] = LookupClientSoundArch(&NicknameId);
        AcquireClientSoundArch(GUiSoundTable[Slot]);
    }
    PlayClientSoundArchIfIdle(GUiSoundTable[Slot], nullptr, nullptr);
}

void PlayUiSoundSlotFlag1(unsigned int Slot)
{
    // FUN_00564650: a 0x18-byte play key, the +0x04 byte (flags & 0xf8) | 1. Deviation: retail
    // keeps bits 3..7 of that byte and the dword's upper three bytes from stack garbage; the port
    // zeroes them.
    if (GUiSoundTable[Slot] == nullptr)
    {
        char const* const Name = kUiSoundNames[Slot];
        ID_String NicknameId;
        NicknameId.Id = CreateID(Name);
        GUiSoundTable[Slot] = LookupClientSoundArch(&NicknameId);
        AcquireClientSoundArch(GUiSoundTable[Slot]);
    }
    unsigned Key[6] = {0, 1, 0, 0, 0, 0};
    PlayClientSoundArchIfIdle(GUiSoundTable[Slot], Key, nullptr);
}

void PlayOrRewindUiSoundSlot(unsigned int Slot)
{
    // FUN_005645c0. No range check, no init latch.
    if (GUiSoundTable[Slot] == nullptr)
    {
        char const* const Name = kUiSoundNames[Slot];
        ID_String NicknameId;
        NicknameId.Id = CreateID(Name);
        GUiSoundTable[Slot] = LookupClientSoundArch(&NicknameId);
        AcquireClientSoundArch(GUiSoundTable[Slot]);
    }
    // It never stops anything.
    PlayOrRewindClientSoundArch(GUiSoundTable[Slot], nullptr, nullptr);
}

void StopUiSoundSlot(unsigned int Slot)
{
    // FUN_005646e0: no lazy fill, no range check.
    StopClientSoundArch(GUiSoundTable[Slot]);
}

void PlayUiRolloverSound()
{
    // FUN_00564700: a plain start, not the if-idle variant.
    if (!IsClientIntroEnvironment() && !IsClientServerTickPaused())
        return;
    unsigned int const Slot = kUiSoundRollover;
    if (GUiSoundTable[Slot] == nullptr)
    {
        char const* const Name = kUiSoundNames[Slot];
        ID_String NicknameId;
        NicknameId.Id = CreateID(Name);
        GUiSoundTable[Slot] = LookupClientSoundArch(&NicknameId);
        AcquireClientSoundArch(GUiSoundTable[Slot]);
    }
    PlayClientSoundArch(GUiSoundTable[Slot], nullptr, nullptr, 0);
}

bool IsAnyUiSoundPlaying()
{
    // FUN_00564760.
    for (int Index = 0; Index < kUiSoundTableCount; ++Index)
    {
        if (GUiSoundTable[Index] == nullptr)
            continue;
        if (CountClientSoundArchSources(GUiSoundTable[Index]) != 0)
            return true;
    }
    return false;
}

// FUN_00564540. No init latch: the table is static storage, zero before InitUiSoundTable runs.
void ShutdownUiSoundTable()
{
    for (int Index = 0; Index < kUiSoundTableCount; ++Index)
    {
        if (GUiSoundTable[Index] == nullptr)
            continue;
        ReleaseClientSoundArch(GUiSoundTable[Index]);
        GUiSoundTable[Index] = nullptr;
    }
}
