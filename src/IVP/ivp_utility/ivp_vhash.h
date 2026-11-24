// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstdlib>

// ============================================================================
// IVP_VHash: the IVP SDK's open-addressing hash of element pointers.
//
// Abstract base vtable .rdata:063a5648 (two slots):
//   slot 0  LAB_06391fb0 (_purecall) -- compare(elem0, elem1), TRUE when equal
//   slot 1  FUN_063522d0 -- scalar deleting destructor -> FUN_063522f0
// Layout (0x10 bytes):
//   +0x04 SizeMm          capacity - 1 (used as the index mask)
//   +0x08 NElemsAndFlags  low 24 bits = element count, top byte = "the element
//                         array is not owned" (the destructor then leaves it)
//   +0x0c Elems           calloc'd array of {hash index | 0x80000000, elem}
//
// The probe / insert / remove / rehash bodies (FUN_06352580 / 063523d0 /
// 06352480 / 06352320) are the members below, defined in ivp_vhash.cpp. They
// dispatch the comparison through slot 0 of the object's own vtable (Compare).
//
// The constructor and destructor stay inline here; the destructor below is
// the non-deleting FUN_063522f0.
// ============================================================================

class IVP_VHash
{
public:
    int SizeMm;              // +0x04
    unsigned NElemsAndFlags; // +0x08
    void* Elems;             // +0x0c

    // FUN_063522a0: Elems = FUN_06343f80(Size, 8) (zeroed allocation).
    explicit IVP_VHash(int Size)
        : SizeMm(Size - 1)
        , NElemsAndFlags(0)
        , Elems(calloc(static_cast<size_t>(Size), 8))
    {
    }

    // slot 0: retail _purecall in the base.
    virtual int Compare(void* Elem0, void* Elem1) = 0;

    // slot 1: FUN_063522d0 -> FUN_063522f0. Stamps the base vtable, then frees
    // Elems (FUN_06343fb0) unless the top byte of +0x08 is set; Elems is nulled
    // only on the freeing path.
    virtual ~IVP_VHash()
    {
        if ((NElemsAndFlags & 0xff000000u) == 0 && Elems != nullptr)
        {
            free(Elems);
            Elems = nullptr;
        }
    }

    // FUN_06352580: the stored element equal to Elem, or null.
    void* FindElem(void* Elem, unsigned HashIndex);

    // FUN_063523d0: insert (grows to twice the size first at half load).
    void AddElem(void* Elem, unsigned HashIndex);

    // FUN_06352480: removes and returns the stored element equal to Elem.
    // Retail writes to address 0 when the element is absent.
    void* RemoveElem(void* Elem, unsigned HashIndex);

protected:
    // FUN_06352320: regrow to exactly NewSize slots and re-insert every
    // stored element.
    void Rehash(int NewSize);
};
static_assert(sizeof(IVP_VHash) == 0x10, "IVP_VHash is 0x10 bytes");

// FUN_0635d4d0 (thiscall with an unused receiver): CRC-32 (DAT_063edcdc) over the four bytes of Key, OR
// 0x80000000 -- the called copy of the loop the IVP_VHash_Store members
// inline. Caller: ivp_cluster_manager.cpp. Defined in ivp_vhash.cpp.
unsigned __fastcall IVP_VHash_HashPointer(unsigned Key);

// ============================================================================
// IVP_VHash_Store: the IVP SDK's key -> value store (no vtable, 0x14 bytes).
// Robin Hood open addressing over 12-byte entries {hash | 0x80000000, key,
// value}; the key is hashed through the CRC-32 table DAT_063edcdc. A zero
// value marks an empty slot.
// Layout (FUN_063525f0):
//   +0x00 Size        capacity (power of two)
//   +0x04 SizeMm      Size - 1, the index mask
//   +0x08 NElems      occupied entries
//   +0x0c ElemsStore  calloc'd entry array (FUN_06343f80(Size, 0xc))
//   +0x10 Field_0x10  zeroed by the ctor; the dtor and the grow step leave
//                     ElemsStore alone when it equals this field. No other
//                     writer is known.
// Members defined in ivp_vhash.cpp.
// Users: IVP_Controller_Phantom (ivp_phantom.h, +0x2c / +0x30) and
// IVP_Core::ObjectHashMap (ivp_core.cpp).
// ============================================================================

class IVP_VHash_Store
{
public:
    int Size;          // +0x00
    int SizeMm;        // +0x04
    int NElems;        // +0x08
    void* ElemsStore;  // +0x0c
    void* Field_0x10;  // +0x10

    // FUN_063525f0
    explicit IVP_VHash_Store(int InSize);

    // FUN_06352620. Non-virtual; retail callers run it and then operator
    // delete (e.g. 063442c0).
    ~IVP_VHash_Store();

    // FUN_063526d0: unconditional insert of (Key, Value).
    void AddElem(void* Key, void* Value);

    // FUN_06352720: the raw Robin Hood insert with a caller-supplied hash
    // index (top bit already set).
    void AddElem(void* Key, void* Value, unsigned HashIndex);

    // FUN_063527e0: removes Key and returns its value.
    void* RemoveElem(void* Key);

    // FUN_06352830: the raw backward-shift erase with a caller-supplied hash
    // index; returns the removed value.
    void* RemoveElem(void* Key, unsigned HashIndex);

    // FUN_06352930: the value stored for Key, or null.
    void* FindElem(void* Key);

    // FUN_06352980: the raw find with a caller-supplied hash index.
    void* FindElem(void* Key, unsigned HashIndex);

    // FUN_063529e0: overwrite the value of an existing Key; no insert when
    // Key is absent.
    void ChangeElem(void* Key, void* NewValue);

private:
    // FUN_06352650: regrow to NewSize entries and re-insert every occupied one.
    void Rehash(int NewSize);
};
static_assert(sizeof(IVP_VHash_Store) == 0x14, "IVP_VHash_Store is 0x14 bytes (operator new(0x14), FUN_06344710)");
