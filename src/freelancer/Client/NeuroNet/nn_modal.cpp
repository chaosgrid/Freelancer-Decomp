// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "NeuroNet/nn_modal.h"

#include "Interface/Pages/character_select_equip.h" // GetPlayerBaseIdRetail (DAT_00673358), GetPlayerStarSystemIdRetail
#include "NeuroNet/Economy/client_base_market.h" // GetClientCurrentLocationBaseData (FUN_0043b630)
#include "Core/globals.h"       // SetClientServerTickPause (FUN_0042d6f0), GetClientFrameTimeCapped
#include "Space/HUD/hud_comm.h"      // HudCommIsWindowOpen (FUN_004cc8d0)
#include "Common/Fonts/ids_strings.h"   // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "Common/input_manager.h" // Get/SetClientMouseCursorDrawFlag (FUN_0041dd90 / FUN_0041dda0)
#include "GameFlow/menu_universe_base.h" // GetClientCurrentBaseStridName (FUN_0043bbe0)
#include "NeuroNet/Economy/infocard_map.h"  // InfocardMapCreate / InfocardMapDestroy (FUN_00458790 / FUN_00458860)
#include "NeuroNet/Economy/knowledge_map.h" // KnowledgeMapCreate / KnowledgeMapDestroy (FUN_00459830 / FUN_00459900)
#include "Interface/cargo_bar_control.h"
#include "Common/Fonts/font_string.h"   // FontStringConstruct (FUN_004144b0)
#include "Interface/menu_ui_widgets.h"
#include "NeuroNet/nn_rollover.h"   // NnRolloverShowHoverGlobal (FUN_004b62d0)
#include "Interface/tooltip_hover.h" // FToolTipHoverInfo, ToolTipHoverReset (FUN_004963a0)
#include "GameFlow/nav_bar.h"       // NavBarHasServiceWindow (FUN_00442720)
#include "NeuroNet/nn_arrival_text.h"
#include "NeuroNet/nn_inventory.h"  // IsPlayerDockedRetail (FUN_004c4810)
#include "NeuroNet/nn_main_button.h"
#include "NeuroNet/nn_messaging.h"  // NnVoiceInit (FUN_00489de0)
#include "NeuroNet/nn_trade.h"      // G_NnTrade (DAT_006724b4)
#include "NeuroNet/nn_story_star.h" // G_MissionObjectiveWindow (DAT_00672400), StoryStarReset
#include "NeuroNet/nn_text_control.h"
#include "NeuroNet/nn_text_scratch.h" // G_NnTextScratchA/B, G_NnTextScratchCapacity
#include "NeuroNet/nn_ui_pulse.h"   // G_NnPalettePaleGreen (DAT_00679b90)
#include "Space/player_behavior.h" // OnMouseFlightRawMessage (FUN_004c80f0)
#include "GameFlow/room_control.h"  // FRoomRuntime, GetRoomRuntimeRoomId (runtime +0x38)
#include "Space/space_letterbox.h" // IsLetterboxVisible (FUN_0041a3e0)
#include "Interface/win3d.h"         // Win3DDeleteTree (FUN_0043d2a0)
#include "Interface/win_factory.h"   // WinFactoryCreate (FUN_0059db20)

#include "common_goods.h"  // BaseData::get_base_id
#include "common_id.h"     // CreateID, MakeLocationID
#include "common_universe.h"

#include <stdio.h>
#include <wchar.h>
#include <windows.h>

bool G_bNnScreenUp = false;              // DAT_0067dcc8
NN_MainButton* G_NnMainButton = nullptr; // DAT_0066d3f0
NnArrivalText* G_NnArrivalText = nullptr; // DAT_0066d398

// Retail DAT_0066d35c -- WINFACTORY<NN_ArrivalText> static (LAB_0045ab60 -> FUN_0045b4c0; Create
// FUN_0045b530; atexit LAB_0045ab80 -> FUN_00579380).
WINFACTORY<NnArrivalText> G_NnArrivalTextFactory("NN_ArrivalText");

namespace
{
    // DAT_0066d344 -- set by the static initializer LAB_0045a0e0 to
    // CreateID("click_on_characters_and_buttons"); only reader FUN_0045a270 (0x0045a37e).
    unsigned const G_NnVoiceClickOnCharactersId = CreateID("click_on_characters_and_buttons");

    // Port-only helper for the head every cdecl entry point spells out (0x0045b2d0, 0x0045b310,
    // 0x0045b340, 0x0045b430, 0x0045b490): create the window when DAT_0066d398 is null, then
    // re-read the pointer.
    NnArrivalText* EnsureNnArrivalText()
    {
        if (G_NnArrivalText == nullptr)
            WinFactoryCreate("NN_ArrivalText", "NN_ArrivalText", nullptr, nullptr);
        return G_NnArrivalText;
    }

    // The strid_name of the player's system, or 0 (inlined at 0x0045aebd, 0x0045b004 and
    // 0x0045b1d4).
    int PlayerSystemStridName()
    {
        Universe::ISystem const* const System =
            Universe::get_system(GetPlayerStarSystemIdRetail());
        if (System == nullptr)
            return 0;
        return System->GetStridName();
    }

    // .rdata 0x005ce928: the typing time per character ShowBaseArrival reserves.
    constexpr float kArrivalSecondsPerChar = 0.06f;
    // Deviation: each Show* reads its format (ids 0x55c / 0x55d / 0x55e) with a count of 0x80
    // into a stack buffer of 0x80 BYTES (0x40 wchars), a retail overrun for a format of 64
    // characters or more. The port's buffer holds the 0x80 wchars the count allows.
    constexpr int kArrivalFormatChars = 0x80;
}

NnArrivalText::NnArrivalText()
{
    // Inlined in FUN_0045b530 (0x0045b569).
    G_NnArrivalText = this;
    bWaitForLetterbox = 0;
}

NnArrivalText::~NnArrivalText()
{
    // FUN_0045abb0.
    G_NnArrivalText = nullptr;
}

bool NnArrivalText::Create(Win3D* NewParent, void* Data)
{
    // FUN_0045abd0 (RET 8).
    (void)NewParent;

    // (-0.45f, 0.24f, 0), 0.95f x 0.06f, 1.4f.
    Vector const CaptionPosition = {NnFloatBits(0xbee66666), NnFloatBits(0x3e75c28f), 0.0f};
    Caption = static_cast<NnTextControl*>(CreateTextControl(
        "ArrivalText", "PageHeader", &CaptionPosition, nullptr, NnFloatBits(0x3f733333),
        NnFloatBits(0x3d75c28f), NnFloatBits(0x3fb33333), 0, 0, 0, 1));

    // The colour override; no null test on the control.
    Caption->SetProperty(kWinPropColour, reinterpret_cast<uintptr_t>(&G_NnPalettePaleGreen), 1);
    Caption->Flags = static_cast<unsigned char>(Caption->Flags & 0xfc);

    SetOffset(0.0f, 0.0f, -1.0f);
    SetPosition(&Position);

    HoldTime = 2.0f;
    Delay = 4.0f;

    // FUN_004b5060 with the window's current parent (+0x04), not NewParent.
    return Win3D::Create(GetParent(), Data);
}

void NnArrivalText::DestroySelf()
{
    Win3DDeleteTree(this); // FUN_0043d2a0, inlined in retail
}

void NnArrivalText::Update()
{
    // LAB_0045acc0.
    if (NnMainButtonHasActivePanel())
    {
        DestroySelf();
        return;
    }

    // Waiting for the letterbox: while it is up the caption stays hidden and nothing else runs;
    // once it is down a delay at or below zero (NaN does not) shows it.
    if (bWaitForLetterbox != 0)
    {
        if (IsLetterboxVisible())
        {
            Caption->Flags = static_cast<unsigned char>(Caption->Flags & 0xfc);
            return;
        }
        if (Delay <= 0.0f)
            Caption->Flags = static_cast<unsigned char>(Caption->Flags | 3);
    }

    // The visible bit is sampled here. A caption showing with its delay spent goes away as soon
    // as the mission-objective banner (DAT_00672400) or HUD_Comm's window is up.
    bool const bWasVisible = ((Caption->Flags >> 1) & 1) != 0;
    if (Delay <= 0.0f && bWasVisible)
    {
        if (G_MissionObjectiveWindow != nullptr || HudCommIsWindowOpen())
        {
            DestroySelf();
            return;
        }
    }

    Win3D::Update();

    if ((Caption->Flags & 2) == 0)
    {
        // Hidden: unless one of those windows is up, the delay runs down (in double, stored as
        // float) and a negative delay shows the caption.
        if (G_MissionObjectiveWindow == nullptr && !HudCommIsWindowOpen())
        {
            Delay = static_cast<float>(static_cast<double>(Delay) - GetClientFrameTimeCapped());
            if (Delay < 0.0f)
                Caption->Flags = static_cast<unsigned char>(Caption->Flags | 3);
        }
    }
    else if (Caption->bTypewriter == 0)
    {
        // Showing and fully typed (+0x362 clear): the hold time runs down the same way.
        HoldTime = static_cast<float>(static_cast<double>(HoldTime) - GetClientFrameTimeCapped());
        if (HoldTime < 0.0f)
        {
            DestroySelf();
            return;
        }
    }

    // A caption that became visible this frame starts typing (60 ms per character).
    if (!bWasVisible && (Caption->Flags & 2) != 0)
        Caption->SetProperty(kTextPropTypewriter, 1, 0x3c);
}

void NnArrivalText::ShowBaseArrival(float ArrivalTime)
{
    // FUN_0045ae90 (RET 4).
    if (!IsPlayerDockedRetail())
        return;
    bWaitForLetterbox = 0;

    int const BaseIds = GetClientCurrentBaseStridName();
    int const SystemIds = PlayerSystemStridName();
    if (BaseIds == 0 || SystemIds == 0)
        return;

    // Base and system names go into the two halves of scratch A.
    wchar_t Format[kArrivalFormatChars];
    GetIdsStringWWithRawFallback(0x55c, Format, kArrivalFormatChars);
    int const Half = G_NnTextScratchCapacity / 2;
    wchar_t* const SecondHalf = G_NnTextScratchA + Half;
    GetIdsStringWWithRawFallback(static_cast<unsigned>(BaseIds), G_NnTextScratchA, Half);
    GetIdsStringWWithRawFallback(static_cast<unsigned>(SystemIds), SecondHalf, Half);
    _swprintf(G_NnTextScratchB, Format, G_NnTextScratchA, SecondHalf);
    CharUpperW(G_NnTextScratchB);

    Caption->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
    int const Length = static_cast<int>(wcslen(G_NnTextScratchB));

    // Only a positive ArrivalTime (NaN not) sets the delay so typing plus the hold ends with it;
    // a negative result is clamped to 0.
    if (!(ArrivalTime > 0.0f))
        return;
    Delay = ArrivalTime - (static_cast<float>(Length) * kArrivalSecondsPerChar + HoldTime);
    if (Delay < 0.0f)
        Delay = 0.0f;
}

void NnArrivalText::ShowSystemArrival(bool bResetDelay)
{
    // FUN_0045aff0 (RET 4).
    bWaitForLetterbox = 1;

    int const SystemIds = PlayerSystemStridName();

    // The region caption by the strid_name's place in 0x30001..0x30018 (byte table 0x0045b1a0
    // over the jump table 0x0045b190).
    unsigned const Index = static_cast<unsigned>(SystemIds) - 0x30001u;
    unsigned RegionIds = 0;
    if (Index <= 4)
        RegionIds = 0x4e1;
    else if (Index <= 10)
        RegionIds = 0x4e2;
    else if (Index <= 16)
        RegionIds = 0x4e4;
    else if (Index <= 0x17)
        RegionIds = 0x4e3;

    if (SystemIds == 0)
        return;

    wchar_t Format[kArrivalFormatChars];
    if (RegionIds != 0)
    {
        GetIdsStringWWithRawFallback(0x55e, Format, kArrivalFormatChars);
        int const Half = G_NnTextScratchCapacity / 2;
        wchar_t* const SecondHalf = G_NnTextScratchA + Half;
        GetIdsStringWWithRawFallback(static_cast<unsigned>(SystemIds), G_NnTextScratchA, Half);
        GetIdsStringWWithRawFallback(RegionIds, SecondHalf, Half);
        _swprintf(G_NnTextScratchB, Format, G_NnTextScratchA, SecondHalf);
        CharUpperW(G_NnTextScratchB);
        Caption->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
    }
    else
    {
        GetIdsStringWWithRawFallback(0x55d, Format, kArrivalFormatChars);
        GetIdsStringWWithRawFallback(static_cast<unsigned>(SystemIds), G_NnTextScratchA,
                                     G_NnTextScratchCapacity / 2);
        _swprintf(G_NnTextScratchB, Format, G_NnTextScratchA);
        CharUpperW(G_NnTextScratchB);
        Caption->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
        Delay = 2.0f;
    }

    if (bResetDelay)
        Delay = 2.0f;
}

void NnArrivalText::ShowNamedArrival(unsigned NameIds)
{
    // FUN_0045b1c0 (RET 4).
    bWaitForLetterbox = 0;

    int const SystemIds = PlayerSystemStridName();
    if (NameIds == 0 || SystemIds == 0)
        return;

    wchar_t Format[kArrivalFormatChars];
    GetIdsStringWWithRawFallback(0x55c, Format, kArrivalFormatChars);
    int const Half = G_NnTextScratchCapacity / 2;
    wchar_t* const SecondHalf = G_NnTextScratchA + Half;
    GetIdsStringWWithRawFallback(NameIds, G_NnTextScratchA, Half);
    GetIdsStringWWithRawFallback(static_cast<unsigned>(SystemIds), SecondHalf, Half);
    _swprintf(G_NnTextScratchB, Format, G_NnTextScratchA, SecondHalf);
    CharUpperW(G_NnTextScratchB);
    Caption->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
    Delay = 2.0f;
}

bool NnArrivalText::IsCaptionVisible() const
{
    // FUN_0045b2c0.
    return ((Caption->Flags >> 1) & 1) != 0;
}

void NnArrivalTextShow()
{
    // FUN_0045b310 (cdecl).
    NnArrivalText* const Self = EnsureNnArrivalText();
    if (Self == nullptr)
        return;
    Self->ShowSystemArrival(false);
}

void NnArrivalTextShowBase(float ArrivalTime)
{
    // FUN_0045b2d0 (cdecl).
    NnArrivalText* const Self = EnsureNnArrivalText();
    if (Self == nullptr)
        return;
    Self->ShowBaseArrival(ArrivalTime);
}

void NnArrivalTextShowNamed(unsigned NameIds)
{
    // FUN_0045b340 (cdecl).
    NnArrivalText* const Self = EnsureNnArrivalText();
    if (Self == nullptr)
        return;
    Self->ShowNamedArrival(NameIds);
}

void NnArrivalTextDestroy()
{
    // FUN_0045b380 (cdecl).
    NnArrivalText* const Self = G_NnArrivalText;
    if (Self == nullptr)
        return;
    Self->DestroySelf();
}

void NnArrivalTextHurry()
{
    // FUN_0045b3c0 (cdecl). A NaN delay also takes the teardown.
    NnArrivalText* const Self = G_NnArrivalText;
    if (Self == nullptr)
        return;
    if (Self->Delay > 0.0f)
    {
        if (Self->Delay > 1.0f)
            Self->Delay = 1.0f;
        return;
    }
    Self->DestroySelf();
}

void NnArrivalTextShowRaw(float Delay, uintptr_t Arg)
{
    // FUN_0045b430 (cdecl).
    NnArrivalText* const Self = EnsureNnArrivalText();
    if (Self == nullptr)
        return;
    Self->Delay = Delay;
    Self->bWaitForLetterbox = 0;
    Self->Caption->SetProperty(kTextPropSetIds, Arg, 0);
}

void NnArrivalTextShowSystemNow()
{
    // FUN_0045b490 (cdecl).
    NnArrivalText* const Self = EnsureNnArrivalText();
    if (Self == nullptr)
        return;
    Self->ShowSystemArrival(true);
}

void NnMainButtonOnModalOpen()
{
    // FUN_0045a460.
    if (G_NnMainButton == nullptr)
        return;
    G_NnMainButton->ClosePanel();
}

void NnMainButtonSetPanelOrigin(int PanelId, Vector const* Origin)
{
    // FUN_0045a780.
    if (G_NnMainButton == nullptr)
        return;
    G_NnMainButton->SetPanelOrigin(PanelId, Origin);
}

WinControl* NnMainButtonGetPanelButton(int PanelId)
{
    // FUN_0045a7a0.
    if (G_NnMainButton == nullptr)
        return nullptr;
    return G_NnMainButton->GetPanelButton(PanelId);
}

bool NnMainButtonHasActivePanel()
{
    // FUN_0045a470.
    if (G_NnMainButton == nullptr)
        return false;
    return G_NnMainButton->HasActivePanel();
}

WinControl* G_NnMissionMessage = nullptr; // DAT_00671ff4

void NnMissionMessageDestroy()
{
    // FUN_0048a130. The pointer is not cleared here; the window's dtor (0x0048a46c) does that.
    WinControl* const Window = G_NnMissionMessage;
    if (Window == nullptr)
        return;
    G_bNnScreenUp = false;
    Win3DDeleteTree(Window);
}

unsigned char G_bNnStoryStarKeepPending = 0; // DAT_0066d35a

void NnSystemInit(bool bCreateMainButton)
{
    // FUN_0045a100 (cdecl). Nothing when NN_MainButton already exists.
    if (G_NnMainButton != nullptr)
        return;
    G_bNnStoryStarKeepPending = 0;
    KnowledgeMapCreate(); // DAT_0066d334, interface\KnowledgeMap.ini
    InfocardMapCreate();  // DAT_0066d320, interface\InfocardMap.ini
    if (bCreateMainButton)
        WinFactoryCreate("NN_MainButton", "NN_MainButton", nullptr, nullptr);
    NnVoiceInit();
}

void NnSystemShutdown()
{
    // FUN_0045a140 (cdecl).
    // The pointer is re-read between Destroy and delete.
    if (G_NnMainButton != nullptr)
    {
        G_NnMainButton->Destroy();
        if (G_NnMainButton != nullptr)
            delete G_NnMainButton;
    }
    if (G_NnTrade != nullptr)
        G_NnTrade->SetProperty(kWinMsgHide, 0, 0);
    KnowledgeMapDestroy();
    InfocardMapDestroy();
    NnArrivalTextDestroy();
    // FUN_004b9bb0: the DAT_00672328 pending list unless DAT_0066d35a, then the DAT_00672400
    // banner.
    StoryStarReset();
    NnMissionMessageDestroy();
}

void NnMainButtonOnUnclaimedMouse(unsigned Msg, unsigned WParam, unsigned LParam)
{
    // FUN_0045a270 (cdecl, 3 args).
    (void)WParam;
    (void)LParam;

    if (GetClientMouseCursorDrawFlag() == 0)
        return;
    if (G_bNnScreenUp)
        return;

    // FUN_004c4810 inlined. In space the message goes to FUN_004c80f0 (0x0045a424).
    if ((GetPlayerBaseIdRetail() & 0x3fffffffu) == 0)
    {
        OnMouseFlightRawMessage(Msg);
        return;
    }

    if (G_NnMainButton == nullptr)
        return;
    if (NavBarHasServiceWindow())
        return;
    if (G_NnMainButton->HasActivePanel())
        return;

    FToolTipHoverInfo Record;
    ToolTipHoverReset(&Record);
    Record.Ids = 0x45e;
    Record.IdsAlt = 0x45e;
    // In the base's "Cityscape" room (runtime +0x38) the ids are 0x3a0 instead.
    FRoomRuntime* const Runtime = static_cast<FRoomRuntime*>(GetClientCurrentLocationRuntime());
    BaseData* const Base = GetClientCurrentLocationBaseData();
    if (Runtime != nullptr && Base != nullptr &&
        GetRoomRuntimeRoomId(Runtime) == MakeLocationID(Base->get_base_id(), "Cityscape"))
    {
        Record.Ids = 0x3a0;
        Record.IdsAlt = 0x3a0;
    }

    // Eight seconds.
    NnRolloverShowHoverGlobal(&Record, NnFloatBits(0x41000000));
    NnVoiceEnqueue(G_NnVoiceClickOnCharactersId, 0);
}

unsigned char NnModalPauseGame()
{
    // FUN_004a7670.
    SetClientServerTickPause(true);
    unsigned char const bPrevious = GetClientMouseCursorDrawFlag();
    SetClientMouseCursorDrawFlag(1);
    return bPrevious;
}

void NnModalResumeGame(unsigned char bCursorDrawFlag)
{
    // FUN_004a7690.
    SetClientServerTickPause(false);
    SetClientMouseCursorDrawFlag(bCursorDrawFlag);
}
