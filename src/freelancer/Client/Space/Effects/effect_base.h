// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_goods.h"
#include "x86math.h"

#include <cstddef>

// Shared Effect head — Client/space/Effect.cpp (FUN_004f79f0).
// BeamEffect / ParticleEffect / CliEquipLight share this through +0x68.

struct FEffectArch;
struct FVisEffectArch;
struct FSpaceCamera;
struct FGameLight;   // Common/engine_light.h
struct ISoundSource; // soundmanager.h

// FEffectBase::Flags08 (+0x08).
enum EEffectFlag
{
    kEffectFlagLive = 0x1,        // FCliEquipLight: set by the owner's effect-list sweep (FUN_004fd040), cleared by UpdateVisibility; IsAlive is this bit
    kEffectFlagDeactivated = 0x2  // the effect is dead: Start failed, the run time elapsed, or it was frozen to the world
};

struct FEffectBase
{
    explicit FEffectBase(unsigned TypeId);
    void AddRef();
    int Release();
    void Attach(void* WatchableOwner, int EngineIndex, Transform const* LocalXform);
    virtual unsigned char Start(FEffectArch* Arch);
    virtual void Kill();
    virtual float GetWorldPos(Vector* Out) = 0;
    virtual void SetIntensity(float Intensity);
    virtual unsigned char IsAlive() = 0;
    virtual unsigned char UpdateVisibility(float Dt, FSpaceCamera* Cam) = 0;
    virtual unsigned char TickDraw(float Dt, FSpaceCamera* Cam) = 0;
    virtual unsigned char BuildGeometry(FSpaceCamera* Cam) = 0;
    virtual ~FEffectBase();
    static void operator delete(void* Memory, size_t Size);

    union
    {
        float Pad04;               // +0x04
        FVisEffectArch* Vis;
    };
    unsigned char Flags08;         // +0x08  bit0 live (light), bit1 deactivated
    unsigned char Pad09[3];
    float Priority;                // +0x0C  1.0, or the type info's priority
    Transform LocalXform;         // +0x10 … +0x3F
    int EngineIndex;               // +0x40  = -1
    BaseWatcher Watcher;           // +0x44 … +0x4B
    unsigned char FrozenVelocity[0x0C]; // +0x4C … +0x57  three floats; the freeze stores the
                                        //               instance's velocity
    unsigned RefCount;             // +0x58  FUN_004f7a80 / FUN_004f7a90
    unsigned EffectTypeId;         // +0x5C
    void* LinkPrev;                // +0x60
    void* LinkNext;                // +0x64
    void* LinkSelf;                // +0x68

    // FUN_004f7db0 — detach the effect from its engine instance and keep it where it is. Nothing
    // when EngineIndex is -1. When the watcher is empty: link into the type bucket if needed, call
    // ITSELF (with the index still set and the watcher still empty that recurses without end;
    // kept as retail), then Flags08 |= 2. Otherwise: +0x4c = CObject::get_velocity() of the instance's user data (IEngine +0x80,
    // CObject vt+0x54), or zero; +0x10 = GetInstanceTransform(index) × local through
    // I3DMathEngine::MulTransform (DAT_00674f50); EngineIndex = -1.
    void FreezeToWorld();
    unsigned char InCameraView(FSpaceCamera* Cam);

    // FUN_004f7ef0 — the world transform (instance × local through
    // DAT_00674f50 when watched and attached, else the local transform) copied into *Out.
    void GetWorldTransform(Transform* Out) const;
};
static_assert(sizeof(FEffectBase) == 0x6c, "EffectBase through +0x68");
static_assert(offsetof(FEffectBase, EngineIndex) == 0x40, "EngineIndex");
static_assert(offsetof(FEffectBase, RefCount) == 0x58, "RefCount");
static_assert(offsetof(FEffectBase, EffectTypeId) == 0x5c, "EffectTypeId");

// The 0x10-byte effect companion every client object keeps its live effects in (filled by
// FUN_005154a0, cleared by FUN_00502d90): the effect's arch, its sound, the effect itself (a
// particle, a beam or a CliEquipLight) and its light.
struct FEffectCompanion
{
    union
    {
        struct
        {
            FEffectArch* EffectArch; // +0x00
            ISoundSource* Sound;     // +0x04
            FEffectBase* Effect;     // +0x08
            FGameLight* Light;       // +0x0c
        };
        void* Slot[4]; // the same four words by index (the tractor wrapper's init loop)
    };
};
static_assert(sizeof(FEffectCompanion) == 0x10, "effect companion 0x10");

// The 0x18-byte node of the GameObject+0x28 effect list (a VC6 std::list): the links and a copy
// of one companion (FUN_004fd480).
struct FEffectListNode
{
    FEffectListNode* Next;      // +0x00
    FEffectListNode* Prev;      // +0x04
    FEffectCompanion Companion; // +0x08
};
static_assert(sizeof(FEffectListNode) == 0x18, "effect list node 0x18");

// FUN_004f79a0 — size==0x84 → Heap::Malloc on the pool DAT_00674ccc; else malloc.
void* EffectAlloc(unsigned Size);

// FUN_004f79c0 — matching free.
void EffectFree(void* Ptr, unsigned Size);

// FUN_004f7140 — client teardown: kill and release every live effect (EffectTypeListKillAll),
// release the FLFxMeshFactory DAT_00674cdc, clear G_EffectShapeDB, Shrink both effect Heaps
// (DAT_00674ccc, DAT_00674cbc).
void EffectSystemShutdown();

// The object the effect's watcher holds (+0x44 minus 8), or null. Retail spells this inline
// everywhere: `[+0x44] == 0 → null; [+0x44] - 8 == 0 → null`.
IObjRW* EffectWatchedObject(FEffectBase const* This);

// FUN_004f7b50 (callers 0x0050c484 and the tractor beam's slot +0x14 LAB_005133f0) — the
// effect's own slot +8 (world position and radius), the camera's view transform (slot +0x40) applied through I3DMathEngine +0x28
// (DAT_00674f50), then the camera's ClassifySphere (slot +0x3c) with the view-space centre and
// the radius: true when the class is 3 or more (straddling or inside), SETGE on a signed int.
struct FSpaceCamera;
