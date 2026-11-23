// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Rich-edit text helpers shared by the FLServer dialogs (all cdecl, UTF-16 through code page
// 1200).
#pragma once

// .text:00409800
int GetRichEditText(HWND Window, wchar_t* Buf, int BufBytes);

// .text:00409840 — replaces the whole text.
void SetRichEditText(HWND Window, const wchar_t* Text);

// .text:00409880 — appends at the end through the selection.
void AppendRichEditText(HWND Window, const wchar_t* Text);
