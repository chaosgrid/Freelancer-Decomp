// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll StringCache (retail StringCache.cpp): the static string pool, its case-insensitive
// intern index and the static owner whose destructor drops that index, plus StringAlloc,
// StringShutdown and CacheString::clear.
//
// Static-initialisation order: GStringCacheOwner must be constructed (its atexit destructor
// registered) before the CacheString globals of the goods TUs (EquipDesc::CARGO_BAY_HP_NAME),
// so this TU keeps common_ini.cpp's slot in Common.vcxproj, first among its siblings.

#include "common_ini.h"
#include "common_archetype.h" // CacheString (clear lives with the StringCache pool)

#include "dacom.h"

#include <string.h>

#include <map>
#include <utility>

// common_dacom_stricmp.cpp: DACOM.DLL!stricmp, the import retail's StringCache tree calls.
int CallDacomStricmp(const char* Left, const char* Right);

namespace
{
    const char StringCacheSource[] = "E:\\FL\\Scratch\\Source\\Common\\StringCache.cpp";

    // StringCache.cpp. One static pool of 0x180000 bytes (.data 0640a810..0658a80f; the overflow
    // message prints it as 0x600 KB). Byte 0 is the shared empty string: StringAlloc(null / "")
    // and CacheString::clear both hand it out. Interned strings grow up from the bottom cursor,
    // temporary ones down from the top cursor; nothing is ever freed or moved, so every pointer
    // stays valid until the process ends (StringShutdown only drops the index).
    const unsigned int StringCachePoolBytes = 0x180000;
    char GStringCachePool[StringCachePoolBytes];                // DAT_0640a810
    unsigned int GStringCacheBottom = 1;                         // DAT_063edb6c (.data = 1)
    unsigned int GStringCacheTop = StringCachePoolBytes;         // DAT_063edb70 (.data = 0x180000)
    unsigned int GStringCacheInternedCount = 1;                  // DAT_063edb74 (.data = 1)
    unsigned int GStringCacheTemporaryCount = 0;                 // DAT_0658a810

    // The intern index (DAT_0658a814): a unique map keyed by the pooled copy, ordered by DACOM
    // stricmp (06333ea0 find, 06333f30 insert, 06333af0 clear), so interning is
    // case-insensitive. The mapped value is the pooled pointer itself.
    struct FStringCacheLess
    {
        bool operator()(char const* Left, char const* Right) const
        {
            return CallDacomStricmp(Left, Right) < 0;
        }
    };

    typedef std::map<char const*, char*, FStringCacheLess> FStringCacheIndex;
    FStringCacheIndex* GStringCacheIndex = nullptr;              // DAT_0658a814

    // FUN_06333bb0 (thiscall on the StringCache object at DAT_0640a800, which it never reads):
    // operator new of the index the first time an interned string is requested.
    void EnsureStringCacheIndex()
    {
        if (GStringCacheIndex == nullptr)
            GStringCacheIndex = new FStringCacheIndex();
    }

    // Shared by StringShutdown and the atexit destructor LAB_06333ac0. The pool and its cursors
    // stay.
    void DestroyStringCacheIndex()
    {
        if (GStringCacheIndex == nullptr)
            return;
        delete GStringCacheIndex;
        GStringCacheIndex = nullptr;
    }

    // The static StringCache object at DAT_0640a800. Its static initializer LAB_06333ab0 only
    // registers LAB_06333ac0 with atexit; this destructor is that registration.
    struct FStringCacheOwner
    {
        ~FStringCacheOwner()
        {
            DestroyStringCacheIndex();
        }
    };

    FStringCacheOwner GStringCacheOwner;
}

char* __cdecl StringAlloc(char const* Text, bool bTemporary)
{
    // 06333ca0.
    if (Text == nullptr || *Text == '\0')
        return GStringCachePool;

    if (bTemporary)
    {
        // Not interned: the copy goes directly below the top cursor.
        unsigned int const Length = (unsigned int)strlen(Text);
        if (Length + 1u > GStringCacheTop - GStringCacheBottom)
        {
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: StringCache overflow! (%d KB)", StringCacheSource,
                  0xb9, 0x600);
            return GStringCachePool;
        }
        GStringCacheTop -= Length + 1u;
        char* const Copy = GStringCachePool + GStringCacheTop;
        memcpy(Copy, Text, Length + 1u);
        GStringCacheTemporaryCount += 1u;
        return Copy;
    }

    EnsureStringCacheIndex();
    FStringCacheIndex::iterator const Found = GStringCacheIndex->find(Text);
    if (Found != GStringCacheIndex->end() && Found->second != nullptr)
        return Found->second;

    unsigned int const Length = (unsigned int)strlen(Text);
    if (Length + 1u > GStringCacheTop - GStringCacheBottom)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: StringCache overflow! (%d KB)", StringCacheSource,
              0xa9, 0x600);
        return GStringCachePool;
    }

    // Unique insert of {copy, 0}: an existing key (with a null value) keeps its node.
    char* const Copy = GStringCachePool + GStringCacheBottom;
    memcpy(Copy, Text, Length + 1u);
    std::pair<FStringCacheIndex::iterator, bool> const Inserted =
        GStringCacheIndex->insert(FStringCacheIndex::value_type(Copy, nullptr));
    Inserted.first->second = Copy;
    GStringCacheInternedCount += 1u;
    GStringCacheBottom = GStringCacheBottom + Length + 1u;
    return Copy;
}

void CacheString::clear()
{
    // 06333e30: the pool's shared empty string.
    Value = GStringCachePool;
}

void __cdecl StringShutdown()
{
    // 06333c70. The pool cursors are not reset, so later StringAlloc calls keep appending.
    DestroyStringCacheIndex();
}
