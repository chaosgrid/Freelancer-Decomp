// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "NeuroNet/Economy/infocard_map.h"

#include "common_ini.h"   // INI_Reader
#include "common_misc.h"  // GetFileSysPath

FInfocardMap* G_InfocardMap = nullptr; // DAT_0066d320

void FInfocardMap::Load()
{
    // FUN_004583a0.
    char Path[0x100];
    GetFileSysPath(Path, "interface\\InfocardMap.ini", nullptr);
    INI_Reader Reader;
    if (!Reader.open(Path, false) || !Reader.read_header())
    {
        return;
    }
    do
    {
        if (!Reader.is_header("InfocardMapTable") || !Reader.read_value())
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
            FInfocardMapEntry Entry;
            Entry.ExtraIds = static_cast<int>(Reader.get_value_float(1));
            Entry.bShowEquipment = 1;
            Entry.bShowCommodities = 1;
            Entry.bShowShips = 1;
            if (!Reader.is_value_empty(2))
            {
                Entry.bShowEquipment = static_cast<int>(Reader.get_value_float(2));
            }
            if (!Reader.is_value_empty(3))
            {
                Entry.bShowCommodities = static_cast<int>(Reader.get_value_float(3));
            }
            if (!Reader.is_value_empty(4))
            {
                Entry.bShowShips = static_cast<int>(Reader.get_value_float(4));
            }
            // FUN_00458d00: a key already in the tree keeps its first line.
            Entries.insert(std::pair<unsigned const, FInfocardMapEntry>(Key, Entry));
        } while (Reader.read_value());
    } while (Reader.read_header());
}

void InfocardMapCreate()
{
    // FUN_00458790.
    if (G_InfocardMap != nullptr)
    {
        return;
    }
    FInfocardMap* const Map = new FInfocardMap();
    Map->Load();
    G_InfocardMap = Map;
}

void InfocardMapDestroy()
{
    // FUN_00458860.
    FInfocardMap* const Map = G_InfocardMap;
    if (Map == nullptr)
    {
        return;
    }
    delete Map;
    G_InfocardMap = nullptr;
}

bool InfocardMapFind(unsigned Key, FInfocardMapEntry const** OutEntry)
{
    // FUN_00458890.
    FInfocardMap const* const Map = G_InfocardMap;
    if (Map == nullptr)
    {
        return false;
    }
    std::map<unsigned, FInfocardMapEntry>::const_iterator const It = Map->Entries.find(Key);
    if (It == Map->Entries.end())
    {
        return false;
    }
    *OutEntry = &It->second;
    return true;
}
