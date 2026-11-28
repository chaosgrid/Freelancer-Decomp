// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail SunEffect / StarSphere — freelancer.exe PTR_FUN_005db658 (7 slots).
// Source path (embedded): Client\space\StarSphere.h.
// Distinct from TexturePanels (PTR_LAB_005db1b4) and from skybox "star_sphere" perf option.
//
// Layout from FUN_00510d10 ctor + LAB_00511020 Load stores. Child at +0x14 is 0x30c
// (FUN_0050d300); glow descriptors at +0x10/+0x0c are 0x50 (star_glow / star_center).

#include <cstddef>

#include "math3d.h"

struct ICamera;

// FUN_004215b0 — write identity 3×3 into *Out (nine floats, no translation).
void Win3DIdentityRotation3x3(Matrix* Out);

// FUN_00422690 — GWin3DModelview = ObjectOrNull ? view(vt+0x40) * Xform : Xform;
// pipeline state 7 set -> SetModelview, else SetView(view or identity) + SetWorld(Xform).
// ObjectOrNull is a camera (TexturePanelsDrawPiece) or null (DrawClouds, the UI tables).
void Win3DBindModelview(ICamera* ObjectOrNull, Transform const* Xform);

struct FTexShape;

// Star-glow CacheString-ish record (0x50). Shape name: length@+0, chars@+4 (0x20).
struct FStarGlowDesc
{
    unsigned ShapeLen;       // +0x00  FUN_00417c50 return length
    char ShapeName[0x20];    // +0x04
    float Scale;             // +0x24
    float InnerColor[3];     // +0x28
    float OuterColor[3];     // +0x34
    float Alpha;             // +0x40
    unsigned Field44;        // +0x44  zeroed by the ctor, never read
    FTexShape const* Shape;  // +0x48  resolved from ShapeName on first use (LAB_00511690)
    unsigned char bShapeAlpha; // +0x4c the shape's texture has alpha
    unsigned char Pad4D[3];

    // FUN_0050c950 — parse [star_glow] nickname into Desc from IniPath. Returns 0 on ok.
    int LoadFromIni(char const* IniPath, char const* Nickname);

    // FUN_00420750. Defined in star_sphere.cpp.
    void InitDefaults(unsigned char OwnerTag);
};
static_assert(sizeof(FStarGlowDesc) == 0x50, "star_glow desc 0x50");

// One corona sample of the lens glow (0x18): a screen offset around the sun, its UV, and the
// visibility alpha the occlusion pass (FUN_0050da20) approaches.
struct FSunCoronaSlot
{
    float X;                 // +0x00
    float Y;                 // +0x04
    float U;                 // +0x08
    float V;                 // +0x0c
    float Alpha;             // +0x10
    unsigned char bTouched;  // +0x14 set by the pass that updated Alpha this frame
    unsigned char Pad15[3];
};
static_assert(sizeof(FSunCoronaSlot) == 0x18, "corona slot 0x18");

// SunEffect body at StarSphere+0x14 (operator_new 0x30c, FUN_0050d300); the [lens_glow] keys
// of FUN_0050d420.
struct FSunEffectBody
{
    FSunCoronaSlot Slots[13];          // +0x000 the centre (0) and the rim ring (1..12)
    FSunCoronaSlot InteriorSlots[12];  // +0x138 between the centre and the rim, interior_tween
    int EdgeFlags[12];                 // +0x258 diagonal choice per interior quad (LOD >= 3)
    float InnerColor[3];               // +0x288 inner_color
    float InteriorColor[3];            // +0x294 inner / outer lerp by interior_tween
    float OuterColor[3];               // +0x2a0 outer_color
    float RadiusScale;                 // +0x2ac radius_scale
    int Lod;                           // +0x2b0 0..MaxLod, from the pixel size
    int MaxLod;                        // +0x2b4 max_LOD
    int LodReduction;                  // +0x2b8 from the SunEffect perf option (LAB_00510bf0)
    unsigned ShapeLen;                 // +0x2bc
    char ShapeName[0x20];              // +0x2c0 shape
    FTexShape const* Shape;            // +0x2e0 resolved on first draw
    unsigned char bShapeAlpha;         // +0x2e4
    unsigned char Pad2E5[3];
    float GlowFadeIn;                  // +0x2e8 glow_fade_in_seconds
    float GlowFadeOut;                 // +0x2ec glow_fade_out_seconds
    float InteriorTween;               // +0x2f0 interior_tween
    float InteriorRayTween;            // +0x2f4 interior_ray_tween
    int LodPixelThresholds[5];         // +0x2f8 8, 15, 25, 40, 60

    // FUN_0050d420 — parse [lens_glow] into SunBody. Returns 0 on ok.
    int LensGlowLoadFromIni(char const* IniPath, char const* Nickname);

    // FUN_0050d930 — bake corona ring positions/UVs after lens_glow load.
    int BakeCorona();

    // FUN_0050d300
    FSunEffectBody* Init();
};
static_assert(sizeof(FSunEffectBody) == 0x30c, "SunEffect body 0x30c");
static_assert(offsetof(FSunEffectBody, InteriorSlots) == 0x138, "interior slots +0x138");
static_assert(offsetof(FSunEffectBody, InnerColor) == 0x288, "inner_color +0x288");
static_assert(offsetof(FSunEffectBody, ShapeName) == 0x2c0, "shape +0x2c0");
static_assert(offsetof(FSunEffectBody, LodPixelThresholds) == 0x2f8, "LOD thresholds +0x2f8");

// One element of the spines vector (stride 0x2c; FUN_005126f0 is its insert). FUN_0050fba0
// reads the nine values of a "spine" line into +0x08..+0x28 and then sets the direction from
// the element's index; the draw FUN_00510160 reads them in this order.
struct FStarSpine
{
    float Cos;               // +0x00 direction, cos(index * 2pi / count)
    float Sin;               // +0x04 direction, sin(...)
    float Length;            // +0x08 times radius_scale at load
    float Width;             // +0x0c times radius_scale at load
    float InnerColor[3];     // +0x10 the colour at the base
    float OuterColor[3];     // +0x1c the colour at the tip
    float Alpha;             // +0x28
};
static_assert(offsetof(FStarSpine, Length) == 0x08, "spine length +0x08");
static_assert(offsetof(FStarSpine, InnerColor) == 0x10, "spine inner colour +0x10");
static_assert(offsetof(FStarSpine, OuterColor) == 0x1c, "spine outer colour +0x1c");
static_assert(offsetof(FStarSpine, Alpha) == 0x28, "spine alpha +0x28");
static_assert(sizeof(FStarSpine) == 0x2c, "spine element 0x2c");

// Lens-flare / spines child shell (0x40). Load parsers FUN_0050f3a0 / 0050fba0 fill
// the element ranges; draw (FUN_0050f850 / 00510160) walks [Begin, End). The lens flare keeps
// a vector of float[6] bead pointers {along, size, r, g, b, alpha}, the spines a vector of
// 0x2c records (FStarSpine), kept as bytes: the port's push (SpinesPushElem) counts bytes.
struct FStarFxChild40
{
    unsigned char AllocatorByte; // +0x00
    unsigned char Pad01[3];
    union                        // +0x04 vector begin
    {
        float** BeadBegin;       // lens flare: float[6] bead pointers
        unsigned char* SpineBegin; // spines: 0x2c records
    };
    union                        // +0x08 vector end
    {
        float** BeadEnd;
        unsigned char* SpineEnd;
    };
    union                        // +0x0c vector capacity end
    {
        float** BeadCapacity;
        unsigned char* SpineCapacity;
    };
    unsigned ShapeLen;           // +0x10
    char ShapeName[0x20];        // +0x14 shape
    FTexShape const* Shape;      // +0x34 resolved on first draw
    float MinRadius;             // +0x38 min_radius (the pixel scale's floor)
    float MaxRadius;             // +0x3c max_radius (its ceiling)

    // FUN_0050f3a0 — parse [lens_flare] into child at StarSphere+0x18. Returns 0 on ok.
    int LensFlareLoadFromIni(char const* IniPath, char const* Nickname);

    // FUN_0050fba0 — parse [spines] into child at StarSphere+0x1c. Returns 0 on ok.
    int SpinesLoadFromIni(char const* IniPath, char const* Nickname);

    // FUN_0050f340. Defined in star_sphere.cpp.
    void LensFlareFree();

    // FUN_0050fb50. Defined in star_sphere.cpp.
    void SpinesFree();
};
static_assert(sizeof(FStarFxChild40) == 0x40, "lens flare / spines 0x40");
static_assert(offsetof(FStarFxChild40, SpineEnd) == 0x08, "child vector end +0x08");
static_assert(offsetof(FStarFxChild40, Shape) == 0x34, "child shape +0x34");


struct IObjInspect;
struct FSpaceCamera;
struct FStarSphere;

// StarEffects::Star — seven thiscall virtual methods.
struct FStarSphere
{
    explicit FStarSphere(IObjInspect* InOwner); // FUN_00510d10
    virtual ~FStarSphere();                     // +0x00 FUN_00510ef0 / FUN_00510f10
    virtual int Load(char const* IniPath, char const* Nickname); // +0x04
    virtual void UpdateIntensity(float Dt);                     // +0x08
    virtual void DrawSimple(FSpaceCamera* Camera);                      // +0x0c
    virtual void Draw(FSpaceCamera* Camera, float Dt);                   // +0x10
    virtual void DrawFlare(FSpaceCamera* Camera, float Dt);              // +0x14
    virtual void DrawBillboard(FSpaceCamera* Camera, float A, float B);   // +0x18

    IObjInspect* Owner;              // +0x04 borrowed solar IObjRW primary base
    float Radius;                    // +0x08  default 1000
    FStarGlowDesc* StarCenter;       // +0x0c
    FStarGlowDesc* StarGlow;         // +0x10
    FSunEffectBody* SunBody;         // +0x14  lens_glow target
    FStarFxChild40* LensFlare;       // +0x18
    FStarFxChild40* Spines;          // +0x1c
    float IntensityFadeIn;           // +0x20  default 1
    float IntensityFadeOut;          // +0x24  default 1
    float ZoneOcclusionFadeIn;       // +0x28  default 2
    float ZoneOcclusionFadeOut;      // +0x2c  default 2
    float Field30;                   // +0x30  pi
    float ZoneOcclusion;             // +0x34  approaches FUN_00511b90
    float CoronaOcclusion;           // +0x38  1 − FUN_0050f2a0(SunBody), the hidden corona share
    unsigned char Pad3C[0x24];       // +0x3c .. +0x5f
    float Dt;                        // +0x60  UpdateIntensity stores Dt
    float Intensity;                 // +0x64  default 1
    float WorldPos[3];               // +0x68 .. +0x70  owner position in VIEW space (Draw)
    float Pad74;                     // +0x74
    float ScreenDepth;               // +0x78  Project OutZ
    float ScreenX;                   // +0x7c  Project OutX
    float ScreenY;                   // +0x80  Project OutY
    float ScreenZFlat;               // +0x84  forced 0 for 2D corona
    float PixelScale;                // +0x88  znear·xscale / depth

    Vector* GetOwnerPosition(Vector* Out) const;

    // FUN_00512620 -- max(glow scale, centre scale or 0) * Radius; the glow is read with no test.
    // Caller: the Solar radius slot FUN_00548110.
    float MaxGlowScale() const;
};
static_assert(offsetof(FStarSphere, SunBody) == 0x14, "SunBody +0x14");
static_assert(offsetof(FStarSphere, Dt) == 0x60, "Dt +0x60");
static_assert(offsetof(FStarSphere, Intensity) == 0x64, "Intensity +0x64");
static_assert(offsetof(FStarSphere, WorldPos) == 0x68, "WorldPos +0x68");
static_assert(offsetof(FStarSphere, PixelScale) == 0x88, "PixelScale +0x88");

// DAT_0067509c — SunEffect perf quality (LAB_00510bf0). Draw/DrawFlare gate on > 0.
extern float G_SunEffectQuality;

// FUN_00510f90 — solar\\StarArch.ini path + Pipeline block + shapes load.
void LoadSolarStarArchIni();
// FUN_00511010 — clear the StarArch ShapeDB (DAT_006750a0); called by the space teardown
// FUN_0054aec0 (SpaceApp::Destroy).
void ClearSolarStarArchShapes();


// The live star list, a std::list<FStarSphere*> at DAT_0067517c: allocator byte, head (the
// sentinel, DAT_00675180), size (DAT_00675184). Its static initializer FUN_0050c580 allocates the
// self-linked sentinel and registers LAB_0050c5c0, which frees every node and the sentinel.
struct FStarSphereListNode
{
    FStarSphereListNode* Next; // +0x00
    FStarSphereListNode* Prev; // +0x04
    FStarSphere* Sphere;       // +0x08
};
struct FStarSphereList
{
    unsigned char AllocatorByte;   // +0x00 DAT_0067517c
    unsigned char Pad01[3];
    FStarSphereListNode* Sentinel; // +0x04 DAT_00675180
    int Count;                     // +0x08 DAT_00675184
};

// FUN_0050c620 -- &DAT_0067517c. Walked by the cockpit env-fx passes (FUN_0051c970 / 0051c9e0)
// and the field draw (space_scene_obj_draw.cpp), and by the SunEffect perf apply LAB_00510bf0.
FStarSphereList* StarSphereList();
// FUN_0050c630 -- new FStarSphere (0x8c, FUN_00510d10 with Owner), appended to the list;
// returns the list's last element. Caller: the solar ini reader FUN_005485c0 ("star").
FStarSphere* StarSphereCreateListed(IObjInspect* Owner);
// FUN_0050c6e0 -- slot 0 (deleting destructor, flag 1) on every non-null element, then every
// node erased. Caller: StarSys leave FUN_00554ab0.
void StarSphereDestroyAll();
// FUN_005125f0 -- the owner's (+4) IObjInspect slot 0 get_position copied into *Out; returns
// Out. No tests. Callers: the Ship draw LAB_00546050 and the Solar radius slot FUN_00548110.
class Vector;
