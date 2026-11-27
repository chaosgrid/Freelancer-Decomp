// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail Client/Space/JumpEffect.cpp — Nebula renderable (operator_new 0x2d0).
// See docs/re/freelancer.md (Space section).
//
// Layout notes (trees):
//   +0x04  VC6 tree nodes 0x5c (FUN_00509c30) — root@+0x08, head@+0x0c, count@+0x14
//   +0x38  zone_shell map (FUN_004e7f60, VC6 nodes 0x18) — a real std::map in the port
//   +0x270 GameLight* ambient (FUN_00537a90), driven by FUN_005097c0

#include "common_net.h"
#include "common_universe.h"
#include "Common/Graphics/texture_shapes.h"

#include <cstddef>
#include <map>

struct FTexShape;
struct FGameLight; // engine_light.h

// One puff of a [Clouds] block (operator new 0x58, FUN_00550b60). The lightning fields run
// while the clouds' lightning_duration is positive (FUN_00550d40).
struct FNebulaCloudPuff
{
    Vector Position;          // +0x00
    float Color[3];           // +0x0c lerp of puff_colora / puff_colorb
    float Size;               // +0x18 puff_radius * detail * [0.8, 1.2)
    int ShapeIndex;           // +0x1c into PuffShapes, by puff_weights
    float LightningAnchor[3]; // +0x20 position when the timer crossed zero
    float LightningDir[3];    // +0x2c random in-plane direction, length 5
    float LightningTimer;     // +0x38 negative countdown (lightning_gap), then up to the duration
    float LightningFlash;     // +0x3c per-frame flash intensity
    float LightningPhase;     // +0x40
    float Velocity[3];        // +0x44 random within puff_drift
    unsigned char bActive;    // +0x50
    unsigned char bHidden;    // +0x51
    unsigned char Pad52[2];
    float ViewDepth;          // +0x54 sort key, written by the clouds draw
};
static_assert(sizeof(FNebulaCloudPuff) == 0x58, "cloud puff 0x58");
static_assert(offsetof(FNebulaCloudPuff, LightningTimer) == 0x38, "puff timer +0x38");
static_assert(offsetof(FNebulaCloudPuff, bActive) == 0x50, "puff active +0x50");

// FUN_00550180 — Clouds subobject (SpaceTerrainRenderComp), sizeof 0x12c; keys from the
// [Clouds] parse FUN_00550240. Also allocated by TexturePanels::Load for nested [Clouds] /
// Exterior panels.
struct FNebulaClouds
{
    FNebulaCloudPuff** PuffTable;              // +0x00
    int PuffCountIni;                          // +0x04 puff_count
    float DetailCountScale;                    // +0x08 from the detail level (FUN_00552530)
    int PuffCount;                             // +0x0c table size, PuffCountIni * DetailCountScale
    int PuffBudget;                            // +0x10 puffs allowed active at this visibility
    int ActiveCount;                           // +0x14
    FTexShape* LightningShapes[4];             // +0x18 lightning_shape
    FTexShape* PuffShapes[8];                  // +0x28 puff_shape
    float PuffWeights[8];                      // +0x48 puff_weights, normalised
    int PuffShapeCount;                        // +0x68
    Vector Up;                                 // +0x6c billboard up, re-squared to the camera per draw
    Vector PuffColorA;                         // +0x78 puff_colora
    Vector PuffColorB;                         // +0x84 puff_colorb
    float LightningColor[3];                   // +0x90 lightning_color
    float LightningGap;                        // +0x9c lightning_gap
    float LightningDuration;                   // +0xa0 lightning_duration
    float LightningSize;                       // +0xa4 lightning_size
    float PuffRadius;                          // +0xa8 puff_radius
    float DetailRadiusScale;                   // +0xac from the detail level (FUN_00552530)
    float PuffDrift;                           // +0xb0 puff_drift
    float PuffMaxAlpha;                        // +0xb4 puff_max_alpha
    float CentreAhead;                         // +0xb8 NearFadeReach + CloudRadius
    float NearFadeReach;                       // +0xbc
    float MaxDistance;                         // +0xc0 max_distance
    Universe::IZoneImmersion const* Owner;     // +0xc4 the nebula zone or the TexturePanels
    float LLightRange;                         // +0xc8 llight_range
    float LLightColor[3];                      // +0xcc llight_color
    float Unknown_0xd8;                        // +0xd8 never read or written
    float LightningIntensity;                  // +0xdc lightning_intensity
    float Fade;                                // +0xe0 visibility / FullVisibility, clamped to [0, 1]
    float NearFadeStart;                       // +0xe4 near_fade_distance[0]
    float NearFadeEnd;                         // +0xe8 near_fade_distance[1]
    unsigned char bLoaded;                     // +0xec set once the [Clouds] block is parsed
    unsigned char PadED[3];
    int Unknown_0xf0;                          // +0xf0 zeroed by the parse, never read
    float Centre[3];                           // +0xf4 camera position + CentreAhead along -z
    float CloudRadius;                         // +0x100 max_distance / 2 + puff_radius
    float FullVisibility;                      // +0x104 visibility at full fade
    float BudgetVisibility;                    // +0x108 visibility at the full puff budget
    float AcceptVisibility;                    // +0x10c from here up a respawn skips the zone test
    int NoCloudBlend;                          // +0x110 no_cloud_blend
    int ZWriteClouds;                          // +0x114 zwrite_clouds
    int DisableClouds;                         // +0x118 disable_clouds
    int DisableWisps;                          // +0x11c disable_wisps
    int DisableLightningBolts;                 // +0x120 disable_lightning_bolts
    int DisableLightningFlash;                 // +0x124 disable_lightning_flash
    int DisableBatcher;                        // +0x128 disable_batcher

    void Construct();
    void ParseIni(class INI_Reader& Reader, FShapeDB* Shapes);
    void ReleaseBits();
    void RebuildBits();
    void ApplyDetail(float Detail);
    void RegenExcluded(void* ExclusionCtx);

    // FUN_00550d40 (__thiscall, RET 0x10) — per-frame puff update: fade, cull, sort, respawn,
    // drift, move; active count at +0x14. Camera is an ICamera (vt+0x0c / vt+0x10).
    void Update(void* Camera, float Dt, float Visibility,
                Exclusion::NebulaExclusionZoneMgr* ExclusionMgr);
    void Destroy(unsigned char Flags);

    // Defined in cli_nebula.cpp.
    void SetDetail(float Detail);

    // FUN_00551500 — shared Clouds panel draw (this = FNebulaClouds).
    void DrawClouds(void* Camera, void* ExclusionCtx);
};

static_assert(sizeof(FNebulaClouds) == 0x12c, "Clouds component 0x12c");
static_assert(offsetof(FNebulaClouds, Up) == 0x6c, "clouds up +0x6c");
static_assert(offsetof(FNebulaClouds, Owner) == 0xc4, "clouds owner +0xc4");
static_assert(offsetof(FNebulaClouds, bLoaded) == 0xec, "clouds loaded +0xec");
static_assert(offsetof(FNebulaClouds, Centre) == 0xf4, "clouds centre +0xf4");
static_assert(offsetof(FNebulaClouds, DisableBatcher) == 0x128, "clouds disable_batcher +0x128");

// DAT_00679860 -- SpaceTerrainRenderComp.cpp's lazily filled I3DMathEngine (FUN_005b73e0 on
// first use). Readers: FUN_0054c8b0 (band rings, cli_asteroid.cpp), FUN_00550d40, FUN_00551500.
struct I3DMathEngine;
extern I3DMathEngine* G_CloudsMath;

                 // FUN_00550180
 // FUN_00550240
               // FUN_005501e0 / 00550cf0
               // FUN_00550b60 (+ FUN_00550040)
 // FUN_005524a0
 // FUN_005513b0

struct FNebulaCloudsListNode
{
    FNebulaCloudsListNode* Next;
    FNebulaCloudsListNode* Prev;
    FNebulaClouds* Clouds;
};

struct FNebulaZoneShell;
typedef std::map<unsigned, FNebulaZoneShell*> FNebulaZoneShellMap;

struct FNebulaEffectCompanion
{
    FNebulaEffectCompanion();
    ~FNebulaEffectCompanion();
    void* Values[4];
};

struct FNebulaCloudsListStorage
{
    FNebulaCloudsListStorage();
    ~FNebulaCloudsListStorage();
    unsigned Allocator; // Never written, as in retail's list constructor.
    FNebulaCloudsListNode* Sentinel;
    unsigned Count;
};
static_assert(sizeof(FNebulaEffectCompanion) == 0x10, "Nebula companion size");
static_assert(sizeof(FNebulaCloudsListStorage) == 0xc, "Nebula clouds list size");
static_assert(sizeof(FShapeDB) < 0x18, "ShapeDB fits reserved storage");
static_assert(sizeof(FNebulaZoneShellMap) < 0x14, "zone shell map fits reserved storage");

struct INebula
{
    virtual void Load(char const* FilePath, char const* ZoneName) = 0;
    virtual void Shutdown() = 0;
    virtual void Update(void* Camera, float Dt) = 0;
    virtual void DrawFogOverlay(void* Camera) = 0;
    virtual void DrawFogShell(void* Camera) = 0;
    virtual void DrawExteriorBitsPass(void* Camera) = 0;
    virtual void DrawExclusionShell(void* Camera) = 0;
    virtual void DrawCloudsPanels(void* Camera) = 0;
    virtual float ZoneExteriorFactor(float const* CamPos3) = 0;
    virtual float GetInsideFade() = 0;
    virtual float GetSunBurnthroughIntensity() = 0;
    virtual float GetSunBurnthroughScaler() = 0;
    virtual float* CopyFogRgb(float* OutRgb3) = 0;
    virtual float* FillFogRgb(float* OutRgb3) = 0;
    virtual float GetFogStart() = 0;
    virtual float FogFactor(float const* CamPos3) = 0;
    virtual float GetFogEnd() = 0;
    virtual float FogFarClamp(float Unused) = 0;
    virtual unsigned char const* GetAmbientBytes() = 0;
    virtual Universe::IZone* GetZone() = 0;
    virtual float GetZoneCenterRadius(float* OutPos3) = 0;
    virtual void Detail(float DetailLevel) = 0;
    virtual ~INebula() {}
};

// One exterior bit (0x20, the FUN_00506600 buffer): a billboard on the zone surface.
struct FNebulaExteriorBit
{
    float Offset[3];  // +0x00 from the zone centre
    float Radius;     // +0x0c
    FTexShape* Shape; // +0x10 weighted pick of the [Exterior] shapes
    float Axis[3];    // +0x14 billboard axis, re-aimed square to the camera (FUN_00507340)
};
static_assert(sizeof(FNebulaExteriorBit) == 0x20, "exterior bit 0x20");

// One [Lightning] bolt segment (0x1c): two end points and the texture UVs.
struct FNebulaLightningSegment
{
    float A[3];         // +0x00
    float B[3];         // +0x0c
    float const* Uvs;   // +0x18 read at +8 / +0xc / +0x10 / +0x14
};
static_assert(sizeof(FNebulaLightningSegment) == 0x1c, "lightning segment 0x1c");

// The [Lightning] member at nebula +0x100 (FUN_00552570 load, FUN_005526d0 disable,
// FUN_005529b0 draw).
struct FNebulaLightning
{
    float Timer;                           // +0x00 negative countdown, only written by the load
    float Unknown_0x04[4];                 // +0x04 never read or written
    float Intensity;                       // +0x14
    float Unknown_0x18;                    // +0x18 zeroed by the load
    float Color[3];                        // +0x1c color
    float Gap;                             // +0x28 gap
    float Size;                            // +0x2c size
    float Duration;                        // +0x30 duration
    float Unknown_0x34;                    // +0x34 never read or written
    FTextureRef const* Texture;            // +0x38 zeroed by the load
    float Unknown_0x3c[3];                 // +0x3c zeroed by the load
    FNebulaLightningSegment Segments[8];   // +0x48
    int SegmentAge[8];                     // +0x128 a segment draws while below 3
    Universe::IZone* Zone;                 // +0x148 zeroed by the load, then the nebula zone
    unsigned char bActive;                 // +0x14c
    unsigned char Pad14D[3];
};
static_assert(sizeof(FNebulaLightning) == 0x150, "nebula lightning 0x150");
static_assert(offsetof(FNebulaLightning, Segments) == 0x48, "lightning segments +0x48");
static_assert(offsetof(FNebulaLightning, SegmentAge) == 0x128, "lightning ages +0x128");
static_assert(offsetof(FNebulaLightning, bActive) == 0x14c, "lightning active +0x14c");

#pragma pack(push, 1)
struct FNebulaEffect : INebula
{
    FNebulaEffect();
    void Load(char const* FilePath, char const* ZoneName) override;
    void Shutdown() override;
    void Update(void* Camera, float Dt) override;
    void DrawFogOverlay(void* Camera) override;
    void DrawFogShell(void* Camera) override;
    void DrawExteriorBitsPass(void* Camera) override;
    void DrawExclusionShell(void* Camera) override;
    void DrawCloudsPanels(void* Camera) override;
    float ZoneExteriorFactor(float const* CamPos3) override;
    float GetInsideFade() override;
    float GetSunBurnthroughIntensity() override;
    float GetSunBurnthroughScaler() override;
    float* CopyFogRgb(float* OutRgb3) override;
    float* FillFogRgb(float* OutRgb3) override;
    float GetFogStart() override;
    float FogFactor(float const* CamPos3) override;
    float GetFogEnd() override;
    float FogFarClamp(float Unused) override;
    unsigned char const* GetAmbientBytes() override;
    Universe::IZone* GetZone() override;
    float GetZoneCenterRadius(float* OutPos3) override;
    void Detail(float DetailLevel) override;
    ~FNebulaEffect() override;

    FShapeDB Shapes;                                 // +0x04
    unsigned char ShapePadding[0x18 - sizeof(FShapeDB)];
    float FrameDelta;                                // +0x1c
    Universe::IZone* Zone;                            // +0x20
    Exclusion::NebulaExclusionZone* ActiveExclusionZone; // +0x24
    float ExclusionImmersion;                        // +0x28 the camera's immersion in it
    unsigned char bHasExclusionShell;                // +0x2c
    unsigned char Pad2D[3];
    FNebulaZoneShell* ActiveExclusionShell;          // +0x30
    Exclusion::NebulaExclusionZoneMgr* ExclusionMgr;  // +0x34
    FNebulaZoneShellMap ZoneShells;                  // +0x38
    unsigned char MapPadding[0x14 - sizeof(FNebulaZoneShellMap)];
    unsigned char bReachPending;                     // +0x4c set by Load, cleared once Update took the clouds reach
    unsigned char Pad4D[3];
    float ExclusionSortTimer;                        // +0x50
    float ExteriorSortTimer;                         // +0x54
    float ExteriorOrientTimer;                       // +0x58
    FTexShape* FillShape;                            // +0x5c [Exterior] fill_shape
    FTexShape* ExteriorShapes[8];                    // +0x60 [Exterior] shape
    float ExteriorShapeWeights[8];                   // +0x80 [Exterior] shape_weights
    int ExteriorShapeCount;                          // +0xa0
    float BitRadius;                                 // +0xa4 [Exterior] bit_radius
    float ExteriorColor[3];                          // +0xa8 [Exterior] color
    float Opacity;                                   // +0xb4 [Exterior] opacity
    float BitAlpha;                                  // +0xb8 Opacity scaled by the detail level
    int PlaneSlices;                                 // +0xbc [Exterior] plane_slices
    int MinBits;                                     // +0xc0 [Exterior] min_bits
    int MaxBits;                                     // +0xc4 [Exterior] max_bits
    float BitRadiusMin;                              // +0xc8 1 - bit_radius_random_variation
    float BitRadiusMax;                              // +0xcc 1 + bit_radius_random_variation
    float EquatorBias;                               // +0xd0 1 - [Exterior] equator_bias
    float MoveBitPercent;                            // +0xd4 [Exterior] move_bit_percent
    int bCullBackBits;                               // +0xd8 1 from the ctor; skips bits facing away
    int ExteriorBitCount;                            // +0xdc
    FNebulaExteriorBit* ExteriorBits;                // +0xe0
    FNebulaEffectCompanion Companion;                // +0xe4
    FNebulaCloudsListStorage Clouds;                  // +0xf4
    FNebulaLightning Lightning;                      // +0x100 [Lightning]
    float BackLightningGap;                          // +0x250 [BackgroundLightning] gap
    float BackLightningDuration;                     // +0x254 [BackgroundLightning] duration
    float BackLightningTimer;                        // +0x258
    float BackLightningColor[3];                     // +0x25c [BackgroundLightning] color
    float BackLightningIntensity;                    // +0x268 [BackgroundLightning] intensity
    unsigned char bBackLightningFlash;               // +0x26c set while the background flashes
    unsigned char Pad26D[3];
    FGameLight* DynamicLight;                        // +0x270 built when DynLightningDuration > 0
    float DynLightningGap;                           // +0x274 [DynamicLightning] gap
    float DynLightningDuration;                      // +0x278 [DynamicLightning] duration
    float DynLightningTimer;                         // +0x27c
    float DynLightningColor[3];                      // +0x280 [DynamicLightning] color
    float DynAmbientIntensity;                       // +0x28c [DynamicLightning] ambient_intensity
    float DynIntensityIncrease;                      // +0x290 [DynamicLightning] intensity_increase
    float InsideFade;                                // +0x294 0 outside, 1 one fog span inside the zone
    float FogDistance;                                // +0x298 [Fog] distance
    float FogNear;                                    // +0x29c [Fog] near
    float FogColor[3];                               // +0x2a0 [Fog] color
    float FogStart;                                  // +0x2ac current fog start
    float FogEnd;                                    // +0x2b0 current fog end
    float MaxCloudsReach;                            // +0x2b4 largest max_distance / 2 + puff_radius
    unsigned char bFullOverlayDrawn;                 // +0x2b8 the overlay was drawn at full fade
    unsigned char Pad2B9[3];
    float FogFarMin;                                 // +0x2bc floor of FogFarClamp (5000)
    unsigned char FlagLoaded;                         // +0x2c0
    unsigned char Pad2C1[3];
    float SunBurnthroughIntensity;                   // +0x2c4 [NebulaLight] sun_burnthrough_intensity
    float SunBurnthroughScaler;                      // +0x2c8 [NebulaLight] sun_burnthrough_scaler
    unsigned char bHasAmbient;                       // +0x2cc
    unsigned char Ambient[3];                        // +0x2cd [NebulaLight] ambient

    // FUN_00506600
    void Rebuild();

    // FUN_00507340 — re-aim the exterior bits' axes square to the camera
    void OrientExteriorBits(float Tx, float Ty, float Tz);

    // FUN_005074e0 — the two crossing fog sheets textured with the fill shape (+0x5c)
    void DrawFillSheets(float CamX, float CamY, float CamZ);

    // FUN_00507ed0 — the exterior bit billboards (Win3D quads + Pipeline RS 0x18)
    void DrawExteriorBits(void* Camera);
};
#pragma pack(pop)

static_assert(sizeof(FNebulaEffect) == 0x2d0, "Nebula 0x2d0");
static_assert(offsetof(FNebulaEffect, bReachPending) == 0x4c, "bReachPending +0x4c");
static_assert(offsetof(FNebulaEffect, ExteriorBits) == 0xe0, "ExteriorBits +0xe0");
static_assert(offsetof(FNebulaEffect, Lightning) == 0x100, "Lightning +0x100");
static_assert(offsetof(FNebulaEffect, DynamicLight) == 0x270, "DynamicLight +0x270");
static_assert(offsetof(FNebulaEffect, InsideFade) == 0x294, "InsideFade +0x294");
static_assert(offsetof(FNebulaEffect, FogColor) == 0x2a0, "FogColor +0x2a0");
static_assert(offsetof(FNebulaEffect, SunBurnthroughIntensity) == 0x2c4, "burnthrough +0x2c4");
static_assert(offsetof(FNebulaEffect, Ambient) == 0x2cd, "Ambient +0x2cd");
static_assert(offsetof(FNebulaEffect, Shapes) == 0x04, "Shapes +0x04");
static_assert(offsetof(FNebulaEffect, FrameDelta) == 0x1c, "FrameDelta +0x1c");
static_assert(offsetof(FNebulaEffect, Companion) == 0xe4, "Companion +0xe4");
static_assert(offsetof(FNebulaEffect, Clouds) == 0xf4, "Clouds list +0xf4");
static_assert(offsetof(FNebulaEffect, Zone) == 0x20, "Zone +0x20");
static_assert(offsetof(FNebulaEffect, ExclusionMgr) == 0x34, "ExclusionMgr +0x34");
static_assert(offsetof(FNebulaEffect, ZoneShells) == 0x38, "zone_shell map +0x38");
static_assert(offsetof(FNebulaEffect, Clouds) + offsetof(FNebulaCloudsListStorage, Sentinel) == 0xf8, "CloudsList +0xf8");
static_assert(offsetof(FNebulaEffect, FogDistance) == 0x298, "FogDistance +0x298");
static_assert(offsetof(FNebulaEffect, FlagLoaded) == 0x2c0, "FlagLoaded +0x2c0");

FNebulaEffect* NebulaConstruct(FNebulaEffect* This);
FNebulaEffect* NebulaCreate();
void NebulaDestroy(FNebulaEffect* Nebula);

// FUN_00504cf0 — full object destructor body
void NebulaDestruct(FNebulaEffect* Nebula);

// FUN_00509b80 — PerfOptions "Nebula" apply: set global detail, walk StarSys list.
void NebulaApplyGlobalDetail(float Detail);

// FUN_00422440 (cdecl) — fog colour from float RGB (x255, clamped) and the raw float bits of
// FogStart / FogEnd into D3DRS 0x22 / 0x24 / 0x25, fog modes by pipeline state 0xd, FOGENABLE.
void FogApplyColorAndRange(float const* Rgb3, float FogStart, float FogEnd);
