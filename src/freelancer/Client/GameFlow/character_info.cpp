// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "GameFlow/character_info.h"

#include "DALib.h"              // DALib::Anim
#include "common_id.h"          // CreateID
#include "common_reputation.h"  // CostumeDescriptions, GetCostumeDescriptions
#include "common_misc.h"        // DAEngine_create_filesystem / DAEngine_release_filesystem
#include "dacom.h"              // FDUMP
#include "engine/engbase.h"     // IAnimation2
#include "readfile.h"           // IFileSystem

#include <cstdint>
#include <string.h>

namespace
{
    char const kCharacterInfoCpp[] = "E:\\FL\\Scratch\\Source\\Client\\gf\\CharacterInfo.cpp"; // 0x005caddc
    char const kFailedToOpen[] = "%s(%d) : *** WARNING: Failed to open %s\n";                 // 0x005cadb0

    // The "no script" value of +0x2c.
    void* NoAnimScript()
    {
        return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    }

    // FUN_00435100's stack record for the enumeration callback: the database at +0, the
    // remembered file name at +4.
    struct FScriptEnumContext
    {
        FCharacterInfoDb* Db;
        std::string const* File;
    };

    // LAB_00434f20 (cdecl): one Animation\Script directory -> AddScript(Name, Ctx->File).
    void __cdecl AddEnumeratedScript(char const* Name, void* Context)
    {
        FScriptEnumContext* const Ctx = static_cast<FScriptEnumContext*>(Context);
        Ctx->Db->AddScript(Name, Ctx->File);
    }

    // DAT_00668660.
    FCharacterInfoDb G_CharacterInfoDb;
}

FCharacterInfoDb::FCharacterInfoDb()
    : AnimScript(NoAnimScript())
{
    // FUN_004349f0.
}

FCharacterInfoDb::~FCharacterInfoDb()
{
    // FUN_00434b90. No test of DALib::Anim, as retail.
    ScriptFiles.clear();
    Scripts.clear();
    Queued.clear();
    if (AnimScript != NoAnimScript())
    {
        DALib::Anim->CloseScript(AnimScript);
        AnimScript = NoAnimScript();
    }
}

void* FCharacterInfoDb::AcquireScript(unsigned Id)
{
    // FUN_00434d10. The lookup is FUN_00432240.
    std::map<unsigned, FCharacterScript>::iterator const It = Scripts.find(Id);
    if (It == Scripts.end())
    {
        return NoAnimScript();
    }
    FCharacterScript& Script = It->second;
    if (Script.RefCount == 0)
    {
        char const* const Path = Script.File->c_str();
        IFileSystem* File = nullptr;
        DAEngine_create_filesystem(Path, &File, nullptr);
        if (File != nullptr)
        {
            char const* Names[1];
            Names[0] = Script.Name.c_str();
            DALib::Anim->AddScriptNames(AnimScript, File, Names, 1); // slot 30
            DAEngine_release_filesystem(&File);
        }
        else
        {
            FDUMP(kFdumpWarning, kFailedToOpen, kCharacterInfoCpp, 0x92, Path);
        }
    }
    // Counted on both paths.
    ++Script.RefCount;
    return AnimScript;
}

void FCharacterInfoDb::ReleaseScript(unsigned Id)
{
    // FUN_00434e20.
    std::map<unsigned, FCharacterScript>::iterator const It = Scripts.find(Id);
    if (It == Scripts.end())
    {
        return;
    }
    FCharacterScript& Script = It->second;
    // Only the reference that reaches zero unloads.
    --Script.RefCount;
    if (Script.RefCount != 0)
    {
        return;
    }
    char const* Names[1];
    Names[0] = Script.Name.c_str();
    DALib::Anim->RemoveScriptNames(AnimScript, Names, 1); // slot 31
}

void* FCharacterInfoDb::AcquireScriptByName(char const* Name)
{
    // FUN_00434e90.
    return AcquireScript(CreateID(Name));
}

void FCharacterInfoDb::ReleaseScriptByName(char const* Name)
{
    // FUN_00434eb0 (FUN_00434e20 inlined).
    ReleaseScript(CreateID(Name));
}

void FCharacterInfoDb::AddScript(char const* Name, std::string const* File)
{
    // FUN_00434f40. The unique insert (FUN_004360e0) result is not looked at.
    unsigned const Id = CreateID(Name);
    FCharacterScript Script;
    Script.Name = Name;
    Script.Id = Id;
    Script.RefCount = 0;
    Script.File = File;
    Scripts.insert(std::map<unsigned, FCharacterScript>::value_type(Id, Script));
}

void FCharacterInfoDb::Load(CostumeDescriptions const* Descs)
{
    // FUN_00435100. No test of DALib::Anim and no release of a script already there.
    AnimScript = DALib::Anim->CreateEmptyScript();

    // Every "anim =" file of CostumeDescriptions +0x00.
    for (std::list<std::string>::const_iterator FileIt = Descs->AnimationNames.begin();
         FileIt != Descs->AnimationNames.end(); ++FileIt)
    {
        char const* const FileName = FileIt->c_str();

        // A file already in ScriptFiles is skipped.
        bool bKnown = false;
        for (std::list<std::string>::const_iterator KnownIt = ScriptFiles.begin();
             KnownIt != ScriptFiles.end(); ++KnownIt)
        {
            if (strcmp(FileName, KnownIt->c_str()) == 0)
            {
                bKnown = true;
                break;
            }
        }
        if (bKnown)
        {
            continue;
        }

        // The scripts keep a pointer to this element (list nodes never move).
        ScriptFiles.push_back(std::string(FileName));
        std::string const* const Stored = &ScriptFiles.back();

        IFileSystem* File = nullptr;
        DAEngine_create_filesystem(FileName, &File, nullptr);
        if (File != nullptr)
        {
            // IAnimation2 slot 32 with LAB_00434f20.
            FScriptEnumContext Context;
            Context.Db = this;
            Context.File = Stored;
            DALib::Anim->EnumFsScripts(&AddEnumeratedScript, File,
                                       &Context);
            DAEngine_release_filesystem(&File);
        }
        else
        {
            FDUMP(kFdumpWarning, kFailedToOpen, kCharacterInfoCpp, 0x149, FileName);
        }
    }
}

void FCharacterInfoDb::QueueScript(char const* Name)
{
    // FUN_00435320.
    std::map<unsigned, FCharacterScript>::iterator const It = Scripts.find(CreateID(Name));
    if (It == Scripts.end())
    {
        return;
    }
    Queued.push_back(&It->second);
}

void FCharacterInfoDb::FlushQueuedScripts()
{
    // FUN_004353a0.
    for (;;)
    {
        if (Queued.empty())
        {
            return;
        }

        // One pass. The first entry picks the file; entries of another file stay. Up to 0x100
        // matching entries leave the queue with a reference each, and the names of the ones
        // nobody held yet are collected.
        std::string const* File = nullptr;
        char const* Names[0x100];
        int Count = 0;
        std::list<FCharacterScript*>::iterator It = Queued.begin();
        while (It != Queued.end())
        {
            FCharacterScript* const Script = *It;
            if (File == nullptr)
            {
                File = Script->File;
            }
            if (File != Script->File)
            {
                ++It;
                continue;
            }
            if (Count >= 0x100)
            {
                break;
            }
            if (Script->RefCount == 0)
            {
                Names[Count] = Script->Name.c_str();
                ++Count;
            }
            ++Script->RefCount;
            It = Queued.erase(It);
        }

        // Nothing new to load -> the next round.
        if (Count <= 0)
        {
            continue;
        }

        // One IAnimation2 slot 30 call for the whole round.
        char const* const Path = File->c_str();
        IFileSystem* Fs = nullptr;
        DAEngine_create_filesystem(Path, &Fs, nullptr);
        if (Fs != nullptr)
        {
            DALib::Anim->AddScriptNames(AnimScript, Fs, Names, Count);
            DAEngine_release_filesystem(&Fs);
        }
        else
        {
            FDUMP(kFdumpWarning, kFailedToOpen, kCharacterInfoCpp, 0x1c2, Path);
        }
    }
}

FCharacterInfoDb* GetCharacterInfoDb()
{
    // FUN_004356c0.
    return &G_CharacterInfoDb;
}

void LoadCharacterInfo()
{
    // FUN_004354e0.
    G_CharacterInfoDb.Load(GetCostumeDescriptions());
}

void ShutdownCharacterInfo()
{
    // FUN_00435500. No test of DALib::Anim.
    G_CharacterInfoDb.ScriptFiles.clear();
    G_CharacterInfoDb.Scripts.clear();
    G_CharacterInfoDb.Queued.clear();
    if (G_CharacterInfoDb.AnimScript != NoAnimScript())
    {
        DALib::Anim->CloseScript(G_CharacterInfoDb.AnimScript);
        G_CharacterInfoDb.AnimScript = NoAnimScript();
    }
}

void PreloadCharacterScripts()
{
    // FUN_004355e0.
    CostumeDescriptions const* const Descs = GetCostumeDescriptions();
    for (std::list<std::string>::const_iterator It = Descs->PreloadNames.begin();
         It != Descs->PreloadNames.end(); ++It)
    {
        G_CharacterInfoDb.AcquireScript(CreateID(It->c_str()));
    }
}

void ResetCharacterScripts()
{
    // FUN_00435630.
    for (std::map<unsigned, FCharacterScript>::iterator It = G_CharacterInfoDb.Scripts.begin();
         It != G_CharacterInfoDb.Scripts.end(); ++It)
    {
        It->second.RefCount = 0;
    }
    if (DALib::Anim == nullptr)
    {
        return;
    }
    if (G_CharacterInfoDb.AnimScript != NoAnimScript())
    {
        DALib::Anim->CloseScript(G_CharacterInfoDb.AnimScript);
        G_CharacterInfoDb.AnimScript = NoAnimScript();
    }
    G_CharacterInfoDb.AnimScript = DALib::Anim->CreateEmptyScript();
}
