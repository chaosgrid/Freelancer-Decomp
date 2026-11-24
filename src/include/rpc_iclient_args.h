// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_iclient_args.h - argument structs Server.dll hands to IClient slots.
//
// IClient's slots take every argument as a 4-byte placeholder (rpc_iclient.h); the
// structs below are what the pointer-typed ones point at. Server.dll builds them on
// its stack and rpclocal.dll's senders read them field by field, so each is declared
// once, here.
#pragma once

#include "common_reputation.h"   // Reputation::Relation
#include "common_text.h"         // FmtStr
#include "common_ini.h"          // Vector (FPayloadServerSetDirective)
#include "common_physys.h"       // Quaternion (FPayloadServerSystemSwitchIn)

// ---------------------------------------------------------------------------
// IClient slot 66 (+0x108), ch2 op 0x29 FLPACKET_SERVER_SETREPUTATION.
//
// Server.dll's IServerImpl::CharacterSelect builds it on its stack: zeroes, +0x00 = the
// slot's rep vibe, +0x10 = operator new(Reputation::group_count() * 8), then
// Reputation::Vibe::Get(+0x00, +0x08, +0x0c, +0x14, +0x10, +0x18, +0x30, &name) and
// wcscpy(+0x48, name).
//
// rpclocal's sender (0x06d88d70) serializes +0x00, +0x04 and +0x08; +0x0c is filled by Get
// and read by nothing. Retail quirk: CharacterSelect never writes +0x04, so it ships
// whatever its stack held there.
// ---------------------------------------------------------------------------
struct FServerSetReputationInfo {
    int VibeHandle;                         // +0x00
    float Attitude;                         // +0x04  Reputation::Vibe::AttitudeTowards out-param
    unsigned int Affiliation;                // +0x08  Reputation::Vibe::Get out-param 2
    unsigned int Rank;                // +0x0c  Get out-param 3 -- never reaches the wire
    ::Reputation::Relation* Relations;      // +0x10  Get out-param 5
    unsigned char RelationCount;            // +0x14  Get out-param 4 (a byte on both sides)
    unsigned char Pad_0x15[3];              // +0x15
    FmtStr NameA;                           // +0x18  Get out-param 6
    FmtStr NameB;                           // +0x30  Get out-param 7
    wchar_t DisplayName[24];                // +0x48  Get out-param 8, wcscpy'd in by the caller
};
static_assert(sizeof(FServerSetReputationInfo) == 0x78,
              "SETREPUTATION argument struct 0x78");

// ---------------------------------------------------------------------------
// IClient slot 14 (+0x38), ch2 op 0x08 FLPACKET_SERVER_CHARSELECTVERIFIED.
//
// IServerImpl::CharacterSelect builds it: +0x00 = the SETREPUTATION struct's vibe handle,
// +0x08 = the server clock (FUN_06d447b0). +0x04 is never written. rpclocal's sender reads
// dwords 0, 2 and 3 (rpc_payload_ch2.cpp, MarshalCharSelectVerified).
// ---------------------------------------------------------------------------
struct FServerCharSelectVerifiedInfo {
    int VibeHandle;                         // +0x00
    unsigned int Unused_0x04;               // +0x04  not written by the sender's caller
    double ServerTime;                      // +0x08
};
static_assert(sizeof(FServerCharSelectVerifiedInfo) == 0x10,
              "CHARSELECTVERIFIED argument struct 0x10");

// ---------------------------------------------------------------------------
// IClient slot 56 (+0x0e0), ch2 op 0x0a FLPACKET_SERVER_ACTIVATEOBJECT.
//
// The 0x0c-byte argument is the wire payload: rpclocal's sender (0x06d89770) copies the
// three fields into packet+0x20 and its receiver (caseD_a) rebuilds them. Server.dll's
// StarSystem::add_player builds one per set entry of CObject::get_activate_state (vtable
// +0xb8) in its multiplayer solar walk: {CObject::Id, 1, entry index}.
// ---------------------------------------------------------------------------
struct FPayloadServerActivateObject {
    unsigned int ObjectId;      // +0x00
    unsigned char bActivate;    // +0x04
    unsigned char Pad_0x05[3];
    unsigned int ActivateParam;    // +0x08  index into CObject::get_activate_state's vector
};
static_assert(sizeof(FPayloadServerActivateObject) == 0x0c, "ACTIVATEOBJECT payload 0x0c");

// ---------------------------------------------------------------------------
// IClient slot 58 (+0x0e8), ch2 op 0x22 FLPACKET_SERVER_SYSTEM_SWITCH_IN. Registry
// size 0x44, payload 0x24. Sender 0x06d8a8b0 writes the fields in struct order; receiver
// caseD_22 spills all nine dwords into the file-static block at .data:0x06dbb91c and
// passes its address.
//
// Its only builder is Server.dll FUN_06d0cc00 (StarSystem::arrive_ship): the ship's id,
// the jump object's get_type_bits() (0 when there is none), *Pos, and
// MatrixToQuaternion(*Orient).
// ---------------------------------------------------------------------------
struct FPayloadServerSystemSwitchIn {
    unsigned int ShipId;     // +0x00  object id              -> .data:0x06dbb91c
    unsigned int JumpObjTypeBits;     // +0x04  jump object type bits  -> .data:0x06dbb920
    Vector Position;             // +0x08  -> 0x06dbb924/928/92c
    Quaternion Orientation;      // +0x14  -> 0x06dbb930/934/938/93c
};
static_assert(sizeof(FPayloadServerSystemSwitchIn) == 0x24, "SYSTEM_SWITCH_IN payload 0x24");

// ---------------------------------------------------------------------------
// IClient slot 64 (+0x100), ch2 op 0x38 FLPACKET_SERVER_ITEMTRACTORED. Registry
// size 0x2c, payload 0x0c. Sender 0x06d88c60 and receiver caseD_38 copy it straight.
// Server.dll's only builder is StarSystem::tractor_loot (FUN_06d0ed40); UnitsTaken is the
// low word of the count.
// ---------------------------------------------------------------------------
struct FPayloadServerItemTractored {
    unsigned int LootId;     // +0x00  loot object id
    unsigned int ShipId;     // +0x04  tractoring ship id
    unsigned short TractorEquipId;   // +0x08  tractor equipment id
    unsigned short UnitsTaken;   // +0x0a  units taken
};
static_assert(sizeof(FPayloadServerItemTractored) == 0x0c, "ITEMTRACTORED payload 0x0c");

// ---------------------------------------------------------------------------
// IClient slot 65 (+0x104), ch2 op 0x51 FLPACKET_SERVER_USE_ITEM. Registry size
// 0x28, payload 0x08. Sender 0x06d88cc0 and receiver caseD_51 copy it straight. Server.dll's
// only builder is StarSystem::use_item (FUN_06d0efe0); Count is the low word.
// ---------------------------------------------------------------------------
struct FPayloadServerUseItem {
    unsigned int ObjectId;     // +0x00  object id
    unsigned short EquipId;   // +0x04  equipment id
    unsigned short Count;   // +0x06  count
};
static_assert(sizeof(FPayloadServerUseItem) == 0x08, "USE_ITEM payload 0x08");

// ---------------------------------------------------------------------------
// IClient slot 60 (+0x0f0), ch2 op 0x07 FLPACKET_SERVER_LAUNCH -- the argument
// struct (0x28). rpclocal's sender (0x06d8a080) reorders it into
// FPayloadServerLaunch (rpc_payloads.h) and the receiver (caseD_7) puts every dword back.
// Server.dll's IServerImpl::PlayerLaunch builds it: the ship id, the object launched from
// and its launch_pos index (Server_GetLaunchPosition's outputs, -1 without one), the
// request's orientation as is, and the launch position minus the rotated HpMount offset.
// ---------------------------------------------------------------------------
struct FServerLaunchInfo {
    unsigned int ShipId;        // +0x00  ship id
    unsigned int LaunchObjId;   // +0x04  launch object id
    unsigned int LaunchPosIndex; // +0x08  launch_pos index (-1: none)
    Quaternion Orientation;     // +0x0c
    Vector Position;            // +0x1c
};
static_assert(sizeof(FServerLaunchInfo) == 0x28, "LAUNCH arg struct 0x28");

// ---------------------------------------------------------------------------
// IClient slot 54 (+0x0d8), ch1 op 0x01 FLPACKET_COMMON_UPDATEOBJECT. The argument
// struct (0x2c); the wire payload FPayloadCommonUpdateObject (rpclocal) holds the
// same fields in another order. Server.dll's only builder is StarSystem::sp_obj_update
// (FUN_06d0bbc0), copying the player's SSPObjUpdateInfo: +0x20 is its double timestamp
// narrowed to float, +0x24 a raw dword copy of info +0x28 (the wire and the client read
// it as a float), +0x28 the byte at info +0x2c.
// ---------------------------------------------------------------------------
struct FCommonUpdateObjectInfo {
    unsigned int ObjectId;       // +0x00
    Quaternion Orientation;      // +0x04
    Vector Position;             // +0x14
    float Timestamp;            // +0x20  timestamp
    float Throttle;             // +0x24  SSPObjUpdateInfo::ThrottleBits, bit copy
    unsigned char State;        // +0x28  SSPObjUpdateInfo::State (the ship's BehaviorIndex)
    unsigned char Pad_0x29[3];
};
static_assert(sizeof(FCommonUpdateObjectInfo) == 0x2c, "UPDATEOBJECT arg struct 0x2c");

// ---------------------------------------------------------------------------
// IClient slot 100 (+0x190), ch2 op 0x4e FLPACKET_SERVER_SETDIRECTIVE. Registry size
// 0xb4, payload 0x94.
//
// The argument is the wire payload: rpclocal's sender (0x06d8a480) writes the same offsets
// and its receiver (caseD_4e) copies 0x25 dwords from packet+0x20. Server.dll's
// pub::AI::SubmitDirective fills one when the target ship belongs to a player and calls
// SendServerSetDirective(&OwnerPlayer, &Info).
//
// Default construction is inlined at every one of SubmitDirective's six case entries (and
// is rpclocal's FUN_06d84100): +0x18 = -1, +0x40 = 3, every other field 0, +0x00 unseeded.
// +0x00 is the directive kind SubmitDirective stores last, and each kind fills only its own
// fields from the op (the op offsets are common_pub.h's):
//   0 DirectiveCancelOp  nothing
//   1 DirectiveDelayOp   +0x0c = op+0x0c
//   2 DirectiveDockOp    +0x10 = op+0x0c, +0x14 = op+0x10, byte +0x08 = op+0x14,
//                        +0x18 = op+0x18, +0x1c = op+0x1c, +0x20 = op+0x20, +0x24 = op+0x24
//   3 DirectiveFollowOp  +0x28 = op+0x0c, +0x2c = op+0x10, +0x30 = op+0x14..0x1c,
//                        +0x3c = op+0x20
//   4 DirectiveGotoOp    +0x40 = op+0x0c, +0x44 = op+0x10..0x18, +0x50 = op+0x1c,
//                        +0x54/+0x60/+0x6c/+0x78 = op+0x20/+0x2c/+0x38/+0x44 (3 dwords each),
//                        +0x84 = op+0x50, +0x88 = op+0x54, bytes +0x8c..+0x8f = op+0x58..0x5b,
//                        +0x1c = op+0x5c, +0x20 = op+0x60, +0x24 = op+0x64, byte +0x90 = op+0x6c
//   5 DirectiveIdleOp    nothing
// The types below are the ones those source fields carry. The four 3-dword blocks at +0x54..
// +0x78 are seeded by Server's 3-dword setter FUN_06ce7c10 and filled from twelve uint32
// DirectiveGotoOp fields whose meaning is not recovered, so they stay three dwords.
// ---------------------------------------------------------------------------
struct FRpcDirectiveBlock {
    unsigned int Field_0x00;        // +0x00
    unsigned int Field_0x04;        // +0x04
    unsigned int Field_0x08;        // +0x08
};
static_assert(sizeof(FRpcDirectiveBlock) == 0x0c, "FUN_06d8f510 moves exactly three dwords");

struct FPayloadServerSetDirective {
    unsigned int DirectiveKind;        // +0x00  directive kind 0..5 (see above); never seeded
    unsigned int Field_0x04;        // +0x04  always 0 from SubmitDirective
    unsigned char bDockApproachCleared;       // +0x08  a byte on both sides (06d8a4b4); dock op+0x14
    unsigned char Pad_0x09[3];      // +0x09  retail leaves these three bytes untouched
    float DelaySeconds;               // +0x0c  delay op+0x0c (DelaySeconds)
    unsigned int DockObjectId;        // +0x10  dock op+0x0c (DockObjectId)
    int DockSecondaryObjectId;                 // +0x14  dock op+0x10
    int DockPortIndex;                 // +0x18  ctor seed -1; dock op+0x18 (DockPortIndex)
    unsigned int ProxTargetId;        // +0x1c  dock op+0x1c / goto op+0x5c
    float ProxNearDist;               // +0x20  dock op+0x20 / goto op+0x60
    float ProxFarDist;               // +0x24  dock op+0x24 / goto op+0x64
    unsigned int FollowTargetId;        // +0x28  follow op+0x0c (TargetId)
    float FollowDistance;               // +0x2c  follow op+0x10 (FollowDistance)
    Vector FollowOffset;              // +0x30  follow op+0x14..0x1c
    float FollowEngageRange;        // +0x3c  follow op+0x20 (EngageRange)
    int GotoMode;                 // +0x40  ctor seed 3; goto op+0x0c
    Vector GotoDestination;              // +0x44  goto op+0x10 (Destination)
    int GotoTargetId;                 // +0x50  goto op+0x1c
    FRpcDirectiveBlock SplineP1;  // +0x54  goto op+0x20..0x28
    FRpcDirectiveBlock SplineP2;  // +0x60  goto op+0x2c..0x34
    FRpcDirectiveBlock SplineP0;  // +0x6c  goto op+0x38..0x40
    FRpcDirectiveBlock SplineP3;  // +0x78  goto op+0x44..0x4c
    float GotoRange;               // +0x84  goto op+0x50
    float GotoSpeed;               // +0x88  goto op+0x54
    unsigned char bGotoFlyThrough;  // +0x8c  goto op+0x58 (bFlyThrough)
    unsigned char Field_0x8d;       // +0x8d  goto op+0x59 (Field_59, meaning unknown)
    unsigned char bGotoCruise;       // +0x8e  goto op+0x5a
    unsigned char bGotoNoCruise;       // +0x8f  goto op+0x5b
    unsigned char bGotoDisableAvoidance; // +0x90  goto op+0x6c (bDisableAvoidance)
    unsigned char Pad_0x91[3];      // +0x91  swept up by the receiver's REP MOVSD
};
static_assert(offsetof(FPayloadServerSetDirective, FollowOffset) == 0x30, "SETDIRECTIVE +0x30");
static_assert(offsetof(FPayloadServerSetDirective, GotoDestination) == 0x44, "SETDIRECTIVE +0x44");
static_assert(offsetof(FPayloadServerSetDirective, SplineP3) == 0x78, "SETDIRECTIVE +0x78");
static_assert(offsetof(FPayloadServerSetDirective, bGotoDisableAvoidance) == 0x90, "SETDIRECTIVE +0x90");
static_assert(sizeof(FPayloadServerSetDirective) == 0x94, "SETDIRECTIVE payload 0x94");

// ---------------------------------------------------------------------------
// IClient slot 70 (+0x118), ch2 op 0x45 FLPACKET_SERVER_SET_MISSION_MESSAGE.
//
// Retail's own name (Server.dll exports CPlayerGroup::SetMissionMessage taking it).
// pub::Player::DisplayMissionMessage (.text:06d55830) builds it: the FmtStr default-
// constructed inline then assigned from the caller's text, +0x18 = the MissionMessageType.
// rpclocal's sender (0x06d8af60) flattens +0x00 and ships +0x18; rpclocal keeps its own
// spelling through the typedef below.
// ---------------------------------------------------------------------------
struct CSetMissionMessage {
    FmtStr Text;                    // +0x00
    unsigned int Type;              // +0x18  MissionMessageType
};
static_assert(sizeof(CSetMissionMessage) == 0x1c, "SET_MISSION_MESSAGE argument struct 0x1c");
typedef CSetMissionMessage FServerSetMissionMessageInfo;

// ---------------------------------------------------------------------------
// IClient slot 71 (+0x11c), ch2 op 0x34 FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE.
//
// pub::Player::ReplaceMissionObjective (.text:06d55c10) builds it: *MissionId, the
// objective's Description, its Type and the third argument as a byte. rpclocal's sender
// (0x06d8ad90) ships +0x00, the byte at +0x20 and the flattened +0x04; it never reads +0x1c.
// ---------------------------------------------------------------------------
struct FServerReplaceMissionObjectiveInfo {
    unsigned int MissionId;        // +0x00  mission id
    FmtStr Text;                    // +0x04  MissionObjective::Description
    unsigned int ObjectiveType;     // +0x1c  MissionObjective::Type (Server writes it, rpclocal never reads it)
    unsigned char ObjectiveIndex;            // +0x20  ReplaceMissionObjective's third argument, as a byte
    unsigned char Pad_0x21[3];      // +0x21
};
static_assert(sizeof(FServerReplaceMissionObjectiveInfo) == 0x24,
              "REPLACEMISSIONOBJECTIVE argument struct 0x24");

// ---------------------------------------------------------------------------
// IClient slot 73 (+0x124), ch2 op 0x33 FLPACKET_SERVER_SETMISSIONOBJECTIVESTATE.
//
// pub::Player::SetMissionObjectiveState (.text:06d55d20) builds it: *MissionId, the third
// argument truncated to a byte, the fourth argument. rpclocal's sender (0x06d8ac40) reads
// the dword, the byte and the low word of +0x08.
// ---------------------------------------------------------------------------
struct FServerMissionObjectiveStateInfo {
    unsigned int MissionId;    // +0x00  mission id
    unsigned char ObjectiveIndex;   // +0x04  objective index (third argument, as a byte)
    unsigned char Pad_0x05[3];
    unsigned int State;    // +0x08  the fourth argument; receiver zero-extends the word
};
static_assert(sizeof(FServerMissionObjectiveStateInfo) == 0x0c, "SETMISSIONOBJECTIVESTATE arg struct 0x0c");

// ---------------------------------------------------------------------------
// IClient slot 87 (+0x15c), ch2 op 0x41 FLPACKET_SERVER_BURNFUSE.
//
// EqObj::light_fuse (.text:06cec4d0) builds it: the igniting source id, the object's own
// id, the fuse's ID_String value, the sub-object, the ignition time, the lifetime and
// bLit = 1; EqObj::unlight_fuse (.text:06cec7f0) builds the same with bLit = 0.
// StarSystem FUN_06d0e750 reads +0x04 for its visibility test and hands the record to
// IClient +0x15c; rpclocal's sender (0x06d8c170, MarshalBurnFuse) copies +0x00 .. +0x18.
// ---------------------------------------------------------------------------
struct FServerBurnFuseInfo {
    unsigned int SourceId;      // +0x00
    unsigned int ObjectId;      // +0x04
    unsigned int FuseId;        // +0x08  ID_String value
    unsigned short SubObjId;    // +0x0c
    unsigned short Pad_0x0e;
    float AtT;                  // +0x10
    float Lifetime;             // +0x14
    unsigned char bLit;         // +0x18
    unsigned char Pad_0x19[3];
};
static_assert(sizeof(FServerBurnFuseInfo) == 0x1c, "BURNFUSE argument struct 0x1c");

// ---------------------------------------------------------------------------
// IClient slot 72 (+0x120), ch2 op 0x35 FLPACKET_SERVER_SETMISSIONOBJECTIVES.
// The sender (rpclocal 0x06d8b020) takes the client id by reference and reads every field
// of this record. Built by pub::Player::SetMissionObjectives (.text:06d55960),
// CPlayerGroup::SetMissionObjectives (.text:06d5bfd0) and, empty, CPlayerGroup::DelMember.
// ---------------------------------------------------------------------------
// Deviation: the receiver rebuilds the element array with a VC6 vector-new (count cookie
// in the first dword); the port uses a plain array plus the struct's Count.
struct FServerMissionObjectiveEntry {
    FmtStr Text;                    // +0x00
    unsigned int State;             // +0x18  a BYTE on the wire, MOVZX'd to a dword here
};
static_assert(sizeof(FServerMissionObjectiveEntry) == 0x1c,
              "objective entry is 0x1c: FmtStr plus one dword");

struct FServerSetMissionObjectivesInfo {
    unsigned int MissionId;                // +0x00
    FServerMissionObjectiveEntry* Entries;  // +0x04
    unsigned int Count;                     // +0x08
    FmtStr TextA;                           // +0x0c
    FmtStr TextB;                           // +0x24
    unsigned char MissionType;               // +0x3c
    unsigned char Pad_0x3d[3];              // +0x3d
};
static_assert(sizeof(FServerSetMissionObjectivesInfo) == 0x40,
              "SETMISSIONOBJECTIVES argument struct 0x40");

// ---------------------------------------------------------------------------
// IClient slot 62 (+0xf8), ch2 op 0x32 FLPACKET_SERVER_REQUESTSPACESCRIPT.
//
// Built by Server.dll's pub::SpaceObj::RequestSpaceScript (.text:06cf9b40): the object id,
// the name (safe_strcpy, 0x104), the position, *param_3; then IClient+0xf8(param_4,
// &record). rpclocal's sender 0x06d8aa40 reads the same offsets; the client's handler
// LAB_0053d9d0 reads CameraMode as 1 = keep the camera transform, 2 = identity orientation
// at the position.
// ---------------------------------------------------------------------------
// 0x104: Server.dll's safe_strcpy bound and the rpclocal receiver's string local.
const unsigned int kRequestSpaceScriptNameSize = 0x104;

struct FServerRequestSpaceScriptInfo {
    unsigned int ObjectId;                       // +0x000  object id
    char Name[kRequestSpaceScriptNameSize];         // +0x004  script name
    unsigned int PosXBits;                       // +0x108  position x (float bits)
    unsigned int PosYBits;                       // +0x10c  position y (float bits)
    unsigned int PosZBits;                       // +0x110  position z (float bits)
    unsigned int CameraMode;                       // +0x114  1 or 2; see rpc_payloads.h remap
};
static_assert(sizeof(FServerRequestSpaceScriptInfo) == 0x118, "REQUESTSPACESCRIPT argument struct 0x118");

// ---------------------------------------------------------------------------
// IClient slot 82 (+0x148), ch2 op 0x2b FLPACKET_SERVER_SETGROUPFEELINGS.
//
// rpclocal's receiver caseD_2b (0x06d815bb) strncpy()s the name to +0x00 and stores the
// relation array and its count (a byte); its sender 0x06d8a5c0 reads the same three fields.
// freelancer.exe's FUN_0053da90 copies the name into a TString<16> and passes the relations
// to Reputation::set.
// ---------------------------------------------------------------------------
struct FServerSetGroupFeelingsInfo {
    char Name[0x10];                        // +0x00
    ::Reputation::Relation* Relations;      // +0x10
    unsigned char RelationCount;            // +0x14
    unsigned char Pad_0x15[3];              // +0x15
};
static_assert(sizeof(FServerSetGroupFeelingsInfo) == 0x18,
              "SETGROUPFEELINGS argument struct 0x18");

// ---------------------------------------------------------------------------
// IClient::SendServerSystemSwitchOut (slot 57, +0x0e4), ch2 op 0x21
// FLPACKET_SERVER_SYSTEM_SWITCH_OUT. Registry size 0x28, payload 0x08.
//
// rpclocal's sender 0x06d8a860 copies the two dwords to packet+0x20 / +0x24 and its
// receiver caseD_21 spills them into a file-static pair. Server.dll's builder
// (FUN_06cf6e60) puts *ObjId at +0x00 and *Destination at +0x04; freelancer.exe's
// LAB_0053d500 reads them as the ship and the gate. The payload is this struct as it
// stands.
// ---------------------------------------------------------------------------
struct FServerSystemSwitchOutInfo {
    unsigned int ShipId;        // +0x00
    unsigned int GateId;        // +0x04  the jump object the ship leaves through
};
static_assert(sizeof(FServerSystemSwitchOutInfo) == 0x08, "SYSTEM_SWITCH_OUT argument struct 0x08");

// ---------------------------------------------------------------------------
// IClient::SendServerLand (slot 59, +0x0ec), ch2 op 0x0b FLPACKET_SERVER_LAND.
// Registry size 0x2c, payload 0x0c.
//
// rpclocal's sender 0x06d8a990 copies the three dwords to packet+0x20.. in order.
// Server.dll's builder (FUN_06cf6fc0) fills {*ObjId, *Arg2, *Arg3}; freelancer.exe's
// FUN_0053d720 looks +0x00 up as the ship, +0x04 as the dock object (its CEqObj +0x164 is
// the docking base) and hands &+0x08 to FUN_00543b40 as the base id. The payload is this
// struct as it stands.
// ---------------------------------------------------------------------------
struct FServerLandInfo {
    unsigned int ShipId;        // +0x00
    unsigned int DockId;        // +0x04  the object docked with
    unsigned int BaseId;        // +0x08
};
static_assert(sizeof(FServerLandInfo) == 0x0c, "LAND argument struct 0x0c");

// ===========================================================================
// Argument structs Server.dll builds and freelancer.exe / RemoteClient.dll read. The ones
// whose layout is the wire payload keep their FPayload* name; rpclocal's codecs use them
// for both.
// ===========================================================================

// ---------------------------------------------------------------------------
// IClient::SendServerDestroyObject (slot 55, +0x0dc), ch2 op 0x06
// FLPACKET_SERVER_DESTROYOBJECT. Sender 0x06d89140 reads the dword at +0x00 and the byte
// at +0x04 through its second argument; receiver caseD_6 at 0x06d81f6a stores them into
// .data:0x06dbb824 / 0x06dbb828 and passes that address. The wire shape is
// FPayloadServerDestroyObject (rpc_payloads.h).
// ---------------------------------------------------------------------------
struct FServerDestroyObjectInfo {
    unsigned int ObjectId;      // +0x00
    unsigned char DestroyType;  // +0x04  0 = vanish / out-of-range, 1 = destroyed (explosion/death camera)
    unsigned char Pad_0x05[3];
};
static_assert(sizeof(FServerDestroyObjectInfo) == 0x08, "DESTROYOBJECT arg struct 0x08");

// ---------------------------------------------------------------------------
// IClient::SendServerRepairObject (slot 84, +0x150), ch2 op 0x3c
// FLPACKET_SERVER_REPAIROBJECT. Sender 0x06d8bfe0 reads dword +0x00, word +0x04 (MOVZX)
// and dword +0x08; receiver caseD_3c at 0x06d82b55 writes the word back as a word and
// leaves +0x06 untouched. The wire shape is FPayloadServerRepairObject (rpc_payloads.h).
// ---------------------------------------------------------------------------
struct FServerRepairObjectInfo {
    unsigned int ObjectId;
    unsigned short EquipId;
    unsigned short Pad_0x06;    // the receiver never writes this half
    unsigned int Points;
};
static_assert(sizeof(FServerRepairObjectInfo) == 0x0c, "REPAIROBJECT arg struct 0x0c");

// ---------------------------------------------------------------------------
// IClient::SendServerCfgInterfaceNotification (slot 76, +0x130), ch2 op 0x47
// FLPACKET_SERVER_CFGINTERFACENOTIFICATION, size 0x2c -> payload 0x0c.
// Sender 0x06d8ad30, receiver caseD_47 at 0x06d8244f, which normalises the flag through
// TEST/SETNZ before storing it. The argument is the wire payload. Server.dll's builder
// pub::Player::CfgInterfaceNotification (.text:06d54a10) writes an int into +0x04;
// freelancer.exe reads +0x04 as float bits -- TODO(type).
// ---------------------------------------------------------------------------
struct FPayloadServerCfgInterfaceNotification {
    unsigned int NotifType;
    unsigned int Param;
    unsigned char bEnable;
    unsigned char Pad_0x09[3];
};
static_assert(sizeof(FPayloadServerCfgInterfaceNotification) == 0x0c, "CFGINTERFACENOTIFICATION payload 0x0c");

// ---------------------------------------------------------------------------
// IClient::SendServerSetCamera (slot 88, +0x160), ch2 op 0x4b FLPACKET_SERVER_SET_CAMERA,
// size 0x44 -> payload 0x24. Sender 0x06d8ae90, receiver caseD_4b at 0x06d82573. Nine
// dwords copied straight through; the receiver moves the four at +0x0c..+0x18 with
// FLD/FSTP. The argument IS the wire payload. Server.dll's builder is pub::Player::SetCamera
// (.text:06d55e60): +0x00 = transform.Translation, +0x0c = I3DMathEngine::MatrixToQuaternion
// (transform.Rotation) in (W,X,Y,Z) order, +0x1c = fov, +0x20 = aspect -- the field names
// below are that builder's.
// ---------------------------------------------------------------------------
struct FPayloadServerSetCamera {
    Vector Position;            // +0x00
    Quaternion Orient;          // +0x0c  (W, X, Y, Z)
    float Fov;                  // +0x1c
    float Aspect;               // +0x20
};
static_assert(offsetof(FPayloadServerSetCamera, Orient) == 0x0c, "SET_CAMERA +0x0c");
static_assert(offsetof(FPayloadServerSetCamera, Fov) == 0x1c, "SET_CAMERA +0x1c");
static_assert(sizeof(FPayloadServerSetCamera) == 0x24, "SET_CAMERA payload 0x24");

// ---------------------------------------------------------------------------
// IClient::SendServerAdjustAttitude (slot 67, +0x10c), ch2 op 0x2a
// FLPACKET_SERVER_ADJUSTATTITUDE, size 0x28 -> payload 0x08. Sender 0x06d88f70,
// receiver caseD_2a at 0x06d81317. The second field is a float on both sides (FLD/FSTP at
// 0x06d88f96 and 0x06d8131a). The argument IS the wire payload; no Server.dll caller.
// ---------------------------------------------------------------------------
struct FPayloadServerAdjustAttitude {
    unsigned int VibeHandle;    // +0x00
    float Attitude;           // +0x04
};
static_assert(sizeof(FPayloadServerAdjustAttitude) == 0x08, "ADJUSTATTITUDE payload 0x08");

// ---------------------------------------------------------------------------
// ch2 0x52 FLPACKET_SERVER_PLAYERLIST: one opcode for IClient slots 110..112, told apart
// by the Kind dword the packet carries (rpclocal payload +0x00, the remote packet's +0x20;
// rpclocal's receiver compares it against 0, 1 and 2 at .text:06d83102). Both packet
// constructors seed 3 (rpclocal 06d9ec60, RemoteClient 06b4cf20), which no receiver
// delivers. Shared by rpclocal (FPayloadServerPlayerList) and RemoteClient / RemoteServer
// (FRpcServerPlayerListPacket) so the senders and receivers cannot drift apart.
// ---------------------------------------------------------------------------
const unsigned int kPlayerListKindClear = 0u;   // slot 112 SendServerPlayerListClear
const unsigned int kPlayerListKindEntry = 1u;   // slot 110 SendServerPlayerListAdd
const unsigned int kPlayerListKindDrop  = 2u;   // slot 111 SendServerPlayerListRemove
const unsigned int kPlayerListKindNone  = 3u;   // the constructors' seed
