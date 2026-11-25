// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_ini.h"
#include "Core/msvcrt_atof.h"

#include "DALib.h"
#include "dacom.h"
#include "msvcrt_compat.h" // MsvcrtAtoi: retail's MSVCRT.DLL atoi wraps, the UCRT saturates

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>

// common_dacom_stricmp.cpp: DACOM.DLL!stricmp, the import retail's StringCache tree and every
// INI_Reader name compare (is_header / is_value / get_value_bool) call.
int CallDacomStricmp(const char* Left, const char* Right);

static_assert(sizeof(INI_Reader::State) == 0xC08, "text INI_Reader::State is cursor+line+3x0x400");
static_assert(offsetof(INI_Reader::State, Text) == 4 && offsetof(INI_Reader::State, Bini) == 4,
              "both State views start after the cursor");
static_assert(offsetof(INI_Reader::State::FBini, ValueIndex) == 4 &&
                  offsetof(INI_Reader::State::FBini, ValueOffset) == 8 &&
                  offsetof(INI_Reader::State::FBini, ParamsOffset) == 0xc,
              "BINI get_state writes +4 dword, +8 word, +0xc dword, +0x10 dword");
static_assert(offsetof(FBiniParam, Int) == 1, "BINI param payload follows the type byte");
static_assert(sizeof(LineParser32) == 0x408, "LineParser32 is Buffer[0x400] + Length + Position");
static_assert(sizeof(Vector) == 12, "Vector is 3 floats");
static_assert(sizeof(BINI_Header) == 0xC, "BINI_Header is 0xC");
static_assert(sizeof(BINI_Block) == 4, "BINI_Block is 4");
static_assert(sizeof(BINI_Value) == 3, "BINI_Value is 3");
static_assert(sizeof(FBiniParam) == 5, "BINI param is type + 4-byte payload");

namespace
{
    const char IniSource[] = "E:\\FL\\Scratch\\Source\\Common\\Ini.cpp";
    const char ParseSource[] = "E:\\FL\\Scratch\\Source\\Common\\Parse.cpp";
    const unsigned int BiniMagic = 0x494E4942; // 'BINI'
    const unsigned int BiniHeaderBytes = 0xC;
    const unsigned int BiniParamBytes = sizeof(FBiniParam); // 5
    const char EmptyC[] = ""; // .rdata:063997b0

    char GStringIndexScratch[0x400];

    // DAT_063fd198: the text-mode get_line_ptr buffer. Retail's is 0x100 bytes and is filled
    // by an unbounded sprintf("%s = %s") -- a line past 0xff chars overruns it (undefined
    // behaviour). Name and parameters are each < 0x400 chars, so this size holds any line.
    char GTextLineScratch[0x808];

    // DAT_0658e800: the BINI get_line_ptr buffer, 0x100 bytes in retail and likewise
    // overrun by long values (undefined behaviour); the port truncates past this size.
    char GBiniLineScratch[0x2000];

    // DAT_0658e900 (0x100 bytes): the "%d" / "%g" text of a numeric BINI parameter.
    char GBiniNumScratch[0x100];

    // DAT_063fcf98 (0x100 wchar_t): the BINI get_value_wstring result, written by
    // mbstowcs(..., 0x100). The extra element is never written and keeps the result
    // terminated when mbstowcs fills all 0x100 (which in retail needs a join that has
    // already overrun its 0x100-byte buffer).
    wchar_t GBiniWideScratch[0x101];

    ILoadingScreen* GLoadingScreen = nullptr;

    // .data:063ed4a8 (128 bytes, character classes) immediately followed by .data:063ed528
    // (128 bytes, digit values), copied from the PE. Class bits: 0x01 letter, 0x02 upper,
    // 0x04 lower, 0x08 decimal digit, 0x10 hex digit, 0x20 identifier, 0x40 white space
    // (\t \n \r ' '), 0x80 operator. Retail indexes the class table with (c & 0x7f), and the
    // digit table with the SIGNED char - a byte >= 0x80 reads the class table that precedes
    // it; GParseTables keeps both in one array so that read is the same.
    unsigned char const GParseTables[256] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x40, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x40, 0x80, 0x00, 0x20, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0x80, 0x00, 0x80, 0x00, 0x80,
        0x38, 0x38, 0x38, 0x38, 0x38, 0x38, 0x38, 0x38, 0x38, 0x38, 0x00, 0x00, 0x80, 0x80, 0x80, 0x00,
        0x00, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23,
        0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x00, 0x00, 0x00, 0x80, 0x20,
        0x00, 0x35, 0x35, 0x35, 0x35, 0x35, 0x35, 0x25, 0x25, 0x25, 0x25, 0x25, 0x25, 0x25, 0x25, 0x25,
        0x25, 0x25, 0x25, 0x25, 0x25, 0x25, 0x25, 0x25, 0x25, 0x25, 0x25, 0x00, 0x80, 0x00, 0x80, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

    unsigned char const ParseClassSpace = 0x40;
    unsigned char const ParseClassDigit = 0x08;
    unsigned char const ParseClassHex = 0x10;

    bool HasParseClass(char Ch, unsigned char Bits)
    {
        return (GParseTables[Ch & 0x7f] & Bits) != 0;
    }

    int ParseDigitValue(char Ch)
    {
        return GParseTables[0x80 + (signed char)Ch];
    }

    // ParseNumber's base-N accumulation. The digit compare is signed. The sign is applied by
    // the caller. Retail keeps Value on the x87 stack for the whole loop (80-bit); the port
    // accumulates in double.
    double ParseBaseN(char const* Begin, char const* End, int Base, char const* Original)
    {
        double Value = 0.0;
        double Frac = 0.0;
        for (char const* Cursor = Begin; Cursor < End; Cursor += 1)
        {
            if (*Cursor == '.')
            {
                Frac = Base;
                continue;
            }
            int const Digit = ParseDigitValue(*Cursor);
            if (Digit >= Base)
            {
                FDUMP(kFdumpWarning,
                      "%s(%d) : *** WARNING: ParseNumber: illegal BASE-N digit(s) in argument! '%s'",
                      ParseSource, 0x14e, Original);
                break;
            }
            if (!(Frac > 0.0))
            {
                Value = Value * Base + Digit;
            }
            else
            {
                Value = Value + Digit / Frac;
                Frac = Frac * Base;
            }
        }
        return Value;
    }

    // MSVCRT tolower in the "C" locale, on the sign-extended char retail passes (MOVSX).
    int ParseToLower(char Ch)
    {
        int const Value = (signed char)Ch;
        if (Value >= 'A' && Value <= 'Z')
            return Value + ('a' - 'A');
        return Value;
    }

    // strcat into a fixed buffer, truncating at its end. Stands in for retail's unbounded
    // strcat into the 0x100-byte join / line buffers (see the buffer notes above).
    void AppendBounded(char* Dest, unsigned int DestChars, char const* Src)
    {
        size_t const Used = strlen(Dest);
        size_t Add = strlen(Src);
        if (Used + Add >= DestChars)
            Add = DestChars - 1u - Used;
        memcpy(Dest + Used, Src, Add);
        Dest[Used + Add] = '\0';
    }
}

char const* __cdecl StringIndex(char const* Text, unsigned int Index)
{
    // 0631be30. Returns field Index of a comma list, white space trimmed, in DAT_06401350; a
    // missing field gives "" (only byte 0 is written). No null check of Text. Retail's copy
    // is unbounded (a field of 0x400+ bytes overflows - undefined behaviour); the port copies
    // at most 0x3ff bytes.
    char const* Cursor = Text;
    unsigned int Remain = Index;
    for (;;)
    {
        while (HasParseClass(*Cursor, ParseClassSpace))
            Cursor += 1;

        char const* const Comma = strchr(Cursor, ',');
        if (Remain != 0)
        {
            if (Comma == nullptr)
            {
                GStringIndexScratch[0] = '\0';
                return GStringIndexScratch;
            }
            Cursor = Comma + 1;
            Remain -= 1;
            continue;
        }

        char const* End = Comma;
        if (End == nullptr)
            End = Cursor + strlen(Cursor);
        while (End > Cursor && HasParseClass(End[-1], ParseClassSpace))
            End -= 1;
        size_t Copy = End - Cursor;
        if (Copy > sizeof(GStringIndexScratch) - 1u)
            Copy = sizeof(GStringIndexScratch) - 1u;
        memcpy(GStringIndexScratch, Cursor, Copy);
        GStringIndexScratch[Copy] = '\0';
        return GStringIndexScratch;
    }
}

double __cdecl ParseNumber(char const* Text)
{
    // 0631bb70. No null check of Text. A char literal ('x) is returned WITHOUT the sign.
    // Suffixes: a hex-digit run > 1 ending in 'd' / 'b' is base 10 / 2; a single digit
    // followed by a lowercase 'h' is base 16. Retail's 'e' exponent scan after the decimal
    // digits only advances a pointer atof never sees, so it is omitted.
    int Sign = 1;
    int Base = 10;
    char const* Start = Text;
    if (*Start == '+')
        Start += 1;
    if (*Start == '-')
    {
        Sign = -1;
        Start += 1;
    }

    if (*Start == '0')
    {
        char Next;
        do
        {
            Next = Start[1];
            Start += 1;
        } while (Next == '0');
        if (ParseToLower(Next) == 'x')
        {
            Start += 1;
            Base = 16;
        }
    }

    double Value = 0.0;
    char const* End = Start;
    if (Base == 16)
    {
        while (HasParseClass(*End, ParseClassHex))
            End += 1;
        if (*End == '.')
            End += 1;
        while (HasParseClass(*End, ParseClassHex))
            End += 1;
        Value = ParseBaseN(Start, End, Base, Text);
    }
    else if (*Start == '\'')
    {
        return (signed char)Start[1];
    }
    else if (*Start == '\0')
    {
        Value = MsvcrtAtof(Start);
    }
    else
    {
        while (HasParseClass(*End, ParseClassHex))
            End += 1;
        ptrdiff_t const Run = End - Start;
        if (Run > 0)
        {
            int const Last = ParseToLower(End[-1]);
            if (Run > 1)
            {
                if (Last == 'd')
                {
                    End -= 1;
                    Base = 10;
                }
                else if (Last == 'b')
                {
                    End -= 1;
                    Base = 2;
                }
            }
            else if (*End == 'h')
            {
                Base = 16;
            }
        }

        if (*End == '\0')
        {
            if (Base == 10)
                Value = MsvcrtAtof(Start);
            else
                Value = ParseBaseN(Start, End, Base, Text);
        }
        else
        {
            if (*End == '.')
            {
                End += 1;
                while (HasParseClass(*End, ParseClassDigit))
                    End += 1;
            }
            if (Base == 10)
                Value = MsvcrtAtof(Start);
            else
                Value = ParseBaseN(Start, End, Base, Text);
        }
    }

    if (Sign < 0)
        Value = -Value;
    return Value;
}

double __cdecl ValueNum(char const* Text, unsigned int Index)
{
    // 0631bee0. Deliberately NOT ParseNumber -- retail calls MSVCRT atof directly, so "0x10"
    // reads back as 0 and the base-N suffixes are not honoured.
    return MsvcrtAtof(StringIndex(Text, Index));
}

void __cdecl RegisterLoadingScreen(ILoadingScreen* Screen)
{
    // 06311420; DAT_063fd2a4.
    GLoadingScreen = Screen;
}

void __cdecl RefreshLoadingProgress()
{
    // 06311430.
    if (GLoadingScreen == nullptr)
        return;
    GLoadingScreen->Refresh();
}


// ---------------------------------------------------------------------------------------------
// INI_Reader's BINI half (+0x1428)
// ---------------------------------------------------------------------------------------------

INI_Reader::FBiniFile::FBiniFile()
    : FileHeader(nullptr)
    , StringBase(nullptr)
    , Block(nullptr)
    , Value(nullptr)
{
    // Inlined in the INI_Reader ctor (0630f690): FileMap part +0x1428..+0x1448, header
    // +0x144c, string base +0x1450, block +0x1454, value +0x145c. The value index (+0x1458),
    // the params pointer (+0x1460) and the join buffer are left uninitialised.
}

bool INI_Reader::FBiniFile::Init()
{
    // FUN_0630fbd0, with seek(0) and FileMap::read_ptr(BINI_Header*&) inlined. The value
    // index (+0x30), the params pointer (+0x38) and the BINI flag (+0x1564, set by open)
    // are not touched here.
    BINI_Header const* Candidate = nullptr;
    if (View != nullptr)
    {
        Cursor = 0;
        unsigned int const Start = Cursor;
        if (Start < Size)
        {
            unsigned int Take = Size - Start;
            if (Take > BiniHeaderBytes)
                Take = BiniHeaderBytes;
            BINI_Header const* const Read = (BINI_Header const*)(View + Start);
            Cursor = Start + Take;
            if (Take == BiniHeaderBytes && Read->Magic == BiniMagic && Read->StringTableOffset < Size)
                Candidate = Read;
        }
        if (Candidate == nullptr)
            Cursor = 0;
    }

    FileHeader = Candidate;
    if (Candidate == nullptr)
        return false;
    StringBase = (char const*)Candidate + Candidate->StringTableOffset;
    Block = nullptr;
    Value = nullptr;
    return true;
}

char const* INI_Reader::FBiniFile::String(unsigned int Offset) const
{
    // Every BINI name/string read is `string base (+0x28) + offset` with no check.
    return StringBase + Offset;
}

FBiniParam const* INI_Reader::FBiniFile::ParamAt(unsigned int Index) const
{
    // No null check of the params pointer (+0x38), which read_value leaves null when the
    // file ends inside a value (a read through it faults, as in retail).
    return &Params[Index];
}

void INI_Reader::FBiniFile::ReportMissingParam(unsigned int Index) const
{
    // The out-of-range branch every BINI getter inlines (e.g. 0630ffde). Block is read
    // without a check. Retail's sprintf into the 0x100 stack buffer is unbounded (undefined
    // behaviour for long names); the port truncates instead.
    char Buffer[0x100];
    _snprintf(Buffer, sizeof(Buffer), "header [%s] value %s missing parameter %u",
              String(Block->NameOffset), String(Value->NameOffset), Index);
    Buffer[sizeof(Buffer) - 1u] = '\0';
    MessageBoxA(nullptr, Buffer, "ERROR", 0);
}

char const* INI_Reader::FBiniFile::FormatParam(unsigned int Index)
{
    // FUN_0630ff40 (also inlined in get_value_string(unsigned), 06310a30). A null value
    // returns "" without a report. An unknown type returns DAT_0658e900 as it stands.
    if (Value == nullptr)
        return EmptyC;
    if (Index >= Value->ParamCount)
    {
        ReportMissingParam(Index);
        return EmptyC;
    }

    FBiniParam const* const Param = ParamAt(Index);
    switch (Param->Type)
    {
    case BiniParamByte:
        sprintf(GBiniNumScratch, "%d", Param->Byte);
        return GBiniNumScratch;
    case BiniParamInt:
        sprintf(GBiniNumScratch, "%d", Param->Int);
        return GBiniNumScratch;
    case BiniParamFloat:
        sprintf(GBiniNumScratch, "%g", (double)Param->Float);
        return GBiniNumScratch;
    case BiniParamString:
        return String(Param->StringOffset);
    default:
        return GBiniNumScratch;
    }
}

char const* INI_Reader::FBiniFile::JoinParams()
{
    // The loop get_line_ptr, get_value_string() and get_value_wstring share (join buffer
    // +0x3c). The parameter is formatted before ", " is appended. Retail appends with
    // unbounded strcat (see JoinChars).
    Joined[0] = '\0';
    if (Value == nullptr)
        return Joined;
    for (int Index = 0; Index < Value->ParamCount; Index += 1)
    {
        char const* const Piece = FormatParam(Index);
        if (Index > 0)
            AppendBounded(Joined, JoinChars, ", ");
        AppendBounded(Joined, JoinChars, Piece);
    }
    return Joined;
}

// ---------------------------------------------------------------------------------------------
// INI_Reader
// ---------------------------------------------------------------------------------------------

INI_Reader::INI_Reader()
{
    // .text:0630f660. LineNum +0x24, Header +0x28, bBini +0x1564. ValueName, ValueParams and
    // ValueWide are left uninitialised.
    Text.LineNum = 0;
    Header[0] = '\0';
    bBini = false;
}

// .text:062634f0 / 06263650: memberwise, in declaration order.
INI_Reader::INI_Reader(INI_Reader const& Other) = default;
INI_Reader& INI_Reader::operator=(INI_Reader const& Other) = default;

INI_Reader::~INI_Reader()
{
    // .text:06263860: member destructors only.
}

bool INI_Reader::open(char const* Path, bool bFatal)
{
    // .text:0630f9b0. No null check of Path or of the DALib FatalErrorFn pointer. Neither of
    // the reader's own maps is closed first: whatever they still hold is overwritten.
    RefreshLoadingProgress();

    FileMap Temp;
    if (!Temp.open(Path))
    {
        if (bFatal)
            FatalErrorFn(200, Path);
        return false;
    }

    Temp.seek(0);
    BINI_Header* FileHeader = nullptr;
    if (Temp.read_ptr(FileHeader) && FileHeader->Magic == BiniMagic &&
        FileHeader->StringTableOffset < Temp.Size)
    {
        // A failed Init falls through to the text path with the local's handles already
        // given away (so the text map gets no view).
        Bini.FileMap::operator=(Temp);
        Temp.View = nullptr;
        Temp.MappingHandle = nullptr;
        Temp.FileHandle = INVALID_HANDLE_VALUE;
        if (Bini.Init())
        {
            bBini = true;
            return true;
        }
    }
    else
    {
        Temp.seek(0);
    }

    bBini = false;
    Temp.handoff_to(Text);
    Text.LineNum = 0;
    Header[0] = '\0';
    Text.seek(0);
    return true;
}

bool INI_Reader::open_memory(char const* Buffer, unsigned int Bytes)
{
    // .text:0630fc50. The previous text map is not closed. BINI data in memory is never
    // recognised.
    FileMap Temp;
    if (!Temp.open_memory(Buffer, Bytes))
        return false;

    bBini = false;
    Temp.handoff_to(Text);
    Text.LineNum = 0;
    Header[0] = '\0';
    Text.seek(0);
    return true;
}

void INI_Reader::close()
{
    // .text:0630f770. The flag, the name buffers, LineNum and the BINI pointers are left as
    // they are.
    if (bBini)
        Bini.close();
    else
        Text.close();
}

void INI_Reader::reset()
{
    // .text:0630f920. BINI seeks past the header; the value index and params pointer stay.
    if (bBini)
    {
        Bini.Block = nullptr;
        Bini.Value = nullptr;
        Bini.seek(BiniHeaderBytes);
        return;
    }

    Text.LineNum = 0;
    Header[0] = '\0';
    Text.seek(0);
}

void INI_Reader::seek(unsigned int Pos)
{
    // .text:0630f6f0.
    if (bBini)
        Bini.seek(Pos);
    else
        Text.seek(Pos);
}

unsigned int INI_Reader::tell() const
{
    // .text:0630f740.
    if (bBini)
        return Bini.tell();
    return Text.tell();
}

int INI_Reader::get_line_num() const
{
    // .text:0630f970. BINI has no lines.
    if (bBini)
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", IniSource, 0x1f6,
              "INI_Reader behavior here differs from traditional behavior.");
        return 0;
    }
    return Text.LineNum;
}

bool INI_Reader::is_end() const
{
    // .text:06310460. BINI data ends at the string table; the header pointer is read without
    // a check.
    if (bBini)
    {
        unsigned int const Table = Bini.FileHeader->StringTableOffset;
        if (Bini.View != nullptr)
            return !(Bini.Cursor < Table);
        return !(0u < Table);
    }
    if (Text.View != nullptr)
        return !(Text.Cursor < Text.Size);
    return !(0u < Text.Size);
}

bool INI_Reader::ReadTextHeader()
{
    // FUN_0630f3d0 (read_header / find_header text path). A section name that starts with
    // ';' is commented out and the scan goes on.
    LineParser32 Line;
    if (!Text.read_line(Line))
    {
        Header[0] = '\0';
        return false;
    }

    for (;;)
    {
        Line.skip_white();
        if (Line.Position < Line.Length && Line.Buffer[Line.Position] == '[')
        {
            Line.Position += 1;
            Line.read_terminator(Header, ']');
            if (Header[0] != ';')
                return true;
        }
        if (!Text.read_line(Line))
        {
            Header[0] = '\0';
            return false;
        }
    }
}

bool INI_Reader::ReadBiniHeader()
{
    // read_header's BINI branch (in 06310070). The unread values of the current block are
    // skipped first (signed index vs unsigned count). The params pointer stays; the header
    // pointer is read without a check.
    if (Bini.Block != nullptr)
    {
        while (Bini.ValueIndex < Bini.Block->ValueCount)
        {
            unsigned int Got = 3;
            Bini.Value = (BINI_Value const*)Bini.request_pointer(Got);
            if (Got != 3)
                Bini.Value = nullptr;
            else
                Bini.seek(Bini.tell() + Bini.Value->ParamCount * BiniParamBytes);
            ++Bini.ValueIndex;
        }
    }

    Bini.Block = nullptr;
    Bini.Value = nullptr;
    if (!(Bini.tell() < Bini.FileHeader->StringTableOffset))
        return false;

    unsigned int Got = 4;
    Bini.Block = (BINI_Block const*)Bini.request_pointer(Got);
    if (Got != 4)
    {
        Bini.Block = nullptr;
        return false;
    }
    Bini.ValueIndex = 0;
    return true;
}

bool INI_Reader::read_header()
{
    // .text:06310070.
    if (bBini)
        return ReadBiniHeader();
    return ReadTextHeader();
}

bool INI_Reader::ReadTextValue()
{
    // LAB_0630f4e0 (read_value's text path). A '[' line belongs to the next section: the line
    // number and cursor are restored and false is returned.
    LineParser32 Line;
    for (;;)
    {
        unsigned int const SavedLine = Text.LineNum;
        unsigned int const SavedCursor = Text.tell();
        Line.clear();
        if (!Text.read_line(Line))
        {
            ValueName[0] = '\0';
            return false;
        }

        Line.skip_white();
        if (!(Line.Position < Line.Length))
            continue;
        char const Ch = Line.Buffer[Line.Position];
        if (Ch == '\0' || Ch == ';')
            continue;
        if (Ch == '[')
        {
            Text.LineNum = SavedLine;
            Text.seek(SavedCursor);
            return false;
        }

        Line.read_terminator2(ValueName, '=', ';');
        if (!(Line.Position < Line.Length) || Line.Buffer[Line.Position] != '=')
        {
            ValueParams[0] = '\0';
            return true;
        }

        Line.Position += 1;
        Line.skip_white();
        Line.read_terminator(ValueParams, ';');
        return true;
    }
}

bool INI_Reader::read_value()
{
    // .text:06310170. BINI: the block pointer is read without a check. The params pointer is
    // null when the file has no bytes left, and the result is true either way.
    if (!bBini)
        return ReadTextValue();

    if (!(Bini.ValueIndex < Bini.Block->ValueCount))
        return false;

    unsigned int Got = 3;
    Bini.Value = (BINI_Value const*)Bini.request_pointer(Got);
    if (Got != 3)
    {
        Bini.Value = nullptr;
        return false;
    }

    unsigned int ParamBytes = Bini.Value->ParamCount * BiniParamBytes;
    Bini.Params = (FBiniParam const*)Bini.request_pointer(ParamBytes);
    ++Bini.ValueIndex;
    return true;
}

bool INI_Reader::find_header(char const* Name)
{
    // .text:06310250.
    reset();
    while (read_header())
    {
        if (is_header(Name))
            return true;
    }
    return false;
}

bool INI_Reader::is_header(char const* Name)
{
    // .text:063103c0. BINI reads the block pointer without a check.
    if (bBini)
        return CallDacomStricmp(Bini.String(Bini.Block->NameOffset), Name) == 0;
    return CallDacomStricmp(Header, Name) == 0;
}

bool INI_Reader::is_value(char const* Name)
{
    // .text:06310410. BINI reads the value pointer without a check.
    if (bBini)
        return CallDacomStricmp(Bini.String(Bini.Value->NameOffset), Name) == 0;
    return CallDacomStricmp(ValueName, Name) == 0;
}

char const* INI_Reader::get_header_ptr()
{
    // .text:063104c0. BINI reads the block pointer without a check.
    if (bBini)
        return Bini.String(Bini.Block->NameOffset);
    return Header;
}

char const* INI_Reader::get_name_ptr()
{
    // .text:063104e0. BINI reads the value pointer without a check.
    if (bBini)
        return Bini.String(Bini.Value->NameOffset);
    return ValueName;
}

char const* INI_Reader::get_file_name() const
{
    // .text:06310040.
    if (bBini)
        return Bini.get_file_name();
    return Text.get_file_name();
}

char const* INI_Reader::get_name() const
{
    // .text:06263450: JMP get_file_name.
    return get_file_name();
}

char const* INI_Reader::get_line_ptr()
{
    // .text:0630fe10. Retail's sprintf / strcat are unbounded (see the scratch buffers).
    if (bBini)
    {
        if (Bini.Value == nullptr)
            return EmptyC;
        _snprintf(GBiniLineScratch, sizeof(GBiniLineScratch), "%s = ",
                  Bini.String(Bini.Value->NameOffset));
        GBiniLineScratch[sizeof(GBiniLineScratch) - 1u] = '\0';
        AppendBounded(GBiniLineScratch, sizeof(GBiniLineScratch), Bini.JoinParams());
        return GBiniLineScratch;
    }

    _snprintf(GTextLineScratch, sizeof(GTextLineScratch), "%s = %s", ValueName, ValueParams);
    GTextLineScratch[sizeof(GTextLineScratch) - 1u] = '\0';
    return GTextLineScratch;
}

unsigned int INI_Reader::get_num_parameters() const
{
    // .text:06310510. BINI reads the value pointer without a check.
    if (bBini)
        return Bini.Value->ParamCount;

    char const* Cursor = ValueParams;
    unsigned int Count = 0;
    if (*Cursor != '\0')
    {
        for (;;)
        {
            Count += 1;
            Cursor = strchr(Cursor, ',');
            if (Cursor == nullptr)
                break;
            Cursor += 1;
        }
    }
    return Count;
}

bool INI_Reader::is_value_empty(unsigned int Index)
{
    // .text:06310560.
    if (bBini)
    {
        if (Bini.Value != nullptr && Index < Bini.Value->ParamCount)
            return false;
        return true;
    }
    return StringIndex(ValueParams, Index)[0] == '\0';
}

bool INI_Reader::get_value_bool(unsigned int Index)
{
    // .text:063105b0. A BINI float is false only when equal to 0.0 (double 0639c430): NaN
    // (unordered) is true.
    if (bBini)
    {
        if (Bini.Value == nullptr)
            return false;
        if (Index >= Bini.Value->ParamCount)
        {
            Bini.ReportMissingParam(Index);
            return false;
        }
        FBiniParam const* const Param = Bini.ParamAt(Index);
        switch (Param->Type)
        {
        case BiniParamByte:
            return Param->Byte != 0;
        case BiniParamInt:
            return Param->Int != 0;
        case BiniParamFloat:
            return !(Param->Float == 0.0);
        case BiniParamString:
        {
            char const* const Field = Bini.String(Param->StringOffset);
            if (CallDacomStricmp(Field, "true") == 0)
                return true;
            if (CallDacomStricmp(Field, "false") == 0)
                return false;
            return MsvcrtAtoi(Field) != 0;
        }
        default:
            return false;
        }
    }

    char const* const Field = StringIndex(ValueParams, Index);
    if (CallDacomStricmp(Field, "true") == 0)
        return true;
    if (CallDacomStricmp(Field, "1") == 0)
        return true;
    if (CallDacomStricmp(Field, "false") == 0)
        return false;
    if (CallDacomStricmp(Field, "0") == 0)
        return false;

    FDUMP(kFdumpError, "%s(%d) : *** ERROR: Invalid bool value %s in .ini file %s\n", IniSource, 0x126,
          Field, Text.get_file_name());
    return false;
}

int INI_Reader::get_value_int(unsigned int Index)
{
    // .text:063107c0.
    if (bBini)
    {
        if (Bini.Value == nullptr)
            return 0;
        if (Index >= Bini.Value->ParamCount)
        {
            Bini.ReportMissingParam(Index);
            return 0;
        }
        FBiniParam const* const Param = Bini.ParamAt(Index);
        switch (Param->Type)
        {
        case BiniParamByte:
            return Param->Byte;
        case BiniParamInt:
            return Param->Int;
        case BiniParamFloat:
            // _ftol truncates to 64 bits; EAX keeps the low half (2^32 -> 0).
            return (int)(long long)Param->Float;
        case BiniParamString:
            return MsvcrtAtoi(Bini.String(Param->StringOffset));
        default:
            return 0;
        }
    }
    return MsvcrtAtoi(StringIndex(ValueParams, Index));
}

double INI_Reader::ValueAsNumber(unsigned int Index)
{
    // get_value_float's body; the result stays on the x87 stack, so value_num sees it
    // unrounded. The BINI default is the float DAT_06399408 (0.0).
    if (bBini)
    {
        if (Bini.Value == nullptr)
            return 0.0;
        if (Index >= Bini.Value->ParamCount)
        {
            Bini.ReportMissingParam(Index);
            return 0.0;
        }
        FBiniParam const* const Param = Bini.ParamAt(Index);
        switch (Param->Type)
        {
        case BiniParamByte:
            return Param->Byte;
        case BiniParamInt:
            return Param->Int;
        case BiniParamFloat:
            return Param->Float;
        case BiniParamString:
            return MsvcrtAtof(Bini.String(Param->StringOffset));
        default:
            return 0.0;
        }
    }
    return ParseNumber(StringIndex(ValueParams, Index));
}

double INI_Reader::get_value_float(unsigned int Index)
{
    // .text:063108f0: the x87 result is returned unrounded; callers either store it as a
    // float or use the full value.
    return ValueAsNumber(Index);
}

char const* INI_Reader::get_value_string(unsigned int Index)
{
    // .text:06310a30.
    if (bBini)
        return Bini.FormatParam(Index);
    return StringIndex(ValueParams, Index);
}

char const* INI_Reader::get_value_string()
{
    // .text:06310b70.
    if (bBini)
        return Bini.JoinParams();
    return ValueParams;
}

wchar_t const* INI_Reader::get_value_wstring()
{
    // .text:06310d20. ValueWide is +0xc28. No terminator is added (ValueParams always fits).
    if (bBini)
    {
        mbstowcs(GBiniWideScratch, Bini.JoinParams(), 0x100);
        return GBiniWideScratch;
    }
    mbstowcs(ValueWide, ValueParams, NameChars);
    return ValueWide;
}

char const* INI_Reader::get_value_ptr()
{
    // .text:062634c0: JMP get_value_string().
    return get_value_string();
}

char const* INI_Reader::get_indexed_value(unsigned int Index)
{
    // .text:062634d0: JMP get_value_string(unsigned).
    return get_value_string(Index);
}

bool INI_Reader::get_bool(unsigned int Index)
{
    // .text:062634e0: JMP get_value_bool.
    return get_value_bool(Index);
}

double INI_Reader::value_num(unsigned int Index)
{
    // .text:062634b0: JMP get_value_float -- the unrounded x87 result.
    return ValueAsNumber(Index);
}

Vector INI_Reader::get_vector()
{
    // .text:06263460: get_value_float(0), (1), (2) in that order.
    Vector Result;
    Result.x = get_value_float(0);
    Result.y = get_value_float(1);
    Result.z = get_value_float(2);
    return Result;
}

void INI_Reader::get_state(State& Out) const
{
    // .text:06310f00. BINI writes only the first five fields of State; a null pointer gives
    // a negative offset, kept as such.
    if (bBini)
    {
        std::uintptr_t const Base = (std::uintptr_t)Bini.View;
        Out.Cursor = Bini.tell();
        Out.Bini.BlockOffset = (int)((std::uintptr_t)Bini.Block - Base);
        Out.Bini.ValueIndex = Bini.ValueIndex;
        Out.Bini.ValueOffset = (int)((std::uintptr_t)Bini.Value - Base);
        Out.Bini.ParamsOffset = (int)((std::uintptr_t)Bini.Params - Base);
        return;
    }

    Out.Cursor = Text.tell();
    Out.Text.LineNum = Text.LineNum;
    memcpy(Out.Text.Header, Header, NameChars);
    memcpy(Out.Text.ValueName, ValueName, NameChars);
    memcpy(Out.Text.ValueParams, ValueParams, NameChars);
}

void INI_Reader::set_state(State const& In)
{
    // .text:06310fc0. BINI rebuilds the pointers as view + offset even without a view.
    if (bBini)
    {
        std::uintptr_t const Base = (std::uintptr_t)Bini.View;
        Bini.seek(In.Cursor);
        Bini.Block = (BINI_Block const*)(Base + In.Bini.BlockOffset);
        Bini.ValueIndex = In.Bini.ValueIndex;
        Bini.Value = (BINI_Value const*)(Base + In.Bini.ValueOffset);
        Bini.Params = (FBiniParam const*)(Base + In.Bini.ParamsOffset);
        return;
    }

    Text.seek(In.Cursor);
    Text.LineNum = In.Text.LineNum;
    memcpy(Header, In.Text.Header, NameChars);
    memcpy(ValueName, In.Text.ValueName, NameChars);
    memcpy(ValueParams, In.Text.ValueParams, NameChars);
}

void INI_Reader::log_link(char const* Link) const
{
    // .text:062ed730: a bare `RET 4` (ICF-shared with CostumeDescriptions::warn). The
    // retail build compiled the link logging out; nothing happens.
    (void)Link;
}

bool INI_Reader::is_number(char const* Candidate) const
{
    // .text:06311080. No null check. A quoted letter ('a') is a number; after any other
    // quote the checks continue past the quote. "0x" with an empty rest is a number. One sign
    // is allowed, only after the exponent marker. tolower is MSVCRT's on the sign-extended char.
    char const* Cursor = Candidate;
    bool bSawExp = false;
    bool bSawDot = false;
    bool bSignLocked = true;

    if (*Cursor == '-' || *Cursor == '+')
        Cursor += 1;
    if (*Cursor == '.')
    {
        Cursor += 1;
        bSawDot = true;
    }
    if (*Cursor == '\'')
    {
        Cursor += 1;
        int const Lower = ParseToLower(*Cursor);
        if (Lower >= 'a' && Lower <= 'z' && Cursor[1] == '\'')
            return true;
    }

    if (*Cursor == '0' && ParseToLower(Cursor[1]) == 'x')
    {
        Cursor += 2;
        for (char Ch = *Cursor; Ch != '\0'; Cursor += 1, Ch = *Cursor)
        {
            if (Ch >= '0' && Ch <= '9')
                continue;
            int const Lower = ParseToLower(Ch); // -128..127: the byte compare retail does
            if (Lower < 'a' || Lower > 'f')
                return false;
        }
        return true;
    }

    if (*Cursor < '0' || *Cursor > '9')
        return false;
    Cursor += 1;
    for (char Ch = *Cursor; Ch != '\0'; Cursor += 1, Ch = *Cursor)
    {
        if (Ch >= '0' && Ch <= '9')
            continue;
        if (!bSignLocked && (Ch == '+' || Ch == '-'))
        {
            bSignLocked = true;
            continue;
        }
        if (!bSawDot && Ch == '.')
        {
            bSawDot = true;
            continue;
        }
        if (bSawExp)
            return false;
        if (ParseToLower(Ch) != 'e')
            return false;
        bSawExp = true;
        bSignLocked = false;
    }
    return true;
}
