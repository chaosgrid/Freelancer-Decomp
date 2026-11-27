// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstdlib>

template <class T>
class IVP_U_Vector
{
public:
    unsigned short memsize; // +0x00: Capacity
    unsigned short n_elems; // +0x02: Element count
    T** elems;              // +0x04: Pointer to array of T* pointers

    IVP_U_Vector()
        : memsize(0)
        , n_elems(0)
        , elems(nullptr)
    {
    }

    // FUN_06344cf0 (thiscall, RET 4): memsize = Size, n_elems = 0, elems =
    // P_MALLOC(Size * 4) (FUN_06343f70) when Size != 0, else null.
    explicit IVP_U_Vector(int Size)
        : memsize((unsigned short)Size)
        , n_elems(0)
        , elems(Size != 0 ? static_cast<T**>(malloc((size_t)Size * 4u)) : nullptr)
    {
    }

    IVP_U_Vector(T** Buffer, int Size)
        : memsize((unsigned short)Size)
        , n_elems(0)
        , elems(Buffer)
    {
    }

    ~IVP_U_Vector()
    {
        Clear();
    }

    // FUN_063449d0 (and the identical instantiation FUN_06369080, the friction
    // solver's vector destructor run by Unwind@06398580): when elems is not the
    // inline buffer right after the header, free it if non-null and then --
    // unconditionally on that branch -- null elems and zero memsize; n_elems is
    // zeroed on both branches.
    void Clear()
    {
        if (elems != InlineBuffer())
        {
            if (elems != nullptr)
            {
                free(elems);
            }
            elems = nullptr;
            memsize = 0;
        }
        n_elems = 0;
    }

    void Add(T* Elem)
    {
        if (n_elems >= memsize)
        {
            Realloc();
        }
        elems[n_elems++] = Elem;
    }

    // FUN_06359170: backward linear search, found / not found only. Retail
    // caller: FUN_063590e0 (add_pushed_core_with_pairs_except, ivp_impact.cpp),
    // to skip re-adding a pair already present.
    bool Contains(T* Elem) const
    {
        for (int I = (int)n_elems - 1; I >= 0; --I)
        {
            if (elems[I] == Elem)
                return true;
        }
        return false;
    }

    // The generic IVP_U_Vector::remove (06355310 and its inlined copies, e.g.
    // 06348ef0 / 06344260): backward search, then -- with NO not-found test --
    // n_elems = n_elems - 1 (u16) and the tail shifts down from the found
    // index while i < n_elems (re-read each pass).
    // Retail quirk reproduced: an absent Elem gives index -1, so
    // elems[-1] = elems[0] and the whole vector shifts down; an empty vector
    // wraps n_elems to 0xffff. Returns the index (-1 when absent).
    int RemoveElement(T* Elem)
    {
        int I = (int)n_elems - 1;
        while (I >= 0)
        {
            if (elems[I] == Elem)
            {
                break;
            }
            --I;
        }
        const int Index = I;
        n_elems = (unsigned short)(n_elems - 1);
        for (; I < (int)n_elems; ++I)
        {
            elems[I] = elems[I + 1];
        }
        return Index;
    }

    void RemoveAt(int Index)
    {
        n_elems--;
        for (int I = Index; I < (int)n_elems; ++I)
        {
            elems[I] = elems[I + 1];
        }
    }

    void RemoveAtAndAllowSort(int Index)
    {
        n_elems--;
        elems[Index] = elems[n_elems];
    }

    T* ElementAt(int Index) const
    {
        return elems[Index];
    }

    T* operator[](int Index) const
    {
        return elems[Index];
    }

    int Len() const
    {
        return (int)n_elems;
    }

protected:
    // The inline storage of IVP_U_Vector_Fixed starts right after this 8-byte header;
    // retail tests `elems == this+8` before freeing.
    T** InlineBuffer()
    {
        return reinterpret_cast<T**>(this + 1);
    }

    // FUN_063441a0: malloc(memsize * 8 + 8) bytes, i.e. room for 2 * memsize + 2
    // pointers, while memsize becomes the u16 2 * memsize + 1; copy the n_elems
    // pointers; free the old block unless it is the inline buffer or null.
    void Realloc()
    {
        unsigned int NewByteCount = ((unsigned int)memsize * 8) + 8;
        T** NewBuffer = static_cast<T**>(malloc(NewByteCount));                  // FUN_06343f70

        unsigned short NewMemsize = (unsigned short)((memsize << 1) + 1);
        memsize = NewMemsize;

        for (int I = 0; I < (int)n_elems; ++I)
        {
            NewBuffer[I] = elems[I];
        }

        if (elems != InlineBuffer())
        {
            if (elems != nullptr)
            {
                free(elems);
            }
        }

        elems = NewBuffer;
    }
};

static_assert(sizeof(IVP_U_Vector<void>) == 0x8, "IVP_U_Vector size must be 0x8");

template <class T, int FIXED_SIZE>
class IVP_U_Vector_Fixed : public IVP_U_Vector<T>
{
public:
    T* Buffer[FIXED_SIZE]; // +0x08: Preallocated inline storage

    // The inline-buffer test (InlineBuffer) offsets from the 8-byte base
    // header, so Buffer must follow it directly.
    IVP_U_Vector_Fixed()
        : IVP_U_Vector<T>(Buffer, FIXED_SIZE)
    {
    }
};

static_assert(sizeof(IVP_U_Vector_Fixed<void, 8>) == 0x28, "IVP_U_Vector_Fixed<8> size must be 0x28");
