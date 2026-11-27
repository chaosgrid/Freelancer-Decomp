// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// The client's mission objectives map DAT_0067294c -- see best_path.h.

#include "NeuroNet/Economy/best_path.h"

#include "Interface/Pages/character_select_equip.h" // GetPlayerStarSystemIdRetail (FUN_004c4830 -> DAT_00673354)
#include "Space/client_gameobj.h"         // FClientGameobj
#include "Space/client_objdb.h"           // ClientObjDb_GetKindHead / ClientObjDb_FindEqObjByVibeId
#include "common_cobject.h"         // CObject, CEqObj::DockingBaseId
#include "common_goods.h"           // BaseWatcher
#include "common_iface.h"           // IObjRW, ObjRwFromWatchable
#include "common_server.h"          // ENnObjectiveState
#include "common_universe.h"        // Universe::get_base
#include "dacom.h"                  // FDUMP
#include "Space/HUD/hud_contact_list.h"       // G_bHudContactListRefreshRequested (DAT_0067494c)
#include "NeuroNet/NavMap/nn_navmap.h"              // G_bNavMapRouteChanged (DAT_006720c9)
#include "NeuroNet/nn_story_star.h"          // StoryStarReset / StoryStarAddHistory / StoryStarPushPending
#include "Space/player_spawn.h"           // GetPlayerWaypointCountRetail / ResetPlayerWaypointsRetail
#include "rpc_iclient_args.h"       // the slot 71 / 72 / 73 argument records

#include <cstring>
#include <utility>

// DAT_0067294c; static init LAB_004c3bd0 (FUN_004c59a0, the empty map), teardown LAB_004c3c00.
std::map<unsigned, FBestPath> G_BestPaths;

namespace
{
    // DAT_006728d8 -- the docking solar of the last base a record named (static init
    // LAB_004c5060).
    BaseWatcher G_BestPathBaseWatcher;
    // DAT_006728e0 -- the star system of G_BestPathBaseId (0 until get_base answers).
    unsigned G_BestPathBaseSystem = 0;
    // DAT_00673394 -- the base id the watcher was resolved for.
    unsigned G_BestPathBaseId = 0;
    // DAT_00673398 -- ObjDB bucket 2 was searched for G_BestPathBaseId's solar.
    unsigned char G_bBestPathBaseSearched = 0;

    // FUN_004c5090 (also inlined twice in FUN_004c5110): the first solar of ObjDB bucket 2
    // whose docking base (CEqObj +0x160) is G_BestPathBaseId becomes the watched object.
    void SearchBaseSolar()
    {
        G_bBestPathBaseSearched = 1;
        int const SolarKind = 2;
        for (FClientGameobj* Go = ClientObjDb_GetKindHead(&SolarKind); Go != nullptr;
             Go = Go->ListNext)
        {
            // Narrowed to a CSolar and read unguarded, as in retail (bucket 2 only holds
            // solars).
            CObject* const Obj = Go->Object;
            CEqObj* Solar = nullptr;
            if (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar)
                Solar = static_cast<CEqObj*>(Obj);
            if (Solar->DockingBaseId == G_BestPathBaseId)
            {
                G_BestPathBaseWatcher.set(static_cast<Watchable const*>(static_cast<IObjRW*>(Go)));
                return;
            }
        }
    }

    // The 'B' branch of FUN_004c5110 and of FUN_004c52d0. The two spell it differently
    // (FUN_004c5110 inlines the search twice and guards set_pointer(0) with a null test;
    // FUN_004c52d0 calls FUN_004c5090 and BaseWatcher::set) but take the same paths. A base in
    // another system returns the (now empty) watcher at once.
    IObjRW* ResolveBaseObject(unsigned BaseId)
    {
        if (G_BestPathBaseId != BaseId)
        {
            G_BestPathBaseId = BaseId;
            G_BestPathBaseSystem = 0;
            G_BestPathBaseWatcher.set(nullptr);
            G_bBestPathBaseSearched = 0;
            Universe::IBase* const Base = Universe::get_base(BaseId);
            if (Base != nullptr)
            {
                G_BestPathBaseSystem = Base->SystemId;
                if (G_BestPathBaseSystem != GetPlayerStarSystemIdRetail())
                    return ObjRwFromWatchable(const_cast<Watchable*>(G_BestPathBaseWatcher.target()));
                SearchBaseSolar();
            }
        }
        if (G_BestPathBaseSystem == GetPlayerStarSystemIdRetail() && G_bBestPathBaseSearched == 0)
            SearchBaseSolar();
        return ObjRwFromWatchable(const_cast<Watchable*>(G_BestPathBaseWatcher.target()));
    }

    // The first 'B' (0x42) or 'R' (0x52) token of the record's chain, as FUN_004c5110 and
    // FUN_004c52d0 walk it.
    FmtStr::Val const* FindObjectToken(FBestPathWaypoint const* Waypoint)
    {
        for (FmtStr::Val const* Token = Waypoint->Marker.Head; Token != nullptr; Token = Token->Next)
        {
            if (Token->TypeTag == FmtStr::TagBase || Token->TypeTag == FmtStr::TagRepInstance)
                return Token;
        }
        return nullptr;
    }

    // The object a 'B' / 'R' token names: 'R' through FUN_00541890(data, 0), 'B' through
    // ResolveBaseObject.
    IObjRW* ResolveObjectToken(FmtStr::Val const* Token)
    {
        unsigned const Data = Token->Value;
        if (Token->TypeTag == FmtStr::TagRepInstance)
            return ClientObjDb_FindEqObjByVibeId(Data, 0);
        return ResolveBaseObject(Data);
    }
}

int BestPathGetWaypointCount()
{
    // FUN_004c4fb0.
    if (G_BestPaths.empty())
        return 0;
    FBestPath const& Path = G_BestPaths.begin()->second;
    if (Path.Waypoints.empty())
        return 0;
    return static_cast<int>(Path.Waypoints.size());
}

FBestPathWaypoint* BestPathGetWaypoint(int Index)
{
    // FUN_004c4ff0.
    if (G_BestPaths.empty())
        return nullptr;
    if (Index < 0)
        return nullptr;
    FBestPath& Path = G_BestPaths.begin()->second;
    if (Path.Waypoints.empty())
        return nullptr;
    if (static_cast<unsigned>(Index) >= static_cast<unsigned>(Path.Waypoints.size()))
        return nullptr;
    return &Path.Waypoints[static_cast<size_t>(Index)];
}

FBestPath* BestPathGetFirst()
{
    // FUN_004c5040 (see best_path.h).
    if (G_BestPaths.empty())
        return nullptr;
    return &G_BestPaths.begin()->second;
}

int BestPathFirstWaypointNotKind3()
{
    // FUN_004c5490 (FUN_004c4ff0 inlined). The lookup cannot fail for Index < count, so the
    // null retail would dereference is never reached.
    int const Count = BestPathGetWaypointCount();
    for (int Index = 0; Index < Count; ++Index)
    {
        FBestPathWaypoint const* const Waypoint = BestPathGetWaypoint(Index);
        if (Waypoint->ObjectiveState != kNnObjectiveComplete)
            return Index;
    }
    return -1;
}

bool BestPathHasText()
{
    // FUN_004c58e0.
    if (G_BestPaths.empty())
        return false;
    FBestPath const& Path = G_BestPaths.begin()->second;
    if (Path.Waypoints.empty())
        return false;
    unsigned const MadLibId = Path.Text.MadLibId;
    return MadLibId != 1 && MadLibId != 0;
}

unsigned BestPathWaypointHash(int Index)
{
    // FUN_004c5530 (FUN_004c4ff0 inlined).
    FBestPathWaypoint* const Waypoint = BestPathGetWaypoint(Index);
    if (Waypoint == nullptr)
        return 0;
    unsigned Hash = Waypoint->Marker.MadLibId;
    for (FmtStr::Val const* Token = Waypoint->Marker.Head; Token != nullptr; Token = Token->Next)
    {
        Hash = (Hash << 1) | (Hash >> 31);
        if (Token->TypeTag != FmtStr::TagRepInstance && Token->TypeTag != FmtStr::TagNavMarker)
            Hash += Token->Value; // the raw payload dword, whatever the tag (0x21 adds the pointer)
    }
    return Hash;
}

IObjRW* FBestPathWaypoint::ResolveObject() const
{
    // FUN_004c5110.
    FmtStr::Val const* const Token = FindObjectToken(this);
    if (Token == nullptr)
        return nullptr;
    return ResolveObjectToken(Token);
}

bool FBestPathWaypoint::ResolvePosition(Vector* OutPos, unsigned* OutSystem) const
{
    // FUN_004c52d0.
    FmtStr::Val const* const ObjectToken = FindObjectToken(this);
    if (ObjectToken != nullptr)
    {
        IObjRW* const Object = ResolveObjectToken(ObjectToken);
        if (Object != nullptr)
        {
            *OutSystem = GetPlayerStarSystemIdRetail();
            *OutPos = Object->get_position();
            return true;
        }
    }

    // Otherwise the first 'N' (0x4e) token's data block {+0x00 system, +0x08 position}.
    for (FmtStr::Val const* Token = Marker.Head; Token != nullptr; Token = Token->Next)
    {
        if (Token->TypeTag != FmtStr::TagNavMarker)
            continue;

        FmtStr::NavMarker const* const Block = Token->Marker;
        *OutSystem = Block->Data[0];
        std::memcpy(OutPos, &Block->Data[2], sizeof(Vector)); // words 2..4: the float triple
        return true;
    }
    return false;
}

void BestPathObjectivesChanged()
{
    // FUN_004bcbf0.
    G_bNavMapRouteChanged = 1;             // DAT_006720c9
    G_bHudContactListRefreshRequested = 1; // DAT_0067494c
    if (G_BestPaths.empty())
        return;

    ResetPlayerWaypointsRetail(); // FUN_004c46c0
    int const Count = BestPathGetWaypointCount();
    for (int Index = 0; Index < Count; ++Index)
    {
        // FUN_004c4ff0 cannot fail for Index < Count; retail would read through the null
        // anyway.
        FBestPathWaypoint* const Waypoint = BestPathGetWaypoint(Index);
        if ((Waypoint->ObjectiveState & kObjectiveRequestReset) != 0)
            StoryStarReset();                    // FUN_004b9bb0, inlined
        if ((Waypoint->ObjectiveState & kObjectiveRequestHistory) != 0)
            StoryStarAddHistory(&Waypoint->Marker);  // FUN_004ba2a0
        if ((Waypoint->ObjectiveState & kObjectiveRequestPending) != 0)
            StoryStarPushPending(&Waypoint->Marker); // FUN_004b9f90
        Waypoint->ObjectiveState &= kObjectiveStateMask;
    }
}

void BestPathReplaceObjective(FServerReplaceMissionObjectiveInfo const* Info)
{
    // FUN_005a5b60: retail does not bounds-check the record index.
    FBestPath& Path = G_BestPaths[Info->MissionId];
    FBestPathWaypoint* const Records = Path.Waypoints.data();
    Records[Info->ObjectiveIndex].Marker = Info->Text;
    Records[Info->ObjectiveIndex].ObjectiveState = Info->ObjectiveType;
}

void BestPathSetObjectives(FServerSetMissionObjectivesInfo const* Info)
{
    // LAB_005a5bc0.
    G_BestPaths.clear();                     // FUN_004c5a80

    // An empty title while the player has waypoints and a mission type of 7 or 8 returns at
    // once -- without FUN_004bcbf0.
    unsigned const TitleMadLib = Info->TextA.MadLibId;
    if ((TitleMadLib == 0 || TitleMadLib == 1) && GetPlayerWaypointCountRetail() > 0 &&
        (Info->MissionType == 7 || Info->MissionType == 8))
        return;

    // No records -> straight to FUN_004bcbf0 with the map left empty.
    if (Info->Count != 0)
    {
        std::pair<std::map<unsigned, FBestPath>::iterator, bool> const Inserted =
            G_BestPaths.insert(std::make_pair(Info->MissionId, FBestPath()));
        FBestPath& Path = Inserted.first->second;

        Path.Waypoints.reserve(Info->Count);

        for (unsigned Index = 0; Index < Info->Count; ++Index)
        {
            FBestPathWaypoint Record;
            Record.Marker = Info->Entries[Index].Text;
            Record.ObjectiveState = Info->Entries[Index].State;
            Path.Waypoints.push_back(Record);
        }

        Path.Title = Info->TextA;
        Path.MissionType = Info->MissionType;
        Path.Text = Info->TextB;
    }

    BestPathObjectivesChanged(); // FUN_004bcbf0
}

void BestPathSetObjectiveState(FServerMissionObjectiveStateInfo const* Info)
{
    // LAB_005a5e80: retail does not bounds-check the record index.
    std::map<unsigned, FBestPath>::iterator const It = G_BestPaths.find(Info->MissionId);
    if (It == G_BestPaths.end())
        return;
    It->second.Waypoints.data()[Info->ObjectiveIndex].ObjectiveState = Info->State;
}
