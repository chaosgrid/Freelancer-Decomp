// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "Common/Graphics/client_mesh_load.h" // FMeshHolder (Ship +0x108, FUN_00420d40)
#include "common_archetype.h"
#include "common_csys.h" // Transform (Ship +0x118)
#include "common_cequip.h" // ActionDB, INotify
#include "common_cobject.h"
#include "common_iface.h"
#include "common_misc.h" // DamageList
#include "common_tstring.h" // TString (Solar +0xe8)
#include "Common/Fonts/font_string.h" // FFontString (GameObject +0x48)

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>

struct FCliEquipFirePacket; // cli_projectile_fx.h
struct FEffectArch;         // vis_effect_arch.h
struct FParticleEffect;
struct FStarSphere;         // star_sphere.h (Solar +0xe4)
struct FNebulaEffect;       // cli_nebula.h (Gameobj +0x38)
struct ISoundSource;
namespace Archetype { struct ISound; }

// ===========================================================================
// The client's GameObject family -- retail E:\FL\Scratch\Source\Client\space\Gameobj.cpp.
//
// RTTI and constructors (sizes from the operator_new calls, vtables from the ctor stores):
//   GameObject  : IObjInspectImpl  ctor FUN_005392d0(kind)  0xac  IObjRW 005de7ec
//                                  IObjAffect 005de76c, CBase 005de764 -> FUN_00539480
//   Simple      : GameObject       ctor FUN_00547440(kind)  0xac  IObjRW 005e00a4
//                                  IObjAffect 005e0024, CBase 005e0018 -> FUN_00547470
//   EquippedObj : Simple, INotify  ctor FUN_00532bc0(kind)  0xe4  IObjRW 005de23c
//                                  IObjAffect 005de1bc, CBase 005de1b0 -> FUN_00532d00,
//                                  INotify at +0xac 005de1a8 -> FUN_00534730
//   Ship        : EquippedObj      ctor FUN_00543380        0x190 IObjRW 005dfd6c
//                                  IObjAffect 005dfcec, CBase 005dfce0 -> FUN_00543500,
//                                  INotify 005dfcd8 -> FUN_00545670
//   Solar       : EquippedObj      ctor FUN_005475b0        0x208 IObjRW 005e0304
//                                  IObjAffect 005e0284, CBase 005e0278 -> FUN_00547650,
//                                  INotify 005e0270 -> FUN_005478f0
// Projectile / Munition / Beam / Mine / CounterMeasure / Guided and Loot are declared after
// Ship (below); Debris lives in client_debris.h, the asteroids in cli_asteroid_med.h /
// cli_asteroid_dynamic.h.
//
// THE EXTENSION VTABLE IS DECLARED IN RETAIL SLOT ORDER. IObjInspect's 85 slots come first
// (common_iface.h), so the first virtual below is slot 85 (+0x154) and every following one is
// the next retail slot; the byte offset and the base body sit next to each. Neither
// IObjInspectImpl's nor these classes' destructors add a primary slot -- they override CBase's
// (the +0x0c sub-object), where retail keeps them. Do not insert a virtual between two of these
// without a retail slot for it. Slot names are not recovered (the exe exports none of them):
// `create`, `find` and `update` borrow Server.dll GameObject's names for the slots of the same
// shape and meaning (server_gameobject.h); every other slot is `SlotNNN` after its byte offset.
// ===========================================================================

enum class EClientGameobjKind : int
{
    None = 0,
    Ship = 1,
    Solar = 2,
    Beam = 3, // FUN_0052dde0
    Guided = 4, // FUN_0053d080
    Mine = 5, // FUN_0053ca40
    CounterMeasure = 6, // FUN_0053cd70
    Loot = 7, // FUN_0053e470
    MedAsteroid = 8, // FUN_00524cb0 (cli_asteroid_med.h)
    DynamicAsteroid = 9, // FUN_00526650 (cli_asteroid_dynamic.h)
    Debris = 10, // FUN_00530620 / FUN_005306e0
};

// FClientShipGameobj::JumpState, set by SetJumpState (FUN_005437d0). The update (FUN_00545050)
// moves 1 -> 5 as the jump effect's clock passes JumpOutTime, JumpOutTunnelTime,
// JumpInTunnelTime (with bJumpInReady) and JumpInTime; 5 -> 0 once the clock passes 1.0.
enum EJumpState : int
{
    kJumpStateNone = 0,
    kJumpStateJumpOut = 1,        // BeginJumpOut: the effect's StartJumpOut
    kJumpStateJumpOutTunnel = 2,  // the effect enters the tunnel; other ships are removed
    kJumpStateJumpInTunnel = 3,   // the destination system becomes the pending one
    kJumpStateJumpIn = 4,         // BeginJumpIn: placed at the exit frame
    kJumpStateArrived = 5,        // the arrival caption
};

// GameObject+0x28 -- the gameobj's effect list: a VC6 std::list whose node is 0x18 bytes
// (next, prev, then the 0x10-byte effect companion {slot0, ISoundSource*, particle, light}).
// FUN_004fcea0 inserts, FUN_004fcef0 removes one, FUN_004fd0d0 clears, FUN_004fd040 sweeps
// (cli_projectile_fx.h / space_scene_obj_draw.cpp).
//
// Two layers, as retail's EH tables show: FGameobjEffectListStorage is the bare list (ctor
// inlined in FUN_005392d0, dtor FUN_0053aa40); FGameobjEffectList is the member whose
// destructor is FUN_005393e0 -- FUN_004fd0d0(0) and then the storage's destructor. Retail
// unwinds a half-built GameObject through FUN_005393e0, so the list is a real sub-object.
struct FGameobjEffectListStorage
{
    explicit FGameobjEffectListStorage(unsigned char InAllocatorByte);
    ~FGameobjEffectListStorage();
    FGameobjEffectListStorage(FGameobjEffectListStorage const&) = delete;
    FGameobjEffectListStorage& operator=(FGameobjEffectListStorage const&) = delete;

    // FUN_0053aab0: unlink Where, release its companion (FUN_00502d90's sequence), free the
    // node; returns the next node.
    void* Erase(void* Where);

    unsigned char AllocatorByte; // +0x00  the ctor copies the kind's low byte here
    unsigned char Pad01[3];
    void* Head;                  // +0x04  sentinel node, operator_new(0x18), next/prev = self
    int Count;                   // +0x08
};

struct FGameobjEffectList : public FGameobjEffectListStorage
{
    explicit FGameobjEffectList(unsigned char InAllocatorByte)
        : FGameobjEffectListStorage(InAllocatorByte)
    {
    }
    // FUN_005393e0
    ~FGameobjEffectList();
};
static_assert(sizeof(FGameobjEffectList) == 0x0c, "GameObject effect list 0xc");

// EquippedObj+0xb4 -- the list of equipment-effect wrappers (VC6 std::list<ClientEquip*>, node
// 0xc {next, prev, wrapper}). Ctor inlined in FUN_00532bc0; dtor is the shared list destructor
// FUN_004a2d30 (EH unwind action of the EquippedObj ctor and dtor; FUN_00532d40 inlines it).
// It does not delete the wrappers (the owner's destructor deletes them first, FUN_00533080).
struct FEquipFxList
{
    explicit FEquipFxList(unsigned char InAllocatorByte);
    ~FEquipFxList();
    FEquipFxList(FEquipFxList const&) = delete;
    FEquipFxList& operator=(FEquipFxList const&) = delete;

    unsigned char AllocatorByte; // +0x00  the kind's low byte
    unsigned char Pad01[3];
    void* Head;                  // +0x04  sentinel, operator_new(0xc), next = prev = itself
    int Count;                   // +0x08
};
static_assert(sizeof(FEquipFxList) == 0x0c, "EquippedObj equip-FX list 0xc");

// EquippedObj+0xc0 -- the list of burning fuses (VC6 std::list<ClientFuse>, node 0x40 =
// FFuseBurnNode {next, prev, ClientFuse}). Ctor inlined in FUN_00532bc0; dtor FUN_005359c0
// (EH unwind action of the EquippedObj ctor and dtor; FUN_00532d40 inlines it).
struct FEquippedFuseList
{
    explicit FEquippedFuseList(unsigned char InAllocatorByte);
    ~FEquippedFuseList();
    FEquippedFuseList(FEquippedFuseList const&) = delete;
    FEquippedFuseList& operator=(FEquippedFuseList const&) = delete;

    unsigned char AllocatorByte; // +0x00  the kind's low byte
    unsigned char Pad01[3];
    void* Head;                  // +0x04  sentinel, operator_new(0x40), only the links written
    int Count;                   // +0x08
};
static_assert(sizeof(FEquippedFuseList) == 0x0c, "EquippedObj fuse list 0xc");

// GameObject+0x84 element: the destructor stops (vt+0x60, ISoundSource::Stop) and releases
// (vt+0x8) the interface at +4 of every 8-byte element.
struct FGameobjSound
{
    unsigned SourceId;    // +0x00  sound owner / source id (FUN_0053a4c0)
    ISoundSource* Source; // +0x04
};
static_assert(sizeof(FGameobjSound) == 8, "GameObject sound element 8 bytes");

// EquippedObj+0xcc map value (retail node +0x10 / +0x14): the target object id and its
// sub-object. A new entry is {0, SUBOBJ_ID_NONE}.
struct FEquipSubTarget
{
    FEquipSubTarget() : TargetId(0), TargetSubObj(SUBOBJ_ID_NONE) {}

    unsigned TargetId;            // +0x00 (node +0x10)
    unsigned short TargetSubObj;  // +0x04 (node +0x14)
};

class FClientGameobj : public IObjInspectImpl
{
public:
    // FUN_005392d0. Kind lands in ManagerKind and its low byte in the two allocator bytes
    // (+0x28, +0x80).
    explicit FClientGameobj(EClientGameobjKind Kind);
    // FUN_005394b0 (this = the CBase sub-object); deleting wrapper FUN_00539480.
    ~FClientGameobj() override;

    // IObjInspect slot 8 (+0x20) LAB_00524e70 -- shared by every client gameobj vtable: the
    // object narrowed to a CSimple ((class & 3) == 3) gives its Id (+0xb0); no object or another
    // class gives 0. Common's IObjInspectImpl::get_id has no such test.
    unsigned int const get_id() const override;

    // ---- the extension vtable, retail slot order --------------------------------------
    // 85 +0x154 LAB_00524e20: bit 2 of Flags (set by destroy(1)).
    virtual bool IsExploding() const;
    // 86 +0x158 LAB_00524e30: bit 0 of Flags. FUN_005424e0 (the ObjDB reap) removes and deletes
    // every gameobj that answers true.
    virtual bool IsDead() const;
    // 87 +0x15c FUN_00539c10: bit 0 of Flags = the argument; then, when there is an object,
    // CObject::enable_collisions_r(false), whatever the argument was.
    virtual void MarkForRemoval(bool bValue);
    // 88 +0x160 LAB_00524e40: bit 1 of Flags.
    virtual bool Slot160() const;
    // 89 +0x164 LAB_00539c40: bit 1 of Flags = the argument.
    virtual void Slot164(bool bValue);
    // 90 +0x168 LAB_005396f0: CObject::Alloc(1) into Object, Arch->create_instance, engine
    // UpdateInstance, CBase::advise(true). Simple, EquippedObj and the projectile branch make it
    // pure again. No caller reads a result.
    virtual void create(Archetype::Root* Arch);
    // 91 +0x16c pure in GameObject: attach to the CObject with this id (Ship FUN_005465d0,
    // Solar FUN_00547760: CObject::Find(Id, Class), advise(true), connect).
    virtual bool find(unsigned const& Id) = 0;
    // 92 +0x170 FUN_00539740: resolve the archetype's explosion into ExplosionArch (+ preload),
    // FDUMP a warning when the archetype names one that does not exist, then join the watch list
    // when get_type & 0x107ffdff and bWatchListed.
    virtual void PostCreateInit();
    // 93 +0x174 FUN_00539c60: Actions.update(Dt), then UpdateNebula(Dt).
    virtual void update(float Dt);
    // 94 +0x178 FUN_00539810: the object's physical radius about Center.
    virtual float GetPhysicalRadius(Vector& Center);
    // 95 +0x17c FUN_00539820: the LOD pick; returns its first argument. A non-zero low word in
    // *CachedLod is copied straight to *OutLod, otherwise the pick runs. The pick's port is the
    // free function GameobjPickLod in space_scene_obj_draw.cpp.
    virtual unsigned* PickLod(unsigned* OutLod, void* Camera, unsigned const* CachedLod);
    // 96 +0x180 LAB_00401e90: empty (EquippedObj overrides).
    virtual void PreSubmitDraw();
    // 97 +0x184 FUN_00539b40: per-object lighting (FUN_005554d0). Body: the free function
    // GameobjUpdateDrawLightingRetail in space_scene_obj_draw.cpp.
    virtual void UpdateDrawLighting(void* Camera);
    // 98 +0x188 FUN_00539980: draw submit -- PreSubmitDraw, PickLod, the LOD gate (a failed gate
    // writes the LOD to *OutLod and returns), the effect-list sweep, UpdateDrawLighting, the
    // player flags, get_index, the envmap arm, IRenderer2::Slot7(Camera, Inst, 1.0f, Flags,
    // Xform, Extra). A non-null Xform ors 0x120 into the flags; Extra is the IFLAppearance
    // Slot8 out-param when it was null. Body: GameobjSubmitDrawRetail in
    // space_scene_obj_draw.cpp; the draw drain calls this slot through the node LAB_0051f7a0.
    virtual unsigned* SubmitDraw(unsigned* OutLod, void* Camera, unsigned CachedLod, Transform const* Xform,
                              void* Extra);
    // 99 +0x18c LAB_00524e50: *OutB = NebulaFogFactor (as bits); return CurrentNebula.
    virtual unsigned GetNebula(unsigned* OutB);
    // 100 +0x190 LAB_00524e60: CurrentNebula = NebulaFogFactor = 0, NebulaUpdateTimer = 10.0f.
    virtual void ResetNebula();
    // 101 +0x194 FUN_00539c90: the collision pair -- Other->OnSurfaceCollisionHit(A, D, this, B),
    // then this->ReportCollisionToServer(A, B, Other, D).
    virtual void OnCollision(void const* A, void const* B, FClientGameobj* Other, void const* D);
    // 102 +0x198 LAB_0053e4a0: empty. Simple FUN_005474a0 applies the root entry.
    virtual void ApplyDamageList(DamageList const& Damage);
    // 103 +0x19c FUN_0053a820: DestroyType 0 -> MarkForRemoval(true); 1 -> Flags |= 4 and
    // Explode(nullptr); anything else FDUMPs "Invalid DestroyType!".
    virtual void Destroy(int DestroyType);
    // 104 +0x1a0 FUN_00539cd0: surface-hit effect (PickDistanceRecord 0x00407450,
    // FUN_00539f90). Contact holds two Vectors: the hit point (+0x00) and the surface normal
    // (+0x0c); B and D are not read.
    virtual void OnSurfaceCollisionHit(void const* Contact, void const* B, FClientGameobj* Other,
                         void const* D);
    // 105 +0x1a4 FUN_00539d80: munition-hit effect (0x00408a70, FUN_0053a230). The munition
    // gameobj is the THIRD argument (Munition's slot 101 pushes `this` last).
    virtual void OnMunitionHit(void const* A, void const* B, FClientGameobj* Munition);
    // 106 +0x1a8 LAB_0053f780: empty. The arguments are EquippedObj's (FUN_005356c0): a gun
    // hardpoint's sub-object id, the target object id and its sub-object.
    virtual void SetSubTarget(unsigned short SubObj, unsigned TargetId, unsigned short TargetSubObj);
    // 107 +0x1ac FUN_00539e70: multiplayer collision report through the server interface
    // DAT_0067ecd0 vt+0xdc.
    virtual void ReportCollisionToServer(void const* A, void const* B, FClientGameobj* Other, void const* D);
    // 108 +0x1b0 FUN_0053a880: explode at Pos (null = get_position()) through Action_Explode
    // (FUN_005361e0) + Actions.add, or MarkForRemoval(true) when there is no explosion.
    virtual void Explode(Vector const* Pos);
    // 109 +0x1b4 FUN_0053a4c0: the hit sound. SourceId keys the per-source voice pool at +0x80;
    // SoundArch is an EffectArch's +0x18 (vt+0x30 tested first). Caller: FUN_00539f90.
    virtual void PlayHitSound(unsigned SourceId, void* SoundArch, Vector const* Pos);
    // 110 +0x1b8 LAB_0053e4a0: empty.
    virtual void UpdateNebula(float Dt);

    // ---- non-virtual members --------------------------------------------------------
    // FUN_0053a960: the cached attitude towards the player, refreshed through FUN_0045a490
    // every eighth call (AttitudeCountdown).
    int GetCachedAttitude();
    // FUN_0053a9c0 / FUN_0053a9d0: read / write +0xa4.
    unsigned char GetFieldA4() const { return bFactionDiscovered; }
    void SetFieldA4(unsigned char Value) { bFactionDiscovered = Value; }
    // FUN_0053a9e0: the address of the font string at +0x48.
    FFontString* GetFontString48() { return &HudLabel; }
    // FUN_0053a9f0: Flags bit 3, but only while +0x7c still equals the surface width
    // DAT_00610850; false otherwise.
    bool GetFlag3IfSurfaceWidthUnchanged() const;
    // FUN_0053aa10: Flags bit 3 = the argument; when it ends up set, +0x7c takes the current
    // surface width DAT_00610850.
    void SetFlag3(bool bValue);

    // FUN_00539f90: the surface-hit particle -- a new ParticleEffect for EffectArch->VisEffect
    // in one of the three HitEffects slots, attached at Pos / Normal in object space -- then
    // PlayHitSound(SourceId, EffectArch->SndEffect, Pos). Skipped entirely for the player's
    // ship while SURFACE_HIT_EFFECTS (option 0x38) is off. The fifth argument is never read:
    // slots 104/105 pass 0, the shield wrapper's hit slots pass the address of a zeroed local
    // effect companion, which therefore stays empty.
    void SpawnSurfaceHitEffect(unsigned SourceId, FEffectArch* EffectArch, Vector const* Pos,
                               Vector const* Normal, void* CompanionOut);
    // FUN_0053a230: the splash-hit one-shot -- EffectArch through FUN_00503070 at Pos with the
    // direction mirrored off Normal. Skipped for the player's ship while SPLASH_HIT_EFFECTS
    // (option 0x39) is off, and when the effect type fails the perf test FUN_004fd570.
    void SpawnSplashHitEffect(FEffectArch* EffectArch, Vector const* Pos, Vector const* Normal,
                              Vector const* Velocity, void* CompanionOut);

    CObject* GetObject() const { return Object; }

    // FUN_005397f0 -- the object narrowed to a CSimple ((class & 3) == 3, else null, used with
    // no test), then CSimple::SetOwnerPlayer(PlayerId). Callers: the IClient system-switch-out
    // and land handlers.
    void SetOwnerPlayer(unsigned PlayerId);

    // IObjAffect -- GameObject's own table, .rdata:005de76c. Every entry is a retail error stub
    // with the same return pattern as Server.dll GameObject's; see client_gameobj.cpp. Simple
    // and Solar keep these entries, EquippedObj overrides six and Ship nineteen.
    int set_throttle(float Throttle) override;
    int set_axis_throttle(Vector const& AxisThrottle) override;
    int set_nudge_vec(Vector const& NudgeVec) override;
    int set_strafe_dir(StrafeDir Dir) override;
    int set_formation_leader(IObjRW* Leader) override;
    int set_formation_offset(Vector const& Offset) override;
    int add_formation_follower(IObjRW* Follower) override;
    int remove_formation_follower(IObjRW* Follower) override;
    int get_formation_follower_count() override;
    int set_follow_leader(IObjRW* Leader) override;
    int set_follow_offset(Vector const& Offset) override;
    int add_follow_follower(IObjRW* Follower) override;
    int remove_follow_follower(IObjRW* Follower) override;
    int fire_weapons(unsigned short const* EquipIds, unsigned Count, Vector const* TargetPos,
                int SourceClient) override;
    int jettison_cargo(unsigned short EquipId, int Count, int SourceClient) override;
    int tractor_all(unsigned short const* EquipId, std::vector<IObjRW*> const& Targets,
                int SourceClient) override;
    int tractor_single(unsigned short const* EquipId, IObjRW* Target, int SourceClient) override;
    int set_gun_target(unsigned short const* EquipId, IObjRW* Target, unsigned short SubTarget,
                int SourceClient) override;
    int set_target(IObjRW* Target, unsigned short SubTarget, int SourceClient) override;
    int toggle_item(unsigned short const* EquipId, bool bActivate, int SourceClient) override;
    int toggle_cruise(bool bEngage, bool bArg2, int SourceClient) override;
    int toggle_thrusters(bool bActivate, int SourceClient) override;
    int use_item(unsigned short EquipId, unsigned Count, int SourceClient) override;
    int request_event(int RequestType, unsigned const* TargetId, unsigned const* SecondId,
                int bFlag, RequestCB* Callback) override;
    int request_cancel(int RequestType, unsigned const* TargetId, int Arg3) override;
    int go_tradelane(IObjInspect const* EntranceRing, IObjInspect const* ExitRing,
                     bool bFastExit, float Speed) override;
    int stop_tradelane() override;
    int slot_6c(int ArgA, int ArgB) override;
    int launch_complete(unsigned BaseObjId) override;
    int jump_in_complete(unsigned SystemId) override;

    // ---- data (IObjInspectImpl ends at +0x14) ---------------------------------------
    FClientGameobj* ListNext;          // +0x14  ObjDB bucket link (FUN_00542350 / 00542390)
    FClientGameobj* ListPrev;          // +0x18
    EClientGameobjKind ManagerKind;    // +0x1c  ObjDB bucket (1 ship, 2 solar, 3 beam, ...)
    ActionDB Actions;                  // +0x20  update(Dt) by slot 93, add by slot 108
    FGameobjEffectList EffectList;     // +0x28
    float NebulaUpdateTimer;                  // +0x34  ctor 0; slot 100 sets 10.0f; Ship ctor rand()/32768
    FNebulaEffect* CurrentNebula;      // +0x38  the nebula at the position (FUN_005553b0)
    float NebulaFogFactor;             // +0x3c  its fog factor; both feed FUN_005554d0
    Archetype::Explosion* ExplosionArch; // +0x40 slot 92 resolves; the dtor releases
    unsigned char Flags;               // +0x44  bit0 slot 86/87, bit1 slot 88/89, bit2 slot 85
    unsigned char Pad45[3];
    FFontString HudLabel;            // +0x48  FUN_004144b0; FUN_0053a9e0 returns its address
    int HudLabelSurfaceWidth;                    // +0x7c  surface width DAT_00610850 (FUN_0053aa10 / 0053a9f0)
    unsigned char SoundAllocatorByte;  // +0x80  the ctor copies the kind's low byte here
    unsigned char Pad81[3];
    FGameobjSound* SoundsBegin;        // +0x84
    FGameobjSound* SoundsEnd;          // +0x88
    FGameobjSound* SoundsCap;          // +0x8c
    FParticleEffect* HitEffects[3];    // +0x90  FUN_00539f90 fills (DAT_00613e60 = 3)
    int CachedAttitude;                // +0x9c  FUN_0053a960
    int AttitudeCountdown;             // +0xa0  ctor -1
    unsigned char bFactionDiscovered;          // +0xa4  FUN_0053a9c0 get / FUN_0053a9d0 set
    unsigned char Pada5[3];
    // +0xa8 -- Port layout: in retail sizeof(GameObject) is 0xa8 and +0xa8 is the first derived
    // field of Simple (bWatchListed), MedAsteroid (bInstantKill), DynamicAsteroid (Owner) and
    // Debris (DeathMethod). PostCreateInit (FUN_00539740) tests type & 0x107ffdff before reading
    // it, which excludes MedAsteroid (0x200), DynamicAsteroid (0x20000000) and Debris
    // (0x80000000), so `this` is a Simple there.
    unsigned char bWatchListed;        // +0xa8
    unsigned char Pada9[3];
};

static_assert(offsetof(FClientGameobj, ListNext) == 0x14, "Gameobj ListNext +0x14");
static_assert(offsetof(FClientGameobj, ListPrev) == 0x18, "Gameobj ListPrev +0x18");
static_assert(offsetof(FClientGameobj, ManagerKind) == 0x1c, "Gameobj ManagerKind +0x1c");
static_assert(offsetof(FClientGameobj, Actions) == 0x20, "Gameobj ActionDB +0x20");
static_assert(offsetof(FClientGameobj, EffectList) == 0x28, "Gameobj effect list +0x28");
static_assert(offsetof(FClientGameobj, NebulaUpdateTimer) == 0x34, "Gameobj +0x34");
static_assert(offsetof(FClientGameobj, CurrentNebula) == 0x38, "Gameobj +0x38");
static_assert(offsetof(FClientGameobj, NebulaFogFactor) == 0x3c, "Gameobj +0x3c");
static_assert(offsetof(FClientGameobj, ExplosionArch) == 0x40, "Gameobj explosion +0x40");
static_assert(offsetof(FClientGameobj, Flags) == 0x44, "Gameobj flags +0x44");
static_assert(offsetof(FClientGameobj, HudLabel) == 0x48, "Gameobj font string +0x48");
static_assert(offsetof(FClientGameobj, HudLabelSurfaceWidth) == 0x7c, "Gameobj +0x7c");
static_assert(offsetof(FClientGameobj, SoundsBegin) == 0x84, "Gameobj sounds +0x84");
static_assert(offsetof(FClientGameobj, HitEffects) == 0x90, "Gameobj hit effects +0x90");
static_assert(offsetof(FClientGameobj, CachedAttitude) == 0x9c, "Gameobj attitude +0x9c");
static_assert(offsetof(FClientGameobj, AttitudeCountdown) == 0xa0, "Gameobj countdown +0xa0");
static_assert(offsetof(FClientGameobj, bFactionDiscovered) == 0xa4, "Gameobj +0xa4");
static_assert(offsetof(FClientGameobj, bWatchListed) == 0xa8, "Gameobj +0xa8");
static_assert(sizeof(FClientGameobj) == 0xac, "GameObject sizeof 0xac");

// Simple : GameObject -- FUN_00547440. Adds no data; its ctor zeroes bWatchListed.
class FClientSimpleGameobj : public FClientGameobj
{
public:
    explicit FClientSimpleGameobj(EClientGameobjKind Kind);
    // FUN_00547470 is the deleting wrapper; the body is GameObject's (thunk to FUN_005394b0).
    ~FClientSimpleGameobj() override;

    // 90 +0x168: pure again in Simple.
    void create(Archetype::Root* Arch) override = 0;
    // 102 +0x198 FUN_005474a0: multiplayer only -- the root entry's hit points go to
    // CSimple::set_hit_pts.
    void ApplyDamageList(DamageList const& Damage) override;
    // 103 +0x19c FUN_00547500: CSimple::set_hit_pts(0), then GameObject's slot 103.
    void Destroy(int DestroyType) override;

    // 111 +0x1bc LAB_00547530: the CSimple's TypeBits (+0xe0).
    virtual unsigned GetTypeBits() const;

    // FUN_00547550: bWatchListed = (bWatch == 1), then join or leave the watch list.
    // The first argument is not read.
    void SetWatchListed(int Unused, int bWatch);
};
static_assert(sizeof(FClientSimpleGameobj) == 0xac, "Simple sizeof 0xac");

// EquippedObj : Simple, INotify -- FUN_00532bc0. INotify's vptr lands at +0xac (right after
// Simple), as in retail.
class FClientEquippedObj : public FClientSimpleGameobj, public INotify
{
public:
    explicit FClientEquippedObj(EClientGameobjKind Kind);
    // FUN_00532d40; deleting wrapper FUN_00532d00.
    ~FClientEquippedObj() override;

    // INotify -- FUN_00534730.
    void notify(Event Code, void* Param) override;

    // IObjInspect slot 14 (+0x38) LAB_00534860 (shared by EquippedObj, Ship and Solar): a tail
    // call to IsDeathFuseBurning (slot 114).
    bool is_dying() const override;
    // IObjInspect slot 72 (+0x120) LAB_00535650 (same three vtables): Common's scan_cargo,
    // then -- for the player's ship -- IServer::SPScanCargo(this id, the scanned object's id,
    // the player's connection).
    int scan_cargo(IObjRW* Scanned, EquipDescVector& OutCargo) const override;

    // 90 +0x168: pure.
    void create(Archetype::Root* Arch) override = 0;
    // 92 +0x170 FUN_00533e10.
    void PostCreateInit() override;
    // 93 +0x174 FUN_00534010.
    void update(float Dt) override;
    // 95 +0x17c FUN_00533710.
    unsigned* PickLod(unsigned* OutLod, void* Camera, unsigned const* CachedLod) override;
    // 96 +0x180 LAB_0051b480.
    void PreSubmitDraw() override;
    // 98 +0x188 FUN_00533780.
    unsigned* SubmitDraw(unsigned* OutLod, void* Camera, unsigned CachedLod, Transform const* Xform,
                      void* Extra) override;
    // 102 +0x198 FUN_005339e0.
    void ApplyDamageList(DamageList const& Damage) override;
    // 104 +0x1a0 FUN_005340b0.
    void OnSurfaceCollisionHit(void const* Contact, void const* B, FClientGameobj* Other,
                 void const* D) override;
    // 105 +0x1a4 FUN_00534190.
    void OnMunitionHit(void const* A, void const* B, FClientGameobj* Munition) override;
    // 106 +0x1a8 FUN_005356c0: SubTargets[SubObj] = {TargetId, TargetSubObj}.
    void SetSubTarget(unsigned short SubObj, unsigned TargetId, unsigned short TargetSubObj) override;

    // FUN_005357f0: re-aim the gun at SubObj on the target SubTargets holds for it (inserting
    // the {0, SUBOBJ_ID_NONE} default when there is none), through IObjAffect slot 17 when the
    // gun's -- or, for SUBOBJ_ID_NONE, this object's -- target differs. Caller: the multiplayer
    // arm of slot_34.
    void ApplySubTarget(unsigned short SubObj);
    // 108 +0x1b0 FUN_00533be0.
    void Explode(Vector const* Pos) override;

    // 112 +0x1c0 FUN_00534240: ignite the fuse FuseId on this object -- the first burning fuse
    // of that name on the +0xc4 list gets Fuse::IgniteAt(SourceId, SubObjId, AtT); otherwise a
    // copy of the ClientFuseDB prototype is appended, bound to this object, randomised, given
    // Lifetime unless it is -1, and ignited. False only when the fuse is not in the database
    // (FDUMP). Callers: the equip-FX rebuild FUN_00533e10, BURNFUSE FUN_0053e090, the ship
    // override FUN_00545a90. Body: EquippedObjIgniteFuse (client_equip_fx.cpp).
    virtual bool LightFuse(unsigned SourceId, ID_String const* FuseId, unsigned short SubObjId,
                         float AtT, float Lifetime);
    // 113 +0x1c4 FUN_005343d0: the first fuse named FuseId on the +0xc4 list gets
    // Fuse::UnBurn(SubObjId, AtT); true when one did.
    virtual bool UnLightFuse(ID_String const* FuseId, unsigned short SubObjId, float AtT);
    // 114 +0x1c8 FUN_00534420: true when a death fuse (Fuse +0xc) on the +0xc4 list is not yet
    // spent (Fuse::IsSpent).
    virtual bool IsDeathFuseBurning();
    // 115 +0x1cc FUN_00534460: the equipment message -- 0x18 fire, 0x19 activate.
    // Port: EquippedObjHandleEquipMsg (cli_projectile_fx.cpp).
    virtual unsigned char HandleEquipMsg(FCliEquipFirePacket const* Packet);

    // FUN_00533d10: a part that came off becomes a Debris gameobj (its [Debris] record by
    // DebrisType, the default with a warning when there is none), the part's effects move to it
    // (FUN_004fd230), and a named Explosion (null = none) starts on it at this Gameobj's
    // position. Callers: notify 5 and 7.
    void SpawnPartDebris(CObject* PartObject, ID_String const* DebrisType,
                         ID_String const* Explosion);

    // IObjAffect overrides (.rdata:005de1bc): slots 13, 15, 16, 17, 19, 22.
    int fire_weapons(unsigned short const* EquipIds, unsigned Count, Vector const* TargetPos,
                int SourceClient) override;                                     // 00534870
    int tractor_all(unsigned short const* EquipId, std::vector<IObjRW*> const& Targets,
                int SourceClient) override;                                     // 00535010
    int tractor_single(unsigned short const* EquipId, IObjRW* Target, int SourceClient) override; // 00534e60
    int set_gun_target(unsigned short const* EquipId, IObjRW* Target, unsigned short SubTarget,
                int SourceClient) override;                                     // 005351d0
    int toggle_item(unsigned short const* EquipId, bool bActivate, int SourceClient) override; // 00535330
    int use_item(unsigned short EquipId, unsigned Count, int SourceClient) override; // 00535470

    // ---- data -------------------------------------------------------------------------
    // (+0xac is INotify's vptr.)
    unsigned char EquipFlags;          // +0xb0  the ctor clears bit 0 only
    unsigned char Padb1[3];
    // Equipment-effect wrappers (client_equip_fx.cpp owns them).
    FEquipFxList EquipFx;              // +0xb4 (allocator byte), +0xb8 head, +0xbc count
    // Burning fuses (FFuseBurnNode, client_fuse.h).
    FEquippedFuseList Fuses;           // +0xc0 (allocator byte), +0xc4 head, +0xc8 count
    // +0xcc .. +0xdf: retail's VC6 std::map<unsigned short, FEquipSubTarget> (nodes 0x1c, key
    // at +0x0c, value at +0x10) -- the target each gun hardpoint was last told to track,
    // written by slot 106 and applied by FUN_005357f0. The port keeps a std::map of its own in
    // the same 0x14 bytes (no retail ABI).
    std::map<unsigned short, FEquipSubTarget> SubTargets; // +0xcc
    unsigned char PadSubTargets[0x14 - sizeof(std::map<unsigned short, FEquipSubTarget>)];
    unsigned char bCloakOverrideActive;          // +0xe0
    unsigned char Pade1[3];
};
static_assert(offsetof(FClientEquippedObj, EquipFlags) == 0xb0, "EquippedObj +0xb0");
static_assert(offsetof(FClientEquippedObj, EquipFx) == 0xb4, "EquippedObj equip-FX list +0xb4");
static_assert(offsetof(FClientEquippedObj, Fuses) == 0xc0, "EquippedObj fuse list +0xc0");
static_assert(offsetof(FClientEquippedObj, SubTargets) == 0xcc, "EquippedObj sub-target map +0xcc");
static_assert(offsetof(FClientEquippedObj, bCloakOverrideActive) == 0xe0, "EquippedObj +0xe0");
static_assert(sizeof(FClientEquippedObj) == 0xe4, "EquippedObj sizeof 0xe4");

class FClientSolarGameobj : public FClientEquippedObj
{
public:
    // FUN_005475b0.
    FClientSolarGameobj();
    // FUN_00547680; deleting wrapper FUN_00547650.
    ~FClientSolarGameobj() override;

    // INotify -- FUN_005478f0.
    void notify(Event Code, void* Param) override;

    // 90 +0x168 FUN_005477d0: FUN_00539bc0(0x303, Arch), open, CEqObj::connect(INotify*).
    void create(Archetype::Root* Arch) override;
    // 91 +0x16c FUN_00547760.
    bool find(unsigned const& Id) override;
    // 92 +0x170 FUN_00547850.
    void PostCreateInit() override;
    // 93 +0x174 FUN_00547880.
    void update(float Dt) override;
    // 94 +0x178 FUN_00548110.
    float GetPhysicalRadius(Vector& Center) override;
    // 98 +0x188 FUN_00548220.
    unsigned* SubmitDraw(unsigned* OutLod, void* Camera, unsigned CachedLod, Transform const* Xform,
                      void* Extra) override;
    // 104 +0x1a0 FUN_00548520.
    void OnSurfaceCollisionHit(void const* Contact, void const* B, FClientGameobj* Other,
                 void const* D) override;
    // 105 +0x1a4 FUN_00548570.
    void OnMunitionHit(void const* A, void const* B, FClientGameobj* Munition) override;
    // 108 +0x1b0 FUN_005485b0.
    void Explode(Vector const* Pos) override;

    // FUN_005485c0 -- the client half of an [Object] section, read after the spawn FUN_00548a20
    // rewound the reader: ring, burn_color, dock_script, voice, jump_effect, star; then the
    // nebula at the position.
    void ReadIni(INI_Reader& Reader);

    // ---- data -------------------------------------------------------------------------
    // +0xe4 -- the star (StarEffects::Star, FStarSphere, vtable 005db658) a sun solar gets from
    // its "star" key (FUN_0050c630); the update ticks it through its slot 2.
    // space_scene_obj_draw.cpp tests it.
    FStarSphere* StarSphere;           // +0xe4  ctor 0
    // +0xe8 -- "dock_script". The ctor zeroes the length and the first char only.
    TString<0x104> DockScript;         // +0xe8 .. +0x1ef
    unsigned char bTradeLaneIdle;         // +0x1f0 ctor 1
    unsigned char Pad1f1[3];
    float BurnColor[3];                // +0x1f4 ctor 1.0f x3; "burn_color" / 255 (ReadIni)
    ID_String JumpEffectId;             // +0x200 ctor ID_String::clear; "jump_effect" CreateID
    void* JumpGateEffect;                 // +0x204 ctor 0; the dtor runs FUN_00502e40 and frees it
};
static_assert(offsetof(FClientSolarGameobj, DockScript) == 0xe8, "Solar +0xe8 dock_script");
static_assert(offsetof(FClientSolarGameobj, BurnColor) == 0x1f4, "Solar +0x1f4 burn_color");
static_assert(offsetof(FClientSolarGameobj, bTradeLaneIdle) == 0x1f0, "Solar +0x1f0");
static_assert(offsetof(FClientSolarGameobj, JumpEffectId) == 0x200, "Solar +0x200");
static_assert(sizeof(FClientSolarGameobj) == 0x208, "Solar sizeof 0x208");

// Bits of the first byte of FClientShipGameobj::TradeLaneFlags (+0x184), kept by tradelane_fx.cpp.
enum ETradeLaneShipFlag
{
    kTradeLaneFlagSideKey = 0x1,   // the ring node's side key; picks the side of the ring's lane hardpoint
    kTradeLaneFlagRingSide = 0x2,  // the ship's side of the exit ring (get_ring_side)
    kTradeLaneFlagTravelFx = 0x20, // FUN_00514750 started the travel effect (once)
    kTradeLaneFlagTunnelFx = 0x40, // the player's tunnel companion (+0x174) is running
    kTradeLaneFlagRiding = 0x80    // the ship is riding the lane
};

// FClientShipGameobj::ShipFlags (+0x18c).
enum EShipFlag
{
    kShipFlagHullHit = 0x1,      // set by OnMunitionHit (FUN_00545ed0) for a non-shield hit; the first-person camera shakes and clears it
    kShipFlagShieldHit = 0x2,    // set by OnMunitionHit for a shield hit; the camera shakes and clears it
    kShipFlagTradeRequest = 0x4, // client_iclient.cpp FUN_005a61c0: a pending trade request; HudReputationColor shows the ship purple
    kShipFlagLanding = 0x8,      // set by RequestLand (FUN_00543b40); the atmosphere burn effect leaves the list
};

class FClientShipGameobj : public FClientEquippedObj
{
public:
    // FUN_00543380.
    FClientShipGameobj();
    // FUN_005444a0; deleting wrapper FUN_00543500.
    ~FClientShipGameobj() override;

    // INotify -- FUN_00545670.
    void notify(Event Code, void* Param) override;

    // 90 +0x168 LAB_00546550: FUN_00539bc0(0x503, Arch), open, CEqObj::connect(INotify*).
    void create(Archetype::Root* Arch) override;
    // 91 +0x16c FUN_005465d0.
    bool find(unsigned const& Id) override;
    // 92 +0x170 FUN_005447b0.
    void PostCreateInit() override;
    // 93 +0x174 FUN_00545050.
    void update(float Dt) override;
    // 97 +0x184 FUN_00546480.
    void UpdateDrawLighting(void* Camera) override;
    // 98 +0x188 FUN_00546050.
    unsigned* SubmitDraw(unsigned* OutLod, void* Camera, unsigned CachedLod, Transform const* Xform,
                      void* Extra) override;
    // 102 +0x198 FUN_00545f50.
    void ApplyDamageList(DamageList const& Damage) override;
    // 104 +0x1a0 FUN_00545e90.
    void OnSurfaceCollisionHit(void const* Contact, void const* B, FClientGameobj* Other,
                 void const* D) override;
    // 105 +0x1a4 FUN_00545ed0.
    void OnMunitionHit(void const* A, void const* B, FClientGameobj* Munition) override;
    // 108 +0x1b0 FUN_00545d80.
    void Explode(Vector const* Pos) override;
    // 110 +0x1b8 FUN_005455b0.
    void UpdateNebula(float Dt) override;
    // 112 +0x1c0 FUN_00545a90: EquippedObj's ignite and, when it lit DEATH_COMM_FUSE, the
    // formation / follow followers' behaviours are told (event 7 with this ship's id) and the
    // player drops this ship as its target.
    bool LightFuse(unsigned SourceId, ID_String const* FuseId, unsigned short SubObjId, float AtT,
                 float Lifetime) override;
    // 115 +0x1cc FUN_00544d70. Port: CliShipHandleEquipUpdate (cli_projectile_fx.cpp).
    unsigned char HandleEquipMsg(FCliEquipFirePacket const* Packet) override;
    // 116 +0x1d0 LAB_00545cc0: empty.
    virtual void PlayFireFailedSound();

    // IObjAffect overrides (.rdata:005dfcec).
    int set_throttle(float Throttle) override;                               // 00546640
    int set_axis_throttle(Vector const& AxisThrottle) override;              // 00546680
    int set_nudge_vec(Vector const& NudgeVec) override;                      // 005466c0
    int set_strafe_dir(StrafeDir Dir) override;                              // 00546700
    int set_formation_leader(IObjRW* Leader) override;                       // 00547140
    int set_formation_offset(Vector const& Offset) override;                 // 005470f0
    int add_formation_follower(IObjRW* Follower) override;                   // 00547190
    int remove_formation_follower(IObjRW* Follower) override;                // 005471c0
    int jettison_cargo(unsigned short EquipId, int Count, int SourceClient) override; // 005467e0
    int set_target(IObjRW* Target, unsigned short SubTarget, int SourceClient) override; // 005468e0
    int toggle_cruise(bool bEngage, bool bArg2, int SourceClient) override;        // 00546a10
    int toggle_thrusters(bool bActivate, int SourceClient) override;                  // 00546b50
    int request_event(int RequestType, unsigned const* TargetId, unsigned const* SecondId,
                int bFlag, RequestCB* Callback) override;                   // 00546df0
    int request_cancel(int RequestType, unsigned const* TargetId, int Arg3) override; // 00546e40
    int go_tradelane(IObjInspect const* EntranceRing, IObjInspect const* ExitRing,
                     bool bFastExit, float Speed) override;                  // 00546e90
    int stop_tradelane() override;                                           // 00546f80
    int slot_6c(int ArgA, int ArgB) override;                                // 00547250
    int launch_complete(unsigned BaseObjId) override;                        // 005471f0
    int jump_in_complete(unsigned SystemId) override;                         // 00547220

    // FUN_00543d50 -- the client side of a launch, called by the IClient launch handler
    // (LAB_0053d7e0) with the dock gameobj found in bucket 0 and the launch-point index. Only the player's character launches (get_id vs DAT_00673378):
    // CShip::launch, seven seconds of suppressed player collisions (FUN_005544d0), and -- when
    // this is the watched player ship -- the docking camera placed on the dock's
    // "HPLaunchCam<A+Index>" hardpoint, else "HPDockCam<A+Index>", else the player camera.
    void LaunchFromDock(FClientGameobj* DockGo, int LaunchIndex);

    // FUN_00543b40 -- the client side of a landing, called by the IClient land handler
    // FUN_0053d720 with the dock gameobj (null when it is not in the ObjDB) and the base id. Flag bit 3 of +0x18c; with a dock, an Action_Land
    // (client_land_action.h), the axis throttle zeroed and, for the watched player ship, the
    // docking camera on the dock's "HPDockCam" 'A' hardpoint and its three-second fade. Without
    // a dock the player's ship asks for the base straight away and cuts the screen to black;
    // any other ship goes through slot 87 with true.
    void RequestLand(FClientGameobj* DockGo, unsigned const* BaseId);

    // FUN_005437d0 -- the jump state machine: 0 idle, 1 jump out (the
    // Action_JumpOut swing), 2 into the tunnel, 3 system switch requested, 4 jump in, 5 done.
    // Each state runs its entry work (and the JumpShipEffect's hook); any other value warns
    // (ship.cpp line 0x272). The state lands in +0x158 and the effect's clock (+0x70) restarts.
    void SetJumpState(int State);
    // FUN_00543530 -- the jump out through Gate (the gate / hole solar gameobj): a fresh
    // JumpShipEffect for it at +0x188, state 1, and an Action_JumpOut over the effect's
    // JumpOutTime. Caller: the IClient system-switch-out handler.
    void BeginJumpOut(FClientGameobj* Gate);
    // FUN_00543630 -- the jump in at Pos / Orient: +0x15c = 1; the player's ship stores them in
    // its effect and raises the camera cut, any other ship gets a fresh no-gate JumpShipEffect
    // with them and goes to state 4. Caller: the IClient system-switch-in handler.
    void BeginJumpIn(Vector const* Pos, Matrix const* Orient);
    // FUN_00543750 (`this` is never read) -- Gate narrowed to a CSolar ((class & 0x303) ==
    // 0x303), its DestSystem (+0x1b4) stored as the pending jump system (FUN_00548ed0, flag 0).
    // Caller: the system-switch-out handler for a gate type with neither bit 0x40 nor 0x800.
    void RequestSystemSwitch(FClientGameobj* Gate);
    // FUN_005437a0 -- the object narrowed to a CShip ((class & 0x503) == 0x503), then
    // CShip::airlock_in(Pos, Orient). Caller: the system-switch-in handler.
    void AirlockIn(Vector const& Pos, Matrix const& Orient);
    // FUN_00544380 -- the pilot mesh: with a non-empty PilotMesh and an
    // "HpPilot" hardpoint on the ship, the simple archetype PilotMesh names goes to +0x104
    // (load, companion Load(0), create_instance -> +0x10c), and +0x118 becomes {HpPilot's
    // orientation, HpPilot's position minus the pilot mesh's "HpConnect" position}. Caller:
    // slot 92 (FUN_005447b0) with the ship companion's pilot_mesh (+0x64).
    void AttachPilotMesh(ID_String const* PilotMesh);
    // FUN_005449f0 (the connection id is never read) -- the next
    // target: every object in the ship's scanner list (CEquipManager::FindFirst(0x80000),
    // ScanList +0x24 / count +0x824) that the CShip calls an enemy and that is not dying; a
    // CSimple also needs TypeBits & 0x7f0000 or a watch-listed gameobj (+0xa8). The nearest
    // watch-listed one wins, else the nearest of all; null without a scanner. Caller: HUD_Target
    // event 0x26.
    IObjRW* PickNextTarget(int ConnectionId);
    // FUN_00544b90 -- the player's target: IObjAffect set_target with the player's connection,
    // then every gun (CEquipTraverser(0x80)) re-aimed at it through FUN_005351d0
    // (FClientEquippedObj::slot_44, called directly). Callers: HUD_Target events 0x24 / 0x25 /
    // 0x26.
    void SetTargetWithGuns(IObjRW* Target, unsigned short SubTarget);
    // FUN_00544c30 -- the tractor: false without a tractor (FindFirst(0x100000)); with a Target
    // and a tractor that is not destroyed, a CLoot target ((class & 0x803) == 0x803) gets
    // IObjAffect vt+0x40; otherwise vt+0x4c with false. True whenever there is a tractor.
    // Caller: HUD_Target event 0x8b.
    bool RequestTractor(IObjRW* Target);
    // FUN_00546d00 -- the answer to the request slot_5c sent: the pending callback (+0xec) hears
    // (Result, Code, Extra) and is dropped when it answers false (no callback: "stale request
    // detected"); the player's ship then plays the docking voice line for Result 0 / 2 and Code
    // 0..5. Caller: the IClient REQUEST_RETURNED handler LAB_0053bf80, on whatever gameobj the
    // id names.
    void RequestReturned(int Result, int Code, int Extra);
    // FUN_00543f50 -- SND_CARGO_JETTISONED at the hull's HpBaySurface hardpoint (the bay-door
    // sequence of the notify, case 2, with that sound). Caller: the CREATELOOT handler
    // FUN_0053b910 for a kind-1 request, on the owner's gameobj.
    void PlayCargoJettisonSound();

    // ---- data (FUN_00543380 zeroes all of it) ------------------------------------------
    // +0xe4 / +0xe8 -- while the countdown is non-zero the update (FUN_00545050) runs it down by
    // Dt; once it reaches 0, CShip::close_bay runs +0xe8 times and both return to 0. The writer
    // is the server arm of IObjAffect slot 14 (FUN_005467e0): one more close per jettison, the
    // countdown += open_bay's result + CShip::JETTISON_CLEARANCE_TIME.
    float BayCloseTimer;                  // +0xe4
    int BayCloseCount;                    // +0xe8
    RequestCB* PendingRequestCB;             // +0xec  slot_5c stores its callback, slot_60 clears
    ISoundSource* ThrusterSound;          // +0xf0  the dtor stops (vt+0x60) and releases (vt+8) it
    // +0xf4 .. +0x103 -- a 0x10 effect companion (arch, sound, particle, light): the dtor
    // releases +0xfc (FUN_004f7a90), +0xf8 (vt+8), +0x100 (FUN_00537ad0), then zeroes +0xf4.
    void* AtmosphereBurnCompanion[4];
    Archetype::Root* PilotMeshArch;      // +0x104 the dtor releases it
    FMeshHolder PilotMeshHolder;           // +0x108 FUN_00420d40 (the dtor runs FUN_00420f70)
    Transform PilotTransform;             // +0x118 identity orientation, zero position
    Archetype::ISound* FireFailedSoundArch;     // +0x148 fire-failed sound archetype
    ISoundSource* FireFailedSound;         // +0x14c the dtor stops and releases it
    float FireFailedCooldown;                 // +0x150 the update runs it down by Dt while above 0
    unsigned char bHullBreachWarned;         // +0x154
    unsigned char Pad155[3];
    // +0x158 -- the EJumpState FUN_005437d0 stores last. Readers: the draw pass (2 and 3),
    // StarSys music, FUN_005437d0 itself (== 5).
    int JumpState;
    unsigned char bJumpInReady;         // +0x15c FUN_00543630 sets 1
    unsigned char Pad15d[3];
    // +0x160 .. +0x183 -- one 0x24-byte member whose destructor is FUN_00544710 (inlined in the
    // Ship dtor): a dword, then two 0x10 effect companions -- the tradelane ring FX binders
    // tradelane_fx.cpp reaches by offset (+0x164 / +0x174). (The Ship dtor runs with ESI =
    // this + 0xc, so its "+0x17c" is the +0x188 object below.)
    unsigned TradeLaneRing;              // +0x160
    void* TradeLaneShipCompanion[4];              // +0x164 companion (arch, sound, particle, light)
    void* TradeLaneTunnelCompanion[4];              // +0x174 companion
    unsigned TradeLaneFlags;              // +0x184 (tradelane_fx.cpp reads bytes +0x184/+0x185)
    // +0x188 -- a 0xdc-byte object built by FUN_00543530 (FUN_00503440(this, Arg)) or
    // FUN_00543630 (FUN_005038c0(this)), destroyed by FUN_00503a80 + free (FUN_005437d0 case 0,
    // FUN_00543530, FUN_00543630, the dtor). FUN_005437d0's tail clears its +0x70.
    void* JumpEffect;
    // +0x18c -- flag byte: bit 0 / bit 1 set by slot 105 (FUN_00545ed0), both cleared by slot 92
    // (FUN_005447b0), bit 3 set by FUN_00543b40. The ctor clears bits 0..1 and then stores 0
    // over the whole dword.
    unsigned char ShipFlags;
    unsigned char Pad18d[3];
};
static_assert(offsetof(FClientShipGameobj, PendingRequestCB) == 0xec, "Ship +0xec");
static_assert(offsetof(FClientShipGameobj, ThrusterSound) == 0xf0, "Ship +0xf0");
static_assert(offsetof(FClientShipGameobj, PilotMeshArch) == 0x104, "Ship +0x104");
static_assert(offsetof(FClientShipGameobj, PilotMeshHolder) == 0x108, "Ship mesh holder +0x108");
static_assert(offsetof(FClientShipGameobj, PilotTransform) == 0x118, "Ship transform +0x118");
static_assert(offsetof(FClientShipGameobj, FireFailedSoundArch) == 0x148, "Ship +0x148");
static_assert(offsetof(FClientShipGameobj, JumpState) == 0x158, "Ship +0x158");
static_assert(offsetof(FClientShipGameobj, TradeLaneRing) == 0x160, "Ship +0x160");
static_assert(offsetof(FClientShipGameobj, TradeLaneShipCompanion) == 0x164, "Ship companion +0x164");
static_assert(offsetof(FClientShipGameobj, TradeLaneTunnelCompanion) == 0x174, "Ship companion +0x174");
static_assert(offsetof(FClientShipGameobj, TradeLaneFlags) == 0x184, "Ship +0x184");
static_assert(offsetof(FClientShipGameobj, JumpEffect) == 0x188, "Ship +0x188");
static_assert(offsetof(FClientShipGameobj, ShipFlags) == 0x18c, "Ship +0x18c");
static_assert(sizeof(FClientShipGameobj) == 0x190, "Ship sizeof 0x190 (operator_new(400))");

// ===========================================================================
// The projectile branch: Projectile : Simple, Munition : Projectile, Beam : Munition.
//   Projectile  ctor FUN_00542550(kind)  0xc0  IObjRW 005df95c, IObjAffect 005df8dc,
//                                             CBase 005df8d0 -> FUN_005425c0
//   Munition    ctor FUN_0052e100(kind)  0xc0  IObjRW 005dd8cc, IObjAffect 005dd84c,
//                                             CBase 005dd844 -> FUN_0053d440
//   Beam        ctor inlined in FUN_0052dde0
//                                       0xd4  IObjRW 005dd67c, IObjAffect 005dd5fc,
//                                             CBase 005dd5f4 -> FUN_0052e140
//   Mine        ctor inlined in FUN_0053ca40: FUN_00542550(5)
//                                       0xc0  IObjRW 005dedcc, IObjAffect 005ded4c,
//                                             CBase 005ded44 -> FUN_0053d440
//   CounterMeasure  inlined in FUN_0053cd70: FUN_00542550(6)
//                                       0xc0  IObjRW 005df01c, IObjAffect 005def9c,
//                                             CBase 005def94 -> FUN_0053d440
//   Guided      inlined in FUN_0053d080: FUN_00542550(4)
//                                       0xc0  IObjRW 005df26c, IObjAffect 005df1ec,
//                                             CBase 005df1e4 -> FUN_0053d440
// RTTI (COL at vftable[-4]): Mine 005eef54 and CounterMeasure 005eefe4 are Projectile's
// children, Guided 005ef074 is Munition's. The three add no data (operator new(0xc0), the
// Projectile size) and share the deleting destructor FUN_0053d440 (Projectile's body). IObjAffect
// is Simple's table unchanged in all of them except Guided's slot 18 (LAB_0053eed0).
// ===========================================================================

class FClientProjectileGameobj : public FClientSimpleGameobj
{
public:
    // FUN_00542550: Simple's ctor, ConstEffectDelay = -1.0f, the companion zeroed.
    explicit FClientProjectileGameobj(EClientGameobjKind Kind);
    // FUN_005425f0 (this = the CBase sub-object); deleting wrapper FUN_005425c0.
    ~FClientProjectileGameobj() override;

    // 90 +0x168: pure.
    void create(Archetype::Root* Arch) override = 0;
    // 92 +0x170 FUN_005426e0: GameObject's slot 92, then the munition companion's const_effect
    // -- started now through FUN_00503070 when its delay is 0, else the delay is armed.
    void PostCreateInit() override;
    // 93 +0x174 FUN_00542790: unless done, GameObject's update, then the armed delay counts
    // down and, on reaching 0, starts the const_effect.
    void update(float Dt) override;
    // 104 +0x1a0 FUN_0053d4f0: empty.
    void OnSurfaceCollisionHit(void const* Contact, void const* B, FClientGameobj* Other,
                 void const* D) override;
    // 108 +0x1b0 FUN_00542870: drop the companion's node from the effect list (FUN_004fcef0),
    // then GameObject's slot 108.
    void Explode(Vector const* Pos) override;
    // 112 +0x1c0 LAB_005428a0: *Out = identity rotation, zero translation.
    virtual void GetConstEffectTransform(Transform* Out);

    float ConstEffectDelay;     // +0xac  -1.0f = not armed
    void* OneShotCompanion[4];  // +0xb0  {EffectArch, ISoundSource*, particle, light}
};
static_assert(offsetof(FClientProjectileGameobj, ConstEffectDelay) == 0xac, "Projectile +0xac");
static_assert(offsetof(FClientProjectileGameobj, OneShotCompanion) == 0xb0, "Projectile +0xb0");
static_assert(sizeof(FClientProjectileGameobj) == 0xc0, "Projectile sizeof 0xc0");

class FClientMunitionGameobj : public FClientProjectileGameobj
{
public:
    // FUN_0052e100: Projectile's ctor with the kind, then Munition's vtables.
    explicit FClientMunitionGameobj(EClientGameobjKind Kind);
    // Projectile's body (the thunk FUN_0053d470); deleting wrapper FUN_0053d440.
    ~FClientMunitionGameobj() override;

    // 101 +0x194 FUN_0053ed80: Other->OnMunitionHit(A, D, this), then
    // this->ReportCollisionToServer(A, B, Other, D).
    void OnCollision(void const* A, void const* B, FClientGameobj* Other, void const* D) override;
};
static_assert(sizeof(FClientMunitionGameobj) == 0xc0, "Munition sizeof 0xc0");

struct FBeamEffect; // beam_effect.h

class FClientBeamGameobj : public FClientMunitionGameobj
{
public:
    // Inlined in FUN_0052dde0: Munition's ctor with kind 3, FUN_005154a0 on +0xc0 with four
    // nulls, +0xd0 = 0.
    FClientBeamGameobj();
    // FUN_0053f1e0 (this = the CBase sub-object); deleting wrapper FUN_0052e140.
    ~FClientBeamGameobj() override;

    // 90 +0x168 FUN_0053f2f0: FUN_00539bc0(0x67, Arch), then the CBeam's open(Arch) with no
    // null test.
    void create(Archetype::Root* Arch) override;
    // 91 +0x16c FUN_0053f330: returns false.
    bool find(unsigned const& Id) override;
    // 93 +0x174 LAB_0053f610: Projectile's update, the object's update(Dt, UPDATE_DEFAULT),
    // then MarkForRemoval(true) once the object's +0xe8 life goes below 0 (unless IsExploding).
    void update(float Dt) override;
    // 101 +0x194 LAB_0053f690: unless IsExploding -- the pair (Other->OnMunitionHit,
    // this->ReportCollisionToServer), the CBeam's +0xf8 velocity zeroed, the beam effect flagged hit
    // (FUN_004fc8f0(1, Other == player)) and released, the companion's node dropped, then
    // Projectile's slot 108 at A.
    void OnCollision(void const* A, void const* B, FClientGameobj* Other, void const* D) override;
    // 105 +0x1a4 LAB_0053f780: empty.
    void OnMunitionHit(void const* A, void const* B, FClientGameobj* Munition) override;
    // 107 +0x1ac LAB_0053f790: the munition-collision report to the server,
    // IServer::SPMunitionCollision (slot 53, +0xd4) -- in multiplayer only for the player's
    // own shots.
    void ReportCollisionToServer(void const* A, void const* B, FClientGameobj* Other, void const* D) override;
    // 111 +0x1bc LAB_0052e130: 0x2000000.
    unsigned GetTypeBits() const override;

    void* Companion[4];       // +0xc0  FUN_005154a0; the effect list gets a copy
    FBeamEffect* BeamAlloc;   // +0xd0  FUN_0053f340's beam effect
};
static_assert(offsetof(FClientBeamGameobj, Companion) == 0xc0, "Beam +0xc0");
static_assert(offsetof(FClientBeamGameobj, BeamAlloc) == 0xd0, "Beam +0xd0");
static_assert(sizeof(FClientBeamGameobj) == 0xd4, "Beam sizeof 0xd4");

// Mine : Projectile (retail Mine.cpp has no string of its own; the bodies sit beside
// Loot.cpp's).
class FClientMineGameobj : public FClientProjectileGameobj
{
public:
    // Inlined in FUN_0053ca40: FUN_00542550(5), then Mine's three vtables.
    FClientMineGameobj();
    // FUN_0053d440 -> thunk_FUN_005425f0: Projectile's body.
    ~FClientMineGameobj() override;

    // IObjInspect slot 1 (+0x04) LAB_0053eae0: a destroyed mine (Flags bit 2) reports a zero
    // velocity, any other asks IObjInspectImpl::get_velocity.
    Vector get_velocity() const override;

    // 90 +0x168 FUN_0053e930: FUN_00539bc0(0xf, Arch), then open(Arch) on the object narrowed
    // by class & 0xf (no null test).
    void create(Archetype::Root* Arch) override;
    // 91 +0x16c FUN_0053e970: CObject::Find(Id, 0xf), advise(true), Object != 0.
    bool find(unsigned const& Id) override;
    // 93 +0x174 FUN_0053e9a0: Projectile's update, then the CMine's update -- UPDATE_DEFAULT
    // when the object's owner is this connection, UPDATE_DUMB in multiplayer.
    void update(float Dt) override;
    // 105 +0x1a4 LAB_0053eaa0: only a CBeam (class & 0x67) munition reaches GameObject's
    // munition-hit effect (FUN_00539d80).
    void OnMunitionHit(void const* A, void const* B, FClientGameobj* Munition) override;
    // 110 +0x1b8 FUN_0053ea60: while NebulaUpdateTimer is exactly 0, the nebula at the position
    // into CurrentNebula / its fog factor into NebulaFogFactor, and NebulaUpdateTimer = 1.0f.
    void UpdateNebula(float Dt) override;
};
static_assert(sizeof(FClientMineGameobj) == 0xc0, "Mine sizeof 0xc0 (operator new(0xc0))");

// CounterMeasure : Projectile.
class FClientCounterMeasureGameobj : public FClientProjectileGameobj
{
public:
    // Inlined in FUN_0053cd70: FUN_00542550(6), then CounterMeasure's three vtables.
    FClientCounterMeasureGameobj();
    // FUN_0053d440 -> thunk_FUN_005425f0: Projectile's body.
    ~FClientCounterMeasureGameobj() override;

    // 90 +0x168 FUN_0052ff90: FUN_00539bc0(0x17, Arch), open on class & 0x17.
    void create(Archetype::Root* Arch) override;
    // 91 +0x16c FUN_0052ffd0: CObject::Find(Id, 0x17), advise(true), Object != 0.
    bool find(unsigned const& Id) override;
    // 93 +0x174 FUN_00530000: as Mine's with class & 0x17.
    void update(float Dt) override;
    // 105 +0x1a4 LAB_0053f780: empty.
    void OnMunitionHit(void const* A, void const* B, FClientGameobj* Munition) override;
    // 110 +0x1b8 FUN_0053ea60: Mine's.
    void UpdateNebula(float Dt) override;
};
static_assert(sizeof(FClientCounterMeasureGameobj) == 0xc0, "CounterMeasure sizeof 0xc0");

// Guided : Munition -- the missile (retail Munition.cpp, .rdata 005df858).
class FClientGuidedGameobj : public FClientMunitionGameobj
{
public:
    // Inlined in FUN_0053d080: FUN_00542550(4), then Guided's three vtables.
    FClientGuidedGameobj();
    // FUN_0053d440 -> thunk_FUN_005425f0: Projectile's body.
    ~FClientGuidedGameobj() override;

    // 90 +0x168 FUN_0053ec30: FUN_00539bc0(0xa7, Arch), open on class & 0xa7.
    void create(Archetype::Root* Arch) override;
    // 91 +0x16c FUN_0053ec70: CObject::Find(Id, 0xa7), advise(true), Object != 0.
    bool find(unsigned const& Id) override;
    // 93 +0x174 FUN_0053ecb0: as Mine's with class & 0xa7.
    void update(float Dt) override;
    // 105 +0x1a4 LAB_0053eaa0: Mine's (CBeam munitions only).
    void OnMunitionHit(void const* A, void const* B, FClientGameobj* Munition) override;
    // 107 +0x1ac LAB_0053ee00: the munition-collision report to the server
    // (IServer::SPMunitionCollision, slot 53) -- in multiplayer only when Other belongs to this
    // connection.
    void ReportCollisionToServer(void const* A, void const* B, FClientGameobj* Other, void const* D) override;
    // 108 +0x1b0 FUN_0053edc0: the const_effect particle (OneShotCompanion +8) is let go of this
    // gameobj (FUN_004f7d20 with no owner, part -1 and the gameobj's frame), then Projectile's
    // slot 108.
    void Explode(Vector const* Pos) override;
    // 110 +0x1b8 FUN_0053ea60: Mine's.
    void UpdateNebula(float Dt) override;
    // 111 +0x1bc LAB_0053d430: 0x1000000.
    unsigned GetTypeBits() const override;
    // 112 +0x1c0 LAB_0053f030: the const_effect / trail frame -- the munition companion's
    // HP_trail_parent hardpoint, else the mesh's bounds on the renderer.
    void GetConstEffectTransform(Transform* Out) override;

    // IObjAffect slot 18 (+0x48) LAB_0053eed0: re-target a seeking missile, and tell
    // the server (IServer::SetTarget) when the request came from this connection.
    int set_target(IObjRW* Target, unsigned short SubTarget, int SourceClient) override;
};
static_assert(sizeof(FClientGuidedGameobj) == 0xc0, "Guided sizeof 0xc0");

// Loot : Simple (retail E:\FL\Scratch\Source\Client\Space\Loot.cpp, .rdata 005df7e0).
//   ctor FUN_0053e470  0xb0  IObjRW 005df5d4, IObjAffect 005df554, CBase 005df548 -> FUN_0053e4b0
class FClientLootGameobj : public FClientSimpleGameobj
{
public:
    // FUN_0053e470: FUN_00547440(7), +0xac = 0, Loot's three vtables.
    FClientLootGameobj();
    // FUN_0053e4e0 (this = the CBase sub-object); deleting wrapper FUN_0053e4b0.
    ~FClientLootGameobj() override;

    // 90 +0x168 LAB_0053e550: FUN_00539bc0(0x803, Arch), open on class & 0x803.
    void create(Archetype::Root* Arch) override;
    // 91 +0x16c LAB_0053e5a0: CObject::Find(Id, 0x803), advise(true), Object != 0.
    bool find(unsigned const& Id) override;
    // 92 +0x170 LAB_0053e5e0: GameObject's slot 92, the crate's tractored_explosion into +0xac,
    // then the nebula lookup (FUN_005553b0) into CurrentNebula / NebulaFogFactor.
    void PostCreateInit() override;
    // 93 +0x174 FUN_0053e6a0: GameObject's update, then the CLoot's update.
    void update(float Dt) override;
    // 102 +0x198 FUN_0053e770: multiplayer only -- the root entry sets the hit points, any
    // other entry the contents' hit points (a unit lost when they ran out).
    void ApplyDamageList(DamageList const& Damage) override;
    // 103 +0x19c LAB_0053e8a0: DestroyType 2 (tractored in) explodes with the
    // tractored_explosion; anything else goes to Simple's slot 103.
    void Destroy(int DestroyType) override;

    Archetype::Explosion* TractoredExplosionArch; // +0xac  slot 92 resolves; the dtor releases
};
static_assert(offsetof(FClientLootGameobj, TractoredExplosionArch) == 0xac, "Loot +0xac");
static_assert(sizeof(FClientLootGameobj) == 0xb0, "Loot sizeof 0xb0 (operator new(0xb0))");

// FUN_00549e30 -- before the docking camera takes the view, close the HUD's rear view
// (command 0x39 when HUD_Status +0x5ba == 1) and turret view (command 0xbf when +0x5b9 == 1).
// Callers: the ship's land and launch paths.
void CloseRearAndTurretViewsForDocking();

// operator new + the Solar constructor FUN_005475b0 (space_solar_world.cpp's solar build).
FClientGameobj* ClientGameobj_CreateSolar();

// FUN_00539bc0 -- CObject::Alloc(Class) into Self->Object, Arch->create_instance,
// DALib::Engine UpdateInstance(Object+4, 0, 0), CBase::advise(true). The caller opens the
// object afterwards. Callers: GameObject create (LAB_005396f0 inlines it), the ship / solar
// create tails, Beam FUN_0053f2f0, Debris FUN_005304b0.
void GameobjCreateOfClass(FClientGameobj* Self, CObject::Class ObjectClass,
                          Archetype::Root* Arch);

// FUN_00536100 -- take a reference on everything an explosion archetype will spawn: each
// effect's EffectArch (FUN_0040e960) and each innards debris simple (Root vt+0x0c, load).
// Eleven retail callers (the Gameobj/equipment explosion preloads). Null is a no-op.
void ExplosionArchAddRef(Archetype::Explosion const* Expl);
// FUN_00536170 -- the matching release: EffectArch FUN_0040e990 and Root::Release.
void ExplosionArchRelease(Archetype::Explosion const* Expl);

// The Gameobj watch list: retail std::list<BaseWatcher> DAT_0066d33c (head) / DAT_0066d340
// (count), built by the static initialiser LAB_0045a7e0 and torn down by LAB_0045a820.
// A Gameobj goes in when its +0xa8 flag is set (FUN_00547550, FUN_00539740); the HUD walks
// the list and drops dead entries through FUN_0045a990(0).
//
// FUN_0045a890 -- append a watcher on Obj unless one already watches it.
void GameobjWatchListAdd(IObjRW* Obj);
// FUN_0045a990 -- erase the first entry watching Obj (a null Obj matches an entry
// whose object has gone), if any.
void GameobjWatchListRemove(IObjRW* Obj);
// Read access for the HUD walk: the list itself, in insertion order.
std::list<BaseWatcher> const& GameobjWatchList();
