// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_misc.h"

#include <cstdlib>
#include <cstring>

// ============================================================================
// Heap
// ============================================================================

Heap::Heap(unsigned int ElementSize, int MaxFreeCount)
    : ElementSize(ElementSize), FreeList(nullptr), MaxFreeCount(MaxFreeCount), CurrentFreeCount(0)
{
    // RVA 0x4d450.
}

void* Heap::Malloc()
{
    // RVA 0xae580.
    if (FreeList != nullptr)
    {
        FFreeBlock* Block = FreeList;
        FreeList = Block->Next;
        --CurrentFreeCount;
        return Block;
    }
    return malloc(ElementSize);
}

void Heap::Free(void* Ptr)
{
    // RVA 0xae5a0. A negative MaxFreeCount means no cap. Pooled blocks get the 0xCD debug
    // fill.
    if (MaxFreeCount <= CurrentFreeCount && MaxFreeCount >= 0)
    {
        free(Ptr);
        return;
    }

    memset(Ptr, 0xCD, ElementSize);

    ++CurrentFreeCount;
    FFreeBlock* const Block = (FFreeBlock*)Ptr;
    Block->Next = FreeList;
    FreeList = Block;
}

void Heap::Shrink()
{
    // RVA 0xae600.
    FFreeBlock* Node = FreeList;
    while (Node != nullptr)
    {
        FFreeBlock* const Next = Node->Next;
        free(Node);
        Node = Next;
    }
    CurrentFreeCount = 0;
    FreeList = nullptr;
}
