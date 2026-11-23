// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail ServerFilter (RTTI .?AVServerFilter@@, vftable 0x005e20a8, one slot: Match
// FUN_00562990). The multiplayer server browser's filter: PageServerSelect keeps one at +0x2a4
// (its Create resets the version range), hands it to CLoginMgr::GetFilteredServers (which calls
// Match on every server) and to the ServerFilterDialog, which edits a copy and assigns it back.
//
// Every criterion has an enable byte (+0x04..+0x09) and a value; a server passes when every
// enabled criterion matches. Retail has no destructor for it.

struct FServerInfo;

class ServerFilter
{
public:
    // FUN_005628e0.
    ServerFilter();

    // FUN_00562930 (RET 4) -- every field from +0x04 on; the vftable stays.
    ServerFilter& operator=(ServerFilter const& Other);

    // slot 0 FUN_00562990 (RET 4) -- false as soon as one enabled criterion fails. The ping and
    // version ranges compare unsigned.
    virtual bool Match(FServerInfo const& Server) const;

    unsigned char bFilterPing;      // +0x04
    unsigned char bFilterKnown;     // +0x05
    unsigned char bFilterVersion;   // +0x06
    unsigned char bFilterAllowNewPlayers;   // +0x07
    unsigned char bFilterPassword;  // +0x08
    unsigned char bFilterLan;   // +0x09
    unsigned PingMin;               // +0x0c -- 0
    unsigned PingMax;               // +0x10 -- 1000
    unsigned char KnownLogin;       // +0x14 -- 1
    unsigned VersionMin;            // +0x18 -- DAT_0067ecb8
    unsigned VersionMax;            // +0x1c -- DAT_0067ecb8
    unsigned char AllowNewPlayers;          // +0x20 -- 1
    unsigned char Password;         // +0x21 -- 0
    unsigned char Lan;          // +0x22 -- 1
};
