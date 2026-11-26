// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Network/login_mgr.h"
#include "msvcrt_compat.h" // MsvcrtAtoi/MsvcrtWtoi: retail MSVCRT.DLL wraps mod 2^32, the UCRT saturates

#include "Interface/Pages/character_select_equip.h" // ApplyCharacterSelectEquipFields (FUN_005ab250 tail)
#include "common_ini.h"
#include "common_misc.h"   // ParseGUID
#include "common_server.h" // IServer
#include "dacom.h"         // FDUMP
#include "Core/globals.h"       // g_client.packed_version (DAT_0067ecb8)
#include "Core/perf_options.h"  // GetPerfOptionSlot (FUN_005b3510)
#include "Space/player_spawn.h"  // DAT_00673344
#include "Interface/server_filter.h" // ServerFilter::Match (GetFilteredServers)
#include "Core/reg_key_stack.h" // FRegKeyStack (login history)
#include "Common/vc6_sort.h"      // Vc6Sort::Sort (GetFilteredServers' inlined VC6 std::sort)

#include <algorithm>
#include <malloc.h>        // _alloca (ReadRegHexString)
#include <mmsystem.h>      // timeGetTime
#include <stdio.h>
#include <time.h>          // time (SaveLogin)
#include <stdlib.h>        // atoi, _wtoi, wcstombs (Read)
#include <string.h>
#include <wchar.h>

// inet_addr / ntohl (Read): declared by the winsock.h that windows.h pulls in
// (WIN32_LEAN_AND_MEAN is not defined in this build).
#pragma comment(lib, "ws2_32.lib")

// .data:0067e7b8 -- static initializer .text:005a81c0, atexit thunk LAB_005a81e0.
CLoginMgr G_LoginMgr;

bool G_bServerDisconnected = false; // DAT_0067ecbe
bool G_bServerConnected = false;    // DAT_0067ecbf

// Both 01 in the PE image; only TypeSelect's multiplayer pick writes them.
unsigned char G_bEnumerateLanHosts = 1;   // DAT_00615b18
unsigned char G_bInternetServerList = 1;  // DAT_00615b10

namespace
{
    char const kLoginMgrSource[] = "E:\\FL\\Scratch\\Source\\Client\\LoginMgr.cpp";

    // FUN_005b6640 (cdecl) -- a REG_SZ value of the top key, decoded by HexToWide into Out.
    // False when the value is missing, not REG_SZ, its size is -1, or the second read does
    // not return exactly the size the first one reported.
    bool ReadRegHexString(FRegKeyStack& Keys, std::wstring& Out, char const* Name)
    {
        DWORD Size = 0;
        DWORD Type = 0;
        if (RegQueryValueExA(Keys.Top(), Name, nullptr, &Type, nullptr, &Size) != ERROR_SUCCESS ||
            Type != REG_SZ || Size == 0xffffffffu)
        {
            return false;
        }

        BYTE* const Buffer = static_cast<BYTE*>(_alloca((Size + 3) & ~3u));
        DWORD Length = Size;
        DWORD Type2 = 0;
        LONG const Result = RegQueryValueExA(Keys.Top(), Name, nullptr, &Type2, Buffer, &Length);
        DWORD const Read = (Result == ERROR_SUCCESS && Type2 == REG_SZ) ? Length : 0xffffffffu;
        if (Read != Size)
        {
            return false;
        }

        wchar_t Wide[0x400];
        HexToWide(reinterpret_cast<char const*>(Buffer), Wide);
        Out = Wide;
        return true;
    }

    // LAB_005aaee0 (cdecl) -- ForEachFaction's callback; the walk always goes on.
    bool __cdecl PushFactionRecord(unsigned /*NicknameId*/, FNewCharFaction const* Faction,
                                   void* Context)
    {
        static_cast<std::vector<FNewCharFaction>*>(Context)->push_back(*Faction);
        return true;
    }
}

// Inline in the CLoginMgr constructor for +0x94.
FServerInfo::FServerInfo()
    : IpAddress(-1)
    , Port(0)
    , bAddressValid(0)
    , Ping(-2)
    , LastResponseTime(0)
    , EnumAttempts(0)
    , ResponseCount(0)
    , Field_0x3c(0)
    , bEnumInFlight(0)
    , bLan(0)
    , MissedPingCount(0)
    , PlayerCount(0)
    , MaxPlayers(0)
    , Version(0)
    , Id(0)
    , bPassword(0)
    , bAllowNewPlayers(1)
    , bAllowPvp(1)
    , bSessionValid(0)
{
}

// FUN_00531050 (on +0x248, the map). +0x00 is not written by the CLoginMgr constructor; the
// retail object is static, so CurrentId starts at 0.
FLoginHistory::FLoginHistory()
    : CurrentId(0)
{
}

// FUN_005b6be0
bool FLoginHistory::Contains(unsigned int Id) const
{
    return Records.find(Id) != Records.end();
}

// FUN_005b6bd0 (operator[] FUN_005ad350).
FLoginRecord& FLoginHistory::GetRecord(unsigned int Id)
{
    return Records[Id];
}

// FUN_005b6c40 -- the key pointer is the history object, i.e. &CurrentId.
void FLoginHistory::SetTime(unsigned int Time)
{
    Records[CurrentId].Time = Time;
}

// FUN_005b6c60 -- the same key as SetTime; the argument is never read.
unsigned int FLoginHistory::GetTime(unsigned int IgnoredId)
{
    (void)IgnoredId;
    return Records[CurrentId].Time;
}

// FUN_005b62a0 -- called from the CLoginMgr constructor.
bool FLoginHistory::Load()
{
    Records.clear();                                                   // FUN_005b6e80

    FRegKeyStack Keys;
    if (Keys.Open(G_FreelancerRegKey, KEY_READ) == nullptr)
    {
        return true;
    }

    // Retail starts Id from an uninitialised stack slot (only the failed-"time" path writes it),
    // so a first record without a readable "id" is keyed by stack garbage. The port starts
    // from 0.
    unsigned int Id = 0;
    for (int Index = 1; ; ++Index)
    {
        char Name[0x2000];                                             // FUN_00402180
        _snprintf(Name, 0x2000, "MP\\Logins\\Login%d", Index);

        if (Keys.Open(Name, KEY_READ) == nullptr)
        {
            break;
        }

        // REG_DWORD as is, REG_SZ through atoi, anything else (or a failed read) keeps the
        // previous Id.
        DWORD Size = 0x20;
        DWORD Type = 0;
        BYTE Data[0x20];
        if (RegQueryValueExA(Keys.Top(), "id", nullptr, &Type, Data, &Size) == ERROR_SUCCESS)
        {
            if (Type == REG_DWORD)
            {
                Id = *reinterpret_cast<unsigned int const*>(Data);
            }
            else if (Type == REG_SZ)
            {
                Id = static_cast<unsigned int>(MsvcrtAtoi(reinterpret_cast<char const*>(Data)));
            }
        }

        // Retail inserts {Id, FLoginRecord()} separately before each of the two reads.
        std::map<unsigned int, FLoginRecord>::iterator Node =
            Records.insert(std::make_pair(Id, FLoginRecord())).first;
        ReadRegHexString(Keys, Node->second.Account, "account");

        Node = Records.insert(std::make_pair(Id, FLoginRecord())).first;
        ReadRegHexString(Keys, Node->second.Password, "password");

        // Anything but REG_DWORD / REG_SZ (or a failed read) stores 0.
        Size = 0x20;
        BYTE TimeData[0x20];
        if (RegQueryValueExA(Keys.Top(), "time", nullptr, &Type, TimeData, &Size) == ERROR_SUCCESS &&
            (Type == REG_DWORD || Type == REG_SZ))
        {
            unsigned int const Time =
                Type == REG_DWORD ? *reinterpret_cast<unsigned int const*>(TimeData)
                                  : static_cast<unsigned int>(MsvcrtAtoi(reinterpret_cast<char const*>(TimeData)));
            Records[Id].Time = Time;
        }
        else
        {
            Records[Id].Time = 0;
        }

        Keys.PopClose();                                               // FUN_00568810
    }
    return true;
}

// FUN_005b67e0 -- called by CLoginMgr::SaveLogin.
bool FLoginHistory::Save()
{
    FRegKeyStack Keys;
    if (Keys.Create(G_FreelancerRegKey, 0x2001f) == nullptr ||
        Keys.Create("MP\\Logins", 0x2001f) == nullptr)
    {
        return true;
    }

    char Name[0x2000];                                                 // FUN_00402180
    int Index = 1;
    for (std::map<unsigned int, FLoginRecord>::iterator It = Records.begin(); It != Records.end();
         ++It, ++Index)
    {
        _snprintf(Name, 0x2000, "Login%d", Index);
        // A record whose key cannot be created is skipped (its index is still used up).
        if (Keys.Create(Name, 0x2001f) == nullptr)
        {
            continue;
        }

        DWORD IdValue = It->first;
        RegSetValueExA(Keys.Top(), "id", 0, REG_DWORD, reinterpret_cast<BYTE const*>(&IdValue), 4);

        char AccountHex[0x400];
        WideToHex(It->second.Account.c_str(), AccountHex);
        Keys.SetString("account", AccountHex);                         // FUN_005b6b90

        // Retail spells this REG_SZ write inline.
        char PasswordHex[0x400];
        WideToHex(It->second.Password.c_str(), PasswordHex);
        Keys.SetString("password", PasswordHex);

        DWORD TimeValue = It->second.Time;
        RegSetValueExA(Keys.Top(), "time", 0, REG_DWORD, reinterpret_cast<BYTE const*>(&TimeValue), 4);

        Keys.PopClose();
    }

    // Delete the stale LoginN keys past the last record until a delete fails.
    for (;; ++Index)
    {
        _snprintf(Name, 0x2000, "Login%d", Index);
        HKEY const Parent = Keys.Top();
        if (Parent == nullptr)
        {
            break;
        }
        if (RegDeleteKeyA(Parent, Name) != ERROR_SUCCESS)
        {
            break;
        }
    }
    return true;
}

// .text:005a8be0
CLoginMgr::CLoginMgr()
    : ConnectTimeout(90.0f)            // DAT_005e64c0
    , bConnected(0)
    , LoginName(L"SinglePlayer")    // .rdata:005e3094
    , bCreateAccount(0)
    , LoginTimeout(50000.0f)         // DAT_005e64c4
    , bLoginResponse(0)
    , LoginStatus(5)
    , Field_0x1c8(0)
    , SaveFailureCount(0)
    , CharacterInfoTimeout(3000.0f)          // DAT_005e64c8
    , bCharacterInfoArrived(0)
    , CharacterSelectTimeout(3000.0f)          // DAT_005e64cc
    , bCharacterSelectResponse(0)
    , ListServerHost("FLListServer2") // .rdata:005e650c
    , ListServerPort(0x708a)
    , Gun(nullptr)
    , bServerListChanged(0)
    , ServerListPage(0)
    , bServerListRequestPending(0)
    , InFlightDirectEnums(0)
{
    SelectCharacterName[0] = L'\0';
    Logins.Load();                  // FUN_005b62a0
    // Field_0x1c8 / SaveFailureCount: the constructor does not write them; the retail object
    // is static and therefore zero-initialised.
}

// .text:005a9440
bool CLoginMgr::SaveLogin()
{
    // Retail copies the Address and password into locals that are never read.
    std::wstring const UnusedAddress = GetCurrentServer().Address;
    std::wstring const Account = GetLoginName();                       // FUN_005a96b0
    std::wstring const UnusedPassword = GetLoginPassword();            // FUN_005a9800
    (void)UnusedAddress;
    (void)UnusedPassword;

    Logins.GetRecord(GetCurrentServer().Id);                           // result unused
    Logins.CurrentId = GetCurrentServer().Id;
    Logins.Records[Logins.CurrentId].Account = Account;
    Logins.SetTime(static_cast<unsigned int>(time(nullptr)));
    return Logins.Save();
}

// .text:005a96b0
std::wstring CLoginMgr::GetLoginName() const
{
    return LoginName;
}

// .text:005a9800
std::wstring CLoginMgr::GetLoginPassword() const
{
    return LoginPassword;
}

// .text:005684a0 (RET 4) -- FServerInfo's copy constructor inlined on +0x94.
FServerInfo CLoginMgr::GetCurrentServer() const
{
    return CurrentServer;
}

// .text:005a9040 (the atexit thunk) / .text:005a8f20 (deleting destructor). Member
// destructors and ~CDPClient follow implicitly in the retail order.
CLoginMgr::~CLoginMgr()
{
    ResetServerLists();          // FUN_005abc80
    bLoginResponse = 0;
    bCharacterInfoArrived = 0;
    bCharacterSelectResponse = 0;
}

// .text:005ab590 (Gun2::IGUNQueueRead slot 0) -- the list server's server-list reply 0x7204.
// A non-empty page asks the browser for the next one; each entry is appended to Servers when no
// entry with the same Id and +0x64 key is there yet, and copied into +0x160 when its key is
// non-empty and the matching Servers entry has +0x3e clear. Every path returns 0.
long __stdcall CLoginMgr::Read(Gun2::GUNQueueMessage* Message)
{
    Gun2::GUNMessageBody* Body = Message->Body;
    unsigned long const Type = Body->Type;
    if ((Type & 0xf00) != 0x200 || (Type & 0xff000) != 0x7000 || Type != 0x7204)
    {
        return 0;
    }

    Gun2::GUNServerEntry** Entries = nullptr;
    unsigned long EntryCount = 0;
    if (Body->bHasEntries != 0)
    {
        EntryCount = Body->EntryCount;
        Entries = Body->Entries;
    }
    if (Body->bHasEntries != 0 && EntryCount > 0)
    {
        unsigned int const Page = ServerListPage;
        ServerListPage = Page + 1;
        Gun->GetBrowser()->SendRequest7104(0x65, Page, 0, 0, 0);
    }
    else
    {
        ServerListPage = 0;
    }

    for (unsigned long Index = 0; Index < EntryCount; ++Index)
    {
        Gun2::GUNServerEntry* Entry = Entries[Index];

        // Retail spells the FServerInfo default inline.
        FServerInfo Info;
        Info.bAddressValid = 1;
        Info.MaxPlayers = 1;
        Info.bSessionValid = 2;

        Info.Name = Entry->Name;
        Info.Description = Entry->Description;
        Info.Version = static_cast<unsigned int>(MsvcrtWtoi(Entry->Version));
        Info.Address = Entry->Address;

        char AddressText[0x84];
        sprintf(AddressText, "%S", Info.Address.c_str());
        Info.IpAddress = static_cast<int>(ntohl(inet_addr(AddressText)));

        // "p1,p2,...:+0x88:password:+0x96:+0x95:+0x64:Id". Any missing ':' or an Id of -1 drops
        // the entry.
        char Record[0x400];
        wcstombs(Record, Entry->Record, sizeof(Record));
        do
        {
            char* Colon = strchr(Record, ':');
            if (Colon == nullptr)
            {
                break;
            }
            *Colon = '\0';

            // Split from the right, so Ports holds the list reversed.
            char* Comma = strrchr(Record, ',');
            while (Comma != nullptr)
            {
                *Comma = '\0';
                Info.Ports.push_back(static_cast<unsigned int>(MsvcrtAtoi(Comma + 1)));
                Comma = strrchr(Record, ',');
            }
            Info.Ports.push_back(static_cast<unsigned int>(MsvcrtAtoi(Record)));

            char* Field = Colon + 1;
            Colon = strchr(Field, ':');
            if (Colon == nullptr)
            {
                break;
            }
            *Colon = '\0';
            Info.MaxPlayers = MsvcrtAtoi(Field);

            Field = Colon + 1;
            Colon = strchr(Field, ':');
            if (Colon == nullptr)
            {
                break;
            }
            *Colon = '\0';
            Info.bPassword = static_cast<unsigned char>(MsvcrtAtoi(Field) != 0);

            Field = Colon + 1;
            Colon = strchr(Field, ':');
            if (Colon == nullptr)
            {
                break;
            }
            *Colon = '\0';
            Info.bAllowPvp = static_cast<unsigned char>(MsvcrtAtoi(Field) != 0);

            Field = Colon + 1;
            Colon = strchr(Field, ':');
            if (Colon == nullptr)
            {
                break;
            }
            *Colon = '\0';
            Info.bAllowNewPlayers = static_cast<unsigned char>(MsvcrtAtoi(Field) != 0);

            Field = Colon + 1;
            Colon = strchr(Field, ':');
            if (Colon == nullptr)
            {
                break;
            }
            *Colon = '\0';
            Info.ServerSig = Field;

            Info.Id = static_cast<unsigned int>(MsvcrtAtoi(Colon + 1));
            if (Info.Id == 0xffffffffu)
            {
                break;
            }

            // Always true here, tested all the same.
            if (Info.bAddressValid != 0 && Info.bSessionValid != 0)
            {
                bool bKnown = false;
                for (size_t ServerIndex = 0; ServerIndex < Servers.size(); ++ServerIndex)
                {
                    FServerInfo const& Server = Servers[ServerIndex];
                    if (Server.Id == Info.Id && Server.ServerSig == Info.ServerSig)
                    {
                        bKnown = true;
                        break;
                    }
                }
                if (!bKnown)
                {
                    Servers.push_back(Info);                           // FUN_005adfc0
                    bServerListChanged = 1;
                }
            }

            if (!Info.ServerSig.empty())
            {
                FServerInfo* Match = nullptr;
                for (size_t ServerIndex = 0; ServerIndex < Servers.size(); ++ServerIndex)
                {
                    FServerInfo& Server = Servers[ServerIndex];
                    if (Server.ServerSig == Info.ServerSig && Server.Id == Info.Id)
                    {
                        Match = &Server;
                        break;
                    }
                }
                if (Match != nullptr && Match->bLan == 0)
                {
                    PendingDirectServers.push_back(Info);
                }
            }
        } while (false);
    }
    return 0;
}

// .text:005accd0 (CDPClient vftable 005e64f0 slot 3, thiscall on the CDPClient sub-object).
// The reply data, when present, is "+0x96:+0x95:Version:Id:+0x64:" followed by a wide string
// that becomes +0x54; a missing ':' or an Id of -1 drops the reply.
void CLoginMgr::OnAddHost(_DPNMSG_ENUM_HOSTS_RESPONSE* Response)
{
    DPN_APPLICATION_DESC const* Desc = Response->pApplicationDescription;

    // Retail spells the FServerInfo default inline.
    FServerInfo Info;
    Info.Port = 0;
    Info.bAddressValid = 1;
    Info.Ping = static_cast<int>(Response->dwRoundTripLatencyMS);
    if (Desc->dwCurrentPlayers != 0)
    {
        Info.PlayerCount = static_cast<int>(Desc->dwCurrentPlayers - 1);
    }
    else
    {
        Info.PlayerCount = 0;
    }
    Info.MaxPlayers = static_cast<int>(Desc->dwMaxPlayers - 1);
    Info.bSessionValid = 2;
    Info.LastResponseTime = static_cast<int>(timeGetTime());
    Info.Version = 0;
    if (Response->pvUserContext == nullptr)
    {
        Info.bLan = 1;
    }
    Info.Name = Desc->pwszSessionName;

    // The data type goes in and out through the same dword for both lookups. The HRESULTs are
    // not read.
    wchar_t HostName[0x400];
    DWORD Size = sizeof(HostName);
    DWORD DataType = DPNA_DATATYPE_STRING;
    Response->pAddressSender->GetComponentByName(L"hostname", HostName, &Size, &DataType);
    Info.Address = HostName;

    char AddressText[0x80];
    sprintf(AddressText, "%S", Info.Address.c_str());
    Info.IpAddress = static_cast<int>(ntohl(inet_addr(AddressText)));

    DWORD PortValue = 0;
    Size = sizeof(PortValue);
    Response->pAddressSender->GetComponentByName(L"port", &PortValue, &Size, &DataType);
    Info.Port = PortValue;

    // DPNSESSION_REQUIREPASSWORD (0x80).
    Info.bPassword = static_cast<unsigned char>((Desc->dwFlags >> 7) & 1);

    char* Data = static_cast<char*>(Response->pvResponseData);
    if (Data != nullptr)
    {
        char* Colon = strchr(Data, ':');
        if (Colon == nullptr)
        {
            return;
        }
        *Colon = '\0';
        Info.bAllowPvp = static_cast<unsigned char>(MsvcrtAtoi(Data) != 0);

        char* Field = Colon + 1;
        Colon = strchr(Field, ':');
        if (Colon == nullptr)
        {
            return;
        }
        *Colon = '\0';
        Info.bAllowNewPlayers = static_cast<unsigned char>(MsvcrtAtoi(Field) != 0);

        Field = Colon + 1;
        Colon = strchr(Field, ':');
        if (Colon == nullptr)
        {
            return;
        }
        *Colon = '\0';
        Info.Version = static_cast<unsigned int>(MsvcrtAtoi(Field));

        Field = Colon + 1;
        Colon = strchr(Field, ':');
        if (Colon == nullptr)
        {
            return;
        }
        *Colon = '\0';
        Info.Id = static_cast<unsigned int>(MsvcrtAtoi(Field));

        Field = Colon + 1;
        Colon = strchr(Field, ':');
        if (Colon == nullptr)
        {
            return;
        }
        *Colon = '\0';
        Info.ServerSig = Field;
        Info.Description = reinterpret_cast<wchar_t const*>(Colon + 1);
    }

    if (Info.Id == 0xffffffffu)
    {
        return;
    }

    RemovePendingDirect(Info);                                         // FUN_005ac010
    RemoveRetryDirect(Info);                                           // FUN_005abde0

    for (size_t ServerIndex = 0; ServerIndex < Servers.size(); ++ServerIndex)
    {
        FServerInfo& Server = Servers[ServerIndex];
        if (Server.ServerSig == Info.ServerSig && Server.Id == Info.Id)
        {
            // Refresh the known entry. Address, Port, +0x10, +0x64 and Id are left as they are.
            Server.bAllowPvp = Info.bAllowPvp;
            Server.bAllowNewPlayers = Info.bAllowNewPlayers;
            Server.bPassword = Info.bPassword;
            Server.Description = Info.Description;
            Server.Name = Info.Name;
            Server.PlayerCount = Info.PlayerCount;
            Server.MaxPlayers = Info.MaxPlayers;
            Server.ResponseCount = Server.ResponseCount + 1;
            // Unsigned minimum.
            if (static_cast<unsigned int>(Info.Ping) < static_cast<unsigned int>(Server.Ping))
            {
                Server.Ping = Info.Ping;
            }
            Server.MissedPingCount = 0;
            Server.LastResponseTime = Info.LastResponseTime;
            Server.Version = Info.Version;
            Server.bLan = Info.bLan;
            bServerListChanged = 1;
            Server.bEnumInFlight = 0;
            return;
        }
    }

    // Always true here, tested all the same; the second search, with the terms the other way
    // round, cannot find what the first one missed.
    if (Info.bAddressValid != 0 && Info.bSessionValid != 0)
    {
        for (size_t ServerIndex = 0; ServerIndex < Servers.size(); ++ServerIndex)
        {
            FServerInfo const& Server = Servers[ServerIndex];
            if (Server.Id == Info.Id && Server.ServerSig == Info.ServerSig)
            {
                return;
            }
        }
        Servers.push_back(Info);                                       // FUN_005ad6a0
        bServerListChanged = 1;
    }
}

// .text:005a9990
int CLoginMgr::Connect(bool bDisconnectFirst)
{
    // Retail builds and destroys unused copies of Name and Address here.
    if (bDisconnectFirst && bConnected && GetPlayerConnectionId() != 0)
    {
        G_Server->DisConnect(GetPlayerConnectionId(), EFLConnection::NONE); // +0x40
        G_bServerDisconnected = false;
        G_bServerConnected = false;
        bConnected = 0;
        bLoginResponse = 0;
        bCharacterInfoArrived = 0;
        Characters.free();          // FUN_005ae7a0
        bCharacterSelectResponse = 0;
    }

    bConnected = 0;

    char Address[0x400];
    if (SinglePlayer())
    {
        FDUMP(kFdumpNotice, "%s(%d) : NOTICE:General:%s", kLoginMgrSource, 0x15f,
              "Starting single player game.\n");
        SetupSinglePlayer();        // FUN_005aa210; its result is not read
        sprintf(Address, "%s", "localpass");
    }
    else
    {
        char Notice[0x2000];
        sprintf(Notice, "MP Connect: Server %S, IP %S\n", CurrentServer.Name.c_str(),
                CurrentServer.Address.c_str());
        FDUMP(kFdumpNotice, "%s(%d) : NOTICE:General:%s", kLoginMgrSource, 0x165, Notice);

        char* Cursor = Address + sprintf(Address, "tcpip, %S:", CurrentServer.Address.c_str());
        if (CurrentServer.Port != 0)
        {
            sprintf(Cursor, "%u", CurrentServer.Port);
        }
        else
        {
            std::vector<unsigned int>::const_iterator It = CurrentServer.Ports.begin();
            while (It != CurrentServer.Ports.end())
            {
                Cursor += sprintf(Cursor, "%u", *It);
                ++It;
                if (It == CurrentServer.Ports.end())
                {
                    break;
                }
                Cursor[0] = ',';
                Cursor[1] = '\0';
                ++Cursor;
            }
        }
    }

    // A null pointer when the password is empty, not L"".
    unsigned short* Password = nullptr;
    if (!ServerPassword.empty())
    {
        Password = reinterpret_cast<unsigned short*>(const_cast<wchar_t*>(ServerPassword.c_str()));
    }
    G_Server->Connect(Address, Password); // +0x3c

    return WaitForServerResponse(&bConnected, 5.0f, 60.0f);
}

// .text:005a9c10 -- IClient slot 11 (LAB_005a81f0) after it sets DAT_0067ecbf.
void CLoginMgr::OnConnectResponse(unsigned int ClientId)
{
    SetPlayerConnectionId(ClientId);
    bConnected = 1;
}

// .text:005aa210
bool CLoginMgr::SetupSinglePlayer()
{
    if (bConnected)
    {
        return false;
    }

    CurrentServer.Address = L"localpass";          // .rdata:005e65b8
    CurrentServer.IpAddress = -1;
    CurrentServer.bAddressValid = 1;
    CurrentServer.Ping = 0;
    CurrentServer.PlayerCount = 0;
    CurrentServer.MaxPlayers = 1;
    CurrentServer.bSessionValid = 2;
    CurrentServer.Version = g_client.packed_version; // DAT_0067ecb8
    CurrentServer.Name = L"";                       // .rdata:005c8ce0
    CurrentServer.Description = L"";
    CurrentServer.Field_0x74 = "";                  // .rdata:005c865c

    ResetServerLists(); // FUN_005abc80 inlined
    return true;
}

// .text:005aa440
bool CLoginMgr::Logout()
{
    if (GetPlayerConnectionId() != 0)
    {
        G_Server->DisConnect(GetPlayerConnectionId(), EFLConnection::NONE); // +0x40
        G_bServerDisconnected = false;
        G_bServerConnected = false;
        bConnected = 0;
        bLoginResponse = 0;
        bCharacterInfoArrived = 0;
        Characters.free();          // inlined OwnerList::free
        bCharacterSelectResponse = 0;
    }
    return true;
}

// .text:005aa510
int CLoginMgr::Login()
{
    if (!bConnected)
    {
        return 0;
    }

    bLoginResponse = 0;

    SLoginInfo Info = {};
    Info.CreateFlag = bCreateAccount;                      // byte at +0xc0
    wcsncpy(Info.AccountName, LoginName.c_str(), 0x28);
    wcsncpy(Info.Password, LoginPassword.c_str(), 0x10);
    Info.AccountName[0x27] = L'\0';                        // word at +0x4e
    Info.Password[0x0f] = L'\0';                         // word at +0xbe
    G_Server->Login(Info, GetPlayerConnectionId());        // +0x48

    int const Result = WaitForServerResponse(&bLoginResponse, 5.0f, 60.0f);
    if (Result != 1 || LoginStatus != kLoginAccepted)
    {
        return 0;
    }

    if (!SinglePlayer())
    {
        SaveLogin();                // FUN_005a9440, result ignored
        return Result;
    }

    if (G_Server == nullptr)
    {
        return Result;
    }

    // +0x15c ReqDifficultyScale: 0 under "+G", else perf-option slot 6.
    float const Scale = G_bCommandLineG ? 0.0f : GetPerfOptionSlot(kPerfSlotDifficultyScale);
    G_Server->ReqDifficultyScale(Scale, GetPlayerConnectionId());
    return Result;
}

// .text:005aa660 -- IClient slot 12 (LAB_005a8210 jumps here with both arguments).
void CLoginMgr::OnLoginResponse(unsigned int Unused, unsigned char const* Status)
{
    (void)Unused;
    LoginStatus = *Status;
    bLoginResponse = 1;
}

// .text:005aa680
int CLoginMgr::RequestCharacterInfo()
{
    if (G_Server == nullptr)
    {
        return 0;
    }

    bCharacterInfoArrived = 0;
    Characters.free();              // inlined OwnerList::free
    G_Server->CharacterInfoReq(GetPlayerConnectionId(), false); // +0x4c
    return WaitForServerResponse(&bCharacterInfoArrived, 5.0f, 60.0f);
}

// .text:005aa8f0 (RET 4).
int CLoginMgr::DestroyCharacter(CHARACTER_ID const& CharId)
{
    if (G_Server == nullptr)
    {
        return 0;
    }

    bCharacterInfoArrived = 0;
    Characters.free();
    G_Server->DestroyCharacter(CharId, GetPlayerConnectionId()); // +0x5c
    return WaitForServerResponse(&bCharacterInfoArrived, 5.0f, 60.0f);
}

// .text:005aa750 (RET 4).
int CLoginMgr::CreateNewCharacter(SCreateCharacterInfo const& Info)
{
    if (G_Server == nullptr)
    {
        return 0;
    }

    bCharacterInfoArrived = 0;
    Characters.free();
    G_Server->CreateNewCharacter(Info, GetPlayerConnectionId()); // +0x58
    int Result = WaitForServerResponse(&bCharacterInfoArrived, 5.0f, 60.0f);

    // No answer, or an answer that brought characters -> the wait's result.
    if (bCharacterInfoArrived == 0 || Characters.get_head() != nullptr)
    {
        return Result;
    }

    if (G_Server == nullptr)
    {
        return 0;
    }

    // RequestCharacterInfo's body inlined; its success becomes 4 (refused).
    bCharacterInfoArrived = 0;
    Characters.free();
    G_Server->CharacterInfoReq(GetPlayerConnectionId(), false);
    Result = WaitForServerResponse(&bCharacterInfoArrived, 5.0f, 60.0f);
    if (Result == 1)
    {
        Result = 4;
    }
    return Result;
}

// .text:005aaca0 (RET 4).
CCharacterSet::CharacterInfo* CLoginMgr::CharacterAt(int Index)
{
    MetaNode<CCharacterSet::CharacterInfo>* Node = nullptr;
    int Count = 0;
    while (Characters.traverse(Node, true))
    {
        if (Count == Index)
        {
            return Node->Data;
        }
        Count = Count + 1;
    }
    return nullptr;
}

// .text:005a9300 -- FUN_005abc80's body, then three flags.
void CLoginMgr::Reset()
{
    ResetServerLists();
    bLoginResponse = 0;
    bCharacterInfoArrived = 0;
    bCharacterSelectResponse = 0;
}

// .text:005abc80
void CLoginMgr::ResetServerLists()
{
    CancelEnums();
    Servers.clear();                // FUN_005ad910
    PendingDirectServers.clear();
    RetryDirectServers.clear();
    InFlightDirectEnums = 0;

    // +0x3d is cleared on the source entry before it is copied.
    for (size_t Index = 0; Index < DirectServers.size(); ++Index)
    {
        DirectServers[Index].bEnumInFlight = 0;
        PendingDirectServers.push_back(DirectServers[Index]);
    }
}

// .text:005ac710 (thiscall, RET 4).
bool CLoginMgr::AddDirectServer(char const* HostPort)
{
    if (HostPort == nullptr)
        return false;
    char* const Copy = _strdup(HostPort);
    if (Copy == nullptr)
        return false;

    // No ':' still returns true.
    char* const Colon = strrchr(Copy, ':');
    if (Colon != nullptr)
    {
        *Colon = '\0';
        char const* const PortText = Colon + 1;
        wchar_t Host[0x400];
        mbstowcs(Host, Copy, 0x400);

        FServerInfo Entry;
        Entry.Address.assign(Host, wcslen(Host));
        Entry.IpAddress = static_cast<int>(ntohl(inet_addr(Copy)));
        unsigned const Port = static_cast<unsigned>(MsvcrtAtoi(PortText));
        Entry.Ports.push_back(Port);
        DirectServers.push_back(Entry);
    }

    free(Copy);
    return true;
}

// .text:005abc40
int CLoginMgr::GetGunStatus()
{
    if (Gun == nullptr)
    {
        return 3;
    }
    return static_cast<int>(Gun->GetStatus());
}

// .text:005abc60 -- vtable +4 with 1 is CGunWrapper's deleting destructor.
void CLoginMgr::DestroyGun()
{
    if (Gun != nullptr)
    {
        delete Gun;
        Gun = nullptr;
    }
}

// .text:005abdb0
FServerInfo* CLoginMgr::FindServer(unsigned int Id)
{
    for (size_t Index = 0; Index < Servers.size(); ++Index)
    {
        if (Servers[Index].Id == Id)
        {
            return &Servers[Index];
        }
    }
    return nullptr;
}

// .text:005a9c30
bool CLoginMgr::SetCurrentServer(FServerInfo const& Server)
{
    if (Server.bAddressValid == 0 || Server.bSessionValid == 0)
        return false;
    CurrentServer = Server; // FServerInfo::operator= (FUN_005a9ea0) inlined
    return true;
}

// .text:005ab4c0
bool CLoginMgr::CreateGun()
{
    if (G_bInternetServerList != 0 && Gun == nullptr)
    {
        // .rdata:005e64d0 -- the same application GUID CDPServer hosts with.
        GUID Guid = {0xa690f026, 0x26f0, 0x4e57, {0xac, 0xa0, 0xec, 0xf8, 0x68, 0xe4, 0x8d, 0x21}};
        // operator_new(0x28); no cd-key.
        Gun = new CGunWrapper("..\\exe\\gundll.dll", Guid, ListServerHost.c_str(),
                              ListServerPort, nullptr);
        ServerListPage = 0;
    }
    return true;
}

// .text:005ac240
bool CLoginMgr::RequestServerList()
{
    if (Gun != nullptr && Gun->GetBrowser() != nullptr &&
        static_cast<int>(Gun->GetStatus()) == 2)
    {
        bServerListRequestPending = 0;
        if (ServerListPage != 0)
            return false;
        Gun->GetBrowser()->SendRequest7104(0x65, 0, 0, 0, 0);
        ++ServerListPage;
        return true;
    }
    bServerListRequestPending = 1;
    return false;
}

namespace
{
    // The two lookups PingServers spells inline: the list-server entry with Entry's +0x64 text
    // and Id, or null. Only asked when that text is not empty.
    FServerInfo* FindServerByKey(std::vector<FServerInfo>& Servers, FServerInfo const& Entry)
    {
        for (size_t Index = 0; Index < Servers.size(); ++Index)
        {
            if (Servers[Index].ServerSig == Entry.ServerSig && Servers[Index].Id == Entry.Id)
                return &Servers[Index];
        }
        return nullptr;
    }

    // The match RemovePendingDirect / RemoveRetryDirect share.
    bool IsSameDirectServer(FServerInfo const& Entry, FServerInfo const& Server)
    {
        if (!Entry.ServerSig.empty() && Entry.ServerSig == Server.ServerSig &&
            Entry.Id == Server.Id)
            return true;
        if (Entry.Port != 0 && Entry.Address == Server.Address && Entry.Port == Server.Port)
            return true;
        return false;
    }
}

// .text:005ac2b0
void CLoginMgr::PingServers()
{
    // CDPClient::PendingConnectHandle read directly.
    if (G_bEnumerateLanHosts != 0 && PendingConnectHandle == 0)
        EnumerateHosts();

    DWORD const Now = timeGetTime();
    // A LAN entry (+0x3e) silent for more than 5 s shows no ping; past 30 s it is dropped.
    size_t Index = 0;
    while (Index < Servers.size())
    {
        FServerInfo& Entry = Servers[Index];
        unsigned long const Age = Now - static_cast<unsigned long>(Entry.LastResponseTime);
        if (Entry.bLan != 0 && Age > 5000)
        {
            bool const bDrop = Age > 30000;
            Entry.Ping = -1;
            bServerListChanged = 1;
            if (bDrop)
            {
                Servers.erase(Servers.begin() + Index);
                bServerListChanged = 1;
                continue;
            }
        }
        ++Index;
    }

    if (ServerListPage != 0)
        return;

    // Direct servers whose enumeration has run for a second. Under five tries they go to the
    // retry list +0x170; after that their list-server twin (if any) shows no ping. Either way
    // they leave +0x160 and the in-flight count drops.
    Index = 0;
    while (Index < PendingDirectServers.size())
    {
        FServerInfo& Entry = PendingDirectServers[Index];
        if (Entry.bEnumInFlight == 0 ||
            Now - static_cast<unsigned long>(Entry.LastResponseTime) < 1000)
        {
            ++Index;
            continue;
        }
        if (static_cast<unsigned>(Entry.EnumAttempts) < 5)
        {
            Entry.bEnumInFlight = 0;
            RetryDirectServers.push_back(Entry);
        }
        else
        {
            FServerInfo* const Twin =
                Entry.ServerSig.empty() ? nullptr : FindServerByKey(Servers, Entry);
            if (Twin != nullptr)
            {
                Twin->Ping = -1;
                bServerListChanged = 1;
            }
        }
        PendingDirectServers.erase(PendingDirectServers.begin() + Index);
        --InFlightDirectEnums;
    }

    if (InFlightDirectEnums >= 5)
        return;

    // +0x160 drained -> every retry goes back into it, its list-server twin showing "????"
    // (-2).
    if (PendingDirectServers.empty() && !RetryDirectServers.empty())
    {
        for (size_t Retry = 0; Retry < RetryDirectServers.size(); ++Retry)
        {
            FServerInfo& Entry = RetryDirectServers[Retry];
            Entry.bEnumInFlight = 0;
            PendingDirectServers.push_back(Entry);
            FServerInfo* const Twin =
                Entry.ServerSig.empty() ? nullptr : FindServerByKey(Servers, Entry);
            if (Twin != nullptr)
            {
                Twin->Ping = -2;
                bServerListChanged = 1;
            }
        }
        RetryDirectServers.clear();
    }

    // Start an enumeration on every port of each idle entry until five are in flight.
    size_t const End = PendingDirectServers.size();
    for (Index = 0; Index < End; ++Index)
    {
        FServerInfo& Entry = PendingDirectServers[Index];
        if (Entry.bEnumInFlight != 0)
            continue;
        bool bStarted = false;
        for (size_t PortIndex = 0; PortIndex < Entry.Ports.size(); ++PortIndex)
        {
            if (EnumerateHost(Entry.Address.c_str(), Entry.Ports[PortIndex], 2, 0, 0))
                bStarted = true;
        }
        if (bStarted)
        {
            ++InFlightDirectEnums;
            Entry.bEnumInFlight = 1;
        }
        Entry.LastResponseTime = static_cast<int>(timeGetTime());
        ++Entry.EnumAttempts;
        if (InFlightDirectEnums >= 5)
            return;
    }
}

// .text:005ac9d0
bool CLoginMgr::PumpServerList()
{
    if (Gun != nullptr && Gun->GetConnection() != nullptr)
    {
        Gun->GetConnection()->DispatchQueue(static_cast<IGunQueueReader*>(this));
        Gun->Update();
    }
    DispatchMsgs(0xffffffff);
    PingServers();

    // DAT_0067ea58 -- the last time the page was told to refresh.
    static DWORD LastRefreshTime = 0;
    bool bRefresh = false;
    if (bServerListChanged != 0 && timeGetTime() - LastRefreshTime >= 1000)
    {
        bRefresh = true;
        bServerListChanged = 0;
        LastRefreshTime = timeGetTime();
    }
    return bRefresh;
}

// .text:005aca40
void CLoginMgr::GetFilteredServers(std::vector<FServerInfo*>& Out, FServerCompare Compare,
                                   ServerFilter const* Filter)
{
    Out.clear();
    size_t const Count = Servers.size();
    Out.reserve(Count);
    for (size_t Index = 0; Index < Count; ++Index)
    {
        FServerInfo* const Server = &Servers[Index];
        if (Filter == nullptr || Filter->Match(*Server))
            Out.push_back(Server);
    }
    // VC6 std::sort inlined (FUN_005affc0 / FUN_005b0060 / FUN_005b00d0 / FUN_005afea0), ported
    // by vc6_sort.h so that servers with equal keys keep retail's order.
    if (Compare != nullptr)
        Vc6Sort::Sort(Out.data(), Out.data() + Out.size(), Compare);
}

// .text:005ac010
void CLoginMgr::RemovePendingDirect(FServerInfo const& Server)
{
    for (size_t Index = 0; Index < PendingDirectServers.size(); ++Index)
    {
        FServerInfo const& Entry = PendingDirectServers[Index];
        if (Entry.bEnumInFlight != 0 && IsSameDirectServer(Entry, Server))
        {
            PendingDirectServers.erase(PendingDirectServers.begin() + Index);
            --InFlightDirectEnums;
            return;
        }
    }
}

// .text:005abde0
void CLoginMgr::RemoveRetryDirect(FServerInfo const& Server)
{
    for (size_t Index = 0; Index < RetryDirectServers.size(); ++Index)
    {
        if (IsSameDirectServer(RetryDirectServers[Index], Server))
        {
            RetryDirectServers.erase(RetryDirectServers.begin() + Index);
            return;
        }
    }
}

// .text:005a9950
bool CLoginMgr::IsKnownLogin(FServerInfo const& Server) const
{
    return Logins.Contains(Server.Id);
}

// .text:005a9970
unsigned int CLoginMgr::GetLoginTime(FServerInfo const& Server)
{
    return Logins.GetTime(Server.Id);
}

// .text:005ab3e0 -- an unbounded wcscpy into the 24-character buffer, as retail.
void CLoginMgr::SetSelectCharacterName(wchar_t const* Name)
{
    wcscpy(SelectCharacterName, Name);
}

// .text:005aad30 (RET 4). Retail quirk: there is no end-of-list test; the step past the tail
// restarts at the head, so an absent name spins forever (and an empty list faults).
CCharacterSet::CharacterInfo* CLoginMgr::FindCharacterByName(wchar_t const* Name)
{
    MetaNode<CCharacterSet::CharacterInfo>* Node = nullptr;
    for (;;)
    {
        Characters.traverse(Node, true);
        if (wcscmp(Node->Data->Name.c_str(), Name) == 0)
        {
            return Node->Data;
        }
    }
}

// .text:005aace0 (RET 4).
CCharacterSet::CharacterInfo* CLoginMgr::FindCharacterById(CHARACTER_ID const& CharId)
{
    MetaNode<CCharacterSet::CharacterInfo>* Node = nullptr;
    while (Characters.traverse(Node, true))
    {
        if (Node->Data->Id == CharId)
        {
            return Node->Data;
        }
    }
    return nullptr;
}

// .text:005aad80 -- the first record with +0x200 (bIsRestart) set.
CCharacterSet::CharacterInfo* CLoginMgr::FindRestartCharacter()
{
    MetaNode<CCharacterSet::CharacterInfo>* Node = nullptr;
    while (Characters.traverse(Node, true))
    {
        if (Node->Data->bIsRestart != 0)
        {
            return Node->Data;
        }
    }
    return nullptr;
}

// .text:005aadb0 -- the first record with +0x201 (bIsAutoSave) set.
CCharacterSet::CharacterInfo* CLoginMgr::FindAutoSaveCharacter()
{
    MetaNode<CCharacterSet::CharacterInfo>* Node = nullptr;
    while (Characters.traverse(Node, true))
    {
        if (Node->Data->bIsAutoSave != 0)
        {
            return Node->Data;
        }
    }
    return nullptr;
}

// .text:005aade0.
bool CLoginMgr::ReloadFactions()
{
    Factions.clear();
    // NewChar::TheDB slot 4; its result is not read.
    NewChar::TheDB->ForEachFaction(PushFactionRecord, &Factions);
    return true;
}

// .text:005ab0e0
std::vector<FNewCharFaction>& CLoginMgr::GetFactions()
{
    return Factions;
}

// .text:005ab0f0. Reached from FUN_005b20a0 (the [Login] auto-login).
bool CLoginMgr::SelectAutoCharacter()
{
    MetaNode<CCharacterSet::CharacterInfo>* Found = nullptr;
    MetaNode<CCharacterSet::CharacterInfo>* Node = nullptr;
    while (Characters.traverse(Node, true))
    {
        if (!SinglePlayer())
        {
            if (_wcsicmp(Node->Data->Name.c_str(), SelectCharacterName) != 0)
            {
                continue;
            }
            Found = Node;
            break;
        }

        // Single player: [Login] char = "AutoSave" (DAT_005e65cc) picks the first autosave
        // record.
        if (_wcsicmp(SelectCharacterName, L"AutoSave") == 0)
        {
            if (Node->Data->bIsAutoSave == 0)
            {
                continue;
            }
            Found = Node;
            break;
        }

        // Otherwise the first record with the name is kept unless it is an autosave and a
        // later non-autosave record matches.
        if (_wcsicmp(Node->Data->Name.c_str(), SelectCharacterName) != 0)
        {
            continue;
        }
        if (Found != nullptr)
        {
            if (Found->Data->bIsAutoSave == 0)
            {
                continue;
            }
            if (Node->Data->bIsAutoSave != 0)
            {
                continue;
            }
        }
        Found = Node;
    }

    if (Found == nullptr)
    {
        return false;
    }
    return SelectCharacter(Found->Data);
}

// .text:005ab1f0 (RET 4).
bool CLoginMgr::SelectCharacterById(CHARACTER_ID const& CharId)
{
    MetaNode<CCharacterSet::CharacterInfo>* Node = nullptr;
    while (Characters.traverse(Node, true))
    {
        if (Node->Data->Id == CharId) // ??8@YA_NABUCHARACTER_ID@@0@Z
        {
            return SelectCharacter(Node->Data);
        }
    }
    return SelectCharacter(nullptr);
}

// .text:005ab250 (RET 4).
bool CLoginMgr::SelectCharacter(CCharacterSet::CharacterInfo* Info)
{
    if (Info == nullptr)
    {
        return false;
    }
    bCharacterSelectResponse = 0;
    G_Server->CharacterSelect(Info->Id, GetPlayerConnectionId()); // +0x50
    WaitForServerResponse(&bCharacterSelectResponse, 5.0f, 60.0f);              // FUN_005a89b0
    if (bCharacterSelectResponse == 0)
    {
        return false;
    }
    // The record into the player globals, its name into DAT_006732f0.
    ApplyCharacterSelectEquipFields(Info);
    return true;
}

// .text:005ab380 (RET 4), reached from IClient slot 14 (LAB_005a8240).
void CLoginMgr::OnCharacterSelectResponse(unsigned int const* Response)
{
    bCharacterSelectResponse = 1;
    NotifyCharacterSelectComplete(Response);
}

// .text:005ab400
void CLoginMgr::SetListServerHost(char const* Host)
{
    ListServerHost.assign(Host, strlen(Host));
}

// .text:005ab4b0
void CLoginMgr::SetListServerPort(unsigned int Port)
{
    ListServerPort = Port;
}

// .text:005ac2a0 -- tail-jumps to CDPClient::SetGUID.
void CLoginMgr::SetAppGuid(GUID& Guid)
{
    SetGUID(Guid);
}

// .text:005a82d0 (cdecl; the first argument, the command line, is not read). The server
// module name goes to G_LocalServerDll, the rest into the context.
void CLoginMgr::LoadConfig(char const* IniPath)
{
    GUID AppGuid;
    bool bHaveGuid = false;

    INI_Reader Reader;
    Reader.open(IniPath, false);   // result not tested
    if (!Reader.read_header())
    {
        return;
    }

    do
    {
        if (Reader.is_header("Freelancer"))
        {
            while (Reader.read_value())
            {
                if (Reader.is_value("local_server"))
                {
                    char const* Value = Reader.get_value_string();
                    if (Value != nullptr)
                    {
                        size_t Length = strlen(Value);
                        if (Length >= 0x20)
                        {
                            Length = 0x1f;
                        }
                        memcpy(G_LocalServerDll, Value, Length);
                        G_LocalServerDll[Length] = '\0';
                    }
                }
                else if (Reader.is_value("AppGUID"))
                {
                    ParseGUID(Reader.get_value_string(), &AppGuid);
                    bHaveGuid = true;
                }
            }
        }
        else if (Reader.is_header("ListServer"))
        {
            while (Reader.read_value())
            {
                if (Reader.is_value("hostname"))
                {
                    SetListServerHost(Reader.get_value_string());
                }
                else if (Reader.is_value("port"))
                {
                    // get_value_float(0) then _ftol.
                    SetListServerPort(static_cast<unsigned int>(
                        static_cast<long long>(Reader.get_value_float(0))));
                }
            }
        }
        else if (Reader.is_header("Login"))
        {
            while (Reader.read_value())
            {
                if (Reader.is_value("name"))
                {
                    LoginName = std::wstring(Reader.get_value_wstring());
                }
                else if (Reader.is_value("pwd"))
                {
                    LoginPassword = std::wstring(Reader.get_value_wstring());
                }
                else if (Reader.is_value("char"))
                {
                    SetSelectCharacterName(Reader.get_value_wstring());
                }
            }
        }
    } while (Reader.read_header());

    if (bHaveGuid)
    {
        SetAppGuid(AppGuid);        // FUN_005ac2a0
    }
}
