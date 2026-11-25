// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/loading_screen.h"

#include "Core/client_frame_tick.h"   // BeginClientRenderFrame / EndClientRenderScene / Present...
#include "Core/globals.h"             // RenderMgrFrameReady
#include "Common/Fonts/ids_strings.h"         // GetIdsStringWWithRawFallback
#include "Common/Graphics/image.h"
#include "NeuroNet/nn_ui_pulse.h"         // NnUiPulseIdleArgb
#include "Interface/win3d.h"
#include "Interface/win3d_immediate.h"     // Win3DSetPipelineClearColor
#include "Interface/win3d_render.h"        // ClearWin3DBuffers
#include "Interface/win3d_renderables.h"   // UiOverlayPrepPipeline

#include "common_ini.h"
#include "common_timing.h"       // Timing

#include <math.h>
#include <stddef.h>
#include <stdint.h>

static_assert(sizeof(void*) != 4 || offsetof(LoadingProgressBar, Field32c) == 0x32c,
              "LoadingProgressBar +0x32c");
static_assert(sizeof(void*) != 4 || offsetof(LoadingProgressBar, Phase) == 0x330,
              "LoadingProgressBar +0x330");
static_assert(sizeof(void*) != 4 || offsetof(LoadingProgressBar, Text) == 0x33c,
              "LoadingProgressBar +0x33c");
static_assert(sizeof(void*) != 4 || offsetof(LoadingProgressBar, Caption) == 0x340,
              "LoadingProgressBar +0x340");
static_assert(sizeof(void*) != 4 || offsetof(LoadingProgressBar, Bar) == 0x380,
              "LoadingProgressBar +0x380");
static_assert(sizeof(void*) != 4 || offsetof(LoadingScreen, Screen) == 0x04, "LoadingScreen +0x04");
static_assert(sizeof(void*) != 4 || offsetof(LoadingScreen, ResetTicks) == 0x10,
              "LoadingScreen +0x10");
static_assert(sizeof(void*) != 4 || offsetof(LoadingScreen, LastUpdateTicks) == 0x18,
              "LoadingScreen +0x18");
static_assert(sizeof(void*) != 4 || offsetof(LoadingScreen, bBeginEnd) == 0x20,
              "LoadingScreen +0x20");
static_assert(sizeof(void*) != 4 || offsetof(LoadingScreen, bDisabled) == 0x23,
              "LoadingScreen +0x23");

unsigned char G_LoadingScreenStarted = 0; // DAT_0067c3e0
int G_LoadingScreenUpdateCount = 0;       // DAT_0067c3f4
int G_LoadingScreenSkipCount = 0;         // DAT_0067c3f8

namespace
{
    // DAT_0067c3b8.
    LoadingScreen G_LoadingScreen;
}

// ---------------------------------------------------------------------------------------
// LoadingProgressBar
// ---------------------------------------------------------------------------------------

LoadingProgressBar::LoadingProgressBar()
{
    Period = 3.0f;
    Bar = nullptr;
    Text = nullptr;
    G_LoadingScreenStarted = 0;
    Flags = static_cast<unsigned char>(Flags & 0xfe);
    Field32c = 0;
    Fraction = 0.0f;
}

LoadingProgressBar::~LoadingProgressBar()
{
    // FUN_00578e70.
}

bool LoadingProgressBar::Create(Win3D* NewParent, void* Data)
{
    // FUN_00578b70 (RET 8).
    float TextPosition[3] = {-0.095f, 0.0192f, 0.0f};
    Text = CreateTextControl("LoadingProgressText", "PageHeader", TextPosition, nullptr, 0.2f,
                             0.04f, 2.0f, 0, 1, 0, 1);

    float BarPosition[3] = {0.0f, 0.0f, -1.0f};
    float BarHalfExtents[3] = {0.0f, 0.0f, 0.0f};
    float BarVector338[3] = {0.0f, 0.0f, 0.0f};
    Bar = CreateStaticControl("LoadingProgressBar", "Interface\\Intro\\objects\\FRONT_loadingbar.cmp",
                              BarPosition, nullptr, BarHalfExtents, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
                              BarVector338);

    SetOffset(0.0f, 0.0f, -1.0f);

    GetIdsStringWWithRawFallback(0x7c, Caption, 0x20);
    if (Text != nullptr)
        Text->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(Caption), 0);

    return Win3D::Create(NewParent, Data);
}

void LoadingProgressBar::Update()
{
    // LAB_00578cd0: shown (and active) only while the bar runs.
    if (G_LoadingScreenStarted != 0)
    {
        SetHiddenWithChildren(false);
        Flags = static_cast<unsigned char>(Flags | 1);
    }
    else
    {
        SetHiddenWithChildren(true);
        Flags = static_cast<unsigned char>(Flags & 0xfe);
    }
    Win3D::Update();
}

void LoadingProgressBar::UpdateInactive()
{
    // LAB_00578d10: re-activate once the bar runs, then Win3D's (inlined in retail).
    if (G_LoadingScreenStarted != 0)
    {
        SetHiddenWithChildren(false);
        Flags = static_cast<unsigned char>(Flags | 1);
    }
    Win3D::UpdateInactive();
}

void LoadingProgressBar::Draw()
{
    // FUN_00401e90.
}

// ---------------------------------------------------------------------------------------
// LoadingScreen
// ---------------------------------------------------------------------------------------

LoadingScreen::LoadingScreen()
{
    // LAB_00579110.
    Screen = nullptr;
    bBeginEnd = 1;
    G_LoadingScreenStarted = 0;
    bClear = 1;
    bShowBar = 1;
    bDisabled = 0;
    ProgressBar = nullptr;
}

LoadingScreen::~LoadingScreen()
{
    // LAB_00579150: the pointer is not cleared.
    if (Screen != nullptr)
        delete Screen;
}

void LoadingScreen::Refresh()
{
    // FUN_00578e90.
    if (bDisabled != 0)
        return;

    long long const Now = Timing::read_ticks();
    if (G_LoadingScreenStarted == 0)
    {
        // Only more than two seconds since the reset starts the bar (virtually, through Start).
        long long const SinceReset = Now - ResetTicks;
        if (Timing::seconds(SinceReset) > 2.0)
            Start();
        return;
    }

    // The time since the last update is rounded to float for Update but compared unrounded
    // against 0.05f (DAT_00615108); an unordered dt counts as skipped.
    long long const SinceUpdate = Now - LastUpdateTicks;
    double const Seconds = Timing::seconds(SinceUpdate);
    float const DeltaSeconds = static_cast<float>(Seconds);
    if (static_cast<double>(0.05f) < fabs(Seconds))
    {
        Update(DeltaSeconds);
        ++G_LoadingScreenUpdateCount;
    }
    else
    {
        ++G_LoadingScreenSkipCount;
    }
}

void LoadingScreen::Start()
{
    // LAB_00578f50.
    G_LoadingScreenStarted = 1;
    Update(-1.0f);
}

void LoadingScreen::Update(float DeltaSeconds)
{
    // FUN_00578f70 (RET 4).
    if (bDisabled != 0)
        return;
    LastUpdateTicks = Timing::read_ticks();
    if (!RenderMgrFrameReady())
        return;
    if (bBeginEnd != 0 && !BeginClientRenderFrame())
        return;

    if (Screen != nullptr)
    {
        Screen->Draw();
    }
    else if (bClear != 0)
    {
        Win3DSetPipelineClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        ClearWin3DBuffers(0x4100);
    }

    if (ProgressBar != nullptr && bShowBar != 0 && G_LoadingScreenStarted != 0)
    {
        Win3DBeginDraw();
        LoadingProgressBar* const Bar = ProgressBar;
        // A negative dt resets; zero, positive and unordered advance.
        if (DeltaSeconds < 0.0f)
        {
            Bar->Phase = 0.0f;
            Bar->Fraction = 0.0f;
        }
        else
        {
            // The sum stays double; it is stored once, and again after Period is subtracted
            // while it is still above Period.
            double const Period = static_cast<double>(Bar->Period);
            double Phase = static_cast<double>(DeltaSeconds) + static_cast<double>(Bar->Phase);
            Bar->Phase = static_cast<float>(Phase);
            if (Phase > Period)
            {
                do
                {
                    Phase -= Period;
                } while (Phase > Period);
                Bar->Phase = static_cast<float>(Phase);
            }
            // Unordered gives 1.0.
            Bar->Fraction = (DeltaSeconds < 1.0f) ? DeltaSeconds : 1.0f;
        }

        if (Bar->Bar != nullptr)
        {
            TickMaterialAnimationPlayback(Bar->Fraction);
            Bar->Bar->Draw();
        }
        if (Bar->Text != nullptr)
        {
            // Property 0x33 takes a copy of the idle colour DAT_00679bb0.
            unsigned long Color = NnUiPulseIdleArgb();
            Bar->Text->SetProperty(kWinPropColour, reinterpret_cast<uintptr_t>(&Color), 1);
            Bar->Text->Draw();
        }
        Win3DEndDraw();
    }

    if (bBeginEnd != 0)
    {
        UiOverlayPrepPipeline();
        EndClientRenderScene();
        PresentClientRenderFrame();
    }
}

void LoadingScreen::CreateProgressBar()
{
    // FUN_00578d50: only once. Retail has no failure path for the allocation.
    if (ProgressBar != nullptr)
        return;
    LoadingProgressBar* const Bar = new LoadingProgressBar();
    ProgressBar = Bar;
    Bar->Create(nullptr, nullptr);
    Bar->SetName("LoadingProgressBar");
    Bar->ClassName = "LoadingProgressBar";
}

LoadingScreen& GetLoadingScreen()
{
    return G_LoadingScreen;
}

void LoadingScreenCreateProgressBar()
{
    // FUN_00579170.
    G_LoadingScreen.CreateProgressBar();
}

void LoadingScreenRegister()
{
    // FUN_00579180.
    RegisterLoadingScreen(&G_LoadingScreen);
}

void LoadingScreenReset()
{
    // FUN_00579190.
    G_LoadingScreen.ResetTicks = Timing::read_ticks();
    G_LoadingScreenStarted = 0;
    G_LoadingScreen.bClear = 0;
    G_LoadingScreen.bShowBar = 1;
}

void LoadingScreenShutdown()
{
    // FUN_005791c0.
    if (G_LoadingScreen.Screen != nullptr)
        delete G_LoadingScreen.Screen;
    G_LoadingScreen.Screen = nullptr;

    LoadingProgressBar* const Bar = G_LoadingScreen.ProgressBar;
    if (Bar != nullptr)
    {
        // ProgressBar keeps the dead pointer (not cleared), as retail.
        Win3D* Child = Bar->GetChildHead();
        while (Child != nullptr)
        {
            Win3D* const Next = Child->GetNextSibling();
            Win3DDeleteTree(Child);
            Child = Next;
        }
        Bar->Destroy();
        delete Bar;
    }
    G_LoadingScreen.bDisabled = 1;
}

bool LoadingScreenSetImage(char const* Path)
{
    // FUN_00579220 (cdecl).
    if (G_LoadingScreen.Screen != nullptr)
    {
        delete G_LoadingScreen.Screen;
        G_LoadingScreen.Screen = nullptr;
    }
    G_LoadingScreen.Screen = CreateImage();
    if (G_LoadingScreen.Screen != nullptr)
        return G_LoadingScreen.Screen->Load(Path);
    return false;
}

void LoadingScreenReleaseImage()
{
    // FUN_00579260.
    if (G_LoadingScreen.Screen != nullptr)
    {
        delete G_LoadingScreen.Screen;
        G_LoadingScreen.Screen = nullptr;
    }
}

void LoadingScreenSetClear(unsigned char bValue)
{
    // FUN_00579280.
    G_LoadingScreen.bClear = bValue;
}

void LoadingScreenStart()
{
    // FUN_00579290: Update called directly, not through the virtual Start.
    G_LoadingScreenStarted = 1;
    G_LoadingScreen.Update(-1.0f);
}
