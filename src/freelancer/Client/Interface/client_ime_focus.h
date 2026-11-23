// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The client's IMEFocus: retail installs its own vftable PTR_FUN_005e2210 (dtor FUN_00564150,
// DrawRect, DrawTextW) over the ImeUi DLL's; the port overrides the same three slots.

#include "imeui.h"

#include <stddef.h>

class FontStringChild;

#pragma pack(push, 4)
// Retail operator new(0x1c).
struct ClientIMEFocus : public IMEFocus
{
    explicit ClientIMEFocus(HWND Hwnd);
    ~ClientIMEFocus() override;

    void DrawRect(int X1, int Y1, int X2, int Y2, DWORD Color) override; // LAB_00563e70
    void DrawTextW(const wchar_t* Text, unsigned Color, void* Layout,
                   unsigned Count) override; // LAB_00563cf0

    FontStringChild* ConfirmText; // retail wrapper +0x18
};
#pragma pack(pop)

static_assert(sizeof(IMEFocus) == 0x18, "IMEFocus base 0x18");
static_assert(offsetof(ClientIMEFocus, ConfirmText) == 0x18, "Confirm at +0x18");
static_assert(sizeof(ClientIMEFocus) == 0x1c, "client wrapper 0x1c");

// FUN_00564060 -- construct the client IMEFocus singleton (once, from the pre-init tail).
IMEFocus* EnsureClientIMEFocus();

// FUN_005641d0 (cdecl) -- delete the singleton (DAT_0067a4a4) and zero it. Called at client
// shutdown, after StringShutdown and before CoUninitialize.
void ReleaseClientIMEFocus();

class IFont;

// DAT_0067a4a0 -- "ConfirmPlayerText", fetched on first use at every site (ClientIMEFocus and
// FLImeInputBox ctors).
IFont* ClientConfirmPlayerFont();

// DAT_0067a480 / DAT_0067a484 -- the CRCs of "IMEbkground" / "IMEtext".
unsigned long ClientImeBackgroundKey();
unsigned long ClientImeTextKey();

// FUN_00417790 -- swap the font atlas material key; the previous one goes to OutPrev.
void ClientImeSwapFontMaterialKey(unsigned* OutPrev, unsigned NewKey);
