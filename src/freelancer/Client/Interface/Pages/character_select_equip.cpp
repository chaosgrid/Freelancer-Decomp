// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/Pages/character_select_equip.h"

#include "server_characterset.h"
#include "common_id.h"
#include "common_newchar.h"
#include "common_reputation.h" // Reputation::Vibe::SetClientID
#include "common_server.h"     // IServer::SetVisitedState
#include "common_misc.h"       // SinglePlayer
#include "common_universe.h"
#include "dacom.h"       // FDUMP
#include "GameFlow/gf_object_ship.h" // GfObjectShip (FUN_00446be0) and its equipment entry points
#include "Core/globals.h"       // SeedClientAccumulatedTime (DAT_0067ecd8)
#include "Network/login_mgr.h"   // G_LoginMgr (DAT_0067e7b8)
#include "Interface/menu_ui_init.h" // PlayOrRewindUiSoundSlot (FUN_005645c0)
#include "Space/player_spawn.h" // GetPlayerConnectionId (DAT_00673344)

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include <map>

namespace
{
    // DAT_00672960 — client EquipDescList singleton (a std::list<EquipDesc>, as in retail).
    EquipDescList G_PlayerEquipDescList;

    // DAT_0067296c — collision-group desc list (retail std::list at EquipDescList+0xc area).
    std::list<CollisionGroupDesc> G_PlayerCollisionGroups;

    // DAT_0067337c — selected ship archetype id.
    unsigned G_PlayerShipArchetypeId = 0;

    // DAT_00673380 — relative health scale (1.0 = full). Retail .bss (zero at load); written only
    // by FUN_004c4a80 (= 0) and FUN_004c4f80.
    float G_PlayerHealthScale = 0.0f;

    // DAT_00673364 — credits.
    unsigned G_PlayerCredits = 0;

    // DAT_00673368 / DAT_0067336c / DAT_00673370 -- the kill, mission-success and
    // mission-failure counts IClient slots 78..80 store through FUN_004c4d70 / 80 / 90. The
    // exe has no reader of any of the three; uninitialised .data, so zero at load.
    unsigned G_PlayerNumKills = 0;
    unsigned G_PlayerMissionSuccesses = 0;
    unsigned G_PlayerMissionFailures = 0;

    // DAT_00673360 -- the character's voice (CharacterInfo +0x26c, FUN_004c4660); the ship
    // spawn reads it into CreateParms::VoiceId (FUN_004c4650).
    unsigned G_PlayerVoiceId = 0;

    // DAT_0067338c — the player's rank level (FUN_004c55d0 / FUN_004c55e0); -1 while a
    // RequestRankLevel answer is pending (FUN_004c55f0).
    int G_PlayerRankLevel = 0;

    // DAT_0061226c -- the player's reputation vibe, from the character-select response
    // (FUN_004c4680); read through FUN_004c4670 into CreateParms::VibeId and the reputation
    // lookups.
    int G_PlayerRepVibe = -1;

    // DAT_00673354 / DAT_00673358 — star system / base (FUN_004c4840/4910).
    unsigned G_PlayerStarSystemId = 0;
    unsigned G_PlayerBaseId = 0;

    // DAT_0067335c -- the location (CharacterInfo +0x268) the character stands in
    // (FUN_004c4a10 / FUN_004c4a00).
    unsigned G_PlayerLocationId = 0;

    // DAT_006732e4..ec / DAT_006732c0..e0 -- the player ship's last position and orientation,
    // written by the space frame through FUN_004c4a30 / FUN_004c4a60.
    float G_PlayerLastPosition[3] = {};
    float G_PlayerLastOrientation[9] = {};

    // DAT_006728e4 / DAT_00672918 -- the selected character's comm and base Costume
    // (FUN_004c3d80 / FUN_004c3d20), back to back as in retail.
    struct FPlayerCostumes
    {
        Costume Com;  // DAT_006728e4
        Costume Base; // DAT_00672918
    };
    static_assert(sizeof(Costume) == 0x34, "Costume 0x34");
    FPlayerCostumes G_PlayerCostumes = {};

    // DAT_00673384 / DAT_00673388 — generation counters bumped by equip/collision sync.
    unsigned G_EquipListGeneration = 0;
    unsigned G_CollisionListGeneration = 0;

    // DAT_006732f0 -- 0x30 bytes up to DAT_00673320: the selected character's name.
    wchar_t G_PlayerCharacterName[24] = {};

    // DAT_00673320 -- the player's visit map: system (or object) id -> visit flags (retail VC6
    // std::map<unsigned, unsigned char>: head DAT_00673324, size DAT_00673330, node key +0xc,
    // value +0x10). Built by the static initialiser LAB_004c39d0 (FUN_004c5a10) and destroyed by
    // its atexit entry LAB_004c3a00; a namespace-scope std::map has the same lifetime.
    std::map<unsigned, unsigned char> G_PlayerVisitMap;

    // The body FUN_004c3d20 and FUN_004c3d80 share: the four part dwords, the accessory count
    // (+0x30) stored as it is, then that many accessory dwords from +0x10, the signed loop bound
    // re-read from the stored count on every pass. No null test, and the accessories past the
    // count keep their old values. Retail: no clamp to the eight accessory slots; a ninth copies
    // the count over itself and later ones run on past the costume (from the comm costume into
    // the base one).
    void CopyCostume(Costume& Out, Costume const& Source)
    {
        Out.PartIds[0] = Source.PartIds[0];
        Out.PartIds[1] = Source.PartIds[1];
        Out.PartIds[2] = Source.PartIds[2];
        Out.PartIds[3] = Source.PartIds[3];
        Out.AccessoryCount = Source.AccessoryCount;
        unsigned* const OutWords = Out.AccessoryIds;
        unsigned const* const SourceWords = Source.AccessoryIds;
        for (int Index = 0; Index < static_cast<int>(Out.AccessoryCount); ++Index)
            OutWords[Index] = SourceWords[Index];
    }

    // The three members FUN_005aa9c0 writes live on the connect context itself,
    // G_LoginMgr (DAT_0067e7b8, login_mgr.h): Characters (+0x1b4), SaveFailureCount
    // (+0x1cc) and bCharacterInfoArrived (+0x1d4).
}

unsigned GetPlayerShipArchetypeIdRetail()
{
    return G_PlayerShipArchetypeId;
}

EquipDescList& GetPlayerEquipDescListRetail()
{
    return G_PlayerEquipDescList;
}

unsigned GetPlayerEquipRevisionRetail()
{
    // FUN_004c4f90.
    return G_EquipListGeneration;
}

unsigned GetPlayerCollisionRevisionRetail()
{
    // FUN_004c4fa0.
    return G_CollisionListGeneration;
}

bool ModifyPlayerEquipRetail(unsigned short Id, CacheString const* Hardpoint, int Count,
                             float Health, unsigned char bEquipped)
{
    // FUN_004c43d0 (cdecl). Caller: FUN_004c4540 (nn_inventory.cpp).
    EquipDesc* const Item = G_PlayerEquipDescList.find_equipment_item(Id);
    if (Item == nullptr)
        return false;
    ++G_EquipListGeneration; // DAT_00673384

    // The same hardpoint (pointer compare): update the item where it is.
    if (Item->Hardpoint == Hardpoint->Value)
    {
        Item->set_count(Count);
        Item->set_status(Health);
        Item->set_equipped(bEquipped);
        return true;
    }

    // Another hardpoint: the item is removed whole and its copy added back.
    EquipDesc Moved(*Item);
    int const Removed = G_PlayerEquipDescList.remove_equipment_item(Id, -1);
    Moved.set_hardpoint(*Hardpoint);
    Moved.set_count(Count);
    Moved.set_status(Health);
    Moved.set_equipped(bEquipped);
    int const Added = G_PlayerEquipDescList.add_equipment_item(Moved, arch_is_combinable(Moved.ArchId));

    // The GF ObjectShip singleton (FUN_00446be0, DAT_00668a9c): FUN_00447850 / FUN_004477d0.
    FObjectShip* const Ship = GfObjectShip();
    if (Ship != nullptr)
    {
        Ship->UnmountEquipment(Removed, Moved.ArchId, Moved.Id,
                               reinterpret_cast<CacheString const*>(&Moved.Hardpoint),
                               static_cast<unsigned>(Moved.Count));
        Ship->MountEquipment(Added, Moved.ArchId, Moved.Id, Hardpoint, Count, Health);
    }
    return true;
}

EquipDesc* FindPlayerNonMissionEquipRetail(unsigned ArchId)
{
    // FUN_004c45d0.
    EquipDesc* Desc = G_PlayerEquipDescList.traverse_equipment_type(ArchId, nullptr);
    while (Desc != nullptr)
    {
        if (!Desc->bMission)
        {
            return Desc;
        }
        Desc = G_PlayerEquipDescList.traverse_equipment_type(ArchId, Desc);
    }
    return nullptr;
}

void AddPlayerEquipRetail(unsigned char UnusedFlag, EquipDesc const* Desc)
{
    // FUN_004c4170 (cdecl). The first argument is never read.
    (void)UnusedFlag;
    // Bumped before anything else.
    ++G_EquipListGeneration;
    int const Added = G_PlayerEquipDescList.add_equipment_item(*Desc, arch_is_combinable(Desc->ArchId));
    // FUN_004477d0 on the GF ObjectShip singleton.
    FObjectShip* const Ship = GfObjectShip();
    if (Ship != nullptr)
    {
        Ship->MountEquipment(Added, Desc->ArchId, Desc->Id,
                             reinterpret_cast<CacheString const*>(&Desc->Hardpoint), Desc->Count,
                             Desc->Health);
    }
}

bool RemovePlayerEquipRetail(unsigned char UnusedFlag, unsigned short Id, int Count)
{
    // FUN_004c4320 (cdecl). The first argument is never read.
    (void)UnusedFlag;
    EquipDesc* const Item = G_PlayerEquipDescList.find_equipment_item(Id);
    if (Item == nullptr)
        return false;
    // The item is copied before it goes.
    EquipDesc const Removed(*Item);
    ++G_EquipListGeneration;
    int const RemovedIndex = G_PlayerEquipDescList.remove_equipment_item(Id, Count);
    // FUN_00447850 on the GF ObjectShip singleton, with the call's Count.
    FObjectShip* const Ship = GfObjectShip();
    if (Ship != nullptr)
    {
        Ship->UnmountEquipment(RemovedIndex, Removed.ArchId, Removed.Id,
                               reinterpret_cast<CacheString const*>(&Removed.Hardpoint),
                               static_cast<unsigned>(Count));
    }
    return true;
}

std::list<CollisionGroupDesc>& GetPlayerCollisionGroupListRetail()
{
    return G_PlayerCollisionGroups;
}

unsigned const* GetPlayerComCostumePartsRetail()
{
    // FUN_004c3de0 hands out &DAT_006728e4 itself.
    return G_PlayerCostumes.Com.PartIds;
}

int GetPlayerComCostumeAccessoryCountRetail()
{
    return static_cast<int>(G_PlayerCostumes.Com.AccessoryCount);
}

unsigned const* GetPlayerComCostumeAccessoriesRetail()
{
    return G_PlayerCostumes.Com.AccessoryIds;
}

unsigned GetPlayerVoiceIdRetail()
{
    return G_PlayerVoiceId;
}

int GetPlayerRepVibeRetail()
{
    return G_PlayerRepVibe;
}

float GetPlayerHealthScaleRetail()
{
    return G_PlayerHealthScale;
}

int GetPlayerRankLevelRetail()
{
    // FUN_004c55d0.
    return G_PlayerRankLevel;
}

void SetPlayerRankLevelRetail(int Level)
{
    // FUN_004c55e0 (cdecl).
    G_PlayerRankLevel = Level;
}

unsigned GetPlayerCreditsRetail()
{
    return G_PlayerCredits;
}

unsigned GetPlayerStarSystemIdRetail()
{
    return G_PlayerStarSystemId;
}

unsigned GetPlayerBaseIdRetail()
{
    return G_PlayerBaseId;
}

unsigned GetPlayerLocationIdRetail()
{
    return G_PlayerLocationId;
}

void SetPlayerShipArchetypeIdRetail(unsigned ShipArchId)
{
    // FUN_004c3e20 (cdecl).
    if (G_PlayerShipArchetypeId == ShipArchId)
        return;

    // No generation counter here (DAT_00673388 is bumped by FUN_004c3f30 and FUN_004c4000
    // only).
    G_PlayerShipArchetypeId = ShipArchId;
    G_PlayerCollisionGroups.clear();

    if ((ShipArchId & 0x3fffffffU) != 0)
    {
        Archetype::Ship* ShipArch = Archetype::GetShip(ShipArchId);
        if (ShipArch != nullptr)
        {
            ShipArch->get_undamaged_collision_group_list(G_PlayerCollisionGroups);
        }
        else
        {
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: Error retrieving ship type %u",
                  "E:\\FL\\Scratch\\Source\\Client\\nn\\Player.cpp", 0xae, ShipArchId);
        }
    }

    FObjectShip* const Ship = GfObjectShip();
    if (Ship == nullptr)
        return;
    Ship->SetEquipment(&G_PlayerEquipDescList);       // FUN_00448540
    Ship->SetShipArchetype(G_PlayerShipArchetypeId); // FUN_00448680
    Ship->RebuildModel();                             // FUN_00447210 (tail jump)
}

void SyncPlayerEquipDescListRetail(EquipDescList const& Source)
{
    // FUN_004c4070 — the singleton list is emptied first, unconditionally (an inlined erase
    // loop; there is no EquipDescList::clear export), and only then assigned from Source when
    // Source is not the singleton itself.
    G_PlayerEquipDescList.Items.clear();
    if (&Source != &G_PlayerEquipDescList)
        G_PlayerEquipDescList.Items = Source.Items;
    ++G_EquipListGeneration;
}

void SyncPlayerCollisionGroupListRetail(std::list<CollisionGroupDesc> const& Source)
{
    // FUN_004c3f30 (cdecl), with VC6 std::list::operator= inlined.
    if (&Source != &G_PlayerCollisionGroups)
        G_PlayerCollisionGroups = Source;
    ++G_CollisionListGeneration;
    // thunk_FUN_00447210 (tail jump).
    FObjectShip* const Ship = GfObjectShip();
    if (Ship != nullptr)
        Ship->RebuildModel();
}

void SetPlayerBaseCostumeRetail(Costume const& Source)
{
    // FUN_004c3d20.
    CopyCostume(G_PlayerCostumes.Base, Source);
}

void SetPlayerComCostumeRetail(Costume const& Source)
{
    // FUN_004c3d80.
    CopyCostume(G_PlayerCostumes.Com, Source);
}

void SetPlayerVoiceIdRetail(unsigned VoiceId)
{
    G_PlayerVoiceId = VoiceId;
}

void SetPlayerRepVibeRetail(int Vibe)
{
    G_PlayerRepVibe = Vibe;
}

void SetPlayerHealthScaleRetail(float Scale)
{
    G_PlayerHealthScale = Scale;
}

void SetPlayerCreditsRetail(unsigned Credits, int bFromCharacterSelect)
{
    // FUN_004c4cf0 (cdecl). Only the low byte of bFromCharacterSelect is tested. Otherwise
    // (IClient SETCASH) UI sound slot 0x26 plays only when the new amount is greater than the
    // old one, a signed compare.
    if (bFromCharacterSelect == 0
        && static_cast<int>(Credits) > static_cast<int>(G_PlayerCredits))
        PlayOrRewindUiSoundSlot(0x26);
    G_PlayerCredits = Credits;
}

void SetPlayerNumKillsRetail(unsigned Kills)
{
    // FUN_004c4d70.
    G_PlayerNumKills = Kills;
}

void SetPlayerMissionSuccessesRetail(unsigned Successes)
{
    // FUN_004c4d80.
    G_PlayerMissionSuccesses = Successes;
}

void SetPlayerMissionFailuresRetail(unsigned Failures)
{
    // FUN_004c4d90.
    G_PlayerMissionFailures = Failures;
}

namespace
{
    // The tail FUN_004c4840 and FUN_004c4910 share byte for byte: the system's own visit byte
    // (ISystem vt+0x48); with neither 0x80 nor 0x01 set, bit 0 is added and the result goes
    // through the body of FUN_004c4e00 with the server notification on.
    void MarkPlayerSystemVisited(unsigned SystemId)
    {
        Universe::ISystem const* const System = Universe::get_system(SystemId);
        if (System == nullptr)
            return;
        unsigned char Flags = System->GetVisit();
        if ((Flags & 0x81u) != 0)
            return;
        Flags = static_cast<unsigned char>(Flags | 1u);
        SetPlayerVisitFlagsRetail(G_PlayerStarSystemId, Flags, true);
    }
}

void SetPlayerStarSystemIdRetail(unsigned SystemId)
{
    // FUN_004c4840.
    G_PlayerStarSystemId = SystemId;
    MarkPlayerSystemVisited(SystemId);
}

void SetPlayerBaseIdRetail(unsigned BaseId)
{
    // FUN_004c4910 (retail takes the id through a pointer). The base's system is IBase vt+0x10;
    // neither lookup is null-tested, as retail.
    G_PlayerBaseId = BaseId;
    if ((BaseId & 0x3fffffffU) == 0)
        return;
    Universe::IBase* const Base = Universe::get_base(BaseId);
    G_PlayerStarSystemId = Base->SystemId;
    MarkPlayerSystemVisited(G_PlayerStarSystemId);
}

bool GetPlayerVisitFlagsRetail(unsigned Id, unsigned char* OutFlags, unsigned Arg3,
                               unsigned Arg4)
{
    // FUN_004c4da0: lower_bound (FUN_0052c7a0), then the key test. The third and fourth
    // arguments are never read.
    (void)Arg3;
    (void)Arg4;
    std::map<unsigned, unsigned char>::const_iterator const It = G_PlayerVisitMap.find(Id);
    if (It == G_PlayerVisitMap.end())
        return false;
    *OutFlags = It->second;
    return true;
}

void SetPlayerVisitFlagsRetail(unsigned Id, unsigned char Flags, bool bNotifyServer)
{
    // FUN_004c4e00. The find is FUN_00432240.
    std::map<unsigned, unsigned char>::iterator const Found = G_PlayerVisitMap.find(Id);
    if (Found != G_PlayerVisitMap.end() &&
        static_cast<unsigned char>(Found->second & Flags) == Flags)
        return;

    // Insert (FUN_0046d6d0), then a plain store, not an OR with what was there.
    std::pair<std::map<unsigned, unsigned char>::iterator, bool> const Inserted =
        G_PlayerVisitMap.insert(std::make_pair(Id, static_cast<unsigned char>(0)));
    Inserted.first->second = Flags;

    if (!bNotifyServer)
        return;
    // IServer vt+0x11c.
    unsigned char Record[9];
    int const Count = 1;
    memcpy(Record, &Count, 4);
    memcpy(Record + 4, &Id, 4);
    Record[8] = Flags;
    G_Server->SetVisitedState(GetPlayerConnectionId(), Record, 9);
}

void ClearPlayerVisitMapRetail()
{
    // The map clear FUN_004c4a80 and FUN_004c4bc0 inline.
    G_PlayerVisitMap.clear();
}

void ClearPlayerShipArchAndHealthRetail()
{
    // FUN_004c4a80 only: two plain stores, not the setters.
    G_PlayerShipArchetypeId = 0;   // DAT_0067337c
    G_PlayerHealthScale = 0.0f;    // DAT_00673380
}

void SetPlayerCollisionGroupHealthRetail(unsigned short Id, float Health)
{
    // FUN_004c4000 (cdecl). The counter is bumped whether or not one matched.
    for (std::list<CollisionGroupDesc>::iterator It = G_PlayerCollisionGroups.begin();
         It != G_PlayerCollisionGroups.end(); ++It)
    {
        if (It->Id == Id)
        {
            It->Health = Health;
            break;
        }
    }
    ++G_CollisionListGeneration;
}

void SetPlayerLocationIdRetail(unsigned LocationId)
{
    G_PlayerLocationId = LocationId;
}

float const* GetPlayerLastPositionRetail()
{
    // FUN_004c4a20.
    return G_PlayerLastPosition;
}

void SetPlayerLastPositionRetail(float const* Position)
{
    // FUN_004c4a30.
    G_PlayerLastPosition[0] = Position[0];
    G_PlayerLastPosition[1] = Position[1];
    G_PlayerLastPosition[2] = Position[2];
}

float const* GetPlayerLastOrientationRetail()
{
    // FUN_004c4a50.
    return G_PlayerLastOrientation;
}

void SetPlayerLastOrientationRetail(float const* Orientation)
{
    // FUN_004c4a60.
    for (int Index = 0; Index < 9; ++Index)
        G_PlayerLastOrientation[Index] = Orientation[Index];
}

void InitCreateParmsScratchRetail(void* ParmsBytes)
{
    // FUN_0052e0a0 (thiscall, returns this) -- the exe's compiled CSimple::CreateParms default
    // constructor: +0x00..+0x58 zero except HitPts +0x0c = -1.0f and the matrix diagonal
    // +0x1c / +0x2c / +0x3c = 1.0f. No null test.
    unsigned* Words = static_cast<unsigned*>(ParmsBytes);
    for (int Index = 0; Index < 0x17; ++Index)
        Words[Index] = 0;
    Words[3] = 0xbf800000;  // -1.0f HitPts
    Words[7] = 0x3f800000;
    Words[0xb] = 0x3f800000;
    Words[0xf] = 0x3f800000;
}

bool ApplyCharacterSelectEquipFields(CCharacterSet::CharacterInfo const* CharInfo)
{
    // FUN_005ab250's extract tail, after the IServer::CharacterSelect wait succeeds. The
    // record's retail offsets are on each line.
    if (CharInfo == nullptr)
        return false;

    SetPlayerVoiceIdRetail(CharInfo->VoiceId);                     // +0x26c FUN_004c4660
    SetPlayerHealthScaleRetail(CharInfo->HullStatus);              // +0x244 FUN_004c4f80
    SetPlayerShipArchetypeIdRetail(CharInfo->ShipArchetype);       // +0x230 FUN_004c3e20

    // +0x254 FUN_004c3f30, then +0x248 FUN_004c4070: the record's own EquipDescList is
    // list-assigned into the client singleton.
    SyncPlayerCollisionGroupListRetail(CharInfo->CollisionGroups);
    SyncPlayerEquipDescListRetail(CharInfo->Equipment);

    SetPlayerCreditsRetail(static_cast<unsigned>(CharInfo->Money), 1);    // +0x234 FUN_004c4cf0

    SetPlayerBaseCostumeRetail(CharInfo->BaseCostume);             // +0x2a8 FUN_004c3d20
    SetPlayerComCostumeRetail(CharInfo->ComCostume);               // +0x274 FUN_004c3d80

    unsigned BaseId = CharInfo->BaseId;                            // +0x264
    if ((BaseId & 0x3fffffffU) != 0)
    {
        SetPlayerStarSystemIdRetail(0);
        SetPlayerBaseIdRetail(BaseId);
    }
    else
    {
        SetPlayerBaseIdRetail(0);
        SetPlayerStarSystemIdRetail(CharInfo->SystemId);           // +0x260
    }

    SetPlayerLocationIdRetail(CharInfo->LocationId);               // +0x268 FUN_004c4a10

    // An empty name is the _Nullstr, i.e. L"". Unbounded, as retail (the buffer is 24 wchars).
    wcscpy(G_PlayerCharacterName, CharInfo->Name.c_str());

    return true;
}

void CharacterSelect_ReceiveCharacterInfo(CCharacterSet const& Set)
{
    // FUN_005aa9c0 (thiscall on DAT_0067e7b8), reached from IClient slot 13 (LAB_005a8220).
    G_LoginMgr.bCharacterInfoArrived = 1;

    // Each record is copied into a fresh one (operator new(0x2dc), the ctor inlined) and
    // appended (FUN_005ae700). The node's Data is used without a null test.
    MetaNode<CCharacterSet::CharacterInfo>* Node = nullptr;
    while (Set.Characters.traverse(Node, true))
    {
        CCharacterSet::CharacterInfo const* Source = Node->Data;
        CCharacterSet::CharacterInfo* Copy = new CCharacterSet::CharacterInfo();
        Copy->Id = Source->Id;
        Copy->bIsRestart = Source->bIsRestart;
        Copy->bIsAutoSave = Source->bIsAutoSave;
        Copy->Name = Source->Name;
        Copy->Description = Source->Description;
        Copy->DescripStrId = Source->DescripStrId;
        Copy->TimestampLow = Source->TimestampLow;
        Copy->TimestampHigh = Source->TimestampHigh;
        Copy->ShipArchetype = Source->ShipArchetype;
        Copy->Money = Source->Money;
        Copy->NumKills = Source->NumKills;
        Copy->NumMissionSuccesses = Source->NumMissionSuccesses;
        Copy->NumMissionFailures = Source->NumMissionFailures;
        Copy->HullStatus = Source->HullStatus;
        Copy->Equipment = Source->Equipment;
        Copy->CollisionGroups = Source->CollisionGroups;
        Copy->SystemId = Source->SystemId;
        Copy->BaseId = Source->BaseId;
        Copy->LocationId = Source->LocationId;
        Copy->VoiceId = Source->VoiceId;
        Copy->Rank = Source->Rank;
        CharacterInfoCopyCostume(Copy->ComCostume, Source->ComCostume);   // FUN_0053c250
        CharacterInfoCopyCostume(Copy->BaseCostume, Source->BaseCostume); // FUN_0053c250
        G_LoginMgr.Characters.append(Copy);
    }

    // +0x1cc = the set's +0x14.
    G_LoginMgr.SaveFailureCount = Set.SaveFailureCount;
}

wchar_t* GetPlayerCharacterNameRetail()
{
    return G_PlayerCharacterName;
}

OwnerList<CCharacterSet::CharacterInfo>& CharacterSelect_GetConnectCharacters()
{
    return G_LoginMgr.Characters;
}

void NotifyCharacterSelectComplete(unsigned const Response4[4])
{
    // FUN_005ab380 after its first store (CLoginMgr+0x21c = 1, done by
    // CLoginMgr::OnCharacterSelectResponse). The response is read without a null test.
    SetPlayerRepVibeRetail(static_cast<int>(Response4[0])); // FUN_004c4680

    if (!SinglePlayer())
    {
        Reputation::Vibe::SetClientID(static_cast<int>(Response4[0]), GetPlayerConnectionId());
    }

    // Both paths store +8 / +0xc into DAT_0067ecd8 / DAT_0067ecdc, the low and high dwords of
    // the client clock double FUN_005b2360 advances every frame (client_timing.cpp), copied bit
    // for bit.
    double ClientTime = 0.0;
    memcpy(&ClientTime, &Response4[2], sizeof(ClientTime));
    SeedClientAccumulatedTime(ClientTime);
}
