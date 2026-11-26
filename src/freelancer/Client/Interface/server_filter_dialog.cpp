// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/server_filter_dialog.h"
#include "msvcrt_compat.h" // MsvcrtWtoi: retail MSVCRT.DLL wraps mod 2^32, the UCRT saturates

#include "Core/client_state.h"     // CallClientStateUpdateSlot
#include "Core/globals.h"          // PumpMessages, GetClientFrameTimeCapped
#include "Common/Fonts/ids_rdl.h"          // LoadIdsIntoRdl, RdlAppendNode
#include "NeuroNet/nn_ui_pulse.h"      // G_HudChromeColorRgba
#include "Interface/rich_text_win.h"
#include "Interface/tooltip_hover.h"    // FToolTipHoverInfo
#include "Interface/win3d.h"
#include "Interface/win_factory.h"
#include "NeuroNet/Chat/chat_init.h"        // EChatTextStyle

#include "common_text.h"

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

// DAT_0067a444 (static init LAB_005626e0, atexit LAB_00562700).
WINFACTORY<ServerFilterDialog> G_ServerFilterDialogFactory("ServerFilterDialog");

namespace
{
    // DAT_0067a440 (static init LAB_00562710): -0.2f + 0.03f, the exact sum rounded once, as
    // the x87 does.
    float const G_FilterLabelX =
        static_cast<float>(static_cast<double>(-0.200000003f) + static_cast<double>(0.0299999993f));

    // DAT_0067a43c (static init LAB_00562730). Both sums are exact in double, so the one
    // rounding to float matches the x87's.
    float const G_FilterDataX = static_cast<float>(static_cast<double>(0.280000001f) +
                                                   static_cast<double>(G_FilterLabelX) +
                                                   static_cast<double>(0.0299999993f));

    // DAT_00614828. Row 0's Y and DataX (0.12f) are linked data; every other field is a store
    // of the static initializer LAB_00562750.
    FServerFilterRowDesc const G_ServerFilterRows[5] = {
        {0.119999997f, 0.0f, -0.200000003f, G_FilterLabelX, 0.119999997f, 1, 0x60078, 0x1b58},
        {0.0599999987f, 0.0f, -0.200000003f, G_FilterLabelX, G_FilterDataX, 0, 0x60074, 0x1b59},
        {0.0f, 0.0f, -0.200000003f, G_FilterLabelX, G_FilterDataX, 0, 0x60073, 0x1b5a},
        {-0.0599999987f, 0.0f, -0.200000003f, G_FilterLabelX, G_FilterDataX, 0, 0x60077, 0x1b5c},
        {-0.119999997f, 0.0f, -0.200000003f, G_FilterLabelX, G_FilterDataX, 0, 0x60075, 0x1b5d},
    };

    // DAT_006148c8 -- the number in the rows' names ("FilterCheck%d" ...), linked as 1 and
    // raised by every BuildRow; nothing resets it, so each dialog gets fresh names.
    int G_ServerFilterRowCounter = 1;

    // PTR_s_ServerFilterDialog_006148cc.
    char const* const kServerFilterDialogClass = "ServerFilterDialog";

    char const kCheckMesh[] = "Interface\\NeuroNet\\Inventory\\check.3db";                 // 005d4e20
    char const kCheckAltMesh[] = "Interface\\NeuroNet\\Inventory\\repairblackbox.3db";     // 005cb0d0
    char const kCheckSound[] = "ui_shipview_select";                                      // 005d4e48
    char const kBackPlaneMesh[] = "Interface\\NeuroNet\\MISC_bigblackplane.3db";          // 005d21b0
    char const kBackgroundMesh[] = "Interface\\intro\\objects\\MULTI_attentionreq'd.cmp"; // 005ce934
    char const kCancelMesh[] = "Interface\\NeuroNet\\Inventory\\X.3db";                   // 005cf1f4
    char const kSelectSound[] = "ui_select_item";                                         // 005ced30

    // The clone-into-list idiom Build uses for the edit box's two lists.
    void AppendNodeClone(RenderDisplayList& Rdl, RDLNode const& Node)
    {
        RDLNode* const Temp = Node.Clone();
        RdlAppendNode(Rdl, Temp->Clone());
        delete Temp;
    }
}

ServerFilterDialog::ServerFilterDialog()
    : OkButton(nullptr)
    , CancelButton(nullptr)
    , Title(nullptr)
    , Target(nullptr)
{
    // Inlined into FUN_00563900.
}

ServerFilterDialog::~ServerFilterDialog()
{
    // FUN_00562f60. The ServerFilter at +0x378 has no destructor.
}

void FServerFilterDialogRow::Build(WinControl* Dialog, FServerFilterRowDesc const& Desc)
{
    // FUN_00562a60. The names go through a 0x2000-byte _vsnprintf buffer.
    char Name[0x2000];
    Vector const* const DialogOffset = &Dialog->Offset; // +0xa8

    // The row's tool tip twice, then property 0x35 = 1.
    Vector const CheckOffset = {Desc.X, Desc.Y, Desc.Z};
    Vector const CheckHalfExtents = {0.0120000001f, 0.0120000001f, 0.0f};
    _snprintf(Name, 0x2000, "FilterCheck%d", G_ServerFilterRowCounter);
    Check = Dialog->CreateToggleButtonControl(Name, kCheckMesh, kCheckAltMesh, kCheckSound,
                                              nullptr, &CheckOffset, nullptr, &CheckHalfExtents,
                                              0, 0, 0, 0, Desc.ToolTipIds,
                                              Desc.ToolTipIds);
    Check->SetProperty(kButtonPropGlow, 1, 0);

    _snprintf(Name, 0x2000, "FilterLabel%d", G_ServerFilterRowCounter);
    Label = static_cast<RichTextWin*>(WinFactoryCreate(Name, "RichTextWin", Dialog, nullptr));
    {
        RenderDisplayList Text;
        LoadIdsIntoRdl(Desc.LabelIds, Text); // result ignored
        Label->ClearContent();
        {
            StyleNode const Style(0x5724);
            Label->AppendNode(Style);
        }
        Label->AppendContent(Text);
    }

    // Summed in double in this order and rounded to float.
    Vector const LabelOffset = {
        static_cast<float>(static_cast<double>(Desc.LabelX) +
                           static_cast<double>(DialogOffset->x)),
        static_cast<float>((0.0299999993f * 0.5 + static_cast<double>(Desc.Y)) +
                           static_cast<double>(DialogOffset->y)),
        static_cast<float>(static_cast<double>(Desc.Z) + static_cast<double>(DialogOffset->z))};
    Label->SetAttachOffset(&LabelOffset);

    Label->SetNormSize(Desc.bHasData != 0 ? 0.280000001f : 0.379999995f, 0.0299999993f);

    Label->GetToolTipHoverInfo()->IdsAlt = Desc.ToolTipIds;
    Label->SetToolTipEnabled(true);
    Label->SetAlignMode(kRichAlignMiddle);

    if (Desc.bHasData == 0)
    {
        Data = nullptr;
    }
    else
    {
        // The edit box: fixed style 0x9ca4, no IME, a centred one-space prefix, four characters.
        _snprintf(Name, 0x2000, "FilterData%d", G_ServerFilterRowCounter);
        Data = static_cast<RichTextEditWin*>(
            WinFactoryCreate(Name, "RichTextEditWin", Dialog, nullptr));
        Vector const DataOffset = {
            static_cast<float>(static_cast<double>(Desc.DataX) +
                               static_cast<double>(DialogOffset->x)),
            static_cast<float>((0.0299999993f * 0.5 + static_cast<double>(Desc.Y)) +
                               static_cast<double>(DialogOffset->y)),
            static_cast<float>(static_cast<double>(Desc.Z) +
                               static_cast<double>(DialogOffset->z))};
        Data->SetAttachOffset(&DataOffset);
        Data->SetNormSize(0.100000001f, 0.0299999993f);
        Data->SetFixedStyle(true);
        Data->SetImeEnabled(false);
        {
            StyleNode const Style(kTextStyleBody);
            AppendNodeClone(Data->FixedStyle, Style);
        }
        {
            JustifyNode const Justify(TextJustifyCenter);
            AppendNodeClone(Data->Prefix, Justify);
        }
        {
            TextNode const Space(L" ", -1);                   // DAT_005cef90
            AppendNodeClone(Data->Prefix, Space);
        }
        Data->SetMaxLength(4);
        Data->SetDrawFrame(true);
        Data->SetFrameColor(
            reinterpret_cast<unsigned long const*>(G_HudChromeColorRgba));
        Data->SetAlignMode(kRichAlignMiddle);
    }

    G_ServerFilterRowCounter = G_ServerFilterRowCounter + 1;
}

void ServerFilterDialog::ReadControls()
{
    // FUN_00562fd0. Toggle state 0 is "on" (property 0x2f reads it; SETZ). Retail: the five rows
    // write +0x04 / +0x05 / +0x06 / +0x08 / +0x09; bFilterAllowNewPlayers (+0x07) is never
    // edited by the dialog (nor shown by WriteControls).
    if (Target == nullptr)
        return;
    int State = 0;
    Rows[0].Check->SetProperty(kTogglePropGetState, reinterpret_cast<uintptr_t>(&State), 0);
    Target->bFilterPing = static_cast<unsigned char>(State == 0);
    Rows[1].Check->SetProperty(kTogglePropGetState, reinterpret_cast<uintptr_t>(&State), 0);
    Target->bFilterKnown = static_cast<unsigned char>(State == 0);
    Rows[2].Check->SetProperty(kTogglePropGetState, reinterpret_cast<uintptr_t>(&State), 0);
    Target->bFilterVersion = static_cast<unsigned char>(State == 0);
    Rows[3].Check->SetProperty(kTogglePropGetState, reinterpret_cast<uintptr_t>(&State), 0);
    Target->bFilterPassword = static_cast<unsigned char>(State == 0);
    Rows[4].Check->SetProperty(kTogglePropGetState, reinterpret_cast<uintptr_t>(&State), 0);
    Target->bFilterLan = static_cast<unsigned char>(State == 0);

    // The ping box's first 15 characters into the maximum; the minimum back to 0.
    wchar_t Text[0x10];
    Rows[0].Data->GetText(Text, 0x10);
    Target->PingMax = static_cast<unsigned>(MsvcrtWtoi(Text));
    Target->PingMin = 0;
}

void ServerFilterDialog::WriteControls()
{
    // FUN_005630f0. Property 0x30 with the enable byte (the toggle turns a zero into state 1).
    if (Target == nullptr)
        return;
    Rows[0].Check->SetProperty(kTogglePropSetState, Target->bFilterPing, 0);
    Rows[1].Check->SetProperty(kTogglePropSetState, Target->bFilterKnown, 0);
    Rows[2].Check->SetProperty(kTogglePropSetState, Target->bFilterVersion, 0);
    Rows[3].Check->SetProperty(kTogglePropSetState, Target->bFilterPassword, 0);
    Rows[4].Check->SetProperty(kTogglePropSetState, Target->bFilterLan, 0);

    wchar_t Text[0x10];
    _ultow(Target->PingMax, Text, 10);
    Rows[0].Data->SetText(Text, nullptr);
}

void ServerFilterDialog::Close()
{
    ReleaseMouse();
    Win3D* Child = ChildHead;
    while (Child != nullptr)
    {
        Win3D* const Next = Child->NextSibling;
        Win3DDeleteTree(Child);
        Child = Next;
    }
    Destroy();
    delete this;
}

bool ServerFilterDialog::Create(Win3D* NewParent, void* Data)
{
    // FUN_005631c0.
    if (!Win3D::Create(NewParent, nullptr))
        return false;

    Offset.z = -1.0f;
    SetMesh(kBackPlaneMesh, nullptr);

    // "Filter_BG" 0.01 behind the plane; not kept.
    Vector const BackgroundOffset = {Offset.x, Offset.y, Offset.z + 0.00999999978f};
    Vector const ZeroHalfExtents = {0.0f, 0.0f, 0.0f};
    Vector const ZeroVector338 = {0.0f, 0.0f, 0.0f};
    CreateStaticControl("Filter_BG", kBackgroundMesh, &BackgroundOffset, nullptr, &ZeroHalfExtents,
                        0, 0, 0, 1, 1, 0, 1, 0, 0, 0, &ZeroVector338);

    Target = static_cast<ServerFilter*>(Data);
    Backup = *Target;

    Title = static_cast<RichTextWin*>(WinFactoryCreate("FilterTitle", "RichTextWin", this, nullptr));
    Vector const TitleOffset = {Offset.x + -0.150000006f, Offset.y + 0.189999998f, Offset.z};
    Title->SetAttachOffset(&TitleOffset);
    Title->SetNormSize(0.300000012f, 0.0399999991f);
    {
        RenderDisplayList Text;
        LoadIdsIntoRdl(0x60079, Text); // result ignored
        Title->ClearContent();
        {
            StyleNode const Style(kTextStyleBody);
            Title->AppendNode(Style);
        }
        {
            StyleNode const Style(kTextStyleScreenTitle);
            Title->AppendNode(Style);
        }
        Title->AppendContent(Text);
    }

    Vector const CancelOffset = {0.224999994f, 0.174999997f, -1.0f};
    Vector const CancelHalfExtents = {0.0199999996f, 0.0199999996f, 0.0f};
    CancelButton = CreateButtonControl("FilterCancel", kCancelMesh, nullptr, kSelectSound, nullptr,
                                       &CancelOffset, nullptr, &CancelHalfExtents, 0, 0, 0, 0,
                                       0x409, 0, 0);

    Vector const OkOffset = {0.0f, -0.170000002f, -1.0f};
    Vector const OkHalfExtents = {0.0500000007f, 0.0170000009f, 0.0f};
    OkButton = CreateRichTextWireButtonControl("FilterOk", kSelectSound, &OkOffset, &OkHalfExtents,
                                               0, 0, 0xbdb, 0x411, 0, 0);

    for (int Index = 0; Index < 5; ++Index)
        Rows[Index].Build(this, G_ServerFilterRows[Index]);

    WriteControls();
    CaptureMouse();
    if (Rows[0].Data != nullptr)
        Rows[0].Data->Activate();
    return true;
}

bool ServerFilterDialog::MouseEvent(FWin3DMouseEvent const* Event)
{
    // FUN_005635a0. On a button-down the edit boxes are dropped one by one until the mouse is
    // over one of them or its label, which then takes the focus. Every mouse event is taken.
    if (Event->Action == kWin3DMousePress)
    {
        bool bFocused = false;
        for (int Index = 0; Index < 5; ++Index)
        {
            if (bFocused)
                continue;
            RichTextEditWin* const Data = Rows[Index].Data;
            if (Data == nullptr)
                continue;
            Data->Deactivate(false);
            Vector Ray;
            if (Data->HitTest(Win3DGetMouseRay(&Ray)) ||
                Rows[Index].Label->HitTest(Win3DGetMouseRay(&Ray)))
            {
                Data->Activate();
                bFocused = true;
            }
        }
    }
    return true;
}

bool ServerFilterDialog::KeyEvent(unsigned Key, unsigned char* Modifiers)
{
    // FUN_00563530. Escape (on its release, not the press) cancels: the copy back, then the close.
    if ((*Modifiers & kWin3DKeyPressed) != 0 || Key != VK_ESCAPE)
        return false;
    *Target = Backup;
    Close();
    return true;
}

bool ServerFilterDialog::ProcessMessage(unsigned Msg, unsigned A, unsigned B)
{
    // FUN_00563630.
    if (Msg == kWinMsgEnter)
    {
        // The ping box finished.
        RichTextEditWin* const Data = Rows[0].Data;
        if (reinterpret_cast<uintptr_t>(Data) == static_cast<uintptr_t>(A))
        {
            if (Data != nullptr)
                Data->Deactivate(false);
            ReadControls();
            return true;
        }
    }
    else if (Msg == kWinMsgEscape)
    {
        *Target = Backup;
        Close();
        return true;
    }
    return WinControl::ProcessMessage(Msg, A, B);
}

intptr_t ServerFilterDialog::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // FUN_005636d0.
    if (Msg == kWinNotifyClick)
    {
        if (A == reinterpret_cast<uintptr_t>(OkButton))
        {
            ReadControls();
            Close();
            return 0;
        }
        if (A == reinterpret_cast<uintptr_t>(CancelButton))
        {
            *Target = Backup;
            Close();
            return 0;
        }
    }
    else if (Msg == kWinNotifyToggled)
    {
        // A toggle changed.
        ReadControls();
        return 0;
    }
    return WinControl::SetProperty(Msg, A, B);
}

bool ServerFilterDialogRun(Win3D* Parent, ServerFilter* Filter)
{
    // FUN_005637b0.
    Win3D* const Created =
        WinFactoryCreate(kServerFilterDialogClass, kServerFilterDialogClass, Parent, Filter);
    if (Created == nullptr)
        return true;
    ServerFilterDialog* const Dialog = static_cast<ServerFilterDialog*>(Created);
    Vector const Zero = {0.0f, 0.0f, 0.0f};
    Dialog->SetProperty(kWinPropPlaceAtOffset, reinterpret_cast<uintptr_t>(&Zero), 0);
    Dialog->CaptureMouse();

    int ExitCode = 0;
    while (Win3DFindByClassName(kServerFilterDialogClass) != nullptr)
    {
        if (!PumpMessages(&ExitCode))
            return false;
        double const Delta = GetClientFrameTimeCapped();
        Win3DUpdateAll(Delta);
        CallClientStateUpdateSlot(static_cast<float>(Delta));
    }
    return true;
}
