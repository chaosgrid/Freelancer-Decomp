// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Space/HUD/hud_targetable_objects.h"

#include "NeuroNet/Economy/best_path.h"        // BestPathGetWaypointCount / BestPathGetWaypoint / resolvers
#include "Interface/cargo_bar_control.h" // ToolTipHoverInit
#include "common_archetype.h" // Archetype::Munition
#include "dalib_net.h"        // CDPClient::GetPingDelay
#include "Core/game_options.h"     // GetGameOption (FUN_005792b0)
#include "Interface/menu_ui_init.h"     // PlayOrRewindUiSoundSlot (FUN_005645c0)
#include "NeuroNet/nn_messaging.h"     // NnVoiceEnqueue (FUN_00489e80)
#include "NeuroNet/nn_tooltip.h"       // NnToolTipWasFedThisFrame (FUN_004be2d0)
#include "Interface/tooltip_hover.h"    // FToolTipHoverInfo, NnToolTipFeedHover (FUN_004be370)
#include "Interface/Pages/character_select_equip.h" // GetPlayerStarSystemIdRetail (FUN_004c4830)
#include "Space/client_gameobj.h"   // FClientGameobj::IsDead (player slot +0x158)
#include "Common/Graphics/client_mesh_load.h"
#include "Space/client_objdb.h"     // GetClientObjDb (DAT_00678a2c)
#include "common_id.h"        // CreateID
#include "common_universe.h"  // Universe::ISystem
#include "dacom.h"            // FDUMP
#include "DALib.h"            // DALib::Pipeline
#include "Common/engine_light.h"     // EngineLightClearRegisteredList (FUN_004101e0)
#include "common_cequip.h"
#include "common_csys.h"      // Acquire3DMathEngine
#include "common_iface.h"     // IObjRW, IObjInspect, ObjRwFromWatchable
#include "common_misc.h"      // ICamera, SUBOBJ_ID_NONE, SUBOBJ_ID_ROOT
#include "common_physys.h"    // PhySys::FindRayCollisions, PhySys::RayHit
#include "common_reputation.h" // Reputation::Vibe::GetAffiliation, Reputation::get_short_name
#include "Common/Fonts/font_imp.h"         // FindMenuFontImp (FUN_004177b0)
#include "Core/globals.h"          // G_RenderSurfaceWidth / G_RenderSurfaceHeight
#include "Core/widescreen.h"   // port-only (FREELANCER_WIDESCREEN_SUPPORT)
#include "Space/HUD/hud_comm.h"         // HudCommIsWatchedObjectId (FUN_004cc880)
#include "Space/HUD/hud_command_dispatch.h" // HudCommandRecord, FSpaceCommand, SpaceSendCommand
#include "Space/HUD/hud_status_core.h"  // HudSampleShieldHitPoints (FUN_004d4b50)
#include "Space/HUD/hud_target.h"       // G_HudObjectsMath, HudIsMousePickBlocked
#include "Space/HUD/hud_waypoint.h"     // HudWaypointSelectPlayerWaypoint / HudWaypointSelectBestPath
#include "Common/Fonts/ids_strings.h"      // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "Common/input_manager.h"    // GetClientSoftwareCursor (DAT_00616840 / DAT_00616844)
#include "Common/Audio/menu_sound_arch.h"  // GetClientSoundListenerObject (FUN_0040f130)
#include "NeuroNet/nn_montage_draw.h"  // FindButtonMontageTile (FUN_0055a360)
#include "NeuroNet/nn_text_control.h"  // NnTextControl (+0x334 Font::String, FUN_0058a5d0 / FUN_0058a5e0)
#include "NeuroNet/nn_ui_pulse.h"      // G_NnPaletteRed
#include "Space/player_behavior.h"  // GetPlayerObjRW, IsMouseFlightHoldThresholdReached, IsMouseFlightLocked
#include "Space/player_spawn.h"     // GetPlayerWaypointCountRetail / GetPlayerWaypointRetail
#include "Space/Cameras/space_camera.h"     // FSpaceCamera, AsCamera
#include "Space/space_letterbox.h"  // IsLetterboxVisible (FUN_0041a3e0)
#include "Space/star_sphere.h"      // Win3DBindModelview (FUN_00422690)
#include "Space/star_sys.h"         // StarSysGetActiveSystemId (FUN_00554e90), GetAttitudeClass
#include "Interface/win3d.h"            // Win3DGetHover
#include "Interface/win3d_immediate.h"  // Win3DEnable / Win3DDisable / Win3DSetBlendFunc
#include "Interface/win3d_render.h"     // Win3DApplyCamera (FUN_00422950)
#include "Interface/win3d_renderables.h" // Win3DImmedDrawLine (FUN_005a2940)
#include "x86math.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <limits>
#include <string>
#include <tuple>
#include <utility>

namespace
{
    // FUN_004ecfd0 constants.
    constexpr float kProjectScaleMul = 10.0f;       // DAT_005d49b0
    constexpr float kProjectNumerFar = 3000.0f;     // DAT_005d95a8
    constexpr float kProjectScaleGate = 100.0f;     // DAT_005ca230
    constexpr double kProjectNumerNearMul = 10.0;   // DAT_005c9ce8
    constexpr double kProjectNumerNearAdd = 2000.0; // DAT_005d95a0
    constexpr float kProjectRadiusMin = 10.0f;      // DAT_005d8ee8
    constexpr float kProjectRadiusMax = 100.0f;     // DAT_005d8eec

    // The inlined `+0xc (the CBase face) -> +4 (CBase::Object)` chain on an IObjRW / gameobj.
    CObject* CObjectOfObjRW(IObjRW* ObjRW)
    {
        CBase const* const Base = static_cast<IObjInspectImpl*>(ObjRW);
        return Base->cobject();
    }

    CObject* CObjectOfGameobj(FClientGameobj* Gameobj)
    {
        CBase const* const Base = static_cast<IObjInspectImpl*>(Gameobj);
        return Base->cobject();
    }

    // The ctor's IDS fetch: FUN_004347e0 into a 0x80-wchar stack buffer; a zero result leaves
    // the buffer as the empty string.
    void LoadLabelIds(unsigned Ids, wchar_t* Buffer)
    {
        if (GetIdsStringWWithRawFallback(Ids, Buffer, 0x80) == 0)
            Buffer[0] = L'\0';
    }

    // The ctor's alert block: FUN_005a0db0 with a 0.018 square mouse box; the lag alert has no
    // IDS (Ids 0).
    WinControl* CreateAlert(WinControl* Parent, char const* Name, char const* MeshPath,
                            Vector const* Position, unsigned Ids)
    {
        Vector const Zero = {0.0f, 0.0f, 0.0f};
        Vector const HalfExtents = {0.018f, 0.018f, 0.0f};
        WinControl* const Alert = Parent->CreateStaticControl(
            Name, MeshPath, Position, nullptr, &HalfExtents, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, &Zero);
        uintptr_t const Red = reinterpret_cast<uintptr_t>(&G_NnPaletteRed);
        Alert->SetProperty(kStaticPropColours, Red, Red);
        if (Ids != 0)
            Alert->SetProperty(kWinPropToolTip, Ids, 0);
        Alert->SetProperty(kWinPropHide, 1, 0);
        return Alert;
    }
}

HudTargetableObjects* G_HudTargetableObjects = nullptr;   // DAT_00674b70

HudTargetableObjects::HudTargetableObjects(INI_Reader& /*Reader*/)
{
    // FUN_004e8fe0.
    std::memset(&WaypointRecord, 0, sizeof(WaypointRecord));
    WaypointRecord.bValid = 1;
    std::memset(&BestPathRecord, 0, sizeof(BestPathRecord));
    BestPathRecord.bValid = 1;
    // Port-only: retail's ctor leaves +0x3e8 / +0x3ec unwritten; FUN_004ed180 sets both before
    // any read.
    HotIndex = 0;
    HotScreenRadius = 0.0f;
    RayHit.Object = nullptr;
    RayHit.Instance = -1;
    RayHit.Point.x = 0.0f;
    RayHit.Point.y = 0.0f;
    RayHit.Point.z = 0.0f;
    RayHit.Distance = 0.0f;

    // The 19 holders (FUN_00420d40).
    FClientMeshHolder* const Holders[19] = {
        &TargetArrow, &TargetArrowHat, &TargetArrowPlayer, &TargetHealth, &TargetShields,
        &TargetArrowQuotes, &NonselectedArrow, &NonselectedArrowHat, &NonselectedArrowQuotes,
        &NonselectedPlayerArrow, &LootArrow, &Reticle, &ReticleQuotes, &ReticleArrows,
        &ReticleHealth, &ReticleShields, &WaypointReticle, &SelectedWaypoint,
        &NonselectedWaypoint,
    };
    for (int Index = 0; Index < 19; ++Index)
        ClientMeshHolderConstruct(Holders[Index]);

    // Create's body inlined; Win3D::Create(0, 0) is a direct (non-virtual) call.
    G_HudTargetableObjects = this;
    HUDItem::Name = "Targetable_Objects";
    SetPosition(&Position);
    if (Win3D::Create(nullptr, nullptr))
        RegisterEvent(USER_BUTTON0);
    Win3D::SetName("Targetable_Objects");
    ClassName = "Targetable_Objects";

    WaypointRecord.bIsWaypoint = 1;
    BestPathRecord.bIsBestPath = 1;
    Scanner = nullptr;
    ScanEntries = nullptr;
    RayObject = nullptr;
    WaypointRecord.TypeFlags = OBJ_WAYPOINT;
    BestPathRecord.TypeFlags = OBJ_WAYPOINT;
    MissileBlinkTimer = 0.1f;         // DAT_005d8ef0
    MissileBlinkPeriod = 0.1f;
    bMissileBlinkOn = 1;
    TrailBlinkTimer = 0.5f;           // DAT_005d8ef4
    TrailBlinkPeriod = 0.5f;
    bTrailBlinkOn = 1;
    RadiationBlinkTimer = 0.7f;       // DAT_005d8efc
    bRadiationBlinkOn = 1;
    LagTimer = 1.0f;                  // DAT_005d8f00
    bLagBlinkOn = 0;
    PickSubObjId = SUBOBJ_ID_NONE;
    PickId = 0;

    // The range read-out under the reticle.
    Vector const RangePos = {0.003f, 0.0f, 0.0f};
    ReticleRangeText = CreateTextControl("ReticleRangeText", "HudSmall", &RangePos, nullptr,
                                         0.171f /* DAT_005d8f0c */, 0.03f,
                                         1.0f, 0x453, 1, 0, 0);
    ReticleRangeText->SetProperty(kWinPropHide, 1, 0);
    unsigned char const White[4] = {0xff, 0xff, 0xff, 0xff};
    ReticleRangeText->SetProperty(kWinPropColour, reinterpret_cast<uintptr_t>(White), 1);

    // The four alert icons.
    Vector const MissilePos = {0.126f, 0.213f, -0.6f};
    MissileAlert = CreateAlert(this, "MissileAlert", "Interface\\hud\\HUD_missilealert.3db",
                               &MissilePos, 0x4d0);
    Vector const TrailPos = {-0.126f, 0.213f, -0.6f};
    TrailAlert = CreateAlert(this, "TrailAlert", "Interface\\hud\\HUD_trailalert.3db", &TrailPos,
                             0x616);
    Vector const RadiationPos = {0.126f, 0.177f, -0.6f};
    RadiationAlert = CreateAlert(this, "RadiationAlert", "Interface\\hud\\HUD_radiationalert.3db",
                                 &RadiationPos, 0x617);
    Vector const LagPos = {-0.126f, 0.177f, -0.6f};
    LagAlert = CreateAlert(this, "LagAlert", "Interface\\multiplayer\\MULTI_lag.3db", &LagPos, 0);

    // FUN_0059ec80, in retail's order.
    TargetArrow.LoadFromPath("interface\\hud\\hud_targetarrow.3db", 0, true);
    TargetArrowHat.LoadFromPath("interface\\hud\\hud_targetarrowhat.3db", 0, true);
    TargetArrowPlayer.LoadFromPath("interface\\hud\\hud_targetarrowplayer.3db", 0,
                                   true);
    TargetHealth.LoadFromPath("interface\\hud\\hud_targethealth.3db", 0, true);
    TargetShields.LoadFromPath("interface\\hud\\hud_targetshields.3db", 0, true);
    TargetArrowQuotes.LoadFromPath("interface\\hud\\hud_targetarrowquotes.3db", 0,
                                   true);
    NonselectedArrow.LoadFromPath("interface\\hud\\hud_nonselectedarrow.3db", 0, true);
    NonselectedArrowHat.LoadFromPath("interface\\hud\\hud_nonselectedarrowhat.3db", 0,
                                     true);
    NonselectedPlayerArrow.LoadFromPath("interface\\hud\\hud_nonselectedplayerarrow.3db", 0, true);
    LootArrow.LoadFromPath("interface\\hud\\hud_lootarrow.3db", 0, true);
    NonselectedArrowQuotes.LoadFromPath("interface\\hud\\hud_nonselectedarrowquotes.3db", 0, true);

    // The bar extents. FUN_0059f0d0 fills {maxX, minX, maxY, minY,
    // maxZ, minZ} into one stack buffer that every query below reuses; a query on a holder with
    // no instance leaves it as the previous query left it.
    FClientMeshAabb Extents;
    TargetHealth.QueryAabb(&Extents);
    HealthBarRect.Left = Extents.Values[1];
    HealthBarRect.Width = Extents.Values[0] - Extents.Values[1];
    HealthBarRect.Top = Extents.Values[4];
    HealthBarRect.Bottom = Extents.Values[5];
    TargetShields.QueryAabb(&Extents);
    ShieldBarRect.Width = Extents.Values[0] - Extents.Values[1];
    ShieldBarRect.Bottom = Extents.Values[5];
    ShieldBarRect.Left = Extents.Values[1];
    ShieldBarRect.Top = Extents.Values[4];

    Reticle.LoadFromPath("interface\\hud\\hud_reticle.3db", 0, true);
    ReticleQuotes.LoadFromPath("interface\\hud\\hud_reticle_quotes.3db", 0, true);
    ReticleArrows.LoadFromPath("interface\\hud\\hud_reticle_arrows.3db", 0, true);
    ReticleHealth.LoadFromPath("interface\\hud\\hud_reticle_health.3db", 0, true);
    ReticleShields.LoadFromPath("interface\\hud\\hud_reticle_shields.3db", 0, true);
    WaypointReticle.LoadFromPath("interface\\hud\\hud_waypointreticle.3db", 0, true);
    SelectedWaypoint.LoadFromPath("interface\\hud\\hud_selectedwaypoint.3db", 0, true);
    NonselectedWaypoint.LoadFromPath("interface\\hud\\hud_nonselectedwaypoint.3db", 0,
                                     true);

    WaypointReticle.QueryAabb(&Extents);
    WaypointReticleWidth = Extents.Values[0] - Extents.Values[1];
    WaypointReticleHeight = Extents.Values[2] - Extents.Values[3];
    Reticle.QueryAabb(&Extents);
    ReticleRect.Left = Extents.Values[1];
    ReticleRect.Top = Extents.Values[2] - Extents.Values[3];
    ReticleRect.Bottom = Extents.Values[3];
    ReticleRect.Width = Extents.Values[0] - Extents.Values[1];
    ReticleHealth.QueryAabb(&Extents);
    ReticleHealthRect.Left = Extents.Values[1];
    ReticleHealthRect.Width = Extents.Values[0] - Extents.Values[1];
    ReticleHealthRect.Top = Extents.Values[2];
    ReticleHealthRect.Bottom = Extents.Values[3];
    ReticleShields.QueryAabb(&Extents);
    ReticleShieldsRect.Width = Extents.Values[0] - Extents.Values[1];
    ReticleShieldsRect.Left = Extents.Values[1];
    ReticleShieldsRect.Bottom = Extents.Values[3];
    ReticleShieldsRect.Top = Extents.Values[2];

    // The ButtonMontage tiles, in retail's order.
    bMissileWarned = 0;
    CurrentTargetCorner = FindButtonMontageTile(std::string("CurrentTargetCorner"));
    CurrentTargetHat = FindButtonMontageTile(std::string("CurrentTargetHat"));
    WaypointTarget = FindButtonMontageTile(std::string("WaypointTarget"));
    WaypointArrow = FindButtonMontageTile(std::string("WaypointArrow"));
    LootArrowTile = FindButtonMontageTile(std::string("LootArrow"));
    NSTargetArrow = FindButtonMontageTile(std::string("NSTargetArrow"));
    NSTargetHat = FindButtonMontageTile(std::string("NSTargetHat"));
    PlayerArrow = FindButtonMontageTile(std::string("PlayerArrow"));
    CurrentTargetArrow = FindButtonMontageTile(std::string("CurrentTargetArrow"));
    CurrentTargetArrowOutline = FindButtonMontageTile(std::string("CurrentTargetArrowOutline"));
    CurrentTargetArrowHat = FindButtonMontageTile(std::string("CurrentTargetArrowHat"));
    NSArrowBackground = FindButtonMontageTile(std::string("NSArrowBackground"));
    LootArrowBackground = FindButtonMontageTile(std::string("LootArrowBackground"));
    WaypointArrowBackground = FindButtonMontageTile(std::string("WaypointArrowBackground"));
    PlayerArrowBackground = FindButtonMontageTile(std::string("PlayerArrowBackground"));

    SmallFont = FindMenuFontImp("HudSmall");
    PlayerNameFont = FindMenuFontImp("PlayerName");

    // The two waypoint captions, scaled to twice the reticle width.
    wchar_t Caption[0x80];
    LoadLabelIds(0x442, Caption);
    WaypointLabel.SetFont(SmallFont);                 // FUN_004ea670
    WaypointLabel.SetScale(WaypointReticleWidth + WaypointReticleWidth, -1.0f);
    WaypointLabel.SetText(Caption);                   // FUN_00415860
    LoadLabelIds(0x443, Caption);
    BestPathLabel.SetFont(SmallFont);                 // inlined FUN_004ea670
    BestPathLabel.SetScale(WaypointReticleWidth + WaypointReticleWidth, -1.0f);
    BestPathLabel.SetText(Caption);

    NameCacheSimpleId = 0;
    NameCacheLootUnits = -1;
    NameCacheVisited = -1;
    TargetInfoKey = -1;
    NameCacheTimer = 0.0f;
    AimPoint.z = 0.0f;
    AimPoint.y = 0.0f;
    AimPoint.x = 0.0f;
    bFireAttitude = 0;
    FireAttitude = 0;
    LastTargetId = 0;
    SelectPulse = 0.0f;
    bPickPending = 0;
}

HudTargetableObjects::~HudTargetableObjects()
{
    // FUN_004ea7b0.
    WaypointLabel.ClearContents();
    BestPathLabel.ClearContents();
    TargetNameText.ClearContents();
    TargetInfoText.ClearContents();
    RangeText.ClearContents();

    // Every name label emptied, then the whole map erased (FUN_004f2c90).
    for (std::map<unsigned, FHudNameLabel>::iterator It = NameLabels.begin();
         It != NameLabels.end(); ++It)
    {
        It->second.Text.ClearContents();
    }
    NameLabels.clear();

    // FUN_0059eee0 on each holder, in retail's order.
    TargetArrow.ReleaseWithAnim();
    TargetArrowHat.ReleaseWithAnim();
    TargetArrowPlayer.ReleaseWithAnim();
    TargetHealth.ReleaseWithAnim();
    TargetShields.ReleaseWithAnim();
    TargetArrowQuotes.ReleaseWithAnim();
    NonselectedArrow.ReleaseWithAnim();
    NonselectedPlayerArrow.ReleaseWithAnim();
    LootArrow.ReleaseWithAnim();
    NonselectedArrowHat.ReleaseWithAnim();
    NonselectedArrowQuotes.ReleaseWithAnim();
    Reticle.ReleaseWithAnim();
    ReticleQuotes.ReleaseWithAnim();
    ReticleArrows.ReleaseWithAnim();
    ReticleHealth.ReleaseWithAnim();
    ReticleShields.ReleaseWithAnim();
    WaypointReticle.ReleaseWithAnim();
    SelectedWaypoint.ReleaseWithAnim();
    NonselectedWaypoint.ReleaseWithAnim();

    G_HudTargetableObjects = nullptr;
    LeadMarkers.clear();

    // Retail's Font::String member dtors (FUN_00444e50), last declared first, run here ahead of
    // the holders'; the port's ~FFontString member dtors then run again on empty strings.
    RangeText.Destroy();
    TargetInfoText.Destroy();
    TargetNameText.Destroy();
    BestPathLabel.Destroy();
    WaypointLabel.Destroy();

    // The rest is the member and base destructors (the holders' FUN_0059ef20 repeats the
    // release above and finds nothing bound).
}

void HudTargetableObjects::SetTarget(IObjRW* Target)
{
    // LAB_004ead20.
    HUDItem::SetTarget(Target);   // FUN_004c81a0
    Scanner = nullptr;

    Watchable const* const Watched = HUDItem::Target.target();
    IObjRW* const ObjRW = ObjRwFromWatchable(const_cast<Watchable*>(Watched));
    if (ObjRW == nullptr)
        return;

    // A miss anywhere in the CBase -> CObject -> CShip chain leaves Ship null.
    CShip* Ship = nullptr;
    CBase const* const Base = static_cast<IObjInspectImpl*>(ObjRW);
    if (Base != nullptr)
    {
        CObject* const Object = Base->cobject();
        if (Object != nullptr &&
            (static_cast<unsigned>(Object->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
        {
            Ship = static_cast<CShip*>(Object);
        }
    }
    // No null test, as retail: a watched object that is not a ship reaches
    // CEquipManager::FindFirst with this = 0xe4. The only broadcaster (FUN_004c7330) hands every
    // HUD item the player's ship.
    CEquip const* const Equip = Ship->EquipManager.FindFirst(EquipTypeScanner);
    Scanner = CEScanner::cast(const_cast<CEquip*>(Equip));
}

bool HudTargetableObjects::Create(Win3D* NewParent, void* Data)
{
    // FUN_004f17c0 (this = the WinControl face).
    SetPosition(&Position);
    if (!Win3D::Create(NewParent, Data))   // FUN_004b5060
        return false;
    RegisterEvent(USER_BUTTON0);        // FUN_005760d0
    return true;
}

bool HudTargetableObjects::OnEvent(unsigned MsgId)
{
    // LAB_004ed910 (this = the Event::Handler face, +0x364).
    if (HudIsMousePickBlocked())   // FUN_0045a740
        return false;
    if (Win3DGetHover() != nullptr)   // FUN_0059daa0
        return false;
    if (MsgId != USER_BUTTON0)
        return false;
    if (IsMouseFlightHoldThresholdReached() && !IsMouseFlightLocked())
        return false;

    // A key-down record re-picks quietly and remembers the result.
    if (HudCommandRecord()->bKeyDown != 0)
    {
        bPickPending = PickUnderCursor(true) ? 1 : 0;
        return false;
    }
    if (HudCommandRecord()->bDoubleClick != 0)
        return false;

    if (PickId == 0 && bPickPending == 0)
        PickUnderCursor(false);

    // "Select target" (0x1c) for the picked object.
    bPickPending = 0;
    if (PickId == 0)
        return false;
    FSpaceCommand Command;
    std::memset(&Command, 0, sizeof(Command));
    Command.Id = USER_SET_TARGET;
    Command.ObjectId = PickId;
    Command.SubObjectId = PickSubObjId;
    Command.Byte26 = 1;
    SpaceSendCommand(&Command);   // FUN_0054bcb0
    return true;
}

Vector* HudTargetableObjects::GetAimPoint(Vector* Out) const
{
    // FUN_004f1da0.
    *Out = AimPoint;
    return Out;
}

void HudTargetableObjects::HideAlerts()
{
    // FUN_004ec400's body.
    MissileAlert->Flags &= ~(kWin3DActive | kWin3DVisible);
    TrailAlert->Flags &= ~(kWin3DActive | kWin3DVisible);
    RadiationAlert->Flags &= ~(kWin3DActive | kWin3DVisible);
    LagAlert->Flags &= ~(kWin3DActive | kWin3DVisible);
}

void HudTargetableObjectsHideAlertsInLetterbox()
{
    // FUN_004ec400.
    if (G_HudTargetableObjects == nullptr)
        return;
    if (!IsLetterboxVisible())   // FUN_0041a3e0
        return;
    G_HudTargetableObjects->HideAlerts();
}

bool HudTargetableObjects::ProjectContact(Vector const* World, float Radius, Vector* OutScreen,
                                          float* OutScreenRadius, float* OutDistance)
{
    // FUN_004ecfd0. The listener camera (FUN_0040f130).
    ICamera* const Camera = static_cast<ICamera*>(GetClientSoundListenerObject());
    const float* const View = (const float*)Camera->GetViewTransform();
    if (G_HudObjectsMath == nullptr)
        G_HudObjectsMath = Acquire3DMathEngine();           // FUN_005b73e0
    Vector ViewLocal;
    Vector const WorldPoint = {World->x, World->y, World->z};
    G_HudObjectsMath->TransformXform(ViewLocal, *reinterpret_cast<Transform const*>(View),
                                     WorldPoint);

    // Projection is tried only for an ordered ZNear <= -viewZ; NaN skips it.
    float const NegViewZ = -ViewLocal.z;
    float const ZNear = Camera->GetZNear();
    bool bOnScreen = false;
    if (ZNear <= NegViewZ)
    {
        if (Camera->ProjectVec(OutScreen, World) != 0)
        {
            // Any clamp into the surface means "off screen". z is zeroed either way.
            int const MaxX = static_cast<int>(G_RenderSurfaceWidth) - 1;
            int const MaxY = static_cast<int>(G_RenderSurfaceHeight) - 1;
            bool bClamped = false;
            if (OutScreen->x < 0.0f)
            {
                OutScreen->x = 0.0f;
                bClamped = true;
            }
            else if (static_cast<float>(MaxX) < OutScreen->x)
            {
                OutScreen->x = static_cast<float>(MaxX);
                bClamped = true;
            }
            if (OutScreen->y < 0.0f)
            {
                OutScreen->y = 0.0f;
                bClamped = true;
            }
            else if (static_cast<float>(MaxY) < OutScreen->y)
            {
                OutScreen->y = static_cast<float>(MaxY);
                bClamped = true;
            }
            bOnScreen = !bClamped;
            OutScreen->z = 0.0f;
        }
    }

    // The distance to the player, summed as x*x + z*z + y*y in double and stored every time.
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    Vector const& PlayerPos = Player->get_position();
    double const Dx = static_cast<double>(PlayerPos.x) - static_cast<double>(World->x);
    double const Dy = static_cast<double>(PlayerPos.y) - static_cast<double>(World->y);
    double const Dz = static_cast<double>(PlayerPos.z) - static_cast<double>(World->z);
    double const Distance = std::sqrt(Dx * Dx + Dz * Dz + Dy * Dy);
    *OutDistance = static_cast<float>(Distance);
    if (!bOnScreen)
        return false;

    // NaN Scale takes the 3000 branch; the radius is stored rounded.
    double const Scale = Distance / static_cast<double>(Radius) * static_cast<double>(kProjectScaleMul);
    double Numer = static_cast<double>(kProjectNumerFar);
    if (Scale < static_cast<double>(kProjectScaleGate))
        Numer = kProjectNumerNearMul * Scale + kProjectNumerNearAdd;
    double const ScreenRadius = Numer / Scale;
    *OutScreenRadius = static_cast<float>(ScreenRadius);
    // The unrounded double is what gets compared for the [10, 100] clamp.
    if (ScreenRadius < static_cast<double>(kProjectRadiusMin))
        *OutScreenRadius = kProjectRadiusMin;
    else if (ScreenRadius > static_cast<double>(kProjectRadiusMax))
        *OutScreenRadius = kProjectRadiusMax;
    return true;
}

void HudTargetableObjects::RebuildContacts()
{
    // FUN_004ed180. No null test on Scanner, as retail: Render only runs with the player's ship,
    // which always carries a scanner.
    ScanEntries = &Scanner->Scans;
    Contacts.clear();

    // The player (kept for the tail) and its ship.
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());   // FUN_0054baf0
    CShip* PlayerShip = nullptr;
    if (Player != nullptr)
    {
        CObject* const Object = CObjectOfObjRW(Player);
        if (Object != nullptr && (static_cast<unsigned>(Object->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
            PlayerShip = static_cast<CShip*>(Object);
    }

    // The nav waypoint -- the player's first waypoint, when it lies in the current system. A waypoint on an object takes that object's position and radius.
    WaypointRecord.bOnScreen = 0;
    WaypointRecord.bValid = 0;
    WaypointRecord.ObjectId = 0;
    if (GetPlayerWaypointCountRetail() > 0)   // FUN_004c4690
    {
        FPlayerWaypointRecord const* const Waypoint = GetPlayerWaypointRetail(0);   // FUN_004c46a0
        if (Waypoint != nullptr && Waypoint->SystemId == GetPlayerStarSystemIdRetail())
        {
            Vector Pos = Waypoint->Pos;
            float Radius = 40.0f;
            if (Waypoint->ObjectId != 0)
            {
                FClientGameobj* const Gameobj = ClientObjDb_FindObject(Waypoint->ObjectId, 0);
                if (Gameobj != nullptr)
                {
                    WaypointRecord.ObjectId = Waypoint->ObjectId;
                    CObject const* const Object = CObjectOfGameobj(Gameobj);
                    Radius = Object->Radius;
                    Pos = Object->WorldPosition;
                }
            }
            WaypointRecord.World = Pos;
            WaypointRecord.bValid = 1;
            WaypointRecord.bOnScreen =
                ProjectContact(&Pos, Radius, &WaypointRecord.Screen, &WaypointRecord.ScreenRadius,
                               &WaypointRecord.Distance) ? 1 : 0;
            // Only an ordered < 30.0f (DAT_005d3e40) is raised; NaN stays.
            if (WaypointRecord.ScreenRadius < 30.0f)
                WaypointRecord.ScreenRadius = 30.0f;
        }
    }

    // The best-path waypoint -- the first waypoint of the first route,
    // when it resolves to a position in the current system.
    BestPathRecord.bOnScreen = 0;
    BestPathRecord.bValid = 0;
    BestPathRecord.ObjectId = 0;
    if (BestPathGetWaypointCount() > 0)   // FUN_004c4fb0
    {
        FBestPathWaypoint* const Waypoint = BestPathGetWaypoint(0);   // FUN_004c4ff0
        if (Waypoint != nullptr)
        {
            Vector Pos;
            unsigned System = 0;
            if (Waypoint->ResolvePosition(&Pos, &System) &&   // FUN_004c52d0
                System == GetPlayerStarSystemIdRetail())
            {
                BestPathRecord.World = Pos;
                BestPathRecord.bValid = 1;
                IObjRW* const Object = Waypoint->ResolveObject();   // FUN_004c5110
                BestPathRecord.ObjectId = (Object != nullptr) ? Object->get_id() : 0;
                BestPathRecord.bOnScreen =
                    ProjectContact(&Pos, 40.0f, &BestPathRecord.Screen,
                                   &BestPathRecord.ScreenRadius, &BestPathRecord.Distance) ? 1 : 0;
                if (BestPathRecord.ScreenRadius < 30.0f)
                    BestPathRecord.ScreenRadius = 30.0f;
            }
        }
    }

    IObjRW* HotObject = nullptr;
    float HotDistance = 0.0f;
    HotScreenRadius = 1.0e8f;
    HotIndex = -1;
    RayObject = nullptr;
    GetClientSoundListenerObject();   // FUN_0040f130; the result is not used

    int CursorX = 0;
    int CursorY = 0;
    int CursorZ = 0;
    GetClientSoftwareCursor(&CursorX, &CursorY, &CursorZ);   // DAT_00616840 / DAT_00616844

    // One record per scanned object.
    for (int Index = 0; Index < ScanEntries->Count; ++Index)
    {
        Watchable const* const Watched = ScanEntries->Entries[Index].target();
        if (Watched == nullptr)
            continue;
        IObjRW* const Object = ObjRwFromWatchable(const_cast<Watchable*>(Watched));
        if (Object == nullptr)
            continue;

        // FUN_004f2da0 inserts at end().
        FHudContactRecord Blank;
        std::memset(&Blank, 0, sizeof(Blank));
        Blank.bValid = 1;
        Contacts.push_back(Blank);
        FHudContactRecord& Record = Contacts.back();
        Record.TypeFlags = 0;
        Record.bHot = 0;
        Record.bWatchListed = 0;
        if (PlayerShip == nullptr)
            continue;

        Record.World = Object->get_position();
        Record.bIsBestPath =
            (BestPathRecord.bValid != 0 && Object->get_id() == BestPathRecord.ObjectId) ? 1 : 0;
        Record.bIsWaypoint =
            (WaypointRecord.bValid != 0 && Object->get_id() == WaypointRecord.ObjectId) ? 1 : 0;
        Record.ObjectId = Object->get_id();

        unsigned Type = 0;
        if (Object->get_type(Type) != 0)
            continue;
        CObject const* const Target = Object->cobject();
        if (Target != nullptr && (static_cast<unsigned>(Target->ObjectClass) & CObject::ClassShip) == CObject::ClassShip &&
            static_cast<CShip const*>(Target)->bIsPlayer != 0)
        {
            Record.bPlayerShip = 1;
        }
        if ((Type & (OBJ_NON_TARGETABLE | OBJ_ASTEROID)) != 0)
            continue;

        // The client gameobj's +0xa8 byte, looked up in the bucket of the type (1 ships, 2 other
        // simples, 7 0x10000000).
        int Kind = 0;
        if ((Type & OBJ_SHIP_MASK) != 0)
            Kind = static_cast<int>(EClientGameobjKind::Ship);
        else if ((Type & OBJ_SOLAR_NO_WAYPOINT_MASK) != 0)
            Kind = static_cast<int>(EClientGameobjKind::Solar);
        else if ((Type & OBJ_LOOT) != 0)
            Kind = static_cast<int>(EClientGameobjKind::Loot);
        if (Kind != 0)
        {
            FClientGameobj* const Gameobj = ClientObjDb_FindObject(Object->get_id(), Kind);
            if (Gameobj != nullptr)
                Record.bWatchListed = Gameobj->bWatchListed;
        }

        Vector const Center = Object->get_center_of_mass();
        Vector PartCenter;
        float const Radius = Object->cobject()->get_physical_radius_r(PartCenter);
        Vector Screen;
        float ScreenRadius = 0.0f;
        float Distance = 0.0f;
        if (!ProjectContact(&Center, Radius, &Screen, &ScreenRadius, &Distance))
        {
            Record.TypeFlags = Type;
            Record.bOnScreen = 0;
            Record.Distance = Distance;
            continue;
        }

        Record.TypeFlags = Type;
        Record.Screen = Screen;
        Record.ScreenRadius = ScreenRadius;
        Record.Distance = Distance;
        Record.bOnScreen = 1;

        // The cursor inside the bracket box, edges included; the edges are computed in double.
        double const MouseX = static_cast<double>(CursorX);
        double const MouseY = static_cast<double>(CursorY);
        if (!(MouseX >= static_cast<double>(Screen.x) - static_cast<double>(ScreenRadius)))
            continue;
        if (!(MouseX <= static_cast<double>(Screen.x) + static_cast<double>(ScreenRadius)))
            continue;
        if (!(MouseY >= static_cast<double>(Screen.y) - static_cast<double>(ScreenRadius)))
            continue;
        if (!(MouseY <= static_cast<double>(Screen.y) + static_cast<double>(ScreenRadius)))
            continue;

        // With a candidate, a larger (or unordered) radius takes it; then any radius but an equal
        // one takes it; an equal radius goes to the one not farther away (only an ordered > is
        // skipped).
        bool bTake = false;
        if (HotObject != nullptr && !(ScreenRadius < HotScreenRadius) &&
            !(ScreenRadius == HotScreenRadius))
        {
            bTake = true;
        }
        else if (!(ScreenRadius == HotScreenRadius))
        {
            bTake = true;
        }
        else if (!(Distance > HotDistance))
        {
            bTake = true;
        }
        if (!bTake)
            continue;

        HotIndex = Index;
        HotObject = Object;
        HotDistance = Distance;
        HotScreenRadius = ScreenRadius;
    }

    // The scan index doubles as the vector index (as retail, though a skipped null watcher leaves
    // the two out of step).
    if (HotIndex >= 0)
    {
        Contacts.data()[HotIndex].bHot = 1;
        return;
    }

    // Nothing bracketed under the cursor -- fall back to the object the camera ray hit, when it
    // is a simple, not of type 0x20004000, and closer than 45 of its radii.
    if (PlayerShip == nullptr)
        return;
    CObject* const RayTarget = RayHit.Object;
    if (RayTarget == nullptr)
        return;
    if ((static_cast<unsigned>(RayTarget->ObjectClass) & CObject::ClassSimple) != CObject::ClassSimple)
        return;
    CSimple* Pick = static_cast<CSimple*>(RayTarget);
    if ((Pick->TypeBits & (OBJ_NON_TARGETABLE | OBJ_ASTEROID)) != 0)
        return;
    Vector RayCenter;
    float const RayRadius = RayTarget->get_physical_radius_r(RayCenter);
    Vector const& PlayerPos = Player->get_position();
    double const Dx = static_cast<double>(RayCenter.x) - static_cast<double>(PlayerPos.x);
    double const Dy = static_cast<double>(RayCenter.y) - static_cast<double>(PlayerPos.y);
    double const Dz = static_cast<double>(RayCenter.z) - static_cast<double>(PlayerPos.z);
    double const Ratio = std::sqrt(Dx * Dx + Dz * Dz + Dy * Dy) / static_cast<double>(RayRadius);
    // Only an ordered ratio < 45.0f (DAT_005d95ac) goes on; NaN stops.
    if (!(Ratio < 45.0))
        return;

    // A solar with a parent picks the parent, when the parent's client object is known; a known
    // parent that is not a solar ends the pick.
    if ((static_cast<unsigned>(RayTarget->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar)
    {
        ID_String const& Parent = static_cast<CSolar*>(RayTarget)->GetParentNickname();
        if (!Parent.IsEmpty())
        {
            FClientGameobj* const Gameobj = ClientObjDb_FindObject(Parent.Id, 2);
            if (Gameobj != nullptr)
            {
                CObject* const ParentObject = CObjectOfGameobj(Gameobj);
                if (ParentObject == nullptr)
                    return;
                if ((static_cast<unsigned>(ParentObject->ObjectClass) & CObject::ClassSolar) != CObject::ClassSolar)
                    return;
                Pick = static_cast<CSimple*>(ParentObject);
            }
        }
    }

    // Remember it, and mark its record hot when it is also scanned.
    RayObject = Pick;
    unsigned const PickObjectId = Pick->Id;
    for (int Index = 0; Index < ScanEntries->Count; ++Index)
    {
        Watchable const* const Watched = ScanEntries->Entries[Index].target();
        if (Watched == nullptr)
            continue;
        IObjRW* const Object = ObjRwFromWatchable(const_cast<Watchable*>(Watched));
        if (Object == nullptr)
            continue;
        if (Object->get_id() == PickObjectId)
        {
            HotIndex = Index;
            Contacts.data()[Index].bHot = 1;
            return;
        }
    }
}

namespace
{
    // .data 0x00612434 -- the radar range assumed without a scanner (only read).
    constexpr float kDefaultRadarRange = 2500.0f;

    // DAT_00674b74 -- an object id the Render tree forces as the pick (written by
    // DrawOffScreenContact; cleared by DrawTargetReticle, DrawWaypointArrow and
    // DrawOffScreenContact). Read by PickUnderCursor only.
    unsigned G_HudPickOverrideId = 0;
    // DAT_00674b78 -- the waypoint record the Render tree found under the cursor (written by
    // DrawWaypointArrow and DrawOffScreenContact; cleared by Render and DrawOffScreenContact).
    // Read by PickUnderCursor only.
    FHudContactRecord const* G_HudHoverRecord = nullptr;

    // The inlined bracket test of FUN_004ed9f0 and FUN_004ee100: the edges are computed in
    // double, and each edge fails when unordered.
    bool CursorInBracket(Vector const& Screen, float Radius, int CursorX, int CursorY)
    {
        double const MouseX = static_cast<double>(CursorX);
        double const MouseY = static_cast<double>(CursorY);
        if (!(MouseX >= static_cast<double>(Screen.x) - static_cast<double>(Radius)))
            return false;
        if (!(MouseX <= static_cast<double>(Radius) + static_cast<double>(Screen.x)))
            return false;
        if (!(MouseY >= static_cast<double>(Screen.y) - static_cast<double>(Radius)))
            return false;
        if (!(MouseY <= static_cast<double>(Radius) + static_cast<double>(Screen.y)))
            return false;
        return true;
    }
}

FCameraRayHit* CameraRayPickTo(FCameraRayHit* Out, ICamera* Camera, Vector const* To,
                               CObject const* Ignore, bool bSkipCloaked)
{
    // FUN_0051ddd0.
    FCameraRayHit Result;
    Result.Object = nullptr;
    Result.Instance = -1;
    Result.Point.x = 0.0f;
    Result.Point.y = 0.0f;
    Result.Point.z = 0.0f;
    Result.Distance = 0.0f;

    Vector const* const From = Camera->GetPosition();
    unsigned const SystemId = StarSysGetActiveSystemId();
    PhySys::RayHit Hits[8];
    int const HitCount = PhySys::FindRayCollisions(SystemId, *From, *To, Hits, 8);

    // The first hit that is not Ignore (itself or through its root), not a shield part, and --
    // with bSkipCloaked -- not on a ship whose CloakPercent is at least 1.0 (DAT_005c75dc;
    // NaN keeps the hit).
    for (int Index = 0; Index < HitCount; ++Index)
    {
        PhySys::RayHit const& Hit = Hits[Index];
        if (Hit.Object == Ignore)
            continue;
        CObject* const HitRoot = GetRoot(Hit.Object);
        if (HitRoot == Ignore)
            continue;
        if (Hit.Object->is_shield_part(Hit.PartId))
            continue;
        if (bSkipCloaked && HitRoot != nullptr &&
            (static_cast<unsigned>(HitRoot->ObjectClass) & CObject::ClassShip) == CObject::ClassShip &&
            static_cast<CEqObj const*>(HitRoot)->CloakPercent >= 1.0f)
        {
            continue;
        }

        // -1 (and an unmapped part) falls back to the object's own instance.
        long Instance = Hit.Object->part_to_inst(Hit.PartId);
        if (Instance == -1)
            Instance = Hit.Object->EngineInstanceId;

        // The camera position is fetched again; the distance is summed z, y, x.
        Result.Object = GetRoot(Hit.Object);
        Result.Instance = Instance;
        Vector const* const CameraPos = Camera->GetPosition();
        Result.Point = Hit.Point;
        double const Dx = static_cast<double>(Hit.Point.x) - static_cast<double>(CameraPos->x);
        double const Dy = static_cast<double>(Hit.Point.y) - static_cast<double>(CameraPos->y);
        double const Dz = static_cast<double>(Hit.Point.z) - static_cast<double>(CameraPos->z);
        Result.Distance = static_cast<float>(std::sqrt(Dz * Dz + Dy * Dy + Dx * Dx));
        break;
    }

    *Out = Result;
    return Out;
}

FCameraRayHit* CameraRayPick(FCameraRayHit* Out, ICamera* Camera, float ScreenX, float ScreenY,
                             CObject const* Ignore, bool bSkipCloaked)
{
    // FUN_0051dd20.
    Vector Direction;
    Camera->Unproject(&Direction, ScreenX, ScreenY);

    // Stretched to 1e7 (DAT_005dc130); the length sums y, z, x.
    double const Length = std::sqrt(static_cast<double>(Direction.y) * Direction.y +
                                    static_cast<double>(Direction.z) * Direction.z +
                                    static_cast<double>(Direction.x) * Direction.x);
    double const Scale = 1.0e7 / Length;
    Direction.x = static_cast<float>(static_cast<double>(Direction.x) * Scale);
    Direction.y = static_cast<float>(static_cast<double>(Direction.y) * Scale);
    Direction.z = static_cast<float>(static_cast<double>(Direction.z) * Scale);

    const Vector* const From = Camera->GetPosition();
    Vector To;
    To.x = Direction.x + From->x;
    To.y = Direction.y + From->y;
    To.z = Direction.z + From->z;
    CameraRayPickTo(Out, Camera, &To, Ignore, bSkipCloaked);
    return Out;
}

bool HudTargetableObjects::PickUnderCursor(bool bQuiet)
{
    // FUN_004ed9f0.
    PickSubObjId = SUBOBJ_ID_NONE;
    PickId = 0;

    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());   // FUN_0054baf0
    if (Player == nullptr)
        return false;
    CObject* const PlayerShip = CObjectOfObjRW(Player);
    if (PlayerShip == nullptr)
        return false;
    if ((static_cast<unsigned>(PlayerShip->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
        return false;

    if (G_HudPickOverrideId != 0)
    {
        PickId = G_HudPickOverrideId;
        PickSubObjId = SUBOBJ_ID_NONE;
        return true;
    }

    // The cursor on a waypoint marker -- the record itself, or a contact standing for it.
    FHudContactRecord const* const Hover = G_HudHoverRecord;
    if (Hover == &WaypointRecord || (Hover != nullptr && Hover->bIsWaypoint != 0))
    {
        PickId = 0;
        PickSubObjId = SUBOBJ_ID_NONE;
        if (bQuiet)
            return false;
        HudWaypointSelectPlayerWaypoint(0);   // FUN_004ceeb0
        return true;
    }
    if (Hover == &BestPathRecord || (Hover != nullptr && Hover->bIsBestPath != 0))
    {
        PickId = 0;
        PickSubObjId = SUBOBJ_ID_NONE;
        if (bQuiet)
            return false;
        HudWaypointSelectBestPath(0);   // FUN_004cef30
        return true;
    }

    // Rebuild the contacts, then the nav waypoint's bracket, else the best-path waypoint's, under
    // the cursor.
    RebuildContacts();   // FUN_004ed180
    int CursorX = 0;
    int CursorY = 0;
    int CursorZ = 0;
    GetClientSoftwareCursor(&CursorX, &CursorY, &CursorZ);
    FHudContactRecord const* Record = nullptr;
    if (WaypointRecord.bOnScreen != 0 &&
        CursorInBracket(WaypointRecord.Screen, WaypointRecord.ScreenRadius, CursorX, CursorY))
    {
        Record = &WaypointRecord;
    }
    else if (BestPathRecord.bOnScreen != 0 &&
             CursorInBracket(BestPathRecord.Screen, BestPathRecord.ScreenRadius, CursorX,
                             CursorY))
    {
        Record = &BestPathRecord;
    }

    // The waypoint wins unless a bracketed contact is there too whose bracket is not larger (only
    // an ordered HotScreenRadius > Record->ScreenRadius keeps the waypoint). The result is "not
    // quiet".
    if (Record != nullptr && (HotIndex < 0 || HotScreenRadius > Record->ScreenRadius))
    {
        if (Record == &WaypointRecord)
        {
            if (!bQuiet)
                HudWaypointSelectPlayerWaypoint(0);   // FUN_004ceeb0
        }
        else if (!bQuiet)
        {
            HudWaypointSelectBestPath(0);   // FUN_004cef30
        }
        PickId = 0;
        PickSubObjId = SUBOBJ_ID_NONE;
        return !bQuiet;
    }

    // The bracketed contact; a solar with a known parent picks the parent's id.
    if (HotIndex >= 0)
    {
        IObjRW* const Hot =
            ObjRwFromWatchable(const_cast<Watchable*>(ScanEntries->Entries[HotIndex].target()));
        if (Hot != nullptr)
        {
            PickId = Hot->get_id();
            PickSubObjId = SUBOBJ_ID_NONE;
            CObject const* const HotObject = Hot->cobject();
            if (HotObject != nullptr &&
                (static_cast<unsigned>(HotObject->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar)
            {
                CSolar const* const Solar = static_cast<CSolar const*>(HotObject);
                if (!Solar->GetParentNickname().IsEmpty())
                {
                    FClientGameobj* const Parent =
                        ClientObjDb_FindObject(Solar->GetParentNickname().Id, static_cast<int>(EClientGameobjKind::Solar));   // FUN_005416c0
                    if (Parent != nullptr)
                        PickId = static_cast<IObjRW*>(Parent)->get_id();
                }
            }
            return PickId != 0;
        }
    }

    // The camera ray from the cursor, past the player's ship and over fully cloaked ships.
    ICamera* const Camera = static_cast<ICamera*>(GetClientSoundListenerObject());   // FUN_0040f130
    FCameraRayHit Hit;
    CameraRayPick(&Hit, Camera, static_cast<float>(CursorX), static_cast<float>(CursorY),
                  PlayerShip, true);
    PickRayTarget(&Hit, &PickId, &PickSubObjId);

    // The squared distance a hostile contact has to beat; retail keeps it in the argument's own
    // stack slot. With a ray pick it is FLT_MAX squared, which overflows to +inf as a float. With
    // the nearest pickable object it is that object's squared distance from the camera, summed
    // z, x, y. With neither, the slot still holds bQuiet's dword (1 or 0) read as a float: the
    // smallest denormal or +0.
    float HostileLimit = 0.0f;
    if (PickId != 0)
    {
        HostileLimit = std::numeric_limits<float>::infinity();
    }
    else
    {
        unsigned const ArgumentDword = bQuiet ? 1u : 0u;
        std::memcpy(&HostileLimit, &ArgumentDword, sizeof(HostileLimit));
        FClientGameobj* const Near = FindPickableNearCursor();   // FUN_004ee100
        if (Near != nullptr)
        {
            IObjRW* const NearObject = Near;
            PickId = NearObject->get_id();
            Vector const* const CameraPos = Camera->GetPosition();
            Vector const& NearPos = NearObject->get_position();
            double const Dx = static_cast<double>(NearPos.x) - static_cast<double>(CameraPos->x);
            double const Dy = static_cast<double>(NearPos.y) - static_cast<double>(CameraPos->y);
            double const Dz = static_cast<double>(NearPos.z) - static_cast<double>(CameraPos->z);
            HostileLimit = static_cast<float>(Dz * Dz + Dx * Dx + Dy * Dy);
        }
    }

    // A hostile contact under the cursor takes over when it is nearer the camera (summed z, y, x;
    // only an ordered <).
    IObjRW* const Hostile = FindHostileContactNearCursor();   // FUN_004f1c20
    if (Hostile != nullptr)
    {
        Vector const* const CameraPos = Camera->GetPosition();
        Vector const& HostilePos = Hostile->get_position();
        double const Dx = static_cast<double>(HostilePos.x) - static_cast<double>(CameraPos->x);
        double const Dy = static_cast<double>(HostilePos.y) - static_cast<double>(CameraPos->y);
        double const Dz = static_cast<double>(HostilePos.z) - static_cast<double>(CameraPos->z);
        if (Dz * Dz + Dy * Dy + Dx * Dx < static_cast<double>(HostileLimit))
        {
            PickId = Hostile->get_id();
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s",
                  "E:\\FL\\Scratch\\Source\\Client\\Space\\HUD_Targetable_Objects.cpp", 0x9ae,
                  "Used Hostile Pick Assistance!");
        }
    }
    return PickId != 0;
}

void HudTargetableObjects::PickRayTarget(FCameraRayHit const* Hit, unsigned* OutId,
                                         unsigned short* OutSubObjId)
{
    // FUN_004edeb0.
    *OutId = 0;
    *OutSubObjId = SUBOBJ_ID_NONE;

    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());   // FUN_0054baf0
    if (Player == nullptr)
        return;
    CObject* const PlayerShip = CObjectOfObjRW(Player);
    if (PlayerShip == nullptr)
        return;
    if ((static_cast<unsigned>(PlayerShip->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
        return;

    CObject* const Object = Hit->Object;
    if (Object == nullptr)
        return;
    if ((static_cast<unsigned>(Object->ObjectClass) & CObject::ClassSimple) != CObject::ClassSimple)
        return;
    CSimple const* const Simple = static_cast<CSimple const*>(Object);

    // With a radar range under 2500 (ordered <), the object's surface -- its centre's distance
    // from the ship (the offset rounded to floats first; summed y, z, x) less its radius, taken
    // absolute -- must lie within it (only an ordered > is out of range).
    float Range = kDefaultRadarRange;
    if (Scanner != nullptr)
        Range = Scanner->GetRadarRange();
    bool bOutOfRange = false;
    if (Range < kDefaultRadarRange)
    {
        Vector Center;
        float const Radius = Object->get_physical_radius_r(Center);
        Vector Offset;
        Offset.x = Center.x - PlayerShip->WorldPosition.x;
        Offset.y = Center.y - PlayerShip->WorldPosition.y;
        Offset.z = Center.z - PlayerShip->WorldPosition.z;
        double const Length = std::sqrt(static_cast<double>(Offset.y) * Offset.y +
                                        static_cast<double>(Offset.z) * Offset.z +
                                        static_cast<double>(Offset.x) * Offset.x);
        if (std::fabs(Length - static_cast<double>(Radius)) > static_cast<double>(Range))
            bOutOfRange = true;
    }

    // In range, the object's id -- at once for one with any of the type bits 7, otherwise only
    // within 45 of its radii (DAT_005d95ac; summed z, y, x; ordered <). Type 0x20004000 is never
    // picked.
    if (!bOutOfRange)
    {
        unsigned const Type = Simple->TypeBits;
        if ((Type & (OBJ_NON_TARGETABLE | OBJ_ASTEROID)) == 0)
        {
            if ((Type & (OBJ_MOON | OBJ_PLANET | OBJ_SUN)) != 0)
            {
                *OutId = Simple->Id;
            }
            else
            {
                Vector Center;
                float const Radius = Object->get_physical_radius_r(Center);
                double const Dx = static_cast<double>(Center.x) -
                                  static_cast<double>(PlayerShip->WorldPosition.x);
                double const Dy = static_cast<double>(Center.y) -
                                  static_cast<double>(PlayerShip->WorldPosition.y);
                double const Dz = static_cast<double>(Center.z) -
                                  static_cast<double>(PlayerShip->WorldPosition.z);
                double const Ratio =
                    std::sqrt(Dz * Dz + Dy * Dy + Dx * Dx) / static_cast<double>(Radius);
                if (Ratio < 45.0)
                    *OutId = Simple->Id;
            }
        }
    }

    // The sub-object only for an equipped object (class 0x103) in range.
    CObject* const Equipped = Hit->Object;
    if (Equipped == nullptr)
        return;
    if ((static_cast<unsigned>(Equipped->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj)
        return;
    if (bOutOfRange)
        return;

    // A solar with a known parent picks the parent, whole.
    if ((static_cast<unsigned>(Equipped->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar)
    {
        CSolar const* const Solar = static_cast<CSolar const*>(Equipped);
        if (!Solar->GetParentNickname().IsEmpty())
        {
            FClientGameobj* const Parent =
                ClientObjDb_FindObject(Solar->GetParentNickname().Id, static_cast<int>(EClientGameobjKind::Solar));   // FUN_005416c0
            if (Parent != nullptr)
            {
                *OutId = static_cast<IObjRW*>(Parent)->get_id();
                *OutSubObjId = SUBOBJ_ID_NONE;
            }
        }
    }

    // Then the hit instance's sub-object, with the root reported as none. This overwrites the
    // parent's SUBOBJ_ID_NONE above too.
    *OutSubObjId = static_cast<CEqObj const*>(Equipped)->inst_to_subobj_id(Hit->Instance);
    if (*OutSubObjId == SUBOBJ_ID_ROOT)
        *OutSubObjId = SUBOBJ_ID_NONE;
}

FClientGameobj* HudTargetableObjects::FindPickableNearCursor()
{
    // FUN_004ee100. Bucket 2 of the client ObjDB (FUN_00540fc0).
    FClientGameobj* Best = nullptr;
    float BestDistance = 1.0e20f;
    int const Kind = static_cast<int>(EClientGameobjKind::Solar);
    for (FClientGameobj* Gameobj = ClientObjDb_GetKindHead(&Kind); Gameobj != nullptr;
         Gameobj = Gameobj->ListNext)
    {
        // A simple whose TypeBits low byte has bit 7.
        IObjRW* const Object = Gameobj;
        CObject const* const Target = Object->cobject();
        if (Target == nullptr)
            continue;
        if ((static_cast<unsigned>(Target->ObjectClass) & CObject::ClassSimple) != CObject::ClassSimple)
            continue;
        if ((static_cast<CSimple const*>(Target)->TypeBits & OBJ_TRADELANE_RING) == 0)
            continue;

        Vector const Center = Object->get_center_of_mass();
        Vector PartCenter;
        float const Radius = Object->cobject()->get_physical_radius_r(PartCenter);
        Vector Screen;
        float ScreenRadius = 0.0f;
        float Distance = 0.0f;
        if (!ProjectContact(&Center, Radius, &Screen, &ScreenRadius, &Distance))
            continue;

        // The doubled bracket (rounded to float) holds the cursor.
        ScreenRadius = ScreenRadius + ScreenRadius;
        int CursorX = 0;
        int CursorY = 0;
        int CursorZ = 0;
        GetClientSoftwareCursor(&CursorX, &CursorY, &CursorZ);
        if (!CursorInBracket(Screen, ScreenRadius, CursorX, CursorY))
            continue;

        // The nearest (ordered <).
        if (Distance < BestDistance)
        {
            BestDistance = Distance;
            Best = Gameobj;
        }
    }

    // Only under 11000 (DAT_005d9610; ordered <).
    if (BestDistance < 11000.0f)
        return Best;
    return nullptr;
}

IObjRW* HudTargetableObjects::FindHostileContactNearCursor()
{
    // FUN_004f1c20.
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());   // FUN_0054baf0
    if (Player == nullptr)
        return nullptr;

    // FUN_0040f130's result is not used. The cursor as a fraction of the surface from its
    // centre: (mouse - size * 0.5) / size, the half size (DAT_005c75e0) and the fraction rounded
    // to float.
    IObjRW* Best = nullptr;
    float BestDistance = FLT_MAX;
    GetClientSoundListenerObject();
    int CursorX = 0;
    int CursorY = 0;
    int CursorZ = 0;
    GetClientSoftwareCursor(&CursorX, &CursorY, &CursorZ);
    double const Width = static_cast<double>(static_cast<int>(G_RenderSurfaceWidth));    // DAT_00610850
    double const Height = static_cast<double>(static_cast<int>(G_RenderSurfaceHeight));  // DAT_00610854
    float const HalfWidth = static_cast<float>(Width * 0.5);
    float const CursorFracX =
        static_cast<float>((static_cast<double>(CursorX) - static_cast<double>(HalfWidth)) / Width);
    float const HalfHeight = static_cast<float>(Height * 0.5);
    float const CursorFracY =
        static_cast<float>((static_cast<double>(CursorY) - static_cast<double>(HalfHeight)) / Height);

    // The scan list and the contact vector walked by one index.
    for (int Index = 0; Index < ScanEntries->Count; ++Index)
    {
        IObjRW* const Object =
            ObjRwFromWatchable(const_cast<Watchable*>(ScanEntries->Entries[Index].target()));
        if (Object == nullptr)
            continue;
        FHudContactRecord const& Record = Contacts.data()[Index];
        if (Record.bOnScreen == 0)
            continue;
        if ((Record.TypeFlags & OBJ_FIGHTER) == 0)
            continue;
        // FUN_0045a490 -- hostile only.
        if (GetAttitudeClass(Player, Object) != -1)
            continue;

        // The contact's circle, in the same units, holds the cursor (DAT_005d8f20; NaN fails).
        double const Dx = static_cast<double>(CursorFracX) -
                          (static_cast<double>(Record.Screen.x) - static_cast<double>(HalfWidth)) / Width;
        double const Dy = static_cast<double>(CursorFracY) -
                          (static_cast<double>(Record.Screen.y) - static_cast<double>(HalfHeight)) / Height;
        double const R = static_cast<double>(Record.ScreenRadius) / Width;
        if (!(R * R + static_cast<double>(0.003f) > Dy * Dy + Dx * Dx))
            continue;

        // The nearest (ordered <).
        if (Record.Distance < BestDistance)
        {
            Best = Object;
            BestDistance = Record.Distance;
        }
    }
    return Best;
}

namespace
{
    // FUN_00489e80(Id, 0) -- defined with the hazard warning below (NnVoiceEnqueue).
    void QueueHazardVoiceLine(unsigned VoiceId);

    // DAT_00674b50 / DAT_00674b4c -- the static initialisers LAB_004e8020 / LAB_004e8040.
    unsigned const G_VoiceIncomingTorpedo = CreateID("incoming_torpedo");   // DAT_00674b50
    unsigned const G_VoiceIncomingMissile = CreateID("incoming_missile");   // DAT_00674b4c

    // DAT_00674b7c / DAT_006127bc -- Render's half-second toggle. The byte is written here and
    // read nowhere else in the PE.
    float G_HudTargetableBlinkTimer = 0.0f;           // DAT_00674b7c (.bss)
    unsigned char G_bHudTargetableBlinkPhase = 1;     // DAT_006127bc

    // The test every alert timer uses: only an ordered "below" stops; above, equal or unordered
    // goes on.
    bool NotBelow(double Value, float Limit)
    {
        return !(Value < static_cast<double>(Limit));
    }

    // The player's CObject, kept only when the class has all of Mask (the inlined player-ship
    // lookups).
    CObject* PlayerObjectOfClass(IObjRW* Player, unsigned Mask)
    {
        if (Player == nullptr)
            return nullptr;
        CObject* const Object = CObjectOfObjRW(Player);
        if (Object == nullptr)
            return nullptr;
        if ((static_cast<unsigned>(Object->ObjectClass) & Mask) != Mask)
            return nullptr;
        return Object;
    }

    // Render's tooltip feed for one object (inlined twice): an unvisited solar shows its type caption (FUN_004d4d50) under UnvisitedIds; anything else
    // its caption (FUN_004e8100, mode 2 without an owner player at +0xb4, else 1, with the
    // faction short name) under its id, with its ids_name (FUN_005472a0).
    void FeedObjectToolTip(FToolTipHoverInfo* Info, CSimple const* Object, int UnvisitedIds)
    {
        if (HudIsUnvisitedSolar(Object))   // FUN_004d4c70
        {
            Info->IdsAlt = static_cast<int>(HudObjectTypeIds(Object));   // FUN_004d4d50
            if (Info->IdsAlt == 0)
                return;
            Info->Ids = UnvisitedIds;
            NnToolTipFeedHover(Info);   // FUN_004be370
            return;
        }
        int const Mode = (Object->OwnerPlayer == 0) ? kCaptionNameAndLocation : kCaptionLocation;
        if (!HudBuildObjectCaption(Object, reinterpret_cast<FHudWideText*>(&Info->CaptionW), Mode,
                                   true))   // FUN_004e8100
        {
            return;
        }
        Info->Ids = static_cast<int>(Object->Id);
        Info->IdsAlt = static_cast<int>(GetObjectIdsName(Object));   // FUN_005472a0
        NnToolTipFeedHover(Info);
    }
}

void HudTargetableObjects::Render(FSpaceCamera* /*Camera*/, float DeltaSeconds)
{
    // LAB_004ec450. With the interface group or the HUD option off, the cursor not drawn
    // (FUN_0041dd90) or no player, the four alerts are hidden and nothing else happens.
    if (GetGameOption(kGameOptInterfaceGroup) == 0 || GetGameOption(kGameOptHud) == 0 ||
        GetClientMouseCursorDrawFlag() == 0 || GetPlayerObjRW() == nullptr)
    {
        HideAlerts();
        return;
    }

    // The camera ray under the cursor, past the player's object (any class with bit 0), with
    // fully cloaked ships passed over.
    G_HudHoverRecord = nullptr;
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    CObject* const IgnoreObject = PlayerObjectOfClass(Player, CObject::ClassObject);
    int CursorX = 0;
    int CursorY = 0;
    int CursorZ = 0;
    GetClientSoftwareCursor(&CursorX, &CursorY, &CursorZ);
    ICamera* const Listener = static_cast<ICamera*>(GetClientSoundListenerObject());   // FUN_0040f130
    CameraRayPick(&RayHit, Listener, static_cast<float>(CursorX), static_cast<float>(CursorY),
                  IgnoreObject, true);

    LeadMarkers.clear();   // FUN_004f2d50

    // The half-second toggle (DAT_005d8ee4): only above 0.5 (NaN not) is it wound down, flipping
    // the phase once per 0.5 taken off.
    double BlinkTimer =
        static_cast<double>(G_HudTargetableBlinkTimer) + static_cast<double>(DeltaSeconds);
    G_HudTargetableBlinkTimer = static_cast<float>(BlinkTimer);
    if (BlinkTimer > 0.5)
    {
        unsigned char Phase = G_bHudTargetableBlinkPhase;
        do
        {
            BlinkTimer -= 0.5;
            Phase = (Phase == 0) ? 1 : 0;
        } while (BlinkTimer > 0.5);
        G_HudTargetableBlinkTimer = static_cast<float>(BlinkTimer);
        G_bHudTargetableBlinkPhase = Phase;
    }

    RebuildContacts();
    DrawContactMarkers();
    DrawTargetReticle();
    CollectLeadMarkers();

    // The lead markers only for a player ship neither in a trade lane nor in cruise.
    CShip* const PlayerShip = static_cast<CShip*>(PlayerObjectOfClass(Player, CObject::ClassShip));
    if (PlayerShip != nullptr && !PlayerShip->is_using_tradelane() &&
        !PlayerShip->is_cruise_active())
    {
        DrawLeadMarkers();
    }

    UpdateAimPoint();
    UpdateMissileAlert();
    UpdateTrailAlert();

    // The radiation alert blinks every 0.7 s (DAT_005d8efc) while the
    // zone warning is on (FUN_004f5340): shown when the warning is on and the phase is off.
    double const RadiationTimer =
        GetClientFrameTimeCapped() + static_cast<double>(RadiationBlinkTimer);   // FUN_0042d680
    RadiationBlinkTimer = static_cast<float>(RadiationTimer);
    if (NotBelow(RadiationTimer, 0.7f))
    {
        RadiationBlinkTimer = static_cast<float>(RadiationTimer - static_cast<double>(0.7f));
        bRadiationBlinkOn = (bRadiationBlinkOn == 0) ? 1 : 0;
        uintptr_t const bHide =
            (IsHudRadiationZoneWarningOn() && bRadiationBlinkOn == 0) ? 0u : 1u;
        RadiationAlert->SetProperty(kWinPropHide, bHide, 0);
    }

    UpdateLagAlert();

    // The radar range, when it is under 2500, limits the ray object's cursor and tooltip below;
    // otherwise the limit (1e30) is never read.
    float Range = kDefaultRadarRange;
    if (Scanner != nullptr && Player != nullptr)
        Range = Scanner->GetRadarRange();
    bool bRangeLimited = false;
    float RangeLimit = 1.0e30f;
    if (Range < kDefaultRadarRange)
    {
        RangeLimit = Range;
        bRangeLimited = true;
    }

    // The cursor. Over a Win3D control it is the arrow; over the ray object or the bracketed
    // contact it shows the attitude towards it; otherwise the cross -- and with the fire attitude
    // latched the fire cursor of FireAttitude instead.
    Win3D* const Hover = Win3DGetHover();   // FUN_0059daa0
    int Attitude = 0;
    bool bUseAttitude = false;
    if (Player == nullptr || Hover != nullptr)
    {
        if (Hover != nullptr)
            MouseSetCursor("Arrow", false);   // FUN_0041dde0
    }
    else if (HotIndex < 0)
    {
        // The ray object.
        CSimple* const Ray = RayObject;
        if (Ray != nullptr)
        {
            bool bNoAttitude = true;
            FClientGameobj* const RayGameobj = ClientObjDb_FindObject(Ray->Id, 0);   // FUN_005416c0
            if (RayGameobj != nullptr)
            {
                // FUN_0045a490: the object's attitude towards the player, the player passed only
                // when its gameobj sits in ObjDB bucket 1.
                IObjRW* const PlayerOrNull =
                    (static_cast<FClientGameobj*>(Player)->ManagerKind == EClientGameobjKind::Ship) ? Player : nullptr;
                Attitude = GetAttitudeClass(PlayerOrNull, RayGameobj);
                bNoAttitude = false;
            }
            if (bRangeLimited)
            {
                // The surface out of radar range (summed x, y, z).
                Vector Center;
                float const Radius = Ray->get_physical_radius_r(Center);
                Vector const& PlayerPos = Player->get_position();
                Vector Offset;
                Offset.x = Center.x - PlayerPos.x;
                Offset.y = Center.y - PlayerPos.y;
                Offset.z = Center.z - PlayerPos.z;
                double const Length = std::sqrt(static_cast<double>(Offset.x) * Offset.x +
                                                static_cast<double>(Offset.y) * Offset.y +
                                                static_cast<double>(Offset.z) * Offset.z);
                if (std::fabs(Length - static_cast<double>(Radius)) > static_cast<double>(RangeLimit))
                    bNoAttitude = true;
            }
            // A fully cloaked equipped object (>= 1.0, ordered) shows no attitude.
            bool const bFullyCloaked =
                (static_cast<unsigned>(Ray->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj &&
                static_cast<CEqObj const*>(static_cast<CObject const*>(Ray))->CloakPercent >= 1.0f;
            if (!bFullyCloaked && !bNoAttitude)
                bUseAttitude = true;
        }
    }
    else
    {
        // The bracketed contact (a solar's known parent stands in).
        IObjRW* Contact =
            ObjRwFromWatchable(const_cast<Watchable*>(ScanEntries->Entries[HotIndex].target()));
        if (Contact != nullptr)
        {
            CObject const* const ContactObject = Contact->cobject();
            if (ContactObject != nullptr &&
                (static_cast<unsigned>(ContactObject->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar)
            {
                CSolar const* const Solar = static_cast<CSolar const*>(ContactObject);
                if (!Solar->GetParentNickname().IsEmpty())
                {
                    FClientGameobj* const Parent =
                        ClientObjDb_FindObject(Solar->GetParentNickname().Id, static_cast<int>(EClientGameobjKind::Solar));
                    if (Parent != nullptr)
                        Contact = Parent;
                }
            }
            // As above, the player only from ObjDB bucket 1.
            IObjRW* const PlayerOrNull =
                (static_cast<FClientGameobj*>(Player)->ManagerKind == EClientGameobjKind::Ship) ? Player : nullptr;
            Attitude = GetAttitudeClass(PlayerOrNull, Contact);   // FUN_0045a490
            CObject const* const Target = Contact->cobject();
            bool const bFullyCloaked =
                Target != nullptr && (static_cast<unsigned>(Target->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj &&
                static_cast<CEqObj const*>(Target)->CloakPercent >= 1.0f;
            if (!bFullyCloaked)
                bUseAttitude = true;
        }
    }
    if (Player != nullptr && Hover == nullptr)
    {
        bool bFireCursor = false;
        if (bUseAttitude)
        {
            if (Attitude == -1)
                MouseSetCursor("hostile", false);
            else if (Attitude == 0)
                MouseSetCursor("neutral", false);
            else if (Attitude == 1)
                MouseSetCursor("friendly", false);
            bFireCursor = bFireAttitude != 0;
        }
        else if (bFireAttitude != 0)
        {
            bFireCursor = true;
        }
        else
        {
            MouseSetCursor("Cross", false);
        }
        if (bFireCursor)
        {
            // DAT_005d9550 is "fire".
            if (FireAttitude == -1)
                MouseSetCursor("fire", false);
            else if (FireAttitude == 0)
                MouseSetCursor("fire_neutral", false);
            else if (FireAttitude == 1)
                MouseSetCursor("fire_friendly", false);
        }
    }
    else if (Player == nullptr && Hover == nullptr)
    {
        // No player and no control under the cursor.
        if (bFireAttitude != 0)
        {
            if (FireAttitude == -1)
                MouseSetCursor("fire", false);
            else if (FireAttitude == 0)
                MouseSetCursor("fire_neutral", false);
            else if (FireAttitude == 1)
                MouseSetCursor("fire_friendly", false);
        }
        else
        {
            MouseSetCursor("Cross", false);
        }
    }

    // The object tooltip -- not over a fed tooltip (FUN_004be2d0) or a
    // control, not while picking is blocked (FUN_0045a740), only with mouse-flight unlocked
    // (FUN_004c7270) and before its hold engages (FUN_004c7c60).
    if (NnToolTipWasFedThisFrame())
        return;
    if (Hover != nullptr)
        return;
    if (HudIsMousePickBlocked())
        return;
    if (!IsMouseFlightUnlocked())
        return;
    if (IsMouseFlightHoldThresholdReached())
        return;

    FToolTipHoverInfo Info;
    ToolTipHoverInit(&Info);
    if (HotIndex < 0)
    {
        // The ray object, within radar range, not cloaked; unvisited solars under ~Id.
        CSimple* const Ray = RayObject;
        if (Ray != nullptr)
        {
            bool bShow = true;
            if (bRangeLimited && Player != nullptr)
            {
                Vector Center;
                float const Radius = Ray->get_physical_radius_r(Center);
                Vector const& PlayerPos = Player->get_position();
                Vector Offset;
                Offset.x = Center.x - PlayerPos.x;
                Offset.y = Center.y - PlayerPos.y;
                Offset.z = Center.z - PlayerPos.z;
                double const Length = std::sqrt(static_cast<double>(Offset.x) * Offset.x +
                                                static_cast<double>(Offset.y) * Offset.y +
                                                static_cast<double>(Offset.z) * Offset.z);
                if (std::fabs(Length - static_cast<double>(Radius)) > static_cast<double>(RangeLimit))
                    bShow = false;
            }
            if (bShow)
            {
                bool const bFullyCloaked =
                    (static_cast<unsigned>(Ray->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj &&
                    static_cast<CEqObj const*>(static_cast<CObject const*>(Ray))->CloakPercent >= 1.0f;
                if (!bFullyCloaked)
                    FeedObjectToolTip(&Info, Ray, static_cast<int>(~Ray->Id));
            }
        }
    }
    else
    {
        // The bracketed contact (a simple); a solar's known parent stands in, and must itself be
        // a solar; not cloaked; unvisited solars under their id.
        IObjRW* const Contact =
            ObjRwFromWatchable(const_cast<Watchable*>(ScanEntries->Entries[HotIndex].target()));
        CObject const* Target = (Contact != nullptr) ? Contact->cobject() : nullptr;
        bool bShow = Target != nullptr && (static_cast<unsigned>(Target->ObjectClass) & CObject::ClassSimple) == CObject::ClassSimple;
        if (bShow && (static_cast<unsigned>(Target->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar)
        {
            CSolar const* const Solar = static_cast<CSolar const*>(Target);
            if (!Solar->GetParentNickname().IsEmpty())
            {
                FClientGameobj* const Parent =
                    ClientObjDb_FindObject(Solar->GetParentNickname().Id, static_cast<int>(EClientGameobjKind::Solar));
                if (Parent != nullptr)
                {
                    CObject* const ParentObject = CObjectOfGameobj(Parent);
                    if (ParentObject == nullptr ||
                        (static_cast<unsigned>(ParentObject->ObjectClass) & CObject::ClassSolar) != CObject::ClassSolar)
                    {
                        bShow = false;
                    }
                    else
                    {
                        Target = ParentObject;
                    }
                }
            }
        }
        if (bShow)
        {
            bool const bFullyCloaked =
                (static_cast<unsigned>(Target->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj &&
                static_cast<CEqObj const*>(Target)->CloakPercent >= 1.0f;
            if (!bFullyCloaked)
            {
                CSimple const* const Simple = static_cast<CSimple const*>(Target);
                FeedObjectToolTip(&Info, Simple, static_cast<int>(Simple->Id));
            }
        }
    }
}

namespace
{
    // FUN_004eada0's corner cut: Corner + (Toward - Corner) * Scale. The difference is rounded to
    // floats; x and y are scaled and added in double with one rounding at the store, z is
    // rounded to float after the scale and then added.
    Vector BracketCutPoint(Vector const& Corner, Vector const& Toward, float Scale)
    {
        float const DeltaX = Toward.x - Corner.x;
        float const DeltaY = Toward.y - Corner.y;
        float const DeltaZ = Toward.z - Corner.z;
        Vector Out;
        Out.x = static_cast<float>(static_cast<double>(Corner.x) +
                                   static_cast<double>(DeltaX) * static_cast<double>(Scale));
        Out.y = static_cast<float>(static_cast<double>(Corner.y) +
                                   static_cast<double>(DeltaY) * static_cast<double>(Scale));
        float const ScaledZ = static_cast<float>(static_cast<double>(DeltaZ) * static_cast<double>(Scale));
        Out.z = Corner.z + ScaledZ;
        return Out;
    }

    // The one cut (the hostile bracket's first) that rounds all three scaled components to float
    // and adds the corner afterwards (FUN_004d7270, Vector::operator+=).
    Vector BracketCutPointRounded(Vector const& Corner, Vector const& Toward, float Scale)
    {
        float const DeltaX = Toward.x - Corner.x;
        float const DeltaY = Toward.y - Corner.y;
        float const DeltaZ = Toward.z - Corner.z;
        Vector Out;
        Out.x = static_cast<float>(static_cast<double>(DeltaX) * static_cast<double>(Scale));
        Out.y = static_cast<float>(static_cast<double>(DeltaY) * static_cast<double>(Scale));
        Out.z = static_cast<float>(static_cast<double>(DeltaZ) * static_cast<double>(Scale));
        Out.x = Corner.x + Out.x;
        Out.y = Corner.y + Out.y;
        Out.z = Corner.z + Out.z;
        return Out;
    }

    Vector MakeVector(float X, float Y, float Z)
    {
        Vector Out;
        Out.x = X;
        Out.y = Y;
        Out.z = Z;
        return Out;
    }

    // Every FUN_005a2940 call in FUN_004eada0 passes width 2.0f.
    void DrawBracketLine(Vector const& A, Vector const& B, unsigned long Color,
                         unsigned long EdgeColor)
    {
        Win3DImmedDrawLine(&A, &B, Color, EdgeColor, 2.0f);
    }
}

void HudTargetableObjects::DrawContactBracket(FHudContactRecord const* Record,
                                              unsigned long const* Color, bool bHostile,
                                              bool bWatchListed)
{
    // FUN_004eada0. Nothing for a radius that equals 0.0 (DAT_005c8ba8; NaN goes on).
    float const Radius = Record->ScreenRadius;
    if (static_cast<double>(Radius) == 0.0)
        return;

    // The four corners, Screen + (+-R, +-R, 0), named by the signs of their x and y offsets.
    float const NegRadius = -Radius;
    Vector const& Screen = Record->Screen;
    Vector const CornerNP = MakeVector(Screen.x + NegRadius, Screen.y + Radius, Screen.z + 0.0f);
    Vector const CornerPP = MakeVector(Screen.x + Radius, Screen.y + Radius, Screen.z + 0.0f);
    Vector const CornerPN = MakeVector(Screen.x + Radius, Screen.y + NegRadius, Screen.z + 0.0f);
    Vector const CornerNN = MakeVector(Screen.x + NegRadius, Screen.y + NegRadius, Screen.z + 0.0f);

    // The arm fraction, 0.2 at a 10-pixel radius falling by 0.15 over the next 90 (DAT_005d8ee8
    // 10, DAT_005d6edc 0.15, DAT_005d8eec 100, DAT_005d043c 0.2).
    double const Falloff = (static_cast<double>(Radius) - static_cast<double>(10.0f)) *
                           static_cast<double>(0.15f) /
                           (static_cast<double>(100.0f) - static_cast<double>(10.0f));
    float Scale = static_cast<float>(static_cast<double>(0.2f) - Falloff);

    // The RGBA bytes as A:R:G:B and the same with alpha 0.
    unsigned char const* const Rgba = reinterpret_cast<unsigned char const*>(Color);
    unsigned long EdgeColor = (static_cast<unsigned long>(Rgba[0]) << 16) |
                              (static_cast<unsigned long>(Rgba[1]) << 8) |
                              static_cast<unsigned long>(Rgba[2]);
    unsigned long LineColor = (static_cast<unsigned long>(Rgba[3]) << 24) | EdgeColor;

    if (bHostile)
    {
        // The four corners cut off diagonally.
        Vector const CutA = BracketCutPointRounded(CornerNN, CornerPN, Scale);
        Vector const CutB = BracketCutPoint(CornerNN, CornerNP, Scale);
        DrawBracketLine(CutA, CutB, LineColor, EdgeColor);
        Vector const LeftLow = CutB;

        Vector const CutC = BracketCutPoint(CornerPP, CornerNP, Scale);
        Vector const CutD = BracketCutPoint(CornerPP, CornerPN, Scale);
        DrawBracketLine(CutC, CutD, LineColor, EdgeColor);
        Vector const RightHigh = MakeVector(CutD.x, CutD.y + 1.0f, CutD.z);      // DAT_005c75dc

        Vector const CutE = BracketCutPoint(CornerPN, CornerNN, Scale);
        Vector const CutF = BracketCutPoint(CornerPN, CornerPP, Scale);
        DrawBracketLine(CutE, CutF, LineColor, EdgeColor);
        Vector const RightLow = CutF;

        Vector const CutG = BracketCutPoint(CornerNP, CornerNN, Scale);
        Vector const CutH = BracketCutPoint(CornerNP, CornerPP, Scale);
        Vector const LeftHigh = MakeVector(CutG.x, CutG.y + 1.0f, CutG.z);
        DrawBracketLine(CutG, CutH, LineColor, EdgeColor);

        // A watch-listed hostile also gets the two side bars between the cuts.
        if (bWatchListed)
        {
            DrawBracketLine(LeftLow, LeftHigh, LineColor, EdgeColor);
            DrawBracketLine(RightLow, RightHigh, LineColor, EdgeColor);
        }
    }
    else
    {
        // An angle at each corner -- the arms run from the cut into the
        // corner or from the corner out to the cut, in retail's order.
        DrawBracketLine(BracketCutPoint(CornerNN, CornerPN, Scale), CornerNN, LineColor, EdgeColor);
        DrawBracketLine(CornerNN, BracketCutPoint(CornerNN, CornerNP, Scale), LineColor, EdgeColor);
        DrawBracketLine(BracketCutPoint(CornerPP, CornerNP, Scale), CornerPP, LineColor, EdgeColor);
        DrawBracketLine(CornerPP, BracketCutPoint(CornerPP, CornerPN, Scale), LineColor, EdgeColor);
        DrawBracketLine(BracketCutPoint(CornerPN, CornerNN, Scale), CornerPN, LineColor, EdgeColor);
        DrawBracketLine(CornerPN, BracketCutPoint(CornerPN, CornerPP, Scale), LineColor, EdgeColor);
        DrawBracketLine(BracketCutPoint(CornerNP, CornerNN, Scale), CornerNP, LineColor, EdgeColor);
        DrawBracketLine(CornerNP, BracketCutPoint(CornerNP, CornerPP, Scale), LineColor, EdgeColor);
    }

    // The side marks.
    if (Record->bPlayerShip != 0)
    {
        // Watch-listed, they are blue.
        if (bWatchListed)
        {
            LineColor = (static_cast<unsigned long>(Rgba[3]) << 24) | 0xffu;
            EdgeColor = 0xffu;
        }

        // Arm = Scale * R; the mark is 2 * Arm wide and its inner ends close in by a quarter of
        // that (DAT_005cbf58 0.25). Width and Inset come from the unrounded double.
        double const ArmExact = static_cast<double>(Scale) * static_cast<double>(Radius);
        Scale = static_cast<float>(ArmExact);
        float Width = static_cast<float>(ArmExact + ArmExact);
        float const Inset = static_cast<float>((ArmExact + ArmExact) * 0.25);
        float const NegArm = -Scale;

        // The left mark, bar then its two slants.
        Vector const LeftBottom = MakeVector(Screen.x + NegRadius, NegArm + Screen.y, Screen.z);
        Vector const LeftTop = MakeVector(Screen.x + NegRadius, Screen.y + Scale, Screen.z);
        DrawBracketLine(LeftBottom, LeftTop, LineColor, EdgeColor);
        DrawBracketLine(LeftBottom, MakeVector(LeftBottom.x + Width, LeftBottom.y + Inset, Screen.z),
                        LineColor, EdgeColor);
        float const NegInset = -Inset;
        DrawBracketLine(LeftTop, MakeVector(LeftTop.x + Width, LeftTop.y + NegInset, Screen.z),
                        LineColor, EdgeColor);

        // The right mark, mirrored.
        Vector const RightTop = MakeVector(Screen.x + Radius, Screen.y + Scale, Screen.z);
        Vector const RightBottom = MakeVector(Screen.x + Radius, NegArm + Screen.y, Screen.z);
        DrawBracketLine(RightBottom, RightTop, LineColor, EdgeColor);
        Width = -Width;
        DrawBracketLine(RightBottom, MakeVector(Width + RightBottom.x, RightBottom.y + Inset, Screen.z),
                        LineColor, EdgeColor);
        DrawBracketLine(RightTop, MakeVector(Width + RightTop.x, RightTop.y + NegInset, Screen.z),
                        LineColor, EdgeColor);
    }
    else if (bWatchListed)
    {
        // A watch-listed object's marks close to a point: the slants end Arm in from each bar
        // end, 2 * Arm across (the colours are rebuilt from Color, the same values as above).
        double const ArmExact = static_cast<double>(Scale) * static_cast<double>(Radius);
        Scale = static_cast<float>(ArmExact);
        float const Width = static_cast<float>(ArmExact + ArmExact);
        float const NegArm = -Scale;

        Vector const LeftTop = MakeVector(NegRadius + Screen.x, Screen.y + Scale, Screen.z);
        Vector const LeftBottom = MakeVector(NegRadius + Screen.x, NegArm + Screen.y, Screen.z);
        DrawBracketLine(LeftBottom, LeftTop, LineColor, EdgeColor);
        DrawBracketLine(LeftBottom, MakeVector(LeftBottom.x + Width, LeftBottom.y + Scale, Screen.z),
                        LineColor, EdgeColor);
        DrawBracketLine(LeftTop, MakeVector(LeftTop.x + Width, LeftTop.y + NegArm, Screen.z),
                        LineColor, EdgeColor);

        Vector const RightTop = MakeVector(Screen.x + Radius, Screen.y + Scale, Screen.z);
        Vector const RightBottom = MakeVector(Screen.x + Radius, NegArm + Screen.y, Screen.z);
        DrawBracketLine(RightBottom, RightTop, LineColor, EdgeColor);
        float const NegWidth = -Width;
        DrawBracketLine(RightBottom, MakeVector(NegWidth + RightBottom.x, RightBottom.y + Scale, Screen.z),
                        LineColor, EdgeColor);
        DrawBracketLine(RightTop, MakeVector(NegWidth + RightTop.x, RightTop.y + NegArm, Screen.z),
                        LineColor, EdgeColor);
    }
}

void HudTargetableObjects::DrawContactMarkers()
{
    // FUN_004ebf70.
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());   // FUN_0054baf0
    if (Scanner == nullptr || Player == nullptr)
        return;
    CShip* const PlayerShip = static_cast<CShip*>(PlayerObjectOfClass(Player, CObject::ClassShip));
    if (PlayerShip == nullptr)
        return;

    unsigned TargetId = 0;
    IObjRW* const ShipTarget = PlayerShip->get_target();
    if (ShipTarget != nullptr)
        TargetId = ShipTarget->get_id();

    // Flat, unlit, blended 2D state.
    Win3DDisable(kWin3DLightingCap);   // FUN_00421ed0
    Win3DDisable(kWin3DTexture2D);
    Win3DDisable(kWin3DCullFace);
    Win3DDisable(kWin3DDepthTest);
    Win3DDisable(kWin3DFog);
    EngineLightClearRegisteredList();  // FUN_004101e0
    DALib::Pipeline->SetRenderState(D3DRS_AMBIENT, -1);   // white
    DALib::Pipeline->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);    // none
    Win3DEnable(kWin3DBlend);                    // FUN_00421dd0
    Win3DSetBlendFunc(kWin3DBlendSrcAlpha, kWin3DBlendInvSrcAlpha);   // FUN_004220c0

    // Every scanner entry with a record (the count re-read each pass).
    for (int Index = 0; Index < ScanEntries->Count; ++Index)
    {
        // A watched object, a record with type bits, not the ship's target and not the best-path
        // waypoint's object.
        Watchable const* const Watched = ScanEntries->Entries[Index].target();
        if (Watched == nullptr)
            continue;
        IObjRW* const Contact = ObjRwFromWatchable(const_cast<Watchable*>(Watched));
        if (Contact == nullptr)
            continue;
        FHudContactRecord const& Record = Contacts.data()[Index];
        if (Record.TypeFlags == 0)
            continue;
        if (Contact->get_id() == TargetId)
            continue;
        if (Record.bIsBestPath != 0)
            continue;

        // The idle cyan; with the player in ObjDB bucket 1 its reputation colour (FUN_0045a4e0)
        // and whether the contact is hostile (FUN_0045a490 == -1).
        unsigned long Color = G_NnPaletteCyan;
        bool bHostile = false;
        if (static_cast<FClientGameobj*>(Player)->ManagerKind == EClientGameobjKind::Ship)
        {
            unsigned long Reputation = 0;
            HudReputationColor(&Reputation, Player, Contact);
            Color = Reputation;
            if (GetAttitudeClass(Player, Contact) == -1)
                bHostile = true;
        }

        // Type 0x10000000 is white.
        unsigned const TypeFlags = Record.TypeFlags;
        unsigned const WhiteTypeBit = TypeFlags & OBJ_LOOT;
        if (WhiteTypeBit != 0)
            Color = 0xffffffffu;

        unsigned char* const ColorBytes = reinterpret_cast<unsigned char*>(&Color);
        if (Record.bHot != 0)
        {
            // The contact under the mouse pulses, unless picking is blocked (FUN_0045a740).
            if (!HudIsMousePickBlocked())
            {
                unsigned long Pulsed = 0;
                NnUiPulseBlendFromArgb(&Pulsed, Color);   // FUN_00561f20
                Color = Pulsed;
            }
        }
        else
        {
            // One of HUD_Comm's watched objects is amber.
            if (HudCommIsWatchedObjectId(Contact->get_id()))   // FUN_004cc880
                Color = G_NnPaletteAmber;

            // The alpha fades with distance (DAT_005c9804, DAT_005d954c), halved for type
            // 0x10000000 (DAT_005c75e0), clamped to [0, 1] with NaN left as is, and the alpha
            // byte scaled by it (ftol truncates).
            double Fade = static_cast<double>(1.25f) -
                          static_cast<double>(Record.Distance) * static_cast<double>(0.0004f);
            if (WhiteTypeBit != 0)
                Fade *= static_cast<double>(0.5f);
            if (Fade < 0.0)
                Fade = 0.0;
            else if (Fade > 1.0)
                Fade = 1.0;
            ColorBytes[3] = static_cast<unsigned char>(
                static_cast<int>(static_cast<double>(static_cast<int>(ColorBytes[3])) * Fade));
        }

        // The bracket for types 0x107f0000, the best-path object and watch-listed objects.
        if ((TypeFlags & (OBJ_SHIP_MASK | OBJ_LOOT)) != 0 || Record.bIsBestPath != 0 || Record.bWatchListed != 0)
            DrawContactBracket(&Record, &Color, bHostile, Record.bWatchListed != 0);

        // An on-screen contact the ship may lead: of types 0x7f0000 only
        // when hostile; of type 0x1000000 only when its own target is the player; of type
        // 0x4000000 always; and only when CShip::get_tgt_lead_fire_pos finds a position.
        if (Record.bOnScreen == 0)
            continue;
        if ((TypeFlags & OBJ_SHIP_MASK) != 0)
        {
            if (!bHostile)
                continue;
        }
        else if ((TypeFlags & OBJ_GUIDED) != 0)
        {
            IObjRW* ContactTarget = nullptr;
            Contact->get_target(ContactTarget);
            if (ContactTarget == nullptr)
                continue;
            if (ContactTarget->get_id() != Player->get_id())
                continue;
        }
        else if ((TypeFlags & OBJ_MINE) == 0)
        {
            continue;
        }
        Vector Lead;
        if (!PlayerShip->get_tgt_lead_fire_pos(Contact, Lead))
            continue;

        // The marker: a watcher on the contact (retail goes through a stack watcher it then
        // releases), the lead position and its projection through the listener camera. Retail
        // projects into an uninitialised stack vector and does not test the result (a failed
        // Project writes nothing); the port starts it at zero.
        Vector LeadScreen;
        LeadScreen.x = 0.0f;
        LeadScreen.y = 0.0f;
        LeadScreen.z = 0.0f;
        ICamera* const Camera = static_cast<ICamera*>(GetClientSoundListenerObject());   // FUN_0040f130
        Camera->ProjectVec(&LeadScreen, &Lead);
        LeadMarkers.emplace_back();
        FHudLeadMarker& Marker = LeadMarkers.back();
        Marker.Target.set(Contact);
        Marker.LeadWorld = Lead;
        Marker.LeadScreen = LeadScreen;
    }
}

namespace
{
    // FUN_004ee260's label point (inlined three times): a screen point onto the Win3D camera's
    // near plane (DAT_0067dbf8; each coordinate rounded to float), then scaled by -1.0 / z
    // (DAT_005d5060) to depth 1. ScreenY arrives as an unrounded double.
    Vector ReticleLabelPoint(double ScreenX, double ScreenY)
    {
        FSpaceCamera const* const Ui = Win3DGetCamera();
        float const X = static_cast<float>(
            (ScreenX - static_cast<double>(Ui->ScreenCenterX)) *
            (static_cast<double>(Ui->ZNear) / static_cast<double>(Ui->XScale)));
        float const Y = static_cast<float>(
            (ScreenY - static_cast<double>(Ui->ScreenCenterY)) *
            (static_cast<double>(Ui->ZNear) / static_cast<double>(Ui->YScale)));
        float const Z = -Ui->ZNear;
        double const Scale = -1.0 / static_cast<double>(Z);
        Vector Out;
        Out.x = static_cast<float>(static_cast<double>(X) * Scale);
        Out.y = static_cast<float>(static_cast<double>(Y) * Scale);
        Out.z = static_cast<float>(Scale * static_cast<double>(Z));
        return Out;
    }

    // The label draw FUN_004ee260 inlines three times: the range text control's Font::String
    // (FUN_0058a5d0 / FUN_0058a5e0) is swapped for Label for one draw, then put back.
    void DrawReticleLabel(WinControl* TextControl, FFontString* Label, Vector const& Point)
    {
        NnTextControl* const Text = static_cast<NnTextControl*>(TextControl);
        FFontString* const Saved = Text->FontString;
        Text->FontString = Label;
        Text->SetProperty(kWinPropPosition, reinterpret_cast<uintptr_t>(&Point), 0);
        Text->Draw();
        Text->FontString = Saved;
    }

    // All three components equal (ordered; NaN is never equal).
    bool SameWorldPosition(Vector const& A, Vector const& B)
    {
        return A.x == B.x && A.y == B.y && A.z == B.z;
    }
}

void HudTargetableObjects::DrawWaypointMarker(FHudContactRecord const* Record,
                                              unsigned long const* Color, int /*Unused*/)
{
    // FUN_004ebde0. The midpoints of the four edges.
    float const Radius = Record->ScreenRadius;
    Vector const& Screen = Record->Screen;
    float const NegRadius = -Radius;
    Vector const Top = MakeVector(Screen.x, Screen.y + Radius, Screen.z);
    Vector const Right = MakeVector(Screen.x + Radius, Screen.y, Screen.z);
    Vector const Bottom = MakeVector(Screen.x, NegRadius + Screen.y, Screen.z);
    Vector const Left = MakeVector(NegRadius + Screen.x, Screen.y, Screen.z);

    // RGBA -> A:R:G:B and R:G:B.
    unsigned char const* const Rgba = reinterpret_cast<unsigned char const*>(Color);
    unsigned long const EdgeColor = (static_cast<unsigned long>(Rgba[0]) << 16) |
                                    (static_cast<unsigned long>(Rgba[1]) << 8) |
                                    static_cast<unsigned long>(Rgba[2]);
    unsigned long const LineColor = (static_cast<unsigned long>(Rgba[3]) << 24) | EdgeColor;
    DrawBracketLine(Top, Right, LineColor, EdgeColor);
    DrawBracketLine(Right, Bottom, LineColor, EdgeColor);
    DrawBracketLine(Bottom, Left, LineColor, EdgeColor);
    DrawBracketLine(Left, Top, LineColor, EdgeColor);
}

void HudTargetableObjects::DrawTargetReticle()
{
    // FUN_004ee260. The range text shown for the draws below, the forced pick cleared, the Win3D
    // camera applied (FUN_00422950).
    ReticleRangeText->SetProperty(kWinPropHide, 0, 0);
    G_HudPickOverrideId = 0;
    Win3DApplyCamera(AsCamera(Win3DGetCamera()));
    bool bTargetIsBestPath = false;
    bool bTargetSeen = false;
    bool bTargetIsWaypoint = false;   // either waypoint

    // Without a scanner or a player the text is hidden again; without a player ship nothing
    // else happens -- not even that.
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());   // FUN_0054baf0
    if (Scanner == nullptr || Player == nullptr)
    {
        ReticleRangeText->SetProperty(kWinPropHide, 1, 0);
        return;
    }
    CShip* const PlayerShip = static_cast<CShip*>(PlayerObjectOfClass(Player, CObject::ClassShip));
    if (PlayerShip == nullptr)
        return;

    // The ship's target (its id restarts the select pulse when it changes), and whether it sits
    // exactly on the nav waypoint or the best-path waypoint.
    unsigned TargetId = 0;
    bool bWaypointDone = false;
    IObjRW* const Target = PlayerShip->get_target();
    if (Target != nullptr)
    {
        TargetId = Target->get_id();
        if (LastTargetId != TargetId)
        {
            SelectPulse = 0.0f;
            LastTargetId = TargetId;
        }
        if (WaypointRecord.bValid != 0 &&
            SameWorldPosition(Target->get_position(), WaypointRecord.World))
        {
            bWaypointDone = true;
            bTargetSeen = true;
            bTargetIsWaypoint = true;
        }
        if (BestPathRecord.bValid != 0 &&
            SameWorldPosition(Target->get_position(), BestPathRecord.World))
        {
            bTargetIsBestPath = true;
            bTargetSeen = true;
            bTargetIsWaypoint = true;
        }
    }
    else
    {
        LastTargetId = 0;
    }

    // The nav waypoint (unless it is the target): on screen its diamond
    // (DAT_00679b80) and label 1.1 radii above, else its edge arrow.
    if (!bWaypointDone && WaypointRecord.bValid != 0)
    {
        if (WaypointRecord.bOnScreen != 0)
        {
            DrawWaypointMarker(&WaypointRecord, &G_NnPaletteViolet, 0);
            double const LabelY = static_cast<double>(WaypointRecord.Screen.y) -
                                  static_cast<double>(WaypointRecord.ScreenRadius) * 1.1;   // DAT_005d9618
            Vector const Point =
                ReticleLabelPoint(static_cast<double>(WaypointRecord.Screen.x), LabelY);
            DrawReticleLabel(ReticleRangeText, &WaypointLabel, Point);
        }
        else
        {
            DrawWaypointArrow(&WaypointRecord);
        }
    }

    // The best-path waypoint the same way (unless it is the target).
    if (!bTargetIsBestPath && BestPathRecord.bValid != 0)
    {
        if (BestPathRecord.bOnScreen != 0)
        {
            DrawWaypointMarker(&BestPathRecord, &G_NnPaletteViolet, 1);
            double const LabelY = static_cast<double>(BestPathRecord.Screen.y) -
                                  static_cast<double>(BestPathRecord.ScreenRadius) * 1.1;
            Vector const Point =
                ReticleLabelPoint(static_cast<double>(BestPathRecord.Screen.x), LabelY);
            DrawReticleLabel(ReticleRangeText, &BestPathLabel, Point);
        }
        else
        {
            DrawWaypointArrow(&BestPathRecord);
        }
    }

    // Every scanner entry (the count re-read each pass).
    for (int Index = 0; Index < ScanEntries->Count; ++Index)
    {
        Watchable const* const Watched = ScanEntries->Entries[Index].target();
        IObjRW* const Contact =
            (Watched != nullptr) ? ObjRwFromWatchable(const_cast<Watchable*>(Watched)) : nullptr;
        FHudContactRecord const& Record = Contacts.data()[Index];
        if (Contact == nullptr)
            continue;
        bool const bIsTarget = Contact->get_id() == TargetId;
        if (bIsTarget)
            bTargetSeen = true;

        // Off screen, its edge arrow (retail compares the id again).
        if (Record.bOnScreen == 0)
        {
            DrawOffScreenContact(Contact, &Record, Contact->get_id() == TargetId);
            continue;
        }
        // The target on screen gets its reticle, unless it is a waypoint (drawn after the loop).
        if (bIsTarget)
        {
            if (!bTargetIsWaypoint)
                DrawOnScreenTarget(Contact, &Record);
            continue;
        }

        // A player ship nearer than 3000 (DAT_005d95a8; ordered "below" only) gets its name label
        // 2 pixels above its bracket.
        if (Record.bPlayerShip == 0)
            continue;
        if (!(Record.Distance < 3000.0f))
            continue;
        double const LabelY = static_cast<double>(Record.Screen.y) -
                              static_cast<double>(Record.ScreenRadius) -
                              static_cast<double>(2.0f);   // DAT_005d55b4
        Vector const Point = ReticleLabelPoint(static_cast<double>(Record.Screen.x), LabelY);
        CObject const* const Object = Contact->cobject();
        if (Object == nullptr || (static_cast<unsigned>(Object->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
            continue;
        CShip const* const Ship = static_cast<CShip const*>(Object);

        // The label cache, keyed by the ship's id. A new entry gets "<faction short name> <pilot
        // name>" in the PlayerName font at 0.171.
        unsigned const Key = Ship->Id;
        std::map<unsigned, FHudNameLabel>::iterator It = NameLabels.find(Key);   // FUN_00432240
        if (It == NameLabels.end())
        {
            // Retail inserts a copy of a fresh stack Font::String (FUN_004f2910 / FUN_004f2a80)
            // and looks the key up again; the port constructs the node's string fresh.
            NameLabels.emplace(std::piecewise_construct, std::forward_as_tuple(Key),
                               std::forward_as_tuple());
            It = NameLabels.find(Key);
            FFontString* const Label = &It->second.Text;

            // The faction short name of the ship's vibe, unless the affiliation query fails or
            // the ids is 0, -1, 0x30386 or 0x303b8; a lone space is dropped, anything else is
            // followed by " - ".
            unsigned ShortNameIds = 0;
            int const Vibe = static_cast<int>(static_cast<CEqObj const*>(Ship)->VibeId);
            unsigned Affiliation = 0;
            if (Reputation::Vibe::GetAffiliation(Vibe, Affiliation, false) == 0)
                ShortNameIds = Reputation::get_short_name(Affiliation);
            wchar_t Text[0x100];
            Text[0] = 0;
            if (ShortNameIds != 0 && ShortNameIds != 0xffffffffu && ShortNameIds != 0x30386u &&
                ShortNameIds != 0x303b8u)
            {
                wchar_t Faction[0x80];
                if (GetIdsStringWWithRawFallback(ShortNameIds, Faction, 0x80) == 0)   // FUN_004347e0
                    Faction[0] = 0;
                if (wcslen(Faction) > 1 || Faction[0] != L' ')
                {
                    wcscat(Text, Faction);
                    wcscat(Text, L" - "); // DAT_005d01f4
                }
            }
            wcscat(Text, Ship->get_pilot_name());

            Label->SetFont(PlayerNameFont);   // inlined FUN_004ea670
            Label->SetScale(0.171f, -1.0f);   // DAT_005d8f0c
            Label->SetText(Text);
        }

        DrawReticleLabel(ReticleRangeText, &It->second.Text, Point);
    }

    // The target is drawn here unless the scan already drew it (seen and not a waypoint).
    if (bTargetSeen && !bTargetIsWaypoint)
    {
        ReticleRangeText->SetProperty(kWinPropHide, 1, 0);
        return;
    }

    // A fresh record for the ship's target; the watch-list byte only for types 0x107ffdff.
    IObjRW* const ShipTarget = PlayerShip->get_target();
    if (ShipTarget == nullptr)
    {
        ReticleRangeText->SetProperty(kWinPropHide, 1, 0);
        return;
    }
    FHudContactRecord TargetRecord;
    std::memset(&TargetRecord, 0, sizeof(TargetRecord));
    TargetRecord.bValid = 1;
    ShipTarget->get_type(TargetRecord.TypeFlags);
    TargetRecord.World = ShipTarget->get_position();
    if ((TargetRecord.TypeFlags & (OBJ_SOLAR_NO_WAYPOINT_MASK | OBJ_SHIP_MASK | OBJ_LOOT)) != 0)
    {
        FClientGameobj* const Gameobj = ClientObjDb_FindObject(ShipTarget->get_id(), 0);   // FUN_005416c0
        if (Gameobj != nullptr)
            TargetRecord.bWatchListed = Gameobj->bWatchListed;
    }
    CObject const* const TargetObject = ShipTarget->cobject();
    if (TargetObject != nullptr &&
        (static_cast<unsigned>(TargetObject->ObjectClass) & CObject::ClassShip) == CObject::ClassShip &&
        static_cast<CShip const*>(TargetObject)->bIsPlayer != 0)
    {
        TargetRecord.bPlayerShip = 1;
    }

    // The screen radius and distance land in locals that are copied into the record only on the
    // paths below (retail reuses the radius's own stack slot for the screen radius).
    Vector const Center = ShipTarget->get_center_of_mass();
    Vector PartCenter;
    float ProjectedRadius = ShipTarget->cobject()->get_physical_radius_r(PartCenter);
    float Distance = 0.0f;
    Vector Screen;
    bool const bOnScreen =
        ProjectContact(&Center, ProjectedRadius, &Screen, &ProjectedRadius, &Distance);

    // On screen the record takes the projection, radius and distance, off screen only the
    // distance.
    if (bOnScreen)
    {
        TargetRecord.Screen = Screen;
        TargetRecord.ScreenRadius = ProjectedRadius;
    }
    TargetRecord.Distance = Distance;
    TargetRecord.bIsBestPath = static_cast<unsigned char>(
        (BestPathRecord.bValid != 0 &&
         SameWorldPosition(ShipTarget->get_position(), BestPathRecord.World)) ? 1 : 0);
    TargetRecord.bIsWaypoint = static_cast<unsigned char>(
        (WaypointRecord.bValid != 0 &&
         SameWorldPosition(ShipTarget->get_position(), WaypointRecord.World)) ? 1 : 0);
    if (bOnScreen)
        DrawOnScreenTarget(ShipTarget, &TargetRecord);
    else
        DrawOffScreenContact(ShipTarget, &TargetRecord, true);

    ReticleRangeText->SetProperty(kWinPropHide, 1, 0);
}

void HudTargetableObjects::DrawWaypointArrow(FHudContactRecord const* Record)
{
    // FUN_004f0620. FUN_0054baf0's result is not used.
    GetPlayerObjRW();

    // The waypoint in the listener camera's frame: its orientation and position (read as one
    // transform) inverted onto World by DAT_00674b6c.
    ICamera* const Listener = static_cast<ICamera*>(GetClientSoundListenerObject());   // FUN_0040f130
    const float* const Frame = (const float*)Listener->GetOrientation();
    if (G_HudObjectsMath == nullptr)
        G_HudObjectsMath = Acquire3DMathEngine();   // FUN_005b73e0
    Vector Local;
    G_HudObjectsMath->InverseTransform(Local, *reinterpret_cast<Transform const*>(Frame),
                                       Record->World);

    // Retail also builds this rotation product and copies it next to the arrow position; nothing
    // reads that copy.
    Vector Flat;
    Flat.x = Local.x;
    Flat.y = Local.y;
    Flat.z = 0.0f;
    Vector Spin;
    Spin.x = 0.0f;
    Spin.y = 0.0f;
    Spin.z = 180.0f;
    Matrix const Rotation = RotateMatrix(Spin);
    Matrix const Look = LookMatrix(Flat);
    if (G_HudObjectsMath == nullptr)
        G_HudObjectsMath = Acquire3DMathEngine();
    Matrix ArrowOrientation;
    G_HudObjectsMath->MulMatrix(ArrowOrientation, Look,
                                Rotation);

    // The arrow sits where the direction leaves the view, at depth -1.3 (DAT_005d8f04): through
    // a side edge when |y| * aspect is below |x| (ordered), at x = +-tan(HalfFovX deg) * 1.3 *
    // 0.945 (DAT_005c77f4 deg to rad, DAT_005d9654) with y on the line (negative unless x > 0,
    // NaN included); otherwise through the top or bottom at y = +-tan(FovY deg) * 1.3 * 0.945
    // with x on the line.
    FSpaceCamera const* const Ui = Win3DGetCamera();
    double const AbsX = std::fabs(static_cast<double>(Local.x));
    double const AbsYAspect =
        std::fabs(static_cast<double>(Local.y)) * static_cast<double>(Ui->Aspect);   // DAT_0067dc98
    Vector ArrowPos;
    ArrowPos.z = -1.3f;
    if (AbsYAspect < AbsX)
    {
        double Edge = std::tan(static_cast<double>(Ui->HalfFovXDeg) *
                               static_cast<double>(0.0174533f)) *   // DAT_0067dc90
                      static_cast<double>(-(-1.3f)) * static_cast<double>(0.945f);
#ifdef FREELANCER_WIDESCREEN_SUPPORT
        // Port-only Hor+: +0x98 is the 4:3 angle; the screen edge is the widened one.
        Edge *= SpaceCameraHorPlusScale(Ui);
#endif
        if (!(Local.x > 0.0f))
            Edge = -Edge;
        ArrowPos.y = static_cast<float>(
            (static_cast<double>(Local.y) / static_cast<double>(Local.x)) * Edge);
        ArrowPos.x = static_cast<float>(Edge);
    }
    else
    {
        double Edge = std::tan(static_cast<double>(Ui->FovYDeg) *
                               static_cast<double>(0.0174533f)) *   // DAT_0067dc94
                      static_cast<double>(-(-1.3f)) * static_cast<double>(0.945f);
        if (!(Local.y > 0.0f))
            Edge = -Edge;
        ArrowPos.y = static_cast<float>(Edge);
        ArrowPos.x = static_cast<float>(
            static_cast<double>(ArrowPos.y) /
            (static_cast<double>(Local.y) / static_cast<double>(Local.x)));
    }

    // With both waypoint tiles, the background and the arrow (FUN_00559900).
    if (WaypointArrow != nullptr && WaypointArrowBackground != nullptr)
    {
        unsigned char const BackgroundRgba[4] = {0x0c, 0x3e, 0x7a, 0xff};
        unsigned long const ArrowRgba = G_NnPaletteViolet;
        NnMontageDrawTileAt(WaypointArrowBackground, &ArrowPos.x, BackgroundRgba, 0.06f, 0, 0.0f);
        NnMontageDrawTileAt(WaypointArrow, &ArrowPos.x,
                            reinterpret_cast<unsigned char const*>(&ArrowRgba), 0.06f, 0, 0.0f);
    }

    // The arrow's point through the Win3D camera; when the cursor lies within the pick radius of
    // it (DAT_005c7500, DAT_005d9650; "farther" or NaN leaves), the record becomes the hover pick
    // and the forced pick is cleared.
    float ScreenX = 0.0f;
    float ScreenY = 0.0f;
    float ScreenZ = 0.0f;
    if ((AsCamera(Win3DGetCamera())->Project(&ScreenX, &ScreenY, &ScreenZ, &ArrowPos) & 0xff) == 0)
        return;
    int CursorX = 0;
    int CursorY = 0;
    int CursorZ = 0;
    GetClientSoftwareCursor(&CursorX, &CursorY, &CursorZ);
    float const ScreenZCopy = ScreenZ;
    float const DeltaX = static_cast<float>(static_cast<double>(ScreenX) - CursorX);
    double const DeltaY = static_cast<double>(ScreenY) - CursorY;
    double const DeltaZ = static_cast<double>(ScreenZ) - static_cast<double>(ScreenZCopy);
    double const CursorDistance =
        std::sqrt(DeltaZ * DeltaZ + DeltaY * DeltaY +
                  static_cast<double>(DeltaX) * static_cast<double>(DeltaX));
    double const PickRadius = static_cast<double>(-1.0f) / static_cast<double>(-1.3f) *
                              static_cast<double>(0.02733f) / static_cast<double>(Ui->ZNear) *
                              static_cast<double>(Ui->ZNear) * static_cast<double>(Ui->XScale);
    if (!(PickRadius >= CursorDistance))
        return;
    G_HudHoverRecord = Record;
    G_HudPickOverrideId = 0;
}

namespace
{
    // DAT_00674b54..DAT_00674b5c -- a zero vector (static initialiser LAB_004e8000). Its only
    // readers are FUN_004ef030's two FUN_004eef50 calls, which pass its address as an argument
    // FUN_004eef50 never reads.
    Vector const G_HudZeroVector = MakeVector(0.0f, 0.0f, 0.0f);

    // One reticle bar (inlined eight times): four vertices in the
    // reticle's x / y plane at z = 0 -- Left/Bottom, Right/Bottom, Right/Top, Left/Top, Right
    // being Fraction * Width + Left for a fill (bFill), else Left + Width.
    void EmitReticleBarQuad(FHudMeshRect const& Rect, float Fraction, bool bFill)
    {
        float Right;
        if (bFill)
            Right = static_cast<float>(static_cast<double>(Fraction) * static_cast<double>(Rect.Width) +
                                       static_cast<double>(Rect.Left));
        else
            Right = static_cast<float>(static_cast<double>(Rect.Left) +
                                       static_cast<double>(Rect.Width));
        Win3DVertex3f(Rect.Left, Rect.Bottom, 0.0f);   // FUN_00421cc0
        Win3DVertex3f(Right, Rect.Bottom, 0.0f);
        Win3DVertex3f(Right, Rect.Top, 0.0f);
        Win3DVertex3f(Rect.Left, Rect.Top, 0.0f);
    }

    // FUN_00421b70 with a palette colour's R, G, B bytes (whole, or each * 3 / 4 as a signed
    // division) and alpha 0xff.
    void SetReticleBarColor(unsigned long Argb, bool bDimmed)
    {
        unsigned char const* const Bytes = reinterpret_cast<unsigned char const*>(&Argb);
        if (bDimmed)
        {
            Win3DColor4ub(static_cast<unsigned char>((static_cast<int>(Bytes[0]) * 3) / 4),
                          static_cast<unsigned char>((static_cast<int>(Bytes[1]) * 3) / 4),
                          static_cast<unsigned char>((static_cast<int>(Bytes[2]) * 3) / 4), 0xff);
        }
        else
        {
            Win3DColor4ub(Bytes[0], Bytes[1], Bytes[2], 0xff);
        }
    }

    // A copy of P with its y replaced -- the label points of FUN_004ef030 all keep x / z.
    Vector WithY(Vector const& P, float Y)
    {
        return MakeVector(P.x, Y, P.z);
    }
}

void HudTargetableObjects::DrawRangeLabel(void const* /*Unused1*/, float Radius,
                                          FHudContactRecord const* /*Unused3*/, int Distance,
                                          Vector const* Point)
{
    // FUN_004eef50. With Radius > 0 (ordered) the distance to the surface, truncated, a negative
    // one read as 0.
    int Range = Distance;
    if (Radius > 0.0f)
    {
        Range = static_cast<int>(static_cast<double>(Distance) - static_cast<double>(Radius));
        if (Range < 0)
            Range = 0;
    }

    // Metres up to 2000, else kilometres with one decimal (signed divisions).
    char Text[0x100];
    if (Range <= 2000)
        sprintf(Text, "%d M", Range);                                // DAT_005d9628
    else
        sprintf(Text, "%d.%d K", Range / 1000, (Range % 1000) / 100); // s_%d.%d_K_005d9620

    // FUN_0058a580: Text goes through a Font::NumberString as a printf format.
    static_cast<NnTextControl*>(ReticleRangeText)->SetNumberText(Text);

    ReticleRangeText->SetProperty(kWinPropPosition, reinterpret_cast<uintptr_t>(Point), 0);
    ReticleRangeText->Draw();
}

void HudTargetableObjects::DrawOnScreenTarget(IObjRW* Object, FHudContactRecord const* Record)
{
    // FUN_004ef030. A ship-typed record (0x7f0000) needs a ship CObject whose vibe has an
    // affiliation.
    unsigned TypeBits = Record->TypeFlags;
    if ((TypeBits & OBJ_SHIP_MASK) != 0)
    {
        CObject const* const ShipObject = Object->cobject();
        if (ShipObject == nullptr)
            return;
        if ((static_cast<unsigned>(ShipObject->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
            return;
        int const Vibe = static_cast<int>(static_cast<CEqObj const*>(ShipObject)->VibeId);
        unsigned Affiliation = 0;
        if (Reputation::Vibe::GetAffiliation(Vibe, Affiliation, false) != 0)
            return;
    }

    // The HUD waypoint's object counts as a waypoint (type 0x200).
    bool bWaypointObject = false;
    if (HudIsWaypointObjectId(Object->get_id()))   // FUN_004d4220
    {
        TypeBits = OBJ_WAYPOINT;
        bWaypointObject = true;
    }
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    bool const bWatched = HudCommIsWatchedObjectId(Object->get_id());
    bool const bHat = Record->bWatchListed != 0 || Record->bIsBestPath != 0;

    // The reticle scale from the bracket size (DAT_005d8eec, DAT_005c89b8, DAT_005cbf58); the
    // corner and hat offsets at rest.
    float Scale = static_cast<float>(
        1.0 - (1.0 - static_cast<double>(Record->ScreenRadius) / static_cast<double>(100.0f)) * 0.25);
    float CornerX = 0.08f;
    float CornerY = 0.05f;
    float CornerZ = 0.0f;
    float HatX = 0.091f;
    float HatY = 0.0f;
    float HatZ = 0.0f;

    // For 0.3 s after a target change (DAT_005d8f08; ordered "below") the select pulse: the lock
    // sound on its first frame, the pulse advanced by the frame time (FUN_0042d680); while the
    // unrounded fraction is below 1 the scale eases in from 0.25 (only when above it) and both
    // offsets grow with the fraction (DAT_005cb890, DAT_005cb88c, DAT_005d964c; the zero
    // components become 0 * fraction).
    if (SelectPulse < 0.3f)
    {
        if (SelectPulse == 0.0f)
            PlayOrRewindUiSoundSlot(kUiSoundTargetExpandBrackets);
        double const Pulse = GetClientFrameTimeCapped() + static_cast<double>(SelectPulse);
        SelectPulse = static_cast<float>(Pulse);
        double const Fraction = Pulse / static_cast<double>(0.3f);
        if (Fraction < 1.0)
        {
            if (Scale > 0.25f)
                Scale = static_cast<float>((static_cast<double>(Scale) - static_cast<double>(0.25f)) *
                                               Fraction +
                                           static_cast<double>(0.25f));
            CornerX = static_cast<float>(static_cast<double>(0.08f) * Fraction);
            CornerY = static_cast<float>(static_cast<double>(0.05f) * Fraction);
            float const Zero = static_cast<float>(static_cast<double>(0.0f) * Fraction);
            CornerZ = Zero;
            HatX = static_cast<float>(Fraction * static_cast<double>(0.091f));
            HatY = Zero;
            HatZ = Zero;
        }
    }

    // The bracket centre on the Win3D camera's near plane, scaled to depth 1.
    Vector const P = ReticleLabelPoint(static_cast<double>(Record->Screen.x),
                                       static_cast<double>(Record->Screen.y));

    // The reticle transform -- the identity scaled by Scale at P.
    Transform Reticle;
    for (int Row = 0; Row < 3; ++Row)
    {
        for (int Column = 0; Column < 3; ++Column)
            Reticle.Rotation.E[Row][Column] = (Row == Column) ? 1.0f : 0.0f;
    }
    if (G_HudObjectsMath == nullptr)
        G_HudObjectsMath = Acquire3DMathEngine();   // FUN_005b73e0
    G_HudObjectsMath->Scale(Reticle.Rotation, Reticle.Rotation, Scale);
    Reticle.Translation.x = P.x;
    Reticle.Translation.y = P.y;
    Reticle.Translation.z = P.z;

    // Violet for a waypoint or the best-path object, else the marker colour; the HUD_Comm
    // speaker amber over either.
    bool const bWaypoint = (TypeBits & OBJ_WAYPOINT) != 0;
    unsigned long Color;
    if (bWaypoint || Record->bIsBestPath != 0)
    {
        Color = G_NnPaletteViolet;
    }
    else
    {
        unsigned long Marker = 0;
        Color = *HudTargetMarkerColor(&Marker, Object);
    }
    if (bWatched)
        Color = G_NnPaletteAmber;
    unsigned char const* const ColorRgba = reinterpret_cast<unsigned char const*>(&Color);

    if (!bWaypoint)
    {
        // The four corners at Scale * 0.04 (DAT_005d845c), unturned: +x+y mirrored (mode 1),
        // -x-y flipped (2), -x+y plain (0), +x-y both (3).
        if (CurrentTargetCorner != nullptr)
        {
            float const Size = static_cast<float>(static_cast<double>(Scale) * static_cast<double>(0.04f));
            float const NegCornerX = -CornerX;
            Vector const TopRight = MakeVector(CornerX + P.x, CornerY + P.y, CornerZ + P.z);
            NnMontageDrawTileAt(CurrentTargetCorner, &TopRight.x, ColorRgba, Size, 1, 0.0f);
            Vector const BottomLeft = MakeVector(P.x - CornerX, P.y - CornerY, P.z - CornerZ);
            NnMontageDrawTileAt(CurrentTargetCorner, &BottomLeft.x, ColorRgba, Size, 2, 0.0f);
            Vector const TopLeft = MakeVector(P.x + NegCornerX, P.y + CornerY, P.z + CornerZ);
            NnMontageDrawTileAt(CurrentTargetCorner, &TopLeft.x, ColorRgba, Size, 0, 0.0f);
            Vector const BottomRight = MakeVector(P.x - NegCornerX, P.y - CornerY, P.z - CornerZ);
            NnMontageDrawTileAt(CurrentTargetCorner, &BottomRight.x, ColorRgba, Size, 3, 0.0f);
        }
    }
    else if (WaypointTarget != nullptr)
    {
        // The waypoint tile at Scale * 0.25, its alpha from the centre distance (DAT_005cfc9c,
        // DAT_005c99c8, DAT_005d9648, DAT_005d9640, DAT_005c75d8); the fade is clamped to [0, 1]
        // with NaN left as is, and the alpha truncated.
        float const Size = static_cast<float>(static_cast<double>(Scale) * static_cast<double>(0.25f));
        double Fade = (static_cast<double>(P.y) * static_cast<double>(P.y) +
                       static_cast<double>(P.x) * static_cast<double>(P.x)) *
                          static_cast<double>(4.0f) -
                      static_cast<double>(0.01f);
        Fade = Fade * static_cast<double>(3.44828f);
        if (Fade > 1.0)
            Fade = 1.0;
        else if (Fade < 0.0)
            Fade = 0.0;
        int const Alpha = static_cast<int>((1.0 - Fade) * 150.0 + Fade * static_cast<double>(255.0f));
        unsigned long WaypointColor = Color;
        reinterpret_cast<unsigned char*>(&WaypointColor)[3] = static_cast<unsigned char>(Alpha);
        NnMontageDrawTileAt(WaypointTarget, &P.x,
                            reinterpret_cast<unsigned char const*>(&WaypointColor), Size, 0, 0.0f);
    }

    // The hat for a watch-listed / best-path non-waypoint: P - H plain, H + P mirrored. The tile
    // is not tested here (FUN_00559900 answers false for a null one).
    if (bHat && !bWaypoint)
    {
        Vector const HatLeft = MakeVector(P.x - HatX, P.y - HatY, P.z - HatZ);
        NnMontageDrawTileAt(CurrentTargetHat, &HatLeft.x, ColorRgba, 0.04f, 0, 0.0f);
        Vector const HatRight = MakeVector(HatX + P.x, HatY + P.y, HatZ + P.z);
        NnMontageDrawTileAt(CurrentTargetHat, &HatRight.x, ColorRgba, 0.04f, 1, 0.0f);
    }

    // The health bar for a CSimple unless it is a solar with a non-destructible archetype or its
    // TypeBits have 1 / 2.
    CObject const* const BarObject = Object->cobject();
    bool bHealthBar = false;
    if (BarObject != nullptr && (static_cast<unsigned>(BarObject->ObjectClass) & CObject::ClassSimple) == CObject::ClassSimple)
    {
        unsigned const Class = static_cast<unsigned>(BarObject->ObjectClass);
        bool bIndestructible = false;
        if ((Class & CObject::ClassSolar) == CObject::ClassSolar && BarObject->Archetype != nullptr &&
            !static_cast<Archetype::Solar const*>(BarObject->Archetype)->bDestructible)
        {
            bIndestructible = true;
        }
        if (!bIndestructible && (static_cast<CSimple const*>(BarObject)->TypeBits & (OBJ_MOON | OBJ_PLANET)) == 0)
            bHealthBar = true;
    }

    // Relative health, and for a CEqObj the shield (FUN_004d4b50) with its fraction only above a
    // positive maximum (ordered).
    bool bShieldActive = false;
    CSimple const* Simple = static_cast<CSimple const*>(Object->cobject());
    if (Simple != nullptr && (static_cast<unsigned>(Simple->ObjectClass) & CObject::ClassSimple) != CObject::ClassSimple)
        Simple = nullptr;
    float Health = 0.0f;
    float ShieldFraction = 0.0f;
    if (Simple != nullptr)
    {
        Health = Simple->get_relative_health();
        if ((static_cast<unsigned>(Simple->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj)
        {
            float ShieldMax = 0.0f;
            float ShieldCur = 0.0f;
            HudSampleShieldHitPoints(const_cast<CEqObj*>(static_cast<CEqObj const*>(Simple)),
                                     &ShieldMax, &ShieldCur, &bShieldActive);
            if (ShieldMax > 0.0f)
                ShieldFraction = static_cast<float>(static_cast<double>(ShieldCur) /
                                                    static_cast<double>(ShieldMax));
        }
    }

    // A waypoint's bars sit 0.003 right (DAT_005d3e90) and 0.04 down.
    if (bWaypoint)
    {
        Reticle.Translation.x = static_cast<float>(static_cast<double>(Reticle.Translation.x) +
                                                   static_cast<double>(0.003f));
        Reticle.Translation.y = static_cast<float>(static_cast<double>(Reticle.Translation.y) -
                                                   static_cast<double>(0.04f));
    }

    // The reticle transform times the Win3D camera's (DAT_0067dbfc), copied (FUN_00406880) and
    // bound as the model-view (FUN_00422690); quads, opaque black.
    if (G_HudObjectsMath == nullptr)
        G_HudObjectsMath = Acquire3DMathEngine();
    Transform Combined;
    G_HudObjectsMath->MulTransform(
        Combined, Reticle, Win3DGetCamera()->WorldXform);
    Transform const ModelView = Combined;
    Win3DBindModelview(nullptr, &ModelView);
    Win3DBegin(kWin3DQuads);          // FUN_00421670
    Win3DColor4ub(0, 0, 0, 0xff);     // FUN_00421b70

    // The black backs.
    if (bHealthBar)
        EmitReticleBarQuad(ReticleHealthRect, 0.0f, false);
    if (bShieldActive)
        EmitReticleBarQuad(ReticleShieldsRect, 0.0f, false);

    // The fills, health red and shield blue, each only above 0 (ordered).
    if (bHealthBar && Health > 0.0f)
    {
        SetReticleBarColor(G_NnPaletteRed, false);
        EmitReticleBarQuad(ReticleHealthRect, Health, true);
    }
    if (bShieldActive && ShieldFraction > 0.0f)
    {
        SetReticleBarColor(G_NnPaletteBlue, false);
        EmitReticleBarQuad(ReticleShieldsRect, ShieldFraction, true);
    }
    Win3DEnd();   // FUN_00421690

    // Each bar's frame, a line loop at 3/4 of its colour.
    if (bHealthBar)
    {
        Win3DBegin(kWin3DLineLoop);
        SetReticleBarColor(G_NnPaletteRed, true);
        EmitReticleBarQuad(ReticleHealthRect, 0.0f, false);
        Win3DEnd();
    }
    if (bShieldActive)
    {
        Win3DBegin(kWin3DLineLoop);
        SetReticleBarColor(G_NnPaletteBlue, true);
        EmitReticleBarQuad(ReticleShieldsRect, 0.0f, false);
        Win3DEnd();
    }

    // A waypoint record (the record's own type 0x200) gets the range at 0.4 of the reticle
    // height (DAT_005d9638) and the waypoint / best-path caption at 0.8 (DAT_005d8d40), both
    // scaled, above P -- and nothing else.
    unsigned const RecordType = Record->TypeFlags;
    double const ReticleHeight = static_cast<double>(ReticleRect.Top);
    double const ReticleWidth = static_cast<double>(ReticleRect.Width);
    if ((RecordType & OBJ_WAYPOINT) != 0)
    {
        Vector const RangePoint = WithY(
            P, static_cast<float>(ReticleHeight * static_cast<double>(0.4f) * static_cast<double>(Scale) +
                                  static_cast<double>(P.y)));
        DrawRangeLabel(&G_HudZeroVector, 0.0f, Record, static_cast<int>(Record->Distance),
                       &RangePoint);
        Vector const CaptionPoint = WithY(
            P, static_cast<float>(ReticleHeight * static_cast<double>(0.8f) * static_cast<double>(Scale) +
                                  static_cast<double>(P.y)));
        DrawReticleLabel(ReticleRangeText,
                         (Record->bIsWaypoint != 0) ? &WaypointLabel : &BestPathLabel, CaptionPoint);
        return;
    }

    // The range at (0.5 - 0.1) of the height (DAT_005c75e0, DAT_005c7720), measured to the
    // surface (radius 0 without a CObject).
    Vector const RangePoint = WithY(
        P, static_cast<float>((ReticleHeight * static_cast<double>(0.5f) - ReticleHeight * 0.1) *
                                  static_cast<double>(Scale) +
                              static_cast<double>(P.y)));
    float Radius = 0.0f;
    if (Object->cobject() != nullptr)
    {
        Vector RadiusCenter;
        Radius = Object->cobject()->get_physical_radius_r(RadiusCenter);
    }
    DrawRangeLabel(&G_HudZeroVector, Radius, Record, static_cast<int>(Record->Distance),
                   &RangePoint);

    // The ship behind the object and whether a player owns it.
    CObject const* const ShipObject = Object->cobject();
    CShip const* Ship = nullptr;
    if (ShipObject != nullptr && (static_cast<unsigned>(ShipObject->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
        Ship = static_cast<CShip const*>(ShipObject);
    FHudWideText Caption;   // retail frees the buffer on the way out under SEH
    Caption.Text = nullptr;
    Caption.Capacity = 0;
    bool const bPlayerOwned =
        Ship != nullptr && static_cast<CSimple const*>(Ship)->OwnerPlayer != 0;
    FFontString* const CaptionText = bPlayerOwned ? &TargetInfoText : &TargetNameText;

    if (Simple != nullptr)
    {
        // The cache keys -- loot units (1 unless CLoot), visited (0 for an unvisited solar,
        // FUN_004d4c70).
        int LootUnits = 1;
        if ((static_cast<unsigned>(Simple->ObjectClass) & CObject::ClassLoot) == CObject::ClassLoot)
            LootUnits = static_cast<int>(static_cast<CLoot const*>(Simple)->Units);
        int Visited = 1;
        if (HudIsUnvisitedSolar(Simple))
            Visited = 0;
        bool const bType80 = (Simple->TypeBits & OBJ_TRADELANE_RING) != 0;
        bool const bType40 = (Simple->TypeBits & OBJ_JUMP_GATE) != 0;

        // Rebuild the caption unless the cache matches.
        if (!(NameCacheSimpleId == Simple->Id && NameCacheLootUnits == LootUnits &&
              NameCacheVisited == Visited))
        {
            // Mode 1 (with the location line) for a ship, a docked CEqObj, or TypeBits 0x80 /
            // 0x40; FUN_004e8100 without the short name.
            bool const bDocked = (static_cast<unsigned>(Simple->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj &&
                                 static_cast<CEqObj const*>(Simple)->DockingBaseId != 0;
            int const Mode =
                (Ship != nullptr || bDocked || bType80 || bType40) ? kCaptionLocation : kCaptionName;
            if (HudBuildObjectCaption(Simple, &Caption, Mode, false))
            {
                // An unvisited non-ship shows its type caption (FUN_004d4d50); a visited one its
                // caption; a ship "<faction short name> - " (as DrawTargetReticle's labels) and
                // its caption.
                wchar_t Text[0xf8];
                Text[0] = 0;
                if (Ship == nullptr)
                {
                    if (Visited == 0)
                    {
                        wchar_t TypeText[0x80];
                        if (GetIdsStringWWithRawFallback(HudObjectTypeIds(Simple), TypeText, 0x80) == 0)
                            TypeText[0] = 0;
                        wcscpy(Text, TypeText);
                    }
                    else
                    {
                        wcscat(Text, Caption.Text);
                    }
                }
                else
                {
                    int const Vibe = static_cast<int>(static_cast<CEqObj const*>(Ship)->VibeId);
                    unsigned Affiliation = 0;
                    if (Reputation::Vibe::GetAffiliation(Vibe, Affiliation, false) == 0)
                    {
                        unsigned const ShortNameIds = Reputation::get_short_name(Affiliation);
                        if (ShortNameIds != 0 && ShortNameIds != 0xffffffffu &&
                            ShortNameIds != 0x30386u && ShortNameIds != 0x303b8u)
                        {
                            wchar_t Faction[0x80];
                            if (GetIdsStringWWithRawFallback(ShortNameIds, Faction, 0x80) == 0)
                                Faction[0] = 0;
                            if (wcslen(Faction) > 1 || Faction[0] != L' ')
                            {
                                wcscat(Text, Faction);
                                wcscat(Text, L" - "); // DAT_005d01f4
                            }
                        }
                    }
                    wcscat(Text, Caption.Text);
                }

                // The PlayerName font for a player-owned ship, else HudSmall; scale 0.171
                // (DAT_005d8f0c).
                NameCacheVisited = Visited;
                NameCacheSimpleId = Simple->Id;
                NameCacheLootUnits = LootUnits;
                NameCacheTimer = 0.0f;
                CaptionText->SetFont(bPlayerOwned ? PlayerNameFont : SmallFont);
                CaptionText->SetScale(0.171f, -1.0f);
                CaptionText->SetText(Text);
            }
            else
            {
                // No caption -- the keys invalidated, the string emptied.
                NameCacheLootUnits = -1;
                NameCacheVisited = -1;
                NameCacheSimpleId = 0;
                NameCacheTimer = 0.0f;
                CaptionText->ClearContents();
            }
        }

        // A TypeBits 0x80 object's cache expires after 10 s (DAT_005d49b0; NaN keeps it) --
        // retail advances the timer twice per frame.
        if (bType80)
        {
            for (int Pass = 0; Pass < 2; ++Pass)
            {
                double const Timer = GetClientFrameTimeCapped() + static_cast<double>(NameCacheTimer);
                NameCacheTimer = static_cast<float>(Timer);
                if (Timer > static_cast<double>(10.0f))
                    NameCacheSimpleId = 0;
            }
        }

        // The caption at (0.05 + 0.5) of the height (DAT_005d5b58, DAT_005c75e0), 0.03 higher
        // (DAT_005d011c) for the waypoint's object.
        float CaptionY = static_cast<float>(
            (ReticleHeight * 0.05 + ReticleHeight * static_cast<double>(0.5f)) *
                static_cast<double>(Scale) +
            static_cast<double>(P.y));
        if (bWaypointObject)
            CaptionY = static_cast<float>(static_cast<double>(CaptionY) + static_cast<double>(0.03f));
        DrawReticleLabel(ReticleRangeText, CaptionText, WithY(P, CaptionY));

        // The best-path object also gets the best-path caption, at (0.2 + 0.5) of the height
        // (DAT_005cf100) or, for the waypoint's object, at the height.
        if (Record->bIsBestPath != 0)
        {
            float BestPathY;
            if (!bWaypointObject)
                BestPathY = static_cast<float>(
                    (ReticleHeight * 0.2 + ReticleHeight * static_cast<double>(0.5f)) *
                        static_cast<double>(Scale) +
                    static_cast<double>(P.y));
            else
                BestPathY = static_cast<float>(ReticleHeight * static_cast<double>(Scale) +
                                               static_cast<double>(P.y));
            DrawReticleLabel(ReticleRangeText, &BestPathLabel, WithY(P, BestPathY));
        }
    }

    // A ship's toughness at the reticle's lower left ((0.5 - 0.13) of the width, DAT_005d4ef0;
    // (0.5 - 0.16) of the height, DAT_005d9630). The string is rebuilt when the value changes:
    // "%d" (PTR_DAT_005d0ec4) for 0..9999, otherwise empty and the key left as it was.
    if (Ship != nullptr)
    {
        int const Toughness = Object->get_toughness();
        if (Toughness != TargetInfoKey)
        {
            wchar_t ToughnessText[0x80];
            ToughnessText[0] = 0;
            if (Toughness >= 0 && Toughness < 10000)
            {
                swprintf(ToughnessText, 0x80, L"%d", Toughness);
                TargetInfoKey = Toughness;
            }
            RangeText.SetFont(SmallFont);
            RangeText.SetScale(0.171f, -1.0f);
            RangeText.SetText(ToughnessText);
        }
        float const ToughnessY = static_cast<float>(
            (ReticleHeight * static_cast<double>(0.5f) - ReticleHeight * 0.16) *
                static_cast<double>(Scale) +
            static_cast<double>(P.y));
        float const ToughnessX = static_cast<float>(
            (ReticleWidth * static_cast<double>(0.5f) - ReticleWidth * 0.13) *
                static_cast<double>(Scale) +
            static_cast<double>(P.x));
        DrawReticleLabel(ReticleRangeText, &RangeText, MakeVector(ToughnessX, ToughnessY, P.z));
    }

    // With no type bits in 0xfdff, the lead-fire marker. No null test on the player's ship, as
    // retail. Retail projects into an uninitialised stack vector; the port starts it at zero.
    if ((RecordType & OBJ_SOLAR_NO_WAYPOINT_MASK) == 0)
    {
        CShip* const PlayerShip = static_cast<CShip*>(PlayerObjectOfClass(Player, CObject::ClassShip));
        Vector Lead;
        if (PlayerShip->get_tgt_lead_fire_pos(Object, Lead))
        {
            Vector LeadScreen = MakeVector(0.0f, 0.0f, 0.0f);
            ICamera* const Camera = static_cast<ICamera*>(GetClientSoundListenerObject());   // FUN_0040f130
            Camera->ProjectVec(&LeadScreen, &Lead);
            LeadMarkers.emplace_back();
            FHudLeadMarker& Marker = LeadMarkers.back();
            Marker.Target.set(Object);
            Marker.LeadWorld = Lead;
            Marker.LeadScreen = LeadScreen;
        }
    }

    if (Caption.Text != nullptr)
        free(Caption.Text);
}

unsigned long* HudTargetMarkerColor(unsigned long* OutArgb, IObjRW* Object)
{
    // FUN_004eeea0.
    unsigned long Color = G_NnPaletteCyan;
    unsigned Type = 0;
    if (Object->get_type(Type) == 0)   // a failure keeps the cyan
    {
        if ((Type & OBJ_WAYPOINT) != 0)
        {
            Color = G_NnPaletteViolet;
        }
        else if ((Type & OBJ_LOOT) != 0)
        {
            Color = 0xffffffffu;
        }
        else if ((Type & (OBJ_GUIDED | OBJ_MINE)) != 0)
        {
            Color = G_NnPaletteRed;
        }
        else
        {
            // The player in ObjDB bucket 1 -- its reputation colour of Object (FUN_0045a4e0).
            void* const Player = GetPlayerObjRW();
            if (Player != nullptr && static_cast<FClientGameobj*>(Player)->ManagerKind == EClientGameobjKind::Ship)
            {
                unsigned long Reputation = 0;
                HudReputationColor(&Reputation, static_cast<IObjRW*>(Player), Object);
                Color = Reputation;
            }
        }
    }
    *OutArgb = Color;
    return OutArgb;
}

bool HudIsWaypointObjectId(unsigned ObjectId)
{
    // FUN_004d4220. GetHudWaypoint() (DAT_00674bc8) is not null-tested, as retail.
    if (GetPlayerWaypointCountRetail() > 0)
    {
        FPlayerWaypointRecord* const Waypoint = GetPlayerWaypointRetail(0);
        if (GetHudWaypoint()->IsWaypointAt(&Waypoint->Pos))
            return true;
        if (Waypoint->ObjectId != 0 && Waypoint->ObjectId == ObjectId)
            return true;
    }

    // The first best-path point: its resolved position is the live waypoint, or its resolved
    // object has id ObjectId. Retail writes System into ObjectId's stack slot after reading the
    // id.
    if (BestPathGetWaypointCount() <= 0)
        return false;
    FBestPathWaypoint* const Point = BestPathGetWaypoint(0);
    Vector Position;
    unsigned System = 0;
    if (Point->ResolvePosition(&Position, &System) &&
        GetHudWaypoint()->IsWaypointAt(&Position))
    {
        return true;
    }
    IObjRW* const Resolved = Point->ResolveObject();
    if (Resolved == nullptr)
        return false;
    return Resolved->get_id() == ObjectId;
}

namespace
{
    // The cursor test of FUN_004f0900 (inlined twice), the same shape as FUN_004f0620's:
    // ArrowPos through the Win3D camera (a failure is "not under the cursor"), then its distance
    // to the cursor compared with the pick radius (DAT_005c7500, DAT_005d8f10, DAT_0067dc9c,
    // DAT_0067dc70). An unordered or "radius below distance" compare is a miss.
    bool IsCursorOverEdgeArrow(Vector const& ArrowPos, float Scale)
    {
        float ScreenX = 0.0f;
        float ScreenY = 0.0f;
        float ScreenZ = 0.0f;
        Vector Point = ArrowPos;
        if ((AsCamera(Win3DGetCamera())->Project(&ScreenX, &ScreenY, &ScreenZ, &Point) & 0xff) == 0)
            return false;
        int CursorX = 0;
        int CursorY = 0;
        int CursorZ = 0;
        GetClientSoftwareCursor(&CursorX, &CursorY, &CursorZ);
        float const ScreenZCopy = ScreenZ;
        float const DeltaX = static_cast<float>(static_cast<double>(ScreenX) - CursorX);
        double const DeltaY = static_cast<double>(ScreenY) - CursorY;
        double const DeltaZ = static_cast<double>(ScreenZ) - static_cast<double>(ScreenZCopy);
        double const CursorDistance =
            std::sqrt(DeltaZ * DeltaZ + DeltaY * DeltaY +
                      static_cast<double>(DeltaX) * static_cast<double>(DeltaX));
        FSpaceCamera const* const Ui = Win3DGetCamera();
        double const PickRadius = static_cast<double>(-1.0f) / static_cast<double>(-1.3f) *
                                  static_cast<double>(Scale) / static_cast<double>(Ui->ZNear) *
                                  static_cast<double>(Ui->ZNear) * static_cast<double>(Ui->XScale);
        return PickRadius >= CursorDistance;
    }

    // One bar of the off-screen target (inlined four times): four vertices Left/Bottom, Right/Bottom, Right/Top, Left/Top at y = 0, Right being
    // Fraction * Width + Left for a fill (bFill), else Left + Width (Fraction not read).
    void EmitBarQuad(FHudMeshRect const& Rect, float Fraction, bool bFill)
    {
        float Right;
        if (bFill)
            Right = static_cast<float>(static_cast<double>(Fraction) * static_cast<double>(Rect.Width) +
                                       static_cast<double>(Rect.Left));
        else
            Right = static_cast<float>(static_cast<double>(Rect.Left) +
                                       static_cast<double>(Rect.Width));
        Win3DVertex3f(Rect.Left, 0.0f, Rect.Bottom);   // FUN_00421cc0
        Win3DVertex3f(Right, 0.0f, Rect.Bottom);
        Win3DVertex3f(Right, 0.0f, Rect.Top);
        Win3DVertex3f(Rect.Left, 0.0f, Rect.Top);
    }

    // A palette colour at three quarters (each byte * 3 / 4 as a signed division), through
    // FUN_00421a70.
    void SetDimmedPaletteColor(unsigned long Argb)
    {
        unsigned char const* const Bytes = reinterpret_cast<unsigned char const*>(&Argb);
        Win3DColor3ub(static_cast<unsigned char>((static_cast<int>(Bytes[0]) * 3) / 4),
                      static_cast<unsigned char>((static_cast<int>(Bytes[1]) * 3) / 4),
                      static_cast<unsigned char>((static_cast<int>(Bytes[2]) * 3) / 4));
    }
}

void HudTargetableObjects::DrawOffScreenContact(IObjRW* Object, FHudContactRecord const* Record,
                                                bool bIsTarget)
{
    // FUN_004f0900. A ship-typed record (0x7f0000) needs a ship CObject whose vibe has an
    // affiliation.
    unsigned Affiliation = 0;
    if ((Record->TypeFlags & OBJ_SHIP_MASK) != 0)
    {
        CObject const* const ShipObject = Object->cobject();
        if (ShipObject == nullptr)
            return;
        if ((static_cast<unsigned>(ShipObject->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
            return;
        int const Vibe = static_cast<int>(static_cast<CEqObj const*>(ShipObject)->VibeId);
        if (Reputation::Vibe::GetAffiliation(Vibe, Affiliation, false) != 0)
            return;
    }

    // The type bits the arrow depends on.
    unsigned const TypeFlags = Record->TypeFlags;
    bool const bLoot = ((TypeFlags >> 28) & 1u) != 0;       // 0x10000000
    bool const bDockingRing = ((TypeFlags >> 5) & 1u) != 0; // 0x20
    bool const bJumpGate = ((TypeFlags >> 6) & 1u) != 0;   // 0x40
    bool const bPlayerShip = Record->bPlayerShip != 0;
    bool const bPlanet = ((TypeFlags >> 1) & 1u) != 0;      // 0x2
    bool const bWatched = HudCommIsWatchedObjectId(Object->get_id());   // FUN_004cc880
    bool const bHat = Record->bWatchListed != 0 || Record->bIsBestPath != 0;
    bool bWaypoint = ((TypeFlags >> 9) & 1u) != 0;          // 0x200

    // The contact's attitude towards the player, 0 without one.
    float Attitude = 0.0f;
    if (GetPlayerObjRW() != nullptr)
        Object->get_attitude_towards(Attitude, static_cast<IObjRW*>(GetPlayerObjRW()));

    if (!bIsTarget)
    {
        // Without the hat or a player ship, only a hostile (attitude <= -0.6, DAT_005d8ed8; NaN
        // leaves) is shown.
        if (!bHat && !bPlayerShip && !(Attitude <= -0.6f))
            return;
        // Planets, docking rings, jump gates and waypoint records never.
        if (bPlanet || bDockingRing || bJumpGate || Record->bIsBestPath != 0 ||
            Record->bIsWaypoint != 0)
        {
            return;
        }
    }
    else
    {
        // The target counts as a waypoint when the HUD waypoint points at it; its select pulse
        // restarts.
        if (HudIsWaypointObjectId(Object->get_id()))
            bWaypoint = true;
        SelectPulse = 0.0f;
    }

    // The alpha. A non-target fades from 500 (loot, DAT_005d9668) or 1500 (DAT_005d8d08) out by
    // 0.001 per unit (DAT_005c75f8; NaN keeps 1); clamped to [0, 1], times 255 truncated.
    double Fade = 1.0;
    if (!bIsTarget)
    {
        float const Start = bLoot ? 500.0f : 1500.0f;
        if (Record->Distance > Start)
            Fade = 1.0 - (static_cast<double>(Record->Distance) - static_cast<double>(Start)) * 0.001;
    }
    if (Fade < 0.0)
        Fade = 0.0;
    if (Fade > 1.0)
        Fade = 1.0;
    int const Alpha = static_cast<int>(Fade * static_cast<double>(255.0f));
    unsigned char const BackgroundRgba[4] = {0x0c, 0x3e, 0x7a,
                                             static_cast<unsigned char>(Alpha)};

    // FUN_0054baf0's result is not used.
    Win3DEnable(kWin3DBlend);                                         // FUN_00421dd0
    Win3DSetBlendFunc(kWin3DBlendSrcAlpha, kWin3DBlendInvSrcAlpha);   // FUN_004220c0
    EngineLightClearRegisteredList();                                 // FUN_004101e0
    GetPlayerObjRW();

    // Violet for a waypoint, else the marker colour; the HUD_Comm speaker amber over either; the
    // alpha byte replaced.
    unsigned long Color;
    if (bWaypoint)
    {
        Color = G_NnPaletteViolet;
    }
    else
    {
        unsigned long Marker = 0;
        Color = *HudTargetMarkerColor(&Marker, Object);
    }
    if (bWatched)
        Color = G_NnPaletteAmber;
    reinterpret_cast<unsigned char*>(&Color)[3] = static_cast<unsigned char>(Alpha);
    unsigned char const* const ColorRgba = reinterpret_cast<unsigned char const*>(&Color);

    // The contact's centre in the listener camera's frame (its orientation read as a transform)
    // through DAT_00674b6c's InverseTransform.
    ICamera* const Listener = static_cast<ICamera*>(GetClientSoundListenerObject());
    Vector const Center = Object->get_center_of_mass();
    const float* const Frame = (const float*)Listener->GetOrientation();
    if (G_HudObjectsMath == nullptr)
        G_HudObjectsMath = Acquire3DMathEngine();   // FUN_005b73e0
    Vector Local;
    G_HudObjectsMath->InverseTransform(Local, *reinterpret_cast<Transform const*>(Frame),
                                       Center);

    // The arrow's orientation: the rotation of the transform whose position is the arrow point
    // below.
    Vector Flat;
    Flat.x = Local.x;
    Flat.y = Local.y;
    Flat.z = 0.0f;
    Vector Spin;
    Spin.x = 0.0f;
    Spin.y = 0.0f;
    Spin.z = 180.0f;
    Matrix const Rotation = RotateMatrix(Spin);
    Matrix const Look = LookMatrix(Flat);
    if (G_HudObjectsMath == nullptr)
        G_HudObjectsMath = Acquire3DMathEngine();
    Transform ArrowXform;
    G_HudObjectsMath->MulMatrix(ArrowXform.Rotation, Look,
                                Rotation);

    // The arrow point at depth -1.3 (DAT_005d8f10), as FUN_004f0620 but at 0.89 of the side edge
    // (DAT_005d9664) or 0.855 of the top / bottom (DAT_005d9660).
    FSpaceCamera const* const Ui = Win3DGetCamera();
    double const AbsX = std::fabs(static_cast<double>(Local.x));
    double const AbsYAspect =
        std::fabs(static_cast<double>(Local.y)) * static_cast<double>(Ui->Aspect);   // DAT_0067dc98
    Vector ArrowPos;
    ArrowPos.z = -1.3f;
    if (AbsYAspect < AbsX)
    {
        double Edge = std::tan(static_cast<double>(Ui->HalfFovXDeg) *
                               static_cast<double>(0.0174533f)) *   // DAT_0067dc90, DAT_005c77f4
                      static_cast<double>(-(-1.3f)) * static_cast<double>(0.89f);
#ifdef FREELANCER_WIDESCREEN_SUPPORT
        // Port-only Hor+: +0x98 is the 4:3 angle; the screen edge is the widened one.
        Edge *= SpaceCameraHorPlusScale(Ui);
#endif
        if (!(Local.x > 0.0f))
            Edge = -Edge;
        ArrowPos.y = static_cast<float>(
            (static_cast<double>(Local.y) / static_cast<double>(Local.x)) * Edge);
        ArrowPos.x = static_cast<float>(Edge);
    }
    else
    {
        double Edge = std::tan(static_cast<double>(Ui->FovYDeg) *
                               static_cast<double>(0.0174533f)) *   // DAT_0067dc94
                      static_cast<double>(-(-1.3f)) * static_cast<double>(0.855f);
        if (!(Local.y > 0.0f))
            Edge = -Edge;
        ArrowPos.y = static_cast<float>(Edge);
        ArrowPos.x = static_cast<float>(
            static_cast<double>(ArrowPos.y) /
            (static_cast<double>(Local.y) / static_cast<double>(Local.x)));
    }
    ArrowXform.Translation.x = ArrowPos.x;
    ArrowXform.Translation.y = ArrowPos.y;
    ArrowXform.Translation.z = ArrowPos.z;

    // The arrow's roll -- 0 for a waypoint, else FUN_0053eb10 of the orientation's third column
    // (rows 0 and 1). Every tile below except the waypoint pair turns by roll + pi - pi/4
    // (DAT_005c8a5c float, DAT_005d9658 double).
    float Roll = 0.0f;
    if (!bWaypoint)
        Roll = HudVectorAngle(ArrowXform.Rotation.E[0][2], ArrowXform.Rotation.E[1][2]);
    float const Angle = static_cast<float>(static_cast<double>(Roll) +
                                           static_cast<double>(3.14159265f) -
                                           static_cast<double>(0.7853982f));
    float const* const Point = &ArrowPos.x;

    // A non-target's arrow at 0.05, mode 0: the background then the arrow in Color -- loot tiles
    // for loot (then the player tiles for a player ship), the player tiles for a player ship,
    // else (and not a waypoint) the NS tiles.
    if (!bIsTarget)
    {
        bool bPlayerTiles = false;
        if (bLoot)
        {
            if (LootArrowTile != nullptr && LootArrowBackground != nullptr)
            {
                NnMontageDrawTileAt(LootArrowBackground, Point, BackgroundRgba, 0.05f, 0, Angle);
                NnMontageDrawTileAt(LootArrowTile, Point, ColorRgba, 0.05f, 0, Angle);
            }
            bPlayerTiles = bPlayerShip;
        }
        else if (bWaypoint)
        {
            bPlayerTiles = bPlayerShip;
        }
        else if (bPlayerShip)
        {
            bPlayerTiles = true;
        }
        else if (NSTargetArrow != nullptr && NSArrowBackground != nullptr)
        {
            NnMontageDrawTileAt(NSArrowBackground, Point, BackgroundRgba, 0.05f, 0, Angle);
            NnMontageDrawTileAt(NSTargetArrow, Point, ColorRgba, 0.05f, 0, Angle);
        }
        if (bPlayerTiles && PlayerArrow != nullptr && PlayerArrowBackground != nullptr)
        {
            NnMontageDrawTileAt(PlayerArrowBackground, Point, BackgroundRgba, 0.05f, 0, Angle);
            NnMontageDrawTileAt(PlayerArrow, Point, ColorRgba, 0.05f, 0, Angle);
        }
    }

    if (bWaypoint)
    {
        // The waypoint pair, unturned, at 0.1 for the target or 0.05; the cursor over it
        // (DAT_005d9650) makes the record the hover pick. The target stops here.
        if (WaypointArrow != nullptr && WaypointArrowBackground != nullptr)
        {
            float const Size = bIsTarget ? 0.1f : 0.05f;
            NnMontageDrawTileAt(WaypointArrowBackground, Point, BackgroundRgba, Size, 0, 0.0f);
            NnMontageDrawTileAt(WaypointArrow, Point, ColorRgba, Size, 0, 0.0f);
            if (IsCursorOverEdgeArrow(ArrowPos, 0.02733f))
            {
                G_HudHoverRecord = Record;
                G_HudPickOverrideId = 0;
            }
        }
        if (bIsTarget)
            return;
    }
    else if (bIsTarget)
    {
        // With all four target tiles, the hat when watch-listed or best path, then the
        // background and the arrow at 0.1 -- the player arrow twice for a player ship, else the
        // target arrow and its outline.
        if (CurrentTargetArrow != nullptr && CurrentTargetArrowOutline != nullptr &&
            PlayerArrow != nullptr && CurrentTargetArrowHat != nullptr)
        {
            if (bHat)
            {
                unsigned long HatColor = Color;
                reinterpret_cast<unsigned char*>(&HatColor)[3] = static_cast<unsigned char>(Alpha);
                NnMontageDrawTileAt(CurrentTargetArrowHat, Point,
                                    reinterpret_cast<unsigned char const*>(&HatColor), 0.1f, 0,
                                    Angle);
            }
            FButtonMontageTile const* Second;
            if (bPlayerShip)
            {
                NnMontageDrawTileAt(PlayerArrow, Point, BackgroundRgba, 0.1f, 0, Angle);
                Second = PlayerArrow;
            }
            else
            {
                NnMontageDrawTileAt(CurrentTargetArrow, Point, BackgroundRgba, 0.1f, 0, Angle);
                Second = CurrentTargetArrowOutline;
            }
            NnMontageDrawTileAt(Second, Point, ColorRgba, 0.1f, 0, Angle);
        }

        // The health bars, under the same rule as DrawOnScreenTarget's.
        CObject const* const BarObject = Object->cobject();
        bool bHealthBar = false;
        if (BarObject != nullptr && (static_cast<unsigned>(BarObject->ObjectClass) & CObject::ClassSimple) == CObject::ClassSimple)
        {
            unsigned const Class = static_cast<unsigned>(BarObject->ObjectClass);
            bool bIndestructible = false;
            if ((Class & CObject::ClassSolar) == CObject::ClassSolar && BarObject->Archetype != nullptr &&
                !static_cast<Archetype::Solar const*>(BarObject->Archetype)->bDestructible)
            {
                bIndestructible = true;
            }
            if (!bIndestructible &&
                (static_cast<CSimple const*>(BarObject)->TypeBits & (OBJ_MOON | OBJ_PLANET)) == 0)
            {
                bHealthBar = true;
            }
        }

        // Relative health; for a CEqObj the shield (FUN_004d4b50), its fraction only above a
        // positive maximum (ordered).
        bool bShieldActive = false;
        CObject const* HealthObject = Object->cobject();
        if (HealthObject != nullptr && (static_cast<unsigned>(HealthObject->ObjectClass) & CObject::ClassSimple) != CObject::ClassSimple)
            HealthObject = nullptr;
        float Health = 0.0f;
        float ShieldFraction = 0.0f;
        if (HealthObject != nullptr)
        {
            Health = static_cast<CSimple const*>(HealthObject)->get_relative_health();
            if ((static_cast<unsigned>(HealthObject->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj)
            {
                float ShieldMax = 0.0f;
                float ShieldCur = 0.0f;
                HudSampleShieldHitPoints(
                    const_cast<CEqObj*>(static_cast<CEqObj const*>(HealthObject)), &ShieldMax,
                    &ShieldCur, &bShieldActive);
                if (ShieldMax > 0.0f)
                    ShieldFraction = static_cast<float>(static_cast<double>(ShieldCur) /
                                                        static_cast<double>(ShieldMax));
            }
        }

        // The arrow transform times the Win3D camera's (DAT_0067dbfc) as the model-view
        // (FUN_00422690), quads in black.
        if (G_HudObjectsMath == nullptr)
            G_HudObjectsMath = Acquire3DMathEngine();
        Transform BarXform;
        G_HudObjectsMath->MulTransform(
            BarXform, ArrowXform,
            Win3DGetCamera()->WorldXform);
        Win3DBindModelview(nullptr, &BarXform);
        Win3DBegin(kWin3DQuads);   // FUN_00421670
        Win3DColor3ub(0, 0, 0);    // FUN_00421a70

        // The black backs of both bars.
        if (bHealthBar)
            EmitBarQuad(HealthBarRect, 0.0f, false);
        if (bShieldActive)
            EmitBarQuad(ShieldBarRect, 0.0f, false);

        // The fills -- health in red, shield in blue -- each only above 0 (ordered).
        if (bHealthBar && Health > 0.0f)
        {
            unsigned char const* const Red =
                reinterpret_cast<unsigned char const*>(&G_NnPaletteRed);
            Win3DColor3ub(Red[0], Red[1], Red[2]);
            EmitBarQuad(HealthBarRect, Health, true);
        }
        if (bShieldActive && ShieldFraction > 0.0f)
        {
            unsigned char const* const Blue =
                reinterpret_cast<unsigned char const*>(&G_NnPaletteBlue);
            Win3DColor3ub(Blue[0], Blue[1], Blue[2]);
            EmitBarQuad(ShieldBarRect, ShieldFraction, true);
        }
        Win3DEnd();   // FUN_00421690

        // Each bar's frame as a line loop in its colour at 3/4.
        if (bHealthBar)
        {
            Win3DBegin(kWin3DLineLoop);
            SetDimmedPaletteColor(G_NnPaletteRed);
            EmitBarQuad(HealthBarRect, 0.0f, false);
            Win3DEnd();
        }
        if (bShieldActive)
        {
            Win3DBegin(kWin3DLineLoop);
            SetDimmedPaletteColor(G_NnPaletteBlue);
            EmitBarQuad(ShieldBarRect, 0.0f, false);
            Win3DEnd();
        }
        return;
    }
    else if (bHat && NSTargetHat != nullptr)
    {
        // A watch-listed / best-path non-target's hat at 0.05.
        unsigned long HatColor = Color;
        reinterpret_cast<unsigned char*>(&HatColor)[3] = static_cast<unsigned char>(Alpha);
        NnMontageDrawTileAt(NSTargetHat, Point, reinterpret_cast<unsigned char const*>(&HatColor),
                            0.05f, 0, Angle);
    }

    // The cursor over the arrow (DAT_005d011c) makes the object the forced pick and clears the
    // hover record.
    if (IsCursorOverEdgeArrow(ArrowPos, 0.03f))
    {
        G_HudPickOverrideId = Record->ObjectId;
        G_HudHoverRecord = nullptr;
    }
}

void HudTargetableObjects::CollectLeadMarkers()
{
    // FUN_004f1dd0.
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());   // FUN_0054baf0
    if (Player == nullptr)
        return;
    ICamera* const Camera = static_cast<ICamera*>(GetClientSoundListenerObject());   // FUN_0040f130
    int const LastColumn = static_cast<int>(G_RenderSurfaceWidth) - 1;   // DAT_00610850
    int const LastRow = static_cast<int>(G_RenderSurfaceHeight) - 1;     // DAT_00610854

    // Every gameobj of ObjDB bucket 9 (FUN_00540fc0).
    int const Kind = static_cast<int>(EClientGameobjKind::DynamicAsteroid);
    for (FClientGameobj* Gameobj = ClientObjDb_GetKindHead(&Kind); Gameobj != nullptr;
         Gameobj = Gameobj->ListNext)
    {
        // Not being reaped, with a collision state (the object is passed only when its class
        // has bit 0).
        if (Gameobj->IsDead())
            continue;
        CObject* const Object = CObjectOfGameobj(Gameobj);
        CObject const* const Engine =
            (Object != nullptr && (static_cast<unsigned>(Object->ObjectClass) & CObject::ClassObject) == CObject::ClassObject) ? Object
                                                                                           : nullptr;
        if (!PhySys::GetCollisionState(Engine))
            continue;

        // Projected and inside the surface on both axes; a point that had to be clamped (below 0
        // ordered, or above the last pixel) is skipped.
        IObjRW* const Object_ = Gameobj;
        Vector Screen;
        Screen.x = 0.0f;
        Screen.y = 0.0f;
        Screen.z = 0.0f;
        if ((Camera->ProjectVec(&Screen, &Object_->get_position()) & 0xff) == 0)
            continue;
        bool bClamped = false;
        if (Screen.x < 0.0f)
        {
            Screen.x = 0.0f;
            bClamped = true;
        }
        else if (static_cast<double>(Screen.x) > static_cast<double>(LastColumn))
        {
            Screen.x = static_cast<float>(LastColumn);
            bClamped = true;
        }
        if (Screen.y < 0.0f)
        {
            Screen.y = 0.0f;
            bClamped = true;
        }
        else if (static_cast<double>(Screen.y) > static_cast<double>(LastRow))
        {
            Screen.y = static_cast<float>(LastRow);
            bClamped = true;
        }
        if (bClamped)
            continue;

        // No null test on the player's ship, as retail.
        CShip* const PlayerShip = static_cast<CShip*>(PlayerObjectOfClass(Player, CObject::ClassShip));
        Vector Lead;
        if (!PlayerShip->get_tgt_lead_fire_pos(Object_, Lead))
            continue;

        // The projection's result is not tested.
        LeadMarkers.emplace_back();
        FHudLeadMarker& Marker = LeadMarkers.back();
        Marker.Target.set(Gameobj);
        Marker.LeadWorld = Lead;
        Marker.LeadScreen.x = 0.0f;
        Marker.LeadScreen.y = 0.0f;
        Marker.LeadScreen.z = 0.0f;
        ICamera* const LeadCamera = static_cast<ICamera*>(GetClientSoundListenerObject());
        LeadCamera->ProjectVec(&Marker.LeadScreen, &Marker.LeadWorld);
    }
}

void HudTargetableObjects::DrawLeadMarkers()
{
    // FUN_004f2070. FUN_0040f130's result is not used; the player is not null-tested (Render
    // calls this only for a player ship).
    GetClientSoundListenerObject();
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());   // FUN_0054baf0
    IObjRW* PlayerTarget = nullptr;
    Player->get_target(PlayerTarget);

    // The red of DAT_00679ba8 as A:R:G:B, once with its alpha and once with alpha 0 for the
    // edges.
    unsigned char const* const Red = reinterpret_cast<unsigned char const*>(&G_NnPaletteRed);
    unsigned long const EdgeColor = (static_cast<unsigned long>(Red[0]) << 16) |
                                    (static_cast<unsigned long>(Red[1]) << 8) |
                                    static_cast<unsigned long>(Red[2]);
    unsigned long const Color = (static_cast<unsigned long>(Red[3]) << 24) | EdgeColor;

    // A cross on every marker whose object is still there: 6 pixels of the 640-wide unit
    // (DAT_005d966c, DAT_005d4f08) on the player's target, 2 on any object of type 0x55f0000,
    // none otherwise; a zero size draws nothing (NaN draws).
    for (std::list<FHudLeadMarker>::iterator It = LeadMarkers.begin(); It != LeadMarkers.end();
         ++It)
    {
        FHudLeadMarker const& Marker = *It;
        IObjRW* const Object = ObjRwFromWatchable(const_cast<Watchable*>(Marker.Target.target()));
        if (Object == nullptr)
            continue;
        unsigned Type = 0;
        Object->get_type(Type);
        float Size = 0.0f;
#ifdef FREELANCER_WIDESCREEN_SUPPORT
        // Port-only Hor+: marker size follows the height-scaled interface.
        double const Unit = WidescreenEquivalentSurfaceWidth() * static_cast<double>(0.0015625f);
#else
        double const Unit = static_cast<double>(static_cast<int>(G_RenderSurfaceWidth)) *
                            static_cast<double>(0.0015625f);   // DAT_00610850
#endif
        if (Object == PlayerTarget)
            Size = static_cast<float>(Unit * 6.0);
        else if ((Type & (OBJ_FIGHTER | OBJ_FREIGHTER | OBJ_GUNBOAT | OBJ_CRUISER | OBJ_TRANSPORT | OBJ_MINING | OBJ_GUIDED | OBJ_MINE)) != 0)
            Size = static_cast<float>(Unit + Unit);
        else
            continue;
        if (Size == 0.0f)
            continue;

        float const X = Marker.LeadScreen.x;
        float const Y = Marker.LeadScreen.y;
        Vector const LeftEnd = {X - Size, Y, 0.0f};
        Vector const RightEnd = {Size + X, Y, 0.0f};
        Win3DImmedDrawLine(&LeftEnd, &RightEnd, Color, EdgeColor, 2.0f);   // FUN_005a2940
        Vector const TopEnd = {X, Y - Size, 0.0f};
        Vector const BottomEnd = {X, Size + Y, 0.0f};
        Win3DImmedDrawLine(&TopEnd, &BottomEnd, Color, EdgeColor, 2.0f);
    }
}

void HudTargetableObjects::UpdateAimPoint()
{
    // FUN_004f2220.
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());   // FUN_0054baf0
    if (Player == nullptr)
        return;
    FireAttitude = 0;
    unsigned PlayerTargetId = 0;
    IObjRW* PlayerTarget = nullptr;
    Player->get_target(PlayerTarget);
    if (PlayerTarget != nullptr)
        PlayerTargetId = PlayerTarget->get_id();

    // A simple under the camera ray moving slower than 5 (|v|^2 < 25, DAT_005d9670; summed x, y,
    // z) with any of the type bits 0x107ffdff counts; its client object's attitude towards the
    // player becomes the fire attitude.
    bool bRayUsed = false;
    CObject* const RayTarget = RayHit.Object;
    if (RayTarget != nullptr && (static_cast<unsigned>(RayTarget->ObjectClass) & CObject::ClassSimple) == CObject::ClassSimple)
    {
        Vector const Velocity = RayTarget->get_velocity();
        double const SpeedSq = static_cast<double>(Velocity.x) * Velocity.x +
                               static_cast<double>(Velocity.y) * Velocity.y +
                               static_cast<double>(Velocity.z) * Velocity.z;
        CSimple const* const Simple = static_cast<CSimple const*>(RayTarget);
        if (SpeedSq < 25.0 && (Simple->TypeBits & (OBJ_SHIP_MASK | OBJ_SOLAR_NO_WAYPOINT_MASK | OBJ_LOOT)) != 0)
        {
            bRayUsed = true;
            FClientGameobj* const Gameobj = ClientObjDb_FindObject(Simple->Id, 0);   // FUN_005416c0
            if (Gameobj != nullptr)
                FireAttitude = GetAttitudeClass(Player, Gameobj);   // FUN_0045a490
        }
    }

    // The cursor as a fraction of the surface from its centre (as FindHostileContactNearCursor),
    // and the depth to beat: FLT_MAX, or the ray hit's distance squared when the ray object
    // counted.
    ICamera* const Camera = static_cast<ICamera*>(GetClientSoundListenerObject());   // FUN_0040f130
    int CursorX = 0;
    int CursorY = 0;
    int CursorZ = 0;
    GetClientSoftwareCursor(&CursorX, &CursorY, &CursorZ);
    double const Width = static_cast<double>(static_cast<int>(G_RenderSurfaceWidth));
    double const Height = static_cast<double>(static_cast<int>(G_RenderSurfaceHeight));
    float BestScreenDistSq = FLT_MAX;
    float BestDepthSq = FLT_MAX;
    int BestAttitude = 0;
    float const HalfWidth = static_cast<float>(Width * 0.5);
    float const CursorFracX =
        static_cast<float>((static_cast<double>(CursorX) - static_cast<double>(HalfWidth)) / Width);
    float const HalfHeight = static_cast<float>(Height * 0.5);
    float const CursorFracY =
        static_cast<float>((static_cast<double>(CursorY) - static_cast<double>(HalfHeight)) / Height);
    if (bRayUsed)
        BestDepthSq = static_cast<float>(static_cast<double>(RayHit.Distance) * RayHit.Distance);

    // The lead marker nearest the camera among those whose screen position lies within a radius
    // of the cursor (0.003 for the player's target, else 0.002 -- compared against the squared
    // distance, as retail does), nearer than the depth to beat. The marker's watcher is not
    // null-tested before get_id, as retail.
    FHudLeadMarker const* Best = nullptr;
    for (std::list<FHudLeadMarker>::iterator It = LeadMarkers.begin(); It != LeadMarkers.end();
         ++It)
    {
        FHudLeadMarker const& Marker = *It;
        IObjRW* const Object = ObjRwFromWatchable(const_cast<Watchable*>(Marker.Target.target()));
        float const Radius = (Object->get_id() == PlayerTargetId) ? 0.003f : 0.002f;
        double const Dx = static_cast<double>(CursorFracX) -
                          (static_cast<double>(Marker.LeadScreen.x) - static_cast<double>(HalfWidth)) / Width;
        double const Dy = static_cast<double>(CursorFracY) -
                          (static_cast<double>(Marker.LeadScreen.y) - static_cast<double>(HalfHeight)) / Height;
        double const ScreenDistSq = Dy * Dy + Dx * Dx;
        if (!(ScreenDistSq < static_cast<double>(Radius)))
            continue;
        Vector const* const CameraPos = Camera->GetPosition();
        Vector Offset;   // rounded to float, as retail's FUN_00422b80
        Offset.x = Marker.LeadWorld.x - CameraPos->x;
        Offset.y = Marker.LeadWorld.y - CameraPos->y;
        Offset.z = Marker.LeadWorld.z - CameraPos->z;
        double const DepthSq = static_cast<double>(Offset.z) * Offset.z +
                               static_cast<double>(Offset.y) * Offset.y +
                               static_cast<double>(Offset.x) * Offset.x;
        if (!(DepthSq < static_cast<double>(BestDepthSq)))
            continue;
        BestScreenDistSq = static_cast<float>(ScreenDistSq);
        BestDepthSq = static_cast<float>(DepthSq);
        Best = &Marker;
        BestAttitude = GetAttitudeClass(Player, Object);   // FUN_0045a490
    }

    // A marker latches the fire attitude; unless auto-aim is set to 5
    // (options 0x35 > 0 and 0x36 == 5), a marker within 0.001 (DAT_005d8f14) of the cursor puts
    // the aim point on its lead position.
    bFireAttitude = 0;
    if (Best != nullptr)
    {
        bFireAttitude = 1;
        FireAttitude = BestAttitude;
        bool const bAutoAim5 =
            GetGameOption(kGameOptPlayerGroup) > 0 && GetGameOption(kGameOptAutoAim) == 5;
        if (!bAutoAim5 && BestScreenDistSq < 0.001f)
        {
            AimPoint = Best->LeadWorld;
            return;
        }

        // Otherwise the cursor ray (normalised: summed y, z, x) out to the marker's distance from
        // the camera (summed z, y, x).
        Vector Direction;
        Camera->Unproject(&Direction, static_cast<float>(CursorX), static_cast<float>(CursorY));
        double const InvLength = 1.0 / std::sqrt(static_cast<double>(Direction.y) * Direction.y +
                                                 static_cast<double>(Direction.z) * Direction.z +
                                                 static_cast<double>(Direction.x) * Direction.x);
        Direction.x = static_cast<float>(Direction.x * InvLength);
        Direction.y = static_cast<float>(Direction.y * InvLength);
        Direction.z = static_cast<float>(Direction.z * InvLength);
        Vector const* const CameraPos = Camera->GetPosition();
        double const Dx = static_cast<double>(Best->LeadWorld.x) - static_cast<double>(CameraPos->x);
        double const Dy = static_cast<double>(Best->LeadWorld.y) - static_cast<double>(CameraPos->y);
        double const Dz = static_cast<double>(Best->LeadWorld.z) - static_cast<double>(CameraPos->z);
        double const Distance = std::sqrt(Dz * Dz + Dy * Dy + Dx * Dx);
        Vector Reach;
        Reach.x = static_cast<float>(Direction.x * Distance);
        Reach.y = static_cast<float>(Direction.y * Distance);
        Reach.z = static_cast<float>(Direction.z * Distance);
        Vector const* const Origin = Camera->GetPosition();
        AimPoint.x = Reach.x + Origin->x;
        AimPoint.y = Reach.y + Origin->y;
        AimPoint.z = Reach.z + Origin->z;
        return;
    }

    // No marker -- the cursor ray (summed y, z, x) out to the farthest active gun's range (no
    // null test on the player's ship, as retail), or 1e10 when that fails.
    Vector Direction;
    Camera->Unproject(&Direction, static_cast<float>(CursorX), static_cast<float>(CursorY));
    double const InvLength = 1.0 / std::sqrt(static_cast<double>(Direction.y) * Direction.y +
                                             static_cast<double>(Direction.z) * Direction.z +
                                             static_cast<double>(Direction.x) * Direction.x);
    Direction.x = static_cast<float>(Direction.x * InvLength);
    Direction.y = static_cast<float>(Direction.y * InvLength);
    Direction.z = static_cast<float>(Direction.z * InvLength);
    float GunRange = 0.0f;
    CShip* const PlayerShip = static_cast<CShip*>(PlayerObjectOfClass(Player, CObject::ClassShip));
    if (!PlayerShip->get_farthest_active_gun_range(GunRange))
        GunRange = 1.0e10f;

    // A counted ray hit nearer than that range (ordered <) sets the
    // reach instead.
    float Reach = GunRange;
    if (bRayUsed && RayHit.Distance < GunRange)
        Reach = RayHit.Distance;
    Vector Scaled;
    Scaled.x = static_cast<float>(static_cast<double>(Direction.x) * Reach);
    Scaled.y = static_cast<float>(static_cast<double>(Direction.y) * Reach);
    Scaled.z = static_cast<float>(static_cast<double>(Direction.z) * Reach);
    Vector const* const Origin = Camera->GetPosition();
    AimPoint.x = Scaled.x + Origin->x;
    AimPoint.y = Scaled.y + Origin->y;
    AimPoint.z = Scaled.z + Origin->z;
}

void HudTargetableObjects::UpdateMissileAlert()
{
    // FUN_004f18d0. The blink timer (0.1, DAT_005d8ef0) -- below it nothing happens; at or past
    // the period the phase flips and the timer restarts.
    bool bToggled = false;
    double const Timer =
        GetClientFrameTimeCapped() + static_cast<double>(MissileBlinkTimer);   // FUN_0042d680
    MissileBlinkTimer = static_cast<float>(Timer);
    if (!NotBelow(Timer, 0.1f))
        return;
    if (!(Timer < static_cast<double>(MissileBlinkPeriod)))   // NaN does not flip
    {
        bMissileBlinkOn = (bMissileBlinkOn == 0) ? 1 : 0;
        MissileBlinkTimer = 0.0f;
        bToggled = true;
    }

    // The nearest scanned missile (type 0x1000000) targeting the player, by squared distance
    // (summed z, y, x; ordered <). The first one found since the alert went dark queues the
    // voice line: "incoming_torpedo" for a munition of hp type 0xa (hp_torpedo), else
    // "incoming_missile".
    float Nearest = 1.0e12f;
    bool bAny = false;
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());   // FUN_0054baf0
    if (Player != nullptr)
    {
        for (int Index = 0; Index < ScanEntries->Count; ++Index)
        {
            IObjRW* const Missile =
                ObjRwFromWatchable(const_cast<Watchable*>(ScanEntries->Entries[Index].target()));
            if (Missile == nullptr)
                continue;
            if ((Contacts.data()[Index].TypeFlags & OBJ_GUIDED) == 0)
                continue;
            IObjRW* MissileTarget = nullptr;
            if (Missile->get_target(MissileTarget) != 0)
                continue;
            if (MissileTarget == nullptr)
                continue;
            if (Player->get_id() != MissileTarget->get_id())
                continue;
            Vector const& MissilePos = Missile->get_position();
            Vector const& PlayerPos = Player->get_position();
            double const Dx = static_cast<double>(PlayerPos.x) - static_cast<double>(MissilePos.x);
            double const Dy = static_cast<double>(PlayerPos.y) - static_cast<double>(MissilePos.y);
            double const Dz = static_cast<double>(PlayerPos.z) - static_cast<double>(MissilePos.z);
            double const DistanceSq = Dz * Dz + Dy * Dy + Dx * Dx;
            if (!(DistanceSq < static_cast<double>(Nearest)))
                continue;
            Nearest = static_cast<float>(DistanceSq);
            bAny = true;
            if (bMissileWarned != 0)
                continue;
            bMissileWarned = 1;
            Archetype::Root const* const Arch = Missile->cobject()->get_archetype();
            if (Arch != nullptr &&
                static_cast<Archetype::Munition const*>(Arch)->HpType == HpTypeTorpedo)
            {
                QueueHazardVoiceLine(G_VoiceIncomingTorpedo);
            }
            else
            {
                QueueHazardVoiceLine(G_VoiceIncomingMissile);
            }
        }
    }

    // Shown while a missile is there and the phase is off; the warned latch follows "any". With
    // none, the period drops back to 0.1; with one, a flip rings UI sound 0x19
    // ("ui_incoming_missile") and the period becomes sqrt(nearest) * 0.001 (DAT_005c75f8), at
    // least 0.1; the unrounded double is compared.
    uintptr_t const bHide = (bAny && bMissileBlinkOn == 0) ? 0u : 1u;
    MissileAlert->SetProperty(kWinPropHide, bHide, 0);
    bMissileWarned = bAny ? 1 : 0;
    if (!bAny)
    {
        MissileBlinkPeriod = 0.1f;
        return;
    }
    if (bToggled)
        PlayOrRewindUiSoundSlot(kUiSoundIncomingMissile);   // FUN_005645c0
    double const Period = std::sqrt(static_cast<double>(Nearest)) * 0.001;
    MissileBlinkPeriod = static_cast<float>(Period);
    if (Period < static_cast<double>(0.1f))
        MissileBlinkPeriod = 0.1f;
}

void HudTargetableObjects::UpdateTrailAlert()
{
    // FUN_004f1800. The blink timer (0.5, DAT_005d8ef4) -- below it nothing happens; at or past
    // the period (NaN not) it restarts with the period 0.5 and the phase flips.
    double const Timer =
        GetClientFrameTimeCapped() + static_cast<double>(TrailBlinkTimer);   // FUN_0042d680
    TrailBlinkTimer = static_cast<float>(Timer);
    if (!NotBelow(Timer, 0.5f))
        return;
    if (!(Timer < static_cast<double>(TrailBlinkPeriod)))
    {
        TrailBlinkTimer = 0.0f;
        TrailBlinkPeriod = 0.5f;
        bTrailBlinkOn = (bTrailBlinkOn == 0) ? 1 : 0;
    }

    // Shown while the player's ship has a behaviour manager whose
    // closest trailing ship is nearer than 500 (DAT_005d8ef8; ordered <) and the phase is off.
    uintptr_t bHide = 1;
    CEqObj* const PlayerShip =
        static_cast<CEqObj*>(PlayerObjectOfClass(static_cast<IObjRW*>(GetPlayerObjRW()), CObject::ClassShip));
    if (PlayerShip != nullptr)
    {
        IBehaviorManager* const Behavior = PlayerShip->get_behavior_interface();
        if (Behavior != nullptr && Behavior->get_closest_trailing_ship() < 500.0f &&
            bTrailBlinkOn == 0)
        {
            bHide = 0;
        }
    }
    TrailAlert->SetProperty(kWinPropHide, bHide, 0);
}

void HudTargetableObjects::UpdateLagAlert()
{
    // FUN_004f1b40. Single player: nothing. Once a second (DAT_005d8f00) the timer loses a
    // second.
    if (SinglePlayer())
        return;
    double const Timer =
        GetClientFrameTimeCapped() + static_cast<double>(LagTimer);   // FUN_0042d680
    LagTimer = static_cast<float>(Timer);
    if (!NotBelow(Timer, 1.0f))
        return;
    LagTimer = static_cast<float>(Timer - 1.0);

    // Lit above 600 ms; above 1200 ms red and blinking, otherwise amber and steady.
    bool const bSlow = CDPClient::GetPingDelay() > 0x258;
    if (CDPClient::GetPingDelay() > 0x4b0)
    {
        bLagBlinkOn = (bLagBlinkOn == 0) ? 1 : 0;
        uintptr_t const Red = reinterpret_cast<uintptr_t>(&G_NnPaletteRed);
        LagAlert->SetProperty(kStaticPropColours, Red, Red);
    }
    else
    {
        uintptr_t const Amber = reinterpret_cast<uintptr_t>(&G_NnPaletteAmber);
        bLagBlinkOn = 0;
        LagAlert->SetProperty(kStaticPropColours, Amber, Amber);
    }
    uintptr_t const bHide = (bSlow && bLagBlinkOn == 0) ? 0u : 1u;
    LagAlert->SetProperty(kWinPropHide, bHide, 0);
}

namespace
{
    // See the header for the factory's retail functions.
    HUDFACTORY<HudTargetableObjects> GTargetableFactory("Targetable_Objects");
}

namespace
{
    // DAT_00674bf4..DAT_00674bd8 -- the voice-line ids the static initialisers LAB_004f4e50 ..
    // LAB_004f4f30 hash with CreateID.
    unsigned const G_VoiceAnomalyDetected = CreateID("anomaly_detected");          // DAT_00674bf4
    unsigned const G_VoiceDanger = CreateID("danger");                               // DAT_00674bf0
    unsigned const G_VoiceRadiationDamage = CreateID("radiation_damage_detected");  // DAT_00674bec
    unsigned const G_VoiceMinesDetected = CreateID("mines_detected");               // DAT_00674be8
    unsigned const G_VoiceVolatileGases = CreateID("volatile_gases_detected");      // DAT_00674be4
    unsigned const G_VoiceWarning = CreateID("warning");                             // DAT_00674be0
    unsigned const G_VoiceSensorEfficiency = CreateID("sensor_efficiency_reduced"); // DAT_00674bdc
    unsigned const G_VoiceIncreasedDrag = CreateID("increased_drag_detected");      // DAT_00674bd8

    // DAT_00674bcc, static initialiser LAB_004f5310: {0, 0, 2.0f (DAT_005d97f0)}.
    FHudHazardWarning G_HudHazardWarning = {0, 0, 2.0f};

    // DAT_00674c08.
    bool G_bHudRadiationZoneWarning = false;

    // FUN_00489e80(Id, 0).
    void QueueHazardVoiceLine(unsigned VoiceId)
    {
        NnVoiceEnqueue(VoiceId, 0);
    }

    // The gameobj's CObject with a class test; null anywhere -> null.
    CObject* HazardObjectOfClass(FClientGameobj* Gameobj, unsigned Mask)
    {
        CBase* const Base = static_cast<IObjInspectImpl*>(Gameobj);
        if (Base == nullptr)
            return nullptr;
        CObject* const Object = Base->cobject();
        if (Object == nullptr)
            return nullptr;
        if ((static_cast<unsigned>(Object->ObjectClass) & Mask) != Mask)
            return nullptr;
        return Object;
    }

}

void FHudHazardWarning::Update(float DeltaSeconds)
{
    // FUN_004f4f50.
    FClientGameobj* const Player = static_cast<FClientGameobj*>(GetPlayerObjRW()); // FUN_0054baf0
    // No player, a dead player, or a CSimple at zero relative health (DAT_005c7474) -> reset.
    // The CSimple is not null-tested, as retail.
    bool bReset = (Player == nullptr);
    if (!bReset && Player->IsDead())
        bReset = true;
    if (!bReset)
    {
        CSimple const* const Simple = static_cast<CSimple const*>(HazardObjectOfClass(Player, 3));
        if (Simple->get_relative_health() == 0.0f)
            bReset = true;
    }
    if (bReset)
    {
        MineId = 0;
        ZoneFlags = 0;
        Timer = 2.0f;
        G_bHudRadiationZoneWarning = false;
        return;
    }

    Vector const& ShipPos = Player->get_position();

    // The ship's scanner, used as the CEScanner it is with no cast check. Any scanned object of
    // type 0x800 closer than 2000 m (DAT_005d97f8) counts; the first new such id is announced.
    bool bMineInRange = false;
    CShip* const ShipForScan = static_cast<CShip*>(HazardObjectOfClass(Player, CObject::ClassShip));
    CEScanner const* const Scanner = static_cast<CEScanner const*>(
        ShipForScan->EquipManager.FindFirst(EquipTypeScanner));
    if (Scanner != nullptr)
    {
        for (int Index = 0; Index < Scanner->Scans.Count; ++Index)
        {
            Watchable const* const Watched = Scanner->Scans.Entries[Index].target();
            if (Watched == nullptr)
                continue;
            IObjRW* const Candidate =
                ObjRwFromWatchable(const_cast<void*>(static_cast<void const*>(Watched)));
            if (Candidate == nullptr)
                continue;
            unsigned Type = 0;
            if (Candidate->get_type(Type) != 0 || Type != OBJ_JUMP_HOLE)
                continue;
            Vector const& Pos = Candidate->get_position();
            // The differences are rounded to float; the squares are summed z, y, x in double.
            float const Dx = Pos.x - ShipPos.x;
            float const Dy = Pos.y - ShipPos.y;
            float const Dz = Pos.z - ShipPos.z;
            double const LengthSq = static_cast<double>(Dz) * Dz + static_cast<double>(Dy) * Dy +
                                    static_cast<double>(Dx) * Dx;
            if (!(LengthSq < 4000000.0))
                continue;
            bMineInRange = true;
            unsigned const Id = Candidate->get_id();
            if (MineId != Id)
            {
                MineId = Id;
                QueueHazardVoiceLine(G_VoiceAnomalyDetected);
            }
        }
    }
    if (!bMineInRange)
        MineId = 0;

    // Nothing more while the ship rides a trade lane (no reset either).
    CShip* const Ship = static_cast<CShip*>(HazardObjectOfClass(Player, CObject::ClassShip));
    if (Ship->is_using_tradelane())
        return;

    // The zone sample runs every 2 seconds (DAT_005d97f0). The rounded sum is stored, the
    // unrounded one compared; only an ordered >= 2.0 goes on.
    double const Elapsed = static_cast<double>(DeltaSeconds) + static_cast<double>(Timer);
    Timer = static_cast<float>(Elapsed);
    if (!(Elapsed >= 2.0))
        return;

    // The zone mask of a 25 m sphere (DAT_005d97f4) about the ship, from the client ObjDB's zone
    // database (DAT_00678a2c).
    Universe::ISystem const* const Zones =
        static_cast<Universe::ISystem const*>(GetClientObjDb()->GetZoneDb());
    Geometry::Sphere Query;
    Query.Center = ShipPos;
    Query.Radius = 25.0f;
    unsigned const NewFlags = Zones->GetSphereProperties(&Query);

    if (NewFlags != ZoneFlags)
    {
        // Radiation is only reported outside an atmosphere.
        bool const bShipHullOk = (Ship != nullptr && Ship->AtmosphereSolar == nullptr);
        unsigned const Radiation = NewFlags & Universe::kZonePropDamage;
        if (Radiation != 0)
        {
            if (bShipHullOk)
                G_bHudRadiationZoneWarning = true;
        }
        else
        {
            G_bHudRadiationZoneWarning = false;
        }

        // One announcement per newly entered hazard, first match wins.
        if (Radiation != 0 && (ZoneFlags & Universe::kZonePropDamage) == 0)
        {
            if (bShipHullOk)
            {
                QueueHazardVoiceLine(G_VoiceDanger);
                QueueHazardVoiceLine(G_VoiceRadiationDamage);
            }
        }
        else if ((NewFlags & 0x1000u) != 0 && (ZoneFlags & 0x1000u) == 0)
        {
            QueueHazardVoiceLine(G_VoiceDanger);
            QueueHazardVoiceLine(G_VoiceMinesDetected);
        }
        else if ((NewFlags & 0x4000u) != 0 && (ZoneFlags & 0x4000u) == 0)
        {
            QueueHazardVoiceLine(G_VoiceDanger);
            QueueHazardVoiceLine(G_VoiceVolatileGases);
        }
        else if ((NewFlags & Universe::kZonePropInterference) != 0 && (ZoneFlags & Universe::kZonePropInterference) == 0)
        {
            QueueHazardVoiceLine(G_VoiceWarning);
            QueueHazardVoiceLine(G_VoiceSensorEfficiency);
        }
        else if ((NewFlags & Universe::kZonePropDrag) != 0 && (ZoneFlags & Universe::kZonePropDrag) == 0)
        {
            QueueHazardVoiceLine(G_VoiceWarning);
            QueueHazardVoiceLine(G_VoiceIncreasedDrag);
        }
    }

    ZoneFlags = NewFlags;
    Timer = 0.0f;
}

void UpdateHudHazardWarning(float DeltaSeconds)
{
    // FUN_004f5330.
    G_HudHazardWarning.Update(DeltaSeconds);
}

bool IsHudRadiationZoneWarningOn()
{
    // FUN_004f5340.
    return G_bHudRadiationZoneWarning;
}
