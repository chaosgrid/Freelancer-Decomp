// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Network/connect_flow.h"

#include "NeuroNet/Chat/chat_init.h"      // ChatResetForConnect (FUN_0046b2b0)
#include "common_ini.h"     // RefreshLoadingProgress
#include "common_server.h"  // IServer
#include "common_misc.h"    // SinglePlayer
#include "Network/connect_lists.h"  // PreConnectReset (FUN_004c4a80)
#include "Network/login_mgr.h"
#include "NeuroNet/nn_error.h"       // NnErrorRunModal (FUN_00473cb0)
#include "Space/player_spawn.h"   // GetPlayerConnectionId (DAT_00673344)
#include "Network/server_connect.h"

// FUN_005a8770 (cdecl).
bool ConnectToServer(char const* IniPath)
{
    RefreshLoadingProgress();
    PreConnectReset();                      // FUN_004c4a80

    if (StartServer(IniPath))
    {
        int const Result = G_LoginMgr.Connect(true);
        if (Result == 1)
        {
            return true;
        }
        // A generic failure is not reported when the server already dropped us (DAT_0067ecbe).
        if (Result == 4)
        {
            NnErrorRunModal(0x73e, true, nullptr);
        }
        else if (Result == 6)
        {
            NnErrorRunModal(0x73f, true, nullptr);
        }
        else if (!G_bServerDisconnected)
        {
            NnErrorRunModal(0x73d, true, nullptr);
        }
    }

    ShutdownServerConnection(); // inlined in retail
    return false;
}

// FUN_005a8840 (cdecl).
bool LoginToServer()
{
    if (G_Server == nullptr)
    {
        return false;
    }

    int Result;
    if (SinglePlayer())
    {
        G_LoginMgr.bCreateAccount = 0;      // DAT_0067e960
        Result = G_LoginMgr.Login();
        if (Result != 1 && G_LoginMgr.LoginStatus == 2)
        {
            G_LoginMgr.bCreateAccount = 1;
            Result = G_LoginMgr.Login();
        }
    }
    else
    {
        Result = G_LoginMgr.Login();
    }

    if (Result == 1)
    {
        ChatResetForConnect(); // FUN_0046b2b0 (returns at once in single player)
        return true;
    }

    if (Result != 2)
    {
        // Keyed on the login status byte (DAT_0067e969); a generic failure is not reported when
        // the server already dropped us (DAT_0067ecbe).
        if (G_LoginMgr.LoginStatus == kLoginBanned)
        {
            NnErrorRunModal(0x742, true, nullptr);
        }
        else if (G_LoginMgr.LoginStatus == kLoginAlreadyLoggedIn)
        {
            NnErrorRunModal(0x753, true, nullptr);
        }
        else if (G_LoginMgr.LoginStatus == kLoginAccountLocked)
        {
            NnErrorRunModal(0x743, true, nullptr);
        }
        else if (!G_bServerDisconnected)
        {
            NnErrorRunModal(0x739, true, nullptr);
        }
    }

    ShutdownServerConnection(); // inlined in retail
    return false;
}

// FUN_005a8960 (cdecl).
bool ConnectAndLogin(char const* IniPath)
{
    if (!ConnectToServer(IniPath))
    {
        return false;
    }
    return LoginToServer();
}
