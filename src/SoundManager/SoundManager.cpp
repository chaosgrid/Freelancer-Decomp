// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "SoundManagerInternal.h"
#include "msvcrt_compat.h" // MsvcrtAtoi: retail MSVCRT.DLL wraps mod 2^32, the UCRT saturates
#include "readfile.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <math.h>
#include <mmreg.h>
#include <msacm.h>
#include <new>
#include <stdint.h>
#include <stdlib.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "dsound.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "msacm32.lib")

namespace
{
    // DAT_06eeaf0c: initialised by every FSoundManager ctor, deleted by every dtor.
    CRITICAL_SECTION G_InstanceLock;
    // DAT_06eeaf00 global instance list (head DAT_06eeaf04, count DAT_06eeaf08). Retail builds the
    // head node in the static initializer 06ee1740 at DLL load and registers 06ee17f0 through
    // _onexit; FInstanceListInit below does both.
    FSoundInstanceNode G_InstanceSentinel;
    unsigned long G_InstanceCount = 0;
    struct FInstanceListInit
    {
        FInstanceListInit()
        {
            G_InstanceSentinel.Next = &G_InstanceSentinel;
            G_InstanceSentinel.Prev = &G_InstanceSentinel;
            G_InstanceCount = 0;
        }
        ~FInstanceListInit(); // 06ee17f0, defined after ReleaseInstancePayload
    };
    FInstanceListInit G_InstanceListInit;
    HANDLE G_MixerEvent = nullptr;   // DAT_06eeaf70: created in the ctor, closed in the dtor
    HANDLE G_NotifyThread = nullptr; // DAT_06eeaf6c, FUN_06ee1a40
    unsigned long G_MixerStopFlag = 0; // DAT_06eeaf74, set by FUN_06ee4900, read by FUN_06ee18e0

    // DAT_06eeaf5c / DAT_06eeaf60 / DAT_06eea028 / DAT_06eeaf64 / DAT_06eeaf68 / DAT_06eea020
    unsigned long G_AllowHwToSw = 0;
    unsigned long G_AllowSwToHw = 0;
    unsigned long G_ForceFreqToSw = 1;
    unsigned long G_NeverSetStartPos = 0;
    unsigned long G_DisplayExtraDebug = 0;
    unsigned long G_Ds3dCreateFlags = DSBCAPS_LOCDEFER | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRL3D;

    const char kDefaultDeviceId[] = "{00000000-0000-0000-0000-000000000000}";
    // s_0123456789truefalsynox.. at 06eea428 (36 chars + NUL); the trailing "OX+-" lets "0X1F"
    // reach the hex branch.
    const char kIntCharset[] = "0123456789truefalsynoxTRUEFALSYNOX+-";
    const char kFloatCharset[] = "0123456789eE.+-";

    // Retail DAT_06ee915c / 914c / 916c — last dword CA8AC24F, not SDK 4FACC94F.
    const GUID kDs3dAlgHrtfFull = {
        0xC2413340, 0x1C1B, 0x11D2, {0x94, 0xF5, 0x00, 0xC0, 0x4F, 0xC2, 0x8A, 0xCA}};
    const GUID kDs3dAlgHrtfLight = {
        0xC2413342, 0x1C1B, 0x11D2, {0x94, 0xF5, 0x00, 0xC0, 0x4F, 0xC2, 0x8A, 0xCA}};
    const GUID kDs3dAlgNoVirtual = {
        0xC241333F, 0x1C1B, 0x11D2, {0x94, 0xF5, 0x00, 0xC0, 0x4F, 0xC2, 0x8A, 0xCA}};
    const GUID kDs3dAlgGuidNull = {0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0}};

    const unsigned long kSpeakerTable[] = {
        DSSPEAKER_MONO,                             // 0 unused by slot 7
        DSSPEAKER_HEADPHONE,                        // 1
        DSSPEAKER_STEREO,                           // 2
        DSSPEAKER_COMBINED(DSSPEAKER_STEREO, 5),    // 3
        DSSPEAKER_COMBINED(DSSPEAKER_STEREO, 10),   // 4
        DSSPEAKER_COMBINED(DSSPEAKER_STEREO, 20),   // 5
        DSSPEAKER_COMBINED(DSSPEAKER_STEREO, 180),  // 6
        DSSPEAKER_QUAD,                             // 7
        DSSPEAKER_SURROUND,                         // 8
        DSSPEAKER_5POINT1,                          // 9
        7,                                          // 10 DSSPEAKER_7POINT1
    };
    const unsigned long kSpeakerCount = sizeof(kSpeakerTable) / sizeof(kSpeakerTable[0]);

    // FUN_06ee71c0. No terminator is forced. Every retail message is formatted through it into a
    // 0x2000-byte stack buffer.
    char* FormatMessage2000(char* Buffer, const char* Fmt, ...)
    {
        va_list Args;
        va_start(Args, Fmt);
        _vsnprintf(Buffer, 0x2000, Fmt, Args);
        va_end(Args);
        return Buffer;
    }

    // Retail calls through the DACOM FDUMP import unconditionally; dacom initialises it
    // statically to DefaultFdump.
    void Fdump(unsigned int Flags, int Line, const char* Message)
    {
        const char* Fmt = kFdumpWarningFmt;
        if (Flags == kFdumpError)
            Fmt = kFdumpErrorFmt;
        else if (Flags == kFdumpNotice)
            Fmt = kFdumpNoticeFmt;
        FDUMP(static_cast<int>(Flags), Fmt, kRetailSource, Line, Message);
    }

    // FDUMP sites inside the statically linked WavLib report wavlib.cpp (06eeadd4).
    void FdumpWavLib(unsigned int Flags, int Line, const char* Message)
    {
        const char* Fmt = kFdumpWarningFmt;
        if (Flags == kFdumpError)
            Fmt = kFdumpErrorFmt;
        FDUMP(static_cast<int>(Flags), Fmt, kWavLibSource, Line, Message);
    }

    void FdumpCouldntCreateArchetype(int Line, int ArchId)
    {
        char Buf[0x2000];
        Fdump(kFdumpWarning, Line,
              FormatMessage2000(Buf, "SoundMgr:Couldn't create archetype %d.\n", ArchId)); // 06eea1ec
    }

    // Signed-char range tests, inlined three times in FUN_06ee6fd0.
    bool IsGuidHexDigit(char C)
    {
        return (C >= '0' && C <= '9') || (C >= 'a' && C <= 'f') || (C >= 'A' && C <= 'F');
    }

    // The cursor steps one past the stop character (a NUL included).
    unsigned long ParseGuidField(const char*& Cursor)
    {
        const char* Field = Cursor;
        while (*Cursor != '\0' && IsGuidHexDigit(*Cursor))
            ++Cursor;
        ++Cursor;
        return strtoul(Field, nullptr, 16);
    }

    // FUN_06ee6fd0 (sole caller StartupDevice, with +0x0D/+0x38). The default id DAT_06eea024
    // zeroes the GUID. Otherwise: skip to '{' and one past it (past the NUL when there is none);
    // strcpy the rest into a 0xC8-byte stack buffer that is never read again; three
    // hex-run fields (Data1 dword, Data2/Data3 word); then Data4 as eight two-character
    // strtoul pairs, the cursor skipping one extra character after the second pair. No
    // validation and no guards, as retail.
    void ParseGuidString(const char* Text, GUID* Out)
    {
        if (strcmp(Text, kDefaultDeviceId) == 0)
        {
            memset(Out, 0, sizeof(GUID));
            return;
        }
        const char* Cursor = Text;
        while (*Cursor != '\0' && *Cursor != '{')
            ++Cursor;
        ++Cursor;
        char DeadCopy[0xC8];
        strcpy(DeadCopy, Cursor);
        const unsigned long Data1 = ParseGuidField(Cursor);
        const unsigned long Data2 = ParseGuidField(Cursor);
        const unsigned long Data3 = ParseGuidField(Cursor);
        unsigned char Data4[8];
        char Pair[3];
        Pair[0] = Cursor[0];
        Pair[1] = Cursor[1];
        Pair[2] = '\0';
        Cursor += 2;
        Data4[0] = static_cast<unsigned char>(strtoul(Pair, nullptr, 16));
        Pair[0] = Cursor[0];
        Pair[1] = Cursor[1];
        Pair[2] = '\0';
        Cursor += 3;
        Data4[1] = static_cast<unsigned char>(strtoul(Pair, nullptr, 16));
        for (int Index = 2; Index < 8; ++Index)
        {
            Pair[0] = Cursor[0];
            Pair[1] = Cursor[1];
            Pair[2] = '\0';
            Cursor += 2;
            Data4[Index] = static_cast<unsigned char>(strtoul(Pair, nullptr, 16));
        }
        Out->Data1 = Data1;
        Out->Data2 = static_cast<unsigned short>(Data2);
        Out->Data3 = static_cast<unsigned short>(Data3);
        memcpy(Out->Data4, Data4, sizeof(Data4));
    }

    // Retail StartupDevice / FUN_06ee72f0 read every INI value through IProfileParser vtbl+0x20
    // (section, key, buffer, 0x400) into one of four global scratch buffers, never cleared first:
    // DAT_06eeafa0 (integers, also FUN_06ee72f0), DAT_06eeb3c0 (reverb floats), DAT_06eeb7e0
    // (deviceID, 3D_SW_Algorithm) and DAT_06eebc00 (speakerConfiguration presence probe).
    char G_IniIntBuffer[0x420];
    char G_IniFloatBuffer[0x420];
    char G_IniStringBuffer[0x420];
    char G_IniSpeakerBuffer[0x404];

    // Inlined after every integer GetString (StartupDevice and FUN_06ee72f0): strspn against the
    // 36-char set and cut there only when the span is non-zero; then "true"/"yes" -> 1 and "false"/"no" -> 0 (stricmp, one call each), an 'x' or 'X'
    // anywhere -> strtoul(.., 0, 16), else atoi. The strspn(buf, "0123456789+-") retail runs just
    // before atoi has its result discarded, so nothing is cut there.
    unsigned long ParseIniUnsigned(char* Text)
    {
        const size_t Span = strspn(Text, kIntCharset);
        if (Span != 0)
            Text[Span] = '\0';
        if (_stricmp(Text, "true") == 0)
            return 1;
        if (_stricmp(Text, "yes") == 0)
            return 1;
        if (_stricmp(Text, "false") == 0)
            return 0;
        if (_stricmp(Text, "no") == 0)
            return 0;
        if (strchr(Text, 'x') != nullptr || strchr(Text, 'X') != nullptr)
            return strtoul(Text, nullptr, 16);
        return static_cast<unsigned long>(MsvcrtAtoi(Text));
    }

    // Inlined after every float GetString; the charset is at 06eea3c4.
    float ParseIniFloat(char* Text)
    {
        const size_t Span = strspn(Text, kFloatCharset);
        if (Span != 0)
            Text[Span] = '\0';
        return static_cast<float>(atof(Text));
    }

    // FUN_06ee74d0: smart-pointer assignment.
    IDAComponent* AssignComPtr(IDAComponent** Dest, IDAComponent* Src)
    {
        if (*Dest != Src)
        {
            if (*Dest != nullptr)
            {
                (*Dest)->Release();
                *Dest = nullptr;
            }
            *Dest = Src;
            if (Src != nullptr)
                Src->AddRef();
        }
        return *Dest;
    }

    // FUN_06ee7600.
    void ReleaseComPtr(IDAComponent** Ptr)
    {
        if (*Ptr != nullptr)
        {
            (*Ptr)->Release();
            *Ptr = nullptr;
        }
    }

    // StartupDevice's per-key prologue (inlined ten times): Local = 0; FUN_06ee74d0(&Local,
    // cached parser [+0x134]) and, when that leaves nothing, Dacom QueryInterface for
    // "1.11_IProfileParser" into Local. True when a parser is held.
    bool AcquireParserLocal(ICOManager* Dacom, IProfileParser* Cached, IDAComponent** Local)
    {
        *Local = nullptr;
        if (AssignComPtr(Local, Cached) != nullptr)
            return true;
        return Dacom->QueryInterface(Iids::ProfileParser, reinterpret_cast<void**>(Local)) == GR_OK;
    }

    // FUN_06ee72f0 cdecl (dacom, cached, section, key, default, out). Out may be null (then the
    // value lives in the 'cached' argument slot). Parser acquisition as above; when no parser is
    // obtained *Out is NOT written and its previous content is returned - StartupDevice passes one
    // stack variable to every call and reads it back, so a failed acquisition keeps the previous
    // key's value. Otherwise *Out = default, OpenSection(section, 3) (0 -> skip), GetString(key,
    // DAT_06eeafa0, 0x400) (0 -> keep default), parse, CloseSection; Release the parser; return *Out.
    unsigned long ReadProfileInt(ICOManager* Dacom, IProfileParser* Cached, const char* Section,
                                 const char* Key, unsigned long DefaultValue, unsigned long* Out)
    {
        unsigned long Fallback = 0;
        if (Out == nullptr)
            Out = &Fallback;
        IDAComponent* Local = nullptr;
        if (Cached != nullptr)
        {
            Local = Cached;
            Cached->AddRef();
        }
        else if (Dacom->QueryInterface(Iids::ProfileParser, reinterpret_cast<void**>(&Local)) != GR_OK)
            return *Out;
        IProfileParser* Parser = static_cast<IProfileParser*>(Local);
        *Out = DefaultValue;
        void* Handle = Parser->OpenSection(Section, 3);
        if (Handle != nullptr)
        {
            if (Parser->GetString(Handle, Key, G_IniIntBuffer, 0x400) != 0)
                *Out = ParseIniUnsigned(G_IniIntBuffer);
            Parser->CloseSection(Handle);
        }
        const unsigned long Value = *Out;
        Local->Release();
        return Value;
    }

    void CopyAlgGuid(unsigned long* Dest, const GUID* Src)
    {
        memcpy(Dest, Src, sizeof(GUID));
    }

    // FUN_06ee54e0.
    void StoreVec3(Vector* Dest, float X, float Y, float Z)
    {
        Dest->x = X;
        Dest->y = Y;
        Dest->z = Z;
    }

    // FUN_06ee5500: each component is rounded to float; Z and Y are computed first.
    void SubVec3(Vector* Out, const Vector* A, const Vector* B)
    {
        const float Z = static_cast<float>(static_cast<double>(A->z) - static_cast<double>(B->z));
        const float Y = static_cast<float>(static_cast<double>(A->y) - static_cast<double>(B->y));
        const float X = static_cast<float>(static_cast<double>(A->x) - static_cast<double>(B->x));
        Out->x = X;
        Out->y = Y;
        Out->z = Z;
    }

    void NoticeArch(int Line, const char* Fmt, ISoundSource* PlayKey)
    {
        if (G_DisplayExtraDebug == 0)
            return;
        // The MixerTick sites call GetArchId on node+8 unguarded.
        char Text[0x2000];
        Fdump(kFdumpNotice, Line, FormatMessage2000(Text, Fmt, PlayKey->GetArchId()));
    }

    // FUN_06ee5f00 (payload at node+8).
    unsigned char MaybeMigrateHwSw(FSoundManager* Manager, FSoundInstanceNode* Node,
                                   unsigned long HwUsed)
    {
        unsigned long Flags = Node->StatusFlags;
        if ((Flags & (kInstanceSoftware | kInstanceFrequency)) != 0)
            return 0;
        if (Node->NotifyEvent != nullptr)
            return 0;
        if ((Flags & kInstanceStarted) != 0)
        {
            if (G_AllowHwToSw == 0 && (Flags & kInstanceHardware) != 0)
                return 0;
            if (G_AllowSwToHw == 0 && (Flags & kInstanceHardware) == 0)
                return 0;
        }
        Flags &= ~kInstanceWantHardware;
        Node->StatusFlags = Flags;
        const unsigned char bIs3d = Node->PlayKey->Is3d();
        if (bIs3d != 0)
        {
            if ((Manager->CapsFlags & kSoundFlagHw3D) != 0 && Node->DistSq < Node->MaxDistSq &&
                HwUsed < Manager->FreeHwMixingBuffers)
                Node->StatusFlags |= kInstanceWantHardware;
        }
        else if ((Manager->CapsFlags & kSoundFlagAll2DInSoftware) == 0 && (Manager->CapsFlags & kSoundFlagHw2D) != 0 &&
                 HwUsed < Manager->FreeHwMixingBuffers)
        {
            Node->StatusFlags |= kInstanceWantHardware;
        }
        Flags = Node->StatusFlags;
        const unsigned char bNeed = static_cast<unsigned char>(((Flags >> 3) ^ Flags) & 1);
        if (bNeed == 0)
            return 0;
        Flags &= ~kInstanceHardware;
        Node->StatusFlags = Flags;
        if (G_DisplayExtraDebug == 0)
            return bNeed;
        char Line[0x2000];
        if (HwUsed > Manager->FreeHwMixingBuffers)
        {
            Fdump(kFdumpNotice, 0xC2B,
                  FormatMessage2000(
                      Line, "Had to remove HW instance of arch %d with priority %d greater than %d.\n",
                      Node->PlayKey->GetArchId(), static_cast<int>(HwUsed),
                      static_cast<int>(Manager->FreeHwMixingBuffers)));
            return bNeed;
        }
        // Retail tests the flags after clearing bit 0, so this notice never fires; kept as retail.
        if ((Flags & kInstanceHardware) != 0)
        {
            Fdump(kFdumpNotice, 0xC30,
                  FormatMessage2000(Line, "Had to stop HW instance of arch %d to move to SW.\n",
                                    Node->PlayKey->GetArchId()));
        }
        return bNeed;
    }

    // FUN_06ee56e0: factory Initialize hook, always 0.
    unsigned long SoundManagerFactoryInitialize(void* /*Unused*/)
    {
        return 0;
    }

    FAttachedNode* MakeAttachedSentinel()
    {
        // FUN_06ee7d80 head node (0xC); retail stores the self-links into the result unchecked.
        FAttachedNode* Node = static_cast<FAttachedNode*>(calloc(1, sizeof(FAttachedNode)));
        Node->Next = Node;
        Node->Prev = Node;
        return Node;
    }

    // ---- WavLib (statically linked into SoundManager.dll, 06ee8660..06ee8d4d) ----

    HACMDRIVER G_WavLibDriver = nullptr; // DAT_06eeaf7c

    // Instance block handed to acmDriverEnum by FUN_06ee8d00: {name to match, found id}.
    struct FAcmDriverSearch
    {
        const char* LongName;  // +0
        HACMDRIVERID DriverId; // +4
    };

    // LAB_06ee8c60 ACMDRIVERENUMCB: TRUE keeps enumerating, FALSE stops (match stored).
    BOOL CALLBACK WavLibAcmEnum(HACMDRIVERID DriverId, DWORD_PTR Instance, DWORD /*Support*/)
    {
        FAcmDriverSearch* Search = reinterpret_cast<FAcmDriverSearch*>(Instance);
        if (Search == nullptr || Search->LongName == nullptr)
            return TRUE;
        ACMDRIVERDETAILSA Details;
        Details.cbStruct = 0x398;
        if (acmDriverDetailsA(DriverId, &Details, 0) != 0)
            return TRUE;
        // SOUND-ACM-DECODE-ONLY (docs/re/deviations.md): deviation, the decode-only codec is
        // accepted too. Owner-requested; remove only on owner instruction.
        if (strcmp(Search->LongName, Details.szLongName) != 0 &&
            strcmp(Details.szLongName, "Fraunhofer IIS MPEG Layer-3 Codec (decode only)") != 0)
            return TRUE;
        Search->DriverId = DriverId;
        return FALSE;
    }

    // FUN_06ee8d00 cdecl: enumerate for LongName, then open whatever id the search left
    // (still 0 when nothing matched - acmDriverOpen then fails).
    HACMDRIVER WavLibOpenAcmDriver(const char* LongName)
    {
        FAcmDriverSearch Search;
        Search.LongName = LongName;
        Search.DriverId = nullptr;
        if (acmDriverEnum(WavLibAcmEnum, reinterpret_cast<DWORD_PTR>(&Search), 0) != 0)
            return nullptr;
        HACMDRIVER Driver = nullptr;
        if (acmDriverOpen(&Driver, Search.DriverId, 0) != 0)
            return nullptr;
        return Driver;
    }

    // FUN_06ee8660: unconditional (the caller FUN_06ee86c0 tests DAT_06eeaf7c first).
    // The name is .data:06eeae38 - the "(advanced)" codec, same as SoundStreamer's.
    void WavLibInitAcmDriver()
    {
        G_WavLibDriver = WavLibOpenAcmDriver("Fraunhofer IIS MPEG Layer-3 Codec (advanced)");
        if (G_WavLibDriver == nullptr)
            FdumpWavLib(kFdumpError, 0x20, "StreamingSound: missing MP3 codec?");
    }

    // One-slot sink WavLib hands the decoded sound to (slot 0, thiscall, RET 0xC). The only
    // implementation is FUN_06ee1240's stack object (vtable PTR_LAB_06ee9210, FArchetypeWavSink).
    // It is consumed while the file mapping (PCM) or the ACM output buffer is still live.
    class FWavSink
    {
    public:
        virtual unsigned char Consume(const unsigned short* Fmt8, const void* Samples,
                                      unsigned long ByteCount) = 0;
    };

    // Stack object of FUN_06ee8960: {file system, map handle, view, mapped size, read cursor}.
    // Its destructor FUN_06ee8c00 is the EH unwind action (LAB_06ee8f88) and is inlined on every
    // normal exit as well.
    struct FWavLibMap
    {
        IFileSystem* FileSystem;   // +0x00
        int Handle;                // +0x04
        const unsigned char* View; // +0x08
        unsigned long Size;        // +0x0C
        unsigned long Cursor;      // +0x10

        // FUN_06ee8c00
        ~FWavLibMap()
        {
            if (Handle != -1 && FileSystem != nullptr)
            {
                FileSystem->CloseFile(static_cast<unsigned long>(Handle)); // +0x10
                Handle = -1;
            }
        }

        // FUN_06ee8c30.
        const unsigned char* Range(unsigned long Offset, unsigned long Length) const
        {
            if (Handle == -1)
                return nullptr;
            if (Offset + Length > Size)
                return nullptr;
            return View + Offset;
        }
    };

    // FUN_06ee86c0: whole-file ACM decode of a compressed WAV; the PCM goes to the sink
    // and is freed afterwards. Trim (encoder delay) and fact (sample count) are honoured only
    // when positive.
    unsigned char WavLibAcmDecode(const WAVEFORMATEX* Src, const unsigned char* Data, unsigned long DataSize,
                                  unsigned long TrimSamples, unsigned long FactSamples, FWavSink* Sink)
    {
        if (G_WavLibDriver == nullptr)
            WavLibInitAcmDriver();
        ACMSTREAMHEADER Header;
        memset(&Header, 0, sizeof(Header));
        WAVEFORMATEX Dst;                    // retail sets only the tag before the suggest
        Dst.wFormatTag = WAVE_FORMAT_PCM;
        unsigned char bOk = 0;
        HACMSTREAM Acm = nullptr;
        if (acmFormatSuggest(G_WavLibDriver, const_cast<WAVEFORMATEX*>(Src), &Dst, 0x12,
                             ACM_FORMATSUGGESTF_WFORMATTAG) != 0)
            return 0;
        if (acmStreamOpen(&Acm, G_WavLibDriver, const_cast<WAVEFORMATEX*>(Src), &Dst, nullptr, 0, 0,
                          ACM_STREAMOPENF_NONREALTIME) != 0)
            return 0;
        Header.cbStruct = 0x54;
        Header.cbSrcLength = DataSize;
        if (DataSize < Src->nBlockAlign) // unsigned
            Header.cbSrcLength = Src->nBlockAlign;
        Header.cbSrcLengthUsed = 0;
        Header.cbDstLength = 0;
        if (acmStreamSize(Acm, Header.cbSrcLength, &Header.cbDstLength, ACM_STREAMSIZEF_SOURCE) == 0)
        {
            Header.cbDstLength += 0x2000;
            Header.cbDstLengthUsed = 0;
            Header.pbSrc = const_cast<unsigned char*>(Data);
            Header.pbDst = static_cast<unsigned char*>(malloc(Header.cbDstLength));
            if (Header.pbDst != nullptr && acmStreamPrepareHeader(Acm, &Header, 0) == 0)
            {
                if (acmStreamConvert(Acm, &Header, ACM_STREAMCONVERTF_END) != 0 ||
                    Header.cbDstLengthUsed == 0)
                    FdumpWavLib(kFdumpError, 0x119, "WavLib: failed to decompress sound.\n");
                else if (Header.cbSrcLength != Header.cbSrcLengthUsed)
                    FdumpWavLib(kFdumpError, 0x120, "WavLib: did not decode entire sound.\n");
                else
                {
                    unsigned long Used = Header.cbDstLengthUsed;
                    if (Used == Header.cbDstLength)
                        FdumpWavLib(kFdumpWarning, 0x129, "WavLib: possible incomplete ACM decode.\n");
                    // Signed division by 8.
                    const int FrameBytes =
                        (static_cast<int>(Dst.wBitsPerSample) * static_cast<int>(Dst.nChannels)) / 8;
                    const unsigned char* Pcm = Header.pbDst;
                    const int Trim = static_cast<int>(TrimSamples);
                    if (Trim > 0)
                    {
                        const unsigned long Skip = static_cast<unsigned long>(FrameBytes * Trim);
                        if (Skip < Used)
                        {
                            Pcm += Skip; // the pointer advances; nothing is moved
                            Used -= Skip;
                        }
                    }
                    const int Fact = static_cast<int>(FactSamples);
                    if (Fact > 0)
                    {
                        const unsigned long Cap = static_cast<unsigned long>(FrameBytes * Fact);
                        if (Used > Cap)
                            Used = Cap;
                    }
                    unsigned short Fmt8[4];
                    Fmt8[0] = Dst.nChannels;
                    Fmt8[1] = static_cast<unsigned short>(Dst.wBitsPerSample >> 3);
                    Fmt8[2] = static_cast<unsigned short>(Dst.nSamplesPerSec);
                    Fmt8[3] = static_cast<unsigned short>(Fmt8[1] * Fmt8[0]);
                    bOk = Sink->Consume(Fmt8, Pcm, Used);
                }
            }
        }
        // Common exit.
        if (Acm != nullptr)
        {
            if (Header.cbStruct != 0)
                acmStreamUnprepareHeader(Acm, &Header, 0);
            if (Header.pbDst != nullptr)
            {
                free(Header.pbDst);
                Header.pbDst = nullptr;
            }
            acmStreamClose(Acm, 0);
        }
        return bOk;
    }

    // FUN_06ee8960: map the file through the caller's IFileSystem (+0x3C/+0x40/+0x28),
    // walk RIFF/WAVE, and hand fmt/data to the sink while the mapping is open. PCM goes to the
    // sink as is; anything else through FUN_06ee86c0. Returns the sink's result, 0 on any
    // failure. No path and no other file access.
    unsigned char WavLibLoad(IFileSystem* FileSystem, FWavSink* Sink)
    {
        FWavLibMap Map;
        Map.FileSystem = FileSystem;
        Map.Handle = -1;
        Map.View = nullptr;
        Map.Size = 0;
        Map.Cursor = 0;
        if (FileSystem == nullptr)
            return 0;
        Map.Handle = FileSystem->CreateMapping(0, 0, 2, 0, 0, 0);
        if (Map.Handle == -1)
            return 0;
        Map.View = reinterpret_cast<const unsigned char*>(static_cast<uintptr_t>(static_cast<unsigned int>(
            FileSystem->MapFileView(static_cast<unsigned long>(Map.Handle), 4, 0, 0, 0))));
        Map.Size = FileSystem->GetSize(0, 0);
        Map.Cursor = 0;
        if (Map.View == nullptr)
            return 0;
        const unsigned long* Riff = reinterpret_cast<const unsigned long*>(Map.Range(0, 0xC));
        if (Riff == nullptr)
            return 0;
        Map.Cursor += 0xC;
        if (Riff[0] != 0x46464952 || Riff[2] != 0x45564157) // "RIFF" / "WAVE"
            return 0;
        // A chunk pointer addresses its 8-byte header; the last chunk of each kind wins.
        const unsigned long* FmtChunk = nullptr;
        const unsigned long* FactChunk = nullptr;
        const unsigned long* TrimChunk = nullptr;
        const unsigned long* DataChunk = nullptr;
        for (;;)
        {
            const unsigned long* Chunk = reinterpret_cast<const unsigned long*>(Map.Range(Map.Cursor, 8));
            if (Chunk == nullptr)
                break;
            Map.Cursor += 8;
            const unsigned long Id = Chunk[0];
            if (Id == 0x74636166) // "fact"
                FactChunk = Chunk;
            else if (Id == 0x6D697274) // "trim"
                TrimChunk = Chunk;
            else if (Id == 0x20746D66) // "fmt "
                FmtChunk = Chunk;
            else if (Id == 0x61746164) // "data"
                DataChunk = Chunk;
            // No even-size padding; the signed sum is clamped to [0, mapped size].
            long Next = static_cast<long>(Chunk[1] + Map.Cursor);
            if (Next < 0)
                Next = 0;
            if (Next > static_cast<long>(Map.Size))
                Next = static_cast<long>(Map.Size);
            Map.Cursor = static_cast<unsigned long>(Next);
        }
        if (FmtChunk == nullptr || DataChunk == nullptr)
            return 0;
        const WAVEFORMATEX* Format = reinterpret_cast<const WAVEFORMATEX*>(FmtChunk + 2);
        const unsigned short Tag = Format->wFormatTag;
        if (FmtChunk[1] < (Tag == WAVE_FORMAT_PCM ? 0xEu : 0x12u))
            return 0;
        unsigned long FactSamples = 0xFFFFFFFF;
        if (FactChunk != nullptr && FactChunk[1] >= 4)
            FactSamples = FactChunk[2];
        unsigned long TrimSamples = 0xFFFFFFFF;
        if (TrimChunk != nullptr && TrimChunk[1] == 4)
            TrimSamples = TrimChunk[2];
        const unsigned char* Samples = reinterpret_cast<const unsigned char*>(DataChunk + 2);
        if (Tag != WAVE_FORMAT_PCM)
            return WavLibAcmDecode(Format, Samples, DataChunk[1], TrimSamples, FactSamples, Sink);
        unsigned short Fmt8[4];
        Fmt8[0] = Format->nChannels;
        Fmt8[1] = static_cast<unsigned short>(Format->wBitsPerSample >> 3);
        Fmt8[2] = static_cast<unsigned short>(Format->nSamplesPerSec);
        Fmt8[3] = static_cast<unsigned short>(Fmt8[1] * Fmt8[0]);
        return Sink->Consume(Fmt8, Samples, DataChunk[1]);
    }

    // Stack object of FUN_06ee1240 (vtable PTR_LAB_06ee9210).
    class FArchetypeWavSink final : public FWavSink
    {
    public:
        FSoundArchetype* Archetype; // +0x04
        IDirectSound* DirectSound;  // +0x08
        unsigned long Flags;        // +0x0C

        // LAB_06ee12f0: no zero test on the block size; loop end is the frame count.
        unsigned char Consume(const unsigned short* Fmt8, const void* Samples,
                              unsigned long ByteCount) override
        {
            const unsigned long Frames = ByteCount / Fmt8[3];
            const GENRESULT Hr =
                Archetype->FillFromPcm(Fmt8, ByteCount, 0, Frames, Samples, DirectSound, Flags);
            return Hr == GR_OK ? 1 : 0;
        }
    };

    // Inlined in FUN_06ee64c0: the 0x5F3759DF inverse square root, times x. The products stay
    // on the x87 stack until the final store; the port is x87 too (/arch:IA32), and its double
    // locals are exact spills of those register values, so the single float rounding point is
    // the same.
    float QuakeDistance(float DistSq)
    {
        long Bits = *reinterpret_cast<long*>(&DistSq);
        Bits = 0x5F3759DF - (Bits >> 1);
        const float Y = *reinterpret_cast<float*>(&Bits);
        const double YD = Y;
        const double XD = DistSq;
        const double Half = ((YD * YD) * XD) * static_cast<double>(0.5f);
        return static_cast<float>(((static_cast<double>(1.5f) - Half) * YD) * XD);
    }

    // Inlined in FUN_06ee64c0, only when DistSq > MinDistSq:
    // 10 * log10(Min / (Min + Rolloff * (Dist * DistanceFactor - Min))) + ClientAtten. Kept as
    // x87 code because FYL2X is not bit-identical to the CRT log10. No guard on the log argument.
    float SoftwareAttenuation(float MinDistance, float Dist, float DistanceFactor,
                              float RolloffFactor, float ClientAtten)
    {
        static const double kTen = 10.0; // DAT_06ee9480
        float Result;
        __asm
        {
            fld MinDistance
            fld Dist
            fmul DistanceFactor
            fsub st(0), st(1)
            fmul RolloffFactor
            fadd st(0), st(1)
            fdivr st(0), st(1)
            fldlg2
            fxch st(1)
            fyl2x
            fmul kTen
            fstp st(1)
            fadd ClientAtten
            fstp Result
        }
        return Result;
    }

    // FUN_06ee1670: clamp to DAT_06ee9220 = -100 / DAT_06ee9250 = 100, then
    // pow(DAT_06ee9248 = 2.0, pitch * DAT_06ee9240 = 0.01f). NaN is clamped by neither test.
    // The product is not rounded to float and _CIpow leaves the result on the FPU stack, so
    // the multiplier is a double.
    double PitchToMultiplier(float Pitch)
    {
        if (Pitch < -100.0f)
            Pitch = -100.0f;
        else if (Pitch > 100.0f)
            Pitch = 100.0f;
        return pow(2.0, static_cast<double>(Pitch) * static_cast<double>(0.01f));
    }

    // MSVCRT ftol: truncate toward zero into a 64-bit integer; the callers keep EAX.
    unsigned long Ftol32(double Value)
    {
        return static_cast<unsigned long>(static_cast<long long>(Value));
    }

    // FUN_06ee16c0: DurationMs (+0x1C), scaled by pitch when flags (+0x20) bit 1 is set. A NaN
    // pitch is not "== 0" and goes on to the division. The duration is rounded to float first;
    // the division stays on the FPU. No play-key null check.
    unsigned long ScaledDurationMs(FSoundArchetype* Archetype, ISoundSource* PlayKey)
    {
        if (Archetype == nullptr)
            return 0;
        unsigned long Duration = Archetype->DurationMs;
        if ((Archetype->Flags & kSoundArchFrequency) == 0)
            return Duration;
        float Pitch = 0.0f;
        if (PlayKey->GetPitch(&Pitch) != 0)
            return Duration;
        if (Pitch == 0.0f)
            return Duration;
        const float DurationF = static_cast<float>(static_cast<unsigned long long>(Duration));
        return Ftol32(static_cast<double>(DurationF) / PitchToMultiplier(Pitch));
    }

    // FUN_06ee1040: CapsFlags, MinDistance and MinDistSq are left alone.
    void InitInstancePayload(FSoundInstanceNode* Node)
    {
        Node->PlayKey = nullptr;
        Node->Buffer = nullptr;
        Node->Buffer3d = nullptr;
        Node->NotifyEvent = nullptr;
        Node->LoopStartBytes = 0;
        Node->LoopEndBytes = 0;
        Node->StatusFlags = 0;
        Node->DsCapsFlags = 0;
        Node->DistSq = 0.0f;
        Node->MaxDistance = 0.0f;
        Node->MaxDistSq = 0.0f;
        Node->Position.x = 0.0f;
        Node->Position.y = 0.0f;
        Node->Position.z = 0.0f;
        Node->Direction.x = 0.0f;
        Node->Direction.y = 0.0f;
        Node->Direction.z = 0.0f;
        Node->bIs3dCache = 0;
        Node->LastStartTick = 0;
    }

    // FUN_06ee11a0.
    void ReleaseInstancePayload(FSoundInstanceNode* Node)
    {
        if (Node->Buffer3d != nullptr)
        {
            Node->Buffer3d->Release();
            Node->Buffer3d = nullptr;
        }
        if (Node->Buffer != nullptr)
        {
            Node->Buffer->Stop();
            Node->Buffer->Release();
            Node->Buffer = nullptr;
        }
        if (Node->PlayKey != nullptr)
        {
            Node->PlayKey->Release();
            Node->PlayKey = nullptr;
        }
    }

    // 06ee17f0 (_onexit, once-guard byte DAT_06eeaf24): erase every node still on the list, then
    // release the head. The port's head is the embedded sentinel.
    FInstanceListInit::~FInstanceListInit()
    {
        FSoundInstanceNode* Node = G_InstanceSentinel.Next;
        while (Node != &G_InstanceSentinel)
        {
            FSoundInstanceNode* Next = Node->Next;
            Node->Prev->Next = Node->Next;
            Node->Next->Prev = Node->Prev;
            ReleaseInstancePayload(Node);
            free(Node);
            --G_InstanceCount;
            Node = Next;
        }
        G_InstanceCount = 0;
    }

    // FUN_06ee10a0.
    void AssignInstancePayload(FSoundInstanceNode* Dest, const FSoundInstanceNode* Src)
    {
        ReleaseInstancePayload(Dest);
        Dest->PlayKey = Src->PlayKey;
        if (Dest->PlayKey != nullptr)
            Dest->PlayKey->AddRef();
        Dest->Buffer = Src->Buffer;
        if (Dest->Buffer != nullptr)
            Dest->Buffer->AddRef();
        Dest->Buffer3d = Src->Buffer3d;
        if (Dest->Buffer3d != nullptr)
            Dest->Buffer3d->AddRef();
        Dest->NotifyEvent = Src->NotifyEvent;
        Dest->LoopStartBytes = Src->LoopStartBytes;
        Dest->LoopEndBytes = Src->LoopEndBytes;
        Dest->CapsFlags = Src->CapsFlags;
        Dest->StatusFlags = Src->StatusFlags;
        Dest->DsCapsFlags = Src->DsCapsFlags;
        Dest->DistSq = Src->DistSq;
        Dest->MaxDistance = Src->MaxDistance;
        Dest->MaxDistSq = Src->MaxDistSq;
        Dest->MinDistance = Src->MinDistance;
        Dest->MinDistSq = Src->MinDistSq;
        Dest->Position = Src->Position;
        Dest->Direction = Src->Direction;
        Dest->bIs3dCache = Src->bIs3dCache;
        Dest->LastStartTick = Src->LastStartTick;
    }

    // FUN_06ee1080.
    void CopyConstructInstancePayload(FSoundInstanceNode* Dest, const FSoundInstanceNode* Src)
    {
        Dest->PlayKey = nullptr;
        Dest->Buffer = nullptr;
        Dest->Buffer3d = nullptr;
        AssignInstancePayload(Dest, Src);
    }

    // FUN_06ee6410. No null test on the locked pointer; the Unlock result is ignored.
    int UploadSoundBuffer(IDirectSoundBuffer* Buffer, const void* Samples, unsigned long ByteCount)
    {
        void* Ptr1 = nullptr;
        DWORD Bytes1 = 0;
        if (Buffer->Lock(0, 0, &Ptr1, &Bytes1, nullptr, nullptr, DSBLOCK_ENTIREBUFFER) < 0)
        {
            Fdump(kFdumpError, 0xD03, "Couldn't lock sound buffer.\n");
            return -1;
        }
        unsigned long CopyBytes = ByteCount;
        if (ByteCount > Bytes1)
            CopyBytes = Bytes1;
        memcpy(Ptr1, Samples, CopyBytes);
        Buffer->Unlock(Ptr1, Bytes1, nullptr, 0);
        return 0;
    }

    // FUN_06ee18e0: DS-notify thread. Waits on every node's notify event plus DAT_06eeaf70; a
    // looping sound wraps to LoopStartBytes, a one-shot's event is closed.
    DWORD WINAPI NotifyThreadProc(void* /*Unused*/)
    {
        HANDLE* Handles = nullptr;
        // Retail's play-cursor slot is never zeroed and keeps its value across iterations when
        // GetCurrentPosition fails.
        DWORD PlayPos = 0;
        for (;;)
        {
            EnterCriticalSection(&G_InstanceLock);
            const unsigned long AllocBytes = G_InstanceCount * 4u + 4u;
            Handles = static_cast<HANDLE*>(operator new(AllocBytes));
            memset(Handles, 0, AllocBytes);
            unsigned long Count = 0;
            FSoundInstanceNode* Node = G_InstanceSentinel.Next;
            while (Node != &G_InstanceSentinel)
            {
                if (Node->NotifyEvent != nullptr)
                {
                    Handles[Count] = Node->NotifyEvent;
                    ++Count;
                }
                Node = Node->Next;
            }
            LeaveCriticalSection(&G_InstanceLock);
            Handles[Count] = G_MixerEvent;
            ++Count;
            const DWORD Wait = WaitForMultipleObjects(Count, Handles, FALSE, INFINITE);
            if (Wait == WAIT_FAILED)
                break;
            HANDLE Signaled = Handles[Wait];
            if (Signaled == G_MixerEvent)
            {
                ResetEvent(G_MixerEvent);
            }
            else
            {
                EnterCriticalSection(&G_InstanceLock);
                Node = G_InstanceSentinel.Next;
                while (Node != &G_InstanceSentinel)
                {
                    if (Node->NotifyEvent == Signaled)
                    {
                        if (Node->PlayKey->IsLooping() != 0)
                        {
                            // The result is not tested.
                            Node->Buffer->GetCurrentPosition(&PlayPos, nullptr);
                            if (PlayPos > Node->LoopEndBytes)
                                Node->Buffer->SetCurrentPosition(Node->LoopStartBytes);
                            ResetEvent(Signaled);
                        }
                        else
                        {
                            CloseHandle(Node->NotifyEvent);
                            Node->NotifyEvent = nullptr;
                        }
                    }
                    Node = Node->Next;
                }
                LeaveCriticalSection(&G_InstanceLock);
            }
            operator delete(Handles);
            Handles = nullptr;
            if (G_MixerStopFlag != 0)
                break;
        }
        if (Handles != nullptr)
            operator delete(Handles);
        return 0;
    }

    float ClampDsDistance(float Value)
    {
        if (Value < 1.0f)
            return 1.0f;
        if (Value > 1.0e9f)
            return 1.0e9f;
        return Value;
    }

    // FUN_06ee1000: { IDirectSoundBuffer* +0x00, DSBUFFERDESC +0x04 }.
    struct FDsBufferDescWrapper
    {
        IDirectSoundBuffer* Buffer;
        DSBUFFERDESC Desc;
    };

    FDsBufferDescWrapper* InitDsBufferDescWrapper(FDsBufferDescWrapper* This, WAVEFORMATEX* Format)
    {
        This->Buffer = nullptr;
        memset(&This->Desc, 0, sizeof(This->Desc));
        This->Desc.dwSize = 0x24;
        This->Desc.lpwfxFormat = Format;
        This->Desc.dwFlags = DSBCAPS_CTRL3D;
        This->Desc.dwBufferBytes = 4;
        return This;
    }

    // FUN_06ee7600.
    void ReleaseDsBufferWrapper(FDsBufferDescWrapper* This)
    {
        if (This->Buffer != nullptr)
        {
            This->Buffer->Release();
            This->Buffer = nullptr;
        }
    }
}

FSoundArchetype::FSoundArchetype()
    : nChannels(0), BytesPerSample(0), nSamplesPerSec(0), nBlockAlign(0), LoopStart(0), LoopEnd(0),
      NumFrames(0), Gain(0.0f), DurationMs(0), Flags(0), Buffer(nullptr), bSamplesReady(0),
      RefCount(1) // retail: the caller of FUN_06ee11f0 stores 1 at +0x2C, not the ctor
{
    Pad29[0] = 0;
    Pad29[1] = 0;
    Pad29[2] = 0;
}

FSoundArchetype::~FSoundArchetype()
{
    // FUN_06ee1220.
    if (Buffer != nullptr)
    {
        Buffer->Release();
        Buffer = nullptr;
    }
}

GENRESULT DACOM_API FSoundArchetype::QueryInterface(const char* Iid, void** Out)
{
    // LAB_06ee8190: the one-entry table DAT_06ee94b0; no null checks on Iid or Out.
    if (!Iids::EqualNoNullTest(Iid, Iids::SoundArchetype))
    {
        *Out = nullptr; // only after the table walk
        return GR_INTERFACE_UNSUPPORTED;
    }
    AddRef();                                 // before the store
    *Out = static_cast<ISoundArchetype*>(this);
    return GR_OK;
}

unsigned long DACOM_API FSoundArchetype::AddRef()
{
    ++RefCount;
    return RefCount;
}

unsigned long DACOM_API FSoundArchetype::Release()
{
    if (RefCount > 0)
        --RefCount;
    const unsigned long Remaining = RefCount;
    if (Remaining == 0)
    {
        RefCount = 1;
        delete this;
        return 0;
    }
    return Remaining;
}

GENRESULT DACOM_API FSoundArchetype::GetFormat(void* Out)
{
    // LAB_06ee1530: two dwords from +4. No null check. Retail's EAX is left holding [this+8];
    // callers ignore the result.
    memcpy(Out, &nChannels, 8);
    unsigned long Packed = 0;
    memcpy(&Packed, &nSamplesPerSec, 4);
    return static_cast<GENRESULT>(Packed);
}

GENRESULT DACOM_API FSoundArchetype::GetSamples(void* /*Out*/)
{
    // LAB_06ee1550. Not supported in retail either.
    Fdump(kFdumpError, 0x204, "SoundManager: get_samples() function is not supported."); // 06eea140
    return GR_OK;
}

unsigned long DACOM_API FSoundArchetype::GetNumFrames()
{
    return NumFrames;
}

float DACOM_API FSoundArchetype::GetGain()
{
    // LAB_06ee1590.
    return Gain;
}

unsigned long DACOM_API FSoundArchetype::GetChannelCount()
{
    // LAB_06ee15a0.
    return nChannels;
}

unsigned long DACOM_API FSoundArchetype::GetDurationMs()
{
    // LAB_06ee15b0.
    return DurationMs;
}

GENRESULT DACOM_API FSoundArchetype::SetSamples(void* /*A*/, void* /*B*/)
{
    // LAB_06ee15f0. Not supported in retail either; retail returns whatever EAX holds.
    Fdump(kFdumpError, 0x23A, "SoundManager: function set_samples() not supported.\n"); // 06eea178
    return GR_OK;
}

GENRESULT DACOM_API FSoundArchetype::SetGain(float InGain)
{
    // LAB_06ee1620: clamp to [DAT_06ee9228 = -100, DAT_06ee9238 = 0].
    Gain = InGain;
    if (Gain > 0.0f)
        Gain = 0.0f;
    else if (Gain < -100.0f)
        Gain = -100.0f;
    bSamplesReady = 1;
    return GR_OK;
}

unsigned long DACOM_API FSoundArchetype::IsReady()
{
    // LAB_06ee15c0.
    return 1;
}

GENRESULT DACOM_API FSoundArchetype::GetLoopRange(unsigned long* Start, unsigned long* End)
{
    // LAB_06ee15d0: no null check; retail's EAX is left holding LoopEnd.
    *Start = LoopStart;
    *End = LoopEnd;
    return static_cast<GENRESULT>(LoopEnd);
}

GENRESULT FSoundArchetype::FillFromPcm(const unsigned short* Fmt8, unsigned long ByteCount,
                                       unsigned long InLoopStart, unsigned long InLoopEnd,
                                       const void* Samples, IDirectSound* DirectSound,
                                       unsigned long InFlags)
{
    // FUN_06ee1330.
    if (Buffer != nullptr)
    {
        Buffer->Release();
        Buffer = nullptr;
    }
    Flags = InFlags;
    memcpy(&nChannels, Fmt8, 8);
    LoopStart = InLoopStart;
    LoopEnd = InLoopEnd;
    // Unsigned divisions with no zero guards, as retail.
    NumFrames = ByteCount / nBlockAlign;
    DurationMs = (NumFrames * 1000) / nSamplesPerSec;

    // The format's block align is NOT the input align but bits * channels * 0.125
    // (DAT_06ee9218) through ftol, truncated to a word.
    WAVEFORMATEX Wave;
    Wave.wFormatTag = WAVE_FORMAT_PCM;
    Wave.nChannels = nChannels;
    Wave.nSamplesPerSec = nSamplesPerSec;
    const unsigned short Bits =
        static_cast<unsigned short>(static_cast<int>(nBlockAlign) * 8 / static_cast<int>(nChannels));
    Wave.wBitsPerSample = Bits;
    const unsigned short AvgAlign = static_cast<unsigned short>(
        Ftol32(static_cast<double>(static_cast<int>(Bits) * static_cast<int>(nChannels)) * 0.125));
    Wave.nBlockAlign = AvgAlign;
    Wave.nAvgBytesPerSec = static_cast<unsigned long>(AvgAlign) * nSamplesPerSec;
    Wave.cbSize = 0;

    DSBUFFERDESC Desc = {};
    Desc.dwSize = 0x24;
    Desc.dwBufferBytes = ByteCount;
    Desc.dwFlags = G_Ds3dCreateFlags;
    Desc.lpwfxFormat = &Wave;

    if ((InFlags & kSoundArch3d) != 0 && (InFlags & kSoundArchPan) != 0)
    {
        Fdump(kFdumpWarning, 0x1AC, "SoundMgr:Archetype requested 3D and Pan controls, disabling pan.\n"); // 06eea0e0
        Flags &= ~kSoundArchPan;
    }
    if ((Flags & kSoundArch3d) != 0)
        Desc.dwFlags = (Desc.dwFlags & ~DSBCAPS_CTRLPAN) | DSBCAPS_CTRL3D;
    else if ((Flags & kSoundArchPan) != 0)
        Desc.dwFlags = (Desc.dwFlags & ~DSBCAPS_CTRL3D) | DSBCAPS_CTRLPAN;
    if ((Flags & kSoundArchFrequency) != 0)
    {
        Desc.dwFlags |= DSBCAPS_CTRLFREQUENCY;
        if (G_ForceFreqToSw != 0)
            Flags |= kSoundArchSoftware;
    }
    if ((Flags & kSoundArchMute3dAtMaxDistance) != 0 && (Desc.dwFlags & DSBCAPS_CTRL3D) != 0)
        Desc.dwFlags |= DSBCAPS_MUTE3DATMAXDISTANCE;
    if ((Flags & kSoundArchLoopNotify) != 0 && (LoopStart != 0 || LoopEnd < NumFrames))
        Desc.dwFlags |= DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_CTRLPOSITIONNOTIFY;
    if ((Flags & kSoundArchSoftware) != 0)
        Desc.dwFlags = (Desc.dwFlags & ~DSBCAPS_LOCDEFER) | DSBCAPS_LOCSOFTWARE;

    if (DirectSound->CreateSoundBuffer(&Desc, &Buffer, nullptr) < 0)
    {
        Buffer = nullptr;
        return GR_GENERIC;
    }
    // A failed upload keeps Buffer.
    if (UploadSoundBuffer(Buffer, Samples, ByteCount) < 0)
        return GR_GENERIC;
    bSamplesReady = 1;
    return GR_OK;
}

GENRESULT FSoundArchetype::FillFromWav(IDAComponent* FileSystem, IDirectSound* DirectSound,
                                       unsigned long InFlags)
{
    // FUN_06ee1240. The failure message takes the name from FS+0x84. Nothing else reads the
    // file.
    FArchetypeWavSink Sink;
    Sink.Archetype = this;
    Sink.DirectSound = DirectSound;
    Sink.Flags = InFlags;
    IFileSystem* Fs = static_cast<IFileSystem*>(FileSystem);
    if (WavLibLoad(Fs, &Sink) != 0)
        return GR_OK;
    char Named[0x80];
    Fs->GetFileSysPath(Named, 0x80);
    char Message[0x2000];
    Fdump(kFdumpWarning, 0x17D,
          FormatMessage2000(Message, "SoundMgr:Couldn't load wav file <%s>.\n", Named));
    return GR_GENERIC;
}

GENRESULT FSoundArchetype::DuplicateBuffer(IDirectSound* DirectSound, IDirectSoundBuffer** Out) const
{
    if (Out == nullptr || DirectSound == nullptr || Buffer == nullptr)
        return GR_GENERIC;
    *Out = nullptr;
    if (FAILED(DirectSound->DuplicateSoundBuffer(Buffer, Out)) || *Out == nullptr)
        return GR_GENERIC;
    return GR_OK;
}

FSoundInner::FSoundInner() : RefCount(1), Self(nullptr) {}

GENRESULT DACOM_API FSoundInner::QueryInterface(const char* Iid, void** Out)
{
    // LAB_06ee8460: table DAT_06ee95cc {"1.11_ISoundManager", +0}, {"1.11_ISoundManagerStatus",
    // +4}; no null checks. The hit is AddRef'd through its own vtable, which goes to the
    // controlling unknown.
    if (Iids::EqualNoNullTest(Iid, Iids::SoundManager))
    {
        ISoundManager* Found = static_cast<ISoundManager*>(Self);
        Found->AddRef();
        *Out = Found;
        return GR_OK;
    }
    if (Iids::EqualNoNullTest(Iid, Iids::SoundManagerStatus))
    {
        ISoundManagerStatus* Found = static_cast<ISoundManagerStatus*>(Self);
        Found->AddRef();
        *Out = Found;
        return GR_OK;
    }
    *Out = nullptr;
    return GR_INTERFACE_UNSUPPORTED;
}

unsigned long DACOM_API FSoundInner::AddRef()
{
    ++RefCount;
    return RefCount;
}

unsigned long DACOM_API FSoundInner::Release()
{
    if (RefCount > 0)
        --RefCount;
    const unsigned long Remaining = RefCount;
    if (Remaining == 0)
    {
        // LAB_06ee8520. The dtor FUN_06ee1de0 itself calls ShutdownDevice.
        RefCount = 1;
        FSoundManager* Owner = Self;
        if (Owner != nullptr)
        {
            // The object came from calloc.
            Owner->~FSoundManager();
            free(Owner);
        }
        return 0;
    }
    return Remaining;
}

FSoundManagerFactory::FSoundManagerFactory() : FDacomComponentFactory(kComponentName) {}

GENRESULT DACOM_API FSoundManagerFactory::CreateInstance(AGGDESC* Desc, void** Instance)
{
    // FUN_06ee8310. No null checks on Desc, its name or Instance.
    FSoundAggDesc* SoundDesc = reinterpret_cast<FSoundAggDesc*>(Desc);
    if (SoundDesc->Size != kAggDescSize || strcmp(SoundDesc->Name, Name) != 0)
    {
        *Instance = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    // calloc: every field FUN_06ee1b40 does not store (DeviceGuid +0x38, Window +0xF4, ...)
    // starts zeroed.
    void* Memory = calloc(1, sizeof(FSoundManager));
    if (Memory == nullptr)
    {
        *Instance = nullptr;
        return GR_OUT_OF_MEMORY;
    }
    FSoundManager* Object = new (Memory) FSoundManager();

    Object->BindOuter(SoundDesc);
    // FUN_06ee56e0 always returns 0, so retail's failure tail cannot run.
    SoundManagerFactoryInitialize(Object);
    *Instance = static_cast<ISoundManager*>(Object);
    return GR_OK;
}

FSoundManager::FSoundManager()
    : bParserBound(0), MasterGain(0.0f), CurrentTickMs(0), FreeHwMixingBuffers(0), MaxChannels(0), EaxEnv(0),
      EaxVol(0.0f), EaxDecay(0.0f), EaxDamp(0.0f), SavedEaxEnv(0), SavedEaxVol(0), SavedEaxDecay(0), SavedEaxDamp(0),
      ReverbMix(0.0f), Ds3dAlgGuid0(0), Ds3dAlgGuid1(0), Ds3dAlgGuid2(0), Ds3dAlgGuid3(0), Flags(kSoundFlagHw2D | kSoundFlagHw3D | kSoundFlagRightHanded | kSoundFlagAlgLight), CapsFlags(0),
      FreeHw3dBuffers(0), SampleRate(0x5622), Channels(2), BitsPerSample(0x10), CoordScale(1),
      DistanceFactor(1.0f), DopplerFactor(1.0f), RolloffFactor(1.0f), Max3dPan(10000.0f),
      Window(nullptr), ArchEntries(nullptr), ArchCount(0), ArchCap(0), bArchEmpty(1), Pad108(0),
      AttachedListAllocator(nullptr), AttachedSentinel(nullptr), AttachedCount(0),
      InstanceListAllocator(nullptr), InstanceSentinel(nullptr), InstanceEmbeddedCount(0), NextArchId(1),
      Listener(nullptr), DirectSound(nullptr), Primary(nullptr), Parser(nullptr),
      bCoInitialized(0), Controlling(nullptr)
{
    // FUN_06ee1b40. The global critical section and event are (re)created on every
    // construction.
    AttachedSentinel = MakeAttachedSentinel();
    // FUN_06ee7d50 head node (0x60), self-linked unchecked.
    InstanceSentinel = static_cast<FSoundInstanceNode*>(calloc(1, sizeof(FSoundInstanceNode)));
    InstanceSentinel->Next = InstanceSentinel;
    InstanceSentinel->Prev = InstanceSentinel;
    InstanceEmbeddedCount = 0;
    InitializeCriticalSection(&G_InstanceLock);
    memset(DeviceIdString, 0, sizeof(DeviceIdString));
    Front.x = 0.0f;
    Front.y = 0.0f;
    Front.z = 1.0f;
    Up.x = 0.0f;
    Up.y = 1.0f;
    Up.z = 0.0f;
    PadC0[0] = 0;
    PadC0[1] = 0;
    PadC0[2] = 0;
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
    Velocity.x = 0.0f;
    Velocity.y = 0.0f;
    Velocity.z = 0.0f;
    Pad105[0] = 0;
    Pad105[1] = 0;
    Pad105[2] = 0;
    Pad139[0] = 0;
    Pad139[1] = 0;
    Pad139[2] = 0;
    Inner.Self = this;
    Controlling = &Inner;
    G_MixerEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
}

FSoundManager::~FSoundManager()
{
    // FUN_06ee1de0, in retail order; ReleaseCom is the member-container teardown.
    ShutdownDevice();
    DeleteCriticalSection(&G_InstanceLock);
    CloseHandle(G_MixerEvent);
    if (Parser != nullptr)
    {
        Parser->Release();
        Parser = nullptr;
    }
    ReleaseCom();
}

void FSoundManager::BindOuter(FSoundAggDesc* Desc)
{
    // Inner unknown at +0x140 (vtable 06ee95c0), controlling unknown at +0x13C. With an outer,
    // *desc+0xC = inner is written unconditionally.
    Inner.RefCount = 1;
    Inner.Self = this;
    if (Desc->Outer != nullptr)
    {
        Controlling = Desc->Outer;
        *Desc->InnerOut = &Inner;
    }
    else
    {
        Controlling = &Inner;
    }
}

void FSoundManager::ReleaseCom()
{
    // Member-container teardown of FUN_06ee1de0, after ShutdownDevice already emptied the global
    // list, the attached list and the archetype map and released the DirectSound objects.
    // +0x118 pending list: FUN_06ee7590 per node, then FUN_06ee7d10 frees the head.
    FSoundInstanceNode* Pending = InstanceSentinel->Next;
    while (Pending != InstanceSentinel)
    {
        FSoundInstanceNode* Next = Pending->Next;
        Pending->Prev->Next = Pending->Next;
        Pending->Next->Prev = Pending->Prev;
        ReleaseInstancePayload(Pending);
        free(Pending);
        --InstanceEmbeddedCount;
        Pending = Next;
    }
    free(InstanceSentinel);
    InstanceSentinel = nullptr;
    InstanceEmbeddedCount = 0;
    // +0x10C attached list: FUN_06ee7510 per node (the play key is not released), then the head.
    FAttachedNode* Attached = AttachedSentinel->Next;
    while (Attached != AttachedSentinel)
    {
        FAttachedNode* Next = Attached->Next;
        free(Attached);
        Attached = Next;
    }
    free(AttachedSentinel);
    AttachedSentinel = nullptr;
    AttachedCount = 0;
    // +0xF8 map (retail std::map, FUN_06ee7ad0 / FUN_06ee7cd0): already emptied by FUN_06ee4800
    // inside ShutdownDevice; only the storage is left.
    free(ArchEntries);
    ArchEntries = nullptr;
    ArchCount = 0;
    ArchCap = 0;
    bArchEmpty = 1;
}

// FUN_06ee8560 / 8580 / 85a0: forward to the controlling unknown at +0x13C. No null check.
GENRESULT DACOM_API FSoundManager::QueryInterface(const char* Iid, void** Out)
{
    return Controlling->QueryInterface(Iid, Out);
}

unsigned long DACOM_API FSoundManager::AddRef()
{
    return Controlling->AddRef();
}

unsigned long DACOM_API FSoundManager::Release()
{
    return Controlling->Release();
}

GENRESULT DACOM_API FSoundManager::SetPcmFormat(unsigned short* Fmt)
{
    SampleRate = Fmt[2];
    Channels = Fmt[0];
    BitsPerSample = static_cast<unsigned long>(Fmt[3]) << 3;
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::GetPcmFormat(unsigned short* Fmt)
{
    // LAB_06ee1ff0. No null check, no zero-channel check.
    Fmt[2] = static_cast<unsigned short>(SampleRate);
    Fmt[0] = static_cast<unsigned short>(Channels);
    Fmt[3] = static_cast<unsigned short>(BitsPerSample >> 3);
    Fmt[1] = static_cast<unsigned short>(static_cast<unsigned long>(Fmt[3]) / Channels);
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::SetMasterGain(float Gain)
{
    MasterGain = Gain;
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::SetMaxChannels(unsigned long Count)
{
    MaxChannels = Count;
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::SetSpeakerConfig(unsigned long Index)
{
    if (DirectSound == nullptr || Index < 1 || Index >= 0xB)
        return GR_GENERIC;
    DirectSound->SetSpeakerConfig(kSpeakerTable[Index]);
    return GR_OK;
}

float DACOM_API FSoundManager::GetMasterGain()
{
    return MasterGain;
}

unsigned long DACOM_API FSoundManager::GetMaxChannels()
{
    return MaxChannels;
}

unsigned long DACOM_API FSoundManager::GetSpeakerConfig()
{
    if (DirectSound == nullptr)
        return 0xC;
    DWORD Config = 0;
    if (DirectSound->GetSpeakerConfig(&Config) < 0)
        return 0xC;
    unsigned long Index = 0;
    while (Index < 0xB)
    {
        if (kSpeakerTable[Index] == Config)
            return Index;
        ++Index;
    }
    return 0xC;
}

GENRESULT DACOM_API FSoundManager::SetEaxPreview(int Env, float Vol, float Decay, float Damp)
{
    // LAB_06ee2100. An out-of-range Env becomes 0 (not clamped to 0x19). The values are stored
    // before the Flags bit 2 test. The probe buffer comes from FUN_06ee1000. The success path
    // does not Release the property set (retail leak, kept).
    if (DirectSound == nullptr)
        return GR_GENERIC;

    EaxEnv = (static_cast<unsigned int>(Env) < 0x1Au) ? Env : 0;
    // Each clamp compares the float against a double constant; NaN is stored unchanged.
    // Bounds DAT_06ee9238 0.0 / 9440 1.0, 9438 0.1 / 9430 100.0, 9238 0.0 / 9248 2.0.
    if (static_cast<double>(Vol) < 0.0)
        EaxVol = static_cast<float>(0.0);
    else if (static_cast<double>(Vol) > 1.0)
        EaxVol = static_cast<float>(1.0);
    else
        EaxVol = Vol;
    if (static_cast<double>(Decay) < 0.1)
        EaxDecay = static_cast<float>(0.1);
    else if (static_cast<double>(Decay) > 100.0)
        EaxDecay = static_cast<float>(100.0);
    else
        EaxDecay = Decay;
    if (static_cast<double>(Damp) < 0.0)
        EaxDamp = static_cast<float>(0.0);
    else if (static_cast<double>(Damp) > 2.0)
        EaxDamp = static_cast<float>(2.0);
    else
        EaxDamp = Damp;

    if ((Flags & kSoundFlagEax) == 0)
        return GR_GENERIC;

    WAVEFORMATEX Format;
    Format.wFormatTag = 1;
    Format.nChannels = 2;
    Format.nSamplesPerSec = 0x5622;
    Format.nAvgBytesPerSec = 0x15888;
    Format.nBlockAlign = 4;
    Format.wBitsPerSample = 0x10;
    Format.cbSize = 0;

    FDsBufferDescWrapper Wrapper;
    InitDsBufferDescWrapper(&Wrapper, &Format);
    if (DirectSound->CreateSoundBuffer(&Wrapper.Desc, &Wrapper.Buffer, nullptr) != 0)
    {
        ReleaseDsBufferWrapper(&Wrapper);
        return GR_GENERIC;
    }

    IKsPropertySet* PropertySet = nullptr;
    if (Wrapper.Buffer->QueryInterface(kIidKsPropertySet, reinterpret_cast<void**>(&PropertySet)) !=
        0)
    {
        ReleaseDsBufferWrapper(&Wrapper);
        return GR_GENERIC;
    }

    ULONG Support = 0;
    if (PropertySet->QuerySupport(kDsPropSetEax20Buffer, 0, &Support) != 0 || (Support & (kKsPropertySupportGet | kKsPropertySupportSet)) == 0)
    {
        PropertySet->Release();
        ReleaseDsBufferWrapper(&Wrapper);
        return GR_GENERIC;
    }

    if ((CapsFlags & kSoundFlagEax) == 0)
    {
        // The buffer is not pre-zeroed and the Get result is ignored; whatever is there lands
        // in +0x68..+0x74.
        unsigned long Cached[4];
        PropertySet->Get(kDsPropSetEax20Buffer, 0, nullptr, 0, Cached, 0x10, nullptr);
        SavedEaxEnv = Cached[0];
        SavedEaxVol = Cached[1];
        SavedEaxDecay = Cached[2];
        SavedEaxDamp = Cached[3];
        CapsFlags |= kSoundFlagEax;
    }

    struct FEaxBlob
    {
        unsigned long Env;
        float Vol;
        float Decay;
        float Damp;
    };
    FEaxBlob Blob;
    Blob.Env = static_cast<unsigned long>(EaxEnv);
    Blob.Vol = EaxVol;
    Blob.Decay = EaxDecay;
    Blob.Damp = EaxDamp;
    PropertySet->Set(kDsPropSetEax20Buffer, 0, nullptr, 0, &Blob, 0x10);
    ReleaseDsBufferWrapper(&Wrapper);
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::GetEaxPreview(unsigned long* Env, unsigned long* Vol,
                                                unsigned long* Decay, unsigned long* Damp)
{
    // LAB_06ee2350: raw dword copies of +0x58..+0x64. No null checks. Retail's EAX is left
    // holding the +0x64 dword.
    *Env = static_cast<unsigned long>(EaxEnv);
    memcpy(Vol, &EaxVol, sizeof(float));
    memcpy(Decay, &EaxDecay, sizeof(float));
    unsigned long DampBits;
    memcpy(&DampBits, &EaxDamp, sizeof(float));
    *Damp = DampBits;
    return static_cast<GENRESULT>(DampBits);
}

void FSoundManager::ApplyListener()
{
    // FUN_06ee6d50: push the stored +0xA8..+0xEC as-is. Every listener call here and in the
    // setters below is DS3D_DEFERRED; the mixer commits them.
    if (Listener == nullptr)
        return;
    Listener->SetOrientation(Front.x, Front.y, Front.z, Up.x, Up.y, Up.z, DS3D_DEFERRED);
    Listener->SetPosition(Position.x, Position.y, Position.z, DS3D_DEFERRED);
    Listener->SetVelocity(Velocity.x, Velocity.y, Velocity.z, DS3D_DEFERRED);
    Listener->SetDistanceFactor(DistanceFactor, DS3D_DEFERRED);
    Listener->SetDopplerFactor(DopplerFactor, DS3D_DEFERRED);
    Listener->SetRolloffFactor(RolloffFactor, DS3D_DEFERRED);
}

GENRESULT DACOM_API FSoundManager::SetOrientation(Vector* InFront, Vector* InUp)
{
    // LAB_06ee23d0: Front is stored negated; both Z components are scaled by CoordScale (+0xA4).
    const float Scale = static_cast<float>(CoordScale);
    Front.x = -InFront->x;
    Front.y = -InFront->y;
    Front.z = -InFront->z * Scale;
    Up.x = InUp->x;
    Up.y = InUp->y;
    Up.z = InUp->z * Scale;
    if (Listener != nullptr)
    {
        Listener->SetOrientation(Front.x, Front.y, Front.z, Up.x, Up.y, Up.z, DS3D_DEFERRED);
    }
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::SetOrientationFromMatrix(const Matrix* Orient)
{
    // LAB_06ee2380: Front = column 2, Up = column 1, through the vtable (+0x34).
    Vector InUp;
    Vector InFront;
    InUp.x = Orient->E[0][1];
    InUp.y = Orient->E[1][1];
    InUp.z = Orient->E[2][1];
    InFront.x = Orient->E[0][2];
    InFront.y = Orient->E[1][2];
    InFront.z = Orient->E[2][2];
    return SetOrientation(&InFront, &InUp);
}

GENRESULT DACOM_API FSoundManager::SetListenerPosition(Vector* Pos)
{
    // LAB_06ee24a0: the scaled Z is stored back.
    const float Scale = static_cast<float>(CoordScale);
    Position.x = Pos->x;
    Position.y = Pos->y;
    Position.z = Pos->z * Scale;
    if (Listener != nullptr)
        Listener->SetPosition(Position.x, Position.y, Position.z, DS3D_DEFERRED);
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::SetListenerVelocity(Vector* Vel)
{
    // LAB_06ee2500: the scaled Z is stored back.
    const float Scale = static_cast<float>(CoordScale);
    Velocity.x = Vel->x;
    Velocity.y = Vel->y;
    Velocity.z = Vel->z * Scale;
    if (Listener != nullptr)
        Listener->SetVelocity(Velocity.x, Velocity.y, Velocity.z, DS3D_DEFERRED);
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::SetDistanceFactor(float Value)
{
    DistanceFactor = Value;
    if (Listener != nullptr)
        Listener->SetDistanceFactor(Value, DS3D_DEFERRED);
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::SetDopplerFactor(float Value)
{
    DopplerFactor = Value;
    if (Listener != nullptr)
        Listener->SetDopplerFactor(Value, DS3D_DEFERRED);
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::SetRolloffFactor(float Value)
{
    RolloffFactor = Value;
    if (Listener != nullptr)
        Listener->SetRolloffFactor(Value, DS3D_DEFERRED);
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::GetOrientationSpace(Vector* Pos, Vector* Vel)
{
    // LAB_06ee2730: undoes SetOrientation's negation of Front.
    const float Scale = static_cast<float>(CoordScale);
    Pos->x = Front.x;
    Pos->y = Front.y;
    Pos->z = Front.z * Scale;
    Pos->x = -Pos->x;
    Pos->y = -Pos->y;
    Pos->z = -Pos->z;
    Vel->x = Up.x;
    Vel->y = Up.y;
    Vel->z = Up.z * Scale;
    // Retail's EAX is left holding the second out pointer.
    return static_cast<GENRESULT>(reinterpret_cast<uintptr_t>(Vel));
}

GENRESULT DACOM_API FSoundManager::GetOrientationMatrix(Matrix* Out9)
{
    // LAB_06ee25f0. Right comes from the stored +0xA8 / +0xB4; Flags bit 4 (+0x8C) picks the
    // operand order. Each term is a product of two floats (exact) and one subtraction, rounded
    // once to float. Column-major out: [0/3/6] Right (Z scaled), [1/4/7] Up, [2/5/8] Front.
    Vector SpaceFront;
    Vector SpaceUp;
    GetOrientationSpace(&SpaceFront, &SpaceUp);
    const double Fx = Front.x;
    const double Fy = Front.y;
    const double Fz = Front.z;
    const double Ux = Up.x;
    const double Uy = Up.y;
    const double Uz = Up.z;
    Vector Right;
    if ((Flags & kSoundFlagRightHanded) != 0)
    {
        Right.x = static_cast<float>(Uz * Fy - Fz * Uy);
        Right.y = static_cast<float>(Fz * Ux - Uz * Fx);
        Right.z = static_cast<float>(Fx * Uy - Ux * Fy);
    }
    else
    {
        Right.x = static_cast<float>(Uy * Fz - Fy * Uz);
        Right.y = static_cast<float>(Fx * Uz - Fz * Ux);
        Right.z = static_cast<float>(Fy * Ux - Fx * Uy);
    }
    Out9->E[0][0] = Right.x;
    Out9->E[0][1] = SpaceUp.x;
    Out9->E[0][2] = SpaceFront.x;
    Out9->E[1][0] = Right.y;
    Out9->E[1][1] = SpaceUp.y;
    Out9->E[1][2] = SpaceFront.y;
    Out9->E[2][0] = static_cast<float>(static_cast<double>(CoordScale) * Right.z);
    Out9->E[2][1] = SpaceUp.z;
    Out9->E[2][2] = SpaceFront.z;
    // Retail returns Out9 in EAX.
    return static_cast<GENRESULT>(reinterpret_cast<uintptr_t>(Out9));
}

GENRESULT DACOM_API FSoundManager::GetListenerPosition(Vector* Out)
{
    // FUN_06ee27c0. Retail returns Out in EAX (both getters).
    Out->x = Position.x;
    Out->y = Position.y;
    Out->z = Position.z * static_cast<float>(CoordScale);
    return static_cast<GENRESULT>(reinterpret_cast<uintptr_t>(Out));
}

GENRESULT DACOM_API FSoundManager::GetListenerVelocity(Vector* Out)
{
    // LAB_06ee2810.
    Out->x = Velocity.x;
    Out->y = Velocity.y;
    Out->z = Velocity.z * static_cast<float>(CoordScale);
    return static_cast<GENRESULT>(reinterpret_cast<uintptr_t>(Out));
}

float DACOM_API FSoundManager::GetDistanceFactor()
{
    return DistanceFactor;
}

float DACOM_API FSoundManager::GetDopplerFactor()
{
    return DopplerFactor;
}

float DACOM_API FSoundManager::GetRolloffFactor()
{
    return RolloffFactor;
}

GENRESULT DACOM_API FSoundManager::EaxGetInstance(void* Id, GUID* Set, unsigned long Prop, void* Buf,
                                                 unsigned long Cb, unsigned long* Pcb)
{
    // LAB_06ee2890: the global instance list, matched on node+8. The Get result is ignored.
    GENRESULT Result = GR_GENERIC;
    FSoundInstanceNode* Node = G_InstanceSentinel.Next;
    while (Node != &G_InstanceSentinel)
    {
        if (Node->PlayKey == Id)
        {
            IDirectSoundBuffer* Buffer = Node->Buffer;
            IKsPropertySet* PropertySet = nullptr;
            if (Buffer->QueryInterface(kIidKsPropertySet, reinterpret_cast<void**>(&PropertySet)) == 0)
            {
                ULONG Support = 0;
                PropertySet->QuerySupport(*Set, Prop, &Support);
                if ((Support & kKsPropertySupportGet) != 0)
                {
                    PropertySet->Get(*Set, Prop, nullptr, 0, Buf, Cb, Pcb);
                    Result = GR_OK;
                }
                PropertySet->Release();
            }
            return Result;
        }
        Node = Node->Next;
    }
    return Result;
}

GENRESULT DACOM_API FSoundManager::EaxSetInstance(void* Id, GUID* Set, unsigned long Prop, void* Data,
                                                 unsigned long Cb)
{
    // LAB_06ee2a40: as EaxGetInstance.
    GENRESULT Result = GR_GENERIC;
    FSoundInstanceNode* Node = G_InstanceSentinel.Next;
    while (Node != &G_InstanceSentinel)
    {
        if (Node->PlayKey == Id)
        {
            IDirectSoundBuffer* Buffer = Node->Buffer;
            IKsPropertySet* PropertySet = nullptr;
            if (Buffer->QueryInterface(kIidKsPropertySet, reinterpret_cast<void**>(&PropertySet)) == 0)
            {
                ULONG Support = 0;
                PropertySet->QuerySupport(*Set, Prop, &Support);
                if ((Support & kKsPropertySupportSet) != 0)
                {
                    PropertySet->Set(*Set, Prop, nullptr, 0, Data, Cb);
                    Result = GR_OK;
                }
                PropertySet->Release();
            }
            return Result;
        }
        Node = Node->Next;
    }
    return Result;
}

GENRESULT DACOM_API FSoundManager::EaxGetPrimary(GUID* Set, unsigned long Prop, void* Buf,
                                                unsigned long Cb, unsigned long* Pcb)
{
    // LAB_06ee2940: a 22050/16/2 probe buffer is created, but the property set is queried on
    // the primary buffer (+0x130). The Get result is ignored.
    GENRESULT Result = GR_GENERIC;
    if (DirectSound == nullptr)
        return Result;

    WAVEFORMATEX Format;
    Format.wFormatTag = 1;
    Format.nChannels = 2;
    Format.nSamplesPerSec = 0x5622;
    Format.nAvgBytesPerSec = 0x15888;
    Format.nBlockAlign = 4;
    Format.wBitsPerSample = 0x10;
    Format.cbSize = 0;

    FDsBufferDescWrapper Wrapper;
    InitDsBufferDescWrapper(&Wrapper, &Format);
    DirectSound->CreateSoundBuffer(&Wrapper.Desc, &Wrapper.Buffer, nullptr);
    if (Wrapper.Buffer != nullptr)
    {
        IKsPropertySet* PropertySet = nullptr;
        if (Primary->QueryInterface(kIidKsPropertySet, reinterpret_cast<void**>(&PropertySet)) == 0)
        {
            ULONG Support = 0;
            PropertySet->QuerySupport(*Set, Prop, &Support);
            if ((Support & kKsPropertySupportGet) != 0)
            {
                PropertySet->Get(*Set, Prop, nullptr, 0, Buf, Cb, Pcb);
                Result = GR_OK;
            }
            PropertySet->Release();
        }
    }
    ReleaseDsBufferWrapper(&Wrapper);
    return Result;
}

GENRESULT DACOM_API FSoundManager::EaxSetPrimary(GUID* Set, unsigned long Prop, void* Data,
                                                unsigned long Cb)
{
    // LAB_06ee2af0: as EaxGetPrimary.
    GENRESULT Result = GR_GENERIC;
    if (DirectSound == nullptr)
        return Result;

    WAVEFORMATEX Format;
    Format.wFormatTag = 1;
    Format.nChannels = 2;
    Format.nSamplesPerSec = 0x5622;
    Format.nAvgBytesPerSec = 0x15888;
    Format.nBlockAlign = 4;
    Format.wBitsPerSample = 0x10;
    Format.cbSize = 0;

    FDsBufferDescWrapper Wrapper;
    InitDsBufferDescWrapper(&Wrapper, &Format);
    DirectSound->CreateSoundBuffer(&Wrapper.Desc, &Wrapper.Buffer, nullptr);
    if (Wrapper.Buffer != nullptr)
    {
        IKsPropertySet* PropertySet = nullptr;
        if (Primary->QueryInterface(kIidKsPropertySet, reinterpret_cast<void**>(&PropertySet)) == 0)
        {
            ULONG Support = 0;
            PropertySet->QuerySupport(*Set, Prop, &Support);
            if ((Support & kKsPropertySupportSet) != 0)
            {
                PropertySet->Set(*Set, Prop, nullptr, 0, Data, Cb);
                Result = GR_OK;
            }
            PropertySet->Release();
        }
    }
    ReleaseDsBufferWrapper(&Wrapper);
    return Result;
}

FSoundArchetype* FSoundManager::LookupArchetype(int Id)
{
    if (Id == -1)
        return nullptr;
    for (unsigned long Index = 0; Index < ArchCount; ++Index)
    {
        if (ArchEntries[Index].Id == Id)
        {
            ArchEntries[Index].Archetype->AddRef();
            return ArchEntries[Index].Archetype;
        }
    }
    return nullptr;
}

int FSoundManager::InsertArchetype(FSoundArchetype* Archetype)
{
    if (Archetype == nullptr)
        return -1;
    if (ArchCount == ArchCap)
    {
        const unsigned long NewCap = ArchCap == 0 ? 8 : ArchCap * 2;
        FArchMapEntry* Grown =
            static_cast<FArchMapEntry*>(realloc(ArchEntries, NewCap * sizeof(FArchMapEntry)));
        if (Grown == nullptr)
            return -1;
        ArchEntries = Grown;
        ArchCap = NewCap;
    }
    const int Id = static_cast<int>(NextArchId);
    ++NextArchId;
    ArchEntries[ArchCount].Id = Id;
    ArchEntries[ArchCount].Archetype = Archetype;
    ++ArchCount;
    bArchEmpty = 0;
    return Id;
}

void FSoundManager::StopInstancesForArch(int ArchId)
{
    // FUN_06ee4740. GetArchId is called unconditionally; the list walk itself is unlocked.
    FSoundInstanceNode* Node = G_InstanceSentinel.Next;
    while (Node != &G_InstanceSentinel)
    {
        FSoundInstanceNode* Next = Node->Next;
        if (Node->PlayKey->GetArchId() == ArchId)
        {
            EnterCriticalSection(&G_InstanceLock);
            UnlinkInstance(Node);
            SetEvent(G_MixerEvent);
            LeaveCriticalSection(&G_InstanceLock);
        }
        Node = Next;
    }
}

void FSoundManager::DestroyAllArchetypes()
{
    // FUN_06ee4800: in ascending id order. Deviation (deviations.md): retail walks an RB-tree at
    // +0xF8; the port keeps a linear array sorted by id (insert appends increasing ids, erase
    // shifts down), so the order matches.
    for (unsigned long Index = 0; Index < ArchCount; ++Index)
    {
        StopInstancesForArch(ArchEntries[Index].Id);
        ArchEntries[Index].Archetype->Release();
    }
    free(ArchEntries);
    ArchEntries = nullptr;
    ArchCount = 0;
    ArchCap = 0;
    bArchEmpty = 1;
}

void FSoundManager::ClearAttached()
{
    // Head of FUN_06ee33b0: first every attached key is Released with the list intact (no null
    // check), then every node is erased (FUN_06ee7510).
    FAttachedNode* Node = AttachedSentinel->Next;
    while (Node != AttachedSentinel)
    {
        Node->PlayKey->Release();
        Node = Node->Next;
    }
    Node = AttachedSentinel->Next;
    while (Node != AttachedSentinel)
    {
        FAttachedNode* Next = Node->Next;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        free(Node);
        --AttachedCount;
        Node = Next;
    }
}

void FSoundManager::UnlinkInstance(FSoundInstanceNode* Node)
{
    // Inline erase of the global list. The payload dtor only releases COM pointers: the notify
    // event (+0x14) is not closed. No underflow check.
    Node->Prev->Next = Node->Next;
    Node->Next->Prev = Node->Prev;
    ReleaseInstancePayload(Node);
    free(Node);
    --G_InstanceCount;
}

FSoundInstanceNode* FSoundManager::FindInstanceByKey(ISoundSource* PlayKey)
{
    // Inline walks of the global list comparing node+8 with the key; no key test.
    FSoundInstanceNode* Node = G_InstanceSentinel.Next;
    while (Node != &G_InstanceSentinel)
    {
        if (Node->PlayKey == PlayKey)
            return Node;
        Node = Node->Next;
    }
    return nullptr;
}

FAttachedNode* FSoundManager::FindAttached(ISoundSource* PlayKey)
{
    // Inline in the mixer: the attached list (+0x110 head) compared on node+8.
    FAttachedNode* Node = AttachedSentinel->Next;
    while (Node != AttachedSentinel)
    {
        if (Node->PlayKey == PlayKey)
            return Node;
        Node = Node->Next;
    }
    return nullptr;
}

void FSoundManager::RefreshInstancePose(FSoundInstanceNode* Node)
{
    // The mixer's existing-instance pose. DistSq stays on the FPU and is rounded once to float.
    // No null checks.
    if (Node->PlayKey->Is3d() == 0)
        return;
    Node->Position = Position;
    Node->MinDistance = 10.0f;
    Node->MaxDistance = 500.0f;
    Node->PlayKey->GetPosition(&Node->Position);
    Node->PlayKey->GetMaxDistance(&Node->MaxDistance);
    Node->PlayKey->GetMinDistance(&Node->MinDistance);
    Node->Position.z *= static_cast<float>(CoordScale);
    Node->MaxDistSq = Node->MaxDistance * Node->MaxDistance;
    Node->MinDistSq = Node->MinDistance * Node->MinDistance;
    Vector Delta;
    SubVec3(&Delta, &Node->Position, &Position);
    Node->Direction = Delta;
    const double DirX = Node->Direction.x;
    const double DirY = Node->Direction.y;
    const double DirZ = Node->Direction.z;
    Node->DistSq = static_cast<float>((DirX * DirX + DirY * DirY) + DirZ * DirZ);
}

unsigned char FSoundManager::MixerAllowCreate(ISoundSource* PlayKey)
{
    // Inline in the mixer. A distance-culled archetype (flags bit 0) is allowed only when
    // max^2 > dist^2; equal and NaN deny.
    if (PlayKey->Is3d() == 0)
        return 1;
    const int ArchId = PlayKey->GetArchId();
    FSoundArchetype* Archetype = LookupArchetype(ArchId);
    if (Archetype == nullptr)
        return 0;
    unsigned char bAllow = 1;
    if ((Archetype->Flags & kSoundArchMute3dAtMaxDistance) != 0)
    {
        // The position local is not pre-initialised; GetPosition fills it.
        Vector SrcPos;
        PlayKey->GetPosition(&SrcPos);
        SrcPos.z *= static_cast<float>(CoordScale);
        float MaxDist = 3.402823466e+38f; // 0x7f7fffff
        PlayKey->GetMaxDistance(&MaxDist);
        // Each difference is rounded to float.
        const float Dz = static_cast<float>(static_cast<double>(SrcPos.z) - Position.z);
        const float Dy = static_cast<float>(static_cast<double>(SrcPos.y) - Position.y);
        const float Dx = static_cast<float>(static_cast<double>(SrcPos.x) - Position.x);
        Vector Delta;
        StoreVec3(&Delta, Dx, Dy, Dz);
        const double DistSq = (static_cast<double>(Delta.z) * Delta.z +
                               static_cast<double>(Delta.y) * Delta.y) +
                              static_cast<double>(Delta.x) * Delta.x;
        const double MaxSq = static_cast<double>(MaxDist) * MaxDist;
        if (!(MaxSq > DistSq))
            bAllow = 0;
    }
    Archetype->Release();
    return bAllow;
}

void FSoundManager::LinkPendingInstance(FSoundInstanceNode* Node)
{
    // FUN_06ee7c00 inserts a new 0x60 node and copy-constructs the payload; the port links the
    // already-filled node instead. No checks.
    Node->Next = InstanceSentinel;
    Node->Prev = InstanceSentinel->Prev;
    InstanceSentinel->Prev->Next = Node;
    InstanceSentinel->Prev = Node;
    ++InstanceEmbeddedCount;
}

void FSoundManager::SplicePendingInstances()
{
    // FUN_06ee7c70 (raw _Splice of the whole +0x118 chain before head DAT_06eeaf04) plus the
    // count transfer. MixerTick already checked +0x120 != 0; the splice itself has no checks.
    FSoundInstanceNode* First = InstanceSentinel->Next;
    FSoundInstanceNode* Last = InstanceSentinel->Prev;
    InstanceSentinel->Next = InstanceSentinel;
    InstanceSentinel->Prev = InstanceSentinel;
    First->Prev = G_InstanceSentinel.Prev;
    Last->Next = &G_InstanceSentinel;
    G_InstanceSentinel.Prev->Next = First;
    G_InstanceSentinel.Prev = Last;
    G_InstanceCount += InstanceEmbeddedCount;
    InstanceEmbeddedCount = 0;
}

void FSoundManager::MixerApplyEax(FSoundInstanceNode* Node)
{
    // Tail of FUN_06ee64c0. The QuerySupport result is ignored; only its SET bit is tested.
    if ((CapsFlags & kSoundFlagEax) == 0)
        return;
    unsigned long Probe = 0;
    const int ProbeHr = Node->PlayKey->GetEaxEnvironment(&Probe);
    if (ProbeHr != 0)
        return;
    IKsPropertySet* PropertySet;
    if (Node->Buffer->QueryInterface(kIidKsPropertySet, reinterpret_cast<void**>(&PropertySet)) != 0)
        return;
    ULONG Support = 0;
    PropertySet->QuerySupport(kDsPropSetEax20Listener, 1, &Support);
    if ((Support & kKsPropertySupportSet) != 0)
        PropertySet->Set(kDsPropSetEax20Listener, 1, nullptr, 0, &Probe, sizeof(Probe));
    PropertySet->Release();
}

unsigned char FSoundManager::MixerApplyInstance(FSoundInstanceNode* Node)
{
    // FUN_06ee64c0. Retail is handed the payload (node+8) and the mixer ignores the result. The
    // pose is filled by FUN_06ee6e20, not here. No null checks on the play key or the buffer.
    ISoundSource* PlayKey = Node->PlayKey;

    // The value is handed to every DS3D setter as dwApply as-is, so mode 0 means
    // DS3D_DEFERRED (the mixer commits) and mode 1 DS3D_IMMEDIATE.
    unsigned long Apply = 1;
    unsigned long ApplyMode = 0;
    const int ApplyHr = PlayKey->GetApplyMode(&ApplyMode);
    if (ApplyHr >= 0)
    {
        if (ApplyMode == 0)
            Apply = 1;
        else if (ApplyMode == 1)
            Apply = 0;
        else
        {
            Fdump(kFdumpWarning, 0xD1A, "Invalid apply mode specified");
            Apply = 1;
        }
    }

    float ClientAtten = 0.0f;
    PlayKey->GetAttenuation(&ClientAtten);

    const unsigned char bWas3d = Node->bIs3dCache;
    const unsigned char bIs3d = PlayKey->Is3d();
    Node->bIs3dCache = bIs3d;

    // 3D: set when the sound just became 3D; 2D: set when it just stopped being 3D. Any
    // SetPan / SetVolume below sets it too.
    unsigned char bChanged;
    IDirectSound3DBuffer* Buffer3d = Node->Buffer3d;
    if (bIs3d != 0)
    {
        bChanged = bWas3d == 0 ? 1 : 0;
        if (Buffer3d != nullptr)
        {
            if (bWas3d == 0)
                Buffer3d->SetMode(DS3DMODE_NORMAL, Apply);
            if ((CapsFlags & kSoundFlagHw3D) != 0)
                Node->StatusFlags |= kInstanceWantHardware;

            unsigned long SoundMode = 0;
            if (PlayKey->GetSoundMode(&SoundMode) >= 0)
            {
                unsigned long DsMode = DS3DMODE_NORMAL;
                if (SoundMode == 1)
                    DsMode = DS3DMODE_HEADRELATIVE;
                else if (SoundMode != 0)
                {
                    Fdump(kFdumpWarning, 0xD4C, "Invalid sound mode specified.\n");
                    DsMode = DS3DMODE_NORMAL;
                }
                Buffer3d->SetMode(DsMode, Apply);
            }

            unsigned long Inside = 360;
            unsigned long Outside = 360;
            if (PlayKey->GetConeAngles(&Inside, &Outside) >= 0)
            {
                if (Inside > 360)
                    Inside = 360;
                if (Outside > 360)
                    Outside = 360;
                Buffer3d->SetConeAngles(Inside, Outside, Apply);

                // Zeroed vector, result ignored.
                Vector ConeDir = {};
                PlayKey->GetConeOrientation(&ConeDir);
                ConeDir.z = static_cast<float>(static_cast<double>(CoordScale) *
                                               static_cast<double>(ConeDir.z));
                Buffer3d->SetConeOrientation(ConeDir.x, ConeDir.y, ConeDir.z, Apply);

                // Zeroed, result ignored; NaN passes both clamps.
                float OutsideVol = 0.0f;
                PlayKey->GetConeOutsideVolume(&OutsideVol);
                float ScaledVol = OutsideVol * 100.0f;
                if (ScaledVol < -10000.0f)
                    ScaledVol = -10000.0f;
                else if (ScaledVol > 0.0f)
                    ScaledVol = 0.0f;
                Buffer3d->SetConeOutsideVolume(static_cast<LONG>(ScaledVol), Apply);
            }

            Vector Velocity = {};
            if (PlayKey->GetVelocity(&Velocity) >= 0)
            {
                Velocity.z = static_cast<float>(static_cast<double>(CoordScale) *
                                                static_cast<double>(Velocity.z));
                Buffer3d->SetVelocity(Velocity.x, Velocity.y, Velocity.z, Apply);
            }

            // The clamped distances are written back.
            Node->MinDistance = ClampDsDistance(Node->MinDistance);
            Buffer3d->SetMinDistance(Node->MinDistance, Apply);
            Node->MaxDistance = ClampDsDistance(Node->MaxDistance);
            Buffer3d->SetMaxDistance(Node->MaxDistance, Apply);
            Buffer3d->SetPosition(Node->Position.x, Node->Position.y, Node->Position.z, Apply);
        }
        else
        {
            // Software 3D: Dist = DistSq * invsqrt(DistSq) (one Newton step).
            const float Dist = QuakeDistance(Node->DistSq);
            if (Node->DistSq > Node->MinDistSq)
                ClientAtten = SoftwareAttenuation(Node->MinDistance, Dist, DistanceFactor,
                                                  RolloffFactor, ClientAtten);

            if ((Node->DsCapsFlags & DSBCAPS_CTRLPAN) != 0)
            {
                // Listener right = Up x Front on the FPU; the normalised direction is stored
                // back as floats. No clamp on the pan.
                const double RightX = static_cast<double>(Up.y) * Front.z -
                                      static_cast<double>(Up.z) * Front.y;
                const double RightY = static_cast<double>(Front.x) * Up.z -
                                      static_cast<double>(Front.z) * Up.x;
                const double RightZ = static_cast<double>(Front.y) * Up.x -
                                      static_cast<double>(Front.x) * Up.y;
                const double InvDist = 1.0 / static_cast<double>(Dist);
                Node->Direction.x = static_cast<float>(InvDist * Node->Direction.x);
                Node->Direction.y = static_cast<float>(InvDist * Node->Direction.y);
                Node->Direction.z = static_cast<float>(InvDist * Node->Direction.z);
                const double Dot = (static_cast<double>(Node->Direction.z) * RightZ +
                                    static_cast<double>(Node->Direction.y) * RightY) +
                                   static_cast<double>(Node->Direction.x) * RightX;
                const long WantPan = static_cast<long>(Ftol32(Dot * Max3dPan));
                long CurrentPan = 0;
                Node->Buffer->GetPan(&CurrentPan);
                if (WantPan != CurrentPan)
                {
                    Node->Buffer->SetPan(WantPan);
                    bChanged = 1;
                }
            }
        }
    }
    else
    {
        bChanged = bWas3d;
        if (Buffer3d != nullptr && bWas3d != 0)
            Buffer3d->SetMode(DS3DMODE_DISABLE, Apply);
        if ((Node->DsCapsFlags & DSBCAPS_CTRLPAN) != 0)
        {
            // Signed clamp.
            long ClientPan = 0;
            if (PlayKey->GetPan(&ClientPan) >= 0)
            {
                if (ClientPan < -10000)
                    ClientPan = -10000;
                else if (ClientPan > 10000)
                    ClientPan = 10000;
                long CurrentPan = 0;
                Node->Buffer->GetPan(&CurrentPan);
                if (ClientPan != CurrentPan)
                {
                    Node->Buffer->SetPan(ClientPan);
                    bChanged = 1;
                }
            }
        }
    }

    // FUN_06ee31d0. Retail does not test the lookup result; DestroyArchetype erases every
    // instance node of that archetype under the instance lock, so a node the mixer walks always
    // resolves.
    FSoundArchetype* Archetype = LookupArchetype(PlayKey->GetArchId());
    if ((Node->StatusFlags & kInstanceFrequency) != 0)
    {
        // Unsigned clamp to [100, 100000].
        const int Rate = static_cast<int>(Archetype->nSamplesPerSec);
        float Pitch;
        if (PlayKey->GetPitch(&Pitch) >= 0)
        {
            unsigned long Frequency = Ftol32(PitchToMultiplier(Pitch) * static_cast<double>(Rate));
            if (Frequency > 100000)
                Frequency = 100000;
            else if (Frequency < 100)
                Frequency = 100;
            if (Node->Buffer->SetFrequency(Frequency) < 0)
            {
                char Message[0x2000];
                Fdump(kFdumpWarning, 0xDFD,
                      FormatMessage2000(Message, "SoundMgr:Couldn't set freq to %d.\n",
                                        Frequency));
            }
        }
    }
    const float ArchGain = Archetype->Gain;
    Archetype->Release();

    // Computed on the FPU; NaN passes both clamps.
    double Volume = ((static_cast<double>(ArchGain) + MasterGain) + ClientAtten) * 100.0;
    if (Volume > 0.0)
        Volume = 0.0;
    else if (Volume < -10000.0)
        Volume = -10000.0;
    const long WantVolume = static_cast<long>(Ftol32(Volume));
    long CurrentVolume = 0;
    Node->Buffer->GetVolume(&CurrentVolume);
    if (WantVolume != CurrentVolume)
    {
        Node->Buffer->SetVolume(WantVolume);
        bChanged = 1;
    }

    MixerApplyEax(Node);
    return bChanged;
}

unsigned char FSoundManager::OneShotAlreadyExpired(ISoundSource* PlayKey)
{
    // FUN_06ee6070 (sole caller MixerTick). Unsigned compare. No play-key null check.
    if (PlayKey->IsLooping() != 0)
        return 0;
    const int ArchId = PlayKey->GetArchId();
    FSoundArchetype* Archetype = LookupArchetype(ArchId);
    if (Archetype == nullptr)
        return 1;
    const unsigned long DurationMs = ScaledDurationMs(Archetype, PlayKey);
    const unsigned long StartTick = PlayKey->GetStartTick();
    Archetype->Release();
    return CurrentTickMs > (DurationMs + StartTick) ? 1 : 0;
}

void FSoundManager::ApplyListenerFromSource(ISoundListenerPose* Pose)
{
    // FUN_06ee6e20 (sole caller MixerTick, which skips a null pose). The position and velocity
    // calls share one vector and the three factor calls share one float, so a failed getter
    // hands the next setter whatever the previous getter left there.
    Vector InFront;
    Vector InUp;
    InFront.x = 0.0f;
    InFront.y = 0.0f;
    InFront.z = 1.0f;
    InUp.x = 0.0f;
    InUp.y = 1.0f;
    InUp.z = 0.0f;
    Vector InVec;
    InVec.x = 0.0f;
    InVec.y = 0.0f;
    InVec.z = 0.0f;
    float InScalar = 1.0f;
    if (Pose->GetOrientation(&InFront, &InUp) >= 0)
        SetOrientation(&InFront, &InUp);
    if (Pose->GetPosition(&InVec) >= 0)
        SetListenerPosition(&InVec);
    if (Pose->GetVelocity(&InVec) >= 0)
        SetListenerVelocity(&InVec);
    if (Pose->GetDistanceFactor(&InScalar) >= 0)
        SetDistanceFactor(InScalar);
    if (Pose->GetDopplerFactor(&InScalar) >= 0)
        SetDopplerFactor(InScalar);
    if (Pose->GetRolloffFactor(&InScalar) >= 0)
        SetRolloffFactor(InScalar);
    ApplyListener();
}

unsigned char FSoundManager::MixerSetStartOffset(FSoundInstanceNode* Node)
{
    // FUN_06ee6b70 (sole caller MixerTick). The result gates the mixer's Play. No null checks.
    ISoundSource* PlayKey = Node->PlayKey;
    unsigned long Tick;
    if ((Node->StatusFlags & kInstanceStarted) == 0)
    {
        PlayKey->MarkStarted();
        Tick = PlayKey->GetStartTick();
    }
    else
    {
        Tick = PlayKey->GetStartTick();
        if (Node->LastStartTick == Tick)
            return 0;
    }
    Node->LastStartTick = Tick;
    if (G_NeverSetStartPos != 0)
        return 1;
    FSoundArchetype* Archetype = LookupArchetype(PlayKey->GetArchId());
    if (Archetype == nullptr)
        return 0;
    unsigned long Delta = CurrentTickMs - Tick;
    if ((Node->StatusFlags & kInstanceFrequency) != 0)
    {
        // A failed GetPitch clears bit 0x20.
        float Pitch;
        double Multiplier = 1.0;
        if (PlayKey->GetPitch(&Pitch) == 0)
            Multiplier = PitchToMultiplier(Pitch);
        else
            Node->StatusFlags &= ~kInstanceFrequency;
        Delta = Ftol32(static_cast<double>(static_cast<unsigned long long>(Delta)) * Multiplier);
    }
    // 0.001f is DAT_06ee949c.
    unsigned long Samples =
        Ftol32((static_cast<double>(static_cast<unsigned long long>(Delta)) *
                static_cast<double>(0.001f)) *
               static_cast<double>(static_cast<int>(Archetype->nSamplesPerSec)));
    // No zero-length loop guard in retail.
    if (PlayKey->IsLooping() != 0 && Samples > Archetype->LoopEnd)
        Samples = ((Samples - Archetype->LoopStart) % (Archetype->LoopEnd - Archetype->LoopStart)) +
                  Archetype->LoopStart;
    const unsigned long ByteOffset = static_cast<unsigned long>(Archetype->nBlockAlign) * Samples;
    const unsigned long MaxBytes =
        Archetype->NumFrames * static_cast<unsigned long>(Archetype->nBlockAlign);
    // Returns WITHOUT releasing the archetype the lookup AddRef'd: a retail reference leak,
    // reproduced on purpose.
    if (MaxBytes <= ByteOffset)
        return 0;
    if (G_DisplayExtraDebug != 0 && PlayKey->IsLooping() == 0)
    {
        char Line[0x2000];
        Fdump(kFdumpNotice, 0xEAC,
              FormatMessage2000(Line, "Setting start offset %d for non-looping instance of %d.\n", // 06eeacac
                                static_cast<int>(ByteOffset), PlayKey->GetArchId()));
    }
    Node->Buffer->SetCurrentPosition(ByteOffset);
    Archetype->Release();
    if ((Node->StatusFlags & kInstanceStarted) == 0)
        return 1;
    DWORD Status = 0;
    Node->Buffer->GetStatus(&Status);
    return static_cast<unsigned char>((~static_cast<unsigned char>(Status)) & 1);
}

FSoundInstanceNode* FSoundManager::CreateInstanceFromKey(ISoundSource* PlayKey)
{
    // FUN_06ee60e0 (sole caller MixerTick). Retail fills a stack payload that MixerTick freshly
    // zeroed, so its opening releases find nothing; the port fills a freshly zeroed node instead
    // and returns it (nullptr = retail's -1). No null checks on the key, the device or the
    // archetype buffer.
    char Message[0x2000];
    const int ArchId = PlayKey->GetArchId();
    FSoundArchetype* Archetype = LookupArchetype(ArchId);
    if (Archetype == nullptr)
    {
        if (ArchId != -1)
            Fdump(kFdumpWarning, 0xCE5,
                  FormatMessage2000(Message, "SoundMgr:Couldn't create an instance of archetype %d.\n", // 06eeab80
                                    ArchId));
        return nullptr;
    }

    // Port-only allocation (retail's list node comes from FUN_06ee7c00 in MixerTick).
    FSoundInstanceNode* Node = static_cast<FSoundInstanceNode*>(calloc(1, sizeof(FSoundInstanceNode)));
    if (Node == nullptr)
    {
        Archetype->Release();
        return nullptr;
    }
    InitInstancePayload(Node);

    // A failed duplicate falls into the lookup-miss tail, so both warnings are emitted.
    if (DirectSound->DuplicateSoundBuffer(Archetype->Buffer, &Node->Buffer) < 0)
    {
        Fdump(kFdumpWarning, 0xCDD,
              FormatMessage2000(Message, "SoundMgr:Couldn't duplicate archetype buffer %d.\n", ArchId)); // 06eeabb8
        Archetype->Release();
        Fdump(kFdumpWarning, 0xCE5,
              FormatMessage2000(Message, "SoundMgr:Couldn't create an instance of archetype %d.\n", // 06eeab80
                                ArchId));
        free(Node);
        return nullptr;
    }
    IDirectSoundBuffer* Dup = Node->Buffer;

    Node->PlayKey = PlayKey;
    PlayKey->AddRef();

    DSBCAPS Caps = {};
    Caps.dwSize = 0x14;
    Dup->GetCaps(&Caps);
    Node->DsCapsFlags = Caps.dwFlags;

    if ((Caps.dwFlags & DSBCAPS_CTRL3D) != 0)
    {
        // IID at DAT_06ee919c; the result is ignored.
        Dup->QueryInterface(kIidDirectSound3DBufferRetail, reinterpret_cast<void**>(&Node->Buffer3d));
        if (Node->Buffer3d != nullptr)
        {
            Node->StatusFlags |= kInstance3d;
            Node->bIs3dCache = 1;
        }
        else
            Node->StatusFlags &= ~kInstance3d;
    }

    if ((Caps.dwFlags & DSBCAPS_CTRLFREQUENCY) != 0)
    {
        // The notice asks GetArchId again.
        DWORD DummyFreq = 0;
        if (Dup->GetFrequency(&DummyFreq) >= 0)
            Node->StatusFlags |= kInstanceFrequency;
        else if (G_DisplayExtraDebug != 0)
            Fdump(kFdumpNotice, 0xCC1,
                  FormatMessage2000(Message, "Failed to get requested freq control for instance of %d.\n", // 06eeabec
                                    PlayKey->GetArchId()));
    }

    if ((Caps.dwFlags & DSBCAPS_LOCSOFTWARE) != 0)
        Node->StatusFlags = (Node->StatusFlags & ~kInstanceWantHardware) | kInstanceSoftware;

    if ((Caps.dwFlags & DSBCAPS_CTRLPOSITIONNOTIFY) != 0)
    {
        const unsigned long Align = Archetype->nBlockAlign;
        Node->LoopStartBytes = Archetype->LoopStart * Align;
        Node->LoopEndBytes = Archetype->LoopEnd * Align;
    }

    Archetype->Release();

    if (PlayKey->Is3d() != 0)
    {
        Node->Position = Position;
        Node->MinDistance = 10.0f;
        Node->MaxDistance = 500.0f;
        PlayKey->GetPosition(&Node->Position);
        PlayKey->GetMaxDistance(&Node->MaxDistance);
        PlayKey->GetMinDistance(&Node->MinDistance);
        Node->Position.z = Node->Position.z * static_cast<float>(CoordScale);
        Node->MaxDistSq = Node->MaxDistance * Node->MaxDistance;
        Node->MinDistSq = Node->MinDistance * Node->MinDistance;
        // Each difference is rounded to float (Z, Y, X); DistSq stays on the FPU and is
        // rounded once.
        const float DiffZ = static_cast<float>(static_cast<double>(Node->Position.z) - Position.z);
        const float DiffY = static_cast<float>(static_cast<double>(Node->Position.y) - Position.y);
        const float DiffX = static_cast<float>(static_cast<double>(Node->Position.x) - Position.x);
        StoreVec3(&Node->Direction, DiffX, DiffY, DiffZ);
        const double DirX = Node->Direction.x;
        const double DirY = Node->Direction.y;
        const double DirZ = Node->Direction.z;
        Node->DistSq = static_cast<float>((DirX * DirX + DirY * DirY) + DirZ * DirZ);
    }

    // MixerTick links it into the pending list (FUN_06ee7c00).
    return Node;
}

int DACOM_API FSoundManager::CreateArchetypePcm(void* Fmt, unsigned long ByteCount,
                                                unsigned long LoopStart, unsigned long LoopEnd,
                                                void* Samples, unsigned long ArchFlags)
{
    // FUN_06ee2be0: new 0x30 + FUN_06ee11f0, FUN_06ee1330 fill, FUN_06ee7e10 map insert. A null
    // operator new result is dereferenced unchecked, as retail.
    FSoundArchetype* Archetype = new (std::nothrow) FSoundArchetype();
    if (DirectSound == nullptr ||
        Archetype->FillFromPcm(static_cast<unsigned short*>(Fmt), ByteCount, LoopStart, LoopEnd,
                               Samples, DirectSound, ArchFlags) != GR_OK)
    {
        Archetype->Release();
        FdumpCouldntCreateArchetype(0x59D, static_cast<int>(NextArchId));
        return -1;
    }
    return InsertArchetype(Archetype);
}

int DACOM_API FSoundManager::CreateArchetypeFromSource(void* Source, unsigned long ArchFlags)
{
    // LAB_06ee2dc0: FUN_06ee2be0's sequence with the arguments read from a PCM descriptor
    // (format +0, loop start +0x8, loop end +0xC, byte count +0x14, samples +0x18), read only
    // once the device is known to exist. A null operator new result is dereferenced unchecked.
    FSoundArchetype* Archetype = new (std::nothrow) FSoundArchetype();
    const unsigned char* Desc = static_cast<const unsigned char*>(Source);
    if (DirectSound == nullptr ||
        Archetype->FillFromPcm(reinterpret_cast<const unsigned short*>(Desc),
                               *reinterpret_cast<const unsigned long*>(Desc + 0x14),
                               *reinterpret_cast<const unsigned long*>(Desc + 0x8),
                               *reinterpret_cast<const unsigned long*>(Desc + 0xC),
                               *reinterpret_cast<void* const*>(Desc + 0x18), DirectSound,
                               ArchFlags) != GR_OK)
    {
        Archetype->Release();
        FdumpCouldntCreateArchetype(0x5B3, static_cast<int>(NextArchId));
        return -1;
    }
    return InsertArchetype(Archetype);
}

int DACOM_API FSoundManager::CreateArchetypeFromWav(IDAComponent* FileSystem,
                                                    unsigned long ArchFlags)
{
    // LAB_06ee2f90. No file-system null test; a null operator new result is dereferenced
    // unchecked.
    FSoundArchetype* Archetype = new (std::nothrow) FSoundArchetype();
    if (DirectSound == nullptr || Archetype->FillFromWav(FileSystem, DirectSound, ArchFlags) != GR_OK)
    {
        Archetype->Release();
        FdumpCouldntCreateArchetype(0x5C5, static_cast<int>(NextArchId));
        return -1;
    }
    return InsertArchetype(Archetype);
}

GENRESULT DACOM_API FSoundManager::DestroyArchetype(int Id)
{
    // LAB_06ee3150: map find FUN_06ee80d0, erase FUN_06ee76d0. A null value fails only after
    // the erase. No lookup AddRef/Release pair; the id -1 is never a key.
    unsigned long Index = 0;
    while (Index < ArchCount && ArchEntries[Index].Id != Id)
        ++Index;
    if (Index == ArchCount)
        return GR_GENERIC;
    FSoundArchetype* Archetype = ArchEntries[Index].Archetype;
    // Erase keeping the ascending-id order the retail map iterates in.
    memmove(&ArchEntries[Index], &ArchEntries[Index + 1],
            (ArchCount - Index - 1) * sizeof(FArchMapEntry));
    --ArchCount;
    if (ArchCount == 0)
        bArchEmpty = 1;
    if (Archetype == nullptr)
        return GR_GENERIC;
    StopInstancesForArch(Id);
    Archetype->Release();
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::QueryArchetype(int Id, void** Out)
{
    // LAB_06ee3250.
    if (Out == nullptr)
        return GR_GENERIC;
    FSoundArchetype* Archetype = LookupArchetype(Id);
    if (Archetype == nullptr)
    {
        *Out = nullptr;
        return GR_GENERIC;
    }
    const GENRESULT Result = Archetype->QueryInterface(Iids::SoundArchetype, Out);
    if (Result != GR_OK)
    {
        Fdump(kFdumpWarning, 0x627, "Failed to query the IID_ISoundArchetype.\n"); // 06eea214
        Archetype->Release();
        return GR_GENERIC;
    }
    Archetype->Release();
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::AttachPlayKey(ISoundSource* PlayKey, unsigned long Index)
{
    // FUN_06ee32e0: insert before the Index-th node, or at the end when Index >= count
    // (unsigned). The head (+0x110) always exists, so retail does not test it.
    if (PlayKey == nullptr)
        return GR_GENERIC;
    ISoundSource* Key = PlayKey;
    Key->AddRef();
    FAttachedNode* InsertBefore = AttachedSentinel;
    if (Index < AttachedCount)
    {
        InsertBefore = AttachedSentinel->Next;
        unsigned long Walk = 0;
        while (Walk < Index && InsertBefore != AttachedSentinel)
        {
            InsertBefore = InsertBefore->Next;
            ++Walk;
        }
    }
    FAttachedNode* Node = static_cast<FAttachedNode*>(calloc(1, sizeof(FAttachedNode)));
    if (Node == nullptr)
        return GR_GENERIC;
    Node->PlayKey = Key;
    Node->Next = InsertBefore;
    Node->Prev = InsertBefore->Prev;
    InsertBefore->Prev->Next = Node;
    InsertBefore->Prev = Node;
    ++AttachedCount;
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::DetachPlayKey(ISoundSource* PlayKey)
{
    // LAB_06ee34d0: the matching instance first, then the attached list.
    FSoundInstanceNode* Inst = FindInstanceByKey(PlayKey);
    if (Inst != nullptr)
    {
        EnterCriticalSection(&G_InstanceLock);
        UnlinkInstance(Inst);
        SetEvent(G_MixerEvent);
        LeaveCriticalSection(&G_InstanceLock);
    }
    // EVERY attached node whose key matches is removed; the walk continues past each removal.
    FAttachedNode* Attached = AttachedSentinel->Next;
    while (Attached != AttachedSentinel)
    {
        if (Attached->PlayKey == PlayKey)
        {
            Attached->PlayKey->Release();
            FAttachedNode* Next = Attached->Next;
            Attached->Prev->Next = Attached->Next;
            Attached->Next->Prev = Attached->Prev;
            free(Attached);
            --AttachedCount;
            Attached = Next;
        }
        else
        {
            Attached = Attached->Next;
        }
    }
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::ReplaceAttachedList(void** Keys, unsigned long Count)
{
    // FUN_06ee33b0. Count == 0 destroys every instance under the lock. A null key returns -1 at
    // once, leaving the keys already appended attached. No test on the key array pointer.
    ClearAttached();
    if (Count == 0)
    {
        EnterCriticalSection(&G_InstanceLock);
        FSoundInstanceNode* Node = G_InstanceSentinel.Next;
        while (Node != &G_InstanceSentinel)
        {
            FSoundInstanceNode* Next = Node->Next;
            UnlinkInstance(Node);
            Node = Next;
        }
        SetEvent(G_MixerEvent);
        LeaveCriticalSection(&G_InstanceLock);
        return GR_OK;
    }
    for (unsigned long Index = 0; Index < Count; ++Index)
    {
        if (Keys[Index] == nullptr)
            return GR_GENERIC;
        ISoundSource* Key = static_cast<ISoundSource*>(Keys[Index]);
        FAttachedNode* Node = static_cast<FAttachedNode*>(calloc(1, sizeof(FAttachedNode)));
        Node->PlayKey = Key;
        Node->Next = AttachedSentinel;
        Node->Prev = AttachedSentinel->Prev;
        AttachedSentinel->Prev->Next = Node;
        AttachedSentinel->Prev = Node;
        ++AttachedCount;
        Key->AddRef();
    }
    return GR_OK;
}

unsigned long DACOM_API FSoundManager::GetAttachedCount()
{
    return AttachedCount;
}

unsigned long DACOM_API FSoundManager::GetPlayingCount()
{
    return G_InstanceCount;
}

GENRESULT DACOM_API FSoundManager::BindParser()
{
    // LAB_06ee56f0 (nested this is object+8).
    if (Parser != nullptr)
    {
        Parser->Release();
        Parser = nullptr;
    }
    // DACOM_Acquire() unchecked; the QI result is ignored.
    ICOManager* Dacom = DACOM_Acquire();
    Dacom->QueryInterface(Iids::ProfileParser, reinterpret_cast<void**>(&Parser));
    bParserBound = 1;
    return GR_OK;
}

void FSoundManager::DumpDeviceNotices()
{
    // FUN_06ee5b20. "" is DAT_06eeaf78, " NOT" is DAT_06eeaae4.
    char Line[0x2000];
    const char* Enabled = ((CapsFlags & kSoundFlagHw2D) != 0) ? "ENABLED" : "DISABLED";
    const char* Requested = ((Flags & kSoundFlagHw2D) != 0) ? "" : " NOT";
    Fdump(kFdumpNotice, 0xBA0,
          FormatMessage2000(Line, "SoundMgr:2D HW%s requested and %s with %d HW buffers available.\n",
                            Requested, Enabled, FreeHwMixingBuffers));

    Enabled = ((CapsFlags & kSoundFlagHw3D) != 0) ? "ENABLED" : "DISABLED";
    Requested = ((Flags & kSoundFlagHw3D) != 0) ? "" : " NOT";
    Fdump(kFdumpNotice, 0xBA5,
          FormatMessage2000(Line,
                            "SoundMgr:3D HW%s requested and %s with %d 3D HW buffers available.\n",
                            Requested, Enabled, FreeHw3dBuffers));

    if ((CapsFlags & (kSoundFlagHw2D | kSoundFlagHw3D)) != 0)
    {
        Requested = (G_AllowHwToSw != 0) ? "" : " NOT";
        Fdump(kFdumpNotice, 0xBAA,
              FormatMessage2000(Line, "SoundMgr:Transfer of HW buffers to SW while playing%s allowed.\n",
                                Requested));
        Requested = (G_AllowSwToHw != 0) ? "" : " NOT";
        Fdump(kFdumpNotice, 0xBAC,
              FormatMessage2000(Line, "SoundMgr:Transfer of SW buffers to HW while playing%s allowed.\n",
                                Requested));
    }

    // Literals 06eea9a0 / 964 / 928 / 8f0 / 8c0 / 894 / 86c / 830 / 804 / 7e0.
    if ((CapsFlags & kSoundFlagAlgFull) != 0)
        Fdump(kFdumpNotice, 0xBB1,
              FormatMessage2000(Line, "SoundMgr:Using Full Algorithm for DSound3D SW buffers.\n"));
    else if ((CapsFlags & kSoundFlagAlgLight) != 0)
        Fdump(kFdumpNotice, 0xBB5,
              FormatMessage2000(Line, "SoundMgr:Using Light Algorithm for DSound3D SW buffers.\n"));
    else if ((CapsFlags & kSoundFlagAlgBasic) != 0)
        Fdump(kFdumpNotice, 0xBB9,
              FormatMessage2000(Line, "SoundMgr:Using Basic Algorithm for DSound3D SW buffers.\n"));
    else if ((CapsFlags & kSoundFlagAlgUser) != 0)
        Fdump(kFdumpNotice, 0xBBD,
              FormatMessage2000(Line, "SoundMgr:Using User Default for DSound3D SW buffers.\n"));
    else if ((CapsFlags & kSoundFlagAlgNone) != 0)
        Fdump(kFdumpNotice, 0xBC1,
              FormatMessage2000(Line, "SoundMgr:Disabling DSound3D for SW buffers.\n"));

    if ((CapsFlags & kSoundFlagRightHanded) != 0)
        Fdump(kFdumpNotice, 0xBC6, FormatMessage2000(Line, "SoundMgr:Using Right Handed Coordinates.\n"));
    if ((CapsFlags & kSoundFlagAll2DInSoftware) != 0)
        Fdump(kFdumpNotice, 0xBCA, FormatMessage2000(Line, "SoundMgr:Forcing all 2D buffers to SW.\n"));
    if (G_ForceFreqToSw != 0)
        Fdump(kFdumpNotice, 0xBCF,
              FormatMessage2000(Line, "SoundMgr:Forcing all frequency controlled buffers to SW.\n"));
    if (G_NeverSetStartPos != 0)
        Fdump(kFdumpNotice, 0xBD4, FormatMessage2000(Line, "SoundMgr:Offsetting start times disabled.\n"));
    if (G_DisplayExtraDebug != 0)
        Fdump(kFdumpNotice, 0xBD9, FormatMessage2000(Line, "SoundMgr:Extra debug info enabled.\n"));

    Enabled = ((CapsFlags & kSoundFlagEax) != 0) ? "ENABLED" : "DISABLED";
    Fdump(kFdumpNotice, 0xBDE, FormatMessage2000(Line, "SoundMgr:EAX %s.\n", Enabled));
    Fdump(kFdumpNotice, 0xBE0, FormatMessage2000(Line, "SoundMgr:Max channels set to %d.\n", MaxChannels));
}

GENRESULT FSoundManager::CreateDirectSound()
{
    // FUN_06ee5730. An already-created device returns -1, not success.
    if (DirectSound != nullptr)
        return GR_GENERIC;

    const HRESULT InitHr = CoInitialize(nullptr);
    if (InitHr < 0)
        return GR_GENERIC;
    bCoInitialized = 1;

    if (DirectSound == nullptr)
    {
        CoCreateInstance(kClsidDirectSound, nullptr, CLSCTX_INPROC_SERVER, kIidDirectSoundRetail,
                         reinterpret_cast<void**>(&DirectSound));
    }
    if (DirectSound == nullptr)
        return GR_GENERIC;

    HRESULT Hr = DirectSound->Initialize(&DeviceGuid);
    if (Hr < 0)
    {
        char Warn[0x2000];
        Fdump(kFdumpWarning, 0xB31,
              FormatMessage2000(Warn, "SoundManager:Couldn't create DSound device %s.\n", // 06eea778
                                DeviceIdString));
        Hr = DirectSound->Initialize(nullptr);
        if (Hr < 0)
        {
            Fdump(kFdumpWarning, 0xB34, "SoundManager:Couldn't initialize default DSound device.\n"); // 06eea73c
            DirectSound->Release();
            DirectSound = nullptr;
            return GR_GENERIC;
        }
    }

    DirectSound->SetCooperativeLevel(Window, DSSCL_PRIORITY);

    DSCAPS Caps = {};
    Caps.dwSize = 0x60;
    DirectSound->GetCaps(&Caps);
    FreeHw3dBuffers = Caps.dwFreeHw3DAllBuffers;
    FreeHwMixingBuffers = Caps.dwFreeHwMixingAllBuffers;
    if (Caps.dwFreeHwMixingAllBuffers + Caps.dwFreeHw3DAllBuffers != 0 && MaxChannels == 0)
    {
        unsigned long Mix = Caps.dwFreeHw3DAllBuffers;
        if (Mix < Caps.dwFreeHwMixingAllBuffers)
            Mix = Caps.dwFreeHwMixingAllBuffers;
        MaxChannels = Mix;
        if (MaxChannels < 8)
            MaxChannels = 8;
    }

    DSBUFFERDESC PrimaryDesc = {};
    PrimaryDesc.dwSize = 0x24;
    PrimaryDesc.dwFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRL3D;
    DirectSound->CreateSoundBuffer(&PrimaryDesc, &Primary, nullptr);
    if (Primary == nullptr)
    {
        Fdump(kFdumpNotice, 0xB8E, "SoundManager: Couldn't create primary sound buffer.\n"); // 06eea6cc
        DirectSound->Release();
        DirectSound = nullptr;
        return GR_GENERIC;
    }

    CapsFlags &= ~(kSoundFlagHw2D | kSoundFlagHw3D);
    if ((Caps.dwFlags & DSCAPS_EMULDRIVER) != 0 || Caps.dwUnlockTransferRateHwBuffers != 0)
    {
        Flags &= ~(kSoundFlagHw2D | kSoundFlagHw3D);
        char Isa[0x2000];
        Fdump(kFdumpNotice, 0xB5D,
              FormatMessage2000(Isa, "ISA or slow sound card detected. Disabling HW buffers.\n")); // 06eea704
    }
    else
    {
        WAVEFORMATEX Fmt = {};
        Fmt.wFormatTag = 1;
        Fmt.nChannels = 2;
        Fmt.nSamplesPerSec = 0x5622;
        Fmt.nAvgBytesPerSec = 0x15888;
        Fmt.nBlockAlign = 4;
        Fmt.wBitsPerSample = 0x10;
        if ((Flags & kSoundFlagHw2D) != 0)
        {
            DSBUFFERDESC Probe = {};
            Probe.dwSize = 0x24;
            Probe.dwFlags = DSBCAPS_LOCHARDWARE;
            Probe.dwBufferBytes = 4;
            Probe.lpwfxFormat = &Fmt;
            IDirectSoundBuffer* ProbeBuf = nullptr;
            if (DirectSound->CreateSoundBuffer(&Probe, &ProbeBuf, nullptr) >= 0)
                CapsFlags |= kSoundFlagHw2D;
            if (ProbeBuf != nullptr)
                ProbeBuf->Release();
        }
        if ((Flags & kSoundFlagHw3D) != 0)
        {
            DSBUFFERDESC Probe = {};
            Probe.dwSize = 0x24;
            Probe.dwFlags = DSBCAPS_LOCHARDWARE | DSBCAPS_CTRL3D;
            Probe.dwBufferBytes = 4;
            Probe.lpwfxFormat = &Fmt;
            IDirectSoundBuffer* ProbeBuf = nullptr;
            if (DirectSound->CreateSoundBuffer(&Probe, &ProbeBuf, nullptr) >= 0)
                CapsFlags |= kSoundFlagHw3D;
            if (ProbeBuf != nullptr)
                ProbeBuf->Release();
        }
    }

    if ((PrimaryDesc.dwFlags & DSBCAPS_CTRL3D) != 0)
        Primary->QueryInterface(kIidDirectSound3DListenerRetail, reinterpret_cast<void**>(&Listener));

    unsigned long BlockAlign = (Channels * BitsPerSample) >> 3;
    unsigned long AvgBytes = SampleRate * BlockAlign;
    WAVEFORMATEX PrimaryFmt = {};
    PrimaryFmt.wFormatTag = 1;
    PrimaryFmt.nChannels = static_cast<WORD>(Channels);
    PrimaryFmt.nSamplesPerSec = SampleRate;
    PrimaryFmt.nAvgBytesPerSec = AvgBytes;
    PrimaryFmt.nBlockAlign = static_cast<WORD>(BlockAlign);
    PrimaryFmt.wBitsPerSample = static_cast<WORD>(BitsPerSample);
    Primary->SetFormat(&PrimaryFmt);

    SetEaxPreview(EaxEnv, EaxVol, EaxDecay, EaxDamp);
    Primary->Play(0, 0, DSBPLAY_LOOPING);
    DumpDeviceNotices();
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::StartupDevice(HWND Hwnd, unsigned long InFlags)
{
    // LAB_06ee3670. The parser bind runs before the flags == 0 reject.
    if (bParserBound == 0)
        BindParser();

    Window = Hwnd;
    if (InFlags == 0)
    {
        Fdump(kFdumpError, 0x6D5, "SoundManager: startup options must be non-zero.\n"); // 06eea47c
        return GR_GENERIC;
    }

    Flags = InFlags;
    if ((InFlags & kSoundFlagNoHw) != 0)
        Flags &= ~(kSoundFlagHw2D | kSoundFlagHw3D);

    // One integer and one float stack variable carry every value. Each block resets them to its default only once a parser was obtained, so a block
    // whose acquisition fails inherits the previous block's value, exactly as retail does.
    unsigned long Value = 0;
    float FloatValue = 0.0f;
    ICOManager* Dacom = DACOM_Acquire();
    IDAComponent* Local = nullptr;

    // useEAX (default = current bit 2). The parser is released inline here.
    if (AcquireParserLocal(Dacom, Parser, &Local))
    {
        IProfileParser* Ini = static_cast<IProfileParser*>(Local);
        Value = Flags & kSoundFlagEax;
        void* Audio = Ini->OpenSection("AudioProperties", 3);
        if (Audio != nullptr)
        {
            if (Ini->GetString(Audio, "useEAX", G_IniIntBuffer, 0x400) != 0)
                Value = ParseIniUnsigned(G_IniIntBuffer);
            Ini->CloseSection(Audio);
        }
    }
    if (Local != nullptr)
        Local->Release();
    if (Value != 0)
        Flags |= kSoundFlagEax;
    else
        Flags &= ~kSoundFlagEax;

    // reverbBaseEnvironment (default 0) -> +0x58.
    if (AcquireParserLocal(Dacom, Parser, &Local))
    {
        IProfileParser* Ini = static_cast<IProfileParser*>(Local);
        Value = 0;
        void* Audio = Ini->OpenSection("AudioProperties", 3);
        if (Audio != nullptr)
        {
            if (Ini->GetString(Audio, "reverbBaseEnvironment", G_IniIntBuffer, 0x400) != 0)
                Value = ParseIniUnsigned(G_IniIntBuffer);
            Ini->CloseSection(Audio);
        }
    }
    ReleaseComPtr(&Local);
    EaxEnv = static_cast<int>(Value);

    // reverbMasterVolume (default 0.0f) -> +0x5C.
    if (AcquireParserLocal(Dacom, Parser, &Local))
    {
        IProfileParser* Ini = static_cast<IProfileParser*>(Local);
        FloatValue = 0.0f;
        void* Audio = Ini->OpenSection("AudioProperties", 3);
        if (Audio != nullptr)
        {
            if (Ini->GetString(Audio, "reverbMasterVolume", G_IniFloatBuffer, 0x400) != 0)
                FloatValue = ParseIniFloat(G_IniFloatBuffer);
            Ini->CloseSection(Audio);
        }
    }
    ReleaseComPtr(&Local);
    EaxVol = FloatValue;

    // reverbDecayTime (default 0.1f) -> +0x60.
    if (AcquireParserLocal(Dacom, Parser, &Local))
    {
        IProfileParser* Ini = static_cast<IProfileParser*>(Local);
        FloatValue = 0.1f;
        void* Audio = Ini->OpenSection("AudioProperties", 3);
        if (Audio != nullptr)
        {
            if (Ini->GetString(Audio, "reverbDecayTime", G_IniFloatBuffer, 0x400) != 0)
                FloatValue = ParseIniFloat(G_IniFloatBuffer);
            Ini->CloseSection(Audio);
        }
    }
    ReleaseComPtr(&Local);
    EaxDecay = FloatValue;

    // reverbDamping (default 1.0f) -> +0x64.
    if (AcquireParserLocal(Dacom, Parser, &Local))
    {
        IProfileParser* Ini = static_cast<IProfileParser*>(Local);
        FloatValue = 1.0f;
        void* Audio = Ini->OpenSection("AudioProperties", 3);
        if (Audio != nullptr)
        {
            if (Ini->GetString(Audio, "reverbDamping", G_IniFloatBuffer, 0x400) != 0)
                FloatValue = ParseIniFloat(G_IniFloatBuffer);
            Ini->CloseSection(Audio);
        }
    }
    ReleaseComPtr(&Local);
    EaxDamp = FloatValue;

    // reverbMix (default 0.0f) -> +0x78, stored as ftol(value) in a float field.
    if (AcquireParserLocal(Dacom, Parser, &Local))
    {
        IProfileParser* Ini = static_cast<IProfileParser*>(Local);
        FloatValue = 0.0f;
        void* Audio = Ini->OpenSection("AudioProperties", 3);
        if (Audio != nullptr)
        {
            if (Ini->GetString(Audio, "reverbMix", G_IniFloatBuffer, 0x400) != 0)
                FloatValue = ParseIniFloat(G_IniFloatBuffer);
            Ini->CloseSection(Audio);
        }
    }
    ReleaseComPtr(&Local);
    *reinterpret_cast<int*>(&ReverbMix) = static_cast<int>(FloatValue);

    // deviceID: the default (DAT_06eea024) is copied into +0x0D only once a parser was obtained;
    // without one +0x0D keeps the ctor's zeros. Neither strncpy forces a terminator.
    if (AcquireParserLocal(Dacom, Parser, &Local))
    {
        IProfileParser* Ini = static_cast<IProfileParser*>(Local);
        strncpy(DeviceIdString, kDefaultDeviceId, 0x28);
        void* Section = Ini->OpenSection("SoundManager", 3);
        if (Section != nullptr)
        {
            if (Ini->GetString(Section, "deviceID", G_IniStringBuffer, 0x400) != 0)
                strncpy(DeviceIdString, G_IniStringBuffer, 0x28);
            Ini->CloseSection(Section);
        }
    }
    ReleaseComPtr(&Local);
    ParseGuidString(DeviceIdString, &DeviceGuid);

    const unsigned long AlgBits = Flags & kSoundFlagAlgMask;
    const char* AlgName = "USER";
    if (AlgBits == kSoundFlagAlgFull)
        AlgName = "FULL";
    else if (AlgBits == kSoundFlagAlgBasic)
        AlgName = "BASIC";
    else if (AlgBits == kSoundFlagAlgLight)
        AlgName = "LIGHT";
    else if (AlgBits == kSoundFlagAlgNone)
        AlgName = "NONE";
    // Retail copies the default name into AlgBuf only once a parser was obtained, so without one
    // it compares an uninitialised buffer. Deviation: the port fills it up front so that case
    // stays defined.
    char AlgBuf[0x20];
    strncpy(AlgBuf, AlgName, 0x20);
    if (AcquireParserLocal(Dacom, Parser, &Local))
    {
        IProfileParser* Ini = static_cast<IProfileParser*>(Local);
        void* Section = Ini->OpenSection("SoundManager", 3);
        if (Section != nullptr)
        {
            if (Ini->GetString(Section, "3D_SW_Algorithm", G_IniStringBuffer, 0x400) != 0)
                strncpy(AlgBuf, G_IniStringBuffer, 0x20);
            Ini->CloseSection(Section);
        }
    }
    ReleaseComPtr(&Local);
    Flags &= ~kSoundFlagAlgMask;
    if (_stricmp(AlgBuf, "FULL") == 0)
    {
        Flags |= kSoundFlagAlgFull;
        CapsFlags |= kSoundFlagAlgFull;
        CopyAlgGuid(&Ds3dAlgGuid0, &kDs3dAlgHrtfFull);
    }
    else if (_stricmp(AlgBuf, "LIGHT") == 0)
    {
        Flags |= kSoundFlagAlgLight;
        CapsFlags |= kSoundFlagAlgLight;
        CopyAlgGuid(&Ds3dAlgGuid0, &kDs3dAlgHrtfLight);
    }
    else if (_stricmp(AlgBuf, "BASIC") == 0)
    {
        Flags |= kSoundFlagAlgBasic;
        CapsFlags |= kSoundFlagAlgBasic;
        CopyAlgGuid(&Ds3dAlgGuid0, &kDs3dAlgNoVirtual);
    }
    else if (_stricmp(AlgBuf, "USER") == 0)
    {
        Flags |= kSoundFlagAlgUser;
        CapsFlags |= kSoundFlagAlgUser;
        CopyAlgGuid(&Ds3dAlgGuid0, &kDs3dAlgGuidNull);
    }
    else if (_stricmp(AlgBuf, "NONE") == 0)
    {
        G_Ds3dCreateFlags = (G_Ds3dCreateFlags & ~DSBCAPS_CTRL3D) | DSBCAPS_CTRLPAN;
        CopyAlgGuid(&Ds3dAlgGuid0, &kDs3dAlgGuidNull);
    }

    // useNoHW (default = current bit 0x800): a non-zero value clears bits 0/1.
    if (AcquireParserLocal(Dacom, Parser, &Local))
    {
        IProfileParser* Ini = static_cast<IProfileParser*>(Local);
        Value = Flags & kSoundFlagNoHw;
        void* Section = Ini->OpenSection("SoundManager", 3);
        if (Section != nullptr)
        {
            if (Ini->GetString(Section, "useNoHW", G_IniIntBuffer, 0x400) != 0)
                Value = ParseIniUnsigned(G_IniIntBuffer);
            Ini->CloseSection(Section);
        }
    }
    ReleaseComPtr(&Local);
    if (Value != 0)
        Flags &= ~(kSoundFlagHw2D | kSoundFlagHw3D);

    // From here on FUN_06ee72f0 does the reads; every call writes the shared Value and the caller
    // reads Value back (not the return), so a failed parser acquisition keeps the previous value.
    IProfileParser* Cached = Parser;
    ReadProfileInt(Dacom, Cached, "SoundManager", "rightHandedCoordinates", Flags & kSoundFlagRightHanded, &Value);
    if (Value != 0)
    {
        Flags |= kSoundFlagRightHanded;
        CapsFlags |= kSoundFlagRightHanded;
        CoordScale = -1; // +0xA4
    }
    else
        Flags &= ~kSoundFlagRightHanded;

    ReadProfileInt(Dacom, Cached, "SoundManager", "createAll2DInSoftware", Flags & kSoundFlagAll2DInSoftware, &Value);
    if (Value != 0)
    {
        Flags |= kSoundFlagAll2DInSoftware;
        CapsFlags |= kSoundFlagAll2DInSoftware;
    }
    else
    {
        Flags &= ~kSoundFlagAll2DInSoftware;
        CapsFlags &= ~kSoundFlagAll2DInSoftware;
    }

    // Presence probe of speakerConfiguration into DAT_06eebc00; only when the key exists is it
    // read through FUN_06ee72f0 (default 2) and applied after the device is up.
    unsigned char bSpeakerKey = 0;
    unsigned long SpeakerValue = 0;
    int SpeakerFound = -1;
    if (AcquireParserLocal(Dacom, Parser, &Local))
    {
        IProfileParser* Ini = static_cast<IProfileParser*>(Local);
        void* Section = Ini->OpenSection("SoundManager", 3);
        if (Section != nullptr)
        {
            if (Ini->GetString(Section, "speakerConfiguration", G_IniSpeakerBuffer, 0x400) != 0)
                SpeakerFound = 0;
            Ini->CloseSection(Section);
        }
    }
    ReleaseComPtr(&Local);
    if (SpeakerFound >= 0)
    {
        ReadProfileInt(Dacom, Cached, "SoundManager", "speakerConfiguration", 2, &Value);
        bSpeakerKey = 1;
        SpeakerValue = Value;
    }

    ReadProfileInt(Dacom, Cached, "SoundManager", "maxSoundChannels", MaxChannels, &Value);
    MaxChannels = Value; // +0x54

    ReadProfileInt(Dacom, Cached, "SoundManager", "use2DHW", Flags & kSoundFlagHw2D, &Value);
    if (Value != 0)
        Flags |= kSoundFlagHw2D;
    else
        Flags &= ~kSoundFlagHw2D;

    ReadProfileInt(Dacom, Cached, "SoundManager", "use3DHW", Flags & kSoundFlagHw3D, &Value);
    if (Value != 0)
        Flags |= kSoundFlagHw3D;
    else
        Flags &= ~kSoundFlagHw3D;

    ReadProfileInt(Dacom, Cached, "SoundManager", "ALLOW_HW_TO_SW", G_AllowHwToSw, &Value);
    if (Value != 0 && (Flags & (kSoundFlagHw2D | kSoundFlagHw3D)) != 0)
        G_AllowHwToSw = 1;
    else
        G_AllowHwToSw = 0;

    ReadProfileInt(Dacom, Cached, "SoundManager", "ALLOW_SW_TO_HW", G_AllowSwToHw, &Value);
    if (Value != 0 && (Flags & (kSoundFlagHw2D | kSoundFlagHw3D)) != 0)
        G_AllowSwToHw = 1;
    else
        G_AllowSwToHw = 0;

    ReadProfileInt(Dacom, Cached, "SoundManager", "FORCE_FREQ_CONTROL_TO_SW", G_ForceFreqToSw, &Value);
    G_ForceFreqToSw = Value;
    ReadProfileInt(Dacom, Cached, "SoundManager", "NEVER_SET_START_POS", G_NeverSetStartPos, &Value);
    G_NeverSetStartPos = Value;
    ReadProfileInt(Dacom, Cached, "SoundManager", "DISPLAY_EXTRA_DEBUG_INFO", G_DisplayExtraDebug,
                   &Value);
    G_DisplayExtraDebug = Value;

    // The 32-bit product is converted as unsigned.
    ReadProfileInt(Dacom, Cached, "SoundManager", "max3DPan", 0x64, &Value);
    Max3dPan = static_cast<float>(static_cast<unsigned long long>(Value * 100));

    if (CreateDirectSound() < 0)
        return GR_GENERIC;
    if (bSpeakerKey != 0)
        SetSpeakerConfig(SpeakerValue); // through the vtable (+0x1C)
    ApplyListener();
    return GR_OK;
}

unsigned char FSoundManager::SetupInstanceNotify(FSoundInstanceNode* Node)
{
    // FUN_06ee1a40 (retail is handed the payload, node+8).
    if (G_NotifyThread == nullptr)
    {
        unsigned long Tid = 0;
        G_NotifyThread = CreateThread(nullptr, 0, NotifyThreadProc, nullptr, 0, &Tid);
        if (G_NotifyThread == nullptr)
        {
            Fdump(kFdumpError, 0x347, "SoundManager: couldn't create thread for looping sounds.\n"); // 06eea1b0
            return 0;
        }
    }
    if (Node->NotifyEvent != nullptr)
    {
        CloseHandle(Node->NotifyEvent);
        Node->NotifyEvent = nullptr;
    }
    Node->NotifyEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (Node->NotifyEvent == nullptr)
        return 0;
    IDirectSoundNotify* Notify = nullptr;
    if (Node->Buffer->QueryInterface(kIidDirectSoundNotify, reinterpret_cast<void**>(&Notify)) < 0)
    {
        CloseHandle(Node->NotifyEvent);
        Node->NotifyEvent = nullptr;
        return 0;
    }
    DSBPOSITIONNOTIFY Pos;
    Pos.dwOffset = Node->LoopEndBytes;
    Pos.hEventNotify = Node->NotifyEvent;
    const HRESULT Hr = Notify->SetNotificationPositions(1, &Pos);
    Notify->Release();
    if (Hr < 0)
        return 0;
    Node->StatusFlags |= kInstanceNotify;
    return 1;
}

GENRESULT DACOM_API FSoundManager::ShutdownDevice()
{
    // FUN_06ee4900. Does not Release the parser cache at +0x134. The notify thread is only
    // flagged and its handle closed: no SetEvent, no wait.
    if (G_NotifyThread != nullptr)
    {
        G_MixerStopFlag = 1;
        CloseHandle(G_NotifyThread);
        G_NotifyThread = nullptr;
    }
    // Drain the global list without taking the instance lock.
    if (G_InstanceCount != 0)
    {
        FSoundInstanceNode* Node = G_InstanceSentinel.Next;
        while (Node != &G_InstanceSentinel)
        {
            FSoundInstanceNode* Next = Node->Next;
            Node->Prev->Next = Node->Next;
            Node->Next->Prev = Node->Prev;
            ReleaseInstancePayload(Node);
            free(Node);
            --G_InstanceCount;
            Node = Next;
        }
    }
    // Drain the attached list; the play keys are NOT released (AttachPlayKey's AddRef is only
    // undone by DetachPlayKey).
    if (AttachedCount != 0)
    {
        FAttachedNode* Attached = AttachedSentinel->Next;
        while (Attached != AttachedSentinel)
        {
            FAttachedNode* Next = Attached->Next;
            Attached->Prev->Next = Attached->Next;
            Attached->Next->Prev = Attached->Prev;
            free(Attached);
            --AttachedCount;
            Attached = Next;
        }
    }
    DestroyAllArchetypes();
    if ((CapsFlags & kSoundFlagEax) != 0)
        // Retail quirk (06EE4A0E): vol and damp are swapped when the saved EAX state is restored.
        SetEaxPreview(static_cast<int>(SavedEaxEnv), *reinterpret_cast<float*>(&SavedEaxDamp),
                      *reinterpret_cast<float*>(&SavedEaxDecay), *reinterpret_cast<float*>(&SavedEaxVol));
    if (Primary != nullptr)
    {
        Primary->Release();
        Primary = nullptr;
    }
    if (Listener != nullptr)
    {
        Listener->Release();
        Listener = nullptr;
    }
    if (DirectSound != nullptr)
    {
        DirectSound->Release();
        DirectSound = nullptr;
    }
    if (bCoInitialized != 0)
        CoUninitialize();
    bCoInitialized = 0;
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::GetDirectSound(IDirectSound** Out)
{
    // LAB_06ee4a90. No check on Out.
    if (DirectSound == nullptr)
    {
        *Out = nullptr;
        return GR_GENERIC;
    }
    *Out = DirectSound;
    DirectSound->AddRef();
    return GR_OK;
}

unsigned long DACOM_API FSoundManager::GetTickCountSlot()
{
    return GetTickCount();
}

GENRESULT FSoundManager::GetDeviceSnapshot(void* Out)
{
    // LAB_06ee4ad0. No memset. The caps copies are dwFree*, not dwMax*.
    unsigned char* Bytes = static_cast<unsigned char*>(Out);
    memcpy(Bytes + 4, &CapsFlags, 4);
    memcpy(Bytes + 8, &MaxChannels, 4);
    memcpy(Bytes + 12, &FreeHw3dBuffers, 4);
    if (DirectSound != nullptr)
    {
        // Only dwSize is set before GetCaps, whose result is ignored.
        DSCAPS Caps;
        Caps.dwSize = 0x60;
        DirectSound->GetCaps(&Caps);
        memcpy(Bytes + 0x10, &Caps.dwFreeHw3DAllBuffers, 4);
        memcpy(Bytes + 0x14, &Caps.dwFreeHwMixingAllBuffers, 4);
        memcpy(Bytes + 0x18, &DeviceGuid, sizeof(GUID));
    }
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::MixerTick(ISoundListenerPose* ListenerPose)
{
    // LAB_06ee4b50: listener pose, prune detached instances, per attached key (active /
    // expired / start tick, pose and range cull, create into the pending list), splice, apply,
    // commit, play.
    unsigned char bDirty = 0;
    CurrentTickMs = GetTickCount();
    if (ListenerPose != nullptr)
        ApplyListenerFromSource(ListenerPose);

    FSoundInstanceNode* Node = G_InstanceSentinel.Next;
    while (Node != &G_InstanceSentinel)
    {
        FSoundInstanceNode* Next = Node->Next;
        if (FindAttached(Node->PlayKey) == nullptr)
        {
            EnterCriticalSection(&G_InstanceLock);
            UnlinkInstance(Node);
            bDirty = 1;
            LeaveCriticalSection(&G_InstanceLock);
        }
        Node = Next;
    }

    unsigned long Created = 0;
    unsigned long AttachedIndex = 0;
    {
        // The attached head (+0x110) always exists; retail does not test it.
        FAttachedNode* Attached = AttachedSentinel->Next;
        while (Attached != AttachedSentinel)
        {
            ISoundSource* PlayKey = Attached->PlayKey;
            unsigned char bSkip = 0;
            if (Created >= MaxChannels)
                bSkip = 1;
            else if (PlayKey->IsActive() == 0)
                bSkip = 1;
            else if (OneShotAlreadyExpired(PlayKey) != 0)
                bSkip = 1;
            else if (CurrentTickMs < PlayKey->GetStartTick())
                bSkip = 1;

            if (bSkip != 0)
            {
                FSoundInstanceNode* Existing = FindInstanceByKey(PlayKey);
                if (Existing != nullptr)
                {
                    // The notice is emitted inside the lock.
                    EnterCriticalSection(&G_InstanceLock);
                    UnlinkInstance(Existing);
                    bDirty = 1;
                    if (Created >= MaxChannels)
                        NoticeArch(0x980,
                                   "Had to stop instance of arch %d because max channels was exceeded.\n",
                                   PlayKey);
                    else
                        NoticeArch(0x97C,
                                   "Had to stop instance of arch %d because it is no longer playing.\n",
                                   PlayKey);
                    LeaveCriticalSection(&G_InstanceLock);
                }
                else if (Created >= MaxChannels)
                    NoticeArch(0x98C,
                               "Ignoring instance of arch %d because max channels was exceeded.\n",
                               PlayKey);
                else
                    NoticeArch(0x988,
                               "Ignoring instance of arch %d because it is no longer playing.\n",
                               PlayKey);
            }
            else
            {
                FSoundInstanceNode* Existing = FindInstanceByKey(PlayKey);
                if (Existing != nullptr)
                {
                    // Equal, greater and NaN all cull.
                    RefreshInstancePose(Existing);
                    if (PlayKey->Is3d() != 0 && (Existing->DsCapsFlags & DSBCAPS_MUTE3DATMAXDISTANCE) != 0 &&
                        !(Existing->DistSq < Existing->MaxDistSq))
                    {
                        EnterCriticalSection(&G_InstanceLock);
                        UnlinkInstance(Existing);
                        bDirty = 1;
                        LeaveCriticalSection(&G_InstanceLock);
                    }
                    else
                    {
                        ++Created;
                        Existing->CapsFlags = AttachedIndex;
                        if (MaybeMigrateHwSw(this, Existing, AttachedIndex) != 0)
                        {
                            Existing->Buffer->Stop();
                            Existing->StatusFlags &= ~kInstanceStarted;
                        }
                    }
                }
                else if (MixerAllowCreate(PlayKey) != 0)
                {
                    // The FUN_06ee5f00 result is unused here.
                    FSoundInstanceNode* CreatedNode = CreateInstanceFromKey(PlayKey);
                    if (CreatedNode != nullptr)
                    {
                        ++Created;
                        CreatedNode->CapsFlags = AttachedIndex;
                        MaybeMigrateHwSw(this, CreatedNode, AttachedIndex);
                        LinkPendingInstance(CreatedNode);
                    }
                }
            }
            ++AttachedIndex;
            Attached = Attached->Next;
        }
    }

    // Dirty is set before the re-check under the lock.
    if (InstanceEmbeddedCount != 0)
    {
        EnterCriticalSection(&G_InstanceLock);
        bDirty = 1;
        if (InstanceEmbeddedCount != 0)
            SplicePendingInstances();
        LeaveCriticalSection(&G_InstanceLock);
    }

    Node = G_InstanceSentinel.Next;
    while (Node != &G_InstanceSentinel)
    {
        MixerApplyInstance(Node);
        Node = Node->Next;
    }

    if (Listener != nullptr)
        Listener->CommitDeferredSettings();

    // Play pass. IsLooping is asked before FUN_06ee6b70; no null checks on the key or the
    // buffer.
    Node = G_InstanceSentinel.Next;
    while (Node != &G_InstanceSentinel)
    {
        FSoundInstanceNode* Next = Node->Next;
        const unsigned char bLooping = Node->PlayKey->IsLooping();
        const DWORD PlayFlags = bLooping != 0 ? DSBPLAY_LOOPING : 0;
        if (MixerSetStartOffset(Node) == 0)
        {
            Node = Next;
            continue;
        }
        if (PlayFlags != 0 && (Node->DsCapsFlags & DSBCAPS_CTRLPOSITIONNOTIFY) != 0)
            SetupInstanceNotify(Node);
        unsigned long LocFlags = 0;
        if ((Node->DsCapsFlags & DSBCAPS_LOCDEFER) != 0)
        {
            if ((Node->StatusFlags & kInstanceWantHardware) != 0)
                LocFlags = DSBPLAY_LOCHARDWARE;
            else
                LocFlags = DSBPLAY_LOCSOFTWARE;
        }
        HRESULT Hr = Node->Buffer->Play(0, 0, PlayFlags | LocFlags);
        if (Hr < 0)
        {
            char Line[0x2000];
            if (Hr == static_cast<HRESULT>(0x88780096))
                Fdump(kFdumpError, 0xA17, "Sound buffer was lost!");
            if (LocFlags == DSBPLAY_LOCHARDWARE)
            {
                if (G_DisplayExtraDebug != 0)
                    Fdump(kFdumpNotice, 0xA2C,
                          FormatMessage2000(
                              Line, "Trying to restart instance of arch %d in SW because HW not available.\n",
                              Node->PlayKey->GetArchId()));
                // A successful retry is therefore not marked HW below.
                LocFlags = DSBPLAY_LOCSOFTWARE;
                Hr = Node->Buffer->Play(0, 0, PlayFlags | LocFlags);
                if (Hr >= 0)
                {
                    Node->StatusFlags = (Node->StatusFlags & ~kInstanceHardware) | LocFlags;
                    if (G_DisplayExtraDebug != 0)
                        Fdump(kFdumpNotice, 0xA3D, "SUCCEEDED!\n");
                }
                else if (G_DisplayExtraDebug != 0)
                    Fdump(kFdumpNotice, 0xA43, "FAILED!\n");
            }
            else
            {
                if (G_DisplayExtraDebug != 0)
                    Fdump(kFdumpNotice, 0xA4D,
                          FormatMessage2000(
                              Line, "Trying to restart instance of arch %d in for some unknown reason.\n",
                              Node->PlayKey->GetArchId()));
                Hr = Node->Buffer->Play(0, 0, PlayFlags);
                if (Hr >= 0)
                {
                    Node->StatusFlags = (Node->StatusFlags & ~kInstanceHardware) | kInstanceSoftware;
                    if (G_DisplayExtraDebug != 0)
                        Fdump(kFdumpNotice, 0xA5D, "SUCCEEDED!\n");
                }
                else if (G_DisplayExtraDebug != 0)
                    Fdump(kFdumpNotice, 0xA62, "FAILED!\n");
            }
        }
        if (Hr >= 0)
        {
            Node->StatusFlags |= kInstanceStarted;
            if (LocFlags == DSBPLAY_LOCHARDWARE)
                Node->StatusFlags |= kInstanceHardware;
        }
        Node = Next;
    }

    if (bDirty != 0)
        SetEvent(G_MixerEvent);

    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::StopInstanceBuffer(ISoundSource* PlayKey)
{
    FSoundInstanceNode* Node = FindInstanceByKey(PlayKey);
    if (Node == nullptr || Node->Buffer == nullptr)
        return GR_OK;
    Node->Buffer->Stop();
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::PlayInstanceBuffer(ISoundSource* PlayKey)
{
    // LAB_06ee6f70.
    FSoundInstanceNode* Node = FindInstanceByKey(PlayKey);
    if (Node == nullptr || Node->Buffer == nullptr)
        return GR_OK;
    const unsigned char bLooping = PlayKey->IsLooping();
    const DWORD PlayFlags = bLooping != 0 ? DSBPLAY_LOOPING : 0;
    Node->Buffer->Play(0, 0, PlayFlags);
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::GetArchetypeFormat(void* Id, void* Out)
{
    // LAB_06ee5530: a hit returns Release's result.
    FSoundArchetype* Archetype = LookupArchetype(static_cast<int>(reinterpret_cast<intptr_t>(Id)));
    if (Archetype == nullptr)
        return GR_OK;
    Archetype->GetFormat(Out);
    return static_cast<GENRESULT>(Archetype->Release());
}

GENRESULT DACOM_API FSoundManager::GetArchetypeSamples(void* Id, void* Out)
{
    // LAB_06ee5570: a miss returns 0 without an FDUMP.
    FSoundArchetype* Archetype = LookupArchetype(static_cast<int>(reinterpret_cast<intptr_t>(Id)));
    if (Archetype == nullptr)
        return GR_OK;
    const GENRESULT Result = Archetype->GetSamples(Out);
    Archetype->Release();
    return Result;
}

unsigned long DACOM_API FSoundManager::GetArchetypeFrames(void* Id)
{
    FSoundArchetype* Archetype = LookupArchetype(static_cast<int>(reinterpret_cast<intptr_t>(Id)));
    if (Archetype == nullptr)
        return 0;
    const unsigned long Frames = Archetype->GetNumFrames();
    Archetype->Release();
    return Frames;
}

float DACOM_API FSoundManager::GetArchetypeGain(void* Id)
{
    // LAB_06ee55f0: arch+0x18 is read after Release in retail.
    FSoundArchetype* Archetype = LookupArchetype(static_cast<int>(reinterpret_cast<intptr_t>(Id)));
    if (Archetype == nullptr)
        return 0.0f;
    const float Value = Archetype->Gain;
    Archetype->Release();
    return Value;
}

unsigned long DACOM_API FSoundManager::GetArchetypeChannels(void* Id)
{
    // LAB_06ee5630.
    FSoundArchetype* Archetype = LookupArchetype(static_cast<int>(reinterpret_cast<intptr_t>(Id)));
    if (Archetype == nullptr)
        return 0;
    const unsigned long Count = Archetype->nChannels;
    Archetype->Release();
    return Count;
}

GENRESULT DACOM_API FSoundManager::SetArchetypeSamples(void* /*A*/, void* /*B*/, void* /*C*/)
{
    // LAB_06ee5670: not supported in retail; no lookup.
    Fdump(kFdumpError, 0xAF5, "SoundManager: set_samples() not supported.");
    return GR_OK;
}

unsigned long DACOM_API FSoundManager::GetArchetypeReady(void* Id)
{
    // LAB_06ee56a0.
    FSoundArchetype* Archetype = LookupArchetype(static_cast<int>(reinterpret_cast<intptr_t>(Id)));
    if (Archetype == nullptr)
        return 0;
    const unsigned long Ready = Archetype->IsReady();
    Archetype->Release();
    return Ready;
}

GENRESULT DACOM_API FSoundManager::GetInstanceStatus(int Index, void* Out)
{
    // LAB_06ee3620: Out receives node+8, +0x24, +0x28. No Out-null check.
    if (Index < 0 || static_cast<unsigned long>(Index) >= G_InstanceCount)
        return GR_GENERIC;
    FSoundInstanceNode* Node = G_InstanceSentinel.Next;
    int Remain = Index;
    while (Remain != 0)
    {
        --Remain;
        Node = Node->Next;
    }
    unsigned long* Dwords = static_cast<unsigned long*>(Out);
    Dwords[0] = reinterpret_cast<unsigned long>(Node->PlayKey);
    Dwords[1] = Node->StatusFlags;
    Dwords[2] = Node->DsCapsFlags;
    return GR_OK;
}

BOOL WINAPI DllMain(HINSTANCE Instance, DWORD Reason, LPVOID /*Reserved*/)
{
    if (Reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(Instance);
        // FUN_06ee71f0: DACOM_Acquire is called once for the null test and again for the
        // RegisterComponent call. Literals 06eead5c / 06eeace8 / 06eead24.
        if (DACOM_Acquire() == nullptr)
        {
            Fdump(kFdumpWarning, 0x10B4, "SoundManager: DllMain: unable to get DACOM!\n");
            return TRUE;
        }
        FSoundManagerFactory* Factory = new (std::nothrow) FSoundManagerFactory();
        if (Factory == nullptr)
        {
            Fdump(kFdumpWarning, 0x10C0, "SoundManager: DllMain: unable to create component factory!\n");
            return TRUE;
        }
        const GENRESULT Result =
            DACOM_Acquire()->RegisterComponent(Factory, kComponentName,
                                               reinterpret_cast<void*>(kRegisterPriority));
        if (Result != GR_OK)
            Fdump(kFdumpWarning, 0x10BA, "SoundManager: DllMain: unable to register component!\n");
        Factory->Release();
    }
    return TRUE;
}
