// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// NeuroNetNavMap -- the side panels (objectives, mission info, filters) and the universe
// view's jump links. See nn_navmap.h. Retail Client\Interface\NeuroNet\NavMap.cpp.

#include "NeuroNet/NavMap/nn_navmap.h"
#include "NeuroNet/Chat/chat_init.h"                 // kTextStyleBody
#include "NeuroNet/Economy/best_path.h"               // BestPathGetWaypointCount / BestPathGetWaypoint / First
#include "Interface/Pages/character_select_equip.h"  // GetPlayerStarSystemIdRetail, GetPlayerVisitFlagsRetail
#include "Space/HUD/hud_target.h"              // NavMapSectorName (FUN_0048dc10)
#include "Common/Fonts/ids_rdl.h"                 // AppendTextMaybeXmlRdl (FUN_0057e2c0)
#include "Common/Fonts/ids_strings.h"             // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "Common/misc_util.h"               // FmtStrSprintf (FUN_0041c970)
#include "NeuroNet/nn_text_scratch.h"         // DAT_0066dc60 / DAT_0066fc60 / DAT_006119f8
#include "NeuroNet/nn_ui_pulse.h"             // G_NnPaletteCyan
#include "Space/player_spawn.h"            // GetPlayerWaypointCountRetail / GetPlayerWaypointRetail
#include "Interface/rich_text_win.h"
#include "Interface/ui_scroll.h"
#include "common_text.h"             // FmtStr, RenderDisplayList, StyleNode
#include "common_universe.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

namespace
{
    // DAT_00611c84 -- the filter title per mode (FUN_004a0220).
    unsigned const kNavMapFilterTitleIds[8] = {0x53a, 0x53b, 0x53c, 0x53d,
                                               0x550, 0x551, 0x1,   0x3c8};
}

// ----------------------------------------------------------------------------------------
// Jump links (universe view)
// ----------------------------------------------------------------------------------------

void NeuroNetNavMap::BuildJumpLinks()
{
    // FUN_004975a0.
    if (ViewMode != 0)
        return;

    // FUN_004a2f80 erases every node; the records are NOT freed (retail
    // leaks them here -- only the destructor frees what is left).
    JumpLinks.clear();

    for (std::list<FNavMapItem*>::iterator It = Items.begin(); It != Items.end(); ++It)
    {
        FNavMapItem* const From = *It;
        if (From->bShown == 0 || (From->Type & 4) == 0)
            continue;

        Universe::ISystem const* const System = From->System;
        int const ConnectionCount = System->GetConnectionCount();
        for (int Index = 0; Index < ConnectionCount; ++Index)
        {
            // The connection, its object's visit flags OR'd with its own.
            Universe::FSystemConnection Connection;
            System->GetConnection(static_cast<unsigned>(Index), &Connection);
            unsigned char VisitFlags = 0;
            GetPlayerVisitFlagsRetail(Connection.ObjectId, &VisitFlags, 0, 0); // FUN_004c4da0
            unsigned char const Flags = static_cast<unsigned char>(VisitFlags | Connection.Visit);
            if ((Flags & 1) == 0)
                continue;

            // Every shown system item the connection leads to.
            for (std::list<FNavMapItem*>::iterator ToIt = Items.begin(); ToIt != Items.end();
                 ++ToIt)
            {
                FNavMapItem* const To = *ToIt;
                if (To->bShown == 0 || (To->Type & 4) == 0)
                    continue;
                if (To->System->GetId() == Connection.GotoSystemId)
                    AddJumpLink(From, To);
            }
        }
    }
}

void NeuroNetNavMap::AddJumpLink(FNavMapItem* From, FNavMapItem* To)
{
    // FUN_004976e0.
    // The same pair is ignored; the reverse pair becomes two-way.
    for (std::list<FNavMapJumpLink*>::iterator It = JumpLinks.begin(); It != JumpLinks.end();
         ++It)
    {
        FNavMapJumpLink* const Existing = *It;
        if (Existing->From == From && Existing->To == To)
            return;
        if (Existing->From == To && Existing->To == From)
        {
            Existing->bBothWays = 1;
            return;
        }
    }

    FNavMapJumpLink* const Link = static_cast<FNavMapJumpLink*>(operator new(0x10));
    Link->From = From;
    Link->To = To;
    Link->Field_0x08 = 1;
    Link->bBothWays = 0;
    Link->Kind = 0;

    // Both ends on the best path (a best-path waypoint's system, or the
    // player's own system) -> kind 2.
    bool bFromOnPath = GetPlayerStarSystemIdRetail() == From->System->GetId();
    if (!bFromOnPath)
    {
        int const Count = BestPathGetWaypointCount();
        for (int Index = 0; Index < Count; ++Index)
        {
            if (BestPathWaypointSystem(Index) == From->System)
            {
                bFromOnPath = true;
                break;
            }
        }
    }
    bool bKindSet = false;
    if (bFromOnPath)
    {
        bool bToOnPath = GetPlayerStarSystemIdRetail() == To->System->GetId();
        if (!bToOnPath)
        {
            int const Count = BestPathGetWaypointCount();
            for (int Index = 0; Index < Count; ++Index)
            {
                if (BestPathWaypointSystem(Index) == To->System)
                {
                    bToOnPath = true;
                    break;
                }
            }
        }
        if (bToOnPath)
        {
            Link->Kind = 2;
            bKindSet = true;
        }
    }

    // Otherwise both ends on the player's waypoint route (a waypoint in
    // the system, or the player's own system) -> kind 1.
    if (!bKindSet)
    {
        bool bFromOnRoute = GetPlayerStarSystemIdRetail() == From->System->GetId();
        if (!bFromOnRoute)
        {
            int const Count = GetPlayerWaypointCountRetail();
            for (int Index = 0; Index < Count; ++Index)
            {
                FPlayerWaypointRecord const* const Record = GetPlayerWaypointRetail(Index);
                if (Record->SystemId == From->System->GetId())
                {
                    bFromOnRoute = true;
                    break;
                }
            }
        }
        if (bFromOnRoute)
        {
            bool bToOnRoute = GetPlayerStarSystemIdRetail() == To->System->GetId();
            if (!bToOnRoute)
            {
                int const Count = GetPlayerWaypointCountRetail();
                for (int Index = 0; Index < Count; ++Index)
                {
                    FPlayerWaypointRecord const* const Record = GetPlayerWaypointRetail(Index);
                    if (Record->SystemId == To->System->GetId())
                    {
                        bToOnRoute = true;
                        break;
                    }
                }
            }
            if (bToOnRoute)
                Link->Kind = 1;
        }
    }

    JumpLinks.push_back(Link);
}

Universe::ISystem const* NeuroNetNavMap::BestPathWaypointSystem(int Index)
{
    // FUN_0049a8f0. The record is dereferenced without a null test, as in retail (every
    // caller stays below BestPathGetWaypointCount).
    FmtStr::Val const* Token = BestPathGetWaypoint(Index)->Marker.Head;
    while (Token != nullptr)
    {
        unsigned short const Tag = Token->TypeTag;
        if (Tag == 0x42)
        {
            // 'B' -- the system one of whose objects docks with the base.
            unsigned const BaseId = Token->Value;
            for (Universe::ISystem* System = Universe::GetFirstSystem(); System != nullptr;
                 System = Universe::GetNextSystem())
            {
                Universe::FSystemObject Object;
                unsigned const Count = static_cast<unsigned>(System->GetObjectCount());
                for (unsigned ObjectIndex = 0; ObjectIndex < Count; ++ObjectIndex)
                {
                    if (System->GetSystemObject(ObjectIndex, &Object) && Object.DockWith == BaseId)
                        return System;
                }
            }
        }
        else if (Tag == 0x4e)
        {
            // 'N' -- the data block's first dword.
            return Universe::get_system(Token->Marker->Data[0]);
        }
        else if (Tag == 0x53)
        {
            // 'S' -- the id itself.
            return Universe::get_system(Token->Value);
        }
        Token = Token->Next;
    }
    return nullptr;
}

// ----------------------------------------------------------------------------------------
// Panels
// ----------------------------------------------------------------------------------------

void NeuroNetNavMap::RefreshMissionInfo()
{
    // FUN_0049a580.
    FBestPath* const Path = G_BestPaths.empty() ? nullptr : &G_BestPaths.begin()->second;
    if (Path == nullptr)
    {
        // No route -- no abort button, a blank title and text.
        AbortMissionButton->SetProperty(1, 1, 0);
        AbortMissionText->SetProperty(1, 1, 0);
        wcscpy(G_NnTextScratchB, L" ");
        InfoTitle->SetProperty(0x1d, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
        InfoText->Flags = static_cast<unsigned char>(InfoText->Flags & 0xfd);

        RenderDisplayList Text;
        {
            StyleNode Style(kTextStyleBody);
            RdlAppendNode(Text, Style.Clone());
        }
        AppendTextMaybeXmlRdl(G_NnTextScratchB,
                              static_cast<unsigned>(wcslen(G_NnTextScratchB)), Text, false);
        InfoText->ClearContent();
        InfoText->AppendContent(Text);
        InfoText->SetScrollOrigin(0, 0);
        return;
    }

    // The route's title and text (node +0x20 / +0x38).
    FmtStrSprintf(&Path->Title, G_NnTextScratchB, G_NnTextScratchCapacity);
    InfoTitle->SetProperty(0x1d, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
    FmtStrSprintf(&Path->Text, G_NnTextScratchB, G_NnTextScratchCapacity);
    InfoText->Flags = static_cast<unsigned char>(InfoText->Flags | 2);

    {
        RenderDisplayList Text;
        {
            StyleNode Style(kTextStyleBody);
            RdlAppendNode(Text, Style.Clone());
        }
        AppendTextMaybeXmlRdl(G_NnTextScratchB,
                              static_cast<unsigned>(wcslen(G_NnTextScratchB)), Text, false);
        InfoText->ClearContent();
        InfoText->AppendContent(Text);
        InfoText->SetScrollOrigin(0, 0);
    }

    // The abort button is hidden for routes of kind 7 and 8.
    unsigned char bHideAbort = 1;
    FBestPath const* const First = BestPathGetFirst(); // FUN_004c5040
    if (First != nullptr && First->MissionType != 7 && First->MissionType != 8)
        bHideAbort = 0;
    AbortMissionButton->SetProperty(1, bHideAbort, 0);
    AbortMissionText->SetProperty(1, bHideAbort, 0);
}

void NeuroNetNavMap::HideObjectTools()
{
    // FUN_0049f0e0.
    InZoomButton->SetProperty(1, 1, 0);
    OutZoomButton->SetProperty(1, 1, 0);
    InfoButton->SetProperty(1, 1, 0);
    SetWaypointButton->SetProperty(1, 1, 0);
    BestPathButton->SetProperty(1, 1, 0);
    JumpGateButton->SetProperty(1, 1, 0);
    SelectObjectButton->SetProperty(1, 1, 0);
    ClearWaypointButton->SetProperty(1, 1, 0);
    OptionBridge->SetProperty(1, 1, 0);
    bObjectToolsShown = 0;
}

void NeuroNetNavMap::RefreshObjectives()
{
    // FUN_0049f7a0.
    G_bNavMapRouteChanged = 0;
    int Row = 0;

    if (bBestPathObjectives == 0)
    {
        // The player's waypoints -- the scroll range, and the first row
        // back to 0 when it ran past the end.
        int const Count = GetPlayerWaypointCountRetail();
        int FirstScroll = Count - 3;
        if (FirstScroll < 0)
            FirstScroll = 0;
        ObjectiveScroll->SetProperty(0x37, 0, static_cast<uintptr_t>(Count - 1));
        if (FirstScroll == 0)
            ObjectiveScroll->SetProperty(2, 1, 0);
        else
            ObjectiveScroll->SetProperty(2, 0, 0);
        if (ObjectiveFirstRow >= Count)
            ObjectiveFirstRow = 0;

        // Up to three rows from ObjectiveFirstRow.
        for (int Index = ObjectiveFirstRow; Index < Count; ++Index)
        {
            G_NnTextScratchB[0] = L'\0';
            FPlayerWaypointRecord const* const Record = GetPlayerWaypointRetail(Index);
            if (Record != nullptr)
            {
                // "Waypoint %d", the system line ("\n" + ids 0x54c formatted with the system
                // name) and the sector ("\n" + FUN_0048dc10).
                GetIdsStringWWithRawFallback(0x54b, G_NnTextScratchA, G_NnTextScratchCapacity);
                _swprintf(G_NnTextScratchB, G_NnTextScratchA, Record->AutoId);
                wchar_t Sector[0x100];
                NavMapSectorName(&Record->Pos, Sector);
                Universe::ISystem const* const System =
                    Universe::get_system(Record->SystemId);
                if (System != nullptr)
                {
                    GetIdsStringWWithRawFallback(0x54c, G_NnTextScratchA,
                                                 G_NnTextScratchCapacity);
                    wcscat(G_NnTextScratchB, L"\n");
                    wchar_t SystemName[0x100];
                    GetIdsStringWWithRawFallback(static_cast<unsigned>(System->GetStridName()),
                                                 SystemName, 0x100);
                    _swprintf(G_NnTextScratchB + wcslen(G_NnTextScratchB), G_NnTextScratchA,
                              SystemName);
                }
                wcscat(G_NnTextScratchB, L"\n");
                wcscat(G_NnTextScratchB, Sector);
            }

            // The row's text in the idle colour, both shown.
            ObjectiveText[Row]->SetProperty(0x1d, reinterpret_cast<uintptr_t>(G_NnTextScratchB),
                                            0);
            ObjectiveText[Row]->SetProperty(0x33,
                                            reinterpret_cast<uintptr_t>(&G_NnPaletteCyan),
                                            1);
            ObjectiveText[Row]->SetProperty(1, 0, 0);
            MissionObjective[Row]->SetProperty(1, 0, 0);
            ++Row;
            if (Row >= 3)
                return;
        }

        // The rows left, hidden.
        for (; Row < 3; ++Row)
        {
            ObjectiveText[Row]->SetProperty(1, 1, 0);
            MissionObjective[Row]->SetProperty(1, 1, 0);
        }
        return;
    }

    // The best path. The loop runs over five times the waypoint count
    // (retail's bound; the records past the end are null and skipped). Kind 9 is not listed
    // and not counted; the rows before ObjectiveFirstRow only advance the stop number.
    int const Total = BestPathGetWaypointCount() * 5;
    int Listed = 0;
    int StopNumber = 1;
    for (int Index = 0; Index < Total; ++Index)
    {
        FBestPathWaypoint* const Waypoint = BestPathGetWaypoint(Index);
        if (Waypoint == nullptr)
            continue;
        unsigned const Kind = Waypoint->ObjectiveState & 0xf;
        if (Kind != 9)
            ++Listed;
        if (Index < ObjectiveFirstRow)
        {
            if (Kind == 0xa)
                ++StopNumber;
            continue;
        }
        if (Kind == 9 || Row >= 3)
            continue;

        if (Kind == 0xa)
        {
            GetIdsStringWWithRawFallback(0x4df, G_NnTextScratchA, G_NnTextScratchCapacity);
            _swprintf(G_NnTextScratchB, G_NnTextScratchA, StopNumber);
            ++StopNumber;
        }
        else
        {
            GetIdsStringWWithRawFallback(0x331c, G_NnTextScratchB, G_NnTextScratchCapacity);
        }
        FmtStrSprintf(&Waypoint->Marker, G_NnTextScratchA, G_NnTextScratchCapacity);
        wcscat(G_NnTextScratchB, G_NnTextScratchA);
        ObjectiveText[Row]->SetProperty(0x1d, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
        ObjectiveText[Row]->SetProperty(1, 0, 0);
        MissionObjective[Row]->SetProperty(1, 0, 0);
        ++Row;
    }

    for (; Row < 3; ++Row)
    {
        ObjectiveText[Row]->SetProperty(1, 1, 0);
        MissionObjective[Row]->SetProperty(1, 1, 0);
    }

    // The scroll range over the listed records.
    int FirstScroll = Listed - 3;
    if (FirstScroll < 0)
        FirstScroll = 0;
    ObjectiveScroll->SetProperty(0x37, 0, static_cast<uintptr_t>(Listed - 1));
    if (FirstScroll == 0)
        ObjectiveScroll->SetProperty(2, 1, 0);
    else
        ObjectiveScroll->SetProperty(2, 0, 0);
    if (ObjectiveFirstRow >= Listed)
        ObjectiveFirstRow = 0;
}

void NeuroNetNavMap::SetFilterMode(int Mode)
{
    // FUN_004a0220.
    // The known-base rows (and their titles) take property 0x24 = mode 7.
    uintptr_t const bBaseList = Mode == 7 ? 1 : 0;
    for (int Index = 0; Index < 15; ++Index)
    {
        BaseTexts[Index]->SetProperty(0x24, bBaseList, 0);
        BaseSystemTexts[Index]->SetProperty(0x24, bBaseList, 0);
    }
    BaseTitleText->SetProperty(0x24, bBaseList, 0);
    SystemTitleText->SetProperty(0x24, bBaseList, 0);

    // Leaving an info-card mode (6 / 7) closes the card and brings back
    // what it covered.
    if (G_NavMapFilterMode == 6 || G_NavMapFilterMode == 7)
    {
        InfoCard->Flags = static_cast<unsigned char>(InfoCard->Flags & 0xfc);
        CloseInfocardButton->Flags = static_cast<unsigned char>(CloseInfocardButton->Flags & 0xfc);
        CloseInfocardText->Flags = static_cast<unsigned char>(CloseInfocardText->Flags & 0xfc);
        if (ViewMode == 0)
        {
            for (int Index = 0; Index < 4; ++Index)
                FactionLabels[Index]->SetProperty(1, 0, 0);
        }
        else
        {
            PhysicalFilter->Flags = static_cast<unsigned char>(PhysicalFilter->Flags | 3);
            PoliticalFilter->Flags = static_cast<unsigned char>(PoliticalFilter->Flags | 3);
            PatrolFilter->Flags = static_cast<unsigned char>(PatrolFilter->Flags | 3);
            LegendFilter->Flags = static_cast<unsigned char>(LegendFilter->Flags | 3);
            BaseListFilter->Flags = static_cast<unsigned char>(BaseListFilter->Flags | 3);
            MineableFilter->Flags = static_cast<unsigned char>(MineableFilter->Flags | 3);
            LabelButton->Flags = static_cast<unsigned char>(LabelButton->Flags | 3);
            if (!MissionZoneIds.empty())
                MissionFilter->Flags = static_cast<unsigned char>(MissionFilter->Flags | 3);
        }
    }

    // The new mode and the toggles' pressed state.
    G_NavMapFilterMode = Mode;
    PhysicalFilter->SetProperty(0xc, G_NavMapFilterMode == 0 ? 1 : 0, 0);
    PoliticalFilter->SetProperty(0xc, G_NavMapFilterMode == 1 ? 1 : 0, 0);
    PatrolFilter->SetProperty(0xc, G_NavMapFilterMode == 2 ? 1 : 0, 0);
    MissionFilter->SetProperty(0xc, G_NavMapFilterMode == 3 ? 1 : 0, 0);
    LegendFilter->SetProperty(0xc, G_NavMapFilterMode == 4 ? 1 : 0, 0);
    BaseListFilter->SetProperty(0xc, G_NavMapFilterMode == 7 ? 1 : 0, 0);
    MineableFilter->SetProperty(0xc, G_NavMapFilterMode == 5 ? 1 : 0, 0);

    // The zone filters only in the political / patrol modes.
    if (G_NavMapFilterMode == 1 || G_NavMapFilterMode == 2)
    {
        AllFilter->SetProperty(1, 0, 0);
        HostileFilter->SetProperty(1, 0, 0);
        FriendlyFilter->SetProperty(1, 0, 0);
    }
    else
    {
        AllFilter->SetProperty(1, 1, 0);
        HostileFilter->SetProperty(1, 1, 0);
        FriendlyFilter->SetProperty(1, 1, 0);
    }

    // Mode 6 (the info card) hides the filter row.
    if (G_NavMapFilterMode == 6)
    {
        PhysicalFilter->Flags = static_cast<unsigned char>(PhysicalFilter->Flags & 0xfc);
        PoliticalFilter->Flags = static_cast<unsigned char>(PoliticalFilter->Flags & 0xfc);
        PatrolFilter->Flags = static_cast<unsigned char>(PatrolFilter->Flags & 0xfc);
        MissionFilter->Flags = static_cast<unsigned char>(MissionFilter->Flags & 0xfc);
        LegendFilter->Flags = static_cast<unsigned char>(LegendFilter->Flags & 0xfc);
        BaseListFilter->Flags = static_cast<unsigned char>(BaseListFilter->Flags & 0xfc);
        MineableFilter->Flags = static_cast<unsigned char>(MineableFilter->Flags & 0xfc);
        LabelButton->Flags = static_cast<unsigned char>(LabelButton->Flags & 0xfc);
    }

    // The title (the mode indexes DAT_00611c84 unchecked, as in retail).
    FilterTitle->SetProperty(0x1e, kNavMapFilterTitleIds[G_NavMapFilterMode], 0);
}

void NeuroNetNavMap::RefreshMissionZones()
{
    // FUN_004a0580.
    ShadeRecheckTimer = 1.0f; // DAT_005d2ed0

    MissionZoneIds.clear();

    // Every 'Z' token of every best-path waypoint, appended. The record is
    // dereferenced without a null test (the index stays below the count).
    int const Count = BestPathGetWaypointCount();
    for (int Index = 0; Index < Count; ++Index)
    {
        for (FmtStr::Val const* Token = BestPathGetWaypoint(Index)->Marker.Head;
             Token != nullptr; Token = Token->Next)
        {
            if (Token->TypeTag == 0x5a)
                MissionZoneIds.push_back(Token->Value);
        }
    }

    // In the system view (and with no transition running) the mission
    // filter shows only with zones; the mission mode falls back to physical without them.
    if (ViewMode != 0 && bTransition == 0)
    {
        unsigned char const bNoZones = MissionZoneIds.empty() ? 1 : 0;
        MissionFilter->SetProperty(1, bNoZones, 0);
        if (bNoZones != 0 && G_NavMapFilterMode == 3)
            SetFilterMode(0);
    }
}
