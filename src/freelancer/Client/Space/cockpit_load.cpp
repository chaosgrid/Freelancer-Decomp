// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Space/cockpit_load.h"

#include "Space/player_spawn.h"
#include "common_archetype.h"
#include "common_cobject.h"
#include "common_misc.h" // GetFileSysPath
#include "dacom.h"
#include "Space/HUD/hud_element_list.h"
#include "Space/Cameras/space_camera.h"

#include <cstddef>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

namespace
{
    static const unsigned kShipObjectClassFlags = CObject::ClassShip;

    // .rdata 0x005e07bc -- the fallback when the ship archetype names no cockpit.
    static const char kDefaultCockpitIniRel[] = "cockpits\\cockpit.ini";
    // .rdata 0x005e0704 and the line FUN_0054ab40 reports the fallback from.
    static const char kSpaceCpp[] = "E:\\FL\\Scratch\\Source\\Client\\space\\space.cpp";
    static const int kNoCockpitWarningLine = 0x5d5;

    // Same `- 8` as everywhere else: the watcher holds the Watchable, eight bytes into the
    // IObjRW.
    const int kWatchableOffset = 8;

    // Retail CEqObj offsets used by FUN_00543350.
    struct FPlayerShipView
    {
        void* Vtable;
        unsigned char PadTo4c[0x48];
        unsigned FlagsAt4c;
        unsigned char PadTo88[0x38];
        Archetype::Root* Archetype;
    };

    static_assert(offsetof(FPlayerShipView, FlagsAt4c) == 0x4c, "CEqObj+0x4c class flags");
    static_assert(offsetof(FPlayerShipView, Archetype) == 0x88, "CObject+0x88 archetype");

    // Archetype::CliShip companion — cockpit ini path at retail +0x60.
    struct FCliShipCompanionView
    {
        void* Vtable;
        unsigned char PadTo60[0x5C];
        char* CockpitIniPath;
    };

    static_assert(offsetof(FCliShipCompanionView, CockpitIniPath) == 0x60,
                  "CliShip+0x60 cockpit ini path");

    // player_spawn.cpp's fallback when it has no manager object; never bound.
    static FCockpitMeshHolder GCockpitMeshHolder;

    // FUN_004c7290 — .data:00673560.
    bool GCockpitResourceStringLoading = false;


    // FUN_00543350 (thiscall on the IObjRW). The CEqObj is the IObjRW's +0x10, and it counts
    // only when its class word at +0x4c carries all of 0x503. A failed test leaves null and
    // retail dereferences it anyway, so no guard is added here either.
    Archetype::Root* ShipArchetypeOf(void* ObjRW)
    {
        FPlayerShipView* EqObj = nullptr;
        if (ObjRW != nullptr)
        {
            unsigned char* const Base = static_cast<unsigned char*>(ObjRW) + 0x0c;
            if (Base != nullptr)
            {
                FPlayerShipView* const Candidate = *reinterpret_cast<FPlayerShipView**>(Base + 4);
                if (Candidate != nullptr &&
                    (Candidate->FlagsAt4c & kShipObjectClassFlags) == kShipObjectClassFlags)
                    EqObj = Candidate;
            }
        }
        return EqObj->Archetype;
    }
}

const FCockpitMeshHolder& GetCockpitMeshHolder()
{
    return GCockpitMeshHolder;
}

bool InitPlayerCockpitSlice()
{
    // The player's IObjRW -- the watcher at DAT_00679744 minus eight, or null.
    void* const Watched = GetPlayerShipWatcher();
    void* const ObjRW =
        (Watched != nullptr) ? static_cast<unsigned char*>(Watched) - kWatchableOffset : nullptr;

    // Retail tests only the first character -- the path pointer itself is never null-tested.
    Archetype::Root* const Arch = ShipArchetypeOf(ObjRW);
    const char* const CockpitPath =
        reinterpret_cast<FCliShipCompanionView*>(Arch->Companion)->CockpitIniPath;

    char FullPath[MAX_PATH];
    if (CockpitPath[0] == '\0')
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kSpaceCpp, kNoCockpitWarningLine,
              "ship has no cockpit definition");
        GetFileSysPath(FullPath, kDefaultCockpitIniRel, nullptr);
    }
    else
    {
        GetFileSysPath(FullPath, CockpitPath, nullptr);
    }

    // The first-person loader (FUN_005173c0) is what loads the cockpit mesh.
    SpaceCameraLoadCockpit(FullPath);

    LoadHudElementsFromIni(); // FUN_004c73d0

    // The rest of FUN_0054ab40 continues in space_state.cpp SpaceApp::Init.
    return true;
}

void SetCockpitResourceStringLoading(bool bLoading)
{
    GCockpitResourceStringLoading = bLoading;
}

bool IsCockpitResourceStringLoading()
{
    return GCockpitResourceStringLoading;
}
