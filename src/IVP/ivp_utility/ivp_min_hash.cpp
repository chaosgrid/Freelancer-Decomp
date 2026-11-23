// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_min_hash.h"

#include <cstdlib>

namespace
{
    // DAT_063edcc8, initial value 1: stamped into every new element and incremented, so
    // equal values come out in insertion order.
    int32_t G_IVP_MinHashCounter = 1;

    // Retail's x87 "below" test: true when A < B or the pair is unordered (NaN).
    bool MinHash_FcomBelow(float A, float B)
    {
        return !(A >= B);
    }

    // The bucket index add and remove compute: ((k * 101) >> 8) + k * 1001, masked by
    // size - 1, in 32-bit unsigned arithmetic.
    uint32_t MinHash_Index(void* Elem, uint32_t Size)
    {
        uint32_t const Key = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(Elem));
        return (((Key * 101u) >> 8) + Key * 1001u) & (Size - 1u);
    }

    // FUN_06343f80 (p_calloc): malloc(Count * Size) then zero-filled.
    IVP_U_Min_Hash_Elem** MinHash_Calloc(uint32_t Count)
    {
        return static_cast<IVP_U_Min_Hash_Elem**>(std::calloc(Count, sizeof(IVP_U_Min_Hash_Elem*)));
    }
}

// FUN_0634d210. Retail first runs a power-of-two search loop over size whose result is never
// used; it is not reproduced.
IVP_U_Min_Hash::IVP_U_Min_Hash(uint32_t Size)
    : size(Size)
{
    buckets = MinHash_Calloc(size);
    stadium = MinHash_Calloc(size * 2u);
    leaves = stadium + size;
    counter = 0;
}

// FUN_0634d260
IVP_U_Min_Hash::~IVP_U_Min_Hash()
{
    // Every chain, element by element (FUN_06391d5a).
    for (uint32_t Bucket = 0; Bucket < size; ++Bucket)
    {
        IVP_U_Min_Hash_Elem* Elem = buckets[Bucket];
        while (Elem != nullptr)
        {
            IVP_U_Min_Hash_Elem* const Next = Elem->next;
            delete Elem;
            Elem = Next;
        }
    }

    // p_free (FUN_06343fb0) and clear.
    if (buckets != nullptr)
    {
        std::free(buckets);
        buckets = nullptr;
    }
    if (stadium != nullptr)
    {
        std::free(stadium);
        stadium = nullptr;
    }
}

// FUN_0634d2d0: the new element enters at its leaf and climbs while it is below the entry it
// meets (an empty entry is always replaced).
void IVP_U_Min_Hash::add_sort(IVP_U_Min_Hash_Elem* Elem, int32_t Pos)
{
    float const Value = Elem->value;
    int32_t Index = Pos + static_cast<int32_t>(size);
    stadium[Index] = Elem;
    uint32_t Level = size >> 1;
    while (Level != 0)
    {
        Index >>= 1;                            // SAR
        IVP_U_Min_Hash_Elem** const Slot = &stadium[Index];
        IVP_U_Min_Hash_Elem* const Current = *Slot;
        // An occupied slot whose value is not above Value stops the climb.
        if (Current != nullptr && !MinHash_FcomBelow(Value, Current->value))
            break;
        Level >>= 1;
        *Slot = Elem;
    }
}

// FUN_0634d320: Removed has already left bucket Pos. The bucket's new minimum becomes the
// leaf, and every ancestor that still names Removed is recomputed from its two children.
void IVP_U_Min_Hash::remove_sort(IVP_U_Min_Hash_Elem* Removed, int32_t Pos)
{
    // The bucket minimum (lower value; equal values: lower seq).
    IVP_U_Min_Hash_Elem* Best = buckets[Pos];
    if (Best != nullptr)
    {
        for (IVP_U_Min_Hash_Elem* Candidate = Best->next; Candidate != nullptr; Candidate = Candidate->next)
        {
            if (MinHash_FcomBelow(Candidate->value, Best->value))
                Best = Candidate;
            else if (Candidate->value == Best->value && Candidate->seq < Best->seq)
                Best = Candidate;
        }
    }
    leaves[Pos] = Best;

    int32_t Index = Pos + static_cast<int32_t>(size);
    uint32_t Level = size >> 1;
    while (Level != 0)
    {
        Index >>= 1;                            // SAR
        if (stadium[Index] != Removed)
            break;

        IVP_U_Min_Hash_Elem* const Left = stadium[Index * 2];
        if (Left == nullptr)
        {
            stadium[Index] = stadium[Index * 2 + 1];
        }
        else
        {
            IVP_U_Min_Hash_Elem* const Right = stadium[Index * 2 + 1];
            if (Right == nullptr || MinHash_FcomBelow(Left->value, Right->value))
                stadium[Index] = Left;
            else if (Left->value == Right->value && Left->seq < Right->seq)
                stadium[Index] = Left;
            else
                stadium[Index] = Right;
        }
        Level >>= 1;
    }
}

// FUN_0634d3e0
void IVP_U_Min_Hash::add(void* Elem, float Value)
{
    uint32_t const Index = MinHash_Index(Elem, size);

    IVP_U_Min_Hash_Elem* const New = new IVP_U_Min_Hash_Elem;
    New->seq = G_IVP_MinHashCounter;
    ++G_IVP_MinHashCounter;
    New->elem = Elem;
    New->next = buckets[Index];
    buckets[Index] = New;
    New->value = Value;
    ++counter;

    // The new element re-sorts only when it beats the bucket's leaf.
    IVP_U_Min_Hash_Elem* const Leaf = leaves[Index];
    if (Leaf == nullptr || MinHash_FcomBelow(Value, Leaf->value))
        add_sort(New, static_cast<int32_t>(Index));
}

// FUN_0634d470
void IVP_U_Min_Hash::remove(void* Elem)
{
    uint32_t const Index = MinHash_Index(Elem, size);
    IVP_U_Min_Hash_Elem** const Bucket = &buckets[Index];

    // Find the element, remembering its predecessor.
    IVP_U_Min_Hash_Elem* Previous = nullptr;
    IVP_U_Min_Hash_Elem* Found = *Bucket;
    while (Found != nullptr && Found->elem != Elem)
    {
        Previous = Found;
        Found = Found->next;
    }
    if (Found == nullptr)
        return;

    if (Previous != nullptr)
        Previous->next = Found->next;
    else
        *Bucket = Found->next;

    // The bucket's minimum left, so the tree is repaired.
    if (Found == leaves[Index])
        remove_sort(Found, static_cast<int32_t>(Index));

    delete Found;
    --counter;
}

// FUN_0634d4f0
void IVP_U_Min_Hash::remove_min()
{
    IVP_U_Min_Hash_Elem* const Min = stadium[1];
    if (Min == nullptr)
        return;
    void* const Elem = Min->elem;
    if (Elem == nullptr)
        return;
    remove(Elem);
}
