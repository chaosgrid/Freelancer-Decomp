// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail LoadingScreen (RTTI .?AVLoadingScreen@@, vftable 0x005e448c) and its
// LoadingProgressBar (.?AVLoadingProgressBar@@, vftable 0x005e44a4). One static instance,
// DAT_0067c3b8 (static init LAB_00579110, atexit LAB_00579150), which InitializeEngine registers
// with Common.dll (RegisterLoadingScreen) so every RefreshLoadingProgress lands in slot 0.

#include "common_ini.h" // ILoadingScreen
#include "Interface/wincontrol_base.h"

struct Image;

// 0x384 bytes: a WinControl with a "LoadingProgressText" TextControl and the
// FRONT_loadingbar.cmp StaticControl whose material animation the phase drives.
class LoadingProgressBar : public WinControl
{
public:
    // Inlined in FUN_00578d50. Also clears DAT_0067c3e0; +0x330 is not written.
    LoadingProgressBar();
    // FUN_00578e70 (deleting FUN_00578e50).
    ~LoadingProgressBar() override;

    // slot 9 FUN_00578b70 (RET 8)
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 11 LAB_00578cd0
    void Update() override;
    // slot 12 LAB_00578d10
    void UpdateInactive() override;
    // slot 13 FUN_00401e90 (empty): the bar never draws through the Win3D tree; the loading
    // screen's Update draws its two children itself.
    void Draw() override;

    unsigned char Field32c;   // +0x32c -- ctor 0; no other reader or writer in the class
    unsigned char Pad32d[3];  // +0x32d
    float Phase;              // +0x330 -- advanced by dt modulo Period
    float Fraction;           // +0x334 -- min(dt, 1.0), the material animation tick
    float Period;             // +0x338 -- ctor 3.0f
    WinControl* Text;         // +0x33c -- "LoadingProgressText"
    wchar_t Caption[0x20];    // +0x340 -- ids 0x7c
    WinControl* Bar;          // +0x380 -- "LoadingProgressBar" StaticControl

private:
    LoadingProgressBar(LoadingProgressBar const&) = delete;
    LoadingProgressBar& operator=(LoadingProgressBar const&) = delete;
};

static_assert(sizeof(void*) != 4 || sizeof(LoadingProgressBar) == 0x384,
              "retail LoadingProgressBar is 0x384 bytes");

// DAT_0067c3b8.
class LoadingScreen : public ILoadingScreen
{
public:
    // LAB_00579110 (static init).
    LoadingScreen();
    // LAB_00579150 (atexit). Not virtual: the vftable 0x005e448c has only the two slots below.
    ~LoadingScreen();

    // slot 0 FUN_00578e90 -- RefreshLoadingProgress's entry.
    void Refresh() override;
    // slot 1 LAB_00578f50 (retail name unknown) -- the body LoadingScreenStart runs
    // non-virtually.
    void Start() override;

    // FUN_00578f70 (thiscall, RET 4).
    void Update(float DeltaSeconds);
    // FUN_00578d50 -- build the progress bar once.
    void CreateProgressBar();

    // The vptr is +0x00.
    Image* Screen;                   // +0x04 DAT_0067c3bc
    LoadingProgressBar* ProgressBar; // +0x08 DAT_0067c3c0
    unsigned char Pad0c[4];          // +0x0c DAT_0067c3c4 -- no reference in the PE
    long long ResetTicks;            // +0x10 DAT_0067c3c8
    long long LastUpdateTicks;       // +0x18 DAT_0067c3d0
    unsigned char bBeginEnd;         // +0x20 DAT_0067c3d8 -- Update brackets its own scene
    unsigned char bClear;            // +0x21 DAT_0067c3d9 -- clear when there is no Screen
    unsigned char bShowBar;          // +0x22 DAT_0067c3da
    unsigned char bDisabled;         // +0x23 DAT_0067c3db -- set at shutdown

private:
    LoadingScreen(LoadingScreen const&) = delete;
    LoadingScreen& operator=(LoadingScreen const&) = delete;
};

// DAT_0067c3e0 -- the bar is running (Start sets it, Reset and the bar's ctor clear it). A
// separate .data byte, not a field of DAT_0067c3b8.
extern unsigned char G_LoadingScreenStarted;
// DAT_0067c3f4 / DAT_0067c3f8 -- Refresh's counts of updates run / skipped as too soon.
extern int G_LoadingScreenUpdateCount;
extern int G_LoadingScreenSkipCount;

LoadingScreen& GetLoadingScreen();

// FUN_00579170. Caller: IntroFocus.
void LoadingScreenCreateProgressBar();
// FUN_00579180. Caller: InitializeEngine.
void LoadingScreenRegister();
// FUN_00579190.
void LoadingScreenReset();
// FUN_005791c0 -- delete the screen and the bar's tree, then disable. Caller: WinMain shutdown.
void LoadingScreenShutdown();
// FUN_00579220 (cdecl) -- replace the screen with a new Image and Load(Path) it; false when the
// Image could not be allocated. Caller: InitializeEngine.
bool LoadingScreenSetImage(char const* Path);
// FUN_00579260. Caller: IntroFocus.
void LoadingScreenReleaseImage();
// FUN_00579280 (cdecl).
void LoadingScreenSetClear(unsigned char bValue);
// FUN_00579290.
void LoadingScreenStart();
