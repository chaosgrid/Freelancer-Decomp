// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// SpaceDust -- retail Client\Space\SpaceDust.cpp: LAB_0054bda0 / LAB_0054bdb0 (static init),
// FUN_0054bdc0, FUN_0054bde0, FUN_0054be20, FUN_0054beb0, FUN_0054c320.

#include "Space/space_dust.h"

#include "Space/Effects/effect_type_list.h"
#include "Core/game_options.h"
#include "Space/Effects/particle_effect.h"
#include "Core/perf_options.h"
#include "Space/star_sphere.h"
#include "Common/Graphics/vis_effect_arch.h"

#include "common_csys.h"
#include "common_id.h"
#include "common_ini.h"
#include "common_misc.h"
#include "dacom.h"
#include "x86math.h"

namespace
{
    char const kSpaceDustCppPath[] = "E:\\FL\\Scratch\\Source\\Client\\Space\\SpaceDust.cpp";

    // Retail constants.
    float const kDustDetailThreshold = 0.75f;           // DAT_005c8930
    float const kDustIntensityPerParticle = 0.002f;     // DAT_005d3e94
    float const kOneF = 1.0f;                           // DAT_005c75dc
    double const kFadeCutoff = 1e-5;                    // DAT_005c75f0
    double const kOneD = 1.0;                           // DAT_005c89b8
    double const kSystemDustIntensityScale = 0.002;     // DAT_005e08f8
    int const kDefaultZoneDustParticles = 0x32;
    unsigned const kPropDustZoneMask = Universe::kZonePropSpacedust;

    // DAT_00614084 (.data, read only by FUN_0054beb0): the dust frame sits this far along the
    // camera's Z axis.
    int const kSpaceDustOffsetZ = -60;

    CacheString MakeClearedCacheString()
    {
        CacheString Result;
        Result.clear();
        return Result;
    }

    // LAB_0054bda0 / LAB_0054bdb0 -- static init: CacheString::clear on each name.
    CacheString G_SystemDust = MakeClearedCacheString(); // DAT_00679794
    CacheString G_ZoneDust = MakeClearedCacheString();   // DAT_00679790

    I3DMathEngine* G_SpaceDustMath = nullptr; // DAT_006797a4 -- FUN_0054c320's lazy engine

    int G_SystemDustAtFullDetail = 0;         // DAT_006797a8 (0x50 after [Dust])
    int G_SystemDustAtZeroDetail = 0;         // DAT_006797ac (0x1e after [Dust])
    int G_ZoneDustParticles = 0;              // DAT_006797b0
    int G_FadingDustParticles = 0;            // DAT_006797b4
    FParticleEffect* G_SystemDustEffect = nullptr; // DAT_006797b8
    FParticleEffect* G_ZoneDustEffect = nullptr;   // DAT_006797bc
    FParticleEffect* G_FadingDustEffect = nullptr; // DAT_006797c0 -- previous zone's, fading

    // ParticleEffect vtable slots called here (PTR_FUN_005d9ad4): +0x00 Start, +0x04 the
    // death arm (LAB_004f84d0), +0x0c SetIntensity (LAB_004f8c20). Retail calls them
    // virtually; so does the port.

    void DustArmDeath(FParticleEffect* Effect)
    {
        Effect->Kill();
    }

    // FUN_0054bdc0
    void ReleaseDustEffect(FParticleEffect** Slot)
    {
        if (*Slot != nullptr)
        {
            DustArmDeath(*Slot);
            ParticleEffectRelease(*Slot); // FUN_004f7a90
            *Slot = nullptr;
        }
    }

    // FUN_0054c320 (thiscall on the camera transform): Out = CamXform * Offset through the
    // file's own lazily acquired 3DMathEngine.
    void ComposeDustXform(Transform const* CamXform, Transform* Out, Transform const* Offset)
    {
        if (G_SpaceDustMath == nullptr)
            G_SpaceDustMath = Acquire3DMathEngine();
        Transform Local;
        G_SpaceDustMath->MulTransform(Local, *CamXform, *Offset);
        *Out = Local;
    }

    // Inlined twice in FUN_0054beb0: allocate, then start it on the archetype named Name, or
    // FDUMP.
    void CreateDustEffect(FParticleEffect** Slot, char const* Name)
    {
        *Slot = ParticleEffectCreate();
        unsigned const NicknameId = CreateID(Name);
        FEffectArch* const Arch = FindEffectArch(&NicknameId);
        if (Arch != nullptr)
        {
            (*Slot)->Start(Arch);
        }
        else
        {
            FDUMP(kFdumpError,
                  "%s(%d) : *** ERROR: no archetype found for effect named \"%s\" in space dust",
                  kSpaceDustCppPath, 0x49, Name);
        }
    }

    // Inlined three times in FUN_0054beb0: place the effect on the dust frame, set its
    // intensity, mark it (+0x08 |= 1) and link it (FUN_004f7af0).
    void PlaceDustEffect(FParticleEffect* Effect, Transform const& DustXform, float Intensity)
    {
        Effect->LocalXform = DustXform;
        Effect->SetIntensity(Intensity);
        Effect->Flags08 = static_cast<unsigned char>(Effect->Flags08 | 1);
        EffectTypeListLink(Effect);
    }
}

void SpaceDustSetSystemDust(CacheString const* Dust)
{
    // FUN_0054bde0
    G_SystemDust.Value = Dust->Value;
    G_SystemDustAtFullDetail = 0x50;
    G_SystemDustAtZeroDetail = 0x1e;
    G_ZoneDust.clear();
    G_ZoneDustParticles = 0;
    G_FadingDustParticles = 0;
}

void SpaceDustShutdown()
{
    // FUN_0054be20 -- the same release sequence as FUN_0054bdc0, inlined.
    ReleaseDustEffect(&G_FadingDustEffect);
    ReleaseDustEffect(&G_ZoneDustEffect);
    G_ZoneDust.clear();
    G_ZoneDustParticles = 0;
    G_FadingDustParticles = 0;
    ReleaseDustEffect(&G_SystemDustEffect);
    G_SystemDust.clear();
    G_SystemDustAtFullDetail = 0;
}

void SpaceDustUpdate(Universe::ISystem const* System, void* Camera)
{
    // FUN_0054beb0

    // Background option off -- drop every dust effect (no early return).
    if (GetGameOption(kGameOptBackgroundGroup) == 0 &&
        (G_SystemDustEffect != nullptr || G_ZoneDustEffect != nullptr ||
         G_FadingDustEffect != nullptr))
    {
        ReleaseDustEffect(&G_FadingDustEffect);
        ReleaseDustEffect(&G_ZoneDustEffect);
        G_ZoneDust.clear();
        G_ZoneDustParticles = 0;
        G_FadingDustParticles = 0;
        ReleaseDustEffect(&G_SystemDustEffect);
    }

    Transform Offset;
    Win3DIdentityRotation3x3(&Offset.Rotation); // FUN_004215b0
    Offset.Translation.x = 0.0f;
    Offset.Translation.y = 0.0f;
    Offset.Translation.z = static_cast<float>(kSpaceDustOffsetZ);
    Transform const* const CamXform =
        (Transform const*)((ICamera*)Camera)->GetOrientation();
    Transform DustXform;
    ComposeDustXform(CamXform, &DustXform, &Offset);
    Vector const& DustPos = DustXform.Translation;

    // The zone dust runs only when detail > 0.75 (not when unordered).
    if (GetPerfSpaceDustDetail() > kDustDetailThreshold) // DAT_00614080
    {
        CacheString Name;
        Name.clear();
        Name.clear();
        int Particles = 0;
        float Blend = 1.0f;
        if (System != nullptr)
        {
            Universe::IZone const* const Zone = System->GetSmallestZoneWithPropertiesAt(&DustPos, kPropDustZoneMask);
            if (Zone != nullptr)
            {
                if (!Zone->GetSpacedust(Name.Value, Particles))
                {
                    FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kSpaceDustCppPath, 0xa8,
                          "zone flagged as PROP_DUST_ZONE but has no dust");
                }
                if (Particles == 0)
                    Particles = kDefaultZoneDustParticles;
                Blend = Zone->GetImmersion(DustPos);
            }
        }

        // A different zone dust name (pointer compare, as retail) moves the current zone effect
        // to the fading slot and starts the new one.
        if (Name.Value != G_ZoneDust.Value)
        {
            G_ZoneDust.Value = Name.Value;
            if (G_FadingDustEffect != nullptr)
            {
                DustArmDeath(G_FadingDustEffect);
                ParticleEffectRelease(G_FadingDustEffect);
            }
            G_FadingDustEffect = G_ZoneDustEffect;
            G_FadingDustParticles = G_ZoneDustParticles;
            G_ZoneDustEffect = nullptr;
            if (Name.Value[0] != '\0')
                CreateDustEffect(&G_ZoneDustEffect, Name.Value);
        }

        // The fading effect dies once its level reaches 1e-5.
        G_ZoneDustParticles = Particles;
        if (G_FadingDustEffect != nullptr)
        {
            double const Fade =
                (static_cast<double>(G_FadingDustParticles) *
                 static_cast<double>(kDustIntensityPerParticle)) *
                (static_cast<double>(kOneF) - static_cast<double>(Blend));
            // It stays when Fade > 1e-5 or unordered.
            if (!(Fade <= kFadeCutoff))
            {
                PlaceDustEffect(G_FadingDustEffect, DustXform, static_cast<float>(Fade));
            }
            else
            {
                DustArmDeath(G_FadingDustEffect);
                ParticleEffectRelease(G_FadingDustEffect);
                G_FadingDustEffect = nullptr;
            }
        }

        if (G_ZoneDustEffect != nullptr)
        {
            float const Level = static_cast<float>(
                (static_cast<double>(G_ZoneDustParticles) *
                 static_cast<double>(kDustIntensityPerParticle)) *
                static_cast<double>(Blend));
            PlaceDustEffect(G_ZoneDustEffect, DustXform, Level);
        }
    }
    else
    {
        ReleaseDustEffect(&G_FadingDustEffect);
        ReleaseDustEffect(&G_ZoneDustEffect);
        G_ZoneDust.clear();
        G_ZoneDustParticles = 0;
        G_FadingDustParticles = 0;
    }

    // The system's own dust.
    if (G_SystemDustEffect != nullptr)
    {
        double const Detail = static_cast<double>(GetPerfSpaceDustDetail()); // DAT_00614080
        float const Level = static_cast<float>(
            ((kOneD - Detail) * static_cast<double>(G_SystemDustAtZeroDetail) +
             static_cast<double>(G_SystemDustAtFullDetail) * Detail) *
            kSystemDustIntensityScale);
        PlaceDustEffect(G_SystemDustEffect, DustXform, Level);
        return;
    }
    if (G_SystemDust.Value[0] != '\0')
        CreateDustEffect(&G_SystemDustEffect, G_SystemDust.Value);
}
