// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_reputation.h"
#include "Universe/common_reputation_internal.h"
#include "msvcrt_compat.h" // MsvcrtAtoi: retail MSVCRT.DLL wraps mod 2^32, the UCRT saturates

#include "common_ini.h"
#include "common_misc.h"
#include "dacom.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "x87_trig.h" // X87Tan (DetailSwitchTable::adjust_distance FPTAN)

namespace
{
    const char kCharDescripSource[] = "E:\\FL\\Scratch\\Source\\Common\\CharDescrip.cpp";
    const unsigned kBodypartTypeCount = 4;
    const unsigned kMaxAccessories = 8;

    char const* const kBodypartTypeNames[kBodypartTypeCount] = {
        "Body",
        "Head",
        "LeftHand",
        "RightHand",
    };

    CostumeDescriptions* GCostumeDescriptions = nullptr;

    // FUN_062feba0 and its inlined copies (062ff83a, 062ff91b, 062ff9fb, 062ffa79, 06300d01,
    // 06300d3d, 06300e8c): atol(Name), CreateID(Name) when that is 0. No null test.
    unsigned NicknameToCreateId(char const* Name)
    {
        long Parsed = MsvcrtAtoi(Name);
        if (Parsed == 0)
            return CreateID(Name);
        return (unsigned)Parsed;
    }

    // The case-insensitive scan retail inlines three times in load_bodyparts ([Animations]
    // anim / preload, [PetalAnimations] anim), as DACOM stricmp(value, entry). A hit means
    // "already present, skip".
    bool ListContainsI(std::list<std::string> const& List, char const* Name)
    {
        std::list<std::string>::const_iterator It = List.begin();
        for (; It != List.end(); ++It)
        {
            if (CallDacomStricmp(Name, It->c_str()) == 0)
                return true;
        }
        return false;
    }

    // The {int Id; char Name[0x100]} table at .data:063ecd68, scanned with DACOM
    // stricmp(entry, Name). Id == index, so retail's `Id == -1` tests never fire; -1 here means
    // "no entry matched".
    int BodypartTypeFromName(char const* Name)
    {
        unsigned Index = 0;
        for (; Index < kBodypartTypeCount; ++Index)
        {
            if (CallDacomStricmp(kBodypartTypeNames[Index], Name) == 0)
                return (int)Index;
        }
        return -1;
    }

    CostumeDescriptions* EnsureCostumeDescriptions()
    {
        if (GCostumeDescriptions == nullptr)
            GCostumeDescriptions = new (std::nothrow) CostumeDescriptions();
        return GCostumeDescriptions;
    }

    void FillCostume(costume const* Desc, Costume& Out)
    {
        // Costume order is head/body/left/right; the record order is body/head/left/right.
        const unsigned TypeIds[4] = {1, 0, 2, 3};
        unsigned Index = 0;
        for (; Index < 4; ++Index)
        {
            bodypart const* const Part = Desc != nullptr ? Desc->Parts[TypeIds[Index]] : nullptr;
            Out.PartIds[Index] = Part != nullptr ? Part->Id : 0;
        }

        Out.AccessoryCount = Desc != nullptr ? Desc->AccessoryCount : 0;
        // Retail leaves the unused accessory slots untouched, including on a missing costume.
        for (Index = 0; Index < Out.AccessoryCount; ++Index)
            Out.AccessoryIds[Index] = Desc->Accessories[Index]->Id;
    }
}

// .data:063fcb48: the one scratch buffer bodypart::get_name and accessory::get_name (a single
// ICF-folded body) format the id into.
static char GCostumeNameBuffer[0x100];

bodypart::bodypart()
    : Id(0)
    , Sex(0)
    , Switch(nullptr)
    , PetalAnimations(nullptr)
{
    // FUN_062ff030
}

void bodypart::init(char const* Nickname, char const* InMesh, int InSex,
                    std::list<std::string>* InPetalAnimations, DetailSwitchTable* InSwitch)
{
    // FUN_062ff060. No null checks.
    Id = (unsigned long)MsvcrtAtoi(Nickname);
    if (Id == 0)
        Id = CreateID(Nickname);
    Mesh = InMesh;
    Sex = InSex;
    PetalAnimations = InPetalAnimations;
    Switch = InSwitch;
}

unsigned long bodypart::get_name_crc() const
{
    // FUN_06261d90
    return Id;
}

char const* bodypart::get_name() const
{
    // FUN_062ff140
    _ultoa(Id, GCostumeNameBuffer, 10);
    return GCostumeNameBuffer;
}

char const* bodypart::get_mesh() const
{
    // FUN_062b68f0
    return Mesh.c_str();
}

int bodypart::get_gender() const
{
    // FUN_06263bb0 (+0x14)
    return Sex;
}

DetailSwitchTable* bodypart::get_switch_table() const
{
    // FUN_062a0160 (+0x18)
    return Switch;
}

std::list<std::string>* bodypart::get_petal_anims() const
{
    // FUN_062629e0 (+0x1c)
    return PetalAnimations;
}

accessory::accessory()
    : Id(0)
{
    // FUN_062ff160
}

void accessory::init(char const* Nickname, char const* InMesh, char const* InAccessoryHardpoint,
                     char const* InCharacterHardpoint, std::list<std::string> const& InMaterials)
{
    // FUN_062ff2f0
    Id = (unsigned long)MsvcrtAtoi(Nickname);
    if (Id == 0)
        Id = CreateID(Nickname);
    Mesh = InMesh;
    AccessoryHardpoint = InAccessoryHardpoint;
    CharacterHardpoint = InCharacterHardpoint;
    if (&Materials != &InMaterials)
        Materials = InMaterials;
}

unsigned long accessory::get_name_crc() const
{
    // FUN_06261d90
    return Id;
}

char const* accessory::get_name() const
{
    // FUN_062ff140 (shared with bodypart::get_name)
    _ultoa(Id, GCostumeNameBuffer, 10);
    return GCostumeNameBuffer;
}

char const* accessory::get_mesh() const
{
    // FUN_062b68f0 (+0x08)
    return Mesh.c_str();
}

char const* accessory::get_accessory_hardpoint() const
{
    // FUN_062b6900 (+0x18)
    return AccessoryHardpoint.c_str();
}

char const* accessory::get_character_hardpoint() const
{
    // FUN_062b6910 (+0x28)
    return CharacterHardpoint.c_str();
}

std::list<std::string> const& accessory::get_materials() const
{
    // FUN_06295a30 (+0x34)
    return Materials;
}

CostumeDescriptions::CostumeDescriptions()
{
}

void CostumeDescriptions::warn(INI_Reader* Reader)
{
    // FUN_062ed730, an empty body ICF-shared with many others.
    (void)Reader;
}

CostumeDescriptions::~CostumeDescriptions()
{
    // FUN_062ff600: only the member destructors (costume map +0x94, accessory map +0x80, the
    // four bodypart maps +0x30, the lists at +0x24 / +0x18 / +0x0c / +0x00).
}

void DetailSwitchTable::add_table_entry(float Key, float Value)
{
    Table.emplace_back(Key, Value);
}

// FUN_063011b0, vector<pair<float,float>>::reserve (unexported; a friend, not a member).
void DetailSwitchTableReserve(DetailSwitchTable& Target, std::size_t Count)
{
    Target.Table.reserve(Count);
}

// FUN_062fecb0. Keys run in descending order. Retail quirk kept: no empty-table test, so an
// empty table faults on its null begin pointer (data() avoids debug-iterator checks). Returns
// double because retail leaves the result unrounded in ST0 and freelancer.exe (004450d3)
// compares it before any store.
double DetailSwitchTable::get_switch_distance(float X) const
{
    std::pair<float, float> const* const Rows = Table.data();
    int const Count = (int)Table.size();

    // All compares are ordered: NaN falls through to the default.
    if (X >= Rows[0].first)
        return Rows[0].second;

    if (X <= Rows[Count - 1].first)
        return Rows[Count - 1].second;

    for (int Index = 0; Index < Count - 1; ++Index)
    {
        if (!(Rows[Index].first >= X))
            continue;
        if (!(Rows[Index + 1].first <= X))
            continue;

        double const KeyA = Rows[Index].first;
        double const ValueB = Rows[Index].second;
        double const ValueC = Rows[Index + 1].second;
        double const KeyD = Rows[Index + 1].first;
        return (((double)X - KeyA) * (ValueC - ValueB)) / (KeyD - KeyA) + ValueB;
    }
    // The 1.0f (DAT_0639a1d0) loaded first is still in ST0.
    return 1.0;
}

float DetailSwitchTable::adjust_distance(float Distance, float AngleDeg, float /*Unused*/) const
{
    // FUN_062fed60. All on the x87 stack; DegToRad is DAT_063a22fc, FovXHalf +0x10. The only
    // caller (freelancer.exe 00445078) stores the result as a float at once.
    double const DegToRad = (double)0.017453277f;
    double const TanAngle = X87Tan((double)AngleDeg * DegToRad);
    double const TanFov = X87Tan((double)FovXHalf * DegToRad);
    return (float)((TanAngle / TanFov) * (double)Distance);
}

void DetailSwitchTable::read_from_ini(INI_Reader* Reader)
{
    while (Reader->read_value())
    {
        if (Reader->is_value("switch"))
        {
            float Key = Reader->get_value_float(0);
            float Value = Reader->get_value_float(1);
            add_table_entry(Key, Value);
        }
        else if (Reader->is_value("fovx"))
        {
            FovXHalf = Reader->get_value_float(0) * 0.5f;
        }
        else if (Reader->is_value("sizex"))
        {
            SizeX = Reader->get_value_float(0);
        }
    }
}

// FUN_062ffc50 (signature from the mangled export).
void CostumeDescriptions::load_bodypart(int TypeId, int Sex, DetailSwitchTable* Switch,
                                        INI_Reader* Reader)
{
    std::string Nickname;
    std::string Mesh;

    while (Reader->read_value())
    {
        if (Reader->is_value("nickname"))
        {
            Nickname = Reader->get_value_string();
        }
        else if (Reader->is_value("mesh"))
        {
            Mesh = Reader->get_value_string();
        }
        else if (Reader->is_value("multilevel"))
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kCharDescripSource, 0x2c9,
                  "multilevel is obsolete");
        }
    }

    if (Nickname.empty() || Mesh.empty())
        return;

    unsigned Id = NicknameToCreateId(Nickname.c_str());

    // The petal-animation list is the most recently registered one (+0x18 list), not a
    // parameter.
    std::list<std::string>* PetalAnims = nullptr;
    if (!PetalAnimations.empty())
        PetalAnims = &PetalAnimations.back();

    // map::insert (FUN_06301430) keeps an existing entry with the same id.
    bodypart Part;
    Part.init(Nickname.c_str(), Mesh.c_str(), Sex, PetalAnims, Switch);
    Bodyparts[TypeId].insert(std::map<unsigned, bodypart>::value_type(Id, Part));
}

void CostumeDescriptions::load_accessory(INI_Reader* Reader)
{
    std::string Nickname;
    std::string Mesh;
    std::string Hardpoint;
    std::string BodyHardpoint;
    std::list<std::string> MaterialLibrary;

    while (Reader->read_value())
    {
        if (Reader->is_value("nickname"))
            Nickname = Reader->get_value_string();
        else if (Reader->is_value("mesh"))
            Mesh = Reader->get_value_string();
        else if (Reader->is_value("hardpoint"))
            Hardpoint = Reader->get_value_string();
        else if (Reader->is_value("body_hardpoint"))
            BodyHardpoint = Reader->get_value_string();
        else if (Reader->is_value("material_library"))
            // Repeated lines accumulate.
            MaterialLibrary.push_back(std::string(Reader->get_value_string()));
    }

    // Retail quirk kept: no nickname test; such an accessory goes in under CreateID("").
    unsigned Id = NicknameToCreateId(Nickname.c_str());

    // Retail fills a local field by field rather than through init; map::insert (+0x80) keeps
    // an existing entry with that id.
    accessory Acc;
    Acc.Id = Id;
    Acc.Mesh = Mesh;
    Acc.AccessoryHardpoint = Hardpoint;
    Acc.CharacterHardpoint = BodyHardpoint;
    Acc.Materials = std::move(MaterialLibrary);
    Accessories.insert(std::map<unsigned, accessory>::value_type(Id, Acc));
}

void CostumeDescriptions::load_bodyparts(char const* Path)
{
    // No null test on Path.
    INI_Reader Reader;
    if (!Reader.open(Path, false))
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: Cannot open bodyparts ini file: %s",
              kCharDescripSource, 0x3b0, Path);
        return;
    }

    // The [DetailSwitchTable] pointer is sticky across sections; the sex is too, except that
    // every [Skeleton] header resets it to 0 first.
    int CurrentSex = 0;
    DetailSwitchTable* CurrentSwitch = nullptr;

    while (Reader.read_header())
    {
        if (Reader.is_header("Skeleton"))
        {
            CurrentSex = 0;
            while (Reader.read_value())
            {
                if (Reader.is_value("nomotion"))
                {
                    FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kCharDescripSource, 0x33b,
                          "nomotion is obsolete");
                }
                else if (Reader.is_value("sex"))
                {
                    char const* Sex = Reader.get_value_string();
                    if (CallDacomStricmp(Sex, "none") == 0)
                        CurrentSex = 0;
                    else if (CallDacomStricmp(Sex, "male") == 0)
                        CurrentSex = 1;
                    else if (CallDacomStricmp(Sex, "female") == 0)
                        CurrentSex = 2;
                }
            }
        }
        else if (Reader.is_header("Animations"))
        {
            // Two persistent, de-duplicated name lists.
            while (Reader.read_value())
            {
                if (Reader.is_value("anim"))
                {
                    char const* Name = Reader.get_value_string();
                    if (!ListContainsI(AnimationNames, Name))
                        AnimationNames.push_back(std::string(Name));
                }
                else if (Reader.is_value("preload"))
                {
                    char const* Name = Reader.get_value_string();
                    if (!ListContainsI(PreloadNames, Name))
                        PreloadNames.push_back(std::string(Name));
                }
            }
        }
        else if (Reader.is_header("PetalAnimations"))
        {
            // One de-duplicated list per section, appended to the registry at +0x18
            // (FUN_06301370). Only "anim" is recognised here, not "preload".
            std::list<std::string> Names;
            while (Reader.read_value())
            {
                if (Reader.is_value("anim"))
                {
                    char const* Name = Reader.get_value_string();
                    if (!ListContainsI(Names, Name))
                        Names.push_back(std::string(Name));
                }
            }
            PetalAnimations.push_back(Names);
        }
        else if (Reader.is_header("DetailSwitchTable"))
        {
            // Appended to the registry at +0x24 (FUN_063013d0); the sticky pointer is the list
            // element's address, not the stack temporary's.
            DetailSwitchTable Table;
            DetailSwitchTableReserve(Table, 0x10);
            Table.read_from_ini(&Reader);
            DetailSwitchTables.push_back(Table);
            CurrentSwitch = &DetailSwitchTables.back();
        }
        else if (Reader.is_header("Accessory"))
        {
            load_accessory(&Reader);
        }
        else
        {
            int TypeId = BodypartTypeFromName(Reader.get_header_ptr());
            if (TypeId >= 0)
            {
                load_bodypart(TypeId, CurrentSex, CurrentSwitch, &Reader);
            }
            else
            {
                FDUMP(0x100001, "%s(%d) : *** ERROR: Unknown header %s in bodyparts.ini\n",
                      kCharDescripSource, 0x3a8, Reader.get_header_ptr());
            }
        }
    }

    Reader.close();
}

void CostumeDescriptions::load_costumes(char const* Path)
{
    // No null test on Path.
    INI_Reader Reader;
    if (!Reader.open(Path, false))
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: Cannot open costume ini file: %s",
              kCharDescripSource, 0x415, Path);
        return;
    }

    // The costume's name as the warnings print it.
    char NicknameText[0x400];

    while (Reader.read_header())
    {
        if (!Reader.is_header("Costume"))
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: Unknown header %s in costumes.ini\n",
                  kCharDescripSource, 0x40e, Reader.get_header_ptr());
            continue;
        }

        // Deviation: retail leaves Accessories[] as stack garbage (only the first Count entries
        // are read); the port zeroes the whole record.
        costume Scratch;
        std::memset(&Scratch, 0, sizeof(Scratch));

        while (Reader.read_value())
        {
            // Retail quirk kept: the name buffer is cleared on every value line and only the
            // nickname branch fills it, so the warnings below always print an empty name.
            memset(NicknameText, 0, sizeof(NicknameText));

            if (Reader.is_value("nickname"))
            {
                char const* Value = Reader.get_value_string();
                unsigned Index = 0;
                do
                {
                    NicknameText[Index] = Value[Index];
                } while (Value[Index++] != '\0');
                Scratch.Id = NicknameToCreateId(Value);
            }
            else if (Reader.is_value("accessory"))
            {
                char const* AccName = Reader.get_value_string();
                accessory const* Acc = find_accessory((unsigned long)NicknameToCreateId(AccName));
                if (Acc != nullptr)
                {
                    // Deviation: retail stores with no bound, so a 9th line overwrites Count
                    // (+0x38); the port drops accessories past 8.
                    if (Scratch.AccessoryCount < kMaxAccessories)
                    {
                        Scratch.Accessories[Scratch.AccessoryCount] = Acc;
                        Scratch.AccessoryCount += 1;
                    }
                }
                else
                {
                    FDUMP(0x100002,
                          "%s(%d) : *** WARNING: Cannot find accessory %s for costume %s\n",
                          kCharDescripSource, 0x3e4, AccName, NicknameText);
                }
            }
            else
            {
                char const* KeyName = Reader.get_name_ptr();
                int TypeId = BodypartTypeFromName(KeyName);
                if (TypeId < 0)
                {
                    FDUMP(0x100002,
                          "%s(%d) : *** WARNING: Invalid part type %s for costume %s\n",
                          kCharDescripSource, 0x404, KeyName, NicknameText);
                    continue;
                }

                // The first non-zero part sex becomes the gender.
                char const* PartName = Reader.get_value_string();
                bodypart const* Part =
                    find_bodypart(TypeId, (unsigned long)NicknameToCreateId(PartName));
                if (Part != nullptr)
                {
                    Scratch.Parts[TypeId] = Part;
                    if (Part->Sex != 0 && Scratch.Gender == 0)
                        Scratch.Gender = Part->Sex;
                }
                else
                {
                    FDUMP(0x100002,
                          "%s(%d) : *** WARNING: Cannot find bodypart %s (type %s) for costume %s\n",
                          kCharDescripSource, 0x3fc, PartName, KeyName, NicknameText);
                }
            }
        }

        // Inserted (+0x94, FUN_06301980) whether or not a nickname line was seen; an existing
        // id keeps its record.
        Costumes.insert(std::map<unsigned, costume>::value_type(Scratch.Id, Scratch));
    }

    Reader.close();
}

bodypart const* CostumeDescriptions::find_bodypart(int TypeId, unsigned long NicknameId) const
{
    // FUN_062ff7d0. No range check on TypeId.
    std::map<unsigned, bodypart>::const_iterator It =
        Bodyparts[TypeId].find((unsigned)NicknameId);
    if (It == Bodyparts[TypeId].end())
        return nullptr;
    return &It->second;
}

bodypart const* CostumeDescriptions::find_bodypart(int TypeId, char const* Name) const
{
    return find_bodypart(TypeId, NicknameToCreateId(Name));
}

accessory const* CostumeDescriptions::find_accessory(unsigned long NicknameId) const
{
    // FUN_062ff8b0
    std::map<unsigned, accessory>::const_iterator It =
        Accessories.find((unsigned)NicknameId);
    if (It == Accessories.end())
        return nullptr;
    return &It->second;
}

accessory const* CostumeDescriptions::find_accessory(char const* Name) const
{
    return find_accessory(NicknameToCreateId(Name));
}

costume const* CostumeDescriptions::find_costume(unsigned long NicknameId) const
{
    // FUN_062ff990
    std::map<unsigned, costume>::const_iterator It =
        Costumes.find((unsigned)NicknameId);
    if (It == Costumes.end())
        return nullptr;
    return &It->second;
}

costume const* CostumeDescriptions::find_costume(char const* Name) const
{
    return find_costume(NicknameToCreateId(Name));
}

unsigned long CostumeDescriptions::find_costume_id(char const* Name) const
{
    costume const* Desc = find_costume(Name);
    if (Desc == nullptr)
        return 0;
    return Desc->Id;
}

unsigned long CostumeDescriptions::find_part_id(int TypeId, char const* Name) const
{
    bodypart const* Part = find_bodypart(TypeId, Name);
    if (Part == nullptr)
        return 0;
    return Part->Id;
}

char const* CostumeDescriptions::get_part_name(int TypeId, unsigned long NicknameId) const
{
    bodypart const* Part = find_bodypart(TypeId, NicknameId);
    if (Part == nullptr)
        return nullptr;
    // Not the nickname text (bodypart keeps none): the Id formatted in decimal into the shared,
    // non-reentrant static buffer. Retail calls the ICF-folded accessory::get_name.
    return Part->get_name();
}

char const* CostumeDescriptions::get_part_mesh(int TypeId, unsigned long NicknameId) const
{
    bodypart const* Part = find_bodypart(TypeId, NicknameId);
    if (Part == nullptr)
        return nullptr;
    return Part->get_mesh();
}

unsigned long CostumeDescriptions::find_accessory_id(char const* Name) const
{
    accessory const* Acc = find_accessory(Name);
    if (Acc == nullptr)
        return 0;
    return Acc->Id;
}

void CostumeDescriptions::get_costume(char const* Name, Costume& Out) const
{
    get_costume((int)NicknameToCreateId(Name), Out);
}

void CostumeDescriptions::get_costume(int NicknameId, Costume& Out) const
{
    costume const* Desc = find_costume((unsigned long)NicknameId);
    if (Desc == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: Costume id %x does not exist\n",
              kCharDescripSource, 0x27b, NicknameId);
        FillCostume(nullptr, Out);
        return;
    }
    FillCostume(Desc, Out);
}

int CostumeDescriptions::get_costume_gender(Costume const& Cost) const
{
    // Costume is head/body/left/right; the maps are body/head/left/right.
    const unsigned PartIds[4] = {
        Cost.PartIds[1], Cost.PartIds[0], Cost.PartIds[2], Cost.PartIds[3],
    };
    int Gender = 0;
    int Slot = 0;
    for (; Slot < 4; ++Slot)
    {
        unsigned const PartId = PartIds[Slot];
        if (PartId == 0)
            continue;

        // Retail dereferences the found part without a null test.
        bodypart const* const Part = find_bodypart(Slot, PartId);
        if (Part->Sex == 0)
            continue;

        if (Gender == 0)
        {
            Gender = Part->Sex;
            continue;
        }
        if (Gender == Part->Sex)
            continue;

        FDUMP(0x100001,
              "%s(%d) : *** ERROR: Costume body/head/lhand/rhand values %u/%u/%u/%u have "
              "inconsistent gender\n",
              kCharDescripSource, 0x2a1, Cost.PartIds[1], Cost.PartIds[0], Cost.PartIds[2],
              Cost.PartIds[3]);
    }
    return Gender;
}

int CostumeDescriptions::get_costume_gender(unsigned long NicknameId) const
{
    costume const* Desc = find_costume(NicknameId);
    if (Desc == nullptr)
        return 0;
    return Desc->Gender;
}

void __cdecl LoadBodypartDescriptions(char const* Path)
{
    // FUN_06301090. DAT_063fcc54 is created on first use (operator new(0xa8)); no null test.
    EnsureCostumeDescriptions()->load_bodyparts(Path);
}

bool __cdecl Costume_is_equal(Costume const& A, Costume const& B)
{
    // FUN_06301030. Retail quirks: a part mismatch does not return early, and only the first
    // AccessoryCount entries are compared.
    bool bEqual = true;
    unsigned Part = 0;
    for (; Part < 4; ++Part)
    {
        if (A.PartIds[Part] != B.PartIds[Part])
        {
            bEqual = false;
            break;
        }
    }

    if (A.AccessoryCount != B.AccessoryCount)
        return false;

    // A signed count with no upper bound; the costume loader keeps it in range.
    int const Used = (int)A.AccessoryCount;
    int Slot = 0;
    for (; Slot < Used; ++Slot)
    {
        if (A.AccessoryIds[Slot] != B.AccessoryIds[Slot])
            bEqual = false;
    }
    return bEqual;
}

void __cdecl LoadCostumeDescriptions(char const* Path)
{
    // FUN_06301100
    EnsureCostumeDescriptions()->load_costumes(Path);
}

void __cdecl FreeCostumeDescriptions()
{
    delete GCostumeDescriptions;
    GCostumeDescriptions = nullptr;
}

CostumeDescriptions const* __cdecl GetCostumeDescriptions()
{
    return GCostumeDescriptions;
}

bool __cdecl CostumeGenderAdjust(int* gender, int adjustment)
{
    // FUN_062febc0
    if (adjustment != 0) {
        if (*gender == 0) {
            *gender = adjustment;
            return true;
        }
        if (*gender != adjustment) {
            return false;
        }
    }
    return true;
}
