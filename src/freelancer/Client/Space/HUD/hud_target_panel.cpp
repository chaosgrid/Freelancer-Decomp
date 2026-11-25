// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail Client\Space\HUD_Target.cpp -- HUD_Target (the [Target] section of hud.ini) and the
// sub-target flash bracket it draws on its 3D view. Declarations and the retail layout are in
// hud_target.h; HUD_Target_Base is in hud_target.cpp.

#include "Space/HUD/hud_target.h"

#include "Interface/Pages/character_select_equip.h" // GetPlayerStarSystemIdRetail (FUN_004c4830)
#include "NeuroNet/Chat/chat_init.h"              // ChatFindPlayerName (FUN_0046b400), ChatAcquireEntry, ...
#include "Space/client_gameobj.h"         // FClientGameobj
#include "Space/client_objdb.h"           // ClientObjDb_FindObject (FUN_005416c0)
#include "Common/Fonts/font_imp.h"               // FindMenuFontImp (FUN_004177b0)
#include "Core/game_options.h"           // GetGameOption, Test/SetGameSessionFlags
#include "Core/globals.h"                // GetClientFrameTimeCapped (FUN_0042d680)
#include "Space/HUD/hud_command_dispatch.h"   // HudPostCommand, SpaceSendCommand, FSpaceCommand
#include "Space/HUD/hud_status_core.h"        // G_HudTargetPanel (DAT_00674b04)
#include "Common/Fonts/ids_rdl.h"                // RdlAppendNode, LoadIdsIntoRdl, RdlRemoveLastParagraph
#include "Common/Fonts/ids_strings.h"            // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "Network/login_mgr.h"              // G_Server (DAT_0067ecd0)
#include "Interface/menu_ui_init.h"           // PlayOrRewindUiSoundSlot (FUN_005645c0)
#include "Interface/menu_ui_widgets.h"        // RetailU32Queue*
#include "NeuroNet/nn_group_control.h"       // NnGroupSendWireMessage (FUN_0045e360)
#include "NeuroNet/nn_help.h"                // NnHelp::ShowObjectInfo (FUN_004754a0)
#include "NeuroNet/nn_messaging.h"           // NnVoiceEnqueue (FUN_00489e80)
#include "NeuroNet/nn_text_control.h"        // NnTextControl::SetFont (FUN_0058a740)
#include "NeuroNet/nn_text_scratch.h"        // G_NnTextScratchA/B (DAT_0066dc60 / DAT_0066fc60)
#include "NeuroNet/nn_tooltip.h"             // NnToolTipHideGlobal (FUN_004be390)
#include "NeuroNet/nn_scanner.h"             // OpenNnScanner (FUN_004b6b60)
#include "NeuroNet/nn_trade.h"               // NnTradeIsOpen (DAT_006724b4 != 0)
#include "NeuroNet/nn_ui_pulse.h"            // G_HudChromeColorRgba (DAT_00679bb4)
#include "Space/player_behavior.h"        // GetPlayerObjRW (FUN_0054baf0)
#include "Space/player_spawn.h"           // GetPlayerConnectionId (DAT_00673344)
#include "Space/Cameras/space_camera.h"           // IsSpaceInitInProgress (FUN_0054bd40), AsCamera
#include "Space/space_letterbox.h"        // IsLetterboxVisible (FUN_0041a3e0)
#include "Space/star_sys.h"               // GetAttitudeClass (FUN_0045a490)
#include "Interface/win3d.h"
#include "Interface/win3d_immediate.h"
#include "Interface/win3d_render.h"           // Win3DApplyCamera (FUN_00422950)

#include "common_cequip.h"
#include "common_cobject.h"
#include "common_csys.h"            // Acquire3DMathEngine (FUN_005b73e0)
#include "common_goods.h"           // FindHardpoint_OS, HardpointInfo, EquipDescVector
#include "common_id.h"              // CreateID
#include "common_iface.h"
#include "common_misc.h"            // SUBOBJ_ID_NONE, ICamera
#include "common_server.h"          // IServer
#include "common_text.h"            // StyleNode

#include "DALib.h"
#include "engine/engbase.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// DAT_00674b00 (hud_target.h).
unsigned char G_bTradeRequested = 0;

namespace
{
    // DAT_00674ad4 -- static initializer LAB_004e0860. Only reader: OpenScanner, the voice line
    // of a cargo scan.
    unsigned const G_ScanningCargoVoiceId = CreateID("scanning_cargo");

    // DAT_00674afc -- the 3DMathEngine NextHardpointScreenPos acquires lazily (FUN_005b73e0).
    I3DMathEngine* G_HudTargetHardpointMath = nullptr;

    // Retail's shared scratch buffers (nn_text_scratch.h): the rank line (capacity read from
    // DAT_006119f8) and its " %d" suffix.
    wchar_t* const G_RankLine = G_NnTextScratchB;   // DAT_0066fc60
    wchar_t* const G_RankSuffix = G_NnTextScratchA; // DAT_0066dc60

    // .rdata constants.
    float const kOne = 1.0f;                   // DAT_005d8864
    float const kAnimSeconds = 0.3f;           // DAT_005d8860
    float const kScaleSmall = 0.1f;            // 0x3dcccccd pushed inline
    float const kTradeRange = 1500.0f;         // DAT_005d8d08
    float const kScanDefaultDistance = 999999.0f; // 0x497423f0 stored inline
    float const kFarTargetSquared = 1.0e8f;    // DAT_005d15b8
    float const kRankMax = 10000.0f;           // DAT_005ca218
    float const kSweepStep = 0.3f;             // DAT_005d3e08
    float const kSweepRate = 3.3333333f;       // DAT_005d8d44
    float const kSweepRound = 0.49999f;        // DAT_005d1910
    float const kFlashExponent = 7.0f;         // DAT_005d8850

    // The slide targets: the toggle button's expanded spot and the y every bottom-row button
    // drops to while the panel animates.
    float const kToggleOpen[3] = {-0.375f, -0.27f, 0.0f};
    float const kRowY = -0.37f;
    float const kRowLoweredY = -0.4f;

    IObjRW* WatchedObjRw(BaseWatcher const& Watcher)
    {
        return ObjRwFromWatchable(const_cast<Watchable*>(Watcher.target()));
    }

    // The inlined chain retail spells out at every use: IObjRW + 0xc (the IObjInspectImpl CBase),
    // then +4 (CBase::Object); null anywhere along it -> null.
    CObject* CObjectOfObjRw(IObjRW* Rw)
    {
        if (Rw == nullptr)
        {
            return nullptr;
        }
        IObjInspectImpl* const Impl = static_cast<IObjInspectImpl*>(Rw);
        CBase* const Base = Impl;
        if (Base == nullptr)
        {
            return nullptr;
        }
        return Base->cobject();
    }

    bool IsClass(CObject const* Object, unsigned Mask)
    {
        return (static_cast<unsigned>(Object->ObjectClass) & Mask) == Mask;
    }

    // FClientGameobj +0x44 bit 0 -- the value of the client gameobj's slot +0x158 (a one-byte
    // getter); +0xa8 -- bWatchListed, a byte FUN_004d4c70 and HUD_Target::Update read.
    bool GameobjFlag44(FClientGameobj const* Gameobj)
    {
        return (Gameobj->Flags & 1) != 0;
    }

    unsigned char GameobjByteA8(FClientGameobj const* Gameobj)
    {
        return Gameobj->bWatchListed;
    }

    void SetFlagBits(WinControl* Control)
    {
        Control->Flags |= 3;
    }

    void ClearFlagBits(WinControl* Control)
    {
        Control->Flags &= 0xfc;
    }

    void SetPos(WinControl* Control, float X, float Y, float Z)
    {
        float const Pos[3] = {X, Y, Z};
        Control->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(Pos), 0);
    }

    // Inlined by retail at three sites.
    void ClearTargetOf(IObjRW* Rw)
    {
        static_cast<IObjAffect*>(Rw)->set_target(nullptr, SUBOBJ_ID_NONE,
                                                 static_cast<int>(GetPlayerConnectionId()));
    }

    // The withdrawn trade, inlined by retail in UpdateTradeButton and OnEvent.
    void WithdrawTrade()
    {
        G_Server->StopTradeRequest(GetPlayerConnectionId());
        G_Server->TerminateTrade(GetPlayerConnectionId(), 0);
        G_bTradeRequested = 0;
    }

    // |A - B| summed in the retail order.
    float PositionDistance(Vector const& A, Vector const& B)
    {
        float const Dx = A.x - B.x;
        float const Dy = A.y - B.y;
        float const Dz = A.z - B.z;
        return sqrtf(Dz * Dz + Dy * Dy + Dx * Dx);
    }

    // FUN_004e45b0 -- the IHardpoint::EnumerateHardpoints callback of OnViewObjectChanged.
    void __cdecl CollectHardpointName(char const* Name, void* Context)
    {
        static_cast<HudTarget*>(Context)->HardpointNames.push_back(std::string(Name));
    }

    // DAT_00674aa8; see the header for the factory's retail functions.
    HUDFACTORY<HudTarget> G_HudTargetFactory("Target");
}

// =============================================================================================
// FHudSubTargetFlash
// =============================================================================================

FHudSubTargetFlash::FHudSubTargetFlash()
{
    // FUN_004dfbd0: the colour bytes are zeroed first, as retail.
    Color[0] = 0;
    Color[1] = 0;
    Color[2] = 0;
    Color[3] = 0;
    SubId = SUBOBJ_ID_NONE;
    Timer = 0.0f;
    Duration = 0.6f;         // 0x3f19999a
    BaseSize = 16.0f;        // 0x41800000
    Field10 = 32.0f;         // 0x42000000
    CornerFrac = 0.22727273f;  // 0x3e68ba2f
    TickFrac = 0.27272728f;    // 0x3e8ba2e9
    TickLenFrac = 0.36363637f; // 0x3eba2e8c
    Color[0] = 0xff;
    Color[1] = 0;
    Color[2] = 0;
    Color[3] = 0xff;
    // +0x02/+0x03 are never written (padding).
}

void FHudSubTargetFlash::Update(float DeltaSeconds, unsigned short NewSubId)
{
    // FUN_004dfc40.
    if (NewSubId != SubId)
    {
        SubId = NewSubId;
        Timer = Duration;
        if (NewSubId != SUBOBJ_ID_NONE)
        {
            PlayOrRewindUiSoundSlot(0x18);
        }
    }
    if (Timer > 0.0f)
    {
        Timer = Timer - DeltaSeconds;
        if (Timer < 0.0f)
        {
            Timer = 0.0f;
        }
    }
}

void FHudSubTargetFlash::DrawBracket(float const* ScreenPos, float HalfSize) const
{
    // FUN_004dfca0: one GL_LINES batch (mode 2) of 56 vertices at z = 0 -- four corner squares
    // of side A, four inward ticks (a vee of half-width B and depth C) at the edge midpoints.
    // Term order follows retail's; retail negates B into its own slot and subtracts it, so
    // "Px - (-B)" is written as Px + B.
    float const S = HalfSize;
    float const A = (S * CornerFrac) + (S * CornerFrac);
    float const B = S * TickFrac;
    float const C = (S * TickLenFrac) + (S * TickLenFrac);
    float const Px = ScreenPos[0];
    float const Py = ScreenPos[1];

    Win3DBegin(2);

    // Top-left corner.
    Win3DVertex3f(Px - S, Py - S, 0.0f);
    Win3DVertex3f((Px - S) + A, Py - S, 0.0f);
    Win3DVertex3f((Px - S) + A, Py - S, 0.0f);
    Win3DVertex3f((Px - S) + A, (Py - S) + A, 0.0f);
    Win3DVertex3f((Px - S) + A, (Py - S) + A, 0.0f);
    Win3DVertex3f(Px - S, (Py - S) + A, 0.0f);
    Win3DVertex3f(Px - S, (Py - S) + A, 0.0f);
    Win3DVertex3f(Px - S, Py - S, 0.0f);

    // Top tick.
    Win3DVertex3f(Px, (Py - S) + C, 0.0f);
    Win3DVertex3f(Px - B, Py - S, 0.0f);
    Win3DVertex3f(Px - B, Py - S, 0.0f);
    Win3DVertex3f(B + Px, Py - S, 0.0f);
    Win3DVertex3f(B + Px, Py - S, 0.0f);
    Win3DVertex3f(Px, (Py - S) + C, 0.0f);

    // Top-right corner.
    Win3DVertex3f(S + Px, Py - S, 0.0f);
    Win3DVertex3f((Px - A) + S, Py - S, 0.0f);
    Win3DVertex3f((Px - A) + S, Py - S, 0.0f);
    Win3DVertex3f((Px - A) + S, (Py - S) + A, 0.0f);
    Win3DVertex3f((Px - A) + S, (Py - S) + A, 0.0f);
    Win3DVertex3f(S + Px, (Py - S) + A, 0.0f);
    Win3DVertex3f(S + Px, (Py - S) + A, 0.0f);
    Win3DVertex3f(S + Px, Py - S, 0.0f);

    // Right tick.
    Win3DVertex3f((S + Px) - C, Py, 0.0f);
    Win3DVertex3f(((S + Px) - C) + C, Py - B, 0.0f);
    Win3DVertex3f(((S + Px) - C) + C, Py - B, 0.0f);
    Win3DVertex3f(((S + Px) - C) + C, B + Py, 0.0f);
    Win3DVertex3f(((S + Px) - C) + C, B + Py, 0.0f);
    Win3DVertex3f((S + Px) - C, Py, 0.0f);

    // Bottom-right corner.
    Win3DVertex3f(S + Px, S + Py, 0.0f);
    Win3DVertex3f((Px - A) + S, S + Py, 0.0f);
    Win3DVertex3f((Px - A) + S, S + Py, 0.0f);
    Win3DVertex3f((Px - A) + S, (Py - A) + S, 0.0f);
    Win3DVertex3f((Px - A) + S, (Py - A) + S, 0.0f);
    Win3DVertex3f(S + Px, (Py - A) + S, 0.0f);
    Win3DVertex3f(S + Px, (Py - A) + S, 0.0f);
    Win3DVertex3f(S + Px, S + Py, 0.0f);

    // Bottom tick.
    Win3DVertex3f(Px, (S + Py) - C, 0.0f);
    Win3DVertex3f(Px + B, ((S + Py) - C) + C, 0.0f);
    Win3DVertex3f(Px + B, ((S + Py) - C) + C, 0.0f);
    Win3DVertex3f(Px - B, ((S + Py) - C) + C, 0.0f);
    Win3DVertex3f(Px - B, ((S + Py) - C) + C, 0.0f);
    Win3DVertex3f(Px, (S + Py) - C, 0.0f);

    // Bottom-left corner.
    Win3DVertex3f(Px - S, S + Py, 0.0f);
    Win3DVertex3f((Px - S) + A, S + Py, 0.0f);
    Win3DVertex3f((Px - S) + A, S + Py, 0.0f);
    Win3DVertex3f((Px - S) + A, (Py - A) + S, 0.0f);
    Win3DVertex3f((Px - S) + A, (Py - A) + S, 0.0f);
    Win3DVertex3f(Px - S, (Py - A) + S, 0.0f);
    Win3DVertex3f(Px - S, (Py - A) + S, 0.0f);
    Win3DVertex3f(Px - S, S + Py, 0.0f);

    // Left tick.
    Win3DVertex3f((Px - S) + C, Py, 0.0f);
    Win3DVertex3f(Px - S, Py + B, 0.0f);
    Win3DVertex3f(Px - S, Py + B, 0.0f);
    Win3DVertex3f(Px - S, Py - B, 0.0f);
    Win3DVertex3f(Px - S, Py - B, 0.0f);
    Win3DVertex3f((Px - S) + C, Py, 0.0f);

    Win3DEnd();
}

void FHudSubTargetFlash::Draw(FSpaceCamera* Camera, float* ScreenPos) const
{
    // FUN_004e04f0: 2D state for Camera's viewport.
    Win3DDisable(0xde1); // GL_TEXTURE_2D
    Win3DDisable(0xb71); // GL_DEPTH_TEST
    Win3DSetOrtho2DForCamera(Camera);
    Win3DEnable(0xbe2);  // GL_BLEND
    Win3DSetBlendFunc(2, 4);

    // The point rounded in place: ftol(v + 0.5) (DAT_005c75e8, double), back to float.
    ScreenPos[0] = static_cast<float>(static_cast<int>(static_cast<double>(ScreenPos[0]) + 0.5));
    ScreenPos[1] = static_cast<float>(static_cast<int>(static_cast<double>(ScreenPos[1]) + 0.5));

    // Size = 2 ^ ((7 / Duration) * Timer) * BaseSize (_CIpow, DAT_005ca228 = 2.0).
    float const Size = static_cast<float>(
        pow(2.0, static_cast<double>((kFlashExponent / Duration) * Timer)) *
        static_cast<double>(BaseSize));

    // The two wide brackets only while flashing.
    if (Timer > 0.0f)
    {
        // Colour * 0.25 (DAT_005c94f8) per byte, clamped. FUN_00421aa0 is the three-byte form:
        // the alpha byte retail computes is never read.
        unsigned char Dim[3];
        for (int Index = 0; Index < 3; ++Index)
        {
            int Value = static_cast<int>(static_cast<double>(Color[Index]) * 0.25);
            if (Value < 0)
            {
                Value = 0;
            }
            else if (Value > 0xff)
            {
                Value = 0xff;
            }
            Dim[Index] = static_cast<unsigned char>(Value);
        }
        Win3DColor3ub(Dim[0], Dim[1], Dim[2]);
        DrawBracket(ScreenPos, static_cast<float>(static_cast<double>(Size) * 1.4)); // DAT_005d8848

        // Colour * 0.5 (DAT_005c75e0).
        for (int Index = 0; Index < 3; ++Index)
        {
            int Value = static_cast<int>(static_cast<double>(Color[Index]) * 0.5);
            if (Value < 0)
            {
                Value = 0;
            }
            else if (Value > 0xff)
            {
                Value = 0xff;
            }
            Dim[Index] = static_cast<unsigned char>(Value);
        }
        Win3DColor3ub(Dim[0], Dim[1], Dim[2]);
        DrawBracket(ScreenPos, static_cast<float>(static_cast<double>(Size) * 1.2)); // DAT_005d3ed0
    }

    // The full colour (FUN_00421aa0: three bytes, alpha 0xff).
    Win3DColor3ub(Color[0], Color[1], Color[2]);
    DrawBracket(ScreenPos, Size);

    Win3DApplyCamera(AsCamera(Camera));
}

// =============================================================================================
// HudTarget -- construction
// =============================================================================================

HudTarget::HudTarget(INI_Reader& /*Reader*/)
    : HudTargetBase()
    , ButtonScaler()
    , ShipName(nullptr)
    , SubtargetName(nullptr)
    , ScanButton(nullptr)
    , TractorButton(nullptr)
    , TradeButton(nullptr)
    , PreviousButton(nullptr)
    , NextButton(nullptr)
    , CommButton(nullptr)
    , RequestGroupButton(nullptr)
    , LabelRefreshTimer(0.0f)
    , bHadViewObject(0)
    , SubTargetCursor(0)
    , SubTargetCount(0)
    , HardpointCount(0)
    , HardpointCursor(0)
    , bScannerOpen(0)
    , SweepTime(0.0f)
    , bSweepRestart(0)
    , SweepTrailCount(0)
    , MinimizedFrame(nullptr)
    , ContactListButton(nullptr)
    , TargetButton(nullptr)
    , NextEnemyButton(nullptr)
    , FormationButton(nullptr)
    , Mode(kTargetPanelContactList)
    , ShownMode(kTargetPanelContactList)
    , bFormationAvailable(0)
    , RankText(nullptr)
    , ShownRank(-1.0f)
    , Rank(0.0f)
    , bClearPlayerTarget(0)
    , SmallFont(nullptr)
    , PlayerNameFont(nullptr)
    , bFirstUpdate(0)
{
    // FUN_004e08b0; publishes this in DAT_00674b04.
    G_HudTargetPanel = this;

    HomePosition.x = -0.296f;  // 0xbe978d50
    HomePosition.y = -0.367f;  // 0xbebbe76d
    HomePosition.z = 0.0f;
    // +0x524 is left alone (Create below sets it). Port-only: retail never writes +0x58c/+0x590,
    // the sweep block, the trail count or +0x634..+0x644 in the ctor; the init list zeroes them
    // for determinism -- Create, OnViewObjectChanged and DrawScanSweep write each before it is
    // read.

    HUDItem::Name = "Target";
    // Create(0, 0) is a direct (non-virtual) call, before the Win3D name is set.
    HudTarget::Create(nullptr, nullptr);
    Win3D::SetName("HUD_Target");

    HitCenter.x = -0.387f;     // 0xbec624dd
    HitCenter.y = -0.29f;      // 0xbe947ae1
    HitCenter.z = -1.0f;
    ClassName = "HUD_Target";
    HitHalfWidth = 0.11f;      // 0x3de147ae
    HitHalfHeight = 0.11f;

    // Retail's inlined test-and-set_pointer (see HudTargetBase::~HudTargetBase).
    ViewObject.set(nullptr);

    bHadViewObject = 0;
    LabelRefreshTimer = 0.0f;
    SubTargetCursor = 0;
    SubTargetCount = 0;
    bScannerOpen = 0;
    bClearPlayerTarget = 0;
    ShownRank = -1.0f;
    Rank = 0.0f;
    bFormationAvailable = 0;
    SmallFont = FindMenuFontImp("HudSmall");
    PlayerNameFont = FindMenuFontImp("PlayerName");

    // FUN_005760d0 for each id, on the handler at +0x384.
    unsigned const kEvents[0x10] = {0x5f, 0x21, 0x22, 0xcc, 0x54, 0x58, 0x59, 0x8b,
                                    0x24, 0x25, 0x26, 0xbb, 0xc9, 0xca, 0x23, 0x8a};
    for (unsigned Index = 0; Index < 0x10; ++Index)
    {
        RegisterEvent(kEvents[Index]);
    }
    bFirstUpdate = 1;
}

HudTarget::~HudTarget()
{
    // FUN_004e0cd0: clears DAT_00674b04; the rest is member and base destructors.
    G_HudTargetPanel = nullptr;
}

bool HudTarget::Create(Win3D* NewParent, void* Data)
{
    // FUN_004e0ee0 (this = the WinControl face).
    float const Zero[3] = {0.0f, 0.0f, 0.0f};

    float const BackgroundPos[3] = {-0.3405f, -0.243f, -0.91f};
    ViewPanel = CreateStaticControl("TargetBackground", "Interface\\hud\\HUD_target.cmp",
                                    BackgroundPos, nullptr, Zero, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0,
                                    Zero);

    float const MinimizedPos[3] = {-0.4105f, -0.37f, -1.0f};
    MinimizedFrame = CreateStaticControl("TargetMinimizedFrame",
                                         "Interface\\hud\\HUD_target_min.cmp", MinimizedPos,
                                         nullptr, Zero, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, Zero);

    // The collapsed hit box.
    HalfExtents[0] = 0.109f;  // 0x3ddf3b64
    HalfExtents[1] = 0.02f;   // 0x3ca3d70a
    HalfExtents[2] = 0.0f;
    HitOffset[0] = -0.402f;   // 0xbecdd2f2
    HitOffset[1] = -0.371f;   // 0xbebdf3b6
    HitOffset[2] = 0.0f;

    float const ToggleHalf[3] = {0.02f, 0.02f, 0.0f};
    ToggleButton = CreateButtonControl("HudTargetOpen", "interface\\hud\\hud_ShipTarget.3db",
                                       nullptr, "ui_window_open", nullptr, &HomePosition.x,
                                       nullptr, ToggleHalf, 0, 1, 0, 0, 0x3fe, 0, 0);
    ToggleButton->SetProperty(0x36, 1, 0);

    float const ShipNamePos[3] = {-0.488f, -0.2f, 0.0f};
    ShipName = CreateTextControl("TargetShipName", "HudSmall", ShipNamePos, nullptr, 0.205f,
                                 0.03f, 1.0f, 0, 1, 0, 1);
    ShipName->SetProperty(kTextPropTypewriter, 1, 0x14);
    ShipName->HomeOffset[0] = ShipNamePos[0];
    ShipName->HomeOffset[1] = ShipNamePos[1];
    ShipName->HomeOffset[2] = ShipNamePos[2];

    float const SubNamePos[3] = {-0.488f, -0.218f, 0.0f};
    SubtargetName = CreateTextControl("SubtargetName", "HudSmall", SubNamePos, nullptr, 0.205f,
                                      0.03f, 1.0f, 0, 1, 0, 1);
    SubtargetName->SetProperty(kTextPropTypewriter, 1, 0x14);
    SubtargetName->HomeOffset[0] = SubNamePos[0];
    SubtargetName->HomeOffset[1] = SubNamePos[1];
    SubtargetName->HomeOffset[2] = SubNamePos[2];

    float const SmallHalf[3] = {0.014f, 0.014f, 0.0f}; // 0x3c656042

    float const PreviousPos[3] = {-0.262f, -0.237f, 0.0f};
    PreviousButton = CreateButtonControl("TargetPreviousButton",
                                         "interface\\hud\\HUD_previoussub.3db", nullptr,
                                         "ui_item_select", nullptr, PreviousPos, nullptr,
                                         SmallHalf, 0, 1, 0, 0, 0x420, 0, 0);

    float const NextPos[3] = {-0.262f, -0.268f, 0.0f};
    NextButton = CreateButtonControl("TargetNextButton", "interface\\hud\\HUD_nextsub.3db",
                                     nullptr, "ui_item_select", nullptr, NextPos, nullptr,
                                     SmallHalf, 0, 1, 0, 0, 0x41f, 0, 0);

    float const ScanPos[3] = {-0.411f, -0.37f, 0.0f};
    ScanButton = CreateButtonControl("TargetScanButton",
                                     "interface\\hud\\HUD_scanshipinactive.3db", nullptr,
                                     "ui_item_select", nullptr, ScanPos, nullptr, SmallHalf, 0,
                                     1, 0, 0, 0x440, 0, 0x8a);

    TractorButton = CreateButtonControl("TargetTractorButton",
                                        "interface\\hud\\HUD_tractorbeam2.3db", nullptr,
                                        "ui_item_select", nullptr, ScanPos, nullptr, SmallHalf,
                                        0, 1, 0, 0, 0xbc2, 0, 0x8b);

    float const CommPos[3] = {-0.445f, -0.37f, 0.0f};
    CommButton = CreateButtonControl("TargetCommButton",
                                     "interface\\hud\\HUD_communication.3db", nullptr,
                                     "ui_item_select", nullptr, CommPos, nullptr, SmallHalf, 0,
                                     1, 0, 0, 0x3fd, 0, 0);

    RequestGroupButton = CreateButtonControl("TargetRequestGroupButton",
                                             "interface\\hud\\HUD_grouprequest.3db", nullptr,
                                             "ui_item_select", nullptr, CommPos, nullptr,
                                             SmallHalf, 0, 1, 0, 0, 0x621, 0, 0);

    float const TradePos[3] = {-0.202f, -0.37f, 0.0f};
    TradeButton = CreateButtonControl("TargetTradeButton", "interface\\hud\\HUD_trade.3db",
                                      nullptr, "ui_item_select", nullptr, TradePos, nullptr,
                                      SmallHalf, 0, 1, 0, 0, 0xbc3, 0, 0);

    float const ClosePos[3] = {-0.386f, -0.308f, -0.5f};
    CloseButton = CreateButtonControl("TargetCloseButton",
                                      "interface\\NeuroNet\\Inventory\\x.3db", nullptr,
                                      "ui_select_item", nullptr, ClosePos, nullptr, SmallHalf, 0,
                                      1, 0, 0, 0x409, 0, 0);

    float const ContactListPos[3] = {-0.343f, -0.37f, 0.0f};
    ContactListButton = CreateButtonControl("SwitchToContactList",
                                            "interface\\hud\\HUD_contactlist.3db", nullptr,
                                            "ui_select_item", nullptr, ContactListPos, nullptr,
                                            SmallHalf, 0, 1, 0, 0, 0x448, 0, 0x54);

    float const TargetPos[3] = {-0.377f, -0.37f, 0.0f};
    TargetButton = CreateButtonControl("SwitchToTarget",
                                       "interface\\HUD\\HUD_targetedobject.3db", nullptr,
                                       "ui_select_item", nullptr, TargetPos, nullptr, SmallHalf,
                                       0, 1, 0, 0, 0x5ff, 0, 0x58);

    float const NextEnemyPos[3] = {-0.479f, -0.37f, 0.0f};
    NextEnemyButton = CreateButtonControl("NextEnemyButton",
                                          "interface\\HUD\\HUD_nextenemy.3db", nullptr,
                                          "ui_select_item", nullptr, NextEnemyPos, nullptr,
                                          SmallHalf, 0, 1, 0, 0, 0x3e2, 0, 0x26);

    float const FormationPos[3] = {-0.278f, -0.37f, 0.0f};
    FormationButton = CreateButtonControl("FormationList",
                                          "interface\\HUD\\HUD_formation.3db", nullptr,
                                          "ui_select_item", nullptr, FormationPos, nullptr,
                                          SmallHalf, 0, 1, 0, 0, 0x60c, 0, 0x59);
    FormationButton->SetProperty(kWinPropHide, 1, 0);

    float const RankPos[3] = {-0.49f, -0.361f, 0.0f};
    RankText = CreateTextControl("TargetRankText", "HudSmall", RankPos, nullptr, 0.228f, 0.03f,
                                 0.9f, 0, 1, 0x574, 1);

    HideExpandedControls(true);
    PreviousButton->SetProperty(kWinPropHide, 1, 0);
    NextButton->SetProperty(kWinPropHide, 1, 0);
    TradeButton->SetProperty(kWinPropHide, 1, 0);
    ContactListButton->SetProperty(0xc, Mode == kTargetPanelContactList ? 1 : 0, 0);
    TargetButton->SetProperty(0xc, Mode == kTargetPanelTarget ? 1 : 0, 0);

    // Retail clones the StyleNode twice: the list takes a clone of the clone, and the first
    // clone is deleted.
    {
        StyleNode Style(kTextStyleNotice);
        RDLNode* const Clone = Style.Clone();
        RdlAppendNode(StyleList, Clone->Clone());
        delete Clone;
    }
    if (LoadIdsIntoRdl(0x60070, StyleList))
    {
        RdlRemoveLastParagraph(StyleList);
    }

    SetOffset(0.0f, 0.0f, -1.0f);
    SetPosition(&Position.x);
    return Win3D::Create(NewParent, Data);
}

// =============================================================================================
// HUDItem face
// =============================================================================================

void HudTarget::ToggleExpanded()
{
    // FUN_004e2fe0: nothing while either animation runs.
    if (ButtonMover::IsActive() || ButtonScaler::IsActive())
    {
        return;
    }

    if (bExpanded == 0)
    {
        // Opening: slide the toggle from its rest spot to the panel corner and drop the bottom
        // row from y -0.37 to -0.4.
        ButtonMover::Start(kAnimSeconds);
        ButtonMover::AddButton(ToggleButton, &HomePosition.x, kToggleOpen);

        WinControl* const Row[8] = {NextEnemyButton, ScanButton, TractorButton,
                                    CommButton, RequestGroupButton, ContactListButton,
                                    TargetButton, MinimizedFrame};
        float const RowX[8] = {-0.479f, -0.411f, -0.411f, -0.445f,
                               -0.445f, -0.343f, -0.377f, -0.4105f};
        float const RowZ[8] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f};
        for (int Index = 0; Index < 8; ++Index)
        {
            float const FromPos[3] = {RowX[Index], kRowY, RowZ[Index]};
            float const ToPos[3] = {RowX[Index], kRowLoweredY, RowZ[Index]};
            ButtonMover::AddButton(Row[Index], FromPos, ToPos);
        }

        LabelRefreshTimer = kOne;
        ShipName->SetProperty(kTextPropTypewriter, 1, 0x14);
        SubtargetName->SetProperty(kTextPropTypewriter, 1, 0x14);
        return;
    }

    // Closing: hide the panel controls, shrink the view panel 1.0 -> 0.1, hide the formation
    // button's flags.
    HideExpandedControls(true);
    ButtonScaler::Start(kAnimSeconds);
    ButtonScaler::AddButton(ViewPanel, kOne, kScaleSmall);
    ClearFlagBits(FormationButton);
}

void HudTarget::HideExpandedControls(bool bHide)
{
    // FUN_004e1930.
    uintptr_t const Hide = bHide ? 1 : 0;
    ViewPanel->SetProperty(kWinPropHide, Hide, 0);
    ShipName->SetProperty(kWinPropHide, Hide, 0);
    SubtargetName->SetProperty(kWinPropHide, Hide, 0);
    CloseButton->SetProperty(kWinPropHide, Hide, 0);
    RankText->SetProperty(kWinPropHide, Hide, 0);
    PreviousButton->SetProperty(kWinPropHide, Hide, 0);
    NextButton->SetProperty(kWinPropHide, Hide, 0);
    TradeButton->SetProperty(kWinPropHide, Hide, 0);
}

void HudTarget::DrawViewObject()
{
    // FUN_004e3c90 -- the target view, drawn by HudTargetBase::Draw right after ViewPanel.
    IObjRW* const View = WatchedObjRw(ViewObject);
    if (View == nullptr)
    {
        return;
    }
    if (WatchedObjRw(HUDItem::Target) == nullptr)
    {
        return;
    }
    if (GetGameOption(0xb) == 0)
    {
        return;
    }
    if (Mode != kTargetPanelTarget)
    {
        return;
    }

    // The GL viewport saved (FUN_00422390, GL_VIEWPORT), the view camera pointed at the object.
    int SavedViewport[4];
    Win3DGetViewport(0xba2, SavedViewport);
    SetupViewCamera(GetGameOption(0xc) != 0, -0.0125f);

    // A CSimple (class & 3) is drawn -- as a wire sphere when TypeBits & 7, else as
    // its coloured part cache -- and the scan sweep over it; anything else draws nothing.
    CSimple* Simple = nullptr;
    CObject* const Object = CObjectOfObjRw(WatchedObjRw(ViewObject));
    if (Object != nullptr && IsClass(Object, 3))
    {
        Simple = static_cast<CSimple*>(Object);
        if ((Simple->TypeBits & (OBJ_MOON | OBJ_PLANET | OBJ_SUN)) != 0)
        {
            // The chrome colour at 80 % per byte (0.8f is DAT_005d8d40) as the current colour,
            // and a 16 x 16 sphere of the hierarchy radius in the object's transform (slot 4,
            // ICF-folded with get_transform).
            Win3DDisable(kWin3DDepthTest);
            SpaceCameraSetFovY(&ViewCamera, 45.0f);
            Win3DApplyCameraPerspective(AsCamera(&ViewCamera));
            unsigned char const* const Chrome = G_HudChromeColorRgba; // DAT_00679bb4
            unsigned char Colour[4];
            for (int Byte = 0; Byte < 4; ++Byte)
            {
                int const Scaled = static_cast<int>(static_cast<double>(Chrome[Byte]) *
                                                    static_cast<double>(0.8f));
                int Clamped = Scaled;
                if (Clamped < 0)
                {
                    Clamped = 0;
                }
                else if (Clamped > 0xff)
                {
                    Clamped = 0xff;
                }
                Colour[Byte] = static_cast<unsigned char>(Clamped);
            }
            Win3DColor4ubv(Colour);
            Vector Center;
            float const Radius = Simple->get_physical_radius_r(Center);
            IObjRW* const Viewed = WatchedObjRw(ViewObject);
            Win3DDrawWireSphere(&ViewCamera, &Viewed->get_transform(), Radius, 0x10, 0x10);
        }
        else
        {
            // The part cache (FUN_00406510) in the object's transform (slot 4).
            ColourViewParts();
            IObjRW* const Viewed = WatchedObjRw(ViewObject);
            PartCacheDraw(&PartCache, AsCamera(&ViewCamera),
                          &Viewed->get_transform());
        }
        DrawScanSweep();
    }

    // The Win3D camera back and the saved viewport (FUN_00421620).
    Win3DApplyCamera(AsCamera(Win3DGetCamera()));
    Win3DSetViewportRect(SavedViewport[0], SavedViewport[1], SavedViewport[2], SavedViewport[3],
                         "E:\\FL\\Scratch\\Source\\Client\\space\\HUD_Target.cpp", 0x642);

    // A flashing sub-target of a CEqObj -- its centre of mass, else the object's position --
    // projected through the view camera gets the bracket.
    if (SubTargetFlash.SubId == SUBOBJ_ID_NONE)
    {
        return;
    }
    if (Simple == nullptr || !IsClass(Simple, CObject::ClassEqObj))
    {
        return;
    }
    CEqObj* const EqObj = static_cast<CEqObj*>(static_cast<CObject*>(Simple));
    Vector Where;
    if (!EqObj->get_sub_obj_center_of_mass(SubTargetFlash.SubId, Where))
    {
        Where = WatchedObjRw(ViewObject)->get_position();
    }
    float Screen[3];
    if (static_cast<unsigned char>(AsCamera(&ViewCamera)->ProjectVec((Vector*)Screen, &Where)) == 0)
    {
        return;
    }
    SubTargetFlash.Draw(Win3DGetCamera(), Screen);
}

void HudTarget::UpdateLabels()
{
    // FUN_004e3f80.
    if (ShipName == nullptr || SubtargetName == nullptr)
    {
        return;
    }
    ShipName->SetProperty(kWinPropHide, 1, 0);
    SubtargetName->SetProperty(kWinPropHide, 1, 0);
    ShipName->SetProperty(kTextPropSetIds, 1, 0);
    SubtargetName->SetProperty(kTextPropSetIds, 1, 0);

    CObject* const Object = CObjectOfObjRw(WatchedObjRw(ViewObject));
    if (Object == nullptr || !IsClass(Object, 3))
    {
        return;
    }
    CSimple* const Simple = static_cast<CSimple*>(Object);

    // SetProperty(1, x) takes "hide", so every show below is !bShow.
    bool const bShow =
        bExpanded != 0 && !ButtonMover::IsActive() && !ButtonScaler::IsActive();
    uintptr_t const Hide = bShow ? 0 : 1;

    wchar_t IdsText[0x80];
    wchar_t SubName[0x100];

    // An unvisited solar shows only its type caption.
    if (HudIsUnvisitedSolar(Simple))
    {
        SubName[0] = 0;
        if (GetIdsStringWWithRawFallback(HudObjectTypeIds(Simple), IdsText, 0x80) == 0)
        {
            IdsText[0] = 0;
        }
        wcscpy(SubName, IdsText);
        ShipName->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(SubName), 0);
        ShipName->SetProperty(kWinPropHide, Hide, 0);
        SubtargetName->SetProperty(kWinPropHide, 1, 0);
        return;
    }

    // CShip +0x2bd: a player's ship.
    CShip* const Ship = IsClass(Object, CObject::ClassShip) ? static_cast<CShip*>(Object) : nullptr;
    bool const bPlayerShip = Ship != nullptr && Ship->bIsPlayer != 0;

    // The font and the rest position: the home offset (+0x2d4) copied to the offset (+0xa8),
    // a player ship's y lowered by 0.004 (DAT_005d7bf0, subtracted in double, rounded to float).
    NnTextControl* const Labels[2] = {static_cast<NnTextControl*>(ShipName),
                                      static_cast<NnTextControl*>(SubtargetName)};
    for (int Index = 0; Index < 2; ++Index)
    {
        NnTextControl* const Label = Labels[Index];
        Label->SetFont(bPlayerShip ? PlayerNameFont : SmallFont);
        Label->Offset[0] = Label->HomeOffset[0];
        Label->Offset[1] = Label->HomeOffset[1];
        Label->Offset[2] = Label->HomeOffset[2];
        if (bPlayerShip)
        {
            Label->Offset[1] = static_cast<float>(static_cast<double>(Label->Offset[1]) - 0.004);
        }
        Label->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(Label->Offset), 0);
    }

    // The name line (Mode 0) with the faction short name.
    FHudWideText Caption = {nullptr, 0};
    if (HudBuildObjectCaption(Simple, &Caption, kCaptionName, true))
    {
        ShipName->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(Caption.Text), 0);
        ShipName->SetProperty(kWinPropHide, Hide, 0);
        // The flashing sub-target's name, when it has one.
        bool bSubDone = false;
        if (SubTargetFlash.SubId != SUBOBJ_ID_NONE)
        {
            GetSubTargetName(SubName);
            if (SubName[0] != 0)
            {
                SubtargetName->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(SubName), 0);
                SubtargetName->SetProperty(kWinPropHide, Hide, 0);
                bSubDone = true;
            }
        }
        if (!bSubDone)
        {
            // Otherwise the location line (Mode 1) -- the label keeps its old text when that
            // fails, and is shown either way.
            if (HudBuildObjectCaption(Simple, &Caption, kCaptionLocation, true))
            {
                SubtargetName->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(Caption.Text), 0);
            }
            SubtargetName->SetProperty(kWinPropHide, Hide, 0);
        }
    }
    else
    {
        // No caption -- the name and sub-name built by hand.
        wchar_t NameText[0x100];
        wchar_t SubText[0x100];
        NameText[0] = 0;
        SubText[0] = 0;
        if (HudIsUnvisitedSolar(Simple))
        {
            if (GetIdsStringWWithRawFallback(HudObjectTypeIds(Simple), IdsText, 0x80) == 0)
            {
                IdsText[0] = 0;
            }
            wcscpy(NameText, IdsText);
        }
        else
        {
            // The pilot name, the sub-target or ship group name, and the ids_name when there is
            // no pilot name.
            bool bHavePilot = false;
            FHudWideText Pilot = {nullptr, 0};
            GetShipPilotName(Object, &Pilot);
            if (Pilot.Text != nullptr && Pilot.Text[0] != 0)
            {
                wcscpy(NameText, Pilot.Text);
                bHavePilot = true;
            }
            if (SubTargetFlash.SubId != SUBOBJ_ID_NONE)
            {
                GetSubTargetName(SubName);
                if (SubName[0] != 0)
                {
                    wcscpy(SubText, SubName);
                }
            }
            else if (Ship != nullptr)
            {
                unsigned const Group = Ship->get_group_name();
                if (Group != 0)
                {
                    if (GetIdsStringWWithRawFallback(Group, IdsText, 0x80) == 0)
                    {
                        IdsText[0] = 0;
                    }
                    wcscpy(SubText, IdsText);
                }
            }
            if (!bHavePilot)
            {
                wchar_t ArchName[0x80];
                if (GetIdsStringWWithRawFallback(GetObjectIdsName(Object), ArchName, 0x80) == 0)
                {
                    ArchName[0] = 0;
                }
                wcscpy(NameText, ArchName);
            }
            if (Pilot.Text != nullptr)
            {
                free(Pilot.Text);
            }
        }
        ShipName->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(NameText), 0);
        ShipName->SetProperty(kWinPropHide, Hide, 0);
        SubtargetName->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(SubText), 0);
        SubtargetName->SetProperty(kWinPropHide, Hide, 0);
    }

    if (Caption.Text != nullptr)
    {
        free(Caption.Text);
    }
}

void HudTarget::OnViewObjectChanged()
{
    // FUN_004e4690.
    SubTargetFlash.SubId = SUBOBJ_ID_NONE;
    // NN_Scanner is a Win3D window (nn_scanner.h), not a flat MenuUi record.
    Win3D* const Scanner = Win3DFindByName("NN_Scanner");
    if (Scanner != nullptr)
    {
        Scanner->ProcessMessage(kWinMsgHide, 0, 0);
    }

    IObjRW* const View = WatchedObjRw(ViewObject);
    if (View != nullptr)
    {
        Win3D* const Help = Win3DFindByName("NN_Help");
        if (Help != nullptr)
        {
            // The view object into the open info window.
            static_cast<NnHelp*>(Help)->ShowObjectInfo(View);
        }

        // Both label colours from the reputation colour.
        if (WatchedObjRw(HUDItem::Target) != nullptr && WatchedObjRw(ViewObject) != nullptr &&
            ShipName != nullptr && SubtargetName != nullptr)
        {
            unsigned long Colour = 0;
            HudReputationColor(&Colour, WatchedObjRw(HUDItem::Target), WatchedObjRw(ViewObject));
            ShipName->SetProperty(kWinPropColour, reinterpret_cast<uintptr_t>(&Colour), 1);
            SubtargetName->SetProperty(kWinPropColour, reinterpret_cast<uintptr_t>(&Colour), 1);
        }
        ShipName->SetProperty(kTextPropTypewriter, 1, 0x14);
        SubtargetName->SetProperty(kTextPropTypewriter, 1, 0x14);

        SubTargetCount = RebuildSubTargets(&SubTargets);

        // The root instance's hardpoints and every related instance's. Each archetype handle is
        // released (slot 15) before it is enumerated -- retail's order.
        HardpointNames.clear();
        const ArchetypeIndex RootArch =
            DALib::Engine->GetInstanceArchetype(View->get_index());
        DALib::Engine->ReleaseHandle(RootArch);
        DALib::Hardpoint->EnumerateHardpoints(&CollectHardpointName, RootArch, this);
        InstanceIndex Cursor = InvalidInstanceIndex;
        for (;;)
        {
            Cursor = DALib::Engine->GetRelatedInstance(View->get_index(), 0,
                                                       Cursor);
            if (Cursor == InvalidInstanceIndex)
            {
                break;
            }
            const ArchetypeIndex Arch = DALib::Engine->GetInstanceArchetype(Cursor);
            DALib::Engine->ReleaseHandle(Arch);
            DALib::Hardpoint->EnumerateHardpoints(&CollectHardpointName, Arch, this);
        }
        HardpointCount = static_cast<int>(HardpointNames.size());
        HardpointCursor = 0;

        bClearPlayerTarget = 0;
        IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
        if (WatchedObjRw(ViewObject) != nullptr && Player != nullptr)
        {
            // The view's attitude towards the player.
            if (GetAttitudeClass(Player, WatchedObjRw(ViewObject)) == -1)
            {
                bClearPlayerTarget = 1;
            }
        }
        if (Scanner != nullptr)
        {
            OpenScanner();
        }
    }

    if (G_bTradeRequested != 0 || NnTradeIsOpen())
    {
        WithdrawTrade();
    }
}

// =============================================================================================
// WinControl face
// =============================================================================================

intptr_t HudTarget::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // FUN_004e19e0.
    if (Msg == kWinNotifyClick)
    {
        if (A == reinterpret_cast<uintptr_t>(CloseButton))
        {
            ToggleExpanded();
            return 1;
        }
        IObjRW* const View = WatchedObjRw(ViewObject);
        if (View != nullptr)
        {
            if (A == reinterpret_cast<uintptr_t>(NextButton) ||
                A == reinterpret_cast<uintptr_t>(PreviousButton))
            {
                if (A == reinterpret_cast<uintptr_t>(NextButton))
                {
                    SelectNextSubTarget();
                }
                else
                {
                    SelectPreviousSubTarget();
                }
                UpdateLabels();
                SubtargetName->SetProperty(kTextPropTypewriter, 1, 0x14);
                return 1;
            }
            if (A == reinterpret_cast<uintptr_t>(RequestGroupButton))
            {
                if (GetPlayerObjRW() != nullptr)
                {
                    CObject* const Object = CObjectOfObjRw(WatchedObjRw(ViewObject));
                    if (Object != nullptr && IsClass(Object, CObject::ClassShip) &&
                        static_cast<CSimple*>(Object)->OwnerPlayer != 0)
                    {
                        // Only the low byte of the result is returned.
                        return static_cast<unsigned char>(HudPostCommand(USER_GROUP_INVITE, 0));
                    }
                }
                return 1;
            }
            if (A == reinterpret_cast<uintptr_t>(CommButton))
            {
                IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
                if (Player != nullptr)
                {
                    // Evaluated in retail's order: the system id first.
                    unsigned const SystemId = GetPlayerStarSystemIdRetail();
                    unsigned const ViewId = View->get_id();
                    unsigned const PlayerId = Player->get_id();
                    G_Server->Hail(PlayerId, ViewId, SystemId);
                }
                return 1;
            }
            if (A == reinterpret_cast<uintptr_t>(TradeButton))
            {
                return static_cast<unsigned char>(HudPostCommand(USER_TRADE_REQUEST, 0));
            }
        }
    }
    return HudTargetBase::SetProperty(Msg, A, B);
}

void HudTarget::Draw()
{
    // FUN_004e1bb0.
    if (IsSpaceInitInProgress())
    {
        return;
    }
    if (IsLetterboxVisible())
    {
        return;
    }
    if ((ButtonMover::IsActive() || ButtonScaler::IsActive()) && TradeButton != nullptr &&
        (TradeButton->Flags & 2) != 0)
    {
        TradeButton->Draw();
    }
    if (ButtonScaler::IsActive())
    {
        ViewPanel->Draw();
        return;
    }
    HudTargetBase::Draw();
}

void HudTarget::Update()
{
    // FUN_004e1c20 (this = the WinControl face).
    if (bFirstUpdate != 0)
    {
        bFirstUpdate = 0;
        unsigned char const bSessionExpanded = TestGameSessionFlags(1) ? 1 : 0;
        if (bSessionExpanded != bExpanded)
        {
            ToggleExpanded();
        }
    }

    // Keep the player's target valid, then drop a target that is cloaked (a ship with +0x140
    // >= 1.0) or -- unless the gameobj byte +0xa8 is set -- more than 10 km away.
    IObjRW* const Synced = SyncPlayerTarget();
    if (Synced != nullptr && WatchedObjRw(HUDItem::Target) != nullptr)
    {
        FClientGameobj* const Gameobj = ClientObjDb_FindObject(Synced->get_id(), 1);
        if (Gameobj != nullptr)
        {
            CObject const* const Object = Gameobj->cobject();
            bool bClear = false;
            // A NaN cloak counts as cloaked.
            if (Object != nullptr && IsClass(Object, CObject::ClassShip) &&
                !(static_cast<CEqObj const*>(Object)->CloakPercent < kOne))
            {
                bClear = true;
            }
            else if (GameobjByteA8(Gameobj) == 0)
            {
                Vector const& From = Gameobj->get_position();
                Vector const& To = WatchedObjRw(HUDItem::Target)->get_position();
                float const Dx = To.x - From.x;
                float const Dy = To.y - From.y;
                float const Dz = To.z - From.z;
                // Summed in retail's order.
                if ((Dx * Dx + Dz * Dz) + Dy * Dy > kFarTargetSquared)
                {
                    bClear = true;
                }
            }
            if (bClear)
            {
                ClearTargetOf(WatchedObjRw(HUDItem::Target));
            }
        }
    }

    if (ButtonMover::IsActive() || ButtonScaler::IsActive())
    {
        // The toggle is hidden while the close (scaler) runs.
        uintptr_t const Hide = (bExpanded != 0 && !ButtonMover::IsActive()) ? 1 : 0;
        ToggleButton->SetProperty(kWinPropHide, Hide, 0);
        ButtonScaler::Update();
        UpdateTradeButton();
        HudTargetBase::Update();
        return;
    }

    if (bExpanded != 0)
    {
        // The formation button -- in formation (behaviour 10), with followers
        // (single player) or in a group of at least two live members that holds the player
        // (multiplayer, when the player leads or is the group's first member).
        unsigned char bFormation = 0;
        IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
        CObject* const PlayerObject = CObjectOfObjRw(Player);
        if (PlayerObject != nullptr && IsClass(PlayerObject, CObject::ClassShip))
        {
            CShip* const Ship = static_cast<CShip*>(PlayerObject);
            IBehaviorManager* const Behavior = Ship->get_behavior_interface();
            if (Behavior != nullptr)
            {
                bFormation = (Behavior->CurrentBehaviorIndex == 10) ? 1 : 0;
            }
            if (SinglePlayer())
            {
                if (bFormation == 0 && !Ship->FormationFollowers.empty())
                {
                    bFormation = 1;
                }
            }
            else
            {
                int const Members = static_cast<int>(RetailU32QueueSize());
                // Retail tests the watched pointer (+0x1b8) and the IObjRW 8 below it for null.
                uintptr_t const RawLeader =
                    reinterpret_cast<uintptr_t>(Ship->FormationLeader.target());
                bool bCount = RawLeader != 0 && RawLeader != 8;
                if (!bCount && Members != 0 && RetailU32QueueAt(0) == Ship->Id)
                {
                    bCount = true;
                }
                if (bCount)
                {
                    int Live = 0;
                    bool bFoundPlayer = false;
                    for (int Index = 0; Index < Members; ++Index)
                    {
                        unsigned const MemberId = RetailU32QueueAt(static_cast<unsigned>(Index));
                        if (MemberId != 0 && ClientObjDb_FindObject(MemberId, 1) != nullptr)
                        {
                            if (MemberId == Player->get_id())
                            {
                                bFoundPlayer = true;
                            }
                            ++Live;
                        }
                    }
                    if (Live > 1 && bFoundPlayer)
                    {
                        bFormation = 1;
                    }
                }
            }
        }
        FormationButton->SetProperty(kWinPropHide, bFormation == 0 ? 1 : 0, 0);
        if (bFormation == 0 && Mode == kTargetPanelFormation)
        {
            Mode = kTargetPanelContactList;
        }
        bFormationAvailable = bFormation;
    }

    // The player ship's current target becomes the view object.
    CShip* PlayerShip = nullptr;
    {
        CObject* const TargetObject = CObjectOfObjRw(WatchedObjRw(HUDItem::Target));
        if (TargetObject != nullptr && IsClass(TargetObject, CObject::ClassShip))
        {
            PlayerShip = static_cast<CShip*>(TargetObject);
            IObjRW* const Current = SyncPlayerTarget();
            if (Current == nullptr && bHadViewObject != 0 && bExpanded != 0)
            {
                Mode = kTargetPanelContactList;
            }
            SetViewObject(Current);
            bHadViewObject = (WatchedObjRw(ViewObject) != nullptr) ? 1 : 0;
        }
    }

    if (Mode != ShownMode)
    {
        ContactListButton->SetProperty(0xc, Mode == kTargetPanelContactList ? 1 : 0, 0);
        TargetButton->SetProperty(0xc, Mode == kTargetPanelTarget ? 1 : 0, 0);
        FormationButton->SetProperty(0xc, Mode == kTargetPanelFormation ? 1 : 0, 0);
    }

    // The target was lost while flagged unfriendly -- post 0x26 (next target) and play UI
    // sound 0x23 when that found one.
    if (WatchedObjRw(ViewObject) == nullptr && bClearPlayerTarget != 0)
    {
        bClearPlayerTarget = 0;
        HudPostCommand(USER_CLOSEST_ENEMY, 0);
        if (SyncPlayerTarget() != nullptr)
        {
            PlayOrRewindUiSoundSlot(0x23);
        }
    }

    if (bExpanded != 0 && Mode != kTargetPanelTarget)
    {
        if (ShownMode == kTargetPanelTarget)
        {
            // Leaving the target view: hide every child, then show this panel, the view panel,
            // the close button and the three switches again.
            SetHiddenWithChildren(true);
            Flags |= 3;
            SetFlagBits(ViewPanel);
            SetFlagBits(CloseButton);
            SetFlagBits(ContactListButton);
            SetFlagBits(NextEnemyButton);
            SetFlagBits(TargetButton);
            FormationButton->SetProperty(kWinPropHide, bFormationAvailable == 0 ? 1 : 0, 0);
            LabelRefreshTimer = kOne;
        }
        ShownMode = Mode;
        Win3D::Update();
        ButtonMover::Update();
        ButtonScaler::Update();
        UpdateButtons(false);
        return;
    }

    if (bExpanded != 0 && ShownMode != kTargetPanelTarget)
    {
        ShownMode = Mode;
        SetFlagBits(ShipName);
        SetFlagBits(SubtargetName);
        SetFlagBits(RankText);
        SetFlagBits(PreviousButton);
        SetFlagBits(NextButton);
        SetFlagBits(TradeButton);
        ClearFlagBits(ToggleButton);
    }

    HudTargetBase::Update();

    if (PlayerShip != nullptr && WatchedObjRw(ViewObject) != nullptr)
    {
        unsigned short const Sub = PlayerShip->get_sub_target();
        bool const bChanged = Sub != SubTargetFlash.SubId;
        SubTargetFlash.Update(static_cast<float>(GetClientFrameTimeCapped()), Sub);
        if (bExpanded != 0 && bChanged)
        {
            UpdateLabels();
            SubtargetName->SetProperty(kTextPropTypewriter, 1, 0x14);
        }
        UpdateButtons(true);
        return;
    }

    ShipName->SetProperty(kWinPropHide, 1, 0);
    SubtargetName->SetProperty(kWinPropHide, 1, 0);
    RankText->SetProperty(kWinPropHide, 1, 0);
    ClearFlagBits(PreviousButton);
    ClearFlagBits(NextButton);
    ClearFlagBits(TradeButton);
    UpdateButtons(true);
}

bool HudTarget::MouseEvent(FWin3DMouseEvent const* Event)
{
    // FUN_004e4af0: a release (-1) hides the tool tip; with a picked sub-target and button 2 it
    // plays UI sound 0x18 and routes command 0x1c {view id, SubTargetId} to the camera.
    if (Event->Button == kWin3DMouseLeft)
    {
        NnToolTipHideGlobal();
        IObjRW* const View = WatchedObjRw(ViewObject);
        if (SubTargetId != SUBOBJ_ID_NONE && Event->Action == kWin3DMousePress && View != nullptr)
        {
            PlayOrRewindUiSoundSlot(0x18);
            FSpaceCommand Command = {};
            Command.Byte26 = 1;
            Command.Id = USER_SET_TARGET;
            Command.ObjectId = View->get_id();
            Command.SubObjectId = SubTargetId;
            SpaceSendCommand(&Command);
        }
    }
    return true;
}

// =============================================================================================
// Animation faces
// =============================================================================================

void HudTarget::OnMoveFinished()
{
    // FUN_004e3320.
    if (bExpanded == 0)
    {
        // The slide ended: show the panel controls, hide the toggle and the minimized frame,
        // grow the view panel 0.1 -> 1.0.
        HideExpandedControls(false);
        ClearFlagBits(ToggleButton);
        ClearFlagBits(MinimizedFrame);
        ButtonScaler::Start(kAnimSeconds);
        ButtonScaler::AddButton(ViewPanel, kScaleSmall, kOne);
        ButtonScaler::Update();
        return;
    }

    // The collapse ended.
    bExpanded = 0;
    SetGameSessionFlags(1, false);
    ContactListButton->SetProperty(0xc, Mode == kTargetPanelContactList ? 1 : 0, 0);
    TargetButton->SetProperty(0xc, Mode == kTargetPanelTarget ? 1 : 0, 0);
    FormationButton->SetProperty(0xc, Mode == kTargetPanelFormation ? 1 : 0, 0);
    HalfExtents[0] = 0.109f;
    HalfExtents[1] = 0.02f;
    HalfExtents[2] = 0.0f;
    HitOffset[0] = -0.402f;
    HitOffset[1] = -0.371f;
    HitOffset[2] = 0.0f;
    LabelRefreshTimer = kOne;
}

void HudTarget::OnScaleFinished()
{
    // FUN_004e3480.
    if (bExpanded == 0)
    {
        // The view panel reached full size: the panel is open.
        bExpanded = 1;
        SetGameSessionFlags(1, true);
        float const TopY = -0.171f; // 0xbe2f1aa0
        SetPos(NextEnemyButton, -0.465f, TopY, 0.0f);
        SetPos(ScanButton, -0.397f, TopY, 0.0f);
        SetPos(TractorButton, -0.397f, TopY, 0.0f);
        SetPos(CommButton, -0.431f, TopY, 0.0f);
        SetPos(RequestGroupButton, -0.431f, TopY, 0.0f);
        SetPos(ContactListButton, -0.295f, TopY, 0.0f);
        SetPos(TargetButton, -0.329f, TopY, 0.0f);
        SetPos(FormationButton, -0.363f, TopY, 0.0f);

        if (Mode != kTargetPanelTarget)
        {
            SetHiddenWithChildren(true);
            Flags |= 3;
            SetFlagBits(ViewPanel);
            SetFlagBits(CloseButton);
            ClearFlagBits(ShipName);
            ClearFlagBits(SubtargetName);
            ClearFlagBits(RankText);
            ClearFlagBits(PreviousButton);
            ClearFlagBits(NextButton);
            ClearFlagBits(TradeButton);
        }
        else
        {
            ClearFlagBits(ToggleButton);
            ClearFlagBits(MinimizedFrame);
            UpdateLabels();
            SetFlagBits(RankText);
            SetFlagBits(PreviousButton);
            SetFlagBits(NextButton);
            SetFlagBits(TradeButton);
        }

        LabelRefreshTimer = kOne;
        SetFlagBits(ContactListButton);
        SetFlagBits(TargetButton);
        SetFlagBits(NextEnemyButton);
        SetFlagBits(TargetButton); // retail sets +0x63c twice
        FormationButton->SetProperty(kWinPropHide, bFormationAvailable == 0 ? 1 : 0, 0);
        ContactListButton->SetProperty(0xc, Mode == kTargetPanelContactList ? 1 : 0, 0);
        TargetButton->SetProperty(0xc, Mode == kTargetPanelTarget ? 1 : 0, 0);
        FormationButton->SetProperty(0xc, Mode == kTargetPanelFormation ? 1 : 0, 0);
        HalfExtents[0] = 0.131f;  // 0x3e0624dd
        HalfExtents[1] = 0.117f;  // 0x3def9db2
        HalfExtents[2] = 0.0f;
        HitOffset[0] = -0.379f;   // 0xbec20c4a
        HitOffset[1] = -0.266f;   // 0xbe883127
        HitOffset[2] = 0.0f;
        return;
    }

    // The view panel shrank away: slide the toggle home and raise the bottom row back from
    // -0.4 to -0.37 (the buttons are put at -0.4 first, the minimized frame shown there).
    ButtonMover::Start(kAnimSeconds);
    ButtonMover::AddButton(ToggleButton, kToggleOpen, &HomePosition.x);

    SetPos(NextEnemyButton, -0.479f, kRowLoweredY, 0.0f);
    SetPos(ScanButton, -0.411f, kRowLoweredY, 0.0f);
    SetPos(TractorButton, -0.411f, kRowLoweredY, 0.0f);
    SetPos(CommButton, -0.445f, kRowLoweredY, 0.0f);
    SetPos(RequestGroupButton, -0.445f, kRowLoweredY, 0.0f);
    SetPos(ContactListButton, -0.343f, kRowLoweredY, 0.0f);
    SetPos(TargetButton, -0.377f, kRowLoweredY, 0.0f);
    MinimizedFrame->SetProperty(kWinPropHide, 0, 0);
    SetPos(MinimizedFrame, -0.4105f, kRowLoweredY, -1.0f);

    WinControl* const Row[8] = {NextEnemyButton, ScanButton, TractorButton,
                                RequestGroupButton, CommButton, ContactListButton,
                                TargetButton, MinimizedFrame};
    float const RowX[8] = {-0.479f, -0.411f, -0.411f, -0.445f,
                           -0.445f, -0.343f, -0.377f, -0.4105f};
    float const RowZ[8] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f};
    for (int Index = 0; Index < 8; ++Index)
    {
        float const FromPos[3] = {RowX[Index], kRowLoweredY, RowZ[Index]};
        float const ToPos[3] = {RowX[Index], kRowY, RowZ[Index]};
        ButtonMover::AddButton(Row[Index], FromPos, ToPos);
    }
}

// =============================================================================================
// Event::Handler face
// =============================================================================================

bool HudTarget::OnEvent(unsigned MsgId)
{
    // FUN_004e2970 (this = the handler face): only with a player ship in space.
    if (HudIsMousePickBlocked())
    {
        return false;
    }
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    if (Player == nullptr)
    {
        return false;
    }
    FClientGameobj* const PlayerGameobj =
        static_cast<FClientGameobj*>(static_cast<IObjInspectImpl*>(Player));
    if (PlayerGameobj->ManagerKind != EClientGameobjKind::Ship)
    {
        return false;
    }
    CObject* const PlayerObject = CObjectOfObjRw(Player);
    if (PlayerObject == nullptr || !IsClass(PlayerObject, CObject::ClassShip))
    {
        return false;
    }
    CShip* const PlayerShipObject = static_cast<CShip*>(PlayerObject);
    // The player's gameobj; FUN_005449f0 / 00544b90 / 00544c30 are called on it directly.
    FClientShipGameobj* const PlayerShip = static_cast<FClientShipGameobj*>(PlayerGameobj);

    switch (MsgId)
    {
    case USER_FIRE_FORWARD:
        static_cast<IObjAffect*>(Player)->fire_weapons(nullptr, 0, nullptr,
                                                  static_cast<int>(GetPlayerConnectionId()));
        return true;

    case USER_PREV_ENEMY:
    case USER_NEXT_ENEMY:
    {
        IObjRW* const Enemy = (MsgId == USER_PREV_ENEMY) ? PlayerShipObject->previous_enemy()
                                              : PlayerShipObject->next_enemy();
        if (Enemy != nullptr)
        {
            PlayerShip->SetTargetWithGuns(Enemy, SUBOBJ_ID_NONE);
        }
        return true;
    }

    case USER_CLOSEST_ENEMY:
    {
        IObjRW* const Next =
            PlayerShip->PickNextTarget(static_cast<int>(GetPlayerConnectionId()));
        if (Next != nullptr)
        {
            PlayerShip->SetTargetWithGuns(Next, SUBOBJ_ID_NONE);
        }
        return true;
    }

    case USER_TRACTOR_BEAM:
    {
        // The result is not read. Retail: no null test of the ship (null when the watched
        // object is not a ship; 0x004e2aa5).
        CShip* const Ship = HudShipOfObjRw(WatchedObjRw(HUDItem::Target));
        PlayerShip->RequestTractor(Ship->get_target());
        return true;
    }

    case USER_SCAN_CARGO:
    {
        LabelRefreshTimer = kOne;
        Win3D* const Scanner = Win3DFindByName("NN_Scanner");
        if (Scanner == nullptr)
        {
            OpenScanner();
            return true;
        }
        Scanner->ProcessMessage(kWinMsgHide, 0, 0);
        return true;
    }

    case 0xcc:
        LabelRefreshTimer = kOne;
        if (bExpanded == 0)
        {
            return false;
        }
        if (Mode == kTargetPanelTarget)
        {
            UpdateLabels();
        }
        LabelRefreshTimer = kOne;
        return false;

    case USER_NEXT_SUBTARGET:
        SelectNextSubTarget();
        UpdateLabels();
        SubtargetName->SetProperty(kTextPropTypewriter, 1, 0x14);
        return true;

    case USER_PREV_SUBTARGET:
        SelectPreviousSubTarget();
        UpdateLabels();
        SubtargetName->SetProperty(kTextPropTypewriter, 1, 0x14);
        return true;

    case USER_CONTACT_LIST:
        Mode = kTargetPanelContactList;
        if (bExpanded == 0)
        {
            ToggleExpanded();
        }
        return true;

    case USER_FORMATION_LIST:
        Mode = kTargetPanelFormation;
        if (bExpanded == 0)
        {
            ToggleExpanded();
        }
        return true;

    case USER_MINIMIZE_HUD:
    case USER_TARGET:
        // Retail's jump table sends these into the ToggleExpanded calls of 0x54 / 0x59.
        ToggleExpanded();
        return true;

    case USER_SWITCH_TO_TARGET:
        Mode = kTargetPanelTarget;
        if (bExpanded == 0)
        {
            ToggleExpanded();
        }
        LabelRefreshTimer = kOne;
        return true;

    case USER_GROUP_INVITE:
    {
        // Retail: no null test of the ship (0x004e2c6d).
        CShip* const Ship = HudShipOfObjRw(WatchedObjRw(HUDItem::Target));
        IObjRW* const ShipTarget = Ship->get_target();
        if (ShipTarget != nullptr)
        {
            CObject const* const TargetObject = ShipTarget->cobject();
            if (TargetObject != nullptr && IsClass(TargetObject, CObject::ClassShip) &&
                static_cast<CSimple const*>(TargetObject)->OwnerPlayer != 0)
            {
                // The group invite (opcode 0) to the target's owner.
                unsigned const Owner =
                    static_cast<unsigned>(static_cast<CSimple const*>(TargetObject)->OwnerPlayer);
                NnGroupSendWireMessage(0, CHAT_ID(Owner & 0xffffu));
            }
        }
        return true;
    }

    case USER_TRADE_REQUEST:
    {
        if (G_bTradeRequested != 0)
        {
            return true;
        }
        CObject* const ViewObjectPtr = CObjectOfObjRw(WatchedObjRw(ViewObject));
        if (ViewObjectPtr == nullptr || !IsClass(ViewObjectPtr, 3))
        {
            return true;
        }
        CSimple* const Simple = static_cast<CSimple*>(ViewObjectPtr);
        if (Simple->OwnerPlayer == 0)
        {
            return true;
        }
        G_Server->RequestTrade(GetPlayerConnectionId(), Simple->OwnerPlayer);
        G_bTradeRequested = 1;
        // The target owner's name RDL in the chat table.
        RenderDisplayList* const OwnerName =
            ChatFindPlayerName(Simple->GetOwnerPlayer() & 0xffffu);
        if (OwnerName == nullptr)
        {
            return true;
        }
        // A chat entry (FUN_00468a80: the store's next free one, or the scratch DAT_0066d440)
        // gets StyleList then the name, and is committed (FUN_00468c00).
        FChatMessageEntry* const Entry = ChatAcquireEntry();
        ChatAppendRdlCopy(Entry->Body, StyleList);
        ChatAppendRdlCopy(Entry->Body, *OwnerName);
        ChatCommitEntries();
        return true;
    }

    case USER_CLEAR_TARGET:
        if (WatchedObjRw(HUDItem::Target) != nullptr)
        {
            ClearTargetOf(WatchedObjRw(HUDItem::Target));
        }
        bClearPlayerTarget = 0;
        return false;

    default:
        return false;
    }
}

// =============================================================================================
// Public helpers
// =============================================================================================

void HudTarget::SetMode(int NewMode)
{
    // FUN_004e5460.
    Mode = NewMode;
}

int HudTarget::GetMode(bool bRaw)
{
    // FUN_004e5470.
    if (bRaw)
    {
        return Mode;
    }
    if (bExpanded != 0 && !ButtonMover::IsActive() && !ButtonScaler::IsActive())
    {
        return Mode;
    }
    return kTargetPanelNotShown;
}

void HudTarget::OpenScanner()
{
    // FUN_004e54c0: scan the player's target when it is a ship-like object (get_type & 0x7f0000).
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    if (Player == nullptr)
    {
        return;
    }
    IObjRW* ScanTarget = nullptr;
    Player->get_target(ScanTarget);
    if (ScanTarget == nullptr)
    {
        return;
    }
    unsigned Type = 0;
    if (ScanTarget->get_type(Type) != 0)
    {
        return;
    }
    if ((Type & 0x7f0000) == 0)
    {
        return;
    }
    NnVoiceEnqueue(G_ScanningCargoVoiceId, 0);

    EquipDescVector Cargo;
    if (Player->scan_cargo(ScanTarget, Cargo) != 0)
    {
        return;
    }
    unsigned ArchId = 0;
    CObject const* const TargetObject = ScanTarget->cobject();
    if (TargetObject != nullptr && (static_cast<unsigned>(TargetObject->ObjectClass) & 1) != 0)
    {
        ArchId = TargetObject->get_archetype()->Nickname.Id;
    }
    // The list dies right after the call.
    EquipDescList const CargoList(Cargo);
    OpenNnScanner(&CargoList, ArchId);
}

// =============================================================================================
// Private helpers
// =============================================================================================

void HudTarget::UpdateButtons(bool bFull)
{
    // FUN_004e22b0: once per second of capped frame time.
    LabelRefreshTimer = static_cast<float>(GetClientFrameTimeCapped() +
                                           static_cast<double>(LabelRefreshTimer));
    if (!(LabelRefreshTimer > kOne))
    {
        return;
    }
    LabelRefreshTimer = LabelRefreshTimer - kOne;

    if (bExpanded != 0 && WatchedObjRw(ViewObject) != nullptr && bFull)
    {
        if (WatchedObjRw(HUDItem::Target) != nullptr && WatchedObjRw(ViewObject) != nullptr &&
            ShipName != nullptr && SubtargetName != nullptr)
        {
            unsigned long Colour = 0;
            HudReputationColor(&Colour, WatchedObjRw(HUDItem::Target), WatchedObjRw(ViewObject));
            ShipName->SetProperty(kWinPropColour, reinterpret_cast<uintptr_t>(&Colour), 1);
            SubtargetName->SetProperty(kWinPropColour, reinterpret_cast<uintptr_t>(&Colour), 1);
        }
        UpdateLabels();
    }

    CObject* const ViewCObject = CObjectOfObjRw(WatchedObjRw(ViewObject));
    CShip* const ViewShip =
        (ViewCObject != nullptr && IsClass(ViewCObject, CObject::ClassShip)) ? static_cast<CShip*>(ViewCObject)
                                                                : nullptr;

    // The sub-target arrows: a view with live sub-targets, and a ship only with type bit
    // 0x200000.
    bool bSubTargets = WatchedObjRw(ViewObject) != nullptr && SubTargetCount > 0;
    if (ViewShip != nullptr && (ViewShip->TypeBits & OBJ_CAPITAL) == 0)
    {
        bSubTargets = false;
    }
    uintptr_t const ArrowHide = (bSubTargets && bExpanded != 0) ? 0 : 1;
    if (PreviousButton != nullptr && bFull)
    {
        PreviousButton->SetProperty(kWinPropHide, ArrowHide, 0);
    }
    if (NextButton != nullptr)
    {
        if (!bFull)
        {
            goto RankDone;
        }
        NextButton->SetProperty(kWinPropHide, ArrowHide, 0);
    }
    if (!bFull)
    {
        goto RankDone;
    }

    // The rank line: only for a ship view, while open and not sliding.
    if (ViewShip != nullptr)
    {
        if (bExpanded != 0 && !ButtonMover::IsActive())
        {
            SetFlagBits(RankText);
            Rank = static_cast<float>(WatchedObjRw(ViewObject)->get_toughness());
            // Out of range keeps the rank shown.
            if (Rank < 0.0f || Rank > kRankMax)
            {
                Rank = ShownRank;
            }
            if (!(Rank == ShownRank))
            {
                ShownRank = Rank;
                GetIdsStringWWithRawFallback(0x619, G_RankLine, G_NnTextScratchCapacity);
                swprintf(G_RankSuffix, 0x1000, L" %d", static_cast<int>(Rank));
                wcscat(G_RankLine, G_RankSuffix);
                RankText->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(G_RankLine), 0);
            }
        }
    }
    else
    {
        ClearFlagBits(RankText);
    }

RankDone:
    {
        // A loot-like view (class 0x803) takes the tractor button, anything else in
        // cargo scanner range the scan button.
        CObject* const LootObject = CObjectOfObjRw(WatchedObjRw(ViewObject));
        bool const bLoot = LootObject != nullptr && IsClass(LootObject, CObject::ClassLoot);
        float Distance = kScanDefaultDistance;
        IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
        if (!bLoot && ViewShip != nullptr && Player != nullptr)
        {
            Distance = PositionDistance(WatchedObjRw(ViewObject)->get_position(),
                                        Player->get_position());
        }

        if (ScanButton != nullptr)
        {
            unsigned char bHideScan = 1;
            if (!bLoot && ViewShip != nullptr && Player != nullptr)
            {
                CObject* const PlayerObject = CObjectOfObjRw(Player);
                if (PlayerObject != nullptr && IsClass(PlayerObject, CObject::ClassShip))
                {
                    CEqObj* const PlayerEq = static_cast<CEqObj*>(PlayerObject);
                    CEScanner const* const Scanner =
                        CEScanner::cast(PlayerEq->EquipManager.FindFirst(EquipTypeScanner));
                    // A NaN distance keeps it hidden.
                    if (Scanner != nullptr &&
                        Distance <= Scanner->ScannerArch()->CargoScanRange)
                    {
                        bHideScan = 0;
                    }
                }
            }
            ScanButton->SetProperty(kWinPropHide, bHideScan, 0);
            Win3D* const ScannerWindow = Win3DFindByName("NN_Scanner");
            bScannerOpen = (ScannerWindow != nullptr) ? 1 : 0;
            ScanButton->SetProperty(0xc, bScannerOpen, 0);
            if (ScannerWindow != nullptr && bHideScan != 0)
            {
                ScannerWindow->ProcessMessage(kWinMsgHide, 0, 0);
            }
        }

        if (TractorButton != nullptr)
        {
            TractorButton->SetProperty(kWinPropHide, bLoot ? 0 : 1, 0);
        }

        if (CommButton != nullptr)
        {
            // A player ship: request group, else hail; no ship: neither.
            bool bPlayerShip = false;
            uintptr_t CommHide = 1;
            if (ViewShip != nullptr)
            {
                if (ViewShip->bIsPlayer != 0)
                {
                    bPlayerShip = true;
                }
                else
                {
                    CommHide = 0;
                }
            }
            CommButton->SetProperty(kWinPropHide, CommHide, 0);
            RequestGroupButton->SetProperty(kWinPropHide, bPlayerShip ? 0 : 1, 0);
        }
    }
    UpdateTradeButton();
}

void HudTarget::UpdateTradeButton()
{
    // FUN_004e27e0.
    CObject* const ViewCObject = CObjectOfObjRw(WatchedObjRw(ViewObject));
    CSimple* const ViewShip = (ViewCObject != nullptr && IsClass(ViewCObject, CObject::ClassShip))
                                  ? static_cast<CSimple*>(ViewCObject)
                                  : nullptr;
    float Distance = kScanDefaultDistance;
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    bool bWithdraw = true;
    if (ViewShip != nullptr && Player != nullptr)
    {
        Distance = PositionDistance(WatchedObjRw(ViewObject)->get_position(),
                                    Player->get_position());
        // Within range (or NaN) keeps the request.
        bWithdraw = Distance > kTradeRange;
    }
    if (bWithdraw && (G_bTradeRequested != 0 || NnTradeIsOpen()))
    {
        WithdrawTrade();
    }

    if (TradeButton != nullptr)
    {
        // NnTradeIsOpen reads DAT_006724b4, the open NN_Trade window.
        bool bEnable = false;
        if (!NnTradeIsOpen() && Distance <= kTradeRange && ViewShip != nullptr &&
            ViewShip->OwnerPlayer != 0 && ViewShip->OwnerPlayer != GetPlayerConnectionId())
        {
            bEnable = true;
        }
        TradeButton->SetProperty(kWinPropHide, bEnable ? 0 : 1, 0);
    }
}

IObjRW* HudTarget::SyncPlayerTarget()
{
    // FUN_004e3bd0 (this = the HUDItem face).
    IObjRW* const PlayerRw = WatchedObjRw(HUDItem::Target);
    if (PlayerRw == nullptr)
    {
        return nullptr;
    }
    // Retail: no null test of the ship (FUN_00543320's result goes straight to VerifyTarget).
    CShip* const Ship = HudShipOfObjRw(PlayerRw);
    Ship->VerifyTarget();
    IObjRW* const View = WatchedObjRw(ViewObject);
    if (View != nullptr)
    {
        // The view's client object gone, or its gameobj slot +0x158 (byte +0x44 bit 0) set:
        // clear the player's target.
        FClientGameobj* const Gameobj = ClientObjDb_FindObject(View->get_id(), 0);
        if (Gameobj == nullptr || GameobjFlag44(Gameobj))
        {
            ClearTargetOf(WatchedObjRw(HUDItem::Target));
        }
    }
    return Ship->get_target();
}

void HudTarget::SelectNextSubTarget()
{
    // FUN_004e4ba0.
    if (SubTargetCount != RebuildSubTargets(&SubTargets))
    {
        SubTargetCount = RebuildSubTargets(&SubTargets);
        SubTargetCursor = 0;
    }
    if (SubTargetCount <= 0 || WatchedObjRw(ViewObject) == nullptr)
    {
        return;
    }
    ++SubTargetCursor;
    if (SubTargetCursor >= SubTargetCount)
    {
        SubTargetCursor = 0;
    }
    int const Wanted = SubTargetCursor;
    RebuildSubTargets(&SubTargets); // rebuilt once more before the walk, as retail
    unsigned short Key = SUBOBJ_ID_NONE;
    int Index = 0;
    for (std::map<unsigned short, long>::const_iterator It = SubTargets.begin();
         It != SubTargets.end(); ++It)
    {
        if (Index == Wanted)
        {
            Key = It->first;
            break;
        }
        ++Index;
    }
    if (Key == SUBOBJ_ID_NONE)
    {
        return;
    }
    FSpaceCommand Command = {};
    Command.Byte26 = 1;
    Command.Id = USER_SET_TARGET;
    Command.ObjectId = WatchedObjRw(ViewObject)->get_id();
    Command.SubObjectId = Key;
    SpaceSendCommand(&Command);
}

void HudTarget::SelectPreviousSubTarget()
{
    // FUN_004e4cc0.
    if (SubTargetCount != RebuildSubTargets(&SubTargets))
    {
        SubTargetCount = RebuildSubTargets(&SubTargets);
        SubTargetCursor = 0;
    }
    if (SubTargetCount <= 0 || WatchedObjRw(ViewObject) == nullptr)
    {
        return;
    }
    --SubTargetCursor;
    if (SubTargetCursor < 0)
    {
        SubTargetCursor = SubTargetCount - 1;
    }
    int const Wanted = SubTargetCursor;
    RebuildSubTargets(&SubTargets);
    unsigned short Key = SUBOBJ_ID_NONE;
    int Index = 0;
    for (std::map<unsigned short, long>::const_iterator It = SubTargets.begin();
         It != SubTargets.end(); ++It)
    {
        if (Index == Wanted)
        {
            Key = It->first;
            break;
        }
        ++Index;
    }
    if (Key == SUBOBJ_ID_NONE)
    {
        return;
    }
    FSpaceCommand Command = {};
    Command.Byte26 = 1;
    Command.Id = USER_SET_TARGET;
    Command.ObjectId = WatchedObjRw(ViewObject)->get_id();
    Command.SubObjectId = Key;
    SpaceSendCommand(&Command);
}

int HudTarget::RebuildSubTargets(std::map<unsigned short, long>* Map)
{
    // FUN_004e4dd0; retail inlines CArchGroup::GetID.
    Map->clear();
    CObject* const Object = CObjectOfObjRw(WatchedObjRw(ViewObject));
    if (Object != nullptr && IsClass(Object, CObject::ClassEqObj))
    {
        CEqObj* const EqObj = static_cast<CEqObj*>(Object);
        CArchGrpTraverser GroupWalk;
        CArchGroup* Group = EqObj->ArchGroups.Traverse(GroupWalk);
        while (Group != nullptr)
        {
            (*Map)[Group->GetID()] = Group->GetRootIndex();
            Group = EqObj->ArchGroups.Traverse(GroupWalk);
        }

        CEquipTraverser EquipWalk(0x1ee0);
        CEquip* Equip = EqObj->EquipManager.Traverse(EquipWalk);
        while (Equip != nullptr)
        {
            CAttachedEquip* const Attached = static_cast<CAttachedEquip*>(Equip);
            if (Attached->IsConnected() && Equip->Arch->IdsName != 0)
            {
                (*Map)[Equip->Id] = Attached->GetRootIndex();
            }
            Equip = EqObj->EquipManager.Traverse(EquipWalk);
        }
    }
    return static_cast<int>(Map->size());
}

void HudTarget::GetSubTargetName(wchar_t* Out)
{
    // FUN_004e49c0.
    Out[0] = 0;
    if (SubTargetFlash.SubId == SUBOBJ_ID_NONE)
    {
        return;
    }
    CObject* const Object = CObjectOfObjRw(WatchedObjRw(ViewObject));
    if (Object == nullptr || !IsClass(Object, CObject::ClassEqObj))
    {
        return;
    }
    CEqObj* const EqObj = static_cast<CEqObj*>(Object);
    wchar_t Name[0x80];
    unsigned Length = 0;
    CArchGroup* const Group = EqObj->ArchGroups.FindByID(SubTargetFlash.SubId);
    if (Group != nullptr)
    {
        Length = GetIdsStringWWithRawFallback(
            HudCollisionGroupTypeIds(Group->GetCollisionGroupType()), Name, 0x80);
    }
    else
    {
        CAttachedEquip* const Attached =
            CAttachedEquip::cast(EqObj->EquipManager.FindByID(SubTargetFlash.SubId));
        if (Attached == nullptr || !Attached->IsConnected())
        {
            return;
        }
        Length = GetIdsStringWWithRawFallback(
            static_cast<unsigned>(Attached->Arch->IdsName), Name, 0x80);
    }
    if (Length == 0)
    {
        Name[0] = 0;
    }
    wcscpy(Out, Name);
}

void HudTarget::NextHardpointScreenPos(float* OutScreen)
{
    // FUN_004e4f70: the cursor wraps before and after the step.
    if (HardpointCursor >= HardpointCount)
    {
        HardpointCursor = 0;
    }
    int const Index = HardpointCursor;
    HardpointCursor = Index + 1;
    if (HardpointCursor >= HardpointCount)
    {
        HardpointCursor = 0;
    }
    char const* const Name = HardpointNames[static_cast<size_t>(Index)].c_str();

    IObjRW* const View = WatchedObjRw(ViewObject);
    long Instance = -1;
    HardpointInfo Info;
    if (!FindHardpoint_OS(View->get_index(), Name, Instance, Info))
    {
        return;
    }
    if (Instance == -1)
    {
        return;
    }
    Transform const* const Xform = DALib::Engine->GetInstanceTransform(Instance);
    if (G_HudTargetHardpointMath == nullptr)
    {
        G_HudTargetHardpointMath = Acquire3DMathEngine();
    }
    Vector Local;
    Local.x = Info.Position.x;
    Local.y = Info.Position.y;
    Local.z = Info.Position.z;
    Vector World;
    G_HudTargetHardpointMath->TransformXform(World, *Xform, Local);

    ICamera* const Camera = AsCamera(&ViewCamera);
    float const WorldPoint[3] = {World.x, World.y, World.z};
    if (Camera->ProjectVec((Vector*)OutScreen, (const Vector*)WorldPoint) == 0)
    {
        // Off screen: the camera's screen centre (+0x68 / +0x6c).
        OutScreen[0] = ViewCamera.ScreenCenterX;
        OutScreen[1] = ViewCamera.ScreenCenterY;
        return;
    }
    int const* const Viewport = Camera->GetViewport();
    OutScreen[0] = OutScreen[0] - static_cast<float>(Viewport[0]);
    OutScreen[1] = OutScreen[1] - static_cast<float>(Viewport[1]);
}

void HudTarget::DrawScanSweep()
{
    // FUN_004e50d0: while NN_Scanner is up, a red crosshair sweeps from hardpoint to hardpoint
    // (0.3 s each) and leaves a fading trail of its last ten positions.
    if (bScannerOpen == 0 || HardpointCount == 0)
    {
        bSweepRestart = 1;
        return;
    }
    if (bSweepRestart != 0)
    {
        bSweepRestart = 0;
        NextHardpointScreenPos(SweepFrom);
        NextHardpointScreenPos(SweepTo);
        SweepTime = 0.0f;
        SweepTrailCount = 0;
    }

    SweepTime = static_cast<float>(GetClientFrameTimeCapped() + static_cast<double>(SweepTime));
    // The negated test is retail's: a NaN SweepTime never leaves the loop.
    while (!(SweepTime < kSweepStep))
    {
        SweepTime = SweepTime - kSweepStep;
        SweepFrom[0] = SweepTo[0];
        SweepFrom[1] = SweepTo[1];
        SweepFrom[2] = SweepTo[2];
        NextHardpointScreenPos(SweepTo);
    }
    float const T = SweepTime * kSweepRate;
    // FUN_00422b80.
    float const DeltaX = SweepTo[0] - SweepFrom[0];
    float const DeltaY = SweepTo[1] - SweepFrom[1];
    float const PointX = DeltaX * T + SweepFrom[0];
    float const PointY = DeltaY * T + SweepFrom[1];
    float const X = static_cast<float>(floor(static_cast<double>(PointX) + kSweepRound));
    float const Y = static_cast<float>(floor(static_cast<double>(PointY) + kSweepRound));

    Win3DDisable(0xde1);
    Win3DDisable(0xb71);
    Win3DSetOrtho2DForCamera(&ViewCamera);
    Win3DEnable(0xbe2);
    Win3DSetBlendFunc(5, 6);
    unsigned char const Red[4] = {0xff, 0, 0, 0x80};
    Win3DColor4ubv(Red);
    Win3DBegin(2);

    int const* const Viewport = ViewCamera.Viewport;
    float const Height = static_cast<float>(Viewport[3] - Viewport[1] + 1);
    float const Width = static_cast<float>(Viewport[2] - Viewport[0] + 1);
    Win3DVertex3f(X, 0.0f, 0.0f);
    Win3DVertex3f(X, Height, 0.0f);
    Win3DVertex3f(0.0f, Y, 0.0f);
    Win3DVertex3f(Width, Y, 0.0f);

    // The trail, newest first, alpha 128 - step, 128 - 2 step, ... (step = 128 / (count + 1)).
    int const Step = 0x80 / (SweepTrailCount + 1);
    float const StepF = static_cast<float>(Step);
    float Alpha = 128.0f;
    for (int Index = SweepTrailCount - 1; Index >= 0; --Index)
    {
        Alpha = Alpha - StepF;
        unsigned char const Fade[4] = {0xff, 0, 0, static_cast<unsigned char>(static_cast<int>(Alpha))};
        Win3DColor4ubv(Fade);
        if (Index < 10)
        {
            Win3DVertex3f(SweepTrail[Index][0], 0.0f, 0.0f);
            Win3DVertex3f(SweepTrail[Index][0], Height, 0.0f);
            Win3DVertex3f(0.0f, SweepTrail[Index][1], 0.0f);
            Win3DVertex3f(Width, SweepTrail[Index][1], 0.0f);
        }
    }
    Win3DEnd();

    // Remember this frame's point: appended, or the oldest dropped when ten are held. Retail
    // stores an uninitialised stack value as the third float; nothing reads it back.
    if (SweepTrailCount < 10)
    {
        SweepTrail[SweepTrailCount][0] = X;
        SweepTrail[SweepTrailCount][1] = Y;
        SweepTrail[SweepTrailCount][2] = 0.0f;
        ++SweepTrailCount;
        return;
    }
    memmove(&SweepTrail[0][0], &SweepTrail[1][0], sizeof(float) * 3 * 9);
    SweepTrail[9][0] = X;
    SweepTrail[9][1] = Y;
    SweepTrail[9][2] = 0.0f;
}
