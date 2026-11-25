// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// server_chat.cpp -- Server.dll's chat router.
//
// Retail source file: "E:\FL\Scratch\Source\Server\FLChatServer.cpp", the __FILE__
// every FDUMP notice below carries. Reached from exactly one place:
// IServerImpl::SubmitChat (0x06d191b0, the IChatServer slot), defined at the bottom of
// this file, which forwards to Server_SubmitChat.
//
// What SubmitChat does, in retail order:
//   1. builds the slash-command table once (flag DAT_06d8f610);
//   2. group-control messages (Target == 0x00010004) go straight to the
//      invite / leave / join verbs and nothing else happens;
//   3. decodes the BinaryRDL text; a leading '/' is matched against the command
//      table, and a matched command either swallows the message or rewrites the
//      target/payload and lets it continue;
//   4. prefixes "<sender name>: " as rich text, appends the original payload (or
//      a red "<message too big>"), re-encodes;
//   5. routes on the target CHAT_ID: private, universe, system, local, group.
#include "../include/common_server.h"
#include "../include/common_server_playerdb.h"
#include "../include/common_text.h"
#include "../include/common_net.h"          // IsMPServer
#include "../include/server_sysdb.h"
#include "../include/rpc_iclient.h"
#include "server_internal.h"
#include <windows.h>
#include <cstring>
#include <cwchar>
#include <list>
#include <new>
#include <regex>
#include <set>
#include <string>
#include <vector>
#include "../include/server_iserverimpl.h"   // IServerImpl, Server
#include "../include/server_gameobject.h"
#include "../include/common_goods.h"
#include "../include/server_basedb.h"
#include <cstdarg>
#include <cstdlib>

// Retail .data:DAT_06d8fb18, defined in IServerImpl.cpp.
extern IChatClient* g_pChatClientInterface;

// FUN_06d19180: a CHAT_ID from a player/session id, bytes 2 and 3 zero (CHAT_ID{word}).
// FUN_06d19ef0 is the same with word 0.
unsigned int MakeChatId(unsigned int Id) { return Id & 0xffff; }

// The exported hook slots g_pPrivateChatHook / g_pSystemChatHook /
// g_pUniverseChatHook are declared in common_server.h and defined further down this
// file.

// ---------------------------------------------------------------------------
// Chat ids. Each is a CHAT_ID global set by its own CRT initializer, which
// writes the word and byte 2 and leaves byte 3 at its .data zero:
//   LAB_06d18930  DAT_06d8f5f0  word 0, byte2 0   console / "nobody"
//   LAB_06d18940  DAT_06d8f5ec  word 0, byte2 1   universe
//   LAB_06d18960  DAT_06d8f5e8  word 1, byte2 1   system
//   LAB_06d18980  DAT_06d8f5e4  word 2, byte2 1   local
//   LAB_06d189a0  DAT_06d8f5e0  word 3, byte2 1   group
//   LAB_06d189c0  DAT_06d8f5dc  word 4, byte2 1   group control
// Byte 2 is the "channel" flag: a CHAT_ID whose byte 2 is zero names a player.
// Every comparison in SubmitChat is a full-dword CMP against these.
// ---------------------------------------------------------------------------
static const unsigned int GChatIdConsole  = 0x00000000u;
static const unsigned int GChatIdUniverse = 0x00010000u;
static const unsigned int GChatIdSystem   = 0x00010001u;
static const unsigned int GChatIdLocal    = 0x00010002u;
static const unsigned int GChatIdGroup    = 0x00010003u;
// server_classes.cpp's CPlayerGroup senders use the same global, hence extern.
extern const unsigned int GGroupChatSourceId = 0x00010004u;

// FUN_06d19110 (cdecl): the control block to every active slot, through the IChatClient
// with no null test. The ushort param is read once, before the loop.
void Server_BroadcastGroupControl(unsigned int MsgType, unsigned int Param) {
    PlayerData* Slot = Players.traverse_active(nullptr);
    if (Slot == nullptr)
        return;
    unsigned int const Word = Param & 0xffff;
    do {
        unsigned int const Target = MakeChatId(Slot->client_id);
        FGroupMsgPayload Payload;
        Payload.MsgType = MsgType;
        Payload.Param = Word;
        g_pChatClientInterface->SendCommonChatMsg(CHAT_ID(Target), CHAT_ID(GGroupChatSourceId),
                                                  sizeof(Payload), &Payload);
        Slot = Players.traverse_active(Slot);
    } while (Slot != nullptr);
}

// ---------------------------------------------------------------------------
// Buffers and codec objects.
// ---------------------------------------------------------------------------
static wchar_t GChatText[0x400];        // .data:06d8e9b0, decoded text (0x400 wchar)
static char GChatSendBuffer[0x400];     // .data:06d8e5a8, the encoded outgoing message
static char GChatCommandBuffer[0x400];  // .data:06d8f1b8, echo / channel re-encodes

// Retail keeps the reader (.data:06d8f1b0) and the first writer (.data:06d8e9a8) as
// function-local statics of SubmitChat (guard DAT_06d8f1b4); the second writer
// (.data:06d8f5b8, CRT initializer LAB_06d18e90) is used only by the command handlers.
// All three are plain file-scope objects here; their constructors only store the vptr.
static BinaryRDLReader GChatReader;
static BinaryRDLWriter GChatWriter;
static BinaryRDLWriter GChatCommandWriter;

static const char* const GChatServerFile = "E:\\FL\\Scratch\\Source\\Server\\FLChatServer.cpp";

// Every notice in this file has this shape, unconditionally.
static void ChatNotice(int Line, const char* Message)
{
    FDUMP(kFdumpNotice, "%s(%d) : NOTICE:General:%s", GChatServerFile, Line, Message);
}

// ---------------------------------------------------------------------------
// PlayerData helpers.
// ---------------------------------------------------------------------------

// FUN_06d4a1e0 (thiscall on the slot).
bool PlayerData::has_selected_character() const
{
    if (!IsMPServer())
        return true;
    return selected_character.Buffer[0] != '\0';
}

// FUN_06d4eda0 (thiscall on the slot): the fallback is .data:06d8d40c; the result aliases
// the vibe's own name buffer.
static const wchar_t GChatBadName[] = L">>>BAD   NAME<<<";

const wchar_t* PlayerData::get_character_name() const
{
    const wchar_t* DisplayName = GChatBadName;
    FmtStr Name1;
    FmtStr Name2;
    if (::Reputation::Vibe::GetName(rep_vibe, Name1, Name2, DisplayName) == -2)
        DisplayName = GChatBadName;
    return DisplayName;
}

// ---------------------------------------------------------------------------
// The group verbs. Every reply goes to the client as an 8-byte control block
// on the group-control channel: IChatClient::SendCommonChatMsg(recipient, 0x00010004, 8,
// &{Type, Param}).
// ---------------------------------------------------------------------------
struct FChatGroupControl {
    unsigned int Type;    // +0x00
    unsigned int Param;   // +0x04, a ushort zero-extended
};

static void ChatGroup_Send(unsigned int Recipient, unsigned int Type, unsigned int Param)
{
    FChatGroupControl Payload;
    Payload.Type = Type;
    Payload.Param = Param & 0xffffu;
    g_pChatClientInterface->SendCommonChatMsg(CHAT_ID(MakeChatId(Recipient)),
                                              CHAT_ID(GGroupChatSourceId), sizeof(Payload), &Payload);
}

// FUN_06d19f20 (cdecl): Inviter asks Invitee into his group. Name, when given, must match
// the invitee's character name (_wcsicmp).
static void ChatGroup_Invite(unsigned int Inviter, unsigned int Invitee, const wchar_t* Name)
{
    // The inviter's slot is indexed unchecked.
    PlayerData& InviterSlot = Players[Inviter];

    if (!Players.is_valid(Invitee))
        return;
    PlayerData& InviteeSlot = Players[Invitee];
    if (!InviteeSlot.has_selected_character())
        return;
    if (Name != nullptr &&
        _wcsicmp(Name, InviteeSlot.get_character_name()) != 0)
        return;

    // A groupless inviter founds one, joining it first.
    CPlayerGroup* Group = static_cast<CPlayerGroup*>(InviterSlot.group_ptr);
    if (Group == nullptr) {
        Group = new CPlayerGroup();                            // operator new(0x30)
        Group->AddMember(Inviter);
    }

    if (InviterSlot.client_id == InviteeSlot.client_id) {
        ChatGroup_Send(Inviter, 0x9, InviterSlot.client_id);  // inviting yourself
        return;
    }
    if (InviteeSlot.group_mission_id != 0) {
        ChatGroup_Send(InviterSlot.client_id, 0xf, InviteeSlot.client_id);
        return;
    }
    if (Group->GetMemberCount() >= 8) {
        ChatGroup_Send(Inviter, 0xa, InviterSlot.client_id);  // group full
        return;
    }
    if (Group->IsMember(InviteeSlot.client_id)) {
        ChatGroup_Send(Inviter, 0xb, InviteeSlot.client_id);  // already a member
        return;
    }
    // Retail re-reads the inviter's group_ptr here rather than reusing the local.
    static_cast<CPlayerGroup*>(InviterSlot.group_ptr)->AddInvite(InviteeSlot.client_id);
    ChatGroup_Send(Inviter, 0x3, InviteeSlot.client_id);
    ChatGroup_Send(InviteeSlot.client_id, 0x4, Inviter);
}

// FUN_06d1a2e0 (cdecl): Joiner accepts Inviter's invitation.
static void ChatGroup_Join(unsigned int Joiner, unsigned int Inviter, const wchar_t* Name)
{
    PlayerData& JoinerSlot = Players[Joiner];                  // unchecked

    if (!Players.is_valid(Inviter))
        return;
    PlayerData& InviterSlot = Players[Inviter];
    if (!InviterSlot.has_selected_character())
        return;
    if (Name != nullptr &&
        _wcsicmp(Name, InviterSlot.get_character_name()) != 0)
        return;

    CPlayerGroup* const OldGroup = static_cast<CPlayerGroup*>(JoinerSlot.group_ptr);
    CPlayerGroup* const Group = static_cast<CPlayerGroup*>(InviterSlot.group_ptr);

    if (JoinerSlot.client_id == InviterSlot.client_id) {
        ChatGroup_Send(Joiner, 0xc, JoinerSlot.client_id);
        return;
    }
    if (Group == nullptr || !Group->IsInvited(JoinerSlot.client_id)) {
        ChatGroup_Send(Joiner, 0xd, JoinerSlot.client_id);
        return;
    }
    if (Group->IsMember(JoinerSlot.client_id)) {
        ChatGroup_Send(Joiner, 0xe, InviterSlot.client_id);
        return;
    }
    if (JoinerSlot.group_mission_id != 0) {
        ChatGroup_Send(JoinerSlot.client_id, 0xf, JoinerSlot.client_id);
        return;
    }
    if (Group->GetMemberCount() >= 8) {
        ChatGroup_Send(Joiner, 0x10, InviterSlot.client_id);
        return;
    }
    if (OldGroup != nullptr)
        OldGroup->DelMember(JoinerSlot.client_id);
    ChatGroup_Send(JoinerSlot.client_id, 0x5, InviterSlot.client_id);
    Group->AddMember(JoinerSlot.client_id);
    Group->DelInvite(JoinerSlot.client_id);
}

// FUN_06d1a6b0 (cdecl): Player leaves his group; a groupless player is told so with type
// 0x12.
static void ChatGroup_Leave(unsigned int Player)
{
    PlayerData& Slot = Players[Player];                        // unchecked
    CPlayerGroup* const Group = static_cast<CPlayerGroup*>(Slot.group_ptr);
    if (Group != nullptr) {
        Group->DelMember(Slot.client_id);
        return;
    }
    ChatGroup_Send(Slot.client_id, 0x12, Slot.client_id);
}

// ---------------------------------------------------------------------------
// The slash-command table.
// ---------------------------------------------------------------------------

// SubmitChat's on-stack context, handed to every command callback.
struct FChatCommandContext {
    CHAT_ID Sender;         // +0x00
    CHAT_ID Target;         // +0x04
    const void* Data;       // +0x08
    unsigned long Size;     // +0x0C
    bool bPassThrough;      // +0x10  set: continue with the rewritten message
};

// Callbacks are cdecl and return a bool the matcher ignores.
typedef bool (__cdecl* FChatCommandFn)(FChatCommandContext* Ctx, std::vector<std::wstring>* Args);

// Server.dll's own RDL append (FUN_06d1b1b0). Retail clones the node, inserts it at the
// tail (the insert clones AGAIN, FUN_06d1b4a0) and deletes the first clone; the net effect
// is one clone at the tail, which this does with a single Clone. No null or allocation
// tests, as in retail. Common.dll keeps its own copy, as retail does. Shared with
// PlayerData::report_cheat (server_playerdb.cpp).
void Server_RdlAppend(RenderDisplayList& Rdl, const RDLNode& Node) {
    RDLNode* Copy = Node.Clone();
    void** Sentinel = (void**)Rdl.NodeList;
    void** Tail = static_cast<void**>(Sentinel[1]);
    void** Entry = static_cast<void**>(malloc(0xC));
    Entry[0] = Sentinel;   // next
    Entry[1] = Tail;       // prev
    Entry[2] = Copy;
    Sentinel[1] = Entry;
    Tail[0] = Entry;
    Rdl.Count++;
}

// One list element, 8 bytes in retail: {rpattern*, callback}.
//
// DEVIATION (library substitution; docs/re/deviations.md, GRETA / std::wregex): retail's
// pattern is a statically linked GRETA rpattern (ctor FUN_06d1be80, match FUN_06d1f300,
// size 0x68) built with flags 0x8000 (C-string mode, case-sensitive, not multiline).
// The std::wregex stand-in reproduces its semantics:
// - `.` is spelled `[^\n]`: GRETA's dot (vtable PTR_06d6906c) fails only on NUL or '\n',
//   while ECMAScript's also refuses '\r', U+2028 and U+2029.
// - `^` is GRETA's beginning-of-STRING node (FUN_06d284c0); MSVC's `^` also matches after
//   every '\n', so FChatCommand::match passes match_continuous (every pattern starts with
//   `^`).
// - `\s` is built by ChatCommand_SpaceClass: GRETA classifies code units <= 0xff with
//   msvcrt _isctype on the "C" table (0x09-0x0D, 0x20; no retail module sets LC_CTYPE)
//   and the rest with iswctype, i.e. GetStringTypeW C1_SPACE. UCRT's ctype<wchar_t> would
//   also accept U+0085 and U+00A0, which retail rejects. None of the added characters is
//   special inside a bracket expression.
static std::wstring ChatCommand_SpaceClass()
{
    std::wstring Class;
    Class.push_back(L'[');
    Class.push_back(static_cast<wchar_t>(0x09));   // 0x09 .. 0x0D
    Class.push_back(L'-');
    Class.push_back(static_cast<wchar_t>(0x0d));
    Class.push_back(static_cast<wchar_t>(0x20));
    for (unsigned int Code = 0x100; Code <= 0xffff; ++Code) {
        const wchar_t Ch = static_cast<wchar_t>(Code);
        WORD Type = 0;
        if (GetStringTypeW(CT_CTYPE1, &Ch, 1, &Type) != 0 && (Type & C1_SPACE) != 0)
            Class.push_back(Ch);
    }
    Class.push_back(L']');
    return Class;
}

struct FChatCommand {
    std::wregex* Pattern;
    FChatCommandFn Callback;

    // FUN_06d18be0.
    bool match(FChatCommandContext* Ctx, const wchar_t* Text) const;
};

// .data:DAT_06d8f5bc (built by LAB_06d18a20). Its atexit FUN_06d18a60 frees the nodes but
// never the patterns -- they are deliberately leaked here too.
static std::list<FChatCommand> GChatCommands;
static bool GChatCommandsBuilt = false;   // .data:DAT_06d8f610

// FUN_06d18ac0 (thiscall). A failed allocation leaves Pattern null (VC6 operator new
// returns null), hence the nothrow new.
static FChatCommand ChatCommand_Make(const wchar_t* Text, FChatCommandFn Callback)
{
    FChatCommand Entry;
    Entry.Pattern = nullptr;
    Entry.Callback = Callback;
    if (Text != nullptr)
        Entry.Pattern = new (std::nothrow) std::wregex(Text, std::regex_constants::ECMAScript);
    return Entry;
}

// FUN_06d18be0 (thiscall on the entry): true on any match, whatever the callback returns;
// the callback gets one string per backref (0 = whole match).
bool FChatCommand::match(FChatCommandContext* Ctx, const wchar_t* Text) const
{
    if (Pattern == nullptr)
        return false;
    std::wcmatch Match;
    // match_continuous = GRETA's beginning-of-string `^` (see FChatCommand).
    if (!std::regex_search(Text, Match, *Pattern, std::regex_constants::match_continuous))
        return false;
    if (Callback == nullptr)
        return true;
    std::vector<std::wstring> Args(Match.size());
    for (size_t Index = 0; Index < Match.size(); ++Index)
        Args[Index].assign(Match[Index].first, Match[Index].second);
    Callback(Ctx, &Args);
    return true;
}

// Server_SendStyledChat -- retail FUN_06d18ea0 (cdecl). Only caller: CPlayerGroup::SendChat.
// The vswprintf is unbounded, as in retail. Sent from the console id only when encoding
// succeeds; no null test on the interface, no notice on failure. The appends are
// FUN_06d1b1b0 inlined (Server_RdlAppend).
void __cdecl Server_SendStyledChat(unsigned short Style, unsigned int Target,
                                   const wchar_t* Fmt, ...)
{
    wchar_t Text[0x400];
    va_list Args;
    va_start(Args, Fmt);
    _vswprintf(Text, Fmt, Args);
    va_end(Args);

    RenderDisplayList Rdl;
    {
        StyleNode Node(Style);
        Server_RdlAppend(Rdl, Node);
    }
    {
        TextNode Node(static_cast<const wchar_t*>(Text), -1);
        Server_RdlAppend(Rdl, Node);
    }
    unsigned int Size = 0;
    if (GChatCommandWriter.write_buffer(Rdl, GChatCommandBuffer, 0x400, Size)) {
        g_pChatClientInterface->SendCommonChatMsg(CHAT_ID(Target), CHAT_ID(GChatIdConsole), Size,
                                                  GChatCommandBuffer);
    }
}

// FUN_06d1aa70 (cdecl): rewrite the message for a channel command -- target := Channel,
// payload := StyleNode(Style) + TextNode(Message) -- and ask SubmitChat to carry on with it.
static void ChatCommand_Redirect(FChatCommandContext* Ctx, const std::wstring& Message,
                                 unsigned int Channel, unsigned short Style)
{
    Ctx->Target = CHAT_ID(Channel);
    RenderDisplayList Rdl;
    {
        StyleNode Node(Style);
        Server_RdlAppend(Rdl, Node);
    }
    {
        TextNode Node(Message, -1);                            // basic_string overload
        Server_RdlAppend(Rdl, Node);
    }
    unsigned int Size = 0;
    if (GChatCommandWriter.write_buffer(Rdl, GChatCommandBuffer, 0x400, Size)) {
        Ctx->Data = GChatCommandBuffer;
        Ctx->Size = Size;
        Ctx->bPassThrough = true;
    } else {
        ChatNotice(0x21e, "Failed to re-encode chat message.\n");
    }
}

// FUN_06d1a800: "/echo <text>" -- send the text back to the sender alone, styled 0x3ea8.
// Swallows the message.
static bool __cdecl ChatCommand_Echo(FChatCommandContext* Ctx, std::vector<std::wstring>* Args)
{
    RenderDisplayList Rdl;
    {
        StyleNode Node(static_cast<unsigned short>(0x3ea8));
        Server_RdlAppend(Rdl, Node);
    }
    if (Args->size() == 2) {
        TextNode Node((*Args)[1], -1);                         // basic_string overload
        Server_RdlAppend(Rdl, Node);
    } else {
        TextNode Node(L"invalid argument count", -1);
        Server_RdlAppend(Rdl, Node);
    }
    unsigned int Size = 0;
    if (GChatCommandWriter.write_buffer(Rdl, GChatCommandBuffer, 0x400, Size)) {
        g_pChatClientInterface->SendCommonChatMsg(Ctx->Sender, Ctx->Sender, Size, GChatCommandBuffer);
    } else {
        ChatNotice(0x1ff, "Failed to encode echo message.\n");
    }
    return true;
}

// LAB_06d1a1a0: "/invite <name>", "/i <name>". Only players (byte 2 of the sender clear);
// the name is looked up among account characters.
static bool __cdecl ChatCommand_Invite(FChatCommandContext* Ctx, std::vector<std::wstring>* Args)
{
    if (((Ctx->Sender.Id >> 16) & 0xffu) != 0)
        return true;
    if (Args->size() < 3)
        return false;
    CiWString Name((*Args)[2].c_str());
    CAccount* const Account = Players.FindAccountFromCharacterName(Name);
    if (Account != nullptr && Account->m_ClientID != 0)       // CAccount+0x34
        ChatGroup_Invite(Ctx->Sender.GetPlayerId(), Account->m_ClientID, (*Args)[2].c_str());
    return true;
}

// LAB_06d1a570: "/join <name>", "/j <name>" -- the invite handler's twin.
static bool __cdecl ChatCommand_Join(FChatCommandContext* Ctx, std::vector<std::wstring>* Args)
{
    if (((Ctx->Sender.Id >> 16) & 0xffu) != 0)
        return true;
    if (Args->size() < 3)
        return false;
    CiWString Name((*Args)[2].c_str());
    CAccount* const Account = Players.FindAccountFromCharacterName(Name);
    if (Account != nullptr && Account->m_ClientID != 0)
        ChatGroup_Join(Ctx->Sender.GetPlayerId(), Account->m_ClientID, (*Args)[2].c_str());
    return true;
}

// LAB_06d1a740: "/leave", "/lv" -- FUN_06d1a6b0 inlined on the sender.
static bool __cdecl ChatCommand_Leave(FChatCommandContext* Ctx, std::vector<std::wstring>* Args)
{
    if (((Ctx->Sender.Id >> 16) & 0xffu) != 0)
        return true;
    if (Args->size() < 2)
        return false;
    ChatGroup_Leave(Ctx->Sender.GetPlayerId());
    return true;
}

// LAB_06d1aa30 / LAB_06d1ac50 / LAB_06d1ac90 / LAB_06d1acd0: exactly three backrefs, else
// nothing; always true.
static bool __cdecl ChatCommand_Universe(FChatCommandContext* Ctx, std::vector<std::wstring>* Args)
{
    if (Args->size() == 3)
        ChatCommand_Redirect(Ctx, (*Args)[2], GChatIdUniverse, 0xd681);
    return true;
}

static bool __cdecl ChatCommand_System(FChatCommandContext* Ctx, std::vector<std::wstring>* Args)
{
    if (Args->size() == 3)
        ChatCommand_Redirect(Ctx, (*Args)[2], GChatIdSystem, 0xd801);
    return true;
}

static bool __cdecl ChatCommand_Local(FChatCommandContext* Ctx, std::vector<std::wstring>* Args)
{
    if (Args->size() == 3)
        ChatCommand_Redirect(Ctx, (*Args)[2], GChatIdLocal, 0x1d9d);
    return true;
}

static bool __cdecl ChatCommand_Group(FChatCommandContext* Ctx, std::vector<std::wstring>* Args)
{
    if (Args->size() == 3)
        ChatCommand_Redirect(Ctx, (*Args)[2], GChatIdGroup, 0xba28);
    return true;
}

// IServerImpl::SubmitChat (0x06d191b0).
void Server_SubmitChat(CHAT_ID Sender, unsigned long Size, const void* Data, CHAT_ID Target, int SystemId)
{
    // 1. The command table, built once.
    if (!GChatCommandsBuilt) {
        // Retail .rdata strings (06d6898c .. 06d6888c):
        // u"^echo\s+(.*)", u"^(invite|i)\s+(.*)", u"^(join|j)\s+(.*)", u"^(leave|lv)",
        // u"^(universe|u)\s+(.*)", u"^(system|s)\s+(.*)", u"^(local|l)\s+(.*)",
        // u"^(group|g)\s+(.*)" -- here with `\s` = ChatCommand_SpaceClass() and
        // `.` = [^\n] (see FChatCommand).
        const std::wstring Space = ChatCommand_SpaceClass();
        GChatCommands.push_back(ChatCommand_Make((L"^echo" + Space + L"+([^\n]*)").c_str(), ChatCommand_Echo));
        GChatCommands.push_back(ChatCommand_Make((L"^(invite|i)" + Space + L"+([^\n]*)").c_str(), ChatCommand_Invite));
        GChatCommands.push_back(ChatCommand_Make((L"^(join|j)" + Space + L"+([^\n]*)").c_str(), ChatCommand_Join));
        GChatCommands.push_back(ChatCommand_Make(L"^(leave|lv)", ChatCommand_Leave));
        GChatCommands.push_back(ChatCommand_Make((L"^(universe|u)" + Space + L"+([^\n]*)").c_str(), ChatCommand_Universe));
        GChatCommands.push_back(ChatCommand_Make((L"^(system|s)" + Space + L"+([^\n]*)").c_str(), ChatCommand_System));
        GChatCommands.push_back(ChatCommand_Make((L"^(local|l)" + Space + L"+([^\n]*)").c_str(), ChatCommand_Local));
        GChatCommands.push_back(ChatCommand_Make((L"^(group|g)" + Space + L"+([^\n]*)").c_str(), ChatCommand_Group));
        GChatCommandsBuilt = true;
    }

    // 2. Group control: {verb, player id}.
    if (Target.Id == GGroupChatSourceId) {
        if (Size != 8)
            return;
        const unsigned int* const Control = static_cast<const unsigned int*>(Data);
        switch (Control[0]) {
        case 0:
            ChatGroup_Invite(Sender.GetPlayerId(), Control[1], nullptr);
            return;
        case 1:
            ChatGroup_Leave(Sender.GetPlayerId());
            return;
        case 2:
            ChatGroup_Join(Sender.GetPlayerId(), Control[1], nullptr);
            return;
        default:
            return;
        }
    }

    // 3. Decode.
    // RETAIL READS AN UNINITIALISED LOCAL HERE: the command pass is gated on the size
    // out-parameter, which extract_text_from_buffer (Common.dll 0x0632a8a0) never writes
    // and nothing in SubmitChat's frame writes first. Retail tests stale stack, in practice
    // non-zero. The port seeds it non-zero, so the gate reduces to `Text[0] == '/'`
    // (docs/re/deviations.md).
    unsigned int DecodedSize = 1;
    GChatReader.extract_text_from_buffer(GChatText, 0x400, DecodedSize,
                                         static_cast<const char*>(Data), Size);

    // 4. The command pass, walking the table BACKWARDS, matching the text after the '/'.
    if (DecodedSize != 0 && GChatText[0] == L'/') {
        FChatCommandContext Ctx;
        Ctx.Sender = Sender;
        Ctx.Target = Target;
        Ctx.Data = Data;
        Ctx.Size = Size;
        Ctx.bPassThrough = false;
        std::list<FChatCommand>::reverse_iterator It = GChatCommands.rbegin();
        for (; It != GChatCommands.rend(); ++It) {
            if (It->match(&Ctx, GChatText + 1)) {
                if (!Ctx.bPassThrough)
                    return;
                Sender = Ctx.Sender;
                Target = Ctx.Target;
                Data = Ctx.Data;
                Size = Ctx.Size;
                break;
            }
        }
    }

    // 5. "<name>: " header.
    RenderDisplayList Rdl;
    const unsigned int SenderWord = Sender.GetPlayerId();
    PlayerData* const SenderSlot = Players.is_valid(SenderWord) ? &Players[SenderWord] : nullptr;
    if (SenderSlot != nullptr) {
        {
            StyleNode Node(static_cast<unsigned short>(0xd5ec));
            Server_RdlAppend(Rdl, Node);
        }
        if (SenderSlot->has_selected_character()) {
            TextNode Node(SenderSlot->get_character_name(), -1);
            Server_RdlAppend(Rdl, Node);
        } else {
            TextNode Node(L"<unnamed>", -1);
            Server_RdlAppend(Rdl, Node);
        }
    } else {
        {
            StyleNode Node(static_cast<unsigned short>(0x6c0e));
            Server_RdlAppend(Rdl, Node);
        }
        {
            TextNode Node(L"CONSOLE", -1);
            Server_RdlAppend(Rdl, Node);
        }
    }
    {
        TextNode Node(L": ", -1);
        Server_RdlAppend(Rdl, Node);
    }
    unsigned int EncodedSize = 0;
    if (!GChatWriter.write_buffer(Rdl, GChatSendBuffer, 0x400, EncodedSize)) {
        ChatNotice(0x2d6, "Failed to encode chat sender name.\n");
        return;
    }

    // 6. Append the caller's payload after the header when it fits (32-bit sum), else a
    // red notice appended to the header list and the whole thing re-encoded.
    if (EncodedSize + Size <= 0x400) {
        memcpy(GChatSendBuffer + EncodedSize, Data, Size);
        EncodedSize = EncodedSize + Size;
    } else {
        // Retail assembles the attribute dword byte by byte on the stack.
        TextRenderAttributes Attrs;
        Attrs.Packed = 0x1d1dbf11u;
        {
            TRANode Node(Attrs, 0xffffffffu, 0u);
            Server_RdlAppend(Rdl, Node);
        }
        {
            TextNode Node(L"<message too big>", -1);
            Server_RdlAppend(Rdl, Node);
        }
        if (!GChatWriter.write_buffer(Rdl, GChatSendBuffer, 0x400, EncodedSize)) {
            ChatNotice(0x2e7, "Failed to encode \"too big chat\" error message.\n");
            return;
        }
    }

    // 7. Decode the final message for the hooks.
    GChatReader.extract_text_from_buffer(GChatText, 0x400, DecodedSize,
                                         GChatSendBuffer, EncodedSize);
    const void* const SendBuffer = GChatSendBuffer;

    // 8. Byte 2 clear: a private message.
    if (((Target.Id >> 16) & 0xffu) == 0) {
        if (g_pPrivateChatHook != nullptr) {
            if (Sender.Id == GChatIdConsole)
                g_pPrivateChatHook(GChatText, static_cast<int>(Target.Id & 0xffffu));
            else if (Target.Id == GChatIdConsole)
                g_pPrivateChatHook(GChatText, static_cast<int>(SenderWord));
        }
        // To the target, when it is a live player ...
        if (Players.is_valid(Target.Id & 0xffffu))
            g_pChatClientInterface->SendCommonChatMsg(Target, Sender, EncodedSize, SendBuffer);
        // ... and always back to the sender.
        g_pChatClientInterface->SendCommonChatMsg(Sender, Target, EncodedSize, SendBuffer);
        return;
    }

    // 9. Universe: admins and the console only.
    if (Target.Id == GChatIdUniverse) {
        if (SenderSlot != nullptr &&
            !static_cast<CAccount*>(SenderSlot->account_ptr)->m_bAdmin) {   // CAccount +0x32
            ChatNotice(0x31e, "Universe chat attempted from an account without the proper rights.\n");
            return;
        }
        if (g_pUniverseChatHook != nullptr)
            g_pUniverseChatHook(GChatText, static_cast<int>(SenderWord));
        for (PlayerData* Slot = Players.traverse_active(nullptr); Slot != nullptr;
             Slot = Players.traverse_active(Slot)) {
            g_pChatClientInterface->SendCommonChatMsg(CHAT_ID(MakeChatId(Slot->client_id)), Target,
                                                      EncodedSize, SendBuffer);
        }
        return;
    }

    // 10. System: the sender's system and/or SystemId.
    if (Target.Id == GChatIdSystem) {
        if (SenderSlot == nullptr && SystemId == -1) {
            ChatNotice(0x340, "System chat attempted without a player or console system.\n");
            return;
        }
        // Retail fetches the first active slot before calling the hook.
        PlayerData* Slot = Players.traverse_active(nullptr);
        if (g_pSystemChatHook != nullptr)
            g_pSystemChatHook(GChatText, SenderSlot != nullptr
                                             ? static_cast<int>(SenderSlot->system_id) : SystemId);
        for (; Slot != nullptr; Slot = Players.traverse_active(Slot)) {
            if ((SenderSlot != nullptr && Slot->system_id == SenderSlot->system_id) ||
                static_cast<int>(Slot->system_id) == SystemId) {
                g_pChatClientInterface->SendCommonChatMsg(CHAT_ID(MakeChatId(Slot->client_id)), Target,
                                                          EncodedSize, SendBuffer);
            }
        }
        return;
    }

    // 11. Local: the players whose objects the sender's own view has already created on his
    // client, then the sender himself.
    if (Target.Id == GChatIdLocal) {
        if (SenderSlot == nullptr) {
            ChatNotice(0x36c, "Local chat attempted from console.\n");
            return;
        }
        if (g_pSystemChatHook != nullptr)
            g_pSystemChatHook(GChatText, static_cast<int>(SenderSlot->system_id));
        SysDB::SysMapType::iterator Found = SysDB::SysMap.find(SenderSlot->system_id);   // FUN_06d15d10
        if (Found != SysDB::SysMap.end()) {
            std::set<unsigned int> Owners;                     // FUN_06d1b150
            Found->second.collect_created_owners(SenderWord, &Owners);
            std::set<unsigned int>::const_iterator It = Owners.begin();
            for (; It != Owners.end(); ++It) {
                g_pChatClientInterface->SendCommonChatMsg(CHAT_ID(MakeChatId(*It)), Target,
                                                          EncodedSize, SendBuffer);
            }
        }
        g_pChatClientInterface->SendCommonChatMsg(Sender, Target, EncodedSize, SendBuffer);
        return;
    }

    // 12. Group.
    if (Target.Id == GChatIdGroup) {
        if (SenderSlot != nullptr && SenderSlot->group_ptr != nullptr) {
            static_cast<CPlayerGroup*>(SenderSlot->group_ptr)->DeliverChat(EncodedSize, GChatSendBuffer);
        } else {
            ChatNotice(0x37a, "Console group chat attempted. Feature is not implemented.\n");
        }
        return;
    }

    ChatNotice(0x381, "Received message to unknown chat list. Ignoring.\n");
}

    // IChatServer::SubmitChat -- retail 0x06d191b0. Retail's FLChatServer.cpp lives in
    // server_chat.cpp; the body touches no member of IServerImpl.
void IServerImpl::SubmitChat(CHAT_ID Sender, unsigned long Size, const void* Data, CHAT_ID Target, int SystemId) {
        Server_SubmitChat(Sender, Size, Data, Target, SystemId);
    }

// Chat hooks -- RVAs 0xAF604, 0xAF608, 0xAF60C. Retail type void (__cdecl*)(unsigned short*,
// int) (VC6's wchar_t); the text is the decoded chat message (DAT_06d8e9b0).
typedef void (__cdecl *ChatHookFn)(wchar_t*, int);

SERVER_API ChatHookFn g_pUniverseChatHook = nullptr;  // RVA 0xAF604

SERVER_API ChatHookFn g_pPrivateChatHook = nullptr;   // RVA 0xAF608

SERVER_API ChatHookFn g_pSystemChatHook = nullptr;    // RVA 0xAF60C
