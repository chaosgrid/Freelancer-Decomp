// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// The client chat core (retail NN_Chat.cpp) -- see chat_init.h for the object map.

#include "NeuroNet/Chat/chat_init.h"
#include "Core/event_handler.h"

#include "Interface/Pages/character_select_equip.h"   // GetPlayerRepVibeRetail (FUN_004c4670)
#include "NeuroNet/Chat/chat_msg_win.h"
#include "NeuroNet/Chat/chat_text_input.h"          // ChatTextInputDisplay ("CTID")
#include "Space/HUD/hud_command_dispatch.h"     // EventHandler::RegisterEvent / ~EventHandler
#include "Common/Fonts/ids_rdl.h"
#include "Common/Fonts/ids_strings.h"              // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "Interface/menu_ui_widgets.h"
#include "NeuroNet/nn_group_control.h"
#include "NeuroNet/nn_ui_pulse.h"              // G_HudChromeColorRgba (DAT_00679bb4), G_NnPaletteAmber
#include "Space/player_spawn.h"             // GetPlayerConnectionId (DAT_00673344)
#include "Interface/rich_text_win.h"
#include "Interface/win_factory.h"
#include "Interface/win3d.h"

#include "common_archetype.h"          // Archetype::Ship (Root +0x14 ids_name)
#include "common_reputation.h"
#include "common_misc.h"              // SinglePlayer
#include "common_universe.h"           // Universe::get_system / get_base, Archetype::GetShip
#include "dacom.h"

#include <ctime>
#include <string>
#include <string.h>

const CHAT_ID G_ChatIdChannel3(0x00010003u);
const CHAT_ID G_ChatIdChannel2(0x00010002u);
const CHAT_ID G_ChatIdChannel1(0x00010001u);
const CHAT_ID G_ChatIdChannel0(0x00010000u);
const CHAT_ID G_ChatIdNone(0x00000000u);
const CHAT_ID G_ChatIdGroupInvite(0x00010004u);

CHAT_ID G_ChatInputTarget;

namespace
{
    // The compiled-in chat text styles FUN_00468c20 registers. Each is a TextRenderAttributes
    // seeded by its own CRT initializer (LAB_0045df30 .. LAB_0045e250, XC table 0x0060f43c..
    // 0x0060f4a0): byte +0 = the flags byte, the dword at +1 = (old & 0xff000000) | RGB over
    // zeroed .data -- so Packed = flags | RGB << 8. Retail spaces them 8 bytes apart; only the
    // first dword is ever read (StyleCollection::set_style copies Packed).
    TextRenderAttributes G_ChatStyleAttrs_d5ec = {0xffffff10u}; // DAT_0066da14, LAB_0045df30
    TextRenderAttributes G_ChatStyleAttrsConsole = {0x1dbf3b16u}; // DAT_0066da0c, LAB_0045df50
    TextRenderAttributes G_ChatStyleAttrsUniverse = {0xffffff00u}; // DAT_0066da04, LAB_0045df70
    TextRenderAttributes G_ChatStyleAttrsSystem = {0xe0c38700u}; // DAT_0066d9fc, LAB_0045df90
    TextRenderAttributes G_ChatStyleAttrsLocal = {0x1dbf3b00u}; // DAT_0066d9f4, LAB_0045dfb0
    TextRenderAttributes G_ChatStyleAttrsPlayer = {0x1dbf3b02u}; // DAT_0066d9ec, LAB_0045dfd0
    TextRenderAttributes G_ChatStyleAttrs_e784 = {0xffffff01u}; // DAT_0066d9e4, LAB_0045dff0
    TextRenderAttributes G_ChatStyleAttrsNotice = {0x1d1dbf01u}; // DAT_0066d9dc, LAB_0045e010
    TextRenderAttributes G_ChatStyleAttrsGroup = {0xfd7eff00u}; // DAT_0066d9d4, LAB_0045e030
    TextRenderAttributes G_ChatStyleAttrs_66b7 = {0x52eaf501u}; // DAT_0066d9cc, LAB_0045e050
    TextRenderAttributes G_ChatStyleAttrs_2b34 = {0x52eaf506u}; // DAT_0066d9c4, LAB_0045e070
    TextRenderAttributes G_ChatStyleAttrs_66ac = {0xffffff00u}; // DAT_0066d9bc, LAB_0045e090
    TextRenderAttributes G_ChatStyleAttrsButton = {0xe0c38708u}; // DAT_0066d9b4, LAB_0045e0b0
    TextRenderAttributes G_ChatStyleAttrsScreenTitle = {0xe0c38718u}; // DAT_0066d9ac, LAB_0045e0d0
    TextRenderAttributes G_ChatStyleAttrsScreenSubtitle = {0xe0c38718u}; // DAT_0066d9a4, LAB_0045e0f0
    TextRenderAttributes G_ChatStyleAttrsColumnHeader = {0xe0c38708u}; // DAT_0066d99c, LAB_0045e110
    TextRenderAttributes G_ChatStyleAttrsColumnHeaderSorted = {0x52eaf508u}; // DAT_0066d994, LAB_0045e130
    TextRenderAttributes G_ChatStyleAttrsRow = {0xe0c38700u}; // DAT_0066d98c, LAB_0045e150
    TextRenderAttributes G_ChatStyleAttrsRowSelected = {0x52eaf500u}; // DAT_0066d984, LAB_0045e170
    TextRenderAttributes G_ChatStyleAttrsDisabled = {0xb6b6b600u}; // DAT_0066d97c, LAB_0045e190
    TextRenderAttributes G_ChatStyleAttrsRowDetail = {0xe0c38708u}; // DAT_0066d974, LAB_0045e1b0
    TextRenderAttributes G_ChatStyleAttrsRowDetailSelected = {0x52eaf508u}; // DAT_0066d96c, LAB_0045e1d0
    TextRenderAttributes G_ChatStyleAttrs_867f = {0xb6b6b608u}; // DAT_0066d964, LAB_0045e1f0
    TextRenderAttributes G_ChatStyleAttrsBody = {0xe0c38700u}; // DAT_0066d95c, LAB_0045e210
    TextRenderAttributes G_ChatStyleAttrs_686c = {0xe0c38728u}; // DAT_0066d954, LAB_0045e230
    TextRenderAttributes G_ChatStyleAttrsCardTitle = {0xe0c38701u}; // DAT_0066d94c, LAB_0045e250

    // FUN_00468c20's colour copy: bytes 0..2 of a palette colour into bytes 1..3 of the
    // attributes; the flags byte stays.
    void ChatStyleTakeColor(TextRenderAttributes& Attrs, unsigned long Rgba)
    {
        Attrs.Packed = (Attrs.Packed & 0xffu) | ((Rgba & 0x00ffffffu) << 8);
    }
}

void ChatRegisterTextStyles()
{
    // FUN_00468c20.
    StyleCollection::set_style(0xd5ec, G_ChatStyleAttrs_d5ec);
    StyleCollection::set_style(kTextStyleChatConsole, G_ChatStyleAttrsConsole);
    StyleCollection::set_style(kTextStyleChatUniverse, G_ChatStyleAttrsUniverse);
    StyleCollection::set_style(kTextStyleChatSystem, G_ChatStyleAttrsSystem);
    StyleCollection::set_style(kTextStyleChatLocal, G_ChatStyleAttrsLocal);
    StyleCollection::set_style(kTextStyleChatPlayer, G_ChatStyleAttrsPlayer);
    StyleCollection::set_style(0xe784, G_ChatStyleAttrs_e784);
    StyleCollection::set_style(kTextStyleNotice, G_ChatStyleAttrsNotice);
    StyleCollection::set_style(kTextStyleChatGroup, G_ChatStyleAttrsGroup);
    StyleCollection::set_style(0x66b7, G_ChatStyleAttrs_66b7);
    StyleCollection::set_style(0x2b34, G_ChatStyleAttrs_2b34);
    StyleCollection::set_style(0x66ac, G_ChatStyleAttrs_66ac);
    StyleCollection::set_style(kTextStyleButton, G_ChatStyleAttrsButton);
    StyleCollection::set_style(kTextStyleScreenTitle, G_ChatStyleAttrsScreenTitle);
    StyleCollection::set_style(kTextStyleScreenSubtitle, G_ChatStyleAttrsScreenSubtitle);
    StyleCollection::set_style(kTextStyleColumnHeader, G_ChatStyleAttrsColumnHeader);
    StyleCollection::set_style(kTextStyleColumnHeaderSorted, G_ChatStyleAttrsColumnHeaderSorted);
    StyleCollection::set_style(kTextStyleRow, G_ChatStyleAttrsRow);
    StyleCollection::set_style(kTextStyleRowSelected, G_ChatStyleAttrsRowSelected);
    StyleCollection::set_style(kTextStyleDisabled, G_ChatStyleAttrsDisabled);
    StyleCollection::set_style(kTextStyleRowDetail, G_ChatStyleAttrsRowDetail);
    StyleCollection::set_style(kTextStyleRowDetailSelected, G_ChatStyleAttrsRowDetailSelected);
    StyleCollection::set_style(0x867f, G_ChatStyleAttrs_867f);

    // Four styles take the HUD chrome colour (DAT_00679bb4), and one more reuses the body
    // record (DAT_0066d95c) with the pulse amber (DAT_00679ba4).
    unsigned long const Chrome = static_cast<unsigned long>(G_HudChromeColorRgba[0]) |
                                 (static_cast<unsigned long>(G_HudChromeColorRgba[1]) << 8) |
                                 (static_cast<unsigned long>(G_HudChromeColorRgba[2]) << 16);
    ChatStyleTakeColor(G_ChatStyleAttrsBody, Chrome);
    StyleCollection::set_style(kTextStyleBody, G_ChatStyleAttrsBody);
    StyleCollection::set_style(0x76ec, G_ChatStyleAttrsBody);
    ChatStyleTakeColor(G_ChatStyleAttrs_686c, Chrome);
    StyleCollection::set_style(0x686c, G_ChatStyleAttrs_686c);
    ChatStyleTakeColor(G_ChatStyleAttrsCardTitle, Chrome);
    StyleCollection::set_style(kTextStyleCardTitle, G_ChatStyleAttrsCardTitle);
    ChatStyleTakeColor(G_ChatStyleAttrsBody, G_NnPaletteAmber);
    StyleCollection::set_style(kTextStyleBodyHighlight, G_ChatStyleAttrsBody);

    // Style 0x4433 is a list; retail clones each node twice and deletes the first clone.
    RenderDisplayList Style;
    {
        TextRenderAttributes Attrs;
        Attrs.Packed = 0xe0c38708u;
        TRANode Tra(Attrs, 0xffffffffu, 0);
        RDLNode* const TraClone = Tra.Clone();
        RdlAppendNode(Style, TraClone->Clone());
        delete TraClone;
    }
    {
        JustifyNode Justify(static_cast<TextJustify>(1));
        RDLNode* const JustifyClone = Justify.Clone();
        RdlAppendNode(Style, JustifyClone->Clone());
        delete JustifyClone;
    }
    StyleCollection::set_style(kTextStyleList, Style);
}

FChatMessageStore* G_ChatMessageStore = nullptr;
FChatDisplaySnapshot* G_ChatAllMessages = nullptr;
FChatDisplaySnapshot* G_ChatRecentMessages = nullptr;
std::map<unsigned, RenderDisplayList*>* G_ChatSystemNameCache = nullptr;
std::map<unsigned, RenderDisplayList*>* G_ChatShipArchNameCache = nullptr;
std::map<unsigned, RenderDisplayList*>* G_ChatBaseNameCache = nullptr;
FChatChannelTable* G_ChatChannelTable = nullptr;
MsgWin* G_ChatMsgWin = nullptr;
NnGroupControl* G_ChatGroupControl = nullptr;
bool G_bChatMsgWinWanted = false;

RenderDisplayList G_ChatPrefixNewPlayer;
RenderDisplayList G_ChatPrefixDepartingPlayer;
RenderDisplayList G_ChatPrefixNewAffiliation;
RenderDisplayList G_ChatPrefixLostAffiliation;

// DAT_0066d8e4 -- ChatTextInputDisplay's Draw builds "chat.txm" under it.
std::string G_ChatImageRoot;

namespace
{
    const char kNnChatCpp[] = "E:\\FL\\Scratch\\Source\\Client\\nn\\NN_Chat.cpp";

    // DAT_0066d430 / DAT_0066d43c
    RenderDisplayList G_ChatServerMotd;
    int G_ChatServerMotdVersion = 0;

    // DAT_0066da30 -- static initializer LAB_0045ded0: DAT_005ceedc (0.46) * DAT_005cef60 (-0.5),
    // the input line's X. (LAB_0045def0 copies it into DAT_0066da2c, which nothing reads.)
    float const G_ChatInputX = 0.46f * -0.5f;

    // The clock()/1000*1000 stamp every chat path computes (0x10624dd3 multiply = signed /1000).
    int ChatNowMsRounded()
    {
        int const Ticks = static_cast<int>(clock());
        return (Ticks / 1000) * 1000;
    }

    void ChatNotice(int Line, char const* Message)
    {
        FDUMP(kFdumpNotice, "%s(%d) : NOTICE:General:%s", kNnChatCpp, Line, Message);
    }

    // FUN_004690c0's prefix idiom (four times): StyleNode 0x3ea8, then the IDS text with its
    // trailing paragraph stripped, or JustifyNode(1) + the debug text.
    void BuildPrefix(RenderDisplayList& Prefix, unsigned Ids, wchar_t const* DebugText)
    {
        StyleNode Style(kTextStyleNotice);
        RdlAppendNode(Prefix, Style.Clone());
        LoadIdsTextIntoRdl(Ids, Prefix, DebugText);
    }

    // FUN_00469d20 -- every cached name list deleted, then the map itself.
    void DestroyNameCache(std::map<unsigned, RenderDisplayList*>*& Cache)
    {
        if (Cache == nullptr)
            return;
        for (std::map<unsigned, RenderDisplayList*>::iterator It = Cache->begin();
             It != Cache->end(); ++It)
        {
            delete It->second;
            It->second = nullptr;
        }
        delete Cache;
        Cache = nullptr;
    }

    // The first step of FUN_00466770 / FUN_0046b6e0 / FUN_0046b8b0: FUN_00432240 find; a hit
    // returns the node's list.
    RenderDisplayList* CacheFindName(std::map<unsigned, RenderDisplayList*>& Cache, unsigned Id)
    {
        std::map<unsigned, RenderDisplayList*>::iterator const Found = Cache.find(Id);
        if (Found != Cache.end())
            return Found->second;
        return nullptr;
    }

    // The miss path of the same three: a new list filled from Ids (bHaveIds false = no object)
    // with the "????" fallback, inserted under Id. The object lookup happens after the find and
    // the allocation in retail, and the callers keep that order.
    RenderDisplayList* CacheNewName(std::map<unsigned, RenderDisplayList*>& Cache, unsigned Id,
                                    RenderDisplayList* Name, bool bHaveIds, unsigned Ids)
    {
        if (!bHaveIds || !LoadIdsIntoRdl(Ids, *Name))
        {
            RdlClear(*Name);
            TextPtrNode Unknown(L"????", -1);
            RdlAppendNode(*Name, Unknown.Clone());
        }
        // FUN_0046d6d0 insert {Id, 0}, then the node's value = the list.
        Cache[Id] = Name;
        return Name;
    }

    // The RDLRef + ParagraphNode pair both rebuilds append per message.
    void AppendMessageLine(RenderDisplayList& Display, RenderDisplayList& Body)
    {
        RDLRefNode Ref(Body);
        RdlAppendNode(Display, Ref.Clone());
        ParagraphNode Para;
        RdlAppendNode(Display, Para.Clone());
    }

    // FUN_0046a4d0 / FUN_0046a770 body: prefix ref + TextNode(Name), then commit.
    void PostNotice(RenderDisplayList& Prefix, wchar_t const* Name)
    {
        FChatMessageEntry* const Entry = ChatAcquireEntry();
        RDLRefNode Ref(Prefix);
        RdlAppendNode(Entry->Body, Ref.Clone());
        TextNode Text(Name, -1);
        RdlAppendNode(Entry->Body, Text.Clone());
        ChatCommitEntries();
    }
}

// ---------------------------------------------------------------------------------------------
// Shared idioms

FChatMessageEntry* ChatScratchEntry()
{
    // DAT_0066d440 under the DAT_0066d44c bit-0 guard; atexit LAB_00468bf0 destroys it.
    static FChatMessageEntry Scratch;
    return &Scratch;
}

FChatMessageEntry* ChatAcquireEntry()
{
    // FUN_00468a80, also inlined at every site: the scratch entry is constructed whether or not
    // it is returned.
    FChatMessageEntry* const Scratch = ChatScratchEntry();
    if (G_ChatMessageStore != nullptr)
        return G_ChatMessageStore->Acquire();
    return Scratch;
}

void ChatCommitEntries()
{
    if (G_ChatMessageStore == nullptr)
        return;
    G_ChatMessageStore->RebuildAll();
    G_ChatMessageStore->RebuildRecent();
}

void ChatAppendRdlCopy(RenderDisplayList& Dst, RenderDisplayList const& Source)
{
    // The FUN_0046c800 loop.
    if (Source.Count == 0)
        return;
    FRdlListNode* const Sentinel = Source.NodeList;
    for (FRdlListNode* Cell = Sentinel->Next; Cell != Sentinel; Cell = Cell->Next)
    {
        RdlAppendNode(Dst, Cell->Node->Clone());
    }
}

void ChatAppendNodeClone(RenderDisplayList& List, RDLNode const& Node)
{
    RDLNode* const Temp = Node.Clone();
    RdlAppendNode(List, Temp->Clone());
    delete Temp;
}

// ---------------------------------------------------------------------------------------------
// FChatChannelTable

FChatChannelTable::FChatChannelTable()
    : Generation(0)
{
}

FChatChannelTable::~FChatChannelTable()
{
    // FUN_00469e60.
    for (FNameMap::iterator It = Names.begin(); It != Names.end(); ++It)
    {
        delete It->second;
    }
    Names.clear();
    PlayerInfo.clear();
    Generation = 0;
}

void FChatChannelTable::AddChannel(CHAT_ID Id, RenderDisplayList const& Name)
{
    FNameMap::iterator It = Names.find(Id.Id);
    if (It == Names.end())
    {
        RenderDisplayList* const Copy = new RenderDisplayList();
        ChatAppendRdlCopy(*Copy, Name);
        Names[Id.Id] = Copy;
        FChatPlayerInfo& Info = PlayerInfo[Id.Id];
        Info.GroupId = 0;
        Info.Rank = 0;
        Info.SystemId = 0;
        ++Generation;
        ChatNotice(0x552, "Channel add successful.\n");
        return;
    }

    ChatNotice(0x558, "Channel add attempted on existing channel. Modifying instead.\n");
    RenderDisplayList* const Existing = Names[Id.Id];
    RdlClear(*Existing);
    ChatAppendRdlCopy(*Existing, Name);
    FChatPlayerInfo& Info = PlayerInfo[Id.Id];
    Info.GroupId = 0;
    Info.Rank = 0;
    Info.SystemId = 0;
    ++Generation;
}

void FChatChannelTable::RemovePlayerChannels()
{
    // FUN_0045fb40.
    FNameMap::iterator It = Names.begin();
    while (It != Names.end())
    {
        unsigned const Key = It->first;
        if (((Key >> 16) & 0xffu) != 0 || Key == G_ChatIdNone.Id)
        {
            ++It;
            continue;
        }
        PlayerInfo.erase(Key);
        delete It->second;
        It = Names.erase(It);
    }
    ++Generation;
}

RenderDisplayList* FChatChannelTable::FindName(CHAT_ID Id) const
{
    FNameMap::const_iterator It = Names.find(Id.Id);
    if (It == Names.end())
        return nullptr;
    return It->second;
}

FChatPlayerInfo* FChatChannelTable::FindPlayerInfo(CHAT_ID Id)
{
    std::map<unsigned, FChatPlayerInfo>::iterator It = PlayerInfo.find(Id.Id);
    if (It == PlayerInfo.end())
        return nullptr;
    return &It->second;
}

// ---------------------------------------------------------------------------------------------
// FChatMessageStore

FChatMessageStore::FChatMessageStore(int Capacity, int InMaxCount, int InMaxAgeMs)
    : LastClockMs(0)
    , MaxAgeMs(InMaxAgeMs)
    , MaxCount(InMaxCount)
{
    // FUN_004699d0.
    for (int Index = 0; Index < Capacity; ++Index)
        Free.push_back(new FChatMessageEntry());
}

FChatMessageStore::~FChatMessageStore()
{
    // FUN_004688b0.
    for (std::list<FChatMessageEntry*>::iterator It = Active.begin(); It != Active.end(); ++It)
    {
        FChatMessageEntry* const Entry = *It;
        RdlClear(Entry->Body);
        Entry->TimestampMs = 0;
        Free.push_back(Entry);
    }
    Active.clear();
    RebuildAll();
    RebuildRecent();
    for (std::list<FChatMessageEntry*>::iterator It = Free.begin(); It != Free.end(); ++It)
        delete *It;
    Free.clear();
}

void FChatMessageStore::RecycleOldest()
{
    // FUN_0046a300.
    if (Active.empty())
        return;
    FChatMessageEntry* const Oldest = Active.front();
    RdlClear(Oldest->Body);
    Oldest->TimestampMs = 0;
    Free.push_back(Oldest);
    Active.pop_front();
    RebuildAll();
    RebuildRecent();
}

FChatMessageEntry* FChatMessageStore::Acquire()
{
    // FUN_004683b0 (FUN_0046a300 inlined). The pool is preloaded with Capacity entries and an
    // entry only ever moves between the two lists, so the front always exists.
    if (Free.empty() && !Active.empty())
        RecycleOldest();
    FChatMessageEntry* const Entry = Free.front();
    RdlClear(Entry->Body);
    Entry->TimestampMs = ChatNowMsRounded();
    Free.pop_front();
    Active.push_back(Entry);
    return Entry;
}

void FChatMessageStore::RebuildRecent()
{
    // FUN_004684e0: walk newest -> oldest while the message is within MaxAgeMs and no more than
    // MaxCount have been kept; LastClockMs ends on the oldest kept stamp (or now).
    int const NowMs = ChatNowMsRounded();
    LastClockMs = NowMs;

    int Kept = 0;
    std::list<FChatMessageEntry*>::reverse_iterator It = Active.rbegin();
    for (; It != Active.rend(); ++It)
    {
        FChatMessageEntry* const Entry = *It;
        unsigned const Age = static_cast<unsigned>(NowMs - Entry->TimestampMs);
        if (static_cast<unsigned>(MaxAgeMs) < Age || MaxCount < Kept)
            break;
        LastClockMs = Entry->TimestampMs;
        ++Kept;
    }

    // Rebuilt only when something is kept or the snapshot was non-empty.
    if (Kept < 1 && Recent.Display.Count == 0)
        return;
    RdlClear(Recent.Display);
    ++Recent.Generation;
    for (std::list<FChatMessageEntry*>::iterator Fwd = It.base(); Fwd != Active.end(); ++Fwd)
        AppendMessageLine(Recent.Display, (*Fwd)->Body);
}

void FChatMessageStore::RebuildAll()
{
    // FUN_004686e0.
    RdlClear(All.Display);
    for (std::list<FChatMessageEntry*>::iterator It = Active.begin(); It != Active.end(); ++It)
        AppendMessageLine(All.Display, (*It)->Body);
    ++All.Generation;
}

bool FChatMessageStore::Discard(FChatMessageEntry* Entry)
{
    for (std::list<FChatMessageEntry*>::iterator It = Active.begin(); It != Active.end(); ++It)
    {
        if (*It != Entry)
            continue;
        RdlClear(Entry->Body);
        Entry->TimestampMs = 0;
        Free.push_back(Entry);
        Active.erase(It);
        return true;
    }
    return false;
}

void FChatMessageStore::DiscardAll()
{
    for (std::list<FChatMessageEntry*>::iterator It = Active.begin(); It != Active.end(); ++It)
    {
        FChatMessageEntry* const Entry = *It;
        RdlClear(Entry->Body);
        Entry->TimestampMs = 0;
        Free.push_back(Entry);
    }
    Active.clear();
    RebuildAll();
    RebuildRecent();
}

// ---------------------------------------------------------------------------------------------
// Init / shutdown

void ChatInit()
{
    G_ChatImageRoot.assign("Interface\\NeuroNet\\Chat\\");

    // The three name caches (FUN_004c5a10).
    G_ChatSystemNameCache = new std::map<unsigned, RenderDisplayList*>();
    G_ChatShipArchNameCache = new std::map<unsigned, RenderDisplayList*>();
    G_ChatBaseNameCache = new std::map<unsigned, RenderDisplayList*>();

    G_ChatMessageStore = new FChatMessageStore(kChatMessageStoreCapacity,
                                               kChatMessageStoreMaxCount,
                                               kChatMessageStoreMaxAgeMs);
    G_ChatAllMessages = &G_ChatMessageStore->All;
    G_ChatRecentMessages = &G_ChatMessageStore->Recent;
    G_ChatInputTarget = G_ChatIdChannel1;

    BuildPrefix(G_ChatPrefixNewPlayer, 0x60052u, L"DEBUG - New player:");
    BuildPrefix(G_ChatPrefixDepartingPlayer, 0x60053u, L"DEBUG - Departing player:");
    BuildPrefix(G_ChatPrefixNewAffiliation, 0x6004fu, L"DEBUG - New affiliation:");
    BuildPrefix(G_ChatPrefixLostAffiliation, 0x60050u, L"DEBUG - Lost affiliation:");

    G_ChatChannelTable = new FChatChannelTable();

    // Three channel names. The IDS text goes in as loaded (no FUN_0057eb40) and only when it
    // loaded -- no debug fallback here.
    {
        RenderDisplayList Label;
        if (LoadIdsIntoRdl(0x6003du, Label))
            G_ChatChannelTable->AddChannel(G_ChatIdChannel1, Label);
    }
    {
        RenderDisplayList Label;
        if (LoadIdsIntoRdl(0x60036u, Label))
            G_ChatChannelTable->AddChannel(G_ChatIdChannel3, Label);
    }
    {
        RenderDisplayList Label;
        if (LoadIdsIntoRdl(0x60035u, Label))
            G_ChatChannelTable->AddChannel(G_ChatIdNone, Label);
    }

    G_ChatGroupControl = static_cast<NnGroupControl*>(
        WinFactoryCreate("NN_GroupControl", "NN_GroupControl", nullptr, nullptr));
    if (G_ChatGroupControl != nullptr)
    {
        Vector const Position = {0.0f, 0.0f, -1.0f};
        G_ChatGroupControl->SetPosition(&Position);
        G_ChatGroupControl->Flags = static_cast<unsigned char>(G_ChatGroupControl->Flags & ~(kWin3DActive | kWin3DVisible));
    }
}

void ChatShutdown()
{
    // FUN_00469b30
    if (G_ChatGroupControl != nullptr)
    {
        // FUN_0043d2a0 inlined.
        Win3DDeleteTree(G_ChatGroupControl);
        G_ChatGroupControl = nullptr;
    }
    if (G_ChatMsgWin != nullptr)
    {
        Win3DDeleteTree(G_ChatMsgWin);
        G_ChatMsgWin = nullptr;
    }
    if (G_ChatChannelTable != nullptr)
    {
        delete G_ChatChannelTable;
        G_ChatChannelTable = nullptr;
    }
    if (G_ChatMessageStore != nullptr)
    {
        delete G_ChatMessageStore;
        G_ChatMessageStore = nullptr;
        G_ChatAllMessages = nullptr;
        G_ChatRecentMessages = nullptr;
    }
    DestroyNameCache(G_ChatSystemNameCache);
    DestroyNameCache(G_ChatShipArchNameCache);
    DestroyNameCache(G_ChatBaseNameCache);
}

// ---------------------------------------------------------------------------------------------
// Incoming chat

void ChatDispatchIncoming(CHAT_ID Sender, void const* Buffer, unsigned SizeBytes)
{
    // FUN_0046a150
    if (Sender.Id == G_ChatIdGroupInvite.Id)
    {
        if (G_ChatGroupControl != nullptr)
            G_ChatGroupControl->HandleInviteMessage(Buffer, SizeBytes);
        return;
    }

    FChatMessageEntry* const Entry = ChatAcquireEntry();

    BinaryRDLReader Reader;
    if (!Reader.read_buffer(Entry->Body, static_cast<char const*>(Buffer), SizeBytes))
    {
        if (G_ChatMessageStore != nullptr)
            G_ChatMessageStore->Discard(Entry);
        ChatNotice(0x1249, "Failed to decode a chat message!\n");
        return;
    }
    ChatCommitEntries();
}

// ---------------------------------------------------------------------------------------------
// Players

void ChatPlayerJoined(wchar_t const* Name, unsigned PlayerId, bool bNotify)
{
    // FUN_0046aa10
    if (G_ChatChannelTable != nullptr)
    {
        RenderDisplayList Label;
        TextNode Text(Name, -1);
        RdlAppendNode(Label, Text.Clone());
        G_ChatChannelTable->AddChannel(ChatIdForPlayer(PlayerId), Label);
    }
    if (!bNotify)
        return;

    FChatMessageEntry* const Entry = ChatAcquireEntry();
    RDLRefNode Ref(G_ChatPrefixNewPlayer);
    RdlAppendNode(Entry->Body, Ref.Clone());
    TextNode Text(Name, -1);
    RdlAppendNode(Entry->Body, Text.Clone());
    ChatCommitEntries();
}

void ChatPlayerLeft(unsigned PlayerId, bool bNotify)
{
    // FUN_0046aeb0
    CHAT_ID const Key = ChatIdForPlayer(PlayerId);
    FChatChannelTable* Table = G_ChatChannelTable;
    if (Table == nullptr)
        return;
    RenderDisplayList* const Name = Table->FindName(Key);
    if (Name == nullptr)
        return;

    if (bNotify)
    {
        FChatMessageEntry* const Entry = ChatAcquireEntry();
        RDLRefNode Ref(G_ChatPrefixDepartingPlayer);
        RdlAppendNode(Entry->Body, Ref.Clone());
        ChatAppendRdlCopy(Entry->Body, *Name);
        ChatCommitEntries();
    }

    // Retail reloads DAT_0066da7c and looks the key up again.
    Table = G_ChatChannelTable;
    FChatChannelTable::FNameMap::iterator It = Table->Names.find(Key.Id);
    if (It == Table->Names.end())
        return;
    Table->PlayerInfo.erase(Key.Id);
    delete It->second;
    Table->Names.erase(It);
    ++Table->Generation;
}

void ChatSetPlayerGroupId(unsigned PlayerId, unsigned Value)
{
    // FUN_0046ad60
    if (G_ChatChannelTable == nullptr)
        return;
    FChatPlayerInfo* const Info = G_ChatChannelTable->FindPlayerInfo(ChatIdForPlayer(PlayerId));
    if (Info == nullptr)
        return;
    Info->GroupId = Value;
    ++G_ChatChannelTable->Generation;
}

void ChatSetPlayerRank(unsigned PlayerId, unsigned Rank)
{
    // FUN_0046add0
    if (G_ChatChannelTable == nullptr)
        return;
    FChatPlayerInfo* const Info = G_ChatChannelTable->FindPlayerInfo(ChatIdForPlayer(PlayerId));
    if (Info == nullptr)
        return;
    Info->Rank = Rank;
    ++G_ChatChannelTable->Generation;
}

void ChatSetPlayerSystemId(unsigned PlayerId, unsigned Value)
{
    // FUN_0046ae40
    if (G_ChatChannelTable == nullptr)
        return;
    FChatPlayerInfo* const Info = G_ChatChannelTable->FindPlayerInfo(ChatIdForPlayer(PlayerId));
    if (Info == nullptr)
        return;
    Info->SystemId = Value;
    ++G_ChatChannelTable->Generation;
}

RenderDisplayList* ChatFindPlayerName(unsigned PlayerId)
{
    // FUN_0046b400: the argument dword is the key as passed (no FUN_0046ad40 here).
    return G_ChatChannelTable->FindName(CHAT_ID(PlayerId));
}

void ChatPostLostAffiliation(wchar_t const* Name)
{
    // FUN_0046a4d0 -- prefix DAT_0066d400.
    PostNotice(G_ChatPrefixLostAffiliation, Name);
}

void ChatPostNewAffiliation(wchar_t const* Name)
{
    // FUN_0046a770 -- prefix DAT_0066d40c.
    PostNotice(G_ChatPrefixNewAffiliation, Name);
}

// ---------------------------------------------------------------------------------------------
// Windows

bool ChatIsInputOpen()
{
    // FUN_0046a480. The name is the string at 0x005cf04c; Ghidra shows only its first byte as
    // "C".
    return Win3DFindByName("CTID") != nullptr;
}

void ChatSetInputOpen(bool bOpen)
{
    // FUN_0046a3c0
    Win3D* Input = Win3DFindByName("CTID");   // FUN_0059da10, name at 0x005cf04c
    if (bOpen)
    {
        if (Input == nullptr)
        {
            Input = WinFactoryCreate("CTID", "ChatTextInputDisplay", nullptr, nullptr);
            if (Input == nullptr)
                return;
            Vector const Position = {G_ChatInputX, -0.265f, -1.0f};   // DAT_005ceee4
            Input->SetPosition(&Position);
        }
        ChatTextInputDisplay* const Line = static_cast<ChatTextInputDisplay*>(Input);
        Line->bCloseAfterSend = 1;
        Line->SetTarget(G_ChatInputTarget);
        Line->Activate();
        Line->SetMaxLength(0x50);
        Line->SetLayoutFlag(true);
        Line->SetKeepScrolledToEnd(true);
        return;
    }
    if (Input != nullptr)
    {
        ChatTextInputDisplay* const Line = static_cast<ChatTextInputDisplay*>(Input);
        Line->ClearText();
        Line->Deactivate(true);
    }
}

namespace
{
    // DAT_0066d3f8: the chat key-command handler (vftable 005cfe7c, static initializer
    // LAB_0046a0a0).
    unsigned const kChatOpenCommand = 0x5d; // the key command id FUN_0046a0c0 registers

    class FChatCommandHandler : public EventHandler
    {
    public:
        FChatCommandHandler();
        ~FChatCommandHandler();

        // LAB_0046a110 (thiscall, RET 4).
        bool OnEvent(unsigned MsgId) override;

    };

    FChatCommandHandler::FChatCommandHandler()
    {
        // FUN_0046a0c0.
        RegisterEvent(kChatOpenCommand);
    }

    FChatCommandHandler::~FChatCommandHandler()
    {
        // LAB_0046a140.
    }

    bool FChatCommandHandler::OnEvent(unsigned MsgId)
    {
        // LAB_0046a110.
        if (MsgId != kChatOpenCommand)
            return false;
        if (SinglePlayer())
            return false;
        if (!G_bChatMsgWinWanted)
            return false;
        ChatSetInputOpen(true);
        return true;
    }

    FChatCommandHandler G_ChatCommandHandler; // DAT_0066d3f8
}

namespace
{
    // The body FUN_0046b170 and FUN_0046b2b0 share.
    void ChatResetMultiplayerState()
    {
        if (SinglePlayer())
            return;

        NnGroupControl* const Group = G_ChatGroupControl;
        if (Group != nullptr)
        {
            Group->GroupMembers.clear();
            Group->Requests.clear();
            Group->RefreshRequestDisplay();
        }
        if (G_ChatChannelTable != nullptr)
            G_ChatChannelTable->RemovePlayerChannels();   // FUN_0045fb40

        G_bChatMsgWinWanted = false;
        if (G_ChatMsgWin != nullptr)
            G_ChatMsgWin->Flags = static_cast<unsigned char>(G_ChatMsgWin->Flags & ~(kWin3DActive | kWin3DVisible));

        ChatSetInputOpen(false);   // FUN_0046a3c0(0)

        if (G_ChatMessageStore != nullptr)
            G_ChatMessageStore->DiscardAll();

        G_ChatInputTarget = G_ChatIdChannel1;   // DAT_0066d3fc = DAT_0066da40
        Win3D* const Input = Win3DFindByName("CTID");   // FUN_0059da10
        if (Input != nullptr)
        {
            ChatTextInputDisplay* const Line = static_cast<ChatTextInputDisplay*>(Input);
            Line->SetTarget(G_ChatInputTarget);
            Line->RebuildDisplay();
        }
    }
}

void ChatResetForDisconnect()
{
    // FUN_0046b170
    ChatResetMultiplayerState();
}

void ChatResetForConnect()
{
    // FUN_0046b2b0
    ChatResetMultiplayerState();
}

void ChatShowMsgWin(bool bShow)
{
    // FUN_0046a4a0
    G_bChatMsgWinWanted = bShow;
    if (bShow)
    {
        if (G_ChatMsgWin != nullptr)
            G_ChatMsgWin->Flags = static_cast<unsigned char>(G_ChatMsgWin->Flags | kWin3DActive | kWin3DVisible);
    }
    else
    {
        if (G_ChatMsgWin != nullptr)
            G_ChatMsgWin->Flags = static_cast<unsigned char>(G_ChatMsgWin->Flags & ~(kWin3DActive | kWin3DVisible));
    }
}

void ChatDestroyMsgWin()
{
    // FUN_0046b650: FUN_0043d2a0 inlined on DAT_0066da80.
    if (G_ChatMsgWin == nullptr)
        return;
    Win3DDeleteTree(G_ChatMsgWin);
    G_ChatMsgWin = nullptr;
}

void ChatPlaceMsgWinForTrade(int Mode)
{
    // FUN_0046b450
    if (G_ChatMsgWin == nullptr)
        return;
    if (Mode == 0)
    {
        // The default place.
        Vector const Position = {-0.51f, 0.25f, -1.0f};
        G_ChatMsgWin->SetPosition(&Position);
        G_ChatMsgWin->SetNormSize(1.0f, 0.39f);
    }
    else if (Mode == 1)
    {
        // Beside the trade window.
        Vector const Position = {-0.275f, 0.045f, -1.0f};
        G_ChatMsgWin->SetPosition(&Position);
        G_ChatMsgWin->SetNormSize(0.56f, 0.39f);
    }
}

// ---------------------------------------------------------------------------------------------
// MsgWin

// DAT_0066d47c -- static initializer LAB_00464d60.
WINFACTORY<MsgWin> G_MsgWinFactory("MsgWin");

MsgWin::MsgWin()
    : Snapshot(nullptr)
    , SnapshotGeneration(0)
{
    // Inlined in FUN_0046c110.
}

MsgWin::~MsgWin()
{
    // FUN_0046c4c0.
    SnapshotGeneration = 0;
    Snapshot = nullptr;
}

bool MsgWin::HitTest(Vector const* /*Ray*/)
{
    // Slot 4 of vftable 0x005cffc4 is FUN_0053f330 (returns false), not BaseRichTextWin's
    // LAB_0057aa80: the chat message window is never picked, so the mouse goes through it to
    // space.
    return false;
}

void MsgWin::Update()
{
    // LAB_0046c1d0.
    std::map<unsigned int, unsigned int>* const Changed =
        Reputation::GetChangedAffiliationClientMap();
    if (Changed != nullptr)
    {
        for (std::map<unsigned int, unsigned int>::iterator It = Changed->begin();
             It != Changed->end(); ++It)
        {
            if (It->first != GetPlayerConnectionId())
                continue;
            // The map holds the affiliation the client had; Vibe::GetAffiliation the one it has.
            unsigned const Lost = It->second;
            unsigned Current = 0xffffffffu;
            int const PlayerVibe = GetPlayerRepVibeRetail();   // FUN_004c4670
            Reputation::Vibe::GetAffiliation(PlayerVibe, Current, false);
            if (Lost != 0xffffffffu)
            {
                wchar_t Name[0x80];
                if (GetIdsStringWWithRawFallback(Reputation::get_name(Lost), Name, 0x80) == 0)
                    Name[0] = 0;
                ChatPostLostAffiliation(Name);   // FUN_0046a4d0
            }
            if (Current == 0xffffffffu)
                continue;

            wchar_t Name[0x80];
            if (GetIdsStringWWithRawFallback(Reputation::get_name(Current), Name, 0x80) == 0)
                Name[0] = 0;
            ChatPostNewAffiliation(Name);    // FUN_0046a770
        }
        // Common never clears this map.
        Changed->clear();
    }

    if (Snapshot != nullptr)
    {
        FChatMessageStore* const Store = G_ChatMessageStore;
        if (Store != nullptr)
        {
            int const Ticks = static_cast<int>(clock());
            int const NowMs = (Ticks / 1000) * 1000;
            if (static_cast<unsigned>(NowMs - Store->LastClockMs) >
                static_cast<unsigned>(Store->MaxAgeMs))
                Store->RebuildRecent();
        }
        if (SnapshotGeneration != Snapshot->Generation)
            RefreshFromSnapshot();
    }
    RichTextWin::Update();   // FUN_0057b530
}

void MsgWin::RefreshFromSnapshot()
{
    // FUN_0046c410
    if (Snapshot == nullptr)
        return;
    ClearContent();
    RDLRefNode Ref(Snapshot->Display);
    AppendNode(Ref);
    SnapshotGeneration = Snapshot->Generation;
}

void ChatCreateMsgWin()
{
    // FUN_0046b580
    if (G_ChatMsgWin != nullptr || G_ChatRecentMessages == nullptr)
        return;
    G_ChatMsgWin = static_cast<MsgWin*>(WinFactoryCreate("MsgWin", "MsgWin", nullptr, nullptr));
    if (G_ChatMsgWin != nullptr)
    {
        // DAT_005ceec4 / DAT_005ceec8, then DAT_005ceebc / DAT_005ceec0.
        Vector const Position = {-0.51f, 0.25f, -1.0f};
        G_ChatMsgWin->SetPosition(&Position);
        G_ChatMsgWin->SetNormSize(1.0f, 0.39f);
    }
    // Retail does not test the window again here.
    G_ChatMsgWin->Snapshot = G_ChatRecentMessages;
    G_ChatMsgWin->SnapshotGeneration = 0;
    G_ChatMsgWin->Flags = static_cast<unsigned char>(G_ChatMsgWin->Flags | kWin3DActive | kWin3DVisible);
    G_ChatMsgWin->SetDrawFrame(false);
    G_ChatMsgWin->SetAlignMode(kRichAlignBottom);
    G_ChatMsgWin->SetAutoScrollbar(false);
}

void ChatGroupTradeRequestReceived(unsigned ShipId)
{
    // FUN_0046b500
    if (G_ChatGroupControl != nullptr)
        G_ChatGroupControl->AddTradeRequest(ShipId);
}

void ChatGroupTradeRequestRemoved(unsigned ShipId)
{
    // FUN_0046b520. Nothing found: no refresh.
    NnGroupControl* const Group = G_ChatGroupControl;
    if (Group == nullptr)
        return;
    for (std::list<FNnGroupRequest>::iterator It = Group->Requests.begin();
         It != Group->Requests.end(); ++It)
    {
        if (It->ShipId != 0 && It->ShipId == ShipId)
        {
            Group->Requests.erase(It);
            Group->RefreshRequestDisplay();
            return;
        }
    }
}

// ---------------------------------------------------------------------------------------------
// MOTD

void ChatSetServerMotd(const wchar_t* Text)
{
    // FUN_0046b0f0
    RdlClear(G_ChatServerMotd);
    AppendTextAsRdl(Text, static_cast<unsigned int>(wcslen(Text)), G_ChatServerMotd, false);
    ++G_ChatServerMotdVersion;
}

// ---------------------------------------------------------------------------------------------
// Name caches

RenderDisplayList* ChatGetSystemName(unsigned SystemId)
{
    // FUN_0046b6a0 -> FUN_00466770 on DAT_0066da70.
    RenderDisplayList* const Cached = CacheFindName(*G_ChatSystemNameCache, SystemId);
    if (Cached != nullptr)
        return Cached;
    RenderDisplayList* const Name = new RenderDisplayList();
    Universe::ISystem const* const System = Universe::get_system(SystemId);
    return CacheNewName(*G_ChatSystemNameCache, SystemId, Name, System != nullptr,
                        System != nullptr ? (unsigned)System->GetStridName() : 0);
}

RenderDisplayList* ChatGetShipArchName(unsigned ShipArchId)
{
    // FUN_0046b6c0 -> FUN_0046b6e0 on DAT_0066da74.
    RenderDisplayList* const Cached = CacheFindName(*G_ChatShipArchNameCache, ShipArchId);
    if (Cached != nullptr)
        return Cached;
    RenderDisplayList* const Name = new RenderDisplayList();
    Archetype::Ship* const Ship = Archetype::GetShip(ShipArchId);
    return CacheNewName(*G_ChatShipArchNameCache, ShipArchId, Name, Ship != nullptr,
                        Ship != nullptr ? static_cast<unsigned>(Ship->IdsName) : 0);
}

RenderDisplayList* ChatGetBaseName(unsigned BaseId)
{
    // FUN_0046b890 -> FUN_0046b8b0 on DAT_0066da78.
    RenderDisplayList* const Cached = CacheFindName(*G_ChatBaseNameCache, BaseId);
    if (Cached != nullptr)
        return Cached;
    RenderDisplayList* const Name = new RenderDisplayList();
    Universe::IBase* const Base = Universe::get_base(BaseId);
    return CacheNewName(*G_ChatBaseNameCache, BaseId, Name, Base != nullptr,
                        Base != nullptr ? static_cast<unsigned>(Base->StridName) : 0);
}

RenderDisplayList* ChatGetServerMotdRdl()
{
    return &G_ChatServerMotd;
}

int ChatGetServerMotdVersion()
{
    return G_ChatServerMotdVersion;
}
