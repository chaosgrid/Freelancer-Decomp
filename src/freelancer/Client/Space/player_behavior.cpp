// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// FUN_0054baf0 and the player behaviour command family (FUN_004c7950 ..).

#include "Space/player_behavior.h"

#include "Space/player_spawn.h"

#include "common_cequip.h"
#include "common_cobject.h"
#include "common_iface.h"
#include "Core/game_options.h"
#include "Core/globals.h"
#include "Space/HUD/hud_status_core.h" // HudStatusIsTurretView / HudStatusIsRearView (+0x5b9 / +0x5ba)
#include "Common/input_manager.h"
#include "Interface/menu_ui_init.h"
#include "NeuroNet/nn_modal.h" // NnMainButtonHasActivePanel (FUN_0045a470)
#include "Space/space_letterbox.h"
#include "Interface/win3d.h" // Win3DGetHover (FUN_0059daa0)

namespace
{
    // A BaseWatcher stores a Watchable*, which is the IObjRW plus eight.
    const int kWatchableOffset = 8;

    // .data:00673564. Retail keeps the clamped throttle here rather than on any object, and
    // FUN_004c7a00 both refreshes and returns it.
    float G_PlayerThrottle = 0.0f;

    // .data:0067354c. See the mouse-flight lock toggle section in the header.
    bool G_MouseFlightLocked = false;

    // UpdateMouseFlightDirection's own state, .data:00673550/554/555/558/55c.
    float G_MouseFlightHoldTimer = 0.0f;    // _DAT_00673550 -- seconds accumulated this hold
    bool G_MouseFlightEngaged = false;      // DAT_00673554 -- latched true once forwarded
    bool G_MouseFlightButtonTrigger = false;// DAT_00673555 -- OnMouseFlightRawMessage's one-shot
    float G_MouseFlightOriginX = 0.0f;      // _DAT_00673558 -- cursor X latched at timer==0
    float G_MouseFlightOriginY = 0.0f;      // _DAT_0067355c -- cursor Y latched at timer==0 (same
                                             // invert-aware reading as every other cursor Y below)

    // DAT_00612270: the hold threshold.
    constexpr float kMouseFlightHoldThreshold = 0.25f;

    // The invert-aware cursor Y block repeated at all four sites in FUN_004c7cb0. Reading
    // G_MouseFlightHoldTimer live reproduces each call site's own point-in-time value.
    int MouseFlightAdjustedCursorY(int RawCursorY)
    {
        bool const bLockOrHoldReady =
            (!NnMainButtonHasActivePanel() && G_MouseFlightLocked) ||
            (G_MouseFlightHoldTimer >= kMouseFlightHoldThreshold);
        // The created surface height (DAT_00610854).
        if (bLockOrHoldReady && GetGameOption(kGameOptInverseMouse) != 0)
            return static_cast<int>(G_RenderSurfaceHeight) - RawCursorY;
        return RawCursorY;
    }

    // The lines every function in this family opens with; retail spells them out at each site.
    IBehaviorManager* PlayerBehaviorManager()
    {
        // The result goes straight into GetBehaviorManager, null included -- Common's own entry
        // test is what turns a missing ship into a null manager.
        return GetBehaviorManager(static_cast<IObjRW*>(GetPlayerObjRW()));
    }

    // Shared preamble of FUN_004c7b90 / FUN_004c7be0: the player's CEqObj read directly (not
    // through GetBehaviorManager), class-tested as a ship. Retail's null test after `+0xc` (the
    // CBase this-adjustment) only fails for the pointer -0xc, kept as a sentinel.
    CEqObj* GetPlayerShipEqObj()
    {
        void* const ObjRW = GetPlayerObjRW();
        if (ObjRW == nullptr || ObjRW == reinterpret_cast<void*>(-0xc))
            return nullptr;

        CEqObj* const EqObj =
            *reinterpret_cast<CEqObj**>(static_cast<unsigned char*>(ObjRW) + 0x10);
        if (EqObj == nullptr)
            return nullptr;

        if ((static_cast<unsigned>(EqObj->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
            return nullptr;

        return EqObj;
    }

    // Shared tail of FUN_004c7b90 / FUN_004c7be0: the player's engine equipment, or null.
    CEEngine* GetPlayerEngineEquip()
    {
        CEqObj* const Ship = GetPlayerShipEqObj();
        if (Ship == nullptr)
            return nullptr;

        CEquip* const Eq = const_cast<CEquip*>(Ship->EquipManager.FindFirst(EquipTypeEngine));
        return CEEngine::cast(Eq);
    }
}

void* GetPlayerObjRW()
{
    // FUN_0054baf0. A null watchable yields a null object rather than the address -8.
    void* const Watched = GetPlayerShipWatcher();
    if (Watched == nullptr)
        return nullptr;

    return static_cast<unsigned char*>(Watched) - kWatchableOffset;
}

void ResetPlayerBehaviorDirection()
{
    // FUN_004c7950
    IBehaviorManager* const Manager = PlayerBehaviorManager();
    if (Manager != nullptr)
        Manager->reset_current_behavior_direction();
}

void SetPlayerStrafeBurst(int Dir)
{
    // FUN_004c7a30
    IBehaviorManager* const Manager = PlayerBehaviorManager();
    if (Manager != nullptr)
        Manager->update_current_behavior_slide_strafe_burst(static_cast<StrafeDir>(Dir));
}

void SetPlayerAfterburner(bool bOn)
{
    // FUN_004c7970
    IBehaviorManager* const Manager = PlayerBehaviorManager();
    if (Manager != nullptr)
        Manager->update_current_behavior_afterburner(bOn);
}

void SetPlayerAutoLevel(bool bOn)
{
    // FUN_004c79a0
    IBehaviorManager* const Manager = PlayerBehaviorManager();
    if (Manager != nullptr)
        Manager->update_current_behavior_auto_level(bOn);
}

void SetPlayerAutoAvoidance(bool bOn)
{
    // FUN_004c79d0
    IBehaviorManager* const Manager = PlayerBehaviorManager();
    if (Manager != nullptr)
        Manager->update_current_behavior_auto_avoidance(bOn);
}

void SetPlayerBrakeReverse(bool bOn)
{
    // FUN_004c7a60
    IBehaviorManager* const Manager = PlayerBehaviorManager();
    if (Manager != nullptr)
        Manager->update_current_behavior_brake_reverse(bOn);
}

void SetPlayerCruise(bool bOn)
{
    // FUN_004c7a90
    IBehaviorManager* const Manager = PlayerBehaviorManager();
    if (Manager != nullptr)
        Manager->update_current_behavior_cruise(bOn);
}

void SetPlayerEngageEngine(bool bOn)
{
    // FUN_004c7ac0
    IBehaviorManager* const Manager = PlayerBehaviorManager();
    if (Manager != nullptr)
        Manager->update_current_behavior_engage_engine(bOn);
}

void SetPlayerThrottle(float Throttle)
{
    // FUN_004c7af0. The argument is stored first, unclamped, as retail orders it.
    G_PlayerThrottle = Throttle;

    // The low bound is a double -1.0 (.rdata:005d5060), the high a float 1.0 (.rdata:005c75dc).
    if (static_cast<double>(Throttle) < -1.0)
        G_PlayerThrottle = -1.0f;
    else if (Throttle > 1.0f)
        G_PlayerThrottle = 1.0f;

    IBehaviorManager* const Manager = PlayerBehaviorManager();
    if (Manager != nullptr)
        Manager->update_current_behavior_throttle(G_PlayerThrottle);

    // A zero throttle (ordered equal only) cancels cruise.
    if (G_PlayerThrottle == 0.0f)
    {
        // Retail re-fetches the manager rather than reusing the one above.
        IBehaviorManager* const Again = PlayerBehaviorManager();
        if (Again != nullptr)
            Again->update_current_behavior_cruise(false);
    }
}

float GetLastPlayerThrottle()
{
    // FUN_004c7a00. get_throttle's result is discarded.
    if (GetPlayerObjRW() == nullptr)
        return 0.0f;

    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    Player->get_throttle(G_PlayerThrottle);
    return G_PlayerThrottle;
}

void SetPlayerBehaviorDirectionGated(float X, float Y, float Z)
{
    // FUN_004c78d0. NaN is not zero; HUD_Status is read without a null test.
    bool const bDirIsZero = (X == 0.0f) && (Y == 0.0f);
    if (bDirIsZero || (!HudStatusIsTurretView() && !HudStatusIsRearView()))
    {
        IBehaviorManager* const Manager = PlayerBehaviorManager();
        if (Manager != nullptr)
        {
            Vector Dir;
            Dir.x = X;
            Dir.y = Y;
            Dir.z = Z;
            Manager->update_current_behavior_direction(Dir);
        }
    }
}

bool GetPlayerEngineCruiseActive()
{
    CEEngine* const Engine = GetPlayerEngineEquip();
    if (Engine == nullptr)
        return false;
    return Engine->IsCruiseEngaged();
}

bool GetPlayerEngineIsActive()
{
    CEEngine* const Engine = GetPlayerEngineEquip();
    if (Engine == nullptr)
        return false;
    return Engine->IsActive();
}

bool IsMouseFlightUnlocked()
{
    // FUN_004c7270
    if (NnMainButtonHasActivePanel())
        return true;
    return !G_MouseFlightLocked;
}

bool IsMouseFlightLocked()
{
    // FUN_004c7c80
    if (NnMainButtonHasActivePanel())
        return false;
    return G_MouseFlightLocked;
}

void ToggleMouseFlightLock()
{
    // FUN_004c7c40
    G_MouseFlightLocked = !G_MouseFlightLocked;
    PlayOrRewindUiSoundSlot(0x1c); // FUN_005645c0
}

void OnMouseFlightRawMessage(unsigned Msg)
{
    // FUN_004c80f0: WM_LBUTTONDOWN only.
    if (Msg == 0x201u)
        G_MouseFlightButtonTrigger = true;
}

void UpdateMouseFlightDirection()
{
    // FUN_004c7cb0
    CEqObj* const ShipEqObj = GetPlayerShipEqObj();
    IBehaviorManager* const ShipBehavior =
        (ShipEqObj != nullptr) ? ShipEqObj->get_behavior_interface() : nullptr;

    // The behavior manager's +0x168 byte.
    bool bSuppressMouseFlight = false;
    if (ShipBehavior != nullptr)
    {
        bSuppressMouseFlight = (ShipBehavior->bAllowUserSteering == 0);
    }

    // The cinematic letterbox, or riding a tradelane, forces the reset path below regardless of
    // the byte above.
    if (IsLetterboxVisible() ||
        (ShipEqObj != nullptr && static_cast<CShip*>(ShipEqObj)->is_using_tradelane()))
    {
        bSuppressMouseFlight = true;
    }

    // DAT_00616850 bit 0 is the left button (input_manager.cpp).
    bool const bMouseButtonHeld = (GetClientMouseButtons() & 1u) == 1u;

    if ((!bMouseButtonHeld && !G_MouseFlightLocked) || bSuppressMouseFlight)
    {
        // Reset. Retail's no-op tail call is described in the header.
        G_MouseFlightHoldTimer = 0.0f;
        if (G_MouseFlightEngaged)
        {
            G_MouseFlightEngaged = false;
            G_MouseFlightButtonTrigger = false;

            IBehaviorManager* const Manager = PlayerBehaviorManager();
            if (Manager != nullptr)
            {
                Vector Zero;
                Zero.x = 0.0f;
                Zero.y = 0.0f;
                Zero.z = 0.0f;
                Manager->update_current_behavior_direction(Zero);
            }
        }
        return;
    }

    if (!(G_MouseFlightButtonTrigger || G_MouseFlightLocked))
        return;

    // Not engaged yet and something under the mouse: the timer goes back to zero.
    if (!G_MouseFlightEngaged && Win3DGetHover() != nullptr)
    {
        G_MouseFlightHoldTimer = 0.0f;
        return;
    }

    int CursorX = 0;
    int CursorY = 0;
    int CursorZ = 0;
    GetClientSoftwareCursor(&CursorX, &CursorY, &CursorZ);

    // Only an ordered-equal zero timer latches the origin.
    if (G_MouseFlightHoldTimer == 0.0f)
    {
        G_MouseFlightOriginX = static_cast<float>(CursorX);
        G_MouseFlightOriginY = static_cast<float>(MouseFlightAdjustedCursorY(CursorY));
    }

    // The sum is stored as float; the unrounded sum is compared (less or unordered goes to the
    // cancel-radius test).
    double const HoldSum =
        GetClientFrameTimeCapped() + static_cast<double>(G_MouseFlightHoldTimer);
    G_MouseFlightHoldTimer = static_cast<float>(HoldSum);

    if (HoldSum >= static_cast<double>(kMouseFlightHoldThreshold))
    {
        // The half width stays on the FPU.
        double const HalfW = static_cast<double>(static_cast<int>(G_RenderSurfaceWidth)) * 0.5;
        float const NormX =
            static_cast<float>((static_cast<double>(CursorX) - HalfW) / HalfW);
        // The half height is stored as a float first.
        float const HalfH =
            static_cast<float>(static_cast<double>(static_cast<int>(G_RenderSurfaceHeight)) * 0.5);
        int const AdjustedY = MouseFlightAdjustedCursorY(CursorY);
        float const NormY = static_cast<float>(
            (static_cast<double>(AdjustedY) - static_cast<double>(HalfH)) /
            static_cast<double>(HalfH));

        // FUN_004c78d0's body, inlined in retail.
        SetPlayerBehaviorDirectionGated(NormX, NormY, 0.0f);

        G_MouseFlightEngaged = true;
        return;
    }

    // Below the threshold. dx is stored as a float, dy stays on the FPU; the constants are
    // DAT_005d6e20 / DAT_005d440c. Only an ordered r*r < sum forces the timer to the threshold.
    float const DeltaX =
        static_cast<float>(static_cast<double>(CursorX) - static_cast<double>(G_MouseFlightOriginX));
    int const AdjustedY2 = MouseFlightAdjustedCursorY(CursorY);
    double const DeltaY =
        static_cast<double>(AdjustedY2) - static_cast<double>(G_MouseFlightOriginY);
    double const CancelRadius =
        800.0 / static_cast<double>(static_cast<int>(G_RenderSurfaceWidth)) * 50.0;
    double const DistSq =
        static_cast<double>(DeltaX) * static_cast<double>(DeltaX) + DeltaY * DeltaY;
    if (CancelRadius * CancelRadius < DistSq)
        G_MouseFlightHoldTimer = kMouseFlightHoldThreshold;
}

void ResetMouseFlightState()
{
    // Inside FUN_004c73d0, in retail's store order.
    G_MouseFlightLocked = false;
    G_MouseFlightHoldTimer = 0.0f;
    G_MouseFlightEngaged = false;
    G_MouseFlightButtonTrigger = false;
    G_MouseFlightOriginX = 0.0f;
    G_MouseFlightOriginY = 0.0f;
}

bool IsMouseFlightHoldThresholdReached()
{
    // FUN_004c7c60
    return G_MouseFlightHoldTimer >= kMouseFlightHoldThreshold;
}

namespace
{
    float G_TargetWindowNotification = 0.0f; // .data:00673568
    float G_StatusWindowNotification = 0.0f; // .data:0067356c
}

void SetTargetWindowNotification(float Value)
{
    // FUN_004c83a0
    G_TargetWindowNotification = Value;
}

void SetStatusWindowNotification(float Value)
{
    // FUN_004c83b0
    G_StatusWindowNotification = Value;
}
