// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// IVP per-PSI friction-impulse pass: Coulomb friction-cone clamp,
// redundant-dist merging and the two tangential reaction kernels. Retail
// Common.dll 06353b50-063555a8 plus LAB_06356660 (the friction system's
// do_simulation_controller, which passes its IVP_Event_Sim straight down).
// The classes are declared in ivp_mindist.h: IVP_ListManager is Ipion's
// IVP_Friction_System and IVP_ListManager_CorePairEntry its contact set
// (pair list); IVP_Impact_Solver_Long_Term is the contact frame.

#include "ivp_mindist.h"

// ---- impact-system helpers of the contact point ----

// FUN_06359d10 (thiscall on the contact point, RET 0x18): the edge / edge
// contact frame -- case 1 of FUN_0635a1e0's first switch. K and L are the
// two sides' edges (raw edge pointers, the low nibble is the edge's slot in
// its triangle), CacheK / CacheL the two sides' IVP_Cache_Ledge_Point.
// Writes the long-term normal (+0x00), contact point (+0x20) and first
// tangent axis (+0x78), the contact's gap (+0x58); the parallel-edge branch
// also zeroes *OutScratch.
class IVP_Compact_Edge;
struct IVP_Cache_Ledge_Point;
