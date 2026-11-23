// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>

// ============================================================================
// IVP_U_Memory: the 0x14-byte bump/chunk arena installed twice into every
// IVP_Environment (+0xA0 and +0xA4). The environment constructor splits the
// caller's Parms->MemoryBlock in half (size SAR 1) and hands one half to each
// arena through init_mem (FUN_06344010).
//
// Layout (FUN_06344010 and FUN_06344130):
//   +0x00 ChunkListStart : the caller-supplied (or first) chunk; set after
//                          init_mem and on the reset path.
//   +0x04 ChunkListHead  : head of the chunk chain; each chunk's first dword
//                          is the link to the previous head.
//   +0x08 Cursor         : next free byte, 32-byte aligned; a chunk's payload
//                          starts at (block + 0x23) & ~0x1F (32-byte header).
//   +0x0C End            : end of the current chunk's payload.
//   +0x10 State          : 3 when constructed, 0 once init_mem ran. The
//                          friction and sim-unit passes use it as a busy
//                          counter (incremented / decremented around a pass).
//   +0x12 UsableSize     : Size - 0x20, a 16-bit field in retail too, so a
//                          block larger than 0xFFFF + 0x20 truncates.
// ============================================================================
struct IVP_U_Memory
{
    void*    ChunkListStart; // +0x00
    void*    ChunkListHead;  // +0x04
    char*    Cursor;         // +0x08
    char*    End;            // +0x0C
    uint16_t State;          // +0x10
    uint16_t UsableSize;     // +0x12

    // FUN_06344120: the constructor is init_object. IVP_Environment's ctor
    // news two of these.
    IVP_U_Memory()
    {
        init_object();
    }

    // First usable byte of a chunk: past the 32-byte header, rounded up to 32
    // bytes (inlined at every retail site).
    static char* ChunkPayload(void* Chunk)
    {
        return (char*)(((uintptr_t)Chunk + 0x23) & ~(uintptr_t)0x1F);
    }

    // FUN_06344000 (`JMP FUN_06344150`): ~IVP_Environment calls it on +0xa0 /
    // +0xa4 before operator delete, i.e. `delete Arena`.
    ~IVP_U_Memory()
    {
        free_mem();
    }

    // FUN_06344130
    void init_object()
    {
        ChunkListStart = nullptr;
        ChunkListHead  = nullptr;
        Cursor         = nullptr;
        End            = nullptr;
        State          = 3;
        UsableSize     = 0;
    }

    // FUN_06344010: installs a caller-supplied block as the arena's current
    // chunk; a null block allocates the first heap chunk instead
    // (FUN_063440d0 with size 0).
    void init_mem(char* Block, int32_t Size)
    {
        State = 0; // on both paths, before the branch

        if (Block == nullptr)
        {
            UsableSize = 0;
            GetMemNewBlock(0);
            ChunkListStart = ChunkListHead;
            return;
        }

        UsableSize = (uint16_t)(Size - 0x20);

        // The block's first dword becomes the link, then the block is the head.
        *(void**)Block = ChunkListHead;
        ChunkListHead = Block;

        Cursor = ChunkPayload(Block);
        End = Cursor + UsableSize;
        ChunkListStart = ChunkListHead;
    }

    // FUN_06344070: frees every chunk above the original one, then rewinds the
    // cursor to the start of that chunk ("release everything allocated since
    // init_mem").
    void reset_chunks()
    {
        void* Node = ChunkListHead;
        while (Node != nullptr)
        {
            if (Node == ChunkListStart)
                break; // the original chunk is never freed
            void* Next = *(void**)Node;
            free(Node); // FUN_06343fb0
            Node = Next;
        }

        char* FreeStart = ChunkPayload(ChunkListStart);
        Cursor = FreeStart;
        // UsableSize 0 means no caller block was installed: the 0x7fe0 default
        // chunk payload applies.
        End = FreeStart + (UsableSize != 0 ? UsableSize : 0x7fe0);
        ChunkListHead = ChunkListStart;
    }

    // FUN_06344150 (also reached through the thunk 06344000). The next link
    // is read first; with UsableSize != 0 the walk stops at ChunkListStart
    // (the caller's block) without freeing it; every other chunk is freed.
    // Then the four pointers are zeroed.
    void free_mem()
    {
        void* Node = ChunkListHead;
        while (Node != nullptr)
        {
            void* const Next = *(void**)Node;
            if (UsableSize != 0 && Node == ChunkListStart)
            {
                break;
            }
            free(Node);
            Node = Next;
        }
        Cursor = nullptr;
        End = nullptr;
        ChunkListHead = nullptr;
        ChunkListStart = nullptr;
    }

    // FUN_063440d0: GetMem's slow path. Mallocs (FUN_06343f70) a chunk of
    // max(0x7fd8, round32(Size)) payload bytes plus 0x28, links it in front of
    // the chain and hands out its first aligned byte. ChunkListStart is not
    // touched.
    void* GetMemNewBlock(int Size)
    {
        unsigned int Rounded = ((unsigned int)Size + 0x1f) & 0xffffffe0u;
        unsigned int Payload = 0x7fd8;
        if (Rounded > Payload)                                                       // unsigned
            Payload = Rounded;
        char* Block = (char*)malloc(Payload + 0x28);
        *(void**)Block = ChunkListHead;
        ChunkListHead = Block;
        char* Start = ChunkPayload(Block);
        Cursor = Start + Rounded;
        End = Start + Payload;
        return Start;
    }

    // FUN_06347380: bump allocation. The new cursor (Cursor + Size + 0x1f) &
    // ~0x1f is committed when below End (unsigned), else GetMemNewBlock. Several
    // retail callers inline this (e.g. 063666f0, 06368300, 06368560).
    void* GetMem(int Size)
    {
        char* Old = Cursor;
        char* New = (char*)(((uintptr_t)Old + (unsigned int)Size + 0x1f) & ~(uintptr_t)0x1F);
        if ((uintptr_t)New < (uintptr_t)End)
        {
            Cursor = New;
            return Old;
        }
        return GetMemNewBlock(Size);
    }
};

static_assert(sizeof(IVP_U_Memory) == 0x14, "IVP_U_Memory must be 0x14 bytes");
static_assert(offsetof(IVP_U_Memory, ChunkListHead) == 0x04, "IVP_U_Memory+0x04");
static_assert(offsetof(IVP_U_Memory, Cursor) == 0x08, "IVP_U_Memory+0x08");
static_assert(offsetof(IVP_U_Memory, End) == 0x0C, "IVP_U_Memory+0x0C");
static_assert(offsetof(IVP_U_Memory, State) == 0x10, "IVP_U_Memory+0x10");
static_assert(offsetof(IVP_U_Memory, UsableSize) == 0x12, "IVP_U_Memory+0x12");
