// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Several includers reach common_goods.h / <windows.h> through this header.
#include "common_goods.h"

#include <windows.h>

// A few unrelated retail functions.

// The ToolTip and Rollover objects the client pre-init creates.
void MenuUiInitToolTipAndRollover();

// The Rollover (DAT_00672288) and its global entry points live in nn_rollover.h.

// FUN_004c55f0 -- the rank level (DAT_0067338c) = -1, then IServer::RequestRankLevel. Called by
// the NN_Dealer ctor and NN_ShipTrader::Create.
void HudMessageQueueRequestRankRefresh();

// FUN_004c5680 / FUN_004c56b0 -- clear and push_back on the retail dword vector at
// DAT_006728cc..d4. Only caller: IClient slot 120 (LAB_0053e270).
void RetailU32QueueClear();
void RetailU32QueuePushBack(unsigned Value);
// FUN_004c5890 / FUN_004c58b0 -- size() and bounds-checked at() (0 out of range). HUD_Target's
// formation test reads the entries as object ids of the player's group.
unsigned RetailU32QueueSize();
unsigned RetailU32QueueAt(unsigned Index);
