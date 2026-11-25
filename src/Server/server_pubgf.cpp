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
#include "../include/common_server_playerdb.h"
#include "../include/server_exports.h"
#include "../include/server_basedb.h"
#include "../include/common_physys.h"
#include "../include/server_sysdb.h"
#include "../include/common_misc.h"
#include "../include/common_net.h"      // IsMPServer (AdjustCash's gate)
#include "../include/common_newchar.h"   // RtcSlider::get_slider (pub::GF::GetRtcPerformanceSlider)
#include "../include/rpc_iclient.h"
#include "../include/common_casteroid.h"
#include "server_internal.h"
#include <windows.h>
#include <cstring>     // std::memcpy (SpaceObj::Create EquipIdMaker seed)

// The IClient the server talks back to (DAT_06d8fb14). Defined in IServerImpl.cpp and set
// by IServerImpl::Startup; several pub:: exports dispatch straight into its vtable.
extern IClient* g_pClientInterface;

// ===========================================================================
// pub::GF exports (Game Framework) -- gf/pubGF.cpp. Every object the family
// creates lives in the BaseDB (FUN_06d15650 = GetBaseDB(), src/Server/server_basedb.cpp).
// ===========================================================================

namespace pub {
namespace GF {

// ---- FindBase / FindLocation / GetSpaceflightLocation ----

// pub::GF::FindBase -- retail 0x37ca0 -> FUN_06d141f0, a bare CreateID. No null test.
SERVER_API unsigned int __cdecl FindBase(const char* name) {
    return CreateID(name);
}

// pub::GF::FindLocation -- retail 0x37cc0 -> FUN_06d14210, a bare MakeLocationID. No null
// test.
SERVER_API unsigned int __cdecl FindLocation(unsigned int base_id, const char* name) {
    return MakeLocationID(base_id, name);
}

// pub::GF::GetSpaceflightLocation -- retail 0x37ce0: Common's SpaceflightLocationID data
// export (CreateID("Spaceflight"), common_misc.cpp).
SERVER_API unsigned int __cdecl GetSpaceflightLocation() {
    return ::SpaceflightLocationID.Id;
}

// ---- GetBasePosition ----

// pub::GF::GetBasePosition -- retail .text:06d17d20. Single-entry memo cache DAT_06d8e578
// (base id) + DAT_06d8e57c/580/584 (the Vector), reset by LAB_06d17cf0.
static unsigned int GCachedBasePosId = 0;

static Vector GCachedBasePos = { 0, 0, 0 };

// Both ids are `unsigned int const&` (?GetBasePosition@GF@pub@@YAHABI0AAVVector@@@Z). The
// base id is tested before the system id; get_system is not null-tested. Walks the
// system's objects (ISystem slots 15/16) for one whose dock_with (+0x80) is the base.
SERVER_API int __cdecl GetBasePosition(const unsigned int& system_id, const unsigned int& base_id, Vector& out_pos) {
    if ((base_id & 0x3fffffff) == 0) return -2;
    if ((system_id & 0x3fffffff) == 0) return -2;

    if (base_id == GCachedBasePosId) {
        out_pos.x = GCachedBasePos.x;
        out_pos.y = GCachedBasePos.y;
        out_pos.z = GCachedBasePos.z;
        return 0;
    }

    Universe::ISystem const* Sys = Universe::get_system(system_id);
    Universe::FSystemObject Record;
    Record.Nickname.Length = 0;
    Record.Nickname.Data[0] = '\0';
    Record.Reputation.Length = 0;
    Record.Reputation.Data[0] = '\0';
    Record.NextRing.Length = 0;
    Record.NextRing.Data[0] = '\0';
    Record.PrevRing.Length = 0;
    Record.PrevRing.Data[0] = '\0';

    unsigned int const Count = (unsigned int)Sys->GetObjectCount();
    for (unsigned int Index = 0; Index < Count; ++Index) {
        if (!Sys->GetSystemObject(Index, &Record)) continue;
        if (Record.DockWith != base_id) continue;
        GCachedBasePosId = base_id;
        GCachedBasePos.x = Record.Pos.x;
        GCachedBasePos.y = Record.Pos.y;
        GCachedBasePos.z = Record.Pos.z;
        out_pos.x = Record.Pos.x;
        out_pos.y = Record.Pos.y;
        out_pos.z = Record.Pos.z;
        return 0;
    }
    return -2;
}

// ---- Costume / BodyPart / Accessory ----

// pub::GF::GetCostumeByID -- retail .text:06d17e70. No null test.
SERVER_API void __cdecl GetCostumeByID(int id, Costume& out_costume) {
    GetCostumeDescriptions()->get_costume(id, out_costume);
}

// pub::GF::GetBodyPart -- retail .text:06d17e90. A hit returns the record's first dword.
// No null tests.
SERVER_API int __cdecl GetBodyPart(const char* name, int type_id) {
    bodypart const* bp = GetCostumeDescriptions()->find_bodypart(type_id, name);
    if (bp) return *(int const*)bp;
    FDUMP(kFdumpError, "%s(%d) : *** ERROR: Cannot find bodypart %s\n",
          "E:\\FL\\Scratch\\Source\\Server\\gf\\pubGF.cpp", 0x78, name);
    return 0;
}

// pub::GF::GetAccessory -- retail .text:06d17ee0. As GetBodyPart over find_accessory.
SERVER_API int __cdecl GetAccessory(const char* name) {
    accessory const* acc = GetCostumeDescriptions()->find_accessory(name);
    if (acc) return *(int const*)acc;
    FDUMP(kFdumpError, "%s(%d) : *** ERROR: Cannot find accessory %s\n",
          "E:\\FL\\Scratch\\Source\\Server\\gf\\pubGF.cpp", 0x87, name);
    return 0;
}

// ---- Character Create/Destroy/SetBehavior ----

// pub::GF::CharacterCreate -- retail .text:06d17f30. BaseDB::create_character
// (FUN_06d14870) with description+0x50 as the session filter.
SERVER_API unsigned int __cdecl CharacterCreate(const CharacterDescription& description) {
    CharacterProperties Props;
    Props.init(description);
    return (unsigned int)GetBaseDB()->create_character(Props, description.Filter);
}

// pub::GF::CharacterDestroy -- retail .text:06d17fb0 (destroy_character is FUN_06d149c0).
// No null test of the pointer.
SERVER_API void __cdecl CharacterDestroy(unsigned int* handle) {
    if ((*handle & 0x3fffffff) != 0) {
        GetBaseDB()->destroy_character(*handle);
        *handle = 0;
    }
}

// pub::GF::CharacterSetBehavior -- retail .text:06d17fe0 (FUN_06d14ac0, unconditional).
SERVER_API void __cdecl CharacterSetBehavior(unsigned int character_id, unsigned long behavior_handle) {
    GetBaseDB()->set_character_behavior(character_id, behavior_handle);
}

// ---- CharacterBehavior Create/Destroy ----

// pub::GF::CharacterBehaviorCreate -- retail .text:06d18000. BaseDB::create_behavior
// (FUN_06d14b70) with description+0x25c as the session filter.
SERVER_API unsigned long __cdecl CharacterBehaviorCreate(const CharacterBehaviorDescription& description) {
    ScriptBehavior Behavior;
    Behavior.init(description);
    return GetBaseDB()->create_behavior(Behavior, description.Filter);
}

// pub::GF::CharacterBehaviorDestroy -- retail .text:06d18090 (destroy_behavior is
// FUN_06d14cc0); -1 is the empty handle.
SERVER_API void __cdecl CharacterBehaviorDestroy(unsigned long* handle) {
    if (*handle != 0xFFFFFFFF) {
        GetBaseDB()->destroy_behavior(*handle);
        *handle = 0xFFFFFFFF;
    }
}

// pub::GF::GetCostumeSkeletonGender -- retail .text:06d180c0. No null test.
SERVER_API int __cdecl GetCostumeSkeletonGender(Costume const& desc, int& out_gender) {
    out_gender = GetCostumeDescriptions()->get_costume_gender(desc);
    return 0;
}

// ---- Character Placement functions ----
// All go through BaseDB::find_location (FUN_06d14170; a miss FDUMPs 'Location %u is
// invalid' and returns), then a FUN_06d127xx helper that reads the RoomData [entry+0x8]
// unchecked. The placement helpers index the vector with no bounds or element null test.

// pub::GF::GetNumCharacterPlacements -- retail 0x380e0 (FUN_06d12720: RoomData+0x178/0x17C).
SERVER_API int __cdecl GetNumCharacterPlacements(unsigned int location_id) {
    FRoomEntry* Entry = GetBaseDB()->find_location(location_id);
    if (!Entry) return 0;
    RoomData* room = Entry->Data;
    return (int)room->CharacterPlacements.size();
}

// pub::GF::GetCharacterPlacementByName -- retail 0x38100 (FUN_06d12740). Only the record's
// name pointer is null-tested; `name` goes straight to stricmp.
SERVER_API int __cdecl GetCharacterPlacementByName(unsigned int location_id, const char* name, int& out_index) {
    FRoomEntry* Entry = GetBaseDB()->find_location(location_id);
    if (!Entry) return -2;
    RoomData* room = Entry->Data;
    for (unsigned int i = 0; i < room->CharacterPlacements.size(); i++) {
        CharPlaceInfo* cp = room->CharacterPlacements[i];
        if (cp->Name && _stricmp(cp->Name, name) == 0) {
            out_index = (int)i;
            return 0;
        }
    }
    out_index = -1;
    return -3;
}

// pub::GF::GetCharacterPlacementName -- retail 0x38140 (FUN_06d127b0). Returns the name
// (record +0x00) as-is, null included.
SERVER_API const char* __cdecl GetCharacterPlacementName(unsigned int location_id, int index) {
    FRoomEntry* Entry = GetBaseDB()->find_location(location_id);
    if (!Entry) return "";
    RoomData* room = Entry->Data;
    return room->CharacterPlacements[index]->Name;
}

// pub::GF::GetCharacterPlacementOccupancy -- retail 0x38170. CharPlaceInfo+0x34 CharType
// (-1 none, 0 npc, 1 pc, 2 rpc; see common_goods.h and RoomData::read_set_script).
SERVER_API unsigned long __cdecl GetCharacterPlacementOccupancy(unsigned int location_id, int index) {
    FRoomEntry* Entry = GetBaseDB()->find_location(location_id);
    if (!Entry) return 0;
    RoomData* room = Entry->Data;
    return (unsigned long)room->CharacterPlacements[index]->CharType;
}

// pub::GF::GetCharacterPlacementPosture -- retail 0x381a0. CharPlaceInfo+0x38 Posture
// (0 stand, 1 sitlow, 2 sithigh, 3 special, -1 unset).
SERVER_API int __cdecl GetCharacterPlacementPosture(unsigned int location_id, int index, unsigned long& out_posture) {
    FRoomEntry* Entry = GetBaseDB()->find_location(location_id);
    if (!Entry) return -2;
    RoomData* room = Entry->Data;
    out_posture = (unsigned long)room->CharacterPlacements[index]->Posture;
    return 0;
}

// pub::GF::IsCharacterPlacementNormal -- retail 0x381e0. CharPlaceInfo+0x3C bZoneScript
// (0 = "zg/" zone-group prefix = normal, 1 = "zs/" zone-script prefix = special).
SERVER_API bool __cdecl IsCharacterPlacementNormal(unsigned int location_id, int index) {
    FRoomEntry* Entry = GetBaseDB()->find_location(location_id);
    if (!Entry) return true;
    RoomData* room = Entry->Data;
    return room->CharacterPlacements[index]->bZoneScript == 0;
}

// pub::GF::IsCharacterPlacementSpecial -- retail 0x38210 (bZoneScript == 1).
SERVER_API bool __cdecl IsCharacterPlacementSpecial(unsigned int location_id, int index) {
    FRoomEntry* Entry = GetBaseDB()->find_location(location_id);
    if (!Entry) return false;
    RoomData* room = Entry->Data;
    return room->CharacterPlacements[index]->bZoneScript == 1;
}

// pub::GF::EnumerateCharacterPlacementIni -- retail 0x38250 (FUN_06d12850). The mangled
// name's trailing `1` repeats PAX, so the context is a void*. No callback null test.
SERVER_API void __cdecl EnumerateCharacterPlacementIni(unsigned int location_id,
    void (__cdecl *callback)(int, INI_Reader*, void*), void* context) {
    FRoomEntry* Entry = GetBaseDB()->find_location(location_id);
    if (!Entry) return;
    RoomData* room = Entry->Data;
    room->enumerate_CharacterPlacement_ini(callback, context);
}

// pub::GF::GetCharacterOnPlacement -- retail .text:06d18280 (FUN_06d127d0).
SERVER_API unsigned int __cdecl GetCharacterOnPlacement(unsigned int location_id, unsigned int filter_id, int placement_index) {
    FRoomEntry* room = GetBaseDB()->find_location(location_id);
    if (!room) return 0;
    return room->get_character_on_placement(filter_id, placement_index);
}

// pub::GF::GetCharacterPlacementProperties -- retail 0x382b0. Only the placement name is
// null-tested; out_props is not.
SERVER_API bool __cdecl GetCharacterPlacementProperties(unsigned int location_id, int index, SetpointProperties* out_props) {
    FRoomEntry* Entry = GetBaseDB()->find_location(location_id);
    if (!Entry) return false;
    RoomData* room = Entry->Data;
    CharPlaceInfo* cp = room->CharacterPlacements[index];
    if (!cp->Name) return false;
    out_props->extract_from_name(cp->Name);
    return out_props->bValid != 0;
}

// pub::GF::FormCharacterPlacementName -- retail .text:06d18300, a tail jump to
// SetpointProperties::combine_to_name; no null test.
SERVER_API const char* __cdecl FormCharacterPlacementName(SetpointProperties const* props) {
    return props->combine_to_name();
}

// ---- MissionVendor ----

// pub::GF::MissionVendorOfferCreate -- retail .text:06d18310. BaseDB::create_mission_vendor
// (FUN_06d14dc0) with description+0x5c as the session filter.
SERVER_API unsigned long __cdecl MissionVendorOfferCreate(const MissionVendorOfferDescription& description) {
    MissionVendorProperties Props;
    Props.init(description);
    return GetBaseDB()->create_mission_vendor(Props, description.Filter);
}

// pub::GF::MissionVendorOfferDestroy -- retail .text:06d18390 (FUN_06d14f10).
SERVER_API void __cdecl MissionVendorOfferDestroy(unsigned long* handle) {
    if (*handle != 0xFFFFFFFF) {
        GetBaseDB()->destroy_mission_vendor(*handle);
        *handle = 0xFFFFFFFF;
    }
}

// pub::GF::GetMissionVendorOfferCount -- retail .text:06d183c0 (FUN_06d15010).
SERVER_API int __cdecl GetMissionVendorOfferCount(unsigned int base_id, unsigned int vendor_type) {
    return GetBaseDB()->count_mission_vendors(base_id, vendor_type);
}

// pub::GF::MissionVendorAcceptance -- retail .text:06d183e0 (FUN_06d150a0, unconditional).
SERVER_API void __cdecl MissionVendorAcceptance(unsigned long handle, bool accepted, const FmtStr& reason_fmtstr, unsigned int session_id) {
    GetBaseDB()->mission_vendor_acceptance(handle, accepted, reason_fmtstr, session_id);
}

// pub::GF::ReportWhyMissionVendorEmpty -- retail 0x38410. A real packet send: IClient +0xa8
// (FLPACKET_SERVER_GFMISSIONVENDORWHYEMPTY) to client_id (Content passes the mission
// state's PlayerId). The client interface is not null-tested.
SERVER_API int __cdecl ReportWhyMissionVendorEmpty(unsigned int client_id, MVEmptyReason reason) {
    ::g_pClientInterface->SendServerGFMissionVendorWhyEmpty(client_id, (unsigned int)reason);
    return 0;
}

// ---- NewsBroadcast ----

// pub::GF::NewsBroadcastCreate -- retail .text:06d18430. BaseDB::create_news_broadcast
// (FUN_06d15140) with description+0x5c as the session filter.
SERVER_API unsigned long __cdecl NewsBroadcastCreate(const NewsBroadcastDescription& description) {
    NewsBroadcastProperties Props;
    Props.init(description);
    return GetBaseDB()->create_news_broadcast(Props, description.Filter);
}

// pub::GF::NewsBroadcastDestroy -- retail .text:06d184b0 (FUN_06d15290).
SERVER_API void __cdecl NewsBroadcastDestroy(unsigned long* handle) {
    if (*handle != 0xFFFFFFFF) {
        GetBaseDB()->destroy_news_broadcast(*handle);
        *handle = 0xFFFFFFFF;
    }
}

// ---- AmbientScript ----

// pub::GF::AmbientScriptCreate -- retail .text:06d184e0. BaseDB::create_ambient_script
// (FUN_06d15390) with description+0x10 as the session filter.
SERVER_API unsigned long __cdecl AmbientScriptCreate(const AmbientScriptDescription& description) {
    AmbientScriptProperties Props;
    Props.init(description);
    return GetBaseDB()->create_ambient_script(Props, description.Filter);
}

// pub::GF::AmbientScriptDestroy -- retail .text:06d18560 (FUN_06d15510).
SERVER_API void __cdecl AmbientScriptDestroy(unsigned long* handle) {
    if (*handle != 0xFFFFFFFF) {
        GetBaseDB()->destroy_ambient_script(*handle);
        *handle = 0xFFFFFFFF;
    }
}

// ---- GetRtcPerformanceSlider ----

// pub::GF::GetRtcPerformanceSlider -- retail 0x38590. Gated on IsMPServer (not
// SinglePlayer): the slider outside an MP server, else 1.0f.
SERVER_API float __cdecl GetRtcPerformanceSlider() {
    if (!::IsMPServer()) {
        return ::RtcSlider::get_slider();
    }
    return 1.0f;
}

} // namespace GF
} // namespace pub
