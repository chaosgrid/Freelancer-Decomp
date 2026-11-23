// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail NN_ArrivalText (RTTI .?AVNN_ArrivalText@@ : WinControl) -- the "<BASE>, <SYSTEM>" /
// "<SYSTEM> <region>" caption that types itself out on arrival (the Ship's jump state 5, the
// space init, the base entry) and fades away after a hold time. One instance at a time: the ctor
// stores itself in DAT_0066d398 (G_NnArrivalText, nn_modal.h) and the dtor clears it.
//
// WINFACTORY<NN_ArrivalText> DAT_0066d35c: static init LAB_0045ab60, Create FUN_0045b530
// (operator_new(0x33c)).
//
// vftable 0x005ce85c (45 slots), scalar deleting dtor FUN_0045ab90. IDataViewOwner face at +0x7c:
// WinControl's.
//
// Layout:
//   +0x000 WinControl (0x32c)
//   +0x32c "ArrivalText" TextControl (Create)
//   +0x330 float hold time once the caption is fully typed (Create: 2.0f)
//   +0x334 float delay before the caption shows (Create: 4.0f; the Show* methods reset it)
//   +0x338 byte: the caption waits for the letterbox to go away (ctor 0; FUN_0045aff0 sets it)
//   size 0x33c

#include "Interface/wincontrol_base.h"

#include <stddef.h>

class NnTextControl;

class NnArrivalText : public WinControl
{
public:
    // Inlined into FUN_0045b530. +0x32c..+0x334 are left to Create.
    NnArrivalText();
    // FUN_0045abb0.
    ~NnArrivalText() override;

    // slot 9 FUN_0045abd0 (RET 8)
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 11 LAB_0045acc0
    void Update() override;

    // FUN_0045ae90 (thiscall, RET 4) -- docked only: "<base>, <system>" (ids 0x55c) with the
    // delay set so the caption finishes typing HoldTime before ArrivalTime runs out.
    void ShowBaseArrival(float ArrivalTime);
    // FUN_0045aff0 (thiscall, RET 4) -- the system caption: ids 0x55e with the region the
    // system's strid_name falls in, or ids 0x55d alone; waits for the letterbox.
    void ShowSystemArrival(bool bResetDelay);
    // FUN_0045b1c0 (thiscall, RET 4) -- "<NameIds>, <system>" (ids 0x55c), delay 2.0f.
    void ShowNamedArrival(unsigned NameIds);
    // FUN_0045b2c0 (thiscall) -- bit 1 of the caption's Win3D flags (it is showing).
    bool IsCaptionVisible() const;

    NnTextControl* Caption;      // +0x32c
    float HoldTime;              // +0x330
    float Delay;                 // +0x334
    unsigned char bWaitForLetterbox; // +0x338
    unsigned char Pad339[3];     // +0x339

    // The teardown retail inlines in Update and in FUN_0045b380 / FUN_0045b3c0: FUN_0043d2a0 on
    // every child, slot 10 Destroy, then delete. `this` is gone afterwards.
    void DestroySelf();

private:
    NnArrivalText(NnArrivalText const&) = delete;
    NnArrivalText& operator=(NnArrivalText const&) = delete;
};

static_assert(sizeof(NnArrivalText) == 0x33c, "retail NN_ArrivalText is 0x33c bytes (0x0045b548)");
static_assert(offsetof(NnArrivalText, Caption) == 0x32c, "NN_ArrivalText +0x32c caption");
static_assert(offsetof(NnArrivalText, HoldTime) == 0x330, "NN_ArrivalText +0x330 hold");
static_assert(offsetof(NnArrivalText, Delay) == 0x334, "NN_ArrivalText +0x334 delay");
static_assert(offsetof(NnArrivalText, bWaitForLetterbox) == 0x338, "NN_ArrivalText +0x338");

// The cdecl entry points. Each "ensure" makes the window through the WinFactory
// (FUN_0059db20("NN_ArrivalText", "NN_ArrivalText", 0, 0)) when DAT_0066d398 is null and gives up
// when it still is.
// FUN_0045b2d0 -- ensure, then ShowBaseArrival(ArrivalTime).
void NnArrivalTextShowBase(float ArrivalTime);
// FUN_0045b340 -- ensure, then ShowNamedArrival(NameIds). Caller FUN_00514bf0.
void NnArrivalTextShowNamed(unsigned NameIds);
// FUN_0045b380 -- tear the window down when there is one.
void NnArrivalTextDestroy();
// FUN_0045b3c0 -- a pending caption (delay above 0) has its delay cut to at most 1.0f; one whose
// delay has run out is torn down.
void NnArrivalTextHurry();
// FUN_0045b430 -- ensure, then Delay = DelayBits (copied as a dword), no letterbox wait, and the
// caption's property 0x1e with Arg.
void NnArrivalTextShowRaw(float Delay, uintptr_t Arg);
// FUN_0045b490 -- ensure, then ShowSystemArrival(true). Caller: the space init.
void NnArrivalTextShowSystemNow();
