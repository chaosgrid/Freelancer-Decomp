// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/ship_view_control.h"

#include "Common/engine_light.h"      // FEngineLight, EngineLight*
#include "GameFlow/gf_object_ship.h"    // GfShipApplyCollisionDamage
#include "Space/Effects/equip_visual_set.h"  // FEquipVisualSet
#include "Core/globals.h"           // GetClientFrameTimeCapped
#include "Space/HUD/hud_element_list.h"  // HudMatrixRotationY / RotateRows12 / RotateRows01
#include "Space/HUD/hud_target.h"        // FHudInstanceBounds, HudInverseTransformPoint
#include "NeuroNet/nn_ui_pulse.h"       // G_HudChromeColorRgba
#include "Interface/win3d_immediate.h"
#include "Interface/win3d_render.h"      // Win3DApplyCamera
#include "Interface/win_factory.h"

#include "Common/client_cliobj.h"
#include "common_archetype.h"
#include "common_csys.h"       // Acquire3DMathEngine
#include "common_goods.h"
#include "common_ini.h"
#include "dacom.h"
#include "DALib.h"
#include "engine/engbase.h"
#include "render/IRenderPipeline.h"
#include "render/rendcomp.h"
#include "x86math.h"

#include <math.h>
#include <string.h>

WINFACTORY<ShipViewControl> G_ShipViewControlFactory("ShipViewControl");

namespace
{
    // .rdata 0x005e4cd8.
    char const* const kShipViewControlCpp =
        "E:\\FL\\Scratch\\Source\\Client\\Interface\\ShipViewControl.cpp";

    // DAT_005c77f4 -- degrees to radians as a float.
    float const kDegToRad = 0.01745329238474369f;

    // DAT_0067c500 -- this file's lazily acquired 3DMathEngine.
    I3DMathEngine* G_ShipViewMath = nullptr;

    I3DMathEngine* GetShipViewMath()
    {
        if (G_ShipViewMath == nullptr)
            G_ShipViewMath = Acquire3DMathEngine();
        return G_ShipViewMath;
    }

    // DAT_00615240 -- 0x3c rows {HpAttachmentType, caption IDS}; no writer in the exe.
    struct FHpTypeCaption
    {
        int HpType;
        int Ids;
    };

    FHpTypeCaption const G_ShipViewHpTypeCaptions[0x3c] = {
        {HpTypeGunSpecial1, 1525}, {HpTypeGunSpecial2, 1526}, {HpTypeGunSpecial3, 1527},
        {HpTypeGunSpecial4, 1528}, {HpTypeGunSpecial5, 1529}, {HpTypeGunSpecial6, 1530},
        {HpTypeGunSpecial7, 1531}, {HpTypeGunSpecial8, 1532}, {HpTypeGunSpecial9, 1533},
        {HpTypeGunSpecial10, 1534}, {HpTypeTurret, 1524}, {HpTypeTurretSpecial1, 1731},
        {HpTypeTurretSpecial2, 1732}, {HpTypeTurretSpecial3, 1733}, {HpTypeTurretSpecial4, 1734},
        {HpTypeTurretSpecial5, 1735}, {HpTypeTurretSpecial6, 1736}, {HpTypeTurretSpecial7, 1737},
        {HpTypeTurretSpecial8, 1738}, {HpTypeTurretSpecial9, 1739}, {HpTypeTurretSpecial10, 1740},
        {HpTypeTorpedo, 1521}, {HpTypeTorpedoSpecial1, 1741}, {HpTypeTorpedoSpecial2, 1742},
        {HpTypeMineDropper, 1522}, {HpTypeCountermeasureDropper, 1523}, {HpTypeFighterShieldSpecial1, 1700},
        {HpTypeFighterShieldSpecial2, 1701}, {HpTypeFighterShieldSpecial3, 1702}, {HpTypeFighterShieldSpecial4, 1704},
        {HpTypeFighterShieldSpecial5, 1705}, {HpTypeFighterShieldSpecial6, 1706}, {HpTypeFighterShieldSpecial7, 1707},
        {HpTypeFighterShieldSpecial8, 1708}, {HpTypeFighterShieldSpecial9, 1709}, {HpTypeFighterShieldSpecial10, 1710},
        {HpTypeFighterShieldGenerator, 1517}, {HpTypeEliteShieldSpecial1, 1711}, {HpTypeEliteShieldSpecial2, 1712},
        {HpTypeEliteShieldSpecial3, 1713}, {HpTypeEliteShieldSpecial4, 1714}, {HpTypeEliteShieldSpecial5, 1715},
        {HpTypeEliteShieldSpecial6, 1716}, {HpTypeEliteShieldSpecial7, 1717}, {HpTypeEliteShieldSpecial8, 1718},
        {HpTypeEliteShieldSpecial9, 1719}, {HpTypeEliteShieldSpecial10, 1720}, {HpTypeEliteShieldGenerator, 1518},
        {HpTypeFreighterShieldSpecial1, 1721}, {HpTypeFreighterShieldSpecial2, 1722}, {HpTypeFreighterShieldSpecial3, 1723},
        {HpTypeFreighterShieldSpecial4, 1724}, {HpTypeFreighterShieldSpecial5, 1725}, {HpTypeFreighterShieldSpecial6, 1726},
        {HpTypeFreighterShieldSpecial7, 1727}, {HpTypeFreighterShieldSpecial8, 1728}, {HpTypeFreighterShieldSpecial9, 1729},
        {HpTypeFreighterShieldSpecial10, 1730}, {HpTypeFreighterShieldGenerator, 1519}, {HpTypeThruster, 1520}};

    // The grouping switch HasHpTypeGroup and BuildHardpointList each inline: 0 for 4, 0xa..0xd
    // and 0xf..0x24; 1 for 5..9 and 0x25..0x42; 2 for 0xe and for anything outside 4..0x42 (the
    // switch default is the 0xe case).
    int HpTypeGroup(int HpType)
    {
        if (HpType < HpTypeGun || HpType > HpTypeFreighterShieldSpecial10)
            return 2;
        if (HpType == HpTypeCargoPod)
            return 2;
        if ((HpType >= HpTypeShieldGenerator && HpType <= HpTypeThruster) ||
            HpType >= HpTypeFighterShieldSpecial1)
            return 1;
        return 0;
    }

    IRenderPipeline* ShipViewPipeline()
    {
        return DALib::Pipeline;
    }

    void SetIdentity9(Matrix* M)
    {
        M->E[0][0] = 1.0f;
        M->E[0][1] = 0.0f;
        M->E[0][2] = 0.0f;
        M->E[1][0] = 0.0f;
        M->E[1][1] = 1.0f;
        M->E[1][2] = 0.0f;
        M->E[2][0] = 0.0f;
        M->E[2][1] = 0.0f;
        M->E[2][2] = 1.0f;
    }

    // The orientation every ypr writer builds (Create, and inlined in Init, DragRotate and
    // SetObjectYpr).
    void BuildYprOrientation(Matrix* M, float const* Ypr)
    {
        HudMatrixRotationY(M, Ypr[0] * kDegToRad);
        HudMatrixRotateRows12(M, Ypr[1] * kDegToRad);
        HudMatrixRotateRows01(M, Ypr[2] * kDegToRad);
    }

    // 255.0 is DAT_005c89c0; truncated.
    unsigned char ColorByte(double Value)
    {
        return static_cast<unsigned char>(static_cast<int>(Value * 255.0));
    }

    // The mountpoint marker holders are released twice in retail: explicitly when an instance
    // is bound, then by ~FClientMeshHolder, which finds nothing.
    void DeleteMountHolder(FClientMeshHolder* Holder)
    {
        if (Holder->EngineInstance != -1)
            Holder->ReleaseWithAnim();
        delete Holder;
    }
}

int ShipViewEquipmentHpClass(unsigned ArchId, Archetype::Equipment const** OutWeapon,
                             Archetype::Equipment const** OutShield)
{
    // FUN_005835f0 (cdecl).
    *OutWeapon = nullptr;
    *OutShield = nullptr;
    Archetype::Equipment* const Equip = Archetype::GetEquipment(ArchId);
    if (Equip == nullptr)
        return 0;
    int const HpType = static_cast<int>(Equip->get_hp_type());
    if (HpType == HpTypeNone)
        return 1;
    if (HpType == HpTypeGun || HpType == HpTypeTurret || HpType == HpTypeTorpedo)
    {
        *OutWeapon = Equip;
        return 4;
    }
    if (HpType == HpTypeShieldGenerator || HpType == HpTypeFighterShieldGenerator ||
        HpType == HpTypeEliteShieldGenerator || HpType == HpTypeFreighterShieldGenerator)
    {
        if (Equip->get_class_type() == Archetype::AClassShieldGenerator)
            *OutShield = Equip;
    }
    return HpType;
}

int ShipViewEquipDescHpClass(EquipDesc const* Desc, Archetype::Equipment const** OutWeapon,
                             Archetype::Equipment const** OutShield)
{
    // FUN_00583690.
    return ShipViewEquipmentHpClass(Desc->ArchId, OutWeapon, OutShield);
}

int ShipViewHpTypeClass(int HpType)
{
    // FUN_00585b10.
    switch (HpType)
    {
    case HpTypeTorpedo:
    case HpTypeTorpedoSpecial1:
        return 1;
    case HpTypeTorpedoSpecial2:
        return 2;
    case HpTypeMineDropper:
        return 3;
    case HpTypeTurret:
    case HpTypeTurretSpecial1:
    case HpTypeTurretSpecial2:
    case HpTypeTurretSpecial3:
    case HpTypeTurretSpecial4:
    case HpTypeTurretSpecial5:
    case HpTypeTurretSpecial6:
    case HpTypeTurretSpecial7:
    case HpTypeTurretSpecial8:
    case HpTypeTurretSpecial9:
    case HpTypeTurretSpecial10:
        return 4;
    case HpTypeCountermeasureDropper:
        return 5;
    case HpTypeShieldGenerator:
    case HpTypeFighterShieldGenerator:
    case HpTypeFighterShieldSpecial1:
    case HpTypeFighterShieldSpecial2:
    case HpTypeFighterShieldSpecial3:
    case HpTypeFighterShieldSpecial4:
    case HpTypeFighterShieldSpecial5:
    case HpTypeFighterShieldSpecial6:
    case HpTypeFighterShieldSpecial7:
    case HpTypeFighterShieldSpecial8:
    case HpTypeFighterShieldSpecial9:
    case HpTypeFighterShieldSpecial10:
        return 6;
    case HpTypeEliteShieldGenerator:
    case HpTypeEliteShieldSpecial1:
    case HpTypeEliteShieldSpecial2:
    case HpTypeEliteShieldSpecial3:
    case HpTypeEliteShieldSpecial4:
    case HpTypeEliteShieldSpecial5:
    case HpTypeEliteShieldSpecial6:
    case HpTypeEliteShieldSpecial7:
    case HpTypeEliteShieldSpecial8:
    case HpTypeEliteShieldSpecial9:
    case HpTypeEliteShieldSpecial10:
        return 7;
    case HpTypeFreighterShieldGenerator:
    case HpTypeFreighterShieldSpecial1:
    case HpTypeFreighterShieldSpecial2:
    case HpTypeFreighterShieldSpecial3:
    case HpTypeFreighterShieldSpecial4:
    case HpTypeFreighterShieldSpecial5:
    case HpTypeFreighterShieldSpecial6:
    case HpTypeFreighterShieldSpecial7:
    case HpTypeFreighterShieldSpecial8:
    case HpTypeFreighterShieldSpecial9:
    case HpTypeFreighterShieldSpecial10:
        return 8;
    case HpTypeThruster:
        return 9;
    default:
        return 0;
    }
}

int ShipViewHpTypeTableCount()
{
    // FUN_00584710.
    return 0x3c;
}

int ShipViewHpTypeTableType(int Index)
{
    // FUN_00584720.
    if (Index < 0 || Index >= 0x3c)
        return 0;
    return G_ShipViewHpTypeCaptions[Index].HpType;
}

ShipViewControl::ShipViewControl()
{
    // FUN_005836a0, in store order. ObjectYpr (+0x37c), ScreenRect (+0x494), SelRadius /
    // SelExtent (+0x50c / +0x510), Lights[3..7], Field514 and Field548 are not written, as in
    // retail.
    ShipId.clear();
    ClientMeshHolderConstruct(&ShipHolder);
    Camera.Construct();
    Ambient[3] = 0;
    Ambient[2] = 0;
    Ambient[1] = 0;
    Ambient[0] = 0;
    WireColor[3] = 0;
    WireColor[2] = 0;
    WireColor[1] = 0;
    WireColor[0] = 0;
    RectColor[3] = 0;
    RectColor[2] = 0;
    RectColor[1] = 0;
    RectColor[0] = 0;
    Color4c4[3] = 0;
    Color4c4[2] = 0;
    Color4c4[1] = 0;
    Color4c4[0] = 0;
    Visuals = nullptr;
    Distance = 0.0f;
    Mode = 2;
    Reset();

    ViewOffset[0] = 0.0f;
    ViewOffset[1] = 0.0f;
    ViewOffset[2] = 0.0f;
    ViewSize[0] = 0.0f;
    ViewSize[1] = 0.0f;
    ViewSize[2] = 0.0f;
    AttachPos[0] = 0.0f;
    AttachPos[1] = 0.0f;
    AttachPos[2] = 0.0f;
    SetIdentity9(&ObjectOrientation);

    memcpy(WireColor, G_HudChromeColorRgba, sizeof(WireColor));
    memcpy(RectColor, G_HudChromeColorRgba, sizeof(RectColor));
    Color4c4[0] = 0xc3;
    Color4c4[1] = 0xb8;
    Color4c4[2] = 0x20;
    Color4c4[3] = 0xff;
    RotSpeed[0] = 0.0f;
    RotSpeed[1] = 0.0f;
    RotSpeed[2] = 0.0f;
    RotAccum[0] = 0.0f;
    RotAccum[1] = 0.0f;
    Ambient[0] = 0;
    Ambient[1] = 0;
    Ambient[2] = 0;
    Ambient[3] = 0xff;
    FovY = 25.0f;
    RotAccum[2] = 0.0f;

    // Three directional lights.
    float const Directions[3][3] = {
        {1.0f, 1.0f, -1.0f}, {-0.965f, 0.259f, 0.965f}, {-1.0f, -1.0f, 1.0f}};
    unsigned char const Colors[3][4] = {
        {0xff, 0xff, 0xff, 0xff}, {0x32, 0x46, 0x64, 0xff}, {0x32, 0x46, 0x64, 0xff}};
    LightCount = 3;
    for (int Index = 0; Index < LightCount; ++Index)
    {
        // operator new(0xc8).
        FEngineLight* const Light = static_cast<FEngineLight*>(operator new(sizeof(FEngineLight)));
        EngineLightInit(Light, kLightNone);
        Lights[Index] = Light;
        Light->SetType(kLightDirectional, false);

        // The direction negated and normalised; the squares sum y, z, x, as retail.
        float const X = Directions[Index][0];
        float const Y = Directions[Index][1];
        float const Z = Directions[Index][2];
        float const Inverse = 1.0f / sqrtf(Y * Y + Z * Z + X * X);
        Vector Direction;
        Direction.x = -(X * Inverse);
        Direction.y = -(Y * Inverse);
        Direction.z = -(Z * Inverse);
        Light->SetDirection(&Direction);
        Light->SetColorBytes(Colors[Index]);
        Light->FEngineLight::Enable();
    }
    Byte55c = 0;
}

ShipViewControl::~ShipViewControl()
{
    // FUN_00583c30.
    for (size_t Index = 0; Index < MountHolders.size(); ++Index)
        DeleteMountHolder(MountHolders[Index]);
    MountHolders.clear();

    for (int Index = 0; Index < LightCount; ++Index)
    {
        if (Lights[Index] != nullptr)
            EngineLightDelete(Lights[Index]);
    }

    if (Visuals != nullptr)
    {
        delete Visuals;
        Visuals = nullptr;
    }
    ShipHolder.ReleaseWithAnim();
}

void ShipViewControl::Reset()
{
    // FUN_00583b90.
    SelHpClass = 0;
    SelWeapon = nullptr;
    SelShield = nullptr;
    Field4f4 = 0;
    Field4f8 = 0;
    ViewFlags = 0;
    Instance = -1;
    SelInstance = -1;
    Field4f0 = -1;
    DragLast[0] = 0.0f;
    DragLast[1] = 0.0f;
    DragLast[2] = 0.0f;
    DragYpr[0] = 0.0f;
    DragYpr[1] = 0.0f;
    DragYpr[2] = 0.0f;
}

bool ShipViewControl::Create(Win3D* NewParent, void* Data)
{
    // FUN_00583e30 (RET 8): Win3D's Create inlined (reparent, register, flags), then the INI
    // keys. Always returns 1.
    Win3D::Create(NewParent, nullptr);
    INI_Reader* const Ini = static_cast<INI_Reader*>(Data);
    if (Ini == nullptr || !Ini->read_value())
        return true;

    do
    {
        // Every key is tested in turn; only "viewSize" skips the "hardpoint" test.
        if (Ini->is_value("rot_speed"))
        {
            float const Y = Ini->get_value_float(1);
            float const X = Ini->get_value_float(0);
            float const Z = Ini->get_value_float(2);
            RotSpeed[0] = X;
            RotSpeed[1] = Y;
            RotSpeed[2] = Z;
        }
        if (Ini->is_value("offset"))
        {
            float const Y = Ini->get_value_float(1);
            float const X = Ini->get_value_float(0);
            float const Z = Ini->get_value_float(2);
            ViewOffset[0] = X;
            ViewOffset[1] = Y;
            ViewOffset[2] = Z;
        }
        if (Ini->is_value("object_ypr"))
        {
            float const Y = Ini->get_value_float(1);
            float const X = Ini->get_value_float(0);
            float const Z = Ini->get_value_float(2);
            ObjectYpr[0] = X;
            ObjectYpr[1] = Y;
            ObjectYpr[2] = Z;
            BuildYprOrientation(&ObjectOrientation, ObjectYpr);
        }
        if (Ini->is_value("viewSize"))
        {
            float const Y = Ini->get_value_float(1);
            float const X = Ini->get_value_float(0);
            float const Z = Ini->get_value_float(2);
            ViewSize[0] = X;
            ViewSize[1] = Y;
            ViewSize[2] = Z;
        }
        else if (Ini->is_value("hardpoint"))
        {
            // The NEW parent's hardpoint, then copied into AttachPos.
            SetIdentity9(&AttachOrientation);
            AttachPosition.z = 0.0f;
            AttachPosition.y = 0.0f;
            AttachPosition.x = 0.0f;
            if (NewParent != nullptr)
            {
                WinControl* const ParentControl = NewParent->AsWinControl();
                if (ParentControl != nullptr)
                {
                    ParentControl->GetHardpointTransform(Ini->get_value_string(),
                                                         &AttachXform);
                    AttachPos[0] = AttachPosition.x;
                    AttachPos[1] = AttachPosition.y;
                    AttachPos[2] = AttachPosition.z;
                }
            }
        }
        if (Ini->is_value("show_rect"))
        {
            unsigned char const Bit = (Ini->get_value_float(0) != 0.0f) ? kShipViewShowRect : 0;
            ViewFlags = static_cast<unsigned char>((ViewFlags & ~kShipViewShowRect) | Bit);
        }
        if (Ini->is_value("show_wireframe"))
        {
            unsigned char const Bit = (Ini->get_value_float(0) != 0.0f) ? kShipViewShowWireframe : 0;
            ViewFlags = static_cast<unsigned char>((ViewFlags & ~kShipViewShowWireframe) | Bit);
        }
        if (Ini->is_value("rect_color"))
        {
            // Components 2, 1, 0, 3 read in that order.
            double const V2 = Ini->get_value_float(2);
            double const V1 = Ini->get_value_float(1);
            double const V0 = Ini->get_value_float(0);
            unsigned char const B3 = ColorByte(Ini->get_value_float(3));
            RectColor[0] = ColorByte(V0);
            RectColor[1] = ColorByte(V1);
            RectColor[2] = ColorByte(V2);
            RectColor[3] = B3;
        }
        if (Ini->is_value("wire_color"))
        {
            double const V2 = Ini->get_value_float(2);
            double const V1 = Ini->get_value_float(1);
            double const V0 = Ini->get_value_float(0);
            unsigned char const B3 = ColorByte(Ini->get_value_float(3));
            WireColor[0] = ColorByte(V0);
            WireColor[1] = ColorByte(V1);
            WireColor[2] = ColorByte(V2);
            WireColor[3] = B3;
        }
    } while (Ini->read_value());
    return true;
}

void ShipViewControl::Init(Vector const* NewRotSpeed, Vector const* NewOffset, Vector const* NewYpr,
                           Vector const* NewViewSize, char const* Hardpoint, bool bShowRect,
                           bool bShowWire, unsigned const* NewRectColor,
                           unsigned const* NewWireColor)
{
    // FUN_005842a0 (RET 0x24).
    RotSpeed[0] = NewRotSpeed->x;
    RotSpeed[1] = NewRotSpeed->y;
    RotSpeed[2] = NewRotSpeed->z;
    ViewOffset[0] = NewOffset->x;
    ViewOffset[1] = NewOffset->y;
    ViewOffset[2] = NewOffset->z;
    ObjectYpr[0] = NewYpr->x;
    ObjectYpr[1] = NewYpr->y;
    ObjectYpr[2] = NewYpr->z;
    BuildYprOrientation(&ObjectOrientation, ObjectYpr);
    ViewSize[0] = NewViewSize->x;
    ViewSize[1] = NewViewSize->y;
    ViewSize[2] = NewViewSize->z;
    if (Hardpoint != nullptr)
    {
        SetIdentity9(&AttachOrientation);
        AttachPosition.z = 0.0f;
        AttachPosition.y = 0.0f;
        AttachPosition.x = 0.0f;
        if (Parent != nullptr)
        {
            WinControl* const ParentControl = Parent->AsWinControl();
            if (ParentControl != nullptr)
            {
                ParentControl->GetHardpointTransform(Hardpoint, &AttachXform);
                AttachPos[0] = AttachPosition.x;
                AttachPos[1] = AttachPosition.y;
                AttachPos[2] = AttachPosition.z;
            }
        }
    }
    unsigned char Bits = static_cast<unsigned char>((ViewFlags & ~kShipViewShowRect) | (bShowRect ? kShipViewShowRect : 0));
    Bits = static_cast<unsigned char>((Bits & ~kShipViewShowWireframe) | (bShowWire ? kShipViewShowWireframe : 0));
    ViewFlags = Bits;
    memcpy(RectColor, NewRectColor, sizeof(RectColor));
    memcpy(WireColor, NewWireColor, sizeof(WireColor));
}

void ShipViewControl::Draw()
{
    // LAB_00584470.
    Win3D::Draw(); // the children; no WinControl mesh
    if (Instance == -1)
        return;
    if ((ScreenRect[3] | ScreenRect[2] | ScreenRect[1] | ScreenRect[0]) == 0)
        return;

    // This control's light rig and ambient.
    EngineLightClearRegisteredList();
    unsigned char AmbientRgb[3];
    AmbientRgb[0] = Ambient[0];
    AmbientRgb[1] = Ambient[1];
    AmbientRgb[2] = Ambient[2];
    EngineLightSetAmbientRgbBytes(AmbientRgb);
    EngineLightApplyList(Lights, static_cast<unsigned>(LightCount), AsCamera(&Camera), nullptr);

    // The camera into the screen rect, the old viewport kept.
    Camera.SetFovY(FovY);
    Camera.SetViewportEx(ScreenRect, 1);
    int SavedViewport[4];
    Win3DGetViewport(0xba2, SavedViewport);
    ICamera* const ViewCamera = AsCamera(&Camera);
    Win3DSetViewport(ViewCamera, kShipViewControlCpp, 0x1d8);

    // Clear the depth buffer over the camera's viewport.
    IRenderPipeline* const Pipe = ShipViewPipeline();
    int const* const View = ViewCamera->GetViewport();
    int ClearRect[4];
    ClearRect[0] = 0;
    ClearRect[1] = 0;
    ClearRect[2] = View[2] - View[0] - 1;
    ClearRect[3] = View[3] - View[1] - 1;
    Pipe->ClearBuffers(2, ClearRect);
    Win3DApplyCameraPerspective(ViewCamera);

    // z on, draw the framed instance, then back.
    Pipe->SetRenderState(D3DRS_ZENABLE, 1);
    Pipe->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    Pipe->SetRenderState(D3DRS_ZWRITEENABLE, 1);
    // 0x300 is the flag word (rendcomp LAB_06c32a80).
    DALib::Renderer->RenderInstance(ViewCamera, Instance, 1.0f, 0x300u, nullptr, nullptr);
    Win3DDisable(0xb71);
    Pipe->SetRenderState(D3DRS_ZENABLE, 0);
    Pipe->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
    Pipe->SetRenderState(D3DRS_ZWRITEENABLE, 0);
    Win3DDisable(0xb71);

    // The Win3D camera and the saved viewport back, white ambient.
    Win3DApplyCamera(AsCamera(Win3DGetCamera()));
    Win3DSetViewportRect(SavedViewport[0], SavedViewport[1], SavedViewport[2],
                         SavedViewport[3], kShipViewControlCpp, 0x1fc);
    unsigned char const White[3] = {0xff, 0xff, 0xff};
    EngineLightClearRegisteredList();
    EngineLightSetAmbientRgbBytes(White);
}

void ShipViewControl::SetPosition(Vector const* NewPosition)
{
    // LAB_004b5020: Win3D's, not WinControl's.
    Win3D::SetPosition(NewPosition);
}

bool ShipViewControl::IsPositionInViewport(unsigned A)
{
    // LAB_005846b0 (RET 4): A is a position; true when its view box lies strictly inside the
    // Win3D camera's viewport (DAT_0067dca4..DAT_0067dcb0).
    ComputeScreenRect(reinterpret_cast<float const*>(static_cast<uintptr_t>(A)), false);
    FSpaceCamera const* const WinCamera = Win3DGetCamera();
    int const Width = WinCamera->Viewport[2] - WinCamera->Viewport[0] + 1;
    int const Height = WinCamera->Viewport[3] - WinCamera->Viewport[1] + 1;
    return ScreenRect[0] > 0 && ScreenRect[2] < Width && ScreenRect[1] > 0 &&
           ScreenRect[3] < Height;
}

void ShipViewControl::Update()
{
    // LAB_00584740 -- no base Update call.
    if (ShipHolder.HasInstance())
    {
        // Rotate while any rot_speed component squared is non-zero; NaN rotates.
        double const Square0 = static_cast<double>(RotSpeed[0]) * RotSpeed[0];
        double const Square1 = static_cast<double>(RotSpeed[1]) * RotSpeed[1];
        double const Square2 = static_cast<double>(RotSpeed[2]) * RotSpeed[2];
        if (Square0 != 0.0 || Square1 != 0.0 || Square2 != 0.0)
        {
            double const Dt = GetClientFrameTimeCapped();
            float const Step0 = static_cast<float>(Dt * RotSpeed[0]);
            float const Step1 = static_cast<float>(Dt * RotSpeed[1]);
            float const Step2 = static_cast<float>(Dt * RotSpeed[2]);
            RotAccum[0] = Step0 + RotAccum[0];
            RotAccum[1] = Step1 + RotAccum[1];
            RotAccum[2] = Step2 + RotAccum[2];

            Transform Turned;
            Matrix* const M = &Turned.Rotation;
            HudMatrixRotationY(M, (ObjectYpr[0] + RotAccum[0]) * kDegToRad);
            HudMatrixRotateRows12(M, (RotAccum[1] + ObjectYpr[1]) * kDegToRad);
            HudMatrixRotateRows01(M, (RotAccum[2] + ObjectYpr[2]) * kDegToRad);
            DALib::Engine->SetInstanceOrientation(ShipHolder.EngineInstance, M);
            ShipHolder.Update();

            // With no equipment shown, re-centre the hull on its longest axis.
            float Axis[3];
            Axis[0] = 0.0f;
            Axis[1] = 0.0f;
            Axis[2] = 0.0f;
            if (ShipHolder.EngineInstance == -1 || Visuals != nullptr)
            {
                ComputeScreenRect(&Position.x, true);
                return;
            }
            const ArchetypeIndex Arch = DALib::Engine->GetInstanceArchetype(ShipHolder.EngineInstance);
            if (Arch == -1)
            {
                ComputeScreenRect(&Position.x, true);
                return;
            }
            float Box[6];
            Box[0] = 0.0f;
            Box[1] = 0.0f;
            Box[2] = 0.0f;
            Box[3] = 0.0f;
            Box[4] = 0.0f;
            Box[5] = 0.0f;
            DALib::Renderer->GetBounds(Arch, 1.0f, Box);
            DALib::Engine->ReleaseHandle(Arch);

            // The axis whose extent dominates, when the other two together are less than half
            // of the total. The sums are grouped as retail's.
            float const Dx = Box[0] - Box[1];
            float const Dy = Box[2] - Box[3];
            float const Dz = Box[4] - Box[5];
            if (Dx > Dy && Dx > Dz && (Dz + Dy) / (Dx + (Dz + Dy)) < 0.5f)
            {
                Axis[0] = 1.0f;
            }
            else if (Dy > Dx && Dy > Dz && (Dz + Dx) / ((Dz + Dy) + Dx) < 0.5f)
            {
                Axis[1] = 1.0f;
            }
            else if (Dz > Dx && Dz > Dy && (Dy + Dx) / ((Dz + Dy) + Dx) < 0.5f)
            {
                Axis[2] = 1.0f;
            }
            else
            {
                ComputeScreenRect(&Position.x, true);
                return;
            }

            // The framed instance's sphere; the point Radius along the axis through
            // {turned, centre}; the ship moved by half its offset from the centre.
            float Radius;
            Vector Centre;
            DALib::Engine->GetInstanceExtent(Instance, 0, &Radius, &Centre);
            Turned.Translation = Centre;
            Vector Along;
            Along.x = Radius * Axis[0];
            Along.y = Axis[1] * Radius;
            Along.z = Axis[2] * Radius;
            Vector Tip;
            GetShipViewMath()->TransformXform(Tip, Turned, Along);
            Transform Moved;
            Moved.Rotation = Turned.Rotation;
            Moved.Translation.x = (Tip.x - Centre.x) * 0.5f;
            Moved.Translation.y = (Tip.y - Centre.y) * 0.5f;
            Moved.Translation.z = (Tip.z - Centre.z) * 0.5f;
            DALib::Engine->SetInstanceMatrix(ShipHolder.EngineInstance, &Moved);
            DALib::Engine->UpdateInstance(ShipHolder.EngineInstance, 0, nullptr);
        }
        else
        {
            DALib::Engine->SetInstanceOrientation(ShipHolder.EngineInstance, &ObjectOrientation);
            ShipHolder.Update();
        }
    }
    ComputeScreenRect(&Position.x, true);
}

bool ShipViewControl::HitTest(Vector const* Ray)
{
    // LAB_00584bd0 (RET 4): the ray's point at 1000 projected by the Win3D camera, strictly
    // inside the screen rect.
    Vector World;
    World.x = Ray->x * 1000.0f; // DAT_005e4d14
    World.y = Ray->y * 1000.0f;
    World.z = Ray->z * 1000.0f;
    float X;
    float Y;
    float Z;
    AsCamera(Win3DGetCamera())->Project(&X, &Y, &Z, &World);
    return static_cast<double>(ScreenRect[0]) < X && static_cast<double>(ScreenRect[2]) > X &&
           static_cast<double>(ScreenRect[1]) < Y && static_cast<double>(ScreenRect[3]) > Y;
}

void ShipViewControl::DragRotate(bool bStart)
{
    // FUN_00584c80 (RET 4).
    Vector Ray;
    Win3DGetMouseRay(&Ray);
    Vector World;
    World.x = Ray.x * 1000.0f;
    World.y = Ray.y * 1000.0f;
    World.z = Ray.z * 1000.0f;
    float X;
    float Y;
    float Z;
    AsCamera(Win3DGetCamera())->Project(&X, &Y, &Z, &World);

    // Strictly inside the rect; left / top are rounded to float first.
    float const Left = static_cast<float>(ScreenRect[0]);
    if (!(X > Left))
        return;
    if (!(static_cast<double>(ScreenRect[2]) > X))
        return;
    float const Top = static_cast<float>(ScreenRect[1]);
    if (!(Y > Top))
        return;
    if (!(static_cast<double>(ScreenRect[3]) > Y))
        return;

    float const HalfWidth = static_cast<float>(ScreenRect[2] - ScreenRect[0]) * 0.5f;
    float const HalfHeight = static_cast<float>(ScreenRect[3] - ScreenRect[1]) * 0.5f;
    float Angles[3];
    Angles[0] = 180.0f - (Left + HalfWidth - X) / fabsf(HalfWidth) * 90.0f;
    Angles[1] = -90.0f - (Top + HalfHeight - Y) / fabsf(HalfHeight) * 90.0f;
    Angles[2] = 0.0f;
    if (bStart)
    {
        DragLast[0] = Angles[0];
        DragLast[1] = Angles[1];
        DragLast[2] = Angles[2];
        return;
    }

    // Yaw is mirrored while the pitch is past +-180.
    float Delta0 = Angles[0] - DragLast[0];
    float const Delta1 = Angles[1] - DragLast[1];
    float const Delta2 = -DragLast[2];
    if (DragYpr[1] > 180.0f || DragYpr[1] < -180.0f)
        Delta0 = -Delta0;
    DragYpr[0] = Delta0 + DragYpr[0];
    DragYpr[1] = Delta1 + DragYpr[1];
    DragYpr[2] = Delta2 + DragYpr[2];
    DragLast[0] = Angles[0];
    DragLast[1] = Angles[1];
    DragLast[2] = Angles[2];

    // Yaw and pitch wrapped into [-360, 360] (DAT_005e4d20 / DAT_005e4d18 doubles, DAT_005ca23c
    // step).
    if (DragYpr[0] < -360.0)
    {
        float Value = DragYpr[0];
        do
            Value += 360.0f;
        while (Value < -360.0);
        DragYpr[0] = Value;
    }
    if (DragYpr[1] < -360.0)
    {
        float Value = DragYpr[1];
        do
            Value += 360.0f;
        while (Value < -360.0);
        DragYpr[1] = Value;
    }
    if (DragYpr[0] > 360.0)
    {
        float Value = DragYpr[0];
        do
            Value -= 360.0f;
        while (Value > 360.0);
        DragYpr[0] = Value;
    }
    if (DragYpr[1] > 360.0)
    {
        float Value = DragYpr[1];
        do
            Value -= 360.0f;
        while (Value > 360.0);
        DragYpr[1] = Value;
    }

    ObjectYpr[0] = DragYpr[0];
    ObjectYpr[1] = DragYpr[1];
    ObjectYpr[2] = DragYpr[2];
    BuildYprOrientation(&ObjectOrientation, ObjectYpr);
}

void ShipViewControl::SetDragYpr(Vector const* Ypr)
{
    // FUN_00585070 (RET 4).
    DragYpr[0] = Ypr->x;
    DragYpr[1] = Ypr->y;
    DragYpr[2] = Ypr->z;
}

void ShipViewControl::LoadEquipment(EquipDescList const* List)
{
    // FUN_005850f0 (RET 4).
    if (Visuals != nullptr)
    {
        Visuals->Clear();
        delete Visuals;
        Visuals = nullptr;
    }
    if (List == nullptr)
        return;

    Equipment.Items.clear();
    Equipment.Items = List->Items;

    // operator new(0x2c).
    Visuals = new FEquipVisualSet();
    Visuals->Populate(Instance);

    // Drop internal items and items on a hardpoint the summary lacks.
    HardpointSummary* const Summary = Visuals->Summary;
    std::list<EquipDesc>::iterator It = Equipment.Items.begin();
    while (It != Equipment.Items.end())
    {
        EquipDesc const& Desc = *It;
        if (Desc.is_internal() || Summary->find_name(Desc.Hardpoint) == -1)
        {
            It = Equipment.Items.erase(It);
            continue;
        }
        ++It;
    }
    Visuals->AttachAll(&Equipment);

    // One mountpoint marker per hardpoint.
    for (size_t HolderIndex = 0; HolderIndex < MountHolders.size(); ++HolderIndex)
        DeleteMountHolder(MountHolders[HolderIndex]);
    MountHolders.clear();
    int const Count = Visuals->Count;
    if (MountHolders.capacity() < static_cast<size_t>(Count < 0 ? 0 : Count))
        MountHolders.reserve(static_cast<size_t>(Count < 0 ? 0 : Count));
    for (int Remaining = Count; Remaining > 0; --Remaining)
    {
        // operator new(0x28).
        FClientMeshHolder* const Holder = new FClientMeshHolder();
        ClientMeshHolderConstruct(Holder);
        Holder->LoadFromPath("Interface\\NeuroNet\\Inventory\\mountpoint.3db", 0,
                             true);
        MountHolders.push_back(Holder);
    }
}

void ShipViewControl::SetInstance(long NewInstance, bool bFixedDistance)
{
    // FUN_00585380 (RET 8).
    if (NewInstance == -1)
    {
        Instance = -1;
        if (ShipHolder.HasInstance())
            ShipHolder.ReleaseWithAnim();
        return;
    }
    Instance = NewInstance;
    const InstanceIndex Handle = NewInstance;

    // The instance's sphere centre into world space and back into its own transform's frame.
    Transform Xform;
    memcpy(&Xform, DALib::Engine->GetInstanceTransform(Handle), sizeof(Xform));
    float Radius = 0.0f;
    Vector Centre;
    DALib::Engine->GetInstanceExtent(Handle, 0, &Radius, &Centre);
    // The transform is fetched again, and the point goes through HUD_Target_Base's math engine
    // (DAT_00674b1c), not this file's.
    Vector World;
    HudTransformPoint(
        DALib::Engine->GetInstanceTransform(Handle), &World,
        &Centre);
    Vector Local;
    HudInverseTransformPoint(&Xform, &Local, &World);

    // The root sphere as a box (retail recomputes Local between the two halves), then every
    // child folded in.
    FHudInstanceBounds Bounds;
    Bounds.Min.x = Local.x - Radius;
    Bounds.Min.y = Local.y - Radius;
    Bounds.Min.z = Local.z - Radius;
    HudInverseTransformPoint(&Xform, &Local, &World);
    Bounds.Max.x = Local.x + Radius;
    Bounds.Max.y = Local.y + Radius;
    Bounds.Max.z = Local.z + Radius;
    Bounds.ExpandChildren(Handle, &Xform, true);

    // The centre, then half the wider of x and z (the half-diagonal is computed and discarded,
    // as retail).
    Center[0] = (Bounds.Max.x + Bounds.Min.x) * 0.5f;
    Center[1] = (Bounds.Max.y + Bounds.Min.y) * 0.5f;
    Center[2] = (Bounds.Max.z + Bounds.Min.z) * 0.5f;
    float const HalfX = Bounds.Max.x - Center[0];
    float const HalfY = Bounds.Max.y - Center[1];
    float const HalfZ = Bounds.Max.z - Center[2];
    Distance = sqrtf(HalfX * HalfX + HalfY * HalfY + HalfZ * HalfZ);
    float const SpanX = Bounds.Max.x - Bounds.Min.x;
    float const SpanZ = Bounds.Max.z - Bounds.Min.z;
    Distance = static_cast<float>(static_cast<double>(SpanX > SpanZ ? SpanX : SpanZ) * 0.5);

    // Plus the length of the extent centre; 14 when fixed.
    float ExtentRadius = Distance;
    Vector ExtentCentre;
    DALib::Engine->GetInstanceExtent(Instance, 0, &ExtentRadius, &ExtentCentre);
    Distance = sqrtf(ExtentCentre.y * ExtentCentre.y + ExtentCentre.z * ExtentCentre.z +
                     ExtentCentre.x * ExtentCentre.x) +
               Distance;
    if (bFixedDistance)
        Distance = 14.0f;

    PlaceCamera(1);
    ScreenRect[3] = 0;
    ScreenRect[2] = 0;
    ScreenRect[1] = 0;
    ScreenRect[0] = 0;
}

void ShipViewControl::PlaceCamera(int PlaceMode)
{
    // FUN_00585630 (RET 4).
    if (Instance == -1)
        return;

    // The distance that fits the framed radius into the FOV: cot(fov * pi / 180) * radius, at
    // least 2; twice the radius below 1.
    double const Cot = 1.0 / tan(static_cast<double>(FovY) * static_cast<double>(3.14159265f) *
                                 (1.0 / 180.0));
    float Reach;
    if (Distance < 1.0f)
    {
        Reach = Distance + Distance;
    }
    else
    {
        double const Scaled = Cot * Distance;
        Reach = (Scaled < 2.0) ? 2.0f : static_cast<float>(Scaled);
    }

    Transform Eye;
    Vector Focus;
    if (PlaceMode == 1)
    {
        // The instance transform turned -pi/2 about its x axis, the focus through the untouched
        // engine transform.
        memcpy(&Eye, DALib::Engine->GetInstanceTransform(Instance), sizeof(Eye));
        double const Cos = cos(-1.5707963705062866); // DAT_005d8ec0, a float's pi/2 as double
        double const Sin = sin(-1.5707963705062866);
        for (int Row = 0; Row < 3; ++Row)
        {
            double const M1 = Eye.Rotation.E[Row][1];
            double const M2 = Eye.Rotation.E[Row][2];
            Eye.Rotation.E[Row][1] = static_cast<float>(Sin * M2 + M1 * Cos);
            Eye.Rotation.E[Row][2] = static_cast<float>(Cos * M2 - M1 * Sin);
        }
        Vector CenterVec;
        CenterVec.x = Center[0];
        CenterVec.y = Center[1];
        CenterVec.z = Center[2];
        GetShipViewMath()->TransformXform(
            Focus,
            *DALib::Engine->GetInstanceTransform(Instance),
            CenterVec);
    }
    else if (PlaceMode == 0)
    {
        memcpy(&Eye, DALib::Engine->GetInstanceTransform(Instance), sizeof(Eye));
        Vector CenterVec;
        CenterVec.x = Center[0];
        CenterVec.y = Center[1];
        CenterVec.z = Center[2];
        GetShipViewMath()->TransformXform(Focus, Eye, CenterVec);
    }
    else
    {
        return;
    }

    // Eye = focus + Reach * column 2.
    float const BackX = Eye.Rotation.E[0][2] * Reach;
    float const BackY = Eye.Rotation.E[1][2] * Reach;
    float const BackZ = Eye.Rotation.E[2][2] * Reach;
    Eye.Translation.x = Focus.x + BackX;
    Eye.Translation.y = Focus.y + BackY;
    Eye.Translation.z = Focus.z + BackZ;
    Camera.bViewDirty = 1;                         // +0x3bc
    memcpy(&Camera.World, &Eye, sizeof(Eye));       // +0x38c: World[9] and WorldPos[3]
}

bool ShipViewControl::LoadMesh(char const* Path, char const* MaterialPath, bool bFixedDistance,
                               bool bDpConnect)
{
    // FUN_00585890 (RET 0x10).
    if (Path != nullptr && Path[0] != '\0')
    {
        ShipHolder.ReleaseWithAnim();
        ShipHolder.LoadFromPath(Path, MaterialPath, false);
        // The player ship's damaged parts.
        if (bDpConnect)
            GfShipApplyCollisionDamage(ShipHolder.EngineInstance);
        SetInstance(ShipHolder.EngineInstance, bFixedDistance);

        // Move the ship so that its sphere centre sits at the origin of the framed transform.
        float Radius;
        Vector ExtentCentre;
        DALib::Engine->GetInstanceExtent(Instance, 0, &Radius, &ExtentCentre);
        Transform Framed;
        memcpy(&Framed, DALib::Engine->GetInstanceTransform(Instance), sizeof(Framed));
        Vector Delta;
        Delta.x = ExtentCentre.x - Framed.Translation.x;
        Delta.y = ExtentCentre.y - Framed.Translation.y;
        Delta.z = ExtentCentre.z - Framed.Translation.z;
        Vector Local;
        GetShipViewMath()->TransposeTransform(Local, Framed.Rotation, Delta);
        Transform Placed;
        SetIdentity9(&Placed.Rotation);
        Placed.Translation = Local;
        DALib::Engine->SetInstanceMatrix(ShipHolder.EngineInstance, &Placed);
        DALib::Engine->UpdateInstance(ShipHolder.EngineInstance, 0, nullptr);
    }
    return Instance != -1;
}

bool ShipViewControl::LoadShip(ID_String const* NewShipId, bool bFixedDistance, bool bDpConnect)
{
    // FUN_00585a90 (RET 0xc).
    LoadEquipment(nullptr);
    ShipId.Id = NewShipId->Id;
    Archetype::Ship* const Ship = Archetype::GetShip(ShipId.Id);

    // The ship's material libraries stay referenced while the mesh loads.
    Archetype::FCliMaterialEntry* Entries[4];
    int const EntryCount = Archetype::ResolveMaterialEntryList(
        Archetype::CliObjMaterialPaths(Ship->Companion), Entries, 4);
    LoadMesh(Ship->DaArchetype.Value, 0, bFixedDistance, bDpConnect);
    for (int Index = 0; Index < EntryCount; ++Index)
        Archetype::ReleaseMaterialEntrySlot(&Entries[Index]);
    return true;
}

void ShipViewControl::SelectInstance(long NewSelection, bool bFlag, int Unused)
{
    // FUN_00585bd0 (RET 0xc); the third argument's slot only serves as scratch.
    (void)Unused;
    if (NewSelection == -1)
        return;
    ViewFlags = static_cast<unsigned char>((ViewFlags & ~0x10) | (bFlag ? 0x10 : 0));
    SelInstance = NewSelection;
    float Radius;
    Vector Centre;
    DALib::Engine->GetInstanceExtent(NewSelection, 0, &Radius, &Centre);
    SelRadius = Radius;
    SelExtent = Radius;
    const ArchetypeIndex Arch = DALib::Engine->GetInstanceArchetype(NewSelection);
    if (Arch == -1)
        return;
    float Box[6];
    Box[0] = 0.0f;
    Box[1] = 0.0f;
    Box[2] = 0.0f;
    Box[3] = 0.0f;
    Box[4] = 0.0f;
    Box[5] = 0.0f;
    DALib::Renderer->GetBounds(Arch, 1.0f, Box);
    // Unordered gives the x span.
    float const SpanX = Box[0] - Box[1];
    SelRadius = (Radius < SpanX) ? Radius : SpanX;
    DALib::Engine->ReleaseHandle(Arch);
}

void ShipViewControl::SelectHpClass(int HpClass, Archetype::Equipment const* Weapon,
                                    Archetype::Equipment const* Shield, bool bFlag)
{
    // FUN_00585cd0 (RET 0x10).
    SelHpClass = HpClass;
    SelWeapon = Weapon;
    SelShield = Shield;
    ViewFlags = static_cast<unsigned char>((ViewFlags & 0xcf) | (bFlag ? 0x20 : 0));
    SelInstance = -1;
}

void ShipViewControl::SelectEquipDesc(EquipDesc const* Desc, bool bFlag)
{
    // FUN_00585d20 (RET 8).
    Archetype::Equipment const* Weapon = nullptr;
    Archetype::Equipment const* Shield = nullptr;
    int const HpClass = ShipViewEquipmentHpClass(Desc->ArchId, &Weapon, &Shield);
    SelHpClass = HpClass;
    SelWeapon = Weapon;
    SelShield = Shield;
    ViewFlags = static_cast<unsigned char>((ViewFlags & 0xcf) | (bFlag ? 0x20 : 0));
    SelInstance = -1;
}

void ShipViewControl::SelectGood(unsigned GoodId, bool bFlag)
{
    // FUN_00585d90 (RET 8).
    Archetype::Equipment const* Weapon = nullptr;
    Archetype::Equipment const* Shield = nullptr;
    int HpClass = 0;
    if (GoodId != 0)
    {
        GoodInfo const* const Good = GoodList::find_by_id(GoodId);
        if (Good != nullptr)
            HpClass = ShipViewEquipmentHpClass(Good->ArchId, &Weapon, &Shield);
    }
    SelShield = Shield;
    SelHpClass = HpClass;
    SelWeapon = Weapon;
    ViewFlags = static_cast<unsigned char>((ViewFlags & 0xcf) | (bFlag ? 0x20 : 0));
    SelInstance = -1;
}

char const* ShipViewControl::GetHardpointOfInstance(long ItemInstance) const
{
    // FUN_00585e20 (RET 4).
    if (ItemInstance == -1 || Visuals == nullptr)
        return nullptr;
    for (int Index = 0; Index < Visuals->Count; ++Index)
    {
        if (Visuals->GetInstance(Index) == ItemInstance && Visuals->Summary != nullptr)
            return Visuals->Summary->get_name(Index);
    }
    return nullptr;
}

long ShipViewControl::GetInstanceOnHardpoint(char const* Name) const
{
    // FUN_00585e80 (RET 4).
    if (Visuals == nullptr)
        return -1;
    HardpointSummary const* const Summary = Visuals->Summary;
    if (Summary == nullptr)
        return -1;
    for (int Index = 0; Index < Summary->get_count(); ++Index)
    {
        if (_stricmp(Summary->get_name(Index), Name) == 0) // DACOM stricmp
            return Visuals->GetInstance(Index);
    }
    return -1;
}

char const* ShipViewControl::FindHardpointForEquipDesc(EquipDesc const* Desc,
                                                       bool bFirstMatch) const
{
    // FUN_00585ef0 (RET 8).
    if (Desc == nullptr)
        return nullptr;
    return FindHardpointForGood(Arch2Good(Desc->ArchId), bFirstMatch);
}

char const* ShipViewControl::FindHardpointForGood(unsigned GoodId, bool bFirstMatch) const
{
    // FUN_00585f20 (RET 8).
    if (Visuals == nullptr || GoodId == 0)
        return nullptr;

    Archetype::Equipment const* Weapon = nullptr;
    Archetype::Equipment const* Shield = nullptr;
    GoodInfo const* const Good = GoodList::find_by_id(GoodId);
    int const HpClass =
        (Good != nullptr) ? ShipViewEquipmentHpClass(Good->ArchId, &Weapon, &Shield) : 0;

    Archetype::Ship const* const Ship = Archetype::GetShipByName(ShipId);
    HardpointSummary const* const Summary = Visuals->Summary;
    std::vector<char const*> Names; // the free weapon hardpoints

    int const EntryCount = static_cast<int>(Ship->LegalHps.size());
    for (int EntryIndex = 0; EntryIndex < EntryCount; ++EntryIndex)
    {
        Archetype::LegalHpTypes const& Entry = Ship->LegalHps[EntryIndex];
        int const Type = static_cast<int>(Entry.Type);

        // A weapon whose Gun hp types include this entry's type. Retail calls Gun's accessors on
        // whatever class-4 equipment it got (hp types 4, 0xa, 0xd).
        bool bWeaponMatch = false;
        if (HpClass == HpTypeGun && Weapon != nullptr)
        {
            Archetype::Gun const* const Gun = static_cast<Archetype::Gun const*>(Weapon);
            int const TypeCount = Gun->get_number_of_hp_types();
            for (int TypeIndex = 0; TypeIndex < TypeCount; ++TypeIndex)
            {
                if (Type == static_cast<int>(Gun->get_hp_type_by_index(TypeIndex)))
                {
                    bWeaponMatch = true;
                    break;
                }
            }
        }

        // The same for a shield generator of class 5..8.
        bool bShieldMatch = false;
        if (Shield != nullptr &&
            (HpClass == HpTypeShieldGenerator || HpClass == HpTypeFighterShieldGenerator ||
             HpClass == HpTypeEliteShieldGenerator || HpClass == HpTypeFreighterShieldGenerator))
        {
            Archetype::ShieldGenerator const* const Generator =
                static_cast<Archetype::ShieldGenerator const*>(Shield);
            int const TypeCount = Generator->get_number_of_hp_types();
            for (int TypeIndex = 0; TypeIndex < TypeCount; ++TypeIndex)
            {
                if (Type == static_cast<int>(Generator->get_hp_type_by_index(TypeIndex)))
                {
                    bShieldMatch = true;
                    break;
                }
            }
        }

        if (Type != HpClass && !bWeaponMatch && !bShieldMatch)
            continue;

        // Every legal hardpoint of the entry the summary knows.
        int const HardpointCount = static_cast<int>(Entry.Hardpoints.size());
        for (int HardpointIndex = 0; HardpointIndex < HardpointCount; ++HardpointIndex)
        {
            char const* const LegalName = Entry.Hardpoints[HardpointIndex].Value;
            if (Summary == nullptr)
                continue;
            for (int SummaryIndex = 0; SummaryIndex < Summary->get_count(); ++SummaryIndex)
            {
                char const* const HpName = Summary->get_name(SummaryIndex);
                if (HpName == nullptr || _stricmp(HpName, LegalName) != 0)
                    continue;
                if (bFirstMatch)
                    return HpName;
                if (Visuals->GetInstance(SummaryIndex) != -1)
                    continue;
                if (!bWeaponMatch)
                    return HpName;
                Names.push_back(HpName);
            }
        }

        // An exact hp class match ends the search.
        if (!bWeaponMatch && !bShieldMatch)
            break;
    }

    // One candidate is the answer; among several, the one listed first in HpList.
    int const NameCount = static_cast<int>(Names.size());
    if (NameCount <= 0)
        return nullptr;
    if (NameCount == 1)
        return Names[0];
    int Best = 0;
    int BestRank = 9999;
    int const ListCount = static_cast<int>(HpList.size());
    for (int NameIndex = 0; NameIndex < NameCount; ++NameIndex)
    {
        for (int ListIndex = 0; ListIndex < ListCount; ++ListIndex)
        {
            if (_stricmp(HpList[static_cast<size_t>(ListIndex)].Name.c_str(), Names[NameIndex]) !=
                0)
                continue;
            if (ListIndex < BestRank)
            {
                BestRank = ListIndex;
                Best = NameIndex;
            }
            break;
        }
    }
    return Names[static_cast<size_t>(Best)];
}

void ShipViewControl::ComputeScreenRect(float const* Where, bool bSetCamera)
{
    // FUN_00586310 (RET 8): the corners projected by the Win3D camera and truncated.
    float const BaseX = AttachPos[0] + ViewOffset[0];
    float const BaseY = AttachPos[1] + ViewOffset[1];
    float const BaseZ = AttachPos[2] + ViewOffset[2];
    float const PointX = BaseX + Where[0];
    float const PointY = BaseY + Where[1];
    float const PointZ = BaseZ + Where[2];
    ICamera* const WinCamera = AsCamera(Win3DGetCamera());

    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    Vector Corner;
    Corner.x = PointX - ViewSize[0];
    Corner.y = PointY - ViewSize[1];
    Corner.z = PointZ - ViewSize[2];
    WinCamera->Project(&X, &Y, &Z, &Corner);
    ScreenRect[0] = static_cast<int>(X);
    ScreenRect[1] = static_cast<int>(Y);
    Corner.x = PointX + ViewSize[0];
    Corner.y = PointY + ViewSize[1];
    Corner.z = PointZ + ViewSize[2];
    WinCamera->Project(&X, &Y, &Z, &Corner);
    ScreenRect[2] = static_cast<int>(X);
    ScreenRect[3] = static_cast<int>(Y);

    if (ScreenRect[0] < 0 || ScreenRect[1] < 0 || ScreenRect[2] < 0 || ScreenRect[3] < 0)
    {
        ScreenRect[3] = 0;
        ScreenRect[2] = 0;
        ScreenRect[1] = 0;
        ScreenRect[0] = 0;
        return;
    }
    if (!bSetCamera)
        return;

    Camera.SetViewportEx(ScreenRect, 1);
    // FSpaceCamera::SetNearFar's two stores, inlined.
    Camera.ZNear = 0.1f;
    Camera.ZFar = 10000.0f;
}

intptr_t ShipViewControl::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_005864c0 (RET 0xc).
    if (Msg == kWinPropRenderFlags)
    {
        // Only the low word of A is tested.
        unsigned char const Bit = (static_cast<unsigned short>(A) != 0) ? kShipViewShowWireframe : 0;
        ViewFlags = static_cast<unsigned char>((ViewFlags & ~kShipViewShowWireframe) | Bit);
        return 0;
    }
    if (Msg == kShipViewPropMode)
    {
        Mode = static_cast<int>(A);
        return 0;
    }
    return WinControl::SetProperty(Msg, A, B);
}

bool ShipViewControl::HasHpType(int HpType) const
{
    // FUN_00586510 (RET 4).
    for (size_t Index = 0; Index < HpTypes.size(); ++Index)
    {
        if (HpTypes[Index] == HpType)
            return true;
    }
    return false;
}

void ShipViewControl::CollectHpTypes(char const* HpName)
{
    // FUN_00586560 (RET 4).
    HpTypes.clear();
    if (HpName == nullptr || ShipId.IsEmpty())
        return;
    Archetype::Ship const* const Ship = Archetype::GetShipByName(ShipId);
    int const EntryCount = static_cast<int>(Ship->LegalHps.size());
    for (int EntryIndex = 0; EntryIndex < EntryCount; ++EntryIndex)
    {
        Archetype::LegalHpTypes const& Entry = Ship->LegalHps[EntryIndex];
        int const HardpointCount = static_cast<int>(Entry.Hardpoints.size());
        for (int HardpointIndex = 0; HardpointIndex < HardpointCount; ++HardpointIndex)
        {
            if (_stricmp(HpName, Entry.Hardpoints[HardpointIndex].Value) == 0)
                HpTypes.push_back(static_cast<int>(Entry.Type));
        }
    }
}

bool ShipViewControl::HasHpTypeGroup(int Group) const
{
    // FUN_00586800 (RET 4).
    for (size_t Index = 0; Index < HpTypes.size(); ++Index)
    {
        if (HpTypeGroup(HpTypes[Index]) == Group)
            return true;
    }
    return false;
}

void ShipViewControl::SetViewSize(float const* NewViewSize)
{
    // FUN_005868c0 (RET 4).
    ViewSize[0] = NewViewSize[0];
    ViewSize[1] = NewViewSize[1];
    ViewSize[2] = NewViewSize[2];
}

void ShipViewControl::SetRotSpeed(Vector const* NewRotSpeed)
{
    // FUN_005868e0 (RET 4).
    RotSpeed[0] = NewRotSpeed->x;
    RotSpeed[1] = NewRotSpeed->y;
    RotSpeed[2] = NewRotSpeed->z;
}

void ShipViewControl::SetObjectYpr(Vector const* NewYpr)
{
    // FUN_00586900 (RET 4).
    ObjectYpr[0] = NewYpr->x;
    ObjectYpr[1] = NewYpr->y;
    ObjectYpr[2] = NewYpr->z;
    BuildYprOrientation(&ObjectOrientation, ObjectYpr);
}

void ShipViewControl::SetAttachPosition(Vector const* NewPosition)
{
    // FUN_00586a10 (RET 4).
    AttachPos[0] = NewPosition->x;
    AttachPos[1] = NewPosition->y;
    AttachPos[2] = NewPosition->z;
}

void ShipViewControl::SetShowRect(bool bShow)
{
    // FUN_00586a30 (RET 4).
    ViewFlags = static_cast<unsigned char>((ViewFlags & ~kShipViewShowRect) | (bShow ? kShipViewShowRect : 0));
}

void ShipViewControl::SetShowWireframe(bool bShow)
{
    // FUN_00586a50 (RET 4).
    ViewFlags = static_cast<unsigned char>((ViewFlags & ~kShipViewShowWireframe) | (bShow ? kShipViewShowWireframe : 0));
}

void ShipViewControl::SetRectColor(float const* Rgba)
{
    // FUN_00586a70 (RET 4): converted 3, 2, 1, 0, as retail.
    unsigned char const B3 = ColorByte(Rgba[3]);
    unsigned char const B2 = ColorByte(Rgba[2]);
    unsigned char const B1 = ColorByte(Rgba[1]);
    unsigned char const B0 = ColorByte(Rgba[0]);
    RectColor[0] = B0;
    RectColor[3] = B3;
    RectColor[1] = B1;
    RectColor[2] = B2;
}

void ShipViewControl::SetWireColor(float const* Rgba)
{
    // FUN_00586af0 (RET 4).
    unsigned char const B3 = ColorByte(Rgba[3]);
    unsigned char const B2 = ColorByte(Rgba[2]);
    unsigned char const B1 = ColorByte(Rgba[1]);
    unsigned char const B0 = ColorByte(Rgba[0]);
    WireColor[0] = B0;
    WireColor[3] = B3;
    WireColor[1] = B1;
    WireColor[2] = B2;
}

void ShipViewControl::BuildHardpointList()
{
    // FUN_00586b70.
    HpList.clear();
    if (Visuals == nullptr)
        return;

    // Per hardpoint the highest hp type (by value) the caption table lists among its legal
    // types.
    HardpointSummary const* const Summary = Visuals->Summary;
    std::vector<FShipViewHpEntry> Collected;
    if (Summary != nullptr)
    {
        for (int Index = 0; Index < Summary->get_count(); ++Index)
        {
            Summary->get_info(Index); // the result is not used
            char const* const Name = Summary->get_name(Index);
            CollectHpTypes(Name);
            if (HpTypes.empty())
                continue;
            int BestRow = -1;
            int BestType = 0;
            for (int Row = 0; Row < 0x3c; ++Row)
            {
                int const RowType = G_ShipViewHpTypeCaptions[Row].HpType;
                for (size_t TypeIndex = 0; TypeIndex < HpTypes.size(); ++TypeIndex)
                {
                    if (RowType == HpTypes[TypeIndex] && RowType > BestType)
                    {
                        BestType = RowType;
                        BestRow = Row;
                    }
                }
            }
            if (BestRow < 0)
                continue;
            FShipViewHpEntry Entry;
            Entry.Ids = G_ShipViewHpTypeCaptions[BestRow].Ids;
            Entry.Name = Name;
            Entry.SortKey = BestRow;
            Entry.Group = HpTypeGroup(BestType);
            Entry.Class = ShipViewHpTypeClass(BestType);
            Collected.push_back(Entry);
        }
    }

    // An index list bubble-sorted by row descending, then by name descending.
    std::vector<int> Order;
    int const CollectedCount = static_cast<int>(Collected.size());
    for (int Index = 0; Index < CollectedCount; ++Index)
        Order.push_back(Index);
    int const Last = CollectedCount - 1;
    bool bSorted = true;
    do
    {
        if (Last <= 0)
            break;
        bSorted = true;
        for (int Index = 0; Index < Last; ++Index)
        {
            FShipViewHpEntry const& Left = Collected[static_cast<size_t>(Order[Index])];
            FShipViewHpEntry const& Right = Collected[static_cast<size_t>(Order[Index + 1])];
            bool bSwap = false;
            if (Left.SortKey < Right.SortKey)
                bSwap = true;
            else if (Left.SortKey == Right.SortKey &&
                     _stricmp(Left.Name.c_str(), Right.Name.c_str()) < 0)
                bSwap = true;
            if (!bSwap)
                continue;

            int const Swapped = Order[Index];
            Order[Index] = Order[Index + 1];
            Order[Index + 1] = Swapped;
            bSorted = false;
        }
    } while (!bSorted);

    // Stored from the back of the sorted order (ascending), each copy with SortKey 0.
    for (int Index = Last; Index >= 0; --Index)
    {
        FShipViewHpEntry const& Source = Collected[static_cast<size_t>(Order[Index])];
        FShipViewHpEntry Copy;
        Copy.Ids = Source.Ids;
        Copy.Name = Source.Name;
        Copy.SortKey = 0;
        Copy.Group = Source.Group;
        Copy.Class = Source.Class;
        HpList.push_back(Copy);
    }
}

int ShipViewControl::GetHardpointListCount() const
{
    // FUN_00587050.
    return static_cast<int>(HpList.size());
}

FShipViewHpEntry const* ShipViewControl::GetHardpointListEntry(int Index) const
{
    // FUN_00587070 (RET 4).
    if (Index < 0 || static_cast<size_t>(Index) >= HpList.size())
        return nullptr;
    return &HpList[static_cast<size_t>(Index)];
}

ShipViewControl* WinControl::CreateShipViewControl(char const* Name, Vector const* RotSpeed,
                                                   Vector const* NewOffset, Vector const* Ypr,
                                                   Vector const* NewViewSize, char const* Hardpoint,
                                                   bool bShowRect, bool bShowWire,
                                                   unsigned const* RectColor,
                                                   unsigned const* WireColor)
{
    // FUN_005a1020 (RET 0x28).
    Win3D* const Created = WinFactoryCreate(Name, "ShipViewControl", this, nullptr);
    if (Created != nullptr)
    {
        static_cast<ShipViewControl*>(Created)->Init(RotSpeed, NewOffset, Ypr, NewViewSize,
                                                     Hardpoint, bShowRect, bShowWire, RectColor,
                                                     WireColor);
    }
    return static_cast<ShipViewControl*>(Created);
}
