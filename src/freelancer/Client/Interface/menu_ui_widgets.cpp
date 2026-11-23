// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/menu_ui_widgets.h"

#include "Interface/Pages/character_select_equip.h" // SetPlayerRankLevelRetail
#include "common_server.h"          // IServer::RequestRankLevel (slot 78)
#include "Network/login_mgr.h"              // G_Server (DAT_0067ecd0)
#include "Space/player_spawn.h"           // GetPlayerConnectionId (DAT_00673344)
#include "Interface/win_factory.h"            // WinFactoryCreate

#include <vector>

// FUN_004c55f0 -- the rank level becomes -1 (pending) and the server is asked for it again;
// NN_ShipTrader::Update notices the change.
void HudMessageQueueRequestRankRefresh()
{
    // No null test on G_Server.
    SetPlayerRankLevelRetail(-1);
    unsigned char Data[4] = {0, 0, 0, 0};
    G_Server->RequestRankLevel(GetPlayerConnectionId(), Data, 4);
}

namespace
{
    // A std::vector<unsigned>-shaped object (DAT_006728cc..d4) behind a leading flag byte
    // (DAT_006728c8); static init LAB_004c5630, atexit LAB_004c5660. Read by HUD_ContactList and
    // HUD_Target through size() + at(); IClient slot 120 clears and fills it.
    struct FRetailU32QueueStub
    {
        bool bFlag = false;
        std::vector<unsigned> Items;
    };
    FRetailU32QueueStub GRetailU32Queue;
}

// FUN_004c5680 -- clear() without releasing storage.
void RetailU32QueueClear()
{
    GRetailU32Queue.Items.clear();
}

// FUN_004c56b0.
void RetailU32QueuePushBack(unsigned Value)
{
    GRetailU32Queue.Items.push_back(Value);
}

// FUN_004c5890.
unsigned RetailU32QueueSize()
{
    return static_cast<unsigned>(GRetailU32Queue.Items.size());
}

// FUN_004c58b0 -- out of range gives 0. A negative int converts to an unsigned above any size,
// so the one unsigned compare covers retail's two tests.
unsigned RetailU32QueueAt(unsigned Index)
{
    if (Index >= GRetailU32Queue.Items.size())
        return 0;
    return GRetailU32Queue.Items[Index];
}

// FUN_004c5940 is the exe's copy of BaseWatcher::BaseWatcher(IObjRW*); its one caller (static
// init LAB_004c5060) is best_path.cpp's G_BestPathBaseWatcher.

void MenuUiInitToolTipAndRollover()
{
    // Called from the client pre-init. Both use the nickname as the class name; retail ignores
    // the return values. WINFACTORY<ToolTip> is in nn_tooltip.cpp, WINFACTORY<Rollover> in
    // nn_rollover.cpp.
    WinFactoryCreate("ToolTip", "ToolTip", nullptr, nullptr);
    WinFactoryCreate("Rollover", "Rollover", nullptr, nullptr);
}
