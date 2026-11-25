// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Server.dll Loot wrapper -- retail Loot.cpp ("E:\FL\Scratch\Source\Server\Space\Loot.cpp",
// .rdata:06d65f9c), .text:06cf24c0-06cf2c03. The class is declared in
// src/include/server_gameobject.h; its constructor, find, get_type_bits and cloot live in
// server_gameobject.cpp.

#include "../include/server_gameobject.h"
#include "../include/server_sysdb.h"   // StarSystem::explode / find_object, StarSystem::damage_object
#include "../include/common_misc.h"    // LOOT_UNSEEN_*, SUBOBJ_ID_ROOT / SUBOBJ_ID_POWER
#include "../include/common_id.h"      // ID_String::IsEmpty / get_string
#include "../include/dacom.h"          // FDUMP
#include "../include/DALib.h"          // DALib::Engine (Loot::create, IEngine +0x6c)

#include <cstdlib>
#include <list>
#include <vector>

// Server_ControllerDispatch is pub::Controller::_SendMessage's dispatch core; retail
// FUN_06d43650 is `_SendMessage(0 /* broadcast */, MsgId, MsgData)`.

namespace
{
// .rdata:06d65f9c -- the __FILE__ every Loot.cpp FDUMP passes.
char const kLootCppPath[] = "E:\\FL\\Scratch\\Source\\Server\\Space\\Loot.cpp";
} // namespace

// ---------------------------------------------------------------------------
// FUN_06cf2440 -- vtable +0x164.
// ---------------------------------------------------------------------------
bool Loot::create(Archetype::Root* Arch)
{
    Object = CObject::Alloc(CObject::ClassLoot);

    // The CLoot narrowing is applied only to the open() call, with no null test.
    CObject* Narrowed = Object;
    if (Narrowed == nullptr ||
        (static_cast<unsigned>(Narrowed->ObjectClass) & CObject::ClassLoot) != CObject::ClassLoot)
    {
        Narrowed = nullptr;
    }
    Narrowed->open(Arch);

    advise(true);

    // Unlike Ship/Solar, Loot makes the engine instance after open and advise; the engine
    // stores the id at CObject+4 through IEngineInstance::initialize_instance.
    Arch->create_instance(static_cast<IEngineInstance*>(Object));

    // No null test on the engine.
    DALib::Engine->UpdateInstance(reinterpret_cast<void*>(Object->EngineInstanceId), 0, nullptr);

    return get_index() != -1;
}

// ---------------------------------------------------------------------------
// LAB_06cf24c0 -- vtable +0x16c.
// ---------------------------------------------------------------------------
bool Loot::update(float Dt)
{
    if (!GameObject::update(Dt))
        return false;

    // Player-owned loot updates as a dumb (client-driven) object. The CSimple narrowing
    // (+0xb4) is not null-tested; the update goes through the raw CBase::Object.
    unsigned int const Mode = (csimple()->OwnerPlayer != 0) ? CObject::UPDATE_DUMB
                                                            : CObject::UPDATE_DEFAULT;
    Object->update(Dt, Mode);

    update_decay(Dt); // result ignored

    if (is_destroyed())
        return false;

    // Invincible loot never times out.
    if (!is_invincible())
    {
        // The compare reads the unrounded double sum, not the stored float.
        double const NewCheckTimer = static_cast<double>(Dt) + static_cast<double>(CheckTimer);
        CheckTimer = static_cast<float>(NewCheckTimer);
        UnseenLife = UnseenLife - Dt;
        // Scan only once more than 3.0f (.rdata:06d65f90) seconds have passed.
        if (NewCheckTimer > 3.0)
        {
            // RadiusSq is rounded to float; the IObjDB cursor {0, 0, 0x7f0000} walks the
            // system's ships.
            float const RadiusSq = LOOT_UNSEEN_RADIUS * LOOT_UNSEEN_RADIUS;
            unsigned int Cursor[3];
            Cursor[0] = 0;
            Cursor[1] = 0;
            Cursor[2] = 0x7f0000u;
            IObjRW* Other = OwnerSystem->GetNext(Cursor);
            while (Other != nullptr)
            {
                if (Other->is_player())
                {
                    // Summed x, z, then y in double precision; strictly inside the radius
                    // resets the unseen life.
                    Vector const& Mine = get_position();
                    Vector const& Theirs = Other->get_position();
                    double const Dx = static_cast<double>(Theirs.x) - static_cast<double>(Mine.x);
                    double const Dy = static_cast<double>(Theirs.y) - static_cast<double>(Mine.y);
                    double const Dz = static_cast<double>(Theirs.z) - static_cast<double>(Mine.z);
                    double const DistSq = (Dx * Dx + Dz * Dz) + Dy * Dy;
                    if (DistSq < static_cast<double>(RadiusSq))
                        UnseenLife = LOOT_UNSEEN_LIFE_TIME;
                }
                Other = OwnerSystem->GetNext(Cursor);
            }

            // Expired loot is removed and the scan timer is left running; otherwise the
            // timer restarts.
            if (UnseenLife < 0.0f)
                destroy(0, get_id());
            else
                CheckTimer = 0.0f;
        }
    }

    return !is_destroyed();
}

// ---------------------------------------------------------------------------
// FUN_06cf2660 -- vtable +0x158.
// ---------------------------------------------------------------------------
bool Loot::destroy(int Kind, unsigned int ObjId)
{
    // Controller message 0x39 with an empty drop list; read order is id, system id
    // (FUN_06d06cd0), position.
    FObjectDestroyedEvent Event;
    Event.ObjId = get_id();
    Event.KillerId = ObjId;
    Event.SystemId = OwnerSystem->SystemInfo->NicknameId;
    Event.OwnerPlayer = 0;
    Event.Kind = Kind;
    Event.Position = get_position();
    Server_ControllerDispatch(0, pub::Controller::EMessage::ObjectDestroyed, &Event);

    // GameObject's destroy (FUN_06cee630); its result is not used.
    GameObject::destroy(Kind, ObjId);

    // A real kill also blows the crate up and spills its cargo.
    if (Kind == 1)
    {
        explode_on_destroy(); // FUN_06cf2b10
        split_stack();        // FUN_06cf2c10
    }

    return true;
}

// ---------------------------------------------------------------------------
// FUN_06cf2750 -- vtable +0x188.
// ---------------------------------------------------------------------------
void Loot::apply_damage_list(DamageList& Damage)
{
    // Re-entry is reported, not refused. Retail evaluates the cause string, then the
    // archetype nickname, then the id.
    if (is_in_damage_callback())
    {
        char const* const CauseName = DamageList::DmgCauseToString(Damage.Cause);
        char const* const ArchName = cloot()->get_archetype()->Nickname.get_string();
        unsigned int const Id = get_id();
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: ID(0x%X) Arch(%s): Tried to apply damage of type %s within "
              "another damage callback.",
              kLootCppPath, 0xa1, Id, ArchName, CauseName);
    }

    set_in_damage_callback(true);

    // An empty list skips the entries and the system broadcast.
    if (Damage.Count != 0)
    {
        for (std::list<DamageEntry>::iterator It = Damage.Entries.begin();
             It != Damage.Entries.end(); ++It)
        {
            DamageEntry const& Entry = *It;
            if (Entry.SubObjId == SUBOBJ_ID_ROOT)
            {
                apply_root_damage(Entry);
            }
            else if (Entry.HitPts > 0.0f)
            {
                // A positive value becomes the current unit's hit points (CLoot+0xf4).
                cloot()->ContentsHitPts = Entry.HitPts;
            }
            else
            {
                // One unit is gone (CLoot+0xf0) and the next starts at the contents
                // archetype's full hit points.
                cloot()->Units = cloot()->Units - 1;
                cloot()->ContentsHitPts = cloot()->ContentsArch->HitPts;
            }
        }
        // FUN_06d0b800.
        OwnerSystem->damage_object(this, &Damage);
    }

    // The invincibility gate (+0x1b8) can block the kill.
    if (Damage.bDestroyed && !is_damage_blocked(Damage) && !is_destroyed())
        destroy(1, Damage.InflictorId);

    set_in_damage_callback(false);
}

// ---------------------------------------------------------------------------
// FUN_06cf2910.
// ---------------------------------------------------------------------------
bool Loot::update_decay(float Dt)
{
    // Only commodities decay; the CLoot narrowing is not null-tested.
    Archetype::Equipment const* const Contents = cloot()->ContentsArch;
    if (Contents->get_class_type() != Archetype::AClassCommodity)
        return true;

    // The product and the sum below stay on the FPU stack, so both compares read the
    // unrounded x87 values.
    double const Decay =
        static_cast<double>(static_cast<Archetype::Commodity const*>(cloot()->ContentsArch)
                                ->get_decay_per_second()) *
        static_cast<double>(Dt);
    if (!(Decay > 0.0))
        return true;

    // More than a whole unit's hit points (strictly greater) takes a unit off.
    double const NewAccum = Decay + static_cast<double>(DecayAccum);
    DecayAccum = static_cast<float>(NewAccum);
    if (!(NewAccum > static_cast<double>(cloot()->ContentsArch->HitPts)))
        return true;

    if (cloot()->Units == 1)
    {
        destroy(1, get_id());
        return false;
    }

    // Cause 0x14 with one non-positive SUBOBJ_ID_POWER entry; apply_damage_list (+0x188)
    // turns that into "one unit fewer".
    {
        DamageList Damage;
        Damage.Cause = DamageCause::LootDecay;
        Damage.add_damage_entry(SUBOBJ_ID_POWER, 0.0f, DamageEntry::SubObjFate::None);
        apply_damage_list(Damage);
    }

    DecayAccum = DecayAccum - cloot()->ContentsArch->HitPts;
    return true;
}

// ---------------------------------------------------------------------------
// FUN_06cf2b10.
// ---------------------------------------------------------------------------
void Loot::explode_on_destroy()
{
    // explosion_arch is Root+0x24.
    Archetype::Root* const Arch = cloot()->get_archetype();
    ID_String const& ExplosionId = Arch->ExplosionArch;
    if (ExplosionId.IsEmpty())
        return;

    Archetype::Explosion* const Explosion = Archetype::GetExplosion(ExplosionId);
    if (Explosion == nullptr)
        return;

    // Needs a radius and at least one of impulse, hull or energy damage (double 0.0001 at
    // .rdata:06d65190).
    double const Epsilon = 0.0001;
    if (!(static_cast<double>(Explosion->Radius) > Epsilon))
        return;
    if (!(static_cast<double>(Explosion->Impulse) > Epsilon) &&
        !(static_cast<double>(Explosion->HullDamage) > Epsilon) &&
        !(static_cast<double>(Explosion->EnergyDamage) > Epsilon))
    {
        return;
    }

    // StarSystem::explode (FUN_06d0b260).
    ExplosionDamageEvent Blast;
    Blast.InflictorId = 0;
    Blast.SkipCollisionGroup = nullptr;
    Blast.ExplodingObjId = get_id();
    Blast.Cause = DamageCause::Explosion;
    Blast.Position = get_position();
    Blast.Explosion = Explosion;
    OwnerSystem->explode(Blast);
}

// ---------------------------------------------------------------------------
// FUN_06cf2c10: a destroyed CargoPod spills its units as new loot crates.
// ---------------------------------------------------------------------------
void Loot::split_stack()
{
    if (cloot()->get_archetype()->get_class_type() != Archetype::AClassCargoPod)
        return;

    // Unsigned compare of Units (CLoot+0xf0) with UnitsPerContainer (ContentsArch+0x64):
    // nothing to split unless strictly more.
    unsigned int Remaining = cloot()->Units;
    unsigned int const PerContainer =
        static_cast<unsigned int>(cloot()->ContentsArch->UnitsPerContainer);
    if (!(PerContainer < Remaining))
        return;

    // 0x6c-byte records.
    std::vector<FLootRecord> Records;
    CFLIDMaker* const Maker = CFLIDMaker::GetInstance();

    // At most five records, even if units remain.
    int Index = 0;
    while (Remaining != 0 && Index <= 4)
    {
        Index = Index + 1;

        // FUN_06cf3040 appends a template: first eight dwords zero (no player to mark, +0x18,
        // no name id, +0x1c), +0x68 = 1, +0x69 = 0; the rest is written below.
        FLootRecord Template = {};
        Template.bCanAiTractor = 1;
        Template.bMission = 0;
        Records.push_back(Template);
        FLootRecord& Record = Records.back();

        // The maker's dynamic counter, post-incremented, 30 bits.
        Record.Id = Maker->AllocateObjectID();
        // CLoot+0xec / +0xf4.
        Record.ContentsArch = cloot()->ContentsArch;
        Record.ContentsHitPts = cloot()->ContentsHitPts;
        Record.Count = static_cast<int>((Remaining > PerContainer) ? PerContainer : Remaining);
        // The crate is the contents' loot appearance (Equipment vt+0x1c).
        Archetype::Equipment* const Appearance =
            const_cast<Archetype::Equipment*>(Record.ContentsArch)->get_loot_appearance();
        Record.Arch = Appearance;
        Record.HitPts = Appearance->HitPts;
        Record.Position = get_position();
        Record.Orientation = get_transform().Rotation;
        Record.Velocity = get_velocity();

        // Random push: rand() * 2^-15 (.rdata:06d656b8) kept as a double and doubled, times
        // random_on_unit_sphere (stored as floats), times 10.0f (.rdata:06d65f98).
        int const SpeedRoll = rand();
        double const Speed = static_cast<double>(SpeedRoll) * (1.0 / 32768.0);
        Vector const Direction = random_on_unit_sphere();
        double const Speed2 = Speed + Speed;
        Vector Push;
        Push.x = static_cast<float>(Speed2 * static_cast<double>(Direction.x));
        Push.y = static_cast<float>(Speed2 * static_cast<double>(Direction.y));
        Push.z = static_cast<float>(Speed2 * static_cast<double>(Direction.z));
        Vector Kick;
        Kick.x = static_cast<float>(static_cast<double>(Push.x) * 10.0);
        Kick.y = static_cast<float>(static_cast<double>(Push.y) * 10.0);
        Kick.z = static_cast<float>(static_cast<double>(Push.z) * 10.0);
        Record.Velocity.x = static_cast<float>(static_cast<double>(Kick.x) +
                                               static_cast<double>(Record.Velocity.x));
        Record.Velocity.y = static_cast<float>(static_cast<double>(Kick.y) +
                                               static_cast<double>(Record.Velocity.y));
        Record.Velocity.z = static_cast<float>(static_cast<double>(Kick.z) +
                                               static_cast<double>(Record.Velocity.z));

        // Spin about the crate's first column, scaled by rand() * 2^-15 * 0.8 - 0.4
        // (doubles at .rdata:06d656b0 / 06d656a8), the scale rounded to float.
        Matrix const& Rot = Record.Orientation;
        int const SpinRollA = rand();
        float const SpinA = static_cast<float>(
            static_cast<double>(SpinRollA) * (1.0 / 32768.0) * 0.8 - 0.4);
        Vector SpinX;
        SpinX.x = static_cast<float>(static_cast<double>(Rot.E[0][0]) * static_cast<double>(SpinA));
        SpinX.y = static_cast<float>(static_cast<double>(Rot.E[1][0]) * static_cast<double>(SpinA));
        SpinX.z = static_cast<float>(static_cast<double>(Rot.E[2][0]) * static_cast<double>(SpinA));

        // The same about the third column.
        int const SpinRollB = rand();
        float const SpinB = static_cast<float>(
            static_cast<double>(SpinRollB) * (1.0 / 32768.0) * 0.8 - 0.4);
        Vector SpinZ;
        SpinZ.x = static_cast<float>(static_cast<double>(Rot.E[0][2]) * static_cast<double>(SpinB));
        SpinZ.y = static_cast<float>(static_cast<double>(Rot.E[1][2]) * static_cast<double>(SpinB));
        SpinZ.z = static_cast<float>(static_cast<double>(Rot.E[2][2]) * static_cast<double>(SpinB));

        // Record +0x5c.
        Record.AngularVelocity.x =
            static_cast<float>(static_cast<double>(SpinX.x) + static_cast<double>(SpinZ.x));
        Record.AngularVelocity.y =
            static_cast<float>(static_cast<double>(SpinX.y) + static_cast<double>(SpinZ.y));
        Record.AngularVelocity.z =
            static_cast<float>(static_cast<double>(SpinX.z) + static_cast<double>(SpinZ.z));

        Remaining = Remaining - static_cast<unsigned int>(Record.Count);
    }

    // The system's loot spawner (FUN_06d08a40) in mode 2; retail reads this crate's id only
    // after the emptiness test.
    if (!Records.empty())
    {
        int const Count = static_cast<int>(Records.size());
        OwnerSystem->create_loot(get_id(), 2, &Records[0], Count);
    }
}
