// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The ToolTip hover record (0x64 bytes): packed on the stack by WinControl slot 34
// (FUN_005a0530), ButtonControl FUN_0055bde0 and WireButton FUN_005a46c0, embedded in every
// BaseRichTextWin at +0x40c, and consumed by the ToolTip through FUN_004be370 -> FUN_004bdea0.

#include "Common/Fonts/font_string.h"

#include <stddef.h>

struct FToolTipHoverInfo;

// FUN_00444ee0, the record's destructor: the Font::String at +0x10, then the caption buffer at
// +0x08. Defined in cargo_bar_control.cpp.
void ToolTipHoverDestroy(FToolTipHoverInfo* Info);

// Every retail site that builds one constructs the caption buffer and the Font::String, and
// tears the record down through FUN_00444ee0 (or its inlined body) on the normal and the unwind
// path (FuncInfo_005fed48 for FUN_005a0530). The rest of the record is written by each site;
// retail leaves it as stack garbage until then, the port starts it at zero.
struct FToolTipHoverInfo
{
    FToolTipHoverInfo()
        : Ids(0), IdsAlt(0), CaptionW(nullptr), CaptionCapacity(0), AuxField(0),
          WorldPos{0.0f, 0.0f, 0.0f}, HalfExtents{0.0f, 0.0f, 0.0f}, bLive(0)
    {
    }
    // FUN_00444ee0. The Font::String member dtor that follows finds it already empty.
    ~FToolTipHoverInfo() { ToolTipHoverDestroy(this); }
    FToolTipHoverInfo(FToolTipHoverInfo const&) = delete;
    FToolTipHoverInfo& operator=(FToolTipHoverInfo const&) = delete;

    int Ids;                     // +0x00  LabelIds; also the record's key
    int IdsAlt;                  // +0x04  IDS used by FUN_004bd880 (via +0xf8)
    wchar_t* CaptionW;           // +0x08  null -> resolve via IdsAlt
    unsigned CaptionCapacity;    // +0x0c  wchar count of +0x08
    FFontString Text;            // +0x10 .. +0x43
    int AuxField;                // +0x44  KeyCmd NicknameId (FUN_00577dd0)
    float WorldPos[3];           // +0x48
    float HalfExtents[3];        // +0x54
    unsigned char bLive;         // +0x60
};

static_assert(offsetof(FToolTipHoverInfo, AuxField) == 0x44, "hover +0x44");
static_assert(offsetof(FToolTipHoverInfo, WorldPos) == 0x48, "hover +0x48");
static_assert(offsetof(FToolTipHoverInfo, bLive) == 0x60, "hover +0x60");
static_assert(offsetof(FToolTipHoverInfo, Text) == 0x10, "hover +0x10 Font::String");
static_assert(sizeof(FToolTipHoverInfo) == 0x64, "BaseRichTextWin +0x40c..+0x470");

// FUN_004be370 -- guarded forward to the ToolTip singleton's UpdateFromHover.
void NnToolTipFeedHover(FToolTipHoverInfo const* Info);

// FUN_004963a0 (thiscall on the record) -- Ids -1, IdsAlt 0, AuxField 0, the caption emptied
// when it has a non-empty buffer, bLive 0. WorldPos / HalfExtents / Text are untouched.
// Callers: NeuroNetNavMap::Update, UiFastTable and one more site.
void ToolTipHoverReset(FToolTipHoverInfo* Info);
