// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// server_savefile.cpp -- the ini-writer object and the "FLS1" container.
// Vtable and layout evidence is in src/include/server_savefile.h.

#include "../include/server_savefile.h"

#include <windows.h>
#include <cstring>

// .rdata:DAT_06d6b30c. Retail passes its address to fprintf as the bare format
// string: four literal characters, no trailing newline.
char const Server_FLS1Magic[5] = "FLS1";

// .rdata:06d6b318. The encoder indexes it with `i & 3`, so only "Gene" is ever
// reachable; the whole literal is kept as the real datum.
static char const* const FLS1KeyString =
    "Genesis - In the beginning Yahweh created the heavens and the ea";

void Server_FLS1Transform(char* Data, int Length) {
    // Retail's length tests are signed, so a negative or zero length does nothing;
    // hence `int`, not size_t.
    if (Length <= 0)
        return;

    for (int I = 0; I < Length; ++I) {
        unsigned char const KeyByte =
            static_cast<unsigned char>(FLS1KeyString[I & 3]);
        // 8-bit add that wraps; the forced top bit keeps the mask non-zero.
        unsigned char const Mask = static_cast<unsigned char>(
            static_cast<unsigned char>(KeyByte + static_cast<unsigned char>(I)) | 0x80u);
        Data[I] = static_cast<char>(static_cast<unsigned char>(Data[I]) ^ Mask);
    }
}

// ---------------------------------------------------------------------------
// SaveGame -- the { FILE*, std::string } pair (.?AUSaveGame@@).
// ---------------------------------------------------------------------------

SaveGame::SaveGame()
    : File(nullptr), Contents() {
    // FUN_06d4eac0.
}

void SaveGame::CloseFile() {
    // FUN_06d4eae0.
    if (File != nullptr) {
        fclose(File);
        File = nullptr;
    }
}

void SaveGame::ReadTail(long Offset) {
    // FUN_06d48960. The two resizes are MSVCP60's resize inlined.
    fseek(File, 0, SEEK_END);
    long const Size = ftell(File) - Offset;
    fseek(File, Offset, SEEK_SET);
    Contents.resize(static_cast<size_t>(Size));
    size_t const Read = fread(&Contents[0], 1, static_cast<size_t>(Size), File);
    Contents.resize(Read);
}

void SaveGame::Encode() {
    // FUN_06d48b60. Retail's COW unshare/freeze is not needed with a non-COW
    // std::string; when empty, the signed length test stops before any access.
    int const Length = static_cast<int>(Contents.size());
    Server_FLS1Transform(&Contents[0], Length);
}

// ---------------------------------------------------------------------------
// SaveGame_Writer
// ---------------------------------------------------------------------------

SaveGame_Writer::SaveGame_Writer()
    : SaveGame(), ISave(), Path() {
    // ISave vptr 06d6b5c0, concrete vptr 06d6b5a0. The path is set in Open().
}

SaveGame_Writer::~SaveGame_Writer() {
    // FUN_06d4c310: if the FILE* is still set, the same encode-and-rewrite as
    // FUN_06d4eb00 (which the inlined copy on PlayerData_SerializeToFile's
    // open-failure exit calls, .text:06d4cfcd).
    Close();
}

bool SaveGame_Writer::Open(char const* InPath) {
    // No null test on InPath; all callers pass a local buffer.
    Path = InPath;
    // "w+t" (.rdata:DAT_06d6b598; the listing truncates it to "w"). The "+" lets
    // Close() read the file back through this handle; the "t" means the CRLFs the
    // text write produces collapse on read-back, so the encoded bytes are LF-only --
    // as real retail saves are (newplayer.fl, encoded offline, has CRLF).
    File = fopen(InPath, "w+t");
    return File != nullptr;
}

bool SaveGame_Writer::HadWriteError() const {
    // Retail inlines ferror() as a test of _IOERR (0x20) in FILE::_flag (+0x0c).
    // No null test; the only caller reaches it after a successful open.
    return ferror(File) != 0;
}

void SaveGame_Writer::Close() {
    // FUN_06d4eb00.
    if (File == nullptr)
        return;

    // FUN_06d48960 with Offset 0, inlined in retail; ftell's result is not tested.
    // In text mode fread returns fewer bytes than ftell by the CRs swallowed, and the
    // second resize trims to that.
    ReadTail(0);

    Encode();

    CloseFile();

    // Reopen the same path "wb" (.rdata:DAT_06d6b4e4). Retail does not check this
    // fopen, and the fwrite is unconditional.
    RewriteEncoded();
}

void SaveGame_Writer::ReadBackAndEncode() {
    // FUN_06d53b90: FUN_06d48960(0) and FUN_06d48b60 inlined.
    if (File == nullptr)
        return;

    fseek(File, 0, SEEK_END);
    long const Size = ftell(File);
    fseek(File, 0, SEEK_SET);

    Contents.resize(static_cast<size_t>(Size));
    size_t const Got = fread(&Contents[0], 1, static_cast<size_t>(Size), File);
    // Text mode ("w+t"): Got is short of Size by the CRs dropped.
    Contents.resize(Got);

    Server_FLS1Transform(&Contents[0], static_cast<int>(Contents.size()));
}

void SaveGame_Writer::RewriteEncoded() {
    // PlayerDB::create_restart_file .text:06d5398f (and its copy at .text:06d53a37).
    File = fopen(Path.c_str(), "wb");
    fprintf(File, Server_FLS1Magic);                      // magic as the format
    fwrite(Contents.data(), 1, Contents.size(), File);
    fclose(File);
    File = nullptr;
}

FILE* SaveGame_Writer::GetFile() const {
    // slot +0x00, .text:06ce10f0.
    return File;
}

void SaveGame_Writer::WriteSection(char const* Name) {
    // slot +0x04, .text:06d4c260. None of the six writer slots (+0x04 .. +0x18) tests
    // the FILE*: both callers only write after a successful Open (FUN_06d4ccd0,
    // FUN_06d4b790).
    fprintf(File, "\n[%s]\n", Name);
}

void SaveGame_Writer::WriteFloat(char const* Key, float Value) {
    // slot +0x08, .text:06d4c2e0. Widened to double for %g.
    fprintf(File, "%s = %g\n", Key, static_cast<double>(Value));
}

void SaveGame_Writer::WriteUInt(char const* Key, unsigned int Value) {
    // slot +0x0c, .text:06d4c2c0.
    fprintf(File, "%s = %u\n", Key, Value);
}

void SaveGame_Writer::WriteInt(char const* Key, int Value) {
    // slot +0x10, .text:06d4c2a0.
    fprintf(File, "%s = %d\n", Key, Value);
}

void SaveGame_Writer::WriteString(char const* Key, char const* Value) {
    // slot +0x14, .text:06d4c280; the slot Server_WriteCostume dispatches.
    fprintf(File, "%s = %s\n", Key, Value);
}

void SaveGame_Writer::WriteLine(char const* Text) {
    // slot +0x18, .text:06d4c230. Retail passes the caller's string as the format
    // string (no "%s"), skipped when null, then a newline.
    if (Text != nullptr)
        fprintf(File, Text);
    fprintf(File, "\n");
}
