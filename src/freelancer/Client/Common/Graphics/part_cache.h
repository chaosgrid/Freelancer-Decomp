// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Client-side per-object VMeshWire/IMesh part cache (retail FUN_00405b10 cluster).
// Builds merged CPU vertex (16-byte: XYZ + tag) and 16-bit index buffers per engine
// instance hierarchy for wireframe/damage-hide consumers.
//
// The sole Build site is HUD_Target_Base (this+0x3a0).

#include "engine/engine_instance.h" // InstanceIndex / ArchetypeIndex
#include "x86math.h"

#include <cstddef>
#include <map>

struct IVMeshWire;
struct IMesh;
struct ICamera;

// One part record — retail size 0x1c (LAB_00405f80 ctor).
#pragma pack(push, 1)
struct FPartCacheVertex
{
    float X;
    float Y;
    float Z;
    unsigned Tag;
};

struct FPartCacheEntry
{
    unsigned VertexCount;           // +0x00
    FPartCacheVertex* Vertices;     // +0x04
    unsigned IndexCount;            // +0x08
    unsigned short* Indices;        // +0x0C
    InstanceIndex InstanceId;       // +0x10  (-1 = unset)
    unsigned Tag;                   // +0x14  packed color written into vertex.Tag
    unsigned char bHidden;          // +0x18  set by MarkHidden
    unsigned char bActive;          // +0x19  ctor=1; ActivatePart sets 1
    unsigned char Field1A;          // +0x1A  ctor=0
    unsigned char bSkipWeightTest;  // +0x1B  IMesh path weight cull gate

    // FUN_004058b0 — free one entry's vertex/index buffers.
    void Destroy();

    // FUN_00405fb0 — VMeshWire path: weld part verts through TransformXform.
    void BuildFromWire(IVMeshWire* Wire, Transform const* Xform);

    // FUN_00406230 — IMesh path: walk groups, weld positions (channel 0x100).
    unsigned BuildFromMesh(IMesh* Mesh, Transform const* Xform);

    // FUN_00406200 — call BuildFromMesh twice; store first return in Entry->IndexCount.
    void BuildFromMeshTwice(IMesh* Mesh, Transform const* Xform);
};
#pragma pack(pop)

static_assert(sizeof(FPartCacheEntry) == 0x1C, "FPartCacheEntry 0x1C");
static_assert(sizeof(FPartCacheVertex) == 0x10, "FPartCacheVertex 0x10");

// Root cache object — retail layout at consumer+0x3a0.
struct FPartCache
{
    FPartCacheEntry* Entries;       // +0x00  points past VC6 array count prefix
    unsigned PartCount;             // +0x04
    unsigned TotalIndices;          // +0x08
    unsigned TotalVertices;         // +0x0C
    unsigned short* MergedIndices;  // +0x10

    // FUN_004058e0 — destroy all entries + merged IB.
    void Clear();

    // FUN_00405b10 — rebuild whole hierarchy cache for Instance with Tag color.
    // Returns true on success (TotalIndices <= 16000); false on invalid instance or overflow.
    bool Build(InstanceIndex Instance, unsigned Tag);

    // FUN_00405930 — mark part (and optional children) hidden.
    void MarkHidden(InstanceIndex Instance, bool bRecurseChildren);

    // FUN_004059d0 — clear bActive on non-root parts that are hidden.
    void ClearHiddenActive();

    // FUN_00405a00 — activate part, optionally retag vertices; recurse children if asked.
    // Returns true if the instance was found in the cache.
    bool ActivatePart(InstanceIndex Instance, bool bRecurseChildren, unsigned Tag);
    void Draw(ICamera* Camera, Transform const* Xform);
};

// FUN_004061a0 — byte size of packed FVF texcoord block.
int FvfTexcoordBlockBytes(unsigned Fvf);

// FUN_00406880 — copy 12 dwords (Transform / instance matrix).
void CopyTransform12(Transform* Dest, Transform const* Source);

// LAB_00405f80 — zero-init one entry.
void PartCacheEntryConstruct(FPartCacheEntry* Entry);

// FUN_00406510 (thiscall on the cache, RET 8) — draw the active parts as one line list (FVF
// 0x42, XYZ + diffuse tag), untextured and unlit. Caller: HUD_Target's target view (the view
// camera +0x3b8, the object's transform).
// The instance -> saved LOD map FUN_00402580 fills and FUN_00402720 replays (retail VC6
// std::map<long,int>, 0x18-byte nodes: key +0xc, value +0x10; ctor FUN_004e7f60, destroy
// FUN_004e7e70 / FUN_004e7f20, insert FUN_00402790, iterator ++ FUN_0052afa0).
typedef std::map<void*, int> FRendererLodMap;

// FUN_00402580 (cdecl Instance, PreferredLod, Saved). Instance -1 returns 0. Otherwise: when
// Saved is given, record the current LOD of Instance, clamp PreferredLod to [.., LodCount-1]
// and set it; then every child the same, except the child's clamp is LodCount (max index + 1).
// Returns the largest max LOD index seen (0 when none is positive). Callers: HUD_Target_Base
// (with a map) and one site at 0x00539a1e.
int BindRendererLods(InstanceIndex Instance, int PreferredLod, FRendererLodMap* Saved);

// FUN_00402720 (cdecl UnusedInstance, Saved) -- replay every saved (instance, LOD) pair through
// IRenderer2, the LOD clamped to LodCount - 1. The first argument is pushed by the caller but
// never read.
void ApplyRendererDetailQueue(InstanceIndex UnusedInstance, FRendererLodMap const* Saved);
