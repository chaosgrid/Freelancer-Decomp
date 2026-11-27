// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Space/client_gameobj.h"

#include "DALib.h"
#include "Space/Effects/cli_projectile_fx.h"   // CliEffectListClear / FCliEquipFirePacket / fire-host ports
#include "Common/client_cliobj.h"       // Archetype::CliShipBayDoorSound (CliShip +0x58 / +0x5c)
#include "Space/client_debris.h"       // FindDebrisArch / CreateDebrisGameobjForObject (FUN_00533d10)
#include "Space/Effects/client_equip_fx.h"     // FClientGameobj::RebuildEquipFx / FClientGameobj::ClearEquipFx
#include "Space/Effects/client_explosion.h"    // Action_Explode (FUN_005361e0)
#include "Space/Effects/client_fuse.h"         // FFuseBurnNode
#include "Space/client_objdb.h"        // GetClientObjDb (DAT_00678a2c)
#include "common_csys.h"         // Transform (the launch camera's Csys)
#include "common_id.h"           // CreateID
#include "common_ini.h"          // INI_Reader (Solar ReadIni, FUN_005485c0)
#include "common_newchar.h"      // pub::StateGraph::get_state_graph
#include "common_server.h"       // IServer (DAT_0067ecd0)
#include "server_cobject.h"      // DestroyType
#include "common_misc.h"         // SinglePlayer, GetFileSysPath (Common exports)
#include "dacom.h"               // FDUMP
#include "common_goods.h"        // FindHardpoint_PS / HardpointInfo
#include "common_physys.h"       // Collision::CalcCollisionDamage
#include "Space/Effects/effect_type_info.h"    // EffectTypeWithinPerf (FUN_004fd570)
#include "engine/engbase.h"
#include "Common/Graphics/fade_to_black.h"       // SetFadeImmediate (FUN_00412070)
#include "Common/engine_light.h"        // FGameLight, EngineLight setters (FUN_00544230)
#include "Core/game_options.h"        // GetGameOption (FUN_005792b0)
#include "Core/globals.h"             // G_RenderSurfaceWidth (DAT_00610850)
#include "Common/Audio/menu_sound_arch.h"     // LookupClientSoundArch (FUN_0042ae40), Source3D factory
#ifdef FREELANCER_RP9
#include "Space/nebula_volume.h"       // NebulaVolume::NoteOpaqueSphere (port-only Direct3D 9 nebula)
#endif
#include "NeuroNet/nn_messaging.h"        // NnVoiceEnqueue (FUN_00489e80)
#include "NeuroNet/nn_modal.h"            // G_bNnScreenUp (DAT_0067dcc8), NnArrivalTextShow (FUN_0045b310)
#include "Space/HUD/hud_command_dispatch.h" // HudPostCommand (FUN_005763b0)
#include "Space/HUD/hud_damage_indicator.h" // GetHudDamageIndicator (DAT_00674a10), FUN_004d6d40 / 004d6cd0
#include "Space/HUD/hud_status_core.h"     // GetHudStatus (DAT_00674aa0)
#include "Space/Effects/jump_effect.h"         // FJumpShipEffect (Ship +0x188, dtor FUN_00503a80)
#include "Space/Effects/gate_tunnel.h"         // FGateTunnel::Light (+0x2a8, Ship slot 97 LAB_00546480)
#include "Common/Graphics/part_cache.h"          // CopyTransform12 (FUN_00406880)
#include "render/IFLAppearance.h" // FUN_005462d0 / 00546380 / 00546400
#include "Space/HUD/hud_waypoint.h"        // GetHudWaypoint (DAT_00674bc8), FUN_004f4d20 / 004f4ca0
#include "Common/input_manager.h"       // GetClientMouseCursorDrawFlag (FUN_0041dd90)
#include "NeuroNet/nn_ui_pulse.h"         // G_NnPaletteViolet (DAT_00679b80)
#include "Interface/win3d_engine_submit.h" // FUN_00433f40 / FUN_00434630 (Solar waypoint marker)
#include "Space/client_land_action.h"  // Action_Land (FUN_00542b70)
#include "Space/Effects/particle_effect.h"     // FParticleEffect, FParticleEffect::Kill / Release
#include "Space/player_behavior.h"     // GetPlayerObjRW (FUN_0054baf0)
#include "Space/player_spawn.h"        // GetPlayerCharacterId (DAT_00673378), GetPlayerConnectionId
#include "render/rendcomp.h"     // IRenderer2 (Guided slot 112's extents / centre)
#include "Network/server_connect.h"      // GetLocalServerInterface (DAT_0067ecd0)
#include "soundmanager.h"        // ISoundSource
#include "Space/Cameras/space_camera.h"        // SetDockingCameraTransform / RequestSpaceCameraMode
#include "Space/space_scene_obj_draw.h" // FClientGameobj::PickLod (FUN_00539820)
#include "Space/space_state.h"         // SetPendingLandBase (FUN_00548ef0)
#include "Space/star_sphere.h"         // FStarSphere (Solar +0xe4)
#include "Space/star_sys.h"            // StarSysSuppressPlayerCollisions, GetAttitudeClass
#include "Common/Graphics/texture_panels.h"      // TexturePanelsCreate / Load (Solar "ring", FUN_005485c0)
#include "Space/Effects/tradelane_fx.h"        // TradeLaneShipGoHandler (the LAB_00546e90 tail)
#include "Common/Graphics/vis_effect_arch.h"     // FindEffectArch / FEffectArch::AddRef / FEffectArch::Release

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <set>
#include <string>
#include <windows.h>

namespace
{
    char const kGameobjSourceFile[] = "E:\\FL\\Scratch\\Source\\Client\\space\\Gameobj.cpp";
    // .rdata:005de4c4 -- FUN_00533d10's FDUMP file argument.
    char const kEquippedObjSourceFile[] =
        "E:\\FL\\Scratch\\Source\\Client\\Space\\EquippedObj.cpp";
    // .rdata:005dff40 -- FUN_00543b40's FDUMP file argument.
    char const kShipSourceFile[] = "E:\\FL\\Scratch\\Source\\Client\\space\\ship.cpp";

    // The 0x18-byte node of the GameObject effect list (next, prev, 0x10 companion) -- the
    // head and the list's own teardown live here; cli_projectile_fx.cpp owns the operations.
    struct FGameobjEffectListHead
    {
        FGameobjEffectListHead* Next;
        FGameobjEffectListHead* Prev;
        void* Companion[4];
    };
    static_assert(sizeof(FGameobjEffectListHead) == 0x18, "effect list node 0x18");
}

// ===========================================================================
// GameObject effect list (+0x28)
// ===========================================================================

FGameobjEffectListStorage::FGameobjEffectListStorage(unsigned char InAllocatorByte)
{
    // Inlined in FUN_005392d0.
    AllocatorByte = InAllocatorByte;
    FGameobjEffectListHead* const NewHead =
        static_cast<FGameobjEffectListHead*>(::operator new(sizeof(FGameobjEffectListHead)));
    NewHead->Next = NewHead;
    NewHead->Prev = NewHead;
    Head = NewHead;
    Count = 0;
}

void* FGameobjEffectListStorage::Erase(void* Where)
{
    // FUN_0053aab0. Next is read before the unlink; the companion teardown is FUN_00502d90's
    // sequence inlined.
    FGameobjEffectListHead* const Node = static_cast<FGameobjEffectListHead*>(Where);
    FGameobjEffectListHead* const Next = Node->Next;
    Node->Prev->Next = Node->Next;
    Node->Next->Prev = Node->Prev;
    CliEffectCompanionClear(Node->Companion);
    ::operator delete(Node);
    Count = Count - 1;
    return Next;
}

FGameobjEffectListStorage::~FGameobjEffectListStorage()
{
    // FUN_0053aa40 (also inlined in FUN_005393e0 and FUN_005394b0).
    FGameobjEffectListHead* const ListHead = static_cast<FGameobjEffectListHead*>(Head);
    void* Node = ListHead->Next;
    while (Node != ListHead)
        Node = Erase(Node);
    ::operator delete(Head);
    Head = nullptr;
    Count = 0;
}

FGameobjEffectList::~FGameobjEffectList()
{
    // FUN_005393e0: FUN_004fd0d0(0) under EH state 0 (its unwind action is FUN_0053aa40, the
    // storage destructor), then the storage destructor (the base) on the normal path.
    CliEffectListClear(this, nullptr);
}

void FClientGameobj::CreateOfClass(CObject::Class ObjectClass, Archetype::Root* Arch)
{
    // FUN_00539bc0. create_instance's return value is dropped (the engine stores the id at
    // CObject+4 through IEngineInstance::initialize_instance). No null tests. open is NOT part
    // of it -- the caller does that afterwards.
    Object = CObject::Alloc(ObjectClass);
    Arch->create_instance(static_cast<IEngineInstance*>(Object));
    DALib::Engine->UpdateInstance(Object->EngineInstanceId, 0u,
                                  nullptr);
    advise(true);
}

namespace
{
    // The object narrowed by (ObjectClass & Mask) == Mask, null otherwise. `this + 0xc` (the
    // CBase sub-object) is never null for a live gameobj, so the tests retail makes on it and on
    // `this` fold into the object test.
    CObject* NarrowObject(FClientGameobj const* Self, unsigned Mask)
    {
        CObject* const Obj = Self->Object;
        if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & Mask) != Mask)
            return nullptr;
        return Obj;
    }

    // The create tail FUN_005477d0 (solar, 0x303) and LAB_00546550 (ship, 0x503) share. No
    // null test on the narrowed object.
    void CreateOpenConnect(FClientEquippedObj* Self, CObject::Class ObjectClass,
                           Archetype::Root* Arch)
    {
        Self->CreateOfClass(ObjectClass, Arch);
        unsigned const Mask = static_cast<unsigned>(ObjectClass);
        NarrowObject(Self, Mask)->open(Arch);
        static_cast<CEqObj*>(NarrowObject(Self, Mask))->connect(static_cast<INotify*>(Self));
    }

    // FUN_00547760 (solar, 0x303) / FUN_005465d0 (ship, 0x503). No null test on the narrowed
    // object before connect.
    bool FindAndConnect(FClientEquippedObj* Self, CObject::Class ObjectClass, unsigned const& Id)
    {
        // The reference overload ?Find@CObject@@SAPAU1@ABIW4Class@1@@Z, as the exe imports.
        Self->Object = CObject::Find(Id, ObjectClass);
        Self->advise(true);
        static_cast<CEqObj*>(NarrowObject(Self, static_cast<unsigned>(ObjectClass)))
            ->connect(static_cast<INotify*>(Self));
        return Self->Object != nullptr;
    }

    // The server interface every multiplayer path talks to: DAT_0067ecd0.
    IServer* LocalServer()
    {
        return static_cast<IServer*>(GetLocalServerInterface());
    }

    // DAT_00613e60 (.data, 3; read only by FUN_00539f90): how many HitEffects slots it scans.
    constexpr int kHitEffectSlotCount = 3;
    // The replace-by-lowest ceiling (an immediate in FUN_00539f90).
    constexpr float kHitEffectReplaceCeiling = 100.0f;
    static_assert(sizeof(FClientGameobj::HitEffects) / sizeof(FClientGameobj::HitEffects[0]) ==
                      kHitEffectSlotCount,
                  "HitEffects holds DAT_00613e60 slots");

    // The companion of the object's archetype, or null -- CObject::get_archetype called twice
    // with a null test on the object and on the archetype, as slots 104 / 105 do.
    Archetype::ICliObj* CompanionOfObjectArchetype(CObject* Obj)
    {
        if (Obj == nullptr || Obj->get_archetype() == nullptr)
            return nullptr;
        return Obj->get_archetype()->Companion;
    }

    // The hit-effect placement shared by FUN_00539f90 and FUN_0053a230: GetDirectionMatrix on
    // {the object's orientation, zero translation} and the object-space direction; the result's
    // columns taken in the order 0, -2, 1; the object-space point as the translation. No null
    // test on the object.
    void BuildHitEffectTransform(Transform* Out, CObject* Obj, Vector const& Direction,
                                 Vector const& LocalPos)
    {
        Transform Basis;
        Basis.Rotation = Obj->Orientation;
        Basis.Translation.x = 0.0f;
        Basis.Translation.y = 0.0f;
        Basis.Translation.z = 0.0f;
        Vector Target;
        Target.x = Direction.x;
        Target.y = Direction.y;
        Target.z = Direction.z;
        Matrix const Facing = GetDirectionMatrix(Basis, Target);
        int Row = 0;
        for (Row = 0; Row < 3; ++Row)
        {
            Out->Rotation.E[Row][0] = Facing.E[Row][0];
            Out->Rotation.E[Row][1] = -Facing.E[Row][2];
            Out->Rotation.E[Row][2] = Facing.E[Row][1];
        }
        Out->Translation = LocalPos;
    }
}

// ===========================================================================
// GameObject
// ===========================================================================

FClientGameobj::FClientGameobj(EClientGameobjKind Kind)
    // The effect list is a member (EH state 1 -> 2); from here on an exception destroys it
    // through FUN_005393e0.
    : EffectList(static_cast<unsigned char>(static_cast<int>(Kind)))
{
    // FUN_005392d0. ListNext / ListPrev (+0x14 / +0x18), CachedAttitude (+0x9c) and
    // bWatchListed (+0xa8) are not written: the ObjDB link, FUN_0053a960 and the derived
    // constructors set them.
    unsigned char const KindByte = static_cast<unsigned char>(static_cast<int>(Kind));
    ManagerKind = Kind;

    // Only the low nibble of the flag byte is cleared; the rest keeps whatever the allocation
    // held.
    Flags = static_cast<unsigned char>(Flags & 0xf0u);
    NebulaUpdateTimer = 0.0f;
    CurrentNebula = nullptr;
    NebulaFogFactor = 0.0f;
    ExplosionArch = nullptr;
    // HudLabel: FUN_004144b0 -- FFontString's own constructor, run as a member.
    HudLabelSurfaceWidth = 0;
    SoundAllocatorByte = KindByte;
    SoundsBegin = nullptr;
    SoundsEnd = nullptr;
    SoundsCap = nullptr;
    bFactionDiscovered = 0;
    AttitudeCountdown = -1;
    HitEffects[0] = nullptr;
    HitEffects[1] = nullptr;
    HitEffects[2] = nullptr;
}

FClientGameobj::~FClientGameobj()
{
    // FUN_005394b0 (this = the CBase sub-object).
    HudLabel.ClearContents(); // FUN_004144f0

    // The count is taken once; a null begin counts as empty.
    int const SoundCount = (SoundsBegin == nullptr) ? 0 : static_cast<int>(SoundsEnd - SoundsBegin);
    for (int Index = 0; Index < SoundCount; ++Index)
    {
        ISoundSource* const Source = SoundsBegin[Index].Source;
        if (Source == nullptr)
            continue;
        Source->Stop();
        SoundsBegin[Index].Source->Release();
    }

    for (int Index = 0; Index < 3; ++Index)
    {
        FParticleEffect* const Effect = HitEffects[Index];
        if (Effect == nullptr)
            continue;
        Effect->FParticleEffect::Kill(); // vt+4 (LAB_004f84d0, shared by both particle vtables)
        HitEffects[Index]->Release();
        HitEffects[Index] = nullptr;
    }

    CliEffectListClear(&EffectList, nullptr);
    Actions.free();
    if (ExplosionArch != nullptr)
        ExplosionArchRelease(ExplosionArch);
    advise(false);
    if (Object != nullptr)
        Object->Release();

    ::operator delete(SoundsBegin);
    SoundsBegin = nullptr;
    SoundsEnd = nullptr;
    SoundsCap = nullptr;

    // The font string's own destructor, inlined in retail at this point. Called explicitly to
    // keep retail's order against the hand-written teardowns around it; ~FFontString then runs
    // on an empty string.
    HudLabel.Destroy();

    // Retail then inlines the effect list's destructor (FUN_005393e0) -- here
    // ~FGameobjEffectList, run as a member after ~FFontString (+0x48) and before ~ActionDB
    // (+0x20), the order retail's EH states give.
}

unsigned int const FClientGameobj::get_id() const
{
    // LAB_00524e70
    CObject* const Obj = NarrowObject(this, CObject::ClassSimple);
    if (Obj == nullptr)
        return 0;
    return static_cast<CSimple const*>(Obj)->Id;
}

bool FClientGameobj::IsExploding() const
{
    // LAB_00524e20
    return ((Flags >> 2) & 1u) != 0;
}

bool FClientGameobj::IsDead() const
{
    // LAB_00524e30
    return (Flags & 1u) != 0;
}

void FClientGameobj::MarkForRemoval(bool bValue)
{
    // FUN_00539c10: the object's collisions go off whatever the argument was.
    Flags = static_cast<unsigned char>((Flags & ~1u) | (static_cast<unsigned>(bValue) & 1u));
    if (Object != nullptr)
        Object->enable_collisions_r(false);
}

bool FClientGameobj::Slot160() const
{
    // LAB_00524e40
    return ((Flags >> 1) & 1u) != 0;
}

void FClientGameobj::Slot164(bool bValue)
{
    // LAB_00539c40
    Flags = static_cast<unsigned char>((Flags & ~2u) | ((static_cast<unsigned>(bValue) << 1) & 2u));
}

void FClientGameobj::create(Archetype::Root* Arch)
{
    // LAB_005396f0: FUN_00539bc0's body inlined with class 1.
    CreateOfClass(CObject::ClassObject, Arch);
}

void FClientGameobj::PostCreateInit()
{
    // FUN_00539740 -- no null tests on the object or its archetype.
    Archetype::Root* const Arch = Object->get_archetype();
    ID_String const* const ExplosionId = &Arch->ExplosionArch; // Arch+0x24

    if (!ExplosionId->IsEmpty())
    {
        ExplosionArch = Archetype::GetExplosion(*ExplosionId);
        if (ExplosionArch != nullptr)
        {
            ExplosionArchAddRef(ExplosionArch);
        }
        else
        {
            // The id's string is fetched first.
            char const* const ExplName = ExplosionId->get_string();
            char const* const ArchName = Object->get_archetype()->Nickname.get_string();
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: Archtype(%s) explosion_arch(%s) not found.",
                  kGameobjSourceFile, 0x85, ArchName, ExplName);
        }
    }

    // get_type's result is not tested.
    uint Type = 0;
    get_type(Type);
    if ((Type & (OBJ_LOOT | OBJ_SHIP_MASK | OBJ_SOLAR_NO_WAYPOINT_MASK)) != 0u && bWatchListed != 0)
        GameobjWatchListAdd(this);
}

void FClientGameobj::update(float Dt)
{
    // FUN_00539c60
    Actions.update(Dt);
    UpdateNebula(Dt);
}

float FClientGameobj::GetPhysicalRadius(Vector& Center)
{
    // FUN_00539810: no null test.
    return Object->get_physical_radius_r(Center);
}

void FClientGameobj::PreSubmitDraw()
{
    // LAB_00401e90: empty.
}

unsigned FClientGameobj::GetNebula(unsigned* OutB)
{
    // LAB_00524e50
    memcpy(OutB, &NebulaFogFactor, sizeof(float));
    return static_cast<unsigned>(reinterpret_cast<uintptr_t>(CurrentNebula));
}

void FClientGameobj::ResetNebula()
{
    // LAB_00524e60
    CurrentNebula = nullptr;
    NebulaFogFactor = 0.0f;
    NebulaUpdateTimer = 10.0f;
}

void FClientGameobj::OnCollision(void const* A, void const* B, FClientGameobj* Other, void const* D)
{
    // FUN_00539c90. No null test on Other.
    Other->OnSurfaceCollisionHit(A, D, this, B);
    ReportCollisionToServer(A, B, Other, D);
}

void FClientGameobj::ApplyDamageList(DamageList const& Damage)
{
    // LAB_0053e4a0: empty.
    (void)Damage;
}

void FClientGameobj::Destroy(int DestroyType)
{
    // FUN_0053a820
    if (DestroyType == DESTROY_VANISH)
    {
        MarkForRemoval(true);
        return;
    }
    if (DestroyType == DESTROY_FUSE)
    {
        Flags = static_cast<unsigned char>(Flags | 4u);
        Explode(nullptr);
        return;
    }
    FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", kGameobjSourceFile, 0x2a4, "Invalid DestroyType!");
}

void FClientGameobj::OnSurfaceCollisionHit(void const* Contact, void const* B, FClientGameobj* Other,
                             void const* D)
{
    // FUN_00539cd0. B and D are not read.
    (void)B;
    (void)D;
    Vector const* const Hit = static_cast<Vector const*>(Contact);
    float const Speed = sqrtf(Hit->x * Hit->x + Hit->y * Hit->y + Hit->z * Hit->z);
    // Other's mass is read first.
    float const OtherMass = Other->get_mass();
    float const MyMass = get_mass();
    float const Damage = Collision::CalcCollisionDamage(Speed, MyMass, OtherMass);
    // FUN_00407450 picks the distance record for the damage.
    FEffectArch* const Effect =
        Archetype::CliObjPickDistanceRecord(CompanionOfObjectArchetype(Object), Damage);
    SpawnSurfaceHitEffect(Other->get_id(), Effect, Hit, Hit + 1, nullptr);
}

void FClientGameobj::OnMunitionHit(void const* A, void const* B, FClientGameobj* Munition)
{
    // FUN_00539d80. A points at the hit point and the surface normal (+0x0c); B is not read.
    // None of the narrowed munition objects below is tested for null.
    (void)B;
    Vector const* const Hit = static_cast<Vector const*>(A);
    Archetype::Munition const* const MunArch =
        static_cast<Archetype::Munition const*>(NarrowObject(Munition, CObject::ClassProjectile)->Archetype);
    // FUN_00407450: the distance record for the munition's hull_damage.
    FEffectArch* const HullEffect =
        Archetype::CliObjPickDistanceRecord(CompanionOfObjectArchetype(Object), MunArch->HullDamage);
    // Keyed by the shot's owner.
    CProjectile const* const Shot = static_cast<CProjectile const*>(NarrowObject(Munition, 0x27u));
    SpawnSurfaceHitEffect(Shot->OwnerId, HullEffect, Hit, Hit + 1, nullptr);
    // FUN_00408a70: a random live effect of the munition's companion.
    FEffectArch* const LiveEffect = Archetype::CliMunitionPickLiveEffect(
        NarrowObject(Munition, CObject::ClassProjectile)->Archetype->Companion);
    Vector const Velocity = NarrowObject(Munition, CObject::ClassObject)->get_velocity();
    SpawnSplashHitEffect(LiveEffect, Hit, Hit + 1, &Velocity, nullptr);
}

void FClientGameobj::SetSubTarget(unsigned short SubObj, unsigned TargetId, unsigned short TargetSubObj)
{
    // LAB_0053f780: empty.
    (void)SubObj;
    (void)TargetId;
    (void)TargetSubObj;
}

void FClientGameobj::ReportCollisionToServer(void const* A, void const* B, FClientGameobj* Other, void const* D)
{
    // FUN_00539e70. A is not read; B and D are sub-object ids (low words).
    (void)A;
    if (Other->get_id() == 0)
        return;
    // In multiplayer only collisions of objects the local player owns report; a failed narrow
    // compares the player's own connection id (DAT_00673344) and so passes.
    if (!SinglePlayer())
    {
        CObject* const OtherObj = NarrowObject(Other, CObject::ClassSimple);
        unsigned const Owner = (OtherObj != nullptr)
                                   ? static_cast<CSimple const*>(OtherObj)->OwnerPlayer
                                   : GetPlayerConnectionId();
        if (Owner != GetPlayerConnectionId())
            return;
    }
    // Other's velocity is read first.
    Vector const OtherVelocity = Other->get_velocity();
    Vector const MyVelocity = get_velocity();
    Vector Relative;
    Relative.x = MyVelocity.x - OtherVelocity.x;
    Relative.y = MyVelocity.y - OtherVelocity.y;
    Relative.z = MyVelocity.z - OtherVelocity.z;
    // Note the mass order: the opposite of OnSurfaceCollisionHit's.
    float const MyMass = get_mass();
    float const OtherMass = Other->get_mass();
    float const Speed =
        sqrtf(Relative.x * Relative.x + Relative.z * Relative.z + Relative.y * Relative.y);
    float const Damage = Collision::CalcCollisionDamage(Speed, OtherMass, MyMass);
    // Retail never writes +0x0e; zero here.
    SSPObjCollisionInfo Info;
    Info.TargetObjectId = get_id();
    Info.TargetSubObjId = static_cast<unsigned short>(reinterpret_cast<uintptr_t>(B));
    Info.Pad_0x06 = 0;
    Info.ObjectId = Other->get_id();
    Info.SubObjId = static_cast<unsigned short>(reinterpret_cast<uintptr_t>(D));
    Info.Pad_0x0e = 0;
    std::memcpy(&Info.DamageBits, &Damage, sizeof(float));
    // IServer slot 55 (+0xdc), no null test.
    LocalServer()->SPObjCollision(Info, GetPlayerConnectionId());
}

void FClientGameobj::Explode(Vector const* Pos)
{
    // FUN_0053a880
    if (ExplosionArch == nullptr)
    {
        MarkForRemoval(true);
        return;
    }
    CliEffectListClear(&EffectList, nullptr);
    Vector const* const Where = (Pos != nullptr) ? Pos : &get_position();
    Vector const Copy = *Where;
    Actions.add(Action_Explode::Create(ExplosionArch, this, &Copy));
}

void FClientGameobj::PlayHitSound(unsigned SourceId, void* SoundArch, Vector const* Pos)
{
    // FUN_0053a4c0. SoundArch is a client sound archetype (SoundArch.cpp); one whose slot 12
    // (+0x30, the lod link) is null plays nothing.
    if (SoundArch == nullptr || GetClientSoundArchLinked(SoundArch) == nullptr)
        return;

    int const Count = (SoundsBegin == nullptr) ? 0 : static_cast<int>(SoundsEnd - SoundsBegin);
    ISoundSource* Voice = nullptr;

    // The first entry keyed to SourceId whose voice's archetype has the id of this
    // archetype's link.
    int Index = 0;
    for (Index = 0; Index < Count; ++Index)
    {
        FGameobjSound* const Entry = &SoundsBegin[Index];
        if (Entry->SourceId != SourceId)
            continue;
        void* const VoiceArch = GetClientSoundSourceArch(Entry->Source);
        void* const Linked = GetClientSoundArchLinked(SoundArch);
        if (GetClientSoundArchId(VoiceArch) == GetClientSoundArchId(Linked))
        {
            Voice = Entry->Source;
            break;
        }
    }

    // Otherwise the first idle voice is taken over for SourceId.
    if (Voice == nullptr)
    {
        for (Index = 0; Index < Count; ++Index)
        {
            FGameobjSound* const Entry = &SoundsBegin[Index];
            if (!Entry->Source->IsActive())
            {
                Voice = Entry->Source;
                Entry->SourceId = SourceId;
                break;
            }
        }
    }

    // Otherwise a new Source3D posed on this object's engine instance, appended to the pool --
    // the VC6 vector insert at end, growing by max(size, 1).
    if (Voice == nullptr)
    {
        Voice = static_cast<ISoundSource*>(CreateClientSoundSource3D());
        Transform Pose;
        Pose.Rotation = Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
        Pose.Translation.x = 0.0f;
        Pose.Translation.y = 0.0f;
        Pose.Translation.z = 0.0f;
        int const Attach = static_cast<int>(get_index());
        Voice->SetPose(Attach, &Pose);

        FGameobjSound Added;
        Added.SourceId = SourceId;
        Added.Source = Voice;
        if (SoundsCap - SoundsEnd < 1)
        {
            int const Size = (SoundsBegin == nullptr) ? 0 : static_cast<int>(SoundsEnd - SoundsBegin);
            int const Grow = (SoundsBegin == nullptr || Size <= 1) ? 1 : Size;
            int NewCapacity = Size + Grow;
            if (NewCapacity < 0)
                NewCapacity = 0;
            FGameobjSound* const Block = static_cast<FGameobjSound*>(
                ::operator new(static_cast<size_t>(NewCapacity) * sizeof(FGameobjSound)));
            int Copy = 0;
            for (Copy = 0; Copy < Size; ++Copy)
                Block[Copy] = SoundsBegin[Copy];
            Block[Size] = Added;
            ::operator delete(SoundsBegin);
            SoundsCap = Block + NewCapacity;
            SoundsEnd = Block + Size + 1;
            SoundsBegin = Block;
        }
        else
        {
            *SoundsEnd = Added;
            ++SoundsEnd;
        }
    }

    // A voice already playing is rewound; an idle one starts on the archetype.
    Voice->SetPosition(const_cast<Vector*>(Pos));
    Voice->SetPitch(GetClientSoundArchRandomPitch(SoundArch));
    if (Voice->IsActive())
        Voice->SetStartOffset(0);
    else
        Voice->Start(SoundArch, 0);
}

void FClientGameobj::SpawnSurfaceHitEffect(unsigned SourceId, FEffectArch* EffectArch,
                                           Vector const* Pos, Vector const* Normal,
                                           void* CompanionOut)
{
    // FUN_00539f90. CompanionOut is never read.
    (void)CompanionOut;
    if (static_cast<void*>(this) == GetPlayerObjRW() &&
        GetGameOption(kGameOptSurfaceHitEffects) == 0)
        return;
    if (EffectArch == nullptr)
        return;

    // No VisEffect -> straight to the sound.
    if (EffectArch->VisEffect != nullptr)
    {
        // No null test on the allocation.
        FParticleEffect* const Effect = ParticleEffectCreate();
// FUN_004f8160 — vtable[0] start(EffectArch*).
        if (Effect->FParticleEffect::Start(EffectArch) == 0)
        {
            Effect->Release();
        }
        else
        {
            // The slot: the first empty or dying (Flags08 bit 1) one, else the one whose +0x78
            // is lowest strictly below 100.0f, else slot 0.
            int Chosen = 0;
            float Best = kHitEffectReplaceCeiling;
            int Index = 0;
            for (Index = 0; Index < kHitEffectSlotCount; ++Index)
            {
                FParticleEffect* const Slot = HitEffects[Index];
                if (Slot == nullptr || (Slot->Flags08 & kEffectFlagDeactivated) != 0)
                {
                    Chosen = Index;
                    break;
                }
                if (Slot->RunTime78 < Best)
                {
                    Best = Slot->RunTime78;
                    Chosen = Index;
                }
            }
            // Every HitEffects entry is a base ParticleEffect made right here, so its vt+4 is
            // LAB_004f84d0.
            if (HitEffects[Chosen] != nullptr)
            {
                HitEffects[Chosen]->CleanupAlchemy();
                HitEffects[Chosen]->FParticleEffect::Kill();
                HitEffects[Chosen]->Release();
                HitEffects[Chosen] = nullptr;
            }
            HitEffects[Chosen] = Effect;

            // Into object space (DAT_006789d0's math engine); no null test on the object.
            Transform const& ObjXform = Object->get_transform();
            Vector LocalPos;
            GameobjMathEngine()->InverseTransform(LocalPos, ObjXform,
                                                  *Pos);
            Vector LocalNormal;
            GameobjMathEngine()->InverseRotate(LocalNormal, ObjXform,
                                               *Normal);
            Transform AttachXform;
            BuildHitEffectTransform(&AttachXform, Object, LocalNormal, LocalPos);
            Effect->Attach(this, static_cast<int>(get_index()), &AttachXform);
        }
    }

    if (EffectArch->SndEffect != nullptr)
        PlayHitSound(SourceId, EffectArch->SndEffect, Pos);
}

void FClientGameobj::SpawnSplashHitEffect(FEffectArch* EffectArch, Vector const* Pos,
                                          Vector const* Normal, Vector const* Velocity,
                                          void* CompanionOut)
{
    // FUN_0053a230
    if (static_cast<void*>(this) == GetPlayerObjRW() &&
        GetGameOption(kGameOptSplashHitEffects) == 0)
        return;
    if (EffectArch == nullptr)
        return;
    if (!EffectTypeWithinPerf(EffectArch->EffectType))
        return;

    // Into object space. The object-space velocity is computed and never read again (retail
    // overwrites its buffer).
    Transform const& ObjXform = Object->get_transform();
    Vector LocalPos;
    GameobjMathEngine()->InverseTransform(LocalPos, ObjXform,
                                          *Pos);
    Vector LocalNormal;
    GameobjMathEngine()->InverseRotate(LocalNormal, ObjXform,
                                       *Normal);
    Vector LocalVelocity;
    GameobjMathEngine()->InverseRotate(LocalVelocity, ObjXform,
                                       *Velocity);

    // The negated WORLD velocity is reflected off the object-space normal (retail mixes the
    // two spaces).
    Vector Incoming;
    Incoming.x = -Velocity->x;
    Incoming.y = -Velocity->y;
    Incoming.z = -Velocity->z;
    float const Scale = (LocalNormal.z * Incoming.z + LocalNormal.y * Incoming.y +
                         LocalNormal.x * Incoming.x) * 2.0f;
    Vector Direction;
    Direction.x = LocalNormal.x * Scale - Incoming.x;
    Direction.y = LocalNormal.y * Scale - Incoming.y;
    Direction.z = LocalNormal.z * Scale - Incoming.z;

    Transform EffectXform;
    BuildHitEffectTransform(&EffectXform, Object, Direction, LocalPos);
    CliCreateEffectOneShot(EffectArch, this, static_cast<int>(get_index()), &EffectXform,
                           CompanionOut);
}

void FClientGameobj::UpdateNebula(float Dt)
{
    // LAB_0053e4a0: empty.
    (void)Dt;
}

int FClientGameobj::GetCachedAttitude()
{
    // FUN_0053a960
    int const Countdown = AttitudeCountdown;
    if (Countdown > 0)
    {
        AttitudeCountdown = Countdown - 1;
        return CachedAttitude;
    }
    // 8 when the countdown was exactly 0, 1 when it was negative (the ctor's -1: the first
    // call refreshes again soon).
    AttitudeCountdown = (Countdown >= 0) ? 8 : 1;
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    if (Player == nullptr)
    {
        CachedAttitude = 0;
        return CachedAttitude;
    }
    CachedAttitude = GetAttitudeClass(Player, this); // FUN_0045a490
    return CachedAttitude;
}

bool FClientGameobj::GetFlag3IfSurfaceWidthUnchanged() const
{
    // FUN_0053a9f0
    if (static_cast<unsigned>(HudLabelSurfaceWidth) != G_RenderSurfaceWidth)
        return false;
    return ((Flags >> 3) & 1u) != 0;
}

void FClientGameobj::SetFlag3(bool bValue)
{
    // FUN_0053aa10
    Flags = static_cast<unsigned char>((Flags & ~8u) | ((static_cast<unsigned>(bValue) << 3) & 8u));
    if ((Flags & 8u) != 0)
        HudLabelSurfaceWidth = static_cast<int>(G_RenderSurfaceWidth);
}

// IObjAffect -- FClientGameobj's own table, .rdata:005de76c. Each entry is a retail
// one-instruction error stub (-3, -8 or 0; the stub's address is on each line) -- the same
// pattern as Server.dll GameObject's .rdata:06d6576c. Simple (005e0024) and Solar (005e0284)
// keep every entry; EquippedObj and Ship override some.
int FClientGameobj::set_throttle(float) { return -3; }                                   // 0053a950
int FClientGameobj::set_axis_throttle(Vector const&) { return -3; }                      // 0053a950
int FClientGameobj::set_nudge_vec(Vector const&) { return -3; }                          // 0053a950
int FClientGameobj::set_strafe_dir(StrafeDir) { return -3; }                             // 0053a950
int FClientGameobj::set_formation_leader(IObjRW*) { return -8; }                         // 0053a900
int FClientGameobj::set_formation_offset(Vector const&) { return -8; }                   // 0053a900
int FClientGameobj::add_formation_follower(IObjRW*) { return -8; }                       // 0053a900
int FClientGameobj::remove_formation_follower(IObjRW*) { return -8; }                    // 0053a900
int FClientGameobj::get_formation_follower_count() { return 0; }                         // 004b5010
int FClientGameobj::set_follow_leader(IObjRW*) { return -8; }                            // 0053a900
int FClientGameobj::set_follow_offset(Vector const&) { return -8; }                      // 0053a900
int FClientGameobj::add_follow_follower(IObjRW*) { return -8; }                          // 0053a900
int FClientGameobj::remove_follow_follower(IObjRW*) { return -8; }                       // 0053a900
int FClientGameobj::fire_weapons(unsigned short const*, unsigned, Vector const*, int) { return -3; }  // 0053a920
int FClientGameobj::jettison_cargo(unsigned short, int, int) { return -3; }                     // 0053a910
int FClientGameobj::tractor_all(unsigned short const*, std::vector<IObjRW*> const&, int) { return -3; }  // 0053a910
int FClientGameobj::tractor_single(unsigned short const*, IObjRW*, int) { return -3; }          // 0053a910
int FClientGameobj::set_gun_target(unsigned short const*, IObjRW*, unsigned short, int) { return -3; }  // 0053a920
int FClientGameobj::set_target(IObjRW*, unsigned short, int) { return -3; }              // 0053a910
int FClientGameobj::toggle_item(unsigned short const*, bool, int) { return -3; }             // 0053a910
int FClientGameobj::toggle_cruise(bool, bool, int) { return -3; }                              // 0053a910
int FClientGameobj::toggle_thrusters(bool, int) { return -3; }                                    // 0053a940
int FClientGameobj::use_item(unsigned short, unsigned, int) { return -3; }                // 0053a910
int FClientGameobj::request_event(int, unsigned const*, unsigned const*, int, RequestCB*) { return -3; }  // 0053a8f0
int FClientGameobj::request_cancel(int, unsigned const*, int) { return -3; }                    // 0053a910
int FClientGameobj::go_tradelane(IObjInspect const*, IObjInspect const*, bool, float) { return -3; }  // 0053a920
int FClientGameobj::stop_tradelane() { return -3; }                                      // 0053a930
int FClientGameobj::slot_6c(int, int) { return -3; }                                     // 0053a940
int FClientGameobj::launch_complete(unsigned) { return -3; }                             // 0053a950
int FClientGameobj::jump_in_complete(unsigned) { return -3; }                            // 0053a950

// ===========================================================================
// Simple
// ===========================================================================

FClientSimpleGameobj::FClientSimpleGameobj(EClientGameobjKind Kind)
    : FClientGameobj(Kind)
{
    // FUN_00547440
    bWatchListed = 0;
}

FClientSimpleGameobj::~FClientSimpleGameobj()
{
    // FUN_00547470 -> thunk_FUN_005394b0: nothing of Simple's own.
}

void FClientSimpleGameobj::ApplyDamageList(DamageList const& Damage)
{
    // FUN_005474a0: multiplayer only. Every entry of the list whose sub-object is the root
    // sets the CSimple's hit points; no null test on the narrowed object.
    if (SinglePlayer())
        return;
    for (std::list<DamageEntry>::const_iterator It = Damage.Entries.begin();
         It != Damage.Entries.end(); ++It)
    {
        if (It->SubObjId != SUBOBJ_ID_ROOT)
            continue;
        static_cast<CSimple*>(NarrowObject(this, CObject::ClassSimple))->set_hit_pts(It->HitPts);
    }
}

void FClientSimpleGameobj::Destroy(int DestroyType)
{
    // FUN_00547500: no null test on the narrowed object.
    static_cast<CSimple*>(NarrowObject(this, CObject::ClassSimple))->set_hit_pts(0.0f);
    FClientGameobj::Destroy(DestroyType);
}

unsigned FClientSimpleGameobj::GetTypeBits() const
{
    // LAB_00547530: the CSimple's +0xe0, no null test.
    return static_cast<CSimple const*>(NarrowObject(this, CObject::ClassSimple))->TypeBits;
}

void FClientSimpleGameobj::SetWatchListed(int Unused, int bWatch)
{
    // FUN_00547550: the first argument is never read.
    (void)Unused;
    bool const bOn = (bWatch == 1);
    bWatchListed = bOn ? 1 : 0;
    if (bOn)
        GameobjWatchListAdd(this);
    else
        GameobjWatchListRemove(this);
}

// ===========================================================================
// EquippedObj
// ===========================================================================

namespace
{
    // The 0xc-byte node of the equip-FX list.
    struct FEquipFxNode
    {
        FEquipFxNode* Next;
        FEquipFxNode* Prev;
        INotify* Wrapper;
    };
    static_assert(sizeof(FEquipFxNode) == 0xc, "equip-FX node 0xc");
}

FEquippedFuseList::FEquippedFuseList(unsigned char InAllocatorByte)
{
    // Inlined in FUN_00532bc0. Only the head's links are written; its ClientFuse payload stays
    // raw memory.
    AllocatorByte = InAllocatorByte;
    FFuseBurnNode* const NewHead = static_cast<FFuseBurnNode*>(::operator new(sizeof(FFuseBurnNode)));
    NewHead->Next = NewHead;
    NewHead->Prev = NewHead;
    Head = NewHead;
    Count = 0;
}

FEquippedFuseList::~FEquippedFuseList()
{
    // FUN_005359c0. Retail calls the fuse's destructor (vt+0 with 0) and frees the node;
    // `delete Node` does the same two steps.
    FFuseBurnNode* const ListHead = static_cast<FFuseBurnNode*>(Head);
    FFuseBurnNode* Node = ListHead->Next;
    while (Node != ListHead)
    {
        FFuseBurnNode* const Next = Node->Next;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        delete Node;
        Count = Count - 1;
        Node = Next;
    }
    ::operator delete(Head);
    Head = nullptr;
    Count = 0;
}

FEquipFxList::FEquipFxList(unsigned char InAllocatorByte)
{
    // Inlined in FUN_00532bc0.
    AllocatorByte = InAllocatorByte;
    FEquipFxNode* const NewHead = static_cast<FEquipFxNode*>(::operator new(sizeof(FEquipFxNode)));
    NewHead->Next = NewHead;
    NewHead->Prev = NewHead;
    Head = NewHead;
    Count = 0;
}

FEquipFxList::~FEquipFxList()
{
    // FUN_004a2d30, the shared pointer-list destructor: the wrappers are not touched.
    FEquipFxNode* const ListHead = static_cast<FEquipFxNode*>(Head);
    FEquipFxNode* Node = ListHead->Next;
    while (Node != ListHead)
    {
        FEquipFxNode* const Next = Node->Next;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        ::operator delete(Node);
        Count = Count - 1;
        Node = Next;
    }
    ::operator delete(Head);
    Head = nullptr;
    Count = 0;
}

FClientEquippedObj::FClientEquippedObj(EClientGameobjKind Kind)
    : FClientSimpleGameobj(Kind)
    // EH state 0 -> 1 (unwind FUN_004a2d30).
    , EquipFx(static_cast<unsigned char>(static_cast<int>(Kind)))
    // EH state 1 -> 2 (unwind FUN_005359c0).
    , Fuses(static_cast<unsigned char>(static_cast<int>(Kind)))
{
    // FUN_00532bc0. Retail's flag store sits before the two list builds; nobody reads the
    // byte while they run.
    EquipFlags = static_cast<unsigned char>(EquipFlags & 0xfeu);

    // The sub-target map at +0xcc is SubTargets' own constructor in the port.
    bCloakOverrideActive = 0;
}

FClientEquippedObj::~FClientEquippedObj()
{
    // FUN_00532d40 (this = the CBase sub-object). The equip-FX wrappers are deleted first
    // (FUN_00533080's body, client_equip_fx.cpp).
    ClearEquipFx();

    // No null test on the narrowed object.
    CEqObj* const EqObj = static_cast<CEqObj*>(NarrowObject(this, CObject::ClassEqObj));
    // Every fuse of the archetype's fuse list (arch +0x78; the port's Archetype::EqObj keeps a
    // vector* there, null for an empty list) found in the fuse DB is UnLoad'ed.
    Archetype::EqObj const* const EqArch = static_cast<Archetype::EqObj const*>(EqObj->Archetype);
    if (EqArch->Fuses != nullptr)
    {
        std::vector<Archetype::FGroupFuse> const& ArchFuses = *EqArch->Fuses;
        size_t FuseIndex = 0;
        for (FuseIndex = 0; FuseIndex < ArchFuses.size(); ++FuseIndex)
        {
            ClientFuse* const Proto = ClientFuseDB::Find(ArchFuses[FuseIndex].Fuse.Id);
            if (Proto != nullptr)
                Proto->UnLoad();
        }
    }
    // The same for every archgroup's fuse ignitions.
    CArchGrpTraverser GroupCursor;
    CArchGroup* Group = EqObj->ArchGroups.Traverse(GroupCursor);
    while (Group != nullptr)
    {
        Archetype::FuseIgnitionList const& Ignitions = Group->FuseIgnitions();
        if (Ignitions.Items != nullptr)
        {
            std::vector<Archetype::FGroupFuse> const& GroupFuses = *Ignitions.Items;
            size_t GroupFuseIndex = 0;
            for (GroupFuseIndex = 0; GroupFuseIndex < GroupFuses.size(); ++GroupFuseIndex)
            {
                ClientFuse* const Proto = ClientFuseDB::Find(GroupFuses[GroupFuseIndex].Fuse.Id);
                if (Proto != nullptr)
                    Proto->UnLoad();
            }
        }
        Group = EqObj->ArchGroups.Traverse(GroupCursor);
    }

    // Emptied here (FUN_00535ad0), in retail's order; SubTargets' own destructor frees the
    // rest after the body.
    SubTargets.clear();

    // Retail then inlines the fuse list's and the equip-FX list's destructors: here
    // ~FEquippedFuseList (+0xc0) and ~FEquipFxList (+0xb4) run as members after ~SubTargets
    // (+0xcc), in that order, then GameObject's destructor FUN_005394b0.
}

void FClientEquippedObj::notify(Event Code, void* Param)
{
    // FUN_00534730 (INotify slot 0 on the +0xac sub-object).
    switch (static_cast<int>(Code))
    {
    case INotify::EquipSeparated:
    {
        // An attached part came off. Its debris object is taken over before the slot-85
        // test; AttachedEquipArch is fetched for SeparationExplosion first.
        CAttachedEquip* const Attached = static_cast<CAttachedEquip*>(Param);
        CObject* const PartObject = Attached->RetrieveDebrisObject();
        if (IsExploding())
        {
            SpawnPartDebris(PartObject, &Attached->AttachedEquipArch()->DebrisType, nullptr);
        }
        else
        {
            ID_String const* const Explosion = &Attached->AttachedEquipArch()->SeparationExplosion;
            SpawnPartDebris(PartObject, &Attached->AttachedEquipArch()->DebrisType, Explosion);
        }
        break;
    }
    case INotify::EquipDetachedInPlace:
        // The part's effects stop (FUN_004fd230, no new owner).
        CliEffectListDetachPart(&EffectList,
                                static_cast<CAttachedEquip*>(Param)->GetRootIndex(), nullptr);
        break;
    case INotify::ArchGroupSeparated:
    {
        // A CArchGroup was destroyed; as EquipSeparated (ExplosionTypeName is fetched first).
        CArchGroup* const Group = static_cast<CArchGroup*>(Param);
        CObject* const PartObject = Group->RetreiveDestroyedObj();
        if (IsExploding())
        {
            SpawnPartDebris(PartObject, &Group->DebrisTypeName(), nullptr);
        }
        else
        {
            ID_String const* const Explosion = &Group->ExplosionTypeName();
            SpawnPartDebris(PartObject, &Group->DebrisTypeName(), Explosion);
        }
        break;
    }
    case INotify::ArchGroupDetachedInPlace:
        CliEffectListDetachPart(&EffectList, static_cast<CArchGroup*>(Param)->GetRootIndex(),
                                nullptr);
        break;
    default:
        break;
    }
}

void FClientEquippedObj::SpawnPartDebris(CObject* PartObject, ID_String const* DebrisType,
                                         ID_String const* Explosion)
{
    // FUN_00533d10. A missing [Debris] record warns and takes the default.
    FDebrisArch* DebrisArch = FindDebrisArch(DebrisType);
    if (DebrisArch == nullptr)
    {
        char const* const ArchName = PartObject->get_archetype()->Nickname.get_string();
        char const* const TypeName = DebrisType->get_string();
        FDUMP(kFdumpWarning,
              "%s(%d) : *** WARNING: ID(0x%X) Exploding: Couldn't find debris_type[%s] with "
              "archetype[%s], using default.",
              kEquippedObjSourceFile, 0x1e8, get_id(), TypeName, ArchName);
        DebrisArch = GetDefaultDebrisArch();
    }

    // The part's effects move to the debris.
    FClientDebrisGameobj* const Debris = CreateDebrisGameobjForObject(PartObject, DebrisArch);
    CliEffectListDetachPart(&EffectList, PartObject->EngineInstanceId, Debris);

    // A named explosion starts on the Debris gameobj at this Gameobj's position; an unknown
    // one warns.
    if (Explosion == nullptr || Explosion->IsEmpty())
        return;
    Archetype::Explosion* const Expl = Archetype::GetExplosion(*Explosion);
    if (Expl != nullptr)
    {
        Debris->Actions.add(Action_Explode::Create(Expl, Debris, &get_position()));
        return;
    }
    FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: Couldn't find separation_explosion[%s], skipping.",
          kEquippedObjSourceFile, 0x202, Explosion->get_string());
}

void FClientEquippedObj::PostCreateInit()
{
    // FUN_00533e10 -- ported as FClientGameobj::RebuildEquipFx (client_equip_fx.cpp).
    RebuildEquipFx();
}

void FClientEquippedObj::update(float Dt)
{
    // FUN_00534010. Also called directly by the ship and solar updates.
    FClientGameobj::update(Dt);

    // The equip-FX list (FEquipFxListNode, client_equip_fx.h), the head re-read at every loop
    // test. A wrapper whose Update answers false is deleted and its node freed; the walk goes
    // on from the old Next.
    FEquipFxListNode* Node = static_cast<FEquipFxListNode*>(EquipFx.Head)->Next;
    while (Node != static_cast<FEquipFxListNode*>(EquipFx.Head))
    {
        if (Node->Wrapper->Update(Dt))
        {
            Node = Node->Next;
            continue;
        }
        delete Node->Wrapper;
        FEquipFxListNode* const Next = Node->Next;
        Node->Prev->Next = Next;
        Next->Prev = Node->Prev;
        ::operator delete(Node);
        EquipFx.Count = EquipFx.Count - 1;
        Node = Next;
    }

    FFuseBurnNode* const FuseListHead = static_cast<FFuseBurnNode*>(Fuses.Head);
    for (FFuseBurnNode* Node = FuseListHead->Next; Node != FuseListHead; Node = Node->Next)
        Node->Live.Burn(Dt);
}

unsigned* FClientEquippedObj::PickLod(unsigned* OutLod, void* Camera, unsigned const* CachedLod)
{
    // FUN_00533710. The narrowed CEqObj is read with no null test. A fully cloaked object
    // (>= 1.0f; below and unordered draw) gets LOD {1, 0xffff}, i.e. not drawn.
    CEqObj const* const EqObj = static_cast<CEqObj const*>(NarrowObject(this, CObject::ClassEqObj));
    if (EqObj->CloakPercent >= 1.0f)
    {
        *OutLod = 0xffff0001u;
        return OutLod;
    }
    return FClientGameobj::PickLod(OutLod, Camera, CachedLod);
}

void FClientEquippedObj::PreSubmitDraw()
{
    // LAB_0051b480: a jump to the empty LAB_00401e90 -- the same body as GameObject's slot 96.
}

void FClientEquippedObj::ApplyDamageList(DamageList const& Damage)
{
    // FUN_005339e0: multiplayer only. Each arm narrows the object by 0x103 again and uses it
    // with no null test.
    if (!SinglePlayer())
    {
        for (std::list<DamageEntry>::const_iterator It = Damage.Entries.begin();
             It != Damage.Entries.end(); ++It)
        {
            DamageEntry const& Entry = *It;
            if (Entry.SubObjId == SUBOBJ_ID_ROOT)
            {
                static_cast<CEqObj*>(NarrowObject(this, CObject::ClassEqObj))->set_hit_pts(Entry.HitPts);
            }
            else if (Entry.SubObjId == SUBOBJ_ID_POWER)
            {
                // Written directly (+0x198).
                static_cast<CEqObj*>(NarrowObject(this, CObject::ClassEqObj))->Power = Entry.HitPts;
            }
            else if (SubObjectID::IsEquipID(Entry.SubObjId))
            {
                // No equipment with that id -> the next entry.
                CEqObj* const EqObj = static_cast<CEqObj*>(NarrowObject(this, CObject::ClassEqObj));
                CEquip* const Equip = EqObj->EquipManager.FindByID(Entry.SubObjId);
                if (Equip == nullptr)
                    continue;
                if (static_cast<int>(Damage.Cause) == 0x13)
                {
                    // Cause 0x13: no type test before the shield cast.
                    static_cast<CEShield*>(Equip)->ShieldBatteryBoost(Entry.HitPts);
                }
                else if (Equip->Type == EquipTypeCargo &&
                         static_cast<CECargo*>(Equip)->Count > 1u && Entry.HitPts == 0.0f)
                {
                    // Stacked cargo whose entry is exactly 0 loses one unit.
                    static_cast<CECargo*>(Equip)->RemoveFromStack(1u);
                }
                else
                {
                    Equip->SetHitPoints(Entry.HitPts);
                }
            }
            else
            {
                // A collision group.
                static_cast<CEqObj*>(NarrowObject(this, CObject::ClassEqObj))
                    ->set_sub_obj_hit_pts(Entry.SubObjId, Entry.HitPts);
            }
        }
    }

    // In single player too.
    static_cast<CEqObj*>(NarrowObject(this, CObject::ClassEqObj))->destroy_sub_objs(Damage, true);
    static_cast<CEqObj*>(NarrowObject(this, CObject::ClassEqObj))->EquipManager.CleanUp();
}

void FClientEquippedObj::OnSurfaceCollisionHit(void const* Contact, void const* B, FClientGameobj* Other,
                                 void const* D)
{
    // FUN_005340b0. B is the hit sub-object id (its low word).
    unsigned short const SubObj = static_cast<unsigned short>(reinterpret_cast<uintptr_t>(B));
    if (SubObjectID::IsShieldEquipID(SubObj))
    {
        // FUN_00533100 inlined.
        FClientEquip* const Wrapper = FindEquipFx(SubObj);
        // A shield wrapper (equipment type 0x100; the equipment is not null-tested).
        if (Wrapper != nullptr && Wrapper->Equip->Type == EquipTypeShield)
        {
            // Fetched and dropped -- leftovers of the inlined body.
            (void)get_velocity();
            (void)Other->get_velocity();
            (void)Other->get_mass();
            CliShieldEquipFxCollisionHit(Wrapper, Contact, Other, D);
            return;
        }
    }
    FClientGameobj::OnSurfaceCollisionHit(Contact, B, Other, D);
}

void FClientEquippedObj::OnMunitionHit(void const* A, void const* B, FClientGameobj* Munition)
{
    // FUN_00534190. B is the hit sub-object id (its low word).
    unsigned short const SubObj = static_cast<unsigned short>(reinterpret_cast<uintptr_t>(B));
    if (SubObjectID::IsShieldEquipID(SubObj))
    {
        // FUN_00533100 inlined, as in slot 104.
        FClientEquip* const Wrapper = FindEquipFx(SubObj);
        if (Wrapper != nullptr && Wrapper->Equip->Type == EquipTypeShield)
        {
            CliShieldEquipFxMunitionHit(Wrapper, A, Munition);
            return;
        }
    }
    FClientGameobj::OnMunitionHit(A, B, Munition);
}

void FClientEquippedObj::SetSubTarget(unsigned short SubObj, unsigned TargetId, unsigned short TargetSubObj)
{
    // LAB_005356c0: the map's operator[] (insert FUN_00535dd0).
    FEquipSubTarget& Entry = SubTargets[SubObj];
    Entry.TargetId = TargetId;
    Entry.TargetSubObj = TargetSubObj;
}

void FClientEquippedObj::ApplySubTarget(unsigned short SubObj)
{
    // FUN_005357f0. Insert without replace (FUN_00535b80).
    std::pair<std::map<unsigned short, FEquipSubTarget>::iterator, bool> const Inserted =
        SubTargets.insert(std::make_pair(SubObj, FEquipSubTarget()));
    FEquipSubTarget const& Entry = Inserted.first->second;

    if (Entry.TargetId == 0)
        return;
    FClientGameobj* const Target = ClientObjDb_FindObject(Entry.TargetId, 0);
    if (Target == nullptr)
        return;

    if (SubObj == SUBOBJ_ID_NONE)
    {
        // Unchanged when this object already targets the same object on the same sub-object.
        IObjRW* Current = nullptr;
        unsigned short CurrentSub = SUBOBJ_ID_NONE;
        get_target(Current);
        get_subtarget(CurrentSub);
        if (Current != nullptr)
        {
            unsigned const CurrentId = Current->get_id();
            unsigned const TargetGoId = Target->get_id();
            if (CurrentId == TargetGoId && CurrentSub == Entry.TargetSubObj)
                return;
        }
    }
    else
    {
        // The gun at SubObj (no null test on the narrowed object); unchanged when it already
        // targets the Gameobj's object on the same sub-object.
        CEqObj* const EqObj = static_cast<CEqObj*>(NarrowObject(this, CObject::ClassEqObj));
        CEquip* const Equip = EqObj->EquipManager.FindByID(SubObj);
        if (Equip == nullptr)
            return;
        CEGun* const Gun = CEGun::cast(Equip);
        if (Gun == nullptr)
            return;
        CSimple* GunTarget = nullptr;
        unsigned short GunSub = SUBOBJ_ID_NONE;
        Gun->GetTarget(GunTarget, GunSub);
        if (GunTarget != nullptr)
        {
            unsigned const TargetGoId = Target->get_id();
            if (GunTarget->Id == TargetGoId && GunSub == Entry.TargetSubObj)
                return;
        }
    }

    // IObjAffect slot 17.
    static_cast<IObjAffect*>(this)->set_gun_target(&SubObj, static_cast<IObjRW*>(Target),
                                            Entry.TargetSubObj, 0);
}

void FClientEquippedObj::Explode(Vector const* Pos)
{
    // FUN_00533be0 -- the explosion of an equipped object. Every attached piece that is not a
    // shield (type 0x100) separates; every piece is then destroyed. No null test on the
    // narrowed object.
    CEquipTraverser Walk;
    for (;;)
    {
        CEqObj* const EqObj = static_cast<CEqObj*>(NarrowObject(this, CObject::ClassEqObj));
        CEquip* const Equip = EqObj->EquipManager.Traverse(Walk);
        if (Equip == nullptr)
            break;
        CAttachedEquip* const Attached = CAttachedEquip::cast(Equip);
        if (Attached != nullptr && Equip->Type != EquipTypeShield)
            Attached->SetFate(DamageEntry::SubObjFate::Separated);
        Equip->Destroy();
    }

    // Every collision group not already destroyed (State 3) is killed.
    CArchGrpTraverser Groups;
    for (;;)
    {
        CEqObj* const EqObj = static_cast<CEqObj*>(NarrowObject(this, CObject::ClassEqObj));
        CArchGroup* const Group = EqObj->ArchGroups.Traverse(Groups);
        if (Group == nullptr)
            break;
        if (Group->State == CArchGroup::STATE_DESTROYED)
            continue;
        Group->Kill(DamageEntry::SubObjFate::Destroyed);
    }

    static_cast<CEqObj*>(NarrowObject(this, CObject::ClassEqObj))->ArchGroups.DestroyDeadGroups();
    FClientGameobj::Explode(Pos);
}

bool FClientEquippedObj::UnLightFuse(ID_String const* FuseId, unsigned short SubObjId, float AtT)
{
    // FUN_005343d0
    FFuseBurnNode* const Sentinel = static_cast<FFuseBurnNode*>(Fuses.Head);
    bool bFound = false;
    for (FFuseBurnNode* Node = Sentinel->Next; Node != Sentinel && !bFound; Node = Node->Next)
    {
        if (Node->Live.Name().Id == FuseId->Id)
        {
            bFound = true;
            Node->Live.UnBurn(SubObjId, AtT);
        }
    }
    return bFound;
}

bool FClientEquippedObj::IsDeathFuseBurning()
{
    // FUN_00534420
    FFuseBurnNode* const Sentinel = static_cast<FFuseBurnNode*>(Fuses.Head);
    for (FFuseBurnNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
    {
        if (Node->Live.IsDeathFuse() && !Node->Live.IsSpent())
            return true;
    }
    return false;
}

bool FClientEquippedObj::is_dying() const
{
    // LAB_00534860: a tail jump through this object's own vtable +0x1c8 (slot 114).
    return const_cast<FClientEquippedObj*>(this)->IsDeathFuseBurning();
}

int FClientEquippedObj::scan_cargo(IObjRW* Scanned, EquipDescVector& OutCargo) const
{
    // LAB_00535650. The base result is returned unchanged.
    int const Result = IObjInspectImpl::scan_cargo(Scanned, OutCargo);
    if (IsPlayerShipId(get_id()))
    {
        // The scanned id is read first.
        unsigned const ScannedId = Scanned->get_id();
        unsigned const SelfId = get_id();
        LocalServer()->SPScanCargo(SelfId, ScannedId, GetPlayerConnectionId());
    }
    return Result;
}

int FClientEquippedObj::fire_weapons(unsigned short const* EquipIds, unsigned Count,
                                Vector const* TargetPos, int SourceClient)
{
    // FUN_00534870 -- ported as CliShipFireWeapons (cli_projectile_fx.cpp), which takes the
    // IObjAffect sub-object (this + 4) as retail's adjustor this.
    return static_cast<int>(CliShipFireWeapons(static_cast<IObjAffect*>(this), EquipIds, Count,
                                               TargetPos, SourceClient));
}

namespace
{
    // The tractor slots' target narrowing: the cobject, kept only when its class carries all of
    // 0x803 (CLoot), null otherwise.
    CLoot* TractorLootOf(IObjRW* Target)
    {
        CObject* const Obj = const_cast<CObject*>(Target->cobject());
        if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & CObject::ClassLoot) != CObject::ClassLoot)
            return nullptr;
        return static_cast<CLoot*>(Obj);
    }

    // The IObjAffect slots' "local request" test: SourceClient -1, or the player's own
    // connection (DAT_00673344).
    bool IsLocalRequest(int SourceClient)
    {
        return SourceClient == -1 ||
               static_cast<unsigned>(SourceClient) == GetPlayerConnectionId();
    }
}

int FClientEquippedObj::tractor_all(unsigned short const* EquipId,
                                std::vector<IObjRW*> const& Targets, int SourceClient)
{
    // LAB_00535010 (on the IObjAffect sub-object; Ghidra left the body as data).
    const bool bLocal = IsLocalRequest(SourceClient);
    if (SinglePlayer() && !bLocal)
        return -5;

    // No null test on the narrowed object.
    CEqObj* const EqObj = static_cast<CEqObj*>(NarrowObject(this, CObject::ClassEqObj));
    CEquip* const Equip = EqObj->EquipManager.FindByID(*EquipId);
    if (Equip == nullptr)
        return -2;

    // The targets are not null-tested; the equipment is used as found -- no cast, no
    // destroyed test.
    CETractor* const Tractor = static_cast<CETractor*>(Equip);
    int Accepted = 0;
    for (std::vector<IObjRW*>::const_iterator It = Targets.begin(); It != Targets.end(); ++It)
    {
        if (Tractor->AddTarget(TractorLootOf(*It)) == TractorOk)
            Accepted = Accepted + 1;
    }
    if (Accepted <= 0)
        return -4;

    if (!bLocal)
        return 0;

    // The server hears the id of EVERY target, accepted or not (IServer slot 5).
    XTractorObjects Info;
    Info.ObjectId = get_id();
    Info.TractorEquipId = *EquipId;
    Info.Pad_0x06 = 0;
    for (std::vector<IObjRW*>::const_iterator It = Targets.begin(); It != Targets.end(); ++It)
        Info.TargetIds.push_back((*It)->get_id());
    LocalServer()->TractorObjects(GetPlayerConnectionId(), Info);
    return 0;
}

int FClientEquippedObj::tractor_single(unsigned short const* EquipId, IObjRW* Target, int SourceClient)
{
    // FUN_00534e60 (on the IObjAffect sub-object; Ghidra left it undisassembled). The
    // single-target form of slot 15.
    const bool bLocal = IsLocalRequest(SourceClient);
    if (SinglePlayer() && !bLocal)
        return -5;

    if (Target == nullptr)
        return -4;

    CEqObj* const EqObj = static_cast<CEqObj*>(NarrowObject(this, CObject::ClassEqObj));
    CEquip* const Equip = EqObj->EquipManager.FindByID(*EquipId);
    if (Equip == nullptr)
        return -2;

    // The equipment is used as found; any failure code -> -5.
    if (static_cast<CETractor*>(Equip)->AddTarget(TractorLootOf(Target)) != TractorOk)
        return -5;

    if (!bLocal)
        return 0;

    XTractorObjects Info;
    Info.ObjectId = get_id();
    Info.TractorEquipId = *EquipId;
    Info.Pad_0x06 = 0;
    Info.TargetIds.push_back(Target->get_id());
    LocalServer()->TractorObjects(GetPlayerConnectionId(), Info);
    return 0;
}

int FClientEquippedObj::set_gun_target(unsigned short const* EquipId, IObjRW* Target,
                                unsigned short SubTarget, int SourceClient)
{
    // FUN_005351d0 -- ported as CliEquippedObjSetGunTarget (cli_projectile_fx.cpp).
    return CliEquippedObjSetGunTarget(static_cast<IObjAffect*>(this), EquipId, Target, SubTarget,
                                      SourceClient);
}

int FClientEquippedObj::toggle_item(unsigned short const* EquipId, bool bActivate, int SourceClient)
{
    // LAB_00535330 -- ported as CliShipActivateEquip (cli_projectile_fx.cpp); 0 on every path.
    return CliShipActivateEquip(static_cast<IObjAffect*>(this), EquipId,
                                static_cast<unsigned char>(bActivate), SourceClient);
}

int FClientEquippedObj::use_item(unsigned short EquipId, unsigned Count, int SourceClient)
{
    // LAB_00535470 (on the IObjAffect sub-object): use a repair kit / shield battery from the
    // cargo hold. There is no single-player test here.
    if (!IsLocalRequest(SourceClient))
        return -5;

    // No null test on the narrowed object.
    CEqObj* const EqObj = static_cast<CEqObj*>(NarrowObject(this, CObject::ClassEqObj));
    CECargo* const Cargo = CECargo::cast(EqObj->EquipManager.FindByID(EquipId));
    if (Cargo == nullptr)
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s",
              "E:\\FL\\Scratch\\Source\\Client\\Space\\EquippedObj.cpp", 0x540,
              "EqObj::use_item() couldn't find target item on Client request.");
        return -2;
    }

    const unsigned UseCount = (Cargo->Count < Count) ? Cargo->Count : Count;

    const Archetype::AClassType ClassType = Cargo->Arch->get_class_type();
    if (ClassType == Archetype::AClassRepairKit)
    {
        // Only while the hull is below full (equal, above or unordered skip).
        CEqObj* const Hull = static_cast<CEqObj*>(NarrowObject(this, CObject::ClassEqObj));
        if (!(Hull->get_relative_health() < 1.0f))
            return 0;
    }
    else if (ClassType == Archetype::AClassShieldBattery)
    {
        // A shield battery only while some shield is below full and has hit points at all.
        // The object is re-narrowed on every step, as retail does.
        CEquipTraverser Walk(static_cast<int>(EquipTypeShield));
        for (;;)
        {
            CEqObj* const Owner = static_cast<CEqObj*>(NarrowObject(this, CObject::ClassEqObj));
            CEquip* const Shield = Owner->EquipManager.Traverse(Walk);
            if (Shield == nullptr)
                return 0;
            if (!(Shield->GetRelativeHealth() < 1.0f))
                continue;
            if (Shield->GetMaxHitPoints() > 0.0f)
                break;
        }
    }
    else
    {
        return 0;
    }

    // IServer slot 56, no null test.
    SSPUseItem Info;
    Info.ObjectId = get_id();
    Info.ItemId = EquipId;
    Info.Count = static_cast<unsigned short>(UseCount);
    LocalServer()->SPRequestUseItem(Info, GetPlayerConnectionId());
    return 0;
}

// ===========================================================================
// Solar
// ===========================================================================

FClientSolarGameobj::FClientSolarGameobj()
    : FClientEquippedObj(EClientGameobjKind::Solar)
{
    // FUN_005475b0. DockScript's buffer past the first char is not written.
    StarSphere = 0;
    DockScript.Length = 0;
    DockScript.Data[0] = '\0';
    bTradeLaneIdle = 1;
    BurnColor[0] = 1.0f;
    BurnColor[1] = 1.0f;
    BurnColor[2] = 1.0f;
    JumpEffectId.clear();
    JumpGateEffect = nullptr;
}

FClientSolarGameobj::~FClientSolarGameobj()
{
    // FUN_00547680. The JumpGateEffect field (dtor FUN_00502e40) is not written back.
    if (JumpGateEffect != nullptr)
        delete static_cast<FJumpGateEffect*>(JumpGateEffect);
    // Disconnect from the ObjDB (DAT_00678a2c) and from this INotify; no null test on the
    // narrowed object.
    if (Object != nullptr)
    {
        CEqObj* const EqObj = static_cast<CEqObj*>(NarrowObject(this, CObject::ClassEqObj));
        EqObj->disconnect(GetClientObjDb());
        static_cast<CEqObj*>(NarrowObject(this, CObject::ClassEqObj))->disconnect(static_cast<INotify*>(this));
    }
}

// FClientSolarGameobj::notify (FUN_005478f0) is defined after the ship's, next to the
// createeffect helpers both use.

void FClientSolarGameobj::create(Archetype::Root* Arch)
{
    // FUN_005477d0
    CreateOpenConnect(this, CObject::ClassSolar, Arch);
}

bool FClientSolarGameobj::find(unsigned const& Id)
{
    // FUN_00547760
    return FindAndConnect(this, CObject::ClassSolar, Id);
}

void FClientSolarGameobj::PostCreateInit()
{
    // FUN_00547850: then the nebula at the position into +0x38, its fog factor into +0x3c
    // (FUN_005553b0).
    FClientEquippedObj::PostCreateInit();
    FNebulaEffect* const Nebula =
        StarSysFindNebula(&get_position(), &NebulaFogFactor, nullptr);
    CurrentNebula = Nebula;
}

void FClientSolarGameobj::update(float Dt)
{
    // FUN_00547880. In multiplayer the CSolar (no null test) runs the dumb update.
    if (!SinglePlayer())
        NarrowObject(this, CObject::ClassSolar)->update(Dt, CObject::UPDATE_DUMB);

    FClientEquippedObj::update(Dt);

    if (JumpGateEffect != nullptr)
        static_cast<FJumpGateEffect*>(JumpGateEffect)->Update(Dt); // FUN_00502f00

    // The star's slot 2 (LAB_00511690).
    if (StarSphere != nullptr)
    {
        StarSphere->UpdateIntensity(Dt);
    }
}

namespace
{
    // DAT_00678b88 -- solar.cpp's 3DMathEngine, acquired on first use by the radius slot.
    I3DMathEngine* G_SolarMath = nullptr;
}

float FClientSolarGameobj::GetPhysicalRadius(Vector& Center)
{
    // LAB_00548110 -- the solar's radius about Center. A star gives its owner's position and
    // its glow extent.
    if (StarSphere != nullptr)
    {
        Vector StarPos;
        StarSphere->GetOwnerPosition(&StarPos);
        Center = StarPos;
        return StarSphere->MaxGlowScale();
    }

    // A planet / star / moon type (TypeBits & 7; no null test on the narrowed object) -> the
    // engine instance's extent, Center taken to world space.
    CSimple* const Simple = static_cast<CSimple*>(NarrowObject(this, CObject::ClassSolar));
    if ((Simple->TypeBits & (OBJ_MOON | OBJ_PLANET | OBJ_SUN)) != 0)
    {
        float Radius = 0.0f;
        long const Inst = get_index();
        DALib::Engine->GetInstanceExtent(Inst, 0u,
                                         &Radius, &Center);
        Transform const& Xform = reinterpret_cast<Transform const&>(get_orientation());
        if (G_SolarMath == nullptr)
            G_SolarMath = Acquire3DMathEngine();
        Vector World;
        G_SolarMath->TransformXform(World, Xform, Center);
        Center = World;
        return Radius;
    }

    return FClientGameobj::GetPhysicalRadius(Center);
}

unsigned* FClientSolarGameobj::SubmitDraw(unsigned* OutLod, void* Camera, unsigned CachedLod,
                                       Transform const* Xform, void* Extra)
{
    // LAB_00548220 -- the solar draw. Every path that draws nothing returns LOD {0, 0xffff}.
    unsigned Lod = 0xffff0000u;
    CSimple* const Simple = static_cast<CSimple*>(NarrowObject(this, CObject::ClassSolar));

    // A star is drawn only with option 0x24 on, and gives {4, 0xffff}.
    if (StarSphere != nullptr)
    {
        if (GetGameOption(kGameOptSun) == 0)
        {
            *OutLod = Lod;
            return OutLod;
        }
        StarSphere->DrawSimple(Camera);
        *OutLod = 0xffff0004u;
        return OutLod;
    }

    unsigned const TypeBits = Simple->TypeBits;
    bool bSubmit = false;
    if ((TypeBits & (OBJ_MOON | OBJ_PLANET | OBJ_SUN)) != 0)
    {
        // Planet / star / moon -> drawn only with option 0x23 on.
        if (GetGameOption(kGameOptPlanets) == 0)
        {
            *OutLod = Lod;
            return OutLod;
        }
        bSubmit = true;
#ifdef FREELANCER_RP9
        // Port-only: the far bucket draws planets without depth; the Direct3D 9 volumetric
        // nebula ends its rays on their bounding sphere instead (nebula_volume.cpp). Only while
        // it is active.
        if (NebulaVolume::IsActive())
        {
            Vector Centre;
            const float Radius = GetPhysicalRadius(Centre);
            NebulaVolume::NoteOpaqueSphere(&Centre.x, Radius);
        }
#endif
    }
    else if ((TypeBits & OBJ_BLACKHOLE) != 0)
    {
        *OutLod = Lod;
        return OutLod;
    }
    else if (((TypeBits >> 9) & 1u) == 0)
    {
        // Not a waypoint solar (bit 9) -> the ordinary draw.
        bSubmit = true;
    }

    if (bSubmit)
    {
        // EquippedObj's draw FUN_00533780 into a temporary.
        unsigned Temp = 0;
        unsigned* const Result =
            FClientEquippedObj::SubmitDraw(&Temp, Camera, CachedLod, Xform, Extra);
        *OutLod = *Result;
        return OutLod;
    }

    // The waypoint marker -- only while the software cursor is drawn and the HUD_Waypoint
    // exists and is active; its fade is fetched before the active test.
    if (GetClientMouseCursorDrawFlag() == 0)
    {
        *OutLod = Lod;
        return OutLod;
    }
    HUD_Waypoint* const Waypoint = GetHudWaypoint();
    if (Waypoint == nullptr)
    {
        *OutLod = Lod;
        return OutLod;
    }
    bool const bActive = GetHudWaypoint()->IsActive();
    float const Fade = GetHudWaypoint()->GetScale();
    if (!bActive)
    {
        *OutLod = Lod;
        return OutLod;
    }

    // Size: distance beyond 2500 (DAT_005db7d8) * 0.0005 (DAT_005e04d8), capped at 99
    // (DAT_005e04d4).
    unsigned long Color = G_NnPaletteViolet;
    float Size = 1.0f;
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    if (Player != nullptr)
    {
        Vector const& PlayerPos = Player->get_position();
        Vector const& Pos = get_position();
        double const Dx = static_cast<double>(Pos.x) - PlayerPos.x;
        double const Dy = static_cast<double>(Pos.y) - PlayerPos.y;
        double const Dz = static_cast<double>(Pos.z) - PlayerPos.z;
        double const Distance = std::sqrt((Dy * Dy + Dx * Dx) + Dz * Dz);
        if (Distance > 2500.0)
        {
            double const Scaled = Distance * static_cast<double>(0.0005f);
            Size = static_cast<float>(Scaled);
            if (Scaled > 99.0)
                Size = 99.0f;
        }
    }

    // FUN_00433f40's answer is the new LOD {answer, 0xffff}.
    unsigned char ColorBytes[4];
    std::memcpy(ColorBytes, &Color, sizeof(ColorBytes));
    ColorBytes[3] = static_cast<unsigned char>(
        static_cast<long>(static_cast<double>(ColorBytes[3]) * Fade));
    long const Inst = get_index();
    int const Answer = SubmitWin3DMaterialEngineInstance(
        Camera, static_cast<intptr_t>(Inst), 1, ColorBytes, 0, Size, 0,
        0u);
    Lod = 0xffff0000u | static_cast<unsigned short>(Answer);

    // Bit 9 is re-read.
    if (static_cast<unsigned short>(Answer) < 3 ||
        ((static_cast<CSimple*>(NarrowObject(this, CObject::ClassSolar))->TypeBits >> 9) & 1u) == 0)
    {
        *OutLod = Lod;
        return OutLod;
    }

    // The outline pass (FUN_00434630); 255.0f is DAT_005c75d8.
    unsigned char Outline[4];
    Outline[0] = 0xff;
    Outline[1] = 0x80;
    Outline[2] = 0xff;
    Outline[3] = static_cast<unsigned char>(static_cast<long>(static_cast<double>(Fade) * 255.0f));
    long const OutlineInst = get_index();
    SubmitWin3DTintedEngineInstanceWrapped(
        Camera, static_cast<intptr_t>(OutlineInst), 1, Outline, 0, Size,
        0u);
    *OutLod = Lod;
    return OutLod;
}

void FClientSolarGameobj::OnSurfaceCollisionHit(void const* Contact, void const* B, FClientGameobj* Other,
                                  void const* D)
{
    // LAB_00548520: a solar of type bits 0..3 ignores the hit (no null test on the narrowed
    // object).
    if ((static_cast<CSimple*>(NarrowObject(this, CObject::ClassSolar))->TypeBits & (OBJ_MOON | OBJ_PLANET | OBJ_SUN | OBJ_BLACKHOLE)) != 0)
        return;
    FClientEquippedObj::OnSurfaceCollisionHit(Contact, B, Other, D);
}

void FClientSolarGameobj::OnMunitionHit(void const* A, void const* B, FClientGameobj* Munition)
{
    // LAB_00548570: the same test.
    if ((static_cast<CSimple*>(NarrowObject(this, CObject::ClassSolar))->TypeBits & (OBJ_MOON | OBJ_PLANET | OBJ_SUN | OBJ_BLACKHOLE)) != 0)
        return;
    FClientEquippedObj::OnMunitionHit(A, B, Munition);
}

void FClientSolarGameobj::Explode(Vector const* Pos)
{
    // LAB_005485b0
    FClientEquippedObj::Explode(Pos);
}

void FClientSolarGameobj::ReadIni(INI_Reader& Reader)
{
    // FUN_005485c0. The EquippedObj body FUN_00533e10 is called directly (not the solar's own
    // slot 92).
    FClientEquippedObj::PostCreateInit();

    while (Reader.read_value())
    {
        if (Reader.is_value("ring"))
        {
            // A new TexturePanels (FUN_0050bfa0); nothing keeps the pointer but the panels'
            // global list.
            TString<0x20> Zone;
            Zone = Reader.get_value_string(0);
            TString<0x104> File;
            File = Reader.get_value_string(1);
            TexturePanelsCreate()->Load(File.Data, Zone.Data);
        }
        else if (Reader.is_value("burn_color"))
        {
            // Retail tests "burn_color" a second time (always true) and reads value 1, value 0,
            // value 2.
            float const Green = Reader.get_value_float(1);
            float const Red = Reader.get_value_float(0);
            float const Blue = Reader.get_value_float(2);
            float const kInv255 = 1.0f / 255.0f; // DAT_005c7f68
            BurnColor[0] = Red * kInv255;
            BurnColor[1] = Green * kInv255;
            BurnColor[2] = Blue * kInv255;
        }
        else if (Reader.is_value("dock_script"))
        {
            DockScript = Reader.get_value_string();
        }
        else if (Reader.is_value("voice"))
        {
            // No null test on the narrowed object.
            char const* const Voice = Reader.get_value_string();
            CSolar* const Solar = static_cast<CSolar*>(NarrowObject(this, CObject::ClassSolar));
            Solar->VoiceId = CreateID(Voice);
        }
        else if (Reader.is_value("jump_effect"))
        {
            JumpEffectId.Id = CreateID(Reader.get_value_string());
        }
        else if (Reader.is_value("star"))
        {
            // FUN_0050c630 (a listed star).
            StarSphere = StarSphereCreateListed(this);
            char Path[0x104];
            GetFileSysPath(Path, "solar\\stararch.ini", DALib::Data);
            StarSphere->Load(Path, Reader.get_value_string());
        }
    }

    // The nebula into +0x38, its fog factor into +0x3c (FUN_005553b0).
    FNebulaEffect* const Nebula =
        StarSysFindNebula(&get_position(), &NebulaFogFactor, nullptr);
    CurrentNebula = Nebula;
}

// ===========================================================================
// Ship
// ===========================================================================

FClientShipGameobj::FClientShipGameobj()
    : FClientEquippedObj(EClientGameobjKind::Ship)
{
    // FUN_00543380, members in store order.
    BayCloseTimer = 0;
    BayCloseCount = 0;
    PendingRequestCB = nullptr;
    ThrusterSound = nullptr;
    AtmosphereBurnCompanion[0] = nullptr;
    AtmosphereBurnCompanion[1] = nullptr;
    AtmosphereBurnCompanion[2] = nullptr;
    AtmosphereBurnCompanion[3] = nullptr;
    PilotMeshArch = nullptr;
    PilotMeshHolder.Construct();                                         // FUN_00420d40
    PilotTransform.Rotation = Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    PilotTransform.Translation.x = 0.0f;
    PilotTransform.Translation.y = 0.0f;
    PilotTransform.Translation.z = 0.0f;
    FireFailedSoundArch = nullptr;
    FireFailedSound = nullptr;
    FireFailedCooldown = 0;
    bHullBreachWarned = 0;
    bJumpInReady = 0;
    TradeLaneRing = 0;
    int CompanionSlot = 0;
    for (CompanionSlot = 0; CompanionSlot < 4; ++CompanionSlot)
        TradeLaneShipCompanion[CompanionSlot] = nullptr;
    for (CompanionSlot = 0; CompanionSlot < 4; ++CompanionSlot)
        TradeLaneTunnelCompanion[CompanionSlot] = nullptr;
    TradeLaneFlags = 0;
    // Retail clears bits 0..1 and then stores 0 over the whole dword.
    ShipFlags = static_cast<unsigned char>(ShipFlags & ~(kShipFlagHullHit | kShipFlagShieldHit));
    JumpEffect = nullptr;
    ShipFlags = 0;
    Pad18d[0] = 0;
    Pad18d[1] = 0;
    Pad18d[2] = 0;
    JumpState = kJumpStateNone;
    // 1/32768 is DAT_005caeec.
    NebulaUpdateTimer = static_cast<float>(static_cast<double>(rand()) * 3.0517578125e-05);
}

FClientShipGameobj::~FClientShipGameobj()
{
    // FUN_005444a0. It runs with ESI = this + 0xc (the CBase sub-object), so retail's
    // ESI-relative offsets are 0xc below the gameobj offsets used here.
    PilotMeshHolder.Release(); // FUN_00420f70
    if (PilotMeshArch != nullptr)
    {
        PilotMeshArch->Release();
        PilotMeshArch = nullptr;
    }
    // The pointer is left as it is.
    if (FireFailedSoundArch != nullptr)
    {
        FireFailedSoundArch->Release();
    }
    if (FireFailedSound != nullptr)
    {
        FireFailedSound->Stop();
        FireFailedSound->Release();
        FireFailedSound = nullptr;
    }
    if (ThrusterSound != nullptr)
    {
        ThrusterSound->Stop();
        ThrusterSound->Release();
        ThrusterSound = nullptr;
    }
    // Dtor FUN_00503a80.
    if (JumpEffect != nullptr)
    {
        delete static_cast<FJumpShipEffect*>(JumpEffect);
        JumpEffect = nullptr;
    }
    // The ObjDB disconnect happens in single player only for a ship the local player owns (a
    // failed narrow compares the player's own id and so passes), in multiplayer always; the
    // INotify disconnect always. No null test on the narrowed CEqObj.
    if (Object != nullptr)
    {
        bool bDisconnectDb = true;
        if (SinglePlayer())
        {
            CObject* const Owned = NarrowObject(this, CObject::ClassSimple);
            unsigned const Owner = (Owned != nullptr)
                                       ? static_cast<CSimple const*>(Owned)->OwnerPlayer
                                       : GetPlayerConnectionId();
            bDisconnectDb = Owner == GetPlayerConnectionId();
        }
        if (bDisconnectDb)
            static_cast<CEqObj*>(NarrowObject(this, CObject::ClassEqObj))->disconnect(GetClientObjDb());
        static_cast<CEqObj*>(NarrowObject(this, CObject::ClassEqObj))->disconnect(static_cast<INotify*>(this));
    }
    // The +0x160 member's destructor (FUN_00544710, inlined): +0x174 first, then +0x164.
    CliEffectCompanionClear(TradeLaneTunnelCompanion);
    CliEffectCompanionClear(TradeLaneShipCompanion);
    // (FUN_00420d10 on +0x108 only restores the holder's vptr.)
    CliEffectCompanionClear(AtmosphereBurnCompanion);
}

namespace
{
    char const kCreateEffectSourceFile[] = "E:\\FL\\Scratch\\Source\\Client\\space\\createeffect.h";

    // EffectArch's constructor / destructor (FUN_0040e250 / FUN_0040e2a0), used for the stack
    // archetypes the notify handlers build, live with the registry in vis_effect_arch.cpp.
    // The stack archetype is EH state 0 of those frames, so its destructor also runs on the
    // unwind path: hence a scoped object.
    struct FScopedEffectArch
    {
        FScopedEffectArch()
        {
            Arch.Construct();
        }
        ~FScopedEffectArch()
        {
            Arch.Destruct();
        }
        FEffectArch Arch;

    private:
        FScopedEffectArch(FScopedEffectArch const&) = delete;
        FScopedEffectArch& operator=(FScopedEffectArch const&) = delete;
    };

    // The createeffect.h companion {Arch, sound, particle, light} (FUN_005154a0), EH state 1
    // of the same frames: cleared by FUN_00502d90 on both paths.
    struct FScopedEffectCompanion
    {
        explicit FScopedEffectCompanion(FEffectArch* Arch)
        {
            Slots[0] = Arch;
            Slots[1] = nullptr;
            Slots[2] = nullptr;
            Slots[3] = nullptr;
        }
        ~FScopedEffectCompanion()
        {
            CliEffectCompanionClear(Slots); // FUN_00502d90
        }
        void* Slots[4];

    private:
        FScopedEffectCompanion(FScopedEffectCompanion const&) = delete;
        FScopedEffectCompanion& operator=(FScopedEffectCompanion const&) = delete;
    };

    // FUN_00544230 (createeffect.h; thiscall on the effect factory DAT_00674c60, which it does
    // not read) -- start whatever the companion holds with Arch, drop what will not start, and
    // report whether anything is still alive.
    bool CreateEffectStart(void* Companion16, FEffectArch* Arch)
    {
        void** const Companion = static_cast<void**>(Companion16);

        FParticleEffect* const Particle = static_cast<FParticleEffect*>(Companion[2]);
        if (Particle != nullptr && Particle->FParticleEffect::Start(Arch) == 0)
        {
            Particle->Release();
            Companion[2] = nullptr;
        }

        // No sound archetype or a refusal releases the source.
        ISoundSource* const Sound = static_cast<ISoundSource*>(Companion[1]);
        if (Sound != nullptr)
        {
            Sound->SetLooping(static_cast<unsigned char>(Arch->Flags & 1u));
            if (Arch->SndEffect == nullptr || Sound->Start(Arch->SndEffect, 0) == 0)
            {
                Sound->Release();
                Companion[1] = nullptr;
            }
        }

        // Failure warns and releases; otherwise a named animation is attached -- the lookup
        // result is not null-tested.
        FGameLight* const Light = static_cast<FGameLight*>(Companion[3]);
        if (Light != nullptr)
        {
            ((FEngineLight*)Light)->SetType(kLightPoint, true);
            ((FEngineLight*)Light)->SetColorBytesAlt(&Arch->LgtColorR);
            ((FEngineLight*)Light)->SetRadius(Arch->LgtRadius);
            if (!Light->FGameLight::Enable())
            {
                ID_String Name;
                Name.Id = Arch->NicknameId;
                FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: couldnt open light",
                      kCreateEffectSourceFile, 0xce, Name.get_string());
                Light->Release();
                Companion[3] = nullptr;
            }
            else if (Arch->LgtEffectLen != 0)
            {
                Light->AttachAnim(GameLightFindAnimByName(Arch->LgtEffect),
                                  Arch->LgtRangeScale, Arch->LgtTimeScale, 0.0f);
            }
        }

        // Alive: a particle without the deactivated bit, an active sound, or a light with no
        // animation keys.
        FParticleEffect* const LiveParticle = static_cast<FParticleEffect*>(Companion[2]);
        if (LiveParticle != nullptr && (LiveParticle->Flags08 & kEffectFlagDeactivated) == 0u)
            return true;
        ISoundSource* const LiveSound = static_cast<ISoundSource*>(Companion[1]);
        if (LiveSound != nullptr && LiveSound->IsActive() != 0)
            return true;
        FGameLight* const LiveLight = static_cast<FGameLight*>(Companion[3]);
        if (LiveLight != nullptr && LiveLight->Anim.Keys == nullptr)
            return true;
        return false;
    }

    // The createeffect.h sequence the notify handlers inline (ship FUN_00545670, solar
    // FUN_005478f0 three times; the attach step is out of line as FUN_005480c0 in the solar
    // copies). Only with EFFECTS on. The archetype is not AddRef'd; on success the companion is
    // copied into Go's effect list (FUN_004fcea0) -- slot 0 there keeps the address of the
    // caller's stack archetype, exactly as retail's does. The local companion is cleared
    // (FUN_00502d90) either way.
    void CreateEffectOnGameobj(FClientGameobj* Go, FEffectArch* Arch, long EngineIndex,
                               Transform const* Pose)
    {
        if (GetGameOption(kGameOptEffectsGroup) == 0)
            return;

        FScopedEffectCompanion Scoped(Arch); // FUN_005154a0; FUN_00502d90 at scope exit
        void** const Companion = Scoped.Slots;
        if (GetGameOption(kGameOptParticles) != 0 && Arch->VisEffect != nullptr &&
            Arch->VisEffect->Nickname[0] != '\0')
            Companion[2] = ParticleEffectCreate();
        if (Arch->SndEffect != nullptr)
            Companion[1] = CreateClientSoundSource3D();
        if (Arch->LgtRadius > 0.0f)
            Companion[3] = GameLightConstruct(static_cast<FGameLight*>(GameLightHeapAlloc(0x12c)));

        if (Companion[2] != nullptr)
            static_cast<FParticleEffect*>(Companion[2])->Attach(Go, EngineIndex,
                                                                  Pose);
        if (Companion[1] != nullptr)
            static_cast<ISoundSource*>(Companion[1])->SetPose(EngineIndex, Pose);
        if (Companion[3] != nullptr)
            CliExplosionAttachLocalXform(Companion[3], EngineIndex,
                                         Pose);

        if (CreateEffectStart(Companion, Arch))
            CliEffectListInsert(&Go->EffectList, Companion);
    }

    // FUN_00545670 case 2: the bay doors' sound at the hull's
    // HpBaySurface hardpoint -- bay_doors_open_snd when the event's byte is set, else
    // bay_doors_close_snd. It is the createeffect.h sequence inlined on a stack EffectArch
    // that carries only the sound.
    void ShipBayDoorSound(FClientShipGameobj* Go, bool bOpen)
    {
        // The CShip narrowing (class & 0x503) is not null-tested before +0x88 is read.
        CObject* const Obj = Go->Object;
        CShip* const Ship = (Obj != nullptr &&
                             (static_cast<unsigned>(Obj->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                                ? static_cast<CShip*>(Obj)
                                : nullptr;
        Archetype::Ship const* const ShipArch =
            static_cast<Archetype::Ship const*>(Ship->Archetype);

        // The hardpoint in the object's own frame; an empty name or a miss leaves position
        // zero and orientation identity (the CRC word is not written).
        HardpointInfo Hp;
        long HpHandle = 0;
        char const* const HpName = ShipArch->HpBaySurface.Value;
        if (HpName[0] == '\0' || !FindHardpoint_PS(Go->get_index(), HpName, HpHandle, Hp))
        {
            Hp.Position.x = 0.0f;
            Hp.Position.y = 0.0f;
            Hp.Position.z = 0.0f;
            Hp.Orient.E[0][0] = 1.0f;
            Hp.Orient.E[0][1] = 0.0f;
            Hp.Orient.E[0][2] = 0.0f;
            Hp.Orient.E[1][0] = 0.0f;
            Hp.Orient.E[1][1] = 1.0f;
            Hp.Orient.E[1][2] = 0.0f;
            Hp.Orient.E[2][0] = 0.0f;
            Hp.Orient.E[2][1] = 0.0f;
            Hp.Orient.E[2][2] = 1.0f;
        }

        // The sound id is the CliShip companion's +0x58 / +0x5c.
        FScopedEffectArch Scoped; // FUN_0040e250; FUN_0040e2a0 at scope exit
        FEffectArch& Arch = Scoped.Arch;
        ID_String const* const SoundId = Archetype::CliShipBayDoorSound(ShipArch->Companion, bOpen);
        Arch.SndEffect = LookupClientSoundArch(SoundId);

        Transform Pose;
        Pose.Rotation.E[0][0] = Hp.Orient.E[0][0];
        Pose.Rotation.E[0][1] = Hp.Orient.E[0][1];
        Pose.Rotation.E[0][2] = Hp.Orient.E[0][2];
        Pose.Rotation.E[1][0] = Hp.Orient.E[1][0];
        Pose.Rotation.E[1][1] = Hp.Orient.E[1][1];
        Pose.Rotation.E[1][2] = Hp.Orient.E[1][2];
        Pose.Rotation.E[2][0] = Hp.Orient.E[2][0];
        Pose.Rotation.E[2][1] = Hp.Orient.E[2][1];
        Pose.Rotation.E[2][2] = Hp.Orient.E[2][2];
        Pose.Translation.x = Hp.Position.x;
        Pose.Translation.y = Hp.Position.y;
        Pose.Translation.z = Hp.Position.z;
        long const EngineIndex = Go->get_index();

        CreateEffectOnGameobj(Go, &Arch, EngineIndex, &Pose);
    }
}

void FClientShipGameobj::PlayCargoJettisonSound()
{
    // FUN_00543f50 -- the bay-door sequence of FUN_00545670 case 2 with SND_CARGO_JETTISONED
    // (the Common export) as the sound.
    if (SND_CARGO_JETTISONED.IsEmpty())
        return;

    // The narrowed CShip is not null-tested.
    CObject* const Obj = Object;
    CShip* const Ship =
        (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
            ? static_cast<CShip*>(Obj)
            : nullptr;
    Archetype::Ship const* const ShipArch = static_cast<Archetype::Ship const*>(Ship->Archetype);
    HardpointInfo Hp;
    long HpHandle = 0;
    char const* const HpName = ShipArch->HpBaySurface.Value;
    if (HpName[0] == '\0' || !FindHardpoint_PS(get_index(), HpName, HpHandle, Hp))
    {
        Hp.Position.x = 0.0f;
        Hp.Position.y = 0.0f;
        Hp.Position.z = 0.0f;
        Hp.Orient = Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    }

    FScopedEffectArch Scoped; // FUN_0040e250; FUN_0040e2a0 at scope exit
    FEffectArch& Arch = Scoped.Arch;
    Arch.SndEffect = LookupClientSoundArch(&SND_CARGO_JETTISONED);

    Transform Pose;
    Pose.Rotation.E[0][0] = Hp.Orient.E[0][0];
    Pose.Rotation.E[0][1] = Hp.Orient.E[0][1];
    Pose.Rotation.E[0][2] = Hp.Orient.E[0][2];
    Pose.Rotation.E[1][0] = Hp.Orient.E[1][0];
    Pose.Rotation.E[1][1] = Hp.Orient.E[1][1];
    Pose.Rotation.E[1][2] = Hp.Orient.E[1][2];
    Pose.Rotation.E[2][0] = Hp.Orient.E[2][0];
    Pose.Rotation.E[2][1] = Hp.Orient.E[2][1];
    Pose.Rotation.E[2][2] = Hp.Orient.E[2][2];
    Pose.Translation.x = Hp.Position.x;
    Pose.Translation.y = Hp.Position.y;
    Pose.Translation.z = Hp.Position.z;
    long const EngineIndex = get_index();

    CreateEffectOnGameobj(this, &Arch, EngineIndex, &Pose);
}

// The three NN voice lines the ship notify queues (static-init CreateID entries).
unsigned G_ShipVoiceCargoExpiredId = CreateID("cargo_expired");     // DAT_00678b58, LAB_00542910
unsigned G_ShipVoiceWeaponExpiredId = CreateID("weapon_expired");   // DAT_00678b54, LAB_00542930
unsigned G_ShipVoiceEngineCriticalId = CreateID("engine_critical"); // DAT_00678b4c, LAB_00542970
// DAT_00678b50, LAB_00542950 -- queued by slot 102 (LAB_00545f50) below 20% hull.
unsigned G_ShipVoiceHullBreachId = CreateID("hull_breach_imminent");
// The request-answer and IObjAffect slot 27 voice lines (static-init CreateID entries; readers
// FUN_00546d00 and FUN_00547250).
unsigned G_ShipVoiceTradeLaneDisruptedId = CreateID("nnvoice_trade_lane_disrupted"); // DAT_00678b34, LAB_00542a30
unsigned G_ShipVoiceDockGrantedId = CreateID("dock_granted");               // DAT_00678b30, LAB_00542a50
unsigned G_ShipVoiceDockDeniedHostileId = CreateID("dock_denied_hostile");  // DAT_00678b2c, LAB_00542a70
unsigned G_ShipVoiceDockDeniedLockedId = CreateID("dock_denied_locked");    // DAT_00678b28, LAB_00542a90
unsigned G_ShipVoiceCannotDockId = CreateID("cannot_dock");                 // DAT_00678b24, LAB_00542ab0
unsigned G_ShipVoiceWaitingToDockId = CreateID("waiting_to_dock");          // DAT_00678b20, LAB_00542ad0
unsigned G_ShipVoiceDockTooFarId = CreateID("nnv_dock_too_far");            // DAT_00678b1c, LAB_00542af0
unsigned G_ShipVoiceFormationLostId = CreateID("nnv_formation_lost");       // DAT_00678b10, LAB_00542b50

namespace
{
    // The collision group types of the engine nozzles.
    unsigned const kEngineGroupTypeMask =
        Archetype::CollisionGroup::kGroupTypeEngine | Archetype::CollisionGroup::kGroupTypePortEngine |
        Archetype::CollisionGroup::kGroupTypeMiddleEngine |
        Archetype::CollisionGroup::kGroupTypeStarboardEngine |
        Archetype::CollisionGroup::kGroupTypeTopEngine |
        Archetype::CollisionGroup::kGroupTypeBottomEngine;
}

void FClientShipGameobj::notify(Event Code, void* Param)
{
    // FUN_00545670 (INotify slot 0 on the +0xac sub-object), then always EquippedObj's notify.
    switch (static_cast<int>(Code))
    {
    case INotify::BayStateChanged:
        ShipBayDoorSound(this, *static_cast<unsigned char const*>(Param) != 0);
        break;
    case INotify::EquipExpired:
        // The player's ship only; FUN_00489e80 is the NN voice queue.
        if (static_cast<void*>(static_cast<IObjRW*>(this)) == GetPlayerObjRW())
            NnVoiceEnqueue(G_ShipVoiceWeaponExpiredId, 0);
        break;
    case INotify::CargoExpired:
        if (static_cast<void*>(static_cast<IObjRW*>(this)) == GetPlayerObjRW())
            NnVoiceEnqueue(G_ShipVoiceCargoExpiredId, 0);
        break;
    case INotify::ArchGroupDamaged:
    {
        // The player's ship, a CArchGroup whose collision group type has an engine bit and
        // whose relative health is below double 0.05 (DAT_005d5b58; equal and NaN stop).
        if (static_cast<void*>(static_cast<IObjRW*>(this)) != GetPlayerObjRW())
            break;
        CArchGroup* const Group = static_cast<CArchGroup*>(Param);
        if (Group == nullptr)
            break;
        if ((Group->GetCollisionGroupType() & kEngineGroupTypeMask) == 0u)
            break;
        if (!(static_cast<double>(Group->GetRelativeHealth()) < 0.05))
            break;
        NnVoiceEnqueue(G_ShipVoiceEngineCriticalId, 0);
        break;
    }
    default:
        break;
    }
    FClientEquippedObj::notify(Code, Param);
}

namespace
{
    // FUN_005478f0 codes 0 and 1: the solar's open_sound (CliSolar +0x54) or close_sound
    // (+0x58) at the root, pose identity -- the same createeffect.h sequence as the ship's bay
    // doors, with FUN_005480c0 doing the attach.
    void SolarDoorSound(FClientSolarGameobj* Go, bool bOpen)
    {
        // The archetype is read through the 0x303 narrowing without a null test.
        CObject* const Obj = Go->Object;
        CObject* const Solar = (Obj != nullptr &&
                                (static_cast<unsigned>(Obj->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar)
                                   ? Obj
                                   : nullptr;
        FScopedEffectArch Scoped; // FUN_0040e250; FUN_0040e2a0 at scope exit
        FEffectArch& Arch = Scoped.Arch;
        ID_String const* const SoundId =
            Archetype::CliSolarDoorSound(Solar->Archetype->Companion, bOpen);
        Arch.SndEffect = LookupClientSoundArch(SoundId);

        Transform Pose = {Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f), {0.0f, 0.0f, 0.0f}};
        long const EngineIndex = Go->get_index();
        CreateEffectOnGameobj(Go, &Arch, EngineIndex, &Pose);
    }
}

void FClientSolarGameobj::notify(Event Code, void* Param)
{
    // FUN_005478f0 (INotify slot 0 on the +0xac sub-object). Codes 0, 1 and 0x11; every path
    // ends in EquippedObj's notify.
    int const Kind = static_cast<int>(Code);
    if (Kind == INotify::DockAnimEnded)
    {
        // The archetype is read through the narrowing without a null test.
        CObject* const Obj = Object;
        CObject* const Solar = (Obj != nullptr &&
                                (static_cast<unsigned>(Obj->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar)
                                   ? Obj
                                   : nullptr;
        Archetype::EqObj const* const SolarArch =
            static_cast<Archetype::EqObj const*>(Solar->Archetype);
        if (SolarArch->bDockingCamera)
            SolarDoorSound(this, *static_cast<unsigned char const*>(Param) != 0);
    }
    else if (Kind == INotify::SolarGateStateChanged)
    {
        // A sub-code in the first dword of Param -- 0, 2 and 3 (1 does nothing). The "has a
        // jump gate effect" test calls GetTypeBits twice, as retail does.
        int const Sub = *static_cast<int const*>(Param);
        if (Sub == 0)
        {
            // operator new(0xe0) + FUN_00502ba0; a failed allocation stores null.
            SolarDoorSound(this, true);
            if ((GetTypeBits() & OBJ_JUMP_GATE) != 0u || (GetTypeBits() & OBJ_JUMP_HOLE) != 0u)
            {
                if (JumpGateEffect == nullptr)
                    JumpGateEffect = new (std::nothrow) FJumpGateEffect(this);
            }
        }
        else if (Sub == 2)
        {
            // FUN_00503380 sets the effect's +0x18 fire flag.
            SolarDoorSound(this, false);
            if ((GetTypeBits() & OBJ_JUMP_GATE) != 0u || (GetTypeBits() & OBJ_JUMP_HOLE) != 0u)
            {
                if (JumpGateEffect != nullptr)
                    static_cast<FJumpGateEffect*>(JumpGateEffect)->Fire();
            }
        }
        else if (Sub == 3)
        {
            if ((GetTypeBits() & OBJ_JUMP_GATE) != 0u || (GetTypeBits() & OBJ_JUMP_HOLE) != 0u)
            {
                if (JumpGateEffect != nullptr)
                    delete static_cast<FJumpGateEffect*>(JumpGateEffect);
                JumpGateEffect = nullptr;
            }
        }
    }
    else if (Kind == INotify::ShieldOfflineStateChanged)
    {
        // A trade lane ring (TypeBits bit 7) switches its lane (FUN_00516910) when its shield
        // goes offline or back online. No null test on
        // the narrowed object.
        CObject* const Obj = Object;
        CSimple const* const Simple =
            (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar)
                ? static_cast<CSimple const*>(Obj)
                : nullptr;
        if ((Simple->TypeBits & OBJ_TRADELANE_RING) != 0u)
            bTradeLaneIdle =
                TradeLaneSolarLaneSwitch(this, *static_cast<unsigned char const*>(Param));
    }
    FClientEquippedObj::notify(Code, Param);
}

void FClientShipGameobj::create(Archetype::Root* Arch)
{
    // LAB_00546550
    CreateOpenConnect(this, CObject::ClassShip, Arch);
}

bool FClientShipGameobj::find(unsigned const& Id)
{
    // FUN_005465d0
    return FindAndConnect(this, CObject::ClassShip, Id);
}

void FClientShipGameobj::PostCreateInit()
{
    // FUN_005447b0
    FClientEquippedObj::PostCreateInit();
    ShipFlags = static_cast<unsigned char>(ShipFlags & ~(kShipFlagHullHit | kShipFlagShieldHit));
    SetJumpState(kJumpStateNone);

    // The ship companion's pilot_mesh (+0x64). No null test on the narrowed CShip.
    CShip* const Ship = static_cast<CShip*>(NarrowObject(this, CObject::ClassShip));
    AttachPilotMesh(reinterpret_cast<ID_String const*>(
        reinterpret_cast<unsigned char const*>(Ship->Archetype->Companion) + 0x64));

    if (Ship->OwnerPlayer == GetPlayerConnectionId())
    {
        int const GraphId = pub::StateGraph::get_state_graph("Player", pub::StateGraph::LEADER);
        Ship->create_behavior_interface(static_cast<IObjRW*>(this), GraphId);
    }

    if (!Ship->is_launching() && !Ship->is_jumping())
        Ship->enable_collisions_r(true);
}

void FClientShipGameobj::AttachPilotMesh(ID_String const* PilotMesh)
{
    // FUN_00544380
    if (PilotMesh->IsEmpty())
        return;

    long Handle = 0;
    HardpointInfo PilotInfo;
    if (!FindHardpoint_PS(get_index(), "HpPilot", Handle, PilotInfo))
        return;

    PilotMeshArch = Archetype::GetSimple(PilotMesh->Id);
    if (PilotMeshArch == nullptr)
        return;
    PilotMeshArch->load();
    PilotMeshArch->Companion->Load(0);
    long const Instance = PilotMeshArch->create_instance(nullptr);
    if (Instance == -1)
        return;
    PilotMeshHolder.EngineInstance = Instance;

    // The result is not tested. Port deviation: retail reads its stack HardpointInfo whether
    // or not the lookup filled it; the port starts it zeroed so a miss reads zeros instead of
    // stale stack.
    HardpointInfo ConnectInfo = {};
    FindHardpoint_PS(Instance, "HpConnect", Handle, ConnectInfo);

    PilotTransform.Rotation = PilotInfo.Orient;
    PilotTransform.Translation.x = PilotInfo.Position.x - ConnectInfo.Position.x;
    PilotTransform.Translation.y = PilotInfo.Position.y - ConnectInfo.Position.y;
    PilotTransform.Translation.z = PilotInfo.Position.z - ConnectInfo.Position.z;
}

IObjRW* FClientShipGameobj::PickNextTarget(int ConnectionId)
{
    // FUN_005449f0. ConnectionId is pushed by the caller and never read.
    (void)ConnectionId;
    // Two bests: all candidates and the watch-listed ones.
    IObjRW* Nearest = nullptr;
    IObjRW* NearestListed = nullptr;
    float NearestDist = 1.0e9f;
    float NearestListedDist = 1.0e9f;

    // No null test on the narrowed CShip.
    CObject* Obj = const_cast<CObject*>(cobject());
    if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
        Obj = nullptr;
    CShip* const Ship = static_cast<CShip*>(Obj);

    CEScanner* const Scanner = static_cast<CEScanner*>(Ship->EquipManager.FindFirst(EquipTypeScanner));
    if (Scanner == nullptr)
        return Nearest;

    for (int I = 0; I < Scanner->Scans.Count; ++I)
    {
        // The watched Watchable, back to its IObjRW (minus 8).
        IObjRW* const Other =
            static_cast<IObjRW*>(const_cast<Watchable*>(Scanner->Scans.Entries[I].target()));
        if (Other == nullptr)
            continue;
        bool bListed = false;
        if (!Ship->is_enemy(Other))
            continue;
        if (Other->is_dying())
            continue;
        // A CSimple counts only with TypeBits & 0x7f0000 or when its gameobj is watch-listed.
        CObject const* const OtherObj = Other->cobject();
        if (OtherObj != nullptr && (static_cast<unsigned>(OtherObj->ObjectClass) & CObject::ClassSimple) == CObject::ClassSimple)
        {
            FClientGameobj* const OtherGo = ClientObjDb_FindObject(Other->get_id(), 0);
            if (OtherGo != nullptr)
                bListed = OtherGo->bWatchListed != 0;
            if ((static_cast<CSimple const*>(OtherObj)->TypeBits & OBJ_SHIP_MASK) == 0 && !bListed)
                continue;
        }
        Vector const& Mine = get_position();
        Vector const& Theirs = Other->get_position();
        // x87 keeps the distance unrounded until it is stored as the new best.
        double const Dx = static_cast<double>(Theirs.x) - Mine.x;
        double const Dy = static_cast<double>(Theirs.y) - Mine.y;
        double const Dz = static_cast<double>(Theirs.z) - Mine.z;
        double const Dist = std::sqrt(Dx * Dx + Dz * Dz + Dy * Dy);
        if (Dist < NearestDist)
        {
            NearestDist = static_cast<float>(Dist);
            Nearest = Other;
        }
        if (bListed && Dist < NearestListedDist)
        {
            NearestListedDist = static_cast<float>(Dist);
            NearestListed = Other;
        }
    }

    if (NearestListed != nullptr)
        return NearestListed;
    return Nearest;
}

void FClientShipGameobj::SetTargetWithGuns(IObjRW* Target, unsigned short SubTarget)
{
    // FUN_00544b90
    static_cast<IObjAffect*>(this)->set_target(Target, SubTarget,
                                               static_cast<int>(GetPlayerConnectionId()));

    // The CShip is narrowed again on each pass and used with no null test. FUN_005351d0 is a
    // direct (non-virtual) call.
    CEquipTraverser Walk(EquipTypeGun);
    for (;;)
    {
        CShip* const Ship = static_cast<CShip*>(NarrowObject(this, CObject::ClassShip));
        CEquip* const Gun = Ship->EquipManager.Traverse(Walk);
        if (Gun == nullptr)
            break;
        unsigned short const GunId = Gun->Id;
        FClientEquippedObj::set_gun_target(&GunId, Target, SubTarget,
                                    static_cast<int>(GetPlayerConnectionId()));
    }
}

bool FClientShipGameobj::RequestTractor(IObjRW* Target)
{
    // FUN_00544c30
    CShip* const Ship = static_cast<CShip*>(NarrowObject(this, CObject::ClassShip));
    CEquip* const Tractor = Ship->EquipManager.FindFirst(EquipTypeTractor);
    if (Tractor == nullptr)
        return false;

    unsigned short const TractorId = Tractor->Id;
    IObjAffect* const Affect = static_cast<IObjAffect*>(this);
    // Only a CLoot target is tractored; any other target nothing.
    if (Target != nullptr && !Tractor->IsDestroyed())
    {
        CObject const* const TargetObj = Target->cobject();
        if (TargetObj != nullptr &&
            (static_cast<unsigned>(TargetObj->ObjectClass) & CObject::ClassLoot) == CObject::ClassLoot)
        {
            Affect->tractor_single(&TractorId, Target, static_cast<int>(GetPlayerConnectionId()));
        }
        return true;
    }
    Affect->toggle_item(&TractorId, false, static_cast<int>(GetPlayerConnectionId()));
    return true;
}

// DAT_00613f58 / DAT_00613f5c / DAT_00613f60 (.data, read only by the ship update): the
// "fade the tunnel out" latch (initially set) and the two fade times.
unsigned char G_bJumpTunnelFadeOutPending = 1; // DAT_00613f58
float G_JumpTunnelFadeOutTime = 1.5f;          // DAT_00613f5c
float G_JumpTunnelFadeInTime = 1.5f;           // DAT_00613f60

// The three NN voice lines of the atmosphere / corona warning (static-init CreateID entries).
unsigned G_ShipVoiceDangerId = CreateID("danger");                           // DAT_00678b48, LAB_00542990
unsigned G_ShipVoiceEnteringSunCoronaId = CreateID("entering_sun_corona");   // DAT_00678b44, LAB_005429b0
unsigned G_ShipVoiceEnteringAtmosphereId = CreateID("entering_atmosphere");  // DAT_00678b40, LAB_005429d0

void FClientShipGameobj::update(float Dt)
{
    // FUN_00545050
    if (IsDead())
        return;

    // Kept for the whole function with no null test anywhere below.
    CShip* const Ship = static_cast<CShip*>(NarrowObject(this, CObject::ClassShip));

    // The player's ship follows the DISABLE_COLLIDE option (0x37), latched on first use
    // (DAT_00678b08, guard DAT_00678b0c).
    if (static_cast<void*>(static_cast<IObjRW*>(this)) == GetPlayerObjRW())
    {
        static int SDisableCollide = GetGameOption(kGameOptDisableCollide);
        if (SDisableCollide != GetGameOption(kGameOptDisableCollide))
        {
            SDisableCollide = GetGameOption(kGameOptDisableCollide);
            Ship->enable_collisions_r(SDisableCollide == 0);
        }
    }

    // The local player's objects run the full CObject::update; others, in multiplayer only,
    // the dumb one; in single player a foreign object is not updated here at all.
    CObject* const OwnerObject = NarrowObject(this, CObject::ClassSimple);
    unsigned const Owner = (OwnerObject != nullptr)
                               ? static_cast<CSimple const*>(OwnerObject)->OwnerPlayer
                               : GetPlayerConnectionId();
    if (Owner == GetPlayerConnectionId())
        Ship->update(Dt, CObject::UPDATE_DEFAULT);
    else if (!SinglePlayer())
        Ship->update(Dt, CObject::UPDATE_DUMB);

    FClientEquippedObj::update(Dt);

    // The bay-close countdown: a NaN goes on; the unrounded difference is compared; the CShip
    // is narrowed again for each close.
    if (BayCloseTimer != 0.0f)
    {
        double const Remaining = static_cast<double>(BayCloseTimer) - static_cast<double>(Dt);
        BayCloseTimer = static_cast<float>(Remaining);
        if (Remaining <= 0.0)
        {
            while (BayCloseCount > 0)
            {
                static_cast<CShip*>(NarrowObject(this, CObject::ClassShip))->close_bay();
                BayCloseCount = BayCloseCount - 1;
            }
            BayCloseTimer = 0.0f;
        }
    }

    // The jump.
    if (JumpState != kJumpStateNone && JumpEffect != nullptr)
    {
        FJumpShipEffect* const JumpFx = static_cast<FJumpShipEffect*>(JumpEffect);
        // The step is clamped to 0.1f (DAT_005d3ee0); a NaN Dt is kept.
        float JumpDt = Dt;
        if (Dt > 0.1f)
            JumpDt = 0.1f;
        JumpFx->Clock = JumpFx->Clock + JumpDt;

        // In the tunnel, once, when no more than 1.5 s of JumpOutTunnelTime remain (greater
        // and NaN skip), the screen fades out to white (FUN_004122a0).
        if (G_bJumpTunnelFadeOutPending != 0 && JumpState == kJumpStateJumpOutTunnel &&
            static_cast<double>(JumpFx->JumpOutTunnelTime) -
                    static_cast<double>(JumpFx->Clock) <=
                static_cast<double>(G_JumpTunnelFadeOutTime))
        {
            G_bJumpTunnelFadeOutPending = 0;
            float const White[3] = {1.0f, 1.0f, 1.0f};
            FadeOutColoured(White, G_JumpTunnelFadeOutTime);
        }

        // Each state moves on once its time is below the clock (ordered "less" only).
        switch (JumpState)
        {
        case kJumpStateJumpOut:
            if (JumpFx->JumpOutTime < JumpFx->Clock)
                SetJumpState(kJumpStateJumpOutTunnel);
            break;
        case kJumpStateJumpOutTunnel:
            // The screen fades back in from white (FUN_004121d0, starting opaque).
            if (JumpFx->JumpOutTunnelTime < JumpFx->Clock)
            {
                G_bJumpTunnelFadeOutPending = 1;
                SetJumpState(kJumpStateJumpInTunnel);
                float const White[3] = {1.0f, 1.0f, 1.0f};
                FadeInColoured(White, G_JumpTunnelFadeInTime, true);
            }
            break;
        case kJumpStateJumpInTunnel:
            // Only once the jump in was ordered.
            if (JumpFx->JumpInTunnelTime < JumpFx->Clock && bJumpInReady != 0)
                SetJumpState(kJumpStateJumpIn);
            break;
        case kJumpStateJumpIn:
            if (JumpFx->JumpInTime < JumpFx->Clock)
                SetJumpState(kJumpStateArrived);
            break;
        case kJumpStateArrived:
            if (JumpFx->Clock > 1.0f)
                SetJumpState(kJumpStateNone);
            break;
        default:
            break;
        }

        // The effect, when it survived the switch (state 0 frees it), gets the same clamped
        // step (FUN_00503cd0).
        if (JumpEffect != nullptr)
            static_cast<FJumpShipEffect*>(JumpEffect)->Update(JumpDt);
    }

    // The object is not narrowed here.
    bool const bCollides = PhySys::GetCollisionState(Object);
    // The CSolar the ship is inside (CShip::update keeps it at +0x218); its type bits: 2
    // planet, 4 sun.
    CSolar* const Atmosphere = Ship->AtmosphereSolar;

    // The FUN_00503330 alive test, inlined here.
    if (CreateEffectCompanionAlive(AtmosphereBurnCompanion) != 0)
    {
        // It leaves the effect list once the ship stops colliding, leaves the atmosphere or is
        // landing (+0x18c bit 3).
        if (!bCollides || Atmosphere == nullptr || (ShipFlags & kShipFlagLanding) != 0)
            CliEffectListRemove(reinterpret_cast<unsigned char*>(this) + 0x28, AtmosphereBurnCompanion);
    }
    else if (bCollides && Atmosphere != nullptr && (ShipFlags & kShipFlagLanding) == 0)
    {
        ID_String BurnEffect;
        BurnEffect.Id = CreateID("atmosphere_burn");
        Transform Local;
        for (int Row = 0; Row < 3; ++Row)
        {
            for (int Col = 0; Col < 3; ++Col)
                Local.Rotation.E[Row][Col] = (Row == Col) ? 1.0f : 0.0f;
        }
        Local.Translation.x = 0.0f;
        Local.Translation.y = 0.0f;
        Local.Translation.z = 0.0f;
        int const Index = static_cast<int>(get_index());
        if (!BurnEffect.IsEmpty())
            CliCreateEffectOneShot(FindEffectArch(&BurnEffect.Id), this, Index, &Local,
                                   AtmosphereBurnCompanion);

        if (static_cast<void*>(static_cast<IObjRW*>(this)) == GetPlayerObjRW())
        {
            unsigned const TypeBits = Atmosphere->TypeBits;
            if ((TypeBits & OBJ_SUN) != 0)
            {
                NnVoiceEnqueue(G_ShipVoiceDangerId, 0);
                NnVoiceEnqueue(G_ShipVoiceEnteringSunCoronaId, 0);
            }
            else if ((TypeBits & OBJ_PLANET) != 0)
            {
                NnVoiceEnqueue(G_ShipVoiceDangerId, 0);
                NnVoiceEnqueue(G_ShipVoiceEnteringAtmosphereId, 0);
            }
        }
    }

    // The thruster sound (class CliThrusterSound, vtable 005ddbfc; menu_sound_arch.cpp) is
    // created the first time the CShip carries a thruster; a failed allocation stores null.
    if (ThrusterSound == nullptr && Ship->EquipManager.FindFirst(EquipTypeThruster) != nullptr)
        ThrusterSound = CreateClientThrusterSound(this);
    if (ThrusterSound != nullptr)
        TickClientThrusterSound(ThrusterSound, Dt);

    if (FireFailedCooldown > 0.0f)
        FireFailedCooldown = FireFailedCooldown - Dt;
}

void FClientShipGameobj::UpdateDrawLighting(void* Camera)
{
    // LAB_00546480. Only the player's ship in the tunnel (jump states 2 / 3) lights itself.
    if ((JumpState == kJumpStateJumpOutTunnel || JumpState == kJumpStateJumpInTunnel) &&
        this == GetPlayerObjRW())
    {
        EngineLightClearRegisteredList(); // FUN_004101e0
        FJumpShipEffect* const Effect = static_cast<FJumpShipEffect*>(JumpEffect);
        if (Effect == nullptr)
            return;
        // The jump_ambient bytes into D3DRS_AMBIENT.
        if (Effect->Ambient != nullptr)
            EngineLightSetAmbientRgbBytes(Effect->Ambient);
        if (Effect->Tunnel == nullptr)
            return;
        Vector Pos = get_position();
        FGameLight* const Light = Effect->Tunnel->Light;
        if (Light == nullptr)
            return;
        ((FEngineLight*)Light)->SetPosition(&Pos);
        FEngineLight* const Lights[1] = {(FEngineLight*)Light};
        EngineLightApplyList(Lights, 1u, static_cast<ICamera*>(Camera), &Pos.x);
        return;
    }
    FClientGameobj::UpdateDrawLighting(Camera);
}

namespace
{
    // FUN_005462d0 -- QI DALib::Engine for IFLAppearance into a smart-pointer local (EH frame
    // LAB_005c0f38), then Slot4 (+0x10).
    bool GetAppearanceInstanceValue(int Id, void** Out)
    {
        IFLAppearance* Ifl = nullptr;
        if (DALib::Engine->QueryInterface(Iids::FLAppearance, reinterpret_cast<void**>(&Ifl)) < 0)
        {
            if (Ifl != nullptr)
                Ifl->Release();
            return false;
        }
        bool const bOk = Ifl->GetInstanceValue(Id, Out) >= 0;
        if (Ifl != nullptr)
            Ifl->Release();
        return bOk;
    }

    // FUN_00546380 -- the same QI around Slot6 (+0x18).
    void SetAppearanceDirectionalLight(float const* First, float const* Second)
    {
        IFLAppearance* Ifl = nullptr;
        if (DALib::Engine->QueryInterface(Iids::FLAppearance, reinterpret_cast<void**>(&Ifl)) >= 0)
            Ifl->SetDirectionalLight(First, Second);
        if (Ifl != nullptr)
            Ifl->Release();
    }

    // FUN_00546400 -- the same around Slot7 (+0x1c).
    void SetAppearanceDirectionalLightEnabled(unsigned char Flag)
    {
        IFLAppearance* Ifl = nullptr;
        if (DALib::Engine->QueryInterface(Iids::FLAppearance, reinterpret_cast<void**>(&Ifl)) >= 0)
            Ifl->SetDirectionalLightEnabled(Flag);
        if (Ifl != nullptr)
            Ifl->Release();
    }
}

unsigned* FClientShipGameobj::SubmitDraw(unsigned* OutLod, void* Camera, unsigned CachedLod,
                                      Transform const* Xform, void* Extra)
{
    // LAB_00546050 -- the ship draw. From the cockpit camera the player's own ship is not
    // drawn.
    if (IsFirstPersonCameraCurrent() && GetPlayerObjRW() == this)
    {
        *OutLod = 0xffff0000u;
        return OutLod;
    }

    // The pilot mesh. Retail tests the signed high word against 2 and then 1.
    unsigned short const LodLow = static_cast<unsigned short>(CachedLod & 0xffffu);
    short const LodHigh = static_cast<short>(CachedLod >> 16);
    if (GetGameOption(kGameOptShips) > 0 && LodLow >= 3 && LodHigh < 2 && LodHigh < 1 &&
        PilotMeshHolder.HasInstance())
    {
        // A cloaking ship passes its appearance value on to the pilot. No null test on the
        // narrowed object.
        CEqObj* const EqObj = static_cast<CEqObj*>(NarrowObject(this, CObject::ClassEqObj));
        if (EqObj->CloakPercent > 0.0f)
        {
            GetAppearanceOverrideMaterial(3u, &Extra);
            // Retail leaves this local unwritten when the QI fails; zeroed here.
            void* Value = nullptr;
            GetAppearanceInstanceValue(static_cast<int>(get_index()), &Value);
            SetAppearanceInstanceValue(PilotMeshHolder.EngineInstance, Value);
        }

        UpdateDrawLighting(Camera);
        // ship.cpp's 3DMathEngine is DAT_00678b68.
        Transform const& ShipXform = reinterpret_cast<Transform const&>(get_orientation());
        Transform PilotXform;
        GetShipMathEngine()->MulTransform(PilotXform, ShipXform,
                                          PilotTransform);
        Transform Copy;
        CopyTransform12(&Copy, &PilotXform);
        PilotMeshHolder.SetMatrix(&Copy);
        DALib::Renderer->RenderInstance(Camera, PilotMeshHolder.EngineInstance, 1.0f, kDrawPickSwitch, nullptr, Extra);
    }

    SetAppearanceDirectionalLightEnabled(0);

    // With a star (FUN_0050c620's first node), the unit direction from the star to this ship
    // is the appearance's directional light; the reciprocal length is stored as a float.
    FStarSphereList* const Stars = StarSphereList();
    if (Stars != nullptr && Stars->Count != 0 && Stars->Sentinel->Next->Sphere != nullptr)
    {
        Vector StarPos;
        Stars->Sentinel->Next->Sphere->GetOwnerPosition(&StarPos);
        Vector const& Pos = get_position();
        double const Dz = static_cast<double>(Pos.z) - StarPos.z;
        double const Dy = static_cast<double>(Pos.y) - StarPos.y;
        double const Dx = static_cast<double>(Pos.x) - StarPos.x;
        float const White[3] = {1.0f, 1.0f, 1.0f};
        float const Inv = static_cast<float>(1.0 / std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz));
        float const Dir[3] = {static_cast<float>(Dx * Inv), static_cast<float>(Dy * Inv),
                              static_cast<float>(Dz * Inv)};
        SetAppearanceDirectionalLight(Dir, White);
    }

    unsigned Lod = 0;
    unsigned* const Result =
        FClientEquippedObj::SubmitDraw(&Lod, Camera, CachedLod, Xform, Extra);
    *OutLod = *Result;
    return OutLod;
}

void FClientShipGameobj::ApplyDamageList(DamageList const& Damage)
{
    // LAB_00545f50. Damage of cause 6 on the player's ship drops cruise. No null test on the
    // narrowed object.
    if (Damage.Cause == DamageCause::CruiseDisrupter && this == GetPlayerObjRW())
    {
        static_cast<IObjAffect*>(this)->toggle_cruise(false, true,
                                                static_cast<int>(GetPlayerConnectionId()));
        IBehaviorManager* const Behavior =
            static_cast<CEqObj*>(NarrowObject(this, CObject::ClassShip))->get_behavior_interface();
        if (Behavior != nullptr)
            Behavior->update_current_behavior_cruise(false);
    }

    FClientEquippedObj::ApplyDamageList(Damage);

    // The player's ship below 0.2f health (DAT_005dfb40) queues "hull_breach_imminent" once,
    // unless the list is a destroyed one; at or above it the latch clears.
    if (this == GetPlayerObjRW())
    {
        CSimple* const Simple = static_cast<CSimple*>(NarrowObject(this, CObject::ClassShip));
        if (!(Simple->get_relative_health() < 0.2f))
        {
            bHullBreachWarned = 0;
            StarSysNoteDamage(this, &Damage);
            return;
        }
        if (bHullBreachWarned == 0 && !Damage.bDestroyed)
        {
            bHullBreachWarned = 1;
            NnVoiceEnqueue(G_ShipVoiceHullBreachId, 0);
        }
    }

    StarSysNoteDamage(this, &Damage); // FUN_00553250
}

void FClientShipGameobj::OnSurfaceCollisionHit(void const* Contact, void const* B, FClientGameobj* Other,
                                 void const* D)
{
    // LAB_00545e90
    FClientEquippedObj::OnSurfaceCollisionHit(Contact, B, Other, D);
    // The player's damage-indicator arrow towards the contact point.
    if (this != GetPlayerObjRW())
        return;
    HUD_DamageIndicator* const Indicator = GetHudDamageIndicator();
    if (Indicator == nullptr)
        return;
    Indicator->FlashFromPosition(static_cast<Vector const*>(Contact));
}

void FClientShipGameobj::OnMunitionHit(void const* A, void const* B, FClientGameobj* Munition)
{
    // LAB_00545ed0
    FClientEquippedObj::OnMunitionHit(A, B, Munition);

    unsigned short const SubObj = static_cast<unsigned short>(reinterpret_cast<uintptr_t>(B));
    if (SubObjectID::IsShieldEquipID(SubObj))
        ShipFlags |= kShipFlagShieldHit;
    else
        ShipFlags |= kShipFlagHullHit;

    if (this != GetPlayerObjRW())
        return;
    if (GetHudDamageIndicator() == nullptr)
        return;
    Vector const Velocity = Munition->get_velocity();
    GetHudDamageIndicator()->FlashAlongRay(static_cast<Vector const*>(A), &Velocity);
}

void FClientShipGameobj::Explode(Vector const* Pos)
{
    // LAB_00545d80. Retail inlines the companion's stop and release here.
    CliEffectCompanionArmStop(AtmosphereBurnCompanion);
    CliEffectCompanionClear(AtmosphereBurnCompanion);
    FClientEquippedObj::Explode(Pos);
}

void FClientShipGameobj::UpdateNebula(float Dt)
{
    // LAB_005455b0 -- the lazy nebula lookup.
    NebulaUpdateTimer += Dt;

    // In the tunnel: no nebula, no factor.
    if (JumpState == kJumpStateJumpOutTunnel || JumpState == kJumpStateJumpInTunnel)
    {
        CurrentNebula = nullptr;
        NebulaFogFactor = 0.0f;
        return;
    }

    // The refresh period: 1.0 for other ships; for the player's ship 0.1 while the fog factor
    // (+0x3c, a float) is strictly between 0.0 and 1.0, else 0.5.
    float Period = 1.0f;
    if (this == GetPlayerObjRW())
    {
        float const Factor = NebulaFogFactor;
        if (Factor > 0.0f && Factor < 1.0f)
            Period = 0.1f;
        else
            Period = 0.5f;
    }

    if (!(NebulaUpdateTimer > Period))
        return;

    // FUN_005553b0 with the previous nebula as the hint.
    FNebulaEffect* const Hint = CurrentNebula;
    FNebulaEffect* const Nebula =
        StarSysFindNebula(&get_position(), &NebulaFogFactor, Hint);
    CurrentNebula = Nebula;

    // The clock is reloaded, reduced on the FPU and stored once.
    double Clock = NebulaUpdateTimer;
    if (!(Clock > Period))
        return;
    do
    {
        Clock -= Period;
    } while (Clock > Period);
    NebulaUpdateTimer = static_cast<float>(Clock);
}

bool FClientShipGameobj::LightFuse(unsigned SourceId, ID_String const* FuseId,
                                 unsigned short SubObjId, float AtT, float Lifetime)
{
    // FUN_00545a90
    bool const bLit = FClientEquippedObj::LightFuse(SourceId, FuseId, SubObjId, AtT, Lifetime);
    if (!bLit)
        return bLit;
    if (FuseId->Id != DEATH_COMM_FUSE.Id)
        return bLit;

    // No null test on the narrowed ship.
    CShip* const Ship = static_cast<CShip*>(NarrowObject(this, CObject::ClassShip));

    // The formation followers (+0x1d4): a follower that is the player's ship hears event 7
    // with this ship's id. Its narrowed object is used without a null test.
    for (std::vector<FObjRWWatcher>::const_iterator It = Ship->FormationFollowers.begin();
         It != Ship->FormationFollowers.end(); ++It)
    {
        IObjRW* const Follower = It->get();
        if (Follower == nullptr || !IsPlayerShipId(Follower->get_id()))
            continue;
        CObject* const FollowerObj = const_cast<CObject*>(Follower->cobject());
        CEqObj* const FollowerShip =
            (FollowerObj != nullptr &&
             (static_cast<unsigned>(FollowerObj->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                ? static_cast<CEqObj*>(FollowerObj)
                : nullptr;
        IBehaviorManager* const Behavior = FollowerShip->get_behavior_interface();
        if (Behavior == nullptr)
            continue;
        unsigned SelfId = get_id();
        FBehaviorManagerEvent Event;
        Event.Type = FBehaviorManagerEvent::kEventObjectDying;
        Event.Data = &SelfId;
        Behavior->OnEvent(&Event);
    }

    // The same over the follow followers (+0x1f8).
    for (std::vector<FObjRWWatcher>::const_iterator It = Ship->FollowFollowers.begin();
         It != Ship->FollowFollowers.end(); ++It)
    {
        IObjRW* const Follower = It->get();
        if (Follower == nullptr || !IsPlayerShipId(Follower->get_id()))
            continue;
        CObject* const FollowerObj = const_cast<CObject*>(Follower->cobject());
        CEqObj* const FollowerShip =
            (FollowerObj != nullptr &&
             (static_cast<unsigned>(FollowerObj->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                ? static_cast<CEqObj*>(FollowerObj)
                : nullptr;
        IBehaviorManager* const Behavior = FollowerShip->get_behavior_interface();
        if (Behavior == nullptr)
            continue;
        unsigned SelfId = get_id();
        FBehaviorManagerEvent Event;
        Event.Type = FBehaviorManagerEvent::kEventObjectDying;
        Event.Data = &SelfId;
        Behavior->OnEvent(&Event);
    }

    // The player drops this ship as its target.
    FClientGameobj* const Player = static_cast<FClientGameobj*>(GetPlayerObjRW());
    if (Player == nullptr)
        return bLit;
    CObject* const PlayerObj = Player->Object;
    if (PlayerObj == nullptr ||
        (static_cast<unsigned>(PlayerObj->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
        return bLit;
    IObjRW* const Target = static_cast<CShip*>(PlayerObj)->get_target();
    if (Target == nullptr)
        return bLit;
    if (Target->get_id() != get_id())
        return bLit;
    static_cast<IObjAffect*>(Player)->set_target(nullptr, SUBOBJ_ID_NONE,
                                                 static_cast<int>(GetPlayerConnectionId()));
    return bLit;
}

unsigned char FClientShipGameobj::HandleEquipMsg(FCliEquipFirePacket const* Packet)
{
    // FUN_00544d70 -- ported as FClientEquippedObj::ShipHandleEquipUpdate (cli_projectile_fx.cpp).
    return ShipHandleEquipUpdate(Packet);
}

void FClientShipGameobj::PlayFireFailedSound()
{
    // LAB_00545cc0 -- the "fire failed" beep. A cooldown above zero (or unordered) returns.
    if (!(FireFailedCooldown <= 0.0f))
        return;

    // Looked up once.
    if (FireFailedSoundArch == nullptr)
    {
        FireFailedSoundArch = (Archetype::ISound*)LookupClientSoundArch(&FIRE_FAILED_SOUND);
        if (FireFailedSoundArch != nullptr)
            AcquireClientSoundArch(FireFailedSoundArch);
    }

    // No archetype -> only the cooldown.
    if (FireFailedSoundArch != nullptr)
    {
        if (FireFailedSound == nullptr)
        {
            // FUN_00428680 creates the source.
            PlayClientSoundArch(FireFailedSoundArch, nullptr, reinterpret_cast<void**>(&FireFailedSound), 0);
            FireFailedCooldown = FIRE_FAILED_DELAY;
            return;
        }
        if (FireFailedSound->IsActive())
        {
            FireFailedSound->SetStartOffset(0);
            FireFailedCooldown = FIRE_FAILED_DELAY;
            return;
        }
        FireFailedSound->Start(FireFailedSoundArch, 0);
    }

    FireFailedCooldown = FIRE_FAILED_DELAY;
}

int FClientShipGameobj::set_throttle(float Throttle)
{
    // LAB_00546640: CShip::set_throttle on the object narrowed by 0x503, no null test.
    static_cast<CShip*>(NarrowObject(this, CObject::ClassShip))->set_throttle(Throttle);
    return 0;
}

int FClientShipGameobj::set_axis_throttle(Vector const& AxisThrottle)
{
    // LAB_00546680: the CShip's steering (+0x230).
    static_cast<CShip*>(NarrowObject(this, CObject::ClassShip))->Steering.SetAxisThrottle(AxisThrottle);
    return 0;
}

int FClientShipGameobj::set_nudge_vec(Vector const& NudgeVec)
{
    // LAB_005466c0
    static_cast<CShip*>(NarrowObject(this, CObject::ClassShip))->set_nudge_vec(NudgeVec);
    return 0;
}

int FClientShipGameobj::set_strafe_dir(StrafeDir Dir)
{
    // LAB_00546700 (IObjAffect slot 3). No null test on the narrowed object.
    if (static_cast<CShip*>(NarrowObject(this, CObject::ClassShip))->get_strafe_dir() == Dir)
        return 0;

    // An inactive engine is switched on first.
    CEquip* const Engine =
        static_cast<CEqObj*>(NarrowObject(this, CObject::ClassShip))->EquipManager.FindFirst(EquipTypeEngine);
    if (Engine != nullptr && !Engine->IsActive())
    {
        unsigned short EngineId = Engine->Id;
        static_cast<IObjAffect*>(this)->toggle_item(&EngineId, true,
                                                static_cast<int>(GetPlayerConnectionId()));
    }

    static_cast<CShip*>(NarrowObject(this, CObject::ClassShip))->set_strafe_dir(Dir);
    return 0;
}

int FClientShipGameobj::set_formation_leader(IObjRW* Leader)
{
    // LAB_00547140: the CShip's +0x1b8 BaseWatcher (BaseWatcher::set is the inlined compare +
    // set_pointer).
    CShip* const Ship = static_cast<CShip*>(NarrowObject(this, CObject::ClassShip));
    Watchable const* const Target = (Leader != nullptr) ? static_cast<Watchable const*>(Leader)
                                                        : nullptr;
    Ship->FormationLeader.set(Target);
    return 0;
}

int FClientShipGameobj::set_formation_offset(Vector const& Offset)
{
    // LAB_005470f0
    static_cast<CShip*>(NarrowObject(this, CObject::ClassShip))->FormationOffset = Offset;
    return 0;
}

int FClientShipGameobj::add_formation_follower(IObjRW* Follower)
{
    // LAB_00547190
    return static_cast<int>(
        static_cast<CShip*>(NarrowObject(this, CObject::ClassShip))->add_formation_follower(Follower));
}

int FClientShipGameobj::remove_formation_follower(IObjRW* Follower)
{
    // LAB_005471c0
    return static_cast<int>(
        static_cast<CShip*>(NarrowObject(this, CObject::ClassShip))->remove_formation_follower(Follower));
}

int FClientShipGameobj::jettison_cargo(unsigned short EquipId, int Count, int SourceClient)
{
    // FUN_005467e0 (IObjAffect slot 14) -- cargo jettison. From this client the request goes
    // to the server.
    if (SourceClient == -1 || static_cast<unsigned>(SourceClient) == GetPlayerConnectionId())
    {
        XJettisonCargo Info;
        Info.ObjectId = get_id();
        Info.ItemId = EquipId;
        Info.Count = static_cast<unsigned int>(Count);
        LocalServer()->JettisonCargo(GetPlayerConnectionId(), Info);
        return 0;
    }

    // From the server: remove the cargo, open the bay and schedule one more close (FADD order
    // kept).
    CEquip* const Equip =
        static_cast<CEqObj*>(NarrowObject(this, CObject::ClassShip))->EquipManager.FindByID(EquipId);
    CECargo* const Cargo = CECargo::cast(Equip);
    if (Cargo == nullptr)
        return 0;
    Cargo->RemoveFromStack(static_cast<unsigned>(Count));
    float const BayTime = static_cast<CShip*>(NarrowObject(this, CObject::ClassShip))->open_bay();
    ++BayCloseCount;
    BayCloseTimer = static_cast<float>(
        (static_cast<double>(BayTime) + CShip::JETTISON_CLEARANCE_TIME) + BayCloseTimer);
    return 0;
}

int FClientShipGameobj::set_target(IObjRW* Target, unsigned short SubTarget, int SourceClient)
{
    // LAB_005468e0 -- ported as CliShipSetTarget (cli_projectile_fx.cpp).
    return CliShipSetTarget(static_cast<IObjAffect*>(this), Target, SubTarget, SourceClient);
}

int FClientShipGameobj::toggle_cruise(bool bEngage, bool bArg2, int SourceClient)
{
    // LAB_00546a10 (IObjAffect slot 20) -- cruise.
    bool const bLocal =
        SourceClient == -1 || static_cast<unsigned>(SourceClient) == GetPlayerConnectionId();
    bool bChanged = false;
    if (SinglePlayer() && !bLocal)
        return -5;

    CEEngine* const Engine = CEEngine::cast(
        static_cast<CEqObj*>(NarrowObject(this, CObject::ClassShip))->EquipManager.FindFirst(EquipTypeEngine));
    if (Engine == nullptr)
        return -2;

    // Engaging from this client first stops the thrusters.
    if (Engine->bCruiseActive != static_cast<unsigned char>(bEngage))
    {
        bChanged = true;
        if (bEngage && bLocal)
            static_cast<IObjAffect*>(this)->toggle_thrusters(false,
                                                    static_cast<int>(GetPlayerConnectionId()));
        Engine->EngageCruise(bEngage, bArg2);
    }

    if (bChanged && bLocal)
    {
        XActivateCruise Info;
        Info.ObjectId = get_id();
        Info.bDisruptCruise = static_cast<unsigned char>(bArg2);
        Info.bActivate = bEngage;
        LocalServer()->ActivateCruise(GetPlayerConnectionId(), Info);
    }
    return 0;
}

int FClientShipGameobj::toggle_thrusters(bool bActivate, int SourceClient)
{
    // LAB_00546b50 (IObjAffect slot 21) -- the thrusters.
    bool const bLocal =
        SourceClient == -1 || static_cast<unsigned>(SourceClient) == GetPlayerConnectionId();
    bool bChanged = false;
    if (SinglePlayer() && !bLocal)
        return -5;

    // The engine is not cast here.
    CEquip* const Engine =
        static_cast<CEqObj*>(NarrowObject(this, CObject::ClassShip))->EquipManager.FindFirst(EquipTypeEngine);
    if (Engine == nullptr)
        return -5;
    unsigned char const bCruise = static_cast<CEEngine*>(Engine)->bCruiseActive;
    bool const bEngineActive = Engine->IsActive();
    if (bLocal)
    {
        if (bActivate && bCruise != 0)
            return -5;
        if (bActivate && !bEngineActive)
        {
            unsigned short EngineId = Engine->Id;
            static_cast<IObjAffect*>(this)->toggle_item(&EngineId, true, SourceClient);
        }
    }

    CEquipTraverser Walk(EquipTypeThruster);
    for (;;)
    {
        CEquip* const Thruster =
            static_cast<CEqObj*>(NarrowObject(this, CObject::ClassShip))->EquipManager.Traverse(Walk);
        if (Thruster == nullptr)
            break;
        if (Thruster->IsActive() == bActivate)
            continue;
        bChanged = true;
        Thruster->Activate(bActivate);
    }

    if (bChanged && bLocal)
    {
        XActivateThrusters Info;
        Info.ObjectId = get_id();
        Info.bActivate = bActivate;
        LocalServer()->ActivateThrusters(GetPlayerConnectionId(), Info);
    }
    return 0;
}

int FClientShipGameobj::request_event(int RequestType, unsigned const* TargetId,
                                unsigned const* SecondId, int bFlag, RequestCB* Callback)
{
    // LAB_00546df0. get_id is called after both ids are read.
    PendingRequestCB = Callback;
    unsigned const Second = *SecondId;
    unsigned const Target = *TargetId;
    unsigned const SelfId = get_id();
    LocalServer()->RequestEvent(RequestType, SelfId, Target, Second,
                                static_cast<unsigned long>(bFlag), GetPlayerConnectionId());
    return 0;
}

int FClientShipGameobj::request_cancel(int RequestType, unsigned const* TargetId, int Arg3)
{
    // LAB_00546e40
    PendingRequestCB = nullptr;
    unsigned const Target = *TargetId;
    unsigned const SelfId = get_id();
    LocalServer()->RequestCancel(RequestType, SelfId, Target, static_cast<unsigned long>(Arg3),
                                 GetPlayerConnectionId());
    return 0;
}

int FClientShipGameobj::go_tradelane(IObjInspect const* EntranceRing, IObjInspect const* ExitRing,
                                     bool bFastExit, float Speed)
{
    // LAB_00546e90 (IObjAffect slot 25). Only the exit ring is tested.
    if (ExitRing == nullptr)
        return -4;

    // No null test on the narrowed CShip.
    IObjRW* const Self = static_cast<IObjRW*>(this);
    CShip* const Ship = static_cast<CShip*>(NarrowObject(this, CObject::ClassShip));
    if (Ship->go_tradelane(EntranceRing, ExitRing, Self, bFastExit, Speed) != 0)
        return -5;

    // The ids are read in this order.
    XGoTradelane Info;
    Info.ObjectId = Self->get_id();
    Info.TradelaneRing1 = EntranceRing->get_id();
    Info.TradelaneRing2 = ExitRing->get_id();
    Info.bMidLane = bFastExit;
    LocalServer()->GoTradelane(GetPlayerConnectionId(), Info);

    // FUN_00515890's result is not used.
    FClientGameobj* const EntranceGo = ClientObjDb_FindObject(EntranceRing->get_id(), static_cast<int>(EClientGameobjKind::Solar));
    FClientGameobj* const ExitGo = ClientObjDb_FindObject(ExitRing->get_id(), static_cast<int>(EClientGameobjKind::Solar));
    TradeLaneShipGoHandler(this, EntranceGo, ExitGo);
    return 0;
}

int FClientShipGameobj::stop_tradelane()
{
    // LAB_00546f80 (IObjAffect slot 26). No null test on the narrowed CShip.
    CShip* const Ship = static_cast<CShip*>(NarrowObject(this, CObject::ClassShip));
    Ship->stop_tradelane();

    // The ship's own behavior manager (not null-tested) hears event 4 -- the same block the
    // server's notify_stop_tradelane builds.
    IBehaviorManager* const Behavior = static_cast<CEqObj*>(Ship)->get_behavior_interface();
    FBehaviorTradelaneEvent Block;
    Block.bEnteringLane = 0;
    Block.bExitRequested = 0;
    Block.bFastExit = 0;
    Block.Pad03 = 0;
    Block.Speed = 0.0f;
    Block.Field_0x08 = 0;
    Block.ShipId = get_id();
    Block.EntranceId = Ship->get_tradelane_entrance_ring();
    Block.ExitId = 0;
    FBehaviorManagerEvent Event;
    Event.Type = FBehaviorManagerEvent::kEventTradelane;
    Event.Data = &Block;
    Behavior->OnEvent(&Event);

    // Evaluated exit ring first, then entrance, then the id.
    unsigned const ExitId = Ship->get_tradelane_exit_ring();
    unsigned const EntranceId = Ship->get_tradelane_entrance_ring();
    unsigned const SelfId = get_id();
    LocalServer()->StopTradelane(GetPlayerConnectionId(), SelfId, EntranceId, ExitId);

    TradeLaneShipStop(this); // FUN_00515e20
    return 0;
}

int FClientShipGameobj::slot_6c(int ArgA, int ArgB)
{
    // FUN_00547250 (IObjAffect slot 27; ArgB is not read). The player's ship only: the voice
    // line for ArgA.
    (void)ArgB;
    if (GetPlayerObjRW() != this)
        return 0;
    unsigned VoiceId = 0;
    switch (ArgA)
    {
    case kShipVoiceDockTooFar:
        VoiceId = G_ShipVoiceDockTooFarId;
        break;
    case kShipVoiceTradeLaneDisrupted:
        VoiceId = G_ShipVoiceTradeLaneDisruptedId;
        break;
    case kShipVoiceFormationLost:
        VoiceId = G_ShipVoiceFormationLostId;
        break;
    default:
        return 0;
    }
    if (VoiceId != 0)
        NnVoiceEnqueue(VoiceId, 0);
    return 0;
}

void FClientShipGameobj::RequestReturned(int Result, int Code, int Extra)
{
    // FUN_00546d00
    if (PendingRequestCB == nullptr)
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kShipSourceFile, 0x863,
              "stale request detected");
        return;
    }
    if (!PendingRequestCB->OnResult(Result, Code, Extra))
        PendingRequestCB = nullptr;

    if (GetPlayerObjRW() != this)
        return;

    // The docking voice line.
    unsigned VoiceId = 0;
    if (Result == 0)
    {
        switch (Code)
        {
        case DOCK_HOST_RESPONSE_DENIED_HOSTILE:
            VoiceId = G_ShipVoiceDockDeniedHostileId;
            break;
        case DOCK_HOST_RESPONSE_ACCESS_DENIED:
            VoiceId = G_ShipVoiceDockDeniedLockedId;
            break;
        case DOCK_HOST_RESPONSE_NO_FITTING_TERMINAL:
        case DOCK_HOST_RESPONSE_NO_TERMINAL:
            VoiceId = G_ShipVoiceCannotDockId;
            break;
        case DOCK_HOST_RESPONSE_BUSY:
            VoiceId = G_ShipVoiceWaitingToDockId;
            break;
        case DOCK_HOST_RESPONSE_PROCEED:
            VoiceId = G_ShipVoiceDockGrantedId;
            break;
        default:
            return;
        }
    }
    else if (Result == 2)
    {
        switch (Code)
        {
        case DOCK_HOST_RESPONSE_DENIED_HOSTILE:
        case DOCK_HOST_RESPONSE_ACCESS_DENIED:
        case DOCK_HOST_RESPONSE_NO_FITTING_TERMINAL:
        case DOCK_HOST_RESPONSE_NO_TERMINAL:
            VoiceId = G_ShipVoiceCannotDockId;
            break;
        case DOCK_HOST_RESPONSE_PROCEED:
            VoiceId = G_ShipVoiceDockGrantedId;
            break;
        default:
            return;
        }
    }
    else
    {
        return;
    }
    if (VoiceId != 0)
        NnVoiceEnqueue(VoiceId, 0);
}

int FClientShipGameobj::launch_complete(unsigned BaseObjId)
{
    // LAB_005471f0: IServer::LaunchComplete.
    unsigned const SelfId = get_id();
    LocalServer()->LaunchComplete(BaseObjId, SelfId);
    return 0;
}

int FClientShipGameobj::jump_in_complete(unsigned SystemId)
{
    // LAB_00547220: IServer::JumpInComplete.
    unsigned const SelfId = get_id();
    LocalServer()->JumpInComplete(SystemId, SelfId);
    return 0;
}

void FClientShipGameobj::RequestLand(FClientGameobj* DockGo, unsigned const* BaseId)
{
    // FUN_00543b40
    ShipFlags |= kShipFlagLanding;

    if (DockGo != nullptr)
    {
        // Action_Land (FUN_00542b70) with the dock's object narrowed to a CEqObj (null
        // otherwise).
        CObject* const DockObject = DockGo->Object;
        CEqObj* Dock = nullptr;
        if (DockObject != nullptr &&
            (static_cast<unsigned>(DockObject->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar)
            Dock = static_cast<CEqObj*>(DockObject);
        Actions.add(new Action_Land(this, Dock));

        Vector Zero;
        Zero.x = 0.0f;
        Zero.y = 0.0f;
        Zero.z = 0.0f;
        static_cast<IObjAffect*>(this)->set_axis_throttle(Zero);

        // The camera only follows the watched player ship.
        if (static_cast<void*>(static_cast<IObjRW*>(this)) != GetPlayerObjRW())
            return;

        // Unless the docking camera already has the view, it goes to the dock's "HPDockCam"
        // 'A' hardpoint (get_hardpoint returns 0 on success). A failed lookup leaves the camera
        // alone.
        if (!IsDockingCameraCurrent())
        {
            Transform Csys;
            Csys.Rotation = Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
            Csys.Translation.x = 0.0f;
            Csys.Translation.y = 0.0f;
            Csys.Translation.z = 0.0f;
            char HardpointName[0x40];
            sprintf(HardpointName, "%s%c", "HPDockCam", 'A');
            if (DockGo->get_hardpoint(HardpointName, &Csys.Translation, &Csys.Rotation) == 0)
            {
                CloseRearAndTurretViewsForDocking();
                SetDockingCameraTransform(reinterpret_cast<const float*>(&Csys));
            }
        }

        // Always: the docking camera's fade over DAT_005dfb34 seconds.
        BeginDockingCameraSequence(3.0f);
        return;
    }

    // No dock.
    if (static_cast<void*>(static_cast<IObjRW*>(this)) == GetPlayerObjRW())
    {
        // The player's ship: an ERROR for base 0, the base asked for anyway, and the hard cut
        // to a covered screen.
        if (*BaseId == 0)
        {
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", kShipSourceFile, 0x2a8,
                  "Cannot land on BASE_ID_NONE");
        }
        SetPendingLandBase(BaseId);
        SetFadeImmediate(1.0f);
        return;
    }
    // Any other ship: slot 87 with true.
    MarkForRemoval(true);
}

void FClientGameobj::SetOwnerPlayer(unsigned PlayerId)
{
    // FUN_005397f0: no null test on the narrowed object.
    static_cast<CSimple*>(NarrowObject(this, CObject::ClassSimple))->SetOwnerPlayer(PlayerId);
}

void FClientShipGameobj::SetJumpState(int State)
{
    // FUN_005437d0. The player test is made afresh at every use, as retail does. No null test
    // on the narrowed CShip anywhere.
    switch (State)
    {
    case kJumpStateNone:
    {
        if (JumpEffect != nullptr)
        {
            delete static_cast<FJumpShipEffect*>(JumpEffect);
            JumpEffect = nullptr;
        }
        // Leaving state 5 on the player's ship.
        if (JumpState == kJumpStateArrived &&
            static_cast<void*>(static_cast<IObjRW*>(this)) == GetPlayerObjRW())
        {
            static_cast<CShip*>(NarrowObject(this, CObject::ClassShip))->set_control_exclusion(0);
            G_bNnScreenUp = false; // DAT_0067dcc8
        }
        break;
    }
    case kJumpStateJumpOut:
    {
        if (JumpEffect != nullptr)
            static_cast<FJumpShipEffect*>(JumpEffect)->StartJumpOut(); // FUN_00503ea0
        if (static_cast<void*>(static_cast<IObjRW*>(this)) == GetPlayerObjRW())
            RequestSpaceCameraMode("player");
        // Control exclusion 7 (after a 0 when 7 is already set).
        if (static_cast<void*>(static_cast<IObjRW*>(this)) == GetPlayerObjRW())
        {
            CShip* const Ship = static_cast<CShip*>(NarrowObject(this, CObject::ClassShip));
            if (Ship->is_control_excluded(7))
                Ship->set_control_exclusion(0);
            Ship->set_control_exclusion(7);
            G_bNnScreenUp = true; // DAT_0067dcc8
        }
        // Every ship drops its target.
        static_cast<CShip*>(NarrowObject(this, CObject::ClassShip))->set_target(nullptr);
        break;
    }
    case kJumpStateJumpOutTunnel:
    {
        if (JumpEffect != nullptr)
            static_cast<FJumpShipEffect*>(JumpEffect)->EnterTunnel(); // FUN_00504210
        IAction* const JumpOut = Actions.find("JumpOut");
        if (JumpOut != nullptr)
            Actions.remove(JumpOut);
        // Any ship but the player's is removed; in single player its CShip also gets an
        // AddRef/Release probe, and -- when that Release does not return 1 -- no owner and no
        // control exclusion.
        if (static_cast<void*>(static_cast<IObjRW*>(this)) != GetPlayerObjRW())
        {
            MarkForRemoval(true);
            if (SinglePlayer())
            {
                CShip* const Ship = static_cast<CShip*>(NarrowObject(this, CObject::ClassShip));
                Ship->bJumpedOut = 1;               // +0x214
                Ship->AddRef();
                if (Ship->Release() != 1)
                {
                    Ship->SetOwnerPlayer(0);
                    Ship->set_control_exclusion(0);
                }
            }
        }
        break;
    }
    case kJumpStateJumpInTunnel:
    {
        // Retail first calls the effect's hook, which the linker folded into an empty function
        // (0x00401e90) -- nothing to do. The destination system (+0xa4) becomes the pending
        // jump system (FUN_00548ed0).
        if (JumpEffect != nullptr)
        {
            unsigned const DestSystem = static_cast<FJumpShipEffect*>(JumpEffect)->DestSystem;
            SetPendingJumpSystem(&DestSystem, false);
        }
        break;
    }
    case kJumpStateJumpIn:
    {
        // The player's character jumps in at the effect's exit frame and the effect's jump-in
        // hook runs; any other ship is just beamed there.
        FJumpShipEffect* const Effect = static_cast<FJumpShipEffect*>(JumpEffect);
        if (Effect != nullptr)
        {
            if (get_id() == GetPlayerCharacterId())
            {
                float const Speed = Effect->JumpInTime;
                static_cast<CShip*>(NarrowObject(this, CObject::ClassShip))
                    ->jump_in(static_cast<IObjRW*>(this), Effect->JumpInPosition,
                              Effect->JumpInOrientation, Speed);
                static_cast<FJumpShipEffect*>(JumpEffect)->StartJumpIn();
            }
            else
            {
                NarrowObject(this, CObject::ClassShip)
                    ->beam_object(Effect->JumpInPosition, Effect->JumpInOrientation, false);
            }
        }
        static_cast<CShip*>(NarrowObject(this, CObject::ClassShip))->set_target(nullptr);
        bJumpInReady = 0;
        break;
    }
    case kJumpStateArrived:
    {
        // FUN_00504640 with no null test; the player's character gets the arrival caption
        // (FUN_0045b310).
        static_cast<FJumpShipEffect*>(JumpEffect)->FinishJump();
        if (get_id() == GetPlayerCharacterId())
            NnArrivalTextShow();
        break;
    }
    default:
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kShipSourceFile, 0x272,
              "invalid jump state set in ship");
        break;
    }

    if (JumpEffect != nullptr)
        static_cast<FJumpShipEffect*>(JumpEffect)->Clock = 0.0f;
    JumpState = State;
}

void FClientShipGameobj::BeginJumpOut(FClientGameobj* Gate)
{
    // FUN_00543530
    if (JumpEffect != nullptr)
    {
        delete static_cast<FJumpShipEffect*>(JumpEffect);
        JumpEffect = nullptr;
    }
    // operator new(0xdc) + FUN_00503440; null on a failed new.
    JumpEffect = new (std::nothrow) FJumpShipEffect(this, static_cast<FClientSolarGameobj*>(Gate));

    unsigned GateType = 0;
    if (Gate != nullptr)
        Gate->get_type(GateType);

    SetJumpState(kJumpStateJumpOut);

    // Action_JumpOut (FUN_00542f10); the effect is not null-tested.
    Actions.add(new Action_JumpOut(this, static_cast<FJumpShipEffect*>(JumpEffect)->JumpOutTime,
                                   GateType));
}

void FClientShipGameobj::BeginJumpIn(Vector const* Pos, Matrix const* Orient)
{
    // FUN_00543630
    bJumpInReady = 1;

    // The player's ship: its effect (no null test) takes the exit frame and the active
    // camera's cut flag is raised (FUN_0054bd50).
    if (static_cast<void*>(static_cast<IObjRW*>(this)) == GetPlayerObjRW())
    {
        FJumpShipEffect* const Effect = static_cast<FJumpShipEffect*>(JumpEffect);
        Effect->JumpInPosition = *Pos;
        Effect->JumpInOrientation = *Orient;
        RaiseActiveSpaceCameraCut();
        return;
    }

    // Any other ship gets a fresh no-gate effect (FUN_005038c0; not null-tested).
    if (JumpEffect != nullptr)
    {
        delete static_cast<FJumpShipEffect*>(JumpEffect);
        JumpEffect = nullptr;
    }
    JumpEffect = new (std::nothrow) FJumpShipEffect(this);
    FJumpShipEffect* const Effect = static_cast<FJumpShipEffect*>(JumpEffect);
    Effect->JumpInPosition = *Pos;
    Effect->JumpInOrientation = *Orient;
    SetJumpState(kJumpStateJumpIn);
}

void FClientShipGameobj::RequestSystemSwitch(FClientGameobj* Gate)
{
    // FUN_00543750: no null test on the narrowed gate.
    CSolar const* const Solar = static_cast<CSolar const*>(NarrowObject(Gate, CObject::ClassSolar));
    unsigned const DestSystem = Solar->DestSystem;
    SetPendingJumpSystem(&DestSystem, false);
}

void FClientShipGameobj::AirlockIn(Vector const& Pos, Matrix const& Orient)
{
    // FUN_005437a0: no null test on the narrowed object.
    static_cast<CShip*>(NarrowObject(this, CObject::ClassShip))->airlock_in(Pos, Orient);
}

// The launch camera's Csys: the orientation followed directly by the position --
// FUN_0054bb90's twelve floats.
static_assert(offsetof(Transform, Translation) == 0x24, "Csys12: position after 3x3");
static_assert(sizeof(Transform) == 0x30, "Csys12: twelve floats");

void FClientShipGameobj::LaunchFromDock(FClientGameobj* DockGo, int LaunchIndex)
{
    // FUN_00543d50
    if (get_id() != GetPlayerCharacterId())
        return;

    CEqObj* DockEqObj = nullptr;
    if (DockGo != nullptr)
    {
        CObject* const DockObj = DockGo->Object;
        if (DockObj != nullptr &&
            (static_cast<unsigned>(DockObj->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj)
            DockEqObj = static_cast<CEqObj*>(DockObj);
    }

    // No null test on the narrowed CShip.
    CShip* const Ship = static_cast<CShip*>(NarrowObject(this, CObject::ClassShip));
    Ship->launch(this, DockEqObj, LaunchIndex);

    StarSysSuppressPlayerCollisions(7.0);

    // The camera only follows the watched player ship.
    if (static_cast<void*>(static_cast<IObjRW*>(this)) != GetPlayerObjRW())
        return;

    // Initialised once, before the first lookup. Both get_hardpoint calls write into the same
    // pair, so a failed first lookup leaves whatever it wrote for the second.
    Transform Csys;
    Csys.Rotation = Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    Csys.Translation.x = 0.0f;
    Csys.Translation.y = 0.0f;
    Csys.Translation.z = 0.0f;

    char HardpointName[0x40];
    int const Suffix = LaunchIndex + 'A';
    sprintf(HardpointName, "%s%c", "HPLaunchCam", Suffix);

    // 0 is success. No null test on the dock here.
    bool bFound = DockGo->get_hardpoint(HardpointName, &Csys.Translation, &Csys.Rotation) == 0;
    if (!bFound)
    {
        sprintf(HardpointName, "%s%c", "HPDockCam", Suffix);
        bFound = DockGo->get_hardpoint(HardpointName, &Csys.Translation, &Csys.Rotation) == 0;
    }

    CloseRearAndTurretViewsForDocking(); // on both arms
    if (bFound)
    {
        SetDockingCameraTransform(reinterpret_cast<const float*>(&Csys));
    }
    else
    {
        RequestSpaceCameraMode("player");
    }
}

void CloseRearAndTurretViewsForDocking()
{
    // FUN_00549e30. DAT_00674aa0 is read twice with no null test; each byte is compared with 1.
    if (GetHudStatus()->bRearView == 1)
        HudPostCommand(USER_REARVIEW_CAMERA_MODE_OFF, 0);
    if (GetHudStatus()->bTurretView == 1)
        HudPostCommand(USER_AUTO_TURRET, 0);
}

// ===========================================================================
// Projectile
// ===========================================================================

FClientProjectileGameobj::FClientProjectileGameobj(EClientGameobjKind Kind)
    : FClientSimpleGameobj(Kind)
{
    // FUN_00542550
    ConstEffectDelay = -1.0f;
    OneShotCompanion[0] = nullptr;
    OneShotCompanion[1] = nullptr;
    OneShotCompanion[2] = nullptr;
    OneShotCompanion[3] = nullptr;
}

FClientProjectileGameobj::~FClientProjectileGameobj()
{
    // FUN_005425f0: retail releases the companion in the destructor body and again in the
    // member's own destructor.
    CliEffectCompanionClear(OneShotCompanion);
    CliEffectCompanionClear(OneShotCompanion);
}

namespace
{
    // The const_effect start shared by Projectile slots 92 and 93: the effect under the
    // companion's +0x58 id, into OneShotCompanion. The id is tested for emptiness first, as
    // both slots do right before the call.
    void ProjectileStartConstEffect(FClientProjectileGameobj* Self, ID_String const* Effect,
                                    long EngineIndex, Transform const& Xform)
    {
        if (Effect->IsEmpty())
            return;
        CliCreateEffectOneShot(FindEffectArch(&Effect->Id), Self, EngineIndex,
                               &Xform,
                               Self->OneShotCompanion);
    }
}

void FClientProjectileGameobj::PostCreateInit()
{
    // FUN_005426e0
    FClientGameobj::PostCreateInit();
    // No null test on the narrowed object.
    Archetype::ICliObj* const Companion = NarrowObject(this, CObject::ClassProjectile)->Archetype->Companion;
    ID_String const* const Effect = Archetype::CliProjConstEffect(Companion);
    if (Effect->IsEmpty())
        return;
    // A delay other than exactly 0 (NaN included) is armed.
    float const Delay = Archetype::CliProjConstEffectDelay(Companion);
    if (!(Delay == 0.0f))
    {
        ConstEffectDelay = Delay;
        return;
    }
    Transform Xform;
    GetConstEffectTransform(&Xform);
    long const EngineIndex = get_index();
    ProjectileStartConstEffect(this, Effect, EngineIndex, Xform);
}

void FClientProjectileGameobj::update(float Dt)
{
    // FUN_00542790
    if (IsDead())
        return;
    FClientGameobj::update(Dt);
    // A delay below 0 (or NaN) is not armed.
    if (!(ConstEffectDelay >= 0.0f))
        return;
    // Go on only at or below 0 (not NaN).
    ConstEffectDelay = ConstEffectDelay - Dt;
    if (!(ConstEffectDelay <= 0.0f))
        return;
    ConstEffectDelay = -1.0f;
    Transform Xform;
    GetConstEffectTransform(&Xform);
    // No null test on the narrowed object.
    Archetype::ICliObj* const Companion = NarrowObject(this, CObject::ClassProjectile)->Archetype->Companion;
    long const EngineIndex = get_index();
    ProjectileStartConstEffect(this, Archetype::CliProjConstEffect(Companion), EngineIndex,
                               Xform);
}

void FClientProjectileGameobj::OnSurfaceCollisionHit(void const* Contact, void const* B,
                                       FClientGameobj* Other, void const* D)
{
    // FUN_0053d4f0: empty.
    (void)Contact;
    (void)B;
    (void)Other;
    (void)D;
}

void FClientProjectileGameobj::Explode(Vector const* Pos)
{
    // FUN_00542870
    CliEffectListRemove(&EffectList, OneShotCompanion);
    FClientGameobj::Explode(Pos);
}

void FClientProjectileGameobj::GetConstEffectTransform(Transform* Out)
{
    // LAB_005428a0
    Out->Rotation = Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    Out->Translation.x = 0.0f;
    Out->Translation.y = 0.0f;
    Out->Translation.z = 0.0f;
}

// ===========================================================================
// Munition
// ===========================================================================

FClientMunitionGameobj::FClientMunitionGameobj(EClientGameobjKind Kind)
    : FClientProjectileGameobj(Kind)
{
    // FUN_0052e100
}

FClientMunitionGameobj::~FClientMunitionGameobj()
{
    // The thunk FUN_0053d470 -> FUN_005425f0: nothing of its own.
}

void FClientMunitionGameobj::OnCollision(void const* A, void const* B, FClientGameobj* Other,
                                     void const* D)
{
    // FUN_0053ed80
    Other->OnMunitionHit(A, D, this);
    ReportCollisionToServer(A, B, Other, D);
}

// ===========================================================================
// Beam
// ===========================================================================

FClientBeamGameobj::FClientBeamGameobj()
    : FClientMunitionGameobj(EClientGameobjKind::Beam)
{
    // Inlined in FUN_0052dde0. FUN_005154a0 with four nulls.
    Companion[0] = nullptr;
    Companion[1] = nullptr;
    Companion[2] = nullptr;
    Companion[3] = nullptr;
    BeamAlloc = nullptr;
}

FClientBeamGameobj::~FClientBeamGameobj()
{
    // FUN_0053f1e0: the companion is released, then the beam effect, then the companion again
    // by its member destructor.
    CliEffectCompanionClear(Companion);
    if (BeamAlloc != nullptr)
    {
        BeamAlloc->Release();
        BeamAlloc = nullptr;
    }
    CliEffectCompanionClear(Companion);
}

void FClientBeamGameobj::create(Archetype::Root* Arch)
{
    // FUN_0053f2f0. No null test on the narrowed object.
    CreateOfClass(CObject::ClassBeam, Arch);
    NarrowObject(this, CObject::ClassBeam)->open(Arch);
}

bool FClientBeamGameobj::find(unsigned const& Id)
{
    // FUN_0053f330
    (void)Id;
    return false;
}

void FClientBeamGameobj::update(float Dt)
{
    // LAB_0053f610
    FClientProjectileGameobj::update(Dt);
    // No null test; result dropped.
    NarrowObject(this, CObject::ClassProjectile)->update(Dt, CObject::UPDATE_DEFAULT);
    if (IsExploding())
        return;
    // A NaN life also expires the beam.
    CProjectile const* const Projectile = static_cast<CProjectile const*>(NarrowObject(this, CObject::ClassProjectile));
    if (!(Projectile->Lifetime >= 0.0f))
        MarkForRemoval(true);
}

void FClientBeamGameobj::OnCollision(void const* A, void const* B, FClientGameobj* Other,
                                 void const* D)
{
    // LAB_0053f690
    if (IsExploding())
        return;
    Other->OnMunitionHit(A, D, this);
    ReportCollisionToServer(A, B, Other, D);
    bool const bOtherIsPlayer = static_cast<void*>(static_cast<IObjRW*>(Other)) == GetPlayerObjRW();
    // The CBeam's velocity (+0xf8) zeroed -- no null test.
    CBeam* const BeamObj = static_cast<CBeam*>(NarrowObject(this, CObject::ClassBeam));
    BeamObj->Velocity.x = 0.0f;
    BeamObj->Velocity.y = 0.0f;
    BeamObj->Velocity.z = 0.0f;
    if (BeamAlloc != nullptr)
    {
        BeamAlloc->SetHitFlags(true, bOtherIsPlayer); // FUN_004fc8f0
        BeamAlloc->Release();
        BeamAlloc = nullptr;
    }
    CliEffectListRemove(&EffectList, Companion);
    FClientProjectileGameobj::Explode(static_cast<Vector const*>(A));
}

void FClientBeamGameobj::OnMunitionHit(void const* A, void const* B, FClientGameobj* Munition)
{
    // LAB_0053f780: empty.
    (void)A;
    (void)B;
    (void)Munition;
}

void FClientBeamGameobj::ReportCollisionToServer(void const* A, void const* B, FClientGameobj* Other,
                                 void const* D)
{
    // LAB_0053f790. B is not read.
    (void)B;
    // In multiplayer only the player's own shots report (owner against DAT_00673378; no null
    // test on the narrowed object).
    if (!SinglePlayer())
    {
        CProjectile const* const Shot = static_cast<CProjectile const*>(NarrowObject(this, CObject::ClassProjectile));
        if (Shot->OwnerId != GetPlayerCharacterId())
            return;
    }
    CObject* const Obj = NarrowObject(this, CObject::ClassProjectile);
    Vector const* const HitPos = static_cast<Vector const*>(A);
    SSPMunitionCollisionInfo Info;
    Info.ProjectileArchId = Obj->Archetype->Nickname.Id;
    Info.AttackerId = static_cast<CProjectile const*>(Obj)->OwnerId;
    Info.ObjectId = Other->get_id();
    // The low word of D, the sub-object that was hit. Retail never writes +0x0e.
    Info.SubObjId = static_cast<unsigned short>(reinterpret_cast<uintptr_t>(D));
    Info.Pad_0x0e = 0;
    std::memcpy(&Info.HitPosX, &HitPos->x, sizeof(float));
    std::memcpy(&Info.HitPosY, &HitPos->y, sizeof(float));
    std::memcpy(&Info.HitPosZ, &HitPos->z, sizeof(float));
    // IServer slot 53, no null test.
    LocalServer()->SPMunitionCollision(Info, GetPlayerConnectionId());
}

unsigned FClientBeamGameobj::GetTypeBits() const
{
    // LAB_0052e130
    return OBJ_BULLET;
}

// ===========================================================================
// Mine / CounterMeasure / Guided / Loot -- shared bodies
// ===========================================================================

namespace
{
    // .rdata:005df7e0 / 005df858 -- the FDUMP file arguments of Loot slot 92 and Guided slot
    // 112.
    char const kLootSourceFile[] = "E:\\FL\\Scratch\\Source\\Client\\Space\\Loot.cpp";
    char const kMunitionSourceFile[] = "E:\\FL\\Scratch\\Source\\Client\\Space\\Munition.cpp";

    // The object-update tail of Loot / Mine / Guided / CounterMeasure slot 93. This
    // connection's own object runs UPDATE_DEFAULT; anyone else's runs UPDATE_DUMB, and only in
    // multiplayer. A failed owner narrow counts as this connection's. The Mask-narrowed object
    // is used with no null test.
    void GameobjOwnerUpdate(FClientGameobj* Self, unsigned Mask, float Dt)
    {
        CObject* const Owned = NarrowObject(Self, CObject::ClassSimple);
        unsigned const Owner = (Owned != nullptr) ? static_cast<CSimple const*>(Owned)->OwnerPlayer
                                                  : GetPlayerConnectionId();
        if (Owner == GetPlayerConnectionId())
        {
            NarrowObject(Self, Mask)->update(Dt, CObject::UPDATE_DEFAULT);
            return;
        }
        if (SinglePlayer())
            return;
        NarrowObject(Self, Mask)->update(Dt, CObject::UPDATE_DUMB);
    }

    // FUN_0053ea60 (Dt unread) -- Mine, CounterMeasure and Guided slot 110. Only an
    // exactly-zero +0x34 goes on (NaN does not).
    void ProjectileLazyNebula(FClientGameobj* Self)
    {
        if (!(Self->NebulaUpdateTimer == 0.0f))
            return;
        FNebulaEffect* const Nebula = StarSysFindNebula(&Self->get_position(), &Self->NebulaFogFactor, nullptr);
        Self->CurrentNebula = Nebula;
        Self->NebulaUpdateTimer = 1.0f;
    }

    // LAB_0053eaa0 -- Mine and Guided slot 105: only a munition whose object is a CBeam goes on
    // to GameObject's slot 105.
    bool IsBeamMunition(FClientGameobj const* Munition)
    {
        if (Munition == nullptr)
            return false;
        return NarrowObject(Munition, CObject::ClassBeam) != nullptr;
    }
}

// ---- Mine ------------------------------------------------------------------

FClientMineGameobj::FClientMineGameobj()
    : FClientProjectileGameobj(EClientGameobjKind::Mine)
{
    // Inlined in FUN_0053ca40.
}

FClientMineGameobj::~FClientMineGameobj()
{
    // FUN_0053d440: Projectile's body; nothing of Mine's own.
}

Vector FClientMineGameobj::get_velocity() const
{
    // LAB_0053eae0: a destroyed mine (Flags bit 2) answers {0,0,0}.
    if ((Flags & 4u) != 0)
    {
        Vector Zero;
        Zero.x = 0.0f;
        Zero.y = 0.0f;
        Zero.z = 0.0f;
        return Zero;
    }
    return IObjInspectImpl::get_velocity();
}

void FClientMineGameobj::create(Archetype::Root* Arch)
{
    // FUN_0053e930
    CreateOfClass(CObject::ClassMine, Arch);
    NarrowObject(this, CObject::ClassMine)->open(Arch);
}

bool FClientMineGameobj::find(unsigned const& Id)
{
    // FUN_0053e970
    Object = CObject::Find(Id, CObject::ClassMine);
    advise(true);
    return Object != nullptr;
}

void FClientMineGameobj::update(float Dt)
{
    // FUN_0053e9a0
    FClientProjectileGameobj::update(Dt);
    GameobjOwnerUpdate(this, CObject::ClassMine, Dt);
}

void FClientMineGameobj::OnMunitionHit(void const* A, void const* B, FClientGameobj* Munition)
{
    // LAB_0053eaa0
    if (!IsBeamMunition(Munition))
        return;
    FClientGameobj::OnMunitionHit(A, B, Munition);
}

void FClientMineGameobj::UpdateNebula(float Dt)
{
    // FUN_0053ea60
    (void)Dt;
    ProjectileLazyNebula(this);
}

// ---- CounterMeasure ----------------------------------------------------------

FClientCounterMeasureGameobj::FClientCounterMeasureGameobj()
    : FClientProjectileGameobj(EClientGameobjKind::CounterMeasure)
{
    // Inlined in FUN_0053cd70.
}

FClientCounterMeasureGameobj::~FClientCounterMeasureGameobj()
{
    // FUN_0053d440: Projectile's body.
}

void FClientCounterMeasureGameobj::create(Archetype::Root* Arch)
{
    // FUN_0052ff90
    CreateOfClass(CObject::ClassCounterMeasure, Arch);
    NarrowObject(this, CObject::ClassCounterMeasure)->open(Arch);
}

bool FClientCounterMeasureGameobj::find(unsigned const& Id)
{
    // FUN_0052ffd0
    Object = CObject::Find(Id, CObject::ClassCounterMeasure);
    advise(true);
    return Object != nullptr;
}

void FClientCounterMeasureGameobj::update(float Dt)
{
    // FUN_00530000
    FClientProjectileGameobj::update(Dt);
    GameobjOwnerUpdate(this, CObject::ClassCounterMeasure, Dt);
}

void FClientCounterMeasureGameobj::OnMunitionHit(void const* A, void const* B, FClientGameobj* Munition)
{
    // LAB_0053f780: empty.
    (void)A;
    (void)B;
    (void)Munition;
}

void FClientCounterMeasureGameobj::UpdateNebula(float Dt)
{
    // FUN_0053ea60
    (void)Dt;
    ProjectileLazyNebula(this);
}

// ---- Guided ----------------------------------------------------------------

FClientGuidedGameobj::FClientGuidedGameobj()
    : FClientMunitionGameobj(EClientGameobjKind::Guided)
{
    // Inlined in FUN_0053d080: Projectile's ctor; Munition's ctor FUN_0052e100 adds only its
    // vtables, which Guided's overwrite at once.
}

FClientGuidedGameobj::~FClientGuidedGameobj()
{
    // FUN_0053d440: Projectile's body.
}

void FClientGuidedGameobj::create(Archetype::Root* Arch)
{
    // FUN_0053ec30
    CreateOfClass(CObject::ClassGuided, Arch);
    NarrowObject(this, CObject::ClassGuided)->open(Arch);
}

bool FClientGuidedGameobj::find(unsigned const& Id)
{
    // FUN_0053ec70
    Object = CObject::Find(Id, CObject::ClassGuided);
    advise(true);
    return Object != nullptr;
}

void FClientGuidedGameobj::update(float Dt)
{
    // FUN_0053ecb0
    FClientProjectileGameobj::update(Dt);
    GameobjOwnerUpdate(this, CObject::ClassGuided, Dt);
}

void FClientGuidedGameobj::OnMunitionHit(void const* A, void const* B, FClientGameobj* Munition)
{
    // LAB_0053eaa0 -- Mine's slot 105.
    if (!IsBeamMunition(Munition))
        return;
    FClientGameobj::OnMunitionHit(A, B, Munition);
}

void FClientGuidedGameobj::ReportCollisionToServer(void const* A, void const* B, FClientGameobj* Other,
                                   void const* D)
{
    // LAB_0053ee00. B is not read.
    (void)B;
    // In multiplayer only a collision with an object this connection owns reports.
    if (!SinglePlayer())
    {
        CObject* const OtherObj = NarrowObject(Other, CObject::ClassSimple);
        unsigned const Owner = (OtherObj != nullptr)
                                   ? static_cast<CSimple const*>(OtherObj)->OwnerPlayer
                                   : GetPlayerConnectionId();
        if (Owner != GetPlayerConnectionId())
            return;
    }
    // No null test on the narrowed object.
    CObject* const Obj = NarrowObject(this, CObject::ClassProjectile);
    Vector const* const HitPos = static_cast<Vector const*>(A);
    SSPMunitionCollisionInfo Info;
    Info.AttackerId = static_cast<CProjectile const*>(Obj)->OwnerId;
    Info.ProjectileArchId = Obj->Archetype->Nickname.Id;
    Info.ObjectId = Other->get_id();
    Info.SubObjId = static_cast<unsigned short>(reinterpret_cast<uintptr_t>(D));
    Info.Pad_0x0e = 0; // retail never writes +0x0e
    std::memcpy(&Info.HitPosX, &HitPos->x, sizeof(float));
    std::memcpy(&Info.HitPosY, &HitPos->y, sizeof(float));
    std::memcpy(&Info.HitPosZ, &HitPos->z, sizeof(float));
    LocalServer()->SPMunitionCollision(Info, GetPlayerConnectionId());
}

void FClientGuidedGameobj::Explode(Vector const* Pos)
{
    // FUN_0053edc0. The const_effect particle is let go (no owner, no part) at this gameobj's
    // frame. Slot 4 (get_orientation) is ICF-folded with slot 3 (get_transform) in retail
    // (both are Common 0x5F1F0), so the pointer it hands over is the object's whole Transform.
    FParticleEffect* const Particle = static_cast<FParticleEffect*>(OneShotCompanion[2]);
    if (Particle != nullptr)
    {
        Transform const& Frame = get_transform();
        Particle->Attach(nullptr, -1, &Frame);
    }
    FClientProjectileGameobj::Explode(Pos);
}

void FClientGuidedGameobj::UpdateNebula(float Dt)
{
    // FUN_0053ea60
    (void)Dt;
    ProjectileLazyNebula(this);
}

unsigned FClientGuidedGameobj::GetTypeBits() const
{
    // LAB_0053d430
    return OBJ_GUIDED;
}

void FClientGuidedGameobj::GetConstEffectTransform(Transform* Out)
{
    // LAB_0053f030
    Transform Local;
    Local.Rotation = Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    Local.Translation.x = 0.0f;
    Local.Translation.y = 0.0f;
    Local.Translation.z = 0.0f;

    // The CliMunition companion's HP_trail_parent (+0x60); no null test on the narrowed
    // object.
    char const* const TrailParent =
        Archetype::CliMunitionHpTrailParent(NarrowObject(this, CObject::ClassProjectile)->Archetype->Companion);
    if (TrailParent[0] != '\0')
    {
        long Handle = -1;
        HardpointInfo Hp;
        if (FindHardpoint_PS(get_index(), TrailParent, Handle, Hp))
        {
            // The renderer is not asked.
            Local.Translation = Hp.Position;
            Local.Rotation = Hp.Orient;
            *Out = Local;
            return;
        }
        // The text is read again for the error; then on to the renderer.
        char const* const TrailParentAgain =
            Archetype::CliMunitionHpTrailParent(NarrowObject(this, CObject::ClassProjectile)->Archetype->Companion);
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s: Missing HP_trail_parent(%s).",
              kMunitionSourceFile, 0xe1, Object->get_archetype()->Nickname.get_string(),
              TrailParentAgain);
    }

    // The archetype mesh's bounds on the renderer: when both answer, the translation is
    // {centre x, centre y, extents[4]}.
    float Extents[6];
    const ArchetypeIndex MeshId =
        (Object->get_archetype()->EngineIndex);
    if (DALib::Renderer->GetBounds(MeshId, 1.0f, Extents) != 0)
    {
        float Center[3];
        const ArchetypeIndex MeshIdAgain =
            (Object->get_archetype()->EngineIndex);
        if (DALib::Renderer->GetCentroid(MeshIdAgain, 1.0f, Center) != 0)
        {
            Local.Translation.x = Center[0];
            Local.Translation.y = Center[1];
            Local.Translation.z = Extents[4];
        }
    }
    *Out = Local;
}

int FClientGuidedGameobj::set_target(IObjRW* Target, unsigned short SubTarget, int SourceClient)
{
    // LAB_0053eed0 (`this` is the IObjAffect sub-object in retail). The narrowed object is
    // used with no null test.
    CGuided* const Guided = static_cast<CGuided*>(NarrowObject(this, CObject::ClassGuided));
    // A munition with no seeker (+0x90) answers -3.
    Archetype::Munition const* const MunArch =
        static_cast<Archetype::Munition const*>(Guided->Archetype);
    if (MunArch->Seeker == 0)
        return -3;

    // The watcher (+0xf0) holds the target's Watchable, IObjRW + 8.
    Watchable const* const Watched = Guided->TargetWatch.target();
    IObjRW const* const Current =
        (Watched != nullptr)
            ? reinterpret_cast<IObjRW const*>(reinterpret_cast<unsigned char const*>(Watched) - 8)
            : nullptr;
    if (Current == Target && Guided->SubTarget == SubTarget)
        return 0;

    bool const bOwnRequest =
        (SourceClient == -1) || (static_cast<unsigned>(SourceClient) == GetPlayerConnectionId());
    // In single player a foreign request changes nothing.
    if (SinglePlayer() && !bOwnRequest)
        return 0;

    // The seeker is re-read before each of the two stores.
    if (static_cast<Archetype::Munition const*>(Guided->Archetype)->Seeker != 0)
    {
        Watchable const* const NewWatched =
            (Target != nullptr) ? reinterpret_cast<Watchable const*>(
                                      reinterpret_cast<unsigned char const*>(Target) + 8)
                                : nullptr;
        Guided->TargetWatch.set(NewWatched);
    }
    if (static_cast<Archetype::Munition const*>(Guided->Archetype)->Seeker != 0)
        Guided->SubTarget = SubTarget;

    // This connection's request goes to the server (IServer slot 4). Retail never writes the
    // pad words.
    if (bOwnRequest)
    {
        XSetTarget Info;
        Info.ObjectId = get_id();
        Info.SubObjId = SUBOBJ_ID_NONE;
        Info.Pad_0x06 = 0;
        Info.TargetObjectId = (Target != nullptr) ? Target->get_id() : 0u;
        Info.TargetSubObjId = SubTarget;
        Info.Pad_0x0e = 0;
        LocalServer()->SetTarget(GetPlayerConnectionId(), Info);
    }
    return 0;
}

// ---- Loot ------------------------------------------------------------------

FClientLootGameobj::FClientLootGameobj()
    : FClientSimpleGameobj(EClientGameobjKind::Loot)
{
    // FUN_0053e470
    TractoredExplosionArch = nullptr;
}

FClientLootGameobj::~FClientLootGameobj()
{
    // FUN_0053e4e0 (ESI = this + 0xc).
    if (TractoredExplosionArch != nullptr)
        ExplosionArchRelease(TractoredExplosionArch);
}

void FClientLootGameobj::create(Archetype::Root* Arch)
{
    // LAB_0053e550
    CreateOfClass(CObject::ClassLoot, Arch);
    NarrowObject(this, CObject::ClassLoot)->open(Arch);
}

bool FClientLootGameobj::find(unsigned const& Id)
{
    // LAB_0053e5a0
    Object = CObject::Find(Id, CObject::ClassLoot);
    advise(true);
    return Object != nullptr;
}

void FClientLootGameobj::PostCreateInit()
{
    // LAB_0053e5e0
    FClientGameobj::PostCreateInit();
    // The crate's +0x68 tractored_explosion; no null test on the narrowed object.
    Archetype::Equipment const* const Crate =
        static_cast<Archetype::Equipment const*>(NarrowObject(this, CObject::ClassLoot)->get_archetype());
    ID_String const* const ExplosionId = &Crate->TractoredExplosion;
    if (!ExplosionId->IsEmpty())
    {
        TractoredExplosionArch = Archetype::GetExplosion(*ExplosionId);
        if (TractoredExplosionArch != nullptr)
        {
            ExplosionArchAddRef(TractoredExplosionArch);
        }
        else
        {
            // The id's string is fetched first.
            char const* const ExplName = ExplosionId->get_string();
            char const* const ArchName = Object->get_archetype()->Nickname.get_string();
            FDUMP(kFdumpWarning,
                  "%s(%d) : *** WARNING: Archtype(%s) tractored_explosion(%s) not found.",
                  kLootSourceFile, 0x47, ArchName, ExplName);
        }
    }
    // FUN_005553b0
    FNebulaEffect* const Nebula =
        StarSysFindNebula(&get_position(), &NebulaFogFactor, nullptr);
    CurrentNebula = Nebula;
}

void FClientLootGameobj::update(float Dt)
{
    // FUN_0053e6a0
    FClientGameobj::update(Dt);
    GameobjOwnerUpdate(this, CObject::ClassLoot, Dt);
}

void FClientLootGameobj::ApplyDamageList(DamageList const& Damage)
{
    // FUN_0053e770: multiplayer only.
    if (SinglePlayer())
        return;
    for (std::list<DamageEntry>::const_iterator It = Damage.Entries.begin();
         It != Damage.Entries.end(); ++It)
    {
        if (It->SubObjId == SUBOBJ_ID_ROOT)
        {
            static_cast<CSimple*>(NarrowObject(this, CObject::ClassLoot))->set_hit_pts(It->HitPts);
            continue;
        }
        // Any other entry: only a value above 0 becomes the contents' hit points; 0, below and
        // NaN cost a unit and refill from the contents archetype's hit_pts.
        CLoot* const Loot = static_cast<CLoot*>(NarrowObject(this, CObject::ClassLoot));
        if (It->HitPts > 0.0f)
        {
            Loot->ContentsHitPts = It->HitPts;
        }
        else
        {
            Loot->Units = Loot->Units - 1u;
            Loot->ContentsHitPts = Loot->ContentsArch->HitPts;
        }
    }
}

void FClientLootGameobj::Destroy(int DestroyType)
{
    // LAB_0053e8a0
    if (DestroyType != DESTROY_TRACTORED)
    {
        FClientSimpleGameobj::Destroy(DestroyType);
        return;
    }
    // Tractored in.
    if (TractoredExplosionArch == nullptr)
    {
        MarkForRemoval(true);
        return;
    }
    Actions.add(Action_Explode::Create(TractoredExplosionArch, this, &get_position()));
}

FClientGameobj* ClientGameobj_CreateSolar()
{
    return new FClientSolarGameobj();
}


namespace
{
    // DAT_0066d33c / DAT_0066d340. The static initialiser LAB_0045a7e0 allocates the empty
    // list head and registers LAB_0045a820 with atexit, which destroys every node's watcher
    // (FUN_005154f0) and frees the head; a static std::list does both.
    std::list<BaseWatcher> G_GameobjWatchList;

    // The IObjRW a list watcher points at: its Watchable sub-object sits at IObjRW+8, and a
    // watcher with nothing behind it compares as null.
    IObjRW const* WatchedGameobj(BaseWatcher const& Watcher)
    {
        Watchable const* const Target = Watcher.target();
        if (Target == nullptr)
            return nullptr;
        return reinterpret_cast<IObjRW const*>(reinterpret_cast<unsigned char const*>(Target) - 8);
    }

    // FUN_0045aa10(out, first, last, &Obj): the first node from first up to last whose
    // watched object equals Obj. Retail builds a temporary BaseWatcher on Obj for every node
    // and compares the two targets minus eight; comparing against Obj itself is the same test.
    std::list<BaseWatcher>::iterator FindWatchedGameobj(IObjRW const* Obj)
    {
        std::list<BaseWatcher>::iterator It = G_GameobjWatchList.begin();
        for (; It != G_GameobjWatchList.end(); ++It)
        {
            if (WatchedGameobj(*It) == Obj)
                break;
        }
        return It;
    }
}

void GameobjWatchListAdd(IObjRW* Obj)
{
    // FUN_0045a890: push_back unless already watched (FUN_0045aad0 is the node copy).
    if (FindWatchedGameobj(Obj) != G_GameobjWatchList.end())
        return;
    // BaseWatcher(IObjRW*) is not a Common export, so the node is default-constructed and then
    // pointed at Obj+8 the way that constructor does it (set on an empty watcher is
    // set_pointer).
    G_GameobjWatchList.emplace_back();
    if (Obj != nullptr)
    {
        Watchable const* const Target =
            reinterpret_cast<Watchable const*>(reinterpret_cast<unsigned char const*>(Obj) + 8);
        G_GameobjWatchList.back().set(Target);
    }
}

void GameobjWatchListRemove(IObjRW* Obj)
{
    // FUN_0045a990 (watcher release FUN_005154f0).
    std::list<BaseWatcher>::iterator const It = FindWatchedGameobj(Obj);
    if (It == G_GameobjWatchList.end())
        return;
    G_GameobjWatchList.erase(It);
}

std::list<BaseWatcher> const& GameobjWatchList()
{
    return G_GameobjWatchList;
}

void ExplosionArchAddRef(Archetype::Explosion const* Expl)
{
    // FUN_00536100. The effect entries are a std::list at +0x24 in retail; the port keeps a
    // vector. Innards debris ids are the vector at +0x44.
    if (Expl == nullptr)
        return;
    for (size_t Index = 0; Index < Expl->Effects.size(); ++Index)
    {
        FEffectArch* const Fx = FindEffectArch(&Expl->Effects[Index].Fx.Id);
        if (Fx != nullptr)
            Fx->AddRef(1u);
    }
    for (size_t Index = 0; Index < Expl->InnardsDebrisObjects.size(); ++Index)
    {
        Archetype::Root* const Simple = Archetype::GetSimple(Expl->InnardsDebrisObjects[Index]);
        if (Simple != nullptr)
            Simple->load();
    }
}

void ExplosionArchRelease(Archetype::Explosion const* Expl)
{
    // FUN_00536170. Archetype::Root::Release is the Common import, not a virtual call.
    if (Expl == nullptr)
        return;
    for (size_t Index = 0; Index < Expl->Effects.size(); ++Index)
    {
        FEffectArch* const Fx = FindEffectArch(&Expl->Effects[Index].Fx.Id);
        if (Fx != nullptr)
            Fx->Release();
    }
    for (size_t Index = 0; Index < Expl->InnardsDebrisObjects.size(); ++Index)
    {
        Archetype::Root* const Simple = Archetype::GetSimple(Expl->InnardsDebrisObjects[Index]);
        if (Simple != nullptr)
            Simple->Release();
    }
}
