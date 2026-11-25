// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/mission_briefing.h"

#include "NeuroNet/Economy/best_path.h"              // BestPathGetWaypointCount, G_BestPaths
#include "Interface/Pages/character_select_equip.h" // SetPlayerBaseIdRetail, GetPlayerStarSystemIdRetail
#include "NeuroNet/Chat/chat_init.h"              // ChatIsInputOpen
#include "Core/client_frame_tick.h"
#include "GameFlow/gameflow.h"               // DAT_00668780 / DAT_00668781 / DAT_00668784
#include "Common/input_manager.h"          // MouseSetCursor, SetClientMouseCursorDrawFlag
#include "Interface/loading_screen.h"         // LoadingScreenReset
#include "Common/Audio/menu_sound_arch.h"        // TickClientSoundMixer, SetClientBackgroundMusic
#include "Interface/menu_ui_init.h"           // IsAnyUiSoundPlaying
#include "NeuroNet/nn_modal.h"               // NnSystemInit / NnSystemShutdown
#include "NeuroNet/nn_static_control.h"      // StaticControl::Init
#include "NeuroNet/nn_story_star.h"          // StoryStarReset
#include "Interface/win3d.h"                  // Win3DDrawAll, Win3DDeleteTree
#include "Interface/win3d_render.h"           // ClearWin3DBuffersToColor
#include "Interface/win_factory.h"            // WinFactoryCreate

#include "common_universe.h"        // Universe::get_system
#include "dacom.h"                  // FDUMP

#include <windows.h>

namespace
{
    // .rdata 0x005cb0d0.
    char const kBriefingWindowMesh[] = "Interface\\NeuroNet\\Inventory\\repairblackbox.3db";

    // RTTI .?AUMissionBriefing@@; the slot map is in mission_briefing.h.
    struct MissionBriefing : public IEnvironment
    {
        bool OnKeyMessage(unsigned A, unsigned Message, unsigned B, unsigned C) override;
        bool Slot01(unsigned A, unsigned B, unsigned C) override;
        bool OnMouseMove(unsigned A, unsigned B, unsigned long Packed) override;
        bool OnMouseButton(unsigned A, unsigned Message, unsigned B, unsigned long Packed) override;
        bool Slot04(unsigned A, unsigned B, unsigned C) override;
        void Slot05() override;
        void Slot06(unsigned A) override;
        int GetEnvironmentType() override;
        void PreInit() override;
        void Shutdown() override;
        void Slot10() override;
        void Resize(int Width, int Height) override;
        bool Init(int Param) override;
        void Destroy() override;
        int Update(float DeltaSeconds) override;

        // Its static initializer LAB_0043d120 only stamps the vptr; the rest is .bss zero until
        // slot 8 runs.
        unsigned char bFirstFramePending = 0; // +0x04
        Win3D* ParentWindow = nullptr;        // +0x08 "BriefingParentWindow"
        // +0x0c / +0x0d / +0x10: G_bMissionBriefingNavMapUp, G_bMissionBriefingContinue and
        // DAT_00668784 (gameflow.cpp).
        unsigned char bIdle = 0;              // +0x14
    };

    // The frame both halves of Update draw (inlined twice in retail).
    void DrawBriefingFrame(float DeltaSeconds)
    {
        TickClientSoundMixer(nullptr, DeltaSeconds);
        if (!BeginClientRenderFrame())
            return;
        unsigned char const Black[4] = {0, 0, 0, 0xff};
        ClearWin3DBuffersToColor(Black);
        Win3DDrawAll();
        EndClientRenderScene();
        PresentClientRenderFrame();
    }

    // The exit both Destroy and Update take (inlined in retail).
    void LeaveBriefing(MissionBriefing* State)
    {
        State->bIdle = 1;
        State->bFirstFramePending = 1;
        G_bMissionBriefingContinue = 0;
        State->ParentWindow->Flags = static_cast<unsigned char>(
            State->ParentWindow->Flags & ~(kWin3DActive | kWin3DVisible));
        SetPlayerBaseIdRetail(0);
        NnSystemShutdown();
    }

    // Slot 0 (LAB_0043d130). Unless the chat input "CTID" is up, GetKeyState(VK_SHIFT) and
    // GetKeyState(VK_CONTROL) with their results dropped; true either way.
    bool MissionBriefing::OnKeyMessage(unsigned A, unsigned Message, unsigned B, unsigned C)
    {
        (void)A;
        (void)Message;
        (void)B;
        (void)C;
        if (!ChatIsInputOpen())
        {
            GetKeyState(VK_SHIFT);
            GetKeyState(VK_CONTROL);
        }
        return true;
    }

    // Slots 1, 2 and 4 (LAB_0043d160).
    bool MissionBriefing::Slot01(unsigned A, unsigned B, unsigned C)
    {
        (void)A;
        (void)B;
        (void)C;
        return true;
    }

    bool MissionBriefing::OnMouseMove(unsigned A, unsigned B, unsigned long Packed)
    {
        (void)A;
        (void)B;
        (void)Packed;
        return true;
    }

    bool MissionBriefing::Slot04(unsigned A, unsigned B, unsigned C)
    {
        (void)A;
        (void)B;
        (void)C;
        return true;
    }

    // Slot 3 (LAB_0043d150).
    bool MissionBriefing::OnMouseButton(unsigned A, unsigned Message, unsigned B, unsigned long Packed)
    {
        (void)A;
        (void)Message;
        (void)B;
        (void)Packed;
        return true;
    }

    // Slots 5 and 10 (0x00401e90).
    void MissionBriefing::Slot05()
    {
    }

    void MissionBriefing::Slot10()
    {
    }

    // Slot 6 (0x0053e4a0).
    void MissionBriefing::Slot06(unsigned A)
    {
        (void)A;
    }

    // Slot 7 (LAB_0043d2e0) -- GF answers 1, IntroFocus 2.
    int MissionBriefing::GetEnvironmentType()
    {
        return kEnvironmentMissionBriefing;
    }

    // Slot 8 (LAB_0043d170).
    void MissionBriefing::PreInit()
    {
        MissionBriefing* const State = this;
        State->bFirstFramePending = 1;
        G_bMissionBriefingNavMapUp = 0;
        G_bMissionBriefingContinue = 0;
        SetGameflowLocationBase(0);
        State->bIdle = 1;
        State->ParentWindow = WinFactoryCreate("BriefingParentWindow", "StaticControl", nullptr,
                                               nullptr);
        if (State->ParentWindow == nullptr)
            return;
        // Init flags: only NoWireRender and NoClearRender set.
        float const Offset[3] = {0.0f, 0.0f, 0.0f};
        float const HalfExtents[3] = {0.0f, 0.0f, 0.0f};
        float const StartYpr[3] = {0.0f, 0.0f, 0.0f};
        static_cast<NnStaticControl*>(State->ParentWindow)
            ->Init(kBriefingWindowMesh, Offset, nullptr, HalfExtents, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0,
                   StartYpr);
        float const Position[3] = {-0.62f, 0.3f, -1.25f};
        State->ParentWindow->SetPosition(Position);
        // Hidden until the first frame.
        State->ParentWindow->Flags = static_cast<unsigned char>(
            State->ParentWindow->Flags & ~(kWin3DActive | kWin3DVisible));
    }

    // Slot 9 (LAB_0043d260) -- Win3DDeleteTree's body inlined.
    void MissionBriefing::Shutdown()
    {
        if (ParentWindow == nullptr)
            return;
        Win3DDeleteTree(ParentWindow);
        ParentWindow = nullptr;
    }

    // Slot 11 (0x0053aaa0).
    void MissionBriefing::Resize(int Width, int Height)
    {
        (void)Width;
        (void)Height;
    }

    // Slot 12 (LAB_0043d2f0). The argument is not read.
    bool MissionBriefing::Init(int Param)
    {
        (void)Param;
        bIdle = 0;
        return true;
    }

    // Slot 13 (LAB_0043d300).
    void MissionBriefing::Destroy()
    {
        MissionBriefing* const State = this;
        // Only while the briefing is up (first frame done) and the window exists.
        if (State->bFirstFramePending != 0 || State->ParentWindow == nullptr)
            return;
        LeaveBriefing(State);
        StoryStarReset();
        // The window's first child, the NeuroNetNavMap, gets the close message 0xef02.
        Win3D* const NavMap = State->ParentWindow->GetChildHead();
        if (NavMap != nullptr)
            NavMap->ProcessMessage(kWinMsgHide, 0, 0);
    }

    // Slot 14 (LAB_0043d360, RET 4).
    int MissionBriefing::Update(float DeltaSeconds)
    {
        MissionBriefing* const State = this;

        // An unordered DeltaSeconds does not count as 0, as retail's x87 test.
        if (DeltaSeconds == 0.0f || G_bMissionBriefingContinue != 0)
        {
            DrawBriefingFrame(DeltaSeconds);
            // With the continue request up, wait while a UI sound plays.
            if (G_bMissionBriefingContinue != 0 && IsAnyUiSoundPlaying())
                return 0;
        }

        if (State->bIdle != 0)
            return 0;

        if (State->bFirstFramePending != 0)
        {
            // No route, an empty route list, or a route whose Text MadLibId is 1 or 2: no
            // briefing, back to space.
            if (BestPathGetWaypointCount() == 0)
            {
                StoryStarReset();
                return 2;
            }
            if (G_BestPaths.empty())
            {
                StoryStarReset();
                return 2;
            }
            unsigned const MadLibId = G_BestPaths.begin()->second.Text.MadLibId;
            if (MadLibId == 2 || MadLibId == 1)
            {
                StoryStarReset();
                return 2;
            }

            // No NN_MainButton.
            State->bFirstFramePending = 0;
            NnSystemInit(false);
            State->ParentWindow->Flags = static_cast<unsigned char>(
                State->ParentWindow->Flags | kWin3DActive | kWin3DVisible);
            SetPlayerBaseIdRetail(GetGameflowLocationBase());
            // The nav map's Create reads the "being built" flag. No null test on the create.
            G_bMissionBriefingNavMapUp = 1;
            Win3D* const NavMap = WinFactoryCreate("NeuroNetNavMap", "NeuroNetNavMap",
                                                   State->ParentWindow, nullptr);
            NavMap->ProcessMessage(kWinMsgShow, 0, 0);
            NavMap->Flags =
                static_cast<unsigned char>(NavMap->Flags | kWin3DActive | kWin3DVisible);
            G_bMissionBriefingNavMapUp = 0;
            MouseSetCursor("arrow", false);
            SetClientMouseCursorDrawFlag(1);
            // The player's system's space music; no system, no music change.
            Universe::ISystem const* const System =
                Universe::get_system(GetPlayerStarSystemIdRetail());
            if (System != nullptr)
                SetClientBackgroundMusic(System->GetSpaceMusic(), 1, -1.0f);
        }

        DrawBriefingFrame(DeltaSeconds);

        if (G_bMissionBriefingContinue == 0)
        {
            LoadingScreenReset();
            SetClientMouseCursorDrawFlag(1);
            return 0;
        }

        // The nav map asked to continue.
        LeaveBriefing(State);
        StoryStarReset();
        return 2;
    }

    // DAT_00668774. Its static initializer LAB_0043d120 only stamps the vptr; the rest is .bss
    // zero until slot 8 runs.
    MissionBriefing G_MissionBriefing;
}

IEnvironment* GetMissionBriefingState()
{
    // FUN_0043d560.
    return &G_MissionBriefing;
}
