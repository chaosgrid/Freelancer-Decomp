// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// ============================================================================
// IVP_U_BigVector growth helpers (retail 0x0635d3e0 / 0x06350590).
//
// The vector is the 32-bit-header growable pointer array
// {capacity@+0, count@+4, elems@+8[, inline buffer @+0xc]} -- in this port
// IVP_SurfaceManager_OutList (ivp_physics/ivp_surface_manager.h). Not
// IVP_U_Vector, whose header fields are 16-bit. The two helpers are its members
// grow / ensure_room (defined in ivp_u_bigvector.cpp).
// ============================================================================

struct IVP_SurfaceManager_OutList;

