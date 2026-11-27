// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Effect base — Client/space/Effect.cpp (FUN_004f79a0 / 004f79f0 / 004f7a80 / 004f7d20,
// FUN_004f7bd0 kill, FUN_004f7db0 freeze-to-world, FUN_004f7ef0 world transform).

#include "Space/Effects/effect_base.h"

#include "Space/Effects/beam_effect.h"
#include "Space/Effects/effect_type_info.h"      // EffectTypeInfoByIndex (FUN_004fd560)
#include "Space/Effects/effect_type_list.h"
#include "Space/Effects/effect_vertex_batch.h"   // G_EffectMathEngine (DAT_00674f50)
#include "Common/Graphics/fl_fx_mesh.h"            // G_FLFxMeshFactory (DAT_00674cdc)
#include "Space/Cameras/space_camera.h"          // AsCamera (FUN_004f7b50)
#include "Common/Graphics/texture_shapes.h"        // G_EffectShapeDB (DAT_00674f24)

#include "DALib.h"
#include "alchemy.h"               // IFxMeshFactory (Release)
#include "common_cobject.h"        // CObject::get_velocity (vt+0x54)
#include "common_csys.h"           // Acquire3DMathEngine (FUN_005b73e0)
#include "common_misc.h"
#include "engine/engbase.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace
{
    // DAT_00674ccc — the Common.dll Heap pool for 0x84-byte effects, {ElementSize 0x84,
    // FreeList 0, MaxFreeCount -1, CurrentFreeCount 0}, stored by the static initializer
    // LAB_004f6fc0 (Heap ctor inlined there). FUN_004f79a0 / FUN_004f79c0 allocate and free
    // through it, FUN_004f7140 / FUN_004f8030 shrink and free into it.
    Heap G_EffectHeap(0x84u, -1);

    // DAT_00674cbc — a second {0x84, 0, -1, 0} Heap set up by the static initializer
    // LAB_004f6ff0. Nothing in the exe allocates from it; kept because FUN_004f7140 shrinks it.
    Heap G_EffectHeapSecond(0x84u, -1);
}

void EffectSystemShutdown()
{
    // FUN_004f7140 (called from the client teardown).
    // Every effect in every bucket is killed and released (the FUN_004f73c0 loop).
    EffectTypeListKillAll();

    // DAT_00674cdc, the FLFxMeshFactory FUN_004f70d0 creates.
    if (G_FLFxMeshFactory != nullptr)
    {
        G_FLFxMeshFactory->Release();
        G_FLFxMeshFactory = nullptr;
    }

    G_EffectShapeDB.Clear();

    G_EffectHeap.Shrink();
    G_EffectHeapSecond.Shrink();
}

void* EffectAlloc(unsigned Size)
{
    // FUN_004f79a0: size 0x84 from the pool DAT_00674ccc, anything else malloc.
    if (Size != 0x84u)
        return malloc(Size);
    return G_EffectHeap.Malloc();
}

void EffectFree(void* Ptr, unsigned Size)
{
    // FUN_004f79c0.
    if (Size != 0x84u)
    {
        free(Ptr);
        return;
    }
    G_EffectHeap.Free(Ptr);
}

void FEffectBase::operator delete(void* Memory, size_t Size)
{
    EffectFree(Memory, (unsigned)Size);
}

FEffectBase::FEffectBase(unsigned TypeId)
{
    // FUN_004f79f0 — every dword through +0x68.
    std::memset((unsigned char*)this + sizeof(void*), 0, sizeof(FEffectBase) - sizeof(void*));
    Priority = 1.0f;
    // Identity rotation.
    LocalXform.Rotation.E[0][0] = 1.0f;
    LocalXform.Rotation.E[1][1] = 1.0f;
    LocalXform.Rotation.E[2][2] = 1.0f;
    EngineIndex = -1;
    RefCount = 1;
    EffectTypeId = TypeId;
    LinkPrev = nullptr;
    LinkNext = nullptr;
    LinkSelf = nullptr;
}

void FEffectBase::AddRef()
{
    FEffectBase* const This = this;
    // FUN_004f7a80.
    This->RefCount = This->RefCount + 1;
}

int FEffectBase::Release()
{
    FEffectBase* const This = this;
    // FUN_004f7a90: a signed count below 1 runs the scalar deleting dtor (slot +0x20).
    int const Next = static_cast<int>(This->RefCount) - 1;
    This->RefCount = static_cast<unsigned>(Next);
    if (Next < 1)
    {
        delete This;
    }
    return Next;
}

unsigned char FEffectBase::Start(FEffectArch* Arch)
{
    FEffectBase* const This = this;
    // LAB_004f7ab0.
    if (Arch != nullptr)
    {
        // A signed test on the type id.
        if (static_cast<int>(This->EffectTypeId) <= 0)
            This->EffectTypeId = *(unsigned const*)Arch;
        // Retail's own null test.
        FEffectTypeInfo const* const Info =
            EffectTypeInfoByIndex(static_cast<int>(This->EffectTypeId));
        if (Info != nullptr)
            This->Priority = Info->Priority;
    }
    return 1;
}

FEffectBase::~FEffectBase()
{
    // FUN_004f8dd0: only a non-empty watcher is reset.
    if (Watcher.target() != nullptr)
        Watcher.set(nullptr);
}

void FEffectBase::SetIntensity(float /*Intensity*/)
{
    // Slot +0x0c: empty.
}

void FEffectBase::Attach(void* WatchableOwner, int EngineIndex, Transform const* LocalXform)
{
    FEffectBase* const This = this;
    // FUN_004f7d20. No null test on the transform.
    Watchable const* WatchablePtr = nullptr;
    if (WatchableOwner != nullptr)
        WatchablePtr =
            reinterpret_cast<Watchable const*>(static_cast<unsigned char*>(WatchableOwner) + 8);
    if (WatchablePtr != This->Watcher.target())
        This->Watcher.set(WatchablePtr);
    This->EngineIndex = EngineIndex;
    This->LocalXform = *LocalXform;
    // The frozen velocity +0x4c..+0x54.
    std::memset(This->FrozenVelocity, 0, sizeof(This->FrozenVelocity));
}

void* EffectWatchedObject(FEffectBase const* This)
{
    // Both an empty watcher and one holding exactly 8 read as "no object".
    unsigned char const* const Held =
        reinterpret_cast<unsigned char const*>(This->Watcher.target());
    if (Held == nullptr)
        return nullptr;
    return const_cast<unsigned char*>(Held - 8);
}

namespace
{
    // DAT_00674f50, lazily filled at every use site.
    I3DMathEngine* EffectMathEngine()
    {
        if (G_EffectMathEngine == nullptr)
            G_EffectMathEngine = Acquire3DMathEngine();
        return G_EffectMathEngine;
    }

    // The body FUN_004f7db0 runs once it knows the index is set and the watcher holds an
    // object; FUN_004f7bd0 and LAB_004f84d0 carry the same instructions inlined.
    void EffectBakeInstance(FEffectBase* This)
    {
        const InstanceIndex Instance = This->EngineIndex;

        // The instance's CObject velocity into +0x4c.
        float* const Velocity = reinterpret_cast<float*>(This->FrozenVelocity);
        CObject* const Object = static_cast<CObject*>(DALib::Engine->GetInstanceUserData(Instance));
        if (Object != nullptr)
        {
            Vector const V = Object->get_velocity();
            Velocity[0] = V.x;
            Velocity[1] = V.y;
            Velocity[2] = V.z;
        }
        else
        {
            Velocity[2] = 0.0f;   // +0x54, +0x50, +0x4c: retail's store order
            Velocity[1] = 0.0f;
            Velocity[0] = 0.0f;
        }

        // The watcher and the index are tested again (the object may have been the one the
        // velocity call just touched); only then is the instance transform multiplied in.
        Transform Product;
        Transform const* Source = &This->LocalXform;
        if (EffectWatchedObject(This) != nullptr && This->EngineIndex != -1)
        {
            Transform const* const InstanceXform = DALib::Engine->GetInstanceTransform(
                    This->EngineIndex);
            EffectMathEngine()->MulTransform(Product, *InstanceXform, This->LocalXform);
            Source = &Product;
        }

        // Two 0xc-dword copies through a stack temporary: source -> temp -> +0x10.
        Transform const Temp = *Source;
        This->LocalXform = Temp;
        This->EngineIndex = -1;
    }
}

void FEffectBase::FreezeToWorld()
{
    // FUN_004f7db0.
    if (EngineIndex == -1)
        return;

    if (EffectWatchedObject(this) == nullptr)
    {
        // FUN_004f7af0 inlined.
        EffectTypeListLink(this);
        // Retail quirk kept: it calls itself with the index still set and the watcher still
        // empty, so it recurses until the stack runs out if this is reached.
        FreezeToWorld();
        Flags08 = static_cast<unsigned char>(Flags08 | kEffectFlagDeactivated);
        return;
    }

    EffectBakeInstance(this);
}

void FEffectBase::Kill()
{
    FEffectBase* const This = this;
    // FUN_004f7bd0.
    EffectTypeListLink(This);      // inlined in retail

    if (This->EngineIndex != -1)
    {
        if (EffectWatchedObject(This) == nullptr)
        {
            // The same unbounded self-recursion as FUN_004f7db0's; kept as retail.
            This->FEffectBase::Kill();
            This->Flags08 = static_cast<unsigned char>(This->Flags08 | kEffectFlagDeactivated);
            return;
        }
        EffectBakeInstance(This);
    }

    This->Flags08 = static_cast<unsigned char>(This->Flags08 | kEffectFlagDeactivated);
}

void FEffectBase::GetWorldTransform(Transform* Out) const
{
    // FUN_004f7ef0.
    if (EffectWatchedObject(this) != nullptr && EngineIndex != -1)
    {
        Transform const* const InstanceXform = DALib::Engine->GetInstanceTransform(
                EngineIndex);
        Transform Product;
        EffectMathEngine()->MulTransform(Product, *InstanceXform, LocalXform);
        *Out = Product;
        return;
    }
    *Out = LocalXform;
}

unsigned char FEffectBase::InCameraView(FSpaceCamera* Cam)
{
    // FUN_004f7b50. The effect's own slot +8 gives the world position and radius.
    Vector WorldPos;
    float const Radius = GetWorldPos(&WorldPos);

    ICamera* const CamIface = AsCamera(Cam);
    Transform const* const ViewXform =
        CamIface->GetViewTransform();
    Vector ViewPos;
    EffectMathEngine()->TransformXform(ViewPos, *ViewXform, WorldPos);

    // The camera's sphere classification (slot +0x3c); 3 and up is visible.
    Vector Centre = ViewPos;
    int const Class = CamIface->ClassifySphere(&Centre, Radius);
    return static_cast<unsigned char>(Class >= kSphereStraddling ? 1 : 0);
}
