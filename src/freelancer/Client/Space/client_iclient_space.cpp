// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// IClientImpl slots of retail IClientS.cpp (.text:0053aaa0..0053e4a0) -- the space side of the
// server's IClient calls. See freelancer_client.h for the split with client_iclient.cpp.
//
// The gameobj a slot works on is FUN_005416c0 (ClientObjDb_FindObject); its IObjAffect is the
// sub-object at +4, i.e. a plain call through FClientGameobj. "Narrowed to M" is retail's
// inline `(Object->ObjectClass & M) == M ? Object : 0`, used with no null test after it
// wherever retail has none.

#include "freelancer_client.h"

#include "Interface/Pages/character_select_equip.h"   // player equip / collision / health setters (0x004c4xxx)
#include "Space/Effects/cli_projectile_fx.h"        // FCliLauncherFx::FireDirect (the dropper tails, slots 49..51)
#include "Common/client_cliobj.h"            // Archetype::LoadMountedEquipmentCompanions (FUN_004252d0)
#include "Space/Effects/client_equip_fx.h"          // EquippedObjFindEquipFx / CliTractorEquipFxDropLoot (slot 64)
#include "Space/client_gameobj.h"           // FClientGameobj family
#include "Space/client_objdb.h"             // ClientObjDb_FindObject (FUN_005416c0)
#include "common_archetype.h"
#include "common_cequip.h"            // CEquip, CECargo
#include "common_cobject.h"           // CObject / CSimple / CEqObj / CShip / CLoot
#include "common_csys.h"              // Acquire3DMathEngine (FUN_005b73e0)
#include "common_goods.h"             // EquipDescList / EquipDescVector / Loadout
#include "common_iface.h"             // IBehaviorManager, IDirectiveInfo, FBehaviorManagerEvent
#include "common_misc.h"              // DamageList, SUBOBJ_ID_NONE / SUBOBJ_ID_ROOT
#include "common_physys.h"            // PhySys::AnyActive / RCSet / RCEnd
#include "common_pub.h"               // pub::AI::Directive*Op (slot 100)
#include "common_reputation.h"        // Reputation::set / Vibe::SetAffiliation / SetRank
#include "common_server_types.h"      // XFireWeaponInfo .. XJettisonCargo
#include "common_text.h"              // FmtStr (slot 47)
#include "common_tstring.h"           // TString<16> (slot 82)
#include "dacom.h"                    // FDUMP
#include "Core/game_options.h"             // AssignGameSessionFlags (FUN_004c8190)
#include "Space/HUD/hud_comm.h"                 // ClientSendComm / HudCommPreloadCostume
#include "Space/HUD/hud_waypoint.h"             // EqObjCreateParmsDefaults (FUN_004f4930, slot 46)
#include "Space/HUD/hud_weapon_groups.h"        // G_WeaponGroups (DAT_00674c0c)
#include "Common/misc_util.h"                // FmtStrSprintf (FUN_0041c970, slot 47)
#include "Common/Audio/menu_sound_arch.h"          // LookupClientSoundArch / PlayClientSoundArch
#include "Interface/menu_ui_widgets.h"          // RetailU32QueueClear / PushBack (slot 120)
#include "NeuroNet/Chat/chat_init.h"                // ChatSetServerMotd (slot 115), player info (117..119)
#include "NeuroNet/nn_inventory.h"             // ReqPlayerHullStatusRetail (FUN_004c4f50)
#include "GameFlow/Thorn/gf_script_object.h"         // FGfScriptSequence::OnMissionOfferAnswered (FUN_004557d0)
#include "NeuroNet/nn_messaging.h"             // NnVoiceEnqueue (FUN_00489e80)
#include "NeuroNet/nn_mission_message.h"       // NnMissionMessagePopUp (slot 95), ShowScanNotice (109)
#include "NeuroNet/NavMap/nn_navmap.h"                // G_bNavMapNoBestPath (DAT_006720a8), FUN_0048d8a0
#include "NeuroNet/nn_player_info.h"           // NnPlayerInfoOnPlayerStats (FUN_004a8e40, slot 94)
#include "NeuroNet/nn_story_star.h"            // StoryStarLoadHistory (slot 97), StoryStarSetRankUp (117)
#include "Space/player_behavior.h"          // GetPlayerObjRW (FUN_0054baf0)
#include "Space/player_spawn.h"             // IsPlayerShipId, player ids, waypoints
#include "rpc_iclient_args.h"         // the slot argument structs shared with Server / rpclocal
#include "server_createinfo.h"        // CEqObjCreateInfo (slots 46 / 47)
#include "server_sysdb.h"             // FMineExplosion (SendServerExplodeAsteroidMine)
#include "Space/Cameras/space_camera.h"             // SetupSpaceScriptCamera / RequestSpaceCameraMode
#include "Space/space_state.h"              // EnterBaseFromDock
#include "Space/space_app.h"                // SpaceAppArmDeathCamera / SetStaticCameraTransform
#include "Space/star_sys.h"                 // StarSysSetMusicOverride / StarSysClearMusicOverride
#include "Space/Effects/tradelane_fx.h"             // the trade-lane receivers (slots 6, 7, 56)
#include "x86math.h"                  // I3DMathEngine

#include <list>
#include <stdint.h>
#include <string.h>
#include <vector>

namespace
{
    // .rdata 0x005dec60 -- IClientS.cpp's path string.
    char const kIClientSCpp[] = "E:\\FL\\Scratch\\Source\\Client\\space\\IClientS.cpp";

    // DAT_006789f8 -- IClientS.cpp's 3DMathEngine, acquired on first use by the create, launch,
    // system switch-in and set-camera slots.
    I3DMathEngine* G_IClientSMath = nullptr;

    I3DMathEngine* IClientSMath()
    {
        if (G_IClientSMath == nullptr)
        {
            G_IClientSMath = Acquire3DMathEngine();
        }
        return G_IClientSMath;
    }

    // The inline narrowing every handler repeats on the gameobj's object.
    CObject* NarrowObject(CObject* Obj, unsigned Mask)
    {
        if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & Mask) != Mask)
        {
            return nullptr;
        }
        return Obj;
    }

    CObject* NarrowGameobj(FClientGameobj const* Go, unsigned Mask)
    {
        return NarrowObject(Go->GetObject(), Mask);
    }

    bool IsPlayerGameobj(FClientGameobj const* Go)
    {
        // The FUN_0054baf0 compare the handlers make against a gameobj pointer.
        return static_cast<void const*>(static_cast<IObjRW const*>(Go)) == GetPlayerObjRW();
    }

    // IDirectiveInfo as slot 100 writes it: three dwords {First, 0, Type}.
    IDirectiveInfo MakeDirectiveInfo(unsigned First, unsigned Type)
    {
        IDirectiveInfo Info;
        unsigned const Dwords[3] = {First, 0u, Type};
        memcpy(Info.Data, Dwords, sizeof(Dwords));
        return Info;
    }

    // The local equipment vector CREATESOLAR / CREATESHIP build: the loadout's items, then
    // every item of the create info. add_equipment_item(_, false) is the plain append.
    void BuildCreateEquipment(CEqObjCreateInfo const* Info, EquipDescVector& Out)
    {
        if (Info->LoadoutId != 0)
        {
            Loadout::Map const* const Map = Loadout::Get(Info->LoadoutId);
            for (std::vector<EquipDesc>::const_iterator It = Map->Items.begin();
                 It != Map->Items.end(); ++It)
            {
                Out.add_equipment_item(*It, false);
            }
        }
        for (EquipDesc const* It = Info->Equipment.Begin; It != Info->Equipment.End; ++It)
        {
            Out.add_equipment_item(*It, false);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Shared stubs
// ---------------------------------------------------------------------------------------------

// LAB_0053aaa0 -- empty (PushToClient, SendServerSetCargo, SendServerRepairObject,
// SendServerSetStoryCue; also LocationController's GFDESTROYCHARACTER target).
void IClientImpl::PushToClient(unsigned int /*ClientId*/, CDAPacket* /*Packet*/)
{
}

void IClientImpl::SendServerSetCargo(unsigned int /*ClientId*/, const EquipDescVector& /*Cargo*/)
{
}

void IClientImpl::SendServerRepairObject(unsigned int /*ClientId*/, const FServerRepairObjectInfo& /*Info*/)
{
}

void IClientImpl::SendServerSetStoryCue(const unsigned int& /*ClientId*/, const unsigned int& /*CueId*/)
{
}

// LAB_0053e180: returns 0 (GetSendQSize and GetSendQBytes).
unsigned int IClientImpl::GetSendQSize(unsigned int /*ClientId*/)
{
    return 0;
}

unsigned int IClientImpl::GetSendQBytes(unsigned int /*ClientId*/)
{
    return 0;
}

// LAB_0053e190: returns the double DAT_005c8ba8 (0.0).
double IClientImpl::GetLinkSaturation(unsigned int /*ClientId*/)
{
    return 0.0;
}

// FUN_0053d4f0: empty (also Projectile's slot 104).
void IClientImpl::SendServerSetGuidedTarget(unsigned int /*ClientId*/, unsigned int /*GuidedId*/,
                                            unsigned int /*TargetId*/, unsigned short /*TargetSubObjId*/)
{
}

// LAB_0053e4a0: empty (SendServerPlayerListClear and DumpPacketStats).
void IClientImpl::SendServerPlayerListClear(unsigned int /*ClientId*/)
{
}

void IClientImpl::DumpPacketStats(const char* /*Name*/)
{
}

// ---------------------------------------------------------------------------------------------
// Equipment traffic (slots 0..8)
// ---------------------------------------------------------------------------------------------

// FUN_0053c2c0 -- ch1 FIREWEAPON: IObjAffect slot 13 on the gameobj (any bucket).
void IClientImpl::SendCommonFireWeapon(unsigned int /*ClientId*/, const XFireWeaponInfo& Info)
{
    FClientGameobj* const Go = ClientObjDb_FindObject(Info.ObjectId, 0);
    if (Go == nullptr)
    {
        return;
    }
    Go->fire_weapons(Info.Hardpoints.data(), static_cast<unsigned>(Info.Hardpoints.size()),
                &Info.TargetPos, 0);
}

// FUN_0053c730 -- ch1 ACTIVATEEQUIP: IObjAffect slot 19.
void IClientImpl::SendCommonActivateEquip(unsigned int /*ClientId*/, const XActivateEquip& Info)
{
    FClientGameobj* const Go = ClientObjDb_FindObject(Info.ObjectId, 0);
    if (Go == nullptr)
    {
        return;
    }
    Go->toggle_item(&Info.EquipId, Info.bActivate, 0);
}

// FUN_0053c940 -- ch1 ACTIVATECRUISE: the ship (bucket 1), IObjAffect slot 20.
void IClientImpl::SendCommonActivateCruise(unsigned int /*ClientId*/, const XActivateCruise& Info)
{
    FClientGameobj* const Go = ClientObjDb_FindObject(Info.ObjectId, 1);
    if (Go == nullptr)
    {
        return;
    }
    Go->toggle_cruise(Info.bActivate, Info.bDisruptCruise != 0, 0);
}

// FUN_0053c980 -- ch1 ACTIVATETHRUSTERS: the ship (bucket 1), IObjAffect slot 21.
void IClientImpl::SendCommonActivateThrusters(unsigned int /*ClientId*/, const XActivateThrusters& Info)
{
    FClientGameobj* const Go = ClientObjDb_FindObject(Info.ObjectId, 1);
    if (Go == nullptr)
    {
        return;
    }
    Go->toggle_thrusters(Info.bActivate, 0);
}

// FUN_0053c9b0 -- ch1 SETTARGET. Outside single player slot 106 first; the target gameobj is
// not tested. SUBOBJ_ID_NONE goes to set_target, a gun sub-object to slot 17.
void IClientImpl::SendCommonSetTarget(unsigned int /*ClientId*/, const XSetTarget& Info)
{
    FClientGameobj* const Go = ClientObjDb_FindObject(Info.ObjectId, 0);
    if (Go == nullptr)
    {
        return;
    }
    if (!SinglePlayer())
    {
        Go->SetSubTarget(Info.SubObjId, Info.TargetObjectId, Info.TargetSubObjId);
    }
    FClientGameobj* const TargetGo = ClientObjDb_FindObject(Info.TargetObjectId, 0);
    IObjRW* const Target = TargetGo;
    if (Info.SubObjId == SUBOBJ_ID_NONE)
    {
        Go->set_target(Target, Info.TargetSubObjId, 0);
        return;
    }
    Go->set_gun_target(&Info.SubObjId, Target, Info.TargetSubObjId, 0);
}

// FUN_0053c470 -- ch1 TRACTOROBJECTS: every target id that resolves, as a non-empty vector, to
// IObjAffect slot 15. The count is taken once, before the loop.
void IClientImpl::SendCommonTractorObjects(unsigned int /*ClientId*/, const XTractorObjects& Info)
{
    FClientGameobj* const Go = ClientObjDb_FindObject(Info.ObjectId, 0);
    if (Go == nullptr)
    {
        return;
    }
    std::vector<IObjRW*> Targets;
    unsigned int const Count = static_cast<unsigned int>(Info.TargetIds.size());
    for (unsigned int Index = 0; Index < Count; ++Index)
    {
        FClientGameobj* const Found = ClientObjDb_FindObject(Info.TargetIds[Index], 0);
        if (Found != nullptr)
        {
            Targets.push_back(Found);
        }
    }
    if (!Targets.empty())
    {
        Go->tractor_all(&Info.TractorEquipId, Targets, 0);
    }
}

// LAB_0053c830 -- ch2 GOTRADELANE. The client id is not read; the body is
// TradeLaneIClientGoTradelane (tradelane_fx.cpp).
void IClientImpl::SendCommonGoTradeLane(unsigned int /*ClientId*/, const XGoTradelane& Info)
{
    TradeLaneIClientGoTradelane(&Info);
}

// LAB_0053c760 -- ch2 STOPTRADELANE: (client id, ship id, entrance ring id, unused);
// the body is TradeLaneIClientStopTradelane (tradelane_fx.cpp).
void IClientImpl::SendCommonStopTradeLane(unsigned int /*ClientId*/, unsigned int ShipId,
                                          unsigned int TradelaneRing1, unsigned int /*TradelaneRing2*/)
{
    TradeLaneIClientStopTradelane(ShipId, TradelaneRing1);
}

// FUN_0053c570 -- ch1 JETTISONCARGO: IObjAffect slot 14 when the gameobj exists; then, for the
// player's ship whether or not it was found, FUN_004c4320.
void IClientImpl::SendCommonJettisonCargo(unsigned int /*ClientId*/, const XJettisonCargo& Info)
{
    FClientGameobj* const Go = ClientObjDb_FindObject(Info.ObjectId, 0);
    if (Go != nullptr)
    {
        Go->jettison_cargo(Info.ItemId, static_cast<int>(Info.Count), 0);
    }
    if (IsPlayerShipId(Info.ObjectId))
    {
        RemovePlayerEquipRetail(0, Info.ItemId, static_cast<int>(Info.Count));
    }
}

// ---------------------------------------------------------------------------------------------
// Object creation (slots 46..51)
// ---------------------------------------------------------------------------------------------

// FUN_0053ac10 -- ch2 CREATESOLAR. Nothing unless PhySys has a system. Single player attaches
// to the server's CObject (slot 91); a miss warns and deletes the gameobj. Multiplayer builds
// the CSolar itself (MultiplayerCreateSolar). Then the shared tail: companion Load(1),
// FUN_004252d0, FUN_00425480, slot 92, SetWatchListed, ObjDB registration and collisions on.
// Finally (multiplayer, with a player ship) a solar that appears over the player's ship lifts
// the ship clear of it.
namespace
{
    // The multiplayer arm of CREATESOLAR. The parms' destructor at the end is retail's inlined
    // anim-list teardown followed by FUN_004f4a10 (the +0xac list, then the +0x9c vector);
    // unwind states 2..4 of LAB_005c0b8b are the same destructors on the exception path.
    void MultiplayerCreateSolar(FClientSolarGameobj* Go, CSolarCreateInfo const* Info,
                                EquipDescVector const& Equipment)
    {
        // Nothing is null-tested.
        Archetype::Solar* const Arch = Archetype::GetSolar(Info->ArchetypeNickname);
        Arch->load();
        Go->create(Arch);
        Arch->Release();

        CSolar* const Solar = static_cast<CSolar*>(NarrowGameobj(Go, CObject::ClassSolar));

        // FUN_004f4930 (the CEqObj part), then the CSolar tail.
        CSolar::CreateParms Parms;
        EqObjCreateParmsDefaults(Parms);
        Parms.NicknameSrc.clear();
        Parms.VisitValue = 0;
        reinterpret_cast<ID_String&>(Parms.ParentNicknameId).clear();
        Parms.bDestructible = 0;
        Parms.bDynamic = 0;
        Parms.DestSystem = 0;
        reinterpret_cast<ID_String&>(Parms.DestGateId).clear();
        Parms.PrevRing = 0;
        Parms.NextRing = 0;
        Parms.TradelaneSpaceNameId = 0;
        Parms.AtmosphereRange = 0.0f;
        Parms.DockWithBaseId = 0;

        // HitPts is an integer in the record; DockingBaseId goes into both the DockingBaseId and
        // DockWithBaseId.
        Parms.HitPts = static_cast<float>(Info->HitPts);
        Parms.Id = Info->ObjId;
        Parms.ObjDb = GetClientObjDb();
        Parms.DockingBaseId = Info->DockingBaseId;
        Parms.DockWithBaseId = Info->DockingBaseId;
        Parms.Pos = Info->Position;
        Parms.OwnerPlayer = 0;

        IClientSMath()->QuaternionToMatrix(reinterpret_cast<FMatrix&>(Parms.Orient),
                                           reinterpret_cast<FQuaternion const&>(Info->Orientation));

        // The accessory count is not clamped.
        Parms.VibeId = static_cast<unsigned>(Info->VibeGroupId);
        Parms.SpaceCostume.PartIds[0] = Info->CostumeHead;
        Parms.VoiceId = Info->VoiceId;
        Parms.SpaceCostume.PartIds[1] = Info->CostumeBody;
        Parms.SpaceCostume.PartIds[2] = Info->CostumeLeftHand;
        Parms.SpaceCostume.PartIds[3] = Info->CostumeRightHand;
        Parms.SpaceCostume.AccessoryCount = Info->CostumeAccessoryCount;
        for (int Index = 0; Index < (int)Parms.SpaceCostume.AccessoryCount; ++Index)
        {
            Parms.SpaceCostume.AccessoryIds[Index] = Info->CostumeAccessory[Index];
        }

        Parms.Equipment = Equipment;
        Parms.ArchGroupDescs = Info->CollisionGroups;

        Parms.bDestructible = Info->bDestructible;
        Parms.bDynamic = 1;
        Parms.NicknameSrc.Id = Parms.Id;

        Solar->init(Parms); // the Common export, not virtual
    }
}

void IClientImpl::SendServerCreateSolar(unsigned int /*ClientId*/, const CSolarCreateInfo& Info)
{
    if (!PhySys::AnyActive())
    {
        return;
    }
    FClientSolarGameobj* const Go = new FClientSolarGameobj();
    EquipDescVector Equipment;
    BuildCreateEquipment(&Info, Equipment);

    if (SinglePlayer())
    {
        if (!Go->find(Info.ObjId))
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: SolarCreate - CObject %d not found.",
                  kIClientSCpp, 0x67, Info.ObjId);
            delete Go;
            return;
        }
    }
    else
    {
        MultiplayerCreateSolar(Go, &Info, Equipment);
    }

    // No null test on the narrowed object.
    CObject* const Obj = NarrowGameobj(Go, CObject::ClassSolar);
    Obj->get_archetype()->Companion->Load(1);
    Archetype::LoadMountedEquipmentCompanions(&Equipment, 1);
    Archetype::HookUpInstanceTreeMaterials(Obj->EngineInstanceId);
    Go->PostCreateInit();
    Go->SetWatchListed(static_cast<int>(GetPlayerConnectionId()), Info.bMarkedBy);
    ClientObjDb_RegisterGameobj(Go, false);
    Obj->enable_collisions_r(true);

    // The player's gameobj is fetched before the single-player test.
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    if (SinglePlayer() || Player == nullptr)
    {
        return;
    }

    // Both physical spheres; the return values are not read.
    Vector SolarCenter = Go->get_position();
    float SolarRadius = 0.0f;
    Go->get_physical_radius(SolarRadius, SolarCenter);
    Vector PlayerCenter = Player->get_position();
    float PlayerRadius = 0.0f;
    Player->get_physical_radius(PlayerRadius, PlayerCenter);

    // The differences and the summed radius stay on the FPU; the sum is also stored as a
    // float. The squared distance adds dz*dz + dy*dy first, then dx*dx.
    double const Dx = static_cast<double>(SolarCenter.x) - PlayerCenter.x;
    double const Dy = static_cast<double>(SolarCenter.y) - PlayerCenter.y;
    double const Dz = static_cast<double>(SolarCenter.z) - PlayerCenter.z;
    double const Reach = static_cast<double>(PlayerRadius) + SolarRadius;
    float const ReachStored = static_cast<float>(Reach);
    double const DistanceSq = Dz * Dz + Dy * Dy + Dx * Dx;
    if (!(Reach * Reach > DistanceSq))
    {
        return;
    }

    // Only y changes. Below the player: centre.y + both radii on the FPU; otherwise (also
    // unordered) centre.y - the stored float sum.
    Vector Clear;
    Clear.x = SolarCenter.x;
    Clear.z = SolarCenter.z;
    if (SolarCenter.y < PlayerCenter.y)
    {
        Clear.y = static_cast<float>(static_cast<double>(SolarCenter.y) + PlayerRadius + SolarRadius);
    }
    else
    {
        Clear.y = static_cast<float>(static_cast<double>(SolarCenter.y) - ReachStored);
    }

    // The player's CShip is beamed there with its own rotation.
    CObject* const PlayerShip = NarrowObject(const_cast<CObject*>(Player->cobject()), 0x503);
    if (PlayerShip == nullptr)
    {
        return;
    }
    PlayerShip->beam_object(Clear, Player->get_transform().Rotation, false);
}

// FUN_0053b220 -- ch2 CREATESHIP. As CREATESOLAR with a Ship gameobj; a single-player hit with
// bMarkedBy first joins the watch list. Then, outside single player, the reputation name
// block.
namespace
{
    // The multiplayer arm of CREATESHIP.
    void MultiplayerCreateShip(FClientShipGameobj* Go, CShipCreateInfo const* Info,
                               EquipDescVector const& Equipment)
    {
        // Nothing is null-tested.
        Archetype::Ship* const Arch = Archetype::GetShip(Info->ArchetypeNickname);
        Arch->load();
        Go->create(Arch);
        Arch->Release();

        CShip* const Ship = static_cast<CShip*>(NarrowGameobj(Go, CObject::ClassShip));

        // FUN_0052e0a0 on the CSimple head, FUN_004cdc10 on the costume. Port deviation:
        // retail leaves +0x74..+0x90 and +0xb8..+0xe7 as they lie; value-initialisation zeroes
        // them, and CShip::init reads neither.
        CShip::CreateParms Parms{};
        InitCreateParmsScratchRetail(&Parms);
        Parms.DockingBaseId = 0;
        Parms.VibeId = 0xffffffffu;
        Parms.SpaceCostume.PartIds[0] = 0;
        Parms.SpaceCostume.PartIds[1] = 0;
        Parms.SpaceCostume.PartIds[2] = 0;
        Parms.SpaceCostume.PartIds[3] = 0;
        Parms.SpaceCostume.AccessoryCount = 0;
        Parms.VoiceId = 0;
        Parms.FieldE8 = 0;
        Parms.GroupNameId = 0;
        Parms.Rank = 0;
        Parms.bUsingTradelane = 0;

        Parms.HitPts = static_cast<float>(Info->HitPts);
        Parms.Id = Info->ObjId;
        Parms.ObjDb = GetClientObjDb();
        Parms.OwnerPlayer = Info->OwnerPlayer;
        Parms.Pos = Info->Position;

        IClientSMath()->QuaternionToMatrix(reinterpret_cast<FMatrix&>(Parms.Orient),
                                           reinterpret_cast<FQuaternion const&>(Info->Orientation));

        // The accessory count is not clamped.
        Parms.Velocity = Info->Velocity;
        Parms.AngularVelocity = Info->AngularVelocity;
        Parms.VibeId = static_cast<unsigned>(Info->VibeGroupId);
        Parms.VoiceId = Info->VoiceId;
        Parms.Rank = Info->ReputationRank;
        Parms.SpaceCostume.PartIds[0] = Info->CostumeHead;
        Parms.SpaceCostume.PartIds[1] = Info->CostumeBody;
        Parms.SpaceCostume.PartIds[2] = Info->CostumeLeftHand;
        Parms.SpaceCostume.PartIds[3] = Info->CostumeRightHand;
        Parms.SpaceCostume.AccessoryCount = Info->CostumeAccessoryCount;
        for (int Index = 0; Index < (int)Parms.SpaceCostume.AccessoryCount; ++Index)
        {
            Parms.SpaceCostume.AccessoryIds[Index] = Info->CostumeAccessory[Index];
        }

        Parms.Equipment = Equipment;
        Parms.ArchGroupDescs = Info->CollisionGroups;

        // A marked record puts this connection's id into +0x58.
        Parms.GroupNameId = Info->GroupNameId;
        Parms.DockingBaseId = Info->DockingBaseId;
        if (Info->bMarkedBy != 0)
        {
            Parms.Pad58 = GetPlayerConnectionId();
        }
        Parms.bUsingTradelane = Info->bUsingTradelane;

        Ship->init(Parms);

        Ship->GroupId = Info->GroupId;
        PhySys::RCBegin(Ship, Info->Position, Info->Velocity, Info->Orientation,
                        Info->AngularVelocity);
    }
}

void IClientImpl::SendServerCreateShip(unsigned int /*ClientId*/, const CShipCreateInfo& Info)
{
    if (!PhySys::AnyActive())
    {
        return;
    }
    FClientShipGameobj* const Go = new FClientShipGameobj();
    EquipDescVector Equipment;
    BuildCreateEquipment(&Info, Equipment);

    if (SinglePlayer())
    {
        if (!Go->find(Info.ObjId))
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: ShipCreate - CObject %d not found.",
                  kIClientSCpp, 0xff, Info.ObjId);
            delete Go;
            return;
        }
        if (Info.bMarkedBy != 0)
        {
            Go->SetWatchListed(static_cast<int>(GetPlayerConnectionId()), 1);
        }
    }
    else
    {
        MultiplayerCreateShip(Go, &Info, Equipment);
    }

    // No null test on the narrowed object.
    CObject* const Obj = NarrowGameobj(Go, CObject::ClassShip);
    Obj->get_archetype()->Companion->Load(1);
    Archetype::LoadMountedEquipmentCompanions(&Equipment, 1);
    Archetype::HookUpInstanceTreeMaterials(Obj->EngineInstanceId);
    Go->PostCreateInit();
    Go->SetWatchListed(static_cast<int>(GetPlayerConnectionId()), Info.bMarkedBy);
    ClientObjDb_RegisterGameobj(Go, false);

    // Outside single player, the record's reputation group re-gets its names and, when the
    // second name carries a mad-lib id, sets them back with that name printed into a 0x18-char
    // buffer (FUN_0041c970). The return values are not read.
    if (SinglePlayer())
    {
        return;
    }
    FmtStr Name1;
    FmtStr Name2;
    wchar_t const* DisplayName = nullptr;
    Reputation::Vibe::GetName(Info.VibeGroupId, Name1, Name2, DisplayName);
    if (Name2.MadLibId != 0)
    {
        wchar_t Printed[0x18];
        FmtStrSprintf(&Name2, Printed, 0x18);
        Reputation::Vibe::SetName(Info.VibeGroupId, Name1, Name2, Printed);
    }
}

namespace
{
    // DAT_005dd55c -- 1e10, the "far away" scale the dropper tails aim with.
    constexpr float kIClientFarScale = 1.0e10f;

    // The launcher tail CREATEMINE / CREATECOUNTER / CREATEGUIDED share, on the single-player
    // miss path too: the equipment wrapper (FUN_00533100) of SubObjId on a remote owner's
    // gameobj. Retail then calls the wrapper's slot 5 -- FireDirect of the launcher family, the
    // only wrappers a munition's launcher id can name.
    FCliLauncherFx* FindRemoteLauncher(unsigned OwnerId, unsigned short SubObjId)
    {
        if (OwnerId == 0)
        {
            return nullptr;
        }
        if (IsPlayerShipId(OwnerId))
        {
            return nullptr;
        }
        FClientGameobj* const Owner = ClientObjDb_FindObject(OwnerId, 0);
        if (Owner == nullptr)
        {
            return nullptr;
        }
        FClientEquip* const Wrapper = EquippedObjFindEquipFx(Owner, SubObjId);
        if (Wrapper == nullptr || Wrapper->Equip == nullptr)
        {
            return nullptr;
        }
        return static_cast<FCliLauncherFx*>(Wrapper);
    }

    // CREATEMINE / CREATECOUNTER (FUN_0053ca40 / FUN_0053cd70, one body apart from the gameobj
    // type and the class mask 0xf / 0x17). The multiplayer CreateParms: the Mine build zeroes
    // Id..Pos and stores HitPts -1 first, the CounterMeasure build only the HitPts -- both then
    // overwrite every one of those, so the default CProjectile::CreateParms is the same record.
    void CreateDroppedProjectile(FClientProjectileGameobj* Go, unsigned Mask,
                                 FServerCreateProjectileInfo const* Info)
    {
        if (SinglePlayer())
        {
            // A miss deletes the gameobj (no warning) and goes straight to the launcher tail.
            if (!Go->find(Info->ObjId))
            {
                delete Go;
                Go = nullptr;
            }
        }
        else
        {
            Archetype::Equipment* const Arch = Archetype::GetEquipment(Info->ArchetypeNickname);
            Arch->load();
            Go->create(Arch);
            Arch->Release();
            // No null test on the narrowed object.
            CProjectile* const Shot = static_cast<CProjectile*>(NarrowGameobj(Go, Mask));
            CProjectile::CreateParms Parms;
            Parms.Id = Info->ObjId;
            Parms.ObjDb = GetClientObjDb();
            Parms.OwnerPlayer = Info->OwnerPlayer;
            Parms.Pos = Info->Position;
            IClientSMath()->QuaternionToMatrix(reinterpret_cast<FMatrix&>(Parms.Orient),
                                               reinterpret_cast<FQuaternion const&>(Info->Orientation));
            Parms.Velocity = Info->Velocity;
            Parms.AngularVelocity = Info->AngularVelocity;
            Parms.OwnerId = Info->OwnerId;
            Shot->init(Parms);
            PhySys::RCBegin(static_cast<CSimple*>(Shot), Info->Position, Info->Velocity,
                            Info->Orientation, Info->AngularVelocity);
        }
        if (Go != nullptr)
        {
            // No null test on the narrowed object.
            CObject* const Obj = NarrowGameobj(Go, Mask);
            Obj->get_archetype()->Companion->Load(1);
            Archetype::HookUpInstanceTreeMaterials(Obj->EngineInstanceId);
            Go->PostCreateInit();
            ClientObjDb_RegisterGameobj(Go, false);
        }

        // The dropper that let it go fires at Position + 1e10 * Velocity. The x and y products
        // stay on the FPU; z's is stored to a float before the add.
        FCliLauncherFx* const Launcher = FindRemoteLauncher(Info->OwnerId, Info->SubObjId);
        if (Launcher == nullptr)
        {
            return;
        }
        Vector Target;
        Target.x = static_cast<float>(static_cast<double>(Info->Velocity.x) * kIClientFarScale +
                                      Info->Position.x);
        Target.y = static_cast<float>(static_cast<double>(Info->Velocity.y) * kIClientFarScale +
                                      Info->Position.y);
        float const FarZ = static_cast<float>(static_cast<double>(Info->Velocity.z) * kIClientFarScale);
        Target.z = FarZ + Info->Position.z;
        Launcher->FireDirect(&Target);
    }
}

// FUN_0053b910 -- ch2 CREATELOOT: one Loot gameobj per item (FServerLootDesc stride 0x48);
// then, for a kind-1 request, the owner's gameobj plays the cargo-jettisoned sound.
void IClientImpl::SendServerCreateLoot(unsigned int /*ClientId*/, const FServerCreateLootRequest& Info)
{
    if (!PhySys::AnyActive())
    {
        return;
    }
    FServerLootDesc const* Desc = Info.Items;
    for (unsigned Left = Info.Count; Left != 0; --Left, ++Desc)
    {
        FClientLootGameobj* const Go = new FClientLootGameobj();
        if (SinglePlayer())
        {
            // A miss warns, deletes the gameobj, and the next item follows.
            if (!Go->find(Desc->LootId))
            {
                FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kIClientSCpp, 0x17c,
                      "LootCreate - CObject not found.");
                delete Go;
                continue;
            }
        }
        else
        {
            Archetype::Equipment* const Arch = Archetype::GetEquipment(Desc->ContainerNickname);
            Arch->load();
            Go->create(Arch);
            Arch->Release();
            // No null test on the narrowed CLoot.
            CLoot* const Loot = static_cast<CLoot*>(NarrowGameobj(Go, CObject::ClassLoot));
            CLoot::CreateParms Parms;
            Parms.Id = Desc->LootId;
            Parms.ObjDb = GetClientObjDb();
            Parms.OwnerPlayer = 0;
            Parms.HitPts = Desc->HitPts;
            Parms.Pos = Desc->Position;
            IClientSMath()->QuaternionToMatrix(reinterpret_cast<FMatrix&>(Parms.Orient),
                                               reinterpret_cast<FQuaternion const&>(Desc->Orientation));
            Parms.Velocity = Desc->Velocity;
            Parms.AngularVelocity.x = 0.0f;
            Parms.AngularVelocity.y = 0.0f;
            Parms.AngularVelocity.z = 0.0f;
            Parms.Pad58 = 0;
            Parms.OwnerId = Info.OwnerId;
            Parms.NameId = static_cast<int>(Desc->NameOverride);
            Parms.ContentsArch = Archetype::GetEquipment(Desc->ContentsNickname);
            Parms.Units = Desc->Units;
            Parms.ContentsHitPts = Desc->ContentsHitPts;
            Parms.bCanAiTractor = true;
            Parms.bTemporary = (Desc->bTemporary != 0);
            Loot->init(Parms);
            Vector NoSpin;
            NoSpin.x = 0.0f;
            NoSpin.y = 0.0f;
            NoSpin.z = 0.0f;
            PhySys::RCBegin(Loot, Desc->Position, Desc->Velocity, Desc->Orientation, NoSpin);
        }

        // No null test on the narrowed object.
        CObject* const Obj = NarrowGameobj(Go, CObject::ClassLoot);
        Obj->get_archetype()->Companion->Load(1);
        Archetype::HookUpInstanceTreeMaterials(Go->get_index());
        Go->PostCreateInit();
        Go->SetWatchListed(static_cast<int>(GetPlayerConnectionId()), Desc->bMarkedBy);
        ClientObjDb_RegisterGameobj(Go, false);
    }

    // Retail calls FUN_00543f50 on whatever gameobj the owner id finds; its body narrows to the
    // CShip itself.
    if (Info.Kind == 1)
    {
        FClientGameobj* const Owner = ClientObjDb_FindObject(Info.OwnerId, 0);
        if (Owner != nullptr)
        {
            static_cast<FClientShipGameobj*>(Owner)->PlayCargoJettisonSound();
        }
    }
}

// FUN_0053ca40 -- ch2 CREATEMINE.
void IClientImpl::SendServerCreateMine(unsigned int /*ClientId*/, const FServerCreateProjectileInfo& Info)
{
    if (!PhySys::AnyActive())
    {
        return;
    }
    CreateDroppedProjectile(new FClientMineGameobj(), 0xf, &Info);
}

// FUN_0053d080 -- ch2 CREATEGUIDED. ClientId is not read.
void IClientImpl::SendServerCreateGuided(const unsigned int& /*ClientId*/, const FServerCreateGuidedInfo& Info)
{
    if (!PhySys::AnyActive())
    {
        return;
    }
    FClientGuidedGameobj* Go = new FClientGuidedGameobj();
    if (SinglePlayer())
    {
        // A miss deletes the gameobj and goes to the launcher tail.
        if (!Go->find(Info.ObjId))
        {
            delete Go;
            Go = nullptr;
        }
    }
    else
    {
        Archetype::Equipment* const Arch = Archetype::GetEquipment(Info.ArchetypeNickname);
        Arch->load();
        Go->create(Arch);
        Arch->Release();
        // No null test on the narrowed CGuided; init is the Common export, not virtual.
        CGuided* const Guided = static_cast<CGuided*>(NarrowGameobj(Go, CObject::ClassGuided));
        CGuided::CreateParms Parms;
        Parms.SubTarget = SUBOBJ_ID_NONE;
        Parms.OwnerPlayer = Info.OwnerPlayer;
        Parms.Id = Info.ObjId;
        Parms.ObjDb = GetClientObjDb();
        Parms.Pos = Info.Position;
        IClientSMath()->QuaternionToMatrix(reinterpret_cast<FMatrix&>(Parms.Orient),
                                           reinterpret_cast<FQuaternion const&>(Info.Orientation));
        Parms.OwnerId = Info.OwnerId;
        FClientGameobj* const TargetGo = ClientObjDb_FindObject(Info.TargetId, 0);
        Parms.SubTarget = Info.TargetSubObjId;
        Parms.Target = static_cast<IObjRW*>(TargetGo);
        Guided->init(Parms);
        PhySys::RCBegin(Guided, Info.Position, Parms.Velocity, Info.Orientation,
                        Parms.AngularVelocity);
    }
    if (Go != nullptr)
    {
        CObject* const Obj = NarrowGameobj(Go, CObject::ClassGuided);
        Obj->get_archetype()->Companion->Load(1);
        Archetype::HookUpInstanceTreeMaterials(Go->get_index());
        Go->PostCreateInit();
        ClientObjDb_RegisterGameobj(Go, false);
    }

    // The launcher that fired it fires at Position minus 1e10 times the orientation's third
    // column. The x and y products are stored to floats before the add; z's stays on the FPU.
    FCliLauncherFx* const Launcher = FindRemoteLauncher(Info.OwnerId, Info.SubObjId);
    if (Launcher == nullptr)
    {
        return;
    }
    Matrix Orient;
    IClientSMath()->QuaternionToMatrix(reinterpret_cast<FMatrix&>(Orient),
                                       reinterpret_cast<FQuaternion const&>(Info.Orientation));
    float const FarX = static_cast<float>(-static_cast<double>(Orient.E[0][2]) * kIClientFarScale);
    float const FarY = static_cast<float>(-static_cast<double>(Orient.E[1][2]) * kIClientFarScale);
    Vector Target;
    Target.x = FarX + Info.Position.x;
    Target.y = FarY + Info.Position.y;
    Target.z = static_cast<float>(-static_cast<double>(Orient.E[2][2]) * kIClientFarScale +
                                  Info.Position.z);
    Launcher->FireDirect(&Target);
}

// FUN_0053cd70 -- ch2 CREATECOUNTER. ClientId is not read.
void IClientImpl::SendServerCreateCounter(const unsigned int& /*ClientId*/,
                                          const FServerCreateProjectileInfo& Info)
{
    if (!PhySys::AnyActive())
    {
        return;
    }
    CreateDroppedProjectile(new FClientCounterMeasureGameobj(), 0x17, &Info);
}

// ---------------------------------------------------------------------------------------------
// Object state (slots 52..63)
// ---------------------------------------------------------------------------------------------

// FUN_0053d480 -- ch2 EXPLODEASTEROIDMINE: the gameobj of bucket 8 explodes at its world
// position + offset. No null test on the narrowed object.
void IClientImpl::SendServerExplodeAsteroidMine(unsigned int /*ClientId*/, const FMineExplosion& Info)
{
    FClientGameobj* const Go = ClientObjDb_FindObject(Info.MineId, 8);
    if (Go == nullptr)
    {
        return;
    }
    CObject* const Obj = NarrowGameobj(Go, CObject::ClassObject);
    Vector const& Offset = Info.Offset;
    Vector Pos;
    Pos.x = Offset.x + Obj->WorldPosition.x;
    Pos.y = Obj->WorldPosition.y + Offset.y;
    Pos.z = Obj->WorldPosition.z + Offset.z;
    Go->Explode(&Pos);
}

// LAB_0053bd40 -- ch1 UPDATEOBJECT: PhySys::RCSet (a failed CSimple narrow passes null); a
// CShip also takes the throttle and the state byte (+0x1a0).
void IClientImpl::SendCommonUpdateObject(unsigned int /*ClientId*/, const FCommonUpdateObjectInfo& Info)
{
    FClientGameobj* const Go = ClientObjDb_FindObject(Info.ObjectId, 0);
    if (Go == nullptr)
    {
        return;
    }
    CSimple* const Simple = static_cast<CSimple*>(NarrowGameobj(Go, CObject::ClassSimple));
    PhySys::RCSet(Simple, Info.Position, Info.Orientation, static_cast<double>(Info.Timestamp));
    CShip* const Ship = static_cast<CShip*>(NarrowGameobj(Go, CObject::ClassShip));
    if (Ship == nullptr)
    {
        return;
    }
    Ship->set_throttle(Info.Throttle);
    Ship->BehaviorIndex = static_cast<int>(Info.State);
}

// FUN_0053bc80 -- ch2 DESTROYOBJECT. The player's ship asks for a zero hull status on type 1
// and captures the camera view (FUN_0054b900); any other gameobj is owned by this connection
// and its CSimple (not tested) re-connects to the client ObjDB. Then slot 103 and, outside
// single player for type 0, the gameobj is unregistered and deleted.
void IClientImpl::SendServerDestroyObject(unsigned int /*ClientId*/, const FServerDestroyObjectInfo& Info)
{
    // Retail reads the destroy type as the whole dword at +0x04, i.e. Field_0x04 together
    // with the three pad bytes after it.
    unsigned int DestroyType = 0;
    memcpy(&DestroyType, reinterpret_cast<unsigned char const*>(&Info) + 4, sizeof(DestroyType));
    FClientGameobj* const Go = ClientObjDb_FindObject(Info.ObjectId, 0);
    if (Go == nullptr)
    {
        return;
    }
    if (IsPlayerShipId(Info.ObjectId))
    {
        if (DestroyType == 1)
        {
            ReqPlayerHullStatusRetail(0.0f);
        }
        SpaceAppArmDeathCamera();
    }
    else
    {
        Go->SetOwnerPlayer(GetPlayerConnectionId());
        CSimple* const Simple = static_cast<CSimple*>(NarrowGameobj(Go, CObject::ClassSimple));
        Simple->connect(GetClientObjDb());
    }
    Go->Destroy(static_cast<int>(DestroyType));
    if (SinglePlayer() || DestroyType != 0)
    {
        return;
    }
    ClientObjDb_Unregister(Go);
    delete Go;
}

// FUN_0053d940 -- ch2 ACTIVATEOBJECT; the body is TradeLaneSPObjActivate (tradelane_fx.cpp).
// Retail reads the argument with no null test.
void IClientImpl::SendServerActivateObject(unsigned int /*ClientId*/, const FPayloadServerActivateObject& Info)
{
    TradeLaneSPObjActivate(Info.ObjectId, Info.bActivate != 0, Info.ActivateParam);
}

// LAB_0053d500 -- ch2 SYSTEM_SWITCH_OUT.
void IClientImpl::SendServerSystemSwitchOut(unsigned int /*ClientId*/, const FServerSystemSwitchOutInfo& Info)
{
    FClientShipGameobj* const Ship =
        static_cast<FClientShipGameobj*>(ClientObjDb_FindObject(Info.ShipId, 1));
    if (Ship == nullptr)
    {
        return;
    }

    // A missing gate is an ERROR, and the handler goes on (retail reads the gate next with no
    // test).
    FClientGameobj* const Gate = ClientObjDb_FindObject(Info.GateId, 2);
    if (Gate == nullptr)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", kIClientSCpp, 0x54e,
              "No facilitator found for SystemSwitchOut");
    }
    // A gate gameobj that is not a solar (ManagerKind 2) warns.
    if (Gate->ManagerKind != EClientGameobjKind::Solar)
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kIClientSCpp, 0x550,
              "SystemSwitchOut, obj is not a Solar?");
    }

    // The player's behavior manager hears event 3 with {gate id, gate type, ship id}. No null
    // test on the narrowed CEqObj.
    FClientGameobj* const Player = static_cast<FClientGameobj*>(static_cast<IObjRW*>(GetPlayerObjRW()));
    if (Player != nullptr)
    {
        CEqObj* const PlayerEq = static_cast<CEqObj*>(NarrowGameobj(Player, CObject::ClassEqObj));
        IBehaviorManager* const Behavior = PlayerEq->get_behavior_interface();
        if (Behavior != nullptr)
        {
            unsigned int Block[3];
            Block[0] = Info.GateId;
            Block[2] = Info.ShipId;
            unsigned GateType = 0;
            Gate->get_type(GateType);
            Block[1] = GateType;
            FBehaviorManagerEvent Event;
            Event.Type = 3;
            Event.Data = Block;
            Behavior->OnEvent(&Event);
        }
    }

    // Any ship but the player's: owned by this connection and control exclusion 1 (no null
    // test on the narrowed CEqObj).
    if (!IsPlayerGameobj(Ship))
    {
        Ship->SetOwnerPlayer(GetPlayerConnectionId());
        CEqObj* const ShipEq = static_cast<CEqObj*>(NarrowGameobj(Ship, CObject::ClassEqObj));
        ShipEq->set_control_exclusion(1);
    }

    // Gate type bit 0x40 or 0x800 -> the jump out through it; otherwise the system switch
    // straight away.
    unsigned Type = 0;
    Gate->get_type(Type);
    if ((Type & 0x40u) != 0 || (Type & 0x800u) != 0)
    {
        Ship->BeginJumpOut(Gate);
    }
    else
    {
        Ship->RequestSystemSwitch(Gate);
    }
}

// LAB_0053d680 -- ch2 SYSTEM_SWITCH_IN.
void IClientImpl::SendServerSystemSwitchIn(unsigned int /*ClientId*/, const FPayloadServerSystemSwitchIn& Info)
{
    FClientShipGameobj* const Ship =
        static_cast<FClientShipGameobj*>(ClientObjDb_FindObject(Info.ShipId, 0));
    if (Ship == nullptr)
    {
        return;
    }

    // Retail converts the orientation on both paths.
    Matrix Orient;
    IClientSMath()->QuaternionToMatrix(reinterpret_cast<FMatrix&>(Orient),
                                       reinterpret_cast<FQuaternion const&>(Info.Orientation));

    // Jump type bits 0x40 or 0x800 -> the jump in; otherwise the airlock.
    if ((Info.JumpObjTypeBits & OBJ_JUMP_GATE) != 0 || (Info.JumpObjTypeBits & OBJ_JUMP_HOLE) != 0)
    {
        Ship->BeginJumpIn(&Info.Position, &Orient);
    }
    else
    {
        Ship->AirlockIn(Info.Position, Orient);
    }
}

// FUN_0053d720 -- ch2 LAND.
void IClientImpl::SendServerLand(unsigned int /*ClientId*/, const FServerLandInfo& Info)
{
    FClientShipGameobj* const Ship =
        static_cast<FClientShipGameobj*>(ClientObjDb_FindObject(Info.ShipId, 0));
    if (Ship == nullptr)
    {
        return;
    }

    // With a dock: the player's ship enters the dock's base at once (FUN_0054bc60; no null test
    // on the narrowed dock); any other ship is owned by this connection.
    FClientGameobj* const Dock = ClientObjDb_FindObject(Info.DockId, 0);
    if (Dock != nullptr)
    {
        if (IsPlayerGameobj(Ship))
        {
            CEqObj* const DockEq = static_cast<CEqObj*>(NarrowGameobj(Dock, CObject::ClassSolar));
            EnterBaseFromDock(&DockEq->DockingPointBaseId);
        }
        else
        {
            Ship->SetOwnerPlayer(GetPlayerConnectionId());
        }
    }

    // Outside single player the ship's remote-control session ends.
    if (!SinglePlayer())
    {
        CObject* const Obj = const_cast<CObject*>(Ship->cobject());
        PhySys::RCEnd(static_cast<CSimple*>(NarrowObject(Obj, CObject::ClassSimple)));
    }

    Ship->RequestLand(Dock, &Info.BaseId);
}

// LAB_0053d7e0 -- ch2 LAUNCH.
void IClientImpl::SendServerLaunch(unsigned int /*ClientId*/, const FServerLaunchInfo& Info)
{
    FClientShipGameobj* const Ship =
        static_cast<FClientShipGameobj*>(ClientObjDb_FindObject(Info.ShipId, 1));
    if (Ship == nullptr)
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: Launch (): launching object %d not found.",
              kIClientSCpp, 0x5c4, Info.ShipId);
        return;
    }

    // The ship's object (no null test) is beamed to the launch position -- outside single
    // player when launching from a dock, always when not.
    if (Info.LaunchObjId != 0)
    {
        if (!SinglePlayer())
        {
            Matrix Orient;
            IClientSMath()->QuaternionToMatrix(
                reinterpret_cast<FMatrix&>(Orient),
                reinterpret_cast<FQuaternion const&>(Info.Orientation));
            NarrowGameobj(Ship, CObject::ClassObject)->beam_object(Info.Position, Orient, false);
        }
        Ship->LaunchFromDock(ClientObjDb_FindObject(Info.LaunchObjId, 0),
                             static_cast<int>(Info.LaunchPosIndex));
    }
    else
    {
        Matrix Orient;
        IClientSMath()->QuaternionToMatrix(
            reinterpret_cast<FMatrix&>(Orient),
            reinterpret_cast<FQuaternion const&>(Info.Orientation));
        NarrowGameobj(Ship, CObject::ClassObject)->beam_object(Info.Position, Orient, false);
    }

    SetPlayerLaunchAcknowledged(true); // DAT_00678b96
}

// LAB_0053d920 -- ch2 REQUESTCREATESHIPRESP: DAT_00678b95 = 1, DAT_00678b94 = bSuccess,
// DAT_00673378 = ShipId.
void IClientImpl::SendServerRequestCreateShipResp(unsigned int /*ClientId*/, bool bSuccess, unsigned int ShipId)
{
    SetPlayerCreateShipResponse(static_cast<unsigned char>(bSuccess), ShipId);
}

// LAB_0053d9d0 -- ch2 REQUEST_SPACE_SCRIPT. The first argument is never read.
void IClientImpl::SendServerRequestSpaceScript(unsigned int /*ClientId*/, const FServerRequestSpaceScriptInfo& Info)
{
    FClientGameobj* const Object = ClientObjDb_FindObject(Info.ObjectId, 0);
    if (Object == nullptr)
    {
        return;
    }

    // Mode 1: the script camera (FUN_0054bb10) keeps the transform it has.
    if (Info.CameraMode == 1u)
    {
        SetupSpaceScriptCamera(Object, nullptr, Info.Name);
        return;
    }

    // Mode 2: identity orientation at the given position. Any other mode does nothing.
    if (Info.CameraMode != 2u)
    {
        return;
    }
    float Transform[12];
    Transform[0] = 1.0f;
    Transform[1] = 0.0f;
    Transform[2] = 0.0f;
    Transform[3] = 0.0f;
    Transform[4] = 1.0f;
    Transform[5] = 0.0f;
    Transform[6] = 0.0f;
    Transform[7] = 0.0f;
    Transform[8] = 1.0f;
    memcpy(&Transform[9], &Info.PosXBits, sizeof(float));
    memcpy(&Transform[10], &Info.PosYBits, sizeof(float));
    memcpy(&Transform[11], &Info.PosZBits, sizeof(float));
    SetupSpaceScriptCamera(Object, Transform, Info.Name);
}

// LAB_0053bdd0 -- ch2 DAMAGEOBJECT. The gameobj takes the damage (slot 102); for the player's
// ship every entry then updates the player's own records (no null test on the narrowed CEqObj):
// equipment (FUN_004c4590 = FUN_004c43d0, or FUN_004c4320 when gone or separated), collision
// groups (FUN_004c4000) and the hull (FUN_004c4f80). Retail constructs one more EquipDesc that
// nothing reads.
void IClientImpl::SendServerDamageObject(unsigned int /*ClientId*/, unsigned int ObjectId, const DamageList& Damage)
{
    FClientGameobj* const Go = ClientObjDb_FindObject(ObjectId, 0);
    if (Go == nullptr)
    {
        return;
    }
    Go->ApplyDamageList(Damage);
    if (!IsPlayerShipId(ObjectId))
    {
        return;
    }
    CEqObj* const Eq = static_cast<CEqObj*>(NarrowGameobj(Go, CObject::ClassEqObj));
    for (std::list<DamageEntry>::const_iterator It = Damage.Entries.begin();
         It != Damage.Entries.end(); ++It)
    {
        unsigned short const Id = It->SubObjId;
        if (SubObjectID::IsEquipID(Id))
        {
            if (It->Fate != DamageEntry::SubObjFate::None)
            {
                RemovePlayerEquipRetail(0, Id, -1);
                continue;
            }
            CEquip* const Equip = Eq->EquipManager.FindByID(Id);
            if (Equip == nullptr)
            {
                RemovePlayerEquipRetail(0, Id, -1);
                continue;
            }
            EquipDesc Desc;
            if (Equip->GetEquipDesc(Desc))
            {
                ModifyPlayerEquipRetail(Desc.Id, &Desc.get_hardpoint(), Desc.Count, Desc.Health,
                                        Desc.bEquipped);
            }
        }
        else if (SubObjectID::IsArchGroupID(Id))
        {
            CArchGroup* const Group = Eq->ArchGroups.FindByID(Id);
            if (Group != nullptr)
            {
                SetPlayerCollisionGroupHealthRetail(Id, Group->GetRelativeHealth());
            }
        }
        else if (Id == SUBOBJ_ID_ROOT)
        {
            SetPlayerHealthScaleRetail(Eq->get_relative_health());
        }
    }
}

// FUN_0053c5d0 -- ch2 ITEMTRACTORED. The tractoring ship's equipment-FX wrapper
// (FUN_00533100) lets the tractor drop the loot (FUN_0052f9a0). Then, outside single player,
// the loot (CLoot, not tested) loses the units.
void IClientImpl::SendServerItemTractored(unsigned int /*ClientId*/, const FPayloadServerItemTractored& Info)
{
    FClientGameobj* const Tractoring = ClientObjDb_FindObject(Info.ShipId, 0);
    if (Tractoring != nullptr)
    {
        FClientEquip* const Wrapper = EquippedObjFindEquipFx(Tractoring, Info.TractorEquipId);
        if (Wrapper != nullptr)
        {
            CliTractorEquipFxDropLoot(Wrapper, Info.LootId,
                                      static_cast<int>(Info.UnitsTaken));
        }
    }
    if (SinglePlayer())
    {
        return;
    }
    FClientGameobj* const LootGo = ClientObjDb_FindObject(Info.LootId, 0);
    if (LootGo == nullptr)
    {
        return;
    }
    CLoot* const Loot = static_cast<CLoot*>(NarrowGameobj(LootGo, CObject::ClassLoot));
    Loot->Units -= Info.UnitsTaken;
}

// LAB_0053c660 -- ch2 USE_ITEM. Outside single player the gameobj's cargo stack shrinks (no
// null test on the narrowed CEqObj). Then, for the player's ship, the player's own item
// (DAT_00672960) through FUN_004c4320.
void IClientImpl::SendServerUseItem(unsigned int /*ClientId*/, const FPayloadServerUseItem& Info)
{
    if (!SinglePlayer())
    {
        FClientGameobj* const Go = ClientObjDb_FindObject(Info.ObjectId, 0);
        if (Go != nullptr)
        {
            CEqObj* const Eq = static_cast<CEqObj*>(NarrowGameobj(Go, CObject::ClassEqObj));
            CECargo* const Cargo = CECargo::cast(Eq->EquipManager.FindByID(Info.EquipId));
            if (Cargo != nullptr)
            {
                Cargo->RemoveFromStack(Info.Count);
            }
        }
    }
    if (!IsPlayerShipId(Info.ObjectId))
    {
        return;
    }
    EquipDesc* const Desc = GetPlayerEquipDescListRetail().find_equipment_item(Info.EquipId);
    if (Desc == nullptr)
    {
        return;
    }
    int const Count = static_cast<int>(Info.Count);
    if (Desc->Count > Count)
    {
        RemovePlayerEquipRetail(1, Desc->Id, Count);
        return;
    }
    RemovePlayerEquipRetail(0, Desc->Id, Count);
}

// ---------------------------------------------------------------------------------------------
// Comm / reputation / cargo (slots 68..89)
// ---------------------------------------------------------------------------------------------

// LAB_0053bfb0 -- ch2 SENDCOMM. CommCostume is the sender's Costume by value (four
// parts, eight accessories, the count -- 13 of the 23 dwords), TextIds the text-id array.
// hud_comm.cpp ClientSendComm.
void IClientImpl::SendServerSendComm(unsigned int ClientId, unsigned int SenderId, unsigned int ReceiverId,
                                     unsigned int VoiceId, Costume CommCostume, unsigned int CaptionIds,
                                     const unsigned int* TextIds, int TextCount, unsigned int PermutationIds,
                                     float Duration, bool bReceiverIsOwnShip)
{
    ClientSendComm(ClientId, SenderId, ReceiverId, VoiceId, CommCostume, CaptionIds, TextIds, TextCount,
                   PermutationIds, Duration, bReceiverIsOwnShip);
}

// LAB_0053c2a0 -- ch2 SENDNNMESSAGE: the NeuroNet voice queue.
void IClientImpl::SendServerSendNNMessage(unsigned int /*ClientId*/, unsigned int MessageId)
{
    NnVoiceEnqueue(MessageId, 0);
}

// LAB_0053de70: a costume hint (Kind 1) preloads its costume (FUN_004cc900). Kind is read with
// no null test, although the receivers pass NULL for any wire Kind but 1 -- kept as retail has
// it.
void IClientImpl::SendServerLoadHint(const unsigned int& /*ClientId*/, BaseHint* Hint)
{
    if (Hint->Kind == 1)
    {
        Costume Copy;
        memcpy(&Copy, &static_cast<CostumeHint*>(Hint)->Cost, sizeof(Copy));
        HudCommPreloadCostume(&Copy);
    }
}

// FUN_0053da90 -- ch2 SETGROUPFEELINGS: the relations copied, the name into a TString<16>
// (clamped to 15), Reputation::set.
void IClientImpl::SendServerSetGroupFeelings(unsigned int /*ClientId*/, const FServerSetGroupFeelingsInfo& Info)
{
    // Untested; a zero count still allocates.
    Reputation::Relation* const Copy = static_cast<Reputation::Relation*>(
        ::operator new(static_cast<unsigned int>(Info.RelationCount) * sizeof(Reputation::Relation)));
    for (unsigned int Index = 0; Index < static_cast<unsigned int>(Info.RelationCount); ++Index)
    {
        Copy[Index] = Info.Relations[Index];
    }
    TString<16> Name;
    Name = Info.Name;
    Reputation::set(Name, Copy, Info.RelationCount);
    ::operator delete(Copy);
}

// LAB_0053bf80 -- ch2 REQUEST_RETURNED: the ship's pending-request answer (FUN_00546d00). The
// gameobj is not class-tested before the ship method runs on it.
void IClientImpl::SendServerRequestReturned(const unsigned int& /*ClientId*/, unsigned int ShipId, int RequestType,
                                            int Response, int Extra)
{
    FClientGameobj* const Go = ClientObjDb_FindObject(ShipId, 0);
    if (Go == nullptr)
    {
        return;
    }
    static_cast<FClientShipGameobj*>(Go)->RequestReturned(RequestType, Response, Extra);
}

// FUN_0053deb0 -- ch2 REMOTEOBJECTCARGOUPDATE: a CShip syncs its cargo. Flag is not read.
void IClientImpl::SendServerRemoteObjectCargoUpdate(const unsigned int& /*ClientId*/, unsigned int ObjectId,
                                                    const EquipDescVector& Cargo, unsigned char /*Flag*/)
{
    FClientGameobj* const Go = ClientObjDb_FindObject(ObjectId, 0);
    if (Go == nullptr)
    {
        return;
    }
    CEqObj* const Eq =
        static_cast<CEqObj*>(NarrowObject(const_cast<CObject*>(Go->cobject()), 0x503));
    if (Eq == nullptr)
    {
        return;
    }
    EquipDescList const CargoList(Cargo);
    Eq->sync_cargo(CargoList);
}

// FUN_0053df80 -- ch2 OBJECTCARGOUPDATE. Outside single player the gameobj's cargo is updated
// or added (no null test on the narrowed CEqObj). Then, for the player's ship, the player's own
// item: FUN_004c4590 while Count > 0, else FUN_004c4320; no such item: FUN_004c4170.
void IClientImpl::SendServerObjectCargoUpdate(const unsigned int& /*ClientId*/, unsigned int ObjectId,
                                              const EquipDesc& Item)
{
    EquipDesc const* const Desc = &Item;
    if (!SinglePlayer())
    {
        FClientGameobj* const Go = ClientObjDb_FindObject(ObjectId, 0);
        if (Go != nullptr)
        {
            CEqObj* const Eq = static_cast<CEqObj*>(NarrowGameobj(Go, CObject::ClassEqObj));
            CEquip* const Equip = Eq->EquipManager.FindByID(Desc->Id);
            if (Equip == nullptr)
            {
                Eq->add_item(*Desc);
            }
            else
            {
                CECargo* const Cargo = CECargo::cast(Equip);
                if (Cargo != nullptr)
                {
                    Cargo->SetCount(static_cast<unsigned>(Desc->Count));
                    Cargo->SetHitPoints(Desc->Health * Cargo->Arch->HitPts);
                }
            }
        }
    }
    if (!IsPlayerShipId(ObjectId))
    {
        return;
    }
    if (GetPlayerEquipDescListRetail().find_equipment_item(Desc->Id) == nullptr)
    {
        AddPlayerEquipRetail(0, Desc);
        return;
    }
    if (Desc->Count > 0)
    {
        ModifyPlayerEquipRetail(Desc->Id, &Desc->get_hardpoint(), Desc->Count, Desc->Health, false);
        return;
    }
    RemovePlayerEquipRetail(0, Desc->Id, -1);
}

// FUN_0053e090 -- ch2 BURNFUSE: the gameobj is used as an EquippedObj with no class test. Lit:
// slot 112, else slot 113; both results are dropped.
void IClientImpl::SendServerBurnFuse(const unsigned int& /*ClientId*/, const FServerBurnFuseInfo& Info)
{
    FClientGameobj* const Found = ClientObjDb_FindObject(Info.ObjectId, 0);
    if (Found == nullptr)
    {
        return;
    }
    FClientEquippedObj* const Equipped = static_cast<FClientEquippedObj*>(Found);
    ID_String FuseId;
    FuseId.Id = Info.FuseId;
    if (Info.bLit != 0)
    {
        Equipped->LightFuse(Info.SourceId, &FuseId, Info.SubObjId, Info.AtT, Info.Lifetime);
        return;
    }
    Equipped->UnLightFuse(&FuseId, Info.SubObjId, Info.AtT);
}

// LAB_0053e100 -- ch2 SET_CAMERA: the static camera (FUN_0054bb50). Fov and Aspect are not
// read.
void IClientImpl::SendServerSetCamera(const unsigned int& /*ClientId*/, const FPayloadServerSetCamera& Info)
{
    FMatrix Orientation;
    IClientSMath()->QuaternionToMatrix(Orientation, reinterpret_cast<FQuaternion const&>(Info.Orient));
    float Csys[12];
    memcpy(Csys, &Orientation, 9 * sizeof(float));
    Csys[9] = Info.Position.x;
    Csys[10] = Info.Position.y;
    Csys[11] = Info.Position.z;
    SetStaticCameraTransform(Csys);
}

// LAB_0053e170 -- ch2 REVERT_CAMERA.
void IClientImpl::SendServerRevertCamera(const unsigned int& /*ClientId*/)
{
    RequestSpaceCameraMode("player");
}

// ---------------------------------------------------------------------------------------------
// ch1 common traffic (slots 91..100)
// ---------------------------------------------------------------------------------------------

// LAB_0053c310 -- SET_WEAPON_GROUP (DAT_00674c0c, FUN_004f5e50).
void IClientImpl::SendCommonSetWeaponGroup(unsigned int /*ClientId*/, const unsigned char* Data, unsigned int Size)
{
    G_WeaponGroups.ReadFromServer(reinterpret_cast<char const*>(Data), static_cast<int>(Size));
}

// FUN_0053c330 -- SET_VISITED_STATE, Data = {int count, then count records of 5 bytes
// {dword id, byte flags}}: FUN_004c4e00(id, flags, 0) for each. Size is not read.
void IClientImpl::SendCommonSetVisitedState(unsigned int /*ClientId*/, unsigned char* Data, unsigned int /*Size*/)
{
    unsigned char const* const Info = Data;
    int Count = 0;
    memcpy(&Count, Info, sizeof(Count));
    unsigned char const* Record = Info + 4;
    for (int Index = 0; Index < Count; ++Index)
    {
        unsigned Id = 0;
        memcpy(&Id, Record, sizeof(Id));
        SetPlayerVisitFlagsRetail(Id, Record[4], false);
        Record += 5;
    }
}

// FUN_0053c370 -- REQUEST_BEST_PATH, Data = {+4 count, +8 byte, +0xc records of 0x14
// {Vector, +0x0c, +0x10}}: DAT_006720a8 = the byte, FUN_004c46c0 (waypoints reset), then
// FUN_004c4780(&rec, rec+0x10, rec+0x0c) for each record but the last, which gets
// FUN_004c4ea0() in place of rec+0x0c. The count is re-read every pass. Size is not read.
void IClientImpl::SendCommonRequestBestPath(unsigned int /*ClientId*/, const unsigned char* Data,
                                            unsigned int /*Size*/)
{
    unsigned char const* const Info = Data;
    G_bNavMapNoBestPath = Info[8];
    ResetPlayerWaypointsRetail();
    unsigned char const* Record = Info + 0xc;
    for (int Index = 0;; ++Index)
    {
        int Count = 0;
        memcpy(&Count, Info + 4, sizeof(Count));
        if (Index >= Count)
        {
            break;
        }
        Vector Pos;
        memcpy(&Pos, Record, sizeof(Pos));
        // Content's FBestPathPoint: +0x0c ObjectId, +0x10 SystemId.
        unsigned ObjectId = 0;
        unsigned SystemId = 0;
        memcpy(&ObjectId, Record + 0xc, sizeof(ObjectId));
        memcpy(&SystemId, Record + 0x10, sizeof(SystemId));
        if (Index == Count - 1)
        {
            AddPlayerWaypointRetail(Pos, SystemId, GetPlayerWaypointTailTagRetail());
        }
        else
        {
            AddPlayerWaypointRetail(Pos, SystemId, ObjectId);
        }
        Record += 0x14;
    }
}

// LAB_0053c3d0 -- REQUEST_PLAYER_STATS: the block, its ship-kill table and its reputation
// table to FUN_004a8e40 (nn_player_info.cpp). ClientId and Size are not read.
void IClientImpl::SendCommonRequestPlayerStats(unsigned int /*ClientId*/, const unsigned char* Data,
                                               unsigned int /*Size*/)
{
    unsigned char const* const Stats = Data;
    unsigned ShipKillCount = 0;
    memcpy(&ShipKillCount, Stats + 0x20, sizeof(ShipKillCount));
    unsigned char const* const ShipKills = Stats + 0x34;
    NnPlayerInfoOnPlayerStats(Stats, ShipKills, ShipKills + ShipKillCount * 8);
}

// LAB_0053c3f0 -- POP_UP_DIALOG (FUN_0048a380).
void IClientImpl::SendCommonPopUpDialog(unsigned int /*ClientId*/, const FmtStr& Title, const FmtStr& Text,
                                        unsigned int Buttons)
{
    NnMissionMessagePopUp(&Title, &Text, Buttons);
}

// LAB_0053c410 -- REQUEST_GROUP_POSITIONS (FUN_0048d8a0, which never reads Size).
void IClientImpl::SendCommonRequestGroupPositions(unsigned int /*ClientId*/, unsigned char* Data,
                                                  unsigned int /*Size*/)
{
    NavMapSetGroupPositions(Data);
}

// LAB_0053c430 -- SET_MISSION_LOG: the story star's history replaced (FUN_004ba430).
void IClientImpl::SendCommonSetMissionLog(unsigned int /*ClientId*/, unsigned char* Data, unsigned int Size)
{
    StoryStarLoadHistory(Data, static_cast<int>(Size));
}

// LAB_0053c450 -- SET_INTERFACE_STATE (FUN_004c8190, the first dword).
void IClientImpl::SendCommonSetInterfaceState(unsigned int /*ClientId*/, unsigned char* Data,
                                              unsigned int /*Size*/)
{
    AssignGameSessionFlags(*reinterpret_cast<unsigned const*>(Data));
}

// LAB_0053c460 -- REQUEST_RANK_LEVEL (FUN_004c55e0, the first dword).
void IClientImpl::SendCommonRequestRankLevel(unsigned int /*ClientId*/, unsigned char* Data,
                                             unsigned int /*Size*/)
{
    SetPlayerRankLevelRetail(*reinterpret_cast<int const*>(Data));
}

// FUN_0053db60 -- ch2 SETDIRECTIVE on the player's ship (no null test on the narrowed CEqObj or
// its behavior manager). Kinds 0..4 build the op and set it; 5 sets the idle directive with no
// op and zeroes the current behavior's throttle and cruise. The IDirectiveInfo is {1, 0, type}
// (type 0x14 delay, 7 dock, 0x11 follow, 2 goto, 6 idle), for cancel {0, 0, -1}.
void IClientImpl::SendServerSetDirective(const unsigned int& /*ClientId*/, const FPayloadServerSetDirective& Info)
{
    void* const PlayerObj = GetPlayerObjRW();
    if (PlayerObj == nullptr)
    {
        return;
    }
    FClientGameobj* const Player = static_cast<FClientGameobj*>(static_cast<IObjRW*>(PlayerObj));
    CEqObj* const PlayerEq = static_cast<CEqObj*>(NarrowGameobj(Player, CObject::ClassShip));
    IBehaviorManager* const Behavior = PlayerEq->get_behavior_interface();

    switch (Info.DirectiveKind)
    {
    case 0:
    {
        pub::AI::DirectiveCancelOp Op;
        Behavior->set_current_directive(MakeDirectiveInfo(0u, 0xffffffffu), &Op);
        break;
    }
    case 1:
    {
        pub::AI::DirectiveDelayOp Op;
        Op.DelaySeconds = Info.DelaySeconds;
        Behavior->set_current_directive(MakeDirectiveInfo(1u, 0x14u), &Op);
        break;
    }
    case 2:
    {
        pub::AI::DirectiveDockOp Op;
        Op.DockObjectId = Info.DockObjectId;
        Op.NextRingId = Info.DockSecondaryObjectId;
        Op.bHasDockPortIndex = Info.bDockApproachCleared;
        Op.DockPortIndex = Info.DockPortIndex;
        Op.TetherObjectId = static_cast<int32_t>(Info.ProxTargetId);
        Op.TetherNearRange = Info.ProxNearDist;
        Op.TetherFarRange = Info.ProxFarDist;
        Behavior->set_current_directive(MakeDirectiveInfo(1u, 7u), &Op);
        break;
    }
    case 3:
    {
        pub::AI::DirectiveFollowOp Op;
        Op.TargetId = Info.FollowTargetId;
        Op.FollowDistance = Info.FollowDistance;
        Op.FollowOffsetX = Info.FollowOffset.x;
        Op.FollowOffsetY = Info.FollowOffset.y;
        Op.FollowOffsetZ = Info.FollowOffset.z;
        Op.EngageRange = Info.FollowEngageRange;
        Behavior->set_current_directive(MakeDirectiveInfo(1u, 0x11u), &Op);
        break;
    }
    case 4:
    {
        pub::AI::DirectiveGotoOp Op;
        Op.GotoKind = Info.GotoMode;
        Op.Destination = Info.GotoDestination;
        Op.TargetId = Info.GotoTargetId;
        Op.SplineP1X = Info.SplineP1.Field_0x00;
        Op.SplineP1Y = Info.SplineP1.Field_0x04;
        Op.SplineP1Z = Info.SplineP1.Field_0x08;
        Op.SplineP2X = Info.SplineP2.Field_0x00;
        Op.SplineP2Y = Info.SplineP2.Field_0x04;
        Op.SplineP2Z = Info.SplineP2.Field_0x08;
        Op.SplineP0X = Info.SplineP0.Field_0x00;
        Op.SplineP0Y = Info.SplineP0.Field_0x04;
        Op.SplineP0Z = Info.SplineP0.Field_0x08;
        Op.SplineP3X = Info.SplineP3.Field_0x00;
        Op.SplineP3Y = Info.SplineP3.Field_0x04;
        Op.SplineP3Z = Info.SplineP3.Field_0x08;
        Op.ArrivalRange = Info.GotoRange;
        Op.MaxSpeed = Info.GotoSpeed;
        Op.bFlyThrough = Info.bGotoFlyThrough;
        Op.Field_59 = Info.Field_0x8d;
        Op.bGotoCruise = Info.bGotoCruise;
        Op.bGotoNoCruise = Info.bGotoNoCruise;
        Op.TetherObjectId = Info.ProxTargetId;
        Op.TetherNearRange = Info.ProxNearDist;
        Op.TetherFarRange = Info.ProxFarDist;
        Op.bDisableAvoidance = Info.bGotoDisableAvoidance;
        Behavior->set_current_directive(MakeDirectiveInfo(1u, 2u), &Op);
        break;
    }
    case 5:
    {
        Behavior->set_current_directive(MakeDirectiveInfo(1u, 6u), nullptr);
        Behavior->update_current_behavior_throttle(0.0f);
        Behavior->update_current_behavior_cruise(false);
        break;
    }
    default:
        break;
    }
}

// ---------------------------------------------------------------------------------------------
// Misc object updates (slots 109..124)
// ---------------------------------------------------------------------------------------------

// LAB_0053db40 -- ch2 SCANNOTIFY: with a player ship, FUN_0048a180.
void IClientImpl::SendServerScanNotify(unsigned int /*ClientId*/, unsigned int ScannerId, unsigned int GoodId)
{
    if (GetPlayerObjRW() == nullptr)
    {
        return;
    }
    NnMissionMessageShowScanNotice(ScannerId, GoodId);
}

// LAB_0053e2b0 -- MISCOBJUPDATE 0x0009: a CShip's multiplayer group id (+0x1b4; the server
// sends it from SystemPlayerView_SendGroupChange).
void IClientImpl::SendServerMiscObjUpdateObjectGroup(unsigned int /*ClientId*/, unsigned int ObjectId,
                                                     unsigned int GroupId)
{
    FClientGameobj* const Go = ClientObjDb_FindObject(ObjectId, 0);
    if (Go == nullptr)
    {
        return;
    }
    CShip* const Ship =
        static_cast<CShip*>(NarrowObject(const_cast<CObject*>(Go->cobject()), 0x503));
    if (Ship == nullptr)
    {
        return;
    }
    Ship->GroupId = GroupId;
}

// LAB_0053e370 -- MISCOBJUPDATE 0x0002, the multiplayer mission-offer answer: DAT_0067334c =
// MissionSetBy, DAT_00673348 = MissionId, then FUN_004557d0 on the offer's ScriptSequence
// (DAT_00673350) when there is one.
void IClientImpl::SendServerMiscObjUpdateMission(unsigned int /*ClientId*/, unsigned int MissionId,
                                                 unsigned int MissionSetBy)
{
    SetMissionOfferAnswerRetail(MissionId, MissionSetBy);
    FGfScriptSequence* const Sequence = GetMissionOfferSequenceRetail();
    if (Sequence != nullptr)
        Sequence->OnMissionOfferAnswered();
}

// LAB_0053e3a0: FUN_0046b0f0 with the string (MSVCP60's _Nullstr when empty). ClientId is not
// read.
void IClientImpl::SendServerMiscObjUpdateMotd(unsigned int /*ClientId*/, const std::wstring& Motd)
{
    ChatSetServerMotd(Motd.c_str());
}

// FUN_0053e2f0 -- MISCOBJUPDATE 0x0028: a CEqObj's vibe (+0x104) takes the affiliation; with
// no gameobj but the local character's id, the player's own vibe (FUN_004c4670).
void IClientImpl::SendServerMiscObjUpdateAffiliation(unsigned int /*ClientId*/, unsigned int ObjectId,
                                                     unsigned int Affiliation)
{
    FClientGameobj* const Go = ClientObjDb_FindObject(ObjectId, 0);
    if (Go != nullptr)
    {
        CEqObj* const Eq =
            static_cast<CEqObj*>(NarrowObject(const_cast<CObject*>(Go->cobject()), 0x103));
        if (Eq == nullptr)
        {
            return;
        }
        int const Vibe = static_cast<int>(Eq->VibeId);
        Reputation::Vibe::SetAffiliation(Vibe, Affiliation, true);
        return;
    }
    if (GetPlayerCharacterId() != ObjectId)
    {
        return;
    }
    int const PlayerVibe = GetPlayerRepVibeRetail(); // FUN_004c4670 -> DAT_0061226c
    Reputation::Vibe::SetAffiliation(PlayerVibe, Affiliation, true);
}

// FUN_0053e200 -- MISCOBJUPDATE 0x0044.
void IClientImpl::SendServerMiscObjUpdateRank(unsigned int ClientId, unsigned int PlayerId, int Rank)
{
    ChatSetPlayerRank(PlayerId, static_cast<unsigned>(Rank)); // FUN_0046add0
    if (ClientId == PlayerId)
    {
        int const PlayerVibe = GetPlayerRepVibeRetail(); // FUN_004c4670
        Reputation::Vibe::SetRank(PlayerVibe, static_cast<unsigned>(Rank));
    }
    if (SinglePlayer())
    {
        StoryStarSetRankUp(Rank); // FUN_004b9b20
    }
}

// LAB_0053e1e0 -- MISCOBJUPDATE 0x0005.
void IClientImpl::SendServerMiscObjUpdatePlayerGroup(unsigned int /*ClientId*/, unsigned int PlayerId,
                                                     unsigned int GroupId)
{
    ChatSetPlayerGroupId(PlayerId, GroupId); // FUN_0046ad60
}

// LAB_0053e250 -- MISCOBJUPDATE 0x0084.
void IClientImpl::SendServerMiscObjUpdateSystem(unsigned int /*ClientId*/, unsigned int PlayerId,
                                                unsigned int SystemId)
{
    ChatSetPlayerSystemId(PlayerId, SystemId); // FUN_0046ae40
}

// LAB_0053e270: outside single player, the formation member queue is cleared (FUN_004c5680)
// and refilled (FUN_004c56b0). ClientId is not read.
void IClientImpl::SendServerMiscObjUpdateFormation(unsigned int /*ClientId*/,
                                                   const std::vector<unsigned int>& MemberIds)
{
    if (SinglePlayer())
    {
        return;
    }
    RetailU32QueueClear();
    for (unsigned int Index = 0; Index < MemberIds.size(); ++Index)
    {
        RetailU32QueuePushBack(MemberIds[Index]);
    }
}

// LAB_0053e1a0 -- ch2 FORMATION_UPDATE on the player's ship; the leader gameobj is not tested.
void IClientImpl::SendServerFormationUpdate(unsigned int /*ClientId*/, unsigned int LeaderId, const Vector& Offset)
{
    void* const PlayerObj = GetPlayerObjRW();
    if (PlayerObj == nullptr)
    {
        return;
    }
    IObjRW* const Player = static_cast<IObjRW*>(PlayerObj);
    IObjRW* const Leader = ClientObjDb_FindObject(LeaderId, 0);
    Player->set_formation_leader(Leader);
    Player->set_formation_offset(Offset);
}

// LAB_0053e3c0 -- SET_MUSIC (FUN_00553340): the last two arguments swap order.
void IClientImpl::SendServerSetMusic(unsigned int /*ClientId*/, unsigned int SpaceMusic, unsigned int DangerMusic,
                                     unsigned int BattleMusic, unsigned int OverrideMusic, bool bFlag, float Fade)
{
    StarSysSetMusicOverride(SpaceMusic, DangerMusic, BattleMusic, OverrideMusic, Fade, bFlag);
}

// LAB_0053e3f0 -- FUN_00553380.
void IClientImpl::SendServerCancelMusic(unsigned int /*ClientId*/)
{
    StarSysClearMusicOverride();
}

// LAB_0053e400 -- ch2 PLAY_SOUND_EFFECT.
void IClientImpl::SendServerPlaySoundEffect(unsigned int /*ClientId*/, unsigned int SoundId)
{
    ID_String Sound;
    Sound.Id = SoundId;
    PlayClientSoundArch(LookupClientSoundArch(&Sound), nullptr, nullptr, 0);
}
