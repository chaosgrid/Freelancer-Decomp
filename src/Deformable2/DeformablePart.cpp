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

static void NegateVec3(const float* Source, float* Dest);
static void Transpose3x3(const float* Source, float* Dest);
static int FvfTexcoordBytes(unsigned long Fvf);
static int FvfColorBytes(unsigned long Fvf);

// FUN_065f6720. No test.
void FDeformPart::Construct()
{
    std::memset(this, 0, sizeof(FDeformPart));
    Flags = 1;
}

// FUN_065f6760, the part dtor.
void FDeformPart::Reset()
{
    G_VertexBuffer->DestroyVb(&Vb.Listing.VertexBuffer);
    Vb.Capacity = 0;
    G_IndexBuffer->DestroyIb(&Ib.Scratch.IndexBuffer);
    Ib.Capacity = 0;

    // FUN_065f17e0 on +0 with no null test.
    ReleaseLodHolder(Lod);
    // The counted mesh array (header at -4), last to first.
    if (Meshes != nullptr)
    {
        int* Header = (int*)Meshes - 1;
        int Index = *Header - 1;
        while (Index >= 0)
        {
            if (Meshes[Index] != nullptr)
            {
                Meshes[Index]->Release();
                Meshes[Index] = nullptr;
            }
            Index -= 1;
        }
        delete[] (unsigned char*)Header;
    }
    // +0 is released a second time when still set. The holder carries two references for the
    // part -- FUN_065f36c0 creates it at 1 and FUN_065f67f0 AddRefs it -- so this is not a
    // double free.
    if (Lod != nullptr)
    {
        ReleaseLodHolder(Lod);
        Lod = nullptr;
    }
}

// FUN_065f67f0 (thiscall on the part: Holder, Meshes, Count). Returns 1.
unsigned char FDeformPart::BindMeshes(FLodHolder* Holder, FDeformMesh** InMeshes, int Count)
{
    if (Lod != Holder)
    {
        if (Lod != nullptr)
        {
            ReleaseLodHolder(Lod);
            Lod = nullptr;
        }
        Lod = Holder;
        if (Holder != nullptr)
            AddRefLodHolder(Holder);
    }

    MeshCount = Count;
    int* Block = (int*)new unsigned char[(size_t)Count * 4 + 4];
    *Block = Count;
    FDeformMesh** Array = (FDeformMesh**)(Block + 1);
    int Index = 0;
    while (Index < Count)
    {
        Array[Index] = nullptr;
        Index += 1;
    }
    Meshes = Array;

    int MaxVertexCapacity = 0;
    int MaxIndexCount = 0;
    Index = 0;
    while (Index < MeshCount)
    {
        Meshes[Index] = InMeshes[Index];
        const int VertexCapacity = Meshes[Index]->GetVertexCapacity();
        if (VertexCapacity > MaxVertexCapacity)
            MaxVertexCapacity = VertexCapacity;
        const int IndexCount = Meshes[Index]->GetIndexCount();
        if (IndexCount > MaxIndexCount)
            MaxIndexCount = IndexCount;
        Index += 1;
    }
    if (MaxVertexCapacity > 0)
        Vb.Capacity = MaxVertexCapacity;
    if (MaxIndexCount > 0)
        Ib.Capacity = MaxIndexCount;
    return 1;
}

// FUN_065f68c0 (thiscall on the part: OutRadius, OutCenter): the first mesh's slot 4.
unsigned char FDeformPart::BoundSphere(float* OutRadius, float* OutCenter)
{
    const unsigned char bOk = Meshes[0]->GetBoundSphere(OutRadius, OutCenter);
    if (bOk != 0)
    {
        I3DMathEngine* Math = AcquireMathEngine();
        float* BoneAtC = Lod->BoneSlots[0].LocalTransform;
        float Transformed[3];
        Math->TransformXform(*(FVector*)Transformed,
                             *(const FTransform*)BoneAtC,
                             *(const FVector*)OutCenter); // +0x28
        OutCenter[0] = Transformed[0];
        OutCenter[1] = Transformed[1];
        OutCenter[2] = Transformed[2];
    }
    return bOk;
}

// FUN_065f6930.
bool FDeformPart::SelectLod(int LodIndex)
{
    FDeformMesh* Old = Current;
    if (LodIndex < 0 || MeshCount <= LodIndex)
    {
        Current = nullptr;
        this->LodIndex = LodIndex;
        return Old != Current;
    }
    FDeformMesh* Next = Meshes[LodIndex];
    Current = Next;
    if ((Flags & 1) == 0 && Old == Next)
    {
        Flags = (unsigned char)(Flags & (unsigned char)(~1));
        this->LodIndex = LodIndex;
        return Old != Current;
    }
    // Retail's XOR/AND/XOR sets bit 0 whatever it was (it does not toggle).
    Flags = (unsigned char)(Flags | 1);
    this->LodIndex = LodIndex;
    return Old != Current;
}

// FUN_065f69c0 (thiscall; its one argument is never read). Retail returns AL = 1.
void FDeformPart::MarkSkinDirty()
{
    Flags = (unsigned char)(Flags | 2);
}

// FUN_065f69d0 (thiscall on the part: Camera, Unused).
unsigned char FDeformPart::DrawLive(ICamera* Camera, void* /*Unused*/)
{
    if (Current == nullptr)
        return 1;
    const bool bVbSelected = G_VertexBuffer->SelectVb(Vb.Listing.VertexBuffer) >= 0;
    const unsigned char Flags = this->Flags;
    const unsigned char bRebuild = ((Flags & 1) != 0 || !bVbSelected) ? 1 : 0;
    if ((Flags & 2) != 0 || bRebuild != 0)
    {
        if (Current->Skin(Lod, &Vb, bRebuild) == 0)
            return 0;
        if (G_VertexBuffer->SelectVb(Vb.Listing.VertexBuffer) < 0)
            return 0;
    }
    const bool bIbSelected =
        G_IndexBuffer->SelectIb(Ib.Scratch.IndexBuffer,
                                (unsigned int)(Vb.Listing.BaseVertex), 0, 0) >= 0;
    if ((this->Flags & 1) != 0 || !bIbSelected)
    {
        if (Current->FillIndexBuffer(&Ib) == 0)
            return 0;
        if (G_IndexBuffer->SelectIb(Ib.Scratch.IndexBuffer,
                                    (unsigned int)(Vb.Listing.BaseVertex), 0,
                                    0) < 0)
            return 0;
    }
    Current->Draw(Camera, this);
    this->Flags = (unsigned char)(this->Flags & 0xFC);
    return 1;
}

// FUN_065f6ad0: out[i] = weight * matrix[i] (12 floats).
static void ScaleBoneMatrix(float* Out, const float* Matrix, float Weight)
{
    int Index = 0;
    while (Index < 12)
    {
        Out[Index] = Weight * Matrix[Index];
        Index += 1;
    }
}

// FUN_065f6b00: out[i] += weight * matrix[i], one float rounding per element.
static void AccumulateBoneMatrix(float* Out, const float* Matrix, float Weight)
{
    int Index = 0;
    while (Index < 12)
    {
        Out[Index] = (float)((double)Weight * Matrix[Index] +
                                        (double)Out[Index]);
        Index += 1;
    }
}

// FUN_065f6b30: out = M3x4 * point (translation m[9..11]).
static void TransformPoint(float* Out, const float* Matrix, const float* Point)
{
    // Point is re-read per row, as retail does (matters only if Out aliases it).
    Out[0] = (float)((((double)Matrix[2] * Point[2] +
                                  (double)Matrix[1] * Point[1]) +
                                 (double)Matrix[0] * Point[0]) +
                                Matrix[9]);
    Out[1] = (float)((((double)Matrix[5] * Point[2] +
                                  (double)Matrix[3] * Point[0]) +
                                 (double)Matrix[4] * Point[1]) +
                                Matrix[10]);
    Out[2] = (float)((((double)Matrix[8] * Point[2] +
                                  (double)Matrix[6] * Point[0]) +
                                 (double)Matrix[7] * Point[1]) +
                                Matrix[11]);
}

// FUN_065f6b90: out = M3x3 * direction; FUN_065f6b30 without the translation, same summation
// order.
static void TransformDirection(float* Out, const float* Matrix, const float* Direction)
{
    Out[0] = (float)(((double)Matrix[2] * Direction[2] +
                                 (double)Matrix[1] * Direction[1]) +
                                (double)Matrix[0] * Direction[0]);
    Out[1] = (float)(((double)Matrix[5] * Direction[2] +
                                 (double)Matrix[3] * Direction[0]) +
                                (double)Matrix[4] * Direction[1]);
    Out[2] = (float)(((double)Matrix[8] * Direction[2] +
                                 (double)Matrix[6] * Direction[0]) +
                                (double)Matrix[7] * Direction[1]);
}

// LAB_065f6bf0 FVF stride. ParseTag 0x112 → xyz 0xC + nrm 0xC + uv0 8 = 0x20.
static int ComputeFvfStride(unsigned long Fvf)
{
    int Position = 0;
    if ((Fvf & 0xC) == 0xC)
        Position = 0x1C;
    else if ((Fvf & 4) == 4)
        Position = 0x10;
    else if ((Fvf & 2) == 2)
        Position = 0xC;
    int Normal = ((Fvf & 0x10) != 0) ? 0xC : 0;
    return Position + Normal + FvfColorBytes(Fvf) + FvfTexcoordBytes(Fvf);
}

static const char* const G_FdumpFileDeformablePart =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Deformable2\\DeformablePart.cpp";

// Reuse a valid VB or recreate it at the descriptor capacity.
static unsigned char EnsurePartVertexBuffer(FDeformVbDesc* Vb, IRPVertexBuffer* VertexBuffer,
                                            unsigned long Fvf, int /*Count*/)
{
    Vb->Listing.Fvf = Fvf;
    Vb->Listing.BaseVertex = 0;
    if (VertexBuffer->IsVbValid(Vb->Listing.VertexBuffer) != 0)
        return 1;
    VertexBuffer->DestroyVb(&Vb->Listing.VertexBuffer);
    return VertexBuffer->CreateVb((unsigned int)(Vb->Listing.Fvf),
                                  (unsigned int)Vb->Capacity,
                                  (unsigned int**)(&Vb->Listing.VertexBuffer),
                                  0) >= 0
               ? 1
               : 0;
}

// LAB_065f6bf0, the DFM mesh's slot 3 (Holder, &part+0x14, bForce).
unsigned char FMeshArchetype::Skin(FLodHolder* Holder, FDeformVbDesc* Vb, unsigned char bForce)
{
    const float* const Root =
        Holder->BoneSlots[0].LocalTransform;
    float RootPos[3];
    MathTransformVec3(RootPos, Root, Root + 9);
    float InverseRoot[12];
    NegateVec3(RootPos, InverseRoot + 9);
    Transpose3x3(Root, InverseRoot);

    SkinnedVertexCount = 0;
    SkinnedBoneCount = 0;
    int BoneIndex = 0;
    while (BoneIndex < Holder->BindCursor)
    {
        FBoneRuntime* const Bone = &Holder->BoneSlots[BoneIndex];
        const FBoneVertexList* const List = &BoneVerts[BoneIndex];
        const unsigned char BoneFlags = Bone->Flags;
        if ((BoneFlags & 2) != 0 || bForce != 0)
        {
            Bone->Flags = (unsigned char)(BoneFlags & 0xFD);
            if (List->Start != nullptr && (List->Finish - List->Start) != 0)
            {
                SkinnedBoneCount += 1;
                if (bSkinned == 0)
                {
                    WriteIdentity3x3(Bone->Matrix);
                    Bone->Matrix[11] = 0.0f;
                    Bone->Matrix[10] = 0.0f;
                    Bone->Matrix[9] = 0.0f;
                }
                else
                {
                    Bone->ComposeWorldMatrix(InverseRoot);
                    unsigned char* const Dirty = FaceFlags;
                    const unsigned short* Cursor = List->Start;
                    while (List->Finish != Cursor)
                    {
                        Dirty[*Cursor] = 1;
                        Cursor += 1;
                    }
                }
            }
        }
        BoneIndex += 1;
    }

    const int Stride = ComputeFvfStride(ParseTag);
    const FVertexRemapList* const Remap = RemapStart;
    unsigned char* Locked = nullptr;
    const int Capacity = GetVertexCapacity();
    IRPVertexBuffer* const VertexBuffer = G_VertexBuffer;
    if (EnsurePartVertexBuffer(Vb, VertexBuffer, ParseTag, Capacity) == 0)
        return 0;
    Vb->Listing.BaseVertex = 0;
    if (VertexBuffer->LockVb(Vb->Listing.VertexBuffer,
                             (unsigned int*)(&Vb->Listing.BaseVertex),
                             (void**)(&Locked),
                             (unsigned int)Capacity) < 0)
        return 0;

    if (bForce != 0)
    {
        FRpVertexSrcDesc StreamDesc;
        FillStreamDesc(&StreamDesc);
        if (G_VertexBuffer->CopyVertices(
                Vb->Listing.VertexBuffer, (unsigned int*)&Vb->Listing.BaseVertex, &StreamDesc, 0,
                (unsigned int)PointIndexCount) < 0)
        {
            FDUMP(kFdumpWarning, "%s(%d) : WARNING:General:%s", G_FdumpFileDeformablePart, 0x134,
                  "failed to copy deformable verts during update_verts");
            G_VertexBuffer->UnlockVb(Vb->Listing.VertexBuffer);
            return 0;
        }
    }

    unsigned char* const Dirty = FaceFlags;
    const unsigned char* const Positions = (const unsigned char*)Vertices;
    unsigned int Vertex = 0;
    while (Vertex < (unsigned int)VertexCount)
    {
        if (Dirty[Vertex] != 0)
        {
            Dirty[Vertex] = 0;
            SkinnedVertexCount += 1;
            const FSkinVertex* const SkinVertex = &SkinTable[Vertex];
            const FSkinPair* const Pairs = SkinVertex->Pairs;
            const float* const Source =
                (const float*)(Positions + (size_t)Vertex * 0x18);
            const float* const First = Holder->BoneSlots[Pairs[0].BoneId].Matrix;
            float Out[6];
            const int Count = SkinVertex->BoneCount;
            if (Count == 1)
            {
                TransformPoint(Out, First, Source);
                TransformDirection(Out + 3, First, Source + 3);
            }
            else
            {
                float Blend[12];
                ScaleBoneMatrix(Blend, First, Pairs[0].Weight);
                if (Count == 4)
                    AccumulateBoneMatrix(Blend, Holder->BoneSlots[Pairs[3].BoneId].Matrix,
                                         Pairs[3].Weight);
                if (Count == 4 || Count == 3)
                    AccumulateBoneMatrix(Blend, Holder->BoneSlots[Pairs[2].BoneId].Matrix,
                                         Pairs[2].Weight);
                if (Count == 4 || Count == 3 || Count == 2)
                    AccumulateBoneMatrix(Blend, Holder->BoneSlots[Pairs[1].BoneId].Matrix,
                                         Pairs[1].Weight);
                TransformPoint(Out, Blend, Source);
                TransformDirection(Out + 3, Blend, Source + 3);
                const float LenSq = (float)(
                    ((double)Out[4] * Out[4] + (double)Out[5] * Out[5]) +
                    (double)Out[3] * Out[3]);
                int Bits = 0;
                std::memcpy(&Bits, &LenSq, sizeof(Bits));
                Bits = (int)(0x5F3759DFu - (unsigned int)(Bits >> 1));
                float Estimate = 0.0f;
                std::memcpy(&Estimate, &Bits, sizeof(Estimate));
                const double Y0 = (double)Estimate;
                const double InvLen =
                    (1.5 - Y0 * Y0 * (double)LenSq * 0.5) * Y0;
                Out[3] = (float)((double)Out[3] * InvLen);
                Out[4] = (float)((double)Out[4] * InvLen);
                Out[5] = (float)((double)Out[5] * InvLen);
            }
            const unsigned int* Cursor = Remap[Vertex].Start;
            const unsigned int* const End = Remap[Vertex].Finish;
            while (Cursor != End)
            {
                std::memcpy(Locked + (size_t)(*Cursor) * (size_t)Stride,
                            Out, 6 * sizeof(float));
                Cursor += 1;
            }
        }
        Vertex += 1;
    }
    G_VertexBuffer->UnlockVb(Vb->Listing.VertexBuffer);
    return 1;
}

// FUN_065f70b0 (thiscall): dest = -this[0..2].
static void NegateVec3(const float* Source, float* Dest)
{
    Dest[0] = -Source[0];
    Dest[1] = -Source[1];
    Dest[2] = -Source[2];
}

// FUN_065f70d0 (thiscall): 3x3 transpose. Retail loads every element before the first store,
// so an in-place transpose works; the copy keeps that.
static void Transpose3x3(const float* Source, float* Dest)
{
    float Copy[9];
    std::memcpy(Copy, Source, sizeof(Copy));
    Dest[0] = Copy[0];
    Dest[1] = Copy[3];
    Dest[2] = Copy[6];
    Dest[3] = Copy[1];
    Dest[4] = Copy[4];
    Dest[5] = Copy[7];
    Dest[6] = Copy[2];
    Dest[7] = Copy[5];
    Dest[8] = Copy[8];
}

// FUN_065f7120: math (DAT_066031a4) +0x38 into a temp, copied to Dest. No tests.
void MathTransformVec3(float* Dest, const float* A, const float* B)
{
    I3DMathEngine* Math = AcquireMathEngine();
    float Tmp[3];
    Math->TransposeTransform(*(FVector*)Tmp,
                                                          *(const FMatrix*)A,
                                                          *(const FVector*)B); // +0x38
    Dest[0] = Tmp[0];
    Dest[1] = Tmp[1];
    Dest[2] = Tmp[2];
}

// FUN_065f71c0: texcoord extra from FVF bits 8–11 count, sizes at bit 16+.
static int FvfTexcoordBytes(unsigned long Fvf)
{
    int Bytes = 0;
    unsigned int SetCount = (Fvf >> 8) & 0xF;
    unsigned int Bit = 0x10;
    while (SetCount != 0)
    {
        switch ((Fvf >> (Bit & 31)) & 3)
        {
        case 0:
            Bytes += 8;
            break;
        case 1:
            Bytes += 0xC;
            break;
        case 2:
            Bytes += 0x10;
            break;
        default:
            Bytes += 4;
            break;
        }
        Bit += 2;
        SetCount -= 1;
    }
    return Bytes;
}

// FUN_065f7220: D3DFVF_DIFFUSE (0x40) / D3DFVF_SPECULAR (0x80).
static int FvfColorBytes(unsigned long Fvf)
{
    int Bytes = 0;
    if ((Fvf & 0x40) != 0)
        Bytes += 4;
    if ((Fvf & 0x80) != 0)
        Bytes += 4;
    return Bytes;
}

// LAB_065f7240, the DFM mesh's slot 2 (&part+0x24).
unsigned char FMeshArchetype::FillIndexBuffer(FDeformIbDesc* Ib)
{
    const int IndexCount = GetIndexCount();
    IRPIndexBuffer* const IndexBuffer = G_IndexBuffer;
    unsigned short* Locked = nullptr;
    if (IndexBuffer->IsIbValid(Ib->Scratch.IndexBuffer) == 0)
    {
        IndexBuffer->DestroyIb(&Ib->Scratch.IndexBuffer);
        if (IndexBuffer->CreateIb((unsigned int)Ib->Capacity,
                                  (unsigned int**)(&Ib->Scratch.IndexBuffer),
                                  0) < 0)
            return 0;
    }
    Ib->Scratch.Bias = 0;
    if (IndexBuffer->LockIb(Ib->Scratch.IndexBuffer,
                            (unsigned int*)(&Ib->Scratch.Bias),
                            (void**)(&Locked),
                            (unsigned int)IndexCount) < 0)
        return 0;
    int GroupIndex = 0;
    while (GroupIndex < FaceGroupCount)
    {
        const FFaceGroup* const Group = &FaceGroups[GroupIndex];
        unsigned long Index = 0;
        while (Index < Group->IndexCount)
        {
            *Locked = (unsigned short)(Group->Indices[Index] +
                                                  (unsigned short)Group->IndexMin);
            Locked += 1;
            Index += 1;
        }
        GroupIndex += 1;
    }
    G_IndexBuffer->UnlockIb(Ib->Scratch.IndexBuffer);
    return 1;
}

// FUN_065f7340 (thiscall on the face group: context, accumulated IB start). No tests of the
// context or the material.
int FFaceGroup::Draw(FMaterialSubmitContext* Context, int StartIndex)
{
    if (IndexCount == 0)
        return 0;
    const int NumVerts =
        (int)IndexMax - (int)IndexMin + 1;
    Context->MaterialContext = UvBinding;
    IMaterial* Material = this->Material;
    Material->Bind(Context);
    Material->Submit(Context, Kind, IndexMin, (unsigned long)NumVerts,
                     (unsigned long)StartIndex, IndexCount, 0x100ul);
    Material->Teardown(Context);
    return NumVerts;
}

// LAB_065f73a0, the DFM mesh's slot 5 (Camera, Part).
unsigned char FMeshArchetype::Draw(ICamera* Camera, FDeformPart* Part)
{
    FBoneRuntime* const BoneSlots = Part->Lod->BoneSlots;
    const float* const Root =
        BoneSlots[0].LocalTransform;
    const float* const View = Camera->GetViewTransform();
    int bHaveView = 1;
    G_Pipeline->GetPipelineState(7, &bHaveView);
    if (bHaveView != 0)
    {
        float Composed[12];
        AcquireMathEngine()->MulTransform(*(FTransform*)Composed,
                                          *(const FTransform*)View,
                                          *(const FTransform*)Root); // +0x1C
        float Modelview[12];
        std::memcpy(Modelview, Composed, sizeof(Modelview));
        G_Pipeline->SetModelview(Modelview);
    }
    else
    {
        float Identity[12];
        Identity[0] = 1.0f;
        Identity[1] = 0.0f;
        Identity[2] = 0.0f;
        Identity[3] = 0.0f;
        Identity[4] = 1.0f;
        Identity[5] = 0.0f;
        Identity[6] = 0.0f;
        Identity[7] = 0.0f;
        Identity[8] = 1.0f;
        Identity[9] = 0.0f;
        Identity[10] = 0.0f;
        Identity[11] = 0.0f;
        G_Pipeline->SetWorld(Identity);
    }

    FMaterialSubmitContext Context;
    Context.Inst = BoneSlots[0].Instance;
    // The mutable context ABI borrows these const transform views.
    Context.World = const_cast<float*>(Root);
    Context.View = const_cast<float*>(View);
    Context.SourceVertices = nullptr;
    Context.Indices = nullptr;
    Context.VbListing = &Part->Vb.Listing;
    Context.IbScratch = &Part->Ib.Scratch;
    // Retail leaves +0x1C as stack garbage; the port cannot reproduce that and stores 0.
    Context.ReplacedMaterial = nullptr;
    Context.MaterialContext = nullptr;

    FUvDeformBinding Binding;
    Binding.Record = nullptr;
    Binding.Bone = nullptr;
    Binding.EyeUvTransformCount = 0;
    unsigned long Start = 0;
    int GroupIndex = 0;
    while (GroupIndex < FaceGroupCount)
    {
        FFaceGroup* const Group = &FaceGroups[GroupIndex];
        if (UvDeformCount != 0 && (long)Group->UvBoneIndex >= 0)
        {
            FUvDeformRecord* const Record = &UvDeform[Group->UvBoneIndex];
            Binding.Record = Record;
            Binding.Bone = &Part->Lod->BoneSlots[Record->BoneId];
            Group->UvBinding = &Binding;
        }
        Group->Draw(&Context, (int)Start);
        Start += Group->IndexCount;
        GroupIndex += 1;
    }
    EyeUvTransformCount = Binding.EyeUvTransformCount;
    return 1;
}

// FUN_065f7590: a face group whose material's ClassName (slot 20) is "EyeMaterial"
// (.data:06602790) gets UvBoneIndex (+0x20) = 0. Retail returns AL = 1.
void BindUvBoneIdsToFaceGroups(FMeshArchetype* Arch)
{
    for (int Index = 0; Index < Arch->FaceGroupCount; ++Index)
    {
        FFaceGroup* Group = &Arch->FaceGroups[Index];
        if (Group->Material == nullptr)
            continue;
        const char* Name = Group->Material->ClassName();
        // strncmp over 12 == REPE CMPSB over 12 here: the 12th byte of "EyeMaterial" is its
        // NUL, so both stop at the same first difference.
        if (std::strncmp(Name, G_EyeMaterialName, 12) == 0)
            Group->UvBoneIndex = 0;
    }
}

static void RemapListPush(FVertexRemapList* List, unsigned int PointIndex)
{
    if (List == nullptr)
        return;
    int Used = 0;
    int Cap = 0;
    if (List->Start != nullptr && List->Finish != nullptr)
        Used = (int)(List->Finish - List->Start);
    if (List->Start != nullptr && List->CapacityEnd != nullptr)
        Cap = (int)(List->CapacityEnd - List->Start);
    if (Used >= Cap)
    {
        int NewCap = (Cap <= 0) ? 4 : Cap * 2;
        unsigned int* NewBuf = (unsigned int*)new (std::nothrow) unsigned char[(size_t)NewCap * sizeof(unsigned int)];
        if (NewBuf == nullptr)
            return;
        if (List->Start != nullptr && Used > 0)
            std::memcpy(NewBuf, List->Start, (size_t)Used * sizeof(unsigned int));
        if (List->Start != nullptr)
            delete[] (unsigned char*)List->Start;
        List->Start = NewBuf;
        List->Finish = NewBuf + Used;
        List->CapacityEnd = NewBuf + NewCap;
    }
    if (List->Finish == nullptr)
        return;
    *List->Finish = PointIndex;
    List->Finish += 1;
}

// FUN_065f75f0: remap[PointIndices[i]].push(i). vector<uint> @ mesh+0x64.
void BuildVertexRemap(FMeshArchetype* Arch)
{
    if (Arch->VertexCount == 0)
        return;

    const size_t VertexCount = (size_t)((unsigned int)Arch->VertexCount);
    FVertexRemapList* Lists =
        (FVertexRemapList*)new unsigned char[VertexCount * sizeof(FVertexRemapList)];
    std::memset(Lists, 0, VertexCount * sizeof(FVertexRemapList));
    Arch->RemapStart = Lists;
    Arch->RemapFinish = Lists + VertexCount;
    Arch->RemapCapacity = Arch->RemapFinish;

    const unsigned long* Indices = Arch->PointIndices;
    const unsigned int Count = (unsigned int)Arch->PointIndexCount;
    for (unsigned int Index = 0; Index < Count; ++Index)
    {
        const unsigned int Vertex = Indices[Index];
        if (Vertex < (unsigned int)Arch->VertexCount)
            RemapListPush(&Lists[Vertex], Index);
    }
}
