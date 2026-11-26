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
// FUN_004fd040 (thiscall on GameObject+0x28) -- sweep the effect list: dead companions are
// cleared and their nodes freed, live particles re-armed. Callers: the gameobj draw submit
// (in this file) and the dynamic asteroid draw FUN_005261b0.
void GameobjSweepEffectListRetail(FGameobjEffectList* List);

// FUN_00539820 -- GameObject's vt+0x17c body (the LOD pick), called by FClientGameobj::PickLod
// and, non-virtually, by EquippedObj's override FUN_00533710. *OutLod: low word = the camera's
// ClassifySphere class (vt+0x3c) of the object's GetPhysicalRadius sphere, high word 0xffff below class 3,
// else 0 / 1 / 2 by the companion's +0x08 / +0x0c distances.
unsigned* GameobjPickLodRetail(FClientGameobj* Self, unsigned* OutLod, void* Camera,
                               unsigned const* CachedLod);

// FUN_00539b40 -- GameObject's vt+0x184 body (per-object lighting): own +0x178 radius,
// get_type & 0x807, 1 - perf against 0.999, then FUN_005554d0(position, radius, Camera, +0x38,
// +0x3c, flag). For FClientGameobj::UpdateDrawLighting to call.
void GameobjUpdateDrawLightingRetail(FClientGameobj* Self, void* Camera);

// FUN_004f70c0 (cdecl) -- 1.0 - the generic effect perf (DAT_006129c8), left unrounded on the
// FPU. Callers include FUN_00539b40 and the first-person cockpit pass.
double SpaceEffectPerfOneMinus();

// FUN_005554d0 -- the StarSys per-object lighting: ambient (with the containing nebula's lerp
// and fog), then the nearby lights. Callers: the cockpit pass, the asteroid ambient and
// FUN_00539b40.
void SpaceApplyObjectLighting(float const* Pos3, float Radius, void* Camera, unsigned NebulaBits,
                              unsigned FogFactorBits, unsigned char Flag);

// FUN_0040fb60 -- ZNear (+0xa4) and ZFar (+0xa8) written.
void SpaceCameraSetNearFar(FSpaceCamera* Cam, float ZNear, float ZFar);

// FUN_00539980 -- GameObject's vt+0x188 body (the draw submit), for FClientGameobj::SubmitDraw:
// PreSubmitDraw, the PickLod LOD pick written back over CachedLod, the gate (low word < 3 or high
// word >= 2 returns the LOD), the effect-list sweep FUN_004fd040 (high word < 1), UpdateDrawLighting, the
// player flags, the IFLAppearance envmap arm, IRenderer2::Slot7(Camera, get_index, 1.0f, Flags,
// Xform, Extra), FlushAll(1) on a result >= 3; *OutLod = {result, 0xffff}. The draw drain
// reaches it through the node slot LAB_0051f7a0 (vt+0x188(&scratch, Cam, node LOD, 0, 0)).
unsigned* GameobjSubmitDrawRetail(FClientGameobj* Self, unsigned* OutLod, void* Camera,
                                  unsigned CachedLod, Transform const* Xform, void* Extra);

// FUN_00533780 -- EquippedObj's vt+0x188 body, for FClientEquippedObj::SubmitDraw: the CEqObj's
// +0x140 fade above 0 pushes 1 - fade through IFLAppearance Slot3 (latch +0xe0 = 1) and, for a
// non-nomad archetype, Slot8(3, &Extra); otherwise a set latch is cleared through Slot5. Then
// FUN_00539980 non-virtually. Also called directly from two other draw slots.
unsigned* EquippedObjSubmitDrawRetail(FClientEquippedObj* Self, unsigned* OutLod, void* Camera,
                                      unsigned CachedLod, Transform const* Xform, void* Extra);

// FUN_00533880 / FUN_00533930 -- DALib::Engine QI for IFLAppearance, then Slot3 (InstId,
// Value) / Slot8 (Index, Out); true when the call returned >= 0, false when the QI failed.
// Callers: the draw submits above and the Ship draw LAB_00546050.
bool SetAppearanceInstanceValue(InstanceIndex InstId, void* ArchPath);
bool GetAppearanceOverrideMaterial(unsigned long Index, void** Out);

// FUN_0051c200 -- GameCamera slot 0x78, also called directly by
// FUN_0051b250 and the first-person FUN_00518420. The player's jump arm (Ship +0x158 == 2|3)
// draws only the ship, its effects and the jump tunnel; otherwise the queue DAT_00675360 is
// refilled (spatial Fill, large solars, ManagerKind walks FUN_0051f7d0), sorted and drained
// (FUN_0051f8d0, FUN_0051f950), the effects drawn (layer bit 0x100) and the clear colour set.
void SpaceObjDrawPass(FSpaceCamera* Cam, unsigned LayerFlags, unsigned Param2);

// FUN_00537c30 — rebuild DAT_00678980 light XZ quadtree from gamelit list.
void LightGridRebuild();
// FUN_005382f0 (callers: the space teardown and StarSysLeave): the grid root emptied with HalfSize = DAT_006789a0 / 2 (the extent is left as is) and both node pools
// rethreaded as free lists.
void LightGridReset();

// FUN_00537f40 — nearby light collect from the XZ light grid (max 4 typical).
int SpaceCollectNearbyLights(void** OutSlots, int MaxSlots, float const* Pos3, float Radius,
                             unsigned char Flag);

// FUN_0051b250(LayerFlags, Param2): watcher gate → SpaceObjDrawPass → depth clear → HUD.
// The in-space frame passes LayerFlags=0xffff, Param2=0.
void SpaceCameraRenderSlot78(FSpaceCamera* Cam, unsigned LayerFlags, unsigned Param2);

// FUN_0051c970 / FUN_0051c9e0 -- with options 0x25, 0x24 and 0x28 all
// on, walk FUN_0050c620()'s environment-effect list around the first-person cockpit draw:
// before, slot 0x10 (camera, 1.0f) on each entry, or +0x34 = 1.0f when fully inside a field;
// after, slot 0x14 (camera, 1.0f). The first-person scene slot FUN_00518420 calls both.
void SpaceEnvFxBeginCockpitPass(FSpaceCamera* Cam);
void SpaceEnvFxEndCockpitPass(FSpaceCamera* Cam);

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
float GetSpaceFieldRadiusSq();

// FUN_0051fbc0 -- the draw queue's teardown: Heap::Shrink on the two ZItem heaps
// (DAT_00675350 / DAT_00675340), free() every queued ZItem and empty the five bucket vectors.
// Callers: the space-state exit and ShutdownSpaceState's tail jump.
void SpaceDrawQueueRelease();
