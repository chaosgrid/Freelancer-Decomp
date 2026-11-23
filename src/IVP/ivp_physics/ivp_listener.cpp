// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_listener.h"

#include "ivp_real_object.h"

#include <string.h>

// FUN_0634bcb0
IVP_Collision_Filter_Coll_Group_Ident::IVP_Collision_Filter_Coll_Group_Ident(int32_t bInDeleteOnEnvDelete)
    : bDeleteOnEnvDelete(bInDeleteOnEnvDelete)
{
}

// FUN_0634bc60. Compares each object's 8-byte group tag at +0x84.
int32_t IVP_Collision_Filter_Coll_Group_Ident::check_objects_for_collision_detection(IVP_Real_Object* A,
                                                                                     IVP_Real_Object* B)
{
    if (A->name[0] == '\0' || B->name[0] == '\0')
        return 1;
    return strncmp(A->name, B->name, 8) != 0 ? 1 : 0;
}

// LAB_0634bcd0. Retail's extra null-this test cannot fire through a virtual call.
void IVP_Collision_Filter_Coll_Group_Ident::environment_will_be_deleted(IVP_Environment* /*Env*/)
{
    if (bDeleteOnEnvDelete != 0)
        delete this;
}
