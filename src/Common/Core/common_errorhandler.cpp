// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// ErrorHandler -- retail FDUMP backend (0x0630ab90-0x0630b6e0). Declared in
// common_errorhandler.h.
#include "common_errorhandler.h"
#include "common_ini.h"  // INI_Reader (activate)
#include "common_misc.h" // File (the log node's embedded file)
#include "dacom.h"       // FDUMP

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

// DACOM.DLL!stricmp through the isolated TU common_dacom_stricmp.cpp.
int CallDacomStricmp(const char* Left, const char* Right);

namespace
{
    // The log-channel node, sizeof 0xA0 (operator new(0xa0) in new_log and activate).
    struct LogEntry
    {
        LogEntry* Next;   // +0x00
        bool bEnabled;    // +0x04
        int LogId;         // +0x08  FdumpHook matches it against (Flags >> 4) unless -1
        int SeverityLimit; // +0x0C  FdumpHook drops a call whose severity nibble is
                           //        above it (unsigned compare); 100 by default, the exe
                           //        passes 7 (new_log(path, -1, 7, ..))
        bool bActive;      // +0x10  cached bEnabled && (bText||bWindow||File.Handle);
                           //        the gate FdumpHook reads
        bool bText;        // +0x11  "$Text" virtual sink; FdumpHook upper-cases the line
                           //        for such a channel when severity <= 2
        bool bWindow;      // +0x12  "$Window" virtual sink
        ::File File;       // +0x14  the exported File class (common_misc.h);
                           //        File.Handle is node +0x94
    };
    static_assert(sizeof(LogEntry) == 0xA0, "ErrorHandler LogEntry sizeof 0xA0");

    // FUN_0630b0d0: default-reset a freshly allocated node.
    void LogEntryResetDefaults(LogEntry* E)
    {
        E->LogId = -1;
        E->SeverityLimit = 100;
        E->bActive = false;
        E->bText = false;
        E->bWindow = false;
        E->bEnabled = true;
    }

    // FUN_0630b0f0: (re)configure a node's name/routing. Name[0]=='$' selects a virtual sink
    // (DACOM stricmp) instead of opening a real file ("wb"). Deviation: retail copies Name into
    // the 0x80-byte buffer with an unbounded loop; the port stops at the buffer.
    void LogEntryConfigure(LogEntry* E, char const* Name, int LogId, int SeverityLimit,
                            bool bFlagText, bool bFlagWindow)
    {
        E->LogId = LogId;
        E->SeverityLimit = SeverityLimit;
        E->bText = bFlagText;
        E->bWindow = bFlagWindow;
        E->File.close();
        if (Name != nullptr)
        {
            if (Name[0] == '$')
            {
                if (CallDacomStricmp(Name, "$Text") == 0)
                    E->bText = true;
                else if (CallDacomStricmp(Name, "$Window") == 0)
                    E->bWindow = true;
            }
            else
            {
                if (E->File.Handle != nullptr)
                    E->File.close();
                if (Name[0] != '\0')
                {
                    E->File.close();
                    size_t Index = 0;
                    for (; Index < sizeof(E->File.Name) - 1 && Name[Index] != '\0'; ++Index)
                        E->File.Name[Index] = Name[Index];
                    E->File.Name[Index] = '\0';
                    E->File.Handle = fopen(E->File.Name, "wb");
                    if (E->File.Handle != nullptr)
                        E->File.get_info();
                }
            }
        }
        E->bActive = E->bEnabled && (E->bText || E->bWindow || E->File.Handle != nullptr);
    }

    // FUN_0630b220, the node destructor, plus operator delete at both call sites.
    void LogEntryDestroy(LogEntry* E)
    {
        E->File.close();
        delete E;
    }

    LogEntry* g_ErrorLogListHead = nullptr;  // DAT_063fcf28
    LogEntry* g_ErrorLogListTail = nullptr;  // DAT_063fcf2c
    void(__cdecl* g_PreviousFdump)(int, char const*, ...) = nullptr;  // DAT_063fcf38
    // DAT_063fcf30 / DAT_063fcf34: FdumpHook counts warnings and errors; activate zeroes both.
    // Retail never reads them back.
    int g_FdumpWarningCount = 0;
    int g_FdumpErrorCount = 0;

    // LAB_0630ab90, the default AssertHandler: PTR_LAB_063ed214 is statically initialised to it
    // and activate() leaves it installed when given a null handler.
    int __cdecl AssertHandlerDefault(char const* Text, char const* Caption, bool bIgnoreIcon)
    {
        UINT Style = bIgnoreIcon ? MB_ABORTRETRYIGNORE : (MB_ICONHAND | MB_ABORTRETRYIGNORE);
        return MessageBoxA(nullptr, Text, Caption, Style);
    }

    int(__cdecl* g_ErrorAssertHandler)(char const*, char const*, bool) =
        &AssertHandlerDefault;  // PTR_LAB_063ed214

    // Recomputes and returns a node's cached "effectively active" flag.
    bool LogEntryRefreshActive(LogEntry* E)
    {
        E->bActive = E->bEnabled && (E->bText || E->bWindow || E->File.Handle != nullptr);
        return E->bActive;
    }
}

namespace ErrorHandler
{
    void* __cdecl new_log(char const* Name, int LogId, int SeverityLimit, bool bText, bool bWindow)
    {
        // 0xab320. Retail's operator new returns null on failure and the next read faults; the
        // port's throws, so there is no null branch.
        LogEntry* E = new LogEntry;
        E->Next = nullptr;
        LogEntryResetDefaults(E);
        LogEntryConfigure(E, Name, LogId, SeverityLimit, bText, bWindow);

        if (!LogEntryRefreshActive(E))
        {
            LogEntryDestroy(E);
            return nullptr;
        }

        // Retail's asymmetric insert: non-$Window channels are pushed to the front of
        // the list; $Window channels are appended at the back. First-ever node becomes
        // both head and tail either way.
        if (!E->bWindow)
        {
            E->Next = g_ErrorLogListHead;
            if (g_ErrorLogListTail != nullptr)
            {
                g_ErrorLogListHead = E;
                return E;
            }
        }
        else if (g_ErrorLogListTail != nullptr)
        {
            g_ErrorLogListTail->Next = E;
        }
        if (g_ErrorLogListTail == nullptr)
            g_ErrorLogListHead = E;
        g_ErrorLogListTail = E;
        return E;
    }

    void __cdecl log_enable(int LogId)
    {
        // 0xab460
        for (LogEntry* E = g_ErrorLogListHead; E != nullptr; E = E->Next)
        {
            if (LogId == -1 || E->LogId == LogId)
            {
                E->bEnabled = true;
                LogEntryRefreshActive(E);
            }
        }
    }

    void __cdecl log_disable(int LogId)
    {
        // 0xab4b0
        for (LogEntry* E = g_ErrorLogListHead; E != nullptr; E = E->Next)
        {
            if (LogId == -1 || E->LogId == LogId)
            {
                E->bEnabled = false;
                E->bActive = false;
            }
        }
    }

    bool __cdecl is_log_enabled(int LogId)
    {
        // 0xab4e0. No -1 wildcard here, unlike log_enable/log_disable.
        for (LogEntry* E = g_ErrorLogListHead; E != nullptr; E = E->Next)
        {
            if (E->LogId == LogId)
                return LogEntryRefreshActive(E);
        }
        return false;
    }

    void __cdecl log_comment(int LogId, char const* Format, ...)
    {
        // FUN_0630b540. Retail formats only while Buffer[0] is still 0 (an empty result is
        // re-formatted for the next node), appends "\n" through a 0x200-byte buffer, and writes
        // only to nodes with a FILE*. Deviation: retail's format buffer is 0x100 bytes with an
        // unbounded vsprintf; the port formats into 0x400 bytes, truncated, identical for lines
        // shorter than 0x100.
        char Buffer[0x400];
        Buffer[0] = '\0';

        for (LogEntry* E = g_ErrorLogListHead; E != nullptr; E = E->Next)
        {
            if (LogId != -1 && E->LogId != LogId)
                continue;
            if (!LogEntryRefreshActive(E))
                continue;

            if (Buffer[0] == '\0')
            {
                va_list Args;
                va_start(Args, Format);
                _vsnprintf(Buffer, sizeof(Buffer), Format, Args);
                va_end(Args);
                Buffer[sizeof(Buffer) - 1] = '\0';
            }

            char const* Line = Buffer;
            char LineWithNewline[0x200];
            int const Len = (int)strlen(Buffer);
            if (Len > 0 && Buffer[Len - 1] != '\n' &&
                (unsigned)(Len + 1) < sizeof(LineWithNewline))
            {
                memcpy(LineWithNewline, Buffer, Len);
                LineWithNewline[Len] = '\n';
                LineWithNewline[Len + 1] = '\0';
                Line = LineWithNewline;
            }

            if (E->File.Handle == nullptr)
                continue;
            fwrite(Line, 1, strlen(Line), E->File.Handle);
            fflush(E->File.Handle);
        }
    }

    void __cdecl deactivate()
    {
        // FUN_0630b2a0. Retail disables the LogId 0x52 nodes before freeing every node.
        FDUMP = g_PreviousFdump;
        g_PreviousFdump = nullptr;

        for (LogEntry* E = g_ErrorLogListHead; E != nullptr; E = E->Next)
        {
            if (E->LogId == 0x52)
            {
                E->bEnabled = false;
                E->bActive = false;
            }
        }

        LogEntry* E = g_ErrorLogListHead;
        while (E != nullptr)
        {
            LogEntry* NextEntry = E->Next;
            LogEntryDestroy(E);
            E = NextEntry;
        }
        g_ErrorLogListHead = nullptr;
        g_ErrorLogListTail = nullptr;
    }

    namespace
    {
        // LAB_0630abd0, installed as FDUMP by activate. Retail returns 0 in EAX; the port's
        // FDUMP pointer type (dacom.h) is void.
        void __cdecl FdumpHook(int Flags, char const* Format, ...)
        {
            // Port-only debugger echo (owner's lines, kept on purpose): format once and
            // send it to the debugger. Not part of retail.
            char Buffer[1024];
            va_list Args;
            va_start(Args, Format);
            _vsnprintf(Buffer, sizeof(Buffer), Format, Args);
            va_end(Args);
            Buffer[sizeof(Buffer) - 1] = '\0';
            OutputDebugStringA(Buffer); /// PORT ONLY, DEVIATION, FOR DEBUGGING
            OutputDebugStringA("\n"); /// PORT ONLY, DEVIATION, FOR DEBUGGING

            // Severity is the signed low nibble of Flags; 2 counts a warning, 1 an error.
            int const Severity = (int)((unsigned)Flags << 28) >> 28;
            unsigned const SeverityBits = (unsigned)Severity;
            if (SeverityBits <= 2)
            {
                if (Severity == 2)
                    ++g_FdumpWarningCount;
                if (Severity == 1)
                    ++g_FdumpErrorCount;
            }

            for (LogEntry* E = g_ErrorLogListHead; E != nullptr; E = E->Next)
            {
                // The LogId route (Flags >> 4, unless -1) and the unsigned severity limit.
                if (!E->bActive)
                    continue;
                if (E->LogId != -1 && (Flags >> 4) != E->LogId)
                    continue;
                if (SeverityBits > (unsigned)E->SeverityLimit)
                    continue;

                // Formatted per node into 0x400 bytes. Deviation: retail's vsprintf is
                // unbounded; the port truncates.
                char Line[0x400];
                va_start(Args, Format);
                _vsnprintf(Line, sizeof(Line), Format, Args);
                va_end(Args);
                Line[sizeof(Line) - 1] = '\0';

                if (E->bText && SeverityBits <= 2)
                    _strupr(Line);

                // "\n" appended through a 0x200-byte buffer when it fits.
                char const* Out = Line;
                char LineWithNewline[0x200];
                int const Len = (int)strlen(Line);
                if (Len > 0 && Line[Len - 1] != '\n' &&
                    (unsigned)(Len + 1) < sizeof(LineWithNewline))
                {
                    memcpy(LineWithNewline, Line, Len);
                    LineWithNewline[Len] = '\n';
                    LineWithNewline[Len + 1] = '\0';
                    Out = LineWithNewline;
                }

                // Only a node with a FILE* is written.
                if (E->File.Handle == nullptr)
                    continue;
                fwrite(Out, 1, strlen(Out), E->File.Handle);
                fflush(E->File.Handle);
            }
        }
    }

    void __cdecl activate(char const* IniPath,
                           int (__cdecl* AssertHandler)(char const*, char const*, bool))
    {
        // FUN_0630ad30. No null check of IniPath of its own.
        g_PreviousFdump = FDUMP;
        FDUMP = &FdumpHook;
        if (AssertHandler != nullptr)
            g_ErrorAssertHandler = AssertHandler;
        g_FdumpWarningCount = 0;
        g_FdumpErrorCount = 0;

        INI_Reader Reader;
        if (!Reader.open(IniPath, false) || !Reader.read_header())
            return;
        do
        {
            if (Reader.is_header("Error") && Reader.read_value())
            {
                do
                {
                    if (Reader.is_value("log"))
                    {
                        // get_value_string(0) returns a shared static buffer (DAT_06401350)
                        // that the get_value_float calls below overwrite, so retail copies
                        // the name into a local 128-byte buffer first.
                        char Name[0x80];
                        Name[0] = '\0';
                        char const* NameSource = Reader.get_value_string(0);
                        if (NameSource != nullptr)
                        {
                            size_t NameLen = strlen(NameSource);
                            if (NameLen >= 0x80)
                                NameLen = 0x7f;
                            memcpy(Name, NameSource, NameLen);
                            Name[NameLen] = '\0';
                        }
                        // Retail builds the node inline with the same steps new_log takes
                        // (FUN_0630b6e0 is the $Window append), so the port calls new_log.
                        int LogId = -1;
                        int SeverityLimit = 100;
                        if (!Reader.is_value_empty(1))
                            LogId = (int)(long long)Reader.get_value_float(1);
                        if (!Reader.is_value_empty(2))
                            SeverityLimit = (int)(long long)Reader.get_value_float(2);
                        new_log(Name, LogId, SeverityLimit, false, false);
                    }
                } while (Reader.read_value());
            }
        } while (Reader.read_header());
    }

    void __cdecl log_last_error(char const* Source, int Line, SeverityLevel Severity,
                                 char* Format, unsigned long ErrorCode)
    {
        // FUN_0630b680. Format is the caller's; Source / Line / MsgBuf are its substitutions.
        char* MsgBuf = nullptr;
        FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                            FORMAT_MESSAGE_IGNORE_INSERTS,
                        nullptr, ErrorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                        (LPSTR)&MsgBuf, 0, nullptr);

        int Flags = (Severity & 0xf) | 0x100000;
        FDUMP(Flags, Format, Source, Line, MsgBuf);

        LocalFree(MsgBuf);
    }
}
