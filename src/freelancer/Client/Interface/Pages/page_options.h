// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail Intro.cpp PageOptions (RTTI .?AUPageOptions@@ : UIPage::MasterWin) -- the main-menu
// Options page. One static instance, DAT_0067a7d0 (static init LAB_005735b0 -> ctor
// FUN_005735d0, atexit LAB_00573780 -> dtor FUN_00573710). vftables: main 0x005e2fbc,
// IUIPage face (+0x7c) 0x005e2fa4, Event::Handler face (+0x80) 0x005e2f9c (slot 0
// FUN_0053f330, false).
//
// TypeSelect command 0xa -> IntroFocus state 10. EnterState case 10 releases the active page,
// calls Create(0, 0) and makes this the active page (+0x228). Create builds the NN_Preferences
// WinControl; IntroFocus::Update watches +0xb8 / +0xb4 to leave the state.
//
// Retail layout: MasterWin (+0x00..+0x8b, no page data), the FUiPageDesc at +0x8c (never
// loaded), +0xb4 Result, +0xb8 Preferences. The port's MasterWin does not reproduce retail's
// byte layout, so the members of this and the other intro pages keep only retail's order.

#include "Interface/ui_page.h"
#include "Interface/ui_page_desc.h"

class WinControl;

// The button command PageOptions::ProcessMessage answers with kWinMsgCommand: back to the main
// menu state.
enum EOptionsCommand
{
    kOptionsCmdMainMenu = 0x300c
};

class PageOptions : public UIPage::MasterWin
{
public:
    // FUN_005735d0.
    PageOptions();
    // FUN_00573710.
    ~PageOptions() override;

    // slot 9 FUN_00573650 (RET 8).
    bool Create(Win3D* NewParent, void* Payload) override;
    // slot 10 is LAB_00573640, a jump to MasterWin::Destroy: not overridden.

    // slot 23 FUN_005736a0 (RET 0xc).
    bool ProcessMessage(unsigned Msg, unsigned A, unsigned B) override;

    FUiPageDesc Desc;        // retail +0x8c
    int Result;              // retail +0xb4 -- DAT_0067a884
    WinControl* Preferences; // retail +0xb8 -- DAT_0067a888
};

// DAT_0067a7d0.
PageOptions& GetPageOptions();
