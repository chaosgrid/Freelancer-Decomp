// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// NeuroNetNavMap -- the input side: slot 19 MouseEvent (LAB_00497d30), slot 42 SetProperty
// (LAB_00493680) and the helpers only they reach (the object tools, the info card, the abort
// prompt, the known-bases list). See nn_navmap.h. Retail Client\Interface\NeuroNet\NavMap.cpp.

#include "NeuroNet/NavMap/nn_navmap.h"
#include "NeuroNet/Chat/chat_init.h"                 // kTextStyleBody / kTextStyleCardTitle
#include "NeuroNet/Economy/best_path.h"               // G_BestPaths (DAT_00672950), BestPathGetFirst / HasText
#include "Interface/Pages/character_select_equip.h"  // GetPlayerStarSystemIdRetail, GetPlayerVisitFlagsRetail
#include "NeuroNet/Economy/commodity_by_base.h"       // IsCommodityByBaseListedName (FUN_004a2310)
#include "GameFlow/gameflow.h"                // RequestMissionBriefingContinue (FUN_0043d580)
#include "Space/HUD/hud_command_dispatch.h"    // FSpaceCommand, SpaceSendCommand (FUN_0054bcb0)
#include "Space/HUD/hud_waypoint.h"            // HudWaypointSelectPlayerWaypoint / BestPath
#include "Common/Fonts/ids_rdl.h"                 // AppendTextMaybeXmlRdl (FUN_0057e2c0), LoadIdsIntoRdl
#include "Common/Fonts/ids_strings.h"             // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "NeuroNet/Economy/infocard_map.h"            // InfocardMapFind (FUN_00458890)
#include "NeuroNet/nn_help.h"                 // NnHelpAppendBaseGoods (FUN_00476080)
#include "Common/input_manager.h"           // GetClientSoftwareCursor (DAT_00616840 / DAT_00616844)
#include "Network/login_mgr.h"               // G_Server (DAT_0067ecd0)
#include "Interface/menu_ui_init.h"            // PlayOrRewindUiSoundSlot (FUN_005645c0)
#include "Common/misc_util.h"               // FmtStrSprintf (FUN_0041c970)
#include "NeuroNet/nn_inventory.h"            // IsPlayerDockedRetail (FUN_004c4810)
#include "NeuroNet/nn_modal.h"                // NnMainButtonOnModalOpen (FUN_0045a460)
#include "NeuroNet/nn_story_star.h"           // StoryStarReset (FUN_004b9bb0)
#include "NeuroNet/nn_text_scratch.h"         // DAT_0066dc60 / DAT_0066fc60 / DAT_006119f8
#include "NeuroNet/nn_tooltip.h"              // NnToolTipHideGlobal (FUN_004be390)
#include "Space/player_behavior.h"         // GetPlayerObjRW (FUN_0054baf0)
#include "Space/player_spawn.h"            // the player's waypoints, RequestBestPathRetail
#include "Interface/rich_text_win.h"
#include "Interface/ui_scroll.h"
#include "common_cobject.h"          // CObject::beam_object
#include "common_iface.h"            // IObjRW, IObjInspectImpl
#include "common_id.h"               // CreateID
#include "common_misc.h"             // SUBOBJ_ID_NONE
#include "common_server.h"           // IServer
#include "common_text.h"             // FmtStr, RenderDisplayList, StyleNode
#include "common_universe.h"
#include "dacom.h"                   // FDUMP

#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>                 // CharUpperW (FUN_005b74d9)

namespace
{
    // DAT_005d3dc8 (double) -- the map plane's depth in front of the camera (the same constant
    // FUN_00492410 uses; nn_navmap_update.cpp).
    double const kNavMapPlaneDepth = -0.55;
    // DAT_005d2edc -- a view switch's transition time.
    float const kNavMapViewSwitchSeconds = 0.4f;
    // 0x3fc00000 -- a zoom-out's transition time.
    float const kNavMapZoomOutSeconds = 1.5f;
    // DAT_005d3f18 -- the object tools' spacing around the cursor (FUN_0049ea50).
    float const kNavMapToolSpacing = 0.038f;
    // DAT_005d3f20 / DAT_005d3f1c -- FUN_0049ea50 opens no tools right of U 0.81 and above
    // V -0.0133 in filter modes 1 / 2.
    float const kNavMapToolsLegendU = 0.81f;
    float const kNavMapToolsLegendV = -0.0133f;
    // DAT_005d3f14 -- the smallest scaled marker size (System view) the select tool accepts.
    float const kNavMapSelectMinSize = 700.0f;

    // Not a retail function: the tail SetProperty's waypoint buttons share (LAB_00493aad and
    // an inlined copy) -- "clear all" hidden while there is no waypoint or the
    // objectives follow the best path, then the objectives and the jump links again.
    void RefreshWaypointButtons(NeuroNetNavMap& Map)
    {
        uintptr_t bHide = GetPlayerWaypointCountRetail() == 0 ? 1 : 0; // FUN_004c4690
        if (Map.bBestPathObjectives != 0)
            bHide = 1;
        Map.ClearAllWaypointsButton->SetProperty(1, bHide, 0);
        Map.ClearAllWaypointsText->SetProperty(1, bHide, 0);
        Map.RefreshObjectives(); // FUN_0049f7a0
        Map.BuildJumpLinks();    // FUN_004975a0
    }

    // Not a retail function: the item half of the info request SetProperty and MouseEvent
    // inline -- refresh the item's visit
    // bits and shown flag from the visit map, then its card.
    void ShowItemInfoCard(NeuroNetNavMap& Map, FNavMapItem* Item)
    {
        if (Item->Id != 0)
        {
            unsigned char Flags = 0;
            GetPlayerVisitFlagsRetail(Item->Id, &Flags, 0, 0); // FUN_004c4da0
            Item->Visit = static_cast<unsigned char>(Item->Visit | Flags);
            Item->bShown = static_cast<unsigned char>(Item->Visit & 1);
            if (Map.bDebugShowAll != 0)
                Item->bShown = 1;
        }
        int const IdsName = Item->StridName;
        int const IdsInfo = Item->IdsInfo;
        unsigned char Visit = 0;
        GetPlayerVisitFlagsRetail(Item->Id, &Visit, 0, 0);
        Visit = static_cast<unsigned char>(Visit | Item->Visit);
        Map.ShowInfoCard(IdsName, IdsInfo, Visit, Map.ObjectBaseId(Item->Id)); // FUN_004978c0
    }

    // Not a retail function: the zone half --
    // the named-info zone at the last click (ClickMapPos, unscaled), its card; false when none.
    bool ShowZoneInfoCard(NeuroNetNavMap& Map)
    {
        double const InvScale = 1.0 / static_cast<double>(Map.SystemScale);
        float Pos[3];
        Pos[0] = static_cast<float>(InvScale * static_cast<double>(Map.ClickMapPos[0]));
        Pos[1] = static_cast<float>(InvScale * static_cast<double>(Map.ClickMapPos[1]));
        Pos[2] = static_cast<float>(InvScale * static_cast<double>(Map.ClickMapPos[2]));
        Universe::IZone const* const Zone =
            NavMapZoneAt(Map.CurrentSystem, Pos, 0, 1, Map.bDebugShowAll); // FUN_004a1b50
        if (Zone == nullptr)
            return false;
        unsigned char Visit = 0;
        GetPlayerVisitFlagsRetail(Zone->GetId(), &Visit, 1, 0);
        Visit = static_cast<unsigned char>(Visit | Zone->GetVisit());
        // The arguments are built right to left: 0, the visit bits, slot 0x88 (ids_info), then
        // slot 0x84 (ids_name).
        int const IdsInfo = Zone->GetIdsInfo();
        int const IdsName = Zone->GetName();
        Map.ShowInfoCard(IdsName, IdsInfo, Visit, 0);
        return true;
    }
}

// ----------------------------------------------------------------------------------------
// Slot 42 -- the children's notifications
// ----------------------------------------------------------------------------------------

intptr_t NeuroNetNavMap::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_00493680. 0x26 click,
    // 0x2c / 0x2d hover, 0x3e scroll; everything else returns 0 -- without WinControl's body.
    if (Msg == 0x2c || Msg == 0x2d)
    {
        // Only the objective rows.
        for (int Row = 0; Row < 3; ++Row)
        {
            if (A != reinterpret_cast<uintptr_t>(MissionObjective[Row]))
                continue;
            if (Msg == 0x2c)
                HoveredObjective = Row;
            else if (HoveredObjective == Row)
                HoveredObjective = -1;
            return 0;
        }
        return 0;
    }
    if (Msg == 0x3e)
    {
        // The two scroll bars; any other sender is a click.
        if (A == reinterpret_cast<uintptr_t>(static_cast<WinControl*>(ObjectiveScroll)))
        {
            ObjectiveFirstRow = static_cast<int>(B);
            RefreshObjectives(); // FUN_0049f7a0
            return 1;
        }
        if (A == reinterpret_cast<uintptr_t>(static_cast<WinControl*>(BaseListScroll)))
        {
            BaseListFirstRow = static_cast<int>(B);
            RefreshBaseList(); // FUN_004a2930
            return 1;
        }
    }
    else if (Msg != 0x26)
    {
        return 0;
    }

    // 0x26: nothing while the side panels slide (FUN_0055e280 on +0x330).
    if (ButtonMover::IsActive())
        return 0;

    // The universe button starts a view switch unless one runs.
    if (A == reinterpret_cast<uintptr_t>(UniverseButton))
    {
        if (bTransition != 0)
            return 0;
        bViewSwitch = 1;
        bTransition = 1;
        TransitionView = 0;
        TransitionTime = 0.0f;
        TransitionDuration = kNavMapViewSwitchSeconds;
        PlayOrRewindUiSoundSlot(8);
        return 0;
    }
    if (A == reinterpret_cast<uintptr_t>(AbortMissionButton))
    {
        AskAbortMission(); // FUN_0049f2e0
        return 0;
    }
    if (A == reinterpret_cast<uintptr_t>(AbortYesButton))
    {
        ConfirmAbortMission(); // FUN_0049f540
        return 0;
    }
    if (A == reinterpret_cast<uintptr_t>(AbortNoButton))
    {
        AbortYesButton->SetProperty(1, 1, 0);
        AbortYesText->SetProperty(1, 1, 0);
        AbortNoButton->SetProperty(1, 1, 0);
        AbortNoText->SetProperty(1, 1, 0);
        RefreshMissionInfo(); // FUN_0049a580
        return 0;
    }
    if (A == reinterpret_cast<uintptr_t>(CloseInfocardButton))
    {
        SetFilterMode(FilterModeBeforeInfoCard); // FUN_004a0220
        return 0;
    }
    if (A == reinterpret_cast<uintptr_t>(LabelButton))
    {
        G_bNavMapShowLabels = G_bNavMapShowLabels == 0 ? 1 : 0;
        LabelButton->SetProperty(0xc, G_bNavMapShowLabels, 0);
        return 0;
    }
    if (A == reinterpret_cast<uintptr_t>(InfoButton))
    {
        // The item at the click (waypoint markers skipped), else the
        // zone there.
        FNavMapItem* const Item = ItemAt(ClickMapPos, 1); // FUN_00496d40
        if (Item != nullptr)
        {
            ShowItemInfoCard(*this, Item);
            return 1;
        }
        ShowZoneInfoCard(*this);
        return 1;
    }
    if (A == reinterpret_cast<uintptr_t>(SetWaypointButton))
    {
        // Nothing while the best path shows its text; else a
        // waypoint at the clicked item (not a waypoint marker) or point.
        if (BestPathHasText()) // FUN_004c58e0
            return 1;
        FNavMapItem* const Item = ItemAt(ClickMapPos, 0);
        if (Item != nullptr && Item->Kind != 3 && Item->Kind != 4)
        {
            ClickMapPos[0] = Item->Position[0];
            ClickMapPos[1] = Item->Position[1];
            ClickMapPos[2] = Item->Position[2];
        }
        double const InvScale = 1.0 / static_cast<double>(SystemScale);
        Vector Pos;
        Pos.x = static_cast<float>(static_cast<double>(ClickMapPos[0]) * InvScale);
        Pos.y = static_cast<float>(static_cast<double>(ClickMapPos[1]) * InvScale);
        Pos.z = static_cast<float>(static_cast<double>(ClickMapPos[2]) * InvScale);
        AddPlayerWaypointRetail(Pos, CurrentSystem->GetId(), 0); // FUN_004c4780
        PlayOrRewindUiSoundSlot(10);
        HideObjectTools(); // FUN_0049f0e0
        if (bBestPathObjectives != 0)
        {
            bBestPathObjectives = 0;
            ObjectiveFirstRow = 0;
        }
        RefreshWaypointButtons(*this);
        return 1;
    }
    if (A == reinterpret_cast<uintptr_t>(BestPathButton))
    {
        // Ask the server for a route from the player (in its own
        // system's scale) to the clicked item (its id as the tag) or point.
        FNavMapItem* const Item = ItemAt(ClickMapPos, 0);
        unsigned TargetId = 0;
        if (Item != nullptr && Item->Kind != 3 && Item->Kind != 4)
        {
            ClickMapPos[0] = Item->Position[0];
            ClickMapPos[1] = Item->Position[1];
            ClickMapPos[2] = Item->Position[2];
            TargetId = Item->Id;
        }
        float const SavedScale = SystemScale;
        SystemScale = 1.0f;
        Universe::ISystem const* const PlayerSystem =
            Universe::get_system(GetPlayerStarSystemIdRetail()); // FUN_004c4830
        if (PlayerSystem != nullptr)
            SystemScale = PlayerSystem->GetNavMapScale();
        float const PlayerScale = SystemScale;
        float PlayerPos[3];
        float PlayerHeading = 0.0f;
        PlayerMapPosition(PlayerPos, &PlayerHeading); // FUN_00491be0
        double const InvSaved = 1.0 / static_cast<double>(SavedScale);
        SystemScale = SavedScale;
        Vector To;
        To.x = static_cast<float>(InvSaved * static_cast<double>(ClickMapPos[0]));
        To.y = static_cast<float>(InvSaved * static_cast<double>(ClickMapPos[1]));
        To.z = static_cast<float>(InvSaved * static_cast<double>(ClickMapPos[2]));
        double const InvPlayer = 1.0 / static_cast<double>(PlayerScale);
        Vector From;
        From.x = static_cast<float>(static_cast<double>(PlayerPos[0]) * InvPlayer);
        From.y = static_cast<float>(static_cast<double>(PlayerPos[1]) * InvPlayer);
        From.z = static_cast<float>(static_cast<double>(PlayerPos[2]) * InvPlayer);
        RequestBestPathRetail(From, To, TargetId, CurrentSystem->GetId()); // FUN_004c4eb0
        PlayOrRewindUiSoundSlot(10);
        HideObjectTools();
        RefreshWaypointButtons(*this);
        return 1;
    }
    if (A == reinterpret_cast<uintptr_t>(ClearWaypointButton))
    {
        // Remove the clicked waypoint marker's waypoint.
        FNavMapItem* const Item = ItemAt(ClickMapPos, 0);
        if (Item != nullptr && Item->Kind == 3)
        {
            int const Count = GetPlayerWaypointCountRetail();
            for (int Index = 0; Index < Count; ++Index)
            {
                if (WaypointItems[Index] != Item)
                    continue;
                if (Index <= ObjectiveFirstRow)
                    ObjectiveFirstRow = 0;
                RemovePlayerWaypointRetail(Index); // FUN_004c46e0
                PlayOrRewindUiSoundSlot(10);
                break;
            }
        }
        HideObjectTools();
        if (GetPlayerWaypointCountRetail() == 0)
            bBestPathObjectives = 1;
        RefreshWaypointButtons(*this);
        return 1;
    }
    if (A == reinterpret_cast<uintptr_t>(ClearAllWaypointsButton))
    {
        bBestPathObjectives = 1;
        ResetPlayerWaypointsRetail(); // FUN_004c46c0
        RefreshWaypointButtons(*this);
        return 0;
    }
    if (A == reinterpret_cast<uintptr_t>(CloseButton))
    {
        NnMainButtonOnModalOpen(); // FUN_0045a460
        return 1;
    }
    if (A == reinterpret_cast<uintptr_t>(ContinueButton))
    {
        RequestMissionBriefingContinue(); // FUN_0043d580
        ProcessMessage(0xef02, 0, 0);
        return 1;
    }
    if (A == reinterpret_cast<uintptr_t>(ShipButton))
    {
        // The player's system, not null-tested.
        PendingSystem = Universe::get_system(GetPlayerStarSystemIdRetail());
        return 0;
    }
    if (A == reinterpret_cast<uintptr_t>(BaseListFilter))
    {
        BuildBaseList();   // FUN_004a2370
        RefreshBaseList(); // FUN_004a2930
        SetFilterMode(7);
        return 0;
    }
    if (A == reinterpret_cast<uintptr_t>(LegendFilter))
    {
        SetFilterMode(4);
        return 0;
    }
    if (A == reinterpret_cast<uintptr_t>(PhysicalFilter))
    {
        SetFilterMode(0);
        return 0;
    }
    if (A == reinterpret_cast<uintptr_t>(PoliticalFilter))
    {
        SetFilterMode(1);
        return 0;
    }
    if (A == reinterpret_cast<uintptr_t>(PatrolFilter))
    {
        SetFilterMode(2);
        return 0;
    }
    if (A == reinterpret_cast<uintptr_t>(MissionFilter))
    {
        SetFilterMode(3);
        return 0;
    }
    if (A == reinterpret_cast<uintptr_t>(AllFilter) ||
        A == reinterpret_cast<uintptr_t>(HostileFilter) ||
        A == reinterpret_cast<uintptr_t>(FriendlyFilter))
    {
        // The zone filter, then the three check boxes (the first set
        // from the case, the other two re-read from DAT_006720a4).
        uintptr_t bAll = 0;
        if (A == reinterpret_cast<uintptr_t>(AllFilter))
        {
            G_NavMapZoneFilter = 0;
            bAll = 1;
        }
        else if (A == reinterpret_cast<uintptr_t>(HostileFilter))
        {
            G_NavMapZoneFilter = 1;
        }
        else
        {
            G_NavMapZoneFilter = 2;
        }
        AllFilter->SetProperty(0xc, bAll, 0);
        HostileFilter->SetProperty(0xc, G_NavMapZoneFilter == 1 ? 1 : 0, 0);
        FriendlyFilter->SetProperty(0xc, G_NavMapZoneFilter == 2 ? 1 : 0, 0);
        return 0;
    }
    if (A == reinterpret_cast<uintptr_t>(MineableFilter))
    {
        SetFilterMode(5);
        return 0;
    }
    if (A == reinterpret_cast<uintptr_t>(SelectObjectButton))
    {
        // In a system view, target the clicked object (command 0x1c),
        // or select the first waypoint / best-path stop as the HUD waypoint.
        HideObjectTools();
        if (ViewMode != 1 && ViewMode != 2)
            return 0;
        FNavMapItem* const Item = ItemAt(ClickMapPos, 0);
        if (Item == nullptr)
            return 0;
        if (Item->Kind == 3)
        {
            if (Item->Variant == 1)
                HudWaypointSelectPlayerWaypoint(0); // FUN_004ceeb0
            return 1;
        }
        if (Item->Kind == 4)
        {
            if (Item->Variant == 1)
                HudWaypointSelectBestPath(0); // FUN_004cef30
            return 1;
        }
        FSpaceCommand Command;
        memset(&Command, 0, sizeof(Command));
        Command.Id = 0x1c;
        Command.ObjectId = Item->Id;
        Command.SubObjectId = SUBOBJ_ID_NONE;
        Command.Byte26 = 1;
        SpaceSendCommand(&Command); // FUN_0054bcb0
        return 1;
    }
    if (A == reinterpret_cast<uintptr_t>(JumpGateButton))
    {
        // In a system view, open the system the clicked item leads to
        // (a star's own, a waypoint's target object's, or the star under a best-path stop).
        HideObjectTools();
        if (ViewMode != 1 && ViewMode != 2)
            return 0;
        FNavMapItem* const Item = ItemAt(ClickMapPos, 0);
        if (Item == nullptr)
            return 0;
        if (Item->Kind == 0 && Item->System != nullptr)
        {
            PendingSystem = Item->System;
            return 1;
        }
        if (Item->Kind == 3)
        {
            FPlayerWaypointRecord const* const Record =
                GetPlayerWaypointRetail(Item->Ordinal - 1); // FUN_004c46a0
            if (Record != nullptr && Record->ObjectId != 0)
            {
                for (std::list<FNavMapItem*>::iterator It = Items.begin(); It != Items.end();
                     ++It)
                {
                    FNavMapItem* const Target = *It;
                    if (Target->bShown == 0 || Target->Id != Record->ObjectId)
                        continue;
                    // Found -- 1 whether or not it has a system.
                    if (Target->System != nullptr)
                        PendingSystem = Target->System;
                    return 1;
                }
            }
        }
        if (Item->Kind != 4)
            return 0;
        FNavMapItem* const Under = ItemAt(ClickMapPos, 1);
        if (Under == nullptr || Under->Kind != 0 || Under->System == nullptr)
            return 0;
        PendingSystem = Under->System;
        return 1;
    }
    if (A == reinterpret_cast<uintptr_t>(InZoomButton) ||
        A == reinterpret_cast<uintptr_t>(OutZoomButton))
    {
        // LAB_004943e1: both zoom buttons, by the view.
        HideObjectTools();
        if (ViewMode == 0)
        {
            // Into the clicked star's system.
            FNavMapItem* const Item = ItemAt(ClickMapPos, 0);
            if (Item == nullptr || (Item->Kind != 0 && Item->Kind != 1))
                return 1;
            CurrentSystem = Item->System;
            bViewSwitch = 1;
            bTransition = 1;
            TransitionView = 1;
            TransitionTime = 0.0f;
            TransitionDuration = kNavMapViewSwitchSeconds;
            PlayOrRewindUiSoundSlot(8);
            return 1;
        }
        if (ViewMode == 1)
        {
            ZoomToClick(); // FUN_0049f1a0
            return 1;
        }
        if (ViewMode != 2 || bDragging != 0)
            return 1;

        // Zoomed in -- back out to the whole system over 1.5 s. Retail first stores
        // TransitionDelta z as -PanOffset z and then overwrites it.
        HideObjectTools();
        double const Distance = static_cast<double>(MapExtentX) / static_cast<double>(ZoomScale);
        TransitionStart[0] = PanOffset[0];
        TransitionStart[1] = PanOffset[1];
        TransitionStart[2] = PanOffset[2];
        ZoomDistance = static_cast<float>(Distance);
        TransitionDelta[0] = -PanOffset[0];
        TransitionDelta[1] = -PanOffset[1];
        TransitionDelta[2] = -PanOffset[2];
        bTransition = 1;
        TransitionDelta[2] = static_cast<float>(static_cast<double>(MapExtentX) - Distance);
        TransitionView = 1;
        TransitionTime = 0.0f;
        TransitionDuration = kNavMapZoomOutSeconds;
        NnToolTipHideGlobal(); // FUN_004be390
        PlayOrRewindUiSoundSlot(7);
        return 1;
    }
    if (A == reinterpret_cast<uintptr_t>(BaseTitleText) ||
        A == reinterpret_cast<uintptr_t>(SystemTitleText))
    {
        // Sort by the clicked column.
        SortBaseList(A == reinterpret_cast<uintptr_t>(BaseTitleText) ? 1 : 0); // FUN_004a2760
        RefreshBaseList();
        return 1;
    }
    for (int Row = 0; Row < 3; ++Row)
    {
        if (A != reinterpret_cast<uintptr_t>(MissionObjective[Row]))
            continue;
        // LAB_00494226: open the system of the clicked objective.
        if (bBestPathObjectives != 0)
        {
            PendingSystem = BestPathWaypointSystem(ObjectiveFirstRow + Row); // FUN_0049a8f0
            return 1;
        }
        int const Waypoint = ObjectiveFirstRow + Row;
        if (Waypoint >= 0 && Waypoint < GetPlayerWaypointCountRetail())
        {
            PendingSystem = Universe::get_system(GetPlayerWaypointRetail(Waypoint)->SystemId);
        }
        return 1;
    }
    for (int Row = 0; Row < 15; ++Row)
    {
        if (A != reinterpret_cast<uintptr_t>(BaseTexts[Row]) &&
            A != reinterpret_cast<uintptr_t>(BaseSystemTexts[Row]))
            continue;

        // LAB_00494290: the base row -- the physical filter, its system with its name (or the
        // generic ids 0x62a for the unnamed ones), then a zoom on the base. The row is read
        // through retail's unchecked pointer (rows past the list are hidden, RefreshBaseList).
        FNavMapBaseEntry const* const Entry = BaseEntries.data() + (BaseListFirstRow + Row);
        SetFilterMode(0);
        CurrentSystem = Universe::get_system(Entry->SystemId);
        if (CurrentSystem != nullptr && !IsSkippedSystem(CurrentSystem))
        {
            // Retail tests CurrentSystem again here.
            GetIdsStringWWithRawFallback(static_cast<unsigned>(CurrentSystem->GetStridName()),
                                         G_NnTextScratchB, G_NnTextScratchCapacity);
            CharUpperW(G_NnTextScratchB);
            SystemNameText->SetProperty(0x1d, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
        }
        else
        {
            SystemNameText->SetProperty(0x1e, 0x62a, 0);
        }
        ZoomScale = 1.0f;
        bViewSwitch = 0;
        bTransition = 0;
        EnterSystemView(); // FUN_0049e1d0

        // The base in map-frame fractions of the new system (x kept in
        // the FPU, z * scale stored as a float first).
        double const Scale = static_cast<double>(SystemScale);
        double const X = static_cast<double>(Entry->Pos[0]) * Scale;
        float const Z = static_cast<float>(static_cast<double>(Entry->Pos[2]) * Scale);
        ClickU = static_cast<float>((X + static_cast<double>(MapExtentX)) /
                                    (static_cast<double>(MapExtentX) +
                                     static_cast<double>(MapExtentX)));
        ClickV = static_cast<float>((static_cast<double>(Z) + static_cast<double>(MapExtentZ)) /
                                    (static_cast<double>(MapExtentZ) +
                                     static_cast<double>(MapExtentZ)));
        ZoomToClick();
        return 1;
    }
    return 0;
}

// ----------------------------------------------------------------------------------------
// Slot 19 -- the mouse
// ----------------------------------------------------------------------------------------

bool NeuroNetNavMap::MouseEvent(FWin3DMouseEvent const* Event)
{
    // LAB_00497d30 (RET 4). Only the -1 / -2 button records (FUN_0059d140).
    if (Event->Button != kWin3DMouseLeft && Event->Button != kWin3DMouseRight)
        return false;
    NnToolTipHideGlobal(); // FUN_004be390

    // Nothing in the legend, info card and base list modes.
    if (G_NavMapFilterMode == 4 || G_NavMapFilterMode == 6 || G_NavMapFilterMode == 7)
        return true;

    // The cursor inside ScreenRect (x kept in double, y rounded to float). x87: a negative (or
    // unordered) offset is rejected, as is one at or past the size.
    int MouseX = 0;
    int MouseY = 0;
    GetClientSoftwareCursor(&MouseX, &MouseY, nullptr); // DAT_00616840 / DAT_00616844
    double const Dx = static_cast<double>(MouseX) - static_cast<double>(ScreenRectMin[0]);
    float const Dy = static_cast<float>(static_cast<double>(MouseY) -
                                        static_cast<double>(ScreenRectMin[1]));
    if (!(Dx >= 0.0) || !(Dy >= 0.0f))
        return true;
    if (!(Dx < static_cast<double>(ScreenRectSize[0])) || !(Dy < ScreenRectSize[1]))
        return true;

    // A press while zoomed in arms a drag (Update moves the pan).
    if (Event->Action == kWin3DMousePress && ViewMode == 2)
    {
        bMouseDown = 1;
        bDragging = 0;
        DragMouseX = MouseX;
        DragMouseY = MouseY;
    }

    if (Event->Action == kWin3DMouseDoubleClick)
    {
        // The info card of the star under the cursor, else of the
        // zone at the LAST CLICK (retail reads ClickMapPos here, not the cursor's point).
        float MapPos[3];
        if (!MapPointUnderCursor(MapPos)) // FUN_00492410
            return true;
        FNavMapItem* const Item = ItemAt(MapPos, 0);
        if (Item != nullptr)
        {
            if (Item->Kind != 0)
                return true;
            ShowItemInfoCard(*this, Item);
            return true;
        }
        ShowZoneInfoCard(*this);
        return true;
    }
    if (Event->Action != kWin3DMouseRelease)
        return true;

    // LAB_00497fa7: a release, unless it ended a drag.
    if (bDragging != 0)
        return true;
    if (bDebugTeleportMode == 0 || (ViewMode != 1 && ViewMode != 2))
    {
        ShowObjectTools(); // FUN_0049ea50
        return true;
    }

    // The debug beam -- the player ship to the cursor's point.
    if (IsPlayerDockedRetail()) // FUN_004c4810
        return true;
    if (ViewMode == 0) // retail's own test
        return true;
    float MapPos[3];
    if (!MapPointUnderCursor(MapPos))
        return true;
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW()); // FUN_0054baf0
    if (Player == nullptr)
        return true;
    double const InvScale = 1.0 / static_cast<double>(SystemScale);
    Vector Pos;
    Pos.x = static_cast<float>(static_cast<double>(MapPos[0]) * InvScale);
    Pos.y = static_cast<float>(static_cast<double>(MapPos[1]) * InvScale);
    Pos.z = static_cast<float>(static_cast<double>(MapPos[2]) * InvScale);
    // The IObjInspectImpl's CBase (+0xc) and its object, when that
    // object's class has bit 0; retail calls through it untested.
    CObject* Object = nullptr;
    IObjInspectImpl* const Impl = static_cast<IObjInspectImpl*>(Player);
    CBase* const Base = Impl;
    if (Base != nullptr)
    {
        CObject* const Candidate = Base->cobject();
        if (Candidate != nullptr && (static_cast<unsigned>(Candidate->ObjectClass) & 1u) == 1u)
            Object = Candidate;
    }
    // CObject slot +0x7c beam_object(Pos, the player's orientation,
    // false). Retail takes the orientation through IObjInspect slot 3 (+0x0c, get_transform),
    // whose body 0x062bf1f0 is folded with slot 4 get_orientation.
    Object->beam_object(Pos, Player->get_orientation(), false);
    bDebugTeleportMode = 0;
    DebugText->SetProperty(1, 1, 0);
    return true;
}

bool NeuroNetNavMap::CursorMapFraction(float* OutU, float* OutV)
{
    // FUN_004980d0. The ScreenRect test of FUN_00492410.
    int MouseX = 0;
    int MouseY = 0;
    GetClientSoftwareCursor(&MouseX, &MouseY, nullptr);
    float const ScreenX = static_cast<float>(MouseX);
    float const ScreenY = static_cast<float>(MouseY);
    double const Dx = static_cast<double>(MouseX) - static_cast<double>(ScreenRectMin[0]);
    float const Dy = static_cast<float>(static_cast<double>(MouseY) -
                                        static_cast<double>(ScreenRectMin[1]));
    if (!(Dx >= 0.0) || !(Dy >= 0.0f))
        return false;
    if (!(Dx < static_cast<double>(ScreenRectSize[0])) || !(Dy < ScreenRectSize[1]))
        return false;

    // The camera ray (slot 0x38) scaled to the map plane, as
    // fractions of the map frame: U = x / (MapSize x / 2), V = -y / (MapSize y / 2), each
    // then * 0.5 + 0.5.
    float Ray[3];
    AsCamera(&Camera)->Unproject(Ray, ScreenX, ScreenY);
    double const K = kNavMapPlaneDepth / static_cast<double>(Ray[2]);
    double const RayX = static_cast<double>(Ray[0]) * K;
    float const RayY = static_cast<float>(static_cast<double>(Ray[1]) * K);
    *OutU = static_cast<float>(RayX / (static_cast<double>(MapSize[0]) * 0.5));
    *OutV = static_cast<float>(-(static_cast<double>(RayY) /
                                 (static_cast<double>(MapSize[1]) * 0.5)));
    *OutU = static_cast<float>(static_cast<double>(*OutU) * 0.5 + 0.5);
    *OutV = static_cast<float>(static_cast<double>(*OutV) * 0.5 + 0.5);
    return true;
}

void NeuroNetNavMap::ShowObjectTools()
{
    // FUN_0049ea50. Not during a transition; the cursor's map point,
    // ray point and frame fractions.
    if (bTransition != 0)
        return;
    float MapPos[3];
    if (!MapPointUnderCursor(MapPos)) // FUN_00492410
        return;
    float RayPos[3];
    if (!CursorRayPoint(RayPos)) // FUN_0049e890
        return;
    float U = 0.0f;
    float V = 0.0f;
    if (!CursorMapFraction(&U, &V)) // FUN_004980d0
        return;

    // Filter modes 1 / 2 -- none in the corner right of U 0.81 and below V -0.0133 (x87
    // compares; unordered does not return).
    if ((G_NavMapFilterMode == 1 || G_NavMapFilterMode == 2) && U > kNavMapToolsLegendU &&
        V < kNavMapToolsLegendV)
        return;

    // The tools around the ray point -- zoom / jump left, waypoint /
    // select right, info above, clear / best path below (property 6 = position).
    float Pos[3];
    Pos[0] = static_cast<float>(static_cast<double>(RayPos[0]) -
                                static_cast<double>(kNavMapToolSpacing));
    Pos[1] = RayPos[1];
    Pos[2] = RayPos[2];
    InZoomButton->SetProperty(6, reinterpret_cast<uintptr_t>(Pos), 0);
    OutZoomButton->SetProperty(6, reinterpret_cast<uintptr_t>(Pos), 0);
    JumpGateButton->SetProperty(6, reinterpret_cast<uintptr_t>(Pos), 0);
    Pos[0] = static_cast<float>(static_cast<double>(RayPos[0]) +
                                static_cast<double>(kNavMapToolSpacing));
    Pos[1] = RayPos[1];
    Pos[2] = RayPos[2];
    SetWaypointButton->SetProperty(6, reinterpret_cast<uintptr_t>(Pos), 0);
    SelectObjectButton->SetProperty(6, reinterpret_cast<uintptr_t>(Pos), 0);
    Pos[0] = RayPos[0];
    Pos[1] = static_cast<float>(static_cast<double>(RayPos[1]) +
                                static_cast<double>(kNavMapToolSpacing));
    Pos[2] = RayPos[2];
    InfoButton->SetProperty(6, reinterpret_cast<uintptr_t>(Pos), 0);
    Pos[0] = RayPos[0];
    Pos[1] = static_cast<float>(static_cast<double>(RayPos[1]) -
                                static_cast<double>(kNavMapToolSpacing));
    Pos[2] = RayPos[2];
    ClearWaypointButton->SetProperty(6, reinterpret_cast<uintptr_t>(Pos), 0);
    BestPathButton->SetProperty(6, reinterpret_cast<uintptr_t>(Pos), 0);

    // What is under the cursor.
    unsigned char bInfo = 0;
    unsigned char bJump = 0;
    unsigned char bWaypointEnd = 0;
    unsigned char bWaypointMarker = 0;
    FNavMapItem* const Item = ItemAt(MapPos, 0); // FUN_00496d40
    if (Item != nullptr)
    {
        if (Item->Kind == 3)
        {
            // A waypoint marker. Its number (Variant) against the
            // smallest / largest AutoId marks the route's ends; its record's target object
            // (ObjectId, +0x10), when shown and a star, allows the jump.
            bWaypointMarker = 1;
            int const Count = GetPlayerWaypointCountRetail(); // FUN_004c4690
            int MaxId = -1;
            int MinId = 0x7fffffff;
            for (int Index = 0; Index < Count; ++Index)
            {
                int const Id = GetPlayerWaypointRetail(Index)->AutoId; // FUN_004c46a0
                if (Id > MaxId)
                    MaxId = Id;
                if (Id < MinId)
                    MinId = Id;
            }
            if (Item->Variant == MaxId || Item->Variant == MinId)
                bWaypointEnd = 1;
            FPlayerWaypointRecord const* const Record = GetPlayerWaypointRetail(Item->Ordinal - 1);
            if (Record != nullptr && Record->ObjectId != 0)
            {
                for (std::list<FNavMapItem*>::iterator It = Items.begin(); It != Items.end();
                     ++It)
                {
                    FNavMapItem* const Target = *It;
                    if (Target->bShown == 0 || Target->Id != Record->ObjectId)
                        continue;
                    if (Target->System != nullptr)
                        bJump = 1;
                    break;
                }
            }
        }
        else if (Item->Kind == 4)
        {
            // A best-path stop over a star (in a system view).
            FNavMapItem* const Under = ItemAt(MapPos, 1);
            if (Under != nullptr && ViewMode != 0 && Under->System != nullptr)
            {
                bInfo = 1;
                bJump = 1;
            }
        }
        else if (Item->Kind == 0 || Item->Kind == 1)
        {
            bInfo = 1;
            if (ViewMode != 0 && Item->System != nullptr)
                bJump = 1;
        }

        // LAB_0049ed01: a waypoint marker or stop over a Kind 0 item has its info.
        if (Item->Kind == 3 || Item->Kind == 4)
        {
            FNavMapItem* const Under = ItemAt(MapPos, 1);
            if (Under != nullptr && Under->Kind == 0)
                bInfo = 1;
        }
    }

    // LAB_0049ed29: the universe view needs an item; else a named-info zone at the point.
    if (ViewMode == 0 && bInfo == 0)
        return;
    unsigned char bZoneHit = 0;
    if (bInfo == 0)
    {
        double const InvScale = 1.0 / static_cast<double>(SystemScale);
        float Scaled[3];
        Scaled[0] = static_cast<float>(static_cast<double>(MapPos[0]) * InvScale);
        Scaled[1] = static_cast<float>(static_cast<double>(MapPos[1]) * InvScale);
        Scaled[2] = static_cast<float>(InvScale * static_cast<double>(MapPos[2]));
        if (NavMapZoneAt(CurrentSystem, Scaled, 0, 1, bDebugShowAll) != nullptr) // FUN_004a1b50
        {
            bInfo = 1;
            bZoneHit = 1;
        }
    }

    // LAB_0049eda3: remember the click, show the option bridge at the ray point
    // plus its attach position (RayPos moved in place).
    ClickMapPos[0] = MapPos[0];
    ClickMapPos[1] = MapPos[1];
    ClickMapPos[2] = MapPos[2];
    ClickRayPos[0] = RayPos[0];
    ClickRayPos[1] = RayPos[1];
    ClickRayPos[2] = RayPos[2];
    ClickV = V;
    ClickU = U;
    bObjectToolsShown = 1;
    OptionBridge->SetProperty(1, 0, 0);
    RayPos[0] = static_cast<float>(static_cast<double>(RayPos[0]) +
                                   static_cast<double>(OptionBridge->AttachPosition[0]));
    RayPos[1] = static_cast<float>(static_cast<double>(RayPos[1]) +
                                   static_cast<double>(OptionBridge->AttachPosition[1]));
    RayPos[2] = static_cast<float>(static_cast<double>(RayPos[2]) +
                                   static_cast<double>(OptionBridge->AttachPosition[2]));
    OptionBridge->SetProperty(6, reinterpret_cast<uintptr_t>(RayPos), 0);

    // The zoom button of the view, hidden when the jump applies.
    WinControl* const ZoomButton = ViewMode == 2 ? OutZoomButton : InZoomButton;
    ZoomButton->SetProperty(1, bJump, 0);

    // No more waypoints past AutoId 99.
    unsigned char const bWaypointsFull = GetPlayerWaypointMaxIdRetail() >= 0x63 ? 1 : 0;

    // Select (in the player's system, undocked; in the unzoomed
    // system view only markers of scaled size >= 700; a route end only when it is waypoint 1)
    // and clear (a route end in a system view).
    unsigned char bSelect = 0;
    uintptr_t bHideClear = 1;
    if (bInfo != 0 || bWaypointEnd != 0)
    {
        bSelect = 1;
        if (ViewMode == 0 || IsPlayerDockedRetail())
            bSelect = 0;
        if (CurrentSystem->GetId() != GetPlayerStarSystemIdRetail())
            bSelect = 0;
        if (bWaypointEnd != 0)
        {
            // LAB_0049f022
            if (Item->Variant != 1)
                bSelect = 0;
            if (ViewMode != 0)
                bHideClear = 0;
        }
        else if (ViewMode == 1 && Item != nullptr)
        {
            // x87: kept on not-less (or unordered).
            if (static_cast<double>(SystemScale) * static_cast<double>(Item->Size) <
                static_cast<double>(kNavMapSelectMinSize))
                bSelect = 0;
        }
    }
    ClearWaypointButton->SetProperty(1, bHideClear, 0);
    InfoButton->SetProperty(1, bInfo == 0 ? 1 : 0, 0);

    // A best path with text blocks a new one; a zone is neither
    // selectable nor a waypoint target.
    unsigned char bBestPathLocked = 0;
    if (BestPathGetWaypointCount() > 0 && BestPathHasText()) // FUN_004c4fb0 / FUN_004c58e0
        bBestPathLocked = 1;
    if (bZoneHit != 0)
    {
        bSelect = 0;
        bInfo = 0;
    }

    // Set waypoint / best path, jump, select, then the sound.
    uintptr_t bHideSetWaypoint = 0;
    uintptr_t bHideBestPath = 0;
    if (bWaypointsFull != 0 || bWaypointEnd != 0 || ViewMode == 0 || bWaypointMarker != 0 ||
        bBestPathLocked != 0)
    {
        bHideSetWaypoint = 1;
        bHideBestPath = 1;
    }
    else if (bInfo != 0)
    {
        bHideSetWaypoint = 1;
    }
    SetWaypointButton->SetProperty(1, bHideSetWaypoint, 0);
    BestPathButton->SetProperty(1, bHideBestPath, 0);
    JumpGateButton->SetProperty(1, bJump == 0 ? 1 : 0, 0);
    SelectObjectButton->SetProperty(1, bSelect == 0 ? 1 : 0, 0);
    PlayOrRewindUiSoundSlot(9);
}

// ----------------------------------------------------------------------------------------
// The info card and the abort prompt
// ----------------------------------------------------------------------------------------

unsigned NeuroNetNavMap::ObjectBaseId(unsigned ObjectId)
{
    // FUN_004978c0 (RET 4). CurrentSystem is used untested, as in retail.
    Universe::FSystemObject Object;
    unsigned const Count = static_cast<unsigned>(CurrentSystem->GetObjectCount());
    for (unsigned Index = 0; Index < Count; ++Index)
    {
        if (CurrentSystem->GetSystemObject(Index, &Object) && CreateID(Object.Nickname.Data) == ObjectId)
            return Object.BaseId;
    }
    return 0;
}

void NeuroNetNavMap::ShowInfoCard(int IdsName, int IdsInfo, unsigned char Visit, unsigned BaseId)
{
    // FUN_00497970 (RET 0x10).
    if (IdsName == 0 && IdsInfo == 0)
        return;

    // The title (upper case, a line break) in style 0xf64, then style
    // 0x9ca4 for the body.
    RenderDisplayList Card;
    {
        StyleNode Style(kTextStyleCardTitle);
        RdlAppendNode(Card, Style.Clone());
    }
    if (IdsName != 0)
    {
        GetIdsStringWWithRawFallback(static_cast<unsigned>(IdsName), G_NnTextScratchB,
                                     G_NnTextScratchCapacity);
        CharUpperW(G_NnTextScratchB);
        wcscat(G_NnTextScratchB, L"\n"); // DAT_005d0a08
        AppendTextMaybeXmlRdl(G_NnTextScratchB, static_cast<unsigned>(wcslen(G_NnTextScratchB)),
                              Card, false);
    }
    {
        StyleNode Style(kTextStyleBody);
        RdlAppendNode(Card, Style.Clone());
    }
    G_NnTextScratchB[0] = 0;

    // The info text, and from the InfocardMap the base sections (with a
    // base) and the extra ids (visit bit 1).
    if (IdsInfo != 0)
    {
        LoadIdsIntoRdl(static_cast<unsigned>(IdsInfo), Card); // FUN_0057da40
        FInfocardMapEntry const* Entry = nullptr;
        if (InfocardMapFind(static_cast<unsigned>(IdsInfo), &Entry)) // FUN_00458890
        {
            if (BaseId != 0)
            {
                unsigned char const bSection1 =
                    ((Visit & 4) != 0 && Entry->bShowCommodities != 0) ? 1 : 0;
                unsigned char const bSection2 =
                    ((Visit & 8) != 0 && Entry->bShowEquipment != 0) ? 1 : 0;
                unsigned char const bSection3 =
                    ((Visit & 0x10) != 0 && Entry->bShowShips != 0) ? 1 : 0;
                // FUN_00476080(&card, base, commodities, equipment,
                // ships).
                NnHelpAppendBaseGoods(&Card, BaseId, bSection1, bSection2, bSection3);
            }
            if ((Visit & 2) != 0 && Entry->ExtraIds != 0)
            {
                wcscpy(G_NnTextScratchB, L"<RDL><TEXT><PARA/></TEXT></RDL>");
                AppendTextMaybeXmlRdl(G_NnTextScratchB,
                                      static_cast<unsigned>(wcslen(G_NnTextScratchB)), Card,
                                      false);
                LoadIdsIntoRdl(static_cast<unsigned>(Entry->ExtraIds), Card);
            }
        }
    }

    // Show the card and its close button, filter mode 6 (the mode
    // before it kept), the faction labels and the object tools hidden.
    InfoCard->Flags = static_cast<unsigned char>(InfoCard->Flags | 3);
    CloseInfocardButton->Flags = static_cast<unsigned char>(CloseInfocardButton->Flags | 3);
    CloseInfocardText->Flags = static_cast<unsigned char>(CloseInfocardText->Flags | 3);
    InfoCard->ClearContent();       // FUN_0057b370
    InfoCard->AppendContent(Card);  // FUN_0057b3e0
    InfoCard->SetScrollOrigin(0, 0); // FUN_005798b0
    FilterModeBeforeInfoCard = G_NavMapFilterMode;
    SetFilterMode(6);
    for (int Index = 0; Index < 4; ++Index)
        FactionLabels[Index]->SetProperty(1, 1, 0);
    HideObjectTools();
}

void NeuroNetNavMap::AskAbortMission()
{
    // FUN_0049f2e0. The abort button goes; with a route its title and the question (ids 0x4db
    // in single player, 0x689 in multiplayer) replace the info text, with yes / no.
    AbortMissionButton->SetProperty(1, 1, 0);
    AbortMissionText->SetProperty(1, 1, 0);
    if (G_BestPaths.empty()) // DAT_00672950
        return;

    FmtStrSprintf(&G_BestPaths.begin()->second.Title, G_NnTextScratchB, G_NnTextScratchCapacity);
    wcscat(G_NnTextScratchB, L"\n"); // DAT_005d0a08
    GetIdsStringWWithRawFallback(SinglePlayer() ? 0x4db : 0x689, G_NnTextScratchA,
                                 G_NnTextScratchCapacity);
    wcscat(G_NnTextScratchB, G_NnTextScratchA);
    InfoText->Flags = static_cast<unsigned char>(InfoText->Flags | 2);

    RenderDisplayList Text;
    {
        StyleNode Style(kTextStyleBody);
        RdlAppendNode(Text, Style.Clone());
    }
    AppendTextMaybeXmlRdl(G_NnTextScratchB, static_cast<unsigned>(wcslen(G_NnTextScratchB)),
                          Text, false);
    InfoText->ClearContent();
    InfoText->AppendContent(Text);
    InfoText->SetScrollOrigin(0, 0);

    AbortYesButton->SetProperty(1, 0, 0);
    AbortYesText->SetProperty(1, 0, 0);
    AbortNoButton->SetProperty(1, 0, 0);
    AbortNoText->SetProperty(1, 0, 0);
}

void NeuroNetNavMap::ConfirmAbortMission()
{
    // FUN_0049f540. Yes / no hidden, the info text and the objective
    // rows blanked.
    AbortYesButton->SetProperty(1, 1, 0);
    AbortYesText->SetProperty(1, 1, 0);
    AbortNoButton->SetProperty(1, 1, 0);
    AbortNoText->SetProperty(1, 1, 0);
    G_NnTextScratchB[0] = 0;

    RenderDisplayList Text;
    {
        StyleNode Style(kTextStyleBody);
        RdlAppendNode(Text, Style.Clone());
    }
    AppendTextMaybeXmlRdl(G_NnTextScratchB, static_cast<unsigned>(wcslen(G_NnTextScratchB)),
                          Text, false);
    InfoText->ClearContent();
    InfoText->AppendContent(Text);
    InfoText->SetScrollOrigin(0, 0);
    InfoText->Flags = static_cast<unsigned char>(InfoText->Flags & 0xfd);
    for (int Row = 0; Row < 3; ++Row)
        ObjectiveText[Row]->SetProperty(0x1d, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
    ObjectiveScroll->SetProperty(2, 1, 0);

    // Abort the first route's mission (not kinds 7 / 8), then leave
    // the briefing (continue + close) or the NeuroNet screen.
    FBestPath const* const First = BestPathGetFirst(); // FUN_004c5040
    if (First == nullptr || First->MissionType == 7 || First->MissionType == 8)
        return;
    G_Server->AbortMission(GetPlayerConnectionId(), First->MissionType); // IServer slot 68
    StoryStarReset(); // FUN_004b9bb0
    if (bBriefing != 0)
    {
        RequestMissionBriefingContinue(); // FUN_0043d580
        ProcessMessage(0xef02, 0, 0);
    }
    else
    {
        NnMainButtonOnModalOpen(); // FUN_0045a460
    }
}

// ----------------------------------------------------------------------------------------
// The known-bases list
// ----------------------------------------------------------------------------------------

void NeuroNetNavMap::BuildBaseList()
{
    // FUN_004a2370.
    if (bBaseListBuilt != 0)
        return;
    bBaseListBuilt = 1;
    BaseEntries.clear(); // FUN_004a2fd0
    BaseNames.clear();   // FUN_004a3240
    SystemNames.clear();

    int BaseIndex = 0;
    int SystemIndex = -1;
    for (Universe::ISystem* System = Universe::GetFirstSystem(); System != nullptr;
         System = Universe::GetNextSystem())
    {
        // Visited systems only (the visit map | the system's own).
        unsigned char SystemVisit = 0;
        GetPlayerVisitFlagsRetail(System->GetId(), &SystemVisit, 0, 0); // FUN_004c4da0
        SystemVisit = static_cast<unsigned char>(SystemVisit | System->GetVisit());
        if ((SystemVisit & 1) == 0)
            continue;

        // The system's name, whether or not it has a listed base.
        GetIdsStringWWithRawFallback(static_cast<unsigned>(System->GetStridName()), G_NnTextScratchB,
                                     G_NnTextScratchCapacity);
        SystemNames.push_back(std::wstring(G_NnTextScratchB)); // FUN_00413df0 + FUN_004a3360
        ++SystemIndex;

        // Every visited [Object] with a base and a listed name.
        Universe::FSystemObject Object;
        unsigned const Count = static_cast<unsigned>(System->GetObjectCount());
        for (unsigned Index = 0; Index < Count; ++Index)
        {
            if (!System->GetSystemObject(Index, &Object) || Object.BaseId == 0)
                continue;
            unsigned char ObjectVisit = 0;
            GetPlayerVisitFlagsRetail(CreateID(Object.Nickname.Data), &ObjectVisit, 0, 0);
            ObjectVisit = static_cast<unsigned char>(ObjectVisit | Object.Visit);
            if ((ObjectVisit & 1) == 0)
                continue;
            // FUN_004a2310's eleven ids_name compares, inlined here in
            // the same order.
            if (!IsCommodityByBaseListedName(Object.IdsName))
                continue;

            FNavMapBaseEntry Entry;
            Entry.SystemId = System->GetId();
            Entry.Pos[0] = Object.Pos.x;
            Entry.Pos[1] = Object.Pos.y;
            Entry.Pos[2] = Object.Pos.z;
            Entry.BaseIndex = BaseIndex;
            Entry.SystemIndex = SystemIndex;
            ++BaseIndex;
            BaseEntries.push_back(Entry); // FUN_004a3680

            // The base's name (FUN_004142a0 + wcsncpy + NUL).
            GetIdsStringWWithRawFallback(static_cast<unsigned>(Object.IdsName), G_NnTextScratchB,
                                         G_NnTextScratchCapacity);
            BaseNames.push_back(std::wstring(G_NnTextScratchB));
        }
    }
    SortBaseList(1);
}

void NeuroNetNavMap::SortBaseList(unsigned char bByBase)
{
    // FUN_004a2760 (RET 4) -- a bubble sort over the adjacent pairs until a pass swaps
    // nothing; _wcsicmp > 0 swaps. By base: the base names; else the system names, the base
    // names on a tie.
    bBaseListByBase = bByBase;
    int const Last = static_cast<int>(BaseEntries.size()) - 1;
    bool bSorted = false;
    do
    {
        bSorted = true;
        if (Last <= 0)
            return;
        for (int Index = 0; Index < Last; ++Index)
        {
            FNavMapBaseEntry& First = BaseEntries[Index];
            FNavMapBaseEntry& Second = BaseEntries[Index + 1];
            int Order = 0;
            if (bByBase != 0)
            {
                Order = _wcsicmp(BaseNames[First.BaseIndex].c_str(),
                                 BaseNames[Second.BaseIndex].c_str());
            }
            else
            {
                Order = _wcsicmp(SystemNames[First.SystemIndex].c_str(),
                                 SystemNames[Second.SystemIndex].c_str());
                if (Order == 0)
                {
                    Order = _wcsicmp(BaseNames[First.BaseIndex].c_str(),
                                     BaseNames[Second.BaseIndex].c_str());
                }
            }
            if (Order > 0)
            {
                FNavMapBaseEntry const Swap = First;
                First = Second;
                Second = Swap;
                bSorted = false;
            }
        }
    } while (!bSorted);
}

void NeuroNetNavMap::RefreshBaseList()
{
    // FUN_004a2930. The fifteen rows from BaseListFirstRow (signed
    // test, as retail), shown with their base and system names or hidden past the end.
    int const Count = static_cast<int>(BaseEntries.size());
    for (int Row = 0; Row < 15; ++Row)
    {
        int const Index = BaseListFirstRow + Row;
        WinControl* const BaseText = BaseTexts[Row];
        WinControl* const SystemText = BaseSystemTexts[Row];
        if (Index >= Count)
        {
            BaseText->Flags = static_cast<unsigned char>(BaseText->Flags & 0xfc);
            SystemText->Flags = static_cast<unsigned char>(SystemText->Flags & 0xfc);
            continue;
        }
        BaseText->Flags = static_cast<unsigned char>(BaseText->Flags | 3);
        SystemText->Flags = static_cast<unsigned char>(SystemText->Flags | 3);
        FNavMapBaseEntry const* const Entry = BaseEntries.data() + Index;
        BaseText->SetProperty(0x1d,
                              reinterpret_cast<uintptr_t>(BaseNames[Entry->BaseIndex].c_str()), 0);
        SystemText->SetProperty(
            0x1d, reinterpret_cast<uintptr_t>(SystemNames[Entry->SystemIndex].c_str()), 0);
    }

    // The scroll bar only past fifteen rows (range + 2).
    WinControl* const Scroll = static_cast<WinControl*>(BaseListScroll);
    int const Extra = Count - 15;
    if (Extra <= 0)
    {
        Scroll->SetProperty(2, 1, 0);
        Scroll->Flags = static_cast<unsigned char>(Scroll->Flags & 0xfd);
        return;
    }
    Scroll->SetProperty(2, 0, 0);
    Scroll->Flags = static_cast<unsigned char>(Scroll->Flags | 2);
    Scroll->SetProperty(0x37, 0, static_cast<uintptr_t>(Extra + 2));
}
