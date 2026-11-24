// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_nullbehavior.h"

// Retail has no dedicated ctor: the id-0 factory FUN_062d16d0 does `operator new(0x2c)`,
// FUN_062d2020 with the type id, and installs the vtable (063a0244) inline. This constructor
// stands in for that sequence.
NullBehavior::NullBehavior(int InTypeId)
    : Behavior(InTypeId)
{
    // The initialiser values are FUN_062d2020's own: +0x0c = 2, +0x10 = 1, the rest zero.
}

// Slot 0 is FUN_062c7090, the scalar deleting dtor shared by 7 vtables; no class-specific
// cleanup.
NullBehavior::~NullBehavior()
{
}

// Slot 1, LAB_062d76a0: unconditionally succeeds and touches nothing, unlike every sibling.
bool NullBehavior::SetContext(void* /*Param1*/, void* /*Param2*/, void* /*Param3*/)
{
    return true;
}

// Slot 7, ICF-folded with Archetype::Solar::get_class_type.
int NullBehavior::Update()
{
    return 2;
}
