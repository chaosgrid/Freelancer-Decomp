// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Server.dll's account database: ci_char_traits / ci_wchar_traits, CiString / CiWString,
// CAccount, PlayerDB and the `Players` singleton. Separate from common_server.h (which
// includes it) because FLServer.exe needs these types in MFC translation units, where
// Freelancer's `class CObject` (common_cobject.h) collides with MFC's CObject. This
// header's include closure must stay free of common_cobject.h.
#pragma once

#include "server_exports.h"         // SERVER_API
#include "common_server_types.h"    // SLoginInfo, SCreateCharacterInfo
#include "common_net.h"             // CPlayerAccount (by value in PlayerDB at +0x10)
#include "common_server_playerdb.h" // PlayerData (PlayerDB's slot array)
#include <cstring>   // _strnicmp (ci_char_traits)
#include <cwchar>    // _wcsnicmp (ci_wchar_traits)
#include <list>
#include <map>
#include <string>

// ============================================================================
// ci_char_traits / ci_wchar_traits -- retail's case-insensitive string traits
// ============================================================================
// Every PlayerDB/CAccount export that takes an account or character name uses one of
// these (retail mangles `struct ci_wchar_traits` over VC6's `unsigned short` wchar_t, G).
// Deviation: the port's are over the real wchar_t, so its decorated names carry _W where
// retail's carry G. The only member retail's code shows is compare, inlined by
// basic_string::compare for the two account maps (_wcsnicmp / _strnicmp over the shorter
// length; equal -> shorter is less).
struct ci_char_traits : public std::char_traits<char>
{
    static int compare(const char* Lhs, const char* Rhs, size_t Count)
    {
        return _strnicmp(Lhs, Rhs, Count);
    }
};

struct ci_wchar_traits : public std::char_traits<wchar_t>
{
    static int compare(const wchar_t* Lhs, const wchar_t* Rhs, size_t Count)
    {
        return _wcsnicmp(Lhs, Rhs, Count);
    }
};

// basic_string<wchar_t, ci_wchar_traits>: account and character names.
typedef std::basic_string<wchar_t, ci_wchar_traits> CiWString;
// basic_string<char, ci_char_traits>: "<hash>.fl" character ids.
typedef std::basic_string<char, ci_char_traits> CiString;

// ============================================================================
// CAccount
// ============================================================================
// Retail Server.dll exports exactly these CAccount members: the four special members,
// the vftable, AppendCharacterNames, DeleteCharacterFromID, ForceLogout and
// InitFromFolder. The data is public and server_classes.cpp reads it directly, as
// retail's PlayerDB code does with raw offsets.
class SERVER_API CAccount {
public:
    CAccount();
    CAccount(const CAccount& Other);
    virtual ~CAccount();
    CAccount& operator=(const CAccount& Other);

    // Server .text:06ce1050: `RET 4` -- the body is empty (ICF-folded with the
    // empty IServerImpl slots and ContentCallback::scan_state_change).
    void AppendCharacterNames(std::list<CiWString>& Names);
    void DeleteCharacterFromID(CiString& CharId);
    // Server .text:06d56dd0, returns void.
    void ForceLogout();
    void InitFromFolder(const char* FolderPath);

    // Member order is retail's 0x38-byte layout, from the default ctor (.text:06d567d0):
    //   +0x00  vftable
    //   +0x04  basic_string<G, ci_wchar_traits>  the account name (same type as the
    //          +0x44 map key)
    //   +0x14  narrow std::string           the account directory
    //   +0x24  std::list<CiWString>         character names
    //   +0x30  bool  locked                 (ctor 0)
    //   +0x31  bool  not banned             (ctor 1)
    //   +0x32  bool  admin                  (ctor 0)
    //   +0x34  unsigned int client id       (ctor 0)
    // The containers are the modern std ones (no retail ABI). Retail has no password member.
    CiWString m_Name;
    // +0x14: the account's own directory on disk. Written by the internal-login helper
    // FUN_06d48e80 and read back by PlayerDB::logout for its FDUMP line.
    std::string m_DirectoryPath;
    // The character list retail keeps at CAccount+0x24 (std::list, head at
    // +0x28, size member at +0x2c). It holds the character NAMES, wide and
    // case-insensitive, not the ".fl" file names -- the element type of
    // AppendCharacterNames' list -- and CAccount::DeleteCharacterFromID
    // (0x06d56bd0) hashes EVERY entry back into a "<hash>.fl" through
    // FUN_06d46370 before comparing it with the id it was given.
    std::list<CiWString> m_Characters;
    // +0x30: set by PlayerDB::LockAccountAccess (.text:06d52a78), cleared by
    // UnlockAccountAccess (.text:06d52ad1); FUN_06d48e80 refuses a login with 7
    // while it is set (.text:06d48f4e).
    bool m_bLocked;
    // +0x31: 1 = the account may log in. InitFromFolder clears it when a
    // "banned" file exists (.text:06d56b73); FUN_06d48e80 returns 0 when it is
    // clear (.text:06d48f5c); GetAccountBanned returns its negation (SETZ at
    // .text:06d52028).
    bool m_bNotBanned;
    // +0x32: InitFromFolder sets it for an "admin" file (.text:06d56bad);
    // GetAccountAdminRights returns it (.text:06d52781).
    bool m_bAdmin;
    // +0x34: the logged-in client, 0 when none (FUN_06d48e80 .text:06d48f7b).
    unsigned int m_ClientID;
};

// ============================================================================
// PlayerData & PlayerDB
// ============================================================================
// PlayerData is defined in common_server_playerdb.h.

class SERVER_API PlayerDB {
    // The port's own C accessor (common_server_playerdb.h) for the
    // allow-new-players flag at [this+0x0c]; not a retail function, and a friend
    // declaration exports nothing.
    friend bool __cdecl PlayerDB_AllowsNewPlayers();
public:
    PlayerDB();
    PlayerDB(const PlayerDB& Other);
    ~PlayerDB();
    PlayerDB& operator=(const PlayerDB& Other);

    void init(unsigned int MaxPlayers, bool bAllowNewPlayers);
    void free();
    void cleanup(unsigned int SessionId);
    unsigned int GetMaxPlayerCount();
    unsigned int GetServerID();
    const char* GetServerSig();
    bool is_valid(const unsigned int& Id);
    // 0x06ce1a40: the first reference is the player id (range-checked against MaxPlayers
    // and the slot's client id), the second the ship flid.
    bool is_valid_ship_owner(const unsigned int& PlayerId, const unsigned int& ShipFlid);
    PlayerData& operator[](const unsigned int& Id);
    PlayerData* traverse_active(PlayerData* Current) const;
    unsigned char login(const SLoginInfo& Info, unsigned int SessionId);
    void logout(unsigned int SessionId);
    void logout_all();
    const wchar_t* GetActiveCharacterName(unsigned int SessionId) const;
    unsigned int CountPlayersInSystem(int SystemId);
    unsigned int GetGroupID(unsigned int PlayerId);
    void SendGroupID(unsigned int PlayerId, unsigned int GroupId);
    void SendSystemID(unsigned int PlayerId, unsigned int SystemId);
    void BuildLocalUserDir();
    bool MakeLocalUserPath(char* OutPath, const char* FileName);
    CAccount* FindAccountFromClientID(unsigned int ClientId);
    bool create_restart_file(const char* FileName);
    bool create_new_character(const SCreateCharacterInfo& Info, unsigned int SessionId);

    // Name/id parameters are non-const references, exactly as retail mangles
    // them (AAV?$basic_string@GUci_wchar_traits@@...; G is VC6's wchar_t).
    void UnlockAccountAccess(CiWString& CharName);
    void ReadCharacterName(const char* FilePath, CiWString& OutName);
    void SetMOTD(std::wstring& Message);
    std::wstring& GetMOTD();
    void DeleteCharacterFromID(CiString& CharId);
    bool DeleteCharacterFromName(CiWString& CharName);

    bool CreateAccount(CiWString& AccountName);
    void DeleteAccount(CiWString& AccountName);
    bool BanAccount(CiWString& AccountName, bool bBan);
    bool GetAccountBanned(CiWString& AccountName);
    bool SetAccountPassword(CiWString& AccountName, CiWString& Password);
    bool GetCharactersForAccount(CiWString& AccountName, std::list<CiWString>& OutCharacters);

    CAccount* FindAccountFromName(CiWString& Name);
    CAccount* FindAccountFromCharacterName(CiWString& CharName);
    CAccount* FindAccountFromCharacterID(CiString& CharId);
    bool GetAccountAdminRights(CiWString& Name);
    bool SetAccountAdminRights(CiWString& Name, bool bAdmin);
    void LockAccountAccess(CiWString& Name);

private:
    // Retail marks these three `private:` (?create_account@PlayerDB@@AAE...,
    // ?load_user_data@PlayerDB@@AAE..., ?to_index@PlayerDB@@AAE...).
    int create_account(const SLoginInfo& Info);
    unsigned char load_user_data(const SLoginInfo& Info, unsigned int SessionId);
    unsigned int to_index(unsigned int SessionId);

protected:
    // Member order follows retail's, from the ctor (0x06d50b10) and init (0x06d510c0):
    //   +0x00  PlayerData*  slot array base
    //   +0x04  unsigned int MaxPlayers
    //   +0x08  unsigned int ServerID          (sscanf(sig, "%x"); IServerImpl::GetServerID
    //                                          reads &Players + 0x8)
    //   +0x0c  bool         bAllowNewPlayers  (SStartupInfo +0x211; gates account creation)
    //   +0x10  CPlayerAccount (0x0C bytes)
    //   +0x1c  PlayerData*  slot array end
    //   +0x20  std::wstring MOTD (VC6 basic_string)
    //   +0x30  character-id container (0x14 bytes)
    //   +0x44  account-name container (0x14 bytes)
    PlayerData* m_PlayerArray;
    unsigned int m_MaxPlayers;
    unsigned int m_ServerID;
    bool m_bAllowNewPlayers;
    // +0x10: what GetServerSig (0x06ce1ac0, ADD ECX,0x10) goes through.
    CPlayerAccount m_ServerAccount;
    PlayerData* m_PlayerArrayEnd;
    std::wstring m_MOTD;
    // Account lookup maps (retail: char-ID map at this+0x30, name map at this+0x44),
    // ordered by the ci traits' compare exactly like retail's std::less keys.
    // Values are bare CAccount* as in retail (node+0x1c); the name map owns
    // them (PlayerDB::~PlayerDB deletes through it).
    std::map<CiString, CAccount*> m_CharacterIDMap;
public:
    // Public: FLServer.exe walks this map inline (Players+0x48 head, node value CAccount*,
    // accounts dialog .text:00412de6); PlayerDB exports no accessor for it.
    std::map<CiWString, CAccount*> m_AccountNameMap;
};

// Retail's one and only PlayerDB: `?Players@@3VPlayerDB@@A`, Server.dll ordinal
// 352, .data:06d90260. Ghidra prints its readers as `MOV EDX,[.data:Players]`
// (the slot array base, first member) and its method calls as
// `MOV ECX,.data:Players` (the object's address) -- for example 0x06ce904b/
// 0x06ce9057, which is `Players.operator[](...)`.
extern SERVER_API PlayerDB Players;
