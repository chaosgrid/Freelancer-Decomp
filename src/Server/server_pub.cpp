// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "../include/common_server.h"
#include "../include/common_pathfind.h" // PathfindManager (pub::AI::SubmitState)
#include "../include/common_id.h"
#include "../include/common_goods.h"
#include "../include/common_universe.h"
#include "../include/common_archetype.h"
#include "../include/common_reputation.h"
#include "../include/common_iface.h"
#include "../include/common_cobjdb.h"
#include "../include/common_csys.h"
#include "../include/common_cobject.h"
#include "../include/common_server_playerdb.h"
#include "../include/server_exports.h"
#include "../include/server_basedb.h"
#include "../include/server_cobject.h"
#include "../include/common_physys.h"
#include "../include/server_gameobject.h"
#include "../include/server_sysdb.h"
#include "../include/common_misc.h"
#include "../include/common_net.h"      // IsMPServer (AdjustCash's gate)
#include "../include/common_newchar.h"   // RtcSlider::get_slider (pub::GF::GetRtcPerformanceSlider)
#include "../include/x86math.h"      // I3DMathEngine::MatrixToQuaternion (pub::Player::SetCamera)
#include "../include/server_createinfo.h" // Server_Acquire3DMathEngine (FUN_06d5ff70)
#include "../include/rpc_iclient.h"
#include "../include/rpc_iclient_args.h" // FPayloadServerSetDirective (pub::AI::SubmitDirective)
#include "../include/server_savefile.h"   // SaveGame_Writer / ISave / Server_FLS1Transform
#include "../include/server_savegame.h"   // pub::Save -> PlayerData::auto_save / PlayerData::mission_save
#include "../include/common_casteroid.h"
#include "server_internal.h"
#include <windows.h>
#include <cstdint>
#include <new>
#include <cmath>
#include <cstring>     // std::memcpy (SpaceObj::Create EquipIdMaker seed)
#include <map>
#include <stdexcept>   // std::out_of_range (Server_VibeID_Free, FUN_06d15f90)
#include <vector>
#include <list>
#include <cwchar>      // wcslen / wcstombs / swprintf

namespace GlobalReputation = ::Reputation;

// The IClient the server talks back to (DAT_06d8fb14). Defined in IServerImpl.cpp and set
// by IServerImpl::Startup; several pub:: exports dispatch straight into its vtable.
extern IClient* g_pClientInterface;

// The guard every pub::Player export opens with (e.g. SetMsnID .text:06d56461):
// id != 0, id-1 < Players.MaxPlayers (DAT_06d90264), and a non-zero client id at
// PlayerData+0x348 (stride 0x418). Retail has no "active" flag; occupancy is the client id.
static bool IsValidPlayer(unsigned int player_id) {
    if (player_id == 0 || (player_id - 1) >= PlayerDB_GetMaxPlayers()) return false;
    return Players[player_id].client_id != 0;
}

// Forward declarations for functions defined in server_dlls.cpp
unsigned int Server_ControllerCreate(const char* dll_path, const char* instance_name, void* parms, unsigned int priority);
void Server_ControllerDestroy(unsigned int flid);

// Game time accumulator defined in IServerImpl.cpp (updated by ElapseTime)
extern double g_ElapsedTimeAccum;   // DAT_06d8fb28, IServerImpl.cpp
extern double GServerTimeBase;      // DAT_06d8fb20, IServerImpl.cpp
extern char GDllPath[0x104];        // DAT_06d8f654, server_dlls.cpp (pub::DLL_LoadLibrary)

int GReportedFreeTerminal = 0;
namespace pub {

// pub::ReportFreeTerminal -- retail .text:06cffd10 (RVA 0x1fd10, ordinal 363). The first
// argument is never read.
SERVER_API int __cdecl ReportFreeTerminal(unsigned int P1, int P2) {
    GReportedFreeTerminal = P2;
    return 0;
}
} // namespace pub -- the save-file family below is not in pub::

// Server_SaveSinglePlayerState -- retail FUN_06d447c0, cdecl (ISave*). The writer half of
// Server_LoadSinglePlayerState (FUN_06d44810, IServerImpl.cpp); only called by the
// serializer below, in single player. The clock sum is narrowed to float.
void Server_SaveSinglePlayerState(ISave* Writer) {
    Writer->WriteSection("Time");
    Writer->WriteFloat("seconds", static_cast<float>(g_ElapsedTimeAccum + GServerTimeBase));
    ::Reputation::Save(Writer);
    Server_ControllerDispatch(0, pub::Controller::EMessage::SaveGame, Writer->GetFile());
}


// ===========================================================================
// pub::Phantom exports and the Server-side phantom record.
//
// Retail allocates one 0x28-byte record per phantom (operator new(0x28) at .text:06cfbea5
// / 06cfc07d) whose vtable .rdata:06d66b8c has exactly two slots, LAB_06cffed0 and
// LAB_06cfff30: it is a PhySys::CObjectCB, handed to PhySys::CreatePhantom as the
// PhantomParms+0x40 listener. The pub::Phantom handle IS that record. Layout:
//   +0x00 vtable          +0x04 std::list<unsigned> of ids that entered (alloc byte,
//   +0x08 head, +0x0c size)  +0x10 the same for ids that left (+0x14 head, +0x18 size)
//   +0x1c TypeMask (tested against CSimple::TypeBits)  +0x20 the PhySys phantom handle
//   +0x24 the system id.
// Every live record sits on the global std::list at .data:DAT_06d8d8f8 (size
// DAT_06d8d8fc), built by the static initializer LAB_06cffd20 and torn down by the atexit
// FUN_06cffd60 -- here a plain static std::list. .data:DAT_06d8d928 is set while
// FUN_06cffdc0 (Server_PumpPhantomEvents) dispatches; every pub::Phantom export returns
// -5 while it is set.
// ===========================================================================
namespace {

class ServerPhantom : public PhySys::CObjectCB
{
public:
    // LAB_06cffed0 (vtable +0x00, RET 4): a CSimple (class & 3) whose TypeBits (+0xe0)
    // share a bit with TypeMask has its id (+0xb0) appended to Entered.
    void added(CObject* Obj) override;
    // LAB_06cfff30 (vtable +0x04): the same test, onto Left.
    void deleted(CObject* Obj) override;

    std::list<unsigned int> Entered;   // +0x04 .. +0x0f
    std::list<unsigned int> Left;      // +0x10 .. +0x1b
    unsigned int TypeMask = 0;         // +0x1c
    void* PhyHandle = nullptr;         // +0x20
    unsigned int SystemId = 0;         // +0x24
    // FUN_06cfbd60 is the destructor (Left, then Entered: member destruction order).
};

void ServerPhantom::added(CObject* Obj)
{
    if (Obj == nullptr) return;
    if ((static_cast<unsigned int>(Obj->ObjectClass) & 3u) != 3u) return;
    if ((static_cast<CSimple*>(Obj)->TypeBits & TypeMask) == 0) return;
    Entered.push_back(static_cast<CSimple*>(Obj)->Id);
}

void ServerPhantom::deleted(CObject* Obj)
{
    if (Obj == nullptr) return;
    if ((static_cast<unsigned int>(Obj->ObjectClass) & 3u) != 3u) return;
    if ((static_cast<CSimple*>(Obj)->TypeBits & TypeMask) == 0) return;
    Left.push_back(static_cast<CSimple*>(Obj)->Id);
}

std::list<ServerPhantom*> GPhantoms;   // .data:DAT_06d8d8f8 / DAT_06d8d8fc
bool GbPhantomPumpBusy = false;        // .data:DAT_06d8d928

// The linear search every export opens with (e.g. .text:06cfbce1).
std::list<ServerPhantom*>::iterator FindPhantom(void* Handle)
{
    std::list<ServerPhantom*>::iterator It = GPhantoms.begin();
    while (It != GPhantoms.end() && *It != Handle)
        ++It;
    return It;
}

} // namespace

// FUN_06cffdc0, called once per server physics tick from FUN_06d001d0. With the busy
// latch set, dispatches controller events 0x2d (enter) / 0x2e (exit) with
// {id, phantom, SystemId} for every queued id, emptying both lists.
void Server_PumpPhantomEvents()
{
    GbPhantomPumpBusy = true;
    for (std::list<ServerPhantom*>::iterator It = GPhantoms.begin(); It != GPhantoms.end(); ++It) {
        ServerPhantom* const Ph = *It;
        for (std::list<unsigned int>::iterator Id = Ph->Entered.begin();
             Id != Ph->Entered.end(); ++Id) {
            unsigned int Event[3];
            Event[0] = *Id;
            Event[1] = static_cast<unsigned int>(reinterpret_cast<uintptr_t>(Ph));
            Event[2] = Ph->SystemId;
            Server_ControllerDispatch(0, pub::Controller::EMessage::PhantomEnter, Event);
        }
        Ph->Entered.clear();
        for (std::list<unsigned int>::iterator Id = Ph->Left.begin();
             Id != Ph->Left.end(); ++Id) {
            unsigned int Event[3];
            Event[0] = *Id;
            Event[1] = static_cast<unsigned int>(reinterpret_cast<uintptr_t>(Ph));
            Event[2] = Ph->SystemId;
            Server_ControllerDispatch(0, pub::Controller::EMessage::PhantomExit, Event);
        }
        Ph->Left.clear();
    }
    GbPhantomPumpBusy = false;
}

namespace pub {

// ===========================================================================
// pub::Player exports. Retail pattern: the IsValidPlayer guard, then field access on
// the PlayerData slot (stride 0x418).
// ===========================================================================
namespace Player {

// Player::MarkObj -- retail 0x74970. GameObject::mark_for_player (FUN_06cef3b0) records the
// mark, then IClient::SendServerMarkObj (+0x12c, FLPACKET_SERVER_MARKOBJ). The client
// interface (DAT_06d8fb14) is not null-tested.
SERVER_API int __cdecl MarkObj(unsigned int player_id, unsigned int obj_id, int mark_type) {
    if (!IsValidPlayer(player_id)) return -2;
    GameObject* obj = Server_LookupGameObject(obj_id);
    if (!obj) return -3;
    obj->mark_for_player(player_id, mark_type);
    ::g_pClientInterface->SendServerMarkObj(player_id, obj_id, mark_type);
    return 0;
}

// Player::CfgInterfaceNotification -- retail 0x74a10. IClient::
// SendServerCfgInterfaceNotification (+0x130) with a 12-byte
// FPayloadServerCfgInterfaceNotification; retail leaves the three pad bytes unwritten,
// and so does this. The client interface is not null-tested.
SERVER_API int __cdecl CfgInterfaceNotification(unsigned int player_id, unsigned int notif_type,
                                      bool enable, int param) {
    if (!IsValidPlayer(player_id)) return -2;
    FPayloadServerCfgInterfaceNotification Payload;
    Payload.NotifType = notif_type;
    Payload.Param = static_cast<unsigned int>(param);
    Payload.bEnable = enable ? 1u : 0u;
    ::g_pClientInterface->SendServerCfgInterfaceNotification(player_id, Payload);
    return 0;
}

// Player::GetSystem -- retail 0x74a80 (slot +0x3e0).
SERVER_API int __cdecl GetSystem(const unsigned int& player_id, unsigned int& out_system_id) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    out_system_id = p->system_id;
    return 0;
}

// Player::GetShip -- retail 0x74ad0 (slot +0x3e4).
SERVER_API int __cdecl GetShip(const unsigned int& player_id, unsigned int& out_ship_id) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    out_ship_id = p->ship_object_id;
    return 0;
}

// Player::GetShipID -- retail 0x74b20 (slot +0x264).
SERVER_API int __cdecl GetShipID(const unsigned int& player_id, unsigned int& out_ship_arch) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    out_ship_arch = p->ship_archetype;
    return 0;
}

// Player::GetRep -- retail 0x74b70 (slot +0x318); -1 on failure.
SERVER_API int __cdecl GetRep(const unsigned int& player_id, int& out_rep) {
    out_rep = -1;
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    out_rep = p->rep_vibe;
    return 0;
}

// Player::GetBase -- retail 0x74bd0 (slot +0x3ec).
SERVER_API int __cdecl GetBase(const unsigned int& player_id, unsigned int& out_base_id) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    out_base_id = p->current_base;
    return 0;
}

// Player::GetLocation -- retail 0x74c20 (slot +0x3f8).
SERVER_API int __cdecl GetLocation(const unsigned int& player_id, unsigned int& out_location_id) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    out_location_id = p->current_location;
    return 0;
}

// Player::GetCharacter -- retail .text:06d54c70 (slot +0x3fc).
SERVER_API int __cdecl GetCharacter(const unsigned int& player_id, unsigned int& out_char_id) {
    if (!IsValidPlayer(player_id)) return -2;
    out_char_id = Players[player_id].character_id;
    return 0;
}

// Player::GetRelativeHealth -- retail 0x74cc0 (slot +0x268); -1.0 on failure.
SERVER_API int __cdecl GetRelativeHealth(const unsigned int& player_id, float& out_health) {
    out_health = -1.0f;
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    out_health = p->relative_health;
    return 0;
}

// Player::EnumerateCargo -- retail 0x74d20, over slot->equipment (+0x278). Mounted items
// are skipped. Unlike SpaceObj::EnumerateCargo, Arch2Good's answer is not tested.
SERVER_API int __cdecl EnumerateCargo(const unsigned int& player_id, CargoEnumerator& enumerator) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;

    for (std::list<EquipDesc>::const_iterator It = p->equipment.Items.begin();
         It != p->equipment.Items.end(); ++It) {
        EquipDesc const& Item = *It;
        if (Item.bEquipped)
            continue;
        unsigned int const GoodId = Arch2Good(Item.ArchId);
        enumerator.notify_cargo(GoodId, Item.Count, Item.Id);
    }
    return 0;
}

// Player::GetRemainingHoldSize -- retail 0x74db0. Goes through pub::GetCargoHoldSize (an
// unsigned dword, zero-extended by FILD qword) rather than HoldSize directly, minus
// EquipDescList::get_cargo_space_occupied of slot->equipment (+0x278).
SERVER_API int __cdecl GetRemainingHoldSize(const unsigned int& player_id, float& out_remaining) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;

    unsigned int HoldSize = 0;
    pub::GetCargoHoldSize(p->ship_archetype, HoldSize);
    out_remaining = (float)HoldSize - p->equipment.get_cargo_space_occupied();
    return 0;
}

// Player::AddCargo -- retail 0x74e90. In space: StarSystem::add_cargo (FUN_06d0e8d0) on the
// ship's system. Docked (no ship object): PlayerData::add_equipment (FUN_06d4efc0) into the
// cargo bay.
// mission_flag: docked it is only FUN_06d4efc0's "do not combine with an existing stack"
// byte; in space it also becomes EquipDesc::bMission (direct byte store at .text:06d0eadd).
SERVER_API int __cdecl AddCargo(const unsigned int& player_id, const unsigned int& good_id,
                     unsigned int quantity, float health, bool mission_flag) {
    if (!IsValidPlayer(player_id)) return -2;
    // Retail's compares reject NaN as well, hence the negated tests.
    if (!(health >= 0.0f)) return -4;
    if (!(health <= 1.0f)) return -4;
    PlayerSlot* p = &Players[player_id];

    if (p->ship_object_id != 0) {
        // A failed lookup is the only -2. The system is used untested (null for an
        // in-transit ship, as in retail).
        GameObject* ShipObj = nullptr;
        StarSystem* ShipSys = nullptr;
        if (!Server_FindObject(p->ship_object_id, ShipObj, ShipSys))
            return -2;
        // Good2Arch only after the lookup succeeded.
        ShipSys->add_cargo(ShipObj, Good2Arch(good_id), quantity, health,
                           mission_flag);
        return 0;
    }

    unsigned int const ArchId = Good2Arch(good_id);
    p->add_equipment(ArchId, EquipDesc::CARGO_BAY_HP_NAME,
                     (int)quantity, health, /*bEquipped*/false,
                     mission_flag, /*bNotify*/true);
    return 0;
}

// Player::RemoveCargo -- retail 0x74fb0. In space: StarSystem::remove_cargo (FUN_06d0ebf0,
// three stack args) on the ship's system, used untested; a failed lookup is the only -2.
// Docked: the tail is PlayerData::remove_equipment (FUN_06d4f100) inlined, so the port
// calls the shared helper.
SERVER_API int __cdecl RemoveCargo(const unsigned int& player_id, unsigned short cargo_slot, unsigned int quantity) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = &Players[player_id];

    if (p->ship_object_id != 0) {
        GameObject* ShipObj = nullptr;
        StarSystem* ShipSys = nullptr;
        if (!Server_FindObject(p->ship_object_id, ShipObj, ShipSys))
            return -2;
        ShipSys->remove_cargo(ShipObj, cargo_slot, (int)quantity);
        return 0;
    }

    p->remove_equipment(cargo_slot, (int)quantity, /*bNotify*/true);
    return 0;
}

// Player::SetShipAndLoadout -- retail 0x750a0. set_ship_arch (FUN_06d4ec10), then a local
// list copy of the vector with every id reset to SUBOBJ_ID_NONE, so load_equipment
// (FUN_06d4eeb0) mints a fresh EquipIdMaker id for each item.
SERVER_API int __cdecl SetShipAndLoadout(const unsigned int& player_id, unsigned int ship_arch,
                                          const EquipDescVector& equip_desc_vector) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;

    p->set_ship_arch(ship_arch);

    EquipDescList Fresh;
    for (EquipDesc const* It = equip_desc_vector.Begin; It != equip_desc_vector.End; ++It) {
        EquipDesc Copy(*It);
        Copy.set_id(SUBOBJ_ID_NONE);
        Fresh.add_equipment_item(Copy, false);
    }
    p->load_equipment(Fresh);
    return 0;
}

// Player::GetRank -- retail 0x75220 (slot +0x284).
SERVER_API int __cdecl GetRank(const unsigned int& player_id, int& out_rank) {
    out_rank = 0;
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    out_rank = (int)p->rank_level;
    return 0;
}

// Player::GetMoneyNeededToNextRank -- retail 0x75280 (slot +0x288).
SERVER_API int __cdecl GetMoneyNeededToNextRank(const unsigned int& player_id, int& out_money) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    out_money = p->money_needed_to_next_rank;
    return 0;
}

// Player::GetAssetValue -- retail 0x752d0. Ship value is find_by_ship_arch (not
// find_by_archetype) price * 0.8f (.rdata:06d6ae78). Each item of slot->equipment (+0x278):
// combinable (GoodInfo+0x70) -> Count * price; otherwise ftol(Health * price) * Count *
// 0.3f (.rdata:06d6ae7c). FUN_06d4faf0 splits on the same flag with the same 0.3f.
SERVER_API int __cdecl GetAssetValue(const unsigned int& player_id, float& out_value) {
    out_value = 0.0f;
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;

    unsigned int const ship_arch = p->ship_archetype;
    if ((ship_arch & 0x3fffffffu) == 0) return -2;

    GoodInfoList* good_list = GoodList_get();
    if (!good_list) return -2;

    const GoodInfo* ship_good = good_list->find_by_ship_arch(ship_arch);
    if (!ship_good) return -2;

    out_value = 0.8f * ship_good->Price;

    for (std::list<EquipDesc>::const_iterator It = p->equipment.Items.begin();
         It != p->equipment.Items.end(); ++It) {
        EquipDesc const& Item = *It;
        if ((Item.ArchId & 0x3fffffffu) == 0) continue;
        const GoodInfo* item_good = GoodList::find_by_archetype(Item.ArchId);
        if (!item_good) continue;

        if (item_good->bCombinable) {
            out_value += (float)Item.Count * item_good->Price;
        } else {
            int const Unit = (int)(Item.Health * item_good->Price);
            out_value += (float)(Unit * Item.Count) * 0.3f;
        }
    }
    return 0;
}

// Player::AdjustCash (0x06d553c0) -- PlayerData::AdjustCash (0x06d4f4e0) inlined with its
// bool folded to true, so the running total at +0x320 always accumulates (even when the
// MP gate below skips the rest).
SERVER_API int __cdecl AdjustCash(const unsigned int& player_id, int amount) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    // The client id doubles as retail's "slot occupied" flag.
    if (p->client_id == 0) return -2;

    p->total_earned += amount;
    int new_cash = p->cash + amount;

    // +0x64 is the selected character id (common_server_playerdb.h): an MP server only
    // adjusts the cash of a session that has picked a character. Skips the clamp, the
    // store and both notifications, yet returns 0.
    if (IsMPServer() && !p->selected_character.is_valid()) return 0;

    if (new_cash < 0) new_cash = 0;
    else if (new_cash > 999999999) new_cash = 999999999;
    p->cash = new_cash;

    // IClient +0x134; the client interface is not null-tested.
    ::g_pClientInterface->SendServerSetCash(p->client_id, new_cash);
    // FUN_06d43650(0, &client_id): message 0 broadcast to every controller.
    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerWorthChanged, &p->client_id);
    return 0;
}

// Player::InspectCash (0x06d55480) -- reads +0x31c and clamps it on the way out without
// writing it back. Same slot-occupied gate as AdjustCash; out_cash is zeroed first.
SERVER_API int __cdecl InspectCash(const unsigned int& player_id, int& out_cash) {
    out_cash = 0;
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    if (p->client_id == 0) return -2;
    int cash = p->cash;
    if (cash < 0) cash = 0;
    if (cash > 999999999) cash = 999999999;
    out_cash = cash;
    return 0;
}

// Player::SetInitialPos -- retail 0x75500 (slot +0x350).
SERVER_API int __cdecl SetInitialPos(const unsigned int& player_id, const Vector& pos) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    p->initial_pos.x = pos.x;
    p->initial_pos.y = pos.y;
    p->initial_pos.z = pos.z;
    return 0;
}

// Player::SetInitialOrnt -- retail 0x75560 (slot +0x35c, nine dwords).
SERVER_API int __cdecl SetInitialOrnt(const unsigned int& player_id, const Matrix& orient) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    for (int i = 0; i < 9; i++) {
        (&p->initial_ornt.E[0][0])[i] = (&orient.E[0][0])[i];
    }
    return 0;
}

// Player::GetNumKills -- retail 0x755c0 (slot +0x334); -1 on failure.
SERVER_API int __cdecl GetNumKills(const unsigned int& player_id, int& out_kills) {
    if (!IsValidPlayer(player_id)) { out_kills = -1; return -2; }
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) { out_kills = -1; return -2; }
    out_kills = p->num_kills;
    return 0;
}

// Player::SetNumKills -- retail 0x06d55620. Store at +0x334, then IClient +0x138
// (FLPACKET_SERVER_SETNUMKILLS) with the client id passed by address.
SERVER_API int __cdecl SetNumKills(const unsigned int& player_id, int kills) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    p->num_kills = kills;
    ::g_pClientInterface->SendServerSetNumKills(p->client_id, kills);
    return 0;
}

// Player::GetNumMissionSuccesses -- retail 0x75690 (slot +0x338); -1 on failure.
SERVER_API int __cdecl GetNumMissionSuccesses(const unsigned int& player_id, int& out_count) {
    if (!IsValidPlayer(player_id)) { out_count = -1; return -2; }
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) { out_count = -1; return -2; }
    out_count = p->num_mission_successes;
    return 0;
}

// Player::SetNumMissionSuccesses -- retail 0x06d556f0. As SetNumKills: +0x338, then
// IClient +0x13c (FLPACKET_SERVER_SETMISSIONSUCCESSES).
SERVER_API int __cdecl SetNumMissionSuccesses(const unsigned int& player_id, int count) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    p->num_mission_successes = count;
    ::g_pClientInterface->SendServerSetMissionSuccesses(p->client_id, count);
    return 0;
}

// Player::GetNumMissionFailures -- retail 0x75760 (slot +0x33c); -1 on failure.
SERVER_API int __cdecl GetNumMissionFailures(const unsigned int& player_id, int& out_count) {
    if (!IsValidPlayer(player_id)) { out_count = -1; return -2; }
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) { out_count = -1; return -2; }
    out_count = p->num_mission_failures;
    return 0;
}

// Player::SetNumMissionFailures -- retail 0x06d557c0. As SetNumKills: +0x33c, then
// IClient +0x140 (FLPACKET_SERVER_SETMISSIONFAILURES).
SERVER_API int __cdecl SetNumMissionFailures(const unsigned int& player_id, int count) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    p->num_mission_failures = count;
    ::g_pClientInterface->SendServerSetMissionFailures(p->client_id, count);
    return 0;
}

// Player::DisplayMissionMessage -- retail 0x75830, ordinal 109. Message type 0 sets
// bSkipNextAutosave (+0x340). Broadcast goes to the player's group (+0x404) when there
// is one; otherwise IClient slot 70 (+0x118), the client interface untested.
SERVER_API int __cdecl DisplayMissionMessage(const unsigned int& player_id, const FmtStr& fmt_str,
                                               MissionMessageType msg_type, bool broadcast) {
    if (!IsValidPlayer(player_id)) return -2;
    CSetMissionMessage Record;
    Record.Text = fmt_str;
    Record.Type = static_cast<unsigned int>(msg_type);
    if (msg_type == 0) {
        Players[player_id].bSkipNextAutosave = 1;
    }
    CPlayerGroup* const Group = static_cast<CPlayerGroup*>(Players[player_id].group_ptr);
    if (broadcast && Group != nullptr) {
        Group->SetMissionMessage(Record);
    } else {
        ::g_pClientInterface->SendServerSetMissionMessage(player_id, Record);
    }
    return 0;
}

// Player::SetStoryCue -- retail 0x75d90. IClient slot 90 (+0x168,
// FLPACKET_SERVER_SET_STORY_CUE) with both arguments by address. Nothing is stored on
// the player slot.
SERVER_API int __cdecl SetStoryCue(const unsigned int& player_id, unsigned int cue_id) {
    if (!IsValidPlayer(player_id)) return -2;
    ::g_pClientInterface->SendServerSetStoryCue(player_id, cue_id);
    return 0;
}

// Player::PopUpDialog -- retail 0x06d55df0. No player validation; IClient +0x17c
// (FLPACKET_COMMON_POP_UP_DIALOG); always 0.
SERVER_API int __cdecl PopUpDialog(unsigned int player_id, const FmtStr& title, const FmtStr& text, unsigned int param) {
    ::g_pClientInterface->SendCommonPopUpDialog(player_id, title, text, param);
    return 0;
}

// Player::ReturnBestPath -- retail 0x06d55e20. IClient +0x174
// (FLPACKET_COMMON_REQUEST_BEST_PATH). No validation, always 0.
SERVER_API int __cdecl ReturnBestPath(unsigned int player_id, unsigned char* data, int data_len) {
    ::g_pClientInterface->SendCommonRequestBestPath(player_id, data, (unsigned int)data_len);
    return 0;
}

// Player::ReturnPlayerStats -- retail 0x06d55e40. IClient +0x178
// (FLPACKET_COMMON_REQUEST_PLAYER_STATS). No validation, always 0.
SERVER_API int __cdecl ReturnPlayerStats(unsigned int player_id, unsigned char* data, int data_len) {
    ::g_pClientInterface->SendCommonRequestPlayerStats(player_id, data, (unsigned int)data_len);
    return 0;
}

SERVER_API int __cdecl SetCamera(unsigned int player_id, const Transform& transform, float fov, float aspect) {
    if (!IsValidPlayer(player_id)) return -2;

    FPayloadServerSetCamera Block;
    Block.Position = transform.Translation;

    // No null test on the engine and no seed for the quaternion (the call writes all
    // four dwords). Retail passes &transform, which works because Rotation is at +0x00.
    Server_MathEngine()->MatrixToQuaternion(Block.Orient, transform.Rotation);

    Block.Fov = fov;
    Block.Aspect = aspect;

    // The client interface is not null-tested.
    ::g_pClientInterface->SendServerSetCamera(player_id, Block);
    return 0;
}

// Player::RevertCamera -- retail 0x75f30. IClient +0x164 (FLPACKET_SERVER_REVERT_CAMERA),
// player id by address.
SERVER_API int __cdecl RevertCamera(unsigned int player_id) {
    if (!IsValidPlayer(player_id)) return -2;
    ::g_pClientInterface->SendServerRevertCamera(player_id);
    return 0;
}

// Player::LoadHint -- retail 0x06d55f80. IClient +0x144 (FLPACKET_SERVER_LOADHINT), id by
// address. No validation: neither the client interface nor the hint is tested; always 0.
SERVER_API int __cdecl LoadHint(unsigned int player_id, BaseHint* hint) {
    ::g_pClientInterface->SendServerLoadHint(player_id, hint);
    return 0;
}

// Player::GetGroupSize -- retail 0x06d55fa0, ordinal 192. No player validation; no group
// means a party of one. Always 0.
SERVER_API int __cdecl GetGroupSize(unsigned int player_id, unsigned int& out_size) {
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    CPlayerGroup* grp = p ? (CPlayerGroup*)p->group_ptr : nullptr;
    if (grp) {
        out_size = grp->GetMemberCount();
    } else {
        out_size = 1;
    }
    return 0;
}

// Player::GetName -- retail 0x75fe0 (ordinal 222). Retail's string is VC6's
// basic_string<unsigned short>; std::wstring here. Every path returns 0; an invalid id
// leaves the string untouched. PlayerDB::GetActiveCharacterName is inlined in retail.
SERVER_API int __cdecl GetName(unsigned int player_id, std::wstring& out_name) {
    if (!IsValidPlayer(player_id)) return 0;
    const wchar_t* Name = Players.GetActiveCharacterName(player_id);
    if (Name != nullptr) {
        out_name.assign(Name);
    } else {
        out_name.assign(L"<bad name>");
    }
    return 0;
}

// Player::IsGroupMember -- retail 0x06d56170, ordinal 314. No validation. Equal ids answer
// true; otherwise player1's group is asked, and no group answers false.
SERVER_API int __cdecl IsGroupMember(unsigned int player1_id, unsigned int player2_id, bool& out_result) {
    if (player1_id == player2_id) { out_result = true; return 0; }
    PlayerSlot* p1 = PlayerDB_GetPlayer(player1_id);
    CPlayerGroup* grp = p1 ? (CPlayerGroup*)p1->group_ptr : nullptr;
    out_result = grp ? grp->IsMember(player2_id) : false;
    return 0;
}

// Player::RemoveFromGroup -- retail 0x06d561c0, ordinal 361. No group returns -3.
// CPlayerGroup::DelMember clears group_ptr, unregisters the player and notifies the rest.
SERVER_API int __cdecl RemoveFromGroup(unsigned int player_id) {
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    CPlayerGroup* grp = p ? (CPlayerGroup*)p->group_ptr : nullptr;
    if (!grp) return -3;
    grp->DelMember(player_id);
    return 0;
}

// Player::RewardGroup -- retail 0x06d563a0, ordinal 388. The slot's group (+0x404) is read
// raw, before any bounds or occupancy test. With a group: RewardMembers. Solo: the usual
// guard, then PlayerData::AdjustCash(amount, true) (FUN_06d4f4e0) inlined. Always 0.
SERVER_API int __cdecl RewardGroup(unsigned int player_id, int amount) {
    CPlayerGroup* const Group = static_cast<CPlayerGroup*>(Players[player_id].group_ptr);
    if (Group != nullptr) {
        Group->RewardMembers(amount);
        return 0;
    }
    if (IsValidPlayer(player_id)) {
        Players[player_id].adjust_cash(amount, true);
    }
    return 0;
}

// Player::SetMsnID -- retail 0x76460, ordinal 431. Every path returns 0. Stores +0x408 /
// +0x40c; flag -> IClient slot 74 (+0x128, MISSIONSAVEA) with the client id by value;
// then IClient slot 114 (+0x1c8) unconditionally. The client interface is not null-tested.
SERVER_API int __cdecl SetMsnID(unsigned int player_id, unsigned int msn_id, unsigned int sub_id,
                                  bool flag, unsigned int param) {
    if (!IsValidPlayer(player_id)) return 0;
    PlayerData& Slot = Players[player_id];
    Slot.group_mission_id = msn_id;
    Slot.group_mission_set_by = sub_id;
    if (flag) {
        ::g_pClientInterface->SendServerMissionSaveA(Slot.client_id, param);
    }
    ::g_pClientInterface->SendServerMiscObjUpdateMission(player_id, msn_id, sub_id);
    return 0;
}

// Player::GetMsnID -- retail 0x76500, ordinal 221 (slot +0x408). out is untouched for an
// invalid id; always 0.
SERVER_API int __cdecl GetMsnID(unsigned int player_id, unsigned int& out_msn_id) {
    if (IsValidPlayer(player_id)) {
        out_msn_id = Players[player_id].group_mission_id;
    }
    return 0;
}

// Player::SetRank -- retail 0x76550. Stores rank_level (+0x284), then IClient slot 117
// (+0x1d4) with (id, id, rank): retail passes the same id twice, by value.
SERVER_API int __cdecl SetRank(unsigned int player_id, int rank) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    p->rank_level = (unsigned int)rank;
    ::g_pClientInterface->SendServerMiscObjUpdateRank(player_id, player_id, rank);
    return 0;
}

// Player::SetMoneyNeededToNextRank -- slot +0x288.
SERVER_API int __cdecl SetMoneyNeededToNextRank(unsigned int player_id, int money) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    p->money_needed_to_next_rank = money;
    return 0;
}

// Player::SetMonkey / SetRobot / SetTrent -- retail 0x76600 / 0x76650 / 0x766c0. All three
// write the session Costume at PlayerData+0x2E4 and send nothing. The literals are
// nickname ids (CreateID hashes); SetMonkey touching only PartIds[0] is retail.
SERVER_API int __cdecl SetMonkey(unsigned int player_id) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    p->costume.PartIds[0] = 0x88db0044u;
    return 0;
}

SERVER_API int __cdecl SetRobot(unsigned int player_id) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    p->costume.PartIds[0] = 0u;
    p->costume.PartIds[1] = 0x8dffc147u;
    p->costume.PartIds[2] = 0x9dc4c883u;
    p->costume.PartIds[3] = 0x93582d4fu;
    return 0;
}

SERVER_API int __cdecl SetTrent(unsigned int player_id) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    CostumeDescriptions const* Costumes = GetCostumeDescriptions();
    if (Costumes != nullptr) {
        Costumes->get_costume((int)0xb8249f07u, p->costume);
    }
    return 0;
}

// Player::SendNNMessage -- retail 0x76720. Every failure, an invalid player included,
// returns -4. IClient slot 69 (+0x114).
SERVER_API int __cdecl SendNNMessage(unsigned int player_id, unsigned int msg_id) {
    if (msg_id == 0) return -4;
    if (!IsValidPlayer(player_id)) return -4;
    ::g_pClientInterface->SendServerSendNNMessage(player_id, msg_id);
    return 0;
}

// Player::ForceLand -- retail 0x76780: IServerImpl::Dock(base, player), both by reference
// (IServer slot 91). No player validation here; it lives inside Dock. `Server` is a static
// object, so there is nothing to null-test.
SERVER_API int __cdecl ForceLand(unsigned int player_id, unsigned int base_id) {
    ::GetServerInterface()->Dock(base_id, player_id);
    return 0;
}

// Player::SetCostume -- retail 0x767c0 is `XOR EAX,EAX / RET`: a shipped no-op, not a port
// stub. Do not "finish" it.
SERVER_API int __cdecl SetCostume(const unsigned int& player_id, int costume_id) {
    (void)player_id; (void)costume_id;
    return 0;
}

// Player::GetGender -- retail 0x767a0 (ordinal 183): always 1, no validation.
SERVER_API int __cdecl GetGender(const unsigned int& player_id, int& out_gender) {
    out_gender = 1;
    return 0;
}

// Player::GetBody -- retail 0x767b0 (ordinal 158): always 0, no validation.
SERVER_API int __cdecl GetBody(const unsigned int& player_id, unsigned int& out_body) {
    out_body = 0;
    return 0;
}

// Player::GetGroupMembers -- retail 0x761f0: CPlayerGroup::StoreMemberList when grouped.
SERVER_API int __cdecl GetGroupMembers(unsigned int player_id, std::vector<unsigned int>& out_members) {
    out_members.clear();
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (p && p->group_ptr) {
        CPlayerGroup* grp = (CPlayerGroup*)p->group_ptr;
        grp->StoreMemberList(out_members);
    } else {
        out_members.push_back(player_id);
    }
    return 0;
}

// Player::SetMissionObjectives -- retail .text:06d55960. Touches no server-side mission
// log: one FServerSetMissionObjectivesInfo to IClient slot 72 (+0x120), the client
// interface untested. The entry array is an eh-vector block (count cookie + count * 0x1c);
// retail fills it without testing the pointer after a failed new.
SERVER_API int __cdecl SetMissionObjectives(const unsigned int& player_id, const unsigned int& mission_id, const MissionObjective* objs, unsigned int count, const FmtStr& title, unsigned char type, const FmtStr& desc) {
    if (!IsValidPlayer(player_id)) return -2;

    FServerSetMissionObjectivesInfo Payload;
    Payload.MissionId = mission_id;
    Payload.Count = count;
    Payload.TextA = title;
    Payload.MissionType = type;
    Payload.TextB = desc;

    FServerMissionObjectiveEntry* Entries = nullptr;
    if (count != 0) {
        unsigned char* const Block = static_cast<unsigned char*>(
            operator new(count * sizeof(FServerMissionObjectiveEntry) + 4));
        if (Block != nullptr) {
            *reinterpret_cast<unsigned int*>(Block) = count;
            Entries = reinterpret_cast<FServerMissionObjectiveEntry*>(Block + 4);
            for (unsigned int I = 0; I < count; ++I)
                new (&Entries[I]) FServerMissionObjectiveEntry;
        }
        for (unsigned int I = 0; I < count; ++I) {
            Entries[I].Text = objs[I].Description;
            Entries[I].State = objs[I].Type;
        }
    }
    Payload.Entries = Entries;

    ::g_pClientInterface->SendServerSetMissionObjectives(player_id, Payload);

    if (Entries != nullptr) {
        unsigned char* const Block = reinterpret_cast<unsigned char*>(Entries) - 4;
        unsigned int const Cookie = *reinterpret_cast<unsigned int*>(Block);
        for (unsigned int I = Cookie; I != 0; --I)
            Entries[I - 1].~FServerMissionObjectiveEntry();
        operator delete(Block);                                           // FUN_06d5ffb0
    }
    return 0;
}

// Player::ReplaceMissionObjective -- retail 0x75c10. One record to IClient slot 71
// (+0x11c), the client interface untested. Nothing is written to the player slot (the
// mission log at +0x390 is not touched).
SERVER_API int __cdecl ReplaceMissionObjective(const unsigned int& player_id, const unsigned int& mission_id, unsigned int index, const MissionObjective& obj) {
    if (!IsValidPlayer(player_id)) return -2;
    FServerReplaceMissionObjectiveInfo Record;
    Record.MissionId = mission_id;
    Record.ObjectiveIndex = static_cast<unsigned char>(index);
    Record.Text = obj.Description;
    Record.ObjectiveType = obj.Type;
    ::g_pClientInterface->SendServerReplaceMissionObjective(player_id, Record);
    return 0;
}

// Player::SetMissionObjectiveState -- retail 0x75d20, ordinal 426. One record to IClient
// slot 73 (+0x124), the client interface untested.
SERVER_API int __cdecl SetMissionObjectiveState(const unsigned int& player_id, const unsigned int& mission_id, int index, unsigned int state) {
    if (!IsValidPlayer(player_id)) return -2;
    FServerMissionObjectiveStateInfo Record;
    Record.MissionId = mission_id;
    Record.ObjectiveIndex = static_cast<unsigned char>(index);
    Record.State = state;
    ::g_pClientInterface->SendServerSetMissionObjectiveState(player_id, Record);
    return 0;
}

} // namespace Player
// ===========================================================================
// pub::Reputation exports
// ===========================================================================

} // namespace pub -- the vibe-id bitmap is file-level Server state, not pub::

// Vibe ID bitmap, retail .data:06d8f768 (a std::bitset<0x1000>; FUN_06d15f90 is its
// out_of_range throw), and the last allocated index .data:06d8f968. Both .bss.
static unsigned int g_VibeBitmap[128] = {};
static unsigned int g_VibeBitmapHint = 0;

// Server_VibeID_Alloc -- retail FUN_06d45b60, cdecl. Callers: PlayerData::apply_saved_character
// (FUN_06d4a5d0, server_savegame.h) and pub::Reputation below. Scans from hint + 1; the
// wrap to 0 skips the hint test, and arriving back at the hint fails whatever its bit
// says (the hint slot itself is never tried). Failure writes 0xffffffff.
void Server_VibeID_Alloc(unsigned int* OutId)
{
    unsigned int Index = g_VibeBitmapHint + 1;
    bool bFirstLap = true;
    if (Index >= 0x1000)
        Index = 0;
    for (;;) {
        unsigned int const Mask = 1u << (Index & 0x1f);
        bool const bTaken = (g_VibeBitmap[Index >> 5] & Mask) != 0;
        if (!bFirstLap)
            break;
        if (!bTaken) {
            g_VibeBitmap[Index >> 5] |= Mask;
            g_VibeBitmapHint = Index;
            *OutId = Index;
            return;
        }
        ++Index;
        if (Index >= 0x1000)
            Index = 0;
        else if (Index == g_VibeBitmapHint)
            bFirstLap = false;
    }
    *OutId = 0xFFFFFFFFu;
}

// Server_VibeID_Free -- retail FUN_06d45c20, cdecl, id by address. Only caller
// pub::Reputation::Free. An id >= 0x1000 throws std::out_of_range (bitset's _Xran,
// FUN_06d15f90), as retail.
static void Server_VibeID_Free(unsigned int const* Id)
{
    unsigned int const Index = *Id;
    if (Index >= 0x1000)
        throw std::out_of_range("invalid bitset<N> position");
    g_VibeBitmap[Index >> 5] &= ~(1u << (Index & 0x1f));
}

namespace pub {

// Server_NotifyRepChange -- retail FUN_06d5f130, cdecl (int const* Vibe, unsigned int
// SessionId). Called by pub::Reputation's Alloc, SetAffiliation, SetReputation, SetAttitude
// and the group-pair SetReputation. Both branches send IClient +0x108
// (FLPACKET_SERVER_SETREPUTATION) with an FServerSetReputationInfo (rpc_iclient_args.h).
// SP: Vibe::Get fills the record and it goes to every active player.
// MP: only to SessionId itself (not the slot's +0x348), with AttitudeTowards the player's
// vibe (+0x318).
// Deviation: fields retail leaves as stack residue (+0x04 in SP; +0x08/+0x0c in MP, which
// rpclocal serializes) are zeroed by the port; no retail value exists to copy.
static void Server_NotifyRepChange(int const* vibe_handle, unsigned int session_id)
{
    if (SinglePlayer()) {
        FServerSetReputationInfo Info;
        Info.Attitude = 0.0f;               // never written by retail (stack residue)
        Info.Affiliation = 0;
        Info.Rank = 0;
        Info.Relations = nullptr;
        Info.RelationCount = 0;
        Info.Pad_0x15[0] = 0;
        Info.Pad_0x15[1] = 0;
        Info.Pad_0x15[2] = 0;
        Info.DisplayName[0] = L'\0';
        Info.VibeHandle = *vibe_handle;
        Info.Relations = static_cast< ::Reputation::Relation*>(
            ::operator new(GlobalReputation::group_count() * 8));
        wchar_t const* VibeName = nullptr;
        GlobalReputation::Vibe::Get(Info.VibeHandle, Info.Affiliation, Info.Rank,
                                    Info.RelationCount, Info.Relations, Info.NameA,
                                    Info.NameB, VibeName);
        if (VibeName != nullptr) {
            wcscpy(Info.DisplayName, VibeName);                         // unbounded, as retail
        } else {
            Info.DisplayName[0] = L'\0';
        }
        for (PlayerData* Active = Players.traverse_active(nullptr); Active != nullptr;
             Active = Players.traverse_active(Active)) {
            g_pClientInterface->SendServerSetReputation(Active->client_id, Info);
        }
        ::operator delete(Info.Relations);                              // FUN_06d5ffb0
        return;
    }

    if (session_id == 0 || (session_id - 1) >= PlayerDB_GetMaxPlayers()) return;
    if (Players[session_id].client_id == 0) return;

    FServerSetReputationInfo Info;
    Info.Affiliation = 0;                    // stack residue in retail
    Info.Rank = 0;
    Info.Relations = nullptr;
    Info.RelationCount = 0;
    Info.Pad_0x15[0] = 0;
    Info.Pad_0x15[1] = 0;
    Info.Pad_0x15[2] = 0;
    Info.DisplayName[0] = L'\0';
    Info.VibeHandle = *vibe_handle;
    int const PlayerVibe = Players[session_id].rep_vibe;
    Info.Attitude = 0.0f;
    GlobalReputation::Vibe::AttitudeTowards(Info.VibeHandle, PlayerVibe,
                                            Info.Attitude);
    g_pClientInterface->SendServerSetReputation(session_id, Info);
}

namespace Reputation {

// pub::Reputation::Alloc -- retail 0x7f0f0: Server_VibeID_Alloc, Vibe::Alloc, Vibe::SetName,
// Server_NotifyRepChange.
SERVER_API int __cdecl Alloc(int& vibe_handle, const FmtStr& name_fmtstr, const FmtStr& nickname_fmtstr) {
    unsigned int vibe_id = (unsigned int)vibe_handle;
    Server_VibeID_Alloc(&vibe_id);
    vibe_handle = (int)vibe_id;
    // A non-zero Vibe::Alloc result skips SetName and the notify.
    int result = GlobalReputation::Vibe::Alloc(vibe_handle);
    if (result == 0) {
        GlobalReputation::Vibe::SetName(vibe_handle, name_fmtstr, nickname_fmtstr, nullptr);
        int vibe_tmp = vibe_handle;
        Server_NotifyRepChange(&vibe_tmp, 0);
    }
    return result;
}

// pub::Reputation::Free -- retail 0x7f3e0. No guard: Vibe::Free, then the bitmap bit is
// cleared through the same reference (Server_VibeID_Free throws for >= 0x1000).
SERVER_API int __cdecl Free(const int& vibe_handle) {
    int result = GlobalReputation::Vibe::Free(vibe_handle);
    Server_VibeID_Free(reinterpret_cast<unsigned int const*>(&vibe_handle));
    return result;
}

// pub::Reputation::GetAffiliation -- retail 0x7f400. 0xFFFFFFFF on failure.
SERVER_API int __cdecl GetAffiliation(const int& vibe_handle, unsigned int& out_affiliation) {
    int result = GlobalReputation::Vibe::GetAffiliation(vibe_handle, out_affiliation, true);
    if (result == 0) return 0;
    out_affiliation = 0xFFFFFFFF;
    return -2;
}

// pub::Reputation::SetAffiliation -- retail 0x7f430. No guard.
SERVER_API int __cdecl SetAffiliation(const int& vibe_handle, const unsigned int& affiliation) {
    int result = GlobalReputation::Vibe::SetAffiliation(vibe_handle, affiliation, false);
    if (result == 0) {
        Server_NotifyRepChange(&vibe_handle, 0);
        return 0;
    }
    return -2;
}

// pub::Reputation::SetReputation (vibe overload) -- retail 0x7f470.
SERVER_API int __cdecl SetReputation(const int& vibe_handle, const unsigned int& group_id, float value) {
    int result = GlobalReputation::Vibe::SetGroupFeelingsTowards(vibe_handle, group_id, value);
    if (result == 0) {
        int vibe_tmp = vibe_handle;
        Server_NotifyRepChange(&vibe_tmp, 0);
        return 0;
    }
    return -2;
}

// pub::Reputation::GetReputation (vibe overload) -- retail 0x7f4b0.
SERVER_API int __cdecl GetReputation(const int& vibe_handle, const unsigned int& group_id, float& out_value) {
    out_value = 0.0f;
    int result = GlobalReputation::Vibe::GetGroupFeelingsTowards(vibe_handle, group_id, out_value);
    if (result == 0) return 0;
    return -2;
}

// pub::Reputation::GetAttitude -- retail 0x7f4e0.
SERVER_API int __cdecl GetAttitude(const int& vibe1, const int& vibe2, float& out_attitude) {
    out_attitude = 0.0f;
    int result = GlobalReputation::Vibe::AttitudeTowards(vibe1, vibe2, out_attitude);
    if (result == 0) return 0;
    if (result == -1) return -2;
    // Deviation: for -2 retail returns the out pointer cast to int; the port returns -3.
    if (result == -2) return -3;
    return result;
}

// pub::Reputation::SetAttitude -- retail 0x7f520. Outside SP, the target vibe's client
// session is notified.
SERVER_API int __cdecl SetAttitude(const int& vibe1, const int& vibe2, float attitude) {
    int result = GlobalReputation::Vibe::AdjustAttitude(vibe1, vibe2, attitude);
    if (result == 0) {
        if (!SinglePlayer()) {
            unsigned int client_id = GlobalReputation::Vibe::GetClientID(vibe2);
            if (client_id != 0) {
                int vibe_tmp = vibe1;
                Server_NotifyRepChange(&vibe_tmp, client_id);
            }
        }
        return 0;
    }
    if (result == -1) return -2;
    // Deviation, as GetAttitude: retail returns a non-error value for -2.
    if (result == -2) return -3;
    return result;
}

// pub::Reputation::GetRank -- retail 0x7f590. No guard. The rank is converted as an
// unsigned value (FILD of a zero-extended QWORD).
SERVER_API int __cdecl GetRank(const int& vibe_handle, float& out_rank) {
    unsigned rank_val = 0;
    int result = GlobalReputation::Vibe::GetRank(vibe_handle, rank_val);
    if (result == 0) {
        out_rank = (float)rank_val;
        return 0;
    }
    out_rank = 0.0f;
    return -2;
}

// pub::Reputation::SetRank -- retail 0x7f5e0. Outside SP, when the vibe has a client
// (0x3fffffff mask real here) and the rank actually changed, every active player gets
// IClient slot 117 (+0x1d4, SendServerMiscObjUpdateRank); the interface is not null-tested.
SERVER_API int __cdecl SetRank(const int& vibe_handle, float rank) {
    unsigned int client_id = 0;
    // Seeded 0xffffffff, so a rank of 0 counts as a change when GetRank never runs.
    unsigned int old_rank = 0xffffffff;
    if (!SinglePlayer()) {
        client_id = GlobalReputation::Vibe::GetClientID(vibe_handle);
        if ((client_id & 0x3fffffff) != 0) {
            int cid = (int)client_id;
            GlobalReputation::Vibe::GetRank(cid, old_rank);
        }
    }
    unsigned new_rank = (unsigned)(int)rank;                             // ftol
    int result = GlobalReputation::Vibe::SetRank(vibe_handle, new_rank);
    if (result != 0) return -2;
    if ((client_id & 0x3fffffff) != 0 && new_rank != old_rank) {
        for (PlayerData* Active = Players.traverse_active(nullptr); Active != nullptr;
             Active = Players.traverse_active(Active)) {
            g_pClientInterface->SendServerMiscObjUpdateRank(Active->client_id, client_id,
                                                            static_cast<int>(new_rank));
        }
    }
    return 0;
}

// pub::Reputation::GetReputationGroup -- retail 0x7f6a0. The name is copied into a local
// TString<16> (at most 15 chars) for get_id.
SERVER_API int __cdecl GetReputationGroup(unsigned int& out_id, const char* name) {
    char local_name[16] = {};
    if (name != nullptr) {
        unsigned int len = 0;
        while (len < 15 && name[len] != '\0') {
            local_name[len] = name[len];
            len++;
        }
        local_name[len] = '\0';
    }
    // Retail builds the TString in place; Length is zeroed for a null name.
    TString<16> tname;
    tname.Length = 0;
    while (tname.Length < 15 && local_name[tname.Length] != '\0') {
        ++tname.Length;
    }
    memcpy(tname.Data, local_name, 16);
    unsigned int id = GlobalReputation::get_id(tname);
    if (id != 0xFFFFFFFF) {
        out_id = id;
        return 0;
    }
    out_id = 0xFFFFFFFF;
    return -2;
}

// pub::Reputation::GetName -- retail 0x7f730.
SERVER_API int __cdecl GetName(const int& vibe_handle, FmtStr& out_name_fmta, FmtStr& out_name_fmtb) {
    wchar_t const* wide_name = nullptr;
    int result = GlobalReputation::Vibe::GetName(vibe_handle, out_name_fmta, out_name_fmtb, wide_name);
    if (result == 0) return 0;
    return -2;
}

// pub::Reputation::GetReputation (group, group) -- retail 0x7f760,
// ?GetReputation@Reputation@pub@@YAHABI0AAM@Z (an overload, not a separate name).
SERVER_API int __cdecl GetReputation(const unsigned int& group_a, const unsigned int& group_b, float& out_value) {
    if (group_a == 0xFFFFFFFF || group_b == 0xFFFFFFFF) return -4;
    int result = GlobalReputation::get_feelings_towards(group_a, group_b, out_value);
    if (result == 0) return 0;
    return -2;
}

// pub::Reputation::SetReputation (group, group) -- retail 0x7f7a0,
// ?SetReputation@Reputation@pub@@YAHABI0M@Z. Applies value - current via affect_relation.
SERVER_API int __cdecl SetReputation(const unsigned int& group_a, const unsigned int& group_b, float value) {
    if (group_a == 0xFFFFFFFF || group_b == 0xFFFFFFFF) return -4;
    float current = 0.0f;
    int result = GlobalReputation::get_feelings_towards(group_a, group_b, current);
    if (result == 0) {
        float delta = value - current;
        GlobalReputation::affect_relation(group_a, group_b, delta);
        int tmp = (int)group_a;
        Server_NotifyRepChange(&tmp, 0);
        return 0;
    }
    return -2;
}

// pub::Reputation::GetGroupName -- retail 0x7f810. The out-param is written on failure too.
SERVER_API int __cdecl GetGroupName(const unsigned int& group_id, unsigned int& out_name_id) {
    unsigned int name_id = GlobalReputation::get_name(group_id);
    out_name_id = name_id;
    if (name_id != 0xFFFFFFFF) return 0;
    return -2;
}

// pub::Reputation::GetShortGroupName -- retail 0x7f840. As GetGroupName.
SERVER_API int __cdecl GetShortGroupName(const unsigned int& group_id, unsigned int& out_name_id) {
    unsigned int name_id = GlobalReputation::get_short_name(group_id);
    out_name_id = name_id;
    if (name_id != 0xFFFFFFFF) return 0;
    return -2;
}

// pub::Reputation::GetGroupFeelingsTowards -- retail 0x7f870. Only -1 maps to -2.
SERVER_API int __cdecl GetGroupFeelingsTowards(const int& vibe_handle, const unsigned int& group_id, float& out_feeling) {
    int result = GlobalReputation::Vibe::GetGroupFeelingsTowards(vibe_handle, group_id, out_feeling);
    if (result != -1) return 0;
    return -2;
}

// pub::Reputation::GetReputation (name) -- retail 0x7f8a0, ?GetReputation@Reputation@pub@@YAHAAHPBD@Z
// (out param is `int&`, written unconditionally). The id goes to FindSolarRep as a stack
// ID_String.
SERVER_API int __cdecl GetReputation(int& out_rep, const char* name) {
    ID_String Key;
    Key.Id = CreateID(name);
    int rep = ::SolarReps::FindSolarRep(Key);
    out_rep = rep;
    if (rep != -1) return 0;
    return -2;
}

// pub::Reputation::GetReputation (ID_String) -- retail 0x7f8e0,
// ?GetReputation@Reputation@pub@@YAHAAHABUID_String@@@Z. Out written unconditionally.
SERVER_API int __cdecl GetReputation(int& out_rep, const ID_String& id) {
    int rep = ::SolarReps::FindSolarRep(id);
    out_rep = rep;
    if (rep != -1) return 0;
    return -2;
}

// pub::Reputation::EnumerateGroups -- retail 0x06d5f910, ordinal 120. An 8-byte stack
// adapter (vtable 0x06d5f940) for Common's Reputation::enumerate; its thunk dereferences
// the group id and calls the Enumerator's slot 0 by value. Always returns 0.
SERVER_API int __cdecl EnumerateGroups(Enumerator& enumerator) {
    struct FRepGroupAdapter : GlobalReputation::RepGroupCB {
        Enumerator* Sink;
        virtual char Execute(unsigned const* GroupId) { return Sink->Execute(*GroupId); }
    };
    FRepGroupAdapter Adapter;
    Adapter.Sink = &enumerator;
    GlobalReputation::enumerate(&Adapter);
    return 0;
}

} // namespace Reputation

// ===========================================================================
// pub::Zone exports
// ===========================================================================
namespace Zone {

// pub::Zone::VerifyId -- retail 0x1b920.
SERVER_API bool __cdecl VerifyId(unsigned int zone_id) {
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    return zone != nullptr;
}

// pub::Zone::GetId -- retail 0x1b940.
SERVER_API unsigned int __cdecl GetId(unsigned int system_id, const char* name) {
    Universe::IZone const* zone = Universe::get_zone(system_id, name);
    if (zone != nullptr) {
        return zone->NicknameId;
    }
    return 0;
}

// pub::Zone::GetSystem -- retail 0x1b970.
SERVER_API unsigned int __cdecl GetSystem(unsigned int zone_id) {
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        return zone->SystemId;
    }
    return 0;
}

// pub::Zone::GetPopulation -- retail 0x1b990, ?GetPopulation@Zone@pub@@YAHIAAPBV?$weighted_vector@I@@@Z
// (a reference to a const pointer). Slot +0x6c is the address of IZone::Population (+0x8c).
SERVER_API int __cdecl GetPopulation(unsigned int zone_id, const weighted_vector<unsigned int>*& out_population) {

    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        out_population = &zone->Population;
        return 0;
    }
    out_population = nullptr;
    return -2;
}

// pub::Zone::GetPos -- retail 0x1b9d0. No id-mask guard.
SERVER_API Vector __cdecl GetPos(unsigned int zone_id) {
    Vector result = {};
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (!zone) return result;
    result = zone->Pos;
    return result;
}

// pub::Zone::GetOrientation -- retail 0x1ba20 (vtable +0x14).
SERVER_API int __cdecl GetOrientation(const unsigned int& zone_id, Matrix& out_matrix) {
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        out_matrix = zone->GetOrientation();
        return 0;
    }
    return -2;
}

// pub::Zone::GetRadius -- retail 0x1ba60. Tail call to ZONE::GetRadius (vtable +0x30, the
// cached per-shape radius).
SERVER_API float __cdecl GetRadius(unsigned int zone_id) {
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr)
        return zone->GetRadius();
    return 0.0f;
}

// pub::Zone::GetSize -- retail 0x1ba80.
SERVER_API int __cdecl GetSize(unsigned int zone_id, Vector& out_size) {
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        out_size = zone->Size;
        return 0;
    }
    return -2;
}

// pub::Zone::GetShape -- retail 0x1bad0.
SERVER_API int __cdecl GetShape(unsigned int zone_id, ZoneShape& out_shape) {
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        out_shape = (ZoneShape)zone->Shape;
        return 0;
    }
    out_shape = (ZoneShape)0;
    return -2;
}

// pub::Zone::ClassifyPoint -- retail 0x1bb10 (vtable +0x48). No zone -> 1.0 (outside).
SERVER_API float __cdecl ClassifyPoint(unsigned int zone_id, const Vector& point) {
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        return zone->ClassifyPoint(point);
    }
    return 1.0f;
}

// pub::Zone::InZone -- retail 0x1bb40 (vtable +0x34).
SERVER_API bool __cdecl InZone(unsigned int zone_id, const Vector& point, float margin) {
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        return zone->InZone(point, margin);
    }
    return false;
}

// pub::Zone::GetDistance -- retail 0x1bb70 (vtable +0x3c). Signed (negative inside), not
// clamped.
SERVER_API float __cdecl GetDistance(unsigned int zone_id, const Vector& point) {
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        return zone->GetDistance(point);
    }
    return 0.0f;
}

// pub::Zone::Intersect -- retail 0x1bba0 (vtable +0x4c).
SERVER_API bool __cdecl Intersect(unsigned int zone_id, const Vector& start, const Vector& end, Vector& hit_point) {
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        return zone->Intersect(start, end, hit_point) ? true : false;
    }
    return false;
}

// pub::Zone::GetProperties -- retail 0x1bbe0 (vtable +0x1c).
SERVER_API int __cdecl GetProperties(unsigned int zone_id, unsigned long& out_props) {
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        out_props = (unsigned long)zone->PropertyFlags;
        return 0;
    }
    return -2;
}

// pub::Zone::GetLootableInfo -- retail 0x1bc10. The string outs are ID_String::clear'd and
// the ints zeroed first.
SERVER_API int __cdecl GetLootableInfo(unsigned int zone_id, ID_String& out_asteroid_loot,
                                        ID_String& out_dynamic_loot, int& out_difficulty,
                                        int& out_count_min, int& out_count_max) {
    out_asteroid_loot.clear();
    out_dynamic_loot.clear();
    out_count_max = 0;
    out_count_min = 0;
    out_difficulty = 0;
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        // vtable +0x7c returns IZone::OwnedLootEntry, a CmnAsteroid::LootableZoneEntry
        // ("[LootableZone]", installed by the +0x8c setter FUN_0633aea0). A null record
        // still returns 0.
        CmnAsteroid::LootableZoneEntry const* const Loot = zone->GetLootableInfo();
        if (Loot != nullptr) {
            out_asteroid_loot = Loot->AsteroidLootCommodityId;  // +0x08
            out_dynamic_loot = Loot->DynamicLootCommodityId;    // +0x0c
            out_difficulty = Loot->AsteroidLootDifficulty;      // +0x20
            out_count_min = Loot->AsteroidLootCountMin;         // +0x10
            out_count_max = Loot->AsteroidLootCountMax;         // +0x14
        }
        return 0;
    }
    return -2;
}

// pub::Zone::GetName -- retail 0x1bca0 (vtable +0x84).
SERVER_API int __cdecl GetName(unsigned int zone_id, unsigned int& out_name_id) {
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        out_name_id = (unsigned int)zone->IdsName;
        return 0;
    }
    return -2;
}

} // namespace Zone

// ===========================================================================
// pub::System exports
// ===========================================================================
namespace System {

// pub::System::LoadSystem -- retail .text:06cface0 (RVA 0x1ace0, ordinal 327). No id mask
// test (that lives inside add_player); a miss returns -1, not -2. StarSystem::add_player(0)
// (FUN_06d07e20) loads the system and then falls out because the session id is zero.
SERVER_API int __cdecl LoadSystem(const unsigned int& system_id) {
    FStarSystem* sys = SysDB_LookupSystem(system_id);
    if (!sys) {
        return -1;
    }
    sys->add_player(0);
    return 0;
}

// pub::System::Find -- retail .text:06cfb260 (RVA 0x1b260, ordinal 124). Looks the solar up
// with StarSystem::find_solar; out_id is written only on success.
SERVER_API int __cdecl Find(const unsigned int& system_id, const char* name, unsigned int& out_id) {
    if (name == nullptr || name[0] == '\0') return -4;
    if ((system_id & 0x3fffffff) == 0) return -4;
    FStarSystem* sys = SysDB_LookupSystem(system_id);
    if (!sys) return -2;
    unsigned int Key = CreateID(name);
    GameObject* Solar = sys->find_solar(&Key);
    if (Solar == nullptr) return -3;
    out_id = Solar->get_id();
    return 0;
}

// pub::System::GetName -- retail 0x1b8e0 (ISystem vtable +0xc).
SERVER_API int __cdecl GetName(unsigned int system_id, unsigned int& out_name_id) {
    out_name_id = 0;
    Universe::ISystem const* sys = Universe::get_system(system_id);
    if (!sys) return -2;
    out_name_id = sys->StridName;
    return 0;
}

// pub::System::GetNestedProperties -- retail .text:06cfb880 (RVA 0x1b880). ISystem slot 9
// (+0x24) with a zero-radius sphere at the position.
SERVER_API int __cdecl GetNestedProperties(const unsigned int& system_id, const Vector& position, unsigned long& out_props) {
    Universe::ISystem const* sys = Universe::get_system(system_id);
    if (!sys) return -2;
    Geometry::Sphere Query;
    Query.Center = position;
    Query.Radius = 0.0f;
    out_props = sys->GetNestedProperties(&Query);
    return 0;
}

// pub::System::EnumerateConnections -- retail .text:06cfb170 (RVA 0x1b170). ISystem slots
// 13/14 (+0x34/+0x38); slot 14's (always false) result is not read. Type 0 takes every
// record, 1/2 only that GateType, any other type none. Appended at [Count] with no bound
// check.
SERVER_API int __cdecl EnumerateConnections(const unsigned int& system_id, ConnectionEnumerator& enumerator, ConnectionType connection_type) {
    Universe::ISystem const* sys = Universe::get_system(system_id);
    if (!sys) return -2;

    int const Count = sys->GetConnectionCount();
    for (int Index = 0; Index < Count; ++Index) {
        Universe::FSystemConnection Record;
        Record.Object.clear();
        sys->GetConnection(static_cast<unsigned int>(Index), &Record);
        if (connection_type == 1) {
            if (Record.GateType != 1) continue;
        } else if (connection_type == 2) {
            if (Record.GateType != 2) continue;
        } else if (connection_type != 0) {
            continue;
        }
        unsigned int const Slot = enumerator.Count;
        enumerator.GotoSystemId[Slot] = Record.GotoSystemId;
        enumerator.Object[Slot] = Record.Object;
        enumerator.ObjectId[Slot] = Record.ObjectId;
        enumerator.Pos[Slot] = Record.Pos;
        ++enumerator.Count;
    }
    return 0;
}

// pub::System::EnumerateObjects -- retail .text:06cfb2e0 (RVA 0x1b2e0). Retail constructs an
// INI_Reader it never opens. ISystem slots 15/16 (+0x3c/+0x40), slot 16's result not read.
// Each record becomes a 0x64-byte FSysObj (two strncpy of 0x20, no terminator forced);
// enumerator slot 0 returning false stops the walk.
SERVER_API int __cdecl EnumerateObjects(const unsigned int& system_id, SysObjEnumerator& enumerator) {
    int Result = -2;
    Universe::ISystem const* sys = Universe::get_system(system_id);
    INI_Reader Reader;
    if (!sys) return Result;

    Result = 0;
    int const Count = sys->GetObjectCount();
    for (int Index = 0; Index < Count; ++Index) {
        Universe::FSystemObject Record;
        Record.Nickname.Length = 0;
        Record.Nickname.Data[0] = '\0';
        Record.Reputation.Length = 0;
        Record.Reputation.Data[0] = '\0';
        Record.NextRing.Length = 0;
        Record.NextRing.Data[0] = '\0';
        Record.PrevRing.Length = 0;
        Record.PrevRing.Data[0] = '\0';
        sys->GetSystemObject((unsigned int)Index, &Record);

        FSysObj Object;
        strncpy(Object.Nickname, Record.Nickname.Data, 0x20);
        Object.Pos = Record.Pos;
        Object.ArchetypeId = Record.ArchetypeId;
        Object.IdsName = Record.IdsName;
        Object.IdsInfo = Record.IdsInfo;
        strncpy(Object.Reputation, Record.Reputation.Data, 0x20);
        Object.DockWith = Record.DockWith;
        Object.Goto = Record.Goto;
        Object.SystemId = system_id;
        if (!enumerator.OnObject(&Object)) break;
    }
    return Result;
}

// pub::System::EnumerateZones -- retail .text:06cfad60 (RVA 0x1ad60). Opens the system file
// (ISystem slot 5) and, for every zone-list entry (slot 6), restores the reader state and
// calls enumerator slot 0 with the zone id and the reader. The enumerator's answer is
// ignored.
SERVER_API int __cdecl EnumerateZones(const unsigned int& system_id, ZoneEnumerator& enumerator) {
    int Result = -2;
    Universe::ISystem const* sys = Universe::get_system(system_id);
    INI_Reader Reader;
    if (!sys) return Result;

    char Path[0x104];
    Universe::get_filename(Path, sys->GetFile());
    if (!Reader.open(Path, false)) return Result;

    std::list<Universe::FSystemZone> const* Zones = sys->GetZones();
    std::list<Universe::FSystemZone>::const_iterator It = Zones->begin();
    std::list<Universe::FSystemZone>::const_iterator End = Zones->end();
    for (; It != End; ++It) {
        Reader.set_state(It->State);
        unsigned int ZoneId = It->Zone->GetId();      // zone vt+0x04
        enumerator.OnZone(&ZoneId, &Reader);
    }
    Result = 0;
    return Result;
}

// pub::System::ScanObjects -- retail .text:06cfad10 (ordinal 0x1ad10, cdecl),
// ?ScanObjects@System@pub@@YAHABIQAIIABVVector@@MIAAI@Z (QAI: `unsigned int* const`).
// StarSystem::scan_objects (FUN_06d0bcd0): the third parameter is the buffer capacity, the
// sixth the type mask. No id mask test.
SERVER_API int __cdecl ScanObjects(const unsigned int& system_id, unsigned int* const buffer,
                                    unsigned int max_results, const Vector& position, float radius,
                                    unsigned int type_mask, unsigned int& out_count) {
    out_count = 0;
    StarSystem* const System = SysDB_LookupSystem(system_id);
    if (System == nullptr) return -2;
    out_count = System->scan_objects(buffer, max_results, &position, radius, type_mask);
    return 0;
}

// pub::System::InZones (Vector) -- retail .text:06cfb640 (RVA 0x1b640),
// ?InZones@System@pub@@YAHIABVVector@@MQAIIAAI@Z. Sphere query on the zone tree (ISystem
// slot 7) with a 0x800-entry result buffer; the max_zones bound is tested before each hit.
SERVER_API int __cdecl InZones(unsigned int system_id, const Vector& position, float radius,
                                       unsigned int* const out_zones, unsigned int max_zones,
                                       unsigned int& out_count) {
    out_count = 0;
    if (radius < 0.0f) return -4;
    Universe::ISystem const* sys = Universe::get_system(system_id);
    if (!sys) return -2;

    ISpatialPartition* Tree = sys->GetZoneTree();
    Geometry::Sphere Query;
    Query.Center = position;
    Query.Radius = radius;
    FSpatialQueryResult Hits;
    Hits.Capacity = 0x800;
    Hits.Count = 0;
    SpatialFill(Tree, &Query, &Hits);

    for (unsigned int Index = 0; Index < Hits.Count; ++Index) {
        if (out_count >= max_zones) break;
        Universe::IZone const* Zone = static_cast<Universe::IZone const*>(Hits.Items[Index]);
        if (SphereCull(Query, Zone)) continue;
        out_zones[out_count] = Zone->GetId();       // zone vt+0x04
        ++out_count;
    }
    return 0;
}

// pub::System::InZones (Transform) -- retail .text:06cfb760 (RVA 0x1b760),
// ?InZones@System@pub@@YAHIABVTransform@@MMMQAIIAAI@Z. The floats are the frustum's AngleX /
// AngleY / Distance (+0x30/+0x34/+0x38). No radius test in this overload; otherwise as the
// Vector overload with FrustumCull.
SERVER_API int __cdecl InZones(unsigned int system_id, const Transform& transform,
                                          float angle_x, float angle_y, float distance,
                                          unsigned int* const out_zones, unsigned int max_zones,
                                          unsigned int& out_count) {
    out_count = 0;
    Universe::ISystem const* sys = Universe::get_system(system_id);
    if (!sys) return -2;

    ISpatialPartition* Tree = sys->GetZoneTree();
    Geometry::Frustum Query;
    Query.Xform = transform;
    Query.AngleX = angle_x;
    Query.AngleY = angle_y;
    Query.Distance = distance;
    FSpatialQueryResult Hits;
    Hits.Capacity = 0x800;
    Hits.Count = 0;
    SpatialFillFrustum(Tree, &Query, &Hits);

    for (unsigned int Index = 0; Index < Hits.Count; ++Index) {
        if (out_count >= max_zones) break;
        Universe::IZone const* Zone = static_cast<Universe::IZone const*>(Hits.Items[Index]);
        if (FrustumCull(Query, Zone)) continue;
        out_zones[out_count] = Zone->GetId();       // zone vt+0x04
        ++out_count;
    }
    return 0;
}

} // namespace System

// ===========================================================================
// pub::AI exports
// ===========================================================================
namespace AI {

// OP_RTYPE values. Retail exports no enumerator names; the names in common_pub.h are the
// port's. The functions below return the literal retail values:
//   0  SubmitState: personality applied / zone cleared
//   1  SubmitDirective: accepted (sent to the player's client, or set on the AI)
//   2  SubmitDirective: the AI already has a current directive
//   4  bad id (both), and every SubmitState zone / flag / unknown-op exit
//   5  op failed validate(), no object, not a CSimple, or no behavior for this op
// (IBehaviorManager::set_directive_priority in Common adds 1 and 3.)

// IDirectiveInfo is three dwords no module names (common_iface.h keeps it as bytes).
// SubmitDirective builds it at S+0x10: word 0 = kind (0 cancel, 1 behavior, 2 player
// maneuver), word 1 = BaseOp::Priority, word 2 = the behavior id (-1 until a branch sets
// it). Common's own builders use the same three words (e.g. common_launchaction.cpp).
static IDirectiveInfo Server_MakeDirectiveInfo(unsigned int Kind, unsigned int OpField08,
                                               unsigned int BehaviorId) {
    IDirectiveInfo Dir;
    unsigned int const Words[3] = { Kind, OpField08, BehaviorId };
    std::memcpy(Dir.Data, Words, sizeof(Words));
    return Dir;
}

// pub::AI::SubmitState -- retail 0x7c8d0 (.text:06d5c8d0). GetBehaviorManager is not
// null-tested (retail would fault). SetZoneBehaviorParams builds a
// PathfindManager::UserZone for the manager's PathfindManager (+0x0c); SetFlagParams sets
// one of two bytes. Zone and flag ops, and unknown ops, return 4.
SERVER_API OP_RTYPE __cdecl SubmitState(unsigned int Flid, BaseOp* Op) {
    if ((Flid & 0x3fffffff) == 0) return static_cast<OP_RTYPE>(4);
    GameObject* Obj = Server_LookupLiveObject(Flid);
    if (Obj == nullptr) return static_cast<OP_RTYPE>(4);
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);

    if (Op->OpType == OpType_SetPersonalityParams) {
        SetPersonalityParams* Params = static_cast<SetPersonalityParams*>(Op);
        Behavior->set_state_graph(Params->StateGraphId, false);
        // -1 means "leave the callback as it is".
        if (Params->DirectiveCallbackObject != (DirectiveCallback*)-1) {
            Behavior->set_directive_callback(Params->DirectiveCallbackObject);
        }
        if (Params->ContentCallbackObject != (ContentCallback*)-1) {
            Behavior->set_content_callback(Params->ContentCallbackObject);
        }
        if (Params->bSetPersonality != 0) {
            Behavior->set_personality(Params->Value);
        }
        return static_cast<OP_RTYPE>(0);
    }

    if (Op->OpType == OpType_SetZoneBehaviorParams) {
        SetZoneBehaviorParams* Params = static_cast<SetZoneBehaviorParams*>(Op);
        PathfindManager* Pathfind = Behavior->Pathfind;                // IBehaviorManager+0x0c

        // The inline ctor sets BoxRotation = identity, BoxCenter = 0 (common_pathfind.h).
        PathfindManager::UserZone Zone;
        // Deviation: retail writes only the fields each zone kind uses and submits the rest
        // as stack residue (+0x08/+0x14/+0x54 for a box, +0x08 for kind 2, +0x00 for an
        // unknown kind, +0x04 for an unknown mode). The port zeroes those fields first.
        Zone.Mode = 0;
        Zone.Invert = 0;
        Zone.TargetPoint.x = 0.0f;
        Zone.TargetPoint.y = 0.0f;
        Zone.TargetPoint.z = 0.0f;
        Zone.Radius = 0.0f;
        Zone.BoxHalfExtent.x = 0.0f;
        Zone.BoxHalfExtent.y = 0.0f;
        Zone.BoxHalfExtent.z = 0.0f;
        Zone.TargetKey = 0;

        if (Params->InvertMode == 0) {
            Zone.Invert = 0;
        } else if (Params->InvertMode == 1) {
            Zone.Invert = 1;
        } else if (Params->InvertMode == 2) {
            Pathfind->clear_user_zone();
            return static_cast<OP_RTYPE>(0);
        }

        // BoxMin = Params+0x24..+0x2c, BoxMax = Params+0x30..+0x38.
        if (Params->Mode == 0) {
            Zone.Mode = 0;
            Zone.TargetPoint.x = Params->TargetPoint.x;
            Zone.TargetPoint.y = Params->TargetPoint.y;
            Zone.TargetPoint.z = Params->TargetPoint.z;
            Zone.Radius = Params->Radius;
        } else if (Params->Mode == 1) {
            // Center = (max + min) * 0.5f (DAT_06d6b9cc), each component stored to float
            // before use; extents = |min - center|. Retail re-copies an identity into the
            // rotation, which changes nothing.
            Zone.Mode = 1;
            float const CenterX = (Params->BoxMax.x + Params->BoxMin.x) * 0.5f;
            float const CenterY = (Params->BoxMax.y + Params->BoxMin.y) * 0.5f;
            float const CenterZ = (Params->BoxMax.z + Params->BoxMin.z) * 0.5f;
            Zone.BoxCenter.x = CenterX;
            Zone.BoxCenter.y = CenterY;
            Zone.BoxCenter.z = CenterZ;
            Zone.BoxHalfExtent.x = std::fabs(Params->BoxMin.x - CenterX);
            Zone.BoxHalfExtent.y = std::fabs(Params->BoxMin.y - CenterY);
            Zone.BoxHalfExtent.z = std::fabs(Params->BoxMin.z - CenterZ);
        } else if (Params->Mode == 2) {
            Zone.Mode = 2;
            Zone.Radius = Params->Radius;
            Zone.TargetKey = Params->TargetId;
        }
        // Every kind, known or not, is submitted.
        Pathfind->submit_user_zone(Zone);
        return static_cast<OP_RTYPE>(4);
    }

    if (Op->OpType == OpType_SetFlagParams) {
        SetFlagParams* Params = static_cast<SetFlagParams*>(Op);
        if (Params->FlagId == 0) {
            // Byte PathfindManager+0x245 (common_pathfind.h bObstacleAvoidanceEnabled).
            Behavior->Pathfind->bObstacleAvoidanceEnabled = Params->bValue;
        } else if (Params->FlagId == 1) {
            // Byte SystemManager+0x11c0 == GunGroups (+0x12c) + 0x1094, the "fire control
            // active" byte common_aifirecontrol.cpp tests.
            Behavior->Sys->GunGroups.bEnabled = Params->bValue;
        }
        return static_cast<OP_RTYPE>(4);
    }

    return static_cast<OP_RTYPE>(4);
}

// OpType -> behavior id pairs of SubmitDirective's AI branch, in retail's test order
// (.text:06d5d48b: thirteen independent compares, each setting IDirectiveInfo word 0 = 1
// and word 2 = the id before set_current_directive).
struct FSubmitDirectiveBehavior {
    int OpType;
    unsigned int BehaviorId;
};
static const FSubmitDirectiveBehavior GSubmitDirectiveBehaviors[] = {
    { OpType_DirectiveGotoOp,              0x02 },
    { OpType_DirectiveFollowOp,            0x11 },
    { OpType_DirectiveTrailOp,             0x03 },
    { OpType_DirectiveDockOp,              0x07 },
    { OpType_DirectiveDelayOp,             0x14 },
    { OpType_DirectiveIdleOp,              0x06 },
    { OpType_DirectiveLaunchOp,            0x08 },
    { OpType_DirectiveInstantTradelaneOp,  0x09 },
    { OpType_DirectiveGuideOp,             0x0e },
    { OpType_DirectiveFaceOp,              0x0f },
    { OpType_DirectiveEvadeOp,             0x05 },
    { OpType_DirectiveDrasticEvadeOp,      0x12 },
    { OpType_DirectiveTractorBeamOp,       0x10 },
};

// pub::AI::SubmitDirective -- retail 0x7cbb0 (.text:06d5cbb0). Jump tables at .text:06d5d768
// (7 dwords) and .text:06d5d784 (19 bytes, indexed by OpType - 3).
// The object must be a CSimple with an ObjectDb (+0xac). When its owner player (+0xb4)
// passes the inlined PlayerDB::is_valid_ship_owner (a live slot whose ship +0x3e4 is this
// object), the directive goes to that player's client (player branch); otherwise to the AI.
SERVER_API OP_RTYPE __cdecl SubmitDirective(unsigned int Flid, BaseOp* Op) {
    if ((Flid & 0x3fffffff) == 0) return static_cast<OP_RTYPE>(4);
    if (!Op->validate()) return static_cast<OP_RTYPE>(5);

    GameObject* Obj = Server_LookupLiveObject(Flid);
    if (Obj == nullptr) return static_cast<OP_RTYPE>(5);

    CObject const* Inspected = Obj->cobject();
    if (Inspected == nullptr) return static_cast<OP_RTYPE>(5);
    if ((static_cast<unsigned int>(Inspected->ObjectClass) & 3u) != 3u) return static_cast<OP_RTYPE>(5);
    if (static_cast<CSimple const*>(Inspected)->ObjectDb == nullptr) return static_cast<OP_RTYPE>(5);

    // No null test on csimple() (retail would fault reading +0xb4 of null).
    unsigned int OwnerPlayer = Obj->csimple()->OwnerPlayer;

    if (::Players.is_valid_ship_owner(OwnerPlayer, Flid)) {
        // Player branch. Each of the six supported kinds default-constructs the struct
        // (+0x18 = -1, +0x40 = 3, the rest 0), fills its fields, stores the kind at +0x00
        // and sends IClient slot 100 (+0x190) with (&OwnerPlayer, &Info); every send returns
        // 1. Any other op returns 5 with no send. Pad_0x09 / Pad_0x91 are stack residue in
        // retail; zeroed here (rpclocal's marshal zeroes them again).
        FPayloadServerSetDirective Info = {};
        Info.DockPortIndex = -1;
        Info.GotoMode = 3;

        if (Op->OpType == OpType_DirectiveCancelOp) {
            Info.DirectiveKind = 0;
        } else if (Op->OpType == OpType_DirectiveDelayOp) {
            DirectiveDelayOp* Delay = static_cast<DirectiveDelayOp*>(Op);
            Info.DelaySeconds = Delay->DelaySeconds;
            Info.DirectiveKind = 1;
        } else if (Op->OpType == OpType_DirectiveDockOp) {
            DirectiveDockOp* Dock = static_cast<DirectiveDockOp*>(Op);
            Info.DockObjectId = Dock->DockObjectId;
            Info.DockSecondaryObjectId = Dock->NextRingId;
            Info.bDockApproachCleared = Dock->bHasDockPortIndex;
            Info.DockPortIndex = Dock->DockPortIndex;
            Info.ProxTargetId = static_cast<unsigned int>(Dock->TetherObjectId);
            Info.ProxNearDist = Dock->TetherNearRange;
            Info.ProxFarDist = Dock->TetherFarRange;
            Info.DirectiveKind = 2;
        } else if (Op->OpType == OpType_DirectiveFollowOp) {
            DirectiveFollowOp* Follow = static_cast<DirectiveFollowOp*>(Op);
            Info.FollowTargetId = Follow->TargetId;
            Info.FollowDistance = Follow->FollowDistance;
            Info.FollowOffset.x = Follow->FollowOffsetX;
            Info.FollowOffset.y = Follow->FollowOffsetY;
            Info.FollowOffset.z = Follow->FollowOffsetZ;
            Info.FollowEngageRange = Follow->EngageRange;
            Info.DirectiveKind = 3;
        } else if (Op->OpType == OpType_DirectiveIdleOp) {
            Info.DirectiveKind = 5;
        } else if (Op->OpType == OpType_DirectiveGotoOp) {
            // The only kind that fills +0x84..+0x90 (the others zero it before the send).
            DirectiveGotoOp* Goto = static_cast<DirectiveGotoOp*>(Op);
            Info.GotoMode = Goto->GotoKind;
            Info.GotoDestination = Goto->Destination;
            Info.GotoTargetId = Goto->TargetId;
            Info.SplineP1.Field_0x00 = Goto->SplineP1X;
            Info.SplineP1.Field_0x04 = Goto->SplineP1Y;
            Info.SplineP1.Field_0x08 = Goto->SplineP1Z;
            Info.SplineP2.Field_0x00 = Goto->SplineP2X;
            Info.SplineP2.Field_0x04 = Goto->SplineP2Y;
            Info.SplineP2.Field_0x08 = Goto->SplineP2Z;
            Info.SplineP0.Field_0x00 = Goto->SplineP0X;
            Info.SplineP0.Field_0x04 = Goto->SplineP0Y;
            Info.SplineP0.Field_0x08 = Goto->SplineP0Z;
            Info.SplineP3.Field_0x00 = Goto->SplineP3X;
            Info.SplineP3.Field_0x04 = Goto->SplineP3Y;
            Info.SplineP3.Field_0x08 = Goto->SplineP3Z;
            Info.GotoRange = Goto->ArrivalRange;
            Info.GotoSpeed = Goto->MaxSpeed;
            Info.bGotoFlyThrough = Goto->bFlyThrough;
            Info.Field_0x8d = Goto->Field_59;
            Info.bGotoCruise = Goto->bGotoCruise;
            Info.bGotoNoCruise = Goto->bGotoNoCruise;
            Info.ProxTargetId = Goto->TetherObjectId;
            Info.ProxNearDist = Goto->TetherNearRange;
            Info.ProxFarDist = Goto->TetherFarRange;
            Info.bGotoDisableAvoidance = Goto->bDisableAvoidance;
            Info.DirectiveKind = 4;
        } else {
            // Trail, Strafe, Face, Ram, TractorBeam, Evade, DrasticEvade, Launch,
            // InstantTradelane, Formation, Guide, WaitForShip, WaitForPlayerManeuver and the
            // Set*Params ops.
            return static_cast<OP_RTYPE>(5);
        }

        g_pClientInterface->SendServerSetDirective(OwnerPlayer, Info);
        return static_cast<OP_RTYPE>(1);
    }

    // AI branch. GetBehaviorManager is not null-tested (retail would fault).
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);

    if (Op->OpType == OpType_DirectiveWaitForPlayerManeuverOp) {
        // Kind 2 with the named behaviour; an unknown name returns 5.
        DirectiveWaitForPlayerManeuverOp* Wait = static_cast<DirectiveWaitForPlayerManeuverOp*>(Op);
        int const BehaviorId = BehaviorTypes::get_behavior_id(Wait->BehaviorName);
        if (BehaviorId == -1) return static_cast<OP_RTYPE>(5);
        Behavior->set_current_directive(
            Server_MakeDirectiveInfo(2u, Op->Priority, static_cast<unsigned int>(BehaviorId)), Op);
        return static_cast<OP_RTYPE>(1);
    }

    if (Op->OpType == OpType_DirectiveCancelOp) {
        // The callee's result is ignored.
        Behavior->set_current_directive(Server_MakeDirectiveInfo(0u, Op->Priority, 0xffffffffu), Op);
        return static_cast<OP_RTYPE>(1);
    }

    // A current directive whose word 0 is non-zero blocks every other op.
    unsigned int CurrentKind = 0;
    std::memcpy(&CurrentKind, Behavior->get_current_directive()->Data, sizeof(CurrentKind));
    if (CurrentKind != 0) return static_cast<OP_RTYPE>(2);

    unsigned int Result = 5;
    size_t const PairCount = sizeof(GSubmitDirectiveBehaviors) / sizeof(GSubmitDirectiveBehaviors[0]);
    for (size_t Index = 0; Index < PairCount; ++Index) {
        if (Op->OpType != GSubmitDirectiveBehaviors[Index].OpType) continue;
        if (Behavior->set_current_directive(
                Server_MakeDirectiveInfo(1u, Op->Priority, GSubmitDirectiveBehaviors[Index].BehaviorId), Op)) {
            Result = 1;
        }
    }
    return static_cast<OP_RTYPE>(Result);
}

// pub::AI::set_directive_priority -- retail 0x7db70. No Flid mask test.
SERVER_API OP_RTYPE __cdecl set_directive_priority(unsigned int Flid, DirectivePriority Priority) {
    GameObject* Obj = Server_LookupLiveObject(Flid);
    if (Obj == nullptr) return static_cast<OP_RTYPE>(5);
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return static_cast<OP_RTYPE>(5);
    return Behavior->set_directive_priority(Priority);
}

// pub::AI::get_behavior_id -- retail 0x7dbb0. IBehaviorManager+0xc0.
SERVER_API int __cdecl get_behavior_id(unsigned int Flid) {
    GameObject* Obj = Server_LookupLiveObject(Flid);
    if (Obj == nullptr) return -1;
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return -1;
    return Behavior->CurrentBehaviorIndex;
}

// pub::AI::get_state_graph_id -- retail 0x7dbe0. State graph at IBehaviorManager+0x04.
SERVER_API int __cdecl get_state_graph_id(unsigned int Flid) {
    GameObject* Obj = Server_LookupLiveObject(Flid);
    if (Obj == nullptr) return -1;
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return -1;
    IStateGraph const* Graph = Behavior->StateGraph;
    if (Graph == nullptr) return -1;
    return Graph->Id;
}

// pub::AI::get_personality -- retail 0x7dc20, ?get_personality@AI@pub@@YA_NIAAVPersonality@12@@Z.
// No Flid mask test.
SERVER_API bool __cdecl get_personality(unsigned int Flid, Personality& OutPersonality) {
    GameObject* Obj = Server_LookupLiveObject(Flid);
    if (Obj == nullptr) return false;
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return false;
    Behavior->get_personality(OutPersonality);
    return true;
}

// pub::AI::enable_maneuver -- retail 0x7dc60. The callee's bool is dropped: true whenever
// the behavior exists.
SERVER_API bool __cdecl enable_maneuver(unsigned int Flid, int ManeuverId, bool bEnable) {
    GameObject* Obj = Server_LookupLiveObject(Flid);
    if (Obj == nullptr) return false;
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return false;
    Behavior->enable_maneuver(ManeuverId, bEnable);
    return true;
}

// pub::AI::enable_all_maneuvers -- retail 0x7dca0. A tail call, so the callee's result is
// returned.
SERVER_API bool __cdecl enable_all_maneuvers(unsigned int Flid) {
    GameObject* Obj = Server_LookupLiveObject(Flid);
    if (Obj == nullptr) return false;
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return false;
    return Behavior->enable_all_maneuvers();
}

// pub::AI::lock_maneuvers -- retail 0x7dcd0. As enable_maneuver: the callee's bool is
// dropped.
SERVER_API bool __cdecl lock_maneuvers(unsigned int Flid, bool bLock) {
    GameObject* Obj = Server_LookupLiveObject(Flid);
    if (Obj == nullptr) return false;
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return false;
    Behavior->lock_maneuvers(bLock);
    return true;
}

// pub::AI::get_scan_response -- retail 0x7dd10. The object looked up is the second
// argument. A player-owned target gets IClient::SendServerScanNotify (+0x1b4) and 3;
// otherwise the AI answers. No null test on csimple().
SERVER_API ScanResponse __cdecl get_scan_response(unsigned int ScannerFlid, unsigned int TargetFlid, unsigned int GoodId) {
    GameObject* Obj = Server_LookupLiveObject(TargetFlid);
    if (Obj == nullptr) return static_cast<ScanResponse>(0);
    unsigned int const OwnerPlayer = Obj->csimple()->OwnerPlayer;
    if (::Players.is_valid_ship_owner(OwnerPlayer, TargetFlid)) {
        g_pClientInterface->SendServerScanNotify(OwnerPlayer, ScannerFlid, GoodId);
        return static_cast<ScanResponse>(3);
    }
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return static_cast<ScanResponse>(0);
    return Behavior->get_scan_response(ScannerFlid);
}

// pub::AI::submit_forced_target -- retail 0x7ddc0. No message and no Flid mask. The
// TargetManager is SystemManager+0x142C (SystemManager::Targeting, common_systemmanager.h);
// submit_forced_target (Common 0x062eec10) appends to an 8-entry id array.
SERVER_API int __cdecl submit_forced_target(unsigned int ship_id, unsigned int target_id) {
    GameObject* Obj = Server_LookupLiveObject(ship_id);
    if (Obj == nullptr) return 0;
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return 0;
    if (Behavior->Sys == nullptr) return 0;
    return Behavior->Sys->Targeting.submit_forced_target(target_id);
}

// pub::AI::remove_forced_target -- retail 0x7de00. As submit_forced_target, over
// TargetManager::remove_forced_target (Common 0x062eec50).
SERVER_API int __cdecl remove_forced_target(unsigned int ship_id, unsigned int target_id) {
    GameObject* Obj = Server_LookupLiveObject(ship_id);
    if (Obj == nullptr) return 0;
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return 0;
    if (Behavior->Sys == nullptr) return 0;
    return Behavior->Sys->Targeting.remove_forced_target(target_id);
}

// pub::AI::set_player_enemy_clamp -- retail 0x7de40. As submit_forced_target, over
// TargetManager::set_player_enemy_clamp (Common 0x062ee5e0, stores DAT_063ec754/758). No
// message.
SERVER_API bool __cdecl set_player_enemy_clamp(unsigned int player_id, int min_rep, int max_rep) {
    GameObject* Obj = Server_LookupLiveObject(player_id);
    if (Obj == nullptr) return false;
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return false;
    if (Behavior->Sys == nullptr) return false;
    Behavior->Sys->Targeting.set_player_enemy_clamp(min_rep, max_rep);
    return true;
}

// pub::AI::update_formation_state -- retail 0x7d7a0,
// ?update_formation_state@AI@pub@@YA?AW4FORMATION_RTYPE@@IIABVVector@@@Z (the global enum).
// Arguments: the ship changing formation, its new leader (may be 0), its offset. Returns -3
// when the ship does not resolve, else 0.
// Outside SP an unchanged leader returns early. Outside SP, before the change the old
// leader's formation is broadcast (old leader id only when it has followers, every
// follower id except the ship's; the ship still receives it), and after the change the
// new leader's (leader id always, no exclusion). Receivers are the followers and the
// leader whose cobject() is a player CShip (byte +0x2bd), via IClient slot 120 (+0x1e0).
// The old leader drops the ship unless it is the ship (compared by get_id); the new leader
// adds it unless it is the ship (compared by pointer).
// Retail reads Followers[i] for i < count with no bound against the 16-entry array; so
// does the port.

// The receiver step retail inlines four times.
static void Formation_SendIdsToOwner(IObjRW const* Obj, std::vector<unsigned int> const& Ids) {
    CObject const* const Cobj = Obj->cobject();                    // +0x150
    if (Cobj == nullptr)
        return;
    if ((static_cast<unsigned int>(Cobj->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
        return;
    CShip const* const ShipCobj = static_cast<CShip const*>(Cobj);
    if (ShipCobj->bIsPlayer == 0)                                  // +0x2bd
        return;
    ::g_pClientInterface->SendServerMiscObjUpdateFormation(ShipCobj->OwnerPlayer, Ids);   // +0xb4
}

SERVER_API FORMATION_RTYPE __cdecl update_formation_state(unsigned int ShipFlid, unsigned int LeaderFlid, const Vector& Offset) {
    GameObject* Ship = Server_LookupLiveObject(ShipFlid);
    if (Ship == nullptr) return FORMATION_RTYPE_NOT_FOUND;

    IObjRW* OldLeader = nullptr;
    Ship->get_formation_leader(OldLeader);                           // IObjInspect +0xb0
    IObjRW* const NewLeader = Server_LookupLiveObject(LeaderFlid);
    if (!SinglePlayer() && NewLeader == OldLeader)
        return FORMATION_RTYPE_OK;

    if (!SinglePlayer() && OldLeader != nullptr) {
        std::vector<unsigned int> Ids;
        int const Count = OldLeader->get_formation_follower_count(); // IObjAffect +0x20
        IObjRW* Followers[0x10];
        OldLeader->get_formation_followers(Followers, 0x10);        // +0x14c
        if (Count != 0)
            Ids.push_back(OldLeader->get_id());
        for (int I = 0; I < Count; ++I) {
            if (Followers[I]->get_id() != Ship->get_id())
                Ids.push_back(Followers[I]->get_id());
        }
        for (int I = 0; I < Count; ++I)
            Formation_SendIdsToOwner(Followers[I], Ids);
        Formation_SendIdsToOwner(OldLeader, Ids);
    }

    if (OldLeader != nullptr && OldLeader->get_id() != Ship->get_id())
        (void)OldLeader->remove_formation_follower(Ship);           // IObjAffect +0x1c
    (void)Ship->set_formation_leader(NewLeader);                    // +0x10
    (void)Ship->set_formation_offset(Offset);                       // +0x14
    if (NewLeader != nullptr && NewLeader != Ship)
        (void)NewLeader->add_formation_follower(Ship);              // +0x18

    if (!SinglePlayer() && NewLeader != nullptr) {
        std::vector<unsigned int> Ids;
        int const Count = NewLeader->get_formation_follower_count();
        IObjRW* Followers[0x10];
        NewLeader->get_formation_followers(Followers, 0x10);
        Ids.push_back(NewLeader->get_id());
        for (int I = 0; I < Count; ++I)
            Ids.push_back(Followers[I]->get_id());
        for (int I = 0; I < Count; ++I)
            Formation_SendIdsToOwner(Followers[I], Ids);
        Formation_SendIdsToOwner(NewLeader, Ids);
    }
    return FORMATION_RTYPE_OK;
}

// pub::AI::refresh_state_graph -- retail 0x7db40, a tail call into
// IBehaviorManager::refresh_state_graph.
SERVER_API void __cdecl refresh_state_graph(unsigned int Flid) {
    GameObject* Obj = Server_LookupLiveObject(Flid);
    if (Obj == nullptr) return;
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return;
    Behavior->refresh_state_graph();
}

} // namespace AI

// pub::Phantom exports -- the record, the global list and the event pump are above
// namespace pub::SpaceObj (Server_PumpPhantomEvents).
namespace Phantom {

// pub::Phantom::Create (box) -- retail .text:06cfbe20. Size components must be > 0 (NaN
// rejected). CreatePhantom's result is not tested. Deviation: PhantomParms bytes retail
// leaves unwritten (+0x07 .. +0x0f) are zeroed here.
SERVER_API int __cdecl Create(unsigned int SystemId, const Vector& Size, const Vector& Pos,
                              const Matrix& Orient, unsigned int TypeMask, void*& OutHandle) {
    if (GbPhantomPumpBusy) return -5;
    if (!(Size.x > 0.0f)) return -4;
    if (!(Size.y > 0.0f)) return -4;
    if (!(Size.z > 0.0f)) return -4;
    if ((SystemId & 0x3fffffffu) == 0) return -4;

    ServerPhantom* const Ph = new ServerPhantom();                       // operator new(0x28)
    PhySys::PhantomParms Parms;
    memset(&Parms, 0, sizeof(Parms));
    Parms.SystemId = SystemId;
    Parms.bPhysicalUnmoveable = 1;
    Parms.Flag05 = 0;
    Parms.Name[0] = 0;
    Parms.Position = Pos;
    Parms.Orientation = Orient;
    Parms.Listener = Ph;
    Ph->TypeMask = TypeMask;
    Ph->SystemId = SystemId;
    PhySys::CreatePhantom(Parms, Size, Ph->PhyHandle);
    OutHandle = Ph;
    GPhantoms.push_back(Ph);
    return 0;
}

// pub::Phantom::Create (sphere) -- retail .text:06cfc020. As the box version with an
// identity orientation; radius must be > 0 (NaN rejected).
SERVER_API int __cdecl Create(unsigned int SystemId, float Radius, const Vector& Center,
                              unsigned int TypeMask, void*& OutHandle) {
    if (GbPhantomPumpBusy) return -5;
    if (!(Radius > 0.0f)) return -4;
    if ((SystemId & 0x3fffffffu) == 0) return -4;

    ServerPhantom* const Ph = new ServerPhantom();
    PhySys::PhantomParms Parms;
    memset(&Parms, 0, sizeof(Parms));
    Parms.SystemId = SystemId;
    Parms.bPhysicalUnmoveable = 1;
    Parms.Flag05 = 0;
    Parms.Name[0] = 0;
    Parms.Position = Center;
    Parms.Orientation = Matrix(1.0f, 0.0f, 0.0f,
                               0.0f, 1.0f, 0.0f,
                               0.0f, 0.0f, 1.0f);
    Parms.Listener = Ph;
    Ph->TypeMask = TypeMask;
    Ph->SystemId = SystemId;
    PhySys::CreatePhantom(Parms, Radius, Ph->PhyHandle);
    OutHandle = Ph;
    GPhantoms.push_back(Ph);
    return 0;
}

// pub::Phantom::Destroy -- retail .text:06cfbcd0. A handle not on the global list -> -2.
SERVER_API int __cdecl Destroy(void* Handle) {
    if (GbPhantomPumpBusy) return -5;
    std::list<ServerPhantom*>::iterator It = FindPhantom(Handle);
    if (It == GPhantoms.end()) return -2;
    PhySys::DestroyPhantom((*It)->PhyHandle);
    delete *It;                                                          // FUN_06cfbd60
    GPhantoms.erase(It);
    return 0;
}

// pub::Phantom::SetActive -- retail .text:06cfbfd0.
SERVER_API int __cdecl SetActive(void* Handle, bool bActive) {
    if (GbPhantomPumpBusy) return -5;
    std::list<ServerPhantom*>::iterator It = FindPhantom(Handle);
    if (It == GPhantoms.end()) return -2;
    PhySys::SetCollisionStatePhantom((*It)->PhyHandle, bActive);
    return 0;
}

// pub::Phantom::Attach -- retail .text:06cfc1e0. A missing object is -2, a missing handle
// -3. The CObject is taken under the (& 1) test, not null-tested after.
SERVER_API int __cdecl Attach(const unsigned int& ObjId, void* Handle) {
    if (GbPhantomPumpBusy) return -5;
    GameObject* const Obj = Server_LookupLiveObject(ObjId);
    if (Obj == nullptr) return -2;
    std::list<ServerPhantom*>::iterator It = FindPhantom(Handle);
    if (It == GPhantoms.end()) return -3;
    CObject* Raw = Obj->Object;
    if (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 1u) != 1u)
        Raw = nullptr;
    if (PhySys::AttachPhantom(Raw, (*It)->PhyHandle) != 0) return -1;
    return 0;
}

// pub::Phantom::Detach -- retail .text:06cfc270.
SERVER_API int __cdecl Detach(void* Handle) {
    if (GbPhantomPumpBusy) return -5;
    std::list<ServerPhantom*>::iterator It = FindPhantom(Handle);
    if (It == GPhantoms.end()) return -2;
    if (PhySys::DetachPhantom((*It)->PhyHandle) != 0) return -1;
    return 0;
}

} // namespace Phantom

// ===========================================================================
// pub::Market exports (retail 0x385c0 - 0x387d0)
// ===========================================================================
namespace Market {

// Every export goes through BaseDB::find_base (FUN_06d140f0; a miss FDUMPs "Base %u is
// invalid", line 0x659) and a FUN_06d12xxx helper over the market map at BaseData+0x10.
// Every failure returns -4, never -1. Only IsGoodInDemand, GetNumCommoditiesForSale and
// GetNumCommoditiesInDemand clear their out-parameter before the bad-id check.

// pub::Market::GetMinInventory -- retail 0x385c0 (helper FUN_06d12c70). The mapped value's
// +0x08 (MinInventory) on a hit, 0 on a miss.
SERVER_API int __cdecl GetMinInventory(unsigned int BaseId, unsigned int GoodId, int& OutInv) {
    if ((BaseId & 0x3fffffff) == 0 || (GoodId & 0x3fffffff) == 0) return -4;
    FBaseEntry* Entry = GetBaseDB()->find_base(BaseId);
    if (!Entry) return -4;
    BaseData* base = Entry->Data;
    std::map<unsigned int, MarketGoodInfo>::const_iterator it = base->Market.find(GoodId);
    OutInv = (it != base->Market.end()) ? it->second.MinInventory : 0;
    return 0;
}

// pub::Market::GetPrice -- retail 0x38610 (helper FUN_06d12cd0). Returns the stored price
// (mapped +0x04); Common computes and ftol-truncates it once at load time (MarketGoodInfo,
// common_goods.h), nothing is recomputed here.
SERVER_API int __cdecl GetPrice(unsigned int BaseId, unsigned int GoodId, float& OutPrice) {
    if ((BaseId & 0x3fffffff) == 0 || (GoodId & 0x3fffffff) == 0) return -4;
    FBaseEntry* Entry = GetBaseDB()->find_base(BaseId);
    if (!Entry) return -4;
    BaseData* base = Entry->Data;
    std::map<unsigned int, MarketGoodInfo>::const_iterator it = base->Market.find(GoodId);
    if (it != base->Market.end()) {
        OutPrice = it->second.Price;
        return 0;
    }
    // A miss falls back to the nominal GoodInfo+0x58, with no null test.
    OutPrice = GoodList::find_by_id(GoodId)->Price;
    return 0;
}

// pub::Market::GetNominalPrice -- retail 0x38660. GoodInfo+0x58.
SERVER_API int __cdecl GetNominalPrice(unsigned int GoodId, float& OutPrice) {
    if ((GoodId & 0x3fffffff) == 0) return -4;
    GoodInfo const* good = GoodList::find_by_id(GoodId);
    if (!good) return -4;
    OutPrice = good->Price;
    return 0;
}

// pub::Market::GetGoodJumpDist -- retail 0x38690. GoodInfo+0x6c. Unlike its neighbours it
// does not screen the good id first.
SERVER_API int __cdecl GetGoodJumpDist(unsigned int GoodId, unsigned int& OutDist) {
    GoodInfo const* good = GoodList::find_by_id(GoodId);
    if (!good) return -4;
    OutDist = (unsigned int)good->JumpDist;
    return 0;
}

// pub::Market::IsGoodInDemand -- retail 0x386c0 (helper FUN_06d12d20). TransactionType is
// mapped +0x10.
SERVER_API int __cdecl IsGoodInDemand(unsigned int BaseId, unsigned int GoodId, bool& OutDemand) {
    OutDemand = false;
    if ((BaseId & 0x3fffffff) == 0 || (GoodId & 0x3fffffff) == 0) return -4;
    FBaseEntry* Entry = GetBaseDB()->find_base(BaseId);
    if (!Entry) return -4;
    BaseData* base = Entry->Data;
    std::map<unsigned int, MarketGoodInfo>::const_iterator it = base->Market.find(GoodId);
    if (it != base->Market.end()) {
        OutDemand = (it->second.Type == TransactionBuyAndSell ||
                     it->second.Type == TransactionDemandOnly);
    }
    return 0;
}

// The two commodity filters. Retail feeds GoodList::find_by_id the id out of the mapped
// value (+0x00), not the map key, and requires the commodity category (GoodInfo+0x4c == 0).
static bool MarketGoodIsCommodity(MarketGoodInfo const& Entry) {
    GoodInfo const* info = GoodList::find_by_id(Entry.GoodId);
    return info != nullptr && info->Category == (unsigned int)GoodCategoryCommodity;
}

static bool MarketGoodForSale(MarketGoodInfo const& Entry) {
    return MarketGoodIsCommodity(Entry) &&
           (Entry.Type == TransactionBuyAndSell || Entry.Type == TransactionSaleOnly);
}

static bool MarketGoodInDemand(MarketGoodInfo const& Entry) {
    return MarketGoodIsCommodity(Entry) &&
           (Entry.Type == TransactionBuyAndSell || Entry.Type == TransactionDemandOnly);
}

// pub::Market::GetNumCommoditiesForSale -- retail 0x38710 (helper FUN_06d12d90).
SERVER_API int __cdecl GetNumCommoditiesForSale(unsigned int BaseId, int* OutCount) {
    *OutCount = 0;   // before the id test, with no pointer test
    if ((BaseId & 0x3fffffff) == 0) return -4;
    FBaseEntry* Entry = GetBaseDB()->find_base(BaseId);
    if (!Entry) return -4;
    BaseData* base = Entry->Data;
    int count = 0;
    for (std::map<unsigned int, MarketGoodInfo>::const_iterator it = base->Market.begin();
         it != base->Market.end(); ++it) {
        if (MarketGoodForSale(it->second))
            count++;
    }
    *OutCount = count;
    return 0;
}

// pub::Market::GetCommoditiesForSale -- retail 0x38750 (helper FUN_06d12e80). The cap is
// read from *InOutCount once; an over-cap good is skipped but the walk continues. No
// pointer tests.
SERVER_API int __cdecl GetCommoditiesForSale(unsigned int BaseId, unsigned int* const OutGoods, int* InOutCount) {
    if ((BaseId & 0x3fffffff) == 0) return -4;
    FBaseEntry* Entry = GetBaseDB()->find_base(BaseId);
    if (!Entry) return -4;
    BaseData* base = Entry->Data;
    int const max_count = *InOutCount;
    int count = 0;
    for (std::map<unsigned int, MarketGoodInfo>::const_iterator it = base->Market.begin();
         it != base->Market.end(); ++it) {
        if (!MarketGoodForSale(it->second))
            continue;
        if (count >= max_count)
            continue;
        OutGoods[count++] = it->second.GoodId;
    }
    *InOutCount = count;
    return 0;
}

// pub::Market::GetNumCommoditiesInDemand -- retail 0x38790 (helper FUN_06d12df0).
SERVER_API int __cdecl GetNumCommoditiesInDemand(unsigned int BaseId, int* OutCount) {
    *OutCount = 0;   // before the id test, with no pointer test
    if ((BaseId & 0x3fffffff) == 0) return -4;
    FBaseEntry* Entry = GetBaseDB()->find_base(BaseId);
    if (!Entry) return -4;
    BaseData* base = Entry->Data;
    int count = 0;
    for (std::map<unsigned int, MarketGoodInfo>::const_iterator it = base->Market.begin();
         it != base->Market.end(); ++it) {
        if (MarketGoodInDemand(it->second))
            count++;
    }
    *OutCount = count;
    return 0;
}

// pub::Market::GetCommoditiesInDemand -- retail 0x387d0 (helper FUN_06d12f40). As
// GetCommoditiesForSale.
SERVER_API int __cdecl GetCommoditiesInDemand(unsigned int BaseId, unsigned int* const OutGoods, int* InOutCount) {
    if ((BaseId & 0x3fffffff) == 0) return -4;
    FBaseEntry* Entry = GetBaseDB()->find_base(BaseId);
    if (!Entry) return -4;
    BaseData* base = Entry->Data;
    int const max_count = *InOutCount;
    int count = 0;
    for (std::map<unsigned int, MarketGoodInfo>::const_iterator it = base->Market.begin();
         it != base->Market.end(); ++it) {
        if (!MarketGoodInDemand(it->second))
            continue;
        if (count >= max_count)
            continue;
        OutGoods[count++] = it->second.GoodId;
    }
    *InOutCount = count;
    return 0;
}

} // namespace Market
} // namespace pub

