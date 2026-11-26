// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail CharCreate.cpp PageCharacterCreate (`E:\FL\Scratch\Source\Client\Interface\
// CharCreate.cpp`, .rdata 0x005e1b1c) : UIPage::MasterWin -- the multiplayer "new character"
// page (PAGE_CharacterCreate.page: SelectShip, Back, Title, DataFrame, CharacterName,
// BaseCostume, StartingDesc, NameLabel). One static instance, DAT_0067a890 (static init
// LAB_00573580 -> ctor FUN_0055fea0(&DAT_0067bcf8), atexit LAB_005735a0 -> dtor FUN_005600c0).
// vftables: main 0x005e1a4c (slot 0 FUN_005600a0 scalar deleting dtor), IUIPage face (+0x7c)
// 0x005e1a34, Event::Handler face (+0x80) 0x005e1a2c.
//
// IntroFocus EnterState case 14 releases the active page and loads this one through Load, or
// moves to state 9 when the file does not load. IntroFocus::Update moves to GetResult() once
// HasResult(); IntroFocus Exit calls Destroy.
//
// The page shows the pick PageCharacterSelect wrote into DAT_0067bcf8 (G_CharacterCreateSelection):
// the home base, the package's money, ship and description, and the pilot's body on a
// UIWinDeform posed with its body.anim scripts. The typed name goes into the pick's Name and
// CLoginMgr::CreateNewCharacter (FUN_005aa750) sends it.
//
// Member offsets are retail's (MasterWin is +0x00..+0x8b). The port's MasterWin has no retail
// byte layout (see page_options.h), so the members keep only the order. +0xe8..+0xf3 are never
// accessed and are not held.

#include "common_text.h" // RenderDisplayList
#include "Interface/ui_page.h"
#include "Interface/ui_page_desc.h"

struct SCreateCharacterInfo;
class UiRichTextWin;
class UiTextEdit;
class UiTextMsgButton;
class UiWinDeform;
class UiWinObject;

// The button commands PageCharacterCreate::ProcessMessage receives with kWinMsgCommand.
enum ECharacterCreateCommand
{
    kCharCreateCmdBack = 0x300c,
    kCharCreateCmdSelectShip = 0x300d
};

class PageCharacterCreate : public UIPage::MasterWin
{
public:
    // FUN_0055fea0 (RET 4) -- Selection = &DAT_0067bcf8 (SCreateCharacterInfo, then its faction
    // index at +0x40).
    explicit PageCharacterCreate(SCreateCharacterInfo* NewSelection);
    // FUN_005600c0.
    ~PageCharacterCreate() override;

    // slot 9 FUN_00560310 (RET 8).
    bool Create(Win3D* NewParent, void* Payload) override;
    // slot 10 FUN_005602f0.
    void Destroy() override;
    // slot 11 LAB_00561790.
    void Update() override;
    // slot 13 FUN_00566500 -- JMP FUN_0059e5c0 (Win3D's sorted children).
    void Draw() override;
    // slot 19 LAB_00561730 (RET 4).
    bool MouseEvent(FWin3DMouseEvent const* Event) override;
    // slot 23 LAB_00561630 (RET 0xc).
    bool ProcessMessage(unsigned Msg, unsigned A, unsigned B) override;
    // Event::Handler face (+0x80) slot 0 LAB_00561700 (RET 4).
    bool OnEvent(unsigned MsgId) override;

    // FUN_005602a0.
    int GetResult() const;
    // FUN_005602b0 (RET 4) -- the descriptor loaded from FullPath; on success Create(0, 0),
    // whose result is not tested, and true.
    bool Load(char const* FullPath);
    // FUN_005602e0.
    bool HasResult() const;

    FUiPageDesc Desc;                    // +0x8c
    int Result;                          // +0xb4 -- the next IntroFocus state (5 / 8)
    SCreateCharacterInfo* Selection;     // +0xb8 -- &DAT_0067bcf8
    int* SelectionFactionIndex;          // +0xbc -- &DAT_0067bd38; no reader in CharCreate.cpp
    RenderDisplayList BackText;          // +0xc0 -- IDS 0x60029, Back's text
    // +0xcc -- IDS 0x60028 "Select Another Account"; loaded, never referenced
    RenderDisplayList SelectAnotherAccountText;
    double SlideT;                       // +0xd8 -- Create -1.0, 0..2 in; Leave 0, down to -0.9
    unsigned char bLeaving;              // +0xe0
    int LeaveDirection;                  // +0xe4 -- -1 back, 1 created, 2 failed (0 = none)
    Vector DataFrameHome;                // +0xf4 -- DataFrame's position at Create
    UiWinObject* DataFrame;              // +0x100 -- "DataFrame"
    UiWinDeform* BaseCostume;            // +0x104 -- "BaseCostume"
    UiTextMsgButton* Back;               // +0x108 -- "Back" (command 0x300c)
    UiTextMsgButton* SelectShip;         // +0x10c -- "SelectShip" (command 0x300d)
    UiTextEdit* CharacterName;           // +0x110 -- "CharacterName"
    UiRichTextWin* StartingDesc;         // +0x114 -- "StartingDesc"
    UiRichTextWin* Title;                // +0x118 -- "Title"
    UiRichTextWin* NameLabel;            // +0x11c -- "NameLabel"
    // +0x120 -- constructed and destroyed only; nothing in the exe reads or writes it.
    RenderDisplayList Unused120;
    RenderDisplayList NameLabelText;     // +0x12c -- IDS 0x6006c
    RenderDisplayList BaseLabelText;     // +0x138 -- IDS 0x6006b
    RenderDisplayList ShipLabelText;     // +0x144 -- IDS 0x6006e
    RenderDisplayList LoadoutLabelText;  // +0x150 -- IDS 0x6006a "LOADOUT:"; never referenced
    RenderDisplayList MoneyLabelText;    // +0x15c -- IDS 0x60063
    RenderDisplayList PackageDescText;   // +0x168 -- the package's strid_desc
    RenderDisplayList TitleText;         // +0x174 -- IDS 0x60034

private:
    // FUN_00560c00 (RET 4) -- the name box and StartingDesc from the pick; with bWithCostume the
    // pilot's body on BaseCostume, settled for 0.1 s and posed.
    void ShowSelection(bool bWithCostume);
    // FUN_00561560 -- the typed name into the pick ("higeneko" becomes its hiragana), then
    // CLoginMgr::CreateNewCharacter and the leave.
    void Confirm();
    // FUN_00561b20 -- start the leave slide.
    void BeginLeave();
    // The name read-back retail inlines in ProcessMessage and Confirm: GetText into the pick's
    // Name (0x18 chars), and the easter egg.
    void ReadNameIntoSelection();

    PageCharacterCreate(PageCharacterCreate const&) = delete;
    PageCharacterCreate& operator=(PageCharacterCreate const&) = delete;
};

// DAT_0067a890.
PageCharacterCreate& GetPageCharacterCreate();
