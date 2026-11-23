// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// IVP_VHash / IVP_VHash_Store bodies and the CRC-32 table they share with
// IVP_Hash and the active-value hash.

#include "ivp_vhash.h"

#include "ivp_crc32.h"

#include <cstdlib>

// ============================================================================
// The CRC-32 table (retail DAT_063edcdc), filled by a dynamic initializer.
// No load-time reader exists.
// ============================================================================

namespace
{
    unsigned InitCrc32Table_063edcdc(unsigned (&table)[256])
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

unsigned g_Crc32Table_063edcdc[256];

namespace
{
    unsigned g_Crc32Table_063edcdc_init = InitCrc32Table_063edcdc(g_Crc32Table_063edcdc);
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

// FUN_06352320: grows to exactly param_1 buckets and re-inserts every entry
// through AddElem (which bumps the count), then restores the pre-rehash count
// and frees the old array unless the "not owned" flag is set.
void IVP_VHash::Rehash(int param_1)
{
    int oldMask = SizeMm;
    FVHashElem* oldBuckets = static_cast<FVHashElem*>(Elems);
    SizeMm = param_1 - 1;
    Elems = calloc(static_cast<size_t>(param_1), 0x8); // FUN_06343f80
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
// param_1 = value, param_2 = hash (already | 0x80000000).
void IVP_VHash::AddElem(void* param_1, unsigned param_2)
{
    int mask = SizeMm;
    int doubledCount = static_cast<int>((NElemsAndFlags & 0xffffffu)) * 2;
    if (doubledCount != mask && mask <= doubledCount)
    {
        Rehash(mask * 2 + 2);
    }

    void* value = param_1;
    unsigned hash = param_2;

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

// FUN_06352480: probe from hash & mask, calling Compare(stored, param_1) on
// each tag match, then backward-shift deletion. param_1 = query, param_2 =
// hash.
void* IVP_VHash::RemoveElem(void* param_1, unsigned param_2)
{
    unsigned idx = param_2 & static_cast<unsigned>(SizeMm);
    FVHashElem* slot;
    for (;;)
    {
        FVHashElem* buckets = static_cast<FVHashElem*>(Elems);
        slot = &buckets[idx];
        if (slot->value == nullptr)
        {
            // Retail writes to address 0 when the element is absent.
            *reinterpret_cast<volatile int*>(0) = 0;
        }
        if (((slot->hashTag | 0x80000000u) == param_2))
        {
            if (Compare(slot->value, param_1) == 1)
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
void* IVP_VHash::FindElem(void* param_1, unsigned param_2)
{
    unsigned idx = param_2 & static_cast<unsigned>(SizeMm);
    FVHashElem* buckets = static_cast<FVHashElem*>(Elems);
    FVHashElem* slot = &buckets[idx];
    void* value = slot->value;

    for (;;)
    {
        if (value == nullptr)
        {
            return nullptr;
        }
        if ((slot->hashTag | 0x80000000u) == param_2)
        {
            if (Compare(value, param_1) != 0)
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
unsigned __fastcall IVP_VHash_HashPointer(unsigned param_1)
{
    return HashKey_063edcdc(param_1) | 0x80000000u;
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
IVP_VHash_Store::IVP_VHash_Store(int param_1)
{
    SizeMm = param_1 - 1;
    Size = param_1;
    NElems = 0;
    Field_0x10 = nullptr;
    ElemsStore = calloc(static_cast<size_t>(param_1), 0xc);
}

// FUN_06352620 (also reached through thunk_FUN_06352620).
IVP_VHash_Store::~IVP_VHash_Store()
{
    if (ElemsStore != Field_0x10 && ElemsStore != nullptr)
    {
        free(ElemsStore); // FUN_06343fb0
        ElemsStore = nullptr;
    }
}

// FUN_06352650: regrow and re-insert through the raw insert, then free the
// old array. Called from the raw insert at 50% load.
void IVP_VHash_Store::Rehash(int param_1)
{
    FVHashStoreElem* oldBuckets = static_cast<FVHashStoreElem*>(ElemsStore);
    int oldCapacity = Size;
    Size = param_1;
    SizeMm = param_1 - 1;
    ElemsStore = calloc(static_cast<size_t>(param_1), 0xc); // FUN_06343f80
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
    if (oldBuckets != Field_0x10 && oldBuckets != nullptr)
    {
        free(oldBuckets); // FUN_06343fb0
    }
}

// FUN_063526d0
void IVP_VHash_Store::AddElem(void* param_1, void* param_2)
{
    unsigned hash = HashKey_063edcdc((unsigned)param_1) | 0x80000000;
    AddElem(param_1, param_2, hash);
}

// FUN_06352720: raw Robin Hood insert. param_1 = key, param_2 = value,
// param_3 = hash (top bit already set).
void IVP_VHash_Store::AddElem(void* param_1, void* param_2, unsigned param_3)
{
    if (Size <= NElems * 2)
    {
        Rehash(Size * 2);
    }
    FVHashStoreElem* base = static_cast<FVHashStoreElem*>(ElemsStore);
    unsigned uVar7 = static_cast<unsigned>(SizeMm);
    unsigned uVar5 = uVar7 & param_3;
    FVHashStoreElem* puVar1 = &base[uVar5];
    NElems = NElems + 1;
    void* uVar2 = puVar1->key;
    unsigned uVar6 = uVar5;
    void* uVar4 = param_2;
    while (uVar2 != nullptr)
    {
        unsigned uVar3 = puVar1->hash;
        uVar7 = uVar3 & uVar7;
        param_2 = uVar4;
        if (static_cast<int>(uVar6) < static_cast<int>(uVar7))
        {
            param_2 = puVar1->value;
            puVar1->key = param_1;
            puVar1->value = uVar4;
            puVar1->hash = param_3;
            uVar6 = uVar7;
            param_1 = uVar2;
            param_3 = uVar3;
        }
        uVar7 = static_cast<unsigned>(SizeMm);
        uVar5 = (uVar5 + 1) & uVar7;
        base = static_cast<FVHashStoreElem*>(ElemsStore);
        puVar1 = &base[uVar5];
        uVar4 = param_2;
        uVar2 = puVar1->key;
    }
    puVar1->key = param_1;
    puVar1->value = uVar4;
    puVar1->hash = param_3;
}

// FUN_063527e0: returns the removed value (the raw erase's EAX).
void* IVP_VHash_Store::RemoveElem(void* param_1)
{
    unsigned hash = HashKey_063edcdc((unsigned)param_1) | 0x80000000;
    return RemoveElem(param_1, hash);
}

// FUN_06352830: raw backward-shift erase; returns the removed value. No
// not-found exit: the probe loops until the key is found.
void* IVP_VHash_Store::RemoveElem(void* param_1, unsigned param_2)
{
    FVHashStoreElem* buckets = static_cast<FVHashStoreElem*>(ElemsStore);
    unsigned mask = static_cast<unsigned>(SizeMm);
    unsigned probe = param_2;
    unsigned foundIdx;
    for (;;)
    {
        probe = mask & probe;
        if (((buckets[probe].hash | 0x80000000) == param_2) && (buckets[probe].key == param_1))
        {
            foundIdx = probe;
            break;
        }
        probe = probe + 1;
    }
    void* removedValue = buckets[foundIdx].value;
    NElems = NElems - 1;

    unsigned uVar7 = foundIdx;
    unsigned uVar5 = mask;
    unsigned refDist = uVar5;
    if (buckets[uVar5].key != nullptr)
    {
        refDist = buckets[uVar5].hash & uVar5;
    }
    unsigned uVar8 = (uVar7 + 1) & uVar5;
    FVHashStoreElem* puVar2 = &buckets[uVar8];
    while (puVar2->key != nullptr)
    {
        unsigned uVar9 = puVar2->hash & uVar5;
        bool doShift = false;
        if (static_cast<int>(uVar7) < static_cast<int>(uVar8))
        {
            if (((static_cast<int>(refDist) <= static_cast<int>(uVar9)) &&
                 (uVar9 != refDist || static_cast<int>(uVar8) < static_cast<int>(uVar9))) ||
                (static_cast<int>(uVar9) <= static_cast<int>(uVar7)))
            {
                doShift = true;
            }
        }
        else
        {
            if (uVar7 != uVar5)
            {
                break;
            }
            if (static_cast<int>(refDist) <= static_cast<int>(uVar9))
            {
                doShift = true;
            }
        }
        if (doShift)
        {
            buckets[uVar7] = *puVar2;
            uVar7 = uVar8;
        }
        mask = static_cast<unsigned>(SizeMm);
        uVar5 = mask;
        buckets = static_cast<FVHashStoreElem*>(ElemsStore);
        uVar8 = (uVar8 + 1) & uVar5;
        puVar2 = &buckets[uVar8];
    }
    buckets[uVar7].key = nullptr;
    buckets[uVar7].hash = 0;
    return removedValue;
}

// FUN_06352930
void* IVP_VHash_Store::FindElem(void* param_1)
{
    unsigned hash = HashKey_063edcdc((unsigned)param_1) | 0x80000000;
    return FindElem(param_1, hash);
}

// FUN_06352980: raw find. The probe count is bounded by the capacity, as in
// retail.
void* IVP_VHash_Store::FindElem(void* param_1, unsigned param_2)
{
    int probes = 0;
    if (Size > 0)
    {
        unsigned idx = param_2;
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
            if (((slot->hash | 0x80000000) == param_2) && (slot->key == param_1))
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
void IVP_VHash_Store::ChangeElem(void* param_1, void* param_2)
{
    unsigned crc = HashKey_063edcdc((unsigned)param_1);
    if (Size > 0)
    {
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
            if (((slot->hash | 0x80000000) == hash) && (slot->key == param_1))
            {
                slot->value = param_2;
                return;
            }
            idx = idx + 1;
            if (Size <= probes)
            {
                return;
            }
        }
    }
}
