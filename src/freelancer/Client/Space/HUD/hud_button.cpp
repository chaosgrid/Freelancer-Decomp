// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Space/HUD/hud_button.h"

#include "Space/cockpit_load.h"
#include "common_csys.h"
#include "common_ini.h"
#include "common_misc.h"
#include "common_text.h"
#include "Core/game_options.h"
#include "Core/globals.h"
#include "Space/HUD/hud_command_dispatch.h"
#include "Common/input_manager.h"
#include "Space/Cameras/space_camera.h"
#include "Interface/win3d_engine_submit.h"
#include "Interface/win3d_immediate.h"

#include "DALib.h"
#include "engine/engbase.h"

#include <string.h>

namespace
{
    // FUN_00421050 + FUN_00421070 on the holder: IEngine::SetInstanceMatrix(+0x04, Transform)
    // then UpdateInstance(+0x04, 0, 0).
    void HolderSetMatrixAndUpdate(FClientMeshHolder* Holder, float* Transform12)
    {
        DALib::Engine->SetInstanceMatrix(Holder->EngineInstance, reinterpret_cast<const Transform*>(Transform12));
        Holder->Update();
    }
}

// ============================================================================================
// WinButton
// ============================================================================================

WinButton::WinButton()
{
    // Inlined in FUN_004c8d40; the rest pose starts at identity (FUN_004b5150).
    memset(&RestPose, 0, sizeof(RestPose));
    RestPose.Rotation.E[2][2] = 1.0f;
    RestPose.Rotation.E[1][1] = 1.0f;
    RestPose.Rotation.E[0][0] = 1.0f;
    bHover = 0;
    SpinAngle = 0.0f;
    SpinSpeed = 180.0f;
    FieldE8 = 0.85f;
    FieldEC = 0.75f;
    ByteF0 = 0;
}

bool WinButtonProjectedRect(FClientMeshHolder* Holder, float* OutRect4)
{
    // FUN_0059f200.
    if (!Holder->HasInstance())
        return false;

    float Radius = 0.0f;
    Vector Center;
    DALib::Engine->GetInstanceExtent(Holder->EngineInstance, 0, &Radius, &Center);
    const Vector* const Pos = DALib::Engine->GetInstancePosition(Holder->EngineInstance);
    Vector World;
    World.x = Center.x + Pos->x;
    World.y = Center.y + Pos->y;
    World.z = Center.z + Pos->z;

    ViewRect Rect;
    float Depth = 0.0f;
    if (get_projected_bounding_box(AsCamera(Win3DGetCamera()), World, Radius, &Rect, Depth) == 0)
        return false;

    OutRect4[0] = static_cast<float>(Rect.Left);
    OutRect4[1] = static_cast<float>(Rect.Top);
    OutRect4[2] = static_cast<float>(Rect.Right);
    OutRect4[3] = static_cast<float>(Rect.Bottom);
    return true;
}

bool WinButton::HitTest(Vector const* Ray)
{
    // FUN_0059f2e0.
    (void)Ray;
    float Rect[4];
    if (!WinButtonProjectedRect(&MeshHolder, Rect))
        return false;

    int CursorX = 0;
    int CursorY = 0;
    int CursorZ = 0;
    GetClientSoftwareCursor(&CursorX, &CursorY, &CursorZ);
    float const Y = static_cast<float>(CursorY);
    float const X = static_cast<float>(CursorX);
    // Edges included; NaN rejects.
    if (!(X >= Rect[0]))
        return false;
    if (!(X <= Rect[2]))
        return false;
    if (!(Y >= Rect[1]))
        return false;
    if (!(Y <= Rect[3]))
        return false;
    return true;
}

void WinButton::SetPosition(Vector const* NewPosition)
{
    // LAB_004c9320.
    Position.x = NewPosition->x;
    Position.y = NewPosition->y;
    Position.z = NewPosition->z;
    RestPose.Translation.x = NewPosition->x;
    RestPose.Translation.y = NewPosition->y;
    RestPose.Translation.z = NewPosition->z;
    memcpy(&Orientation, &RestPose, 12 * sizeof(float));
    HolderSetMatrixAndUpdate(&MeshHolder, &Orientation.E[0][0]);
}

void WinButton::SetOrientation(Matrix const* NewOrientation)
{
    // LAB_004c92e0: the orientation, then the live orientation and position into the rest pose.
    memcpy(&Orientation, NewOrientation, 9 * sizeof(float));
    memcpy(&RestPose, &Orientation, 12 * sizeof(float));
    HolderSetMatrixAndUpdate(&MeshHolder, &Orientation.E[0][0]);
}

void WinButton::SetTransform(Transform const& NewTransform)
{
    // FUN_004c92b0.
    memcpy(&Orientation, &NewTransform, 12 * sizeof(float));
    HolderSetMatrixAndUpdate(&MeshHolder, &Orientation.E[0][0]);
}

void WinButton::Update()
{
    // FUN_0059f360.
    double const Dt = GetClientFrameTimeCapped();

    // Only a positive speed moves the angle.
    if (SpinSpeed > 0.0f)
    {
        double const Delta = Dt * SpinSpeed;
        if (bHover != 0)
        {
            SpinAngle = static_cast<float>(Delta + SpinAngle);
        }
        else
        {
            // Off the button the angle runs on at twice the speed to the nearer rest: past 180
            // it continues to 359 and snaps to 0, otherwise it winds back below 1 and snaps.
            double const Delta2 = Delta + Delta;
            if (SpinAngle > 180.0f)
            {
                double const Next = Delta2 + SpinAngle;
                SpinAngle = static_cast<float>(Next);
                if (Next > 359.0)
                    SpinAngle = 0.0f;
            }
            else
            {
                double const Next = SpinAngle - Delta2;
                SpinAngle = static_cast<float>(Next);
                if (Next < 1.0)
                    SpinAngle = 0.0f;
            }
        }
    }

    // Wrap into (-360, 360); NaN is left alone.
    if (SpinAngle >= 360.0f)
        SpinAngle = static_cast<float>(static_cast<double>(SpinAngle) - 360.0);
    else if (SpinAngle <= -360.0f)
        SpinAngle = static_cast<float>(static_cast<double>(SpinAngle) + 360.0);

    // The rest orientation times RotateMatrix((0, angle, 0)), with the rest translation, into
    // the live transform and the instance.
    Vector Angles;
    Angles.x = 0.0f;
    Angles.y = SpinAngle;
    Angles.z = 0.0f;
    Matrix const Spin = RotateMatrix(Angles);
    // DAT_0067dd10 (G_Win3DObjectMath, shared with FUN_0059efa0), FUN_005b73e0 when null.
    if (G_Win3DObjectMath == nullptr)
        G_Win3DObjectMath = Acquire3DMathEngine();
    Transform Live;
    G_Win3DObjectMath->MulMatrix(Live.Rotation, RestPose.Rotation,
                                 Spin);
    Live.Translation = RestPose.Translation;
    memcpy(&Orientation, &Live, 12 * sizeof(float));
    HolderSetMatrixAndUpdate(&MeshHolder, &Orientation.E[0][0]);

    // The Win3D child walk (FUN_005a17b0 inlined).
    Win3D* Child = ChildHead;
    while (Child != nullptr)
    {
        Win3D* const NextChild = Child->NextSibling;
        if ((Child->Flags & kWin3DActive) != 0)
            Child->Update();
        else
            Child->UpdateInactive();
        Child = NextChild;
    }
}

void WinButton::Draw()
{
    // FUN_0059f510: "[INTERFACE]" (option 6) and "NEURONET" (option 9) must both be on.
    if (GetGameOption(kGameOptInterfaceGroup) == 0)
        return;
    if (GetGameOption(kGameOptNeuronet) == 0)
        return;

    if ((Flags & kWin3DVisible) != 0)
    {
        MeshHolder.SubmitWin3DEngineInstance(Win3DGetCamera(), 0, 1.0f, 0);
        // With bit 0 of +0xa4 the tinted pass (FUN_00434630).
        if ((DrawFlags & 1) != 0)
        {
            SubmitWin3DTintedEngineInstanceWrapped(Win3DGetCamera(), MeshHolder.EngineInstance, 1,
                                                   TintRgba, 1, 1.0f, 0);
        }
    }

    if (ChildHead != nullptr)
        DrawChildren();
}

void WinButton::MouseOver(Vector const* Ray)
{
    // FUN_004c9380.
    bHover = HitTest(Ray) ? 1 : 0;
}

void WinButton::MouseLeave(Vector const* Ray)
{
    // LAB_004c93a0.
    (void)Ray;
    bHover = 0;
}

// ============================================================================================
// HUD_Button
// ============================================================================================

HUD_Button::HUD_Button(INI_Reader& Reader)
{
    // FUN_004c8d40 after the WinButton part: the HUDItem part at +0xf4 (inlined), the action
    // string at +0x12c emptied, both vptrs, HUDItem's name "Button" (FUN_00420750), the position
    // (0, 0, -5) straight into +0x60, +0x150 = 0, then the ini keys.
    Action.Length = 0;
    Action.Data[0] = '\0';
    HUDItem::Name = "Button";   // HUDItem's TString, not Win3D's +0x18 name
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = -5.0f;
    CommandId = 0;

    while (Reader.read_value())
    {
        if (Reader.is_value("action"))
        {
            char const* const Value = Reader.get_value_string();
            Action = Value;
            CommandId = 0;
            if (Action == "radio")
                CommandId = USER_RADIO;
            else if (Action == "status")
                CommandId = USER_STATUS;
            else if (Action == "zoom")
                CommandId = USER_RADAR_ZOOM;
            else if (Action == "navmap")
                CommandId = USER_NAV_MAP;
            else if (Action == "toggle_manuevers")
                CommandId = USER_MANEUVER_WINDOW;
            else
                DebugPrint("Warning: unknown HUD_Button action %s\n", Value);
        }
        else if (Reader.is_value("object"))
        {
            LoadMeshAndCreate(nullptr, Reader.get_value_string(), 0);
        }
        else if (Reader.is_value("object_pos"))
        {
            // Values 1, 0, 2 read in that order.
            float const Y = Reader.get_value_float(1);
            float const X = Reader.get_value_float(0);
            float const Z = Reader.get_value_float(2);
            Position.x = X;
            Position.y = Y;
            Position.z = Z;
            RestPose.Translation.x = X;
            RestPose.Translation.y = Y;
            RestPose.Translation.z = Z;
            SetTransform(RestPose);
        }
        else if (Reader.is_value("tool_tip_id"))
        {
            ToolTipIds = static_cast<int>(Reader.get_value_float(0));
        }
    }
}

HUD_Button::~HUD_Button()
{
    // FUN_004c94b0: WinObject::Destroy inlined (release a bound instance, clear bits 0 and 1,
    // slot 10 on every child, FUN_0059dc00); then ~HUDItem, the holder's destructor and ~Win3D
    // run as the bases unwind.
    WinObject::Destroy();
}

void HUD_Button::Update()
{
    // LAB_004c93d0: visible only with DAT_00673560 set and options 6 and 0xa on.
    bool const bVisible = IsCockpitResourceStringLoading() &&
                          GetGameOption(kGameOptInterfaceGroup) != 0 &&
                          GetGameOption(kGameOptHud) != 0;
    Flags = static_cast<unsigned char>((Flags & ~kWin3DVisible) | (bVisible ? kWin3DVisible : 0));
    if (bHover != 0)
        HUDItem::ShowToolTip(); // FUN_004c81d0 on +0xf4
    WinButton::Update();        // tail call FUN_0059f360
}

void HUD_Button::Draw()
{
    // LAB_004c93b0: depth test off, then a tail call of FUN_0059f510.
    Win3DDisable(kWin3DDepthTest);
    WinButton::Draw();
}

bool HUD_Button::MouseEvent(FWin3DMouseEvent const* Event)
{
    // LAB_004c9430: a click (-1, 2) posts the command record through FUN_0054bcb0.
    if (Event->Button != kWin3DMouseLeft)
        return false;
    if (Event->Action != kWin3DMousePress)
        return false;
    FSpaceCommand Command;
    memset(&Command, 0, sizeof(Command));
    Command.Byte26 = 1;
    Command.Id = CommandId;
    if (CommandId == 0)
        return false;
    return SpaceSendCommand(&Command);
}

HUDItem* HUD_ButtonFactory::Create(INI_Reader& Reader)
{
    // FUN_004c8d40: operator new(0x154), WinObject's ctor FUN_004c9060, the rest inlined.
    HUD_Button* const Button = new HUD_Button(Reader);
    return Button;
}

namespace
{
    // DAT_006733b0 -- XC entry LAB_004c8320: FUN_004c8ca0(this, "Button"); atexit LAB_004c8340.
    HUD_ButtonFactory G_HudButtonFactory("Button");
}
