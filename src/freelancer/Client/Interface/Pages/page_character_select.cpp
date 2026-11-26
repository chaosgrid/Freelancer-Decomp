// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/Pages/page_character_select.h"

#include "NeuroNet/Chat/chat_init.h"         // ChatAppendRdlCopy, ChatGetServerMotd*, ChatGetShip/SystemName
#include "Network/connect_flow.h"      // ConnectToServer (FUN_005a8770)
#include "Common/engine_light.h"      // ScaleVec3 (FUN_00411d60)
#include "Core/globals.h"           // g_display (DAT_00679d04), GetClientFrameTimeCapped
#include "Common/Fonts/ids_rdl.h"           // AppendTextAsRdl, LoadIdsIntoRdl, RdlClear, RdlRemoveLastParagraph
#include "Interface/Pages/intro_page_util.h"
#include "Interface/loading_screen.h"    // LoadingScreenSetClear / LoadingScreenStart (FUN_00579280 / 290)
#include "Network/login_mgr.h"         // G_LoginMgr (DAT_0067e7b8)
#include "Interface/menu_ui_init.h"      // PlayUiSoundSlot (FUN_00564570), PlayOrRewindUiSoundSlot (5645c0)
#include "NeuroNet/nn_confirm.h"        // NnConfirmOpenFormatted (FUN_00488a80)
#include "NeuroNet/nn_error.h"          // NnErrorRunModal (FUN_00473cb0)
#include "Core/reg_key_stack.h"     // FRegKeyStack
#include "Network/server_connect.h"    // ShutdownServerConnection (FUN_005a8260)
#include "Interface/tooltip_hover.h"     // FToolTipHoverInfo
#include "Interface/ui_fast_table.h"
#include "Interface/ui_rich_text_win.h"
#include "Interface/ui_scroll.h"
#include "Interface/ui_text_msg_button.h"
#include "Interface/win3d_immediate.h"   // Win3DVec3Add (FUN_00423b30)
#include "Interface/menu_enter_state.h"

#include "common_text.h"

#include <stdint.h>            // uintptr_t
#include <stdlib.h>            // qsort
#include <string.h>            // memset
#include <wchar.h>             // _wcsicoll
#include <sstream>
#include <string>
#include <vector>

using namespace IntroPageUtil;

// DAT_0067bd40.
CHARACTER_ID G_NoCharacterId;

// DAT_0067bcf8.
FCharacterCreateSelection G_CharacterCreateSelection;
// Retail clears all 0x11 dwords, the faction index included.
static_assert(sizeof(FCharacterCreateSelection) == 0x44, "DAT_0067bcf8 is 0x11 dwords");

namespace
{
    // DAT_0067b180.
    PageCharacterSelect G_PageCharacterSelect;

    char const kCharacterTableKey[] = "CharacterTable"; // s_ 0x005e29a4 (also the widget name)

    // .rdata:005e25c8 -- the column captions.
    unsigned const kColumnHeaderIds[5] = {0xaf1, 0x6002f, 0xaf2, 0x6004a, 0x60049};
    // .rdata:005e25dc -- the header cells' tool tips.
    unsigned const kColumnHeaderTipIds[5] = {0xad2, 0x1b64, 0xad3, 0x1b65, 0x1b66};
    // .rdata:005e25f0 -- the column edges Create writes when the registry has none.
    float const kDefaultColumnEdges[5] = {0.0f, 0.228489995f, 0.316978991f, 0.413801998f,
                                          0.640625f};

    // -----------------------------------------------------------------------------------------
    // The ten qsort comparators (cdecl, on CharacterInfo* elements). Columns: 0 name, 1 rank
    // (+0x270), 2 money (+0x234), 3 ship (+0x230), 4 system (+0x260).

    CCharacterSet::CharacterInfo const* ElementOf(void const* Element)
    {
        return *static_cast<CCharacterSet::CharacterInfo* const*>(Element);
    }

    // FUN_00568f70.
    int __cdecl CompareNameUp(void const* A, void const* B)
    {
        return _wcsicoll(ElementOf(A)->Name.c_str(), ElementOf(B)->Name.c_str());
    }

    // FUN_00568fd0.
    int __cdecl CompareRankUp(void const* A, void const* B)
    {
        return static_cast<int>(ElementOf(A)->Rank - ElementOf(B)->Rank);
    }

    // FUN_00568fb0.
    int __cdecl CompareMoneyUp(void const* A, void const* B)
    {
        return ElementOf(A)->Money - ElementOf(B)->Money;
    }

    // FUN_00568ff0. Retail looks up B's name before A's.
    int __cdecl CompareShipUp(void const* A, void const* B)
    {
        RenderDisplayList const* const NameB = ChatGetShipArchName(ElementOf(B)->ShipArchetype);
        RenderDisplayList const* const NameA = ChatGetShipArchName(ElementOf(A)->ShipArchetype);
        return lex_compare(*NameA, *NameB, false);
    }

    // FUN_00569030.
    int __cdecl CompareSystemUp(void const* A, void const* B)
    {
        RenderDisplayList const* const NameB = ChatGetSystemName(ElementOf(B)->SystemId);
        RenderDisplayList const* const NameA = ChatGetSystemName(ElementOf(A)->SystemId);
        return lex_compare(*NameA, *NameB, false);
    }

    // FUN_00569070.
    int __cdecl CompareNameDown(void const* A, void const* B)
    {
        return -_wcsicoll(ElementOf(A)->Name.c_str(), ElementOf(B)->Name.c_str());
    }

    // FUN_005690b0.
    int __cdecl CompareRankDown(void const* A, void const* B)
    {
        return static_cast<int>(ElementOf(B)->Rank - ElementOf(A)->Rank);
    }

    // FUN_005690d0.
    int __cdecl CompareMoneyDown(void const* A, void const* B)
    {
        return ElementOf(B)->Money - ElementOf(A)->Money;
    }

    // FUN_005690f0.
    int __cdecl CompareShipDown(void const* A, void const* B)
    {
        RenderDisplayList const* const NameB = ChatGetShipArchName(ElementOf(B)->ShipArchetype);
        RenderDisplayList const* const NameA = ChatGetShipArchName(ElementOf(A)->ShipArchetype);
        return -lex_compare(*NameA, *NameB, false);
    }

    // FUN_00569130.
    int __cdecl CompareSystemDown(void const* A, void const* B)
    {
        RenderDisplayList const* const NameB = ChatGetSystemName(ElementOf(B)->SystemId);
        RenderDisplayList const* const NameA = ChatGetSystemName(ElementOf(A)->SystemId);
        return -lex_compare(*NameA, *NameB, false);
    }

    typedef int(__cdecl* FCharacterCompare)(void const*, void const*);

    // .data:00614b98 -- used while bAscending[SortColumn] is set.
    FCharacterCompare const kAscendingCompare[5] = {CompareNameUp, CompareRankUp, CompareMoneyUp,
                                                    CompareShipUp, CompareSystemUp};
    // .data:00614b84 -- used while it is clear.
    FCharacterCompare const kDescendingCompare[5] = {CompareNameDown, CompareRankDown,
                                                     CompareMoneyDown, CompareShipDown,
                                                     CompareSystemDown};

    void AppendStyle(UiRichTextWin* Win, unsigned short Style)
    {
        StyleNode const Node(Style);
        Win->AppendNode(Node);
    }

    void AppendJustify(UiRichTextWin* Win, TextJustify Justify)
    {
        JustifyNode const Node(Justify);
        Win->AppendNode(Node);
    }

    void AppendRef(UiRichTextWin* Win, RenderDisplayList& Rdl)
    {
        RDLRefNode const Node(Rdl);
        Win->AppendNode(Node);
    }

    void AppendParagraph(UiRichTextWin* Win)
    {
        ParagraphNode const Node;
        Win->AppendNode(Node);
    }

    void SetButtonLive(UiTextMsgButton* Button, bool bLive)
    {
        if (bLive)
        {
            Button->Flags = static_cast<unsigned char>(Button->Flags | kWin3DActive);
            Button->bInteractive = 1;
        }
        else
        {
            Button->Flags = static_cast<unsigned char>(Button->Flags & ~kWin3DActive);
            Button->bInteractive = 0;
        }
    }
}

// ---------------------------------------------------------------------------------------------
// CharacterRowData

PageCharacterSelect::CharacterRowData::CharacterRowData(CHARACTER_ID const& Source)
{
    Id = Source;
}

PageCharacterSelect::CharacterRowData::~CharacterRowData()
{
    // FUN_0056ae70.
}

// ---------------------------------------------------------------------------------------------

PageCharacterSelect::PageCharacterSelect()
    : UIPage::MasterWin(Desc.GetPageData())
    , Result(0)
    , CharacterTable(nullptr)
    , CharacterScroll(nullptr)
    , LoadCharacter(nullptr)
    , NewCharacter(nullptr)
    , DeleteCharacter(nullptr)
    , Back(nullptr)
    , MainMenu(nullptr)
    , CharacterBox(nullptr)
    , MotdBox(nullptr)
    , ServerDetailsBox(nullptr)
    , Title(nullptr)
    , ServerDetails(nullptr)
    , Motd(nullptr)
    , MotdVersion(0)
    , Field_0x380(PackTextAttributes(0, 0, 0, 0, 0x52eaf5))
    , UpPip(nullptr)
    , DownPip(nullptr)
    , SortColumn(0)
    , bAscending{0, 0, 0, 0, 0}
    , bCharacterChosen(0)
    , FirstVisible(0)
    , CharacterBoxHome{0.0f, 0.0f, 0.0f}
    , MotdBoxHome{0.0f, 0.0f, 0.0f}
    , DetailsBoxHome{0.0f, 0.0f, 0.0f}
    , SlideT(0.0)
    , bLeaving(0)
    , LeaveDirection(kCharSelectLeaveNone)
    , bLoadRequested(0)
    , bIntroBaseLeft(0)
{
    // FUN_00567020. Retail leaves the images, the homes and +0x5e8..+0x5f4 unwritten; the static
    // object starts zeroed, which is what the initialisers above reproduce.
    CurrentCharacter = G_NoCharacterId;
}

PageCharacterSelect::~PageCharacterSelect()
{
    // FUN_0056aeb0.
}

bool PageCharacterSelect::SaveEdges()
{
    // FUN_00567350: keys created with KEY_ALL_ACCESS (0x2001f); every key left on the stack is
    // closed at the end, HKCU included, as retail.
    bool bSaved = false;
    FRegKeyStack Keys;
    if (Keys.Create(G_FreelancerRegKey, 0x2001f) == nullptr)
        return false;
    if (Keys.Create(kCharacterTableKey, 0x2001f) == nullptr)
        return false;
    if (Edges.SaveToRegistry(&Keys))
        bSaved = true;
    Keys.PopClose();
    return bSaved;
}

void PageCharacterSelect::Destroy()
{
    // FUN_00567290.
    UnregisterEvent(USER_CANCEL);
    if (CharacterTable != nullptr)
    {
        CharacterTable->GetEdges(&Edges);
        SaveEdges();
        CharacterTable->GetDeviceTRC()->destroy_image(UpPip);
        CharacterTable->GetDeviceTRC()->destroy_image(DownPip);
    }
    UIPage::MasterWin::Destroy();

    CharacterTable = nullptr;
    CharacterScroll = nullptr;
    LoadCharacter = nullptr;
    NewCharacter = nullptr;
    DeleteCharacter = nullptr;
    Back = nullptr;
    MainMenu = nullptr;
    CharacterBox = nullptr;
    MotdBox = nullptr;
    ServerDetailsBox = nullptr;
    Title = nullptr;
    ServerDetails = nullptr;
    Motd = nullptr;
}

bool PageCharacterSelect::Create(Win3D* NewParent, void* /*Payload*/)
{
    // LAB_005674a0. MasterWin::Create with no payload; its result is not tested.
    UIPage::MasterWin::Create(NewParent, nullptr);
    Result = 0;

    CharacterTable = static_cast<UiFastTable*>(FindWidget(kCharacterTableKey));
    CharacterScroll = static_cast<UiScroll*>(FindWidget("CharacterScroll"));
    LoadCharacter = static_cast<UiTextMsgButton*>(FindWidget("LoadCharacter"));
    NewCharacter = static_cast<UiTextMsgButton*>(FindWidget("NewCharacter"));
    DeleteCharacter = static_cast<UiTextMsgButton*>(FindWidget("DeleteCharacter"));
    Back = static_cast<UiTextMsgButton*>(FindWidget("Back"));
    MainMenu = static_cast<UiTextMsgButton*>(FindWidget("MainMenu"));
    CharacterBox = FindWidget("CharacterBox");
    MotdBox = FindWidget("MOTDBox");
    ServerDetailsBox = FindWidget("ServerDetailsBox");
    Title = static_cast<UiRichTextWin*>(FindWidget("Title"));
    ServerDetails = static_cast<UiRichTextWin*>(FindWidget("ServerDetails"));
    Motd = static_cast<UiRichTextWin*>(FindWidget("MOTD"));
    bCharacterChosen = 0;

    RegisterEvent(USER_CANCEL);

    // Tip mode 3: one tip per cell.
    CharacterTable->SetTipMode(kTableTipCell);
    CharacterTable->SetToolTipEnabled(true);
    UpPip = CharacterTable->GetDeviceTRC()->create_image("interface\\intro\\images\\uppip.bmp");
    DownPip =
        CharacterTable->GetDeviceTRC()->create_image("interface\\intro\\images\\DownPip.bmp");

    // The trailing paragraph is cut without testing the load.
    for (unsigned Column = 0; Column < 5; ++Column)
    {
        RdlClear(ColumnHeaders[Column]);
        LoadIdsIntoRdl(kColumnHeaderIds[Column], ColumnHeaders[Column]);
        RdlRemoveLastParagraph(ColumnHeaders[Column]);
        bAscending[Column] = 1;
        FToolTipHoverInfo* const Tip = CharacterTable->GetCellTip(0, Column);
        if (Tip != nullptr)
            Tip->IdsAlt = kColumnHeaderTipIds[Column];
    }

    // In retail's order.
    LoadIds(ServerNameText, 0x729);
    LoadIds(DetailsHeader, 0x744);
    LoadIds(DetailsPassword, 0x750);
    LoadIds(DetailsNoNewPlayers, 0x74f);
    LoadIdsCut(TitleText, 0x6004e);
    LoadIdsCut(MotdHeader, 0x60007);
    LoadIdsCut(DetailsRestricted, 0x6007c);

    CharacterTable->SetFrameColor(ChromeColor());
    CharacterTable->SetGrabDistance(0.00400000019f);
    CharacterTable->SetResizable(false, true);
    CharacterTable->SetLayoutFlag(true);
    CharacterTable->SetHeaderRows(1);
    CharacterTable->SetBorders(false, false, false, true);
    CharacterTable->SetRowHighlights(false);

    // Range and page convert the row count as an unsigned dword.
    CharacterScroll->SetArrowColors(ChromeColor(), ChromeColor());
    CharacterScroll->SetBarColors(ChromeColor(), ChromeColor());
    CharacterScroll->SetRange(
        0.0f,
        static_cast<float>(static_cast<unsigned>(EdgeCount(CharacterTable->RowEdges) - 1)));
    CharacterScroll->SetPage(
        static_cast<float>(static_cast<unsigned>(EdgeCount(CharacterTable->RowEdges) - 1)));
    CharacterScroll->SetStep(1.0f);
    CharacterScroll->SetValue(0.0f);
    ServerDetails->SetAutoScrollbar(true);
    Motd->SetAutoScrollbar(true);

    // Retail inlines RefreshMotd's body here.
    MotdVersion = 0;
    RefreshMotd();

    // The saved edges from HKCU\...\CharacterTable (KEY_READ).
    bool bLoaded = false;
    {
        FRegKeyStack Keys;
        if (Keys.Open(G_FreelancerRegKey, 0x20019) != nullptr &&
            Keys.Open(kCharacterTableKey, 0x20019) != nullptr)
        {
            if (Edges.LoadFromRegistry(&Keys))
                bLoaded = true;
            Keys.PopClose();
        }
        Keys.PopClose();
    }
    // Nothing saved: the table's own edges with the default column edges written over the first
    // five (no size check, as retail).
    if (!bLoaded)
    {
        CharacterTable->GetEdges(&Edges);
        float* const Cols = Edges.Cols.data();
        for (int Index = 0; Index < 5; ++Index)
            Cols[Index] = kDefaultColumnEdges[Index];
    }
    CharacterTable->SetEdges(&Edges);

    Title->ClearContent();
    AppendStyle(Title, kTextStyleScreenTitle);
    AppendRef(Title, TitleText);

    SelectedCharacter = G_NoCharacterId;

    // With five column edges the header row gets its captions and stops hovering.
    if (CharacterTable->ColEdges.size() == 5)
    {
        for (unsigned Column = 0; Column < 5; ++Column)
            SetCell(0, Column, &ColumnHeaders[Column], false, false);
        CharacterTable->GetRow(0)->bHoverable = 0;
    }

    // The server's details, from a copy of the current server (FUN_005684a0) that lives to the
    // end of the block.
    {
        FServerInfo const Server = G_LoginMgr.GetCurrentServer();
        ServerDetails->ClearContent();
        AppendJustify(ServerDetails, TextJustifyCenter);
        AppendStyle(ServerDetails, kTextStyleList);
        AppendRef(ServerDetails, ServerNameText);
        AppendParagraph(ServerDetails);
        AppendJustify(ServerDetails, TextJustifyLeft);
        AppendStyle(ServerDetails, 0x76ec);
        {
            TextNode const Name(Server.Name.c_str(), -1);
            ServerDetails->AppendNode(Name);
        }
        AppendParagraph(ServerDetails);
        AppendJustify(ServerDetails, TextJustifyCenter);
        AppendStyle(ServerDetails, kTextStyleList);
        AppendRef(ServerDetails, DetailsHeader);
        AppendParagraph(ServerDetails);
        AppendJustify(ServerDetails, TextJustifyLeft);
        AppendStyle(ServerDetails, 0x76ec);
        RenderDisplayList Description;
        AppendTextAsRdl(Server.Description.c_str(), static_cast<unsigned>(Server.Description.size()),
                        Description, false);
        ServerDetails->AppendContent(Description);
        AppendParagraph(ServerDetails);

        // The restrictions. Here the style comes before the left justification, as retail.
        if (Server.bAllowNewPlayers == 0 || Server.bPassword != 0)
        {
            AppendJustify(ServerDetails, TextJustifyCenter);
            AppendStyle(ServerDetails, kTextStyleList);
            AppendRef(ServerDetails, DetailsRestricted);
            AppendParagraph(ServerDetails);
            AppendStyle(ServerDetails, 0x76ec);
            AppendJustify(ServerDetails, TextJustifyLeft);
            if (Server.bAllowNewPlayers == 0)
            {
                AppendRef(ServerDetails, DetailsNoNewPlayers);
                AppendParagraph(ServerDetails);
            }
            if (Server.bPassword != 0)
            {
                AppendRef(ServerDetails, DetailsPassword);
                AppendParagraph(ServerDetails);
            }
        }
    }

    G_LoginMgr.ReloadFactions();
    if (G_LoginMgr.RequestCharacterInfo() == 1)
        RebuildTable();

    // Retail inlines UpdateButtons' body here.
    NewCharacter->Flags =
        static_cast<unsigned char>(NewCharacter->Flags | kWin3DActive | kWin3DVisible);
    LoadCharacter->Flags =
        static_cast<unsigned char>(LoadCharacter->Flags | kWin3DActive | kWin3DVisible);
    DeleteCharacter->Flags =
        static_cast<unsigned char>(DeleteCharacter->Flags | kWin3DActive | kWin3DVisible);
    UpdateButtons();

    SlideT = -1.0;
    bLeaving = 0;
    LeaveDirection = kCharSelectLeaveMainMenu;

    LoadCharacter->SetSkipUpload(true);
    NewCharacter->SetSkipUpload(true);
    DeleteCharacter->SetSkipUpload(true);
    Back->SetSkipUpload(true);
    MainMenu->SetSkipUpload(true);
    HideWin3D(ServerDetails);
    HideWin3D(Motd);
    HideWin3D(Title);
    HideWin3D(CharacterTable);
    HideWin3D(CharacterScroll);

    for (int Axis = 0; Axis < 3; ++Axis)
    {
        (&CharacterBoxHome.x)[Axis] = (&CharacterBox->Position.x)[Axis];
        (&MotdBoxHome.x)[Axis] = (&MotdBox->Position.x)[Axis];
        (&DetailsBoxHome.x)[Axis] = (&ServerDetailsBox->Position.x)[Axis];
    }
    return true;
}

void PageCharacterSelect::SetCell(unsigned Row, unsigned Column, RenderDisplayList const* Content,
                                  bool bEnabled, bool bSelected)
{
    // FUN_00568880.
    FUiFastTableCell* Cell = nullptr;
    if (!CharacterTable->GetCell(Row, Column, &Cell))
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
            !bEnabled ? kTextStyleDisabled
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
    CharacterTable->InvalidateCell(Row, Column);
}

void PageCharacterSelect::RebuildTable()
{
    // FUN_00568d10.
    CharacterTable->ClearSelection();

    // The scroll bar is shown only when the roster does not fit.
    int const CharacterCount = static_cast<int>(G_LoginMgr.Characters.count());
    unsigned const RowCount = static_cast<unsigned>(EdgeCount(CharacterTable->RowEdges));
    CharacterScroll->SetRange(0.0f, static_cast<float>(CharacterCount));
    int const VisibleRows = static_cast<int>(RowCount) - 1;
    CharacterScroll->SetPage(static_cast<float>(VisibleRows));
    CharacterScroll->SetStep(1.0f);
    FirstVisible = static_cast<int>(CharacterScroll->GetValue());
    if (CharacterCount > VisibleRows)
        ShowWin3D(CharacterScroll);
    else
        HideWin3D(CharacterScroll);

    // Retail uses an _alloca'd array; its operator== against +0x399 after each fetch has no
    // effect and is not ported.
    std::vector<CCharacterSet::CharacterInfo*> Characters(static_cast<size_t>(CharacterCount));
    for (int Index = 0; Index < CharacterCount; ++Index)
        Characters[static_cast<size_t>(Index)] = G_LoginMgr.CharacterAt(Index);

    if (CharacterCount != 0)
    {
        FCharacterCompare const Compare = bAscending[SortColumn] != 0
                                              ? kAscendingCompare[SortColumn]
                                              : kDescendingCompare[SortColumn];
        qsort(Characters.data(), static_cast<size_t>(CharacterCount),
              sizeof(CCharacterSet::CharacterInfo*), Compare);
    }
    else
    {
        SelectedCharacter = G_NoCharacterId;
    }

    unsigned Row = 1;
    for (int Index = FirstVisible; Index < CharacterCount; ++Index, ++Row)
    {
        if (Row >= RowCount)
            return;
        CCharacterSet::CharacterInfo* const Character = Characters[static_cast<size_t>(Index)];
        if (Character != nullptr)
        {
            FillRow(Row, Character);
            if (Character->Id == SelectedCharacter)
                CharacterTable->SelectRow(Row);
        }
    }

    // The rows left over are emptied (retail inlines ClearRow).
    for (; Row < RowCount; ++Row)
    {
        for (unsigned Column = 0; Column < CharacterTable->ColEdges.size(); ++Column)
            CharacterTable->ClearCell(Row, Column);
        FUiFastTableRow* const RowRecord = CharacterTable->GetRow(Row);
        if (RowRecord != nullptr && RowRecord->Payload.Object != nullptr)
            RowRecord->Payload.Reset();
        CharacterTable->GetRow(Row)->bSelectable = 0;
        CharacterTable->GetRow(Row)->bHoverable = 0;
    }
}

void PageCharacterSelect::FillRow(unsigned Row, CCharacterSet::CharacterInfo const* Character)
{
    // FUN_00569170. Every cell enabled. Retail fills the columns in the order 0, 2, 1, 3, 4.
    bool const bSelected = Character->Id == SelectedCharacter;

    {
        RenderDisplayList Text;
        TextNode const Node(Character->Name.c_str(), -1);
        AppendNodeClone(Text, Node);
        SetCell(Row, 0, &Text, true, bSelected);
    }

    {
        std::wstringstream Stream;
        Stream << Character->Money << std::ends;
        std::wstring const Digits = Stream.str();
        RenderDisplayList Text;
        TextNode const Node(Digits.c_str(), -1);
        AppendNodeClone(Text, Node);
        SetCell(Row, 2, &Text, true, bSelected);
    }

    {
        std::wstringstream Stream;
        Stream << Character->Rank << std::ends;
        std::wstring const Digits = Stream.str();
        RenderDisplayList Text;
        TextNode const Node(Digits.c_str(), -1);
        AppendNodeClone(Text, Node);
        SetCell(Row, 1, &Text, true, bSelected);
    }

    SetCell(Row, 3, ChatGetShipArchName(Character->ShipArchetype), true, bSelected);
    SetCell(Row, 4, ChatGetSystemName(Character->SystemId), true, bSelected);

    // Assign takes a reference; the construction reference is dropped.
    FUiFastTableRow* const RowRecord = CharacterTable->GetRow(Row);
    RowRecord->bSelectable = 1;
    RowRecord->bHoverable = 1;
    CharacterRowData* const Data = new CharacterRowData(Character->Id);
    RowRecord->Payload.Assign(Data);
    Data->Release();
}

bool PageCharacterSelect::ProcessMessage(unsigned Msg, unsigned A, unsigned B)
{
    // FUN_00569670.
    if (Msg == kWinMsgCommand)
    {
        PlayOrRewindUiSoundSlot(kUiSoundSelectItem);
        int Direction = kCharSelectLeaveNone;
        switch (A)
        {
        case kCharSelectCmdMainMenu:
            Direction = kCharSelectLeaveMainMenu;
            break;
        case kCharSelectCmdBack:
            Direction = kCharSelectLeaveBack;
            break;
        case kCharSelectCmdNew:
            Direction = kCharSelectLeaveNew;
            break;
        case kCharSelectCmdLoad:
            Direction = kCharSelectLeaveLoad;
            break;
        case kCharSelectCmdDelete:
        {
            // Delete: the confirm box with the selected character's name.
            unsigned SelectedRow = A;
            if (CharacterTable->GetSelectedRow(&SelectedRow))
            {
                FUiFastTableRow* const RowRecord = CharacterTable->GetRow(SelectedRow);
                if (RowRecord->Payload.Object != nullptr)
                {
                    CharacterRowData const* const Data =
                        static_cast<CharacterRowData const*>(RowRecord->Payload.Object);
                    CCharacterSet::CharacterInfo const* const Character =
                        G_LoginMgr.FindCharacterById(Data->Id);
                    NnConfirmOpenFormatted(this, Character->Name.c_str(), nullptr);
                }
            }
            return true;
        }
        default:
            return false;
        }

        // The leave sequence retail inlines in each of the four cases: BeginLeave's body with
        // DisableToolTips for the tips, then the direction.
        bLeaving = 1;
        SlideT = 0.0;
        LoadCharacter->bInteractive = 0;
        NewCharacter->bInteractive = 0;
        DeleteCharacter->bInteractive = 0;
        Back->bInteractive = 0;
        MainMenu->bInteractive = 0;
        LoadCharacter->SetSkipUpload(true);
        NewCharacter->SetSkipUpload(true);
        DeleteCharacter->SetSkipUpload(true);
        Back->SetSkipUpload(true);
        MainMenu->SetSkipUpload(true);
        HideWin3D(ServerDetails);
        HideWin3D(Motd);
        HideWin3D(Title);
        HideWin3D(CharacterTable);
        HideWin3D(CharacterScroll);
        PlayUiSoundSlot(kUiSoundMotionSwish);
        DisableToolTips();
        LeaveDirection = Direction;
        return true;
    }

    if (Msg == kWinMsgSelectionChanged)
    {
        // A click on the table: the selection moves to the clicked row's character; the new and
        // the old row are redrawn.
        if (static_cast<uintptr_t>(A) != reinterpret_cast<uintptr_t>(CharacterTable))
            return false;
        CHARACTER_ID const OldSelection = SelectedCharacter;
        SelectedCharacter = G_NoCharacterId;
        unsigned ClickedRow = 0;
        if (CharacterTable->GetSelectedRow(&ClickedRow))
        {
            FUiFastTableRow* const RowRecord = CharacterTable->GetRow(ClickedRow);
            if (RowRecord->Payload.Object != nullptr)
            {
                SelectedCharacter =
                    static_cast<CharacterRowData const*>(RowRecord->Payload.Object)->Id;

                CCharacterSet::CharacterInfo* NewOne = nullptr;
                CCharacterSet::CharacterInfo* OldOne = nullptr;
                int const Count = static_cast<int>(G_LoginMgr.Characters.count());
                for (int Index = 0; Index < Count; ++Index)
                {
                    if (NewOne != nullptr && OldOne != nullptr)
                        break;
                    CCharacterSet::CharacterInfo* const Character = G_LoginMgr.CharacterAt(Index);
                    if (Character->Id == SelectedCharacter)
                        NewOne = Character;
                    if (Character->Id == OldSelection)
                        OldOne = Character;
                }
                if (NewOne != nullptr)
                    FillRow(ClickedRow, NewOne);
                // The old one's row: the first row from 1 whose payload carries its id.
                if (OldOne != nullptr)
                {
                    for (unsigned Row = 1; Row < CharacterTable->RowEdges.size(); ++Row)
                    {
                        FUiFastTableRow* const Other = CharacterTable->GetRow(Row);
                        if (Other->Payload.Object != nullptr &&
                            static_cast<CharacterRowData const*>(Other->Payload.Object)->Id ==
                                OldSelection)
                        {
                            FillRow(Row, OldOne);
                            break;
                        }
                    }
                }
            }
        }
        UpdateButtons(); // inlined in retail
        return true;
    }

    if (Msg == kWinMsgConfirmAnswer)
    {
        // The delete confirm's answer.
        if (A == 0)
        {
            ProcessMessage(kWinMsgCommand, 0xf003, 0); // virtual, on itself
            return true;
        }
        if (A != 1)
            return true;
        unsigned SelectedRow = A;
        CharacterTable->GetSelectedRow(&SelectedRow); // result not tested
        CHARACTER_ID const Victim =
            static_cast<CharacterRowData const*>(CharacterTable->GetRow(SelectedRow)->Payload.Object)
                ->Id;
        int const Destroyed = G_LoginMgr.DestroyCharacter(Victim);
        if (Destroyed == 1)
        {
            CharacterTable->ClearSelection();
            SelectedCharacter = G_NoCharacterId;
        }
        else if (Destroyed == 2)
        {
            Result = kMenuStateServerSelect;
        }
        else
        {
            NnErrorRunModal(0x73b, false, nullptr);
            G_LoginMgr.ReloadFactions();
            int const Relisted = G_LoginMgr.RequestCharacterInfo();
            if (Relisted != 1)
            {
                if (Relisted != 2)
                    NnErrorRunModal(0x73c, false, nullptr);
                Result = kMenuStateServerSelect;
            }
        }
        RebuildTable();
        UpdateButtons();
        return true;
    }

    if (Msg == kWinMsgPress)
    {
        // A header click: the sort column or its direction, the headers redrawn, the table
        // rebuilt (RebuildTable's body inline, the leftover rows through ClearRow).
        if (static_cast<uintptr_t>(A) != reinterpret_cast<uintptr_t>(CharacterTable))
            return false;
        unsigned const Column = B & 0xffffu;
        if ((B & 0xffff0000u) != 0 || Column >= 5)
            return false;
        if (Column == SortColumn)
            bAscending[Column] = static_cast<unsigned char>(bAscending[Column] == 0);
        else
            SortColumn = Column;
        for (unsigned Header = 0; Header < 5; ++Header)
            SetCell(0, Header, &ColumnHeaders[Header], false, false);
        CharacterTable->GetRow(0)->bHoverable = 0;
        CharacterTable->ClearSelection();

        int const CharacterCount = static_cast<int>(G_LoginMgr.Characters.count());
        unsigned const RowCount = static_cast<unsigned>(EdgeCount(CharacterTable->RowEdges));
        CharacterScroll->SetRange(0.0f, static_cast<float>(CharacterCount));
        int const VisibleRows = static_cast<int>(RowCount) - 1;
        CharacterScroll->SetPage(static_cast<float>(VisibleRows));
        CharacterScroll->SetStep(1.0f);
        FirstVisible = static_cast<int>(CharacterScroll->GetValue());
        if (CharacterCount > VisibleRows)
            ShowWin3D(CharacterScroll);
        else
            HideWin3D(CharacterScroll);

        std::vector<CCharacterSet::CharacterInfo*> Characters(static_cast<size_t>(CharacterCount));
        for (int Index = 0; Index < CharacterCount; ++Index)
            Characters[static_cast<size_t>(Index)] = G_LoginMgr.CharacterAt(Index);
        if (CharacterCount != 0)
        {
            FCharacterCompare const Compare = bAscending[SortColumn] != 0
                                                  ? kAscendingCompare[SortColumn]
                                                  : kDescendingCompare[SortColumn];
            qsort(Characters.data(), static_cast<size_t>(CharacterCount),
                  sizeof(CCharacterSet::CharacterInfo*), Compare);
        }
        else
        {
            SelectedCharacter = G_NoCharacterId;
        }

        unsigned Row = 1;
        for (int Index = FirstVisible; Index < CharacterCount; ++Index, ++Row)
        {
            if (Row >= RowCount)
                return true;
            CCharacterSet::CharacterInfo* const Character = Characters[static_cast<size_t>(Index)];
            if (Character != nullptr)
            {
                FillRow(Row, Character);
                if (Character->Id == SelectedCharacter)
                    CharacterTable->SelectRow(Row);
            }
        }
        for (; Row < RowCount; ++Row)
        {
            ClearRow(Row);
            CharacterTable->GetRow(Row)->bSelectable = 0;
            CharacterTable->GetRow(Row)->bHoverable = 0;
        }
        return true;
    }

    if (Msg == kWinMsgDoubleClick)
    {
        // A double click on the table loads.
        if (static_cast<uintptr_t>(A) != reinterpret_cast<uintptr_t>(CharacterTable))
            return false;
        BeginLeave();
        LeaveDirection = kCharSelectLeaveLoad;
        return true;
    }

    if (Msg == kWinMsgScroll)
    {
        RebuildTable();
        return true;
    }
    return false;
}

void PageCharacterSelect::DisableToolTips()
{
    // FUN_0056a150.
    LoadCharacter->SetToolTipEnabled(false);
    NewCharacter->SetToolTipEnabled(false);
    DeleteCharacter->SetToolTipEnabled(false);
    Back->SetToolTipEnabled(false);
    MainMenu->SetToolTipEnabled(false);
    ServerDetails->SetToolTipEnabled(false);
    Motd->SetToolTipEnabled(false);
}

void PageCharacterSelect::ClearRow(unsigned Row)
{
    // FUN_0056a1b0. The column count is read again each pass.
    for (unsigned Column = 0; Column < CharacterTable->ColEdges.size(); ++Column)
        CharacterTable->ClearCell(Row, Column);
    FUiFastTableRow* const RowRecord = CharacterTable->GetRow(Row);
    if (RowRecord != nullptr && RowRecord->Payload.Object != nullptr)
        RowRecord->Payload.Reset();
}

void PageCharacterSelect::UpdateButtons()
{
    // FUN_0056a230.
    bool const bNone = SelectedCharacter == G_NoCharacterId;
    SetButtonLive(LoadCharacter, !bNone);
    SetButtonLive(DeleteCharacter, !bNone);
    SetButtonLive(NewCharacter, G_LoginMgr.Characters.count() < 5);
}

bool PageCharacterSelect::OnEvent(unsigned MsgId)
{
    // LAB_0056a300 (this = the +0x80 face): event 0xf reconnects unless a leave runs.
    if (MsgId != USER_CANCEL)
        return UIPage::MasterWin::OnEvent(MsgId);
    if (bLeaving == 0)
    {
        ShutdownServerConnection();
        if (!ConnectToServer(g_display.ini_path.Text))
        {
            G_LoginMgr.Reset();
            Result = kMenuStateMultiplayer; // dead store, overwritten below as retail
        }
        Result = kMenuStateServerSelect;
    }
    return true;
}

void PageCharacterSelect::Update()
{
    // LAB_0056a360.
    RefreshMotd();
    Win3D::Update();

    // Below 1.0 and not leaving slides in; leaving (at any value) slides out.
    bool const bBelowOne = SlideT < 1.0;
    if (bBelowOne && bLeaving == 0)
    {
        // Frame time capped at 0.5, 1.3 per second.
        double Dt = GetClientFrameTimeCapped();
        if (Dt > 0.5)
            Dt = 0.5;
        SlideT = Dt * 1.3 + SlideT;

        if (SlideT < 0.0)
        {
            Vector const Along = {static_cast<float>(SlideT), 0.0f, 0.0f};
            float const Against[3] = {static_cast<float>(-SlideT), 0.0f, 0.0f};
            Vector const Up = {0.0f, static_cast<float>(SlideT), 0.0f};
            LoadCharacter->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(&Along), 0);
            NewCharacter->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(&Along), 0);
            DeleteCharacter->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(&Along), 0);
            Back->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(&Along), 0);
            MainMenu->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(&Along), 0);
            Vector Scaled;
            Vector Position;
            ScaleVec3(&Scaled.x, Against, 1.20000005f);
            Win3DVec3Add(&Position, &CharacterBoxHome, &Scaled);
            CharacterBox->SetPosition(&Position);
            Win3DVec3Add(&Position, &MotdBoxHome, &Up);
            MotdBox->SetPosition(&Position);
            ScaleVec3(&Scaled.x, &Along.x, 1.20000005f);
            Win3DVec3Add(&Position, &DetailsBoxHome, &Scaled);
            ServerDetailsBox->SetPosition(&Position);
        }
        else if (SlideT > 0.0)
        {
            // Arrived (strictly past 0): everything in place, shown and live; SlideT parked at 2.0.
            Vector const ZeroOffset = {0.0f, 0.0f, 0.0f};
            LoadCharacter->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(&ZeroOffset), 0);
            NewCharacter->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(&ZeroOffset), 0);
            DeleteCharacter->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(&ZeroOffset), 0);
            Back->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(&ZeroOffset), 0);
            MainMenu->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(&ZeroOffset), 0);
            CharacterBox->SetPosition(&CharacterBoxHome);
            MotdBox->SetPosition(&MotdBoxHome);
            ServerDetailsBox->SetPosition(&DetailsBoxHome);
            LoadCharacter->SetSkipUpload(false);
            NewCharacter->SetSkipUpload(false);
            DeleteCharacter->SetSkipUpload(false);
            Back->SetSkipUpload(false);
            MainMenu->SetSkipUpload(false);
            ShowWin3D(ServerDetails);
            ShowWin3D(Motd);
            ShowWin3D(Title);
            ShowWin3D(CharacterTable);
            int const Count = static_cast<int>(G_LoginMgr.Characters.count());
            if (Count > EdgeCount(CharacterTable->RowEdges) - 1)
                ShowWin3D(CharacterScroll);
            else
                HideWin3D(CharacterScroll);
            LoadCharacter->SetToolTipEnabled(true);
            LoadCharacter->GetToolTipHoverInfo()->IdsAlt = 0xad5;
            NewCharacter->SetToolTipEnabled(true);
            NewCharacter->GetToolTipHoverInfo()->IdsAlt = 0xad6;
            DeleteCharacter->SetToolTipEnabled(true);
            DeleteCharacter->GetToolTipHoverInfo()->IdsAlt = 0xad7;
            Back->SetToolTipEnabled(true);
            Back->GetToolTipHoverInfo()->IdsAlt = 0xad4;
            MainMenu->SetToolTipEnabled(true);
            MainMenu->GetToolTipHoverInfo()->IdsAlt = 0xac6;
            ServerDetails->SetToolTipEnabled(true);
            ServerDetails->GetToolTipHoverInfo()->IdsAlt = 0xac8;
            Motd->SetToolTipEnabled(true);
            Motd->GetToolTipHoverInfo()->IdsAlt = 0xae4;
            SlideT = 2.0;
        }

        if (SlideT > -0.5 && SlideT < -0.1)
            PlayUiSoundSlot(kUiSoundMotionSwish);
    }
    else if (bLeaving != 0)
    {
        // 1.5 per second, no frame-time cap.
        SlideT = SlideT - GetClientFrameTimeCapped() * 1.5;
        // At or below -0.9, or unordered, the leave ends.
        if (SlideT > -0.9)
        {
            Vector const Along = {static_cast<float>(SlideT), 0.0f, 0.0f};
            float const Negated = static_cast<float>(-SlideT);
            LoadCharacter->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(&Along), 0);
            NewCharacter->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(&Along), 0);
            DeleteCharacter->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(&Along), 0);
            Back->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(&Along), 0);
            MainMenu->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(&Along), 0);
            // The same three positions as the slide-in, but built with FUN_00422b80's float sums.
            float const Out[3] = {Negated * 1.20000005f, 0.0f, 0.0f};
            Vector Position = {Out[0] + CharacterBoxHome.x, Out[1] + CharacterBoxHome.y,
                               Out[2] + CharacterBoxHome.z};
            CharacterBox->SetPosition(&Position);
            Position.x = MotdBoxHome.x;
            Position.y = Along.x + MotdBoxHome.y;
            Position.z = MotdBoxHome.z;
            MotdBox->SetPosition(&Position);
            float const Right[3] = {Along.x * 1.20000005f, Along.y * 1.20000005f,
                                    Along.z * 1.20000005f};
            Position.x = Right[0] + DetailsBoxHome.x;
            Position.y = Right[1] + DetailsBoxHome.y;
            Position.z = Right[2] + DetailsBoxHome.z;
            ServerDetailsBox->SetPosition(&Position);
        }
        else
        {
            HideWin3D(LoadCharacter);
            HideWin3D(NewCharacter);
            HideWin3D(DeleteCharacter);
            HideWin3D(Back);
            HideWin3D(MainMenu);
            HideWin3D(ServerDetails);
            HideWin3D(Motd);
            HideWin3D(Title);
            HideWin3D(CharacterBox);
            HideWin3D(MotdBox);
            HideWin3D(ServerDetailsBox);
            bLeaving = 0;
            if (LeaveDirection == kCharSelectLeaveLoad)
            {
                bLoadRequested = 1;
            }
            else if (LeaveDirection == kCharSelectLeaveNew)
            {
                // The character-create pick: faction record 0, its id and the first key of each
                // set, with no empty test (as retail).
                memset(&G_CharacterCreateSelection, 0, sizeof(G_CharacterCreateSelection));
                G_CharacterCreateSelection.FactionIndex = 0;
                FNewCharFaction const& Faction =
                    G_LoginMgr.GetFactions()[static_cast<size_t>(
                        G_CharacterCreateSelection.FactionIndex)];
                G_CharacterCreateSelection.Info.FactionId = Faction.NicknameId;
                G_CharacterCreateSelection.Info.BaseId = *Faction.Bases.begin();
                G_CharacterCreateSelection.Info.PackageId = *Faction.Packages.begin();
                G_CharacterCreateSelection.Info.PilotId = *Faction.Pilots.begin();
                Result = kMenuStateCharacterCreate;
            }
            else if (LeaveDirection == kCharSelectLeaveBack)
            {
                ShutdownServerConnection();
                Result = kMenuStateMainMenu;
            }
            else
            {
                ShutdownServerConnection();
                Result = kMenuStateMultiplayer;
            }
        }
    }

    // Once IntroFocus has left the intro base, the selected row's character becomes the chosen
    // one (loading screen first), state 0xc.
    if (bIntroBaseLeft == 0)
        return;
    bIntroBaseLeft = 0;
    bLoadRequested = 0;
    if (bCharacterChosen != 0)
        return;
    LoadingScreenSetClear(1);
    LoadingScreenStart();
    unsigned SelectedRow = 0;
    if (!CharacterTable->GetSelectedRow(&SelectedRow))
        return;
    FUiFastTableRow* const RowRecord = CharacterTable->GetRow(SelectedRow);
    if (RowRecord->Payload.Object == nullptr)
        return;
    bCharacterChosen = 1;
    SelectedCharacter = static_cast<CharacterRowData const*>(RowRecord->Payload.Object)->Id;
    Result = kMenuStateEnterGame;
    if (SelectedCharacter != CurrentCharacter)
        ChatResetForConnect();
    CurrentCharacter = SelectedCharacter;
}

void PageCharacterSelect::RefreshMotd()
{
    // FUN_0056abb0.
    int const Version = ChatGetServerMotdVersion();
    if (Version == MotdVersion)
        return;
    MotdVersion = Version;
    Motd->ClearContent();
    AppendJustify(Motd, TextJustifyCenter);
    AppendStyle(Motd, kTextStyleList);
    AppendRef(Motd, MotdHeader);
    AppendParagraph(Motd);
    AppendJustify(Motd, TextJustifyLeft);
    AppendStyle(Motd, 0x76ec);
    Motd->AppendContent(*ChatGetServerMotdRdl());
}

void PageCharacterSelect::BeginLeave()
{
    // FUN_0056ad40. The caller sets the direction.
    bLeaving = 1;
    SlideT = 0.0;
    LoadCharacter->bInteractive = 0;
    NewCharacter->bInteractive = 0;
    DeleteCharacter->bInteractive = 0;
    Back->bInteractive = 0;
    MainMenu->bInteractive = 0;
    LoadCharacter->SetSkipUpload(true);
    NewCharacter->SetSkipUpload(true);
    DeleteCharacter->SetSkipUpload(true);
    Back->SetSkipUpload(true);
    MainMenu->SetSkipUpload(true);
    HideWin3D(ServerDetails);
    HideWin3D(Motd);
    HideWin3D(Title);
    HideWin3D(CharacterTable);
    HideWin3D(CharacterScroll);
    PlayUiSoundSlot(kUiSoundMotionSwish);
    DisableToolTips(); // inlined in retail
}

PageCharacterSelect& GetPageCharacterSelect()
{
    return G_PageCharacterSelect;
}
