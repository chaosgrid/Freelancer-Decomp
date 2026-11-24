// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Physics/common_physys_internal.h"

using namespace CmnPhySys;

// ============================================================================
// SphereIntruderChecker
// ============================================================================

// FUN_06332470
SphereIntruderChecker::SphereIntruderChecker()
{
}

// FUN_063324a0
SphereIntruderChecker::SphereIntruderChecker(SphereIntruderChecker const& Other)
    : Intruders(Other.Intruders)
{
}

// FUN_06332610
SphereIntruderChecker::~SphereIntruderChecker()
{
}

// FUN_06332530: the list assignment, skipped for self-assignment.
SphereIntruderChecker& SphereIntruderChecker::operator=(SphereIntruderChecker const& Other)
{
    if (&Intruders != &Other.Intruders)
    {
        Intruders = Other.Intruders;
    }
    return *this;
}

// FUN_06332420: push_back.
void SphereIntruderChecker::added(CObject* Object)
{
    Intruders.push_back(Object);
}

// Slot 1, an ICF-shared no-op.
void SphereIntruderChecker::deleted(CObject* /*Object*/)
{
}

// FUN_06332660. A ball phantom with this as its listener: switching its collision state on
// reports each intruder through added(), then it is destroyed. Adjacent duplicates are removed
// (list::unique) and the result copied to Out. The result of CreatePhantom is not tested.
void SphereIntruderChecker::check_sphere(unsigned SystemId, Vector const& Center, float Radius,
                                         std::list<CObject*>& Out)
{
    Intruders.clear();

    PhySys::PhantomParms Parms;
    memset(&Parms, 0, sizeof(Parms));  // +0x07..+0x0f are uninitialised in retail; never read
    Parms.SystemId = SystemId;
    Parms.bPhysicalUnmoveable = 0;
    Parms.Flag05 = 0;
    Parms.Name[0] = '\0';
    Parms.Position = Center;
    Parms.Orientation.E[0][0] = 1.0f;
    Parms.Orientation.E[0][1] = 0.0f;
    Parms.Orientation.E[0][2] = 0.0f;
    Parms.Orientation.E[1][0] = 0.0f;
    Parms.Orientation.E[1][1] = 1.0f;
    Parms.Orientation.E[1][2] = 0.0f;
    Parms.Orientation.E[2][0] = 0.0f;
    Parms.Orientation.E[2][1] = 0.0f;
    Parms.Orientation.E[2][2] = 1.0f;
    Parms.Listener = this;

    void* Handle = nullptr;  // CreatePhantom writes it on every path
    PhySys::CreatePhantom(Parms, Radius, Handle);
    PhySys::SetCollisionStatePhantom(Handle, true);
    PhySys::DestroyPhantom(Handle);

    Intruders.unique();

    if (&Out != &Intruders)
    {
        Out = Intruders;
    }
}

