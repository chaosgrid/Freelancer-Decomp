// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The severity word FDUMP takes as its first argument. dacom.dll formats the message from it
// ("%s(%d) : ERROR:General:%s", "... WARNING ...", "... NOTICE ..."); the low word is the
// severity, 0x100000 the "General" category every caller in the game uses.
enum EFdumpSeverity : int
{
    kFdumpFatal   = 0x100000,
    kFdumpError   = 0x100001,
    kFdumpWarning = 0x100002,
    kFdumpNotice  = 0x100003,
    kFdumpTrace1  = 0x100004,
};
