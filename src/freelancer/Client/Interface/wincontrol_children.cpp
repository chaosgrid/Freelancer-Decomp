// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/wincontrol_base.h"

#include "NeuroNet/nn_button_control.h"
#include "NeuroNet/nn_static_control.h"
#include "NeuroNet/nn_text_control.h"
#include "Interface/rich_text_win.h"
#include "Interface/ui_page_desc.h"
#include "Interface/ui_scroll.h"
#include "Interface/win_factory.h"
#include "Interface/wire_controls.h"

// Retail WinControl's child-construction helpers (0x005a0c90..0x005a0ee5, 0x005a10e0,
// 0x005a1950). Each is FUN_0059db20(Name, <class name>, this, 0), then on a non-null result a
// direct call to that class's Init with the remaining arguments in order.

WinControl* WinControl::CreateButtonControl(char const* Name, char const* MeshPath,
                                            char const* String350, char const* SoundName,
                                            char const* KeyCommandName, float const* Position,
                                            char const* Hardpoint, float const* HalfExtents,
                                            unsigned char bFlag331, unsigned char bFlag330Bit3,
                                            unsigned char bFlag330Bit5, unsigned char bNoZEnable,
                                            int ToolTipIds, int Dword32c, int Dword34c)
{
    // FUN_005a0c90 (ButtonControl::Init FUN_0055ac30).
    Win3D* const Created = WinFactoryCreate(Name, "ButtonControl", this, nullptr);
    if (Created != nullptr)
    {
        NnButtonControl* const Button = static_cast<NnButtonControl*>(Created);
        Button->Init(MeshPath, String350, SoundName, KeyCommandName, Position, Hardpoint,
                     HalfExtents, bFlag331, bFlag330Bit3, bFlag330Bit5, bNoZEnable, ToolTipIds,
                     Dword32c, Dword34c);
    }
    return static_cast<WinControl*>(Created);
}

WinControl* WinControl::CreateWireButtonControl(char const* Name, char const* SoundName,
                                                float const* Position, float const* HalfExtents,
                                                unsigned char bFlag3, unsigned char bNoZEnable,
                                                int ToolTipIds, int CommandId, int WireFlags)
{
    // FUN_005a0d00.
    Win3D* const Created = WinFactoryCreate(Name, "WireButtonControl", this, nullptr);
    if (Created != nullptr)
    {
        WireButtonControl* const Wire = static_cast<WireButtonControl*>(Created);
        Wire->Init(SoundName, Position, HalfExtents, bFlag3, bNoZEnable, ToolTipIds, CommandId,
                   static_cast<unsigned>(WireFlags)); // FUN_005a3ee0
    }
    return static_cast<WinControl*>(Created);
}

WinControl* WinControl::CreateWireStaticControl(char const* Name, float const* Position,
                                                float const* HalfExtents, unsigned char bFlag3,
                                                unsigned char bNoZEnable, int ToolTipIds,
                                                int WireFlags)
{
    // FUN_005a0d60.
    Win3D* const Created = WinFactoryCreate(Name, "WireStaticControl", this, nullptr);
    if (Created != nullptr)
    {
        WireStaticControl* const Wire = static_cast<WireStaticControl*>(Created);
        Wire->Init(Position, HalfExtents, bFlag3, bNoZEnable, ToolTipIds,
                   static_cast<unsigned>(WireFlags)); // FUN_005a3360
    }
    return static_cast<WinControl*>(Created);
}

WinControl* WinControl::CreateStaticControl(char const* Name, char const* MeshPath,
                                            float const* Position, char const* Hardpoint,
                                            float const* HalfExtents, unsigned char bNoZEnable,
                                            unsigned char Byte34c, unsigned char Byte344,
                                            unsigned char Byte345, unsigned char Byte346,
                                            unsigned char Byte34b, unsigned char Byte34a,
                                            int Int418, int Int41c, unsigned char Byte349,
                                            float const* Vector338)
{
    // FUN_005a0db0 (StaticControl::Init FUN_00588540).
    Win3D* const Created = WinFactoryCreate(Name, "StaticControl", this, nullptr);
    if (Created != nullptr)
    {
        NnStaticControl* const Static = static_cast<NnStaticControl*>(Created);
        Static->Init(MeshPath, Position, Hardpoint, HalfExtents, bNoZEnable, Byte34c, Byte344,
                     Byte345, Byte346, Byte34b, Byte34a, Int418, Int41c, Byte349, Vector338);
    }
    return static_cast<WinControl*>(Created);
}

WinControl* WinControl::CreateTextControl(char const* Name, char const* StyleName,
                                          float const* Position, char const* Hardpoint,
                                          float Width, float Height, float Arg6, unsigned Ids,
                                          int Dword358, int Dword344, unsigned char Byte378)
{
    // FUN_005a0e30 (TextControl::Init FUN_0058a110).
    Win3D* const Created = WinFactoryCreate(Name, "TextControl", this, nullptr);
    if (Created != nullptr)
    {
        NnTextControl* const Text = static_cast<NnTextControl*>(Created);
        Text->Init(StyleName, Position, Hardpoint, Width, Height, Arg6, Ids, Dword358, Dword344,
                   Byte378);
    }
    return static_cast<WinControl*>(Created);
}

UiScroll* WinControl::CreateUIScroll(char const* Name, float const* Position, float Width,
                                     float Height, int Min, int Max, int Page,
                                     unsigned char bHorizontal)
{
    // FUN_005a17e0.
    FUiPayloadScrollCycle Payload;
    Payload.Right = Width;
    Payload.Bottom = Height;
    Payload.Extra0 = static_cast<float>(Min);
    Payload.Extra1 = static_cast<float>(Max);
    Payload.Extra2 = static_cast<float>(Page);
    Payload.Extra3 = 1.0f;
    Payload.Flag = bHorizontal;
    UiScroll* const Scroll = static_cast<UiScroll*>(WinFactoryCreate(Name, "UIScroll", this,
                                                                      &Payload));
    if (Scroll != nullptr)
    {
        Scroll->SetScrollOffset(Position);   // FUN_005972d0
        Scroll->SetValue(0.0f);              // FUN_00595db0
        Scroll->SetHorizontal(bHorizontal != 0); // FUN_00597550
    }
    return Scroll;
}

UiScroll* WinControl::CreateUICycle(char const* Name, float const* Position, float Width,
                                    float Height, int Min, int Max, int Page,
                                    unsigned char bHorizontal)
{
    // FUN_005a1870: FUN_005a17e0 with the factory name "UICycle".
    FUiPayloadScrollCycle Payload;
    Payload.Right = Width;
    Payload.Bottom = Height;
    Payload.Extra0 = static_cast<float>(Min);
    Payload.Extra1 = static_cast<float>(Max);
    Payload.Extra2 = static_cast<float>(Page);
    Payload.Extra3 = 1.0f;
    Payload.Flag = bHorizontal;
    UiScroll* const Cycle = static_cast<UiScroll*>(WinFactoryCreate(Name, "UICycle", this,
                                                                     &Payload));
    if (Cycle != nullptr)
    {
        Cycle->SetScrollOffset(Position);   // FUN_005972d0
        Cycle->SetValue(0.0f);              // FUN_00595db0
        Cycle->SetHorizontal(bHorizontal != 0); // FUN_00597550
    }
    return Cycle;
}

RichTextWin* WinControl::CreateRichTextWin(char const* Name, float Width, float Height,
                                           float const* AttachOffset, bool bAutoScrollbar)
{
    // FUN_005a10e0: retail does not test the factory's result for null.
    RichTextWin* const Text =
        static_cast<RichTextWin*>(WinFactoryCreate(Name, "RichTextWin", this, nullptr));
    Text->SetNormSize(Width, Height);          // FUN_00579cc0
    Text->SetAttachOffset(AttachOffset);       // FUN_00579c40
    Text->SetAutoScrollbar(bAutoScrollbar);    // FUN_005799f0
    return Text;
}

RichTextEditWin* WinControl::CreateUiTextEdit(char const* Name, float const* AttachOffset,
                                              float Width, float Height, wchar_t* Caption)
{
    // FUN_005a1900.
    FUiPayloadText Payload;
    Payload.Right = Width;
    Payload.Bottom = Height;
    Payload.CaptionUtf16 = Caption;
    RichTextEditWin* const Edit =
        static_cast<RichTextEditWin*>(WinFactoryCreate(Name, "UITextEdit", this, &Payload));
    if (Edit != nullptr)
        Edit->SetAttachOffset(AttachOffset);
    return Edit;
}

WinControl* WinControl::CreateRichTextWireButtonControl(char const* Name, char const* SoundName,
                                                        float const* Position,
                                                        float const* HalfExtents,
                                                        unsigned char bFlag3,
                                                        unsigned char bNoZEnable, int ToolTipIds,
                                                        unsigned CaptionIds, int CommandId,
                                                        int WireFlags)
{
    // FUN_005a1950.
    Win3D* const Created = WinFactoryCreate(Name, "RichTextWireButtonControl", this, nullptr);
    if (Created != nullptr)
    {
        RichTextWireButtonControl* const Wire = static_cast<RichTextWireButtonControl*>(Created);
        Wire->Init(SoundName, Position, HalfExtents, bFlag3, bNoZEnable, ToolTipIds, CaptionIds,
                   CommandId, static_cast<unsigned>(WireFlags)); // FUN_005a4810
    }
    return static_cast<WinControl*>(Created);
}

WinControl* WinControl::CreateEditControl(char const* Name, char const* StyleName,
                                          float const* Position, char const* Hardpoint,
                                          float Width, float Height, float Arg6, unsigned Ids,
                                          int Dword358, int Dword344)
{
    // FUN_005a0e90 (factory DAT_0067d860; TextControl::Init with a trailing 0).
    Win3D* const Created = WinFactoryCreate(Name, "EditControl", this, nullptr);
    if (Created != nullptr)
    {
        NnEditControl* const Edit = static_cast<NnEditControl*>(Created);
        Edit->Init(StyleName, Position, Hardpoint, Width, Height, Arg6, Ids, Dword358, Dword344,
                   0); // FUN_0058a110
    }
    return static_cast<WinControl*>(Created);
}

WinControl* WinControl::CreateToggleButtonControl(char const* Name, char const* MeshPath,
                                                  char const* AltMeshPath, char const* SoundName,
                                                  char const* KeyCommandName,
                                                  float const* Position, char const* Hardpoint,
                                                  float const* HalfExtents,
                                                  unsigned char bFlag331,
                                                  unsigned char bFlag330Bit3,
                                                  unsigned char bFlag330Bit5,
                                                  unsigned char bNoZEnable, int ToolTipIds,
                                                  int Dword32c)
{
    // FUN_005a0fb0 (factory DAT_00679a80; ToggleButtonControl::Init FUN_0055c1c0).
    Win3D* const Created = WinFactoryCreate(Name, "ToggleButtonControl", this, nullptr);
    if (Created != nullptr)
    {
        static_cast<NnToggleButtonControl*>(Created)->Init(
            MeshPath, AltMeshPath, SoundName, KeyCommandName, Position, Hardpoint, HalfExtents,
            bFlag331, bFlag330Bit3, bFlag330Bit5, bNoZEnable, ToolTipIds, Dword32c);
    }
    return static_cast<WinControl*>(Created);
}

bool WinControlHalfExtentsFromMesh(float* HalfExtents, FClientMeshHolder* Holder)
{
    // FUN_005a1340.
    FClientMeshAabb Box;
    if (!ClientMeshQueryAabb(Holder, &Box))
        return false;
    HalfExtents[0] = Box.Values[0];
    HalfExtents[1] = Box.Values[2];
    HalfExtents[2] = 0.0f;
    return true;
}
