// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail WinFactory / WINFACTORY<T> (RTTI .?AUWinFactory@@, .?AU?$WINFACTORY@U<T>@@@@).
//
// Every WINFACTORY<T> is a .data global built by a static initializer and unlinked by an
// atexit dtor. The base vftable 0x005cb554 has one slot, Create: new T, widget+0x10 =
// &factory+0xc, Win3D::Create(Parent, Payload); false deletes the widget and returns 0.
// The class name (<= 31 chars) is the PAGE_*.txt type token and the lookup key.

#include "Interface/win3d.h"

class WinFactory
{
public:
    // Inline head of every WINFACTORY<T> ctor (e.g. FUN_0059c360).
    explicit WinFactory(char const* Name);
    // FUN_00579380. Not virtual: retail's WinFactory vftable has the single Create slot.
    ~WinFactory();

    // slot 0 (+0x00)
    virtual Win3D* Create(Win3D* Parent, void* Payload) = 0;

    WinFactory* Next;        // +0x04 -- list head DAT_0067dcec
    unsigned NameLength;     // +0x08
    char ClassName[32];      // +0x0c

private:
    WinFactory(WinFactory const&) = delete;
    WinFactory& operator=(WinFactory const&) = delete;
};

template <class T>
class WINFACTORY : public WinFactory
{
public:
    explicit WINFACTORY(char const* Name)
        : WinFactory(Name)
    {
    }

    Win3D* Create(Win3D* Parent, void* Payload) override
    {
        T* Widget = new T();
        Widget->ClassName = ClassName;
        if (!Widget->Create(Parent, Payload))
        {
            delete Widget;
            return nullptr;
        }
        return Widget;
    }
};

// FUN_0059dab0 — push-front.
void WinFactoryRegister(WinFactory* Factory);

// FUN_0059dad0
void WinFactoryUnregister(WinFactory* Factory);

// FUN_0059db20 (cdecl) — the first factory whose name equals ClassName creates the widget,
// which then takes Nickname as its name (clamped to 31 chars; null -> empty). Null when
// ClassName is null, no factory matches or Create fails.
Win3D* WinFactoryCreate(char const* Nickname, char const* ClassName, Win3D* Parent,
                        void* Payload);
