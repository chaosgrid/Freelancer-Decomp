// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Space/space_state.h"
#include "Space/space_app.h"

#include "GameFlow/character_info.h"         // ResetCharacterScripts (FUN_00435630)
#include "NeuroNet/Chat/chat_init.h"              // ChatShowMsgWin (FUN_0046a4a0)
#include "Network/login_mgr.h"              // WaitForServerResponse (FUN_005a89b0), DAT_0067ecbe
#include "Core/main_command_handler.h"   // SetClientQuitRequested (DAT_0067ecbd)
#include "NeuroNet/nn_arrival_text.h"        // NnArrivalTextShowSystemNow (FUN_0045b490)
#include "Space/space_scene_load.h"       // FUN_005020a0 / FUN_00510f90
#include "Space/HUD/hud_target.h"             // HudIsMousePickBlocked (FUN_0045a740)
#include "common_misc.h"            // Geometry::Sphere, SinglePlayer, GetFileSysPath, GetFullFilename
#include "common_universe.h"        // ISpatialPartition, SpatialFill
#include "dacom.h"                  // FDUMP
#include "Interface/Pages/character_select_equip.h" // FUN_004c4a30 / FUN_004c4a60
#include "Space/Effects/client_equip_fx.h"
#include "Core/client_frame_tick.h"      // FUN_00424f30 / FUN_00425080 / FUN_00425140, FUN_0041b2e0
#include "Core/client_state.h"
#include "Core/client_tick_profile.h"    // Port-only owner-requested diagnostics
#include "Space/Effects/client_fuse.h"
#include "Space/client_gameobj.h"         // FClientGameobj (the player's object, FUN_0054a940)
#include "Space/client_objdb.h"
#include "Space/cockpit_load.h"
#include "Space/Effects/effect_type_list.h"
#include "Common/engine_light.h"
#include "Common/Graphics/fade_to_black.h"
#include "Core/game_options.h"
#include "Space/HUD/hud_command_dispatch.h"   // HudPostCommand (FUN_005763b0)
#include "Space/HUD/hud_element_list.h"
#include "Space/HUD/hud_status_core.h"        // GetHudStatus (DAT_00674aa0) +0x5b9 / +0x5ba
#include "Space/HUD/hud_targetable_objects.h" // FUN_004ec400
#include "Common/input_manager.h"
#include "GameFlow/gameflow.h"               // ResetEngineInstanceCache (FUN_004348b0 on DAT_00667e50)
#include "Space/Effects/gate_tunnel.h"            // FUN_004fe180
#include "Space/Effects/jump_effect.h"            // FUN_00502aa0
#include "Interface/loading_screen.h"         // LoadingScreenReset (FUN_00579190)
#include "Interface/menu_keymap_config.h"
#include "Common/Audio/menu_sound_arch.h"
#include "Interface/menu_ui_widgets.h"
#include "NeuroNet/nn_exit.h"                // NnExitPostShipLostMessage (FUN_00471e80)
#include "NeuroNet/nn_modal.h"
#include "Space/player_behavior.h"
#include "Space/player_spawn.h"
#include "Common/Audio/sound_config.h"
#include "Space/Cameras/space_camera.h"
#include "Space/Cameras/space_camera_game.h"
#include "Space/space_letterbox.h"        // IsLetterboxVisible / SetLetterboxTarget (FUN_0041a3e0 / 0041a0f0)
#include "Space/space_scene_obj_draw.h"   // LightGridRebuild (FUN_00537c30), SpaceDrawQueueRelease (FUN_0051fbc0)
#include "Space/star_sphere.h"
#include "Space/star_sys.h"
#include "Space/Effects/tradelane_fx.h"
#include "Interface/win3d.h"                  // Win3DDrawAll (FUN_0059e300)
#include "Interface/win3d_renderables.h"
#include "common_animdb.h"
#include "common_iface.h"
#include "common_physys.h"
#include "DALib.h"
#include "Core/globals.h"
#include "render/IRenderPipeline.h"
#include "Interface/win3d_render.h"
#include "common_cobject.h"
#include "common_ini.h"     // RefreshLoadingProgress
#include "common_server.h"  // IServer (DAT_0067ecd0)
#include "Network/server_connect.h" // GetLocalServerInterface

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

namespace
{
    // DAT_00679778: the value the space frame returns -- the next client state, 0 to stay.
    // The frame zeroes it and raises 6 from +0x0a; the space init zeroes it and FUN_0054bc60
    // stores 4.
    int G_SpaceNextState = 0;

    // DAT_00679784: the base a landing ship asked for (FUN_00548ef0). ProcessPendingSystemSwitch
    // reads it (FUN_004c4910 with it, FUN_004c4a10(0), next state 3, then zeroes it).
    unsigned G_PendingLandBase = 0;

    // DAT_0067977c / DAT_00679780: the system a jumping ship asked for and a byte beside it
    // (FUN_00548ed0). ProcessPendingSystemSwitch consumes the system and zeroes both; nothing
    // in retail reads DAT_00679780 (two writers only).
    unsigned G_PendingJumpSystem = 0;
    unsigned G_PendingJumpFlag = 0;

    // The player's object as a CEqObj through the watcher DAT_00679744: minus eight, the +0xc'd
    // pointer's +0x4, then the class mask (0x503 ship, 0x103 any CEqObj) -- the chain retail
    // inlines (FUN_0054b230 twice, the death camera, DestroySpaceState). Null when any link is
    // missing.
    CEqObj* PlayerEqObj(unsigned ClassMask)
    {
        void* const ObjRW = GetPlayerObjRW();
        if (ObjRW == nullptr || ObjRW == reinterpret_cast<void*>(-0xc))
            return nullptr;
        CEqObj* const EqObj =
            *reinterpret_cast<CEqObj**>(static_cast<unsigned char*>(ObjRW) + 0x10);
        if (EqObj == nullptr ||
            (static_cast<unsigned>(EqObj->ObjectClass) & ClassMask) != ClassMask)
            return nullptr;
        return EqObj;
    }

    // FUN_00543f00 (one caller): the docking port's camera hardpoint "HPDockCam" + ('A' +
    // PortIndex), read through the port's IObjInspect slot 48 get_hardpoint; true when that
    // answers 0.
    bool GetDockCameraCsys(IObjRW const* Port, int PortIndex, float* Csys12)
    {
        char Name[0x40];
        sprintf_s(Name, "%s%c", "HPDockCam", PortIndex + 0x41);
        return Port->get_hardpoint(Name, reinterpret_cast<Vector*>(Csys12 + 9),
                                   reinterpret_cast<Matrix*>(Csys12)) == 0;
    }

    // FUN_0054b230
    void UpdateSpaceDocking(SpaceApp* State)
    {
        if (State->bSpaceReady != 0)
        {
            // Once the ship is no longer excluded from control bit 4
            // the launch is over -- +0xba9 drops and, unless +0xba8 holds it, the camera goes
            // back to "player" (attach target 0).
            CEqObj* const EqObj = PlayerEqObj(0x503u);
            if (EqObj != nullptr && !EqObj->is_control_excluded(4u))
            {
                State->bSpaceReady = 0;
                if (State->bScriptCameraLatch == 0)
                    SwitchSpaceCameraMode("player", nullptr);
            }
        }
        else if (State->bDockingPending == 0)
        {
            // The object the active camera (not tested again after the frame's own test)
            // watches through its +0xc0 BaseWatcher, minus eight; null when nothing is watched.
            FGameCamera* const Cam = reinterpret_cast<FGameCamera*>(GetActiveSpaceCamera());
            Watchable const* const Watched = Cam->Watched.target();
            void const* const WatchedObjRW =
                Watched != nullptr ? reinterpret_cast<unsigned char const*>(Watched) - 8
                                   : nullptr;
            // The camera must be watching exactly the player's ship.
            CEqObj* const EqObj = PlayerEqObj(0x503u);
            if (EqObj != nullptr && WatchedObjRW == GetPlayerObjRW())
            {
                IObjRW const* Port = nullptr;
                int PortIndex = 0;
                State->bDockingPending = static_cast<char>(
                    EqObj->get_behavior_interface()->get_docking_port(Port, PortIndex) ? 1 : 0);
                if (State->bDockingPending != 0)
                {
                    // IServer slot 57, no test on the server.
                    static_cast<IServer*>(GetLocalServerInterface())
                        ->SPRequestInvincibility(static_cast<CSimple*>(EqObj)->Id, true,
                                                 INVINCIBILITY_REASON_2,
                                                 GetPlayerConnectionId());
                    // The port's type (skipped without a port) tested for 0x120.
                    unsigned PortType = 0;
                    if (Port != nullptr)
                        Port->get_type(PortType);
                    if ((PortType & 0x120u) != 0)
                    {
                        float Csys[12] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                          0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};
                        if (GetDockCameraCsys(Port, PortIndex, Csys))
                        {
                            // The body of FUN_0054bb90, inlined in retail.
                            SetDockingCameraTransform(Csys);
                        }
                    }
                    // HUD_Status is not null-tested.
                    if (GetHudStatus()->bRearView == 1)
                        HudPostCommand(USER_REARVIEW_CAMERA_MODE_OFF, 0);
                    if (GetHudStatus()->bTurretView == 1)
                        HudPostCommand(USER_AUTO_TURRET, 0);
                    if (!IsLetterboxVisible())
                    {
                        SetLetterboxTarget(1.0f, 3.0f);
                        SetClientMouseCursorDrawFlag(0);
                    }
                    EqObj->set_control_exclusion(4u);
                }
            }
        }

        // While docking with a player object, the ship Gameobj's +0x158 state (FUN_005437d0)
        // steers +0xbab: raised once the state leaves 0, and when it is back at 0 with +0xbab
        // raised both bytes drop.
        void* const PlayerObjRW = GetPlayerObjRW();
        if (State->bDockingPending != 0 && PlayerObjRW != nullptr)
        {
            FClientShipGameobj const* const Ship = static_cast<FClientShipGameobj const*>(
                static_cast<IObjRW const*>(PlayerObjRW));
            int const DockState = Ship->JumpState;
            if (State->bDockingActive != 0)
            {
                if (DockState == 0)
                {
                    State->bDockingActive = 0;
                    State->bDockingPending = 0;
                }
            }
            else if (DockState != 0)
            {
                State->bDockingActive = 1;
            }
        }
    }
}

// DAT_00678b98. Its static initializer LAB_00548f00 runs FUN_00549300 and registers
// LAB_00548f20 (-> FUN_00548f30) with atexit -- a C++ global with a ctor and a dtor.
SpaceApp G_SpaceApp;

SpaceApp::SpaceApp()
{
    // FUN_00549300. The Events member is constructed before this body.

    // The eight cameras, in this order.
    FirstPersonCameraConstruct(&FirstPerson); // FUN_00517180 on +0x00c
    ThirdPersonCameraConstruct(&ThirdPerson); // FUN_00518600 on +0x1b0
    TurretCameraConstruct(&Turret);           // FUN_0051a210 on +0x3c8
    RearViewCameraConstruct(&RearView);       // FUN_0051ad30 on +0x500
    DeathCameraConstruct(&Death);             // FUN_0051b2f0, 4 floats, vt 0x005e061c (+0x60c)
    ScriptCameraConstruct(&Script);           // FUN_0051e100 on +0x760
    StaticCameraConstruct(&Static);           // FUN_0051b2f0, vt 0x005e0594 (+0x9a4)
    DockingCameraConstruct(&Docking);         // FUN_0051ea20 on +0xa9c

    bCameraReady = 0;
    ActiveCamera = nullptr;
    bPlayerLostHandled = 0;
    bLeaveRequested = 0;

    // DAT_00679744 unhooked when it holds something.
    ClearPlayerShipWatcher();

    bDockingPending = 0;
    bDockingActive = 0;
    bSpaceReady = 0;
    bScriptCameraLatch = 0;
    bInitInProgress = 0;
    PlayerPreference = 1;
}

SpaceApp::~SpaceApp()
{
    // Member destruction runs Docking, Static, Script, Death, RearView, Turret, ThirdPerson,
    // FirstPerson, then Events, matching FUN_00548f30. Each HUD camera destroys its
    // EventHandler base before FGameCamera's watcher.
}

IEnvironment* GetSpaceState()
{
    // FUN_0054bd30
    return &G_SpaceApp;
}

// Slots 0, 2, 3 and 4 (LAB_0054a0e0, LAB_0054a210, LAB_0054a2e0, LAB_0054a450): vestigial
// pre-DirectInput window-message handlers that build a 0x28-byte input event and offer it to
// the active camera (+0xba0) slots 0x70 / 0x74 and the watched gameobj's slot 0x1cc. Port
// deviation: no retail code calls IInputEvent slots 0..6 on any IEnvironment state (only slots
// 7..14), so these overrides only fill the vtable slots and return false.
bool SpaceApp::OnKeyMessage(unsigned A, unsigned Message, unsigned B, unsigned C)
{
    (void)A;
    (void)Message;
    (void)B;
    (void)C;
    return false;
}

// Slot 1 (0x004b50e0): returns false.
bool SpaceApp::Slot01(unsigned A, unsigned B, unsigned C)
{
    (void)A;
    (void)B;
    (void)C;
    return false;
}

bool SpaceApp::OnMouseMove(unsigned A, unsigned B, unsigned long Packed)
{
    (void)A;
    (void)B;
    (void)Packed;
    return false;
}

bool SpaceApp::OnMouseButton(unsigned A, unsigned Message, unsigned B, unsigned long Packed)
{
    (void)A;
    (void)Message;
    (void)B;
    (void)Packed;
    return false;
}

bool SpaceApp::Slot04(unsigned A, unsigned B, unsigned C)
{
    (void)A;
    (void)B;
    (void)C;
    return false;
}

// Slot 5 (0x00401e90): empty.
void SpaceApp::Slot05()
{
}

// Slot 6 (0x0053e4a0): empty.
void SpaceApp::Slot06(unsigned A)
{
    (void)A;
}

// Slot 7 (0x004b5010): environment type 0.
int SpaceApp::GetEnvironmentType()
{
    return kEnvironmentSpace;
}

// Slot 8 (0x00401e90): empty (InitializeEngine calls it).
void SpaceApp::PreInit()
{
}

// Slot 9: FUN_0054ab20.
void SpaceApp::Shutdown()
{
    ShutdownSpaceState(this);
}

// Slot 10 (0x00401e90): empty (FUN_005b2a30 case 5 calls it).
void SpaceApp::Slot10()
{
}

// LAB_0054a540 -- slot 11. Its caller is the render surface reset. With an active camera: a
// local rectangle {0, 0, Width - 1, Height - 1} handed to the camera's slot 0x5c (the viewport
// apply). No active camera: nothing.
void SpaceApp::Resize(int Width, int Height)
{
    FSpaceCamera* const Active = ActiveCamera;
    if (Active == nullptr)
        return;
    int ViewRect[4];
    ViewRect[0] = 0;
    ViewRect[1] = 0;
    ViewRect[2] = Width - 1;
    ViewRect[3] = Height - 1;
    ((FGameCamera*)Active)->ApplyViewport(ViewRect);
}

// Slot 12: FUN_0054ab40.
bool SpaceApp::Init(int Param)
{
    return InitSpaceState(this, Param);
}

// Slot 13: FUN_0054aec0.
void SpaceApp::Destroy()
{
    DestroySpaceState(this);
}

// Slot 14: LAB_0054b000.
int SpaceApp::Update(float DeltaSeconds)
{
    return UpdateSpaceState(this, DeltaSeconds);
}

namespace
{
    // The two-way tail LAB_0054a6b3 / LAB_0054a77b of the face's handler: back to "turret" when
    // the turret view is still up (HUD_Status +0x5b9), else to "player" -- each only when the
    // controller byte +0x08 is up -- then HUD_Status FUN_004dca20.
    void SpaceAppRestoreViewAfterRear(SpaceApp* App)
    {
        HUD_Status* const Status = GetHudStatus();
        if (Status->bTurretView != 0)
        {
            if (App->bCameraReady != 0)
                SwitchSpaceCameraMode("turret", nullptr);
        }
        else if (App->bCameraReady != 0)
        {
            SwitchSpaceCameraMode("player", nullptr);
        }
        GetHudStatus()->UpdateSpecialModeCaptions();
    }
}

bool FSpaceAppEventFace::OnEvent(unsigned MsgId)
{
    // LAB_0054a580 -- thiscall on the face (+4); retail reads every controller field through
    // face-relative offsets. Handled ids: 0x0f, 0x31, 0x38, 0x39, 0xbf; every other id answers
    // false.
    SpaceApp* const App = reinterpret_cast<SpaceApp*>(reinterpret_cast<unsigned char*>(this) -
                                                      offsetof(SpaceApp, Events));
    HUD_Status* Status = nullptr;

    switch (MsgId)
    {
    case USER_COCKPIT_CAMERA_MODE:
        // The first/third-person toggle, refused while the mouse pick is blocked
        // (FUN_0045a740).
        if (HudIsMousePickBlocked())
            return false;
        if (App->PlayerPreference == 0)
        {
            App->PlayerPreference = 1;
            if (App->bCameraReady != 0)
                SwitchSpaceCameraMode("third_person", nullptr);
            SetGameSessionFlags(8, false); // FUN_004c8120(8, 0)
            return true;
        }
        if (App->PlayerPreference == 1)
        {
            App->PlayerPreference = 0;
            if (App->bCameraReady != 0)
                SwitchSpaceCameraMode("first_person", nullptr);
            SetGameSessionFlags(8, true); // FUN_004c8120(8, 1)
            return true;
        }
        // Any other preference -- consumed, nothing done.
        return true;

    case USER_AUTO_TURRET:
        // The turret view toggle (HUD_Status +0x5b9), refused while the mouse pick is blocked.
        if (HudIsMousePickBlocked())
            return false;
        Status = GetHudStatus();
        Status->bTurretView = Status->bTurretView == 0 ? 1 : 0;
        if (GetHudStatus()->bTurretView != 0)
        {
            // The player's behaviour manager drops its steering direction.
            IBehaviorManager* const Manager =
                GetBehaviorManager(static_cast<IObjRW*>(GetPlayerObjRW()));
            if (Manager != nullptr)
                Manager->reset_current_behavior_direction();
            if (App->bCameraReady != 0)
                SwitchSpaceCameraMode("turret", nullptr);
        }
        else
        {
            // FUN_005763b0(0x36, 0), then "player".
            HudPostCommand(USER_LOOK_ROTATE_CAMERA_RESET, 0);
            if (App->bCameraReady != 0)
                SwitchSpaceCameraMode("player", nullptr);
        }
        GetHudStatus()->UpdateSpecialModeCaptions(); // FUN_004dca20
        return true;

    case USER_REARVIEW_CAMERA_MODE:
        // The rear view follows the key (the command record's key-down
        // byte, FUN_00576240 +0x18). With the mouse pick blocked only a rear view already up
        // gets through (to be released).
        if (HudIsMousePickBlocked() && GetHudStatus()->bRearView == 0)
            return false;
        GetHudStatus()->bRearView = HudCommandRecord()->bKeyDown != 0 ? 1 : 0;
        if (GetHudStatus()->bRearView != 0)
        {
            if (App->bCameraReady != 0)
                SwitchSpaceCameraMode("rearview", nullptr);
            GetHudStatus()->UpdateSpecialModeCaptions();
            return true;
        }
        // Released -- only when the rear-view camera is the active one
        // does the view go back.
        if (App->ActiveCamera != SpaceCameraOf(&App->RearView))
        {
            GetHudStatus()->UpdateSpecialModeCaptions();
            return true;
        }
        SpaceAppRestoreViewAfterRear(App);
        return true;

    case USER_REARVIEW_CAMERA_MODE_OFF:
        // Rear view off; the same tail as 0x38's release.
        GetHudStatus()->bRearView = 0;
        if (App->ActiveCamera != SpaceCameraOf(&App->RearView))
        {
            GetHudStatus()->UpdateSpecialModeCaptions();
            return true;
        }
        SpaceAppRestoreViewAfterRear(App);
        return true;

    case USER_CANCEL:
    {
        // Single player only, while docking: the player ship's docking port (the answer of
        // get_docking_port unread), that port's dock target, and when it names one IServer
        // slot 91 Dock (no test on the server).
        if (!SinglePlayer())
            return false;
        if (App->bDockingPending == 0)
            return false;
        CEqObj* const EqObj = PlayerEqObj(0x503u);
        if (EqObj == nullptr)
            return false;
        IBehaviorManager* const Manager = EqObj->get_behavior_interface();
        if (Manager == nullptr)
            return false;
        IObjRW const* Port = nullptr;
        int PortIndex = -1;
        Manager->get_docking_port(Port, PortIndex);
        if (Port == nullptr)
            return false;
        unsigned DockTarget = 0;
        Port->get_dock_target(DockTarget);
        if (DockTarget == 0)
            return false;
        const unsigned Connection = GetPlayerConnectionId();
        static_cast<IServer*>(GetLocalServerInterface())->Dock(DockTarget, Connection);
        return true;
    }

    default:
        return false;
    }
}

void SpaceAppLoadCamerasIni(SpaceApp* App)
{
    // FUN_00549830
    INI_Reader Reader;
    if (Reader.open(GetFullFilename("cameras.ini"), false))
    {
        while (Reader.read_header())
        {
            // [WinCamera] is the Win3D camera DAT_0067dbf8; the rest are the controller's own.
            FSpaceCamera* Camera = nullptr;
            if (Reader.is_header("WinCamera"))
                Camera = Win3DGetCamera();
            else if (Reader.is_header("CockpitCamera"))
                Camera = SpaceCameraOf(&App->FirstPerson);
            else if (Reader.is_header("ThirdPersonCamera"))
                Camera = SpaceCameraOf(&App->ThirdPerson);
            else if (Reader.is_header("TurretCamera"))
                Camera = SpaceCameraOf(&App->Turret);
            else if (Reader.is_header("RearViewCamera"))
                Camera = SpaceCameraOf(&App->RearView);
            else if (Reader.is_header("DeathCamera"))
                Camera = SpaceCameraOf(&App->Death);
            else
            {
                FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: Unknown camera type? (%s)",
                      "E:\\FL\\Scratch\\Source\\Client\\space\\space.cpp", 0x17e,
                      Reader.get_header_ptr());
                continue;
            }

            // Every entry to the camera's slot 0x4c (SetProperty). (The [WinCamera] path skips
            // the null test the others make -- they are never null.)
            while (Reader.read_value())
            {
                const char* const Name = Reader.get_name_ptr();
                const char* const Value = Reader.get_value_string();
                if (static_cast<unsigned char>(AsCamera(Camera)->SetProperty(Name, Value)) == 0)
                {
                    FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: [%s] unknown property '%s = %s'",
                          "E:\\FL\\Scratch\\Source\\Client\\space\\space.cpp", 0x18a,
                          Reader.get_header_ptr(), Name, Value);
                }
            }
        }
    }
}

void SpaceAppArmDeathCamera()
{
    // FUN_0054b900. The death camera is SpaceApp + 0x60c = 0x006791a4, so retail addresses its
    // fields as DAT_006791a8 (World), DAT_006791d8, DAT_006792b4 and so on.
    FDeathCamera* const Death = &G_SpaceApp.Death;

    // The client ObjDB's spatial bucket (slot 0xc, mask 0xfdff).
    ISpatialPartition* const Bucket =
        static_cast<ISpatialPartition*>(GetClientObjDb()->GetBucket(0xfdff));
    bool bCrowded = false;
    if (Bucket != nullptr)
    {
        // Everything within 50 (DAT_00614030) of the active camera's position (the camera is
        // not tested), capacity 0x800.
        Geometry::Sphere Query;
        const float* const CameraPos = AsCamera(G_SpaceApp.ActiveCamera)->GetPosition();
        Query.Center.x = CameraPos[0];
        Query.Center.y = CameraPos[1];
        Query.Center.z = CameraPos[2];
        Query.Radius = 50.0f;
        FSpatialQueryResult Result;
        Result.Capacity = 0x800;
        Result.Count = 0;
        if (SpatialFill(Bucket, &Query, &Result) != 0)
        {
            // Something close -- the rear view and the turret view closed when up, and the
            // drift limited to 100.
            if (GetHudStatus()->bRearView == 1)
                HudPostCommand(USER_REARVIEW_CAMERA_MODE_OFF, 0);
            if (GetHudStatus()->bTurretView == 1)
                HudPostCommand(USER_AUTO_TURRET, 0);
            Death->MaxDriftSpeed = 100.0f;
            bCrowded = true;
        }
        else
        {
            // Nothing close -- only the rear view is closed.
            if (GetHudStatus()->bRearView == 1)
                HudPostCommand(USER_REARVIEW_CAMERA_MODE_OFF, 0);
        }
    }
    if (!bCrowded)
        Death->MaxDriftSpeed = 200.0f;

    // A first-person view goes to "third_person" on the player's object first.
    Death->DriftDamping = 0.5f;
    if (G_SpaceApp.ActiveCamera == SpaceCameraOf((FHudCamera*)&G_SpaceApp.FirstPerson))
        SwitchSpaceCameraMode("third_person", GetPlayerObjRW());

    // The listener camera's (FUN_0040f130) transform into the death camera's World, its 3x3
    // into the shake matrix, its position into the drift accumulator.
    FSpaceCamera* const Listener = static_cast<FSpaceCamera*>(GetClientSoundListenerObject());
    const float* const Src = AsCamera(Listener)->GetOrientation();
    FSpaceCamera* const DeathBase = SpaceCameraOf((FGameCamera*)Death);
    DeathBase->bViewDirty = 1;
    memcpy(DeathBase->World, Src, 9 * sizeof(float));
    memcpy(DeathBase->WorldPos, Src + 9, 3 * sizeof(float));
    memcpy(Death->ShakeOrientation, Src, 9 * sizeof(float));
    Death->DriftPosition[0] = Src[9];
    Death->DriftPosition[1] = Src[10];
    Death->DriftPosition[2] = Src[11];
    Death->ShakeTimer = Death->ShakeDuration;
    Death->DriftSpeed = 1.0f;

    // The player's object narrowed with mask 0x103 (a CEqObj); control exclusion 3 set unless
    // it already is.
    CEqObj* const EqObj = PlayerEqObj(0x103u);
    if (EqObj != nullptr && !EqObj->is_control_excluded(3u))
        EqObj->set_control_exclusion(3u);

    // The player's object is re-read.
    SwitchSpaceCameraMode("death", GetPlayerObjRW());
}

void SetStaticCameraTransform(const float* Csys12)
{
    // FUN_0054bb50. The Csys and the view-dirty byte are written before the guard.
    FSpaceCamera* const Static = SpaceCameraOf((FGameCamera*)&G_SpaceApp.Static);
    Static->bViewDirty = 1;
    memcpy(Static->World, Csys12, 9 * sizeof(float));
    memcpy(Static->WorldPos, Csys12 + 9, 3 * sizeof(float));

    if (G_SpaceApp.bCameraReady != 0)
        SwitchSpaceCameraMode("static", nullptr);
}

void SetPendingLandBase(unsigned const* BaseId)
{
    // FUN_00548ef0: no null test.
    G_PendingLandBase = *BaseId;
}

void EnterBaseFromDock(unsigned const* BaseId)
{
    // FUN_0054bc60
    SetPlayerBaseIdRetail(*BaseId);
    SetPlayerLocationIdRetail(0);
    G_SpaceNextState = 4;
}

void SetPendingJumpSystem(unsigned const* SystemId, bool bFlag)
{
    // FUN_00548ed0: no null test.
    G_PendingJumpSystem = *SystemId;
    G_PendingJumpFlag = bFlag ? 1u : 0u;
}

namespace
{
    // FUN_0054a940 (thiscall on the space state; `this` is never read). Called once per space
    // frame.
    void ProcessPendingSystemSwitch()
    {
        if (G_PendingJumpSystem != 0)
        {
            // The player's object when its class has bit 0 set, else null -- used below with no
            // test.
            IObjRW* const PlayerRW = static_cast<IObjRW*>(GetPlayerObjRW());
            CObject* Object = nullptr;
            if (PlayerRW != nullptr)
            {
                CObject* const Candidate = static_cast<FClientGameobj*>(PlayerRW)->GetObject();
                if (Candidate != nullptr &&
                    (static_cast<unsigned>(Candidate->ObjectClass) & CObject::ClassObject) == CObject::ClassObject)
                {
                    Object = Candidate;
                }
            }

            RefreshLoadingProgress();

            // The re-make parameters -- mass 1.0f, the default
            // dampings, system 0, type 2, flag 0, collisions on, empty name, no inertia.
            PhySys::CreateParms Parms;
            Parms.Mass = 1.0f;
            Parms.LinearDamping = PhySys::DEFAULT_LINEAR_DAMPING;
            Parms.AngularDamping[0] = PhySys::DEFAULT_ANGULAR_DAMPING.x;
            Parms.AngularDamping[1] = PhySys::DEFAULT_ANGULAR_DAMPING.y;
            Parms.AngularDamping[2] = PhySys::DEFAULT_ANGULAR_DAMPING.z;
            Parms.SystemId = 0;
            Parms.Type = 2;
            Parms.bPhysicalUnmoveable = 0;
            Parms.bEnableCollisions = 1;
            Parms.Name[0] = '\0';
            Parms.bHasInertia = 0;
            Parms.Inertia.x = 0.0f;
            Parms.Inertia.y = 0.0f;
            Parms.Inertia.z = 0.0f;

            Parms.Mass = Object->get_mass();
            Parms.bHasInertia = 1;

            // The moment of inertia when the object has a body (+0x54), else zero.
            Vector Inertia;
            if (Object->BodyPtr != nullptr)
            {
                Inertia = PhySys::GetMomentOfInertia(Object);
            }
            else
            {
                Inertia.x = 0.0f;
                Inertia.y = 0.0f;
                Inertia.z = 0.0f;
            }
            Parms.Inertia = Inertia;

            // The collision state, the dampings and the collision group name (strcpy, unbounded,
            // as retail) off the object as it is now.
            Parms.bEnableCollisions = PhySys::GetCollisionState(Object) ? 1 : 0;
            Vector AngularDamping;
            AngularDamping.x = Parms.AngularDamping[0];
            AngularDamping.y = Parms.AngularDamping[1];
            AngularDamping.z = Parms.AngularDamping[2];
            PhySys::GetDamping(Object, Parms.LinearDamping, AngularDamping);
            Parms.AngularDamping[0] = AngularDamping.x;
            Parms.AngularDamping[1] = AngularDamping.y;
            Parms.AngularDamping[2] = AngularDamping.z;
            strcpy(Parms.Name, PhySys::GetCollisionGroup(Object));

            // Out of the physics, out of the old system with the player gameobj, the new system
            // recorded (FUN_004c4840) and entered.
            Object->unmake_physical();
            StarSysLeave(PlayerRW);
            SetPlayerStarSystemIdRetail(G_PendingJumpSystem);
            StarSysEnter(G_PendingJumpSystem);

            // Back into the physics in the new system, radius 0.
            Parms.SystemId = G_PendingJumpSystem;
            Object->remake_physical(Parms, 0.0f);

            // IServer slot 47.
            static_cast<IServer*>(GetLocalServerInterface())
                ->SystemSwitchOutComplete(GetPlayerShipId(), GetPlayerConnectionId());

            G_PendingJumpFlag = 0;
            G_PendingJumpSystem = 0;
            return;
        }

        // A landing ship's base: next state 3 (enter the base), and the request consumed.
        if (G_PendingLandBase != 0)
        {
            SetPlayerBaseIdRetail(G_PendingLandBase);
            SetPlayerLocationIdRetail(0);
            G_SpaceNextState = kClientStateBase;
            G_PendingLandBase = 0;
        }
    }
}

namespace
{
    // A local of FUN_0054ab40: a pointer to a byte that is raised on construction and dropped
    // on scope exit. FUN_0054aeb0 is its destructor, reached only through the unwind funclet;
    // both normal exits inline it.
    struct FScopedFlagByte
    {
        explicit FScopedFlagByte(unsigned char* InFlag)
            : Flag(InFlag)
        {
            *Flag = 1;
        }

        ~FScopedFlagByte()
        {
            // FUN_0054aeb0
            *Flag = 0;
        }

        FScopedFlagByte(FScopedFlagByte const&) = delete;
        FScopedFlagByte& operator=(FScopedFlagByte const&) = delete;

        unsigned char* Flag;
    };
}

bool InitSpaceState(SpaceApp* State, int Param)
{
    // FUN_0054ab40 -- slot 12; the argument is never read.
    (void)Param;

    // +0x0b raised for the whole init (FUN_0054bd40 reads it) and dropped by the guard's
    // destructor on every exit, the exception path included.
    FScopedFlagByte InitInProgress(&State->bInitInProgress);

    // The loading screen (FUN_00579290), the NN system (FUN_0045a100(1)).
    LoadingScreenStart();
    NnSystemInit(true);
    RefreshLoadingProgress();

    State->bCameraReady = 1;
    RegisterAcceleratorGroup("IDR_SPACE");

    // IServer slot 64, no test on the server.
    static_cast<IServer*>(GetLocalServerInterface())->RequestCreateShip(GetPlayerConnectionId());

    ClearPlayerCreateShipResponse();
    WaitForServerResponse(GetPlayerCreateShipAnsweredByte(), 5.0f, 60.0f);

    // No answer -- single player raises DAT_0067ecbd (leave the frame loop), multiplayer
    // DAT_0067ecbe (disconnected); next state 0, return 1.
    if (*GetPlayerCreateShipAnsweredByte() == 0)
    {
        if (SinglePlayer())
            SetClientQuitRequested(true);
        else
            G_bServerDisconnected = true;
        G_SpaceNextState = 0;
        return true;
    }

    // LoadSpaceSceneSlice is FUN_005020a0, FUN_00510f90, RefreshLoadingProgress.
    RefreshLoadingProgress();
    LoadSpaceSceneSlice();

    // Not reproduced: DAT_00679788 is a pending background loader -- waited for, deleted and
    // cleared when set. Nothing in the exe ever stores one, so the other branch is the only one
    // retail takes.
    // Then lightanim.ini (FUN_00538b20), the fuse DB (FUN_0052a7a0), the gate tunnels from
    // [Data] gate_tunnels (DAT_0067a22c, FUN_004fe1c0), FUN_0042acb0(1).
    {
        char LightAnimPath[MAX_PATH];
        GetFileSysPath(LightAnimPath, "fx\\lightanim.ini", nullptr);
        LightAnimationLoad(LightAnimPath);
    }
    ClientFuseDB::LoadAll();
    {
        char GateTunnelPath[MAX_PATH];
        GetFileSysPath(GateTunnelPath, g_display.gate_tunnels.Text, nullptr);
        GateTunnelIniLoad(GateTunnelPath);
    }
    PreloadClientSoundArchesByCategory(1);

    RefreshLoadingProgress();
    StarSysEnter(GetPlayerStarSystemIdRetail());
    RefreshLoadingProgress();

    // FUN_0054b520, the result dropped.
    SpawnPlayerShip();

    // IServer slot 11.
    RefreshLoadingProgress();
    static_cast<IServer*>(GetLocalServerInterface())->Update();
    RefreshLoadingProgress();

    // The cockpit ini, the four cockpit camera loaders, FUN_004c73d0.
    InitPlayerCockpitSlice();

    // FUN_00549bc0(this, 0, 0), then the cockpit shake amplitude DAT_00675294 = 0.
    State->bDockingPending = 0;
    RefreshLoadingProgress();
    InstallSpaceCamera(nullptr, false);
    ResetCameraShakeAmount();
    RefreshLoadingProgress();

    SpaceAppLoadCamerasIni(State);
    RefreshLoadingProgress();

    // FUN_004120d0(1) -- fade in from a covered screen.
    FadeIn(true);

    State->bPlayerLostHandled = 0;
    State->bLeaveRequested = 0;

    // Registered on the Event::Handler face (FUN_005760d0), in this order.
    {
        const unsigned EventIds[7] = {0xc3, 0xc4, 0xf, 0x31, 0x39, 0x38, 0xbf};
        for (int Index = 0; Index < 7; ++Index)
            State->Events.RegisterEvent(EventIds[Index]);
    }

    // FUN_0045b490 (the system-arrival caption).
    RefreshLoadingProgress();
    NnArrivalTextShowSystemNow();

    // Session flag 8 (FUN_004c8110) -> first person; otherwise the preference stays (the
    // ctor's 1, or the last choice).
    State->bSpaceReady = 1;
    if (TestGameSessionFlags(8))
        State->PlayerPreference = 0;

    if (State->ActiveCamera == nullptr)
        SwitchSpaceCameraMode("player", nullptr);

    // No launch answer -> DAT_0067ecbd (single player) or DAT_0067ecbe.
    WaitForServerResponse(GetPlayerLaunchAcknowledgedByte(), 5.0f, 60.0f);
    if (*GetPlayerLaunchAcknowledgedByte() == 0)
    {
        if (SinglePlayer())
            SetClientQuitRequested(true);
        else
            G_bServerDisconnected = true;
    }

    // Outside single player (the next state being 0, which it now always is) FUN_0046a4a0(1)
    // shows the chat window.
    G_SpaceNextState = 0;
    if (!SinglePlayer() && G_SpaceNextState == 0)
        ChatShowMsgWin(true);

    RefreshLoadingProgress();
    return G_SpaceNextState == 0;
}

int UpdateSpaceState(SpaceApp* State, float DeltaSeconds)
{
    // LAB_0054b000 -- slot 14. The whole in-space frame: simulation, camera, sound, then the
    // render bracket FUN_00424f30 .. FUN_00425080 / FUN_00425140 with the scene drawn through
    // the active camera's slot 0x78. Returns DAT_00679778, the next client state (0 = stay).
    // (FClientTickScope / FClientPhysicsProfileScope are port-only diagnostics.)

    // No active camera -> 6, leave space.
    if (GetActiveSpaceCamera() == nullptr)
        return kClientStateMenu;

    // The simulation half, skipped while FUN_0042d740 holds the local server.
    if (!ShouldSkipLocalServerTick())
    {
        G_SpaceNextState = 0;

        // Retail calls the shared empty function 0x00401e90 (Dt) here. Nothing to do.

        // FUN_00541380 -- the per-bucket Gameobj update. Each ship's slot 93
        // (FClientShipGameobj::update) runs its CShip's CObject::update for the local player's
        // objects, so the player CShip is ticked there, not here.
        FClientTickScope ObjectsProfile(EClientTickSection::Objects);
        ClientObjDb_UpdateAll(DeltaSeconds);
        ObjectsProfile.Stop();

        FClientTickScope AnimationProfile(EClientTickSection::WorldFx);
        AnimDB::Update(DeltaSeconds);
        AnimationProfile.Stop();
        FClientTickScope PhysicsProfile(EClientTickSection::Physics);
        FClientPhysicsProfileScope PhysicsDetails;
        FClientTickScope StepProfile(EClientTickSection::PhysicsStep);
        PhySys::Update(StarSysGetActiveSystemId(), DeltaSeconds);
        StepProfile.Stop();

        // FUN_00540bd0 -- CBeam::move on every bucket-3 Gameobj's beam.
        FClientTickScope BeamsProfile(EClientTickSection::MoveBeams);
        ClientObjDb_MoveBeams(DeltaSeconds);
        BeamsProfile.Stop();
        // FUN_0052fc20 -- PhySys::GenerateCollisions, then the collision list resolved into
        // Gameobj hit pairs (FUN_0052fe60, slot 0x194).
        ClientObjDb_ProcessCollisions();
        PhysicsDetails.Stop();
        PhysicsProfile.Stop();

        // FUN_005135b0(Dt, camera); the callee reads only Dt.
        FClientTickScope WorldProfile(EClientTickSection::WorldFx);
        TradeLaneDetachTick(DeltaSeconds);

        // FUN_00538390, FUN_00537c30.
        if (GetGameOption(kGameOptLighting) != 0)
        {
            GameLightTickAll(DeltaSeconds);
            LightGridRebuild();
        }

        UpdateSpaceDocking(State);

        // The active camera's slot 0x6c, re-read and tested.
        FSpaceCamera* const Cam = GetActiveSpaceCamera();
        if (Cam != nullptr)
            ((FGameCamera*)Cam)->Update(DeltaSeconds);

        StarSysUpdate(DeltaSeconds);
        TickMaterialAnimationPlayback(DeltaSeconds); // FUN_0041b2e0
        WorldProfile.Stop();
    }

    // The sound environment from the camera's slot 0x7c into FUN_00428030, then the mixer
    // FUN_00428040. No null test: the camera was checked on entry.
    {
        FClientTickScope SoundProfile(EClientTickSection::Sound);
        FSpaceCamera* const Cam = GetActiveSpaceCamera();
        SetClientAudioMode(
            ((FGameCamera*)Cam)->GetSoundMode());
        TickClientSoundMixer(GetActiveSpaceCamera(), DeltaSeconds);
    }

    // FUN_00424f30, kept for the present below.
    const bool bSceneBegun = BeginClientRenderFrame();
    if (bSceneBegun)
    {
        Win3DImmedBatchReset();       // FUN_005a29b0
        ButtonMontageEnsure();        // FUN_00559c10
        UpdateMouseFlightDirection(); // FUN_004c7cb0
        // FUN_004ec400 -- with the Targetable HUD and the letterbox up, clear the low two bits
        // of +0x6c on its four alert children.
        HudTargetableObjectsHideAlertsInLetterbox();

        // FUN_0051b4b0 (letterbox, clear, viewport, Win3D camera) and the camera's slot 0x78.
        FSpaceCamera* const Cam = GetActiveSpaceCamera();
        if (Cam != nullptr)
        {
            GameCameraBeginFrame(reinterpret_cast<FGameCamera*>(Cam));
            FSpaceCamera* const DrawCam = GetActiveSpaceCamera();
            ((FGameCamera*)DrawCam)->DrawScene(0xffffu, 0u);
        }

        Win3DImmedBatchFlushAll();    // FUN_005a2a20
        ButtonMontageFlushBatch();    // FUN_00559c20
        UiOverlayPrepPipeline();      // FUN_00417f10
        Win3DDrawAll();               // FUN_0059e300 (NN_Pause is one of its roots)

        // With Dt > 0 and the server tick not paused (FUN_0042d730), the fade.
        if (DeltaSeconds > 0.0f && !IsClientServerTickPaused())
        {
            FSpaceCamera* const FadeCam = GetActiveSpaceCamera();
            UpdateFade(DeltaSeconds);
            DrawFade(AsCamera(FadeCam));
        }

        EndClientRenderScene(); // FUN_00425080
    }

    // FUN_00541510 -- every Gameobj whose slot 0x158 reports it done is unregistered and
    // deleted.
    ClientObjDb_Reap();
    // FUN_00541580 -- multiplayer only (returns at once under SinglePlayer).
    if (!ShouldSkipLocalServerTick())
        ClientObjDb_UpdateRemotePhysics(DeltaSeconds);
    // A pending system switch or landing.
    ProcessPendingSystemSwitch();

    // The player's last position and orientation (FUN_004c4a30 / FUN_004c4a60).
    {
        IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
        if (Player != nullptr)
        {
            SetPlayerLastPositionRetail(&Player->get_position().x);
            IObjRW* const Again = static_cast<IObjRW*>(GetPlayerObjRW());
            SetPlayerLastOrientationRetail(&Again->get_transform().Rotation.E[0][0]);
        }
    }

    // When the active camera reports it is finished (slot 0x60), either
    // the player ship is gone -- FUN_00471e80 once, latched in +0x09 -- or the camera returns to
    // "player", attached to the player object, and +0xba8 drops.
    {
        FSpaceCamera* const Cam = GetActiveSpaceCamera();
        if (Cam != nullptr &&
            ((FGameCamera*)Cam)->IsFinished())
        {
            void* const Player = GetPlayerObjRW();
            if (Player == nullptr)
            {
                if (State->bPlayerLostHandled == 0)
                {
                    NnExitPostShipLostMessage(); // FUN_00471e80
                    State->bPlayerLostHandled = 1;
                }
            }
            else
            {
                SwitchSpaceCameraMode("player", Player);
                State->bScriptCameraLatch = 0;
            }
        }
    }

    if (State->bLeaveRequested != 0)
        G_SpaceNextState = kClientStateMenu;

    // FUN_00425140, only when FUN_00424f30 began the scene.
    if (bSceneBegun)
        PresentClientRenderFrame();

    // FUN_00579190 -- the loading screen's timer reset.
    LoadingScreenReset();

    return G_SpaceNextState;
}

void DestroySpaceState(SpaceApp* State)
{
    // FUN_0054aec0 -- slot 13. No null test on `this`: the client state driver only calls slot
    // 13 on a live state.

    // FUN_00577590 -- drop the space accelerator group the init registered.
    UnregisterAcceleratorGroup("IDR_SPACE");

    // FUN_00576190 on the Event::Handler face -- drop every registration of this handler.
    State->Events.UnregisterAllEvents();

    // FUN_004348b0 on DAT_00667e50 (the engine-instance table).
    ResetEngineInstanceCache();

    // FUN_00435630 -- every character script's reference count back to 0 and a fresh
    // IAnimation2 script for the gf\CharacterInfo.cpp database.
    ResetCharacterScripts();

    // FUN_00549bc0(this, 0, 0) -- deactivate the active camera.
    InstallSpaceCamera(nullptr, false);

    // FUN_00518410 on the first-person camera, the empty OnDisconnect on three others.
    ReleaseSpaceCameraControllerParts();

    HudElementListShutdown(); // FUN_004c77a0

    // +0xbaa is tested and only then cleared (the compare is on the value before the other two
    // stores).
    State->bSpaceReady = 0;        // +0xba9
    State->bScriptCameraLatch = 0; // +0xba8
    if (State->bDockingPending != 0)
        State->bDockingPending = 0; // +0xbaa
    G_bNnScreenUp = false;      // DAT_0067dcc8
    State->bDockingActive = 0;  // +0xbab

    // The player's ship CEqObj; when control exclusion 7 is set it is cleared (0).
    CEqObj* const EqObj = PlayerEqObj(0x503u);
    if (EqObj != nullptr && EqObj->is_control_excluded(7u))
        EqObj->set_control_exclusion(0u);

    State->bCameraReady = 0;

    StarSysLeave(nullptr);

    MouseSetCursor("Arrow", true); // FUN_0041dde0

    CObject::ReleaseAll();

    // FUN_0051fbc0 -- the ZItem heaps and draw queues released.
    SpaceDrawQueueRelease();

    ClientFuseDB::Clear(); // FUN_0052a690

    // FUN_00502aa0 -- the jump-effect descriptors freed.
    JumpEffectDescsClear();

    ClearSolarStarArchShapes(); // FUN_00511010

    // FUN_004fe180 -- virtual-delete every gate tunnel.
    GateTunnelsClear();

    EffectTypeListKillAll(); // FUN_004f73c0

    LightGridReset(); // FUN_005382f0
    // FUN_00538520 -- drop one reference on every GameLight, virtual-delete at zero.
    GameLightReleaseAll();

    GameLightHeapShrink();   // FUN_00537ab0
    LightAnimationFreeAll(); // FUN_00538ab0

    // FUN_004284f0 -- every sound stopped, the live-source leak warning.
    StopAllClientSounds();
    // FUN_0042ad30(1) -- unload the sound arches by category mask.
    UnloadClientSoundArchesByCategory(1);

    AnimDB::Shutdown();

    // FUN_0045a140 (tail jump) -- the NN system teardown.
    NnSystemShutdown();
}

void ShutdownSpaceState(SpaceApp* State)
{
    // FUN_0054ab20 (slot 9).
    StarSysLeaveWithoutPlayer();
    // Slot 13 (FUN_0054aec0). Retail calls it even when the atexit teardown already ran slot 13
    // on the current state.
    State->Destroy();
    CObject::ReleaseAll();
    // FUN_0051fbc0 (tail jump).
    SpaceDrawQueueRelease();
}
