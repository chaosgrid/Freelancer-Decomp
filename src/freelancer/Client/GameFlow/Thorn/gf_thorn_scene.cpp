// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "GameFlow/Thorn/gf_thorn_scene.h"

#include "DALib.h"
#include "GameFlow/Thorn/gf_script.h"
#include "soundmanager.h"

#include <cstdint>
#include <cstring>

// FUN_00454390. Retail copies 0xf dwords from Desc+0x44 to a local and keeps the first three;
// the other twelve are never read.
FThorn_Scene::FThorn_Scene(Csys const& ScriptFrame, FGfScript* InOwner, FThornEntitySnap const* Desc)
    : FThornComponent(ScriptFrame, InOwner, Desc)
{
    unsigned int Blob[15];
    int Index = 0;
    for (Index = 0; Index < 15; ++Index)
        Blob[Index] = Desc->SceneBlob[Index];
    memcpy(Ambient, Blob, sizeof(Ambient));
}

// LAB_00454450.
GENRESULT DACOM_API FThorn_Scene::SetOn(unsigned char On)
{
    GfFogSetOn(On);
    return GR_OK;
}

// LAB_00454480. The FUN_00454700 answer (the fog really in effect), not the stored byte.
GENRESULT DACOM_API FThorn_Scene::GetOn(unsigned char* OutOn)
{
    *OutOn = GfFogIsActive() ? 1 : 0;
    return GR_OK;
}

// LAB_00454500.
GENRESULT DACOM_API FThorn_Scene::SetColor(int const* Rgb3)
{
    GfFogSetColor(Rgb3);
    return GR_OK;
}

// LAB_00454530.
GENRESULT DACOM_API FThorn_Scene::GetColor(int* OutRgb3)
{
    GfFogGetColor(OutRgb3);
    return GR_OK;
}

// LAB_00454570.
GENRESULT DACOM_API FThorn_Scene::SetRange(float Start, float End, float Density)
{
    return SetRangeValue(&Start, &End, &Density);
}

GENRESULT DACOM_API FThorn_Scene::SetRangeValue(const float* Start, const float* End, const float* Density)
{
    GfFogSetRangeValue(Start, End, Density);
    return GR_OK;
}

// LAB_004545a0.
GENRESULT DACOM_API FThorn_Scene::GetRange(float* OutStart, float* OutEnd, float* OutDensity)
{
    GfFogGetRange(OutStart, OutEnd, OutDensity);
    return GR_OK;
}

// LAB_004545d0.
GENRESULT DACOM_API FThorn_Scene::SetTableMode(unsigned char Table, unsigned int Mode)
{
    GfFogSetMode(Table, Mode);
    return GR_OK;
}

// LAB_00454600.
GENRESULT DACOM_API FThorn_Scene::GetTableMode(unsigned char* OutTable, unsigned int* OutMode)
{
    GfFogGetMode(OutTable, OutMode);
    return GR_OK;
}

// FUN_00454640. Through the facet's own vtable, in this order.
GENRESULT DACOM_API FThorn_Scene::SetPacked(unsigned char const* Packed32)
{
    IFogControl* const Fog = static_cast<IFogControl*>(this);
    Fog->SetOn(Packed32[0]);
    Fog->SetColor(reinterpret_cast<int const*>(Packed32 + 4));
    float RangeValues[3];
    std::memcpy(RangeValues, Packed32 + 0x14, sizeof(RangeValues));
    Fog->SetRangeValue(&RangeValues[0], &RangeValues[1], &RangeValues[2]);
    Fog->SetTableMode(Packed32[1], *reinterpret_cast<unsigned int const*>(Packed32 + 0x10));
    return GR_OK;
}

// FUN_00454690.
GENRESULT DACOM_API FThorn_Scene::GetPacked(unsigned char* Out32)
{
    IFogControl* const Fog = static_cast<IFogControl*>(this);
    float* const Range = reinterpret_cast<float*>(Out32 + 0x14);
    Fog->GetOn(Out32);
    Fog->GetColor(reinterpret_cast<int*>(Out32 + 4));
    Fog->GetRange(&Range[0], &Range[1], &Range[2]);
    Fog->GetTableMode(Out32 + 1, reinterpret_cast<unsigned int*>(Out32 + 0x10));
    return GR_OK;
}

// LAB_00454790. DALib::Sound slot 11 with the four dwords as given.
GENRESULT DACOM_API FThorn_Scene::SetReverb(unsigned int Environment, float Volume, float Decay,
                                           float Damping)
{
    if (DALib::Sound != nullptr)
        DALib::Sound->SetEaxPreview(static_cast<int>(Environment), Volume, Decay, Damping);
    return GR_OK;
}

// LAB_004547c0. DALib::Sound slot 12.
GENRESULT DACOM_API FThorn_Scene::GetReverb(float* OutEnvironment, float* OutVolume,
                                           float* OutDecay, float* OutDamping)
{
    if (DALib::Sound != nullptr)
        DALib::Sound->GetEaxPreview(reinterpret_cast<unsigned long*>(OutEnvironment),
                                    reinterpret_cast<unsigned long*>(OutVolume),
                                    reinterpret_cast<unsigned long*>(OutDecay),
                                    reinterpret_cast<unsigned long*>(OutDamping));
    return GR_OK;
}

// FUN_00450870.
InstanceIndex FThorn_Scene::GetEngineInstance()
{
    return static_cast<intptr_t>(-1);
}

// LAB_00453ec0 (ICF-shared with Thorn_Psys geo slot 3).
GENRESULT DACOM_API FThorn_Scene::SetAmbient(float const* Rgb3)
{
    Ambient[0] = Rgb3[0];
    Ambient[1] = Rgb3[1];
    Ambient[2] = Rgb3[2];
    return GR_OK;
}

// LAB_00454770 (ICF-shared with Thorn_Psys geo slot 5).
GENRESULT DACOM_API FThorn_Scene::GetAmbient(float* OutRgb3)
{
    OutRgb3[0] = Ambient[0];
    OutRgb3[1] = Ambient[1];
    OutRgb3[2] = Ambient[2];
    return GR_OK;
}
