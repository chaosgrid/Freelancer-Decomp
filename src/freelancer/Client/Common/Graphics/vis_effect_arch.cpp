// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Common/Graphics/vis_effect_arch.h"

#include "alchemy.h"
#include "Common/client_cliobj.h" // Archetype::ResolveMaterialEntry (FUN_0041aa50) / ReleaseMaterialEntrySlot (FUN_0041b030)
#include "common_id.h"
#include "common_ini.h"
#include "dacom.h"
#include "DALib.h"
#include "Space/Effects/effect_type_info.h"
#include "Common/Audio/menu_sound_arch.h"
#include "readfile.h"

#include <map>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Defined below; used by BeamArchFindOrInsert / LoadEffectSection.
FBeamEffectArch* FindBeamEffectArchByNickname(char const* Nickname);
FVisEffectArch* FindVisEffectByNickname(char const* Nickname);
FEffectArch* FindEffectArch(unsigned const* NicknameId);

namespace
{
    static const float kColorScale = 0.003921569f; // 1/255
    static const float kBrightMin = 0.01f;
    static const unsigned kBrightMinBits = 0x3c23d70a; // 0.01f
    static const unsigned kOneBits = 0x3f800000;       // 1.0f

    // DAT_006164b0 -- the EffectArch registry, a std::map keyed by the nickname's CreateID
    // (unsigned compare, FUN_0052c7a0 / FUN_0046d6d0) holding operator-new'd EffectArchs.
    // Built by the static initializer LAB_0040e9c0, destroyed through atexit LAB_0040e9f0.
    std::map<unsigned, FEffectArch*> G_EffectArchs;

    // DAT_00667e14 -- set by FUN_0042e290 once the FxRuntime is bound, cleared by FUN_0042e2f0.
    bool G_bFxRuntimeBound = false;

    // s_E:\FL\Scratch\Source\Client\cmn\EffectArch.cpp_005c84cc.
    char const kEffectArchCpp[] = "E:\\FL\\Scratch\\Source\\Client\\cmn\\EffectArch.cpp";

    // The nickname id as retail's ID_String::get_string formats it.
    char const* EffectArchNickname(FEffectArch const* Arch)
    {
        ID_String Name;
        Name.Id = Arch->NicknameId;
        return Name.get_string();
    }

    // Client\cmn\ALELib.cpp. The map DAT_00616440 is ordered by stricmp (FUN_004016f0 /
    // FUN_00401130); its value is { use count (node + 0x10), handle (node + 0x14) }.
    struct FAleNameLess
    {
        bool operator()(std::string const& A, std::string const& B) const
        {
            return _stricmp(A.c_str(), B.c_str()) < 0;
        }
    };

    struct FAleLibEntry
    {
        int RefCount;
        int Handle;
    };

    std::map<std::string, FAleLibEntry, FAleNameLess> G_AleLib; // DAT_00616440
    int G_AleLibNextHandle = 1;                                 // DAT_006101d0 (.data init 1)

    // DAT_00667df0 -- Client\cmn\VisEffectArch.cpp's VisEffect map, built by the static
    // initializer LAB_0042d8f0 and torn down through atexit LAB_0042d920. Node: key at +0x0c,
    // the VisEffect by value at +0x10. Retail keys it by the StringAlloc'd nickname POINTER
    // (unsigned compare FUN_0042f0f0, lower bound FUN_0052c7a0, find FUN_0042dd20).
    // StringAlloc(Text, false) interns through Common's stricmp-ordered StringCache (Common
    // FUN_06333ea0), so two keys are one entry exactly when their texts are stricmp-equal.
    // Deviation: the port's StringAlloc does not intern, so the key here is the text itself
    // under the same stricmp: the same entries and lookups; only the iteration order differs,
    // and nothing but the exit teardown walks it.
    // Teardown order as in retail's XC table: G_AleLib (slot 0x0060f010, above in this file)
    // and the material cache (client_cliobj.cpp, slot 0x0060f0bc, linked before this file --
    // see the note in freelancer.vcxproj) are built first, so both outlive this map, whose
    // ~FVisEffectArch (FUN_0042edb0) releases through them.
    std::map<std::string, FVisEffectArch, FAleNameLess> G_VisEffects;

    // DAT_00667d78 -- the BeamSpear / BeamBolt map, the same shape: built by the static
    // initializer at 0x0042e320, torn down through atexit 0x0042e350, keyed by the StringAlloc'd
    // nickname pointer (operator[] FUN_0042ed30, find FUN_0042e9e0 through FUN_0052c7a0's
    // lower bound), the value (FUN_0042e430 defaults) at node + 0x10. Keyed here by the text
    // under stricmp for the reason given on G_VisEffects.
    std::map<std::string, FBeamEffectArch, FAleNameLess> G_BeamArches;

    void CopyZ(char* Dest, int DestChars, char const* Src)
    {
        if (Dest == nullptr || DestChars <= 0)
            return;
        if (Src == nullptr)
        {
            Dest[0] = '\0';
            return;
        }
        strncpy(Dest, Src, static_cast<size_t>(DestChars - 1));
        Dest[DestChars - 1] = '\0';
    }

    // 5-char retail strncmp style (counts down including NUL for short words).
    bool Match5(char const* Value, char const* Literal)
    {
        if (Value == nullptr || Literal == nullptr)
            return false;
        int Remain = 5;
        char const* A = Value;
        char const* B = Literal;
        bool bEqual = true;
        while (Remain != 0)
        {
            Remain -= 1;
            bEqual = (*A == *B);
            if (!bEqual)
                break;
            A += 1;
            B += 1;
        }
        return bEqual;
    }

    // FUN_0042e1d0 (thiscall) -- resolve one material-cache entry per texture name, in order.
    // A name that does not resolve is reported and erased from the name list (the iterator
    // stays put); a resolved one is appended to TextureSlots. Returns the last iteration's
    // outcome, false for an empty list.
    bool VisEffectArchLoadTextures(FVisEffectArch* Vis)
    {
        bool bOk = false;
        size_t Index = 0;
        while (Index != Vis->Textures.size())
        {
            // FUN_0041aa50.
            Archetype::FCliMaterialEntry* const Slot =
                Archetype::ResolveMaterialEntry(Vis->Textures[Index].c_str(), true, 0xffffffffu);
            bOk = Slot != nullptr;
            if (bOk)
            {
                Vis->TextureSlots.push_back(Slot);
                ++Index;
            }
            else
            {
                // The "%s" before the name is the .rdata literal "temp".
                FDUMP(0x100001,
                      "%s(%d) : *** ERROR: [VisEffect] %s: Failed to load texture library: %s.  "
                      "Removing from txm list.",
                      "E:\\FL\\Scratch\\Source\\Client\\cmn\\VisEffectArch.cpp", 0x166, "temp",
                      Vis->Textures[Index].c_str());
                Vis->Textures.erase(Vis->Textures.begin() + static_cast<ptrdiff_t>(Index));
            }
        }
        return bOk;
    }

    // Inlined in FUN_0042e080 and FUN_0042e160: FUN_0041b030 on every slot.
    void VisEffectArchReleaseTextures(FVisEffectArch* Vis)
    {
        for (size_t Index = 0; Index < Vis->TextureSlots.size(); ++Index)
            Archetype::ReleaseMaterialEntrySlot(&Vis->TextureSlots[Index]);
        Vis->TextureSlots.clear();
    }

    // FUN_0042e430 — default TrailAppearance / BeamEffectArch (all 1.0f).
    void BeamArchInitDefaults(FBeamEffectArch* Arch)
    {
        memset(Arch, 0, sizeof(*Arch));
        Arch->Nickname = nullptr;
        Arch->SegmentMode = 0;
        Arch->UvModeV = 0;
        Arch->UvModeU = 0;
        float* F = &Arch->ColorCoreR;
        int Index = 0;
        for (Index = 0; Index < 28; ++Index)
            F[Index] = 1.0f;
    }

    // FUN_0042e920 — clamp lengths vs core_width and brightness to [0.01, 1].
    void BeamArchClamp(FBeamEffectArch* Arch)
    {
        if (Arch->Radius0 < Arch->RadiusNear)
            Arch->Radius0 = Arch->RadiusNear;
        if (Arch->SegmentMode == 0)
        {
            if (Arch->Radius1 < Arch->RadiusNear)
                Arch->Radius1 = Arch->RadiusNear;
        }
        else if (Arch->SegmentMode == 1)
        {
            if (Arch->Radius1 < Arch->SecCoreWidth)
                Arch->Radius1 = Arch->SecCoreWidth;
        }
        if (Arch->RadiusTip < Arch->RadiusNear)
            Arch->RadiusTip = Arch->RadiusNear;

        if (Arch->IntensityScale0 < kBrightMin)
        {
            unsigned Bits = kBrightMinBits;
            Arch->IntensityScale0 = *reinterpret_cast<float*>(&Bits);
        }
        else if (Arch->IntensityScale0 > 1.0f)
        {
            unsigned Bits = kOneBits;
            Arch->IntensityScale0 = *reinterpret_cast<float*>(&Bits);
        }

        if (Arch->IntensityScale1 < kBrightMin)
        {
            unsigned Bits = kBrightMinBits;
            Arch->IntensityScale1 = *reinterpret_cast<float*>(&Bits);
            return;
        }
        if (Arch->IntensityScale1 > 1.0f)
        {
            unsigned Bits = kOneBits;
            Arch->IntensityScale1 = *reinterpret_cast<float*>(&Bits);
        }
    }

    void WriteRgb255(float* Dest, float R, float G, float B)
    {
        Dest[0] = R * kColorScale;
        Dest[1] = G * kColorScale;
        Dest[2] = B * kColorScale;
    }

    // FUN_0042e4a0 — one INI value into an open BeamSpear/Bolt arch.
    bool BeamArchParseValue(FBeamEffectArch* Arch, INI_Reader* Reader)
    {
        if (Reader->is_value("tip_color"))
        {
            WriteRgb255(&Arch->ColorCoreR, Reader->get_value_float(0), Reader->get_value_float(1),
                        Reader->get_value_float(2));
            return true;
        }
        if (Reader->is_value("tail_color"))
        {
            WriteRgb255(&Arch->ColorMidR, Reader->get_value_float(0), Reader->get_value_float(1),
                        Reader->get_value_float(2));
            return true;
        }
        if (Reader->is_value("core_color"))
        {
            WriteRgb255(&Arch->ColorOuterR, Reader->get_value_float(0), Reader->get_value_float(1),
                        Reader->get_value_float(2));
            return true;
        }
        if (Reader->is_value("sec_core_color"))
        {
            WriteRgb255(&Arch->ColorBand1R, Reader->get_value_float(0), Reader->get_value_float(1),
                        Reader->get_value_float(2));
            return true;
        }
        if (Reader->is_value("outter_color"))
        {
            WriteRgb255(&Arch->ColorTipR, Reader->get_value_float(0), Reader->get_value_float(1),
                        Reader->get_value_float(2));
            return true;
        }
        if (Reader->is_value("sec_outter_color"))
        {
            WriteRgb255(&Arch->ColorBand2R, Reader->get_value_float(0), Reader->get_value_float(1),
                        Reader->get_value_float(2));
            return true;
        }
        if (Reader->is_value("tip_length"))
        {
            Arch->Radius0 = Reader->get_value_float(0);
            return true;
        }
        if (Reader->is_value("tail_length"))
        {
            Arch->Radius1 = Reader->get_value_float(0);
            return true;
        }
        if (Reader->is_value("core_length"))
        {
            Arch->RadiusMid = Reader->get_value_float(0);
            return true;
        }
        if (Reader->is_value("core_width"))
        {
            Arch->RadiusNear = Reader->get_value_float(0);
            return true;
        }
        if (Reader->is_value("sec_core_width"))
        {
            Arch->SecCoreWidth = Reader->get_value_float(0);
            return true;
        }
        if (Reader->is_value("head_brightness"))
        {
            Arch->IntensityScale0 = Reader->get_value_float(0);
            return true;
        }
        if (Reader->is_value("trail_brightness"))
        {
            Arch->IntensityScale1 = Reader->get_value_float(0);
            return true;
        }
        if (Reader->is_value("flash_size"))
        {
            Arch->RadiusNearAlt = Reader->get_value_float(0);
            return true;
        }
        if (Reader->is_value("head_width"))
        {
            Arch->RadiusTip = Reader->get_value_float(0);
            return true;
        }
        if (Reader->is_value("head_texture"))
        {
            // ball → 1, star/other → 0 (5-char compare).
            char const* Value = Reader->get_value_string();
            if (!Match5(Value, "star") && Match5(Value, "ball"))
                Arch->UvModeV = 1;
            else
                Arch->UvModeV = 0;
            return true;
        }
        if (Reader->is_value("trail_texture"))
        {
            // thin → 1, wide/other → 0.
            char const* Value = Reader->get_value_string();
            if (!Match5(Value, "wide") && Match5(Value, "thin"))
                Arch->UvModeU = 1;
            else
                Arch->UvModeU = 0;
            return true;
        }
        return false;
    }

    FBeamEffectArch* BeamArchFindOrInsert(char const* Nickname)
    {
        // FUN_0042ed30 (thiscall on DAT_00667d78, RET 4): the map's operator[] with FUN_0042e430
        // defaults. An existing entry is returned untouched.
        std::map<std::string, FBeamEffectArch, FAleNameLess>::iterator Found =
            G_BeamArches.find(std::string(Nickname));
        if (Found != G_BeamArches.end())
            return &Found->second;
        FBeamEffectArch Defaults;
        BeamArchInitDefaults(&Defaults);
        std::pair<std::map<std::string, FBeamEffectArch, FAleNameLess>::iterator, bool> Inserted =
            G_BeamArches.insert(
                std::pair<std::string const, FBeamEffectArch>(std::string(Nickname), Defaults));
        return &Inserted.first->second;
    }

    // FUN_0042e850 (cdecl) -- one [BeamSpear] / [BeamBolt] section; both headers reach it from
    // one call in FUN_0040eac0. Returns whether an arch was selected.
    bool LoadBeamSection(INI_Reader* Reader)
    {
        FBeamEffectArch* Current = nullptr;
        bool bHaveValue = Reader->read_value();
        while (bHaveValue)
        {
            if (Reader->is_value("nickname"))
            {
                char* const Nick = StringAlloc(Reader->get_value_string(), false);
                Current = BeamArchFindOrInsert(Nick);
                Current->Nickname = Nick;
                // Any other header leaves +0x04 as it was.
                if (Reader->is_header("BeamSpear"))
                    Current->SegmentMode = 0;
                else if (Reader->is_header("BeamBolt"))
                    Current->SegmentMode = 1;
            }
            else if (Current != nullptr)
            {
                BeamArchParseValue(Current, Reader);
            }
            bHaveValue = Reader->read_value();
        }
        if (Current != nullptr)
            BeamArchClamp(Current);
        return Current != nullptr;
    }

    // FUN_00402180 -- _vsnprintf into a 0x2000-byte buffer; FUN_0040e300 hands the text to
    // INI_Reader::log_link.
    void EffectArchLogLink(INI_Reader* Reader, char const* Format, char const* A, char const* B)
    {
        char Buffer[0x2000];
        _snprintf(Buffer, sizeof(Buffer), Format, A, B);
        Reader->log_link(Buffer);
    }

    // FUN_0040e300 (thiscall on the arch, RET 4) -- one INI value into an open [Effect] arch;
    // true for every key it knows (a failed lookup included), false otherwise.
    bool EffectArchParseValue(FEffectArch* Effect, INI_Reader* Reader)
    {
        if (Reader->is_value("nickname"))
        {
            Effect->NicknameId = CreateID(Reader->get_value_string());
            return true;
        }
        if (Reader->is_value("effect_type"))
        {
            // FUN_004fd500 into +0x00; 0x2f (unknown): log_link("") and an error.
            int const TypeIndex = EffectTypeIndexFromName(Reader->get_value_string());
            Effect->EffectType = TypeIndex;
            if (TypeIndex == kEffectTypeCount)
            {
                Reader->log_link("");
                FDUMP(0x100001, "%s(%d) : ERROR:General:%s", kEffectArchCpp, 0x3a,
                      "unknown effect type");
                return true;
            }
            // FUN_004fd560; no info -> an error naming the index.
            FEffectTypeInfo const* const Info = EffectTypeInfoByIndex(TypeIndex);
            if (Info == nullptr)
            {
                FDUMP(0x100001,
                      "%s(%d) : *** ERROR: EffectTypeInfo missing for EffectType (%d)",
                      kEffectArchCpp, 0x47, Effect->EffectType);
                return true;
            }
            // An unordered run_time clears the bit.
            if (Info->RunTime < 0.0f)
                Effect->Flags50 = static_cast<unsigned char>(Effect->Flags50 | 1u);
            else
                Effect->Flags50 = static_cast<unsigned char>(Effect->Flags50 & ~1u);
            return true;
        }
        // FUN_0042dd20; a hit also copies the VisEffect's nickname into +0x0c.
        if (Reader->is_value("vis_effect"))
        {
            char const* const Name = StringAlloc(Reader->get_value_string(), false);
            Effect->VisEffect = FindVisEffectByNickname(Name);
            if (Effect->VisEffect != nullptr)
                Effect->VisEffectName = Effect->VisEffect->Nickname;
            if (Effect->VisEffect == nullptr)
            {
                EffectArchLogLink(Reader, "[Effect] %s couldn't find [VisEffect] %s.",
                                  EffectArchNickname(Effect), Name);
            }
            return true;
        }
        // A miss reports the same [VisEffect] text (retail jumps back into vis_effect's test).
        if (Reader->is_value("vis_generic"))
        {
            char const* const Name = StringAlloc(Reader->get_value_string(), false);
            Effect->VisGeneric = FindVisEffectByNickname(Name);
            if (Effect->VisGeneric == nullptr)
            {
                EffectArchLogLink(Reader, "[Effect] %s couldn't find [VisEffect] %s.",
                                  EffectArchNickname(Effect), Name);
            }
            return true;
        }
        // FUN_0042e9e0.
        if (Reader->is_value("vis_beam"))
        {
            char const* const Name = StringAlloc(Reader->get_value_string(), false);
            Effect->VisBeam = FindBeamEffectArchByNickname(Name);
            if (Effect->VisBeam == nullptr)
            {
                EffectArchLogLink(Reader, "[Effect] %s couldn't find [VisBeam] %s.",
                                  EffectArchNickname(Effect), Name);
            }
            return true;
        }
        // FUN_0042ae40.
        if (Reader->is_value("snd_effect"))
        {
            char const* const Name = Reader->get_value_string();
            ID_String SoundId;
            SoundId.Id = CreateID(Name);
            Effect->SndEffect = LookupClientSoundArch(&SoundId);
            if (Effect->SndEffect == nullptr)
            {
                char const* const SoundText = SoundId.get_string();
                EffectArchLogLink(Reader, "[Effect] %s couldn't find [Sound] %s.",
                                  EffectArchNickname(Effect), SoundText);
            }
            return true;
        }
        // At most 0x1f characters.
        if (Reader->is_value("lgt_effect"))
        {
            char const* const Name = Reader->get_value_string();
            if (Name == nullptr)
            {
                Effect->LgtEffectLen = 0;
                Effect->LgtEffect[0] = '\0';
                return true;
            }
            size_t Len = strlen(Name);
            if (Len >= 0x20)
                Len = 0x1f;
            memcpy(Effect->LgtEffect, Name, Len);
            Effect->LgtEffect[Len] = '\0';
            Effect->LgtEffectLen = static_cast<unsigned>(Len);
            return true;
        }
        if (Reader->is_value("lgt_range_scale"))
        {
            Effect->LgtRangeScale = Reader->get_value_float(0);
            return true;
        }
        if (Reader->is_value("lgt_time_scale"))
        {
            Effect->LgtTimeScale = Reader->get_value_float(0);
            return true;
        }
        if (Reader->is_value("lgt_radius"))
        {
            Effect->LgtRadius = Reader->get_value_float(0);
            return true;
        }
        // Three ftol'd floats; only the low bytes are kept.
        if (Reader->is_value("lgt_color"))
        {
            Effect->LgtColorR = static_cast<unsigned char>(static_cast<long>(Reader->get_value_float(0)));
            Effect->LgtColorG = static_cast<unsigned char>(static_cast<long>(Reader->get_value_float(1)));
            Effect->LgtColorB = static_cast<unsigned char>(static_cast<long>(Reader->get_value_float(2)));
            return true;
        }
        return false;
    }

    // The four bytes +0x4c..+0x4f as the one dword FUN_0040e6e0 compares and prints.
    unsigned EffectArchColorDword(FEffectArch const* Arch)
    {
        return (static_cast<unsigned>(Arch->LgtColorA) << 24) |
               (static_cast<unsigned>(Arch->LgtColorB) << 16) |
               (static_cast<unsigned>(Arch->LgtColorG) << 8) |
               static_cast<unsigned>(Arch->LgtColorR);
    }

    // The dword +0x50..+0x53 FUN_0040e6e0 compares and copies.
    unsigned EffectArchFlagsDword(FEffectArch const* Arch)
    {
        return static_cast<unsigned>(Arch->Flags50) |
               (static_cast<unsigned>(Arch->Pad51[0]) << 8) |
               (static_cast<unsigned>(Arch->Pad51[1]) << 16) |
               (static_cast<unsigned>(Arch->Pad51[2]) << 24);
    }

    // FUN_0040e6e0 (thiscall on the registered arch, RET 4) -- a reload's copy of an [Effect]
    // merged into the registered one: every field that differs is warned about, then copied.
    void EffectArchMerge(FEffectArch* Existing, FEffectArch const* Incoming)
    {
        // The new VisEffect is named by its ale name, "NULL" without one.
        if (Incoming->VisEffect != Existing->VisEffect)
        {
            char const* const NewName =
                Incoming->VisEffect != nullptr ? Incoming->VisEffect->AlchemyPath : "NULL";
            FDUMP(0x100002, "%s(%d) : *** WARNING: %s: 'vis_effect' changed to %s",
                  kEffectArchCpp, 0x89, EffectArchNickname(Existing), NewName);
            Existing->VisEffect = Incoming->VisEffect;
            Existing->VisEffectName = Incoming->VisEffectName;
        }

        // The pointer is copied; nothing is acquired.
        if (Incoming->SndEffect != Existing->SndEffect)
        {
            char const* const NewName =
                Incoming->SndEffect != nullptr
                    ? static_cast<Archetype::ISound*>(Incoming->SndEffect)->GetFileName()
                    : "NULL";
            FDUMP(0x100002, "%s(%d) : *** WARNING: %s: 'snd_effect' changed to %s",
                  kEffectArchCpp, 0x93, EffectArchNickname(Existing), NewName);
            Existing->SndEffect = Incoming->SndEffect;
        }

        // Retail prints the old light name where the other messages print the arch nickname.
        if (strcmp(Existing->LgtEffect, Incoming->LgtEffect) != 0)
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: %s: 'lgt_effect.name' changed to %s.",
                  kEffectArchCpp, 0x97, Existing->LgtEffect, Incoming->LgtEffect);
            size_t Len = strlen(Incoming->LgtEffect);
            if (Len >= 0x20)
                Len = 0x1f;
            memmove(Existing->LgtEffect, Incoming->LgtEffect, Len);
            Existing->LgtEffect[Len] = '\0';
            Existing->LgtEffectLen = static_cast<unsigned>(Len);
        }

        // An unordered pair counts as changed.
        if (Incoming->LgtRangeScale != Existing->LgtRangeScale)
        {
            FDUMP(0x100002,
                  "%s(%d) : *** WARNING: %.2f: 'lgt_effect.range_scale' changed to %.2f.",
                  kEffectArchCpp, 0x98, static_cast<double>(Existing->LgtRangeScale),
                  static_cast<double>(Incoming->LgtRangeScale));
            Existing->LgtRangeScale = Incoming->LgtRangeScale;
        }
        if (Incoming->LgtTimeScale != Existing->LgtTimeScale)
        {
            FDUMP(0x100002,
                  "%s(%d) : *** WARNING: %.2f: 'lgt_effect.time_scale' changed to %.2f.",
                  kEffectArchCpp, 0x99, static_cast<double>(Existing->LgtTimeScale),
                  static_cast<double>(Incoming->LgtTimeScale));
            Existing->LgtTimeScale = Incoming->LgtTimeScale;
        }

        unsigned const OldColor = EffectArchColorDword(Existing);
        unsigned const NewColor = EffectArchColorDword(Incoming);
        if (NewColor != OldColor)
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: %x: 'lgt_color_value' changed to %x.",
                  kEffectArchCpp, 0x9b, OldColor, NewColor);
            Existing->LgtColorR = Incoming->LgtColorR;
            Existing->LgtColorG = Incoming->LgtColorG;
            Existing->LgtColorB = Incoming->LgtColorB;
            Existing->LgtColorA = Incoming->LgtColorA;
        }

        if (Incoming->LgtRadius != Existing->LgtRadius)
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: %2.f: 'lgt_radius' changed to %2.f.",
                  kEffectArchCpp, 0x9c, static_cast<double>(Existing->LgtRadius),
                  static_cast<double>(Incoming->LgtRadius));
            Existing->LgtRadius = Incoming->LgtRadius;
        }

        unsigned const OldFlags = EffectArchFlagsDword(Existing);
        unsigned const NewFlags = EffectArchFlagsDword(Incoming);
        if (NewFlags != OldFlags)
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: %x: 'flags' changed to %x.", kEffectArchCpp,
                  0x9e, OldFlags, NewFlags);
            Existing->Flags50 = Incoming->Flags50;
            Existing->Pad51[0] = Incoming->Pad51[0];
            Existing->Pad51[1] = Incoming->Pad51[1];
            Existing->Pad51[2] = Incoming->Pad51[2];
        }
    }

    // FUN_0040eac0's [Effect] branch.
    void LoadEffectSection(INI_Reader* Reader, bool bReload)
    {
        // FUN_0040e250 inlined (a failed new leaves a null arch, which the value loop then
        // writes through, as retail).
        FEffectArch* const Arch = static_cast<FEffectArch*>(operator new(sizeof(FEffectArch)));
        if (Arch != nullptr)
            EffectArchConstruct(Arch);

        while (Reader->read_value())
            EffectArchParseValue(Arch, Reader);

        // FUN_0046d6d0.
        std::pair<std::map<unsigned, FEffectArch*>::iterator, bool> const Inserted =
            G_EffectArchs.insert(std::make_pair(Arch->NicknameId, Arch));
        if (Inserted.second)
            return;

        if (bReload)
        {
            EffectArchMerge(Inserted.first->second, Arch);
        }
        else
        {
            FDUMP(0x100002,
                  "%s(%d) : *** WARNING: [Effect] nickname = %s value is not unique. Aborted "
                  "second version.",
                  kEffectArchCpp, 0xfc, EffectArchNickname(Arch));
        }

        // FUN_0040e2a0 inlined.
        if (Arch != nullptr)
        {
            EffectArchDestruct(Arch);
            operator delete(Arch);
        }
    }

    // FUN_0042e290 (cdecl) -- once (DAT_00667e14), with an FxRuntime: IFxRuntime +0x10 and
    // +0x0c get DALib::Alchemy, +0x14 gets DALib::FxEffectLibrary.
    void VisEffectBindFxRuntime()
    {
        if (G_bFxRuntimeBound)
            return;
        G_bFxRuntimeBound = true;
        if (DALib::FxRuntime == nullptr)
            return;
        DALib::FxRuntime->BindUnknown(DALib::Alchemy);
        DALib::FxRuntime->BindAlchemy(DALib::Alchemy);
        DALib::FxRuntime->BindEffectLibrary(DALib::FxEffectLibrary);
    }

    // FUN_0042e2f0 (cdecl) -- DALib::Alchemy vt+0x24 (ClearNodeLibrary) and
    // DALib::FxEffectLibrary vt+0x24 (ClearEffects), each when present; the bind latch drops.
    void VisEffectUnbindFxRuntime()
    {
        if (DALib::Alchemy != nullptr)
            DALib::Alchemy->ClearNodeLibrary();
        if (DALib::FxEffectLibrary != nullptr)
            DALib::FxEffectLibrary->ClearEffects();
        G_bFxRuntimeBound = false;
    }

    // FUN_0042dc40 -- one [VisEffect] section: "nickname" selects the entry (find-or-insert,
    // FUN_0042eaa0 on DAT_00667df0); every other key goes to FUN_0042de30 on the selected
    // entry, and keys before the first nickname are dropped. Returns whether an entry was
    // selected; the one caller drops it.
    bool VisEffectLoadSection(INI_Reader* Reader)
    {
        FVisEffectArch* Entry = nullptr;

        bool bHaveValue = Reader->read_value();
        while (bHaveValue)
        {
            if (Reader->is_value("nickname"))
            {
                // FUN_0042eaa0: find or insert; the nickname is stored either way.
                char const* const Nick = StringAlloc(Reader->get_value_string(), false);
                Entry = &G_VisEffects[std::string(Nick)];
                Entry->Nickname = Nick;
            }
            else if (Entry != nullptr)
            {
                // FUN_0042de30 (thiscall, RET 4): the first matching key wins.
                if (Reader->is_value("effect_crc"))
                {
                    Entry->EffectCrc = static_cast<unsigned long>(Reader->get_value_int(0));
                }
                else if (Reader->is_value("has_alpha"))
                {
                    Entry->bHasAlpha = Reader->get_value_bool(0);
                }
                else if (Reader->is_value("alchemy"))
                {
                    // Deviation: a buffer copy, not a StringAlloc'd pointer.
                    CopyZ(Entry->AlchemyPath, MAX_PATH, Reader->get_value_string());
                }
                else if (Reader->is_value("textures"))
                {
                    char const* const Texture = Reader->get_value_string();
                    Entry->Textures.push_back(std::string(Texture != nullptr ? Texture : ""));
                }
            }
            bHaveValue = Reader->read_value();
        }
        return Entry != nullptr;
    }
}

int EffectArchLoadIni(char const* Path, bool bReload)
{
    // FUN_0040eac0.
    VisEffectBindFxRuntime();

    // The answer is the growth of DAT_006164c0.
    size_t const OldCount = G_EffectArchs.size();

    INI_Reader Reader;
    if (Reader.open(Path, false) && Reader.read_header())
    {
        do
        {
            // The section headers in retail's test order.
            if (Reader.is_header("Effect"))
            {
                LoadEffectSection(&Reader, bReload);
            }
            else if (Reader.is_header("EffectLOD"))
            {
                LoadEffectLodSection(&Reader);  // FUN_0042dae0
            }
            else if (Reader.is_header("EffectType"))
            {
                LoadEffectTypeSection(&Reader); // FUN_004fd5a0
            }
            else if (Reader.is_header("VisEffect"))
            {
                VisEffectLoadSection(&Reader);  // FUN_0042dc40
            }
            else if (Reader.is_header("BeamSpear") || Reader.is_header("BeamBolt"))
            {
                // FUN_0042e850 reads the kind from the header itself.
                LoadBeamSection(&Reader);
            }
        } while (Reader.read_header());
    }

    return static_cast<int>(G_EffectArchs.size() - OldCount);
}

void EffectArchConstruct(FEffectArch* Arch)
{
    // FUN_0040e250 (also inlined in FUN_0040eac0).
    Arch->EffectType = 0;          // +0x00
    Arch->NicknameId = 0;          // +0x04  ID_String()
    Arch->VisEffect = nullptr;     // +0x08
    Arch->VisEffectName = nullptr; // +0x0c  CacheString()
    Arch->VisGeneric = nullptr;    // +0x10
    Arch->VisBeam = nullptr;       // +0x14
    Arch->SndEffect = nullptr;     // +0x18
    Arch->LgtEffectLen = 0;        // +0x1c
    Arch->LgtEffect[0] = '\0';     // +0x20  (a byte store)
    Arch->LgtRangeScale = 1.0f;    // +0x40
    Arch->LgtTimeScale = 1.0f;     // +0x44
    Arch->LgtRadius = 0.0f;        // +0x48
    Arch->LgtColorR = 0xff;        // +0x4c  (one dword store of -1)
    Arch->LgtColorG = 0xff;
    Arch->LgtColorB = 0xff;
    Arch->LgtColorA = 0xff;
    Arch->Flags50 = 0;             // +0x50  (a dword store)
    Arch->Pad51[0] = 0;
    Arch->Pad51[1] = 0;
    Arch->Pad51[2] = 0;
    Arch->RefCount = 0;            // +0x54
}

void EffectArchDestruct(FEffectArch* Arch)
{
    // FUN_0040e2a0 (also inlined in FUN_0040eac0 and FUN_0040ef30).
    int Dangling = Arch->RefCount;
    if (Dangling <= 0)
        return;
    FDUMP(0x100002,
          "%s(%d) : *** WARNING: Destroying effect archetype (%s) with %d dangling references. "
          "See SSTEVENS",
          kEffectArchCpp, 0x25, EffectArchNickname(Arch), Dangling);
    while (Dangling > 0)
    {
        EffectArchRelease(Arch);
        --Dangling;
    }
}

void EffectArchShutdown()
{
    // FUN_0040ef30. The nodes stay until the clear below.
    for (std::map<unsigned, FEffectArch*>::iterator It = G_EffectArchs.begin();
         It != G_EffectArchs.end(); ++It)
    {
        FEffectArch* const Arch = It->second;
        if (Arch == nullptr)
            continue;
        EffectArchDestruct(Arch);
        operator delete(Arch);
    }

    G_EffectArchs.clear();

    // Tail jump to FUN_0042e2f0.
    VisEffectUnbindFxRuntime();
}

FVisEffectArch* FindVisEffectByNickname(char const* Nickname)
{
    // FUN_0042dd20 (cdecl; retail takes the StringAlloc'd CacheString by address). Every caller
    // passes a StringAlloc result, which is never null (an empty text is a valid key).
    std::map<std::string, FVisEffectArch, FAleNameLess>::iterator Found =
        G_VisEffects.find(std::string(Nickname));
    if (Found == G_VisEffects.end())
        return nullptr;
    return &Found->second;
}

FBeamEffectArch* FindBeamEffectArchByNickname(char const* Nickname)
{
    // FUN_0042e9e0 (cdecl, &StringAlloc'd CacheString). The one caller (vis_beam) passes a
    // StringAlloc result.
    std::map<std::string, FBeamEffectArch, FAleNameLess>::iterator Found =
        G_BeamArches.find(std::string(Nickname));
    if (Found == G_BeamArches.end())
        return nullptr;
    return &Found->second;
}

FEffectArch* FindEffectArch(unsigned const* NicknameId)
{
    // FUN_0040f080 (cdecl): FUN_0052c7a0 lower_bound, unsigned key compare.
    std::map<unsigned, FEffectArch*>::iterator const Found = G_EffectArchs.lower_bound(*NicknameId);
    if (Found == G_EffectArchs.end() || *NicknameId < Found->first)
        return nullptr;
    return Found->second;
}

FEffectArch* FindEffectArchByNickname(char const* Nickname)
{
    if (Nickname == nullptr || Nickname[0] == '\0')
        return nullptr;
    unsigned const Id = CreateID(Nickname);
    return FindEffectArch(&Id);
}

void EffectArchAddRef(FEffectArch* Arch, unsigned UnusedArg)
{
    // FUN_0040e960. The VisEffect AddRef's result is not read.
    (void)UnusedArg;
    if (Arch->VisEffect != nullptr)
        VisEffectArchAddRef(Arch->VisEffect);
    if (Arch->SndEffect != nullptr)
        AcquireClientSoundArch(Arch->SndEffect);
    Arch->RefCount = Arch->RefCount + 1;
}

void EffectArchRelease(FEffectArch* Arch)
{
    // FUN_0040e990.
    if (Arch->VisEffect != nullptr)
        VisEffectArchRelease(Arch->VisEffect);
    if (Arch->SndEffect != nullptr)
        ReleaseClientSoundArch(Arch->SndEffect);
    Arch->RefCount = Arch->RefCount - 1;
}

FVisEffectArch::~FVisEffectArch()
{
    // FUN_0042edb0: only a VisEffect still referenced gives anything back.
    if (RefCount > 0)
    {
        VisEffectArchReleaseTextures(this);
        AleLibRelease(AleHandle);
        AleHandle = -1;
    }
}

bool VisEffectArchAddRef(FVisEffectArch* Vis)
{
    // FUN_0042e080. A live reference only counts up (and answers false).
    bool bLoaded = false;
    if (Vis->RefCount > 0)
    {
        Vis->RefCount += 1;
        return bLoaded;
    }

    if (Vis->TextureSlots.size() != Vis->Textures.size())
    {
        VisEffectArchReleaseTextures(Vis);
        // Its answer is not read.
        VisEffectArchLoadTextures(Vis);
    }

    // An empty ale name is an error; the count still goes up.
    if (Vis->AlchemyPath[0] == '\0')
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Client\\cmn\\VisEffectArch.cpp", 0x13a,
              "[VisEffect]: Missing ale file name!");
        Vis->RefCount += 1;
        return bLoaded;
    }

    Vis->AleHandle = AleLibLoad(Vis->AlchemyPath);
    if (Vis->AleHandle != -1)
        bLoaded = true;
    Vis->RefCount += 1;
    return bLoaded;
}

bool VisEffectArchRelease(FVisEffectArch* Vis)
{
    // FUN_0042e160.
    Vis->RefCount -= 1;
    if (Vis->RefCount != 0)
        return false;

    VisEffectArchReleaseTextures(Vis);
    AleLibRelease(Vis->AleHandle);
    Vis->AleHandle = -1;
    return true;
}

int AleLibLoad(char const* AleName)
{
    // FUN_00401160 (cdecl; retail takes the CacheString by address and reads its char*).
    if (AleName[0] == '\0')
        return -1;

    int Handle = -1;
    std::map<std::string, FAleLibEntry, FAleNameLess>::iterator Found =
        G_AleLib.find(std::string(AleName));
    if (Found != G_AleLib.end())
    {
        Found->second.RefCount += 1;
        Handle = Found->second.Handle;
    }
    if (Handle != -1)
        return Handle;

    if (DALib::Alchemy == nullptr)
        return Handle;

    // The DALib::OpenData-shaped FileSystem desc (as FUN_00420dd0 builds it).
    FFileSystemAggDesc Desc;
    memset(&Desc, 0, sizeof(Desc));
    Desc.Size = 0x34;
    Desc.ComponentName = "FileSystem";
    Desc.DataPath = AleName;
    Desc.Access = 0x80000000u;
    Desc.ShareMode = 1;
    Desc.Creation = 3;
    Desc.Flags = 0x8000080u;
    Desc.FindHandle = 0xffffffffu;

    void* FsOut = nullptr;
    if (static_cast<IFileSystem*>(DALib::Data)
            ->CreateInstance(reinterpret_cast<AGGDESC*>(&Desc), &FsOut) == 0)
    {
        IFileSystem* const Filesys = static_cast<IFileSystem*>(FsOut);
        // Neither result is read.
        DALib::Alchemy->LoadNodeLibrary(Filesys);
        DALib::FxEffectLibrary->LoadAle(Filesys);

        Handle = G_AleLibNextHandle;
        G_AleLibNextHandle += 1;
        FAleLibEntry Entry;
        Entry.RefCount = 1;
        Entry.Handle = Handle;
        G_AleLib.insert(std::make_pair(std::string(AleName), Entry));
    }
    else
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: Failed to load %s.",
              "E:\\FL\\Scratch\\Source\\Client\\cmn\\ALELib.cpp", 0x5c, AleName);
    }

    if (FsOut != nullptr)
        static_cast<IFileSystem*>(FsOut)->Release();
    return Handle;
}

int AleLibRelease(int Handle)
{
    // FUN_00401300 (cdecl).
    if (Handle == -1)
        return -1;

    // Linear walk for the entry holding Handle.
    std::map<std::string, FAleLibEntry, FAleNameLess>::iterator It = G_AleLib.begin();
    while (It != G_AleLib.end())
    {
        if (It->second.Handle == Handle)
            break;
        ++It;
    }
    if (It == G_AleLib.end())
        return -1;

    // Nothing is unloaded.
    It->second.RefCount -= 1;
    int const Count = It->second.RefCount;
    if (Count != 0 && Count < 0)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s ref count < 0!",
              "E:\\FL\\Scratch\\Source\\Client\\cmn\\ALELib.cpp", 0x7d, It->first.c_str());
    }
    return Count;
}

int AleLibShutdown()
{
    // FUN_00401380 (cdecl).
    int Dangling = 0;
    std::map<std::string, FAleLibEntry, FAleNameLess>::iterator It = G_AleLib.begin();
    for (; It != G_AleLib.end(); ++It)
    {
        if (It->second.RefCount > 0)
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: ALELib::Shutdown(): %s has %d dangling refs.",
                  "E:\\FL\\Scratch\\Source\\Client\\cmn\\ALELib.cpp", 0x96, It->first.c_str(),
                  It->second.RefCount);
            ++Dangling;
        }
    }

    // The handle counter DAT_006101d0 is left alone.
    G_AleLib.clear();
    return Dangling;
}
