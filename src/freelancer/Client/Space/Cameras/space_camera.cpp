// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail in-flight camera base class (FUN_0040f150, vtable .rdata 0x005c8944) and the camera
// mode dispatcher FUN_00549e70.
//
// What is here: the object layout, all twenty interface slots, the mode dispatch and the camera
// install (FUN_00549bc0). The eight derived cameras retail embeds in the in-flight controller at
// +0x00c / +0x1b0 / +0x3c8 / +0x500 / +0x60c / +0x760 / +0x9a4 / +0xa9c live in their own files
// (space_camera_first/third/turret/rear/death/script/static/docking) and are members of the
// controller SpaceApp (space_app.h), built by its ctor FUN_00549300.

#include "Space/Cameras/space_camera.h"
#include "Space/Cameras/space_camera_third.h"
#include "Space/Cameras/space_camera_first.h"
#include "Space/Cameras/space_camera_turret.h"
#include "Space/Cameras/space_camera_rear.h"
#include "Space/Cameras/space_camera_script.h"
#include "Space/Cameras/space_camera_static.h"
#include "Space/Cameras/space_camera_docking.h"
#include "Space/Cameras/space_camera_death.h"
#include "Space/space_app.h"       // G_SpaceApp (DAT_00678b98)
#include "Space/player_spawn.h"
#include "Space/client_gameobj.h" // CloseRearAndTurretViewsForDocking (FUN_00549e30)
#include "Network/server_connect.h"
#include "Common/Audio/menu_sound_arch.h"
#include "Common/input_manager.h"
#include "Core/globals.h"         // g_client.window (DAT_0067eca0), G_RenderSurfaceWidth/Height
#include "Space/player_behavior.h" // GetPlayerObjRW (the DAT_00679744 - 8 read, FUN_0054baf0)
#include "Space/star_sys.h"        // StarSysGetFogColor (FUN_005552e0)
#include "Core/widescreen.h"      // port-only Hor+ (FREELANCER_WIDESCREEN_SUPPORT)

#include "dacom.h"
#include "common_csys.h"     // Acquire3DMathEngine (FUN_005b73e0)
#include "common_iface.h"    // IObjRW (NotifyCinematicView)
#include "common_server.h"   // IServer::SPRequestInvincibility (DAT_0067ecd0 slot 57)
#include "x86math.h"         // I3DMathEngine::InverseTransform (slot 0x3c)

#include <math.h>
#include <stdlib.h>
#include <string.h>

// DAT_00614028 -- set to 1 by every FUN_00549e70 call, including the ones that select nothing.
// .data ships 1; nothing in the PE reads it.
unsigned char G_SpaceCameraSwitched = 1;

namespace
{
    // The base ctor's literals.
    const double kHalfFovXRadians = 0.6108652334660292; // DAT_005c8938
    const float  kInverseAspect = 0.75f;                // DAT_005c8930
    const double kRadiansToDegrees = 57.29577791868205; // DAT_005c8928
    const float  kHalfFovXDegrees = 35.0f;
    const float  kDefaultAspect = 1.3333334f;
    const float  kDefaultZNear = 3.0f;                  // DAT_00610530
    const float  kDefaultZFar = 250000.0f;              // DAT_00610534
    const float  kDefaultHalfWidth = 320.0f;
    const float  kDefaultHalfHeight = 240.0f;

    // FUN_0040f5f0's "fovx" branch: DAT_005c75e8 = 0.5 (the INI carries the FULL angle) and
    // DAT_005c77f4 = 0.01745329238474369f (degrees to radians).
    const double kFullFovToHalf = 0.5;
    const float  kDegreesToRadians = 0.01745329238474369f;

    // A frustum plane normalised as every projection rebuild does it (FUN_00410190, or its body
    // inlined), at full precision with the same association. No zero guard: one component is
    // always exactly 1.0f.
    void NormalizePlane(float* V)
    {
        const double Len = sqrt(static_cast<double>(V[0]) * V[0] + static_cast<double>(V[1]) * V[1] +
                                static_cast<double>(V[2]) * V[2]);
        const double Inv = 1.0 / Len;
        V[0] = static_cast<float>(Inv * V[0]);
        V[1] = static_cast<float>(Inv * V[1]);
        V[2] = static_cast<float>(Inv * V[2]);
    }

    // The X frustum plane and XScale, as all six rebuilds inline them. XScale is skipped unless
    // TanX > 0.0f (DAT_005c7474; less, equal and unordered skip). TanX is the unrounded tangent
    // in five of them and the float the "fovy" property stored in the sixth.
    void RebuildPlaneX(FSpaceCamera* Cam, double TanX)
    {
        Cam->PlaneX[0] = 1.0f;
        Cam->PlaneX[1] = 0.0f;
        Cam->PlaneX[2] = static_cast<float>(TanX);
        NormalizePlane(Cam->PlaneX);
        if (TanX > 0.0)
            Cam->XScale = static_cast<float>(static_cast<double>(Cam->HalfWidth) / TanX);
    }

    // The Y plane and YScale, the same guard. FUN_0040fa10 divides by the unrounded tangent;
    // every other rebuild by the float it stored.
    void RebuildPlaneY(FSpaceCamera* Cam, double TanY)
    {
        Cam->PlaneY[0] = 0.0f;
        Cam->PlaneY[1] = 1.0f;
        Cam->PlaneY[2] = static_cast<float>(TanY);
        NormalizePlane(Cam->PlaneY);
        if (TanY > 0.0)
            Cam->YScale = static_cast<float>(-(static_cast<double>(Cam->HalfHeight) / TanY));
    }

    // atan times DAT_005c8928 (180 / PI).
    float TangentToDegrees(double Tangent)
    {
        return static_cast<float>(atan2(Tangent, 1.0) * kRadiansToDegrees);
    }

    // DAT_006164d8 -- the camera code's lazily acquired 3DMathEngine (FUN_005b73e0 on first use).
    I3DMathEngine* G_CameraMathEngine = nullptr;

    // FUN_0040f5f0's body. No null checks: the key goes straight to stricmp.
    int SetPropertyImpl(FSpaceCamera* Cam, const char* Key, const char* Value)
    {
        if (_stricmp(Key, "fovx") == 0)
        {
            // The INI carries the FULL angle. The tangent is taken of the unrounded half-angle;
            // the X plane and XScale use the unrounded tangent, the Y plane and YScale the
            // stored float.
            const double HalfFovX = atof(Value) * kFullFovToHalf;
            Cam->HalfFovXDeg = static_cast<float>(HalfFovX);
            double TanX = tan(HalfFovX * static_cast<double>(kDegreesToRadians));
#ifdef FREELANCER_WIDESCREEN_SUPPORT
            // Port-only Hor+: +0x98 keeps the 4:3 angle, the frustum takes the widened one.
            TanX *= SpaceCameraHorPlusScale(Cam);
#endif
            const double TanY = TanX / static_cast<double>(Cam->Aspect);
            const float TanYStored = static_cast<float>(TanY);
            Cam->FovYDeg = TangentToDegrees(TanY);
            RebuildPlaneX(Cam, TanX);
            RebuildPlaneY(Cam, TanYStored);
            // Retail does NOT raise +0x34 here: changing the projection does not invalidate
            // the cached VIEW matrix.
            return 1;
        }

        if (_stricmp(Key, "fovy") == 0)
        {
            // The mirror image of "fovx". This branch alone compares and divides with the
            // STORED floats for both XScale and YScale. The planes go through FUN_00410190.
            const double FovY = atof(Value) * kFullFovToHalf;
            Cam->FovYDeg = static_cast<float>(FovY);
            const double TanY = tan(FovY * static_cast<double>(kDegreesToRadians));
            const float TanYStored = static_cast<float>(TanY);
            const double TanX = TanY * static_cast<double>(Cam->Aspect);
            const float TanXStored = static_cast<float>(TanX);
#ifdef FREELANCER_WIDESCREEN_SUPPORT
            // Port-only Hor+: +0x98 is the 4:3 angle the widened tangent came from.
            Cam->HalfFovXDeg = TangentToDegrees(TanX / SpaceCameraHorPlusScale(Cam));
#else
            Cam->HalfFovXDeg = TangentToDegrees(TanX);
#endif
            RebuildPlaneX(Cam, TanXStored);
            RebuildPlaneY(Cam, TanYStored);
            return 1;
        }

        // No rebuild: neither near nor far takes part in the plane maths.
        if (_stricmp(Key, "znear") == 0)
        {
            Cam->ZNear = static_cast<float>(atof(Value));
            return 1;
        }
        if (_stricmp(Key, "zfar") == 0)
        {
            Cam->ZFar = static_cast<float>(atof(Value));
            return 1;
        }

        return 0;
    }

    // ---------------------------------------------------------------- mode dispatch state
    // Retail carries all of it on the in-flight controller SpaceApp (DAT_00678b98, the `this`
    // of FUN_00549e70 / FUN_00549bc0): the eight cameras at +0x00c .. +0xa9c, the active camera
    // at +0xba0, the preference at +0xba4, the bytes at +0x08 / +0x0b / +0xba8. The port's
    // object is G_SpaceApp (space_app.h), built at static init as retail's is.
    bool         G_LastModeWasScript = false;  // FUN_00549e70's local_28

    // The camera for one of FUN_00549e70's names.
    FSpaceCamera* CameraForSlot(int Slot)
    {
        switch (Slot)
        {
        case kSpaceCameraFirstPerson: return SpaceCameraOf((FHudCamera*)&G_SpaceApp.FirstPerson);
        case kSpaceCameraThirdPerson: return SpaceCameraOf((FHudCamera*)&G_SpaceApp.ThirdPerson);
        case kSpaceCameraTurret:      return SpaceCameraOf((FHudCamera*)&G_SpaceApp.Turret);
        case kSpaceCameraRearview:    return SpaceCameraOf((FHudCamera*)&G_SpaceApp.RearView);
        case kSpaceCameraScript:      return (FSpaceCamera*)&G_SpaceApp.Script;
        case kSpaceCameraStatic:      return (FSpaceCamera*)&G_SpaceApp.Static;
        case kSpaceCameraDocking:     return (FSpaceCamera*)&G_SpaceApp.Docking;
        case kSpaceCameraDeath:       return (FSpaceCamera*)&G_SpaceApp.Death;
        default:                      return nullptr;
        }
    }
}

#ifdef FREELANCER_WIDESCREEN_SUPPORT
double SpaceCameraHorPlusScale(const FSpaceCamera* Cam)
{
    // Inclusive edges, as FUN_0040fd60 counts them.
    return WidescreenHorPlusScale(static_cast<long long>(Cam->Viewport[2]) - Cam->Viewport[0] + 1);
}
#endif

void FSpaceCamera::SetHalfFovX(float HalfFovXDeg)
{
    // FUN_0040f8c0. A NON-virtual method (RET 4) that owns the whole projection derivation;
    // the ctor and the "fovx" property branch are inlined copies of it, and the GameCamera ctor
    // calls it directly with 35.0f. X plane / XScale with the unrounded tangent, Y plane /
    // YScale with the stored float.
    this->HalfFovXDeg = HalfFovXDeg;

    double TanX = tan(static_cast<double>(HalfFovXDeg) * static_cast<double>(kDegreesToRadians));
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only Hor+: +0x98 keeps the 4:3 angle, the frustum takes the widened one.
    TanX *= SpaceCameraHorPlusScale(this);
#endif
    const double TanY = TanX / static_cast<double>(Aspect);
    const float TanYStored = static_cast<float>(TanY);
    FovYDeg = TangentToDegrees(TanY);

    RebuildPlaneX(this, TanX);
    RebuildPlaneY(this, TanYStored);
}

void FSpaceCamera::SetFovY(float FovYDeg)
{
    // FUN_0040fa10. Both plane rebuilds use the UNROUNDED tangents.
    this->FovYDeg = FovYDeg;
    const double TanY = tan(static_cast<double>(FovYDeg) * static_cast<double>(kDegreesToRadians));
    const double TanX = TanY * static_cast<double>(Aspect);
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only Hor+: +0x98 is the 4:3 angle the widened tangent came from.
    HalfFovXDeg = TangentToDegrees(TanX / SpaceCameraHorPlusScale(this));
#else
    HalfFovXDeg = TangentToDegrees(TanX);
#endif
    RebuildPlaneX(this, TanX);
    RebuildPlaneY(this, TanY);
}

bool FSpaceCamera::SetViewport(const int* ViewRect4)
{
    // FUN_0040fd60 (thiscall, RET 4).
    Viewport[0] = ViewRect4[0];
    Viewport[1] = ViewRect4[1];
    Viewport[2] = ViewRect4[2];
    Viewport[3] = ViewRect4[3];

    // Both extents are the difference PLUS ONE (inclusive edges).
    const int Width = Viewport[2] - Viewport[0] + 1;
    const int Height = Viewport[3] - Viewport[1] + 1;

    // The centre x adds the float half-width, the centre y the unrounded half-height (0.5 is
    // the double DAT_005c75e8).
    const float AspectStored = static_cast<float>(static_cast<double>(Width) / static_cast<double>(Height));
    Aspect = AspectStored;
    const float HalfWidthStored = static_cast<float>(static_cast<double>(Width) * 0.5);
    HalfWidth = HalfWidthStored;
    const double HalfHeight = static_cast<double>(Height) * 0.5;
    this->HalfHeight = static_cast<float>(HalfHeight);
    ScreenCenterX = static_cast<float>(static_cast<double>(Viewport[0]) + HalfWidthStored);
    ScreenCenterY = static_cast<float>(static_cast<double>(Viewport[1]) + HalfHeight);

    // Divided by the stored float aspect. X plane / XScale with the unrounded tangent, Y plane /
    // YScale with the stored float.
    double TanX = tan(static_cast<double>(HalfFovXDeg) * static_cast<double>(kDegreesToRadians));
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only Hor+: the 4:3 angle at +0x98 widened for the viewport just stored.
    TanX *= SpaceCameraHorPlusScale(this);
#endif
    const double TanY = TanX / static_cast<double>(AspectStored);
    const float TanYStored = static_cast<float>(TanY);
    FovYDeg = TangentToDegrees(TanY);

    RebuildPlaneX(this, TanX);
    RebuildPlaneY(this, TanYStored);
    return true;
}

void FSpaceCamera::SetViewportEx(const int* ViewRect4, int UnusedFlag)
{
    // FUN_0040fb80. See the header: the flag really is dead in the shipped build.
    (void)UnusedFlag;
    SetViewport(ViewRect4);
}

void FSpaceCamera::Construct()
{
    // FUN_0040f150. No memset: retail writes exactly the fields below and leaves ScreenCenterX/Y
    // (+0x68/+0x6c), the viewport (+0xac..+0xbb) and the pad bytes (+0x35..+0x37,
    // +0xbd..+0xbf) as the storage held them -- SetViewport (FUN_0040fd60) fills those in.
    for (int Index = 0; Index < 9; ++Index)
    {
        (&World.E[0][0])[Index] = 0.0f;
        (&View.E[0][0])[Index] = 0.0f;
    }
    World.E[0][0] = 1.0f;
    World.E[1][1] = 1.0f;
    World.E[2][2] = 1.0f;
    View.E[0][0] = 1.0f;
    View.E[1][1] = 1.0f;
    View.E[2][2] = 1.0f;
    for (int Index = 0; Index < 3; ++Index)
    {
        (&WorldPos.x)[Index] = 0.0f;
        (&ViewPos.x)[Index] = 0.0f;
    }

    bViewDirty = 1;
    FlagBC = 0;

    Aspect = kDefaultAspect;
    HalfWidth = kDefaultHalfWidth;
    ZNear = kDefaultZNear;           // DAT_00610530
    HalfHeight = kDefaultHalfHeight;
    ZFar = kDefaultZFar;             // DAT_00610534
    HalfFovXDeg = kHalfFovXDegrees;

    // X plane and XScale against the unrounded tangent; the Y plane and YScale against the
    // skew rounded to a float.
    const double TanX = tan(kHalfFovXRadians);
    const double Skew = static_cast<double>(kInverseAspect) * TanX;
    const float SkewStored = static_cast<float>(Skew);
    FovYDeg = TangentToDegrees(Skew);
    RebuildPlaneX(this, TanX);
    RebuildPlaneY(this, SkewStored);
}

const float kSpaceCameraCutCosine = 0.95f;      // .data 0x0061402c

bool SpaceCameraIsHardCut(const float* OldOrientation, const float* NewOrientation,
                          bool bHadNoPreviousCamera)
{
    // 0x00549da8: the dot of the two column-2 axes, with retail's association.
    const double Dot =
        (static_cast<double>(NewOrientation[8]) * OldOrientation[8] +
         static_cast<double>(NewOrientation[5]) * OldOrientation[5]) +
        static_cast<double>(NewOrientation[2]) * OldOrientation[2];

    // A strict, ORDERED "less than": equal does not cut, and neither does an unordered compare
    // (a NaN in either matrix).
    if (Dot < static_cast<double>(kSpaceCameraCutCosine))
        return true;

    // No camera at all before this one always counts as a cut, whatever the dot said.
    return bHadNoPreviousCamera;
}

void InstallSpaceCamera(FSpaceCamera* NewCam, bool bForce)
{
    // FUN_00549bc0 (thiscall on the in-flight controller G_SpaceApp, RET 8).
    if (G_SpaceApp.ActiveCamera == NewCam && !bForce)
        return;

    // The outgoing camera's transform, read through slot 0x10 BEFORE anything is torn down;
    // without one an identity (FUN_004215b0). Only the 3x3 part is read again, by the cut test.
    float Saved[12];
    if (G_SpaceApp.ActiveCamera != nullptr)
    {
        const float* const Src = (const float*)AsCamera(G_SpaceApp.ActiveCamera)->GetOrientation();
        for (int I = 0; I < 12; ++I)
            Saved[I] = Src[I];
    }
    else
    {
        for (int I = 0; I < 12; ++I)
            Saved[I] = 0.0f;
        Saved[0] = 1.0f;
        Saved[4] = 1.0f;
        Saved[8] = 1.0f;
    }

    // Slot 0x68 deactivates; FUN_0040f140(0) drops the sound listener.
    bool bWasNone = false;
    if (G_SpaceApp.ActiveCamera != nullptr)
    {
        ((FGameCamera*)G_SpaceApp.ActiveCamera)->Deactivate();
        G_SpaceApp.ActiveCamera = nullptr;
        SetClientSoundListenerObject(nullptr);
    }
    else
    {
        bWasNone = true;
    }

    if (NewCam != nullptr)
    {
        // The game window's client rect (DAT_0067eca0) as an inclusive view rect.
        RECT Client;
        GetClientRect(g_client.window, &Client);
        int View[4];
        View[0] = 0;
        View[1] = 0;
        View[2] = Client.right - 1;
        View[3] = Client.bottom - 1;

        // Slot 0x58; the result is not read.
        ((FGameCamera*)NewCam)->Attach(GetPlayerObjRW());

        // Slot 0x64 with that rect when both extents are positive (signed), else with the
        // surface's (DAT_00610850 / DAT_00610854). The result is not read.
        if (View[2] <= 0 || View[3] <= 0)
        {
            View[2] = static_cast<int>(G_RenderSurfaceWidth) - 1;
            View[3] = static_cast<int>(G_RenderSurfaceHeight) - 1;
        }
        ((FGameCamera*)NewCam)->Activate(View);

        // The fog colour at the camera's position (FUN_005552e0) becomes the clear colour
        // (colour and depth clear), then FUN_0051b500 clears now. Every camera the controller
        // installs is a GameCamera.
        unsigned char Fog[4];
        StarSysGetFogColor(AsCamera(NewCam)->GetPosition(), Fog);
        FGameCamera* const Game = reinterpret_cast<FGameCamera*>(NewCam);
        Game->ClearColor[0] = Fog[0];
        Game->ClearColor[1] = Fog[1];
        Game->ClearColor[2] = Fog[2];
        Game->Flags = static_cast<unsigned char>(Game->Flags | kGameCameraClearColour | kGameCameraClearDepth);
        Game->Clear();
    }

    G_SpaceApp.ActiveCamera = NewCam;
    SetClientSoundListenerObject(NewCam); // FUN_0040f140

    float Current[12];
    if (NewCam != nullptr)
    {
        const float* const Src = (const float*)AsCamera(NewCam)->GetOrientation();
        for (int I = 0; I < 12; ++I)
            Current[I] = Src[I];
    }
    else
    {
        for (int I = 0; I < 12; ++I)
            Current[I] = 0.0f;
        Current[0] = 1.0f;
        Current[4] = 1.0f;
        Current[8] = 1.0f;
    }

    // A cut raises +0xbc on the active camera, re-read from the controller -- null after a
    // switch to nothing, so no flag then.
    if (SpaceCameraIsHardCut(Saved, Current, bWasNone) && G_SpaceApp.ActiveCamera != nullptr)
        G_SpaceApp.ActiveCamera->FlagBC = 1;
}

bool SpaceCameraLoadCockpit(const char* CockpitIniPath)
{
    // 0x0054ad25: the in-flight init hands the SAME path to all four cockpit cameras, in this
    // order. Deviation: retail ignores every return value; the port folds them so a caller can
    // tell whether anything complained.
    bool bOk = true;
    G_SpaceApp.FirstPerson.Load(CockpitIniPath);
    bOk = G_SpaceApp.ThirdPerson.Load(CockpitIniPath) && bOk;
    bOk = G_SpaceApp.Turret.Load(CockpitIniPath) && bOk;
    bOk = G_SpaceApp.RearView.Load(CockpitIniPath) && bOk;
    return bOk;
}

FSpaceCamera* SwitchSpaceCameraMode(const char* ModeName, void* AttachTarget)
{
    // FUN_00549e70. Retail copies the name into a 0x20-byte stack buffer via FUN_00417c50
    // first, so a name longer than 31 characters is silently truncated before the comparisons;
    // that truncation is reproduced because it decides which mode a long string lands in.
    char Name[0x20];
    Name[0] = 0;
    if (ModeName != nullptr)
    {
        unsigned i = 0;
        while (i + 1 < sizeof(Name) && ModeName[i] != 0)
        {
            Name[i] = ModeName[i];
            ++i;
        }
        Name[i] = 0;
    }

    // DAT_00614028 = 1 unconditionally, before any name test.
    G_SpaceCameraSwitched = 1;

    G_LastModeWasScript = false;

    int Selected = -1;
    bool bCrosshair = false;

    if (strcmp(Name, "player") == 0)
    {
        // The rear and turret views of DAT_00674aa0 are closed (FUN_00549e30's body, inlined
        // here and in the next two branches).
        CloseRearAndTurretViewsForDocking();
        if (G_SpaceApp.PlayerPreference == 0)
            Selected = kSpaceCameraFirstPerson;
        else if (G_SpaceApp.PlayerPreference == 1)
            Selected = kSpaceCameraThirdPerson;
        // Any other stored value selects nothing at all, but still reaches the crosshair branch.
        bCrosshair = true;
    }
    else if (strcmp(Name, "first_person") == 0)
    {
        CloseRearAndTurretViewsForDocking();
        G_SpaceApp.PlayerPreference = 0;
        Selected = kSpaceCameraFirstPerson;
        bCrosshair = true;
    }
    else if (strcmp(Name, "third_person") == 0)
    {
        CloseRearAndTurretViewsForDocking();
        G_SpaceApp.PlayerPreference = 1;
        Selected = kSpaceCameraThirdPerson;
        bCrosshair = true;
    }
    else if (strcmp(Name, "turret") == 0)
    {
        Selected = kSpaceCameraTurret;
        bCrosshair = true;
    }
    else if (strcmp(Name, "rearview") == 0)
    {
        Selected = kSpaceCameraRearview;
        bCrosshair = true;
    }
    else if (strcmp(Name, "death") == 0)
    {
        Selected = kSpaceCameraDeath;
    }
    else if (strcmp(Name, "script") == 0)
    {
        G_LastModeWasScript = true;
        Selected = kSpaceCameraScript;
    }
    else if (strcmp(Name, "static") == 0)
    {
        Selected = kSpaceCameraStatic;
    }
    else if (strcmp(Name, "docking") == 0)
    {
        Selected = kSpaceCameraDocking;
    }
    else
    {
        // Unknown name: retail returns immediately without even touching the cursor.
        return nullptr;
    }

    if (bCrosshair)
    {
        // The five piloting modes select the "Cross" cursor (FUN_0041dde0) and turn the
        // software-cursor draw latch on (FUN_0041dda0). Reached even when "player" selected
        // nothing.
        MouseSetCursor("Cross", true);
        SetClientMouseCursorDrawFlag(1);
    }
    else
    {
        // The four cinematic modes only turn the latch off.
        SetClientMouseCursorDrawFlag(0);
    }

    // "player" with a stored preference other than 0/1 installs nothing and returns null.
    if (Selected < 0)
        return nullptr;

    FSpaceCamera* Cam = CameraForSlot(Selected);

    // The "script" flag is the force argument.
    InstallSpaceCamera(Cam, G_LastModeWasScript);

    if (AttachTarget != nullptr)
    {
        // The SELECTED camera's slot 0x58 (attach). Every camera CameraForSlot hands out is a
        // GameCamera. Retail discards the result.
        ((FGameCamera*)Cam)->Attach(AttachTarget);
    }

    return Cam;
}

void ReleaseSpaceCameraControllerParts()
{
    // 0x0054aef4: FUN_00518410 on controller + 0x00c.
    G_SpaceApp.FirstPerson.ReleaseMesh();
    // Retail's calls on + 0x1b0, + 0x3c8 and + 0x500 are the ICF-folded bare RET 0x00401e90.
}

FSpaceCamera* GetActiveSpaceCamera()
{
    return G_SpaceApp.ActiveCamera;
}

void RaiseActiveSpaceCameraCut()
{
    // FUN_0054bd50.
    if (G_SpaceApp.ActiveCamera != nullptr)
        G_SpaceApp.ActiveCamera->FlagBC = 1;
}

bool IsSpaceCameraControllerReady()
{
    // DAT_00678ba0 = SpaceApp + 0x08 (read at 0x0054bcb0); written only by the space init
    // (0x0054ab8e) and teardown (0x0054af8d), both in space_state.cpp.
    return G_SpaceApp.bCameraReady != 0;
}

bool IsSpaceInitInProgress()
{
    // FUN_0054bd40: DAT_00678ba3 (SpaceApp + 0x0b), written only by the space init.
    return G_SpaceApp.bInitInProgress != 0;
}

FSpaceCamera* GetFirstPersonSpaceCamera()
{
    // FUN_0054bc30: 0x00678ba4.
    return CameraForSlot(kSpaceCameraFirstPerson);
}

FSpaceCamera* GetThirdPersonSpaceCamera()
{
    // FUN_0054bc40: 0x00678d48.
    return CameraForSlot(kSpaceCameraThirdPerson);
}

FSpaceCamera* GetTurretSpaceCamera()
{
    // FUN_0054bc50: 0x00678f60.
    return CameraForSlot(kSpaceCameraTurret);
}

bool IsFirstPersonCameraCurrent()
{
    // FUN_0054bc80: whether the sound listener (FUN_0040f130, DAT_006164dc) is the
    // first-person camera (0x00678ba4). No null test, as in retail.
    return GetClientSoundListenerObject() ==
           static_cast<void*>(CameraForSlot(kSpaceCameraFirstPerson));
}

bool IsDockingCameraCurrent()
{
    // FUN_0054bca0: the same against the docking camera (0x00679634). No null test, as in
    // retail.
    return GetClientSoundListenerObject() == static_cast<void*>((FSpaceCamera*)&G_SpaceApp.Docking);
}

void NotifyCinematicView(bool bActive)
{
    // Port-only helper for 0x0051e615 / 0x0051e6f1 (script camera) and 0x0051e93f / 0x0051e985
    // (static camera).
    IObjRW* const Ship = static_cast<IObjRW*>(GetPlayerObjRW()); // FUN_0054baf0
    if (Ship == nullptr)
        return;

    // IServer slot 57; DAT_0067ecd0 is used with no null test.
    const unsigned Connection = GetPlayerConnectionId();
    const unsigned ShipId = Ship->get_id();
    static_cast<IServer*>(GetLocalServerInterface())
        ->SPRequestInvincibility(ShipId, bActive, INVINCIBILITY_CAMERA, Connection);
}

void SetDockingCameraTransform(const float* Csys12)
{
    // FUN_0054bb90 (cdecl). The docking camera's Csys and view-dirty byte are written BEFORE
    // the guard, so they land even when no controller exists.
    FSpaceCamera* const Docking = (FSpaceCamera*)&G_SpaceApp.Docking;
    memcpy(&Docking->World, Csys12, 9 * sizeof(float));
    memcpy(&Docking->WorldPos, Csys12 + 9, 3 * sizeof(float));
    Docking->bViewDirty = 1;

    // DAT_00678ba0, the "controller exists" byte.
    if (G_SpaceApp.bCameraReady)
        SwitchSpaceCameraMode("docking", nullptr);
}

void BeginDockingCameraSequence(float Seconds)
{
    // FUN_0054bbd0 (cdecl). The active camera must be the docking camera, or retail complains
    // -- and arms it anyway.
    if (G_SpaceApp.ActiveCamera != (FSpaceCamera*)&G_SpaceApp.Docking)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Client\\space\\space.cpp", 0x8ad,
              "static docking camera must be the active camera before setting the fade out "
              "timer");
    }
    G_SpaceApp.Docking.BeginSequence(Seconds);
}

void SetupSpaceScriptCamera(void* Object, const float* Transform12, const char* ScriptName)
{
    // FUN_0054bb10 (cdecl). FUN_0051e3b0 with the arguments reordered, before the guard like
    // FUN_0054bb90.
    G_SpaceApp.Script.Setup(ScriptName, Object, Transform12);

    if (G_SpaceApp.bCameraReady)
        SwitchSpaceCameraMode("script", nullptr);

    // Raised on both paths. DAT_00679740 (SpaceApp + 0xba8) is dropped by the frame at
    // 0x0054b1fa and the teardown at 0x0054af2e.
    G_SpaceApp.bScriptCameraLatch = 1;
}

bool GetSpaceScriptCameraLatch()
{
    // FUN_0054bc90.
    return G_SpaceApp.bScriptCameraLatch != 0;
}

void RequestSpaceCameraMode(const char* ModeName)
{
    // FUN_0054bc10.
    if (!G_SpaceApp.bCameraReady)
        return;
    SwitchSpaceCameraMode(ModeName, nullptr);
}

int GetSpaceCameraPlayerPreference()
{
    return G_SpaceApp.PlayerPreference;
}

void SetSpaceCameraPlayerPreference(int Preference)
{
    // Retail writes +0xba4 inline (0x0054ae06, with 0).
    G_SpaceApp.PlayerPreference = Preference;
}

bool WasLastSpaceCameraModeScript()
{
    // FUN_00549e70's local_28, which it hands to FUN_00549bc0 as the second argument.
    return G_LastModeWasScript;
}

GENRESULT DACOM_API FSpaceCamera::QueryInterface(char const* Iid, void** Out)
{
    // LAB_0040f350 (no null test).
    *Out = nullptr;
    return -1;
}

unsigned long DACOM_API FSpaceCamera::AddRef()
{
    // LAB_0040f360, both slot 0x04 and slot 0x08.
    return 1;
}

unsigned long DACOM_API FSpaceCamera::Release()
{
    return 1;
}

const Vector* DACOM_API FSpaceCamera::GetPosition()
{
    return &this->WorldPos;                    // LAB_0040f370: this + 0x28
}

const Matrix* DACOM_API FSpaceCamera::GetOrientation()
{
    return &this->World;                 // LAB_0040f380: this + 0x04
}

int const* DACOM_API FSpaceCamera::GetViewport()
{
    return this->Viewport;                       // LAB_0040f470: this + 0xac
}

float DACOM_API FSpaceCamera::GetHalfFovXDeg()
{ return this->HalfFovXDeg; }

float DACOM_API FSpaceCamera::GetFovYDeg()
{ return this->FovYDeg; }

float DACOM_API FSpaceCamera::GetZNear()
{ return this->ZNear; }

float DACOM_API FSpaceCamera::GetZFar()
{ return this->ZFar; }

float DACOM_API FSpaceCamera::GetAspect()
{ return this->Aspect; }

double DACOM_API FSpaceCamera::GetXScaleOverNear()
{
    return static_cast<double>(this->XScale) / this->ZNear; // LAB_0040f4d0
}

double DACOM_API FSpaceCamera::GetYScaleOverNear()
{
    return static_cast<double>(this->YScale) / this->ZNear; // LAB_0040f4e0
}

int DACOM_API FSpaceCamera::Project(float* OutX, float* OutY, float* OutZ, const Vector* World)
{
    // FUN_0040ff50 (explicit this, RET 0x14).
    if (G_CameraMathEngine == nullptr)
        G_CameraMathEngine = Acquire3DMathEngine();
    Vector Local;
    G_CameraMathEngine->InverseTransform(Local, this->WorldXform,
                                         *World);

    // Only -ZNear >= z (ordered) projects; a point exactly on the near plane does.
    if (!(-this->ZNear >= Local.z))
        return 0;

    // InvZ (-1.0f is .rdata 0x005c7500) stays unrounded; each coordinate is rounded once.
    const double InvZ = -1.0 / static_cast<double>(Local.z);
    *OutX = static_cast<float>(static_cast<double>(Local.x) * this->XScale * InvZ +
                               this->ScreenCenterX);
    *OutY = static_cast<float>(static_cast<double>(Local.y) * this->YScale * InvZ +
                               this->ScreenCenterY);
    *OutZ = -Local.z;
    return 1;
}

void DACOM_API FSpaceCamera::Unproject(Vector* OutDir, float ScreenX, float ScreenY)
{
    // LAB_0040f520. Screen point -> a direction in world space (math engine slot 0x2c).
    Vector Local;
    Local.x = (ScreenX - this->ScreenCenterX) * (this->ZNear / this->XScale);
    Local.y = (ScreenY - this->ScreenCenterY) * (this->ZNear / this->YScale);
    Local.z = -this->ZNear;
    Matrix Rotation;
    memcpy(&Rotation, &this->World, sizeof(Rotation));
    if (G_CameraMathEngine == nullptr)
        G_CameraMathEngine = Acquire3DMathEngine();
    Vector Dir;
    G_CameraMathEngine->TransformMatrix(Dir, Rotation, Local);
    OutDir->x = Dir.x;
    OutDir->y = Dir.y;
    OutDir->z = Dir.z;
}

int DACOM_API FSpaceCamera::ClassifySphere(const Vector* Center, float Radius)
{
    // FUN_0040ffe0. Classifies a sphere against the frustum and returns a CODE, not a
    // boolean: 1 = outside, 3 = straddling a plane, 4 = fully inside. The result starts at
    // 4 and is knocked down to 3 by any plane the sphere crosses; any plane it is fully
    // outside of returns 1 immediately.
    //
    // The camera looks down -Z, so the distance along the view axis is -z, and the sphere
    // spans [-z - Radius, -z + Radius].
    const float NearEdge = -Center->z - Radius;
    const float FarEdge = Radius - Center->z;

    int Result = kSphereInside;

    if (FarEdge < this->ZNear)
        return kSphereOutside;
    if (NearEdge < this->ZNear)
        Result = kSphereStraddling;

    // The left/right pair, folded into one test by taking |x|. The distance is never rounded
    // to float.
    const double DistX = static_cast<double>(fabsf(Center->x)) * this->PlaneX[0] +
                         static_cast<double>(this->PlaneX[2]) * Center->z;
    if (DistX > Radius)
        return kSphereOutside;
    if (DistX > -Radius)
        Result = kSphereStraddling;

    // The top/bottom pair, same shape.
    const double DistY = static_cast<double>(fabsf(Center->y)) * this->PlaneY[1] +
                         static_cast<double>(this->PlaneY[2]) * Center->z;
    if (DistY > Radius)
        return kSphereOutside;
    if (DistY > -Radius)
        Result = kSphereStraddling;

    if (NearEdge > this->ZFar)
        return kSphereOutside;
    if (FarEdge > this->ZFar)
        Result = kSphereStraddling;

    return Result;
}

const Transform* DACOM_API FSpaceCamera::GetViewTransform()
{
    // LAB_0040f390. Lazily rebuilds the cached view transform {transpose(World), -R^T * p}.
    if (this->bViewDirty != 0)
    {
        if (G_CameraMathEngine == nullptr)
            G_CameraMathEngine = Acquire3DMathEngine();
        Vector Out;
        G_CameraMathEngine->TransposeTransform(Out, this->World, this->WorldPos);

        const float* R = &this->World.E[0][0];
        float* V = &this->View.E[0][0];
        V[0] = R[0]; V[1] = R[3]; V[2] = R[6];
        V[3] = R[1]; V[4] = R[4]; V[5] = R[7];
        V[6] = R[2]; V[7] = R[5]; V[8] = R[8];
        this->ViewPos.x = -Out.x;
        this->ViewPos.y = -Out.y;
        this->ViewPos.z = -Out.z;

        this->bViewDirty = 0;
    }
    return (const Transform*)(&this->View.E[0][0]);
}

int DACOM_API FSpaceCamera::ProjectVec(Vector* OutXYZ, const Vector* World)
{
    // LAB_0040f4f0: slot 0x34 through the vtable.
    return Project(&OutXYZ->x, &OutXYZ->y, &OutXYZ->z, World);
}

void FSpaceCamera::GetVelocity(Vector* Out)
{
    // LAB_0040f5d0.
    Out->x = 0.0f;
    Out->y = 0.0f;
    Out->z = 0.0f;
}

// FUN_0040f5f0 -- the base SetProperty ("fovx" / "fovy" / "znear" / "zfar"; nonzero when the key
// was one of them), called DIRECTLY, not through slot 0x4c: the GameCamera override
// (0x0051b3fe) calls the base body this way. Through the vtable it is ICamera::SetProperty.
int FSpaceCamera::SetProperty(char const* Key, char const* Value)
{
    return SetPropertyImpl(this, Key, Value);
}
