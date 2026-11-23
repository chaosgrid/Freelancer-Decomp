// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstddef>
#include <cstdint>

// ============================================================================
// IVP_Time_Event: the base of everything the environment's time manager
// (IVP_Sim_Unit_Manager, env+0x04) keeps in its event pool (library name).
//   +0x00 vptr, slot 0 = simulate_time_event(IVP_Environment*), dispatched
//         after the event is removed from the pool (063606d0, 063607c0).
//   +0x04 the event's pool slot, 0xffff when not queued (set after removal;
//         FUN_063609c0 stores the Insert result).
// There is no virtual destructor: the PSI event's vtable 063a574c has one slot,
// and the time manager's destructor frees queued events with a bare operator
// delete (FUN_06360960). Derived classes that need one (IVP_Collision) declare
// it themselves.
// ============================================================================

class IVP_Environment;

class IVP_Time_Event
{
public:
    int32_t EventQueueHandle; // +0x04

    virtual void simulate_time_event(IVP_Environment* Environment) = 0;

protected:
    IVP_Time_Event() {}
    ~IVP_Time_Event() {}
};
