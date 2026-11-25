// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "../include/server_gameobject.h"
#include "../include/server_sysdb.h"   // StarSystem::on_object_destroyed (GameObject::destroy)
#include "../include/server_createinfo.h" // Server_Acquire3DMathEngine (DAT_06d8d71c)
#include "../include/x86math.h"          // I3DMathEngine::MatrixToQuaternion
#include <cmath>
#include <windows.h>
#include "../include/rpc_iclient.h"   // IClient (Ship::request_returned / set_formation_*)
#include "../include/DALib.h"         // DALib::Engine (Ship::create / Solar::create, IEngine +0x6c)
#include "../include/common_ini.h"              // StringAlloc (EqObj::*_by_name / dump_cargo)
#include "../include/common_server.h"           // Players, Server_BroadcastGroupControl (EqObj::destroy)
#include "server_internal.h"

Solar::~Solar()
{
    // Solar's +0x15c entry is EqObj's destroy_notify (.text:06ce7c80); ~EqObj
    // (FUN_06ce6710) follows as the base destructor.
    destroy_notify();
}

// FUN_06d02c20. AddRef on Obj untested.
bool Solar::attach_existing(CObject* Obj)
{
    Object = Obj;
    Obj->AddRef();
    advise(true);
    return Object != nullptr;
}

// ---------------------------------------------------------------------------
// Solar -- create .text:06d02c50
// ---------------------------------------------------------------------------

Solar::Solar()
{
}

bool Solar::create(Archetype::Root* Arch)
{
    // .text:06d02c50. Same steps as Ship::create (CreateOfClass) except that
    // CObject::open runs BEFORE CBase::advise(true). No null checks, as in
    // retail.
    Object = CObject::Alloc(CObject::ClassSolar);
    Arch->create_instance(static_cast<IEngineInstance*>(Object));
    DALib::Engine->UpdateInstance(Object->EngineInstanceId, 0, nullptr);
    Object->open(Arch);
    advise(true);
    return Object != nullptr;
}

bool Solar::find(unsigned int const& Id)
{
    // .text:06d02cb0, the "return false" ICF body Loot/Bullet/Mine's find and
    // MedAsteroid's create share: a Solar is never found by id. Single player
    // wraps an existing CSolar through attach_existing (FUN_06d02c20) instead.
    // Ship's is the only EqObj-family find that calls CObject::Find.
    (void)Id;
    return false;
}

// +0x16c .text:06d02cc0. The EqObj body (FUN_06ce7e20) directly; the 0x303
// narrowing is not null-tested and CObject::update's result is dropped.
bool Solar::update(float Dt)
{
    if (!EqObj::update(Dt))
        return false;
    csolar()->update(Dt, CObject::UPDATE_DEFAULT);
    return true;
}

// +0x158 .text:06d02d10. The EqObj body (FUN_06ce8080) directly; on success the
// owning system drops the solar (FUN_06d08940).
bool Solar::destroy(int Kind, unsigned int ObjId)
{
    if (!EqObj::destroy(Kind, ObjId))
        return false;
    OwnerSystem->remove_solar(this);
    return true;
}

// +0x17c .text:06d02d40. A solar that is not destructible (CSolar +0x1a8 clear,
// read through the unguarded 0x303 narrowing) ignores a hit on its root;
// everything else is EqObj's (FUN_06ce9250, a tail jump).
void Solar::damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage)
{
    if (!csolar()->bDestructible && Hit.SubObjId == SUBOBJ_ID_ROOT)
        return;
    EqObj::damage_sub_obj(Hit, Damage);
}

// The 0x24-byte record Solar's two root-hit overrides hand to controller event
// 0xd: nine dwords zeroed, then the solar's id, its system's id, the
// DamageList's inflictor (+0x14) and cause (+0x10), and 1.0f in the last one.
struct FSolarHitEvent
{
    unsigned int ObjId;        // +0x00  get_id()
    unsigned int SystemId;     // +0x04  FUN_06d06cd0 on OwnerSystem
    unsigned int InflictorId;  // +0x08  DamageList +0x14
    unsigned int Zero0c;       // +0x0c
    unsigned int Zero10;       // +0x10
    unsigned int Cause;        // +0x14  DamageList +0x10
    unsigned int Zero18;       // +0x18
    unsigned int Zero1c;       // +0x1c
    float Scale;               // +0x20  1.0f
};

// +0x180 .text:06d02d90. A destructible solar, or a hit on a sub-object, takes
// the EqObj body (FUN_06ce9350). A root hit on an indestructible solar only
// reports: FSolarHitEvent (system id read first) to controller event 0xd, the
// same nine dwords as a type-0 behavior event (through the unguarded 0x303
// narrowing), then damage_power with the munition's EnergyDamage.
void Solar::munition_hit(FMunitionImpact const& Hit, DamageList& Damage)
{
    if (csolar()->bDestructible)
    {
        EqObj::munition_hit(Hit, Damage);
        return;
    }
    if (Hit.SubObjId != SUBOBJ_ID_ROOT)
    {
        EqObj::munition_hit(Hit, Damage);
        return;
    }

    FSolarHitEvent Event = {};
    Event.SystemId = OwnerSystem->SystemInfo->NicknameId;
    Event.InflictorId = Damage.InflictorId;
    Event.ObjId = get_id();
    Event.Cause = static_cast<unsigned int>(Damage.Cause);
    Event.Scale = 1.0f;
    Server_ControllerDispatch(0, pub::Controller::EMessage::ObjectDamaged, &Event);

    IBehaviorManager* const Behavior = static_cast<CEqObj*>(csolar())->get_behavior_interface();
    if (Behavior != nullptr)
    {
        // The 0xd record reordered into FBehaviorDamageEvent:
        // {system, inflictor, id, 0, 0, cause, 0, 0, 1.0f}.
        FBehaviorDamageEvent Payload;
        Payload.SystemId = Event.SystemId;
        Payload.InflictorId = Event.InflictorId;
        Payload.ObjId = Event.ObjId;
        Payload.Cause = static_cast<int>(Event.Cause);
        std::memcpy(&Payload.Damage, &Event.Zero18, sizeof(unsigned int));
        std::memcpy(&Payload.RelativeDamage, &Event.Zero1c, sizeof(unsigned int));
        Payload.RelativeHealth = Event.Scale;
        Payload.EquipType = 0u;
        Payload.CollisionGroupType = 0u;
        FBehaviorManagerEvent DamageEvent;
        DamageEvent.Type = 0;
        DamageEvent.Data = &Payload;
        Behavior->OnEvent(&DamageEvent);
    }

    damage_power(Hit.Munition->EnergyDamage, Damage);
}

static_assert(sizeof(FSolarHitEvent) == 0x24, "Solar root-hit event is 0x24 bytes");

// +0x1fc .text:06d02f20. A destructible solar (CSolar +0x1a8) takes EqObj's
// hull pass (FUN_06ce9550). An indestructible one only reports the blast, as
// munition_hit does -- no damage at all; Blast is not read on that path.
void Solar::explosion_hit_hull(ExplosionDamageEvent const& Blast, DamageList& Out)
{
    if (csolar()->bDestructible)
    {
        EqObj::explosion_hit_hull(Blast, Out);
        return;
    }

    FSolarHitEvent Event = {};
    Event.SystemId = OwnerSystem->SystemInfo->NicknameId;
    Event.InflictorId = Out.InflictorId;
    Event.ObjId = get_id();
    Event.Cause = static_cast<unsigned int>(Out.Cause);
    Event.Scale = 1.0f;
    Server_ControllerDispatch(0, pub::Controller::EMessage::ObjectDamaged, &Event);

    IBehaviorManager* const Behavior = static_cast<CEqObj*>(csolar())->get_behavior_interface();
    if (Behavior != nullptr)
    {
        FBehaviorDamageEvent Payload;
        Payload.SystemId = Event.SystemId;
        Payload.InflictorId = Event.InflictorId;
        Payload.ObjId = Event.ObjId;
        Payload.Cause = static_cast<int>(Event.Cause);
        std::memcpy(&Payload.Damage, &Event.Zero18, sizeof(unsigned int));
        std::memcpy(&Payload.RelativeDamage, &Event.Zero1c, sizeof(unsigned int));
        Payload.RelativeHealth = Event.Scale;
        Payload.EquipType = 0u;
        Payload.CollisionGroupType = 0u;
        FBehaviorManagerEvent DamageEvent;
        DamageEvent.Type = 0;
        DamageEvent.Data = &Payload;
        Behavior->OnEvent(&DamageEvent);
    }
}

// +0x250 .text:06d03070. The type bits pick the branch:
//   0x40 or 0x800 -- somewhere on a 500-unit ring 350 units behind the solar.
//     vt +0x10 is ICF-folded with get_transform, so the copy is the whole
//     Transform. 2pi is the double .rdata:06d66b80.
//   0x400 -- in place, turned round: a half turn about the local Y axis with
//     cos/sin(pi) on the x87 (pi double .rdata:06d67728), both rows from the
//     old values.
//   anything else -- FDUMP, {35000, 0, 35000} and identity.
void Solar::ChooseSystemArrivalLocation(GameObject* Arriving, Vector& OutPos, Matrix& OutOrient)
{
    (void)Arriving;   // pushed by both callers, never read
    unsigned int const TypeBits = get_type_bits();

    if ((TypeBits & OBJ_JUMP_GATE) != 0 || (TypeBits & OBJ_JUMP_HOLE) != 0)
    {
        Transform const Xform = reinterpret_cast<Transform const&>(get_orientation());
        int const Random = rand();
        double const Angle = static_cast<double>(static_cast<float>(Random) * 3.0517578125e-05f)
                           * 6.2831854820251465;
        Vector Local;
        Local.x = static_cast<float>(cos(Angle) * 500.0);
        Local.y = static_cast<float>(sin(Angle) * 500.0);
        Local.z = -350.0f;
        Vector Out;
        Server_MathEngine()->TransformXform(Out, Xform, Local);
        OutPos = Out;
        OutOrient = Xform.Rotation;
        return;
    }

    if ((TypeBits & OBJ_AIRLOCK_GATE) != 0)
    {
        OutPos = get_position();
        OutOrient = get_transform().Rotation;
        double const Pi = 3.1415927410125732;
        double const C = cos(Pi);
        double const S = sin(Pi);
        for (int Column = 0; Column < 3; ++Column)
        {
            double const Row0 = OutOrient.E[0][Column];
            double const Row2 = OutOrient.E[2][Column];
            OutOrient.E[0][Column] = static_cast<float>(Row0 * C + S * Row2);
            OutOrient.E[2][Column] = static_cast<float>(C * Row2 - Row0 * S);
        }
        return;
    }

    FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s",
          "E:\\FL\\Scratch\\Source\\Server\\Space\\Solar.cpp", 0xeb,
          "Invalid type in ChooseSystemArrivalLocation()");
    OutPos.x = 35000.0f;
    OutPos.y = 0.0f;
    OutPos.z = 35000.0f;
    OutOrient.E[0][0] = 1.0f; OutOrient.E[0][1] = 0.0f; OutOrient.E[0][2] = 0.0f;
    OutOrient.E[1][0] = 0.0f; OutOrient.E[1][1] = 1.0f; OutOrient.E[1][2] = 0.0f;
    OutOrient.E[2][0] = 0.0f; OutOrient.E[2][1] = 0.0f; OutOrient.E[2][2] = 1.0f;
}

CSolar* Solar::csolar() const
{
    CObject* Obj = cobj();
    if (Obj == nullptr)
        return nullptr;
    if ((static_cast<unsigned>(Obj->ObjectClass) & CObject::ClassSolar) != CObject::ClassSolar)
        return nullptr;
    return static_cast<CSolar*>(Obj);
}
