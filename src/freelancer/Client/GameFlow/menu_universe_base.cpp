// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "GameFlow/menu_universe_base.h"

#include "NeuroNet/Economy/client_base_market.h"
#include "GameFlow/mirror_property_bucket.h" // FLocationBaseRuntime::MissionOffers (+0x00)
#include "Common/Audio/menu_sound_arch.h"
#include "Common/Audio/sound_config.h"
#include "Interface/Pages/character_select_equip.h" // SetPlayerBaseIdRetail / SetPlayerLocationIdRetail
#include "GameFlow/gameflow.h"               // SetGameflowLocationBase (FUN_0043d590)
#include "Network/login_mgr.h"              // WaitForServerResponse (FUN_005a89b0), G_bServerDisconnected
#include "Core/main_command_handler.h"   // SetClientQuitRequested (FUN_005b2c50)
#include "Interface/win3d.h"                  // Win3DFindByName (FUN_0059da10)
#include "Interface/wincontrol_base.h"        // WinControl::SetProperty (slot 42)
#include "GameFlow/nav_bar.h"                // NavBarDestroy (FUN_00442130)
#include "NeuroNet/nn_arrival_text.h"        // NnArrivalTextDestroy (FUN_0045b380)
#include "NeuroNet/nn_story_star.h"          // StoryStarForgetLastObjective (FUN_004b9b30)
#include "Space/player_spawn.h"           // GetPlayerConnectionId (DAT_00673344)
#include "GameFlow/room_control.h"
#include "Network/server_connect.h"         // GetLocalServerInterface (DAT_0067ecd0)
#include "Interface/win3d_renderables.h"

#include "common_misc.h"

#include "DALib.h"
#include "render/rendcomp.h"
#include "common_archetype.h"
#include "Common/Graphics/client_mesh_load.h"
#include "common_csys.h"
#include "common_goods.h"
#include "common_id.h"
#include "common_iface.h"           // ScriptBehavior (the room state's +0x18 bucket)
#include "common_ini.h"
#include "common_server.h"          // IServer
#include "common_universe.h"
#include "dacom.h"

#include <list>
#include <map>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

namespace
{
    // this+0x58 (DAT_00668760) -- raised by the base enter FUN_005588a0; while it is set
    // FUN_0043bab0 keeps a music / ambience whose loop flag is clear, and it clears the byte
    // itself. No other writer.
    static bool G_bLocationKeepAudio = false;

    // ------------------------------------------------------------------
    // LocationController registry — FUN_004393b0.
    //
    // Retail keeps a list of per-base records at this+0x04, each carrying the BaseData* and a
    // std::list of four-dword per-room records `{RoomData*, 0, 0, 0}`. Two maps index them:
    // this+0x20 by BaseData->+0x00 (BaseId) and this+0x0c by RoomData->+0x04, which
    // `BaseData::read_Room_block` fills with `CreateID("%X_%s", BaseId, nickname)` — the
    // location id `FUN_0043bab0` is called with.
    //
    // Rooms live in a std::list, not a vector: the room map stores pointers into it.
    // ------------------------------------------------------------------
    // FUN_004397a0 allocates the base runtime with `operator new(0x4c)` and three sub-object
    // ctors (FUN_0043c0d0 / FUN_0043c310 / FUN_0042abc0), then writes +0x44 = -1 and +0x48 = 0.
    // The two bytes at +0x14 and +0x2c are MirrorDatabase "synchronised" flags: the IClient
    // completion slots set them (FUN_0043aaf0 / FUN_0043a860) while FUN_004397a0 waits on each
    // through FUN_005a89b0; one still clear after the wait ends the base load (quit in single
    // player, disconnect otherwise).
    struct FLocationBaseRuntime
    {
        // +0x00..+0x18 — the MissionVendorProperties cache (FUN_0043c0d0). Its +0x14 byte
        // (bComplete) is what FUN_0043aaf0 sets. Filled by the IClient handlers FUN_004378f0
        // -> FUN_0043a960 (upsert) / LAB_004378d0 -> FUN_0043a9f0 (erase), client_iclient.cpp.
        // Read by NN_MissionVendor (FUN_0048b6e0 / FUN_0048c770).
        TMirrorPropertyBucket<MissionVendorProperties> MissionOffers; // +0x00
        // +0x18..+0x30 — the NewsBroadcastProperties cache (FUN_0043c310; MetaList vftable
        // 0x005cb02c). Its +0x14 byte (Runtime+0x2c) is what FUN_0043a860 sets. Filled the same
        // way (FUN_0043a6c0 / FUN_0043a750). Read by NN_News (FUN_004a4fd0 through FUN_0043a630).
        TMirrorPropertyBucket<NewsBroadcastProperties> News;          // +0x18
        FClientMarket Market;     // +0x30 — FUN_0042abc0, filled by FUN_00437de0 (client_base_market.h)
        unsigned NewsId;          // +0x44 — the randomly picked news item, 0xffffffff = none
        unsigned StartLocationId; // +0x48 — where the base drops the player
    };

    // Retail room record, from FUN_00439d80: +0x00 RoomData*, +0x04 a 0x48-byte state object
    // (three MirrorDatabase flags at +0x14 / +0x2c / +0x44), +0x08 the 0x13ac-byte room runtime
    // FUN_00449260 builds — whose +0x500 is the camera FUN_00438c90 hands to the sound mixer —
    // and +0x0c the visit refcount.
    // The room's MirrorDatabase state (0x48 bytes, allocated by FUN_00439d80): three buckets
    // the server fills through IClient slots 30..37, each with its completion byte. Destroyed by
    // FUN_0043a140 (the three bucket teardowns, last to first) + operator delete.
    struct FLocationRoomState
    {
        TMirrorPropertyBucket<CharacterProperties> Characters;          // +0x00 FUN_0043c550
        TMirrorPropertyBucket<ScriptBehavior> ScriptBehaviors;          // +0x18 FUN_0043c790
        TMirrorPropertyBucket<AmbientScriptProperties> AmbientScripts;  // +0x30 FUN_0043c9d0
    };

    struct FLocationRoomEntry
    {
        RoomData* Room;         // +0x00
        FLocationRoomState* State; // +0x04 FUN_00439d80
        FRoomRuntime* Runtime;  // +0x08 — FUN_00449260 (Camera* at Runtime+0x500)
        int RefCount;           // +0x0C
    };

    // Retail base record: +0x00 BaseData*, +0x04 runtime, +0x08 refcount, +0x10 the room list.
    struct FLocationBaseEntry
    {
        BaseData* Base;                       // +0x00
        FLocationBaseRuntime* Runtime;        // +0x04
        int RefCount;                         // +0x08
        std::list<FLocationRoomEntry> Rooms;  // +0x10
    };

    static std::list<FLocationBaseEntry> G_LocationBases;              // this+0x04
    static std::map<unsigned, FLocationRoomEntry*> G_LocationRooms;    // this+0x0c
    static std::map<unsigned, FLocationBaseEntry*> G_LocationBaseById; // this+0x20

    void UnloadLocationBase(unsigned BaseId);
    FLocationBaseEntry* FindLocationBaseEntry(unsigned BaseId);
    FLocationRoomEntry* FindLocationRoomEntry(unsigned LocationId);
    void EnsureRoomRuntime(FLocationRoomEntry* Entry, unsigned LocationId);
    IServer* GetLocationServer();

    // LocationController scalars. this+0x34 is the base the player is in, this+0x38 the
    // location; this+0x59 is raised for the length of a room transition (FUN_0043b6b0).
    static unsigned G_LocationCurrentBase = 0;  // this+0x34
    static unsigned G_LocationCurrentRoom = 0;  // this+0x38
    // this+0x3c / +0x40 -- the base and room records of the current location, set by
    // FUN_0043b6b0 and cleared with +0x38 whenever the location is left. FUN_0043bbc0 draws
    // through +0x40's runtime.
    static FLocationBaseEntry* G_LocationCurrentBaseEntry = nullptr; // this+0x3c
    static FLocationRoomEntry* G_LocationCurrentRoomEntry = nullptr; // this+0x40
    // this+0x44..+0x50 -- a queued location change (FUN_0043ba90): the location, whether its
    // FUN_0043bab0 audio runs too, the time waited and the time to wait. FUN_00439540 counts
    // +0x4c up and makes the change once it reaches +0x50.
    static unsigned G_LocationPendingRoom = 0;      // this+0x44
    static bool G_bLocationPendingAudio = false;    // this+0x48
    static float G_LocationPendingElapsed = 0.0f;   // this+0x4c
    static float G_LocationPendingDelay = 0.0f;     // this+0x50
    // this+0x54 -- the loaded base's solar object id (CreateID of the nickname of the system
    // object whose "base =" is it), written by FUN_004397a0, read through FUN_0043bc90.
    static unsigned G_LocationBaseSolarId = 0;      // this+0x54
    static bool G_bLocationInTransition = false; // this+0x59 (DAT_00668761)
    // this+0x5a -- zeroed by the LocationController ctor, set by IntroFocus::Init, cleared when
    // the intro base is left. The controller reads it through `this+0x5a`, so a search for the
    // global address does not find those reads.
    static bool G_bLocationNoServerNotify = false;  // this+0x5a

    // The controller's destructor FUN_00438fd0, run by the atexit entry LAB_00438de0 that the
    // static initializer LAB_00438dc0 registers after constructing DAT_00668708: FUN_00439260
    // (ClearClientLocationMaps) first, then the base map (+0x20), the room map (+0x0c) and the
    // base list (+0x04) freed. The port's containers above are those three; declared before
    // this object, their destructors run after its own, in the same order as retail's tail.
    struct FLocationControllerTeardown
    {
        ~FLocationControllerTeardown()
        {
            ClearClientLocationMaps();
        }
    };
    static FLocationControllerTeardown G_LocationControllerTeardown;

    void ApplyLocationAudio(unsigned LocationId)
    {
        // FUN_0043bab0. FUN_00439610's lookup inlined: a miss FDUMPs ("Could not find location
        // id", line 0x110) and the record is read through null (retail).
        RoomData* const Room = FindLocationRoomEntry(LocationId)->Room;

        // Unless this+0x58 is set, a non-looping music / ambience is cleared.
        unsigned MusicId = Room->Music.Id;
        unsigned AmbientId = Room->Ambient.Id;
        if (!G_bLocationKeepAudio)
        {
            if (!Room->bMusicLoop)
                MusicId = 0;
            if (!Room->bAmbientLoop)
                AmbientId = 0;
        }

        SetClientBackgroundMusic(MusicId, Room->bMusicLoop ? 1u : 0u, -1.f);
        SetClientBackgroundAmbient(AmbientId, Room->bAmbientLoop ? 1u : 0u, -1.f);
        G_bLocationKeepAudio = false;

        // music_fade_time; the fades go through the Gf script manager DAT_0066d2d0.
        float FadeSeconds = 0.f;
        GetClientDuckingConfig(nullptr, nullptr, &FadeSeconds);
        SnapClientAudioFade(-96.f);
        SetClientAudioFade(0.f, FadeSeconds);
    }
}

void SetClientLocationNoServerNotify(bool bDisabled)
{
    G_bLocationNoServerNotify = bDisabled;
}

void ApplyClientLocationAudio(unsigned LocationId)
{
    // FUN_0043bab0, for callers outside the controller.
    ApplyLocationAudio(LocationId);
}

void SetClientLocationKeepAudio()
{
    // Inlined in FUN_005588a0.
    G_bLocationKeepAudio = true;
}

void BuildClientLocationMaps()
{
    // FUN_004393b0. Only caller FUN_00558850, paired with the teardown FUN_00439260 in
    // FUN_00558870; this function does not clear first. BaseDataList_get() is not null-tested
    // and no entry is skipped.
    BaseDataList* const BaseList = BaseDataList_get();
    std::list<BaseData*>* const BaseDataItems = BaseList->get_base_data_list();
    for (std::list<BaseData*>::iterator BaseIt = BaseDataItems->begin();
         BaseIt != BaseDataItems->end(); ++BaseIt)
    {
        BaseData* const Base = *BaseIt;
        G_LocationBases.push_back(FLocationBaseEntry());
        FLocationBaseEntry& Entry = G_LocationBases.back();
        Entry.Base = Base;
        Entry.Runtime = nullptr;
        Entry.RefCount = 0;

        for (std::list<RoomData*>::iterator RoomIt = Base->Rooms.begin();
             RoomIt != Base->Rooms.end(); ++RoomIt)
        {
            FLocationRoomEntry RoomEntry;
            RoomEntry.Room = *RoomIt;
            RoomEntry.State = nullptr;
            RoomEntry.Runtime = nullptr;
            RoomEntry.RefCount = 0;
            Entry.Rooms.push_back(RoomEntry);
        }
    }

    // Unique-key inserts (FUN_0046d6d0): an existing key is kept.
    for (std::list<FLocationBaseEntry>::iterator It = G_LocationBases.begin();
         It != G_LocationBases.end(); ++It)
    {
        G_LocationBaseById.insert(
            std::map<unsigned, FLocationBaseEntry*>::value_type(It->Base->BaseId, &(*It)));
        for (std::list<FLocationRoomEntry>::iterator RoomIt = It->Rooms.begin();
             RoomIt != It->Rooms.end(); ++RoomIt)
        {
            G_LocationRooms.insert(std::map<unsigned, FLocationRoomEntry*>::value_type(
                RoomIt->Room->RoomId, &(*RoomIt)));
        }
    }
}

void ClearClientLocationMaps()
{
    // FUN_00439260. Each base's still-referenced rooms are unloaded, then the base itself.
    for (std::list<FLocationBaseEntry>::iterator BaseIt = G_LocationBases.begin();
         BaseIt != G_LocationBases.end(); ++BaseIt)
    {
        for (std::list<FLocationRoomEntry>::iterator RoomIt = BaseIt->Rooms.begin();
             RoomIt != BaseIt->Rooms.end(); ++RoomIt)
        {
            unsigned const LocationId = RoomIt->Room->RoomId;
            FLocationRoomEntry* const RoomEntry = FindLocationRoomEntry(LocationId);
            if (RoomEntry->RefCount > 0)
            {
                UnloadClientLocationRoomState(LocationId);
                RoomEntry->RefCount = 0;
            }
        }

        // FUN_00439590 inlined (FDUMP at line 0x100 on a miss).
        unsigned const BaseId = BaseIt->Base->BaseId;
        FLocationBaseEntry* const BaseEntry = FindLocationBaseEntry(BaseId);
        if (BaseEntry->RefCount > 0)
        {
            UnloadLocationBase(BaseId);
            BaseEntry->RefCount = 0;
        }
    }

    G_LocationBases.clear();
    G_LocationRooms.clear();
    G_LocationBaseById.clear();

    NavBarDestroy();
}

RoomData* LookupClientLocationRoom(unsigned LocationId)
{
    // Port-only: unlike retail's inlined lookup, a miss returns null.
    std::map<unsigned, FLocationRoomEntry*>::const_iterator It = G_LocationRooms.find(LocationId);
    if (It == G_LocationRooms.end() || It->second == nullptr)
        return nullptr;
    return It->second->Room;
}

BaseData* LookupClientLocationBase(unsigned BaseId)
{
    // FUN_0043a2b0 -- the same inlined body as FUN_00439590.
    return FindLocationBaseEntry(BaseId)->Base;
}

unsigned GetClientLocationBaseCount()
{
    return static_cast<unsigned>(G_LocationBaseById.size());
}

unsigned GetClientLocationRoomCount()
{
    return static_cast<unsigned>(G_LocationRooms.size());
}

// ----------------------------------------------------------------------------------------
// LocationController: base entry (FUN_0043b290 / FUN_004397a0 / FUN_00439590 / FUN_0043b3e0).
// ----------------------------------------------------------------------------------------

namespace
{
    char const* const kLocationControllerCpp =
        "E:\\FL\\Scratch\\Source\\Client\\Gf\\LocationController.cpp";

    // FUN_00439590 — lower_bound on the base map, FDUMP at LocationController.cpp:0x100 on miss.
    FLocationBaseEntry* FindLocationBaseEntry(unsigned BaseId)
    {
        std::map<unsigned, FLocationBaseEntry*>::const_iterator It =
            G_LocationBaseById.find(BaseId);
        if (It != G_LocationBaseById.end())
            return It->second;
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Could not find base id %d\n",
              kLocationControllerCpp, 0x100, BaseId);
        return nullptr;
    }

    // FUN_00439610 — same shape on the room map, FDUMP at LocationController.cpp:0x110.
    FLocationRoomEntry* FindLocationRoomEntry(unsigned LocationId)
    {
        std::map<unsigned, FLocationRoomEntry*>::const_iterator It =
            G_LocationRooms.find(LocationId);
        if (It != G_LocationRooms.end())
            return It->second;
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Could not find location id %d\n",
              kLocationControllerCpp, 0x110, LocationId);
        return nullptr;
    }

    // FUN_00439d80 -- load a room: its MirrorDatabase state, the server's location info, the
    // 0x13ac room runtime (FUN_00449260 + FUN_004496b0; Camera* at +0x500), the sync of the
    // state's three buckets, the scene scripts and the NavBar. Callers do the room lookup
    // themselves and call it only at a zero count, when +0x04 / +0x08 are clear: retail
    // allocates both unconditionally.
    void EnsureRoomRuntime(FLocationRoomEntry* Entry, unsigned LocationId)
    {
        // Retail hands this base id to FUN_004496b0; the port's CreateRoomRuntime reads the same
        // RoomData +0x00 itself, so only the lookup (and its FDUMP on a miss) is kept.
        unsigned const RoomBaseId = FindLocationBaseEntry(Entry->Room->BaseId)->Base->BaseId;
        (void)RoomBaseId;
        Entry->State = new FLocationRoomState();
        Entry->State->Characters.bComplete = 0;
        Entry->State->ScriptBehaviors.bComplete = 0;
        Entry->State->AmbientScripts.bComplete = 0;
        IServer* const Server = GetLocationServer();
        if (Server != nullptr)
            Server->LocationInfoRequest(LocationId, GetPlayerConnectionId(), true);
        Entry->Runtime = CreateRoomRuntime(Entry->Room);
        // The three buckets are synchronized in turn. A flag still clear after the wait ends the
        // load without the scenes: quit in single player, disconnect otherwise. A set flag stays
        // set (the IClient completion slots wrote it).
        if (!G_bLocationNoServerNotify)
        {
            unsigned char* const SyncFlags[3] = {
                &Entry->State->Characters.bComplete,
                &Entry->State->ScriptBehaviors.bComplete,
                &Entry->State->AmbientScripts.bComplete,
            };
            int SyncIndex = 0;
            for (SyncIndex = 0; SyncIndex < 3; ++SyncIndex)
            {
                unsigned char* const Flag = SyncFlags[SyncIndex];
                if (WaitForServerResponse(Flag, 5.0f, 60.0f) != 1)
                {
                    FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kLocationControllerCpp, 0x108,
                          "Synchronization failed\n");
                    *Flag = 0;
                }
                if (*Flag == 0)
                {
                    if (SinglePlayer())
                        SetClientQuitRequested(true); // FUN_005b2c50
                    else
                        G_bServerDisconnected = true; // DAT_0067ecbe
                    return;
                }
            }
        }
        Entry->Runtime->PlayScenes();
        NavBarEnsure();
    }

    // FUN_004397a0 — load a base: its runtime, RoomData::load on every room, and (with +0x5a
    // clear) the server's base info, the MirrorDatabase sync of the mission and news buckets,
    // the random news item, the market, the base's solar object id (+0x54) and its visit flags.
    // Callers do the base lookup themselves and load only at a zero count, after FUN_00439b50
    // cleared +0x04.
    void LoadLocationBase(FLocationBaseEntry* Entry, unsigned BaseId)
    {
        Entry->Runtime = new FLocationBaseRuntime(); // buckets' ctors clear +0x14 / +0x2c
        Entry->Runtime->NewsId = 0xffffffffu;
        Entry->Runtime->StartLocationId = 0;

        for (std::list<FLocationRoomEntry>::iterator It = Entry->Rooms.begin();
             It != Entry->Rooms.end(); ++It)
        {
            It->Room->load();
        }

        if (G_bLocationNoServerNotify)
            return;

        Entry->Runtime->MissionOffers.bComplete = 0;
        Entry->Runtime->News.bComplete = 0;
        IServer* const Server = GetLocalServerInterface();
        if (Server != nullptr)
            Server->BaseInfoRequest(BaseId, GetPlayerConnectionId(), true);

        // Same synchronisation as EnsureRoomRuntime, for the two base buckets.
        unsigned char* const SyncFlags[2] = {
            &Entry->Runtime->MissionOffers.bComplete,
            &Entry->Runtime->News.bComplete,
        };
        int SyncIndex = 0;
        for (SyncIndex = 0; SyncIndex < 2; ++SyncIndex)
        {
            unsigned char* const Flag = SyncFlags[SyncIndex];
            if (WaitForServerResponse(Flag, 5.0f, 60.0f) != 1)
            {
                FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kLocationControllerCpp, 0x108,
                      "Synchronization failed\n");
                *Flag = 0;
            }
            if (*Flag == 0)
            {
                if (SinglePlayer())
                    SetClientQuitRequested(true); // FUN_005b2c50
                else
                    G_bServerDisconnected = true; // DAT_0067ecbe
                return;
            }
        }

        // +0x44 = the key of a random news entry; Pick = rand() * Count / 32768 as a truncating
        // signed divide. No entries -> -1.
        TMirrorPropertyBucket<NewsBroadcastProperties>& News = Entry->Runtime->News;
        MetaNode<NewsBroadcastProperties>* Node = nullptr;
        int NewsCount = 0;
        while (News.List.traverse(Node, true))
            ++NewsCount;
        if (NewsCount > 0)
        {
            int const Pick = (rand() * NewsCount) / 32768;
            MetaNode<NewsBroadcastProperties>* Picked = nullptr;
            int Index = 0;
            Node = nullptr;
            while (News.List.traverse(Node, true))
            {
                if (Index == Pick)
                {
                    Picked = Node;
                    break;
                }
                ++Index;
            }
            // Retail reads the element through the (never null: Pick < Count) pick.
            Entry->Runtime->NewsId = static_cast<unsigned>(Picked->Data->Key);
        }
        else
        {
            Entry->Runtime->NewsId = 0xffffffffu;
        }

        // No explicit base: the current one (FUN_0043b630).
        ClientMarketFill(&Entry->Runtime->Market, Entry->Base->PriceVariance, nullptr);

        // With a server: the first object of the player's system (not null-tested) whose base
        // (+0x88) is this one; none, or no server -> 0.
        unsigned SolarId = 0;
        if (GetLocalServerInterface() != nullptr)
        {
            Universe::ISystem const* const System =
                Universe::get_system(GetPlayerStarSystemIdRetail());
            Universe::FSystemObject Object;
            Object.Nickname.Length = 0;
            Object.Nickname.Data[0] = '\0';
            Object.Reputation.Length = 0;
            Object.Reputation.Data[0] = '\0';
            Object.NextRing.Length = 0;
            Object.NextRing.Data[0] = '\0';
            Object.PrevRing.Length = 0;
            Object.PrevRing.Data[0] = '\0';
            unsigned const ObjectCount = (unsigned)System->GetObjectCount();
            unsigned ObjectIndex = 0;
            for (ObjectIndex = 0; ObjectIndex < ObjectCount; ++ObjectIndex)
            {
                if (!System->GetSystemObject(ObjectIndex, &Object))
                    continue;
                if (Object.BaseId == BaseId)
                {
                    SolarId = CreateID(Object.Nickname.Data);
                    break;
                }
            }
        }
        // The visit flags are read into a zeroed byte (the result ignored), | 0x1e stored back
        // with the server told.
        G_LocationBaseSolarId = SolarId;
        if (SolarId != 0)
        {
            unsigned char VisitFlags = 0;
            GetPlayerVisitFlagsRetail(SolarId, &VisitFlags, 0, 0);
            VisitFlags = static_cast<unsigned char>(VisitFlags | kVisitExtraInfo | kVisitCommodities | kVisitEquipment | kVisitShips);
            SetPlayerVisitFlagsRetail(SolarId, VisitFlags, true);
        }

        StoryStarForgetLastObjective();
    }

    // DAT_0067ecd0, the IServer every LocationController notification goes to, gated at every
    // site on this+0x5a being clear and the server existing; the session id is DAT_00673344.
    IServer* GetLocationServer()
    {
        if (G_bLocationNoServerNotify)
            return nullptr;
        return GetLocalServerInterface();
    }

    // FUN_00439b50 -- the server told the base's info is no longer wanted, RoomData::unload on
    // every room (the room runtimes are dropped separately by FUN_0043a080 / FUN_00439690 when
    // their count reaches 0), the base runtime destroyed. A lookup miss reads through null, as
    // retail does.
    void UnloadLocationBase(unsigned BaseId)
    {
        FLocationBaseEntry* const Found = FindLocationBaseEntry(BaseId);
        IServer* const Server = GetLocationServer();
        if (Server != nullptr)
            Server->BaseInfoRequest(BaseId, GetPlayerConnectionId(), false);
        for (std::list<FLocationRoomEntry>::iterator It = Found->Rooms.begin();
             It != Found->Rooms.end(); ++It)
        {
            It->Room->unload();
        }
        // FUN_00439c10 is the runtime's destructor: the implicit member destructors in reverse
        // order.
        delete Found->Runtime;
        Found->Runtime = nullptr;
    }
}

// ----------------------------------------------------------------------------------------
// LocationController: small field accessors (FUN_0043a460/a510/a5a0/a630/a860/a8d0/aaf0/ab60).
// Retail resolves the base/room record through the same map lookups as
// FindLocationBaseEntry/FindLocationRoomEntry above (COMDAT-folded onto one map-find helper),
// then reads or writes one field of the record. A miss FDUMPs (line 0x100 for bases, 0x110 for
// rooms) and reads through null; the port makes the same read.
// ----------------------------------------------------------------------------------------

void* GetClientLocationBaseRuntime(unsigned BaseId)
{
    // FUN_0043a8d0. Opaque because FLocationBaseRuntime is a port-local type.
    FLocationBaseEntry* const Entry = FindLocationBaseEntry(BaseId);
    return static_cast<void*>(Entry->Runtime);
}

TMirrorPropertyBucket<MissionVendorProperties>* GetClientLocationBaseMissionOffers(unsigned BaseId)
{
    // FUN_0043a8d0 as NN_MissionVendor uses it: the runtime pointer is the bucket (its +0x00).
    FLocationBaseEntry* const Entry = FindLocationBaseEntry(BaseId);
    return &Entry->Runtime->MissionOffers;
}

TMirrorPropertyBucket<NewsBroadcastProperties>* GetClientLocationBaseNews(unsigned BaseId)
{
    // FUN_0043a630.
    FLocationBaseEntry* const Entry = FindLocationBaseEntry(BaseId);
    return &Entry->Runtime->News;
}

FClientMarket* GetClientLocationBaseMarket(unsigned BaseId)
{
    // FUN_0043a510.
    FLocationBaseEntry* const Entry = FindLocationBaseEntry(BaseId);
    return &Entry->Runtime->Market;
}

BaseData* GetClientCurrentLocationBaseData()
{
    // FUN_0043b630.
    FLocationBaseEntry* const Entry = FindLocationBaseEntry(G_LocationCurrentBase);
    return Entry->Base;
}

void SetClientLocationBaseStartLocation(unsigned BaseId, unsigned LocationId)
{
    // FUN_0043a5a0.
    FLocationBaseEntry* const Entry = FindLocationBaseEntry(BaseId);
    if (Entry->Runtime != nullptr)
        Entry->Runtime->StartLocationId = LocationId;
}

void SetClientLocationBaseMissionListComplete(unsigned BaseId)
{
    // FUN_0043aaf0. Caller: IClient slot 40 (LAB_00437990, ch2 0x10
    // GFCOMPLETEMISSIONCOMPUTERLIST). Only a null runtime is guarded, not a lookup miss.
    FLocationBaseEntry* const Entry = FindLocationBaseEntry(BaseId);
    if (Entry->Runtime != nullptr)
        Entry->Runtime->MissionOffers.bComplete = 1;
}

void UpsertClientLocationBaseMissionOffer(MissionVendorProperties const& Offer)
{
    // FUN_0043a960. Caller: IClient slot 39 (FUN_004378f0).
    FLocationBaseEntry* const Entry = FindLocationBaseEntry(Offer.BaseId);
    if (Entry->Runtime != nullptr)
        Entry->Runtime->MissionOffers.Upsert(Offer);
}

void EraseClientLocationBaseMissionOffer(unsigned BaseId, unsigned long Key)
{
    // FUN_0043a9f0 (the erase is inlined MirrorDatabase.h 0xf6). Caller: IClient slot 38
    // (LAB_004378d0).
    FLocationBaseEntry* const Entry = FindLocationBaseEntry(BaseId);
    if (Entry->Runtime != nullptr)
        Entry->Runtime->MissionOffers.Erase(Key);
}

void UpsertClientLocationBaseNews(NewsBroadcastProperties const& News)
{
    // FUN_0043a6c0. Caller: IClient slot 44 (FUN_00437a70).
    FLocationBaseEntry* const Entry = FindLocationBaseEntry(News.BaseId);
    if (Entry->Runtime != nullptr)
        Entry->Runtime->News.Upsert(News);
}

void EraseClientLocationBaseNews(unsigned BaseId, unsigned long Key)
{
    // FUN_0043a750. Caller: IClient slot 43 (LAB_00437a50).
    FLocationBaseEntry* const Entry = FindLocationBaseEntry(BaseId);
    if (Entry->Runtime != nullptr)
        Entry->Runtime->News.Erase(Key);
}

void SetClientLocationBaseNewsListComplete(unsigned BaseId)
{
    // FUN_0043a860. Caller: IClient slot 45 (LAB_00437b10, ch2 0x0e
    // GFCOMPLETENEWSBROADCASTLIST).
    FLocationBaseEntry* const Entry = FindLocationBaseEntry(BaseId);
    if (Entry->Runtime != nullptr)
        Entry->Runtime->News.bComplete = 1;
}

void* GetClientLocationRoomState(unsigned LocationId)
{
    // FUN_0043ab60.
    FLocationRoomEntry* const Entry = FindLocationRoomEntry(LocationId);
    return static_cast<void*>(Entry->State);
}

namespace
{
    // FUN_0043ab60's shape with the typed state.
    FLocationRoomState* FindClientLocationRoomState(unsigned LocationId)
    {
        FLocationRoomEntry* const Entry = FindLocationRoomEntry(LocationId);
        return Entry->State;
    }
}

void UpsertClientLocationRoomCharacter(CharacterProperties const& Character)
{
    // FUN_0043abf0. The room entry is looked up a second time (FUN_0041bd10) for the
    // RoomControl update (FUN_00449e60).
    FLocationRoomState* const State = FindClientLocationRoomState(Character.RoomId);
    if (State != nullptr)
        State->Characters.Upsert(Character);
    FLocationRoomEntry* const Entry = FindLocationRoomEntry(Character.RoomId);
    if (Entry->Runtime != nullptr)
        Entry->Runtime->UpdateCharacter(Character);
}

void SetClientLocationRoomCharactersComplete(unsigned LocationId)
{
    // FUN_0043ace0.
    FLocationRoomState* const State = FindClientLocationRoomState(LocationId);
    if (State != nullptr)
        State->Characters.bComplete = 1;
}

void UpsertClientLocationRoomScriptBehavior(ScriptBehavior const& Behavior)
{
    // FUN_0043ade0.
    FLocationRoomState* const State = FindClientLocationRoomState(Behavior.LocationId);
    if (State != nullptr)
        State->ScriptBehaviors.Upsert(Behavior);
}

void EraseClientLocationRoomScriptBehavior(unsigned LocationId, unsigned long Key)
{
    // FUN_0043ae70.
    FLocationRoomState* const State = FindClientLocationRoomState(LocationId);
    if (State != nullptr)
        State->ScriptBehaviors.Erase(Key);
}

void SetClientLocationRoomScriptBehaviorsComplete(unsigned LocationId)
{
    // FUN_0043af80.
    FLocationRoomState* const State = FindClientLocationRoomState(LocationId);
    if (State != nullptr)
        State->ScriptBehaviors.bComplete = 1;
}

void UpsertClientLocationRoomAmbientScript(AmbientScriptProperties const& Script)
{
    // FUN_0043b080.
    FLocationRoomState* const State = FindClientLocationRoomState(Script.RoomId);
    if (State != nullptr)
        State->AmbientScripts.Upsert(Script);
}

void EraseClientLocationRoomAmbientScript(unsigned LocationId, unsigned long Key)
{
    // FUN_0043b110.
    FLocationRoomState* const State = FindClientLocationRoomState(LocationId);
    if (State != nullptr)
        State->AmbientScripts.Erase(Key);
}

void SetClientLocationRoomAmbientScriptsComplete(unsigned LocationId)
{
    // FUN_0043b220.
    FLocationRoomState* const State = FindClientLocationRoomState(LocationId);
    if (State != nullptr)
        State->AmbientScripts.bComplete = 1;
}

TMirrorPropertyBucket<CharacterProperties>* GetClientLocationRoomCharacters(unsigned LocationId)
{
    // FUN_0043ab60 again, typed: the state's +0x00 bucket is the state itself.
    FLocationRoomState* const State = FindClientLocationRoomState(LocationId);
    return &State->Characters;
}

TMirrorPropertyBucket<ScriptBehavior>* GetClientLocationRoomScriptBehaviors(unsigned LocationId)
{
    // FUN_0043ad50. No state check.
    FLocationRoomState* const State = FindClientLocationRoomState(LocationId);
    return &State->ScriptBehaviors;
}

TMirrorPropertyBucket<AmbientScriptProperties>* GetClientLocationRoomAmbientScripts(
    unsigned LocationId)
{
    // FUN_0043aff0. No state check.
    FLocationRoomState* const State = FindClientLocationRoomState(LocationId);
    return &State->AmbientScripts;
}

bool IsClientLocationRoomLoaded(unsigned LocationId)
{
    // FUN_0043a340. RoomData +0x08 is RoomData::load's bLoaded (Common .text:06277c53).
    FLocationRoomEntry* const Entry = FindLocationRoomEntry(LocationId);
    return Entry->Room->bLoaded;
}

FRoomRuntime* GetClientCurrentLocationRuntime()
{
    // FUN_0043a460. Only the low 30 bits are tested, but the map is searched with the whole
    // value.
    if ((G_LocationCurrentRoom & 0x3fffffffu) == 0)
        return nullptr;
    FLocationRoomEntry* const Entry = FindLocationRoomEntry(G_LocationCurrentRoom);
    return Entry->Runtime;
}

void UnloadClientLocationRoomState(unsigned LocationId)
{
    // FUN_0043a080. The 0x13ac runtime is released through its deleting dtor, the 0x48-byte
    // state through FUN_0043a140.
    FLocationRoomEntry* Entry = FindLocationRoomEntry(LocationId);
    IServer* const Server = GetLocationServer();
    if (Server != nullptr)
        Server->LocationInfoRequest(LocationId, GetPlayerConnectionId(), false);

    if (Entry->Runtime != nullptr)
        DestroyRoomRuntime(Entry->Runtime);
    Entry->Runtime = nullptr;
    delete Entry->State;
    Entry->State = nullptr;
}

ICamera* GetClientRoomCamera()
{
    // FUN_00438c90 (cdecl on DAT_00668708's fields: +0x38 is DAT_00668740, the room map
    // DAT_00668714). Same low-30-bit test as FUN_0043a460.
    if ((G_LocationCurrentRoom & 0x3fffffffu) == 0)
        return nullptr;
    FLocationRoomEntry* const Entry = FindLocationRoomEntry(G_LocationCurrentRoom);
    if (Entry->Runtime == nullptr)
        return nullptr;
    return GetRoomRuntimeCamera(Entry->Runtime);
}

namespace
{
    // FUN_00439220 (also inlined in FUN_0043b3e0 and FUN_0043b970). NN_Inventory handles
    // property 0x4b in FUN_00479d10 as the ship-dealer reselect block.
    void NotifyInventoryLocationLeft()
    {
        Win3D* const Inventory = Win3DFindByName("NN_Inventory");
        if (Inventory == nullptr)
            return;
        if (Inventory->AsWinControl() == nullptr)
            return;
        Inventory->AsWinControl()->SetProperty(0x4b, 0, 0);
    }

    // FUN_0043b970 -- leave the current location while the base changes. Location is the
    // caller's +0x38; the body re-reads +0x38 itself.
    void LeaveClientLocationRoom(unsigned Location)
    {
        NnArrivalTextDestroy();
        unsigned const Current = G_LocationCurrentRoom;
        FLocationRoomEntry* const Entry = FindLocationRoomEntry(Current);
        Entry->RefCount -= 1;
        if (Entry->RefCount == 0)
        {
            // Retail looks the entry up a second time.
            FLocationRoomEntry* const Again = FindLocationRoomEntry(Current);
            IServer* const Server = GetLocationServer();
            if (Server != nullptr)
                Server->LocationInfoRequest(Current, GetPlayerConnectionId(), false);
            if (Again->Runtime != nullptr)
                DestroyRoomRuntime(Again->Runtime);
            Again->Runtime = nullptr;
            delete Again->State;
            Again->State = nullptr;
        }
        // LocationExit gets the argument, not +0x38.
        IServer* const Server = GetLocationServer();
        if (Server != nullptr)
            Server->LocationExit(Location, GetPlayerConnectionId());
        G_LocationCurrentRoom = 0;
        G_LocationCurrentBaseEntry = nullptr;
        G_LocationCurrentRoomEntry = nullptr;
        NotifyInventoryLocationLeft();
    }

    // FUN_00439690 -- drop one reference to Location; at 0 the server is told and the runtime
    // and state are released, as FUN_0043a080 does.
    void ReleaseClientLocationRoom(unsigned Location)
    {
        FLocationRoomEntry* const Entry = FindLocationRoomEntry(Location);
        Entry->RefCount -= 1;
        if (Entry->RefCount != 0)
            return;
        FLocationRoomEntry* const Again = FindLocationRoomEntry(Location);
        IServer* const Server = GetLocationServer();
        if (Server != nullptr)
            Server->LocationInfoRequest(Location, GetPlayerConnectionId(), false);
        if (Again->Runtime != nullptr)
            DestroyRoomRuntime(Again->Runtime);
        Again->Runtime = nullptr;
        delete Again->State;
        Again->State = nullptr;
    }

    // The base id of the base Location belongs to, through Common's unloaded room data
    // (RoomData+0x00 = base id) and the base map: a pattern FUN_0043b6b0 inlines twice. 0 when
    // Location is 0; an unknown room FDUMPs (line 0x120) and answers 0.
    unsigned LocationBaseOf(unsigned Location)
    {
        if (Location == 0)
            return 0;
        RoomData* const Room = BaseDataList_get()->get_unloaded_room_data(Location);
        if (Room == nullptr)
        {
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: Could not find location %d\n",
                  kLocationControllerCpp, 0x120, Location);
            return 0;
        }
        FLocationBaseEntry* const Entry = FindLocationBaseEntry(Room->BaseId);
        if (Entry == nullptr)
            return 0;
        return Entry->Base->BaseId;
    }
}

void ResetClientLocationController()
{
    // FUN_00439200.
    G_LocationCurrentBase = 0;
    G_LocationCurrentRoom = 0;
    G_LocationCurrentBaseEntry = nullptr;
    G_LocationCurrentRoomEntry = nullptr;
    G_LocationPendingRoom = 0;
    G_bLocationPendingAudio = false;
    G_LocationPendingElapsed = 0.0f;
    G_LocationPendingDelay = 0.0f;
    G_bLocationInTransition = false;
}

void TickClientLocationController(float DeltaSeconds)
{
    // FUN_00439540.
    unsigned const Pending = G_LocationPendingRoom;
    if ((Pending & 0x3fffffffu) == 0)
        return;
    // x87 compare: less or unordered keeps waiting.
    G_LocationPendingElapsed += DeltaSeconds;
    if (G_LocationPendingElapsed < G_LocationPendingDelay ||
        G_LocationPendingElapsed != G_LocationPendingElapsed ||
        G_LocationPendingDelay != G_LocationPendingDelay)
    {
        return;
    }
    ChangeClientLocation(Pending);
    // +0x44 is re-read after the change.
    if (G_bLocationPendingAudio)
        ApplyLocationAudio(G_LocationPendingRoom);
    G_LocationPendingRoom = 0;
    G_bLocationPendingAudio = false;
}

void QueueClientLocationChange(unsigned Location, float Delay, bool bWithAudio)
{
    // FUN_0043ba90.
    G_LocationPendingRoom = Location;
    G_bLocationPendingAudio = bWithAudio;
    G_LocationPendingElapsed = 0.0f;
    G_LocationPendingDelay = Delay;
}

void EnterClientLocationBase(unsigned BaseId)
{
    // FUN_0043b290.
    G_LocationCurrentBase = BaseId;
    IServer* const Server = GetLocationServer();
    if (Server != nullptr)
        Server->BaseEnter(BaseId, GetPlayerConnectionId());

    FLocationBaseEntry* const Entry = FindLocationBaseEntry(BaseId);
    if (Entry->RefCount == 0)
        LoadLocationBase(Entry, BaseId);
    Entry->RefCount += 1;

    // The start location the server set, else BaseData's own (+0x04). Retail looks the base up
    // again each time.
    unsigned StartLocation = FindLocationBaseEntry(BaseId)->Runtime->StartLocationId;
    if (StartLocation == 0)
    {
        if (GetLocalServerInterface() != nullptr)
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kLocationControllerCpp, 0x405,
                  "Failed to get start location\n");
        }
        StartLocation = FindLocationBaseEntry(BaseId)->Base->StartRoomId;
    }

    ApplyLocationAudio(StartLocation);
    ChangeClientLocation(StartLocation);
}

void SetClientLocationBase(unsigned BaseId)
{
    // FUN_0043b570.
    G_LocationCurrentBase = BaseId;
    IServer* const Server = GetLocationServer();
    if (Server != nullptr)
        Server->BaseEnter(BaseId, GetPlayerConnectionId());
    FLocationBaseEntry* const Entry = FindLocationBaseEntry(BaseId);
    if (Entry->RefCount == 0)
        LoadLocationBase(Entry, BaseId);
    Entry->RefCount += 1;
}

void ChangeClientLocation(unsigned Location)
{
    // FUN_0043b6b0.
    G_bLocationInTransition = true;

    // The current location is tested whole here, not masked.
    unsigned const OldBase = LocationBaseOf(G_LocationCurrentRoom);
    unsigned const NewBase = LocationBaseOf(Location);

    if (OldBase != 0 && NewBase != 0 && OldBase != NewBase)
    {
        // A base change.
        if ((G_LocationCurrentRoom & 0x3fffffffu) != 0)
            LeaveClientLocationRoom(G_LocationCurrentRoom);
        FLocationBaseEntry* const OldEntry = FindLocationBaseEntry(OldBase);
        OldEntry->RefCount -= 1;
        if (OldEntry->RefCount == 0)
            UnloadLocationBase(OldBase);
        IServer* Server = GetLocationServer();
        if (Server != nullptr)
            Server->BaseExit(OldBase, GetPlayerConnectionId());
        G_LocationCurrentBase = 0;
        NavBarDestroy();
        G_LocationCurrentBase = NewBase;
        Server = GetLocationServer();
        if (Server != nullptr)
            Server->BaseEnter(NewBase, GetPlayerConnectionId());
        FLocationBaseEntry* const NewEntry = FindLocationBaseEntry(NewBase);
        if (NewEntry->RefCount == 0)
            LoadLocationBase(NewEntry, NewBase);
        NewEntry->RefCount += 1;
        SetPlayerBaseIdRetail(NewBase);
        SetPlayerLocationIdRetail(Location);
        SetGameflowLocationBase(NewBase);
    }
    else if ((G_LocationCurrentRoom & 0x3fffffffu) != 0)
    {
        // The same base: leave the current location.
        unsigned const Current = G_LocationCurrentRoom;
        NnArrivalTextDestroy();
        ReleaseClientLocationRoom(G_LocationCurrentRoom);
        IServer* const Server = GetLocationServer();
        if (Server != nullptr)
            Server->LocationExit(Current, GetPlayerConnectionId());
        G_LocationCurrentRoom = 0;
        G_LocationCurrentBaseEntry = nullptr;
        G_LocationCurrentRoomEntry = nullptr;
        NotifyInventoryLocationLeft(); // FUN_00439220
    }

    // The new location and its two records.
    G_LocationCurrentRoom = Location;
    if (Location != 0)
    {
        RoomData* const Room = BaseDataList_get()->get_unloaded_room_data(Location);
        if (Room != nullptr)
        {
            G_LocationCurrentBaseEntry = FindLocationBaseEntry(Room->BaseId);
        }
        else
        {
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: Could not find location %d\n",
                  kLocationControllerCpp, 0x120, Location);
            G_LocationCurrentBaseEntry = nullptr;
        }
        G_LocationCurrentRoomEntry = FindLocationRoomEntry(G_LocationCurrentRoom);
    }
    else
    {
        G_LocationCurrentBaseEntry = nullptr;
        G_LocationCurrentRoomEntry = nullptr;
    }
    // +0x4c / +0x50: the queued change's timer, not its target.
    G_LocationPendingElapsed = 0.0f;
    G_LocationPendingDelay = 0.0f;

    IServer* const Server = GetLocationServer();
    if (Server != nullptr)
        Server->LocationEnter(Location, GetPlayerConnectionId());

    // FUN_00439610 on +0x38 (FDUMP at 0x110 and a read through null on a miss, as retail); at a
    // zero count FUN_00439d80 loads the room.
    FLocationRoomEntry* const RoomEntry = FindLocationRoomEntry(G_LocationCurrentRoom);
    if (RoomEntry->RefCount == 0)
        EnsureRoomRuntime(RoomEntry, G_LocationCurrentRoom);
    RoomEntry->RefCount += 1;
    G_bLocationInTransition = false;
}

void LeaveClientLocationBase()
{
    // FUN_0043b3e0 (thiscall).
    unsigned const Base = G_LocationCurrentBase;
    if ((Base & 0x3fffffffu) == 0)
        return;

    unsigned const Current = G_LocationCurrentRoom;
    if ((Current & 0x3fffffffu) != 0)
    {
        NnArrivalTextDestroy(); // FUN_0045b380
        FLocationRoomEntry* const RoomEntry = FindLocationRoomEntry(G_LocationCurrentRoom);
        RoomEntry->RefCount -= 1;
        if (RoomEntry->RefCount == 0)
            UnloadClientLocationRoomState(G_LocationCurrentRoom); // FUN_0043a080
        IServer* const Server = GetLocationServer();
        if (Server != nullptr)
            Server->LocationExit(Current, GetPlayerConnectionId());
        G_LocationCurrentRoom = 0;
        G_LocationCurrentBaseEntry = nullptr;
        G_LocationCurrentRoomEntry = nullptr;
        NotifyInventoryLocationLeft();
    }

    // The base lookup is inlined (FDUMP at 0x100, read through null on a miss); at a zero count
    // FUN_00439b50; IServer slot 36.
    FLocationBaseEntry* const Entry = FindLocationBaseEntry(Base);
    Entry->RefCount -= 1;
    if (Entry->RefCount == 0)
        UnloadLocationBase(Base);
    IServer* const Server = GetLocationServer();
    if (Server != nullptr)
        Server->BaseExit(Base, GetPlayerConnectionId());
    G_LocationCurrentBase = 0;
    NavBarDestroy(); // FUN_00442130
}

void DrawClientLocation()
{
    // FUN_0043bbc0 (thiscall). FRoomRuntime::Draw is FUN_0044b570.
    if (G_LocationCurrentRoomEntry == nullptr)
        return;
    FRoomRuntime* const Runtime = G_LocationCurrentRoomEntry->Runtime;
    if (Runtime == nullptr)
        return;
    Runtime->Draw();
}

unsigned GetClientLocationCurrentBase()
{
    return G_LocationCurrentBase;
}

unsigned GetClientLocationBaseSolarId()
{
    // FUN_0043bc90: controller +0x54.
    return G_LocationBaseSolarId;
}

int GetClientCurrentBaseStridName()
{
    // FUN_0043bbe0. FUN_00439590's lookup inlined on this+0x34 (the FDUMP at
    // LocationController.cpp:0x100 on a miss, after which retail reads through a null entry --
    // the same read is made here).
    FLocationBaseEntry* const Entry = FindLocationBaseEntry(G_LocationCurrentBase);
    BaseData* const Base = Entry->Base;
    if (Base == nullptr)
        return 0;
    // Retail reads strid_name (+0x0c) through IBase slot 2.
    Universe::IBase const* const Record = Universe::get_base(Base->get_base_id());
    if (Record == nullptr)
        return 0;
    return Record->StridName;
}

unsigned GetClientLocationCurrentRoom()
{
    return G_LocationCurrentRoom;
}

bool IsClientLocationInTransition()
{
    return G_bLocationInTransition;
}

void SetClientLocationInTransition()
{
    // Inlined in FUN_005588a0: DAT_00668761 (this+0x59) = 1.
    G_bLocationInTransition = true;
}
