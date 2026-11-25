// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/Pages/page_server_select.h"

#include "NeuroNet/Chat/chat_init.h"         // ChatAppendRdlCopy (the FUN_0046c800 / FUN_005754b0 clone loop)
#include "Network/connect_flow.h"      // ConnectToServer (FUN_005a8770)
#include "dacom.h"             // FDUMP
#include "Common/Fonts/font_imp.h"          // FindMenuFontImp (FUN_004177b0)
#include "Core/globals.h"           // g_client (DAT_0067ecb8), g_display, GetClientFrameTimeCapped
#include "Common/Fonts/ids_rdl.h"           // LoadIdsIntoRdl, RdlAppendNode, RdlClear, RdlRemoveLastParagraph
#include "Common/Fonts/ids_strings.h"       // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "Common/input_manager.h"     // MouseSetCursor (FUN_0041dde0)
#include "Interface/Pages/intro_page_util.h"   // the inline RDL / Win3D idioms shared with PageCharacterSelect
#include "Interface/loading_screen.h"    // LoadingScreenStart (FUN_00579290)
#include "Network/login_mgr.h"         // G_LoginMgr (DAT_0067e7b8), G_bInternetServerList
#include "Interface/menu_ui_init.h"      // PlayUiSoundSlot (FUN_00564570), PlayOrRewindUiSoundSlot
#include "NeuroNet/nn_prompt.h"         // NnPromptRunTextEntry (FUN_00473f70)
#include "NeuroNet/nn_text_control.h"   // NnTextControl (TextControl, FUN_0058a110)
#include "NeuroNet/nn_ui_pulse.h"       // G_HudChromeColorRgba (DAT_00679bb4)
#include "Core/reg_key_stack.h"     // FRegKeyStack
#include "Interface/server_filter_dialog.h" // ServerFilterDialogRun (FUN_005637b0)
#include "Interface/tooltip_hover.h"     // FToolTipHoverInfo
#include "Interface/ui_fast_table.h"
#include "Interface/ui_rich_text_win.h"
#include "Interface/ui_scroll.h"
#include "Interface/ui_text_msg_button.h"
#include "Interface/win3d_immediate.h"   // Win3DVec3Add (FUN_00423b30)
#include "Interface/win_factory.h"       // WinFactoryCreate (FUN_0059db20)
#include "Interface/menu_enter_state.h"

#include "common_text.h"

#include <stdint.h>
#include <stdlib.h>
#include <wchar.h>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    // DAT_0067ab20.
    PageServerSelect G_PageServerSelect;

    char const kIntroCpp[] = "E:\\FL\\Scratch\\Source\\Client\\Interface\\Intro.cpp";

    char const kServerTableKey[] = "ServerTable"; // s_ 0x005e2c4c (also the widget name)

    // .rdata:005e2604 -- the column captions.
    unsigned const kColumnHeaderIds[8] = {0x747, 0x745, 0x746, 0x748,
                                          0x749, 0x74a, 0x74b, 0x74c};
    // .rdata:005e2624 -- the header cells' tool tips.
    unsigned const kColumnHeaderTipIds[8] = {0xac0, 0xabe, 0xabf, 0xac1,
                                             0xac2, 0xac3, 0xac4, 0xac5};
    // .rdata:005e2644 -- the column edges Create writes when the registry has none.
    float const kDefaultColumnEdges[8] = {0.0f,         0.371596009f, 0.553894997f,
                                          0.664525986f, 0.756824017f, 0.875788987f,
                                          0.992121994f, 1.06441998f};

    // -----------------------------------------------------------------------------------------
    // The sixteen column comparators (cdecl, "A before B"). CLoginMgr::GetFilteredServers sorts
    // with one of them. Unsigned compares throughout.

    // LAB_00565e50 -- column 0.
    bool CompareNameAsc(FServerInfo const* A, FServerInfo const* B)
    {
        return _wcsicoll(A->Name.c_str(), B->Name.c_str()) < 0;
    }

    // FUN_00565db0 -- column 1, the address (+0x10).
    bool CompareAddressAsc(FServerInfo const* A, FServerInfo const* B)
    {
        return static_cast<unsigned>(A->IpAddress) < static_cast<unsigned>(B->IpAddress);
    }

    // FUN_00565dd0 -- column 2: known logins first; two known -> the later login time first;
    // two unknown -> by name.
    bool CompareKnownAsc(FServerInfo const* A, FServerInfo const* B)
    {
        bool const bKnownA = G_LoginMgr.IsKnownLogin(*A);
        bool const bKnownB = G_LoginMgr.IsKnownLogin(*B);
        if (bKnownA)
        {
            if (!bKnownB)
                return true;
            unsigned const TimeA = G_LoginMgr.GetLoginTime(*A);
            unsigned const TimeB = G_LoginMgr.GetLoginTime(*B);
            return TimeA > TimeB;
        }
        if (bKnownB)
            return false;
        return A->Name < B->Name;
    }

    // FUN_00565e90 -- column 3 (unsigned, so pings -1 / -2 sort last).
    bool ComparePingAsc(FServerInfo const* A, FServerInfo const* B)
    {
        return static_cast<unsigned>(A->Ping) < static_cast<unsigned>(B->Ping);
    }

    // FUN_00565eb0 -- column 4: players, then the maximum.
    bool ComparePlayersAsc(FServerInfo const* A, FServerInfo const* B)
    {
        if (A->PlayerCount != B->PlayerCount)
            return static_cast<unsigned>(A->PlayerCount) < static_cast<unsigned>(B->PlayerCount);
        return static_cast<unsigned>(A->MaxPlayers) < static_cast<unsigned>(B->MaxPlayers);
    }

    // FUN_00565ee0 -- column 5.
    bool CompareVersionAsc(FServerInfo const* A, FServerInfo const* B)
    {
        return A->Version < B->Version;
    }

    // FUN_00565f00 -- column 6: LAN (+0x3e) first; ties fall back to the pointers.
    bool CompareLanAsc(FServerInfo const* A, FServerInfo const* B)
    {
        if (A->bLan != 0)
        {
            if (B->bLan == 0)
                return true;
        }
        else if (B->bLan != 0)
        {
            return false;
        }
        return std::less<FServerInfo const*>()(A, B);
    }

    // FUN_00565f30 -- column 7: password first, then PvP, then the pointers.
    bool CompareLockAsc(FServerInfo const* A, FServerInfo const* B)
    {
        if (A->bPassword != 0)
        {
            if (B->bPassword == 0)
                return true;
        }
        else if (B->bPassword != 0)
        {
            return false;
        }
        if (A->bAllowPvp != 0)
        {
            if (B->bAllowPvp == 0)
                return true;
        }
        else if (B->bAllowPvp != 0)
        {
            return false;
        }
        return std::less<FServerInfo const*>()(A, B);
    }

    // LAB_00566030 -- the descending comparators mirror the ascending ones.
    bool CompareNameDesc(FServerInfo const* A, FServerInfo const* B)
    {
        return _wcsicoll(A->Name.c_str(), B->Name.c_str()) > 0;
    }

    // FUN_00565fa0.
    bool CompareAddressDesc(FServerInfo const* A, FServerInfo const* B)
    {
        return static_cast<unsigned>(B->IpAddress) < static_cast<unsigned>(A->IpAddress);
    }

    // FUN_00565fc0.
    bool CompareKnownDesc(FServerInfo const* A, FServerInfo const* B)
    {
        bool const bKnownA = G_LoginMgr.IsKnownLogin(*A);
        bool const bKnownB = G_LoginMgr.IsKnownLogin(*B);
        if (bKnownA)
        {
            if (!bKnownB)
                return false;
            unsigned const TimeA = G_LoginMgr.GetLoginTime(*A);
            unsigned const TimeB = G_LoginMgr.GetLoginTime(*B);
            return TimeA < TimeB;
        }
        if (bKnownB)
            return true;
        return A->Name > B->Name;
    }

    // FUN_00566070.
    bool ComparePingDesc(FServerInfo const* A, FServerInfo const* B)
    {
        return static_cast<unsigned>(B->Ping) < static_cast<unsigned>(A->Ping);
    }

    // FUN_00566090.
    bool ComparePlayersDesc(FServerInfo const* A, FServerInfo const* B)
    {
        if (B->PlayerCount != A->PlayerCount)
            return static_cast<unsigned>(B->PlayerCount) < static_cast<unsigned>(A->PlayerCount);
        return static_cast<unsigned>(B->MaxPlayers) < static_cast<unsigned>(A->MaxPlayers);
    }

    // FUN_005660c0.
    bool CompareVersionDesc(FServerInfo const* A, FServerInfo const* B)
    {
        return B->Version < A->Version;
    }

    // FUN_005660e0.
    bool CompareLanDesc(FServerInfo const* A, FServerInfo const* B)
    {
        if (A->bLan != 0)
        {
            if (B->bLan == 0)
                return false;
        }
        else if (B->bLan != 0)
        {
            return true;
        }
        return std::less<FServerInfo const*>()(B, A);
    }

    // FUN_00566110.
    bool CompareLockDesc(FServerInfo const* A, FServerInfo const* B)
    {
        if (A->bPassword != 0)
        {
            if (B->bPassword == 0)
                return false;
        }
        else if (B->bPassword != 0)
        {
            return true;
        }
        if (A->bAllowPvp != 0)
        {
            if (B->bAllowPvp == 0)
                return false;
        }
        else if (B->bAllowPvp != 0)
        {
            return true;
        }
        return std::less<FServerInfo const*>()(B, A);
    }

    // .data:00614aa0 / .data:00614ac0.
    FServerCompare const kAscendingCompare[8] = {
        CompareNameAsc,    CompareAddressAsc, CompareKnownAsc,  ComparePingAsc,
        ComparePlayersAsc, CompareVersionAsc, CompareLanAsc, CompareLockAsc};
    FServerCompare const kDescendingCompare[8] = {
        CompareNameDesc,    CompareAddressDesc, CompareKnownDesc,  ComparePingDesc,
        ComparePlayersDesc, CompareVersionDesc, CompareLanDesc, CompareLockDesc};

}

using namespace IntroPageUtil;

// ---------------------------------------------------------------------------------------------
// ServerRowData

PageServerSelect::ServerRowData::ServerRowData(FServerInfo const& Source)
{
    // Retail default-constructs Info inline, then assigns it memberwise.
    Info = Source;
}

PageServerSelect::ServerRowData::~ServerRowData()
{
}

// ---------------------------------------------------------------------------------------------

PageServerSelect::PageServerSelect()
    : UIPage::MasterWin(Desc.GetPageData())
    , Result(0)
    , Connect(nullptr)
    , MainMenu(nullptr)
    , SetFilter(nullptr)
    , ServerTable(nullptr)
    , ServerScroll(nullptr)
    , ListServerStatus(nullptr)
    , ServerDetails(nullptr)
    , Refresh(nullptr)
    , Title(nullptr)
    , SubTitle(nullptr)
    , ServerInfoBox(nullptr)
    , ServerSelectBox(nullptr)
    , Legend(nullptr)
    , Version(nullptr)
    , InfoBoxHome{0.0f, 0.0f, 0.0f}
    , SelectBoxHome{0.0f, 0.0f, 0.0f}
    , SlideT(0.0)
    , bLeaving(0)
    , LeaveDirection(0)
    , FirstVisible(0)
    , UpPip(nullptr)
    , DownPip(nullptr)
    , Locked(nullptr)
    , Skull(nullptr)
    , GrayLock(nullptr)
    , GraySkull(nullptr)
    , BlueLock(nullptr)
    , BlueSkull(nullptr)
    , ServerConnectImage(nullptr)
    , ServerNoConnectImage(nullptr)
    , bListRequested(0)
    , SortColumn(2)
    , bAscending{1, 1, 1, 1, 1, 1, 1, 1}
    , SelectedServerId(0xffffffffu)
    , ListStatus(3)
{
    // FUN_0056d510. Retail leaves +0xf0..+0x114 and the images unwritten; the static object
    // starts zeroed, which is what the initialisers above reproduce.
}

PageServerSelect::~PageServerSelect()
{
    // FUN_005723b0. The ServerFilter at +0x2a4 has no destructor.
}

bool PageServerSelect::SaveEdges()
{
    // FUN_0056daa0: keys created with KEY_ALL_ACCESS (0x2001f); every key left on the stack is
    // closed at the end, HKCU included, as retail.
    bool bSaved = false;
    FRegKeyStack Keys;
    if (Keys.Create(G_FreelancerRegKey, 0x2001f) == nullptr)
        return false;
    if (Keys.Create(kServerTableKey, 0x2001f) == nullptr)
        return false;
    if (Edges.SaveToRegistry(&Keys))
        bSaved = true;
    Keys.PopClose();
    return bSaved;
}

void PageServerSelect::Destroy()
{
    // FUN_0056d8f0.
    UnregisterEvent(USER_CANCEL);
    if (ServerTable != nullptr)
    {
        // The images are destroyed on the TRC they were made on, fetched each time.
        ServerTable->GetEdges(&Edges);
        SaveEdges();
        ServerTable->GetDeviceTRC()->destroy_image(UpPip);
        ServerTable->GetDeviceTRC()->destroy_image(DownPip);
        ServerTable->GetDeviceTRC()->destroy_image(Locked);
        ServerTable->GetDeviceTRC()->destroy_image(Skull);
        ServerTable->GetDeviceTRC()->destroy_image(GrayLock);
        ServerTable->GetDeviceTRC()->destroy_image(GraySkull);
        ServerTable->GetDeviceTRC()->destroy_image(BlueLock);
        ServerTable->GetDeviceTRC()->destroy_image(BlueSkull);
        Refresh->GetDeviceTRC()->destroy_image(ServerConnectImage);
        Refresh->GetDeviceTRC()->destroy_image(ServerNoConnectImage);
    }
    UIPage::MasterWin::Destroy();

    if (Version != nullptr)
    {
        delete Version;
        Version = nullptr;
    }
    Connect = nullptr;
    MainMenu = nullptr;
    SetFilter = nullptr;
    ServerTable = nullptr;
    ServerScroll = nullptr;
    ServerDetails = nullptr;
    ListServerStatus = nullptr;
    Refresh = nullptr;
    Title = nullptr;
    ServerInfoBox = nullptr;
    ServerSelectBox = nullptr;
    SubTitle = nullptr;
    Legend = nullptr;
    MouseSetCursor("Arrow", true);
}

bool PageServerSelect::Create(Win3D* NewParent, void* /*Payload*/)
{
    // FUN_0056dbf0. Unlike the other intro pages, a failed MasterWin::Create returns at once.
    if (!UIPage::MasterWin::Create(NewParent, nullptr))
        return false;

    bListRequested = 0;
    ListStatus = 3;
    Filter.VersionMin = static_cast<unsigned>(g_client.packed_version);
    Filter.VersionMax = static_cast<unsigned>(g_client.packed_version);
    SelectedServerId = 0xffffffffu;
    Result = 0;

    Connect = static_cast<UiTextMsgButton*>(FindWidget("Connect"));
    MainMenu = static_cast<UiTextMsgButton*>(FindWidget("MainMenu"));
    SetFilter = static_cast<UiTextMsgButton*>(FindWidget("SetFilter"));
    ServerTable = static_cast<UiFastTable*>(FindWidget(kServerTableKey));
    ServerScroll = static_cast<UiScroll*>(FindWidget("ServerScroll"));
    ServerDetails = static_cast<UiRichTextWin*>(FindWidget("ServerDetails"));
    ListServerStatus = static_cast<UiRichTextWin*>(FindWidget("ListServerStatus"));
    Refresh = static_cast<UiTextMsgButton*>(FindWidget("Refresh"));
    Title = static_cast<UiRichTextWin*>(FindWidget("Title"));
    ServerInfoBox = FindWidget("ServerInfoBox");
    ServerSelectBox = FindWidget("ServerSelectBox");
    SubTitle = static_cast<UiRichTextWin*>(FindWidget("SubTitle"));
    Legend = static_cast<UiRichTextWin*>(FindWidget("Legend"));

    // The "Version" TextControl: the IDS 0x4f7 format with the version word's (DAT_0067ecba)
    // high then low byte as its text (property 0x1d); hidden.
    Version = WinFactoryCreate("Version", "TextControl", this, nullptr);
    if (Version != nullptr)
    {
        NnTextControl* const VersionText = static_cast<NnTextControl*>(Version);
        IFont* const NormalFont = FindMenuFontImp("Normal");
        float const Height =
            static_cast<float>(static_cast<unsigned>(NormalFont->GetLineHeight())) * 0.002f;
        float const Position[3] = {0.319999993f, -0.349999994f, -1.0f};
        VersionText->Init("Normal", Position, nullptr, 0.25f, Height, 1.0f, 0u, 1, 0, 0);
        wchar_t Format[0x100];
        GetIdsStringWWithRawFallback(0x4f7u, Format, 0x100);
        unsigned const VersionWord =
            (static_cast<unsigned>(g_client.packed_version) >> 16) & 0xffffu;
        wchar_t Text[0x100];
        swprintf(Text, 0x100, Format, (VersionWord >> 8) & 0xffu, VersionWord & 0xffu);
        VersionText->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(Text), 0);
        HideWin3D(Version);
    }

    RegisterEvent(USER_CANCEL);

    // The last two images are made on Refresh's TRC.
    UpPip = ServerTable->GetDeviceTRC()->create_image("interface\\intro\\images\\uppip.bmp");
    DownPip = ServerTable->GetDeviceTRC()->create_image("interface\\intro\\images\\DownPip.bmp");
    Locked = ServerTable->GetDeviceTRC()->create_image("interface\\intro\\images\\Locked.bmp");
    Skull = ServerTable->GetDeviceTRC()->create_image("interface\\intro\\images\\Skull.bmp");
    GrayLock =
        ServerTable->GetDeviceTRC()->create_image("interface\\intro\\images\\GrayLocked.bmp");
    GraySkull =
        ServerTable->GetDeviceTRC()->create_image("interface\\intro\\images\\GraySkull.bmp");
    BlueLock =
        ServerTable->GetDeviceTRC()->create_image("interface\\intro\\images\\BlueLocked.bmp");
    BlueSkull =
        ServerTable->GetDeviceTRC()->create_image("interface\\intro\\images\\BlueSkull.bmp");
    ServerConnectImage =
        Refresh->GetDeviceTRC()->create_image("interface\\intro\\images\\MULTI_ServerConnect.bmp");
    ServerNoConnectImage = Refresh->GetDeviceTRC()->create_image(
        "interface\\intro\\images\\MULTI_ServerNoConnect.bmp");

    Connect->GetToolTipHoverInfo()->IdsAlt = 0xac7;
    MainMenu->GetToolTipHoverInfo()->IdsAlt = 0xac6;
    SetFilter->GetToolTipHoverInfo()->IdsAlt = 0xaf0;
    ServerDetails->GetToolTipHoverInfo()->IdsAlt = 0xac8;
    ListServerStatus->GetToolTipHoverInfo()->IdsAlt = 0xade;
    Refresh->GetToolTipHoverInfo()->IdsAlt = 0xac9;

    ServerTable->SetTipMode(3); // one tip per cell

    // In retail's order.
    LoadIds(TextYes, 0x727);
    LoadIds(TextNo, 0x728);
    RdlClear(PingUnknown);
    {
        TextNode const Node(L"????", -1); // .rdata 0x005e2c84
        AppendNodeClone(PingUnknown, Node);
    }
    RdlClear(PingNone);
    {
        TextNode const Node(L"-", -1); // DAT_005e2c7c
        AppendNodeClone(PingNone, Node);
    }
    LoadIds(ServerNameText, 0x729);
    LoadIds(DetailsHeader, 0x744);
    LoadIds(DetailsPassword, 0x750);
    LoadIds(DetailsNoNewPlayers, 0x74f);
    LoadIdsCut(DetailsRestricted, 0x6007c);
    LoadStyledIds(SubTitleText, kTextStyleScreenSubtitle, 0x6007d);
    LoadStyledIds(TitleText, kTextStyleScreenTitle, 0x60072);
    LoadStyledIds(StatusDisconnected, kTextStyleDisabled, 0x60058);
    LoadStyledIds(StatusConnecting, kTextStyleDisabled, 0x60057);
    LoadStyledIds(StatusConnected, kTextStyleBody, 0x60055);
    LoadStyledIds(StatusConnectFailed, kTextStyleDisabled, 0x60056);
    LoadIdsCut(LegendSkullText, 0x6007b);
    LoadIdsCut(LegendLockText, 0x6007a);
    LoadStyledIds(EsrbNoticeText, 0x5724, 0x601eb);
    LoadStyledIds(SubTitleLan, 0xc9b7, 0x601ef);
    LoadStyledIds(SubTitleInternet, 0xc9b7, 0x601ee);

    for (unsigned Column = 0; Column < 8; ++Column)
    {
        LoadIds(ColumnHeaders[Column], kColumnHeaderIds[Column]);
        FToolTipHoverInfo* const Tip = ServerTable->GetCellTip(0, Column);
        if (Tip != nullptr)
            Tip->IdsAlt = kColumnHeaderTipIds[Column];
    }

    ServerTable->SetFrameColor(ChromeColor());
    ServerTable->SetGrabDistance(0.00400000019f);
    ServerTable->SetResizable(false, true);
    ServerTable->SetLayoutFlag(true);
    ServerTable->SetHeaderRows(1);
    ServerTable->SetBorders(false, false, false, true);
    ServerTable->SetRowHighlights(false);
    ServerTable->SetCellMargins(2, 2);

    // Range and page convert the row count as an unsigned dword.
    ServerScroll->SetArrowColors(ChromeColor(), ChromeColor());
    ServerScroll->SetBarColors(ChromeColor(), ChromeColor());
    ServerScroll->SetRange(
        0.0f, static_cast<float>(static_cast<unsigned>(EdgeCount(ServerTable->RowEdges) - 1)));
    ServerScroll->SetPage(
        static_cast<float>(static_cast<unsigned>(EdgeCount(ServerTable->RowEdges) - 1)));
    ServerScroll->SetStep(1.0f);
    ServerScroll->SetValue(0.0f);
    HideWin3D(ServerScroll);
    ServerDetails->SetAutoScrollbar(true);

    // The saved edges from HKCU\...\ServerTable (KEY_READ).
    bool bLoaded = false;
    {
        FRegKeyStack Keys;
        if (Keys.Open(G_FreelancerRegKey, 0x20019) != nullptr &&
            Keys.Open(kServerTableKey, 0x20019) != nullptr)
        {
            if (Edges.LoadFromRegistry(&Keys))
                bLoaded = true;
            Keys.PopClose();
        }
        Keys.PopClose();
    }
    // Nothing saved: the table's own edges with the default column edges written over the first
    // eight (no size check, as retail).
    if (!bLoaded)
    {
        ServerTable->GetEdges(&Edges);
        float* const Cols = Edges.Cols.data();
        for (int Index = 0; Index < 8; ++Index)
            Cols[Index] = kDefaultColumnEdges[Index];
    }
    ServerTable->SetEdges(&Edges);

    // With eight column edges the header row gets its captions and stops hovering.
    if (ServerTable->ColEdges.size() == 8)
    {
        for (unsigned Column = 0; Column < 8; ++Column)
            SetCell(0, Column, &ColumnHeaders[Column], false, false);
        ServerTable->GetRow(0)->bHoverable = 0;
    }

    Title->ClearContent();
    {
        JustifyNode const Justify(TextJustifyCenter);
        Title->AppendNode(Justify);
    }
    {
        RDLRefNode const Ref(TitleText);
        Title->AppendNode(Ref);
    }
    SubTitle->ClearContent();
    {
        JustifyNode const Justify(TextJustifyLeft);
        SubTitle->AppendNode(Justify);
    }
    {
        RDLRefNode const Ref(SubTitleText);
        SubTitle->AppendNode(Ref);
    }
    {
        ParagraphNode const Paragraph;
        SubTitle->AppendNode(Paragraph);
    }
    SubTitle->AppendContent(G_bInternetServerList != 0 ? SubTitleInternet : SubTitleLan);
    Refresh->bInteractive = 1;

    int const RowCount = EdgeCount(ServerTable->RowEdges);
    for (int Row = 1; Row < RowCount; ++Row)
    {
        for (unsigned Column = 0; Column < ServerTable->ColEdges.size(); ++Column)
            ServerTable->ClearCell(static_cast<unsigned>(Row), Column);
        FUiFastTableRow* const RowRecord = ServerTable->GetRow(static_cast<unsigned>(Row));
        if (RowRecord != nullptr)
            RowRecord->Payload.Assign(nullptr);
        ServerTable->GetRow(static_cast<unsigned>(Row))->bSelectable = 0;
        ServerTable->GetRow(static_cast<unsigned>(Row))->bHoverable = 0;
    }

    RefreshDetailsText();

    ListServerStatus->SetAlignMode(2);
    RefreshListStatus(); // inlined in retail

    Legend->ClearContent();
    {
        StyleNode const Style(0x76ec);
        Legend->AppendNode(Style);
    }
    {
        JustifyNode const Justify(TextJustifyCenter);
        Legend->AppendNode(Justify);
    }
    {
        ImageNode const Image(BlueLock);
        Legend->AppendNode(Image);
    }
    {
        RDLRefNode const Ref(LegendLockText);
        Legend->AppendNode(Ref);
    }
    {
        TextNode const Space(L" ", -1); // .rdata 0x005e2c58
        Legend->AppendNode(Space);
    }
    {
        ImageNode const Image(BlueSkull);
        Legend->AppendNode(Image);
    }
    {
        RDLRefNode const Ref(LegendSkullText);
        Legend->AppendNode(Ref);
    }

    SlideT = -1.5;
    bLeaving = 0;
    LeaveDirection = 0;
    HideWin3D(ServerTable);
    HideWin3D(ServerScroll);
    HideWin3D(Legend);
    HideWin3D(ServerDetails);
    HideWin3D(Title);
    HideWin3D(SubTitle);
    HideWin3D(ListServerStatus);
    Connect->SetSkipUpload(true);
    MainMenu->SetSkipUpload(true);
    SetFilter->SetSkipUpload(true);
    Refresh->SetSkipUpload(true);
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        InfoBoxHome[Axis] = ServerInfoBox->Position[Axis];
        SelectBoxHome[Axis] = ServerSelectBox->Position[Axis];
    }

    // Retail inlines RefreshDetailsText a second time here.
    RefreshDetailsText();
    return true;
}

void PageServerSelect::SetCell(unsigned Row, unsigned Column, RenderDisplayList const* Content,
                               bool bVersionMatch, bool bSelected)
{
    // FUN_0056f180.
    FUiFastTableCell* Cell = nullptr;
    if (!ServerTable->GetCell(Row, Column, &Cell))
        return;
    RdlClear(Cell->Source);

    bool bSortArrow = false;
    if (Row == 0)
    {
        if (Column == SortColumn)
        {
            StyleNode const Style(kTextStyleColumnHeaderSorted);
            AppendNodeClone(Cell->Source, Style);
            bSortArrow = true;
        }
        else
        {
            StyleNode const Style(kTextStyleColumnHeader);
            AppendNodeClone(Cell->Source, Style);
        }
        JustifyNode const Justify(TextJustifyCenter);
        AppendNodeClone(Cell->Source, Justify);
    }
    else
    {
        unsigned short const StyleId =
            !bVersionMatch ? kTextStyleDisabled
                           : (bSelected ? kTextStyleRowSelected
                                        : kTextStyleRow);
        {
            StyleNode const Style(StyleId);
            AppendNodeClone(Cell->Source, Style);
        }
        JustifyNode const Justify(Column == 0 ? TextJustifyLeft : TextJustifyCenter);
        AppendNodeClone(Cell->Source, Justify);
    }

    ChatAppendRdlCopy(Cell->Source, *Content);

    // DownPip while ascending, UpPip while descending.
    if (bSortArrow)
    {
        ImageNode const Arrow(bAscending[Column] != 0 ? DownPip : UpPip);
        AppendNodeClone(Cell->Source, Arrow);
    }
    ServerTable->InvalidateCell(Row, Column);
}

void PageServerSelect::RefreshDetailsText()
{
    // FUN_0056f610.
    ServerDetails->ClearContent();
    {
        StyleNode const Style(kTextStyleList);
        ServerDetails->AppendNode(Style);
    }
    {
        RDLRefNode const Ref(DetailsHeader);
        ServerDetails->AppendNode(Ref);
    }
    ParagraphNode const Paragraph;
    ServerDetails->AppendNode(Paragraph);
}

void PageServerSelect::RefreshListStatus()
{
    // FUN_0056fc90.
    Refresh->bInteractive = 1;
    ListServerStatus->ClearContent();
    {
        JustifyNode const Justify(TextJustifyRight);
        ListServerStatus->AppendNode(Justify);
    }
    if (ListStatus == 1 || ListStatus == 4)
    {
        RDLRefNode const Ref(StatusConnecting);
        ListServerStatus->AppendNode(Ref);
    }
    else if (ListStatus == 2)
    {
        RDLRefNode const Ref(StatusConnected);
        ListServerStatus->AppendNode(Ref);
        Refresh->bInteractive = 1; // redundant store, as retail
    }
    else
    {
        RDLRefNode const Ref(StatusDisconnected);
        ListServerStatus->AppendNode(Ref);
    }
}

void PageServerSelect::RebuildTable()
{
    // FUN_0056fdf0. The comparator for the sort column and direction (none past column 7).
    FServerCompare Compare = nullptr;
    if (SortColumn < 8)
        Compare = bAscending[SortColumn] != 0 ? kAscendingCompare[SortColumn]
                                              : kDescendingCompare[SortColumn];
    std::vector<FServerInfo*> Servers;
    G_LoginMgr.GetFilteredServers(Servers, Compare, &Filter);
    ServerTable->ClearSelection();

    // The bar is shown (unless leaving) only when the list does not fit.
    int const ServerCount = static_cast<int>(Servers.size());
    int const RowCount = EdgeCount(ServerTable->RowEdges);
    ServerScroll->SetRange(0.0f, static_cast<float>(ServerCount));
    int const VisibleRows = RowCount - 1;
    ServerScroll->SetPage(static_cast<float>(VisibleRows));
    ServerScroll->SetStep(1.0f);
    FirstVisible = static_cast<int>(ServerScroll->GetValue());
    if (ServerCount > VisibleRows && bLeaving == 0)
        ShowWin3D(ServerScroll);
    else
        HideWin3D(ServerScroll);

    int Row = 1;
    for (int Index = FirstVisible; Index < ServerCount; ++Index, ++Row)
    {
        if (Row >= RowCount)
            return;
        FillRow(static_cast<unsigned>(Row), Servers[static_cast<size_t>(Index)]);
        if (Servers[static_cast<size_t>(Index)]->Id == SelectedServerId)
            ServerTable->SelectRow(static_cast<unsigned>(Row));
    }

    // The rows left over are emptied (retail inlines ClearRow).
    for (; Row < RowCount; ++Row)
    {
        for (unsigned Column = 0; Column < ServerTable->ColEdges.size(); ++Column)
            ServerTable->ClearCell(static_cast<unsigned>(Row), Column);
        FUiFastTableRow* const RowRecord = ServerTable->GetRow(static_cast<unsigned>(Row));
        if (RowRecord != nullptr && RowRecord->Payload.Object != nullptr)
            RowRecord->Payload.Reset();
        ServerTable->GetRow(static_cast<unsigned>(Row))->bSelectable = 0;
        ServerTable->GetRow(static_cast<unsigned>(Row))->bHoverable = 0;
    }
}

void PageServerSelect::FillRow(unsigned Row, FServerInfo const* Server)
{
    // FUN_00570040. A server of another version is greyed and inert. Retail fills the columns
    // in the order 1, 3, 2, 0, 4, 5, 6, 7.
    bool const bVersionMatch =
        Server->Version == static_cast<unsigned>(g_client.packed_version);
    bool const bSelected = Server->Id == SelectedServerId;

    {
        RenderDisplayList Text;
        TextNode const Node(Server->Address.c_str(), -1);
        AppendNodeClone(Text, Node);
        SetCell(Row, 1, &Text, bVersionMatch, bSelected);
    }

    // The ping: "????" for -2, "-" for -1 (retail inlines SetCell for both).
    {
        int const Ping = Server->Ping;
        if (Ping == -2 || Ping == -1)
        {
            RenderDisplayList Text;
            RDLRefNode const Ref(Ping == -2 ? PingUnknown : PingNone);
            AppendNodeClone(Text, Ref);
            SetCell(Row, 3, &Text, bVersionMatch, bSelected);
        }
        else
        {
            std::wstringstream Stream;
            Stream << static_cast<unsigned>(Ping) << std::ends;
            std::wstring const PingText = Stream.str();
            RenderDisplayList Text;
            TextNode const Node(PingText.c_str(), -1);
            AppendNodeClone(Text, Node);
            SetCell(Row, 3, &Text, bVersionMatch, bSelected);
        }
    }

    SetCell(Row, 2, G_LoginMgr.IsKnownLogin(*Server) ? &TextYes : &TextNo, bVersionMatch,
            bSelected);

    {
        RenderDisplayList Text;
        TextNode const Node(Server->Name.c_str(), -1);
        AppendNodeClone(Text, Node);
        SetCell(Row, 0, &Text, bVersionMatch, bSelected);
    }

    {
        std::wstringstream Stream;
        Stream << static_cast<unsigned>(Server->PlayerCount) << L"/"
               << static_cast<unsigned>(Server->MaxPlayers) << std::ends;
        std::wstring const PlayersText = Stream.str();
        RenderDisplayList Text;
        TextNode const Node(PlayersText.c_str(), -1);
        AppendNodeClone(Text, Node);
        SetCell(Row, 4, &Text, bVersionMatch, bSelected);
    }

    // "major.minor", recoloured when it is not the client's version.
    {
        std::wstringstream Stream;
        Stream << static_cast<unsigned long>((Server->Version >> 24) & 0xffu) << L"."
               << static_cast<unsigned long>((Server->Version >> 16) & 0xffu) << std::ends;
        RenderDisplayList Text;
        if (Server->Version != static_cast<unsigned>(g_client.packed_version))
        {
            TextRenderAttributes const Attrs = PackTextAttributes(0, 0, 0, 0, 0x1d1dbf);
            TRANode const Tra(Attrs, 0xffffff00u, 0);
            AppendNodeClone(Text, Tra);
        }
        std::wstring const VersionText = Stream.str();
        TextNode const Node(VersionText.c_str(), -1);
        AppendNodeClone(Text, Node);
        SetCell(Row, 5, &Text, bVersionMatch, bSelected);
    }

    SetCell(Row, 6, Server->bLan != 0 ? &TextYes : &TextNo, bVersionMatch, bSelected);

    // The lock and the skull: plain when selected, blue for this version, grey otherwise.
    {
        RenderDisplayList Icons;
        if (Server->bPassword != 0)
        {
            ImageNode const Image(bSelected ? Locked : (bVersionMatch ? BlueLock : GrayLock));
            AppendNodeClone(Icons, Image);
        }
        if (Server->bAllowPvp != 0)
        {
            ImageNode const Image(bSelected ? Skull : (bVersionMatch ? BlueSkull : GraySkull));
            AppendNodeClone(Icons, Image);
        }
        SetCell(Row, 7, &Icons, bVersionMatch, bSelected);
    }

    // Assign takes a reference; the creation reference is dropped. No null test on the row, as
    // retail.
    FUiFastTableRow* const RowRecord = ServerTable->GetRow(Row);
    RowRecord->bSelectable = bVersionMatch ? 1 : 0;
    RowRecord->bHoverable = bVersionMatch ? 1 : 0;
    ServerRowData* const Data = new ServerRowData(*Server);
    RowRecord->Payload.Assign(Data);
    Data->Release();
}

bool PageServerSelect::ProcessMessage(unsigned Msg, unsigned A, unsigned B)
{
    // FUN_005714d0.
    unsigned const TableId = static_cast<unsigned>(reinterpret_cast<uintptr_t>(ServerTable));
    if (Msg == kWinMsgCommand)
    {
        PlayOrRewindUiSoundSlot(0);
        if (A == 0x3e9)
        {
            // "MainMenu".
            BeginLeave();
            LeaveDirection = -1;
            return false;
        }
        if (A == 0x3ea)
        {
            // "SetFilter": a WM_QUIT inside the modal filter dialog quits the process.
            SetToolTipsEnabled(false);
            if (!ServerFilterDialogRun(this, &Filter))
                exit(0);
            SetToolTipsEnabled(true);
            RebuildTable();
            return false;
        }
        if (A == 0x3eb)
        {
            // "Refresh": only before the first list request (CLoginMgr +0x23c).
            if (G_LoginMgr.ServerListPage != 0)
                return false;
            int const RowCount = EdgeCount(ServerTable->RowEdges);
            for (int Row = 1; Row < RowCount; ++Row)
            {
                ClearRow(static_cast<unsigned>(Row));
                ServerTable->GetRow(static_cast<unsigned>(Row))->bSelectable = 0;
                ServerTable->GetRow(static_cast<unsigned>(Row))->bHoverable = 0;
            }
            RefreshDetailsText();
            ServerDetails->SetScrollOrigin(0, 0);
            G_LoginMgr.ResetServerLists();
            G_LoginMgr.RequestServerList();
            return false;
        }
        if (A != 0x3003)
            return false;

        // "Connect".
        FServerInfo const* const Server = GetSelectedServer();
        if (Server == nullptr)
            return true;
        if (!ConnectToServer(Server))
            return true;
        BeginLeave();
        LeaveDirection = 1;
        return true;
    }

    if (Msg == kWinMsgPress)
    {
        // A header click: the same column flips its direction, another becomes the sort column.
        if (A != TableId || (B & 0xffff0000u) != 0)
            return false;
        unsigned const Column = B & 0xffffu;
        if (Column >= 8)
            return false;
        if (Column == SortColumn)
            bAscending[SortColumn] = bAscending[SortColumn] == 0 ? 1 : 0;
        else
            SortColumn = Column;
        for (unsigned Index = 0; Index < 8; ++Index)
            SetCell(0, Index, &ColumnHeaders[Index], false, false);
        ServerTable->GetRow(0)->bHoverable = 0;
        RebuildTable();
        return true;
    }

    if (Msg == kWinMsgSelectionChanged)
    {
        // A row selected: its details (the vertical scroll reset for another server), the new
        // and the previous row redrawn.
        if (A != TableId)
            return false;
        RefreshDetailsText();
        unsigned const PreviousId = SelectedServerId;
        SelectedServerId = 0xffffffffu;
        unsigned SelectedRow = 0;
        if (!ServerTable->GetSelectedRow(&SelectedRow))
            return true;
        ServerRowData* const Data =
            static_cast<ServerRowData*>(ServerTable->GetRow(SelectedRow)->Payload.Object);
        if (Data == nullptr)
            return true;
        ShowServerDetails(&Data->Info, Data->Info.Id != PreviousId);
        SelectedServerId = Data->Info.Id;
        FillRow(SelectedRow, &Data->Info);
        for (unsigned Row = 1; Row < ServerTable->RowEdges.size(); ++Row)
        {
            ServerRowData* const Other =
                static_cast<ServerRowData*>(ServerTable->GetRow(Row)->Payload.Object);
            if (Other != nullptr && Other->Info.Id == PreviousId)
            {
                FillRow(Row, &Other->Info);
                return true;
            }
        }
        return true;
    }

    if (Msg == kWinMsgDoubleClick)
    {
        // A double click on the table: connect and leave.
        if (A != TableId)
            return false;
        FServerInfo const* const Server = GetSelectedServer();
        if (Server == nullptr)
            return true;
        if (!ConnectToServer(Server))
            return true;
        BeginLeave();
        LeaveDirection = 1;
        return true;
    }

    if (Msg == kWinMsgEscape)
    {
        // Back to the main menu at once.
        Result = kMenuStateMainMenu;
        MouseSetCursor("Arrow", true);
        return true;
    }

    if (Msg == kWinMsgScroll)
    {
        // The scroll bar moved.
        RebuildTable();
        return true;
    }
    return false;
}

FServerInfo const* PageServerSelect::GetSelectedServer()
{
    // Inlined twice in retail's ProcessMessage. A selected row without a payload gives nothing.
    unsigned SelectedRow = 0;
    if (ServerTable->GetSelectedRow(&SelectedRow))
    {
        ServerRowData* const Data =
            static_cast<ServerRowData*>(ServerTable->GetRow(SelectedRow)->Payload.Object);
        if (Data == nullptr)
            return nullptr;
        return &Data->Info;
    }
    if (SelectedServerId == 0xffffffffu)
        return nullptr;
    return G_LoginMgr.FindServer(SelectedServerId);
}

void PageServerSelect::ClearRow(unsigned Row)
{
    // FUN_00571b50. The column count is read again each pass.
    for (unsigned Column = 0; Column < ServerTable->ColEdges.size(); ++Column)
        ServerTable->ClearCell(Row, Column);
    FUiFastTableRow* const RowRecord = ServerTable->GetRow(Row);
    if (RowRecord != nullptr && RowRecord->Payload.Object != nullptr)
        RowRecord->Payload.Reset();
}

void PageServerSelect::ShowServerDetails(FServerInfo const* Server, bool bNewServer)
{
    // FUN_00571bd0.
    ServerDetails->ClearContent();
    if (bNewServer)
    {
        // Back to the top, the horizontal scroll kept.
        int ScrollX = 0;
        int ScrollY = 0;
        ServerDetails->GetScrollOrigin(&ScrollX, &ScrollY);
        ServerDetails->SetScrollOrigin(ScrollX, 0);
    }

    {
        StyleNode const Style(kTextStyleList);
        ServerDetails->AppendNode(Style);
    }
    {
        RDLRefNode const Ref(DetailsHeader);
        ServerDetails->AppendNode(Ref);
    }
    {
        ParagraphNode const Paragraph;
        ServerDetails->AppendNode(Paragraph);
    }
    {
        StyleNode const Style(0x76ec);
        ServerDetails->AppendNode(Style);
    }
    RenderDisplayList Description;
    AppendTextAsRdl(Server->Description.c_str(), static_cast<unsigned>(Server->Description.size()),
                    Description, false);
    ServerDetails->AppendContent(Description);
    {
        ParagraphNode const Paragraph;
        ServerDetails->AppendNode(Paragraph);
    }

    // The restrictions.
    if (Server->bAllowNewPlayers != 0 && Server->bPassword == 0)
        return;
    {
        StyleNode const Style(kTextStyleList);
        ServerDetails->AppendNode(Style);
    }
    {
        RDLRefNode const Ref(DetailsRestricted);
        ServerDetails->AppendNode(Ref);
    }
    {
        ParagraphNode const Paragraph;
        ServerDetails->AppendNode(Paragraph);
    }
    {
        StyleNode const Style(0x76ec);
        ServerDetails->AppendNode(Style);
    }
    if (Server->bAllowNewPlayers == 0)
    {
        {
            RDLRefNode const Ref(DetailsNoNewPlayers);
            ServerDetails->AppendNode(Ref);
        }
        ParagraphNode const Paragraph;
        ServerDetails->AppendNode(Paragraph);
    }
    if (Server->bPassword != 0)
    {
        {
            RDLRefNode const Ref(DetailsPassword);
            ServerDetails->AppendNode(Ref);
        }
        ParagraphNode const Paragraph;
        ServerDetails->AppendNode(Paragraph);
    }
}

void PageServerSelect::SetToolTipsEnabled(bool bEnabled)
{
    // FUN_00571f50: the Legend's tip is always switched off.
    ServerTable->SetToolTipEnabled(bEnabled);
    Connect->SetToolTipEnabled(bEnabled);
    MainMenu->SetToolTipEnabled(bEnabled);
    SetFilter->SetToolTipEnabled(bEnabled);
    ServerDetails->SetToolTipEnabled(bEnabled);
    Legend->SetToolTipEnabled(false);
    Refresh->SetToolTipEnabled(bEnabled);
    ListServerStatus->SetToolTipEnabled(bEnabled);
}

void PageServerSelect::BeginLeave()
{
    // FUN_00571fc0.
    bLeaving = 1;
    SlideT = 0.0;
    HideWin3D(ServerTable);
    HideWin3D(ServerScroll);
    HideWin3D(Legend);
    HideWin3D(ServerDetails);
    HideWin3D(Title);
    HideWin3D(SubTitle);
    HideWin3D(ListServerStatus);
    HideWin3D(Version);
    Connect->bInteractive = 0;
    MainMenu->bInteractive = 0;
    SetFilter->bInteractive = 0;
    Refresh->bInteractive = 0;
    Connect->SetSkipUpload(true);
    MainMenu->SetSkipUpload(true);
    SetFilter->SetSkipUpload(true);
    Refresh->SetSkipUpload(true);
    PlayUiSoundSlot(0x1b);
    SetToolTipsEnabled(false); // inlined in retail
}

bool PageServerSelect::ConnectToServer(FServerInfo const* Server)
{
    // FUN_00572100.
    if (!G_LoginMgr.SetCurrentServer(*Server))
    {
        FDUMP(kFdumpError, "%s(%d) : ERROR:General:%s", kIntroCpp, 0x14cb,
              "Internal error: invalid server data.\n");
        return false;
    }

    // A locked server asks for a password (OK tool tip 0xd48); cancelling ends the connect.
    std::wstring Password(L"");
    if (Server->bPassword != 0 && !NnPromptRunTextEntry(0x60071, &Password, false, 0xd48))
    {
        FDUMP(kFdumpNotice, "%s(%d) : NOTICE:General:%s", kIntroCpp, 0x14b4,
              "Password cancelled.\n");
        return false;
    }

    // A failed connect recreates the Gun.
    G_LoginMgr.ServerPassword = Password;
    if (::ConnectToServer(g_display.ini_path.c_str()))
        return true;
    G_LoginMgr.CreateGun();
    FDUMP(kFdumpNotice, "%s(%d) : NOTICE:General:%s", kIntroCpp, 0x14c5,
          "Failed to connect to the server.\n");
    return false;
}

void PageServerSelect::Update()
{
    // FUN_0056f6e0. The list is asked for until CLoginMgr takes the request.
    if (bListRequested == 0)
        bListRequested = G_LoginMgr.RequestServerList() ? 1 : 0;

    // Once in (past 1.0 or unordered) or leaving, and not while a leave with a direction runs:
    // the list pumped, the status line kept current, Connect live only with a server.
    if ((!(SlideT <= 1.0) || bLeaving != 0) && (bLeaving == 0 || LeaveDirection == 0))
    {
        if (G_LoginMgr.PumpServerList())
            RebuildTable();
        int const Status = G_LoginMgr.GetGunStatus();
        if (Status != ListStatus)
        {
            ListStatus = Status;
            RefreshListStatus();
        }
        unsigned SelectedRow = 0;
        if (!ServerTable->GetSelectedRow(&SelectedRow) && SelectedServerId == 0xffffffffu)
        {
            Connect->Flags = static_cast<unsigned char>(Connect->Flags & 0xfe);
            Connect->bInteractive = 0;
        }
        else
        {
            Connect->Flags = static_cast<unsigned char>(Connect->Flags | 1);
            Connect->bInteractive = 1;
        }
    }

    // The slide in, unless past 1.0 (or unordered) or leaving.
    if (SlideT <= 1.0 && bLeaving == 0)
    {
        // Frame time capped at 0.5, 1.5 per second.
        double Dt = GetClientFrameTimeCapped();
        if (Dt > 0.5)
            Dt = 0.5;
        SlideT = Dt * 1.5 + SlideT;

        // In place once past 0; an unordered SlideT also arrives.
        if (SlideT <= 0.0)
        {
            float Right[3] = {static_cast<float>(SlideT), 0.0f, 0.0f};
            float Left[3] = {static_cast<float>(-SlideT), 0.0f, 0.0f};
            MainMenu->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(Right), 0);
            SetFilter->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(Right), 0);
            Connect->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(Left), 0);
            Refresh->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(Left), 0);
            float BoxPosition[3];
            Win3DVec3Add(BoxPosition, InfoBoxHome, Left);
            ServerInfoBox->SetPosition(BoxPosition);
            Win3DVec3Add(BoxPosition, SelectBoxHome, Right);
            ServerSelectBox->SetPosition(BoxPosition);
        }
        else
        {
            // Everything in place, shown and live; SlideT parked at 2.0.
            float ZeroOffset[3] = {0.0f, 0.0f, 0.0f};
            MainMenu->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(ZeroOffset), 0);
            SetFilter->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(ZeroOffset), 0);
            Connect->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(ZeroOffset), 0);
            Refresh->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(ZeroOffset), 0);
            ServerInfoBox->SetPosition(InfoBoxHome);
            ServerSelectBox->SetPosition(SelectBoxHome);
            ShowWin3D(ServerTable);
            ShowWin3D(Legend);
            ShowWin3D(ServerDetails);
            ShowWin3D(Title);
            ShowWin3D(SubTitle);
            ShowWin3D(ListServerStatus);
            ShowWin3D(Version);
            Connect->SetSkipUpload(false);
            MainMenu->SetSkipUpload(false);
            SetFilter->SetSkipUpload(false);
            Refresh->SetSkipUpload(false);
            ServerTable->SetToolTipEnabled(true);
            Connect->SetToolTipEnabled(true);
            MainMenu->SetToolTipEnabled(true);
            SetFilter->SetToolTipEnabled(true);
            ServerDetails->SetToolTipEnabled(true);
            Legend->SetToolTipEnabled(false);
            Refresh->SetToolTipEnabled(true);
            ListServerStatus->SetToolTipEnabled(true);
            SlideT = 2.0;
        }

        if (SlideT > -0.8 && SlideT < -0.1)
            PlayUiSoundSlot(0x1b);
    }

    // Leaving with a direction: 1.5 per second, no frame-time cap.
    if (bLeaving != 0 && LeaveDirection != 0)
    {
        SlideT = SlideT - GetClientFrameTimeCapped() * 1.5;
        float Right[3] = {static_cast<float>(SlideT), 0.0f, 0.0f};
        float Left[3] = {static_cast<float>(-SlideT), 0.0f, 0.0f};
        MainMenu->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(Right), 0);
        SetFilter->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(Right), 0);
        Connect->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(Left), 0);
        Refresh->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(Left), 0);
        float BoxPosition[3];
        Win3DVec3Add(BoxPosition, InfoBoxHome, Left);
        ServerInfoBox->SetPosition(BoxPosition);
        Win3DVec3Add(BoxPosition, SelectBoxHome, Right);
        ServerSelectBox->SetPosition(BoxPosition);

        // -1.5 is DAT_005e2bb0.
        if (SlideT <= -1.5)
        {
            HideWin3D(MainMenu);
            HideWin3D(SetFilter);
            HideWin3D(Connect);
            HideWin3D(Refresh);
            HideWin3D(ServerInfoBox);
            HideWin3D(ServerSelectBox);
            HideWin3D(Version);
            if (LeaveDirection == 1)
            {
                Result = kMenuStateLogin;
                LoadingScreenStart();
            }
            else if (LeaveDirection == -1)
            {
                Result = kMenuStateMainMenu;
            }
            bLeaving = 0;
        }
    }

    Win3D::Update();
}

bool PageServerSelect::OnEvent(unsigned MsgId)
{
    // LAB_00572330 (this = the +0x80 face): event 0xf goes back to the main menu unless a leave
    // runs.
    if (MsgId != USER_CANCEL)
        return UIPage::MasterWin::OnEvent(MsgId);
    if (bLeaving == 0)
    {
        Result = kMenuStateMainMenu;
        MouseSetCursor("Arrow", true);
    }
    return true;
}

PageServerSelect& GetPageServerSelect()
{
    return G_PageServerSelect;
}
