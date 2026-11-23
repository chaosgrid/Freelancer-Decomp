// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_material.h"
#include "ivp_mindist.h"

// ============================================================================
// FUN_0634bec0. +0x04 is not written.
// ============================================================================
IVP_Material_Simple::IVP_Material_Simple(float FrictionValue, float ElasticityValue)
{
    FrictionFactor = FrictionValue;
    SecondFrictionXEnabled = 0;
    SecondFrictionFactor = 0.0f;
    Elasticity = ElasticityValue;
    Adhesion = 0.0f;
}

// ============================================================================
// FUN_0634be10, IVP_Material_Manager slot 0 (both arguments unread). A
// function-local static (DAT_0658a9f0, guard bit 0 of DAT_0658a9f4); no atexit
// is registered, so the material is never freed.
// ============================================================================
IVP_Material* IVP_Material_Manager::get_material_by_index(const IVP_U_Point* WorldPosition, int Index)
{
    (void)WorldPosition;
    (void)Index;
    static IVP_Material_Simple* DefaultMaterial = new IVP_Material_Simple(0.5f, 0.5f);
    return DefaultMaterial;
}

// ============================================================================
// LAB_0634bcf0. The second side (+0x44) is asked first and spilled to float.
// ============================================================================
float IVP_Material_Manager::get_friction_factor(IVP_Impact_Solver_Long_Term* Contact)
{
    const float SecondSide = Contact->Materials[1]->get_friction_factor();
    return Contact->Materials[0]->get_friction_factor() * SecondSide;
}

// ============================================================================
// LAB_0634bd20. As slot 1, but the first side (+0x40) is asked first.
// ============================================================================
float IVP_Material_Manager::get_elasticity(IVP_Impact_Solver_Long_Term* Contact)
{
    const float FirstSide = Contact->Materials[0]->get_elasticity();
    return Contact->Materials[1]->get_elasticity() * FirstSide;
}

// ============================================================================
// LAB_0634bd50. First side first, summed.
// ============================================================================
float IVP_Material_Manager::get_adhesion(IVP_Impact_Solver_Long_Term* Contact)
{
    const float FirstSide = Contact->Materials[0]->get_adhesion();
    return Contact->Materials[1]->get_adhesion() + FirstSide;
}

// ============================================================================
// LAB_0634bda0. Deletes through slot 4; retail's `this != null` test is the
// delete expression's own.
// ============================================================================
void IVP_Material_Manager::environment_will_be_deleted(IVP_Environment* Environment)
{
    (void)Environment;
    if (DeleteOnEnvDelete != 0)
    {
        delete this;
    }
}
