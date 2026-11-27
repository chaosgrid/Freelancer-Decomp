// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "../include/common_server.h"
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
#include "../include/server_cobject.h"
#include "../include/common_physys.h"
#include "../include/server_gameobject.h"
#include "../include/server_sysdb.h"
#include "../include/common_misc.h"
#include "../include/x86math.h"      // I3DMathEngine::MatrixToQuaternion (pub::Player::SetCamera)
#include "../include/server_createinfo.h" // Server_Acquire3DMathEngine (FUN_06d5ff70)
#include "../include/rpc_iclient.h"
#include "../include/rpc_iclient_args.h" // FPayloadServerSetDirective (pub::AI::SubmitDirective)
#include "../include/common_casteroid.h"
#include "server_internal.h"
#include <windows.h>
#include <cmath>
#include <cstring>     // std::memcpy (SpaceObj::Create EquipIdMaker seed)
#include <map>
#include <set>
#include <vector>
#include <cstdlib>     // rand, wcstombs

// The IClient the server talks back to (DAT_06d8fb14). Defined in IServerImpl.cpp and set
// by IServerImpl::Startup; several pub:: exports dispatch straight into its vtable.
extern IClient* g_pClientInterface;

namespace GlobalReputation = ::Reputation;

// Retail's Dock / InstantDock / Launch / FormationResponse / LaneResponse follow the
// lookup with __RTDynamicCast to Ship; here that is `dynamic_cast< ::Ship*>`.

// The two file-static trees of SpaceObj.cpp, written by pub::SpaceObj::Create, erased by
// pub::SpaceObj::Destroy, and referenced nowhere else in Server.dll:
//   DAT_06d8d834 -- the ship ids Create made.
//   DAT_06d8d820 -- ship id -> the ShipInfo's ControllerId; Create inserts {id, 0} and
//     then stores the id into the mapped dword.
// Retail uses VC6 _Tree (0x18-byte nodes); the port uses std containers (no retail ABI).
static std::set<unsigned int> GSpaceObjCreatedShips;                    // .data:DAT_06d8d834

static std::map<unsigned int, unsigned int> GSpaceObjShipControllers;   // .data:DAT_06d8d820

namespace pub {
namespace SpaceObj {

// SpaceObj::Create -- retail .text:06cf7c90 (ordinal 0x17c90, cdecl). Builds an
// FShipCreateRequest from the ShipInfo and hands it to StarSystem::create_ship
// (FUN_06d0a880, return value unused). No null test on the ship archetype or the loadout.
// The id is CFLIDMaker::AllocateObjectID inlined; a zero id leaves the result at -1.
SERVER_API int __cdecl Create(unsigned int& out_id, const SpaceObj::ShipInfo& info) {
    CFLIDMaker* const IdMaker = CFLIDMaker::GetInstance();
    if (IdMaker == nullptr) {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\pubSpace.cpp", 0x3f,
              "Failed to grab instance of global ID maker.\n");
        return -1;
    }
    int Result = -1;
    FStarSystem* const System = SysDB_LookupSystem(info.SystemID);
    if (System == nullptr) return Result;

    FShipCreateRequest Request;
    Request.EquipStateData = nullptr;
    Request.MarkPlayerId = 0u;
    Request.CostumeBody = info.CostumeBody;
    Request.CostumeHead = info.CostumeHead;
    Request.CostumeRightHand = info.CostumeRightHand;
    Request.CostumeLeftHand = info.CostumeLeftHand;
    Request.CostumeAccessoryCount = info.CostumeAccessoryCount;
    for (int Index = 0; Index < Request.CostumeAccessoryCount; ++Index)
        Request.CostumeAccessory[Index] = info.CostumeAccessory[Index];
    Request.ReputationRank = info.ReputationRank;
    Request.VoiceId = info.VoiceId;
    Request.VibeGroupId = info.VibeGroupId;
    Request.GroupNameId = info.GroupNameId;
    Request.ArchetypeNickname = info.ShipArchID;
    Request.Position = info.Position;
    Quaternion ShipOrient;
    Server_MatrixToQuat(ShipOrient, info.Orientation);
    Request.Orientation = ShipOrient;
    Request.Velocity = info.Velocity;
    Request.AngularVelocity = info.AngularVelocity;
    Request.DockingBaseId = info.DockingBaseId;
    Request.LoadoutId = info.LoadoutID;
    Request.Throttle = 0.0f;
    Request.OwnerPlayer = 0u;
    Request.HitPts = info.HitPts;
    Request.MarkPlayerId = info.MarkPlayerId;
    Archetype::Ship* const ShipArch = Archetype::GetShip(info.ShipArchID);
    ShipArch->get_undamaged_collision_group_list(Request.CollisionGroups);

    SubObjectID::EquipIdMaker EquipIds;
    if (info.LoadoutID != 0u) {
        Loadout::Map const* const LoadoutMap = Loadout::Get(info.LoadoutID);
        EquipIds = LoadoutMap->IdMaker;                                  // word store
    }
    SpaceObj_BuildTransportLoadout(ShipArch, &EquipIds, &info.Cargo, &Request.Equipment);
    Request.EquipIds = EquipIds;

    if (!Server_ControllerExists(info.ControllerId)) {
        Result = -4;
    } else {
        Request.ObjId = IdMaker->AllocateObjectID();
        System->create_ship(Request);
        unsigned int const Id = Request.ObjId;
        if ((Id & 0x3fffffffu) != 0) {
            out_id = Id;
            GSpaceObjCreatedShips.insert(out_id);
            Result = 0;
            std::pair<std::map<unsigned int, unsigned int>::iterator, bool> const Inserted =
                GSpaceObjShipControllers.insert(
                    std::pair<unsigned int const, unsigned int>(Id, 0u));
            Inserted.first->second = info.ControllerId;
        }
    }
    return Result;
}

// SpaceObj::CreateSolar -- retail .text:06cf80d0 (ordinal 0x180d0, cdecl). The system
// lookup fails before the request exists. The id is CreateID(nickname), written to out_id
// before the archetype is touched; no null test on the solar archetype.
// StarSystem::create_solar is FUN_06d0a200; a null Solar gives -1.
SERVER_API int __cdecl CreateSolar(unsigned int& out_id, const SpaceObj::SolarInfo& solar_info) {
    FStarSystem* const System = SysDB_LookupSystem(solar_info.SystemID);
    if (System == nullptr) return -4;

    CSolarCreateInfo Request;
    Request.NicknameSrc.clear();
    Request.MarkPlayerId = 0u;
    int Result;
    if (solar_info.Nickname[0] == '\0') {
        Result = -4;
    } else {
        unsigned int const Id = CreateID(solar_info.Nickname);
        out_id = Id;
        Request.NicknameSrc.Id = Id;
        Request.ObjId = Id;
        Request.HitPts = solar_info.HitPts;
        Request.DockingBaseId = solar_info.DockingBaseId;
        Request.ArchetypeNickname = solar_info.SolarArchID;
        Request.CostumeBody = solar_info.CostumeBody;
        Request.CostumeHead = solar_info.CostumeHead;
        Request.CostumeRightHand = solar_info.CostumeRightHand;
        Request.CostumeLeftHand = solar_info.CostumeLeftHand;
        Request.CostumeAccessoryCount = solar_info.CostumeAccessoryCount;
        for (int Index = 0; Index < Request.CostumeAccessoryCount; ++Index)
            Request.CostumeAccessory[Index] = solar_info.CostumeAccessory[Index];
        Request.VoiceId = solar_info.VoiceId;
        Request.VibeGroupId = solar_info.VibeGroupId;
        Request.Position = solar_info.Position;
        Quaternion SolarOrient;
        Server_MatrixToQuat(SolarOrient, solar_info.Orientation);
        Request.Orientation = SolarOrient;
        Request.LoadoutId = solar_info.LoadoutID;
        Archetype::Solar* const SolarArch = Archetype::GetSolar(solar_info.SolarArchID);
        SolarArch->get_undamaged_collision_group_list(Request.CollisionGroups);
        Request.MarkPlayerId = solar_info.MarkPlayerId;
        Request.VisitValue = solar_info.VisitValue;
        Request.bDynamic = 1u;
        Request.bDestructible = 1u;
        ::Solar* const Created = System->create_solar(Request);
        Result = (Created != nullptr) ? 0 : -1;
    }
    return Result;
}

// SpaceObj::CreateLoot -- retail .text:06cf84a0 (cdecl). One 0x6c-byte FLootRecord built
// on the stack, handed to StarSystem::create_loot (FUN_06d08a40) in mode 4 with
// LootInfo+0x0c as the source object.
SERVER_API int __cdecl CreateLoot(unsigned int& out_id, const SpaceObj::LootInfo& loot_info) {
    StarSystem* const Sys = SysDB_LookupSystem(loot_info.SystemID);
    if (Sys == nullptr) return -4;

    FLootRecord Record = {};
    Record.bCanAiTractor = 1;
    Record.bMission = 0;

    // out_id is written before anything else is read. No null test on the id maker.
    unsigned int const LootId = CFLIDMaker::GetInstance()->AllocateObjectID();
    out_id = LootId;
    Record.Id = LootId;

    // No null test on the contents archetype.
    Archetype::Equipment* const Contents = Archetype::GetEquipment(loot_info.EquipArchID);
    Record.ContentsArch = Contents;
    Record.ContentsHitPts = Contents->HitPts;
    Record.Count = static_cast<int>(loot_info.Units);

    // The crate is the contents' loot appearance (Equipment vt+0x1c).
    Archetype::Equipment* const Appearance = Contents->get_loot_appearance();
    Record.Arch = Appearance;
    Record.HitPts = static_cast<float>(static_cast<double>(Appearance->HitPts) *
                                       static_cast<double>(loot_info.HitPtsScale));

    Record.NameId = loot_info.NameId;                   // record +0x1c
    Record.Velocity = loot_info.Velocity;               // record +0x20
    Record.Position = loot_info.Position;               // record +0x2c
    Record.Orientation = loot_info.Orientation;         // record +0x38
    Record.AngularVelocity = loot_info.AngularVelocity; // record +0x5c
    Record.bCanAiTractor = loot_info.bCanAiTractor;     // record +0x68
    Record.bMission = loot_info.bMission;               // record +0x69
    Record.MarkForPlayer = loot_info.MarkForPlayer;     // record +0x18

    Sys->create_loot(loot_info.OwnerObjId, 4, &Record, 1);
    return 0;
}

// SpaceObj::Destroy -- retail .text:06cf85f0 (ordinal 0x185f0, cdecl). An in-transit
// object (no system) goes to Server_DestroyInTransit (FUN_06d00410) and still returns -2.
// Both tree erases run on both paths.
SERVER_API int __cdecl Destroy(unsigned int obj_id, DestroyType destroy_type) {
    if ((obj_id & 0x3fffffffu) == 0) return -4;
    int Result = -2;
    GameObject* Obj = nullptr;
    StarSystem* System = nullptr;
    if (!Server_FindLiveObject(obj_id, Obj, System))                     // FUN_06d00630
        return Result;

    if (System != nullptr) {
        Obj->destroy(static_cast<int>(destroy_type), 0);
        Result = 0;
    } else {
        Server_DestroyInTransit(Obj);
    }

    GSpaceObjCreatedShips.erase(obj_id);
    GSpaceObjShipControllers.erase(obj_id);
    return Result;
}

// SpaceObj::SetInvincible -- retail .text:06cf8700 (ordinal 419, cdecl). obj_id is tested
// against 0 as a whole dword, not masked. An in-transit object (null system) returns -2.
// Deviation: retail never writes the two pad bytes (+6/+7); the port zeroes them.
SERVER_API int __cdecl SetInvincible(unsigned int obj_id, bool invincible, bool param3, float duration) {
    if (obj_id == 0) return -4;
    GameObject* Obj = nullptr;
    StarSystem* System = nullptr;
    if (!Server_FindLiveObject(obj_id, Obj, System)) return -2;
    if (System == nullptr) return -2;

    FInvincibility Cmd;
    Cmd.Type = 3;
    Cmd.Pad06[0] = 0;
    Cmd.Pad06[1] = 0;
    if (invincible) {
        Cmd.bProtect = true;
        Cmd.bFromPlayers = !param3;
        Cmd.MaxHpLoss = duration;
    } else {
        Cmd.bProtect = false;
        Cmd.bFromPlayers = false;
        Cmd.MaxHpLoss = 1.0f;
    }
    Obj->set_invincible(Cmd);                                            // vtable +0x194
    return 0;
}

// SpaceObj::SetInvincible2 -- retail .text:06cf87f0 (ordinal 418, cdecl). As SetInvincible
// but with no branch and param3 not inverted. Pad bytes zeroed by the port (never written
// in retail).
SERVER_API int __cdecl SetInvincible2(unsigned int obj_id, unsigned char invincible, unsigned char param3, float duration) {
    if (obj_id == 0) return -4;
    GameObject* Obj = nullptr;
    StarSystem* System = nullptr;
    if (!Server_FindLiveObject(obj_id, Obj, System)) return -2;
    if (System == nullptr) return -2;

    FInvincibility Cmd;
    Cmd.Type = 3;
    Cmd.bProtect = invincible;
    Cmd.bFromPlayers = param3;
    Cmd.Pad06[0] = 0;
    Cmd.Pad06[1] = 0;
    Cmd.MaxHpLoss = duration;
    Obj->set_invincible(Cmd);
    return 0;
}

// SpaceObj::ExistsAndAlive -- retail .text:06cf8870. The liveness half of FUN_06d00670 is the
// point; a plain lookup hit is not enough.
SERVER_API int __cdecl ExistsAndAlive(unsigned int obj_id) {
    if ((obj_id & 0x3fffffff) == 0) return -2;
    GameObject* obj = Server_LookupLiveObject(obj_id);
    if (!obj) return -2;
    return 0;
}

// SpaceObj::GetInvincible -- retail .text:06cf88a0. Live lookup; the id test is unmasked.
SERVER_API int __cdecl GetInvincible(unsigned int ObjId, unsigned char& OutFromPlayers,
                                     unsigned char& OutProtect, float& OutMaxHpLoss) {
    if (ObjId == 0) return -4;
    GameObject* Obj = nullptr;
    StarSystem* System = nullptr;
    if (!Server_FindLiveObject(ObjId, Obj, System)) return -2;
    if (System == nullptr) return -2;
    OutFromPlayers = Obj->get_invincible_flag();  // +0x198 -> raw byte +0x39
    OutProtect = Obj->is_invincible();            // +0x19c -> raw byte +0x38
    OutMaxHpLoss = Obj->get_max_hp_loss();   // +0x1a0 -> float +0x3c
    return 0;
}

// SpaceObj::DrainShields -- retail .text:06cf8930 (0x18930). A stack DamageList with
// Cause 0x17 (DamageCause::DrainShields), filled by EqObj::drain_shields
// (vtable +0x1d8: a -1.0f entry per shield) and applied by apply_damage_list (+0x188) on
// the object the lookup returned, without re-narrowing after the class test.
SERVER_API int __cdecl DrainShields(unsigned int obj_id) {
    if ((obj_id & 0x3fffffffu) == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id);
    if (Obj == nullptr) return -2;
    CObject* const Cobj = Obj->Object;
    if (Cobj == nullptr ||
        (static_cast<unsigned int>(Cobj->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj) return -1;
    DamageList Damage;
    Damage.Cause = DamageCause::DrainShields;
    static_cast< ::EqObj*>(Obj)->drain_shields(Damage);
    Obj->apply_damage_list(Damage);
    return 0;
}

// SpaceObj::GetLocation -- retail .text:06cf8a80 (FUN_06d00670 lookup). The orientation
// is the Rotation of get_transform (IObjInspect slot 3; slots 3 and 4 are ICF-folded in
// retail).
SERVER_API int __cdecl GetLocation(unsigned int obj_id, Vector& out_pos, Matrix& out_orient) {
    out_pos.x = 0.0f; out_pos.y = 0.0f; out_pos.z = 0.0f;
    if ((obj_id & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupLiveObject(obj_id);
    if (!obj) return -2;
    out_pos = obj->get_position();
    out_orient = obj->get_transform().Rotation;
    return 0;
}

// SpaceObj::GetMotion -- retail 0x18b20 (FUN_06d00670 lookup), IObjInspect slots 1 and 2.
SERVER_API int __cdecl GetMotion(unsigned int obj_id, Vector& out_linear, Vector& out_angular) {

    out_linear.x = 0; out_linear.y = 0; out_linear.z = 0;
    out_angular.x = 0; out_angular.y = 0; out_angular.z = 0;
    if ((obj_id & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupLiveObject(obj_id);
    if (!obj) return -2;
    out_linear = obj->get_velocity();
    out_angular = obj->get_angular_velocity();
    return 0;
}

// SpaceObj::GetRep -- retail 0x18ba0 (FUN_06d00670 lookup). CEqObj only
// ((ObjectClass & 0x103) == 0x103), vibe at +0x104. out_rep is written only on success;
// Content callers preset it (often -1) and rely on that.
SERVER_API int __cdecl GetRep(unsigned int obj_id, int& out_rep) {
    if ((obj_id & 0x3fffffff) == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id);
    if (Obj == nullptr) return -2;
    CObject* const Raw = Obj->Object;
    if (Raw == nullptr ||
        (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj)
        return -1;
    out_rep = (int)static_cast<CEqObj*>(Raw)->VibeId;
    return 0;
}

// SpaceObj::GetSolarRep -- retail 0x18c00. Echoes the vibe back when Vibe::Verify passes.
SERVER_API int __cdecl GetSolarRep(unsigned int vibe_id, int& out_rep) {
    bool valid = GlobalReputation::Vibe::Verify(vibe_id);
    if (valid) {
        out_rep = (int)vibe_id;
        return 0;
    }
    return -2;
}

// SpaceObj::GetSolarArchetypeID -- retail 0x18c30 (FUN_06d00670 lookup). CEqObj only; the
// id is the archetype's (CObject+0x88) Root::Nickname.Id; the archetype is not null-tested.
SERVER_API int __cdecl GetSolarArchetypeID(unsigned int obj_id, unsigned int& out_arch_id) {
    out_arch_id = 0;
    if ((obj_id & 0x3fffffff) == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id);
    if (Obj == nullptr) return -2;
    CObject* const Raw = Obj->Object;
    if (Raw == nullptr ||
        (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj)
        return -1;
    out_arch_id = Raw->Archetype->get_id();
    return 0;
}

// SpaceObj::GetSolarArchetypeNickname -- retail 0x18ca0: Archetype::GetSolar, the
// Root::Nickname (+8) string, safe_strcpy.
SERVER_API int __cdecl GetSolarArchetypeNickname(char* out_name, int max_len, unsigned int arch_id) {
    if (!out_name || max_len <= 0) return -4;
    Archetype::Solar* solar = Archetype::GetSolar(arch_id);
    if (!solar) return -2;
    const char* nick = solar->Nickname.get_string();
    if (!nick) return -2;
    int len = 0;
    while (nick[len] != '\0') len++;
    if (len < max_len) {
        for (int i = 0; i <= len; i++) out_name[i] = nick[i];
        return 0;
    }
    return -1;
}

// SpaceObj::GetSolarParent -- retail 0x18d00 (FUN_06d00670 lookup). Solars only
// ((ObjectClass & 0x303) == 0x303 on IObjInspect::cobject, +0x150); the id test is
// unmasked. The parent nickname id is the object id, so a second live lookup doubles as
// the existence check.
SERVER_API int __cdecl GetSolarParent(const unsigned int& obj_id_ref, unsigned int& out_parent_id) {
    out_parent_id = 0;
    if (obj_id_ref == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id_ref);
    if (Obj == nullptr) return -2;
    CObject const* const Raw = Obj->cobject();
    if (Raw == nullptr ||
        (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassSolar) != CObject::ClassSolar)
        return -1;
    CSolar const* const Cs = static_cast<CSolar const*>(Raw);
    ID_String const& Parent = Cs->GetParentNickname();
    if (Parent.IsEmpty()) return -3;
    GameObject* const ParentObj = Server_LookupLiveObject(Parent.Id);
    if (ParentObj == nullptr) return -3;
    out_parent_id = ParentObj->get_id();
    return 0;
}

// SpaceObj::GetType -- retail 0x18da0. out_type is zeroed before the id test, so every
// failure path returns it zeroed.
SERVER_API int __cdecl GetType(unsigned int obj_id, unsigned int& out_type) {
    out_type = 0;
    if ((obj_id & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupLiveObject(obj_id);
    if (!obj) return -2;
    // csimple() is retail's (ObjectClass & 3) == 3 guard; TypeBits is CSimple+0xe0.
    CSimple* Cs = obj->csimple();
    if (Cs == nullptr) return 0;
    out_type = Cs->get_type();
    return 0;
}

// SpaceObj::GetSystem -- retail .text:06cf8e00 (cdecl). The id is the system's
// (FUN_06d06cd0, SystemInfo vtable slot 0); an in-transit object reports 0 and returns 0.
SERVER_API int __cdecl GetSystem(unsigned int obj_id, unsigned int& out_system_id) {
    out_system_id = 0;
    if ((obj_id & 0x3fffffffu) == 0) return -4;
    GameObject* Obj = nullptr;
    StarSystem* System = nullptr;
    if (!Server_FindLiveObject(obj_id, Obj, System)) return -2;
    if (System == nullptr) {
        out_system_id = 0;
        return 0;
    }
    out_system_id = System->SystemInfo->NicknameId;
    return 0;
}

// SpaceObj::GetHardpoint -- retail 0x18e70 (FUN_06d00670 lookup). get_index (IObjInspect
// slot 7) is the engine instance handle passed to HardpointPosition.
SERVER_API int __cdecl GetHardpoint(const unsigned int& obj_id_ref, const char* hp_name,
                                     Vector* out_pos, Matrix* out_orient) {
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupLiveObject(obj_id_ref);
    if (!obj) return -2;
    bool bOk = HardpointPosition(obj->get_index(), hp_name, out_pos, out_orient);
    return bOk ? 0 : -3;
}

// SpaceObj::GetRadius -- retail 0x18ed0 (FUN_06d00670 lookup). One IObjInspect slot 11
// (+0x2c) call fills radius and centre.
SERVER_API int __cdecl GetRadius(const unsigned int& obj_id_ref, float& out_radius, Vector& out_center) {
    out_radius = 0.0f;
    out_center.x = 0; out_center.y = 0; out_center.z = 0;
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupLiveObject(obj_id_ref);
    if (!obj) return -2;
    obj->get_physical_radius(out_radius, out_center);
    return 0;
}

// SpaceObj::GetBurnRadius -- retail 0x18f60 (FUN_06d00670 lookup). "Burn radius" is
// IObjInspect slot 80 (+0x140), get_atmosphere_range.
SERVER_API int __cdecl GetBurnRadius(const unsigned int& obj_id_ref, float& out_radius, Vector& out_center) {
    out_radius = 0.0f;
    out_center.x = 0; out_center.y = 0; out_center.z = 0;
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupLiveObject(obj_id_ref);
    if (!obj) return -2;
    out_center = obj->get_position();
    obj->get_atmosphere_range(out_radius);
    return 0;
}

// SpaceObj::GetMass -- retail 0x19000 (FUN_06d00670 lookup), IObjInspect slot 12.
SERVER_API int __cdecl GetMass(const unsigned int& obj_id_ref, float& out_mass) {
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupLiveObject(obj_id_ref);
    if (!obj) return -2;
    out_mass = obj->get_mass();
    return 0;
}

// SpaceObj::GetCenterOfMass -- retail 0x19040 (FUN_06d00670 lookup). The +0x68 slot is on
// the CObject's own vtable (CObject::get_center_of_mass), not IObjInspect's.
SERVER_API int __cdecl GetCenterOfMass(const unsigned int& obj_id_ref, Vector& out_center) {

    out_center.x = 0; out_center.y = 0; out_center.z = 0;
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupLiveObject(obj_id_ref);
    if (!obj) return -2;
    // Deviation: retail calls through the CObject even when the (& 3) check zeroed it
    // (null deref); the port returns -2 instead.
    CObject* Cobj = obj->cobj();
    if (Cobj == nullptr) return -2;
    out_center = Cobj->get_center_of_mass();
    return 0;
}

// SpaceObj::AddImpulse -- retail 0x190b0. The PhySys call is gated on the physics body
// (CObject+0x54); returns 0 either way.
SERVER_API int __cdecl AddImpulse(const unsigned int& obj_id_ref, const Vector& impulse, const Vector& point) {
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupLiveObject(obj_id_ref);
    if (!obj) return -2;
    CObject* Cobj = obj->cobj();
    if (Cobj == nullptr) return 0;
    if (Cobj->BodyPtr != nullptr) {
        PhySys::AddImpulseAtPoint(Cobj, impulse, point);
    }
    return 0;
}

// SpaceObj::GetTarget -- retail .text:06cf9120 (FUN_06d00670 lookup). Ships only
// ((ObjectClass & 0x503) == 0x503); CShip::get_target, then the target's get_id (+0x20).
// Deviation: retail calls CShip::get_target on the zeroed narrowing for a non-ship (null
// deref at [0x220]); the port returns -1 instead.
SERVER_API int __cdecl GetTarget(const unsigned int& obj_id_ref, unsigned int& out_target_id) {
    out_target_id = 0;
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id_ref);
    if (Obj == nullptr) return -2;
    CObject* const Raw = Obj->Object;
    if (Raw == nullptr ||
        (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
        return -1;
    IObjRW* const Target = static_cast<CShip*>(Raw)->get_target();
    if (Target == nullptr) {
        out_target_id = 0;
        return 0;
    }
    out_target_id = Target->get_id();
    return 0;
}

// SpaceObj::GetVoiceID -- retail .text:06cf91a0. The id test is unmasked. A plain lookup
// (FUN_06d006c0) miss is -2; then FUN_06d00670, whose reject of a destroyed object is -3;
// a non-CEqObj is -4 (EDI keeps its initial 0xfffffffc, .text:06cf91bf). Voice at +0x13c.
SERVER_API int __cdecl GetVoiceID(const unsigned int& obj_id_ref, unsigned int& out_voice_id) {
    if (obj_id_ref == 0) return -4;
    GameObject* Found = nullptr;
    StarSystem* System = nullptr;
    if (!Server_FindObject(obj_id_ref, Found, System) || Found == nullptr) return -2;
    GameObject* const Obj = Server_LookupLiveObject(obj_id_ref);
    if (Obj == nullptr) return -3;
    CObject* const Raw = Obj->Object;
    if (Raw == nullptr ||
        (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj)
        return -4;
    out_voice_id = static_cast<CEqObj*>(Raw)->VoiceId;    // CEqObj+0x13c
    return 0;
}

// SpaceObj::SendComm -- retail .text:06cf9230 (ordinal 401, cdecl). The system comes from
// the target, else from the sender (it is the system that is kept and tested, not the
// object). A CEqObj sender supplies the default voice (+0x13c) and costume (+0x108), and
// whenever that branch is taken caption_ids is zeroed. send_comm is FUN_06d0db30.
SERVER_API int __cdecl SendComm(unsigned int sender_id, unsigned int target_id,
                                 unsigned int voice_id, const Costume* costume,
                                 unsigned int caption_ids, unsigned int* text_ids,
                                 int text_count, unsigned int permutation_ids,
                                 float duration, bool interrupt) {
    if (text_ids == nullptr) return -4;
    if (text_count <= 0) return -4;                                       // signed

    GameObject* LookupObj = nullptr;
    StarSystem* System = nullptr;
    if ((target_id & 0x3fffffffu) != 0) {
        if (!Server_FindObject(target_id, LookupObj, System))
            System = nullptr;
    }
    if (System == nullptr) {
        if ((sender_id & 0x3fffffffu) == 0) return -4;
        if (!Server_FindObject(sender_id, LookupObj, System))
            System = nullptr;
        if (System == nullptr) return -4;
    }

    unsigned int Voice = voice_id;
    const Costume* Cost = costume;
    unsigned int CaptionIds = caption_ids;
    if ((sender_id & 0x3fffffffu) != 0) {
        CBase* const SenderBase = Server_LookupLiveObject(sender_id);
        if (SenderBase != nullptr) {
            CObject* const Cobj = SenderBase->Object;
            if (Cobj != nullptr &&
                (static_cast<unsigned int>(Cobj->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj) {
                CEqObj* const Eq = static_cast<CEqObj*>(Cobj);
                if (Voice == 0)
                    Voice = Eq->VoiceId;
                if (costume == nullptr)
                    Cost = &Eq->SpaceCostume;
                CaptionIds = 0;
            }
        }
    }
    if (Cost == nullptr) return -2;

    System->send_comm(sender_id, target_id, Voice, *Cost, CaptionIds, text_ids, text_count,
                      permutation_ids, duration, interrupt);
    return 0;
}

// SpaceObj::JettisonEquipment -- retail 0x19390. No id guard: a failed lookup returns -2.
// IObjAffect slot 14 (jettison_cargo); 0 on success, -3 when it reports -2.
SERVER_API int __cdecl JettisonEquipment(const unsigned int& obj_id,
                                          const unsigned short& equip_id, const int& quantity) {
    GameObject* obj = Server_LookupGameObject(obj_id);
    if (!obj) return -2;
    int const Result = obj->jettison_cargo(equip_id, quantity, 0);
    return (Result != -2) ? 0 : -3;
}

// Retail SpaceObj::Dock calls SpaceObj::ActivateEquipment, which is defined further
// down this file; forward-declare it so the call order matches retail.
SERVER_API EQUIPMENT_RTYPE __cdecl ActivateEquipment(const unsigned int& obj_id_ref, EQInfo* eq_info);

// SpaceObj::ActivateEquipment -- retail .text:06cf93f0 (ordinal 0x193f0, cdecl); switch
// table at .text:06cf95d8. Type 0 = cruise (IObjAffect +0x50), 1 = scan cargo
// (IObjInspect +0x120), 2 = the first cloak (FindFirst(0x1000)), 3 = every docking light
// (CELightEquip arch +0x71). The EQUIPMENT_RTYPE names are the port's; the values
// (-5/-2/-1/0) are retail's.
SERVER_API EQUIPMENT_RTYPE __cdecl ActivateEquipment(const unsigned int& obj_id_ref, EQInfo* eq_info) {
    if ((obj_id_ref & 0x3fffffffu) == 0) return EQUIPMENT_RTYPE_BAD_TYPE;
    GameObject* Obj = nullptr;
    StarSystem* System = nullptr;
    if (!Server_FindObject(obj_id_ref, Obj, System))                     // system out unused
        return EQUIPMENT_RTYPE_INVALID;
    if (Obj == nullptr) return EQUIPMENT_RTYPE_INVALID;

    switch (eq_info->Type) {                                             // unsigned > 3 -> -1
    case EQINFO_TYPE_CRUISE:
        if (eq_info->Count != 1) return EQUIPMENT_RTYPE_INVALID;
        return Obj->toggle_cruise(true, true, 0) != 0 ? EQUIPMENT_RTYPE_BAD_TYPE
                                                 : EQUIPMENT_RTYPE_OK;
    case EQINFO_TYPE_SCAN_CARGO: {
        if (eq_info->TargetObjId == 0) return EQUIPMENT_RTYPE_INVALID;
        GameObject* const Other = Server_LookupLiveObject(eq_info->TargetObjId);
        if (Other == nullptr) return EQUIPMENT_RTYPE_BAD_TYPE;
        EquipDescVector& Cargo = *reinterpret_cast<EquipDescVector*>(eq_info->CargoOut);
        return Obj->scan_cargo(Other, Cargo) != 0 ? EQUIPMENT_RTYPE_BAD_TYPE
                                                   : EQUIPMENT_RTYPE_OK;
    }
    case EQINFO_TYPE_CLOAK: {
        CObject* const Raw = Obj->Object;
        if (Raw == nullptr ||
            (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj)
            return EQUIPMENT_RTYPE_INVALID;
        CEqObj* const Ceq = static_cast<CEqObj*>(Raw);
        CEquip const* const Cloak = Ceq->EquipManager.FindFirst(EquipTypeCloak);
        if (Cloak == nullptr) return EQUIPMENT_RTYPE_NOT_FOUND;
        unsigned short const SubObjId = Cloak->Id;
        Obj->toggle_item(&SubObjId, static_cast<unsigned char>(eq_info->EquipId) != 0, 0);
        return EQUIPMENT_RTYPE_OK;
    }
    case EQINFO_TYPE_DOCKING_LIGHTS: {
        CObject* const Raw = Obj->Object;
        if (Raw == nullptr ||
            (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj)
            return EQUIPMENT_RTYPE_INVALID;
        CEqObj* const Ceq = static_cast<CEqObj*>(Raw);
        CEquipTraverser Walk(EquipTypeLight);
        CEquip* Light = Ceq->EquipManager.Traverse(Walk);
        while (Light != nullptr) {
            if (static_cast<CELightEquip*>(Light)->LightArch()->bDockingLight) {
                unsigned short const SubObjId = Light->Id;
                Obj->toggle_item(&SubObjId, static_cast<unsigned char>(eq_info->EquipId) != 0, 0);
            }
            Light = Ceq->EquipManager.Traverse(Walk);
        }
        return EQUIPMENT_RTYPE_OK;
    }
    default:
        return EQUIPMENT_RTYPE_INVALID;
    }
}

// SpaceObj::EnumerateCargo -- retail .text:06cf95f0. A lookup miss and a non-CEqObj both
// return -2.
SERVER_API int __cdecl EnumerateCargo(const unsigned int& obj_id, CargoEnumerator& enumerator) {
    if ((obj_id & 0x3fffffff) == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id);
    if (Obj == nullptr) return -2;
    CObject* const Raw = Obj->Object;
    if (Raw == nullptr ||
        (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj)
        return -2;
    CEqObj* const Ceq = static_cast<CEqObj*>(Raw);

    // notify_cargo is the enumerator's own vtable slot 0, fed straight from the CECargo:
    // its archetype id (+0x0c -> +0x08), Count (+0x28) and Id (+0x08). An item whose
    // Arch2Good is 0 is skipped, and the walk never stops early (the slot returns void).
    CEquipTraverser Walk(EquipTypeCargo);
    CEquip* Item = Ceq->EquipManager.Traverse(Walk);
    while (Item != nullptr) {
        unsigned int const good_id = Arch2Good(Item->Arch->get_id());
        if (good_id != 0) {
            enumerator.notify_cargo(good_id, static_cast<CECargo*>(Item)->Count, Item->Id);
        }
        Item = Ceq->EquipManager.Traverse(Walk);
    }
    return 0;
}

// SpaceObj::GetCargoSpaceOccupied -- retail .text:06cf96d0 (0x196d0). HoldSize (+0x100)
// minus get_cargo_hold_remaining, unrounded, then ftol (the decompile hides the FSUBR).
// Retail does not null-test the CShip narrowing before reading its archetype; nor does
// the port.
SERVER_API int __cdecl GetCargoSpaceOccupied(const unsigned int& obj_id_ref, unsigned int& out_occupied) {
    out_occupied = 0;
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id_ref);
    if (Obj == nullptr) return -2;
    CObject* const Raw = Obj->cobj();
    CShip* const ShipObj =
        (Raw != nullptr && ((unsigned int)Raw->ObjectClass & CObject::ClassShip) == CObject::ClassShip) ? static_cast<CShip*>(Raw) : nullptr;
    Archetype::Ship const* const Arch = (Archetype::Ship const*)ShipObj->Archetype;
    double const Remaining = ShipObj->get_cargo_hold_remaining();
    out_occupied = (unsigned int)(long long)(Arch->HoldSize - Remaining);
    return 0;
}

// SpaceObj::GetScannerRange -- ships only ((ObjectClass & 0x503) == 0x503). No scanner
// returns -2 like a failed lookup; a destroyed scanner returns -3; outs untouched on both.
SERVER_API int __cdecl GetScannerRange(unsigned int obj_id, int& radar_range, int& cargo_scan_range) {
    if ((obj_id & 0x3fffffff) == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id);
    if (Obj == nullptr) return -2;
    CObject* const Raw = Obj->Object;
    if (Raw == nullptr ||
        (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
        return -1;
    CShip* const Cs = static_cast<CShip*>(Raw);
    CEScanner const* const Scanner =
        static_cast<CEScanner const*>(Cs->EquipManager.FindFirst(EquipTypeScanner));
    if (Scanner == nullptr) return -2;
    // Scanner vtable +0x08 is CEquip::IsDestroyed (Common 0x06286d10), not IsFunctioning.
    if (Scanner->IsDestroyed()) return -3;
    radar_range = (int)(long)Scanner->GetRadarRange();
    cargo_scan_range = (int)(long)Scanner->GetCargoScanRange();
    return 0;
}

// SpaceObj::GetAtmosphereRange -- retail 0x19800. Planets and suns only
// ((ObjectClass & 0x303) == 0x303).
SERVER_API int __cdecl GetAtmosphereRange(const unsigned int& obj_id, float& out_range) {
    if ((obj_id & 0x3fffffff) == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id);
    if (Obj == nullptr) return -2;
    CObject* const Raw = Obj->Object;
    if (Raw == nullptr ||
        (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassSolar) != CObject::ClassSolar) {
        out_range = 0.0f;
        return 0;
    }
    CSolar* const Cs = static_cast<CSolar*>(Raw);
    float atmo = Cs->AtmosphereRange;   // CSolar+0x1ac
    out_range = atmo;
    // A positive range is reported minus the object's radius (EngineObject+0x38).
    if (atmo > 0.0f) {
        out_range = atmo - Cs->Radius;
    }
    return 0;
}

// SpaceObj::GetDockingTarget -- retail 0x19880 (FUN_06d00670 lookup). CEqObj only; prefers
// DockingPointBaseId (+0x164) and falls back to DockingBaseId (+0x160).
SERVER_API int __cdecl GetDockingTarget(const unsigned int& obj_id_ref, unsigned int& out_target) {
    out_target = 0;
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id_ref);
    if (Obj == nullptr) return -2;
    CObject* const Raw = Obj->Object;
    if (Raw == nullptr ||
        (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj) {
        out_target = 0;
        return 0;
    }
    CEqObj* const Ceq = static_cast<CEqObj*>(Raw);
    unsigned int secondary = Ceq->DockingPointBaseId;
    if ((secondary & 0x3fffffff) != 0) {
        out_target = secondary;
    } else {
        out_target = Ceq->DockingBaseId;
    }
    return 0;
}

// SpaceObj::GetTerminalInfo -- retail .text:06cf9900 (cdecl). A zero id returns -2, not
// -4. The archetype (+0x88) is not null-tested. Retail's null-begin and index tests on the
// docking-sphere vector collapse to one size test. Name is the sphere's hardpoint string
// (+0x04), Value its type (+0x0c).
SERVER_API int __cdecl GetTerminalInfo(const unsigned int& obj_id_ref, int index, TerminalInfo& out_info) {
    if ((obj_id_ref & 0x3fffffffu) == 0) return -2;
    GameObject* const Obj = Server_LookupLiveObject(obj_id_ref);
    if (Obj == nullptr) return -2;
    CObject* const Cobj = Obj->Object;
    if (Cobj == nullptr ||
        (static_cast<unsigned int>(Cobj->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj) return -2;
    Archetype::EqObj const* const Arch =
        static_cast<Archetype::EqObj const*>(Cobj->Archetype);
    if (static_cast<unsigned int>(index) >= Arch->DockingSpheres.size()) return -3;
    Archetype::EqObj::DockingSphere const& Sphere = Arch->DockingSpheres[index];
    safe_strcpy(out_info.Name, 0x20, Sphere.HardpointName.c_str());
    out_info.Value = Sphere.Type;
    return 0;
}

// SpaceObj::GetGoodID -- retail 0x199a0. Loot only ((ObjectClass & 0x803) == 0x803); the
// good comes from CLoot::ContentsArch (CObject+0xec), not null-tested. A zero id returns -2.
SERVER_API int __cdecl GetGoodID(const unsigned int& obj_id_ref, unsigned int& out_good_id) {
    if ((obj_id_ref & 0x3fffffff) == 0) return -2;
    GameObject* const Obj = Server_LookupLiveObject(obj_id_ref);
    if (Obj == nullptr) return -2;
    CObject* const Raw = Obj->Object;
    if (Raw == nullptr ||
        (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassLoot) != CObject::ClassLoot)
        return -1;
    CLoot* const Cl = static_cast<CLoot*>(Raw);
    out_good_id = Arch2Good(Cl->ContentsArch->get_id());
    return 0;
}

// SpaceObj::GetArchetypeID -- Good2Arch with no entry guard; the out-param is written on
// failure too.
SERVER_API int __cdecl GetArchetypeID(const unsigned int& good_id, unsigned int& out_arch_id) {
    unsigned int arch = Good2Arch(good_id);
    out_arch_id = arch;
    return (arch != 0) ? 0 : -2;
}

// SpaceObj::GetJumpTarget -- retail 0x19a40. Solars only ((ObjectClass & 0x303) == 0x303)
// whose TypeBits (+0xe0) lie within 0xc40 (jump gate/hole).
SERVER_API int __cdecl GetJumpTarget(const unsigned int& obj_id_ref, unsigned int& out_target1, unsigned int& out_target2) {
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id_ref);
    if (Obj == nullptr) return -2;
    CObject* const Raw = Obj->Object;
    if (Raw == nullptr ||
        (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassSolar) != CObject::ClassSolar)
        return -1;
    CSolar* const Cs = static_cast<CSolar*>(Raw);
    unsigned int otype = Cs->get_type();
    if ((otype & (OBJ_JUMP_GATE | OBJ_AIRLOCK_GATE | OBJ_JUMP_HOLE)) != otype) return -1;
    out_target1 = Cs->DestSystem;    // CSolar+0x1b4
    out_target2 = Cs->DestGate.Id;   // CSolar+0x1b8 (ID_String)
    return 0;
}

// SpaceObj::Activate -- retail .text:06cf9ac0 (cdecl). Deactivating an object whose type
// has 0x400 set is a no-op returning 0. The owner system is used without a null test.
SERVER_API int __cdecl Activate(const unsigned int& obj_id, bool activate, int param) {
    if ((obj_id & 0x3fffffffu) == 0) return -4;
    GameObject* Obj = nullptr;
    StarSystem* Owner = nullptr;
    if (!Server_FindLiveObject(obj_id, Obj, Owner)) return -2;
    unsigned int Flags;
    Obj->get_type(Flags);
    if (!activate && (Flags & OBJ_AIRLOCK_GATE) != 0) return 0;
    return Owner->activate_object(Obj, activate, static_cast<unsigned int>(param)) ? 0 : -3; // FUN_06d0d8a0
}

// SpaceObj::RequestSpaceScript -- retail 0x19b40. One 0x118-byte
// FServerRequestSpaceScriptInfo (rpc_iclient_args.h) handed to
// IClient::SendServerRequestSpaceScript (+0xf8) for client_id (Content's Act_CallThorn
// passes the mission's player id). Neither the client interface nor the name is tested.
SERVER_API int __cdecl RequestSpaceScript(const unsigned int& obj_id_ref, const Vector& pos,
                                          const int& camera_mode, unsigned int client_id, const char* script_name) {
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    FServerRequestSpaceScriptInfo Payload;
    Payload.ObjectId = obj_id_ref;
    memcpy(&Payload.PosXBits, &pos.x, sizeof(float));
    memcpy(&Payload.PosYBits, &pos.y, sizeof(float));
    memcpy(&Payload.PosZBits, &pos.z, sizeof(float));
    Payload.CameraMode = static_cast<unsigned int>(camera_mode);
    safe_strcpy(Payload.Name, static_cast<int>(kRequestSpaceScriptNameSize), script_name);
    ::g_pClientInterface->SendServerRequestSpaceScript(client_id, Payload);
    return 0;
}

// SpaceObj::GetEmptyPos -- retail .text:06cf9bd0 (ordinal 0x19bd0, cdecl). Negative or
// NaN radius/clearance -> -4. Each outer pass scans up to 0x400 objects within R (falling
// back to a scan with radius -1.0f and type mask 2, the planets, when nothing is found), then tries up to 10
// random offsets of radius R placed by selection_type; an offset collides when
// (distance - object radius) - clearance <= 0. After 10 collisions R doubles and the
// outer pass repeats. An invalid selection_type returns the current result (-4 on the
// first pass). x87 arithmetic is reproduced in double (53-bit precision control).
SERVER_API int __cdecl GetEmptyPos(const unsigned int& system_id_ref, const Transform& base_transform,
                                   const float& start_radius, const float& clearance,
                                   const PosSelectionType& selection_type, Vector& out_pos) {
    int Result = -4;
    if (!(start_radius >= 0.0f)) return Result;
    if (!(clearance >= 0.0f)) return Result;
    float Radius = start_radius;
    unsigned int Ids[0x400];
    bool bEmpty = false;
    do {
        unsigned int Count = 0;
        StarSystem* System = SysDB_LookupSystem(system_id_ref);
        if (System != nullptr)
            Count = System->scan_objects(Ids, 0x400, &base_transform.Translation, Radius,
                                         0xffffffffu);
        if (Count == 0) {
            float const NoRadius = -1.0f;                                // .rdata:06d66b14
            Count = 0;
            System = SysDB_LookupSystem(system_id_ref);
            if (System != nullptr)
                Count = System->scan_objects(Ids, 0x400, &base_transform.Translation,
                                             NoRadius, OBJ_PLANET);
        }
        int Attempts = 0;
        for (;;) {
            unsigned int const Selection = static_cast<unsigned int>(selection_type);
            if (Selection > POS_SEL_3D_VOLUME) return Result;
            Vector Offset;
            double const Step = 3.0517578125e-05;                        // .rdata:06d656b8 (float 2^-15)
            double const TwoPi = 6.2831854820251465;                     // .rdata:06d66b80
            if (Selection == POS_SEL_3D_VOLUME) {
                double const U1 = static_cast<double>(rand()) * Step;
                double const U2 = static_cast<double>(rand()) * Step;
                Offset.x = static_cast<float>(static_cast<double>(rand()) * Step *
                                              static_cast<double>(Radius));
                Offset.y = static_cast<float>(static_cast<double>(Radius) * U2);
                Offset.z = static_cast<float>(static_cast<double>(Radius) * U1);
            } else {
                double const Angle = static_cast<double>(rand()) * Step * TwoPi;
                float const C = static_cast<float>(std::cos(Angle) * static_cast<double>(Radius));
                float const S = static_cast<float>(std::sin(Angle) * static_cast<double>(Radius));
                if (Selection == POS_SEL_XY_PLANE) {
                    Offset.x = C;
                    Offset.y = S;
                    Offset.z = 0.0f;
                } else if (Selection == POS_SEL_YZ_PLANE) {
                    Offset.x = 0.0f;
                    Offset.y = C;
                    Offset.z = S;
                } else {
                    Offset.x = C;
                    Offset.y = 0.0f;
                    Offset.z = S;
                }
            }

            Vector Candidate;
            Server_MathEngine()->TransformXform(Candidate, base_transform, Offset);
            out_pos = Candidate;

            bEmpty = true;
            Result = 0;
            bool bCollided = false;
            for (unsigned int Index = 0; Index < Count; ++Index) {
                unsigned int const Id = Ids[Index];
                unsigned int Bits = 0;
                if ((Id & 0x3fffffffu) != 0) {
                    GameObject* const Probe = Server_LookupLiveObject(Id);
                    if (Probe != nullptr) {
                        CObject* const Raw = Probe->Object;
                        if (Raw != nullptr &&
                            (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassSimple) == CObject::ClassSimple)
                            Bits = static_cast<CSimple*>(Raw)->TypeBits;
                    }
                }
                float OtherRadius = 0.0f;
                Vector Other;
                Other.x = 0.0f;
                Other.y = 0.0f;
                Other.z = 0.0f;
                if ((Id & 0x3fffffffu) != 0) {
                    GameObject* const Obj = Server_LookupLiveObject(Id);
                    if (Obj != nullptr) {
                        if ((Bits & OBJ_PLANET) != 0) {
                            Other = Obj->get_position();
                            Obj->get_atmosphere_range(OtherRadius);
                        } else {
                            Obj->get_physical_radius(OtherRadius, Other);
                        }
                    }
                }
                double const Dx = static_cast<double>(out_pos.x) - Other.x;
                double const Dy = static_cast<double>(out_pos.y) - Other.y;
                double const Dz = static_cast<double>(out_pos.z) - Other.z;
                double const Dist = std::sqrt((Dz * Dz + Dy * Dy) + Dx * Dx); // retail's sum order
                double const Gap = (Dist - static_cast<double>(OtherRadius))
                                   - static_cast<double>(clearance);
                if (Gap <= 0.0) {
                    bCollided = true;
                    break;
                }
            }
            if (!bCollided)
                break;
            ++Attempts;
            bEmpty = false;
            Result = -1;
            if (Attempts >= 10)
                break;
        }
        Radius = static_cast<float>(static_cast<double>(Radius) + static_cast<double>(Radius));
    } while (!bEmpty);
    return Result;
}

// SpaceObj::IsPosEmpty -- retail .text:06cfa060 (ordinal 0x1a060, cdecl). Any hit in the
// radius scan means not empty. Otherwise a second scan (radius -1.0f, mode 2) tests each
// live object's atmosphere range against the distance. An unknown system returns -1 with
// out_empty true. x87 arithmetic is reproduced in double (53-bit precision control).
SERVER_API int __cdecl IsPosEmpty(const unsigned int& system_id_ref, const Vector& pos,
                                  const float& radius, bool& out_empty) {
    out_empty = true;
    float SearchRadius = radius;
    int Result = -1;
    bool bHit = false;
    unsigned int Count2 = 0;
    StarSystem* System = SysDB_LookupSystem(system_id_ref);
    if (System == nullptr) {
        out_empty = true;
        return Result;
    }
    unsigned int Ids[0x400];
    unsigned int const Count1 =
        System->scan_objects(Ids, 0x400, &pos, SearchRadius, 0xffffffffu); // FUN_06d0bcd0
    if (Count1 == 0) {
        SearchRadius = -1.0f;                                            // .rdata:06d66b14
        System = SysDB_LookupSystem(system_id_ref);                      // looked up again
        if (System != nullptr) {
            Count2 = System->scan_objects(Ids, 0x400, &pos, SearchRadius, OBJ_PLANET);
            for (unsigned int Index = 0; Index < Count2; ++Index) {
                Vector Other;
                Other.x = 0.0f;
                Other.y = 0.0f;
                Other.z = 0.0f;
                float OtherRadius = 0.0f;
                unsigned int const Id = Ids[Index];
                if ((Id & 0x3fffffffu) != 0) {
                    GameObject* const Obj = Server_LookupLiveObject(Id);
                    if (Obj != nullptr) {
                        Other = Obj->get_position();
                        Obj->get_atmosphere_range(OtherRadius);          // return ignored
                    }
                }
                double const Dx = static_cast<double>(pos.x) - Other.x;
                double const Dy = static_cast<double>(pos.y) - Other.y;
                double const Dz = static_cast<double>(pos.z) - Other.z;
                double const Dist = std::sqrt((Dz * Dz + Dy * Dy) + Dx * Dx);
                double const Gap = Dist - static_cast<double>(OtherRadius);
                if (Gap <= 0.0) {
                    bHit = true;
                    break;
                }
            }
        }
    }
    Result = 0;
    if (Count1 != 0 || (Count2 != 0 && bHit)) {
        out_empty = false;
        return Result;
    }
    out_empty = true;
    return Result;
}

// The PhySys::CreateParms that JumpIn, Relocate and Launch each build inline before a
// system transfer. Deviation: retail leaves Name[1..7] and Pad27 unwritten; the port
// zeroes the whole block first.
static void SpaceObj_InitTransferParms(PhySys::CreateParms& Parms)
{
    memset(&Parms, 0, sizeof(Parms));
    Parms.Mass = 1.0f;
    Parms.LinearDamping = PhySys::DEFAULT_LINEAR_DAMPING;
    Parms.AngularDamping.x = PhySys::DEFAULT_ANGULAR_DAMPING.x;
    Parms.AngularDamping.y = PhySys::DEFAULT_ANGULAR_DAMPING.y;
    Parms.AngularDamping.z = PhySys::DEFAULT_ANGULAR_DAMPING.z;
    Parms.SystemId = 0;
    Parms.Type = 2;
    Parms.bPhysicalUnmoveable = 0;
    Parms.bEnableCollisions = 1;
    Parms.Name[0] = 0;
    Parms.bHasInertia = 0;
    Parms.Inertia.x = 0.0f;
    Parms.Inertia.y = 0.0f;
    Parms.Inertia.z = 0.0f;
}

// SpaceObj::JumpIn -- retail .text:06cfa220 (ordinal 0x1a220, cdecl). The arrival location
// comes from gate vtable +0x250 (Solar::ChooseSystemArrivalLocation), called on whatever
// the gate object is. An in-transit ship goes through Server_TakeInTransit (FUN_06d00380).
SERVER_API int __cdecl JumpIn(const unsigned int& ship_id, const unsigned int& gate_id) {
    if ((ship_id & 0x3fffffffu) == 0) return -4;
    GameObject* ShipObj = nullptr;
    StarSystem* ShipSystem = nullptr;
    if (!Server_FindLiveObject(ship_id, ShipObj, ShipSystem))
        return -2;
    GameObject* GateObj = nullptr;
    StarSystem* GateSystem = nullptr;
    if (!Server_FindLiveObject(gate_id, GateObj, GateSystem))
        return -3;

    PhySys::CreateParms Parms;
    SpaceObj_InitTransferParms(Parms);
    if (ShipSystem == nullptr)
        Server_TakeInTransit(ShipObj, Parms);
    else if (GateSystem != ShipSystem)
        ShipSystem->remove_ship(ShipObj, &Parms);

    Vector Pos;
    Matrix Orient;
    static_cast<Solar*>(GateObj)->ChooseSystemArrivalLocation(ShipObj, Pos, Orient);
    if (GateSystem != ShipSystem)
        GateSystem->add_ship(ShipObj, &Parms, Pos, Orient);
    GateSystem->arrive_ship(ShipObj, GateObj, Pos, Orient);             // FUN_06d0cc00
    return 0;
}

// SpaceObj::Relocate -- retail .text:06cfa370 (ordinal 0x1a370, cdecl). A zero system id
// parks the object in transit (Server_PushInTransit, FUN_06d00560). Within the same
// system the raw CObject (CBase::Object under the (& 1) test, not null-tested after) is
// beamed via vtable +0x7c. Otherwise it is added to the target system and a CShip gets
// set_control_exclusion(0). Always returns 0 past the lookup.
SERVER_API int __cdecl Relocate(const unsigned int& obj_id, const unsigned int& system_id,
                                 const Vector& pos, const Matrix& orient) {
    if ((obj_id & 0x3fffffffu) == 0) return -4;
    GameObject* Obj = nullptr;
    StarSystem* OwnSystem = nullptr;
    if (!Server_FindLiveObject(obj_id, Obj, OwnSystem))
        return -2;

    StarSystem* TargetSystem = nullptr;
    if ((system_id & 0x3fffffffu) != 0)
        TargetSystem = SysDB_LookupSystem(system_id);

    PhySys::CreateParms Parms;
    SpaceObj_InitTransferParms(Parms);
    if (OwnSystem == nullptr)
        Server_TakeInTransit(Obj, Parms);
    else if (TargetSystem != OwnSystem)
        OwnSystem->remove_ship(Obj, &Parms);

    if (TargetSystem == nullptr) {
        Server_PushInTransit(Obj, Parms);
        return 0;
    }
    if (TargetSystem == OwnSystem) {
        CObject* Raw = Obj->Object;
        if (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassObject) != CObject::ClassObject)
            Raw = nullptr;
        Raw->beam_object(pos, orient, false);
        return 0;
    }
    TargetSystem->add_ship(Obj, &Parms, pos, orient);
    CObject* const ShipRaw = Obj->Object;
    if (ShipRaw != nullptr &&
        (static_cast<unsigned int>(ShipRaw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
        static_cast<CShip*>(ShipRaw)->set_control_exclusion(0);
    return 0;
}

// SpaceObj::Launch -- retail .text:06cfa520 (ordinal 0x1a520, cdecl). Neither cast is
// tested. Across systems the ship is moved to the dock's system at the origin with an
// identity orientation before StarSystem::launch (FUN_06d0cee0).
SERVER_API int __cdecl Launch(const unsigned int& launch_obj_id, const unsigned int& dock_obj_id,
                               int launch_type) {
    if ((launch_obj_id & 0x3fffffffu) == 0) return -4;
    GameObject* LaunchObj = nullptr;
    StarSystem* LaunchSystem = nullptr;
    if (!Server_FindLiveObject(launch_obj_id, LaunchObj, LaunchSystem))
        return -2;
    GameObject* DockObj = nullptr;
    StarSystem* DockSystem = nullptr;
    if (!Server_FindLiveObject(dock_obj_id, DockObj, DockSystem))
        return -3;

    ::EqObj* const DockEq = dynamic_cast< ::EqObj*>(DockObj);
    ::Ship* const ShipRw = dynamic_cast< ::Ship*>(LaunchObj);

    if (DockSystem != LaunchSystem) {
        PhySys::CreateParms Parms;
        SpaceObj_InitTransferParms(Parms);
        LaunchSystem->remove_ship(ShipRw, &Parms);                       // FUN_06d0d000
        Vector Pos;
        Pos.x = 0.0f;
        Pos.y = 0.0f;
        Pos.z = 0.0f;
        Matrix const Orient(1.0f, 0.0f, 0.0f,
                            0.0f, 1.0f, 0.0f,
                            0.0f, 0.0f, 1.0f);
        DockSystem->add_ship(ShipRw, &Parms, Pos, Orient);               // FUN_06d0d100
    }
    DockSystem->launch(ShipRw, DockEq, launch_type);
    return 0;
}

// SpaceObj::DockRequest -- retail 0x1a6b0. No mask check, and a failed lookup returns 0
// (not -4/-2); otherwise a tail call into IObjAffect slot 23 (+0x5c of the second vtable).
SERVER_API int __cdecl DockRequest(const unsigned int& obj_id, const unsigned int& target_id) {
    GameObject* Obj = Server_LookupLiveObject(obj_id);
    if (!Obj) return 0;
    // The third argument is the address of four zero bytes.
    static unsigned const ZeroSecondId = 0;   // .rdata:06d66b04
    return Obj->request_event(kRequestEventDock, &target_id, &ZeroSecondId, 1, nullptr);
}

// SpaceObj::Dock -- retail .text:06cfa6e0 (ordinal 0x1a6e0, cdecl). Neither the Ship nor
// the EqObj cast is tested; a failed ship lookup still falls through to the host part.
// For a CShip, every follower watcher (CShip +0x1f8 vector) with a behaviour manager gets
// event 5 (handled by LAB_062d3f90) while the target resolves. dequeue's bool is ignored.
// Deviation: retail writes only EQInfo's dword at +0 and byte at +4; the port zeroes the
// rest.
SERVER_API int __cdecl Dock(const unsigned int& ship_id, const unsigned int& target_id,
                             int dock_port, DOCK_HOST_RESPONSE dock_host_response) {
    if ((ship_id & 0x3fffffffu) == 0) return -4;
    int Result = -2;
    GameObject* const Obj = Server_LookupLiveObject(ship_id);
    if (Obj != nullptr) {
        ::Ship* const ShipRw = dynamic_cast< ::Ship*>(Obj);
        if (ShipRw->bDockInUse != 0) {                                   // Ship byte +0x98
            ShipRw->request_returned(0, static_cast<int>(dock_host_response), dock_port);
            Result = 0;
        }

        EQInfo DockEquip;
        memset(&DockEquip, 0, sizeof(DockEquip));
        DockEquip.Type = EQINFO_TYPE_DOCKING_LIGHTS;
        DockEquip.EquipId = (dock_host_response == DOCK_HOST_RESPONSE_PROCEED || dock_host_response == DOCK_HOST_RESPONSE_BUSY) ? 1 : 0; // a byte in retail
        ActivateEquipment(ship_id, &DockEquip);                          // result ignored

        if (dock_host_response == DOCK_HOST_RESPONSE_BUSY) return Result;

        CObject* const Raw = Obj->Object;
        if (Raw != nullptr &&
            (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip) {
            CShip* const Cship = static_cast<CShip*>(Raw);
            for (std::vector<FObjRWWatcher>::const_iterator Entry = Cship->FollowFollowers.begin();
                 Entry != Cship->FollowFollowers.end(); ++Entry) {
                IObjRW* const Follower = Entry->get();
                if (Follower == nullptr) continue;
                IBehaviorManager* const Manager = GetBehaviorManager(Follower);
                if (Manager == nullptr) continue;
                GameObject* const Target = Server_LookupLiveObject(target_id);
                if (Target == nullptr) continue;

                // LAB_062d3f90's case 5 reads [+0] / [+4] & 0x120 / [+8] / byte [+0xc].
                struct {
                    unsigned int TargetId;   // +0x00
                    unsigned int TargetType; // +0x04 (get_type out)
                    unsigned int ShipId;     // +0x08
                    unsigned char bHasPort;  // +0x0C
                    unsigned char Pad0d[3];
                } Block;
                memset(&Block, 0, sizeof(Block));
                Block.ShipId = ship_id;
                Block.TargetId = target_id;
                Target->get_type(Block.TargetType);
                Block.bHasPort = (dock_port != -1) ? 1 : 0;
                FBehaviorManagerEvent Event;
                Event.Type = FBehaviorManagerEvent::kEventDockRequest;
                Event.Data = &Block;
                Manager->OnEvent(&Event);
            }
        }
    }
    if (dock_host_response == DOCK_HOST_RESPONSE_BUSY) return Result;

    GameObject* const Host = Server_LookupLiveObject(target_id);
    if (Host == nullptr) return -3;
    ::EqObj* const HostEq = dynamic_cast< ::EqObj*>(Host);
    if (dock_port >= 0)
        HostEq->queue_dock_request(dock_port, ship_id);                  // FUN_06ce7880
    else
        HostEq->dequeue_dock_request(dock_port, ship_id);                // FUN_06ce78d0
    return 0;
}

// SpaceObj::InstantDock -- retail .text:06cfa8d0 (ordinal 0x1a8d0, cdecl). Past the mask
// checks it always returns -2. The Ship cast is not tested and Server_FindLiveObject's
// result is ignored. The owning system is not null-tested on either call (null for an
// in-transit host; retail dereferences it the same way). The docking-sphere index is
// unchecked; types 3/4/5 (moor_small/medium/large) are moors, everything else lands.
SERVER_API int __cdecl InstantDock(const unsigned int& ship_id, const unsigned int& target_id,
                                    int dock_port) {
    if ((ship_id & 0x3fffffffu) == 0 || (target_id & 0x3fffffffu) == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(ship_id);
    if (Obj == nullptr) return -2;

    ::Ship* const ShipRw = dynamic_cast< ::Ship*>(Obj);
    GameObject* Host = nullptr;
    StarSystem* Owner = nullptr;
    Server_FindLiveObject(target_id, Host, Owner);
    ::EqObj* const HostEq = dynamic_cast< ::EqObj*>(Host);
    if (HostEq == nullptr) return -2;

    unsigned int const Flags = HostEq->get_type_bits();
    if (Flags == (Flags & (OBJ_JUMP_GATE | OBJ_AIRLOCK_GATE | OBJ_JUMP_HOLE))) {
        Owner->complete_dock(ShipRw, HostEq);                            // FUN_06d0c970
        return -2;
    }

    CEqObj* const HostCeq = HostEq->ceqobj();
    Archetype::EqObj const* const Arch =
        static_cast<Archetype::EqObj const*>(HostCeq->Archetype);
    unsigned int const Kind = Arch->DockingSpheres[dock_port].Type;
    bool const bLand = !(Kind == TERM_MOOR_SMALL || Kind == TERM_MOOR_MEDIUM || Kind == TERM_MOOR_LARGE);
    Owner->queue_dock(ShipRw, HostEq, &HostCeq->DockingPointBaseId, &bLand); // FUN_06d0d370
    return -2;
}

// SpaceObj::FormationResponse -- retail .text:06cfa9f0 (cdecl). The Ship cast is not
// tested. request_returned is FUN_06d01e80.
SERVER_API int __cdecl FormationResponse(const unsigned int& obj_id, FORMATION_RTYPE response_type) {
    if ((obj_id & 0x3fffffffu) == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id);
    if (Obj == nullptr) return -2;
    ::Ship* const ShipRw = dynamic_cast< ::Ship*>(Obj);
    ShipRw->request_returned(1, static_cast<int>(response_type), 0);
    return 0;
}

// SpaceObj::LaneResponse -- retail .text:06cfaa50 (cdecl). FormationResponse with
// request type 2.
SERVER_API int __cdecl LaneResponse(const unsigned int& obj_id, int lane_param) {
    if ((obj_id & 0x3fffffffu) == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id);
    if (Obj == nullptr) return -2;
    ::Ship* const ShipRw = dynamic_cast< ::Ship*>(Obj);
    ShipRw->request_returned(2, lane_param, 0);
    return 0;
}

// SpaceObj::GetRelativeHealth -- retail .text:06cfaab0 (FUN_06d00670 lookup), IObjInspect slot 16
// (+0x40). Retail tests the id against 0 unmasked.
SERVER_API int __cdecl GetRelativeHealth(const unsigned int& obj_id_ref, float& out_ratio) {
    out_ratio = 0.0f;
    if (obj_id_ref == 0) return -4;
    GameObject* obj = Server_LookupLiveObject(obj_id_ref);
    if (!obj) return -2;
    obj->get_status(out_ratio);
    return 0;
}

// SpaceObj::SetRelativeHealth -- retail .text:06cfaaf0 (cdecl). The range tests reject
// NaN as well (unordered compares). The id test is unmasked. vtable +0x1ac.
SERVER_API int __cdecl SetRelativeHealth(const unsigned int& obj_id_ref, float ratio) {
    if (!(ratio >= 0.0f)) return -4;
    if (!(ratio <= 1.0f)) return -4;
    if (obj_id_ref == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id_ref);
    if (Obj == nullptr) return -2;
    Obj->set_relative_health(ratio);
    return 0;
}

// SpaceObj::GetHealth -- retail .text:06cfab50 (FUN_06d00670 lookup). One IObjInspect slot 15
// (+0x3c) call fills both outs.
SERVER_API int __cdecl GetHealth(const unsigned int& obj_id_ref, float& out_current, float& out_max) {
    out_current = 0.0f; out_max = 0.0f;
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupLiveObject(obj_id_ref);
    if (!obj) return -2;
    obj->get_status(out_current, out_max);
    return 0;
}

// SpaceObj::GetShieldHealth -- retail 0x1abb0 (FUN_06d00670 lookup). One IObjInspect
// slot 17 (+0x44) call fills all three outs.
SERVER_API int __cdecl GetShieldHealth(const unsigned int& obj_id_ref, float& out_current,
                                        float& out_max, bool& out_active) {
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupLiveObject(obj_id_ref);
    if (!obj) return -2;
    obj->get_shield_status(out_current, out_max, out_active);
    return 0;
}

// SpaceObj::GetTradelaneNextAndPrev -- retail 0x1ac00 (FUN_06d00670 lookup). Solars only
// ((ObjectClass & 0x303) == 0x303).
SERVER_API int __cdecl GetTradelaneNextAndPrev(const unsigned int& obj_id_ref,
                                                unsigned int& out_next, unsigned int& out_prev) {

    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id_ref);
    if (Obj == nullptr) return -2;
    CObject* const Raw = Obj->Object;
    if (Raw == nullptr ||
        (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassSolar) != CObject::ClassSolar)
        return -1;
    CSolar* const Cs = static_cast<CSolar*>(Raw);
    out_next = Cs->NextRing;   // CSolar+0x1dc
    out_prev = Cs->PrevRing;   // CSolar+0x1d8
    return 0;
}

// SpaceObj::LightFuse -- retail .text:06cfac70 (cdecl). The id test is unmasked.
// light_fuse is EqObj's vtable +0x1e4, called on the looked-up wrapper without
// re-narrowing.
SERVER_API int __cdecl LightFuse(const unsigned int& obj_id_ref, const char* fuse_name, float delay) {
    if (obj_id_ref == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id_ref);
    if (Obj == nullptr) return -2;
    ID_String FuseId;
    FuseId.Id = CreateID(fuse_name);
    bool const bLit = static_cast< ::EqObj*>(Obj)->light_fuse(0, FuseId, SUBOBJ_ID_NONE,
                                                              delay, -1.0f);
    return bLit ? 0 : -4;
}

} // namespace SpaceObj
} // namespace pub
