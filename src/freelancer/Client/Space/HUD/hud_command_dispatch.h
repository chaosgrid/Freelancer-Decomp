// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail Client/Interface/KeyMap.cpp — HUD / KeyCmd message post + handler tree.

// Event::Handler is the single canonical command receiver.
class EventHandler;
using FHudCmdHandler = EventHandler;

// The KeyMap command ids: the index of the nickname in retail's KeyMap table
// (PTR_s_USER_NONE_00614dd8, 0xcc entries, FKeyCmd::NicknameId), which is what the handler
// tree dispatches as the event id. Names as in the table (menu_keymap_nicknames.inc).
enum EUserCmd : unsigned
{
    USER_NONE                       = 0x00,
    USER_KEY                        = 0x01,
    USER_CHAR                       = 0x02,
    USER_BUTTON                     = 0x03,
    USER_WHEEL                      = 0x04,
    USER_MOVE                       = 0x05,
    USER_BUTTON0                    = 0x06,
    USER_BUTTON1                    = 0x07,
    USER_BUTTON2                    = 0x08,
    USER_BUTTON3                    = 0x09,
    USER_BUTTON4                    = 0x0a,
    USER_BUTTON5                    = 0x0b,
    USER_BUTTON6                    = 0x0c,
    USER_BUTTON7                    = 0x0d,
    USER_EVENTS                     = 0x0e,
    USER_CANCEL                     = 0x0f,
    USER_STACKABLE_CANCEL           = 0x10,
    USER_ENTER                      = 0x11,
    USER_NO                         = 0x12,
    USER_YES                        = 0x13,
    USER_PAUSE                      = 0x14,
    USER_RESET                      = 0x15,
    USER_SAVE_GAME                  = 0x16,
    USER_FULLSCREEN                 = 0x17,
    USER_USE_ITEM                   = 0x18,
    USER_ACTIVATE_ITEM              = 0x19,
    USER_NEXT_ITEM                  = 0x1a,
    USER_PREV_ITEM                  = 0x1b,
    USER_SET_TARGET                 = 0x1c,
    USER_NEXT_TARGET                = 0x1d,
    USER_PREV_TARGET                = 0x1e,
    USER_NEXT_WEAPON_SUBTARGET      = 0x1f,
    USER_PREV_WEAPON_SUBTARGET      = 0x20,
    USER_NEXT_SUBTARGET             = 0x21,
    USER_PREV_SUBTARGET             = 0x22,
    USER_CLEAR_TARGET               = 0x23,
    USER_NEXT_ENEMY                 = 0x24,
    USER_PREV_ENEMY                 = 0x25,
    USER_CLOSEST_ENEMY              = 0x26,
    USER_ZOOM_IN                    = 0x27,
    USER_ZOOM_OUT                   = 0x28,
    USER_ZOOM_TOGGLE                = 0x29,
    USER_X_ROTATE                   = 0x2a,
    USER_X_UNROTATE                 = 0x2b,
    USER_Y_ROTATE                   = 0x2c,
    USER_Y_UNROTATE                 = 0x2d,
    USER_Z_ROTATE                   = 0x2e,
    USER_Z_UNROTATE                 = 0x2f,
    USER_VIEW_RESET                 = 0x30,
    USER_COCKPIT_CAMERA_MODE        = 0x31,
    USER_LOOK_ROTATE_CAMERA_LEFT    = 0x32,
    USER_LOOK_ROTATE_CAMERA_RIGHT   = 0x33,
    USER_LOOK_ROTATE_CAMERA_UP      = 0x34,
    USER_LOOK_ROTATE_CAMERA_DOWN    = 0x35,
    USER_LOOK_ROTATE_CAMERA_RESET   = 0x36,
    USER_CHASE_CAMERA_MODE          = 0x37,
    USER_REARVIEW_CAMERA_MODE       = 0x38,
    USER_REARVIEW_CAMERA_MODE_OFF   = 0x39,
    USER_AFTERBURN                  = 0x3a,
    USER_AFTERBURN_OFF              = 0x3b,
    USER_AFTERBURN_ON               = 0x3c,
    USER_TOGGLE_AUTO_AVOIDANCE      = 0x3d,
    USER_TOGGLE_AUTO_LEVEL          = 0x3e,
    USER_TOGGLE_LEVEL_CAMERA        = 0x3f,
    USER_WARP                       = 0x40,
    USER_ENGINE_TOGGLE              = 0x41,
    USER_SET_THROTTLE               = 0x42,
    USER_INC_THROTTLE               = 0x43,
    USER_DEC_THROTTLE               = 0x44,
    USER_CRUISE                     = 0x45,
    USER_THROTTLE_0                 = 0x46,
    USER_THROTTLE_10                = 0x47,
    USER_THROTTLE_20                = 0x48,
    USER_THROTTLE_25                = 0x49,
    USER_THROTTLE_30                = 0x4a,
    USER_THROTTLE_40                = 0x4b,
    USER_THROTTLE_50                = 0x4c,
    USER_THROTTLE_60                = 0x4d,
    USER_THROTTLE_70                = 0x4e,
    USER_THROTTLE_75                = 0x4f,
    USER_THROTTLE_80                = 0x50,
    USER_THROTTLE_90                = 0x51,
    USER_THROTTLE_100               = 0x52,
    USER_RADAR                      = 0x53,
    USER_CONTACT_LIST               = 0x54,
    USER_RADAR_ZOOM                 = 0x55,
    USER_SWITCH_TO_PLAYER_SHIP      = 0x56,
    USER_SWITCH_TO_WEAPON_LIST      = 0x57,
    USER_SWITCH_TO_TARGET           = 0x58,
    USER_FORMATION_LIST             = 0x59,
    USER_RADAR_ZOOM_IN              = 0x5a,
    USER_RADAR_ZOOM_OUT             = 0x5b,
    USER_RADIO                      = 0x5c,
    USER_CHAT                       = 0x5d,
    USER_STATUS                     = 0x5e,
    USER_TARGET                     = 0x5f,
    USER_STATUS_MODE                = 0x60,
    USER_TOGGLE_MAINFRAME           = 0x61,
    USER_NN                         = 0x62,
    USER_NAV_MAP                    = 0x63,
    USER_NAVMAP                     = 0x64,
    USER_PLAYER_STATS               = 0x65,
    USER_INVENTORY                  = 0x66,
    USER_STORY_STAR                 = 0x67,
    USER_CHAT_WINDOW                = 0x68,
    USER_GROUP_WINDOW               = 0x69,
    USER_HELP                       = 0x6a,
    USER_INVENTORY_CLOSE            = 0x6b,
    NN_TOGGLE_OPEN                  = 0x6c,
    USER_MINIMIZE_HUD               = 0x6d,
    USER_DISPLAY_LAST_OBJECTIVE     = 0x6e,
    USER_COLLECT_LOOT               = 0x6f,
    USER_ACTIONS                    = 0x70,
    USER_MANEUVER                   = 0x71,
    USER_SET_MANEUVER_DIRECTION     = 0x72,
    USER_MANEUVER_WINDOW            = 0x73,
    USER_MANEUVER_TRAIL             = 0x74,
    USER_MANEUVER_AFTERBURNER       = 0x75,
    USER_MANEUVER_EVADE             = 0x76,
    USER_MANEUVER_ENGINEKILL        = 0x77,
    USER_MANEUVER_BRAKE_REVERSE     = 0x78,
    USER_MANEUVER_DOCK              = 0x79,
    USER_MANEUVER_GOTO              = 0x7a,
    USER_MANEUVER_FACE              = 0x7b,
    USER_MANEUVER_CRUISE            = 0x7c,
    USER_MANEUVER_TRADE_LANE        = 0x7d,
    USER_MANEUVER_DRASTIC_EVADE     = 0x7e,
    USER_MANEUVER_FORMATION         = 0x7f,
    USER_MANEUVER_STRAFE            = 0x80,
    USER_MANEUVER_TRAIL_CLOSER      = 0x81,
    USER_MANEUVER_TRAIL_FARTHER     = 0x82,
    USER_MANEUVER_CORKSCREW_EVADE   = 0x83,
    USER_MANEUVER_SLIDE_EVADE       = 0x84,
    USER_MANEUVER_SLIDE_EVADE_LEFT  = 0x85,
    USER_MANEUVER_SLIDE_EVADE_RIGHT = 0x86,
    USER_MANEUVER_SLIDE_EVADE_UP    = 0x87,
    USER_MANEUVER_SLIDE_EVADE_DOWN  = 0x88,
    USER_ACTIVATE_MANEUVER          = 0x89,
    USER_SCAN_CARGO                 = 0x8a,
    USER_TRACTOR_BEAM               = 0x8b,
    USER_JAMMER                     = 0x8c,
    USER_STEALTH                    = 0x8d,
    USER_CLOAK                      = 0x8e,
    USER_REPAIR_HEALTH              = 0x8f,
    USER_REPAIR_SHIELD              = 0x90,
    USER_WEAPON_GROUP1              = 0x91,
    USER_WEAPON_GROUP2              = 0x92,
    USER_WEAPON_GROUP3              = 0x93,
    USER_WEAPON_GROUP4              = 0x94,
    USER_WEAPON_GROUP5              = 0x95,
    USER_WEAPON_GROUP6              = 0x96,
    USER_TOGGLE_WEAPON1             = 0x97,
    USER_TOGGLE_WEAPON2             = 0x98,
    USER_TOGGLE_WEAPON3             = 0x99,
    USER_TOGGLE_WEAPON4             = 0x9a,
    USER_TOGGLE_WEAPON5             = 0x9b,
    USER_TOGGLE_WEAPON6             = 0x9c,
    USER_TOGGLE_WEAPON7             = 0x9d,
    USER_TOGGLE_WEAPON8             = 0x9e,
    USER_TOGGLE_WEAPON9             = 0x9f,
    USER_TOGGLE_WEAPON10            = 0xa0,
    USER_FIRE_WEAPON1               = 0xa1,
    USER_FIRE_WEAPON2               = 0xa2,
    USER_FIRE_WEAPON3               = 0xa3,
    USER_FIRE_WEAPON4               = 0xa4,
    USER_FIRE_WEAPON5               = 0xa5,
    USER_FIRE_WEAPON6               = 0xa6,
    USER_FIRE_WEAPON7               = 0xa7,
    USER_FIRE_WEAPON8               = 0xa8,
    USER_FIRE_WEAPON9               = 0xa9,
    USER_FIRE_WEAPON10              = 0xaa,
    USER_FIRE_WEAPON_GROUP1         = 0xab,
    USER_FIRE_WEAPON_GROUP2         = 0xac,
    USER_FIRE_WEAPON_GROUP3         = 0xad,
    USER_FIRE_WEAPON_GROUP4         = 0xae,
    USER_FIRE_WEAPON_GROUP5         = 0xaf,
    USER_FIRE_WEAPON_GROUP6         = 0xb0,
    USER_ASSIGN_WEAPON_GROUP1       = 0xb1,
    USER_ASSIGN_WEAPON_GROUP2       = 0xb2,
    USER_ASSIGN_WEAPON_GROUP3       = 0xb3,
    USER_ASSIGN_WEAPON_GROUP4       = 0xb4,
    USER_ASSIGN_WEAPON_GROUP5       = 0xb5,
    USER_ASSIGN_WEAPON_GROUP6       = 0xb6,
    USER_REMAPPABLE_LEFT            = 0xb7,
    USER_REMAPPABLE_RIGHT           = 0xb8,
    USER_REMAPPABLE_UP              = 0xb9,
    USER_REMAPPABLE_DOWN            = 0xba,
    USER_FIRE_FORWARD               = 0xbb,
    USER_LAUNCH_MISSILES            = 0xbc,
    USER_LAUNCH_MINES               = 0xbd,
    USER_LAUNCH_COUNTERMEASURES     = 0xbe,
    USER_AUTO_TURRET                = 0xbf,
    USER_LAUNCH_TORPEDOS            = 0xc0,
    USER_LAUNCH_CRUISE_DISRUPTORS   = 0xc1,
    USER_TURN                       = 0xc2,
    USER_NEXT_OBJECT                = 0xc3,
    USER_PREV_OBJECT                = 0xc4,
    USER_EXIT_GAME                  = 0xc5,
    USER_MANEUVER_FREE_FLIGHT       = 0xc6,
    USER_FIRE_WEAPONS               = 0xc7,
    USER_TURN_SHIP                  = 0xc8,
    USER_GROUP_INVITE               = 0xc9,
    USER_TRADE_REQUEST              = 0xca,
    USER_SCREEN_SHOT                = 0xcb,
};

// DAT_0067c278 -- the record of the event being dispatched (seven dwords). FUN_00576410 writes
// the id at +0; the poster fills the rest before posting. Readers reach it through
// FUN_00576240 (HudCommandRecord). Field meanings as the Win3D input handler FUN_0059d140 and
// the key dispatcher FUN_0059dfd0 read them:
struct FHudCmdRecord
{
    unsigned MsgId;          // +0x00
    unsigned Key;            // +0x04 -- key code (event 1)
    unsigned Modifiers;      // +0x08 -- DAT_0067c280: Shift 1 / Ctrl 4 / Alt 0x10 (FUN_005763b0)
    unsigned WindowMessage;  // +0x0c -- DAT_0067c284: raw window message (FUN_00577850)
    unsigned Arg10;          // +0x10 -- DAT_0067c288: wParam (event 2 first argument)
    unsigned Arg14;          // +0x14 -- DAT_0067c28c: lParam (event 2 second argument)
    unsigned char bKeyDown;  // +0x18 -- DAT_0067c290: key down (FUN_00577850) / set by FUN_005763b0
    unsigned char bRepeat;   // +0x19 -- DAT_0067c291: auto-repeat key event (FUN_00577850)
    unsigned char bDoubleClick; // +0x1a -- mouse events: double click (FUN_0059d140 0059d184)
    unsigned char Byte1B;    // +0x1b
};

// FUN_00576240 -- &DAT_0067c278.
FHudCmdRecord* HudCommandRecord();

// FUN_005763b0(msgId, bSkipMods): if bSkipMods==0 capture Shift/Ctrl/Alt into
// DAT_0067c280 and set DAT_0067c290=1; then FUN_00576410(msgId, 0), a tail call, so the result
// is FUN_00576410's (HUD_Target's SetProperty returns its low byte).
int HudPostCommand(unsigned MsgId, int bSkipMods);

// FUN_00576410(msgId, mode): walk DAT_0067c3a4 handler tree; call handler vt[0].
int HudDispatchCommand(unsigned MsgId, int Mode);

// FUN_005760d0 — register Handler for MsgId into DAT_0067c3a4 multimap (after existing entries
// with the same id; duplicates allowed).
void HudRegisterCommandHandler(FHudCmdHandler* Handler, unsigned MsgId);

// FUN_005760f0 — erase Handler's entries for one MsgId (the tree is kept even if it empties).
void HudUnregisterCommand(FHudCmdHandler* Handler, unsigned MsgId);

// FUN_00576190 — erase every entry whose handler == Handler; free the tree once it is empty.
void HudUnregisterCommandHandler(FHudCmdHandler* Handler);

// FUN_005762f0 — cancel stack DAT_0067c270: skip the first Skip entries holding Handler and
// unlink every later one.
void HudRemoveCancelHandler(FHudCmdHandler* Handler, int Skip);

// FUN_00576360 — HudRemoveCancelHandler(Handler, Skip), then push Handler on the cancel stack;
// USER_CANCEL (0xf) in mode 1 pops it and calls its slot 0 with 0x10 (FUN_00576410 tail).
void HudPushCancelHandler(FHudCmdHandler* Handler, int Skip);

// FUN_00576010 — Event::Handler base destructor body: HudPopInputFocus, erase every
// registration (freeing an emptied tree), HudRemoveCancelHandler(Handler, 0).
void HudDestroyEventHandler(FHudCmdHandler* Handler);

// FUN_00577b40 — make Handler the Mode-1 filter (DAT_0067c3ac); a filter already in place is
// pushed onto DAT_0067c3a8. Modal NeuroNet dialogs call it when they open.
void HudPushInputFocus(FHudCmdHandler* Handler);

// FUN_00577c30 — drop Handler: if it is the active filter the last pushed one comes back,
// otherwise it is unlinked from the stack. An emptied stack is freed.
void HudPopInputFocus(FHudCmdHandler* Handler);

// FUN_00577d50 — the active Mode-1 filter (DAT_0067c3ac).
FHudCmdHandler* HudGetInputFocus();

// The 0x28-byte command record FUN_0054bcb0 routes. HUD_Button's click (LAB_004c9430) zeroes
// ten dwords, sets +0x26 = 1 and the id at +0; the other posters fill the target fields as well.
struct FSpaceCommand
{
    int Id;                        // +0x00 -- 0 ends routing after the camera's first look
    unsigned char Pad04[0x0c];
    unsigned ObjectId;             // +0x10
    unsigned SubObjectId;          // +0x14
    unsigned char Pad18[0x0e];
    unsigned char Byte26;          // +0x26 -- 1 at every poster; reader not identified
    unsigned char Pad27;
};
static_assert(sizeof(FSpaceCommand) == 0x28, "retail command record is 0x28 bytes");

// FUN_0054bcb0 -- route a command through the in-flight camera
// controller. False when no controller exists (DAT_00678ba0); otherwise the active camera
// (DAT_00679738) sees it through its slot 0x70, a record whose id is then 0 is consumed, slot
// 0x74 may consume it, and ids above 0xe go on to the watched client ship's slot 0x1cc. True
// whenever the controller exists and the id is above 0xe or a camera consumed it.
bool SpaceSendCommand(FSpaceCommand* Command);
