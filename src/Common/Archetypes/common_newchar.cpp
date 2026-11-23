// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_newchar.h"

#include "common_ini.h"

extern "C"
{
    __declspec(dllimport) extern void(__cdecl* FDUMP)(int flags, const char* fmt, ...);
}

namespace
{
    const char kNewCharSource[] = "E:\\FL\\Scratch\\Source\\Common\\NewCharDB.cpp";

    NewChar::Database GNewCharDatabase;
}

NewChar::IDatabase* NewChar::TheDB = &GNewCharDatabase;

NewChar::Database::Database()
{
}

NewChar::Database::~Database()
{
    Shutdown();
}

bool NewChar::Database::Load(char const* Path)
{
    // Retail LAB_063149d0 (slot 14). Every section is parsed inline into a stack record that is
    // handed to its insert slot through the vtable (slots 6 / 7 / 8, and slot 9 for each
    // faction base); the insert results are not read.
    INI_Reader Reader;
    if (!Reader.open(Path, false))
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: failed to open configuration file (%s)\n",
              kNewCharSource, 0x1cd, Path);
        return false;
    }

    // Retail quirk kept: the [Package] record's StridName / StridDesc / Money stack slots are
    // never initialised, so a [Package] missing those keys keeps the previous [Package]'s
    // values. Deviation: the first one starts from 0 instead of stack garbage.
    int PackageStridName = 0;
    int PackageStridDesc = 0;
    int PackageMoney = 0;

    while (Reader.read_header())
    {
        if (Reader.is_header("Faction"))
        {
            FNewCharFaction Faction;
            Faction.NicknameId = 0;
            while (Reader.read_value())
            {
                if (Reader.is_value("nickname"))
                {
                    Faction.NicknameId = CreateID(Reader.get_value_string());
                }
                else if (Reader.is_value("rep_group"))
                {
                    Faction.RepGroup.assign(Reader.get_value_string());
                }
                else if (Reader.is_value("base"))
                {
                    // A {CreateID(base), base} record through slot 9; Server.dll's FUN_06d4b790
                    // finds it by the base id the client sends and writes its string as
                    // %%Home_Base%%.
                    FNewCharExtra Extra;
                    Extra.Id = CreateID(Reader.get_value_string());
                    Extra.Value.assign(Reader.get_value_string());
                    InsertExtra(Extra);
                    Faction.Bases.insert(Extra.Id);
                }
                else if (Reader.is_value("package"))
                {
                    Faction.Packages.insert(CreateID(Reader.get_value_string()));
                }
                else if (Reader.is_value("pilot"))
                {
                    Faction.Pilots.insert(CreateID(Reader.get_value_string()));
                }
            }
            InsertFaction(Faction);
        }
        else if (Reader.is_header("Package"))
        {
            FNewCharPackage Package;
            Package.NicknameId = 0;
            Package.StridName = PackageStridName;
            Package.StridDesc = PackageStridDesc;
            Package.Money = PackageMoney;
            while (Reader.read_value())
            {
                if (Reader.is_value("nickname"))
                {
                    Package.NicknameId = CreateID(Reader.get_value_string());
                }
                else if (Reader.is_value("strid_name"))
                {
                    // ftol of the float value.
                    Package.StridName = (int)Reader.get_value_float(0);
                }
                else if (Reader.is_value("strid_desc"))
                {
                    Package.StridDesc = (int)Reader.get_value_float(0);
                }
                else if (Reader.is_value("ship"))
                {
                    Package.Ship.assign(Reader.get_value_string());
                }
                else if (Reader.is_value("loadout"))
                {
                    Package.Loadout.assign(Reader.get_value_string());
                }
                else if (Reader.is_value("money"))
                {
                    Package.Money = (int)Reader.get_value_float(0);
                }
            }
            PackageStridName = Package.StridName;
            PackageStridDesc = Package.StridDesc;
            PackageMoney = Package.Money;
            InsertPackage(Package);
        }
        else if (Reader.is_header("Pilot"))
        {
            FNewCharPilot Pilot;
            Pilot.NicknameId = 0;
            while (Reader.read_value())
            {
                if (Reader.is_value("nickname"))
                {
                    Pilot.NicknameId = CreateID(Reader.get_value_string());
                }
                else if (Reader.is_value("body"))
                {
                    Pilot.Body.assign(Reader.get_value_string());
                }
                else if (Reader.is_value("body.anim"))
                {
                    // The vector is emptied, then every parameter up to the first empty one is
                    // appended.
                    Pilot.BodyAnims.clear();
                    unsigned ParamIndex = 0;
                    while (!Reader.is_value_empty(ParamIndex))
                    {
                        std::string const Anim(Reader.get_value_string(ParamIndex));
                        ParamIndex += 1;
                        Pilot.BodyAnims.push_back(Anim);
                    }
                }
                else if (Reader.is_value("comm"))
                {
                    Pilot.Comm.assign(Reader.get_value_string());
                }
                else if (Reader.is_value("comm.anim"))
                {
                    // As body.anim.
                    Pilot.CommAnims.clear();
                    unsigned ParamIndex = 0;
                    while (!Reader.is_value_empty(ParamIndex))
                    {
                        std::string const Anim(Reader.get_value_string(ParamIndex));
                        ParamIndex += 1;
                        Pilot.CommAnims.push_back(Anim);
                    }
                }
                else if (Reader.is_value("voice"))
                {
                    Pilot.Voice.assign(Reader.get_value_string());
                }
                else if (Reader.is_value("thumb"))
                {
                    Pilot.Thumb.assign(Reader.get_value_string());
                }
            }
            InsertPilot(Pilot);
        }
    }

    return true;
}

bool NewChar::Database::Slot15(void* Arg)
{
    // Retail slot 15 (+0x3c), ICF-folded onto 0x0629ba10: always false, argument ignored. The
    // slot's meaning is unknown (the symbol on that address, CEShield::GetEquipDesc, belongs
    // to an unrelated class).
    // TODO: identify slot 15 from a caller outside Common.dll.
    (void)Arg;
    return false;
}

void NewChar::Database::Shutdown()
{
    // Retail LAB_06313fd0, in this order (+0x04, +0x18, +0x2c, +0x40).
    Factions.clear();
    Packages.clear();
    Pilots.clear();
    Extras.clear();
}

FNewCharPilot const* NewChar::Database::FindPilot(unsigned NicknameId) const
{
    std::map<unsigned, FNewCharPilot>::const_iterator It = Pilots.find(NicknameId);
    if (It == Pilots.end())
        return nullptr;
    return &It->second;
}

FNewCharPackage const* NewChar::Database::FindPackage(unsigned NicknameId) const
{
    std::map<unsigned, FNewCharPackage>::const_iterator It = Packages.find(NicknameId);
    if (It == Packages.end())
        return nullptr;
    return &It->second;
}

FNewCharFaction const* NewChar::Database::FindFaction(unsigned NicknameId) const
{
    std::map<unsigned, FNewCharFaction>::const_iterator It = Factions.find(NicknameId);
    if (It == Factions.end())
        return nullptr;
    return &It->second;
}

bool NewChar::Database::ForEachFaction(bool(__cdecl* Callback)(unsigned, FNewCharFaction const*, void*), void* Context) const
{
    // Retail LAB_06313f50 (slot 4): in-order walk of the Factions tree (+0x04), ascending key
    // order as retail's RB-tree walk. No null test on Callback, as in retail.
    for (std::map<unsigned, FNewCharFaction>::const_iterator It = Factions.begin(); It != Factions.end(); ++It)
    {
        if (!Callback(It->first, &It->second, Context))
            return false;
    }
    return true;
}

bool NewChar::Database::RemovePilot(unsigned NicknameId)
{
    // FUN_06314910 (slot 12).
    return Pilots.erase(NicknameId) != 0;
}

bool NewChar::Database::RemovePackage(unsigned NicknameId)
{
    // FUN_063148b0 (slot 11).
    return Packages.erase(NicknameId) != 0;
}

bool NewChar::Database::RemoveFaction(unsigned NicknameId)
{
    // LAB_06314850 (slot 10).
    return Factions.erase(NicknameId) != 0;
}

bool NewChar::Database::InsertExtra(FNewCharExtra const& Extra)
{
    // LAB_063146c0 (slot 9): false when the key exists, else the node is made (FUN_06316330).
    if (Extras.find(Extra.Id) != Extras.end())
        return false;
    FNewCharExtra& Record = Extras[Extra.Id];
    Record.Id = Extra.Id;
    Record.Value = Extra.Value;
    return true;
}

bool NewChar::Database::InsertFaction(FNewCharFaction const& Faction)
{
    // LAB_06314100 (slot 6): false when the key exists.
    if (Factions.find(Faction.NicknameId) != Factions.end())
        return false;
    Factions[Faction.NicknameId] = Faction;
    return true;
}

bool NewChar::Database::InsertPackage(FNewCharPackage const& Package)
{
    // LAB_06314280 (slot 7): false when the key exists, else FUN_06315f90 makes the node.
    if (Packages.find(Package.NicknameId) != Packages.end())
        return false;
    Packages[Package.NicknameId] = Package;
    return true;
}

bool NewChar::Database::InsertPilot(FNewCharPilot const& Pilot)
{
    // LAB_063144c0 (slot 8): false when the key exists, else FUN_06316110 makes the node.
    if (Pilots.find(Pilot.NicknameId) != Pilots.end())
        return false;
    Pilots[Pilot.NicknameId] = Pilot;
    return true;
}

bool NewChar::Database::RemoveExtra(unsigned Id)
{
    // FUN_06314970 (slot 13).
    return Extras.erase(Id) != 0;
}

FNewCharExtra const* NewChar::Database::FindExtra(unsigned Id) const
{
    // LAB_06313f00 (slot 3).
    std::map<unsigned, FNewCharExtra>::const_iterator It = Extras.find(Id);
    if (It == Extras.end())
        return nullptr;
    return &It->second;
}
