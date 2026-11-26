// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "NeuroNet/Economy/knowledge_map.h"

#include "Interface/Pages/character_select_equip.h" // GetPlayerVisitFlagsRetail / SetPlayerVisitFlagsRetail (FUN_004c4da0 / FUN_004c4e00)
#include "Core/globals.h"                // CopyBoundedString (FUN_00417c50)

#include "common_id.h"         // CreateID
#include "common_ini.h"        // INI_Reader
#include "common_reputation.h" // Reputation::get_id / get_name
#include "common_misc.h"       // GetFileSysPath
#include "common_text.h"       // FmtStr
#include "common_tstring.h"    // TString<16>
#include "common_universe.h"   // Universe::get_system / get_base / get_zone

#include <stdint.h>

FKnowledgeMap* G_KnowledgeMap = nullptr; // DAT_0066d334

namespace
{
    // The find every method inlines (FUN_0052c7a0 / FUN_00432240 / FUN_0041bd10 lower_bound,
    // then `end or Key < node key` is a miss).
    FKnowledgeMapEntry const* KnowledgeFind(FKnowledgeMap const* Map, unsigned Key)
    {
        std::map<unsigned, FKnowledgeMapEntry>::const_iterator const It = Map->Entries.find(Key);
        if (It == Map->Entries.end())
        {
            return nullptr;
        }
        return &It->second;
    }

    // One {Id, Flags} line of MarkKnown's walk; both writes notify the server.
    void KnowledgeMarkLine(unsigned Id, unsigned char Flags)
    {
        unsigned char Known = 0;
        if (GetPlayerVisitFlagsRetail(Id, &Known, 0, 0))
        {
            if (static_cast<unsigned char>(Flags & Known) == Flags)
            {
                return;
            }
            SetPlayerVisitFlagsRetail(Id, static_cast<unsigned char>(Known | Flags), true);
            return;
        }
        SetPlayerVisitFlagsRetail(Id, Flags, true);
    }
}

void FKnowledgeMap::Load()
{
    // FUN_00459470.
    char Path[0x100];
    GetFileSysPath(Path, "interface\\KnowledgeMap.ini", nullptr);
    INI_Reader Reader;
    if (!Reader.open(Path, false) || !Reader.read_header())
    {
        return;
    }
    do
    {
        if (!Reader.is_header("KnowledgeMapTable") || !Reader.read_value())
        {
            continue;
        }
        do
        {
            if (!Reader.is_value("map"))
            {
                continue;
            }
            unsigned const Key = static_cast<unsigned>(static_cast<int>(Reader.get_value_float(0)));
            unsigned Id = CreateID(Reader.get_value_string(1));
            unsigned char const Flags =
                static_cast<unsigned char>(static_cast<int>(Reader.get_value_float(2)));
            if ((Flags & 0x40) != 0)
            {
                // A faction (empty name for a missing value).
                char const* const Nickname = Reader.get_value_string(1);
                TString<16> Name;
                Name.Length = 0;
                Name.Data[0] = '\0';
                if (Nickname != nullptr)
                {
                    Name.Length = CopyBoundedString(Name.Data, 0x10, Nickname);
                }
                Id = Reputation::get_id(Name);
            }

            std::map<unsigned, FKnowledgeMapEntry>::iterator const It = Entries.find(Key);
            if (It == Entries.end())
            {
                FKnowledgeMapEntry Entry;
                Entry.Id = Id;
                Entry.Flags = Flags;
                Entries.insert(std::pair<unsigned const, FKnowledgeMapEntry>(Key, Entry));
            }
            else
            {
                FKnowledgeMapLink Link;
                Link.Id = Id;
                Link.Flags = Flags;
                It->second.More.push_front(Link);
            }
        } while (Reader.read_value());
    } while (Reader.read_header());
}

void FKnowledgeMap::MarkKnown(unsigned Key)
{
    // FUN_00459210 (RET 4).
    FKnowledgeMapEntry const* const Entry = KnowledgeFind(this, Key);
    if (Entry == nullptr)
    {
        return;
    }
    KnowledgeMarkLine(Entry->Id, Entry->Flags);
    for (std::forward_list<FKnowledgeMapLink>::const_iterator It = Entry->More.begin();
         It != Entry->More.end(); ++It)
    {
        KnowledgeMarkLine(It->Id, It->Flags);
    }
}

bool FKnowledgeMap::Resolve(FmtStr const* Text, unsigned* OutId, unsigned char* OutFlags)
{
    // FUN_004592b0 (RET 0xc).
    FKnowledgeMapEntry const* Entry = KnowledgeFind(this, Text->MadLibId);
    if (Entry == nullptr)
    {
        for (FmtStr::Val const* Val = Text->Head; Val != nullptr; Val = Val->Next)
        {
            unsigned Key = 0;
            switch (Val->TypeTag)
            {
            case FmtStr::TagString:
                Key = Val->Value;
                // Marked known through DAT_0066d334 (not this) first.
                G_KnowledgeMap->MarkKnown(Key);
                break;
            case FmtStr::TagSystem:
                Key = (unsigned)Universe::get_system(Val->Value)->GetStridName();
                break;
            case FmtStr::TagBase: // BASE vtable +0x8 (strid_name)
                Key = static_cast<unsigned>(Universe::get_base(Val->Value)->StridName);
                break;
            case FmtStr::TagRepGroup:
                Key = Reputation::get_name(Val->Value);
                break;
            default:
                continue;
            }
            Entry = KnowledgeFind(this, Key);
            if (Entry != nullptr)
            {
                break;
            }
        }
        if (Entry == nullptr)
        {
            return false;
        }
    }
    *OutId = Entry->Id;
    *OutFlags = Entry->Flags;
    return true;
}

void KnowledgeMapCreate()
{
    // FUN_00459830.
    if (G_KnowledgeMap != nullptr)
    {
        return;
    }
    FKnowledgeMap* const Map = new FKnowledgeMap();
    Map->Load();
    G_KnowledgeMap = Map;
}

void KnowledgeMapDestroy()
{
    // FUN_00459900.
    FKnowledgeMap* const Map = G_KnowledgeMap;
    if (Map == nullptr)
    {
        return;
    }
    delete Map; // FUN_00459060 + FUN_005b7e1d
    G_KnowledgeMap = nullptr;
}

void KnowledgeMapReveal(unsigned Key)
{
    // FUN_00459930.
    if (G_KnowledgeMap != nullptr)
    {
        G_KnowledgeMap->MarkKnown(Key);
    }
}

void KnowledgeMapRevealFmtStr(FmtStr const* Text)
{
    // FUN_00459950.
    if (G_KnowledgeMap == nullptr)
    {
        return;
    }
    G_KnowledgeMap->MarkKnown(Text->MadLibId);

    // DAT_0066d334 is re-read for every call.
    for (FmtStr::Val const* Val = Text->Head; Val != nullptr; Val = Val->Next)
    {
        switch (Val->TypeTag)
        {
        case FmtStr::TagString:
            G_KnowledgeMap->MarkKnown(Val->Value);
            break;
        case FmtStr::TagSystem:
        {
            unsigned const Key =
                (unsigned)Universe::get_system(Val->Value)->GetStridName();
            G_KnowledgeMap->MarkKnown(Key);
            break;
        }
        case FmtStr::TagBase:
        {
            unsigned const Key =
                static_cast<unsigned>(Universe::get_base(Val->Value)->StridName);
            G_KnowledgeMap->MarkKnown(Key);
            break;
        }
        case FmtStr::TagRepGroup:
        {
            unsigned const Key = Reputation::get_name(Val->Value);
            G_KnowledgeMap->MarkKnown(Key);
            break;
        }
        case FmtStr::TagZoneId: // only a zone that exists
        {
            Universe::IZone const* const Zone = Universe::get_zone(Val->Value);
            if (Zone != nullptr)
            {
                G_KnowledgeMap->MarkKnown(static_cast<unsigned>(Zone->GetName()));
            }
            break;
        }
        case FmtStr::TagFmtStr: // a nested FmtStr
            KnowledgeMapRevealFmtStr(Val->Subtree);
            break;
        default:
            break;
        }
    }
}

bool KnowledgeMapLookupFmtStr(FmtStr const* Text, unsigned* OutId, unsigned char* OutFlags)
{
    // FUN_00459ad0.
    if (G_KnowledgeMap == nullptr)
    {
        return false;
    }
    return G_KnowledgeMap->Resolve(Text, OutId, OutFlags);
}
