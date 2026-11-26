// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "dacom.h"
#include "render/IRenderPipeline.h"
#include "x86math.h"
#include "luaprofile.h"
#include "render/shading.h"
#include "readfile.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>
#include "MaterialRegex.h"
#include <utility>
#include <string>
#include <vector>

static const void* kRegisterPriority = (const void*)0x80000000;

// D3D8 numbers (enum names are d3d8.h matches, not DLL strings).
static const D3DRENDERSTATETYPE kRsLighting = D3DRS_LIGHTING;
static const D3DRENDERSTATETYPE kRsColorVertex = D3DRS_COLORVERTEX;
static const D3DRENDERSTATETYPE kRsAlphaBlendEnable = D3DRS_ALPHABLENDENABLE;
static const D3DRENDERSTATETYPE kRsSrcBlend = D3DRS_SRCBLEND;
static const D3DRENDERSTATETYPE kRsDestBlend = D3DRS_DESTBLEND;
static const D3DRENDERSTATETYPE kRsZWriteEnable = D3DRS_ZWRITEENABLE;
static const D3DRENDERSTATETYPE kRsZFunc = D3DRS_ZFUNC;
static const D3DRENDERSTATETYPE kRsCullMode = D3DRS_CULLMODE;
static const D3DRENDERSTATETYPE kRsAlphaTestEnable = D3DRS_ALPHATESTENABLE;

static const unsigned long kTssColorOp = D3DTSS_COLOROP;
static const unsigned long kTssColorArg1 = D3DTSS_COLORARG1;
static const unsigned long kTssColorArg2 = D3DTSS_COLORARG2;
static const unsigned long kTssAlphaOp = D3DTSS_ALPHAOP;
static const unsigned long kTssAlphaArg1 = D3DTSS_ALPHAARG1;
static const unsigned long kTssAlphaArg2 = D3DTSS_ALPHAARG2;
static const unsigned long kTssTexCoordIndex = D3DTSS_TEXCOORDINDEX;
static const unsigned long kTssAddressU = D3DTSS_ADDRESSU;
static const unsigned long kTssAddressV = D3DTSS_ADDRESSV;

// DAT_06ed4848 dword[4]: NONE stays, CW↔CCW. Indexed by D3DRS_CULLMODE (0x16).
static const unsigned long kCullFlip[4] = {0, D3DCULL_NONE, D3DCULL_CCW, D3DCULL_CW};

// FShadingPropertyValue / FShadingPropertyRecord (FUN_06eb28f0, stride 0x4C) live in
// render/shading.h because freelancer.exe's SwappedMaterial reads them.

inline void PipeSetRs(IRenderPipeline* Pipeline, D3DRENDERSTATETYPE State, unsigned long Value)
{
    if (Pipeline != nullptr)
        Pipeline->SetRenderState(State, (int)Value);
}

// IRenderPipeline +0xC8 slot 50 — get_texture_format (FUN_06d0e3d9).
inline GENRESULT PipeGetTextureFormat(IRenderPipeline* Pipeline, unsigned long Handle,
                                      int* OutFormat)
{
    if (Pipeline == nullptr || OutFormat == nullptr)
        return GR_GENERIC;
    return Pipeline->GetTextureFormat((void*)Handle, OutFormat);
}

static const unsigned long kPixelFormatAlphaBits[15] = {
    0, 0, 0, 0, 0, 4, 1, 8, 0, 0, 1, 0, 0, 0, 0};
static const unsigned char kPixelFormatHasAlpha[15] = {0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1};
static const int kDxtFormatIndexMin = kTextureFormatDxt1;
static const int kDxtFormatIndexMax = kTextureFormatDxt5;
static const int kPixelFormatFourCcCount = 22;
// DAT_06ed51d8 — FUN_06ed0b50 / FUN_06ed0b80 (ids 9…21).
static const unsigned long kPixelFormatFourCc[22] = {
    D3DFMT_UNKNOWN, D3DFMT_P8, D3DFMT_R8G8B8, D3DFMT_R5G6B5, D3DFMT_X1R5G5B5, D3DFMT_A4R4G4B4,
    D3DFMT_A1R5G5B5, D3DFMT_A8R8G8B8, D3DFMT_X8R8G8B8, D3DFMT_UNKNOWN, D3DFMT_DXT1, D3DFMT_DXT2,
    D3DFMT_DXT3, D3DFMT_DXT4, D3DFMT_DXT5, 0x504F4144u, 0x544F4144u, 0x41414144u, 0x4C414144u,
    0x31414144u, 0x34414144u, 0x38414144u};

inline unsigned long PixelFormatAlphaBits(int FormatIndex)
{
    if (FormatIndex < 0 || FormatIndex > kTextureFormatDxt5)
        return kPixelFormatAlphaBits[0];
    return kPixelFormatAlphaBits[FormatIndex];
}

inline unsigned char PixelFormatHasAlpha(int FormatIndex)
{
    if (FormatIndex < 0 || FormatIndex > kTextureFormatDxt5)
        return kPixelFormatHasAlpha[0];
    return kPixelFormatHasAlpha[FormatIndex];
}

inline void PipeSetTss(IRenderPipeline* Pipeline, unsigned long Stage, unsigned long Type,
                       unsigned long Value)
{
    if (Pipeline != nullptr)
        Pipeline->SetTextureStageState((int)Stage, (int)Type,
                                       (int)Value);
}

inline void PipeSetTexture(IRenderPipeline* Pipeline, unsigned long Stage, unsigned long Handle)
{
    if (Pipeline != nullptr)
        Pipeline->SetTextureStageTexture((int)Stage,
                                         (void*)Handle);
}

// Convert the published opaque file-system argument at the local boundary.
inline IFileSystem* AsFileSystem(void* FileSystem)
{
    return (IFileSystem*)FileSystem;
}

inline IVertexBufferManager* AsVbMgr(void* VbMgr)
{
    return (IVertexBufferManager*)VbMgr;
}

inline IRPDraw* AsRpDraw(void* Draw)
{
    return (IRPDraw*)Draw;
}

inline GENRESULT RpDrawIndexed(void* Draw, unsigned long Prim, unsigned long MinIndex,
                               unsigned long NumVertices, unsigned long StartIndex,
                               unsigned long Count)
{
    if (Draw == nullptr)
        return GR_GENERIC;
    return AsRpDraw(Draw)->DrawIndexedPrimitive(Prim, (unsigned int)MinIndex,
                                      (unsigned int)NumVertices,
                                      (unsigned int)StartIndex,
                                      (unsigned int)Count);
}

// FUN_06eb5940
inline unsigned long FvfPosSize(unsigned long Fvf)
{
    const unsigned char Bits = (unsigned char)Fvf;
    if ((Bits & D3DFVF_XYZB4) == D3DFVF_XYZB4)
        return 0x1C;
    if ((Bits & D3DFVF_XYZRHW) == D3DFVF_XYZRHW)
        return 0x10;
    if ((Bits & D3DFVF_XYZ) == D3DFVF_XYZ)
        return 0xC;
    return 0;
}

// FUN_06eb5980
inline unsigned long FvfColorSize(unsigned long Fvf)
{
    unsigned long Size = 0;
    if ((Fvf & D3DFVF_DIFFUSE) != 0)
        Size = 4;
    if ((Fvf & D3DFVF_SPECULAR) != 0)
        Size += 4;
    return Size;
}

// FUN_06eb3310
inline unsigned long FvfTexSize(unsigned long Fvf)
{
    unsigned long Size = 0;
    unsigned long Remaining = (Fvf >> 8) & 0xF;
    unsigned char Shift = 0x10;
    while (Remaining != 0)
    {
        switch ((Fvf >> (Shift & 0x1F)) & 3)
        {
        case D3DFVF_TEXTUREFORMAT2:
            Size += 8;
            break;
        case D3DFVF_TEXTUREFORMAT3:
            Size += 0xC;
            break;
        case D3DFVF_TEXTUREFORMAT4:
            Size += 0x10;
            break;
        case D3DFVF_TEXTUREFORMAT1:
            Size += 4;
            break;
        }
        Shift = (unsigned char)(Shift + 2);
        Remaining -= 1;
    }
    return Size;
}

inline unsigned long FvfNormalSize(unsigned long Fvf)
{
    if ((Fvf & D3DFVF_NORMAL) != 0)
        return 0xC;
    return 0;
}

inline unsigned long FvfStride(unsigned long Fvf)
{
    return FvfPosSize(Fvf) + FvfNormalSize(Fvf) + FvfColorSize(Fvf) + FvfTexSize(Fvf);
}

inline GENRESULT VbMgrLock(void* VbMgr, unsigned long Fvf, unsigned long Count,
                           FVertexMemoryLock* Out)
{
    if (VbMgr == nullptr || Out == nullptr)
        return GR_GENERIC;
    return AsVbMgr(VbMgr)->LockVertexMemory((unsigned int)Fvf,
                                   (unsigned int)Count, Out);
}

inline void VbMgrUnlock(void* VbMgr, FVertexMemoryLock* Out)
{
    if (VbMgr == nullptr || Out == nullptr)
        return;
    AsVbMgr(VbMgr)->UnlockVertexMemory(Out);
}

inline int FileSystemOpenNode(void* FileSystem, const char* Name)
{
    return AsFileSystem(FileSystem)->OpenNode(Name);
}

inline int FileSystemClose(void* FileSystem, unsigned long Handle)
{
    return (FileSystem != nullptr) ? AsFileSystem(FileSystem)->CloseFile(Handle) : 0;
}

inline int FileSystemRead(void* FileSystem, unsigned long Handle, void* Buffer,
                          unsigned long Size, unsigned long* BytesRead)
{
    return (FileSystem != nullptr) ? AsFileSystem(FileSystem)->Read(Handle, Buffer, Size, BytesRead, 0) : 0;
}

inline unsigned long FileSystemGetSize(void* FileSystem, unsigned long Handle)
{
    return (FileSystem != nullptr) ? AsFileSystem(FileSystem)->GetSize(Handle, 0) : 0;
}

inline int FileSystemFindFirst(void* FileSystem, const char* Pattern,
                               WIN32_FIND_DATAA* FindData)
{
    if (FileSystem == nullptr)
        return -1;
    return AsFileSystem(FileSystem)->FindFirst(Pattern,
                              FindData);
}

inline int FileSystemFindNext(void* FileSystem, int Handle, WIN32_FIND_DATAA* FindData)
{
    if (FileSystem == nullptr)
        return 0;
    return AsFileSystem(FileSystem)->FindNext((unsigned long)Handle,
                              FindData);
}

inline int FileSystemFindClose(void* FileSystem, int Handle)
{
    return (FileSystem != nullptr) ? AsFileSystem(FileSystem)->FindClose((unsigned long)Handle) : 0;
}

inline int FileSystemOpenByDesc(void* FileSystem, void* Desc)
{
    if (FileSystem == nullptr)
        return -1;
    return AsFileSystem(FileSystem)->OpenByDesc((FFileSystemDesc*)Desc);
}

// RS/TSS table walk:
// first dword = start state/type; [+4]=value; [+8]=next; stride 8; stop at next==0.
inline void ApplyStateTable(IRenderPipeline* Pipeline, const unsigned long* Table,
                            bool bTextureStage,
                            unsigned long Stage)
{
    if (Pipeline == nullptr || Table == nullptr)
        return;
    unsigned long State = Table[0];
    unsigned long Index = 0;
    if (State == 0)
        return;
    while (true)
    {
        const unsigned long Value = Table[Index + 1];
        if (bTextureStage)
            PipeSetTss(Pipeline, Stage, State, Value);
        else
            PipeSetRs(Pipeline, (D3DRENDERSTATETYPE)State, Value);
        const unsigned long Next = Table[Index + 2];
        if (Next == 0)
            break;
        State = Next;
        Index += 2;
    }
}

struct FSystemAggDesc
{
    unsigned long Size;
    const char* Name;
    IDAComponent* Outer;
    IDAComponent** InnerOut;
    const char* Extra;
};

// Shared ReadFile descriptor; Size selects its DOS/UTF prefix.
inline void InitializeShadingFileDesc(FFileSystemDesc* Desc, const char* Path,
                                      const char* ComponentName = "FileSystem")
{
    std::memset(Desc, 0, sizeof(*Desc));
    Desc->Size = offsetof(FFileSystemDesc, MemBuffer);
    Desc->ComponentName = ComponentName;
    Desc->Path = Path;
    Desc->Access = GENERIC_READ;
    Desc->ShareMode = FILE_SHARE_READ;
    Desc->Creation = OPEN_EXISTING;
    Desc->Flags = FILE_FLAG_SEQUENTIAL_SCAN | FILE_ATTRIBUTE_NORMAL;
    Desc->FindHandle = 0xFFFFFFFF;
}

inline bool ReadNamedBytes(void* FileSystem, const char* Name, void* Buffer, unsigned long Size)
{
    if (FileSystem == nullptr || Name == nullptr || Buffer == nullptr)
        return false;
    FFileSystemDesc Desc;
    InitializeShadingFileDesc(&Desc, Name);
    const int Handle = FileSystemOpenByDesc(FileSystem, &Desc);
    if (Handle == -1)
        return false;
    unsigned long BytesRead = 0;
    const int ReadOk =
        FileSystemRead(FileSystem, (unsigned long)Handle, Buffer, Size, &BytesRead);
    FileSystemClose(FileSystem, (unsigned long)Handle);
    // FUN_06eb3450 treats a zero Read result as failure even when the requested
    // byte count is reported.
    return ReadOk != 0 && BytesRead == Size;
}

inline int FileSystemWrite(void* FileSystem, unsigned long Handle, const void* Buffer,
                           unsigned long Size, unsigned long* BytesWritten)
{
    if (FileSystem == nullptr)
        return 0;
    return AsFileSystem(FileSystem)->Write(Handle, Buffer, Size, BytesWritten, nullptr);
}

inline bool WriteNamedBytes(void* FileSystem, const char* Name, const void* Buffer,
                            unsigned long Size)
{
    if (FileSystem == nullptr || Name == nullptr || Buffer == nullptr)
        return false;
    FFileSystemDesc Desc;
    InitializeShadingFileDesc(&Desc, Name);
    Desc.Implementation = "UTF";
    Desc.Access = GENERIC_READ | GENERIC_WRITE;
    Desc.Creation = CREATE_ALWAYS;
    const int Handle = FileSystemOpenByDesc(FileSystem, &Desc);
    if (Handle == -1)
        return false;
    unsigned long BytesWritten = 0;
    const int WriteOk = FileSystemWrite(FileSystem, (unsigned long)Handle, Buffer,
                                        Size, &BytesWritten);
    FileSystemClose(FileSystem, (unsigned long)Handle);
    return WriteOk != 0 && BytesWritten == Size;
}

struct FTexBlock
{
    const char* Name;           // +0
    int TextureId;              // +4
    unsigned long Handle;       // +8  TexLib +0x70 writes 5 dwords from here
    float Uv0;                  // +C
    float Uv1;                  // +10
    float Uv2;                  // +14
    float Uv3;                  // +18
    unsigned long Flags;        // +1C
    unsigned long AddressU;     // +20
    unsigned long AddressV;     // +24
    unsigned char bSkipResolve; // +28
    unsigned char Pad29[3];

    // Run for every element by the vector delete of a material's block array.
    ~FTexBlock()
    {
        delete[] Name;
        Name = nullptr;
    }
};

static_assert(sizeof(FTexBlock) == 0x2C, "FTexBlock stride 0x2C");

// Serialized frame first word selects a canonical child, never a GPU handle.
struct FTextureFrame
{
    unsigned long ChildIndex;
    float Uv[4];
};
static_assert(sizeof(FTextureFrame) == 0x14, "Texture frame file payload");

// Canonical name record: own GPU handle, frame descriptors and child pointers.
// Playback state is independently owned by FTexturePlayback in TextureLibrary.h.
struct FTextureEntry
{
    int RefCount;
    unsigned long GpuHandle; // retail canonical record+8
    unsigned long Crc;
    bool bHasName; // Distinguish retail null name from an allocated empty string.
    std::string FullName;
    char Name[128]; // Compatibility name mirror, identity uses FullName.
    std::vector<FTextureFrame> Frames;
    std::vector<FTextureEntry*> Children;
    float TileRate;

    FTextureEntry() : RefCount(0), GpuHandle(0), Crc(0), bHasName(false), TileRate(0.0f)
    {
        Name[0] = '\0';
    }

    // 06ec0ca4: release the owned name, then child storage, then frames.
    ~FTextureEntry()
    {
        std::string().swap(FullName);
        std::vector<FTextureEntry*>().swap(Children);
        std::vector<FTextureFrame>().swap(Frames);
    }

    const char* GetName() const
    {
        return bHasName ? FullName.c_str() : nullptr;
    }

    void SetName(const char* NewName)
    {
        bHasName = NewName != nullptr;
        if (NewName == nullptr)
        {
            FullName.clear();
            Name[0] = '\0';
            return;
        }
        FullName.assign(NewName);
        std::strncpy(Name, NewName, sizeof(Name) - 1);
        Name[sizeof(Name) - 1] = '\0';
    }
};

struct FMaterialMapRule
{
    int Key = kMapRuleType;
    bool bNamePattern = false;
    std::unique_ptr<FMaterialRegex> Expression;
    std::string Pattern;
    std::string Type;

    FMaterialMapRule() = default;
    // 06ecde20 / 06ece1c0: release program, pattern, then replacement type.
    ~FMaterialMapRule()
    {
        Expression.reset();
        std::string().swap(Pattern);
        std::string().swap(Type);
    }
    // 06ecd320: move owning fields, retain the source key.
    FMaterialMapRule(FMaterialMapRule&& Other) noexcept
        : Key(Other.Key), bNamePattern(Other.bNamePattern),
          Expression(std::move(Other.Expression)), Pattern(std::move(Other.Pattern)),
          Type(std::move(Other.Type))
    {
        Other.Pattern.clear();
        Other.Type.clear();
    }

    FMaterialMapRule& operator=(FMaterialMapRule&& Other) noexcept
    {
        Key = Other.Key;
        bNamePattern = Other.bNamePattern;
        Expression = std::move(Other.Expression);
        Pattern = std::move(Other.Pattern);
        Type = std::move(Other.Type);
        Other.Pattern.clear();
        Other.Type.clear();
        return *this;
    }
};

struct FMaterialGroup
{
    unsigned long Id;
    // Retail duplicates group names into heap storage; keep native ownership
    // dynamic so SetGroupName does not truncate valid long names.
    std::string Name;
    bool bHasName = false;
    std::vector<unsigned long> Members;
};

struct FCrcSlot : FMaterialLibraryRecord
{
    unsigned long Crc;
};

struct FBatchRule
{
    unsigned long Kind = 0;
    std::string Pattern;
    // Destruction is compiled program before original pattern, as in 8f20/9080.
    std::unique_ptr<FMaterialRegex> Expression;

    FBatchRule() = default;
    FBatchRule(FBatchRule&& Other) noexcept = default;
    FBatchRule& operator=(FBatchRule&& Other) noexcept
    {
        // vector erase moves over the removed element: discard its program first.
        Expression = std::move(Other.Expression);
        Pattern = std::move(Other.Pattern);
        Kind = Other.Kind;
        return *this;
    }
};

class FMaterial;
class FMaterialLibrary;
class FMaterialBatcher;
class FTextureLibrary;
class FMaterialAnimation;

void RegisterShadingComponents();
