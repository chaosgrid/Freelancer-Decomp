// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/Pages/page_options.h"

#include "Interface/menu_ui_init.h"
#include "Interface/win_factory.h"
#include "Interface/wincontrol_base.h"
#include "Interface/menu_enter_state.h"

namespace
{
    // DAT_0067a7d0.
    PageOptions G_PageOptions;
}

PageOptions::PageOptions()
    : UIPage::MasterWin(nullptr)
    , Result(0)
    , Preferences(nullptr)
{
    // FUN_005735d0. Retail leaves +0xb8 unwritten; the static object starts zeroed, which is
    // what the initialiser above reproduces.
}

PageOptions::~PageOptions()
{
    // FUN_00573710.
}

bool PageOptions::Create(Win3D* NewParent, void* /*Payload*/)
{
    // FUN_00573650. With no page data MasterWin::Create only reparents, registers and sets the
    // flags; its result is not tested.
    UIPage::MasterWin::Create(NewParent, nullptr);
    Result = 0;
    // No null test on the factory result, as retail.
    Win3D* const Created =
        WinFactoryCreate("NN_Preferences", "NN_Preferences", nullptr, reinterpret_cast<void*>(1));
    Preferences = Created->AsWinControl();
    return true;
}

bool PageOptions::ProcessMessage(unsigned Msg, unsigned A, unsigned /*B*/)
{
    // FUN_005736a0.
    if (Msg == kWinMsgCommand)
    {
        PlayOrRewindUiSoundSlot(0);
        if (A != 0x300c)
            return false;
        Result = kMenuStateMainMenu;
        return true;
    }
    return Msg == kWinMsgDoubleClick;
}

PageOptions& GetPageOptions()
{
    return G_PageOptions;
}
