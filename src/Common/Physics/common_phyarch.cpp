// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_phyarch.h"

#include "common_ini.h"
#include "DALib.h"
#include "dacom.h"
#include "readfile.h"

#include <map>
#include <new>
#include <stdio.h>
#include <string.h>
#include <utility>
#include <vector>

// IVP's 16-byte aligned block allocator (FUN_06343fc0(size, align)) and its release
// (FUN_06343ff0): ivp_malloc_aligned / ivp_free_aligned.
#include "ivp_utility/ivp_malloc.h"
#include "ivp_collision/ivp_compact_surface.h" // max_deviation_and_size (+0x1c)

int CallDacomStricmp(const char* Left, const char* Right); // common_dacom_stricmp.cpp

namespace
{
    // .rdata:0639b81c
    const char kPhyArchSource[] = "E:\\FL\\Scratch\\Source\\Common\\PhyArch.cpp";
    // .rdata:0639b6f4: the only .sur version LoadSurfaces accepts.
    const float kSurVersion = 2.0f;

    typedef std::map<unsigned, PhyArch::Part> FPartMap;

    // A compact surface's byte size: the dword at +0x1c, arithmetic shift right by 8.
    unsigned PhyArch_SurfaceByteSize(IVP_Compact_Surface const* Surface)
    {
        int const SizeAndFlags = (int)Surface->max_deviation_and_size;   // +0x1c
        return (unsigned)(SizeAndFlags >> 8);
    }

    // IFileSystem slot +0x14 with handle 0 and no position: Read(0, Buffer, Size, &Got, 0).
    int PhyArch_Read(IFileSystem* Filesys, void* Buffer, unsigned long Size, unsigned long* Got)
    {
        return Filesys->Read(0, Buffer, Size, Got, 0);
    }

    // The inline release every LoadSurfaces exit performs: `if (fs) fs->vtbl[2]()`.
    void PhyArch_Release(IFileSystem* Filesys)
    {
        if (Filesys != nullptr)
            Filesys->Release();
    }
}

// FUN_0627f9b0
PhyArch::Part::Part()
    : bNotFixed(1)
    , Surface(nullptr)
{
    // Retail quirk kept: both extents are left unwritten, so a part without an 'exts' chunk
    // keeps whatever the storage held.
}

// FUN_0626c600: the surface pointer is copied shallow; the hardpoint ids get a fresh vector.
PhyArch::Part::Part(Part const& Other)
    : bNotFixed(Other.bNotFixed)
    , Surface(Other.Surface)
    , ExtentsMin(Other.ExtentsMin)
    , ExtentsMax(Other.ExtentsMax)
    , HardpointIds(Other.HardpointIds)
{
}

// FUN_0627f9d0
PhyArch::Part::~Part()
{
    if (Surface != nullptr)
        ivp_free_aligned(Surface); // FUN_06343ff0
}

// FUN_0627fa90
PhyArch::Part& PhyArch::Part::operator=(Part const& Other)
{
    // Retail quirk kept: no self-assignment test, so `a = a` frees the own surface and then
    // copies from the freed block. The pointer is not cleared here.
    if (Surface != nullptr)
        ivp_free_aligned(Surface); // FUN_06343ff0

    bNotFixed = Other.bNotFixed;
    ExtentsMin = Other.ExtentsMin;
    ExtentsMax = Other.ExtentsMax;

    HardpointIds = Other.HardpointIds;

    // A source surface is duplicated into a new 16-byte aligned block of its own size.
    if (Other.Surface != nullptr)
    {
        unsigned const Size = PhyArch_SurfaceByteSize(Other.Surface);
        IVP_Compact_Surface* const Copy = (IVP_Compact_Surface*)ivp_malloc_aligned(Size, 0x10); // FUN_06343fc0
        memcpy(Copy, Other.Surface, Size);
        Surface = Copy;
    }
    // Retail quirk kept: without a source surface, Surface keeps the pointer just freed, so a
    // later destructor or clear() frees it again.
    return *this;
}

// FUN_0627fa30
void PhyArch::Part::clear()
{
    bNotFixed = 1;
    if (Surface != nullptr)
    {
        ivp_free_aligned(Surface); // FUN_06343ff0
        Surface = nullptr;
    }
    ExtentsMin.x = 0.0f;
    ExtentsMin.y = 0.0f;
    ExtentsMin.z = 0.0f;
    ExtentsMax.x = 0.0f;
    ExtentsMax.y = 0.0f;
    ExtentsMax.z = 0.0f;
    HardpointIds.clear();
}

// FUN_0627fc80
bool __cdecl PhyArch::LoadSurfaces(char const* SurPath, std::map<unsigned int, Part>& Parts)
{
    // A "DOS" FileSystem over SurPath through DALib::Data's CreateInstance.
    FFileSystemDesc Desc;
    memset(&Desc, 0, sizeof(Desc));
    Desc.Size = offsetof(FFileSystemDesc, MemBuffer);
    Desc.ComponentName = "FileSystem";
    Desc.Implementation = "DOS";
    Desc.Path = SurPath;
    Desc.Access = GENERIC_READ;
    Desc.ShareMode = FILE_SHARE_READ;
    Desc.Creation = OPEN_EXISTING;
    Desc.Flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN;
    Desc.FindHandle = 0xFFFFFFFF;
    IFileSystem* Filesys = nullptr;
    // FFileSystemDesc begins with AGGDESC's {size, interface name} (readfile.h).
    if (DALib::Data->CreateInstance((AGGDESC*)&Desc, (void**)&Filesys) != 0)
    {
        PhyArch_Release(Filesys);
        return false;
    }

    // A failed or short read fails silently. The version is preset to 1.0f and read only
    // after a "vers" tag.
    unsigned long Got = 0;
    char Tag[5];
    float Version = 1.0f;
    int ReadResult = PhyArch_Read(Filesys, Tag, 4, &Got);
    if (ReadResult == 0 || Got != 4)
    {
        PhyArch_Release(Filesys);
        return false;
    }
    Tag[4] = '\0';
    if (CallDacomStricmp(Tag, "vers") == 0)
    {
        ReadResult = PhyArch_Read(Filesys, &Version, 4, &Got);
        if (ReadResult == 0 || Got != 4)
        {
            PhyArch_Release(Filesys);
            return false;
        }
    }

    // Only an exact 2.0f continues (NaN fails).
    if (!(Version == kSurVersion))
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s uses old format, must be rebuilt to version %f.  Non-ignorable.",
              kPhyArchSource, 0xb1, SurPath, (double)kSurVersion);
        PhyArch_Release(Filesys);
        return false;
    }

    // The unchecked reads below land in variables retail never re-initialises, so a failed
    // read keeps the previous part's or chunk's value. Retail leaves them uninitialised before
    // the first read; the port starts them at 0.
    unsigned PartId = 0;
    int ChunkCount = 0;
    unsigned SurfaceSize = 0;
    int HardpointCount = 0;
    char ChunkTag[5] = {};

    for (;;)
    {
        // A failed or short read of the next part id ends the file successfully.
        ReadResult = PhyArch_Read(Filesys, &PartId, 4, &Got);
        if (ReadResult == 0 || Got != 4)
        {
            PhyArch_Release(Filesys);
            return true;
        }

        std::pair<FPartMap::iterator, bool> const Inserted = Parts.insert(FPartMap::value_type(PartId, Part()));
        Part& Current = Inserted.first->second;
        if (!Inserted.second)
        {
            // A duplicate id is reported and the existing part cleared.
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: Error reading %s. Duplicate part ids: %u. Second used.",
                  kPhyArchSource, 0xc6, SurPath, PartId);
            Current.clear();
        }

        // Read without a result test; the compare is signed.
        PhyArch_Read(Filesys, &ChunkCount, 4, &Got);
        for (int Chunk = 0; Chunk < ChunkCount; ++Chunk)
        {
            PhyArch_Read(Filesys, ChunkTag, 4, &Got);
            ChunkTag[4] = '\0';

            if (CallDacomStricmp(ChunkTag, "surf") == 0)
            {
                // A previous surface is not freed (leak, as retail). Neither read is tested.
                unsigned long SurfGot = 0;
                PhyArch_Read(Filesys, &SurfaceSize, 4, &SurfGot);
                IVP_Compact_Surface* const SurfaceBlock =
                    (IVP_Compact_Surface*)ivp_malloc_aligned(SurfaceSize, 0x10); // FUN_06343fc0
                PhyArch_Read(Filesys, SurfaceBlock, SurfaceSize, &SurfGot);
                Current.Surface = SurfaceBlock;
            }
            else if (CallDacomStricmp(ChunkTag, "exts") == 0)
            {
                PhyArch_Read(Filesys, &Current.ExtentsMin, 0xc, &Got);
                PhyArch_Read(Filesys, &Current.ExtentsMax, 0xc, &Got);
            }
            else if (CallDacomStricmp(ChunkTag, "!fxd") == 0)
            {
                Current.bNotFixed = 0;
            }
            else if (CallDacomStricmp(ChunkTag, "hpid") == 0)
            {
                // A count, then each id appended unless present.
                PhyArch_Read(Filesys, &HardpointCount, 4, &Got);
                for (int Index = 0; Index < HardpointCount; ++Index)
                {
                    unsigned HardpointId = 0;  // zeroed before every read
                    PhyArch_Read(Filesys, &HardpointId, 4, &Got);
                    std::vector<unsigned>::iterator Found = Current.HardpointIds.begin();
                    for (; Found != Current.HardpointIds.end(); ++Found)
                    {
                        if (*Found == HardpointId)
                            break;
                    }
                    if (Found == Current.HardpointIds.end())
                        Current.HardpointIds.insert(Current.HardpointIds.end(), HardpointId);
                }
            }
            else
            {
                // An unknown chunk aborts the file.
                FDUMP(kFdumpError, "%s(%d) : *** ERROR: File(%s) Part(%u) contains unrecognized info type. Aborting.",
                      kPhyArchSource, 0xfe, SurPath, PartId);
                PhyArch_Release(Filesys);
                return false;
            }
        }
    }
}
