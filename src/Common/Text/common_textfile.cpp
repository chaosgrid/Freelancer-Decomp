// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll text-file layer: FileMap (a file mapped, or a buffer copied, into memory),
// LineParser32 (one line and its read cursor) and TextFile32 (FileMap + line counter).
// INI_Reader (common_ini.cpp) reads text INIs through these.

#include "common_ini.h"

#include <stdlib.h>
#include <string.h>
#include <windows.h>

FileMap::FileMap()
    : FileHandle(INVALID_HANDLE_VALUE)
    , MappingHandle(nullptr)
    , View(nullptr)
    , Size(0)
    , Cursor(0)
{
    // .text:06263040
}

FileMap::~FileMap()
{
    // .text:06263310
    close();
}

FileMap::FileMap(FileMap const& Other)
    : FileName(Other.FileName)
    , FileHandle(Other.FileHandle)
    , MappingHandle(Other.MappingHandle)
    , View(Other.View)
    , Size(Other.Size)
    , Cursor(Other.Cursor)
{
    // .text:06262ac0: the handles are copied verbatim (shared, not duplicated).
}

FileMap& FileMap::operator=(FileMap const& Other)
{
    // .text:06262c00. Nothing previously held by `this` is closed.
    FileName = Other.FileName;
    FileHandle = Other.FileHandle;
    MappingHandle = Other.MappingHandle;
    View = Other.View;
    Size = Other.Size;
    Cursor = Other.Cursor;
    return *this;
}

void FileMap::close()
{
    // .text:06262630.
    FileName.clear();
    if (FileHandle != INVALID_HANDLE_VALUE)
    {
        if (MappingHandle != nullptr)
        {
            if (View != nullptr)
            {
                UnmapViewOfFile(View);
                View = nullptr;
                Size = 0;
            }
            CloseHandle(MappingHandle);
            MappingHandle = nullptr;
        }
        CloseHandle(FileHandle);
        FileHandle = INVALID_HANDLE_VALUE;
    }
    if (View != nullptr)
    {
        free(View);
        View = nullptr;
        Size = 0;
    }
    Cursor = 0;
}

bool FileMap::open(char const* Path)
{
    // .text:06262750
    close();
    FileName = Path;

    FileHandle = CreateFileA(Path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (FileHandle != INVALID_HANDLE_VALUE)
    {
        MappingHandle = CreateFileMappingA(FileHandle, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (MappingHandle != nullptr)
        {
            Size = GetFileSize(FileHandle, nullptr);
            View = (char*)MapViewOfFile(MappingHandle, FILE_MAP_READ, 0, 0, Size);
            if (View != nullptr)
            {
                Cursor = 0;
                return true;
            }
        }
    }

    close();
    return false;
}

bool FileMap::open_memory(void const* Buffer, unsigned int Bytes)
{
    // .text:06262980. No close() first; the malloc result is not checked.
    if (Buffer == nullptr || Bytes == 0)
        return false;

    View = (char*)malloc(Bytes);
    memcpy(View, Buffer, Bytes);
    Size = Bytes;
    Cursor = 0;
    return true;
}

void FileMap::seek(unsigned int Pos)
{
    // .text:06262a10.
    if (View == nullptr)
        return;
    Cursor = (Pos <= Size) ? Pos : Size;
}

unsigned int FileMap::tell() const
{
    // .text:06262a00.
    if (View != nullptr)
        return Cursor;
    return 0;
}

bool FileMap::eof() const
{
    // .text:062629f0: no View check.
    return !(Cursor < Size);
}

unsigned int FileMap::get_file_length() const
{
    // .text:062629e0
    return Size;
}

char const* FileMap::get_file_name() const
{
    // .text:062629d0
    return FileName.c_str();
}

void* FileMap::request_pointer(unsigned int& Bytes)
{
    // .text:06262a30.
    if (View != nullptr && Cursor < Size)
    {
        unsigned int const Remain = Size - Cursor;
        if (Bytes > Remain)
            Bytes = Remain;
        unsigned int const Start = Cursor;
        Cursor = Start + Bytes;
        return View + Start;
    }
    Bytes = 0;
    return nullptr;
}

unsigned int FileMap::read(void* Dest, unsigned int Bytes)
{
    // .text:06262a70. Dest is not checked.
    if (View == nullptr || Cursor >= Size)
        return 0;

    unsigned int const Remain = Size - Cursor;
    if (Bytes > Remain)
        Bytes = Remain;

    char const* const Src = View + Cursor;
    Cursor += Bytes;
    if (Src != nullptr)
        memcpy(Dest, Src, Bytes);
    return Bytes;
}

namespace
{
    // The three read_ptr bodies: an inlined request_pointer; a short read nulls the out pointer.
    template <typename T>
    bool ReadBiniPtr(FileMap& Map, T*& Out, unsigned int Want)
    {
        unsigned int Got = Want;
        Out = (T*)Map.request_pointer(Got);
        if (Got != Want)
        {
            Out = nullptr;
            return false;
        }
        return true;
    }
}

bool FileMap::read_ptr(BINI_Header*& Out)
{
    // .text:0626c010
    return ReadBiniPtr(*this, Out, 0x0c);
}

bool FileMap::read_ptr(BINI_Block*& Out)
{
    // .text:0626c060
    return ReadBiniPtr(*this, Out, 4);
}

bool FileMap::read_ptr(BINI_Value*& Out)
{
    // .text:0626c0b0
    return ReadBiniPtr(*this, Out, 3);
}

void FileMap::handoff_to(FileMap& Other)
{
    // .text:062624c0. What Other held before is NOT closed; `this` keeps its name, Size and
    // Cursor. No self-check: handing off to itself drops the handles.
    Other.FileName = FileName;
    Other.FileHandle = FileHandle;
    Other.MappingHandle = MappingHandle;
    Other.View = View;
    Other.Size = Size;
    Other.Cursor = Cursor;

    View = nullptr;
    MappingHandle = nullptr;
    FileHandle = INVALID_HANDLE_VALUE;
}

// ---------------------------------------------------------------------------------------------
// LineParser32
// ---------------------------------------------------------------------------------------------

LineParser32::LineParser32()
{
    // .text:06262e50. Length +0x400, Position +0x404.
    Buffer[0] = '\0';
    Position = 0;
    Length = 0;
}

void LineParser32::clear()
{
    // .text:06262e30
    Buffer[0] = '\0';
    Position = 0;
    Length = 0;
}

void LineParser32::set(char const* Text, unsigned int Count)
{
    // .text:06262e70. Unclamped: a Count past 0x3ff overruns the buffer as in retail.
    memcpy(Buffer, Text, Count);
    Buffer[Count] = '\0';
    Length = Count;
    Position = 0;
}

bool LineParser32::whitespace(char Ch) const
{
    // .text:06262eb0
    return Ch == ' ' || Ch == '\t';
}

void LineParser32::skip_white()
{
    // .text:06262ed0. No Length bound; the NUL at Buffer[Length] stops it.
    while (Buffer[Position] == ' ' || Buffer[Position] == '\t')
        Position += 1;
}

char LineParser32::get_char() const
{
    // .text:06262ef0
    if (Position < Length)
        return Buffer[Position];
    return '\0';
}

bool LineParser32::skip_char(char Ch)
{
    // .text:06262f10.
    if (Position < Length && Buffer[Position] == Ch)
    {
        Position += 1;
        return true;
    }
    return false;
}

int LineParser32::find_char(char Ch) const
{
    // .text:06262f40
    for (unsigned int Index = Position; Index < Length; Index += 1)
    {
        if (Buffer[Index] == Ch)
            return (int)Index;
    }
    return -1;
}

unsigned int LineParser32::read_string(char* Dest, char const* Terminators)
{
    // .text:06262f70. Position moves over the terminator, or past the NUL when there was none.
    // No Length bound and no null checks.
    char const* const Start = Buffer + Position;
    unsigned int const Count = (unsigned int)strcspn(Start, Terminators);
    memcpy(Dest, Start, Count);
    Dest[Count] = '\0';
    Position += Count + 1u;
    return Count;
}

bool LineParser32::terminate_line(char Ch)
{
    // .text:06262fd0
    for (unsigned int Index = Position; Index < Length; Index += 1)
    {
        if (Buffer[Index] == Ch)
        {
            Length = Index;
            return true;
        }
    }
    return false;
}

unsigned int LineParser32::fetch_string(char* Dest, unsigned int EndPos)
{
    // .text:0630f170. Position is not advanced. A negative Count copies (unsigned)Count bytes
    // as retail does.
    int Count = (int)(EndPos - Position);
    if (Count > 0)
    {
        char const* Last = Buffer + Position + Count - 1;
        while (*Last == ' ' || *Last == '\t')
        {
            Count -= 1;
            Last -= 1;
            if (Count <= 0)
                break;
        }
    }
    memcpy(Dest, Buffer + Position, (unsigned int)Count);
    Dest[Count] = '\0';
    return (unsigned int)Count;
}

unsigned int LineParser32::read_terminator(char* Dest, char Term)
{
    // .text:0630f1d0. Position is left ON the terminator; trailing white space is dropped from
    // the copy only.
    unsigned int const Remain = Length - Position;
    unsigned int Count = 0;
    if (Remain != 0)
    {
        do
        {
            if (Buffer[Position + Count] == Term)
                break;
            Count += 1;
        } while (Count < Remain);
    }
    char const* const Start = Buffer + Position;
    Position += Count;
    while (Count != 0 && (Start[Count - 1u] == ' ' || Start[Count - 1u] == '\t'))
        Count -= 1;
    memcpy(Dest, Start, Count);
    Dest[Count] = '\0';
    return Count;
}

unsigned int LineParser32::read_terminator2(char* Dest, char TermA, char TermB)
{
    // .text:0630f250: like read_terminator with two terminators, but every byte is stored
    // into Dest while scanning and the trailing ' ' / '\t' trim reads Dest.
    unsigned int const Remain = Length - Position;
    unsigned int Count = 0;
    if (Remain != 0)
    {
        do
        {
            char const Ch = Buffer[Position + Count];
            if (Ch == TermA || Ch == TermB)
                break;
            Dest[Count] = Ch;
            Count += 1;
        } while (Count < Remain);
    }
    Position += Count;
    while (Count != 0 && (Dest[Count - 1u] == ' ' || Dest[Count - 1u] == '\t'))
        Count -= 1;
    Dest[Count] = '\0';
    return Count;
}

LineParser32& LineParser32::operator=(LineParser32 const& Other)
{
    // .text:06263010: no self check.
    memmove(Buffer, Other.Buffer, sizeof(Buffer));
    Length = Other.Length;
    Position = Other.Position;
    return *this;
}

// ---------------------------------------------------------------------------------------------
// TextFile32 (FileMap + line counter)
// ---------------------------------------------------------------------------------------------

TextFile32::TextFile32()
{
    // .text:06263040, the FileMap ctor itself (ICF): LineNum (+0x24) is not written.
}

TextFile32::TextFile32(TextFile32 const& Other)
    : FileMap(Other)
    , LineNum(Other.LineNum)
{
    // .text:06263070
}

TextFile32::~TextFile32()
{
    // .text:06263310, the FileMap dtor itself (ICF).
}

TextFile32& TextFile32::operator=(TextFile32 const& Other)
{
    // .text:062631b0
    FileMap::operator=(Other);
    LineNum = Other.LineNum;
    return *this;
}

unsigned int TextFile32::get_line_num() const
{
    // .text:06263030
    return LineNum;
}

bool TextFile32::read_line(LineParser32& Line)
{
    // .text:0630f2c0. LineNum is bumped on every call, failures included. A line is at most a
    // 0x3ff-byte window; a longer one is split there. The pair partner (byte ^ 7: "\r\n" or
    // "\n\r") is consumed only when inside the window.
    unsigned int const Start = (View != nullptr) ? Cursor : 0u;
    LineNum += 1;

    if (View != nullptr && Cursor < Size)
    {
        unsigned int Window = 0x3ff;
        unsigned int const Remain = Size - Cursor;
        if (Remain < Window)
            Window = Remain;
        char const* const Src = View + Cursor;
        Cursor += Window;
        if (Src != nullptr)
        {
            unsigned int TextLength = Window;
            unsigned int Consumed = Window;
            for (unsigned int Index = 0; Index < Window; Index += 1)
            {
                char const Ch = Src[Index];
                if (Ch != '\r' && Ch != '\n')
                    continue;

                TextLength = Index;
                unsigned int Next = Index + 1u;
                if (Next < Window && Src[Next] == (char)(Ch ^ 7))
                    Next += 1u;
                Consumed = Next;
                break;
            }

            memcpy(Line.Buffer, Src, TextLength);
            Line.Buffer[TextLength] = '\0';
            Line.Length = TextLength;
            Line.Position = 0;

            unsigned int const NewCursor = Start + Consumed;
            if (View != nullptr)
                Cursor = (NewCursor <= Size) ? NewCursor : Size;
            return true;
        }
    }

    Line.Buffer[0] = '\0';
    Line.Position = 0;
    Line.Length = 0;
    return false;
}
