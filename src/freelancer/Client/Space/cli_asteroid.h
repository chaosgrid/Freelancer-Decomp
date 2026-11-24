// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail Client/Space/CliAsteroid.cpp — client asteroid-field object (operator new 0x238),
// registered in DAT_006753c4. Distinct from Archetype::CliAsteroid (ICliObj companion) and from
// CmnAsteroid::CAsteroidField (Common.dll field sim at this+0x04). No vtable at +0
// (bTestPlacement).
//
// Subobjects:
//   +0xFC  FAsteroidBand 0x84 (FUN_0054c580 / [Band] FUN_0054c630)
//   +0x19C FAsteroidBillboardBlock ([AsteroidBillboards] FUN_0054dbe0 / 00550000 / 0054fee0)

#include "common_casteroid.h"
#include "common_ini.h"
#include "common_universe.h"
#include "render/IRenderPipeline.h"  // FRpVertexSrcDesc
#include "Common/Graphics/texture_shapes.h"

#include <cstddef>

struct IMaterial;

// One billboard rock, 0x20 bytes: the array FUN_0054de10 allocates at block +0x0C (operator new
// of RockCount << 5) and FUN_0054e900 / FUN_0054f9f0 walk.
#pragma pack(push, 1)
struct FAsteroidBillboardRock
{
    float Pos[3];                     // +0x00 zeroed by FUN_0054de10, placed by FUN_0054e900
    float HalfSize;                   // +0x0C (rand * (SizeMax - SizeMin) / 32768 + SizeMin) / 2
    float Alpha;                      // +0x10 1.0 at creation, recomputed every draw
    unsigned char bVisible;           // +0x14
    unsigned char bFlipU;             // +0x15 corner-UV / corner-colour index, bit 0
    unsigned char bFlipV;             // +0x16 corner-UV / corner-colour index, bit 1
    unsigned char bSwapAxes;          // +0x17 FUN_0054f9f0: which of right/up spans the base
    unsigned char bNegateApex;        // +0x18 FUN_0054f9f0: apex offset negated
    unsigned char Pad19[3];
    float Phase;                      // +0x1C Index / RockCount, the density-cull threshold
};
#pragma pack(pop)

static_assert(sizeof(FAsteroidBillboardRock) == 0x20, "billboard rock stride 0x20");

// Billboard subobject at FCliAsteroid+0x19C, 0x80 bytes (the parent's bNebulaLookupPending
// follows at +0x21C). Retail source: Client\Space\SpaceTerrainRenderComp.cpp (the FDUMP path at
// .rdata:005e0b08).
#pragma pack(push, 1)
struct FAsteroidBillboardBlock
{
    float SphereRadius;               // +0x00 (+0x19C) ctor 2500; FUN_0054de10: the rock sphere's
                                      //        R = Q*start_dist*tan(35deg) / (1 - tan(35deg))
    float SphereAhead;                // +0x04 (+0x1A0) ctor 4000; FUN_0054de10: R + Q*start_dist,
                                      //        the distance of the rock sphere's centre ahead
    float StartDist;                  // +0x08 (+0x1A4) ctor 1500 / start_dist
    FAsteroidBillboardRock* Rocks;    // +0x0C (+0x1A8) FUN_0054de10 operator new
    int Count;                        // +0x10 (+0x1AC) count
    int RockCount;                    // +0x14 (+0x1B0) ftol(Count * QualityA), ctor 100
    Universe::IZone* ZoneRef;         // +0x18 (+0x1B4)
    float DensityScale;               // +0x1C (+0x1B8) rocks with Phase above it are culled
    FTexShape const* Shape;           // +0x20 (+0x1BC) shape
    float SizeMin;                    // +0x24 (+0x1C0) size[0]
    float SizeMax;                    // +0x28 (+0x1C4) size[1]
    float SphereFadeWidth;            // +0x2C (+0x1C8) ctor 700; FUN_0054de10: R * fade pct, the
                                      //        shell of the sphere the rocks fade out in
    float FadeDistPercent;            // +0x30 (+0x1CC) fade_dist_percent (def 0.3)
    float Up[3];                      // +0x34 (+0x1D0) ctor (0, 1, 0); FUN_0054e900 rewrites it
                                      //        every draw as normalise(right x forward)
    float NearFadeEnd;                // +0x40 (+0x1DC) FUN_0054feb0 max(fade planes): fully drawn
                                      //        from this depth on
    float NearFadeStart;              // +0x44 (+0x1E0) FUN_0054feb0 min(fade planes): hidden
                                      //        below this depth
    int MaxLights;                    // +0x48 (+0x1E4) ctor 4, FUN_0054e130's light cap
    float TintedDiffuse0;             // +0x4C (+0x1E8) FUN_0054fee0 out
    float TintedDiffuse1;
    float TintedDiffuse2;
    float TintedAmbient0;             // +0x58 (+0x1F4)
    float TintedAmbient1;
    float TintedAmbient2;
    float ColorShift0;                // +0x64 (+0x200) color_shift
    float ColorShift1;
    float ColorShift2;
    float AmbientIntensity;           // +0x70 (+0x20C)
    float QualityA;                   // +0x74 (+0x210) FUN_00550000
    float QualityB;                   // +0x78 (+0x214)
    unsigned char bParsed;            // +0x7C (+0x218) FUN_0054dbe0; FUN_0054de10 builds no rocks
                                      //        while it is clear
    unsigned char Pad7D[3];
};
#pragma pack(pop)

static_assert(sizeof(FAsteroidBillboardBlock) == 0x80, "Billboard block 0x80");
static_assert(offsetof(FAsteroidBillboardBlock, Rocks) == 0x0C, "billboard Rocks +0x0C");
static_assert(offsetof(FAsteroidBillboardBlock, Up) == 0x34, "billboard Up +0x34");
static_assert(offsetof(FAsteroidBillboardBlock, MaxLights) == 0x48, "billboard MaxLights +0x48");
static_assert(offsetof(FAsteroidBillboardBlock, TintedDiffuse0) == 0x4C, "billboard diffuse +0x4C");
static_assert(offsetof(FAsteroidBillboardBlock, AmbientIntensity) == 0x70, "billboard amb +0x70");

// One band particle, 0x3c bytes: the ring FUN_0054c8b0 allocates at band +0x3C.
#pragma pack(push, 1)
struct FAsteroidBandParticle
{
    float Pos[3];                     // +0x00 on the ring, in world space
    float HalfHeight;                 // +0x0C 1000 at creation, then the band's halved height
    float Normal[3];                  // +0x10 from the two neighbours
    float ColorFront[3];              // +0x1C FUN_0054cfb0: drawn when bFacing (1.0 at creation)
    float ColorBack[3];               // +0x28 FUN_0054cfb0: drawn otherwise (0.2 at creation)
    float Fade;                       // +0x34 FUN_0054c420 (1.0 at creation)
    unsigned char bFacing;            // +0x38 FUN_0054c420: the camera on the normal's side
    unsigned char Pad39[3];
};
#pragma pack(pop)

static_assert(sizeof(FAsteroidBandParticle) == 0x3c, "band particle stride 0x3c");

// One band piece, 0x18 bytes: the render_parts table FUN_0054c8b0 allocates at band +0x60.
// A piece is one far-draw unit (CliAsteroidDrawFarPiece).
#pragma pack(push, 1)
struct FAsteroidBandPiece
{
    int FirstParticle;                // +0x00
    int EndParticle;                  // +0x04 one past the last; may equal the particle count
    float Center[3];                  // +0x08 the middle particle, or the middle two averaged
    float Radius;                     // +0x14 the larger distance to the two end particles
};
#pragma pack(pop)

static_assert(sizeof(FAsteroidBandPiece) == 0x18, "band piece stride 0x18");

// A [Band] / [ExclusionBand] ring, 0x84 bytes: embedded at FCliAsteroid+0xFC and allocated
// (operator new(0x84)) per exclusion band. FUN_0054c580 initialises it, FUN_0054c630 parses it.
#pragma pack(push, 1)
struct FAsteroidBand
{
    FTexShape const* Shape;           // +0x00 shape
    Universe::IZone* Zone;            // +0x04 the field's zone, or the exclusion band's "zone"
    float AmbientIntensity;           // +0x08 ambient_intensity
    float TintedDiffuse[3];           // +0x0C FUN_0054d970: color_shift * field diffuse
    float TintedAmbient[3];           // +0x18 FUN_0054d970: color_shift * field ambient
    float ColorShift[3];              // +0x24 color_shift
    float AmbientIncrease[3];         // +0x30 FUN_0054dab0: the field's ambient_increase / 255
    FAsteroidBandParticle* Particles; // +0x3C
    float TextureAspect;              // +0x40 texture_aspect * 1.15
    int ParticleCount;                // +0x44
    float Height;                     // +0x48 height; halved by FUN_0054c8b0
    int VertIncrease;                 // +0x4C vert_increase (at least 1)
    int CullMode;                     // +0x50 cull_mode: 1 drops the front pass, 2 the back pass
    float MaxAlpha;                   // +0x54 1.0; an exclusion band's near-field fraction
    int RenderParts;                  // +0x58 render_parts: the piece count
    int FirstPiece;                   // +0x5C the field-wide index of piece 0
    FAsteroidBandPiece* Pieces;       // +0x60
    float FadeDist[4];                // +0x64 fade; multiplied by height in FUN_0054c8b0
    unsigned char bOffsetPercent;     // +0x74 offset_percent (1) or offset_dist (0)
    unsigned char Pad75[3];
    float Offset;                     // +0x78
    unsigned char bLit;               // +0x7C FUN_0054cfb0 has baked the particle colours
    unsigned char Pad7D[3];
    int UpdateCount;                  // +0x80 FUN_0054d410 calls while bLit is clear
};
#pragma pack(pop)

static_assert(sizeof(FAsteroidBand) == 0x84, "band 0x84");
static_assert(offsetof(FAsteroidBand, Particles) == 0x3C, "band Particles +0x3C");
static_assert(offsetof(FAsteroidBand, MaxAlpha) == 0x54, "band MaxAlpha +0x54");
static_assert(offsetof(FAsteroidBand, Pieces) == 0x60, "band Pieces +0x60");
static_assert(offsetof(FAsteroidBand, bLit) == 0x7C, "band bLit +0x7C");
static_assert(offsetof(FAsteroidBand, UpdateCount) == 0x80, "band UpdateCount +0x80");

// FUN_0051feb0 fills the vertex-source descriptor at FCliAsteroid+0x30 (0x70 bytes): the
// published FRpVertexSrcDesc (render/IRenderPipeline.h), flags 2, five streams into the
// merged 0x2C-byte vertices -- position +0 (0xc), normal +0xc (0xc), diffuse +0x18 (4),
// texcoord 0 +0x1c (8), texcoord 1 +0x24 (8) -- FVF 0x252 (XYZ | NORMAL | DIFFUSE | TEX2).
static_assert(sizeof(FRpVertexSrcDesc) == 0x70, "MeshDesc 0x70 at +0x30");

#pragma pack(push, 1)
struct FCliAsteroid
{
    unsigned char bTestPlacement;                      // +0x00  test_asteroid_placement: no near draw
    unsigned char Pad01[3];
    CmnAsteroid::CAsteroidField* CmnField;             // +0x04
    unsigned char CubeMapStorage[0x14];                // +0x08  FCliAsteroidCubeMap in place
    Universe::IZone* Zone;                             // +0x1C
    int CubeSize;                                      // +0x20  cube_size
    int FillDist;                                      // +0x24  fill_dist
    float EmptyCubeFreq;                               // +0x28  empty_cube_frequency
    unsigned char bLoaded;                             // +0x2C  the field ini opened
    unsigned char Pad2D[3];                            // +0x2D
    FRpVertexSrcDesc MeshDesc;                         // +0x30  FUN_0051feb0
    IMaterial* Material;                               // +0xA0 first IMesh material
    void* MeshVerts;                                   // +0xA4  merged stride-0x2C verts
    void* MeshIndices;                                 // +0xA8  ushort indices
    unsigned MeshIndexCount;                           // +0xAC
    unsigned PrimitiveType;                            // +0xB0  IMesh face-group primitive type
    float DiffuseColor0;                               // +0xB4  diffuse_color / tint_field / 255
    float DiffuseColor1;
    float DiffuseColor2;
    float AmbientColor0;                               // +0xC0  ambient_color / tint_field / 255
    float AmbientColor1;
    float AmbientColor2;
    float MaxAlpha;                                    // +0xCC  max_alpha
    unsigned char AmbientIncrease0;                    // +0xD0  ambient_increase
    unsigned char AmbientIncrease1;
    unsigned char AmbientIncrease2;
    unsigned char bContainsFogZone;                    // +0xD3  contains_fog_zone
    unsigned FogNebula;                                // +0xD4  the nebula the lighting pass fogs with
    float ExclusionSortTimer;                          // +0xD8
    Exclusion::AsteroidExclusionZoneMgr* ExclusionMgr; // +0xDC
    unsigned char ShapeDbStorage[0x18];                // +0xE0  the field's ShapeDB (FShapeDB in place)
    unsigned char bHasBand;                            // +0xF8  [Band]
    unsigned char PadF9[3];
    FAsteroidBand Band;                                // +0xFC  FUN_0054c580 / [Band]
    unsigned char bHasExclusionBands;                  // +0x180
    unsigned char Pad181[3];
    unsigned char BandVecAllocator;                    // +0x184
    unsigned char Pad185[3];
    FAsteroidBand** BandBegin;                         // +0x188  [ExclusionBand] vector
    FAsteroidBand** BandEnd;                           // +0x18C
    FAsteroidBand** BandCap;                           // +0x190
    unsigned ExclusionPieceCount;                      // +0x194  the exclusion bands' RenderParts
    unsigned char bHasBillboards;                      // +0x198  [AsteroidBillboards]
    unsigned char Pad199[3];
    FAsteroidBillboardBlock Billboard;                 // +0x19C
    unsigned char bNebulaLookupPending;                // +0x21C  the first update's nebula lookup
    unsigned char Pad21D[3];
    unsigned char DynVecAllocator;                     // +0x220
    unsigned char Pad221[3];
    void** DynBegin;                                   // +0x224
    void** DynEnd;
    void** DynCap;
    float FillDetailScale;                             // +0x230  FUN_00524880: (1 - d) * 0.65 + d
    unsigned char bDrawFarBands;                       // +0x234  1 from the constructor, no writer
    unsigned char Pad235[3];
};
#pragma pack(pop)

static_assert(sizeof(FCliAsteroid) == 0x238, "FCliAsteroid must match retail operator_new(0x238)");
static_assert(offsetof(FCliAsteroid, CmnField) == 0x04, "CmnField +0x04");
static_assert(offsetof(FCliAsteroid, Zone) == 0x1C, "Zone +0x1C");
static_assert(offsetof(FCliAsteroid, CubeSize) == 0x20, "CubeSize +0x20");
static_assert(offsetof(FCliAsteroid, FillDist) == 0x24, "FillDist +0x24");
static_assert(offsetof(FCliAsteroid, EmptyCubeFreq) == 0x28, "EmptyCubeFreq +0x28");
static_assert(offsetof(FCliAsteroid, bLoaded) == 0x2C, "bLoaded +0x2C");
static_assert(offsetof(FCliAsteroid, MeshDesc) == 0x30, "MeshDesc +0x30");
static_assert(offsetof(FCliAsteroid, Material) == 0xA0, "Material +0xA0");
static_assert(offsetof(FCliAsteroid, MeshVerts) == 0xA4, "MeshVerts +0xA4");
static_assert(offsetof(FCliAsteroid, MeshIndices) == 0xA8, "MeshIndices +0xA8");
static_assert(offsetof(FCliAsteroid, MeshIndexCount) == 0xAC, "MeshIndexCount +0xAC");
static_assert(offsetof(FCliAsteroid, DiffuseColor0) == 0xB4, "DiffuseColor +0xB4");
static_assert(offsetof(FCliAsteroid, MaxAlpha) == 0xCC, "MaxAlpha +0xCC");
static_assert(offsetof(FCliAsteroid, FogNebula) == 0xD4, "FogNebula +0xD4");
static_assert(offsetof(FCliAsteroid, bHasBand) == 0xF8, "bHasBand +0xF8");
static_assert(offsetof(FCliAsteroid, Band) == 0xFC, "Band +0xFC");
static_assert(offsetof(FCliAsteroid, bHasExclusionBands) == 0x180, "bHasExclusionBands +0x180");
static_assert(offsetof(FCliAsteroid, BandBegin) == 0x188, "BandBegin +0x188");
static_assert(offsetof(FCliAsteroid, bHasBillboards) == 0x198, "bHasBillboards +0x198");
static_assert(offsetof(FCliAsteroid, Billboard) == 0x19C, "Billboard +0x19C");
static_assert(offsetof(FCliAsteroid, Billboard.StartDist) == 0x1A4, "StartDist +0x1A4");
static_assert(offsetof(FCliAsteroid, Billboard.Count) == 0x1AC, "Count +0x1AC");
static_assert(offsetof(FCliAsteroid, Billboard.ColorShift0) == 0x200, "ColorShift +0x200");
static_assert(offsetof(FCliAsteroid, bNebulaLookupPending) == 0x21C, "bNebulaLookupPending +0x21C");
static_assert(offsetof(FCliAsteroid, DynBegin) == 0x224, "DynBegin +0x224");
static_assert(offsetof(FCliAsteroid, FillDetailScale) == 0x230, "FillDetailScale +0x230");
static_assert(offsetof(FCliAsteroid, bDrawFarBands) == 0x234, "bDrawFarBands +0x234");

class FCliDynamicAsteroidGameobj; // cli_asteroid_dynamic.h
struct IMaterial;                 // render/IMaterial.h

// The dynamic asteroid mesh copy FUN_00525800 builds (operator new(0x10), zeroed): the
// IRenderableMesh vertices (stride 0x2c, FVF 0x252) and ushort indices.
struct FCliDynamicAsteroidMesh
{
    unsigned char* Verts;      // +0x00  operator new(VertCount * 0x2c)
    int VertCount;             // +0x04
    unsigned short* Indices;   // +0x08  operator new(IndexCount * 2)
    int IndexCount;            // +0x0c
};
static_assert(sizeof(FCliDynamicAsteroidMesh) == 0x10, "dynamic asteroid mesh block 0x10");

// Client DynamicAsteroids manager — operator new(0xE8), ctor FUN_005256a0.
// Distinct from Archetype::DynamicAsteroid (0x5C Root) and CDynamicAsteroid (CObject).
// Stored in FCliAsteroid's DynBegin/DynEnd vector (+0x224); its functions are in
// cli_asteroid_dynamic.{h,cpp}.
#pragma pack(push, 1)
struct FCliDynamicAsteroid
{
    Universe::IZone* Zone;                       // +0x00
    CmnAsteroid::CAsteroidField* CmnField;       // +0x04
    Archetype::DynamicAsteroid* Arch;            // +0x08
    FCliDynamicAsteroidGameobj** SlotPtrs;       // +0x0C  SlotCount gameobjs (null = free)
    int CountIni;                                // +0x10  "count" (default 10)
    int SlotCount;                               // +0x14  ftol(CountIni * DetailScale + 0.5)
    float MaxVelocity;                           // +0x18  default 50
    float MaxAngularVelocity;                    // +0x1C  default 5
    float PlacementRadius;                       // +0x20  default 150
    float PlacementRadiusInner;                  // +0x24  default 75 / radius*0.7
    float CameraPos[3];                          // +0x28  FUN_005261b0: the camera's position
    unsigned char bSpawnAllowed;                 // +0x34  FUN_00525c80 (zone, tradelane, exclusion)
    unsigned char bAnyVisible;                   // +0x35  FUN_00525c80: some slot above alpha 0.005
    unsigned char Pad36[2];
    FRpVertexSrcDesc MeshDesc;                   // +0x38  FUN_0051feb0 (same fill as field)
    IMaterial* Material;                         // +0xA8  IMesh slot 14; released (vt+8)
    unsigned PrimitiveType;                      // +0xAC  IMesh slot 13 out
    FCliDynamicAsteroidMesh* Mesh;               // +0xB0  FUN_00525800
    unsigned MeshVertTotal;                      // +0xB4
    unsigned MeshIndexTotal;                     // +0xB8
    float TintedDiffuse0;                        // +0xBC
    float TintedDiffuse1;                        // +0xC0
    float TintedDiffuse2;                        // +0xC4
    float TintedAmbient0;                        // +0xC8
    float TintedAmbient1;                        // +0xCC
    float TintedAmbient2;                        // +0xD0
    float ColorShift0;                           // +0xD4
    float ColorShift1;                           // +0xD8
    float ColorShift2;                           // +0xDC
    float DetailScale;                           // +0xE0
    float InvDetailScale;                        // +0xE4
};
#pragma pack(pop)

static_assert(sizeof(FCliDynamicAsteroid) == 0xE8, "FCliDynamicAsteroid must be 0xE8");
static_assert(offsetof(FCliDynamicAsteroid, CmnField) == 0x04, "Dyn CmnField +0x04");
static_assert(offsetof(FCliDynamicAsteroid, Arch) == 0x08, "Dyn Arch +0x08");
static_assert(offsetof(FCliDynamicAsteroid, SlotPtrs) == 0x0C, "Dyn SlotPtrs +0x0C");
static_assert(offsetof(FCliDynamicAsteroid, CountIni) == 0x10, "Dyn CountIni +0x10");
static_assert(offsetof(FCliDynamicAsteroid, SlotCount) == 0x14, "Dyn SlotCount +0x14");
static_assert(offsetof(FCliDynamicAsteroid, MaxVelocity) == 0x18, "Dyn MaxVelocity +0x18");
static_assert(offsetof(FCliDynamicAsteroid, PlacementRadius) == 0x20, "Dyn PlacementRadius +0x20");
static_assert(offsetof(FCliDynamicAsteroid, CameraPos) == 0x28, "Dyn CameraPos +0x28");
static_assert(offsetof(FCliDynamicAsteroid, bSpawnAllowed) == 0x34, "Dyn +0x34");
static_assert(offsetof(FCliDynamicAsteroid, bAnyVisible) == 0x35, "Dyn +0x35");
static_assert(offsetof(FCliDynamicAsteroid, MeshDesc) == 0x38, "Dyn MeshDesc +0x38");
static_assert(offsetof(FCliDynamicAsteroid, Material) == 0xA8, "Dyn Material +0xA8");
static_assert(offsetof(FCliDynamicAsteroid, PrimitiveType) == 0xAC, "Dyn PrimitiveType +0xAC");
static_assert(offsetof(FCliDynamicAsteroid, Mesh) == 0xB0, "Dyn Mesh +0xB0");
static_assert(offsetof(FCliDynamicAsteroid, MeshVertTotal) == 0xB4, "Dyn +0xB4");
static_assert(offsetof(FCliDynamicAsteroid, MeshIndexTotal) == 0xB8, "Dyn +0xB8");
static_assert(offsetof(FCliDynamicAsteroid, TintedDiffuse0) == 0xBC, "Dyn TintedDiffuse +0xBC");
static_assert(offsetof(FCliDynamicAsteroid, ColorShift0) == 0xD4, "Dyn ColorShift +0xD4");
static_assert(offsetof(FCliDynamicAsteroid, DetailScale) == 0xE0, "Dyn DetailScale +0xE0");
static_assert(offsetof(FCliDynamicAsteroid, InvDetailScale) == 0xE4, "Dyn InvDetailScale +0xE4");

// FUN_005201e0.
FCliAsteroid* CliAsteroidConstruct(FCliAsteroid* This);
// FUN_0051ff50 — called from the [Asteroids] arm of the system INI load (FUN_00553740).
bool CliAsteroidCreateAndLoad(char const* FieldIniRel, char const* ZoneNickname);
// FUN_00520130 — StarSys tick: with ASTEROIDS on, CliAsteroidUpdate on every field. Camera is
// the FSpaceCamera FUN_0040f130 hands out.
void CliAsteroidUpdateFields(void* Camera, float Dt);
// FUN_005200b0 — every registered field destroyed and freed, then the list emptied. Called
// from the StarSys system unload.
void CliAsteroidDestroyAll();
// FUN_005205d0 — the field destructor (only CliAsteroidDestroyAll calls it).
void CliAsteroidDestruct(FCliAsteroid* This);
// FUN_00521fa0 — each of the three floats not below 1.0 (NaN included) becomes 1.0; nothing
// is clamped from below. Used for the dynamic asteroid tints and the billboard centre colour.
void CliAsteroidClampColorToOne(float* Rgb);
// FUN_00522000 — Out = A * B per component; returns Out.
float* CliAsteroidMulColor(float* Out, float const* A, float const* B);
// FUN_0051feb0 — the vertex source descriptor over stride-0x2c FVF 0x252 vertices. Used by
// the field mesh merge and the dynamic asteroid parse.
void CliAsteroidFillVertexSource(FRpVertexSrcDesc* Desc, void* Verts, unsigned short VertCount);
// DAT_006753d8 — CliAsteroid.cpp's 3DMathEngine, acquired (FUN_005b73e0) on first use.
struct I3DMathEngine;
I3DMathEngine* CliAsteroidMathEngine();
// LAB_005267f0 — Asteroids perf option (IDS 0x57c): DAT_00613844 = Detail, the active camera's
// cut flag raised, then CliAsteroidApplyDetail on every field.
void CliAsteroidApplyGlobalDetail(float Detail);
// FUN_00520850. Resolves zones against the active system itself.
void CliAsteroidLoadFieldIni(FCliAsteroid* This, char const* FieldIniRel);
// FUN_005201c0.
unsigned CliAsteroidRegistryCount();

// FUN_00524730 — Band.RenderParts (+0x154) + ExclusionPieceCount (+0x194).
int CliAsteroidPieceCount(FCliAsteroid* Field);
// FUN_00524740 — piece world position; returns OutXYZ (zeroed, with an FDUMP warning, for a
// piece no band holds).
float* CliAsteroidGetPiecePos(FCliAsteroid* Field, int PieceIndex, float* OutXYZ);
// FUN_005247f0 — piece radius (1.0f, with an FDUMP warning, for a piece no band holds).
float CliAsteroidGetPieceRadius(FCliAsteroid* Field, int PieceIndex);

// DAT_005dc374: LIGHTING (0x89) on, SPECULARENABLE (0x1d) off, ALPHABLENDENABLE (0x1b) off
// (the opaque asteroid draw state). DAT_005dc394: the same with ALPHABLENDENABLE on, SRCBLEND
// (0x13) SRCALPHA (5), DESTBLEND (0x14) INVSRCALPHA (6). {state, value} pairs ending in 0, read
// by the field's near draw and the dynamic asteroid draw.
extern unsigned const kRsAsteroidOpaque[7];
extern unsigned const kRsAsteroidBlend[11];

typedef void (*FCliAsteroidVisitFn)(FCliAsteroid* Field, void* Ctx);
// Walk of the DAT_006753c4 registry, as FUN_0051ba20 and FUN_00511b90 inline it.
void CliAsteroidForEach(FCliAsteroidVisitFn Fn, void* Ctx);

struct FSpaceCamera;

// FUN_00522700 (thiscall on the field) — light the field around the point Scale along the
// camera's view axis (the dynamic asteroid draw calls it with 100.0f).
void CliAsteroidLightAround(FCliAsteroid* Field, FSpaceCamera* Cam, float Scale);

// FUN_00522070 (thiscall on the field) -- the per-frame update: the nebula lookup once, the
// exclusion re-sort, the CmnAsteroid change events into the cube map (MedAsteroid gameobjs),
// empty cubes dropped, the dynamic asteroid managers, then the band fades.
void CliAsteroidUpdate(FCliAsteroid* This, FSpaceCamera* Cam, float Dt);

// FUN_00520170 — per-frame walk of DAT_006753c4, gated on ASTEROIDS: per field the billboards,
// the near-field cube mesh draw (FUN_00522a60) and the dynamic asteroids.
void CliAsteroidTickFields(FSpaceCamera* Cam);
// FUN_00522850 (thiscall on the field, reached through LAB_0051bdf0) — the far band draw for
// one piece index from the deferred node.
void CliAsteroidDrawFarPiece(FCliAsteroid* Field, FSpaceCamera* Cam, int PieceIndex);
