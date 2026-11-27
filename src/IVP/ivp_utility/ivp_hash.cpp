// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// IVP_Hash bodies (06356a40 / 06356a70 / 06356ac0 / 06356b40) and the string
// compare FUN_063569e0 next to them.

#include "ivp_hash.h"

#include "ivp_crc32.h"

#include <cstdlib>
#include <cstring>

// ============================================================================
// A fixed-key-length chained hash, separate from IVP_VHash_Store
// (ivp_vhash.cpp): entries own malloc'd copies of their key bytes. It uses the
// same CRC-32 table (DAT_063edcdc) but over KeyLength bytes, hence its own
// loop instead of IVP_Crc32HashKey.
//
// Node (KeyLength + 0xc bytes, malloc'd per insert; retail over-allocates by
// 4 bytes, reproduced):
//   +0x00 Next, +0x04 Value, +0x08 KeyLength raw key bytes.
// ============================================================================

struct IVP_Hash_Elem
{
    IVP_Hash_Elem* Next; // +0x00
    unsigned Value;      // +0x04
    // +0x08: the KeyLength key bytes follow inline.
    unsigned char* KeyBytes()
    {
        return reinterpret_cast<unsigned char*>(this + 1);
    }
};
static_assert(sizeof(IVP_Hash_Elem) == 0x8, "IVP_Hash_Elem key bytes start at +0x08");

namespace
{
    // Inlined in Find and Add: CRC-32 over Length key bytes from 0xffffffff,
    // no final inversion. Length <= 0 hashes nothing.
    unsigned HashKeyBytes(const unsigned char* Key, int32_t Length)
    {
        unsigned Crc = 0xffffffffu;
        for (int32_t I = 0; I < Length; ++I)
        {
            Crc = (Crc >> 8) ^ g_IVP_Crc32Table[(Key[I] ^ Crc) & 0xff];
        }
        return Crc;
    }
}

// FUN_06356a40. Buckets = calloc (FUN_06343f80) of BucketCount pointers.
IVP_Hash::IVP_Hash(int32_t Size, int32_t KeySize, unsigned NotFoundValue)
{
    BucketCount = Size;
    KeyLength = KeySize;
    DefaultValue = NotFoundValue;
    Buckets = static_cast<IVP_Hash_Elem**>(calloc(4, (size_t)Size)); // FUN_06343f80
}

// FUN_06356a70: frees every node, then the bucket array (and nulls it). The
// object itself is released by its owner.
IVP_Hash::~IVP_Hash()
{
    for (int32_t I = 0; I < BucketCount; ++I) // signed, count re-read
    {
        IVP_Hash_Elem* Node = Buckets[I];
        while (Node != nullptr)
        {
            IVP_Hash_Elem* Next = Node->Next;
            free(Node); // FUN_06343fb0
            Node = Next;
        }
    }
    if (Buckets != nullptr)
    {
        free(Buckets); // FUN_06343fb0
        Buckets = nullptr;
    }
}

// FUN_06356ac0
unsigned IVP_Hash::Find(const void* Key)
{
    const unsigned char* KeyBytes = static_cast<const unsigned char*>(Key);
    const unsigned Hash = HashKeyBytes(KeyBytes, KeyLength);
    IVP_Hash_Elem* Node = Buckets[Hash % (unsigned)BucketCount]; // unsigned DIV
    while (Node != nullptr)
    {
        if (memcmp(Node->KeyBytes(), KeyBytes, (size_t)KeyLength) == 0) // REPE CMPSB
        {
            return Node->Value;
        }
        Node = Node->Next;
    }
    return DefaultValue;
}

// FUN_06356b40: head insert with no duplicate check (a repeated key shadows
// and leaks the earlier node, as in retail).
void IVP_Hash::Add(const void* Key, unsigned Value)
{
    const unsigned char* KeyBytes = static_cast<const unsigned char*>(Key);
    const unsigned Hash = HashKeyBytes(KeyBytes, KeyLength);
    const unsigned BucketIndex = Hash % (unsigned)BucketCount;

    IVP_Hash_Elem* Node = static_cast<IVP_Hash_Elem*>(malloc((size_t)KeyLength + 0xc)); // FUN_06343f70
    memcpy(Node->KeyBytes(), KeyBytes, (size_t)KeyLength);

    Node->Next = Buckets[BucketIndex];
    Buckets[BucketIndex] = Node;
    Node->Value = Value;
}

// FUN_063569e0. 0 when equal, -1 / 1 at the first differing byte (unsigned).
// A null first string returns 1 against a non-null second (0 when both are
// null); a non-null first against a null second returns -1. The loop is the
// inlined CRT strcmp, two bytes per pass. Caller: LAB_06361580,
// IVP_U_Active_Value_Hash::Compare.
int IVP_CompareStringsNullsLast(const char* First, const char* Second)
{
    if (First == nullptr)
    {
        return Second != nullptr ? 1 : 0;
    }
    if (Second == nullptr)
    {
        return -1;
    }
    const unsigned char* A = reinterpret_cast<const unsigned char*>(First);
    const unsigned char* B = reinterpret_cast<const unsigned char*>(Second);
    for (;;)
    {
        if (A[0] != B[0])
        {
            return A[0] < B[0] ? -1 : 1;
        }
        if (A[0] == 0)
        {
            return 0;
        }
        if (A[1] != B[1])
        {
            return A[1] < B[1] ? -1 : 1;
        }
        if (A[1] == 0)
        {
            return 0;
        }
        A += 2;
        B += 2;
    }
}
