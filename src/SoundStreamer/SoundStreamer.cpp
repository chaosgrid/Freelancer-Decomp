// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "SoundStreamerInternal.h"
#include "readfile.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mmsystem.h>
#include <new>
#include <stddef.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "dsound.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "msacm32.lib")

namespace
{
    FSoundStreamer* G_Streamer = nullptr;
    HACMDRIVERID G_FraunhoferDriverId = nullptr;
    HACMDRIVER G_FraunhoferDriver = nullptr;

    // FDUMP is DACOM's statically initialised handler (DefaultFdump); retail calls through the
    // import with no null test.
    void Fdump(unsigned int Flags, int Line, const char* Message)
    {
        const char* Fmt = kFdumpWarningFmt;
        if (Flags == kFdumpError)
            Fmt = kFdumpErrorFmt;
        else if (Flags == kFdumpNotice)
            Fmt = kFdumpNoticeFmt;
        FDUMP(static_cast<int>(Flags), Fmt, kRetailSource, Line, Message);
    }

    // FDUMP sites of the streamer object (06ef2e00 / 06ef3060) report SoundStreamer.cpp.
    void FdumpStreamer(unsigned int Flags, int Line, const char* Message)
    {
        const char* Fmt = kFdumpWarningFmt;
        if (Flags == kFdumpError)
            Fmt = kFdumpErrorFmt;
        else if (Flags == kFdumpNotice)
            Fmt = kFdumpNoticeFmt;
        FDUMP(static_cast<int>(Flags), Fmt, kStreamerSource, Line, Message);
    }

    // FUN_06ef2910. No forced terminator.
    char* FormatStreamerMessage(char* Buffer, const char* Fmt, ...)
    {
        va_list Args;
        va_start(Args, Fmt);
        _vsnprintf(Buffer, 0x2000, Fmt, Args);
        va_end(Args);
        return Buffer;
    }

    // Retail formats the file and line into the message and then FDUMPs with them again.
    void FdumpDsound(int Line, HRESULT Hr)
    {
        char Inner[0x2000];
        FdumpStreamer(kFdumpError, Line,
                      FormatStreamerMessage(Inner, "%s(%d) : DSound error #%08X", kStreamerSource,
                                            Line, static_cast<unsigned int>(Hr)));
    }

    // Retail QIs compare inline with no null checks.
    bool IidEqual(const char* Left, const char* Right)
    {
        return strcmp(Left, Right) == 0;
    }

    static const char kFraunhoferName[] = "Fraunhofer IIS MPEG Layer-3 Codec (advanced)";

    struct FAcmSearch
    {
        const char* Name;
        HACMDRIVERID FoundId;
    };

    BOOL CALLBACK AcmEnumProc(HACMDRIVERID DriverId, DWORD_PTR Instance, DWORD /*Support*/)
    {
        FAcmSearch* Search = reinterpret_cast<FAcmSearch*>(Instance);
        if (Search == nullptr || Search->Name == nullptr)
            return TRUE;
        ACMDRIVERDETAILS Details = {};
        Details.cbStruct = 0x398;
        if (acmDriverDetailsA(DriverId, &Details, 0) != 0)
            return TRUE;
        // SOUND-ACM-DECODE-ONLY (docs/re/deviations.md): deviation, the decode-only codec is
        // accepted too. Owner-requested; remove only on owner instruction.
        if (strcmp(Details.szLongName, Search->Name) == 0 ||
            strcmp(Details.szLongName, "Fraunhofer IIS MPEG Layer-3 Codec (decode only)") == 0)
        {
            Search->FoundId = DriverId;
            return FALSE;
        }
        return TRUE;
    }

    HACMDRIVER OpenFraunhoferDriver()
    {
        FAcmSearch Search;
        Search.Name = kFraunhoferName;
        Search.FoundId = nullptr;
        if (acmDriverEnum(AcmEnumProc, reinterpret_cast<DWORD_PTR>(&Search), 0) != 0)
            return nullptr;
        if (Search.FoundId == nullptr)
            return nullptr;
        HACMDRIVER Driver = nullptr;
        if (acmDriverOpen(&Driver, Search.FoundId, 0) != 0)
            return nullptr;
        G_FraunhoferDriverId = Search.FoundId;
        return Driver;
    }

    void StreamingSoundStop(FStreamingSound* Stream);
    unsigned char PumpHandle(FStreamingSound* Stream);

    DWORD WINAPI WorkerThreadProc(LPVOID Param)
    {
        FSoundStreamer* This = static_cast<FSoundStreamer*>(Param);
        const HRESULT Hr = CoInitialize(nullptr);
        if (Hr != S_OK && Hr != S_FALSE)
            return 0;
        const unsigned long Result = This->PumpStreams();
        CoUninitialize();
        return Result;
    }

    // FUN_06ef1990 call sites (StopStream, ShutdownDevice): no handle test of their own.
    void StopHandle(FStreamingSound* Stream)
    {
        StreamingSoundStop(Stream);
    }

    // FUN_06ef1d10. No handle test.
    void StartHandle(FStreamingSound* Stream)
    {
        if (Stream->bWantPlay == 0)
            Stream->bWantPlay = 1;
        if (Stream->State == 1 || Stream->State == 8 || Stream->State == 9)
            Stream->State = 2;
    }

    // FUN_06ef1d70: the buffer's value when it answers, else the cached +0x88.
    long QueryBufferVolume(FStreamingSound* Stream)
    {
        if (Stream->Buffer != nullptr)
        {
            long Queried = 0;
            if (Stream->Buffer->GetVolume(&Queried) >= 0)
                return Queried;
        }
        return Stream->Volume;
    }

    long QueryBufferPan(FStreamingSound* Stream)
    {
        if (Stream->Buffer != nullptr)
        {
            long Queried = 0;
            if (Stream->Buffer->GetPan(&Queried) >= 0)
                return Queried;
        }
        return Stream->Pan;
    }

    struct FFileSystemAggDesc
    {
        unsigned long Size;
        const char* ComponentName;
        const char* BackendName;
        const char* DataPath;
        unsigned long Access;
        unsigned long ShareMode;
        unsigned long Security;
        unsigned long Creation;
        unsigned long Flags;
        unsigned long TemplateFile;
        unsigned long Parent;
        unsigned long Extra;
        unsigned long FindHandle;
    };

    // Inline in CreateStream. DACOM_Acquire() unchecked; CreateInstance writes straight into the
    // caller's out slot.
    GENRESULT CreateFileSystem(const char* Path, IDAComponent** Out)
    {
        FFileSystemAggDesc Desc = {};
        Desc.Size = 0x34;
        Desc.ComponentName = "FileSystem";
        Desc.DataPath = Path;
        Desc.Access = 0x80000000;
        Desc.ShareMode = 1;
        Desc.Security = 0;
        Desc.Creation = 3;
        Desc.Flags = 0x8000080;
        Desc.FindHandle = 0xFFFFFFFF;
        ICOManager* Dacom = DACOM_Acquire();
        return Dacom->CreateInstance(reinterpret_cast<AGGDESC*>(&Desc),
                                     reinterpret_cast<void**>(Out));
    }

    // FUN_06ef1200: the 0x110 stream handle ctor. No null test on Name. Retail leaves
    // +0xAC..+0xB4 and +0xEA unwritten (operator new memory); the port's calloc gives 0, and
    // none of them is read before LoadWav / the decoder writes it.
    void ConstructStreamHandle(FStreamingSound* Stream, const char* Name, IDAComponent* FileSystem)
    {
        const size_t NameLen = strlen(Name);
        Stream->Path = static_cast<char*>(malloc(NameLen + 1));
        memcpy(Stream->Path, Name, NameLen + 1);
        Stream->PathLen = static_cast<unsigned long>(NameLen);
        Stream->PathCap = static_cast<unsigned long>(NameLen + 1);
        Stream->FileSystem = FileSystem;
        if (FileSystem != nullptr)
            FileSystem->AddRef();
        Stream->FileHandle = 0xFFFFFFFF;
        Stream->MapHandle = 0xFFFFFFFF;
        Stream->FactSamples = 0xFFFFFFFF;
        Stream->EncoderDelayBytes = -1;
    }

    // FileSystem slot 0x70 on the path, no checks.
    unsigned long FileSystemAttributes(IDAComponent* FileSystem, const char* Path)
    {
        return static_cast<unsigned long>(
            static_cast<IFileSystem*>(FileSystem)->GetPathAttributes(Path));
    }

    void PtrListPush(FStreamingSound*** Begin, FStreamingSound*** End, FStreamingSound*** Cap,
                     FStreamingSound* Value)
    {
        if (*End == *Cap)
        {
            const size_t Count = static_cast<size_t>(*End - *Begin);
            const size_t NewCap = Count == 0 ? 4 : Count * 2;
            FStreamingSound** Grown =
                static_cast<FStreamingSound**>(realloc(*Begin, NewCap * sizeof(FStreamingSound*)));
            // VC6 vector growth (FUN_06ef3960) does not test its allocation either.
            *Begin = Grown;
            *End = Grown + Count;
            *Cap = Grown + NewCap;
        }
        **End = Value;
        *End += 1;
    }

    void ReopenAcmStream(FStreamingSound* Stream);
    void AdvanceSourceOrDrain(FStreamingSound* Stream, unsigned long Used);
    void SilenceLocked(FStreamingSound* Stream);

    // Retail copies inline or with memcpy, with no pointer tests.
    void CopyBytes(void* Dest, const void* Src, unsigned long Count)
    {
        memcpy(Dest, Src, Count);
    }

    // Only when the count is non-zero; no pointer test.
    void ZeroBytes(void* Dest, unsigned long Count)
    {
        if (Count == 0)
            return;
        memset(Dest, 0, Count);
    }

    // No zero test on the block size.
    unsigned long AlignDown(unsigned long Value, unsigned long Align)
    {
        return (Value / Align) * Align;
    }

    unsigned long AlignUp(unsigned long Value, unsigned long Align)
    {
        if (Align == 0)
            return 0;
        return ((Value + Align - 1) / Align) * Align;
    }

    unsigned long PcmFrameBytes(FStreamingSound* Stream)
    {
        const unsigned long Bits = static_cast<unsigned long>(Stream->PcmBits);
        const unsigned long Channels = static_cast<unsigned long>(Stream->PcmChannels);
        const long Product = static_cast<long>(Bits * Channels);
        return static_cast<unsigned long>((Product + (Product >> 31 & 7)) >> 3);
    }

    unsigned char DecodeHeapExhausted(FStreamingSound* Stream)
    {
        if (Stream->DecodeHeap == nullptr)
            return 1;
        if (Stream->DecodeRead < Stream->DecodeFilled)
            return 0;
        return 1;
    }

    // retail FUN_06ef28c0
    unsigned char* PeekMapped(FStreamingSound* Stream, unsigned long Offset, unsigned long Size)
    {
        // Unsigned range check; no view test.
        if (Stream->MapHandle == 0xFFFFFFFFu)
            return nullptr;
        if (Offset + Size > Stream->MappedSize)
            return nullptr;
        return Stream->MappedView + Offset;
    }

    // retail FUN_06ef2850
    unsigned char MapFileView(FStreamingSound* Stream)
    {
        if (Stream->FileSystem == nullptr)
            return 0;
        IFileSystem* Fs = static_cast<IFileSystem*>(Stream->FileSystem);
        // +0x3C CreateFileMappingA-like, +0x40 MapViewOfFile-like, +0x28 GetSize
        const unsigned long Map =
            static_cast<unsigned long>(Fs->CreateMapping(Stream->FileHandle, 0, 2, 0, 0, 0));
        Stream->MapHandle = Map;
        if (Map == 0xFFFFFFFFu)
            return 0;
        const unsigned long View = static_cast<unsigned long>(Fs->MapFileView(Map, 4, 0, 0, 0));
        Stream->MappedView = reinterpret_cast<unsigned char*>(View);
        Stream->MappedSize = Fs->GetSize(Stream->FileHandle, 0);
        Stream->ReadCursor = 0;
        return Stream->MappedView != nullptr ? 1 : 0;
    }

    void OpenNestedWavFile(FStreamingSound* Stream)
    {
        if (Stream->Path == nullptr || Stream->Path[0] == 0)
        {
            Stream->FileHandle = 0;
            return;
        }
        FFileSystemAggDesc Desc = {};
        Desc.Size = 0x34;
        Desc.ComponentName = "FileSystem";
        Desc.DataPath = Stream->Path;
        Desc.Access = 0x80000000;
        Desc.ShareMode = 1;
        Desc.Creation = 3;
        Desc.Flags = 0x8000080;
        Desc.FindHandle = 0xFFFFFFFF;
        Stream->FileHandle = static_cast<IFileSystem*>(Stream->FileSystem) // +0x7C
                                 ->OpenByDesc(reinterpret_cast<FFileSystemDesc*>(&Desc));
    }

    // retail FUN_06ef1040 — play cursor, not the buffer pointer
    unsigned long GetPlayCursor(FStreamingSound* Stream)
    {
        if (Stream->State != 2 && Stream->State != 3 && Stream->State != 6)
            return 0;
        if (Stream->Buffer == nullptr)
            return 0;
        DWORD Play = 0;
        const HRESULT Hr = Stream->Buffer->GetCurrentPosition(&Play, nullptr);
        if (Hr < 0)
            return 0;
        return Play;
    }

    // retail FUN_06ef1080
    unsigned long GetBufferedBytes(FStreamingSound* Stream)
    {
        const unsigned long Play = GetPlayCursor(Stream);
        const unsigned long Write = Stream->WriteCursor;
        if (Write >= Play)
            return Write - Play;
        return (Stream->BufferBytes - Play) + Write;
    }

    // retail FUN_06ef10b0
    unsigned long ComputeLockFree(FStreamingSound* Stream, unsigned long* Lock1, unsigned long* Lock2)
    {
        *Lock1 = 0;
        *Lock2 = 0;
        if (Stream->State != 2 && Stream->State != 3)
            return 0;
        const unsigned long Play = GetPlayCursor(Stream);
        const unsigned long Write = Stream->WriteCursor;
        if (Write < Play)
        {
            *Lock1 = Play - Write;
            return *Lock1;
        }
        if (Write < Stream->BufferBytes)
        {
            *Lock1 = Stream->BufferBytes - Write;
            *Lock2 = Play;
            return *Lock1 + *Lock2;
        }
        *Lock1 = Play;
        return *Lock1;
    }

    // retail FUN_06ef23c0
    unsigned char LockBufferRegion(FStreamingSound* Stream, unsigned long Want)
    {
        // Want == 0 or a failed Lock (no buffer test) clears only the two pointers; the byte
        // counts are left as they are.
        if (Stream->WriteCursor >= Stream->BufferBytes)
            Stream->WriteCursor = 0;
        if (Want != 0)
        {
            const HRESULT Hr = Stream->Buffer->Lock(Stream->WriteCursor, Want, &Stream->LockPtr1,
                                                    &Stream->LockBytes1, &Stream->LockPtr2,
                                                    &Stream->LockBytes2, 0);
            if (Hr >= 0)
                return 1;
        }
        Stream->LockPtr1 = nullptr;
        Stream->LockPtr2 = nullptr;
        return 0;
    }

    // retail FUN_06ef22d0
    unsigned long TrimEncoderDelay(FStreamingSound* Stream, unsigned char* Dest, unsigned long Count)
    {
        long Delay = Stream->EncoderDelayBytes;
        if (Delay > 0)
        {
            if (Delay < static_cast<long>(Count))
            {
                const unsigned long Skip = static_cast<unsigned long>(Delay);
                memmove(Dest, Dest + Skip, Count - Skip);
                Stream->EncoderDelayBytes = 0;
                Count -= Skip;
            }
            else
            {
                Stream->EncoderDelayBytes = Delay - static_cast<long>(Count);
                Count = 0;
            }
        }
        if (Stream->FactSamples != 0xFFFFFFFFu)
        {
            const unsigned long Limit = PcmFrameBytes(Stream) * Stream->FactSamples;
            if (static_cast<long>(Stream->EmittedPcm + Count) >= static_cast<long>(Limit))
            {
                Count = Limit - Stream->EmittedPcm;
                Stream->AcmHeader.cbSrcLengthUsed = Stream->DataSize - Stream->DataCursor;
                if (Stream->bLoop != 0)
                    ReopenAcmStream(Stream);
                Stream->DecodeRead = 0;
                Stream->DecodeFilled = 0;
                Stream->EncoderDelayBytes = -1;
            }
        }
        Stream->EmittedPcm += Count;
        return Count;
    }

    // retail FUN_06ef2440
    unsigned long UnlockAdvance(FStreamingSound* Stream, unsigned long Used1, unsigned long Used2)
    {
        if (Stream->LockPtr1 == nullptr)
            return 0;
        unsigned long Trimmed1 = 0;
        unsigned long Trimmed2 = 0;
        if (Used1 != 0)
            Trimmed1 = TrimEncoderDelay(Stream, static_cast<unsigned char*>(Stream->LockPtr1), Used1);
        if (Used2 != 0)
        {
            if (Stream->EncoderDelayBytes != -1)
                Trimmed2 = TrimEncoderDelay(Stream, static_cast<unsigned char*>(Stream->LockPtr2), Used2);
        }
        // No buffer test.
        Stream->Buffer->Unlock(Stream->LockPtr1, Trimmed1, Stream->LockPtr2, Trimmed2);
        Stream->LockPtr1 = nullptr;
        Stream->LockPtr2 = nullptr;
        if (Trimmed2 == 0)
        {
            Stream->WriteCursor += Trimmed1;
            return Trimmed1;
        }
        Stream->WriteCursor = Trimmed2;
        return Trimmed1 + Trimmed2;
    }

    // retail FUN_06ef1fb0
    void PrepareAcmSource(FStreamingSound* Stream, unsigned long Want)
    {
        if (Want < Stream->DecodeOutSize)
            Want = Stream->DecodeOutSize;
        if (Stream->DataCursor >= Stream->DataSize)
        {
            Stream->DataCursor = 0;
            Stream->EmittedPcm = 0;
            Stream->DecodeRead = 0;
            Stream->DecodeFilled = 0;
        }
        unsigned long Remain = Stream->DataSize - Stream->DataCursor;
        if (Remain < Want)
            Want = Remain;
        if (Want == 0)
            return;
        Stream->AcmHeader.pbSrc = Stream->DataPtr + Stream->DataCursor;
        Stream->AcmHeader.cbSrcLength = Want;
        Stream->AcmHeader.cbSrcLengthUsed = 0;
    }

    // retail FUN_06ef2190
    unsigned char AcmConvertBlock(FStreamingSound* Stream, void* Dest, unsigned long DestBytes)
    {
        Stream->AcmHeader.cbSrcLengthUsed = 0;
        Stream->AcmHeader.cbDstLengthUsed = 0;
        if (Stream->AcmStream == nullptr)
            return 0;
        // -1 passes through; both divisions are unguarded.
        unsigned long SrcWant = 0;
        if (DestBytes == 0xFFFFFFFFu)
            SrcWant = 0xFFFFFFFFu;
        else
        {
            SrcWant = Stream->AlignedSize * DestBytes / Stream->BytesPerSec;
            SrcWant = AlignDown(SrcWant, Stream->DecodeOutSize);
        }
        PrepareAcmSource(Stream, SrcWant);
        if (Stream->AcmHeader.pbSrc == nullptr || Stream->AcmHeader.cbSrcLength == 0)
            return 0;
        WAVEFORMATEX* Fmt = reinterpret_cast<WAVEFORMATEX*>(Stream->FmtBlob);
        if (Fmt->wFormatTag == 0x55 &&
            Stream->AcmHeader.pbSrc == Stream->DataPtr && Stream->EncoderDelayBytes == -1)
            Stream->EncoderDelayBytes = static_cast<long>(PcmFrameBytes(Stream) * Stream->TrimSamples);
        Stream->AcmHeader.cbDstLength = DestBytes;
        Stream->AcmHeader.pbDst = static_cast<LPBYTE>(Dest);
        unsigned long Flags = 0x24;
        if (Stream->AcmHeader.pbSrc == Stream->DataPtr)
            Flags = 0x14;
        const unsigned long SrcEnd = Stream->DataCursor + Stream->AcmHeader.cbSrcLength;
        if (SrcEnd >= Stream->DataSize && Stream->AcmHeader.cbSrcLength > Stream->DecodeOutSize)
        {
            unsigned long Reduced = Stream->AcmHeader.cbSrcLength - Stream->DecodeOutSize;
            if (Reduced < Stream->DecodeOutSize)
                Reduced = Stream->DecodeOutSize;
            Stream->AcmHeader.cbSrcLength = Reduced;
        }
        acmStreamPrepareHeader(Stream->AcmStream, &Stream->AcmHeader, 0);
        const MMRESULT ConvertHr =
            acmStreamConvert(Stream->AcmStream, &Stream->AcmHeader, Flags);
        const MMRESULT UnprepHr =
            acmStreamUnprepareHeader(Stream->AcmStream, &Stream->AcmHeader, 0);
        if (ConvertHr == 0 && UnprepHr == 0)
            return 1;
        return 0;
    }

    void ReopenAcmStream(FStreamingSound* Stream)
    {
        if (Stream->AcmStream == nullptr)
            return;
        acmStreamClose(Stream->AcmStream, 0);
        Stream->AcmStream = nullptr;
        WAVEFORMATEX* Src = reinterpret_cast<WAVEFORMATEX*>(Stream->FmtBlob);
        WAVEFORMATEX* Dst = reinterpret_cast<WAVEFORMATEX*>(&Stream->PcmTag);
        if (acmStreamOpen(&Stream->AcmStream, G_FraunhoferDriver, Src, Dst, nullptr, 0, 0,
                          ACM_STREAMOPENF_NONREALTIME) != 0)
            Fdump(kFdumpError, 0x3FD, "StreamingSound: unable to re-open stream");
    }

    void SilenceLocked(FStreamingSound* Stream)
    {
        if (Stream->SilenceBytes == 0)
            return;
        const unsigned long Buffered = GetBufferedBytes(Stream);
        if (Stream->BufferBytes - Buffered < Stream->SilenceBytes)
            return;
        if (LockBufferRegion(Stream, Stream->SilenceBytes) == 0)
        {
            Stream->SilenceBytes = 0;
            return;
        }
        ZeroBytes(Stream->LockPtr1, Stream->LockBytes1);
        ZeroBytes(Stream->LockPtr2, Stream->LockBytes2);
        // No buffer test.
        Stream->Buffer->Unlock(Stream->LockPtr1, Stream->LockBytes1, Stream->LockPtr2,
                               Stream->LockBytes2);
        Stream->LockPtr1 = nullptr;
        Stream->LockPtr2 = nullptr;
        Stream->SilenceBytes = 0;
    }

    void AdvanceSourceOrDrain(FStreamingSound* Stream, unsigned long Used)
    {
        Stream->DataCursor += Used;
        if (Stream->DataCursor < Stream->DataSize)
            return;
        if (Stream->bLoop != 0)
        {
            ReopenAcmStream(Stream);
            if (DecodeHeapExhausted(Stream) == 0)
                return;
            if (Stream->EncoderDelayBytes != -1)
                Stream->EncoderDelayBytes = -1;
            Stream->DataCursor = 0;
            Stream->EmittedPcm = 0;
            return;
        }
        if (DecodeHeapExhausted(Stream) == 0)
        {
            Stream->DrainEndCursor = 0xFFFFFFFF;
            return;
        }
        Stream->State = 6;
        Stream->DrainStartCursor = GetPlayCursor(Stream);
        Stream->DrainEndCursor = Stream->WriteCursor;
        Stream->SilenceBytes = Stream->BytesPerSec >> 2;
        SilenceLocked(Stream);
        Stream->EncoderDelayBytes = -1;
    }

    unsigned long CopyHeapIntoLocks(FStreamingSound* Stream, unsigned long Lock1, unsigned long Lock2)
    {
        unsigned long Copied1 = 0;
        if (Stream->DecodeHeap != nullptr)
        {
            unsigned long Avail = Stream->DecodeFilled - Stream->DecodeRead;
            unsigned long Take = Lock1;
            if (Avail < Take)
                Take = Avail;
            if (Take != 0)
            {
                CopyBytes(Stream->LockPtr1, Stream->DecodeHeap + Stream->DecodeRead, Take);
                Stream->DecodeRead += Take;
                Copied1 = Take;
            }
        }
        unsigned long Copied2 = 0;
        if (Stream->DecodeHeap != nullptr)
        {
            unsigned long Avail = Stream->DecodeFilled - Stream->DecodeRead;
            unsigned long Take = Lock2;
            if (Avail < Take)
                Take = Avail;
            if (Take != 0)
            {
                CopyBytes(Stream->LockPtr2, Stream->DecodeHeap + Stream->DecodeRead, Take);
                Stream->DecodeRead += Take;
                Copied2 = Take;
            }
        }
        Stream->LockBytes1 = Copied1;
        Stream->LockBytes2 = Copied2;
        return Copied1 + Copied2;
    }

    // retail FUN_06ef1df0
    unsigned long FillPcmLocked(FStreamingSound* Stream, unsigned long Want)
    {
        unsigned long Copied = 0;
        if (Want == 0)
            return 0;
        while (Copied < Want)
        {
            unsigned long Lock1 = 0;
            unsigned long Lock2 = 0;
            unsigned long Free = ComputeLockFree(Stream, &Lock1, &Lock2);
            unsigned long Take = Want - Copied;
            if (Free < Take)
                Take = Free;
            const unsigned long Remain = Stream->DataSize - Stream->DataCursor;
            if (Remain < Take)
                Take = Remain;
            if (Take == 0)
                return Copied;
            if (LockBufferRegion(Stream, Take) == 0)
                return Copied;
            unsigned char* Src = Stream->DataPtr + Stream->DataCursor;
            CopyBytes(Stream->LockPtr1, Src, Stream->LockBytes1);
            if (Stream->LockBytes2 != 0)
                CopyBytes(Stream->LockPtr2, Src + Stream->LockBytes1, Stream->LockBytes2);
            const unsigned long Used =
                UnlockAdvance(Stream, Stream->LockBytes1, Stream->LockBytes2);
            AdvanceSourceOrDrain(Stream, Used);
            Copied += Used;
        }
        return Copied;
    }

    // retail FUN_06ef24f0
    void FillAcmBlock(FStreamingSound* Stream, unsigned long Want)
    {
        if (Stream->AcmStream == nullptr || Want == 0)
            return;
        if (Stream->WriteCursor >= Stream->BufferBytes)
            Stream->WriteCursor = 0;
        if (Stream->DecodeHeap != nullptr && Stream->DecodeRead < Stream->DecodeFilled)
        {
            if (LockBufferRegion(Stream, Want) == 0)
                return;
            CopyHeapIntoLocks(Stream, Stream->LockBytes1, Stream->LockBytes2);
            // The amount written (after the encoder-delay trim) comes off the request, not the
            // bytes copied out of the heap.
            const unsigned long Written =
                UnlockAdvance(Stream, Stream->LockBytes1, Stream->LockBytes2);
            if (Stream->AcmHeader.cbSrcLengthUsed != 0)
            {
                AdvanceSourceOrDrain(Stream, Stream->AcmHeader.cbSrcLengthUsed);
                Stream->AcmHeader.cbSrcLengthUsed = 0;
            }
            if (Stream->DecodeHeap != nullptr && Stream->DecodeRead < Stream->DecodeFilled)
                return;
            Want -= Written;
        }
        if (Want == 0)
            return;
        if (LockBufferRegion(Stream, Want) == 0)
            return;
        // A lock smaller than the decode heap converts into the heap and copies out; otherwise
        // the convert goes straight into the first lock region. A failed convert only clears the
        // success byte.
        unsigned char bOk = 1;
        if (Stream->LockBytes1 < Stream->DecodeCap)
        {
            if (AcmConvertBlock(Stream, Stream->DecodeHeap, Stream->DecodeCap) == 0)
                bOk = 0;
            const unsigned long Produced = Stream->AcmHeader.cbDstLengthUsed;
            if (Produced != 0)
            {
                Stream->DecodeRead = 0;
                Stream->DecodeFilled = Produced;
                CopyHeapIntoLocks(Stream, Stream->LockBytes1, Stream->LockBytes2);
                Stream->AcmHeader.cbDstLengthUsed = Stream->LockBytes1 + Stream->LockBytes2;
            }
            else
            {
                Stream->LockBytes1 = 0;
                Stream->LockBytes2 = 0;
            }
        }
        else
        {
            if (AcmConvertBlock(Stream, Stream->LockPtr1, Stream->LockBytes1) == 0)
                bOk = 0;
            Stream->LockBytes1 = Stream->AcmHeader.cbDstLengthUsed;
            Stream->LockBytes2 = 0;
        }
        UnlockAdvance(Stream, Stream->LockBytes1, Stream->LockBytes2);
        if (bOk != 0)
        {
            if (Stream->AcmHeader.cbSrcLengthUsed != 0)
            {
                AdvanceSourceOrDrain(Stream, Stream->AcmHeader.cbSrcLengthUsed);
                Stream->AcmHeader.cbSrcLength = 0;
                Stream->AcmHeader.cbSrcLengthUsed = 0;
            }
            Stream->AcmHeader.cbDstLength = 0;
            Stream->AcmHeader.cbDstLengthUsed = 0;
        }
    }

    // retail FUN_06ef27a0
    void FillStreamEntry(FStreamingSound* Stream, unsigned long Want)
    {
        if (Stream->State != 2 && Stream->State != 3)
            return;
        unsigned long Lock1 = 0;
        unsigned long Lock2 = 0;
        const unsigned long Free = ComputeLockFree(Stream, &Lock1, &Lock2);
        if (Free < Stream->DecodeFilled)
            return;
        if (Free < Want)
            Want = Free;
        if (Want == 0)
            return;
        if (Stream->AcmStream != nullptr)
            FillAcmBlock(Stream, Want);
        else
            FillPcmLocked(Stream, Want);
    }

    // retail FUN_06ef1990
    void StreamingSoundStop(FStreamingSound* Stream)
    {
        Stream->bWantPlay = 0;
        if (Stream->State == 2 || Stream->State == 3 || Stream->State == 6)
        {
            if (Stream->Buffer != nullptr)
                Stream->Buffer->Stop();
            Stream->State = 9;
        }
    }

    // retail FUN_06ef19d0 (thiscall, no return value). The port is x87 (/arch:IA32): the double
    // operands are exact widenings, the division is the same single rounding step as retail, and
    // the float locals are retail's FSTP float points (see docs/re/audio.md).
    void PrefillAndPlay(FStreamingSound* Stream)
    {
        Stream->State = 2;
        Stream->bWantPlay = 1;
        // Threshold (+0xC4) clamped to 1.0, compared as a double.
        float Thresh = Stream->Threshold;
        if (Thresh > 1.0)
            Thresh = 1.0f;
        unsigned long Buffered = GetBufferedBytes(Stream);
        // The rate is rounded to float first. No zero-rate guard: buffered/0 is inf or NaN, and
        // both go on to Play.
        const float RateF = static_cast<float>(static_cast<unsigned long long>(Stream->BytesPerSec));
        const double Seconds =
            static_cast<double>(static_cast<unsigned long long>(Buffered)) / static_cast<double>(RateF);
        if (Seconds < static_cast<double>(Thresh))
        {
            const unsigned long Block = Stream->PcmBlock; // +0xE4
            unsigned long Want = 0;
            if (Block != 0)
            {
                // The product of two floats is exact in double.
                const unsigned long Raw = static_cast<unsigned long>(
                    static_cast<long long>(static_cast<double>(RateF) * static_cast<double>(Thresh)));
                Want = ((Raw + Block - 1) / Block) * Block;
            }
            FillStreamEntry(Stream, Want);
            // Clamped to 0.25 and kept on the FPU.
            float WaitThresh = Stream->Threshold;
            if (WaitThresh > 0.25)
                WaitThresh = 0.25f;
            Buffered = GetBufferedBytes(Stream);
            // This division is by the signed rate. Equal or unordered plays.
            const double Seconds2 = static_cast<double>(static_cast<unsigned long long>(Buffered)) /
                                    static_cast<double>(static_cast<long>(Stream->BytesPerSec));
            if (static_cast<double>(WaitThresh) > Seconds2)
                return;
        }
        if (Stream->Buffer == nullptr)
        {
            Stream->State = 0;
            Fdump(kFdumpError, 0x265, "StreamingSound: invalid sound buffer");
            return;
        }
        const HRESULT Hr = Stream->Buffer->Play(0, 0, DSBPLAY_LOOPING);
        if (Hr != 0)
        {
            Stream->State = 0;
            Fdump(kFdumpError, 0x25F, "StreamingSound: failed to play");
            return;
        }
        if (Stream->State == 2)
            Stream->State = 3;
    }

    // retail FUN_06ef1b70
    unsigned char PumpHandle(FStreamingSound* Stream)
    {
        if (Stream->State == 2)
        {
            if (Stream->bWantPlay != 0)
                PrefillAndPlay(Stream);
        }
        else if (Stream->State == 8 || Stream->State == 9)
            Stream->bWantPlay = 0;
        else if (Stream->State == 6)
        {
            if (Stream->Buffer == nullptr)
            {
                Fdump(kFdumpError, 0x282, "StreamingSound: missing buffer?");
                Stream->State = 8;
                return 0;
            }
            if (Stream->DrainEndCursor == 0xFFFFFFFFu && DecodeHeapExhausted(Stream) != 0)
                Stream->DrainEndCursor = Stream->WriteCursor;
            DWORD Play = 0;
            const HRESULT Hr = Stream->Buffer->GetCurrentPosition(&Play, nullptr);
            unsigned char bInside = 0;
            if (Hr >= 0)
            {
                const unsigned long StartMark = Stream->DrainStartCursor;
                const unsigned long EndMark = Stream->DrainEndCursor;
                if (StartMark > EndMark)
                {
                    if (Play < EndMark || Play >= StartMark)
                        bInside = 1;
                }
                else if (Play >= StartMark && Play < EndMark)
                    bInside = 1;
            }
            else
                Fdump(kFdumpError, 0x2AF, "StreamingSound: failed to get play position");
            if (Hr >= 0 && bInside != 0)
            {
                if (Stream->SilenceBytes != 0)
                    SilenceLocked(Stream);
            }
            else
            {
                StreamingSoundStop(Stream);
                Stream->State = 8;
            }
        }
        // No buffer test.
        if (Stream->State == 2 || Stream->State == 3 || Stream->State == 6)
        {
            Stream->Buffer->SetVolume(Stream->Volume);
            Stream->Buffer->SetPan(Stream->Pan);
            return 1;
        }
        return 0;
    }

}

FStreamerInner::FStreamerInner() : RefCount(1), Self(nullptr) {}

GENRESULT DACOM_API FStreamerInner::QueryInterface(const char* Iid, void** Out)
{
    // LAB_06ef3d00: table 06efc24c {IStreamer,+0} {IStreamer2,+0} {IAggregateComponent,+4}, no
    // null checks. The hit is AddRef'd through its own vtable.
    if (IidEqual(Iid, Iids::Streamer) || IidEqual(Iid, Iids::Streamer2))
    {
        IStreamer2* Facet = static_cast<IStreamer2*>(Self);
        Facet->AddRef();
        *Out = Facet;
        return GR_OK;
    }
    if (IidEqual(Iid, Iids::AggregateComponent))
    {
        IAggregateComponent* Facet = static_cast<IAggregateComponent*>(Self);
        Facet->AddRef();
        *Out = Facet;
        return GR_OK;
    }
    *Out = nullptr;
    return GR_INTERFACE_UNSUPPORTED;
}

unsigned long DACOM_API FStreamerInner::AddRef()
{
    ++RefCount;
    return RefCount;
}

unsigned long DACOM_API FStreamerInner::Release()
{
    if (RefCount > 0)
        --RefCount;
    const unsigned long Remaining = RefCount;
    if (Remaining == 0)
    {
        // LAB_06ef3dd0. The dtor FUN_06ef2b50's ShutdownDevice clears DAT_06f010a4; this does not.
        RefCount = 1;
        FSoundStreamer* Owner = Self;
        if (Owner != nullptr)
            delete Owner;
        return 0;
    }
    return Remaining;
}

FSoundStreamerFactory::FSoundStreamerFactory() : RefCount(1), Name(kComponentName) {}

GENRESULT DACOM_API FSoundStreamerFactory::QueryInterface(const char* Iid, void** Out)
{
    // LAB_06ef3b80: REPE CMPSB over 0x17 bytes. No null checks.
    *Out = nullptr;
    if (memcmp(Iid, Iids::ComponentFactory, 0x17) != 0)
        return GR_INTERFACE_UNSUPPORTED;
    *Out = static_cast<IComponentFactory*>(this);
    AddRef();
    return GR_OK;
}

unsigned long DACOM_API FSoundStreamerFactory::AddRef()
{
    ++RefCount;
    return RefCount;
}

unsigned long DACOM_API FSoundStreamerFactory::Release()
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

GENRESULT DACOM_API FSoundStreamerFactory::CreateInstance(AGGDESC* Desc, void** Instance)
{
    // LAB_06ef3bf0. No null checks on Desc, its name or Instance.
    *Instance = nullptr;
    FStreamerAggDesc* StreamDesc = reinterpret_cast<FStreamerAggDesc*>(Desc);
    if (StreamDesc->Size != kAggDescSize || strcmp(StreamDesc->Name, Name) != 0)
        return GR_INTERFACE_UNSUPPORTED;

    FSoundStreamer* Object = new (std::nothrow) FSoundStreamer();
    if (Object == nullptr)
        return GR_OUT_OF_MEMORY;

    Object->BindOuter(StreamDesc);
    // FUN_06ef2d80: a second streamer is refused with -1.
    if (G_Streamer != nullptr)
    {
        if (StreamDesc->InnerOut != nullptr)
            *StreamDesc->InnerOut = nullptr;
        delete Object;
        return GR_GENERIC;
    }
    G_Streamer = Object;
    Object->Initialize();
    *Instance = static_cast<IStreamer2*>(Object);
    return GR_OK;
}

FSoundStreamer::FSoundStreamer()
    : DirectSound(nullptr), Window(nullptr), Extra(0), Pad14(0), Gain(1.0f), bCoInitialized(0),
      ActiveCookie(0), ActiveBegin(nullptr), ActiveEnd(nullptr), ActiveCap(nullptr), PendingCookie(0),
      PendingBegin(nullptr), PendingEnd(nullptr), PendingCap(nullptr), Reserved58(0), Reserved5C(0),
      WorkerThread(nullptr), ThreadFlags(0), WakeEvent(nullptr), Controlling(nullptr)
{
    Pad1D[0] = 0;
    Pad1D[1] = 0;
    Pad1D[2] = 0;
    InitializeCriticalSection(&Lock);
    WakeEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    Inner.Self = this;
    Controlling = &Inner;
}

FSoundStreamer::~FSoundStreamer()
{
    // FUN_06ef2b50. The CloseHandle is unconditional; DAT_06f010a4 is left to ShutdownDevice.
    ShutdownDevice();
    CloseHandle(WakeEvent);
    free(PendingBegin);
    PendingBegin = nullptr;
    PendingEnd = nullptr;
    PendingCap = nullptr;
    free(ActiveBegin);
    ActiveBegin = nullptr;
    ActiveEnd = nullptr;
    ActiveCap = nullptr;
    DeleteCriticalSection(&Lock);
}

void FSoundStreamer::BindOuter(FStreamerAggDesc* Desc)
{
    // Inline in LAB_06ef3bf0; InnerOut is written unchecked when there is an outer.
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

// LAB_06ef3e10 / 3e20 / 3e30: forward to the controlling unknown at +0x6C. No null check.
GENRESULT DACOM_API FSoundStreamer::QueryInterface(const char* Iid, void** Out)
{
    return Controlling->QueryInterface(Iid, Out);
}

unsigned long DACOM_API FSoundStreamer::AddRef()
{
    return Controlling->AddRef();
}

unsigned long DACOM_API FSoundStreamer::Release()
{
    return Controlling->Release();
}

GENRESULT DACOM_API FSoundStreamer::Initialize()
{
    DirectSound = nullptr;
    Window = nullptr;
    Extra = 0;
    Pad14 = 0;
    Gain = 0.0f;
    WorkerThread = nullptr;
    ThreadFlags = 0;
    // FUN_06ef1000 on every Initialize (driver DAT_06f010a0, open FUN_06ef3fb0). A previously
    // opened driver is neither checked nor closed.
    G_FraunhoferDriver = OpenFraunhoferDriver();
    if (G_FraunhoferDriver == nullptr)
        Fdump(kFdumpError, 0x32, "StreamingSound: missing MP3 codec?");
    return GR_OK;
}

bool FSoundStreamer::OpenOwnDirectSound()
{
    const HRESULT Hr = CoInitialize(nullptr);
    if (FAILED(Hr))
    {
        FdumpStreamer(kFdumpError, 0x1B2, "SoundStreamer - Failed to CoInitialize."); // 06efe370
        return false;
    }

    const HRESULT CreateHr =
        CoCreateInstance(kClsidDirectSound, nullptr, CLSCTX_INPROC_SERVER, kIidDirectSoundRetail,
                         reinterpret_cast<void**>(&DirectSound));
    // A failed CoCreateInstance is only reported; Initialize is then called unchecked.
    if (CreateHr != 0)
        FdumpDsound(0x1BB, CreateHr);

    if (DirectSound->Initialize(nullptr) != 0)
    {
        DirectSound->Release();
        DirectSound = nullptr;
        CoUninitialize();
        bCoInitialized = 0;
        FdumpStreamer(kFdumpError, 0x1C7, "SoundStreamer - Failed to initialize Direct Sound."); // 06efe2d0
        return false;
    }
    const HRESULT CoopHr = DirectSound->SetCooperativeLevel(Window, DSSCL_NORMAL);
    if (CoopHr != 0)
        FdumpDsound(0x1CC, CoopHr);
    return true;
}

int DACOM_API FSoundStreamer::InitializeDevice(FStreamerInitDesc* Desc)
{
    if (Desc == nullptr || Desc->Size != 0x18)
    {
        FdumpStreamer(kFdumpError, 0x1A5, "SoundStreamer - Invalid parameters."); // 06efe254
        return 0;
    }

    Window = Desc->Window;
    Extra = Desc->Extra;

    if (Desc->DirectSound != nullptr)
    {
        DirectSound = Desc->DirectSound;
        DirectSound->AddRef();
    }
    else if (!OpenOwnDirectSound())
        return 0;

    Gain = Desc->Gain;
    if (Gain == 0.0f)
        Gain = 1.0f;

    DWORD ThreadId = 0;
    WorkerThread = CreateThread(nullptr, 0x1000, WorkerThreadProc, this, 0, &ThreadId);
    if (WorkerThread == nullptr)
    {
        FdumpStreamer(kFdumpError, 0x21A, "SoundStreamer - failed to Create thread."); // 06efe278
        if (DirectSound != nullptr)
        {
            DirectSound->Release();
            DirectSound = nullptr;
        }
        return 0;
    }
    SetThreadPriority(WorkerThread, THREAD_PRIORITY_HIGHEST);

    int Wait = 500;
    while ((ThreadFlags & 3) == 0)
    {
        Sleep(10);
        --Wait;
        if (Wait == 0)
        {
            FdumpStreamer(kFdumpError, 0x20F, "SoundStreamer - failed to Activate thread."); // 06efe2a4
            // The handle is closed but +0x60 keeps the stale value.
            CloseHandle(WorkerThread);
            if (DirectSound != nullptr)
            {
                DirectSound->Release();
                DirectSound = nullptr;
            }
            return 0;
        }
    }
    return 1;
}

void FSoundStreamer::ShutdownDevice()
{
    if (G_Streamer == nullptr)
        return;
    G_Streamer = nullptr;

    EnterCriticalSection(&Lock);
    FStreamingSound** Scan = ActiveBegin;
    while (Scan != ActiveEnd)
    {
        StopHandle(*Scan);
        ++Scan;
    }
    LeaveCriticalSection(&Lock);

    if (WorkerThread != nullptr)
    {
        ThreadFlags |= 0x80;
        WaitForSingleObject(WorkerThread, 2000);
        CloseHandle(WorkerThread);
        WorkerThread = nullptr;
    }

    EnterCriticalSection(&Lock);
    Scan = ActiveBegin;
    while (Scan != ActiveEnd)
    {
        FStreamingSound* Stream = *Scan;
        if (Stream != nullptr)
        {
            ReleaseStreamResources(Stream);
            free(Stream);
        }
        ++Scan;
    }
    ActiveEnd = ActiveBegin;
    Scan = PendingBegin;
    while (Scan != PendingEnd)
    {
        FStreamingSound* Stream = *Scan;
        if (Stream != nullptr)
        {
            ReleaseStreamResources(Stream);
            free(Stream);
        }
        ++Scan;
    }
    PendingEnd = PendingBegin;
    LeaveCriticalSection(&Lock);

    if (DirectSound != nullptr)
    {
        DirectSound->AddRef();
        DirectSound->Release();
        DirectSound->Release();
        DirectSound = nullptr;
    }
    if (bCoInitialized != 0)
        CoUninitialize();
    bCoInitialized = 0;
}

void FSoundStreamer::ReleaseStreamResources(FStreamingSound* Stream)
{
    // The teardown + dtor sequence every retail delete site runs (FUN_06ef1120, FUN_06ef3910,
    // FUN_06ef3ea0, FUN_06ef3ec0); the caller frees the 0x110 block. No null test on the handle.
    Stream->bBufferReady = 0;
    if (Stream->Buffer != nullptr)
    {
        Stream->Buffer->Stop();
        Stream->Buffer->Release();
        Stream->Buffer = nullptr;
    }
    Stream->DataPtr = nullptr;
    Stream->FmtBlob = nullptr;
    if (Stream->FileSystem != nullptr)
    {
        IFileSystem* Fs = static_cast<IFileSystem*>(Stream->FileSystem);
        if (Stream->MapHandle != 0xFFFFFFFFu)
        {
            Fs->CloseFile(Stream->MapHandle); // +0x10
            Stream->MapHandle = 0xFFFFFFFF;
        }
        if (Stream->FileHandle != 0xFFFFFFFFu)
        {
            Fs->CloseFile(Stream->FileHandle); // +0x10
            Stream->FileHandle = 0xFFFFFFFF;
        }
        Stream->FileSystem->Release();
        Stream->FileSystem = nullptr;
    }
    if (Stream->AcmStream != nullptr)
    {
        free(Stream->DecodeHeap);
        Stream->DecodeHeap = nullptr;
        Stream->AcmHeader.pbSrc = nullptr;
        Stream->AcmHeader.pbDst = nullptr;
        acmStreamClose(Stream->AcmStream, 0);
        Stream->AcmStream = nullptr;
    }
    Stream->State = 0;
    // FUN_06ef3ea0: +0xA8 freed again when still set (no ACM stream but a decode heap).
    if (Stream->DecodeHeap != nullptr)
    {
        free(Stream->DecodeHeap);
        Stream->DecodeHeap = nullptr;
    }
    free(Stream->Path);
    Stream->Path = nullptr;
}

bool FSoundStreamer::OpenAcmStream(FStreamingSound* Stream)
{
    // FUN_06ef13d0. No handle check.
    if (Stream->DataPtr == nullptr)
        return false;
    WAVEFORMATEX* Src = reinterpret_cast<WAVEFORMATEX*>(Stream->FmtBlob);
    WAVEFORMATEX* Dst = reinterpret_cast<WAVEFORMATEX*>(&Stream->PcmTag);
    Dst->wFormatTag = WAVE_FORMAT_PCM;
    if (acmFormatSuggest(G_FraunhoferDriver, Src, Dst, 0x12, ACM_FORMATSUGGESTF_WFORMATTAG) != 0)
        return false;
    if (acmStreamOpen(&Stream->AcmStream, G_FraunhoferDriver, Src, Dst, nullptr, 0, 0,
                      ACM_STREAMOPENF_NONREALTIME) != 0)
        return false;
    memset(&Stream->AcmHeader, 0, sizeof(Stream->AcmHeader));
    Stream->AcmHeader.cbStruct = 0x54;
    const unsigned long Avg = *reinterpret_cast<unsigned long*>(Stream->FmtBlob + 8);
    // Low dword of the int64 truncation.
    const double Scaled = static_cast<double>(static_cast<unsigned long long>(Avg)) *
                          static_cast<double>(Stream->Threshold);
    const unsigned long SrcGuess = static_cast<unsigned long>(static_cast<long long>(Scaled));
    Stream->AcmHeader.cbSrcLength = SrcGuess;
    if (acmStreamSize(Stream->AcmStream, SrcGuess, &Stream->AcmHeader.cbDstLength,
                      ACM_STREAMSIZEF_SOURCE) != 0)
        return false;
    if (acmStreamSize(Stream->AcmStream, Stream->AcmHeader.cbDstLength, &Stream->AcmHeader.cbSrcLength,
                      ACM_STREAMSIZEF_DESTINATION) != 0)
        return false;
    DWORD Block = 0;
    const unsigned short SrcAlign = *reinterpret_cast<unsigned short*>(Stream->FmtBlob + 0xC);
    if (acmStreamSize(Stream->AcmStream, SrcAlign, &Block, ACM_STREAMSIZEF_SOURCE) != 0)
        Block = 0x1000;
    Stream->SourceBlock = Block;
    Stream->DecodeCap = Block * 3;
    Stream->DecodeHeap = static_cast<unsigned char*>(malloc(Stream->DecodeCap));
    Stream->DecodeRead = 0;
    Stream->DecodeFilled = 0;
    DWORD DestGuess = 0;
    if (acmStreamSize(Stream->AcmStream, Stream->SourceBlock, &DestGuess, ACM_STREAMSIZEF_DESTINATION) == 0)
        Stream->DecodeOutSize = DestGuess;
    else
        Stream->DecodeOutSize = SrcAlign;
    return true;
}

// Direct calls of FUN_06ef27a0 / FUN_06ef1b70 from the worker and StartStream; retail passes the
// handle straight through.
void FSoundStreamer::FillStreamBlock(FStreamingSound* Stream, unsigned long Want)
{
    FillStreamEntry(Stream, Want);
}

void FSoundStreamer::PumpOneStream(FStreamingSound* Stream)
{
    PumpHandle(Stream);
}

void FSoundStreamer::MovePendingActive()
{
    // FUN_06ef3790: no null-handle checks. A moving entry is erased in place and appended to the
    // other vector (FUN_06ef3960).
    EnterCriticalSection(&Lock);
    FStreamingSound** Scan = ActiveBegin;
    while (Scan != ActiveEnd)
    {
        FStreamingSound* Stream = *Scan;
        if (Stream->State == 2 || Stream->State == 3 || Stream->State == 6)
        {
            ++Scan;
            continue;
        }
        memmove(Scan, Scan + 1, static_cast<size_t>(ActiveEnd - (Scan + 1)) * sizeof(*Scan));
        --ActiveEnd;
        PtrListPush(&PendingBegin, &PendingEnd, &PendingCap, Stream);
    }
    Scan = PendingBegin;
    while (Scan != PendingEnd)
    {
        FStreamingSound* Stream = *Scan;
        if (Stream->State != 2 && Stream->State != 3 && Stream->State != 6)
        {
            ++Scan;
            continue;
        }
        memmove(Scan, Scan + 1, static_cast<size_t>(PendingEnd - (Scan + 1)) * sizeof(*Scan));
        --PendingEnd;
        PtrListPush(&ActiveBegin, &ActiveEnd, &ActiveCap, Stream);
    }
    LeaveCriticalSection(&Lock);
}

unsigned long FSoundStreamer::PumpStreams()
{
    ThreadFlags |= 1;
    while (static_cast<char>(ThreadFlags) >= 0)
    {
        if (ActiveBegin != nullptr && ActiveBegin != ActiveEnd)
        {
            // The port is x87 (/arch:IA32): double locals are exact widenings of retail's
            // register values, the float locals are retail's float stores (rate, threshold,
            // best), and each multiply / divide is the same single rounding step.
            EnterCriticalSection(&Lock);
            FStreamingSound* Emptiest = nullptr;
            float BestBuffered = 60.0f;
            const float Threshold = 3.0f * 0.04f; // DAT_06efc16c * DAT_06efc1e0
            FStreamingSound** Scan = ActiveBegin;
            while (Scan != ActiveEnd)
            {
                FStreamingSound* Stream = *Scan;
                ++Scan;
                if (Stream == nullptr)
                    continue;
                PumpOneStream(Stream);
                if (Stream->State != 3 && Stream->State != 2)
                    continue;
                const unsigned long Buffered = GetBufferedBytes(Stream);
                // A zero rate gives inf/NaN, which fails both '<' tests below, as in retail.
                const float Rate = static_cast<float>(static_cast<unsigned long long>(Stream->BytesPerSec));
                const double Seconds =
                    static_cast<double>(static_cast<unsigned long long>(Buffered)) / static_cast<double>(Rate);
                if (Seconds < static_cast<double>(Threshold))
                {
                    unsigned long Want = 0;
                    const unsigned long Block = Stream->PcmBlock;
                    if (Block != 0)
                    {
                        const unsigned long Raw = static_cast<unsigned long>(
                            static_cast<long long>(static_cast<double>(Threshold) * static_cast<double>(Rate)));
                        Want = ((Raw + Block - 1) / Block) * Block;
                    }
                    FillStreamBlock(Stream, Want);
                }
                else if (Seconds < static_cast<double>(BestBuffered))
                {
                    BestBuffered = static_cast<float>(Seconds);
                    Emptiest = Stream;
                }
            }
            if (Emptiest != nullptr)
            {
                unsigned long Count = 0;
                if (ActiveBegin != nullptr)
                    Count = static_cast<unsigned long>(ActiveEnd - ActiveBegin);
                unsigned long Want = 0;
                const unsigned long Block = Emptiest->PcmBlock;
                if (Block != 0)
                {
                    // 4.0f is DAT_06efc170; the rate is multiplied as a signed int.
                    const double Raw = static_cast<double>(static_cast<int>(Count)) * 4.0 *
                                       static_cast<double>(0.04f) *
                                       static_cast<double>(static_cast<int>(Emptiest->BytesPerSec));
                    const unsigned long Bytes =
                        static_cast<unsigned long>(static_cast<long long>(Raw));
                    Want = ((Bytes + Block - 1) / Block) * Block;
                }
                FillStreamBlock(Emptiest, Want);
            }
            LeaveCriticalSection(&Lock);
        }
        MovePendingActive();
        Sleep(0x28);
    }
    ThreadFlags |= 2;
    return 1;
}

bool FSoundStreamer::LoadWav(FStreamingSound* Stream, const char* Path, IDAComponent* FileSystem)
{
    // FUN_06ef1560: retail's arguments are the streamer's DirectSound (+8) and Gain (+0x18).
    (void)Path;
    (void)FileSystem;
    if (Stream->bBufferReady != 0 || Stream->State != 0)
        return false;
    if (DirectSound == nullptr)
        return false;
    // Less or equal fails; greater and NaN go on.
    Stream->Threshold = Gain;
    if (Gain < 0.0f || Gain == 0.0f)
        return false;
    if (Stream->FileSystem == nullptr)
        return false;

    OpenNestedWavFile(Stream);
    if (MapFileView(Stream) == 0)
        return false;

    unsigned char* Riff = PeekMapped(Stream, Stream->ReadCursor, 0xC);
    if (Riff == nullptr)
        return false;
    Stream->ReadCursor += 0xC;
    if (*reinterpret_cast<unsigned long*>(Riff) != 0x46464952)
        return false;
    if (*reinterpret_cast<unsigned long*>(Riff + 8) != 0x45564157)
        return false;

    unsigned char* FmtHdr = nullptr;
    unsigned char* DataHdr = nullptr;
    unsigned char* FactHdr = nullptr;
    unsigned char* TrimHdr = nullptr;
    for (;;)
    {
        unsigned char* Chunk = PeekMapped(Stream, Stream->ReadCursor, 8);
        if (Chunk == nullptr)
            break;
        Stream->ReadCursor += 8;
        const unsigned long Id = *reinterpret_cast<unsigned long*>(Chunk);
        const unsigned long Size = *reinterpret_cast<unsigned long*>(Chunk + 4);
        if (Id == 0x6D697274)
            TrimHdr = Chunk;
        else if (Id == 0x20746D66)
            FmtHdr = Chunk;
        else if (Id == 0x61746164)
            DataHdr = Chunk;
        else if (Id == 0x74636166)
            FactHdr = Chunk;
        // Signed clamp to [0, mapped size].
        long Next = static_cast<long>(Stream->ReadCursor + Size);
        if (Next < 0)
            Next = 0;
        if (Next > static_cast<long>(Stream->MappedSize))
            Next = static_cast<long>(Stream->MappedSize);
        Stream->ReadCursor = static_cast<unsigned long>(Next);
        if (FmtHdr != nullptr && FactHdr != nullptr && DataHdr != nullptr)
            break;
    }
    if (FmtHdr == nullptr || DataHdr == nullptr)
        return false;

    if (FactHdr != nullptr && *reinterpret_cast<unsigned long*>(FactHdr + 4) >= 4)
        Stream->FactSamples = *reinterpret_cast<unsigned long*>(FactHdr + 8);
    else
        Stream->FactSamples = 0xFFFFFFFF;

    Stream->FmtBlob = FmtHdr + 8;
    const unsigned short Tag = *reinterpret_cast<unsigned short*>(Stream->FmtBlob);
    const unsigned long FmtSize = *reinterpret_cast<unsigned long*>(FmtHdr + 4);
    if (Tag == 1)
    {
        if (FmtSize < 0x10)
            return false;
    }
    else if (FmtSize < 0x12)
        return false;

    if (TrimHdr != nullptr && *reinterpret_cast<unsigned long*>(TrimHdr + 4) == 4)
        Stream->TrimSamples = *reinterpret_cast<unsigned long*>(TrimHdr + 8);
    else
        Stream->TrimSamples = 0;

    Stream->DataPtr = DataHdr + 8;
    Stream->DataSize = *reinterpret_cast<unsigned long*>(DataHdr + 4);
    const unsigned short BlockAlign = *reinterpret_cast<unsigned short*>(Stream->FmtBlob + 0xC);
    const unsigned long Avg = *reinterpret_cast<unsigned long*>(Stream->FmtBlob + 8);
    Stream->AlignedSize = AlignUp(Avg, BlockAlign);

    unsigned long BufferBytes = 0;
    char Msg[0x2000];
    if (Tag == 1)
    {
        // Low dword of the int64 truncation.
        CopyBytes(&Stream->PcmTag, Stream->FmtBlob, 0x12);
        Stream->BytesPerSec = Stream->PcmAvg;
        const double Scaled = static_cast<double>(static_cast<unsigned long long>(Stream->PcmAvg)) *
                              static_cast<double>(Stream->Threshold);
        BufferBytes = static_cast<unsigned long>(static_cast<long long>(Scaled));
    }
    else
    {
        if (!OpenAcmStream(Stream))
        {
            // "" is DAT_06efc14c.
            const char* Name = Stream->Path != nullptr ? Stream->Path : "";
            Fdump(kFdumpError, 0x1E7,
                  FormatStreamerMessage(Msg,
                                        "StreamingSound: Error initializing ACM for WAV file  '%s'\n",
                                        Name)); // 06efe14c
            return false;
        }
        Stream->BytesPerSec = Stream->PcmAvg;
        BufferBytes = Stream->AcmHeader.cbDstLength;
    }

    if (Stream->Buffer != nullptr)
    {
        Stream->Buffer->Release();
        Stream->Buffer = nullptr;
    }

    DSBUFFERDESC Desc = {};
    Desc.dwSize = 0x14;
    Desc.dwFlags = 0x100C0;
    Desc.dwBufferBytes = BufferBytes;
    Desc.lpwfxFormat = reinterpret_cast<WAVEFORMATEX*>(&Stream->PcmTag);
    const HRESULT CreateHr = DirectSound->CreateSoundBuffer(&Desc, &Stream->Buffer, nullptr);
    if (CreateHr < 0)
    {
        const char* Name = Stream->Path != nullptr ? Stream->Path : "";
        Fdump(kFdumpError, 0x208,
              FormatStreamerMessage(Msg, "StreamingSound: '%s': createsoundbuffer failed, error=#%08X",
                                    Name, static_cast<unsigned int>(CreateHr)));
        return false;
    }
    // No buffer checks.
    Stream->Buffer->GetFormat(reinterpret_cast<WAVEFORMATEX*>(&Stream->PcmTag), 0x12, nullptr);
    Stream->BufferBytes = Desc.dwBufferBytes;
    Stream->WriteCursor = 0;
    Stream->EmittedPcm = 0;
    Stream->DecodeRead = 0;
    Stream->DecodeFilled = 0;
    Stream->Buffer->SetVolume(Stream->Volume);
    Stream->Buffer->SetPan(Stream->Pan);
    Stream->State = 1;
    Stream->bBufferReady = 1;
    return true;
}

void* DACOM_API FSoundStreamer::CreateStream(const char* Path, IDAComponent* FileSystem,
                                             unsigned long Flags)
{
    // FUN_06ef3060.
    if (WorkerThread == nullptr)
        return nullptr;

    // OwnedFs is released on every exit after this point. Name is what the handle gets and what
    // the error message prints.
    IDAComponent* OwnedFs = nullptr;
    const char* Name = nullptr;
    if (FileSystem == nullptr)
    {
        // Create a FileSystem rooted at the path; the handle then gets a NULL name.
        // Retail: FUN_06ef1200 runs repne scasb on that NULL (06ef123e), so this path faults.
        if (Path == nullptr)
            return nullptr;
        if (CreateFileSystem(Path, &OwnedFs) != GR_OK)
        {
            if (OwnedFs != nullptr)
                OwnedFs->Release();
            return nullptr;
        }
        FileSystem = OwnedFs;
        Name = nullptr;
    }
    else
    {
        // A missing path or a directory returns -1, not null.
        Name = Path;
        const unsigned long Attrs = FileSystemAttributes(FileSystem, Path);
        if (Attrs == static_cast<unsigned long>(-1) || (Attrs & 0x10) != 0)
            return reinterpret_cast<void*>(static_cast<uintptr_t>(0xFFFFFFFFu));
    }

    // operator new(0x110) + FUN_06ef1200; a null allocation is dereferenced unchecked.
    FStreamingSound* Stream = static_cast<FStreamingSound*>(calloc(1, sizeof(FStreamingSound)));
    ConstructStreamHandle(Stream, Name, FileSystem);
    Stream->bLoop = static_cast<unsigned char>((Flags >> 1) & 1);
    if (!LoadWav(Stream, Name, FileSystem))
    {
        char ReadMsg[0x2000];
        FdumpStreamer(kFdumpError, 0x254,
                      FormatStreamerMessage(ReadMsg, "StreamingSound: Error reading WAV file  '%s'\n",
                                            Name));
        ReleaseStreamResources(Stream);
        free(Stream);
        if (OwnedFs != nullptr)
            OwnedFs->Release();
        return nullptr;
    }

    if ((Flags & 1) != 0)
        StartHandle(Stream);

    EnterCriticalSection(&Lock);
    PtrListPush(&PendingBegin, &PendingEnd, &PendingCap, Stream);
    LeaveCriticalSection(&Lock);

    if (OwnedFs != nullptr)
        OwnedFs->Release();
    return Stream;
}

GENRESULT DACOM_API FSoundStreamer::DestroyStream(void* Handle)
{
    FStreamingSound* Stream = static_cast<FStreamingSound*>(Handle);
    if (Stream == nullptr || WorkerThread == nullptr)
        return static_cast<GENRESULT>(0);
    // LAB_06ef3290: the handle is torn down and freed only where it is found (first match in
    // the active vector, then in the pending vector). A handle in neither vector is left alone
    // (retail leaks it).
    StopStream(Stream);
    EnterCriticalSection(&Lock);
    FStreamingSound** Scan = ActiveBegin;
    while (Scan != ActiveEnd && *Scan != Stream)
        ++Scan;
    if (Scan != ActiveEnd)
    {
        if (*Scan != nullptr)
        {
            ReleaseStreamResources(*Scan);
            free(*Scan);
        }
        memmove(Scan, Scan + 1, static_cast<size_t>(ActiveEnd - (Scan + 1)) * sizeof(*Scan));
        --ActiveEnd;
    }
    Scan = PendingBegin;
    while (Scan != PendingEnd && *Scan != Stream)
        ++Scan;
    if (Scan != PendingEnd)
    {
        if (*Scan != nullptr)
        {
            ReleaseStreamResources(*Scan);
            free(*Scan);
        }
        memmove(Scan, Scan + 1, static_cast<size_t>(PendingEnd - (Scan + 1)) * sizeof(*Scan));
        --PendingEnd;
    }
    LeaveCriticalSection(&Lock);
    return static_cast<GENRESULT>(1);
}

GENRESULT DACOM_API FSoundStreamer::StopStream(void* Handle)
{
    FStreamingSound* Stream = static_cast<FStreamingSound*>(Handle);
    if (Stream == nullptr || WorkerThread == nullptr)
        return static_cast<GENRESULT>(0);
    StopHandle(Stream);
    return static_cast<GENRESULT>(1);
}

GENRESULT DACOM_API FSoundStreamer::StartStream(void* Handle)
{
    FStreamingSound* Stream = static_cast<FStreamingSound*>(Handle);
    if (Stream == nullptr || WorkerThread == nullptr)
        return static_cast<GENRESULT>(0);
    StartHandle(Stream);
    EnterCriticalSection(&Lock);
    PumpOneStream(Stream);
    LeaveCriticalSection(&Lock);
    return static_cast<GENRESULT>(1);
}

GENRESULT DACOM_API FSoundStreamer::SetVolume(void* Handle, long Volume)
{
    FStreamingSound* Stream = static_cast<FStreamingSound*>(Handle);
    if (Stream == nullptr || WorkerThread == nullptr)
        return static_cast<GENRESULT>(0);
    Stream->Volume = Volume;
    if (Stream->Buffer != nullptr)
        Stream->Buffer->SetVolume(Volume);
    return static_cast<GENRESULT>(1);
}

GENRESULT DACOM_API FSoundStreamer::GetVolume(void* Handle, long* Out)
{
    FStreamingSound* Stream = static_cast<FStreamingSound*>(Handle);
    if (Stream == nullptr || WorkerThread == nullptr)
        return static_cast<GENRESULT>(0);
    *Out = QueryBufferVolume(Stream);
    return static_cast<GENRESULT>(1);
}

unsigned long DACOM_API FSoundStreamer::QueryStatus(void* Handle)
{
    FStreamingSound* Stream = static_cast<FStreamingSound*>(Handle);
    if (Stream == nullptr || WorkerThread == nullptr)
        return 0;
    switch (Stream->State - 1)
    {
    case 0:
    case 1:
    case 8:
        return 2;
    case 2:
    case 5:
    case 6:
        return 1;
    case 4:
        return 3;
    case 7:
        return 4;
    default:
        return 0;
    }
}

unsigned long DACOM_API FSoundStreamer::GetReserved58()
{
    return Reserved58;
}

unsigned long DACOM_API FSoundStreamer::GetReserved5C()
{
    return Reserved5C;
}

GENRESULT DACOM_API FSoundStreamer::SetPan(void* Handle, long Pan)
{
    FStreamingSound* Stream = static_cast<FStreamingSound*>(Handle);
    if (Stream == nullptr || WorkerThread == nullptr)
        return static_cast<GENRESULT>(0);
    Stream->Pan = Pan;
    if (Stream->Buffer != nullptr)
        Stream->Buffer->SetPan(Pan);
    return static_cast<GENRESULT>(1);
}

GENRESULT DACOM_API FSoundStreamer::GetPan(void* Handle, long* Out)
{
    FStreamingSound* Stream = static_cast<FStreamingSound*>(Handle);
    if (Stream == nullptr || WorkerThread == nullptr)
        return static_cast<GENRESULT>(0);
    *Out = QueryBufferPan(Stream);
    return static_cast<GENRESULT>(1);
}

BOOL WINAPI DllMain(HINSTANCE /*Instance*/, DWORD Reason, LPVOID /*Reserved*/)
{
    if (Reason == DLL_PROCESS_ATTACH)
    {
        // FUN_06ef3890: DACOM_Acquire has no null check. No DisableThreadLibraryCalls.
        FSoundStreamerFactory* Factory = new (std::nothrow) FSoundStreamerFactory();
        if (Factory == nullptr)
            return TRUE;
        ICOManager* Dacom = DACOM_Acquire();
        Dacom->RegisterComponent(Factory, kComponentName, reinterpret_cast<void*>(kRegisterPriority));
        Factory->Release();
    }
    else if (Reason == DLL_PROCESS_DETACH)
    {
        if (G_Streamer != nullptr)
        {
            G_Streamer->ShutdownDevice();
            G_Streamer = nullptr;
        }
    }
    return TRUE;
}
