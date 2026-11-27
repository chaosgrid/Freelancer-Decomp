// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Core/perf_options.h"
#include "GameFlow/Thorn/gf_script_object.h" // LAB_004548f0 / FUN_00454910: one transition-script latch.

#include "Space/cli_asteroid.h"
#include "Space/cli_nebula.h"
#include "Common/client_cliobj.h"
#include "common_archetype.h"
#include "common_ini.h"
#include "common_newchar.h"
#include "common_misc.h"   // GetUserDataPath
#include "common_timing.h" // Timing::GetMachineSpeed
#include "dacom.h"
#include "DALib.h"
#include "Core/globals.h" // ApplyGammaRampOption / ApplyDitherControlOption / ApplyMipFilterControlOption
#include "Space/HUD/hud_target.h"       // HudShipOfObjRw (FUN_00543320 chain)
#include "Space/player_behavior.h"  // GetPlayerObjRW (FUN_0054baf0)
#include "common_cobject.h"   // CShip / CEqObj::get_behavior_interface
#include "common_iface.h"     // IBehaviorManager::set_turn_sensitivity
#include "Common/input_manager.h"
#include "Common/Audio/menu_sound_arch.h"
#include "render/IRenderPipeline.h"
#include "render/slots.h"          // Render::EPipelineState
#include "render/shading.h"
#include "Space/space_solar_world.h"
#include "Space/star_sphere.h"
#include "Space/star_sys.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

namespace
{
    // Retail source coordinates for the FDUMP below.
    char const kPerfOptionsCpp[] = "E:\\FL\\Scratch\\Source\\Client\\PerfOptions.cpp";

    // DAT_0067ed10..DAT_0067ed28 — seven floats read back through FUN_005b3510(index).
    int const kPerfSlotCount = 7;
    float G_Slots[kPerfSlotCount] = {};

    bool G_bSkipMachineWarnings = false;    // DAT_0067a439

    int   G_MaxSubmittedSounds = 0x20;      // DAT_00615c6c
    bool  G_bEnableTooltips = true;         // DAT_00615c68 (.data initial 01h)
    bool  G_bEnableRollover = true;         // DAT_00615c69 (.data initial 01h)
    bool  G_bCockpit = false;               // DAT_0067ed44
    bool  G_bAutohideManeuverBar = false;   // DAT_0067ed46
    bool  G_bAdvancedGraphics = false;      // DAT_0067ed3c
    int   G_GeneralGraphics = -1;           // DAT_00615c64 (.data initial FFFFFFFFh)
    int   G_DefaultGraphics = 0;            // DAT_0067ed40
    float G_SpaceDustDetail = 1.0f;         // DAT_00614080 (.data initial 0x3f800000)
    float G_GenericEffectPerf = 1.0f;       // DAT_006129c8 (.data initial 0x3f800000)

    // ftol() truncates toward zero; retail relies on that everywhere below.
    int Ftol(float Value)
    {
        return static_cast<int>(Value);
    }

    // ------------------------------------------------------------------
    // Apply callbacks. Each is one retail function; its address is on the line.
    // ------------------------------------------------------------------

    void ApplyAudioMusic(float Value) { SetClientAudioMusicVolume(Value); }         // LAB_00428e00
    void ApplyAudioAmbient(float Value) { SetClientAudioAmbientVolume(Value); }     // LAB_00428e30
    void ApplyAudioSfx(float Value) { SetClientAudioSfxVolume(Value); }             // LAB_00428e60
    void ApplyAudioInterface(float Value) { SetClientAudioInterfaceVolume(Value); } // LAB_00428e90
    void ApplyAudioVoice(float Value) { SetClientAudioVoiceVolume(Value); }         // LAB_00428ec0

    void ApplySoundMaxDistScale(float Value)  // LAB_005b6230 — DAT_0067ed18
    {
        // Readers go through FUN_005b3510(2).
        G_Slots[kPerfSlotSoundMaxDistScale] = Value;
    }

    void ApplySoundLevelOfDetail(float Value)  // LAB_005b4bd0 — DAT_0067ed1c + FUN_0042ac30
    {
        // floor(2 * (1 - v)) in double, as on the x87 stack.
        G_Slots[kPerfSlotSoundLevelOfDetail] = Value;
        SetClientSoundLod(static_cast<int>(floor(2.0 * (1.0 - static_cast<double>(Value)))));
    }

    void ApplyUse3dSounds(float Value)  // LAB_005b6240 — DAT_0067ed20
    {
        G_Slots[kPerfSlotUse3dSounds] = Value;
    }

    void ApplyDynamicCommHead(float Value)  // LAB_005b6250 — DAT_0067ed24
    {
        G_Slots[kPerfSlotDynamicCommHead] = Value;
    }

    void ApplyDifficultyScale(float Value)  // LAB_005b6260 — DAT_0067ed28
    {
        G_Slots[kPerfSlotDifficultyScale] = Value;
    }

    void ApplyMaxSubmittedSounds(float Value)  // LAB_005b3870 — DAT_00615c6c
    {
        // DAT_005ca230 / DAT_005c75e0; the product and sum stay on the x87 stack, so they are not
        // rounded to float.
        G_MaxSubmittedSounds = static_cast<int>(static_cast<double>(Value) * 100.0 + 0.5);
    }

    void ApplyEnableTooltips(float Value) { G_bEnableTooltips = Value > 0.5f; }  // LAB_005b37e0
    void ApplyEnableRollover(float Value) { G_bEnableRollover = Value > 0.5f; }  // LAB_005b3800
    void ApplyCockpit(float Value) { G_bCockpit = Value > 0.5f; }                // LAB_005b3820

    void ApplyAutohideManeuverBar(float Value)  // LAB_005b3840
    {
        G_bAutohideManeuverBar = Value > 0.5f;
    }

    void ApplyGeneralGraphics(float Value) { G_GeneralGraphics = Ftol(Value); }     // LAB_005b3890
    void ApplyAdvancedGraphics(float Value) { G_bAdvancedGraphics = Value != 0.f; } // LAB_005b38a0
    void ApplyDefaultGraphics(float Value) { G_DefaultGraphics = Ftol(Value); }     // LAB_005b38c0

    void ApplySpaceDustDetail(float Value) { G_SpaceDustDetail = Value; }           // LAB_0054c3e0
    // LAB_004f7090 (DAT_005c89b8 is the double 1.0).
    void ApplyGenericEffectPerf(float Value)
    {
        G_GenericEffectPerf = static_cast<float>(1.0 - static_cast<double>(Value));
    }
    void ApplySunEffect(float Value) // LAB_00510bf0 — DAT_0067509c
    {
        // 1.33333 is the double DAT_005db640, not 4/3. Retail stores the float and keeps the
        // unrounded Q on the FPU for the loop.
        double Q = static_cast<double>(Value) * 1.33333;
        if (Q > 1.0)
            Q = 1.0;
        G_SunEffectQuality = static_cast<float>(Q);

        // Every star of the list DAT_00675180 (StarSphereList): its sun body (+0x14) gets
        // LodReduction (+0x2b8) = ftol(MaxLod (+0x2b4) * (1.0f - Q) + 0.5f). No null tests.
        double const Keep = 1.0 - Q;
        FStarSphereListNode* const Sentinel = StarSphereList()->Sentinel;
        for (FStarSphereListNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        {
            FSunEffectBody* const Body = Node->Sphere->SunBody;
            Body->LodReduction = static_cast<int>(static_cast<double>(Body->MaxLod) * Keep + 0.5);
        }
    }

    // LAB_005b42a0 - apply shared by AtmosphereMaterialConfig (option +0x20 = 1) and
    // HUDAnimConfig (+0x20 = 4); vtable PTR_OnAddHost_005e6f80. 0.999 is DAT_005c8b68.
    void ApplyMaterialRecordOption(unsigned long RecordIndex, float Value)
    {
        if (DALib::MaterialLib == nullptr)
            return;
        unsigned long bEnabled = 1;
        if (!(static_cast<double>(Value) > 0.999))
            bEnabled = 0;
        DALib::MaterialLib->SetGlobalRecordValue(RecordIndex, &bEnabled);
        DALib::MaterialLib->ResolveTexturesGroup(0, 0.0f);
    }

    // LAB_004c70b0 (cdecl, the option value) - TurnSensitivity: the player IObjRW
    // (FUN_0054baf0), its +0xc CBase, the CObject at +4 of class 0x503 (the inlined
    // FUN_00543320 chain, each step null-tested), CEqObj::get_behavior_interface; a manager gets
    // IBehaviorManager::set_turn_sensitivity(Value).
    void ApplyTurnSensitivity(float Value)
    {
        CShip* const Ship = HudShipOfObjRw(GetPlayerObjRW());
        if (Ship == nullptr)
            return;
        IBehaviorManager* const Behavior = Ship->get_behavior_interface();
        if (Behavior == nullptr)
            return;
        Behavior->set_turn_sensitivity(Value);
    }

    void ApplyAtmosphereMaterialConfig(float Value) { ApplyMaterialRecordOption(1, Value); }
    void ApplyHudAnimConfig(float Value) { ApplyMaterialRecordOption(4, Value); }

    void ApplyUseEnvironmentMaps(float Value)  // LAB_005b4700 - DAT_0067ed10
    {
        // A non-zero device ability 8 keeps the raw value in slot 0, every other path stores 0.
        // The HRESULT is not examined.
        unsigned long Ability = 0;
        IRenderPipeline* Pipe = DALib::Pipeline;
        if (Value > 0.f && Pipe != nullptr)
        {
            Pipe->QueryDeviceAbility(8, &Ability);
            if (Ability != 0)
            {
                G_Slots[kPerfSlotUseEnvironmentMaps] = Value;
                return;
            }
        }
        G_Slots[kPerfSlotUseEnvironmentMaps] = 0.f;
    }

    void ApplyHighDetailTextures(float Value)  // LAB_005b4870 - DAT_0067ed14
    {
        // DALib::Pipeline is dereferenced without a null check here.
        unsigned long VidMem = 0;
        int bLowVidMem = 0;
        IRenderPipeline* Pipe = DALib::Pipeline;
        if (Pipe->QueryDeviceAbility(3, &VidMem) >= 0)
            bLowVidMem = ((VidMem & 0xfff00000u) < 0x01100000u) ? 1 : 0;

        // Each flag is "v > double constant".
        double const V = static_cast<double>(Value);
        int const bHighDetail = (V > 0.6 && bLowVidMem == 0) ? 1 : 0; // DAT_005d5068
        int const bPipelineState4 = (V > 0.5) ? 1 : 0;                // DAT_005c75e8
        unsigned long Record0 = (V > 0.4) ? 1 : 0;                    // DAT_005e7028
        unsigned long Record2 = (V > 0.3) ? 1 : 0;                    // DAT_005c9858

        if (DALib::MaterialLib != nullptr)
        {
            DALib::MaterialLib->SetGlobalRecordValue(0, &Record0);
            DALib::MaterialLib->SetGlobalRecordValue(2, &Record2);
            DALib::MaterialLib->ResolveTexturesGroup(0, 0.0f);
        }
        if (DALib::TextureLib != nullptr)
            DALib::TextureLib->SetGenericSlot(4, bHighDetail != 0 ? 0u : 0x80u);
        G_Slots[kPerfSlotHighDetailTextures] = bHighDetail != 0 ? 1.f : 0.f;
        Pipe = DALib::Pipeline;
        if (Pipe != nullptr)
            Pipe->SetPipelineState(Render::PipelineStateTextureAllow32Bit, bPipelineState4);
    }

    // level_of_detail: the retail option object is 0x28 bytes (ctor 005b4430) -- the common
    // 0x1c head, then +0x1c an archetype enumerator (vtable PTR_LAB_005e6fb8, one slot
    // LAB_005b4540), +0x20 a byte and +0x24 a float, both written by the apply below. The
    // port keeps the three tail members as file statics.
    //
    // LAB_005b4540: a record with a companion (Root+0x04) gets FUN_00406c50(companion, 0); every
    // record returns true, so the walk never stops early.
    struct FLevelOfDetailEnumerator : Archetype::ArchEnumerator
    {
        bool Enumerate(Archetype::Root* Record) override
        {
            if (Record->Companion != nullptr)
                Archetype::CliObjRecomputeLodPick(Record->Companion, nullptr);
            return true;
        }
    };

    FLevelOfDetailEnumerator G_LevelOfDetailEnumerator; // option +0x1c
    bool G_bLevelOfDetailLow = false;                   // option +0x20
    float G_LevelOfDetailScale = 0.f;                   // option +0x24

    void ApplyLevelOfDetail(float Value)  // LAB_005b4560 (thiscall on the option, RET 4)
    {
        // 0.75 is DAT_005debf8; a NaN v is not low.
        bool const bLow = static_cast<double>(Value) < 0.75;
        G_bLevelOfDetailLow = bLow;

        // All on the x87 stack, then rounded once to float. Constants: 4/3 DAT_005e6fd8,
        // 0.5 DAT_005c75e8, 4.0 DAT_005db628, 1.0 DAT_005c89b8.
        double Scale = 0.0;
        if (bLow)
            Scale = static_cast<double>(Value) * (4.0 / 3.0) * 0.5 + 0.5;
        else
            Scale = (static_cast<double>(Value) - 0.75) * 4.0 + 1.0;
        G_LevelOfDetailScale = static_cast<float>(Scale);

        Archetype::CliObjSetArchLodGlobals(bLow ? 1 : 0, G_LevelOfDetailScale); // FUN_004022b0

        Archetype::EnumSimples(&G_LevelOfDetailEnumerator);
        Archetype::EnumShips(&G_LevelOfDetailEnumerator);
        Archetype::EnumEquipment(&G_LevelOfDetailEnumerator);
        Archetype::EnumSolars(&G_LevelOfDetailEnumerator);

        SolarSpheresApplyDetail(G_LevelOfDetailScale); // FUN_005488a0
    }

    // ------------------------------------------------------------------
    // The registry. Retail builds it from static initializers into a heap std::list
    // (DAT_0067ed04, count DAT_0067ed08; FUN_005b3520 pushes each option object). The port holds
    // the same rows in a fixed table, in retail's list order -- the _initterm order of the
    // registrars (XC slots 54, 72..74, 469, 586, 611, 626, 670, 828, 837, then 1107..1131). That
    // order drives FUN_005b40f0 (apply) and FUN_005b3ec0 (save). The [PerfCount] check compares
    // against the full retail count of 36.
    // ------------------------------------------------------------------
    FPerfOption G_Options[] = {
        { "mouse_speed",              0x5dd, 0, 0.f, -1.f, ApplyMouseSpeedOption },  // LAB_0041d970
        { "gamma_ramp",               0x534, 0, 0.f, -1.f, ApplyGammaRampOption },  // LAB_004244e0
        { "DitherControl",            0x67f, 0, 0.f, -1.f, ApplyDitherControlOption },  // LAB_00424630
        { "MipFilterControlCallback", 0,     0, 0.f, -1.f, ApplyMipFilterControlOption },  // LAB_004246d0
        // TurnSensitivity / Nebula / SunEffect / Asteroids / star_sphere: the ctor leaves +0x08 at
        // 0 and passes the IDS as FUN_005b3520's second argument, which stores it at +0x08 when
        // non-zero.
        { "TurnSensitivity",          0x614, 0, 0.f, -1.f, ApplyTurnSensitivity },  // LAB_004c70b0
        { "generic_effect_perf",      0x5cb, 0, 0.f, -1.f, ApplyGenericEffectPerf },
        { "Nebula",                   0x57d, 0, 0.f, -1.f, NebulaApplyGlobalDetail },  // FUN_00509b80
        { "SunEffect",                0x5ca, 0, 0.f, -1.f, ApplySunEffect },  // LAB_00510bf0
        { "Asteroids",                0x57c, 0, 0.f, -1.f, CliAsteroidApplyGlobalDetail },  // LAB_005267f0
        { "space_dust_detail",        0x57a, 0, 0.f, -1.f, ApplySpaceDustDetail },
        { "star_sphere",              0x57b, 0, 0.f, -1.f, StarSphereApplyDetail },  // LAB_00552cd0
        { "AtmosphereMaterialConfig", 0x5c9, 0, 0.f, -1.f, ApplyAtmosphereMaterialConfig },  // LAB_005b42a0
        { "HUDAnimConfig",            0x65e, 0, 0.f, -1.f, ApplyHudAnimConfig },  // LAB_005b42a0
        { "level_of_detail",          0x5d5, 0, 0.f, -1.f, ApplyLevelOfDetail },  // LAB_005b4560
        { "use_environment_maps",     0x657, 0, 0.f, -1.f, ApplyUseEnvironmentMaps },  // LAB_005b4700
        { "high_detail_textures",     0x658, 0, 0.f, -1.f, ApplyHighDetailTextures },  // LAB_005b4870
        { "sound_max_dist_scale",     0x77a, 0, 0.f, -1.f, ApplySoundMaxDistScale },
        { "sound_level_of_detail",    0x77b, 0, 0.f, -1.f, ApplySoundLevelOfDetail },
        { "USE_3D_SOUNDS",            0x77e, 0, 0.f, -1.f, ApplyUse3dSounds },
        { "audio_music",              0x539, 0, 0.f, -1.f, ApplyAudioMusic },
        { "audio_ambient",            0x584, 0, 0.f, -1.f, ApplyAudioAmbient },
        { "audio_sfx",                0x538, 0, 0.f, -1.f, ApplyAudioSfx },
        { "audio_interface",          0x583, 0, 0.f, -1.f, ApplyAudioInterface },
        { "audio_voice",              0x581, 0, 0.f, -1.f, ApplyAudioVoice },
        // FUN_005b5380: +0x18 = [PTR_set_slider_005c6ce8] (COMMON.DLL RtcSlider::set_slider) on the
        // base option vtable 005d6d50, whose apply 004c7160 calls it cdecl with the value.
        { "rtc_performance",          0x656, 0, 0.f, -1.f, RtcSlider::set_slider },
        { "general_graphics",         0,     0, 0.f, -1.f, ApplyGeneralGraphics },
        { "advanced_graphics",        0,     0, 0.f, -1.f, ApplyAdvancedGraphics },
        { "default_graphics",         0,     0, 0.f, -1.f, ApplyDefaultGraphics },
        { "skip_transition_scripts",  0x5d7, 0, 0.f, -1.f, GfScriptSequenceSetSkipOption },
        { "enable_tooltips",          0x578, 0, 0.f, -1.f, ApplyEnableTooltips },
        { "enable_rollover",          0x579, 0, 0.f, -1.f, ApplyEnableRollover },
        { "cockpit",                  0x5da, 0, 0.f, -1.f, ApplyCockpit },
        { "USE_DYNAMIC_COMM_HEAD",    0x631, 0, 0.f, -1.f, ApplyDynamicCommHead },
        { "DIFFICULTY_SCALE",         0x57f, 0, 0.f, -1.f, ApplyDifficultyScale },
        { "autohide_maneuver_bar",    0x613, 0, 0.f, -1.f, ApplyAutohideManeuverBar },
        { "max_submitted_sounds",     0x687, 0, 0.f, -1.f, ApplyMaxSubmittedSounds },
    };

    unsigned const kOptionCount = sizeof(G_Options) / sizeof(G_Options[0]);
}

    // FUN_004a92a0 — hardware graphics score in [0.25, 1.0], through
    // IRenderPipeline::QueryDeviceAbility (vtable+0x20).
    //
    // Ability index 4 = HW T&L flag (RP8 Ability_[4]: DevCaps HWTRANSFORMANDLIGHT &&
    // DRAWPRIMITIVES2EX). Ability index 3 = video memory bytes (Ability_[3] = VidMem_).
    // Timing::GetMachineSpeed thresholds are 1000 / 800 / 700 (retail units).
    float ProbeMachineGraphicsLevel()
    {
        float GfxScore = 1.f;
        unsigned long AbilityHwTnL = 0; // out of QueryDeviceAbility(4)
        unsigned long AbilityVidMem = 0;

        IRenderPipeline* Pipe = DALib::Pipeline;
        if (Pipe != nullptr)
        {
            // Retail always reaches the first Query with GfxScore already 1.0. Failure or a
            // non-zero HW-T&L flag jumps to the CPU-speed half with that 1.0 intact.
            AbilityHwTnL = 0;
            GENRESULT const Hr4 = Pipe->QueryDeviceAbility(4, &AbilityHwTnL);
            if (Hr4 >= 0 && AbilityHwTnL == 0)
            {
                // No HW T&L: drop to 0.75, then maybe 0.5 from VRAM.
                GfxScore = 0.75f; // 0x3f400000
                AbilityVidMem = 0;
                GENRESULT const Hr3 = Pipe->QueryDeviceAbility(3, &AbilityVidMem);
                if (Hr3 >= 0)
                {
                    // (VidMem & 0xfff00000) < 0x01100000 → under ~17 MiB → 0.5
                    if ((AbilityVidMem & 0xfff00000u) < 0x01100000u)
                        GfxScore = 0.5f; // 0x3f000000
                }
            }
        }

        float CpuScore;
        int const MachineSpeed = Timing::GetMachineSpeed();
        if (MachineSpeed > 0x3e8) // > 1000
        {
            CpuScore = 1.f;
        }
        else
        {
            // Ability_[4]==0 → 0.6 (DAT_005d2c9c); else 0.75 (DAT_005c8930).
            if (AbilityHwTnL == 0)
                CpuScore = 0.6f;
            else
                CpuScore = 0.75f;

            if (MachineSpeed < 0x320) // < 800
            {
                if (MachineSpeed < 0x2bc) // < 700
                    CpuScore = 0.25f;    // DAT_005c94f8
                else
                    CpuScore = 0.6f;     // DAT_005d2c9c
            }
        }

        if (GfxScore < CpuScore)
            return GfxScore;
        return CpuScore;
    }

FPerfOption* FindPerfOption(char const* Name)
{
    // FUN_005b36b0 — the first option whose name (+0x04) matches; Name is not null-tested.
    for (unsigned Index = 0; Index < kOptionCount; ++Index)
    {
        if (strcmp(G_Options[Index].Name, Name) == 0)
            return &G_Options[Index];
    }
    return nullptr;
}

FPerfOption* FindPerfOptionByIds(int IdsName)
{
    // FUN_005b3770 — the first option whose IDS name (+0x08) equals IdsName. Zero is not
    // special-cased: it finds the first option registered without an IDS name.
    for (unsigned Index = 0; Index < kOptionCount; ++Index)
    {
        if (G_Options[Index].IdsName == IdsName)
            return &G_Options[Index];
    }
    return nullptr;
}

unsigned GetPerfOptionCount()
{
    return kOptionCount;
}

FPerfOption* GetPerfOptionAt(unsigned Index)
{
    if (Index >= kOptionCount)
        return nullptr;
    return &G_Options[Index];
}

float GetPerfOptionSlot(int Index)
{
    // FUN_005b3510: no bounds check.
    return G_Slots[Index];
}

void ApplyChangedPerfOptions()
{
    // FUN_005b40f0.
    for (unsigned Index = 0; Index < kOptionCount; ++Index)
    {
        FPerfOption& Option = G_Options[Index];
        if (Option.Applied == Option.Value)
            continue;
        Option.Applied = Option.Value;
        if (Option.Apply != nullptr)
            Option.Apply(Option.Value);
    }
}

bool GetSkipMachineWarnings()
{
    return G_bSkipMachineWarnings;
}

void SetSkipMachineWarnings(bool bSkip)
{
    G_bSkipMachineWarnings = bSkip;
}

int GetMaxSubmittedSounds()
{
    // FUN_005b3860.
    return G_MaxSubmittedSounds;
}

bool GetPerfEnableTooltips() { return G_bEnableTooltips; }
bool GetPerfEnableRollover() { return G_bEnableRollover; }
bool GetPerfCockpit() { return G_bCockpit; }
bool GetPerfAutohideManeuverBar() { return G_bAutohideManeuverBar; }
bool GetPerfSkipTransitionScripts() { return GfScriptSequenceSkipLatch(); }
bool GetPerfAdvancedGraphics() { return G_bAdvancedGraphics; }
int GetPerfGeneralGraphics() { return G_GeneralGraphics; }
int GetPerfDefaultGraphics() { return G_DefaultGraphics; }
float GetPerfSpaceDustDetail() { return G_SpaceDustDetail; }
float GetPerfGenericEffectPerf() { return G_GenericEffectPerf; }

bool LoadPerfOptions(char const* FileName, FPerfDisplaySettings* Display)
{
    // FUN_005b38d0. GetUserDataPath's result is not tested.
    char UserPath[MAX_PATH];
    GetUserDataPath(UserPath);

    char IniPath[MAX_PATH];
    _makepath(IniPath, nullptr, UserPath, FileName, nullptr);

    for (int Slot = 0; Slot < kPerfSlotCount; ++Slot)
        G_Slots[Slot] = 0.f;

    bool bOk = false;
    bool bVersionOk = false;
    int Expected = 0;
    int Parsed = 0;

    INI_Reader Ini;
    if (!Ini.open(IniPath, false))
        return false;

    if (Ini.read_header())
    {
        do
        {
            if (Ini.is_header("PerfVersion"))
            {
                if (!Ini.read_value())
                    continue;
                do
                {
                    if (Ini.is_value("version"))
                        bOk = Ini.get_value_float(0) >= 5.f;
                    else if (Ini.is_value("SkipMachineWarnings"))
                        G_bSkipMachineWarnings = Ini.get_value_bool(0);
                } while (Ini.read_value());
                // Retail stashes the version verdict here and restores it after every accepted
                // [PerfOptions] entry, so the last entry cannot decide the whole file.
                bVersionOk = bOk;
            }
            else if (Ini.is_header("PerfCount"))
            {
                if (!Ini.read_value())
                    continue;
                do
                {
                    if (Ini.is_value("count"))
                        Expected = Ftol(Ini.get_value_float(0));
                } while (Ini.read_value());
            }
            else if (Ini.is_header("PerfOptions"))
            {
                if (!bOk)
                {
                    FDUMP(kFdumpTrace1, "%s(%d) : TRACE_1:General:%s", kPerfOptionsCpp, 0x127,
                          "perfoptions out of date -- reverting to defaults");
                    break;
                }
                if (!Ini.read_value())
                    continue;
                do
                {
                    if (!bOk)
                        break;
                    FPerfOption* Option = FindPerfOption(Ini.get_name_ptr());
                    if (Option == nullptr)
                        continue;
                    float const Value = Ini.get_value_float(0);
                    Parsed += 1;
                    // A value below zero or NaN is rejected.
                    if (!(Value >= 0.f))
                    {
                        bOk = false;
                        bVersionOk = false;
                        continue;
                    }
                    Option->Value = Value;
                    bOk = bVersionOk;
                } while (Ini.read_value());
            }
            else if (Ini.is_header("Display") && Display != nullptr)
            {
                if (!Ini.read_value())
                    continue;
                do
                {
                    if (Ini.is_value("size"))
                    {
                        Display->Width = Ini.get_value_int(0);
                        Display->Height = Ini.get_value_int(1);
                    }
                    else if (Ini.is_value("color_bpp"))
                        Display->ColorBpp = Ini.get_value_int(0);
                    else if (Ini.is_value("depth_bpp"))
                        Display->DepthBpp = Ini.get_value_int(0);
                } while (Ini.read_value());
            }
        } while (Ini.read_header());

        // The file is trusted only when it lists exactly as many known options as it claims.
        if (Parsed != Expected)
            bOk = false;
    }

    Ini.close();

    if (bOk)
        ApplyChangedPerfOptions();

    return bOk;
}

void SavePerfOptions(char const* FileName)
{
    // FUN_005b3ec0. GetUserDataPath's result is not tested.
    char UserPath[MAX_PATH];
    GetUserDataPath(UserPath);

    char IniPath[MAX_PATH];
    _makepath(IniPath, nullptr, UserPath, FileName, nullptr);

    FILE* File = fopen(IniPath, "w");
    if (File == nullptr)
        return;

    // DAT_005e6dfc = 5.0f, pushed as a double.
    fprintf(File, "[PerfVersion]\nversion=%g\n", 5.0);
    fprintf(File, "SkipMachineWarnings=%s\n\n", G_bSkipMachineWarnings ? "TRUE" : "FALSE");
    fprintf(File, "[PerfCount]\ncount=%g\n\n", static_cast<double>(kOptionCount));
    fprintf(File, "[PerfOptions]\n");
    for (unsigned Index = 0; Index < kOptionCount; ++Index)
    {
        // Retail writes only options it has applied at least once (+0x14 >= 0).
        if (G_Options[Index].Applied >= 0.f)
            fprintf(File, "%s = %.2f\n", G_Options[Index].Name,
                    static_cast<double>(G_Options[Index].Applied));
    }
    // Always written, straight from the options object.
    fprintf(File, "\n[Display]\n");
    fprintf(File, "size= %d, %d\n", g_display.width, g_display.height);
    fprintf(File, "color_bpp= %d\n", g_display.color_bpp);
    fprintf(File, "depth_bpp= %d\n", g_display.depth_bpp);
    fclose(File);
}
