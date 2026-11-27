// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/Pages/page_character_select_single.h"

#include "Common/Fonts/font_string.h"       // FHudWideText, FHudWideText::Assign (FUN_00413df0)
#include "Core/globals.h"           // GetClientFrameTimeCapped (FUN_0042d680)
#include "Common/Fonts/ids_rdl.h"           // AppendTextAsRdl (FUN_0057db50), AppendTextMaybeXmlRdl (FUN_0057e2c0)
#include "Common/Fonts/ids_strings.h"       // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "Interface/Pages/intro_page_util.h"
#include "Interface/loading_screen.h"    // LoadingScreenSetClear / LoadingScreenStart (FUN_00579280 / 290)
#include "Network/login_mgr.h"         // G_LoginMgr (DAT_0067e7b8)
#include "Interface/menu_ui_init.h"      // PlayUiSoundSlot (FUN_00564570), PlayOrRewindUiSoundSlot (5645c0)
#include "NeuroNet/nn_confirm.h"        // NnConfirmOpenFormatted (FUN_00488a80)
#include "NeuroNet/nn_error.h"          // NnErrorRunModal (FUN_00473cb0)
#include "NeuroNet/nn_load_save.h"      // NnLoadSaveEntryText (FUN_00488b70)
#include "Interface/ui_list_box.h"
#include "Interface/ui_rich_text_win.h"
#include "Interface/ui_scroll.h"
#include "Interface/ui_text_msg_button.h"
#include "Interface/menu_enter_state.h"
#include "NeuroNet/Chat/chat_init.h"        // EChatTextStyle

#include "common_archetype.h"  // Archetype::GetShip
#include "common_misc.h"       // filetime_to_wstring
#include "common_text.h"
#include "common_tstring.h"    // TString<32> (FUN_00420750)
#include "common_universe.h"   // Universe::get_system

#include <stdint.h>            // intptr_t
#include <stdlib.h>            // free
#include <string.h>            // memcmp, memcpy
#include <wchar.h>             // swprintf, wcslen
#include <string>
#include <vector>

using namespace IntroPageUtil;

namespace
{
    // DAT_0067ae10.
    PageCharacterSelectSingle G_PageCharacterSelectSingle;

    // .rdata:005e2af4 -- L"-", the text of an empty value window.
    wchar_t const kDashText[] = L"-";

    // The UIListBox rows carry the CharacterInfo* as their data word.
    int InfoToItemData(CCharacterSet::CharacterInfo* Info)
    {
        return static_cast<int>(reinterpret_cast<intptr_t>(Info));
    }

    CCharacterSet::CharacterInfo* ItemDataToInfo(int Data)
    {
        return reinterpret_cast<CCharacterSet::CharacterInfo*>(static_cast<intptr_t>(Data));
    }

    void AppendStyle(UiRichTextWin* Win, unsigned short Style)
    {
        StyleNode const Node(Style);
        Win->AppendNode(Node);
    }

    void AppendRef(UiRichTextWin* Win, RenderDisplayList& Rdl)
    {
        RDLRefNode const Node(Rdl);
        Win->AppendNode(Node);
    }

    // The {Text, Capacity} local of ShowDetails: free()d on the way out, also by the EH unwind.
    struct FScopedWideText : FHudWideText
    {
        FScopedWideText()
        {
            Text = nullptr;
            Capacity = 0;
        }
        ~FScopedWideText()
        {
            if (Text != nullptr)
                free(Text);
        }
        FScopedWideText(FScopedWideText const&) = delete;
        FScopedWideText& operator=(FScopedWideText const&) = delete;
    };

    // The row loop over the three "bImg" statics, inlined twice in Update: the row index runs
    // 2 down to 0, V = Row * 0.1 (DAT_005c7720) + SlideT; at or below 0.0 (or unordered) the
    // image goes to {V, 0, 0}, above to {0, 0, 0}.
    void PlaceButtonImages(Win3D* const* Images, double SlideT)
    {
        int Row = 2;
        for (int Index = 0; Index < 3; ++Index, --Row)
        {
            double const V = static_cast<double>(Row) * 0.1 + SlideT;
            Vector Position;
            if (V > 0.0)
            {
                Position.x = 0.0f;
                Position.y = 0.0f;
                Position.z = 0.0f;
            }
            else
            {
                Position.x = static_cast<float>(V);
                Position.y = 0.0f;
                Position.z = 0.0f;
            }
            Images[Index]->SetPosition(&Position);
        }
    }
}

// ---------------------------------------------------------------------------------------------

PageCharacterSelectSingle::PageCharacterSelectSingle()
    : UIPage::MasterWin(Desc.GetPageData())
    , Result(0)
    , CharacterMoney(nullptr)
    , CharacterRank(nullptr)
    , CharacterLocation(nullptr)
    , CharacterShip(nullptr)
    , LoadCharacter(nullptr)
    , DeleteCharacter(nullptr)
    , MainMenu(nullptr)
    , CharScroll(nullptr)
    , CharSelDesc(nullptr)
    , CharSelTime(nullptr)
    , MoneyLabel(nullptr)
    , RankLabel(nullptr)
    , LocationLabel(nullptr)
    , ShipLabel(nullptr)
    , ScreenTitle(nullptr)
    , ButtonImages{nullptr, nullptr, nullptr}
    , BigBox(nullptr)
    , SmallBox(nullptr)
    , SlideT(1.0)
    , bLeaving(0)
    , LeaveDirection(-1)
    , bDeletePending(0)
    , bCharacterChosen(0)
    , bLoadRequested(0)
    , bIntroBaseLeft(0)
{
    // FUN_0056b050. Retail leaves the statics +0xf4..+0x104 and the bytes +0x318 / +0x319
    // unwritten; the static object starts zeroed, which is what the initialisers above reproduce.
}

PageCharacterSelectSingle::~PageCharacterSelectSingle()
{
    // FUN_0056d360.
}

void PageCharacterSelectSingle::Destroy()
{
    // LAB_0056b240. Hidden first.
    Flags = static_cast<unsigned char>(Flags & ~(kWin3DActive | kWin3DVisible));

    // Every child is destroyed; the StaticControls IntroFocus built are also deleted. The class
    // name is copied before Destroy and compared afterwards (14 bytes, terminator included).
    Win3D* Child = ChildHead;
    while (Child != nullptr)
    {
        Win3D* const Next = Child->NextSibling;
        TString<32> ClassCopy;
        ClassCopy.Length = 0;
        ClassCopy.Data[0] = '\0';
        ClassCopy = Child->ClassName;
        Child->Destroy();
        if (memcmp(ClassCopy.Data, "StaticControl", 14) == 0)
            delete Child;
        Child = Next;
    }

    UIPage::MasterWin::Destroy();
    UnregisterEvent(USER_CANCEL);

    CharacterShip = nullptr;
    LoadCharacter = nullptr;
    DeleteCharacter = nullptr;
    MainMenu = nullptr;
    CharacterMoney = nullptr;
    CharacterRank = nullptr;
    CharacterLocation = nullptr;
    CharScroll = nullptr;
    CharSelDesc = nullptr;
    CharSelTime = nullptr;
    MoneyLabel = nullptr;
    RankLabel = nullptr;
    LocationLabel = nullptr;
    ShipLabel = nullptr;
    ScreenTitle = nullptr;
}

bool PageCharacterSelectSingle::Create(Win3D* NewParent, void* /*Payload*/)
{
    // LAB_0056b330. MasterWin::Create with no payload; its result is not tested.
    UIPage::MasterWin::Create(NewParent, nullptr);
    Result = 0;

    // In retail's lookup order.
    CharacterShip = static_cast<UiRichTextWin*>(FindWidget("CharacterShip"));
    LoadCharacter = static_cast<UiTextMsgButton*>(FindWidget("LoadCharacter"));
    DeleteCharacter = static_cast<UiTextMsgButton*>(FindWidget("DeleteCharacter"));
    MainMenu = static_cast<UiTextMsgButton*>(FindWidget("MainMenu"));
    CharacterMoney = static_cast<UiRichTextWin*>(FindWidget("CharacterMoney"));
    CharacterRank = static_cast<UiRichTextWin*>(FindWidget("CharacterRank"));
    CharacterLocation = static_cast<UiRichTextWin*>(FindWidget("CharacterLocation"));
    CharScroll = static_cast<UiScroll*>(FindWidget("CharScroll"));
    CharSelDesc = static_cast<UiListBox*>(FindWidget("CharSelDesc"));
    CharSelTime = static_cast<UiListBox*>(FindWidget("CharSelTime"));
    MoneyLabel = static_cast<UiRichTextWin*>(FindWidget("MoneyLabel"));
    RankLabel = static_cast<UiRichTextWin*>(FindWidget("RankLabel"));
    LocationLabel = static_cast<UiRichTextWin*>(FindWidget("LocationLabel"));
    ShipLabel = static_cast<UiRichTextWin*>(FindWidget("ShipLabel"));
    ScreenTitle = static_cast<UiRichTextWin*>(FindWidget("ScreenTitle"));

    RegisterEvent(USER_CANCEL);

    CharScroll->SetArrowColors(ChromeColor(), ChromeColor());
    CharScroll->SetBarColors(ChromeColor(), ChromeColor());
    CharScroll->SetPage(20.0f);
    CharScroll->SetStep(1.0f);

    LoadCharacter->SetDrawFrame(false);
    DeleteCharacter->SetDrawFrame(false);
    MainMenu->SetDrawFrame(false);

    bDeletePending = 0;
    bCharacterChosen = 0;
    SlideT = -2.0;

    G_LoginMgr.ReloadFactions();

    // Only a 1 fills the lists.
    if (G_LoginMgr.RequestCharacterInfo() == 1)
    {
        CharSelDesc->ItemCount = 0;
        CharSelTime->ItemCount = 0;
        CharScroll->SetPage(20.0f);
        CharScroll->SetStep(1.0f);
        ShowWin3D(CharScroll);

        // One row per record that is not the restart save.
        int Added = 0;
        unsigned const Count = G_LoginMgr.Characters.count();
        Entries.clear();
        Entries.reserve(Count);
        for (unsigned Index = 0; Index < Count; ++Index)
        {
            CCharacterSet::CharacterInfo* const Info =
                G_LoginMgr.CharacterAt(static_cast<int>(Index));
            if (Info == nullptr || Info->bIsRestart != 0)
                continue;
            FCharSelectSingleEntry Entry;
            Entry.Info = Info;
            wchar_t const* const EntryText = NnLoadSaveEntryText(Info);
            Entry.Name.assign(EntryText, wcslen(EntryText));
            wchar_t const* const TimeText =
                filetime_to_wstring(*reinterpret_cast<_FILETIME const*>(&Info->TimestampLow));
            Entry.Time.assign(TimeText, wcslen(TimeText));
            Entries.push_back(Entry);
            FCharSelectSingleEntry const& Last = Entries.back();
            CharSelDesc->AddItem(Last.Name.c_str(), InfoToItemData(Last.Info));
            CharSelTime->AddItem(Last.Time.c_str(), InfoToItemData(Last.Info));
            ++Added;
        }

        CharScroll->SetRange(0.0f, static_cast<float>(Added));
        CharScroll->SetPage(20.0f);
        CharScroll->SetStep(1.0f);
        CharScroll->SetValue(0.0f);
        UpdateScrollVisibility();

        CharSelDesc->FirstVisible = static_cast<int>(CharScroll->GetValue());
        CharSelTime->FirstVisible = static_cast<int>(CharScroll->GetValue());
    }

    CharacterShip->ClearContent();
    LoadCharacter->Flags =
        static_cast<unsigned char>(LoadCharacter->Flags | kWin3DActive | kWin3DVisible);
    DeleteCharacter->Flags =
        static_cast<unsigned char>(DeleteCharacter->Flags | kWin3DActive | kWin3DVisible);
    MainMenu->Flags = static_cast<unsigned char>(MainMenu->Flags | kWin3DActive | kWin3DVisible);

    CharSelDesc->bDrawFrame = 0;
    CharSelDesc->bDrawRowRules = 1;
    CharSelTime->bDrawFrame = 0;
    CharSelTime->bDrawRowRules = 1;

    ShowDetails(nullptr);

    HideWin3D(CharacterMoney);
    HideWin3D(CharacterRank);
    HideWin3D(CharacterLocation);
    HideWin3D(CharacterShip);
    HideWin3D(LoadCharacter);
    HideWin3D(DeleteCharacter);
    HideWin3D(MainMenu);
    HideWin3D(CharScroll);
    HideWin3D(CharSelDesc);
    HideWin3D(CharSelTime);
    HideWin3D(MoneyLabel);
    HideWin3D(RankLabel);
    HideWin3D(LocationLabel);
    HideWin3D(ShipLabel);
    HideWin3D(ScreenTitle);

    // No trailing-paragraph cut here.
    LoadIds(MoneyLabelText, 0x594);
    LoadIds(RankLabelText, 0x5ec);
    LoadIds(LocationLabelText, 0x593);
    LoadIds(ShipLabelText, 0x591);
    LoadIds(TitleText, 0x4f5);

    MoneyLabel->ClearContent();
    AppendStyle(MoneyLabel, 0x5724);
    AppendRef(MoneyLabel, MoneyLabelText);
    RankLabel->ClearContent();
    AppendStyle(RankLabel, 0x5724);
    AppendRef(RankLabel, RankLabelText);
    LocationLabel->ClearContent();
    AppendStyle(LocationLabel, 0x5724);
    AppendRef(LocationLabel, LocationLabelText);
    ShipLabel->ClearContent();
    AppendStyle(ShipLabel, 0x5724);
    AppendRef(ShipLabel, ShipLabelText);
    ScreenTitle->ClearContent();
    AppendStyle(ScreenTitle, kTextStyleScreenTitle);
    AppendRef(ScreenTitle, TitleText);

    // Retail repeats ShowDetails(nullptr)'s dash fill here, without the button changes.
    {
        RenderDisplayList Dash;
        AppendTextMaybeXmlRdl(kDashText, static_cast<unsigned>(wcslen(kDashText)), Dash, false);
        CharacterShip->ClearContent();
        AppendStyle(CharacterShip, 0x5724);
        CharacterShip->AppendContent(Dash);
        CharacterMoney->ClearContent();
        AppendStyle(CharacterMoney, 0x5724);
        CharacterMoney->AppendContent(Dash);
        CharacterRank->ClearContent();
        AppendStyle(CharacterRank, 0x5724);
        CharacterRank->AppendContent(Dash);
        CharacterLocation->ClearContent();
        AppendStyle(CharacterLocation, 0x5724);
        CharacterLocation->AppendContent(Dash);

        CharSelDesc->VisibleRows = 0x14;
        CharSelTime->VisibleRows = 0x14;
    }
    return true;
}

void PageCharacterSelectSingle::UpdateScrollVisibility()
{
    // FUN_0056bd10.
    if (CharSelDesc->ItemCount - 0x14 >= 1)
        CharScroll->Flags =
            static_cast<unsigned char>(CharScroll->Flags | kWin3DActive | kWin3DVisible);
    else
        CharScroll->Flags =
            static_cast<unsigned char>(CharScroll->Flags & ~(kWin3DActive | kWin3DVisible));
}

void PageCharacterSelectSingle::ShowDetails(CCharacterSet::CharacterInfo const* Info)
{
    // FUN_0056bd40 (RET 4).
    if (Info == nullptr)
    {
        RenderDisplayList Dash;
        AppendTextMaybeXmlRdl(kDashText, static_cast<unsigned>(wcslen(kDashText)), Dash, false);
        CharacterShip->ClearContent();
        AppendStyle(CharacterShip, 0x5724);
        CharacterShip->AppendContent(Dash);
        CharacterMoney->ClearContent();
        AppendStyle(CharacterMoney, 0x5724);
        CharacterMoney->AppendContent(Dash);
        CharacterRank->ClearContent();
        AppendStyle(CharacterRank, 0x5724);
        CharacterRank->AppendContent(Dash);
        CharacterLocation->ClearContent();
        AppendStyle(CharacterLocation, 0x5724);
        CharacterLocation->AppendContent(Dash);
        LoadCharacter->Flags = static_cast<unsigned char>(LoadCharacter->Flags & ~kWin3DActive);
        DeleteCharacter->Flags = static_cast<unsigned char>(DeleteCharacter->Flags & ~kWin3DActive);
        LoadCharacter->bInteractive = 0;
        DeleteCharacter->bInteractive = 0;
        return;
    }

    wchar_t Number[0x80];
    swprintf(Number, 0x80, L"%d", Info->Money);
    CharacterMoney->ClearContent();
    AppendStyle(CharacterMoney, 0x5724);
    RenderDisplayList MoneyText;
    AppendTextAsRdl(Number, static_cast<unsigned>(wcslen(Number)), MoneyText, false);
    CharacterMoney->AppendContent(MoneyText);

    swprintf(Number, 0x80, L"%d", Info->Rank);
    CharacterRank->ClearContent();
    AppendStyle(CharacterRank, 0x5724);
    RenderDisplayList RankText;
    AppendTextAsRdl(Number, static_cast<unsigned>(wcslen(Number)), RankText, false);
    CharacterRank->AppendContent(RankText);

    // The ship's name only with a ship archetype that resolves; a missed lookup leaves the
    // window with just the style.
    CharacterShip->ClearContent();
    if (Info->ShipArchetype != 0)
    {
        Archetype::Ship const* const Ship = Archetype::GetShip(Info->ShipArchetype);
        if (Ship != nullptr)
        {
            wchar_t ShipName[0x80];
            if (GetIdsStringWWithRawFallback(static_cast<unsigned>(Ship->IdsName), ShipName,
                                             0x80) == 0)
                ShipName[0] = L'\0';
            AppendStyle(CharacterShip, 0x5724);
            RenderDisplayList ShipText;
            AppendTextAsRdl(ShipName, static_cast<unsigned>(wcslen(ShipName)), ShipText, false);
            CharacterShip->AppendContent(ShipText);
        }
    }

    // The system's name, "IDS?" on a missed lookup.
    Universe::ISystem const* const System = Universe::get_system(Info->SystemId);
    CharacterLocation->ClearContent();
    if (System != nullptr)
    {
        FScopedWideText SystemName;
        wchar_t Buffer[0x1000];
        unsigned const Length = GetIdsStringWWithRawFallback(
            (unsigned)System->GetStridName(), Buffer, 0x1000);
        if (Length == 0)
            SystemName.Assign(L"IDS?", 0xffffffffu);
        else
            SystemName.Assign(Buffer, Length);
        AppendStyle(CharacterLocation, 0x5724);
        RenderDisplayList LocationText;
        AppendTextAsRdl(SystemName.Text, static_cast<unsigned>(wcslen(SystemName.Text)),
                        LocationText, false);
        CharacterLocation->AppendContent(LocationText);
    }

    LoadCharacter->Flags = static_cast<unsigned char>(LoadCharacter->Flags | kWin3DActive);
    DeleteCharacter->Flags = static_cast<unsigned char>(DeleteCharacter->Flags | kWin3DActive);
    LoadCharacter->bInteractive = 1;
    DeleteCharacter->bInteractive = 1;
}

void PageCharacterSelectSingle::BeginLeave(int Direction)
{
    HideWin3D(CharacterMoney);
    HideWin3D(CharacterRank);
    HideWin3D(CharacterLocation);
    HideWin3D(CharacterShip);
    HideWin3D(LoadCharacter);
    HideWin3D(DeleteCharacter);
    HideWin3D(MainMenu);
    HideWin3D(CharScroll);
    HideWin3D(CharSelDesc);
    HideWin3D(CharSelTime);
    HideWin3D(MoneyLabel);
    HideWin3D(RankLabel);
    HideWin3D(LocationLabel);
    HideWin3D(ShipLabel);
    HideWin3D(ScreenTitle);
    ShowWin3D(ButtonImages[0]);
    ShowWin3D(ButtonImages[1]);
    ShowWin3D(ButtonImages[2]);
    CharSelDesc->bSelectable = 0;
    CharSelTime->bSelectable = 0;
    LoadCharacter->bInteractive = 0;
    DeleteCharacter->bInteractive = 0;
    MainMenu->bInteractive = 0;
    LeaveDirection = Direction;
    bLeaving = 1;
    SlideT = 0.0;
    PlayUiSoundSlot(kUiSoundMotionSwish);
}

bool PageCharacterSelectSingle::ProcessMessage(unsigned Msg, unsigned A, unsigned B)
{
    // LAB_0056c360.
    if (Msg == kWinMsgCommand)
    {
        PlayOrRewindUiSoundSlot(kUiSoundSelectItem);
        if (A == kCharSelectSingleCmdLoad)
        {
            if (bLeaving != 0)
                return true;
            BeginLeave(1);
            return true;
        }
        if (A == kCharSelectSingleCmdMainMenu)
        {
            if (bLeaving != 0)
                return true;
            BeginLeave(0);
            return true;
        }
        if (A == kCharSelectSingleCmdDelete)
        {
            if (bDeletePending != 0)
            {
                // The confirm box said yes: everything live again.
                bDeletePending = 0;
                CharScroll->Flags = static_cast<unsigned char>(CharScroll->Flags | kWin3DActive);
                CharSelDesc->Flags = static_cast<unsigned char>(CharSelDesc->Flags | kWin3DActive);
                CharSelTime->Flags = static_cast<unsigned char>(CharSelTime->Flags | kWin3DActive);
                MainMenu->Flags = static_cast<unsigned char>(MainMenu->Flags | kWin3DActive);
                LoadCharacter->Flags = static_cast<unsigned char>(LoadCharacter->Flags | kWin3DActive);
                DeleteCharacter->Flags = static_cast<unsigned char>(DeleteCharacter->Flags | kWin3DActive);
                MainMenu->bInteractive = 1;
                LoadCharacter->bInteractive = 1;
                DeleteCharacter->bInteractive = 1;
                if (CharSelDesc->SelectedIndex == -1)
                    return true;

                // The id is copied raw before the rows that point into the roster are dropped.
                CCharacterSet::CharacterInfo const* const Victim =
                    ItemDataToInfo(CharSelDesc->GetItemData(CharSelDesc->SelectedIndex));
                CHARACTER_ID VictimId;
                memcpy(VictimId.Buffer, Victim->Id.Buffer, sizeof(VictimId.Buffer));
                Entries.clear();
                CharSelDesc->ItemCount = 0;
                CharSelTime->ItemCount = 0;
                CharScroll->SetRange(0.0f, 0.0f);

                int const Destroyed = G_LoginMgr.DestroyCharacter(VictimId);
                if (Destroyed == 1)
                {
                    if (CharSelDesc->ItemCount > -1)
                        CharSelDesc->SelectedIndex = -1;
                    if (CharSelTime->ItemCount > -1)
                        CharSelTime->SelectedIndex = -1;
                    float Min = 0.0f;
                    float Max = 0.0f;
                    CharScroll->GetRange(&Min, &Max);
                    CharScroll->SetRange(Min, static_cast<float>(Max - 1.0f));
                    ShowDetails(nullptr);
                    RefillLists();
                    return true;
                }
                if (Destroyed != 2)
                {
                    // The delete failed.
                    NnErrorRunModal(0x73b, false, nullptr);
                    SlideT = -2.0;
                    G_LoginMgr.ReloadFactions();
                    int const Relisted = G_LoginMgr.RequestCharacterInfo();
                    if (Relisted == 1)
                    {
                        RefillLists();
                        return true;
                    }
                    if (Relisted != 2)
                        NnErrorRunModal(0x73c, false, nullptr);
                }
                Result = kMenuStateServerSelect;
                RefillLists();
                return true;
            }

            // First press: the confirm box over the page; everything inactive until it answers
            // (0x3006).
            bDeletePending = 1;
            CharScroll->Flags = static_cast<unsigned char>(CharScroll->Flags & ~kWin3DActive);
            CharSelDesc->Flags = static_cast<unsigned char>(CharSelDesc->Flags & ~kWin3DActive);
            CharSelTime->Flags = static_cast<unsigned char>(CharSelTime->Flags & ~kWin3DActive);
            MainMenu->Flags = static_cast<unsigned char>(MainMenu->Flags & ~kWin3DActive);
            LoadCharacter->Flags = static_cast<unsigned char>(LoadCharacter->Flags & ~kWin3DActive);
            DeleteCharacter->Flags = static_cast<unsigned char>(DeleteCharacter->Flags & ~kWin3DActive);
            MainMenu->bInteractive = 0;
            LoadCharacter->bInteractive = 0;
            DeleteCharacter->bInteractive = 0;
            if (CharSelDesc->SelectedIndex == -1)
                return true;
            CCharacterSet::CharacterInfo const* const Selected =
                ItemDataToInfo(CharSelDesc->GetItemData(CharSelDesc->SelectedIndex));
            NnConfirmOpenFormatted(this, NnLoadSaveEntryText(Selected), nullptr);
            return true;
        }
        if (A == 0xf003)
        {
            // The confirm box said no: everything live again; false either way.
            if (bDeletePending != 0)
            {
                bDeletePending = 0;
                CharScroll->Flags = static_cast<unsigned char>(CharScroll->Flags | kWin3DActive);
                CharSelDesc->Flags = static_cast<unsigned char>(CharSelDesc->Flags | kWin3DActive);
                CharSelTime->Flags = static_cast<unsigned char>(CharSelTime->Flags | kWin3DActive);
                MainMenu->Flags = static_cast<unsigned char>(MainMenu->Flags | kWin3DActive);
                LoadCharacter->Flags = static_cast<unsigned char>(LoadCharacter->Flags | kWin3DActive);
                DeleteCharacter->Flags = static_cast<unsigned char>(DeleteCharacter->Flags | kWin3DActive);
                MainMenu->bInteractive = 1;
                LoadCharacter->bInteractive = 1;
                DeleteCharacter->bInteractive = 1;
            }
            return false;
        }
        return false;
    }

    if (Msg == kWinMsgPress)
    {
        // A click on row B (from ListBox::MouseEvent) selects it in both lists when
        // -1 <= B < count; the details follow even for an out-of-range B.
        int const Row = static_cast<int>(B);
        if (Row >= -1 && Row < CharSelDesc->ItemCount)
            CharSelDesc->SelectedIndex = Row;
        if (Row >= -1 && Row < CharSelTime->ItemCount)
            CharSelTime->SelectedIndex = Row;
        ShowDetails(ItemDataToInfo(CharSelDesc->GetItemData(Row)));
        return false;
    }

    if (Msg == kWinMsgConfirmAnswer)
    {
        // The confirm box's answer, back through the virtual ProcessMessage.
        if (A == 0)
        {
            ProcessMessage(kWinMsgCommand, 0xf003, 0);
            return true;
        }
        if (A == 1)
        {
            ProcessMessage(kWinMsgCommand, kCharSelectSingleCmdDelete, 0);
            return true;
        }
        return true;
    }

    if (Msg == kWinMsgDoubleClick)
    {
        // A double click loads the selection.
        if (CharSelDesc->SelectedIndex == -1)
            return true;
        if (bLeaving != 0)
            return true;
        BeginLeave(1);
        return true;
    }

    if (Msg == kWinMsgScroll)
    {
        // The bar moved.
        CharSelDesc->FirstVisible = static_cast<int>(CharScroll->GetValue());
        CharSelTime->FirstVisible = static_cast<int>(CharScroll->GetValue());
        return true;
    }
    return false;
}

void PageCharacterSelectSingle::RefillLists()
{
    // FUN_0056cb40. The same fill as Create's.
    CharSelDesc->ItemCount = 0;
    CharSelTime->ItemCount = 0;
    CharScroll->SetPage(20.0f);
    CharScroll->SetStep(1.0f);
    ShowWin3D(CharScroll);

    int Added = 0;
    unsigned const Count = G_LoginMgr.Characters.count();
    Entries.clear();
    if (Entries.capacity() < Count)
        Entries.reserve(Count);

    for (unsigned Index = 0; Index < Count; ++Index)
    {
        CCharacterSet::CharacterInfo* const Info = G_LoginMgr.CharacterAt(static_cast<int>(Index));
        if (Info == nullptr || Info->bIsRestart != 0)
            continue;
        FCharSelectSingleEntry Entry;
        Entry.Info = Info;
        wchar_t const* const EntryText = NnLoadSaveEntryText(Info);
        Entry.Name.assign(EntryText, wcslen(EntryText));
        wchar_t const* const TimeText =
            filetime_to_wstring(*reinterpret_cast<_FILETIME const*>(&Info->TimestampLow));
        Entry.Time.assign(TimeText, wcslen(TimeText));
        Entries.push_back(Entry);
        FCharSelectSingleEntry const& Last = Entries.back();
        CharSelDesc->AddItem(Last.Name.c_str(), InfoToItemData(Last.Info));
        CharSelTime->AddItem(Last.Time.c_str(), InfoToItemData(Last.Info));
        ++Added;
    }

    CharScroll->SetRange(0.0f, static_cast<float>(Added));
    CharScroll->SetPage(20.0f);
    CharScroll->SetStep(1.0f);
    CharScroll->SetValue(0.0f);
    UpdateScrollVisibility(); // inlined in retail
    CharSelDesc->FirstVisible = static_cast<int>(CharScroll->GetValue());
    CharSelTime->FirstVisible = static_cast<int>(CharScroll->GetValue());
}

void PageCharacterSelectSingle::Update()
{
    // FUN_0056ce30.
    Win3D::Update();

    // The row under the cursor in either list (CharSelTime's wins) pulses in both.
    int Hover = -1;
    if (CharSelDesc->HoverIndex != -1)
        Hover = CharSelDesc->HoverIndex;
    if (CharSelTime->HoverIndex != -1)
        Hover = CharSelTime->HoverIndex;
    if (Hover >= -1 && Hover < CharSelDesc->ItemCount)
        CharSelDesc->HighlightIndex = Hover;
    if (Hover >= -1 && Hover < CharSelTime->ItemCount)
        CharSelTime->HighlightIndex = Hover;

    // Not leaving and below 1.0 (DAT_005c89b8) slides in; leaving slides out at any value.
    bool const bBelowOne = SlideT < 1.0;
    if (bLeaving == 0 && bBelowOne)
    {
        // Frame time capped at 0.5 (DAT_005c75e8), 1.3 per second (DAT_005e2a58).
        double Dt = GetClientFrameTimeCapped();
        if (Dt > 0.5)
            Dt = 0.5;
        SlideT = Dt * 1.3 + SlideT;

        PlaceButtonImages(ButtonImages, SlideT);

        // At or below 0.0, or unordered, the boxes follow SlideT; above, everything arrives.
        if (SlideT > 0.0)
        {
            // UpdateScrollVisibility's test is inlined here.
            Vector const Home = {0.0f, 0.0f, 0.0f};
            BigBox->SetPosition(&Home);
            SmallBox->SetPosition(&Home);
            ShowWin3D(CharacterMoney);
            ShowWin3D(CharacterRank);
            ShowWin3D(CharacterLocation);
            ShowWin3D(CharacterShip);
            ShowWin3D(LoadCharacter);
            ShowWin3D(DeleteCharacter);
            ShowWin3D(MainMenu);
            if (CharSelDesc->ItemCount - 0x14 >= 1)
                ShowWin3D(CharScroll);
            else
                HideWin3D(CharScroll);
            ShowWin3D(CharSelDesc);
            ShowWin3D(CharSelTime);
            ShowWin3D(MoneyLabel);
            ShowWin3D(RankLabel);
            ShowWin3D(LocationLabel);
            ShowWin3D(ShipLabel);
            ShowWin3D(ScreenTitle);
            HideWin3D(ButtonImages[0]);
            HideWin3D(ButtonImages[1]);
            HideWin3D(ButtonImages[2]);
            SlideT = 2.0;
        }
        else
        {
            // -1.5 is DAT_005e2bb0.
            Vector const BigPosition = {static_cast<float>(SlideT * -1.5), 0.0f, 0.0f};
            BigBox->SetPosition(&BigPosition);
            Vector const SmallPosition = {0.0f, static_cast<float>(SlideT), 0.0f};
            SmallBox->SetPosition(&SmallPosition);
        }

        // -0.5 is DAT_005e2a50, -0.1 DAT_005d5070.
        if (SlideT > -0.5 && SlideT < -0.1)
            PlayUiSoundSlot(kUiSoundMotionSwish);
    }
    else if (bLeaving != 0)
    {
        // 1.5 per second (DAT_005d3c48), no frame-time cap.
        SlideT = SlideT - GetClientFrameTimeCapped() * 1.5;

        PlaceButtonImages(ButtonImages, SlideT);

        // Not below -0.5, or unordered, the boxes follow; below, the leave ends. Direction 1 asks
        // IntroFocus to leave the intro base, anything else is state 1.
        if (SlideT < -0.5)
        {
            HideWin3D(ButtonImages[0]);
            HideWin3D(ButtonImages[1]);
            HideWin3D(ButtonImages[2]);
            bLeaving = 0;
            if (LeaveDirection == 1)
                bLoadRequested = 1;
            else
                Result = kMenuStateMainMenu;
            SlideT = -2.0;
        }
        else
        {
            Vector const BigPosition = {static_cast<float>(SlideT * -1.5), 0.0f, 0.0f};
            BigBox->SetPosition(&BigPosition);
            Vector const SmallPosition = {0.0f, static_cast<float>(SlideT), 0.0f};
            SmallBox->SetPosition(&SmallPosition);
        }
    }

    // Once IntroFocus has left the intro base, the selected row's record becomes the chosen
    // character (loading screen first), state 0xc.
    if (bIntroBaseLeft == 0)
        return;
    bIntroBaseLeft = 0;
    bLoadRequested = 0;
    if (bCharacterChosen != 0)
        return;
    LoadingScreenSetClear(1);
    LoadingScreenStart();
    CCharacterSet::CharacterInfo const* const Chosen =
        ItemDataToInfo(CharSelDesc->GetItemData(CharSelDesc->SelectedIndex));
    if (Chosen == nullptr)
        return;
    bCharacterChosen = 1;
    CHARACTER_ID Local; // raw copy, then CHARACTER_ID::operator=, as retail
    memcpy(Local.Buffer, Chosen->Id.Buffer, sizeof(Local.Buffer));
    SelectedCharacter = Local;
    Result = kMenuStateEnterGame;
}

bool PageCharacterSelectSingle::OnEvent(unsigned MsgId)
{
    // LAB_0056d310 (this = the +0x80 face).
    if (MsgId == USER_CANCEL && bLeaving == 0)
    {
        Result = kMenuStateMainMenu;
        return true;
    }
    return UIPage::MasterWin::OnEvent(MsgId);
}

PageCharacterSelectSingle& GetPageCharacterSelectSingle()
{
    return G_PageCharacterSelectSingle;
}
