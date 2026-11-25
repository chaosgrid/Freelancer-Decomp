// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Core/main_command_handler.h"
#include "Core/event_handler.h"

#include "DALib.h"
#include "common_misc.h" // GetScreenShotPath
#include "dacom.h"
#include "Space/HUD/hud_command_dispatch.h"
#include "NeuroNet/nn_pause.h"              // NnPauseOpen (FUN_004a75c0)
#include "render/IRenderPipeline.h"

#include <io.h>
#include <stdio.h>
#include <stdlib.h>

namespace
{
    bool G_bClientQuitRequested = false; // DAT_0067ecbd (.bss)
    int G_ScreenShotCounter = 0;         // DAT_006679fc (.bss)

    // DAT_0067ea60: one vptr (005e6d20, single slot LAB_005b2440) -- the FHudCmdHandler shape
    // FUN_00576410 calls through.
    class FMainCommandHandler : public EventHandler
    {
    public:
        FMainCommandHandler();
        ~FMainCommandHandler();

        // LAB_005b2440 (thiscall, RET 4).
        bool OnEvent(unsigned MsgId) override;

    };

    FMainCommandHandler::FMainCommandHandler()
    {
        // FUN_005b23c0: the ids are registered in this order through FUN_005760d0.
        static unsigned const kIds[5] = {USER_EXIT_GAME, USER_PAUSE, USER_RESET, USER_FULLSCREEN,
                                         USER_SCREEN_SHOT};
        for (unsigned Index = 0; Index < 5; ++Index)
            RegisterEvent(kIds[Index]);
    }

    FMainCommandHandler::~FMainCommandHandler()
    {
        // LAB_005b2550 -> FUN_00576010 runs through the EventHandler base destructor.
    }

    bool FMainCommandHandler::OnEvent(unsigned MsgId)
    {
        switch (MsgId)
        {
        case USER_PAUSE:
            NnPauseOpen(); // FUN_004a75c0
            return true;
        case USER_RESET:
            // Consumed, nothing else.
            return true;
        case USER_EXIT_GAME:
            G_bClientQuitRequested = true;
            return true;
        case USER_SCREEN_SHOT:
            TakeClientScreenShot(); // FUN_00425170
            return true;
        default:
            // USER_FULLSCREEN (0x17) is registered but retail's switch sends it to the false arm,
            // like every unlisted id.
            return false;
        }
    }

    // LAB_005b23a0 (XC 0061013c) constructs it; the atexit LAB_005b2550 destroys it.
    FMainCommandHandler G_MainCommandHandler;
}

bool ClientQuitRequested()
{
    return G_bClientQuitRequested;
}

void SetClientQuitRequested(bool bQuit)
{
    G_bClientQuitRequested = bQuit;
}

int TakeClientScreenShot()
{
    // FUN_00425170.
    char ShotDir[0x104];
    if (!GetScreenShotPath(ShotDir))
        return -1;

    char FileName[0x80];
    char FullPath[0x104];
    do
    {
        G_ScreenShotCounter = G_ScreenShotCounter + 1;
        sprintf(FileName, "screen%d", G_ScreenShotCounter);
        _makepath(FullPath, nullptr, ShotDir, FileName, "BMP");
    } while (_access(FullPath, 0) != -1);

    // Released whenever the out pointer is non-null, whatever QueryInterface answered.
    IRPTexture* Texture = nullptr;
    IRenderPipeline* Pipe = DALib::Pipeline;
    if (Pipe->QueryInterface(Iids::RPTexture, reinterpret_cast<void**>(&Texture)) == GR_OK)
        Texture->PrintScreen(nullptr, FullPath);
    int const Counter = G_ScreenShotCounter;
    if (Texture != nullptr)
        Texture->Release();
    return Counter;
}
