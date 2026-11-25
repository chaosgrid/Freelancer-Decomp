// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_pub.h"
#include "common_csys.h"   // Matrix (ShipInfo / SolarInfo orientation)
#include "common_metalist.h" // OwnerList<CargoDesc> (ShipInfo +0x5c)
#include "server_exports.h"

// CObject — base game object in Server.dll. Offsets from GetAtmosphereRange (0x19800),
// GetType (0x18da0) and the constructor FUN_06cee360.

// ===========================================================================
// pub::SpaceObj info structures — input to Create / CreateSolar / CreateLoot
// ===========================================================================

// DestroyType -- a GLOBAL enum in retail, not pub::SpaceObj's:
//   ?Destroy@SpaceObj@pub@@YAHIW4DestroyType@@@Z
enum DestroyType {
    DESTROY_NORMAL = 0,
    DESTROY_TYPE1 = 1,
    DESTROY_TYPE2 = 2,
};

// EQUIPMENT_RTYPE -- return type for ActivateEquipment, also GLOBAL:
//   ?ActivateEquipment@SpaceObj@pub@@YA?AW4EQUIPMENT_RTYPE@@ABIPAUEQInfo@12@@Z
// (EQInfo, by contrast, is pub::SpaceObj's -- the `@12@` back-reference.)
enum EQUIPMENT_RTYPE {
    EQUIPMENT_RTYPE_OK = 0,
    EQUIPMENT_RTYPE_INVALID = -1,
    EQUIPMENT_RTYPE_NOT_FOUND = -2,
    EQUIPMENT_RTYPE_ERROR = -4,
    EQUIPMENT_RTYPE_BAD_TYPE = -5,
};

namespace pub {
namespace SpaceObj {

// ShipInfo -- pub::SpaceObj::Create's argument
// (?Create@SpaceObj@pub@@YAHAAIABUShipInfo@12@@Z, .text:06cf7c90).
//
// Every placed member is proven by a load in pub::SpaceObj::Create (ESI = &info,
// .text:06cf7cf7). Create copies them into its CShipCreateInfo request (built on
// the stack, CEqObjCreateInfo ctor .text:06cf7d23, server_createinfo.h) and hands
// that to FUN_06d0a880, which copies it again (FUN_06d0af00) and fills
// CShip::CreateParms from the copy. The "request +N" and "Parms +N" columns below
// name those two hops. Retail offsets throughout; the last load is +0xbc, so the
// retail size is at least 0xc0. Members Create never reads are placeholders.

// CargoDesc -- one element of ShipInfo's cargo list (forward-declared in
// common_server.h for the OwnerList<CargoDesc> exports). Only the two dwords
// FUN_06d5f990 reads are known; retail's full size is not recovered, and
// nothing in the port allocates one.
struct CargoDesc {
    unsigned int GoodId;   // +0x00  GoodList::find_by_id (.text:06d5faa4), Good2Arch (.text:06d5fc9d)
    // FILD qword with a zeroed high dword (.text:06d5fad6 .. 06d5fae5), so the
    // division reads it unsigned; set_count takes it raw (.text:06d5fcb5).
    unsigned int Count;    // +0x04
};

struct ShipInfo {
    // FUN_06d43820 (Server_ControllerExists, server_dlls.cpp) is called with &info
    // (.text:06cf7f60) and dereferences it as the controller's FLID; the same
    // dword is stored into the new controller-map entry's +0x10 (.text:06cf7fe1).
    unsigned int ControllerId;       // +0x00
    unsigned int SystemID;           // +0x04  FUN_06d00280 (.text:06cf7cfe)
    unsigned int ShipArchID;         // +0x08  request +0x08 (.text:06cf7e08), Archetype::GetShip (.text:06cf7eca)
    Vector Position;                 // +0x0c  request +0x4c (.text:06cf7e0b)            -> Parms +0x10
    Vector AngularVelocity;          // +0x18  request +0xc4 (.text:06cf7e9d)            -> Parms +0x4c
    Vector Velocity;                 // +0x24  request +0xb8 (.text:06cf7e84)            -> Parms +0x40
    // I3DMathEngine::MatrixToQuaternion on &info+0x30 (.text:06cf7e4b / 06cf7e55),
    // quaternion -> request +0x58; FUN_06d0a880 turns it back into a matrix
    // (QuaternionToMatrix, .text:06d0aaed) and copies 9 dwords -> Parms +0x1c.
    Matrix Orientation;              // +0x30 .. +0x53
    // +0x54  request +0xb0 (.text:06cf7e01) -> Parms +0xec (CShip+0x21c); CShip::get_group_name
    // (common.dll 062b2640) returns it when nonzero, else the affiliation's name.
    unsigned int GroupNameId;
    unsigned int LoadoutID;          // +0x58  Loadout::Get (.text:06cf7f29), request +0x6c (.text:06cf7ec3)
    // FUN_06d5f990's third argument is &info+0x5c (.text:06cf7f4c). It reads
    // the dword at +4 and walks `node = *node` until NULL (.text:06d5f9ae ..
    // .text:06d5f9c5), and each node's +8 is a pointer to {GoodId, Count}:
    // MetaNode<CargoDesc> {Next, Prev, Data}, i.e. the OwnerList<CargoDesc>
    // (vptr +0x5c, Head +0x60, Tail +0x64, bUseHeap +0x68, FreeList +0x6c)
    // whose members Server.dll exports. sizeof 0x14 fills +0x5c..+0x6f exactly.
    OwnerList<CargoDesc> Cargo;      // +0x5c
    // pub::Costume (13 dwords), copied field by field and then a counted loop:
    unsigned int CostumeHead;        // +0x70  request +0x14 (.text:06cf7d8c)       -> Parms +0x64
    unsigned int CostumeBody;        // +0x74  request +0x18 (.text:06cf7d89)       -> Parms +0x68
    unsigned int CostumeLeftHand;    // +0x78  request +0x1c (.text:06cf7d9d)       -> Parms +0x6c
    unsigned int CostumeRightHand;   // +0x7c  request +0x20 (.text:06cf7d96)       -> Parms +0x70
    unsigned int CostumeAccessory[8];// +0x80  request +0x24, `count` dwords (.text:06cf7dd0) -> Parms +0x74
    int CostumeAccessoryCount;       // +0xa0  request +0x44 (.text:06cf7da4)       -> Parms +0x94
    int VibeGroupId;                 // +0xa4  request +0x10 (.text:06cf7ded)       -> Parms +0x60
    unsigned int VoiceId;            // +0xa8  request +0x48 (.text:06cf7de1)       -> Parms +0x98
    unsigned int DockingBaseId;      // +0xac  request +0x0c (.text:06cf7eb6)       -> Parms +0x5c
    // An INTEGER: FUN_06d0a880 FILDs the request copy (.text:06d0aa6d) into
    // Parms +0x0c HitPts.
    int HitPts;                      // +0xb0  request +0x68 (.text:06cf7ed4)
    // +0xb4  not read by Create; Content's NPC ship spawn (NpcShip.cpp) writes 0.
    unsigned int Field_0xb4;
    // +0xb8  request +0xe8 (.text:06cf7ee1); when `& 0x3fffffff` is nonzero, FUN_06d0a880
    // passes it to FUN_06cef3b0(Id, 1) (.text:06d0ad30 / 06d0ad36), the mark list
    // pub::Player::MarkObj (06d54970) adds its client id to: the player the ship is marked for.
    unsigned int MarkPlayerId;
    unsigned int ReputationRank;     // +0xbc  request +0xb4 (.text:06cf7de7)       -> Parms +0xf0 (CShip::CreateParms::Rank)
};

// SolarInfo -- pub::SpaceObj::CreateSolar's argument
// (?CreateSolar@SpaceObj@pub@@YAHAAIABUSolarInfo@12@@Z, .text:06cf80d0).
//
// Placed from the loads in pub::SpaceObj::CreateSolar (ESI = &info, .text:06cf80ed)
// into its CSolarCreateInfo request (server_createinfo.h) and from there by
// FUN_06d0a200 into CSolar::CreateParms (FUN_06d0a770 ctor). Retail offsets; the last
// load is +0xcc.
struct SolarInfo {
    // +0x00  not read by CreateSolar. Both Content writers store the creating mission's
    // controller id (MissionObject.cpp solar spawn, RandomMissionMachine.cpp), as ShipInfo's
    // +0x00 holds the controller id pub::SpaceObj::Create checks (.text:06cf7f60).
    unsigned int ControllerId;
    unsigned int SolarArchID;        // +0x04  request +0x08 (.text:06cf818c), Archetype::GetSolar (.text:06cf8254)
    unsigned int SystemID;           // +0x08  FUN_06d00280 (.text:06cf80f4)
    Vector Position;                 // +0x0c  request +0x4c (.text:06cf8200)       -> Parms +0x10
    // MatrixToQuaternion on &info+0x18 (.text:06cf8234 / 06cf823e) -> request
    // +0x58; FUN_06d0a200 QuaternionToMatrix (.text:06d0a3b0) -> Parms +0x1c.
    Matrix Orientation;              // +0x18 .. +0x3b
    unsigned int LoadoutID;          // +0x3c  request +0x6c (.text:06cf8263); Loadout::Get in FUN_06d0a200 (.text:06d0a455)
    unsigned int CostumeHead;        // +0x40  request +0x14 (.text:06cf81b5)       -> Parms +0x64
    unsigned int CostumeBody;        // +0x44  request +0x18 (.text:06cf81ae)       -> Parms +0x68
    unsigned int CostumeLeftHand;    // +0x48  request +0x1c (.text:06cf81c2)       -> Parms +0x6c
    unsigned int CostumeRightHand;   // +0x4c  request +0x20 (.text:06cf81bb)       -> Parms +0x70
    unsigned int CostumeAccessory[8];// +0x50  request +0x24, `count` dwords (.text:06cf81e8) -> Parms +0x74
    int CostumeAccessoryCount;       // +0x70  request +0x44 (.text:06cf81c9)       -> Parms +0x94
    int VibeGroupId;                 // +0x74  request +0x10 (.text:06cf81fd)       -> Parms +0x60
    unsigned int VoiceId;            // +0x78  request +0x48 (.text:06cf81fa)       -> Parms +0x98
    unsigned int DockingBaseId;      // +0x7c  request +0x0c (.text:06cf8189)       -> Parms +0x5c and +0xe0
    unsigned char VisitValue;        // +0x80  byte, request +0xb4 (.text:06cf8294) -> Parms +0xbc (VisitValue)
    unsigned char Pad81[3];          // +0x81
    // An INTEGER: FUN_06d0a200 FILDs the request copy (.text:06d0a352).
    int HitPts;                      // +0x84  request +0x68 (.text:06cf819a)
    // Inline, not a pointer: CreateSolar tests the first byte (.text:06cf815a,
    // empty -> -4) and passes &info+0x88 to CreateID (.text:06cf8179). Nothing
    // in either function bounds it; 0x44 is the gap up to the next read.
    char Nickname[0x44];             // +0x88 .. +0xcb
    // +0xcc  request +0xbc (.text:06cf828e); FUN_06d0a200 calls FUN_06cef3b0(value, 1)
    // when `value & 0x3fffffff` (.text:06d0a53d .. 06d0a54f): the mark list
    // pub::Player::MarkObj (06d54970) adds its client id to.
    unsigned int MarkPlayerId;
};

// LootInfo — passed to pub::SpaceObj::CreateLoot (.text:06cf84a0), which copies it
// field by field into one 0x6c-byte FLootRecord (server_gameobject.h) and hands that to
// StarSystem::create_loot (FUN_06d08a40) in mode 4. The names follow the record slot each
// field lands in (.text:06cf8503-06cf85c6); highest offset read is +0x64.
struct LootInfo {
    unsigned int SystemID;          // +0x00  FUN_06d00280 system lookup (missing -> -4)
    unsigned int EquipArchID;       // +0x04  Archetype::GetEquipment -> record +0x0c (contents)
    unsigned int NameId;            // +0x08  -> record +0x1c (CLoot NameId)
    unsigned int OwnerObjId;        // +0x0C  create_loot's SourceObjId (CLoot OwnerId)
    Vector Velocity;                // +0x10  -> record +0x20
    Vector Position;                // +0x1C  -> record +0x2c
    Matrix Orientation;             // +0x28  -> record +0x38 (REP MOVSD ECX=9)
    Vector AngularVelocity;         // +0x4C  -> record +0x5c
    unsigned int Units;             // +0x58  -> record +0x14
    float HitPtsScale;              // +0x5C  times the loot appearance's hit points -> record +0x08
    unsigned char bCanAiTractor;    // +0x60  -> record +0x68
    unsigned char bMission;         // +0x61  -> record +0x69
    unsigned char Pad62[2];         // +0x62
    unsigned int MarkForPlayer;     // +0x64  -> record +0x18 (mark_for_player when non-zero)
};
static_assert(sizeof(LootInfo) == 0x68, "LootInfo: last field read at +0x64");

// DestroyType and EQUIPMENT_RTYPE are GLOBAL enums -- declared above, before
// namespace pub.

// EQInfo — equipment activation descriptor passed to ActivateEquipment.
// Offsets from pub::SpaceObj::ActivateEquipment (0x193f0) and
// pub::SpaceObj::Dock (0x1a6e0), which between them read +0x00, +0x04 (a single BYTE,
// the on/off flag Dock derives from DOCK_HOST_RESPONSE), +0x08, +0x0c and +0x10.
// The struct is 0x20 bytes: +0x10 is an EquipDescVector-sized block (see CargoOut).
// Type values, from ActivateEquipment's switch (.text:06cf95d8); the names are the port's.
enum EQInfoType {
    EQINFO_TYPE_CRUISE = 0,          // IObjAffect toggle_cruise (+0x50)
    EQINFO_TYPE_SCAN_CARGO = 1,      // IObjInspect scan_cargo (+0x120)
    EQINFO_TYPE_CLOAK = 2,           // toggle_item on FindFirst(EquipTypeCloak)
    EQINFO_TYPE_DOCKING_LIGHTS = 3,  // toggle_item on every light whose archetype is a docking light
};
struct EQInfo {
    unsigned int Type;              // +0x00: EQInfoType
    unsigned short EquipId;         // +0x04: low byte is the on/off flag Dock writes
    unsigned short Pad06;           // +0x06: alignment
    unsigned int Count;             // +0x08: case 0 requires this to be exactly 1
    // +0x0c: case 1 only: the object whose cargo is scanned. 0 skips the case; otherwise
    // FUN_06d00670 looks it up (missing -> -5) and IObjInspect::scan_cargo gets it.
    unsigned int TargetObjId;
    // +0x10: case 1 hands &CargoOut to the object's IObjInspect::scan_cargo (+0x120,
    // `ADD ESI,0x10; PUSH ESI` at .text:06cf9490) as its EquipDescVector& out-parameter,
    // so the field is 0x10 bytes (sizeof(EquipDescVector)). It is raw storage rather than
    // an EquipDescVector member because
    // retail EQInfo is trivial: pub::SpaceObj::Dock builds one on its stack with no
    // constructor or destructor call (.text:06cfa755 .. 06cfa77b).
    unsigned int CargoOut[4];       // +0x10 .. +0x1f
};
static_assert(sizeof(EQInfo) == 0x20, "EQInfo: +0x10 carries a 0x10-byte EquipDescVector");

// Server.dll .text:06cf93f0.
SERVER_API EQUIPMENT_RTYPE __cdecl ActivateEquipment(unsigned int const& ObjId, EQInfo* Info);

// TerminalInfo — output struct for GetTerminalInfo; 0x24 bytes.
struct TerminalInfo {
    char Name[0x20];                // +0x00: terminal name string (max 31 chars + null)
    unsigned int Value;             // +0x20: terminal value dword
};

// Server.dll 0x19900. Value is the docking sphere's type.
SERVER_API int __cdecl GetTerminalInfo(const unsigned int& ObjId, int Index, TerminalInfo& OutInfo);

// PosSelectionType — position selection algorithm for GetEmptyPos
enum PosSelectionType {
    POS_SEL_XY_PLANE = 0,
    POS_SEL_XZ_PLANE = 1,
    POS_SEL_YZ_PLANE = 2,
    POS_SEL_3D_VOLUME = 3,
};

// Server.dll .text:06cf9bd0.
SERVER_API int __cdecl GetEmptyPos(const unsigned int& system_id_ref, const Transform& base_transform,
                                   const float& start_radius, const float& clearance,
                                   const PosSelectionType& selection_type, Vector& out_pos);

// Server.dll .text:06cfa060.
SERVER_API int __cdecl IsPosEmpty(const unsigned int& system_id_ref, const Vector& pos,
                                  const float& radius, bool& out_empty);

// Server.dll .text:06cf7c90.
SERVER_API int __cdecl Create(unsigned int& OutId, ShipInfo const& Info);

// Server.dll .text:06cf80d0.
SERVER_API int __cdecl CreateSolar(unsigned int& OutId, SolarInfo const& Info);

// Server.dll .text:06cf84a0.
SERVER_API int __cdecl CreateLoot(unsigned int& OutId, LootInfo const& Info);

SERVER_API int __cdecl GetHealth(const unsigned int& obj_id_ref, float& out_current, float& out_max);

SERVER_API int __cdecl Destroy(unsigned int ObjId, DestroyType Type);

} // namespace SpaceObj
} // namespace pub
