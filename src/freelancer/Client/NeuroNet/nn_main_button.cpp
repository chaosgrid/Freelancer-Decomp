// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "NeuroNet/nn_main_button.h"

#include "Common/Graphics/client_mesh_load.h"    // FClientMeshHolder::ReleaseWithAnim (FUN_0059eee0), FClientMeshHolder::LoadFromPath (FUN_0059ec80)
#include "Core/globals.h"             // IsClientServerTickPaused (FUN_0042d730), GetClientFrameTimeCapped (FUN_0042d680)
#include "Space/HUD/hud_command_dispatch.h" // HudPostCommand (FUN_005763b0), Hud{Push,Remove}CancelHandler
#include "Space/HUD/hud_target.h"          // HudIsMousePickBlocked (FUN_0045a740)
#include "GameFlow/menu_universe_base.h"  // IsClientLocationInTransition (DAT_00668761)
#include "Common/input_manager.h"       // Get/SetClientMouseCursorDrawFlag (FUN_0041dd90 / FUN_0041dda0), MouseSetCursor
#include "Network/login_mgr.h"           // G_Server (DAT_0067ecd0)
#include "Interface/menu_ui_init.h"        // PlayOrRewindUiSoundSlot (FUN_005645c0)
#include "GameFlow/nav_bar.h"             // NavBarCloseService (FUN_004424f0)
#include "NeuroNet/nn_messaging.h"        // NnVoiceTick (FUN_00489e00)
#include "NeuroNet/nn_modal.h"            // G_NnMainButton, G_bNnScreenUp, NnFloatBits
#include "NeuroNet/NavMap/nn_navmap.h"           // NavMapPostPendingOpen (FUN_004a2a80)
#include "NeuroNet/nn_button_control.h"
#include "NeuroNet/nn_static_control.h"
#include "NeuroNet/nn_story_star.h"       // StoryStarTick (FUN_004bc990), StoryStarRequeueLastObjective
#include "NeuroNet/nn_trade.h"            // G_NnTrade (DAT_006724b4)
#include "NeuroNet/nn_ui_pulse.h"         // G_NnPaletteRed (DAT_00679ba8)
#include "Space/player_spawn.h"        // GetPlayerConnectionId (DAT_00673344)
#include "Space/space_letterbox.h"     // SetLetterboxTarget (FUN_0041a0f0), IsLetterboxVisible (FUN_0041a3e0)
#include "Interface/win3d.h"               // Win3DFindByName, Win3DReparent, Win3DDeleteTree, Win3DDrawChildrenOf
#include "Interface/win_factory.h"         // WINFACTORY, WinFactoryCreate (FUN_0059db20)

#include "DALib.h"
#include "common_csys.h"         // Acquire3DMathEngine (FUN_005b73e0)
#include "common_server.h"       // IServer
#include "common_misc.h"         // SinglePlayer
#include "engine/engbase.h"
#include "x86math.h"

#include <string.h>
#include <windows.h>

unsigned char G_bNnKeepPanelOpen = 0; // DAT_0066d359

// Retail DAT_0066d39c -- WINFACTORY<NN_MainButton> static (LAB_0045b6d0 -> FUN_0045dce0; Create
// FUN_0045dd50; atexit LAB_0045b6f0 -> FUN_00579380).
WINFACTORY<NN_MainButton> G_NnMainButtonFactory("NN_MainButton");

namespace
{
    // DAT_0066d3ec -- MakeScaledBackground's lazily acquired 3DMathEngine (FUN_005b73e0).
    I3DMathEngine* G_NnMainButtonMath = nullptr;

    // DAT_005cea10 -- 0.3f, the duration of every slide and scale.
    float const kAnimSeconds = 0.3f;

    // DAT_006114f8[7] -- the "ScaledBackground" mesh of each panel slot.
    char const* const kScaledBackgroundMeshes[7] = {
        "Interface\\NeuroNet\\NavMap\\NewNavMap\\navmap_background.cmp",
        "Interface\\NeuroNet\\Inventory\\CHAR_infowindow.cmp",
        "Interface\\NeuroNet\\Inventory\\CHAR_infowindow.cmp",
        "Interface\\NeuroNet\\Inventory\\CHAR_infowindow.cmp",
        "Interface\\NeuroNet\\Inventory\\CHAR_infowindow.cmp",
        "Interface\\NeuroNet\\Chat\\CHAT_background.cmp",
        "Interface\\intro\\objects\\MULTI_attentionreq'd.cmp"};

    // The panel id -> slot switch every method inlines (e.g. 0x0045c7a0, jump table 0x0045c800).
    int NnPanelSlot(int PanelId)
    {
        switch (PanelId)
        {
        case USER_INVENTORY:
            return 1;
        case USER_STORY_STAR:
            return 2;
        case USER_PLAYER_STATS:
            return 3;
        case USER_HELP:
            return 4;
        case USER_CHAT_WINDOW:
            return 5;
        case USER_GROUP_WINDOW:
            return 6;
        default:
            return 0;
        }
    }

    // The window class each panel id opens (inlined at 0x0045cfb7 and 0x0045d358); null makes
    // those paths return.
    char const* NnPanelWindowName(int PanelId)
    {
        switch (PanelId)
        {
        case USER_NAV_MAP:
            return "NeuroNetNavMap";
        case USER_INVENTORY:
            return "NN_Inventory";
        case USER_STORY_STAR:
            return "NeuroNetStoryStar";
        case USER_PLAYER_STATS:
            return "NN_PlayerInfo";
        case USER_HELP:
            return "NN_Help";
        case USER_CHAT_WINDOW:
            return "ChatControl";
        case USER_GROUP_WINDOW:
            return "NN_GroupControl";
        default:
            return nullptr;
        }
    }

    // A plain icon button and its "Root" twin, the pair Create builds five (seven in
    // multiplayer) times (e.g. 0x0045b9da): the plain button is hidden, with no tooltip or
    // command and its +0x2d4 rest offset at Position; the root carries the tooltip and the
    // command id. Neither result is null-tested.
    void NnMainButtonCreatePair(NN_MainButton* Bar, char const* Name, char const* RootName,
                                char const* MeshPath, float X, int ToolTipIds, int CommandId,
                                WinControl** OutButton, WinControl** OutRoot)
    {
        Vector const Position = {X, NnFloatBits(0x3eeb020c), NnFloatBits(0xbfa00000)};
        Vector const HalfExtents = {NnFloatBits(0x3c9374bc), NnFloatBits(0x3c9374bc), 0.0f};

        WinControl* const Button =
            Bar->CreateButtonControl(Name, MeshPath, nullptr, "ui_select_item", nullptr, &Position,
                                     nullptr, &HalfExtents, 0, 0, 0, 0, 0, 0, 0);
        *OutButton = Button;
        Button->HomeOffset.x = Position.x;
        Button->HomeOffset.y = Position.y;
        Button->HomeOffset.z = Position.z;
        Button->Flags = static_cast<unsigned char>(Button->Flags & ~(kWin3DActive | kWin3DVisible));

        WinControl* const Root =
            Bar->CreateButtonControl(RootName, MeshPath, nullptr, "ui_select_item", nullptr,
                                     &Position, nullptr, &HalfExtents, 0, 0, 0, 0, ToolTipIds, 0,
                                     CommandId);
        *OutRoot = Root;
        Root->SetProperty(kButtonPropSpin, 1, 0);
    }

    void NnSetVector(Vector* Out, unsigned XBits, unsigned YBits, unsigned ZBits)
    {
        Out->x = NnFloatBits(XBits);
        Out->y = NnFloatBits(YBits);
        Out->z = NnFloatBits(ZBits);
    }
}

NN_MainButton::NN_MainButton()
{
    // FUN_0045b700.
    ClosingStart.x = 0.0f;
    ClosingStart.y = 0.0f;
    ClosingStart.z = 0.0f;
    OpenButton = nullptr;
    ClosingButton = nullptr;
    OpenPanelId = 0;
    ClosingPanelId = 0;
    ScaledBackground = nullptr;
    QueuedButton = nullptr;
    bInfoCardLayout = 0;
    bInventoryAfterDealer = 0;
    bInventoryWithDealer = 0;
    G_NnMainButton = this; // DAT_0066d3f0
    bForceMissionLog = 0;
}

NN_MainButton::~NN_MainButton()
{
    // FUN_0045b820.
    if (G_NnMainButton == this)
    {
        G_NnMainButton = nullptr;
    }
}

bool NN_MainButton::Create(Win3D* NewParent, void* Data)
{
    // LAB_0045b8f0 (RET 8).
    // (0.403, 0.366, -1) in single player, (0.362, 0.366, -1) otherwise.
    Vector const Zero = {0.0f, 0.0f, 0.0f};
    Vector const Zero338 = {0.0f, 0.0f, 0.0f};
    if (SinglePlayer())
    {
        Vector const Position = {NnFloatBits(0x3ece5604), NnFloatBits(0x3ebb645a),
                                 NnFloatBits(0xbf800000)};
        Background = CreateStaticControl("NNBackground", "Interface\\HUD\\HUD_nnbox5.cmp",
                                         &Position, nullptr, &Zero, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0,
                                         &Zero338);
    }
    else
    {
        Vector const Position = {NnFloatBits(0x3eb95810), NnFloatBits(0x3ebb645a),
                                 NnFloatBits(0xbf800000)};
        Background = CreateStaticControl("NNBackground", "Interface\\HUD\\HUD_nnbox7.cmp",
                                         &Position, nullptr, &Zero, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0,
                                         &Zero338);
    }

    // The five common pairs: y 0.459, z -1.25, half size 0.018.
    NnMainButtonCreatePair(this, "NNNavmapButton", "NNNavmapButtonRoot",
                           "interface\\NeuroNet\\icons\\NN_Map.3db", NnFloatBits(0x3ee978d6),
                           0x3fc, USER_NAV_MAP, &NavmapButton, &NavmapRoot);
    NnMainButtonCreatePair(this, "NNInventoryButton", "NNInventoryButtonRoot",
                           "interface\\NeuroNet\\icons\\NN_Inventory.3db",
                           NnFloatBits(0x3f010625), 0x3ed, USER_INVENTORY, &InventoryButton,
                           &InventoryRoot);
    NnMainButtonCreatePair(this, "NNMissionLogButton", "NNMissionLogButtonRoot",
                           "interface\\NeuroNet\\icons\\NN_storystar.3db",
                           NnFloatBits(0x3ed0e561), 0x421, USER_STORY_STAR, &MissionLogButton,
                           &MissionLogRoot);
    NnMainButtonCreatePair(this, "NNPlayerInfoButton", "NNPlayerInfoButtonRoot",
                           "interface\\NeuroNet\\icons\\NN_PlayerStatus.3db",
                           NnFloatBits(0x3f0d4fe0), 0x55f, USER_PLAYER_STATS, &PlayerInfoButton,
                           &PlayerInfoRoot);
    NnMainButtonCreatePair(this, "NNHelpButton", "NNHelpButtonRoot",
                           "interface\\NeuroNet\\icons\\NN_info.3db", NnFloatBits(0x3f19999a),
                           0x570, USER_HELP, &HelpButton, &HelpRoot);

    // Multiplayer adds chat and group.
    if (!SinglePlayer())
    {
        NnMainButtonCreatePair(this, "NNChatButton", "NNChatButtonRoot",
                               "interface\\NeuroNet\\icons\\NN_Chat.3db", NnFloatBits(0x3eb851ec),
                               0x435, USER_CHAT_WINDOW, &ChatButton, &ChatRoot);
        NnMainButtonCreatePair(this, "NNGroupButton", "NNGroupButtonRoot",
                               "interface\\NeuroNet\\icons\\NN_request.3db",
                               NnFloatBits(0x3e9fbe78), 0x560, USER_GROUP_WINDOW, &GroupButton, &GroupRoot);
    }
    else
    {
        ChatButton = nullptr;
        GroupButton = nullptr;
        ChatRoot = nullptr;
        GroupRoot = nullptr;
    }

    // FUN_005760d0 on the Event::Handler face.
    unsigned const Events[9] = {USER_NAV_MAP, USER_INVENTORY, USER_STORY_STAR, USER_PLAYER_STATS, USER_HELP,
                                  USER_CHAT_WINDOW, USER_GROUP_WINDOW, USER_DISPLAY_LAST_OBJECTIVE,
                                  USER_INVENTORY_CLOSE};
    for (int Index = 0; Index < 9; ++Index)
    {
        RegisterEvent(Events[Index]);
    }

    Buttons[0] = NavmapButton;
    Buttons[1] = InventoryButton;
    Buttons[2] = MissionLogButton;
    Buttons[3] = PlayerInfoButton;
    Buttons[4] = HelpButton;
    Buttons[5] = ChatButton;
    Buttons[6] = GroupButton;
    Roots[0] = NavmapRoot;
    Roots[1] = InventoryRoot;
    Roots[2] = MissionLogRoot;
    Roots[3] = PlayerInfoRoot;
    Roots[4] = HelpRoot;
    Roots[5] = ChatRoot;
    Roots[6] = GroupRoot;

    // Where each button slides to when its panel opens.
    NnSetVector(&OpenTarget[0], 0x00000000, 0x3d75c28f, 0xbfa00000); // (0, 0.06, -1.25)
    NnSetVector(&OpenTarget[1], 0x00000000, 0x3d8f5c29, 0xbfa00000); // (0, 0.07, -1.25)
    NnSetVector(&OpenTarget[2], 0x00000000, 0x3d8f5c29, 0xbfa00000);
    NnSetVector(&OpenTarget[3], 0x00000000, 0x3da3d70a, 0xbfa00000); // (0, 0.08, -1.25)
    NnSetVector(&OpenTarget[4], 0x00000000, 0x3da3d70a, 0xbfa00000);
    NnSetVector(&OpenTarget[5], 0x00000000, 0x3d6147ae, 0xbf800000); // (0, 0.055, -1)
    NnSetVector(&OpenTarget[6], 0x00000000, 0x3d4ccccd, 0xbf800000); // (0, 0.05, -1)
    // Where the open button sits while its panel shows (FUN_0045c7a0 overrides them).
    NnSetVector(&PanelOrigin[0], 0xbf1eb852, 0x3e99999a, 0xbfa00000); // (-0.62, 0.3, -1.25)
    NnSetVector(&PanelOrigin[1], 0x00000000, 0x3d9ba5e3, 0xbfa4dd2f); // (0, 0.076, -1.288)
    NnSetVector(&PanelOrigin[2], 0x00000000, 0x3d9ba5e3, 0xbfa4dd2f);
    NnSetVector(&PanelOrigin[3], 0x00000000, 0x3d9ba5e3, 0xbfa4dd2f);
    NnSetVector(&PanelOrigin[4], 0x00000000, 0x3d9ba5e3, 0xbfa4dd2f);
    NnSetVector(&PanelOrigin[5], 0x00000000, 0x00000000, 0xbf800000); // (0, 0, -1)
    NnSetVector(&PanelOrigin[6], 0x00000000, 0x00000000, 0xbf800000);
    // "ScaledBackground" positions.
    NnSetVector(&BackgroundPosition[0], 0x00000000, 0x3d8f5c29, 0xbfbc28f6); // (0, 0.07, -1.47)
    NnSetVector(&BackgroundPosition[1], 0x00000000, 0x3d9ba5e3, 0xbfa4dd2f); // (0, 0.076, -1.288)
    NnSetVector(&BackgroundPosition[2], 0x00000000, 0x3d9ba5e3, 0xbfa4dd2f);
    NnSetVector(&BackgroundPosition[3], 0x00000000, 0x3d9ba5e3, 0xbfa4dd2f);
    NnSetVector(&BackgroundPosition[4], 0x00000000, 0x3d9ba5e3, 0xbfa4dd2f);
    NnSetVector(&BackgroundPosition[5], 0x00000000, 0x3d6147ae, 0xbf800000); // (0, 0.055, -1)
    NnSetVector(&BackgroundPosition[6], 0x00000000, 0x3d4ccccd, 0xbf800000); // (0, 0.05, -1)
    // The background's collapsed scale per slot.
    PanelScale[0] = NnFloatBits(0x3da3d70a); // 0.08
    PanelScale[1] = NnFloatBits(0x3d4ccccd); // 0.05
    PanelScale[2] = NnFloatBits(0x3d4ccccd);
    PanelScale[3] = NnFloatBits(0x3d4ccccd);
    PanelScale[4] = NnFloatBits(0x3d4ccccd);
    PanelScale[5] = NnFloatBits(0x3da3d70a);
    PanelScale[6] = NnFloatBits(0x3da3d70a);

    return Win3D::Create(NewParent, Data); // FUN_004b5060
}

void NN_MainButton::Update()
{
    // LAB_0045c8c0.
    // Multiplayer with +0x52f set: open the mission log, with DAT_0067dcc8 lowered around the
    // call.
    if (!SinglePlayer() && bForceMissionLog != 0 && OpenButton == nullptr &&
        !IsClientServerTickPaused())
    {
        SetLetterboxTarget(0.0f, 3.0f);
        SetClientMouseCursorDrawFlag(1);
        MouseSetCursor("Arrow", false);
        G_bNnScreenUp = false;
        OnEvent(USER_STORY_STAR);
        G_bNnScreenUp = true;
    }

    Win3D::Update();
    ButtonMover::Update();  // FUN_0055e170
    ButtonScaler::Update(); // FUN_0055e5e0

    // The story star's per-frame decision whether to show itself.
    StoryStarTick();

    NnVoiceTick(static_cast<float>(GetClientFrameTimeCapped()));

    // The inventory re-opens once the dealer window has gone.
    if (bInventoryAfterDealer != 0 && !HudIsMousePickBlocked() &&
        Win3DFindByName("NN_Dealer") == nullptr && !ButtonMover::IsActive() &&
        !ButtonScaler::IsActive())
    {
        bInventoryAfterDealer = 0;
        HudPostCommand(USER_INVENTORY, 0);
    }

    // The same while the dealer window is still there.
    if (bInventoryWithDealer != 0 && !HudIsMousePickBlocked() &&
        Win3DFindByName("NN_Dealer") != nullptr && !ButtonMover::IsActive() &&
        !ButtonScaler::IsActive())
    {
        bInventoryWithDealer = 0;
        HudPostCommand(USER_INVENTORY, 0);
    }
}

void NN_MainButton::Draw()
{
    // LAB_0045ca00.
    // The HUD stopped picking (and no NeuroNet screen holds it): close the open panel unless
    // the mover runs or DAT_0066d359 holds it, and draw nothing.
    if (HudIsMousePickBlocked() && !G_bNnScreenUp)
    {
        if (OpenButton != nullptr && !ButtonMover::IsActive() && G_bNnKeepPanelOpen == 0)
        {
            ClosePanel();
        }
        return;
    }

    if (IsLetterboxVisible() && bForceMissionLog == 0)
    {
        return;
    }
    if (G_bNnScreenUp && OpenButton == nullptr)
    {
        return;
    }

    // The closing button is skipped while the background shrinks; the open button, once it has
    // arrived, draws only its children (FUN_0059e6e0, the panel window reparented under it).
    Win3D* Child = GetChildHead();
    while (Child != nullptr)
    {
        if (Child == ClosingButton && ButtonScaler::IsActive())
        {
        }
        else if (Child == OpenButton && !ButtonMover::IsActive())
        {
            Win3DDrawChildrenOf(Child);
        }
        else if ((Child->Flags & kWin3DVisible) != 0)
        {
            Child->Draw();
        }
        Child = Child->GetNextSibling();
    }

    NavMapPostPendingOpen(); // tail call FUN_004a2a80
}

intptr_t NN_MainButton::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_0045d550 (RET 0xc).
    if (Msg == kWinNotifyToolTip && (ButtonScaler::IsActive() || ButtonMover::IsActive()))
    {
        return 1;
    }
    return WinControl::SetProperty(Msg, A, B);
}

bool NN_MainButton::OnEvent(unsigned MsgId)
{
    // FUN_0045cae0 (RET 4; this = the +0x32c face).
    if (MsgId == USER_STORY_STAR)
    {
        // The mission log is refused only by the screen/cursor/pause gates, not by the docked /
        // no-ship half of FUN_0045a740.
        if (G_bNnScreenUp || GetClientMouseCursorDrawFlag() == 0 || IsClientServerTickPaused() ||
            IsClientLocationInTransition()) // DAT_00668761
        {
            return false;
        }
    }
    else if (HudIsMousePickBlocked())
    {
        return false;
    }

    // Byte-indexed switch over MsgId - 0x10 (0x0045cc6c).
    switch (MsgId)
    {
    case USER_DISPLAY_LAST_OBJECTIVE:
        // Queue the current mission objective (DAT_0067230c) on the story star's pending list.
        StoryStarRequeueLastObjective();
        return true;

    case USER_STACKABLE_CANCEL:
        // USER_CANCEL through the cancel stack.
        ClosePanel();
        return true;

    case USER_INVENTORY_CLOSE:
        // Toggle the inventory.
        if (OpenButton == Buttons[1])
        {
            ClosePanel();
            return true;
        }
        bInventoryAfterDealer = 0;
        OpenPanel(Buttons[1], USER_INVENTORY);
        return true;

    case USER_NAV_MAP:
    case USER_PLAYER_STATS:
    case USER_INVENTORY:
    case USER_STORY_STAR:
    case USER_CHAT_WINDOW:
    case USER_GROUP_WINDOW:
    case USER_HELP:
    {
        WinControl* const Button = Buttons[NnPanelSlot(static_cast<int>(MsgId))];
        if (OpenButton == Button)
        {
            // Clicking the open button closes it; with the dealer up the inventory comes back
            // once the dealer goes.
            if (Win3DFindByName("NN_Dealer") != nullptr)
            {
                bInventoryAfterDealer = 1;
            }
            ClosePanel();
            return true;
        }

        bInventoryAfterDealer = 0;
        bInventoryWithDealer = 0;
        if (Win3DFindByName("NN_Dealer") != nullptr &&
            (ButtonMover::IsActive() || ButtonScaler::IsActive()))
        {
            bInventoryWithDealer = 1;
            return false;
        }

        OpenPanel(Button, static_cast<int>(MsgId));
        return true;
    }

    default:
        return false;
    }
}

void NN_MainButton::OnMoveFinished()
{
    // LAB_0045d5a0 (this = the +0x330 face).
    // A scale still running is finished first.
    if (ButtonScaler::IsActive())
    {
        OnScaleFinished();
    }

    // A closing button is back home: hide it.
    if (ClosingButton != nullptr)
    {
        ClosingButton->Flags = static_cast<unsigned char>(ClosingButton->Flags & ~(kWin3DActive | kWin3DVisible));
        ClosingButton = nullptr;
        UpdateRootHighlights();
    }

    if (OpenButton != nullptr)
    {
        // The opening button arrived: grow the background from the slot's collapsed scale
        // (0.05 in the info-card layout) to 1.
        int const Slot = NnPanelSlot(OpenPanelId);
        float Scale = bInfoCardLayout != 0 ? NnFloatBits(0x3d4ccccd) : PanelScale[Slot];
        MakeScaledBackground(Scale);
        ButtonScaler::Start(kAnimSeconds);
        Scale = bInfoCardLayout != 0 ? NnFloatBits(0x3d4ccccd) : PanelScale[Slot];
        ButtonScaler::AddButton(ScaledBackground, Scale, 1.0f);
        return;
    }

    if (ScaledBackground != nullptr)
    {
        Win3DDeleteTree(ScaledBackground);
        ScaledBackground = nullptr;
    }

    // A panel queued while another was closing opens now.
    if (QueuedButton != nullptr)
    {
        OpenPanel(QueuedButton, QueuedPanelId);
        QueuedButton = nullptr;
    }
}

void NN_MainButton::OnScaleFinished()
{
    // LAB_0045d210 (this = the +0x350 face).
    if (ClosingButton != nullptr)
    {
        // The background has shrunk: slide the closing button home, with its tooltip back.
        if (ScaledBackground != nullptr)
        {
            Win3DDeleteTree(ScaledBackground);
            ScaledBackground = nullptr;
        }
        if (!ButtonMover::IsActive())
        {
            ButtonMover::Start(kAnimSeconds);
        }
        Vector const Zero = {0.0f, 0.0f, 0.0f};
        ClosingButton->SetPosition(&Zero);
        ButtonMover::AddButton(ClosingButton, &ClosingStart, &ClosingButton->HomeOffset);
        ClosingButton->ToolTipIds = ClosingToolTipIds;
        RemoveCancelHandler(0);
        return;
    }

    if (OpenButton == nullptr)
    {
        return;
    }

    // The background has grown. It stays only for the inventory in the normal layout.
    if (OpenPanelId != USER_INVENTORY || bInfoCardLayout != 0)
    {
        if (ScaledBackground != nullptr)
        {
            Win3DDeleteTree(ScaledBackground);
            ScaledBackground = nullptr;
        }
    }
    bInfoCardLayout = 0;

    char const* const Name = NnPanelWindowName(OpenPanelId);
    if (Name == nullptr)
    {
        return;
    }

    // A NeuroNet screen took over meanwhile (and +0x52f is clear): drop the open button without
    // a window.
    if (bForceMissionLog == 0 && G_bNnScreenUp)
    {
        OpenButton->Flags = static_cast<unsigned char>(OpenButton->Flags & ~(kWin3DActive | kWin3DVisible));
        OpenButton = nullptr;
        UpdateRootHighlights();
        return;
    }

    Win3D* Window = Win3DFindByName(Name);
    if (Window == nullptr)
    {
        Window = WinFactoryCreate(Name, Name, OpenButton, nullptr);
    }
    else
    {
        Win3DReparent(Window, OpenButton);
    }
    SavedToolTipIds = OpenButton->ToolTipIds;
    OpenButton->ToolTipIds = 0;
    if (Window == nullptr)
    {
        return;
    }

    OpenButton->SetPosition(&PanelOrigin[NnPanelSlot(OpenPanelId)]);
    Window->ProcessMessage(kWinMsgShow, 0, 0);
    Window->Flags = static_cast<unsigned char>(Window->Flags | kWin3DActive | kWin3DVisible);
    PlayOrRewindUiSoundSlot(kUiSoundWindowOpen);
    PushCancelHandler(0);
    UpdateRootHighlights();
}

void NN_MainButton::SetPanelOrigin(int PanelId, Vector const* Origin)
{
    // FUN_0045c7a0 (RET 8).
    Vector* const Out = &PanelOrigin[NnPanelSlot(PanelId)];
    Out->x = Origin->x;
    Out->y = Origin->y;
    Out->z = Origin->z;
}

WinControl* NN_MainButton::GetPanelButton(int PanelId) const
{
    // FUN_0045c820 (RET 4).
    return Buttons[NnPanelSlot(PanelId)];
}

void NN_MainButton::OpenPanel(WinControl* Button, int PanelId)
{
    // FUN_0045ccf0 (RET 8).
    // A queued open goes ahead even while a NeuroNet screen is up.
    if (QueuedButton == nullptr && G_bNnScreenUp)
    {
        return;
    }
    if (ButtonMover::IsActive() || ButtonScaler::IsActive())
    {
        return;
    }

    // An open trade ends (IServer slot 80, +0x140).
    if (G_NnTrade != nullptr)
    {
        G_Server->TerminateTrade(GetPlayerConnectionId(), 0);
        G_NnTrade->SetProperty(kWinMsgHide, 0, 0);
    }

    if (Button == OpenButton)
    {
        return;
    }

    NavBarCloseService(true);

    // Another panel is open: queue this one and close that.
    if (OpenButton != nullptr)
    {
        QueuedPanelId = PanelId;
        QueuedButton = Button;
        ClosePanel();
        return;
    }

    // Slide the button from its rest offset to the slot's open target ({0, -0.075, -1.25} in
    // the info-card layout).
    PlayOrRewindUiSoundSlot(kUiSoundMotionSwish);
    Button->Flags = static_cast<unsigned char>(Button->Flags | kWin3DActive | kWin3DVisible);
    ButtonMover::Start(kAnimSeconds);
    int const Slot = NnPanelSlot(PanelId);
    Vector Target;
    if (bInfoCardLayout != 0)
    {
        Target.x = 0.0f;
        Target.y = NnFloatBits(0xbd99999a);
        Target.z = NnFloatBits(0xbfa00000);
    }
    else
    {
        Target.x = OpenTarget[Slot].x;
        Target.y = OpenTarget[Slot].y;
        Target.z = OpenTarget[Slot].z;
    }
    ButtonMover::AddButton(Button, &Button->HomeOffset, &Target);
    OpenButton = Button;
    OpenPanelId = PanelId;
    PushCancelHandler(0);
    UpdateRootHighlights();
}

void NN_MainButton::ResetPanel()
{
    // FUN_0045cef0.
    if (OpenButton != nullptr)
    {
        OpenButton->Flags = static_cast<unsigned char>(OpenButton->Flags & ~(kWin3DActive | kWin3DVisible));
    }
    OpenButton = nullptr;
    if (ScaledBackground != nullptr)
    {
        Win3DDeleteTree(ScaledBackground);
        ScaledBackground = nullptr;
    }
    ButtonMover::Finish(); // tail call FUN_0055e290
}

void NN_MainButton::ClosePanel()
{
    // FUN_0045cf60.
    if (ButtonMover::IsActive() || ButtonScaler::IsActive() || OpenButton == nullptr)
    {
        return;
    }
    char const* const Name = NnPanelWindowName(OpenPanelId);
    if (Name == nullptr)
    {
        return;
    }

    PlayOrRewindUiSoundSlot(kUiSoundMotionSwish);
    ClosingButton = OpenButton;
    ClosingPanelId = OpenPanelId;
    ClosingToolTipIds = SavedToolTipIds;
    if (bInfoCardLayout != 0)
    {
        ClosingStart.x = 0.0f;
        ClosingStart.y = NnFloatBits(0xbd99999a);
        ClosingStart.z = NnFloatBits(0xbfa00000);
    }
    else
    {
        int const Slot = NnPanelSlot(OpenPanelId);
        ClosingStart.x = OpenTarget[Slot].x;
        ClosingStart.y = OpenTarget[Slot].y;
        ClosingStart.z = OpenTarget[Slot].z;
    }

    Win3D* const Window = Win3DFindByName(Name);
    if (Window != nullptr)
    {
        Window->ProcessMessage(kWinMsgHide, 0, 0);
    }

    // A full-size background shrinks to the slot's scale.
    MakeScaledBackground(1.0f);
    int const ClosingSlot = NnPanelSlot(ClosingPanelId);
    ButtonScaler::Start(kAnimSeconds);
    float const Scale = bInfoCardLayout != 0 ? NnFloatBits(0x3d4ccccd) : PanelScale[ClosingSlot];
    ButtonScaler::AddButton(ScaledBackground, 1.0f, Scale);

    OpenButton = nullptr;
    UpdateRootHighlights();
}

void NN_MainButton::MakeScaledBackground(float Scale)
{
    // FUN_0045d770 (RET 4).
    if (ScaledBackground != nullptr)
    {
        Win3DDeleteTree(ScaledBackground);
        ScaledBackground = nullptr;
    }

    // The info card at (0, -0.086, -1.288), or the slot's mesh at its BackgroundPosition.
    int const Slot = NnPanelSlot(OpenPanelId);
    Vector const HalfExtents = {0.0f, 0.0f, 0.0f};
    Vector const Zero338 = {0.0f, 0.0f, 0.0f};
    if (bInfoCardLayout != 0)
    {
        Vector const Position = {0.0f, NnFloatBits(0xbdb020c5), NnFloatBits(0xbfa4dd2f)};
        ScaledBackground = CreateStaticControl(
            "ScaledBackground", "Interface\\NeuroNet\\Inventory\\INV_biginforcard_center.cmp",
            &Position, nullptr, &HalfExtents, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, &Zero338);
    }
    else
    {
        ScaledBackground = CreateStaticControl("ScaledBackground", kScaledBackgroundMeshes[Slot],
                                               &BackgroundPosition[Slot], nullptr, &HalfExtents, 0,
                                               0, 0, 1, 0, 0, 1, 0, 0, 0, &Zero338);
    }

    // The rotation becomes identity scaled by Scale, the translation is kept. No null or -1
    // test on the instance.
    const InstanceIndex Instance = ScaledBackground->MeshHolder.EngineInstance;
    Transform Xform;
    Xform.Rotation.E[0][0] = 1.0f;
    Xform.Rotation.E[0][1] = 0.0f;
    Xform.Rotation.E[0][2] = 0.0f;
    Xform.Rotation.E[1][0] = 0.0f;
    Xform.Rotation.E[1][1] = 1.0f;
    Xform.Rotation.E[1][2] = 0.0f;
    Xform.Rotation.E[2][0] = 0.0f;
    Xform.Rotation.E[2][1] = 0.0f;
    Xform.Rotation.E[2][2] = 1.0f;
    const Transform* const Current = DALib::Engine->GetInstanceTransform(Instance);
    Xform.Translation.x = Current->Translation.x;
    Xform.Translation.y = Current->Translation.y;
    Xform.Translation.z = Current->Translation.z;
    if (G_NnMainButtonMath == nullptr)
    {
        G_NnMainButtonMath = Acquire3DMathEngine();
    }
    G_NnMainButtonMath->Scale(Xform.Rotation, Xform.Rotation, Scale);
    DALib::Engine->SetInstanceMatrix(Instance, &Xform);
    DALib::Engine->UpdateInstance(Instance, 0, nullptr);
}

bool NN_MainButton::HasActivePanel() const
{
    // FUN_0045da00.
    return OpenButton != nullptr;
}

void NN_MainButton::SetGroupButtonAlert(bool bAlert)
{
    // FUN_0045da10 (RET 4). No null test (only the multiplayer group control calls it).
    uintptr_t const Flag = bAlert ? 1u : 0u;
    GroupButton->SetProperty(kWinPropColour, reinterpret_cast<uintptr_t>(&G_NnPaletteRed), Flag);
    GroupRoot->SetProperty(kWinPropColour, reinterpret_cast<uintptr_t>(&G_NnPaletteRed), Flag);
}

void NN_MainButton::SetGroupButtonMesh(int Mode)
{
    // FUN_0045da50 (RET 4).
    char const* Path = nullptr;
    switch (Mode)
    {
    case kGroupButtonMeshRequest:
        Path = "interface\\NeuroNet\\icons\\NN_request.3db"; // 0x005ceb1c
        break;
    case kGroupButtonMeshTradeRequest:
        Path = "interface\\NeuroNet\\icons\\NN_traderequest.3db"; // 0x005cedd8
        break;
    case kGroupButtonMeshGroupRequest:
        Path = "interface\\NeuroNet\\icons\\NN_grouprequest.3db"; // 0x005cee08
        break;
    default:
        return;
    }

    // One transform buffer for both controls: identity with zero translation, overwritten by a
    // bound instance's current transform (12 dwords) before the mesh is swapped and put back
    // after. The root reuses whatever the button left in it.
    Transform Saved;
    Saved.Rotation.E[0][0] = 1.0f;
    Saved.Rotation.E[0][1] = 0.0f;
    Saved.Rotation.E[0][2] = 0.0f;
    Saved.Rotation.E[1][0] = 0.0f;
    Saved.Rotation.E[1][1] = 1.0f;
    Saved.Rotation.E[1][2] = 0.0f;
    Saved.Rotation.E[2][0] = 0.0f;
    Saved.Rotation.E[2][1] = 0.0f;
    Saved.Rotation.E[2][2] = 1.0f;
    Saved.Translation.x = 0.0f;
    Saved.Translation.y = 0.0f;
    Saved.Translation.z = 0.0f;

    WinControl* const Controls[2] = {GroupButton, GroupRoot};
    for (int Index = 0; Index < 2; ++Index)
    {
        WinControl* const Control = Controls[Index];
        if (Control->MeshHolder.EngineInstance != InvalidInstanceIndex)
        {
            memcpy(&Saved, DALib::Engine->GetInstanceTransform(Control->MeshHolder.EngineInstance),
                   sizeof(Saved));
        }
        strcpy(Control->MeshPath, Path);
        Control->MeshHolder.ReleaseWithAnim();
        Control->MeshHolder.LoadFromPath(Path, 0, false);
        const InstanceIndex Instance = Control->MeshHolder.EngineInstance;
        if (Instance != InvalidInstanceIndex)
        {
            DALib::Engine->SetInstanceMatrix(Instance, &Saved);
            DALib::Engine->UpdateInstance(Instance, 0, nullptr);
        }
    }
}

void NN_MainButton::FreeScaledBackground()
{
    // FUN_0045dc60.
    if (ScaledBackground != nullptr)
    {
        Win3DDeleteTree(ScaledBackground);
        ScaledBackground = nullptr;
    }
}

void NN_MainButton::SetInfoCardLayout()
{
    // FUN_0045dcb0.
    bInfoCardLayout = 1;
}

void NN_MainButton::SetForceMissionLog(bool bForce)
{
    // FUN_0045dcc0 (RET 4).
    bForceMissionLog = bForce ? 1 : 0;
}

bool NN_MainButton::GetForceMissionLog() const
{
    // FUN_0045dcd0.
    return bForceMissionLog != 0;
}

void NN_MainButton::UpdateRootHighlights()
{
    for (int Index = 0; Index < 7; ++Index)
    {
        WinControl* const Root = Roots[Index];
        if (Root == nullptr)
        {
            continue;
        }
        bool const bLit = Buttons[Index] == OpenButton || Buttons[Index] == ClosingButton;
        Root->SetProperty(kWinPropLit, bLit ? 1u : 0u, 0);
    }
}
