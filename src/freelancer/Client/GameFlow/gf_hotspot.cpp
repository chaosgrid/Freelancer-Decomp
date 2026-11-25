// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// supt\Hotspot.cpp. See gf_hotspot.h.

#include "GameFlow/gf_hotspot.h"

#include "GameFlow/gameflow.h"     // ClearHoveredHotspotIf (FUN_00558de0)
#include "GameFlow/room_control.h" // FRoomObject slots 10..12

#include "dacom.h"        // FDUMP

namespace
{
    char const kHotspotCpp[] = "E:\\FL\\Scratch\\Source\\Client\\supt\\Hotspot.cpp";

    // DAT_006799a4 -- LAB_00555d00 (static initializer) stores .rdata DAT_005e1210 (0.6f) here;
    // nothing in the PE reads it.
    float G_Hotspot6799a4 = 0.6f;

    // DAT_00679998 / DAT_0067999c / DAT_006799a0 -- LAB_00555d10 builds the empty list and
    // registers LAB_00555d50 with atexit, which frees every node (not the hotspots) and the
    // head. The std::list's own construction and destruction are that pair.
    std::list<FHotspot*> G_Hotspots;
}

std::list<FHotspot*>& GetHotspotList()
{
    return G_Hotspots;
}

FHotspot::FHotspot()
    : Owner(nullptr)
    , Cookie(0xffffffffu)
    , bEnabled(0)
    , bHovered(0)
{
    // FUN_00555db0. Retail zeroes the rectangle twice.
    Pad0E[0] = 0;
    Pad0E[1] = 0;
    ScreenRect.Left = 0;
    ScreenRect.Top = 0;
    ScreenRect.Width = 0;
    ScreenRect.Height = 0;
    G_Hotspots.push_back(this);
}

FHotspot::~FHotspot()
{
    // FUN_00555ea0: the tracker lets go of this hotspot first (FUN_00558de0).
    ClearHoveredHotspotIf(this);

    // The first node holding this leaves the list.
    for (std::list<FHotspot*>::iterator It = G_Hotspots.begin(); It != G_Hotspots.end(); ++It)
    {
        if (*It == this)
        {
            G_Hotspots.erase(It);
            break;
        }
    }
}

void FHotspot::SendEvent(int Event, int const* Point)
{
    // FUN_00555f00.
    if (Owner == nullptr)
        return;
    if (Event == kHotspotEventEnter)
        Owner->OnHotspotEnter(Cookie, Point);
    else if (Event == kHotspotEventLeave)
        Owner->OnHotspotLeave(Cookie, Point);
    else if (Event == kHotspotEventClick)
        Owner->OnHotspotClick(Cookie, Point);
}

bool FindHotspotAt(int const* Point, FHotspot** OutHotspot)
{
    // FUN_00556090.
    FHotspot* Best = nullptr;
    float BestScore = 0.0f;

    for (std::list<FHotspot*>::iterator It = G_Hotspots.begin(); It != G_Hotspots.end(); ++It)
    {
        FHotspot* const Hotspot = *It;
        if (!Hotspot->bEnabled)
            continue;
        if (!Hotspot->ScreenRect.point_is_inside(Point[0], Point[1]))
            continue;

        // Rect::get_center on a copy of the rectangle.
        Rect const Copy = Hotspot->ScreenRect;
        int CenterX = 0;
        int CenterY = 0;
        Copy.get_center(&CenterX, &CenterY);

        // 1.0f is DAT_005c75dc. The division is by the integer width / height on the x87 stack
        // (FIDIV), unrounded.
        double Score = 1.0;
        if (Copy.Width > 0 && Copy.Height > 0)
        {
            double const Dx = static_cast<double>(Point[0] - CenterX) /
                              static_cast<double>(Copy.Width);
            double const Dy = static_cast<double>(Point[1] - CenterY) /
                              static_cast<double>(Copy.Height);
            Score = Dy * Dy + Dx * Dx;
        }

        // The unrounded score against the stored float: taken only when strictly less (not on
        // equal, not on a NaN).
        if (Best == nullptr || Score < static_cast<double>(BestScore))
        {
            BestScore = static_cast<float>(Score);
            Best = Hotspot;
        }
    }

    if (Best != nullptr)
    {
        *OutHotspot = Best;
        return true;
    }
    *OutHotspot = nullptr;
    return false;
}

bool ReportLeakedHotspots()
{
    // FUN_00556200.
    for (std::list<FHotspot*>::iterator It = G_Hotspots.begin(); It != G_Hotspots.end(); ++It)
    {
        FHotspot* const Hotspot = *It;
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: hotspot = %p id = %d not freed\n", kHotspotCpp,
              0x161, Hotspot, Hotspot->Cookie);
    }
    return G_Hotspots.size() == 0;
}
