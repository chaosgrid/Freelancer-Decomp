// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// FGameCamera (FUN_0051b2f0) and FHudCamera (FUN_0051ca80): the two intermediate camera
// classes (retail Gamecam.cpp). The header lists every slot with its retail address.

#include "Space/Cameras/space_camera_game.h"

#include "Interface/Pages/character_select_equip.h" // Get/SetPlayerVisitFlagsRetail, GetPlayerStarSystemIdRetail
#include "Space/Effects/cli_projectile_fx.h"  // FCliEquipFirePacket, CliShipHandleEquipUpdate (FUN_00544d70)
#include "Space/client_gameobj.h"     // FClientGameobj::ManagerKind (+0x1c)
#include "Space/client_objdb.h"       // GetClientObjDb, ClientObjDb_GetKindHead (FUN_00540fc0)
#include "common_misc.h"        // Geometry::Sphere
#include "common_reputation.h"  // Reputation::Vibe::GetAffiliation
#include "common_universe.h"    // Universe::get_system, ISpatialPartition, SpatialFill
#include "Space/cockpit_load.h"       // SetCockpitResourceStringLoading (FUN_004c7290)
#include "common_cequip.h"      // CEGun, CEquipTraverser
#include "common_cobject.h"     // CEqObj
#include "common_iface.h"       // IObjRW
#include "Core/game_options.h"
#include "Core/globals.h"            // GetClientFrameTimeCapped (FUN_0042d680)
#include "Space/HUD/hud_command_dispatch.h"
#include "Space/HUD/hud_element_list.h"
#include "Space/HUD/hud_target.h"         // HudIsMousePickBlocked (FUN_0045a740)
#include "Space/HUD/hud_targetable_objects.h" // G_HudTargetableObjects (DAT_00674b70), FUN_004f1da0
#include "Common/input_manager.h"      // MouseIsArrowCursor (FUN_0041ddb0)
#include "Space/Effects/jump_effect.h"        // FJumpShipEffect::ShakeAmplitude (FUN_00503e20)
#include "Interface/menu_keymap_config.h" // (Un)RegisterAcceleratorGroup, KeyMapIsCommandHeld (FUN_005784a0)
#include "NeuroNet/nn_modal.h"          // G_bNnScreenUp (DAT_0067dcc8)
#include "Space/player_behavior.h"    // ToggleMouseFlightLock, SetPlayerBehaviorDirectionGated
#include "Space/player_spawn.h"       // GetPlayerConnectionId (DAT_00673344)
#include "Space/space_letterbox.h"
#include "Space/space_scene_obj_draw.h" // SpaceCameraRenderSlot78 (FUN_0051b250)
#include "Interface/win3d_immediate.h"    // Win3DSetViewport (FUN_00422610), Win3DSetPipelineClearColor
#include "Interface/win3d.h"          // Win3DBroadcast (FUN_0059df40)
#include "Interface/win3d_render.h"       // Win3DApplyCamera (FUN_00422950)

#include <cstddef>
#include <math.h>
#include <new>
#include <string.h>
#include <windows.h>            // GetKeyState

#ifdef _MSC_VER
#include <stdlib.h>          // _stricmp
#else
#define _stricmp strcasecmp
#endif

const float kGameCameraDefaultHalfFovXDeg = 35.0f;   // the literal 0x420c0000

namespace
{
    // Retail keeps the object as its Watchable subobject, which sits 8 bytes into IObjRW.
    const int kWatchableOffset = 8;

    // .rdata 0x005dbfe8, the source path FUN_0051b4b0 / FUN_0051b500 hand FUN_00422610.
    const char kGamecamCpp[] = "E:\\FL\\Scratch\\Source\\Client\\space\\Gamecam.cpp";

    // The six keyboard steering halves FUN_0051d7f0 writes (1.0 / -1.0 while the key is down,
    // 0.0 otherwise) and FUN_0051d070 sums into one steering vector.
    float G_HudKeySteerPosX = 0.0f; // DAT_00675278, event 0x2a
    float G_HudKeySteerNegX = 0.0f; // DAT_0067526c, event 0x2b
    float G_HudKeySteerPosY = 0.0f; // DAT_0067527c, event 0x2c
    float G_HudKeySteerNegY = 0.0f; // DAT_00675270, event 0x2d
    float G_HudKeySteerPosZ = 0.0f; // DAT_00675280, event 0x2e
    float G_HudKeySteerNegZ = 0.0f; // DAT_00675274, event 0x2f
    // Event 0xc7: the fire key's down state (DAT_0067529c) and the "just changed" latch
    // FUN_0051d070 consumes (DAT_006752a0).
    unsigned char G_bHudFireHeld = 0;
    int G_HudFireChanged = 0;

    // FUN_0051d070's keyboard-steering ramp (DAT_006752a4): grows by G_HudSteerRampRate per second
    // while a steering key is held, capped at 1, dropped to 0 when the keys change or let go.
    float G_HudSteerRamp = 0.0f;
    // .data (not .rdata) 0x00613740 / 0x00613744, both 2.0f and each read only by FUN_0051d070:
    // the ramp rate per second and the exponent of the shift-steer falloff.
    float G_HudSteerRampRate = 2.0f;
    float G_HudShiftSteerExponent = 2.0f;

    // DAT_00675294: the cockpit shake amplitude (radians) FUN_0051db00 decays and raises and
    // FUN_0051dbd0 jitters the view with. DAT_0061373c (.data, read only there) is the decay per
    // update.
    float G_CameraShakeAmount = 0.0f;
    float G_CameraShakeDecay = 0.001f;
}

void GameCameraConstruct(FGameCamera* Cam)
{
    // FUN_0051b2f0, in retail's order.
    SpaceCameraConstruct((FSpaceCamera*)Cam);

    // Retail installs vtable 0x005dbf5c here; a derived ctor overwrites it immediately after.

    Cam->FieldF0 = 0;                        // +0xf0
    Cam->FieldEC = 0;                        // +0xec

    // Retail re-tests the watcher it has just zeroed; it is never non-null.
    if (Cam->Watched.target() != nullptr)
        Cam->Watched.set(nullptr);

    Cam->Flags = static_cast<unsigned char>(Cam->Flags & 0xfc);

    Cam->bFlagF4 = 0;
    Cam->bFlagF5 = 0;
    Cam->bFlagF6 = 0;

    // The resting half-FOV is cached BEFORE it is applied, through the base's non-virtual
    // setter rather than the vtable.
    Cam->BaseHalfFovXDeg = kGameCameraDefaultHalfFovXDeg;
    SpaceCameraSetHalfFovX((FSpaceCamera*)Cam, kGameCameraDefaultHalfFovXDeg);

    // Retail clears the two vectors downwards, +0xe4 first.
    for (int i = 0; i < 3; ++i)
    {
        Cam->Velocity[i] = 0.0f;
        Cam->VectorD0[i] = 0.0f;
    }

    Cam->Pad0CC[0] = 0;
    Cam->Pad0CC[1] = 0;
    Cam->Pad0CC[2] = 0;
    Cam->Pad0CC[3] = 0;
    Cam->ClearColor[0] = 0;
    Cam->ClearColor[1] = 0;
    Cam->ClearColor[2] = 0;
    Cam->Pad0F7 = 0;
}

void HudCameraConstruct(FHudCamera* Cam)
{
    // FUN_0051ca80.
    GameCameraConstruct((FGameCamera*)Cam);

    // The secondary base's abstract table (0x005cbed8, one _purecall thunk) is installed first
    // and replaced below; it is never callable.
    Cam->bFlagFC = 0;        // +0xfc
    Cam->bFlagFE = 1;        // +0xfe
    // +0xfd and +0xff are not written here (the FirstPerson ctor clears +0xfd itself); every
    // HUDCamera lives in G_SpaceApp's zeroed static storage.

    // Retail then swaps the primary vptr to 0x005dc08c (HUDCamera) and the face's to
    // 0x005dc084.
}

const char* GameCameraGetName()
{
    return "GameCamera";     // 0x0051b3b0 -> .rdata 0x005dbfdc
}

const char* HudCameraGetName()
{
    return "HUDCamera";      // 0x0051cac0 -> .rdata 0x005dc10c
}

void GameCameraUpdate(FGameCamera* Cam, float DeltaSeconds)
{
    // FUN_0051b650 (slot 0x6c). The position comes through the camera's own slot 0x0c.
    FSpaceCamera* const Space = SpaceCameraOf(Cam);
    const float* const Position =
        Space->GetPosition();

    // Only an exact zero dt skips (a NaN does not). The reciprocal is taken once.
    if (!(DeltaSeconds == 0.0f))
    {
        const float DiffZ = Position[2] - Cam->VectorD0[2];
        const float DiffX = Position[0] - Cam->VectorD0[0];
        const float DiffY = Position[1] - Cam->VectorD0[1];
        const float InvDt = 1.0f / DeltaSeconds;
        Cam->Velocity[0] = InvDt * DiffX;
        Cam->Velocity[1] = InvDt * DiffY;
        Cam->Velocity[2] = InvDt * DiffZ;
    }

    Cam->VectorD0[0] = Position[0];
    Cam->VectorD0[1] = Position[1];
    Cam->VectorD0[2] = Position[2];
}

void GameCameraBeginFrame(FGameCamera* Cam)
{
    // FUN_0051b4b0. The letterbox advances and, when it moved, re-cuts this camera's viewport.
    FSpaceCamera* const Space = SpaceCameraOf(Cam);
    const float DeltaSeconds = static_cast<float>(GetClientFrameTimeCapped());
    UpdateLetterbox(Space, DeltaSeconds);

    if ((Cam->Flags & 3) != 0)
        GameCameraClear(Cam);

    Win3DSetViewport(AsCamera(Space), kGamecamCpp, 0x85);
    Win3DApplyCamera(AsCamera(Space));
}

void GameCameraClear(FGameCamera* Cam)
{
    // FUN_0051b500.
    unsigned ClearBits = 0;
    bool bBarsBlackened = false;
    float Red = 0.0f;
    float Green = 0.0f;
    float Blue = 0.0f;

    if ((Cam->Flags & 1) != 0)
    {
        // The colour bytes scaled by 1/255 (double 0x005c89c8).
        Red = static_cast<float>(Cam->ClearColor[0] * (1.0 / 255.0));
        Green = static_cast<float>(Cam->ClearColor[1] * (1.0 / 255.0));
        Blue = static_cast<float>(Cam->ClearColor[2] * (1.0 / 255.0));

        // With the letterbox showing and any colour byte non-zero, the whole surface is cleared
        // black first and the colour goes into the camera's own viewport afterwards.
        if (IsLetterboxVisible() &&
            (Cam->ClearColor[0] != 0 || Cam->ClearColor[1] != 0 || Cam->ClearColor[2] != 0))
        {
            bBarsBlackened = true;
            Win3DSetPipelineClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        }
        else
        {
            Win3DSetPipelineClearColor(Red, Green, Blue, 1.0f);
        }
        ClearBits = kWin3DClearColour;
    }

    if ((Cam->Flags & 2) != 0)
        ClearBits = ClearBits | kWin3DClearDepth;

    if (ClearBits == 0)
        return;

    // The whole surface first.
    Win3DSetViewport(nullptr, kGamecamCpp, 0xb7);
    ClearWin3DBuffers(ClearBits);

    // After a black full-surface clear, the camera's own viewport gets the real colour.
    if ((Cam->Flags & 1) != 0 && bBarsBlackened)
    {
        Win3DSetViewport(AsCamera(SpaceCameraOf(Cam)), kGamecamCpp, 0xc0);
        Win3DSetPipelineClearColor(Red, Green, Blue, 1.0f);
        ClearWin3DBuffers(kWin3DClearColour);
    }
}

void GameCameraGetVelocity(const FGameCamera* Cam, float* OutVelocity)
{
    // 0x0051b700. Replaces the base's zero-vector stub LAB_0040f5d0, so a camera bound to a
    // moving ship reports a velocity while a free camera reports standing still.
    OutVelocity[0] = Cam->Velocity[0];
    OutVelocity[1] = Cam->Velocity[1];
    OutVelocity[2] = Cam->Velocity[2];
}

bool GameCameraSetProperty(FGameCamera* Cam, const char* Key, const char* Value)
{
    // 0x0051b3f0. The base (FUN_0040f5f0, called directly) decides whether the key is known.
    if (SpaceCameraSetProperty((FSpaceCamera*)Cam, Key, Value) == 0)
        return false;

    // Only the two fov keys re-cache the resting angle, read back through slot 0x18 (which no
    // concrete class overrides).
    if (_stricmp(Key, "fovx") == 0 || _stricmp(Key, "fovy") == 0)
    {
        Cam->BaseHalfFovXDeg = AsCamera((FSpaceCamera*)Cam)->GetHalfFovXDeg();
    }

    return true;
}

bool GameCameraAttach(FGameCamera* Cam, void* Object)
{
    // 0x0051b3c0. Null unbinds; anything else is stored as its Watchable subobject.
    Watchable const* const Target =
        (Object != nullptr)
            ? reinterpret_cast<Watchable const*>(static_cast<unsigned char*>(Object) +
                                                 kWatchableOffset)
            : nullptr;

    // Re-attaching to the same object does not disturb the watcher chain.
    if (Target == Cam->Watched.target())
        return false;

    Cam->Watched.set(Target);
    return true;
}

bool GameCameraHasNoTarget(const FGameCamera* Cam)
{
    // 0x005172f0. Both a null watcher and one holding exactly 8 (an IObjRW at address 0)
    // report "empty".
    const unsigned char* const Raw =
        reinterpret_cast<const unsigned char*>(Cam->Watched.target());
    if (Raw == nullptr)
        return true;
    return (Raw - kWatchableOffset) == nullptr;
}

void* GameCameraGetTarget(const FGameCamera* Cam)
{
    if (Cam->Watched.target() == nullptr)
        return nullptr;
    return const_cast<unsigned char*>(
        reinterpret_cast<const unsigned char*>(Cam->Watched.target())) - kWatchableOffset;
}

bool GameCameraActivate(FGameCamera* Cam, const int* ViewRect4)
{
    // FUN_0051b450.
    const bool bApplied = SpaceCameraSetViewport((FSpaceCamera*)Cam, ViewRect4);

    // Guarded on that result (always true, see the header). Slot 0x5c is VIRTUAL: a HUDCamera
    // lands in FUN_0051cb90, which also hands the rectangle to every HUD element.
    if (bApplied)
        Cam->ApplyViewport(ViewRect4);

    return bApplied;
}

void GameCameraApplyViewport(FGameCamera* Cam, const int* ViewRect4)
{
    // FUN_0051b490: the viewport setter with the dead flag zero, then the letterbox forgets which
    // camera it last drew on -- this reset has just wiped the bars out of that viewport.
    SpaceCameraSetViewportEx((FSpaceCamera*)Cam, ViewRect4, 0);
    ForgetLetterboxCamera();
}

void HudCameraAttach(FHudCamera* Cam, void* Object)
{
    // FUN_0051cbb0. GameCamera's watcher swap, inlined.
    BaseWatcher& Watched = Cam->Watched;
    Watchable const* const Target =
        (Object != nullptr)
            ? reinterpret_cast<Watchable const*>(static_cast<unsigned char*>(Object) +
                                                 kWatchableOffset)
            : nullptr;
    if (Target != Watched.target())
        Watched.set(Target);

    // The HUD is retargeted to the IObjRW when it is a client ship (ManagerKind == 1), otherwise
    // null. `watched - 8 == 0` also reads as null.
    IObjRW* HudTarget = nullptr;
    const unsigned char* const Held = reinterpret_cast<const unsigned char*>(Watched.target());
    if (Held != nullptr)
    {
        IObjRW* const Object8 =
            reinterpret_cast<IObjRW*>(const_cast<unsigned char*>(Held - kWatchableOffset));
        if (Object8 != nullptr && static_cast<FClientGameobj*>(Object8)->ManagerKind == 1)
            HudTarget = Object8;
    }
    HudElementListSetTarget(HudTarget);
}

void HudCameraApplyViewport(FHudCamera* Cam, const int* ViewRect4)
{
    // FUN_0051cb90.
    GameCameraApplyViewport((FGameCamera*)Cam, ViewRect4);
    HudElementListSetViewport(ViewRect4);
}

int HudCameraCommand70(void* Command)
{
    // FUN_0051da40 (and LAB_0051ad20, which tail-jumps here). While a NeuroNet window holds the
    // screen nothing is offered to the HUD; otherwise 1 when some element's Slot7 took it.
    if (!G_bNnScreenUp && HudElementListAnySlot7(Command))
        return 1;
    return 0;
}

int HudCameraCommand74(void* Command)
{
    // LAB_0051da70: the same gate, then Slot8.
    if (G_bNnScreenUp)
        return 0;
    return HudElementListAnySlot8(Command) ? 1 : 0;
}

bool HudCameraActivate(FHudCamera* Cam, const int* ViewRect4)
{
    // FUN_0051cad0.
    SetCockpitResourceStringLoading(true);
    RegisterAcceleratorGroup("IDR_COCKPIT");

    // The nine ids in this order, each registered on the face at +0xf8.
    static const unsigned kCockpitEvents[9] = {0x5c, 0x2a, 0x2b, 0x2c, 0x2d,
                                               0x2e, 0x2f, 0xc7, 0xc8};
    FHudCmdHandler* const Face = Cam->AsHudCmdHandler();
    for (unsigned Index = 0; Index < 9; ++Index)
        HudRegisterCommandHandler(Face, kCockpitEvents[Index]);

    return GameCameraActivate((FGameCamera*)Cam, ViewRect4);
}

void HudCameraDeactivate(FHudCamera* Cam)
{
    // FUN_0051cb70. Retail tail-jumps the empty IDPMsgHandler::OnDisconnect.
    (void)Cam;
    UnregisterAcceleratorGroup("IDR_COCKPIT");
    SetCockpitResourceStringLoading(false);
}

void HudCameraDerivedDeactivate(FHudCamera* Cam)
{
    // FUN_0051b0b0: every command registration of the face at +0xf8 goes, then FUN_0051cb70.
    HudUnregisterCommandHandler(Cam->AsHudCmdHandler());
    HudCameraDeactivate(Cam);
}

void HudCameraDraw(FGameCamera* Cam)
{
    // FUN_0051daa0. A watched object (and not the degenerate 8).
    const unsigned char* const Held =
        reinterpret_cast<const unsigned char*>(Cam->Watched.target());
    if (Held == nullptr || Held - kWatchableOffset == nullptr)
        return;

    if (GetGameOption(kGameOptInterfaceGroup) == 0)
        return;
    if (GetGameOption(kGameOptHud) == 0)
        return;

    FSpaceCamera* const Space = SpaceCameraOf(Cam);
    Win3DApplyCamera(AsCamera(Space));
    const float DeltaSeconds = static_cast<float>(GetClientFrameTimeCapped());
    HudElementListRender(Space, DeltaSeconds);
}

bool HudCameraOnEvent(FHudCamera* Cam, unsigned MsgId)
{
    // FUN_0051d7f0 (slot 0 of the Event::Handler face).
    if (HudIsMousePickBlocked())
        return false;

    // A zeroed 0x28-byte stack record, byte +0x26 = 1. Only event 0x5c stores its id into
    // +0x00, which is what the send test below reads.
    FCliEquipFirePacket Record;
    memset(&Record, 0, sizeof(Record));
    Record.Pad24[2] = 1;

    switch (MsgId)
    {
    case 0x5c:
        Record.Op = static_cast<int>(MsgId);
        break;
    case 0x2a:
        G_HudKeySteerPosX = (HudCommandRecord()->Byte18 != 0) ? 1.0f : 0.0f;
        break;
    case 0x2b:
        G_HudKeySteerNegX = (HudCommandRecord()->Byte18 != 0) ? -1.0f : 0.0f;
        break;
    case 0x2c:
        G_HudKeySteerPosY = (HudCommandRecord()->Byte18 != 0) ? 1.0f : 0.0f;
        break;
    case 0x2d:
        G_HudKeySteerNegY = (HudCommandRecord()->Byte18 != 0) ? -1.0f : 0.0f;
        break;
    case 0x2e:
        G_HudKeySteerPosZ = (HudCommandRecord()->Byte18 != 0) ? 1.0f : 0.0f;
        break;
    case 0x2f:
        G_HudKeySteerNegZ = (HudCommandRecord()->Byte18 != 0) ? -1.0f : 0.0f;
        break;
    case 0xc7:
        // Ignored while the arrow cursor is up.
        if (!MouseIsArrowCursor())
        {
            G_bHudFireHeld = (HudCommandRecord()->Byte18 != 0) ? 1 : 0;
            G_HudFireChanged = 1;
        }
        break;
    case 0xc8:
        ToggleMouseFlightLock();
        break;
    default:
        return false;
    }

    // Nothing to send unless +0x00 was stored (event 0x5c only).
    if (Record.Op == 0)
        return false;

    // The watched client object when it is a ship (+0x1c == 1), else null -- which retail then
    // dereferences, as the port does.
    FClientGameobj* Ship = nullptr;
    const unsigned char* const Held =
        reinterpret_cast<const unsigned char*>(Cam->Watched.target());
    if (Held != nullptr)
    {
        FClientGameobj* const Object = reinterpret_cast<FClientGameobj*>(
            const_cast<unsigned char*>(Held - kWatchableOffset));
        if (Object != nullptr && Object->ManagerKind == 1)
            Ship = Object;
    }

    // Retail calls the object's slot 0x1cc, which for a ship is FUN_00544d70 (vtable 0x005dfd6c,
    // built by FUN_00543380). Port deviation: called directly, since the port's client objects
    // do not carry retail's slot numbering there.
    CliShipHandleEquipUpdate((FClientEquippedObj*)Ship, &Record);
    return true;
}

namespace
{
    // DAT_00675298 -- FUN_0051cc00's clock: frame time summed, one second taken off per scan.
    float G_HudVisitScanClock = 0.0f;

    // .data 0x00612434 -- the radar range assumed when the player ship has no scanner (read
    // only; 0x451c4000).
    const float kDefaultRadarRange = 2500.0f;

    // FUN_0051cc00's constants: .rdata 0x005d8850 (solar radius factor), 0x005ca218 (the
    // smallest solar visit range), 0x005db0b4 (the range for a solar whose visit byte carries
    // 0x10), 0x469c4000 and 0x45ea6000 (the two query radii).
    const float kSolarVisitRadiusScale = 7.0f;
    const float kSolarVisitMinRange = 10000.0f;
    const float kSolarVisitFlag10Range = 7000.0f;
    const float kSolarQueryRadius = 20000.0f;
    const float kZoneQueryRadius = 7500.0f;

    // FUN_0051cc00: once a second, marks nearby solars, zones and factions as visited.
    void HudCameraScanVisits()
    {
        if (IsLetterboxVisible())
            return;

        // The unrounded sum is compared with 1.0f; only "below" leaves (a NaN scans). Then one
        // second comes off the stored float.
        const double Clock = GetClientFrameTimeCapped() + G_HudVisitScanClock;
        G_HudVisitScanClock = static_cast<float>(Clock);
        if (Clock < 1.0f)
            return;
        G_HudVisitScanClock = static_cast<float>(static_cast<double>(G_HudVisitScanClock) - 1.0f);

        IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
        if (Player == nullptr)
            return;

        const Vector PlayerPos = Player->get_position();

        // Pass 1: the object DB's spatial bucket (mask 0xfdff, used unchecked), everything within
        // 20000 of the player.
        {
            ISpatialPartition* const Bucket =
                static_cast<ISpatialPartition*>(GetClientObjDb()->GetBucket(0xfdff));
            Geometry::Sphere Query;
            Query.Center = PlayerPos;
            Query.Radius = kSolarQueryRadius;
            FSpatialQueryResult Result;   // retail: 0x2008 bytes of its 0x4040 stack frame
            Result.Capacity = 0x800;
            Result.Count = 0;
            if (SpatialFill(Bucket, &Query, &Result) != 0)
            {
                for (unsigned Index = 0; Index < Result.Count; ++Index)
                {
                    // The entry's CObject when it is a solar (mask 0x303), else null -- and
                    // GetVisitValue is called either way.
                    FClientGameobj* const Entry = static_cast<FClientGameobj*>(Result.Items[Index]);
                    CObject const* const Obj = Entry->cobject();
                    CSolar const* Solar = nullptr;
                    if (Obj != nullptr &&
                        (static_cast<unsigned>(Obj->ObjectClass) & 0x303u) == 0x303u)
                        Solar = static_cast<CSolar const*>(Obj);
                    const unsigned char Visit = Solar->GetVisitValue();

                    // Already visited (bit 0) or never to be (bit 7).
                    if ((Visit & 0x81u) != 0)
                        continue;

                    // Each component rounded to float; |d| sums y*y + z*z first.
                    const float Dx = static_cast<float>(static_cast<double>(Solar->WorldPosition.x) - PlayerPos.x);
                    const float Dy = static_cast<float>(static_cast<double>(Solar->WorldPosition.y) - PlayerPos.y);
                    const float Dz = static_cast<float>(static_cast<double>(Solar->WorldPosition.z) - PlayerPos.z);
                    double SumSq = static_cast<double>(Dy) * Dy + static_cast<double>(Dz) * Dz;
                    SumSq = SumSq + static_cast<double>(Dx) * Dx;
                    const float Radius = Solar->Radius;
                    const double Gap = sqrt(SumSq) - Radius;

                    // The visit range: radius * 7, at least 10000, and 7000 outright for bit 0x10.
                    double Range = static_cast<double>(Radius) * kSolarVisitRadiusScale;
                    if (Range < kSolarVisitMinRange)
                        Range = kSolarVisitMinRange;
                    if ((Visit & 0x10u) != 0)
                        Range = kSolarVisitFlag10Range;

                    // Strictly inside: the object id marked with the visit byte | 1, server told.
                    if (Gap < Range)
                        SetPlayerVisitFlagsRetail(Solar->Id,
                                                  static_cast<unsigned char>(Visit | 0x01u), true);
                }
            }
        }

        // Pass 2: the current system's zone tree (system pointer unchecked), everything within
        // 7500.
        {
            Universe::ISystem const* const System =
                Universe::get_system(GetPlayerStarSystemIdRetail());
            ISpatialPartition* const Zones = System->GetZoneTree();
            if (Zones != nullptr)
            {
                Geometry::Sphere Query;
                Query.Center = PlayerPos;
                Query.Radius = kZoneQueryRadius;
                FSpatialQueryResult Result;
                Result.Capacity = 0x800;
                Result.Count = 0;
                if (SpatialFill(Zones, &Query, &Result) != 0)
                {
                    for (unsigned Index = 0; Index < Result.Count; ++Index)
                    {
                        // Unless bit 0 or 7 is set, a player inside the zone grown by 7500 marks
                        // it with the byte | 0x29.
                        Universe::IZone const* const Zone =
                            static_cast<Universe::IZone const*>(Result.Items[Index]);
                        const unsigned char Visit = Zone->GetVisit();
                        if ((Visit & 0x81u) != 0)
                            continue;
                        if (!Zone->InZone(PlayerPos, kZoneQueryRadius))
                            continue;
                        SetPlayerVisitFlagsRetail(Zone->GetId(),
                                                  static_cast<unsigned char>(Visit | 0x29u), true);
                    }
                }
            }
        }

        // Pass 3. The radar range: the player ship's first scanner (ship pointer unchecked) or
        // the .data default.
        CEqObj* PlayerShip = nullptr;
        {
            unsigned char* const Plus0c = reinterpret_cast<unsigned char*>(Player) + 0xc;
            if (Plus0c != nullptr)
            {
                CEqObj* const EqObj = *reinterpret_cast<CEqObj**>(Plus0c + 4);
                if (EqObj != nullptr &&
                    (static_cast<unsigned>(EqObj->ObjectClass) & 0x503u) == 0x503u)
                    PlayerShip = EqObj;
            }
        }
        float RadarRange = kDefaultRadarRange;
        CEquip* const Scanner = PlayerShip->EquipManager.FindFirst(0x80000);
        if (Scanner != nullptr)
            RadarRange = static_cast<CEScanner*>(Scanner)->GetRadarRange();

        // Every ship Gameobj (ManagerKind 1).
        int Kind = 1;
        for (FClientGameobj* Go = ClientObjDb_GetKindHead(&Kind); Go != nullptr; Go = Go->ListNext)
        {
            // Already counted (+0xa4), or the player.
            if (Go->GetFieldA4() != 0)
                continue;
            if (static_cast<void*>(Go) == static_cast<void*>(Player))
                continue;

            // Its ship (mask 0x503, else null -- read unchecked); only while not fully cloaked
            // (a NaN passes).
            CEqObj* Ship = nullptr;
            {
                unsigned char* const Plus0c = reinterpret_cast<unsigned char*>(Go) + 0xc;
                if (Plus0c != nullptr)
                {
                    CEqObj* const EqObj = *reinterpret_cast<CEqObj**>(Plus0c + 4);
                    if (EqObj != nullptr &&
                        (static_cast<unsigned>(EqObj->ObjectClass) & 0x503u) == 0x503u)
                        Ship = EqObj;
                }
            }
            if (Ship->CloakPercent >= 1.0f)
                continue;

            // Squared distance in double, strictly inside the squared radar range.
            const double Dx = static_cast<double>(Ship->WorldPosition.x) - PlayerPos.x;
            const double Dy = static_cast<double>(Ship->WorldPosition.y) - PlayerPos.y;
            const double Dz = static_cast<double>(Ship->WorldPosition.z) - PlayerPos.z;
            double DistSq = Dz * Dz + Dy * Dy;
            DistSq = DistSq + Dx * Dx;
            const double RangeSq = static_cast<double>(RadarRange) * RadarRange;
            if (!(RangeSq > DistSq))
                continue;

            Go->SetFieldA4(1);

            // A non-zero GetAffiliation answer skips it.
            int VibeId = static_cast<int>(Ship->VibeId);
            unsigned Affiliation = 0;
            if (Reputation::Vibe::GetAffiliation(VibeId, Affiliation, false) != 0)
                continue;

            // The faction's visit byte (zero when absent); unless bit 0 is set, stored as
            // byte | 0x41 and sent.
            unsigned char Flags = 0;
            GetPlayerVisitFlagsRetail(Affiliation, &Flags, 0, 1);
            if ((Flags & 0x01u) != 0)
                continue;
            SetPlayerVisitFlagsRetail(Affiliation, static_cast<unsigned char>(Flags | 0x41u), true);
        }
    }
}

void HudCameraUpdate(FHudCamera* Cam, float DeltaSeconds)
{
    // FUN_0051d070 (slot 0x6c).
    //
    // The watched object's CEqObj (ObjRW + 0x10) when its class has ship mask 0x503, else null.
    CEqObj* Ship = nullptr;
    {
        unsigned char* const Held = const_cast<unsigned char*>(
            reinterpret_cast<const unsigned char*>(Cam->Watched.target()));
        if (Held != nullptr)
        {
            unsigned char* const Object = Held - kWatchableOffset;
            if (Object != nullptr)
            {
                unsigned char* const Plus0c = Object + 0xc;
                if (Plus0c != nullptr)
                {
                    CEqObj* const EqObj = *reinterpret_cast<CEqObj**>(Plus0c + 4);
                    if (EqObj != nullptr &&
                        (static_cast<unsigned>(EqObj->ObjectClass) & 0x503u) == 0x503u)
                        Ship = EqObj;
                }
            }
        }
    }

    // With a ship, control exclusion 4 (cinematic) drives the cursor latch and the letterbox.
    // Excluded: unless the local server tick is held, cursor off and bars in. Not excluded:
    // cursor on and bars out.
    if (Ship != nullptr)
    {
        if (Ship->is_control_excluded(4u))
        {
            if (!IsClientServerTickPaused())
            {
                SetClientMouseCursorDrawFlag(0);
                if (!IsLetterboxVisible())
                    SetLetterboxTarget(1.0f, 3.0f);
            }
        }
        else
        {
            SetClientMouseCursorDrawFlag(1);
            if (IsLetterboxVisible())
                SetLetterboxTarget(0.0f, 3.0f);
        }
    }

    // The fire latch drops when command 0xc7 is no longer held, the ship is excluded from
    // control 4, or a NeuroNet window is up. Retail asks the exclusion even with no ship.
    if (G_bHudFireHeld != 0)
    {
        if (!KeyMapIsCommandHeld(0xc7u) || Ship->is_control_excluded(4u) || G_bNnScreenUp)
            G_bHudFireHeld = 0;
    }

    // The aim point of the Targetable_Objects element (no null test).
    Vector Aim;
    G_HudTargetableObjects->GetAimPoint(&Aim);

    // Every gun gets that point.
    if (Ship != nullptr)
    {
        CEquipTraverser Walk(EquipTypeGun);
        CEGun* Gun = CEGun::cast(Ship->EquipManager.Traverse(Walk));
        while (Gun != nullptr)
        {
            Gun->SetTargetPoint(Aim);
            Gun = CEGun::cast(Ship->EquipManager.Traverse(Walk));
        }
    }

    // Firing while the latch is up.
    if (G_bHudFireHeld != 0)
    {
        Win3DBroadcast(0x3006u, 0u, 0u);

        // The watched object's +4 is the adjustor whose slot 0x34 fires (FUN_00534870), called
        // directly here as in cli_projectile_fx.
        unsigned char* const Held = const_cast<unsigned char*>(
            reinterpret_cast<const unsigned char*>(Cam->Watched.target()));
        unsigned char* const Object = (Held != nullptr) ? Held - kWatchableOffset : nullptr;

        if (G_HudFireChanged != 0)
        {
            // The first frame after the key changed fires with no hardpoint list at all.
            G_HudFireChanged = 0;
            CliShipFireWeapons(Object + 4, nullptr, 0u, &Aim,
                               static_cast<int>(GetPlayerConnectionId()));   // DAT_00673344
        }
        else if (Ship != nullptr)
        {
            // Every active gun whose munition has no seeker goes into a stack list of hardpoint
            // ids; a non-empty list is fired. Retail's list has no bound check.
            unsigned short HardpointIds[24];
            unsigned Count = 0;
            CEquipTraverser Walk(EquipTypeGun);
            CEGun* Gun = CEGun::cast(Ship->EquipManager.Traverse(Walk));
            while (Gun != nullptr)
            {
                if (Gun->MunitionArch()->Seeker == 0 && Gun->IsActive())
                {
                    HardpointIds[Count] = Gun->Id;
                    ++Count;
                }
                Gun = CEGun::cast(Ship->EquipManager.Traverse(Walk));
            }
            if (Count != 0)
                CliShipFireWeapons(Object + 4, HardpointIds, Count, &Aim,
                                   static_cast<int>(GetPlayerConnectionId()));
        }
    }

    // Marks things in range as visited (the player visit map DAT_00673320).
    HudCameraScanVisits();

    // The HUD's per-frame hazard / proximity warning (FUN_004f5330, on DAT_00674bcc).
    UpdateHudHazardWarning(DeltaSeconds);

    // Function-local statics (guard bits 1 and 2 of DAT_00675268).
    static Vector PrevSteer = {0.0f, 0.0f, 0.0f};  // DAT_0067525c..DAT_00675264
    static Vector LastSent = {0.0f, 0.0f, 0.0f};   // DAT_00675250..DAT_00675258

    Vector Steer;
    Steer.x = G_HudKeySteerNegX + G_HudKeySteerPosX;
    Steer.y = G_HudKeySteerNegY + G_HudKeySteerPosY;
    Steer.z = G_HudKeySteerNegZ + G_HudKeySteerPosZ;

    // The change since last frame; its length (the 0x5f3759df estimate with one Newton step,
    // times the squared length) over 0.5 drops the ramp.
    {
        Vector Delta;
        Delta.x = Steer.x - PrevSteer.x;
        Delta.y = Steer.y - PrevSteer.y;
        Delta.z = Steer.z - PrevSteer.z;
        const double LenSq = static_cast<double>(Delta.z) * Delta.z +
                             static_cast<double>(Delta.y) * Delta.y +
                             static_cast<double>(Delta.x) * Delta.x;
        const float LenSqF = static_cast<float>(LenSq);
        int Bits = *reinterpret_cast<const int*>(&LenSqF);
        Bits = 0x5f3759df - (Bits >> 1);
        const float Seed = *reinterpret_cast<const float*>(&Bits);
        const double Length =
            ((1.5 - static_cast<double>(Seed) * Seed * LenSq * 0.5) * Seed) * LenSq;
        if (Length > 0.5)
            G_HudSteerRamp = 0.0f;
    }

    // Remembered before the clamp.
    PrevSteer = Steer;

    // A NaN passes both tests.
    if (Steer.x < -1.0)
        Steer.x = -1.0f;
    else if (Steer.x > 1.0)
        Steer.x = 1.0f;
    if (Steer.y < -1.0)
        Steer.y = -1.0f;
    else if (Steer.y > 1.0)
        Steer.y = 1.0f;
    if (Steer.z < -1.0)
        Steer.z = -1.0f;
    else if (Steer.z > 1.0)
        Steer.z = 1.0f;

    // No steering drops the ramp; otherwise it grows by rate * dt, capped at 1.
    {
        const double LenSq = static_cast<double>(Steer.y) * Steer.y +
                             static_cast<double>(Steer.x) * Steer.x +
                             static_cast<double>(Steer.z) * Steer.z;
        if (LenSq < 0.01)
        {
            G_HudSteerRamp = 0.0f;
        }
        else
        {
            const double Ramp = static_cast<double>(G_HudSteerRampRate) * DeltaSeconds +
                                G_HudSteerRamp;
            G_HudSteerRamp = static_cast<float>(Ramp);
            if (Ramp > 1.0f)
                G_HudSteerRamp = 1.0f;
        }
    }

    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
    {
        // With a target, steering is scaled by how far the nose is off it:
        // s = 1.5 * (1 - dot^exp), clamped to [0.1, 1], dot = target direction . nose (minus the
        // third orientation column). The ramp is NOT applied on this path.
        unsigned char* const Held = const_cast<unsigned char*>(
            reinterpret_cast<const unsigned char*>(Cam->Watched.target()));
        IObjRW* const Object =
            reinterpret_cast<IObjRW*>((Held != nullptr) ? Held - kWatchableOffset : nullptr);
        IObjRW* Target = nullptr;
        Object->get_target(Target);
        if (Target != nullptr)
        {
            const Vector TargetPos = Target->get_position();
            const Vector& OwnPos = Object->get_position();
            const double Dx = static_cast<double>(TargetPos.x) - OwnPos.x;
            const double Dy = static_cast<double>(TargetPos.y) - OwnPos.y;
            const double Dz = static_cast<double>(TargetPos.z) - OwnPos.z;
            const float DzF = static_cast<float>(Dz);   // rounded, then multiplied by
            const double LenSq = Dz * DzF + Dy * Dy + Dx * Dx;
            const float InvLen = static_cast<float>(1.0 / sqrt(LenSq));
            Vector Dir;
            Dir.x = static_cast<float>(Dx * InvLen);
            Dir.y = static_cast<float>(Dy * InvLen);
            Dir.z = DzF * InvLen;

            const Matrix& Rot = Object->get_transform().Rotation;
            Vector Nose;
            Nose.x = -Rot.E[0][2];
            Nose.y = -Rot.E[1][2];
            Nose.z = -Rot.E[2][2];
            const double Dot = static_cast<double>(Nose.z) * Dir.z +
                               static_cast<double>(Nose.y) * Dir.y +
                               static_cast<double>(Nose.x) * Dir.x;

            double Scale = (1.0 - pow(Dot, static_cast<double>(G_HudShiftSteerExponent))) * 1.5;
            if (Scale < 0.1)
                Scale = 0.1f;
            if (Scale > 1.0)
                Scale = 1.0f;
            Steer.x = static_cast<float>(Steer.x * Scale);
            Steer.y = static_cast<float>(Steer.y * Scale);
            Steer.z = static_cast<float>(Steer.z * Scale);
        }
    }
    else
    {
        Steer.x = Steer.x * G_HudSteerRamp;
        Steer.y = Steer.y * G_HudSteerRamp;
        Steer.z = G_HudSteerRamp * Steer.z;
    }

    // Any axis moved by more than 1e-6 squared since the last send (a NaN counts as moved) ->
    // sent as (-y, -x, -z), in that argument order; the unnegated vector is remembered.
    const float kSendThresholdSq = 1.0000001111620804e-06f;   // .rdata 0x005dc124
    const double DiffX = static_cast<double>(Steer.x) - LastSent.x;
    const double DiffY = static_cast<double>(Steer.y) - LastSent.y;
    const double DiffZ = static_cast<double>(Steer.z) - LastSent.z;
    if (!(DiffX * DiffX <= kSendThresholdSq) || !(DiffY * DiffY <= kSendThresholdSq) ||
        !(DiffZ * DiffZ <= kSendThresholdSq))
    {
        SetPlayerBehaviorDirectionGated(-Steer.y, -Steer.x, -Steer.z);
        LastSent = Steer;
    }

    GameCameraUpdate((FGameCamera*)Cam, DeltaSeconds);
}

void ResetCameraShakeAmount()
{
    // Inlined in the space init (0x0054ad78).
    G_CameraShakeAmount = 0.0f;
}

void HudCameraUpdateShake(FHudCamera* Cam)
{
    // FUN_0051db00. A positive amplitude decays per call and snaps to zero once under 1e-4f
    // (.rdata 0x005db098; compared unrounded).
    if (G_CameraShakeAmount > 0.0f)
    {
        const double Decayed = static_cast<double>(G_CameraShakeAmount) - G_CameraShakeDecay;
        G_CameraShakeAmount = static_cast<float>(Decayed);
        if (Decayed < 9.999999747378752e-05f)
            G_CameraShakeAmount = 0.0f;
    }

    const unsigned char* const Held =
        reinterpret_cast<const unsigned char*>(Cam->Watched.target());
    if (Held == nullptr)
        return;
    if (Held - 8 == nullptr)
        return;
    // Retail reads the Ship Gameobj fields straight off that object at fixed offsets. Port: the
    // watched object is its IObjInspect face, so dynamic_cast gets the object (as
    // GameobjJumpArmState does); the downcast to the Ship class is unchecked, like retail.
    FClientShipGameobj* const Ship = static_cast<FClientShipGameobj*>(dynamic_cast<FClientGameobj*>(
        reinterpret_cast<IObjInspect*>(const_cast<unsigned char*>(Held - 8))));

    // +0x18c bit 0, a hull hit (set by slot 105 FUN_00545ed0).
    if ((Ship->ShipFlags & 0x1u) != 0)
    {
        G_CameraShakeAmount = 0.02f;
        Ship->ShipFlags = static_cast<unsigned char>(Ship->ShipFlags & 0xfeu);
    }

    // Bit 1, a shield hit: raises the amplitude to 0.005f (.rdata 0x005d3e44) unless already
    // strictly greater.
    if ((Ship->ShipFlags & 0x2u) != 0)
    {
        if (!(G_CameraShakeAmount > 0.005f))
            G_CameraShakeAmount = 0.005f;
        Ship->ShipFlags = static_cast<unsigned char>(Ship->ShipFlags & 0xfdu);
    }

    // In a jump with a jump effect: the effect's amplitude unless the current one is strictly
    // greater (NaN takes the effect's).
    if (Ship->JumpState != 0 && Ship->JumpEffect != nullptr)
    {
        double const Jump = static_cast<FJumpShipEffect*>(Ship->JumpEffect)->ShakeAmplitude();
        if (!(static_cast<double>(G_CameraShakeAmount) > Jump))
            G_CameraShakeAmount = static_cast<float>(Jump);
    }
}

void HudCameraApplyShake(FHudCamera* Cam, float* Orientation)
{
    // FUN_0051dbd0 on a 3x3 row-major orientation. The same two-rotation shake as the death
    // camera.
    HudCameraUpdateShake(Cam);

    if (!(G_CameraShakeAmount > 0.0f))
        return;

    // a = rand() * (1/32768) * (amp - -amp) + -amp; cos rounded to float, sin kept unrounded.
    {
        const float Amp = G_CameraShakeAmount;
        const float NegAmp = -G_CameraShakeAmount;
        const int Random = rand();
        const double Angle = static_cast<double>(Random) * 3.0517578125e-05f *
                                 (static_cast<double>(Amp) - NegAmp) + NegAmp;
        const float CosA = static_cast<float>(cos(Angle));
        const double SinA = sin(Angle);

        // Rows 0 and 2.
        for (int Column = 0; Column < 3; ++Column)
        {
            const float Row0 = Orientation[Column];
            const float Row2 = Orientation[6 + Column];
            Orientation[Column] = static_cast<float>(SinA * Row2 + static_cast<double>(Row0) * CosA);
            Orientation[6 + Column] =
                static_cast<float>(static_cast<double>(CosA) * Row2 - Row0 * SinA);
        }
    }

    // A second draw, rows 0 and 1.
    {
        const float Amp = G_CameraShakeAmount;
        const float NegAmp = -G_CameraShakeAmount;
        const int Random = rand();
        const double Angle = static_cast<double>(Random) * 3.0517578125e-05f *
                                 (static_cast<double>(Amp) - NegAmp) + NegAmp;
        const float CosB = static_cast<float>(cos(Angle));
        const double SinB = sin(Angle);

        for (int Column = 0; Column < 3; ++Column)
        {
            const float Row0 = Orientation[Column];
            const float Row1 = Orientation[3 + Column];
            Orientation[Column] =
                static_cast<float>(static_cast<double>(CosB) * Row0 - SinB * Row1);
            Orientation[3 + Column] =
                static_cast<float>(static_cast<double>(CosB) * Row1 + Row0 * SinB);
        }
    }
}

void CameraOrientFromForwardAndUp(float* Out, const float* M, const float* Up)
{
    // FUN_00516fe0. Forward F = column 2 of M (M[2], M[5], M[8]).
    const double A2 = M[2];
    const double A5 = M[5];
    const float A8 = M[8];   // retail spills it to a float local and reloads it

    // Summed as (a2*u0 + a8*u2) + a5*u1.
    double Dot = A2 * Up[0] + static_cast<double>(A8) * Up[2];
    Dot = Dot + A5 * Up[1];

    float Rx;
    float Ry;
    float Rz;
    // The cross product only when strictly under 0.999 (a NaN takes the fallback).
    if (fabs(Dot) < 0.999)
    {
        // R = Up x F, normalised. z is rounded to float once and the square uses z * (float)z;
        // the reciprocal length is rounded to float before use.
        const double X = static_cast<double>(A8) * Up[1] - A5 * Up[2];
        const double Y = A2 * Up[2] - static_cast<double>(A8) * Up[0];
        const double Z = A5 * Up[0] - A2 * Up[1];
        const float Zf = static_cast<float>(Z);
        double SumSq = Z * Zf + Y * Y;
        SumSq = SumSq + X * X;
        const float Inv = static_cast<float>(1.0f / sqrt(SumSq));
        Rx = static_cast<float>(X * Inv);
        Ry = static_cast<float>(Y * Inv);
        Rz = static_cast<float>(static_cast<double>(Zf) * Inv);
    }
    else
    {
        // Column 0 of M.
        Rx = M[0];
        Ry = M[3];
        Rz = M[6];
    }

    // C = F x R, each component rounded to float.
    const float C0 = static_cast<float>(static_cast<double>(Rz) * A5 - static_cast<double>(Ry) * A8);
    const float C1 = static_cast<float>(static_cast<double>(A8) * Rx - static_cast<double>(Rz) * A2);
    const float C2 = static_cast<float>(static_cast<double>(Ry) * A2 - static_cast<double>(Rx) * A5);

    // Columns {R, C, F}.
    Out[0] = Rx;
    Out[1] = C0;
    Out[2] = M[2];
    Out[3] = Ry;
    Out[4] = C1;
    Out[5] = M[5];
    Out[6] = Rz;
    Out[7] = C2;
    Out[8] = A8;
}

void FGameCamera::GetVelocity(float* OutVelocity)
{
    GameCameraGetVelocity(this, OutVelocity);
}

int FGameCamera::SetProperty(char const* Key, char const* Value)
{
    return GameCameraSetProperty(this, Key, Value) ? 1 : 0;
}

char const* FGameCamera::GetName()
{
    return GameCameraGetName();
}

void FGameCamera::Attach(void* Object)
{
    GameCameraAttach(this, Object);
}

void FGameCamera::ApplyViewport(int const* ViewRect4)
{
    GameCameraApplyViewport(this, ViewRect4);
}

bool FGameCamera::Activate(int const* ViewRect4)
{
    return GameCameraActivate(this, ViewRect4);
}

bool FGameCamera::IsFinished()
{
    return false;
}

void FGameCamera::Deactivate()
{
}

void FGameCamera::Update(float DeltaSeconds)
{
    GameCameraUpdate(this, DeltaSeconds);
}

int FGameCamera::HandleCommand70(void* Command)
{
    return 0;
}

int FGameCamera::HandleCommand74(void* Command)
{
    return 0;
}

void FGameCamera::DrawScene(unsigned LayerFlags, unsigned Param2)
{
    SpaceObjDrawPass((FSpaceCamera*)this, LayerFlags, Param2);
}

void FGameCamera::ScaleEnvironmentEffects()
{
    SpaceEnvFxBeginCockpitPass((FSpaceCamera*)this);
    SpaceEnvFxEndCockpitPass((FSpaceCamera*)this);
}

int FGameCamera::GetSoundMode()
{
    return 1;
}

char const* FHudCamera::GetName()
{
    return HudCameraGetName();
}

void FHudCamera::Attach(void* Object)
{
    HudCameraAttach(this, Object);
}

void FHudCamera::ApplyViewport(int const* ViewRect4)
{
    HudCameraApplyViewport(this, ViewRect4);
}

bool FHudCamera::IsFinished()
{
    return GameCameraHasNoTarget((FGameCamera*)this);
}

bool FHudCamera::Activate(int const* ViewRect4)
{
    return HudCameraActivate(this, ViewRect4);
}

void FHudCamera::Deactivate()
{
    HudCameraDeactivate(this);
}

void FHudCamera::Update(float DeltaSeconds)
{
    HudCameraUpdate(this, DeltaSeconds);
}

int FHudCamera::HandleCommand70(void* Command)
{
    return HudCameraCommand70(Command);
}

int FHudCamera::HandleCommand74(void* Command)
{
    return HudCameraCommand74(Command);
}

void FHudCamera::DrawScene(unsigned LayerFlags, unsigned Param2)
{
    HudCameraDraw((FGameCamera*)this);
}

int FHudCamera::GetSoundMode()
{
    return 2;
}

bool FHudCamera::OnEvent(unsigned MsgId)
{
    return HudCameraOnEvent(this, MsgId);
}
