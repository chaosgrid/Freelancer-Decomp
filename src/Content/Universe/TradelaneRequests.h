// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Content.dll TU55: the trade lane request handlers of the space support controller (TU53).
// Retail emits them as __thiscall methods on the support object (SpaceSupport messages 0x14 /
// 0x15), but neither body reads this, so the port keeps them as free functions taking only the
// message parameters.

// The 0x10-byte parameter block of SpaceSupport messages 0x14 / 0x15.
struct FTradelaneRequest
{
    unsigned int ShipId;          // +0x00 the ship asking for the lane
    unsigned int RingId;          // +0x04 the ring it enters (GetType / Activate target)
    unsigned int EndRingId;       // +0x08 the ring it travels to (TU46 FUN_06f47ee0 argument 2)
    unsigned int SystemId;        // +0x0c FindSystemInfo key of the GCS event
};

// FUN_06f6fcb0: SpaceSupport message 0x14.
void HandleTradelaneRequest(FTradelaneRequest const* Request);

// FUN_06f6fc90: SpaceSupport message 0x15.
void HandleTradelaneRelease(FTradelaneRequest const* Request);
