// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Common.dll goods family -- helpers shared by several goods TUs. Common-internal: nothing here
// is exported.

#include "common_misc.h"       // malloc_strcpy
#include "common_reputation.h" // Costume

namespace CmnGoods
{
    // Costume's inline operator=, as retail expands it wherever a Costume is assigned
    // (CostumeHint::operator=, CharacterProperties::init / operator= / unmarshal): only `count`
    // accessory ids are copied (signed loop). Retail quirk kept: no bound, so a count above 8
    // writes past AccessoryIds; ids past the count keep their old values.
    inline void AssignCostume(Costume& Dest, Costume const& Src)
    {
        Dest.PartIds[0] = Src.PartIds[0];
        Dest.PartIds[1] = Src.PartIds[1];
        Dest.PartIds[2] = Src.PartIds[2];
        Dest.PartIds[3] = Src.PartIds[3];
        Dest.AccessoryCount = Src.AccessoryCount;
        for (int Index = 0; Index < (int)Dest.AccessoryCount; ++Index)
            Dest.AccessoryIds[Index] = Src.AccessoryIds[Index];
    }

    const unsigned kPathChars = 0x104;

    // Every room / setpoint / placement / hotspot / character-property string is written in
    // retail with malloc_strcpy(&Field, Text) and released with malloc_strfree, never
    // StringAlloc. malloc_strcpy (06334400) is _strdup: a null source gives null, "" a fresh "".
    // It never frees the previous value.
    inline char const* MallocCopy(char const* Text)
    {
        char* Copy = nullptr;
        malloc_strcpy(&Copy, Text);
        return Copy;
    }
}
