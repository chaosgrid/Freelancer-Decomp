// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The camera block of a ship archetype's client companion (Archetype::FCliShip +0x68, in
// client_cliobj.h) is written by the ship ini value handler at 0x0040763f, and read back by the
// third-person camera's attach (FUN_00518af0 at 0x00518b75).

// The three turn angles are clamped to [0, 90] DEGREES and then converted to radians
// (DAT_005c7474 = 0.0, DAT_005c77f0 = 90.0, DAT_005c77f4 = 0.01745329238474369f).
float ShipCameraClampTurnAngle(float Degrees);

