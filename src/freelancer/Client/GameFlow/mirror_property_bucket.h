// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// A LocationController base runtime's per-type property cache (freelancer.exe Client\Gf):
// a MetaList<T> of heap-owned T plus a completion byte. The base runtime holds five of them in
// a row, each 0x18 bytes -- +0x00 MissionVendorProperties (MetaList vftable 0x005cb024), +0x18
// NewsBroadcastProperties (0x005cb02c), then CharacterProperties (0x005cb034), ScriptBehavior
// (0x005cb03c) and AmbientScriptProperties (0x005cb044). The MirrorDatabase listener
// (LAB_004378f0 ..) upserts into them through the LocationController (FUN_0043a960 /
// FUN_0043a9f0), and a flag setter (FUN_0043aaf0 for +0x14) marks the list complete.
//
// The MetaList half is Common's template (common_metalist.h): FUN_0043c070 / FUN_0043cbd0 are
// its (scalar deleting) destructor, FUN_0043ce50 free(), FUN_0043ce90 traverse(), FUN_0043cef0
// free_heap(), FUN_0043cf80 remove(), FUN_0043d010 free_node() -- the MissionVendorProperties
// instantiation, ICF-shared across the five types where the bodies match. The
// NewsBroadcastProperties bucket's own bodies are FUN_0043c310 / FUN_0043c330 / FUN_0043c3f0.

#include "common_metalist.h"
#include "dacom.h" // FDUMP

template <class T>
struct TMirrorPropertyBucket
{
    // FUN_0043c0d0.
    TMirrorPropertyBucket()
        : List(false)
        , bComplete(0)
    {
    }

    // FUN_0043c0f0 (the base runtime's inlined teardown has the same body): Clear, then
    // MetaList's destructor (free + free_heap).
    ~TMirrorPropertyBucket()
    {
        Clear();
    }

    // The body of FUN_0043c0f0 before the MetaList destructor.
    void Clear()
    {
        MetaNode<T>* Node = nullptr;
        while (List.traverse(Node, true))
        {
            delete Node->Data;
            Node->Data = nullptr;
        }
        List.free();
        bComplete = 0;
    }

    // FUN_0043c1b0: the element with Value's key (dword +0x00) is replaced by a copy appended at
    // the tail.
    void Upsert(T const& Value)
    {
        unsigned long const Key = Value.get_key();
        MetaNode<T>* Found = nullptr;
        MetaNode<T>* Node = nullptr;
        while (List.traverse(Node, true))
        {
            if (Node->Data->get_key() == Key)
            {
                Found = Node;
                break;
            }
        }
        if (Found != nullptr)
        {
            delete Found->Data;
            List.remove(Found);
        }
        T* const Copy = new T();
        *Copy = Value;
        List.append(Copy);
    }

    // The erase the LocationController inlines (MirrorDatabase.h line 0xf6; in FUN_0043a9f0 for
    // +0x00, FUN_0043a750 for +0x18). No such node: a WARNING FDUMP.
    void Erase(unsigned long Key)
    {
        MetaNode<T>* Found = nullptr;
        MetaNode<T>* Node = nullptr;
        while (List.traverse(Node, true))
        {
            if (Node->Data->get_key() == Key)
            {
                Found = Node;
                break;
            }
        }
        if (Found == nullptr)
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: %s",
                  "E:\\FL\\Scratch\\Source\\Client\\Gf\\MirrorDatabase.h", 0xf6,
                  "Cannot find record to remove\n");
            return;
        }
        if (Found->Data != nullptr)
            delete Found->Data;
        List.remove(Found);
    }

    MetaList<T> List;          // +0x00 (vptr, Head +0x04, Tail +0x08, bUseHeap +0x0c, FreeList +0x10)
    unsigned char bComplete;   // +0x14 set by the LocationController (FUN_0043aaf0 for +0x00's)
    unsigned char Pad15[3];
};
