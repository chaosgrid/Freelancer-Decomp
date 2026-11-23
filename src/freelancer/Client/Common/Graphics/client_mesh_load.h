// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstddef>
#include "Common/client_cliobj.h"

struct IEngine;

// -------------------------------------------------------------------------------------------
// Client mesh holder + .3db load — retail FUN_00420d40 / FUN_0059ec80 / FUN_0059f0d0.
//
// HUD panels (Steer, Targetable, alerts, WinControl icons) embed a 0x28-byte holder and call
// FUN_0059ec80(path, material path or 0, 1). The +0x14 floats are the CompoundInstanceAnalyze
// context (callback FUN_0059ec60 → IEngine SetInstanceUserData).
// -------------------------------------------------------------------------------------------

struct IEngine;

// Retail `ObjectHandle` (RTTI .?AUObjectHandle@@, COL 0x005e8300): the object FUN_00420d40
// constructs, exactly 0x10 bytes (vtable PTR_LAB_005c9694, instance -1, two material-cache
// slots). Its only virtual is the scalar deleting destructor LAB_00420d20; the destructor
// FUN_00420d10 just resets the vptr -- it does NOT release the instance, so owners release
// explicitly (the star sphere FUN_00552c70, the Ship's +0x108). The first-person camera embeds
// one bare at +0x190; Object3D below derives from it.
struct FMeshHolder
{
    FMeshHolder() = default;
    virtual ~FMeshHolder(); // FUN_00420d10; scalar deleting wrapper LAB_00420d20
    void* EngineInstance = reinterpret_cast<void*>(static_cast<ptrdiff_t>(-1)); // +0x04
    Archetype::FCliMaterialEntry* MaterialEntry = nullptr;    // +0x08
    Archetype::FCliMaterialEntry* AuxMaterialEntry = nullptr; // +0x0c
};

static_assert(sizeof(FMeshHolder) == 0x10, "FUN_00420d40 object is 0x10");
static_assert(offsetof(FMeshHolder, EngineInstance) == 0x4, "holder +0x4 instance");

// Retail `Object3D : ObjectHandle` (RTTI .?AUObject3D@@, COL 0x005eb5e8), 0x28 bytes, vftable
// 0x005d2efc whose one slot is the scalar deleting destructor FUN_0048dd20. The constructor is
// always inlined: FUN_00420d40, FUN_00492250 on +0x14 (five floats 1.0f), the Object3D vptr,
// +0x10 = -1. The destructor FUN_0059ef20 is FUN_0059eee0's body -- close the animation script,
// release a bound instance -- then ~ObjectHandle; it runs as the member dtor of every owner
// (21 call sites, most of them EH unwind funclets), so the port holds it as a real destructor.
// Copying is deleted: two holders would release one instance twice.
struct FClientMeshHolder : FMeshHolder
{
    FClientMeshHolder() = default;
    ~FClientMeshHolder() override; // FUN_0059ef20
    FClientMeshHolder(FClientMeshHolder const&) = delete;
    FClientMeshHolder& operator=(FClientMeshHolder const&) = delete;

    long AnimOrArchIndex = -1;                            // +0x10
    float UserFloats[5] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f}; // +0x14..+0x24 (FUN_00492250)
};

static_assert(sizeof(FClientMeshHolder) == 0x28, "retail HUD mesh holder 0x28");
static_assert(offsetof(FClientMeshHolder, AnimOrArchIndex) == 0x10, "holder +0x10 anim");
static_assert(offsetof(FClientMeshHolder, UserFloats) == 0x14, "holder +0x14 analyze ctx");

// FUN_00420d40 itself: the four stores and nothing else.
void MeshHolderConstruct(FMeshHolder* Holder);

// IRenderer2 slot 4 / FUN_0059f0d0 out — 6 floats (AABB max/min XYZ, per RendComp slot 13).
struct FClientMeshAabb
{
    float Values[6] = {};
};

// FUN_00420d40 + the inlined Object3D part — construct an empty holder.
void ClientMeshHolderConstruct(FClientMeshHolder* Holder);

// FUN_00420d60 — instance bound?
bool ClientMeshHolderHasInstance(FMeshHolder const* Holder);

// FUN_00420f70 — destroy the instance and release both material-cache slots.
void ClientMeshHolderRelease(FMeshHolder* Holder);

// FUN_0059eee0 — close the animation script, then release a bound mesh holder.
void ClientUiMeshHolderRelease(FClientMeshHolder* Holder);

// FUN_00420dd0 (thiscall on the holder, RET 8) - open Path as a DALib::Data FileSystem,
// resolve the material-cache entries for Path/AuxPath into +0x08/+0x0c, Archetype::Load the
// FileSystem and, on success, replace the bound instance with CreateInstance(handle, 0).
// Returns Archetype::Load's result. Caller: the star sphere layer load FUN_00552fd0.
bool ClientMeshHolderLoad(FMeshHolder* Holder, char const* Path, char const* AuxPath);

// FUN_00420f30 (thiscall on the holder, RET 8) - a non-null Path resolves its material-cache
// entry into +0x08 (FUN_0041aa50(Path, 1, -1)), a non-null AuxPath into +0x0c. What was there
// before is not released. Caller: FUN_004c9a80 (the engine-character accessory material).
void ClientMeshHolderSetMaterials(FMeshHolder* Holder, char const* Path, char const* AuxPath);

// FUN_00420d70 (thiscall on the holder, RET 4) - a bound instance is destroyed and BOTH
// material-cache slots released (FUN_0041b030); then CreateInstance(ArchHandle, 0). True when
// that is not -1. Callers: FLFxMeshFactory, FUN_0059ec80.
bool ClientMeshHolderBindInstance(FMeshHolder* Holder, void* ArchHandle);

// FUN_00421030 (thiscall on the holder) - IEngine GetInstanceTransform.
float* ClientMeshHolderGetTransform(FMeshHolder const* Holder);

// FUN_00421090 (thiscall on the holder, RET 4) - the instance's bounding sphere in world space
// (the centre through the instance transform). The math engine is cached in DAT_00616870.
// Returns the radius. Caller: the first-person cockpit pass.
float ClientMeshHolderGetBoundingSphere(FMeshHolder const* Holder, float* OutCenter);

// FUN_00421050 (thiscall on the holder, RET 4) - IEngine SetInstanceMatrix.
void ClientMeshHolderSetMatrix(FMeshHolder* Holder, void* Transform);

// FUN_00421120 (thiscall on the holder, RET 8) - IRenderer2 slot 7 at lod 1.0.
void ClientMeshHolderRender(FMeshHolder* Holder, void* Camera, unsigned long Flags);

// FUN_00420fd0 (thiscall on the holder, RET 4) - IEngine SetInstancePosition.
void ClientMeshHolderSetPosition(FMeshHolder* Holder, float const Position[3]);

// FUN_00420fb0 (thiscall on the holder) - IEngine GetInstancePosition.
float* ClientMeshHolderGetPosition(FMeshHolder const* Holder);

// FUN_00420ff0 (thiscall on the holder) - IEngine GetInstanceOrientation: nine floats,
// row-major.
float* ClientMeshHolderGetOrientation(FMeshHolder const* Holder);

// FUN_00421010 (thiscall on the holder, RET 4) - IEngine SetInstanceOrientation.
void ClientMeshHolderSetOrientation(FMeshHolder* Holder, float const Orientation[9]);

// FUN_00421070 (thiscall on the holder) - IEngine UpdateInstance(instance, 0, 0).
void ClientMeshHolderUpdate(FMeshHolder* Holder);

// FUN_004a1af0 (thiscall on the holder, RET 4) - the RGBA byte quad at Rgba scaled by 1/255
// into UserFloats[0..2] and [4]; +0x20 is left alone. Callers: HUD_DamageIndicator::Draw, the
// nav map, HUD_Comm and ScrollControl.
void ClientMeshHolderSetColor(FClientMeshHolder* Holder, unsigned char const* Rgba);

// FUN_0059ec80 (thiscall on the holder, RET 0xc) -- bind RelPath's archetype to Holder.
// FindArchetype hit: FUN_00420d70 + CompoundInstanceAnalyze, then the handle is released.
// Miss: RelPath is opened as a DALib::Data FileSystem, FUN_00420f30(Holder, RelPath,
// MaterialPath) resolves both material-cache entries (MaterialPath may be null), then
// Archetype::Load; with bRegisterCache the handle is kept in the Win3D model cache
// (DAT_0067dcf4) instead of being released. Returns FUN_00420d60 (instance bound).
bool ClientMeshLoadFromPath(FClientMeshHolder* Holder, char const* RelPath,
                            char const* MaterialPath, bool bRegisterCache);

// The Win3D model cache DAT_0067dcf4 (a vector, element stride 0xc; FUN_0059ebf0 builds
// {handle, 0, 0}). It keeps an archetype handle -- and for a preloaded model its two
// material-cache entries -- alive until the Win3D manager shuts down.
struct FWin3DModelCacheEntry
{
    void* ArchHandle;                                   // +0x00
    Archetype::FCliMaterialEntry* MaterialEntry;        // +0x04
    Archetype::FCliMaterialEntry* AuxMaterialEntry;     // +0x08
};

// FUN_0059e980 (thiscall on DAT_0067dcf4, RET 8) -- preload Path into the model cache.
// Caller: HUD_ContactList::Create (the 16 icon meshes, AuxPath 0).
bool Win3DModelCachePreload(char const* Path, char const* AuxPath);

// FUN_0059e8d0 (thiscall on DAT_0067dcf4) -- release every cached handle and both
// material-cache slots of each entry, then empty the vector. Caller: the Win3D manager
// shutdown FUN_0059d6d0.
void Win3DModelCacheClear();

// FUN_0059f0d0 (thiscall on the holder, RET 4) — false when the instance is -1, else
// IRenderer2 slot 4 into OutAabb and true whatever it answered. OutAabb is not cleared on the
// false path.
bool ClientMeshQueryAabb(FClientMeshHolder* Holder, FClientMeshAabb* OutAabb);
