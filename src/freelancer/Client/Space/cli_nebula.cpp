// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Space/cli_nebula.h"

#include "Space/Effects/cli_projectile_fx.h" // CliEffectCompanionArmStop / CliEffectCompanionClear (+0xe4)
#include "Common/Graphics/client_mesh_load.h"
#include "common_csys.h"
#include "common_id.h"
#include "common_ini.h"
#include "common_misc.h"
#include "common_net.h"
#include "common_universe.h"
#include "Space/Effects/effect_base.h"    // FEffectBase (the effect at Nebula +0xec)
#include "Common/engine_light.h"
#ifdef FREELANCER_RP9
#include "Space/nebula_volume.h" // port-only Direct3D 9 volumetric nebula gates
#endif
#include "Common/Graphics/pixel_format.h"   // PixelFormatFromD3D / PixelFormatGetDesc (FUN_005b7010 / 005b7030)
#include "Space/Cameras/space_camera.h"
#include "Space/star_sys.h"
#include "Common/Graphics/texture_panels.h"
#include "Common/Graphics/texture_shapes.h"
#include "Interface/win3d_immediate.h"
#include "Interface/win3d_render.h"

#include "DALib.h"
#include "engine/engbase.h"
#include "render/IRenderPipeline.h"
#include "render/rendcomp.h"
#include "render/shading.h"
#include "render/slots.h"      // Render::EDeviceAbility
#include "x86math.h"

#include <list>
#include <map>
#include <math.h>
#include <new>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

I3DMathEngine* G_CloudsMath = nullptr; // DAT_00679860 (cli_nebula.h)

// The zone_shell mesh an [Exclusion Zones] section loads (operator new(0x38)).
// +0x00 the ObjectHandle (FUN_00420d40); the seven floats +0x10..+0x28 start at 1.0f;
// +0x10 is handed to the engine as the instance's user data, and the exclusion-shell draw
// copies the tint into +0x10..+0x18 and the faded alpha into +0x20. +0x24 shell_scalar
// (raised to 1), +0x28 max_alpha (capped at 1), +0x2c..+0x34 exclusion_tint (each capped at
// 1) -- the tint is never initialised when the key is absent, as in retail.
struct FNebulaZoneShell
{
    FMeshHolder Handle;   // +0x00
    float UserData[5];    // +0x10 colour r, g, b; +0x1c; +0x20 alpha
    float ShellScalar;    // +0x24
    float MaxAlpha;       // +0x28
    float Tint[3];        // +0x2c
};
static_assert(sizeof(FNebulaZoneShell) == 0x38, "zone_shell mesh is 0x38");
static_assert(offsetof(FNebulaZoneShell, ShellScalar) == 0x24, "zone_shell +0x24");
static_assert(offsetof(FNebulaZoneShell, Tint) == 0x2c, "zone_shell +0x2c");


namespace
{
    // .data defaults.
    float G_NebulaDetail = 1.0f;         // DAT_006132dc
    float G_NebulaDetailEase = 0.707f;   // DAT_006132e0
    float G_NebulaDetailMix = 0.35f;     // DAT_006132e4
    float G_NebulaDetailFog = 0.5f;      // DAT_006132e8

    float BitsToFloat(unsigned Bits)
    {
        float V = 0.0f;
        memcpy(&V, &Bits, sizeof(float));
        return V;
    }

    // The "color" keys of FUN_00504e60 and FUN_00550240: three values, each * 1/255 (float
    // 0x005c7f68) and stored as float, into Out[0..2].
    void ReadColor255(INI_Reader& Reader, float* Out)
    {
        float const R = Reader.get_value_float(0);
        float const G = Reader.get_value_float(1);
        float const Bl = Reader.get_value_float(2);
        Out[0] = R * (1.0f / 255.0f);
        Out[1] = G * (1.0f / 255.0f);
        Out[2] = Bl * (1.0f / 255.0f);
    }

    I3DMathEngine* AcquireNebulaMath()
    {
        // DAT_00675000 — lazy 3DMathEngine (FUN_005b73e0).
        static I3DMathEngine* G_NebulaMath = nullptr;
        if (G_NebulaMath == nullptr)
            G_NebulaMath = Acquire3DMathEngine();
        return G_NebulaMath;
    }

    // Nebula +0x38: std::map<unsigned (CreateID of the mesh name), zone_shell*>. Retail's VC6
    // map (FUN_004e7f60 ctor, lower_bound FUN_0052c7a0 / FUN_0041bd10, insert FUN_004b68b0,
    // erase FUN_00509d60); the port keeps a real std::map in the same bytes (FNebulaZoneShellMap,
    // cli_nebula.h).
    FNebulaZoneShellMap& NebulaZoneShells
(FNebulaEffect* Nebula)
    {
        return Nebula->ZoneShells;
    }

    // The two-step find every reader inlines: lower_bound, then end unless !(Key < node key).
    FNebulaZoneShellMap::iterator NebulaFindZoneShell(FNebulaZoneShellMap& Map, unsigned Key)
    {
        FNebulaZoneShellMap::iterator It = Map.lower_bound(Key);
        if (It == Map.end() || Key < It->first)
            return Map.end();
        return It;
    }

    // The nebula's ShapeDB lives in the retail bytes at +0x04 (FUN_00509c30 builds it there).
    FShapeDB* NebulaShapes(FNebulaEffect* Nebula)
    {
        return &Nebula->Shapes;
    }

    // FUN_00506eb0 -- the zone_shell's destructor: FUN_00420f70 (instance and both material
    // slots released), then ~ObjectHandle FUN_00420d10 (vptr reset only). The caller frees the
    // memory.
    void NebulaZoneShellDestroy(FNebulaZoneShell* Shell)
    {
        Shell->Handle.Release();
    }

    // DAT_0067505c/60/64 is the TexturePanels instance list, not a nebula list; it and its
    // walks (FUN_00509f30 / FUN_00509f60) live in texture_panels.cpp.

    int ClampByte255(int V)
    {
        if (V < 0)
            return 0;
        if (V > 255)
            return 255;
        return V;
    }

} // namespace

// FUN_00422440 (cdecl). Each colour channel * 255.0f (DAT_005c75d8), truncated and clamped
// to [0,0xff]; the float*float product is exact in double, as in the x87 register. No null
// tests: retail dereferences Rgb3 and DALib::Pipeline directly.
void FogApplyColorAndRange(float const* Rgb3, float FogStart, float FogEnd)
{
    int R = static_cast<int>(static_cast<double>(Rgb3[0]) * 255.0);
    if (R < 0)
        R = 0;
    else if (R > 0xff)
        R = 0xff;
    int G = static_cast<int>(static_cast<double>(Rgb3[1]) * 255.0);
    if (G < 0)
        G = 0;
    else if (G > 0xff)
        G = 0xff;
    int B = static_cast<int>(static_cast<double>(Rgb3[2]) * 255.0);
    if (B < 0)
        B = 0;
    else if (B > 0xff)
        B = 0xff;

    // Pipeline vt+0x20 queries device ability 0xd (W-fog), preset to 0. (GetPipelineState,
    // vt+0x18, reads an unrelated setting.)
    unsigned long WFogAbility = 0;
    DALib::Pipeline->QueryDeviceAbility(Render::FogW, &WFogAbility);
    if (WFogAbility == 0)
    {
        DALib::Pipeline->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_NONE);
        DALib::Pipeline->SetRenderState(D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
    }
    else
    {
        DALib::Pipeline->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_LINEAR);
        DALib::Pipeline->SetRenderState(D3DRS_FOGVERTEXMODE, D3DFOG_NONE);
        DALib::Pipeline->SetRenderState(D3DRS_RANGEFOGENABLE, 0);
    }

    unsigned Packed = static_cast<unsigned>(R) | 0xffffff00u;
    Packed = (Packed << 8) | static_cast<unsigned>(G);
    Packed = (Packed << 8) | static_cast<unsigned>(B);
    DALib::Pipeline->SetRenderState(D3DRS_FOGCOLOR, static_cast<int>(Packed));

    int FogStartBits = 0;
    int FogEndBits = 0;
    memcpy(&FogStartBits, &FogStart, sizeof(float));
    memcpy(&FogEndBits, &FogEnd, sizeof(float));
    DALib::Pipeline->SetRenderState(D3DRS_FOGSTART, FogStartBits);
    DALib::Pipeline->SetRenderState(D3DRS_FOGEND, FogEndBits);
    DALib::Pipeline->SetRenderState(D3DRS_FOGENABLE, 1);
}

namespace
{

    // DALib::MaterialBatcher slot +0x20. Retail calls through the import with no null test.
    void SetMaterialBatchBinOrder(unsigned long Index, long Key)
    {
        DALib::MaterialBatcher->SetBinOrder(Index, Key);
    }

    // FUN_005097c0 — ambient GameLight at +0x270: SetPosition, flicker accum, colour/enable.
    // Called from FNebulaEffect::Update. No test of `this` or of the position pointer.
    void NebulaAmbientTick(FNebulaEffect* Nebula, Vector const* CamPos3)
    {
        // The light at +0x270 is the one pointer retail tests.
        FGameLight* const GLight = Nebula->DynamicLight;
        if (GLight == nullptr)
            return;
#ifdef FREELANCER_RP9
        // Port-only: while the Direct3D 9 volumetric nebula's lightning strikes drive this light
        // (direction from the strike, its flicker; nebula_volume.cpp), the random flash below
        // stays out. Never true on RP8 or with the volume off.
        if (NebulaVolume::DrivesNebulaLight(Nebula))
            return;
#endif
        FEngineLight* const Light = GLight;
        // Return when the period is <= 0; a NaN period goes on.
        float const Period = Nebula->DynLightningDuration;
        if (Period <= 0.0f)
            return;

        Light->SetPosition(CamPos3);

        // The sum is stored as float but the unrounded value is compared below.
        float const Dt = Nebula->FrameDelta;
        float const AccPrev = Nebula->DynLightningTimer;
        double const Acc = static_cast<double>(Dt) + AccPrev;
        Nebula->DynLightningTimer = static_cast<float>(Acc);

        // An old value not above 0 returns unless the new sum is above 0.
        if (!(AccPrev > 0.0f))
        {
            if (!(Acc > 0.0))
                return;
        }
        // Only a sum strictly above the period re-arms; anything else takes the colour path.
        else if (Acc > Period)
        {
            int const RArm = rand();
            Nebula->DynLightningTimer = static_cast<float>(
                (static_cast<double>(RArm) * 3.0517578125e-05f * static_cast<double>(0.7f) - 1.5) *
                Nebula->DynLightningGap);

            double const First = static_cast<double>(rand()) * 3.0517578125e-05f;
            double const Second = static_cast<double>(rand()) * 3.0517578125e-05f;
            double const Third = static_cast<double>(rand()) * 3.0517578125e-05f;
            double const Z = (Third + Third) - 1.0;
            double const Y = (Second + Second) - 1.0;
            double const X = (First + First) - 1.0;
            // No zero-length test.
            float const Inv = static_cast<float>(1.0f / sqrt(X * X + Y * Y + Z * Z));
            // The THIRD rand lands in [0], the first in [2].
            Vector Dir;
            Dir.x = static_cast<float>(Z * Inv);
            Dir.y = static_cast<float>(Y * Inv);
            Dir.z = static_cast<float>(static_cast<float>(X) * static_cast<double>(Inv));
            Light->SetDirection(&Dir);
            // Lit (+0xb4 bit 3) -> GameLight vt+8 (disable).
            if ((Light->Flags & kLightFlagEnabled) != 0)
                GameLightVirtualDisable(GLight);
            return;
        }

        float Intensity = Nebula->DynAmbientIntensity;
        if (Nebula->bBackLightningFlash != 0)
            Intensity = Intensity + Nebula->DynIntensityIncrease;

        // Flick stays unrounded (x87).
        float const U0 = static_cast<float>(static_cast<double>(rand()) * 3.0517578125e-05f);
        int const RFlick = rand();
        double const Flick =
            static_cast<double>(RFlick) * U0 * Intensity * 3.0517578125e-05f;
        // Red and green are stored as float, blue stays on the FPU.
        float Cr = static_cast<float>(Flick * Nebula->DynLightningColor[0]);
        float Cg = static_cast<float>(Flick * Nebula->DynLightningColor[1]);
        double Cb = Flick * Nebula->DynLightningColor[2];
        // Keep only a value below 1.0; anything else (>=, NaN) becomes 1.0.
        if (!(Cr < 1.0f))
            Cr = 1.0f;
        if (!(Cg < 1.0f))
            Cg = 1.0f;
        if (!(Cb < 1.0))
            Cb = 1.0;

        // Red and green * 255.0f are rounded to float before truncation; blue is truncated
        // from the FPU.
        float const Red255 = Cr * 255.0f;
        float const Green255 = Cg * 255.0f;
        int const Blue = ClampByte255(static_cast<int>(Cb * 255.0f));
        int const Green = ClampByte255(static_cast<int>(Green255));
        int const Red = ClampByte255(static_cast<int>(Red255));

        unsigned char Rgba[4];
        Rgba[0] = static_cast<unsigned char>(Red);
        Rgba[1] = static_cast<unsigned char>(Green);
        Rgba[2] = static_cast<unsigned char>(Blue);
        Rgba[3] = 0xff;
        Light->SetColorBytes(Rgba);
        // Not lit -> GameLight vt+4 (enable).
        if ((Light->Flags & kLightFlagEnabled) == 0)
            static_cast<FGameLight*>(Light)->FGameLight::Enable();
    }

    float const kCloudsRandScale = 3.0517578125e-05f; // DAT_005caeec = 1/32768

    // FUN_00550180: only these fields; the rest of the 0x12c block keeps what operator new
    // left there until the [Clouds] parse.
    void LocalNebulaCloudsConstruct(FNebulaClouds* Clouds)
    {
        Clouds->DetailCountScale = 1.0f;
        Clouds->DetailRadiusScale = 1.0f;
        Clouds->Centre[0] = 0.0f;
        Clouds->Centre[1] = 0.0f;
        Clouds->Centre[2] = 0.0f;
        Clouds->PuffTable = nullptr;
        Clouds->PuffCountIni = 0;
        Clouds->PuffCount = 0;
        Clouds->Owner = nullptr;
        Clouds->bLoaded = 0;
        Clouds->DisableBatcher = 0;
    }

    // The detail pair both FUN_00552530 and FUN_005524a0 compute, on the FPU: a = 1.0f - d;
    // +0x08 = float(0.5f * a + d); +0xac = float((1.0f - e) * 0.707f + e), e = 1.0f - a * a
    // (0.707f is DAT_005e0e10).
    void CloudsDetailFactors(FNebulaClouds* Clouds, float Detail)
    {
        double const A = 1.0f - static_cast<double>(Detail);
        Clouds->DetailCountScale = static_cast<float>(0.5f * A + Detail);
        double const Ease = 1.0f - A * A;
        Clouds->DetailRadiusScale = static_cast<float>((1.0f - Ease) * 0.707f + Ease);
    }

    // FUN_00552530. No test of `this`.
    void LocalNebulaCloudsSetDetail(FNebulaClouds* Clouds, float Detail)
    {
        CloudsDetailFactors(Clouds, Detail);
    }

    // FUN_00550cf0: free every non-null puff of the table (+0x00, count +0x0c re-read each
    // step) and null its slot, then free the table. No test of the table pointer; +0x04 /
    // +0xec are left alone.
    void LocalNebulaCloudsFreePuffTable(FNebulaClouds* Clouds)
    {
        for (int I = 0; I < Clouds->PuffCount; ++I)
        {
            if (Clouds->PuffTable[I] != nullptr)
                operator delete(Clouds->PuffTable[I]);
            Clouds->PuffTable[I] = nullptr;
        }
        operator delete(Clouds->PuffTable);
        Clouds->PuffTable = nullptr;
    }

    // FUN_005501e0
    void LocalNebulaCloudsReleaseBits(FNebulaClouds* Clouds)
    {
        if (Clouds->bLoaded == 0)
            return;
        LocalNebulaCloudsFreePuffTable(Clouds);
        Clouds->PuffCountIni = 0;
        Clouds->bLoaded = 0;
    }

    // FUN_00550040 (thiscall on the puff).
    void LocalNebulaCloudsFillPuff(FNebulaCloudPuff* Puff, Vector const& Sample, float Scale,
                                   Vector const& MinCorner, Vector const& MaxCorner,
                                   float const* Weights, int WeightCount)
    {
        Puff->Position.x = Sample.x;
        Puff->Position.y = Sample.y;
        Puff->Position.z = Sample.z;

        // Constants: double 0x005e0bd8, double(0.8f) 0x005e0bd0.
        int const RSize = rand();
        Puff->Size = static_cast<float>(
            (static_cast<double>(RSize) * kCloudsRandScale * 0.40000003576278687 +
             0.800000011920929) *
            Scale);

        // x is rounded once after the add; y and z add Min to the rounded product.
        float const Dx = MaxCorner.x - MinCorner.x;
        float const Dy = MaxCorner.y - MinCorner.y;
        float const Dz = MaxCorner.z - MinCorner.z;
        int const RTint = rand();
        double const T = static_cast<double>(RTint) * kCloudsRandScale;
        float const Py = static_cast<float>(Dy * T);
        float const Pz = static_cast<float>(Dz * T);
        Puff->Color[0] = static_cast<float>(Dx * T + MinCorner.x);
        Puff->Color[1] = Py + MinCorner.y;
        Puff->Color[2] = Pz + MinCorner.z;

        int const RIndex = rand();
        Puff->ShapeIndex = (RIndex * WeightCount) / 32768;

        // The weights accumulate on the FPU; the first running sum strictly above the pick
        // replaces +0x1c.
        int const RPick = rand();
        float const Pick = static_cast<float>(static_cast<double>(RPick) * kCloudsRandScale);
        double Running = 0.0f;
        for (int I = 0; I < WeightCount; ++I)
        {
            Running += Weights[I];
            if (Pick < Running)
            {
                Puff->ShapeIndex = I;
                break;
            }
        }

        Puff->ViewDepth = 0.0f;
        Puff->LightningTimer = -1.0f;
    }

    // FUN_00550b60: rebuild the puff table while +0xec is set.
    void LocalNebulaCloudsRebuildBits(FNebulaClouds* Clouds)
    {
        if (Clouds->bLoaded == 0)
            return;

        // The table is the one pointer tested here.
        if (Clouds->PuffTable != nullptr)
            LocalNebulaCloudsFreePuffTable(Clouds);

        // The new table is not tested for null.
        int const Count = static_cast<int>(
            static_cast<double>(Clouds->PuffCountIni) * Clouds->DetailCountScale +
            0.5f);
        Clouds->PuffCount = Count;
        FNebulaCloudPuff** const Table =
            static_cast<FNebulaCloudPuff**>(operator new(static_cast<unsigned>(Count) << 2));
        Clouds->PuffTable = Table;

        for (int I = 0; I < Clouds->PuffCount; ++I)
        {
            FNebulaCloudPuff* Puff = static_cast<FNebulaCloudPuff*>(operator new(sizeof(FNebulaCloudPuff)));
            if (Puff != nullptr)
            {
                Puff->bActive = 0;
                Puff->bHidden = 0;
            }
            Table[I] = Puff;

            // Box +0x78 / +0x84, weights +0x48, count +0x68.
            Vector const Sample = random_inside_sphere(Clouds->CloudRadius);
            float const Scale = Clouds->PuffRadius * Clouds->DetailRadiusScale;
            LocalNebulaCloudsFillPuff(Puff, Sample, Scale,
                                      Clouds->PuffColorA,
                                      Clouds->PuffColorB,
                                      Clouds->PuffWeights, Clouds->PuffShapeCount);

            Vector const Drift = random_inside_sphere(Clouds->PuffDrift);
            Puff->Velocity[0] = Drift.x;
            Puff->Velocity[1] = Drift.y;
            Puff->Velocity[2] = Drift.z;

            // A lightning duration <= 0 or unordered gives timer -1.0f. Constants 0x005e0df8 /
            // 0x005db0b8.
            if (!(Clouds->LightningDuration > 0.0f))
            {
                Puff->LightningTimer = -1.0f;
            }
            else
            {
                int const RPhase = rand();
                Puff->LightningTimer = static_cast<float>(
                    (-0.800000011920929 -
                     static_cast<double>(RPhase) * kCloudsRandScale * 0.699999988079071) *
                    Clouds->LightningGap);
            }
            Puff->bActive = 1;
        }
    }

    // FUN_005524a0: frees the table without the +0xec test, then the FUN_00552530 factors and
    // FUN_00550b60 (which tests +0xec itself). No test of `this`.
    void LocalNebulaCloudsApplyDetail(FNebulaClouds* Clouds, float Detail)
    {
        LocalNebulaCloudsFreePuffTable(Clouds);
        CloudsDetailFactors(Clouds, Detail);
        LocalNebulaCloudsRebuildBits(Clouds);
    }

    // DAT_006140b0 (.data, initialised 1, read only by FUN_00550d40): puff sort direction.
    // Non-zero sorts the table by descending +0x54, zero by ascending.
    int G_CloudsSortDescending = 1;

    // G_CloudsMath (DAT_00679860), see cli_nebula.h.
    I3DMathEngine* AcquireCloudsMath()
    {
        if (G_CloudsMath == nullptr)
            G_CloudsMath = Acquire3DMathEngine();
        return G_CloudsMath;
    }

    // .rdata constants read by FUN_00550d40.
    float const kCloudsCullSlack = 1.0199999809265137f;    // DAT_005e0e04
    float const kCloudsExclusionDepth = 0.8999999761581421f; // DAT_005e0e00
    double const kCloudsDriftSpan = 0.699999988079071;     // DAT_005db0b8
    double const kCloudsDriftBase = -0.800000011920929;    // DAT_005e0df8
    double const kCloudsTwoPi = 6.2831854820251465;        // DAT_005c97f8
    float const kCloudsDriftSpeed = 5.0f;                  // DAT_005d8464

    // FUN_00550d40. Callers: the nebula update (visibility = nebula +0x294, manager = nebula
    // +0x34) and the TexturePanels update (visibility = panels +0x70, manager = null).
    // Each puff drifts by its velocity; its lightning timer runs while lightning_duration is
    // positive (FNebulaCloudPuff).
    void LocalNebulaCloudsUpdate(FNebulaClouds* Clouds, ICamera* Camera, float Dt, float Visibility,
                                 Exclusion::NebulaExclusionZoneMgr* ExclusionMgr)
    {
        if (Clouds->bLoaded == 0)
            return;

        int const PuffCount = Clouds->PuffCount;
        int Budget = static_cast<int>(static_cast<double>(Visibility) /
                                      static_cast<double>(Clouds->BudgetVisibility) *
                                      static_cast<double>(PuffCount));
        Clouds->PuffBudget = Budget;
        if (Budget < 0)
            Clouds->PuffBudget = 0;
        else if (Budget > PuffCount)
            Clouds->PuffBudget = PuffCount;

        // Fade: an ordered visibility <= 0 gives 0.0; everything else below the full value
        // (including unordered) divides.
        float const FullVisibility = Clouds->FullVisibility;
        if (Visibility > FullVisibility)
            Clouds->Fade = 1.0f;
        else if (Visibility <= 0.0f)
            Clouds->Fade = 0.0f;
        else
            Clouds->Fade = Visibility / FullVisibility;

        ICamera* const Cam = Camera;
        float const Radius = Clouds->CloudRadius;
        float const Ahead = Clouds->NearFadeReach + Radius;
        Clouds->CentreAhead = Ahead;
        float CullDistSq = static_cast<float>(static_cast<double>(Radius) * Radius *
                                              static_cast<double>(kCloudsCullSlack));
        Vector AheadLocal;
        AheadLocal.x = 0.0f;
        AheadLocal.y = 0.0f;
        AheadLocal.z = -Ahead;

        // Cloud centre +0xf4 = the camera transform applied to that vector.
        Transform const* const CamXf = reinterpret_cast<Transform const*>(Cam->GetOrientation());
        Vector Centre;
        AcquireCloudsMath()->TransformXform(Centre, *CamXf, AheadLocal);
        Clouds->Centre[0] = Centre.x;
        Clouds->Centre[1] = Centre.y;
        Clouds->Centre[2] = Centre.z;

        FNebulaCloudPuff** const Table = Clouds->PuffTable;

        // Pass 1: cull.
        int Active = 0;
        for (int I = 0; I < Clouds->PuffCount; ++I)
        {
            FNebulaCloudPuff* const Puff = Table[I];
            // An ordered fade <= 0 deactivates every puff.
            if (Clouds->Fade <= 0.0f)
            {
                Puff->bActive = 0;
                Puff->bHidden = 1;
            }
            if (Puff->bActive == 0)
                continue;
            // <= and unordered distances stay active.
            double const Dx = static_cast<double>(Clouds->Centre[0]) - Puff->Position.x;
            double const Dy = static_cast<double>(Clouds->Centre[1]) - Puff->Position.y;
            double const Dz = static_cast<double>(Clouds->Centre[2]) - Puff->Position.z;
            double const DistSq = Dz * Dz + Dy * Dy + Dx * Dx;
            if (DistSq > CullDistSq)
            {
                Puff->bActive = 0;
                Puff->bHidden = 1;
            }
            else
                ++Active;
        }

        // The second pass lowers this to the nearest lightning anchor distance; nothing reads
        // the result.
        float NearestDistSq = static_cast<float>(static_cast<double>(Clouds->LLightRange) *
                                                 Clouds->LLightRange);

        // Bubble sort by +0x54 until a pass makes no swap; unordered pairs never swap.
        for (;;)
        {
            if (Clouds->PuffCount <= 1)
                break;
            int Swaps = 0;
            for (int J = 1; J < Clouds->PuffCount; ++J)
            {
                FNebulaCloudPuff* const Left = Table[J - 1];
                FNebulaCloudPuff* const Right = Table[J];
                float const L = Left->ViewDepth;
                float const R = Right->ViewDepth;
                bool const bSwap = G_CloudsSortDescending != 0 ? (L < R) : (L > R);
                if (bSwap)
                {
                    Table[J - 1] = Right;
                    ++Swaps;
                    Table[J] = Left;
                }
            }
            if (Swaps <= 0)
                break;
        }

        // Pass 2: respawn, lightning, move.
        for (int I = 0; I < Clouds->PuffCount; ++I)
        {
            // An inactive puff respawns while fewer than the budget are active.
            if (Table[I]->bActive == 0 && Active < Clouds->PuffBudget)
            {
                // On the sphere of radius R around the centre.
                float const SpawnRadius = Clouds->CloudRadius;
                Vector const OnSphere = random_on_unit_sphere();
                float const Sz = static_cast<float>(static_cast<double>(SpawnRadius) * OnSphere.z);
                float* const Pos = &Table[I]->Position.x;
                Pos[0] = static_cast<float>(static_cast<double>(SpawnRadius) * OnSphere.x +
                                            Clouds->Centre[0]);
                Pos[1] = static_cast<float>(static_cast<double>(SpawnRadius) * OnSphere.y +
                                            Clouds->Centre[1]);
                Pos[2] = static_cast<float>(static_cast<double>(Sz) + Clouds->Centre[2]);

                Vector const Vel = random_inside_sphere(Clouds->PuffDrift);
                Table[I]->Velocity[0] = Vel.x;
                Table[I]->Velocity[1] = Vel.y;
                Table[I]->Velocity[2] = Vel.z;

                // Visibility not below +0x10c accepts outright; otherwise the owner zone
                // (+0xc4, null rejects) must report a density above 0 at the new position.
                bool bAccept = true;
                if (!(Visibility >= Clouds->AcceptVisibility))
                {
                    Universe::IZoneImmersion const* const Owner = Clouds->Owner;
                    if (Owner == nullptr)
                        bAccept = false;
                    else
                    {
                        float const Density =
                            static_cast<float>(Owner->GetImmersion(*reinterpret_cast<Vector const*>(Pos)));
                        if (!(static_cast<double>(Density) > 0.0))
                            bAccept = false;
                    }
                }

                if (bAccept)
                {
                    // Inside an exclusion zone deeper than 0.9 it stays hidden.
                    ++Active;
                    Table[I]->bActive = 1;
                    Table[I]->bHidden = 0;
                    if (ExclusionMgr != nullptr)
                    {
                        Exclusion::NebulaExclusionZone* HitZone;
                        float Depth = -1.0f;
                        if (ExclusionMgr->is_in_exclusion_zone(
                                Table[I]->Position, &HitZone, &Depth,
                                true) &&
                            Depth > kCloudsExclusionDepth)
                            Table[I]->bHidden = 1;
                    }
                }
            }

            // Only active, visible puffs flash and move.
            FNebulaCloudPuff* const Puff = Table[I];
            if (Puff->bActive == 0 || Puff->bHidden != 0)
                continue;

            float* const Timer = &Puff->LightningTimer;
            float const Period = Clouds->LightningDuration;
            if (Period > 0.0f)
            {
                // The branch is taken on the OLD timer value; < 0 and unordered go to the
                // zero-crossing test.
                float const OldTimer = *Timer;
                *Timer = static_cast<float>(static_cast<double>(Dt) + OldTimer);
                if (OldTimer >= 0.0f)
                {
                    // Lightning anchor +0x20.
                    if (Clouds->LLightRange > 0.0f)
                    {
                        const float* const CamPos = (const float*)Cam->GetPosition();
                        double const Ax = static_cast<double>(Puff->LightningAnchor[0]) - CamPos[0];
                        double const Ay = static_cast<double>(Puff->LightningAnchor[1]) - CamPos[1];
                        double const Az = static_cast<double>(Puff->LightningAnchor[2]) - CamPos[2];
                        double const AnchorDistSq = Az * Az + Ay * Ay + Ax * Ax;
                        if (AnchorDistSq < NearestDistSq)
                            NearestDistSq = static_cast<float>(AnchorDistSq);
                    }
                    // Past the period the timer restarts negative.
                    if (*Timer > Period)
                    {
                        int const R = rand();
                        *Timer = static_cast<float>(
                            (kCloudsDriftBase -
                             static_cast<double>(R) * kCloudsRandScale * kCloudsDriftSpan) *
                            Clouds->LightningGap);
                    }
                }
                else if (*Timer >= 0.0f)
                {
                    // The timer just crossed zero: anchor = position; the lightning direction is
                    // a random in-plane direction of LookMatrix(anchor - camera), length 5.0.
                    Puff->LightningAnchor[0] = Puff->Position.x;
                    Puff->LightningAnchor[1] = Puff->Position.y;
                    Puff->LightningAnchor[2] = Puff->Position.z;
                    const Vector* const CamPos = Cam->GetPosition();
                    Vector ToAnchor;
                    ToAnchor.x = Puff->LightningAnchor[0] - CamPos->x;
                    ToAnchor.y = Puff->LightningAnchor[1] - CamPos->y;
                    ToAnchor.z = Puff->LightningAnchor[2] - CamPos->z;
                    Matrix const Look = LookMatrix(ToAnchor);

                    int const RAngle = rand();
                    double const Angle = static_cast<double>(RAngle) * kCloudsRandScale * kCloudsTwoPi;
                    Vector InPlane;
                    InPlane.x = static_cast<float>(cos(Angle));
                    InPlane.y = static_cast<float>(sin(Angle));
                    InPlane.z = 0.0f;
                    Vector Dir;
                    AcquireCloudsMath()->TransformMatrix(
                        Dir, Look, InPlane);
                    float* const DriftDir = Puff->LightningDir;
                    DriftDir[0] = Dir.x;
                    DriftDir[1] = Dir.y;
                    DriftDir[2] = Dir.z;
                    double const Len = sqrt(static_cast<double>(DriftDir[0]) * DriftDir[0] +
                                            static_cast<double>(DriftDir[1]) * DriftDir[1] +
                                            static_cast<double>(DriftDir[2]) * DriftDir[2]);
                    double const Scale = static_cast<double>(kCloudsDriftSpeed) / Len;
                    DriftDir[0] = static_cast<float>(Scale * DriftDir[0]);
                    DriftDir[1] = static_cast<float>(Scale * DriftDir[1]);
                    DriftDir[2] = static_cast<float>(Scale * DriftDir[2]);

                    int const RPhase = rand();
                    Puff->LightningPhase =
                        static_cast<float>(static_cast<double>(RPhase) * kCloudsRandScale);
                }

                // < 0 and unordered skip.
                if (*Timer >= 0.0f)
                {
                    int const RA = rand();
                    float const A = static_cast<float>(static_cast<double>(RA) * kCloudsRandScale);
                    int const RB = rand();
                    Puff->LightningFlash = static_cast<float>(
                        static_cast<double>(RB) * A * kCloudsRandScale);
                }
            }

            float* const Pos = &Puff->Position.x;
            float const StepZ = static_cast<float>(static_cast<double>(Dt) * Puff->Velocity[2]);
            Pos[0] = static_cast<float>(static_cast<double>(Dt) * Puff->Velocity[0] + Pos[0]);
            Pos[1] = static_cast<float>(static_cast<double>(Dt) * Puff->Velocity[1] + Pos[1]);
            Pos[2] = static_cast<float>(static_cast<double>(StepZ) + Pos[2]);
        }

        Clouds->ActiveCount = Active;
    }

    // FUN_005513b0 (arg the nebula's exclusion manager): re-seed every puff of the table
    // around the clouds centre (+0xf4..+0xfc). No test of `this`, the table or a puff.
    void LocalNebulaCloudsRegenExcluded(FNebulaClouds* Clouds, void* ExclusionCtx)
    {
        Exclusion::NebulaExclusionZoneMgr* const ExclMgr =
            static_cast<Exclusion::NebulaExclusionZoneMgr*>(ExclusionCtx);
        for (int I = 0; I < Clouds->PuffCount; ++I)
        {
            FNebulaCloudPuff* const Puff = Clouds->PuffTable[I];
            // Past the budget (+0x10) -> hidden.
            if (I >= Clouds->PuffBudget)
            {
                Puff->bActive = 0;
                Puff->bHidden = 1;
                continue;
            }

            Vector const Sample = random_inside_sphere(Clouds->CloudRadius);
            Puff->Position.x = Clouds->Centre[0] + Sample.x;
            Puff->Position.y = Clouds->Centre[1] + Sample.y;
            Puff->Position.z = Clouds->Centre[2] + Sample.z;
            Vector const Drift = random_inside_sphere(Clouds->PuffDrift);
            Puff->Velocity[0] = Drift.x;
            Puff->Velocity[1] = Drift.y;
            Puff->Velocity[2] = Drift.z;

            // A null zone (+0xc4) counts as outside; only an immersion strictly above 0 is in.
            Universe::IZoneImmersion const* const Zone = Clouds->Owner;
            if (Zone == nullptr ||
                !(Zone->GetImmersion(Puff->Position) > 0.0))
            {
                Puff->bActive = 0;
                Puff->bHidden = 1;
                continue;
            }

            // An exclusion hit deeper than 0.9f (0x005e0e00) hides it again.
            Puff->bActive = 1;
            Puff->bHidden = 0;
            if (ExclMgr != nullptr)
            {
                Exclusion::NebulaExclusionZone* HitZone = nullptr;
                float HitDist = -1.0f;
                if (ExclMgr->is_in_exclusion_zone(Puff->Position, &HitZone,
                                                  &HitDist, true) &&
                    HitDist > 0.9f)
                {
                    Puff->bHidden = 1;
                }
            }
        }
    }

    // FUN_00506f00. No test of `this`.
    void LocalNebulaCloudsDestroy(FNebulaClouds* Clouds, unsigned char Flags)
    {
        LocalNebulaCloudsReleaseBits(Clouds);
        if ((Flags & 1u) != 0)
            operator delete(Clouds);
    }

    // FUN_00550240 -- the [Clouds] section. Shapes is the owner's ShapeDB (nebula or
    // TexturePanels +0x04). No test of `this`.
    void LocalNebulaCloudsParseIni(FNebulaClouds* Clouds, INI_Reader& Reader, FShapeDB* Shapes)
    {
        if (Clouds->bLoaded != 0)
        {
            LocalNebulaCloudsFreePuffTable(Clouds);
            Clouds->PuffCountIni = 0;
            Clouds->bLoaded = 0;
        }

        // Defaults; +0x11c, +0x128, +0x90..+0x98 and +0x18..+0x44 are left as they are.
        Clouds->ZWriteClouds = 0;
        Clouds->NoCloudBlend = 0;
        Clouds->DisableLightningFlash = 0;
        Clouds->DisableLightningBolts = 0;
        Clouds->DisableClouds = 0;
        Clouds->Up.z = 0.0f;
        Clouds->Up.x = 1.0f;
        Clouds->Up.y = 1.0f;
        {
            // Normalised on the FPU, no zero test.
            float* const N = &Clouds->Up.x;
            double const Inv = 1.0f / sqrt((static_cast<double>(N[0]) * N[0] +
                                            static_cast<double>(N[1]) * N[1]) +
                                           static_cast<double>(N[2]) * N[2]);
            N[0] = static_cast<float>(Inv * N[0]);
            N[1] = static_cast<float>(Inv * N[1]);
            N[2] = static_cast<float>(Inv * N[2]);
        }
        Clouds->LightningSize = 1.0f;
        Clouds->LightningIntensity = 1.0f;
        Clouds->PuffRadius = 50.0f;
        Clouds->PuffMaxAlpha = 0.75f;
        Clouds->LightningDuration = 0.0f;
        Clouds->PuffDrift = 0.0f;
        Clouds->MaxDistance = 1500.0f;
        Clouds->LLightRange = 0.0f;
        Clouds->PuffShapeCount = 0;
        for (int I = 0; I < 8; ++I)
            Clouds->PuffWeights[I] = 1.0f;
        Clouds->NearFadeStart = 50.0f;
        Clouds->NearFadeEnd = 100.0f;
        Clouds->FullVisibility = BitsToFloat(0x3dcccccd);
        Clouds->BudgetVisibility = BitsToFloat(0x3e99999a);
        Clouds->AcceptVisibility = BitsToFloat(0x3e99999a);
        Clouds->Unknown_0xf0 = 0;

        // How many puff_colora / puff_colorb keys were taken (at most two).
        int ColorCount = 0;
        while (Reader.read_value())
        {
            if (Reader.is_value("puff_count"))
            {
                Clouds->PuffCountIni = static_cast<int>(Reader.get_value_float(0));
            }
            else if (Reader.is_value("puff_radius"))
            {
                Clouds->PuffRadius = Reader.get_value_float(0);
            }
            else if (Reader.is_value("puff_colora") || Reader.is_value("puff_colorb"))
            {
                // Either name fills the next of the two colour slots (+0x78, +0x84) in the
                // order the keys come; the first one also seeds the second slot. A third is
                // ignored.
                if (ColorCount < 2)
                {
                    float Color[3];
                    ReadColor255(Reader, Color);
                    float* const Dest = ColorCount == 0 ? &Clouds->PuffColorA.x : &Clouds->PuffColorB.x;
                    Dest[0] = Color[0];
                    Dest[1] = Color[1];
                    Dest[2] = Color[2];
                    if (ColorCount == 0)
                    {
                        float* const Second = &Clouds->PuffColorB.x;
                        Second[0] = Dest[0];
                        Second[1] = Dest[1];
                        Second[2] = Dest[2];
                    }
                    ++ColorCount;
                }
            }
            else if (Reader.is_value("puff_shape"))
            {
                // At most eight (+0x68). A miss is the ShapeDB warning and then this file's own
                // error; the null shape is stored anyway.
                if (Clouds->PuffShapeCount < 8)
                {
                    char const* const Name = Reader.get_value_string();
                    FTexShape* const Shape = Shapes->Find(Name);
                    if (Shape == nullptr)
                    {
                        char const* const File = Reader.get_file_name();
                        FDUMP(kFdumpError,
                              "%s(%d) : *** ERROR: Texture Shape (%s) not found for [Clouds] in "
                              "file (%s)",
                              "E:\\FL\\Scratch\\Source\\Client\\Space\\SpaceTerrainRenderComp.cpp",
                              0x523, Reader.get_value_string(), File);
                    }
                    Clouds->PuffShapes[Clouds->PuffShapeCount] = Shape;
                    ++Clouds->PuffShapeCount;
                }
            }
            else if (Reader.is_value("puff_weights"))
            {
                for (int I = 0; I < Clouds->PuffShapeCount; ++I)
                    Clouds->PuffWeights[I] = Reader.get_value_float(I);
            }
            else if (Reader.is_value("puff_opacity"))
            {
                // Recognised and ignored.
            }
            else if (Reader.is_value("puff_max_alpha"))
            {
                Clouds->PuffMaxAlpha = Reader.get_value_float(0);
            }
            else if (Reader.is_value("puff_drift"))
            {
                Clouds->PuffDrift = Reader.get_value_float(0);
            }
            else if (Reader.is_value("lightning_shape"))
            {
                // Four shapes (+0x18..+0x24); each value is copied into a 32-byte buffer
                // (FUN_00417c50, at most 31 characters) before the find; a miss stores null.
                for (int I = 0; I < 4; ++I)
                {
                    char Buf[0x20];
                    Buf[0] = '\0';
                    char const* const Src = Reader.get_value_string(static_cast<unsigned>(I));
                    if (Src != nullptr)
                    {
                        size_t Len = strlen(Src);
                        if (Len >= sizeof(Buf))
                            Len = sizeof(Buf) - 1;
                        memcpy(Buf, Src, Len);
                        Buf[Len] = '\0';
                    }
                    Clouds->LightningShapes[I] = Shapes->Find(Buf);
                }
            }
            else if (Reader.is_value("lightning_color"))
            {
                ReadColor255(Reader, Clouds->LightningColor);
            }
            else if (Reader.is_value("lightning_gap"))
            {
                Clouds->LightningGap = Reader.get_value_float(0);
            }
            else if (Reader.is_value("lightning_duration"))
            {
                Clouds->LightningDuration = Reader.get_value_float(0);
            }
            else if (Reader.is_value("lightning_size"))
            {
                Clouds->LightningSize = Reader.get_value_float(0);
            }
            else if (Reader.is_value("llight_range"))
            {
                Clouds->LLightRange = Reader.get_value_float(0);
            }
            else if (Reader.is_value("llight_color"))
            {
                ReadColor255(Reader, Clouds->LLightColor);
            }
            else if (Reader.is_value("lightning_intensity"))
            {
                Clouds->LightningIntensity = Reader.get_value_float(0);
            }
            else if (Reader.is_value("max_distance"))
            {
                Clouds->MaxDistance = Reader.get_value_float(0);
            }
            else if (Reader.is_value("near_fade_distance"))
            {
                // A second value strictly below the first warns and swaps the two.
                Clouds->NearFadeStart = Reader.get_value_float(0);
                Clouds->NearFadeEnd = Reader.get_value_float(1);
                if (Clouds->NearFadeEnd < Clouds->NearFadeStart)
                {
                    FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s",
                          "E:\\FL\\Scratch\\Source\\Client\\Space\\SpaceTerrainRenderComp.cpp",
                          0x555, "near_fade_distances are flipped");
                    float const Far = Clouds->NearFadeEnd;
                    Clouds->NearFadeEnd = Clouds->NearFadeStart;
                    Clouds->NearFadeStart = Far;
                }
            }
            else if (Reader.is_value("no_cloud_blend"))
            {
                Clouds->NoCloudBlend = static_cast<int>(Reader.get_value_float(0));
            }
            else if (Reader.is_value("zwrite_clouds"))
            {
                Clouds->ZWriteClouds = static_cast<int>(Reader.get_value_float(0));
            }
            else if (Reader.is_value("disable_clouds"))
            {
                Clouds->DisableClouds = static_cast<int>(Reader.get_value_float(0));
            }
            else if (Reader.is_value("disable_wisps"))
            {
                Clouds->DisableWisps = static_cast<int>(Reader.get_value_float(0));
            }
            else if (Reader.is_value("disable_lightning_flash"))
            {
                Clouds->DisableLightningFlash = static_cast<int>(Reader.get_value_float(0));
            }
            else if (Reader.is_value("disable_lightning_bolts"))
            {
                Clouds->DisableLightningBolts = static_cast<int>(Reader.get_value_float(0));
            }
            else if (Reader.is_value("disable_batcher"))
            {
                Clouds->DisableBatcher = static_cast<int>(Reader.get_value_float(0));
            }
        }

        // Weights normalised; the sum stays on the FPU, no zero test.
        Clouds->bLoaded = 1;
        {
            float* const Weights = Clouds->PuffWeights;
            int const WeightCount = Clouds->PuffShapeCount;
            double Sum = 0.0f;
            for (int I = 0; I < WeightCount; ++I)
                Sum += Weights[I];
            if (WeightCount > 0)
            {
                double const InvSum = 1.0f / Sum;
                for (int I = 0; I < Clouds->PuffShapeCount; ++I)
                    Weights[I] = static_cast<float>(InvSum * Weights[I]);
            }
        }

        Clouds->PuffCount = Clouds->PuffCountIni;
        Clouds->PuffBudget = Clouds->PuffCountIni;
        Clouds->CloudRadius =
            static_cast<float>(static_cast<double>(Clouds->MaxDistance) * 0.5f + Clouds->PuffRadius);
        LocalNebulaCloudsRebuildBits(Clouds);

        // +0xbc, the near-fade reach; only a +0xe4 strictly above 0 computes it.
        float const NearFade = Clouds->NearFadeStart;
        if (!(NearFade > 0.0f))
        {
            Clouds->NearFadeReach = 0.0f;
            return;
        }
        // Tangent constant: double 0x005e0be0. A is rounded to float, BReach stays on the FPU.
        double const Tan = tan(0.6108652551968893);
        double const Reach = Clouds->CloudRadius;
        float const A = static_cast<float>(Reach * 0.8f / Tan - NearFade);
        double const BReach =
            (Tan * NearFade + static_cast<double>(Clouds->PuffRadius) * 1.2f) * 1.25f;
        double X;
        if (BReach > Reach)
            X = A;
        else
            X = sqrt(Reach * Reach - BReach * BReach);
        if (X > A)
            X = A;
        // Only an ordered "less" becomes 0.
        double const Result = (X + NearFade) - Reach;
        Clouds->NearFadeReach = static_cast<float>(Result);
        if (Result < 0.0)
            Clouds->NearFadeReach = 0.0f;
    }

    // FUN_00504e60's clouds-list insert (node FUN_0042a7e0, value FUN_005999b0): push_back
    // before the sentinel (+0xf8). No tests.
    void CloudsListPush(FNebulaEffect* Nebula, FNebulaClouds* Clouds)
    {
        FNebulaCloudsListNode* const Sentinel = Nebula->Clouds.Sentinel;
        FNebulaCloudsListNode* const Node =
            static_cast<FNebulaCloudsListNode*>(operator new(sizeof(FNebulaCloudsListNode)));
        Node->Next = Sentinel;
        Node->Prev = Sentinel->Prev;
        Sentinel->Prev = Node;
        Node->Prev->Next = Node;
        Node->Clouds = Clouds;
        ++Nebula->Clouds.Count;
    }

    // FUN_00504e60's [Exterior] section.
    void ParseExteriorSection(FNebulaEffect* Nebula, INI_Reader& Reader)
    {
        while (Reader.read_value())
        {
            if (Reader.is_value("bit_radius"))
            {
                Nebula->BitRadius = Reader.get_value_float(0);
            }
            else if (Reader.is_value("color"))
            {
                ReadColor255(Reader, Nebula->ExteriorColor);
            }
            else if (Reader.is_value("shape"))
            {
                // At most eight shapes (+0x60[+0xa0]); a miss warns and stores null, the count
                // goes up either way.
                if (Nebula->ExteriorShapeCount < 8)
                {
                    char const* const Name = Reader.get_value_string();
                    FTexShape* const Shape = NebulaShapes(Nebula)->Find(Name);
                    Nebula->ExteriorShapes[Nebula->ExteriorShapeCount] =
                        Shape;
                    ++Nebula->ExteriorShapeCount;
                }
            }
            else if (Reader.is_value("shape_weights"))
            {
                // One weight per shape read so far.
                for (int I = 0; I < Nebula->ExteriorShapeCount; ++I)
                    Nebula->ExteriorShapeWeights[I] = Reader.get_value_float(I);
            }
            else if (Reader.is_value("fill_shape"))
            {
                char const* const Name = Reader.get_value_string();
                Nebula->FillShape = NebulaShapes(Nebula)->Find(Name);
            }
            else if (Reader.is_value("plane_slices"))
            {
                Nebula->PlaneSlices = static_cast<int>(Reader.get_value_float(0));
            }
            else if (Reader.is_value("min_bits"))
            {
                Nebula->MinBits = static_cast<int>(Reader.get_value_float(0));
            }
            else if (Reader.is_value("max_bits"))
            {
                Nebula->MaxBits = static_cast<int>(Reader.get_value_float(0));
            }
            else if (Reader.is_value("bit_radius_random_variation"))
            {
                // Compared against double 0.9999 (0x005dafa8), clamped to float 0.9999f
                // (0x005dafa4); a NaN passes both clamps.
                float V = Reader.get_value_float(0);
                if (static_cast<double>(V) > 0.9999)
                    V = 0.9999f;
                if (static_cast<double>(V) < 0.0)
                    V = 0.0f;
                Nebula->BitRadiusMin = 1.0f - V;
                Nebula->BitRadiusMax = V + 1.0f;
            }
            else if (Reader.is_value("equator_bias"))
            {
                Nebula->EquatorBias =
                    static_cast<float>(1.0 - static_cast<double>(Reader.get_value_float(0)));
            }
            else if (Reader.is_value("move_bit_percent"))
            {
                Nebula->MoveBitPercent = Reader.get_value_float(0);
            }
            else if (Reader.is_value("opacity"))
            {
                float const V = Reader.get_value_float(0);
                Nebula->Opacity = V;
                Nebula->BitAlpha = V;
            }
        }
    }

    // FUN_00504e60's [Fog] section.
    void ParseFogSection(FNebulaEffect* Nebula, INI_Reader& Reader)
    {
        while (Reader.read_value())
        {
            if (Reader.is_value("distance"))
            {
                Nebula->FogDistance = Reader.get_value_float(0);
            }
            else if (Reader.is_value("near"))
            {
                Nebula->FogNear = Reader.get_value_float(0);
            }
            else if (Reader.is_value("color"))
            {
                // Only a value below 1.0 is kept; anything else (>=, NaN) becomes 1.0f.
                float* const Color = Nebula->FogColor;
                ReadColor255(Reader, Color);
                for (int I = 0; I < 3; ++I)
                    Color[I] = (Color[I] < 1.0f) ? Color[I] : 1.0f;
            }
        }
    }

    // FUN_00504e60's [BackgroundLightning] section.
    void ParseBackgroundLightningSection(FNebulaEffect* Nebula, INI_Reader& Reader)
    {
        while (Reader.read_value())
        {
            if (Reader.is_value("gap"))
            {
                float const Gap = Reader.get_value_float(0);
                Nebula->BackLightningGap = Gap;
                Nebula->BackLightningTimer = -Gap;
            }
            else if (Reader.is_value("duration"))
            {
                Nebula->BackLightningDuration = Reader.get_value_float(0);
            }
            else if (Reader.is_value("color"))
            {
                ReadColor255(Reader, Nebula->BackLightningColor);
            }
            else if (Reader.is_value("intensity"))
            {
                Nebula->BackLightningIntensity = Reader.get_value_float(0);
            }
        }
    }

    // FUN_00504e60's [DynamicLightning] section.
    void ParseDynamicLightningSection(FNebulaEffect* Nebula, INI_Reader& Reader)
    {
        while (Reader.read_value())
        {
            if (Reader.is_value("gap"))
                Nebula->DynLightningGap = Reader.get_value_float(0);
            else if (Reader.is_value("duration"))
                Nebula->DynLightningDuration = Reader.get_value_float(0);
            else if (Reader.is_value("color"))
                ReadColor255(Reader, Nebula->DynLightningColor);
            else if (Reader.is_value("ambient_intensity"))
                Nebula->DynAmbientIntensity = Reader.get_value_float(0);
            else if (Reader.is_value("intensity_increase"))
                Nebula->DynIntensityIncrease = Reader.get_value_float(0);
        }
    }

    // FUN_00504e60's [NebulaLight] section.
    void ParseNebulaLightSection(FNebulaEffect* Nebula, INI_Reader& Reader)
    {
        while (Reader.read_value())
        {
            if (Reader.is_value("ambient"))
            {
                Nebula->bHasAmbient = 1;
                Nebula->Ambient[0] = static_cast<unsigned char>(static_cast<int>(Reader.get_value_float(0)));
                Nebula->Ambient[1] = static_cast<unsigned char>(static_cast<int>(Reader.get_value_float(1)));
                Nebula->Ambient[2] = static_cast<unsigned char>(static_cast<int>(Reader.get_value_float(2)));
            }
            else if (Reader.is_value("sun_burnthrough_intensity"))
            {
                Nebula->SunBurnthroughIntensity = Reader.get_value_float(0);
            }
            else if (Reader.is_value("sun_burnthrough_scaler"))
            {
                Nebula->SunBurnthroughScaler = Reader.get_value_float(0);
            }
        }
    }

    // FUN_00504e60's [Exclusion Zones] section. Two locals carry across the keys: the number of
    // zones this section added gates every key but "exclusion", and the current zone_shell
    // receives shell_scalar / max_alpha / exclusion_tint.
    void ParseExclusionZonesSection(FNebulaEffect* Nebula, INI_Reader& Reader, unsigned SystemId)
    {
        // The manager is created once per nebula.
        if (Nebula->ExclusionMgr == nullptr)
            Nebula->ExclusionMgr = new Exclusion::NebulaExclusionZoneMgr();

        int ZonesAdded = 0;
        FNebulaZoneShell* Shell = nullptr;
        while (Reader.read_value())
        {
            if (Reader.is_value("exclusion"))
            {
                // An unknown zone is an FDUMP and nothing else.
                char const* const Nick = Reader.get_value_string();
                Universe::IZone const* const Z = Universe::get_zone(SystemId, Nick);
                if (Z == nullptr)
                {
                    FDUMP(kFdumpError,
                          "%s(%d) : *** ERROR: AsteroidField linked to missing Exclusion Zone (%s)",
                          "E:\\FL\\Scratch\\Source\\Client\\Space\\Nebula.cpp", 0x2b2, Nick);
                    continue;
                }
                Nebula->ExclusionMgr->push_back(Z, Nebula->Zone);
                ++ZonesAdded;
            }
            else if (Reader.is_value("fog_far"))
            {
                if (ZonesAdded == 0)
                    continue;
                Exclusion::NebulaExclusionZone* const Back = Nebula->ExclusionMgr->back();
                Back->FogFar = Reader.get_value_float(0);
            }
            else if (Reader.is_value("zone_shell"))
            {
                // The current shell is dropped first, whatever follows.
                Shell = nullptr;
                if (ZonesAdded == 0)
                    continue;
                Exclusion::NebulaExclusionZone* const Back = Nebula->ExclusionMgr->back();
                char const* const Name = Reader.get_value_string();
                Back->set_zone_shell_name(Name);
                unsigned const Key = CreateID(Name);

                // A mesh this nebula already loaded is reused.
                FNebulaZoneShellMap& Shells = NebulaZoneShells(Nebula);
                FNebulaZoneShellMap::iterator const Found = NebulaFindZoneShell(Shells, Key);
                if (Found != Shells.end())
                {
                    Shell = Found->second;
                    continue;
                }

                FNebulaZoneShell* const NewShell = new FNebulaZoneShell;
                for (int I = 0; I < 5; ++I)
                    NewShell->UserData[I] = 1.0f;
                NewShell->ShellScalar = 1.0f;
                NewShell->MaxAlpha = 1.0f;

                // FUN_00420dd0; on failure there is no current shell.
                if (!NewShell->Handle.Load(Name, nullptr))
                {
                    NebulaZoneShellDestroy(NewShell);
                    delete NewShell;
                    continue;
                }

                // The first three floats are set to 1.0f again after the user-data hand-off.
                DALib::Engine->SetInstanceUserData(NewShell->Handle.EngineInstance,
                                                   &NewShell->UserData[0]);
                NewShell->UserData[0] = 1.0f;
                NewShell->UserData[1] = 1.0f;
                NewShell->UserData[2] = 1.0f;

                // Unique insert keyed by the zone's shell id (the same CreateID), result
                // discarded.
                Shells.insert(FNebulaZoneShellMap::value_type(Back->ZoneShellName.Id, NewShell));
                Shell = NewShell;
            }
            else if (Reader.is_value("shell_scalar"))
            {
                // Raised to 1.0 only on an ordered "less".
                if (ZonesAdded == 0 || Shell == nullptr)
                    continue;
                Shell->ShellScalar = Reader.get_value_float(0);
                if (Shell->ShellScalar < 1.0f)
                    Shell->ShellScalar = 1.0f;
            }
            else if (Reader.is_value("max_alpha"))
            {
                // Capped only when strictly greater.
                if (ZonesAdded == 0 || Shell == nullptr)
                    continue;
                Shell->MaxAlpha = Reader.get_value_float(0);
                if (Shell->MaxAlpha > 1.0f)
                    Shell->MaxAlpha = 1.0f;
            }
            else if (Reader.is_value("exclusion_tint"))
            {
                // / 255 (DAT_005c7f68); each is kept only on an ordered "less" than 1.0,
                // anything else is 1.0.
                if (ZonesAdded == 0 || Shell == nullptr)
                    continue;
                float const R = Reader.get_value_float(0);
                float const G = Reader.get_value_float(1);
                float const Bl = Reader.get_value_float(2);
                float const ScaledB = static_cast<float>(Bl * (1.0f / 255.0f));
                float const ScaledG = static_cast<float>(G * (1.0f / 255.0f));
                float const ScaledR = static_cast<float>(R * (1.0f / 255.0f));
                Shell->Tint[0] = (ScaledR < 1.0f) ? ScaledR : 1.0f;
                Shell->Tint[1] = (ScaledG < 1.0f) ? ScaledG : 1.0f;
                Shell->Tint[2] = (ScaledB < 1.0f) ? ScaledB : 1.0f;
            }
        }
    }

    // FUN_00552570 — [Lightning] INI into the member at Nebula+0x100; no test of `this`.
    void LightningLoadIni(FNebulaLightning* Lightning, INI_Reader& Reader)
    {
        if (Lightning->bActive != 0)
            Lightning->bActive = 0;
        Lightning->Zone = nullptr;
        while (Reader.read_value())
        {
            if (Reader.is_value("gap"))
                Lightning->Gap = Reader.get_value_float(0);
            else if (Reader.is_value("color"))
            {
                Lightning->Color[0] = Reader.get_value_float(0) * (1.0f / 255.0f);
                Lightning->Color[1] = Reader.get_value_float(1) * (1.0f / 255.0f);
                Lightning->Color[2] = Reader.get_value_float(2) * (1.0f / 255.0f);
            }
            else if (Reader.is_value("size"))
                Lightning->Size = Reader.get_value_float(0);
            else if (Reader.is_value("duration"))
                Lightning->Duration = Reader.get_value_float(0);
        }
        Lightning->Unknown_0x18 = 0.0f;
        Lightning->Texture = nullptr;
        Lightning->Unknown_0x3c[0] = 0.0f;
        Lightning->Unknown_0x3c[1] = 0.0f;
        Lightning->Unknown_0x3c[2] = 0.0f;
        // The first flash is a negative countdown (DAT_005e0df8 = double(-0.8f),
        // DAT_005db0b8 = double(0.7f)). This is the only write of +0x00 in Freelancer.exe, so
        // the draw's "+0x00 >= 0" test keeps a lightning with a positive gap dark for good.
        int const R = rand();
        Lightning->bActive = 1;
        Lightning->Timer = static_cast<float>(
            (static_cast<double>(-0.8f) -
             static_cast<double>(R) * 3.0517578125e-05f * static_cast<double>(0.7f)) *
            Lightning->Gap);
    }

    // FUN_005526d0 — clear the lightning's active flag (+0x14c).
    void LightningDisable(FNebulaLightning* Lightning)
    {
        if (Lightning->bActive != 0)
            Lightning->bActive = 0;
    }

    // FUN_005526f0 (stdcall) — one lightning bolt segment as a camera-facing quad. The
    // inlined vector helpers (FUN_00423b60, FUN_00423b30, FUN_00411d60) each store float.
    void LightningDrawSegment(FNebulaLightningSegment const* Seg, float const* Eye)
    {
        float const* const A = Seg->A;
        float const* const B = Seg->B;

        float EyeMinusA[3];
        EyeMinusA[0] = Eye[0] - A[0];
        EyeMinusA[1] = Eye[1] - A[1];
        EyeMinusA[2] = Eye[2] - A[2];

        float BA[3];
        BA[0] = B[0] - A[0];
        BA[1] = B[1] - A[1];
        BA[2] = B[2] - A[2];

        // Both products and the difference stay on the FPU, one float store (a float*float
        // product is exact in double).
        float Cross[3];
        Cross[0] = static_cast<float>(static_cast<double>(EyeMinusA[2]) * BA[1] -
                                      static_cast<double>(BA[2]) * EyeMinusA[1]);
        Cross[1] = static_cast<float>(static_cast<double>(BA[2]) * EyeMinusA[0] -
                                      static_cast<double>(EyeMinusA[2]) * BA[0]);
        Cross[2] = static_cast<float>(static_cast<double>(EyeMinusA[1]) * BA[0] -
                                      static_cast<double>(BA[1]) * EyeMinusA[0]);

        // The sum is stored to float first.
        float Mid[3];
        Mid[0] = (A[0] + B[0]) * 0.5f;
        Mid[1] = (A[1] + B[1]) * 0.5f;
        Mid[2] = (A[2] + B[2]) * 0.5f;

        float HalfChord[3];
        HalfChord[0] = Mid[0] - A[0];
        HalfChord[1] = Mid[1] - A[1];
        HalfChord[2] = Mid[2] - A[2];

        // Both lengths and the quotient stay on the FPU; no zero test in retail.
        double const LenHalf = sqrt(static_cast<double>(HalfChord[0]) * HalfChord[0] +
                                    static_cast<double>(HalfChord[1]) * HalfChord[1] +
                                    static_cast<double>(HalfChord[2]) * HalfChord[2]);
        double const LenCross = sqrt(static_cast<double>(Cross[0]) * Cross[0] +
                                     static_cast<double>(Cross[1]) * Cross[1] +
                                     static_cast<double>(Cross[2]) * Cross[2]);
        double const Scale = LenHalf / LenCross;
        Cross[0] = static_cast<float>(Cross[0] * Scale);
        Cross[1] = static_cast<float>(Cross[1] * Scale);
        Cross[2] = static_cast<float>(Cross[2] * Scale);

        float const K = 1.15f; // DAT_005db09c
        float Edge[3];
        Edge[0] = A[0] - Mid[0];
        Edge[1] = A[1] - Mid[1];
        Edge[2] = A[2] - Mid[2];
        Edge[0] *= K;
        Edge[1] *= K;
        Edge[2] *= K;
        Cross[0] *= K;
        Cross[1] *= K;
        Cross[2] *= K;

        // The UV block (+0x18) is read without a null test.
        float const* const UvBase = Seg->Uvs;

        Win3DTexCoord2f(UvBase[2], UvBase[3]); // +8, +0xc
        Win3DVertex3f(Mid[0] + Edge[0], Mid[1] + Edge[1], Mid[2] + Edge[2]);
        Win3DTexCoord2f(UvBase[4], UvBase[3]); // +0x10, +0xc
        Win3DVertex3f(Mid[0] + Cross[0], Mid[1] + Cross[1], Mid[2] + Cross[2]);
        Win3DTexCoord2f(UvBase[4], UvBase[5]); // +0x10, +0x14
        Win3DVertex3f(Mid[0] - Edge[0], Mid[1] - Edge[1], Mid[2] - Edge[2]);
        Win3DTexCoord2f(UvBase[2], UvBase[5]); // +8, +0x14
        Win3DVertex3f(Mid[0] - Cross[0], Mid[1] - Cross[1], Mid[2] - Cross[2]);
    }

    // FUN_005529b0 — the lightning flash draw.
    void LightningDraw(FNebulaLightning* Lightning, ICamera* Camera)
    {
        // Nothing unless active (+0x14c) and the countdown +0x00 is not below zero (only an
        // ordered "less" returns).
        if (Lightning->bActive == 0 || Lightning->Timer < 0.0f)
            return;

        Win3DDisable(kWin3DCullFace);
        Win3DEnable(kWin3DBlend);
        Win3DSetBlendFunc(kWin3DBlendOne, kWin3DBlendOne);
        Win3DSetDepthWrite(0);

        // The red product stays on the FPU between the two multiplies; green and blue are
        // rounded to float after the first.
        double const Jitter = static_cast<double>(rand()) * 3.0517578125e-05f;
        float const Intensity = Lightning->Intensity;
        float const Red =
            static_cast<float>(static_cast<double>(Lightning->Color[0]) * Jitter * Intensity);
        float const Green = static_cast<float>(
            static_cast<double>(static_cast<float>(Lightning->Color[1] * Jitter)) * Intensity);
        float const Blue = static_cast<float>(
            static_cast<double>(static_cast<float>(Lightning->Color[2] * Jitter)) * Intensity);

        Win3DApplyCamera(Camera);

        Win3DBindTexture(Lightning->Texture->GetGpuHandle(0));

        Win3DColor3f(Red, Green, Blue);
        Win3DBegin(kWin3DQuads);

        // Each of the eight segments draws while its age is below 3; the eye is re-read per
        // segment.
        for (int I = 0; I < 8; ++I)
        {
            if (Lightning->SegmentAge[I] < 3)
                LightningDrawSegment(&Lightning->Segments[I], (const float*)Camera->GetPosition());
        }

        Win3DEnd();
        Win3DSetDepthWrite(1);
        Win3DDisable(kWin3DBlend);
        Win3DEnable(kWin3DCullFace);
    }

} // anonymous namespace

void FNebulaClouds::Construct()
{
    LocalNebulaCloudsConstruct(this);
}

void FNebulaClouds::SetDetail(float Detail)
{
    LocalNebulaCloudsSetDetail(this, Detail);
}

void FNebulaClouds::ApplyDetail(float Detail)
{
    LocalNebulaCloudsApplyDetail(this, Detail);
}

void FNebulaClouds::ReleaseBits()
{
    LocalNebulaCloudsReleaseBits(this);
}

void FNebulaClouds::RebuildBits()
{
    LocalNebulaCloudsRebuildBits(this);
}

void FNebulaClouds::RegenExcluded(void* ExclusionCtx)
{
    LocalNebulaCloudsRegenExcluded(this, ExclusionCtx);
}

void FNebulaClouds::Update(ICamera* Camera, float Dt, float Visibility,
                           Exclusion::NebulaExclusionZoneMgr* ExclusionMgr)
{
    LocalNebulaCloudsUpdate(this, Camera, Dt, Visibility, ExclusionMgr);
}

void FNebulaClouds::Destroy(unsigned char Flags)
{
    LocalNebulaCloudsDestroy(this, Flags);
}

void FNebulaClouds::ParseIni(INI_Reader& Reader, FShapeDB* Shapes)
{
    LocalNebulaCloudsParseIni(this, Reader, Shapes);
}

// FUN_00504e60 -- vt+0x00. Caller: StarSysLoadIni's [Nebula] block. No test of `this` or of
// either string.
void FNebulaEffect::Load(char const* FilePath, char const* ZoneName)
{
    FNebulaEffect* const This = this;

    if (This->FlagLoaded != 0)
        This->Shutdown();
    This->bReachPending = 1;

    // A missing zone is an FDUMP error and nothing else -- the zone's GetShape is called
    // regardless.
    unsigned const SystemId = StarSysGetActiveSystemId();
    This->Zone = const_cast<Universe::IZone*>(Universe::get_zone(SystemId, ZoneName));
    if (This->Zone == nullptr)
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Nebula linked to missing Zone (%s)",
              "E:\\FL\\Scratch\\Source\\Client\\Space\\Nebula.cpp", 0x1ee, ZoneName);
    (void)This->Zone->GetShape();

    This->BitAlpha = 0.75f;
    This->Opacity = 0.75f;
    This->ExteriorShapeCount = 0;
    for (int I = 0; I < 8; ++I)
        This->ExteriorShapeWeights[I] = 1.0f;
    This->FogDistance = 10000.0f;
    This->FogNear = 0.0f;
    This->ActiveExclusionZone = nullptr;
    This->ExclusionImmersion = 0.0f;
    This->bHasExclusionShell = 0;
    This->ActiveExclusionShell = nullptr;
    This->BackLightningTimer = -1.0f;
    This->PlaneSlices = 1;

    INI_Reader Reader;
    char FullPath[MAX_PATH];
    GetFileSysPath(FullPath, FilePath, DALib::Data);
    if (!Reader.open(FullPath, false))
    {
        // An error, then the rebuild and the light as usual.
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: unable to open nebula file %s",
              "E:\\FL\\Scratch\\Source\\Client\\Space\\Nebula.cpp", 0x312, FullPath);
    }
    else
    {
        // A header nobody names is skipped.
        while (Reader.read_header())
        {
            if (Reader.is_header("Exterior"))
            {
                ParseExteriorSection(This, Reader);
            }
            else if (Reader.is_header("Clouds"))
            {
                // Only the ctor is guarded by the null test.
                void* const Mem = operator new(sizeof(FNebulaClouds));
                FNebulaClouds* Clouds = nullptr;
                if (Mem != nullptr)
                {
                    Clouds = static_cast<FNebulaClouds*>(Mem);
                    LocalNebulaCloudsConstruct(Clouds);
                }
                LocalNebulaCloudsSetDetail(Clouds, G_NebulaDetail);
                LocalNebulaCloudsParseIni(Clouds, Reader, NebulaShapes(This));
                Clouds->Owner = This->Zone;
                CloudsListPush(This, Clouds);
            }
            else if (Reader.is_header("ObjectFog"))
            {
                // Recognised and skipped -- its keys are never read.
            }
            else if (Reader.is_header("Fog"))
            {
                ParseFogSection(This, Reader);
            }
            else if (Reader.is_header("TexturePanels"))
            {
                // FUN_00425b30 with no base directory.
                while (Reader.read_value())
                {
                    if (Reader.is_value("file"))
                        NebulaShapes(This)->Load(GetFullFilename(Reader.get_value_string()),
                                                 nullptr);
                }
            }
            else if (Reader.is_header("Lightning"))
            {
                LightningLoadIni(&This->Lightning, Reader);
                This->Lightning.Zone = This->Zone;
            }
            else if (Reader.is_header("BackgroundLightning"))
            {
                ParseBackgroundLightningSection(This, Reader);
            }
            else if (Reader.is_header("DynamicLightning"))
            {
                ParseDynamicLightningSection(This, Reader);
            }
            else if (Reader.is_header("NebulaLight"))
            {
                ParseNebulaLightSection(This, Reader);
            }
            else if (Reader.is_header("Exclusion Zones"))
            {
                ParseExclusionZonesSection(This, Reader, SystemId);
            }
        }
    }

    This->Rebuild();

    // Ambient GameLight.
    {
        // Only a strictly positive period builds the light. There is no else: +0x270 keeps
        // whatever it held (null from the ctor, or the pointer the last Shutdown released --
        // retail never clears it there).
        float const Period = This->DynLightningDuration;
        if (Period > 0.0f)
        {
            // FUN_00537a90(0x12c); the result is used with no null test.
            void* Mem = GameLightHeapAlloc(sizeof(FGameLight));
            FGameLight* Light = nullptr;
            if (Mem != nullptr)
                Light = GameLightConstruct(static_cast<FGameLight*>(Mem));
            This->DynamicLight = Light;
            // Type 3 = directional.
            Light->SetType(kLightDirectional, false);
            Vector Zero = {0.0f, 0.0f, 0.0f};
            Light->SetPosition(&Zero);
            unsigned char White[3] = {0xff, 0xff, 0xff};
            Light->SetColorBytesAlt(White);
            Light->SetRangeRaw(500000.0f);
            // Success turns it straight off again when its flag bit 3 is set (FUN_005097c0's
            // flicker enables it).
            if (!Light->FGameLight::Enable())
            {
                Light->Release();
                // Retail passes the double 500000.0 to a "%d".
                FDUMP(kFdumpWarning,
                      "%s(%d) : *** WARNING: Could not create nebula light of range %d",
                      "E:\\FL\\Scratch\\Source\\Client\\Space\\Nebula.cpp", 0x32a, 500000.0);
                This->DynamicLight = nullptr;
            }
            else if ((Light->Flags & kLightFlagEnabled) != 0)
            {
                GameLightVirtualDisable(Light);
            }
        }
    }

    This->FlagLoaded = 1;
}

// LAB_00508b00 — screen fog fade overlay, then the lightning (+0x100). No test of `this` or
// Camera.
void FNebulaEffect::DrawFogOverlay(FSpaceCamera* Camera)
{
    FNebulaEffect* const This = this;
    if (This->FlagLoaded == 0)
        return;
    ICamera* const Cam = Camera;

    // Both are called and their results dropped.
    (void)This->Zone->GetPos();
    (void)Cam->GetOrientation();

    float const* const FadePtr = &This->InsideFade;
    bool bDrawOverlay = true;
    // Only an ordered "less" clears +0x2b8.
    if (*FadePtr < 1.0f)
        This->bFullOverlayDrawn = 0;
    // Otherwise a set +0x2b8 goes straight to the lightning.
    if (!(*FadePtr < 1.0f) && This->bFullOverlayDrawn != 0)
        bDrawOverlay = false;
    // <= 0 or unordered skips to the lightning.
    else if (!(*FadePtr > 0.0f))
        bDrawOverlay = false;

    if (bDrawOverlay)
    {
        if (*FadePtr >= 1.0f)
            This->bFullOverlayDrawn = 1;
    }

#ifdef FREELANCER_RP9
    // Port-only: while the Direct3D 9 volumetric nebula draws this nebula's inside
    // (nebula_volume.cpp), it replaces the fullscreen fog quad; the flag above and the
    // lightning below stay as retail.
    if (bDrawOverlay && !NebulaVolume::ReplacesFogOverlay(This))
#else
    if (bDrawOverlay)
#endif
    {
        // Copied before the state calls.
        float const Alpha = *FadePtr;
        Win3DSetOrtho2DForCamera(Camera);
        Win3DDisable(kWin3DLightingCap);
        Win3DSetDepthWrite(0);
        Win3DDisable(kWin3DDepthTest);
        Win3DDisable(kWin3DTexture2D); // GL_TEXTURE_2D
        Win3DEnable(kWin3DBlend);
        Win3DDisable(kWin3DCullFace);
        Win3DSetBlendFunc(kWin3DBlendSrcAlpha, kWin3DBlendInvSrcAlpha);
        Win3DBegin(kWin3DQuads);
        float const* Rgb = This->FogColor;
        Win3DColor4f(Rgb[0], Rgb[1], Rgb[2], Alpha);
        // The viewport rect is used with no null test.
        int const* const Vp = Cam->GetViewport();
        Win3DVertex3f(0.0f, 0.0f, 0.0f);
        Win3DVertex3f(static_cast<float>(Vp[2] - Vp[0]), 0.0f, 0.0f);
        Win3DVertex3f(static_cast<float>(Vp[2] - Vp[0]), static_cast<float>(Vp[3] - Vp[1]),
                      0.0f);
        Win3DVertex3f(0.0f, static_cast<float>(Vp[3] - Vp[1]), 0.0f);
        Win3DEnd();
        Win3DSetDepthWrite(1);
        Win3DEnable(kWin3DDepthTest);
        Win3DDisable(kWin3DBlend);
        Win3DEnable(kWin3DCullFace);
        Win3DApplyCameraPerspective(Cam);
    }

    // +0x294 is re-read; only a value strictly above 0 draws the lightning.
    if (*FadePtr > 0.0f)
        LightningDraw(&This->Lightning, Camera);
}

// LAB_00508cf0 — fog shell (fill sheets) under the MaterialBatcher / depth gate. No test of
// `this`, Camera, the MaterialBatcher or the Pipeline.
void FNebulaEffect::DrawFogShell(FSpaceCamera* Camera)
{
    FNebulaEffect* const This = this;
    if (This->FlagLoaded == 0)
        return;
    // Only an ordered "less" goes on.
    float const Fade = This->InsideFade;
    if (!(Fade < 1.0f))
        return;

    SetMaterialBatchBinOrder(1u, -1);
    DALib::Pipeline->SetRenderState(D3DRS_LIGHTING, 0);
    Win3DSetDepthWrite(0);
    Win3DDisable(kWin3DDepthTest);
    EngineLightClearRegisteredList();
    DALib::Pipeline->SetRenderState(D3DRS_AMBIENT, -1);

    // 12 dwords: the 3x3 axes and, at +0x24, the position.
    ICamera* const Cam = Camera;
    Transform Xform;
    memcpy(&Xform, Cam->GetOrientation(), sizeof(Xform));
    // The passed camera, not the active space camera.
    Win3DApplyCamera(Cam);
#ifdef FREELANCER_RP9
    // Port-only: while the Direct3D 9 volumetric nebula draws this nebula at full strength
    // (nebula_volume.cpp) the two fill sheets are left out -- the path FUN_005074e0 itself
    // takes without a fill shape; the states around it stay as retail.
    if (!NebulaVolume::ReplacesFillSheets(This))
#endif
        This->DrawFillSheets(Xform.Translation.x, Xform.Translation.y, Xform.Translation.z);

    SetMaterialBatchBinOrder(1u, 1);
    DALib::Pipeline->SetRenderState(D3DRS_LIGHTING, 1);
    Win3DSetDepthWrite(1);
    Win3DEnable(kWin3DDepthTest);
}

// FUN_00508e00 — fog render states + FNebulaEffect::DrawExteriorBits under the MaterialBatcher / depth gate.
// No null tests on This, the Pipeline or the MaterialBatcher, and no guard on a zero +0x298
// (retail divides straight through).
void FNebulaEffect::DrawExteriorBitsPass(FSpaceCamera* Camera)
{
    FNebulaEffect* const This = this;
    if (This->FlagLoaded == 0)
        return;
    float const FarZ = This->FogStart;
    float const Near = This->FogNear;
    float const FogDist = This->FogDistance;
    double T = (static_cast<double>(FarZ) - Near) * 20.0 / FogDist;
    if (T < 0.0)
        T = 0.0;
    float const FogEnd = static_cast<float>((T + 1.0) * FogDist + FarZ);
    FogApplyColorAndRange(This->FogColor, FarZ, FogEnd);

    SetMaterialBatchBinOrder(1u, -1);
    DALib::Pipeline->SetRenderState(D3DRS_LIGHTING, 0);
    Win3DSetDepthWrite(0);
    Win3DDisable(kWin3DDepthTest);
    EngineLightClearRegisteredList();
    DALib::Pipeline->SetRenderState(D3DRS_AMBIENT, -1);

#ifdef FREELANCER_RP9
    // Port-only: while the Direct3D 9 volumetric nebula draws this nebula at full strength
    // (nebula_volume.cpp) the exterior billboards are left out -- the path FUN_00507ed0 itself
    // takes with the zone out of view; the states around it stay as retail.
    if (!NebulaVolume::ReplacesExteriorBits(This))
#endif
        This->DrawExteriorBits(Camera);

    SetMaterialBatchBinOrder(1u, 1);
    DALib::Pipeline->SetRenderState(D3DRS_LIGHTING, 1);
    Win3DSetDepthWrite(1);
    Win3DEnable(kWin3DDepthTest);
    Win3DDisable(kWin3DFog); // GL_FOG
}

// LAB_00508f20 — exclusion-shell mesh draw, only while Update found a shell for the current
// exclusion zone (+0x2c / +0x30).
void FNebulaEffect::DrawExclusionShell(FSpaceCamera* Camera)
{
    FNebulaEffect* const This = this;
    if (This->FlagLoaded == 0)
        return;
    if (This->bHasExclusionShell == 0)
        return;

    // Depth test and culling are left alone.
    SetMaterialBatchBinOrder(1u, -1);
    DALib::Pipeline->SetRenderState(D3DRS_LIGHTING, 0);
    EngineLightClearRegisteredList();
    int SavedRs8b = 0;
    DALib::Pipeline->GetRenderState(D3DRS_AMBIENT, &SavedRs8b);
    DALib::Pipeline->SetRenderState(D3DRS_AMBIENT, -1);
    Win3DSetDepthWrite(0);
    Win3DDisable(kWin3DFog);

    // Kept on the FPU.
    double Alpha = (static_cast<double>(This->ExclusionImmersion) - 0.2f) * 1.25f;
    if (Alpha < 0.0)
        Alpha = 0.0f;
    else if (Alpha > 1.0)
        Alpha = 1.0f;

    FNebulaZoneShell* const Shell = This->ActiveExclusionShell;
    Shell->UserData[0] = Shell->Tint[0];
    Shell->UserData[1] = Shell->Tint[1];
    Shell->UserData[2] = Shell->Tint[2];
    Shell->UserData[4] = static_cast<float>(Alpha * Shell->MaxAlpha);

    Exclusion::NebulaExclusionZone* const Excl = This->ActiveExclusionZone;
    Universe::IZone const& Zone = *Excl->ZonePtr;
    float const Scale = static_cast<float>(
        static_cast<double>(Zone.GetRadius()) * Shell->ShellScalar * 0.001f);
    Transform ScaleXf;
    memset(&ScaleXf, 0, sizeof(ScaleXf));
    ScaleXf.Rotation.E[0][0] = Scale;
    ScaleXf.Rotation.E[1][1] = Scale;
    ScaleXf.Rotation.E[2][2] = Scale;

    Transform const& ZoneXf = Zone.GetTransform();
    Transform Scaled;
    AcquireNebulaMath()->MulTransform(Scaled, ZoneXf, ScaleXf);
    DALib::Engine->SetInstanceMatrix(Shell->Handle.EngineInstance, &Scaled);

    if (Shell->Handle.EngineInstance != InvalidInstanceIndex)
    {
        DALib::Renderer->RenderInstance(Camera, Shell->Handle.EngineInstance, 1.0f, kDrawNoCull | kDrawPickSwitch, nullptr,
                              nullptr);
    }

    SetMaterialBatchBinOrder(1u, 1);
    DALib::Pipeline->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW);
    DALib::Pipeline->SetRenderState(D3DRS_LIGHTING, 1);
    DALib::Pipeline->SetRenderState(D3DRS_AMBIENT, SavedRs8b);
    Win3DSetDepthWrite(1);
}

// LAB_005091a0 — the clouds list (+0xf8) through FUN_00551500.
void FNebulaEffect::DrawCloudsPanels(FSpaceCamera* Camera)
{
    FNebulaEffect* const This = this;
    NebulaDrawCloudsPanels(This, Camera);
}

// LAB_00509340 — exterior distance factor against the zone (+0x20, not the exclusion +0x24).
float FNebulaEffect::ZoneExteriorFactor(float const* CamPos3)
{
    FNebulaEffect* const This = this;
    Universe::IZone const& Zone = *This->Zone;
    Vector const& ZonePos = Zone.GetPos();
    Vector Dir;
    Dir.x = CamPos3[0] - ZonePos.x;
    Dir.y = CamPos3[1] - ZonePos.y;
    Dir.z = CamPos3[2] - ZonePos.z;

    // The squared distance is rounded to float, the squared radius is not; <= or unordered
    // goes on.
    double const Radius = Zone.GetRadius();
    float const DistSq = static_cast<float>(
        (static_cast<double>(Dir.x) * Dir.x + static_cast<double>(Dir.z) * Dir.z) +
        static_cast<double>(Dir.y) * Dir.y);
    if (DistSq > Radius * Radius)
        return 0.0f;

    // double .rdata:005c7720; >= or unordered goes on.
    if (DistSq < 0.1)
        return 1.0f;

    double const Dist = sqrt(static_cast<double>(DistSq));
    float const DistF = static_cast<float>(Dist);
    double const Inv = 1.0 / Dist;
    Dir.x = static_cast<float>(Dir.x * Inv);
    Dir.y = static_cast<float>(Dir.y * Inv);
    Dir.z = static_cast<float>(Dir.z * Inv);

    // The unrounded edge is tested, the float edge divided.
    double const Edge = static_cast<double>(Zone.GetRadiusAlong(Dir)) - DistF;
    float const EdgeF = static_cast<float>(Edge);
    if (Edge < 0.0)
        return 0.0f;

    float const FogDist = This->FogDistance;
    if (EdgeF > FogDist)
        return 1.0f;
    return EdgeF / FogDist;
}

// LAB_00504bf0 / LAB_00504c00 / LAB_00509470 / LAB_00509660 / LAB_00509670: plain float
// getters, no test of `this`.
float FNebulaEffect::GetInsideFade()
{
    return InsideFade;
}

float FNebulaEffect::GetSunBurnthroughIntensity()
{
    return SunBurnthroughIntensity;
}

float FNebulaEffect::GetSunBurnthroughScaler()
{
    return SunBurnthroughScaler;
}

// LAB_00509480 — copy the fog RGB (+0x2a0); no tests. Returns Out, which the space scene's
// lighting pass uses.
float* FNebulaEffect::CopyFogRgb(float* OutRgb3)
{
    FNebulaEffect* const This = this;
    float const* Src = This->FogColor;
    OutRgb3[0] = Src[0];
    OutRgb3[1] = Src[1];
    OutRgb3[2] = Src[2];
    return OutRgb3;
}

// LAB_005094b0 — fog RGB with the background-lightning flicker (or a plain copy of +0x2a0
// when idle). No test of `this` or of the out pointer; every exit returns Out.
float* FNebulaEffect::FillFogRgb(float* OutRgb3)
{
    FNebulaEffect* const This = this;
    // Copy when the period is <= 0; a NaN period goes on.
    float const Period = This->BackLightningDuration;
    if (Period <= 0.0f)
    {
        float const* Src = This->FogColor;
        OutRgb3[0] = Src[0];
        OutRgb3[1] = Src[1];
        OutRgb3[2] = Src[2];
        return OutRgb3;
    }

    // The sum is stored as float; the unrounded value is compared below.
    float const AccPrev = This->BackLightningTimer;
    This->bBackLightningFlash = 0;
    float const Dt = This->FrameDelta;
    double const Acc = static_cast<double>(Dt) + AccPrev;
    This->BackLightningTimer = static_cast<float>(Acc);

    if (AccPrev > 0.0f)
    {
        // A sum strictly above the period re-arms the countdown.
        This->bBackLightningFlash = 1;
        if (Acc > Period)
        {
            int const R = rand();
            This->BackLightningTimer = static_cast<float>(
                (static_cast<double>(R) * 3.0517578125e-05f * static_cast<double>(0.7f) - 1.5) *
                This->BackLightningGap);
        }
    }
    else if (Acc > 0.0)
    {
        This->bBackLightningFlash = 1;
    }

    if (This->bBackLightningFlash == 0)
    {
        float const* Src = This->FogColor;
        OutRgb3[0] = Src[0];
        OutRgb3[1] = Src[1];
        OutRgb3[2] = Src[2];
        return OutRgb3;
    }

    // Scale stays on the FPU.
    float const U0 = static_cast<float>(static_cast<double>(rand()) * 3.0517578125e-05f);
    int const Rand2 = rand();
    double const Scale = static_cast<double>(Rand2) * This->BackLightningIntensity * U0 *
                         3.0517578125e-05f;
    // The green and blue products are stored as float before the base is added; the red
    // product stays on the FPU. (Retail compares green's unrounded sum below, which clamps
    // to the same value.)
    float const GreenProduct = static_cast<float>(Scale * This->BackLightningColor[1]);
    float const BlueProduct = static_cast<float>(Scale * This->BackLightningColor[2]);
    float R = static_cast<float>(Scale * This->BackLightningColor[0] + This->FogColor[0]);
    float G = GreenProduct + This->FogColor[1];
    float Bv = BlueProduct + This->FogColor[2];
    // Keep only a value below 1.0; anything else (>=, NaN) becomes 1.0f.
    if (!(R < 1.0f))
        R = 1.0f;
    if (!(G < 1.0f))
        G = 1.0f;
    if (!(Bv < 1.0f))
        Bv = 1.0f;
    OutRgb3[0] = R;
    OutRgb3[1] = G;
    OutRgb3[2] = Bv;
    return OutRgb3;
}

float FNebulaEffect::GetFogStart()
{
    return FogStart;
}

// FUN_00509680 — fog factor; +0x24 is the active NebulaExclusionZone*.
float FNebulaEffect::FogFactor(float const* CamPos3)
{
    FNebulaEffect* const This = this;
    float const FarZ = This->FogEnd;
    Exclusion::NebulaExclusionZone* const Excl = This->ActiveExclusionZone;
    if (Excl != nullptr)
    {
        // Blended between FarZ and FogNear + FogDistance by the immersion.
        double const Inside = Excl->ZonePtr->GetImmersion(*reinterpret_cast<Vector const*>(CamPos3));
        float const Span = This->FogNear + This->FogDistance;
        return static_cast<float>((1.0f - Inside) * Span + Inside * FarZ);
    }

    // FarZ when F >= 1 or unordered.
    double F = This->ZoneExteriorFactor(CamPos3);
    if (!(F < 1.0f))
        return FarZ;
    F = F * F;
    if (F < 0.0001f)
        F = 0.0001f;
    return static_cast<float>(FarZ / F);
}

float FNebulaEffect::GetFogEnd()
{
    return FogEnd;
}

// LAB_00509710. The one stack argument is never read; the draw pass pushes DAT_006164d4
// there. No test of `this` in retail.
float FNebulaEffect::FogFarClamp(float Unused)
{
    FNebulaEffect* const This = this;
    (void)Unused;
    // 500.0f is .rdata 0x005d9668. The compare below uses the unrounded sum.
    const double Sum = static_cast<double>(This->GetFogEnd()) + 500.0f;
    float V = static_cast<float>(Sum);
    // An unordered sum is kept.
    const float Floor = This->FogFarMin;
    if (Sum < Floor)
        V = Floor;
    // Only the exclusion pointer is tested; 500.0 is double .rdata 0x005db0c0. V <= R or
    // unordered returns R.
    Exclusion::NebulaExclusionZone* const Excl = This->ActiveExclusionZone;
    if (Excl != nullptr)
    {
        const double Radius = static_cast<double>(Excl->ZonePtr->GetRadius());
        const double R = (Radius + Radius) + 500.0;
        if (!(V > R))
            return static_cast<float>(R);
    }
    return V;
}

// LAB_00509770 — the [NebulaLight] ambient bytes (+0x2cd..+0x2cf) when their flag +0x2cc is
// set, else null; no test of `this`. Reader: the space scene's ambient lerp.
unsigned char const* FNebulaEffect::GetAmbientBytes()
{
    FNebulaEffect* const This = this;
    if (This->bHasAmbient == 0)
        return nullptr;
    return This->Ambient;
}

// FUN_00504880, shared with the JumpShipEffect colour getter (JumpShipEffectColor,
// space_scene_obj_draw.cpp). No test of `this`.
Universe::IZone* FNebulaEffect::GetZone()
{
    FNebulaEffect* const This = this;
    return This->Zone;
}

// FUN_00509790. No tests of `this`, the out pointer or the zone.
float FNebulaEffect::GetZoneCenterRadius(float* OutPos3)
{
    FNebulaEffect* const This = this;
    Vector const& Pos = This->Zone->GetPos();
    OutPos3[0] = Pos.x;
    OutPos3[1] = Pos.y;
    OutPos3[2] = Pos.z;
    return This->Zone->GetRadius();
}

FNebulaEffectCompanion::FNebulaEffectCompanion()
{
    EffectArch = nullptr;
    Sound = nullptr;
    Effect = nullptr;
    Light = nullptr;
}

FNebulaEffectCompanion::~FNebulaEffectCompanion()
{
    CliEffectCompanionClear(this);
}

FNebulaCloudsListStorage::FNebulaCloudsListStorage()
{
    void* ListSent = operator new(0xc);
    *static_cast<void**>(ListSent) = ListSent;
    *reinterpret_cast<void**>(static_cast<unsigned char*>(ListSent) + 4) = ListSent;
    Sentinel = static_cast<FNebulaCloudsListNode*>(ListSent);
    Count = 0;
}

FNebulaCloudsListStorage::~FNebulaCloudsListStorage()
{
    FNebulaCloudsListNode* Node = Sentinel->Next;
    while (Node != Sentinel)
    {
        FNebulaCloudsListNode* const Next = Node->Next;
        operator delete(Node);
        Node = Next;
    }
    operator delete(Sentinel);
    Sentinel = nullptr;
    Count = 0;
}

FNebulaEffect::FNebulaEffect()
    : ActiveExclusionZone(nullptr), ExclusionImmersion(0), bHasExclusionShell(0),
      ActiveExclusionShell(nullptr)
{
    // FUN_00504970. Bytes not stored below are deliberately left untouched.
    Lightning.bActive = 0;

    ExteriorShapeCount = 0;
    ExteriorBitCount = 0;
    ExteriorBits = nullptr;
    Zone = nullptr;
    FlagLoaded = 0;
    bHasAmbient = 0;
    EquatorBias = 0.0f;
    InsideFade = 0.0f;
    MaxCloudsReach = 0.0f;
    FillShape = nullptr;
    ExclusionMgr = nullptr;

    // Retail erases the (empty) zone_shell map here (FUN_00509d60).
    NebulaZoneShells(this).clear();

    bBackLightningFlash = 0;
    DynamicLight = nullptr;
    FrameDelta = 0.0f;

    MinBits = 4;
    MaxBits = 8;
    BitRadiusMin = 1.0f;
    BitRadiusMax = 1.0f;
    MoveBitPercent = 0.75f; // 0x3f400000
    bCullBackBits = 1;

    ExclusionSortTimer = 0.1f;   // 0x3dcccccd
    ExteriorSortTimer = 10.0f;   // 0x41200000
    ExteriorOrientTimer = 0.2f;  // 0x3e4ccccd

    BackLightningGap = 10.0f;      // 0x41200000
    BackLightningDuration = 0.5f;  // 0x3f000000
    BackLightningTimer = -1.0f;    // 0xbf800000
    BackLightningColor[0] = 0.25f; // 0x3e800000
    BackLightningColor[1] = 0.25f;
    BackLightningColor[2] = 0.25f;
    BackLightningIntensity = 1.0f;

    DynLightningGap = 3.0f;       // 0x40400000
    DynLightningDuration = 0.5f;  // 0x3f000000
    DynLightningTimer = -1.0f;    // 0xbf800000
    DynLightningColor[0] = 1.0f;
    DynLightningColor[1] = 1.0f;
    DynLightningColor[2] = 1.0f;
    DynAmbientIntensity = 0.25f;  // 0x3e800000
    DynIntensityIncrease = 0.75f; // 0x3f400000

    SunBurnthroughIntensity = 0.5f; // 0x3f000000
    SunBurnthroughScaler = 1.75f;   // 0x3fe00000
    FogFarMin = 5000.0f;            // 0x459c4000
}

FNebulaEffect* NebulaConstruct(FNebulaEffect* This)
{
    return ::new (This) FNebulaEffect();
}

FNebulaEffect* NebulaCreate()
{
    FNebulaEffect* const Nebula = new FNebulaEffect;
    Nebula->Detail(G_NebulaDetail);
    return Nebula;
}


void NebulaDestroy(FNebulaEffect* Nebula)
{
    // FUN_00509b70: a non-null nebula gets the deleting dtor (vt+0x58).
    if (Nebula == nullptr)
        return;
#ifdef FREELANCER_RP9
    NebulaVolume::UnregisterNebula(Nebula); // port-only bookkeeping (nebula_volume.cpp)
#endif
    delete Nebula;
}

void FNebulaEffect::Detail(float Detail)
{
    FNebulaEffect* const Nebula = this;
    // LAB_00509ac0 -- vt+0x54. The global detail is not written here (the options callback
    // FUN_00509b80 does that); no test of `this` or of a node.
    FNebulaCloudsListNode* const Sentinel = Nebula->Clouds.Sentinel;
    for (FNebulaCloudsListNode* N = Sentinel->Next; N != Sentinel; N = N->Next)
        LocalNebulaCloudsApplyDetail(N->Clouds, Detail);
    Nebula->Rebuild();
}

void NebulaApplyGlobalDetail(float Detail)
{
    // FUN_00509b80 — PerfOptions Nebula callback.
    RaiseActiveSpaceCameraCut(); // FUN_0054bd50, before the store
    G_NebulaDetail = Detail;
    std::list<FNebulaEffect*>& Nebulae = StarSysNebulaList();
    for (std::list<FNebulaEffect*>::iterator It = Nebulae.begin(); It != Nebulae.end(); ++It)
    {
        FNebulaEffect* const Nebula = *It;
        Nebula->Detail(Detail);

    }
    TexturePanelsApplyDetailAll(Detail); // FUN_00509f60
}

void FNebulaEffect::OrientExteriorBits(float Tx, float Ty, float Tz)
{
    // FUN_00507340: re-aim each exterior bit's axis (+0x14..+0x1c) square to the direction from
    // the bit to the target. No test of `this`, the zone or the buffer; no zero-length test.
    Vector const ZonePos = Zone->GetPos();
    float const RelX = Tx - ZonePos.x;
    float const RelY = Ty - ZonePos.y;
    float const RelZ = Tz - ZonePos.z;

    for (int I = 0; I < ExteriorBitCount; ++I)
    {
        FNebulaExteriorBit* const Bit = &ExteriorBits[I];
        // D = normalize(Rel - anchor). The z difference is squared as unrounded * float. Dx and
        // Dz are stored as floats, Dy stays on the FPU; the dot below uses the unrounded Dz, the
        // cross the stored one.
        double const A = static_cast<double>(RelX) - Bit->Offset[0];
        double const Bv = static_cast<double>(RelY) - Bit->Offset[1];
        double const C = static_cast<double>(RelZ) - Bit->Offset[2];
        float const CF = static_cast<float>(C);
        float const Inv = static_cast<float>(1.0f / sqrt((C * CF + Bv * Bv) + A * A));
        float const Dx = static_cast<float>(A * Inv);
        double const Dy = Bv * Inv;
        double const DzExt = static_cast<double>(CF) * Inv;
        float const DzF = static_cast<float>(DzExt);

        // Only strictly above 0.25 re-aims.
        double const Dot =
            fabs((DzExt * Bit->Axis[2] + Dy * Bit->Axis[1]) + static_cast<double>(Dx) * Bit->Axis[0]);
        if (Dot > 0.25)
        {
            // N = D x O, on the FPU.
            double const Cx = Dy * Bit->Axis[2] - static_cast<double>(DzF) * Bit->Axis[1];
            double const Cy = static_cast<double>(DzF) * Bit->Axis[0] - static_cast<double>(Dx) * Bit->Axis[2];
            double const Cz = static_cast<double>(Dx) * Bit->Axis[1] - Dy * Bit->Axis[0];
            float const CzF = static_cast<float>(Cz);
            float const CInv = static_cast<float>(1.0f / sqrt((Cz * CzF + Cy * Cy) + Cx * Cx));
            float const Ncx = static_cast<float>(Cx * CInv);
            double const Ncy = Cy * CInv;
            double const Ncz = static_cast<double>(CzF) * CInv;
            // O = N x D.
            Bit->Axis[0] = static_cast<float>(Ncy * DzF - Ncz * Dy);
            Bit->Axis[1] = static_cast<float>(Ncz * Dx - static_cast<double>(DzF) * Ncx);
            Bit->Axis[2] = static_cast<float>(Dy * Ncx - Ncy * Dx);
        }
    }
}

namespace
{
    // FUN_00506600's projection step (rings and the two caps).
    void NebulaBitProject(Universe::IZone* Zone, Vector const& Point, Vector const& ZonePos,
                          FNebulaExteriorBit* Bit)
    {
        Vector const Surface = Zone->GetSurfacePoint(Point);
        Bit->Offset[0] = Surface.x - ZonePos.x;
        Bit->Offset[1] = Surface.y - ZonePos.y;
        Bit->Offset[2] = Surface.z - ZonePos.z;
    }

    // FUN_00506600's pull-in step: the factor on the FPU, no zero test. Len is the caller's
    // sqrt; the sites differ in their summation order.
    void NebulaBitShrink(FNebulaEffect* Nebula, FNebulaExteriorBit* Bit, double Len)
    {
        double const Margin = static_cast<double>(Nebula->MoveBitPercent) * Nebula->BitRadius;
        double const Shrink = (Len - Margin) / Len;
        Bit->Offset[0] = static_cast<float>(Shrink * Bit->Offset[0]);
        Bit->Offset[1] = static_cast<float>(Shrink * Bit->Offset[1]);
        Bit->Offset[2] = static_cast<float>(Shrink * Bit->Offset[2]);
    }
}

void FNebulaEffect::Rebuild()
{
    // FUN_00506600: the exterior bits (+0xe0, count +0xdc) rebuilt on the zone's surface. No
    // test of `this`.
    operator delete(ExteriorBits);
    ExteriorBits = nullptr;
    ExteriorBitCount = 0;

    // double 0.05 (0x005d5b58); only an ordered "less" returns. The zone is the one pointer
    // tested.
    if (static_cast<double>(G_NebulaDetail) < 0.05 || Zone == nullptr)
        return;

    Vector const ZonePos = Zone->GetPos();

    // Intermediates stay on the FPU.
    double const OneMinusDetail = 1.0 - static_cast<double>(G_NebulaDetail);
    double const Ease = 1.0f - OneMinusDetail * OneMinusDetail;
    double const OneMinusEase = 1.0f - Ease;
    float const Scale = static_cast<float>((G_NebulaDetailEase * OneMinusEase + Ease) *
                                           BitRadius);
    float const Density = static_cast<float>(
        (1.0 - static_cast<double>(G_NebulaDetail)) * G_NebulaDetailMix + G_NebulaDetail);
    BitAlpha =
        static_cast<float>((OneMinusEase * G_NebulaDetailFog + Ease) * Opacity);

    // Per-slice bit counts: max_bits at the middle slice, min_bits at the ends.
    int const MidCount = static_cast<int>(static_cast<double>(MaxBits) * Density + 0.5f);
    int const EndCount = static_cast<int>(static_cast<double>(MinBits) * Density + 0.5f);

    int const PlaneSlices = this->PlaneSlices;
    if (PlaneSlices <= 0)
        return;

    // Shape weights normalised; the sum on the FPU, no zero test.
    {
        float* const Weights = ExteriorShapeWeights;
        int const WeightCount = ExteriorShapeCount;
        double Sum = 0.0f;
        for (int I = 0; I < WeightCount; ++I)
            Sum += Weights[I];
        if (WeightCount > 0)
        {
            double const InvSum = 1.0f / Sum;
            for (int I = 0; I < ExteriorShapeCount; ++I)
                Weights[I] = static_cast<float>(InvSum * Weights[I]);
        }
    }

    // Total bit count: W rises to the middle slice and falls after it. This pass multiplies by
    // a float 1.0f / Mid; the ring pass below divides by Mid.
    float const Mid = static_cast<float>(static_cast<double>(PlaneSlices - 1) * 0.5);
    int BitTotal = 0;
    if (PlaneSlices == 1)
    {
        BitTotal = MidCount;
    }
    else
    {
        float const InvMid = static_cast<float>(1.0f / static_cast<double>(Mid));
        double const EndCountD = EndCount;
        float const MidCountF = static_cast<float>(MidCount);
        for (int I = 0; I < PlaneSlices; ++I)
        {
            double const T = I;
            double W;
            if (T > Mid)
                W = 1.0f - (T - Mid) * InvMid;
            else
                W = T * InvMid;
            BitTotal += static_cast<int>(floor((1.0 - W) * EndCountD + MidCountF * W + 0.5));
        }
    }

    // + 2 for the two polar caps; the buffer is neither tested nor cleared.
    int const BitCount = BitTotal + 2;
    ExteriorBitCount = BitCount;
    FNebulaExteriorBit* const Bits =
        static_cast<FNebulaExteriorBit*>(operator new(static_cast<unsigned>(BitCount) << 5));
    ExteriorBits = Bits;

    // Per bit: a random radius, the axis sin/cos of a random angle and 0, a weighted random
    // shape.

    for (int I = 0; I < ExteriorBitCount; ++I)
    {
        FNebulaExteriorBit* const Bit = &Bits[I];
        float const RadiusLo = BitRadiusMin;
        float const RadiusHi = BitRadiusMax;
        int const RRadius = rand();
        Bit->Radius = static_cast<float>(
            (static_cast<double>(RRadius) * 3.0517578125e-05f *
                 (static_cast<double>(RadiusHi) - RadiusLo) +
             RadiusLo) *
            Scale);
        // 2pi is double 0x005c97f8.
        int const RAngle = rand();
        double const Angle =
            static_cast<double>(RAngle) * 3.0517578125e-05f * 6.2831854820251465;
        Bit->Axis[0] = static_cast<float>(sin(Angle));
        Bit->Axis[1] = static_cast<float>(cos(Angle));
        Bit->Axis[2] = 0.0f;
        // The weights accumulate on the FPU. No match leaves +0x10 as it was.
        int const RPick = rand();
        float const Pick = static_cast<float>(static_cast<double>(RPick) * 3.0517578125e-05f);
        int const WeightCount = ExteriorShapeCount;
        double Running = 0.0f;
        for (int J = 0; J < WeightCount; ++J)
        {
            Running += ExteriorShapeWeights[J];
            if (Pick < Running)
            {
                Bit->Shape = ExteriorShapes[J];
                break;
            }
        }
    }

    // The latitude rings. The phase carries over between slices.
    FNebulaExteriorBit* Bit = Bits;
    float Phase = 0.0f;
    for (int Slice = 0; Slice < this->PlaneSlices; ++Slice)
    {
        int const Slices = this->PlaneSlices;
        int SliceBits;
        if (Slices == 1)
        {
            SliceBits = MidCount;
        }
        else
        {
            // Past the middle the phase becomes pi (0x005c8a5c) / SliceBits, no zero test.
            float const T = static_cast<float>(Slice);
            double W;
            if (T > Mid)
                W = 1.0f - (static_cast<double>(T) - Mid) / Mid;
            else
                W = static_cast<double>(T) / Mid;
            SliceBits = static_cast<int>(
                floor((1.0 - W) * EndCount + static_cast<double>(MidCount) * W + 0.5));
            if (T > Mid)
                Phase = static_cast<float>(3.1415927f / static_cast<double>(SliceBits));
        }

        double const Lat =
            acos((((static_cast<double>(Slice) + 1.0f) / (static_cast<double>(Slices) + 1.0f)) *
                      2.0 -
                  1.0f) *
                 EquatorBias);

        if (SliceBits > 0)
        {
            // cos(Lat) is stored as float, sin(Lat) stays double.
            double const BitsD = SliceBits;
            float const CosLat = static_cast<float>(cos(Lat));
            double const SinLat = sin(Lat);
            float const PointY = ZonePos.y + CosLat;
            for (int BitIndex = 0; BitIndex < SliceBits; ++BitIndex)
            {
                // x is kept unrounded for the point; the offset is overwritten by the projection.
                Bit->Offset[1] = CosLat;
                double const Angle =
                    static_cast<double>(BitIndex) * 6.2831854820251465 / BitsD + Phase;
                double const DirX = cos(Angle) * SinLat;
                Bit->Offset[0] = static_cast<float>(DirX);
                float const DirZ = static_cast<float>(sin(Angle) * SinLat);
                Bit->Offset[2] = DirZ;
                Vector Point;
                Point.x = static_cast<float>(ZonePos.x + DirX);
                Point.y = PointY;
                Point.z = ZonePos.z + DirZ;
                NebulaBitProject(Zone, Point, ZonePos, Bit);
                double const Len = sqrt((static_cast<double>(Bit->Offset[2]) * Bit->Offset[2] +
                                         static_cast<double>(Bit->Offset[1]) * Bit->Offset[1]) +
                                        static_cast<double>(Bit->Offset[0]) * Bit->Offset[0]);
                NebulaBitShrink(this, Bit, Len);
                ++Bit;
            }
        }
    }

    // The +Y cap; same length order.
    Vector Point;
    Point.x = ZonePos.x;
    Point.y = ZonePos.y + 1.0f;
    Point.z = ZonePos.z;
    NebulaBitProject(Zone, Point, ZonePos, Bit);
    double const TopLen = sqrt((static_cast<double>(Bit->Offset[2]) * Bit->Offset[2] +
                                static_cast<double>(Bit->Offset[1]) * Bit->Offset[1]) +
                               static_cast<double>(Bit->Offset[0]) * Bit->Offset[0]);
    NebulaBitShrink(this, Bit, TopLen);
    ++Bit;

    // The -Y cap; this length sums (x^2 + y^2) + z^2.
    Point.y = ZonePos.y - 1.0f;
    NebulaBitProject(Zone, Point, ZonePos, Bit);
    double const BottomLen = sqrt((static_cast<double>(Bit->Offset[0]) * Bit->Offset[0] +
                                   static_cast<double>(Bit->Offset[1]) * Bit->Offset[1]) +
                                  static_cast<double>(Bit->Offset[2]) * Bit->Offset[2]);
    NebulaBitShrink(this, Bit, BottomLen);
}

void FNebulaEffect::Update(FSpaceCamera* Camera, float Dt)
{
    FNebulaEffect* const Nebula = this;
    // LAB_00506f20 — vtable +0x08.
    Nebula->FrameDelta = Dt;

    // After a (re)load (+0x4c), +0x2b4 = the largest clouds reach. Retail inlines
    // NebulaExclusionZoneMgr::set_max_distance (Common .text:06242b70).
    if (Nebula->bReachPending != 0)
    {
        Nebula->MaxCloudsReach = 0.0f;
        FNebulaCloudsListNode* const Sentinel = Nebula->Clouds.Sentinel;
        for (FNebulaCloudsListNode* N = Sentinel->Next; N != Sentinel; N = N->Next)
        {
            FNebulaClouds const* const Clouds = N->Clouds;
            double const Reach =
                static_cast<double>(Clouds->MaxDistance) * 0.5f + Clouds->PuffRadius;
            if (Reach > Nebula->MaxCloudsReach)
                Nebula->MaxCloudsReach = static_cast<float>(Reach);
        }
        if (Nebula->ExclusionMgr != nullptr)
            Nebula->ExclusionMgr->set_max_distance(Nebula->MaxCloudsReach);
        Nebula->bReachPending = 0;
    }

    ICamera* const Cam = Camera;
    const Vector* const CamPosPtr = Cam->GetPosition();
    Vector CamPos;
    CamPos.x = CamPosPtr->x;
    CamPos.y = CamPosPtr->y;
    CamPos.z = CamPosPtr->z;
    // The camera's cut flag, byte +0xbc of the camera object (FSpaceCamera::FlagBC, raised by
    // FUN_0054bd50 when the view jumps); retail reads it raw.
    unsigned char const CameraCut = Camera->FlagBC;

    // Not yet fully inside (>= and unordered skip): re-orient the exterior bits when the +0x58
    // timer is above 1.0 or the camera cut, then restart that timer.
    if (Nebula->InsideFade < 1.0)
    {
        if (Nebula->ExteriorOrientTimer > 1.0f || CameraCut != 0)
        {
            Nebula->OrientExteriorBits(CamPos.x, CamPos.y, CamPos.z);
            Nebula->ExteriorOrientTimer = 0.0f;
        }
    }

    // Partly inside: the exclusion manager is sorted around the camera on a camera cut, or
    // when +0x50 is above 0.25 and +0x58 is not zero (only an ordered equal skips).
    if (Nebula->InsideFade > 0.0)
    {
        bool bSort = CameraCut != 0;
        if (!bSort)
            bSort = Nebula->InsideFade > 0.0 && Nebula->ExclusionSortTimer > 0.25f &&
                    !(Nebula->ExteriorOrientTimer == 0.0f);
        if (bSort && Nebula->ExclusionMgr != nullptr)
            Nebula->ExclusionMgr->sort(&CamPos);
    }

    // Depth into the zone: D = |camera - zone| minus the surface distance along that
    // direction, positive outside. GetRadius is called and its result dropped. <= and
    // unordered divide.
    Universe::IZone const* const Zone = Nebula->Zone;
    Vector const& ZonePos = Zone->GetPos();
    Vector Dir;
    Dir.x = CamPos.x - ZonePos.x;
    Dir.y = CamPos.y - ZonePos.y;
    Dir.z = CamPos.z - ZonePos.z;
    (void)Zone->GetRadius();
    float const Len = static_cast<float>(sqrt(static_cast<double>(Dir.y) * Dir.y +
                                              static_cast<double>(Dir.z) * Dir.z +
                                              static_cast<double>(Dir.x) * Dir.x));
    float const FogSpan = Nebula->FogDistance + Nebula->FogNear;
    double const InvLen = 1.0 / Len;
    Dir.x = static_cast<float>(Dir.x * InvLen);
    Dir.y = static_cast<float>(Dir.y * InvLen);
    Dir.z = static_cast<float>(Dir.z * InvLen);
    float const Depth = static_cast<float>(static_cast<double>(Len) - Zone->GetRadiusAlong(Dir));
    if (Depth > 0.0f)
        Nebula->InsideFade = 0.0f;
    else
    {
        double const Inside = -static_cast<double>(Depth);
        if (Inside > FogSpan)
            Nebula->InsideFade = 1.0f;
        else
            Nebula->InsideFade = static_cast<float>(Inside / FogSpan);
    }

    if (Nebula->InsideFade > 0.0)
    {
        Nebula->FogEnd = Nebula->FogDistance + Nebula->FogNear;
        Nebula->FogStart = Nebula->FogNear;

        // The current exclusion zone (+0x24) is dropped when the camera's immersion falls below
        // 1e-4 (>= and unordered keep it); +0x28 keeps that immersion.
        Exclusion::NebulaExclusionZone** const ExclSlot = &Nebula->ActiveExclusionZone;
        if (*ExclSlot != nullptr)
        {
            float const Immersion = (*ExclSlot)->ZonePtr->GetImmersion(CamPos);
            Nebula->ExclusionImmersion = Immersion;
            if (Immersion < 9.999999747378752e-05f) // DAT_005db098
            {
                *ExclSlot = nullptr;
                Nebula->ExclusionImmersion = 0.0f;
                Nebula->bHasExclusionShell = 0;
                Nebula->ActiveExclusionShell = nullptr;
            }
        }

        // With none, ask the manager straight into +0x24/+0x28 (its bool is ignored); a zone
        // found there looks its shell up in the +0x38 map.
        if (*ExclSlot == nullptr && Nebula->ExclusionMgr != nullptr)
        {
            Nebula->ExclusionMgr->is_in_exclusion_zone(
                CamPos, ExclSlot, &Nebula->ExclusionImmersion, true);
            if (*ExclSlot != nullptr)
            {
                unsigned const Key = (*ExclSlot)->ZoneShellName.Id;
                FNebulaZoneShellMap& Shells = NebulaZoneShells(Nebula);
                FNebulaZoneShellMap::iterator const Found = NebulaFindZoneShell(Shells, Key);
                if (Found != Shells.end())
                {
                    Nebula->bHasExclusionShell = 1;
                    Nebula->ActiveExclusionShell = Found->second;
                }
            }
        }

        // Inside a zone whose fog_far is beyond the near value, the far distance blends toward
        // it by the immersion.
        Exclusion::NebulaExclusionZone* const Excl = *ExclSlot;
        if (Excl != nullptr && Nebula->ExclusionImmersion > 0.0f)
        {
            float const FogFar = Excl->FogFar;
            if (FogFar > Nebula->FogStart)
            {
                float const F = Nebula->ExclusionImmersion;
                Nebula->FogEnd = static_cast<float>(
                    (1.0f - static_cast<double>(F)) * Nebula->FogEnd +
                    static_cast<double>(FogFar) * F);
            }
        }
    }
    else
    {
        // Outside, the fog range follows the depth; an enabled ambient light is switched off
        // (GameLight vt+0x08, FUN_005376f0).
        double const Near = static_cast<double>(Depth) + Nebula->FogNear;
        Nebula->FogStart = static_cast<float>(Near);
        Nebula->FogEnd = static_cast<float>(Near + Nebula->FogDistance);
        FGameLight* const Light = Nebula->DynamicLight;
        if (Light != nullptr && (Light->Flags & kLightFlagEnabled) != 0)
            GameLightVirtualDisable(Light);
    }

    // Within 300 of the surface (>= and unordered skip).
    if (Depth < 300.0)
    {
        FNebulaCloudsListNode* const CloudSent = Nebula->Clouds.Sentinel;
        for (FNebulaCloudsListNode* N = CloudSent->Next; N != CloudSent; N = N->Next)
            N->Clouds->Update(Camera, Dt, Nebula->InsideFade, Nebula->ExclusionMgr);

    }

    // Fully inside (< and unordered skip).
    if (Nebula->InsideFade >= 1.0f)
    {
        Vector const CamCopy = {CamPos.x, CamPos.y, CamPos.z};
        NebulaAmbientTick(Nebula, &CamCopy);
    }

    Nebula->ExteriorOrientTimer = Dt + Nebula->ExteriorOrientTimer;
    Nebula->ExclusionSortTimer = Dt + Nebula->ExclusionSortTimer;
    Nebula->ExteriorSortTimer = Dt + Nebula->ExteriorSortTimer;
}

namespace
{
    // FUN_00507e70: fast inverse square root, one Newton step, returned on the FPU.
    // FUN_005074e0 also inlines the same sequence twice.
    double NebulaInvSqrt(float X)
    {
        int XBits = 0;
        memcpy(&XBits, &X, sizeof(XBits));
        int const YBits = 0x5f3759df - (XBits >> 1);
        float Y = 0.0f;
        memcpy(&Y, &YBits, sizeof(Y));
        return (1.5f - static_cast<double>(Y) * Y * X * 0.5f) * Y;
    }

    // FUN_00507ea0 (thiscall on the vector).
    void NebulaScaleVec3(float* V, float S)
    {
        V[0] = S * V[0];
        V[1] = S * V[1];
        V[2] = S * V[2];
    }

    // The FUN_005074e0 fog-sheet edge: normalize, then scale to the zone's reach * 1.15f
    // (Vec3Length is FUN_00410190); both factors on the FPU.
    void NebulaSheetEdge(Universe::IZone* Zone, Vector* Edge)
    {
        float const LenSq = static_cast<float>(
            (static_cast<double>(Edge->x) * Edge->x + static_cast<double>(Edge->z) * Edge->z) +
            static_cast<double>(Edge->y) * Edge->y);
        double const Inv = NebulaInvSqrt(LenSq);
        Edge->x = static_cast<float>(Edge->x * Inv);
        Edge->y = static_cast<float>(Edge->y * Inv);
        Edge->z = static_cast<float>(Edge->z * Inv);
        Vector Dir;
        Dir.x = Edge->x;
        Dir.y = Edge->y;
        Dir.z = Edge->z;
        float const Reach = Zone->GetRadiusAlong(Dir) * 1.15f;
        double const Scale = Reach / Vec3Length(Edge);
        Edge->x = static_cast<float>(Edge->x * Scale);
        Edge->y = static_cast<float>(Edge->y * Scale);
        Edge->z = static_cast<float>(Edge->z * Scale);
    }
}

void FNebulaEffect::DrawFillSheets(float CamX, float CamY, float CamZ)
{
    // FUN_005074e0: two crossing fog sheets through the zone, textured with the fill shape
    // (+0x5c). Only +0x5c is tested; the arguments are the camera position.

    FTexShape* const Shape = FillShape;
    if (Shape == nullptr)
        return;

    Universe::IZone* const Zone = this->Zone;
    Vector const Size = Zone->GetSize();
    Vector const ZonePos = Zone->GetPos();
    Matrix const Orient = Zone->GetOrientation();
    float Col0[3] = {Orient.E[0][0], Orient.E[1][0], Orient.E[2][0]};
    float Col1[3] = {Orient.E[0][1], Orient.E[1][1], Orient.E[2][1]};
    float Col2[3] = {Orient.E[0][2], Orient.E[1][2], Orient.E[2][2]};

    // The longer of columns 0 / 2 is axis A; the other is B, used only for the second sheet's
    // fade.
    float AxisA[3];
    float AxisB[3];
    float ExtA;
    if (Size.x > Size.z)
    {
        AxisA[0] = Col0[0];
        AxisA[1] = Col0[1];
        AxisA[2] = Col0[2];
        ExtA = Size.x;
        AxisB[0] = Col2[0];
        AxisB[1] = Col2[1];
        AxisB[2] = Col2[2];
    }
    else
    {
        AxisA[0] = Col2[0];
        AxisA[1] = Col2[1];
        AxisA[2] = Col2[2];
        ExtA = Size.z;
        AxisB[0] = Col0[0];
        AxisB[1] = Col0[1];
        AxisB[2] = Col0[2];
    }

    float Look[3];
    Look[0] = CamX - ZonePos.x;
    Look[1] = CamY - ZonePos.y;
    Look[2] = CamZ - ZonePos.z;
    float const LookLenSq = static_cast<float>(
        (static_cast<double>(Look[1]) * Look[1] + static_cast<double>(Look[0]) * Look[0]) +
        static_cast<double>(Look[2]) * Look[2]);
    double const LookInv = NebulaInvSqrt(LookLenSq);
    Look[0] = static_cast<float>(Look[0] * LookInv);
    Look[1] = static_cast<float>(Look[1] * LookInv);
    Look[2] = static_cast<float>(Look[2] * LookInv);

    // The upper clamp tests the unrounded value, the lower one the stored float.
    double const DotA = fabs((static_cast<double>(AxisA[1]) * Look[1] +
                              static_cast<double>(AxisA[0]) * Look[0]) +
                             static_cast<double>(AxisA[2]) * Look[2]);
    double const FaceARaw = ((1.0 - DotA) - static_cast<double>(0.0001f)) * 204.08163f;
    float FaceA = static_cast<float>(FaceARaw);
    if (FaceARaw > 1.0)
        FaceA = 1.0f;
    else if (FaceA < 0.0)
        FaceA = 0.0f;

    Vector EdgeA;
    EdgeA.x = static_cast<float>(static_cast<double>(AxisA[2]) * Look[1] -
                                  static_cast<double>(Look[2]) * AxisA[1]);
    EdgeA.y = static_cast<float>(static_cast<double>(Look[2]) * AxisA[0] -
                                  static_cast<double>(AxisA[2]) * Look[0]);
    EdgeA.z = static_cast<float>(static_cast<double>(AxisA[1]) * Look[0] -
                                  static_cast<double>(Look[1]) * AxisA[0]);
    NebulaSheetEdge(Zone, &EdgeA);

    double const AxisScale = static_cast<double>(ExtA) * 1.15f;
    AxisA[0] = static_cast<float>(AxisA[0] * AxisScale);
    AxisA[1] = static_cast<float>(AxisA[1] * AxisScale);
    AxisA[2] = static_cast<float>(AxisA[2] * AxisScale);

    // FaceB from column 1, the same two clamps.
    double const DotY = fabs((static_cast<double>(Col1[1]) * Look[1] +
                              static_cast<double>(Col1[0]) * Look[0]) +
                             static_cast<double>(Col1[2]) * Look[2]);
    double const FaceBRaw = ((1.0 - DotY) - static_cast<double>(0.2f)) * 5.0f;
    float FaceB = static_cast<float>(FaceBRaw);
    if (FaceBRaw > 1.0)
        FaceB = 1.0f;
    else if (FaceB < 0.0)
        FaceB = 0.0f;
    // Refined by axis B; the unrounded value is clamped at 0.
    if (FaceB > 0.0)
    {
        double const DotB = fabs((static_cast<double>(AxisB[1]) * Look[1] +
                                  static_cast<double>(AxisB[0]) * Look[0]) +
                                 static_cast<double>(AxisB[2]) * Look[2]);
        double const Refined = ((1.0 - DotB) - static_cast<double>(0.2f)) * FaceB * 5.0f;
        FaceB = static_cast<float>(Refined);
        if (Refined < 0.0)
            FaceB = 0.0f;
    }

    Vector EdgeB;
    EdgeB.x = static_cast<float>(static_cast<double>(Col1[2]) * Look[1] -
                                  static_cast<double>(Col1[1]) * Look[2]);
    EdgeB.y = static_cast<float>(static_cast<double>(Look[2]) * Col1[0] -
                                  static_cast<double>(Col1[2]) * Look[0]);
    EdgeB.z = static_cast<float>(static_cast<double>(Col1[1]) * Look[0] -
                                  static_cast<double>(Look[1]) * Col1[0]);
    NebulaSheetEdge(Zone, &EdgeB);
    NebulaScaleVec3(Col1, Size.y * 1.15f);

    Win3DDisable(kWin3DLightingCap);
    Win3DDisable(kWin3DCullFace);
    Win3DSetDepthWrite(0);
    Win3DDisable(kWin3DDepthTest);
    Win3DEnable(kWin3DBlend);
    Win3DBindTexture(Shape->GetGpuHandle(0));
    Win3DSetBlendFunc(kWin3DBlendSrcAlpha, kWin3DBlendInvSrcAlpha);
    Win3DBegin(kWin3DQuads);

    float const* const Fog = FogColor;
    float V[3];
    float T[3];

    // Sheet A.
    if (FaceA > 0.0)
    {
        Win3DColor4f(Fog[0], Fog[1], Fog[2], FaceA);
        Win3DTexCoord2f(Shape->U0, Shape->V0);
        T[0] = ZonePos.x + EdgeA.x;
        T[1] = ZonePos.y + EdgeA.y;
        T[2] = ZonePos.z + EdgeA.z;
        Win3DVertex3f(T[0] - AxisA[0], T[1] - AxisA[1], T[2] - AxisA[2]);
        Win3DTexCoord2f(Shape->U1, Shape->V0);
        Win3DVertex3f(T[0] + AxisA[0], T[1] + AxisA[1], T[2] + AxisA[2]);
        Win3DTexCoord2f(Shape->U1, Shape->V1);
        T[0] = ZonePos.x - EdgeA.x;
        T[1] = ZonePos.y - EdgeA.y;
        T[2] = ZonePos.z - EdgeA.z;
        Win3DVertex3f(T[0] + AxisA[0], T[1] + AxisA[1], T[2] + AxisA[2]);
        Win3DTexCoord2f(Shape->U0, Shape->V1);
        Win3DVertex3f(T[0] - AxisA[0], T[1] - AxisA[1], T[2] - AxisA[2]);
    }

    // Sheet B. The last corner keeps x and y on the FPU across both subtractions; its z is
    // rounded in between.
    if (FaceB > 0.0)
    {
        Win3DColor4f(Fog[0], Fog[1], Fog[2], FaceB);
        Win3DTexCoord2f(Shape->U0, Shape->V0);
        T[0] = ZonePos.x + EdgeB.x;
        T[1] = EdgeB.y + ZonePos.y;
        T[2] = EdgeB.z + ZonePos.z;
        Win3DVertex3f(T[0] - Col1[0], T[1] - Col1[1], T[2] - Col1[2]);
        Win3DTexCoord2f(Shape->U1, Shape->V0);
        Win3DVertex3f(T[0] + Col1[0], T[1] + Col1[1], T[2] + Col1[2]);
        Win3DTexCoord2f(Shape->U1, Shape->V1);
        T[0] = ZonePos.x - EdgeB.x;
        T[1] = ZonePos.y - EdgeB.y;
        T[2] = ZonePos.z - EdgeB.z;
        V[0] = T[0] + Col1[0];
        V[1] = T[1] + Col1[1];
        V[2] = T[2] + Col1[2];
        Win3DVertex3f(V[0], V[1], V[2]);
        Win3DTexCoord2f(Shape->U0, Shape->V1);
        V[0] = static_cast<float>(
            (static_cast<double>(ZonePos.x) - EdgeB.x) - static_cast<double>(Col1[0]));
        V[1] = static_cast<float>(
            (static_cast<double>(ZonePos.y) - EdgeB.y) - static_cast<double>(Col1[1]));
        V[2] = (ZonePos.z - EdgeB.z) - Col1[2];
        Win3DVertex3f(V[0], V[1], V[2]);
    }

    // Lighting is left off.
    Win3DEnd();
    Win3DEnable(kWin3DDepthTest);
    Win3DSetDepthWrite(1);
    Win3DDisable(kWin3DBlend);
    Win3DEnable(kWin3DCullFace);
}

void FNebulaEffect::DrawExteriorBits(FSpaceCamera* Camera)
{
    // FUN_00507ed0 — the exterior bit billboards. Nothing once +0x294 is not below 1.0
    // (unordered also exits).
    if (ExteriorBitCount <= 0)
        return;
    if (!(InsideFade < 1.0))
        return;

    ICamera* const Cam = Camera;
    Transform View;
    memcpy(&View, Cam->GetOrientation(), sizeof(View));
    Vector const CamPos = { View.Translation.x, View.Translation.y, View.Translation.z };
    Vector const ZonePos = Zone->GetPos();

    // Zone sphere in view space; a ClassifySphere result below 3 draws nothing.
    {
        Vector ZoneWorld;
        ZoneWorld.x = ZonePos.x;
        ZoneWorld.y = ZonePos.y;
        ZoneWorld.z = ZonePos.z;
        Vector ZoneView = {};
        AcquireNebulaMath()->InverseTransform(ZoneView, View, ZoneWorld);
        float const Center[3] = { ZoneView.x, ZoneView.y, ZoneView.z };
        float const ZoneRadius = Zone->GetRadius();
        if (static_cast<unsigned short>(Cam->ClassifySphere(reinterpret_cast<const Vector*>(Center), ZoneRadius)) < kSphereStraddling)
            return;
    }

    // Back-to-front bubble sort while +0x54 is above 1.15f, by squared distance to the camera.
    // A pair swaps only when the later one is farther by more than 10. The previous distance
    // is cached only right after a swap -- the swapped-down record keeps its value; a pair left
    // alone inside the 10 band keeps the flag as it was (retail quirk, kept). +0x54 is zeroed
    // after the sort, including when there is only one bit.
    if (ExteriorSortTimer > 1.15f)
    {
        int Swapped = 0;
        do
        {
            Swapped = 0;
            if (ExteriorBitCount <= 1)
                break;
            bool bPrevCached = false;
            double PrevDistSq = 0.0;
            for (int I = 1; I < ExteriorBitCount; ++I)
            {
                FNebulaExteriorBit* const Prev = &ExteriorBits[I - 1];
                FNebulaExteriorBit* const Cur = &ExteriorBits[I];
                if (!bPrevCached)
                {
                    float const Dx = (ZonePos.x + Prev->Offset[0]) - CamPos.x;
                    float const Dy = (ZonePos.y + Prev->Offset[1]) - CamPos.y;
                    float const Dz = (ZonePos.z + Prev->Offset[2]) - CamPos.z;
                    PrevDistSq = static_cast<double>(Dx) * Dx + static_cast<double>(Dy) * Dy +
                                 static_cast<double>(Dz) * Dz;
                }
                float const Cx = (ZonePos.x + Cur->Offset[0]) - CamPos.x;
                float const Cy = (ZonePos.y + Cur->Offset[1]) - CamPos.y;
                float const Cz = (ZonePos.z + Cur->Offset[2]) - CamPos.z;
                double const CurDistSq = static_cast<double>(Cx) * Cx +
                                         static_cast<double>(Cy) * Cy +
                                         static_cast<double>(Cz) * Cz;
                if (!(PrevDistSq < CurDistSq))
                {
                    bPrevCached = false;
                    continue;
                }
                if (!(fabs(PrevDistSq - CurDistSq) > 10.0))
                    continue;
                ++Swapped;
                FNebulaExteriorBit const Tmp = *Prev;
                *Prev = *Cur;
                *Cur = Tmp;

                bPrevCached = true;
            }
        } while (Swapped > 0);
        ExteriorSortTimer = 0.0f;
    }

    Win3DDisable(kWin3DLightingCap);
    Win3DDisable(kWin3DCullFace);
    Win3DSetDepthWrite(0);
    Win3DDisable(kWin3DDepthTest);
    Win3DEnable(kWin3DBlend);
    Win3DApplyCamera(Cam);
    // The first shape (+0x60) is bound for every bit.
    FTexShape* const BoundShape = ExteriorShapes[0];
    Win3DBindTexture(BoundShape->GetGpuHandle(0));
    // The shape's alpha cache (+0x04) is probed while negative (FUN_00552420 inline): format
    // ids 10..14 have alpha, any other id takes its descriptor's +0x48 byte.
    if (BoundShape->AlphaCache < 0)
    {
        int Format = PixelFormatFromD3D(0);
        DALib::TextureLib->GetGroupPixelFormat(BoundShape->Texture, nullptr, &Format);
        unsigned char bHasAlpha = 1;
        if (Format < 10 || Format > 14)
            bHasAlpha = static_cast<unsigned char>(PixelFormatGetDesc(Format)->HasAlpha & 0xffu);
        BoundShape->AlphaCache = bHasAlpha;
    }
    // Also picks the colour call per bit below.
    bool const bHasAlpha = BoundShape->AlphaCache != 0;
    if (bHasAlpha)
        Win3DSetBlendFunc(kWin3DBlendSrcAlpha, kWin3DBlendInvSrcAlpha);
    else
        Win3DSetBlendFunc(kWin3DBlendOne, kWin3DBlendOne);

    // No null test on the Pipeline.
    int SavedRs18 = 0;
    DALib::Pipeline->GetRenderState(D3DRS_ALPHAREF, &SavedRs18);
    DALib::Pipeline->SetRenderState(D3DRS_ALPHAREF, 4);

    // Base alpha +0xb8, faded out over the second half of +0x294.
    float const Fade = InsideFade;
    float BaseAlpha = BitAlpha;
    if (Fade > 0.5f)
        BaseAlpha = static_cast<float>((1.0 - ((static_cast<double>(Fade) - 0.5) +
                                               (static_cast<double>(Fade) - 0.5))) * BaseAlpha);

    // Edge fade: a +0x294 above zero (or unordered) gives 0; only a non-positive one gets the
    // span ramp (below 7000 -> 0, from 10000 up -> 1, linear in between).
    float EdgeFade = 1.0f;
    if (!(Fade <= 0.0f))
        EdgeFade = 0.0f;
    else
    {
        float const Span = FogStart - FogNear;
        if (Span < 7000.0f)
            EdgeFade = 0.0f;
        else if (Span < 10000.0f)
            EdgeFade = static_cast<float>(1.0 - (10000.0f - Span) * 0.00033333333f);
    }

    Win3DBegin(kWin3DQuads);

    float ToCam[3] = { CamPos.x - ZonePos.x, CamPos.y - ZonePos.y, CamPos.z - ZonePos.z };
    {
        float const InvLen =
            1.0f / sqrtf(ToCam[0] * ToCam[0] + ToCam[2] * ToCam[2] + ToCam[1] * ToCam[1]);
        ToCam[0] *= InvLen;
        ToCam[1] *= InvLen;
        ToCam[2] *= InvLen;
    }

    // The count is re-read per iteration.
    FNebulaExteriorBit* Bit = ExteriorBits;
    for (int I = 0; I < ExteriorBitCount; ++I, ++Bit)
    {
        float A = BaseAlpha;

        float const P[3] = {ZonePos.x + Bit->Offset[0], ZonePos.y + Bit->Offset[1],
                            ZonePos.z + Bit->Offset[2]};
        float const V[3] = { P[0] - CamPos.x, P[1] - CamPos.y, P[2] - CamPos.z };

        // A point on or behind the eye plane is skipped.
        Vector PWorld;
        PWorld.x = P[0];
        PWorld.y = P[1];
        PWorld.z = P[2];
        Vector PView = {};
        AcquireNebulaMath()->InverseTransform(PView, View, PWorld);
        float const Depth = -PView.z;
        if (Depth <= 0.0f)
            continue;

        // Facing fade. Dot uses the bit's offset from the zone centre, not its axis.

        int const Mode = bCullBackBits;
        if (Mode != 0 || EdgeFade < 1.0f)
        {
            float const Sx = Bit->Offset[0];
            float const Sy = Bit->Offset[1];
            float const Sz = Bit->Offset[2];
            float const Inv = 1.0f / sqrtf(Sx * Sx + Sz * Sz + Sy * Sy);
            float const Dot = ToCam[0] * (Sx * Inv) + ToCam[2] * (Sz * Inv) + ToCam[1] * (Sy * Inv);
            if (Mode == 0)
            {
                if (Dot < 0.1f)
                    A = EdgeFade * BaseAlpha;
            }
            else
            {
                if (Dot < 0.0f)
                    continue;
                if (Dot < 0.05)
                    A = static_cast<float>(Dot * BaseAlpha * 20.0);
            }
        }

        // Q = V x axis and H = Q x V, each scaled to the radius.
        float const R = Bit->Radius;
        float const* const Axis = Bit->Axis;
        float Qx = V[1] * Axis[2] - V[2] * Axis[1];
        float Qy = V[2] * Axis[0] - V[0] * Axis[2];
        float Qz = V[0] * Axis[1] - V[1] * Axis[0];
        float const QScale = R / sqrtf(Qx * Qx + Qz * Qz + Qy * Qy);
        Qx *= QScale;
        Qy *= QScale;
        Qz *= QScale;
        float Hx = Qy * V[2] - Qz * V[1];
        float Hy = Qz * V[0] - V[2] * Qx;
        float Hz = V[1] * Qx - Qy * V[0];
        float const HScale = R / sqrtf(Hz * Hz + Hy * Hy + Hx * Hx);
        Hx *= HScale;
        Hy *= HScale;
        Hz *= HScale;

        if (Depth < R)
            A = (Depth / R) * A;
        if (A < 0.01)
            continue;

        // The additive path premultiplies the colour.
        float const Cr = ExteriorColor[0];
        float const Cg = ExteriorColor[1];
        float const Cb = ExteriorColor[2];
        if (bHasAlpha)
            Win3DColor4f(Cr, Cg, Cb, A);
        else
            Win3DColor3f(Cr * A, Cg * A, Cb * A);

        FTexShape const* const Shape = Bit->Shape;

        Win3DTexCoord2f(Shape->U0, Shape->V0);
        Win3DVertex3f((P[0] + Qx) + Hx, (P[1] + Qy) + Hy, (P[2] + Qz) + Hz);
        Win3DTexCoord2f(Shape->U1, Shape->V0);
        Win3DVertex3f((P[0] + Qx) - Hx, (P[1] + Qy) - Hy, (P[2] + Qz) - Hz);
        Win3DTexCoord2f(Shape->U1, Shape->V1);
        Win3DVertex3f((P[0] - Qx) - Hx, (P[1] - Qy) - Hy, (P[2] - Qz) - Hz);
        Win3DTexCoord2f(Shape->U0, Shape->V1);
        Win3DVertex3f((P[0] - Qx) + Hx, (P[1] - Qy) + Hy, (P[2] - Qz) + Hz);
    }

    Win3DEnd();
    DALib::Pipeline->SetRenderState(D3DRS_ALPHAREF, SavedRs18);
    Win3DEnable(kWin3DDepthTest);
    Win3DSetDepthWrite(1);
    Win3DDisable(kWin3DBlend);
    Win3DEnable(kWin3DCullFace);

    // The effect at +0xec (the companion's, released by Shutdown).
    FEffectBase* const Effect = Companion.Effect;
    if (Effect != nullptr)
    {
        Effect->SetIntensity(static_cast<float>(1.0 - InsideFade));
    }
}


void FNebulaEffect::Shutdown()
{
    FNebulaEffect* const Nebula = this;
    // FUN_00506cc0
    if (Nebula->FlagLoaded == 0)
        return;

    Nebula->ExteriorShapeCount = 0;

    if (Nebula->ExteriorBits != nullptr)
        operator delete(Nebula->ExteriorBits);

    Nebula->ExteriorBits = nullptr;
    Nebula->ExteriorBitCount = 0;

    // Every Clouds is destroyed, then the list is cleared (FUN_00535980).
    FNebulaCloudsListNode* const Sentinel = Nebula->Clouds.Sentinel;
    FNebulaCloudsListNode* N = Sentinel->Next;
    while (N != Sentinel)
    {
        if (N->Clouds != nullptr)
            LocalNebulaCloudsDestroy(N->Clouds, 1);
        N = N->Next;
    }
    N = Sentinel->Next;
    while (N != Sentinel)
    {
        FNebulaCloudsListNode* const Next = N->Next;
        N->Prev->Next = N->Next;
        N->Next->Prev = N->Prev;
        operator delete(N);
        --Nebula->Clouds.Count;
        N = Next;
    }

    if (Nebula->ExclusionMgr != nullptr)
    {
        delete Nebula->ExclusionMgr;
        Nebula->ExclusionMgr = nullptr;
    }

    {
        FNebulaZoneShellMap& Shells = NebulaZoneShells(Nebula);
        for (FNebulaZoneShellMap::iterator It = Shells.begin(); It != Shells.end(); ++It)
        {
            FNebulaZoneShell* const Shell = It->second;
            if (Shell != nullptr)
            {
                NebulaZoneShellDestroy(Shell);
                delete Shell;
            }
        }
        Shells.clear();
    }

    LightningDisable(&Nebula->Lightning);

    // The effect / sound / light companion at +0xe4 is stopped (FUN_004fcef0 prelude inlined)
    // and released (FUN_00502d90 inlined). Nothing in Nebula.cpp ever fills it.
    CliEffectCompanionArmStop(&Nebula->Companion);
    CliEffectCompanionClear(&Nebula->Companion);

    // +0x270 is NOT cleared -- only a later Load with a positive period rewrites it.
    FGameLight* const Ambient = Nebula->DynamicLight;
    if (Ambient != nullptr)
    {
        if ((Ambient->Flags & kLightFlagEnabled) != 0)
            GameLightVirtualDisable(Ambient);
        Nebula->DynamicLight->Release();
    }

    NebulaShapes(Nebula)->Clear();
    Nebula->Zone = nullptr;
    Nebula->FlagLoaded = 0;
}

FNebulaEffect::~FNebulaEffect()
{
    FNebulaEffect* const Nebula = this;
    // FUN_00504cf0
    Nebula->FNebulaEffect::Shutdown();


    // ~lightning (+0x100) is FUN_005526d0.
    LightningDisable(&Nebula->Lightning);


    // Clouds, companion, zone-shell map and ShapeDB unwind in reverse member order.
}

void NebulaDestruct(FNebulaEffect* Nebula)
{
    Nebula->FNebulaEffect::~FNebulaEffect();
}
