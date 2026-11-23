// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Forwarding header. The retail GUI hierarchy lives in:
//   - win3d.{h,cpp}             -- Win3D (0x7c), the root, and the Win3D manager
//   - wincontrol_base.{h,cpp}   -- WinControl : Win3D, IDataViewOwner (0x32c)
//   - text_win / win_object / list_box -- TextWin, WinObject, ListBox : Win3D
//   - rich_text_win.{h,cpp}     -- BaseRichTextWin : WinControl, RichTextWin, RichTextEditWin
//   - ui_*.{h,cpp}              -- the PAGE_*.txt UI* widgets on those bases
//   - ui_page.{h,cpp}           -- UIPage::Page / MasterWin : Win3D, IUIPage
//   - win_factory               -- WINFACTORY<T> / FUN_0059db20
// The Options Video/Audio/Credits screens are NeuroNet sub-panels (FUN_004a9790), not
// WinControls.

#include "Interface/wincontrol_base.h"
