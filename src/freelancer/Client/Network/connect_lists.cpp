// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Network/connect_lists.h"

#include "NeuroNet/Economy/best_path.h"              // G_BestPaths -- DAT_0067294c
#include "Interface/Pages/character_select_equip.h" // the player-state lists (DAT_00672960 / DAT_0067296c / ...)
#include "common_goods.h"           // EquipDescList
#include "dacom.h"                  // FDUMP
#include "NeuroNet/nn_story_star.h"          // StoryStarReset (FUN_004b9bb0), StoryStarSetRankUp (FUN_004b9b20)
#include "Space/player_spawn.h"           // ResetPlayerWaypointsRetail (FUN_004c46c0)

#include <list>

namespace
{
    // The steps FUN_004c4a80 and FUN_004c4bc0 share, in retail order.
    void ClearPlayerConnectLists()
    {
        GetPlayerCollisionGroupListRetail().clear(); // DAT_0067296c
        // DAT_00672960; neither revision counter is touched.
        GetPlayerEquipDescListRetail().Items.clear();
        G_BestPaths.clear();                         // DAT_0067294c (FUN_004c5a80)
        ClearPlayerVisitMapRetail();                 // DAT_00673320
        StoryStarReset();
        StoryStarSetRankUp(0);
    }
}

void PreConnectReset()
{
    // FUN_004c4a80.
    ClearPlayerShipArchAndHealthRetail(); // DAT_0067337c, DAT_00673380
    ResetPlayerWaypointsRetail();         // FUN_004c46c0 inlined
    ClearPlayerConnectLists();
}

void TeardownConnectLists()
{
    // FUN_004c4bc0: FUN_004c4a80 without the two stores and the waypoint reset.
    ClearPlayerConnectLists();
}
