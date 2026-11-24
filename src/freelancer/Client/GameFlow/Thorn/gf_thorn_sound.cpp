// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "GameFlow/Thorn/gf_thorn_sound.h"
#include "msvcrt_compat.h" // MsvcrtAtoi: retail MSVCRT.DLL wraps mod 2^32, the UCRT saturates

#include "common_id.h"   // CreateID
#include "common_misc.h" // malloc_strcpy / malloc_strfree
#include "dacom.h"
#include "GameFlow/gf_character_object.h" // FCharacterObject::ResolveBgcsContextToken (FUN_00445720)
#include "GameFlow/gf_object_ship.h"
#include "GameFlow/Thorn/gf_script_object.h"
#include "GameFlow/Thorn/gf_thorn_light.h" // ThornReference* forwarders shared by every reference class
#include "Common/Audio/menu_sound_arch.h"
#include "Common/Audio/sound_archetype.h"
#include "Common/Audio/sound_config.h"
#include "soundmanager.h"
#include "Common/Audio/voice_profiles.h"


#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace
{
    char const* const GScriptSoundFile = "E:\\FL\\Scratch\\Source\\Client\\Gf\\ScriptSound.cpp";

    // The play key FUN_00428680 reads (menu_sound_arch.h): +0 start offset, +4 flags byte.
    // Retail builds it on the stack and leaves the flags dword's upper bytes uninitialised; only
    // bits 0..2 are read.
    struct FSoundPlayKey
    {
        unsigned Offset;   // +0x00
        unsigned Flags;    // +0x04 bit 0 loop, bit 1 spatial, bit 2 streamer
        unsigned Pad[4];   // +0x08..+0x14 zeroed
    };

    void CopyDword(void* Dst, void const* Src)
    {
        memcpy(Dst, Src, 4);
    }
}

// ---------------------------------------------------------------------------------------
// Thorn_Sound
// ---------------------------------------------------------------------------------------

// FUN_00456d80.
FThornSound::FThornSound(Csys const& ScriptFrame, FGfScript* InOwner, FThornEntitySnap const* Desc)
    : FThornComponent(ScriptFrame, InOwner, Desc),
      Template(nullptr),
      Speaker(nullptr),
      bVoice(0),
      bSpatial(0),
      bAmbient(0),
      bMusic(0),
      bHoldScript(0),
      Arch(nullptr),
      Handle(nullptr),
      bUseScriptFade(0),
      Attenuation(0.0f),
      Pan(0.0f),
      MinDistance(1e9f),
      MaxDistance(1e9f),
      ConeInsideAngle(0x168),
      ConeOutsideAngle(0x168),
      ConeOutsideAttenuation(0.0f),
      ReverbMix(0.0f)
{
    Pad79[0] = 0;
    Pad79[1] = 0;
    Pad79[2] = 0;
    Pad85[0] = 0;
    Pad85[1] = 0;
    Pad85[2] = 0;
    Local.Pos.x = 0.0f;
    Local.Pos.y = 0.0f;
    Local.Pos.z = 0.0f;
    MatrixSetIdentity(Local.Orient);
}

// FUN_00456ee0. A background sound only releases the source it borrowed.
FThornSound::~FThornSound()
{
    malloc_strfree(&Template);
    malloc_strfree(&Speaker);
    if (bMusic == 0 && bAmbient == 0)
    {
        Owner->RemoveSoundEvent(Handle);
        if (Handle != nullptr)
        {
            Handle->Stop();
            Handle->Release();
            Handle = nullptr;
        }
    }
    if ((bMusic != 0 || bAmbient != 0) && Handle != nullptr)
    {
        Handle->Release();
        Handle = nullptr;
    }
}

// FUN_00456ff0. BindClientBackgroundAmbient / Music are FUN_00428bc0 / FUN_00428ba0,
// LookupClientSoundArch FUN_0042ae40. The archetype is not Acquired here.
void FThornSound::BindTemplate(char const* InTemplate, unsigned char bInVoice)
{
    malloc_strcpy(&Template, InTemplate);
    bVoice = bInVoice;
    if (_stricmp(InTemplate, "BackgroundAmbient") == 0)
    {
        bAmbient = 1;
        BindClientBackgroundAmbient(reinterpret_cast<void**>(&Handle));
        return;
    }
    if (_stricmp(InTemplate, "BackgroundMusic") == 0)
    {
        bMusic = 1;
        BindClientBackgroundMusic(reinterpret_cast<void**>(&Handle));
        return;
    }
    if (bVoice == 0 && Template != nullptr && Template[0] != '\0')
    {
        ID_String TemplateId;
        TemplateId.Id = CreateID(Template);
        Arch = LookupClientSoundArch(&TemplateId);
    }
}

// FUN_004572b0.
void FThornSound::SetSpeaker(char const* InSpeaker)
{
    if (InSpeaker != nullptr)
        malloc_strcpy(&Speaker, InSpeaker);
}

// FUN_004572d0.
void FThornSound::SetSpatial(unsigned char bInSpatial)
{
    bSpatial = bInSpatial;
}

// FUN_00457380. ISoundSource slot 29 (+0x74).
bool FThornSound::IsHoldingScript() const
{
    if (Handle == nullptr || bHoldScript == 0)
        return false;
    return Handle->IsFinished() != 0;
}

// FUN_004570a0. The six "$BGCS_*" context tokens go through the speaking character
// (FUN_00445720), and a "$BGCS_Trent_<name>_<n>" template gets a random index 0..n-1 written
// over its last underscore and loses the leading "$B". A missing profile or message is an FDUMP
// and +0x7c keeps its value.
//
// The receiver of FUN_00445720 is Actor as it came back from the spawn table's FindActor (only
// Thorn_Deformable answers with one: its CharacterObject); with no actor retail calls it on a
// null receiver, and so does the port -- no guard.
void FThornSound::ResolveVoice(void* VoiceId, void* Actor)
{
    if (bVoice != 1 || Template == nullptr || Template[0] == '\0')
        return;

    FCharacterObject const* const Talker = static_cast<FCharacterObject const*>(Actor);
    char const* MessageName = Template;
    if (_stricmp(Template, "$BGCS_Base_Name") == 0 || _stricmp(Template, "$BGCS_Base_Run_By") == 0 ||
        _stricmp(Template, "$BGCS_My_Faction_ML") == 0 ||
        _stricmp(Template, "$BGCS_My_Faction_FM") == 0)
    {
        MessageName = Talker->ResolveBgcsContextToken(Template);
        bHoldScript = 0;
    }
    if (_stricmp(Template, "$BGCS_Faction_Rebuff_ML") == 0 ||
        _stricmp(Template, "$BGCS_Faction_Rebuff_FM") == 0)
    {
        MessageName = Talker->ResolveBgcsContextToken(Template);
        bHoldScript = 1;
    }

    // The local is 0x100 bytes; the copy is unbounded, as retail's.
    char Scratch[0x100];
    if (_strnicmp(Template, "$BGCS_Trent_", strlen("$BGCS_Trent_")) == 0)
    {
        strcpy(Scratch, Template);
        char* const Underscore = strrchr(Scratch, '_');
        int const Count = MsvcrtAtoi(Underscore + 1);
        int const Product = rand() * Count;
        int const Pick = (Product + ((Product >> 31) & 0x7fff)) >> 15;
        _ltoa(Pick, Underscore, 10);
        MessageName = Scratch + 2;
        bHoldScript = 0;
    }

    unsigned int const MessageId = CreateID(MessageName);
    unsigned int const Voice = static_cast<unsigned int>(reinterpret_cast<uintptr_t>(VoiceId));
    FVoiceProfile* const Profile = LookupClientVoice(Voice);
    if (Profile == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: Voice profile %u does not exist\n", GScriptSoundFile,
              0xe8, Voice);
        return;
    }
    FVoiceMsg* const Message = Profile->FindMessage(MessageId);
    if (Message != nullptr)
    {
        Arch = Message;
        return;
    }
    FDUMP(0x100001, "%s(%d) : *** ERROR: Voice profile %s (%x) does not support message %s",
          GScriptSoundFile, 0xe3, Profile->GetName(), Voice, MessageName);
}

// LAB_00457400.
GENRESULT DACOM_API FThornSound::SetTransform(float const* Transform12)
{
    Local.Pos.x = Transform12[9];
    Local.Pos.y = Transform12[10];
    Local.Pos.z = Transform12[11];
    memcpy(&Local.Orient, Transform12, sizeof(Local.Orient));
    RefreshSound();
    return GR_OK;
}

// LAB_00457490.
GENRESULT DACOM_API FThornSound::GetTransform(float* OutTransform12)
{
    OutTransform12[9] = Local.Pos.x;
    OutTransform12[10] = Local.Pos.y;
    OutTransform12[11] = Local.Pos.z;
    memcpy(OutTransform12, &Local.Orient, sizeof(Local.Orient));
    return GR_OK;
}

// LAB_004574d0. StopSound first, through the facet's vtable; nothing more while the owning
// script is paused. A streamer is started by the script's event list (FUN_0044f610), anything
// else through FUN_00428680. RefreshSound runs with or without an archetype.
GENRESULT DACOM_API FThornSound::StartSound(void* Desc, int TimeMs)
{
    static_cast<ISoundControl*>(this)->StopSound(Desc);
    if (Owner->bPaused != 0)
        return GR_OK;

    FThornSoundEventDesc const* const Event = static_cast<FThornSoundEventDesc const*>(Desc);
    int const Offset = Event->SoundStartTime - Event->Common.StartMs + TimeMs;
    unsigned char const bLoop = (Event->Common.Flags & 8) != 0 ? 1 : 0;

    if (Arch != nullptr)
    {
        Archetype::ISound* const SoundArch = static_cast<Archetype::ISound*>(Arch);
        if (SoundArch->IsStreamer() != 0)
        {
            Handle = static_cast<ISoundSource*>(
                Owner->PlayStreamedSoundEvent(Event->Common.UniqueId, Offset));
        }
        else
        {
            FSoundPlayKey Key;
            memset(&Key, 0, sizeof(Key));
            unsigned char Flags = 0;
            unsigned char const bStreamer = SoundArch->IsStreamer();
            Flags = static_cast<unsigned char>(Flags ^ (((bStreamer << 2) ^ Flags) & 4));
            Flags = static_cast<unsigned char>(Flags ^ (((bSpatial << 1) ^ Flags) & 2));
            Flags = static_cast<unsigned char>(Flags ^ ((bLoop ^ Flags) & 1));
            Key.Flags = Flags;
            Key.Offset = static_cast<unsigned>(Offset);
            PlayClientSoundArch(Arch, reinterpret_cast<unsigned*>(&Key),
                                reinterpret_cast<void**>(&Handle), 0);
        }
    }
    RefreshSound();
    return GR_OK;
}

// FUN_004575e0. A background sound is left alone.
GENRESULT DACOM_API FThornSound::StopSound(void*)
{
    if (bMusic == 0 && bAmbient == 0)
    {
        Owner->RemoveSoundEvent(Handle);
        if (Handle != nullptr)
        {
            Handle->Stop();
            Handle->Release();
            Handle = nullptr;
        }
    }
    return GR_OK;
}

// LAB_004576c0. The ambience also snaps the Gf fade to it (FUN_004569b0 on DAT_0066d2d0).
GENRESULT DACOM_API FThornSound::SetAttenuation(float Value)
{
    Attenuation = Value;
    if (bAmbient != 0)
        SnapClientAudioFade(Value);
    RefreshSound();
    return GR_OK;
}

// LAB_00457720.
GENRESULT DACOM_API FThornSound::SetPan(float Value)
{
    Pan = Value;
    RefreshSound();
    return GR_OK;
}

// LAB_00457700.
GENRESULT DACOM_API FThornSound::GetAttenuation(float* OutAttenuation)
{
    *OutAttenuation = Attenuation;
    return GR_OK;
}

// LAB_00457740.
GENRESULT DACOM_API FThornSound::GetPan(float* OutPan)
{
    *OutPan = Pan;
    return GR_OK;
}

// LAB_00457760. Mask bit i copies Props[i] into the i-th dword from +0x88.
GENRESULT DACOM_API FThornSound::SetAudioProps(float const* Props8, int Mask)
{
    if ((Mask & 0x1) != 0)
    {
        float const Value = Props8[0];
        Attenuation = Value;
        if (bAmbient != 0)
            SnapClientAudioFade(Value);
    }
    if ((Mask & 0x2) != 0)
        CopyDword(&Pan, &Props8[1]);
    if ((Mask & 0x4) != 0)
        CopyDword(&MinDistance, &Props8[2]);
    if ((Mask & 0x8) != 0)
        CopyDword(&MaxDistance, &Props8[3]);
    if ((Mask & 0x10) != 0)
        CopyDword(&ConeInsideAngle, &Props8[4]);
    if ((Mask & 0x20) != 0)
        CopyDword(&ConeOutsideAngle, &Props8[5]);
    if ((Mask & 0x40) != 0)
        CopyDword(&ConeOutsideAttenuation, &Props8[6]);
    if ((Mask & 0x80) != 0)
        CopyDword(&ReverbMix, &Props8[7]);
    RefreshSound();
    return GR_OK;
}

// LAB_00457810.
GENRESULT DACOM_API FThornSound::GetAudioProps(float* Props8, int Mask)
{
    if ((Mask & 0x1) != 0)
        CopyDword(&Props8[0], &Attenuation);
    if ((Mask & 0x2) != 0)
        CopyDword(&Props8[1], &Pan);
    if ((Mask & 0x4) != 0)
        CopyDword(&Props8[2], &MinDistance);
    if ((Mask & 0x8) != 0)
        CopyDword(&Props8[3], &MaxDistance);
    if ((Mask & 0x10) != 0)
        CopyDword(&Props8[4], &ConeInsideAngle);
    if ((Mask & 0x20) != 0)
        CopyDword(&Props8[5], &ConeOutsideAngle);
    if ((Mask & 0x40) != 0)
        CopyDword(&Props8[6], &ConeOutsideAttenuation);
    if ((Mask & 0x80) != 0)
        CopyDword(&Props8[7], &ReverbMix);
    return GR_OK;
}

// LAB_004573b0.
GENRESULT DACOM_API FThornSound::SetPosition(float* Position)
{
    Local.Pos.x = Position[0];
    Local.Pos.y = Position[1];
    Local.Pos.z = Position[2];
    RefreshSound();
    return GR_OK;
}

// LAB_004573e0. No RefreshSound here.
GENRESULT DACOM_API FThornSound::SetOrientation(float* Orient9)
{
    memcpy(&Local.Orient, Orient9, sizeof(Local.Orient));
    return GR_OK;
}

// LAB_00457440.
GENRESULT DACOM_API FThornSound::GetPosition(float* OutPosition)
{
    OutPosition[0] = Local.Pos.x;
    OutPosition[1] = Local.Pos.y;
    OutPosition[2] = Local.Pos.z;
    return GR_OK;
}

// LAB_00457470.
GENRESULT DACOM_API FThornSound::GetOrientation(float* OutOrient9)
{
    memcpy(OutOrient9, &Local.Orient, sizeof(Local.Orient));
    return GR_OK;
}

// LAB_004572e0. FUN_004570a0 runs even on a miss, with the two outs still null.
void FThornSound::SetSpawnTable(FThornComponent** Table, int Count)
{
    if (bVoice != 1)
        return;
    void* VoiceId = nullptr;
    void* Actor = nullptr;
    bool bFound = false;
    int Index = 0;
    for (Index = 0; Index < Count; ++Index)
    {
        FThornComponent* const Entity = Table[Index];
        if (Entity != nullptr && Entity->FindActor(Speaker, &VoiceId, &Actor))
        {
            bFound = true;
            break;
        }
    }
    if (!bFound)
    {
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: Speaker name %s for sound %s in script %s does not match any "
              "entity names, sound cannot play",
              GScriptSoundFile, 0x123, Speaker, Template, Owner->ScriptFile);
    }
    ResolveVoice(VoiceId, Actor);
}

// FUN_00450870.
void* FThornSound::GetEngineInstance()
{
    return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
}

// LAB_00457630. GetClientAudioFade is FUN_0042adf0 (DAT_0066d2d0 +0x10); 100.0 is the double
// DAT_005ca320.
void FThornSound::RefreshSound()
{
    float Fade = GetClientAudioFade();
    if (bMusic != 0 || bAmbient != 0 || bUseScriptFade == 0)
        Fade = 0.0f;
    if (Handle == nullptr)
        return;
    Handle->SetAttenuation(Fade + Attenuation);
    Handle->SetPan(static_cast<float>(static_cast<double>(Pan) * 100.0));
    Handle->SetPosition(&Local.Pos);
}

// LAB_00456ea0.
bool FThornSound::IsBackgroundSound()
{
    return bMusic != 0 || bAmbient != 0;
}

// ---------------------------------------------------------------------------------------
// Thorn_PlayerEngineSound
// ---------------------------------------------------------------------------------------

// FUN_004578a0.
FThornPlayerEngineSound::FThornPlayerEngineSound(Csys const& ScriptFrame, FGfScript* InOwner,
                                                 FThornEntitySnap const* Desc)
    : FThornComponent(ScriptFrame, InOwner, Desc),
      Attenuation(0.0f)
{
}

// LAB_004579a0. StartEngineSound is FUN_00447f90.
GENRESULT DACOM_API FThornPlayerEngineSound::StartSound(void*, int)
{
    FObjectShip* const Ship = GfObjectShip();
    if (Ship != nullptr)
        Ship->StartEngineSound();
    return GR_OK;
}

// LAB_004579c0. StopEngineSound is FUN_004481b0.
GENRESULT DACOM_API FThornPlayerEngineSound::StopSound(void*)
{
    FObjectShip* const Ship = GfObjectShip();
    if (Ship != nullptr)
        Ship->StopEngineSound();
    return GR_OK;
}

// LAB_004579e0. SetEngineSoundAttenuation is FUN_00448210.
GENRESULT DACOM_API FThornPlayerEngineSound::SetAttenuation(float Value)
{
    Attenuation = Value;
    FObjectShip* const Ship = GfObjectShip();
    if (Ship != nullptr)
        Ship->SetEngineSoundAttenuation(Value);
    return GR_OK;
}

// LAB_00457a10.
GENRESULT DACOM_API FThornPlayerEngineSound::GetAttenuation(float* OutAttenuation)
{
    *OutAttenuation = Attenuation;
    return GR_OK;
}

// LAB_00457a30. Mask bit 0 only.
GENRESULT DACOM_API FThornPlayerEngineSound::SetAudioProps(float const* Props8, int Mask)
{
    if ((Mask & 1) == 0)
        return GR_OK;
    CopyDword(&Attenuation, &Props8[0]);
    FObjectShip* const Ship = GfObjectShip();
    if (Ship != nullptr)
        Ship->SetEngineSoundAttenuation(Attenuation);
    return GR_OK;
}

// LAB_00457a60. Mask bit 0 only.
GENRESULT DACOM_API FThornPlayerEngineSound::GetAudioProps(float* Props8, int Mask)
{
    if ((Mask & 1) != 0)
        CopyDword(&Props8[0], &Attenuation);
    return GR_OK;
}

// LAB_00457980.
void FThornPlayerEngineSound::Stop()
{
    FObjectShip* const Ship = GfObjectShip();
    if (Ship != nullptr)
        Ship->StopEngineSound();
}

// FUN_00450870.
void* FThornPlayerEngineSound::GetEngineInstance()
{
    return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
}

// ---------------------------------------------------------------------------------------
// Thorn_SoundReference
// ---------------------------------------------------------------------------------------

// FUN_00457a80.
FThornSoundReference::FThornSoundReference(Csys const& ScriptFrame, FGfScript* InOwner,
                                           FThornEntitySnap const* Desc)
    : FThornComponent(ScriptFrame, InOwner, Desc),
      Target(nullptr)
{
    Target = G_GfScriptManager.FindCreatedEntity(kThornEntitySound, Desc->Name);
    if (Target == nullptr)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: invalid sound reference to name %s",
              GScriptSoundFile, 0x2f2, Desc->Name);
    }
}

// FUN_00457b70.
FThornSoundReference::~FThornSoundReference()
{
    Target = nullptr;
}

// LAB_00457c00.
GENRESULT DACOM_API FThornSoundReference::SetTransform(float const* Transform12)
{
    return ThornReferenceSetTransform(Target, Transform12);
}

// LAB_00457c20.
GENRESULT DACOM_API FThornSoundReference::GetTransform(float* OutTransform12)
{
    return ThornReferenceGetTransform(Target, OutTransform12);
}

// LAB_00457c40 -- a reference never starts its target.
GENRESULT DACOM_API FThornSoundReference::StartSound(void*, int)
{
    return GR_OK;
}

// LAB_00451370.
GENRESULT DACOM_API FThornSoundReference::StopSound(void*)
{
    return GR_OK;
}

// LAB_00457c50 -- the target's sound facet slot 5, or -1.
GENRESULT DACOM_API FThornSoundReference::SetAttenuation(float Value)
{
    if (Target == nullptr)
        return static_cast<GENRESULT>(-1);
    return static_cast<ISoundControl*>(Target)->SetAttenuation(Value);
}

// LAB_00457ca0 -- slot 6, or -1.
GENRESULT DACOM_API FThornSoundReference::SetPan(float Value)
{
    if (Target == nullptr)
        return static_cast<GENRESULT>(-1);
    return static_cast<ISoundControl*>(Target)->SetPan(Value);
}

// LAB_00457c80 -- slot 7, or -1.
GENRESULT DACOM_API FThornSoundReference::GetAttenuation(float* OutAttenuation)
{
    if (Target == nullptr)
        return static_cast<GENRESULT>(-1);
    return static_cast<ISoundControl*>(Target)->GetAttenuation(OutAttenuation);
}

// LAB_00457cd0 -- slot 8, or -1.
GENRESULT DACOM_API FThornSoundReference::GetPan(float* OutPan)
{
    if (Target == nullptr)
        return static_cast<GENRESULT>(-1);
    return static_cast<ISoundControl*>(Target)->GetPan(OutPan);
}

// LAB_00457cf0 -- slot 9, or -1.
GENRESULT DACOM_API FThornSoundReference::SetAudioProps(float const* Props8, int Mask)
{
    if (Target == nullptr)
        return static_cast<GENRESULT>(-1);
    return static_cast<ISoundControl*>(Target)->SetAudioProps(Props8, Mask);
}

// LAB_00457d10 -- slot 10, or -1.
GENRESULT DACOM_API FThornSoundReference::GetAudioProps(float* Props8, int Mask)
{
    if (Target == nullptr)
        return static_cast<GENRESULT>(-1);
    return static_cast<ISoundControl*>(Target)->GetAudioProps(Props8, Mask);
}

// LAB_00457bc0.
GENRESULT DACOM_API FThornSoundReference::SetPosition(float* Position)
{
    return ThornReferenceSetPosition(Target, Position);
}

// LAB_00457be0.
GENRESULT DACOM_API FThornSoundReference::SetOrientation(float* Orient9)
{
    return ThornReferenceSetOrientation(Target, Orient9);
}

// LAB_00453560.
GENRESULT DACOM_API FThornSoundReference::GetPosition(float* OutPosition)
{
    return ThornReferenceGetPosition(Target, OutPosition);
}

// LAB_00453580.
GENRESULT DACOM_API FThornSoundReference::GetOrientation(float* OutOrient9)
{
    return ThornReferenceGetOrientation(Target, OutOrient9);
}

// LAB_00450cd0.
void FThornSoundReference::Stop()
{
    Target = nullptr;
}

// LAB_00453550.
void* FThornSoundReference::GetEngineInstance()
{
    return ThornReferenceGetEngineInstance(Target);
}
