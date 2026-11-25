// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Network/intro_connect.h"

#include "Interface/Pages/character_select_equip.h" // GetPlayerBaseIdRetail (FUN_004c4810's DAT_00673358)
#include "Network/connect_flow.h"
#include "Core/globals.h"
#include "Core/client_state.h"
#include "Network/login_mgr.h"
#include "Network/menu_mp_session.h"

#include "common_misc.h" // SinglePlayer
#include "dacom.h" // FDUMP

#include <string.h>
#include <windows.h>

int ComputeInitialEnvironment()
{
    // FUN_005b20a0. Default result 6 (menu, IntroFocus).
    int Result = kClientStateMenu;
    // The [Login] auto-login: CLoginMgr::LoadConfig (FUN_005a82d0) fills both names from
    // [Login]; the character name is DAT_0067e9a0.
    bool const bAutoLogin =
        !G_LoginMgr.LoginName.empty() && G_LoginMgr.SelectCharacterName[0] != 0;
    if (bAutoLogin)
    {
        // FUN_005accb0 on DAT_0067e84c.
        if (SinglePlayer() || MenuMpSessionIsReady(&G_LoginMgr.CurrentServer))
        {
            Result = kClientStateQuit;
            if (ConnectAndLogin(g_display.ini_path.Text) &&        // FUN_005a8960
                G_LoginMgr.RequestCharacterInfo() != 0 &&          // FUN_005aa680
                G_LoginMgr.SelectAutoCharacter())                  // FUN_005ab0f0
            {
                // FUN_004c4810: the player's base (DAT_00673358) is set.
                Result = (GetPlayerBaseIdRetail() & 0x3fffffffu) != 0 ? kClientStateBase
                                                                      : kClientStateSpace;
            }
            if (Result == kClientStateQuit)
            {
                FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", "E:\\FL\\Scratch\\Source\\Client\\main.cpp",
                      0x476, "ComputeInitialEnvironment() failed - exiting Freelancer");
            }
        }
    }
    return Result;
}
