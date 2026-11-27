// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Common/Graphics/part_cache.h"

#include "DALib.h"
#include "common_csys.h"
#include "common_misc.h"               // ICamera
#include "dacom.h"
#include "dacom_iids.h"
#include "engine/engbase.h"
#include "render/IRenderPipeline.h"  // IVertexBufferManager
#include "render/mesh_iface.h"
#include "render/rendcomp.h"
#include "render/slots.h"          // Render::EPipelineState
#include "x86math.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace
{
    // DAT_0061649c -- this unit's lazily filled I3DMathEngine (FUN_00405fb0, FUN_00406230 and
    // FUN_00406510 are its only users).
    I3DMathEngine* G_PartCacheMath = nullptr;

    I3DMathEngine* PartCacheMath()
    {
        if (G_PartCacheMath == nullptr)
            G_PartCacheMath = Acquire3DMathEngine();
        return G_PartCacheMath;
    }

    // FUN_00526a20: release the held interface, if any, and clear the slot.
    void ReleaseCom(IDAComponent** Pp)
    {
        IDAComponent* Component = *Pp;
        if (Component != nullptr)
        {
            Component->Release();
            *Pp = nullptr;
        }
    }

    // The stack holder FUN_00405b10 keeps its QueryInstance results in: EH-registered, so its
    // destructor (FUN_00526a20) also runs on the unwind path.
    struct FPartComHolder
    {
        IDAComponent* Ptr = nullptr;

        FPartComHolder() = default;
        FPartComHolder(FPartComHolder const&) = delete;
        FPartComHolder& operator=(FPartComHolder const&) = delete;
        ~FPartComHolder()
        {
            ReleaseCom(&Ptr);
        }
    };

    void FillUshortOnes(unsigned short* Dest, unsigned Count)
    {
        unsigned ByteCount = Count * 2U;
        unsigned DwordCount = ByteCount / 4U;
        unsigned* Dwords = reinterpret_cast<unsigned*>(Dest);
        unsigned Index = 0;
        for (; Index < DwordCount; ++Index)
            Dwords[Index] = 0xFFFFFFFFu;
        unsigned char* Bytes = reinterpret_cast<unsigned char*>(Dwords + DwordCount);
        unsigned Rem = ByteCount & 3U;
        unsigned ByteIndex = 0;
        for (; ByteIndex < Rem; ++ByteIndex)
            Bytes[ByteIndex] = 0xFFu;
    }

    void MakeIdentityTransform(Transform* Out)
    {
        std::memset(Out, 0, sizeof(Transform));
        Out->Rotation.E[0][0] = 1.0f;
        Out->Rotation.E[1][1] = 1.0f;
        Out->Rotation.E[2][2] = 1.0f;
    }
}

// FUN_004061a0
int FvfTexcoordBlockBytes(unsigned Fvf)
{
    int Total = 0;
    unsigned SetCount = (Fvf >> 8) & 0xFu;
    if (SetCount != 0)
    {
        unsigned Shift = 0x10;
        unsigned Remaining = SetCount;
        do
        {
            unsigned Code = (Fvf >> (Shift & 0x1Fu)) & 3U;
            if (Code == D3DFVF_TEXTUREFORMAT2)
                Total += 8;
            else if (Code == D3DFVF_TEXTUREFORMAT3)
                Total += 0xC;
            else if (Code == D3DFVF_TEXTUREFORMAT4)
                Total += 0x10;
            else if (Code == D3DFVF_TEXTUREFORMAT1)
                Total += 4;
            Shift += 2;
            Remaining -= 1;
        } while (Remaining != 0);
    }
    return Total;
}

// FUN_00406880
void CopyTransform12(Transform* Dest, Transform const* Source)
{
    unsigned* Dst = reinterpret_cast<unsigned*>(Dest);
    unsigned const* Src = reinterpret_cast<unsigned const*>(Source);
    int Index = 0xC;
    for (; Index != 0; Index -= 1)
    {
        *Dst = *Src;
        Src += 1;
        Dst += 1;
    }
}

// FUN_004058b0 (thiscall, no test of the entry): free +4, clear it, free +0xc, clear it.
void FPartCacheEntry::Destroy()
{
    operator delete(Vertices);
    Vertices = nullptr;
    operator delete(Indices);
    Indices = nullptr;
}

// LAB_00405f80
void PartCacheEntryConstruct(FPartCacheEntry* Entry)
{
    Entry->VertexCount = 0;
    Entry->Vertices = nullptr;
    Entry->IndexCount = 0;
    Entry->Indices = nullptr;
    Entry->InstanceId = InvalidInstanceIndex;
    Entry->Tag = 0xFFFFFFFFu;
    Entry->bHidden = 0;
    Entry->bActive = 1;
    Entry->Field1A = 0;
}

// FUN_004058e0 (thiscall, no test of the cache): the count-prefixed entry array through the
// vector destructor iterator (FUN_004058b0 per 0x1c record), then the merged index buffer.
void FPartCache::Clear()
{
    FPartCacheEntry* Entries = this->Entries;
    if (Entries != nullptr)
    {
        unsigned* Prefix = reinterpret_cast<unsigned*>(Entries) - 1;
        unsigned Count = *Prefix;
        unsigned Index = 0;
        for (; Index < Count; ++Index)
            (Entries + Index)->Destroy();
        operator delete(Prefix);
    }
    operator delete(MergedIndices);
    this->Entries = nullptr;
    PartCount = 0;
    TotalIndices = 0;
    TotalVertices = 0;
    MergedIndices = nullptr;
}

// FUN_00405fb0 (thiscall on the entry, RET 8: Wire, Xform). The wire is used untested.
void FPartCacheEntry::BuildFromWire(IVMeshWire* Wire, Transform const* Xform)
{
    unsigned char* HeaderBytes = static_cast<unsigned char*>(Wire->GetMeshData());
    unsigned char* PartBytes = static_cast<unsigned char*>(Wire->GetWireData());
    if (HeaderBytes == nullptr || PartBytes == nullptr)
        return;

    FVmeshDataHeader const* MeshHeader = reinterpret_cast<FVmeshDataHeader*>(HeaderBytes);
    FVwireDataHeader const* PartHeader = reinterpret_cast<FVwireDataHeader*>(PartBytes);

    unsigned RemapCount = PartHeader->RemapCount;
    VertexCount = RemapCount;
    if (RemapCount == 0)
        return;

    unsigned Fvf = MeshHeader->Fvf;
    unsigned ExtraPos = 0;
    if ((Fvf & D3DFVF_XYZB4) == D3DFVF_XYZB4)
        ExtraPos = 0x1Cu;
    else if ((Fvf & D3DFVF_XYZRHW) == D3DFVF_XYZRHW)
        ExtraPos = 0x10U;
    else if ((Fvf & D3DFVF_XYZ) == D3DFVF_XYZ)
        ExtraPos = 0xCu;

    unsigned ExtraUv2 = 0;
    if ((Fvf & D3DFVF_NORMAL) != 0)
        ExtraUv2 = 0xCu;

    unsigned ExtraTail = 0;
    if ((Fvf & D3DFVF_DIFFUSE) != 0)
        ExtraTail = 4;
    if ((Fvf & D3DFVF_SPECULAR) != 0)
        ExtraTail += 4;

    int TexBytes = FvfTexcoordBlockBytes(Fvf);
    unsigned Stride = static_cast<unsigned>(TexBytes) + ExtraTail + ExtraUv2 + ExtraPos;

    Vertices = static_cast<FPartCacheVertex*>(
        operator new(static_cast<std::size_t>(VertexCount) << 4));
    IndexCount = PartHeader->Count;
    Indices = static_cast<unsigned short*>(
        operator new(static_cast<std::size_t>(IndexCount) * 2U));

    unsigned short const HeaderVerts = MeshHeader->Vertices;
    unsigned short* Remap = static_cast<unsigned short*>(
        operator new(static_cast<std::size_t>(HeaderVerts) * 2U));
    FillUshortOnes(Remap, HeaderVerts);

    unsigned short FaceGroups = MeshHeader->FaceGroups;
    unsigned short MeshIndices = MeshHeader->Indices;
    unsigned char const* VertexPool =
        HeaderBytes + 0x10
        + (static_cast<unsigned>(MeshIndices) + static_cast<unsigned>(FaceGroups) * 6U) * 2U;

    unsigned short Compacted = 0;
    unsigned IndexIter = 0;
    unsigned short const* PartIndices =
        reinterpret_cast<unsigned short const*>(PartBytes + 0x10);

    if (IndexCount != 0)
    {
        do
        {
            unsigned short RawIndex = PartIndices[IndexIter];
            unsigned short IbBias = PartHeader->IbBias;
            unsigned short* RemapSlot = Remap + RawIndex;
            unsigned short Mapped = *RemapSlot;
            if (Mapped == 0xFFFFu)
            {
                *RemapSlot = Compacted;
                Vector const* Source = reinterpret_cast<Vector const*>(
                    VertexPool
                    + static_cast<unsigned>(static_cast<unsigned short>(IbBias + RawIndex))
                          * Stride);
                Vector Transformed;
                PartCacheMath()->TransformXform(Transformed, *Xform, *Source);
                FPartCacheVertex* OutVert = Vertices + Compacted;
                OutVert->X = Transformed.x;
                OutVert->Y = Transformed.y;
                OutVert->Z = Transformed.z;
                OutVert->Tag = Tag;
                Mapped = Compacted;
                Compacted = static_cast<unsigned short>(Compacted + 1);
            }
            Indices[IndexIter] = Mapped;
            IndexIter += 1;
        } while (IndexIter < IndexCount);
    }

    operator delete(Remap);
}

// FUN_00406230 (thiscall on the entry, RET 8: Mesh, Xform). Run twice: a counting pass (no
// index buffer yet) and a filling pass. The Cell* locals are out-arguments the code never
// reads.
unsigned FPartCacheEntry::BuildFromMesh(IMesh* Mesh, Transform const* Xform)
{
    unsigned short* Remap = nullptr;
    unsigned ReturnCursor = 0;
    unsigned Compacted = 0;
    unsigned WriteCursor = 0;
    float* Positions = nullptr;          // written only by the prologue

    // Only while IndexCount is set and the index buffer is not.
    if (this->IndexCount != 0 && Indices == nullptr)
    {
        Indices = static_cast<unsigned short*>(
            operator new(static_cast<std::size_t>(this->IndexCount) * 2U));
        Mesh->OpenFaceGroup(0, kMeshOpenRead);
        Mesh->OpenFaceGroupChannels(kMeshChannelEdgeIndices);

        void* CellB4c = nullptr;
        void* CellB48 = nullptr;
        unsigned VertCap = 0;
        Mesh->GetFaceGroupData(kMeshChannelEdgeIndices, reinterpret_cast<void**>(&Positions), &CellB4c, &CellB48,
                     reinterpret_cast<void**>(&VertCap));

        Vertices = static_cast<FPartCacheVertex*>(
            operator new(static_cast<std::size_t>(VertCap) << 4));
        Mesh->CloseFaceGroup();
        Remap = static_cast<unsigned short*>(
            operator new(static_cast<std::size_t>(VertCap) * 2U));
        FillUshortOnes(Remap, VertCap);
    }

    // Both count tests are unsigned.
    int GroupCountCell = 0;
    Mesh->GetFaceGroupCount(&GroupCountCell);
    unsigned const GroupCount = static_cast<unsigned>(GroupCountCell);

    for (unsigned GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
    {
        // A negative select result skips the group (no close).
        if (Mesh->OpenFaceGroup(static_cast<int>(GroupIndex), kMeshOpenRead) < 0)
            continue;

        // -8 closes and returns 0 at once. Retail neither frees the remap table nor stores
        // VertexCount on this path.
        if (Mesh->OpenFaceGroupChannels(kMeshChannelEdgeIndices | kMeshChannelEdgeAngles) == -8)
        {
            Mesh->CloseFaceGroup();
            return 0;
        }
        Mesh->LockFaceGroupChannels(0x180U);

        float* Weights = nullptr;
        void* CellB58 = nullptr;
        void* CellB54 = nullptr;
        void* CellB50 = nullptr;
        Mesh->GetFaceGroupData(kMeshChannelEdgeAngles, reinterpret_cast<void**>(&Weights), &CellB58, &CellB54, &CellB50);

        unsigned short* WeightIndex = nullptr;
        void* CellB5c = nullptr;
        Mesh->GetFaceGroupIndices(0x100, reinterpret_cast<void**>(&WeightIndex), &CellB5c);

        unsigned short* RawIndices = nullptr;
        unsigned IndexCount = 0;
        Mesh->GetFaceGroupIndices(0x80, reinterpret_cast<void**>(&RawIndices),
                     reinterpret_cast<void**>(&IndexCount));

        // Fewer than two indices, no pairs.
        if ((IndexCount & 0xFFFFFFFEu) != 0)
        {
            unsigned Pair = 0;
            do
            {
                // Unless byte +0x1b is set, a pair weight at or below the DOUBLE 0.1
                // (DAT_005c7720) leaves the group; greater and unordered go on.
                if (bSkipWeightTest == 0)
                {
                    double const Weight = static_cast<double>(Weights[WeightIndex[Pair]]);
                    if (Weight <= 0.1)
                        break;
                }

                // The counting pass only moves the cursor.
                if (Indices != nullptr)
                {
                    unsigned PairOffset = Pair * 4U;
                    for (int PairLeft = 2; PairLeft != 0; --PairLeft)
                    {
                        unsigned const RawIndex = *reinterpret_cast<unsigned short const*>(
                            reinterpret_cast<unsigned char const*>(RawIndices) + PairOffset);
                        unsigned short Mapped = Remap[RawIndex];
                        if (Mapped == 0xFFFFu)
                        {
                            Mapped = static_cast<unsigned short>(Compacted);
                            Compacted += 1;
                            Remap[RawIndex] = Mapped;

                            Vector const* Source =
                                reinterpret_cast<Vector const*>(Positions + RawIndex * 3U);
                            Vector Transformed;
                            PartCacheMath()->TransformXform(Transformed, *Xform, *Source);

                            FPartCacheVertex* OutVert = Vertices + Mapped;
                            OutVert->X = Transformed.x;
                            OutVert->Y = Transformed.y;
                            OutVert->Z = Transformed.z;
                            OutVert->Tag = Tag;
                        }

                        Indices[WriteCursor] = Mapped;
                        WriteCursor += 1;
                        PairOffset += 2;
                    }
                }

                ReturnCursor += 2;
                Pair += 1;
            } while (Pair < (IndexCount >> 1));
        }

        // Closed after the pairs, including the weight break.
        Mesh->CloseFaceGroup();
    }

    if (Vertices != nullptr)
        VertexCount = Compacted;
    if (Remap != nullptr)
        operator delete(Remap);
    return ReturnCursor;
}

// FUN_00406200
void FPartCacheEntry::BuildFromMeshTwice(IMesh* Mesh, Transform const* Xform)
{
    unsigned First = BuildFromMesh(Mesh, Xform);
    IndexCount = First;
    BuildFromMesh(Mesh, Xform);
}

namespace
{
    // The record search FUN_00405930 / FUN_00405a00 inline for the instance and for every
    // child: PartCount compared SIGNED and re-read per pass.
    FPartCacheEntry* PartCacheFindRecord(FPartCache* Cache, InstanceIndex Instance)
    {
        int const PartCount = static_cast<int>(Cache->PartCount);
        for (int PartIndex = 0; PartIndex < PartCount; ++PartIndex)
        {
            FPartCacheEntry* Entry = Cache->Entries + PartIndex;
            if (Entry->InstanceId == Instance)
                return Entry;
        }
        return nullptr;
    }

    // FUN_00405a00's per-record body (inlined twice): VertexCount compared signed and re-read
    // per pass.
    void PartCacheActivateRecord(FPartCacheEntry* Entry, unsigned Tag)
    {
        Entry->bActive = 1;
        if (Tag == Entry->Tag)
            return;
        Entry->Tag = Tag;
        for (int VertIndex = 0; VertIndex < static_cast<int>(Entry->VertexCount); ++VertIndex)
            Entry->Vertices[VertIndex].Tag = Tag;
    }
}

// FUN_00405930 (thiscall, RET 8: Instance, bRecurseChildren). No test of the cache or of
// DALib::Engine.
void FPartCache::MarkHidden(InstanceIndex Instance, bool bRecurseChildren)
{
    const InstanceIndex kInvalid = InvalidInstanceIndex;

    FPartCacheEntry* Found = PartCacheFindRecord(this, Instance);
    if (Found != nullptr)
        Found->bHidden = 1;

    if (!bRecurseChildren)
        return;

    InstanceIndex Child = DALib::Engine->GetRelatedInstance(Instance, 0, kInvalid);
    while (Child != kInvalid)
    {
        Found = PartCacheFindRecord(this, Child);
        if (Found != nullptr)
            Found->bHidden = 1;
        Child = DALib::Engine->GetRelatedInstance(Instance, 0, Child);
    }
}

// FUN_004059d0 (thiscall): from the second record on, a hidden record loses its active flag.
// PartCount (re-read each pass) compared signed.
void FPartCache::ClearHiddenActive()
{
    for (int PartIndex = 1; PartIndex < static_cast<int>(PartCount); ++PartIndex)
    {
        FPartCacheEntry* Entry = Entries + PartIndex;
        if (Entry->bHidden != 0)
            Entry->bActive = 0;
    }
}

// FUN_00405a00 (thiscall, RET 0xc: Instance, bRecurseChildren, Tag). Returns whether any
// record matched. No test of the cache, of the vertex array or of DALib::Engine.
bool FPartCache::ActivatePart(InstanceIndex Instance, bool bRecurseChildren, unsigned Tag)
{
    const InstanceIndex kInvalid = InvalidInstanceIndex;
    bool bFound = false;

    FPartCacheEntry* Found = PartCacheFindRecord(this, Instance);
    if (Found != nullptr)
    {
        bFound = true;
        PartCacheActivateRecord(Found, Tag);
    }

    if (!bRecurseChildren)
        return bFound;

    InstanceIndex Child = DALib::Engine->GetRelatedInstance(Instance, 0, kInvalid);
    while (Child != kInvalid)
    {
        Found = PartCacheFindRecord(this, Child);
        if (Found != nullptr)
        {
            bFound = true;
            PartCacheActivateRecord(Found, Tag);
        }
        Child = DALib::Engine->GetRelatedInstance(Instance, 0, Child);
    }
    return bFound;
}

// FUN_00405b10 (thiscall on the cache, RET 8: Instance, Tag).
bool FPartCache::Build(InstanceIndex Instance, unsigned Tag)
{
    const InstanceIndex kInvalid = InvalidInstanceIndex;

    if (Instance == kInvalid)
        return false;

    IEngine* Engine = DALib::Engine;

    // FUN_004058e0's body inlined.
    if (this->Entries != nullptr)
    {
        unsigned* Prefix = reinterpret_cast<unsigned*>(this->Entries) - 1;
        unsigned Count = *Prefix;
        unsigned Index = 0;
        for (; Index < Count; ++Index)
            (this->Entries + Index)->Destroy();
        operator delete(Prefix);
    }
    operator delete(MergedIndices);
    this->Entries = nullptr;
    this->PartCount = 0;
    TotalIndices = 0;
    TotalVertices = 0;
    MergedIndices = nullptr;

    // The parts are cached in the instance's identity pose.
    Transform SavedTransform;
    const Transform* TransformPtr = Engine->GetInstanceTransform(Instance);
    CopyTransform12(&SavedTransform, TransformPtr);

    Transform Identity;
    MakeIdentityTransform(&Identity);
    Engine->SetInstanceMatrix(Instance, &Identity);
    Engine->UpdateInstance(Instance, 0, nullptr);

    // Root + children.
    this->PartCount = 1;
    InstanceIndex Child = Engine->GetRelatedInstance(Instance, 0, kInvalid);
    while (Child != kInvalid)
    {
        this->PartCount += 1;
        Child = Engine->GetRelatedInstance(Instance, 0, Child);
    }

    unsigned PartCount = this->PartCount;
    unsigned* Prefix = static_cast<unsigned*>(
        operator new(static_cast<std::size_t>(PartCount) * sizeof(FPartCacheEntry) + 4U));
    FPartCacheEntry* Entries = nullptr;
    if (Prefix != nullptr)
    {
        *Prefix = PartCount;
        Entries = reinterpret_cast<FPartCacheEntry*>(Prefix + 1);
        unsigned Index = 0;
        for (; Index < PartCount; ++Index)
            PartCacheEntryConstruct(Entries + Index);
    }

    // No test of the array -- retail writes the root record through it whatever operator new
    // returned.
    this->Entries = Entries;
    TotalVertices = 0;
    TotalIndices = 0;

    Entries[0].Tag = Tag;
    Entries[0].InstanceId = Instance;
    Entries[0].bSkipWeightTest = 0;

    Transform WorldXform;
    TransformPtr = Engine->GetInstanceTransform(Instance);
    CopyTransform12(&WorldXform, TransformPtr);

    {
        // EH states 1 and 2: FUN_00526a20 release-and-clear holders, destroyed Mesh first.
        FPartComHolder Wire;
        if (Engine->QueryInstance(Entries[0].InstanceId, Iids::VMeshWire,
                                  reinterpret_cast<void**>(&Wire.Ptr)) >= 0)
        {
            Entries[0].BuildFromWire(static_cast<IVMeshWire*>(Wire.Ptr), &WorldXform);
        }
        else
        {
            FPartComHolder Mesh;
            if (Engine->QueryInstance(Entries[0].InstanceId, Iids::Mesh,
                                      reinterpret_cast<void**>(&Mesh.Ptr)) >= 0)
                Entries[0].BuildFromMeshTwice(static_cast<IMesh*>(Mesh.Ptr),
                                              &WorldXform);
        }
    }

    TotalIndices += Entries[0].IndexCount;
    TotalVertices += Entries[0].VertexCount;

    // No bound against PartCount -- the counting walk above saw the same chain.
    FPartCacheEntry* Entry = Entries + 1;
    Child = Engine->GetRelatedInstance(Instance, 0, kInvalid);
    while (Child != kInvalid)
    {
        Entry->Tag = Tag;
        Entry->InstanceId = Child;
        Entry->bSkipWeightTest = 0;

        TransformPtr = Engine->GetInstanceTransform(Child);
        CopyTransform12(&WorldXform, TransformPtr);

        {
            FPartComHolder Wire;
            if (Engine->QueryInstance(Entry->InstanceId, Iids::VMeshWire,
                                      reinterpret_cast<void**>(&Wire.Ptr)) >= 0)
            {
                Entry->BuildFromWire(static_cast<IVMeshWire*>(Wire.Ptr), &WorldXform);
            }
            else
            {
                FPartComHolder Mesh;
                if (Engine->QueryInstance(Entry->InstanceId, Iids::Mesh,
                                          reinterpret_cast<void**>(&Mesh.Ptr)) >= 0)
                    Entry->BuildFromMeshTwice(static_cast<IMesh*>(Mesh.Ptr),
                                              &WorldXform);
            }
        }

        TotalIndices += Entry->IndexCount;
        TotalVertices += Entry->VertexCount;

        Entry += 1;
        Child = Engine->GetRelatedInstance(Instance, 0, Child);
    }

    Engine->SetInstanceMatrix(Instance, &SavedTransform);
    Engine->UpdateInstance(Instance, 0, nullptr);

    // A SIGNED compare against 16000 indices.
    if (static_cast<int>(TotalIndices) > 0x3E80)
    {
        FDUMP(kFdumpTrace1,
              "%s(%d) : TRACE_1:General:%s",
              "E:\\FL\\Scratch\\Source\\Client\\cmn\\cached_wire.cpp",
              0xE0,
              "way too many indicies in the wireframe object");
        Clear();
        return false;
    }

    MergedIndices = static_cast<unsigned short*>(
        operator new(static_cast<std::size_t>(TotalIndices) * 2U));
    return true;
}

namespace
{
    // DAT_005c7608 / DAT_005c7628 / DAT_005c7640: {render state, value} pairs ended by state 0.
    unsigned long const kPartDrawStatesOff[] = {D3DRS_LIGHTING, 0, D3DRS_SPECULARENABLE, 0,
                                                D3DRS_ALPHABLENDENABLE, 0, 0};
    unsigned long const kPartDrawColourVertexOn[] = {D3DRS_COLORVERTEX, 1,
                                                     D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1,
                                                     0};
    unsigned long const kPartDrawColourVertexOff[] = {D3DRS_COLORVERTEX, 0, 0};
    // DAT_005c7650: {texture stage state, value} pairs for stage 0, ended by type 0.
    unsigned long const kPartDrawStage0[] = {D3DTSS_COLOROP, D3DTOP_DISABLE,
                                             D3DTSS_COLORARG1, D3DTA_TEXTURE,
                                             D3DTSS_COLORARG2, D3DTA_CURRENT,
                                             D3DTSS_ALPHAOP, D3DTOP_DISABLE,
                                             D3DTSS_ALPHAARG1, D3DTA_TEXTURE,
                                             D3DTSS_ALPHAARG2, D3DTA_CURRENT, 0};

    // FUN_00406510's loops: the first pair's state is an immediate, then [cursor + 8] until 0.
    void PartDrawApplyStates(IRenderPipeline* Pipeline, unsigned long const* Table)
    {
        unsigned long State = Table[0];
        unsigned long const* Cursor = Table;
        while (State != 0)
        {
            Pipeline->SetRenderState((D3DRENDERSTATETYPE)State, static_cast<int>(Cursor[1]));
            State = Cursor[2];
            Cursor += 2;
        }
    }
}

void FPartCache::Draw(ICamera* Camera, Transform const* Xform)
{
    // FUN_00406510.
    PartDrawApplyStates(DALib::Pipeline, kPartDrawStatesOff);
    DALib::Pipeline->SetTextureStageTexture(0, nullptr);
    PartDrawApplyStates(DALib::Pipeline, kPartDrawColourVertexOn);
    if (TotalVertices == 0)
    {
        return;
    }

    // The result of the lock is not tested. Only the source-block fields below are ever written
    // (retail's normal and texcoord streams stay as the stack left them).
    FRpVertexSrcDesc Source;
    Source.Flags = kRpVertexSrcInterleaved;
    Source.Position.ByteSize = 0xc;
    Source.Position.Stride = 0x10;
    Source.Position.IndexArray = nullptr;
    Source.Diffuse.ByteSize = 4;
    Source.Diffuse.Stride = 0x10;
    Source.Diffuse.IndexArray = nullptr;
    Source.SrcFvf = D3DFVF_XYZ | D3DFVF_DIFFUSE;
    unsigned IndexTotal = 0;
    FVertexMemoryLock LockOut; // vertex buffer, FVF, base vertex, memory
    DALib::VertexBuffer->LockVertexMemory(D3DFVF_XYZ | D3DFVF_DIFFUSE, TotalVertices, &LockOut);

    // The stride of the locked FVF.
    unsigned const Fvf = static_cast<unsigned>(LockOut.Listing.Fvf);
    int PositionBytes = 0;
    if ((Fvf & D3DFVF_XYZB4) == D3DFVF_XYZB4)
    {
        PositionBytes = 0x1c;
    }
    else if ((Fvf & D3DFVF_XYZRHW) == D3DFVF_XYZRHW)
    {
        PositionBytes = 0x10;
    }
    else if ((Fvf & D3DFVF_XYZ) == D3DFVF_XYZ)
    {
        PositionBytes = 0xc;
    }
    int NormalBytes = 0;
    if ((Fvf & D3DFVF_NORMAL) != 0)
    {
        NormalBytes = 0xc;
    }
    int ColourBytes = 0;
    if ((Fvf & D3DFVF_DIFFUSE) != 0)
    {
        ColourBytes = 4;
    }
    if ((Fvf & D3DFVF_SPECULAR) != 0)
    {
        ColourBytes += 4;
    }
    int const Stride = FvfTexcoordBlockBytes(Fvf) + ColourBytes + NormalBytes + PositionBytes;

    // Every active part's indices rebased by the vertices already placed (a 16-bit add).
    unsigned VertexTotal = 0;
    FPartCacheEntry* Entry = Entries;
    for (int PartIndex = 0; PartIndex < static_cast<int>(PartCount); ++PartIndex, ++Entry)
    {
        if (Entry->bActive == 0)
        {
            continue;
        }
        unsigned short* Merged = MergedIndices + IndexTotal;
        for (int Index = 0; Index < static_cast<int>(Entry->IndexCount); ++Index)
        {
            Merged[Index] = static_cast<unsigned short>(
                Entry->Indices[Index] + static_cast<unsigned short>(VertexTotal));
        }
        IndexTotal += Entry->IndexCount;

        Source.Position.Base = reinterpret_cast<unsigned char const*>(Entry->Vertices);
        Source.Position.Count = Entry->VertexCount;
        Source.Diffuse.Base = reinterpret_cast<unsigned char const*>(Entry->Vertices) + 0xc;
        Source.Diffuse.Count = Entry->VertexCount;
        Source.SrcCount = Entry->VertexCount;
        unsigned char* const Dest =
            (unsigned char*)LockOut.Vertices + Stride * static_cast<int>(VertexTotal);
        DALib::VertexBuffer->CopyVertexData(
            Dest, LockOut.Listing.Fvf, &Source, 0, Entry->VertexCount);
        VertexTotal += Entry->VertexCount;
    }
    DALib::VertexBuffer->UnlockVertexMemory(&LockOut);

    unsigned long const* Stage = kPartDrawStage0;
    unsigned long Type = Stage[0];
    while (Type != 0)
    {
        DALib::Pipeline->SetTextureStageState(0, static_cast<int>(Type), static_cast<int>(Stage[1]));
        Type = Stage[2];
        Stage += 2;
    }

    // Pipeline state 7 (seeded 1 before the query) picks one combined modelview or a separate
    // world and view.
    int bCombined = 1;
    DALib::Pipeline->GetPipelineState(Render::PipelineStateViewspaceLights, &bCombined);
    if (bCombined != 0)
    {
        Transform const* const View =
            Camera->GetViewTransform();
        Transform Product;
        PartCacheMath()->MulTransform(Product, *View, *Xform);
        Transform Modelview = Product;
        DALib::Pipeline->SetModelview(&Modelview);
    }
    else
    {
        DALib::Pipeline->SetWorld(Xform);
        DALib::Pipeline->SetView(Camera->GetViewTransform());
    }

    // D3DPT_LINELIST over everything placed above.
    DALib::Pipeline->DrawIndexedPrimitiveVb(2, LockOut.Listing.VertexBuffer,
                                            static_cast<unsigned>(LockOut.Listing.BaseVertex), VertexTotal,
                                            MergedIndices, IndexTotal, 0);
    PartDrawApplyStates(DALib::Pipeline, kPartDrawColourVertexOff);
}

// FUN_00402580
int BindRendererLods(InstanceIndex Instance, int PreferredLod, FRendererLodMap* Saved)
{
    int LargestMax = 0;
    if (Instance == InvalidInstanceIndex)
        return 0;

    if (Saved != nullptr)
    {
        int Current = 0;
        if (DALib::Renderer->GetInstanceCurrentLod(Instance, &Current) >= 0)
            (*Saved)[(void*)Instance] = Current;
    }

    int LodCount = 0;
    DALib::Renderer->GetInstanceLodCount(Instance, &LodCount);
    int MaxIndex = 0;
    if (LodCount > 0)
        MaxIndex = LodCount - 1;
    int Lod = PreferredLod;
    if (PreferredLod > MaxIndex)
        Lod = MaxIndex;
    DALib::Renderer->SetInstanceLod(Instance, Lod);
    if (MaxIndex > 0)
        LargestMax = MaxIndex;

    InstanceIndex Child = DALib::Engine->GetRelatedInstance(Instance, 0, -1);
    while (Child != InvalidInstanceIndex)
    {
        if (Saved != nullptr)
        {
            int Current = 0;
            if (DALib::Renderer->GetInstanceCurrentLod(Child, &Current) >= 0)
                (*Saved)[(void*)Child] = Current;
        }

        int ChildCount = 0;
        DALib::Renderer->GetInstanceLodCount(Child, &ChildCount);
        int ChildMax = 0;
        if (ChildCount > 0)
            ChildMax = ChildCount - 1;
        // Retail quirk: the child clamp is max index + 1, not max index.
        int ChildLod = PreferredLod;
        if (PreferredLod > ChildMax + 1)
            ChildLod = ChildMax + 1;
        DALib::Renderer->SetInstanceLod(Child, ChildLod);
        if (ChildMax > LargestMax)
            LargestMax = ChildMax;

        Child = DALib::Engine->GetRelatedInstance(Instance, 0, Child);
    }
    return LargestMax;
}

// FUN_00402720
void ApplyRendererDetailQueue(InstanceIndex /*UnusedInstance*/, FRendererLodMap const* Saved)
{
    for (FRendererLodMap::const_iterator It = Saved->begin(); It != Saved->end(); ++It)
    {
        const InstanceIndex Inst = (InstanceIndex)It->first;
        int DetailIndex = It->second;

        int LodCount = 0;
        DALib::Renderer->GetInstanceLodCount(Inst, &LodCount);
        int MaxIndex = 0;
        if (LodCount > 0)
            MaxIndex = LodCount - 1;
        if (DetailIndex > MaxIndex)
            DetailIndex = MaxIndex;

        DALib::Renderer->SetInstanceLod(Inst, DetailIndex);
    }
}
