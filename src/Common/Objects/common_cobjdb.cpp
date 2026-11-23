// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_cobjdb.h"

#include "common_archetype.h"
#include "common_id.h"
#include "common_ini.h"
#include "common_reputation.h"
#include "common_text.h"
#include "common_universe.h"

#include <cstring>

// ============================================================================
// SolarReps. LoadUniverseIni runs this pass over each system file before the Zone/Object
// parse; it seeds a Reputation::Vibe for every [Object] nickname so CSolar::ReadObj's
// Vibe::Verify gate can pass.
// ============================================================================

namespace
{
    // FUN_062b8ab0 (cdecl). Returns the vibe handle, which is the nickname id; Vibe::Alloc does
    // not write it back.
    int AllocObjectVibe(ID_String const* Nick, unsigned IdsName, TString<16> const* Group,
                        unsigned Infocard, Archetype::Solar const* Arch)
    {
        int Handle = (int)Nick->Id;
        Reputation::Vibe::Alloc(Handle);

        // The false is an EBX push left on the stack after get_id's call.
        Reputation::Vibe::SetAffiliation(Handle, Reputation::get_id(*Group), false);

        // begin_mad_lib is the folded setter 06309b30, end_mad_lib the empty 062a8920.
        FmtStr Name1;
        FmtStr Name2;
        Name1.begin_mad_lib(IdsName);
        Name1.end_mad_lib();
        Reputation::Vibe::SetName(Handle, Name1, Name2, nullptr);

        Reputation::Vibe::SetInfocard(Handle, Infocard);

        if (Arch != nullptr)
            Reputation::Vibe::SetRank(Handle, (unsigned)Arch->Toughness);

        return Handle;
    }

    // FUN_062b8c20. One [Object] section.
    int LoadOneObjectSection(INI_Reader& Reader)
    {
        ID_String Nick;
        Nick.clear();
        TString<16> Group;
        Group.Length = 0;
        Group.Data[0] = '\0';
        unsigned IdsName = 0;
        unsigned Infocard = 0;
        Archetype::Solar const* Arch = nullptr;

        if (Reader.read_value())
        {
            do
            {
                // Retail quirk kept: reading stops once a name, a nickname and a group are
                // known, so later keys (infocard, archetype) are skipped.
                if (IdsName != 0 && !Nick.IsEmpty() && Group.Length != 0)
                    break;

                if (Reader.is_value("nickname"))
                {
                    Nick.Id = CreateID(Reader.get_value_string());
                }
                else if (Reader.is_value("ids_name"))
                {
                    IdsName = (unsigned)Reader.get_value_int(0);
                }
                else if (Reader.is_value("infocard_ids") || Reader.is_value("ids_info"))
                {
                    Infocard = (unsigned)Reader.get_value_int(0);
                }
                else if (Reader.is_value("group"))
                {
                    char const* Text = Reader.get_value_string();
                    if (Text == nullptr)
                    {
                        Group.Length = 0;
                        Group.Data[0] = '\0';
                    }
                    else
                    {
                        unsigned Len = (unsigned)std::strlen(Text);
                        if (Len >= 16u)
                            Len = 15u;
                        std::memcpy(Group.Data, Text, Len);
                        Group.Data[Len] = '\0';
                        Group.Length = Len;
                    }
                }
                else if (Reader.is_value("archetype"))
                {
                    ID_String ArchName;
                    ArchName.Id = CreateID(Reader.get_value_string());
                    Arch = Archetype::GetSolar(Archetype::GetAsteroidID(ArchName));
                }
            } while (Reader.read_value());
        }

        if (Nick.IsEmpty())
            return -1;
        return AllocObjectVibe(&Nick, IdsName, &Group, Infocard, Arch);
    }
}

int __cdecl SolarReps::FindSolarRep(ID_String const& Id)
{
    // FUN_062b8a80
    if (Reputation::Vibe::Verify((int)Id.Id))
        return (int)Id.Id;
    return -1;
}

void __cdecl Reputation::SolarReps::LoadObjectVibesFromSystemIni(INI_Reader& Reader)
{
    // FUN_062b8de0
    while (Reader.read_header())
    {
        if (Reader.is_header("Object"))
            LoadOneObjectSection(Reader);
    }
}
