// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll BaseWatcher (one link of a Watchable's watcher chain).
//
// Retail's three "clear the BaseWatcher embedded at +0x14 / +0x28 / +0x8" helpers
// (FUN_062c3040 / FUN_062c5c20 / FUN_062d27b0) are called only from EH unwind funclets
// (Unwind@06394a48 .. Unwind@0639536e): they are the compiler's member destructor on the
// exception path. In the port the BaseWatcher members' own destructor (set(nullptr)) runs
// there, so those helpers have no body of their own here.

#include "common_goods.h"
#include "common_iface.h" // IObjRW, ObjRwFromWatchable (BaseWatcher::get_watched_id)

// ---------------------------------------------------------------------------
// BaseWatcher
// ---------------------------------------------------------------------------

BaseWatcher::BaseWatcher()
    : Watched(nullptr)
    , Next(nullptr)
{
}

BaseWatcher::BaseWatcher(IObjRW* Target)
    : Watched(nullptr)
    , Next(nullptr)
{
    // FUN_062c2920. Target+8 is IObjRW's Watchable sub-object. Retail's redundant
    // "(Watchable*)(param_1+8) != 0" test, never false once param_1 != 0, is omitted.
    if (Target != nullptr)
    {
        Watchable* WatchedObj = Target;   // IObjRW -> its Watchable base (+8)
        set_pointer(WatchedObj);
    }
}

BaseWatcher::BaseWatcher(Watchable* const* Src)
    : Watched(nullptr)
    , Next(nullptr)
{
    // FUN_062ed070. Dereferences Src once (no +8 adjustment, unlike the IObjRW* overload
    // above) and also treats the literal pointer value 0x8 as a sentinel alongside null.
    Watchable* V = *Src;
    if (V != nullptr && V != reinterpret_cast<Watchable*>(8))
        set_pointer(V);
}

BaseWatcher::~BaseWatcher()
{
    set(nullptr);
}

BaseWatcher& BaseWatcher::operator=(BaseWatcher const& Other)
{
    // .text:06280ee0: a raw copy of both dwords (Watched, Next). It does not unlink from the
    // old chain or link into the new one.
    Watched = Other.Watched;
    Next = Other.Next;
    return *this;
}

void BaseWatcher::set(Watchable const* Target)
{
    if (Target != Watched)
        set_pointer(Target);
}

void BaseWatcher::set_pointer(Watchable const* Target)
{
    // Retail 0xe1610: unlink `this` from its current chain, which is rooted at
    // Watched->Head (not this->Next): walk the slot that holds `this` from &Watched->Head.
    if (Watched != nullptr)
    {
        BaseWatcher** Slot = &const_cast<Watchable*>(Watched)->Head;
        while (*Slot != this)
            Slot = &(*Slot)->Next;
        *Slot = Next;
        Next = nullptr;
    }

    if (Target != nullptr)
    {
        // Link into target's watcher chain
        Next = Target->Head;
        const_cast<Watchable*>(Target)->Head = this;
    }
    Watched = Target;
}

unsigned int BaseWatcher::get_watched_id() const
{
    // FUN_062c2ab0, thiscall: Watched - 8 (the Watchable -> IObjRW downcast), then a tail
    // JMP to IObjInspect slot 0x20, get_id(), a plain thiscall virtual (not __stdcall).
    IObjRW* const Owner = ObjRwFromWatchable(const_cast<Watchable*>(Watched));
    if (Owner == nullptr)
        return 0;
    return Owner->get_id();
}
