// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "NeuroNet/Chat/chat_text_input.h"

#include "NeuroNet/Chat/chat_init.h"             // FChatChannelTable, G_ChatChannelTable, chat ids, ChatSetInputOpen
#include "Space/Effects/effect_vertex_batch.h"   // FEffectVertexBatch (FUN_0046ebc0 / FUN_0046eab0 / FUN_0046e9c0)
#include "Space/HUD/hud_command_dispatch.h"  // FHudCmdRecord, HudPostCommand (FUN_005763b0)
#include "Common/Fonts/ids_rdl.h"               // LoadIdsTextIntoRdl, RdlAppendNode, RdlClear
#include "Network/login_mgr.h"             // G_ChatServer (DAT_0067ecd4)
#include "Interface/menu_ui_widgets.h"
#include "Interface/win3d.h"                 // Win3DFindByName (FUN_0059da10)
#include "Common/misc_util.h"             // MiscUtilLoadTextureLibrary / MiscUtilReleaseTextureLibrary
#include "NeuroNet/nn_group_control.h"      // NnGroupControl::GroupMembers
#include "Space/player_spawn.h"          // GetPlayerConnectionId (DAT_00673344)
#include "Interface/win3d_renderables.h"     // Win3DFlushBeforeOverlayDraw (FUN_0045a9f0)
#include "Interface/win_factory.h"

#include "DALib.h"                 // DALib::TextureLib
#include "common_server.h"         // IChatServer
#include "dacom.h"                 // FDUMP, DACOM_CRC::GetCRC32
#include "render/shading.h"        // ITextureLibrary2

#include <string.h>

// Retail DAT_0066d920 -- WINFACTORY<ChatTextInputDisplay> static (LAB_0045e270; Create
// FUN_0046be00).
WINFACTORY<ChatTextInputDisplay> G_ChatTextInputDisplayFactory("ChatTextInputDisplay");

namespace
{
    // The target frame colours (RGBA bytes in memory order, read as one dword by FUN_00579870).
    unsigned char const kColorUniverse[4] = {0xff, 0xff, 0xff, 0xff};   // DAT_00611560
    unsigned char const kColorSystem[4] = {0x87, 0xc3, 0xe0, 0xff};     // DAT_00611564
    unsigned char const kColorLocal[4] = {0x3b, 0xbf, 0x1d, 0xff};      // DAT_00611568
    unsigned char const kColorPlayer[4] = {0x3b, 0xbf, 0x1d, 0xff};     // DAT_0061156c
    unsigned char const kColorGroup[4] = {0xff, 0x7e, 0xfd, 0xff};      // DAT_00611570
    unsigned char const kColorConsole[4] = {0x3b, 0xbf, 0x1d, 0xff};    // DAT_00611574

    wchar_t const kTargetSpace[] = L" ";         // DAT_005cef90
    wchar_t const kTargetUnknown[] = L" ????";   // u" ????" 0x005cef84
    wchar_t const kTargetArrow[] = L"->";        // DAT_005cef58 via PTR_DAT_0061155c

    // DAT_0066d4e0 -- the 0x400-byte send buffer BinaryRDLWriter fills before SubmitChat.
    char G_ChatSendBuffer[0x400];

    // DAT_0066da84 +0x350: the group set's element count.
    bool ChatGroupIsEmpty()
    {
        NnGroupControl* const Group = G_ChatGroupControl;
        return Group == nullptr || Group->GroupMembers.empty();
    }
}

// ---------------------------------------------------------------------------------------------
// FChatBackground

FChatBackground::FChatBackground()
    // Inlined in FUN_0046be40.
    : bLoaded(0)
    , LibraryHandle(-1)
    , TextureId(nullptr)
    , TileWidth(0.0f)
    , TileHeight(0.0f)
{
}

FChatBackground::~FChatBackground()
{
    // FUN_0045e3e0
    if (bLoaded != 0)
    {
        DALib::TextureLib->ReleaseTextureRecord(TextureId);        // slot 14 (+0x38)
        MiscUtilReleaseTextureLibrary(LibraryHandle);    // FUN_0041c830
        bLoaded = 0;
    }
}

namespace
{
    // FUN_0045e470's texture-stage tables, {type, value} pairs up to a zero type: stage 0
    // DAT_005cee48 (COLOROP MODULATE, COLORARG1 TEXTURE, COLORARG2 DIFFUSE, ALPHAOP MODULATE,
    // ALPHAARG1 TEXTURE, ALPHAARG2 DIFFUSE), stage 1 DAT_005cee80 (both ops DISABLE, args
    // TEXTURE / CURRENT).
    struct FChatBgStageState
    {
        int Type;
        int Value;
    };
    FChatBgStageState const kChatBgStage0[] = {{D3DTSS_COLOROP, D3DTOP_MODULATE}, {D3DTSS_COLORARG1, D3DTA_TEXTURE},
                                                 {D3DTSS_COLORARG2, D3DTA_DIFFUSE}, {D3DTSS_ALPHAOP, D3DTOP_MODULATE},
                                                 {D3DTSS_ALPHAARG1, D3DTA_TEXTURE}, {D3DTSS_ALPHAARG2, D3DTA_DIFFUSE},
                                                 {0, 0}};
    FChatBgStageState const kChatBgStage1[] = {{D3DTSS_COLOROP, D3DTOP_DISABLE}, {D3DTSS_COLORARG1, D3DTA_TEXTURE},
                                                 {D3DTSS_COLORARG2, D3DTA_CURRENT}, {D3DTSS_ALPHAOP, D3DTOP_DISABLE},
                                                 {D3DTSS_ALPHAARG1, D3DTA_TEXTURE}, {D3DTSS_ALPHAARG2, D3DTA_CURRENT},
                                                 {0, 0}};

    // DAT_00611578 -- the quad's vertex colour, copied byte by byte into the batch.
    unsigned char const kChatBgColor[4] = {0x00, 0x00, 0x80, 0xb0};

    // The vertex batch FUN_0045e470 builds on its stack; its destructor is FUN_0046e9c0.
    struct FChatBgBatch
    {
        FChatBgBatch()
        {
            Batch.Type = 1;
            Batch.Vertices = nullptr;
            Batch.VertCount = 0;
            Batch.Fvf = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;
            Batch.Indices = nullptr;
            Batch.IndexCount = 0;
            Batch.Pipeline = DALib::Pipeline;
            if (Batch.Pipeline != nullptr)
                Batch.Pipeline->AddRef();
            Batch.Growth = 0x80;
            Batch.VertCapacity = 0;
            Batch.IndexCapacity = 0;
            Batch.bCloseLoop = 0;
            Batch.bStripMode = 0;
            Batch.Pad2A = 0;
            Batch.Pad2B = 0;
            Batch.StripCounter = 0;
            Batch.V = 0.0f;
            Batch.U = 0.0f;
            Batch.ColorR = kChatBgColor[0];
            Batch.ColorG = kChatBgColor[1];
            Batch.ColorB = kChatBgColor[2];
            Batch.ColorA = kChatBgColor[3];
        }
        ~FChatBgBatch() { Batch.Destroy(); } // FUN_0046e9c0

        FEffectVertexBatch Batch;

    private:
        FChatBgBatch(FChatBgBatch const&) = delete;
        FChatBgBatch& operator=(FChatBgBatch const&) = delete;
    };
}

bool FChatBackground::Draw(float const* Orientation, float Width, float Height)
{
    // FUN_0045e470. x87: only an equal compare with 0.0f (DAT_005c7474) leaves; unordered goes
    // on.
    if (TileWidth == 0.0f)
        return false;
    if (TileHeight == 0.0f)
        return false;
    if (bLoaded == 0)
        return false;

    // Retail re-reads DALib::Pipeline for every call.
    DALib::Pipeline->SetRenderState(D3DRS_ZENABLE, 0);
    DALib::Pipeline->SetRenderState(D3DRS_ZWRITEENABLE, 0);
    DALib::Pipeline->SetRenderState(D3DRS_CULLMODE, 1);
    DALib::Pipeline->SetRenderState(D3DRS_LIGHTING, 0);
    DALib::Pipeline->SetRenderState(D3DRS_SRCBLEND, 5);
    DALib::Pipeline->SetRenderState(D3DRS_DESTBLEND, 6);
    DALib::Pipeline->SetRenderState(D3DRS_ALPHABLENDENABLE, 1);

    // The two stage tables; the first type is the constant 1, each next type from the table
    // until a zero one.
    {
        IRenderPipeline* const Pipe = DALib::Pipeline;
        FChatBgStageState const* Entry = kChatBgStage0;
        int Type = D3DTSS_COLOROP;
        for (;;)
        {
            Pipe->SetTextureStageState(0, Type, Entry->Value);
            Type = Entry[1].Type;
            ++Entry;
            if (Type == 0)
                break;
        }
    }
    {
        IRenderPipeline* const Pipe = DALib::Pipeline;
        FChatBgStageState const* Entry = kChatBgStage1;
        int Type = D3DTSS_COLOROP;
        for (;;)
        {
            Pipe->SetTextureStageState(1, Type, Entry->Value);
            Type = Entry[1].Type;
            ++Entry;
            if (Type == 0)
                break;
        }
    }
    // Stage 0 ADDRESSU / ADDRESSV = WRAP.
    DALib::Pipeline->SetTextureStageState(0, 0xd, 1);
    DALib::Pipeline->SetTextureStageState(0, 0xe, 1);

    unsigned long Frame[5];
    if (DALib::TextureLib->GetTextureFrame(TextureId, 0, Frame) == 0)
        DALib::Pipeline->SetTextureStageTexture(0, reinterpret_cast<void*>(Frame[0]));

    DALib::Pipeline->SetModelview((const Transform*)Orientation);

    FChatBgBatch Local;
    FEffectVertexBatch* const Batch = &Local.Batch;
    Batch->Reset();                                  // FUN_0046ebc0

    // A triangle list (type 4) in quad mode -- FUN_0046eab0 turns the fourth vertex into two
    // triangles. UV repeats the tile over the box from (0, 0) to (Width, -Height).
    float const V1 = Height / TileHeight;
    Batch->Type = D3DPT_TRIANGLELIST;
    Batch->bCloseLoop = 0;
    Batch->bStripMode = 1;
    Batch->U = 0.0f;
    Batch->V = V1;
    Batch->Append(0.0f, 0.0f, 0.0f);

    Batch->U = Width / TileWidth;
    Batch->V = V1;
    Batch->Append(Width, 0.0f, 0.0f);

    Batch->V = 0.0f;
    Batch->U = Width / TileWidth;
    float const Bottom = -Height;
    Batch->Append(Width, Bottom, 0.0f);

    Batch->U = 0.0f;
    Batch->V = 0.0f;
    Batch->Append(0.0f, Bottom, 0.0f);

    // A closed batch (+0x28) repeats its first vertex at the end.
    int Count = Batch->VertCount;
    if (Count != 0)
    {
        if (Batch->bCloseLoop != 0)
        {
            Batch->Vertices[Count] = Batch->Vertices[0];
            ++Count;
            Batch->VertCount = Count;
        }
        if (Batch->Pipeline != nullptr)
        {
            Batch->Pipeline->DrawPrimitive(Batch->Type, Batch->Fvf, Batch->Vertices,
                                           static_cast<unsigned>(Count), nullptr);
        }
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// ChatTextInputDisplay

ChatTextInputDisplay::ChatTextInputDisplay()
    // FUN_0046be40. Retail does NOT initialise +0x530..+0x539 (Create sets +0x538 / +0x539 and,
    // with a table, +0x530); the port zeroes them (see Update).
    : Table(nullptr)
    , TableGeneration(0)
    , bDrawBackground(0)
    , bCloseAfterSend(0)
    , Target(0u)
{
}

ChatTextInputDisplay::~ChatTextInputDisplay()
{
    // FUN_0046c640.
}

bool ChatTextInputDisplay::Create(Win3D* NewParent, void* /*Data*/)
{
    // FUN_0045eee0.
    bool const bResult = RichTextWin::Create(NewParent, nullptr);   // FUN_0057b5a0

    bDrawFrame = 1;                              // +0x402
    bDrawBackground = 1;                         // +0x538
    bCloseAfterSend = 0;                         // +0x539
    Background.TileWidth = 0.08f;                // +0x55c = 0x3da3d70a
    Background.TileHeight = 0.0533f;             // +0x560 = 0x3d5a740d
    Background.Name.assign("ChatBackground");    // +0x540 (s_ChatBackground_005cf030)

    NormWidth = 0.46f;                           // DAT_005ceedc
    NormHeight = 0.065f;                         // DAT_005ceee0

    // The five target prefixes -- the IDS text with its last paragraph stripped, or
    // JustifyNode(1) + the English debug text.
    LoadIdsTextIntoRdl(0x6003b, PrefixSystem, L"DEBUG - system");       // +0x56c
    LoadIdsTextIntoRdl(0x6003a, PrefixLocal, L"DEBUG - local");         // +0x578
    LoadIdsTextIntoRdl(0x60039, PrefixGroup, L"DEBUG - group");         // +0x584
    LoadIdsTextIntoRdl(0x60038, PrefixConsole, L"DEBUG - console");     // +0x590
    LoadIdsTextIntoRdl(0x6003c, PrefixUniverse, L"DEBUG - universe");   // +0x59c

    if (G_ChatChannelTable != nullptr)
        Table = G_ChatChannelTable;
    SetFixedStyle(true);                 // FUN_0057bdd0(1)
    SetTarget(G_ChatIdChannel1);         // FUN_0045eaf0(&DAT_0066da40)
    RebuildDisplay();                    // FUN_0057d330

    // DAT_005c75e0 = 0.5f.
    float const HalfW = NormWidth * 0.5f;
    float const HalfH = NormHeight * 0.5f;
    HalfExtents.x = HalfW;
    HalfExtents.y = HalfH;
    HalfExtents.z = 0.0f;
    HitOffset.x = HalfW;
    HitOffset.y = HalfH;
    HitOffset.z = 0.0f;
    DrawLayer = 7;
    DrawSubLayer = 7;
    return bResult;
}

void ChatTextInputDisplay::SetTarget(CHAT_ID const& Id)
{
    // FUN_0045eaf0. A player target uses its name from the channel table ("????" text when
    // there is no table).
    wchar_t const* Text = kTargetSpace;
    unsigned short Style = 0;
    RenderDisplayList const* Name = nullptr;
    unsigned char const* Color = nullptr;
    if (Id.Id == G_ChatIdChannel1.Id)
    {
        Style = kTextStyleChatSystem;
        Name = &PrefixSystem;
        Color = kColorSystem;
    }
    else if (Id.Id == G_ChatIdChannel2.Id)
    {
        Style = kTextStyleChatLocal;
        Name = &PrefixLocal;
        Color = kColorLocal;
    }
    else if (Id.Id == G_ChatIdChannel3.Id)
    {
        Style = kTextStyleChatGroup;
        Name = &PrefixGroup;
        Color = kColorGroup;
    }
    else if (Id.Id == G_ChatIdChannel0.Id)
    {
        Style = kTextStyleChatUniverse;
        Name = &PrefixUniverse;
        Color = kColorUniverse;
    }
    else if (Id.Id == G_ChatIdNone.Id)
    {
        Style = kTextStyleChatConsole;
        Name = &PrefixConsole;
        Color = kColorConsole;
    }
    else
    {
        Style = kTextStyleChatPlayer;
        if (Table != nullptr)
            Name = Table->FindName(Id);
        else
            Text = kTargetUnknown;
        Color = kColorPlayer;
    }

    unsigned long ColorWord = 0;
    memcpy(&ColorWord, Color, 4);
    SetFrameColor(&ColorWord);
    Target = Id;

    RdlClear(Prefix);
    {
        StyleNode const Node(Style);
        ChatAppendNodeClone(Prefix, Node);
    }
    {
        TextNode const Node(Text, -1);
        ChatAppendNodeClone(Prefix, Node);
    }

    if (Name != nullptr)
        ChatAppendRdlCopy(Prefix, *Name);

    {
        TextNode const Node(kTargetArrow, -1);
        ChatAppendNodeClone(Prefix, Node);
    }

    RdlClear(FixedStyle);
    {
        StyleNode const Node(Style);
        ChatAppendNodeClone(FixedStyle, Node);
    }
}

void ChatTextInputDisplay::NextTarget()
{
    // FUN_0045e800. Retail does not test Table for null.
    CHAT_ID const Own(GetPlayerConnectionId() & 0xffffu);
    CHAT_ID Result(0u);

    if (Target.Id == G_ChatIdChannel1.Id)
    {
        // From the first entry, the first player that is not us; the system channel when there
        // is none.
        Result = G_ChatIdChannel1;
        for (FChatChannelTable::FNameMap::iterator It = Table->Names.begin();
             It != Table->Names.end(); ++It)
        {
            unsigned const Key = It->first;
            if (((Key >> 16) & 0xffu) != 0)
                continue;
            if (Key == Own.Id)
                continue;
            Result = CHAT_ID(Key);
            break;
        }
    }
    else if (Target.Id == G_ChatIdChannel3.Id)
    {
        Result = G_ChatIdChannel1;
    }
    else
    {
        // From the current target onwards, the next entry that is not us (a channel counts);
        // past the last one, the group channel. An unknown target leaves {0, 0, 0}.
        FChatChannelTable::FNameMap::iterator It = Table->Names.find(Target.Id);
        if (It != Table->Names.end())
        {
            for (;;)
            {
                ++It;
                if (It == Table->Names.end())
                {
                    Result = G_ChatIdChannel3;
                    break;
                }
                if (It->first == Own.Id)
                    continue;
                Result = CHAT_ID(It->first);
                break;
            }
        }
    }

    // The group channel only with a non-empty group.
    if (Result.Id == G_ChatIdChannel3.Id && ChatGroupIsEmpty())
        Result = G_ChatIdChannel1;
    SetTarget(Result);
}

void ChatTextInputDisplay::PrevTarget()
{
    // FUN_0045e940. Retail does not test Table for null.
    CHAT_ID const Own(GetPlayerConnectionId() & 0xffffu);
    CHAT_ID Result(0u);

    if (Target.Id == G_ChatIdChannel1.Id && !ChatGroupIsEmpty())
    {
        Result = G_ChatIdChannel3;
    }
    else if (Target.Id == G_ChatIdChannel3.Id || Target.Id == G_ChatIdChannel1.Id)
    {
        // From the last entry backwards; a channel stops the walk, the first player that is
        // not us wins; the system channel otherwise.
        Result = G_ChatIdChannel1;
        FChatChannelTable::FNameMap::iterator It = Table->Names.end();
        while (It != Table->Names.begin())
        {
            --It;
            unsigned const Key = It->first;
            if (((Key >> 16) & 0xffu) != 0)
                break;
            if (Key != Own.Id)
            {
                Result = CHAT_ID(Key);
                break;
            }
        }
    }
    else
    {
        // From the current target backwards, the same rules. Stepping back from the first
        // entry lands on the head (end) and stops.
        Result = G_ChatIdChannel1;
        FChatChannelTable::FNameMap::iterator It = Table->Names.find(Target.Id);
        if (It != Table->Names.end())
        {
            while (It != Table->Names.begin())
            {
                --It;
                unsigned const Key = It->first;
                if (((Key >> 16) & 0xffu) != 0)
                    break;
                if (Key != Own.Id)
                {
                    Result = CHAT_ID(Key);
                    break;
                }
            }
        }
    }

    if (Result.Id == G_ChatIdChannel3.Id && ChatGroupIsEmpty())
        Result = G_ChatIdChannel1;
    SetTarget(Result);
}

void ChatTextInputDisplay::Update()
{
    // FUN_0045f5e0: when the channel table changed since the last look and the current target
    // is no longer in it, fall back to the system channel -- on the "CTID" line as well as on
    // this one (they are the same object in practice) -- and rebuild.
    FChatChannelTable* const Current = Table;
    if (Current != nullptr && TableGeneration != Current->Generation)
    {
        if (Current->Names.find(Target.Id) == Current->Names.end())
        {
            CHAT_ID const Fallback = G_ChatIdChannel1;
            G_ChatInputTarget = Fallback;    // DAT_0066d3fc
            Win3D* const Found = Win3DFindByName("CTID");   // FUN_0059da10, name 0x005cf04c
            if (Found != nullptr)
            {
                ChatTextInputDisplay* const Input = static_cast<ChatTextInputDisplay*>(Found);
                Input->SetTarget(Fallback);
                Input->RebuildDisplay();
            }
            RebuildDisplay();
        }
        TableGeneration = Table->Generation;
    }
    RichTextEditWin::Update();   // FUN_0057c360
}

void ChatTextInputDisplay::Draw()
{
    // FUN_0045f4f0
    Win3DFlushBeforeOverlayDraw();   // FUN_0045a9f0
    if (bDrawBackground != 0)
    {
        if (Background.bLoaded == 0)
        {
            // FUN_0041c7e0 loads the library; the texture is looked up by the CRC of the
            // background's name.
            std::string Path = G_ChatImageRoot;
            Path.append("chat.txm");
            Background.LibraryHandle = MiscUtilLoadTextureLibrary(Path.c_str());
            struct FTextureQuery
            {
                char const* Name;
                unsigned long Crc;
            } Query;
            Query.Name = Background.Name.c_str();
            Query.Crc = DACOM_CRC::GetCRC32(Query.Name);
            if (DALib::TextureLib->FindTextureByCrc(&Query, &Background.TextureId) == 0)
                Background.bLoaded = 1;
        }
        // Drawn whether or not the load succeeded.
        Background.Draw(&Orientation.E[0][0], NormWidth, NormHeight);   // FUN_0045e470
    }
    RichTextEditWin::Draw();   // FUN_0057c440
}

bool ChatTextInputDisplay::OnKeyDown(FHudCmdRecord const* Record)
{
    // FUN_0045f700: key-downs of an active line only.
    if (Record->bKeyDown != 0 && (Flags & kWin3DActive) != 0)
    {
        switch (Record->Key)
        {
        case VK_RETURN:
        {
            // A non-empty line is serialised and submitted; the line is cleared either way, and
            // a line opened for one message closes.
            if (GetText(nullptr, 0) > 0)
            {
                unsigned OutSize = 0;
                if (Writer.write_buffer(TextDisplay, G_ChatSendBuffer, 0x400, OutSize))
                {
                    IChatServer* const ChatServer = static_cast<IChatServer*>(G_ChatServer);
                    if (ChatServer != nullptr)
                    {
                        ChatServer->SubmitChat(CHAT_ID(GetPlayerConnectionId() & 0xffffu), OutSize,
                                               G_ChatSendBuffer, Target, -1);
                    }
                }
            }
            ClearText();
            if (bCloseAfterSend != 0)
            {
                ChatSetInputOpen(false);
                bCloseAfterSend = 0;
            }
            return true;
        }
        case VK_ESCAPE:
            ClearText();
            ChatSetInputOpen(false);
            return true;
        case VK_UP:
            PrevTarget();
            RebuildDisplay();
            return true;
        case VK_DOWN:
            NextTarget();
            RebuildDisplay();
            return true;
        default:
            break;
        }
    }
    return RichTextEditWin::OnKeyDown(Record);
}

void ChatTextInputDisplay::OnActivated()
{
    // LAB_0045f6a0 (on the +0x498 face).
    RegisterEvent(USER_PAUSE);
}

void ChatTextInputDisplay::OnDeactivating()
{
    // LAB_0045f6b0.
    UnregisterEvent(USER_PAUSE);
}

bool ChatTextInputDisplay::OnEvent(unsigned MsgId)
{
    // FUN_0045f6c0 (this = the face): 0x14 clears and closes the line and posts 0x14 on.
    if (MsgId != USER_PAUSE)
        return RichTextEditWin::OnEvent(MsgId);
    ClearText();
    ChatSetInputOpen(false);
    HudPostCommand(USER_PAUSE, 0);
    return true;
}
