// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "Common/Graphics/client_mesh_load.h"

#include <cstddef>

// The cockpit half of the in-flight init FUN_0054ab40 and the ship LoadSystem stub of
// FUN_0054b900.
//
// The cockpit MESH is not loaded here: retail loads it inside the first-person camera's ini
// loader FUN_005173c0 (space_camera_first.cpp), on the FUN_00420d40 holder at camera +0x190.

// The fallback uses the same ObjectHandle class as every other cockpit mesh owner.
using FCockpitMeshHolder = FMeshHolder;

// The fallback holder player_spawn.cpp hands out when it has no manager object. Always empty
// (instance -1): nothing in this file binds a mesh any more.
const FCockpitMeshHolder& GetCockpitMeshHolder();

// Part of FUN_0054ab40: resolve the player ship's cockpit ini (or the "cockpits\cockpit.ini"
// fallback with a warning), run the four cockpit camera loaders over it and load the HUD
// elements (FUN_004c73d0). The rest of the init is SpaceApp::Init.
bool InitPlayerCockpitSlice();

// FUN_004c7290 -- .data:00673560. Read by HUD_Button::Update (LAB_004c93d0: the button is
// visible only while it is set). Set around the "IDR_COCKPIT" resource-string-table
// load/unload (space_camera_game.cpp).
void SetCockpitResourceStringLoading(bool bLoading);
bool IsCockpitResourceStringLoading();
