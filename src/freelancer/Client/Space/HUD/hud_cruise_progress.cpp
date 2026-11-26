// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Space/HUD/hud_cruise_progress.h"

#include "common_archetype.h"
#include "common_cequip.h"
#include "common_cobject.h"
#include "common_iface.h"
#include "Core/game_options.h"
#include "Common/Fonts/ids_strings.h"
#include "Common/input_manager.h"
#include "NeuroNet/nn_text_control.h"
#include "NeuroNet/nn_ui_pulse.h"
#include "Space/player_behavior.h"
#include "Space/space_letterbox.h"

#include <math.h>
#include <stdint.h>
#include <wchar.h>

static_assert(sizeof(HUD_CruiseProgress) == 0x42c, "retail HUD_CruiseProgress is 0x42c bytes");

HUD_CruiseProgress::HUD_CruiseProgress(INI_Reader& /*Reader*/)
    : CruiseProgressText(nullptr)
    , bCharging(0)
{
    // FUN_004d5bd0. Create(0, 0) is a direct (non-virtual) call; bCharging is written twice,
    // as in retail.
    ChargingCaption[0] = 0;
    CountdownCaption[0] = 0;
    HUDItem::Name = "HUD_CruiseProgress";
    HUD_CruiseProgress::Create(nullptr, nullptr);
    Win3D::SetName("HUD_CruiseProgress");
    Flags = static_cast<unsigned char>(Flags & ~kWin3DActive);
    ClassName = "HUD_CruiseProgress";
    bCharging = 0;
}

HUD_CruiseProgress::~HUD_CruiseProgress()
{
    // FUN_004d5d40: only the base destructors.
}

bool HUD_CruiseProgress::Create(Win3D* NewParent, void* Data)
{
    // FUN_004d5db0 (this = the WinControl face).
    Vector const TextPos = {-0.243f, -0.263f, 0.0f}; // 0xbe78d4fe, 0xbe86a7f0, 0
    CruiseProgressText = CreateTextControl("CruiseProgressText", "SubHeader", &TextPos, nullptr,
                                           0.485f, 0.04f, 0.9f, 0, 1, 0, 1);

    // The label's Font::String goes multi-line (and its layout is dropped) unless it already
    // is. The factory's result is not tested, as in retail.
    FFontString* const Text = static_cast<NnTextControl*>(CruiseProgressText)->GetFontString();
    if (Text->bMultiLine != 1)
    {
        Text->bMultiLine = 1;   // +0x30
        Text->LayoutGen = 0;    // +0x28
    }

    SetOffset(0.0f, 0.0f, -1.0f);

    GetIdsStringWWithRawFallback(0x585, ChargingCaption, 0x30);
    GetIdsStringWWithRawFallback(0x64f, CountdownCaption, 0x30);

    return Win3D::Create(NewParent, Data);
}

void HUD_CruiseProgress::Update()
{
    // FUN_004d6100 (this = the WinControl face).
    if (GetClientMouseCursorDrawFlag() != 0 && GetPlayerObjRW() != nullptr)
    {
        SetHiddenWithChildren(false);
        Flags = static_cast<unsigned char>(Flags | kWin3DActive);
    }
    else
    {
        SetHiddenWithChildren(true);
    }
    Win3D::Update();
}

void HUD_CruiseProgress::UpdateInactive()
{
    // FUN_004d6140 (this = the WinControl face).
    if (GetClientMouseCursorDrawFlag() != 0 && GetPlayerObjRW() != nullptr)
    {
        SetHiddenWithChildren(false);
    }
    Win3D::UpdateInactive();
}

void HUD_CruiseProgress::Draw()
{
    // LAB_004d5eb0 (this = the WinControl face).
    if (GetGameOption(kGameOptInterfaceGroup) == 0)     // FUN_005792b0(6)
    {
        return;
    }
    if (GetGameOption(kGameOptHud) == 0)                // FUN_005792b0(0xa)
    {
        return;
    }
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW()); // FUN_0054baf0
    if (Player == nullptr)
    {
        return;
    }
    if (GetClientMouseCursorDrawFlag() == 0)            // FUN_0041dd90
    {
        return;
    }
    if (IsLetterboxVisible())                           // FUN_0041a3e0
    {
        return;
    }
    CObject* const Object = const_cast<CObject*>(Player->cobject()); // slot +0x150
    if (Object == nullptr)
    {
        return;
    }
    if ((static_cast<unsigned>(Object->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
    {
        return;
    }
    CEqObj* const Ship = static_cast<CEqObj*>(Object);
    CEEngine* const Engine = CEEngine::cast(Ship->EquipManager.FindFirst(EquipTypeEngine)); // +0xe4
    if (Engine == nullptr)
    {
        return;
    }

    // No cruise -- the latch drops, nothing is drawn.
    if (Engine->bCruiseActive == 0)
    {
        bCharging = 0;
        return;
    }

    // Leaves only on an exactly-zero charge time (a NaN one goes on).
    float const ChargeTime = Engine->EngineArch()->CruiseChargeTime;   // Engine +0x80
    if (ChargeTime == 0.0f)
    {
        return;
    }
    float const Elapsed = Engine->CruiseChargeTimeElapsed;              // CEEngine +0x50
    // x87-precision divide, stored as a float.
    float const Ratio =
        static_cast<float>(static_cast<double>(Elapsed) / static_cast<double>(ChargeTime));

    NnTextControl* const Text = static_cast<NnTextControl*>(CruiseProgressText);
    wchar_t Line[0x3c];

    if (Ratio < 0.0f)
    {
        // The countdown before the charge starts.
        bCharging = 0;
        Text->SetCachedDraw(0, 0);                                      // FUN_0058ad70
        Text->SetProperty(kWinPropColour, reinterpret_cast<uintptr_t>(&G_NnPaletteRed), 1);
        int const Seconds = static_cast<int>(ceil(-static_cast<double>(Elapsed))); // ftol
        // Retail's format is u"%s - %d%" (0x005d7fdc). VC6 msvcrt's _woutput leaves its format
        // loop at the terminator right after the lone trailing '%' and prints nothing for it;
        // the current CRT rejects an unterminated conversion through the invalid-parameter
        // handler instead, so the same output is produced here without that '%'.
        swprintf(Line, 0x3c, L"%s - %d", CountdownCaption, Seconds);
        Text->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(Line), 0);
        Text->Draw();
        return;
    }

    // 1.0 or more (or NaN) draws nothing.
    if (Ratio < 1.0f)
    {
        // The charge percentage.
        Text->SetCachedDraw(1, static_cast<unsigned char>(bCharging == 0));
        bCharging = 1;
        Text->SetProperty(kWinPropColour, reinterpret_cast<uintptr_t>(&G_NnPaletteCyan), 1);
        // 100.0f (0x005ca230), 0.5 (double 0x005c75e8), ftol.
        int const Percent = static_cast<int>(static_cast<double>(Ratio) * 100.0 + 0.5);
        swprintf(Line, 0x3c, L"%s - %d%%", ChargingCaption, Percent);   // u"%s - %d%%" 0x005d7fc8
        Text->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(Line), 0);
        Text->Draw();
    }
}

namespace
{
    // DAT_00674998; see the header for the factory's retail functions.
    HUDFACTORY<HUD_CruiseProgress> G_HudCruiseProgressFactory("CruiseProgress");
}
