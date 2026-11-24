// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Content.dll TU34 (06f00670-06f03fa0): generic bar scripts (scripts\gcs\genericScripts.ini,
// [GenericScripts]) and the per-voice GCS exclusions (scripts\GCS\GCSExcl.ini,
// [GCS_Exclusions]).

#include "Bases/BaseDB.h"     // FBarCharacterList

#include <map>
#include <string>
#include <vector>

struct CharacterBehaviorDescription;   // common_iface.h

// The [GenericScripts] set_segment names: the .data 06fce380 {value, name} table that
// FUN_06f01d70 matches (stricmp); each value is its index. The codes are retail's own.
enum EGenericScriptSegment
{
    GSEG_FIDGET = 0x00,
    GSEG_A01aG = 0x01,
    GSEG_A02aG = 0x02,
    GSEG_A02cG = 0x03,
    GSEG_A02dG = 0x04,
    GSEG_A02eB = 0x05,
    GSEG_A03aG = 0x06,
    GSEG_A04aG = 0x07,
    GSEG_C01aR = 0x08,
    GSEG_D01aN = 0x09,
    GSEG_D01bG = 0x0a,
    GSEG_D01cG = 0x0b,
    GSEG_D01dG = 0x0c,
    GSEG_D01fG = 0x0d,
    GSEG_D01fGm = 0x0e,
    GSEG_D01hG = 0x0f,
    GSEG_D01kG = 0x10,
    GSEG_D01kGm = 0x11,
    GSEG_D02aP = 0x12,
    GSEG_D02aPm = 0x13,
    GSEG_D02bP = 0x14,
    GSEG_D02dP = 0x15,
    GSEG_D02eP = 0x16,
    GSEG_D03aN = 0x17,
    GSEG_D04aG = 0x18,
    GSEG_D04bB = 0x19,
    GSEG_D05aP = 0x1a,
    GSEG_D05bP = 0x1b,
    GSEG_D06aG = 0x1c,
    GSEG_D07aG = 0x1d,
    GSEG_D07bG = 0x1e,
    GSEG_F01aB = 0x1f,
    GSEG_F01bB = 0x20,
    GSEG_F01dG = 0x21,
    GSEG_F01eG = 0x22,
    GSEG_F01fG = 0x23,
    GSEG_F01jG = 0x24,
    GSEG_F02aP = 0x25,
    GSEG_F03aR = 0x26,
    GSEG_F03bR = 0x27,
    GSEG_F03cR = 0x28,
    GSEG_F03dR = 0x29,
    GSEG_F04aG = 0x2a,
    GSEG_F04dG = 0x2b,
    GSEG_F04fG = 0x2c,
    GSEG_F04gP = 0x2d,
    GSEG_F05aG = 0x2e,
    GSEG_F05bG = 0x2f,
    GSEG_F05cG = 0x30,
    GSEG_F06aP = 0x31,
    GSEG_F07aP = 0x32,
    GSEG_F07bP = 0x33,
    GSEG_F07cP = 0x34,
    GSEG_F08dR = 0x35,
    GSEG_F08bR = 0x36,
    GSEG_F08cR = 0x37,
    GSEG_F09bP = 0x38,
    GSEG_F10aR = 0x39,
    GSEG_F10bR = 0x3a,
    GSEG_F10bRm = 0x3b,
    GSEG_W01cG = 0x3c,
    GSEG_W01bB = 0x3d,
    GSEG_W02aF = 0x3e,
    GSEG_W03bB = 0x3f,
    GSEG_W04bP = 0x40,
    GSEG_W04cP = 0x41,
    GSEG_COUNT = 0x42,   // also the loader's "no segment" default
};

// DAT_06fcf478: 0x108 vectors of script names, index Posture + 2 * (Gender + 2 * Segment)
// (0x42 segments x 2 genders x 2 postures). Constructed by LAB_06f01c00 (eh vector ctor,
// element ctor LAB_06f01c50), destroyed by the atexit FUN_06f01c30.
extern std::vector<std::string> GGenericScripts[0x108];

// DAT_06fd04f8: voice id -> script names that voice must not play. Constructed by
// LAB_06f01af0, destroyed by the atexit FUN_06f01b20.
extern std::map<unsigned int, std::vector<std::string>> GGcsExclusions;

// FUN_06f01d70: fill GGenericScripts from the given ini (BaseSupport ctor).
void __cdecl LoadGenericScripts(char const* Path);
// FUN_06f01c70: clear every GGenericScripts vector (BaseSupport dtor).
void __cdecl UnloadGenericScripts();
// FUN_06f02330: fill GGcsExclusions from the given ini (LoadContentGameTables).
void __cdecl LoadGcsExclusions(char const* Path);

// FUN_06f020c0: a random script of the (Segment, Gender, Posture) list that VoiceId is not
// excluded from; null when there is none. Gender 1 selects the "male" lists, Posture 0 the
// "stand" lists.
char const* __cdecl PickGenericScript(int Segment, int Gender, int Posture, unsigned int VoiceId);
// FUN_06f020a0: PickGenericScript(GSEG_FIDGET, Gender, Posture, 0).
char const* __cdecl PickFidgetScript(int Gender, int Posture);

// FUN_06f01ab0 (thiscall on the description, RET 8; callers FUN_06f00b90, inlined seven
// times in the bar scene creator FUN_06ea9640): Text is appended to the second string array
// with Value when it is non-null and non-empty. No bound check (32 slots), as in retail.
void AppendBehaviorText(CharacterBehaviorDescription* Behavior, char const* Text,
                        unsigned int Value);

// FUN_06f00b60 (cdecl): a character on a placement gets the behavior built by FUN_06f00b90.
void __cdecl StartBarCharacterScript(FBarCharacterList::iterator Character);
