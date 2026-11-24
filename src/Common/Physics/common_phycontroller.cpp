// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Physics/common_physys_internal.h"

using namespace CmnPhySys;

// ============================================================================
// PhySys::Controller
// ============================================================================

// FUN_06285b50. Deviation: retail leaves +0x0c unwritten; the port zeroes it.
PhySys::Controller::Controller()
    : PadC(0)
{
}

// FUN_0627ccf0: a shallow copy; the core array is shared, not duplicated (retail).
PhySys::Controller::Controller(Controller const& Other)
    : PadC(Other.PadC)
{
    Cores.memsize = Other.Cores.memsize;
    Cores.n_elems = Other.Cores.n_elems;
    Cores.elems = Other.Cores.elems;
}

// FUN_06285b90: with cores registered, the controller comes off every core and the vector is
// emptied; the vector member's own destructor then clears again.
PhySys::Controller::~Controller()
{
    if (Cores.n_elems != 0)
    {
        IVP_Controller_Detach_From_All_Cores(this, 1);
        Cores.Clear();
    }
}

// FUN_0627cd30: the same shallow copy, no self test.
PhySys::Controller& PhySys::Controller::operator=(Controller const& Other)
{
    Cores.memsize = Other.Cores.memsize;
    Cores.n_elems = Other.Cores.n_elems;
    Cores.elems = Other.Cores.elems;
    PadC = Other.PadC;
    return *this;
}

// FUN_06285c30: the destructor's first block.
void PhySys::Controller::core_is_going_to_be_deleted_event(IVP_Core* Core)
{
    (void)Core;
    if (Cores.n_elems != 0)
    {
        IVP_Controller_Detach_From_All_Cores(this, 1);   // FUN_06348f60(this, 1)
        Cores.Clear();
    }
}

IVP_CONTROLLER_PRIORITY PhySys::Controller::get_controller_priority()
{
    // FUN_06285c80: 1500.
    return IVP_CP_MOTION;
}

IVP_U_Vector<IVP_Core>* PhySys::Controller::get_associated_controlled_cores()
{
    // FUN_0632e7a0 (ICF)
    return &Cores;
}

// FUN_06285c90
void PhySys::Controller::wakeup()
{
    if (Cores.n_elems != 0)
        IVP_Controller_Wake_Or_Refresh_Associated_Sim_Unit(this);   // FUN_06348fe0
}

// FUN_06285cb0. No null tests.
void __cdecl PhySys::Controller::Register(Controller* Ctrl, CObject* Object)
{
    IVP_Core* const Core = Object->BodyPtr->core;
    Ctrl->Cores.Add(Core);

    // FUN_06349040 on the controller manager's environment box.
    IVP_Environment* const* const Holder = &Core->Environment->controller_manager->environment;
    IVP_Controller_Reconcile_Associated_Cores(Holder, Ctrl);

    IVP_Controller_Wake_Or_Refresh_Associated_Sim_Unit(Ctrl);   // FUN_06348fe0
}

// FUN_06285d10: core_is_going_to_be_deleted_event's body on Ctrl. No null test.
void __cdecl PhySys::Controller::UnRegister(Controller* Ctrl)
{
    if (Ctrl->Cores.n_elems != 0)
    {
        IVP_Controller_Detach_From_All_Cores(Ctrl, 1);   // FUN_06348f60(Ctrl, 1)
        Ctrl->Cores.Clear();
    }
}

// FUN_06285d60: only the first registered core, no loop and no test (push_core_linear).
void PhySys::Controller::push(Vector const& Force, float Dt)
{
    IVP_Core* const Core = Cores.elems[0];
    Vector const Copy = Force;
    Core->push_core_linear(&Copy, Dt);
}

// FUN_06285da0: the same on elems[0] through push_core_angular_ws.
void PhySys::Controller::rotate(Vector const& Torque, float Dt)
{
    IVP_Core* const Core = Cores.elems[0];
    Vector const Copy = Torque;
    Core->push_core_angular_ws(&Copy, Dt);
}

