// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/ui_ship_view_control.h"

#include "Interface/ui_page.h"
#include "Interface/ui_page_desc.h"
#include "Interface/win_factory.h"

#include "x86math.h"

namespace
{
    // .data 0x006156f4 / 0x00615704 -- the rect and wire RGBA Create sets; both tables hold the
    // same four floats and nothing writes them.
    float const kUiShipViewRectColor[4] = {0.529411972f, 0.764706016f, 0.874509990f, 0.486274987f};
    float const kUiShipViewWireColor[4] = {0.529411972f, 0.764706016f, 0.874509990f, 0.486274987f};
}

UiShipViewControl::UiShipViewControl()
{
    // Inlined into FUN_0059ccf0. Retail leaves +0x570..+0x578 as heap bytes until Create; the
    // port starts them at 0.
    HalfExtent[0] = 0.0f;
    HalfExtent[1] = 0.0f;
    HalfExtent[2] = 0.0f;
}

UiShipViewControl::~UiShipViewControl()
{
    // FUN_0059bff0.
}

bool UiShipViewControl::Create(Win3D* NewParent, void* Payload)
{
    // FUN_0059c010 (RET 8): ShipViewControl's Create with no INI reader.
    bool const bCreated = ShipViewControl::Create(NewParent, nullptr);
    if (!bCreated)
        return bCreated;

    if (Payload != nullptr)
    {
        FUiPayloadBounds const* const Bounds = static_cast<FUiPayloadBounds const*>(Payload);
        HalfExtent[2] = 0.0f;
        HalfExtent[0] = Bounds->Right * 0.5f;
        HalfExtent[1] = Bounds->Bottom * -0.5f;
    }

    float Vector[3];
    Vector[0] = 0.0f;
    Vector[1] = 0.0f;
    Vector[2] = 30.0f;
    SetRotSpeed(Vector);
    Vector[0] = 0.0f;
    Vector[1] = 270.0f;
    Vector[2] = 0.0f;
    SetObjectYpr(Vector);
    Vector[0] = 0.0f;
    Vector[1] = 0.0f;
    Vector[2] = 0.0f;
    SetAttachPosition(Vector);
    SetShowRect(false);
    SetShowWireframe(true);
    SetRectColor(kUiShipViewRectColor);
    SetWireColor(kUiShipViewWireColor);
    return bCreated;
}

void UiShipViewControl::SetTransform(float const* Transform)
{
    // LAB_0059c100 (RET 4). The half extent turned by the rotation is the view size; that plus
    // the translation (each sum rounded to float) is the box's corner, the position.
    float Block[12];
    for (int Index = 0; Index < 12; ++Index)
        Block[Index] = Transform[Index];
    float const Translation[3] = {Transform[9], Transform[10], Transform[11]};

    FVector Turned;
    FVector Source;
    Source.X = HalfExtent[0];
    Source.Y = HalfExtent[1];
    Source.Z = HalfExtent[2];
    UiWidgetMathEngine()->TransformMatrix(Turned, *reinterpret_cast<FMatrix const*>(Transform),
                                          Source);

    float Corner[3];
    Corner[0] = Turned.X + Translation[0];
    Corner[1] = Turned.Y + Translation[1];
    Corner[2] = Turned.Z + Translation[2];
    float const ViewExtent[3] = {Turned.X, Turned.Y, Turned.Z};

    SetOrientation(Block);
    SetPosition(Corner);
    SetViewSize(ViewExtent);
}

// DAT_0067da3c.
WINFACTORY<UiShipViewControl> G_UIShipViewControlFactory("UIShipViewControl");
