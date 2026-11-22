// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "server_filter.h"

#include "globals.h"   // g_client.packed_version (DAT_0067ecb8)
#include "login_mgr.h" // FServerInfo, G_LoginMgr (DAT_0067e7b8)

// FUN_005628e0.
ServerFilter::ServerFilter()
    : bFilterPing(0)
    , bFilterKnown(0)
    , bFilterVersion(0)
    , bFilterAllowNewPlayers(0)
    , bFilterPassword(0)
    , bFilterLan(0)
    , PingMin(0)
    , PingMax(1000)
    , KnownLogin(1)
    , VersionMin(g_client.packed_version)
    , VersionMax(g_client.packed_version)
    , AllowNewPlayers(1)
    , Password(0)
    , Lan(1)
{
}

// FUN_00562930.
ServerFilter& ServerFilter::operator=(ServerFilter const& Other)
{
    bFilterPing = Other.bFilterPing;
    bFilterKnown = Other.bFilterKnown;
    bFilterVersion = Other.bFilterVersion;
    bFilterAllowNewPlayers = Other.bFilterAllowNewPlayers;
    bFilterPassword = Other.bFilterPassword;
    bFilterLan = Other.bFilterLan;
    PingMin = Other.PingMin;
    PingMax = Other.PingMax;
    KnownLogin = Other.KnownLogin;
    VersionMin = Other.VersionMin;
    VersionMax = Other.VersionMax;
    AllowNewPlayers = Other.AllowNewPlayers;
    Password = Other.Password;
    Lan = Other.Lan;
    return *this;
}

// FUN_00562990.
bool ServerFilter::Match(FServerInfo const& Server) const
{
    // 0x0056299f..0x005629aa: JA past the maximum, JNC from the minimum up.
    if (bFilterPing)
    {
        unsigned const Ping = static_cast<unsigned>(Server.Ping);
        if (Ping > PingMax || Ping < PingMin)
            return false;
    }
    // 0x005629ba..0x005629c8: the known-login byte compared with +0x14.
    if (bFilterKnown)
    {
        unsigned char const bKnown = G_LoginMgr.IsKnownLogin(Server) ? 1 : 0;
        if (bKnown != KnownLogin)
            return false;
    }
    if (bFilterVersion)
    {
        if (Server.Version > VersionMax || Server.Version < VersionMin)
            return false;
    }
    if (bFilterAllowNewPlayers && Server.bAllowNewPlayers != AllowNewPlayers)
        return false;
    if (bFilterPassword && Server.bPassword != Password)
        return false;
    if (bFilterLan && Server.bLan != Lan)
        return false;
    return true;
}
