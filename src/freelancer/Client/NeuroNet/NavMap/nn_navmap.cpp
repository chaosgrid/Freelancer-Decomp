// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// NeuroNetNavMap -- lifecycle, Create and the WinControl overrides (see nn_navmap.h).
// Retail Client\Interface\NeuroNet\NavMap.cpp.

#include "NeuroNet/NavMap/nn_navmap.h"
#include "NeuroNet/Economy/best_path.h"               // BestPathGetWaypointCount (FUN_004c4fb0)
#include "Interface/Pages/character_select_equip.h"  // GetPlayerStarSystemIdRetail, GetPlayerVisitFlagsRetail
#include "Common/Fonts/font_string.h"             // FFontString (the item label's dtor)
#include "GameFlow/gameflow.h"                // IsMissionBriefingNavMapUp / RequestMissionBriefingContinue
#include "Space/HUD/hud_command_dispatch.h"    // HudPostCommand (FUN_005763b0)
#include "Common/Fonts/ids_strings.h"             // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "Network/login_mgr.h"               // G_Server (DAT_0067ecd0)
#include "NeuroNet/nn_modal.h"                // NnFloatBits
#include "NeuroNet/nn_text_control.h"         // NnTextControl (+0x334 Font::String)
#include "NeuroNet/nn_text_scratch.h"         // DAT_0066dc60 / DAT_0066fc60 / DAT_006119f8
#include "NeuroNet/nn_ui_pulse.h"             // palette colours DAT_00679ba4 / DAT_00679bb0
#include "Space/player_spawn.h"            // GetPlayerConnectionId, GetPlayerWaypointCountRetail
#include "Interface/rich_text_win.h"
#include "Interface/ui_scroll.h"
#include "Interface/win3d.h"                   // Win3DReparent, Win3DDeleteTree
#include "Interface/win3d_engine_submit.h"     // Win3DRayHitsAabbHalfExtents (FUN_005a1130)
#include "Interface/win_factory.h"
#include "x86math.h"                 // Transform, I3DMathEngine
#include "common_csys.h"             // Acquire3DMathEngine (FUN_005b73e0)
#include "common_id.h"               // CreateID
#include "common_server.h"           // IServer
#include "common_misc.h"             // SinglePlayer, GetFileSysPath
#include "common_universe.h"
#include "dacom.h"
#include "DALib.h"                   // DALib::Engine / Pipeline / Data
#include "engine/engbase.h"          // IEngine
#include "render/IRenderPipeline.h" // IRPVertexBuffer / IRPIndexBuffer / IRPDraw

#include <io.h>                      // _access
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>                 // CharUpperW (FUN_005b74d9)

// --- Globals (retail NavMap.cpp .bss / .data) ---
int G_NavMapFilterMode = kNavMapFilterPhysical;               // DAT_006720a0
int G_NavMapZoneFilter = kNavMapZoneFilterAll;               // DAT_006720a4
unsigned char G_bNavMapNoBestPath = 0;    // DAT_006720a8
// DAT_00672048 -- LAB_0048d730 constructs it empty and registers LAB_0048d760 (free) with atexit.
std::vector<FNavMapGroupMember> G_NavMapGroupPositions;
unsigned G_NavMapRequestedSystem = 0;     // DAT_006720c4
float G_NavMapRequestedPos[3] = {0.0f, 0.0f, 0.0f}; // DAT_00672084..8c
unsigned char G_bNavMapRequestedPos = 0;  // DAT_006720c8
// DAT_0067203c..44 -- the XC-table initializer LAB_00494900 stores 0.005f / 0.048f / 0.
float G_NavMapScreenRectOffset[3] = {0.005f, 0.048f, 0.0f};
unsigned char G_bNavMapRouteChanged = 0;  // DAT_006720c9
unsigned char G_bNavMapShowLabels = 1;    // DAT_00611c5c

// DAT_0067209c -- the lazily acquired I3DMathEngine (FUN_005b73e0), shared by every function
// of the file.
I3DMathEngine* G_NavMapMath = nullptr;
// DAT_006720ac / DAT_006720b0 / DAT_006720b4 -- the pipeline interfaces the ctor queries once.
IRPVertexBuffer* G_NavMapVertexBuffer = nullptr;
IRPIndexBuffer* G_NavMapIndexBuffer = nullptr;
IRPDraw* G_NavMapDraw = nullptr;
// DAT_006720b8 / DAT_006720bc / DAT_006720c0 -- the vertex buffers of the terrain patches
// (FUN_0049c260), the zone shading (FUN_0049dcc0) and the trade lanes (FUN_004992b0); the dtor
// destroys all three through G_NavMapVertexBuffer's DestroyVb. The index data goes through the
// index buffer's shared scratch (handle -1), which owns no handle here.
void* G_NavMapBufferHandles[3] = {nullptr, nullptr, nullptr};

void NavMapScaleMeshRotation(FMeshHolder* Holder, float Factor)
{
    // Inlined in retail. No test of the instance handle.
    IEngine* const Engine = DALib::Engine;
    Transform Xform;
    memcpy(&Xform, Engine->GetInstanceTransform(Holder->EngineInstance), sizeof(Xform));
    if (G_NavMapMath == nullptr)
        G_NavMapMath = Acquire3DMathEngine();
    G_NavMapMath->Scale(Xform.Rotation, Xform.Rotation, Factor);
    Engine->SetInstanceMatrix(Holder->EngineInstance, &Xform);
}

namespace
{
    // WINFACTORY<NeuroNetNavMap> (static initializer LAB_0048d700; Create FUN_004a2e40).
    WINFACTORY<NeuroNetNavMap> G_NeuroNetNavMapFactory("NeuroNetNavMap");

    // DAT_005d2ed4 / DAT_005d2ed8 -- the two bases Create adds its right-panel offsets to.
    float const kNavMapPanelBaseA = -0.434f; // 0xbede353f
    float const kNavMapPanelBaseB = 0.421f;  // 0x3ed78d50

    char const kNavMapSpaceObjectsDir[] = "Interface\\NeuroNet\\NavMap\\NewNavMap\\SpaceObjects\\";
    char const kNavMapSpaceObjectsMat[] =
        "Interface\\NeuroNet\\NavMap\\NewNavMap\\SpaceObjects\\spaceobjects.mat";

    // DAT_00611c60 -- the terrain shape names (the last entry repeats nav_terrain_ice).
    char const* const kNavMapTerrainShapeNames[9] = {
        "asteroidtest",         "nav_terrain_debris", "dustcloud",
        "nav_terrain_mines",    "nav_terrain_badlands", "nav_terrain_ice",
        "nav_terrain_lava",     "nav_terrain_nomadast", "nav_terrain_ice"};

    // Inlined for each of the seven tool buttons. The button is not null-tested (neither is it
    // in retail).
    void ScaleToolButtonMesh(WinControl* Button)
    {
        NavMapScaleMeshRotation(&Button->MeshHolder, 1.7f);
    }

    // Inlined three times in Create: the objective line's Font::String goes multi-line,
    // dropping its layout when it changes.
    void SetObjectiveTextMultiLine(WinControl* Text)
    {
        FFontString* const Font = static_cast<NnTextControl*>(Text)->GetFontString();
        if (Font->bMultiLine != 1)
        {
            Font->bMultiLine = 1;
            Font->LayoutGen = 0;
        }
    }
}

// ----------------------------------------------------------------------------------------
// FNavMapItem
// ----------------------------------------------------------------------------------------

FNavMapItem::FNavMapItem()
    : System(nullptr)
    , Label(nullptr)
    , bLabelStarred(0)
    , bLabelCrowded(0)
{
    // Inlined in FUN_00491f90. +0x64 is written by AddItem right after; it is initialised here.
}

FNavMapItem::~FNavMapItem()
{
    // FUN_0048dd40.
    if (Label != nullptr)
    {
        // EnsureItemLabel allocates it with new.
        delete Label;
        Label = nullptr;
    }
    TooltipText.clear();
}

// ----------------------------------------------------------------------------------------
// NeuroNetNavMap -- construction / destruction
// ----------------------------------------------------------------------------------------

NeuroNetNavMap::NeuroNetNavMap()
{
    // FUN_0048de20. Retail stores are interleaved; the order here follows them roughly.
    Camera.Construct();   // FUN_0040f150

    ShipMarkerSize = 5000.0f;
    WaypointMarkerSize = 4500.0f;

    MapLabelOrigin[0] = 0.0f;
    MapLabelOrigin[1] = 0.0f;
    MapLabelOrigin[2] = 0.0f;
    MapRotation.E[0][0] = 1.0f;
    MapRotation.E[0][1] = 0.0f;
    MapRotation.E[0][2] = 0.0f;
    MapRotation.E[1][0] = 0.0f;
    MapRotation.E[1][1] = 1.0f;
    MapRotation.E[1][2] = 0.0f;
    MapRotation.E[2][0] = 0.0f;
    MapRotation.E[2][1] = 0.0f;
    MapRotation.E[2][2] = 1.0f;

    // Retail re-stores the holders' instance slot (already -1).
    ZoomedMapMesh.EngineInstance = InvalidInstanceIndex;
    ZoomedMapMesh2.EngineInstance = -1;
    PrettyMapMesh.EngineInstance = static_cast<intptr_t>(-1);
    CachedObjectId = 0xffffffffu;
    HoveredObjective = -1;

    ZoomScale = 1.0f;
    bInitialShowDone = 0;
    PlayerItem = nullptr;
    ViewMode = kNavMapViewSystem;
    bMouseDown = 0;
    bDragging = 0;
    bDebugTeleportMode = 0;
    bTransition = 0;
    PendingSystem = nullptr;
    bSystemViewPending = 0;
    bDrawnOnce = 0;
    CurrentSystem = nullptr;
    bObjectToolsShown = 0;
    ObjectiveFirstRow = 0;

    for (int Index = 0; Index < 0x63; ++Index)
    {
        WaypointItems[Index] = nullptr;
        BestPathItems[Index] = nullptr;
    }

    MeshPosition[0] = 0.0f;
    MeshPosition[1] = 0.0f;
    MeshPosition[2] = -0.71f;
    ZoomedMeshPosition[0] = 0.0f;
    ZoomedMeshPosition[1] = 0.0f;
    ZoomedMeshPosition[2] = -0.55f;
    CachedWaypointHash = 0;

    ScreenRectSize[0] = 1.0f;
    ScreenRectSize[1] = 1.0f;
    ScreenRectSize[2] = 0.0f;
    ScreenRectMin[0] = 0.0f;
    ScreenRectMin[1] = 0.0f;
    ScreenRectMin[2] = 0.0f;
    MapOrigin[0] = -0.234f;
    MapOrigin[1] = -0.234f;
    MapOrigin[2] = 0.0f;
    MapSize[0] = 0.468f;
    MapSize[1] = 0.468f;
    MapSize[2] = 0.0f;

    MapExtentX = 112000.0f;
    MapExtentZ = 112000.0f;
    BlinkTimer = 0.0f;
    bBlink = 0;
    SolarTypeMask = 0x2020bdffu;
    bResourcesLoaded = 0;
    bDebugShowAll = 0;
    bDrawZoneOutlines = 0;
    GroupMarkerMesh.EngineInstance = InvalidInstanceIndex;
    Field_0x8c4 = 0;
    bViewSwitch = 0;
    bOutlineTerrainZonesOnly = 0;
    bSidePanelsOpen = 0;
    PulseTimer = 0;
    PulseIndex = -1;
    ZoomDistance = 1.0f;
    bBestPathObjectives = 1;
    LegendMesh.EngineInstance = InvalidInstanceIndex;
    bItemClamped = 0;

    G_bNavMapNoBestPath = 0;

    TradeLaneCachePan[0] = 0.0f;
    TradeLaneCachePan[1] = 0.0f;
    TradeLaneCachePan[2] = 0.0f;
    NoBestPathTimer = 0.0f;
    GroupPollTimer = 0.0f;
    TradeLaneFrame = 0;
    TradeLaneFrameTimer = 0.0f;
    LastWaypointCount = -1;
    TradeLaneCacheZoom = -1.0f;
    TradeLaneCacheSystem = 0;
    TradeLaneCacheSegments = 0;
    TerrainCachePan[0] = 0.0f;
    TerrainCachePan[1] = 0.0f;
    TerrainCachePan[2] = 0.0f;
    ShadeCachePan[0] = 0.0f;
    ShadeCachePan[1] = 0.0f;
    TerrainCacheZoom = -1.0f;
    TerrainCacheSystem = 0;
    ShadeCachePan[2] = 0.0f;
    ShadeCacheZoom = -1.0f;
    ShadeCacheSystem = 0;
    ShadeCacheZoneFilter = -1;
    ShadeCacheFilterMode = -1;
    bBriefing = 0;

    // In multiplayer ask the server for the group positions (IServer slot 75).
    if (!SinglePlayer())
    {
        unsigned Zero = 0;
        G_Server->RequestGroupPositions(GetPlayerConnectionId(),
                                        reinterpret_cast<unsigned char*>(&Zero), 4);
    }

    TradeLaneVertices = static_cast<unsigned char*>(operator new(0x6000));
    TradeLaneIndices = static_cast<unsigned short*>(operator new(0x1800));
    TradeLaneVertexCount = 0;
    TradeLaneIndexCount = 0;
    TerrainVertices = static_cast<unsigned char*>(operator new(0x9000));
    TerrainIndices = static_cast<unsigned short*>(operator new(0x2400));
    TerrainVertexCount = 0;
    TerrainIndexCount = 0;
    TerrainZoneCount = 0;
    ShadeZoneCount = 0;
    TerrainRecheckTimer = 1.0f; // DAT_005d2ecc
    ShadeVertices = static_cast<unsigned char*>(operator new(0x17700));
    ShadeIndices = static_cast<unsigned short*>(operator new(0x8ca0));
    ShadeVertexCount = 0;
    ShadeIndexCount = 0;
    ShadeRecheckTimer = 1.0f; // DAT_005d2ed0

    // The pipeline interfaces, once per process.
    if (G_NavMapVertexBuffer == nullptr)
        DALib::Pipeline->QueryInterface(Iids::RPVertexBuffer,
                                        reinterpret_cast<void**>(&G_NavMapVertexBuffer));
    if (G_NavMapIndexBuffer == nullptr)
        DALib::Pipeline->QueryInterface(Iids::RPIndexBuffer,
                                        reinterpret_cast<void**>(&G_NavMapIndexBuffer));
    if (G_NavMapDraw == nullptr)
        DALib::Pipeline->QueryInterface(Iids::RPDraw, reinterpret_cast<void**>(&G_NavMapDraw));

    // The systems the nav map never shows a name for.
    SkipSystemIds[0] = Universe::get_system_id("St02");
    SkipSystemIds[1] = Universe::get_system_id("St03");
    SkipSystemIds[2] = Universe::get_system_id("St03b");
    SkipSystemIds[3] = Universe::get_system_id("St02c");

    FilterModeBeforeInfoCard = 0;
    SystemScale = 1.0f;
    G_bNavMapRouteChanged = 0;
    bBaseListBuilt = 0;
    BaseListFirstRow = 0;
    bBaseListByBase = 1;
}

NeuroNetNavMap::~NeuroNetNavMap()
{
    // FUN_0048e650.
    operator delete(TerrainVertices);
    TerrainVertices = nullptr;
    operator delete(TerrainIndices);
    TerrainIndices = nullptr;
    operator delete(ShadeVertices);
    ShadeVertices = nullptr;
    operator delete(ShadeIndices);
    ShadeIndices = nullptr;
    operator delete(TradeLaneVertices);
    TradeLaneVertices = nullptr;
    operator delete(TradeLaneIndices);
    TradeLaneIndices = nullptr;

    if (ZoomedMapMesh.EngineInstance != InvalidInstanceIndex)
        ZoomedMapMesh.ReleaseWithAnim();
    if (ZoomedMapMesh2.EngineInstance != InvalidInstanceIndex)
        ZoomedMapMesh2.ReleaseWithAnim();
    if (PrettyMapMesh.EngineInstance != InvalidInstanceIndex)
        PrettyMapMesh.ReleaseWithAnim();
    if (GroupMarkerMesh.EngineInstance != InvalidInstanceIndex)
        GroupMarkerMesh.ReleaseWithAnim();
    if (LegendMesh.EngineInstance != InvalidInstanceIndex)
        LegendMesh.ReleaseWithAnim();

    // The digit holders are released unconditionally.
    for (int Index = 0; Index < 10; ++Index)
        DigitMeshes[Index].ReleaseWithAnim();

    // Here (and only here) the jump link records are freed.
    while (!JumpLinks.empty())
    {
        FNavMapJumpLink* const Link = JumpLinks.front();
        operator delete(Link);
        JumpLinks.pop_front();
    }

    while (!Items.empty())
    {
        FNavMapItem* const Item = Items.front();
        if (Item != nullptr)
            delete Item;
        Items.pop_front();
    }

    for (int Index = 0; Index < 0x63; ++Index)
    {
        WaypointItems[Index] = nullptr;
        BestPathItems[Index] = nullptr;
    }

    Shapes.Clear(); // FUN_00425a20

    if (G_NavMapVertexBuffer != nullptr)
    {
        for (int Index = 0; Index < 3; ++Index)
        {
            if (G_NavMapBufferHandles[Index] != nullptr)
            {
                G_NavMapVertexBuffer->DestroyVb(&G_NavMapBufferHandles[Index]);
                G_NavMapBufferHandles[Index] = nullptr;
            }
        }
        G_NavMapVertexBuffer->Release();
        G_NavMapVertexBuffer = nullptr;
    }
    if (G_NavMapIndexBuffer != nullptr)
    {
        G_NavMapIndexBuffer->Release();
        G_NavMapIndexBuffer = nullptr;
    }
    if (G_NavMapDraw != nullptr)
    {
        G_NavMapDraw->Release();
        G_NavMapDraw = nullptr;
    }
}

// ----------------------------------------------------------------------------------------
// WinControl overrides
// ----------------------------------------------------------------------------------------

bool NeuroNetNavMap::HitTest(Vector const* Ray)
{
    // LAB_00492270.
    if (WinControl::HitTest(Ray)) // FUN_005a0340
        return true;
    if (bSidePanelsOpen == 0)
        return false;

    Transform Xform = {Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f), {0.0f, 0.0f, 0.0f}};
    if (MeshHolder.HasInstance())
        memcpy(&Xform, DALib::Engine->GetInstanceTransform(MeshHolder.EngineInstance),
               sizeof(Xform));

    // The side panels' boxes.
    Vector const HalfExtents = {0.19f, 0.28f, 0.0f};
    Transform Box;
    memcpy(&Box, &Xform, sizeof(Box));
    Box.Translation.x = Xform.Translation.x + 0.54f;
    if (Win3DRayHitsAabbHalfExtents(&HalfExtents, &Box, Ray))
        return true;

    // DAT_005d3dc0.
    memcpy(&Box, &Xform, sizeof(Box));
    Box.Translation.x = Xform.Translation.x - 0.536f;
    return Win3DRayHitsAabbHalfExtents(&HalfExtents, &Box, Ray);
}

void NeuroNetNavMap::SetPosition(Vector const* NewPosition)
{
    // LAB_004948a0.
    WinControl::SetPosition(NewPosition);
    MapLabelOrigin[0] = NewPosition->x + 0.2f;
    MapLabelOrigin[1] = NewPosition->y - 0.25f;
    MapLabelOrigin[2] = NewPosition->z;
}

bool NeuroNetNavMap::Create(Win3D* NewParent, void* Data)
{
    // LAB_0048eba0.
    bBriefing = IsMissionBriefingNavMapUp(); // FUN_0043d570
    SetMesh("Interface\\NeuroNet\\NavMap\\NewNavMap\\navmap_background.cmp", 0);

    Vector const Zero3 = {0.0f, 0.0f, 0.0f};
    Vector const Zero3b = {0.0f, 0.0f, 0.0f};

    {
        Vector const Position = {0.19f, -0.23f, -0.22f};
        RightWindow = CreateStaticControl(
            "NavMapRightWindow", "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_navmap_right.cmp",
            &Position, nullptr, &Zero3, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, &Zero3b);
    }
    {
        Vector const Position = {1.045f, -0.23f, -0.22f};
        LeftWindow = CreateStaticControl(
            "NavMapLeftWindow", "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_navmap_left.cmp",
            &Position, nullptr, &Zero3, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, &Zero3b);
    }

    // The map's own pick box. Without it WinControl::HitTest never hits the map area, Win3DPick
    // never returns the map and the click falls through to the space pick.
    HalfExtents.x = 0.35f;
    HalfExtents.y = 0.33f;
    HalfExtents.z = 0.0f;

    // The frame buttons: 0.018 half extents on the top edge (y 0.295).
    Vector const SmallHalf = {0.018f, 0.018f, 0.0f};
    {
        Vector const Position = {-0.26f, 0.295f, 0.0f};
        UniverseButton = CreateButtonControl(
            "NavMapUniverseButton", "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_universebutton.3db",
            nullptr, "ui_select_item", nullptr, &Position, nullptr, &SmallHalf, 0, 0, 0, 0, 0x219c, 0,
            0);
    }
    {
        Vector const Position = {-0.214f, 0.295f, 0.0f};
        LabelButton = CreateButtonControl(
            "NavMapLabelButton", "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_labels.3db", nullptr,
            "ui_select_item", nullptr, &Position, nullptr, &SmallHalf, 0, 0, 0, 0, 0x416, 0, 0);
    }
    {
        Vector const Position = {0.258f, 0.295f, 0.0f};
        CloseButton = CreateButtonControl(
            "NavMapCloseButton", "Interface\\NeuroNet\\Inventory\\x.3db", nullptr,
            "ui_select_item", nullptr, &Position, nullptr, &SmallHalf, 0, 0, 0, 0, 0x409, 0, 0);
    }

    // Briefing only -- no close button, the briefing text and the
    // continue button, and the 0xf event.
    if (bBriefing != 0)
    {
        CloseButton->Flags = static_cast<unsigned char>(CloseButton->Flags & ~(kWin3DActive | kWin3DVisible));
        {
            Vector const Position = {-0.76f, 0.47f, 0.0f};
            MissionBriefingText = CreateTextControl("MissionBriefingText", "MissionObjective",
                                                    &Position, nullptr, 1.48f, 0.06f, 4.0f, 0x627,
                                                    1, 0, 1);
        }
        {
            Vector const HalfExtents = {0.153f, 0.036f, 0.0f};
            Vector const Position = {-0.004f, -0.443f, 0.0f};
            ContinueButton = CreateWireButtonControl("NavMapContinueButton", "ui_select_item",
                                                     &Position, &HalfExtents, 0, 0, 0x326, 0, 0);
        }
        {
            Vector const Position = {-0.76f, -0.418f, 0.0f};
            ContinueText = CreateTextControl("NavMapContinueText", "ButtonText", &Position,
                                             nullptr, 1.51f, 0.03f, 3.0f, 0x48e, 1, 0, 1);
        }
        RegisterEvent(USER_CANCEL); // FUN_005760d0
    }

    {
        Vector const Position = {0.212f, 0.295f, 0.0f};
        ShipButton = CreateButtonControl(
            "NavMapShipButton", "Interface\\neuronet\\inventory\\gotoship.3db", nullptr,
            "ui_select_item", nullptr, &Position, nullptr, &SmallHalf, 0, 0, 0, 0, 0x4de, 0, 0);
    }
    {
        Vector const Position = {-0.274f, -0.252f, 0.0f};
        SystemNameText = CreateTextControl("NavMapSystemName", "SubHeader", &Position, nullptr,
                                           0.549f, 0.06f, 2.2f, 0, 1, 0, 1);
    }
    {
        Vector const Position = {-0.314f, -0.222f, 0.0f};
        DebugText = CreateTextControl("NavMapDebugText", "Normal", &Position, nullptr, 0.64f,
                                      0.06f, 2.2f, 0x4d2, 1, 0, 1);
    }
    DebugText->SetProperty(kWinPropColour, reinterpret_cast<uintptr_t>(&G_NnPaletteAmber), 1);
    {
        Vector const Position = {-0.314f, -0.222f, 0.0f};
        NoBestPathText = CreateTextControl("NavMapNoBestPathText", "Normal", &Position, nullptr,
                                           0.64f, 0.06f, 2.2f, 0x573, 1, 0, 1);
    }
    NoBestPathText->SetProperty(kWinPropColour, reinterpret_cast<uintptr_t>(&G_NnPaletteAmber), 1);

    // The filter toggles along the top edge.
    {
        Vector const Position = {0.092f, 0.295f, 0.0f};
        LegendFilter = CreateButtonControl(
            "NavMapLegendFilter", "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_legendtoggle.3db",
            nullptr, "ui_filter_select", nullptr, &Position, nullptr, &SmallHalf, 0, 0, 0, 0, 0x54e,
            0, 0);
    }
    {
        Vector const Position = {0.138f, 0.295f, 0.0f};
        BaseListFilter = CreateButtonControl(
            "NavMapBaseListFilter", "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_knownbases.3db",
            nullptr, "ui_filter_select", nullptr, &Position, nullptr, &SmallHalf, 0, 0, 0, 0, 0x3c7,
            0, 0);
    }
    {
        Vector const Position = {-0.092f, 0.295f, 0.0f};
        PhysicalFilter = CreateButtonControl(
            "NavMapPhysicalFilter",
            "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_filter_physical.3db", nullptr,
            "ui_filter_select", nullptr, &Position, nullptr, &SmallHalf, 0, 0, 0, 0, 0x4d3, 0, 0);
    }
    PhysicalFilter->SetProperty(kWinPropLit, 1, 0);
    {
        Vector const Position = {-0.046f, 0.295f, 0.0f};
        PoliticalFilter = CreateButtonControl(
            "NavMapPoliticalFilter",
            "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_filter_political.3db", nullptr,
            "ui_filter_select", nullptr, &Position, nullptr, &SmallHalf, 0, 0, 0, 0, 0x4d4, 0, 0);
    }
    {
        Vector const Position = {0.0f, 0.295f, 0.0f};
        PatrolFilter = CreateButtonControl(
            "NavMapPatrolFilter", "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_patrol.3db",
            nullptr, "ui_filter_select", nullptr, &Position, nullptr, &SmallHalf, 0, 0, 0, 0, 0x521,
            0, 0);
    }
    {
        Vector const Position = {0.046f, 0.295f, 0.0f};
        MineableFilter = CreateButtonControl(
            "NavMapmineableFilter",
            "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_minablezonefilter.3db", nullptr,
            "ui_filter_select", nullptr, &Position, nullptr, &SmallHalf, 0, 0, 0, 0, 0x54f, 0, 0);
    }
    {
        Vector const Position = {-0.018f, 0.295f, 0.0f};
        MissionFilter = CreateButtonControl(
            "NavMapMissionFilter",
            "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_filter_missions.3db", nullptr,
            "ui_filter_select", nullptr, &Position, nullptr, &SmallHalf, 0, 0, 0, 0, 0x522, 0, 0);
    }
    {
        Vector const Position = {0.161f, 0.246f, 0.0f};
        AllFilter = CreateButtonControl(
            "NavMapAllFilter", "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_allzonefilter.3db",
            nullptr, "ui_filter_select", nullptr, &Position, nullptr, &SmallHalf, 0, 0, 0, 0, 0x559,
            0, 0);
    }
    AllFilter->SetProperty(kWinPropHide, 1, 0);
    {
        Vector const Position = {0.207f, 0.246f, 0.0f};
        HostileFilter = CreateButtonControl(
            "NavMapHostileFilter",
            "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_hostilezonefilter.3db", nullptr,
            "ui_filter_select", nullptr, &Position, nullptr, &SmallHalf, 0, 0, 0, 0, 0x55a, 0, 0);
    }
    HostileFilter->SetProperty(kWinPropHide, 1, 0);
    {
        Vector const Position = {0.253f, 0.246f, 0.0f};
        FriendlyFilter = CreateButtonControl(
            "NavMapFriendlyFilter",
            "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_friendlyzonefilter.3db", nullptr,
            "ui_filter_select", nullptr, &Position, nullptr, &SmallHalf, 0, 0, 0, 0, 0x55b, 0, 0);
    }
    FriendlyFilter->SetProperty(kWinPropHide, 1, 0);

    // The object tools, each mesh scaled by 1.7.
    Vector const ToolHalf = {0.02f, 0.02f, 0.0f};
    Vector const ZoomPosition = {0.25f, 0.298f, 0.0f};
    Vector const WaypointPosition = {-0.68f, 0.228f, 0.0f};
    InZoomButton = CreateButtonControl(
        "NavMapInZoom", "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_zoomin.cmp", nullptr,
        "ui_select_item", nullptr, &ZoomPosition, nullptr, &ToolHalf, 0, 0, 0, 0, 0x4d5, 0, 0);
    ScaleToolButtonMesh(InZoomButton);
    OutZoomButton = CreateButtonControl(
        "NavMapOutZoom", "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_zoomout.cmp", nullptr,
        "ui_select_item", nullptr, &ZoomPosition, nullptr, &ToolHalf, 0, 0, 0, 0, 0x4d6, 0, 0);
    ScaleToolButtonMesh(OutZoomButton);
    SetWaypointButton = CreateButtonControl(
        "NavMapSetWaypointButton", "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_addwaypoint.cmp",
        nullptr, "ui_select_item", nullptr, &WaypointPosition, nullptr, &ToolHalf, 0, 0, 0, 0,
        0x4d7, 0, 0);
    ScaleToolButtonMesh(SetWaypointButton);
    BestPathButton = CreateButtonControl(
        "NavMapBestPathButton", "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_shortestpath.cmp",
        nullptr, "ui_select_item", nullptr, &WaypointPosition, nullptr, &ToolHalf, 0, 0, 0, 0,
        0x56e, 0, 0);
    ScaleToolButtonMesh(BestPathButton);
    JumpGateButton = CreateButtonControl(
        "NavMapJumpGateButton", "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_jumptosystem.cmp",
        nullptr, "ui_select_item", nullptr, &WaypointPosition, nullptr, &ToolHalf, 0, 0, 0, 0,
        0x571, 0, 0);
    ScaleToolButtonMesh(JumpGateButton);
    SelectObjectButton = CreateButtonControl(
        "NavMapSelectObjectButton",
        "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_selectorhand.cmp", nullptr, "ui_select_item",
        nullptr, &WaypointPosition, nullptr, &ToolHalf, 0, 0, 0, 0, 0x572, 0, 0);
    ScaleToolButtonMesh(SelectObjectButton);
    InfoButton = CreateButtonControl(
        "NavMapInfo", "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_info.cmp", nullptr,
        "ui_select_item", nullptr, &ZoomPosition, nullptr, &ToolHalf, 0, 0, 0, 0, 0x4d9, 0, 0);
    ScaleToolButtonMesh(InfoButton);
    ClearWaypointButton = CreateButtonControl(
        "NavMapClearWaypointButton",
        "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_subtractwaypoint.cmp", nullptr,
        "ui_select_item", nullptr, &WaypointPosition, nullptr, &ToolHalf, 0, 0, 0, 0, 0x4d8, 0, 0);
    ScaleToolButtonMesh(ClearWaypointButton);

    // The info text and the info card (both lose +0x6c bit 1).
    {
        Vector const AttachOffset = {kNavMapPanelBaseA + 0.93f, -0.06f, 0.0f};
        InfoText = CreateRichTextWin("NavMapInfoText", 0.27f, 0.33f, &AttachOffset, true);
    }
    InfoText->Flags = static_cast<unsigned char>(InfoText->Flags & ~kWin3DVisible);
    {
        Vector const AttachOffset = {0.4f, -0.02f, 0.0f};
        InfoCard = CreateRichTextWin("NavMapInfoCard", 0.43f, 0.41f, &AttachOffset, true);
    }
    InfoCard->Flags = static_cast<unsigned char>(InfoCard->Flags & ~kWin3DVisible);
    {
        Vector const Position = {kNavMapPanelBaseA + 0.36f, 0.256f, 0.0f};
        InfoTitle = CreateTextControl("NavMapInfoTitle", "Normal", &Position, nullptr, 0.32f,
                                      0.03f, 2.1f, 0, 1, 0, 1);
    }

    // The waypoint / mission buttons under the info text.
    {
        Vector const HalfExtents = {0.146f, 0.026f, 0.0f};
        Vector const Position = {kNavMapPanelBaseB - 0.516f, -0.237f, 0.0f};
        ClearAllWaypointsButton = CreateWireButtonControl(
            "NavMapClearAllWaypointsButton", "ui_select_item", &Position, &HalfExtents, 0, 0, 0x327,
            0, 0);
    }
    {
        Vector const Position = {kNavMapPanelBaseB - 0.659f, -0.209f, 0.0f};
        ClearAllWaypointsText = CreateTextControl("NavMapClearAllWaypointsText", "ButtonText",
                                                  &Position, nullptr, 0.288f, 0.03f, 1.9f,
                                                  0x219d, 1, 0, 1);
    }
    {
        Vector const HalfExtents = {0.133f, 0.026f, 0.0f};
        Vector const Position = {kNavMapPanelBaseA + 0.53f, -0.23f, 0.0f};
        AbortMissionButton = CreateWireButtonControl(
            "NavMapAbortMissionButton", "ui_select_item", &Position, &HalfExtents, 0, 0, 0x328, 0,
            0);
    }
    {
        Vector const Position = {kNavMapPanelBaseA + 0.397f, -0.206f, 0.0f};
        AbortMissionText = CreateTextControl("NavMapAbortMissionText", "ButtonText", &Position,
                                             nullptr, 0.266f, 0.03f, 1.9f, 0x4da, 1, 0, 1);
    }
    AbortMissionButton->SetProperty(kWinPropHide, 1, 0);
    AbortMissionText->SetProperty(kWinPropHide, 1, 0);
    {
        Vector const HalfExtents = {0.067f, 0.026f, 0.0f};
        Vector const Position = {kNavMapPanelBaseA + 0.447f, -0.23f, 0.0f};
        AbortYesButton = CreateWireButtonControl("NavMapAbortYesButton", "ui_select_item",
                                                 &Position, &HalfExtents, 0, 0, 0x329, 0, 0);
    }
    {
        Vector const Position = {kNavMapPanelBaseA + 0.381f, -0.206f, 0.0f};
        AbortYesText = CreateTextControl("NavMapAbortYesText", "ButtonText", &Position, nullptr,
                                         0.132f, 0.03f, 1.9f, 0x4dc, 1, 0, 1);
    }
    {
        Vector const HalfExtents = {0.067f, 0.026f, 0.0f};
        Vector const Position = {kNavMapPanelBaseA + 0.617f, -0.23f, 0.0f};
        AbortNoButton = CreateWireButtonControl("NavMapAbortNoButton", "ui_select_item",
                                                &Position, &HalfExtents, 0, 0, 0x32a, 0, 0);
    }
    {
        Vector const Position = {kNavMapPanelBaseA + 0.55f, -0.206f, 0.0f};
        AbortNoText = CreateTextControl("NavMapAbortNoText", "ButtonText", &Position, nullptr,
                                        0.132f, 0.03f, 1.9f, 0x4dd, 1, 0, 1);
    }
    AbortYesButton->SetProperty(kWinPropHide, 1, 0);
    AbortYesText->SetProperty(kWinPropHide, 1, 0);
    AbortNoButton->SetProperty(kWinPropHide, 1, 0);
    AbortNoText->SetProperty(kWinPropHide, 1, 0);

    OptionBridge = CreateStaticControl("NavMapOptionBridge",
                                       "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_selector.3db",
                                       &Zero3, nullptr, &Zero3, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0,
                                       &Zero3b);
    OptionBridge->SetProperty(kWinPropHide, 1, 0);

    // The three objective boxes, their scroll bar and texts.
    {
        Vector const HalfExtents = {0.146f, 0.068f, 0.0f};
        float const X = kNavMapPanelBaseB - 0.547f;
        Vector const Position1 = {X, 0.181f, 0.0f};
        MissionObjective[0] = CreateWireStaticControl("NavMapMissionObjective1", &Position1,
                                                      &HalfExtents, 0, 0, 0, 0);
        Vector const Position2 = {X, 0.046f, 0.0f};
        MissionObjective[1] = CreateWireStaticControl("NavMapMissionObjective2", &Position2,
                                                      &HalfExtents, 0, 0, 0, 0);
        Vector const Position3 = {X, -0.089f, 0.0f};
        MissionObjective[2] = CreateWireStaticControl("NavMapMissionObjective3", &Position3,
                                                      &HalfExtents, 0, 0, 0, 0);
    }
    {
        Vector const Position = {kNavMapPanelBaseB + 0.3261f, -0.058f, 0.0f};
        ObjectiveScroll = CreateUIScroll("NavMapMissionObjectiveScroll", &Position, 0.019f,
                                         0.285f, 0, 5, 3, 0);
    }
    {
        float const X = kNavMapPanelBaseB - 0.686f;
        Vector const Position1 = {X, NnFloatBits(0x3e808313), 0.0f}; // 0.251 (listing bits)
        ObjectiveText[0] = CreateTextControl("NavMapMissionObjectiveText1", "NewsMission",
                                             &Position1, nullptr, 0.285f, 0.03f, 1.4f, 0, 0, 0, 1);
        ObjectiveText[0]->SetProperty(0x22, 0, 5);
        SetObjectiveTextMultiLine(ObjectiveText[0]);
        Vector const Position2 = {X, 0.116f, 0.0f};
        ObjectiveText[1] = CreateTextControl("NavMapMissionObjectiveText2", "NewsMission",
                                             &Position2, nullptr, 0.285f, 0.03f, 1.4f, 0, 0, 0, 1);
        ObjectiveText[1]->SetProperty(0x22, 0, 5);
        SetObjectiveTextMultiLine(ObjectiveText[1]);
        Vector const Position3 = {X, NnFloatBits(0xbc9ba5e4), 0.0f}; // -0.019 (DAT bits)
        ObjectiveText[2] = CreateTextControl("NavMapMissionObjectiveText3", "NewsMission",
                                             &Position3, nullptr, 0.285f, 0.03f, 1.4f, 0, 0, 0, 1);
        ObjectiveText[2]->SetProperty(0x22, 0, 5);
        SetObjectiveTextMultiLine(ObjectiveText[2]);
    }

    // The four faction labels of the universe view, hidden.
    {
        Vector const Position = {-0.055f, -0.09f, 0.0f};
        FactionLabels[0] = CreateTextControl("NavMapLibertyLabel", "SubHeader", &Position,
                                             nullptr, 0.132f, 0.03f, 1.9f, 0x4e1, 0, 0, 1);
    }
    FactionLabels[0]->SetProperty(kWinPropHide, 1, 0);
    {
        Vector const Position = {-0.245f, -0.123f, 0.0f};
        FactionLabels[1] = CreateTextControl("NavMapBretoniaLabel", "SubHeader", &Position,
                                             nullptr, 0.132f, 0.03f, 1.9f, 0x4e2, 0, 0, 1);
    }
    FactionLabels[1]->SetProperty(kWinPropHide, 1, 0);
    {
        Vector const Position = {0.005f, 0.121f, 0.0f};
        FactionLabels[2] = CreateTextControl("NavMapKusariLabel", "SubHeader", &Position,
                                             nullptr, 0.132f, 0.03f, 1.9f, 0x4e3, 0, 0, 1);
    }
    FactionLabels[2]->SetProperty(kWinPropHide, 1, 0);
    {
        Vector const Position = {0.065f, -0.15f, 0.0f};
        FactionLabels[3] = CreateTextControl("NavMapRheinlandLabel", "SubHeader", &Position,
                                             nullptr, 0.132f, 0.03f, 1.9f, 0x4e4, 0, 0, 1);
    }
    FactionLabels[3]->SetProperty(kWinPropHide, 1, 0);

    {
        Vector const Position = {-0.274f, 0.266f, 0.0f};
        FilterTitle = CreateTextControl("NavMapFilterTitle", "Normal", &Position, nullptr, 0.547f,
                                        0.03f, 1.5f, 0, 1, 0, 1);
    }

    // The grid letters A..H (ids 0x506 + i), x = i * 0.07 - 0.279.
    char Name[0x40];
    for (int Index = 0; Index < 8; ++Index)
    {
        sprintf(Name, "NavMapGridLetter%c", 'A' + Index);
        Vector const Position = {
            static_cast<float>(static_cast<double>(Index) * 0.07f - 0.279f), -0.294f, 0.0f};
        GridLetters[Index] =
            CreateTextControl(Name, "Normal", &Position, nullptr, 0.08f, 0.03f, 1.9f,
                              static_cast<unsigned>(Index + 0x506), 1, 0, 1);
    }
    // The grid numbers 1..8 (ids 0x515 + i), y = 0.252 - i * 0.07.
    for (int Index = 0; Index < 8; ++Index)
    {
        sprintf(Name, "NavMapGridNumber%d", Index + 1);
        Vector const Position = {
            -0.309f, static_cast<float>(0.252f - static_cast<double>(Index) * 0.07f), 0.0f};
        GridNumbers[Index] =
            CreateTextControl(Name, "Normal", &Position, nullptr, 0.032f, 0.03f, 1.9f,
                              static_cast<unsigned>(Index + 0x515), 1, 0, 1);
    }

    // The legend labels, hidden, in the idle palette colour (one stack
    // copy of DAT_00679bb0 handed to all five).
    unsigned long LegendColor = G_NnPaletteCyan;
    static float const kLegendY[5] = {0.194f, 0.09f, 0.14f, -0.047f, -0.198f};
    static char const* const kLegendNames[5] = {"NavMapHostileLabel", "NavMapFriendlyLabel",
                                                "NavMapNeutralLabel", "NavMapPlayerShipLabel",
                                                "NavMapMissionWaypointsLabel"};
    static unsigned const kLegendIds[5] = {0x553, 0x555, 0x556, 0x557, 0x3af};
    for (int Index = 0; Index < 5; ++Index)
    {
        Vector const Position = {-0.159f, kLegendY[Index], 0.0f};
        LegendLabels[Index] = CreateTextControl(kLegendNames[Index], "Normal", &Position, nullptr,
                                                0.53f, 0.03f, 2.2f, kLegendIds[Index], 0, 0, 1);
        LegendLabels[Index]->SetProperty(kWinPropHide, 1, 0);
        LegendLabels[Index]->SetProperty(kWinPropColour, reinterpret_cast<uintptr_t>(&LegendColor), 1);
    }

    // The info card's close button and caption, hidden.
    {
        Vector const HalfExtents = {0.133f, 0.026f, 0.0f};
        Vector const Position = {0.0f, -0.254f, 0.0f};
        CloseInfocardButton = CreateWireButtonControl("NavMapCloseInfocardButton",
                                                      "ui_select_item", &Position, &HalfExtents, 0,
                                                      0, 0x32b, 0, 0);
    }
    {
        Vector const Position = {-0.133f, -0.229f, 0.0f};
        CloseInfocardText = CreateTextControl("NavMapCloseInfocardText", "ButtonText", &Position,
                                              nullptr, 0.266f, 0.03f, 1.9f, 0x3a1, 1, 0, 1);
    }
    CloseInfocardButton->Flags = static_cast<unsigned char>(CloseInfocardButton->Flags & ~(kWin3DActive | kWin3DVisible));
    CloseInfocardText->Flags = static_cast<unsigned char>(CloseInfocardText->Flags & ~(kWin3DActive | kWin3DVisible));

    // The fifteen known-base rows, y = 0.171 - i * 0.03.
    for (int Index = 0; Index < 15; ++Index)
    {
        sprintf(Name, "NavMapBaseText%d", Index);
        float const Y = static_cast<float>(0.171f - static_cast<double>(Index) * 0.03f);
        Vector const BasePosition = {-0.259f, Y, 0.0f};
        BaseTexts[Index] = CreateTextControl(Name, "ItemName", &BasePosition, nullptr, 0.39f,
                                             0.03f, 1.9f, 0, 0, 0, 1);
        sprintf(Name, "NavMapBaseSystemText%d", Index);
        Vector const SystemPosition = {0.019f, Y, 0.0f};
        BaseSystemTexts[Index] = CreateTextControl(Name, "ItemName", &SystemPosition, nullptr,
                                                   0.21f, 0.03f, 1.9f, 0, 0, 0, 1);
    }
    {
        Vector const Position = {0.829f, -0.058f, 0.0f};
        BaseListScroll = CreateUIScroll("NavMapBaseListScroll", &Position, 0.019f, 0.406f, 0, 5,
                                        3, 0);
    }
    {
        Vector const Position = {-0.261f, 0.219f, 0.0f};
        BaseTitleText = CreateTextControl("NavMapBaseTitleText", "SubHeader", &Position, nullptr,
                                          0.279f, 0.03f, 1.9f, 0x3cc, 0, 0, 1);
    }
    {
        Vector const Position = {0.016f, 0.219f, 0.0f};
        SystemTitleText = CreateTextControl("NavMapSystemTitleText", "SubHeader", &Position,
                                            nullptr, 0.219f, 0.03f, 1.9f, 0x3cd, 0, 0, 1);
    }

    // "Clear all waypoints" is hidden with no waypoints or in best-path mode.
    unsigned char bHideClearAll = GetPlayerWaypointCountRetail() == 0 ? 1 : 0;
    if (bBestPathObjectives != 0)
        bHideClearAll = 1;
    ClearAllWaypointsButton->SetProperty(kWinPropHide, bHideClearAll, 0);
    ClearAllWaypointsText->SetProperty(kWinPropHide, bHideClearAll, 0);

    RefreshObjectives();
    BuildJumpLinks();
    HideObjectTools();
    RefreshMissionInfo();
    RefreshObjectives();

    // A best path forces the mission filter; the info-card modes do not
    // survive a reopen.
    if (BestPathGetWaypointCount() > 0)
        G_NavMapFilterMode = kNavMapFilterMission;
    else if (G_NavMapFilterMode == kNavMapFilterInfoCard || G_NavMapFilterMode == kNavMapFilterBaseList)
        G_NavMapFilterMode = kNavMapFilterPhysical;

    LabelButton->SetProperty(kWinPropLit, G_bNavMapShowLabels, 0);
    SetFilterMode(G_NavMapFilterMode);
    AllFilter->SetProperty(kWinPropLit, G_NavMapZoneFilter == kNavMapZoneFilterAll ? 1 : 0, 0);
    HostileFilter->SetProperty(kWinPropLit, G_NavMapZoneFilter == kNavMapZoneFilterHostile ? 1 : 0, 0);
    FriendlyFilter->SetProperty(kWinPropLit, G_NavMapZoneFilter == kNavMapZoneFilterFriendly ? 1 : 0, 0);

    // Hidden until shown (0xef01), the frame offset, the position again
    // (through slot 5), then FUN_004b5060.
    Flags = static_cast<unsigned char>(Flags & ~(kWin3DActive | kWin3DVisible));
    SetOffset(0.624f, -0.23f, -0.22f);
    SetPosition(&Position);
    return Win3D::Create(NewParent, Data);
}

bool NeuroNetNavMap::ProcessMessage(unsigned Msg, unsigned A, unsigned B)
{
    // LAB_004945e0.
    (void)A;
    (void)B;
    if (Msg == kWinMsgShow)
    {
        if (GetParent() != nullptr)
            SetPosition(&GetParent()->Position);
        InitialShow();
        TransitionTime = TransitionDuration;
        Flags = static_cast<unsigned char>(Flags | kWin3DActive | kWin3DVisible);
        bDebugTeleportMode = 0;

        // The system view's filter row.
        if (ViewMode != kNavMapViewUniverse)
        {
            UniverseButton->SetProperty(kWinPropHide, 0, 0);
            LabelButton->SetProperty(kWinPropHide, 0, 0);
            PhysicalFilter->SetProperty(kWinPropHide, 0, 0);
            PoliticalFilter->SetProperty(kWinPropHide, 0, 0);
            PatrolFilter->SetProperty(kWinPropHide, 0, 0);
            FilterTitle->SetProperty(kWinPropHide, 0, 0);
            LegendFilter->SetProperty(kWinPropHide, 0, 0);
            BaseListFilter->SetProperty(kWinPropHide, 0, 0);
            MineableFilter->SetProperty(kWinPropHide, 0, 0);

            unsigned char const bNoZones = MissionZoneIds.empty() ? 1 : 0;
            MissionFilter->SetProperty(kWinPropHide, bNoZones, 0);
            if (bNoZones != 0 && G_NavMapFilterMode == kNavMapFilterMission)
                SetFilterMode(kNavMapFilterPhysical);

            if (G_NavMapFilterMode == kNavMapFilterPolitical || G_NavMapFilterMode == kNavMapFilterPatrol)
            {
                AllFilter->SetProperty(kWinPropHide, 0, 0);
                HostileFilter->SetProperty(kWinPropHide, 0, 0);
                FriendlyFilter->SetProperty(kWinPropHide, 0, 0);
            }
            else
            {
                AllFilter->SetProperty(kWinPropHide, 1, 0);
                HostileFilter->SetProperty(kWinPropHide, 1, 0);
                FriendlyFilter->SetProperty(kWinPropHide, 1, 0);
            }

            for (int Index = 0; Index < 4; ++Index)
                FactionLabels[Index]->SetProperty(kWinPropHide, 1, 0);
        }

        DebugText->SetProperty(kWinPropHide, bDebugTeleportMode == 0 ? 1 : 0, 0);
        NoBestPathText->SetProperty(kWinPropHide, 1, 0);
        return false;
    }

    if (Msg == kWinMsgHide)
    {
        Flags = static_cast<unsigned char>(Flags & ~(kWin3DActive | kWin3DVisible));
        Win3DReparent(this, nullptr);
        Win3D* Child = GetChildHead();
        while (Child != nullptr)
        {
            Win3D* const Next = Child->GetNextSibling();
            Win3DDeleteTree(Child);
            Child = Next;
        }
        Destroy();
        delete this;
        return true;
    }

    return false;
}

bool NeuroNetNavMap::OnEvent(unsigned MsgId)
{
    // LAB_0049e190.
    if (MsgId == USER_CANCEL && bBriefing != 0)
    {
        RequestMissionBriefingContinue(); // FUN_0043d580
        ProcessMessage(kWinMsgHide, 0, 0);
        return true;
    }
    return false;
}

// ----------------------------------------------------------------------------------------
// Resources and the first show
// ----------------------------------------------------------------------------------------

void NeuroNetNavMap::LoadMapResources()
{
    // FUN_00490cd0.
    if (bResourcesLoaded != 0)
        return;
    bResourcesLoaded = 1;

    ZoomedMapMesh.LoadFromPath("Interface\\NeuroNet\\NavMap\\NewNavMap\\zoomedmap_liberty.3db", 0,
                               true);
    ZoomedMapMesh2.LoadFromPath("Interface\\NeuroNet\\NavMap\\NewNavMap\\zoomedmap_liberty.3db", 0,
                                true);
    PrettyMapMesh.LoadFromPath("Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_prettymap.3db", 0, true);

    IEngine* const Engine = DALib::Engine;
    float Transform[12];

    // The first zoomed map at MeshPosition, its rotation scaled by 1.5.
    Engine->SetInstancePosition(ZoomedMapMesh.EngineInstance, (const Vector*)MeshPosition);
    memcpy(Transform, Engine->GetInstanceTransform(ZoomedMapMesh.EngineInstance),
           sizeof(Transform));
    for (int Index = 0; Index < 9; ++Index)
        Transform[Index] = Transform[Index] * 1.5f; // DAT_005ca220
    Engine->SetInstanceMatrix(ZoomedMapMesh.EngineInstance, (const ::Transform*)Transform);
    Engine->UpdateInstance(ZoomedMapMesh.EngineInstance, 0, nullptr);

    // The second one only positioned.
    Engine->SetInstancePosition(ZoomedMapMesh2.EngineInstance, (const Vector*)MeshPosition);
    Engine->UpdateInstance(ZoomedMapMesh2.EngineInstance, 0, nullptr);

    // The pretty map, [0] * 1.175 and [4] * 1.5 (double constants
    // DAT_005d3c50 / DAT_005d3c48).
    Engine->SetInstancePosition(PrettyMapMesh.EngineInstance, (const Vector*)MeshPosition);
    memcpy(Transform, Engine->GetInstanceTransform(PrettyMapMesh.EngineInstance),
           sizeof(Transform));
    Transform[0] = static_cast<float>(static_cast<double>(Transform[0]) * 1.175);
    Transform[4] = static_cast<float>(static_cast<double>(Transform[4]) * 1.5);
    Engine->SetInstanceMatrix(PrettyMapMesh.EngineInstance, (const ::Transform*)Transform);
    Engine->UpdateInstance(PrettyMapMesh.EngineInstance, 0, nullptr);

    // The shapes (the inline ShapeDB.h:0xf2 find, "cmn\shapedb.h").
    Shapes.Load("Interface\\NeuroNet\\NavMap\\NewNavMap\\NavMapTextures.ini", DALib::Data);
    for (int Index = 0; Index < 9; ++Index)
        TerrainShapes[Index] = Shapes.Find(kNavMapTerrainShapeNames[Index], kShapeDbHeaderLower);
    WaypathShape = Shapes.Find("nav_waypathtexture", kShapeDbHeaderLower);
    char Name[0x20];
    for (int Index = 1; Index < 0x11; ++Index)
    {
        sprintf(Name, "TLRanimated_%.2d", Index);
        TradeLaneShapes[Index - 1] = Shapes.Find(Name, kShapeDbHeaderLower);
    }
}

void NeuroNetNavMap::InitialShow()
{
    // FUN_00491100.
    if (bInitialShowDone != 0)
        return;
    bInitialShowDone = 1;

    LoadMapResources();
    PanOffset[0] = 0.0f;
    PanOffset[1] = 0.0f;
    PanOffset[2] = 0.0f;

    if (ViewMode != kNavMapViewUniverse)
    {
        // The requested system (consumed), else the player's.
        if (G_NavMapRequestedSystem != 0)
        {
            CurrentSystem = Universe::get_system(G_NavMapRequestedSystem);
            G_NavMapRequestedSystem = 0;
        }
        else
        {
            CurrentSystem = Universe::get_system(GetPlayerStarSystemIdRetail());
        }
        bDrawnOnce = 0;
        bSystemViewPending = 1;

        // The system name, unless there is no system or it is one of the
        // four hidden ones (then ids 0x62a).
        bool bNameHidden = CurrentSystem == nullptr;
        if (!bNameHidden)
        {
            unsigned const Id = CurrentSystem->GetId();
            for (int Index = 0; Index < 4; ++Index)
            {
                if (SkipSystemIds[Index] == Id)
                {
                    bNameHidden = true;
                    break;
                }
            }
        }
        if (bNameHidden)
        {
            SystemNameText->SetProperty(kTextPropSetIds, 0x62a, 0);
        }
        else
        {
            GetIdsStringWWithRawFallback(static_cast<unsigned>(CurrentSystem->GetStridName()),
                                         G_NnTextScratchB, G_NnTextScratchCapacity);
            CharUpperW(G_NnTextScratchB); // FUN_005b74d9
            SystemNameText->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
        }
    }
    else
    {
        BuildUniverseItems();
    }

    // The map's four corners.
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        MapCorner0[Axis] = MapOrigin[Axis];
        MapCorner1[Axis] = MapOrigin[Axis];
    }
    MapCorner1[0] = MapCorner1[0] + MapSize[0];
    MapCorner2[0] = MapSize[0] + MapOrigin[0];
    MapCorner2[1] = MapOrigin[1] + MapSize[1];
    MapCorner2[2] = MapOrigin[2] + MapSize[2];
    for (int Axis = 0; Axis < 3; ++Axis)
        MapCorner3[Axis] = MapOrigin[Axis];
    MapCorner3[1] = MapSize[1] + MapCorner3[1];

    // Everything on the filter row hidden.
    UniverseButton->SetProperty(kWinPropHide, 1, 0);
    LabelButton->SetProperty(kWinPropHide, 1, 0);
    PhysicalFilter->SetProperty(kWinPropHide, 1, 0);
    PoliticalFilter->SetProperty(kWinPropHide, 1, 0);
    PatrolFilter->SetProperty(kWinPropHide, 1, 0);
    MissionFilter->SetProperty(kWinPropHide, 1, 0);
    MineableFilter->SetProperty(kWinPropHide, 1, 0);
    FilterTitle->SetProperty(kWinPropHide, 1, 0);
    LegendFilter->SetProperty(kWinPropHide, 1, 0);
    BaseListFilter->SetProperty(kWinPropHide, 1, 0);
    AllFilter->SetProperty(kWinPropHide, 1, 0);
    HostileFilter->SetProperty(kWinPropHide, 1, 0);
    FriendlyFilter->SetProperty(kWinPropHide, 1, 0);

    // Tail call through slot 11 (virtual; ButtonMover::Update shares the
    // name, so the Win3D face is named explicitly).
    static_cast<Win3D*>(this)->Update();
}

// ----------------------------------------------------------------------------------------
// Items
// ----------------------------------------------------------------------------------------

void NeuroNetNavMap::BuildUniverseItems()
{
    // FUN_004915b0.
    SystemScale = 1.0f;
    MapExtentX = 112000.0f;
    MapExtentZ = 112000.0f;

    for (Universe::ISystem* System = Universe::GetFirstSystem(); System != nullptr;
         System = Universe::GetNextSystem())
    {
        // Kept as a float.
        int const Random = rand() * 0x7d0 / 0x8000 + 0xbb8;
        float const RandomRadius = static_cast<float>(Random);

        // The [system] pos scaled to the map (1/15 * 200000 - 100000; z
        // is rounded to float after the first product and shifted by 5000).
        Vector const* const Pos = System->GetPos();
        float const PosX = Pos->x;
        float const PosY = Pos->y;
        float const ScaleK = NnFloatBits(0x3d888889); // DAT_005d3d30, 1/15
        float const ScaledY = PosY * ScaleK;
        float const X =
            static_cast<float>(static_cast<double>(PosX) * ScaleK * 200000.0f - 100000.0f);
        float const Z = static_cast<float>(
            static_cast<double>(ScaledY) * 200000.0f - 100000.0f + 5000.0f);

        // Id (slot 0), visit (slot 18), ids_info (slot 4), strid (slot 3),
        // in retail's call order.
        unsigned const Id = System->GetId();
        unsigned char const Visit = System->GetVisit();
        int const IdsInfo = System->GetIdsInfo();
        int const StridName = System->GetStridName();
        FNavMapItem* const Item = AddItem(kNavMapItemSolar, OBJ_SUN, X, 0.0f, Z, RandomRadius, "NAV_star", StridName,
                                          IdsInfo, 0, Id, Visit);

        // The item is used without a null test, as in retail.
        Item->System = System;
        Item->Variant = rand() * 7 / 0x8000;
        int const Twinkle = rand() * 0x384 / 0x8000 + 0x64;
        Item->PulsePhase = static_cast<float>(static_cast<double>(Twinkle) * 0.001f);
        int const Pulse = rand() * 0x64 / 0x8000 + 1;
        Item->RestSize = RandomRadius;
        Item->PulseRate = static_cast<float>(static_cast<double>(Pulse) * 0.01f);

        // The visit map's flags for the system join the item's; bit 0
        // shows it (or +0x87e forces every one).
        if (Item->Id == 0)
            continue;

        unsigned char VisitFlags = 0;
        GetPlayerVisitFlagsRetail(Item->Id, &VisitFlags, 0, 0); // FUN_004c4da0
        Item->Visit = static_cast<unsigned char>(Item->Visit | VisitFlags);
        Item->bShown = static_cast<unsigned char>(Item->Visit & kVisitShown);
        if (bDebugShowAll != 0)
            Item->bShown = 1;
    }

    BuildJumpLinks();
}

FNavMapItem* NeuroNetNavMap::AddItem(int Kind, int Type, float X, float Y, float Z, float Arg6,
                                     char const* MeshName, int StridName, int IdsInfo,
                                     float Heading, unsigned Id, unsigned char Visit)
{
    // FUN_00491f90.
    FNavMapItem* const Item = new FNavMapItem();

    Item->Position.x = X;
    Item->Position.y = Y;
    Item->Position.z = Z;
    Item->Type = Type;
    Item->Size = Arg6;
    Item->Kind = Kind;
    Item->StridName = StridName;
    Item->Heading = Heading;
    Item->IdsInfo = IdsInfo;
    Item->Id = Id;
    Item->bShown = 1;
    Item->System = nullptr;
    Item->Variant = 0;
    Item->Ordinal = 0;
    Item->RestSize = Arg6;
    Item->Visit = Visit;

    char Path[0x104];
    strcpy(Path, kNavMapSpaceObjectsDir);
    strcat(Path, MeshName);
    strcat(Path, ".3db");

    char FullPath[0x104];
    GetFileSysPath(FullPath, Path, DALib::Data);
    if (_access(FullPath, 0) == -1)
    {
        delete Item;
        return nullptr;
    }

    // The material path goes to FUN_00420f30 (holder +0x0c) on a FindArchetype miss.
    Item->Mesh.LoadFromPath(Path, kNavMapSpaceObjectsMat, true);

    Items.push_back(Item);
    return Item;
}

// ----------------------------------------------------------------------------------------
// Pending focus (free functions)
// ----------------------------------------------------------------------------------------

void NavMapPostPendingOpen()
{
    // FUN_004a2a80.
    if (G_NavMapRequestedSystem != 0)
        HudPostCommand(USER_NAV_MAP, 0); // FUN_005763b0(0x63, 0)
}

void NavMapRequestFocus(unsigned Id, unsigned char bZone)
{
    // FUN_004a2aa0.
    if (bZone != 0)
    {
        // Every system's zone list (slot 6). Retail copies each 0xc0c record to the stack and
        // reads only the zone pointer from the copy.
        for (Universe::ISystem* System = Universe::GetFirstSystem(); System != nullptr;
             System = Universe::GetNextSystem())
        {
            std::list<Universe::FSystemZone> const* const Zones = System->GetZones();
            for (std::list<Universe::FSystemZone>::const_iterator It = Zones->begin();
                 It != Zones->end(); ++It)
            {
                Universe::IZone const* const Zone = It->Zone;
                if (Zone->GetId() != Id)
                    continue;
                Vector const& Pos = Zone->GetPos();
                float const X = Pos.x;
                float const Y = Pos.y;
                float const Z = Pos.z;
                unsigned const SystemId = System->GetId();
                G_NavMapRequestedPos[0] = X;
                G_NavMapRequestedPos[1] = Y;
                G_NavMapRequestedPos[2] = Z;
                G_bNavMapRequestedPos = 1;
                G_NavMapRequestedSystem = SystemId;
                return;
            }
        }
        return;
    }

    // A system with that id; else one of its [Object]s by nickname hash.
    for (Universe::ISystem* System = Universe::GetFirstSystem(); System != nullptr;
         System = Universe::GetNextSystem())
    {
        if (System->GetId() == Id)
        {
            // Slot 0 called again for the stored id; no position.
            G_NavMapRequestedSystem = System->GetId();
            return;
        }

        // The record's four TStrings emptied (length 0, first char 0).
        Universe::FSystemObject Object;
        Object.Nickname.Length = 0;
        Object.Nickname.Data[0] = '\0';
        Object.Reputation.Length = 0;
        Object.Reputation.Data[0] = '\0';
        Object.NextRing.Length = 0;
        Object.NextRing.Data[0] = '\0';
        Object.PrevRing.Length = 0;
        Object.PrevRing.Data[0] = '\0';
        unsigned const Count = static_cast<unsigned>(System->GetObjectCount());
        for (unsigned Index = 0; Index < Count; ++Index)
        {
            if (!System->GetSystemObject(Index, &Object))
                continue;
            if (CreateID(Object.Nickname.Data) != Id)
                continue;
            float const X = Object.Pos.x;
            float const Y = Object.Pos.y;
            float const Z = Object.Pos.z;
            unsigned const SystemId = System->GetId();
            G_NavMapRequestedPos[0] = X;
            G_NavMapRequestedPos[1] = Y;
            G_NavMapRequestedPos[2] = Z;
            G_bNavMapRequestedPos = 1;
            G_NavMapRequestedSystem = SystemId;
            return;
        }
    }
}
