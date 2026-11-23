// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail Client\Gf\RoomControlHotspot.cpp -- the room buttons. See room_control_hotspot.h.

#include "GameFlow/room_control_hotspot.h"

#include "Common/Graphics/fade_to_black.h"      // SetFadeTarget / SetFadeImmediate / GetFadeDuration
#include "GameFlow/gf_object_ship.h"     // GfObjectShip (FUN_00446be0), PlayLaunchScript (FUN_00447cd0)
#include "Common/input_manager.h"      // SetClientMouseCursorDrawFlag (FUN_0041dda0)
#include "GameFlow/menu_universe_base.h" // ApplyClientLocationAudio / QueueClientLocationChange
#include "GameFlow/nav_bar.h"            // NavBarCloseService (FUN_004424f0)
#include "NeuroNet/nn_mission_vendor.h"  // NnMissionVendor::CloseWindow (FUN_0048c720)
#include "Interface/win3d.h"              // Win3DFindByName (FUN_0059da10), Win3DDeleteTree (FUN_0043d2a0)
#include "Interface/win_factory.h"        // WinFactoryCreate (FUN_0059db20)

RoomButton::RoomButton()
    : Location(0)
    , Behavior(0)
{
    // FUN_0044d2c0.
}

RoomButton::~RoomButton()
{
    // FUN_0044d300.
}

void RoomButton::Activate()
{
    // 0x00401e90.
}

void RoomButton::Update(float /*Delta*/)
{
    // 0x0053e4a0.
}

void RoomButton::Set(unsigned InLocation, int InBehavior)
{
    // FUN_0044d310.
    Location = InLocation;
    Behavior = InBehavior;
}

ExitDoorButton::ExitDoorButton()
    : State(0)
    , Mode(0)
{
    // Inlined in FUN_0044ced0. +0x14 is left alone (Activate zeroes it before Update reads it).
}

ExitDoorButton::~ExitDoorButton()
{
    // FUN_0044d1a0 -- nothing of its own.
}

void ExitDoorButton::Activate()
{
    // FUN_0044d330.
    if (State != 0)
        return;
    State = 1;
    Elapsed = 0.0f;
    // The fade duration is DAT_0061650c; the 1.0 is the double at 0x005c89b8.
    SetFadeTarget(1.0f, static_cast<float>(1.0 / static_cast<double>(GetFadeDuration())));
    SetClientMouseCursorDrawFlag(0);
    ApplyClientLocationAudio(Location);
}

void ExitDoorButton::Update(float Delta)
{
    // FUN_0044d380.
    if (State == 0)
        return;
    if (State == 1)
    {
        // The compare is on the unrounded sum, not on the float just stored; below the
        // duration or NaN keeps fading.
        double const Sum = static_cast<double>(Delta) + static_cast<double>(Elapsed);
        Elapsed = static_cast<float>(Sum);
        if (!(Sum >= static_cast<double>(GetFadeDuration())))
            return;
        State = 2;
        SetClientMouseCursorDrawFlag(1);
        SetFadeImmediate(1.0f);
        return;
    }
    if (State != 2)
        return;
    if (Mode == 0)
    {
        QueueClientLocationChange(Location, 0.0f, false);
        State = 0;
        return;
    }
    if (Mode == 1)
    {
        // Not null-tested, as retail.
        GfObjectShip()->PlayLaunchScript();
    }
    State = 0;
}

LaunchButton::LaunchButton()
{
    // Inlined in FUN_0044ced0.
    Mode = 1;
}

NewsVendorButton::NewsVendorButton()
{
    // FUN_0044d410.
}

NewsVendorButton::~NewsVendorButton()
{
    // FUN_0044d450.
    Win3D* const Window = Win3DFindByName("NN_News");
    if (Window != nullptr)
    {
        NavBarCloseService(false);
        // FUN_0043d2a0's body inlined.
        Win3DDeleteTree(Window);
    }
}

void NewsVendorButton::Activate()
{
    // LAB_0044d4e0. The destructor's close, inlined.
    Win3D* const Window = Win3DFindByName("NN_News");
    if (Window != nullptr)
    {
        NavBarCloseService(false);
        Win3DDeleteTree(Window);
    }
    // The result is dropped.
    WinFactoryCreate("NN_News", "NN_News", nullptr, nullptr);
}

MissionVendorButton::MissionVendorButton()
{
    // FUN_0044d540.
}

MissionVendorButton::~MissionVendorButton()
{
    // FUN_0044d580.
    Win3D* const Window = Win3DFindByName("NN_MissionVendor");
    if (Window != nullptr)
        static_cast<NnMissionVendor*>(Window)->CloseWindow();
}

void MissionVendorButton::Activate()
{
    // LAB_0044d5e0. The result is dropped.
    WinFactoryCreate("NN_MissionVendor", "NN_MissionVendor", nullptr, nullptr);
    SetClientMouseCursorDrawFlag(1);
}
