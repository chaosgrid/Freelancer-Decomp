// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "../include/server_gameobject.h"
#include "../include/server_sysdb.h"   // StarSystem::on_object_destroyed (GameObject::destroy)
#include "../include/common_misc.h"    // SUBOBJ_ID_NONE (Ship::allocate_equip_id)
#include "../include/dacom.h"          // FDUMP (Ship::allocate_equip_id)
#include "../include/common_goods.h"   // FindHardpoint / JETTISONED_CARGO_VELOCITY / EquipDesc (Ship::tick_jettison)
#include "../include/common_id.h"      // CFLIDMaker (Ship::tick_jettison)
#include "../include/common_universe.h" // Universe::IZone (CSimple::CurrentZone)
#include <cmath>
#include <windows.h>
#include <cstdlib>   // rand (Ship::tick_jettison / compute_jettison_launch)
#include "../include/rpc_iclient.h"   // IClient (Ship::request_returned / set_formation_*)
#include "../include/common_ini.h"              // StringAlloc (EqObj::*_by_name / dump_cargo)
#include "../include/common_server.h"           // Players, Server_BroadcastGroupControl (EqObj::destroy)
#include "server_internal.h"

// DAT_06d8fb14 (IServerImpl.cpp) and FUN_06d43650 (pub::Controller::_SendMessage
// with a zero id; Server_ControllerDispatch, common_server_controller.h) -- the Ship
// IObjAffect bodies use both.
extern IClient* g_pClientInterface;

// ---------------------------------------------------------------------------
// Ship -- .text:06d00e80 / create .text:06d01040
// ---------------------------------------------------------------------------

// .text:06d00e80. The Jettison EquipDescList (+0xA0..+0xA8) is built before
// this body and the EquipIdMaker at +0xC4 last. See server_gameobject.h for the
// layout.
Ship::Ship()
{
    DockedObjId = 0;
    DockExitTimer = 0.0f;
    bDockInUse = 0;
    PendingRequest = nullptr;
    JettisonTimer = 0.0f;
    PendingBayCloses = 0;
    // Deviation: retail's constructor never writes +0xB4; zeroed here anyway.
    EquipStateData = nullptr;
    bLaunchInvincible = 0;
    AccumulatedDamage = 0.0f;
    AccumulatedDamageTime = 0.0f;
}

// .text:06d00f70. When reading that listing: it is reached through the CBase
// vtable at Ship+0x0C, so its ECX is `object + 0x0C` and every offset in it is
// 0x0C LOW ([ESI+0x98] / [ESI+0x9c] are Ship+0xA4 / +0xA8, the jettison list's
// sentinel and size -- not bDockInUse and PendingRequest).
Ship::~Ship()
{
    // FUN_06d00180: drop this ship from the dock-exited ship list.
    Ship_ForgetDockExited(this);

    // Through the just re-stamped Ship vtable, i.e. this class's own.
    destroy_notify();

    // The Jettison member's destructor (the inlined EquipDescList one) runs
    // after this body and before ~EqObj.
}

// .text:06d01040.
bool Ship::create(Archetype::Root* Arch)
{
    return CreateOfClass(CObject::ClassShip, Arch);
}

// .text:06d010a0.
bool Ship::find(unsigned int const& Id)
{
    return FindOfClass(CObject::ClassShip, Id);
}

CShip* Ship::cship() const
{
    CObject* Obj = cobj();
    if (Obj == nullptr)
        return nullptr;
    if ((static_cast<unsigned>(Obj->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
        return nullptr;
    return static_cast<CShip*>(Obj);
}

// FUN_06d010e0. check_fuses is the EqObj body FUN_06ce7d00, called directly.
void Ship::set_equip_state(FEquipState const& State)
{
    EqObj::check_fuses();
    EquipIds = State.EquipIds;
    EquipStateData = State.Data;
}

// FUN_06d01110. Latched once by +0xb8.
void Ship::apply_launch_invincibility()
{
    if (bLaunchInvincible != 0)
        return;
    bLaunchInvincible = 1;
    FInvincibility Cmd = {};
    Cmd.Type = 1u;
    Cmd.bProtect = true;
    Cmd.bFromPlayers = true;
    Cmd.MaxHpLoss = 0.0f;
    set_invincible(Cmd);
}

// FUN_06d01150. Uses the raw CBase::Object with no class or null test.
void Ship::tick_zone_damage(float Dt)
{
    CObject* const Raw = Object;
    CSimple* const Simple = static_cast<CSimple*>(Raw);

    if (is_invincible())
        return;

    // The zone CSimple::update_zones cached (+0xa4); GetDamage is Common
    // .text:0633ad80, the zone's damage rate (+0x60).
    Universe::IZone* const Zone = Simple->CurrentZone;
    if (Zone != nullptr)
    {
        float const Rate = Zone->GetDamage();
        if (Rate > 0.0f)
        {
            AccumulatedDamage = Rate * Simple->ZoneImmersion * Dt + AccumulatedDamage;
        }
    }

    AccumulatedDamageTime = Dt + AccumulatedDamageTime;

    if (AccumulatedDamage == 0.0f)
        return;
    if (AccumulatedDamageTime <= 1.0f)                 // .rdata:06d67088
        return;

    // CEqObj::get_total_max_hit_pts (Common CShip vt +0xd4) on the raw object.
    float const Ratio = AccumulatedDamage / static_cast<CEqObj*>(Raw)->get_total_max_hit_pts();
    if (Ratio <= 0.01f)                                // .rdata:06d6708c
        return;

    // Retail builds the list inline and stores the cause directly. Cause 8 has
    // no name in DamageCause (common_misc.h names 0..5 only).
    DamageList Damage;
    Damage.Cause = DamageCause::ZoneDamage;

    apply_damage_ratio(Ratio, Damage);             // FUN_06d012e0
    apply_damage_list(Damage);                     // vt +0x188
    AccumulatedDamage = 0.0f;
}

// FUN_06d012e0. Mostly through the raw CBase::Object.
bool Ship::apply_damage_ratio(float Ratio, DamageList& Damage)
{
    CObject* const Raw = Object;

    if (Ratio > 1.0f)                              // .rdata:06d65520
    {
        // Through ceqobj() (FUN_06ce6870); damage_hull's result is ignored.
        damage_hull(ceqobj()->get_hit_pts(), Damage);
        Damage.bDestroyed = true;
        return true;
    }

    bool bAnyDestroyed = false;

    if (damage_hull(static_cast<CSimple*>(Raw)->get_max_hit_pts() * Ratio, Damage))
        bAnyDestroyed = true;

    // Equipment pass only while the list does not say destroyed.
    if (!Damage.bDestroyed)
    {
        CEquipTraverser Walk(0x1ee0);
        CEquipManager& Equipment = static_cast<CEqObj*>(Raw)->EquipManager;
        for (CEquip* Equip = Equipment.Traverse(Walk); Equip != nullptr;
             Equip = Equipment.Traverse(Walk))
        {
            if (damage_equip(Equip, Equip->GetMaxHitPoints() * Ratio, Damage))
                bAnyDestroyed = true;
        }
    }

    // Re-read.
    if (!Damage.bDestroyed)
    {
        CArchGrpTraverser Walk;
        CArchGroupManager& Groups = static_cast<CEqObj*>(Raw)->ArchGroups;
        for (CArchGroup* Group = Groups.Traverse(Walk); Group != nullptr;
             Group = Groups.Traverse(Walk))
        {
            // The product is formed on the unrounded max and rounded once.
            if (damage_group(Group, (float)(Group->GetMaxHitPoints() * Ratio), Damage))
                bAnyDestroyed = true;
        }
    }

    return bAnyDestroyed;
}

// LAB_06d01450 -- Ship vtable +0x16c.
bool Ship::update(float Dt)
{
    if (!EqObj::update(Dt))                                     // FUN_06ce7e20
        return false;

    if (DockedObjId == 0u)
    {
        // OwnerPlayer is read through the (& 3) narrowing with NO null test;
        // CObject::update itself goes through the raw Object.
        CObject* const Raw = Object;
        CSimple* Simple = nullptr;
        if (Raw != nullptr &&
            (static_cast<unsigned>(Raw->ObjectClass) & CObject::ClassSimple) == CObject::ClassSimple)
            Simple = static_cast<CSimple*>(Raw);

        if (Simple->OwnerPlayer == 0u)
        {
            Raw->update(Dt, CObject::NO_SCANNER);
        }
        else if (SinglePlayer() == 0)
        {
            Object->update(Dt, CObject::UPDATE_DUMB);
        }
        // else: owned + single player skips CObject::update.

        tick_jettison(Dt);
        tick_zone_damage(Dt);
        return true;
    }

    // Dock-exit arm: the raw CBase::Object, no class or null test.
    CObject* const DockedObj = Object;
    if (SinglePlayer() != 0)
    {
        // CShip::bJumpedOut (+0x214).
        if (((CShip*)DockedObj)->bJumpedOut == 0)
            return true;
        Ship_QueueDockExited(this);                                 // FUN_06d00130
        DockedObjId = 0u;
        return true;
    }

    // MP: bail while CShip+0x2bd (bIsPlayer) is set.
    if (static_cast<CShip*>(DockedObj)->bIsPlayer != 0)
        return true;

    // Leave unless <= 0.
    DockExitTimer -= Dt;
    if (DockExitTimer <= 0.0f)
    {
        DockedObj->enable_collisions_r(true);
        Ship_QueueDockExited(this);
        DockExitTimer = 0.0f;
        DockedObjId = 0u;
    }
    return true;
}

// FUN_06d01550.
void Ship::tick_jettison(float Dt)
{
    if (Jettison.Items.size() == 0u)
    {
        if (JettisonTimer == 0.0f)
            return;
        // Return while it is still > 0 (or unordered).
        JettisonTimer = JettisonTimer - Dt;
        if (!(JettisonTimer <= 0.0f))
            return;
        // The owed bay closes on the raw CShip (close_bay's float result is
        // dropped).
        while (PendingBayCloses > 0)
        {
            static_cast<CShip*>(Object)->close_bay();
            --PendingBayCloses;
        }
        JettisonTimer = 0.0f;
        return;
    }

    // Only a running timer counts down; clamped to 0 once it is <= 0.
    if (JettisonTimer != 0.0f)
    {
        JettisonTimer = JettisonTimer - Dt;
        if (JettisonTimer <= 0.0f)
            JettisonTimer = 0.0f;
    }
    if (JettisonTimer != 0.0f)
        return;
    // The raw CShip's BayState (+0x2ac) must be 2.
    if (static_cast<CShip*>(Object)->BayState != 2)
        return;

    EquipDesc& Desc = Jettison.Items.front();

    FLootRecord Record;
    Record.Id = 0u;
    Record.Arch = nullptr;
    Record.HitPts = 0.0f;
    Record.ContentsArch = nullptr;
    Record.ContentsHitPts = 0.0f;
    Record.Count = 0;
    Record.MarkForPlayer = 0u;
    Record.NameId = 0u;
    Record.bCanAiTractor = 1;
    Record.bMission = 0;

    // AllocateObjectID is inlined in retail.
    Record.Id = CFLIDMaker::GetInstance()->AllocateObjectID();

    Archetype::Equipment* const Contents = Archetype::GetEquipment(Desc.ArchId);
    Record.ContentsArch = Contents;
    Record.ContentsHitPts = Desc.Health * Contents->HitPts;
    Archetype::Equipment* const LootArch = Contents->get_loot_appearance();
    Record.Arch = LootArch;
    // Same nickname -> the contents' hit points, else the loot archetype's own.
    if (LootArch->Nickname.Id == Contents->Nickname.Id)
        Record.HitPts = Record.ContentsHitPts;
    else
        Record.HitPts = LootArch->HitPts;

    int const Count = Desc.Count;
    Record.Count = Count;

    compute_jettison_launch(&Record.Position, &Record.Orientation, &Record.Velocity);   // FUN_06d01860

    // The spin: two rand() draws, each rand() * 2^-15 (.rdata:06d656b8) *
    // (double)0.8f (.rdata:06d656b0) - (double)0.4f (.rdata:06d656a8), stored
    // as a float; the first scales the orientation's column 0, the second its
    // column 2.
    Matrix const& M = Record.Orientation;
    float const A = static_cast<float>(
        static_cast<double>(static_cast<float>(rand()) * 3.0517578125e-05f) *
        static_cast<double>(0.8f) - static_cast<double>(0.4f));
    float const Ax = M.E[0][0] * A;
    float const Ay = M.E[1][0] * A;
    float const Az = M.E[2][0] * A;
    float const B = static_cast<float>(
        static_cast<double>(static_cast<float>(rand()) * 3.0517578125e-05f) *
        static_cast<double>(0.8f) - static_cast<double>(0.4f));
    Record.AngularVelocity.x = M.E[0][2] * B + Ax;
    Record.AngularVelocity.y = M.E[1][2] * B + Ay;
    Record.AngularVelocity.z = M.E[2][2] * B + Az;

    OwnerSystem->create_loot(get_id(), 1, &Record, 1);   // FUN_06d08a40

    Desc.set_count(Count - Record.Count);

    // An emptied stack is dropped, and one more bay close is owed.
    if (Desc.Count == 0)
    {
        ++PendingBayCloses;
        Jettison.Items.erase(Jettison.Items.begin());
    }

    JettisonTimer = CShip::JETTISON_CLEARANCE_TIME;
}

// FUN_06d01860. The raw CBase::Object's archetype (+0x88) is read as
// Archetype::Ship with no test.
void Ship::compute_jettison_launch(Vector* OutPos, Matrix* OutOrient, Vector* OutVel)
{
    Archetype::Ship const* const Arch =
        static_cast<Archetype::Ship const*>(Object->Archetype);

    // Both bay hardpoints (HpBaySurface +0xe0, HpBayExternal +0xe4) must exist
    // on the engine instance.
    long Surface = 0;
    long External = 0;
    Vector Dir;
    if (FindHardpoint(get_index(), Arch->HpBaySurface.Value, Surface) &&
        FindHardpoint(get_index(), Arch->HpBayExternal.Value, External))
    {
        HardpointPosition(Surface, Arch->HpBaySurface.Value, OutPos, OutOrient);
        Vector ExternalPos;
        HardpointPosition(External, Arch->HpBayExternal.Value, &ExternalPos, nullptr);
        // Exterior minus surface.
        Dir.x = ExternalPos.x - OutPos->x;
        Dir.y = ExternalPos.y - OutPos->y;
        Dir.z = ExternalPos.z - OutPos->z;
    }
    else
    {
        // The raw object's position and orientation; minus column 1.
        *OutPos = Object->WorldPosition;
        *OutOrient = Object->Orientation;
        Dir.x = -OutOrient->E[0][1];
        Dir.y = -OutOrient->E[1][1];
        Dir.z = -OutOrient->E[2][1];
    }

    *OutVel = Dir;

    // Doubles .rdata:06d656d8 / .rdata:06d656d0.
    double const Speed =
        (static_cast<double>(static_cast<float>(rand()) * 3.0517578125e-05f) * 0.5 + 0.75) *
        static_cast<double>(JETTISONED_CARGO_VELOCITY);
    // The scale Speed / |OutVel| is stored as a float.
    double const Length = sqrt(
        (static_cast<double>(OutVel->x) * OutVel->x + static_cast<double>(OutVel->y) * OutVel->y) +
        static_cast<double>(OutVel->z) * OutVel->z);
    float const Scale = static_cast<float>(Speed / Length);
    OutVel->x = Scale * OutVel->x;
    OutVel->y = Scale * OutVel->y;
    OutVel->z = Scale * OutVel->z;

    Vector const Velocity = get_velocity();
    OutVel->x = Velocity.x + OutVel->x;
    OutVel->y = Velocity.y + OutVel->y;
    OutVel->z = Velocity.z + OutVel->z;
}

// .text:06d01c30.
unsigned short Ship::allocate_equip_id()
{
    // No null test on the guarded CSimple in retail.
    if (csimple()->OwnerPlayer != 0)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\ship.cpp", 0x1e5,
              "Ship::allocate_equip_id (): Player tried to allocate id!");
        return SUBOBJ_ID_NONE;
    }
    // A tail jump into EquipIdMaker::CreateEquipID on this + 0xc4.
    return EquipIds.CreateEquipID();
}

// ---------------------------------------------------------------------------
// Ship's GameObject / EqObj overrides and its IObjAffect table. In every
// IObjAffect body retail runs with ECX = the IObjAffect sub-object (this + 4),
// so `[ECX+0xc]` is the raw CBase::Object (+0x10), `[ECX+0x24]` is OwnerSystem
// (+0x28) and `ECX - 4` is the wrapper; the port's `this` is the wrapper itself.
// ---------------------------------------------------------------------------

// .text:06d01a60. GameObject's body (FUN_06cee6e0) is called directly.
void Ship::enter_system(StarSystem* Sys, PhySys::CreateParms const* Parms)
{
    GameObject::enter_system(Sys, Parms);
    DockedObjId = 0u;
}

// .text:06d01a10. OwnerPlayer is read unguarded; the EqObj body (FUN_06ceb330)
// is called directly.
void Ship::apply_damage_list(DamageList& Damage)
{
    if (Damage.Cause == DamageCause::CruiseDisrupter && csimple()->OwnerPlayer == 0u)
        toggle_cruise(false, true, 0);
    EqObj::apply_damage_list(Damage);
}

// .text:06d01a90. The EqObj body (FUN_06cec4d0) is called directly and its
// result returned on every path. When DEATH_COMM_FUSE is lit, every follower
// in CShip's formation (+0x1d4) and follow (+0x1f8) lists that is not a player
// gets behavior event 7 with this ship's id: "my leader's death fuse is lit".
// The (& 0x503) narrowing of the follower is NOT tested before
// get_behavior_interface.
bool Ship::light_fuse(unsigned int SourceId, ID_String const& FuseId,
                      unsigned short SubObjId, float AtT, float Lifetime)
{
    bool const bLit = EqObj::light_fuse(SourceId, FuseId, SubObjId, AtT, Lifetime);
    if (!bLit)
        return bLit;
    if (FuseId.Id != DEATH_COMM_FUSE.Id)
        return bLit;

    // Raw CBase::Object as CShip, no test.
    CShip* const Raw = static_cast<CShip*>(Object);

    // Retail repeats this body for both loops. *Entry is read again for the
    // cobject() call, and the ship's own id is fetched per follower.
    auto NotifyFollower = [this](FObjRWWatcher const& Entry)
    {
        // W != 0 -> W - 8, tested for 0 (CShip's typed watcher, common_cobject.h).
        IObjRW* const Follower = Entry.get();
        if (Follower == nullptr)
            return;
        if (Follower->is_player())
            return;
        IObjRW* const FollowerAgain = Entry.get();
        CObject* const FollowerObj = const_cast<CObject*>(FollowerAgain->cobject());
        CEqObj* const FollowerEqObj =
            (FollowerObj != nullptr &&
             (static_cast<unsigned int>(FollowerObj->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                ? static_cast<CEqObj*>(FollowerObj) : nullptr;
        IBehaviorManager* const Behavior = FollowerEqObj->get_behavior_interface();
        if (Behavior == nullptr)
            return;
        // IBehaviorManager slot 0 (Common LAB_062d3f90).
        unsigned int LeaderId = get_id();
        FBehaviorManagerEvent FuseEvent;
        FuseEvent.Type = 7;
        FuseEvent.Data = &LeaderId;
        Behavior->OnEvent(&FuseEvent);
    };

    // The end of each vector is re-read after every entry.
    for (std::vector<FObjRWWatcher>::const_iterator Entry = Raw->FormationFollowers.begin();
         Entry != Raw->FormationFollowers.end(); ++Entry)
        NotifyFollower(*Entry);
    for (std::vector<FObjRWWatcher>::const_iterator Entry = Raw->FollowFollowers.begin();
         Entry != Raw->FollowFollowers.end(); ++Entry)
        NotifyFollower(*Entry);
    return bLit;
}

// .text:06d01c90: the EqObj body (FUN_06ce8510) directly, then every queued
// jettison item appended.
void Ship::collect_droppable_equip(std::vector<EquipDesc>& Out)
{
    EqObj::collect_droppable_equip(Out);
    for (std::list<EquipDesc>::const_iterator It = Jettison.Items.begin();
         It != Jettison.Items.end(); ++It)
        Out.push_back(*It);
}

// FUN_06d01e80. A PendingRequest (+0x9c) hears the result first (vtable slot
// 0) and is dropped when it answers false; with none, the client is told
// (IClient +0x14c, OwnerPlayer read unguarded) and bDockInUse (+0x98) becomes
// (RequestType == 0 && Response == 3).
void Ship::request_returned(int RequestType, int Response, int Arg3)
{
    if (PendingRequest != nullptr)
    {
        if (!PendingRequest->OnResult(RequestType, Response, Arg3))
        {
            PendingRequest = nullptr;
            bDockInUse = 0;
        }
        return;
    }
    unsigned int OwnerPlayer = csimple()->OwnerPlayer;
    unsigned int const Id = get_id();
    g_pClientInterface->SendServerRequestReturned(OwnerPlayer, Id, RequestType, Response, Arg3);
    bDockInUse = 0;
    if (RequestType == 0 && Response == DOCK_HOST_RESPONSE_BUSY)
        bDockInUse = 1;
}

// .text:06d01f30.
int Ship::set_throttle(float Throttle)
{
    static_cast<CShip*>(Object)->set_throttle(Throttle);
    return 0;
}

// .text:06d01f50: CShip+0x230 is the CSteering.
int Ship::set_axis_throttle(Vector const& AxisThrottle)
{
    static_cast<CShip*>(Object)->Steering.SetAxisThrottle(AxisThrottle);
    return 0;
}

// .text:06d01f70.
int Ship::set_nudge_vec(Vector const& NudgeVec)
{
    static_cast<CShip*>(Object)->set_nudge_vec(NudgeVec);
    return 0;
}

// FUN_06d01f90. Strafing switches an inactive engine (type 0x20000, used
// untested) on first. Always returns 0.
int Ship::set_strafe_dir(StrafeDir Dir)
{
    CShip* const Raw = static_cast<CShip*>(Object);
    if (Raw->get_strafe_dir() == Dir)
        return 0;
    CEquip* const Engine = Raw->EquipManager.FindFirst(EquipTypeEngine);
    if (!Engine->IsActive())
    {
        unsigned short const EngineId = Engine->GetID();
        toggle_item(&EngineId, true, 0);
    }
    static_cast<CShip*>(Object)->set_strafe_dir(Dir);
    return 0;
}

// .text:06d02000. Lifts the launch grant apply_launch_invincibility made and
// sends controller event 0x28 (LaunchComplete).
int Ship::launch_complete(unsigned BaseObjId)
{
    if (bLaunchInvincible != 0)
    {
        bLaunchInvincible = 0;
        FInvincibility Cmd = {};
        Cmd.Type = 1u;
        Cmd.bProtect = false;
        Cmd.bFromPlayers = false;
        Cmd.MaxHpLoss = 1.0f;
        set_invincible(Cmd);
        unsigned int Msg[2];
        Msg[0] = BaseObjId;
        Msg[1] = get_id();
        Server_ControllerDispatch(0, pub::Controller::EMessage::LaunchComplete, Msg);
    }
    return 0;
}

// .text:06d02070. Controller event 0x26 (JumpInComplete), then lifts the type-2
// invincibility.
int Ship::jump_in_complete(unsigned SystemId)
{
    unsigned int Msg[2];
    Msg[0] = SystemId;
    Msg[1] = get_id();
    Server_ControllerDispatch(0, pub::Controller::EMessage::JumpInComplete, Msg);
    FInvincibility Cmd = {};
    Cmd.Type = 2u;
    Cmd.bProtect = false;
    Cmd.bFromPlayers = false;
    Cmd.MaxHpLoss = 1.0f;
    set_invincible(Cmd);
    return 0;
}

// .text:06d020d0, the jettison request. Controller event 0x23 first; a stack
// in a pod (+0x20) is dropped by destroying the pod (fate 4, cause 0x16);
// otherwise the desc is queued for the bay (+0xa4), the bay opened (its float
// result dropped), and the system told (FUN_06d0f0c0).
int Ship::jettison_cargo(unsigned short EquipId, int Count, int SourceClient)
{
    (void)SourceClient;   // retail never reads the third argument
    if (OwnerSystem == nullptr)
        return -5;
    if (is_destroyed())
        return -5;

    CECargo* const Cargo =
        CECargo::cast(static_cast<CEqObj*>(Object)->EquipManager.FindByID(EquipId));
    if (Cargo == nullptr)
        return -2;

    // Unsigned min.
    unsigned int Amount = static_cast<unsigned int>(Count);
    if (!(Amount < Cargo->Count))
        Amount = Cargo->Count;

    struct FControllerMsg23
    {
        unsigned int ObjId;
        unsigned short EquipId;
        unsigned short Pad;       // not written by retail
        unsigned int GoodId;
        unsigned int Count;
    } Msg;
    Msg.ObjId = get_id();
    Msg.EquipId = EquipId;
    Msg.Pad = 0;
    Msg.GoodId = Arch2Good(Cargo->EquipArch()->get_id());
    Msg.Count = Amount;
    Server_ControllerDispatch(0, pub::Controller::EMessage::CargoJettisoned, &Msg);

    CEquip* const Pod = Cargo->CargoPod;
    if (Pod != nullptr)
    {
        // InflictorOwner read through the unguarded (& 3) narrowing.
        DamageList Damage;
        Damage.bDestroyed = false;
        Damage.Cause = DamageCause::CargoPodJettison;
        Damage.InflictorId = get_id();
        Damage.InflictorOwner = csimple()->OwnerPlayer;
        destroy_equip(Pod, DamageEntry::SubObjFate::Loot, Damage);
        apply_damage_list(Damage);
        return 0;
    }

    EquipDesc Desc;
    if (!Cargo->GetEquipDesc(Desc))
        return -5;
    Desc.set_count(static_cast<int>(Amount));

    // Node copy is FUN_06ced210.
    Jettison.Items.push_back(Desc);

    static_cast<CShip*>(Object)->open_bay();
    if (!SinglePlayer())
        Cargo->RemoveFromStack(Amount);
    OwnerSystem->notify_jettison(this, EquipId, static_cast<int>(Amount));
    return 0;
}

// .text:06d02340. Single player with a SourceClient goes straight to the
// notification (FUN_06d0e670 with SUBOBJ_ID_NONE as the equip); the server's
// own unchanged request is not announced.
int Ship::set_target(IObjRW* Target, unsigned short SubTarget, int SourceClient)
{
    if (OwnerSystem == nullptr)
        return -5;
    if (is_destroyed())
        return -5;
    if (SourceClient == -1)
        SourceClient = 0;

    if (!(SinglePlayer() && SourceClient != 0))
    {
        bool bChanged = false;
        CShip* const Raw = static_cast<CShip*>(Object);
        if (Raw->get_target() != Target || Raw->get_sub_target() != SubTarget)
        {
            bChanged = true;
            Raw->set_target(Target);
            Raw->set_sub_target(SubTarget);
        }
        if (SourceClient == 0 && !bChanged)
            return 0;
    }

    unsigned int const Id = get_id();
    OwnerSystem->notify_set_target(Id, SUBOBJ_ID_NONE, Target, SubTarget, SourceClient);
    return 0;
}

// .text:06d02410. The engine (CEEngine::cast of FindFirst(EquipTypeEngine)) is used
// untested; engaging cruise from the server switches the thrusters off first.
// Announced through FUN_06d0e580.
int Ship::toggle_cruise(bool bEngage, bool bInstant, int SourceClient)
{
    if (OwnerSystem == nullptr)
        return -5;
    if (is_destroyed())
        return -5;
    if (SourceClient == -1)
        SourceClient = 0;

    if (!(SinglePlayer() && SourceClient != 0))
    {
        bool bChanged = false;
        CEEngine* const Engine =
            CEEngine::cast(static_cast<CEqObj*>(Object)->EquipManager.FindFirst(EquipTypeEngine));
        if (Engine->bCruiseActive != static_cast<unsigned char>(bEngage))
        {
            bChanged = true;
            if (bEngage && SourceClient == 0)
                toggle_thrusters(false, 0);
            Engine->EngageCruise(bEngage, bInstant);
        }
        if (SourceClient == 0 && !bChanged)
            return 0;
    }

    unsigned int const Id = get_id();
    OwnerSystem->notify_cruise(Id, bEngage, bInstant, SourceClient);
    return 0;
}

// .text:06d02500. The engine (FindFirst(EquipTypeEngine)) is used untested; the
// server's own request to activate is refused (-5) under cruise and switches
// an inactive engine on. Announced through FUN_06d0e600.
int Ship::toggle_thrusters(bool bActivate, int SourceClient)
{
    if (OwnerSystem == nullptr)
        return -5;
    if (is_destroyed())
        return -5;
    int const Result = 0;
    if (SourceClient == -1)
        SourceClient = 0;

    if (!(SinglePlayer() && SourceClient != 0))
    {
        bool bChanged = false;
        CEqObj* const Raw = static_cast<CEqObj*>(Object);
        CEquip* const Engine = Raw->EquipManager.FindFirst(EquipTypeEngine);
        unsigned char const bCruiseActive = static_cast<CEEngine*>(Engine)->bCruiseActive;
        bool const bEngineActive = Engine->IsActive();
        if (SourceClient == 0 && bActivate)
        {
            if (bCruiseActive != 0)
                return -5;
            if (!bEngineActive)
            {
                unsigned short const EngineId = Engine->GetID();
                toggle_item(&EngineId, true, 0);
            }
        }

        CEquipTraverser Walk(EquipTypeThruster);
        for (CEquip* Thruster = Raw->EquipManager.Traverse(Walk); Thruster != nullptr;
             Thruster = Raw->EquipManager.Traverse(Walk))
        {
            if (Thruster->IsActive() != bActivate)
            {
                bChanged = true;
                Thruster->Activate(bActivate);
            }
        }
        if (SourceClient == 0 && !bChanged)
            return Result;
    }

    unsigned int const Id = get_id();
    OwnerSystem->notify_thrusters(Id, bActivate, SourceClient);
    return Result;
}

// .text:06d02670. PendingRequest and bDockInUse are set BEFORE the type is
// looked at; an unknown type clears bDockInUse but PendingRequest keeps the
// callback. Deviation: for type 0 retail never writes bytes +0xd..+0xf of the
// message (stack garbage); the port zeroes them. The system id is
// FUN_06d06cd0. Every path returns 0.
int Ship::request_event(int RequestType, unsigned const* TargetId, unsigned const* SecondId,
                  int bFlag, RequestCB* Callback)
{
    if (bDockInUse != 0)
        return 0;
    PendingRequest = Callback;
    bDockInUse = 1;

    if (RequestType == 0)
    {
        unsigned int Msg[4] = { 0u, 0u, 0u, 0u };
        Msg[0] = get_id();
        Msg[1] = *TargetId;
        Msg[2] = OwnerSystem->SystemInfo->NicknameId;
        reinterpret_cast<unsigned char*>(&Msg[3])[0] = (bFlag == 1) ? 1u : 0u;
        Server_ControllerDispatch(0, pub::Controller::EMessage::DockRequest, Msg);
        return 0;
    }
    if (RequestType == 1)
    {
        unsigned int Msg[3];
        Msg[2] = OwnerSystem->SystemInfo->NicknameId;
        Msg[0] = get_id();
        Msg[1] = *TargetId;
        Server_ControllerDispatch(0, pub::Controller::EMessage::FormationRequest, Msg);
        return 0;
    }
    if (RequestType == 2)
    {
        unsigned int Msg[4];
        Msg[0] = get_id();
        Msg[1] = *TargetId;
        Msg[2] = *SecondId;
        Msg[3] = OwnerSystem->SystemInfo->NicknameId;
        Server_ControllerDispatch(0, pub::Controller::EMessage::TradelaneRequest, Msg);
        return 0;
    }
    bDockInUse = 0;
    return 0;
}

// .text:06d027a0 (FUN_06d027ac is the same body). Every path ends in
// set_invincible({2, 0, 0, 1.0f}).
int Ship::request_cancel(int RequestType, unsigned const* TargetId, int Arg3)
{
    PendingRequest = nullptr;
    bDockInUse = 0;

    if (RequestType == 0)
    {
        unsigned int Msg[3];
        Msg[0] = get_id();
        Msg[1] = *TargetId;
        Msg[2] = static_cast<unsigned int>(Arg3);
        Server_ControllerDispatch(0, pub::Controller::EMessage::DockCancel, Msg);
    }
    else if (RequestType == 1)
    {
        unsigned int Msg[3];
        Msg[0] = get_id();
        Msg[1] = *TargetId;
        Msg[2] = OwnerSystem->SystemInfo->NicknameId;
        Server_ControllerDispatch(0, pub::Controller::EMessage::FormationLeave, Msg);
    }
    else if (RequestType == 2)
    {
        unsigned int Msg[3];
        Msg[0] = get_id();
        Msg[1] = *TargetId;
        Msg[2] = OwnerSystem->SystemInfo->NicknameId;
        Server_ControllerDispatch(0, pub::Controller::EMessage::TradelaneRelease, Msg);
    }

    FInvincibility Cmd = {};
    Cmd.Type = 2u;
    Cmd.bProtect = false;
    Cmd.bFromPlayers = false;
    Cmd.MaxHpLoss = 1.0f;
    set_invincible(Cmd);
    return 0;
}

// .text:06d028c0. EntranceRing is untested; announced through FUN_06d0df40.
int Ship::go_tradelane(IObjInspect const* EntranceRing, IObjInspect const* ExitRing,
                       bool bFastExit, float Speed)
{
    if (static_cast<CShip*>(Object)->go_tradelane(EntranceRing, ExitRing, this, bFastExit, Speed) != 0)
        return -5;
    unsigned int const ExitId = (ExitRing != nullptr) ? ExitRing->get_id() : 0u;
    unsigned int const EntranceId = EntranceRing->get_id();
    unsigned int const ShipId = get_id();
    OwnerSystem->notify_go_tradelane(ShipId, EntranceId, ExitId, bFastExit, Speed);
    return 0;
}

// FUN_06d02880. The exit ring is read before the entrance ring; announced
// through FUN_06d0e120.
int Ship::stop_tradelane()
{
    CShip* const Raw = static_cast<CShip*>(Object);
    Raw->stop_tradelane();
    unsigned int const ExitId = Raw->get_tradelane_exit_ring();
    unsigned int const EntranceId = static_cast<CShip*>(Object)->get_tradelane_entrance_ring();
    unsigned int const ShipId = get_id();
    OwnerSystem->notify_stop_tradelane(ShipId, EntranceId, ExitId);
    return 0;
}

// .text:06d02940. CShip+0x1c0 on the raw object, untested; then the same
// client update as set_formation_leader.
int Ship::set_formation_offset(Vector const& Offset)
{
    static_cast<CShip*>(Object)->FormationOffset = Offset;
    unsigned int const Owner = csimple()->OwnerPlayer;
    if (Owner != 0u)
    {
        IObjRW* Leader = nullptr;
        get_formation_leader(Leader);
        unsigned int const LeaderId = (Leader != nullptr) ? Leader->get_id() : 0u;
        g_pClientInterface->SendServerFormationUpdate(Owner, LeaderId, Offset);
    }
    return 0;
}

// FUN_06d029c0. CShip::FormationLeader (+0x1b8) is a BaseWatcher (Leader + 8);
// BaseWatcher::set is the compare + set_pointer retail inlines. A player's ship
// (OwnerPlayer read unguarded) then tells its client (IClient +0x1e4,
// FORMATION_UPDATE).
int Ship::set_formation_leader(IObjRW* Leader)
{
    CShip* const Raw = static_cast<CShip*>(Object);
    Watchable const* const Watched = (Leader != nullptr) ? static_cast<Watchable const*>(Leader)
                                                         : nullptr;
    Raw->FormationLeader.set(Watched);

    unsigned int const Owner = csimple()->OwnerPlayer;
    if (Owner != 0u)
    {
        Vector Offset;
        get_formation_offset(Offset);
        unsigned int const LeaderId = (Leader != nullptr) ? Leader->get_id() : 0u;
        g_pClientInterface->SendServerFormationUpdate(Owner, LeaderId, Offset);
    }
    return 0;
}

// .text:06d02a50 / 06d02a60 / 06d02a70 / 06d02ac0 / 06d02ad0 -- each a tail
// jump into the CShip export, whose FORMATION_RTYPE (or count) is the result.
int Ship::add_formation_follower(IObjRW* Follower)
{
    return static_cast<int>(static_cast<CShip*>(Object)->add_formation_follower(Follower));
}

// .text:06d02a80.
int Ship::set_follow_offset(Vector const& Offset)
{
    static_cast<CShip*>(Object)->set_follow_offset(Offset);
    return 0;
}

int Ship::remove_formation_follower(IObjRW* Follower)
{
    return static_cast<int>(static_cast<CShip*>(Object)->remove_formation_follower(Follower));
}

int Ship::get_formation_follower_count()
{
    return static_cast<CShip*>(Object)->get_formation_follower_count();
}

int Ship::add_follow_follower(IObjRW* Follower)
{
    return static_cast<int>(static_cast<CShip*>(Object)->add_follow_follower(Follower));
}

int Ship::remove_follow_follower(IObjRW* Follower)
{
    return static_cast<int>(static_cast<CShip*>(Object)->remove_follow_follower(Follower));
}

// .text:06d02aa0.
int Ship::set_follow_leader(IObjRW* Leader)
{
    static_cast<CShip*>(Object)->set_follow_leader(Leader);
    return 0;
}

// .text:06d02ae0.
int Ship::get_data(void const*& OutData) const
{
    OutData = EquipStateData;
    return 0;
}
