// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// FUN_0635a9e0: a multiplicative-congruential PRNG. The 32-bit global seed
// (DAT_063ee4dc) is multiplied by 75 each call (natural 32-bit wraparound) and
// its low 16 bits scaled by 2^-16 (DAT_063a567c) to a float in [0, 1).
//
// Callers (06349855 / 063498bf): IVP_Simulation_Unit_Process_One_Psi_Step's
// reseed of the environment's next_movement_check countdown
// (15 - ftol(rand * -5.0f)); not gameplay-visible randomness.
float IVP_FastRandomFloat();
