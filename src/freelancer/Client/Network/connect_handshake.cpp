// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Network/connect_handshake.h"

#include "Core/client_frame_tick.h"    // BeginClientRenderFrame / EndClientRenderFrame
#include "common_server.h"        // IServer
#include "common_misc.h"          // SinglePlayer
#include "common_timing.h"        // Timing::read
#include "Core/globals.h"              // PumpMessages, GetClientFrameTimeCapped
#include "Space/HUD/hud_command_dispatch.h" // HudPostCommand (FUN_005763b0)
#include "Network/login_mgr.h"
#include "Interface/menu_enter_state.h"     // FUN_00564b10 / FUN_00564b20
#include "Network/server_connect.h"       // ShutdownServerConnection (FUN_005a8260)
#include "NeuroNet/nn_blocking_dialog.h"   // FUN_00474520 / FUN_00474590
#include "NeuroNet/nn_network_operation.h" // FUN_00473ad0 / FUN_00473b60 / FUN_00473be0
#include "Interface/win3d.h"                // Win3DUpdateAll / Win3DDrawAll

// Retail .data:0067ea50 -- the DirectPlay connect HRESULT HandleConnectError records; zeroed
// and polled by WaitForServerResponse.
long G_ConnectResult = 0;

// Retail .data:0067ea54 -- 1 while WaitForServerResponse runs. Nothing in freelancer.exe
// reads it.
bool G_bWaitingForServer = false;

// Retail .text:005a8760 HandleConnectError.
extern "C" __declspec(dllexport) void __cdecl HandleConnectError(long Result)
{
    G_ConnectResult = Result;
}

// Retail .text:005a89b0 (cdecl).
int WaitForServerResponse(unsigned char* bResponse, float MpTimeout, float SpTimeout)
{
    G_bWaitingForServer = true;
    int Result = 1;
    G_ConnectResult = 0;
    float const StartTime = static_cast<float>(Timing::read()); // FSTP float

    if (!SinglePlayer())
    {
        BlockingDialogOpen(); // FUN_00474520
    }

    // x87: the loop ends only when Timeout < Elapsed; equal or unordered keeps pumping.
    while (*bResponse == 0 && !G_bServerDisconnected && G_ConnectResult == 0 && G_Server != nullptr)
    {
        G_Server->Update(); // +0x2c
        double const Elapsed = Timing::read() - static_cast<double>(StartTime);
        double const Timeout = static_cast<double>(SinglePlayer() ? SpTimeout : MpTimeout);
        if (Timeout < Elapsed)
        {
            break;
        }
    }

    if (!SinglePlayer())
    {
        BlockingDialogClose(); // FUN_00474590
    }

    if (!SinglePlayer() && *bResponse == 0)
    {
        if (G_bServerDisconnected)
        {
            Result = 0;
        }
        else if (G_ConnectResult == 0)
        {
            // The NN_NetworkOperation wait.
            NnNetworkOperationOpen(0x736); // FUN_00473ad0
            int ExitCode = 0;              // FUN_005b0b60's out slot
            while (*bResponse == 0)
            {
                // A drop or a DirectPlay error ends the wait as it is.
                if (G_bServerDisconnected || G_ConnectResult != 0)
                {
                    break;
                }
                // The dialog was dismissed, or WM_QUIT.
                if (!NnNetworkOperationIsOpen() || !PumpMessages(&ExitCode))
                {
                    Result = 2;
                    break;
                }
                if (G_Server == nullptr)
                {
                    Result = 0;
                    break;
                }
                G_Server->Update(); // +0x2c
                // x87, against the float start: equal or unordered goes on.
                double const Elapsed = Timing::read() - static_cast<double>(StartTime);
                if (static_cast<double>(SpTimeout) < Elapsed)
                {
                    Result = 0;
                    break;
                }
                Win3DUpdateAll(GetClientFrameTimeCapped()); // FUN_0059e080(FUN_0042d680())
                if (BeginClientRenderFrame())               // FUN_00424f30
                {
                    Win3DDrawAll();         // FUN_0059e300
                    EndClientRenderFrame(); // FUN_00425080 + FUN_00425140
                }
            }
            NnNetworkOperationClose(); // FUN_00473b60
            if (G_bServerDisconnected)
            {
                Result = 0;
            }
        }
    }
    else if (G_bServerDisconnected)
    {
        Result = 0;
    }

    if (G_ConnectResult != 0)
    {
        // DPNERR HRESULTs.
        if (G_ConnectResult == static_cast<long>(0x80158410))
        {
            Result = 6;
        }
        else
        {
            Result = (G_ConnectResult != static_cast<long>(0x80158610)) ? 5 : 4;
        }
        G_bWaitingForServer = false;
        return Result;
    }

    if (Result == 2 && !SinglePlayer())
    {
        // The player dismissed the NN_NetworkOperation wait. Unless the server-select page is
        // up (DAT_0067c204), drop the connection and send the menu back to mode 3.
        if (!MenuNavIsServerSelectActive())
        {
            ShutdownServerConnection();    // FUN_005a8260
            MenuNavSetIntroModeRequest(3); // FUN_00564b10(3)
            HudPostCommand(0xc5, 0);       // FUN_005763b0(0xc5, 0)
        }
    }

    G_bWaitingForServer = false;
    return Result;
}
