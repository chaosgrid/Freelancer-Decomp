// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Network/menu_mp_session.h"

#include "Network/login_mgr.h"   // FServerInfo

bool MenuMpSessionIsReady(FServerInfo const* Server)
{
    // FUN_005accb0. No null test on the argument.
    if (Server->bAddressValid == 0)
        return false;
    if (Server->bSessionValid == 0)
        return false;
    return true;
}
