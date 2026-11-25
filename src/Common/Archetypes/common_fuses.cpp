// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_fuses.h"

#include "common_ini.h"
#include "common_misc.h" // GetFileSysPath
#include "DALib.h"

#include <algorithm>
#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <utility>

extern "C"
{
    __declspec(dllimport) extern void(__cdecl* FDUMP)(int flags, const char* fmt, ...);
}

// common_dacom_stricmp.cpp: DACOM.DLL!stricmp (Common IAT PTR 0639902c).
int CallDacomStricmp(const char* Left, const char* Right);

namespace
{
    const char kPetalSource[] = "E:\\FL\\Scratch\\Source\\Common\\PetalDB.cpp";
    const char kWeaponModSource[] = "E:\\FL\\Scratch\\Source\\Common\\WeaponModDB.cpp";
    const unsigned kPetalNameChars = 0x400;
    const int kMaxPetalExtras = 0x10;
    // load_from_ini's GetFileSysPath destination is 0x400 bytes (the frame slot size), not
    // MAX_PATH.
    const unsigned kPetalPathChars = 0x400;
    const float kDefaultShieldMod = 1.0f;
    // FuseAction's "no range" AtTEnd and Fuse's default lifetime.
    float const kMinusOne = -1.0f;
    // .rdata:063997d0 = 2^-15, the rand() scale in FuseAction::Randomize.
    float const kRandScale = 3.0517578125e-05f;

    PetalInterfaceDatabase GPetalDb;
    std::map<unsigned, FWeaponType> GWeaponTypes;

    // Deviation: load_from_ini's value copies are unbounded inline strcpy loops into 0x400-byte
    // heap buffers in retail; the port keeps the copy inside the buffer. The source is never
    // null: get_value_string(int) returns "" for a missing parameter.
    void CopyZ(char* Dest, unsigned DestChars, char const* Src)
    {
        unsigned Index = 0;
        for (; Index + 1 < DestChars && Src[Index] != '\0'; ++Index)
            Dest[Index] = Src[Index];
        Dest[Index] = '\0';
    }

    // DACOM stricmp.
    int ObjectTableCategory(char const* ValueName)
    {
        if (CallDacomStricmp(ValueName, "room") == 0)
            return PetalCategoryRoom;
        if (CallDacomStricmp(ValueName, "cart") == 0)
            return PetalCategoryEquipmentCart;
        if (CallDacomStricmp(ValueName, "prop") == 0)
            return PetalCategoryProp;
        return -1;
    }
}

// ?m_FuseINIFiles@FuseDB@@1V?$list@U?$TString@$0BAE@@@...@std@@A (.data:063fc4c4; its
// dynamic initializer at .text:062be350 allocates the list head and registers the dtor).
std::list<TString<260>> FuseDB::m_FuseINIFiles;

void __cdecl FuseDB::AddFuseINIFile(char const* Path)
{
    // .text:062be260. Nothing is opened or parsed here: the consumer modules read these files
    // themselves (freelancer.exe FUN_0052a7a0, Server.dll FUN_06cfd4b0).
    TString<260> Stored;
    Stored = Path;
    m_FuseINIFiles.push_back(Stored);
}

void __cdecl FuseDB::Shutdown()
{
    // .text:062be310
    m_FuseINIFiles.clear();
}

PetalInterfaceDatabase::PetalInterfaceDatabase()
{
}

PetalInterfaceDatabase::~PetalInterfaceDatabase()
{
    destroy();
}

void PetalInterfaceDatabase::destroy()
{
    unsigned Category = 0;
    for (; Category < PetalCategoryCount; ++Category)
        Categories[Category].clear();
}

int __cdecl PetalInterfaceDatabase::get_category(char const* Name)
{
    if (Name == nullptr || Name[0] == '\0')
        return -1;
    if (std::strcmp(Name, "Room") == 0)
        return PetalCategoryRoom;
    if (std::strcmp(Name, "Spaceship") == 0)
        return PetalCategorySpaceship;
    if (std::strcmp(Name, "Solar") == 0)
        return PetalCategorySolar;
    if (std::strcmp(Name, "Asteroid") == 0)
        return PetalCategoryAsteroid;
    if (std::strcmp(Name, "Equipment") == 0)
        return PetalCategoryEquipment;
    if (std::strcmp(Name, "Equipment Cart") == 0)
        return PetalCategoryEquipmentCart;
    if (std::strcmp(Name, "Prop") == 0)
        return PetalCategoryProp;

    FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: Unknown Petal category name %s\n", kPetalSource, 0xbb,
          Name);
    return -1;
}

// FUN_0631c870. The nickname text is dropped (only its id kept); extras are StringAlloc'd with
// the temporary flag. The first record under a nickname stays. Category is used as an index
// with no range test.
void PetalInterfaceDatabase::add_record(int Category, char const* Nickname, char const* Mesh,
                                        char** ExtraNames, int ExtraCount)
{
    petal_record Record;
    Record.NicknameId = CreateID(Nickname);
    Record.Mesh.assign(Mesh, strlen(Mesh));

    int ExtraIndex = 0;
    for (ExtraIndex = 0; ExtraIndex < ExtraCount; ++ExtraIndex)
        Record.Extras.push_back(StringAlloc(ExtraNames[ExtraIndex], true));

    Categories[Category].insert(
        std::pair<unsigned const, petal_record>(Record.NicknameId, Record));
}

// FUN_0631cb40; no range test of Category.
PetalInterfaceDatabase::petal_record const* PetalInterfaceDatabase::find_record(
    int Category, unsigned long NicknameId) const
{
    std::map<unsigned, petal_record>::const_iterator It =
        Categories[Category].find(NicknameId);
    if (It == Categories[Category].end())
        return nullptr;
    return &It->second;
}

void PetalInterfaceDatabase::load_from_ini(char const* RelPath)
{
    // FUN_0631c210. Retail heap-allocates the 18 scratch buffers with operator new(0x400); the
    // port keeps the same sizes on the stack.
    char FullPath[kPetalPathChars];
    GetFileSysPath(FullPath, RelPath, DALib::Data);

    INI_Reader Reader;
    if (!Reader.open(FullPath, false))
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Cannot open petal database file %s\n", kPetalSource,
              0x86, FullPath);
        return;
    }

    while (Reader.read_header())
    {
        if (!Reader.is_header("ObjectTable"))
            continue;
        if (!Reader.read_value())
            continue;

        do
        {
            char const* ValueName = Reader.get_name_ptr();
            int Category = ObjectTableCategory(ValueName);
            if (Category < 0)
            {
                FDUMP(kFdumpError, "%s(%d) : *** ERROR: Invalid category '%s' in petal database file %s\n",
                      kPetalSource, 0x59, ValueName, FullPath);
            }

            if (Reader.is_value_empty(0) || Reader.is_value_empty(1))
            {
                FDUMP(kFdumpError,
                      "%s(%d) : *** ERROR: Invalid PetalDB entry %s in .ini file %s, cannot find nickname and mesh\n",
                      kPetalSource, 0x61, Reader.get_value_string(), FullPath);
            }

            char NicknameBuf[kPetalNameChars];
            char MeshBuf[kPetalNameChars];
            char ExtraBufs[kMaxPetalExtras][kPetalNameChars];
            char* ExtraPtrs[kMaxPetalExtras];

            CopyZ(NicknameBuf, kPetalNameChars, Reader.get_value_string(0));
            CopyZ(MeshBuf, kPetalNameChars, Reader.get_value_string(1));

            // Deviation: retail has no upper bound on the extras even though only 16 buffers
            // exist (more than 16 writes through garbage pointers); the port stops at 16.
            int ExtraCount = 0;
            unsigned ParamIndex = 2;
            while (!Reader.is_value_empty(ParamIndex) && ExtraCount < kMaxPetalExtras)
            {
                CopyZ(ExtraBufs[ExtraCount], kPetalNameChars, Reader.get_value_string(ParamIndex));
                ExtraPtrs[ExtraCount] = ExtraBufs[ExtraCount];
                ExtraCount += 1;
                ParamIndex += 1;
            }

            // An unrecognised value name still parses its strings, it just never reaches
            // add_record.
            if (Category != -1)
                add_record(Category, NicknameBuf, MeshBuf, ExtraPtrs, ExtraCount);
        } while (Reader.read_value());
    }

    // Retail closes the reader explicitly here; the failed-open path does not.
    Reader.close();
}

void __cdecl PetalDB_load(char const* RelPath)
{
    GPetalDb.load_from_ini(RelPath);
}

void __cdecl PetalDB_destroy()
{
    GPetalDb.destroy();
}

PetalInterfaceDatabase const* __cdecl PetalDB_get()
{
    return &GPetalDb;
}

// FUN_06341790 (called from DataConfig::startup with the resolved path). The path is opened as
// given. Each [WeaponType] starts with no current type;
// "nickname" inserts {id, empty map} (an existing entry is kept) and makes it current;
// "shield_mod" with a current type inserts CreateID(value 0) -> value 1 (first one stays).
void __cdecl WeaponModDB_load(char const* Path)
{
    INI_Reader Reader;
    if (!Reader.open(Path, false))
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Failed to open WeaponMod file %s.", kWeaponModSource,
              0x48, Path);
        return;
    }

    FWeaponType* Current = nullptr;
    while (Reader.read_header())
    {
        if (!Reader.is_header("WeaponType"))
            continue;
        Current = nullptr;
        if (!Reader.read_value())
            continue;

        do
        {
            if (Reader.is_value("nickname"))
            {
                unsigned NicknameId = CreateID(Reader.get_value_string());
                std::map<unsigned, FWeaponType>::iterator It = GWeaponTypes.find(NicknameId);
                if (It == GWeaponTypes.end())
                {
                    FWeaponType Type;
                    Type.NicknameId = NicknameId;
                    It = GWeaponTypes.insert(std::pair<unsigned, FWeaponType>(NicknameId, Type))
                             .first;
                }
                Current = &It->second;
            }
            else if (Reader.is_value("shield_mod") && Current != nullptr)
            {
                unsigned ShieldId = CreateID(Reader.get_value_string(0));
                float Modifier = Reader.get_value_float(1);
                if (Current->ShieldMods.find(ShieldId) == Current->ShieldMods.end())
                    Current->ShieldMods[ShieldId] = Modifier;
            }
        } while (Reader.read_value());
    }
}

void __cdecl WeaponModDB_destroy()
{
    GWeaponTypes.clear();
}

float __cdecl WeaponModDB_GetShieldMod(unsigned WeaponTypeId, unsigned ShieldTypeId)
{
    std::map<unsigned, FWeaponType>::const_iterator WeaponIt = GWeaponTypes.find(WeaponTypeId);
    if (WeaponIt == GWeaponTypes.end())
        return kDefaultShieldMod;

    std::map<unsigned, float>::const_iterator ModIt = WeaponIt->second.ShieldMods.find(ShieldTypeId);
    if (ModIt == WeaponIt->second.ShieldMods.end())
        return kDefaultShieldMod;
    return ModIt->second;
}

// ---------------------------------------------------------------------------
// FuseAction -- Common.dll .text:062bd3d0-062bd790 / 062bee90-062bef64.
// ---------------------------------------------------------------------------

FuseAction::FuseAction()
    : AtT(kMinusOne)
    , AtTEnd(kMinusOne)
    , RefCount(0)
    , bTriggered(false)
{
    // .text:062bd3d0
}

FuseAction::FuseAction(FuseAction const& Other)
    : AtT(Other.AtT)
    , AtTEnd(Other.AtTEnd)
    , RefCount(Other.RefCount)
    , bTriggered(Other.bTriggered)
    , TriggeredSubObjs(Other.TriggeredSubObjs)
{
    // .text:062bd460
}

FuseAction::~FuseAction()
{
    // .text:062bd400
}

FuseAction& FuseAction::operator=(FuseAction const& Other)
{
    // .text:062bd4e0: scalars unconditionally, the vector only when it is a different object.
    AtT = Other.AtT;
    AtTEnd = Other.AtTEnd;
    RefCount = Other.RefCount;
    bTriggered = Other.bTriggered;
    if (&TriggeredSubObjs != &Other.TriggeredSubObjs)
        TriggeredSubObjs = Other.TriggeredSubObjs;
    return *this;
}

int FuseAction::Load()
{
    // .text:062bee90
    RefCount = RefCount + 1;
    return RefCount;
}

int FuseAction::UnLoad()
{
    // .text:062beea0
    RefCount = RefCount - 1;
    return RefCount;
}

bool FuseAction::IsTriggered() const
{
    // .text:06286e60 (ICF-shared with CEquip::IsActive).
    return bTriggered;
}

bool FuseAction::IsTriggered(unsigned short SubObjId) const
{
    // .text:062bd6f0
    if (!bTriggered)
        return false;
    std::vector<unsigned short>::const_iterator It =
        std::find(TriggeredSubObjs.begin(), TriggeredSubObjs.end(), SubObjId);
    return It != TriggeredSubObjs.end();
}

void FuseAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    // .text:062bd730. The source id is not used by the base class. The second find retail runs
    // after the insert has no side effect.
    (void)SourceId;
    bTriggered = true;
    std::vector<unsigned short>::iterator It =
        std::find(TriggeredSubObjs.begin(), TriggeredSubObjs.end(), SubObjId);
    if (It == TriggeredSubObjs.end())
        TriggeredSubObjs.insert(TriggeredSubObjs.end(), 1, SubObjId);
}

void FuseAction::UnTrigger(unsigned short SubObjId)
{
    // .text:062bd790. The flag at +0x10 is left alone.
    std::vector<unsigned short>::iterator It =
        std::find(TriggeredSubObjs.begin(), TriggeredSubObjs.end(), SubObjId);
    if (It != TriggeredSubObjs.end())
        TriggeredSubObjs.erase(It);
}

bool FuseAction::ShouldRandomize() const
{
    // .text:062bd430: a bit-pattern compare with -1.0f.
    return AtTEnd != kMinusOne;
}

float FuseAction::Randomize()
{
    // .text:062bef20. Unrounded until the final store.
    float const Begin = AtT;
    float const End = AtTEnd;
    int const Roll = rand();
    AtTEnd = kMinusOne;
    double const Scaled = (double)Roll * kRandScale;
    double const Span = (double)End - Begin;
    AtT = (float)(Scaled * Span + Begin);
    return AtT;
}

float FuseAction::GetTriggerTime() const
{
    // .text:062bd450
    return AtT;
}

void FuseAction::CopyArchProperties(FuseAction const& Other)
{
    // .text:062beeb0: copies the archetype part only -- +0x04 AtT and +0x08 AtTEnd.
    AtT = Other.AtT;
    AtTEnd = Other.AtTEnd;
}

bool FuseAction::ReadFuseActionValue(INI_Reader& Reader)
{
    // .text:062beed0: consumes only "at_t"; a non-empty value 1 makes it a random range.
    // Anything else -> false so the subclass handles it.
    if (!Reader.is_value("at_t"))
        return false;
    AtT = Reader.get_value_float(0);
    if (!Reader.is_value_empty(1))
        AtTEnd = Reader.get_value_float(1);
    return true;
}

// ---------------------------------------------------------------------------
// Fuse -- Common.dll .text:062bd020-062be255.
// ---------------------------------------------------------------------------

Fuse::Fuse(ID_String const& NicknameIn, float LifetimeIn)
    : Nickname(NicknameIn)
    , LifetimeSec(LifetimeIn)
    , bDeathFuse(false)
    , bNeedsRandomize(false)
{
    // .text:062bd020
}

Fuse::Fuse(Fuse const& Other)
    : Nickname(Other.Nickname)
    , LifetimeSec(Other.LifetimeSec)
    , bDeathFuse(Other.bDeathFuse)
    , bNeedsRandomize(Other.bNeedsRandomize)
{
    // .text:062bd960. Ignitions are never copied. Retail's inlined FreeActions over the fresh
    // tree does nothing. Every action is cloned (no null check) and inserted under Other's key,
    // not the clone's trigger time.
    for (std::multimap<float, FuseAction*>::const_iterator It = Other.Actions.begin();
         It != Other.Actions.end(); ++It)
    {
        FuseAction* Clone = It->second->Clone();
        Actions.insert(std::pair<float const, FuseAction*>(It->first, Clone));
    }
}

Fuse::~Fuse()
{
    // .text:062bd7e0
    for (std::multimap<float, FuseAction*>::iterator It = Actions.begin(); It != Actions.end();
         ++It)
    {
        if (It->second != nullptr)
            delete It->second;
    }
    Actions.clear();
    Burns.clear();
}

Fuse& Fuse::operator=(Fuse const& Other)
{
    // .text:062bd100, the compiler's memberwise assignment: the FuseAction pointers are shared,
    // not cloned (FUN_062be950).
    Nickname = Other.Nickname;
    LifetimeSec = Other.LifetimeSec;
    bDeathFuse = Other.bDeathFuse;
    if (&Actions != &Other.Actions)
        Actions = Other.Actions;
    bNeedsRandomize = Other.bNeedsRandomize;
    if (&Burns != &Other.Burns)
        Burns = Other.Burns;
    return *this;
}

void Fuse::FreeActions()
{
    // .text:062bdb80
    for (std::multimap<float, FuseAction*>::iterator It = Actions.begin(); It != Actions.end();
         ++It)
    {
        if (It->second != nullptr)
            delete It->second;
    }
    Actions.clear();
}

bool Fuse::ReadFuseValues(INI_Reader& Reader)
{
    // .text:062bdc50 (FreeActions through the vftable). Always true; +0x24 and the burn list
    // are not touched.
    FreeActions();
    Nickname.clear();
    LifetimeSec = kMinusOne;
    bDeathFuse = false;
    while (Reader.read_value())
    {
        if (Reader.is_value("name"))
            Nickname.Id = CreateID(Reader.get_value_string());
        else if (Reader.is_value("lifetime"))
            LifetimeSec = Reader.get_value_float(0);
        else if (Reader.is_value("death_fuse"))
            bDeathFuse = Reader.get_value_bool(0);
    }
    return true;
}

bool Fuse::IsBurning() const
{
    // .text:062bd0f0
    return !Burns.empty();
}

bool Fuse::IsSpent() const
{
    // .text:062bdcf0: true for the first burn whose window is fully open (NaN fails).
    for (std::list<FFuseBurn>::const_iterator It = Burns.begin(); It != Burns.end(); ++It)
    {
        if (It->Lower <= 0.0f && It->Upper >= 1.0f)
            return true;
    }
    return false;
}

bool Fuse::IsSpentAt(float T) const
{
    // .text:062bdd30: false for T outside [0, 1] or NaN; then true for the first burn whose
    // window holds T.
    if (!(T >= 0.0f))
        return false;
    if (!(T <= 1.0f))
        return false;
    for (std::list<FFuseBurn>::const_iterator It = Burns.begin(); It != Burns.end(); ++It)
    {
        if (It->Lower <= T && T <= It->Upper)
            return true;
    }
    return false;
}

bool Fuse::IgniteAt(unsigned SourceId, unsigned short SubObjId, float AtTIn)
{
    // .text:062bdd90: an existing burn with the same sub-object and ignition time refuses.
    for (std::list<FFuseBurn>::const_iterator It = Burns.begin(); It != Burns.end(); ++It)
    {
        if (It->SubObjId == SubObjId && It->IgniteT == AtTIn)
            return false;
    }
    FFuseBurn NewBurn;
    NewBurn.SourceId = SourceId;
    NewBurn.SubObjId = SubObjId;
    NewBurn.IgniteT = AtTIn;
    NewBurn.Lower = AtTIn;
    NewBurn.Upper = AtTIn;
    Burns.push_back(NewBurn);
    return true;
}

void Fuse::Burn(float Dt)
{
    // .text:062bde40. Step is rounded to float. The first pass widens every window, clamped to
    // [0, 1] (unrounded compares). The second triggers every action keyed in [Lower, Upper]
    // (FUN_062becd0 / FUN_062bed30) not yet triggered for the burn's sub-object.
    if (Burns.empty())
        return;
    float const Step = (float)((double)Dt / LifetimeSec);

    for (std::list<FFuseBurn>::iterator It = Burns.begin(); It != Burns.end(); ++It)
    {
        double const LowerX = (double)It->Lower - Step;
        It->Lower = (LowerX < 0.0) ? 0.0f : (float)LowerX;
        double const UpperX = (double)Step + It->Upper;
        It->Upper = (UpperX > 1.0) ? 1.0f : (float)UpperX;
    }

    for (std::list<FFuseBurn>::iterator It = Burns.begin(); It != Burns.end(); ++It)
    {
        std::multimap<float, FuseAction*>::iterator First = Actions.lower_bound(It->Lower);
        std::multimap<float, FuseAction*>::iterator Last = Actions.upper_bound(It->Upper);
        for (std::multimap<float, FuseAction*>::iterator ActionIt = First; ActionIt != Last;
             ++ActionIt)
        {
            FuseAction* Action = ActionIt->second;
            if (!Action->IsTriggered(It->SubObjId))
                Action->Trigger(It->SourceId, It->SubObjId);
        }
    }
}

bool Fuse::UnBurn(unsigned short SubObjId, float AtTIn)
{
    // .text:062bdf50. Every action in the window that reports itself triggered at all (the
    // no-argument IsTriggered) is untriggered for SubObjId, then the burn is removed.
    for (std::list<FFuseBurn>::iterator It = Burns.begin(); It != Burns.end(); ++It)
    {
        if (It->SubObjId != SubObjId || !(It->IgniteT == AtTIn))
            continue;

        std::multimap<float, FuseAction*>::iterator First = Actions.lower_bound(It->Lower);
        std::multimap<float, FuseAction*>::iterator Last = Actions.upper_bound(It->Upper);
        for (std::multimap<float, FuseAction*>::iterator ActionIt = First; ActionIt != Last;
             ++ActionIt)
        {
            FuseAction* Action = ActionIt->second;
            if (Action->IsTriggered())
                Action->UnTrigger(It->SubObjId);
        }

        Burns.erase(It);
        return true;
    }
    return false;
}

void Fuse::Load() const
{
    // .text:062be040, no null check.
    for (std::multimap<float, FuseAction*>::const_iterator It = Actions.begin();
         It != Actions.end(); ++It)
        It->second->Load();
}

void Fuse::UnLoad() const
{
    // .text:062be070, no null check.
    for (std::multimap<float, FuseAction*>::const_iterator It = Actions.begin();
         It != Actions.end(); ++It)
        It->second->UnLoad();
}

void Fuse::RandomizeActions()
{
    // .text:062be0a0: a randomizable action is erased (FUN_062be400 returns the successor),
    // randomized and re-inserted under its new trigger time.
    if (!bNeedsRandomize)
        return;

    std::multimap<float, FuseAction*>::iterator It = Actions.begin();
    while (It != Actions.end())
    {
        FuseAction* Action = It->second;
        if (Action->ShouldRandomize())
        {
            It = Actions.erase(It);
            Action->Randomize();
            Actions.insert(std::pair<float const, FuseAction*>(Action->GetTriggerTime(), Action));
        }
        else
        {
            ++It;
        }
    }
    bNeedsRandomize = false;
}

bool Fuse::OverrideLifetime(float NewLifetime)
{
    // .text:062be240: only while no burn is live.
    if (!Burns.empty())
        return false;
    LifetimeSec = NewLifetime;
    return true;
}

// ===========================================================================
// Explicit STL instantiation exported like retail's. Retail Common.dll exports all 47
// members of its VC6 std::vector<unsigned short>, the type of FuseAction's
// sub-object id list (copy ctor 0x5d460, operator= 0x5d4e0). The element is a real
// unsigned short, not a VC6 wide character. Importing declaration: common_fuses.h.
// ===========================================================================
template class COMMON_API std::vector<unsigned short>;
