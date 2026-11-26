// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// NeuroNetNavMap -- the per-frame work: slot 11 Update and the helpers only it reaches. See
// nn_navmap.h. Retail Client\Interface\NeuroNet\NavMap.cpp.

#include "NeuroNet/NavMap/nn_navmap.h"
#include "NeuroNet/Economy/best_path.h"              // BestPath* (FUN_004c4fb0 / 4ff0 / 5530 / 52d0 / 58e0)
#include "Interface/cargo_bar_control.h"      // ToolTipHoverInit / SetCaption
#include "Interface/Pages/character_select_equip.h" // GetPlayerStarSystemIdRetail / BaseId / LastPosition
#include "Common/Graphics/client_mesh_load.h"       // ClientMeshLoadFromPath (FUN_0059ec80)
#include "Core/globals.h"                // GetClientFrameTimeCapped (FUN_0042d680)
#include "Space/HUD/hud_element_list.h"       // HudVectorAngle (FUN_0053eb10)
#include "Common/Fonts/ids_strings.h"            // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "Common/input_manager.h"          // GetClientSoftwareCursor, GetClientMouseButtons
#include "Network/login_mgr.h"              // G_Server (DAT_0067ecd0)
#include "Interface/menu_ui_init.h"           // PlayOrRewindUiSoundSlot (FUN_005645c0)
#include "Space/mission_created_solars.h" // FindMissionCreatedSolar (FUN_00476fe0)
#include "NeuroNet/nn_inventory.h"           // IsPlayerDockedRetail (FUN_004c4810)
#include "NeuroNet/nn_text_scratch.h"        // G_NnTextScratchB (DAT_0066fc60), capacity DAT_006119f8
#include "NeuroNet/nn_tooltip.h"             // FUN_004be2d0 / FUN_004be2f0 / FUN_004be370
#include "NeuroNet/nn_ui_pulse.h"            // G_NnPaletteCyan / _679b80
#include "Space/player_spawn.h"           // waypoints (FUN_004c4690 / 46a0), GetPlayerConnectionId
#include "Space/Cameras/space_camera.h"           // FSpaceCamera, AsCamera, FUN_0040f8c0 / FUN_0040fb80
#include "Interface/tooltip_hover.h"
#include "Interface/win3d.h"                  // Win3DGetMouseRay / Win3DGetCamera / FUN_00423b90
#include "x86math.h"                // Transform, Vector, Matrix, I3DMathEngine
#include "common_csys.h"            // Acquire3DMathEngine (FUN_005b73e0)
#include "common_misc.h"            // Geometry::Sphere
#include "common_reputation.h"      // Reputation::Vibe::GetAffiliation / GetName
#include "common_server.h"          // IServer
#include "common_text.h"            // FmtStr
#include "common_universe.h"
#include "DALib.h"                  // DALib::Engine / Data
#include "engine/engbase.h"         // IEngine

#include <io.h>                     // _access
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>                // CharUpperW (FUN_005b74d9)

namespace
{
    // DAT_005c7f68 -- 1/255 as the float 0x3b808081.
    float const kNavMapInv255 = 0.003921568859368563f;
    // DAT_005d1494 -- 0.001f.
    float const kNavMapMilli = 0.001f;
    // DAT_005d3f50 (double) -- the pulse's swing of +0x3c.
    double const kNavMapPulseSwing = 1000.0;
    // DAT_005d3f08 / DAT_005d3f0c.
    float const kNavMapPulseAlphaScale = 64.0f;
    float const kNavMapPulseBase = 191.0f;

    float NavMapByteToUnit(unsigned char Value)
    {
        // Times DAT_005c7f68 (1/255), rounded to float once.
        return static_cast<float>(static_cast<double>(Value) * kNavMapInv255);
    }

    // DAT_005d3d34 / DAT_005d3d28 / DAT_005d3d24 -- the universe view's system-to-map scale.
    float const kNavMapUniverseScale = 13333.3330078125f; // 0x46505555
    float const kNavMapUniverseOffset = 100000.0f;
    float const kNavMapUniverseLift = 5000.0f;
    // DAT_005d3d38 / DAT_005ca23c.
    float const kNavMapRadToDeg = 57.2957763671875f; // 0x42652ee0 (not the nearest float)
    float const kNavMapFullTurn = 360.0f;

    // DAT_005d3dc8 (double) -- the map plane's depth in front of the camera: a picking ray is
    // scaled to reach it.
    double const kNavMapPlaneDepth = -0.55;
    // DAT_005d3e6c -- the pick circles' margin.
    float const kNavMapPickMargin = 1.0499999523162842f; // 0x3f866666
    // DAT_005ca220 -- a universe-view system's (Type bit 2) pick circle.
    float const kNavMapSystemPickScale = 1.5f;
    // DAT_005d3f10 / DAT_005d1494 -- FUN_0049e890's cursor offsets.
    float const kNavMapRayOffsetX = 0.0024999999441206455f; // 0x3b23d70a
    float const kNavMapRayOffsetY = 0.0010000000474974513f; // 0x3a83126f
    // DAT_005d3e44 -- the object tools close once the cursor moved this far (squared).
    float const kNavMapToolsCloseDist2 = 0.004999999888241291f; // 0x3ba3d70a
    // DAT_005d011c -- a hover tip's y offset below the cursor.
    float const kNavMapHoverDrop = 0.029999999329447746f; // 0x3cf5c28f
    // DAT_005d3e68 / DAT_005d3e64 -- the edge a Kind 2 item (the player) is clamped to.
    float const kNavMapEdge = 112000.0f;
    // DAT_005c77f4 -- degrees to radians (float).
    float const kNavMapDegToRad = 0.01745329238474369f; // 0x3c8efa35
    // DAT_005c9800 / DAT_005d3e4c -- PlaceItems' mesh scale terms.
    float const kNavMapSystemMeshScale = 1.2000000476837158f; // 0x3f99999a
    float const kNavMapMeshScale = 60.0f;
    // FUN_00496600: the item's depth, an immediate 0xbf0ccccc -- one ulp short of
    // the nearest float to -0.55 (0xbf0ccccd), so it is spelled out.
    float const kNavMapItemDepth = -0.54999995231628418f; // 0xbf0ccccc
    // DAT_005c8a5c -- pi as a float.
    float const kNavMapPi = 3.1415927410125732f; // 0x40490fdb
    // DAT_005d2edc -- a view switch's duration.
    float const kNavMapSwitchSeconds = 0.4000000059604645f; // 0x3ecccccd
    // DAT_005d3e40 -- the group-position poll period.
    float const kNavMapGroupPollSeconds = 30.0f;
    // 0x41000000 -- NoBestPathText's time on screen.
    float const kNavMapNoBestPathSeconds = 8.0f;

    char const kNavMapSpaceObjectsDir[] = "Interface\\NeuroNet\\NavMap\\NewNavMap\\SpaceObjects\\";
    char const kNavMapSpaceObjectsMat[] =
        "Interface\\NeuroNet\\NavMap\\NewNavMap\\SpaceObjects\\spaceobjects.mat";

    // The universe / system / waypoint markers' tint (FUN_00496e50):
    // the palette colour's four bytes scaled by DAT_005c7f68 into the holder's user floats.
    void NavMapTintFromPalette(FNavMapItem* Item, unsigned long Rgba)
    {
        unsigned char const* const Bytes = reinterpret_cast<unsigned char const*>(&Rgba);
        Item->Mesh.UserFloats[0] = NavMapByteToUnit(Bytes[0]);
        Item->Mesh.UserFloats[1] = NavMapByteToUnit(Bytes[1]);
        Item->Mesh.UserFloats[2] = NavMapByteToUnit(Bytes[2]);
        Item->Mesh.UserFloats[4] = NavMapByteToUnit(Bytes[3]);
    }

    // FUN_00496600: a Kind 2 item's mesh swapped for Name (the holder
    // released through FUN_0059eee0, then "...\SpaceObjects\" Name ".3db" loaded when the file
    // exists).
    void NavMapReloadItemMesh(FNavMapItem* Item, char const* Name)
    {
        ClientUiMeshHolderRelease(&Item->Mesh); // FUN_0059eee0
        char Path[0x104];
        strcpy(Path, kNavMapSpaceObjectsDir);
        strcat(Path, Name);
        strcat(Path, ".3db");
        char FullPath[0x104];
        GetFileSysPath(FullPath, Path, DALib::Data);
        if (_access(FullPath, 0) == -1)
            return;
        ClientMeshLoadFromPath(&Item->Mesh, Path, kNavMapSpaceObjectsMat, true);
    }

    // The hover tip Update feeds (inlined four times there): the cursor's
    // ray point 0.03 lower, no extents, live; the ToolTip's current tip (FUN_004be2f0, a
    // scratch record built with FUN_004144b0 / FUN_004963a0 and torn down by FUN_00444ee0)
    // keeps its position when it shows the same ids; then FUN_004be370.
    void NavMapPlaceHoverAtCursor(FToolTipHoverInfo* Record)
    {
        Vector Ray;
        Win3DGetMouseRay(&Ray); // FUN_0059da70
        Record->WorldPos[0] = Ray.x;
        Record->WorldPos[1] = static_cast<float>(static_cast<double>(Ray.y) -
                                                 static_cast<double>(kNavMapHoverDrop));
        Record->WorldPos[2] = Ray.z;
        Record->HalfExtents[0] = 0.0f;
        Record->HalfExtents[1] = 0.0f;
        Record->HalfExtents[2] = 0.0f;
        Record->bLive = 1;
    }

    void NavMapSubmitHover(FToolTipHoverInfo* Record)
    {
        FToolTipHoverInfo Current;          // caption null + FUN_004144b0
        ToolTipHoverReset(&Current);        // FUN_004963a0
        if (NnToolTipReadHover(&Current) && Current.IdsAlt == Record->IdsAlt)
        {
            Record->WorldPos[0] = Current.WorldPos[0];
            Record->WorldPos[1] = Current.WorldPos[1];
            Record->WorldPos[2] = Current.WorldPos[2];
        }
        NnToolTipFeedHover(Record); // FUN_004be370
        // FUN_00444ee0 on Current: ~FToolTipHoverInfo.
    }

    // The system name the nav map shows (InitialShow, Update twice): FUN_004347e0 into DAT_0066fc60, CharUpperW, property 0x1d; the hidden
    // systems (and none) show ids 0x62a.
    void NavMapShowSystemName(WinControl* Text, Universe::ISystem const* System,
                              unsigned const* SkipSystemIds)
    {
        bool bHidden = System == nullptr;
        if (!bHidden)
        {
            unsigned const Id = System->GetId();
            for (int Index = 0; Index < 4; ++Index)
            {
                if (SkipSystemIds[Index] == Id)
                {
                    bHidden = true;
                    break;
                }
            }
        }
        if (bHidden)
        {
            Text->SetProperty(kTextPropSetIds, 0x62a, 0);
            return;
        }
        GetIdsStringWWithRawFallback(static_cast<unsigned>(System->GetStridName()), G_NnTextScratchB,
                                     G_NnTextScratchCapacity);
        CharUpperW(G_NnTextScratchB); // FUN_005b74d9
        Text->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
    }
}

// ----------------------------------------------------------------------------------------
// Positions
// ----------------------------------------------------------------------------------------

float* NavMapBasePosition(float* Out, unsigned BaseId, unsigned SystemId)
{
    // FUN_0048d780 (cdecl) -- FUN_00491e10 without the cache.
    Universe::ISystem const* const System = Universe::get_system(SystemId);
    if (System != nullptr)
    {
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
            if (Object.DockWith != BaseId)
                continue;
            Out[0] = Object.Pos.x;
            Out[1] = Object.Pos.y;
            Out[2] = Object.Pos.z;
            return Out;
        }

        // A base a mission created.
        float Pos[3];
        int IdsInfo;
        if (FindMissionCreatedSolar(BaseId, Pos, &IdsInfo))
        {
            Out[0] = Pos[0];
            Out[1] = Pos[1];
            Out[2] = Pos[2];
            return Out;
        }
    }
    Out[0] = 0.0f;
    Out[1] = 0.0f;
    Out[2] = 0.0f;
    return Out;
}

float* NeuroNetNavMap::CachedBasePosition(float* Out, unsigned BaseId,
                                          Universe::ISystem const* System)
{
    // FUN_00491e10.
    if (CachedObjectId == BaseId)
    {
        Out[0] = CachedObjectPosition[0];
        Out[1] = CachedObjectPosition[1];
        Out[2] = CachedObjectPosition[2];
        return Out;
    }

    if (System != nullptr)
    {
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
            if (Object.DockWith != BaseId)
                continue;
            CachedObjectPosition[0] = Object.Pos.x;
            CachedObjectPosition[1] = Object.Pos.y;
            CachedObjectPosition[2] = Object.Pos.z;
            CachedObjectId = BaseId;
            Out[0] = Object.Pos.x;
            Out[1] = Object.Pos.y;
            Out[2] = Object.Pos.z;
            return Out;
        }

        // A base a mission created -- not cached.
        float Pos[3];
        int IdsInfo;
        if (FindMissionCreatedSolar(BaseId, Pos, &IdsInfo))
        {
            Out[0] = Pos[0];
            Out[1] = Pos[1];
            Out[2] = Pos[2];
            return Out;
        }
    }
    Out[0] = 0.0f;
    Out[1] = 0.0f;
    Out[2] = 0.0f;
    return Out;
}

void NeuroNetNavMap::PlayerMapPosition(Vector* OutPos, float* OutHeading)
{
    // FUN_00491be0.
    if (ViewMode == kNavMapViewUniverse)
    {
        // The universe view -- the player's system's map position, not
        // scaled by SystemScale.
        OutPos->x = 0.0f;
        OutPos->y = 0.0f;
        OutPos->z = 0.0f;
        *OutHeading = 0.0f;
        for (Universe::ISystem* System = Universe::GetFirstSystem(); System != nullptr;
             System = Universe::GetNextSystem())
        {
            if (System->GetId() != GetPlayerStarSystemIdRetail())
                continue;
            Vector const* const Pos = System->GetPos();
            OutPos->x = Pos->x;
            OutPos->y = Pos->y;
            OutPos->z = Pos->z;
            OutPos->z = static_cast<float>(static_cast<double>(OutPos->y) *
                                               static_cast<double>(kNavMapUniverseScale) -
                                           static_cast<double>(kNavMapUniverseOffset));
            OutPos->y = 0.0f;
            OutPos->x = static_cast<float>(static_cast<double>(OutPos->x) *
                                               static_cast<double>(kNavMapUniverseScale) -
                                           static_cast<double>(kNavMapUniverseOffset));
            OutPos->z = static_cast<float>(static_cast<double>(OutPos->z) +
                                           static_cast<double>(kNavMapUniverseLift));
            return;
        }
        return;
    }

    // The system view -- the player's last position.
    float const* const LastPos = GetPlayerLastPositionRetail();
    OutPos->x = LastPos[0];
    OutPos->y = LastPos[1];
    OutPos->z = LastPos[2];
    *OutHeading = 0.0f;

    if (IsPlayerDockedRetail())
    {
        // Docked -- the base's solar (FUN_00491e10). The base is read
        // through IBase slot 0 (its nickname id).
        Universe::IBase const* const Base = Universe::get_base(GetPlayerBaseIdRetail());
        if (Base != nullptr)
        {
            Universe::ISystem const* const System =
                Universe::get_system(GetPlayerStarSystemIdRetail());
            float Pos[3];
            float const* const Result = CachedBasePosition(Pos, Base->NicknameId, System);
            OutPos->x = Result[0];
            OutPos->y = Result[1];
            OutPos->z = Result[2];
        }
    }
    else
    {
        // In space -- the heading from the orientation's third column
        // (m[0][2], m[2][2]) normalised, in degrees, wrapped into [0, 360).
        float const* const Orientation = GetPlayerLastOrientationRetail();
        double const A = static_cast<double>(Orientation[2]);
        double const B = static_cast<double>(Orientation[8]);
        double const InvLength = 1.0 / sqrt(B * B + A * A);
        float const X = static_cast<float>(A * InvLength);
        float const Y = static_cast<float>(B * InvLength);
        double Heading =
            -static_cast<double>(HudVectorAngle(X, Y)) * static_cast<double>(kNavMapRadToDeg);
        *OutHeading = static_cast<float>(Heading);
        // x87: only "less than 0" enters the loop (the compare runs on the
        // unrounded value).
        if (Heading < 0.0)
        {
            do
            {
                Heading += static_cast<double>(kNavMapFullTurn);
            } while (Heading < 0.0);
            *OutHeading = static_cast<float>(Heading);
        }
        // x87: greater or equal (not unordered) enters the loop.
        double Wrapped = static_cast<double>(*OutHeading);
        if (Wrapped >= static_cast<double>(kNavMapFullTurn))
        {
            do
            {
                Wrapped -= static_cast<double>(kNavMapFullTurn);
            } while (Wrapped >= static_cast<double>(kNavMapFullTurn));
            *OutHeading = static_cast<float>(Wrapped);
        }
    }

    // Scaled by the system's NavMapScale.
    double const Scale = static_cast<double>(SystemScale);
    OutPos->x = static_cast<float>(Scale * static_cast<double>(OutPos->x));
    OutPos->y = static_cast<float>(Scale * static_cast<double>(OutPos->y));
    OutPos->z = static_cast<float>(Scale * static_cast<double>(OutPos->z));
}

// ----------------------------------------------------------------------------------------
// Item pulse
// ----------------------------------------------------------------------------------------

void NeuroNetNavMap::AnimateItems()
{
    // FUN_004a1710.
    float const Dt = static_cast<float>(GetClientFrameTimeCapped());

    // The highlight timer. x87: the re-arm is skipped when the
    // timer is still greater than 0 (or unordered); <= 0 re-arms it.
    double const Remaining = static_cast<double>(PulseTimer) - static_cast<double>(Dt);
    PulseTimer = static_cast<float>(Remaining);
    if (Remaining <= 0.0)
    {
        int const Delay = rand() * 0x7d0 / 0x8000 + 0x64;
        int const ItemCount = static_cast<int>(Items.size());
        PulseTimer = static_cast<float>(static_cast<double>(static_cast<unsigned>(Delay)) *
                                            static_cast<double>(kNavMapMilli) +
                                        static_cast<double>(PulseTimer));
        PulseIndex = rand() * ItemCount * 3 / 0x8000;
    }

    int Index = 0;
    for (std::list<FNavMapItem*>::iterator It = Items.begin(); It != Items.end(); ++It, ++Index)
    {
        FNavMapItem* const Item = *It;
        if (Item->bShown == 0 || Item->Kind != kNavMapItemSolar)
            continue;

        // The phase advances and wraps while it is above 1 (the compare
        // runs on the unrounded sum; only the final value is stored).
        double Phase = static_cast<double>(Dt) * static_cast<double>(Item->PulseRate) +
                       static_cast<double>(Item->PulsePhase);
        Item->PulsePhase = static_cast<float>(Phase);
        if (Phase > 1.0)
        {
            do
            {
                Phase -= 1.0;
            } while (Phase > 1.0);
            Item->PulsePhase = static_cast<float>(Phase);
        }

        // A triangle wave, 0 -> 1 -> 0 over one phase (x87:
        // only "less than 0.5" keeps the phase itself).
        double Tri;
        if (Item->PulsePhase < 0.5f)
            Tri = static_cast<double>(Item->PulsePhase);
        else
            Tri = 1.0 - static_cast<double>(Item->PulsePhase);
        double const Wave = Tri + Tri;

        Item->Size =
            static_cast<float>(kNavMapPulseSwing * Wave + static_cast<double>(Item->RestSize));

        // Alpha = min(level * 4, 255), 255 for the highlighted item;
        // the tint = level + 191 (truncated to a byte).
        int const Level = static_cast<int>(Wave * static_cast<double>(kNavMapPulseAlphaScale));
        int Alpha = Level * 4;
        if (Alpha > 0xff)
            Alpha = 0xff;
        int const Tint =
            static_cast<int>(static_cast<double>(Level) + static_cast<double>(kNavMapPulseBase));
        if (PulseIndex != Index)
            Alpha = 0xff;
        unsigned char const TintByte = static_cast<unsigned char>(Tint);
        unsigned char const AlphaByte = static_cast<unsigned char>(Alpha);

        float* const Colour = Item->Mesh.UserFloats;
        switch (Item->Variant)
        {
        case 0:
            // Red full, green / blue the tint.
            Colour[0] = 1.0f;
            Colour[1] = NavMapByteToUnit(TintByte);
            Colour[2] = Colour[1];
            Colour[4] = NavMapByteToUnit(AlphaByte);
            break;
        case 1:
            // Green full.
            Colour[1] = 1.0f;
            Colour[0] = NavMapByteToUnit(TintByte);
            Colour[2] = Colour[0];
            Colour[4] = NavMapByteToUnit(AlphaByte);
            break;
        case 2:
            // Blue full.
            Colour[2] = 1.0f;
            Colour[0] = NavMapByteToUnit(TintByte);
            Colour[1] = Colour[0];
            Colour[4] = NavMapByteToUnit(AlphaByte);
            break;
        case 3:
            // Green and blue full.
            Colour[1] = 1.0f;
            Colour[2] = 1.0f;
            Colour[0] = NavMapByteToUnit(TintByte);
            Colour[4] = NavMapByteToUnit(AlphaByte);
            break;
        case 4:
        {
            // {0xff, tint, 0xff, alpha} packed (FUN_004a1ad0), then every byte
            // scaled back (so red / blue are 255 * (1/255), not a literal 1.0).
            unsigned char const Rgba[4] = {0xff, TintByte, 0xff, AlphaByte};
            Colour[0] = NavMapByteToUnit(Rgba[0]);
            Colour[1] = NavMapByteToUnit(Rgba[1]);
            Colour[2] = NavMapByteToUnit(Rgba[2]);
            Colour[4] = NavMapByteToUnit(Rgba[3]);
            break;
        }
        case 5:
        {
            // {0xff, 0xff, tint, alpha} (FUN_004a1ad0).
            unsigned char const Rgba[4] = {0xff, 0xff, TintByte, AlphaByte};
            Colour[0] = NavMapByteToUnit(Rgba[0]);
            Colour[1] = NavMapByteToUnit(Rgba[1]);
            Colour[2] = NavMapByteToUnit(Rgba[2]);
            Colour[4] = NavMapByteToUnit(Rgba[3]);
            break;
        }
        case 6:
        {
            // {tint, tint, tint, alpha} (FUN_004a1ab0) through FUN_004a1af0.
            unsigned char const Rgba[4] = {TintByte, TintByte, TintByte, AlphaByte};
            ClientMeshHolderSetColor(&Item->Mesh, Rgba);
            break;
        }
        default:
            // Above 6 -- untouched.
            break;
        }
    }
}

// ----------------------------------------------------------------------------------------
// Group positions (IClient slot 96)
// ----------------------------------------------------------------------------------------

void NavMapSetGroupPositions(void const* Data)
{
    // FUN_0048d8a0 (cdecl).
    G_NavMapGroupPositions.clear();

    int Count = 0;
    memcpy(&Count, Data, sizeof(Count));
    // Retail bug kept: the record pointer is never advanced inside the loop (only the grow
    // path's scratch copy is), so the FIRST record is stored Count times.
    FNavMapGroupMember const* const Record = reinterpret_cast<FNavMapGroupMember const*>(
        static_cast<unsigned char const*>(Data) + sizeof(int));
    double const kNearOrigin = 128.0; // DAT_005d2ee0 (double)
    for (int Index = 0; Index < Count; ++Index)
    {
        FNavMapGroupMember Member;
        if (Record->BaseId != 0)
        {
            // Docked -- the base's position (FUN_0048d780). The member is kept unless all three
            // components lie within 128 of the origin (an unordered compare keeps it).
            float Pos[3];
            NavMapBasePosition(Pos, Record->BaseId, Record->SystemId);
            if (!(fabs(static_cast<double>(Pos[0])) < kNearOrigin) ||
                !(fabs(static_cast<double>(Pos[1])) < kNearOrigin) ||
                !(fabs(static_cast<double>(Pos[2])) < kNearOrigin))
            {
                Member.Pos[0] = Pos[0];
                Member.Pos[1] = Pos[1];
                Member.Pos[2] = Pos[2];
                Member.BaseId = Record->BaseId;
                Member.SystemId = Record->SystemId;
                Member.RepId = Record->RepId;
                G_NavMapGroupPositions.push_back(Member); // FUN_004a3680(last, 1, &rec)
            }
            continue;
        }

        // In space -- the record's own position under the same 128.0 test.
        if (!(fabs(static_cast<double>(Record->Pos[0])) < kNearOrigin) ||
            !(fabs(static_cast<double>(Record->Pos[1])) < kNearOrigin) ||
            !(fabs(static_cast<double>(Record->Pos[2])) < kNearOrigin))
        {
            Member.Pos[0] = Record->Pos[0];
            Member.Pos[1] = Record->Pos[1];
            Member.Pos[2] = Record->Pos[2];
            Member.BaseId = 0;
            Member.SystemId = Record->SystemId;
            Member.RepId = Record->RepId;
            G_NavMapGroupPositions.push_back(Member);
        }
    }
}

int NeuroNetNavMap::GroupMemberAt(Vector const* MapPos)
{
    // FUN_004a21f0.
    if (SinglePlayer())
        return -1;
    int const Count = static_cast<int>(G_NavMapGroupPositions.size());

    // The pick radius ShipMarkerSize / max(ZoomScale / 2, 1) * 1.05, squared.
    double Half = static_cast<double>(ZoomScale) * 0.5;
    if (Half < 1.0)
        Half = 1.0;
    double const Radius =
        static_cast<double>(ShipMarkerSize) / Half * static_cast<double>(kNavMapPickMargin);
    float const Radius2 = static_cast<float>(Radius * Radius);

    for (int Index = 0; Index < Count; ++Index)
    {
        FNavMapGroupMember const& Member = G_NavMapGroupPositions[static_cast<size_t>(Index)];
        if (Member.SystemId != CurrentSystem->GetId())
            continue;
        double const Dx = static_cast<double>(MapPos->x) - static_cast<double>(Member.Pos[0]);
        double const Dy = static_cast<double>(MapPos->y) - static_cast<double>(Member.Pos[1]);
        double const Dz = static_cast<double>(MapPos->z) - static_cast<double>(Member.Pos[2]);
        if (Dx * Dx + Dz * Dz + Dy * Dy <= static_cast<double>(Radius2))
            return Member.RepId;
    }
    return -1;
}

// ----------------------------------------------------------------------------------------
// Picking
// ----------------------------------------------------------------------------------------

Universe::IZone const* NavMapZoneAt(Universe::ISystem const* System, float const* Pos,
                                    unsigned char bNamed, unsigned char bInfo,
                                    unsigned char bShowAll)
{
    // FUN_004a1b50 (cdecl).
    ISpatialPartition* const Tree = System->GetZoneTree();
    if (Tree == nullptr)
        return nullptr;

    // A 0.1 sphere at Pos into a 0x800-slot result.
    Geometry::Sphere Query;
    Query.Center.x = Pos[0];
    Query.Center.y = Pos[1];
    Query.Center.z = Pos[2];
    Query.Radius = 0.1f;
    FSpatialQueryResult Result; // 0x2008 bytes on the stack, as retail's __chkstk frame
    Result.Capacity = 0x800;
    Result.Count = 0;
    if (SpatialFill(Tree, &Query, &Result) == 0)
        return nullptr;

    Universe::IZone const* Best = nullptr;
    float BestRadius = 1.0e20f; // 0x60ad78ec
    // Retail hands the caller's float[3] straight in as the Vector.
    Vector Point;
    Point.x = Pos[0];
    Point.y = Pos[1];
    Point.z = Pos[2];
    for (unsigned Index = 0; Index < Result.Count; ++Index)
    {
        Universe::IZone const* const Zone =
            static_cast<Universe::IZone const*>(Result.Items[Index]);
        if (bNamed != 0 && Zone->GetName() == 0)
            continue;
        if (bInfo != 0 && Zone->GetIdsInfo() == 0)
            continue;
        if (bShowAll == 0)
        {
            // The visit map's flags | the zone's own visit byte.
            unsigned char VisitFlags = 0;
            GetPlayerVisitFlagsRetail(Zone->GetId(), &VisitFlags, 1, 0); // FUN_004c4da0
            unsigned char const Visit = static_cast<unsigned char>(VisitFlags | Zone->GetVisit());
            if ((Visit & kVisitShown) == 0)
                continue;
            if (bInfo != 0 && (Visit & 8) == 0)
                continue;
        }
        if (!Zone->InZoneXZ(Point, 0.0f))
            continue;
        // Only a strictly smaller radius wins.
        float const Radius = Zone->GetRadius();
        if (Radius < BestRadius)
        {
            BestRadius = Radius;
            Best = Zone;
        }
    }
    return Best;
}

bool NeuroNetNavMap::MapPointUnderCursor(Vector* OutMapPos)
{
    // FUN_00492410. The cursor inside ScreenRect (x kept in double, y rounded to float). x87:
    // a negative (or unordered) offset is rejected, as is one at or past the size: the rect is
    // [0, size).
    int MouseX = 0;
    int MouseY = 0;
    GetClientSoftwareCursor(&MouseX, &MouseY, nullptr); // DAT_00616840 / DAT_00616844
    float const ScreenX = static_cast<float>(MouseX);
    float const ScreenY = static_cast<float>(MouseY);
    double const Dx = static_cast<double>(MouseX) - static_cast<double>(ScreenRectMin[0]);
    float const Dy = static_cast<float>(static_cast<double>(MouseY) -
                                        static_cast<double>(ScreenRectMin[1]));
    if (!(Dx >= 0.0) || !(Dy >= 0.0f))
        return false;
    if (!(Dx < static_cast<double>(ScreenRectSize[0])) || !(Dy < ScreenRectSize[1]))
        return false;

    if (ViewMode == kNavMapViewSystem || ViewMode == kNavMapViewUniverse)
    {
        // The camera ray to the map plane, then the map-frame fractions to
        // map units.
        Vector Ray;
        AsCamera(&Camera)->Unproject(&Ray, ScreenX, ScreenY);
        double const K = kNavMapPlaneDepth / static_cast<double>(Ray.z);
        double const A = static_cast<double>(Ray.x) * K;
        double const B = static_cast<double>(Ray.y) * K;
        float const V = static_cast<float>(-(B / (static_cast<double>(MapSize[1]) * 0.5)));
        OutMapPos->x = static_cast<float>(A / (static_cast<double>(MapSize[0]) * 0.5) *
                                          static_cast<double>(MapExtentX));
        OutMapPos->y = 0.0f;
        OutMapPos->z = static_cast<float>(static_cast<double>(V) *
                                          static_cast<double>(MapExtentZ));
        return true;
    }
    if (ViewMode != kNavMapViewSystemZoomed)
        return false;

    // Zoomed in -- the ray's point as fractions of the zoomed frame, less
    // the pan, back to map units.
    Vector Ray;
    AsCamera(&Camera)->Unproject(&Ray, ScreenX, ScreenY);
    double const K = kNavMapPlaneDepth / static_cast<double>(Ray.z);
    double const A = static_cast<double>(Ray.x) * K;
    float const B = static_cast<float>(static_cast<double>(Ray.y) * K);
    double const U = A / (static_cast<double>(MapSize[0]) * 0.5) * 0.5 + 0.5;
    double const V = -1.0 / (static_cast<double>(MapSize[1]) * 0.5) * static_cast<double>(B) *
                         0.5 +
                     0.5;
    double const InvZoom = 1.0 / static_cast<double>(ZoomScale);
    float const ZoomedX = static_cast<float>(static_cast<double>(MapSize[0]) * InvZoom);
    float const ZoomedY = static_cast<float>(static_cast<double>(MapSize[1]) * InvZoom);
    float const FracX = static_cast<float>(U * static_cast<double>(ZoomedX));
    float const HalfZoomedY = static_cast<float>(static_cast<double>(ZoomedY) * 0.5);
    double const LocalX = static_cast<double>(FracX) - static_cast<double>(ZoomedX) * 0.5;
    float const LocalY = static_cast<float>(V * static_cast<double>(ZoomedY) -
                                            static_cast<double>(HalfZoomedY));
    float const NegPanY = -PanOffset[1];
    double const PannedX = LocalX - static_cast<double>(PanOffset[0]);
    double const PannedY = static_cast<double>(LocalY) - static_cast<double>(NegPanY);
    float const HalfY = static_cast<float>(static_cast<double>(MapSize[1]) * 0.5);
    float const SumX = static_cast<float>(static_cast<double>(MapSize[0]) * 0.5 + PannedX);
    double const SumY = static_cast<double>(HalfY) + PannedY;
    double const FracY = SumY / static_cast<double>(MapSize[1]);
    OutMapPos->x = static_cast<float>(
        ((static_cast<double>(SumX) / static_cast<double>(MapSize[0])) * 2.0 - 1.0) *
        static_cast<double>(MapExtentX));
    OutMapPos->y = 0.0f;
    OutMapPos->z = static_cast<float>((FracY * 2.0 - 1.0) * static_cast<double>(MapExtentZ));
    return true;
}

bool NeuroNetNavMap::CursorRayPoint(Vector* OutPoint)
{
    // FUN_0049e890 -- the same ScreenRect test as FUN_00492410.
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

    // The cursor on the Win3D camera's near plane, turned by the camera
    // (a copy of its orientation, FUN_00423b90).
    FSpaceCamera const* const Cam = Win3DGetCamera(); // DAT_0067dbf8
    Vector Local;
    Local.x = static_cast<float>((static_cast<double>(ScreenX) -
                                  static_cast<double>(Cam->ScreenCenterX)) /
                                 static_cast<double>(Cam->XScale) *
                                 static_cast<double>(Cam->ZNear));
    Local.y = static_cast<float>((static_cast<double>(ScreenY) -
                                  static_cast<double>(Cam->ScreenCenterY)) /
                                 static_cast<double>(Cam->YScale) *
                                 static_cast<double>(Cam->ZNear));
    Local.z = -Cam->ZNear;
    Matrix Orientation;
    memcpy(&Orientation, &Cam->World, sizeof(Orientation));
    Vector Turned;
    Win3DTransformByMatrix(Turned, Orientation, Local);

    // Scaled onto this control's depth (Offset z + Position z), moved by
    // (0.0025, -0.001) and made relative to Offset + Position in x / y; z is the scaled ray's
    // own and then 0.
    double const K = (static_cast<double>(Offset.z) + static_cast<double>(Position.z)) /
                     static_cast<double>(Turned.z);
    float const X = static_cast<float>(static_cast<double>(Turned.x) * K);
    float const Y = static_cast<float>(static_cast<double>(Turned.y) * K);
    double const Z = K * static_cast<double>(Turned.z);
    double const OriginX = static_cast<double>(Position.x) + static_cast<double>(Offset.x);
    float const OriginY = static_cast<float>(static_cast<double>(Offset.y) +
                                             static_cast<double>(Position.y));
    OutPoint->x = static_cast<float>(static_cast<double>(X) +
                                     static_cast<double>(kNavMapRayOffsetX));
    OutPoint->y = static_cast<float>(static_cast<double>(Y) -
                                     static_cast<double>(kNavMapRayOffsetY));
    OutPoint->z = static_cast<float>(Z);
    OutPoint->x = static_cast<float>(static_cast<double>(OutPoint->x) - OriginX);
    OutPoint->y = static_cast<float>(static_cast<double>(OutPoint->y) -
                                     static_cast<double>(OriginY));
    OutPoint->z = 0.0f;
    return true;
}

FNavMapItem* NeuroNetNavMap::ItemAt(Vector const* MapPos, unsigned char bSkipWaypoints)
{
    // FUN_00496d40.
    for (std::list<FNavMapItem*>::iterator It = Items.begin(); It != Items.end(); ++It)
    {
        FNavMapItem* const Item = *It;
        if (bSkipWaypoints != 0 && (Item->Kind == kNavMapItemWaypoint || Item->Kind == kNavMapItemBestPath))
            continue;
        if (Item->bShown == 0)
            continue;

        // The distance in the XZ plane, squared.
        double const Dx = static_cast<double>(MapPos->x) - static_cast<double>(Item->Position.x);
        double const Dz = static_cast<double>(MapPos->z) - static_cast<double>(Item->Position.z);
        double const Dist2 = Dz * Dz + Dx * Dx;

        // The pick radius -- a universe-view system's is 1.5 x; the markers
        // (Kind 2 / 3 / 4) shrink with the zoom (over max(ZoomScale / 2, 1)), the rest follow
        // SystemScale; then the 1.05 margin.
        double Radius = static_cast<double>(Item->Size);
        if ((Item->Type & OBJ_SUN) != 0)
            Radius *= static_cast<double>(kNavMapSystemPickScale);
        if (Item->Kind == kNavMapItemPlayer || Item->Kind == kNavMapItemWaypoint || Item->Kind == kNavMapItemBestPath)
        {
            double Half = static_cast<double>(ZoomScale) * 0.5;
            if (Half < 1.0)
                Half = 1.0;
            Radius /= Half;
        }
        else
        {
            Radius *= static_cast<double>(SystemScale);
        }
        Radius *= static_cast<double>(kNavMapPickMargin);

        if (Dist2 <= Radius * Radius)
            return Item;
    }
    return nullptr;
}

// ----------------------------------------------------------------------------------------
// Waypoint markers
// ----------------------------------------------------------------------------------------

bool NeuroNetNavMap::ResolveBestPathWaypoint(int Index, float* OutPos, unsigned* OutSystem)
{
    // FUN_004a0800. The cached answer while the waypoint's hash matches.
    unsigned const Hash = BestPathWaypointHash(Index); // FUN_004c5530
    if (Hash == CachedWaypointHash)
    {
        OutPos[0] = CachedWaypointPos[0];
        OutPos[1] = CachedWaypointPos[1];
        OutPos[2] = CachedWaypointPos[2];
        *OutSystem = CachedWaypointSystem;
        return true;
    }

    // The record is used without a null test, as in retail.
    FmtStr::Val const* Token = BestPathGetWaypoint(Index)->Marker.Head; // FUN_004c4ff0
    unsigned char bSystemKnown = 0;
    for (; Token != nullptr; Token = Token->Next)
    {
        unsigned short const Tag = Token->TypeTag;
        if (Tag == FmtStr::TagSystem)
        {
            // 'S' -- the star system.
            *OutSystem = Token->Value;
            bSystemKnown = 1;
            continue;
        }
        if (Tag == FmtStr::TagNavMarker)
        {
            // 'N' -- a nav marker: {system, ?, position}.
            unsigned const* const Marker = Token->Marker->Data;
            *OutSystem = Marker[0];
            float const* const MarkerPos = reinterpret_cast<float const*>(Marker + 2);
            OutPos[0] = MarkerPos[0];
            OutPos[1] = MarkerPos[1];
            OutPos[2] = MarkerPos[2];
            CachedWaypointPos[0] = MarkerPos[0];
            CachedWaypointPos[1] = MarkerPos[1];
            CachedWaypointPos[2] = MarkerPos[2];
            CachedWaypointSystem = *OutSystem;
            CachedWaypointHash = Hash;
            return true;
        }
        if (Tag != FmtStr::TagBase)
            continue;

        // 'B' -- the base: the system object docking with it, searched in
        // the 'S' system (when one came first) and then, as retail's iterator runs on, in every
        // system after the first.
        unsigned const BaseId = Token->Value;
        Universe::ISystem const* System = Universe::GetFirstSystem();
        if (bSystemKnown != 0)
            System = Universe::get_system(*OutSystem);
        while (System != nullptr)
        {
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
            for (unsigned Slot = 0; Slot < Count; ++Slot)
            {
                if (!System->GetSystemObject(Slot, &Object))
                    continue;
                if (Object.DockWith != BaseId)
                    continue;
                OutPos[0] = Object.Pos.x;
                OutPos[1] = Object.Pos.y;
                OutPos[2] = Object.Pos.z;
                *OutSystem = System->GetId();
                CachedWaypointPos[0] = OutPos[0];
                CachedWaypointPos[1] = OutPos[1];
                CachedWaypointPos[2] = OutPos[2];
                CachedWaypointSystem = *OutSystem;
                CachedWaypointHash = Hash;
                return true;
            }
            System = Universe::GetNextSystem();
        }
    }
    return false;
}

bool NeuroNetNavMap::RefreshWaypointItems()
{
    // FUN_00496e50.
    unsigned char bMoved = 0;

    // One marker per player waypoint (created on first use).
    int const WaypointCount = GetPlayerWaypointCountRetail(); // FUN_004c4690
    for (int Index = 0; Index < WaypointCount; ++Index)
    {
        if (WaypointItems[Index] == nullptr)
        {
            // No null test of the new item, as in retail.
            FNavMapItem* const Item =
                AddItem(kNavMapItemWaypoint, 0, 0.0f, 0.0f, 0.0f,
                        static_cast<float>(static_cast<double>(WaypointMarkerSize) +
                                           static_cast<double>(WaypointMarkerSize)),
                        "NAV_waypointdiamond", 0x219b, 0, 0.0f, 0, 0);
            WaypointItems[Index] = Item;
            Item->Variant = Index + 1;
            Item->Ordinal = Index + 1;
            Item->bShown = 1;
            NavMapTintFromPalette(Item, G_NnPaletteViolet);
        }

        FNavMapItem* const Item = WaypointItems[Index];
        FPlayerWaypointRecord const* const Record = GetPlayerWaypointRetail(Index); // FUN_004c46a0
        // The record's +0x0c is compared with the current system's id.
        if (Record == nullptr || Record->SystemId != CurrentSystem->GetId())
        {
            Item->bShown = 0;
            continue;
        }

        // The first and the last waypoint show their id; the markers
        // between show none.
        int Number = Record->AutoId;
        if (Index > 0 && Index < WaypointCount - 1)
            Number = 0;
        Item->bShown = 1;
        Item->Ordinal = Record->AutoId;
        double const Scale = static_cast<double>(SystemScale);
        Vector const Pos = {static_cast<float>(Scale * static_cast<double>(Record->Pos.x)),
                            static_cast<float>(Scale * static_cast<double>(Record->Pos.y)),
                            static_cast<float>(Scale * static_cast<double>(Record->Pos.z))};
        // x87: any component not equal (or unordered) moves the marker.
        if (!(Item->Position.x == Pos.x) || !(Item->Position.y == Pos.y) ||
            !(Item->Position.z == Pos.z) || Item->Variant != Number)
        {
            Item->Position.x = Pos.x;
            Item->Position.y = Pos.y;
            Item->Position.z = Pos.z;
            Item->Variant = Number;
            bMoved = 1;
        }
    }
    for (int Index = WaypointCount; Index < 0x63; ++Index)
    {
        if (WaypointItems[Index] != nullptr)
            WaypointItems[Index]->bShown = 0;
    }

    // One marker per best-path waypoint.
    unsigned char bPathChanged = 0;
    int const PathCount = BestPathGetWaypointCount(); // FUN_004c4fb0
    int NextNumber = 1;
    for (int Index = 0; Index < PathCount; ++Index)
    {
        if (BestPathItems[Index] == nullptr)
        {
            FNavMapItem* const Item =
                AddItem(kNavMapItemBestPath, 0, 0.0f, 0.0f, 0.0f,
                        static_cast<float>(static_cast<double>(WaypointMarkerSize) +
                                           static_cast<double>(WaypointMarkerSize)),
                        "NAV_waypointdiamond", 0x219b, 0, 0.0f, 0, 0);
            BestPathItems[Index] = Item;
            Item->bShown = 0;
            bPathChanged = 1;
            NavMapTintFromPalette(Item, G_NnPaletteViolet);
        }

        FNavMapItem* const Item = BestPathItems[Index];
        // The record's kind (low nibble of +0x18): 0xa numbered stops,
        // 9 no number, anything else -1. No null test of the record, as in retail.
        FBestPathWaypoint* const Waypoint = BestPathGetWaypoint(Index); // FUN_004c4ff0
        unsigned const RecordKind = Waypoint->ObjectiveState & kObjectiveStateMask;
        if (RecordKind == kObjectiveStateNumbered)
            Item->Variant = NextNumber++;
        else if (RecordKind == kObjectiveStateNoNumber)
            Item->Variant = 0;
        else
            Item->Variant = -1;

        // The record's own position (FUN_004c52d0), else FUN_004a0800.
        Vector Pos;
        unsigned SystemId = 0;
        bool bResolved = BestPathWaypointResolvePosition(Waypoint, &Pos, &SystemId);
        if (!bResolved)
        {
            float Resolved[3];
            bResolved = ResolveBestPathWaypoint(Index, Resolved, &SystemId);
            Pos.x = Resolved[0];
            Pos.y = Resolved[1];
            Pos.z = Resolved[2];
        }
        if (!bResolved)
        {
            Item->bShown = 0;
            continue;
        }
        if (SystemId != CurrentSystem->GetId())
        {
            Item->bShown = 0;
            continue;
        }

        Item->bShown = 1;
        double const Scale = static_cast<double>(SystemScale);
        float const Scaled[3] = {static_cast<float>(static_cast<double>(Pos.x) * Scale),
                                 static_cast<float>(static_cast<double>(Pos.y) * Scale),
                                 static_cast<float>(static_cast<double>(Pos.z) * Scale)};
        if (!(Item->Position.x == Scaled[0]) || !(Item->Position.y == Scaled[1]) ||
            !(Item->Position.z == Scaled[2]))
        {
            Item->Position.x = Scaled[0];
            Item->Position.y = Scaled[1];
            Item->Position.z = Scaled[2];
            bMoved = 1;
            bPathChanged = 1;
        }
    }
    for (int Index = PathCount; Index < 0x63; ++Index)
    {
        if (BestPathItems[Index] != nullptr)
            BestPathItems[Index]->bShown = 0;
    }

    if (bPathChanged != 0)
        RefreshMissionZones(); // FUN_004a0580

    // The objectives follow the waypoints while there are some and no best
    // path with a text, else the best path.
    if (GetPlayerWaypointCountRetail() == 0 || !BestPathHasText())
    {
        if (GetPlayerWaypointCountRetail() != 0 && bBestPathObjectives != 0)
        {
            bBestPathObjectives = 0;
            RefreshObjectives(); // FUN_0049f7a0
        }
        else if (BestPathHasText() && bBestPathObjectives == 0)
        {
            bBestPathObjectives = 1;
            RefreshObjectives();
        }
    }

    if (G_bNavMapRouteChanged != 0)
    {
        RefreshObjectives();
        RefreshMissionInfo(); // FUN_0049a580
    }

    // The clear-all button with the waypoint count.
    if (GetPlayerWaypointCountRetail() != LastWaypointCount)
    {
        LastWaypointCount = GetPlayerWaypointCountRetail();
        unsigned const bHide =
            (GetPlayerWaypointCountRetail() == 0 || bBestPathObjectives != 0) ? 1u : 0u;
        ClearAllWaypointsButton->SetProperty(kWinPropHide, bHide, 0);
        ClearAllWaypointsText->SetProperty(kWinPropHide, bHide, 0);
        RefreshObjectives();
        BuildJumpLinks(); // FUN_004975a0
    }
    return bMoved != 0;
}

// ----------------------------------------------------------------------------------------
// Item placement
// ----------------------------------------------------------------------------------------

void NeuroNetNavMap::PlaceItems()
{
    // FUN_00496600. The zoomed frame -- its size (MapSize * ZoomScale)
    // and its corner (the pan scaled, less the half size scaled), with retail's float stores.
    double const Zoom = static_cast<double>(ZoomScale);
    float const ZoomLess1 = static_cast<float>(Zoom - 1.0);
    float const NegHalfX = static_cast<float>(-(static_cast<double>(MapSize[0]) * 0.5));
    float const NegHalfY = static_cast<float>(-(static_cast<double>(MapSize[1]) * 0.5));
    float const HalfZ = static_cast<float>(static_cast<double>(MapSize[2]) * 0.5);
    float const NegHalfZ = -HalfZ;
    float const GrowX = static_cast<float>(static_cast<double>(MapSize[0]) * 0.5 *
                                           static_cast<double>(ZoomLess1));
    double const GrowY = static_cast<double>(MapSize[1]) * 0.5 * static_cast<double>(ZoomLess1);
    double const GrowZ = static_cast<double>(HalfZ) * static_cast<double>(ZoomLess1);
    float const CornerX0 = static_cast<float>(static_cast<double>(NegHalfX) -
                                              static_cast<double>(GrowX));
    float const CornerY0 = static_cast<float>(static_cast<double>(NegHalfY) - GrowY);
    double const CornerZ0 = static_cast<double>(NegHalfZ) - GrowZ;
    float const PanY = static_cast<float>(static_cast<double>(PanOffset[1]) * Zoom);
    float const PanZ = static_cast<float>(static_cast<double>(PanOffset[2]) * Zoom);
    float const OriginX = static_cast<float>(static_cast<double>(PanOffset[0]) * Zoom +
                                             static_cast<double>(CornerX0));
    float const OriginY = static_cast<float>(static_cast<double>(PanY) +
                                             static_cast<double>(CornerY0));
    float const OriginZ = static_cast<float>(static_cast<double>(PanZ) + CornerZ0);
    float const FrameX = static_cast<float>(static_cast<double>(MapSize[0]) * Zoom);
    float const FrameY = static_cast<float>(static_cast<double>(MapSize[1]) * Zoom);
    float const FrameZ = static_cast<float>(static_cast<double>(MapSize[2]) * Zoom);

    IEngine* const Engine = DALib::Engine;
    for (std::list<FNavMapItem*>::iterator It = Items.begin(); It != Items.end(); ++It)
    {
        FNavMapItem* const Item = *It;
        int const Kind = Item->Kind;
        int const Type = Item->Type;
        // Stars and objects only with a type the mask lets through.
        if ((Kind == kNavMapItemSolar || Kind == 1) && (SolarTypeMask & static_cast<unsigned>(Type)) == 0)
            continue;

        if (Kind == kNavMapItemPlayer)
        {
            // The player marker pinned to the map's edge, pointing out.
            unsigned char const bWasClamped = bItemClamped;
            bItemClamped = 0;
            if (Item->Position.x > kNavMapEdge)
            {
                Item->Position.x = kNavMapEdge;
                bItemClamped = 1;
                Item->Heading = 0.0f;
            }
            if (Item->Position.x < -kNavMapEdge)
            {
                Item->Position.x = -kNavMapEdge;
                bItemClamped = 1;
                Item->Heading = 180.0f;
            }
            if (Item->Position.z > kNavMapEdge)
            {
                Item->Position.z = kNavMapEdge;
                bItemClamped = 1;
                Item->Heading = 270.0f;
            }
            if (Item->Position.z < -kNavMapEdge)
            {
                Item->Position.z = -kNavMapEdge;
                bItemClamped = 1;
                Item->Heading = 90.0f;
            }
            // The mesh follows the change -- the off-screen arrow, or the
            // position marker back.
            if (bWasClamped != bItemClamped)
            {
                if (bItemClamped != 0)
                    NavMapReloadItemMesh(Item, "NAV_offscreenarrow");
                else
                    NavMapReloadItemMesh(Item, "NNM_SM_INFO_POSITION");
            }
        }
        else
        {
            // The rest clamped to the extents.
            if (Item->Position.x > MapExtentX)
                Item->Position.x = MapExtentX;
            if (-MapExtentX > Item->Position.x)
                Item->Position.x = -MapExtentX;
            if (Item->Position.z > MapExtentZ)
                Item->Position.z = MapExtentZ;
            if (-MapExtentZ > Item->Position.z)
                Item->Position.z = -MapExtentZ;
        }

        // The item's place in the zoomed frame (z at the map plane).
        float const TwiceExtZ = static_cast<float>(static_cast<double>(MapExtentZ) +
                                                   static_cast<double>(MapExtentZ));
        double const U = (static_cast<double>(Item->Position.x) + static_cast<double>(MapExtentX)) /
                         (static_cast<double>(MapExtentX) + static_cast<double>(MapExtentX));
        double const V = (static_cast<double>(MapExtentZ) + static_cast<double>(Item->Position.z)) /
                         static_cast<double>(TwiceExtZ);
        Vector Place;
        Place.x = static_cast<float>(static_cast<double>(OriginX) + U * static_cast<double>(FrameX));
        Place.y = static_cast<float>(static_cast<double>(OriginY) +
                                      (1.0 - V) * static_cast<double>(FrameY));
        Place.z = static_cast<float>(static_cast<double>(OriginZ) + static_cast<double>(FrameZ));

        // The size -- the markers (Kind 2 / 3 / 4) shrink with the zoom
        // (during a zoom from the start distance to the end one), the rest follow SystemScale.
        double Size = static_cast<double>(Item->Size);
        if (Kind == kNavMapItemPlayer || Kind == kNavMapItemWaypoint || Kind == kNavMapItemBestPath)
        {
            if (bTransition != 0 && bViewSwitch == 0)
            {
                double HalfFrom = static_cast<double>(MapExtentX) /
                                  static_cast<double>(ZoomDistance) * 0.5;
                if (HalfFrom < 1.0)
                    HalfFrom = 1.0;
                double const SizeFrom = Size / HalfFrom;
                double HalfTo = static_cast<double>(MapExtentX) /
                                (static_cast<double>(TransitionDelta[2]) +
                                 static_cast<double>(ZoomDistance)) *
                                0.5;
                if (HalfTo < 1.0)
                    HalfTo = 1.0;
                Size = (Size / HalfTo - SizeFrom) * static_cast<double>(TransitionEase) + SizeFrom;
            }
            else
            {
                double Half = Zoom * 0.5;
                if (Half < 1.0)
                    Half = 1.0;
                Size = Size / Half;
            }
        }
        else
        {
            Size *= static_cast<double>(SystemScale);
        }
        Size = Size / static_cast<double>(TwiceExtZ) * static_cast<double>(FrameX);
        if ((Type & OBJ_SUN) != 0)
            Size *= static_cast<double>(kNavMapSystemMeshScale);
        float const MeshScale = static_cast<float>(Size * static_cast<double>(kNavMapMeshScale));
        Place.z = kNavMapItemDepth; // overwrites the frame z computed above, as retail does

        // The rotation about z by Heading (cos of the unrounded angle, sin
        // of the float one), scaled through I3DMathEngine::Scale, written back; then the
        // position and UpdateInstance.
        double const Radians = static_cast<double>(Item->Heading) *
                               static_cast<double>(kNavMapDegToRad);
        float const RadiansF = static_cast<float>(Radians);
        Transform Xform;
        memcpy(&Xform, Engine->GetInstanceTransform(Item->Mesh.EngineInstance),
               sizeof(Xform));
        double const Cos = cos(Radians);
        double const Sin = sin(static_cast<double>(RadiansF));
        Xform.Rotation.E[0][2] = 0.0f;
        Xform.Rotation.E[1][2] = 0.0f;
        Xform.Rotation.E[2][0] = 0.0f;
        Xform.Rotation.E[2][1] = 0.0f;
        Xform.Rotation.E[2][2] = 1.0f;
        Xform.Rotation.E[0][0] = static_cast<float>(Cos);
        Xform.Rotation.E[0][1] = static_cast<float>(-Sin);
        Xform.Rotation.E[1][0] = static_cast<float>(Sin);
        Xform.Rotation.E[1][1] = static_cast<float>(Cos);
        if (G_NavMapMath == nullptr)
            G_NavMapMath = Acquire3DMathEngine(); // FUN_005b73e0
        G_NavMapMath->Scale(Xform.Rotation, Xform.Rotation, MeshScale);
        Engine->SetInstanceMatrix(Item->Mesh.EngineInstance, &Xform);
        Engine->SetInstancePosition(Item->Mesh.EngineInstance, &Place);
        Engine->UpdateInstance(Item->Mesh.EngineInstance, 0, nullptr);

        // An object with an id takes the visit map's flags; bit 0 shows it
        // (or bDebugShowAll every one).
        if (Item->Id != 0)
        {
            unsigned char VisitFlags = 0;
            GetPlayerVisitFlagsRetail(Item->Id, &VisitFlags, 0, 0); // FUN_004c4da0
            Item->Visit = static_cast<unsigned char>(Item->Visit | VisitFlags);
            Item->bShown = static_cast<unsigned char>(Item->Visit & kVisitShown);
            if (bDebugShowAll != 0)
                Item->bShown = 1;
        }
    }
}

// ----------------------------------------------------------------------------------------
// Update (slot 11)
// ----------------------------------------------------------------------------------------

namespace
{
    // Update's text colour writes (inlined several times): DAT_00679bb0 with
    // Alpha as its top byte, property 0x33 with bFlag, on the system name and the four faction
    // labels, then -- from a second copy of the same colour -- on the sixteen grid labels.
    void NavMapTintTexts(NeuroNetNavMap* Map, unsigned char Alpha, uintptr_t bFlag)
    {
        unsigned long Colour = G_NnPaletteCyan;
        reinterpret_cast<unsigned char*>(&Colour)[3] = Alpha;
        Map->SystemNameText->SetProperty(kWinPropColour, reinterpret_cast<uintptr_t>(&Colour), bFlag);
        for (int Index = 0; Index < 4; ++Index)
            Map->FactionLabels[Index]->SetProperty(kWinPropColour, reinterpret_cast<uintptr_t>(&Colour),
                                                   bFlag);
        unsigned long GridColour = G_NnPaletteCyan;
        reinterpret_cast<unsigned char*>(&GridColour)[3] = Alpha;
        for (int Index = 0; Index < 8; ++Index)
            Map->GridLetters[Index]->SetProperty(kWinPropColour, reinterpret_cast<uintptr_t>(&GridColour),
                                                 bFlag);
        for (int Index = 0; Index < 8; ++Index)
            Map->GridNumbers[Index]->SetProperty(kWinPropColour, reinterpret_cast<uintptr_t>(&GridColour),
                                                 bFlag);
    }

    // DAT_00679bb0's own alpha byte (the colour copied unchanged at the end of a switch).
    unsigned char NavMapPaletteAlpha()
    {
        return reinterpret_cast<unsigned char const*>(&G_NnPaletteCyan)[3];
    }

    // The fade's alpha: ftol((1 - Ease) * the palette's alpha byte).
    unsigned char NavMapFadeAlpha(float Ease)
    {
        int const PaletteAlpha = NavMapPaletteAlpha();
        return static_cast<unsigned char>(
            static_cast<int>((1.0 - static_cast<double>(Ease)) * static_cast<double>(PaletteAlpha)));
    }
}

void NeuroNetNavMap::Update()
{
    // LAB_00494920. Retail keeps the hover record(s) below in an SEH frame; the
    // port tears them down explicitly on its only (non-throwing) path.
    Win3D::Update();       // FUN_005a17b0
    ButtonMover::Update(); // FUN_0055e170 (+0x330)
    SyncSidePanels();      // FUN_004a1620

    // The ship button shows unless the player's own system is open (not
    // the universe) or a transition runs; SetProperty(1, hide) only on a change of the visible
    // bit.
    unsigned const ShownSystem = CurrentSystem->GetId();
    unsigned char bShipButton = 1;
    if (ShownSystem == GetPlayerStarSystemIdRetail() && ViewMode != kNavMapViewUniverse)
        bShipButton = 0;
    if (bTransition != 0)
        bShipButton = 0;
    if (bShipButton != static_cast<unsigned char>((ShipButton->Flags >> 1) & 1))
        ShipButton->SetProperty(kWinPropHide, bShipButton == 0 ? 1u : 0u, 0);

    // The object tools close once the cursor moved away from where they opened (squared
    // distance strictly over 0.005; unordered does not close).
    if (bObjectToolsShown != 0)
    {
        Vector Ray;
        if (CursorRayPoint(&Ray))
        {
            float const Dx = static_cast<float>(static_cast<double>(Ray.x) -
                                                static_cast<double>(ClickRayPos.x));
            float const Dy = static_cast<float>(static_cast<double>(Ray.y) -
                                                static_cast<double>(ClickRayPos.y));
            Ray.z = static_cast<float>(static_cast<double>(Ray.z) -
                                        static_cast<double>(ClickRayPos.z));
            if (static_cast<double>(Dy) * static_cast<double>(Dy) +
                    static_cast<double>(Dx) * static_cast<double>(Dx) >
                static_cast<double>(kNavMapToolsCloseDist2))
            {
                bObjectToolsShown = 0;
                HideObjectTools(); // FUN_0049f0e0
            }
        }
    }

    // The map frame's corners from MapOrigin / MapSize.
    MapCorner0[0] = MapOrigin[0];
    MapCorner0[1] = MapOrigin[1];
    MapCorner0[2] = MapOrigin[2];
    MapCorner1[0] = MapOrigin[0];
    MapCorner1[1] = MapOrigin[1];
    MapCorner1[2] = MapOrigin[2];
    MapCorner1[0] = static_cast<float>(static_cast<double>(MapSize[0]) +
                                       static_cast<double>(MapCorner1[0]));
    MapCorner2[0] = MapOrigin[0];
    MapCorner2[1] = MapOrigin[1];
    MapCorner2[2] = MapOrigin[2];
    {
        // FUN_00422b80: the Vector built from the three rounded sums, then copied in.
        float const X = static_cast<float>(static_cast<double>(MapCorner2[0]) +
                                           static_cast<double>(MapSize[0]));
        float const Y = static_cast<float>(static_cast<double>(MapCorner2[1]) +
                                           static_cast<double>(MapSize[1]));
        float const Z = static_cast<float>(static_cast<double>(MapCorner2[2]) +
                                           static_cast<double>(MapSize[2]));
        MapCorner2[0] = X;
        MapCorner2[1] = Y;
        MapCorner2[2] = Z;
    }
    MapCorner3[0] = MapOrigin[0];
    MapCorner3[1] = MapOrigin[1];
    MapCorner3[2] = MapOrigin[2];
    MapCorner3[1] = static_cast<float>(static_cast<double>(MapSize[1]) +
                                       static_cast<double>(MapCorner3[1]));

    // The first frame after Draw builds the system view; a requested focus
    // point (FUN_004a2aa0) is scaled into the map and zoomed on.
    if (bSystemViewPending != 0 && bDrawnOnce != 0)
    {
        bSystemViewPending = 0;
        BuildSystemItems(); // FUN_004917c0
        AddPlayerMarker();  // FUN_00491410
        PlaceItems();       // FUN_00496600
        if (G_bNavMapRequestedPos != 0)
        {
            G_bNavMapRequestedPos = 0;
            double const Scale = static_cast<double>(SystemScale);
            G_NavMapRequestedPos[0] =
                static_cast<float>(static_cast<double>(G_NavMapRequestedPos[0]) * Scale);
            G_NavMapRequestedPos[1] =
                static_cast<float>(static_cast<double>(G_NavMapRequestedPos[1]) * Scale);
            G_NavMapRequestedPos[2] =
                static_cast<float>(Scale * static_cast<double>(G_NavMapRequestedPos[2]));
            ClickU = static_cast<float>(
                (static_cast<double>(G_NavMapRequestedPos[0]) + static_cast<double>(MapExtentX)) /
                (static_cast<double>(MapExtentX) + static_cast<double>(MapExtentX)));
            ClickV = static_cast<float>(
                (static_cast<double>(G_NavMapRequestedPos[2]) + static_cast<double>(MapExtentZ)) /
                (static_cast<double>(MapExtentZ) + static_cast<double>(MapExtentZ)));
            ZoomToClick(); // FUN_0049f1a0
        }
    }

    // A pending system (set by MouseEvent / SetProperty) once no transition
    // runs.
    if (bTransition == 0 && PendingSystem != nullptr)
    {
        Universe::ISystem const* const Pending = PendingSystem;
        if (Pending == CurrentSystem && (ViewMode == kNavMapViewSystemZoomed || ViewMode == kNavMapViewSystem))
        {
            // It is open -- done.
            PendingSystem = nullptr;
        }
        else if (ViewMode != kNavMapViewSystem && ViewMode != kNavMapViewSystemZoomed)
        {
            // The universe view zooms into its star first (sound 8); no
            // star, no switch.
            for (std::list<FNavMapItem*>::iterator It = Items.begin(); It != Items.end(); ++It)
            {
                FNavMapItem* const Item = *It;
                if (Item->System != Pending)
                    continue;
                CurrentSystem = Item->System;
                bViewSwitch = 1;
                bTransition = 1;
                TransitionView = kNavMapViewSystem;
                TransitionTime = 0.0f;
                TransitionDuration = kNavMapSwitchSeconds;
                PlayOrRewindUiSoundSlot(kUiSoundNavMapFade); // FUN_005645c0(8)
                break;
            }
        }
        else
        {
            // From a system straight to the other one.
            SetFilterMode(kNavMapFilterPhysical); // FUN_004a0220
            CurrentSystem = PendingSystem;
            NavMapShowSystemName(SystemNameText, CurrentSystem, SkipSystemIds);
            ZoomScale = 1.0f;
            bViewSwitch = 0;
            bTransition = 0;
            EnterSystemView(); // FUN_0049e1d0
            TransitionView = ViewMode;
        }
    }

    unsigned char bRebuild = 0;
    float const FrameTime = static_cast<float>(GetClientFrameTimeCapped()); // FUN_0042d680

    // The hover tip -- skipped when the ToolTip was fed this frame or the
    // info card / base list / known-objects filter mode (4 / 6 / 7) is up.
    if (!NnToolTipWasFedThisFrame() && G_NavMapFilterMode != kNavMapFilterLegend && G_NavMapFilterMode != kNavMapFilterInfoCard &&
        G_NavMapFilterMode != kNavMapFilterBaseList)
    {
        Vector MapPos;
        if (MapPointUnderCursor(&MapPos))
        {
            FNavMapItem* const Item = ItemAt(&MapPos, 0);
            if (Item != nullptr)
            {
                FToolTipHoverInfo Record;
                ToolTipHoverInit(&Record); // FUN_004144b0 + FUN_00413df0(0, -1), -1 / 0 / 0
                if (Item->Kind == kNavMapItemBestPath && Item->TooltipText.c_str()[0] != L'\0')
                {
                    // A best-path marker with its own text (wcscmp against
                    // L"" at DAT_005c8ce0) shows that text, always fed.
                    ToolTipHoverSetCaption(&Record, Item->TooltipText.c_str());
                    NavMapPlaceHoverAtCursor(&Record);
                    NavMapSubmitHover(&Record);
                }
                else
                {
                    // The item's name, keyed by the item; fed only with a
                    // name.
                    Record.IdsAlt = Item->StridName;
                    Record.Ids = static_cast<int>(reinterpret_cast<intptr_t>(Item));
                    NavMapPlaceHoverAtCursor(&Record);
                    if (Record.IdsAlt != 0)
                        NavMapSubmitHover(&Record);
                }
                // The record's dtor: ~FToolTipHoverInfo.
            }
            else if (ViewMode != kNavMapViewUniverse)
            {
                int Rep = GroupMemberAt(&MapPos); // FUN_004a21f0
                if (Rep != -1)
                {
                    // A group member -- named (Reputation::Vibe::GetName's
                    // display name) unless it has an affiliation.
                    FToolTipHoverInfo Record;
                    ToolTipHoverInit(&Record);
                    // Only read when GetAffiliation returns 0 (it then wrote it).
                    unsigned Affiliation = 0;
                    if (Reputation::Vibe::GetAffiliation(Rep, Affiliation, false) != 0 ||
                        Affiliation == kNoAffiliation)
                    {
                        FmtStr Name1; // [esp+0xd4]
                        FmtStr Name2; // [esp+0xbc]
                        wchar_t const* Display = nullptr;
                        if (Reputation::Vibe::GetName(Rep, Name1, Name2, Display) == 0)
                        {
                            if (Display != nullptr)
                            {
                                ToolTipHoverSetCaption(&Record, Display);
                            }
                            else if (Record.CaptionW != nullptr && Record.CaptionW[0] != L'\0')
                            {
                                Record.CaptionW[0] = L'\0'; // FUN_00414280
                            }
                            Record.Ids = Rep + 1;
                            NavMapPlaceHoverAtCursor(&Record);
                            NavMapSubmitHover(&Record);
                        }
                    }
                    // The record's dtor: ~FToolTipHoverInfo.
                }
                else
                {
                    // The smallest named, visited zone under the cursor
                    // (map units back to system units).
                    float const InvScale =
                        static_cast<float>(1.0 / static_cast<double>(SystemScale));
                    float Pos[3];
                    Pos[0] = static_cast<float>(static_cast<double>(MapPos.x) *
                                                static_cast<double>(InvScale));
                    Pos[1] = static_cast<float>(static_cast<double>(InvScale) *
                                                static_cast<double>(MapPos.y));
                    Pos[2] = static_cast<float>(static_cast<double>(InvScale) *
                                                static_cast<double>(MapPos.z));
                    Universe::IZone const* const Zone =
                        NavMapZoneAt(CurrentSystem, Pos, 1, 0, bDebugShowAll); // FUN_004a1b50
                    if (Zone != nullptr)
                    {
                        FToolTipHoverInfo Record;
                        ToolTipHoverInit(&Record);
                        Record.IdsAlt = Zone->GetName(); // IZone +0x84
                        Record.Ids = static_cast<int>(reinterpret_cast<intptr_t>(Zone));
                        NavMapPlaceHoverAtCursor(&Record);
                        if (Record.IdsAlt != 0)
                            NavMapSubmitHover(&Record);
                    }
                }
            }
        }
    }

    // The drag. A press (MouseEvent) arms it; with the left button still
    // down and no transition it starts after more than 4 px (Manhattan) and pans the map by
    // twice the cursor's move over the frame's pixel size, clamped to half MapSize.
    if (bMouseDown != 0)
    {
        int MouseX = 0;
        int MouseY = 0;
        GetClientSoftwareCursor(&MouseX, &MouseY, nullptr);
        if (bTransition == 0 && (GetClientMouseButtons() & 1) != 0)
        {
            if (bDragging == 0 && abs(DragMouseY - MouseY) + abs(DragMouseX - MouseX) > 4)
                bDragging = 1;
        }
        else
        {
            bMouseDown = 0;
            bDragging = 0;
        }
        if (bDragging != 0)
        {
            double const MoveX = static_cast<double>(MouseX - DragMouseX) * 2.0 /
                                 static_cast<double>(ScreenRectSize[0]);
            double const MoveY = static_cast<double>(DragMouseY - MouseY) * 2.0 /
                                 static_cast<double>(ScreenRectSize[1]);
            float const PanScale = static_cast<float>(
                static_cast<double>(MapSize[0]) /
                (static_cast<double>(ZoomScale) + static_cast<double>(ZoomScale)));
            float const PanX = static_cast<float>(MoveX * static_cast<double>(PanScale));
            double const PanY = MoveY * static_cast<double>(PanScale);
            double const PanZ = static_cast<double>(PanScale) * 0.0;
            PanOffset[0] = static_cast<float>(static_cast<double>(PanX) +
                                              static_cast<double>(PanOffset[0]));
            PanOffset[1] = static_cast<float>(PanY + static_cast<double>(PanOffset[1]));
            PanOffset[2] = static_cast<float>(PanZ + static_cast<double>(PanOffset[2]));

            // Each bound only when strictly passed;
            // the upper one is tested only when the lower one was not.
            float const HalfX = static_cast<float>(static_cast<double>(MapSize[0]) * 0.5);
            float const HalfY = static_cast<float>(static_cast<double>(MapSize[1]) * 0.5);
            if (-HalfX > PanOffset[0])
                PanOffset[0] = -HalfX;
            else if (PanOffset[0] > HalfX)
                PanOffset[0] = HalfX;
            if (-HalfY > PanOffset[1])
                PanOffset[1] = -HalfY;
            else if (PanOffset[1] > HalfY)
                PanOffset[1] = HalfY;

            SetMapMeshTransform(ViewMode, 1.0f); // FUN_004963d0
            DragMouseX = MouseX;
            DragMouseY = MouseY;
            bRebuild = 1;
            PlaceGridLabels(); // FUN_0049fbf0
        }
    }

    // ScreenRect -- the frame's two corners (with G_NavMapScreenRectOffset's
    // offset, at z -1.25) through the Win3D camera, floored; then this map's own camera takes
    // the Win3D camera's near / far / half fov and ScreenRect as its viewport.
    FSpaceCamera* const SceneCamera = Win3DGetCamera(); // DAT_0067dbf8
    Vector Corner;
    Corner.x = static_cast<float>(static_cast<double>(G_NavMapScreenRectOffset[0]) +
                                   static_cast<double>(MapCorner0[0]));
    Corner.y = static_cast<float>(static_cast<double>(G_NavMapScreenRectOffset[1]) +
                                   static_cast<double>(MapCorner2[1]));
    Corner.z = -1.25f;
    AsCamera(SceneCamera)->Project(&ScreenRectMin[0], &ScreenRectMin[1], &ScreenRectMin[2],
                                   &Corner);
    ScreenRectMin[0] = static_cast<float>(floor(static_cast<double>(ScreenRectMin[0])));
    ScreenRectMin[1] = static_cast<float>(floor(static_cast<double>(ScreenRectMin[1])));
    Corner.x = static_cast<float>(static_cast<double>(G_NavMapScreenRectOffset[0]) +
                                   static_cast<double>(MapCorner2[0]));
    Corner.y = static_cast<float>(static_cast<double>(G_NavMapScreenRectOffset[1]) +
                                   static_cast<double>(MapCorner0[1]));
    float Far[3];
    AsCamera(SceneCamera)->Project(&Far[0], &Far[1], &Far[2], &Corner);
    Far[0] = static_cast<float>(floor(static_cast<double>(Far[0])));
    Far[1] = static_cast<float>(floor(static_cast<double>(Far[1])));
    ScreenRectSize[0] = static_cast<float>(static_cast<double>(Far[0]) -
                                           static_cast<double>(ScreenRectMin[0]));
    ScreenRectSize[1] = static_cast<float>(static_cast<double>(Far[1]) -
                                           static_cast<double>(ScreenRectMin[1]));
    ScreenRectSize[2] = static_cast<float>(static_cast<double>(Far[2]) -
                                           static_cast<double>(ScreenRectMin[2]));
    int Viewport[4];
    Viewport[0] = static_cast<int>(ScreenRectMin[0]); // ftol
    Viewport[1] = static_cast<int>(ScreenRectMin[1]);
    Viewport[2] = static_cast<int>(static_cast<double>(ScreenRectSize[0]) +
                                   static_cast<double>(ScreenRectMin[0]));
    Viewport[3] = static_cast<int>(static_cast<double>(ScreenRectSize[1]) +
                                   static_cast<double>(ScreenRectMin[1]));
    Camera.ZNear = SceneCamera->ZNear; // FUN_0040fb60: the two stores
    Camera.ZFar = SceneCamera->ZFar;
    SpaceCameraSetHalfFovX(&Camera, SceneCamera->HalfFovXDeg); // FUN_0040f8c0
    SpaceCameraSetViewportEx(&Camera, Viewport, 1);           // FUN_0040fb80

    // The system view follows the player (marker position / heading) and
    // the waypoints; the universe view pulses its stars.
    if (ViewMode != kNavMapViewUniverse)
    {
        if (PlayerItem != nullptr)
        {
            Vector Pos;
            float Heading = 0.0f;
            PlayerMapPosition(&Pos, &Heading); // FUN_00491be0
            // x87: any difference (or unordered) moves the marker.
            if (!(Pos.x == PlayerItem->Position.x) || !(Pos.y == PlayerItem->Position.y) ||
                !(Pos.z == PlayerItem->Position.z) || !(PlayerItem->Heading == Heading))
            {
                PlayerItem->Position.x = Pos.x;
                PlayerItem->Position.y = Pos.y;
                PlayerItem->Position.z = Pos.z;
                PlayerItem->Heading = Heading;
                bRebuild = 1;
            }
        }
        if (RefreshWaypointItems()) // FUN_00496e50
            bRebuild = 1;
    }
    else
    {
        AnimateItems(); // FUN_004a1710
        bRebuild = 1;
    }

    // The blink flag toggles once per whole second.
    {
        double Blink = GetClientFrameTimeCapped() + static_cast<double>(BlinkTimer);
        BlinkTimer = static_cast<float>(Blink);
        if (Blink > 1.0)
        {
            unsigned char bNewBlink = bBlink;
            do
            {
                Blink -= 1.0;
                bNewBlink = bNewBlink == 0 ? 1 : 0;
            } while (Blink > 1.0);
            BlinkTimer = static_cast<float>(Blink);
            bBlink = bNewBlink;
        }
    }

    // The transition.
    if (bTransition != 0)
    {
        double const Time = static_cast<double>(FrameTime) + static_cast<double>(TransitionTime);
        TransitionTime = static_cast<float>(Time);
        // x87: still running while less (or unordered).
        if (Time >= static_cast<double>(TransitionDuration))
        {
            // It ended.
            int const View = TransitionView;
            TransitionTime = TransitionDuration;
            bTransition = 0;
            if (View == kNavMapViewUniverse)
            {
                bViewSwitch = 0;
                if (ViewMode == kNavMapViewUniverse)
                {
                    // Back on the universe -- the texts opaque again.
                    NavMapTintTexts(this, NavMapPaletteAlpha(), 0);
                    RefreshMissionInfo(); // FUN_0049a580
                    return;
                }
                // Out of a system -- the universe view, faded back in.
                ZoomScale = 1.0f;
                EnterUniverseView(); // FUN_0049e550
                bViewSwitch = 1;
                bTransition = 1;
                TransitionView = kNavMapViewUniverse;
                TransitionTime = 0.0f;
                TransitionDuration = kNavMapSwitchSeconds;
                return;
            }
            if (View == kNavMapViewSystem && ViewMode != kNavMapViewSystemZoomed)
            {
                bViewSwitch = 0;
                if (ViewMode == kNavMapViewSystem)
                {
                    NavMapTintTexts(this, NavMapPaletteAlpha(), 0);
                    RefreshMissionInfo();
                    return;
                }
                // Into the system -- its view, its name, faded back in.
                EnterSystemView(); // FUN_0049e1d0
                NavMapShowSystemName(SystemNameText, CurrentSystem, SkipSystemIds);
                bViewSwitch = 1;
                bTransition = 1;
                TransitionView = kNavMapViewSystem;
                TransitionTime = 0.0f;
                TransitionDuration = kNavMapSwitchSeconds;
                return;
            }
            // A zoom ended -- the view is the target one; both zoomed map
            // meshes opaque.
            ViewMode = View;
            ZoomedMapMesh.UserFloats[0] = 1.0f;
            ZoomedMapMesh.UserFloats[1] = 1.0f;
            ZoomedMapMesh.UserFloats[2] = 1.0f;
            ZoomedMapMesh.UserFloats[4] = 1.0f;
            ZoomedMapMesh2.UserFloats[0] = 1.0f;
            ZoomedMapMesh2.UserFloats[1] = 1.0f;
            ZoomedMapMesh2.UserFloats[2] = 1.0f;
            ZoomedMapMesh2.UserFloats[4] = 1.0f;
        }

        // Ease = 1 - (cos(pi t / T) + 1) / 2.
        float const Ease = static_cast<float>(
            1.0 - (cos(static_cast<double>(TransitionTime) /
                       static_cast<double>(TransitionDuration) * static_cast<double>(kNavMapPi)) +
                   1.0) *
                      0.5);
        TransitionEase = Ease;
        if (bViewSwitch != 0)
        {
            // A view switch fades the texts (out, then -- on the way into
            // the target view -- back in); nothing moves.
            if (TransitionView == ViewMode)
                TransitionEase = static_cast<float>(1.0 - static_cast<double>(Ease));
            NavMapTintTexts(this, NavMapFadeAlpha(TransitionEase), 1);
        }
        else
        {
            // A zoom pans / scales towards the target.
            PanOffset[0] = static_cast<float>(static_cast<double>(Ease) *
                                              static_cast<double>(TransitionDelta[0]));
            PanOffset[1] = static_cast<float>(static_cast<double>(Ease) *
                                              static_cast<double>(TransitionDelta[1]));
            PanOffset[2] = static_cast<float>(static_cast<double>(Ease) *
                                              static_cast<double>(TransitionDelta[2]));
            bRebuild = 1;
            double const Distance =
                static_cast<double>(ZoomDistance) + static_cast<double>(PanOffset[2]);
            PanOffset[2] = 0.0f;
            ZoomScale = static_cast<float>(static_cast<double>(MapExtentX) / Distance);
            PanOffset[0] = static_cast<float>(static_cast<double>(PanOffset[0]) +
                                              static_cast<double>(TransitionStart[0]));
            PanOffset[1] = static_cast<float>(static_cast<double>(PanOffset[1]) +
                                              static_cast<double>(TransitionStart[1]));
            PanOffset[2] = static_cast<float>(static_cast<double>(PanOffset[2]) +
                                              static_cast<double>(TransitionStart[2]));
            if (TransitionView != kNavMapViewUniverse)
            {
                if (ViewMode == kNavMapViewSystemZoomed)
                {
                    SetMapMeshTransform(ViewMode, static_cast<float>(1.0 - static_cast<double>(Ease)));
                    SetMapMeshTransform(TransitionView, Ease);
                }
                else if (ViewMode == kNavMapViewSystem)
                {
                    SetMapMeshTransform(TransitionView, Ease);
                    SetMapMeshTransform(ViewMode, 1.0f);
                }
            }
            PlaceGridLabels(); // FUN_0049fbf0
        }
    }

    // IClient slot 93's "no best path" answer shows NoBestPathText for 8 s.
    if (G_bNavMapNoBestPath != 0)
    {
        G_bNavMapNoBestPath = 0;
        NoBestPathText->SetProperty(kWinPropHide, 0, 0);
        NoBestPathTimer = kNavMapNoBestPathSeconds;
    }
    if (NoBestPathTimer > 0.0f)
    {
        double const Left = static_cast<double>(NoBestPathTimer) - GetClientFrameTimeCapped();
        NoBestPathTimer = static_cast<float>(Left);
        // x87: hidden once less or equal (not while greater or unordered).
        if (Left <= 0.0)
        {
            NoBestPathText->SetProperty(kWinPropHide, 1, 0);
            NoBestPathTimer = 0.0f;
        }
    }

    // In multiplayer the group's positions are asked for every 30 s
    // (IServer +0x12c with one zero dword).
    {
        double const Poll = GetClientFrameTimeCapped() + static_cast<double>(GroupPollTimer);
        GroupPollTimer = static_cast<float>(Poll);
        if (Poll > static_cast<double>(kNavMapGroupPollSeconds))
        {
            GroupPollTimer =
                static_cast<float>(Poll - static_cast<double>(kNavMapGroupPollSeconds));
            if (!SinglePlayer())
            {
                unsigned Zero = 0;
                G_Server->RequestGroupPositions(GetPlayerConnectionId(),
                                                reinterpret_cast<unsigned char*>(&Zero), 4);
            }
        }
    }

    if (bRebuild != 0)
        PlaceItems();
}

