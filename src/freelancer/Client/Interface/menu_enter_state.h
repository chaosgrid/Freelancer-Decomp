// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_server_types.h" // CHARACTER_ID
#include "Common/Graphics/image.h"

#include <windows.h>

// Retail Intro.cpp IntroFocus (RTTI .?AUIntroFocus@@, vftable 0x005e3034) -- the main-menu
// client state. One static instance, DAT_0067a598 (static init LAB_00573790 -> ctor
// FUN_00573840, atexit LAB_005737b0 -> dtor FUN_005737c0). Methods:
//   slot 7  FUN_00517320 -- returns 2 (the environment type FUN_005b0880 tests)
//   slot 12 FUN_00573860 -- Init (RET 4), client_state.cpp
//   slot 13 FUN_00573c00 -- Exit, client_state.cpp
//   slot 14 FUN_00573eb0 -- Update (RET 4): MenuStateDriverUpdate
//   FUN_005743a0 -- EnterState (19-entry jump table over state - 1): EnterMenuState
//   FUN_005739c0 -- start the single-player session (Init and EnterState case 1)
//
// FMenuNavState is the object after its vftable pointer; each comment gives the retail offset
// from the object start (client_state.cpp static_asserts them through struct IntroFocus).

class WinControl;
struct Win3D;

struct FMenuNavState
{
    int CurrentState;           // +0x004
    int PrevState;              // +0x008
    // +0x00c -- the character the next state works on: case 16 copies the new-game record's
    // id here, poll indices 3 / 8 copy the page's pick; case 12 selects it when
    // bHasCharacterPick is set.
    CHARACTER_ID CharacterId;
    unsigned char bHasCharacterPick; // +0x20c -- CharacterId holds a pick (Init clears it)
    unsigned char Pad20d[3];
    // +0x210 -- the menu logo Image (vftable 0x005e2718, 0x18 bytes): Init loads DAT_0067bf4c
    // into it, Exit releases it, the Update frame tail draws it.
    Image LogoImage;
    Win3D* ActivePage;          // +0x228 -- Destroy on it before every page change
    unsigned char bLoadingScreenSettled; // +0x22c -- the first settled frame hid the loading screen
    unsigned char bFromMultiplayer; // +0x22d -- case 4 (multiplayer) set it: case 1 restarts SP
    unsigned char Pad22e[2];
    // +0x230 -- the sub-millisecond rest of the script clock: Init zeroes it, the frame tail
    // hands the whole milliseconds to the script manager and keeps the remainder here.
    float ScriptTimeRemainder;
};

// The Update poll reads the next-state fields of the ported intro pages directly:
// PageServerSelect / PageAccountSelect +0xb4, PageCharacterSelect +0x2b4 (DAT_0067b434),
// PageCharacterSelectSingle +0xb4 (DAT_0067aec4).
int MenuNavGetIntroModeLatch(); // DAT_0067c1fc
void MenuNavSetIntroModeRequest(int Mode); // DAT_0067c200
int MenuNavGetIntroModeRequest();          // DAT_0067c200 (FUN_00564b00 tests it for != 0)
bool MenuNavIsServerSelectActive();        // FUN_00564b20 -- DAT_0067c204
void MenuNavConsumeIntroModeRequest(); // latch = request; request = 0

// FUN_005743a0 -- EnterState(State) on the IntroFocus.
void EnterMenuState(FMenuNavState* Nav, int State);

// FUN_00573eb0 -- IntroFocus::Update(DeltaSeconds). Returns the client-state code (0 = stay).
int MenuStateDriverUpdate(FMenuNavState* Nav, float DeltaSeconds);

// FUN_005739c0 -- the single-player session start: SetMultiPlayer(0), +0x22d = 0, the
// "SinglePlayer" login, then ConnectToServer && LoginToServer && Reset / list copy /
// RequestCharacterInfo. False when any step fails.
bool MenuNavStartSinglePlayer(FMenuNavState* Nav);

// The TypeSelect column (EnterState case 1) gets its input from the Win3D manager as in retail.

void MenuEnterStateReleaseActivePage(FMenuNavState* Nav);
