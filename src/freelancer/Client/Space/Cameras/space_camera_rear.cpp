// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail rear-view camera (in-flight controller + 0x500, vtable .rdata 0x005dbe24): ctor
// 0x0051ad30, name 0x0051ad70, ini loader 0x0051ad80, update LAB_0051b0d0 (slot 0x6c). The
// other slots are shared with the other cockpit cameras (see the header).

#include "Space/Cameras/space_camera_rear.h"
#include "Space/space_scene_obj_draw.h"

#include "Space/Cameras/space_camera_first.h"  // G_FirstPersonMathEngine (DAT_00675220, shared Cockpit.cpp cache)

#include "common_cobject.h"
#include "common_csys.h"         // Acquire3DMathEngine (FUN_005b73e0)
#include "common_iface.h"        // IObjRW (thiscall getters)
#include "common_ini.h"
#include "dacom.h"
#include "x86math.h"

#include <math.h>
#include <string.h>

namespace
{
    const char kCockpitCpp[] = "E:\\FL\\Scratch\\Source\\Client\\space\\Cockpit.cpp";

    // .rdata 0x005dbf48: (double)(float)pi -- the yaw the rear view turns the ship's
    // orientation by.
    const double kRearViewYaw = 3.1415927410125732;
}

void RearViewCameraConstruct(FRearViewCamera* Cam)
{
    // FUN_0051ad30.
    HudCameraConstruct((FHudCamera*)Cam);

    Cam->ViewPosition.x = 0.0f;
    Cam->ViewPosition.y = 5.0f;
    Cam->ViewPosition.z = 20.0f;

    // Retail's face table 0x005dbe18 has the same one slot as HUDCamera's 0x005dc084
    // (FUN_0051d7f0); the port shares it.
}

const char* RearViewCameraGetName()
{
    // 0x0051ad70.
    return "RearView";
}

void RearViewCameraUpdate(FRearViewCamera* Cam, float DeltaSeconds)
{
    // LAB_0051b0d0 (slot 0x6c).
    FHudCamera* const Hud = (FHudCamera*)Cam;
    FSpaceCamera* const Space = SpaceCameraOf(Hud);

    // Only with a watched client ship (CObject mask 0x503); HUDCamera's update runs either way.
    if (GameCameraWatchedShip(Hud) != nullptr)
    {
        IObjRW* const Object = static_cast<IObjRW*>(GameCameraGetTarget((FGameCamera*)Hud));

        float Orientation[9];
        memcpy(Orientation, &Object->get_transform().Rotation.E[0][0], sizeof(Orientation));

        // cos/sin kept double; per row, x' and z' both from the row's original x and z.
        // Column 1 is untouched.
        const double Cos = cos(kRearViewYaw);
        const double Sin = sin(kRearViewYaw);
        for (int Row = 0; Row < 3; ++Row)
        {
            const double X = Orientation[Row * 3 + 0];
            Orientation[Row * 3 + 0] =
                static_cast<float>(X * Cos - Sin * static_cast<double>(Orientation[Row * 3 + 2]));
            Orientation[Row * 3 + 2] =
                static_cast<float>(X * Sin + Cos * static_cast<double>(Orientation[Row * 3 + 2]));
        }

        HudCameraApplyShake(Hud, Orientation);

        // The view position (+0x100) through the shaken orientation, using Cockpit.cpp's shared
        // math engine cache DAT_00675220.
        if (G_FirstPersonMathEngine == nullptr)
            G_FirstPersonMathEngine = Acquire3DMathEngine();
        Vector Offset;
        G_FirstPersonMathEngine->TransformMatrix(
            Offset, *reinterpret_cast<Matrix const*>(Orientation),
            Cam->ViewPosition);

        Vector const& ShipPos = Object->get_position();
        float Position[3];
        Position[0] = Offset.x + ShipPos.x;
        Position[1] = Offset.y + ShipPos.y;
        Position[2] = Offset.z + ShipPos.z;

        memcpy(&Space->World, Orientation, 9 * sizeof(float));
        Space->WorldPos.x = Position[0];
        Space->WorldPos.y = Position[1];
        Space->WorldPos.z = Position[2];
        Space->bViewDirty = 1;
    }

    HudCameraUpdate(Hud, DeltaSeconds);
}

bool RearViewCameraLoad(FRearViewCamera* Cam, const char* CockpitIniPath)
{
    // FUN_0051ad80, in retail's order. The warnings carry retail's Cockpit.cpp line numbers.
    bool bOk = true;

    INI_Reader Reader;
    if (!Reader.open(CockpitIniPath, false))
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: rearview camera file not found '%s'\n",
              kCockpitCpp, 0x6e3, CockpitIniPath);
        bOk = false;
    }

    while (Reader.read_header())
    {
        // [RearView] is recognised only to warn about it; no key is read from it.
        if (Reader.is_header("RearView"))
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: [RearView] is obsolete in file %s\n",
                  kCockpitCpp, 0x6ea, CockpitIniPath);
            continue;
        }

        if (!Reader.is_header("RearViewCamera"))
            continue;

        while (Reader.read_value())
        {
            if (!Reader.is_value("view_position"))
                continue;

            // Retail reads the components in the order 1, 0, 2 (FPU stack juggling); reading
            // them straight through is equivalent.
            const float X = Reader.get_value_float(0);
            const float Y = Reader.get_value_float(1);
            const float Z = Reader.get_value_float(2);

            Cam->ViewPosition.x = X;
            Cam->ViewPosition.y = Y;
            Cam->ViewPosition.z = Z;
        }
    }

    return bOk;
}

char const* FRearViewCamera::GetName()
{
    return RearViewCameraGetName();
}

void FRearViewCamera::Update(float DeltaSeconds)
{
    RearViewCameraUpdate(this, DeltaSeconds);
}

void FRearViewCamera::Deactivate()
{
    HudCameraDerivedDeactivate(this);
}

void FRearViewCamera::DrawScene(unsigned LayerFlags, unsigned Param2)
{
    SpaceCameraRenderSlot78(this, LayerFlags, Param2);
}
