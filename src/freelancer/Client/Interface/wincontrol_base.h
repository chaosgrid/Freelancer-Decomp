// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail WinControl (RTTI .?AUWinControl@@ : Win3D, IDataViewOwner) -- the mesh-backed Win3D
// every NeuroNet control, UIScroll, BaseRichTextWin / RichTextWin and most HUD panels derive
// from. Constructed by FUN_0059f860, destroyed by FUN_0059fa50 (scalar deleting FUN_0059fa30),
// built by WINFACTORY<WinControl>::Create FUN_005a1a20 (operator_new(0x32c)).
//
// vftable 0x005e5e44 (45 slots): slots 0..27 are Win3D's (win3d.h), 28..44 WinControl's own.
// The IDataViewOwner face sits at +0x7c (vftable 0x005e5e38).
//
// Layout (FUN_0059f860):
//   +0x000 Win3D (0x7c)
//   +0x07c IDataViewOwner vptr
//   +0x080 FClientMeshHolder (0x28): holder ctor FUN_00420d40, +0x90 anim = -1,
//          +0x94..+0xa4 = 1.0f; the holder's own vftable 0x005d2efc is not modelled
//   +0x0a8 offset[3]            ("offset" in the BaseFrame ini; added to +0x60 for the mesh)
//   +0x0b4 half extents[3]      ("mouse_size"; the box FUN_005a1130 tests the ray against)
//   +0x0c0 hit offset[3]        (added to the hit box position by FUN_005a0340)
//   +0x0cc tooltip id           ("tooltip"; slot 34 / FUN_005a0340)
//   +0x0d0 char[0x100] mesh path (slot 38)
//   +0x1d0 char[0x100] hardpoint name (slot 35 attaches to the parent's hardpoint of this name)
//   +0x2d0 bCaptured, +0x2d1 no_z_enable, +0x2d2 (property 9), +0x2d3 identity hit orientation
//   +0x2e0 requested position {0, 0, 10000.0f}; +0x2ec 0, +0x2f0 0 -- written by the ctor,
//          read by subclasses
//   +0x2f4 flag byte: bit0 = add +0xa8 to the instance position (ctor sets it)
//   +0x2f8 attach orientation[9], +0x31c attach position[3], +0x328 bAttached
//   size 0x32c

#include "Common/Graphics/client_mesh_load.h"
#include "Interface/win3d.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

class RichTextWin;
class RichTextEditWin;
class UiScroll;

// The property ids WinControl's own SetProperty (slot 42, FUN_005a13c0) handles. Subclasses add
// their own ids above 11 and pass these down.
enum EWinControlProperty
{
    kWinPropHide = 1,            // A == 1 clears Flags bits 0..1, anything else sets them
    kWinPropDisable = 2,         // Flags bit 0 = (A == 0)
    kWinPropToolTip = 3,         // ToolTipIds = A
    kWinPropMesh = 4,            // SetMesh(A, B)
    kWinPropPosition = 5,        // SetPosition(A)
    kWinPropOffset = 6,          // Offset = *A, the instance re-placed
    kWinPropGetOffset = 7,       // *A = Offset
    kWinPropOrientation = 8,     // the instance's rotation from A (9 floats)
    kWinPropFlag2d2 = 9,         // +0x2d2 = (A != 0); nothing reads it
    kWinPropGetInstance = 10,    // returns the mesh instance
    kWinPropGetMeshPath = 11,    // strncpy(A, MeshPath, B)
};

// The text property ids only NnTextControl's SetProperty (FUN_0058b360, nn_text_control.cpp) and
// NnEditControl's handle; every other control passes them to WinControl, which ignores them.
enum EWinTextProperty
{
    kTextPropSetText = 0x1d,      // A = the wide text
    kTextPropSetIds = 0x1e,       // A = an IDS string id
    kTextPropGetText = 0x1f,      // returns the text pointer
    kTextPropGetLength = 0x20,    // returns wcslen of the text (0 without one)
    kTextPropGetLineCount = 0x21,
    kTextPropClickable = 0x24,    // A != 0
    kTextPropTypewriter = 0x50,   // A == 1: on, B milliseconds per character; else off
};

// NnEditControl's own ids (LAB_0058c030); both answer 1.
enum EWinEditProperty
{
    kEditPropEditable = 0x43,     // A != 0: editable, a non-zero B is the new maximum length;
                                  // A == 0: read-only, the cursor removed
    kEditPropDigitsOnly = 0x44,   // A bit 0
};

// NnStaticControl's own ids (LAB_00589050).
enum EWinStaticProperty
{
    kStaticPropSetRoll = 0x0d,        // A = the float's bits; also the start roll
    kStaticPropGetRoll = 0x0e,
    kStaticPropSetPitch = 0x0f,
    kStaticPropGetPitch = 0x10,
    kStaticPropSetYaw = 0x11,
    kStaticPropGetYaw = 0x12,
    kStaticPropSetInstance = 0x17,    // A = a new engine instance; the angles reset
    kStaticPropColours = 0x18,        // A -> the mesh colour, B -> the wire colour
    kStaticPropEnableRotation = 0x1c, // A == 1; anything else also resets the orientation
    kStaticPropForceRadius = 0x4d,    // A, converted as unsigned
};

// The button ids: NnButtonControl (FUN_0055bc30), WireButtonControl (LAB_005a4690) and
// NnToggleButtonControl.
enum EWinButtonProperty
{
    kButtonPropGetCommand = 0x2e,     // *A = the button's command id
    kTogglePropGetState = 0x2f,       // *A = the toggle state
    kTogglePropSetState = 0x30,       // state = (A == 0) ? 1 : 0
    kTogglePropSounds = 0x31,         // A, B = the sound names of the two states (null keeps)
    kButtonPropNoMeshRender = 0x32,   // A != 0 sets, 0 clears ButtonBits2 kButtonNoMeshRender
    kButtonPropGlow = 0x35,           // A != 0
    kButtonPropSpin = 0x36,           // A == 1
};

// CargoBarControl's own ids.
enum EWinCargoBarProperty
{
    kBarPropColour = 0x3f,            // A = the colour dword itself
    kBarPropFill = 0x40,              // A = capacity, B = used
    kBarPropSegments = 0x41,          // A = tail segment, B = head segment
};

// ShipViewControl's own id (LAB_005864c0).
enum EWinShipViewProperty
{
    kShipViewPropMode = 0x4c,         // A = the view mode
};

// The scroll bar property ids (ScrollControl FUN_005831b0 and UiScroll LAB_00597350, the only
// handlers).
enum EWinScrollProperty
{
    kScrollPropSetRange = 0x37,   // A = minimum, B = maximum
    kScrollPropSetValue = 0x38,   // A
    kScrollPropGetMax = 0x39,     // ScrollControl only
    kScrollPropGetMin = 0x3a,     // ScrollControl only
    kScrollPropGetValue = 0x3b,
    kScrollPropSetPage = 0x3c,    // A = page size, B = button step (0 = 1); ScrollControl only
    kScrollPropGetPage = 0x3d,    // ScrollControl only
};

// The words of the A / B arguments of kWinPropRenderFlags: a flag is set by any non-zero value
// in its 16-bit word.
enum EWinRenderFlagsArg
{
    kRenderArgNoMeshRender = 0x1,       // A, low word
    kRenderArgNoWireRender = 0x10000,   // A, high word
    kRenderArgNoZEnable = 0x1,          // B, low word
    kRenderArgNoColoring = 0x10000,     // B, high word
};

// Property ids NnTextControl, NnButtonControl and WireStaticControl handle alike.
enum EWinSharedProperty
{
    // A != 0 shows the control in its lit (selected) state: NnStaticControl +0x347,
    // NnButtonControl ButtonBits 0x40, WireStaticControl kWireLit, NnTextControl bHighlighted.
    kWinPropLit = 0x0c,
    // Two flags per word, low word first: A = (no_mesh_render, no_wire_render), B =
    // (no_z_enable, no_coloring). NnStaticControl takes all four; CargoBarControl draws its
    // border unless A's high word is set; ShipViewControl keeps A's low word as ViewFlags 8.
    kWinPropRenderFlags = 0x19,
    kWinPropColour = 0x33,        // A -> the colour dword; B == 1 turns the colour override on
    kWinPropDrawInactive = 0x34,  // A != 0: NnButtonControl ButtonBits2 0x10, WireStaticControl
                                  // kWireDrawInactive
    // The NeuroNet panels (NN_News, NN_Repair, NN_MissionVendor, NN_ShipTrader): closed from
    // outside (the NavBar, FUN_0043e600), so without their close callback.
    kWinPropCloseFromOutside = 0x4e,
    kWinPropBlink = 0x51,         // A != 0 blinks
    // The NeuroNet dialogs (NN_Confirm, NN_Error, NN_Exit, NN_LoadSave, NN_Pause, NN_Prompt,
    // NN_News, NN_Repair, ...): SetOffset(*A) and SetPosition(Position).
    kWinPropPlaceAtOffset = 0x42,
};

// The ids a control sends to its parent's SetProperty about itself (A = the sending control).
enum EWinControlNotify
{
    kWinNotifyShipViewPick = 0x15,   // ShipViewControl: A = the engine instance picked
    kWinNotifyShipViewDrop = 0x16,   // ShipViewControl: an item dropped on hardpoint A (its name)
    kWinNotifyHover = 0x25,          // WinControl::HitTest (FUN_005a0340) / UiScroll: a hit
    kWinNotifyClick = 0x26,          // a button's Activate, NnTextControl's release on it
    kWinNotifyDoubleClick = 0x27,    // a double click (WireButtonControl: non-zero answer
                                     // suppresses the 0x26 that follows)
    kWinNotifyPress = 0x28,          // a left press on a button
    kWinNotifyRelease = 0x29,        // a release that is no click (NnButtonControl: off the
                                     // button; UiScroll: every release)
    kWinNotifyCapturedLeave = 0x2a,  // a pressed button: the cursor moved off it
    kWinNotifyCapturedEnter = 0x2b,  // a pressed button: the cursor came back
    kWinNotifyHighlight = 0x2c,      // MouseEnter, a press, the captured cursor back on
    kWinNotifyUnhighlight = 0x2d,    // MouseLeave, the captured cursor off
    kWinNotifyToggled = 0x30,        // NnToggleButtonControl: B = its new state (the number
                                     // is also the toggle's own kTogglePropSetState)
    kWinNotifyScrollValue = 0x3e,    // ScrollControl / UiScroll: B = the new value
    kWinNotifyEditChanged = 0x45,    // NnEditControl: A = the text (null when emptied)
    kWinNotifyTextHover = 0x46,      // NnTextControl's hit: handled like kWinNotifyHover
    kWinNotifyToolTip = 0x4f,        // HitTest asks before the child's tooltip; non-zero
                                     // suppresses it
};

// RTTI .?AUIDataViewOwner@@. The base vftable 0x005e5efc holds two _purecall entries; every
// derived face (WinControl 0x005e5e38 and its subclasses) fills them with LAB_0053aaa0 (RET 8)
// and FUN_0053e4a0 (RET 4), and nothing in the exe registers the face: names unknown.
struct IDataViewOwner
{
    virtual void DataViewSlot0(unsigned A, unsigned B) = 0; // +0x00
    virtual void DataViewSlot1(unsigned A) = 0;             // +0x04
};

// The bits of WinControl::PositionFlags.
enum EWinPositionFlag
{
    kWinPositionAddOffset = 0x1,   // the instance is placed at Position + Offset (the ctor sets it)
};

class WinControl : public Win3D, public IDataViewOwner
{
public:
    // FUN_0059f860
    WinControl();
    // FUN_0059fa50
    ~WinControl() override;

    // --- Win3D overrides ---
    // slot 2 FUN_0043d950 -- returns this
    WinControl* AsWinControl() override;
    // slot 4 FUN_005a0340
    bool HitTest(Vector const* Ray) override;
    // slot 5 FUN_005a0880
    void SetPosition(Vector const* NewPosition) override;
    // slot 7 FUN_005a0950
    void SetOrientation(Matrix const* NewOrientation) override;
    // slot 10 FUN_0059fb60
    void Destroy() override;
    // slot 13 FUN_005a0760
    void Draw() override;
    // slot 19 FUN_005a0650
    bool MouseEvent(FWin3DMouseEvent const* Event) override;
    // slot 23 FUN_005a0840 -- 0xef01 show / 0xef02 hide
    bool ProcessMessage(unsigned Msg, unsigned A, unsigned B) override;

    // --- IDataViewOwner (vftable 0x005e5e38) ---
    // LAB_0053aaa0 -- no-op
    void DataViewSlot0(unsigned A, unsigned B) override;
    // FUN_0053e4a0 -- no-op
    void DataViewSlot1(unsigned A) override;

    // --- WinControl slots 28..44 ---
    // slot 28 (+0x70) LAB_0059fd10 -- read the [BaseFrame] ini at Path, then build every other
    // section's class_name through the factory with this as the parent
    virtual void LoadIni(char const* Path);
    // slots 29 / 30 (+0x74 / +0x78) FUN_0053e4a0 -- no-op. Slot 29's argument is a FILE*: the
    // one override seen, NavBar's FUN_0043efd0, fprintf's its [NavBar] section into it (the
    // counterpart of slot 28 LoadIni). Slot 30 is the no-op in every WinControl vftable and no
    // caller is known: its purpose is unknown.
    virtual void SaveIni(FILE* File);
    virtual void Slot30(unsigned A);
    // slot 31 (+0x7c) FUN_005a0250
    virtual void SetOffset(float X, float Y, float Z);
    // slot 32 (+0x80) FUN_005a0300
    virtual void CaptureMouse();
    // slot 33 (+0x84) FUN_005a0320
    virtual void ReleaseMouse();
    // slot 34 (+0x88) FUN_005a0530 -- feed the ToolTip with this control's tooltip id
    virtual void ShowToolTip();
    // slot 35 (+0x8c) FUN_005a09d0
    virtual void RefreshAttachment();
    // slot 36 (+0x90) FUN_005a0a70
    virtual bool GetHardpointTransform(char const* Name, Transform* OutXform);
    // slot 37 (+0x94) FUN_005a0bc0
    virtual WinControl* FindChild(char const* Name);
    // slot 38 (+0x98) FUN_0059fbd0 -- MaterialPath is FUN_0059ec80's second argument.
    virtual void SetMesh(char const* Path, char const* MaterialPath);
    // slot 39 (+0x9c) FUN_00401e90 -- no-op in every WinControl vftable; every Draw ends by
    // calling it
    virtual void PostDraw();
    // slot 40 (+0xa0) FUN_005a1670
    virtual void GetAttachOrientation(Matrix* OutOrientation);
    // slot 41 (+0xa4) FUN_005a1750
    virtual void GetAttachPosition(Vector* OutPosition);
    // slot 42 (+0xa8) FUN_005a13c0 -- property switch 1..11 (see wincontrol_base.cpp). A and B
    // are dwords that are an int, a flag or a pointer depending on Msg.
    virtual intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B);
    // slot 43 (+0xac) FUN_005a1380
    virtual void AddAttachOffset(float const* Delta);
    // slot 44 (+0xb0) FUN_00585090
    virtual bool NameEquals(char const* OtherName);

    // FUN_005a06a0 (thiscall) -- with "DRAW STATIC CONTROLS" on and an instance bound, submit
    // the +0x80 instance with depth test off, tinted by DAT_00679bb4. The head of Draw
    // (FUN_005a0760 inlines it); BaseRichTextWin::Draw (0x0057ae1f) and two HUD draws
    // (0x00492a2c, 0x004d573e) call it directly.
    void DrawStaticMesh();

    // FUN_005a0c30 (thiscall, RET 4) -- every child that answers AsWinControl (slot 2) gets
    // SetProperty(1, bHide, 0); then this control's own Flags: bHide clears bits 0..1, otherwise
    // sets them. Callers: HUD_Target 0x004e2062 / 0x004e3621.
    void SetHiddenWithChildren(bool bHide);

    // --- Child construction (wincontrol_children.cpp) ---
    // Each is thiscall on the parent: FUN_0059db20(Name, <class>, this, 0), then -- when the
    // factory returned an object -- that control's Init with the remaining arguments in order.
    // The return value is FUN_0059db20's result (null when the factory failed).
    //
    // FUN_005a0c90 (RET 0x3c) -- "ButtonControl" + ButtonControl::Init FUN_0055ac30. The Init
    // arguments land at +0xd0 (mesh), +0x350 (string), +0x360 (sound, slot at +0x374), the
    // FUN_0055a490 key lookup (+0x34c / +0xcc), +0xa8 (position), +0x1d0 (hardpoint), +0xb4
    // (half extents), +0x331 bit 0, +0x330 bits 3 and 5, +0x2d1, +0xcc, +0x32c and +0x34c.
    WinControl* CreateButtonControl(char const* Name, char const* MeshPath, char const* Mesh2,
                                    char const* SoundName, char const* KeyCommandName,
                                    Vector const* Position, char const* Hardpoint,
                                    Vector const* HalfExtents, unsigned char bNoMeshRender,
                                    unsigned char bActionButton, unsigned char bNoColorChange,
                                    unsigned char bNoZEnable, int ToolTipIds, int ToolTip2Ids,
                                    int CommandId);
    // FUN_005a0d00 (RET 0x24) -- "WireButtonControl" + WireButtonControl::Init FUN_005a3ee0.
    WinControl* CreateWireButtonControl(char const* Name, char const* SoundName,
                                        Vector const* Position, Vector const* HalfExtents,
                                        unsigned char bAlwaysWhite, unsigned char bNoZEnable,
                                        int ToolTipIds, int CommandId, int EdgeMask);
    // FUN_005a0d60 (RET 0x1c) -- "WireStaticControl" + WireStaticControl::Init FUN_005a3360.
    WinControl* CreateWireStaticControl(char const* Name, Vector const* Position,
                                        Vector const* HalfExtents, unsigned char bAlwaysWhite,
                                        unsigned char bNoZEnable, int ToolTipIds, int EdgeMask);
    // FUN_005a17e0 (RET 0x20) -- "UIScroll" with the FUiPayloadScrollCycle {Width, Height,
    // float(Min), float(Max), float(Page), 1.0f, bHorizontal}; on a non-null result
    // SetScrollOffset(Position) FUN_005972d0, SetValue(0) FUN_00595db0 and SetHorizontal
    // FUN_00597550. Caller: HUD_Status::Create 0x004daf10 ("WeaponListScroll").
    UiScroll* CreateUIScroll(char const* Name, Vector const* Position, float Width, float Height,
                             int Min, int Max, int Page, unsigned char bHorizontal);
    // FUN_005a1870 (RET 0x20) -- FUN_005a17e0 with the class name "UICycle" (0x005e53f4).
    // Caller: NN_Preferences::Create 0x004aa7ab ("video_mode_cycle").
    UiScroll* CreateUICycle(char const* Name, Vector const* Position, float Width, float Height,
                            int Min, int Max, int Page, unsigned char bHorizontal);
    // FUN_005a10e0 (RET 0x14) -- "RichTextWin", then SetNormSize (FUN_00579cc0), SetAttachOffset
    // (FUN_00579c40) and SetAutoScrollbar (FUN_005799f0). Unlike the others it does not test
    // the factory's result before configuring it.
    RichTextWin* CreateRichTextWin(char const* Name, float Width, float Height,
                                   Vector const* AttachOffset, bool bAutoScrollbar);
    // FUN_005a1900 (RET 0x14) -- "UITextEdit" with the FUiPayloadText {Width, Height, Caption};
    // on a non-null result SetAttachOffset (FUN_00579c40). Caller: NN_LoadSave::Create
    // 0x00486739 ("LoadSave_Edit2").
    RichTextEditWin* CreateUiTextEdit(char const* Name, Vector const* AttachOffset, float Width,
                                      float Height, wchar_t* Caption);
    // FUN_005a1950 (RET 0x28) -- "RichTextWireButtonControl" + FUN_005a4810.
    WinControl* CreateRichTextWireButtonControl(char const* Name, char const* SoundName,
                                                Vector const* Position, Vector const* HalfExtents,
                                                unsigned char bAlwaysWhite, unsigned char bNoZEnable,
                                                int ToolTipIds, unsigned CaptionIds,
                                                int CommandId, int EdgeMask);
    // FUN_005a0db0 (RET 0x40) -- "StaticControl" + StaticControl::Init FUN_00588540.
    WinControl* CreateStaticControl(char const* Name, char const* MeshPath, Vector const* Position,
                                    char const* Hardpoint, Vector const* HalfExtents,
                                    unsigned char bNoZEnable, unsigned char bZClear,
                                    unsigned char bNoMeshRender, unsigned char bNoWireRender,
                                    unsigned char bNoColoring, unsigned char bRenderToWindow,
                                    unsigned char bNoClearRender, int ForceRadius,
                                    int RotationSpeed, unsigned char bEnableRotation,
                                    Vector const* StartYpr);
    // FUN_005a0e30 (RET 0x2c) -- "TextControl" + TextControl::Init FUN_0058a110. The arguments
    // follow TextControl's ini keys (font, offset, hardpoint, width, height, font_scale,
    // stringID, justification, tooltip); FontScale is never read. bTopLeftAnchor (+0x378) set:
    // Position is the text rectangle's upper-left corner; clear: its centre.
    WinControl* CreateTextControl(char const* Name, char const* StyleName, Vector const* Position,
                                  char const* Hardpoint, float Width, float Height,
                                  float FontScale, unsigned Ids, int Justification,
                                  int TextToolTipIds, unsigned char bTopLeftAnchor);
    // FUN_005a1080 (RET 0x24) -- "CargoBarControl" + CargoBarControl::Init FUN_0055ee80
    // (cargo_bar_control.cpp). Color / HighlightColor are RGBA byte quads in a dword.
    WinControl* CreateCargoBarControl(char const* Name, char const* MeshPath,
                                      Vector const* NewOffset, char const* Hardpoint,
                                      Vector const* NewHalfExtents, unsigned char bHorizontal,
                                      int ToolTip2Ids, unsigned Color, unsigned HighlightColor);
    // FUN_005a1020 (RET 0x28) -- "ShipViewControl" + ShipViewControl::Init FUN_005842a0
    // (ship_view_control.cpp). RectColor / WireColor point at RGBA byte quads.
    WinControl* CreateShipViewControl(char const* Name, Vector const* RotSpeed,
                                      Vector const* NewOffset, Vector const* Ypr,
                                      Vector const* NewViewSize, char const* Hardpoint,
                                      bool bShowRect, bool bShowWire, unsigned const* RectColor,
                                      unsigned const* WireColor);
    // FUN_005a0e90 (RET 0x28) -- "EditControl" + FUN_0058a110 (TextControl::Init) with the
    // helper's arguments 2..10 and a trailing 0 for Init's last (byte +0x378) argument.
    WinControl* CreateEditControl(char const* Name, char const* StyleName, Vector const* Position,
                                  char const* Hardpoint, float Width, float Height,
                                  float FontScale, unsigned Ids, int Justification,
                                  int TextToolTipIds);
    // FUN_005a0fb0 (RET 0x38) -- "ToggleButtonControl" + ToggleButtonControl::Init FUN_0055c1c0
    // with arguments 2..14. That Init sets byte +0x335 = 1, runs ButtonControl::Init
    // FUN_0055ac30(MeshPath, AltMeshPath, SoundName, KeyCommandName, Position, Hardpoint,
    // HalfExtents, bNoMeshRender, bActionButton, bNoColorChange, bNoZEnable, ToolTipIds,
    // ToolTip2Ids, 0) and loads AltMeshPath into a second mesh holder at +0x378.
    WinControl* CreateToggleButtonControl(char const* Name, char const* MeshPath,
                                          char const* AltMeshPath, char const* SoundName,
                                          char const* KeyCommandName, Vector const* Position,
                                          char const* Hardpoint, Vector const* HalfExtents,
                                          unsigned char bNoMeshRender, unsigned char bActionButton,
                                          unsigned char bNoColorChange, unsigned char bNoZEnable,
                                          int ToolTipIds, int ToolTip2Ids);
    // FUN_005a0ef0 (RET 0x70) -- "ScrollControl" + ScrollControl::Init FUN_00582120
    // (scroll_control.cpp) with the other 27 arguments. Callers: 0x004bf4ec, 0x004c1093.
    WinControl* CreateScrollControl(
        char const* Name, char const* BackMesh, Vector const* BackOffset, char const* BackHp,
        Vector const* BackMouse, bool bBackNoMeshRender, char const* NextMesh,
        Vector const* NextOffset, char const* NextHp, Vector const* NextMouse,
        bool bNextNoMeshRender, char const* PrevMesh, Vector const* PrevOffset,
        char const* PrevHp, Vector const* PrevMouse, bool bPrevNoMeshRender,
        char const* ThumbMesh, Vector const* ThumbOffset0, Vector const* ThumbOffset1,
        char const* ThumbHp0, char const* ThumbHp1, Vector const* ThumbMouse,
        bool bThumbNoMeshRender, int MinRange, int MaxRange, int PageSize,
        bool bSliderBehavior, bool bNoZEnableArg);

    FClientMeshHolder MeshHolder;   // +0x080
    Vector Offset;                  // +0x0a8
    Vector HalfExtents;             // +0x0b4
    Vector HitOffset;               // +0x0c0
    int ToolTipIds;                 // +0x0cc
    char MeshPath[0x100];           // +0x0d0
    char HardpointName[0x100];      // +0x1d0
    unsigned char bCaptured;        // +0x2d0
    unsigned char bNoZEnable;       // +0x2d1
    unsigned char Field2d2;         // +0x2d2 -- property 9 (FUN_005a13c0), also set 1 by some
                                    //  subclasses (FUN_00599f60); no reader found
    unsigned char bIdentityHitOrientation; // +0x2d3
    // +0x2d4 -- not written by the ctor and never read by WinControl itself: the HUD panels park
    // a child's rest Offset here (HUD_Target 0x004e0ee0.., HUD_ContactList 0x004d38e7) and hand
    // it to ButtonMover as the slide start (0x0045ce55, 0x004e4188).
    Vector HomeOffset;
    // +0x2e0 -- ctor {0, 0, 10000.0f}; the ButtonControl, StaticControl, TextControl and
    // WireStaticControl SetPosition overrides store their NewPosition here. No reader found.
    Vector RequestedPosition;
    int UserData;                   // +0x2ec -- ctor 0
    int OwnerData;                   // +0x2f0 -- ctor 0
    unsigned char PositionFlags;    // +0x2f4 -- bit0: instance position includes +0xa8
    unsigned char Pad2f5[3];        // +0x2f5
    // +0x2f8: the orientation, then the position of the hardpoint this control is attached to
    // -- together the Transform GetHardpointTransform fills.
    union
    {
        struct
        {
            Matrix AttachOrientation;   // +0x2f8
            Vector AttachPosition;      // +0x31c
        };
        Transform AttachXform;          // +0x2f8
    };
    unsigned char bAttached;        // +0x328
    unsigned char Pad329[3];        // +0x329

private:
    WinControl(WinControl const&) = delete;
    WinControl& operator=(WinControl const&) = delete;
};

static_assert(sizeof(WinControl) == 0x32c, "retail WinControl is 0x32c bytes");
static_assert(offsetof(WinControl, MeshHolder) == 0x80, "WinControl +0x80 mesh holder");
static_assert(offsetof(WinControl, Offset) == 0xa8, "WinControl +0xa8 offset");
static_assert(offsetof(WinControl, HalfExtents) == 0xb4, "WinControl +0xb4 half extents");
static_assert(offsetof(WinControl, HitOffset) == 0xc0, "WinControl +0xc0 hit offset");
static_assert(offsetof(WinControl, ToolTipIds) == 0xcc, "WinControl +0xcc tooltip");
static_assert(offsetof(WinControl, MeshPath) == 0xd0, "WinControl +0xd0 mesh path");
static_assert(offsetof(WinControl, HardpointName) == 0x1d0, "WinControl +0x1d0 hardpoint");
static_assert(offsetof(WinControl, bCaptured) == 0x2d0, "WinControl +0x2d0 captured");
static_assert(offsetof(WinControl, RequestedPosition) == 0x2e0, "WinControl +0x2e0");
static_assert(offsetof(WinControl, PositionFlags) == 0x2f4, "WinControl +0x2f4 flags");
static_assert(offsetof(WinControl, AttachOrientation) == 0x2f8, "WinControl +0x2f8 attach");
static_assert(offsetof(WinControl, AttachPosition) == 0x31c, "WinControl +0x31c attach pos");
static_assert(offsetof(WinControl, bAttached) == 0x328, "WinControl +0x328 attached");

// FUN_005a1340 (thiscall on HalfExtents, RET 4; wincontrol_children.cpp) -- the mesh's box via
// FUN_0059f0d0; on success HalfExtents = {box[0], box[2], 0}. Callers pass a control's +0xb4
// and its +0x80 holder (NN_Inventory 0x004782b1 and nine other windows).
bool WinControlHalfExtentsFromMesh(Vector* HalfExtents, FClientMeshHolder* Holder);
