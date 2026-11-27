// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "NeuroNet/nn_text_control.h"
#include "Space/HUD/hud_widescreen.h"   // port-only (FREELANCER_WIDESCREEN_SUPPORT)

#include "Common/Fonts/font_imp.h"
#include "Common/Fonts/font_atlas.h"
#include "Common/Fonts/font_string.h"
#include "Core/game_options.h"
#include "Core/globals.h"                // GetClientFrameTimeCapped (FUN_0042d680)
#include "Common/Fonts/ids_strings.h"
#include "Common/input_manager.h"          // GetClientSoftwareCursor (DAT_00616840 / DAT_00616844)
#include "Interface/menu_ui_init.h"
#include "NeuroNet/nn_ui_pulse.h"
#include "Space/Cameras/space_camera.h"           // FSpaceCamera (DAT_0067dbf8 fields)
#include "Interface/text_win.h"
#include "Interface/tooltip_hover.h"          // FToolTipHoverInfo, NnToolTipFeedHover (FUN_004be370)
#include "Interface/win3d.h"
#include "Interface/win3d_engine_submit.h"    // Win3DRayHitsAabbHalfExtents (FUN_005a1130)
#include "Interface/win3d_immediate.h"
#include "Interface/win_factory.h"

#include "common_csys.h"      // Acquire3DMathEngine (FUN_005b73e0)
#include "common_ini.h"
#include "x86math.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <string>

// Retail DAT_0067d88c -- WINFACTORY<TextControl> (static init LAB_00589af0: FUN_0058c0b0
// "TextControl" + atexit LAB_00589b10 -> FUN_00579380). Its Create FUN_0058c120 (vftable
// 0x005e50d8) is the template's, operator_new(0x3a0).
WINFACTORY<NnTextControl> G_TextControlFactory("TextControl");

namespace
{
    // DAT_0067d8c8 -- how many TextControls hold a typewriter run (+0x389). The ctor clamps it
    // to 0 from below; the dtor and property 0x50 decrement it and stop UI sound slot 0xb when
    // it reaches zero.
    int G_TextControlTypingCount = 0;

    // DAT_0067d8c4 -- TextControl's cached 3D math engine (FUN_005b73e0 on first use).
    I3DMathEngine* G_TextControlMath = nullptr;

    // The second argument every Font::String scale call (FUN_004145d0) of this class gets.
    float TextDepth(NnTextControl const* Self)
    {
        return Self->Offset.z + Self->Position.z;
    }
}

NnTextControl::NnTextControl()
{
    // FUN_00589ba0.
    OverrideColour[0] = 0;
    OverrideColour[1] = 0;
    OverrideColour[2] = 0;
    OverrideColour[3] = 0;
    PendingText = nullptr;
    PendingCapacity = 0;
    for (int Index = 0; Index < 4; ++Index)
    {
        BlinkColourOn[Index] = 0;
        BlinkColourOff[Index] = 0;
    }
    Width = 0.0f;
    Height = 0.0f;
    bHighlighted = 0;
    Justification = 0;
    // DAT_00679bb0 at full alpha.
    TextColour = (G_NnPaletteCyan & 0x00ffffffUL) | 0xff000000UL;
    Field338 = -1;
    Field33c = -1;
    LineCount = 0;
    Field34c = 0;
    Field350 = 0;
    TextToolTipIds = 0;
    bClickable = 0;
    bColourOverride = 0;
    bTypewriter = 0;
    TypedCount = 1;
    TypeElapsed = 0.0f;
    TypeInterval = 0.05f;
    bTopLeftAnchor = 0;
    bBlink = 0;
    BlinkPhase = 0;
    BlinkElapsed = 0.0f;
    bHoverPulse = 1;
    bTyping = 0;
    if (G_TextControlTypingCount < 0)
        G_TextControlTypingCount = 0;

    // operator_new(0x34) + FUN_004144b0.
    FontString = static_cast<FFontString*>(::operator new(sizeof(FFontString)));
    FontString->Construct();
    bCachedPlacementValid = 0;
    bUseCachedPlacement = 0;
    bInCaptureList = 0;
}

NnTextControl::~NnTextControl()
{
    // FUN_00589d50.
    if (bTyping != 0)
    {
        // End the typewriter run; the full pending text goes into the Font::String.
        bTyping = 0;
        G_TextControlTypingCount = G_TextControlTypingCount - 1;
        if (G_TextControlTypingCount <= 0)
            StopUiSoundSlot(kUiSoundTypingCharacters);   // FUN_005646e0(0xb)
        wchar_t* const Text = PendingText;
        FontString->SetScale(Width, TextDepth(this));
        FontString->SetText(Text);
    }
    FontString->ClearContents(); // FUN_004144f0

    // The Font::String's own destructor inlined (FUN_004144f0 again; the clear left no child).
    FFontString* const String = FontString;
    if (String != nullptr)
    {
        String->ClearContents();
        operator delete[](String->ChildBegin);
        String->ChildBegin = nullptr;
        String->ChildEnd = nullptr;
        String->ChildCap = nullptr;
        ::operator delete(String);
    }
    FontString = nullptr;

    if (bInCaptureList != 0)
        Win3DRemoveCapture(this);   // FUN_0059d960

    if (PendingText != nullptr)
    {
        free(PendingText);
        PendingText = nullptr;
        PendingCapacity = 0;
    }
}

void NnTextControl::Init(char const* StyleName, Vector const* InPosition, char const* Hardpoint,
                         float InWidth, float InHeight, float FontScale, unsigned Ids, int InJustification,
                         int InTextToolTipIds, unsigned char bInTopLeftAnchor)
{
    // FUN_0058a110. FontScale has no read in the body.
    (void)FontScale;
    bTopLeftAnchor = bInTopLeftAnchor;

    if (StyleName != nullptr)
    {
        // FUN_0058a740's body, inlined in retail.
        IFont* const Font = FindMenuFontImp(StyleName);
        if (Font != nullptr)
            SetFont(Font);
    }

    Offset.x = InPosition->x;
    Offset.y = InPosition->y;
    Offset.z = InPosition->z;

    if (Hardpoint != nullptr)
    {
        strcpy(HardpointName, Hardpoint);
        AttachOrientation.E[0][0] = 1.0f;
        AttachOrientation.E[1][1] = 1.0f;
        AttachOrientation.E[2][2] = 1.0f;
        AttachOrientation.E[2][1] = 0.0f;
        AttachOrientation.E[2][0] = 0.0f;
        AttachOrientation.E[1][2] = 0.0f;
        AttachOrientation.E[1][0] = 0.0f;
        AttachOrientation.E[0][2] = 0.0f;
        AttachOrientation.E[0][1] = 0.0f;
        AttachPosition.z = 0.0f;
        AttachPosition.y = 0.0f;
        AttachPosition.x = 0.0f;
        // The current parent resolves the hardpoint; the result is not looked at.
        Win3D* const ParentWin = GetParent();
        if (ParentWin != nullptr)
        {
            WinControl* const ParentControl = ParentWin->AsWinControl();
            if (ParentControl != nullptr)
                ParentControl->GetHardpointTransform(Hardpoint, &AttachXform);
        }
    }

    Height = InHeight;
    Width = InWidth;
    SetIds(Ids);                    // FUN_0058a5f0
    Justification = InJustification;
    TextToolTipIds = InTextToolTipIds;
}

bool NnTextControl::Create(Win3D* NewParent, void* Data)
{
    // FUN_00589ed0 (RET 8). Win3D's Create FUN_004b5060 is inlined in retail.
    Win3D::Create(NewParent, nullptr);

    INI_Reader* const Ini = static_cast<INI_Reader*>(Data);
    if (Ini != nullptr && Ini->read_value())
    {
        do
        {
            if (Ini->is_value("font"))
            {
                // Through FUN_004ea670, not SetFont FUN_0058a740.
                IFont* const Font = FindMenuFontImp(Ini->get_value_string());
                if (Font != nullptr)
                    FontString->SetFont(Font);
            }
            else if (Ini->is_value("offset"))
            {
                // The values are read in the order 1, 0, 2.
                float const Y = static_cast<float>(Ini->get_value_float(1));
                float const X = Ini->get_value_float(0);
                float const Z = Ini->get_value_float(2);
                Offset.x = X;
                Offset.y = Y;
                Offset.z = Z;
            }
            else if (Ini->is_value("hardpoint"))
            {
                // Unlike Init, the name is NOT copied to +0x1d0.
                AttachOrientation.E[2][2] = 1.0f;
                AttachOrientation.E[1][1] = 1.0f;
                AttachOrientation.E[0][0] = 1.0f;
                AttachOrientation.E[2][1] = 0.0f;
                AttachOrientation.E[2][0] = 0.0f;
                AttachOrientation.E[1][2] = 0.0f;
                AttachOrientation.E[1][0] = 0.0f;
                AttachOrientation.E[0][2] = 0.0f;
                AttachOrientation.E[0][1] = 0.0f;
                AttachPosition.z = 0.0f;
                AttachPosition.y = 0.0f;
                AttachPosition.x = 0.0f;
                // The NEW parent (the argument, not +0x04) resolves it; the result is not
                // looked at.
                if (NewParent != nullptr)
                {
                    WinControl* const ParentControl = NewParent->AsWinControl();
                    if (ParentControl != nullptr)
                        ParentControl->GetHardpointTransform(Ini->get_value_string(),
                                                             &AttachXform);
                }
            }
            else if (Ini->is_value("width"))
            {
                Width = Ini->get_value_float(0);
            }
            else if (Ini->is_value("height"))
            {
                Height = Ini->get_value_float(0);
            }
            else if (Ini->is_value("font_scale"))
            {
                // Matched and skipped, no read.
            }
            else if (Ini->is_value("stringID"))
            {
                SetIds(static_cast<unsigned>(static_cast<long>(Ini->get_value_float(0))));
            }
            else if (Ini->is_value("justification"))
            {
                Justification = static_cast<long>(Ini->get_value_float(0));
            }
            else if (Ini->is_value("tooltip"))
            {
                TextToolTipIds = static_cast<long>(Ini->get_value_float(0));
            }
        } while (Ini->read_value());
    }

    // The parent is re-placed at its own position, which positions this control with it.
    Win3D* const ParentWin = GetParent();
    if (ParentWin != nullptr)
        ParentWin->SetPosition(&ParentWin->Position);
    return true;
}

bool NnTextControl::HitTest(Vector const* Ray)
{
    // LAB_0058a230 (RET 4).
    if (TextToolTipIds == 0 && bClickable == 0)
        return false;

    // The sums Position + Offset stay at full precision; each half extent is rounded to a float
    // first.
    double const SumX = static_cast<double>(Position.x) + static_cast<double>(Offset.x);
    double const SumY = static_cast<double>(Position.y) + static_cast<double>(Offset.y);
    float const CentreZ =
        static_cast<float>(static_cast<double>(Position.z) + static_cast<double>(Offset.z));
    float const HalfWidth = Width * 0.5f;   // DAT_005c75e0
    float const HalfHeight = Height * 0.5f;
    float const CentreX = static_cast<float>(SumX + static_cast<double>(HalfWidth));
    float const CentreY = static_cast<float>(SumY - static_cast<double>(HalfHeight));

    Vector const HalfExtents = {HalfWidth, HalfHeight, 0.0f};
    Transform Xform = {Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f),
                       {CentreX, CentreY, CentreZ}};
    if (!Win3DRayHitsAabbHalfExtents(&HalfExtents, &Xform, Ray)) // FUN_005a1130
        return false;

    if (TextToolTipIds != 0)
    {
        FToolTipHoverInfo Info;
        Info.AuxField = 0;                              // +0x44
        Info.Ids = TextToolTipIds;
        Info.IdsAlt = TextToolTipIds;
        Info.bLive = 1;
        Info.WorldPos[0] = CentreX;
        Info.WorldPos[1] = CentreY;
        Info.WorldPos[2] = CentreZ;
        Info.HalfExtents[0] = HalfWidth;
        Info.HalfExtents[1] = HalfHeight;
        Info.HalfExtents[2] = 0.0f;
        NnToolTipFeedHover(&Info);                      // FUN_004be370
    }

    Win3D* const ParentWin = GetParent();
    if (ParentWin != nullptr)
    {
        WinControl* const ParentControl = ParentWin->AsWinControl();
        if (ParentControl != nullptr)
            ParentControl->SetProperty(kWinNotifyTextHover, reinterpret_cast<uintptr_t>(AsWinControl()), 0);
    }
    return true;
}

bool NnTextControl::MouseEvent(FWin3DMouseEvent const* Event)
{
    // FUN_0058ad90 (RET 4).
    if (bClickable == 0)
        return false;

    if (Event->Button == kWin3DMouseLeft && Event->Action == kWin3DMouseDoubleClick &&
        Parent != nullptr)
    {
        WinControl* const ParentControl = Parent->AsWinControl();
        if (ParentControl != nullptr)
            ParentControl->SetProperty(kWinNotifyDoubleClick, reinterpret_cast<uintptr_t>(AsWinControl()), 0);
    }

    if (bInCaptureList != 0)
    {
        bInCaptureList = 0;
        Win3DRemoveCapture(this);
        if (Event->Button != kWin3DMouseLeft)
            return false;
        if (Event->Action == kWin3DMouseRelease)
        {
            // True with or without a parent.
            if (Parent != nullptr)
            {
                WinControl* const ParentControl = Parent->AsWinControl();
                if (ParentControl != nullptr)
                    ParentControl->SetProperty(kWinNotifyClick, reinterpret_cast<uintptr_t>(AsWinControl()),
                                               0);
            }
            return true;
        }
    }

    // Onto the capture stack (FUN_0059d8d0).
    if (Event->Button != kWin3DMouseLeft || Event->Action == kWin3DMouseRelease)
        return false;
    bInCaptureList = 1;
    Win3DPushCapture(this);
    return true;
}

void NnTextControl::SetOrientation(Matrix const* NewOrientation)
{
    // LAB_0058af70 (RET 4).
    bAttached = 1;
    Matrix Attach;
    GetAttachOrientation(&Attach);   // slot 40
    if (G_TextControlMath == nullptr)
        G_TextControlMath = Acquire3DMathEngine();
    Matrix Product;
    G_TextControlMath->MulMatrix(Product, *NewOrientation,
                                 Attach);
    memcpy(&Orientation, &Product, sizeof(Orientation)); // +0x3c
}

void NnTextControl::UpdateInactive()
{
    // LAB_0058afd0. Slot 12 on every inactive child.
    for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
    {
        if ((Child->Flags & kWin3DActive) == 0)
            Child->UpdateInactive();
    }
    // The typewriter stops: the same flush as property 0x50.
    if (bTyping == 0)
        return;

    bTyping = 0;
    G_TextControlTypingCount = G_TextControlTypingCount - 1;
    if (G_TextControlTypingCount <= 0)
        StopUiSoundSlot(kUiSoundTypingCharacters);           // FUN_005646e0(0xb)
    FontString->SetScale(Width, TextDepth(this));
    FontString->SetText(PendingText);             // FUN_00415860
}

void NnTextControl::Update()
{
    // FUN_0058b060.
    Win3D::Update(); // FUN_005a17b0

    // The typewriter runs while +0x362 is set, the Font::String has a child slot (read inline,
    // no layout rebuild), the control is visible and the local server tick is not held
    // (FUN_0042d740).
    bool const bHasChildren =
        FontString->ChildBegin != nullptr && (FontString->ChildEnd - FontString->ChildBegin) != 0;
    if (bTypewriter != 0 && bHasChildren && (Flags & kWin3DVisible) != 0 && !ShouldSkipLocalServerTick())
    {
        // An empty pending text takes a copy of the Font::String's text; with no text there, a
        // non-empty pending text is emptied.
        if (PendingText == nullptr || PendingText[0] == L'\0')
        {
            wchar_t const* const Source = FontString->Text;
            if (Source != nullptr)
            {
                unsigned const Len = static_cast<unsigned>(wcslen(Source));
                FHudWideText Pending = {PendingText, PendingCapacity};
                Pending.Reserve(Len + 1);
                PendingText = Pending.Text;
                PendingCapacity = Pending.Capacity;
                wcsncpy(PendingText, Source, Len);
                PendingText[Len] = L'\0';
            }
            else if (PendingText != nullptr && PendingText[0] != L'\0')
            {
                PendingText[0] = L'\0';
            }
        }
        // Still nothing to type: on to the blink.
        if (PendingText != nullptr && PendingText[0] != L'\0')
        {
            // An elapsed time of exactly 0 (DAT_005c7474) starts the run (FUN_00564650(0xb)),
            // which takes a typing reference once.
            if (TypeElapsed == 0.0f)
            {
                PlayUiSoundSlotFlag1(0xb);
                if (bTyping == 0)
                {
                    bTyping = 1;
                    G_TextControlTypingCount = G_TextControlTypingCount + 1;
                }
            }
            TypeElapsed = static_cast<float>(GetClientFrameTimeCapped() +
                                             static_cast<double>(TypeElapsed));
            int const Length =
                PendingText != nullptr ? static_cast<int>(wcslen(PendingText)) : 0;
            while (TypeElapsed > TypeInterval)
            {
                TypeElapsed = TypeElapsed - TypeInterval;
                TypedCount = TypedCount + 1;
                if (TypedCount >= Length)
                {
                    bTypewriter = 0; // before the +0x389 test
                    if (bTyping != 0)
                    {
                        bTyping = 0;
                        G_TextControlTypingCount = G_TextControlTypingCount - 1;
                        if (G_TextControlTypingCount <= 0)
                            StopUiSoundSlot(kUiSoundTypingCharacters);
                        FontString->SetScale(Width, TextDepth(this));
                        FontString->SetText(PendingText);
                    }
                }
            }
        }
    }
    else if (bTyping != 0)
    {
        // A run in progress ends when the control is hidden or its Font::String has no child
        // slot.
        if ((Flags & kWin3DVisible) == 0 || !bHasChildren)
        {
            bTyping = 0;
            G_TextControlTypingCount = G_TextControlTypingCount - 1;
            if (G_TextControlTypingCount <= 0)
                StopUiSoundSlot(kUiSoundTypingCharacters);
            FontString->SetScale(Width, TextDepth(this));
            FontString->SetText(PendingText);
        }
    }

    // The colour blink. The sum is stored as a float but compared (with 1.0f, DAT_005c75dc) at
    // full precision.
    if (bBlink == 0)
        return;
    double Sum = GetClientFrameTimeCapped() + static_cast<double>(BlinkElapsed);
    BlinkElapsed = static_cast<float>(Sum);
    if (Sum > 1.0)
    {
        // Whole seconds subtracted, the phase toggling each time.
        unsigned char Phase = BlinkPhase;
        do
        {
            Sum = Sum - 1.0;
            Phase = static_cast<unsigned char>(Phase == 0);
        } while (Sum > 1.0);
        BlinkElapsed = static_cast<float>(Sum);
        BlinkPhase = Phase;
    }
    if (BlinkPhase != 0)
        memcpy(OverrideColour, BlinkColourOn, sizeof(OverrideColour));
    else
        memcpy(OverrideColour, BlinkColourOff, sizeof(OverrideColour));
}

void NnTextControl::SetIds(unsigned Ids)
{
    // FUN_0058a5f0.
    if (Ids != 0)
    {
        wchar_t Buffer[0x1000];
        unsigned const Count = GetIdsStringWWithRawFallback(Ids, Buffer, 0x1000); // FUN_004347e0
        // "IDS?" when the lookup copied nothing.
        std::wstring Temp;
        if (Count == 0)
            Temp.assign(L"IDS?");
        else
            Temp.assign(Buffer, Count);
        FontString->SetScale(Width, TextDepth(this)); // FUN_004145d0
        FontString->SetText(Temp.c_str());            // FUN_00415860
        return;
    }
    // DAT_005c8ce0.
    FontString->SetScale(Width, TextDepth(this));
    FontString->SetText(L"");
}

void NnTextControl::SetText(wchar_t const* Text)
{
    // FUN_0058a530.
    FontString->SetScale(Width, TextDepth(this));
    FontString->SetText(Text);
}

void NnTextControl::SetNumberText(char const* Format)
{
    // FUN_0058a580.
    FontString->SetScale(Width, TextDepth(this));
    FontStringSetNumberText(FontString, Format);
}

void NnTextControl::SetFont(IFont* Font)
{
    // FUN_0058a740. The kind byte of a child slot is not looked at: every child gets its vt+8.
    if (FontString->FontIface == Font)
        return;
    FontString->FontIface = Font;
    for (unsigned Index = 0;
         FontString->ChildBegin != nullptr &&
         Index < static_cast<unsigned>(FontString->ChildEnd - FontString->ChildBegin);
         ++Index)
    {
        // The argument is re-read from +0x1c each pass; no null test.
        FontString->ChildBegin[Index].Child->BindFont(FontString->FontIface);
    }
    FontString->LayoutGen = 0;
}

void NnTextControl::SetFontByName(char const* StyleName)
{
    // FUN_0058a790; the rest is FUN_0058a740's body, inlined in retail.
    IFont* const Font = FindMenuFontImp(StyleName);
    if (Font == nullptr)
        return;
    SetFont(Font);
}

void NnTextControl::SetWidth(float NewWidth)
{
    // FUN_0058b730.
    Width = NewWidth;
    FontString->SetScale(Width, TextDepth(this));
}

void NnTextControl::SetCachedDraw(unsigned char bCached, unsigned char bInvalidate)
{
    // FUN_0058ad70.
    bUseCachedPlacement = bCached;
    if (bInvalidate != 0)
        bCachedPlacementValid = 0;
}

void NnTextControl::SetPosition(Vector const* NewPosition)
{
    // LAB_0058ae70. Keep the requested anchor separately; the base WinControl position is
    // the rectangle's upper-left corner when bTopLeftAnchor is clear.
    memcpy(&RequestedPosition, NewPosition, sizeof(RequestedPosition));
    bAttached = 1;
    Vector Attach;
    GetAttachPosition(&Attach);
    if (bTopLeftAnchor == 0)
    {
        Attach.x -= Width * 0.5f;
        Attach.y += Height * 0.5f;
    }

    Vector Placed = {NewPosition->x, NewPosition->y, NewPosition->z};
    if ((PositionFlags & kWinPositionAddOffset) != 0)
    {
        Placed.x += Attach.x;
        Placed.y += Attach.y;
        Placed.z += Attach.z;
    }
    WinControl::SetPosition(&Placed); // a direct base call
    if (Width > 0.0f)
        FontString->SetScale(Width, TextDepth(this));
}

void NnTextControl::Draw()
{
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only: owners that walk their children themselves (HudTargetBase, NN_MainButton)
    // reach this without the Win3D draw walk, so the control sets its own HUD shift.
    FWidescreenHudShiftScope const WidescreenScope(WidescreenHudShiftPixelsFor(this), true);
#endif
    // LAB_0058a7f0: options 6, 9 and 0x13 gate the draw before the child walk.
    if (GetGameOption(kGameOptInterfaceGroup) == 0)
        return;
    if (GetGameOption(kGameOptNeuronet) == 0)
        return;
    if (GetGameOption(kGameOptDrawTextControls) == 0)
        return;

    // FUN_0059e5c0: the children first (no visibility test of its own).
    DrawChildren();

    if (bTypewriter != 0)
    {
        // An empty pending text takes the Font::String's text.
        if (PendingText == nullptr || PendingText[0] == L'\0')
        {
            FHudWideText Pending = {PendingText, PendingCapacity};
            Pending.Assign(FontString->Text, 0xffffffffu);
            PendingText = Pending.Text;
            PendingCapacity = Pending.Capacity;
        }
        if (PendingText != nullptr && PendingText[0] != L'\0')
        {
            // Past 0x3ff characters (unsigned) the run is cut there: the typewriter goes off
            // and a live run shows the whole text.
            if (static_cast<unsigned>(TypedCount) > 0x3ffu)
            {
                TypedCount = 0x3ff;
                bTypewriter = 0;            // before the +0x389 test
                if (bTyping != 0)
                {
                    bTyping = 0;
                    G_TextControlTypingCount = G_TextControlTypingCount - 1;
                    if (G_TextControlTypingCount <= 0)
                        StopUiSoundSlot(kUiSoundTypingCharacters);
                    wchar_t* const Whole = PendingText;
                    Vector Sum;
                    Win3DVec3Add(&Sum, &Position, &Offset); // FUN_00423b30
                    FontString->SetScale(Width, Sum.z);
                    FontString->SetText(Whole);
                }
            }
            // The first TypedCount characters become the text shown.
            wchar_t Typed[0x400];
            wcsncpy(Typed, PendingText, static_cast<size_t>(TypedCount));
            Typed[TypedCount] = L'\0';
            float const TypedDepth = Position.z + Offset.z; // FUN_00422b80
            FontString->SetScale(Width, TypedDepth);
            FontString->SetText(Typed);
        }
    }

    // Each sum rounded to a float (FUN_00422b80).
    Vector const World = {Position.x + Offset.x, Position.y + Offset.y,
                          Position.z + Offset.z};
    FontString->SetScale(Width, World.z);
    // No line: nothing more, and no depth-test call.
    if (FontString->ChildCount() == 0) // FUN_004145a0
        return;
    // No font: "Normal", through FUN_004ea670's body inline.
    if (FontString->FontIface == nullptr)
        FontString->SetFont(FindMenuFontImp("Normal"));
    if (FontString->FontIface == nullptr)
    {
        Win3DDisable(kWin3DDepthTest);
        return;
    }
    // -z against 0.001 (DAT_005c75f8, a double): below returns without the depth-test call;
    // equal or unordered go on.
    if (-static_cast<double>(World.z) < 0.001)
        return;

    // The colour (FUN_00417780).
    if (bColourOverride != 0)
    {
        unsigned long PackedOverride;
        memcpy(&PackedOverride, OverrideColour, sizeof(PackedOverride));
        FontAtlasSetDrawColor(static_cast<unsigned>(PackedOverride));
    }
    else if (bHighlighted != 0)
    {
        FontAtlasSetDrawColor(static_cast<unsigned>(G_NnPaletteAmber));
    }
    else if ((Flags & kWin3DActive) == 0)
    {
        FontAtlasSetDrawColor(0xff808080u); // bytes 80 80 80 ff
    }
    else
    {
        FontAtlasSetDrawColor(static_cast<unsigned>(TextColour)); // +0x348
        if (bClickable != 0)
        {
            // The cursor (DAT_00616840 / DAT_00616844) onto the near plane of the Win3D camera
            // DAT_0067dbf8, inline.
            FSpaceCamera* const Cam = Win3DGetCamera();
            int CursorX = 0;
            int CursorY = 0;
            GetClientSoftwareCursor(&CursorX, &CursorY, nullptr);
            Vector Near;
            Near.x = static_cast<float>(
                (static_cast<double>(CursorX) - static_cast<double>(Cam->ScreenCenterX)) /
                static_cast<double>(Cam->XScale) * static_cast<double>(Cam->ZNear));
            Near.y = static_cast<float>(
                (static_cast<double>(CursorY) - static_cast<double>(Cam->ScreenCenterY)) /
                static_cast<double>(Cam->YScale) * static_cast<double>(Cam->ZNear));
            Near.z = -Cam->ZNear;
            // Through a copy of the camera orientation (DAT_0067dbfc, FUN_00423b90), then
            // normalised inline.
            Matrix CamOrientation;
            memcpy(&CamOrientation, &Cam->World, sizeof(CamOrientation));
            Vector Dir;
            Win3DTransformByMatrix(Dir, CamOrientation, Near);
            double const Inv = 1.0 / sqrt(static_cast<double>(Dir.x) * Dir.x +
                                          static_cast<double>(Dir.y) * Dir.y +
                                          static_cast<double>(Dir.z) * Dir.z);
            Vector Ray;
            Ray.x = static_cast<float>(Dir.x * Inv);
            Ray.y = static_cast<float>(Dir.y * Inv);
            Ray.z = static_cast<float>(Dir.z * Inv);
            // A hit with bHoverPulse set draws in the pulse colour (FUN_00561da0).
            if (HitTest(&Ray) && bHoverPulse != 0)
            {
                unsigned long Pulse = 0;
                NnUiPulseBlendArgb(&Pulse);
                FontAtlasSetDrawColor(static_cast<unsigned>(Pulse));
            }
        }
    }

    LineCount = FontString->ChildCount(); // the line count
    // Scale / Height from FUN_004176c0.
    FFontStringDrawOrigin Origin;
    Origin.X = 0.0f;
    Origin.Y = 0.0f;
    Origin.HAlign = 0;
    Origin.VAlign = 0;
    float ScreenWidth = 0.0f;
    float ScreenHeight = 0.0f;
    TextWinProjectExtent(Width, Height, World.z, &ScreenWidth, &ScreenHeight);
    Origin.Scale = ScreenWidth;
    Origin.Height = ScreenHeight;
    if (bUseCachedPlacement != 0)
    {
        // A different surface width drops the cache.
        if (CachedSurfaceWidth != G_RenderSurfaceWidth)
            bCachedPlacementValid = 0;
        if (bCachedPlacementValid == 0)
        {
            // x moves right by half of (box width - text width); the text width (FUN_00414570)
            // is widened as unsigned.
            bCachedPlacementValid = 1;
            TextWinProjectPoint(&World, &CachedX, &CachedY);
            int Extent[2] = {0, 0};
            int const* const TextExtent = FontString->GetExtent(Extent);
            double const TextWidth =
                static_cast<double>(static_cast<unsigned>(TextExtent[0]));
            CachedX = static_cast<float>(
                (static_cast<double>(ScreenWidth) - TextWidth) * 0.5 +
                static_cast<double>(CachedX));
            CachedSurfaceWidth = G_RenderSurfaceWidth;
        }
        // Left-aligned at the cached point.
        Origin.HAlign = 0;
        Origin.X = CachedX;
        Origin.Y = CachedY;
    }
    else
    {
        // Aligned by Justification at FUN_00417730(World).
        Origin.HAlign = static_cast<unsigned>(Justification);
        TextWinProjectPoint(&World, &Origin.X, &Origin.Y);
    }
    Origin.DrawChildren(FontString,
                        reinterpret_cast<void*>(static_cast<intptr_t>(Field34c)),
                        reinterpret_cast<void*>(static_cast<intptr_t>(Field350)));
    Win3DDisable(kWin3DDepthTest);
}

intptr_t NnTextControl::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // FUN_0058b360.
    switch (Msg)
    {
    case kWinPropOffset:
    {
        float const* const NewOffset = reinterpret_cast<float const*>(A);
        Offset.x = NewOffset[0];
        Offset.y = NewOffset[1];
        Offset.z = NewOffset[2];
        if (MeshHolder.EngineInstance == InvalidInstanceIndex)
            return 0;
        Vector Local;
        GetAttachPosition(&Local);   // vt+0xa4
        if (bTopLeftAnchor == 0)
        {
            Local.x = Local.x - Width * 0.5f; // DAT_005c75e0
            Local.y = Height * 0.5f + Local.y;
        }
        // Without +0x2f4 bit 0 retail copies the Offset itself, not Local (as here).
        Vector Sum;
        Vector const* Source = &Offset;
        if ((PositionFlags & kWinPositionAddOffset) != 0)
        {
            Sum.x = Local.x + Offset.x;
            Sum.y = Local.y + Offset.y;
            Sum.z = Local.z + Offset.z;
            Source = &Sum;
        }
        Vector Copy = {Source->x, Source->y, Source->z};
        WinControl::SetPosition(&Copy); // FUN_005a0880, a direct call
        return 0;
    }
    case kWinPropLit:
        bHighlighted = static_cast<unsigned char>(A != 0 ? 1 : 0);
        return 0;
    case kTextPropSetText:
        FontString->SetScale(Width, TextDepth(this));
        FontString->SetText(reinterpret_cast<wchar_t const*>(A));
        break;
    case kTextPropSetIds:
        SetIds(static_cast<unsigned>(A));
        break;
    case kTextPropGetText:
        return reinterpret_cast<intptr_t>(FontString->Text);
    case kTextPropGetLength:
        if (FontString->Text == nullptr)
            return 0;
        return static_cast<intptr_t>(wcslen(FontString->Text));
    case kTextPropGetLineCount:
        return LineCount;
    case 0x22:
        Field33c = static_cast<int>(B);
        Field338 = static_cast<int>(A);
        return 0;
    case 0x23:
        Field34c = static_cast<int>(A);
        Field350 = static_cast<int>(B);
        return 0;
    case kTextPropClickable:
        bClickable = static_cast<unsigned char>(A != 0 ? 1 : 0);
        return 0;
    case kWinPropColour:
        // The override flag is B == 1; the colour is the dword A points at.
        bColourOverride = static_cast<unsigned char>(B == 1 ? 1 : 0);
        memcpy(OverrideColour, reinterpret_cast<void const*>(A), sizeof(OverrideColour));
        return 0;
    case kTextPropTypewriter:
    {
        if (bTyping != 0)
        {
            bTyping = 0;
            G_TextControlTypingCount = G_TextControlTypingCount - 1;
            if (G_TextControlTypingCount <= 0)
                StopUiSoundSlot(kUiSoundTypingCharacters);
            // The pending text goes in whole; the depth is the z of FUN_00423b30.
            wchar_t* const Text = PendingText;
            float const Depth = Position.z + Offset.z;
            FontString->SetScale(Width, Depth);
            FontString->SetText(Text);
        }
        bTypewriter = 0;
        TypedCount = 1;
        TypeElapsed = 0.0f;
        if (PendingText != nullptr && PendingText[0] != L'\0')
            PendingText[0] = L'\0';
        // A == 1 turns the typewriter on with B (unsigned) milliseconds per step (0.001f,
        // DAT_005d1494).
        if (A == 1)
        {
            bTypewriter = 1;
            TypeInterval = static_cast<float>(static_cast<double>(static_cast<unsigned>(B)) *
                                              static_cast<double>(0.001f));
        }
        return 0;
    }
    case kWinPropBlink:
        bBlink = 0;
        if (A != 0)
        {
            bBlink = 1;
            bColourOverride = 1;
        }
        return 0;
    default:
        return WinControl::SetProperty(Msg, A, B); // FUN_005a13c0
    }

    // Cases 0x1d and 0x1e: a non-empty pending text is emptied.
    if (PendingText != nullptr && PendingText[0] != L'\0')
        PendingText[0] = L'\0';
    return 0;
}

// ---------------------------------------------------------------------------
// EditControl (vftable 0x005e501c)
// ---------------------------------------------------------------------------

// Retail DAT_0067d860 -- WINFACTORY<EditControl> (static init LAB_00589b20: FUN_0058c1c0
// "EditControl" + atexit LAB_00589b40 -> FUN_00579380). Its Create FUN_0058c230 (vftable
// 0x005e50e0) is the template's, operator_new(0x3b0).
WINFACTORY<NnEditControl> G_EditControlFactory("EditControl");

namespace
{
    wchar_t const kEditCursor[] = L"|"; // DAT_005e50d0
    float const kEditBlinkSeconds = 0.5f; // DAT_005c75e0

    // FUN_00413df0 with Len -1: EditControl copies the Font::String's text this way before
    // every change (e.g. 0x0058b80b, 0x0058bf3f).
    void EditCopyText(FHudWideText* Copy, wchar_t const* Source)
    {
        if (Source == nullptr)
        {
            Copy->Clear();
            return;
        }
        unsigned const Len = static_cast<unsigned>(wcslen(Source));
        Copy->Reserve(Len + 1);
        wcsncpy(Copy->Text, Source, Len);
        Copy->Text[Len] = L'\0';
    }
}

NnEditControl::NnEditControl()
    : CursorTimer(0.0f)
    , bCursorShown(0)
    , MaxLength(-1)
    , bEditable(1)
    , bDigitsOnly(0)
{
    // Inlined into FUN_0058c230 (0x0058c26f).
    Pad3a5[0] = Pad3a5[1] = Pad3a5[2] = 0;
    Pad3ae[0] = Pad3ae[1] = 0;
}

void NnEditControl::RemoveCursor()
{
    // FUN_0058bcb0 (thiscall). Nothing without the cursor.
    if (bCursorShown == 0)
        return;

    // A non-empty copy of the text loses its last character and goes back in (FUN_0058a530's
    // two calls, inlined).
    FHudWideText Copy = {nullptr, 0};
    EditCopyText(&Copy, FontString->Text);
    if (Copy.Text != nullptr)
    {
        size_t const Len = wcslen(Copy.Text);
        if (static_cast<int>(Len) > 0)
        {
            if (Copy.Capacity > static_cast<unsigned>(Len - 1))
                Copy.Text[Len - 1] = L'\0';
            SetText(Copy.Text);
        }
    }
    bCursorShown = 0;
    Copy.Free();
}

void NnEditControl::Update()
{
    // LAB_0058bdb0 (slot 11). Not editable: straight out, without TextControl's Update.
    if (bEditable == 0)
        return;

    // The timer is stored as a float but compared at full precision; less or unordered skips.
    double const Sum = GetClientFrameTimeCapped() + static_cast<double>(CursorTimer);
    CursorTimer = static_cast<float>(Sum);
    if (Sum >= static_cast<double>(kEditBlinkSeconds))
    {
        // One toggle per 0.5 s owed.
        do
        {
            CursorTimer = static_cast<float>(static_cast<double>(CursorTimer) -
                                             static_cast<double>(kEditBlinkSeconds));
            if (bCursorShown == 0)
            {
                // The text plus '|' (a null text gives just the cursor).
                FHudWideText Copy = {nullptr, 0};
                EditCopyText(&Copy, FontString->Text);
                unsigned const Len =
                    Copy.Text != nullptr ? static_cast<unsigned>(wcslen(Copy.Text)) : 0;
                Copy.Reserve(Len + 2);
                wcsncpy(&Copy.Text[Len], kEditCursor, 1);
                Copy.Text[Len + 1] = L'\0';
                SetText(Copy.Text);
                bCursorShown = 1;
                Copy.Free();
            }
            else
            {
                // FUN_0058bcb0's body, inlined in retail.
                RemoveCursor();
            }
        } while (CursorTimer >= kEditBlinkSeconds);
    }

    NnTextControl::Update(); // FUN_0058b060
}

bool NnEditControl::KeyEvent(unsigned Key, unsigned char* Modifiers)
{
    // LAB_0058b7a0 (slot 20, RET 8). Only an active (Flags bit 0), editable control.
    if ((Flags & kWin3DActive) == 0 || bEditable == 0)
        return false;

    if (Key == VK_RETURN && (*Modifiers & kWin3DKeyReleased) != 0)
    {
        // Enter. Without a parent nothing happens (but it is taken).
        if (Parent == nullptr)
            return true;
        RemoveCursor(); // inline in retail
        CursorTimer = 0.0f;
        WinControl* const Owner = Parent->AsWinControl(); // slot 2
        if (Owner != nullptr)
            Owner->SetProperty(kWinNotifyEditChanged, reinterpret_cast<uintptr_t>(FontString->Text), 1);
        return true;
    }

    if (Key == VK_BACK)
    {
        // Backspace.
        if ((*Modifiers & kWin3DKeyPressed) != 0)
            return true;
        RemoveCursor(); // inline in retail
        CursorTimer = 0.0f;

        FHudWideText Copy = {nullptr, 0};
        EditCopyText(&Copy, FontString->Text); // FUN_00413df0(text, -1)
        int Len = 0;
        if (Copy.Text != nullptr)
        {
            Len = static_cast<int>(wcslen(Copy.Text));
            if (Len > 0)
            {
                if (Copy.Capacity > static_cast<unsigned>(Len - 1))
                    Copy.Text[Len - 1] = L'\0';
                SetText(Copy.Text);
            }
        }
        // Retail: Parent is not tested here (0x0058b9b7), unlike Enter.
        WinControl* const Owner = Parent->AsWinControl();
        if (Len - 1 > 0)
        {
            if (Owner != nullptr)
                Owner->SetProperty(kWinNotifyEditChanged, reinterpret_cast<uintptr_t>(FontString->Text), 0);
        }
        else if (Owner != nullptr)
        {
            Owner->SetProperty(kWinNotifyEditChanged, 0, 0);
        }
        Copy.Free();
        return true;
    }

    // The numeric keypad (0x60..0x69) types '0'..'9'. Enter without modifier bit 0 comes here
    // too, as the character 0x0d.
    if (Key != VK_RETURN && Key >= VK_NUMPAD0 && Key <= VK_NUMPAD9)
        Key -= 0x30;

    if (bDigitsOnly != 0 && (Key < '0' || Key > '9'))
        return false;
    if ((*Modifiers & kWin3DKeyPressed) != 0)
        return true;

    // With a maximum, a text already that long takes nothing more (the copy is measured
    // without a null test, as retail does).
    if (MaxLength > 0)
    {
        FHudWideText Copy = {nullptr, 0};
        EditCopyText(&Copy, FontString->Text);
        int const Len = static_cast<int>(wcslen(Copy.Text));
        Copy.Free();
        if (Len + 1 > MaxLength)
            return true;
    }

    // The key's low byte as a one-byte UTF-8 string (FUN_005b7548, the MSLU thunk of
    // MultiByteToWideChar).
    char Narrow[2] = {static_cast<char>(Key & 0xff), '\0'};
    wchar_t Wide[2] = {L'\0', L'\0'};
    if (MultiByteToWideChar(CP_UTF8, 0, Narrow, static_cast<int>(strlen(Narrow)), Wide, 2) <= 0)
        return true;

    RemoveCursor();
    CursorTimer = 0.0f;

    FHudWideText Copy = {nullptr, 0};
    EditCopyText(&Copy, FontString->Text);
    unsigned const Len = Copy.Text != nullptr ? static_cast<unsigned>(wcslen(Copy.Text)) : 0;
    Copy.Reserve(Len + 2);
    wcsncpy(&Copy.Text[Len], Wide, 1);
    Copy.Text[Len + 1] = L'\0';

    // Retail: Parent is not tested here either (0x0058bc5b).
    SetText(Copy.Text);
    WinControl* const Owner = Parent->AsWinControl();
    if (Owner != nullptr)
        Owner->SetProperty(kWinNotifyEditChanged, reinterpret_cast<uintptr_t>(FontString->Text), 0);
    Copy.Free();
    return true;
}

intptr_t NnEditControl::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_0058c030 (slot 42, RET 0xc).
    switch (Msg)
    {
    case kTextPropSetText:
        // The new text carries no cursor.
        bCursorShown = 0;
        return NnTextControl::SetProperty(Msg, A, B); // FUN_0058b360
    case kEditPropEditable:
        if (A == 0)
        {
            bEditable = 0;
            RemoveCursor(); // FUN_0058bcb0
            return 1;
        }
        bEditable = 1;
        if (B != 0) // unsigned: only zero keeps the old maximum
            MaxLength = static_cast<int>(B);
        return 1;
    case kEditPropDigitsOnly:
        bDigitsOnly = (A & 1) != 0 ? 1 : 0;
        return 1;
    default:
        return NnTextControl::SetProperty(Msg, A, B); // FUN_0058b360
    }
}
