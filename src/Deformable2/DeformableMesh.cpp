// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Deformable2.h"
#include "EyeMaterial.h"
#include "common_misc.h"
#include "msvcrt_compat.h"

#include <cstddef>
#include <cstdarg>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <vector>


// Source paths used by diagnostics.
static const char* const G_FdumpFileDeformableMesh =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Deformable2\\DeformableMesh.cpp";


// DAT_06603158: the mesh cache -- an STL map from CRC32(name) to the mesh (FUN_065f2a40 find,
// FUN_065f2ab0 insert, FUN_065f3440/3320 + FUN_065f2cc0 erase). Both kinds of FDeformMesh
// live in it. The port uses the current std::map; no VC6 layout is kept.
static std::map<unsigned long, FDeformMesh*> G_MeshCache;

// FUN_065f26f0.
FDeformMesh* LoadDeformMesh(FLodHolder* Holder, IFileSystem* MeshFs, const char* Name,
                            unsigned char bSkinned)
{
    const unsigned long Key = DACOM_CRC::GetCRC32(Name);
    std::map<unsigned long, FDeformMesh*>::iterator Found = G_MeshCache.find(Key);
    if (Found != G_MeshCache.end())
    {
        FDeformMesh* Cached = Found->second;
        Cached->AddRef();
        return Cached;
    }

    FDeformMesh* Result = nullptr;
    FMeshArchetype* Mesh = new FMeshArchetype(Name);
    if (Mesh->LoadTables(Holder, MeshFs))
    {
        Mesh->bSkinned = bSkinned;
        Result = Mesh;
    }
    else
    {
        Mesh->Release();
        FEngineName ArchName;
        ArchName.Text = Name;
        ArchName.Crc = DACOM_CRC::GetCRC32(Name);
        void* Handle = G_Engine->CreateArchetypeHandle(&ArchName, MeshFs);
        IVMeshPart* Library = nullptr;
        // This query ABI accepts the const IID as an opaque mutable token.
        if (G_Engine->QueryEngineComponent((int)((intptr_t)Handle),
                                           const_cast<char*>(Iids::VMeshLibrary),
                                           (void**)(&Library)) < 0)
        {
            G_Engine->ReleaseHandle(Handle);
        }
        else
        {
            Result = new FVmeshWrapper(Name, Handle, Library);
        }
        if (Library != nullptr)
            Library->Release();
    }

    if (Result == nullptr)
    {
        char Message[0x2000];
        FDUMP(kFdumpWarning, "%s(%d) : WARNING:General:%s", G_FdumpFileDeformableMesh, 0x39,
              FormatDeformMessage(Message, "Deformable: Could not load mesh %s", Name));
        return nullptr;
    }
    G_MeshCache[Key] = Result;
    return Result;
}

// PTR_LAB_06601378 -- the base's slots.
FDeformMesh::FDeformMesh(const char* Name)
    : RefCount(1)
    , NameCrc(DACOM_CRC::GetCRC32(Name))
{
}

// FUN_065f28b0 (cdecl): _vsnprintf(Buffer, 0x2000, Format, ...) and return Buffer. Every
// caller hands it a 0x2000-byte stack buffer.
char* FormatDeformMessage(char* Buffer, const char* Format, ...)
{
    va_list Args;
    va_start(Args, Format);
    _vsnprintf(Buffer, 0x2000, Format, Args);
    va_end(Args);
    return Buffer;
}


// FUN_065f28e0.
void FDeformMesh::AddRef()
{
    RefCount += 1;
}

// FUN_065f28f0.
int FDeformMesh::Release()
{
    RefCount -= 1;
    const int Remaining = RefCount;
    if (Remaining > 0)
        return Remaining;
    G_MeshCache.erase(NameCrc);
    delete this;
    return Remaining;
}
