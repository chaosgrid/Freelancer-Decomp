// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "NeuroNet/nn_button_control.h"
#include "Space/HUD/hud_widescreen.h"   // port-only (FREELANCER_WIDESCREEN_SUPPORT)

#include "Core/game_options.h"         // GetGameOption (FUN_005792b0)
#include "Core/globals.h"              // GetClientFrameTimeCapped (FUN_0042d680)
#include "Space/HUD/hud_command_dispatch.h" // HudPostCommand (FUN_005763b0)
#include "Common/input_manager.h"        // GetClientSoftwareCursor (DAT_00616840 / DAT_00616844)
#include "Common/Audio/menu_sound_arch.h"      // LookupClientSoundArch / PlayClientSoundArchIfIdle
#include "Interface/menu_ui_init.h"         // FindUiSoundSlot / PlayOrRewindUiSoundSlot / PlayUiRolloverSound
#include "NeuroNet/nn_montage_draw.h"      // ButtonMontage tiles, NnMontageDrawTileAt (FUN_00559900)
#include "NeuroNet/nn_tooltip.h"           // NnToolTipHideGlobal (FUN_004be390)
#include "NeuroNet/nn_ui_pulse.h"          // NnUiPulseReset / NnUiPulseBlendArgb / palette
#include "Space/Cameras/space_camera.h"         // FSpaceCamera (DAT_0067dbf8)
#include "Space/Cameras/space_camera_third.h"   // CockpitMulMatrix (FUN_0051a140)
#include "Interface/tooltip_hover.h"        // FToolTipHoverInfo / NnToolTipFeedHover (FUN_004be370)
#include "Interface/win3d.h"
#include "Interface/win3d_engine_submit.h"  // SubmitWin3DEngineInstance / ..TintedEngineInstanceWrapped
#include "Interface/win3d_immediate.h"      // Win3DDisable (FUN_00421ed0)
#include "Interface/win_factory.h"

#include "common_csys.h"          // RotateMatrix, Acquire3DMathEngine (FUN_005b73e0)
#include "common_id.h"            // CreateID
#include "common_ini.h"           // INI_Reader
#include "x86math.h"

#include "DALib.h"

#include <math.h>
#include <string.h>

// Retail DAT_00679aac -- WINFACTORY<ButtonControl> (static init LAB_005597a0: FUN_0055d240
// "ButtonControl" + atexit LAB_005597c0 -> FUN_00579380); Create FUN_0055d2b0.
WINFACTORY<NnButtonControl> G_ButtonControlFactory("ButtonControl");

// Retail DAT_00679a80 -- WINFACTORY<ToggleButtonControl> (static init LAB_005597d0:
// FUN_0055d350 "ToggleButtonControl" + atexit LAB_005597f0 -> FUN_00579380); Create
// FUN_0055d3c0.
WINFACTORY<NnToggleButtonControl> G_ToggleButtonControlFactory("ToggleButtonControl");

// Retail DAT_00679a54 -- WINFACTORY<RichTextButtonControl> (static init LAB_00559800:
// FUN_0055d4e0 "RichTextButtonControl" + atexit LAB_00559820 -> FUN_00579380); Create
// FUN_0055d550.
WINFACTORY<NnRichTextButtonControl> G_RichTextButtonControlFactory("RichTextButtonControl");

namespace
{
    // DAT_00679aec -- the 3DMathEngine this class acquires on first use (FUN_005b73e0), shared
    // by SetOrientation and MouseEvent.
    I3DMathEngine* G_ButtonMathEngine = nullptr;

    I3DMathEngine* ButtonMathEngine()
    {
        if (G_ButtonMathEngine == nullptr)
            G_ButtonMathEngine = Acquire3DMathEngine();
        return G_ButtonMathEngine;
    }

    // DAT_005c75dc
    float const kOne = 1.0f;
    // DAT_005c7f68 -- 1/255 (0x3b808081)
    float const kInv255 = 1.0f / 255.0f;
    // 0x3cb43958 -- FUN_00559aa0's icon size without a tile scale
    float const kDefaultIconSize = 0.022f;
    // DAT_005e1514 -- 0x3fb504f3, sqrt(2)
    float const kSqrt2 = 1.41421354f;

    bool HasInstance(WinControl const* Control)
    {
        // The `CMP [ESI+0x84],-1` every slot below inlines.
        return Control->MeshHolder.EngineInstance !=
               InvalidInstanceIndex;
    }

    // The parent notification the handlers spell out: the parent as a WinControl (slot 2),
    // and if it is one, its slot 42 with (Msg, this as a WinControl, 0).
    intptr_t TellParent(WinControl* Self, int Msg)
    {
        Win3D* const Parent = Self->GetParent();
        if (Parent == nullptr)
            return 0;
        WinControl* const ParentControl = Parent->AsWinControl();
        if (ParentControl == nullptr)
            return 0;
        return ParentControl->SetProperty(Msg, reinterpret_cast<uintptr_t>(Self->AsWinControl()),
                                          0);
    }

    // The tile search Init and Create inline:
    // load ButtonMontage on first use (FUN_00559db0), then the first tile whose name stricmp-
    // equals Path, else null.
    FButtonMontageTile* FindMontageTileForPath(char const* Path)
    {
        if (!G_bButtonMontageLoaded)
            LoadButtonMontage();
        int const Count = static_cast<int>(G_ButtonMontageTiles.size());
        for (int Index = 0; Index < Count; ++Index)
        {
            if (_stricmp(G_ButtonMontageTiles[Index].Name.c_str(), Path) == 0)
                return &G_ButtonMontageTiles[Index];
        }
        return nullptr;
    }

    // The mouse ray LAB_0055b660 builds inline from the software cursor through the Win3D
    // camera: near-plane point, FUN_00423b90 with a copy of the camera
    // orientation, normalised by 1 / sqrt.
    void CursorRayForUpdate(Vector* Ray)
    {
        FSpaceCamera* const Cam = Win3DGetCamera(); // DAT_0067dbf8
        int CursorX = 0;
        int CursorY = 0;
        GetClientSoftwareCursor(&CursorX, &CursorY, nullptr);
        Vector Local;
        Local.x = static_cast<float>((static_cast<double>(CursorX) - Cam->ScreenCenterX) /
                                     Cam->XScale * Cam->ZNear);
        Local.y = static_cast<float>((static_cast<double>(CursorY) - Cam->ScreenCenterY) /
                                     Cam->YScale * Cam->ZNear);
        Local.z = -Cam->ZNear;
        Matrix CamOrientation;
        memcpy(&CamOrientation, &Cam->World, sizeof(CamOrientation)); // DAT_0067dbfc
        Vector World;
        Win3DTransformByMatrix(World, CamOrientation, Local); // FUN_00423b90
        double const Inv =
            kOne / sqrt(static_cast<double>(World.x) * World.x +
                        static_cast<double>(World.y) * World.y +
                        static_cast<double>(World.z) * World.z);
        Ray->x = static_cast<float>(World.x * Inv);
        Ray->y = static_cast<float>(World.y * Inv);
        Ray->z = static_cast<float>(World.z * Inv);
    }

    // The mouse ray FUN_0055b950 and LAB_0055c690 build inline: the near-plane
    // point (FUN_004bdd40), the 3DMathEngine's TransformMatrix (DAT_00679aec +0x2c) with a copy
    // of the camera orientation (DAT_0067dbfc), normalised by 1 / sqrt.
    void CursorRayThroughMathEngine(Vector* Ray)
    {
        FSpaceCamera* const Cam = Win3DGetCamera(); // DAT_0067dbf8
        int CursorX = 0;
        int CursorY = 0;
        GetClientSoftwareCursor(&CursorX, &CursorY, nullptr); // DAT_00616840 / DAT_00616844
        Vector Local;
        Win3DScreenToNearPlane(Cam, Local, static_cast<float>(CursorX),
                               static_cast<float>(CursorY));
        Matrix CamOrientation;
        memcpy(&CamOrientation, &Cam->World, sizeof(CamOrientation));
        Vector World;
        ButtonMathEngine()->TransformMatrix(World, CamOrientation, Local);
        double const Inv =
            kOne / sqrt(static_cast<double>(World.x) * World.x +
                        static_cast<double>(World.y) * World.y +
                        static_cast<double>(World.z) * World.z);
        Ray->x = static_cast<float>(World.x * Inv);
        Ray->y = static_cast<float>(World.y * Inv);
        Ray->z = static_cast<float>(World.z * Inv);
    }

    // The holder tint FUN_00559aa0 and Draw write before FUN_0059efa0: bytes 0..2 into +0x14 / +0x18 / +0x1c, byte 3 into +0x24,
    // each times 1/255.
    void SetHolderTint(FClientMeshHolder* Holder, unsigned char const* Rgba)
    {
        Holder->UserFloats[0] = static_cast<float>(static_cast<double>(Rgba[0]) * kInv255);
        Holder->UserFloats[1] = static_cast<float>(static_cast<double>(Rgba[1]) * kInv255);
        Holder->UserFloats[2] = static_cast<float>(static_cast<double>(Rgba[2]) * kInv255);
        Holder->UserFloats[4] = static_cast<float>(static_cast<double>(Rgba[3]) * kInv255);
    }

    // FUN_00559aa0 (cdecl Button, Rgba; Draw pushes +0x335 as a third argument that the
    // function never reads) -- the button body: the ButtonMontage icon at the instance's
    // position (or +0x60 + +0xa8) through FUN_00559900, sized by the tile's scale, else by the
    // instance's extent (Engine vt+0x88), else 0.022, times sqrt(2); when no icon was drawn
    // and an instance is bound, the instance tinted by Rgba through FUN_0059efa0.
    void ButtonDrawBody(NnButtonControl* Button, unsigned char const* Rgba)
    {
        float Size = kDefaultIconSize;
        FButtonMontageTile* const Tile = Button->MontageTile;
        if (Tile != nullptr)
        {
            Size = Tile->Scale;
            if (Size == 0.0f)
            {
                // GetInstanceExtent(+0x84, 1, &Size, &Centre) -- no instance test.
                Vector Centre;
                DALib::Engine->GetInstanceExtent(Button->MeshHolder.EngineInstance, 1, &Size,
                                                 &Centre);
            }
        }
        Size = Size * kSqrt2;

        float Where[3];
        if (HasInstance(Button))
        {
            const Vector* const Placed = ClientMeshHolderGetPosition(&Button->MeshHolder); // FUN_00420fb0
            Where[0] = Placed->x;
            Where[1] = Placed->y;
            Where[2] = Placed->z;
        }
        else
        {
            Where[0] = Button->Offset.x + Button->Position.x;
            Where[1] = Button->Position.y + Button->Offset.y;
            Where[2] = Button->Position.z + Button->Offset.z;
        }

        if (NnMontageDrawTileAt(Tile, Where, Rgba, Size, 0, 0.0f)) // FUN_00559900
            return;
        if (!HasInstance(Button))
            return;
        SetHolderTint(&Button->MeshHolder, Rgba);
        SubmitWin3DEngineInstance(&Button->MeshHolder, Win3DGetCamera(), 0, 1.0f, 0); // FUN_0059efa0
    }

    void SetRgba(unsigned char* Out, unsigned char R, unsigned char G, unsigned char B,
                 unsigned char A)
    {
        Out[0] = R;
        Out[1] = G;
        Out[2] = B;
        Out[3] = A;
    }
}

void ButtonKeyCommandLookup(char const* KeyName, int* OutCommandId, int* OutToolTipIds)
{
    // FUN_0055a490.
    *OutCommandId = 0;
    *OutToolTipIds = 0;
    if (_stricmp(KeyName, "tractor") == 0)
    {
        *OutCommandId = 0x8b;
        *OutToolTipIds = 0xbc2;
        return;
    }
    if (_stricmp(KeyName, "next") == 0) // DAT_005e15d4
    {
        *OutCommandId = 0x1d;
        *OutToolTipIds = 0xbb9;
        return;
    }
    if (_stricmp(KeyName, "prev") == 0) // DAT_005e15cc
    {
        *OutCommandId = 0x1e;
        *OutToolTipIds = 0xbba;
        return;
    }
    if (_stricmp(KeyName, "next_subtarget") == 0)
    {
        *OutCommandId = 0x21;
        *OutToolTipIds = 0x41f;
        return;
    }
    if (_stricmp(KeyName, "prev_subtarget") == 0)
    {
        *OutCommandId = 0x22;
        *OutToolTipIds = 0x420;
    }
}

NnButtonControl::NnButtonControl()
    : ToolTip2Ids(0)
    , ButtonBits(0)
    , ButtonBits2(0)
    , bSpin(0)
    , bBlink(0)
    , bBlinkPhase(0)
    , MontageMode(0)
    , bUseColour(0)
    , bGlow(0)
    , MontageTile(nullptr)
    , SpinAngle(0.0f)
    , SpinSpeed(180.0f) // 0x43340000
    , BlinkTimer(0.0f)
    , GlowArgb(0)
    , CommandId(0)
    , UiSoundSlot(0)
{
    // FUN_0055a560 after FUN_0059f860. Two empty strings, the colour +0x370 = 0, then
    // +0x330 &= 0x80 and +0x331 = (+0x331 & 0xee) | 0xe; the bits the masks keep are whatever
    // the allocation held in retail and 0 here. +0x338 and +0x348 are not written by the ctor
    // (Init / Create and Draw write them); the port starts them at null / 0.
    Colour[0] = 0;
    Colour[1] = 0;
    Colour[2] = 0;
    Colour[3] = 0;
    ButtonBits = static_cast<unsigned char>(ButtonBits & 0x80);
    ButtonBits2 = static_cast<unsigned char>((ButtonBits2 & 0xee) | 0x0e);
}

NnButtonControl::~NnButtonControl()
{
    // FUN_0055d610: a button still holding the capture leaves the capture stack
    // (FUN_0059d960); then the two strings (+0x360 first) and WinControl's FUN_0059fa50.
    if ((ButtonBits & 0x10) != 0)
        Win3DRemoveCapture(this);
}

void NnButtonControl::SetPosition(Vector const* NewPosition)
{
    // FUN_0055b4a0 (RET 4).
    RequestedPosition.x = NewPosition->x;
    RequestedPosition.y = NewPosition->y;
    RequestedPosition.z = NewPosition->z;
    bAttached = 1;

    Vector Placed;
    if ((PositionFlags & 1) != 0)
    {
        Vector Attach;
        GetAttachPosition(&Attach); // slot 41
        Placed.x = NewPosition->x + Attach.x;
        Placed.y = Attach.y + NewPosition->y;
        Placed.z = Attach.z + NewPosition->z;
    }
    else
    {
        Placed.x = NewPosition->x;
        Placed.y = NewPosition->y;
        Placed.z = NewPosition->z;
    }
    WinControl::SetPosition(&Placed); // FUN_005a0880

    // The instance is placed again, at +0x60 (+ +0xa8 under PositionFlags bit 0).
    if (!HasInstance(this))
        return;
    Vector Instance;
    if ((PositionFlags & 1) != 0)
    {
        Instance.x = Offset.x + Position.x;
        Instance.y = Offset.y + Position.y;
        Instance.z = Offset.z + Position.z;
    }
    else
    {
        Instance.x = Position.x;
        Instance.y = Position.y;
        Instance.z = Position.z;
    }
    DALib::Engine->SetInstancePosition(MeshHolder.EngineInstance, &Instance);
    DALib::Engine->UpdateInstance(MeshHolder.EngineInstance, 0, nullptr);
}

void NnButtonControl::SetOrientation(Matrix const* NewOrientation)
{
    // LAB_0055b5c0 (RET 4): the new orientation times the attach orientation (slot 40) through
    // the 3DMathEngine's MulMatrix (+0x20) into Win3D's +0x3c, then onto the instance. Unlike
    // WinControl's FUN_005a0950 it does not recurse into children.
    bAttached = 1;
    Matrix Attach;
    GetAttachOrientation(&Attach);
    Matrix Result;
    ButtonMathEngine()->MulMatrix(Result, *NewOrientation,
                                  Attach);
    memcpy(&Orientation, &Result, sizeof(Orientation));
    if (!HasInstance(this))
        return;
    DALib::Engine->SetInstanceOrientation(MeshHolder.EngineInstance, &Orientation);
    DALib::Engine->UpdateInstance(MeshHolder.EngineInstance, 0, nullptr);
}

bool NnButtonControl::Create(Win3D* NewParent, void* Data)
{
    // FUN_0055a6b0 (RET 8).
    Win3DReparent(this, NewParent); // FUN_0059dd00
    Win3DRegister(this);            // FUN_0059dbf0
    Flags = static_cast<unsigned char>((Flags & 0xfb) | 0x03);

    INI_Reader* const Ini = static_cast<INI_Reader*>(Data);
    if (Ini != nullptr)
    {
        while (Ini->read_value())
        {
            if (Ini->is_value("mesh"))
            {
                SetMesh(Ini->get_value_string(), 0); // slot 38
                continue;
            }
            if (Ini->is_value("mesh2"))
            {
                Mesh2.assign(Ini->get_value_string());
                continue;
            }
            if (Ini->is_value("button_sound"))
            {
                char const* const Value = Ini->get_value_string();
                if (Value[0] == '\0')
                    continue;
                SoundName.assign(Value);
                UiSoundSlot = FindUiSoundSlot(SoundName.c_str()); // FUN_00564610
                continue;
            }
            if (Ini->is_value("event"))
            {
                // A null value still reaches the lookup; only an empty one is
                // skipped.
                char const* const Value = Ini->get_value_string();
                if (Value != nullptr && Value[0] == '\0')
                    continue;
                ButtonKeyCommandLookup(Value, &CommandId, &ToolTipIds); // FUN_0055a490
                if (CommandId != 0 || ToolTipIds != 0)
                    RefreshAttachment(); // slot 35
                continue;
            }
            if (Ini->is_value("offset"))
            {
                float const Y = Ini->get_value_float(1);
                float const X = Ini->get_value_float(0);
                float const Z = Ini->get_value_float(2);
                Offset.x = X;
                Offset.y = Y;
                Offset.z = Z;
                if (HasInstance(this))
                {
                    // FUN_00422b80 builds {+0x60 + offset} and the instance moves.
                    Vector Placed;
                    Placed.z = Offset.z + Position.z;
                    Placed.y = Offset.y + Position.y;
                    Placed.x = Position.x + Offset.x;
                    DALib::Engine->SetInstancePosition(MeshHolder.EngineInstance, &Placed);
                    DALib::Engine->UpdateInstance(MeshHolder.EngineInstance, 0, nullptr);
                }
                continue;
            }
            if (Ini->is_value("hardpoint"))
            {
                // The attach transform is reset, then filled from the NEW parent's
                // hardpoint of that name. The name itself is not kept (+0x1d0 is untouched).
                AttachOrientation.E[0][0] = 1.0f;
                AttachOrientation.E[0][1] = 0.0f;
                AttachOrientation.E[0][2] = 0.0f;
                AttachOrientation.E[1][0] = 0.0f;
                AttachOrientation.E[1][1] = 1.0f;
                AttachOrientation.E[1][2] = 0.0f;
                AttachOrientation.E[2][0] = 0.0f;
                AttachOrientation.E[2][1] = 0.0f;
                AttachOrientation.E[2][2] = 1.0f;
                AttachPosition.x = 0.0f;
                AttachPosition.y = 0.0f;
                AttachPosition.z = 0.0f;
                if (NewParent == nullptr)
                    continue;
                WinControl* const ParentControl = NewParent->AsWinControl();
                if (ParentControl == nullptr)
                    continue;
                ParentControl->GetHardpointTransform(Ini->get_value_string(), &AttachXform);
                continue;
            }
            if (Ini->is_value("mouse_size"))
            {
                float const Y = Ini->get_value_float(1);
                float const X = Ini->get_value_float(0);
                float const Z = Ini->get_value_float(2);
                HalfExtents.x = X;
                HalfExtents.y = Y;
                HalfExtents.z = Z;
                continue;
            }
            if (Ini->is_value("no_mesh_render"))
            {
                unsigned char const Bit = (Ini->get_value_float(0) != 0.0f) ? 1 : 0;
                ButtonBits2 = static_cast<unsigned char>((ButtonBits2 & 0xfe) | Bit);
                continue;
            }
            if (Ini->is_value("action_button"))
            {
                unsigned char const Bit = (Ini->get_value_float(0) != 0.0f) ? 0x08 : 0;
                ButtonBits = static_cast<unsigned char>((ButtonBits & 0xf7) | Bit);
                continue;
            }
            if (Ini->is_value("no_color_change"))
            {
                unsigned char const Bit = (Ini->get_value_float(0) != 0.0f) ? 0x20 : 0;
                ButtonBits = static_cast<unsigned char>((ButtonBits & 0xdf) | Bit);
                continue;
            }
            if (Ini->is_value("no_z_enable"))
            {
                bNoZEnable = (Ini->get_value_float(0) != 0.0f) ? 1 : 0;
                continue;
            }
            if (Ini->is_value("tooltip"))
            {
                ToolTipIds = static_cast<int>(Ini->get_value_float(0)); // ftol
                continue;
            }
            if (Ini->is_value("tooltip2"))
                ToolTip2Ids = static_cast<int>(Ini->get_value_float(0)); // ftol
        }

        // The ButtonMontage tile of the (possibly just loaded) mesh path.
        MontageTile = FindMontageTileForPath(MeshPath);
    }

    // The parent is re-placed at its own position (slot 5), which positions this
    // control with it.
    if (Parent != nullptr)
        Parent->SetPosition(&Parent->Position);
    return true;
}

void NnButtonControl::Update()
{
    // LAB_0055b660: Win3D's child walk (FUN_005a17b0) first.
    Win3D::Update();

    // While the button holds the capture, follow the cursor and light / unlight
    // bit 2 as it moves on and off.
    if ((ButtonBits & 0x10) != 0)
    {
        Vector Ray;
        CursorRayForUpdate(&Ray);
        if (HitTest(&Ray)) // slot 4
        {
            if ((ButtonBits & 0x04) == 0)
            {
                ButtonBits = static_cast<unsigned char>(ButtonBits | 0x04);
                TellParent(this, kWinNotifyCapturedEnter);
                if (Parent != nullptr)
                {
                    NnUiPulseReset(); // FUN_0059d580
                    TellParent(this, kWinNotifyHighlight);
                }
            }
        }
        else if ((ButtonBits & 0x04) != 0)
        {
            ButtonBits = static_cast<unsigned char>(ButtonBits & 0xfb);
            TellParent(this, kWinNotifyCapturedLeave);
            TellParent(this, kWinNotifyUnhighlight);
        }
    }

    // The blink. The timer gains the frame time; every whole second past 1.0 flips +0x334
    // (x87: "not above", unordered included, stops).
    if (bBlink == 0)
        return;
    double Timer = GetClientFrameTimeCapped() + static_cast<double>(BlinkTimer); // FUN_0042d680
    BlinkTimer = static_cast<float>(Timer);
    if (!(Timer > kOne))
        return;
    unsigned char Phase = bBlinkPhase;
    do
    {
        Timer = Timer - kOne;
        Phase = (Phase == 0) ? 1 : 0;
    } while (Timer > kOne);
    BlinkTimer = static_cast<float>(Timer);
    bBlinkPhase = Phase;
}

void NnButtonControl::Draw()
{
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only: owners that walk their children themselves (HudTargetBase, NN_MainButton)
    // reach this without the Win3D draw walk, so the control sets its own HUD shift.
    FWidescreenHudShiftScope const WidescreenScope(WidescreenHudShiftPixelsFor(this), true);
#endif
    // FUN_0055af90.
    GlowArgb = 0;
    if (GetGameOption(kGameOptInterfaceGroup) == 0) // FUN_005792b0(6)
        return;
    if (GetGameOption(kGameOptNeuronet) == 0) // (9)
        return;
    if (GetGameOption(kGameOptDrawButtonControls) == 0) // (0x14)
        return;
    if ((Flags & kWin3DVisible) == 0)
        return;
    if ((ButtonBits2 & 0x10) == 0 && (Flags & kWin3DActive) == 0)
        return;
    if (bBlink != 0 && bBlinkPhase != 0)
        return;

    Win3DDrawChildren(this); // FUN_0059e5c0
    unsigned char const SavedSelected = static_cast<unsigned char>((ButtonBits >> 6) & 1);

    if (bSpin != 0)
    {
        // The spin. With a positive speed the angle runs forward while bit 0 is
        // set, otherwise it winds back to 0 the short way (up through 359, down through 1).
        double const Frame = GetClientFrameTimeCapped(); // FUN_0042d680
        if (SpinSpeed > 0.0f) // FCOMP 0 / TEST AH,0x41 / JNZ: not above skips
        {
            double const Step = Frame * SpinSpeed;
            if ((ButtonBits & 0x01) != 0)
            {
                SpinAngle = static_cast<float>(Step + SpinAngle);
            }
            else if (SpinAngle > 180.0f) // DAT_005da41c
            {
                double const Angle = Step + Step + SpinAngle;
                SpinAngle = static_cast<float>(Angle);
                if (Angle > 359.0) // DAT_005e1734
                    SpinAngle = 0.0f;
            }
            else
            {
                double const Angle = SpinAngle - (Step + Step);
                SpinAngle = static_cast<float>(Angle);
                if (Angle < kOne) // TEST AH,5 / JP: not below skips
                    SpinAngle = 0.0f;
            }
        }
        // Wrap into (-360, 360) -- DAT_005ca23c / DAT_005e1730.
        if (SpinAngle >= 360.0f)
            SpinAngle = SpinAngle - 360.0f;
        else if (SpinAngle <= -360.0f)
            SpinAngle = SpinAngle + 360.0f;

        // RotateMatrix({0, angle, 0}) onto the instance (Engine vt+0x94, vt+0x6c).
        // Retail tests no instance here.
        Vector Angles;
        Angles.x = 0.0f;
        Angles.y = SpinAngle;
        Angles.z = 0.0f;
        Matrix Spun = RotateMatrix(Angles);
        DALib::Engine->SetInstanceOrientation(MeshHolder.EngineInstance, &Spun);
        DALib::Engine->UpdateInstance(MeshHolder.EngineInstance, 0, nullptr);
    }

    // The colours. Glow starts black; both +0x330 bit-3 arms in retail produce the same pair,
    // so the test is not repeated.
    unsigned char Glow[4] = {0, 0, 0, 0};
    unsigned char Body[4] = {0, 0, 0, 0};
    if ((Flags & kWin3DActive) == 0)
    {
        // Inactive: grey, and the selected / lit bits are dropped.
        ButtonBits = static_cast<unsigned char>(ButtonBits & 0xbb);
        SetRgba(Glow, 0x80, 0x80, 0x80, 0xff);
        SetRgba(Body, 0x80, 0x80, 0x80, 0xff);
    }
    else if ((ButtonBits & 0x40) != 0 || ((ButtonBits2 & 0x04) != 0 && (ButtonBits & 0x04) != 0))
    {
        // Selected, or lit while pressed: yellow.
        SetRgba(Glow, 0xff, 0xff, 0x00, 0xff);
        SetRgba(Body, 0xff, 0xff, 0x00, 0xff);
    }
    else if ((ButtonBits & 0x02) != 0 || ((ButtonBits & 0x01) != 0 && (ButtonBits2 & 0x08) != 0))
    {
        // Focused, or hovered with the hover pulse on: two FUN_00561da0 samples.
        unsigned long Pulse = 0;
        NnUiPulseBlendArgb(&Pulse);
        memcpy(Body, &Pulse, sizeof(Body));
        NnUiPulseBlendArgb(&Pulse);
        memcpy(Glow, &Pulse, sizeof(Glow));
    }
    else
    {
        // Idle: DAT_00679bb0.
        memcpy(Glow, &G_NnPaletteCyan, sizeof(Glow));
        memcpy(Body, &G_NnPaletteCyan, sizeof(Body));
    }
    if ((ButtonBits & 0x20) != 0)
        SetRgba(Body, 0xff, 0xff, 0xff, 0xff); // no_color_change
    if (bUseColour != 0)
    {
        memcpy(Body, Colour, sizeof(Body));
        memcpy(Glow, Colour, sizeof(Glow));
    }

    // The body, unless no_mesh_render.
    if ((ButtonBits2 & 0x01) == 0)
    {
        if (bSpin != 0 && (ButtonBits & 0x01) != 0)
        {
            if (HasInstance(this))
            {
                SetHolderTint(&MeshHolder, Body);
                SubmitWin3DEngineInstance(&MeshHolder, Win3DGetCamera(), 0, 1.0f, 0); // FUN_0059efa0
            }
        }
        else
        {
            ButtonDrawBody(this, Body); // FUN_00559aa0(this, Body, +0x335)
        }
    }

    Win3DDisable(kWin3DDepthTest); // FUN_00421ed0(0xb71)
    if ((ButtonBits2 & 0x01) != 0 || bGlow != 0)
    {
        if (HasInstance(this))
        {
            GlowArgb = (static_cast<unsigned long>(Glow[3]) << 24) |
                       (static_cast<unsigned long>(Glow[0]) << 16) |
                       (static_cast<unsigned long>(Glow[1]) << 8) |
                       static_cast<unsigned long>(Glow[2]);
            SubmitWin3DTintedEngineInstanceWrapped(Win3DGetCamera(), MeshHolder.EngineInstance,
                                                  1, Glow, 1, 1.0f, 0); // FUN_00434630
        }
    }
    Win3DDisable(kWin3DDepthTest); // FUN_00421ed0(0xb71)

    // Bit 6 goes back to what it was before the inactive arm could clear it.
    ButtonBits = static_cast<unsigned char>((ButtonBits & 0xbf) | (SavedSelected << 6));
    PostDraw(); // vt+0x9c
}

void NnButtonControl::MouseEnter(Vector const* /*Ray*/)
{
    // FUN_0055b8a0 (RET 4).
    if (Parent != nullptr)
    {
        NnUiPulseReset(); // FUN_0059d580
        TellParent(this, kWinNotifyHighlight);
    }
    ButtonBits = static_cast<unsigned char>(ButtonBits | 0x01);
    if ((ButtonBits2 & 0x02) != 0)
        PlayUiRolloverSound(); // FUN_00564700
}

void NnButtonControl::MouseLeave(Vector const* /*Ray*/)
{
    // FUN_0055b900 (RET 4).
    if (ToolTipIds != 0)
        NnToolTipHideGlobal(); // FUN_004be390
    TellParent(this, kWinNotifyUnhighlight);
    ButtonBits = static_cast<unsigned char>(ButtonBits & 0xfe);
}

bool NnButtonControl::MouseEvent(FWin3DMouseEvent const* Event)
{
    // FUN_0055b950 (RET 4).
    if ((ButtonBits2 & 0x02) == 0)
        return false;
    // While some control holds the mouse, a button whose WinControl parent does
    // not hold it ignores the event.
    if (Parent != nullptr && Win3DAnyCaptured()) // FUN_0059e6a0
    {
        WinControl* const ParentControl = Parent->AsWinControl();
        if (ParentControl != nullptr && ParentControl->bCaptured == 0)
            return false;
    }

    if ((ButtonBits & 0x10) != 0)
    {
        // The release of a press this button captured.
        if (Event->Button != kWin3DMouseLeft || Event->Action != kWin3DMouseRelease)
            return false;
        ButtonBits = static_cast<unsigned char>(ButtonBits & 0xeb);
        Win3DRemoveCapture(this); // FUN_0059d960

        // The cursor ray (FUN_004bdd40), through the math engine.
        Vector Ray;
        CursorRayThroughMathEngine(&Ray);

        if (!HitTest(&Ray)) // slot 4
        {
            // Released off the button.
            TellParent(this, kWinNotifyRelease);
            ButtonBits = static_cast<unsigned char>(ButtonBits & 0xfe);
            return true;
        }

        // The click sound -- the UI sound slot, else the sound archetype named by
        // a non-empty +0x360.
        if (UiSoundSlot != -1)
        {
            PlayOrRewindUiSoundSlot(static_cast<unsigned>(UiSoundSlot)); // FUN_005645c0
        }
        else if (!SoundName.empty())
        {
            ID_String SoundId;
            SoundId.Id = CreateID(SoundName.c_str());
            void* const Arch = LookupClientSoundArch(&SoundId); // FUN_0042ae40
            PlayClientSoundArchIfIdle(Arch, nullptr, nullptr);          // FUN_004285f0
        }

        if ((ButtonBits & 0x80) != 0)
        {
            // A double click only tells the parent 0x27 (its answer is ignored).
            TellParent(this, kWinNotifyDoubleClick);
            return true;
        }
        Activate(); // slot 45
        return true;
    }

    // A left press, single or double click.
    if (Event->Button != kWin3DMouseLeft)
        return false;
    if (Event->Action != kWin3DMousePress && Event->Action != kWin3DMouseDoubleClick)
        return false;
    if (Event->Action == kWin3DMouseDoubleClick)
        ButtonBits = static_cast<unsigned char>(ButtonBits | 0x80);
    else
        ButtonBits = static_cast<unsigned char>(ButtonBits & 0x7f);
    TellParent(this, kWinNotifyPress);
    ButtonBits = static_cast<unsigned char>(ButtonBits | 0x14);
    if (Parent != nullptr)
    {
        NnUiPulseReset(); // FUN_0059d580
        TellParent(this, kWinNotifyHighlight);
    }
    Win3DPushCapture(this); // FUN_0059d8d0
    return true;
}

void NnButtonControl::ShowToolTip()
{
    // FUN_0055bde0: key -1 and no alt id first, the empty caption (FUN_00413df0), then both ids
    // from +0xcc, the command +0x34c as the aux dword, the instance's position (Engine vt+0xa0
    // [9..11]) or +0x60 + +0xa8, the half extents +0xb4 and the live byte; FUN_004be370; then
    // the record's teardown (~FToolTipHoverInfo). The caption +0x08 / +0x0c zeroing and
    // FUN_004144b0 on +0x10 are the record's constructor.
    FToolTipHoverInfo Info;
    Info.IdsAlt = 0;                  // +0x04
    Info.Ids = -1;                    // +0x00
    Info.bLive = 0;                   // +0x60
    Info.Ids = ToolTipIds;            // +0x00 <- +0xcc
    Info.IdsAlt = ToolTipIds;         // +0x04 <- +0xcc
    Info.AuxField = CommandId;        // +0x44 <- +0x34c
    if (HasInstance(this))
    {
        Transform Xform;
        memcpy(&Xform, DALib::Engine->GetInstanceTransform(MeshHolder.EngineInstance),
               sizeof(Xform));
        Info.WorldPos[0] = Xform.Translation.x;
        Info.WorldPos[1] = Xform.Translation.y;
        Info.WorldPos[2] = Xform.Translation.z;
    }
    else
    {
        Info.WorldPos[0] = Position.x + Offset.x;
        Info.WorldPos[1] = Offset.y + Position.y;
        Info.WorldPos[2] = Offset.z + Position.z;
    }
    Info.HalfExtents[0] = HalfExtents.x; // +0x54 <- +0xb4
    Info.HalfExtents[1] = HalfExtents.y;
    Info.HalfExtents[2] = HalfExtents.z;
    Info.bLive = 1;
    NnToolTipFeedHover(&Info);
}

intptr_t NnButtonControl::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // FUN_0055bc30. Every own case answers 0; the rest go to WinControl's FUN_005a13c0.
    switch (Msg)
    {
    case 0x0c:
        ButtonBits = static_cast<unsigned char>((ButtonBits & 0xbf) | (A != 0 ? 0x40 : 0));
        return 0;
    case kWinNotifyHighlight:
        ButtonBits = static_cast<unsigned char>(ButtonBits | 0x02);
        return 0;
    case kWinNotifyUnhighlight:
        ButtonBits = static_cast<unsigned char>(ButtonBits & 0xfd);
        return 0;
    case 0x2e:
        *reinterpret_cast<int*>(A) = CommandId;
        return 0;
    case 0x32:
        ButtonBits2 = static_cast<unsigned char>((ButtonBits2 & 0xfe) | (A != 0 ? 0x01 : 0));
        return 0;
    case kWinPropColour:
        memcpy(Colour, reinterpret_cast<void const*>(A), sizeof(Colour));
        bUseColour = (B == 1) ? 1 : 0;
        return 0;
    case 0x34:
        ButtonBits2 = static_cast<unsigned char>((ButtonBits2 & 0xef) | (A != 0 ? 0x10 : 0));
        return 0;
    case 0x35:
        bGlow = (A != 0) ? 1 : 0;
        return 0;
    case 0x36:
        bSpin = (A == 1) ? 1 : 0;
        return 0;
    case kWinPropBlink:
        bBlink = (A != 0) ? 1 : 0;
        return 0;
    default:
        return WinControl::SetProperty(Msg, A, B); // JMP FUN_005a13c0
    }
}

void NnButtonControl::Activate()
{
    // FUN_0055a660.
    if (CommandId != 0)
    {
        HudPostCommand(static_cast<unsigned>(CommandId), 0); // FUN_005763b0
        return;
    }
    TellParent(this, kWinNotifyClick);
}

void NnButtonControl::Init(char const* NewMeshPath, char const* NewMesh2,
                           char const* NewSoundName, char const* KeyCommandName,
                           Vector const* NewOffset, char const* Hardpoint,
                           Vector const* NewHalfExtents, unsigned char bNoMeshRender,
                           unsigned char bActionButton, unsigned char bNoColorChange,
                           unsigned char bNewNoZEnable, int NewToolTipIds, int NewToolTip2Ids,
                           int NewCommandId)
{
    // FUN_0055ac30 (RET 0x38).
    if (NewMeshPath != nullptr)
    {
        strcpy(MeshPath, NewMeshPath);
        MontageTile = FindMontageTileForPath(MeshPath);
        // Without a tile, or with a tile of scale 0 (x87: a non-zero or unordered scale skips),
        // the mesh itself is loaded (slot 38).
        if (MontageTile == nullptr || MontageTile->Scale == 0.0f)
            SetMesh(NewMeshPath, 0);
    }
    if (NewMesh2 != nullptr)
        Mesh2.assign(NewMesh2);
    if (NewSoundName != nullptr && NewSoundName[0] != '\0')
    {
        SoundName.assign(NewSoundName);
        UiSoundSlot = FindUiSoundSlot(SoundName.c_str()); // FUN_00564610
    }
    if (KeyCommandName != nullptr && KeyCommandName[0] != '\0')
    {
        ButtonKeyCommandLookup(KeyCommandName, &CommandId, &ToolTipIds); // FUN_0055a490
        if (CommandId != 0 || ToolTipIds != 0)
            RefreshAttachment(); // slot 35
    }

    Offset.x = NewOffset->x;
    Offset.y = NewOffset->y;
    Offset.z = NewOffset->z;
    if (HasInstance(this))
    {
        Vector Placed;
        Placed.x = Offset.x + Position.x;
        Placed.y = Offset.y + Position.y;
        Placed.z = Offset.z + Position.z;
        DALib::Engine->SetInstancePosition(MeshHolder.EngineInstance, &Placed);
        DALib::Engine->UpdateInstance(MeshHolder.EngineInstance, 0, nullptr);
    }

    if (Hardpoint != nullptr)
    {
        // The name is kept, the attach transform reset and filled from the
        // parent's hardpoint of that name (slot 36; its answer is not used).
        strcpy(HardpointName, Hardpoint);
        AttachOrientation.E[0][0] = 1.0f;
        AttachOrientation.E[0][1] = 0.0f;
        AttachOrientation.E[0][2] = 0.0f;
        AttachOrientation.E[1][0] = 0.0f;
        AttachOrientation.E[1][1] = 1.0f;
        AttachOrientation.E[1][2] = 0.0f;
        AttachOrientation.E[2][0] = 0.0f;
        AttachOrientation.E[2][1] = 0.0f;
        AttachOrientation.E[2][2] = 1.0f;
        AttachPosition.x = 0.0f;
        AttachPosition.y = 0.0f;
        AttachPosition.z = 0.0f;
        if (Parent != nullptr)
        {
            WinControl* const ParentControl = Parent->AsWinControl();
            if (ParentControl != nullptr)
                ParentControl->GetHardpointTransform(Hardpoint, &AttachXform);
        }
    }

    HalfExtents.x = NewHalfExtents->x;
    HalfExtents.y = NewHalfExtents->y;
    HalfExtents.z = NewHalfExtents->z;
    ButtonBits2 = static_cast<unsigned char>((ButtonBits2 & 0xfe) | (bNoMeshRender & 0x01));
    ButtonBits = static_cast<unsigned char>((ButtonBits & 0xf7) | ((bActionButton << 3) & 0x08));
    ButtonBits = static_cast<unsigned char>((ButtonBits & 0xdf) | ((bNoColorChange << 5) & 0x20));
    bNoZEnable = bNewNoZEnable;
    ToolTipIds = NewToolTipIds;
    ToolTip2Ids = NewToolTip2Ids;
    CommandId = NewCommandId;
}

// ---------------------------------------------------------------------------------------
// ToggleButtonControl (nn_button_control.h)
// ---------------------------------------------------------------------------------------

namespace
{
    // DAT_005e1738 -- the double 0x400921fb51000000 (pi rounded to float): the Draw glow pass
    // turns the instance half a turn about y.
    double const kToggleGlowAngle = 3.1415927410125732;

    bool HasToggleInstance(NnToggleButtonControl const* Control)
    {
        // The `CMP [reg+0x37c],-1` the slots inline.
        return Control->ToggleMesh.EngineInstance !=
               InvalidInstanceIndex;
    }

    // The parent notification with a B argument (FUN_0055cc90's 0x30 carries the state).
    void TellParentWithValue(WinControl* Self, int Msg, uintptr_t Value)
    {
        Win3D* const Parent = Self->GetParent();
        if (Parent == nullptr)
            return;
        WinControl* const ParentControl = Parent->AsWinControl();
        if (ParentControl == nullptr)
            return;
        ParentControl->SetProperty(Msg, reinterpret_cast<uintptr_t>(Self->AsWinControl()), Value);
    }

    // The hover record LAB_0055c690 packs inline: the ids twice, no
    // aux dword (unlike FUN_0055bde0 it leaves +0x44 at 0), the main instance's position
    // (Engine vt+0xa0 [9..11], no instance test), the half extents +0xb4 and the live byte;
    // FUN_004be370; then the record's teardown (~FToolTipHoverInfo).
    void FeedToggleToolTip(NnToggleButtonControl* Control, int Ids)
    {
        // The caption +0x08 / +0x0c and FUN_004144b0 on +0x10: the record's constructor;
        // FUN_00413df0(0, -1) on the null caption does nothing.
        FToolTipHoverInfo Info;
        Info.IdsAlt = 0;                 // +0x04
        Info.Ids = -1;                   // +0x00
        Info.AuxField = 0;               // +0x44
        Info.bLive = 0;                  // +0x60
        Info.Ids = Ids;
        Info.IdsAlt = Ids;
        Transform Xform;
        memcpy(&Xform, DALib::Engine->GetInstanceTransform(Control->MeshHolder.EngineInstance),
               sizeof(Xform));
        Info.WorldPos[0] = Xform.Translation.x;
        Info.WorldPos[1] = Xform.Translation.y;
        Info.WorldPos[2] = Xform.Translation.z;
        Info.HalfExtents[0] = Control->HalfExtents.x;
        Info.HalfExtents[1] = Control->HalfExtents.y;
        Info.HalfExtents[2] = Control->HalfExtents.z;
        Info.bLive = 1;
        NnToolTipFeedHover(&Info);
    }
}

NnToggleButtonControl::NnToggleButtonControl()
    : ToggleState(0)
    , UiSoundSlotState0(0)
    , UiSoundSlotState1(0)
{
    // The ctor FUN_0055d3c0 inlines after FUN_0055a560: the +0x378 holder (FUN_00420d40, the
    // five floats 1.0f, +0x10 = -1 -- FClientMeshHolder's defaults) and two empty strings.
    // +0x3a0 / +0x3c4 / +0x3c8 are not written (Create / Init set them); the port starts them
    // at 0.
}

NnToggleButtonControl::~NnToggleButtonControl()
{
    // FUN_0055d710: +0x3b4, then +0x3a4, then the holder (FUN_0059ef20), then ButtonControl's
    // body inlined -- all by the member / base destructors in that order.
}

void NnToggleButtonControl::PlaceToggleMesh()
{
    if (HasToggleInstance(this))
    {
        // FUN_00422b80(+0xa8 + +0x60, +0xac + +0x64, +0xb0 + +0x68).
        Vector Placed;
        Placed.x = Offset.x + Position.x;
        Placed.y = Offset.y + Position.y;
        Placed.z = Offset.z + Position.z;
        DALib::Engine->SetInstancePosition(ToggleMesh.EngineInstance, &Placed);
        DALib::Engine->UpdateInstance(ToggleMesh.EngineInstance, 0, nullptr);
        if (HasInstance(this))
        {
            Transform Xform;
            memcpy(&Xform, DALib::Engine->GetInstanceTransform(MeshHolder.EngineInstance),
                   sizeof(Xform));
            DALib::Engine->SetInstanceOrientation(ToggleMesh.EngineInstance, &Xform.Rotation);
            DALib::Engine->UpdateInstance(ToggleMesh.EngineInstance, 0, nullptr);
        }
    }
    RefreshAttachment(); // slot 35
}

bool NnToggleButtonControl::Create(Win3D* NewParent, void* Data)
{
    // FUN_0055bfd0 (RET 8): montage mode first, then ButtonControl's INI parser.
    MontageMode = 1; // +0x335
    bool const bResult = NnButtonControl::Create(NewParent, Data); // FUN_0055a6b0

    // A non-empty "mesh2" goes into the +0x378 holder (FUN_0059ec80(path, 0, 0)),
    // whose instance takes the main holder's floats +0x94 as user data (Engine vt+0x7c).
    if (Mesh2.c_str()[0] != '\0')
    {
        ClientMeshLoadFromPath(&ToggleMesh, Mesh2.c_str(), 0, false);
        DALib::Engine->SetInstanceUserData(ToggleMesh.EngineInstance, &MeshHolder.UserFloats[0]);
        PlaceToggleMesh();
    }

    // State 0, both state sounds from "button_sound", and their UI slots.
    ToggleState = 0;
    SoundState0 = SoundName;
    SoundState1 = SoundName;
    UiSoundSlotState0 = FindUiSoundSlot(SoundState0.c_str()); // FUN_00564610
    UiSoundSlotState1 = FindUiSoundSlot(SoundState1.c_str());
    return bResult;
}

void NnToggleButtonControl::Init(char const* NewMeshPath, char const* NewMesh2,
                                 char const* NewSoundName, char const* KeyCommandName,
                                 Vector const* NewOffset, char const* Hardpoint,
                                 Vector const* NewHalfExtents, unsigned char bNoMeshRender,
                                 unsigned char bActionButton, unsigned char bNoColorChange,
                                 unsigned char bNewNoZEnable, int NewToolTipIds,
                                 int NewToolTip2Ids)
{
    // FUN_0055c1c0 (RET 0x34).
    MontageMode = 1; // +0x335
    NnButtonControl::Init(NewMeshPath, NewMesh2, NewSoundName, KeyCommandName, NewOffset,
                          Hardpoint, NewHalfExtents, bNoMeshRender, bActionButton,
                          bNoColorChange, bNewNoZEnable, NewToolTipIds, NewToolTip2Ids,
                          0); // FUN_0055ac30

    // A non-null NewMesh2 (only null is tested here) into the +0x378 holder.
    if (NewMesh2 != nullptr)
    {
        ClientMeshLoadFromPath(&ToggleMesh, NewMesh2, 0, false); // FUN_0059ec80(path, 0, 0)
        DALib::Engine->SetInstanceUserData(ToggleMesh.EngineInstance, &MeshHolder.UserFloats[0]);
        PlaceToggleMesh();
    }

    // As Create's tail.
    ToggleState = 0;
    SoundState0 = SoundName;
    SoundState1 = SoundName;
    UiSoundSlotState0 = FindUiSoundSlot(SoundState0.c_str());
    UiSoundSlotState1 = FindUiSoundSlot(SoundState1.c_str());
}

void NnToggleButtonControl::SetPosition(Vector const* NewPosition)
{
    // LAB_0055c510 (RET 4): ButtonControl's, then with both instances bound the main
    // instance's whole transform onto the +0x378 one (Engine vt+0x9c).
    NnButtonControl::SetPosition(NewPosition); // FUN_0055b4a0
    if (!HasToggleInstance(this))
        return;
    if (!HasInstance(this))
        return;
    Transform Xform;
    memcpy(&Xform, DALib::Engine->GetInstanceTransform(MeshHolder.EngineInstance),
           sizeof(Xform));
    DALib::Engine->SetInstanceMatrix(ToggleMesh.EngineInstance, &Xform);
    DALib::Engine->UpdateInstance(ToggleMesh.EngineInstance, 0, nullptr);
}

void NnToggleButtonControl::SetOrientation(Matrix const* NewOrientation)
{
    // LAB_0055c5a0 (RET 4): the new orientation times the attach orientation (slot 40) through
    // FUN_0051a140 into +0x3c, onto the main instance, then the main instance's transform onto
    // the +0x378 one.
    bAttached = 1; // +0x328
    float Attach[9];
    GetAttachOrientation(reinterpret_cast<Matrix*>(Attach));
    float Result[9];
    CockpitMulMatrix(Result, &NewOrientation->E[0][0], Attach); // FUN_0051a140
    memcpy(&Orientation, Result, sizeof(Orientation));
    if (HasInstance(this))
    {
        DALib::Engine->SetInstanceOrientation(MeshHolder.EngineInstance, &Orientation);
        DALib::Engine->UpdateInstance(MeshHolder.EngineInstance, 0, nullptr);
    }
    if (!HasToggleInstance(this))
        return;
    if (!HasInstance(this))
        return;
    Transform Xform;
    memcpy(&Xform, DALib::Engine->GetInstanceTransform(MeshHolder.EngineInstance),
           sizeof(Xform));
    DALib::Engine->SetInstanceOrientation(ToggleMesh.EngineInstance, &Xform.Rotation);
    DALib::Engine->UpdateInstance(ToggleMesh.EngineInstance, 0, nullptr);
}

void NnToggleButtonControl::Update()
{
    // LAB_0055c690. The +0x378 mesh is loaded late when it is still unbound and
    // "mesh2" is set -- without the user data Create / Init give it.
    if (!ClientMeshHolderHasInstance(&ToggleMesh) && Mesh2.c_str()[0] != '\0') // FUN_00420d60
    {
        ClientMeshLoadFromPath(&ToggleMesh, Mesh2.c_str(), 0, false); // FUN_0059ec80(path, 0, 0)
        PlaceToggleMesh();
    }

    Win3D::Update(); // FUN_005a17b0

    // Under the cached mouse ray (FUN_0059da70, slot 4) the tooltip of the current
    // state: +0x32c in state 1, else +0xcc -- each only when non-zero.
    Vector MouseRay;
    Win3DGetMouseRay(&MouseRay);
    if (HitTest(&MouseRay))
    {
        if (ToggleState == 1)
        {
            if (ToolTip2Ids != 0)
                FeedToggleToolTip(this, ToolTip2Ids);
        }
        else if (ToolTipIds != 0)
        {
            FeedToggleToolTip(this, ToolTipIds);
        }
    }

    // While the button holds the capture -- the cursor ray (FUN_004bdd40 on
    // DAT_0067dbf8, the 3DMathEngine's TransformMatrix with a copy of the camera orientation,
    // normalised by 1 / sqrt), then slot 4. Unlike ButtonControl's LAB_0055b660 no +0x330 bit is
    // written: on the button it tests bit 0 (not bit 2), off it tests bit 2.
    if ((ButtonBits & 0x10) != 0)
    {
        Vector Ray;
        CursorRayThroughMathEngine(&Ray);

        bool const bHit = HitTest(&Ray); // slot 4
        unsigned char const Bits = ButtonBits; // read after the call (0x0055cb68)
        if (bHit)
        {
            if ((Bits & 0x01) == 0)
            {
                TellParent(this, kWinNotifyCapturedEnter);
                if (Parent != nullptr)
                {
                    NnUiPulseReset(); // FUN_0059d580
                    TellParent(this, kWinNotifyHighlight);
                }
            }
        }
        else if ((Bits & 0x04) != 0)
        {
            TellParent(this, kWinNotifyCapturedLeave);
            TellParent(this, kWinNotifyUnhighlight);
        }
    }

    // The blink, as ButtonControl's.
    if (bBlink == 0)
        return;
    double Timer = GetClientFrameTimeCapped() + static_cast<double>(BlinkTimer); // FUN_0042d680
    BlinkTimer = static_cast<float>(Timer);
    if (!(Timer > kOne))
        return;
    unsigned char Phase = bBlinkPhase;
    do
    {
        Timer = Timer - kOne;
        Phase = (Phase == 0) ? 1 : 0;
    } while (Timer > kOne);
    BlinkTimer = static_cast<float>(Timer);
    bBlinkPhase = Phase;
}

void NnToggleButtonControl::Draw()
{
    // LAB_0055c3b0: in state 1 ButtonControl's Draw runs with the +0x378 instance standing in
    // for +0x84 (restored after); the instance drawn is kept for the glow pass below.
    InstanceIndex Instance = 0;
    if (ToggleState == 1)
    {
        Instance = ToggleMesh.EngineInstance;
        const InstanceIndex Saved = MeshHolder.EngineInstance;
        MeshHolder.EngineInstance = Instance;
        NnButtonControl::Draw(); // FUN_0055af90
        MeshHolder.EngineInstance = Saved;
    }
    else
    {
        Instance = MeshHolder.EngineInstance;
        NnButtonControl::Draw(); // FUN_0055af90
    }

    Win3DDisable(kWin3DDepthTest); // FUN_00421ed0(0xb71)
    // After a glow pass (+0x348 non-zero) the glow again, half a turn about y --
    // the orientation {C,0,S, 0,1,0, -S,0,C} of DAT_005e1738 replaces the instance's (updated),
    // the tinted submit FUN_00434630 in the glow colour, and the saved orientation goes back
    // without an update.
    if (GlowArgb != 0 && Instance != InvalidInstanceIndex)
    {
        Matrix Saved;
        memcpy(&Saved, DALib::Engine->GetInstanceOrientation(Instance), sizeof(Saved)); // vt+0x98
        float const C = static_cast<float>(cos(kToggleGlowAngle));
        float const S = static_cast<float>(sin(kToggleGlowAngle));
        Matrix Turned;
        Turned.E[0][0] = C;
        Turned.E[0][1] = 0.0f;
        Turned.E[0][2] = S;
        Turned.E[1][0] = 0.0f;
        Turned.E[1][1] = 1.0f;
        Turned.E[1][2] = 0.0f;
        Turned.E[2][0] = -S;
        Turned.E[2][1] = 0.0f;
        Turned.E[2][2] = C;
        DALib::Engine->SetInstanceOrientation(Instance, &Turned);
        DALib::Engine->UpdateInstance(Instance, 0, nullptr);
        unsigned char Glow[4];
        Glow[0] = static_cast<unsigned char>(GlowArgb >> 16);
        Glow[1] = static_cast<unsigned char>(GlowArgb >> 8);
        Glow[2] = static_cast<unsigned char>(GlowArgb);
        Glow[3] = static_cast<unsigned char>(GlowArgb >> 24);
        SubmitWin3DTintedEngineInstanceWrapped(Win3DGetCamera(), Instance, 1, Glow, 1, 1.0f,
                                               0); // FUN_00434630
        DALib::Engine->SetInstanceOrientation(Instance, &Saved);
    }
    Win3DDisable(kWin3DDepthTest); // FUN_00421ed0(0xb71)
}

bool NnToggleButtonControl::MouseEvent(FWin3DMouseEvent const* Event)
{
    // LAB_0055cd20 (RET 4): the button sound and its UI slot of the current state, then
    // ButtonControl's handler.
    if (ToggleState == 0)
    {
        SoundName = SoundState0;
        UiSoundSlot = UiSoundSlotState0;
    }
    else
    {
        SoundName = SoundState1;
        UiSoundSlot = UiSoundSlotState1;
    }
    return NnButtonControl::MouseEvent(Event); // FUN_0055b950
}

intptr_t NnToggleButtonControl::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_0055cf90.
    switch (Msg)
    {
    case kWinPropOffset:
    {
        // The offset from A; with a MAIN instance bound the +0x378 instance moves to
        // +0x60 (+ +0xa8 under +0x2f4 bit 0). Then ButtonControl's 6 as well.
        float const* const NewOffset = reinterpret_cast<float const*>(A);
        Offset.x = NewOffset[0];
        Offset.y = NewOffset[1];
        Offset.z = NewOffset[2];
        if (HasInstance(this))
        {
            Vector Placed;
            if ((PositionFlags & 1) != 0)
            {
                Placed.x = Offset.x + Position.x;
                Placed.y = Offset.y + Position.y;
                Placed.z = Offset.z + Position.z;
            }
            else
            {
                Placed.x = Position.x;
                Placed.y = Position.y;
                Placed.z = Position.z;
            }
            DALib::Engine->SetInstancePosition(ToggleMesh.EngineInstance, &Placed);
            DALib::Engine->UpdateInstance(ToggleMesh.EngineInstance, 0, nullptr);
        }
        return NnButtonControl::SetProperty(Msg, A, B); // FUN_0055bc30
    }
    case 0x2f:
        *reinterpret_cast<int*>(A) = ToggleState;
        return 0;
    case 0x30:
        // SETZ: a zero A selects state 1.
        ToggleState = (A == 0) ? 1 : 0;
        return 0;
    case 0x31:
        // The two state sounds, each only when its pointer is non-null.
        if (A != 0)
        {
            SoundState0.assign(reinterpret_cast<char const*>(A));
            UiSoundSlotState0 = FindUiSoundSlot(SoundState0.c_str()); // FUN_00564610
        }
        if (B != 0)
        {
            SoundState1.assign(reinterpret_cast<char const*>(B));
            UiSoundSlotState1 = FindUiSoundSlot(SoundState1.c_str());
        }
        return 0;
    default:
        return NnButtonControl::SetProperty(Msg, A, B); // FUN_0055bc30
    }
}

void NnToggleButtonControl::Activate()
{
    // FUN_0055cc90: ButtonControl's FUN_0055a660 body inlined (the command, else the parent's
    // 0x26), then the state flips and the parent hears 0x30 with the new state as B.
    NnButtonControl::Activate();
    ToggleState = (ToggleState == 0) ? 1 : 0;
    TellParentWithValue(this, 0x30, static_cast<uintptr_t>(ToggleState));
}

