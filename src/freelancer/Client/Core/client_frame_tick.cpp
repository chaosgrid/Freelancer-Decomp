// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Core/client_frame_tick.h"
#include "Core/client_tick_profile.h"

#include "Space/Effects/client_equip_fx.h"
#include "Core/client_state.h"
#include "Interface/menu_ui_widgets.h"
#include "Space/cockpit_load.h"
#include "GameFlow/gameflow.h"
#include "Common/Audio/menu_sound_arch.h"
#include "GameFlow/menu_universe_base.h"
#include "Common/Audio/sound_config.h"
#include "Core/game_options.h"
#include "Common/Fonts/font_atlas.h"
#include "Common/Fonts/font_imp.h"
#include "Common/Fonts/font_string.h"
#include "Core/globals.h"
#include "Space/HUD/hud_element_list.h"
#include "Network/login_mgr.h"             // G_Server (DAT_0067ecd0), DAT_0067ecbe / DAT_0067ecbf
#include "Core/main_command_handler.h"  // DAT_0067ecbd (FUN_005b2c50 / FUN_005b2c60)
#include "Interface/menu_enter_state.h"      // FUN_00564b10 / FUN_00564b20
#include "NeuroNet/nn_error.h"              // FUN_00473cb0
#include "Space/player_behavior.h"
#include "Network/server_connect.h"
#include "Space/Cameras/space_camera.h"
#include "Space/space_scene_load.h"
#include "Space/space_scene_obj_draw.h"
#include "Common/engine_light.h"
#include "Space/star_sys.h"
#include "Space/Effects/tradelane_fx.h"
#include "Interface/win3d.h"
#include "Interface/win3d_immediate.h"
#include "Interface/win3d_render.h"
#include "Interface/win3d_renderables.h"

#include "DALib.h"
#include "alchemy.h"
#include "common_animdb.h"
#include "common_physys.h"
#include "common_misc.h"   // SinglePlayer
#include "common_timing.h" // Timing::UpdateGlobalTime
#include "dacom.h"
#include "render/IRenderPipeline.h"
#include "render/shading.h"

#include <float.h>
#include <stdio.h>
#include <windows.h>

namespace
{
    // DAT_0061085c / DAT_00610860 are RenderMgr.cpp globals: G_bRenderDither (the DitherControl
    // apply LAB_00424630) and G_RenderMipFilter (the MipFilterControlCallback apply
    // LAB_004246d0), defined in engine_init.cpp.

    // DAT_0067ecb0 -- the IServer* the frame saw last. Only FUN_005b2890 touches it.
    IServer* G_FrameLastServer = nullptr;

    IRenderPipeline* FramePipeline()
    {
        // Every pipeline call in FUN_00424f30 / FUN_00425080 / FUN_00425140 re-reads the DALib
        // import -- no null test anywhere.
        return DALib::Pipeline;
    }

    // FUN_00424f30, cdecl, returns DAT_006679f8.
    bool TryBeginFrame()
    {
        G_bRenderFrameBegun = false;
        if (!ClientAppIsActive())
            return false;

        // FUN_005b27c0(0) behind DAT_0067ecbc.
        if (G_bFpExceptionTrap)
            SetFpuExceptionTrap(false);

        // When the device changed the precision (DAT_0061084c differs from DAT_00610848), the
        // frame runs with the one the device left (DAT_0061084c).
        if (G_FpuPrecisionFound != G_FpuPrecisionWanted)
        {
            if (G_FpuPrecisionFound == 24)
                _control87(_PC_24, _MCW_PC);
            else if (G_FpuPrecisionFound == 53)
                _control87(_PC_53, _MCW_PC);
            else
                _control87(0, 0);
        }

        if (G_bRenderPostCreate)
            G_bRenderFrameBegun = FramePipeline()->BeginScene() >= 0;

        if (!G_bRenderFrameBegun)
        {
            // Device recovery: FUN_004249c0 recreates the buffers; only a non-negative swap is
            // followed by BeginScene.
            Sleep(1000);
            if (RenderMgrCreateBuffers(G_RenderMgrWindow, &g_display, &g_display))
            {
                FramePipeline()->ClearBuffers(3, nullptr);
                if (FramePipeline()->SwapBuffers() >= 0)
                    G_bRenderFrameBegun = FramePipeline()->BeginScene() >= 0;
            }
            if (!G_bRenderFrameBegun)
                return false;
        }

        // color_bpp is DAT_00679bd0, G_bRenderDither DAT_0061085c.
        const int DitherEnable = (g_display.color_bpp == 16 && G_bRenderDither != 0) ? 1 : 0;
        FramePipeline()->SetRenderState(D3DRS_DITHERENABLE, DitherEnable);
        FramePipeline()->SetTextureStageState(0, D3DTSS_MIPFILTER, G_RenderMipFilter); // DAT_00610860

        return G_bRenderFrameBegun;
    }

    // FUN_00425080 -- flush the material batcher, EndScene, put the render states the next
    // frame starts from back, re-arm the FP trap, restore the FPU precision. Unconditional: its
    // callers only reach it when FUN_00424f30 returned true.
    void EndSceneHalf()
    {
        DALib::MaterialBatcher->FlushAll(1); // no null test

        // Intentional port-only addition: CLIENT-BETA-LABEL, removal only by Giperion.
        DrawClientTickProfile();

        FramePipeline()->EndScene(); // result not read

        // The state the next frame starts from, through the OpenGL.cpp layer.
        Win3DDisable(kWin3DFog);            // FUN_00421ed0(0xb60)
        Win3DSetDepthWrite(1);              // FUN_00422190(1)
        Win3DDisable(kWin3DBlend);          // FUN_00421ed0(0xbe2)
        Win3DEnable(kWin3DDepthTest);       // FUN_00421dd0(0xb71)
        Win3DSetDepthFunc(4);               // FUN_00422120(4)
        Win3DEnable(kWin3DCullFace);        // FUN_00421dd0(0xb44)

        // FUN_005b27c0(1) behind DAT_0067ecbc.
        if (G_bFpExceptionTrap)
            SetFpuExceptionTrap(true);

        // When the precision FUN_004249c0 found (DAT_0061084c) differs from the one the renderer
        // wants (DAT_00610848), put the wanted one back: 24-bit, 53-bit, or the (0, 0) query for
        // anything else.
        if (G_FpuPrecisionFound != G_FpuPrecisionWanted)
        {
            if (G_FpuPrecisionWanted == 24)
                _control87(_PC_24, _MCW_PC);
            else if (G_FpuPrecisionWanted == 53)
                _control87(_PC_53, _MCW_PC);
            else
                _control87(0, 0);
        }
    }

    // FUN_00425140.
    void PresentHalf()
    {
        FClientTickScope PresentProfile(EClientTickSection::Present);
        Win3DOnPresentFrame(); // FUN_004214a0
        // DAT_00610864 = (result == 0), not (result >= 0).
        G_bRenderPostCreate = FramePipeline()->SwapBuffers() == 0;
    }
}

// FUN_005b2890, cdecl(double dt) -- WinMain passes FUN_0042d680 read before the message pump.
// Every state (space LAB_0054b000, GF FUN_00558c10, IntroFocus FUN_00573eb0, MissionBriefing
// LAB_0043d360) begins, draws and presents its own frame inside its slot 14; the frame function
// only runs the timers, the disconnect check, the Win3D roots, the local server and the state
// machine.
void TickClientFrame(double DeltaSeconds)
{
    // Port-only QPC diagnostics requested by Giperion; CLIENT-BETA-LABEL.
    FClientTickProfileFrame ProfileFrame;
    // FUN_0042d740.
    if (!ShouldSkipLocalServerTick())
        Timing::UpdateGlobalTime(DeltaSeconds);

    // The server pointer changed since the last frame (DAT_0067ecb0 against DAT_0067ecd0).
    bool bServerChanged = false;
    if (G_FrameLastServer != G_Server)
    {
        bServerChanged = true;
        G_FrameLastServer = G_Server;
    }

    // No quit pending (DAT_0067ecbd), multiplayer, and the server dropped us (DAT_0067ecbe): the
    // "connection lost" box FUN_00473cb0(0x737, 0, 0) when the connect response had arrived
    // (DAT_0067ecbf); FUN_005a8260; unless the server select page is up (FUN_00564b20), the
    // intro mode request 3 (FUN_00564b10) and the quit flag.
    if (!ClientQuitRequested() && !SinglePlayer() && G_bServerDisconnected)
    {
        if (G_bServerConnected)
            NnErrorRunModal(0x737, false, nullptr);
        G_bServerDisconnected = false;
        G_bServerConnected = false;
        ShutdownServerConnection();
        if (!MenuNavIsServerSelectActive())
        {
            MenuNavSetIntroModeRequest(3);
            SetClientQuitRequested(true);
        }
    }

    // FUN_0059e080(dt) -- the only per-frame Update of the registered Win3D roots (modal
    // dialogs included); the NN modal loops call it themselves.
    FClientTickScope UiProfile(EClientTickSection::Ui);
    Win3DUpdateAll(DeltaSeconds);
    UiProfile.Stop();

    // IServer slot 11 Update and slot 12 ElapseTime, both inside TickLocalServer.
    FClientTickScope ServerProfile(EClientTickSection::Server);
    TickLocalServer(static_cast<float>(DeltaSeconds));
    ServerProfile.Stop();

    // NextState 0 = stay; CurrentState is DAT_0067ecac as last read.
    int NextState = 0;
    IEnvironment* CurrentState = g_client.current_state;
    bool bForceIntro = false;

    // The server went away this frame while the current state is not the IntroFocus
    // (FUN_00575430): in multiplayer the intro mode request 3, then back to the intro.
    if (bServerChanged && G_Server == nullptr && CurrentState != GetIntroFocusState())
    {
        if (!SinglePlayer())
            MenuNavSetIntroModeRequest(3);
        bForceIntro = true;
    }
    else
    {
        // A live state runs its slot 14 unless a quit is pending; DAT_0067ecac is read again
        // after it.
        if (CurrentState != nullptr)
        {
            if (ClientQuitRequested())
            {
                bForceIntro = true;
            }
            else
            {
                FClientTickScope GameProfile(EClientTickSection::Game);
                NextState = CurrentState->Update(static_cast<float>(DeltaSeconds));
                GameProfile.Stop();
                CurrentState = g_client.current_state;
            }
        }
        // A quit pending now (set before or by the slot 14) forces the intro.
        if (!bForceIntro && ClientQuitRequested())
            bForceIntro = true;
    }

    // State 6 is IntroFocus.
    if (bForceIntro)
    {
        NextState = kClientStateMenu;
        SetClientQuitRequested(false);
    }

    // A state queued by FUN_005b2c70 (DAT_0067ecc4) wins and is cleared.
    NextState = ConsumePendingClientState(NextState);

    // FUN_005b2a30.
    if (NextState != 0)
    {
        CurrentState = EnterClientState(NextState, CurrentState);
        g_client.current_state = CurrentState;
    }

    if (CurrentState == nullptr)
        PostQuitMessage(0);
}

void TickMaterialAnimationPlayback(float DeltaSeconds)
{
    // FUN_0041b2e0. No null tests: both DALib globals are live from engine init on.
    DALib::TextureLib->TickAllPlayback(0, DeltaSeconds);   // slot 0x20 (0, dt)
    DALib::MatAnimLibrary->TickAnimationInstances(-1, DeltaSeconds);         // slot 0x18 (-1, dt)
}

void EndClientRenderScene()
{
    EndSceneHalf();
    EndClientProfileRender();
}

void PresentClientRenderFrame()
{
    // FUN_00425140 -- every retail caller sits inside its FUN_00424f30 success branch.
    PresentHalf();
}

bool BeginClientRenderFrame()
{
    // FUN_00424f30; the result is DAT_006679f8 (G_bRenderFrameBegun).
    BeginClientProfileRender();
    const bool bBegun = TryBeginFrame();
    if (!bBegun)
        EndClientProfileRender();
    return bBegun;
}

void EndClientRenderFrame()
{
    // FUN_00425080 + FUN_00425140 back to back.
    EndSceneHalf();
    EndClientProfileRender();
    PresentHalf();
}
