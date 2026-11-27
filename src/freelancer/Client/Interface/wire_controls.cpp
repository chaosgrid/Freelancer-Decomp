// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/wire_controls.h"

#include "NeuroNet/Chat/chat_init.h"        // kTextStyleButton
#include "Core/game_options.h"        // GetGameOption (FUN_005792b0)
#include "Core/globals.h"             // GetClientFrameTimeCapped (FUN_0042d680)
#include "Space/HUD/hud_command_dispatch.h" // HudPostCommand (FUN_005763b0)
#include "Common/Fonts/ids_rdl.h"             // LoadIdsIntoRdl (FUN_0057da40), RdlAppendNode (FUN_0046c800)
#include "Common/input_manager.h"       // GetClientSoftwareCursor (DAT_00616840 / DAT_00616844)
#include "Common/Audio/menu_sound_arch.h"     // LookupClientSoundArch / PlayClientSoundArchIfIdle
#include "Interface/menu_ui_init.h"        // FindUiSoundSlot / PlayOrRewindUiSoundSlot / PlayUiRolloverSound
#include "NeuroNet/nn_tooltip.h"          // NnToolTipHideGlobal (FUN_004be390)
#include "NeuroNet/nn_ui_pulse.h"         // NnUiPulseReset / NnUiPulseBlendArgb / idle palette
#include "Space/Cameras/space_camera.h"        // FSpaceCamera (DAT_0067dbf8)
#include "Interface/tooltip_hover.h"       // FToolTipHoverInfo / NnToolTipFeedHover (FUN_004be370)
#include "Interface/win3d.h"
#include "Interface/win3d_renderables.h"   // Win3DImmedBatchFlushAll / Win3DImmedBatchReset
#include "Interface/win_factory.h"

#include "common_csys.h"         // Acquire3DMathEngine (FUN_005b73e0)
#include "common_id.h"           // CreateID
#include "common_text.h"
#include "x86math.h"

#include <math.h>
#include <string.h>
#include <windows.h>

// Retail DAT_0067de10 / DAT_0067dde4 / DAT_0067ddb8 -- the three WINFACTORY<T> statics
// (initializers LAB_005a2830 / LAB_005a2860 / LAB_005a2890).
WINFACTORY<WireStaticControl> G_WireStaticControlFactory("WireStaticControl");
WINFACTORY<WireButtonControl> G_WireButtonControlFactory("WireButtonControl");
WINFACTORY<RichTextWireButtonControl> G_RichTextWireButtonControlFactory(
    "RichTextWireButtonControl");

namespace
{
    // DAT_0067de48 -- acquired on first use, shared by SetOrientation and MouseEvent.
    I3DMathEngine* G_WireMathEngine = nullptr;

    I3DMathEngine* WireMathEngine()
    {
        if (G_WireMathEngine == nullptr)
            G_WireMathEngine = Acquire3DMathEngine();
        return G_WireMathEngine;
    }

    // DAT_005c75dc
    float const kOne = 1.0f;
    // 0x3b449ba6
    float const kWireLineWidth = 0.003f;

    // FUN_005a2a40 on the batcher DAT_0067dd90.
    void WireBatchAddLine(Vector const* From, Vector const* To, unsigned long Argb,
                          unsigned long Rgb, float Width)
    {
        G_ImmedBatchA.AddLine(From, To, Argb, Rgb, Width);
    }

    void SetIdentity9(Matrix* Out)
    {
        Out->E[0][0] = 1.0f;
        Out->E[0][1] = 0.0f;
        Out->E[0][2] = 0.0f;
        Out->E[1][0] = 0.0f;
        Out->E[1][1] = 1.0f;
        Out->E[1][2] = 0.0f;
        Out->E[2][0] = 0.0f;
        Out->E[2][1] = 0.0f;
        Out->E[2][2] = 1.0f;
    }

    // The parent notification every WireButtonControl handler inlines. Returns the parent's
    // answer, or 0 when there is no WinControl parent.
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

    // The ToolTip record FUN_005a3c30 / FUN_005a46c0 build; retail writes the key and alt id
    // twice.
    void FeedWireToolTip(WireStaticControl* Self, int Aux)
    {
        FToolTipHoverInfo Info;
        Info.IdsAlt = 0;                  // +0x04
        Info.Ids = -1;                    // +0x00
        Info.AuxField = 0;                // +0x44
        Info.Ids = Self->ToolTipIds;      // +0x00 <- +0xcc
        Info.IdsAlt = Self->ToolTipIds;   // +0x04 <- +0xcc
        Info.AuxField = Aux;              // +0x44
        Info.WorldPos[0] = Self->WireCenter[0];   // +0x48 <- +0x350
        Info.WorldPos[1] = Self->WireCenter[1];
        Info.WorldPos[2] = Self->WireCenter[2];
        Info.HalfExtents[0] = Self->HalfExtents.x; // +0x54 <- +0xb4
        Info.HalfExtents[1] = Self->HalfExtents.y;
        Info.HalfExtents[2] = Self->HalfExtents.z;
        Info.bLive = 1;                   // +0x60
        NnToolTipFeedHover(&Info);
    }

    // The sound-name store of FUN_005a3ee0 / FUN_005a4810: only a non-empty name replaces
    // +0x368, and then FUN_00564610 looks it up in the UI sound table.
    void StoreSoundName(WireStaticControl* Self, char const* NewSoundName)
    {
        if (NewSoundName == nullptr || NewSoundName[0] == '\0')
            return;
        Self->SoundName.assign(NewSoundName);
        Self->SoundSlot = FindUiSoundSlot(Self->SoundName.c_str());
    }
}

// ---------------------------------------------------------------------------------------
// WireStaticControl
// ---------------------------------------------------------------------------------------

WireStaticControl::WireStaticControl()
{
    // FUN_005a3210 after FUN_0059f860.
    SetIdentity9(&WireOrientation);
    WireCenter[0] = 0.0f;
    WireCenter[1] = 0.0f;
    WireCenter[2] = 0.0f;
    Colour[0] = 0;
    Colour[1] = 0;
    Colour[2] = 0;
    Colour[3] = 0;
    SoundSlot = 0;
    bUseColour = 0;
    // Only bit 0 of +0x35d is cleared; the other bits keep whatever the allocation held. Only
    // bit 0 is ever read.
    WireState2 = static_cast<unsigned char>(WireState2 & ~kWire2FlushBatchers);
    bBlink = 0;
    bBlinkPhase = 0;
    BlinkTimer = 0.0f;
    WireState = kWireLitWhilePressed | kWirePulseWhileHovered;
    // +0x364 (ToolTipAux) and +0x384 (EdgeMask) are not written here; Init writes the mask.
}

WireStaticControl::~WireStaticControl()
{
    // FUN_005a3310.
}

bool WireStaticControl::HitTest(Vector const* Ray)
{
    // FUN_0053f330.
    return Win3D::HitTest(Ray);
}

void WireStaticControl::SetPosition(Vector const* NewPosition)
{
    // LAB_005a3950 (RET 4).
    RequestedPosition.x = NewPosition->x;
    RequestedPosition.y = NewPosition->y;
    RequestedPosition.z = NewPosition->z;
    bAttached = 1;

    Vector Placed;
    if ((PositionFlags & kWinPositionAddOffset) != 0)
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

    if ((PositionFlags & kWinPositionAddOffset) != 0)
    {
        WireCenter[0] = Offset.x + Position.x;
        WireCenter[1] = Offset.y + Position.y;
        WireCenter[2] = Offset.z + Position.z;
    }
    else
    {
        WireCenter[0] = Position.x;
        WireCenter[1] = Position.y;
        WireCenter[2] = Position.z;
    }
}

void WireStaticControl::SetOrientation(Matrix const* NewOrientation)
{
    // LAB_005a3a30. Unlike WinControl's FUN_005a0950 it neither moves an engine instance nor
    // recurses into children.
    bAttached = 1;
    Matrix Attach;
    GetAttachOrientation(&Attach);
    Matrix Result;
    WireMathEngine()->MulMatrix(Result, *NewOrientation, Attach);
    memcpy(&Orientation, &Result, sizeof(Orientation));
    memcpy(&WireOrientation, &Result, sizeof(WireOrientation));
}

bool WireStaticControl::Create(Win3D* NewParent, void* /*Data*/)
{
    // FUN_005a3ea0: re-placing the parent at its own position positions this control with it.
    Win3DReparent(this, NewParent); // FUN_0059dd00
    Win3DRegister(this);            // FUN_0059dbf0
    Flags = static_cast<unsigned char>((Flags & ~kWin3DUnregistered) | kWin3DActive | kWin3DVisible);
    if (Parent != nullptr)
        Parent->SetPosition(&Parent->Position);
    return true;
}

void WireStaticControl::UpdateBlink()
{
    // Every whole second past 1.0 flips the phase. The compares are on the double sum; an
    // unordered result stops.
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

void WireStaticControl::Update()
{
    // FUN_005a3aa0.
    Win3D::Update();
    UpdateBlink();
}

void WireStaticControl::Draw()
{
    // LAB_005a3420.
    if (GetGameOption(kGameOptInterfaceGroup) == 0) // FUN_005792b0(6)
        return;
    if (GetGameOption(kGameOptNeuronet) == 0)       // FUN_005792b0(9)
        return;
    if ((Flags & kWin3DVisible) == 0)
        return;
    if ((WireState & kWireDrawInactive) == 0 && (Flags & kWin3DActive) == 0)
        return;
    if (bBlink != 0 && bBlinkPhase != 0)
        return;

    DrawChildren(); // FUN_0059e5c0

    unsigned char const SavedLit = static_cast<unsigned char>((WireState >> 4) & 1);
    unsigned char Rgba[4];
    if ((Flags & kWin3DActive) == 0)
    {
        WireState = static_cast<unsigned char>(WireState & ~(kWirePressed | kWireLit));
        Rgba[0] = 0x80;
        Rgba[1] = 0x80;
        Rgba[2] = 0x80;
        Rgba[3] = 0xff;
    }
    else if (SavedLit != 0 || ((WireState & kWireLitWhilePressed) != 0 && (WireState & kWirePressed) != 0))
    {
        Rgba[0] = 0xff;
        Rgba[1] = 0xff;
        Rgba[2] = 0x00;
        Rgba[3] = 0xff;
    }
    else if ((WireState & kWireFocused) != 0 || ((WireState & kWireHovered) != 0 && (WireState & kWirePulseWhileHovered) != 0))
    {
        // FUN_00561da0 twice: the first result is the colour, the second lands in a scratch
        // dword nobody reads.
        unsigned long Pulse = 0;
        NnUiPulseBlendArgb(&Pulse);
        unsigned long Scratch = 0;
        NnUiPulseBlendArgb(&Scratch);
        memcpy(Rgba, &Pulse, sizeof(Rgba));
    }
    else
    {
        memcpy(Rgba, &G_NnPaletteCyan, sizeof(Rgba)); // DAT_00679bb0
    }
    if ((WireState & kWireAlwaysWhite) != 0)
    {
        Rgba[0] = 0xff;
        Rgba[1] = 0xff;
        Rgba[2] = 0xff;
        Rgba[3] = 0xff;
    }
    if (bUseColour != 0)
        memcpy(Rgba, Colour, sizeof(Rgba));

    float const Center[3] = {Position.x + Offset.x, Position.y + Offset.y,
                             Position.z + Offset.z};
    WireCenter[0] = Center[0];
    WireCenter[1] = Center[1];
    WireCenter[2] = Center[2];
    float CornerA[3] = {-HalfExtents.x, -HalfExtents.y, -HalfExtents.z};
    float CornerB[3] = {-CornerA[0], CornerA[1], CornerA[2]};
    unsigned long const Argb = (static_cast<unsigned long>(Rgba[3]) << 24) |
                               (static_cast<unsigned long>(Rgba[0]) << 16) |
                               (static_cast<unsigned long>(Rgba[1]) << 8) |
                               static_cast<unsigned long>(Rgba[2]);
    unsigned long const Rgb = (static_cast<unsigned long>(Rgba[0]) << 16) |
                              (static_cast<unsigned long>(Rgba[1]) << 8) |
                              static_cast<unsigned long>(Rgba[2]);

    // Bit 2, the bottom edge, (-hx, -hy) -> (hx, -hy).
    if ((EdgeMask & kWireEdgeBottom) != 0)
    {
        Vector const To = {Center[0] + CornerB[0], Center[1] + CornerB[1],
                           Center[2] + CornerB[2]};
        Vector const From = {Center[0] + CornerA[0], Center[1] + CornerA[1],
                             Center[2] + CornerA[2]};
        WireBatchAddLine(&From, &To, Argb, Rgb, kWireLineWidth);
    }
    // Bit 1, the right edge, (hx, -hy) -> (hx, hy).
    CornerA[0] = -CornerA[0];
    CornerA[1] = -CornerA[1];
    if ((EdgeMask & kWireEdgeRight) != 0)
    {
        Vector const To = {Center[0] + CornerA[0], Center[1] + CornerA[1],
                           Center[2] + CornerA[2]};
        Vector const From = {Center[0] + CornerB[0], Center[1] + CornerB[1],
                             Center[2] + CornerB[2]};
        WireBatchAddLine(&From, &To, Argb, Rgb, kWireLineWidth);
    }
    // Bit 0, the top edge, (hx, hy) -> (-hx, hy).
    CornerB[0] = -CornerB[0];
    CornerB[1] = -CornerB[1];
    if ((EdgeMask & kWireEdgeTop) != 0)
    {
        Vector const To = {Center[0] + CornerB[0], Center[1] + CornerB[1],
                           Center[2] + CornerB[2]};
        Vector const From = {Center[0] + CornerA[0], Center[1] + CornerA[1],
                             CornerA[2] + Center[2]};
        WireBatchAddLine(&From, &To, Argb, Rgb, kWireLineWidth);
    }
    // Bit 3, the left edge, (-hx, hy) -> (-hx, -hy).
    CornerA[0] = -HalfExtents.x;
    CornerA[1] = -HalfExtents.y;
    CornerA[2] = -HalfExtents.z;
    if ((EdgeMask & kWireEdgeLeft) != 0)
    {
        Vector const To = {CornerA[0] + Center[0], CornerA[1] + Center[1],
                           CornerA[2] + Center[2]};
        Vector const From = {Center[0] + CornerB[0], Center[1] + CornerB[1],
                             Center[2] + CornerB[2]};
        WireBatchAddLine(&From, &To, Argb, Rgb, kWireLineWidth);
    }

    // Bit 4 goes back to what it was before the inactive branch could clear it.
    WireState = static_cast<unsigned char>((WireState & ~kWireLit) | (SavedLit << 4));
    PostDraw(); // vt+0x9c

    // FUN_005a29b0 inlined: flush both batchers (DAT_0067dd90, DAT_0067dd68).
    if ((WireState2 & kWire2FlushBatchers) != 0)
    {
        Win3DImmedBatchFlushAll();
        Win3DImmedBatchReset();
    }
}

void WireStaticControl::ShowToolTip()
{
    // FUN_005a3c30.
    FeedWireToolTip(this, ToolTipAux);
}

intptr_t WireStaticControl::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_005a3b00.
    switch (Msg)
    {
    case kWinPropLit:
        WireState = static_cast<unsigned char>((WireState & ~kWireLit) | ((A != 0) ? kWireLit : 0));
        return 0;
    case kWinNotifyHighlight:
        WireState = static_cast<unsigned char>(WireState | kWireFocused);
        return 0;
    case kWinNotifyUnhighlight:
        WireState = static_cast<unsigned char>(WireState & ~kWireFocused);
        return 0;
    case kWinPropColour:
        memcpy(Colour, reinterpret_cast<void const*>(A), sizeof(Colour));
        bUseColour = (B == 1) ? 1 : 0;
        return 0;
    case kWinPropDrawInactive:
        WireState = static_cast<unsigned char>(WireState & ~kWireDrawInactive);
        if (A != 0)
            WireState = static_cast<unsigned char>(WireState | kWireDrawInactive);
        return 0;
    case kWinPropBlink:
        bBlink = 0;
        if (A != 0)
            bBlink = 1;
        return 0;
    default:
        return WinControl::SetProperty(Msg, A, B);
    }
}

void WireStaticControl::Init(Vector const* NewOffset, Vector const* NewHalfExtents,
                             unsigned char bAlwaysWhite, unsigned char bNewNoZEnable,
                             int NewToolTipIds, unsigned NewEdgeMask)
{
    // FUN_005a3360.
    EdgeMask = (NewEdgeMask != 0) ? NewEdgeMask : kWireEdgeAll;
    Offset.x = NewOffset->x;
    Offset.y = NewOffset->y;
    Offset.z = NewOffset->z;
    WireCenter[0] = Offset.x + Position.x;
    WireCenter[1] = Offset.y + Position.y;
    WireCenter[2] = Offset.z + Position.z;
    HalfExtents.x = NewHalfExtents->x;
    HalfExtents.y = NewHalfExtents->y;
    HalfExtents.z = NewHalfExtents->z;
    WireState = static_cast<unsigned char>((WireState & ~kWireAlwaysWhite) | ((bAlwaysWhite << 3) & kWireAlwaysWhite));
    bNoZEnable = bNewNoZEnable;
    ToolTipIds = NewToolTipIds;
}

// ---------------------------------------------------------------------------------------
// WireButtonControl
// ---------------------------------------------------------------------------------------

WireButtonControl::WireButtonControl()
{
    // Bits 1 and 3..7 keep what the allocation held (bit 1 is written by every press before it
    // is read).
    ButtonState = static_cast<unsigned char>((ButtonState & ~kWireButtonCaptured) | kWireButtonEnabled);
    CommandId = 0;
}

WireButtonControl::~WireButtonControl()
{
    // FUN_005a3db0.
    if ((ButtonState & kWireButtonCaptured) != 0)
        Win3DRemoveCapture(this);
}

bool WireButtonControl::HitTest(Vector const* Ray)
{
    // LAB_005a42c0.
    return WinControl::HitTest(Ray);
}

void WireButtonControl::Update()
{
    // LAB_005a4080.
    Win3D::Update(); // FUN_005a17b0
    UpdateBlink();

    // While the button holds the capture, light / unlight as the cursor moves on and off.
    if ((ButtonState & kWireButtonCaptured) == 0)
        return;

    FSpaceCamera* const Cam = Win3DGetCamera(); // DAT_0067dbf8
    int CursorX = 0;
    int CursorY = 0;
    GetClientSoftwareCursor(&CursorX, &CursorY, nullptr); // DAT_00616840 / DAT_00616844
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
    Vector Ray;
    Ray.x = static_cast<float>(World.x * Inv);
    Ray.y = static_cast<float>(World.y * Inv);
    Ray.z = static_cast<float>(World.z * Inv);

    if (HitTest(&Ray)) // slot 4
    {
        if ((WireState & kWirePressed) != 0)
            return;
        WireState = static_cast<unsigned char>(WireState | kWirePressed);
        TellParent(this, kWinNotifyCapturedEnter);
        if (Parent == nullptr)
            return;
        NnUiPulseReset(); // FUN_0059d580
        TellParent(this, kWinNotifyHighlight);
        return;
    }
    if ((WireState & kWirePressed) == 0)
        return;
    WireState = static_cast<unsigned char>(WireState & ~kWirePressed);
    TellParent(this, kWinNotifyCapturedLeave);
    TellParent(this, kWinNotifyUnhighlight);
}

void WireButtonControl::MouseEnter(Vector const* /*Ray*/)
{
    // FUN_005a42d0.
    if (Parent != nullptr)
    {
        NnUiPulseReset(); // FUN_0059d580
        TellParent(this, kWinNotifyHighlight);
    }
    WireState = static_cast<unsigned char>(WireState | kWireHovered);
    if ((ButtonState & kWireButtonEnabled) != 0)
        PlayUiRolloverSound(); // FUN_00564700
}

void WireButtonControl::MouseLeave(Vector const* /*Ray*/)
{
    // FUN_005a4330.
    if (ToolTipIds != 0)
        NnToolTipHideGlobal(); // FUN_004be390
    TellParent(this, kWinNotifyUnhighlight);
    WireState = static_cast<unsigned char>(WireState & ~kWireHovered);
}

bool WireButtonControl::MouseEvent(FWin3DMouseEvent const* Event)
{
    // FUN_005a4380.
    if ((ButtonState & kWireButtonEnabled) == 0)
        return false;
    // While some control holds the mouse, a button whose WinControl parent does not hold it
    // ignores the event.
    if (Parent != nullptr && Win3DAnyCaptured())
    {
        WinControl* const ParentControl = Parent->AsWinControl();
        if (ParentControl != nullptr && ParentControl->bCaptured == 0)
            return false;
    }

    if ((ButtonState & kWireButtonCaptured) != 0)
    {
        // The release of a press this button captured.
        if (Event->Button != kWin3DMouseLeft || Event->Action != kWin3DMouseRelease)
            return false;
        WireState = static_cast<unsigned char>(WireState & ~kWirePressed);
        ButtonState = static_cast<unsigned char>(ButtonState & ~kWireButtonCaptured);
        Win3DRemoveCapture(this); // FUN_0059d960

        // The cursor ray (FUN_004bdd40); unlike Update this goes through the math engine.
        FSpaceCamera* const Cam = Win3DGetCamera();
        int CursorX = 0;
        int CursorY = 0;
        GetClientSoftwareCursor(&CursorX, &CursorY, nullptr);
        Vector Local;
        Cam->ScreenToNearPlane(Local, static_cast<float>(CursorX),
                               static_cast<float>(CursorY));
        Matrix CamOrientation;
        memcpy(&CamOrientation, &Cam->World, sizeof(CamOrientation));
        Vector World;
        WireMathEngine()->TransformMatrix(World, CamOrientation, Local);
        double const Inv =
            kOne / sqrt(static_cast<double>(World.x) * World.x +
                        static_cast<double>(World.y) * World.y +
                        static_cast<double>(World.z) * World.z);
        Vector Ray;
        Ray.x = static_cast<float>(World.x * Inv);
        Ray.y = static_cast<float>(World.y * Inv);
        Ray.z = static_cast<float>(World.z * Inv);

        if (!HitTest(&Ray)) // slot 4
        {
            // Released off the button.
            TellParent(this, kWinNotifyRelease);
            WireState = static_cast<unsigned char>(WireState & ~kWireHovered);
            return true;
        }

        // The click sound: the UI sound slot, else the sound archetype by name.
        if (SoundSlot != -1)
        {
            PlayOrRewindUiSoundSlot(static_cast<unsigned>(SoundSlot)); // FUN_005645c0
        }
        else
        {
            ID_String SoundId;
            SoundId.Id = CreateID(SoundName.c_str());
            Archetype::ISound* const Arch = LookupClientSoundArch(&SoundId); // FUN_0042ae40
            PlayClientSoundArchIfIdle(Arch, nullptr, nullptr);          // FUN_004285f0
        }

        if ((ButtonState & kWireButtonDoubleClick) != 0)
        {
            // A double click asks the parent 0x27 first; a zero answer becomes 0x26.
            if (Parent == nullptr)
                return true;
            WinControl* const ParentControl = Parent->AsWinControl();
            if (ParentControl == nullptr)
                return true;
            if (ParentControl->SetProperty(kWinNotifyDoubleClick, reinterpret_cast<uintptr_t>(AsWinControl()),
                                           0) != 0)
                return true;
            ParentControl->SetProperty(kWinNotifyClick, reinterpret_cast<uintptr_t>(AsWinControl()), 0);
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
        ButtonState = static_cast<unsigned char>(ButtonState | kWireButtonDoubleClick);
    else
        ButtonState = static_cast<unsigned char>(ButtonState & ~kWireButtonDoubleClick);
    TellParent(this, kWinNotifyPress);
    WireState = static_cast<unsigned char>(WireState | kWirePressed);
    ButtonState = static_cast<unsigned char>(ButtonState | kWireButtonCaptured);
    if (Parent != nullptr)
    {
        NnUiPulseReset(); // FUN_0059d580
        TellParent(this, kWinNotifyHighlight);
    }
    Win3DPushCapture(this); // FUN_0059d8d0
    return true;
}

void WireButtonControl::ShowToolTip()
{
    // FUN_005a46c0: the record's aux dword is the command id.
    FeedWireToolTip(this, CommandId);
}

intptr_t WireButtonControl::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_005a4690.
    if (Msg == kButtonPropGetCommand)
    {
        *reinterpret_cast<int*>(A) = CommandId;
        return 0;
    }
    return WireStaticControl::SetProperty(Msg, A, B);
}

void WireButtonControl::Activate()
{
    // FUN_005a3e50.
    if (CommandId != 0)
    {
        HudPostCommand(static_cast<unsigned>(CommandId), 0); // FUN_005763b0
        return;
    }
    TellParent(this, kWinNotifyClick);
}

void WireButtonControl::Init(char const* NewSoundName, Vector const* NewOffset,
                             Vector const* NewHalfExtents, unsigned char bAlwaysWhite,
                             unsigned char bNewNoZEnable, int NewToolTipIds, int NewCommandId,
                             unsigned NewEdgeMask)
{
    // FUN_005a3ee0 (FUN_005a3360 inlined).
    WireStaticControl::Init(NewOffset, NewHalfExtents, bAlwaysWhite, bNewNoZEnable,
                            NewToolTipIds, NewEdgeMask);
    CommandId = NewCommandId;
    StoreSoundName(this, NewSoundName);
}

// ---------------------------------------------------------------------------------------
// RichTextWireButtonControl
// ---------------------------------------------------------------------------------------

RichTextWireButtonControl::RichTextWireButtonControl()
{
    TextWinRef = &TextWin;
    TextWin = nullptr;
}

RichTextWireButtonControl::~RichTextWireButtonControl()
{
}

void RichTextWireButtonControl::Init(char const* NewSoundName, Vector const* NewOffset,
                                     Vector const* NewHalfExtents, unsigned char bAlwaysWhite,
                                     unsigned char bNewNoZEnable, int NewToolTipIds,
                                     unsigned CaptionIds, int NewCommandId,
                                     unsigned NewEdgeMask)
{
    // FUN_005a4810.
    WireStaticControl::Init(NewOffset, NewHalfExtents, bAlwaysWhite, bNewNoZEnable,
                            NewToolTipIds, NewEdgeMask); // FUN_005a3360
    CommandId = NewCommandId;
    StoreSoundName(this, NewSoundName);

    // "Text" is DAT_005d075c.
    std::string Nickname(Name);
    Nickname.append("Text");
    TextWin = static_cast<RichTextWin*>(
        WinFactoryCreate(Nickname.c_str(), "RichTextWin", this, nullptr)); // FUN_0059db20
    if (TextWin == nullptr)
        return;

    // The caption box sits on the wire box's top-left corner.
    Vector AttachOffset;
    AttachOffset.x = Offset.x - HalfExtents.x;
    AttachOffset.y = Offset.y + HalfExtents.y;
    AttachOffset.z = Offset.z;
    TextWin->SetAttachOffset(&AttachOffset);                               // FUN_00579c40
    TextWin->SetNormSize(HalfExtents.x + HalfExtents.x,
                         HalfExtents.y + HalfExtents.y);                // FUN_00579cc0
    TextWin->SetAutoScrollbar(false);                                     // FUN_005799f0
    TextWin->SetNeverHit(true);                                           // FUN_00579880
    TextWin->SetAlignMode(kRichAlignMiddle);                                             // FUN_00579a60

    if (CaptionIds == 0)
        return;

    // FUN_0057da40 loads the ids without the trailing-paragraph trim; FUN_0057b3e0 appends.
    RenderDisplayList Caption;
    StyleNode Style(kTextStyleButton);
    RdlAppendNode(Caption, Style.Clone());
    JustifyNode Justify(TextJustifyCenter);
    RdlAppendNode(Caption, Justify.Clone());
    LoadIdsIntoRdl(CaptionIds, Caption);
    TextWin->AppendContent(Caption);
}

void WireControlsJoinColumn(WinControl* const* Controls, int Count)
{
    // FUN_005a4b50.
    WireStaticControl* Prev = nullptr;
    for (int Index = 0; Index < Count; ++Index)
    {
        WireStaticControl* const Cur = static_cast<WireStaticControl*>(Controls[Index]);
        WireStaticControl* const Next = (Index + 1 != Count)
                                            ? static_cast<WireStaticControl*>(Controls[Index + 1])
                                            : nullptr;

        // Highlighted (hovered or focused) with no lit (bit 4) neighbour on either side.
        unsigned char const State = Cur->WireState;
        unsigned char const bFocused = static_cast<unsigned char>((State >> 1) & 1);
        unsigned char bAlone = 0;
        if (bFocused != 0 || (State & kWireHovered) != 0)
        {
            bAlone = 1;
            if (Next != nullptr && (Next->WireState & kWireLit) != 0)
                bAlone = 0;
            else if (Prev != nullptr && (Prev->WireState & kWireLit) != 0)
                bAlone = 0;
        }

        // The full box unless the next control is shown, this one is not lit and not
        // highlighted alone.
        if (Next == nullptr || (Next->Flags & kWin3DVisible) == 0 || (Next->Flags & kWin3DActive) == 0
            || (Cur->WireState & kWireLit) != 0 || bAlone != 0)
        {
            Cur->EdgeMask = kWireEdgeAll;
        }
        else if (bFocused == 0 && (Cur->WireState & kWireHovered) == 0)
        {
            if (Prev != nullptr && (Prev->WireState & (kWireHovered | kWireFocused | kWireLit)) != 0)
                Cur->EdgeMask = kWireEdgeRight | kWireEdgeLeft;
            else
                Cur->EdgeMask = kWireEdgeTop | kWireEdgeRight | kWireEdgeLeft;
        }
        else
        {
            Cur->EdgeMask = kWireEdgeRight | kWireEdgeLeft;
            if (Prev == nullptr || (Prev->WireState & kWireLit) == 0)
                Cur->EdgeMask = kWireEdgeTop | kWireEdgeRight | kWireEdgeLeft;
            if ((Next->WireState & kWireLit) == 0)
                Cur->EdgeMask |= kWireEdgeBottom;
        }

        // A coloured control below an uncoloured one takes over the shared edge: the previous
        // one gets its bottom edge, this one loses its top edge.
        if (Prev != nullptr && (Prev->EdgeMask & kWireEdgeBottom) == 0 &&
            (Cur->EdgeMask & kWireEdgeTop) != 0 && Prev->bUseColour == 0 && Cur->bUseColour != 0)
        {
            Prev->EdgeMask |= kWireEdgeBottom;
            Cur->EdgeMask &= ~static_cast<unsigned>(kWireEdgeTop);
        }

        Prev = Cur;
    }
}
