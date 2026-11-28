// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "dacom.h"
#include "readfile.h"
#include "x86math.h"
#include "luaprofile.h"
#include "engine/engbase.h"
#include "render/IRenderPipeline.h"
#include "render/IMaterial.h"
#include "render/rendcomp.h"
#include "render/shading.h"
#include <cstddef>

// Unversioned aliases in .data -- retail accepts both the
// "1.11_"-stamped ids (Iids::Deformable, Iids::EngineComponent) and these bare names.
static const char* const G_IidDeformableShort = "IDeformable";
static const char* const G_IidEngineComponentShort = "IEngineComponent";
static const char* const G_DeformableComponentName = "Deformable";
static const char* const G_DeformableSection = "Deformable";
static const char* const G_DeformRateKey = "DeformRate";
static const char* const G_EyeMaterialName = "EyeMaterial";
static const char* const G_EyeMaterialPattern = "^eye*";
static const char* const G_FileSystemName = "FileSystem";
static const char* const G_Name3DMathEngine = "3DMathEngine";

static const int G_MaxBoneCount = 150;

// The 0x34 FileSystem AGGDESC (FUN_065f3e80 / FUN_065f36c0) and IFileSystem come from
// readfile.h.

// FUN_065f7c70 face group 0x24. +0x10/+0x14 min/max after FUN_065f7e00.
struct FUvDeformBinding;

struct FFaceGroup
{
    unsigned long Kind;       // +0 = 4
    unsigned long MaterialCrc;
    IMaterial* Material;
    FUvDeformBinding* UvBinding; // +0x0C
    unsigned long IndexMin;
    unsigned long IndexMax;
    unsigned short* Indices;
    unsigned long IndexCount;
    unsigned long UvBoneIndex; // +0x20 FUN_065f7590: -1 none; 0 = EyeMaterial

    // FUN_065f7c70. Defined in DeformablePartArchetype.cpp.
    void Construct();

    // FUN_065f7340. Defined in DeformablePart.cpp.
    int Draw(FMaterialSubmitContext* Context, int StartIndex);
};

static_assert(sizeof(FFaceGroup) == 0x24, "face group must be 0x24");

// One row of the nine Geometry streams FUN_065f8480 zips.
struct FUvDeformRecord
{
    int BoneId;
    int VertexCount;
    float PlaneDistance;
    float BoneXToU;
    float BoneYToV;
    float MinDu;
    float MaxDu;
    float MinDv;
    float MaxDv;
};

static_assert(sizeof(FUvDeformRecord) == 0x24, "UV-deform record must be 0x24");

struct FBoneRuntime;

// The triple LAB_065f73a0 keeps on its stack (ESP+0x1C) and hangs off a UV-deformed face
// group's +0x0C, which FUN_065f7340 hands the material as context +0x20. The draw zeroes all
// three, fills the first two per group, and copies the third into mesh +0x48 afterwards.
struct FUvDeformBinding
{
    FUvDeformRecord* Record;   // UvDeform[group +0x20]
    FBoneRuntime* Bone;        // BoneSlots[Record->BoneId]
    // EyeMaterial Bind (LAB_065fff20) counts every eye texture transform it sets.
    unsigned long EyeUvTransformCount;
};

// Bone_id_chain + Bone_weight_chain pair.
struct FSkinPair
{
    int BoneId;
    float Weight;
};

static_assert(sizeof(FSkinPair) == 8, "skin pair must be 8");

struct FSkinVertex
{
    int BoneCount;
    FSkinPair* Pairs;
};

static_assert(sizeof(FSkinVertex) == 8, "skin vertex must be 8");

// FUN_065f89e0 / FUN_065f96b0: per-bone std::vector<ushort> of vertex indices.
struct FBoneVertexList
{
    unsigned char Allocator;    // +0x00 the STL allocator byte
    unsigned char Pad01[3];
    unsigned short* Start;
    unsigned short* Finish;
    unsigned short* CapacityEnd;

    // FUN_065f9430. Defined in DeformablePartArchetype.cpp.
    void Destroy();
};

static_assert(sizeof(FBoneVertexList) == 0x10, "bone vertex list must be 0x10");

// FUN_065f75f0: per-vertex vector<uint> of Point_indices slots (mesh+0x64).
struct FVertexRemapList
{
    unsigned char Pad00[4];
    unsigned int* Start;
    unsigned int* Finish;
    unsigned int* CapacityEnd;
};

static_assert(sizeof(FVertexRemapList) == 0x10, "vertex remap list must be 0x10");

// FDeformPart::Flags.
enum EDeformPartFlag
{
    kDeformPartLodChanged = 0x1, // rebuild both buffers (FUN_065f6930)
    kDeformPartSkinStale = 0x2,  // re-skin the vertex buffer (FUN_065f69c0)
};

struct FDeformPart;
struct FLodHolder;

// Shared polymorphic interface for DFM meshes and VMesh wrappers.
struct FDeformVbDesc;
struct FDeformIbDesc;
struct ICamera;

struct FDeformMesh
{
    int RefCount;           // +0x04
    unsigned long NameCrc;  // +0x08

    explicit FDeformMesh(const char* Name);
    virtual int GetIndexCount();                                              // 0 +0x00
    virtual int GetVertexCapacity();                                          // 1 +0x04
    // 2 +0x08 -- the part's index-buffer descriptor (&part+0x24).
    virtual unsigned char FillIndexBuffer(FDeformIbDesc* Ib);
    // 3 +0x0C -- (part+0 holder, &part+0x14, a flag byte). FUN_065f69d0 pushes the flag as a
    // whole dword but LAB_065f6bf0 reads only its low byte.
    virtual unsigned char Skin(FLodHolder* Holder, FDeformVbDesc* Vb, unsigned char bForce);
    virtual unsigned char GetBoundSphere(float* OutRadius, Vector* OutCenter); // 4 +0x10
    virtual unsigned char Draw(ICamera* Camera, FDeformPart* Part) = 0;       // 5 +0x14
    virtual ~FDeformMesh();                                                   // 6 +0x18

    // FUN_065f28e0. Defined in DeformableMesh.cpp.
    void AddRef();

    // FUN_065f28f0. Defined in DeformableMesh.cpp.
    int Release();
};

// FUN_065f8b00 on calloc(1, 0x84). Vtable PTR_LAB_0660135c.
struct FMeshArchetype : FDeformMesh
{
    explicit FMeshArchetype(const char* Name);
    ~FMeshArchetype() override;
    int GetIndexCount() override;
    int GetVertexCapacity() override;
    unsigned char FillIndexBuffer(FDeformIbDesc* Ib) override;
    unsigned char Skin(FLodHolder* Holder, FDeformVbDesc* Vb, unsigned char bForce) override;
    unsigned char GetBoundSphere(float* OutRadius, Vector* OutCenter) override;
    unsigned char Draw(ICamera* Camera, FDeformPart* Part) override;

    unsigned long ParseTag;     // +0x0C FVF (0x112 after UTF)
    int PointIndexCount;        // +0x10
    unsigned long* PointIndices;         // +0x14
    int FaceGroupCount;         // +0x18
    FFaceGroup* FaceGroups;     // +0x1C counted body
    // Read only by slot 1 (LAB_065f8b60, added to +0x10). Nothing in deformable2.dll writes it,
    // so it stays 0 from the calloc of FUN_065f26f0.
    unsigned long Field_0x20;   // +0x20 TODO: meaning (never written)
    int VertexCount;            // +0x24 Points bytes / 0xC
    void* Vertices;             // +0x28 count * 0x18
    FSkinPair* SkinPairs;       // +0x2C flattened id/weight
    FSkinVertex* SkinTable;     // +0x30 vertexCount * 8
    FBoneVertexList* BoneVerts; // +0x34 counted 0x10, indexed by bone id
    unsigned char* FaceFlags;            // +0x38 byte[VertexCount]
    int FaceFlagCount;          // +0x3C = VertexCount
    // Per-draw skinning and UV transform counters.
    unsigned int SkinnedVertexCount; // +0x40
    unsigned int SkinnedBoneCount;   // +0x44
    unsigned long EyeUvTransformCount; // +0x48
    int UvCount;                // +0x4C  UV0 bytes >> 3
    void* UvData;               // +0x50  count * 8
    unsigned long* UvIndices;            // +0x54  UV0_indices
    int UvDeformCount;          // +0x58 FUN_065f8480 rows
    FUvDeformRecord* UvDeform;  // +0x5C count * 0x24
    // +0x60 the allocator byte of the remap vector; the ctor stores the low byte of Name in it.
    unsigned char RemapAllocator;
    unsigned char Pad61[3];
    FVertexRemapList* RemapStart;    // +0x64 FUN_065f75f0
    FVertexRemapList* RemapFinish;   // +0x68
    FVertexRemapList* RemapCapacity; // +0x6C
    // +0x70 LodIndex < FDeformPartDesc::LodLimit: bones are skinned; else they stay identity.
    unsigned char bSkinned;
    unsigned char SphereReady;  // +0x71 LAB_065f8d00 cache
    unsigned char Pad72[2];
    Vector SphereCenter;        // +0x74
    float SphereRadius;         // +0x80

    // FUN_065f90c0. Defined in DeformablePartArchetype.cpp.
    void FillStreamDesc(FRpVertexSrcDesc* Out);

    // FUN_065f7e80. Defined in DeformablePartArchetype.cpp.
    bool LoadGeometry(IFileSystem* Fs);

    // FUN_065f8480. Defined in DeformablePartArchetype.cpp.
    bool LoadUvDeformTables(IFileSystem* Fs);

    // FUN_065f89e0. Defined in DeformablePartArchetype.cpp.
    bool LoadTables(FLodHolder* Holder, IFileSystem* Fs);

    // FUN_065f7760 (thiscall). Defined in DeformablePartArchetype.cpp.
    bool LoadFaceGroups(IFileSystem* Fs);
};

static_assert(sizeof(FMeshArchetype) == 0x84, "mesh archetype must be 0x84");
static_assert(offsetof(FMeshArchetype, Field_0x20) == 0x20, "never-written field +20");
static_assert(offsetof(FMeshArchetype, SkinPairs) == 0x2C, "skin pairs +2C");
static_assert(offsetof(FMeshArchetype, SkinTable) == 0x30, "skin table +30");
static_assert(offsetof(FMeshArchetype, BoneVerts) == 0x34, "bone vertex lists +34");
static_assert(offsetof(FMeshArchetype, FaceFlags) == 0x38, "face flags +38");
static_assert(offsetof(FMeshArchetype, FaceFlagCount) == 0x3C, "face flag count +3C");
static_assert(offsetof(FMeshArchetype, UvCount) == 0x4C, "UV0 count +4C");
static_assert(offsetof(FMeshArchetype, UvIndices) == 0x54, "UV0_indices +54");
static_assert(offsetof(FMeshArchetype, UvDeformCount) == 0x58, "UV-deform count +58");
static_assert(offsetof(FMeshArchetype, RemapStart) == 0x64, "vertex remap +64");
static_assert(offsetof(FMeshArchetype, SphereReady) == 0x71, "sphere cache flag +71");
static_assert(offsetof(FMeshArchetype, SphereCenter) == 0x74, "sphere center +74");
static_assert(offsetof(FMeshArchetype, SphereRadius) == 0x80, "sphere radius +80");

// FUN_065fdcc0 IVMeshLibrary wrapper 0x14. Vtable PTR_LAB_066013dc.
struct FVmeshWrapper : FDeformMesh
{
    FVmeshWrapper(const char* Name, ArchetypeIndex Handle, IVMeshPart* Part);
    ~FVmeshWrapper() override;
    unsigned char Draw(ICamera* Camera, FDeformPart* Part) override;

    ArchetypeIndex EngineHandle; // +0x0C IEngine +0x30 archetype handle
    // +0x10 AddRef'd in the ctor: what IEngine +0x48 answers for "1.11_IVMeshLibrary" on the
    // archetype -- RendComp's VMesh part answers that IID with its IVMeshPart face
    // (FVMeshPart::QueryInterface, LAB_06c31d10), and LAB_065fdd10 calls its +0x0C Draw.
    IVMeshPart* VMeshPart;
};

static_assert(sizeof(FVmeshWrapper) == 0x14, "VMesh wrapper must be 0x14");

// The EyeMaterial classes (LAB_065fe070 operator new(0x78)) live in EyeMaterial.h/.cpp.

// Shared between the Deformable2 source files and EyeMaterial.cpp (module-internal, not exported).
extern IEngine* G_Engine;                                        // DAT_06603134
char* FormatDeformMessage(char* Buffer, const char* Format, ...); // FUN_065f28b0
void MathTransformVec3(Vector* Dest, const Matrix* Rotation, const Vector* In); // FUN_065f7120
void FillFileDesc(FFileSystemDesc* Desc, const char* Path);      // FUN_065f3e80

// Aggregate descriptor used to create the Deformable component.
struct FAggDesc18
{
    unsigned long Size;
    const char* NameAt4;
    IDAComponent* Outer;
    void** InnerOut;
    unsigned long Pad10;
    IDAComponent* SystemAt14;
};

// Factory FUN_065f2250: operator_new(0xC). CreateInstance stays on derived types.
struct FDeformableFactory : IComponentFactory
{
    unsigned long RefCount;
    const char* Name;

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;
};

// Shared implementation and its concrete DACOM aggregate wrapper.
struct FDeformable : IDeformable, IEngineComponent
{
    FDeformable();  // FUN_065f1810
    ~FDeformable(); // FUN_065f18d0

    GENRESULT DACOM_API Create(void** OutInstance, FDeformPartTable* Parts, void* Arg3, void* Arg4,
                                void* Arg5) override;
    GENRESULT DACOM_API CreateUnused(void* A, void* B, void* C, void* D, void* E, void* F,
                                       void* G) override;
    GENRESULT DACOM_API CreateAlias(void** OutInstance, FDeformPartTable* Parts, void* Arg3, void* Arg4,
                                      void* Arg5) override;
    GENRESULT DACOM_API Destroy(void* Instance) override;
    GENRESULT DACOM_API Play(void* Instance, int* Out, void* Name, void* Extra, void* ArgC,
                              void* ArgD, void* ArgE, void* ArgF, void* ArgG, void* ArgH,
                              void* Script) override;
    GENRESULT DACOM_API PlayAlt(void* Instance, int* Out, void* Name, void* Extra, void* ArgC,
                                  void* ArgD, void* ArgE, void* ArgF, void* ArgG, void* ArgH,
                                  InstanceIndex Joint) override;
    GENRESULT DACOM_API DestroyAll(void* Instance) override;
    GENRESULT DACOM_API DestroyHandle(void* Instance, int Handle) override;
    GENRESULT DACOM_API StartAim(void* Instance, void** Out, unsigned char* AimDesc,
                                  void* Arg3) override;
    GENRESULT DACOM_API StopAim(void* Instance, void* Node) override;
    GENRESULT DACOM_API Unused13(void* A) override;
    GENRESULT DACOM_API Unused14(void* A) override;
    GENRESULT DACOM_API SetLod(void* Instance, float Dt, int* LodIds) override;
    GENRESULT DACOM_API Draw(void* Instance, void* Camera, void* UnusedB, void* RenderArg,
                              int* OutLod, unsigned int Flags) override;
    GENRESULT DACOM_API Unused17(void* A, void* B, void* C, void* D) override;
    GENRESULT DACOM_API GetFirstBone(void* Instance, int* Out) override;
    GENRESULT DACOM_API SetPosePos(void* Instance, const Vector* Pos) override;
    GENRESULT DACOM_API SetPoseOri(void* Instance, const Matrix* Ori) override;
    GENRESULT DACOM_API Unused21(void* A, void* B) override;
    GENRESULT DACOM_API Unused22(void* A, void* B) override;
    GENRESULT DACOM_API Unused23(void* A, void* B) override;
    GENRESULT DACOM_API SetHeight(void* Instance, float Height) override;
    GENRESULT DACOM_API Unused25(void* A, void* B, void* C, void* D) override;
    GENRESULT DACOM_API GetListCount(void* Instance, int* Out) override;
    GENRESULT DACOM_API CopyList(void* Instance, int* Out) override;
    GENRESULT DACOM_API Unused28(void* A, void* B) override;
    GENRESULT DACOM_API Unused29(void* A, void* B, void* C, void* D, void* E, void* F, void* G,
                                  void* H, void* I) override;
    GENRESULT DACOM_API Unused30(void* A, void* B) override;
    GENRESULT DACOM_API Unused31(void* A, void* B) override;
    GENRESULT DACOM_API Unused32(void* A, void* B) override;
    GENRESULT DACOM_API Unused33(void* A, void* B, void* C) override;
    GENRESULT DACOM_API Unused34(void* A, void* B, void* C, void* D) override;
    GENRESULT DACOM_API GetOffset(void* Instance, float* Out) override;
    GENRESULT DACOM_API WalkHardpoints(void* Instance, void* UserFn, void* UserData) override;
    GENRESULT DACOM_API GetMeshStats(void* Instance, unsigned int* Out) override;
    GENRESULT DACOM_API GetBoundSphere(void* Instance, float* OutRadius, Vector* OutCenter) override;
    int DACOM_API Initialize() override;
    int DACOM_API CreateArchetype(ArchetypeIndex Arg1, IFileSystem* Arg2) override;
    int DACOM_API DuplicateArchetype(ArchetypeIndex Arg1, ArchetypeIndex Arg2) override;
    int DACOM_API DestroyArchetype(ArchetypeIndex Arg1) override;
    int DACOM_API QueryArchetype(ArchetypeIndex Arg1, const char* Iid, void** Out) override;
    int DACOM_API CreateInstance(InstanceIndex Arg1, ArchetypeIndex Arg2) override;
    int DACOM_API DestroyInstance(InstanceIndex Arg1) override;
    int DACOM_API UpdateInstance(InstanceIndex Arg1, void* Arg2) override;
    int DACOM_API QueryInstance(InstanceIndex Arg1, const char* Iid, void** Out) override;
    int DACOM_API Notify(void* Arg1) override;
};

// Nondelegating unknown owned by the concrete aggregate.
struct FDeformableObject;

struct FDeformableInner : IDAComponent
{
    unsigned long RefCount;    // +0x04 (object +0x10)
    FDeformableObject* Owner;  // +0x08 (object +0x14)

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;
};

// Component FUN_065f22f0: operator_new(0x18). IDeformable @ +0, IEngineComponent @ +4.
struct FDeformableObject : FDeformable
{
    IDAComponent* Controlling; // +0x08 descriptor outer or the inner unknown
    FDeformableInner Inner;  // +0x0C

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;

    // FUN_065f1900 (thiscall). Defined in Deformable.cpp.
    GENRESULT ApplyCreateInstanceInit(FAggDesc18* Desc);
};

// FUN_065f36c0 ctor: 150 * 0x34 + header. FUN_065f13e0 identity at bone+4.
struct FBoneSlot
{
    int Parent;
    Transform BoneToRoot;
};

static_assert(sizeof(FBoneSlot) == 0x34, "bone slot must be 0x34");

struct FSkeleton
{
    float Scale;
    int BoneCount;
    FBoneSlot Bones[150];
    ArchetypeIndex EngineHandle;
    int RefCount;

    // FUN_065f10c0. Defined in Deformable.cpp.
    void DestroyHandles();

    // FUN_065f1100. Defined in Deformable.cpp.
    bool Load(const char* Name, IFileSystem* Fs);

    // FUN_065f1420 (thiscall). Defined in Deformable.cpp.
    int Release();

    // FUN_065f1410 (thiscall). Defined in Deformable.cpp.
    void AddRef();
};

static_assert(sizeof(FSkeleton) == 0x1E88, "skeleton must be 0x1e88");
static_assert(offsetof(FSkeleton, EngineHandle) == 0x1E80, "skel handle +1e80");

// FBoneRuntime::Flags. Bit 0 is set from the skeleton slot (not named yet).
enum EBoneFlag
{
    kBoneFlagPoseChanged = 0x2, // a set_* call or joint_changed marked the pose; the skinning pass clears it
};

// Engine instance callbacks: local pose, dirty flags and computed world pose.
struct FBoneRuntime : IEngineInstance
{
    InstanceIndex Instance;   // +0x04 engine instance (slot 0), -1 until bound
    FBoneSlot* Source;        // +0x08 &Skeleton->Bones[i]
    Transform LocalTransform; // +0x0C rotation followed by position
    unsigned char Flags;
    unsigned char FlagPadding[3];
    Transform WorldMatrix;    // +0x40

    // The per-slot body of the FUN_065f1550 construct loop.
    FBoneRuntime();

    void DACOM_API initialize_instance(long Inst) override;                           // LAB_065f1690
    void DACOM_API create_instance(long Inst) override;                               // FUN_065f16a0
    void DACOM_API destroy_instance(long Inst) override;                              // FUN_065f16a0
    void DACOM_API set_position(long Inst, Vector const& Pos) override;               // LAB_065f16b0
    Vector const& DACOM_API get_position(long Inst) const override;                   // LAB_065f16e0
    void DACOM_API set_orientation(long Inst, Matrix const& Orient) override;         // LAB_065f16f0
    Matrix const& DACOM_API get_orientation(long Inst) const override;                // LAB_065f1720
    void DACOM_API set_transform(long Inst, Transform const& Xform) override;         // LAB_065f1730
    Transform const& DACOM_API get_transform(long Inst) const override;               // LAB_065f1720
    void DACOM_API get_centered_radius(long Inst, float* OutRadius,
                                       Vector* OutCenter) const override;             // LAB_065f1760
    void DACOM_API set_centered_radius(long Inst, float Radius,
                                       Vector const& Center) override;                // LAB_065f1780
    void DACOM_API set_instance_flags(long Inst, unsigned long InstFlags) override;   // LAB_065f1790
    unsigned long DACOM_API get_instance_flags(long Inst) const override;             // LAB_065f17a0
    bool DACOM_API joint_changed(long Inst) override;                                 // LAB_065f17b0

    // FUN_065f1030. Defined in Deformable.cpp.
    void ComposeWorldMatrix(const Transform* InverseRoot);
};

static_assert(sizeof(FBoneRuntime) == 0x70, "bone runtime must be 0x70");
static_assert(offsetof(FBoneRuntime, LocalTransform) == 0x0C, "bone local transform +0c");
static_assert(offsetof(FBoneRuntime, Flags) == 0x3C, "bone flag byte +3c");
static_assert(offsetof(FBoneRuntime, WorldMatrix) == 0x40, "bone world matrix +40");

// FUN_065f1550 LOD/skel holder 0x10.
struct FLodHolder
{
    FSkeleton* Skeleton;
    FBoneRuntime* BoneSlots;
    int BindCursor;              // +8 FUN_065f14f0 walk (also slot count after bind)
    int RefCount;

    unsigned char AttachSkeleton(FSkeleton* Skel); // FUN_065f1550

    // FUN_065f14a0. Defined in Deformable.cpp.
    FBoneRuntime* BindOneBoneRuntime(int Index, InstanceIndex BoneInst);

    // FUN_065f14f0. Defined in Deformable.cpp.
    bool LinkBoneByParent(InstanceIndex BoneInst, int Parent);

    // FUN_065f17d0 (thiscall). Defined in Deformable.cpp.
    void AddRef();

    // FUN_065f17e0 (thiscall). Defined in Deformable.cpp.
    int Release();

    // FUN_065f1470 (thiscall). Defined in Deformable.cpp.
    void DestroyBody();
};

static_assert(sizeof(FLodHolder) == 0x10, "LOD holder must be 0x10");

// FUN_065f9f00 / FUN_065fadb0: 12-stride Golub SVD (IK Jacobian ≤ 4×3).
static const int G_SvdMaxDim = 12;
static const int G_SvdMaxIter = 30;
static const int G_IkMaxJoints = 4;

struct FSvdMatrix
{
    float Cell[G_SvdMaxDim][G_SvdMaxDim]; // +0x00 stride 0xC
    int RowCount;       // +0x240 this+0x240
    int ColCount;       // +0x244 this+0x244

    // FUN_065f9f00. Defined in DeformIK.cpp.
    unsigned char SvdCmp(float* A, float* W, float* V);

    // FUN_065fadb0. Defined in DeformIK.cpp.
    void PseudoInverse(FSvdMatrix* Out);

    // FUN_065f9d70. Defined in DeformIK.cpp.
    void Transpose(FSvdMatrix* Out) const;
};

static_assert(sizeof(FSvdMatrix) == 0x248, "SVD matrix must be 0x248");
static_assert(offsetof(FSvdMatrix, RowCount) == 0x240, "SVD rows +240");
static_assert(offsetof(FSvdMatrix, ColCount) == 0x244, "SVD cols +244");

// FUN_065f5400 list node 0x14. FUN_065fb030 IK event 0x44.
// Mixed list @ instance+0x0C: Active==0 animation handle @+4; Active!=0 FIkEvent*.
struct FIkListNode
{
    unsigned char Active; // +0 = 0 anim (FUN_065f41d0); 1 IK (FUN_065f5400)
    unsigned char Pad01[3];
    struct FIkEvent* Event; // +4 handle or FIkEvent*
    unsigned long StartHeight; // +8 float bits: the root height FUN_065f41d0 played at
    FIkListNode* Prev; // +0xC
    FIkListNode* Next; // +0x10
};

static_assert(sizeof(FIkListNode) == 0x14, "IK list node must be 0x14");
static_assert(offsetof(FIkListNode, StartHeight) == 0x08, "list node start height +8");

// FIkEvent::StateFlags.
enum EIkStateFlag
{
    kIkStateStopped = 0x1,    // FUN_065f5950: Apply returns the current joint rotation
    kIkStateSphereAxis = 0x2, // the DLS solve takes the sphere-axis variant (SolveDlsAlt)
};

// IK event owns its typed channels and serves as their owner callback.
struct FIkEvent : IChannelOwner
{
    FIkEvent() {}            // the inlined constructor's field writes stay at the call site
    ~FIkEvent();             // FUN_065fbf40
    FIkEvent(const FIkEvent&) = delete;
    FIkEvent& operator=(const FIkEvent&) = delete;

    // LAB_065fb540 (vtable +0x00).
    int DACOM_API Apply(float* Sampled, unsigned long ChannelKey, void* BindDesc,
                        float Time) override;

    FIkListNode* Owner;      // +0x04
    int* JointIds;           // +0x08
    Matrix* BindMats;        // +0x0C count * 0x24
    int StartJoint;          // +0x10
    int EndJoint;            // +0x14
    void** Channels;         // +0x18
    Quaternion* Quats;       // +0x1C count * 0x10
    Vector* TargetA;         // +0x20 desc+0x10
    Matrix* TargetB;         // +0x24 desc+0x14
    // +0x28 desc+0x0C: bit 0 TargetA is a point (else a direction), 0x20 / 0x40 position /
    // orientation target, 0x80 no rest pull and no joint-limit clamp.
    unsigned long AimFlags;
    unsigned char* LockBytes;// +0x2C
    int LockCount;           // +0x30 desc+0x1C, locked joint ids at desc+0x20
    unsigned char StateFlags; // +0x34 bit 0 stopped (FUN_065f5950), bit 1 sphere axis (DLS alt)
    unsigned char Pad35[3];
    float Damping;           // +0x38 desc+0x18, the DLS damping factor
    int ApplyTick;           // +0x3C Apply calls this round; solves on 0, wraps at JointCount
    int JointCount;          // +0x40 FUN_065fb030

    // FUN_065fc000. Defined in DeformIK.cpp.
    void SolveDls();

    // FUN_065fce60. Defined in DeformIK.cpp.
    void SolveDlsAlt();

    // FUN_065fb030 (thiscall). Defined in DeformIK.cpp.
    bool SetJointCount(unsigned int JointCount);
};

static_assert(sizeof(FIkEvent) == 0x44, "IK event must be 0x44");
static_assert(offsetof(FIkEvent, JointIds) == 0x08, "IK joints +8");
static_assert(offsetof(FIkEvent, AimFlags) == 0x28, "IK aim flags +28");
static_assert(offsetof(FIkEvent, StateFlags) == 0x34, "IK state flags +34");
static_assert(offsetof(FIkEvent, Damping) == 0x38, "IK damping +38");
static_assert(offsetof(FIkEvent, JointCount) == 0x40, "IK joint count +40");

// LAB_065fb540 arg4: +0 skip DLS; +0xC joint for +0xC4/+0xD4.
struct FIkApplyGate
{
    int SkipFlag;
    unsigned char Pad04[8];
    int Joint;    // +0x0C
};

static_assert(offsetof(FIkApplyGate, Joint) == 0x0C, "IK apply gate +C");

struct FIkMathDesc
{
    unsigned long Size;
    const char* Name;
    void* Extra;
};

static_assert(sizeof(FIkMathDesc) == 0xC, "3DMathEngine desc must be 0xC");

// Material-context VB listing followed by its allocation capacity.
struct FDeformVbDesc
{
    FVbListing Listing;
    int Capacity;
};

// The part's dynamic index buffer (part +0x24): {Handle, lock offset, capacity}; the first
// eight bytes are the IB scratch the material context points at (ctx+0x18).
struct FDeformIbDesc
{
    FIbScratch Scratch;
    int Capacity;
};

static_assert(sizeof(FDeformVbDesc) == 0x10, "part VB descriptor is 0x10");
static_assert(sizeof(FDeformIbDesc) == 0xC, "part IB descriptor is 0xC");

// FUN_065f6720 constructs it (13 zero dwords, then byte +0x30 |= 1).
struct FDeformPart
{
    FLodHolder* Lod;             // +0x00
    FDeformMesh* Current;        // +0x04
    FDeformMesh** Meshes;        // +0x08 counted body (FUN_065f67f0)
    int MeshCount;               // +0x0C
    int LodIndex;                // +0x10
    FDeformVbDesc Vb;            // +0x14
    FDeformIbDesc Ib;            // +0x24
    // Bit 0: the LOD changed (FUN_065f6930) -- rebuild both buffers; bit 1: the skin is
    // stale (FUN_065f69c0). FUN_065f69d0 clears both after a draw.
    unsigned char Flags;         // +0x30 = 1
    unsigned char Pad31[3];

    unsigned char BindMeshes(FLodHolder* Holder, FDeformMesh** InMeshes, int Count); // FUN_065f67f0
    void Reset(); // FUN_065f6760

    // FUN_065f6930. Defined in DeformablePart.cpp.
    bool SelectLod(int LodIndex);

    // FUN_065f69c0. Defined in DeformablePart.cpp.
    void MarkSkinDirty();

    // FUN_065f6720. Defined in DeformablePart.cpp.
    void Construct();

    // FUN_065f68c0. Defined in DeformablePart.cpp.
    unsigned char BoundSphere(float* OutRadius, Vector* OutCenter);

    // FUN_065f69d0. Defined in DeformablePart.cpp.
    unsigned char DrawLive(ICamera* Camera, void* /*Unused*/);
};

static_assert(offsetof(FDeformPart, Vb) == 0x14, "part VB descriptor +0x14");
static_assert(offsetof(FDeformPart, Ib) == 0x24, "part IB descriptor +0x24");
static_assert(offsetof(FDeformPart, Flags) == 0x30, "part flags +0x30");
static_assert(sizeof(FDeformPart) == 0x34, "part is 0x34");

// Callback of FDeformInstance::WalkHardpoints: bone instance, hardpoint name, user data.
typedef void(__cdecl* FHardpointUserFn)(InstanceIndex, char const*, void*);

// Playback notifications forward to the caller-provided channel sink.
struct FDeformInstance : IChannelSink
{
    // +0x00 vtable PTR_FUN_06601308 (3 slots)
    int PartCount;           // +0x04
    FDeformPart* PartArray;  // +0x08 counted array body (header at -4)
    FIkListNode* ListHead;          // +0x0C
    FIkListNode* ListTail;          // +0x10
    int ListCount;           // +0x14
    IChannelSink* ForwardSink;  // +0x18 Create arg3: the sink the notifications go on to
    void* DefaultScript;        // +0x1C Create arg4: the script of a Play without one
    float BaseHeight;           // +0x20 SetHeight (FUN_065f50b0); added to the pose Y
    float RootHeightOffset;     // +0x24 FUN_065f4de0: root bone Y (bone+0x34) - BaseHeight
    float RateCookie;        // +0x28 DAT_0660314c / rand phase

    void DACOM_API OnKeys(unsigned long ChannelKey, void* Context,
                          FChannelKeyBatch* Batch) override;                 // FUN_065f5210
    void DACOM_API OnOpaqueNotification(void* A, void* B) override; // LAB_065f5290
    void DACOM_API OnLoop(unsigned long ChannelKey, Transform* Xform,
                          void* Context) override;                           // LAB_065f52b0

    // FUN_065f4c80. Defined in DeformableObject.cpp.
    void DestroyAllList();

    // FUN_065f4d30. Defined in DeformableObject.cpp.
    void DestroyListByHandle(int Handle);

    // FUN_065f4de0. Defined in DeformableObject.cpp.
    void SweepExpiredList();

    // FUN_065f5400. Defined in DeformableObject.cpp.
    FIkListNode* AllocAimEvent(const unsigned char* Desc, void* PlayExtra);

    // FUN_065f3650. Defined in DeformableObject.cpp.
    void ConstructBody();

    // FUN_065f36c0. Defined in DeformableObject.cpp.
    bool ConnectParts(FDeformPartTable* Parts, void* Arg3, void* Arg4);

    // FUN_065f3ed0. Defined in DeformableObject.cpp.
    void Teardown();

    // FUN_065f4070. Defined in DeformableObject.cpp.
    void DrawParts(void* /*Unused*/, ICamera* Camera, int* OutLod, void* RenderArg,
                   unsigned long Flags);

    // FUN_065f41d0. Defined in DeformableObject.cpp.
    int PlayAnim(void* Name, void* Extra, void* ArgC, void* ArgD, void* ArgE, void* ArgF,
                 void* ArgG, void* ArgH, void* ScriptOrNull);

    // FUN_065f4660. Defined in DeformableObject.cpp.
    int PlayAnimChain(void* Name, void* Extra, void* ArgC, void* ArgD, void* ArgE, void* ArgF,
                      void* ArgG, void* ArgH, InstanceIndex Joint);

    // FUN_065f59d0. Defined in DeformableObject.cpp.
    unsigned char WalkHardpoints(FHardpointUserFn UserFn, void* UserData);
};

static_assert(sizeof(FDeformableFactory) == 0x0C, "factory must be 0xC");
static_assert(sizeof(FDeformableObject) == 0x18, "component must be 0x18");
static_assert(offsetof(FDeformableObject, Controlling) == 0x08, "deformable Controlling +8");
static_assert(offsetof(FDeformableObject, Inner) == 0x0C, "deformable inner +C");
static_assert(sizeof(FDeformableInner) == 0x0C, "deformable inner is 0xC (ref +0x10, owner +0x14)");
static_assert(sizeof(FDeformPart) == 0x34, "part must be 0x34");
static_assert(offsetof(FDeformPart, Meshes) == 0x08, "part meshes +8");
static_assert(offsetof(FDeformPart, MeshCount) == 0x0C, "part mesh count +C");
static_assert(offsetof(FDeformPart, Vb) + offsetof(FDeformVbDesc, Listing) +
                      offsetof(FVbListing, VertexBuffer) == 0x14, "part VB handle +14");
static_assert(offsetof(FDeformPart, Vb) + offsetof(FDeformVbDesc, Listing) +
                      offsetof(FVbListing, Fvf) == 0x18, "part VB FVF +18");
static_assert(offsetof(FDeformPart, Vb) + offsetof(FDeformVbDesc, Listing) +
                      offsetof(FVbListing, BaseVertex) == 0x1C, "part VB lock offset +1C");
static_assert(offsetof(FDeformPart, Vb) + offsetof(FDeformVbDesc, Capacity) == 0x20,
              "part vertex capacity +20");
static_assert(offsetof(FDeformPart, Ib) + offsetof(FDeformIbDesc, Scratch) +
                      offsetof(FIbScratch, IndexBuffer) == 0x24, "part IB handle +24");
static_assert(offsetof(FDeformPart, Ib) + offsetof(FDeformIbDesc, Scratch) +
                      offsetof(FIbScratch, Bias) == 0x28, "part IB lock offset +28");
static_assert(offsetof(FDeformPart, Ib) + offsetof(FDeformIbDesc, Capacity) == 0x2C,
              "part index capacity +2C");
static_assert(offsetof(FDeformPart, Flags) == 0x30, "part flags +30");
static_assert(sizeof(FDeformInstance) == 0x2C, "create instance must be 0x2C");
static_assert(offsetof(FDeformInstance, ListHead) == 0x0C, "instance list head +0c");
static_assert(offsetof(FDeformInstance, ListTail) == 0x10, "instance list tail +10");
static_assert(offsetof(FDeformInstance, BaseHeight) == 0x20, "instance base height +20");
static_assert(offsetof(FDeformInstance, RootHeightOffset) == 0x24, "instance root height offset +24");

// ---- File-scope helpers shared between the Deformable2 source files -----------------------

// Deformable.cpp
extern IMaterialLibrary* G_MaterialLibrary;
extern IRenderPipeline* G_Pipeline;
extern IAnimation2* G_Animation;
extern IChannel* G_Channel;
extern IRenderer2* G_Renderer;
extern IRPVertexBuffer* G_VertexBuffer;
extern IRPIndexBuffer* G_IndexBuffer;
extern float G_DeformRate;
void WriteIdentity3x3(Matrix* Dest);

// DeformablePartArchetype.cpp
extern const char* const G_ParentNode;

// Deformable2.cpp
int FsFindFirst(IFileSystem* Fs, const char* Pattern, WIN32_FIND_DATAA* FindData);
int FsFindNext(IFileSystem* Fs, int Finder, WIN32_FIND_DATAA* FindData);
void FsFindClose(IFileSystem* Fs, int Finder);
I3DMathEngine* AcquireMathEngine();

// DeformablePart.cpp
void BuildVertexRemap(FMeshArchetype* Arch);
void BindUvBoneIdsToFaceGroups(FMeshArchetype* Arch);

// DeformableMesh.cpp
FDeformMesh* LoadDeformMesh(FLodHolder* Holder, IFileSystem* MeshFs, const char* Name,
                            unsigned char bSkinned);

// DeformableObject.cpp
void IkMathQuatToMatrix(Matrix* Out, const Quaternion* Quat);
InstanceIndex FirstBoneInstance(FDeformInstance* Instance);
void StopAimNode(FIkListNode* Node);
void IkEulerRebuild(Matrix* Out, float Yaw, float Pitch, float Roll);
void WriteVec3(float* Dest, float X, float Y, float Z);
void ScaleVec3(float* Out, const float* In, float Scale);
void SubVec3(float* Out, const float* A, const float* B);

