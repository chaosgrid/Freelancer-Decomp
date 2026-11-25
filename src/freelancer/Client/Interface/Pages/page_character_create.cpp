// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/Pages/page_character_create.h"

#include "NeuroNet/Chat/chat_init.h"            // ChatGetBaseName (FUN_0046b890), ChatGetShipArchName (FUN_0046b6c0)
#include "Common/engine_character.h"     // FEngineCharacterBody
#include "Core/globals.h"              // GetClientFrameTimeCapped (FUN_0042d680)
#include "Common/Fonts/ids_rdl.h"              // RdlClear, LoadIdsIntoRdl (FUN_0057da40), RdlRemoveLastParagraph
#include "Interface/Pages/intro_page_util.h"
#include "Network/login_mgr.h"            // G_LoginMgr (DAT_0067e7b8)
#include "Interface/menu_ui_init.h"         // PlayUiSoundSlot (FUN_00564570), PlayOrRewindUiSoundSlot (5645c0)
#include "NeuroNet/nn_error.h"             // NnErrorRunModal (FUN_00473cb0)
#include "Interface/Pages/page_character_select.h" // G_CharacterCreateSelection (DAT_0067bcf8)
#include "Space/Cameras/space_camera.h"         // AsCamera, SpaceCameraSetHalfFovX (FUN_0040f8c0)
#include "Space/space_scene_obj_draw.h" // SpaceCameraSetNearFar (FUN_0040fb60)
#include "Interface/tooltip_hover.h"        // FToolTipHoverInfo
#include "Interface/ui_rich_text_win.h"
#include "Interface/ui_text_edit.h"
#include "Interface/ui_text_msg_button.h"
#include "Interface/ui_win_deform.h"
#include "Interface/ui_win_object.h"
#include "Interface/win3d.h"                // Win3DGetCamera (FUN_0059da60), Win3DGetMouseRay (FUN_0059da70)
#include "Interface/win3d_immediate.h"      // Win3DVec3Add (FUN_00423b30)
#include "Interface/menu_enter_state.h"

#include "common_id.h"            // CreateID, ID_String
#include "common_newchar.h"       // NewChar::TheDB, FNewCharPackage, FNewCharPilot
#include "common_server_types.h"  // SCreateCharacterInfo
#include "common_text.h"
#include "common_universe.h"      // Archetype::GetShipID
#include "dacom.h"                // FDUMP

#include <stdint.h>               // uintptr_t
#include <string.h>
#include <wchar.h>                // wcscmp, wcscpy
#include <sstream>
#include <string>
#include <vector>

using namespace IntroPageUtil;

namespace
{
    // DAT_0067a890.
    PageCharacterCreate G_PageCharacterCreate(&G_CharacterCreateSelection.Info);

    // .rdata 0x005e1b1c -- the FDUMP source name.
    char const kCharCreateCpp[] = "E:\\FL\\Scratch\\Source\\Client\\Interface\\CharCreate.cpp";

    // .rdata 0x005e1b7c.
    wchar_t const kHigeneko[] = L"higeneko";
    // .data 0x00614344 -- the same word in hiragana (U+3072 U+3052 U+306D U+3053).
    wchar_t const kHigenekoKana[] = L"\x3072\x3052\x306d\x3053";

    // .data 0x00614338 -- the characters the name box refuses. Create passes a count of 5, so
    // the terminator is in the set too.
    wchar_t const kNameFilter[] = L" \t\r\n";

    // .rdata 0x005cef90 -- the name box's prefix text.
    wchar_t const kSpace[] = L" ";

    // The eight caption loads of Create: the load's result is not tested, the trailing paragraph
    // is cut whatever the load did.
    void LoadIdsAlwaysCut(RenderDisplayList& Rdl, unsigned Ids)
    {
        RdlClear(Rdl);
        LoadIdsIntoRdl(Ids, Rdl);
        RdlRemoveLastParagraph(Rdl);
    }

    void AppendStyle(UiRichTextWin* Win, unsigned short Style)
    {
        StyleNode const Node(Style);
        Win->AppendNode(Node);
    }

    void AppendJustify(UiRichTextWin* Win, TextJustify Mode)
    {
        JustifyNode const Node(Mode);
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

    void SetButtonOffset(UiTextMsgButton* Button, float const* Offset)
    {
        Button->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(Offset), 0);
    }
}

PageCharacterCreate& GetPageCharacterCreate()
{
    return G_PageCharacterCreate;
}

// ---------------------------------------------------------------------------------------------

PageCharacterCreate::PageCharacterCreate(SCreateCharacterInfo* NewSelection)
    : UIPage::MasterWin(Desc.GetPageData())
    , Result(0)
    , Selection(NewSelection)
    , SelectionFactionIndex(&G_CharacterCreateSelection.FactionIndex)
    , SlideT(0.0)
    , bLeaving(0)
    , LeaveDirection(0)
    , DataFrameHome{0.0f, 0.0f, 0.0f}
    , DataFrame(nullptr)
    , BaseCostume(nullptr)
    , Back(nullptr)
    , SelectShip(nullptr)
    , CharacterName(nullptr)
    , StartingDesc(nullptr)
    , Title(nullptr)
    , NameLabel(nullptr)
{
    // FUN_0055fea0. Retail leaves +0xd8..+0xfc unwritten; the static object starts zeroed, which
    // is what the initialisers above reproduce.
}

PageCharacterCreate::~PageCharacterCreate()
{
    // FUN_005600c0.
}

int PageCharacterCreate::GetResult() const
{
    return Result;
}

bool PageCharacterCreate::Load(char const* FullPath)
{
    if (!Desc.LoadFromFile(FullPath))
        return false;
    Create(nullptr, nullptr);
    return true;
}

bool PageCharacterCreate::HasResult() const
{
    return Result != 0;
}

void PageCharacterCreate::Destroy()
{
    // FUN_005602f0.
    UnregisterEvent(USER_CANCEL);
    UIPage::MasterWin::Destroy();
}

bool PageCharacterCreate::Create(Win3D* NewParent, void* /*Payload*/)
{
    // FUN_00560310. MasterWin::Create with no payload; its result is not tested.
    UIPage::MasterWin::Create(NewParent, nullptr);
    Result = 0;

    // In retail's lookup order.
    DataFrame = static_cast<UiWinObject*>(FindWidget("DataFrame"));
    BaseCostume = static_cast<UiWinDeform*>(FindWidget("BaseCostume"));
    Back = static_cast<UiTextMsgButton*>(FindWidget("Back"));
    SelectShip = static_cast<UiTextMsgButton*>(FindWidget("SelectShip"));
    CharacterName = static_cast<UiTextEdit*>(FindWidget("CharacterName"));
    StartingDesc = static_cast<UiRichTextWin*>(FindWidget("StartingDesc"));
    Title = static_cast<UiRichTextWin*>(FindWidget("Title"));
    NameLabel = static_cast<UiRichTextWin*>(FindWidget("NameLabel"));

    RegisterEvent(USER_CANCEL);

    CharacterName->GetToolTipHoverInfo()->IdsAlt = 0xad8;
    Back->GetToolTipHoverInfo()->IdsAlt = 0xadc;
    SelectShip->GetToolTipHoverInfo()->IdsAlt = 0xadd;

    LoadIdsAlwaysCut(BackText, 0x60029);
    LoadIdsAlwaysCut(SelectAnotherAccountText, 0x60028);
    LoadIdsAlwaysCut(NameLabelText, 0x6006c);
    LoadIdsAlwaysCut(BaseLabelText, 0x6006b);
    LoadIdsAlwaysCut(ShipLabelText, 0x6006e);
    LoadIdsAlwaysCut(LoadoutLabelText, 0x6006a);
    LoadIdsAlwaysCut(MoneyLabelText, 0x60063);
    LoadIdsAlwaysCut(TitleText, 0x60034);

    // NameLabel is not emptied first.
    AppendJustify(NameLabel, TextJustifyCenter);
    AppendStyle(NameLabel, kTextStyleBody);
    AppendRef(NameLabel, NameLabelText);

    Title->ClearContent();
    AppendStyle(Title, kTextStyleScreenTitle);
    AppendRef(Title, TitleText);

    BaseCostume->PoseZ = -6.0f;
    BaseCostume->PoseHeight = -0.9f;

    // The preview camera's half horizontal fov is the scene camera's (DAT_0067dc90) scaled by the
    // ratio of the viewport widths, computed in double and stored as float.
    FSpaceCamera* const Scene = Win3DGetCamera();
    int const SceneWidth = Scene->Viewport[2] - Scene->Viewport[0];
    FSpaceCamera* const Preview = &BaseCostume->Camera;
    int const* const PreviewRect = AsCamera(Preview)->GetViewport();
    int const PreviewWidth = PreviewRect[2] - PreviewRect[0];
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only Hor+: the scene's +0x98 is its angle on a 4:3 surface, so the width it spans
    // is the scene width over the Hor+ factor.
    SpaceCameraSetHalfFovX(
        Preview, static_cast<float>(static_cast<double>(PreviewWidth) *
                                    static_cast<double>(Scene->HalfFovXDeg) /
                                    (static_cast<double>(SceneWidth) / SpaceCameraHorPlusScale(Scene))));
#else
    SpaceCameraSetHalfFovX(
        Preview, static_cast<float>(static_cast<double>(PreviewWidth) *
                                    static_cast<double>(Scene->HalfFovXDeg) /
                                    static_cast<double>(SceneWidth)));
#endif
    SpaceCameraSetNearFar(Preview, 1.0f, 100.0f);

    RdlClear(Back->TextBodyList);
    {
        RDLRefNode const Node(BackText);
        AppendNodeClone(Back->TextBodyList, Node);
    }
    Back->ApplyCaptionLayout();

    StartingDesc->SetAutoScrollbar(true);

    ShowSelection(true);

    CharacterName->Activate();
    CharacterName->SetMaxLength(0x17);
    CharacterName->AddFilterChars(kNameFilter, 5);
    CharacterName->SetFixedStyle(true);

    {
        StyleNode const Node(kTextStyleBody);
        AppendNodeClone(CharacterName->FixedStyle, Node);
    }
    {
        JustifyNode const Node(TextJustifyCenter);
        AppendNodeClone(CharacterName->Prefix, Node);
    }
    {
        TextNode const Node(kSpace, -1);
        AppendNodeClone(CharacterName->Prefix, Node);
    }

    CharacterName->SetDrawFrame(true);
    CharacterName->SetFrameColor(ChromeColor());
    CharacterName->SetAlignMode(2);
    CharacterName->SetLayoutFlag(false);

    SlideT = -1.0;
    LeaveDirection = 0;
    bLeaving = 0;
    DataFrameHome[0] = DataFrame->Position[0];
    DataFrameHome[1] = DataFrame->Position[1];
    DataFrameHome[2] = DataFrame->Position[2];

    // Hidden until the slide ends.
    HideWin3D(CharacterName);
    HideWin3D(BaseCostume);
    HideWin3D(NameLabel);
    HideWin3D(StartingDesc);
    HideWin3D(Title);

    Back->SetSkipUpload(true);
    SelectShip->SetSkipUpload(true);
    return true;
}

void PageCharacterCreate::ShowSelection(bool bWithCostume)
{
    // FUN_00560c00 (RET 4).
    CharacterName->ClearText();
    CharacterName->SetText(Selection->Name, nullptr);

    StartingDesc->ClearContent();
    AppendStyle(StartingDesc, kTextStyleList);
    AppendRef(StartingDesc, BaseLabelText);
    AppendParagraph(StartingDesc);
    AppendStyle(StartingDesc, kTextStyleBody);
    AppendJustify(StartingDesc, TextJustifyCenter);
    AppendRef(StartingDesc, *ChatGetBaseName(Selection->BaseId));
    AppendParagraph(StartingDesc);
    AppendParagraph(StartingDesc);

    FNewCharPackage const* const Package = NewChar::TheDB->FindPackage(Selection->PackageId);
    if (Package != nullptr)
    {
        // Retail formats the money through a basic_stringstream<unsigned short>.
        AppendStyle(StartingDesc, kTextStyleList);
        AppendRef(StartingDesc, MoneyLabelText);
        AppendParagraph(StartingDesc);
        AppendStyle(StartingDesc, kTextStyleBody);
        AppendJustify(StartingDesc, TextJustifyCenter);
        {
            std::wstringstream Money;
            Money << Package->Money << std::ends;
            {
                std::wstring const Text = Money.str();
                TextNode const Node(Text.c_str(), -1);
                StartingDesc->AppendNode(Node);
            }
            AppendParagraph(StartingDesc);
            AppendParagraph(StartingDesc);
        }

        AppendStyle(StartingDesc, kTextStyleList);
        AppendRef(StartingDesc, ShipLabelText);
        AppendParagraph(StartingDesc);
        AppendStyle(StartingDesc, kTextStyleBody);
        AppendJustify(StartingDesc, TextJustifyCenter);
        ID_String ShipNickname;
        ShipNickname.Id = CreateID(Package->Ship.c_str());
        AppendRef(StartingDesc, *ChatGetShipArchName(Archetype::GetShipID(ShipNickname)));
        AppendParagraph(StartingDesc);
        AppendParagraph(StartingDesc);

        // A failed load ends the text here.
        RdlClear(PackageDescText);
        if (LoadIdsIntoRdl(static_cast<unsigned>(Package->StridDesc), PackageDescText))
        {
            AppendStyle(StartingDesc, kTextStyleBody);
            AppendJustify(StartingDesc, TextJustifyLeft);
            AppendRef(StartingDesc, PackageDescText);
        }
    }

    if (!bWithCostume)
        return;

    FNewCharPilot const* const Pilot = NewChar::TheDB->FindPilot(Selection->PilotId);
    if (Pilot == nullptr)
    {
        FDUMP(kFdumpWarning, "%s(%d) : WARNING:General:%s", kCharCreateCpp, 0x19d,
              "Failed to retrieve pilot information.\n");
        return;
    }

    BaseCostume->SetCostume(Pilot->Body.c_str());

    FEngineCharacterBody& Body = BaseCostume->Body;
    for (std::vector<std::string>::const_iterator It = Pilot->BodyAnims.begin();
         It != Pilot->BodyAnims.end(); ++It)
    {
        Body.PlayAnimation(It->c_str(), 0, 0.0f, 1.0f, 2);
    }

    // 0.1 s of animation in steps of at most 0.2f (DAT_005d043c; 0.2f when unordered). The step
    // body is FUN_004cba60's. The loop tests the double difference, before it is rounded to float.
    float Remaining = 0.1f;
    for (;;)
    {
        float const Step = (Remaining < 0.2f) ? Remaining : 0.2f;
        Body.Advance(Step);
        double const Left = static_cast<double>(Remaining) - static_cast<double>(Step);
        Remaining = static_cast<float>(Left);
        if (!(Left > 0.0))
            break;
    }

    // FUN_004c9a30's body.
    Body.StopAnimations();

    for (std::vector<std::string>::const_iterator It = Pilot->BodyAnims.begin();
         It != Pilot->BodyAnims.end(); ++It)
    {
        Body.PlayAnimation(It->c_str(), 0, 0.0f, 1.0f, 2);
    }
}

void PageCharacterCreate::ReadNameIntoSelection()
{
    CharacterName->GetText(Selection->Name, 0x18);
    // Easter egg: "higeneko" becomes its hiragana and the box is refilled.
    if (wcscmp(Selection->Name, kHigeneko) == 0)
    {
        wcscpy(Selection->Name, kHigenekoKana);
        CharacterName->ClearText();
        CharacterName->SetText(Selection->Name, nullptr);
    }
}

void PageCharacterCreate::Confirm()
{
    // FUN_00561560.
    ReadNameIntoSelection();

    if (Selection->Name[0] == L'\0')
        return;

    int const Code = G_LoginMgr.CreateNewCharacter(*Selection);
    if (Code == 1)
    {
        LeaveDirection = 1;
        bLeaving = 1;
        BeginLeave();
        return;
    }
    if (Code == 4)
    {
        // Refused: the page stays.
        NnErrorRunModal(0x738, false, nullptr);
        return;
    }
    LeaveDirection = 2;
    bLeaving = 1;
    BeginLeave();
}

bool PageCharacterCreate::ProcessMessage(unsigned Msg, unsigned A, unsigned /*B*/)
{
    // LAB_00561630 (RET 0xc).
    bool bBack = false;
    bool bConfirm = false;
    if (Msg == kWinMsgCommand)
    {
        // A button click; the name is read back whichever button it was.
        PlayOrRewindUiSoundSlot(0);
        ReadNameIntoSelection();
        bBack = (A == 0x300c);
        bConfirm = (A == 0x300d);
    }
    else
    {
        bBack = (Msg == kWinMsgEscape);
        bConfirm = (Msg == kWinMsgEnter);
    }

    if (bBack)
    {
        LeaveDirection = -1;
        BeginLeave();
        return true;
    }
    if (bConfirm)
    {
        Confirm();
        return true;
    }
    return false;
}

bool PageCharacterCreate::OnEvent(unsigned MsgId)
{
    // LAB_00561700 (RET 4, this = the +0x80 face).
    if (MsgId == USER_CANCEL)
    {
        LeaveDirection = -1;
        BeginLeave();
        return true;
    }
    return false;
}

bool PageCharacterCreate::MouseEvent(FWin3DMouseEvent const* Event)
{
    // LAB_00561730 (RET 4): a single press (any button) drops the name box's focus and gives it back
    // when the mouse ray hits the box. Always false.
    if (Event->Action == kWin3DMousePress)
    {
        CharacterName->Deactivate(false);
        float Ray[3];
        if (CharacterName->HitTest(Win3DGetMouseRay(Ray)))
            CharacterName->Activate();
    }
    return false;
}

void PageCharacterCreate::BeginLeave()
{
    // FUN_00561b20.
    bLeaving = 1;
    SlideT = 0.0;

    HideWin3D(CharacterName);
    HideWin3D(BaseCostume);
    HideWin3D(NameLabel);
    HideWin3D(StartingDesc);
    HideWin3D(Title);

    Back->bInteractive = 0;
    SelectShip->bInteractive = 0;

    Back->SetSkipUpload(true);
    SelectShip->SetSkipUpload(true);
    CharacterName->SetToolTipEnabled(false);
    Back->SetToolTipEnabled(false);
    SelectShip->SetToolTipEnabled(false);

    PlayUiSoundSlot(0x1b);
}

void PageCharacterCreate::Update()
{
    // LAB_00561790.
    // SelectShip is enabled only while the name box holds text.
    unsigned char const OldFlags = SelectShip->Flags;
    unsigned char const bHasName = (CharacterName->TextDisplay.get_element_count() != 0) ? 1 : 0;
    SelectShip->Flags = static_cast<unsigned char>((OldFlags & 0xfe) | bHasName);
    SelectShip->bInteractive = static_cast<unsigned char>(SelectShip->Flags & 1);

    // The compares below follow retail's x87 tests: an unordered SlideT skips the slide-in
    // (1.0, DAT_005c89b8), counts as arrived (0.0) and plays no sound.
    if (SlideT <= 1.0 && bLeaving == 0)
    {
        // A frame time of 0.5 or more (DAT_005c75e8) does not advance the slide; rate DAT_005d3c48.
        double const Dt = GetClientFrameTimeCapped();
        if (Dt < 0.5)
            SlideT = Dt * 1.5 + SlideT;

        if (SlideT < 0.0)
        {
            float const BackOffset[3] = {static_cast<float>(SlideT), 0.0f, 0.0f};
            SetButtonOffset(Back, BackOffset);
            float const ShipOffset[3] = {static_cast<float>(-SlideT), 0.0f, 0.0f};
            SetButtonOffset(SelectShip, ShipOffset);
            float const FrameOffset[3] = {0.0f, static_cast<float>(SlideT), 0.0f};
            float FramePosition[3];
            Win3DVec3Add(FramePosition, DataFrameHome, FrameOffset);
            DataFrame->SetPosition(FramePosition);
        }
        else
        {
            float const Zero[3] = {0.0f, 0.0f, 0.0f};
            SetButtonOffset(Back, Zero);
            SetButtonOffset(SelectShip, Zero);
            DataFrame->SetPosition(DataFrameHome);
            ShowWin3D(CharacterName);
            ShowWin3D(BaseCostume);
            ShowWin3D(NameLabel);
            ShowWin3D(StartingDesc);
            ShowWin3D(Title);
            Back->SetSkipUpload(false);
            SelectShip->SetSkipUpload(false);
            CharacterName->SetToolTipEnabled(true);
            Back->SetToolTipEnabled(true);
            SelectShip->SetToolTipEnabled(true);
            SlideT = 2.0;
        }

        // Sound strictly between -0.8 (DAT_005d5078) and -0.1 (DAT_005d5070).
        if (SlideT > -0.8 && SlideT < -0.1)
            PlayUiSoundSlot(0x1b);
    }

    if (bLeaving != 0 && LeaveDirection != 0)
    {
        double const Dt = GetClientFrameTimeCapped();
        SlideT = SlideT - Dt * 1.5;
        float const Slide = static_cast<float>(SlideT);

        float const BackOffset[3] = {Slide, 0.0f, 0.0f};
        SetButtonOffset(Back, BackOffset);
        float const ShipOffset[3] = {static_cast<float>(-SlideT), 0.0f, 0.0f};
        SetButtonOffset(SelectShip, ShipOffset);

        float const FramePosition[3] = {DataFrameHome[0], Slide + DataFrameHome[1],
                                        DataFrameHome[2]};
        DataFrame->SetPosition(FramePosition);

        // -0.9 is DAT_005e1b90; unordered keeps sliding.
        if (SlideT <= -0.9)
        {
            // Retail's unsigned switch: -1 -> state 8, other values above 3 -> none;
            // 3 -> NN_Error 0x738 then state 8; 1 -> state 8; 2 -> state 5.
            unsigned const Direction = static_cast<unsigned>(LeaveDirection);
            if (Direction > 3)
            {
                if (LeaveDirection == -1)
                    Result = 8;
            }
            else if (Direction == 3)
            {
                NnErrorRunModal(0x738, false, nullptr);
                Result = 8;
            }
            else if (Direction == 1)
            {
                Result = 8;
            }
            else if (Direction == 2)
            {
                Result = kMenuStateServerSelect;
            }
            bLeaving = 0;
        }
    }

    // Retail inlines Win3D's Update here.
    Win3D::Update();
}

void PageCharacterCreate::Draw()
{
    // FUN_00566500.
    Win3D::Draw();
}
