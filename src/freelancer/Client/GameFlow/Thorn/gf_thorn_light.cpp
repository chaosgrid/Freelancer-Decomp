// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "GameFlow/Thorn/gf_thorn_light.h"

#include "dacom.h"
#include "Common/engine_light.h"
#include "GameFlow/Thorn/gf_script.h"
#include "GameFlow/Thorn/gf_script_object.h"
#include "x86math.h"

#include <cstdint>
#include <cstring>

namespace
{
    char const* const GScriptLightFile = "E:\\FL\\Scratch\\Source\\Client\\GF\\ScriptLight.cpp";

    // DAT_0066d27c -- this file's 3DMathEngine, acquired (FUN_005b73e0) on first use by each of
    // the three bodies that read it.
    I3DMathEngine* G_ThornLightMath = nullptr;

    I3DMathEngine* ThornLightMath()
    {
        if (G_ThornLightMath == nullptr)
            G_ThornLightMath = Acquire3DMathEngine();
        return G_ThornLightMath;
    }

    FEngineLight* EngineLightOf(FGfLightWrapper* Wrapper)
    {
        return Wrapper->Light;
    }

    // The tail shared by LAB_00453150 / LAB_004531e0 / LAB_00453330: the direction carried into
    // the current orientation (3DMathEngine slot 11, TransformMatrix(out, Orient, Direction)),
    // LookMatrixYup of it, and FUN_00410f60 on the engine light when the wrapper holds one. The
    // wrapper itself is dereferenced without a test.
    void PushLightOrientation(FThornLight& Self)
    {
        Vector Rotated;
        ThornLightMath()->TransformMatrix(Rotated, Self.Orient,
                                          Self.Direction);
        Vector Look;
        Look.x = Rotated.x;
        Look.y = Rotated.y;
        Look.z = Rotated.z;
        Matrix const LookOrient = LookMatrixYup(Look);
        FEngineLight* const Light = EngineLightOf(Self.Wrapper);
        if (Light != nullptr)
            EngineLightSetOrientation(Light, &LookOrient.E[0][0]);
    }
}

// ---------------------------------------------------------------------------------------
// Thorn_Light
// ---------------------------------------------------------------------------------------

// FUN_00452fa0. FUN_00556fd0 gets the snap's position, props+0x28 (the D3DLIGHT8), props+0x0c and
// the light group; the registry slot takes the enable byte props+0x20 != 0.
FThornLight::FThornLight(Csys const& ScriptFrame, FGfScript* InOwner, FThornEntitySnap const* Desc)
    : FThornComponent(ScriptFrame, InOwner, Desc),
      Wrapper(nullptr),
      RegistryIndex(-1)
{
    FThornLightProps const* const Props = &Desc->Light;
    Wrapper = GfLightWrapperCreate(&Desc->Pos.x, ThornLightPropsD3d(Props),
                                   ThornLightPropsTarget(Props), Desc->LtGrp);
    memcpy(&Orient, &Desc->Orient, sizeof(Orient));
    Direction.x = Desc->Light.Direction.x;
    Direction.y = Desc->Light.Direction.y;
    Direction.z = Desc->Light.Direction.z;
    RegistryIndex = GfLightRegistryAdd(Wrapper, Desc->Light.On != 0);
}

// LAB_004531e0. No script-frame transform.
GENRESULT DACOM_API FThornLight::SetTransform(const Transform* Transform12)
{
    FEngineLight* const Light = EngineLightOf(Wrapper);
    if (Light != nullptr)
        EngineLightSetPosition(Light, &Transform12->Translation);
    memcpy(&Orient, Transform12, sizeof(Orient));
    PushLightOrientation(*this);
    return GR_OK;
}

// LAB_004532d0. Without a light the position part of Out is not written.
GENRESULT DACOM_API FThornLight::GetTransform(Transform* OutTransform12)
{
    FEngineLight* const Light = EngineLightOf(Wrapper);
    if (Light != nullptr)
    {
        OutTransform12->Translation.x = Light->Position.x;
        OutTransform12->Translation.y = Light->Position.y;
        OutTransform12->Translation.z = Light->Position.z;
    }
    memcpy(OutTransform12, &Orient, sizeof(Orient));
    return GR_OK;
}

// LAB_00453130. EngineLightSetPosition is FUN_00410ef0.
GENRESULT DACOM_API FThornLight::SetPosition(const Vector* Position)
{
    FEngineLight* const Light = EngineLightOf(Wrapper);
    if (Light != nullptr)
        EngineLightSetPosition(Light, Position);
    return GR_OK;
}

// LAB_00453150.
GENRESULT DACOM_API FThornLight::SetOrientation(const Matrix* Orient9)
{
    memcpy(&Orient, Orient9, sizeof(Orient));
    PushLightOrientation(*this);
    return GR_OK;
}

// LAB_00453280. Without a light nothing is written.
GENRESULT DACOM_API FThornLight::GetPosition(Vector* OutPosition)
{
    FEngineLight* const Light = EngineLightOf(Wrapper);
    if (Light != nullptr)
    {
        OutPosition->x = Light->Position.x;
        OutPosition->y = Light->Position.y;
        OutPosition->z = Light->Position.z;
    }
    return GR_OK;
}

// LAB_004532b0.
GENRESULT DACOM_API FThornLight::GetOrientation(Matrix* OutOrient9)
{
    memcpy(OutOrient9, &Orient, sizeof(Orient));
    return GR_OK;
}

// LAB_00453310.
GENRESULT DACOM_API FThornLight::SetOn(int On)
{
    if (RegistryIndex != -1)
        GfLightRegistrySetEnabled(RegistryIndex, On != 0);
    return GR_OK;
}

// LAB_004533d0. EngineLightSetLight is FUN_00411120.
GENRESULT DACOM_API FThornLight::SetLight(void const* D3dLight8)
{
    FEngineLight* const Light = EngineLightOf(Wrapper);
    if (Light != nullptr)
        EngineLightSetLight(Light, static_cast<FD3dLight8 const*>(D3dLight8));
    return GR_OK;
}

// LAB_00453330.
GENRESULT DACOM_API FThornLight::SetDirection(Vector* Dir)
{
    Direction.x = Dir->x;
    Direction.y = Dir->y;
    Direction.z = Dir->z;
    PushLightOrientation(*this);
    return GR_OK;
}

// LAB_004533f0.
GENRESULT DACOM_API FThornLight::GetOn(int* OutOn)
{
    *OutOn = 0;
    if (RegistryIndex != -1)
        *OutOn = GfLightRegistryIsEnabled(RegistryIndex) ? 1 : 0;
    return GR_OK;
}

// LAB_00453450. 0x1a dwords from the light's +0x0c.
GENRESULT DACOM_API FThornLight::GetLight(void* OutD3dLight8)
{
    FEngineLight* const Light = EngineLightOf(Wrapper);
    if (Light != nullptr)
        memcpy(OutD3dLight8, &Light->D3d, sizeof(FD3dLight8));
    return GR_OK;
}

// LAB_00453420.
GENRESULT DACOM_API FThornLight::GetDirection(Vector* OutDir)
{
    OutDir->x = Direction.x;
    OutDir->y = Direction.y;
    OutDir->z = Direction.z;
    return GR_OK;
}

// LAB_00453100. No -1 test on the registry index.
void FThornLight::Stop()
{
    GfLightRegistryRemove(RegistryIndex);
    RegistryIndex = -1;
    GfLightWrapperDestroy(&Wrapper);
}

// FUN_00450870.
InstanceIndex FThornLight::GetEngineInstance()
{
    return static_cast<intptr_t>(-1);
}

// ---------------------------------------------------------------------------------------
// The reference forwarders (ICF-shared by the light, marker and sound references)
// ---------------------------------------------------------------------------------------

GENRESULT ThornReferenceSetPosition(FThornComponent* Target, const Vector* Position)
{
    if (Target == nullptr)
        return kThornReferenceMissing;
    return Target->SetPosition(Position);
}

GENRESULT ThornReferenceSetOrientation(FThornComponent* Target, const Matrix* Orient9)
{
    if (Target == nullptr)
        return kThornReferenceMissing;
    return Target->SetOrientation(Orient9);
}

GENRESULT ThornReferenceGetPosition(FThornComponent* Target, Vector* OutPosition)
{
    if (Target == nullptr)
        return kThornReferenceMissing;
    return Target->GetPosition(OutPosition);
}

GENRESULT ThornReferenceGetOrientation(FThornComponent* Target, Matrix* OutOrient9)
{
    if (Target == nullptr)
        return kThornReferenceMissing;
    return Target->GetOrientation(OutOrient9);
}

GENRESULT ThornReferenceSetTransform(FThornComponent* Target, const Transform* Xform12)
{
    if (Target == nullptr)
        return kThornReferenceMissing;
    return Target->SetTransform(Xform12);
}

GENRESULT ThornReferenceGetTransform(FThornComponent* Target, Transform* OutXform12)
{
    if (Target == nullptr)
        return kThornReferenceMissing;
    return Target->GetTransform(OutXform12);
}

InstanceIndex ThornReferenceGetEngineInstance(FThornComponent* Target)
{
    if (Target == nullptr)
        return static_cast<intptr_t>(-1);
    return Target->GetEngineInstance();
}

// ---------------------------------------------------------------------------------------
// Thorn_LightReference
// ---------------------------------------------------------------------------------------

// FUN_00453480. The lookup uses the snap's name, not the malloc'd copy.
FThornLightReference::FThornLightReference(Csys const& ScriptFrame, FGfScript* InOwner,
                                           FThornEntitySnap const* Desc)
    : FThornComponent(ScriptFrame, InOwner, Desc),
      Target(nullptr)
{
    Target = G_GfScriptManager.FindCreatedEntity(kThornEntityLight, Desc->Name);
    if (Target == nullptr)
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: invalid light reference to name %s",
              GScriptLightFile, 0x177, Desc->Name);
}

// LAB_00457c00 / LAB_00457c20.
GENRESULT DACOM_API FThornLightReference::SetTransform(const Transform* Transform12)
{
    return ThornReferenceSetTransform(Target, Transform12);
}

GENRESULT DACOM_API FThornLightReference::GetTransform(Transform* OutTransform12)
{
    return ThornReferenceGetTransform(Target, OutTransform12);
}

// LAB_00457bc0 / LAB_00457be0 / LAB_00453560 / LAB_00453580.
GENRESULT DACOM_API FThornLightReference::SetPosition(const Vector* Position)
{
    return ThornReferenceSetPosition(Target, Position);
}

GENRESULT DACOM_API FThornLightReference::SetOrientation(const Matrix* Orient9)
{
    return ThornReferenceSetOrientation(Target, Orient9);
}

GENRESULT DACOM_API FThornLightReference::GetPosition(Vector* OutPosition)
{
    return ThornReferenceGetPosition(Target, OutPosition);
}

GENRESULT DACOM_API FThornLightReference::GetOrientation(Matrix* OutOrient9)
{
    return ThornReferenceGetOrientation(Target, OutOrient9);
}

// LAB_004535a0 .. LAB_00453640 -- the target's D3D-light facet, same slot; -1 without one.
GENRESULT DACOM_API FThornLightReference::SetOn(int On)
{
    if (Target == nullptr)
        return kThornReferenceMissing;
    return static_cast<ID3DLightControl*>(Target)->SetOn(On);
}

GENRESULT DACOM_API FThornLightReference::SetLight(void const* D3dLight8)
{
    if (Target == nullptr)
        return kThornReferenceMissing;
    return Target->SetLight(D3dLight8);
}

GENRESULT DACOM_API FThornLightReference::SetDirection(Vector* Dir)
{
    if (Target == nullptr)
        return kThornReferenceMissing;
    return Target->SetDirection(Dir);
}

GENRESULT DACOM_API FThornLightReference::GetOn(int* OutOn)
{
    if (Target == nullptr)
        return kThornReferenceMissing;
    return Target->GetOn(OutOn);
}

GENRESULT DACOM_API FThornLightReference::GetLight(void* OutD3dLight8)
{
    if (Target == nullptr)
        return kThornReferenceMissing;
    return Target->GetLight(OutD3dLight8);
}

GENRESULT DACOM_API FThornLightReference::GetDirection(Vector* OutDir)
{
    if (Target == nullptr)
        return kThornReferenceMissing;
    return Target->GetDirection(OutDir);
}

// LAB_00450cd0.
void FThornLightReference::Stop()
{
    Target = nullptr;
}

// LAB_00453550.
InstanceIndex FThornLightReference::GetEngineInstance()
{
    return ThornReferenceGetEngineInstance(Target);
}
