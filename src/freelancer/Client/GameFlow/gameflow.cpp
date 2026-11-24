// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// gf\Gameflow.cpp (the GF state, DAT_006686a0, vftable 0x005cae24) and the supt\Support.cpp
// functions it drives (FUN_00558850 .. FUN_005592a0). See gameflow.h.

#include "GameFlow/gameflow.h"

#include "GameFlow/character_info.h"         // LoadCharacterInfo / PreloadCharacterScripts / ...
#include "NeuroNet/Chat/chat_init.h"              // ChatShowMsgWin (FUN_0046a4a0)
#include "Interface/Pages/character_select_equip.h" // GetPlayerBaseIdRetail / SetPlayerBaseIdRetail / ...
#include "Common/client_cliobj.h"          // Archetype::ReleaseMaterialEntrySlot (FUN_0041b030)
#include "Core/client_frame_tick.h"      // FUN_00424f30 / FUN_00425080 / FUN_00425140, FUN_0041b2e0
#include "Core/event_handler.h"
#include "Common/Graphics/fade_to_black.h"
#include "Space/Effects/flash_light.h"            // LoadFlashlightTextures / ReleaseFlashlightTextures
#include "GameFlow/gf_character_object.h"    // ReleaseSharedCharacterLod (FUN_00443e90)
#include "GameFlow/gf_hotspot.h"
#include "GameFlow/Thorn/gf_script_object.h"       // G_GfScriptManager (DAT_0066d2d0)
#include "Core/globals.h"
#include "Space/HUD/hud_command_dispatch.h"   // HudCommandRecord (FUN_00576240)
#include "Common/input_manager.h"
#include "Interface/loading_screen.h"         // FUN_00579190 / FUN_00579290
#include "Core/main_command_handler.h"   // ClientQuitRequested (FUN_005b2c60)
#include "Common/Audio/menu_sound_arch.h"        // TickClientSoundMixer (FUN_00428040)
#include "Interface/menu_ui_widgets.h"
#include "GameFlow/menu_universe_base.h"     // the LocationController (DAT_00668708)
#include "NeuroNet/nn_arrival_text.h"        // NnArrivalTextShowBase (FUN_0045b2d0)
#include "NeuroNet/nn_modal.h"               // NnSystemInit / NnSystemShutdown (FUN_0045a100 / FUN_0045a140)
#include "GameFlow/room_camera.h"             // FRoomCamera (FUN_0041a140's receiver)
#include "GameFlow/room_control.h"
#include "Common/Audio/sound_config.h"           // SetClientAudioMode (FUN_00428030)
#include "Space/space_letterbox.h"
#include "Interface/win3d.h"                  // Win3DDrawAll (FUN_0059e300)
#include "Interface/win3d_immediate.h"
#include "Interface/win3d_render.h"

#include "common_newchar.h"         // RtcSlider
#include "common_misc.h"            // SinglePlayer
#include "dacom.h"                  // FDUMP
#include "DALib.h"                  // DALib::Engine (IEngine::ReleaseHandle, G_InvalidEngineHandle)

#include <math.h>
#include <stddef.h>
#include <windows.h>

// Retail runs this file's static initializers before Win3D.cpp's, and the mouse depends on it:
// LAB_00559030 (the hotspot handler below, DAT_006799ec) precedes LAB_0059d060 (the Win3D input
// handler DAT_0067dbf0) in the _initterm table, and FUN_00576410 calls the handlers of one id in
// reverse registration order, so Win3D sees every mouse event first and a click on a window
// never reaches a room hotspot. Port: MSVC orders the user segment by link order, which would
// put win3d.obj first and let a character's hotspot take clicks through the NeuroNet windows;
// init_seg(lib) runs this file ahead of every user initializer. Nothing constructed here reads
// another file's dynamic initializer: the HUD registry is a pointer allocated on first insert
// (EnsureHudCmdRegistry), GF only stamps its vptr, the instance cache fills constants.
#pragma warning(push)
#pragma warning(disable : 4073) // initializers put in library initialization area
#pragma init_seg(lib)
#pragma warning(pop)

namespace
{
    char const kGameflowCpp[] = "E:\\FL\\Scratch\\Source\\Client\\gf\\Gameflow.cpp";
    char const kSupportCpp[] = "E:\\FL\\Scratch\\Source\\Client\\supt\\Support.cpp";

    // ------------------------------------------------------------------------------------
    // supt\Support.cpp globals.
    // ------------------------------------------------------------------------------------
    bool G_bGameflowMissionBriefing = false;   // DAT_006799dc -- the base frame answers 8
    bool G_bGameflowStateFive = false;         // DAT_006799dd -- the base frame answers 5, once
    bool G_bGameflowQuit = false;              // DAT_006799de -- answers 1; only ever zeroed
    float G_GameflowMsRemainder = 0.0f;        // DAT_006799e0 -- the tick's sub-millisecond rest
    // DAT_006799e4 -- the arrival caption's delay in milliseconds. Zeroed by FUN_005588a0 before
    // it is read there; its only other writer is FGfScriptSequence::AddScript
    // (SetGameflowArrivalDelayMs).
    unsigned G_GameflowArrivalDelayMs = 0;
    unsigned G_GameflowHoldUntilFrame = 0;     // DAT_006799e8 -- FUN_00558bf0

    // DAT_006799fc / DAT_00679a00 -- the last cursor point the tracking saw (LAB_00558da0, a
    // static initializer, zeroes both).
    int G_HotspotLastCursor[2] = {0, 0};
    bool G_bHotspotTracking = false;           // DAT_00679a04
    FHotspot* G_HoveredHotspot = nullptr;      // DAT_00679a08
    int G_HotspotInputLock = 0;                // DAT_00679a0c -- FUN_00559290 / FUN_005592a0

    // DAT_00668784 -- FUN_0043d590's store; the MissionBriefing state's +0x10, which its update
    // LAB_0043d360 hands to FUN_004c4910 by address.
    unsigned G_GameflowLocationBase = 0;

    // FUN_00558e00's and FUN_00558ed0's shared first half, and FUN_00558f80's body: find the
    // hotspot under Point and move the hover to it -- the old one gets bHovered = 0 and a leave
    // (2), the new one bHovered = 1 and an enter (1), both with Point.
    void MoveHotspotHover(int const* Point)
    {
        FHotspot* Hit = nullptr;
        FindHotspotAt(Point, &Hit);
        if (Hit == G_HoveredHotspot)
            return;
        if (G_HoveredHotspot != nullptr)
        {
            G_HoveredHotspot->bHovered = 0;
            G_HoveredHotspot->SendEvent(2, Point);
        }
        G_HoveredHotspot = Hit;
        if (Hit != nullptr)
        {
            Hit->bHovered = 1;
            G_HoveredHotspot->SendEvent(1, Point);
        }
    }

    // FUN_00558d80 (thiscall on an 8-byte point, RET 4): a packed mouse lParam into
    // {low word, high word}, both zero-extended.
    void UnpackCursorPoint(int* Out, unsigned long Packed)
    {
        Out[0] = static_cast<int>(Packed & 0xffffu);
        Out[1] = static_cast<int>(Packed >> 16);
    }

    // FUN_00558e00 (cdecl) -- the tracking for a mouse move. Locked -> false. Otherwise record
    // Point, move the hover, and answer whether something is hovered.
    bool TrackHotspotMouseMove(int const* Point)
    {
        if (G_HotspotInputLock != 0)
            return false;
        G_bHotspotTracking = true;
        G_HotspotLastCursor[0] = Point[0];
        G_HotspotLastCursor[1] = Point[1];
        MoveHotspotHover(Point);
        return G_HoveredHotspot != nullptr;
    }

    // FUN_00558ed0 (cdecl) -- the tracking for a mouse button message. As FUN_00558e00, then
    // the hovered hotspot (if any) gets event 3 for WM_LBUTTONDOWN (0x201) and -1 (which
    // FUN_00555f00 ignores) for anything else; true when there was a hovered hotspot.
    bool TrackHotspotMouseButton(unsigned Message, int const* Point)
    {
        if (G_HotspotInputLock != 0)
            return false;
        G_bHotspotTracking = true;
        G_HotspotLastCursor[0] = Point[0];
        G_HotspotLastCursor[1] = Point[1];
        MoveHotspotHover(Point);
        int Event = -1;
        if (Message == 0x201)
            Event = 3;
        if (G_HoveredHotspot == nullptr)
            return false;
        G_HoveredHotspot->SendEvent(Event, Point);
        return true;
    }

    // FUN_00558f80 (cdecl) -- once a frame from FUN_00558ac0: while tracking and not locked,
    // re-test the last cursor point (the room may have moved under a still mouse).
    void RefreshHoveredHotspot()
    {
        if (!G_bHotspotTracking)
            return;
        if (G_HotspotInputLock != 0)
            return;
        G_bHotspotTracking = true;
        MoveHotspotHover(G_HotspotLastCursor);
    }

    // DAT_006799ec -- the Support file's Event::Handler (vftable 0x005e1494, one slot
    // LAB_005590c0). Static initializer LAB_00559030: FUN_00559050 registers USER_MOVE (5) and
    // USER_BUTTON0 (6) through FUN_005760d0; atexit LAB_00559280 -> thunk_FUN_00576010.
    class FGameflowHotspotHandler : public EventHandler
    {
    public:
        FGameflowHotspotHandler()
        {
            // FUN_00559050.
            static unsigned const kIds[2] = {5, 6};
            for (unsigned Index = 0; Index < 2; ++Index)
                RegisterEvent(kIds[Index]);
        }

        // LAB_005590c0.
        bool OnEvent(unsigned MsgId) override
        {
            int Point[2] = {0, 0};
            int Wheel = 0;
            GetClientSoftwareCursor(&Point[0], &Point[1], &Wheel);

            if (MsgId == 5)
                return OnMove(Point);
            if (MsgId == 6)
                return OnButton(Point);
            return false;
        }

    private:
        // USER_MOVE.
        bool OnMove(int const* Point)
        {
            bool bHovering = false;
            if (G_HotspotInputLock == 0)
            {
                G_HotspotLastCursor[0] = Point[0];
                G_HotspotLastCursor[1] = Point[1];
                G_bHotspotTracking = true;
                MoveHotspotHover(Point);
                bHovering = G_HoveredHotspot != nullptr;
                if (bHovering)
                    return true;
            }
            // A hovered hotspot (only possible here when locked) gets a leave with point {0, 0}.
            G_bHotspotTracking = false;
            if (G_HoveredHotspot != nullptr)
            {
                int const Zero[2] = {0, 0};
                G_HoveredHotspot->bHovered = 0;
                G_HoveredHotspot->SendEvent(2, Zero);
            }
            G_HoveredHotspot = nullptr;
            return bHovering;
        }

        // USER_BUTTON0.
        bool OnButton(int const* Point)
        {
            // Only a button-down record (+0x18), and not while locked.
            if (HudCommandRecord()->bKeyDown == 0)
                return false;
            if (G_HotspotInputLock != 0)
                return false;
            G_HotspotLastCursor[0] = Point[0];
            G_HotspotLastCursor[1] = Point[1];
            G_bHotspotTracking = true;
            FHotspot* Hit = nullptr;
            FindHotspotAt(Point, &Hit);
            if (Hit != G_HoveredHotspot)
            {
                if (G_HoveredHotspot != nullptr)
                {
                    G_HoveredHotspot->bHovered = 0;
                    G_HoveredHotspot->SendEvent(2, Point);
                }
                G_HoveredHotspot = Hit;
                if (Hit == nullptr)
                    return false;
                Hit->bHovered = 1;
                G_HoveredHotspot->SendEvent(1, Point);
            }
            if (G_HoveredHotspot == nullptr)
                return false;
            G_HoveredHotspot->SendEvent(3, Point);
            return true;
        }
    };

    FGameflowHotspotHandler G_GameflowHotspotHandler;

    // FUN_00558ac0 (cdecl, double) -- the base's world tick.
    void TickGameflow(double DeltaSeconds)
    {
        // Whole milliseconds are floor(sum * 1000.0) of the unrounded sum; the rest is the sum
        // rounded to float, less ms * 0.001.
        double const Sum = static_cast<double>(G_GameflowMsRemainder) + DeltaSeconds;
        float const SumAsFloat = static_cast<float>(Sum);
        double const Milliseconds = floor(Sum * 1000.0);
        G_GameflowMsRemainder =
            static_cast<float>(static_cast<double>(SumAsFloat) - Milliseconds * 0.001);
        G_GfScriptManager.Update(static_cast<int>(Milliseconds)); // FUN_00456650

        float const Dt = static_cast<float>(DeltaSeconds);
        TickClientLocationController(Dt); // FUN_00439540 on DAT_00668708

        // RoomObject slot 5 for every object whose +0x04 is clear.
        std::list<FRoomObject*>& Objects = GetRoomObjectList();
        for (std::list<FRoomObject*>::iterator It = Objects.begin(); It != Objects.end(); ++It)
        {
            FRoomObject* const Object = *It;
            if (Object->bNoUpdate == 0)
                Object->Update(DeltaSeconds);
        }

        // FUN_0041a140 -- the letterbox update for the room camera (the twin of FUN_0041a420,
        // which the space camera uses).
        UpdateRoomLetterbox(
            static_cast<FRoomCamera*>(static_cast<ICamera*>(GetClientRoomCamera())), Dt);
        UpdateFade(Dt);              // FUN_00412360
        RefreshHoveredHotspot();     // FUN_00558f80

        // With the letterbox and the fade all exactly 0.0 (x87 equality: a NaN does not pass), a
        // current room runtime whose +0x1338 is clear, and the software cursor not drawn: draw
        // it again.
        if (!(GetLetterboxTarget() == 0.0f))
            return;
        if (!(GetLetterboxAmount() == 0.0f))
            return;
        if (!(GetFadeTarget() == 0.0f))
            return;
        if (!(GetFadeLevel() == 0.0f))
            return;
        // FUN_00438c00: FUN_0043a460's lookup through the controller's static +0x38.
        FRoomRuntime* const Runtime = static_cast<FRoomRuntime*>(GetClientCurrentLocationRuntime());
        if (Runtime == nullptr)
            return;
        if (RoomControlIsCameraMoving(Runtime) != 0)
            return;
        if (GetClientMouseCursorDrawFlag() != 0)
            return;
        SetClientMouseCursorDrawFlag(1);
    }

    // FUN_00558c10 (cdecl) -- the base frame: tick, draw, sound, and the next state code.
    int UpdateGameflowBaseFrame()
    {
        // Zeroed while the frame counter has not passed DAT_006799e8 (unsigned).
        double Dt = GetClientFrameTimeCapped();
        if (!(GetClientFrameCount() > G_GameflowHoldUntilFrame))
            Dt = 0.0;

        // FUN_0042d740 holds the world -> only the hover is dropped.
        if (!ShouldSkipLocalServerTick())
        {
            TickGameflow(Dt);
            TickMaterialAnimationPlayback(static_cast<float>(Dt)); // FUN_0041b2e0
        }
        else
        {
            ReleaseHoveredHotspot(); // FUN_00558e90
        }

        if (BeginClientRenderFrame())
        {
            Win3DSetViewport(nullptr, kSupportCpp, 0x222);  // FUN_00422610
            Win3DSetPipelineClearColor(0.0f, 0.0f, 0.0f, 1.0f); // FUN_004214d0
            ClearWin3DBuffers(kWin3DClearColour | kWin3DClearDepth); // FUN_004215e0(0x4100)
            DrawClientLocation();                           // FUN_0043bbc0 on DAT_00668708
            ClearWin3DBuffers(kWin3DClearDepth);            // FUN_004215e0(0x100)
            Win3DSetDepthFunc(4);                           // FUN_00422120
            Win3DDrawAll();                                 // FUN_0059e300
            if (!IsClientServerTickPaused())                // FUN_0042d730
                DrawFade(static_cast<ICamera*>(GetClientRoomCamera())); // FUN_00412430
            EndClientRenderScene();                         // FUN_00425080
            PresentClientRenderFrame();                     // FUN_00425140
        }

        LoadingScreenReset(); // FUN_00579190

        SetClientAudioMode(3); // FUN_00428030
        TickClientSoundMixer(GetClientRoomCamera(), static_cast<float>(Dt)); // FUN_00428040

        int Result = 0;
        if (G_bGameflowMissionBriefing)
            Result = 8;
        else
            Result = G_bGameflowQuit ? 1 : 0;
        if (G_bGameflowStateFive)
        {
            G_bGameflowStateFive = false;
            Result = 5;
        }
        return Result;
    }

    // --------------------------------------------------------------------------------------
    // The GF state object (DAT_006686a0), RTTI .?AUGF@@ (COL 0x005e8734), vftable 0x005cae24,
    // fifteen slots:
    //   0 LAB_00437150  1 LAB_004371e0  2 LAB_00437200  3 LAB_00437230  4 LAB_0043d160
    //   5 LAB_00437260  6 0x0053e4a0    7 LAB_0051ca40  8 LAB_00437270  9 LAB_00437290
    //   10 0x00401e90   11 LAB_00437400 12 LAB_004372a0 13 FUN_004372c0 14 LAB_004372e0
    // --------------------------------------------------------------------------------------
    struct GF : public IEnvironment
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

        // Its static initializer LAB_00437140 only stamps the vptr; the three bytes start at 0
        // (.bss).
        unsigned char bFirstFramePending = 0; // +0x04
        unsigned char bBaseEntered = 0;       // +0x05
        unsigned char bFromIntro = 0;         // +0x06
    };

    // Slot 0 (LAB_00437150) -- a key message. With the "CTID" window up (FUN_0046a480) it is
    // consumed at once. Every arm of retail's switch on Message (some ask FUN_0044fd00, always
    // false) ends in the two GetKeyState calls, their results dropped, and true.
    bool GF::OnKeyMessage(unsigned A, unsigned Message, unsigned B, unsigned C)
    {
        (void)A;
        (void)Message;
        (void)B;
        (void)C;
        if (Win3DFindByName("CTID") != nullptr) // FUN_0059da10
            return true;
        GetKeyState(VK_SHIFT);
        GetKeyState(VK_CONTROL);
        return true;
    }

    // Slot 1 (LAB_004371e0) -- FUN_0044fd00: false.
    bool GF::Slot01(unsigned A, unsigned B, unsigned C)
    {
        (void)A;
        (void)B;
        (void)C;
        return false;
    }

    // Slot 2 (LAB_00437200) -- a mouse move: true whatever FUN_00558e00 said.
    bool GF::OnMouseMove(unsigned A, unsigned B, unsigned long Packed)
    {
        (void)A;
        (void)B;
        int Point[2] = {0, 0};
        UnpackCursorPoint(Point, Packed);
        TrackHotspotMouseMove(Point);
        return true;
    }

    // Slot 3 (LAB_00437230) -- a mouse button: FUN_00558ed0 is the answer.
    bool GF::OnMouseButton(unsigned A, unsigned Message, unsigned B, unsigned long Packed)
    {
        (void)A;
        (void)B;
        int Point[2] = {0, 0};
        UnpackCursorPoint(Point, Packed);
        return TrackHotspotMouseButton(Message, Point);
    }

    // Slot 4 (LAB_0043d160, shared with MissionBriefing).
    bool GF::Slot04(unsigned A, unsigned B, unsigned C)
    {
        (void)A;
        (void)B;
        (void)C;
        return true;
    }

    // Slot 5 (LAB_00437260) -- FUN_00558e90.
    void GF::Slot05()
    {
        ReleaseHoveredHotspot();
    }

    // Slot 6 (0x0053e4a0) -- empty (the ICF-folded body IDPMsgHandler::OnAddHost shares).
    void GF::Slot06(unsigned A)
    {
        (void)A;
    }

    // Slot 7 (LAB_0051ca40) -- the environment type FUN_005b0880 and the sound frame read
    // (IntroFocus answers 2).
    int GF::GetEnvironmentType()
    {
        return 1;
    }

    void GF::PreInit()
    {
        // LAB_00437270: FUN_00558850(DAT_0067eca0).
        InitGameflowData();
        bFirstFramePending = 0;
        bBaseEntered = 0;
    }

    void GF::Shutdown()
    {
        // LAB_00437290: FUN_00558870.
        ShutdownGameflow();
    }

    // Slot 10 (0x00401e90) -- empty. Code 4 of the state machine calls it.
    void GF::Slot10()
    {
    }

    // Slot 11 (LAB_00437400) -- FUN_00558d60(Width, Height), which reads neither.
    void GF::Resize(int Width, int Height)
    {
        (void)Width;
        (void)Height;
        ResetGameflowRoomLetterbox();
    }

    bool GF::Init(int Param)
    {
        // LAB_004372a0. Only the argument's low byte is read (FUN_005b2a30 case 3 pushes a
        // dword whose low byte it set).
        bFirstFramePending = 1;
        bBaseEntered = 0;
        bFromIntro = static_cast<unsigned char>(Param);
        return true;
    }

    void GF::Destroy()
    {
        // FUN_004372c0.
        LeaveGameflowBase();  // FUN_00558a50
        NnSystemShutdown();   // FUN_0045a140
        bFirstFramePending = 0;
    }

    int GF::Update(float DeltaSeconds)
    {
        // LAB_004372e0. The argument is not read.
        (void)DeltaSeconds;
        GF* const State = this;
        if (State->bFirstFramePending == 1)
        {
            // The first frame enters the base.
            State->bFirstFramePending = 0;
            RefreshLoadingProgress();
            // FUN_0045a100 -- the NN system (NN_MainButton and the voice init).
            NnSystemInit(true);
            RefreshLoadingProgress();

            unsigned const Base = GetPlayerBaseIdRetail();        // *FUN_004c4900()
            unsigned Location = GetPlayerLocationIdRetail();  // FUN_004c4a00
            if (!SinglePlayer() && Location != 0)
            {
                FDUMP(0x100001, "%s(%d) : *** ERROR: Invalid non-empty location %d in multiplayer\n",
                      kGameflowCpp, 0xa7, Location);
                Location = 0;
            }
            if ((Base & 0x3fffffffu) != 0)
                EnterGameflowBase(Base, Location, State->bFromIntro != 0);
            RefreshLoadingProgress();
            State->bBaseEntered = 1;
            if (!SinglePlayer() && !ClientQuitRequested())
            {
                // FUN_0046a4a0 -- DAT_0066da88 = 1 and the chat window DAT_0066da80's
                // +0x6c |= 3.
                ChatShowMsgWin(true);
            }
            RefreshLoadingProgress();
        }

        if (!ClientQuitRequested() && State->bBaseEntered == 1)
            return UpdateGameflowBaseFrame();

        // A black frame -- FUN_00564f90 outside the scene, then the scene with the Win3D roots
        // unless the client is quitting.
        unsigned char const Black[4] = {0, 0, 0, 0xff};
        ClearWin3DBuffersToColor(Black);
        if (BeginClientRenderFrame())
        {
            if (!ClientQuitRequested())
                Win3DDrawAll();
            EndClientRenderScene();
            PresentClientRenderFrame();
        }
        LoadingScreenReset(); // FUN_00579190
        return 0;
    }

    // DAT_006686a0. Its static initializer LAB_00437140 only stamps the vptr 0x005cae24.
    GF G_Gameflow;
    static_assert(sizeof(void*) != 4 || offsetof(GF, bFirstFramePending) == 0x04, "GF +0x04");
    static_assert(sizeof(void*) != 4 || offsetof(GF, bFromIntro) == 0x06, "GF +0x06");

    // DAT_00667e50 (0x804 bytes). See ResetEngineInstanceCache in gameflow.h.
    struct FEngineInstanceCache
    {
        // LAB_00434920 (static init), with atexit LAB_00434950 -- the destructor below.
        FEngineInstanceCache()
        {
            for (int Index = 0; Index < 0x100; ++Index)
            {
                Handles[Index] = G_InvalidEngineHandle;
                Entries[Index] = nullptr;
            }
            Count = 0;
        }

        // LAB_00434950: FUN_004348b0's body on this table.
        ~FEngineInstanceCache()
        {
            Reset();
        }

        // FUN_004348b0. Count is re-read after every release; the handle goes to IEngine slot
        // 15 without a -1 test.
        void Reset()
        {
            for (int Index = 0; Index < Count; ++Index)
            {
                Archetype::ReleaseMaterialEntrySlot(&Entries[Index]); // FUN_0041b030
                DALib::Engine->ReleaseHandle(Handles[Index]);         // vt+0x3c
            }
            for (int Index = 0; Index < 0x100; ++Index)
            {
                Handles[Index] = G_InvalidEngineHandle;
                Entries[Index] = nullptr;
            }
            Count = 0;
        }

        void* Handles[0x100];                         // +0x000
        Archetype::FCliMaterialEntry* Entries[0x100]; // +0x400 (DAT_00668250)
        int Count;                                    // +0x800 (DAT_00668650)
    };

    FEngineInstanceCache G_EngineInstanceCache; // DAT_00667e50
}

void ResetEngineInstanceCache()
{
    // FUN_004348b0 (thiscall, ECX = 0x00667e50 at every call site).
    G_EngineInstanceCache.Reset();
}

IEnvironment* GetGameflowState()
{
    // FUN_00437420.
    return &G_Gameflow;
}

void InitGameflowData()
{
    // FUN_00558850. Retail first calls an ICF-folded empty body (IDPMsgHandler::OnDisconnect).
    BuildClientLocationMaps();   // FUN_004393b0 on DAT_00668708
    LoadCharacterInfo();         // FUN_004354e0
    PreloadCharacterScripts();   // FUN_004355e0
}

bool ShutdownGameflow()
{
    // FUN_00558870.
    ShutdownCharacterInfo();     // FUN_00435500
    ClearClientLocationMaps();   // FUN_00439260 on DAT_00668708
    ClearRoomObjectList();       // FUN_00559380
    ResetEngineInstanceCache();  // FUN_004348b0 on DAT_00667e50
    return ReportLeakedHotspots(); // FUN_00556200
}

bool EnterGameflowBase(unsigned BaseId, unsigned LocationId, bool bFromIntro)
{
    // FUN_005588a0.
    LoadingScreenStart();                     // FUN_00579290
    ResetClientLocationController();         // FUN_00439200 on DAT_00668708
    SetClientLocationInTransition();          // DAT_00668761 (controller +0x59) = 1
    G_GameflowArrivalDelayMs = 0;             // DAT_006799e4
    SetPlayerBaseIdRetail(BaseId);            // FUN_004c4910(&BaseId)
    SetPlayerLocationIdRetail(LocationId); // FUN_004c4a10(LocationId)
    SetGameflowLocationBase(BaseId);          // FUN_0043d590
    LoadFlashlightTextures();                 // FUN_00557550
    PreloadClientSoundArchesByCategory(2);    // FUN_0042acb0
    RefreshLoadingProgress();
    SetClientAudioMode(3);                    // FUN_00428030(3)
    RefreshLoadingProgress();
    SetFadeImmediate(1.0f);                   // FUN_00412070(1.0f)
    // FUN_00412050(0, (float)(1.0 / DAT_0061650c)).
    SetFadeTarget(0.0f, static_cast<float>(1.0 / static_cast<double>(GetFadeDuration())));
    RefreshLoadingProgress();
    ResetHotspotTracking();                   // FUN_00558db0
    MouseSetCursor("arrow", false);           // FUN_0041dde0("arrow", 0)
    RefreshLoadingProgress();
    G_GfScriptManager.RegisterEvents();       // FUN_004564d0 on DAT_0066d2d0
    G_GameflowMsRemainder = 0.0f;             // DAT_006799e0
    RefreshLoadingProgress();
    G_bGameflowMissionBriefing = false;       // DAT_006799dc
    G_bGameflowStateFive = false;             // DAT_006799dd
    G_bGameflowQuit = false;                  // DAT_006799de
    RoomControlSetLandingPending(bFromIntro ? 0 : 1); // FUN_004489f0
    // DAT_00668760 = 1 -- the LocationController's +0x58, which the next FUN_0043bab0 reads and
    // clears.
    SetClientLocationKeepAudio();
    RefreshLoadingProgress();

    if (LocationId == 0)
    {
        EnterClientLocationBase(BaseId);      // FUN_0043b290
    }
    else
    {
        SetClientLocationBase(BaseId);        // FUN_0043b570
        // FUN_0043bab0 (room audio) then FUN_0043b6b0 (the location).
        ApplyClientLocationAudio(LocationId);
        ChangeClientLocation(LocationId);
    }
    RefreshLoadingProgress();

    // The arrival caption, unless the room runtime's +0x517 is set or the base was entered from
    // the intro. A runtime whose FUN_0044ab60 is false zeroes the delay first.
    FRoomRuntime* const Runtime = static_cast<FRoomRuntime*>(GetClientCurrentLocationRuntime());
    bool const bShowCaption = !bFromIntro;
    if ((Runtime == nullptr || RoomControlStartScriptRan(Runtime) == 0) && bShowCaption)
    {
        if (Runtime != nullptr && !RoomControlHasLandingScript(Runtime))
            G_GameflowArrivalDelayMs = 0;
        // The delay is loaded unsigned and multiplied by the float 0.001f.
        float const DelaySeconds = static_cast<float>(
            static_cast<double>(G_GameflowArrivalDelayMs) * static_cast<double>(0.001f));
        NnArrivalTextShowBase(DelaySeconds);  // FUN_0045b2d0
    }
    RefreshLoadingProgress();
    SetClientFpsCap(RtcSlider::get_fps_cap()); // DAT_00610a0c
    return true;
}

void RequestGameflowMissionBriefing()
{
    // FUN_00558a30.
    SetFadeImmediate(1.0f);
    G_bGameflowMissionBriefing = true;
}

void LeaveGameflowBase()
{
    // FUN_00558a50.
    SetClientFpsCap(RtcSlider::get_default_fps_cap()); // DAT_00610a0c
    LeaveClientLocationBase();                // FUN_0043b3e0 on DAT_00668708
    G_GfScriptManager.Shutdown();             // FUN_004564e0 on DAT_0066d2d0
    ReleaseSharedCharacterLod();              // FUN_00443e90 (DAT_00668948)
    ResetCharacterScripts();                  // FUN_00435630
    StopAllClientSounds();                    // FUN_004284f0
    UnloadClientSoundArchesByCategory(2);     // FUN_0042ad30
    ReleaseFlashlightTextures();              // FUN_005575e0
    ResetEngineInstanceCache();               // FUN_004348b0 on DAT_00667e50
    SetPlayerBaseIdRetail(0);                 // FUN_004c4910(&0)
    SetPlayerLocationIdRetail(0);         // FUN_004c4a10(0)
}

void SetGameflowArrivalDelayMs(unsigned DelayMs)
{
    G_GameflowArrivalDelayMs = DelayMs;
}

void HoldGameflowTime()
{
    // FUN_00558bf0.
    G_GameflowHoldUntilFrame = GetClientFrameCount() + 2;
}

void RequestGameflowStateFive()
{
    // FUN_00558c00.
    G_bGameflowStateFive = true;
}

void SetGameflowLocationBase(unsigned BaseId)
{
    // FUN_0043d590.
    G_GameflowLocationBase = BaseId;
}

unsigned GetGameflowLocationBase()
{
    // DAT_00668784 as LAB_0043d360 reads it (MissionBriefing state +0x10).
    return G_GameflowLocationBase;
}

unsigned char G_bMissionBriefingNavMapUp = 0;
unsigned char G_bMissionBriefingContinue = 0;

unsigned char IsMissionBriefingNavMapUp()
{
    // FUN_0043d570.
    return G_bMissionBriefingNavMapUp;
}

void RequestMissionBriefingContinue()
{
    // FUN_0043d580.
    G_bMissionBriefingContinue = 1;
}

void ResetGameflowRoomLetterbox()
{
    // FUN_00558d60.
    void* const Camera = GetClientRoomCamera();
    if (Camera == nullptr)
        return;
    ForgetLetterboxCamera(); // FUN_0041a3d0
    // FUN_0041a140(Camera, 0.0f).
    UpdateRoomLetterbox(static_cast<FRoomCamera*>(static_cast<ICamera*>(Camera)), 0.0f);
}

void ResetHotspotTracking()
{
    // FUN_00558db0.
    G_bHotspotTracking = false;
    G_HoveredHotspot = nullptr;
    G_HotspotInputLock = 0;
}

void ClearHoveredHotspot()
{
    // FUN_00558dd0.
    G_HoveredHotspot = nullptr;
}

void ClearHoveredHotspotIf(FHotspot* Hotspot)
{
    // FUN_00558de0.
    if (Hotspot == G_HoveredHotspot)
        G_HoveredHotspot = nullptr;
}

void ReleaseHoveredHotspot()
{
    // FUN_00558e90.
    G_bHotspotTracking = false;
    if (G_HoveredHotspot != nullptr)
    {
        int const Zero[2] = {0, 0};
        G_HoveredHotspot->bHovered = 0;
        G_HoveredHotspot->SendEvent(2, Zero);
    }
    G_HoveredHotspot = nullptr;
}

void LockHotspotInput()
{
    // FUN_00559290.
    G_HotspotInputLock = 1;
}

void UnlockHotspotInput()
{
    // FUN_005592a0.
    G_HotspotInputLock = 0;
}
