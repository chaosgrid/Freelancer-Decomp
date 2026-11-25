// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Space/player_spawn.h"
#include "Space/client_objdb.h"

#include "Interface/Pages/character_select_equip.h"
#include "Common/client_cliobj.h"   // Archetype::LoadMountedEquipmentCompanions / HookUpInstanceTreeMaterials
#include "Space/Effects/client_fuse.h"
#include "Space/client_gameobj.h"  // FClientShipGameobj (FUN_00543380)
#include "Space/HUD/hud_waypoint.h"    // HudWaypointOnPlayerWaypointsCleared (FUN_004f4cd0)
#include "Interface/loading_screen.h"  // LoadingScreenStart (FUN_00579290)
#include "Common/engine_light.h"
#include "common_archetype.h"
#include "common_cobject.h"
#include "common_goods.h"    // BaseWatcher, EquipDescVector
#include "common_id.h"
#include "common_ini.h"
#include "common_reputation.h"
#include "common_server.h"   // IServer (DAT_0067ecd0)
#include "common_misc.h"     // SinglePlayer
#include "common_universe.h"
#include "Space/Effects/gate_tunnel.h"     // GateTunnelIniLoad (FUN_004fe1c0)
#include "Network/login_mgr.h"       // G_Server (DAT_0067ecd0)
#include "Core/globals.h"         // g_display.gate_tunnels (DAT_0067a22c)
#include "Interface/menu_keymap_config.h"
#include "Common/Audio/menu_sound_arch.h"  // PreloadClientSoundArchesByCategory (FUN_0042acb0)
#include "NeuroNet/nn_modal.h"         // NnSystemInit (FUN_0045a100)
#include "Network/server_connect.h"
#include "Space/Cameras/space_camera.h"
#include "Space/space_scene_load.h"
#include "Space/star_sys.h"

#include <cstddef>
#include <list>
#include <new>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

namespace
{
    // Retail globals touched by FUN_0054b520 / FUN_0054ab40.
    // DAT_0067337c (ship archetype) lives in character_select_equip.cpp.
    static unsigned G_PlayerCharacterId = 0;       // DAT_00673378
    static unsigned G_PlayerConnectionId = 0;        // DAT_00673344
    static unsigned G_PlayerShipCharMirror = 0;    // DAT_00679774
    // DAT_00679744 -- the BaseWatcher on the player's Ship gameobj (its Watchable at IObjRW+8).
    // FUN_0054b520 points it at the gameobj it builds; FUN_0054baf0 and its inlined copies read
    // the watched pointer and subtract eight.
    static BaseWatcher G_PlayerShipWatcher;
    // DAT_00678b96 (see player_spawn.h). A byte, because the wait takes it by address.
    static unsigned char G_bPlayerLaunchAcknowledged = 0;

    // DAT_00672978 (99 records of 0x18) and DAT_00673374 (the count).
    static const int kMaxPlayerWaypoints = 0x63;
    static FPlayerWaypointRecord G_PlayerWaypoints[kMaxPlayerWaypoints] = {};
    static int G_PlayerWaypointCount = 0;
    // DAT_00673390 -- read by FUN_004c4ea0, written only by FUN_004c4eb0.
    static unsigned G_PlayerWaypointTailTag = 0;

    // DAT_00673348 / DAT_0067334c / DAT_00673350 -- the multiplayer group mission-offer answer
    // (player_spawn.h).
    static unsigned G_MissionOfferAnswered = 0;              // DAT_00673348
    static unsigned G_MissionOfferAcceptorId = 0;            // DAT_0067334c
    static FGfScriptSequence* G_MissionOfferSequence = nullptr; // DAT_00673350
}

unsigned GetPlayerShipArchetypeId()
{
    return GetPlayerShipArchetypeIdRetail();
}

int GetPlayerWaypointCountRetail()
{
    // FUN_004c4690
    return G_PlayerWaypointCount;
}

FPlayerWaypointRecord* GetPlayerWaypointRetail(int Index)
{
    // FUN_004c46a0 (signed compares).
    if (Index < 0 || Index >= G_PlayerWaypointCount)
        return nullptr;
    return &G_PlayerWaypoints[Index];
}

void ResetPlayerWaypointsRetail()
{
    // FUN_004c46c0: only a positive count is cleared and notified (FUN_004f4cd0).
    if (G_PlayerWaypointCount > 0)
    {
        G_PlayerWaypointCount = 0;
        HudWaypointOnPlayerWaypointsCleared();
    }
}

void RemovePlayerWaypointRetail(int Index)
{
    // FUN_004c46e0
    if (Index < 0 || Index >= G_PlayerWaypointCount)
        return;
    G_PlayerWaypointCount = G_PlayerWaypointCount - 1;
    for (int Slot = Index; Slot < G_PlayerWaypointCount; ++Slot)
        G_PlayerWaypoints[Slot] = G_PlayerWaypoints[Slot + 1];
}

int GetPlayerWaypointMaxIdRetail()
{
    // FUN_004c4750 (signed compares).
    int MaxId = 0;
    for (int Slot = 0; Slot < G_PlayerWaypointCount; ++Slot)
    {
        if (G_PlayerWaypoints[Slot].AutoId > MaxId)
            MaxId = G_PlayerWaypoints[Slot].AutoId;
    }
    return MaxId;
}

unsigned GetPlayerWaypointTailTagRetail()
{
    // FUN_004c4ea0
    return G_PlayerWaypointTailTag;
}

void RequestBestPathRetail(Vector const& From, Vector const& To, unsigned Tag, unsigned ToSystemId)
{
    // FUN_004c4eb0: the 0x34-byte request through IServer slot 72. Port deviation: the
    // untouched bytes (+0x09..+0x0b) are stack garbage in retail; the port zeroes them.
    // Server's RequestBestPath forwards +0x00, +0x1c, +0x0c, +0x20 and +0x30 as Content's
    // FBestPathRequest {+0x04 cookie, FromSystemId, FromPosition, ToPosition, ToSystemId}; the
    // cookie comes back in the reply header +0x00. +0x04 is not forwarded.
    G_PlayerWaypointTailTag = Tag;
    unsigned char Request[0x34];
    memset(Request, 0, sizeof(Request));
    unsigned const ClientCookie = static_cast<unsigned>(GetPlayerRepVibeRetail()); // DAT_0061226c
    unsigned const Field04 = 2;
    unsigned const FromSystemId = GetPlayerStarSystemIdRetail(); // DAT_00673354
    memcpy(Request + 0x00, &ClientCookie, 4);
    memcpy(Request + 0x04, &Field04, 4);
    memcpy(Request + 0x0c, &From, 0xc);
    memcpy(Request + 0x1c, &FromSystemId, 4);
    memcpy(Request + 0x20, &To, 0xc);
    memcpy(Request + 0x30, &ToSystemId, 4);
    G_Server->RequestBestPath(GetPlayerConnectionId(), Request, 0x34);
}

void ResetMissionOfferAnswerRetail()
{
    // FUN_004c55b0
    G_MissionOfferAnswered = 0;
    G_MissionOfferAcceptorId = 0;
    G_MissionOfferSequence = nullptr;
}

void SetMissionOfferAnswerRetail(unsigned Answered, unsigned AcceptorId)
{
    // IClient slot 114 (inline in retail).
    G_MissionOfferAcceptorId = AcceptorId;
    G_MissionOfferAnswered = Answered;
}

unsigned GetMissionOfferAnsweredRetail()
{
    return G_MissionOfferAnswered;
}

unsigned GetMissionOfferAcceptorIdRetail()
{
    return G_MissionOfferAcceptorId;
}

FGfScriptSequence* GetMissionOfferSequenceRetail()
{
    return G_MissionOfferSequence;
}

void SetMissionOfferSequenceRetail(FGfScriptSequence* Sequence)
{
    G_MissionOfferSequence = Sequence;
}

bool AddPlayerWaypointRetail(Vector const& Pos, unsigned SystemId, unsigned ObjectId)
{
    // FUN_004c4780: refused at 0x63 records or once the largest AutoId reaches 0x63.
    if (G_PlayerWaypointCount >= kMaxPlayerWaypoints)
        return false;
    int const MaxId = GetPlayerWaypointMaxIdRetail();
    if (MaxId >= kMaxPlayerWaypoints)
        return false;
    FPlayerWaypointRecord& Record = G_PlayerWaypoints[G_PlayerWaypointCount];
    Record.Pos = Pos;
    Record.SystemId = SystemId;
    Record.ObjectId = ObjectId;
    Record.AutoId = MaxId + 1;
    G_PlayerWaypointCount = G_PlayerWaypointCount + 1;
    return true;
}

// FUN_004c4610 -- the player ship archetype's HoldSize (+0x100); used with
// get_cargo_space_occupied (FUN_004c4640) to compute remaining cargo space.
float GetPlayerShipHoldSizeRetail()
{
    // 0.0 (DAT_005c7474) without a ship archetype id; GetShip is not null-tested.
    if (GetPlayerShipArchetypeIdRetail() == 0)
        return 0.0f;
    Archetype::Ship* const ShipArch = Archetype::GetShip(GetPlayerShipArchetypeIdRetail());
    return ShipArch->HoldSize;
}

unsigned GetPlayerCharacterId()
{
    return G_PlayerCharacterId;
}

namespace
{
    // DAT_00678b94 / DAT_00678b95 -- the server's answer to IServer::RequestCreateShip and "an
    // answer arrived". Written by IClient slot 61; FUN_0054ab40 clears both before the request
    // and waits on +0x95 through FUN_005a89b0.
    unsigned char G_PlayerCreateShipResult = 0;      // DAT_00678b94
    unsigned char G_bPlayerCreateShipAnswered = 0;   // DAT_00678b95
}

void SetPlayerCreateShipResponse(unsigned char Result, unsigned CharacterId)
{
    // LAB_0053d920 -- the only retail writer of DAT_00673378.
    G_bPlayerCreateShipAnswered = 1;
    G_PlayerCreateShipResult = Result;
    G_PlayerCharacterId = CharacterId;
}

bool IsPlayerCreateShipAnswered()
{
    return G_bPlayerCreateShipAnswered != 0;
}

unsigned char GetPlayerCreateShipResult()
{
    return G_PlayerCreateShipResult;
}

void ClearPlayerCreateShipResponse()
{
    G_bPlayerCreateShipAnswered = 0;
    G_PlayerCreateShipResult = 0;
}

unsigned char* GetPlayerCreateShipAnsweredByte()
{
    return &G_bPlayerCreateShipAnswered;
}

unsigned char* GetPlayerLaunchAcknowledgedByte()
{
    return &G_bPlayerLaunchAcknowledged;
}

bool IsPlayerShipId(unsigned Id)
{
    // FUN_00548eb0
    if ((Id & 0x3fffffffu) == 0)
    {
        return false;
    }
    return G_PlayerShipCharMirror == Id;
}

unsigned GetPlayerShipId()
{
    return G_PlayerShipCharMirror;
}

unsigned GetPlayerConnectionId()
{
    return G_PlayerConnectionId;
}

void SetPlayerConnectionId(unsigned Id)
{
    G_PlayerConnectionId = Id;
}

void* GetPlayerShipWatcher()
{
    return const_cast<Watchable*>(G_PlayerShipWatcher.target());
}

void ClearPlayerShipWatcher()
{
    // Inline in the SpaceApp ctor.
    if (G_PlayerShipWatcher.target() != nullptr)
        G_PlayerShipWatcher.set(nullptr);
}

void* GetPlayerShipObject()
{
    // NOT RETAIL (port accessor): the CShip behind the watched gameobj, narrowed the way
    // FUN_0054b520 narrows it.
    Watchable const* const Target = G_PlayerShipWatcher.target();
    if (Target == nullptr)
        return nullptr;
    FClientGameobj* const Go =
        static_cast<FClientGameobj*>(static_cast<IObjRW*>(const_cast<Watchable*>(Target)));
    CObject* const Obj = Go->Object;
    if (Obj == nullptr ||
        (static_cast<unsigned>(Obj->ObjectClass) & static_cast<unsigned>(CObject::ClassShip)) !=
            static_cast<unsigned>(CObject::ClassShip))
        return nullptr;
    return static_cast<CShip*>(Obj);
}

void GetPlayerShipWorldPosition3(float OutPos3[3])
{
    // NOT RETAIL (port accessor for the HUD scanner): the player CShip's world position.
    OutPos3[0] = 0.f;
    OutPos3[1] = 0.f;
    OutPos3[2] = 0.f;

    CShip* const Ship = static_cast<CShip*>(GetPlayerShipObject());
    if (Ship == nullptr)
        return;

    OutPos3[0] = Ship->WorldPosition.x;
    OutPos3[1] = Ship->WorldPosition.y;
    OutPos3[2] = Ship->WorldPosition.z;
}

void SetPlayerLaunchAcknowledged(bool bAcknowledged)
{
    G_bPlayerLaunchAcknowledged = bAcknowledged ? 1 : 0;
}

bool IsPlayerLaunchAcknowledged()
{
    return G_bPlayerLaunchAcknowledged != 0;
}

FClientShipGameobj* SpawnPlayerShip()
{
    // FUN_0054b520 (the one caller drops the result).
    FClientShipGameobj* const Go = new FClientShipGameobj();

    // Retail tests neither the archetype nor the gameobj.
    Archetype::Ship* const ShipArch = Archetype::GetShip(GetPlayerShipArchetypeIdRetail());
    ShipArch->load();
    Go->create(ShipArch);
    ShipArch->Release();

    CObject* const Obj = Go->Object;
    CShip* const Ship =
        (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) &
                            static_cast<unsigned>(CObject::ClassShip)) ==
                               static_cast<unsigned>(CObject::ClassShip))
            ? static_cast<CShip*>(Obj)
            : nullptr;

    // FUN_0052e0a0 on the CSimple head, FUN_004cdc10 on the costume; value-initialisation
    // covers the rest of retail's stores.
    CShip::CreateParms Parms{};
    InitCreateParmsScratchRetail(&Parms);
    Parms.DockingBaseId = 0;
    Parms.VibeId = 0xffffffffu;

    Parms.Id = G_PlayerCharacterId;
    Parms.ObjDb = GetClientObjDb();
    Parms.OwnerPlayer = G_PlayerConnectionId;

    // FUN_004c4670: the player's reputation vibe (DAT_0061226c).
    Parms.VibeId = static_cast<unsigned>(GetPlayerRepVibeRetail());

    // The costume from FUN_004c3de0 (DAT_006728e4).
    unsigned const* const ComParts = GetPlayerComCostumePartsRetail();
    Parms.SpaceCostume.PartIds[0] = ComParts[0];
    Parms.SpaceCostume.PartIds[1] = ComParts[1];
    Parms.SpaceCostume.PartIds[2] = ComParts[2];
    Parms.SpaceCostume.PartIds[3] = ComParts[3];
    Parms.SpaceCostume.AccessoryCount = GetPlayerComCostumeAccessoryCountRetail();
    unsigned const* const ComAccessories = GetPlayerComCostumeAccessoriesRetail();
    for (int Index = 0; Index < (int)Parms.SpaceCostume.AccessoryCount; ++Index)
        Parms.SpaceCostume.AccessoryIds[Index] = ComAccessories[Index];

    Parms.VoiceId = GetPlayerVoiceIdRetail(); // FUN_004c4650 (DAT_00673360)
    // FUN_004c4f70 (DAT_00673380) is a health fraction.
    Parms.HitPts = GetPlayerHealthScaleRetail() * ShipArch->HitPts;

    Parms.Equipment = EquipDescVector(GetPlayerEquipDescListRetail());
    Parms.ArchGroupDescs = GetPlayerCollisionGroupListRetail();

    // GetRank's return value is ignored (it writes the out value on every path).
    int const VibeId = GetPlayerRepVibeRetail();
    unsigned Rank = 0;
    Reputation::Vibe::GetRank(VibeId, Rank);
    Parms.Rank = Rank;
    // Not reproduced: retail's wcscpy of the character's name (DAT_006732f0) into +0xb8.
    // Nothing on this path reads +0xb8..+0xe7 (only CSolar::init does), and the port's
    // CreateParms has named CSolar fields there instead.

    Ship->init(Parms);

    Ship->get_archetype()->Companion->Load(1);

    Archetype::LoadMountedEquipmentCompanions(&Parms.Equipment, 1);
    Archetype::HookUpInstanceTreeMaterials(Go->get_index());

    Go->PostCreateInit();

    // ObjDB bucket + remote-physics block.
    ClientObjDb_RegisterGameobj(Go, true);

    if (!SinglePlayer())
        Ship->enable_collisions_r(true);

    G_PlayerShipCharMirror = G_PlayerCharacterId;

    G_PlayerShipWatcher.set(static_cast<Watchable const*>(static_cast<IObjRW*>(Go)));

    // IServer slot 48; no null test on the server.
    G_bPlayerLaunchAcknowledged = false;
    static_cast<IServer*>(GetLocalServerInterface())
        ->PlayerLaunch(G_PlayerCharacterId, G_PlayerConnectionId);

    return Go;
}
