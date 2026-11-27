// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Thorn_Sound, Thorn_PlayerEngineSound and Thorn_SoundReference (RTTI .?AUThorn_Sound@@ /
// .?AUThorn_PlayerEngineSound@@ / .?AUThorn_SoundReference@@, E:\FL\Scratch\Source\Client\Gf\
// ScriptSound.cpp) -- the objects SOUND entities are bound to.
//
// A Thorn_Sound (operator new(0xd8) in FUN_0044e030, ctor FUN_00456d80, identity vtable
// 0x005ce264) plays one sound archetype, a voice line of a speaking character
// ("VoiceProfile..." templates) or the room's background music / ambience
// ("BackgroundMusic" / "BackgroundAmbient"). Streamed archetypes are started by the owning
// script's event list (FGfScript::PlayStreamedSoundEvent); the others straight through the
// client sound heap (FUN_00428680). Identity slot 21 re-applies attenuation, pan and position to
// the playing handle; the Gf fade (DAT_0066d2d0 +0x10, sound_config.h) is added only for a
// non-background sound whose script asked for it (+0x84).
//
// A Thorn_PlayerEngineSound (operator new(0x70) in FUN_0044e180, ctor FUN_004578a0) drives the
// Gf ObjectShip's engine sound; a Thorn_SoundReference (operator new(0x70) in FUN_0044e1f0,
// ctor FUN_00457a80) forwards to a Thorn_Sound another script created.

#include "engine/engine_instance.h" // InstanceIndex / ArchetypeIndex
#include "common_csys.h"
#include "GameFlow/Thorn/gf_thorn_component.h"

struct FCharacterObject;
struct ISoundSource;
namespace Archetype
{
    struct ISound;
}

// The Mask bits of ISoundControl::SetAudioProps / GetAudioProps (FThornSound LAB_00457760 /
// LAB_00457810): bit i selects Props8[i].
enum EThornAudioProp
{
    kThornAudioAttenuation = 0x01,
    kThornAudioPan = 0x02,
    kThornAudioMinDistance = 0x04,
    kThornAudioMaxDistance = 0x08,
    kThornAudioConeInsideAngle = 0x10,
    kThornAudioConeOutsideAngle = 0x20,
    kThornAudioConeOutsideAttenuation = 0x40,
    kThornAudioReverbMix = 0x80
};

struct FThornSound : FThornComponent
{
    // FUN_00456d80 (thiscall, RET 0xc).
    FThornSound(Csys const& ScriptFrame, FGfScript* InOwner, FThornEntitySnap const* Desc);
    // FUN_00456ee0 (FUN_00456ec0 is the scalar deleting destructor).
    ~FThornSound() override;

    // FUN_00456ff0 (thiscall, RET 8) -- the template: background music / ambience take the
    // global source, anything else (not a voice line) its sound archetype. Caller: the spawn
    // FUN_0044e030.
    void BindTemplate(char const* InTemplate, unsigned char bInVoice);
    // FUN_004572b0 (thiscall, RET 4) -- malloc_strcpy of the speaker unless it is null.
    void SetSpeaker(char const* InSpeaker);
    // FUN_004572d0 (thiscall, RET 4) -- +0x75.
    void SetSpatial(unsigned char bInSpatial);
    // FUN_00457380 (thiscall) -- true for a playing voice line with bHoldScript set whose
    // handle's slot 29 answers true. Caller: FGfScript::GetSoundTailMs (FUN_0044f500).
    bool IsHoldingScript() const;

    // ICompoundControl (0x005ce290 / 0x005ce294)
    GENRESULT DACOM_API SetTransform(const Transform* Transform12) override;   // 11 LAB_00457400
    GENRESULT DACOM_API GetTransform(Transform* OutTransform12) override;      // 12 LAB_00457490

    // ISoundControl (0x005ce234)
    GENRESULT DACOM_API StartSound(void* Desc, int TimeMs) override;        // 3  LAB_004574d0
    GENRESULT DACOM_API StopSound(void* Desc) override;                     // 4  FUN_004575e0
    GENRESULT DACOM_API SetAttenuation(float Attenuation) override;         // 5  LAB_004576c0
    GENRESULT DACOM_API SetPan(float Pan) override;                         // 6  LAB_00457720
    GENRESULT DACOM_API GetAttenuation(float* OutAttenuation) override;     // 7  LAB_00457700
    GENRESULT DACOM_API GetPan(float* OutPan) override;                     // 8  LAB_00457740
    GENRESULT DACOM_API SetAudioProps(float const* Props8, int Mask) override; // 9  LAB_00457760
    GENRESULT DACOM_API GetAudioProps(float* Props8, int Mask) override;    // 10 LAB_00457810

    // IGeoTransformControl (0x005ce1f8)
    GENRESULT DACOM_API SetPosition(const Vector* Position) override;             // 3  LAB_004573b0
    GENRESULT DACOM_API SetOrientation(const Matrix* Orient9) override;           // 4  LAB_004573e0
    GENRESULT DACOM_API GetPosition(Vector* OutPosition) override;          // 5  LAB_00457440
    GENRESULT DACOM_API GetOrientation(Matrix* OutOrient9) override;        // 6  LAB_00457470

    // identity tail
    void SetSpawnTable(FThornComponent** Table, int Count) override;       // 15 LAB_004572e0
    InstanceIndex GetEngineInstance() override;                                    // 17 FUN_00450870
    void RefreshSound() override;                                          // 21 LAB_00457630
    bool IsBackgroundSound() override;                                     // 22 LAB_00456ea0

    char* Template;                // +0x6c malloc'd template (the "VoiceProfile" prefix cut off)
    char* Speaker;                 // +0x70 malloc'd "speaker" user property of a voice line
    unsigned char bVoice;          // +0x74
    unsigned char bSpatial;        // +0x75 the entity's Flags bit 1
    unsigned char bAmbient;        // +0x76 "BackgroundAmbient"
    unsigned char bMusic;          // +0x77 "BackgroundMusic"
    unsigned char bHoldScript;     // +0x78 the line's remaining time holds the script
                                   //       (IsHoldingScript); FUN_004570a0 writes 0 for the base /
                                   //       faction tokens and "$BGCS_Trent_", 1 for
                                   //       "$BGCS_Faction_Rebuff_*"
    unsigned char Pad79[3];
    Archetype::ISound* Arch;                    // +0x7c Archetype::ISound (or VoiceMsg) to play, or null
    ISoundSource* Handle;          // +0x80 the playing source, or null
    unsigned char bUseScriptFade;  // +0x84 the owning script's +0x60, set by the spawn
    unsigned char Pad85[3];
    float Attenuation;             // +0x88 audio prop 0 (dB)
    float Pan;                     // +0x8c audio prop 1
    // Audio props 2..7, FThornAudio8's dmin / dmax / ain / aout / atout / rmix (the Thorn
    // script keys). Stored and handed back only; nothing here applies them to the handle.
    float MinDistance;             // +0x90 dmin, 1e9
    float MaxDistance;             // +0x94 dmax, 1e9
    unsigned int ConeInsideAngle;  // +0x98 ain, integer 360
    unsigned int ConeOutsideAngle; // +0x9c aout, integer 360
    float ConeOutsideAttenuation;  // +0xa0 atout, 0
    float ReverbMix;               // +0xa4 rmix, 0
    Csys Local;                    // +0xa8 position 0, orientation identity; the handle takes
                                   //       the position (ISoundSource slot 35)

private:
    // FUN_004570a0 (thiscall, RET 8) -- resolve the voice line for the speaking character:
    // VoiceId is the character's voice (Thorn_Deformable FindActor's first out), Actor the
    // character itself. Caller: SetSpawnTable.
    void ResolveVoice(void* VoiceId, FCharacterObject* Actor);
};

struct FThornPlayerEngineSound : FThornComponent
{
    // FUN_004578a0 (thiscall, RET 0xc) -- base ctor, +0x6c = 0.
    FThornPlayerEngineSound(Csys const& ScriptFrame, FGfScript* InOwner,
                            FThornEntitySnap const* Desc);

    // ISoundControl (0x005ce508)
    GENRESULT DACOM_API StartSound(void* Desc, int TimeMs) override;        // 3  LAB_004579a0
    GENRESULT DACOM_API StopSound(void* Desc) override;                     // 4  LAB_004579c0
    GENRESULT DACOM_API SetAttenuation(float Attenuation) override;         // 5  LAB_004579e0
    GENRESULT DACOM_API GetAttenuation(float* OutAttenuation) override;     // 7  LAB_00457a10
    GENRESULT DACOM_API SetAudioProps(float const* Props8, int Mask) override; // 9  LAB_00457a30
    GENRESULT DACOM_API GetAudioProps(float* Props8, int Mask) override;    // 10 LAB_00457a60

    // identity tail
    void Stop() override;                                                  // 14 LAB_00457980
    InstanceIndex GetEngineInstance() override;                                    // 17 FUN_00450870

    float Attenuation; // +0x6c
};

struct FThornSoundReference : FThornComponent
{
    // FUN_00457a80 (thiscall, RET 0xc).
    FThornSoundReference(Csys const& ScriptFrame, FGfScript* InOwner,
                         FThornEntitySnap const* Desc);
    // FUN_00457b70 (FUN_00457b50 is the scalar deleting destructor): +0x6c = 0, base dtor.
    ~FThornSoundReference() override;

    // ICompoundControl
    GENRESULT DACOM_API SetTransform(const Transform* Transform12) override;   // 11 LAB_00457c00
    GENRESULT DACOM_API GetTransform(Transform* OutTransform12) override;      // 12 LAB_00457c20

    // ISoundControl (0x005ce6e8)
    GENRESULT DACOM_API StartSound(void* Desc, int TimeMs) override;        // 3  LAB_00457c40
    GENRESULT DACOM_API StopSound(void* Desc) override;                     // 4  LAB_00451370
    GENRESULT DACOM_API SetAttenuation(float Attenuation) override;         // 5  LAB_00457c50
    GENRESULT DACOM_API SetPan(float Pan) override;                         // 6  LAB_00457ca0
    GENRESULT DACOM_API GetAttenuation(float* OutAttenuation) override;     // 7  LAB_00457c80
    GENRESULT DACOM_API GetPan(float* OutPan) override;                     // 8  LAB_00457cd0
    GENRESULT DACOM_API SetAudioProps(float const* Props8, int Mask) override; // 9  LAB_00457cf0
    GENRESULT DACOM_API GetAudioProps(float* Props8, int Mask) override;    // 10 LAB_00457d10

    // IGeoTransformControl (0x005ce6ac)
    GENRESULT DACOM_API SetPosition(const Vector* Position) override;             // 3  LAB_00457bc0
    GENRESULT DACOM_API SetOrientation(const Matrix* Orient9) override;           // 4  LAB_00457be0
    GENRESULT DACOM_API GetPosition(Vector* OutPosition) override;          // 5  LAB_00453560
    GENRESULT DACOM_API GetOrientation(Matrix* OutOrient9) override;        // 6  LAB_00453580

    // identity tail
    void Stop() override;                                                  // 14 LAB_00450cd0
    InstanceIndex GetEngineInstance() override;                                    // 17 LAB_00453550

    FThornComponent* Target; // +0x6c the created sound of that name, or null
};
