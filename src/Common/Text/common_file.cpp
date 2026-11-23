// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_misc.h"

#include <cstdio>
#include <cstring>

// ============================================================================
// File class — CRT FILE* wrapper, retail layout (sizeof 0x8C, see common_misc.h)

File::File()
{
    // FUN_0630b270. The rest of Name stays uninitialised.
    Name[0] = '\0';
    Handle = nullptr;
    Length = 0;
    Offset = 0;
}

File::~File()
{
    // FUN_0630b290
    close();
}

bool File::open(char const* Path)
{
    // FUN_0630cdc0. A null or empty Path skips the whole body (the result is the existing
    // handle's null-ness). Deviation: retail's copy into Name is unbounded (UB past 0x80
    // bytes); the port stops at the buffer. An already open handle is overwritten without
    // fclose, as retail.
    if (Path != nullptr && Path[0] != '\0')
    {
        size_t Index = 0;
        for (; Index < sizeof(Name) - 1 && Path[Index] != '\0'; ++Index)
            Name[Index] = Path[Index];
        Name[Index] = '\0';

        Handle = fopen(Name, "rb");
        if (Handle != nullptr)
        {
            fseek(Handle, 0, SEEK_END);
            Length = (int)ftell(Handle);
            Offset = 0;
            fseek(Handle, 0, SEEK_SET);
        }
    }
    return Handle != nullptr;
}

void File::close()
{
    // FUN_0630cd40: Offset and Length stay.
    if (Handle != nullptr)
    {
        fclose(Handle);
        Handle = nullptr;
    }
}

int File::read(void* Buffer, int Bytes)
{
    // FUN_0630cec0. Offset is not updated.
    if (Handle == nullptr)
        return 0;
    return (int)fread(Buffer, 1, Bytes, Handle);
}

int File::read(void* Buffer, int OffsetFromStart, int Bytes)
{
    // FUN_06262de0
    seek(OffsetFromStart);
    return read(Buffer, Bytes);
}

int File::read_line(char* Line, int MaxLen)
{
    // FUN_0630cef0. Reads up to MaxLen - 1 chars, stopping at EOF, '\r' or '\n'. EOF with
    // nothing read returns 0. A '\r' not followed by '\n' pushes the peeked char back. The
    // result counts the NUL. With MaxLen == 1 retail holds the Line pointer as the "last
    // char" (never -1 or '\r'), so the result is an empty line and 1.
    int Count = 0;
    if (Handle == nullptr || Line == nullptr || MaxLen <= 0)
        return 0;

    int const Limit = MaxLen - 1;
    int Last = 0; // Limit <= 0: retail holds the Line pointer here - never -1 or '\r'
    if (Limit > 0)
    {
        for (;;)
        {
            Last = fgetc(Handle);
            if (Last == -1 || Last == '\r' || Last == '\n')
                break;
            Line[Count] = (char)Last;
            ++Count;
            if (Count >= Limit)
                break;
        }
    }

    if (Last == -1 && Count == 0)
        return 0;

    Line[Count] = '\0';
    ++Count;
    if (Last == '\r')
    {
        if (fgetc(Handle) != '\n')
            fseek(Handle, -1, SEEK_CUR);
    }
    Offset = (int)ftell(Handle);
    return Count;
}

bool File::is_open()
{
    // FUN_06262d80
    return Handle != nullptr;
}

int File::eof()
{
    // FUN_06262d60 (signed compare).
    return Offset >= Length ? 1 : 0;
}

int File::tell() const
{
    // FUN_06262dd0: the cached Offset; no handle check, no ftell.
    return Offset;
}

int File::seek(int NewOffset)
{
    // FUN_0630ce50. -1 is a pure query. Otherwise Offset changes only for a value in
    // [0, Length]; then a successful fseek returns Offset, and no handle or a failed fseek
    // returns 0.
    if (NewOffset == -1)
        return Offset;
    if (NewOffset != Offset && NewOffset >= 0 && NewOffset <= Length)
        Offset = NewOffset;
    if (Handle != nullptr)
    {
        if (fseek(Handle, Offset, SEEK_SET) == 0)
            return Offset;
    }
    return 0;
}

int File::get_length()
{
    // FUN_06262d50 (ICF-shared with several archetype getters).
    return Length;
}

void File::get_info()
{
    // FUN_0630cd70. No handle check in retail (callers only reach it with an open handle).
    fseek(Handle, 0, SEEK_END);
    Length = (int)ftell(Handle);
    Offset = 0;
    fseek(Handle, 0, SEEK_SET);
}

void File::set_name(char const* NewName)
{
    // FUN_06262d90. Deviation: retail's copy is unbounded (UB past 0x80 bytes); the port stops
    // at the buffer.
    size_t Index = 0;
    for (; Index < sizeof(Name) - 1 && NewName[Index] != '\0'; ++Index)
        Name[Index] = NewName[Index];
    Name[Index] = '\0';
}

int File::set_offset(int NewOffset)
{
    // FUN_06262db0 (signed compares).
    if (NewOffset >= 0 && NewOffset <= Length)
        Offset = NewOffset;
    return Offset;
}

char const* File::get_name() const
{
    // FUN_06261260 (ICF): Name is at +0.
    return Name;
}

File& File::operator=(File const& Other)
{
    // FUN_06262e10: a raw 0x8C-byte copy; the copy shares the source's FILE* (retail).
    memmove((void*)this, &Other, sizeof(File)); // memmove: self-assignment
    return *this;
}
