// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "dacom.h"

// Mesh interfaces shared by RendComp and materials.

struct IMaterial;

// Face group channel bits of IMesh (OpenFaceGroupChannels / LockFaceGroupChannels /
// Get/SetFaceGroupData / Get/SetFaceGroupIndices / Add/RemoveFaceGroupChannels; RendComp
// FUN_06c28f50 and LAB_06c22d50). Position, normal, the two texture sets and diffuse are the
// streams of the group's FRpVertexSrcDesc; the edge channels are the "Edge_indices" /
// "Edge_angles" leaves; the extra channels are the group's four ExtraStreams.
enum EMeshChannel
{
    kMeshChannelPosition = 0x1,
    kMeshChannelTex0 = 0x2,
    kMeshChannelTex1 = 0x4,
    kMeshChannelNormal = 0x8,
    kMeshChannelDiffuse = 0x10,
    kMeshChannelEdgeIndices = 0x80,
    kMeshChannelEdgeAngles = 0x100,
    kMeshChannelExtra0 = 0x4000,
    kMeshChannelExtra1 = 0x8000,
    kMeshChannelExtra2 = 0x10000,
    kMeshChannelExtra3 = 0x20000
};

// Mode of IMesh::OpenFaceGroup. Every caller outside RendComp reads (1); the Set* / Add* /
// Remove* operations of FTriMeshArch require 2 (the edited group is made private).
enum EMeshOpenMode
{
    kMeshOpenRead = 1,
    kMeshOpenEdit = 2
};

#ifndef FREELANCER_IMESH
#define FREELANCER_IMESH
struct IMesh : IDAComponent
{
    virtual int DACOM_API ResetMesh() = 0; // slot 3
    virtual int DACOM_API NoOp() = 0; // slot 4
    virtual int DACOM_API RefreshMaterials(unsigned long Flags) = 0; // slot 5
    virtual int DACOM_API RejectOperation(void* A) = 0; // slot 6
    virtual int DACOM_API RejectOperation() = 0; // slot 7
    virtual int DACOM_API GetFaceGroupCount(int* Out) = 0; // slot 8
    virtual int DACOM_API OpenFaceGroup(int Index, int Mode) = 0; // slot 9
    virtual int DACOM_API CloseFaceGroup() = 0; // slot 10
    virtual int DACOM_API AddFaceGroup(void* A) = 0; // slot 11
    virtual int DACOM_API RemoveFaceGroup(int Index) = 0; // slot 12
    virtual int DACOM_API GetFaceGroupPrimitiveType(unsigned int* Out) = 0; // slot 13
    virtual int DACOM_API GetFaceGroupMaterial(IMaterial** Out) = 0; // slot 14
    virtual int DACOM_API SetFaceGroupMaterial(IMaterial* Material) = 0; // slot 15
    virtual int DACOM_API GetFaceGroupMaterialContext(void** Out) = 0; // slot 16
    virtual int DACOM_API SetFaceGroupMaterialContext(void* A) = 0; // slot 17
    virtual int DACOM_API OpenFaceGroupChannels(unsigned long Flags) = 0; // slot 18
    virtual int DACOM_API CloseFaceGroupChannels() = 0; // slot 19
    virtual int DACOM_API LockFaceGroupChannels(unsigned long Mask) = 0; // slot 20
    virtual int DACOM_API UnlockFaceGroupChannels() = 0; // slot 21
    virtual int DACOM_API GetFaceGroupData(int Channel, void** A, void** B, void** C, void** D) = 0; // slot 22
    virtual int DACOM_API SetFaceGroupData(int Channel, void* A, void* B, void* C, void* D) = 0; // slot 23
    virtual int DACOM_API GetFaceGroupIndices(int Channel, void** A, void** B) = 0; // slot 24
    virtual int DACOM_API SetFaceGroupIndices(int Channel, void* Src, int Count) = 0; // slot 25
    virtual int DACOM_API GetFaceGroupIndexCount(int Selector, void** Out) = 0; // slot 26
    virtual int DACOM_API SetFaceGroupIndexCount(int Selector, int Count) = 0; // slot 27
    virtual int DACOM_API AddFaceGroupChannels(unsigned long Flags) = 0; // slot 28
    virtual int DACOM_API RemoveFaceGroupChannels(unsigned long Flags) = 0; // slot 29
};
#endif

#ifndef FREELANCER_IVMESHWIRE
#define FREELANCER_IVMESHWIRE
struct IVMeshWire : IDAComponent
{

    virtual void* DACOM_API GetWireData() = 0; // slot 3
    virtual int DACOM_API Draw() = 0; // slot 4
    virtual void* DACOM_API GetMeshData() = 0; // slot 5
};
#endif
