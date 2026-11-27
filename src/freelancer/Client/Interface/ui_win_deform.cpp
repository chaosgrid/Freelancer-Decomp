// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/ui_win_deform.h"

#include "Core/globals.h"
#include "Space/HUD/hud_element_list.h"
#include "Space/space_scene_obj_draw.h"
#include "Interface/ui_page.h"
#include "Interface/ui_page_desc.h"
#include "Interface/win3d_immediate.h"
#include "Interface/win3d_render.h"
#include "Interface/win_factory.h"

#include "DALib.h"
#include "common_reputation.h"
#include "dacom.h"
#include "engine/engbase.h"
#include "x86math.h"

#include <stdio.h>
#include <string.h>

namespace
{
// .rdata 0x005e5874.
char const kUiWinCpp[] = "E:\\FL\\Scratch\\Source\\Client\\Interface\\UIWin.cpp";
} // namespace

UiWinDeform::UiWinDeform()
    : Width(0.0f)
    , Height(0.0f)
    , PoseZ(-5.0f)
    , PoseHeight(-1.0f)
{
    // FUN_0059aca0.
    Camera.Construct();

    // The preview camera takes the Win3D scene camera's viewport, near / far and half
    // horizontal fov.
    FSpaceCamera* const Scene = Win3DGetCamera();
    Camera.SetViewport(Scene->Viewport);
    Camera.SetNearFar(Scene->ZNear, Scene->ZFar);
    Camera.SetHalfFovX(Scene->HalfFovXDeg);
}

UiWinDeform::~UiWinDeform()
{
    // FUN_0059ae60.
    if (Body.Deformable != nullptr)
        Body.Reset();
}

void UiWinDeform::SetPosition(Vector const* NewPosition)
{
    // LAB_0059bb40 (RET 4).
    Position.x = NewPosition->x;
    Position.y = NewPosition->y;
    Position.z = NewPosition->z;

    if (Body.Deformable == nullptr)
        return;
    PoseBody();
}

void UiWinDeform::SetTransform(Transform const* NewXform)
{
    // LAB_0059b8f0 (RET 4).
    Transform Local;
    memcpy(&Local, NewXform, sizeof(Local));

    // The widget's centre offset, turned by the incoming rotation.
    Vector HalfExtent;
    HalfExtent.x = Width * 0.5f;
    HalfExtent.y = Height * -0.5f;
    HalfExtent.z = 0.0f;

    Vector Rotated;
    UiWidgetMathEngine()->TransformMatrix(Rotated,
                                          NewXform->Rotation,
                                          HalfExtent);

    Local.Translation.x = Rotated.x + NewXform->Translation.x;
    Local.Translation.y = Rotated.y + NewXform->Translation.y;
    Local.Translation.z = Rotated.z + NewXform->Translation.z;

    // Virtual calls, so this class's SetPosition poses the body.
    SetOrientation(&Local.Rotation);
    SetPosition(&Local.Translation);

    // The top-left and bottom-right corners through the new orientation + position (+0x3c).
    Transform const& Xform = Placement;
    Vector Corner;
    Corner.x = Width * -0.5f;
    Corner.y = Height * 0.5f;
    Corner.z = 0.0f;
    Vector TopLeft;
    UiWidgetMathEngine()->TransformXform(TopLeft, Xform, Corner);

    Corner.x = Width * 0.5f;
    Corner.y = Height * -0.5f;
    Corner.z = 0.0f;
    Vector BottomRight;
    UiWidgetMathEngine()->TransformXform(BottomRight, Xform, Corner);

    // Both corners projected by the Win3D scene camera; the results are not tested.
    ICamera* const Scene = AsCamera(Win3DGetCamera());
    float X1;
    float Y1;
    float Z1;
    Scene->Project(&X1, &Y1, &Z1, &TopLeft);
    float X2;
    float Y2;
    float Z2;
    Scene->Project(&X2, &Y2, &Z2, &BottomRight);

    // Truncated into {left, top, right, bottom}: the preview camera's viewport.
    int Rect[4];
    Rect[0] = static_cast<int>(X1);
    Rect[1] = static_cast<int>(Y1);
    Rect[2] = static_cast<int>(X2);
    Rect[3] = static_cast<int>(Y2);
    Camera.SetViewportEx(Rect, 1);
}

bool UiWinDeform::Create(Win3D* NewParent, void* Payload)
{
    // LAB_0059b8a0 (RET 8). Win3D's Create inlined.
    Win3D::Create(NewParent, Payload);

    if (Payload != nullptr)
    {
        FUiPayloadBounds const* const Bounds = static_cast<FUiPayloadBounds const*>(Payload);
        Width = Bounds->Right;
        Height = Bounds->Bottom;
    }
    return true;
}

void UiWinDeform::Update()
{
    // LAB_0059bc30. Win3D's Update inlined.
    Win3D::Update();

    if (Body.Deformable == nullptr)
        return;

    // The frame time is kept as a float; the test is on the double (unordered -> out).
    double const FrameTime = GetClientFrameTimeCapped();
    float Remaining = static_cast<float>(FrameTime);
    if (!(FrameTime > 0.0))
        return;

    for (;;)
    {
        // The step is 0.2f when Remaining >= 0.2f or unordered.
        float const Step = (Remaining < 0.2f) ? Remaining : 0.2f;

        // FUN_004cba60 inlined.
        Body.Advance(Step);

        // Loop while the unrounded difference is > 0.
        double const Left = static_cast<double>(Remaining) - static_cast<double>(Step);
        Remaining = static_cast<float>(Left);
        if (!(Left > 0.0))
            break;
    }
}

void UiWinDeform::Draw()
{
    // LAB_0059bda0. Retail's debug outline here is gated on DAT_0067dbc4, which has no writer
    // in the PE; left out, as in ui_static_text / ui_list_box.

    if (Body.Deformable != nullptr)
    {
        int Zeros[4] = {0, 0, 0, 0};
        // Retail does not initialize Draw's out-parameter.
        int OutLod = 0;

        Win3DApplyCamera(AsCamera(&Camera));
        Win3DSetViewport(AsCamera(&Camera), kUiWinCpp, 0x29f);

        DALib::Pipeline->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW);
        DALib::Pipeline->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);

        DALib::Deform->SetLod(Body.Deformable, 0.0f, Zeros);
        DALib::Deform->Draw(Body.Deformable, &Camera, Zeros, nullptr, &OutLod, 0);

        // The scene camera back.
        Win3DApplyCamera(AsCamera(Win3DGetCamera()));
        Win3DSetViewport(AsCamera(Win3DGetCamera()), kUiWinCpp, 0x2ad);
    }

    Win3D::Draw();
}

void UiWinDeform::SetCostume(char const* Name)
{
    // FUN_0059b020 (thiscall, RET 4).
    if (Body.Deformable != nullptr)
        Body.Reset();

    CostumeDescriptions const* const Descs = GetCostumeDescriptions();
    if (Descs == nullptr)
        return;
    costume const* const Found = Descs->find_costume(Name);
    if (Found == nullptr || Found->Id == 0)
        return;

    Costume Local;
    Local.PartIds[0] = 0;
    Local.PartIds[1] = 0;
    Local.PartIds[2] = 0;
    Local.PartIds[3] = 0;
    Local.AccessoryCount = 0;
    Descs->get_costume(static_cast<int>(Found->Id), Local);

    if (!Body.Build(Local))
    {
        char Message[0x2000];
        _snprintf(Message, sizeof(Message), "Failed to create costume \"%s\".\n", Name);
        Message[sizeof(Message) - 1] = '\0';
        FDUMP(kFdumpNotice, "%s(%d) : NOTICE:General:%s", kUiWinCpp, 0x210, Message);
        return;
    }

    if (Body.Deformable == nullptr)
        return;
    PoseBody();
}

void UiWinDeform::PoseBody()
{
    Matrix Rotation(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    HudMatrixRotationY(&Rotation, 3.14159274f);
    HudMatrixRotateRows12(&Rotation, 0.0f);
    HudMatrixRotateRows01(&Rotation, 0.0f);

    if (Body.Deformable != nullptr)
        DALib::Deform->SetHeight(Body.Deformable, PoseHeight);

    Vector Pose = {0.0f, 0.0f, PoseZ};
    DALib::Deform->SetPosePos(Body.Deformable, &Pose);

    DALib::Deform->SetPoseOri(Body.Deformable, &Rotation);
}

// DAT_0067da94.
WINFACTORY<UiWinDeform> G_UIWinDeformFactory("UIWinDeform");
