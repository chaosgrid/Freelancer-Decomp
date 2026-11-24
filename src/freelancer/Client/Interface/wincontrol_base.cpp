// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/wincontrol_base.h"
#include "Space/HUD/hud_widescreen.h"   // port-only (FREELANCER_WIDESCREEN_SUPPORT)

#include "common_ini.h"
#include "Core/game_options.h"
#include "NeuroNet/nn_tooltip.h"
#include "NeuroNet/nn_ui_pulse.h"
#include "Interface/win3d_engine_submit.h"
#include "Interface/win3d_immediate.h"
#include "Interface/win_factory.h"

#include "DALib.h"
#include "dacom.h"

#include <string.h>

namespace
{
    // DAT_005c7f68 -- 1/255 (0x3b808081).
    float const kInv255 = 1.0f / 255.0f;

    void SetIdentity3x3(float* M)
    {
        M[0] = 1.0f;
        M[1] = 0.0f;
        M[2] = 0.0f;
        M[3] = 0.0f;
        M[4] = 1.0f;
        M[5] = 0.0f;
        M[6] = 0.0f;
        M[7] = 0.0f;
        M[8] = 1.0f;
    }

    // DALib::Engine vt+0x8c SetInstancePosition then vt+0x6c UpdateInstance(inst, 0, 0), the
    // pair every WinControl slot below issues after moving its instance.
    void SetInstancePositionAndUpdate(void* Instance, float* Position)
    {
        DALib::Engine->SetInstancePosition(Instance, Position);
        DALib::Engine->UpdateInstance(Instance, 0, nullptr);
    }

    bool HasInstance(WinControl const* Control)
    {
        // Several slots inline this test instead of calling FUN_00420d60.
        return Control->MeshHolder.EngineInstance !=
               reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    }
}

WinControl::WinControl()
{
    // FUN_0059f860 (Win3D base ctor FUN_004b6790; holder ctor FUN_00420d40). Retail interleaves
    // the field stores.
    ClientMeshHolderConstruct(&MeshHolder);
    MeshHolder.UserFloats[3] = 1.0f;
    MeshHolder.UserFloats[2] = 1.0f;
    MeshHolder.UserFloats[1] = 1.0f;
    MeshHolder.UserFloats[0] = 1.0f;
    MeshHolder.UserFloats[4] = 1.0f;
    MeshHolder.AnimOrArchIndex = -1;

    SetIdentity3x3(AttachOrientation);
    AttachPosition[2] = 0.0f;
    AttachPosition[1] = 0.0f;
    AttachPosition[0] = 0.0f;

    Offset[0] = 0.0f;
    Offset[1] = 0.0f;
    Offset[2] = 0.0f;
    HalfExtents[0] = 0.0f;
    HalfExtents[1] = 0.0f;
    HalfExtents[2] = 0.0f;

    bCaptured = 0;
    bNoZEnable = 0;
    ToolTipIds = 0;
    UserData = 0;
    OwnerData = 0;
    PositionFlags = static_cast<unsigned char>(PositionFlags | 1);
    bIdentityHitOrientation = 1;
    HitOffset[0] = 0.0f;
    HitOffset[1] = 0.0f;
    HitOffset[2] = 0.0f;
    RequestedPosition[0] = 0.0f;
    RequestedPosition[1] = 0.0f;
    RequestedPosition[2] = 10000.0f; // 0x461c4000
    bAttached = 0;
    MeshPath[0] = '\0';
    HardpointName[0] = '\0';
    Field2d2 = 0;
}

WinControl::~WinControl()
{
    // FUN_0059fa50.
    if (bCaptured != 0)
    {
        bCaptured = 0;
        Win3DRemoveCapture(this);
    }
    ClientUiMeshHolderRelease(&MeshHolder);

    // Retail also dynamic_casts each child to WinControl here and discards the result.
    Win3D* Child = ChildHead;
    while (Child != nullptr)
    {
        Child->Destroy();
        delete Child;
        Child = ChildHead;
    }

    Flags = static_cast<unsigned char>(Flags & 0xfc);
    for (Win3D* Rest = ChildHead; Rest != nullptr; Rest = Rest->NextSibling)
        Rest->Destroy();
    Win3DUnregister(this);

    // Then ~FClientMeshHolder (FUN_0059ef20) and the Win3D dtor FUN_004b4fd0.
}

WinControl* WinControl::AsWinControl()
{
    // FUN_0043d950.
    return this;
}

bool WinControl::HitTest(float const* Ray)
{
    // FUN_005a0340.
    float Transform[12];
    SetIdentity3x3(Transform);
    Transform[9] = 0.0f;
    Transform[10] = 0.0f;
    Transform[11] = 0.0f;
    if (ClientMeshHolderHasInstance(&MeshHolder))
    {
        float const* const Source = DALib::Engine->GetInstanceTransform(MeshHolder.EngineInstance);
        memcpy(Transform, Source, sizeof(Transform));
    }
    else
    {
        Transform[9] = Position[0] + Offset[0];
        Transform[10] = Position[1] + Offset[1];
        Transform[11] = Position[2] + Offset[2];
    }
    Transform[9] = Transform[9] + HitOffset[0];
    Transform[10] = Transform[10] + HitOffset[1];
    Transform[11] = Transform[11] + HitOffset[2];
    if (bIdentityHitOrientation != 0)
        SetIdentity3x3(Transform);

    bool const bHit = Win3DRayHitsAabbHalfExtents(HalfExtents, Transform, Ray);
    if (!bHit)
        return bHit;

    // The parent hears property 0x25, then 0x4f when a tooltip id is set; a non-zero answer to
    // 0x4f suppresses this control's own tooltip.
    if (Parent != nullptr)
    {
        WinControl* const ParentControl = Parent->AsWinControl();
        if (ParentControl != nullptr)
        {
            ParentControl->SetProperty(0x25, reinterpret_cast<uintptr_t>(AsWinControl()), 0);
            if (ToolTipIds == 0)
                return bHit;
            if (ParentControl->SetProperty(0x4f, reinterpret_cast<uintptr_t>(AsWinControl()),
                                           0) != 0)
                return bHit;
        }
    }

    if (ToolTipIds != 0 && (Flags & kWin3DActive) != 0 && (Flags & kWin3DVisible) != 0)
        ShowToolTip();
    return bHit;
}

void WinControl::SetPosition(float const* NewPosition)
{
    // FUN_005a0880.
    Position[0] = NewPosition[0];
    Position[1] = NewPosition[1];
    Position[2] = NewPosition[2];
    if (HasInstance(this))
    {
        float Placed[3];
        if ((PositionFlags & 1) != 0)
        {
            Placed[0] = Offset[0] + Position[0];
            Placed[1] = Position[1] + Offset[1];
            Placed[2] = Position[2] + Offset[2];
        }
        else
        {
            Placed[0] = Position[0];
            Placed[1] = Position[1];
            Placed[2] = Position[2];
        }
        SetInstancePositionAndUpdate(MeshHolder.EngineInstance, Placed);
    }

    for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
        Child->SetPosition(Position);
}

void WinControl::SetOrientation(float const* NewOrientation)
{
    // FUN_005a0950.
    memmove(Orientation, NewOrientation, sizeof(Orientation));
    if (ClientMeshHolderHasInstance(&MeshHolder))
    {
        DALib::Engine->SetInstanceOrientation(MeshHolder.EngineInstance,
                                              const_cast<float*>(NewOrientation));
        DALib::Engine->UpdateInstance(MeshHolder.EngineInstance, 0, nullptr);
    }
    for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
        Child->SetOrientation(NewOrientation);
}

void WinControl::Destroy()
{
    // FUN_0059fb60.
    ClientUiMeshHolderRelease(&MeshHolder);
    Win3D* Child = ChildHead;
    while (Child != nullptr)
    {
        Child->Destroy();
        delete Child;
        Child = ChildHead;
    }
    Flags = static_cast<unsigned char>(Flags & 0xfc);
    for (Win3D* Rest = ChildHead; Rest != nullptr; Rest = Rest->NextSibling)
        Rest->Destroy();
    Win3DUnregister(this);
}

void WinControl::Draw()
{
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only: owners that walk their children themselves (HudTargetBase, NN_MainButton)
    // reach this without the Win3D draw walk, so the control sets its own HUD shift.
    FWidescreenHudShiftScope const WidescreenScope(WidescreenHudShiftPixelsFor(this), true);
#endif
    // FUN_005a0760 (FUN_005a06a0 inlined).
    DrawStaticMesh();
    Win3DDrawChildren(this);
    PostDraw();
}

void WinControl::DrawStaticMesh()
{
    // FUN_005a06a0. The tint is the HUD chrome colour DAT_00679bb4.
    if (GetGameOption(kGameOptDrawStaticControls) != 0 && ClientMeshHolderHasInstance(&MeshHolder))
    {
        Win3DDisable(kWin3DDepthTest);
        MeshHolder.UserFloats[0] =
            static_cast<float>(static_cast<double>(G_HudChromeColorRgba[0]) * kInv255);
        MeshHolder.UserFloats[1] =
            static_cast<float>(static_cast<double>(G_HudChromeColorRgba[1]) * kInv255);
        MeshHolder.UserFloats[2] =
            static_cast<float>(static_cast<double>(G_HudChromeColorRgba[2]) * kInv255);
        MeshHolder.UserFloats[4] =
            static_cast<float>(static_cast<double>(G_HudChromeColorRgba[3]) * kInv255);
        SubmitWin3DEngineInstance(&MeshHolder, Win3DGetCamera(), 0, 1.0f, 0);
        Win3DDisable(kWin3DDepthTest);
    }
}

void WinControl::SetHiddenWithChildren(bool bHide)
{
    // FUN_005a0c30.
    for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
    {
        WinControl* const AsControl = Child->AsWinControl();
        if (AsControl != nullptr)
        {
            AsControl->SetProperty(1, bHide ? 1u : 0u, 0);
        }
    }
    if (bHide)
        Flags = static_cast<unsigned char>(Flags & 0xfc);
    else
        Flags = static_cast<unsigned char>(Flags | 3);
}

bool WinControl::MouseEvent(FWin3DMouseEvent const* Event)
{
    // FUN_005a0650: a capturing control takes every left-button event; otherwise the event
    // is taken when the cached mouse ray hits this control.
    if (bCaptured != 0 && Event->Button == kWin3DMouseLeft)
        return true;
    float Ray[3];
    return HitTest(Win3DGetMouseRay(Ray));
}

bool WinControl::ProcessMessage(unsigned Msg, unsigned /*A*/, unsigned /*B*/)
{
    // FUN_005a0840.
    if (Msg == 0xef01)
    {
        if (Parent != nullptr)
            SetPosition(Parent->Position);
        Flags = static_cast<unsigned char>(Flags | kWin3DActive | kWin3DVisible);
        return true;
    }
    if (Msg == 0xef02)
    {
        Flags = static_cast<unsigned char>(Flags & 0xfc);
        return true;
    }
    return false;
}

void WinControl::DataViewSlot0(unsigned /*A*/, unsigned /*B*/)
{
    // LAB_0053aaa0.
}

void WinControl::DataViewSlot1(unsigned /*A*/)
{
    // FUN_0053e4a0.
}

void WinControl::LoadIni(char const* Path)
{
    // LAB_0059fd10.
    INI_Reader Ini;
    if (!Ini.open(Path, false) || !Ini.find_header("BaseFrame") || !Ini.is_header("BaseFrame"))
        return;

    while (Ini.read_value())
    {
        if (Ini.is_value("behavior"))
            continue;
        if (Ini.is_value("mesh"))
        {
            SetMesh(Ini.get_value_string(), 0);
            continue;
        }
        if (Ini.is_value("offset"))
        {
            float const Y = Ini.get_value_float(1);
            float const X = Ini.get_value_float(0);
            float const Z = Ini.get_value_float(2);
            Offset[0] = X;
            Offset[1] = Y;
            Offset[2] = Z;
            if (ClientMeshHolderHasInstance(&MeshHolder))
            {
                float Placed[3];
                Placed[2] = Offset[2] + Position[2];
                Placed[1] = Offset[1] + Position[1];
                Placed[0] = Offset[0] + Position[0];
                SetInstancePositionAndUpdate(MeshHolder.EngineInstance, Placed);
            }
            continue;
        }
        if (Ini.is_value("subcontrol"))
            continue;
        if (Ini.is_value("mouse_size"))
        {
            float const Y = Ini.get_value_float(1);
            float const X = Ini.get_value_float(0);
            float const Z = Ini.get_value_float(2);
            HalfExtents[0] = X;
            HalfExtents[1] = Y;
            HalfExtents[2] = Z;
            continue;
        }
        if (Ini.is_value("no_z_enable"))
        {
            bNoZEnable = (Ini.get_value_float(0) != 0.0) ? 1 : 0;
            continue;
        }
        if (Ini.is_value("z_order"))
        {
            DrawLayer = static_cast<unsigned>(static_cast<int>(Ini.get_value_float(0)));
            continue;
        }
        if (Ini.is_value("tooltip"))
            ToolTipIds = static_cast<int>(Ini.get_value_float(0));
    }

    // Every further section names a child; its class_name value picks the factory.
    while (Ini.read_header())
    {
        char SectionName[0x100];
        strcpy(SectionName, Ini.get_header_ptr());
        while (Ini.read_value())
        {
            if (Ini.is_value("class_name"))
            {
                WinFactoryCreate(SectionName, Ini.get_value_string(), this, &Ini);
                break;
            }
        }
    }
}

void WinControl::SaveIni(FILE* /*File*/)
{
    // FUN_0053e4a0.
}

void WinControl::Slot30(unsigned /*A*/)
{
    // FUN_0053e4a0.
}

void WinControl::SetOffset(float X, float Y, float Z)
{
    // FUN_005a0250.
    float Delta[3];
    Delta[0] = X - Offset[0];
    Delta[1] = Y - Offset[1];
    Delta[2] = Z - Offset[2];
    Offset[0] = X;
    Offset[1] = Y;
    Offset[2] = Z;
    for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
    {
        WinControl* const ChildControl = Child->AsWinControl();
        if (ChildControl != nullptr)
            ChildControl->AddAttachOffset(Delta);
    }
    if (Parent != nullptr)
        SetPosition(Parent->Position);
    else
        SetPosition(Position);
}

void WinControl::CaptureMouse()
{
    // FUN_005a0300.
    if (bCaptured != 0)
        return;
    bCaptured = 1;
    Win3DPushCapture(this);
}

void WinControl::ReleaseMouse()
{
    // FUN_005a0320.
    if (bCaptured == 0)
        return;
    bCaptured = 0;
    Win3DRemoveCapture(this);
}

void WinControl::ShowToolTip()
{
    // FUN_005a0530: the hover record's key (+0x00, compared by FUN_004bdea0 against the
    // ToolTip's +0xf4) is this control. Retail leaves the world position / half extents
    // (+0x48..+0x5f) as stack garbage; the port zeroes them.
    FToolTipHoverInfo Info;
    Info.AuxField = 0;                // +0x44
    Info.bLive = 0;                   // +0x60
    Info.Ids = static_cast<int>(reinterpret_cast<intptr_t>(this)); // +0x00
    Info.IdsAlt = ToolTipIds;         // +0x04 <- +0xcc
    NnToolTipFeedHover(&Info);        // FUN_004be370
}

void WinControl::RefreshAttachment()
{
    // FUN_005a09d0: last, the PARENT (not this) is re-placed at its own position, which moves
    // this control along with its siblings.
    for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
    {
        WinControl* const ChildControl = Child->AsWinControl();
        if (ChildControl != nullptr)
            ChildControl->RefreshAttachment();
    }
    if (Parent != nullptr)
    {
        SetIdentity3x3(AttachOrientation);
        AttachPosition[2] = 0.0f;
        AttachPosition[1] = 0.0f;
        AttachPosition[0] = 0.0f;
        WinControl* const ParentControl = Parent->AsWinControl();
        if (ParentControl != nullptr)
            bAttached = ParentControl->GetHardpointTransform(HardpointName, AttachOrientation)
                            ? 1
                            : 0;
    }
    if (Parent != nullptr)
        Parent->SetPosition(Parent->Position);
}

bool WinControl::GetHardpointTransform(char const* Name, float* OutTransform12)
{
    // FUN_005a0a70 (RET 8). OutTransform12 is 3x3 orientation then position; the position
    // always has +0xa8 added.
    if (ClientMeshHolderHasInstance(&MeshHolder))
    {
        void* const Archetype = DALib::Engine->GetInstanceArchetype(MeshHolder.EngineInstance);
        DALib::Engine->ReleaseHandle(Archetype);
        FHardpointKey Key;
        Key.Name = Name;
        Key.Crc = DACOM_CRC::GetCRC32(Name);
        unsigned char Blob[0x48];
        if (static_cast<unsigned char>(DALib::Hardpoint->GetHardpoint(Archetype, &Key, Blob)) != 0)
        {
            // Blob +0x04 position, +0x10 orientation.
            float Local[12];
            memcpy(Local, Blob + 0x10, 9 * sizeof(float));
            memcpy(Local + 9, Blob + 0x04, 3 * sizeof(float));
            memcpy(OutTransform12, Local, sizeof(Local));
            OutTransform12[9] = Offset[0] + OutTransform12[9];
            OutTransform12[10] = Offset[1] + OutTransform12[10];
            OutTransform12[11] = Offset[2] + OutTransform12[11];
            return true;
        }
    }
    SetIdentity3x3(OutTransform12);
    OutTransform12[11] = 0.0f;
    OutTransform12[10] = 0.0f;
    OutTransform12[9] = 0.0f;
    OutTransform12[9] = Offset[0] + OutTransform12[9];
    OutTransform12[10] = Offset[1] + OutTransform12[10];
    OutTransform12[11] = Offset[2] + OutTransform12[11];
    return false;
}

WinControl* WinControl::FindChild(char const* ChildName)
{
    // FUN_005a0bc0.
    for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
    {
        if (strcmp(Child->Name, ChildName) == 0)
            return Child->AsWinControl();
    }
    return nullptr;
}

void WinControl::SetMesh(char const* Path, char const* MaterialPath)
{
    // FUN_0059fbd0 (RET 8). A bound instance of the same path (stricmp) is left alone.
    if (HasInstance(this) && _stricmp(MeshPath, Path) == 0)
        return;
    strcpy(MeshPath, Path);
    ClientUiMeshHolderRelease(&MeshHolder);
    if (Path[0] != '\0')
        ClientMeshLoadFromPath(&MeshHolder, Path, MaterialPath, false);
    if (HasInstance(this))
    {
        float Matrix[12];
        SetIdentity3x3(Matrix);
        Matrix[9] = Offset[0] + Position[0];
        Matrix[10] = Offset[1] + Position[1];
        Matrix[11] = Offset[2] + Position[2];
        DALib::Engine->SetInstanceMatrix(MeshHolder.EngineInstance, Matrix);
        DALib::Engine->UpdateInstance(MeshHolder.EngineInstance, 0, nullptr);
    }
    RefreshAttachment();
}

void WinControl::PostDraw()
{
    // FUN_00401e90.
}

void WinControl::GetAttachOrientation(float* OutOrientation9)
{
    // FUN_005a1670.
    if (Parent != nullptr && bAttached != 0 && Parent->AsWinControl() != nullptr)
    {
        memcpy(OutOrientation9, AttachOrientation, sizeof(AttachOrientation));
        return;
    }
    float Identity[9];
    SetIdentity3x3(Identity);
    memcpy(OutOrientation9, Identity, sizeof(Identity));
}

void WinControl::GetAttachPosition(float* OutPosition3)
{
    // FUN_005a1750.
    if (Parent != nullptr && bAttached != 0 && Parent->AsWinControl() != nullptr)
    {
        OutPosition3[0] = AttachPosition[0];
        OutPosition3[1] = AttachPosition[1];
        OutPosition3[2] = AttachPosition[2];
        return;
    }
    OutPosition3[0] = 0.0f;
    OutPosition3[1] = 0.0f;
    OutPosition3[2] = 0.0f;
}

intptr_t WinControl::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // FUN_005a13c0 (RET 0xc), jump table 0x005a1638 over Msg - 1.
    switch (Msg)
    {
    case kWinPropHide:
        if (A == 1)
            Flags = static_cast<unsigned char>(Flags & 0xfc);
        else
            Flags = static_cast<unsigned char>(Flags | kWin3DActive | kWin3DVisible);
        return 1;
    case kWinPropDisable:
        Flags = static_cast<unsigned char>((Flags & 0xfe) | (A == 0 ? 1 : 0));
        return 1;
    case kWinPropToolTip:
        ToolTipIds = static_cast<int>(A);
        return 1;
    case kWinPropMesh:
        SetMesh(reinterpret_cast<char const*>(A), reinterpret_cast<char const*>(B));
        return 1;
    case kWinPropPosition:
        SetPosition(reinterpret_cast<float const*>(A));
        return 1;
    case kWinPropOffset:
    {
        float const* const NewOffset = reinterpret_cast<float const*>(A);
        Offset[0] = NewOffset[0];
        Offset[1] = NewOffset[1];
        Offset[2] = NewOffset[2];
        if (HasInstance(this))
        {
            float Placed[3];
            if ((PositionFlags & 1) != 0)
            {
                Placed[0] = Offset[0] + Position[0];
                Placed[1] = Offset[1] + Position[1];
                Placed[2] = Offset[2] + Position[2];
            }
            else
            {
                Placed[0] = Position[0];
                Placed[1] = Position[1];
                Placed[2] = Position[2];
            }
            SetInstancePositionAndUpdate(MeshHolder.EngineInstance, Placed);
        }
        return 0;
    }
    case kWinPropGetOffset:
    {
        float* const Out = reinterpret_cast<float*>(A);
        Out[0] = Offset[0];
        Out[1] = Offset[1];
        Out[2] = Offset[2];
        return 0;
    }
    case kWinPropOrientation:
        if (HasInstance(this))
        {
            float Matrix[12];
            memcpy(Matrix, reinterpret_cast<float const*>(A), 9 * sizeof(float));
            Matrix[9] = Position[0] + Offset[0];
            Matrix[10] = Position[1] + Offset[1];
            Matrix[11] = Position[2] + Offset[2];
            DALib::Engine->SetInstanceMatrix(MeshHolder.EngineInstance, Matrix);
            DALib::Engine->UpdateInstance(MeshHolder.EngineInstance, 0, nullptr);
        }
        return 1;
    case kWinPropFlag2d2:
        Field2d2 = (A != 0) ? 1 : 0;
        return 1;
    case kWinPropGetInstance:
        return reinterpret_cast<intptr_t>(MeshHolder.EngineInstance);
    case kWinPropGetMeshPath:
        strncpy(reinterpret_cast<char*>(A), MeshPath, static_cast<size_t>(B));
        return 0;
    default:
        return 0;
    }
}

void WinControl::AddAttachOffset(float const* Delta)
{
    // FUN_005a1380.
    AttachPosition[0] = AttachPosition[0] + Delta[0];
    AttachPosition[1] = Delta[1] + AttachPosition[1];
    AttachPosition[2] = Delta[2] + AttachPosition[2];
}

bool WinControl::NameEquals(char const* OtherName)
{
    // FUN_00585090.
    return strcmp(Name, OtherName) == 0;
}

// Retail DAT_0067dd14 -- WINFACTORY<WinControl> static (initializer LAB_0059f830, atexit
// LAB_0059f850; Create FUN_005a1a20).
WINFACTORY<WinControl> G_WinControlFactory("WinControl");
