// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"

// Common.dll IFACE batch — IObjInspectImpl, IBehaviorManager, ScriptBehavior,
// BehaviorTypes, IAction.

#include "common_cobject.h"
#include "common_cequip.h"
#include "common_net.h"
#include "common_goods.h" // CBase, the second base of IObjInspectImpl
#include "common_text.h"
#include "common_pub.h" // Wave 4: pub::AI types (Personality, callbacks, directives)
// common_systemmanager.h is included further down, right after TargetManager: SystemManager
// embeds a TargetManager at +0x142C, and TargetManager derives from
// IObjInspect::SubtargetEnumerator, so the header can only be read once IObjInspect and
// TargetManager exist. Everything above that point needs SystemManager only by pointer.

#include <cstddef>

class SystemManager;
class PathfindManager; // common_pathfind.h -- IBehaviorManager+0x0C
class Behavior;
COMMON_API Behavior* __cdecl CREATE_BEHAVIOR(int TypeId);

typedef unsigned int uint; // Convenience typedef for IFACE interfaces

// ============================================================================
// Forward declarations for types not yet ported
// ============================================================================

// IStateGraph -- the pub::StateGraph record (common_newchar.h). IBehaviorManager+0x04 holds
// the one pub::StateGraph::get_state_graph_internal returned; the export
// ?get_state_graph@IBehaviorManager@@QAEPBUIStateGraph@@XZ names it. The former
// "IUStateGraph" here was an invented name for the same pointer.
struct IStateGraph;

// SystemManager is defined in common_systemmanager.h (ctor FUN_062e6260, size
// 0x195C; IBehaviorManager's ctor allocates it into +0x08).

// IDirectiveInfo — directive info struct (12 bytes). Retail exports no member of it (it is
// only named, `UIDirectiveInfo@@`, by IBehaviorManager's get/set_current_directive), so no
// export macro: a dllexport POD would still export its implicit operator=.
struct IDirectiveInfo
{
    unsigned char Data[12];

    // Word 2 (+0x08): the behavior id the directive selects (Server's SubmitDirective
    // OpType -> id table; FleeBehavior tests it for 2 = Goto / 7 = Dock).
    unsigned GetBehaviorId() const { return *reinterpret_cast<unsigned const*>(Data + 8); }
};

// ============================================================================
// Enums used by IFACE classes
// ============================================================================

// ?get_equipment_status@IObjInspectImpl@@UBEHAAUEquipStatus@@ABG@Z -- U, so a struct, not an
// enum. Shape recovered from CEquip::GetStatus (0629ae80) and CELauncher::GetStatus (06299410):
//
//   +0x00  a byte of bit flags written with the classic (dst & ~m) | (src & m) idiom.
//          bit 0 <- CEquip vtable +0x04, bit 1 <- +0x08, bit 2 <- +0x10. CELauncher then
//          tests (Flags & 0x6) to decide whether to bother with the ammo checks below.
//   +0x04  a dword of equipment-specific flags. CELauncher ORs 0x4 (vtable +0xa0 said no),
//          0x8 (vtable +0xb0) and 0x2 (vtable +0xbc) into it.
//
// The field NAMES are the port's: nothing about EquipStatus is exported, so only the offsets
// and the bit positions are recovered. The size is a lower bound -- the only two writers stop
// at +0x08, and no caller in freelancer.exe allocates one visibly (every reference is a vtable
// slot, never a direct call), so 8 is what the evidence supports and no more.
struct EquipStatus
{
    unsigned char Flags;        // +0x00  bit0/bit1/bit2, see above
    unsigned char Pad01;
    unsigned char Pad02;
    unsigned char Pad03;
    unsigned EquipFlags;        // +0x04  0x2 / 0x4 / 0x8 from CELauncher::GetStatus
};

// Bit names are likewise the port's.
unsigned char const EquipStatusFlagActive = 0x01;      // CEquip vtable +0x04
unsigned char const EquipStatusFlagDestroyed = 0x02;   // CEquip vtable +0x08
unsigned char const EquipStatusFlagDisabled = 0x04;    // CEquip vtable +0x10 IsDisabled (CEquip::GetStatus 0629ae80)

// CELauncher::GetStatus EquipFlags (+0x04) bits.
unsigned const LauncherStatusRefireElapsed = 0x02;     // vtable +0xbc RefireDelayElapsed
unsigned const LauncherStatusInsufficientAmmo = 0x04;  // vtable +0xa0 AmmoNeedsMet == false
unsigned const LauncherStatusPowerNeedsMet = 0x08;     // vtable +0xb0 PowerNeedsMet

// IObjInspect::get_equipment_val's selector, named from IObjInspectImpl::get_equipment_val's
// switch (.text:062c0460-062c0837); the value it writes depends on the equipment type.
enum EquipmentValType
{
    EquipValPowerDraw = 0,          // gun / engine / thruster power draw, power ChargeRate
    EquipValHullDamagePerFire = 1,  // projectiles per fire * munition HullDamage
    EquipValRefireDelay = 2,        // gun archetype RefireDelay
    EquipValAmmoCount = 3,
    EquipValCapacity = 4,           // power plant Capacity
    EquipValRange = 5,              // munition / radar / tractor range
    EquipValMunitionKind = 6,       // int: 3 cruise disruptor, 2 HpType 0xa, else seeker != 0
    EquipValCargoScanRange = 7,
    EquipValMuzzleVelocity = 8,
    EquipValAutoTurret = 9,         // byte: gun archetype auto-turret flag
    EquipValSeeker = 0xA,           // byte: munition seeker != 0
    EquipValTargetId = 0xB          // int: CEGun::GetTarget's object id, or 0
};

// The answer of get_equipment_val(EquipValMunitionKind).
enum EEquipMunitionKind
{
    kMunitionKindPlain = 0,            // munition without a seeker
    kMunitionKindSeeker = 1,
    kMunitionKindTorpedo = 2,          // HpType 0xa (HpTypeTorpedo)
    kMunitionKindCruiseDisruptor = 3,
};

struct EquipmentVal
{
    float Value;
    unsigned short EquipId;
    unsigned short Pad;
};

// The docking terminal types (the enum name is retail's, from get_dock_hardpoints' mangling;
// the enumerators are the port's). get_dock_hardpoints answers Archetype::EqObj::
// DockingSphere::Type, filled from the PTR_s_berth_063eca00 table (berth 1, moor_small 3,
// moor_medium 4, moor_large 5, ring / pad 6, jump 7, airlock 8; no match 0);
// pub::SpaceObj::GetTerminalInfo answers the same value. pub::AI::DirectiveLaunchOp::
// TerminalType takes Content's [ObjList] Launch keywords: NONE 0, BERTH 1, PAD 2, MOOR_* 3..5,
// RING 6, JUMP_GATE 7.
enum TERMINAL_TYPE
{
    TERM_NONE = 0,
    TERM_BERTH = 1,
    TERM_PAD = 2,           // the Launch keyword "PAD"; the archetype table maps "pad" to 6
    TERM_MOOR_SMALL = 3,
    TERM_MOOR_MEDIUM = 4,
    TERM_MOOR_LARGE = 5,
    TERM_RING = 6,
    TERM_JUMP = 7,
    TERM_AIRLOCK = 8,
};

enum CancelRequestType
{
    CancelNone = 0,
    CancelCurrent = 1,
    CancelAll = 2
};

enum EvaluateResult
{
    EvalOk = 0,
    EvalBlocked = 1,
    EvalSkip = 2
};

// ============================================================================
// IObjInspect / IObjAffect — abstract interfaces (vtable only)
// ============================================================================

// Defined below; the slot signatures need the name first.
struct IObjRW;

// ScanList (?get_scanlist@IObjInspectImpl@@UBEHAAPBUScanList@@I_N@Z) is declared in
// common_cequip.h, next to CEScanner, whose +0x24 member it is.

// IObjInspect -- pure virtual query methods on game objects, 85 slots in retail order, all
// __thiscall const.
//
// From `??_7IObjInspectImpl@@6BIObjInspect@@@` (.rdata:0639effc..0639f14c, 85 entries, each
// resolved through the export ordinals). Raw `Vtable[N]` comments elsewhere use these
// retail offsets.
//
// Not slots of this interface:
//   * a destructor: slot 0 is `get_position`; IObjInspectImpl's destructor overrides
//     IObjAffect's (the second base).
//   * `find_equipment`: exported `ABE`, private non-virtual.
//
// Slots 3 and 4 share one address (0x062bf1f0): `get_transform` and `get_orientation` have
// identical bodies and MSVC folded them; the order here (transform, then orientation) is an
// assumption.
//
// Slot 14 is 0x0629ba00, an address seven different `bool ()` bodies were folded onto; the
// one belonging to this interface is `?is_dying@IObjInspectImpl@@UBE_NXZ`. Slot 84 is
// 0x06263a70, folded the same way, and its member here is `?cobject@...@@UBEPBUCObject@@XZ`.
struct IObjInspect
{
    // Nested enumerator callback types. Both are confirmed by the mangled names of the slots
    // that take them: `PAUCargoEnumerator@IObjInspect@@` (41) and
    // `PAUSubtargetEnumerator@IObjInspect@@` (73).
    struct CargoEnumerator
    {
        // IObjInspectImpl::enumerate_cargo (.text:062c0bc0-062c0bdb) pushes, right to left,
        // [CEquip+0x28] (CECargo::Count), [CEquip+0x0C] (the archetype pointer, not its id)
        // and the address of a stack copy of CEquip+0x08 (the equip id). The one retail
        // implementation, RepairManager::enumerate (FUN_062ecf60, `RET 0xc`), reads the id
        // through the reference and calls Arch->get_class_type().
        virtual bool enumerate(unsigned short const& Id, Archetype::Equipment const* Arch, int Count) = 0;
    };
    struct SubtargetEnumerator
    {
        virtual bool enumerate_equip(unsigned short Id, unsigned Type) = 0;
        virtual bool enumerate_subpart(unsigned short Id, unsigned CollisionGroup) = 0;
    };
    virtual Vector const& get_position() const = 0;  //  0 +0x000  ABVVector@@XZ
    virtual Vector get_velocity() const = 0;  //  1 +0x004  ?AVVector@@XZ
    virtual Vector get_angular_velocity() const = 0;  //  2 +0x008  ?AVVector@@XZ
    virtual Transform const& get_transform() const = 0;  //  3 +0x00C  ABVTransform@@XZ (folded with slot 4)
    virtual Matrix const& get_orientation() const = 0;  //  4 +0x010  ABVMatrix@@XZ (folded with slot 3)
    virtual Vector get_center_of_mass() const = 0;  //  5 +0x014  ?AVVector@@XZ
    virtual int get_sub_obj_center_of_mass(unsigned short SubId, Vector& OutCenter) const = 0;  //  6 +0x018  HGAAVVector@@@Z
    virtual long get_index() const = 0;  //  7 +0x01C  JXZ
    virtual unsigned int const get_id() const = 0;  //  8 +0x020  ?BIXZ (const-qualified return)
    virtual int get_good_id(unsigned& OutId) const = 0;  //  9 +0x024  HAAI@Z
    virtual int get_archetype_extents(Vector& Min, Vector& Max) const = 0;  // 10 +0x028  HAAVVector@@0@Z
    virtual int get_physical_radius(float& Radius, Vector& Center) const = 0;  // 11 +0x02C  HAAMAAVVector@@@Z
    virtual float get_mass() const = 0;  // 12 +0x030  MXZ
    virtual bool is_targetable() const = 0;  // 13 +0x034  _NXZ
    virtual bool is_dying() const = 0;  // 14 +0x038  _NXZ
    virtual int get_status(float& HitPts, float& MaxHitPts) const = 0;  // 15 +0x03C  HAAM0@Z
    virtual int get_status(float& Health) const = 0;  // 16 +0x040  HAAM@Z
    virtual int get_shield_status(float& CurHealth, float& MaxHealth, bool& bActive) const = 0;  // 17 +0x044  HAAM0AA_N@Z
    virtual int get_shield_status(float& Health, bool& bActive) const = 0;  // 18 +0x048  HAAMAA_N@Z
    virtual int get_throttle(float& OutThrottle) const = 0;  // 19 +0x04C  HAAM@Z
    virtual int get_axis_throttle(Vector& OutThrottle) const = 0;  // 20 +0x050  HAAVVector@@@Z
    virtual int get_nudge_vec(Vector& OutVec) const = 0;  // 21 +0x054  HAAVVector@@@Z
    virtual int get_strafe_dir(StrafeDir& OutDir) const = 0;  // 22 +0x058  HAAW4StrafeDir@@@Z
    virtual int is_cruise_active(bool& OutCruise, bool& OutCharging) const = 0;  // 23 +0x05C  HAA_N0@Z
    virtual int is_cruise_active(bool& OutActive) const = 0;  // 24 +0x060  HAA_N@Z
    virtual int are_thrusters_active(bool& OutActive) const = 0;  // 25 +0x064  HAA_N@Z
    virtual int get_attitude_towards(float& OutAtt, IObjInspect const* Other) const = 0;  // 26 +0x068  HAAMPBUIObjInspect@@@Z
    virtual int get_attitude_towards_symmetrical(float& OutAtt, IObjInspect const* Other,
                                                  float& OutReverse) const = 0;  // 27 +0x06C  HAAMPBUIObjInspect@@0@Z

    virtual int get_reputation(float& OutRep, uint const& GroupId) const = 0;  // 28 +0x070  HAAMABI@Z
    virtual int get_target(IObjRW*& OutTarget) const = 0;  // 29 +0x074  HAAPAUIObjRW@@@Z
    virtual int get_subtarget(unsigned short& OutSub) const = 0;  // 30 +0x078  HAAG@Z
    virtual int get_subtarget_center_of_mass(Vector& OutCenter) const = 0;  // 31 +0x07C  HAAVVector@@@Z
    virtual int get_rank(uint& OutRank) const = 0;  // 32 +0x080  HAAI@Z
    virtual int get_affiliation(uint& OutAffil) const = 0;  // 33 +0x084  HAAI@Z
    virtual int get_type(uint& OutType) const = 0;  // 34 +0x088  HAAI@Z
    virtual int get_base(uint& OutBase) const = 0;  // 35 +0x08C  HAAI@Z
    virtual int get_dock_target(uint& OutDock) const = 0;  // 36 +0x090  HAAI@Z
    virtual int get_power(float& OutPower) const = 0;  // 37 +0x094  HAAM@Z
    virtual int get_zone_props(unsigned long& OutProps) const = 0;  // 38 +0x098  HAAK@Z
    virtual float get_scanner_interference() const = 0;  // 39 +0x09C  MXZ
    virtual int get_hold_left(float& OutHold) const = 0;  // 40 +0x0A0  HAAM@Z
    virtual int enumerate_cargo(CargoEnumerator* Enum) const = 0;  // 41 +0x0A4  HPAUCargoEnumerator@IObjInspect@@@Z
    virtual int get_data(void const*& OutData) const = 0;  // 42 +0x0A8  HAAPBX@Z
    virtual int get_formation_offset(Vector& OutOffset) const = 0;  // 43 +0x0AC  HAAVVector@@@Z
    virtual int get_formation_leader(IObjRW*& OutLeader) const = 0;  // 44 +0x0B0  HAAPAUIObjRW@@@Z
    virtual int get_follow_offset(Vector& OutOffset) const = 0;  // 45 +0x0B4  HAAVVector@@@Z
    virtual int get_follow_leader(IObjRW*& OutLeader) const = 0;  // 46 +0x0B8  HAAPAUIObjRW@@@Z
    virtual bool is_player() const = 0;  // 47 +0x0BC  _NXZ
    virtual int get_hardpoint(char const* Name, Vector* Pos, Matrix* Orient) const = 0;  // 48 +0x0C0  HPBDPAVVector@@PAVMatrix@@@Z
    virtual bool has_dock_hardpoints() const = 0;  // 49 +0x0C4  _NXZ
    virtual int get_dock_hardpoints(int Index, TERMINAL_TYPE* Type, Transform* XformA,
                                     Transform* XformB, Transform* XformC,
                                     float* Distance) const = 0;  // 50 +0x0C8  HHPAW4TERMINAL_TYPE@@PAVTransform@@11PAM@Z

    virtual float get_time_to_accelerate(float From, float To, float Dt,
                                         ThrustEquipType Type) const = 0;  // 51 +0x0CC  MMMMW4ThrustEquipType@IObject@@@Z

    virtual float get_distance_travelled(float Speed, float Dt, float Accel,
                                         ThrustEquipType Type) const = 0;  // 52 +0x0D0  MMMMW4ThrustEquipType@IObject@@@Z

    virtual float get_projected_throttle(float Speed, ThrustEquipType Type) const = 0;  // 53 +0x0D4  MMW4ThrustEquipType@IObject@@@Z
    virtual float get_speed(float Throttle, ThrustEquipType Type) const = 0;  // 54 +0x0D8  MMW4ThrustEquipType@IObject@@@Z
    virtual float get_initial_speed_to_coast_distance(float Speed, bool bStop) const = 0;  // 55 +0x0DC  MM_N@Z
    virtual float get_time_to_accelerate_angularly(float From, float To, float Dt) const = 0;  // 56 +0x0E0  MMMM@Z
    virtual float get_time_to_accelerate_angularly(float From, float To, float Dt,
                                                    float MaxSpeed) const = 0;  // 57 +0x0E4  MMMMM@Z

    virtual float get_angular_distance_travelled(float From, float To, float Dt) const = 0;  // 58 +0x0E8  MMMM@Z
    virtual float get_angular_distance_travelled(float From, float To, float Dt,
                                                  float MaxSpeed) const = 0;  // 59 +0x0EC  MMMMM@Z

    virtual float get_angular_speed_XY(float Speed, float MaxSpeed) const = 0;  // 60 +0x0F0  MMM@Z
    virtual float get_angular_speed_Z(float Speed) const = 0;  // 61 +0x0F4  MM@Z
    virtual float get_projected_axis_throttle_XY(float Speed) const = 0;  // 62 +0x0F8  MM@Z
    virtual float get_projected_axis_throttle_Z(float Speed) const = 0;  // 63 +0x0FC  MM@Z
    virtual float get_max_bank_angle() const = 0;  // 64 +0x100  MXZ
    virtual int get_scanlist(ScanList const*& OutList, uint Type, bool bFlag) const = 0;  // 65 +0x104  HAAPBUScanList@@I_N@Z
    virtual int get_tgt_lead_fire_pos(unsigned short const& EquipId, Vector& OutPos) const = 0;  // 66 +0x108  HABGAAVVector@@@Z
    virtual int is_pointing_at(bool& OutIsPointing, unsigned short const& EquipId,
                               float Angle) const = 0;  // 67 +0x10C  HAA_NABGM@Z

    virtual int can_point_at(bool& OutCanPoint, unsigned short const& EquipId,
                              Vector const& Point, float Angle) const = 0;  // 68 +0x110  HAA_NABGABVVector@@M@Z

    virtual int find_equipment(unsigned short* const Buf, uint MaxCount, uint TypeMask) const = 0;  // 69 +0x114  HQAGII@Z
    virtual int get_equipment_status(EquipStatus& OutStatus, unsigned short const& Id) const = 0;  // 70 +0x118  HAAUEquipStatus@@ABG@Z
    virtual int get_equipment_val(EquipmentVal& OutVal, unsigned short const& Id,
                                   EquipmentValType Type, float Param) const = 0;  // 71 +0x11C  HAAUEquipmentVal@@ABGW4EquipmentValType@@M@Z

    virtual int scan_cargo(IObjRW* Scanner, EquipDescVector& OutCargo) const = 0;  // 72 +0x120  HPAUIObjRW@@AAUEquipDescVector@@@Z
    virtual int enumerate_subtargets(SubtargetEnumerator* Enum) const = 0;  // 73 +0x124  HPAUSubtargetEnumerator@IObjInspect@@@Z
    virtual int get_lane_direction(Transform const& Xform, bool* OutDir) const = 0;  // 74 +0x128  HABVTransform@@PA_N@Z
    virtual int get_ring_side(Transform const& Xform, bool* OutSide) const = 0;  // 75 +0x12C  HABVTransform@@PA_N@Z
    virtual int traverse_rings(uint& OutRing, bool bForward) const = 0;  // 76 +0x130  HAAI_N@Z
    virtual int is_using_tradelane(bool* OutUsing) const = 0;  // 77 +0x134  HPA_N@Z
    virtual int get_lane_start(IObjInspect const* Self, IObjInspect const*& OutLane,
                               Vector& OutStart, Vector& OutEnd) const = 0;  // 78 +0x138  HPBUIObjInspect@@AAPBU2@AAVVector@@2@Z

    virtual bool generate_follow_offset(IObjInspect const* Leader, Transform& OutXform) const = 0;  // 79 +0x13C  _NPBUIObjInspect@@AAVTransform@@@Z
    virtual int get_atmosphere_range(float& OutRange) const = 0;  // 80 +0x140  HAAM@Z
    virtual int get_toughness() const = 0;  // 81 +0x144  HXZ
    virtual int get_behavior_id() const = 0;  // 82 +0x148  HXZ
    virtual int get_formation_followers(IObjRW** const OutFollowers, uint MaxCount) const = 0;  // 83 +0x14C  HQAPAUIObjRW@@I@Z
    virtual CObject const* cobject() const = 0;  // 84 +0x150  PBUCObject@@XZ
};

// Retail Common.dll exports every member of std::vector<IObjInspect*> (VC6 vector, 47
// members). Its bodies are /OPT:ICF-folded with every other pointer vector, so the retail
// user cannot be pinned from the addresses; the export itself is what the port reproduces.
// The definition is in common_iobjinspectimpl.cpp; other modules import it through this declaration.
#ifndef COMMON_EXPORTS
extern template class COMMON_API std::vector<IObjInspect*>;
#endif

// TargetManager -- the AI target arbiter embedded in SystemManager at +0x142C (retail ctor
// FUN_062ed570, called at .text:062e62ca with ECX = SystemManager+0x142c; the next member
// starts at +0x1498, so sizeof is 0x6C). RTTI .?AVTargetManager@@ with one base,
// IObjInspect::SubtargetEnumerator (CHD .rdata:063a9e44); vftable .rdata:063a0888 =
// { LAB_062ee9d0 enumerate_equip, 0x0632c0c0 enumerate_subpart (`XOR AL,AL; RET 8`) }.
// Retail Common.dll exports only the three members below (function-level export, no
// class-level: no ctor / operator= exports). Server.dll's pub::AI::submit_forced_target /
// remove_forced_target / set_player_enemy_clamp call them on
// IBehaviorManager[+8] + 0x142c.
// Members are defined in common_targetmanager.cpp; QueryZonePropertyLevels /
// SelectAttackCandidate / UpdateAttackTargetOverride still read `this` as raw bytes at the
// offsets this layout names. Field names come from those readers.
struct FAiTarget; // common_aisteer.h -- TargetManager::MatchesAttackOrder's argument

class TargetManager : public IObjInspect::SubtargetEnumerator
{
public:
    // FUN_062ed570, sole caller SystemManager's ctor (.text:062e62ca). Stores
    // +0x54/+0x58/+0x5C = 0, byte +0x60 = 0, +0x64 = 0, bytes +0x68/+0x69 = 0, the vftable
    // (.rdata:063a0888), and SubtargetId = SUBOBJ_ID_NONE -- nothing else. Owner and the
    // forced-target count stay unset until Attach.
    TargetManager();

    // vftable slot 0 (+0x00), LAB_062ee9d0. Keeps the closest-facing subtarget:
    // see the definition for the full trace. Always returns false.
    bool enumerate_equip(unsigned short Id, unsigned Type) override;
    // vftable slot 1 (+0x04), 0x0632c0c0 (ICF-folded `XOR AL,AL; RET 8`): returns false.
    bool enumerate_subpart(unsigned short Id, unsigned CollisionGroup) override;

    // FUN_062ed620, called from FUN_062e6490 (.text:062e662e) with the
    // SystemManager. Refuses (returns false, writes nothing) unless NewOwner is non-null and
    // its Watcher targets something; otherwise Owner = NewOwner, AttackState = 2,
    // NumForcedTargets = 0, AggressionRatio = 0.5f, AnchorPosition = 0, CommitTimer = 0,
    // bCommitted = 0, and returns true.
    bool Attach(SystemManager* NewOwner);

    // FUN_062ed740, called from SystemManager's tick (.text:062e6751) with the
    // frame time. While bCommitted: CommitTimer += Dt, and once it exceeds 10.0f
    // (DAT_063a0814) the commitment lapses (timer, anchor and flag cleared).
    void UpdateCommitTimer(float Dt);

    // FUN_062ed5a0, no stack arguments. The owner's current
    // target: Owner->Watcher's object (Watchable - 8, or a null receiver when nothing is
    // watched -- retail then faults on the vtable read, it has no guard), then
    // IObjInspect::get_target(IObjRW*&) (+0x74) into a zeroed local, which is returned.
    // Fifteen call sites; IBehaviorManager::get_debugger_target tail-jumps here.
    IObjRW* GetTarget() const;

    // FUN_062ee940 (.text:062ee940-062ee9c2, RET 8). Retail name unknown. Asks the owner ship
    // (Owner->Watcher's object, `-8` with a null arm) for its current target (IObjInspect
    // +0x74 into a zeroed local). Same pointer as Target -> false. Both non-null with equal
    // get_id() -> true, no change. Otherwise the ship's IObjAffect::set_target(Target,
    // SubTarget, -1) (+0x48 on the IObjAffect sub-object at +4) and true. Callers
    // FaceBehavior::evaluate (.text:062c8db9) and TrailBehavior.
    bool RetargetOwner(IObjRW* Target, unsigned short SubTarget);

    // FUN_062ed5e0, no stack arguments. Same receiver as
    // GetTarget, then IObjInspect::get_subtarget(unsigned short&) (+0x78) into a local seeded
    // with SUBOBJ_ID_NONE; returns it in AX. Sole caller UpdateBehaviorTick (.text:062d2c2e).
    unsigned short GetSubTarget() const;

    // 062ee280, `RET 4`; retail name not exported. Stores
    // NewState in AttackState unless it is already there, and reports the change through the
    // owning IBehaviorManager's ContentCallback (+0x110), when set, as
    // combat_state_notification: 0 -> (0, owner id, owner's target id or 0), 1 -> (2, owner
    // id, 0), 2 -> (4, owner id, 0); other values only store. State 0 also copies
    // ExternalIface's transform translation into AnchorPosition. Callers ScanGunGroupTargets
    // (.text:062ea038) and UpdateAttackTargetOverride (062ed979 / 062edbcd / 062ee03f /
    // 062ee206).
    void SetAttackState(int NewState);

    // FUN_062ed780, RET 0. The per-tick attack-target arbitration, called from
    // IBehaviorManager::UpdateBehaviorTick (.text:062d2c08) on Sys->Targeting: the
    // directive-override target, the close-range commit, the zone / aggression gates, the two
    // AI-target-list scans and the arbitration through SelectAttackCandidate.
    void UpdateAttackTargetOverride();

    // FUN_062ed670, RET 8. Zone property flags of a 20-unit sphere at
    // ProbeSrc's position (Universe::ISystem slot 8 through Owner->ObjectDb->GetZoneDb()):
    // returns 0 when bit 0x40000 is set; otherwise decodes bits 1/2/4, 8/0x10/0x20 and 0x8000
    // into Out3[0..2] (levels 1..3, 1..3, 1) and returns 1.
    int QueryZonePropertyLevels(IObjInspect const* ProbeSrc, int* Out3);

    // FUN_062ee600, RET 0x10. Picks one of CandidateCount FAiTarget
    // pointers against the Personality's job attack order (SystemManager+0x1834) and the
    // enemy-count clamp, preferring StickyTarget's record on a random pick; writes the
    // matching attack-order record's +8 dword to *OutPriority (0xF when none). Null when
    // nothing matches.
    FAiTarget* SelectAttackCandidate(FAiTarget** Candidates, int CandidateCount,
                                     IObjRW const* StickyTarget, int* OutPriority);

    // FUN_062ee390, no stack arguments, result in ST0. The share
    // of nearby combat power that is hostile: own toughness (+0x144 on the owner) plus every
    // friendly-enough scanned object's, against every hostile-enough one's; returns
    // Hostile / (Own + Hostile) when that sum exceeds 0.01, else 0.0f. The quotient is the
    // unrounded FDIV result (double), and both callers compare it unrounded.
    // Callers UpdateAttackTargetOverride (.text:062edba9) and GetScanResponse (.text:062eeb90).
    double GetHostilePowerRatio();

    // FUN_062eeb70, `RET 4`. The body of the exported
    // IBehaviorManager::get_scan_response, which is `MOV ECX,[ECX+8]; ADD ECX,0x142c; JMP`.
    // Asks the object database for Type (IObjDB +0x04), drops the AI target list, then answers
    // 1 when GetHostilePowerRatio() is below the Personality's job toughness threshold and 0
    // otherwise.
    pub::AI::ScanResponse GetScanResponse(uint Type);

    // FUN_062ee530, `RET 4`. Sole caller: ScanGunGroupTargets
    // (FUN_062e9c50). Walks the Personality's job attack order -- 12-byte {class, range,
    // weapon flags} records of JobParams.AttackPreferences (SystemManager+0x1834), ended by class
    // 12 -- and returns true on the first record whose class mask (DAT_063a0848) meets
    // Candidate's lazily cached get_type flags.
    bool MatchesAttackOrder(FAiTarget* Candidate);

    // 0x062eec10 ?submit_forced_target@TargetManager@@QAEHI@Z
    COMMON_API int submit_forced_target(unsigned int TargetId);
    // 0x062eec50 ?remove_forced_target@TargetManager@@QAEHI@Z
    COMMON_API int remove_forced_target(unsigned int TargetId);
    // 0x062ee5e0 ?set_player_enemy_clamp@TargetManager@@QAEXHH@Z -- stores into two
    // module globals (DAT_063ec754 / DAT_063ec758, .data init 2), not into `this`.
    COMMON_API void set_player_enemy_clamp(int MinClamp, int MaxClamp);

    enum { MaxForcedTargets = 8 };

    // The values of AttackState; SetAttackState reports them to the content callback as
    // combat_state_notification CSNEngage / CSNNoEngage / CSNCombatOver.
    enum EAttackState
    {
        kAttackStateEngaged = 0,     // a hostile target was acquired
        kAttackStateNoEngage = 1,    // hostiles around (aggression ratio above the threshold), none engaged
        kAttackStateCombatOver = 2,  // not in combat (Attach starts here, a combat drift ends there)
    };

    // +0x00: vfptr (IObjInspect::SubtargetEnumerator).
    SystemManager* Owner;                  // +0x04 Attach; FUN_062ee600 / enumerate_equip read SystemManager through it
    int AttackState;                       // +0x08 SetAttackState (062ee280) compares and stores it; Attach sets 2.
                                           //       SystemManager+0x1434 readers (UpdateBehaviorTick) test it.
    float AggressionRatio;                 // +0x0C FUN_062ed780 stores FUN_062ee390's ratio (.text:062edbb4); Attach 0.5f
    float SubtargetDistanceSq;             // +0x10 enumerate_equip keeps the smallest value it has seen
    unsigned short SubtargetId;            // +0x14 enumerate_equip stores the matching Id; ctor SUBOBJ_ID_NONE
    unsigned char Pad16[2];
    int NumForcedTargets;                  // +0x18
    unsigned int ForcedTargets[MaxForcedTargets]; // +0x1C
    Vector AnchorPosition;                 // +0x3C SetAttackState (062ee280) state 0 caches ExternalIface's position here
    bool bCommitted;                       // +0x48 FUN_062ed780 sets it on the close-range commit (.text:062ed975)
    unsigned char Pad49[3];
    float CommitTimer;                     // +0x4C UpdateCommitTimer accumulates it
    int SelectionPriority;                 // +0x50 FUN_062ed780 stores the chosen tier's priority (.text:062edfaa)
    int NumCandidatesA;                    // +0x54 FUN_062ed780 per-tick candidate counters (ctor 0)
    int NumCandidatesB;                    // +0x58
    int NumSelected;                       // +0x5C
    bool bSelectedIsPlayer;                // +0x60 FUN_062ed780 sets it when the pick is_player() (.text:062edf94)
    unsigned char Pad61[3];
    int TargetedEnemyCount;                // +0x64 ctor 0
    // +0x68 / +0x69: per-tick flags of the player enemy clamp in SelectAttackCandidate. A player
    // candidate already targeted by more enemies than the clamp max (DAT_063ec758) is dropped
    // from the list (+0x69 = 1, .text:062ee88f) unless it is the only candidate left, in which
    // case it is kept (+0x68 = 1, .text:062ee899). Zeroed by the ctor and at the start of every
    // UpdateAttackTargetOverride (.text:062ed7a6 / 062ed7aa); no reader in Common.
    bool bPlayerOverClampKept;             // +0x68
    bool bPlayerOverClampDropped;          // +0x69
    unsigned char Pad6A[2];
};

static_assert(offsetof(TargetManager, Owner) == 0x04, "TargetManager+0x04 Owner");
static_assert(offsetof(TargetManager, AttackState) == 0x08, "TargetManager+0x08 AttackState");
static_assert(offsetof(TargetManager, AggressionRatio) == 0x0C, "TargetManager+0x0C AggressionRatio");
static_assert(offsetof(TargetManager, SubtargetDistanceSq) == 0x10, "TargetManager+0x10 SubtargetDistanceSq");
static_assert(offsetof(TargetManager, SubtargetId) == 0x14, "TargetManager+0x14 SubtargetId");
static_assert(offsetof(TargetManager, NumForcedTargets) == 0x18, "TargetManager+0x18 NumForcedTargets");
static_assert(offsetof(TargetManager, ForcedTargets) == 0x1C, "TargetManager+0x1C ForcedTargets");
static_assert(offsetof(TargetManager, AnchorPosition) == 0x3C, "TargetManager+0x3C AnchorPosition");
static_assert(offsetof(TargetManager, bCommitted) == 0x48, "TargetManager+0x48 bCommitted");
static_assert(offsetof(TargetManager, CommitTimer) == 0x4C, "TargetManager+0x4C CommitTimer");
static_assert(offsetof(TargetManager, SelectionPriority) == 0x50, "TargetManager+0x50 SelectionPriority");
static_assert(offsetof(TargetManager, NumCandidatesA) == 0x54, "TargetManager+0x54");
static_assert(offsetof(TargetManager, bSelectedIsPlayer) == 0x60, "TargetManager+0x60");
static_assert(offsetof(TargetManager, TargetedEnemyCount) == 0x64, "TargetManager+0x64");
static_assert(offsetof(TargetManager, bPlayerOverClampKept) == 0x68, "TargetManager+0x68");
static_assert(offsetof(TargetManager, bPlayerOverClampDropped) == 0x69, "TargetManager+0x69");
static_assert(sizeof(TargetManager) == 0x6C, "TargetManager sizeof 0x6C");

// Needs IObjInspect and TargetManager complete (SystemManager+0x142C); see the note at the
// top of this file.
#include "common_systemmanager.h"

// Every `IObjRW` carries its `IObjInspect` sub-object at offset 0. RTTI-proven, not assumed:
// Common.dll's ClassHierarchyDescriptor for IObjRW (.rdata:063a8b8c, numBaseClasses=4) lists
// the bases at .rdata:063a8b78 as IObjRW, IObjInspect, IObjAffect, Watchable, and the
// CompleteObjectLocator at .rdata:063a8b9c puts the IObjAffect sub-object at offset 4 -- which
// leaves IObjInspect at 0. That is why the Behavior family's shared context initializer
// (FUN_062d2090) can do `MOV EAX,[owner]; CALL [EAX+0x20]` on a bare owner pointer.
// This helper exists because the Behavior family still holds the owner as a bare `void*`.
inline IObjInspect const* ObjInspectOf(void const* Object)
{
    return static_cast<IObjInspect const*>(Object);
}

// `Watchable` is IObjRW's third base and sits at +8, so the `- 8` the AI state machines
// do on a watcher target is a downcast to the whole object. The AI TUs (common_aisteer.cpp,
// common_behaviormanager.cpp, common_targetmanager.cpp, ...) all spell it out by hand; this names it.
inline IObjRW* ObjRwFromWatchable(void* WatchableSubObject)
{
    if (WatchableSubObject == nullptr)
        return nullptr;
    return reinterpret_cast<IObjRW*>(static_cast<char*>(WatchableSubObject) - 8);
}

// The RequestType of IObjAffect::request_event / request_cancel and the first argument of
// RequestCB::OnResult: the controller message each one sends (DockRequest / DockCancel,
// FormationRequest / FormationLeave, TradelaneRequest / TradelaneRelease).
enum ERequestEventType
{
    kRequestEventDock = 0,
    kRequestEventFormation = 1,
    kRequestEventTradelane = 2,
};

// The ArgA of IObjAffect::slot_6c: the client plays the player's ship voice line for it
// (FClientShipGameobj::slot_6c, FUN_00547250); the other values it is called with (2, 3, 4)
// are ignored there.
enum EShipVoiceNotice
{
    kShipVoiceDockTooFar = 0,
    kShipVoiceTradeLaneDisrupted = 1,
    kShipVoiceFormationLost = 5,
};

// IObjAffect abstract interface — mutation methods.
struct IObjAffect
{
    // No virtual destructor: retail's slot 0 is set_throttle.

    // ".?AURequestCB@IObjAffect@@" (TypeDescriptor .data:063ebdc8). DockBehavior and
    // InstantTradeLaneBehavior implement it as a second base at mdisp 0x2c, each with a
    // one-slot secondary vtable (DockBehavior's .rdata:0639f498 -> LAB_062c53f0, which adjusts
    // `this` by -0x2c and dispatches). The caller is Server.dll's Ship::request_returned
    // (FUN_06d01e80), which passes its own three arguments; pub::SpaceObj::Dock calls it with
    // (0, its DOCK_HOST_RESPONSE, the dock port). A false answer drops the callback.
    //
    // Not COMMON_API: retail exports no RequestCB member. No virtual destructor: the
    // secondary vtables have one slot.
    struct RequestCB
    {
        virtual bool OnResult(int RequestType, int Response, int Arg3) = 0;
    };

    // ------------------------------------------------------------------------------------
    // The thirty slots.
    //
    // Implemented in Server.dll and freelancer.exe:
    //   * Server.dll GameObject's vftable for IObjAffect is .rdata:06d6576c (its ctor
    //     .text:06cee360 stores it at +4, .text:06cee444). Eleven of the thirteen Server
    //     wrappers keep it slot for slot; Ship's (.rdata:06d6709c, ctor .text:06d00f21)
    //     overrides 29 of the 30, and EqObj/Solar (.rdata:06d651c4, .rdata:06d673ec) share
    //     Ship's weapon/equipment bodies.
    //   * freelancer.exe's client wrapper (ctor .text:005392d0) installs .rdata:005de76c,
    //     which has the same thirty RET n values as Server's GameObject base.
    //
    // Arity comes from GameObject's base bodies, one-instruction `MOV EAX,<err>; RET n`
    // stubs (every slot returns int):
    //   .text:06cef3a0  MOV EAX,-3 / RET 4      slots 0-3, 28, 29
    //   .text:06cef340  MOV EAX,-8 / RET 4      slots 4-7, 9-12
    //   .text:06d567c0  XOR EAX,EAX / RET       slot 8 (ICF-folded with pub::Player::SetCostume)
    //   .text:06cef380  MOV EAX,-3 / RET 0x10   slots 13, 17, 25
    //   .text:06cef370  MOV EAX,-3 / RET 0xc    slots 14-16, 18-20, 22, 24
    //   .text:06cef350  MOV EAX,-3 / RET 8      slots 21, 27
    //   .text:06cef360  MOV EAX,-3 / RET 0x14   slot 23
    //   .text:06cef390  MOV EAX,-3 / RET        slot 26
    // freelancer.exe's base (.text:0053a8f0..0053a950, .text:004b5010) repeats the same RET
    // n values slot for slot.
    //
    // Names are given only where Ship's override forwards to a Common export or writes the
    // CShip field an IObjInspect getter reads back; the others stay slot_NN with Ship's body
    // cited.
    //
    // `SourceClient`: slots 13-22 end in a dword that Ship first normalises (-1 -> 0,
    // e.g. .text:06d02362) and then hands to the system notifier, which SKIPS the player
    // whose id equals it (.text:06d0e6b8 `CMP EAX,[ECX]` against StarSystem player node
    // +0x40). In single player a non-zero value also skips the local simulation step.
    // Common.dll's own callers always pass -1.

    virtual int set_throttle(float Throttle) = 0;  //  0 +0x00
    // Ship .text:06d01f30: CShip::set_throttle(Throttle) (IAT 06d645c4); return 0; RET 4.

    virtual int set_axis_throttle(Vector const& AxisThrottle) = 0;  //  1 +0x04
    // Ship .text:06d01f50: CSteering::SetAxisThrottle on CShip+0x230 -- exactly the body of
    // ?set_axis_throttle@CShip@@ (Common .text:06287b80 `ADD ECX,0x230; JMP
    // CSteering::SetAxisThrottle`), inlined. Paired getter IObjInspect::get_axis_throttle.

    virtual int set_nudge_vec(Vector const& NudgeVec) = 0;  //  2 +0x08
    // Ship .text:06d01f70: CShip::set_nudge_vec (IAT 06d645cc); return 0.

    virtual int set_strafe_dir(StrafeDir Dir) = 0;  //  3 +0x0C
    // Ship .text:06d01f90: CShip::get_strafe_dir, and on a change activates the first
    // 0x20000-class equipment through this interface's slot 19, then CShip::set_strafe_dir.

    virtual int set_formation_leader(IObjRW* Leader) = 0;  //  4 +0x10
    // Ship .text:06d029c0: BaseWatcher::set_pointer on CShip+0x1b8 with Leader+8 (null stays
    // null) -- the field IObjInspect::get_formation_leader reads (CShip::FormationLeader) --
    // then, for a player ship, reports get_formation_offset + Leader->get_id() to the client.
    // Caller: pub::AI::update_formation_state (.text:06d5d9c2).

    virtual int set_formation_offset(Vector const& Offset) = 0;  //  5 +0x14
    // Ship .text:06d02940: copies Offset into CShip+0x1c0 (CShip::FormationOffset, the field
    // get_formation_offset reads), then the same player report with get_formation_leader.

    virtual int add_formation_follower(IObjRW* Follower) = 0;  //  6 +0x18
    // Ship .text:06d02a50: JMP CShip::add_formation_follower (returns FORMATION_RTYPE).
    virtual int remove_formation_follower(IObjRW* Follower) = 0;  //  7 +0x1C
    // Ship .text:06d02a60: JMP CShip::remove_formation_follower.
    virtual int get_formation_follower_count() = 0;  //  8 +0x20
    // Ship .text:06d02a70: JMP CShip::get_formation_follower_count (RET, no arguments).
    virtual int set_follow_leader(IObjRW* Leader) = 0;  //  9 +0x24
    // Ship .text:06d02aa0: CShip::set_follow_leader (IAT 06d64608); return 0.
    virtual int set_follow_offset(Vector const& Offset) = 0;  // 10 +0x28
    // Ship .text:06d02a80: CShip::set_follow_offset (IAT 06d64604); return 0.
    virtual int add_follow_follower(IObjRW* Follower) = 0;  // 11 +0x2C
    // Ship .text:06d02ac0: JMP CShip::add_follow_follower.
    virtual int remove_follow_follower(IObjRW* Follower) = 0;  // 12 +0x30
    // Ship .text:06d02ad0: JMP CShip::remove_follow_follower.

    virtual int fire_weapons(unsigned short const* EquipIds, unsigned Count, Vector const* TargetPos,
                        int SourceClient) = 0;  // 13 +0x34
    // Ship/EqObj/Solar .text:06ce68a0 (RET 0x10): for each of the Count equipment ids, finds
    // the equipment and fires it (CEGun / CELauncher; a null TargetPos takes the launcher
    // path), then notifies the system with the ids that fired. EquipIds is an ARRAY of ids.

    virtual int jettison_cargo(unsigned short EquipId, int Count, int SourceClient) = 0;  // 14 +0x38
    // Ship .text:06d020d0 (RET 0xc): jettisons Count of a cargo item (CECargo). Caller
    // pub::SpaceObj::JettisonEquipment (.text:06cf93c0) pushes (id by value, count, 0).

    virtual int tractor_all(unsigned short const* EquipId, std::vector<IObjRW*> const& Targets,
                        int SourceClient) = 0;  // 15 +0x3C
    // .text:06ce6d80: CETractor::AddTarget for each object in Targets (the callee walks the
    // vector's begin/end); returns -2 when EquipId is not a tractor, -4 when nothing took.

    virtual int tractor_single(unsigned short const* EquipId, IObjRW* Target, int SourceClient) = 0;  // 16 +0x40
    // .text:06ce6ba0: the single-target form of slot 15 (CETractor::AddTarget once).

    virtual int set_gun_target(unsigned short const* EquipId, IObjRW* Target, unsigned short SubTarget,
                        int SourceClient) = 0;  // 17 +0x44
    // .text:06ce6f60 (RET 0x10): CEGun::SetTarget(Target->cobject(), SubTarget) on EquipId.

    virtual int set_target(IObjRW* Target, unsigned short SubTarget, int SourceClient) = 0;  // 18 +0x48
    // Ship .text:06d02340: compares with CShip::get_target / get_sub_target and, on a change,
    // CShip::set_target(Target) + CShip::set_sub_target(SubTarget) (IAT 06d645dc / 06d645d8).
    // Paired getters IObjInspect::get_target / get_subtarget. Guided overrides it too
    // (.text:06cf4c10 in .rdata:06d664bc).

    virtual int toggle_item(unsigned short const* EquipId, bool bActivate, int SourceClient) = 0;  // 19 +0x4C
    // .text:06ce70b0: activates / deactivates one equipment (the byte is read as BL).
    // Caller pub::SpaceObj::ActivateEquipment (.text:06cf9526) pushes (&id, EQInfo byte, 0).

    virtual int toggle_cruise(bool bEngage, bool bArg2, int SourceClient) = 0;  // 20 +0x50
    // Ship .text:06d02410: CEEngine::EngageCruise(bEngage, bArg2) (?EngageCruise@CEEngine@@
    // QAE_N_N0@Z) on the ship's engine. pub::SpaceObj::ActivateEquipment case 0 pushes
    // (1, 1, 0) at .text:06cf9464.

    virtual int toggle_thrusters(bool bActivate, int SourceClient) = 0;  // 21 +0x54
    // Ship .text:06d02500 (RET 8): toggles the thrusters. Paired getter
    // IObjInspect::are_thrusters_active.

    virtual int use_item(unsigned short EquipId, unsigned Count, int SourceClient) = 0;  // 22 +0x58
    // .text:06ce7210 (RET 0xc): removes up to Count of a cargo item (CECargo::Remove...,
    // clamped to what is held) and notifies the system.

    virtual int request_event(int RequestType, unsigned const* TargetId, unsigned const* SecondId,
                        int bFlag, RequestCB* Callback) = 0;  // 23 +0x5C
    // Ship .text:06d02670 (RET 0x14): unless a request is already pending (+0x94), stores
    // Callback at +0x98, latches +0x94 and raises controller event 0x12 / 0x19 / 0x14 for
    // RequestType 0 / 1 / 2. pub::SpaceObj::DockRequest (.text:06cfa6d9) pushes
    // (0, &TargetId, &<zero dword .rdata:06d66b04>, 1, nullptr); DockBehavior passes itself
    // as the RequestCB.

    virtual int request_cancel(int RequestType, unsigned const* TargetId, int Arg3) = 0;  // 24 +0x60
    // Ship .text:06d027a0 (RET 0xc): clears the pending request (+0x98 / +0x94) and raises
    // controller event 0x10 / 0x1a / 0x15 for RequestType 0 / 1 / 2.

    virtual int go_tradelane(IObjInspect const* EntranceRing, IObjInspect const* ExitRing,
                             bool bFastExit, float Speed) = 0;  // 25 +0x64
    // Ship .text:06d028c0 (RET 0x10): CShip::go_tradelane(EntranceRing, ExitRing,
    // <this IObjRW>, bFastExit, Speed) (IAT 06d645f4), and on success notifies the system.

    virtual int stop_tradelane() = 0;  // 26 +0x68
    // Ship .text:06d02880 (RET): CShip::stop_tradelane (IAT 06d645f0), then notifies the
    // system with the entrance / exit rings.

    virtual int slot_6c(int ArgA, int ArgB) = 0;  // 27 +0x6C
    // No Server.dll wrapper overrides it: all thirteen tables keep GameObject's
    // .text:06cef350 (`MOV EAX,-3; RET 8`). Common calls it (4, 0), (1, 0) and (0, 0).

    virtual int launch_complete(unsigned BaseObjId) = 0;  // 28 +0x70
    // Ship .text:06d02000: clears the one-shot latch at Ship+0xb8, sets invincibility, and
    // raises controller event 0x28 LaunchComplete {BaseObjId, get_id()}. Common passes a base
    // object id.

    virtual int jump_in_complete(unsigned SystemId) = 0;  // 29 +0x74
    // Ship .text:06d02070: raises controller event 0x26 JumpInComplete {SystemId, get_id()} and clears
    // invincibility.
};

// IObjRW — the read/write handle on a simulated object, and the thing the whole AI side
// passes around. It adds no slots of its own; it exists to bundle the two interfaces and the
// watcher-list node.
//
// RTTI: CHD .rdata:063a8b8c (multiple inheritance, numBaseClasses 4, array .rdata:063a8b78:
// IObjRW, IObjInspect, IObjAffect, Watchable). The COL at .rdata:063a8b9c (for the
// IObjAffect table at .rdata:0639f154, 30 _purecall entries since IObjRW is abstract) has
// offset 4, so IObjInspect is at 0 and Watchable at 8.
//
// The IObjAffect sub-object, `(char*)ObjRw + 4`. A cast because callers still hold the
// object as void*; with a real IObjRW* a static_cast does the same.
inline IObjAffect* ObjAffectOf(void* ObjRw)
{
    if (ObjRw == nullptr)
        return nullptr;
    return reinterpret_cast<IObjAffect*>(static_cast<char*>(ObjRw) + 4);
}

struct IObjRW : IObjInspect, IObjAffect, Watchable
{
};

static_assert(sizeof(IObjRW) == 0x0C, "IObjRW is two vtable pointers plus Watchable");

// ============================================================================
// IObjInspectImpl — concrete implementation delegating to CObject/CSimple/CShip
// ============================================================================

// Layout from the ctor (0x062befa0):
//   +0x00  IObjInspect vtable ptr   \
//   +0x04  IObjAffect vtable ptr     > IObjRW
//   +0x08  Watchable::Head          /
//   +0x0C  CBase vtable ptr         \
//   +0x10  CBase::Object            / the CObject all the getters delegate to
// sizeof = 0x14
//
// Retail's ClassHierarchyDescriptor for IObjInspectImpl (.rdata:063a8b40, numBaseClasses 6,
// base array .rdata:063a8b24) lists IObjInspectImpl, IObjRW, IObjInspect, IObjAffect,
// Watchable, CBase -- i.e. `IObjInspectImpl : IObjRW, CBase`.
//
// TODO: the AI side still passes this object around as `void*` in the Behavior family's
// `SetContext(void*, void*, void*)` signatures (Behavior::OwnerContext itself is an IObjRW*).
struct COMMON_API IObjInspectImpl : public IObjRW, public CBase
{
    IObjInspectImpl();
    IObjInspectImpl(IObjInspectImpl const& Other);
    virtual ~IObjInspectImpl();
    IObjInspectImpl& operator=(IObjInspectImpl const& Other);

    // IObjInspect implementation — delegates to CObject at +0x10
    int get_toughness() const override;
    Vector const& get_position() const override;
    Vector get_velocity() const override;
    Vector get_angular_velocity() const override;
    Transform const& get_transform() const override;
    Matrix const& get_orientation() const override;
    long get_index() const override;
    unsigned int const get_id() const override;
    int get_good_id(unsigned& OutId) const override;
    int get_archetype_extents(Vector& Min, Vector& Max) const override;
    float get_mass() const override;
    int get_physical_radius(float& Radius, Vector& Center) const override;
    int get_status(float& Health) const override;
    int get_status(float& HitPts, float& MaxHitPts) const override;
    int get_shield_status(float& Health, bool& bActive) const override;
    int get_shield_status(float& CurHealth, float& MaxHealth, bool& bActive) const override;
    int get_data(void const*& OutData) const override;
    float get_time_to_accelerate(float From, float To, float Dt,
                                 ThrustEquipType Type) const override;
    float get_distance_travelled(float Speed, float Dt, float Accel,
                                 ThrustEquipType Type) const override;
    float get_initial_speed_to_coast_distance(float Speed, bool bStop) const override;
    float get_speed(float Throttle, ThrustEquipType Type) const override;
    float get_projected_throttle(float Speed, ThrustEquipType Type) const override;
    float get_time_to_accelerate_angularly(float From, float To, float Dt) const override;
    float get_time_to_accelerate_angularly(float From, float To, float Dt,
                                            float MaxSpeed) const override;
    float get_angular_distance_travelled(float From, float To, float Dt) const override;
    float get_angular_distance_travelled(float From, float To, float Dt,
                                          float MaxSpeed) const override;
    float get_angular_speed_XY(float Speed, float MaxSpeed) const override;
    float get_projected_axis_throttle_XY(float Speed) const override;
    float get_projected_axis_throttle_Z(float Speed) const override;
    float get_angular_speed_Z(float Speed) const override;
    float get_max_bank_angle() const override;
    int get_throttle(float& OutThrottle) const override;
    int get_axis_throttle(Vector& OutThrottle) const override;
    int get_nudge_vec(Vector& OutVec) const override;
    int get_strafe_dir(StrafeDir& OutDir) const override;
    int are_thrusters_active(bool& OutActive) const override;
    int is_cruise_active(bool& OutActive) const override;
    int is_cruise_active(bool& OutCruise, bool& OutCharging) const override;
    int get_target(IObjRW*& OutTarget) const override;
    int get_rank(uint& OutRank) const override;
    int get_affiliation(uint& OutAffil) const override;
    int get_type(uint& OutType) const override;
    int get_base(uint& OutBase) const override;
    int get_dock_target(uint& OutDock) const override;
    int get_zone_props(unsigned long& OutProps) const override;
    float get_scanner_interference() const override;
    int get_power(float& OutPower) const override;
    int get_reputation(float& OutRep, uint const& GroupId) const override;
    int get_attitude_towards(float& OutAtt, IObjInspect const* Other) const override;
    int get_attitude_towards_symmetrical(float& OutAtt, IObjInspect const* Other,
                                          float& OutReverse) const override;
    bool is_player() const override;
    int get_hardpoint(char const* Name, Vector* Pos, Matrix* Orient) const override;
    // Not a slot of IObjInspect: exported as
    // `?find_equipment@IObjInspectImpl@@ABEHAAPBVCEquip@@ABG@Z` (`ABE`: private, non-virtual).
private:
    int find_equipment(CEquip const*& OutEquip, unsigned short const& EquipId) const;

public:
    int find_equipment(unsigned short* const Buf, uint MaxCount, uint TypeMask) const override;
    int get_equipment_status(EquipStatus& OutStatus, unsigned short const& Id) const override;
    int get_equipment_val(EquipmentVal& OutVal, unsigned short const& Id,
                           EquipmentValType Type, float Param) const override;
    int can_point_at(bool& OutCanPoint, unsigned short const& EquipId,
                      Vector const& Point, float Angle) const override;
    int is_pointing_at(bool& OutIsPointing, unsigned short const& EquipId,
                       float Angle) const override;
    int get_tgt_lead_fire_pos(unsigned short const& EquipId, Vector& OutPos) const override;
    int get_subtarget(unsigned short& OutSub) const override;
    int get_subtarget_center_of_mass(Vector& OutCenter) const override;
    Vector get_center_of_mass() const override;
    int get_sub_obj_center_of_mass(unsigned short SubId, Vector& OutCenter) const override;
    bool is_targetable() const override;
    int get_scanlist(ScanList const*& OutList, uint Type, bool bFlag) const override;
    int get_hold_left(float& OutHold) const override;
    CObject const* cobject() const override;
    bool has_dock_hardpoints() const override;
    int get_dock_hardpoints(int Index, TERMINAL_TYPE* Type, Transform* XformA,
                             Transform* XformB, Transform* XformC,
                             float* Distance) const override;
    int enumerate_subtargets(SubtargetEnumerator* Enum) const override;
    int enumerate_cargo(CargoEnumerator* Enum) const override;
    int scan_cargo(IObjRW* Scanner, EquipDescVector& OutCargo) const override;
    int get_formation_offset(Vector& OutOffset) const override;
    int get_formation_leader(IObjRW*& OutLeader) const override;
    int get_follow_offset(Vector& OutOffset) const override;
    int get_follow_leader(IObjRW*& OutLeader) const override;
    bool generate_follow_offset(IObjInspect const* Leader, Transform& OutXform) const override;
    int get_behavior_id() const override;
    int get_formation_followers(IObjRW** const OutFollowers, uint MaxCount) const override;
    int get_atmosphere_range(float& OutRange) const override;
    int is_using_tradelane(bool* OutUsing) const override;
    int get_lane_start(IObjInspect const* Self, IObjInspect const*& OutLane,
                       Vector& OutStart, Vector& OutEnd) const override;
    int get_lane_direction(Transform const& Xform, bool* OutDir) const override;
    int get_ring_side(Transform const& Xform, bool* OutSide) const override;
    int traverse_rings(uint& OutRing, bool bForward) const override;
    bool is_dying() const override;

    // The port's delegate-cast helpers (CSimple / CShip / CEqObj by ObjectClass mask) are
    // file-local functions in common_iobjinspectimpl.cpp: retail inlines those tests and exports no
    // such members.
};

static_assert(sizeof(IObjInspectImpl) == 0x14, "IObjInspectImpl sizeof 0x14");

// ============================================================================
// IBehaviorManager — AI behavior control interface
// ============================================================================

// Layout from the method bodies:
//   +0x00  vtable ptr
//   +0x04  IStateGraph* (get_state_graph returns this+4)
//   +0x08  SystemManager* (get_sys returns this+8)
//   +0x0C  ... various fields up to 0x1C8+
//
//   +0xB8  maneuver bitmask
//   +0xBC  field checked in external_player_evaluate
//   +0xC0  current behavior index (-1 = none)
//   +0xC4  IDirectiveInfo* (get_current_directive returns this+0xc4)
//   +0xC8  DirectivePriority (set_directive_priority writes this+200=0xC8)
//   +0xD8  behavior dirty flag
//   +0xF0  IObjRW* watchable ptr
//   +0x154  camera level status
//   +0x164  turn sensitivity
//   +0x174  behavior dispatch table base
//   +0x1C8  flags byte
//
// FUN_062ed780's receiver is SystemManager+0x142C (TargetManager), not an
// IBehaviorManager: UpdateBehaviorTick does `MOV ECX,[ESI+0x8]; ADD ECX,0x142c; CALL`
// (.text:062d2bff-062d2c08), and SystemManager's ctor builds that sub-object with
// FUN_062ed570 (.text:062e62bf). Its [ESI+4] is the back-pointer to SystemManager.
class IBehaviorManager;
class Behavior;   // common_behavior.h -- what the dispatch table holds

COMMON_API IBehaviorManager* __cdecl GetBehaviorManager(IObjRW* Obj);

// ----------------------------------------------------------------------------
// NotificationNode — intrusive doubly-linked node for IBehaviorManager's
// notification / delayed-callback queue.
//
// The queue root is at IBehaviorManager+0x158 (Pad158/NotificationQueueHead/
// NotificationCount). Node shape from FUN_062d4d60 (payload copy), FUN_062d46f0
// (watcher teardown) and FUN_062d4450 (per-tick walk/expiry):
//   - operator_new(0x24) at every allocation site (FUN_062d4cf0, and an inline
//     duplicate inside FUN_062d4520) -- sizeof 0x24 (36 bytes).
//   - +0x00/+0x04 Next/Prev: standard circular doubly-linked list, confirmed by
//     the insert-before-anchor sequence in FUN_062d4cf0/FUN_062d4520 (new.next=
//     anchor; new.prev=anchor.prev; anchor.prev=new; oldPrev.next=new) and the
//     matching unlink sequence in FUN_062d4c50/FUN_062d4d20 (prev.next=next;
//     next.prev=prev).
//   - +0x08 Mode: read by FUN_062d4450's expiry dispatch (CMP/DEC/JNZ against
//     0 and 1) to pick which of the two delayed ops re-fires; written as a
//     literal 0 by FUN_062d4520's own insert (QueueNotification) and literal 1
//     by FUN_062d4750's own insert (InsertNotification) -- i.e. each op tags
//     its own queued node so the SAME op re-enters on expiry.
//   - +0x0C/+0x14 WatcherA/WatcherB: two BaseWatchers, zeroed then optionally
//     pointed at a target (the port uses the public BaseWatcher::set()).
//   - +0x1C Extra: the "Extra" argument, forwarded verbatim to whichever op
//     Mode selects. It ends up as the notification code FUN_062d4750's
//     immediate path switches on (0..3, jump table 062d49b4; > 3 does nothing).
//   - Only a Duration > 0.0f queues a node at all (both ops: `FCOMP 0.0f;
//     TEST AH,0x41; JNZ <immediate path>`); Duration <= 0 acts immediately.
//     So a node always holds a positive Timer when inserted, and its expiry
//     re-enters the op with Duration 0, i.e. the immediate path.
//   - +0x20 Timer: a float, decremented by Dt every UpdateNotificationQueue
//     tick (FUN_062d4450: `FLD [node+0x20]; FSUB dt; FSTP [node+0x20]`) and
//     compared against 0.0f (DAT_06399408) to detect expiry.
// ----------------------------------------------------------------------------
struct NotificationNode
{
    NotificationNode* Next;     // +0x00  intrusive list link
    NotificationNode* Prev;     // +0x04  intrusive list link
    unsigned Mode;               // +0x08  0 -> re-fire QueueNotification, 1 -> re-fire InsertNotification
    BaseWatcher WatcherA;         // +0x0C .. +0x13
    BaseWatcher WatcherB;         // +0x14 .. +0x1B
    unsigned Extra;             // +0x1C  Extra (notification code), forwarded verbatim (see notes above)
    float Timer;                  // +0x20  countdown; <= 0.0f fires Mode's op and retires the node
};

static_assert(offsetof(NotificationNode, Prev) == 0x04, "NotificationNode+0x04 Prev");
static_assert(offsetof(NotificationNode, Mode) == 0x08, "NotificationNode+0x08 Mode");
static_assert(offsetof(NotificationNode, WatcherA) == 0x0C, "NotificationNode+0x0C WatcherA");
static_assert(offsetof(NotificationNode, WatcherB) == 0x14, "NotificationNode+0x14 WatcherB");
static_assert(offsetof(NotificationNode, Extra) == 0x1C, "NotificationNode+0x1C Extra");
static_assert(offsetof(NotificationNode, Timer) == 0x20, "NotificationNode+0x20 Timer");
static_assert(sizeof(NotificationNode) == 0x24, "NotificationNode sizeof 0x24");

// One entry of IBehaviorManager's inline behavior-scoring scratch table (IBehaviorManager+0x10,
// 21 entries), from UpdateBehaviorTick's re-evaluation pass (FUN_062d2b10): retail writes the
// pair with `FST [EBP]` / `MOV [EBP+4],EDI` on a cursor stepping by 8, and reads it back with
// `FLD [EBX+ECX*8]` / `MOV EDX,[EBX+ECX*8+4]` -- a float followed by the int behavior index it
// came from. Weight is the candidate's transition weight for this tick (see the scoring block in
// common_behaviormanager.cpp for how it is derived), BehaviorIndex the index into BehaviorDispatchTable
// that produced it.
struct BehaviorScore
{
    float Weight;       // +0x00
    int BehaviorIndex;  // +0x04
};
static_assert(sizeof(BehaviorScore) == 0x08, "BehaviorScore sizeof 0x08");

// EXPORTS: member-level. Retail exports exactly the 41 methods marked COMMON_API below and
// nothing else of this class -- no constructor, destructor, operator= or ??_7 -- so the class
// itself carries no export macro; the non-exported members are retail-internal functions.
// The argument of IBehaviorManager::Slot00 (LAB_062d3f90): the dispatcher reads
// `[arg]` as the case selector (.text:062d3fab) and `[arg+4]` as the case's payload pointer
// (.text:062d3fc0 / 062d41a8 / 062d4271 ...), and hands the whole block unchanged to the
// current behavior's Slot22 (.text:062d40e9 / 062d41fa). Retail struct name not recovered.
struct FBehaviorManagerEvent
{
    // The values of Type, from IBehaviorManager::OnEvent's cases and the producers' payloads.
    enum EType
    {
        kEventDamageTaken = 0,       // FBehaviorDamageEvent: Server's apply_*_entry
        kEventMunitionHit = 1,       // FMunitionImpact, EqObj::munition_hit; OnEvent only reads it
        kEventSetManeuver = 2,       // XSetManeuver: completes a directive whose target is [Data+8]
        kEventObjectLeftSystem = 3,  // {id, type bits, ...}: StarSystem::remove_object
        kEventTradelane = 4,         // FBehaviorTradelaneEvent
        kEventDockRequest = 5,       // {TargetId, TargetType, ShipId, bHasPort}: pub::SpaceObj::Dock
        kEventMissileLaunched = 6,   // {OwnerId, MissileId}: appended to the system manager's timer list
        kEventObjectDying = 7,       // {ObjectId}, sent when its death fuse is lit: the owner itself resets to its default behavior, a formation leader is suppressed
    };
    int Type;          // +0x00 -- 0..7
    void* Data;        // +0x04 -- per-case payload
};

// The case-4 payload of IBehaviorManager::Slot00 (Common .text:062d41a5 .. 062d41fb reads
// byte +0, byte +2, dwords +4 / +0xc / +0x10 / +0x14). Built by Server.dll's
// StarSystem::notify_go_tradelane (FUN_06d0df40, .text:06d0e069 .. 06d0e0a7: +0 = 1,
// +1 = 0, +2 = bFastExit, +4 = Speed, +8 = 0, +0xc = ship, +0x10 = entrance, +0x14 = exit)
// and notify_stop_tradelane (FUN_06d0e120, .text:06d0e244 .. 06d0e27d: +0/+1/+2 = 0,
// +4 = 0, +8 = 0, +0xc = ship, +0x10 = entrance, +0x14 = 0). +3 is never written.
// Retail struct name not recovered.
struct FBehaviorTradelaneEvent
{
    // +0x00 -- 1 entering a lane (notify_go_tradelane), 0 leaving it (notify_stop_tradelane);
    // BaseFollowerBehavior::Slot22 (062cbecb..062cbf62) treats 0 as "left the lane".
    unsigned char bEnteringLane;
    // +0x01 -- 0 at both Server sites; 1 only from CShip::request_exit_tradelane (062b191c),
    // which sends it to the followers: a follower whose leader it names then requests its own
    // exit (062cbecb..062cbf3b).
    unsigned char bExitRequested;
    unsigned char bFastExit;    // +0x02
    unsigned char Pad03;        // +0x03 -- never written
    float Speed;                // +0x04
    unsigned int Field_0x08;    // +0x08 -- always 0 at both sites
    unsigned int ShipId;        // +0x0c
    unsigned int EntranceId;    // +0x10
    unsigned int ExitId;        // +0x14
};
static_assert(sizeof(FBehaviorTradelaneEvent) == 0x18, "FBehaviorTradelaneEvent size");

// The case-0 payload of IBehaviorManager::Slot00 ("this object took damage"). Common's
// case 0 (FUN_062e86d0, AccumulateSystemManagerTimerLevel) reads +0x08 / +0x14 / +0x18.
// Built by Server.dll's EqObj::apply_root_entry (.text:06ceb61b .. 06ceb66c),
// apply_equip_entry, apply_group_entry and apply_power_entry: nine dwords copied from
// the controller event 0xd record, whose own order starts with the object id. Retail
// struct name not recovered.
struct FBehaviorDamageEvent
{
    unsigned int SystemId;            // +0x00
    unsigned int InflictorId;         // +0x04 -- DamageList+0x14
    unsigned int ObjId;               // +0x08 -- the damaged object's get_id()
    unsigned int EquipType;           // +0x0c
    unsigned int CollisionGroupType;  // +0x10
    int Cause;                        // +0x14 -- DamageList+0x10
    float Damage;                     // +0x18 -- hit points before - after
    float RelativeDamage;             // +0x1c
    float RelativeHealth;             // +0x20
};
static_assert(sizeof(FBehaviorDamageEvent) == 0x24, "FBehaviorDamageEvent size");

class IBehaviorManager
{
public:
    // Not exported by retail; see the class comment. FUN_062d2220 / FUN_062d2530. The ctor
    // takes the owning object (CEqObj::create_behavior_interface passes its IObjRW* param,
    // .text:062ac280-062ac287) -- retail has no default ctor.
    explicit IBehaviorManager(IObjRW* Owner);
    ~IBehaviorManager();

    // Exported methods (41 exports)
    COMMON_API bool allow_head_tracking();
    COMMON_API bool cancel_behavior(CancelRequestType Request);
    COMMON_API bool enable_all_maneuvers();
    COMMON_API bool enable_maneuver(int Maneuver, bool bEnable);
    COMMON_API EvaluateResult external_player_evaluate(int PlayerIndex);
    COMMON_API bool get_camera_level_status();
    COMMON_API float get_closest_trailing_ship() const;
    COMMON_API IDirectiveInfo const* get_current_directive();
    COMMON_API IObjRW const* get_debugger_target() const;
    COMMON_API bool get_docking_port(IObjRW const*& OutPort, int& OutIndex);
    COMMON_API unsigned get_parent_id();
    COMMON_API void get_personality(pub::AI::Personality& OutPersonality);
    COMMON_API pub::AI::ScanResponse get_scan_response(uint Type);
    // ?get_ship_up_direction@IBehaviorManager@@QAE?BVVector@@XZ -- a const Vector by value.
    COMMON_API const Vector get_ship_up_direction();
    COMMON_API IStateGraph const* get_state_graph();
    COMMON_API SystemManager* get_sys();
    COMMON_API bool get_user_turning_input_state();
    COMMON_API bool lock_maneuvers(bool bLock);
    COMMON_API void refresh_state_graph();
    COMMON_API void reset_current_behavior_direction();
    // ?set_camera_mode@IBehaviorManager@@QAEXW4IBehaviorCameraMode@1@@Z -- nested enum.
    enum IBehaviorCameraMode
    {
        CameraModeNormal = 0,
        CameraModeLevel = 1
    };

    COMMON_API void set_camera_mode(IBehaviorCameraMode Mode);
    COMMON_API void set_content_callback(pub::AI::ContentCallback* Callback);
    COMMON_API bool set_current_directive(IDirectiveInfo const& Dir, pub::AI::BaseOp const* Op);
    COMMON_API void set_directive_callback(pub::AI::DirectiveCallback* Callback);

    // Retail 0x62d3f10 (FUN_062d3f10). Not the mirror-image of
    // set_directive_callback's early-return-on-first-match: this scans and clears
    // *every* matching slot in DirectiveCallbacks[5], no early exit. Called from
    // LaunchAction's real destructor (Common.dll 0x62afec0) to unregister itself
    // before being freed, so a still-registered DirectiveCallbacks[] slot can't
    // later dispatch into freed memory.
    void clear_directive_callback(pub::AI::DirectiveCallback* Callback);
    COMMON_API pub::AI::OP_RTYPE set_directive_priority(pub::AI::DirectivePriority Priority);
    COMMON_API void set_personality(pub::AI::Personality const& Personality);
    COMMON_API void set_ship_up_direction(Vector const& Dir);
    COMMON_API void set_state_graph(int GraphId, bool bForce);
    COMMON_API void set_turn_sensitivity(float Sensitivity);
    // Not an export: internal getter for the value set_turn_sensitivity stores
    // at +0x164 (0x062d4b20, `FLD [ECX+0x164]; RET`).
    // Retail stores 1.0 - clamp(Sensitivity, 0, 0.9), not the raw sensitivity.
    float get_turn_sensitivity_factor() const;
    COMMON_API void set_user_turning_input_state(bool bState);
    COMMON_API void submit_camera_up(Vector const& Up);
    COMMON_API void update_current_behavior_afterburner(bool bOn);
    COMMON_API void update_current_behavior_auto_avoidance(bool bOn);
    COMMON_API void update_current_behavior_auto_level(bool bOn);
    COMMON_API void update_current_behavior_brake_reverse(bool bOn);
    COMMON_API void update_current_behavior_cruise(bool bOn);
    COMMON_API void update_current_behavior_direction(Vector const& Dir);
    COMMON_API void update_current_behavior_engage_engine(bool bOn);
    COMMON_API void update_current_behavior_slide_strafe_burst(StrafeDir Dir);
    COMMON_API void update_current_behavior_throttle(float Throttle);
    COMMON_API void update_level_camera(bool bEnable);

    // Notification / delayed-callback queue (not exported). FUN_062d4450 ->
    // UpdateNotificationQueue; FUN_062d4520 / FUN_062d4750 (the two delayed ops a
    // node's Mode selects) -> QueueNotification / InsertNotification; FUN_062d46f0
    // watcher teardown; FUN_062d4bf0 / FUN_062d4c90 full teardown and FUN_062d4c50 /
    // FUN_062d4d20 single unlink (each pair behaviorally identical).
    void UpdateNotificationQueue(float Dt);
    void QueueNotification(IObjRW* Target, unsigned Extra, float Duration);
    void InsertNotification(IObjRW* Target, unsigned Extra, float Duration);
    void ClearNotificationQueue();

    // FUN_062d2b10 (665 instructions, not exported): the per-tick method -- transition
    // dispatch, docking-target probe, 21-entry re-evaluation and weighted pick, and the
    // DirectiveCallbacks[] fan-out.
    void UpdateBehaviorTick(float Dt);

    // FUN_062d3420, `RET 0xc`. IBehaviorManager's own
    // behavior-transition primitive, called from ten sites (four of them inside
    // UpdateBehaviorTick). Deactivates the outgoing BehaviorDispatchTable entry
    // (Behavior::Slot4, +0x10), activates the incoming one (Behavior::Slot3,
    // +0x0c) and, when DirectiveArg is non-null, first offers it to the incoming
    // behavior through Behavior::Slot10 (+0x28) whose bool result becomes this
    // method's own return value. Returns false without transitioning when the
    // watched object is the player and the requested behavior is either masked out
    // by ManeuverMask or blocked by LockManeuversByte; returns true immediately
    // when the requested index is already current and bForce is clear.
    bool SetCurrentBehavior(int NewIndex, bool bForce, void* DirectiveArg);

    // The DirectiveCallbacks[5] fan-out UpdateBehaviorTick runs at both of its
    // "transition completed" sites (two identical loops, unrolled by five in retail).
    // Not a retail symbol.
    void NotifyDirectiveCallbacks();

    // FUN_062d29a0, `RET 8`, 15 call sites across the behavior family:
    // `MOV [ECX+0x118],param_2; MOV [ECX+0x11c],param_1`. The behavior reports which
    // behavior index finished (CompletedBehavior) and the result word (DirectiveResult) that
    // NotifyDirectiveCallbacks later hands to every DirectiveCallback.
    void SetDirectiveResult(int CompletedBehavior, int DirectiveResult);

    // FUN_062d35c0, thiscall, `RET 4`, not exported (retail name unknown). While
    // LevelCameraFlag is set and bApply is non-zero: when the watched ship's forward axis
    // (column 2 of get_transform) has |y| < 0.70700073f (DAT_063a033c), LevelCameraFlag is
    // dropped for the CheckSystemManagerSteeringGate query and, when that passes,
    // ShipUpDirection = (0, 1, 0) and ShipUpDirFlag = 0; LevelCameraFlag is then set again.
    // Sole caller: FaceIdea::Update (.text:062d707b) for the player ship.
    void RelevelShipUpDirection(bool bApply);

    // FUN_062d2ad0, `RET 0x18`, sole caller FleeBehavior::Perform
    // (.text:062c7316). With a content callback (+0x110): tail-jump into its slot 4
    // (+0x10, request_flee_destination) with all six arguments. Without one:
    // Sys->ExternalIface->is_player() (+0xbc) for effect, then false.
    bool RequestFleeDestination(uint ObjectId, pub::AI::ContentCallback::FleeReason Reason,
                                pub::AI::ContentCallback::FleeDestinationType& OutType,
                                Vector& OutPosition, uint& OutParam, float& OutRadius);

    // FUN_062d29c0, `RET 0x10`. Only while Sys->Targeting.AttackState (+0x1434)
    // is nonzero and behaviour 0x11 is current: with a content callback, its slot 0
    // follow_event(all four arguments) and true; without one, Sys->ExternalIface->is_player()
    // for effect. False otherwise. Callers: FollowBehavior::Slot27 (062cd043, 062cd164).
    bool NotifyFollowEvent(uint ObjectId1, uint ObjectId2,
                           pub::AI::ContentCallback::FollowStatusType Status, float Distance);

    // FUN_062d2a10, `RET 0x10`. NotifyFollowEvent's twin for behaviours 2 and 7,
    // through slot 1 tether_object_event. Caller: ProxWatcherNotifyZoneChange (062c2e28).
    bool NotifyTetherEvent(uint ObjectId1, uint ObjectId2,
                           pub::AI::ContentCallback::TetherStatusType Status, Vector const& Position);

    // The one virtual of retail IBehaviorManager: vftable 063a037c holds a single entry,
    // LAB_062d3f90 (`RET 4`), installed by both ctors (.text:062d22bf / 062d2552). Retail
    // name not recovered. The only virtual (no virtual dtor), so the vptr sits at +0x00 as in
    // retail. Not exported: Server.dll reaches it through the vptr (`MOV EDX,[ECX]; CALL
    // [EDX]`).
    // Event->Type selects one of eight cases; Event->Data is a per-case payload block whose
    // offsets are listed on the definition in common_behaviormanager.cpp. Types above 7, and any event
    // while CurrentBehaviorIndex == -1, are ignored.
    virtual void OnEvent(FBehaviorManagerEvent const* Event); // vftable 063a037c +0x00

private:
    void InitNotificationQueue();
    NotificationNode* AllocNotificationNode(NotificationNode* NextIfAny, NotificationNode* PrevIfAny);
    NotificationNode* InsertNotificationNodeBefore(NotificationNode* Anchor);
    static void ReleaseNotificationWatchers(NotificationNode* Node);

public:
    // Layout summary:
    // +0x00: vptr (compiler-generated; one-slot vftable, see OnEvent)
    // +0x04: IStateGraph* StateGraph
    // +0x08: SystemManager* Sys
    // +0xB8: ManeuverMask (unsigned)
    // +0xBC: LockManeuversByte (bool written by lock_maneuvers)
    // +0xC0: CurrentBehaviorIndex (int, -1 = none)
    // +0xC4..+0xCF: CurrentDirective storage (IDirectiveInfo copied as 3 dwords)
    // +0xD0: BehaviorTimerA (float)
    // +0xD4: BehaviorTimerB (float)
    // +0xD8: BehaviorDirty (byte)
    // +0xDC: BehaviorTimerC (float)
    // +0xF0: WatchablePtr (void*)
    // +0xF8: bScanState (byte): the attack/scan state UpdateBehaviorTick last reported through
    //        ContentCallback::scan_state_change
    // +0xF9: LevelCameraFlag (byte)
    // +0xFC..+0x10F: DirectiveCallbacks[5] (5 pointers)
    // +0x110: ContentCallback* ptr
    // +0x114: ContentCallback get_creation_type result
    // +0x120: field used in set_current_directive
    // +0x124/0x128/0x12C: direction reset zeros these
    // +0x130: ShipUpDir flag (byte)
    // +0x134: ShipUpDirection (Vector, 12 bytes)
    // +0x140: CameraUp (Vector, 12 bytes)
    // +0x150: UserTurningInputState (byte)
    // +0x154: CameraLevelStatus (int)
    // +0x164: TurnSensitivity (float)
    // +0x169: DockingPortFlag (byte)
    // +0x16C: DockingPortStatus (int)
    // +0x174 .. +0x1C7: BehaviorDispatchTable[21] (array of behavior object pointers)
    // +0x1C8: Flags1c8 (byte)

    // +0x00 is the compiler's vptr (virtual Slot00 above), matching retail's vftable 063a037c.
    IStateGraph* StateGraph;            // +0x04
    SystemManager* Sys;                 // +0x08
    // +0x0C: the owned PathfindManager (common_pathfind.h). The ctor allocates it
    // (.text:062d2327-062d2369: operator_new(0x580) + FUN_062db3b0(this)), the dtor deletes it
    // (.text:062d2573-062d25c4); UpdateBehaviorTick ticks it through FUN_062dd760, and
    // Server.dll's pub::AI::SubmitState calls its user-zone exports through this word.
    PathfindManager* Pathfind;          // +0x0C
    // +0x10 .. +0xB7: the behavior-scoring scratch table UpdateBehaviorTick's re-evaluation
    // fills (`EBX = this+0x10`, 8-byte stride, 21 entries; 0x10 + 21*8 == ManeuverMask's
    // +0xB8). Rewritten each pass; the cursor's final position gives the count.
    BehaviorScore BehaviorScores[21];   // +0x10 .. +0xB7
    unsigned ManeuverMask;              // +0xB8
    unsigned char LockManeuversByte;    // +0xBC
    unsigned char PadBd[3];             // +0xBD .. +0xBF
    int CurrentBehaviorIndex;           // +0xC0
    unsigned char DirectiveStorage[12]; // +0xC4 .. +0xCF (IDirectiveInfo copy)
    // Timer trio of UpdateBehaviorTick's dispatch: BehaviorTimerA accumulates Dt and wraps
    // via fmodf(Timer, 3.0f) (DAT_063a0340) once it reaches 3.0f (`TEST AH,0x1`, i.e. >=,
    // unlike AttackOverrideTimer's strict >), and is reset to 0 whenever
    // CurrentBehaviorIndex == 6. BehaviorTimerB and BehaviorTimerC accumulate Dt; the
    // dispatch tail also reads and resets BehaviorTimerB.
    float BehaviorTimerA;               // +0xD0
    float BehaviorTimerB;               // +0xD4
    unsigned char BehaviorDirty;        // +0xD8
    unsigned char OpaqueD9[3];          // +0xD9 .. +0xDB
    float BehaviorTimerC;               // +0xDC
    // Timer pair gating UpdateBehaviorTick's periodic UpdateAttackTargetOverride: the
    // timer accumulates Dt (FADD [this+0xe0]); once past AttackOverridePeriod (FCOMP
    // [this+0xe4]) it is wrapped via fmodf and Sys->Targeting.UpdateAttackTargetOverride()
    // fires. AttackOverridePeriod is written only by the ctor FUN_062d2220 (1.0f at
    // .text:062d2261 / 062d242c / 062d2466); UpdateBehaviorTick (062d2bdd / 062d2bee) reads it.
    float AttackOverrideTimer;          // +0xE0
    float AttackOverridePeriod;         // +0xE4
    // +0xE8: the id of the last target reported through ContentCbPtr->ship_in_sights_event
    // (slot +0x0C). UpdateBehaviorTick (.text:062d2ca3-062d2ccd) compares the current
    // target's get_id() against it, stores and reports on change, and zeroes it whenever the
    // in-sights test fails. Zeroed by the ctor (.text:062d24f6).
    unsigned int InSightsTargetId;      // +0xE8
    // +0xEC: the object database the owner is connected to. Zeroed by the ctor
    // (.text:062d22e9); written only by ConnectBehaviorManagerObjectDb (FUN_062d2830,
    // .text:062d2840), which CEqObj::connect / disconnect / create_behavior_interface call
    // with the owner's CSimple+0xAC ObjectDb (or 0 on disconnect).
    IObjDB* ObjectDb;                   // +0xEC
    void* WatchablePtr;                 // +0xF0
    unsigned char OpaqueF4[4];          // +0xF4 .. +0xF7
    // Reconciled "content-callback active" style flag: recomputed every tick as
    // (Sys+0x1434 [SystemManager::Targeting.AttackState, TargetManager+0x08,
    // ctor 0x062ed570] == 2) && (CurrentBehaviorIndex == 2) &&
    // (DirectiveStorage's first dword == 0 || DirectiveStorage's second
    // dword == 1); only when the recomputed value differs from the stored
    // byte here does UpdateBehaviorTick store it and fire a notify through
    // ContentCbPtr's own vtable slot 0x18. Real semantic name not recovered.
    unsigned char bScanState;            // +0xF8
    unsigned char LevelCameraFlag;      // +0xF9
    unsigned char OpaqueFa[2];          // +0xFA .. +0xFB
    pub::AI::DirectiveCallback* DirectiveCallbacks[5]; // +0xFC .. +0x10F
    pub::AI::ContentCallback* ContentCbPtr;   // +0x110
    unsigned ContentCbCreationType;     // +0x114
    // +0x118/+0x11C: the two-word "directive result" payload UpdateBehaviorTick's dispatch
    // tail hands to every registered DirectiveCallbacks[] entry (`PUSH [this+0x118]; PUSH
    // [this+0x11C]; CALL [*Cb]`: PendingCompletedBehavior is the callback's first parameter).
    // Written as literals at .text:062d2ea9 / 062d3244 and by SetDirectiveResult
    // (FUN_062d29a0, 15 callers), e.g. (0x14, 0) DelayBehavior .text:062d0b14, (2, 0)
    // GotoBehavior 062fddf2, (0xe, 1) GuideBehavior 062cf56a. PendingCompletedBehavior holds
    // the finished behavior index, the current directive's third dword, or -1 (the reset the
    // tail leaves). Further +0x118 stores: the ctor FUN_062d2220 (.text:062d2332, ESI = this)
    // and OnEvent (.text:062d4193, on the same ESI as its +0x170 store at 062d42cb).
    int PendingDirectiveResult;                       // +0x118
    int PendingCompletedBehavior;                       // +0x11C
    unsigned DefaultBehaviorIndex;                  // +0x120
    // Last direction vector passed to update_current_behavior_direction (or zeroed by
    // reset_current_behavior_direction) -- 0x062d3690 / 0x062d3b10.
    Vector LastCommandedDirection;      // +0x124 .. +0x12F
    unsigned char ShipUpDirFlag;        // +0x130
    unsigned char Pad131[3];            // +0x131 .. +0x133
    Vector ShipUpDirection;            // +0x134
    Vector CameraUp;                   // +0x140
    unsigned char Opaque14c[4];         // +0x14C .. +0x14F
    unsigned char UserTurningInputState; // +0x150
    unsigned char Pad151[3];            // +0x151 .. +0x153
    int CameraLevelStatus;              // +0x154
    // Notification queue root (see NotificationNode). +0x158 is not touched by the queue
    // functions (FUN_062d4450..062d4d60).
    unsigned char Pad158[4];            // +0x158 .. +0x15B  reserved/unidentified
    NotificationNode* NotificationQueueHead; // +0x15C  circular-list sentinel (self-referencing when empty)
    int NotificationCount;              // +0x160
    float TurnSensitivity;              // +0x164
    unsigned char bAllowUserSteering;   // +0x168
    unsigned char DockingPortFlag;      // +0x169
    unsigned char Opaque16a[2];         // +0x16A .. +0x16B
    int DockingPortStatus;              // +0x16C
    // Nonzero skips UpdateBehaviorTick (`TEST AL,AL; JNZ <early-return>`, read at 062d2b2d).
    // The ctor zeroes it (062d22c5); OnEvent case 7 sets 1 (.text:062d42cb) when the event
    // names the ship itself.
    unsigned char TickSuppressedFlag;   // +0x170
    unsigned char Opaque171[3];         // +0x171 .. +0x173
    // +0x174 .. +0x1C7: the behavior dispatch table, indexed by CurrentBehaviorIndex.
    // 21 entries: UpdateBehaviorTick's loop walks 0x15 entries from +0x174 and indexes the
    // state graph's 21x21 transition matrix; the ctor FUN_062d2220 fills i = 0..0x14; and
    // +0x174 + 21*4 == Flags1c8. Reads of +0x190 / +0x19C are entries 7 / 10 with the index
    // constant-folded (guarded by CurrentBehaviorIndex == 7 / 0xa).
    Behavior* BehaviorDispatchTable[21]; // +0x174 .. +0x1C7
    unsigned char Flags1c8;             // +0x1C8
    unsigned char Pad1c9[3];
};

static_assert(offsetof(IBehaviorManager, InSightsTargetId) == 0xE8, "IBehaviorManager+0xE8 InSightsTargetId");
static_assert(offsetof(IBehaviorManager, NotificationQueueHead) == 0x15C,
              "IBehaviorManager+0x15C NotificationQueueHead");
static_assert(offsetof(IBehaviorManager, NotificationCount) == 0x160,
              "IBehaviorManager+0x160 NotificationCount");
static_assert(offsetof(IBehaviorManager, TickSuppressedFlag) == 0x170,
              "IBehaviorManager+0x170 TickSuppressedFlag");
// BehaviorScores / BehaviorDispatchTable extents: each array is pinned at both ends.
static_assert(offsetof(IBehaviorManager, BehaviorScores) == 0x10,
              "IBehaviorManager+0x10 BehaviorScores");
static_assert(offsetof(IBehaviorManager, ManeuverMask) == 0xB8,
              "IBehaviorManager+0xB8 ManeuverMask -- pins BehaviorScores[21]'s end");
static_assert(offsetof(IBehaviorManager, BehaviorDispatchTable) == 0x174,
              "IBehaviorManager+0x174 BehaviorDispatchTable");
static_assert(offsetof(IBehaviorManager, Flags1c8) == 0x1C8,
              "IBehaviorManager+0x1C8 Flags1c8 -- pins BehaviorDispatchTable[21]'s end");
static_assert(offsetof(IBehaviorManager, PendingDirectiveResult) == 0x118,
              "IBehaviorManager+0x118 PendingDirectiveResult");
static_assert(offsetof(IBehaviorManager, PendingCompletedBehavior) == 0x11C,
              "IBehaviorManager+0x11C PendingCompletedBehavior");
static_assert(offsetof(IBehaviorManager, ObjectDb) == 0xEC,
              "IBehaviorManager+0xEC ObjectDb");
static_assert(sizeof(IBehaviorManager) == 0x1CC,
              "IBehaviorManager sizeof 0x1CC (CEqObj::create_behavior_interface, operator_new(0x1cc))");

// FUN_062d2830, thiscall on the manager, `RET 4`.
// Not exported and not a named retail method, so it is a free function here (a member of
// this member-exported class would be one more export than retail has). Stores ObjectDb at
// +0xEC, hands it to every BehaviorDispatchTable entry through Behavior slot 2 (+0x08), then
// to the SystemManager (FUN_062e68e0, SetSystemManagerSharedListHead) and to the
// PathfindManager (FUN_062db7c0, SetPathfindObjectDb). Callers: CEqObj::connect
// (.text:062aa1b2, a tail JMP), CEqObj::disconnect (.text:062aa1e3, with 0) and
// CEqObj::create_behavior_interface (.text:062ac2ab).
void ConnectBehaviorManagerObjectDb(IBehaviorManager* Self, IObjDB* ObjectDb);
static_assert(offsetof(IBehaviorManager, AttackOverrideTimer) == 0xE0,
              "IBehaviorManager+0xE0 AttackOverrideTimer");
static_assert(offsetof(IBehaviorManager, AttackOverridePeriod) == 0xE4,
              "IBehaviorManager+0xE4 AttackOverridePeriod");
static_assert(offsetof(IBehaviorManager, BehaviorTimerA) == 0xD0,
              "IBehaviorManager+0xD0 BehaviorTimerA");
static_assert(offsetof(IBehaviorManager, BehaviorTimerB) == 0xD4,
              "IBehaviorManager+0xD4 BehaviorTimerB");
static_assert(offsetof(IBehaviorManager, BehaviorDirty) == 0xD8,
              "IBehaviorManager+0xD8 BehaviorDirty");
static_assert(offsetof(IBehaviorManager, BehaviorTimerC) == 0xDC,
              "IBehaviorManager+0xDC BehaviorTimerC");
static_assert(offsetof(IBehaviorManager, bScanState) == 0xF8,
              "IBehaviorManager+0xF8 bScanState");

// ============================================================================
// ScriptBehavior — serializable behavior description
// ============================================================================

// The values of DialogType (CharacterBehaviorDescription / ScriptBehavior); the client picks the
// cursor `talk_<name>` of the same name (FCharacterObject::UpdateCursor).
enum ECharacterDialogType
{
    kDialogBlowoff = 0,
    kDialogBribe = 1,
    kDialogRumor = 2,
    kDialogMission = 3,
    kDialogInfo = 4,
    kDialogStory = 5,
};

// CharacterBehaviorDescription — input to ScriptBehavior::init.
// Layout from init @ 0xCE530. Not exported by retail (no member of it is in the export
// table; ScriptBehavior::init names it `ABUCharacterBehaviorDescription@@`, a struct).
struct CharacterBehaviorDescription
{
    // Content.dll FUN_06eac990 (thiscall; callers the bar scene creator 06ea9724 and the
    // generic-script behavior builder 06f00bdc), after the two inlined FmtStr zeroing ctors
    // (FUN_06eac680). Retail then runs FmtStr::destroy on both and zeroes their 0x18 bytes
    // again -- a no-op on a freshly constructed FmtStr. IdArray / StringArray (and the pad
    // bytes) are not written.
    CharacterBehaviorDescription()
        : LocationId(0), bApplySpatial(1), bAutoplay(0), bGenericScript(1), RelocateLocationId(0), StartRoomLocationId(0), bBindSingleCharacter(1),
          IdArrayCount(0), StringArray2Count(0), DialogType(0), OfferId(0), OfferKind(0),
          RtcId(0), Filter(0)
    {
        for (int Index = 0; Index < 32; ++Index)
        {
            StringArray2[Index] = nullptr;
            StringArray2Dword[Index] = 0;
        }
    }
    // Content.dll FUN_06eacae0 (callers 06f011cb and the inlined copy at 06ea9e86-06ea9ecc):
    // FmtStr::destroy on +0x228 then +0x240, then the members' ~FmtStr (+0x240, +0x228).
    ~CharacterBehaviorDescription()
    {
        NameFmtStr.destroy();
        DescFmtStr.destroy();
    }

    int LocationId;               // +0x00 → this+0x04
    unsigned char bApplySpatial;  // +0x04 → this[8]
    unsigned char bAutoplay;      // +0x05 → this[9]
    unsigned char bGenericScript; // +0x06 → this[10]
    unsigned char Pad07;
    unsigned RelocateLocationId;  // +0x08 → this+0x0C
    unsigned StartRoomLocationId; // +0x0C → this+0x10
    unsigned char bBindSingleCharacter; // +0x10 → this[0x14]
    unsigned char Pad11[3];
    unsigned IdArray[32];         // +0x14 .. +0x93
    char const* StringArray[32];  // +0x94 .. +0x113
    int IdArrayCount;             // +0x114
    char const* StringArray2[32]; // +0x118 .. +0x197
    unsigned StringArray2Dword[32]; // +0x198 .. +0x217
    int StringArray2Count;        // +0x218
    unsigned DialogType;          // +0x21C
    unsigned OfferId;             // +0x220 echoed by the client in IServer::MissionResponse
    unsigned OfferKind;           // +0x224 pub::Controller::EMissionOfferKind
    FmtStr NameFmtStr;            // +0x228
    FmtStr DescFmtStr;            // +0x240
    unsigned RtcId;               // +0x258  retail trailing dword, copied into ScriptBehavior::Field25c
    // +0x25C. pub::GF::CharacterBehaviorCreate reads it
    // (Server.dll .text:06d18043 `MOV EAX,[ESI+0x25c]`) and passes it on as the
    // behaviour record's session filter (FUN_06d122f0 .text:06d12452). init()
    // does not read it.
    unsigned Filter;
};

// ScriptBehavior -- size 0x260: Server.dll's BehaviorRecord is operator new(0x26c) with the
// ScriptBehavior at +0x0c (.text:06d123f1-06d12424), and freelancer.exe's room bucket allocates
// operator new(0x260) for one (.text:0043c8d9). Two FmtStr (0x18 each) at +0x22c and +0x244,
// trailing dword at +0x25c. The default ctor zero-constructs both FmtStr inline
// (.text:0632dccf-0632dd8d) and then calls clear().
class COMMON_API ScriptBehavior
{
public:
    ScriptBehavior();
    ScriptBehavior(ScriptBehavior const& Other);
    ~ScriptBehavior();
    ScriptBehavior& operator=(ScriptBehavior const& Other);

    void clear();
    void destroy();
    // ?get_key@ScriptBehavior@@QBEKXZ -- returns `unsigned long` (K), not `unsigned int`.
    unsigned long get_key() const;
    void init(CharacterBehaviorDescription const& Desc);
    void marshal(MarshalBuffer* Buf) const;
    void unmarshal(MarshalBuffer* Buf);

public:
    int BehaviorId;               // +0x00  default -1
    unsigned LocationId;          // +0x04
    unsigned char bApplySpatial;  // +0x08  default 1
    unsigned char bAutoplay;      // +0x09
    unsigned char bGenericScript; // +0x0A  default 1
    unsigned char Pad0b;
    unsigned RelocateLocationId;  // +0x0C
    unsigned StartRoomLocationId; // +0x10
    unsigned char bBindSingleCharacter; // +0x14  default 1
    unsigned char Pad15[3];
    // Two parallel arrays of 0x20 entries each:
    //   ids at +0x18..+0x94 (32 * 4 = 0x80)
    //   strings at +0x98..+0x114 (32 * 4 = 0x80)
    unsigned IdArray[32];         // +0x18
    char* StringArray[32];        // +0x98
    int IdArrayCount;             // +0x118
    // Second pair of arrays -- corrected against ScriptBehavior::init/marshal/unmarshal/operator=
    // decompilation (@ 0xCE530/0xCDF90/0xCE160/0xCE370). The previous "Opaque11c[0xa0]" guess
    // was oversized and mis-sited StringArray2; retail actually keeps:
    //   strings at +0x11C..+0x19B (32*4), a per-entry dword at +0x19C..+0x21B (32*4), count at +0x21C.
    char* StringArray2[32];         // +0x11C
    unsigned StringArray2Dword[32]; // +0x19C  per-entry value paired with StringArray2[i] -- retail
                                     // marshal/unmarshal round-trips this; it must not be dropped.
    int StringArray2Count;        // +0x21C
    unsigned DialogType;          // +0x220
    unsigned OfferId;             // +0x224
    unsigned OfferKind;           // +0x228
    FmtStr NameFmtStr;            // +0x22C  (FmtStr is 0x18, common_text.h)
    FmtStr DescFmtStr;            // +0x244
    unsigned RtcId;                // +0x25C retail trailing dword (source: CharacterBehaviorDescription's
                                    // own trailing dword right after DescFmtStr).
};

static_assert(offsetof(ScriptBehavior, BehaviorId) == 0x00, "ScriptBehavior+0x00 id");
static_assert(offsetof(ScriptBehavior, IdArray) == 0x18, "ScriptBehavior+0x18 ids");
static_assert(offsetof(ScriptBehavior, StringArray) == 0x98, "ScriptBehavior+0x98 strings");
static_assert(offsetof(ScriptBehavior, IdArrayCount) == 0x118, "ScriptBehavior+0x118 count");
static_assert(offsetof(ScriptBehavior, StringArray2) == 0x11C, "ScriptBehavior+0x11C strings 2");
static_assert(offsetof(ScriptBehavior, StringArray2Dword) == 0x19C, "ScriptBehavior+0x19C dwords 2");
static_assert(offsetof(ScriptBehavior, StringArray2Count) == 0x21C, "ScriptBehavior+0x21C count 2");
static_assert(offsetof(ScriptBehavior, NameFmtStr) == 0x22C, "ScriptBehavior+0x22C NameFmtStr");
static_assert(offsetof(ScriptBehavior, DescFmtStr) == 0x244, "ScriptBehavior+0x244 DescFmtStr");
static_assert(offsetof(ScriptBehavior, RtcId) == 0x25C, "ScriptBehavior+0x25C trailing dword");
static_assert(sizeof(ScriptBehavior) == 0x260, "ScriptBehavior must be 0x260");

// ============================================================================
// BehaviorTypes — static lookup table for behavior IDs/names
// ============================================================================

// ?get_behavior_id@BehaviorTypes@@YAHPBD@Z and ?get_behavior_name@BehaviorTypes@@YAPBDH@Z --
// YA, so free functions in a NAMESPACE, not statics on a class.
namespace BehaviorTypes
{
    COMMON_API int __cdecl get_behavior_id(char const* Name);
    COMMON_API char const* __cdecl get_behavior_name(int Id);
}

// The behavior type ids get_behavior_id / get_behavior_name, IBehaviorManager::get_behavior_id
// and pub::AI::get_behavior_id deal in: the order of the behavior registry (the
// TBehaviorTypeDesc statics of common_behaviortypes.cpp, ctors 062d0bb0 .. 062d15b0).
enum EBehaviorTypeId
{
    BEHAVIOR_NULL = 0,
    BEHAVIOR_BUZZ = 1,
    BEHAVIOR_GOTO = 2,
    BEHAVIOR_TRAIL = 3,
    BEHAVIOR_FLEE = 4,
    BEHAVIOR_EVADE = 5,
    BEHAVIOR_IDLE = 6,
    BEHAVIOR_DOCK = 7,
    BEHAVIOR_LAUNCH = 8,
    BEHAVIOR_INSTANT_TRADE_LANE = 9,
    BEHAVIOR_FORMATION = 10,
    BEHAVIOR_LARGE_SHIP = 11,
    BEHAVIOR_CRUISE = 12,
    BEHAVIOR_STRAFE = 13,
    BEHAVIOR_GUIDE = 14,
    BEHAVIOR_FACE = 15,
    BEHAVIOR_LOOT = 16,
    BEHAVIOR_FOLLOW = 17,
    BEHAVIOR_DRASTIC_EVADE = 18,
    BEHAVIOR_FREE_FLIGHT = 19,
    BEHAVIOR_DELAY = 20,
};
