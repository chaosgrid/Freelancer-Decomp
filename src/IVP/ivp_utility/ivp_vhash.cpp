// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// IVP_VHash / IVP_VHash_Store bodies and the CRC-32 table they share with
// IVP_Hash and the active-value hash.

#include "ivp_vhash.h"

#include "ivp_core_trap.h"
#include "ivp_crc32.h"

#include <cstdlib>

// ============================================================================
// The CRC-32 table (retail DAT_063edcdc), filled by a dynamic initializer.
// No load-time reader exists.
// ============================================================================

namespace
{
    unsigned IVP_InitCrc32Table(unsigned (&table)[256])
    {
        for (unsigned i = 0; i < 256; ++i)
        {
            unsigned c = i;
            for (int k = 0; k < 8; ++k)
            {
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        return 0;
    }
}

unsigned g_IVP_Crc32Table[256];

namespace
{
    unsigned g_IVP_Crc32TableInit = IVP_InitCrc32Table(g_IVP_Crc32Table);
}

// ============================================================================
// IVP_VHash: a Robin Hood value set. Unlike IVP_VHash_Store it stores one
// element pointer per slot, tagged with a caller-supplied hash.
//
// Slot (8 bytes): +0x0 HashIndex (caller's hash | 0x80000000; the OR is
// applied again at each compare, as in retail), +0x4 Elem (null = empty).
// The count dword (+0x08) keeps the element count in its low 24 bits and a
// "not owned" flag in its top byte.
//
// The Robin Hood carry/swap order and the backward-shift deletion's
// wraparound refDist follow retail exactly; do not simplify.
//
// RemoveElem writes through a null pointer when the element is absent (retail
// contract-violation crash, reproduced). FindElem returns null instead.
// ============================================================================

namespace
{
    struct FVHashElem
    {
        unsigned hashTag; // +0x0
        void* value;      // +0x4
    };

    // Retail's "replace the low 24 bits, keep the flag byte" idiom
    // (`(new ^ old) & 0xffffff ^ old`), shared by all four count updates.
    unsigned ReplaceLow24PreserveTopByte(unsigned dwordWithFlags, unsigned newLow24)
    {
        return ((dwordWithFlags ^ newLow24) & 0xffffffu) ^ dwordWithFlags;
    }
}

// FUN_06352320: grows to exactly NewSize buckets and re-inserts every entry
// through AddElem (which bumps the count), then restores the pre-rehash count
// and frees the old array unless the "not owned" flag is set.
void IVP_VHash::Rehash(int NewSize)
{
    int oldMask = SizeMm;
    FVHashElem* oldBuckets = static_cast<FVHashElem*>(Elems);
    SizeMm = NewSize - 1;
    Elems = calloc(static_cast<size_t>(NewSize), 0x8); // FUN_06343f80
    unsigned preRehashCount = NElemsAndFlags;

    if (oldMask >= 0)
    {
        int n = oldMask + 1;
        FVHashElem* entry = oldBuckets;
        do
        {
            if (entry->value != nullptr)
            {
                AddElem(entry->value, entry->hashTag);
            }
            ++entry;
            --n;
        } while (n != 0);
    }

    NElemsAndFlags = ReplaceLow24PreserveTopByte(NElemsAndFlags, preRehashCount & 0xffffffu);
    if (((NElemsAndFlags & 0xff000000u) == 0) && (oldBuckets != nullptr))
    {
        free(oldBuckets); // FUN_06343fb0
    }
    NElemsAndFlags &= 0xffffffu;
}

// FUN_063523d0: doubles the table (to 2*mask+2) at 50% load, then the Robin
// Hood insert: at each occupied slot the incoming entry displaces a resident
// that sits closer to its ideal bucket, and the resident is carried forward.
// HashIndex is already | 0x80000000.
void IVP_VHash::AddElem(void* Elem, unsigned HashIndex)
{
    int mask = SizeMm;
    int doubledCount = static_cast<int>((NElemsAndFlags & 0xffffffu)) * 2;
    if (doubledCount != mask && mask <= doubledCount)
    {
        Rehash(mask * 2 + 2);
    }

    void* value = Elem;
    unsigned hash = HashIndex;

    unsigned mask2 = static_cast<unsigned>(SizeMm);
    unsigned idx = mask2 & hash;
    FVHashElem* buckets = static_cast<FVHashElem*>(Elems);
    FVHashElem* slot = &buckets[idx];
    NElemsAndFlags = ReplaceLow24PreserveTopByte(NElemsAndFlags, (NElemsAndFlags + 1) & 0xffffffu);

    void* residentValue = slot->value;
    unsigned curIdx = idx;
    while (residentValue != nullptr)
    {
        unsigned residentHash = slot->hashTag;
        unsigned residentIdeal = residentHash & mask2;
        if (static_cast<int>(curIdx) < static_cast<int>(residentIdeal))
        {
            slot->value = value;
            slot->hashTag = hash;
            curIdx = residentIdeal;
            value = residentValue;
            hash = residentHash;
        }
        mask2 = static_cast<unsigned>(SizeMm);
        idx = (idx + 1) & mask2;
        buckets = static_cast<FVHashElem*>(Elems);
        slot = &buckets[idx];
        residentValue = slot->value;
    }

    slot->value = value;
    slot->hashTag = hash;
}

// FUN_06352480: probe from HashIndex & mask, calling Compare(stored, Elem) on
// each tag match, then backward-shift deletion.
void* IVP_VHash::RemoveElem(void* Elem, unsigned HashIndex)
{
    unsigned idx = HashIndex & static_cast<unsigned>(SizeMm);
    FVHashElem* slot;
    for (;;)
    {
        FVHashElem* buckets = static_cast<FVHashElem*>(Elems);
        slot = &buckets[idx];
        if (slot->value == nullptr)
        {
            // Retail writes to address 0 when the element is absent (063524a0).
            IVP_CoreTrap();
        }
        if (((slot->hashTag | 0x80000000u) == HashIndex))
        {
            if (Compare(slot->value, Elem) == 1)
            {
                break;
            }
        }
        idx = (idx + 1) & static_cast<unsigned>(SizeMm);
    }

    void* removedValue = slot->value;
    FVHashElem* buckets = static_cast<FVHashElem*>(Elems);
    unsigned mask = static_cast<unsigned>(SizeMm);
    NElemsAndFlags = ReplaceLow24PreserveTopByte(NElemsAndFlags, (NElemsAndFlags - 1) & 0xffffffu);

    unsigned refDist = mask;
    if (buckets[mask].value != nullptr)
    {
        refDist = buckets[mask].hashTag & mask;
    }

    unsigned vacant = idx;
    unsigned scan = (idx + 1) & mask;
    while (buckets[scan].value != nullptr)
    {
        unsigned scanIdeal = mask & buckets[scan].hashTag;
        bool doShift = false;
        if (static_cast<int>(vacant) < static_cast<int>(scan))
        {
            if (((static_cast<int>(refDist) <= static_cast<int>(scanIdeal)) &&
                 (scanIdeal != refDist || static_cast<int>(scan) < static_cast<int>(scanIdeal))) ||
                (static_cast<int>(scanIdeal) <= static_cast<int>(vacant)))
            {
                doShift = true;
            }
        }
        else
        {
            if (vacant != mask)
            {
                break;
            }
            if (static_cast<int>(refDist) <= static_cast<int>(scanIdeal))
            {
                doShift = true;
            }
        }

        if (doShift)
        {
            buckets[vacant] = buckets[scan];
            vacant = scan;
        }

        mask = static_cast<unsigned>(SizeMm);
        buckets = static_cast<FVHashElem*>(Elems);
        scan = (scan + 1) & mask;
    }

    buckets[vacant].value = nullptr;
    buckets[vacant].hashTag = 0;
    return removedValue;
}

// FUN_06352580: the same probe as RemoveElem, returning null at an empty slot.
void* IVP_VHash::FindElem(void* Elem, unsigned HashIndex)
{
    unsigned idx = HashIndex & static_cast<unsigned>(SizeMm);
    FVHashElem* buckets = static_cast<FVHashElem*>(Elems);
    FVHashElem* slot = &buckets[idx];
    void* value = slot->value;

    for (;;)
    {
        if (value == nullptr)
        {
            return nullptr;
        }
        if ((slot->hashTag | 0x80000000u) == HashIndex)
        {
            if (Compare(value, Elem) != 0)
            {
                break;
            }
        }
        idx = (idx + 1) & static_cast<unsigned>(SizeMm);
        buckets = static_cast<FVHashElem*>(Elems);
        slot = &buckets[idx];
        value = slot->value;
    }

    return slot->value;
}

// FUN_063522f0 (the non-deleting ~IVP_VHash) is IVP_VHash::~IVP_VHash in ivp_vhash.h; the
// derived FUN_0636cf90 (vtable 063a58f8) is FLedgeSoupPointHash::~FLedgeSoupPointHash in
// ivp_surface_builder_ledge_soup.cpp.

// FUN_0635d4d0: the called copy of the CRC-32-over-4-bytes hash IVP_VHash_Store
// inlines, | 0x80000000. Caller: IVP_Cluster_Manager (ivp_cluster_manager.cpp),
// feeding IVP_VHash::FindElem.
unsigned __fastcall IVP_VHash_HashPointer(unsigned Key)
{
    return IVP_Crc32HashKey(Key) | 0x80000000u;
}

// ============================================================================
// IVP_VHash_Store: a pointer -> pointer map (Robin Hood open addressing, keys
// hashed through the CRC-32 table above). Layout: see ivp_vhash.h.
//
// Find and grow test `value != null` for an occupied slot, insert and erase
// test `key != null`, as in retail; keys and values are never zero in use.
// ============================================================================

namespace
{
    struct FVHashStoreElem
    {
        unsigned hash; // +0x0
        void* key;     // +0x4
        void* value;   // +0x8
    };
}

// FUN_063525f0. The array must be zeroed (FUN_06343f80 = calloc): an empty
// slot is value == 0.
IVP_VHash_Store::IVP_VHash_Store(int InSize)
{
    SizeMm = InSize - 1;
    Size = InSize;
    NElems = 0;
    PreallocatedElemsStore = nullptr;
    ElemsStore = calloc(static_cast<size_t>(InSize), 0xc);
}

// FUN_06352620 (also reached through thunk_FUN_06352620).
IVP_VHash_Store::~IVP_VHash_Store()
{
    if (ElemsStore != PreallocatedElemsStore && ElemsStore != nullptr)
    {
        free(ElemsStore); // FUN_06343fb0
        ElemsStore = nullptr;
    }
}

// FUN_06352650: regrow and re-insert through the raw insert, then free the
// old array. Called from the raw insert at 50% load.
void IVP_VHash_Store::Rehash(int NewSize)
{
    FVHashStoreElem* oldBuckets = static_cast<FVHashStoreElem*>(ElemsStore);
    int oldCapacity = Size;
    Size = NewSize;
    SizeMm = NewSize - 1;
    ElemsStore = calloc(static_cast<size_t>(NewSize), 0xc); // FUN_06343f80
    int savedCount = NElems;
    if (oldCapacity > 0)
    {
        for (int i = 0; i < oldCapacity; ++i)
        {
            if (oldBuckets[i].value != nullptr)
            {
                AddElem(oldBuckets[i].key, oldBuckets[i].value, oldBuckets[i].hash);
            }
        }
    }
    NElems = savedCount;
    if (oldBuckets != PreallocatedElemsStore && oldBuckets != nullptr)
    {
        free(oldBuckets); // FUN_06343fb0
    }
}

// FUN_063526d0
void IVP_VHash_Store::AddElem(void* Key, void* Value)
{
    unsigned hash = IVP_Crc32HashKey((unsigned)Key) | 0x80000000;
    AddElem(Key, Value, hash);
}

// FUN_06352720: raw Robin Hood insert (HashIndex has its top bit set). The
// carried entry starts as (Key, Value, HashIndex) and is swapped with each
// resident that sits closer to its ideal bucket.
void IVP_VHash_Store::AddElem(void* Key, void* Value, unsigned HashIndex)
{
    if (Size <= NElems * 2)
    {
        Rehash(Size * 2);
    }
    FVHashStoreElem* buckets = static_cast<FVHashStoreElem*>(ElemsStore);
    unsigned mask = static_cast<unsigned>(SizeMm);
    unsigned idx = mask & HashIndex;
    FVHashStoreElem* slot = &buckets[idx];
    NElems = NElems + 1;
    void* residentKey = slot->key;
    unsigned carryIdeal = idx;
    void* carryValue = Value;
    while (residentKey != nullptr)
    {
        unsigned residentHash = slot->hash;
        unsigned residentIdeal = residentHash & mask;
        void* nextCarryValue = carryValue;
        if (static_cast<int>(carryIdeal) < static_cast<int>(residentIdeal))
        {
            nextCarryValue = slot->value;
            slot->key = Key;
            slot->value = carryValue;
            slot->hash = HashIndex;
            carryIdeal = residentIdeal;
            Key = residentKey;
            HashIndex = residentHash;
        }
        mask = static_cast<unsigned>(SizeMm);
        idx = (idx + 1) & mask;
        buckets = static_cast<FVHashStoreElem*>(ElemsStore);
        slot = &buckets[idx];
        carryValue = nextCarryValue;
        residentKey = slot->key;
    }
    slot->key = Key;
    slot->value = carryValue;
    slot->hash = HashIndex;
}

// FUN_063527e0: returns the removed value (the raw erase's EAX).
void* IVP_VHash_Store::RemoveElem(void* Key)
{
    unsigned hash = IVP_Crc32HashKey((unsigned)Key) | 0x80000000;
    return RemoveElem(Key, hash);
}

// FUN_06352830: raw backward-shift erase; returns the removed value. No
// not-found exit: the probe loops until the key is found.
void* IVP_VHash_Store::RemoveElem(void* Key, unsigned HashIndex)
{
    FVHashStoreElem* buckets = static_cast<FVHashStoreElem*>(ElemsStore);
    unsigned mask = static_cast<unsigned>(SizeMm);
    unsigned probe = HashIndex;
    unsigned foundIdx;
    for (;;)
    {
        probe = mask & probe;
        if (((buckets[probe].hash | 0x80000000) == HashIndex) && (buckets[probe].key == Key))
        {
            foundIdx = probe;
            break;
        }
        probe = probe + 1;
    }
    void* removedValue = buckets[foundIdx].value;
    NElems = NElems - 1;

    unsigned vacant = foundIdx;
    unsigned refDist = mask;
    if (buckets[mask].key != nullptr)
    {
        refDist = buckets[mask].hash & mask;
    }
    unsigned scan = (vacant + 1) & mask;
    FVHashStoreElem* scanSlot = &buckets[scan];
    while (scanSlot->key != nullptr)
    {
        unsigned scanIdeal = scanSlot->hash & mask;
        bool doShift = false;
        if (static_cast<int>(vacant) < static_cast<int>(scan))
        {
            if (((static_cast<int>(refDist) <= static_cast<int>(scanIdeal)) &&
                 (scanIdeal != refDist || static_cast<int>(scan) < static_cast<int>(scanIdeal))) ||
                (static_cast<int>(scanIdeal) <= static_cast<int>(vacant)))
            {
                doShift = true;
            }
        }
        else
        {
            if (vacant != mask)
            {
                break;
            }
            if (static_cast<int>(refDist) <= static_cast<int>(scanIdeal))
            {
                doShift = true;
            }
        }
        if (doShift)
        {
            buckets[vacant] = *scanSlot;
            vacant = scan;
        }
        mask = static_cast<unsigned>(SizeMm);
        buckets = static_cast<FVHashStoreElem*>(ElemsStore);
        scan = (scan + 1) & mask;
        scanSlot = &buckets[scan];
    }
    buckets[vacant].key = nullptr;
    buckets[vacant].hash = 0;
    return removedValue;
}

// FUN_06352930
void* IVP_VHash_Store::FindElem(void* Key)
{
    unsigned hash = IVP_Crc32HashKey((unsigned)Key) | 0x80000000;
    return FindElem(Key, hash);
}

// FUN_06352980: raw find. The probe count is bounded by the capacity, as in
// retail.
void* IVP_VHash_Store::FindElem(void* Key, unsigned HashIndex)
{
    int probes = 0;
    if (Size > 0)
    {
        unsigned idx = HashIndex;
        do
        {
            idx = static_cast<unsigned>(SizeMm) & idx;
            FVHashStoreElem* buckets = static_cast<FVHashStoreElem*>(ElemsStore);
            FVHashStoreElem* slot = &buckets[idx];
            probes = probes + 1;
            if (slot->value == nullptr)
            {
                return nullptr;
            }
            if (((slot->hash | 0x80000000) == HashIndex) && (slot->key == Key))
            {
                return slot->value;
            }
            idx = idx + 1;
        } while (probes < Size);
    }
    return nullptr;
}

// FUN_063529e0: update-only (no insert when the key is absent). Used by
// IVP_Controller_Phantom to bump a per-pair reference count.
void IVP_VHash_Store::ChangeElem(void* Key, void* NewValue)
{
    unsigned crc = IVP_Crc32HashKey((unsigned)Key);
    if (Size <= 0)
        return;

    unsigned hash = crc | 0x80000000;
    unsigned idx = hash;
    int probes = 0;
    for (;;)
    {
        idx = static_cast<unsigned>(SizeMm) & idx;
        FVHashStoreElem* buckets = static_cast<FVHashStoreElem*>(ElemsStore);
        FVHashStoreElem* slot = &buckets[idx];
        probes = probes + 1;
        if (slot->value == nullptr)
        {
            return;
        }
        if (((slot->hash | 0x80000000) == hash) && (slot->key == Key))
        {
            slot->value = NewValue;
            return;
        }
        idx = idx + 1;
        if (Size <= probes)
        {
            return;
        }
    }
}
