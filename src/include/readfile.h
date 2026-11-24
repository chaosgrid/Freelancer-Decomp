// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Public ABI of ReadFile.dll -- the DACOM FileSystem component (DOS / UTF / MEM)
// and SearchPath. Every module that touches game data calls through this; it is
// declared here and nowhere else (docs/re/cross_dll_protocol.md).

#include "dacom.h"

#include <cstddef>

// Digital Anvil FileSystem AGGDESC (size 0x30 or 0x34). DALib::OpenData / ReadFile.dll.c.
struct FFileSystemDesc
{
    unsigned long Size;              // +0x00  0x30 or 0x34
    const char* ComponentName;       // +0x04  "FileSystem"
    const char* Implementation;      // +0x08  "DOS" / "UTF" / "MEM"
    const char* Path;                // +0x0C
    unsigned long Access;            // +0x10  GENERIC_READ 0x80000000
    unsigned long ShareMode;         // +0x14  FILE_SHARE_READ
    void* Security;                  // +0x18
    unsigned long Creation;          // +0x1C  OPEN_EXISTING 3
    unsigned long Flags;             // +0x20  0x08000080
    void* TemplateFile;              // +0x24
    IDAComponent* Parent;            // +0x28
    unsigned long Extra;             // +0x2C
    unsigned long FindHandle;           // +0x30  0xFFFFFFFF when Size==0x34
    void* MemBuffer;                 // +0x34  MEM size 0x40 only (FUN_06b777f0)
    unsigned long MemSize;           // +0x38
    unsigned long MemFlags;          // +0x3C  bit0=wrap pointer, bit1=flag+1
};

// Callers fill `Size` (0x30 / 0x34 for DOS and UTF, 0x40 for MEM) and the callee reads
// only that much, so the DOS-sized prefix layout is the invariant that matters.
static_assert(offsetof(FFileSystemDesc, FindHandle) == 0x30, "DOS/UTF desc prefix is 0x34");
static_assert(sizeof(FFileSystemDesc) == 0x40, "MEM tail extends the desc to 0x40");

// File-system instances also expose their component factory. Slots +0x10..+0x78 follow the
// Win32 file API one for one, in winbase.h order and with its argument counts (the MEM table
// 06b792f0 confirms each RET size): CloseHandle, ReadFile, WriteFile, GetOverlappedResult,
// SetFilePointer, SetEndOfFile, GetFileSize, LockFile, UnlockFile, Get/SetFileTime, the mapping
// trio, FindFirst/Next/Close, Create/RemoveDirectory, Get/SetCurrentDirectory (OpenNode),
// DeleteFile, CopyFile, MoveFile, Get/SetFileAttributes, GetLastError. The names avoid the
// Win32 A/W macros.
struct IFileSystem : IComponentFactory
{
    virtual int DACOM_API CloseFile(unsigned long Handle) = 0;                          // +0x10
    virtual int DACOM_API Read(unsigned long Handle, void* Buffer, unsigned long Size,
                               unsigned long* BytesRead, unsigned long Extra) = 0;      // +0x14
    virtual int DACOM_API Write(unsigned long Handle, const void* Buffer, unsigned long Size,
                                 unsigned long* BytesWritten, const void* PositionOverride) = 0; // +0x18
    virtual int DACOM_API GetOverlappedResult(unsigned long Handle, unsigned long Overlapped,
                                 unsigned long BytesTransferredOut, unsigned long bWait) = 0; // +0x1C
    virtual int DACOM_API SeekFile(unsigned long Handle, unsigned long OffLow, unsigned long* OffHighOut,
                                  unsigned long Origin) = 0; // +0x20
    virtual int DACOM_API SetEndOfFile(unsigned long Handle) = 0; // +0x24
    virtual unsigned long DACOM_API GetSize(unsigned long Handle, unsigned long* HighOut) = 0; // +0x28
    virtual int DACOM_API LockFileRegion(unsigned long Handle, unsigned long OffLow, unsigned long OffHigh,
                                 unsigned long CountLow, unsigned long CountHigh) = 0; // +0x2C
    virtual int DACOM_API UnlockFileRegion(unsigned long Handle, unsigned long OffLow, unsigned long OffHigh,
                                 unsigned long CountLow, unsigned long CountHigh) = 0; // +0x30
    virtual int DACOM_API GetFileTimes(unsigned long Handle, FILETIME* CreationOut, FILETIME* AccessOut,
                                  FILETIME* WriteOut) = 0; // +0x34
    virtual int DACOM_API SetFileTimes(unsigned long Handle, unsigned long Creation, unsigned long Access,
                                 unsigned long Write) = 0; // +0x38
    virtual int DACOM_API CreateMapping(unsigned long Handle, unsigned long Security, unsigned long Protect,
                                 unsigned long MaxSizeHigh, unsigned long MaxSizeLow,
                                 unsigned long Name) = 0; // +0x3C
    virtual int DACOM_API MapFileView(unsigned long Mapping, unsigned long Access, unsigned long OffHigh,
                                 unsigned long OffLow, unsigned long ByteCount) = 0; // +0x40
    virtual int DACOM_API UnmapFileView(unsigned long View) = 0; // +0x44
    virtual int DACOM_API FindFirst(const char* Pattern, WIN32_FIND_DATAA* FindData) = 0; // +0x48
    virtual int DACOM_API FindNext(unsigned long Handle, WIN32_FIND_DATAA* FindData) = 0; // +0x4C
    virtual int DACOM_API FindClose(unsigned long Handle) = 0; // +0x50
    // +0x54: alchemy's library savers call it with (name, 0) before OpenNode on that name.
    virtual int DACOM_API CreateNode(const char* Name, unsigned long Security) = 0; // +0x54
    virtual int DACOM_API RemoveNode(unsigned long Name) = 0; // +0x58
    virtual int DACOM_API GetCurrentPath(unsigned long MaxChars, char* Out) = 0;        // +0x5C
    virtual int DACOM_API OpenNode(const char* Name) = 0;                               // +0x60
    virtual int DACOM_API RemoveFile(unsigned long Name) = 0; // +0x64
    virtual int DACOM_API CopyFileTo(unsigned long ExistingName, unsigned long NewName,
                                 unsigned long bFailIfExists) = 0; // +0x68
    virtual int DACOM_API MoveFileTo(unsigned long ExistingName, unsigned long NewName) = 0; // +0x6C
    virtual int DACOM_API GetPathAttributes(const char* Path) = 0; // +0x70
    virtual int DACOM_API SetPathAttributes(unsigned long Name, unsigned long Attributes) = 0; // +0x74
    virtual int DACOM_API GetFileSystemError() = 0; // +0x78
    virtual int DACOM_API OpenByDesc(FFileSystemDesc* Desc) = 0;                        // +0x7C
    virtual int DACOM_API GetFilePosition(unsigned long Handle, unsigned long* HighOut) = 0; // +0x80
    virtual void DACOM_API GetFileSysPath(char* Out, unsigned long MaxChars) = 0;       // +0x84
    virtual int DACOM_API GetAccessFlags() = 0; // +0x88
    virtual int DACOM_API GetParentFileSystem(IFileSystem** Out) = 0; // +0x8C
    virtual int DACOM_API FailOperation90(unsigned long A1, unsigned long A2) = 0; // +0x90 TODO: original operation contract.
    virtual int DACOM_API FailOperation94(unsigned long A1, unsigned long A2) = 0; // +0x94 TODO: original operation contract.
    virtual int DACOM_API FailOperation98(unsigned long A1, unsigned long A2, unsigned long A3,
                                 unsigned long A4, unsigned long A5) = 0; // +0x98 TODO: original operation contract.
    virtual int DACOM_API FailOperation9C(unsigned long A1, unsigned long A2, unsigned long A3,
                                 unsigned long A4, unsigned long A5) = 0; // +0x9C TODO: original operation contract.
    // +0xA0: Callback(Arg0, Arg1) under the instance lock (DOS: its critical section).
    virtual int DACOM_API InvokeLockedCallback(unsigned long Arg0, unsigned long Callback,
                                 unsigned long Arg1) = 0; // +0xA0
    virtual int DACOM_API ResolvePath(char* Dest, const char* Src,
                                 unsigned long DestChars) = 0; // +0xA4
};

// Access to a memory file system's underlying buffer.
struct IDataBuffer : IDAComponent
{
    virtual GENRESULT DACOM_API GetBuffer(void** OutPtr, unsigned long* OutSize) = 0; // +0x0C
};

struct ISearchPath : IComponentFactory
{
    virtual int DACOM_API SetPath(const char* PathList) = 0;                            // +0x10
    virtual int DACOM_API GetPath(char* Dest, unsigned long MaxChars) = 0; // +0x14
};
