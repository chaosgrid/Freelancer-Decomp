// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Common/Graphics/client_mesh_load.h"

#include "DALib.h"
#include "common_archetype.h"
#include "common_cobject.h" // CompoundInstanceAnalyze
#include "common_csys.h"    // Acquire3DMathEngine (FUN_005b73e0)
#include "x86math.h"        // I3DMathEngine::TransformXform
#include "dacom.h"
#include "readfile.h" // IFileSystem
#include "engine/engbase.h"
#include "render/rendcomp.h" // IRenderer2
#include "render/RenderPipeline.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cmath>
#include <vector>
#include <windows.h>

namespace
{
    // FUN_0059ec60 (cdecl Instance, Context) -- the CompoundInstanceAnalyze callback of
    // FUN_0059ec80.
    void __cdecl MeshAnalyzeSetUserData(long Instance, void* Context)
    {
        DALib::Engine->SetInstanceUserData(Instance, Context);
    }

    // Inlined three times in FUN_0059ec80: the holder's floats +0x14 are the context.
    void AnalyzeBoundInstance(FClientMeshHolder* Holder)
    {
        long const Inst = Holder->EngineInstance;
        CompoundInstanceAnalyze(Inst, MeshAnalyzeSetUserData, Holder->UserFloats);
    }

    // The DALib::Data FileSystem description FUN_0059ec80 and FUN_0059e980 build on the stack
    // (no backend name).
    void BuildMeshFileSystemDesc(FFileSystemAggDesc* Desc, char const* Path)
    {
        memset(Desc, 0, sizeof(*Desc));
        Desc->Size = 0x34;
        Desc->ComponentName = "FileSystem";
        Desc->DataPath = Path;
        Desc->Access = GENERIC_READ;
        Desc->ShareMode = FILE_SHARE_READ;
        Desc->Creation = OPEN_EXISTING;
        Desc->Flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN;
        Desc->FindHandle = 0xffffffffu;
    }

    // DAT_0067dcf4 -- the Win3D model cache. Constructed empty by the static initializer
    // LAB_0059ec10 (FUN_0059ec40), freed at exit by LAB_0059ec30 (FUN_004de730) without
    // releasing anything; FUN_0059e8d0 is what releases the entries.
    std::vector<FWin3DModelCacheEntry> G_Win3DModelCache;

    // The linear search FUN_0059ec80 and FUN_0059e980 inline.
    FWin3DModelCacheEntry* FindModelCacheEntry(ArchetypeIndex ArchHandle)
    {
        size_t const Count = G_Win3DModelCache.size();
        for (size_t Index = 0; Index < Count; ++Index)
        {
            if (G_Win3DModelCache[Index].ArchHandle == ArchHandle)
                return &G_Win3DModelCache[Index];
        }
        return nullptr;
    }

    // FUN_0059ebf0 + FUN_0059f5b0 / FUN_00470c90: append {ArchHandle, 0, 0}.
    void AppendModelCacheEntry(ArchetypeIndex ArchHandle)
    {
        FWin3DModelCacheEntry Entry;
        Entry.ArchHandle = ArchHandle;
        Entry.MaterialEntry = nullptr;
        Entry.AuxMaterialEntry = nullptr;
        G_Win3DModelCache.push_back(Entry);
    }
}

FMeshHolder::~FMeshHolder()
{
    // ObjectHandle only changes its vptr; instance/material ownership is released explicitly.
}

void ClientMeshHolderConstruct(FClientMeshHolder* Holder)
{
    // FUN_00420d40 + FUN_00492250 on +0x14 + the Object3D vptr + +0x10 = -1: the inlined
    // Object3D constructor sequence. None of the retail sites tests the holder for null.
    Holder->EngineInstance = InvalidInstanceIndex;
    Holder->MaterialEntry = nullptr;
    Holder->AuxMaterialEntry = nullptr;
    Holder->AnimOrArchIndex = -1;
    Holder->UserFloats[0] = 1.0f;
    Holder->UserFloats[1] = 1.0f;
    Holder->UserFloats[2] = 1.0f;
    Holder->UserFloats[3] = 1.0f;
    Holder->UserFloats[4] = 1.0f;
}

void FMeshHolder::Construct()
{
    // FUN_00420d40 field stores; the compiler owns the existing object's vptr.
    EngineInstance = InvalidInstanceIndex;
    MaterialEntry = nullptr;
    AuxMaterialEntry = nullptr;
}

bool FMeshHolder::HasInstance() const
{
    return EngineInstance != InvalidInstanceIndex;
}

void FMeshHolder::Release()
{
    if (EngineInstance != InvalidInstanceIndex)
        DALib::Engine->DestroyInstance(EngineInstance);
    EngineInstance = InvalidInstanceIndex;
    Archetype::ReleaseMaterialEntrySlot(&MaterialEntry);
    Archetype::ReleaseMaterialEntrySlot(&AuxMaterialEntry);
}

FClientMeshHolder::~FClientMeshHolder()
{
    // FUN_0059ef20: FUN_0059eee0's body inlined, then FUN_00420d10, which only resets the vptr.
    ReleaseWithAnim();
}

void FClientMeshHolder::ReleaseWithAnim()
{
    if (AnimOrArchIndex != -1)
    {
        DALib::Anim->CloseScript(reinterpret_cast<void*>(AnimOrArchIndex));
        AnimOrArchIndex = -1;
    }
    if (HasInstance())
        Release();
}

bool FMeshHolder::Load(char const* Path, char const* AuxPath)
{
    // FUN_00420dd0 (thiscall, RET 8).
    bool bOk = false;

    // The DALib::OpenData-shaped FileSystem desc with no backend name.
    FFileSystemAggDesc Desc;
    memset(&Desc, 0, sizeof(Desc));
    Desc.Size = 0x34;
    Desc.ComponentName = "FileSystem";
    Desc.DataPath = Path;
    Desc.Access = GENERIC_READ;
    Desc.ShareMode = FILE_SHARE_READ;
    Desc.Creation = OPEN_EXISTING;
    Desc.Flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN;
    Desc.FindHandle = 0xffffffffu;

    void* FsOut = nullptr;
    if (static_cast<IFileSystem*>(DALib::Data)
            ->CreateInstance(reinterpret_cast<AGGDESC*>(&Desc), &FsOut) == 0)
    {
        if (Path != nullptr)
            MaterialEntry = Archetype::ResolveMaterialEntry(Path, true, 0xffffffffu);
        if (AuxPath != nullptr)
            AuxMaterialEntry = Archetype::ResolveMaterialEntry(AuxPath, true, 0xffffffffu);

        long Handle = -1;
        bOk = Archetype::Load(static_cast<IFileSystem*>(FsOut), &Handle, nullptr);
        if (bOk)
        {
            // Retail quirk: an instance already bound is destroyed and BOTH cache slots
            // released -- including the two entries resolved just above.
            if (EngineInstance != InvalidInstanceIndex)
            {
                DALib::Engine->DestroyInstance(EngineInstance);
                EngineInstance = InvalidInstanceIndex;
                Archetype::ReleaseMaterialEntrySlot(&MaterialEntry);
                Archetype::ReleaseMaterialEntrySlot(&AuxMaterialEntry);
            }
            const ArchetypeIndex HandlePtr = Handle;
            EngineInstance = DALib::Engine->CreateInstance(HandlePtr, nullptr);
            DALib::Engine->ReleaseHandle(HandlePtr);
        }
    }

    if (FsOut != nullptr)
        static_cast<IFileSystem*>(FsOut)->Release();
    return bOk;
}

void FMeshHolder::SetMaterials(char const* Path, char const* AuxPath)
{
    // FUN_00420f30.
    if (Path != nullptr)
        MaterialEntry = Archetype::ResolveMaterialEntry(Path, true, 0xffffffffu);
    if (AuxPath != nullptr)
        AuxMaterialEntry = Archetype::ResolveMaterialEntry(AuxPath, true, 0xffffffffu);
}

bool FMeshHolder::BindInstance(ArchetypeIndex ArchHandle)
{
    // FUN_00420d70. A bound instance takes both material entries with it.
    if (EngineInstance != InvalidInstanceIndex)
    {
        DALib::Engine->DestroyInstance(EngineInstance);
        EngineInstance = InvalidInstanceIndex;
        Archetype::ReleaseMaterialEntrySlot(&MaterialEntry);
        Archetype::ReleaseMaterialEntrySlot(&AuxMaterialEntry);
    }
    EngineInstance = DALib::Engine->CreateInstance(ArchHandle, nullptr);
    return EngineInstance != InvalidInstanceIndex;
}

const Transform* FMeshHolder::GetTransform() const
{
    // FUN_00421030.
    return DALib::Engine->GetInstanceTransform(EngineInstance);
}

namespace
{
    // DAT_00616870 -- FUN_00421090's lazily acquired math engine (no other reader in the PE).
    I3DMathEngine* G_MeshHolderMathEngine = nullptr;
}

float FMeshHolder::GetBoundingSphere(float* OutCenter) const
{
    // FUN_00421090.
    float Radius = 0.0f;
    DALib::Engine->GetInstanceExtent(EngineInstance, 0, &Radius, (Vector*)OutCenter);
    const Transform* const InstanceXform = DALib::Engine->GetInstanceTransform(EngineInstance);
    if (G_MeshHolderMathEngine == nullptr)
        G_MeshHolderMathEngine = Acquire3DMathEngine();
    Vector World;
    G_MeshHolderMathEngine->TransformXform(World, *InstanceXform,
                                           *reinterpret_cast<Vector const*>(OutCenter));
    OutCenter[0] = World.x;
    OutCenter[1] = World.y;
    OutCenter[2] = World.z;
    return Radius;
}

void FMeshHolder::SetMatrix(const Transform* Xform)
{
    // FUN_00421050.
    DALib::Engine->SetInstanceMatrix(EngineInstance, Xform);
}

void FMeshHolder::Render(void* Camera, unsigned long Flags)
{
    // FUN_00421120.
    DALib::Renderer->RenderInstance(Camera, EngineInstance, 1.0f, Flags, nullptr, nullptr);
}

void FMeshHolder::SetPosition(Vector const* Position)
{
    // FUN_00420fd0.
    DALib::Engine->SetInstancePosition(EngineInstance, (const Vector*)Position);
}

const Vector* FMeshHolder::GetPosition() const
{
    // FUN_00420fb0.
    return DALib::Engine->GetInstancePosition(EngineInstance);
}

const Matrix* FMeshHolder::GetOrientation() const
{
    // FUN_00420ff0.
    return DALib::Engine->GetInstanceOrientation(EngineInstance);
}

void FMeshHolder::SetOrientation(float const Orientation[9])
{
    // FUN_00421010.
    DALib::Engine->SetInstanceOrientation(EngineInstance,
                                          (const Matrix*)(const_cast<float*>(Orientation)));
}

void FMeshHolder::Update()
{
    // FUN_00421070.
    DALib::Engine->UpdateInstance(EngineInstance, 0, nullptr);
}

void FClientMeshHolder::SetColor(unsigned char const* Rgba)
{
    // FUN_004a1af0: float 1/255 is DAT_005c7f68. UserFloats[3] (+0x20) is not written.
    double const Inv255 = static_cast<double>(0.003921568859368563f);
    UserFloats[0] = static_cast<float>(static_cast<double>(Rgba[0]) * Inv255);
    UserFloats[1] = static_cast<float>(static_cast<double>(Rgba[1]) * Inv255);
    UserFloats[2] = static_cast<float>(static_cast<double>(Rgba[2]) * Inv255);
    UserFloats[4] = static_cast<float>(static_cast<double>(Rgba[3]) * Inv255);
}

bool FClientMeshHolder::LoadFromPath(char const* RelPath, char const* MaterialPath,
                                     bool bRegisterCache)
{
    // FUN_0059ec80 (thiscall on the holder, RET 0xc).
    FEngineName Name = {};
    Name.Text = RelPath;
    Name.Crc = DACOM_CRC::GetCRC32(RelPath);
    const ArchetypeIndex Found = DALib::Engine->FindArchetype(&Name);
    if (Found != InvalidArchetypeIndex)
    {
        // Already loaded: no FileSystem, no material entries.
        if (BindInstance(Found))
            AnalyzeBoundInstance(this);
        DALib::Engine->ReleaseHandle(Found);
        return HasInstance();
    }

    // The FileSystem is an EH-guarded local released whatever happened.
    FFileSystemAggDesc Desc;
    BuildMeshFileSystemDesc(&Desc, RelPath);
    void* FsOut = nullptr;
    if (static_cast<IFileSystem*>(DALib::Data)
            ->CreateInstance(reinterpret_cast<AGGDESC*>(&Desc), &FsOut) == 0)
    {
        // FUN_00420f30: the mesh's own material entry and the material library's.
        SetMaterials(RelPath, MaterialPath);

        // Archetype::Load(Fs, &handle, holder +0x10) on both branches; the handle lands in
        // retail's own RelPath argument slot.
        long LoadedHandle = -1;
        int* const AnimIndex = reinterpret_cast<int*>(&AnimOrArchIndex);
        if (bRegisterCache)
        {
            // A loaded handle is kept in the model cache and NOT released here (FUN_0059e8d0
            // does that).
            if (Archetype::Load(static_cast<IFileSystem*>(FsOut), &LoadedHandle, AnimIndex))
            {
                const ArchetypeIndex Loaded = LoadedHandle;
                if (FindModelCacheEntry(Loaded) == nullptr)
                    AppendModelCacheEntry(Loaded);
                if (BindInstance(Loaded))
                    AnalyzeBoundInstance(this);
            }
        }
        else
        {
            if (Archetype::Load(static_cast<IFileSystem*>(FsOut), &LoadedHandle, AnimIndex))
            {
                const ArchetypeIndex Loaded = LoadedHandle;
                if (BindInstance(Loaded))
                    AnalyzeBoundInstance(this);
                DALib::Engine->ReleaseHandle(Loaded);
            }
        }
    }

    bool const bResult = HasInstance();
    if (FsOut != nullptr)
        static_cast<IFileSystem*>(FsOut)->Release();
    return bResult;
}

bool Win3DModelCachePreload(char const* Path, char const* AuxPath)
{
    // FUN_0059e980 (thiscall on DAT_0067dcf4, RET 8).
    FEngineName Name = {};
    Name.Text = Path;
    Name.Crc = DACOM_CRC::GetCRC32(Path);
    const ArchetypeIndex Found = DALib::Engine->FindArchetype(&Name);
    if (Found != InvalidArchetypeIndex)
    {
        // Not cached yet: the FindArchetype handle itself is kept (FUN_00470c90). True either
        // way.
        if (FindModelCacheEntry(Found) == nullptr)
        {
            AppendModelCacheEntry(Found);
            return true;
        }
        DALib::Engine->ReleaseHandle(Found);
        return true;
    }

    bool bResult = false;
    FFileSystemAggDesc Desc;
    BuildMeshFileSystemDesc(&Desc, Path);
    void* FsOut = nullptr;
    if (static_cast<IFileSystem*>(DALib::Data)
            ->CreateInstance(reinterpret_cast<AGGDESC*>(&Desc), &FsOut) == 0)
    {
        IFileSystem* const Filesys = static_cast<IFileSystem*>(FsOut);
        // FUN_0041ade0 reads the mesh's own materials from the file system already open;
        // FUN_0041aa50 takes AuxPath as is.
        Archetype::FCliMaterialEntry* const Material =
            Archetype::ResolveMaterialEntryFromFs(Filesys, Path, true, 0xffffffffu);
        Archetype::FCliMaterialEntry* const AuxMaterial =
            Archetype::ResolveMaterialEntry(AuxPath, true, 0xffffffffu);

        long LoadedHandle = -1;
        int AnimIndex = -1;
        if (Archetype::Load(Filesys, &LoadedHandle, &AnimIndex))
        {
            const ArchetypeIndex Loaded = LoadedHandle;
            if (FindModelCacheEntry(Loaded) == nullptr)
                AppendModelCacheEntry(Loaded);
            // Every entry of that handle takes both material entries (whatever it held before
            // is overwritten, not released).
            size_t const Count = G_Win3DModelCache.size();
            for (size_t Index = 0; Index < Count; ++Index)
            {
                if (G_Win3DModelCache[Index].ArchHandle == Loaded)
                {
                    G_Win3DModelCache[Index].MaterialEntry = Material;
                    G_Win3DModelCache[Index].AuxMaterialEntry = AuxMaterial;
                }
            }
            bResult = true;
        }
    }

    if (FsOut != nullptr)
        static_cast<IFileSystem*>(FsOut)->Release();
    return bResult;
}

void Win3DModelCacheClear()
{
    // FUN_0059e8d0 (thiscall on DAT_0067dcf4). FUN_0041b030 releases the material slots.
    size_t const Count = G_Win3DModelCache.size();
    for (size_t Index = 0; Index < Count; ++Index)
    {
        FWin3DModelCacheEntry& Entry = G_Win3DModelCache[Index];
        if (Entry.ArchHandle != InvalidArchetypeIndex)
        {
            DALib::Engine->ReleaseHandle(Entry.ArchHandle);
            Entry.ArchHandle = InvalidArchetypeIndex;
        }
        Archetype::ReleaseMaterialEntrySlot(&Entry.MaterialEntry);
        Archetype::ReleaseMaterialEntrySlot(&Entry.AuxMaterialEntry);
    }
    // The storage is kept.
    G_Win3DModelCache.clear();
}

bool FClientMeshHolder::QueryAabb(FClientMeshAabb* OutAabb)
{
    // FUN_0059f0d0 (thiscall on the holder, RET 4). The only test is the instance against -1;
    // OutAabb is not cleared, and the Renderer's answer is not looked at.
    if (!HasInstance())
        return false;

    ArchetypeIndex Arch = DALib::Engine->GetInstanceArchetype(EngineInstance);
    // IRenderer2 slot 4 (+0x10).
    DALib::Renderer->GetBounds(Arch, 1.0f, OutAabb->Values);
    DALib::Engine->ReleaseHandle(Arch);
    return true;
}
