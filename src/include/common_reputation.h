// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"

// Common.dll Reputation + CostumeDescriptions (bodyparts / costumes).
// See docs/re/common.md (reputation, costumes).

#include "common_cobjdb.h"
#include "common_id.h"
#include "common_text.h"

#include <cstddef>
#include <cstring>
#include <list>
#include <map>
#include <string>
#include <utility>
#include <vector>

class INI_Reader;
// The save writer Reputation::Save writes through. Declared once, in server_savefile.h
// (Server implements it); retail's export spells it `struct ISave` at global scope.
struct ISave;

// Per-bodypart level-of-detail switch-distance curve, read from a "[DetailSwitchTable]" INI
// section in bodyparts.ini. A vector<pair<Key,Value>> of "switch=" entries in file order plus
// two camera constants ("fovx" halved, "sizex") used by adjust_distance. get_switch_distance's
// bracket search assumes the INI lists Key descending.
// CostumeDescriptions::load_bodyparts (0x06300570) builds one per section, keeps it in its own
// std::list and passes the element's address to each following load_bodypart, which stores
// it at bodypart+0x18.
class COMMON_API DetailSwitchTable
{
public:
    // 0x062febf0: zeroed vector, room for 16 pairs, then 20.0f / 1280.0f.
    DetailSwitchTable() : FovXHalf(20.0f), SizeX(1280.0f) { Table.reserve(0x10); }

    // FUN_063011b0, a std::vector<std::pair<float,float>>::reserve instantiation that
    // load_bodyparts calls with Count 0x10. Not a retail member, hence a friend free function
    // (common_costume.cpp) rather than a member of this exported class.
    friend void DetailSwitchTableReserve(DetailSwitchTable& Target, std::size_t Count);
    // Retail hand-inlines a vector-of-pairs copy/assign; defaulted members reproduce it.
    DetailSwitchTable(DetailSwitchTable const&) = default;
    DetailSwitchTable& operator=(DetailSwitchTable const&) = default;
    ~DetailSwitchTable() = default;

    // Common.dll.txt:0x062fecb0. Interpolates between the entries bracketing X (keys descending);
    // 1.0 when X falls between the end keys without a bracket. Retail reads Table[0] without an
    // empty test. Returns double: retail leaves the value unrounded in ST0 and freelancer.exe
    // 004450d3 compares that ST0.
    double get_switch_distance(float X) const;
    // Common.dll.txt:0x062fed60. tan(AngleDeg in degrees)/tan(FovXHalf) * Distance, computed in
    // double (retail x87) and rounded once on return. The third parameter is unused in retail
    // (`RET 0xc`, never read); kept for signature parity.
    float adjust_distance(float Distance, float AngleDeg, float Unused) const;
    // Common.dll.txt:0x062fef70. Parses "switch"/"fovx"/"sizex" values until the next header.
    void read_from_ini(INI_Reader* Reader);

private:
    // Common.dll.txt:0x062fed90. Always appends; relies on the INI order (see the class note).
    void add_table_entry(float Key, float Value);

    std::vector<std::pair<float, float>> Table;
    float FovXHalf; // retail +0x10, half of "fovx" in degrees
    float SizeX;    // retail +0x14, "sizex" -- stored, never read
};

// TString<N> (retail length-prefixed fixed string) lives in common_tstring.h.
#include "common_tstring.h"

// bodypart -- one [Head]/[Body]/[LeftHand]/[RightHand] entry of bodyparts.ini. Layout from
// the ctor (0x062ff030) and init (0x062ff060). Id is atol(nickname), else CreateID(nickname);
// retail never stores the nickname text: get_name (0x062ff140) _ultoa's the Id into a static
// buffer. Copy ctor, operator= and the non-virtual dtor are memberwise.
struct COMMON_API bodypart
{
    bodypart();                                     // 0x062ff030
    bodypart(bodypart const& Other) = default;      // 0x062b6640
    ~bodypart() = default;                          // 0x06315710
    bodypart& operator=(bodypart const& Other) = default; // 0x062b6790

    // 0x062ff060
    void init(char const* Nickname, char const* InMesh, int InSex,
              std::list<std::string>* InPetalAnimations, DetailSwitchTable* InSwitch);

    unsigned long get_name_crc() const;             // 0x06261d90  [+0x00]
    char const* get_name() const;                   // 0x062ff140  (shared with accessory)
    char const* get_mesh() const;                   // 0x062b68f0  [+0x04].c_str()
    int get_gender() const;                         // 0x06263bb0  [+0x14]
    DetailSwitchTable* get_switch_table() const;    // 0x062a0160  [+0x18]
    std::list<std::string>* get_petal_anims() const; // 0x062629e0 [+0x1c]

    unsigned long Id;                               // +0x00
    std::string Mesh;                               // +0x04
    int Sex;                                        // +0x14
    // Borrowed, non-owning pointer into CostumeDescriptions::DetailSwitchTables (node-stable
    // std::list, as in retail). Null when no "[DetailSwitchTable]" preceded this bodypart.
    DetailSwitchTable* Switch;                      // +0x18
    // Borrowed, non-owning pointer into CostumeDescriptions::PetalAnimations (the most
    // recently parsed "[PetalAnimations]" block).
    std::list<std::string>* PetalAnimations;        // +0x1c
};

// accessory -- one [Accessory] entry of bodyparts.ini. Layout from the ctor (0x062ff160),
// init (0x062ff2f0) and load_accessory. INI keys: "mesh", "hardpoint" (AccessoryHardpoint),
// "body_hardpoint" (CharacterHardpoint), "material_library" (one Materials entry per line).
struct COMMON_API accessory
{
    accessory();                                    // 0x062ff160
    accessory(accessory const& Other) = default;    // 0x062b6920
    ~accessory() = default;                         // 0x062ff1f0
    accessory& operator=(accessory const& Other) = default; // 0x062b6a30

    // 0x062ff2f0
    void init(char const* Nickname, char const* InMesh, char const* InAccessoryHardpoint,
              char const* InCharacterHardpoint, std::list<std::string> const& InMaterials);

    unsigned long get_name_crc() const;             // 0x06261d90  [+0x00]
    char const* get_name() const;                   // 0x062ff140  (shared with bodypart)
    char const* get_mesh() const;                   // 0x062b68f0  [+0x04].c_str()
    char const* get_accessory_hardpoint() const;    // 0x062b6900  [+0x14].c_str()
    char const* get_character_hardpoint() const;    // 0x062b6910  [+0x24].c_str()
    std::list<std::string> const& get_materials() const; // 0x06295a30 [+0x34]

    unsigned long Id;                               // +0x00
    std::string Mesh;                               // +0x04
    std::string AccessoryHardpoint;                 // +0x14
    std::string CharacterHardpoint;                 // +0x24
    std::list<std::string> Materials;               // +0x34
};

// costume -- the 0x3c-byte value of the costume map, as CostumeDescriptions::load_costumes
// builds it and get_costume / get_costume_gender(id) read it. Retail stores no nickname.
struct costume
{
    unsigned Id;                        // +0x00
    int Gender;                         // +0x04, the first non-zero part sex
    bodypart const* Parts[4];           // +0x08, indexed by body-part type (body/head/lhand/rhand)
    accessory const* Accessories[8];    // +0x18
    unsigned AccessoryCount;            // +0x38
};

// Shared runtime costume: head/body/left hand/right hand IDs. Bodypart TypeIds are
// body/head/left hand/right hand, so TypeIds 0/1 must be swapped when converting.
struct Costume
{
    unsigned PartIds[4];
    unsigned AccessoryIds[8];
    unsigned AccessoryCount;

    // An inline member in retail; Server.dll's copy is FUN_06d48940. No bound and no zero
    // test: an id of 0 is appended too.
    void add_accessory(unsigned AccessoryId)
    {
        AccessoryIds[AccessoryCount] = AccessoryId;
        ++AccessoryCount;
    }

    // An inline member in retail; Content.dll's copy is FUN_06eb8ef0 (thiscall, one argument).
    // The four parts, the accessory count, then that many accessories.
    bool operator==(Costume const& Other) const
    {
        if (PartIds[0] != Other.PartIds[0] || PartIds[1] != Other.PartIds[1] ||
            PartIds[2] != Other.PartIds[2] || PartIds[3] != Other.PartIds[3])
            return false;
        if (AccessoryCount != Other.AccessoryCount)
            return false;
        // The count is signed.
        int const Count = static_cast<int>(AccessoryCount);
        for (int Index = 0; Index < Count; ++Index)
        {
            if (AccessoryIds[Index] != Other.AccessoryIds[Index])
                return false;
        }
        return true;
    }
};

class COMMON_API CostumeDescriptions
{
public:
    CostumeDescriptions();
    ~CostumeDescriptions();

    // Retail copy ctor (0x062b6d70) and operator= (0x062b6f60) are memberwise. Borrowed
    // pointers inside the copied records (bodypart::Switch/PetalAnimations,
    // costume::Parts/Accessories) keep pointing into the source object, as in retail.
    CostumeDescriptions(CostumeDescriptions const&) = default;
    CostumeDescriptions& operator=(CostumeDescriptions const&) = default;

    void load_bodyparts(char const* Path);
    void load_costumes(char const* Path);

    bodypart const* find_bodypart(int TypeId, unsigned long NicknameId) const;
    bodypart const* find_bodypart(int TypeId, char const* Name) const;
    accessory const* find_accessory(unsigned long NicknameId) const;
    accessory const* find_accessory(char const* Name) const;
    costume const* find_costume(unsigned long NicknameId) const;
    costume const* find_costume(char const* Name) const;
    unsigned long find_costume_id(char const* Name) const;
    unsigned long find_accessory_id(char const* Name) const;
    unsigned long find_part_id(int TypeId, char const* Name) const;
    char const* get_part_name(int TypeId, unsigned long NicknameId) const;
    char const* get_part_mesh(int TypeId, unsigned long NicknameId) const;

    void get_costume(char const* Name, Costume& Out) const;
    void get_costume(int NicknameId, Costume& Out) const;
    int get_costume_gender(Costume const& Cost) const;
    // Common.dll.txt:062b6ca0. Reads the stored Gender off the costume record by id instead
    // of re-deriving it from the bodypart maps.
    int get_costume_gender(unsigned long NicknameId) const;

private:
    // 0x062ffc50. Switch is the sticky "[DetailSwitchTable]" pointer from load_bodyparts.
    void load_bodypart(int TypeId, int Sex, DetailSwitchTable* Switch, INI_Reader* Reader);
    void load_accessory(INI_Reader* Reader);
    // 0x062ed730 ?warn@CostumeDescriptions@@AAEXPAVINI_Reader@@@Z: an empty body (RET 4).
    void warn(INI_Reader* Reader);

public:
    // freelancer.exe's gf\CharacterInfo.cpp reads the two name lists in place (FUN_00435100,
    // FUN_004355e0); there is no accessor export, so they are public.
    // Retail member order and offsets from load_bodyparts / load_bodypart. Each is a 0x0c-byte
    // MSVCP60 std::list; DetailSwitchTables and PetalAnimations hand out interior pointers that
    // must survive later insertions.
    //   +0x00 "[Animations]" / "anim="
    std::list<std::string> AnimationNames;
    //   +0x0c "[Animations]" / "preload="
    std::list<std::string> PreloadNames;

private:
    //   +0x18 one entry per "[PetalAnimations]" section
    std::list<std::list<std::string>> PetalAnimations;
    //   +0x24 one entry per "[DetailSwitchTable]" section
    std::list<DetailSwitchTable> DetailSwitchTables;
    //   +0x30 the four bodypart maps (indexed TypeId*0x14 + 0x30), +0x80 accessories,
    //   +0x94 costumes. Every map holds its record by value (the finds return the mapped
    //   value's address); the dtor (0x062ff600) only runs the member destructors.
    std::map<unsigned, bodypart> Bodyparts[4];
    std::map<unsigned, accessory> Accessories;
    std::map<unsigned, costume> Costumes;
};

COMMON_API void __cdecl LoadBodypartDescriptions(char const* Path);
// Common.dll.txt:06301030, ?Costume_is_equal@@YA_NABUCostume@@0@Z. Compares the four body
// part ids, the accessory count, and only the accessories the count claims are in use.
COMMON_API bool __cdecl Costume_is_equal(Costume const& A, Costume const& B);

COMMON_API void __cdecl LoadCostumeDescriptions(char const* Path);
COMMON_API void __cdecl FreeCostumeDescriptions();
COMMON_API CostumeDescriptions const* __cdecl GetCostumeDescriptions();

COMMON_API bool __cdecl CostumeGenderAdjust(int* gender, int adjustment);

namespace Reputation
{
    struct Relation
    {
        unsigned GroupId;
        float Feeling;
    };

    struct RepGroupCB
    {
        virtual char Execute(unsigned const* GroupId) = 0;
    };

    COMMON_API bool __cdecl Startup(char const* Path);
    COMMON_API void __cdecl Shutdown();
    COMMON_API void __cdecl Save(ISave* Writer);
    COMMON_API void __cdecl LoadFeelings(INI_Reader& Reader);
    COMMON_API void __cdecl FreeFeelings();

    COMMON_API unsigned __cdecl get_id(TString<16> const& Name);
    COMMON_API int __cdecl get_nickname(TString<16>& Out, unsigned const& GroupId);
    COMMON_API unsigned __cdecl get_name(unsigned GroupId);
    COMMON_API unsigned __cdecl get_short_name(unsigned GroupId);
    COMMON_API unsigned __cdecl get_info_card(unsigned GroupId);
    COMMON_API unsigned __cdecl group_count();
    COMMON_API void __cdecl enumerate(RepGroupCB* Callback);
    COMMON_API int __cdecl get_feelings_towards(unsigned FromId, unsigned ToId, float& Out);
    COMMON_API int __cdecl affect_relation(unsigned const& FromId, unsigned const& ToId, float Delta);
    COMMON_API bool __cdecl IsStoryFaction(unsigned GroupId);
    // Relation* const: retail mangles the top-level const (Q, not P).
    COMMON_API void __cdecl set(TString<16> const& Name, Relation* const Rels, unsigned Count);

    // .text:0631e8d0. The changed-affiliation map (DAT_06401900): client id -> that client's
    // previous affiliation, filled by Vibe::SetAffiliation (MP, Notify) and by the
    // auto-affiliation pass after Vibe::Set / Vibe::SetGroupFeelingsTowards (MP). Null while
    // empty. Common never clears it: the consumers do (Server's per-system update, the
    // client's reputation notice).
    COMMON_API std::map<unsigned int, unsigned int>* __cdecl GetChangedAffiliationClientMap();

    // ============================================================================
    // Reputation::Vibe -- per-GroupId *dynamic/runtime* reputation record: a name override,
    // custom infocard, rank, per-player attitude, and per-other-group feeling overrides, layered
    // on top of the static Relation matrix above (GetGroupFeelingsTowards/AttitudeTowards fall
    // back to Reputation::get_feelings_towards when a group has no override of its own).
    //
    // Retail keeps these records in TWO stores, and so does the port (common_vibe.cpp):
    //  - the static store, a std::vector of {int key; VibeValue} slots (DAT_064018b4), filled
    //    while the universe loads (BeginStaticLoad .. EndStaticLoad: Alloc appends, lookups
    //    scan linearly), then sorted and binary-searched with a one-slot cache (DAT_06401938);
    //    FreeDynamic keeps it, FreeStatic empties it;
    //  - the dynamic store, a std::map<int, VibeValue> (DAT_064018c4) with a last-node cache
    //    (DAT_064018d8); Alloc inserts here outside the universe load, FreeDynamic empties it.
    // Every accessor looks in the dynamic store first and then in the static one, except
    // SetClientID / GetClientID, which only see the dynamic store; Free only frees from the
    // dynamic store.
    namespace Vibe
    {
        // Per-other-group feeling override: the std::vector at VibeValue+0x7c, read by
        // GetGroupFeelingsTowards and grown by SetGroupFeelingsTowards.
        struct GroupFeeling
        {
            unsigned GroupId;
            float Feeling;
        };

        // Node of the VibeValue+0x88 singly-linked attitude-delta list. AdjustAttitude
        // (0x06321af0) allocates one per distinct target GroupId and prepends it; VibeValue's
        // copy tail (FUN_0631fad0) deep-copies the chain.
        struct AttitudeDelta
        {
            AttitudeDelta* Next;
            unsigned GroupId;
            float Delta;
        };

        // Per-GroupId dynamic reputation record. Offsets from the dtor (FUN_063221f0), the
        // shared copy tail (FUN_0631fad0) and the accessors. Not exported by retail, so not
        // COMMON_API.
        struct VibeValue
        {
            unsigned Affiliation;              // +0x00, default 0xffffffff ("no affiliation")
            float AttitudeTowardsPlayer;        // +0x04
            unsigned ClientID;                   // +0x08, doubles as "is player-owned" (!=0)
            // +0x0c: the Affiliation group's record inside the group registry DAT_064018ec
            // (the port's GGroups value). Written by SetAffiliation and Set, cleared by the
            // auto-affiliation pass FUN_0631fdd0 and SetAffiliation's MP-notify path; the
            // attitude blend FUN_0631ff70 reads its +0x20 (feelings map), null = "no base
            // relation". Opaque because the record type is private to Common
            // (CmnReputation::FGroup, src/Common/Universe/common_reputation_internal.h).
            void const* AffiliationGroup;          // +0x0c
            FmtStr Name1;                          // +0x10
            FmtStr Name2;                           // +0x28
            unsigned Infocard;                       // +0x40
            wchar_t DisplayName[0x18];                // +0x44, wcsncpy-filled, NUL-terminated
            unsigned Rank;                              // +0x74
            unsigned Reserved78;                         // +0x78: in retail the allocator byte of
                                                           // the VC6 std::vector that follows;
                                                           // no data.
            AttitudeDelta* Deltas;                          // retail +0x88, head of the delta list
            std::vector<GroupFeeling> GroupFeelings;       // retail +0x7c

            VibeValue();
            VibeValue(VibeValue const& Other);
            VibeValue& operator=(VibeValue const& Other);
            ~VibeValue();

            // FUN_0631fad0: retail's shared copy/assign tail (this = destination). Public
            // because Reputation::Vibe::Alloc applies it to an existing entry directly.
            void CopyFieldsFrom(VibeValue const& Other);
        };
        // Deviation: GroupFeelings/Deltas are declared in the reverse of their retail
        // +0x7c/+0x88 order. This build's std::vector is 16 bytes (MSVC Debug iterator proxy)
        // against retail's 12, so the vector goes last and nothing after it is asserted (as
        // for EqObj::DockingSpheres). All access is through the named members.
        static_assert(offsetof(VibeValue, Affiliation) == 0x00, "VibeValue::Affiliation at +0x00");
        static_assert(offsetof(VibeValue, AttitudeTowardsPlayer) == 0x04, "VibeValue::AttitudeTowardsPlayer at +0x04");
        static_assert(offsetof(VibeValue, ClientID) == 0x08, "VibeValue::ClientID at +0x08");
        static_assert(offsetof(VibeValue, AffiliationGroup) == 0x0c, "VibeValue::AffiliationGroup at +0x0c");
        static_assert(offsetof(VibeValue, Name1) == 0x10, "VibeValue::Name1 at +0x10");
        static_assert(offsetof(VibeValue, Name2) == 0x28, "VibeValue::Name2 at +0x28");
        static_assert(offsetof(VibeValue, Infocard) == 0x40, "VibeValue::Infocard at +0x40");
        static_assert(offsetof(VibeValue, DisplayName) == 0x44, "VibeValue::DisplayName at +0x44");
        static_assert(offsetof(VibeValue, Rank) == 0x74, "VibeValue::Rank at +0x74");
        static_assert(offsetof(VibeValue, Deltas) == 0x7c, "VibeValue::Deltas");

        COMMON_API unsigned __cdecl Count();
        COMMON_API bool __cdecl Verify(int GroupId);
        // .text:06320720. Returns -1 for GroupId -1; otherwise makes sure GroupId has an entry
        // and RESETS it to the default record through the copy tail (so an existing entry
        // loses its affiliation, names, rank, feelings and client id -- but keeps its
        // attitude-delta list, which the copy tail only prepends to), and returns 0. During
        // the universe load the entry lives in the static store, otherwise in the dynamic one.
        COMMON_API int __cdecl Alloc(int const& GroupId);
        // Calls Alloc only when GroupId has no entry yet. Always returns 0.
        COMMON_API int __cdecl EnsureExists(int const& GroupId);
        // .text:06320ae0. Frees GroupId's DYNAMIC entry (-2 when it has none) and drops every
        // other entry's attitude deltas towards it.
        COMMON_API int __cdecl Free(int const& GroupId);
        COMMON_API void __cdecl FreeDynamic();

        // Common.dll-internal (not exported by retail), called by the universe load:
        // FUN_06320430 -- static-store loading mode on, capacity at least 0x1000 slots;
        void __cdecl BeginStaticLoad();
        // FUN_06320530 -- loading mode off, the static store sorted by key;
        void __cdecl EndStaticLoad();
        // FUN_06320550 (via the thunk 062b8e20) -- the static store emptied.
        void __cdecl FreeStatic();

        // 0x06321720. With ExcludeIfPlayerOwned and a player-owned entry (ClientID != 0), Out
        // is 0xffffffff instead of the Affiliation. Returns -2 if GroupId has no entry (Out
        // preset to 0xffffffff either way).
        COMMON_API int __cdecl GetAffiliation(int const& GroupId, unsigned& Out, bool ExcludeIfPlayerOwned);
        // .text:063213b0. -2 if GroupId has no entry, -3 if Affiliation is not a registered
        // group, 0 otherwise. On a real change outside SinglePlayer() with Notify set, the
        // client's previous affiliation is recorded in the changed-affiliation map (see
        // GetChangedAffiliationClientMap) keyed by the entry's ClientID.
        COMMON_API int __cdecl SetAffiliation(int const& GroupId, unsigned Affiliation, bool Notify);
        // .text:06320ea0 / 06320f10: the DYNAMIC store only -- an entry that lives in the static
        // store is not found (-2 / 0). Returns 0/current ClientID, or -2/0.
        COMMON_API int __cdecl SetClientID(int GroupId, unsigned ClientID);
        COMMON_API unsigned __cdecl GetClientID(int GroupId);
        COMMON_API int __cdecl GetInfocard(int const& GroupId, unsigned& Out);
        COMMON_API int __cdecl SetInfocard(int const& GroupId, unsigned Infocard);
        COMMON_API int __cdecl GetRank(int const& GroupId, unsigned& Out);
        COMMON_API int __cdecl SetRank(int const& GroupId, unsigned Rank);
        // *DisplayName receives a pointer into the entry's internal buffer, as in retail.
        COMMON_API int __cdecl GetName(int const& GroupId, FmtStr& Name1Out, FmtStr& Name2Out, wchar_t const*& DisplayNameOut);
        // DisplayName may be null (clears the stored display name); otherwise copied via
        // wcsncpy(..., 0x17) with a forced NUL at DisplayName[0x17].
        COMMON_API int __cdecl SetName(int const& GroupId, FmtStr const& Name1, FmtStr const& Name2, wchar_t const* DisplayName);
        // .text:06320fc0: -2 if GroupId has no entry, -3 if get_feelings_towards(Other,
        // Affiliation) fails, else 0 with Out = clamp(override + base).
        COMMON_API int __cdecl GetGroupFeelingsTowards(int const& GroupId, unsigned const& OtherGroupId, float& Out);
        COMMON_API int __cdecl SetGroupFeelingsTowards(int const& GroupId, unsigned const& OtherGroupId, float Feeling);
        // Direct field write, no blending.
        COMMON_API int __cdecl SetAttitudeTowardsPlayer(int GroupId, float Attitude);

        // Common.dll.txt:0x06320bc0. Bulk getter: Affiliation, Rank, GroupFeelings (as a
        // Relation[] snapshot), Name1, Name2 and DisplayName. Returns -2 if GroupId has no
        // entry. Retail quirk kept: CountOut is a byte counter, so more than 255 entries wrap
        // and overwrite from the start; retail callers pass a 0xff-entry buffer.
        // *DisplayNameOut aliases the entry's buffer, as in GetName.
        COMMON_API int __cdecl Get(int const& GroupId, unsigned& AffiliationOut, unsigned& RankOut, unsigned char& CountOut, Relation* const Relations, FmtStr& Name1Out, FmtStr& Name2Out, wchar_t const*& DisplayNameOut);
        // Common.dll.txt:0x06320cb0. Bulk setter: replaces Affiliation, Rank, GroupFeelings
        // (from Relations[0..Count)), Name1, Name2 and DisplayName. Returns -1 if GroupId == -1,
        // -2 if no entry exists (does not create one; callers EnsureExists first).
        //  - an NPC entry (ClientID == 0) caches its new affiliation's group record in
        //    AffiliationGroup (null when the group is not registered);
        //  - only Relations entries whose GroupId is a registered group are kept;
        //  - unlike SetName, DisplayName is copied with no null check, as in retail;
        //  - ends with the auto-affiliation pass FUN_0631fdd0, which reads DAT_064018dc, the
        //    story-faction id list shared with IsStoryFaction (fc_*_grp MakeId values truncated
        //    to 16 bits).
        COMMON_API int __cdecl Set(int const& GroupId, unsigned Affiliation, unsigned Rank, unsigned char Count, Relation const* const Relations, FmtStr const& Name1, FmtStr const& Name2, wchar_t const* DisplayName);

        // AttitudeTowards (.text:063217c0) and AttitudeTowardsSymmetrical (.text:06321a70) are
        // both FUN_063217e0 (AttitudeTowardsCore in common_vibe.cpp): -2 if From has no
        // entry, -3 if To has none (Out1 = 0 first; Out2 untouched on failure), otherwise the
        // blend FUN_0631ff70 per direction, clamped to [-1, 1] and rounded to hundredths
        // (DAT_06401918 / DAT_06401920).
        // AdjustAttitude (.text:06321af0): -2 / -3 the same way; stores in From's delta list
        // for To the value `Delta - clamp(override + base)` (or `Delta - base` when the base
        // relation cannot be computed), so Delta acts as a target attitude, not an increment.
        COMMON_API int __cdecl AttitudeTowards(int const& FromGroupId, int const& ToGroupId, float& Out);
        COMMON_API int __cdecl AttitudeTowardsSymmetrical(int const& FromGroupId, int const& ToGroupId, float& Out1, float& Out2);
        COMMON_API int __cdecl AdjustAttitude(int const& FromGroupId, int const& ToGroupId, float Delta);
    }
}
