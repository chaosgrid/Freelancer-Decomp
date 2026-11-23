// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Map of the retail client files in freelancer.exe .text 0x00540930-0x0055fea0 (boundaries
// from the embedded "E:\FL\Scratch\Source\Client\..." path strings), with where each lives in
// the port. This header carries no declarations.
//
//   ship.cpp                   ~0x00540930-0x0054a97f  the CLIENT Ship gameobj
//                              (FClientShipGameobj, client_gameobj.h); FUN_00543b40 is
//                              RequestLand, its IAction Action_Land (client_land_action.h).
//                              Common's CShip is only reached through the IAT.
//   space.cpp                  ~0x0054ab20-0x0054be1f  space connect (FUN_0054ab40,
//                              player_spawn.cpp) and teardown (FUN_0054aec0, space_state.cpp).
//   SpaceDust.cpp              0x0054bda0-0x0054c361   space_dust.h/.cpp. 0x0054c370 onward is
//                              the "space_dust_detail" perf-option registration.
//   SpaceTerrainRenderComp.cpp ~0x0054de10-0x00552ecf  asteroid billboards: cli_asteroid.cpp
//                              (FUN_0054de10, FUN_00550000) and asteroid_billboard.h/.cpp.
//   StarSphere.h / StarSys.cpp ~0x00552fd0-0x005561ff  star_sphere.h/.cpp, star_sys.h/.cpp.
//   supt\Hotspot.cpp           ~0x00556200-0x005572ff  GameFlow/gf_hotspot.h/.cpp.
//   supt\LightsCamera.cpp      ~0x00557360-0x00558bff  Common/Graphics/lights_camera.h/.cpp.
//   supt\Support.cpp           ~0x00558c10-0x0055fea0+ shared client-object support code;
//                              parts in GameFlow/gameflow.cpp and elsewhere.
