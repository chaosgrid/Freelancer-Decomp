// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "engine/engine_instance.h" // InstanceIndex / ArchetypeIndex

// Retail space-object mesh draw walk (camera +0x78 → Slot7).
// Listing anchors: FUN_0051b250, FUN_0051c200, FUN_0051f2c0, FUN_00539980,
// plus same-pass drain FUN_0051eda0 / FUN_0051f810 (required to close the queue).
//
// LOD: Gameobj vtable +0x17c = FUN_00539820 (not data fields at those offsets).

struct FSpaceCamera;
struct IObjInspect;
struct I3DMathEngine;
class FClientGameobj;
class FClientEquippedObj;

// DAT_006789d0 -- Gameobj.cpp's math engine, acquired (FUN_005b73e0) on first use.
I3DMathEngine* GameobjMathEngine();

struct FGameobjEffectList; // client_gameobj.h

// FUN_004f70c0 (cdecl) -- 1.0 - the generic effect perf (DAT_006129c8), left unrounded on the
// FPU. Callers include FUN_00539b40 and the first-person cockpit pass.
double SpaceEffectPerfOneMinus();

// FUN_005554d0 -- the StarSys per-object lighting: ambient (with the containing nebula's lerp
// and fog), then the nearby lights. Callers: the cockpit pass, the asteroid ambient and
// FUN_00539b40.
void SpaceApplyObjectLighting(float const* Pos3, float Radius, void* Camera, unsigned NebulaBits,
                              unsigned FogFactorBits, unsigned char Flag);

// FUN_00533880 / FUN_00533930 -- DALib::Engine QI for IFLAppearance, then Slot3 (InstId,
// Value) / Slot8 (Index, Out); true when the call returned >= 0, false when the QI failed.
// Callers: the draw submits above and the Ship draw LAB_00546050.
bool SetAppearanceInstanceValue(InstanceIndex InstId, void* ArchPath);
bool GetAppearanceOverrideMaterial(unsigned long Index, void** Out);

// The LayerFlags bits FGameCamera::DrawScene tests (the in-space frame passes 0xffff).
enum ESceneLayer
{
    kSceneLayerBackground = 0x1,   // the background star sphere
    kSceneLayerShips = 0x20,       // the ships' gameobj list (with option SHIPS)
    kSceneLayerSolars = 0x40,      // every large solar
    kSceneLayerProjectiles = 0x80, // guided, mine and countermeasure lists (with option PROJECTILES)
    kSceneLayerEffects = 0x100,    // the effects pass
    kSceneLayerDebris = 0x400,     // the debris list (with option DEBRIS)
    kSceneLayerLoot = 0x800        // the loot list (with option DEBRIS)
};

// FUN_00537c30 — rebuild DAT_00678980 light XZ quadtree from gamelit list.
void LightGridRebuild();
// FUN_005382f0 (callers: the space teardown and StarSysLeave): the grid root emptied with HalfSize = DAT_006789a0 / 2 (the extent is left as is) and both node pools
// rethreaded as free lists.
void LightGridReset();

// FUN_00537f40 — nearby light collect from the XZ light grid (max 4 typical).
int SpaceCollectNearbyLights(void** OutSlots, int MaxSlots, float const* Pos3, float Radius,
                             unsigned char Flag);

// FUN_0051b250(LayerFlags, Param2): watcher gate → FGameCamera::DrawScene → depth clear → HUD.
// The in-space frame passes LayerFlags=0xffff, Param2=0.
void SpaceCameraRenderSlot78(FSpaceCamera* Cam, unsigned LayerFlags, unsigned Param2);

// FUN_00410130 -- default draw range / far clip: DAT_006164d0 = 20000, DAT_006164d4 = 21000.
// Caller: the system INI load FUN_00553740 (before the file opens).
void SpaceResetFarClip();

// FUN_00410150 -- [SystemInfo] space_farclip: DAT_006164d4 = max(FarClip, 1000) (NaN kept),
// DAT_006164d0 = that - 1000.
void SpaceSetFarClip(float FarClip);

// DAT_00675244 / DAT_0067524c / DAT_00675248 -- the field controller (a nebula) FUN_0051b730
// picked for the current camera, how far the camera is inside it (0 none, 1 partial, 2 fully)
// and its radius squared. Read by the particle visibility test FUN_004f86c0.
void* GetSpaceFieldController();
int GetSpaceFieldEnclosure();

// The values GetSpaceFieldEnclosure answers (DAT_0067524c).
enum EFieldEnclosure
{
    kFieldEnclosureNone = 0,    // the camera is in no nebula
    kFieldEnclosurePartial = 1, // the controller's zone is only partly around the camera
    kFieldEnclosureInside = 2   // fully inside it
};
float GetSpaceFieldRadiusSq();

// FUN_0051fbc0 -- the draw queue's teardown: Heap::Shrink on the two ZItem heaps
// (DAT_00675350 / DAT_00675340), free() every queued ZItem and empty the five bucket vectors.
// Callers: the space-state exit and SpaceApp::Shutdown's tail jump.
void SpaceDrawQueueRelease();
