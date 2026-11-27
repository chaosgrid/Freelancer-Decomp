// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail first-person (cockpit) camera (retail Cockpit.cpp; in-flight controller + 0x00c,
// vtable .rdata 0x005db944): ctor FUN_00517180, name 0x00517330, ini loader FUN_005173c0,
// attach LAB_005178e0, Event::Handler face FUN_005183e0, mesh release FUN_00518410, update
// LAB_00517ba0 (slot 0x6c) and scene draw LAB_00518420 (slot 0x78).

#include "Space/Cameras/space_camera_first.h"

#include "DALib.h"
#include "common_cobject.h"   // CompoundInstanceAnalyze
#include "common_csys.h"      // Acquire3DMathEngine (FUN_005b73e0)
#include "common_goods.h"     // FindHardpoint_PS, HardpointInfo
#include "common_iface.h"     // IObjRW / IObjInspect (thiscall getters)
#include "common_ini.h"
#include "dacom.h"
#include "engine/engbase.h"
#include "Space/client_gameobj.h"   // FClientShipGameobj (+0x158 / +0x188, slot 0x18c)
#include "Common/engine_light.h"     // EngineLightClearRegisteredList / EngineLightSetAmbientRgbBytes
#include "Core/game_options.h"     // GetGameOption (FUN_005792b0)
#include "Core/globals.h"          // CopyBoundedString (FUN_00417c50)
#include "Space/Effects/jump_effect.h"      // FJumpShipEffect::Ambient (FUN_00504870)
#include "Space/player_behavior.h"  // GetPlayerObjRW (FUN_0054baf0)
#include "Space/HUD/hud_target.h"       // HudIsMousePickBlocked (FUN_0045a740)
#include "Core/perf_options.h"     // FindPerfOption (FUN_005b36b0)
#include "Space/space_scene_obj_draw.h" // FGameCamera::DrawScene (FUN_0051c200), FUN_0051c970 / FUN_0051c9e0
#include "Interface/win3d_render.h"     // ClearWin3DBuffers (FUN_004215e0)
#include "x86math.h"

#include <cstddef>

#include <math.h>
#include <string.h>

// Written only by the [Cockpit] head_turn loader; nothing in the PE reads them.
float G_CockpitHeadTurnX = 30.0f;   // DAT_006135a0
float G_CockpitHeadTurnY = 25.0f;   // DAT_006135a4

// DAT_00675220: this file's own lazily-filled math engine cache (FUN_005b73e0).
I3DMathEngine* G_FirstPersonMathEngine = nullptr;

namespace
{
    const float  kDegreesToRadians = 0.01745329238474369f;   // .rdata 0x005c77f4
    const double kExpectedAspect = 1.333;
    const double kAspectTolerance = 0.05;

    // FUN_00417c50's capacity at both call sites in the loader.
    const unsigned kIniStringCapacity = 0x104;

    const char kCockpitCpp[] = "E:\\FL\\Scratch\\Source\\Client\\space\\Cockpit.cpp";

    // LAB_00517ba0's head-motion state (.bss) and tuning (.data), each read only by that update.
    float G_CockpitLastForwardSpeed = 0.0f;   // DAT_00675228
    float G_CockpitHeadLag = 0.0f;            // DAT_0067522c
    float G_CockpitLagScale = 0.3f;           // DAT_006135a8 (0x3e99999a)
    float G_CockpitHeadPivot = 1.5f;          // DAT_006135ac
    float G_CockpitRollPerSpin = -4.0f;       // DAT_006135b8
    float G_CockpitRollLimit = 4.0f;          // DAT_006135bc

    // Event::Handler face table .rdata 0x005db93c: one slot, FUN_005183e0.

    // Slot 0x50 is IDPMsgHandler::OnDisconnect (0x00401e90, a bare RET), not GameCamera's
    // FUN_0051c8b0 -- the scene slot runs the two list walks itself around the cockpit draw.

    I3DMathEngine* FirstPersonMathEngine()
    {
        if (G_FirstPersonMathEngine == nullptr)
            G_FirstPersonMathEngine = Acquire3DMathEngine();
        return G_FirstPersonMathEngine;
    }

    Matrix const& AsMatrix(const float* Nine)
    {
        return *reinterpret_cast<Matrix const*>(Nine);
    }

    Vector const& AsVector(const Vector* Three)
    {
        return *reinterpret_cast<Vector const*>(Three);
    }

    // FUN_00517130 -- the CompoundInstanceAnalyze callback. Asks the engine for the part's name
    // (IEngine slot 0x68, GetInstanceName, into a stack FEngineName), treats a null name as ""
    // (.rdata 0x005c865c), and records the part in the context when the name contains "cam"
    // (.rdata 0x005db934). No early out: a later match overwrites an earlier one.
    void __cdecl FindCockpitCameraPart(long Instance, void* Context)
    {
        FEngineName Name;
        const char* Text =
            DALib::Engine->GetInstanceName(&Name, Instance)->Text;
        if (Text == nullptr)
            Text = "";
        if (strstr(Text, "cam") != nullptr)
            *static_cast<long*>(Context) = Instance;
    }

    void SetIdentity(float* M)
    {
        for (int i = 0; i < 9; ++i)
            M[i] = 0.0f;
        M[0] = 1.0f;
        M[4] = 1.0f;
        M[8] = 1.0f;
    }
}

void FFirstPersonCamera::Construct()
{
    // FUN_00517180.
    FHudCamera::Construct();

    // FUN_00420d40 constructs the cockpit mesh holder in place at +0x190.
    Mesh.Construct();

    // Three transforms, each a zero position and an identity matrix (retail stores them in a
    // different order, same result).
    CockpitHpPosition.x = 0.0f;
    CockpitHpPosition.y = 0.0f;
    CockpitHpPosition.z = 0.0f;
    SetIdentity(&CockpitHpOrientation.E[0][0]);

    HeadPosition.x = 0.0f;
    HeadPosition.y = 0.0f;
    HeadPosition.z = 0.0f;
    SetIdentity(&HeadOrientation.E[0][0]);

    EyePosition.x = 0.0f;
    EyePosition.y = 0.0f;
    EyePosition.z = 0.0f;
    SetIdentity(EyeOrientation);

    // Clears BOTH HUDCamera bytes +0xfc and +0xfd -- the only evidence +0xfd is a field rather
    // than padding. +0xfe keeps the 1 the HUDCamera ctor put there.
    bFlagFC = 0;
    bFlagFD = 0;

    bJustAttached = 1;
}

const char* FirstPersonCameraGetName()
{
    return "FirstPerson";       // 0x00517330 -> .rdata
}

bool FFirstPersonCamera::OnEvent(unsigned MsgId)
{
    // FUN_005183e0 (on the face): FUN_0045a740 -> false; otherwise HUDCamera's FUN_0051d7f0 on
    // the same face, its answer normalised to 0 / 1.
    if (HudIsMousePickBlocked())
        return false;
    return FHudCamera::OnEvent(MsgId) != false;
}

void FFirstPersonCamera::ReleaseMesh()
{
    // FUN_00518410: a tail jump to FUN_00420f70 on +0x190.
    Mesh.Release();
}

void FFirstPersonCamera::Attach(IObjRW* Object)
{
    // LAB_005178e0 (slot 0x58).
    FHudCamera::Attach(Object);

    FSpaceCamera* const Space = SpaceCameraOf(this);

    IObjRW* const Watched = GameCameraGetTarget(this);
    if (Watched != nullptr)
    {
        IObjInspect* const Inspect = Watched;

        // Retail calls slot 4 (get_orientation), whose body is folded with slot 3 and returns
        // the whole transform: twelve dwords into the world rotation + position.
        Transform const& ObjectXform = Inspect->get_transform();
        Space->bViewDirty = 1;
        memcpy(&Space->World, &ObjectXform, 12 * sizeof(float));

        long Handle = 0;
        HardpointInfo Info;
        if (FindHardpoint_PS(Inspect->get_index(), "HpCockpit", Handle, Info))
        {
            CockpitHpPosition.x = Info.Position.x;                           // +0x100
            CockpitHpPosition.y = Info.Position.y;
            CockpitHpPosition.z = Info.Position.z;
            memcpy(&CockpitHpOrientation, &Info.Orient, 9 * sizeof(float));   // +0x10c
        }
        else
        {
            // No cockpit hardpoint: four units behind the origin, identity.
            CockpitHpPosition.x = 0.0f;
            CockpitHpPosition.y = 0.0f;
            CockpitHpPosition.z = -4.0f;                                     // 0xc0800000
            CockpitHpOrientation.E[2][2] = 1.0f;
            CockpitHpOrientation.E[1][1] = 1.0f;
            CockpitHpOrientation.E[0][0] = 1.0f;
            CockpitHpOrientation.E[2][1] = 0.0f;
            CockpitHpOrientation.E[2][0] = 0.0f;
            CockpitHpOrientation.E[1][2] = 0.0f;
            CockpitHpOrientation.E[1][0] = 0.0f;
            CockpitHpOrientation.E[0][2] = 0.0f;
            CockpitHpOrientation.E[0][1] = 0.0f;
        }
    }

    // Retail tests the watcher again.
    IObjRW* const Again = GameCameraGetTarget(this);
    if (Again != nullptr)
    {
        I3DMathEngine* const Math = FirstPersonMathEngine();

        // The head's base orientation in world space; kept in a second stack copy.
        Matrix Product;
        Math->MulMatrix(Product, Space->World, CockpitHpOrientation);
        Matrix const Head = Product;

        Vector const& ObjectPosition = static_cast<IObjInspect*>(Again)->get_position();
        float Position[3] = {ObjectPosition.x, ObjectPosition.y, ObjectPosition.z};

        // + world rotation x hardpoint position.
        Vector Offset;
        FirstPersonMathEngine()->TransformMatrix(Offset, Space->World,
                                                 CockpitHpPosition);
        Position[0] = Offset.x + Position[0];
        Position[1] = Offset.y + Position[1];
        Position[2] = Offset.z + Position[2];

        // + head orientation x the cockpit mesh's camera-part position (+0x130).
        FirstPersonMathEngine()->TransformMatrix(Offset, Head, HeadPosition);
        Position[0] = Offset.x + Position[0];
        Position[1] = Offset.y + Position[1];
        Position[2] = Offset.z + Position[2];

        // +0x160 keeps the eye point; world = {head orientation, eye point}.
        EyePosition.x = Position[0];
        EyePosition.y = Position[1];
        EyePosition.z = Position[2];
        Space->bViewDirty = 1;
        memcpy(&Space->World, &Head, 9 * sizeof(float));
        Space->WorldPos.x = Position[0];
        Space->WorldPos.y = Position[1];
        Space->WorldPos.z = Position[2];
    }

    bJustAttached = 1;
}

void FFirstPersonCamera::Update(float DeltaSeconds)
{
    // LAB_00517ba0 (slot 0x6c).
    FHudCamera* const Hud = this;
    FSpaceCamera* const Space = SpaceCameraOf(Hud);

    // Only with a watched client ship.
    if (GameCameraWatchedShip(Hud) != nullptr)
    {
        IObjRW* const Object = GameCameraGetTarget(Hud);

        // The ship's rotation is read through this reference for the rest of the function.
        Transform const& ShipXform = Object->get_transform();
        Matrix const& ShipRot = ShipXform.Rotation;

        // Ship rotation x cockpit hardpoint orientation (+0x10c) -> this frame's mesh rotation.
        Matrix CockpitRot;
        FirstPersonMathEngine()->MulMatrix(CockpitRot, ShipRot, CockpitHpOrientation);
        Matrix MeshRot = CockpitRot;

        Vector const& ShipPos = Object->get_position();
        Vector Eye = {ShipPos.x, ShipPos.y, ShipPos.z};

        // On the first update after an attach (+0x1a0), back to the normal camera mode.
        IBehaviorManager* const Manager = GetBehaviorManager(Object);
        if (bJustAttached != 0 && Manager != nullptr)
            Manager->set_camera_mode(IBehaviorManager::CameraModeNormal);

        // + ship rotation x cockpit hardpoint position (+0x100), kept in +0x160.
        Vector Offset;
        FirstPersonMathEngine()->TransformMatrix(Offset, ShipRot, CockpitHpPosition);
        Eye.x = Offset.x + Eye.x;
        EyePosition.x = Eye.x;
        Eye.y = Offset.y + Eye.y;
        EyePosition.y = Eye.y;
        Eye.z = Offset.z + Eye.z;
        EyePosition.z = Eye.z;

        // The cockpit mesh follows.
        Mesh.SetPosition(&Eye);
        Mesh.SetOrientation(&MeshRot.E[0][0]);
        Mesh.Update();

        // Velocity into ship space.
        const Vector Velocity = Object->get_velocity();
        Vector LocalVelocity;
        FirstPersonMathEngine()->TransposeTransform(LocalVelocity, ShipRot,
                                                    Velocity);

        // Forward acceleration, 0 unless dt > 0.
        float Accel = 0.0f;
        if (DeltaSeconds > 0.0)
        {
            Accel = static_cast<float>(
                (static_cast<double>(LocalVelocity.z) - G_CockpitLastForwardSpeed) / DeltaSeconds);
        }

        // The lag the head wants, (1 - 1/(|a| + 1)) * 0.3f with a's sign, less the current lag,
        // times 0.01.
        double Step = (1.0 - 1.0 / (fabs(static_cast<double>(Accel)) + 1.0)) * G_CockpitLagScale;
        if (Accel < 0.0f)
            Step = -Step;
        Step = Step - G_CockpitHeadLag;
        G_CockpitLastForwardSpeed = LocalVelocity.z;
        Step = Step * 0.01;

        // At most 3 * dt either way.
        const float Limit = static_cast<float>(DeltaSeconds * 3.0);
        const float NegLimit = -Limit;
        if (Step < NegLimit)
            Step = NegLimit;
        else if (Step > Limit)
            Step = Limit;
        G_CockpitHeadLag = static_cast<float>(Step + G_CockpitHeadLag);
        const float Lag = G_CockpitHeadLag;

        // The mesh's orientation back from the engine.
        memcpy(&MeshRot, Mesh.GetOrientation(), sizeof(MeshRot));

        // The rigid eye: mesh position + mesh rotation x the camera part's position (+0x130).
        Vector HeadOffset;
        FirstPersonMathEngine()->TransformMatrix(HeadOffset, MeshRot, HeadPosition);
        {
            const float* const MeshPos = (const float*)Mesh.GetPosition();
            Eye.x = MeshPos[0] + HeadOffset.x;
            Eye.y = MeshPos[1] + HeadOffset.y;
            Eye.z = MeshPos[2] + HeadOffset.z;
        }
        // Retail also writes an identity to a stack matrix the roll overwrites before any read.

        // A function-local static (guard DAT_00675210 bit 0) takes the angular velocity, then
        // its ship-space version.
        static Vector LocalSpin = {0.0f, 0.0f, 0.0f};   // DAT_00675204..DAT_0067520c
        LocalSpin = Object->get_angular_velocity();
        Vector Spin;
        FirstPersonMathEngine()->TransposeTransform(Spin, ShipRot, LocalSpin);
        LocalSpin.x = Spin.x;
        LocalSpin.y = Spin.y;
        LocalSpin.z = Spin.z;

        // Roll from the ship-space spin about Y, clamped.
        double Roll = static_cast<double>(LocalSpin.y) * G_CockpitRollPerSpin;
        const float NegRollLimit = -G_CockpitRollLimit;
        if (Roll < NegRollLimit)
            Roll = NegRollLimit;
        else if (Roll > G_CockpitRollLimit)
            Roll = G_CockpitRollLimit;
        Vector RollAngles;
        RollAngles.z = static_cast<float>(Roll);
        RollAngles.x = 0.0f;
        RollAngles.y = 0.0f;
        const Matrix RollMatrix = RotateMatrix(RollAngles);

        // The head pivot (0, pivot, 0) through the camera part's orientation (+0x13c), taken off
        // the part's position (+0x130).
        Vector Pivot;
        Pivot.x = 0.0f;
        Pivot.y = G_CockpitHeadPivot;
        Pivot.z = 0.0f;
        Vector PivotOffset;
        FirstPersonMathEngine()->TransformMatrix(PivotOffset, HeadOrientation,
                                                 Pivot);
        float Base[3];
        Base[0] = HeadPosition.x - PivotOffset.x;
        Base[1] = HeadPosition.y - PivotOffset.y;
        Base[2] = HeadPosition.z - PivotOffset.z;

        // The pivot pushed back by the lag, rolled, put back on the base.
        Vector Swing;
        Swing.x = Pivot.x;
        Swing.y = Pivot.y;
        Swing.z = static_cast<float>(static_cast<double>(Pivot.z) + -static_cast<double>(Lag));
        Vector Swung;
        FirstPersonMathEngine()->TransformMatrix(Swung, RollMatrix,
                                                 Swing);
        Vector Local;
        Local.x = Swung.x + Base[0];
        Local.y = Swung.y + Base[1];
        Local.z = Swung.z + Base[2];

        // Into the mesh's frame, onto the mesh position -> +0x160.
        Vector Moved;
        FirstPersonMathEngine()->TransformMatrix(Moved, MeshRot, AsVector(&Local));
        {
            const Vector* const MeshPos = Mesh.GetPosition();
            EyePosition.x = Moved.x + MeshPos->x;
            EyePosition.y = Moved.y + MeshPos->y;
            EyePosition.z = Moved.z + MeshPos->z;
        }

        // The "cockpit" perf option (no null test) under 0.999 keeps the head rigid; otherwise
        // the lag and roll stay.
        Matrix Final;
        if (FindPerfOption("cockpit")->Applied < 0.999)
        {
            // The eye back to the rigid point; mesh x part (+0x13c) x +0x16c.
            EyePosition.x = Eye.x;
            EyePosition.y = Eye.y;
            EyePosition.z = Eye.z;
            Matrix Head;
            FirstPersonMathEngine()->MulMatrix(Head, MeshRot, HeadOrientation);
            const Matrix HeadCopy = Head;
            FirstPersonMathEngine()->MulMatrix(Final, HeadCopy, AsMatrix(EyeOrientation));
        }
        else
        {
            // The same product, then x the roll.
            Matrix Head;
            FirstPersonMathEngine()->MulMatrix(Head, MeshRot, HeadOrientation);
            const Matrix HeadCopy = Head;
            Matrix Offsetted;
            FirstPersonMathEngine()->MulMatrix(Offsetted, HeadCopy, AsMatrix(EyeOrientation));
            const Matrix OffsettedCopy = Offsetted;
            FirstPersonMathEngine()->MulMatrix(Final, OffsettedCopy, RollMatrix);
        }

        Hud->ApplyShake(&Final);

        memcpy(&Space->World, &Final, 9 * sizeof(float));
        Space->WorldPos.x = EyePosition.x;
        Space->WorldPos.y = EyePosition.y;
        Space->WorldPos.z = EyePosition.z;
        Space->bViewDirty = 1;
    }

    // HUDCamera's update, then the attach flag drops.
    Hud->FHudCamera::Update(DeltaSeconds);
    bJustAttached = 0;
}

void FFirstPersonCamera::DrawScene(unsigned LayerFlags, unsigned Param2)
{
    // LAB_00518420 (slot 0x78).
    FHudCamera* const Hud = this;
    FSpaceCamera* const Space = SpaceCameraOf(Hud);

    // The watcher at +0xc0 set, and the object (watcher - 8) non-null.
    const unsigned char* const Held =
        reinterpret_cast<const unsigned char*>(Hud->Watched.target());
    if (Held == nullptr)
        return;
    if (Held - 8 == nullptr)
        return;

    Hud->FGameCamera::DrawScene(LayerFlags, Param2);

    // The "cockpit" perf option's applied value (no null test), read once for both compares.
    const float Applied = FindPerfOption("cockpit")->Applied;

    // Strictly greater (ordered) clears depth outright; otherwise only with the HUD option on.
    if (static_cast<double>(Applied) > 0.999 || GetGameOption(kGameOptHud) != 0)
        ClearWin3DBuffers(kWin3DClearDepth);

    Space->EnvFxBeginCockpitPass();

    if (static_cast<double>(Applied) > 0.999)
    {
        // The cockpit mesh pass, under near/far 0.05 / 500 (.rdata 0x005c8914 / 0x005c8918).
        ICamera* const Camera = AsCamera(Space);
        const float SavedNear = Camera->GetZNear();
        const float SavedFar = Camera->GetZFar();
        Space->SetNearFar(0.05f, 500.0f);
        Win3DApplyCamera(Camera);

        // The player in the jump arm (+0x158 == 2 or 3, read without a class test): registered
        // lights cleared, then the jump effect's ambient when both are set.
        FClientShipGameobj* const Ship =
            static_cast<FClientShipGameobj*>(GetPlayerObjRW());
        if (Ship != nullptr && (Ship->JumpState == kJumpStateJumpOutTunnel ||
                                Ship->JumpState == kJumpStateJumpInTunnel))
        {
            EngineLightClearRegisteredList();
            FJumpShipEffect* const JumpFx = Ship->JumpEffect;
            if (JumpFx != nullptr && JumpFx->Ambient != nullptr)
                EngineLightSetAmbientRgbBytes(JumpFx->Ambient);
        }
        else
        {
            // Object lighting for the cockpit mesh's bounding sphere, with the ship's slot 0x18c
            // draw state (0.999: DAT_005c8b68). A missing player reaches the slot call through
            // null, as in retail.
            float Center[3];
            const float Radius = Mesh.GetBoundingSphere(Center);
            unsigned char Flag = 1;
            if (SpaceEffectPerfOneMinus() < 0.999)
                Flag = 0;
            unsigned DrawStateB = 0;
            const unsigned DrawStateA = Ship->GetNebula(&DrawStateB);
            SpaceApplyObjectLighting(Center, Radius, Space, DrawStateA, DrawStateB, Flag);
        }

        // The cockpit draws in depth range [0.75, 1], the world in [0, 0.75].
        DALib::Pipeline->SetDepthRange(0.75f, 1.0f);
        if (Mesh.HasInstance())
            Mesh.Render(Space, 0x100);
        DALib::Pipeline->SetDepthRange(0.0f, 0.75f);
        Space->SetNearFar(SavedNear, SavedFar);
    }

    // HUDCamera's slot 0x78 body, called directly.
    Hud->FHudCamera::DrawScene(LayerFlags, Param2);

    Space->EnvFxEndCockpitPass();
}

void FFirstPersonCamera::Load(const char* CockpitIniPath)
{
    // FUN_005173c0.

    // Two length-prefixed 0x104-byte strings, both {0, ""} before the reader opens.
    unsigned MeshLength = 0;
    char MeshName[kIniStringCapacity];
    MeshName[0] = 0;
    unsigned MaterialLibraryLength = 0;
    char MaterialLibrary[kIniStringCapacity];
    MaterialLibrary[0] = 0;

    INI_Reader Reader;
    if (!Reader.open(CockpitIniPath, false))
    {
        // Client\space\Cockpit.cpp:0xe4 -- a warning only; the rest of the loader still runs.
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: cockpit file not found '%s'\n",
              kCockpitCpp, 0xe4, CockpitIniPath);
    }

    while (Reader.read_header())
    {
        if (Reader.is_header("Cockpit"))
        {
            while (Reader.read_value())
            {
                if (Reader.is_value("mesh"))
                {
                    // A null value clears the string; otherwise at most 0x103 characters are
                    // copied and the length is stored.
                    const char* const Value = Reader.get_value_string();
                    if (Value == nullptr)
                    {
                        MeshLength = 0;
                        MeshName[0] = 0;
                    }
                    else
                    {
                        MeshLength = CopyBoundedString(MeshName, kIniStringCapacity, Value);
                    }
                }
                else if (Reader.is_value("material_library"))
                {
                    const char* const Value = Reader.get_value_string();
                    if (Value == nullptr)
                    {
                        MaterialLibraryLength = 0;
                        MaterialLibrary[0] = 0;
                    }
                    else
                    {
                        MaterialLibraryLength =
                            CopyBoundedString(MaterialLibrary, kIniStringCapacity, Value);
                    }
                }
                else if (Reader.is_value("head_turn"))
                {
                    G_CockpitHeadTurnX = Reader.get_value_float(0);
                    G_CockpitHeadTurnY = Reader.get_value_float(1);
                }
            }
            continue;
        }

        if (!Reader.is_header("Camera"))
            continue;

        // Client\space\Cockpit.cpp:0xf9
        FDUMP(kFdumpWarning,
              "%s(%d) : *** WARNING: header [Camera] is no longer supported, found in file (%s)\n",
              kCockpitCpp, 0xf9, CockpitIniPath);
    }
    (void)MaterialLibraryLength;

    // Loaded only when the stored LENGTH is non-zero; the material library is passed as an
    // empty string, never null, when not given.
    if (MeshLength != 0)
        Mesh.Load(MeshName, MaterialLibrary);

    // Placed at the origin and updated, unconditionally.
    const Vector Origin = {0.0f, 0.0f, 0.0f};
    Mesh.SetPosition(&Origin);    // FUN_00420fd0
    Mesh.Update();                 // FUN_00421070

    // The head transform comes from the part whose name contains "cam", if any: IEngine slot
    // 0x98 into +0x13c and slot 0x90 into +0x130.
    long CameraPart = -1;
    CompoundInstanceAnalyze(reinterpret_cast<long>(Mesh.EngineInstance),
                            &FindCockpitCameraPart, &CameraPart);
    if (CameraPart != -1)
    {
        const InstanceIndex Part = CameraPart;
        memcpy(&HeadOrientation, DALib::Engine->GetInstanceOrientation(Part),
               9 * sizeof(float));
        const Vector* Position = DALib::Engine->GetInstancePosition(Part);
        HeadPosition.x = Position->x;
        HeadPosition.y = Position->y;
        HeadPosition.z = Position->z;
    }

    // The closing 4:3 check, read through the vtable as the third-person loader does.
    ICamera* const Space = AsCamera(SpaceCameraOf(this));

    const double TanX = tan(static_cast<double>(Space->GetHalfFovXDeg()) *
                            static_cast<double>(kDegreesToRadians));
    const double TanY = tan(static_cast<double>(Space->GetFovYDeg()) *
                            static_cast<double>(kDegreesToRadians));
    const double Aspect = TanX / TanY;

    // Warns only for an ordered >= (a NaN ratio stays silent).
    if (fabs(Aspect - kExpectedAspect) >= kAspectTolerance)
    {
        // Client\space\Cockpit.cpp:0x113
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: cockpit aspect (%f) is not PC-compatible?\n",
              kCockpitCpp, 0x113, Aspect);
    }
}

char const* FFirstPersonCamera::GetName()
{
    return FirstPersonCameraGetName();
}

void FFirstPersonCamera::ScaleEnvironmentEffects()
{
}

void FFirstPersonCamera::Deactivate()
{
    DerivedDeactivate();
}
