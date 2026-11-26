// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Common/Graphics/swapped_material.h"

#include "DALib.h"
#include "common_misc.h"   // malloc_strcpy / malloc_strfree, DAEngine_create_filesystem / DAEngine_release_filesystem
#include "dacom.h"         // FDUMP, DACOM_CRC::GetCRC32
#include "render/shading.h" // IMaterialLibrary, ITextureLibrary2, FShadingPropertyRecord

#include <string.h>

namespace
{
    char const* const kSwappedMaterialCpp = "E:\\FL\\Scratch\\Source\\Client\\cmn\\SwappedMaterial.cpp";
    char const* const kNoDtName = "%s(%d) : *** ERROR: Cannot find Dt_name parameter in material %s";
    char const* const kNoMaterial = "%s(%d) : *** ERROR: cannot find material name %s\n";

    // The TextureLib slot 12 query: +0x04 is the name's CRC. Retail leaves +0x00 as whatever
    // the stack held; it is zero here.
    struct FTextureQuery
    {
        unsigned long Field_0x00; // never written by these callers
        unsigned long Crc;        // +0x04
    };

    // Slot 12 writes straight into the member (retail passes its address), so a miss leaves the
    // member as it was.
    void FindTextureRecord(char const* Name, void** Out)
    {
        FTextureQuery Query = {};
        Query.Crc = DACOM_CRC::GetCRC32(Name);
        DALib::TextureLib->FindTextureByCrc(&Query, Out);
    }

    // The record destructor FUN_0042cd00 / FUN_0042d1d0 inline: Saved (FUN_0042cfd0), then
    // Default and Current (FUN_0042d030 / FUN_0042d090).
    void ReleasePropertyRecord(FShadingPropertyRecord& Record)
    {
        Record.Maximum.ReleaseOwnedPayload();
        Record.Minimum.ReleaseOwnedPayload();
        Record.Current.ReleaseOwnedPayload();
    }

    // The material search FUN_0042cd00 / FUN_0042d1d0 inline: a prefix match (_strnicmp over
    // Name's length) in MaterialLib group 0.
    IMaterial* FindMaterial(char const* Name)
    {
        int Count = 0;
        DALib::MaterialLib->GetGroupMaterialCount(0, &Count);
        for (unsigned long Index = 0; Index < static_cast<unsigned long>(Count); ++Index)
        {
            IMaterial* Material = nullptr;
            DALib::MaterialLib->EnumGroupMaterial(0, Index, &Material);
            char MaterialNameBuffer[0x400];
            Material->GetName(MaterialNameBuffer, 0x3ff);
            if (_strnicmp(MaterialNameBuffer, Name, strlen(Name)) == 0)
                return Material;
            Material->Release();
        }
        return nullptr;
    }

    // 8 bytes against "Dt_name" (0x005ca818), the terminator included.
    bool IsDtName(char const* ParameterName)
    {
        return memcmp(ParameterName, "Dt_name", 8) == 0;
    }
}

FSwappedMaterial::FSwappedMaterial()
{
    // FUN_0042cc10
    MaterialName = nullptr;
    OriginalTexture = nullptr;
    OriginalTexRecord = nullptr;
    SwapTexRecord = nullptr;
    bSwapped = 0;
}

void FSwappedMaterial::Reset()
{
    // FUN_0042cc30
    MaterialName = nullptr;
    OriginalTexture = nullptr;
    OriginalTexRecord = nullptr;
    SwapTexRecord = nullptr;
    bSwapped = 0;
}

void FSwappedMaterial::Release()
{
    // FUN_0042cc50. A swap is undone first.
    if (bSwapped != 0)
    {
        SetDtName(OriginalTexture);
        if (OriginalTexRecord != nullptr)
        {
            DALib::TextureLib->ReleaseTextureRecord(OriginalTexRecord);
            OriginalTexRecord = nullptr;
        }
        if (SwapTexRecord != nullptr)
        {
            DALib::TextureLib->ReleaseTextureRecord(SwapTexRecord);
            SwapTexRecord = nullptr;
        }
        bSwapped = 0;
    }

    malloc_strfree(&MaterialName);
    malloc_strfree(&OriginalTexture);
    if (OriginalTexRecord != nullptr)
    {
        DALib::TextureLib->ReleaseTextureRecord(OriginalTexRecord);
        OriginalTexRecord = nullptr;
    }
    if (SwapTexRecord != nullptr)
    {
        DALib::TextureLib->ReleaseTextureRecord(SwapTexRecord);
        SwapTexRecord = nullptr;
    }
    MaterialName = nullptr;
    OriginalTexRecord = nullptr;
    SwapTexRecord = nullptr;
    bSwapped = 0;
    OriginalTexture = nullptr;
}

bool FSwappedMaterial::CaptureOriginal()
{
    // FUN_0042cd00.
    IMaterial* const Material = FindMaterial(MaterialName);
    if (Material == nullptr)
        return false;

    // The string (type 7) Dt_name is kept.
    unsigned long ParameterCount = 0;
    Material->GetParameterCount(&ParameterCount);
    bool bFound = false;
    for (unsigned long Parameter = 0; Parameter < ParameterCount; ++Parameter)
    {
        FShadingPropertyRecord Descriptor = {};
        Material->GetParameterDescriptor(Parameter, &Descriptor);
        if (IsDtName(Descriptor.Name))
        {
            FShadingPropertyValue Value = {};
            Material->GetParameterValue(Parameter, &Value);
            if (Value.Type == kMaterialParamTextureName)
            {
                malloc_strcpy(&OriginalTexture, reinterpret_cast<char const*>(Value.Payload0));
                FindTextureRecord(OriginalTexture, &OriginalTexRecord);
                bFound = true;
            }
            Value.ReleaseOwnedPayload();
        }
        ReleasePropertyRecord(Descriptor);
    }

    if (!bFound)
        FDUMP(kFdumpError, kNoDtName, kSwappedMaterialCpp, 0x9c, MaterialName);
    Material->Release();
    return true;
}

void FSwappedMaterial::SetDtName(char const* TextureName)
{
    // FUN_0042d1d0.
    IMaterial* const Material = FindMaterial(MaterialName);
    if (Material == nullptr)
    {
        FDUMP(kFdumpError, kNoMaterial, kSwappedMaterialCpp, 0xdf, MaterialName);
        return;
    }

    // Every string Dt_name parameter becomes a ProcessHeap copy of TextureName (none when
    // TextureName is null).
    unsigned long ParameterCount = 0;
    Material->GetParameterCount(&ParameterCount);
    bool bFound = false;
    for (unsigned long Parameter = 0; Parameter < ParameterCount; ++Parameter)
    {
        FShadingPropertyRecord Descriptor = {};
        Material->GetParameterDescriptor(Parameter, &Descriptor);
        if (IsDtName(Descriptor.Name))
        {
            FShadingPropertyValue Value = {};
            Material->GetParameterValue(Parameter, &Value);
            if (Value.Type == kMaterialParamTextureName)
            {
                if (Value.Payload0 != 0)
                {
                    HeapFree(GetProcessHeap(), 0, reinterpret_cast<void*>(Value.Payload0));
                    Value.Payload0 = 0;
                }
                if (TextureName != nullptr)
                {
                    // HeapAlloc not checked.
                    size_t const Size = strlen(TextureName) + 1;
                    char* const Copy = static_cast<char*>(HeapAlloc(GetProcessHeap(), 0, Size));
                    memcpy(Copy, TextureName, Size);
                    Value.Payload0 = reinterpret_cast<unsigned long>(Copy);
                }
                Material->SetParameterValue(Parameter, &Value);
                bFound = true;
            }
            Value.ReleaseOwnedPayload();
        }
        ReleasePropertyRecord(Descriptor);
    }

    if (!bFound)
        FDUMP(kFdumpError, kNoDtName, kSwappedMaterialCpp, 0xd4, MaterialName);

    // Slot 6 with (0, 1.0f).
    Material->ApplyVerify(nullptr, reinterpret_cast<void*>(0x3f800000u));
    Material->Release();
}

bool FSwappedMaterial::Init(char const* NewMaterialName)
{
    // FUN_0042d4c0.
    malloc_strcpy(&MaterialName, NewMaterialName);
    bool const bResult = CaptureOriginal();
    bSwapped = 0;
    return bResult;
}

void FSwappedMaterial::Swap(char const* TextureName, char const* UtfFile)
{
    // FUN_0042d4f0. Undo an earlier swap first.
    if (bSwapped != 0)
    {
        SetDtName(OriginalTexture);
        if (OriginalTexRecord != nullptr)
        {
            DALib::TextureLib->ReleaseTextureRecord(OriginalTexRecord);
            OriginalTexRecord = nullptr;
        }
        if (SwapTexRecord != nullptr)
        {
            DALib::TextureLib->ReleaseTextureRecord(SwapTexRecord);
            SwapTexRecord = nullptr;
        }
        bSwapped = 0;
    }

    // When the named child does not load, the whole "Texture library" is loaded as a group,
    // looked up, and the group garbage-collected.
    IFileSystem* FileSystem = nullptr;
    DAEngine_create_filesystem(UtfFile, &FileSystem, "UTF");
    if (DALib::TextureLib->LoadTextureLibrary(FileSystem, TextureName) != 0)
    {
        int Group;
        DALib::TextureLib->LoadTextureGroup(FileSystem, &Group);
        FindTextureRecord(TextureName, &SwapTexRecord);
        DALib::TextureLib->GarbageCollect(Group);
    }
    else
    {
        FindTextureRecord(TextureName, &SwapTexRecord);
    }
    DAEngine_release_filesystem(&FileSystem);

    SetDtName(TextureName);
    bSwapped = 1;
}
