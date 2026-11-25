// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Space/HUD/hud_status_core.h"

#include "Interface/Pages/character_select_equip.h"
#include "Space/client_gameobj.h"
#include "Space/client_objdb.h"
#include "common_archetype.h"
#include "common_cequip.h"
#include "common_csys.h"
#include "common_misc.h"
#include "common_cobject.h"
#include "common_id.h"
#include "common_iface.h"
#include "dacom.h"
#include "DALib.h"
#include "Core/game_options.h"
#include "Core/globals.h"
#include "Core/widescreen.h"   // port-only (FREELANCER_WIDESCREEN_SUPPORT)
#include "Space/HUD/hud_command_dispatch.h"
#include "Space/HUD/hud_targetable_objects.h"
#include "Space/HUD/hud_weapon_groups.h"
#include "Common/Fonts/ids_strings.h"
#include "Network/login_mgr.h"
#include "Interface/menu_keymap_config.h"
#include "Interface/menu_ui_init.h"
#include "NeuroNet/nn_inventory.h"
#include "NeuroNet/nn_tooltip.h"
#include "Common/input_manager.h"
#include "NeuroNet/nn_modal.h"
#include "NeuroNet/nn_text_control.h"     // NnTextControl::SetNumberText (FUN_0058a580)
#include "NeuroNet/nn_text_scratch.h"     // G_NnTextScratchA/B (DAT_0066dc60 / DAT_0066fc60), DAT_006119f8
#include "NeuroNet/nn_ui_pulse.h"
#include "Space/player_behavior.h"
#include "Space/player_spawn.h"
#include "render/IRenderPipeline.h"
#include "Common/Graphics/screen_quad_state.h"
#include "Interface/ship_view_control.h"
#include "Space/Cameras/space_camera.h"
#include "Space/space_letterbox.h"
#include "Interface/ui_scroll.h"
#include "Interface/win3d.h"
#include "x86math.h"

#include <math.h>
#include <cstdio>
#include <cstring>
#include <new>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// ---------------------------------------------------------------------------------------------
// Shield sampler
// ---------------------------------------------------------------------------------------------

namespace
{
    // DAT_00674950 (static initializer LAB_004d4b30); FUN_004d4b50 is its only reader.
    unsigned const G_ShieldlessShipId = CreateID("Ku07_02");

    // DAT_00674a9c -- HUD_Status.cpp's 3DMathEngine (FUN_005b73e0 on first use).
    I3DMathEngine* G_HudStatusMath = nullptr;
}

void HudSampleShieldHitPoints(CEqObj* Object, float* OutMax, float* OutCur, bool* OutAnyActive)
{
    // FUN_004d4b50.
    *OutMax = 0.0f;
    *OutCur = 0.0f;
    if (Object->Id == G_ShieldlessShipId)
    {
        return;
    }

    CEquipTraverser Walk(EquipTypeShield); // 0x100
    CEquip* Item = Object->EquipManager.Traverse(Walk);
    while (Item != nullptr)
    {
        CEShield* const Shield = CEShield::cast(Item);
        if (Shield != nullptr)
        {
            // Cap stays on the FPU; the floor goes through ftol and is stored back as a float.
            // The product of two floats is exact in a double.
            float const Cap = Shield->GetMaxHitPoints();
            int const FloorInt = static_cast<int>(static_cast<double>(Shield->OfflineThreshold) *
                                                  static_cast<double>(Cap));
            float const Floor = static_cast<float>(FloorInt);

            // A difference that is >= 0 or unordered is kept; anything below 0 becomes 0.
            double Spare = static_cast<double>(Cap) - static_cast<double>(Floor);
            if (Spare < 0.0)
            {
                Spare = 0.0;
            }
            *OutMax = static_cast<float>(Spare + static_cast<double>(*OutMax));

            if (Shield->IsActive() && OutAnyActive != nullptr)
            {
                *OutAnyActive = true;
            }

            if (Shield->IsFunctioning())
            {
                double Left = static_cast<double>(Shield->GetHitPoints()) -
                              static_cast<double>(Floor);
                if (Left < 0.0)
                {
                    Left = 0.0;
                }
                *OutCur = static_cast<float>(Left + static_cast<double>(*OutCur));
            }
        }
        Item = Object->EquipManager.Traverse(Walk);
    }
}

// ---------------------------------------------------------------------------------------------
// Panel singletons
// ---------------------------------------------------------------------------------------------

namespace
{
    // DAT_00674aa0.
    HUD_Status* G_HudStatus = nullptr;
}

HudTargetBase* G_HudTargetPanel = nullptr;

HUD_Status* GetHudStatus()
{
    return G_HudStatus;
}

bool HudStatusIsTurretView()
{
    // No null test, as retail.
    return G_HudStatus->bTurretView != 0;
}

bool HudStatusIsRearView()
{
    return G_HudStatus->bRearView != 0;
}

// ---------------------------------------------------------------------------------------------
// HUD_Core
// ---------------------------------------------------------------------------------------------

namespace
{
    // FUN_004d5410 constants (.rdata).
    double const kHalfThicknessPerLine = 0.005208333333333333; // 005d7e68 (1/192)
    double const kSegmentWidthA = 0.08544921875;               // 005d7e60
    double const kSegmentWidthB = 0.125;                        // 005d7e58
    double const kFirstChipScale = 0.8;                         // 005d7e50
    double const kHalf = 0.5;                                   // 005c75e8
    double const kDesignLineToScreen = 0.0013020833333333333;   // 005d7e38 (1/768)

    // DAT_00674980 -- the screen-quad list key FUN_004d5410 flushes; its static initializer
    // LAB_004d4af0 sets it to -1 and nothing else writes it. -1 equals FUN_00403ea0's "current
    // list" key DAT_0061648c, so the flush takes the container of the current material key, the
    // one the chips were just queued on.
    unsigned long G_HudCoreListKey = 0xffffffffu;
}

HUD_Core::HUD_Core(INI_Reader& /*Reader*/)
    : HealthBar(nullptr)
    , ShieldBar(nullptr)
    , PowerBar(nullptr)
    , VelocityText(nullptr)
    , ThrustText(nullptr)
    , LastVelocity(0)
    , LastThrust(0)
    , Background2(nullptr)
{
    // FUN_004d4db0. Create(0, 0) is a direct (non-virtual) call. Retail does not write the six
    // child pointers in the ctor; Create fills them before anything reads them.
    HUDItem::Name = "Core";
    HUD_Core::Create(nullptr, nullptr);
    Win3D::SetName("HUD_Core");
    ClassName = "HUD_Core";
    LastVelocity = -999;
    LastThrust = -1;
}

HUD_Core::~HUD_Core()
{
    // FUN_004d4f20: only the base destructors.
}

bool HUD_Core::Create(Win3D* NewParent, void* Data)
{
    // FUN_004d4fa0 (this = the WinControl face).
    SetMesh("Interface\\hud\\HUD_guagewindow.cmp", 0);

    float const Zero[3] = {0.0f, 0.0f, 0.0f};

    float const BackgroundPos[3] = {0.005f, -0.365f, -1.0f};
    Background2 = CreateStaticControl("Background2", "Interface\\hud\\HUD_numberboxes.cmp",
                                      BackgroundPos, nullptr, Zero, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0,
                                      Zero);

    float const BarHalf[3] = {0.085f, 0.0055f, 0.0f};

    float const HealthPos[3] = {0.005f, -0.376f, -1.0f};
    HealthBar = CreateStaticControl("HealthBar", "Interface\\hud\\HUD_hullgauge.3db", HealthPos,
                                    nullptr, BarHalf, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, Zero);
    HealthBar->SetProperty(kWinPropToolTip, 0x470, 0);

    float const ShieldPos[3] = {0.005f, -0.366f, -1.0f};
    ShieldBar = CreateStaticControl("ShieldBar", "Interface\\hud\\HUD_shieldgauge.3db", ShieldPos,
                                    nullptr, BarHalf, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, Zero);
    ShieldBar->SetProperty(kWinPropToolTip, 0x471, 0);

    float const PowerPos[3] = {0.005f, -0.355f, -1.0f};
    PowerBar = CreateStaticControl("PowerBar", "Interface\\hud\\HUD_energygauge.3db", PowerPos,
                                   nullptr, BarHalf, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, Zero);
    PowerBar->SetProperty(kWinPropToolTip, 0x3f2, 0);

    float const VelocityPos[3] = {0.108f, 0.008f, 0.0f};
    VelocityText = CreateTextControl("VelocityText", "Normal", VelocityPos, nullptr, 0.056f, 0.03f,
                                     1.0f, 0, 1, 0, 1);

    float const ThrustPos[3] = {-0.16f, 0.008f, 0.0f};
    ThrustText = CreateTextControl("ThrustText", "Normal", ThrustPos, nullptr, 0.056f, 0.03f, 1.0f,
                                   0, 1, 0, 1);

    HalfExtents[0] = 0.1f;
    HalfExtents[1] = 0.02f;
    HalfExtents[2] = 0.0f;
    HitOffset[0] = 0.005f;
    HitOffset[1] = 0.0f;
    HitOffset[2] = 0.0f;

    RegisterEvent(USER_MINIMIZE_HUD);

    SetOffset(0.005f, -0.365f, -1.0f);
    SetPosition(&Position.x);
    return Win3D::Create(NewParent, Data);
}

bool HUD_Core::OnEvent(unsigned MsgId)
{
    // LAB_004d5370.
    if (HudIsMousePickBlocked())
    {
        return false;
    }
    if (MsgId != USER_MINIMIZE_HUD)
    {
        return false;
    }

    HUD_Status* const Status = G_HudStatus;
    HudTargetBase* Target = G_HudTargetPanel;
    unsigned char const bStatusOpen = Status->bExpanded;
    if (bStatusOpen == 0 && Target->bExpanded == 0)
    {
        // Both closed -- both open, the target panel re-read from DAT_00674b04.
        static_cast<EventHandler*>(Status)->OnEvent(0x6d);
        static_cast<EventHandler*>(G_HudTargetPanel)->OnEvent(0x6d);
        NnMainButtonOnModalOpen();
        return true;
    }

    // Close whichever is open (the target flag read after the status panel has handled the
    // command, from a fresh DAT_00674b04 when the status panel was open).
    if (bStatusOpen != 0)
    {
        static_cast<EventHandler*>(Status)->OnEvent(0x6d);
        Target = G_HudTargetPanel;
    }
    if (Target->bExpanded != 0)
    {
        static_cast<EventHandler*>(Target)->OnEvent(0x6d);
    }
    NnMainButtonOnModalOpen();
    return true;
}

void HUD_Core::DrawBars(float DesignY, unsigned long Color, float Ratio)
{
    // FUN_004d5410. The surface size is read as signed.
    int const SurfaceWidth = static_cast<int>(G_RenderSurfaceWidth);
    int const SurfaceHeight = static_cast<int>(G_RenderSurfaceHeight);

    float const HalfThickness =
        static_cast<float>(static_cast<double>(SurfaceHeight) * kHalfThicknessPerLine);
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only Hor+: the bars keep their 4:3 size (height-scaled) and stay centred on the
    // full surface below.
    int const SegmentWidth = static_cast<int>(WidescreenEquivalentSurfaceWidth() * kSegmentWidthA *
                                              kSegmentWidthB); // ftol
#else
    int const SegmentWidth = static_cast<int>(static_cast<double>(SurfaceWidth) * kSegmentWidthA *
                                              kSegmentWidthB); // ftol
#endif
    int const SpanInt = SegmentWidth << 4;
    double const SegmentWidthD = static_cast<double>(SegmentWidth);
    float const FirstChip = static_cast<float>(SegmentWidthD * kFirstChipScale);
    float const Span = static_cast<float>(SpanInt);
    float const HalfSpan = static_cast<float>(static_cast<double>(SpanInt) * kHalf);

    // DAT_006101d8 is the constant {no texture, UV 0,0,1,1} descriptor (never written).
    ScreenQuadSetMaterialKey(DACOM_CRC::GetCRC32("core_bars"));
    ScreenQuadBeginBatchFrame(G_ScreenQuadDefaultFrame, 0, false);

    float Left = static_cast<float>(
        (static_cast<double>(SurfaceWidth / 2) - static_cast<double>(HalfSpan)) +
        SegmentWidthD * kHalf);
    float const Y0 = static_cast<float>(static_cast<double>(DesignY) * kDesignLineToScreen *
                                        static_cast<double>(SurfaceHeight));
    float Right = static_cast<float>(static_cast<double>(Left) + static_cast<double>(FirstChip));
    float const Y1 = static_cast<float>(static_cast<double>(HalfThickness) * 2.0 +
                                        static_cast<double>(Y0));
    float const FillEnd = static_cast<float>(
        static_cast<double>(Span) * static_cast<double>(Ratio) + static_cast<double>(Left));

    if (Right > FillEnd)
    {
        Right = FillEnd;
    }

    // One record per chip, appended inline into the node FUN_00402e30 returned (growth to
    // count + 1 + 0x20 past the capacity), with no test of its extents: {old left, Y0, right,
    // Y1, 1.0f, 1.0f, 0, 0, 1, 1, the colour dword}.
    float const Step = static_cast<float>(SegmentWidth);
    int Chip = 0;
    do
    {
        float const ChipLeft = Left;
        double const NextLeft = static_cast<double>(Step) + static_cast<double>(Left);
        Left = static_cast<float>(NextLeft);
        FScreenQuadRecord Record;
        Record.X0 = ChipLeft;
        Record.Y0 = Y0;
        Record.X1 = Right;
        Record.Y1 = Y1;
        Record.DepthA = 1.0f;
        Record.DepthB = 1.0f;
        std::memcpy(&Record.U0, &G_ScreenQuadDefaultFrame[1], sizeof(float));
        std::memcpy(&Record.V0, &G_ScreenQuadDefaultFrame[2], sizeof(float));
        std::memcpy(&Record.U1, &G_ScreenQuadDefaultFrame[3], sizeof(float));
        std::memcpy(&Record.V1, &G_ScreenQuadDefaultFrame[4], sizeof(float));
        Record.ColorBits = Color;
        ScreenQuadAppendRecord(Record);
        // Only a strictly greater (ordered) next left edge ends the row.
        if (NextLeft > static_cast<double>(FillEnd))
        {
            break;
        }
        double const NextRight = static_cast<double>(Step) + static_cast<double>(Right);
        Right = static_cast<float>(NextRight);
        if (NextRight > static_cast<double>(FillEnd))
        {
            Right = FillEnd;
        }
        ++Chip;
    } while (Chip < 0x10);

    // ZFUNC saved, forced to 8 around the flush and put back, whatever GetRenderState returned.
    // (Retail reads the saved value even when the call fails; the port starts it at 0 instead of
    // stack garbage.)
    IRenderPipeline* const Pipe = DALib::Pipeline;
    int Saved = 0;
    Pipe->GetRenderState(D3DRS_ZFUNC, &Saved);
    Pipe->SetRenderState(D3DRS_ZFUNC, 8);
    ScreenQuadFlush(Pipe, G_HudCoreListKey); // FUN_00403ea0(Pipe, DAT_00674980)
    Pipe->SetRenderState(D3DRS_ZFUNC, Saved);
}

void HUD_Core::Draw()
{
    // LAB_004d56d0 (this = the WinControl face).
    if (IsSpaceInitInProgress())
    {
        return;
    }
    if (GetGameOption(kGameOptInterfaceGroup) == 0)
    {
        return;
    }
    if (GetGameOption(kGameOptHud) == 0)
    {
        return;
    }
    if (GetClientMouseCursorDrawFlag() == 0)
    {
        return;
    }
    if (GetPlayerObjRW() == nullptr)
    {
        return;
    }
    if (IsLetterboxVisible())
    {
        return;
    }

    Background2->Draw();
    DrawStaticMesh();                                   // FUN_005a06a0

    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    if (Player == nullptr)
    {
        return;
    }
    CObject* const Object = const_cast<CObject*>(Player->cobject());
    if (Object == nullptr)
    {
        return;
    }
    if ((static_cast<unsigned>(Object->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
    {
        return;
    }
    CShip* const Ship = static_cast<CShip*>(Object);

    // Power; divides when MaxPower is above 0 or NaN.
    float PowerRatio = 0.0f;
    if (!(Ship->MaxPower <= 0.0f))
    {
        PowerRatio = static_cast<float>(static_cast<double>(Ship->Power) /
                                        static_cast<double>(Ship->MaxPower));
    }
    DrawBars(735.0f, 0xffa7a15eu, PowerRatio);

    // Shield, through the IObjInspectImpl's CBase (+0xc) object.
    CBase* const Base = static_cast<CBase*>(static_cast<IObjInspectImpl*>(Player));
    float ShieldMax = 0.0f;
    float ShieldCur = 0.0f;
    if (Base != nullptr && Base->Object != nullptr &&
        (static_cast<unsigned>(Base->Object->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj)
    {
        HudSampleShieldHitPoints(static_cast<CEqObj*>(Base->Object), &ShieldMax, &ShieldCur,
                                 nullptr);
    }
    float ShieldRatio = 0.0f;
    if (ShieldMax > 0.0f)
    {
        double const Quotient = static_cast<double>(ShieldCur) / static_cast<double>(ShieldMax);
        ShieldRatio = static_cast<float>(Quotient);
        if (Quotient > 1.0)
        {
            ShieldRatio = 1.0f;
        }
    }
    DrawBars(745.0f, 0xff3e3db5u, ShieldRatio);

    // Hull.
    if (Base == nullptr)
    {
        return;
    }
    CObject* const HullObject = Base->Object;
    if (HullObject == nullptr)
    {
        return;
    }
    if ((static_cast<unsigned>(HullObject->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj)
    {
        return;
    }
    float const Hull = static_cast<CSimple*>(HullObject)->get_relative_health();
    DrawBars(755.0f, 0xff782a33u, Hull);

    // Speed along the ship's forward axis (orientation column 2), the tradelane speed in a
    // lane, capped at 300 in cruise, then pushed away from 0 to +-1.
    float Speed = 0.0f;
    bool bSkipPositive = false;
    if (Ship->is_using_tradelane())
    {
        Speed = Ship->get_tradelane_speed();
    }
    else
    {
        float const AxisX = Ship->Orientation.E[0][2]; // +0x10
        float const AxisY = Ship->Orientation.E[1][2]; // +0x1c
        float const AxisZ = Ship->Orientation.E[2][2]; // +0x28
        Vector const Velocity = Ship->get_velocity();
        double Dot = static_cast<double>(AxisZ) * static_cast<double>(Velocity.z) +
                     static_cast<double>(AxisY) * static_cast<double>(Velocity.y);
        Dot += static_cast<double>(AxisX) * static_cast<double>(Velocity.x);
        Speed = static_cast<float>(-Dot);
        if (Ship->is_cruise_active() && Speed > 300.0f)
        {
            Speed = 300.0f;
            bSkipPositive = true;
        }
    }

    bool bRounded = false;
    if (!bSkipPositive && Speed > 1.0e-5f && Speed < 1.0f)
    {
        Speed = 1.0f;
        bRounded = true;
    }
    if (!bRounded && static_cast<double>(Speed) < -1.0e-5 && Speed > -1.0f)
    {
        Speed = -1.0f;
    }
    int const SpeedInt = static_cast<int>(Speed); // ftol

    char Text[32];
    if (LastVelocity != SpeedInt)
    {
        LastVelocity = SpeedInt;
        if (SpeedInt > 999)
        {
            VelocityText->SetProperty(kTextPropSetIds, 0x526, 0);
        }
        else
        {
            sprintf(Text, "%d", SpeedInt);
            // Text is used as a printf format by the NumberString child (FUN_004159a0).
            static_cast<NnTextControl*>(VelocityText)->SetNumberText(Text);
        }
    }

    // Thrust percentage (CShip +0x2a0 / +0x2a4).
    double ThrustRatio = 0.0;
    if (!(Ship->MaxThrustPower <= 0.0f))
    {
        ThrustRatio = (double)Ship->ThrustPower / (double)Ship->MaxThrustPower;
    }
    int const Thrust = static_cast<int>(ThrustRatio * 100.0); // ftol
    if (LastThrust != Thrust)
    {
        LastThrust = Thrust;
        if (Thrust < 25)
        {
            ThrustText->SetProperty(kWinPropColour, reinterpret_cast<uintptr_t>(&G_NnPaletteRed), 1);
        }
        else
        {
            ThrustText->SetProperty(kWinPropColour, reinterpret_cast<uintptr_t>(&G_NnPaletteCyan), 1);
        }
        sprintf(Text, "%d%%%%", Thrust);
        // "NN%%" prints as "NN%" through the NumberString's own printf.
        static_cast<NnTextControl*>(ThrustText)->SetNumberText(Text);
    }

    VelocityText->Draw();
    ThrustText->Draw();
    PostDraw();
}

// ---------------------------------------------------------------------------------------------
// HUD_Status
// ---------------------------------------------------------------------------------------------

namespace
{
    // The 0x3b command ids the ctor registers on its handler, in order.
    unsigned const kStatusCommandIds[0x3b] = {
        0x5e, 0xbc, 0xc0, 0xc1, 0xbd, 0xbe, 0x3a, 0x3c, 0x3b, 0x3e, 0x3d, 0x56, 0x57, 0xa1, 0xa2,
        0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d,
        0x9e, 0x9f, 0xa0, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
        0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x8f, 0x90, 0x0f, 0xb7, 0xb8, 0xb9, 0xba, 0x6f,
    };

    // .rdata 0x005d86b8: the wide placeholder both count texts start with (raw bytes
    // 31 00 32 00 33 00 00 00; Ghidra shows it as a one-byte DAT). Property 0x1d reads a
    // wchar_t*.
    wchar_t const kStatusCountPlaceholder[] = L"123";
}

FHudStatusCaption::FHudStatusCaption()
{
    Text = nullptr;
    Capacity = 0;
}

FHudStatusCaption::~FHudStatusCaption()
{
    // Both words are zeroed only when there was text to free.
    if (Text != nullptr)
    {
        free(Text);
        Text = nullptr;
        Capacity = 0;
    }
}

HUD_Status::HUD_Status(INI_Reader& /*Reader*/)
    : bPendingWeaponGroupInit(1)
    , ActiveWeaponGroup(0)
    , bThrustersOn(0)
    , bHasThruster(0)
    , bCruiseActive(0)
    , RefreshTimer(1.0f)
    , bShowWeaponList(1)
    , bLastShowWeaponList(1)
    , bUsingTradeLane(0)
    , bTurretView(0)
    , bRearView(0)
    , WeaponTopRow(0)
    , bPendingExpandedSync(1)
    , bDockingOrTradeLaneCaption(0)
{
    // FUN_004da170. HomePosition, bShowWeaponList / bLastShowWeaponList and the HUDItem name are
    // written before the Create call; the rest after it (Create reads none of them).
    G_HudStatus = this;

    HomePosition.x = 0.295f;   // +0x4d4
    HomePosition.y = -0.367f;
    HomePosition.z = 0.0f;

    HUDItem::Name = "Test_Status";
    HUD_Status::Create(nullptr, nullptr);   // direct (non-virtual) call
    Win3D::SetName("HUD_Status");
    ClassName = "HUD_Status";

    HitCenter.x = 0.418f;      // +0x4b4
    HitCenter.y = -0.29f;
    HitCenter.z = -1.0f;
    HitHalfWidth = 0.11f;      // +0x4c0
    HitHalfHeight = 0.11f;     // +0x4c4

    for (unsigned Index = 0; Index < 0x3b; ++Index)
    {
        RegisterEvent(kStatusCommandIds[Index]);
    }
}

HUD_Status::~HUD_Status()
{
    // FUN_004da740: then the member and base destructors, in retail's order.
    G_HudStatus = nullptr;
}

void HUD_Status::HideExpandedControls(bool bHide)
{
    // LAB_004db120.
    uintptr_t const Hide = static_cast<uintptr_t>(bHide ? 1u : 0u);
    if (bShowWeaponList != 0)
    {
        for (int Index = 0; Index < 10; ++Index)
        {
            WeaponText[Index]->SetProperty(kWinPropHide, Hide, 0);
            WeaponNumberText[Index]->SetProperty(kWinPropHide, Hide, 0);
            AmmoText[Index]->SetProperty(kWinPropHide, Hide, 0);
        }
        WeaponListScroll->SetProperty(kWinPropHide, Hide, 0);
    }
    ViewPanel->SetProperty(kWinPropHide, Hide, 0);
    CloseButton->SetProperty(kWinPropHide, Hide, 0);
}

bool HUD_Status::Create(Win3D* NewParent, void* Data)
{
    // FUN_004da930 (this = the WinControl face). No result is tested for null.
    float const Zero[3] = {0.0f, 0.0f, 0.0f};

    float const BackgroundPos[3] = {0.3387f, -0.243f, -0.91f};
    ViewPanel = CreateStaticControl("StatusBackground", "Interface\\hud\\HUD_shipinfo.cmp",
                                    BackgroundPos, nullptr, Zero, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0,
                                    Zero);

    float const MinimizedPos[3] = {0.4101f, -0.37f, -1.0f};
    MinimizedFrame = CreateStaticControl("StatusMinimizedFrame",
                                         "Interface\\hud\\HUD_shipinfo_min.cmp", MinimizedPos,
                                         nullptr, Zero, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, Zero);

    HalfExtents[0] = 0.105f;
    HalfExtents[1] = 0.02f;
    HalfExtents[2] = 0.0f;
    HitOffset[0] = 0.4f;
    HitOffset[1] = -0.371f;
    HitOffset[2] = 0.0f;

    // The open button sits at HomePosition.
    float const OpenHalf[3] = {0.02f, 0.02f, 0.0f};
    ToggleButton = CreateButtonControl("HudStatusOpen", "interface\\hud\\hud_ShipInfo_button.3db",
                                       nullptr, "ui_window_open", nullptr, &HomePosition.x,
                                       nullptr, OpenHalf, 0, 1, 0, 0, 0x3ff, 0, 0);
    ToggleButton->SetProperty(0x36, 1, 0);

    // Ten rows. Step = float(Index * 0.018f) (DAT_005d3ee8). The number column is -0.199f
    // (DAT_005d8768) - Step; the name and ammo columns are -0.199 (the double DAT_005d8750)
    // - Step, rounded once to float and reused. The double subtraction rounds to the same float
    // as the x87 extended one for all ten rows.
    // 0.016999999f is 0x3c8b4395 (the name columns); the ammo column's 0.017f is 0x3c8b4396.
    char ControlName[64];
    for (int Index = 0; Index < 10; ++Index)
    {
        sprintf(ControlName, "WeaponNumberText%d", Index);
        float const Step = static_cast<float>(Index) * 0.018f;
        float const NumberPos[3] = {0.27f, -0.199f - Step, 0.0f};
        WeaponNumberText[Index] = CreateTextControl(ControlName, "HudSmall", NumberPos, nullptr,
                                                    0.02f, 0.016999999f, 1.0f,
                                                    static_cast<unsigned>(0x515 + Index), 1, 0, 1);
        WeaponNumberText[Index]->SetProperty(kTextPropClickable, 1, 0);

        sprintf(ControlName, "WeaponText%d", Index);
        float const RowY = static_cast<float>(-0.199 - static_cast<double>(Step));
        float const TextPos[3] = {0.288f, RowY, 0.0f};
        WeaponText[Index] = CreateTextControl(ControlName, "HudSmall", TextPos, nullptr, 0.165f,
                                              0.016999999f, 1.0f, 0, 0, 0, 1);
        WeaponText[Index]->SetProperty(kTextPropClickable, 1, 0);

        sprintf(ControlName, "AmmoText%d", Index);
        float const AmmoPos[3] = {0.458f, RowY, 0.0f};
        AmmoText[Index] = CreateTextControl(ControlName, "HudSmall", AmmoPos, nullptr, 0.04f, 0.017f,
                                            1.0f, 0x514, 0, 0, 1);
        AmmoText[Index]->SetProperty(kTextPropClickable, 1, 0);

        WeaponNumberText[Index]->SetProperty(kWinPropHide, 1, 0);
        WeaponText[Index]->SetProperty(kWinPropHide, 1, 0);
        AmmoText[Index]->SetProperty(kWinPropHide, 1, 0);
    }

    float const SmallHalf[3] = {0.014f, 0.014f, 0.0f};

    float const ClosePos[3] = {0.383f, -0.307f, -0.5f};
    CloseButton = CreateButtonControl("StatusCloseButton", "interface\\NeuroNet\\Inventory\\x.3db",
                                      nullptr, "ui_select_item", nullptr, ClosePos, nullptr,
                                      SmallHalf, 0, 1, 0, 0, 0x409, 0, 0);

    float const RepairPos[3] = {0.341f, -0.371f, 0.0f};
    RepairButton = CreateButtonControl("HudRepairButton", "interface\\hud\\HUD_repairactivate.3db",
                                       nullptr, "ui_item_select", nullptr, RepairPos, nullptr,
                                       SmallHalf, 0, 1, 0, 0, 0x43e, 0, 0x8f);
    float const RepairCountPos[3] = {0.347f, -0.359f, 0.0f};
    RepairCountText = CreateTextControl("RepairCountText", "HudSmall", RepairCountPos, nullptr,
                                        0.034f, 0.03f, 0.8f, 0, 1, 0x4b2, 1);
    RepairCountText->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(kStatusCountPlaceholder),
                                 0x10);

    float const BatteryPos[3] = {0.397f, -0.371f, 0.0f};
    UseBatteryButton = CreateButtonControl("UseBatteryButton",
                                           "interface\\hud\\HUD_shieldgaugeicon.3db", nullptr,
                                           "ui_item_select", nullptr, BatteryPos, nullptr,
                                           SmallHalf, 0, 1, 0, 0, 0x4b5, 0, 0x90);
    float const BatteryCountPos[3] = {0.404f, -0.359f, 0.0f};
    BatteryCountText = CreateTextControl("BatteryCountText", "HudSmall", BatteryCountPos, nullptr,
                                         0.034f, 0.03f, 0.8f, 0, 1, 0x4b3, 1);
    BatteryCountText->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(kStatusCountPlaceholder),
                                  0x10);

    // The scroll bar, then HUDItem slot 13 (virtual) with 1.
    float const ScrollPos[3] = {0.49f, -0.234f, -1.0f};
    WeaponListScroll = CreateUIScroll("WeaponListScroll", ScrollPos, 0.016f, 0.118f, 0, 5, 10, 0);
    HideExpandedControls(true);

    // The special-mode captions, both hidden.
    float const SpecialPos[3] = {-0.513f, -0.29f, 0.0f};
    SpecialModeText = CreateTextControl("SpecialModeText", "PageHeader", SpecialPos, nullptr,
                                        1.022f, 0.03f, 1.5f, 0, 1, 0, 1);
    float const CancelPos[3] = {-0.513f, -0.321f, 0.0f};
    SpecialModeTextCancel = CreateTextControl("SpecialModeTextCancel", "Normal", CancelPos,
                                              nullptr, 1.028f, 0.03f, 1.3f, 0, 1, 0, 1);
    SpecialModeText->Flags &= 0xfc;
    SpecialModeTextCancel->Flags &= 0xfc;

    RebuildWeaponList();

    float const LootPos[3] = {0.21f, -0.37f, 0.0f};
    LootButton = CreateButtonControl("LootButton", "interface\\HUD\\HUD_loot_bw.3db", nullptr,
                                     "ui_select_item", nullptr, LootPos, nullptr, SmallHalf, 0, 1,
                                     0, 0, 0x3a2, 0, 0x6f);
    LootButton->Flags &= 0xfc;

    SetOffset(0.0f, 0.0f, -1.0f);
    SetPosition(&Position.x);
    return Win3D::Create(NewParent, Data);
}

int HUD_Status::FindHardpointRow(char const* Hardpoint)
{
    // FUN_004dd730. The cache first, matched case-sensitively. strlen runs on every entry, so a
    // null name with a non-empty cache faults here, as in retail.
    for (std::vector<FHudStatusHpRow>::const_iterator It = HpRowCache.begin();
         It != HpRowCache.end(); ++It)
    {
        if (It->Hardpoint.compare(0, It->Hardpoint.size(), Hardpoint, std::strlen(Hardpoint)) == 0)
        {
            return It->Row;
        }
    }

    // No name, no selected ship archetype (FUN_004c3e10) or no Ship record -> 0, nothing
    // cached.
    if (Hardpoint == nullptr)
    {
        return 0;
    }
    unsigned const ShipId = GetPlayerShipArchetypeIdRetail();
    if (ShipId == 0)
    {
        return 0;
    }
    Archetype::Ship const* const ShipArch = Archetype::GetShip(ShipId);
    if (ShipArch == nullptr)
    {
        return 0;
    }

    // Every hp type whose hardpoint list names Hardpoint (DACOM stricmp), once per matching
    // name.
    std::vector<int> Types;
    int const GroupCount = static_cast<int>(ShipArch->LegalHps.size());
    for (int Group = 0; Group < GroupCount; ++Group)
    {
        Archetype::LegalHpTypes const& Legal = ShipArch->LegalHps[Group];
        int const NameCount = static_cast<int>(Legal.Hardpoints.size());
        for (int NameIndex = 0; NameIndex < NameCount; ++NameIndex)
        {
            if (_stricmp(Hardpoint, Legal.Hardpoints[NameIndex].Value) == 0)
            {
                Types.push_back(static_cast<int>(Legal.Type));
            }
        }
    }
    if (Types.empty())
    {
        return 0; // nothing cached either
    }

    // The row of the caption table (FUN_00584710 rows, FUN_00584720 type) holding the largest
    // matched type; signed compares, the first such row wins.
    int BestRow = 0;
    int BestType = 0;
    int const RowCount = ShipViewHpTypeTableCount();
    for (int Row = 0; Row < RowCount; ++Row)
    {
        int const Type = ShipViewHpTypeTableType(Row);
        for (int Index = 0; Index < static_cast<int>(Types.size()); ++Index)
        {
            if (Type == Types[Index] && Type > BestType)
            {
                BestType = Type;
                BestRow = Row;
            }
        }
    }

    // Cache {Hardpoint, BestRow} at the end (FUN_004df080).
    FHudStatusHpRow Entry;
    Entry.Hardpoint.assign(Hardpoint, std::strlen(Hardpoint));
    Entry.Row = BestRow;
    HpRowCache.insert(HpRowCache.end(), 1, Entry);
    return BestRow;
}

void HUD_Status::RebuildWeaponList()
{
    // FUN_004dd9e0.
    Weapons.erase(Weapons.begin(), Weapons.end());

    // The player ship's launchers (traverser mask 0xe0), gathered in a local vector. The walk
    // stops at the first item CELauncher::cast rejects.
    std::vector<FHudStatusWeapon> Found;
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    if (Player != nullptr)
    {
        CObject* const Object = const_cast<CObject*>(Player->cobject());
        if (Object != nullptr &&
            (static_cast<unsigned>(Object->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj)
        {
            CEqObj* const Owner = static_cast<CEqObj*>(Object);
            CEquipTraverser Walk(EquipTypeMineDropper | EquipTypeCmDropper | EquipTypeGun);
            CELauncher* Launcher = CELauncher::cast(Owner->EquipManager.Traverse(Walk));
            while (Launcher != nullptr)
            {
                Archetype::Equipment* const Arch = Launcher->Arch; // +0x0c, read first
                EquipDesc Desc;
                // The hardpoint when GetEquipDesc succeeds, else null.
                char const* HardpointName = nullptr;
                if (Launcher->GetEquipDesc(Desc))
                {
                    HardpointName = Desc.Hardpoint;
                }
                FHudStatusWeapon Row;
                Row.SortKey = FindHardpointRow(HardpointName);
                Row.SubId = Launcher->Id;
                Row.bActive = Launcher->IsActive() ? 1 : 0;
                Row.IdsName = Arch->IdsName;
                Row.AmmoCount = Launcher->GetAmmoCount();
                // strlen on the name, no null test (as retail).
                Row.Hardpoint.assign(HardpointName, std::strlen(HardpointName));
                Found.insert(Found.end(), Row); // FUN_004de8b0
                Launcher = CELauncher::cast(Owner->EquipManager.Traverse(Walk));
            }
        }
    }

    // An index vector 0..Count-1.
    int const Count = static_cast<int>(Found.size());
    std::vector<int> Order;
    for (int Index = 0; Index < Count; ++Index)
    {
        Order.insert(Order.end(), Index);
    }

    // Bubble sort of the indices, swapping a pair when the first has the smaller SortKey or, on
    // equal keys, the smaller name (DACOM stricmp < 0) -- so the indices end up in descending
    // order.
    int const Last = Count - 1;
    bool bSorted = true;
    do
    {
        bSorted = true;
        if (Last <= 0)
        {
            break;
        }
        for (int Index = 0; Index < Last; ++Index)
        {
            FHudStatusWeapon const& First = Found[Order[Index]];
            FHudStatusWeapon const& Second = Found[Order[Index + 1]];
            bool bSwap = false;
            if (First.SortKey < Second.SortKey)
            {
                bSwap = true;
            }
            else if (First.SortKey == Second.SortKey &&
                     _stricmp(First.Hardpoint.c_str(), Second.Hardpoint.c_str()) < 0)
            {
                bSwap = true;
            }
            if (bSwap)
            {
                int const Swap = Order[Index];
                Order[Index] = Order[Index + 1];
                Order[Index + 1] = Swap;
                bSorted = false;
            }
        }
    } while (!bSorted);

    // Copied into Weapons from the last index down, each inserted at the end, the name re-read
    // through c_str().
    for (int Index = Last; Index >= 0; --Index)
    {
        FHudStatusWeapon const& Source = Found[Order[Index]];
        FHudStatusWeapon Copy;
        Copy.SubId = Source.SubId;
        Copy.AmmoCount = Source.AmmoCount;
        Copy.IdsName = Source.IdsName;
        Copy.bActive = Source.bActive;
        Copy.Hardpoint = Source.Hardpoint.c_str();
        Copy.SortKey = Source.SortKey;
        Weapons.insert(Weapons.end(), 1, Copy);
    }
}

void HUD_Status::RefreshWeaponRows()
{
    // FUN_004de0a0.
    RebuildWeaponList();

    // The top row is clamped (unsigned compare) to Count - 10, or to 0 while the list fits.
    int const Count = static_cast<int>(Weapons.size());
    int MaxTop = 0;
    if (Count > 10)
    {
        MaxTop = Count - 10;
    }
    if (static_cast<unsigned>(WeaponTopRow) > static_cast<unsigned>(MaxTop))
    {
        WeaponTopRow = MaxTop;
    }

    // The cursor ray -- the cursor pixel onto the near plane of the Win3D camera DAT_0067dbf8,
    // through a copy of its orientation DAT_0067dbfc, then normalised.
    FSpaceCamera* const Cam = Win3DGetCamera();
    int CursorX = 0;
    int CursorY = 0;
    GetClientSoftwareCursor(&CursorX, &CursorY, nullptr);
    Vector Local;
    Local.x = static_cast<float>((static_cast<double>(CursorX) - Cam->ScreenCenterX) /
                                 Cam->XScale * Cam->ZNear);
    Local.y = static_cast<float>((static_cast<double>(CursorY) - Cam->ScreenCenterY) /
                                 Cam->YScale * Cam->ZNear);
    Local.z = -Cam->ZNear;
    Matrix Orientation;
    memcpy(&Orientation, &Cam->World, sizeof(Orientation));
    if (G_HudStatusMath == nullptr)
    {
        G_HudStatusMath = Acquire3DMathEngine();
    }
    Vector World;
    G_HudStatusMath->TransformMatrix(World, Orientation, Local);
    double const Inv = 1.0 / sqrt(static_cast<double>(World.x) * World.x +
                                  static_cast<double>(World.y) * World.y +
                                  static_cast<double>(World.z) * World.z);
    float Ray[3];
    Ray[0] = static_cast<float>(World.x * Inv);
    Ray[1] = static_cast<float>(World.y * Inv);
    Ray[2] = static_cast<float>(World.z * Inv);

    // Up to ten rows from WeaponTopRow (signed compares against Count). The active launcher's
    // row is DAT_00679bac, the rest DAT_00679bb0; a hovered row is pulsed unless the mouse pick
    // is blocked.
    int Row = WeaponTopRow;
    if (Row >= Count)
    {
        return;
    }
    for (int Slot = 0;;)
    {
        unsigned long Color = G_NnPaletteCyan;
        if (Weapons[Row].bActive != 0)
        {
            Color = G_NnPaletteGreen;
        }
        if (WeaponNumberText[Slot]->HitTest(Ray) || WeaponText[Slot]->HitTest(Ray) ||
            AmmoText[Slot]->HitTest(Ray))
        {
            if (!HudIsMousePickBlocked())
            {
                unsigned long Pulsed = 0;
                NnUiPulseBlendFromArgb(&Pulsed, Color);
                Color = Pulsed;
            }
        }
        WeaponNumberText[Slot]->SetProperty(kWinPropColour, reinterpret_cast<uintptr_t>(&Color), 1);
        WeaponText[Slot]->SetProperty(kWinPropColour, reinterpret_cast<uintptr_t>(&Color), 1);
        AmmoText[Slot]->SetProperty(kWinPropColour, reinterpret_cast<uintptr_t>(&Color), 1);

        ++Slot;
        if (Slot >= 10)
        {
            break;
        }
        ++Row;
        if (Row >= Count)
        {
            break;
        }
    }
}

void HUD_Status::Draw()
{
    // FUN_004db2c0 (this = the WinControl face).
    if (IsSpaceInitInProgress())
    {
        return;
    }
    if (IsLetterboxVisible())
    {
        return;
    }

    // While either animation runs, the loot button draws itself when it exists and its Flags
    // bit 1 is set -- the only null test here is retail's.
    if ((ButtonMover::IsActive() || ButtonScaler::IsActive()) && LootButton != nullptr &&
        (LootButton->Flags & 0x02) != 0)
    {
        LootButton->Draw();
    }

    // While the scaler runs only the background draws.
    if (ButtonScaler::IsActive())
    {
        ViewPanel->Draw();
        return;
    }

    // HUD_Target_Base's Draw is FUN_004e7890.
    RefreshWeaponRows();
    HudTargetBase::Draw();
}

void HUD_Status::SetTarget(IObjRW* NewTarget)
{
    // LAB_004db0d0: HUDItem::SetTarget non-virtually, SetViewObject virtually, then the weapon
    // groups (DAT_00674c0c) get the view watcher's object.
    HUDItem::SetTarget(NewTarget);
    SetViewObject(NewTarget);
    G_WeaponGroups.SetShip(ObjRwFromWatchable(const_cast<Watchable*>(ViewObject.target())));
}

void HUD_Status::ToggleExpanded()
{
    // LAB_004dccf0 (this = the HUDItem face). Nothing while either animation runs.
    if (ButtonMover::IsActive())
    {
        return;
    }
    if (ButtonScaler::IsActive())
    {
        return;
    }

    if (bExpanded == 0)
    {
        // Opening, stage 1 -- the minimized controls slide down to -0.4 over 0.3 s
        // (DAT_005d8488); OnMoveFinished continues.
        ButtonMover::Start(0.3f);
        float const ToggleEnd[3] = {0.371f, -0.27f, 0.0f};
        ButtonMover::AddButton(ToggleButton, &HomePosition.x, ToggleEnd);
        float const FrameStart[3] = {0.4101f, -0.37f, -1.0f};
        float const FrameEnd[3] = {0.4101f, -0.4f, -1.0f};
        ButtonMover::AddButton(MinimizedFrame, FrameStart, FrameEnd);
        float const RepairStart[3] = {0.341f, -0.371f, 0.0f};
        float const RepairEnd[3] = {0.341f, -0.4f, 0.0f};
        ButtonMover::AddButton(RepairButton, RepairStart, RepairEnd);
        float const RepairCountStart[3] = {0.347f, -0.359f, 0.0f};
        float const RepairCountEnd[3] = {0.347f, -0.4f, 0.0f};
        ButtonMover::AddButton(RepairCountText, RepairCountStart, RepairCountEnd);
        float const BatteryStart[3] = {0.397f, -0.371f, 0.0f};
        float const BatteryEnd[3] = {0.397f, -0.4f, 0.0f};
        ButtonMover::AddButton(UseBatteryButton, BatteryStart, BatteryEnd);
        float const BatteryCountStart[3] = {0.404f, -0.359f, 0.0f};
        float const BatteryCountEnd[3] = {0.404f, -0.4f, 0.0f};
        ButtonMover::AddButton(BatteryCountText, BatteryCountStart, BatteryCountEnd);
        RefreshTimer = 1.0f; // DAT_005d8484
        return;
    }

    // Closing, stage 1 -- the background shrinks 1.0 -> 0.1 over 0.3 s (OnScaleFinished
    // continues), and the weapon rows and the scroll bar are hidden.
    HideExpandedControls(true);
    ButtonScaler::Start(0.3f);
    ButtonScaler::AddButton(ViewPanel, 1.0f, 0.1f);
    for (int Index = 0; Index < 10; ++Index)
    {
        WeaponNumberText[Index]->SetProperty(kWinPropHide, 1, 0);
        WeaponText[Index]->SetProperty(kWinPropHide, 1, 0);
        AmmoText[Index]->SetProperty(kWinPropHide, 1, 0);
    }
    WeaponListScroll->SetProperty(kWinPropHide, 1, 0);
}

void HUD_Status::OnMoveFinished()
{
    // LAB_004dcf80 (this = the ButtonMover face at +0x364).
    RefreshTimer = 1.0f;

    if (bExpanded == 0)
    {
        // Opening, stage 2 -- the toggle button and the minimized frame are hidden, and the
        // background grows 0.1 -> 1.0 over 0.3 s.
        HideExpandedControls(true);
        ToggleButton->Flags &= 0xfc;
        MinimizedFrame->Flags &= 0xfc;
        ButtonScaler::Start(0.3f);
        ButtonScaler::AddButton(ViewPanel, 0.1f, 1.0f);
        ButtonScaler::Update();
        return;
    }

    // Closing, done -- collapsed, session flag 2 cleared, and the minimized hit box back.
    bExpanded = 0;
    SetGameSessionFlags(2, false);
    HalfExtents[0] = 0.105f;
    HalfExtents[1] = 0.02f;
    HalfExtents[2] = 0.0f;
    HitOffset[0] = 0.4f;
    HitOffset[1] = -0.371f;
    HitOffset[2] = 0.0f;
}

void HUD_Status::OnScaleFinished()
{
    // LAB_004dd080 (this = the ButtonScaler face at +0x4e0).
    if (bExpanded == 0)
    {
        // Opening, done -- expanded, session flag 2 set, the repair / battery controls placed
        // on the panel, the weapon rows and the scroll bar shown when bShowWeaponList is set,
        // the minimized frame hidden, and the expanded hit box.
        bExpanded = 1;
        SetGameSessionFlags(2, true);
        HideExpandedControls(false);
        float const RepairPos[3] = {0.3f, -0.171f, 0.0f};
        RepairButton->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(RepairPos), 0);
        float const RepairCountPos[3] = {0.306f, -0.16f, 0.0f};
        RepairCountText->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(RepairCountPos), 0);
        float const BatteryPos[3] = {0.357f, -0.171f, 0.0f};
        UseBatteryButton->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(BatteryPos), 0);
        float const BatteryCountPos[3] = {0.365f, -0.16f, 0.0f};
        BatteryCountText->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(BatteryCountPos), 0);
        if (bShowWeaponList != 0)
        {
            for (int Index = 0; Index < 10; ++Index)
            {
                WeaponNumberText[Index]->SetProperty(kWinPropHide, 0, 0);
                WeaponText[Index]->SetProperty(kWinPropHide, 0, 0);
                AmmoText[Index]->SetProperty(kWinPropHide, 0, 0);
            }
            WeaponListScroll->SetProperty(kWinPropHide, 0, 0);
        }
        MinimizedFrame->SetProperty(kWinPropHide, 1, 0);
        HalfExtents[0] = 0.131f;
        HalfExtents[1] = 0.117f;
        HalfExtents[2] = 0.0f;
        HitOffset[0] = 0.376f;
        HitOffset[1] = -0.267f;
        HitOffset[2] = 0.0f;
        return;
    }

    // Closing, stage 2 -- the minimized frame shown, the repair / battery controls put at -0.4
    // and, with the frame and the toggle button, slid back up over 0.3 s; OnMoveFinished
    // finishes.
    MinimizedFrame->SetProperty(kWinPropHide, 0, 0);
    ButtonMover::Start(0.3f);
    float const ToggleStart[3] = {0.371f, -0.27f, 0.0f};
    ButtonMover::AddButton(ToggleButton, ToggleStart, &HomePosition.x);
    float const RepairLow[3] = {0.341f, -0.4f, 0.0f};
    RepairButton->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(RepairLow), 0);
    float const RepairCountLow[3] = {0.347f, -0.4f, 0.0f};
    RepairCountText->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(RepairCountLow), 0);
    float const BatteryLow[3] = {0.397f, -0.4f, 0.0f};
    UseBatteryButton->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(BatteryLow), 0);
    float const BatteryCountLow[3] = {0.404f, -0.4f, 0.0f};
    BatteryCountText->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(BatteryCountLow), 0);
    float const RepairHome[3] = {0.341f, -0.371f, 0.0f};
    ButtonMover::AddButton(RepairButton, RepairLow, RepairHome);
    float const RepairCountHome[3] = {0.347f, -0.359f, 0.0f};
    ButtonMover::AddButton(RepairCountText, RepairCountLow, RepairCountHome);
    float const BatteryHome[3] = {0.397f, -0.371f, 0.0f};
    ButtonMover::AddButton(UseBatteryButton, BatteryLow, BatteryHome);
    float const BatteryCountHome[3] = {0.404f, -0.359f, 0.0f};
    ButtonMover::AddButton(BatteryCountText, BatteryCountLow, BatteryCountHome);
    float const FrameLow[3] = {0.4101f, -0.4f, -1.0f};
    float const FrameHome[3] = {0.4101f, -0.37f, -1.0f};
    ButtonMover::AddButton(MinimizedFrame, FrameLow, FrameHome);
    RefreshTimer = 1.0f;
}

void HUD_Status::ToggleLauncher(unsigned short SubId)
{
    // FUN_004db930 (this = the HUDItem face). Only a launcher of the target.
    CEquip* const Equip = FindTargetEquip(SubId); // FUN_004e7e20
    if (Equip == nullptr)
    {
        return;
    }
    if (CELauncher::cast(Equip) == nullptr)
    {
        return;
    }

    // UI sound slot 0x17, then the toggle record (id 0x19) through FUN_0054bcb0.
    PlayOrRewindUiSoundSlot(0x17);
    FSpaceCommand Command;
    memset(&Command, 0, sizeof(Command));
    Command.Byte26 = 1;
    Command.Id = USER_ACTIVATE_ITEM;
    Command.SubObjectId = 2;
    Command.ObjectId = SubId;
    SpaceSendCommand(&Command);

    // The ship of the HUDItem target, used without a null test.
    CShip* const Ship = HudShipOfObjRw(ObjRwFromWatchable(const_cast<Watchable*>(Target.target())));

    // The ship's target object when it is a CSimple.
    CSimple* TargetObject = nullptr;
    if (Ship->get_target() != nullptr)
    {
        CObject const* const Object = Ship->get_target()->cobject();
        if (Object != nullptr && (static_cast<unsigned>(Object->ObjectClass) & CObject::ClassSimple) == CObject::ClassSimple)
        {
            TargetObject = static_cast<CSimple*>(const_cast<CObject*>(Object));
        }
    }

    // An active gun is aimed there.
    CEGun* const Gun = CEGun::cast(Equip);
    if (Equip->IsActive() && Gun != nullptr)
    {
        Gun->SetTarget(TargetObject, Ship->get_sub_target());
    }

    G_WeaponGroups.StoreActive(G_WeaponGroups.GetCurrent());
}

bool HUD_Status::MouseEvent(FWin3DMouseEvent const* Event)
{
    // LAB_004db8f0 (this = the WinControl face). Always consumed.
    if (Event->Button == kWin3DMouseLeft)
    {
        NnToolTipHideGlobal();
        unsigned short const SubId = SubTargetId;
        if (SubId != SUBOBJ_ID_NONE && Event->Action == kWin3DMousePress)
        {
            ToggleLauncher(SubId);
        }
    }
    return true;
}

intptr_t HUD_Status::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // FUN_004db1c0 (this = the WinControl face).
    if (Msg == kWinNotifyClick)
    {
        // The close button (not forwarded to the base).
        if (A == reinterpret_cast<uintptr_t>(CloseButton))
        {
            ToggleExpanded();
            return 1;
        }

        // A weapon row's text (name, number, ammo -- in that order); a row inside Weapons
        // (unsigned compare, the top row not added) toggles the launcher at WeaponTopRow + row
        // and restarts RefreshTimer.
        int Row = 0;
        for (; Row < 10; ++Row)
        {
            if (A == reinterpret_cast<uintptr_t>(WeaponText[Row]) ||
                A == reinterpret_cast<uintptr_t>(WeaponNumberText[Row]) ||
                A == reinterpret_cast<uintptr_t>(AmmoText[Row]))
            {
                break;
            }
        }
        if (Row < 10 && static_cast<unsigned>(Row) < Weapons.size())
        {
            ToggleLauncher(Weapons[WeaponTopRow + Row].SubId);
            RefreshTimer = 1.0f;
        }
    }
    else if (Msg == kWinNotifyScrollValue)
    {
        if (!HudIsMousePickBlocked() && A == reinterpret_cast<uintptr_t>(WeaponListScroll))
        {
            WeaponTopRow = static_cast<int>(B);
            RefreshTimer = 1.0f;
        }
    }

    return HudTargetBase::SetProperty(Msg, A, B); // FUN_004e7950
}

namespace
{
    // DAT_00674aa5 / DAT_00674aa6 (.bss, 0 at load) -- the last IsMouseFlightUnlocked /
    // IsMouseFlightHoldThresholdReached Update saw. Only Update reads or writes them.
    unsigned char G_StatusLastMouseFlightUnlocked = 0;
    unsigned char G_StatusLastMouseFlightHoldReached = 0;

    // Retail's shared scratch buffers (nn_text_scratch.h): the special-mode caption's format
    // is DAT_0066dc60, its line and the weapon rows' name scratch DAT_0066fc60. Each capacity
    // passed is DAT_006119f8.
    wchar_t* const G_StatusScratchFormat = G_NnTextScratchA; // DAT_0066dc60
    wchar_t* const G_StatusScratchLine = G_NnTextScratchB;   // DAT_0066fc60
}

void HUD_Status::Update()
{
    // LAB_004db340 (this = the WinControl face).

    // Once the view object is live, sync the panel with session flag 2.
    if (bPendingExpandedSync != 0 && ObjRwFromWatchable(const_cast<Watchable*>(ViewObject.target())) != nullptr)
    {
        bPendingExpandedSync = 0;
        unsigned char const bFlag = TestGameSessionFlags(2) ? 1 : 0;
        if (bFlag != bExpanded)
        {
            ToggleExpanded();
        }
    }

    // The captions follow the mouse-flight state.
    if ((IsMouseFlightUnlocked() ? 1 : 0) != G_StatusLastMouseFlightUnlocked) // FUN_004c7270
    {
        UpdateSpecialModeCaptions();
        G_StatusLastMouseFlightUnlocked = IsMouseFlightUnlocked() ? 1 : 0;
    }
    if ((IsMouseFlightHoldThresholdReached() ? 1 : 0) != G_StatusLastMouseFlightHoldReached) // FUN_004c7c60
    {
        UpdateSpecialModeCaptions();
        G_StatusLastMouseFlightHoldReached = IsMouseFlightHoldThresholdReached() ? 1 : 0;
    }

    // The player ship and its behaviour manager.
    CShip* const Ship = HudShipOfObjRw(static_cast<IObjRW*>(GetPlayerObjRW()));
    if (Ship != nullptr)
    {
        IBehaviorManager* const Behavior = Ship->get_behavior_interface();
        if (Behavior != nullptr)
        {
            if (Behavior->bAllowUserSteering == 0 && !Ship->is_using_tradelane() &&
                Behavior->DockingPortStatus != 0)
            {
                // Ids 0x63b shown, the cancel caption hidden; with a target whose TypeBits has
                // bit 7, the cancel caption gets the target's caption.
                SpecialModeText->SetProperty(kTextPropSetIds, 0x63b, 0);
                SpecialModeText->Flags |= 3;
                SpecialModeTextCancel->Flags &= 0xfc;
                bDockingOrTradeLaneCaption = 1;
                IObjRW* const ShipTarget = Ship->get_target();
                if (ShipTarget != nullptr)
                {
                    CObject* Object = const_cast<CObject*>(ShipTarget->cobject());
                    if (Object != nullptr &&
                        (static_cast<unsigned>(Object->ObjectClass) & CObject::ClassSimple) != CObject::ClassSimple)
                    {
                        Object = nullptr;
                    }
                    // TypeBits is read with no null test (retail faults on a target without a
                    // CSimple).
                    CSimple* const Simple = static_cast<CSimple*>(Object);
                    if ((Simple->TypeBits & OBJ_TRADELANE_RING) != 0)
                    {
                        if (HudBuildObjectCaption(Simple, &Caption, kCaptionLocation, false))
                        {
                            SpecialModeTextCancel->SetProperty(
                                0x1d, reinterpret_cast<uintptr_t>(Caption.Text), 0);
                            SpecialModeTextCancel->Flags |= 3;
                        }
                    }
                }
            }
            else if (bDockingOrTradeLaneCaption != 0 || Ship->is_using_tradelane())
            {
                if (Ship->is_using_tradelane())
                {
                    SpecialModeTextCancel->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(Caption.Text), 0);
                    SpecialModeTextCancel->Flags |= 3;
                    bDockingOrTradeLaneCaption = 1;
                }
                else
                {
                    bDockingOrTradeLaneCaption = 0;
                    SpecialModeText->Flags &= 0xfc;
                    SpecialModeTextCancel->Flags &= 0xfc;
                    UpdateSpecialModeCaptions();
                }
            }
        }
    }

    // Nothing more while either animation runs.
    HudTargetBase::Update();   // FUN_004e7980
    ButtonScaler::Update();
    if (!ButtonMover::IsActive() && !ButtonScaler::IsActive())
    {
        // bShowWeaponList changed -- the rows and the scroll bar follow it.
        if (bShowWeaponList != bLastShowWeaponList)
        {
            bLastShowWeaponList = bShowWeaponList;
            RefreshTimer = 1.0f;
            if (bShowWeaponList == 0)
            {
                for (int Index = 0; Index < 10; ++Index)
                {
                    WeaponNumberText[Index]->SetProperty(kWinPropHide, 1, 0);
                    WeaponText[Index]->SetProperty(kWinPropHide, 1, 0);
                    AmmoText[Index]->SetProperty(kWinPropHide, 1, 0);
                }
                WeaponListScroll->SetProperty(kWinPropHide, 1, 0);
                RefreshTimer = 1.0f;
            }
            else
            {
                for (int Index = 0; Index < 10; ++Index)
                {
                    WeaponNumberText[Index]->SetProperty(kWinPropHide, 0, 0);
                    WeaponText[Index]->SetProperty(kWinPropHide, 0, 0);
                    AmmoText[Index]->SetProperty(kWinPropHide, 0, 0);
                }
                WeaponListScroll->SetProperty(kWinPropHide, 0, 0);
            }
        }

        // The first update with a live view object selects weapon group 0 -- stored from the
        // active launchers when it does not exist yet.
        if (bPendingWeaponGroupInit != 0 && ObjRwFromWatchable(const_cast<Watchable*>(ViewObject.target())) != nullptr)
        {
            bPendingWeaponGroupInit = 0;
            if (G_WeaponGroups.FindGroupIndex(0) == -1)
            {
                G_WeaponGroups.StoreActive(0);
                G_WeaponGroups.SetCurrent(0);
            }
            else
            {
                G_WeaponGroups.Activate(0);
            }
        }

        // With a live HUDItem target, the rest runs once RefreshTimer plus the frame time (kept
        // on the FPU) passes 1.0; <= 1.0 and NaN skip. The sum is stored before the test and
        // 1.0 taken off after it.
        if (ObjRwFromWatchable(const_cast<Watchable*>(Target.target())) != nullptr)
        {
            double const Sum = GetClientFrameTimeCapped() + static_cast<double>(RefreshTimer);
            RefreshTimer = static_cast<float>(Sum);
            if (Sum > 1.0)
            {
                RefreshTimer = static_cast<float>(Sum - 1.0);
                if (!ButtonMover::IsActive() && !ButtonScaler::IsActive())
                {
                    bThrustersOn = 0;
                    bHasThruster = 0;
                    bUsingTradeLane = 0;
                    bCruiseActive = 0;

                    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
                    if (Player != nullptr)
                    {
                        // The first mine dropper and the first countermeasure dropper are asked
                        // for their ammo count; both answers are dropped (as retail).
                        CObject* const Object = const_cast<CObject*>(Player->cobject());
                        if (Object != nullptr &&
                            (static_cast<unsigned>(Object->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj)
                        {
                            CEqObj* const Owner = static_cast<CEqObj*>(Object);
                            CEquipTraverser MineWalk(EquipTypeMineDropper);
                            CEquip* const Mine = Owner->EquipManager.Traverse(MineWalk);
                            if (Mine != nullptr)
                            {
                                CELauncher* const Launcher = CELauncher::cast(Mine);
                                if (Launcher != nullptr)
                                {
                                    Launcher->GetAmmoCount();
                                }
                            }
                            CEquipTraverser CmWalk(EquipTypeCmDropper);
                            CEquip* const Cm = Owner->EquipManager.Traverse(CmWalk);
                            if (Cm != nullptr)
                            {
                                CELauncher* const Launcher = CELauncher::cast(Cm);
                                if (Launcher != nullptr)
                                {
                                    Launcher->GetAmmoCount();
                                }
                            }
                        }

                        // The ship-state bytes. When the chain gives no ship retail goes on with
                        // a null ship and faults; the port does the same.
                        CShip* const PlayerShip = HudShipOfObjRw(static_cast<IObjRW*>(GetPlayerObjRW()));
                        if (PlayerShip != nullptr)
                        {
                            bThrustersOn = PlayerShip->any_thrusters_on() ? 1 : 0;
                        }
                        if (PlayerShip->is_using_tradelane())
                        {
                            bUsingTradeLane = 1;
                        }
                        if (PlayerShip->is_cruise_active())
                        {
                            bCruiseActive = 1;
                        }
                        if (PlayerShip->EquipManager.FindFirst(EquipTypeThruster) != nullptr)
                        {
                            bHasThruster = 1;
                        }
                    }

                    UpdateRepairBatteryCounts();
                    if (IsLootInReach())
                    {
                        LootButton->Flags |= 3;
                    }
                    else
                    {
                        LootButton->Flags &= 0xfc;
                    }
                    if (bExpanded != 0)
                    {
                        ShowWeaponRows();
                    }
                }
            }
        }
    }

    // Also reached while an animation runs: the toggle button is hidden while the panel is open
    // and the mover is idle.
    uintptr_t const bHideToggle = (bExpanded != 0 && !ButtonMover::IsActive()) ? 1u : 0u;
    ToggleButton->SetProperty(kWinPropHide, bHideToggle, 0);
}

void HUD_Status::UpdateSpecialModeCaptions()
{
    // FUN_004dca20 (this = the HUDItem face).
    // The player ship: in a tradelane, and whether the current behaviour is 10.
    bool bTradelane = false;
    bool bHideText = false;
    CShip* const Ship = HudShipOfObjRw(static_cast<IObjRW*>(GetPlayerObjRW()));
    if (Ship != nullptr)
    {
        if (Ship->is_using_tradelane())
        {
            bTradelane = true;
        }
        IBehaviorManager* const Behavior = Ship->get_behavior_interface();
        if (Behavior != nullptr && Behavior->CurrentBehaviorIndex == 10)
        {
            bHideText = true;
        }
    }

    // Outside the turret view, with mouse flight unlocked (or behaviour 10) and no tradelane,
    // the cancel caption is hidden; the text is hidden as well unless the rear view is up or
    // the mouse-flight hold has fired without behaviour 10. Otherwise both show.
    bool bHideCancel = false;
    if (bTurretView == 0 && (IsMouseFlightUnlocked() || bHideText) && !bTradelane)
    {
        bHideCancel = true;
        if (bRearView != 0)
        {
            bHideText = false;
        }
        else if (IsMouseFlightHoldThresholdReached() && !bHideText)
        {
            bHideText = false;
        }
        else
        {
            bHideText = true;
        }
    }
    else
    {
        bHideCancel = false;
        bHideText = false;
    }

    SpecialModeText->SetProperty(kWinPropHide, bHideText ? 1u : 0u, 0);
    SpecialModeTextCancel->SetProperty(kWinPropHide, bHideCancel ? 1u : 0u, 0);
    if (bHideText)
    {
        return;
    }

    if (bTurretView != 0)
    {
        SpecialModeText->SetProperty(kTextPropSetIds, IsMouseFlightUnlocked() ? 0x615u : 0x633u, 0);
        if (bTradelane)
        {
            return;
        }
    }
    else if (bRearView != 0)
    {
        SpecialModeText->SetProperty(kTextPropSetIds, 0x62b, 0);
        if (bTradelane)
        {
            return;
        }
    }
    else if (bTradelane)
    {
        SpecialModeText->Flags &= 0xfc;
        return;
    }
    else
    {
        SpecialModeText->SetProperty(kTextPropSetIds, 0x62c, 0);
    }

    // The cancel caption -- ids 0x62d as a format around the name of the key bound to the
    // command that leaves the mode ("???" when none is bound): 0xbf in the turret view, 0xf with
    // mouse flight unlocked, 0xc8 otherwise.
    GetIdsStringWWithRawFallback(0x62d, G_StatusScratchFormat,
                                 G_NnTextScratchCapacity);
    wchar_t KeyName[0x100];
    wcscpy(KeyName, L"???"); // DAT_005d0214
    unsigned Command = 0xc8;
    if (bTurretView != 0)
    {
        Command = 0xbf;
    }
    else if (IsMouseFlightUnlocked())
    {
        Command = 0xf;
    }
    FKeyBindingLookup Binding;
    if (KeyMapFindActiveBinding(Command, &Binding)) // FUN_00577dd0
    {
        KeyMapFormatBindingName(&Binding, KeyName, 0x100); // FUN_00577f50
    }
    swprintf(G_StatusScratchLine, 0x1000, G_StatusScratchFormat, KeyName);
    SpecialModeTextCancel->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(G_StatusScratchLine), 0);
}

void HUD_Status::UpdateRepairBatteryCounts()
{
    // FUN_004de2f0: the count of the first non-mission entry (FUN_004c45d0), 0 without one,
    // capped at 999 (signed).
    wchar_t Text[0x20];
    EquipDesc const* const Repair = FindPlayerNonMissionEquipRetail(G_NanobotArchId);
    int RepairCount = 0;
    if (Repair != nullptr)
    {
        RepairCount = Repair->Count; // +0x14
        if (RepairCount > 999)
        {
            RepairCount = 999;
        }
    }
    swprintf(Text, 0x20, L"%d", RepairCount);
    RepairCountText->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(Text), 0x10);

    EquipDesc const* const Battery = FindPlayerNonMissionEquipRetail(G_ShieldBatteryArchId);
    int BatteryCount = 0;
    if (Battery != nullptr)
    {
        BatteryCount = Battery->Count;
        if (BatteryCount > 999)
        {
            BatteryCount = 999;
        }
    }
    swprintf(Text, 0x20, L"%d", BatteryCount);
    BatteryCountText->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(Text), 0x10);
}

bool HUD_Status::IsLootInReach()
{
    // FUN_004de3a0.
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    if (Player == nullptr)
    {
        return false;
    }
    if (GetPlayerEngineCruiseActive())
    {
        return false;
    }
    CShip* const Ship = HudShipOfObjRw(Player);
    if (Ship == nullptr)
    {
        return false;
    }
    IBehaviorManager* const Behavior = Ship->get_behavior_interface();
    if (Behavior == nullptr)
    {
        return false;
    }
    if (Behavior->bAllowUserSteering == 0)
    {
        return false;
    }
    if (Ship->is_using_tradelane())
    {
        return false;
    }

    // Bucket 7 (loot).
    int Kind = 7;
    FClientGameobj* Loot = ClientObjDb_GetKindHead(&Kind);
    while (Loot != nullptr)
    {
        // The player's position first, then the loot's; the distance squared is summed x, z, y
        // on the FPU. >= 1e6 (DAT_005d848c) and NaN skip.
        Vector const& PlayerPos = Player->get_position();
        Vector const& LootPos = static_cast<IObjRW*>(Loot)->get_position();
        double const Dx = static_cast<double>(LootPos.x) - static_cast<double>(PlayerPos.x);
        double const Dy = static_cast<double>(LootPos.y) - static_cast<double>(PlayerPos.y);
        double const Dz = static_cast<double>(LootPos.z) - static_cast<double>(PlayerPos.z);
        double const DistanceSquared = Dx * Dx + Dz * Dz + Dy * Dy;
        if (DistanceSquared < 1000000.0)
        {
            // The CLoot behind it -- no null test before ContentsArch is read -- and the ship's
            // room for its contents.
            CBase* const Base = static_cast<CBase*>(static_cast<IObjInspectImpl*>(Loot));
            CObject* LootObject = nullptr;
            if (Base != nullptr && Base->Object != nullptr &&
                (static_cast<unsigned>(Base->Object->ObjectClass) & CObject::ClassLoot) == CObject::ClassLoot)
            {
                LootObject = Base->Object;
            }
            CLoot* const Container = static_cast<CLoot*>(LootObject);
            if (Ship->get_space_for_cargo_type(Container->ContentsArch) > 0)
            {
                return true;
            }
        }
        Loot = Loot->ListNext;
    }
    return false;
}

void HUD_Status::ShowWeaponRows()
{
    // FUN_004ddeb0.
    RebuildWeaponList();

    // WeaponTopRow is clamped with an unsigned compare.
    int const Count = static_cast<int>(Weapons.size());
    int MaxTop = 0;
    if (Count > 10)
    {
        MaxTop = Count - 10;
    }
    if (static_cast<unsigned>(WeaponTopRow) > static_cast<unsigned>(MaxTop))
    {
        WeaponTopRow = MaxTop;
    }

    // Fill rows 0.. from WeaponTopRow while both last.
    int Row = 0;
    int Index = WeaponTopRow;
    if (Index < Count)
    {
        for (;;)
        {
            FHudStatusWeapon const& Weapon = Weapons[Index];
            G_StatusScratchLine[0] = 0;
            GetIdsStringWWithRawFallback(static_cast<unsigned>(Weapon.IdsName), G_StatusScratchLine,
                                         G_NnTextScratchCapacity);
            WeaponText[Row]->SetProperty(kWinPropHide, 0, 0);
            WeaponNumberText[Row]->SetProperty(kWinPropHide, 0, 0);
            WeaponText[Row]->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(G_StatusScratchLine), 0);

            ++Index;
            char NumberText[0x20];
            sprintf(NumberText, "%d", Index);
            static_cast<NnTextControl*>(WeaponNumberText[Row])->SetNumberText(NumberText);

            if (Weapon.AmmoCount >= 0)
            {
                AmmoText[Row]->SetProperty(kWinPropHide, 0, 0);
                char AmmoCountText[0x20];
                sprintf(AmmoCountText, "%d", Weapon.AmmoCount);
                static_cast<NnTextControl*>(AmmoText[Row])->SetNumberText(AmmoCountText);
            }
            else
            {
                AmmoText[Row]->SetProperty(kWinPropHide, 1, 0);
            }

            ++Row;
            if (Row >= 10)
            {
                break;
            }
            if (Index >= Count)
            {
                break;
            }
        }
    }

    WeaponListScroll->SetProperty(kScrollPropSetRange, 0, static_cast<uintptr_t>(Count - 1));

    // The rows left over are hidden.
    for (; Row < 10; ++Row)
    {
        WeaponNumberText[Row]->SetProperty(kWinPropHide, 1, 0);
        AmmoText[Row]->SetProperty(kWinPropHide, 1, 0);
        WeaponText[Row]->SetProperty(kWinPropHide, 1, 0);
    }
}

namespace
{
    // DAT_00612700 (.data, 1 at load) / DAT_00674aa4 (.bss, 0) -- the auto-level and
    // auto-avoidance toggles OnEvent 0x3e / 0x3d flip. Nothing else in the PE touches them.
    unsigned char G_StatusAutoLevel = 1;
    unsigned char G_StatusAutoAvoidance = 0;

    // FUN_00544d00 (thiscall on the player's IObjRW; retail Client\space\ship.cpp, only caller
    // FUN_004de4c0) -- the ship's first tractor pulls in every object of Targets (IObjAffect
    // slot 15). False without a tractor.
    bool ShipTractorInTargets(IObjRW* Player, std::vector<IObjRW*> const& Targets)
    {
        // A null ship is not tested -- FindFirst faults on it, in retail as here.
        CShip* const Ship = HudShipOfObjRw(Player);
        CEquip* const Tractor = Ship->EquipManager.FindFirst(EquipTypeTractor);
        if (Tractor == nullptr)
        {
            return false;
        }
        unsigned short TractorId = Tractor->Id;
        static_cast<IObjAffect*>(Player)->tractor_all(&TractorId, Targets,
                                                  static_cast<int>(GetPlayerConnectionId()));
        return true;
    }
}

void HUD_Status::FireSeekerGuns(int Mode)
{
    // FUN_004dd4e0 (this = the HUDItem face). The player ship and its first gun; nothing
    // without them.
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    if (Player == nullptr)
    {
        return;
    }
    CObject* const Object = const_cast<CObject*>(Player->cobject());
    if (Object == nullptr)
    {
        return;
    }
    if ((static_cast<unsigned>(Object->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
    {
        return;
    }
    CShip* const Ship = static_cast<CShip*>(Object);

    // The ids go to the frame at +0x44 (26 slots up to the frame's end); retail writes them with
    // no bound test.
    unsigned short Ids[26];
    int Count = 0;
    CEquipTraverser Walk(EquipTypeGun);
    CEGun* Gun = CEGun::cast(Ship->EquipManager.Traverse(Walk));
    if (Gun == nullptr)
    {
        return;
    }
    while (Gun != nullptr)
    {
        // Only guns whose munition seeks count.
        if (Gun->MunitionArch()->Seeker != 0)
        {
            bool bTorpedo = false;
            bool bCruiseDisruptor = false;
            if (Gun->MunitionArch()->bCruiseDisruptor) // a second MunitionArch call, as retail
            {
                bCruiseDisruptor = true;
            }
            else
            {
                // The gun's hardpoint against every name of the ship archetype's legal hp types
                // (DACOM stricmp); a match under type 0x0a (torpedo), 0x24 or 0x23 (torpedo
                // specials) marks it.
                CacheString const HpName = Gun->GetParentHPName();
                Archetype::Ship const* const ShipArch =
                    static_cast<Archetype::Ship const*>(Ship->Archetype);
                int const TypeCount = static_cast<int>(ShipArch->LegalHps.size());
                for (int TypeIndex = 0; TypeIndex < TypeCount; ++TypeIndex)
                {
                    Archetype::LegalHpTypes const& Legal = ShipArch->LegalHps[TypeIndex];
                    int const NameCount = static_cast<int>(Legal.Hardpoints.size());
                    for (int NameIndex = 0; NameIndex < NameCount; ++NameIndex)
                    {
                        if (_stricmp(Legal.Hardpoints[NameIndex].Value, HpName.Value) == 0)
                        {
                            unsigned const Type = static_cast<unsigned>(Legal.Type);
                            if (Type == 0x0au || Type == 0x24u || Type == 0x23u)
                            {
                                bTorpedo = true;
                            }
                        }
                    }
                }
            }

            bool bTake = false;
            if (Mode == 0)
            {
                bTake = !bTorpedo && !bCruiseDisruptor;
            }
            else if (Mode == 1)
            {
                bTake = bTorpedo;
            }
            else if (Mode == 2)
            {
                bTake = bCruiseDisruptor;
            }
            if (bTake)
            {
                Ids[Count] = Gun->Id;
                ++Count;
            }
        }
        Gun = CEGun::cast(Ship->EquipManager.Traverse(Walk));
    }

    // One IObjAffect slot 13 call at the targetable-objects aim point (DAT_00674b70, no null
    // test).
    if (Count == 0)
    {
        return;
    }
    Vector Aim;
    G_HudTargetableObjects->GetAimPoint(&Aim); // FUN_004f1da0
    static_cast<IObjAffect*>(Player)->fire_weapons(Ids, static_cast<unsigned>(Count), &Aim,
                                              static_cast<int>(GetPlayerConnectionId()));
}

void HUD_Status::FireWeaponGroup(int Group)
{
    // FUN_004dcc60 (this = the HUDItem face).
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    if (Player == nullptr)
    {
        return;
    }
    Vector Aim;
    G_HudTargetableObjects->GetAimPoint(&Aim);
    if (Group == ActiveWeaponGroup)
    {
        // The active group -- a null list fires every active launcher.
        static_cast<IObjAffect*>(Player)->fire_weapons(nullptr, 0, &Aim,
                                                  static_cast<int>(GetPlayerConnectionId()));
        return;
    }
    // The group's launchers, at most 0x10.
    unsigned short Ids[0x10];
    int const Count = G_WeaponGroups.GetGroupEquipIds(Group, Ids, 0x10);
    if (Count > 0)
    {
        static_cast<IObjAffect*>(Player)->fire_weapons(Ids, static_cast<unsigned>(Count), &Aim,
                                                  static_cast<int>(GetPlayerConnectionId()));
    }
}

void HUD_Status::TractorInAllLoot()
{
    // FUN_004de4c0.
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    if (Player == nullptr)
    {
        return;
    }

    // The loot (bucket 7) within 1000 m. The offset is rounded to three floats (FUN_00423b60,
    // player position asked first); the square is summed y, z, x on the FPU against 1e6
    // (DAT_005d848c), >= and NaN skipping.
    std::vector<IObjRW*> Loot; // retail vector<IObjInspect*>
    int Kind = 7;
    FClientGameobj* Item = ClientObjDb_GetKindHead(&Kind);
    while (Item != nullptr)
    {
        Vector const& PlayerPos = Player->get_position();
        Vector const& ItemPos = static_cast<IObjRW*>(Item)->get_position();
        float const OffsetZ = ItemPos.z - PlayerPos.z;
        float const OffsetY = ItemPos.y - PlayerPos.y;
        float const OffsetX = ItemPos.x - PlayerPos.x;
        double DistanceSquared = static_cast<double>(OffsetY) * static_cast<double>(OffsetY) +
                                 static_cast<double>(OffsetZ) * static_cast<double>(OffsetZ);
        DistanceSquared += static_cast<double>(OffsetX) * static_cast<double>(OffsetX);
        if (DistanceSquared < 1000000.0)
        {
            Loot.insert(Loot.end(), static_cast<IObjRW*>(Item));
        }
        Item = Item->ListNext;
    }

    // A value per loot -- 1e30 for contents of volume 0, 1e20 for nanobots and shield
    // batteries (both ids read once, before the loop), else the good's price, else 0.
    int const Count = static_cast<int>(Loot.size());
    unsigned const NanobotArchId = G_NanobotArchId;
    unsigned const BatteryArchId = G_ShieldBatteryArchId;
    std::vector<float> Values;
    for (int Index = 0; Index < Count; ++Index)
    {
        float Value = 0.0f;
        CObject const* const Object = Loot[Index]->cobject();
        if (Object != nullptr && (static_cast<unsigned>(Object->ObjectClass) & CObject::ClassLoot) == CObject::ClassLoot)
        {
            Archetype::Equipment const* const Contents =
                static_cast<CLoot const*>(Object)->ContentsArch;
            unsigned const ContentsId = Contents->get_id();
            if (Contents->Volume == 0.0f)                         // NaN is not equal
            {
                Value = 1.0e30f;
            }
            else if (ContentsId == NanobotArchId || ContentsId == BatteryArchId)
            {
                Value = 1.0e20f;
            }
            else
            {
                GoodInfo const* const Good = GoodList::find_by_id(Arch2Good(ContentsId));
                if (Good != nullptr)
                {
                    Value = Good->Price;
                }
            }
        }
        Values.insert(Values.end(), 1, Value);
    }

    // Bubble sort, highest value first, the loot swapped alongside. A pair swaps only when the
    // first is strictly below the second.
    int const Last = Count - 1;
    bool bSorted = true;
    do
    {
        bSorted = true;
        if (Last <= 0)
        {
            break;
        }
        for (int Index = 0; Index < Last; ++Index)
        {
            if (Values[Index] < Values[Index + 1])
            {
                float const Higher = Values[Index + 1];
                Values[Index + 1] = Values[Index];
                Values[Index] = Higher;
                IObjRW* const Swapped = Loot[Index];
                Loot[Index] = Loot[Index + 1];
                Loot[Index + 1] = Swapped;
                bSorted = false;
            }
        }
    } while (!bSorted);

    // The ship's tractor takes the list when the player is a ship gameobj.
    FClientGameobj* const PlayerObject =
        static_cast<FClientGameobj*>(static_cast<IObjInspectImpl*>(Player));
    if (PlayerObject->ManagerKind == EClientGameobjKind::Ship)
    {
        ShipTractorInTargets(Player, Loot);
    }
}

bool HUD_Status::OnEvent(unsigned MsgId)
{
    // LAB_004dba50 (this = the Event::Handler face at +0x384).

    // Nothing without a server; while the HUD may not pick only the afterburner key's release
    // is acted on.
    if (G_Server == nullptr) // DAT_0067ecd0
    {
        return false;
    }
    if (HudIsMousePickBlocked())
    {
        if (MsgId == USER_AFTERBURN && HudCommandRecord()->bKeyDown == 0)
        {
            SetPlayerAfterburner(false);
        }
        return false;
    }

    // The player must be a ship gameobj whose object is a ship.
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    if (Player == nullptr)
    {
        return false;
    }
    FClientGameobj* const PlayerObject =
        static_cast<FClientGameobj*>(static_cast<IObjInspectImpl*>(Player));
    if (PlayerObject->ManagerKind != EClientGameobjKind::Ship)
    {
        return false;
    }
    CShip* const Ship = HudShipOfObjRw(Player);
    if (Ship == nullptr)
    {
        return false;
    }

    // Ids outside the switch answer false; every case answers true.
    switch (MsgId)
    {
    case USER_AFTERBURN:
        // Afterburner key -- post AFTERBURN_ON while it is down, AFTERBURN_OFF once it is up.
        HudPostCommand(HudCommandRecord()->bKeyDown != 0 ? USER_AFTERBURN_ON : USER_AFTERBURN_OFF,
                       0);
        return true;

    case USER_AFTERBURN_OFF:
        SetPlayerAfterburner(false);
        return true;

    case USER_AFTERBURN_ON:
        SetPlayerAfterburner(true);
        return true;

    case USER_TOGGLE_AUTO_AVOIDANCE:
        G_StatusAutoAvoidance = (G_StatusAutoAvoidance == 0) ? 1 : 0;
        SetPlayerAutoAvoidance(G_StatusAutoAvoidance != 0);
        return true;

    case USER_TOGGLE_AUTO_LEVEL:
        G_StatusAutoLevel = (G_StatusAutoLevel == 0) ? 1 : 0;
        SetPlayerAutoLevel(G_StatusAutoLevel != 0);
        return true;

    case USER_SWITCH_TO_PLAYER_SHIP:
    case USER_SWITCH_TO_WEAPON_LIST:
        // Open the panel when it is closed, then hide (0x56) or show (0x57) the weapon list --
        // bShowWeaponList and session flag 4.
        if (bExpanded == 0)
        {
            ToggleExpanded();
        }
        bShowWeaponList = (MsgId == USER_SWITCH_TO_WEAPON_LIST) ? 1 : 0;
        SetGameSessionFlags(4, MsgId == USER_SWITCH_TO_WEAPON_LIST);
        return true;

    case USER_STATUS:
    case USER_MINIMIZE_HUD:
        ToggleExpanded();
        return true;

    case USER_COLLECT_LOOT:
        TractorInAllLoot();
        return true;

    case USER_REPAIR_HEALTH:
    {
        // Nanobots -- used (UI sound 0x28) unless there are none or the hull is exactly whole
        // (NaN uses them); else UI sound 0x2a.
        EquipDesc const* const Repair = FindPlayerNonMissionEquipRetail(G_NanobotArchId);
        if (Repair != nullptr && !(Ship->get_relative_health() == 1.0f))
        {
            static_cast<IObjAffect*>(Player)->use_item(Repair->Id, static_cast<unsigned>(Repair->Count),
                                                      static_cast<int>(GetPlayerConnectionId()));
            PlayOrRewindUiSoundSlot(0x28);
            return true;
        }
        PlayOrRewindUiSoundSlot(0x2a);
        return true;
    }

    case USER_REPAIR_SHIELD:
    {
        // Shield batteries -- used (UI sound 0x29) on the first shield whose relative health is
        // below 1 and whose max hit points are above 0; else UI sound 0x2b.
        EquipDesc const* const Battery = FindPlayerNonMissionEquipRetail(G_ShieldBatteryArchId);
        if (Battery != nullptr)
        {
            CEquipTraverser Walk(EquipTypeShield);
            CEquip* Shield = Ship->EquipManager.Traverse(Walk);
            while (Shield != nullptr)
            {
                if (Shield->GetRelativeHealth() < 1.0f && Shield->GetMaxHitPoints() > 0.0f)
                {
                    static_cast<IObjAffect*>(Player)->use_item(
                        Battery->Id, static_cast<unsigned>(Battery->Count),
                        static_cast<int>(GetPlayerConnectionId()));
                    PlayOrRewindUiSoundSlot(0x29);
                    return true;
                }
                Shield = Ship->EquipManager.Traverse(Walk);
            }
        }
        PlayOrRewindUiSoundSlot(0x2b);
        return true;
    }

    case USER_WEAPON_GROUP1:
    case USER_WEAPON_GROUP2:
    case USER_WEAPON_GROUP3:
    case USER_WEAPON_GROUP4:
    case USER_WEAPON_GROUP5:
    case USER_WEAPON_GROUP6:
    {
        // Select weapon group 0..5 -- the current one is stored first.
        int const Group = static_cast<int>(MsgId - USER_WEAPON_GROUP1);
        G_WeaponGroups.StoreActive(G_WeaponGroups.GetCurrent());
        G_WeaponGroups.SetCurrent(Group);
        G_WeaponGroups.Activate(Group);
        ActiveWeaponGroup = Group;
        RefreshTimer = 1.0f;
        return true;
    }

    case USER_TOGGLE_WEAPON1:
    case USER_TOGGLE_WEAPON2:
    case USER_TOGGLE_WEAPON3:
    case USER_TOGGLE_WEAPON4:
    case USER_TOGGLE_WEAPON5:
    case USER_TOGGLE_WEAPON6:
    case USER_TOGGLE_WEAPON7:
    case USER_TOGGLE_WEAPON8:
    case USER_TOGGLE_WEAPON9:
    case USER_TOGGLE_WEAPON10:
    {
        // Toggle launcher 0..9 of Weapons (no WeaponTopRow offset) and restart RefreshTimer.
        unsigned const Row = MsgId - USER_TOGGLE_WEAPON1;
        if (Weapons.size() > Row)
        {
            ToggleLauncher(Weapons[Row].SubId);
            RefreshTimer = 1.0f;
        }
        return true;
    }

    case USER_FIRE_WEAPON1:
    case USER_FIRE_WEAPON2:
    case USER_FIRE_WEAPON3:
    case USER_FIRE_WEAPON4:
    case USER_FIRE_WEAPON5:
    case USER_FIRE_WEAPON6:
    case USER_FIRE_WEAPON7:
    case USER_FIRE_WEAPON8:
    case USER_FIRE_WEAPON9:
    case USER_FIRE_WEAPON10:
    {
        // Fire launcher 0..9 of Weapons alone at the aim point.
        unsigned const Row = MsgId - USER_FIRE_WEAPON1;
        if (Weapons.size() > Row)
        {
            IObjRW* const Shooter = static_cast<IObjRW*>(GetPlayerObjRW());
            if (Shooter == nullptr)
            {
                return true;
            }
            unsigned short SubId = Weapons[Row].SubId;
            Vector Aim;
            G_HudTargetableObjects->GetAimPoint(&Aim);
            static_cast<IObjAffect*>(Shooter)->fire_weapons(&SubId, 1, &Aim,
                                                       static_cast<int>(GetPlayerConnectionId()));
        }
        return true;
    }

    case USER_FIRE_WEAPON_GROUP1:
    {
        // Fire weapon group 0 -- FireWeaponGroup(0) inlined.
        IObjRW* const Shooter = static_cast<IObjRW*>(GetPlayerObjRW());
        if (Shooter == nullptr)
        {
            return true;
        }
        Vector Aim;
        G_HudTargetableObjects->GetAimPoint(&Aim);
        if (ActiveWeaponGroup == 0)
        {
            static_cast<IObjAffect*>(Shooter)->fire_weapons(nullptr, 0, &Aim,
                                                       static_cast<int>(GetPlayerConnectionId()));
            return true;
        }
        unsigned short Ids[0x10];
        int const Count = G_WeaponGroups.GetGroupEquipIds(0, Ids, 0x10);
        if (Count > 0)
        {
            static_cast<IObjAffect*>(Shooter)->fire_weapons(Ids, static_cast<unsigned>(Count), &Aim,
                                                       static_cast<int>(GetPlayerConnectionId()));
        }
        return true;
    }

    case USER_FIRE_WEAPON_GROUP2:
    case USER_FIRE_WEAPON_GROUP3:
    case USER_FIRE_WEAPON_GROUP4:
    case USER_FIRE_WEAPON_GROUP5:
    case USER_FIRE_WEAPON_GROUP6:
        // Fire weapon group 1..5.
        FireWeaponGroup(static_cast<int>(MsgId - USER_FIRE_WEAPON_GROUP1));
        return true;

    case USER_ASSIGN_WEAPON_GROUP1:
    case USER_ASSIGN_WEAPON_GROUP2:
    case USER_ASSIGN_WEAPON_GROUP3:
    case USER_ASSIGN_WEAPON_GROUP4:
    case USER_ASSIGN_WEAPON_GROUP5:
    case USER_ASSIGN_WEAPON_GROUP6:
        // Store the active launchers as weapon group 0..5.
        G_WeaponGroups.StoreActive(static_cast<int>(MsgId - USER_ASSIGN_WEAPON_GROUP1));
        return true;

    case USER_REMAPPABLE_LEFT:
    case USER_REMAPPABLE_RIGHT:
    case USER_REMAPPABLE_UP:
    case USER_REMAPPABLE_DOWN:
    {
        // Nothing in the rear view; else post (skipping modifiers) the camera look command in
        // the turret view, or the ship rotation command.
        static unsigned const kTurretCommands[4] = {
            USER_LOOK_ROTATE_CAMERA_LEFT, USER_LOOK_ROTATE_CAMERA_RIGHT,
            USER_LOOK_ROTATE_CAMERA_UP, USER_LOOK_ROTATE_CAMERA_DOWN};
        static unsigned const kShipCommands[4] = {USER_Y_ROTATE, USER_Y_UNROTATE, USER_X_ROTATE,
                                                  USER_X_UNROTATE};
        if (bRearView != 0)
        {
            return true;
        }
        unsigned const Slot = MsgId - USER_REMAPPABLE_LEFT;
        HudPostCommand(bTurretView != 0 ? kTurretCommands[Slot] : kShipCommands[Slot], 1);
        return true;
    }

    case USER_LAUNCH_MISSILES:
        FireSeekerGuns(0); // missiles
        return true;

    case USER_LAUNCH_TORPEDOS:
        FireSeekerGuns(1); // torpedoes
        return true;

    case USER_LAUNCH_CRUISE_DISRUPTORS:
        FireSeekerGuns(2); // cruise disruptors
        return true;

    case USER_LAUNCH_MINES:
    case USER_LAUNCH_COUNTERMEASURES:
    {
        // Drop from every mine dropper (0xbd) or every countermeasure dropper (0xbe) of the
        // player's CEqObj -- the ids into a 24-slot frame buffer (no bound test, as retail),
        // then IObjAffect slot 13 with no target position.
        IObjRW* const Dropper = static_cast<IObjRW*>(GetPlayerObjRW());
        if (Dropper == nullptr)
        {
            return true;
        }
        CObject* const Object = const_cast<CObject*>(Dropper->cobject());
        if (Object == nullptr)
        {
            return true;
        }
        if ((static_cast<unsigned>(Object->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj)
        {
            return true;
        }
        CEqObj* const Owner = static_cast<CEqObj*>(Object);
        CEquipTraverser Walk(MsgId == USER_LAUNCH_MINES ? EquipTypeMineDropper : EquipTypeCmDropper);
        CEquip* Equip = Owner->EquipManager.Traverse(Walk);
        if (Equip == nullptr)
        {
            return true;
        }
        unsigned short Ids[24];
        int Count = 0;
        while (Equip != nullptr)
        {
            Ids[Count] = Equip->Id;
            ++Count;
            Equip = Owner->EquipManager.Traverse(Walk);
        }
        if (Count == 0)
        {
            return true;
        }
        static_cast<IObjAffect*>(Dropper)->fire_weapons(Ids, static_cast<unsigned>(Count), nullptr,
                                                   static_cast<int>(GetPlayerConnectionId()));
        return true;
    }

    default:
        return false;
    }
}

// ---------------------------------------------------------------------------------------------
// Factories
// ---------------------------------------------------------------------------------------------

namespace
{
    // DAT_00674954; Create FUN_004d5af0.
    HUDFACTORY<HUD_Core> G_HudCoreFactory("Core");
    // Create FUN_004de840.
    HUDFACTORY<HUD_Status> G_HudStatusFactory("Status");
}
