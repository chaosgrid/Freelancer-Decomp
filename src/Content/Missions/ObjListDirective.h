// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Content.dll TU40: abstract base of the [ObjList] directive objects built by the factory
// FUN_06f24870 (switch on the directive type, 0x19 cases) and kept in the TU50 ship's
// directive list (FContentShipBase::Directives, +0x158).
//
// Retail vtable 06fb8314 (written inline by every derived ctor before its own). AddRef 06eacd40
// is ICF-shared with FContentPlayerStateBase. GetType 0x13 = goto (checked by FUN_06f579c0).
// The first derived member sits at +0x10c, so Name is TString<0x100>.
// The derived classes live in TU40; only this base is here because the TU50 ship base calls
// its slots.

#include "Core/ContentRefObject.h"   // FContentRefObject (the ref-counted base 06fb43f8)
#include "common_tstring.h"

#include <cstdio>
#include <list>

class INI_Reader;
class FContentShipBase;

// Slots 0-2 and +0x04 are the ref-counted base FContentRefObject (vtable 06fb43f8).
class FObjListDirective : public FContentRefObject
{
public:
    TString<0x100> Name;           // +0x08

    FObjListDirective()
    {
        Name.Length = 0;
        Name.Data[0] = '\0';
    }

    // Slot 2 (06f06eb0 -> FUN_06ea8cc0).
    ~FObjListDirective() override = default;

    // Slot 3 (+0x0c): false = finished at once (the ship advances to the next one,
    // FUN_06f57b00).
    virtual bool Execute(FContentShipBase* Ship) = 0;

    // Slot 4 (+0x10), base 06ebf6f0.
    virtual void Parse(INI_Reader* Reader)
    {
        (void)Reader;
    }

    // Slot 5 (+0x14).
    virtual int GetType() = 0;

    // Slot 6 (+0x18), base 06f25f30.
    virtual void Save(FILE* File)
    {
        if (Name.Length != 0)
            fprintf(File, "%s\n", Name.Data);
    }
};

// A list of [ObjList] directives holding one reference on each element. Retail keeps these as
// bare std::list<Directive*> and inlines the reference handling at every use: the empty list
// FUN_06f1d4c0, the assignment FUN_06f20230, the teardown FUN_06ed2a00 + FUN_06f15350 (free
// the head); FUN_06ecfa90 is that teardown as an unwind action.
// Used by the mission ship slot (+0x58), the [ObjList] map (+0x100), the guides' directive
// lists and the ship base's queue (FContentShipBase::Directives, +0x158). Lives here rather
// than in MissionObject.h so that BehaviorCmds.h can hold one.
class FMissionDirectiveList
{
public:
    FMissionDirectiveList() = default;
    // FUN_06f1d4c0 + FUN_06f20230 on the empty list.
    FMissionDirectiveList(FMissionDirectiveList const& Other);
    // FUN_06f20230 (also inlined in FUN_06ed34e0).
    FMissionDirectiveList& operator=(FMissionDirectiveList const& Other);
    // FUN_06ed2a00 + FUN_06f15350.
    ~FMissionDirectiveList();

    // FUN_06ed2a00.
    void Clear();

    std::list<FObjListDirective*> Items;
};

inline FMissionDirectiveList::FMissionDirectiveList(FMissionDirectiveList const& Other)
    : Items(Other.Items)
{
    std::list<FObjListDirective*>::iterator It = Items.begin();
    for (; It != Items.end(); ++It)
        (*It)->AddRef();
}

// FUN_06f20230. Retail skips the assign when Other is this list.
inline FMissionDirectiveList& FMissionDirectiveList::operator=(FMissionDirectiveList const& Other)
{
    Clear();
    Items = Other.Items;
    std::list<FObjListDirective*>::iterator It = Items.begin();
    for (; It != Items.end(); ++It)
        (*It)->AddRef();
    return *this;
}

inline FMissionDirectiveList::~FMissionDirectiveList()
{
    Clear();
}

inline void FMissionDirectiveList::Clear()
{
    std::list<FObjListDirective*>::iterator It = Items.begin();
    for (; It != Items.end(); ++It)
        (*It)->Release();
    Items.clear();
}
