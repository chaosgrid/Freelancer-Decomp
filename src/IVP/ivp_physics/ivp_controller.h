// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The IVP_Controller family is public Common.dll ABI (Common exports classes built on it and
// Server.dll dynamic_casts IVP_Controller*): its single declaration is
// src/include/common_ivp_controller.h. This forwarding header keeps the src/IVP and
// src/Common includers resolving.
#include "../../include/common_ivp_controller.h"
