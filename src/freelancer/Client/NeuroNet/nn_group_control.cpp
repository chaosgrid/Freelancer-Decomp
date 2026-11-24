// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// NN_GroupControl -- the group / trade request window (see nn_group_control.h for the object).
//
//   FUN_0045fd70 ctor            FUN_00460170 dtor (FUN_00460090 scalar deleting)
//   FUN_00460520 SetChannelTable FUN_00460550 Create
//   FUN_00462a30 ShowControl     FUN_00462a40 ProcessMessage
//   LAB_00462ab0 UpdateInactive  FUN_00462ba0 Update
//   LAB_00462bd0 SetProperty     FUN_00462e30 RefreshRequestDisplay
//   FUN_004632f0 AddTradeRequest FUN_00463630 HandleInviteMessage
//   DAT_0066d8f4 WINFACTORY<NN_GroupControl> (registrar FUN_0046bf90, create FUN_0046c000)

#include "NeuroNet/nn_group_control.h"

#include "Space/client_gameobj.h"
#include "Space/client_objdb.h"      // ClientObjDb_FindObject (FUN_005416c0)
#include "Space/HUD/hud_maneuvers.h"     // GetHudManeuvers (DAT_00674a58), RequestManeuver (FUN_004d8e20)
#include "Common/Fonts/ids_rdl.h"
#include "Network/login_mgr.h"         // G_Server (DAT_0067ecd0), G_ChatServer (DAT_0067ecd4)
#include "NeuroNet/nn_main_button.h"    // NN_MainButton::SetGroupButtonAlert / SetGroupButtonMesh
#include "NeuroNet/nn_modal.h"          // G_NnMainButton (DAT_0066d3f0), NnMainButtonOnModalOpen
#include "NeuroNet/nn_ui_pulse.h"       // G_HudChromeColorRgba (DAT_00679bb4)
#include "Space/player_behavior.h"   // GetPlayerObjRW (FUN_0054baf0)
#include "Space/player_spawn.h"      // GetPlayerConnectionId / GetPlayerCharacterId
#include "Interface/rich_text_win.h"
#include "Interface/win_factory.h"
#include "Interface/wire_controls.h"     // RichTextWireButtonControl

#include "common_cobject.h"
#include "common_iface.h"
#include "common_server.h"
#include "dacom.h"

#include <ctime>

// DAT_0066d8f4 -- the static initializer runs FUN_0046bf90(&DAT_0066d8f4, "NN_GroupControl").
WINFACTORY<NnGroupControl> G_NnGroupControlFactory("NN_GroupControl");

namespace
{
    const char kNnChatCpp[] = "E:\\FL\\Scratch\\Source\\Client\\nn\\NN_Chat.cpp";

    // The group channel's 8-byte record (FUN_00463630 reads [+0] and [+4]; LAB_00462bd0 builds
    // one on the stack for SubmitChat).
    struct FNnGroupWireMessage
    {
        unsigned Opcode;    // +0x0
        unsigned Payload;   // +0x4 a player id (low word) or a connection id
    };

    // DAT_0066da5c -- BtnLeave's X. A .bss float nothing in the listing writes, so it is 0.0f;
    // retail reads it from memory where the other button coordinates are .data constants.
    float G_GroupBtnLeaveX = 0.0f;

    // The clock()/1000*1000 stamp (0x10624dd3 multiply, SAR 6, sign fix, IMUL 0x3e8).
    int GroupNowMsRounded()
    {
        int const Ticks = static_cast<int>(clock());
        return (Ticks / 1000) * 1000;
    }

    void GroupNotice(int Line, char const* Message)
    {
        FDUMP(0x100003, "%s(%d) : NOTICE:General:%s", kNnChatCpp, Line, Message);
    }

    // One text of FUN_00460550's table: an optional StyleNode clone pushed first, then the
    // FUN_0057da40 / FUN_0057eb40 / JustifyNode(1) + TextNode(debug) idiom.
    void LoadGroupText(RenderDisplayList& Text, unsigned short Style, unsigned Ids,
                       wchar_t const* DebugText)
    {
        if (Style != 0)
        {
            StyleNode StyleCell(Style);
            RdlAppendNode(Text, StyleCell.Clone());
        }
        LoadIdsTextIntoRdl(Ids, Text, DebugText);
    }

    // The discard every FUN_00463630 exit that built nothing takes, and FUN_004632f0's no-name
    // exit.
    void DiscardEntry(FChatMessageEntry* Entry)
    {
        if (G_ChatMessageStore != nullptr)
            G_ChatMessageStore->Discard(Entry);
    }

    // RDLRef(Prefix) clone, then a clone of every node of Name (the FUN_0046c800 loop).
    void AppendPrefixAndName(FChatMessageEntry* Entry, RenderDisplayList& Prefix,
                             RenderDisplayList const* Name)
    {
        RDLRefNode Ref(Prefix);
        RdlAppendNode(Entry->Body, Ref.Clone());
        if (Name != nullptr)
            ChatAppendRdlCopy(Entry->Body, *Name);
    }

    // The FUN_0046cd50 lookup on DAT_0066da7c used by every "name" case: key {Id & 0xffff, 0, 0},
    // hit only when the node exists and its RDL pointer is non-null. Retail reads DAT_0066da7c
    // without a null test at each of these sites.
    RenderDisplayList* FindChatNameForWord(unsigned Word)
    {
        return G_ChatChannelTable->FindName(ChatIdForPlayer(Word & 0xffffu));
    }
}

void NnGroupSendWireMessage(unsigned Opcode, CHAT_ID Player)
{
    // FUN_0045e360 (cdecl).
    IChatServer* const ChatServer = static_cast<IChatServer*>(G_ChatServer);
    if (ChatServer == nullptr)
        return;
    FNnGroupWireMessage Message;
    Message.Opcode = Opcode;
    Message.Payload = Player.Id & 0xffffu;
    ChatServer->SubmitChat(CHAT_ID(GetPlayerConnectionId() & 0xffffu), sizeof(Message), &Message,
                           G_ChatIdGroupInvite, -1);
}

// ---------------------------------------------------------------------------------------------

NnGroupControl::NnGroupControl()
    : ChannelTable(nullptr)
    , ChannelTableGeneration(0)
    , ShownFrom(G_ChatIdNone)
    , ShownShipId(0)
    , Field_0x33c(0)
    , BtnClose(nullptr)
    , BtnLeave(nullptr)
    , BtnAccept(nullptr)
    , BtnDecline(nullptr)
    , RequestDisplay(nullptr)
    , AttentionTitle(nullptr)
{
    // FUN_0045fd70: WinControl, +0x334 = {0}, the set (FUN_0046d100), the list (FUN_00489f40),
    // 25 empty RDLs, the six control pointers 0, then +0x338/+0x33c/+0x330/+0x32c = 0 and
    // +0x334 = DAT_0066da48.
}

NnGroupControl::~NnGroupControl()
{
    // FUN_00460170: DAT_0066da84 cleared when it is this; then the RDLs 0x480 .. 0x360, the list
    // and the set go (member destruction order), then WinControl's dtor.
    if (G_ChatGroupControl == this)
        G_ChatGroupControl = nullptr;
}

void NnGroupControl::SetChannelTable(FChatChannelTable* Table)
{
    // FUN_00460520
    ChannelTable = Table;
    RefreshRequestDisplay();
}

void NnGroupControl::ShowControl(Win3D* Control)
{
    // FUN_00462a30: OR byte [ECX+0x6c],3.
    Control->Flags = static_cast<unsigned char>(Control->Flags | 0x03);
}

bool NnGroupControl::Create(Win3D* NewParent, void* Data)
{
    // FUN_00460550 -- Win3D::Create FUN_004b5060(NewParent, 0); Data is not passed on.
    (void)Data;
    if (!Win3D::Create(NewParent, nullptr))
        return false;

    Offset[0] = 0.0f;
    Offset[1] = 0.052f;     // 0x3d54fdf4
    Offset[2] = 0.0f;
    SetMesh("Interface\\intro\\objects\\MULTI_attentionreq'd.cmp", 0);
    Field2d2 = 1;

    // The 25 texts, in retail's order.
    LoadGroupText(Texts[kNnGroupTextInviteFrom], 0, 0x60064u,
                  L"DEBUG - You have received a group invitation from:");
    LoadGroupText(Texts[kNnGroupTextTradeFrom], 0, 0x6006fu,
                  L"DEBUG - You have received a trade request from:");
    LoadGroupText(Texts[kNnGroupTextLeaveGroup], kTextStyleButton, 0x60020u, L"DEBUG - Leave Group");
    LoadGroupText(Texts[kNnGroupTextAccept], kTextStyleButton, 0x60013u, L"DEBUG - Accept");
    LoadGroupText(Texts[kNnGroupTextDecline], kTextStyleButton, 0x6001au, L"DEBUG - Decline");
    LoadGroupText(Texts[kNnGroupTextAcceptTrade], kTextStyleButton, 0x60014u, L"DEBUG - Accept Trade");
    LoadGroupText(Texts[kNnGroupTextDeclineTrade], kTextStyleButton, 0x6001bu, L"DEBUG - Decline Trade");
    LoadGroupText(Texts[kNnGroupTextNoInvitations], kTextStyleBody, 0x6006du,
                  L"DEBUG - There are no outstanding group invitations.");
    LoadGroupText(Texts[kNnGroupTextInviteSent], kTextStyleNotice, 0x60065u,
                  L"DEBUG - Group invitation sent to:");
    LoadGroupText(Texts[kNnGroupTextJoinedGroup], kTextStyleNotice, 0x60067u,
                  L"DEBUG - You have joined into a group with:");
    LoadGroupText(Texts[kNnGroupTextNewMember], kTextStyleNotice, 0x60066u, L"DEBUG - New group member:");
    LoadGroupText(Texts[kNnGroupTextLeftGroup], kTextStyleNotice, 0x60069u,
                  L"DEBUG - You have left your group.");
    LoadGroupText(Texts[kNnGroupTextMemberLeft], kTextStyleNotice, 0x60068u,
                  L"DEBUG - A group member has left:");
    LoadGroupText(Texts[kNnGroupTextAbortMission], kTextStyleNotice, 0x60060u,
                  L"DEBUG - You must abort or complete your mission.");
    LoadGroupText(Texts[kNnGroupTextJoinFailedMission], kTextStyleNotice, 0x60061u,
                  L"DEBUG - A player failed to join your group due to already being involved "
                  L"in a mission: ");
    LoadGroupText(Texts[kNnGroupTextCannotInviteSelf], kTextStyleNotice, 0x6005bu,
                  L"DEBUG - You cannot invite yourself to join a group.");
    LoadGroupText(Texts[kNnGroupTextGroupFull], kTextStyleNotice, 0x60059u, L"DEBUG - Your group is full.");
    LoadGroupText(Texts[kNnGroupTextAlreadyInYourGroup], kTextStyleNotice, 0x6005au,
                  L"DEBUG - You cannot invite a player already in your group.");
    LoadGroupText(Texts[kNnGroupTextCannotJoinOwnGroup], kTextStyleNotice, 0x6005eu,
                  L"DEBUG - You cannot join your own group.");
    LoadGroupText(Texts[kNnGroupTextNotInvited], kTextStyleNotice, 0x60062u,
                  L"DEBUG - You have not received an invitation to this group:");
    LoadGroupText(Texts[kNnGroupTextAlreadyMember], kTextStyleNotice, 0x6005du,
                  L"DEBUG - You are already a member of this group:");
    LoadGroupText(Texts[kNnGroupTextGroupNowFull], kTextStyleNotice, 0x6005cu,
                  L"DEBUG - Sorry, but this group is now full:");
    LoadGroupText(Texts[kNnGroupTextNotInGroup], kTextStyleNotice, 0x6005fu,
                  L"DEBUG - You cannot leave your group because you are not in a group.");
    LoadGroupText(Texts[kNnGroupTextPlayerDied], kTextStyleNotice, 0x60051u, L"DEBUG - A player has died:");
    LoadGroupText(Texts[kNnGroupTextAttentionRequired], 0, 0x60012u,
                  L"DEBUG - ATTENTION REQUIRED");

    RequestDisplay = static_cast<RichTextWin*>(
        WinFactoryCreate("RequestDisplay", "RichTextWin", this, nullptr));
    if (RequestDisplay != nullptr)
    {
        float const AttachOffset[3] = {-0.23f, 0.2f, 0.0f};    // DAT_005cef30 / DAT_005cef34
        RequestDisplay->SetAttachOffset(AttachOffset);
        RequestDisplay->SetNormSize(0.46f, 0.15f);             // DAT_005cef28 / DAT_005cef2c
        RequestDisplay->SetAutoScrollbar(true);
        RequestDisplay->SetFrameColor(
            reinterpret_cast<unsigned long const*>(G_HudChromeColorRgba));
        RequestDisplay->SetAlignMode(2);
        RequestDisplay->GetToolTipHoverInfo()->IdsAlt = 0x1b83;
        RequestDisplay->SetToolTipEnabled(true);
    }

    // BtnAccept, DAT_0061157c.. = {0.089, 0.021} / {-0.11, -0.03}.
    {
        float const Position[3] = {-0.11f, -0.03f, 0.0f};
        float const Half[3] = {0.089f, 0.021f, 0.0f};
        BtnAccept = static_cast<RichTextWireButtonControl*>(CreateRichTextWireButtonControl(
            "BtnAccept", "ui_select_item", Position, Half, 0, 0, 0x1b80, 0, 0, 0));
        RDLRefNode Ref(Texts[kNnGroupTextAccept]);
        (*BtnAccept->TextWinRef)->AppendNode(Ref);
    }
    // BtnDecline, DAT_0061158c.. = {0.089, 0.021} / {0.12, -0.03}.
    {
        float const Position[3] = {0.12f, -0.03f, 0.0f};
        float const Half[3] = {0.089f, 0.021f, 0.0f};
        BtnDecline = static_cast<RichTextWireButtonControl*>(CreateRichTextWireButtonControl(
            "BtnDecline", "ui_select_item", Position, Half, 0, 0, 0x1b81, 0, 0, 0));
        RDLRefNode Ref(Texts[kNnGroupTextDecline]);
        (*BtnDecline->TextWinRef)->AppendNode(Ref);
    }
    // BtnLeave, DAT_0061159c.. = {0.089, 0.021} / {DAT_0066da5c, -0.1}.
    {
        float const Position[3] = {G_GroupBtnLeaveX, -0.1f, 0.0f};
        float const Half[3] = {0.089f, 0.021f, 0.0f};
        RichTextWireButtonControl* const Leave =
            static_cast<RichTextWireButtonControl*>(CreateRichTextWireButtonControl(
                "BtnLeave", "ui_select_item", Position, Half, 0, 0, 0x1b82, 0, 0, 0));
        BtnLeave = Leave;
        RDLRefNode Ref(Texts[kNnGroupTextLeaveGroup]);
        (*Leave->TextWinRef)->AppendNode(Ref);
    }
    {
        float const Position[3] = {0.23f, 0.23f, 0.0f};    // 0x3e6b851f
        float const Half[3] = {0.02f, 0.02f, 0.0f};        // 0x3ca3d70a
        BtnClose = CreateButtonControl("BtnClose", "Interface\\NeuroNet\\Inventory\\X.3db",
                                       nullptr, "ui_select_item", nullptr, Position, nullptr,
                                       Half, 0, 0, 0, 0, 0x409, 0, 0);
    }
    // AttentionTitle, 0.46 x 0.035 at {-0.23, 0.035 + 0.2, 0}.
    {
        float const AttachOffset[3] = {-0.23f, 0.035f + 0.2f, 0.0f};
        AttentionTitle = CreateRichTextWin("AttentionTitle", 0.46f, 0.035f, AttachOffset, false);
        StyleNode Style1(kTextStyleBody);
        AttentionTitle->AppendNode(Style1);
        StyleNode Style2(kTextStyleScreenTitle);
        AttentionTitle->AppendNode(Style2);
        RDLRefNode Ref(Texts[kNnGroupTextAttentionRequired]);
        AttentionTitle->AppendNode(Ref);
    }

    BtnLeave->Flags = static_cast<unsigned char>(BtnLeave->Flags | 0x03);
    BtnAccept->Flags = static_cast<unsigned char>(BtnAccept->Flags | 0x03);
    ShowControl(BtnDecline);

    if (G_ChatChannelTable != nullptr)
        SetChannelTable(G_ChatChannelTable);

    RefreshRequestDisplay();
    ShownFrom = G_ChatIdNone;
    ShownShipId = 0;
    WinControlHalfExtentsFromMesh(HalfExtents, &MeshHolder);
    return true;
}

bool NnGroupControl::ProcessMessage(unsigned Msg, unsigned A, unsigned B)
{
    // FUN_00462a40
    if (Msg == 0xef01)
    {
        if (Parent != nullptr)
            SetPosition(Parent->Position);
        RefreshRequestDisplay();
        return true;
    }
    if (Msg == 0xef02)
    {
        if (Parent != nullptr)
            Win3DReparent(this, nullptr);
        Flags = static_cast<unsigned char>(Flags & 0xfc);
        RefreshRequestDisplay();
        return true;
    }
    return WinControl::ProcessMessage(Msg, A, B);
}

void NnGroupControl::UpdateInactive()
{
    // LAB_00462ab0
    if (ChannelTable != nullptr && (Flags & 0x02) == 0)
    {
        bool bChanged = false;
        int const NowMs = GroupNowMsRounded();
        std::list<FNnGroupRequest>::iterator It = Requests.begin();
        while (It != Requests.end())
        {
            // An unsigned age above 20000 ms expires the request.
            if (static_cast<unsigned>(NowMs - It->TimestampMs) <= 0x4e20u)
                break;
            if (It->From.Id != G_ChatIdNone.Id && It->ShipId != 0)
                G_Server->StopTradeRequest(It->From.Id & 0xffffu);    // IServer +0x158
            It = Requests.erase(It);
            bChanged = true;
        }
        // Refresh when the list ran out or stopped with a change.
        if (bChanged)
            RefreshRequestDisplay();
    }
    // Win3D's child walk, inlined.
    Win3D::UpdateInactive();
}

void NnGroupControl::Update()
{
    // FUN_00462ba0
    if (ChannelTable != nullptr && ChannelTableGeneration != ChannelTable->Generation)
    {
        ChannelTableGeneration = ChannelTable->Generation;
        RefreshRequestDisplay();
    }
    Win3D::Update();    // tail jump FUN_005a17b0
}

intptr_t NnGroupControl::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_00462bd0
    if (Msg != 0x26)
        return WinControl::SetProperty(Msg, A, B);

    WinControl* const Clicked = reinterpret_cast<WinControl*>(A);
    if (Clicked == BtnAccept)
    {
        if (ShownFrom.Id == G_ChatIdNone.Id)
            return 0;
        if (ShownShipId == 0)
        {
            // Accept the group invitation (opcode 2).
            IChatServer* const ChatServer = static_cast<IChatServer*>(G_ChatServer);
            if (ChatServer != nullptr)
            {
                FNnGroupWireMessage Message;
                Message.Opcode = 2;
                Message.Payload = ShownFrom.Id & 0xffffu;
                ChatServer->SubmitChat(CHAT_ID(GetPlayerConnectionId() & 0xffffu),
                                       sizeof(Message), &Message, G_ChatIdGroupInvite, -1);
            }
        }
        else
        {
            // Accept the trade -- target the trader, then RequestTrade.
            FClientGameobj* const Self = ClientObjDb_FindObject(GetPlayerCharacterId(), 1);
            FClientGameobj* const Trader = ClientObjDb_FindObject(ShownShipId, 1);
            if (Self != nullptr && Trader != nullptr)
            {
                // The ship bucket's gameobj (null otherwise, used
                // anyway), its object via IObjInspect slot 84 (+0x150), narrowed by 0x503.
                FClientGameobj* const ShipGo = (Self->ManagerKind == 1) ? Self : nullptr;
                CObject* const Obj = const_cast<CObject*>(ShipGo->cobject());
                CShip* const Ship =
                    (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x503u) == 0x503u)
                        ? static_cast<CShip*>(Obj)
                        : nullptr;
                Ship->set_target(static_cast<IObjRW*>(Trader));
                G_Server->RequestTrade(GetPlayerConnectionId(), ShownFrom.Id & 0xffffu);
            }
        }
        Requests.erase(Requests.begin());
        RefreshRequestDisplay();
        if (Requests.empty())
            NnMainButtonOnModalOpen();
        return 0;
    }

    if (Clicked == BtnDecline)
    {
        if (ShownFrom.Id == G_ChatIdNone.Id)
            return WinControl::SetProperty(Msg, A, B);
        if (ShownShipId != 0)
            G_Server->StopTradeRequest(ShownFrom.Id & 0xffffu);
        Requests.erase(Requests.begin());
        RefreshRequestDisplay();
        if (Requests.empty())
            NnMainButtonOnModalOpen();
        return WinControl::SetProperty(Msg, A, B);
    }

    if (Clicked == BtnLeave)
    {
        // Leave the group (opcode 1, own connection id).
        IChatServer* const ChatServer = static_cast<IChatServer*>(G_ChatServer);
        if (ChatServer != nullptr)
        {
            unsigned const ConnWord = GetPlayerConnectionId() & 0xffffu;
            FNnGroupWireMessage Message;
            Message.Opcode = 1;
            Message.Payload = ConnWord;
            ChatServer->SubmitChat(CHAT_ID(ConnWord), sizeof(Message), &Message,
                                   G_ChatIdGroupInvite, -1);
        }
        return 0;
    }

    if (Clicked == BtnClose)
        NnMainButtonOnModalOpen();    // 0x00462e07
    return WinControl::SetProperty(Msg, A, B);
}

void NnGroupControl::RefreshRequestDisplay()
{
    // FUN_00462e30
    ShownFrom = G_ChatIdNone;
    ShownShipId = 0;
    if (!Requests.empty())
    {
        ShownFrom = Requests.front().From;
        ShownShipId = Requests.front().ShipId;
    }

    if (ShownFrom.Id == G_ChatIdNone.Id)
    {
        BtnAccept->Flags = static_cast<unsigned char>(BtnAccept->Flags & 0xfe);
        BtnDecline->Flags = static_cast<unsigned char>(BtnDecline->Flags & 0xfe);
        RequestDisplay->ClearContent();
        JustifyNode Justify(TextJustifyCenter);    // JustifyNode(1)
        RequestDisplay->AppendNode(Justify);
        StyleNode Style(kTextStyleBody);
        RequestDisplay->AppendNode(Style);
        RDLRefNode Ref(Texts[kNnGroupTextNoInvitations]);
        RequestDisplay->AppendNode(Ref);

        // G_NnMainButton && !(Flags & 2) -> FUN_0045da10(0) and
        // FUN_0045da50(0) on it (the plain request icon, no alert colour).
        if (G_NnMainButton != nullptr && (Flags & 0x02) == 0)
        {
            G_NnMainButton->SetGroupButtonAlert(false);
            G_NnMainButton->SetGroupButtonMesh(0);
        }
    }
    else
    {
        BtnAccept->Flags = static_cast<unsigned char>(BtnAccept->Flags | 0x01);
        BtnDecline->Flags = static_cast<unsigned char>(BtnDecline->Flags | 0x01);
        RequestDisplay->ClearContent();
        {
            JustifyNode Justify(TextJustifyCenter);    // JustifyNode(1)
            RequestDisplay->AppendNode(Justify);
            StyleNode Style(kTextStyleBody);
            RequestDisplay->AppendNode(Style);
        }

        RichTextWin* const AcceptCaption = *BtnAccept->TextWinRef;
        RichTextWin* const DeclineCaption = *BtnDecline->TextWinRef;
        if (ShownShipId == 0)
        {
            // A group invitation.
            RDLRefNode What(Texts[kNnGroupTextInviteFrom]);
            RequestDisplay->AppendNode(What);
            AcceptCaption->ClearContent();
            RDLRefNode AcceptRef(Texts[kNnGroupTextAccept]);
            AcceptCaption->AppendNode(AcceptRef);
            DeclineCaption->ClearContent();
            RDLRefNode DeclineRef(Texts[kNnGroupTextDecline]);
            DeclineCaption->AppendNode(DeclineRef);
        }
        else
        {
            // A trade request.
            RDLRefNode What(Texts[kNnGroupTextTradeFrom]);
            RequestDisplay->AppendNode(What);
            AcceptCaption->ClearContent();
            RDLRefNode AcceptRef(Texts[kNnGroupTextAcceptTrade]);
            AcceptCaption->AppendNode(AcceptRef);
            DeclineCaption->ClearContent();
            RDLRefNode DeclineRef(Texts[kNnGroupTextDeclineTrade]);
            DeclineCaption->AppendNode(DeclineRef);
        }

        {
            ParagraphNode Para;
            RequestDisplay->AppendNode(Para);
            JustifyNode Justify(TextJustifyCenter);    // JustifyNode(1)
            RequestDisplay->AppendNode(Justify);
            StyleNode Style(0xd5ec);
            RequestDisplay->AppendNode(Style);
        }

        // A non-null name list from the channel table goes in through FUN_0057b3e0.
        if (ChannelTable != nullptr)
        {
            RenderDisplayList* const Name = ChannelTable->FindName(ShownFrom);
            if (Name != nullptr)
                RequestDisplay->AppendContent(*Name);
        }

        // G_NnMainButton && !(Flags & 2) -> FUN_0045da10(1) and
        // FUN_0045da50(ShipId == 0 ? 2 : 1) -- the group or the trade request icon.
        if (G_NnMainButton != nullptr && (Flags & 0x02) == 0)
        {
            G_NnMainButton->SetGroupButtonAlert(true);
            G_NnMainButton->SetGroupButtonMesh(ShownShipId == 0 ? 2 : 1);
        }
    }

    // BtnLeave shows while the group has members.
    if (!GroupMembers.empty())
        BtnLeave->Flags = static_cast<unsigned char>(BtnLeave->Flags | 0x01);
    else
        BtnLeave->Flags = static_cast<unsigned char>(BtnLeave->Flags & 0xfe);
}

void NnGroupControl::AddTradeRequest(unsigned ShipId)
{
    // FUN_004632f0. A pending request from this ship already exists.
    for (std::list<FNnGroupRequest>::iterator It = Requests.begin(); It != Requests.end(); ++It)
    {
        if (It->ShipId != 0 && It->ShipId == ShipId)
            return;
    }

    // The sender is the ship's owning player when its object is a
    // CSimple (class bits 3), else FUN_0054bb00 (DAT_00673344); no object -> {0}.
    FNnGroupRequest Request;
    Request.TimestampMs = 0;
    Request.From = G_ChatIdNone;
    Request.ShipId = ShipId;
    FClientGameobj* const Go = ClientObjDb_FindObject(ShipId, 1);
    if (Go != nullptr)
    {
        CObject* const Obj = Go->GetObject();
        unsigned Owner;
        if (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x3u) == 0x3u)
            Owner = static_cast<CSimple*>(Obj)->OwnerPlayer;
        else
            Owner = GetPlayerConnectionId();
        Request.From = ChatIdForPlayer(Owner & 0xffffu);
    }

    Request.TimestampMs = GroupNowMsRounded();
    Requests.push_back(Request);
    RefreshRequestDisplay();

    // "Trade request from" + the sender's name, or the entry discarded.
    FChatMessageEntry* const Entry = ChatAcquireEntry();
    RenderDisplayList* const Name = G_ChatChannelTable->FindName(Request.From);
    if (Name == nullptr)
    {
        DiscardEntry(Entry);
        return;
    }
    StyleNode Style(kTextStyleNotice);
    RdlAppendNode(Entry->Body, Style.Clone());
    AppendPrefixAndName(Entry, Texts[kNnGroupTextTradeFrom], Name);
    ChatCommitEntries();
}

void NnGroupControl::HandleInviteMessage(void const* Buffer, unsigned SizeBytes)
{
    // FUN_00463630
    if (SizeBytes != 8)
    {
        GroupNotice(0x8df, "Received mis-sized invite message.\n");
        return;
    }

    FChatMessageEntry* const Entry = ChatAcquireEntry();
    FNnGroupWireMessage const* const Message = static_cast<FNnGroupWireMessage const*>(Buffer);
    unsigned const Payload = Message->Payload;
    CHAT_ID const Key = ChatIdForPlayer(Payload & 0xffffu);

    // Each case either appends to Entry and commits (LAB_00464b5b: FUN_004686e0 inlined, then
    // FUN_004684e0) or discards it (caseD_11 / caseD_13).
    RenderDisplayList* Name = nullptr;
    switch (Message->Opcode)
    {
    case 0:
    case 1:
    case 2:
        // Server-side opcodes.
        GroupNotice(0x8ef, "Received a group command on a client. This is illegal.\n");
        DiscardEntry(Entry);
        return;

    case 3:
        Name = FindChatNameForWord(Payload);
        if (Name == nullptr)
            break;
        AppendPrefixAndName(Entry, Texts[kNnGroupTextInviteSent], Name);
        ChatCommitEntries();
        return;

    case 4:
    {
        // An identical pending invitation -> nothing.
        for (std::list<FNnGroupRequest>::iterator It = Requests.begin(); It != Requests.end();
             ++It)
        {
            if (It->From.Id == Key.Id && It->ShipId == 0)
            {
                DiscardEntry(Entry);
                return;
            }
        }
        FNnGroupRequest Request;
        Request.TimestampMs = GroupNowMsRounded();
        Request.From = Key;
        Request.ShipId = 0;
        Requests.push_back(Request);
        RefreshRequestDisplay();

        Name = FindChatNameForWord(Payload);
        if (Name == nullptr)
            break;
        StyleNode Style(kTextStyleNotice);
        RdlAppendNode(Entry->Body, Style.Clone());
        AppendPrefixAndName(Entry, Texts[kNnGroupTextInviteFrom], Name);
        ChatCommitEntries();
        return;
    }

    case 5:
    {
        // Every pending invitation (ShipId 0, From set) is dropped.
        bool bErased = false;
        std::list<FNnGroupRequest>::iterator It = Requests.begin();
        while (It != Requests.end())
        {
            if (It->ShipId == 0 && It->From.Id != G_ChatIdNone.Id)
            {
                It = Requests.erase(It);
                bErased = true;
            }
            else
            {
                ++It;
            }
        }
        if (bErased)
            RefreshRequestDisplay();
        GroupMembers.insert(Key.Id);
        RefreshRequestDisplay();

        Name = FindChatNameForWord(Payload);
        if (Name == nullptr)
            break;
        AppendPrefixAndName(Entry, Texts[kNnGroupTextJoinedGroup], Name);
        ChatCommitEntries();
        return;
    }

    case 6:
        // The invitation from this player, if pending, is dropped.
        if (Key.Id != G_ChatIdNone.Id)
        {
            for (std::list<FNnGroupRequest>::iterator It = Requests.begin();
                 It != Requests.end(); ++It)
            {
                if (It->ShipId == 0 && It->From.Id == Key.Id)
                {
                    Requests.erase(It);
                    RefreshRequestDisplay();
                    break;
                }
            }
        }
        GroupMembers.insert(Key.Id);
        RefreshRequestDisplay();

        Name = FindChatNameForWord(Payload);
        if (Name == nullptr)
            break;
        AppendPrefixAndName(Entry, Texts[kNnGroupTextNewMember], Name);
        ChatCommitEntries();
        return;

    case 7:
    {
        GroupMembers.clear();
        RefreshRequestDisplay();

        // A player ship flying behaviour 10 (formation) is switched to
        // maneuver 0x13.
        HUD_Maneuvers* const Hud = GetHudManeuvers();
        if (Hud != nullptr)
        {
            void* const PlayerRw = GetPlayerObjRW();
            if (PlayerRw != nullptr)
            {
                FClientGameobj* const PlayerGo =
                    static_cast<FClientGameobj*>(static_cast<IObjRW*>(PlayerRw));
                CObject* const Obj = PlayerGo->GetObject();
                if (Obj != nullptr &&
                    (static_cast<unsigned>(Obj->ObjectClass) & 0x503u) == 0x503u)
                {
                    IBehaviorManager* const Behavior =
                        static_cast<CEqObj*>(Obj)->get_behavior_interface();
                    if (Behavior != nullptr && Behavior->CurrentBehaviorIndex == 10)
                        Hud->RequestManeuver(0x13, false);
                }
            }
        }

        AppendPrefixAndName(Entry, Texts[kNnGroupTextLeftGroup], nullptr);
        ChatCommitEntries();
        return;
    }

    case 8:
        GroupMembers.erase(Key.Id);
        RefreshRequestDisplay();

        Name = FindChatNameForWord(Payload);
        if (Name == nullptr)
            break;
        AppendPrefixAndName(Entry, Texts[kNnGroupTextMemberLeft], Name);
        ChatCommitEntries();
        return;

    case 9:
        AppendPrefixAndName(Entry, Texts[kNnGroupTextCannotInviteSelf], nullptr);
        ChatCommitEntries();
        return;

    case 0xa:
        AppendPrefixAndName(Entry, Texts[kNnGroupTextGroupFull], nullptr);
        ChatCommitEntries();
        return;

    case 0xb:
        Name = FindChatNameForWord(Payload);
        if (Name == nullptr)
            break;
        AppendPrefixAndName(Entry, Texts[kNnGroupTextAlreadyInYourGroup], Name);
        ChatCommitEntries();
        return;

    case 0xc:
        AppendPrefixAndName(Entry, Texts[kNnGroupTextCannotJoinOwnGroup], nullptr);
        ChatCommitEntries();
        return;

    case 0xd:
        Name = FindChatNameForWord(Payload);
        if (Name == nullptr)
            break;
        AppendPrefixAndName(Entry, Texts[kNnGroupTextNotInvited], Name);
        ChatCommitEntries();
        return;

    case 0xe:
        Name = FindChatNameForWord(Payload);
        if (Name == nullptr)
            break;
        AppendPrefixAndName(Entry, Texts[kNnGroupTextAlreadyMember], Name);
        ChatCommitEntries();
        return;

    case 0xf:
        // The full payload dword against DAT_00673344.
        if (Payload == GetPlayerConnectionId())
        {
            AppendPrefixAndName(Entry, Texts[kNnGroupTextAbortMission], nullptr);
            ChatCommitEntries();
            return;
        }
        Name = FindChatNameForWord(Payload);
        if (Name == nullptr)
            break;
        AppendPrefixAndName(Entry, Texts[kNnGroupTextJoinFailedMission], Name);
        ChatCommitEntries();
        return;

    case 0x10:
        Name = FindChatNameForWord(Payload);
        if (Name == nullptr)
            break;
        AppendPrefixAndName(Entry, Texts[kNnGroupTextGroupNowFull], Name);
        ChatCommitEntries();
        return;

    case 0x12:
        AppendPrefixAndName(Entry, Texts[kNnGroupTextNotInGroup], nullptr);
        ChatCommitEntries();
        return;

    case 0x14:
        Name = FindChatNameForWord(Payload);
        if (Name == nullptr)
            break;
        AppendPrefixAndName(Entry, Texts[kNnGroupTextPlayerDied], Name);
        ChatCommitEntries();
        return;

    default:
        // 0x11, 0x13 and anything above 0x14.
        break;
    }

    // No name, or an opcode with nothing to say.
    DiscardEntry(Entry);
}
