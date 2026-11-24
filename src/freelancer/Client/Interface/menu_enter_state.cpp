// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/menu_enter_state.h"

#include "Interface/Pages/character_select_equip.h"
#include "Core/client_frame_tick.h" // FUN_00424f30 / FUN_00425080 / FUN_00425140, FUN_0041b2e0
#include "NeuroNet/Chat/chat_init.h"    // ChatShowMsgWin (FUN_0046a4a0)
#include "NeuroNet/Chat/chat_msg_win.h" // ChatCreateMsgWin (FUN_0046b580)
#include "Network/connect_flow.h"
#include "Network/connect_lists.h" // PreConnectReset (FUN_004c4a80)
#include "dacom.h" // FDUMP
#include "Core/dataconfig_progress.h" // GetIntroConfigState (bDrawScripts, DAT_00614a9c)
#include "Common/Graphics/fade_to_black.h" // SetFadeImmediate / SetFadeTarget (FUN_00412070 / FUN_00412050)
#include "Common/Fonts/font_imp.h"
#include "GameFlow/Thorn/gf_script_object.h" // G_GfScriptManager (DAT_0066d2d0)
#include "Core/globals.h"
#include "GameFlow/menu_universe_base.h" // LeaveClientLocationBase (FUN_0043b3e0)
#include "Common/Fonts/ids_strings.h"
#include "Common/input_manager.h"
#include "Interface/loading_screen.h"
#include "Network/login_mgr.h"
#include "Common/Audio/menu_sound_arch.h" // TickClientSoundMixer (FUN_00428040)
#include "NeuroNet/nn_preferences.h"  // NnPreferences::bClosed (+0x32c)
#include "NeuroNet/nn_static_control.h"
#include "NeuroNet/nn_text_control.h"
#include "Interface/Pages/page_account_select.h" // PageAccountSelect (DAT_0067aa10)
#include "Interface/Pages/page_character_create.h" // PageCharacterCreate (DAT_0067a890)
#include "Interface/Pages/page_character_select.h" // PageCharacterSelect (DAT_0067b180), G_NoCharacterId
#include "Interface/Pages/page_character_select_single.h" // PageCharacterSelectSingle (DAT_0067ae10)
#include "Interface/Pages/page_server_select.h"  // PageServerSelect (DAT_0067ab20)
#include "Interface/Pages/page_options.h"
#include "Space/player_spawn.h" // ResetMissionOfferAnswerRetail (FUN_004c55b0)
#include "GameFlow/room_camera.h"  // FRoomCamera (FUN_0041a140's receiver)
#include "GameFlow/room_control.h" // GetRoomObjectList (FUN_005593c0), FRoomObject
#include "Network/server_connect.h"
#include "Common/Audio/sound_config.h" // SetClientAudioMode (FUN_00428030)
#include "Space/space_letterbox.h" // UpdateRoomLetterbox (FUN_0041a140)
#include "Interface/text_win.h"
#include "Interface/Pages/type_select.h"
#include "Interface/ui_page.h"
#include "Interface/ui_page_desc.h"
#include "Interface/win3d.h"           // Win3DDrawAll (FUN_0059e300)
#include "Interface/win3d_engine_submit.h"
#include "Interface/win3d_immediate.h" // Win3DSetViewport / Win3DSetPipelineClearColor
#include "Interface/win3d_render.h"    // ClearWin3DBuffers (FUN_004215e0)
#include "Interface/win_factory.h"
#include "Interface/wincontrol_base.h"

#include "common_net.h" // CPlayerAccount (cases 2 and 4)
#include "common_misc.h" // SinglePlayer, SetMultiPlayer, GetFullFilename

#include <math.h>
#include <process.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

#include <list>

namespace
{
    // .rdata:005e2724 -- the source name of the driver's frame.
    char const kIntroCpp[] = "E:\\FL\\Scratch\\Source\\Client\\Interface\\Intro.cpp";

    // EnterState case 1 column: y = Y0 - i * YStep.
    float const kMainColumnPosX = -0.330000013f; // 0xbea8f5c3
    float const kMainColumnPosZ = -1.0f;         // 0xbf800000
    float const kMainColumnPosY0 = 0.155000001f; // DAT_005e3210
    float const kMainColumnPosYStep = 0.081500001f; // DAT_005e3214
    float const kMainColumnBoxWidth = 0.25f;
    float const kFontLineScale = 0.002f;         // DAT_005e2554

    // The "Version" TextControl position.
    float const kVersionPosX = 0.319999993f;  // 0x3ea3d70a
    float const kVersionPosY = -0.349999994f; // 0xbeb33333
    float const kVersionPosZ = -1.0f;         // 0xbf800000

    char const kFrontButtonMesh[] = "Interface\\NeuroNet\\Inventory\\FRONT_button.cmp";
    char const kShortOptionsBox[] = "Interface\\NeuroNet\\Inventory\\FRONT_shortoptions_box.cmp";
    char const kInfoWindowMesh[] = "Interface\\LoadSave\\FRONT_infowindow.cmp";

    char const kPageServerSelect[] = "Interface\\intro\\PAGE_ServerSelect.page";
    char const kPageAccountSelect[] = "Interface\\intro\\PAGE_AccountSelect.page";
    char const kPageCharacterSelect[] = "Interface\\intro\\PAGE_CharacterSelect.page";
    char const kPageCharacterCreate[] = "Interface\\intro\\PAGE_CharacterCreate.page";
    char const kPageCharSelectSingle[] = "Interface\\intro\\PAGE_CharSelectSingle.page";

    // The five TextMsgButtons: caption IDS and ProcessMessage command.
    struct FMainColumnSlot
    {
        unsigned LabelIds;
        int Command;
    };

    FMainColumnSlot const kMainColumnSlots[5] = {
        {0x4f4u, 0x10},
        {0x4f5u, 0x03},
        {0x4f6u, 0x04},
        {0x4f8u, 0x0a},
        {0x4f9u, 0x0b},
    };

    // DAT_0067c200 request / DAT_0067c1fc latch.
    int GIntroModeRequest = 0; // DAT_0067c200
    int GIntroModeLatch = 0;   // DAT_0067c1fc

    // DAT_0067c204 -- set by case 5, cleared when state 5 is left.
    unsigned char GServerSelectActive = 0;
    // DAT_0067c20c -- a Win3D the driver deletes when state 10 is left while it is inactive.
    // Nothing in retail writes a non-null value (its only references are two reads and a clear
    // in FUN_00573c00, and its address is never taken), so it stays null.
    Win3D* GOptionsChild_67c20c = nullptr;

    // The intro base teardown the driver inlines three times (Update's character-select polls
    // and EnterState case 12). The last store clears LocationController +0x5a (DAT_00668762),
    // which LoadLocationBase reads through `this`.
    void LeaveIntroBase()
    {
        LeaveClientLocationBase();
        G_GfScriptManager.Shutdown();
        SetPlayerBaseIdRetail(0);
        SetPlayerLocationIdRetail(0);
        SetFadeImmediate(1.0f);
        SetFadeTarget(0.0f, static_cast<float>(1.0 / static_cast<double>(GetFadeDuration())));
        SetClientLocationNoServerNotify(false);
    }

    // Retail inlines this at each page change; `this` is never tested.
    void ReleaseActivePageInternal(FMenuNavState* Nav)
    {
        if (Nav->ActivePage != nullptr)
        {
            Nav->ActivePage->Destroy();
            Nav->ActivePage = nullptr;
        }
    }

    // Case 18's five chrome statics are members of PageCharacterSelectSingle (DAT_0067ae10,
    // +0xf4..+0x104); case 18 stores them absolutely (DAT_0067af04..DAT_0067af14), the page's
    // methods read them through `this`. Ghidra leaves this range undisassembled.
    struct FCase18Static
    {
        char const* Nickname;
        char const* Mesh;
        float Offset[3];
    };

    FCase18Static const kCase18Statics[5] = {
        {"bImg", kFrontButtonMesh, {-0.34799999f, 0.19499999f, -1.0f}},
        {"bImg", kFrontButtonMesh, {-0.34900001f, 0.11050000f, -1.0f}},
        {"bImg", kFrontButtonMesh, {-0.34900001f, 0.029999999f, -1.0f}},
        {"bigBox", kShortOptionsBox, {0.019999999f, -0.019999999f, -0.98000002f}},
        {"smallBox", kInfoWindowMesh, {-0.34999999f, -0.19699999f, -1.0f}},
    };

    Win3D*& Case18StaticSlot(PageCharacterSelectSingle& Page, int Index)
    {
        if (Index < 3)
            return Page.ButtonImages[Index];
        if (Index == 3)
            return Page.BigBox;
        return Page.SmallBox;
    }

    // Built before the page is loaded. Init's flags: NoWireRender, NoColoring and NoClearRender
    // set, the rest clear. The three "bImg" skip the Init on a null create; "bigBox" and
    // "smallBox" have no null test, as retail. Every entry into case 18 builds them again (no
    // latch; the page's Destroy on the way out destroys the old ones as its children).
    void EnterCase18Chrome(PageCharacterSelectSingle& Page)
    {
        float const Zero[3] = {0.0f, 0.0f, 0.0f};
        for (int Index = 0; Index < 5; ++Index)
        {
            FCase18Static const& Slot = kCase18Statics[Index];
            Win3D* const Created = WinFactoryCreate(Slot.Nickname, "StaticControl", &Page, nullptr);
            Case18StaticSlot(Page, Index) = Created;
            if (Index < 3 && Created == nullptr)
                continue;
            static_cast<NnStaticControl*>(Created)->Init(Slot.Mesh, Slot.Offset, nullptr, Zero, 0, 0,
                                                         0, 1, 1, 0, 1, 0, 0, 0, Zero);
        }
    }
}

void MenuEnterStateReleaseActivePage(FMenuNavState* Nav)
{
    ReleaseActivePageInternal(Nav);
}

bool MenuNavStartSinglePlayer(FMenuNavState* Nav)
{
    // FUN_005739c0 (thiscall on the IntroFocus).
    SetMultiPlayer(false);
    Nav->bFromMultiplayer = 0;
    ChatDestroyMsgWin();
    G_LoginMgr.LoginName = L"SinglePlayer";
    G_LoginMgr.LoginPassword = L"";
    G_LoginMgr.ServerPassword = L"";
    if (!ConnectToServer(g_display.ini_path.Text))
        return false;
    if (!LoginToServer())
        return false;
    G_LoginMgr.Reset();
    G_LoginMgr.ReloadFactions();
    return G_LoginMgr.RequestCharacterInfo() != 0;
}

namespace
{
    // EnterState case 1: the main menu.
    void EnterCase1(FMenuNavState* Nav)
    {
        G_LoginMgr.DestroyGun();

        if (Nav->bFromMultiplayer != 0)
        {
            // Back from multiplayer: restart the single-player session (retail inlines
            // MenuNavStartSinglePlayer here).
            ShutdownServerConnection();
            ResetMissionOfferAnswerRetail();
            G_LoginMgr.Reset();
            if (!MenuNavStartSinglePlayer(Nav))
                Nav->CurrentState = 0xd;
        }

        SetClientMouseCursorDrawFlag(1);
        if (Nav->ActivePage != nullptr)
        {
            Nav->ActivePage->Destroy();
            Nav->ActivePage = nullptr;
        }

        TypeSelect& Column = GetTypeSelect();
        Column.NextState = 0;
        Column.bAnimating = 0;
        Column.PendingCommand = 0;
        Column.Create(nullptr, nullptr);

        // The caption box height is the "Normal" font's line height (unsigned) times 0.002.
        IFont* const NormalFont = FindMenuFontImp("Normal");
        float const BoxHeight =
            static_cast<float>(static_cast<unsigned>(NormalFont->GetLineHeight())) *
            kFontLineScale;

        for (int Index = 0; Index < 5; ++Index)
        {
            Win3D* const Created = WinFactoryCreate("bImg", "StaticControl", &Column, nullptr);
            Column.Statics[Index] = Created;
            if (Created == nullptr)
                continue;
            float const PosY =
                kMainColumnPosY0 - static_cast<float>(Index) * kMainColumnPosYStep;
            // The same Init flags as case 18's statics.
            float const Offset[3] = {kMainColumnPosX, PosY, kMainColumnPosZ};
            float const ZeroVector[3] = {0.0f, 0.0f, 0.0f};
            static_cast<NnStaticControl*>(Created)->Init(kFrontButtonMesh, Offset, nullptr,
                                                         ZeroVector, 0, 0, 0, 1, 1, 0, 1, 0, 0,
                                                         0, ZeroVector);
        }

        for (int Index = 0; Index < 5; ++Index)
        {
            wchar_t Caption[0x40];
            GetIdsStringWWithRawFallback(kMainColumnSlots[Index].LabelIds, Caption, 0x40);
            Column.Buttons[Index].Init(&Column, Caption, kMainColumnBoxWidth, BoxHeight,
                                       kMainColumnSlots[Index].Command);
        }
        for (int Index = 0; Index < 5; ++Index)
        {
            Column.Buttons[Index].HAlign = 1; // centred
            Column.Buttons[Index].SetFlagsE4E5(1, 1);
        }
        Column.AnimTime = -2.0;

        Win3D* const VersionWin = WinFactoryCreate("Version", "TextControl", &Column, nullptr);
        Column.Version = VersionWin;
        if (VersionWin != nullptr)
        {
            // The text comes from property 0x1d below.
            NnTextControl* const VersionText = static_cast<NnTextControl*>(VersionWin);
            float const VersionPos[3] = {kVersionPosX, kVersionPosY, kVersionPosZ};
            VersionText->Init("Normal", VersionPos, nullptr, kMainColumnBoxWidth, BoxHeight, 1.0f,
                              0u, 1, 0, 0);

            wchar_t Format[0x100];
            GetIdsStringWWithRawFallback(0x4f7u, Format, 0x100);
            // The version word is the high half of g_client.packed_version (DAT_0067ecba).
            unsigned const VersionWord =
                (static_cast<unsigned>(g_client.packed_version) >> 16) & 0xffffu;
            wchar_t Text[0x100];
            swprintf(Text, 0x100, Format, (VersionWord >> 8) & 0xffu, VersionWord & 0xffu);
            VersionText->SetProperty(0x1d, reinterpret_cast<uintptr_t>(Text), 0);
        }

        Column.RegisterEvent(0xf); // USER_CANCEL
        Nav->ActivePage = &Column;
        GetPageCharacterSelect().CurrentCharacter = G_NoCharacterId;
    }
}

void EnterMenuState(FMenuNavState* Nav, int State)
{
    // FUN_005743a0 (thiscall, RET 4). No test of `this`. Retail also sprintf()s
    // "EnterState %d\n" into a local nobody reads; not ported.
    RefreshLoadingProgress();

    switch (State)
    {
    case 1:
        EnterCase1(Nav);
        break;

    case 2:
        if (SinglePlayer())
        {
            Nav->CurrentState = 18;
        }
        else
        {
            LoadingScreenStart();
            // LoadAccount's result is not tested; only VerifyAccount decides.
            CPlayerAccount Account;
            Account.LoadAccount();
            if (!Account.VerifyAccount())
            {
                Nav->CurrentState = 5;
                break;
            }
            // CLoginMgr +0x1a8 -- LoginToServer sends it.
            G_LoginMgr.bCreateAccount = 1;
            // The login name is the account's signature of the current server's +0x64 string.
            G_LoginMgr.LoginName = std::wstring(
                Account.GetServerSignature(G_LoginMgr.GetCurrentServer().ServerSig.c_str())
                    .c_str());
            G_LoginMgr.LoginPassword = std::wstring(L"");
            if (LoginToServer())
            {
                G_LoginMgr.Reset();
                Nav->CurrentState = 9;
            }
            else
            {
                Nav->CurrentState = 4;
            }
        }
        break;

    case 3:
        Nav->CurrentState = 2;
        break;

    case 4:
        // Multiplayer.
        ShutdownServerConnection();
        ResetMissionOfferAnswerRetail();
        G_LoginMgr.Reset();
        SetMultiPlayer(true);
        Nav->bFromMultiplayer = 1;
        ChatCreateMsgWin();
        Nav->CurrentState = 5;
        {
            // No account or a failed check -> state 6, the account page.
            CPlayerAccount Account;
            if (!Account.LoadAccount() || !Account.VerifyAccount())
                Nav->CurrentState = 6;
        }
        LoadingScreenStart();
        break;

    case 5:
    {
        GetPageCharacterSelect().CurrentCharacter = G_NoCharacterId;
        LoadingScreenStart();
        ShutdownServerConnection();
        // The chat window is hidden in multiplayer.
        ReleaseActivePageInternal(Nav);
        SetClientMouseCursorDrawFlag(1);
        if (!SinglePlayer())
            ChatShowMsgWin(false);
        // Unless the server page is already active: lists reset, the Gun made, the page loaded.
        // A failed load goes back to state 1.
        PageServerSelect& ServerPage = GetPageServerSelect();
        if (Nav->ActivePage == &ServerPage)
            break;
        G_LoginMgr.ResetServerLists();
        G_LoginMgr.CreateGun();
        ReleaseActivePageInternal(Nav);
        if (!ServerPage.Desc.LoadFromFile(GetFullFilename(kPageServerSelect)))
        {
            Nav->CurrentState = 1;
            break;
        }
        ServerPage.Create(nullptr, nullptr);
        Nav->ActivePage = &ServerPage;
        GServerSelectActive = 1;
        break;
    }

    case 6:
    {
        // As case 5, for the account page.
        ReleaseActivePageInternal(Nav);
        SetClientMouseCursorDrawFlag(1);
        if (!SinglePlayer())
            ChatShowMsgWin(false);
        PageAccountSelect& AccountPage = GetPageAccountSelect();
        if (Nav->ActivePage == &AccountPage)
            break;
        ReleaseActivePageInternal(Nav);
        if (!AccountPage.Desc.LoadFromFile(GetFullFilename(kPageAccountSelect)))
        {
            Nav->CurrentState = 1;
            break;
        }
        AccountPage.Create(nullptr, nullptr);
        Nav->ActivePage = &AccountPage;
        break;
    }

    case 7:
    case 11:
    case 13:
        break;

    case 8:
    case 15:
        Nav->CurrentState = 9;
        break;

    case 9:
    {
        // A failed load goes back to state 1.
        PreConnectReset();
        ReleaseActivePageInternal(Nav);
        if (!SinglePlayer())
            ChatShowMsgWin(false);
        PageCharacterSelect& CharacterPage = GetPageCharacterSelect();
        if (!CharacterPage.Desc.LoadFromFile(GetFullFilename(kPageCharacterSelect)))
        {
            Nav->CurrentState = 1;
            break;
        }
        CharacterPage.Create(nullptr, nullptr);
        Nav->ActivePage = &CharacterPage;
        break;
    }

    case 10:
        ReleaseActivePageInternal(Nav);
        GetPageOptions().Create(nullptr, nullptr);
        Nav->ActivePage = &GetPageOptions();
        break;

    case 12:
        // Load the chosen character into the game.
        {
            LoadingScreenSetClear(1);
            LoadingScreenStart();
            // With bDrawScripts (DAT_00614a9c) and latch != 4, the intro base IntroFocus::Init
            // entered is left.
            if (GetIntroConfigState()->bDrawScripts && GIntroModeLatch != 4)
                LeaveIntroBase();
            bool bSelected = false;
            if (Nav->bHasCharacterPick != 0)
            {
                bSelected = G_LoginMgr.SelectCharacterById(Nav->CharacterId);
            }
            else
            {
                // The autosave, copied to a local first; none leaves the result false.
                CCharacterSet::CharacterInfo const* const AutoSave =
                    G_LoginMgr.FindAutoSaveCharacter();
                if (AutoSave != nullptr)
                {
                    CHARACTER_ID const Local = AutoSave->Id;
                    bSelected = G_LoginMgr.SelectCharacterById(Local);
                }
            }
            RefreshLoadingProgress();
            if (bSelected)
            {
                G_LoginMgr.DestroyGun();
                Nav->CurrentState = 0x14;
            }
            else
            {
                Nav->CurrentState = 5;
            }
        }
        break;

    case 14:
    {
        ReleaseActivePageInternal(Nav);
        PageCharacterCreate& CreatePage = GetPageCharacterCreate();
        if (!CreatePage.Load(GetFullFilename(kPageCharacterCreate)))
        {
            Nav->CurrentState = 9;
            break;
        }
        Nav->ActivePage = &CreatePage;
        break;
    }

    case 16:
        // New Game: the Restart.fl record the single-player session listed.
        {
            CCharacterSet::CharacterInfo const* const Restart = G_LoginMgr.FindRestartCharacter();
            if (Restart == nullptr)
            {
                FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
                      "E:\\FL\\Scratch\\Source\\Client\\Interface\\Intro.cpp", 0x1d95,
                      "New game character file not found\n");
                Nav->CurrentState = 1;
            }
            else
            {
                Nav->CharacterId = Restart->Id;
                Nav->bHasCharacterPick = 1;
                Nav->CurrentState = 0xc;
            }
        }
        break;

    case 17:
        // Minimise, run Flasher.exe and wait for it, maximise again.
        ShowWindow(g_client.window, SW_MINIMIZE);
        _spawnl(_P_WAIT, "Flasher.exe", "Flasher.exe", nullptr);
        ShowWindow(g_client.window, SW_MAXIMIZE);
        break;

    case 18:
    {
        ReleaseActivePageInternal(Nav);
        // The path is computed before the statics are built.
        char const* const PagePath = GetFullFilename(kPageCharSelectSingle);
        PageCharacterSelectSingle& SinglePage = GetPageCharacterSelectSingle();
        EnterCase18Chrome(SinglePage);
        if (SinglePage.Desc.LoadFromFile(PagePath))
        {
            SinglePage.Create(nullptr, nullptr);
            Nav->ActivePage = &SinglePage;
        }
        else
        {
            SinglePage.SlideT = -2.0;
            Nav->CurrentState = 1;
        }
        break;
    }

    case 19:
        // Back from the game (latch 2 / 4). Latch 2 -> the character list (state 9); otherwise
        // the character by the name DAT_006732f0 kept, and on success straight back into the
        // game (0x14).
        if (G_LoginMgr.RequestCharacterInfo() == 0)
        {
            break;
        }
        if (GIntroModeLatch == 2)
        {
            Nav->CurrentState = 9;
            break;
        }
        if (G_LoginMgr.SelectCharacter(
                G_LoginMgr.FindCharacterByName(GetPlayerCharacterNameRetail())))
        {
            Nav->CurrentState = 0x14;
        }
        break;

    default:
        break;
    }
    RefreshLoadingProgress();
}

int MenuStateDriverUpdate(FMenuNavState* Nav, float DeltaSeconds)
{
    // FUN_00573eb0 (thiscall, RET 4).
    int TransitionCode = 0;
    bool bDidEnter = false;

    // Leave the old state, enter the new one, until it settles.
    while (Nav->CurrentState != Nav->PrevState)
    {
        bDidEnter = true;
        RefreshLoadingProgress();
        int const Leaving = Nav->PrevState;
        // Retail sprintf()s "ExitState %d\n" into a local nobody reads; not ported.

        if (Leaving == 5)
        {
            GServerSelectActive = 0;
        }
        else if (Leaving == 10)
        {
            if (GOptionsChild_67c20c == nullptr || (GOptionsChild_67c20c->Flags & 1) == 0)
            {
                Nav->CurrentState = 1;
                if (GOptionsChild_67c20c != nullptr)
                    delete GOptionsChild_67c20c;
                GOptionsChild_67c20c = nullptr;
            }
        }

        RefreshLoadingProgress();
        Nav->PrevState = Nav->CurrentState;
        EnterMenuState(Nav, Nav->CurrentState);
        RefreshLoadingProgress();
    }

    // Per-state poll: retail's byte table 0x00574380 over CurrentState - 1; index 10 polls
    // nothing.
    static unsigned char const kPollIndex[20] = {0, 10, 10, 10, 1, 2, 10, 10, 3, 4,
                                                 10, 10, 5, 6, 10, 10, 7, 8, 10, 9};
    unsigned const PollSlot = static_cast<unsigned>(Nav->CurrentState - 1);
    unsigned char const Poll = PollSlot < 20u ? kPollIndex[PollSlot] : 10;
    switch (Poll)
    {
    case 0:
    {
        // State 1: not cleared here -- EnterState case 1 clears it.
        int const Next = GetTypeSelect().NextState;
        if (Next != 0)
        {
            Nav->CurrentState = Next;
            if (Next == 0xc)
                RefreshLoadingProgress();
        }
        break;
    }
    case 1:
    {
        // State 5: cleared once taken.
        PageServerSelect& ServerPage = GetPageServerSelect();
        if (ServerPage.Result != 0)
        {
            Nav->CurrentState = ServerPage.Result;
            ServerPage.Result = 0;
        }
        break;
    }
    case 2:
    {
        // State 6: cleared once taken.
        PageAccountSelect& AccountPage = GetPageAccountSelect();
        if (AccountPage.Result != 0)
        {
            Nav->CurrentState = AccountPage.Result;
            AccountPage.Result = 0;
        }
        break;
    }
    case 3:
    {
        // State 9. With the latch at 4, DAT_00668762 is cleared a second time.
        PageCharacterSelect& CharacterPage = GetPageCharacterSelect();
        if (CharacterPage.bLoadRequested != 0)
        {
            if (GetIntroConfigState()->bDrawScripts)
                LeaveIntroBase();
            if (GIntroModeLatch == 4)
                SetClientLocationNoServerNotify(false);
            CharacterPage.bIntroBaseLeft = 1;
        }
        // The Result field is not cleared.
        if (CharacterPage.Result != 0)
        {
            Nav->CurrentState = CharacterPage.Result;
            Nav->CharacterId = CharacterPage.SelectedCharacter;
            Nav->bHasCharacterPick = 1;
        }
        break;
    }
    case 4:
    {
        // State 10.
        PageOptions& Options = GetPageOptions();
        WinControl* const Preferences = Options.Preferences;
        if (Preferences == nullptr)
        {
            Nav->CurrentState = 1;
            Options.Result = 0;
            break;
        }
        // NnPreferences::bClosed (+0x32c), set by its close paths and by a failed Create.
        bool const bClosed = static_cast<NnPreferences*>(Preferences)->bClosed != 0;
        if (bClosed)
        {
            Win3DDeleteTree(Preferences);
            Options.Preferences = nullptr;
            Nav->CurrentState = 1;
            Options.Result = 0;
            break;
        }
        if (Options.Result != 0)
        {
            Nav->CurrentState = Options.Result;
            Options.Result = 0;
        }
        break;
    }
    case 5:
        // State 13: the single-player start failed -- client state 1 (quit).
        TransitionCode = 1;
        break;
    case 6:
    {
        // State 14. The field is not cleared.
        PageCharacterCreate& CreatePage = GetPageCharacterCreate();
        if (CreatePage.HasResult())
            Nav->CurrentState = CreatePage.GetResult();
        break;
    }
    case 7:
        // State 17: no poll.
        break;
    case 8:
    {
        // State 18. Unlike state 9 there is no latch-4 write.
        PageCharacterSelectSingle& SinglePage = GetPageCharacterSelectSingle();
        if (SinglePage.bLoadRequested != 0)
        {
            if (GetIntroConfigState()->bDrawScripts)
                LeaveIntroBase();
            SinglePage.bIntroBaseLeft = 1;
        }
        // The Result field is not cleared.
        if (SinglePage.Result != 0)
        {
            Nav->CurrentState = SinglePage.Result;
            Nav->CharacterId = SinglePage.SelectedCharacter;
            Nav->bHasCharacterPick = 1;
        }
        break;
    }
    case 9:
        // State 20: into the game -- 3 when the player has a base (DAT_00673358), else 2.
        TransitionCode = (GetPlayerBaseIdRetail() & 0x3fffffffu) != 0 ? 3 : 2;
        break;
    default:
        break;
    }

    // No camera (retail passes a register that is 0 on every path): the mixer keeps its
    // listener. dt is the uncapped argument.
    SetClientAudioMode(3);
    TickClientSoundMixer(nullptr, DeltaSeconds);

    if (bDidEnter)
    {
        return TransitionCode;
    }
    if (!GetIntroConfigState()->bDrawScripts)
        return TransitionCode;

    // Capped at 0.2 (DAT_005cf100); unordered is kept.
    double Dt = static_cast<double>(DeltaSeconds);
    if (Dt > 0.2)
        Dt = 0.2;
    // Whole milliseconds go to the script manager; the remainder is computed from the sum
    // rounded to float, while floor() takes the unrounded sum.
    double const Sum = static_cast<double>(Nav->ScriptTimeRemainder) + Dt;
    float const SumAsFloat = static_cast<float>(Sum);
    double const Milliseconds = floor(Sum * 1000.0);
    Nav->ScriptTimeRemainder =
        static_cast<float>(static_cast<double>(SumAsFloat) - Milliseconds * 0.001);
    G_GfScriptManager.Update(static_cast<int>(Milliseconds));

    float const DtFloat = static_cast<float>(Dt);
    TickClientLocationController(DtFloat);

    // Room objects get the capped double.
    std::list<FRoomObject*>& Objects = GetRoomObjectList();
    for (std::list<FRoomObject*>::iterator It = Objects.begin(); It != Objects.end(); ++It)
    {
        FRoomObject* const Object = *It;
        if (Object->bNoUpdate == 0)
            Object->Update(Dt);
    }

    // The material animation tick takes the uncapped argument.
    UpdateRoomLetterbox(static_cast<FRoomCamera*>(static_cast<ICamera*>(GetClientRoomCamera())),
                        DtFloat);
    UpdateFade(DtFloat);
    TickMaterialAnimationPlayback(DeltaSeconds);

    if (BeginClientRenderFrame())
    {
        LoadingScreenReset();
        Win3DSetViewport(nullptr, kIntroCpp, 0x1ac7);
        Win3DSetPipelineClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        ClearWin3DBuffers(kWin3DClearColour | kWin3DClearDepth);
        DrawClientLocation();
#ifdef FREELANCER_WIDESCREEN_SUPPORT
        // Port-only: the logo sits in the top left of its 4:3 picture and goes with the main
        // menu column to the left edge (hud_widescreen.cpp).
        ImageSetWidescreenAlign(-1);
        Nav->LogoImage.Draw();
        ImageSetWidescreenAlign(0);
#else
        Nav->LogoImage.Draw();
#endif
        ClearWin3DBuffers(kWin3DClearDepth);
        Win3DDrawAll();
        if (!IsClientServerTickPaused())
            DrawFade(static_cast<ICamera*>(GetClientRoomCamera()));
        Sleep(0);
        EndClientRenderScene();
        PresentClientRenderFrame();
    }

    // Once: the loading screen gets its progress bar, drops the startup Image and stops
    // clearing.
    if (Nav->bLoadingScreenSettled == 0)
    {
        LoadingScreenCreateProgressBar();
        LoadingScreenReleaseImage();
        LoadingScreenSetClear(0);
        Nav->bLoadingScreenSettled = 1;
    }

    return TransitionCode;
}

int MenuNavGetIntroModeLatch()
{
    return GIntroModeLatch;
}

void MenuNavSetIntroModeRequest(int Mode)
{
    // Consumed into the latch at menu Init.
    GIntroModeRequest = Mode;
}

int MenuNavGetIntroModeRequest()
{
    return GIntroModeRequest;
}

bool MenuNavIsServerSelectActive()
{
    // FUN_00564b20. Read by WaitForServerResponse.
    return GServerSelectActive != 0;
}

void MenuNavConsumeIntroModeRequest()
{
    GIntroModeLatch = GIntroModeRequest;
    GIntroModeRequest = 0;
}

// The column's TextMsgButtons get the mouse the retail way: MouseDispatchEvents ->
// KeyMapRouteMouseEvent -> commands 4..13 -> FWin3DInputHandler::Handle -> Win3DPick ->
// MouseEvent; the hover from Win3DUpdateAll, the draw from Win3DDrawAll.
