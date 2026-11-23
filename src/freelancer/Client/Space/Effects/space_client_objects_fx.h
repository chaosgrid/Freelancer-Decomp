// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Map of the retail Client/Space object files in freelancer.exe .text 0x00520000-0x0053f890
// (boundaries from the embedded "E:\FL\Scratch\Source\Client\..." path strings), with where
// each lives in the port. This header carries no declarations; ship_space_supt_fx.h continues
// the map from 0x00540930.
//
//   CliAsteroid.cpp  0x005200b0-0x0052c7d0  Space/cli_asteroid*.cpp (the static list at
//                    DAT_006753c4 / DAT_006753c8 opens the range).
//   CliLauncher.cpp  0x0052c830-0x0052ec30  Effects/cli_projectile_fx.cpp.
//   CliThruster.cpp  0x0052ec40-0x00530a10  Effects/client_equip_fx.cpp (ctor FUN_0052ec40,
//                    vtable PTR_FUN_005ddbd8).
//   Debris.cpp       0x005300f0-0x00531032  Space/client_debris.h/.cpp.
//   EquippedObj.cpp  0x00533d10-0x00536800  Space/client_gameobj.cpp and
//                    Effects/client_equip_fx.cpp.
//   explosion.cpp    0x00536b50-0x00537ae0  Effects/client_explosion.h/.cpp.
//   gamelit.cpp      0x00537af0-0x00539730  Common/engine_light.cpp.
//   Gameobj.cpp      0x00539740-0x0053d930  Space/client_gameobj.cpp.
//   IClientS.cpp     0x0053d940-0x0053f880  Space/client_iclient_space.cpp and
//                    Effects/tradelane_fx.cpp (FUN_0053d940).
//
// 0x0053f890 onward is Common's CDeadReckonedVector / CRemotePhysicsSimulation cluster.
