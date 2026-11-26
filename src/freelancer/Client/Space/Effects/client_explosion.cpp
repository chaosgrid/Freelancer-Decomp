// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Space/Effects/client_explosion.h"

#include "DALib.h"
#include "Common/client_cliobj.h"         // Archetype::HookUpInstanceTreeMaterials (FUN_00425480)
#include "Space/client_debris.h"         // FDebrisArch, the Debris gameobj factories
#include "Space/client_gameobj.h"
#include "Space/client_objdb.h"          // ClientObjDb_GetKindHead (FUN_00540fc0)
#include "common_misc.h"           // random_on_unit_sphere / random_inside_sphere
#include "common_physys.h"
#include "common_universe.h"       // Archetype::GetSimpleID / GetSimple
#include "dacom.h"                 // FDUMP
#include "engine/engbase.h"
#include "Space/HUD/hud_damage_indicator.h"  // GetHudDamageIndicator (DAT_00674a10), FUN_004d6f00
#include "Space/Effects/particle_effect.h"       // CliCreateEffectWorldOneShot (FUN_00503f40)
#include "Space/player_behavior.h"       // GetPlayerObjRW (FUN_0054baf0)
#include "Space/star_sys.h"              // StarSysGetActiveSystemId (FUN_00554e90)
#include "Common/Graphics/vis_effect_arch.h"       // FindEffectArch (FUN_0040f080)
#include "x86math.h"               // Transform

#include <cmath>
#include <cstdlib>
#include <cstring>

static_assert(sizeof(Action_Explode) == 0x28, "Action_Explode is IAction::operator new(0x28)");

namespace
{
    char const kExplosionSourceFile[] = "E:\\FL\\Scratch\\Source\\Client\\space\\explosion.cpp";

    // DAT_005caeec: 1/32768, the rand() scale.
    constexpr float kRandScale = 3.0517578125e-05f;
    // DAT_005de598: the piece mass per unit of radius.
    constexpr float kPieceMassPerRadius = 200.0f;
    // DAT_005de590 (double): the share of the radius the shatter piece's sphere gets.
    constexpr double kPieceSphereScale = 0.65;
    // DAT_005dbc98 (double): below this centre distance a piece flies off in a random
    // direction.
    constexpr double kPieceMinDistance = 0.0001;
    // DAT_005cbf58 (double): the scatter radius as a share of debris_impulse.
    constexpr double kPieceScatterShare = 0.25;

    // The Gameobj's object narrowed by (ObjectClass & Mask) == Mask, null otherwise -- the
    // tests every call site here inlines.
    CObject* NarrowGameobjObject(FClientGameobj* Gameobj, unsigned Mask)
    {
        if (Gameobj == nullptr)
            return nullptr;
        CObject* const Obj = Gameobj->Object;
        if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & Mask) != Mask)
            return nullptr;
        return Obj;
    }

    // FUN_0054baf0 -- the player's IObjRW is the player's Gameobj (IObjRW is its first base).
    FClientGameobj* PlayerGameobj()
    {
        return static_cast<FClientGameobj*>(static_cast<IObjRW*>(GetPlayerObjRW()));
    }
}

// ===========================================================================
// Action_Explode
// ===========================================================================

Action_Explode::Action_Explode(Archetype::Explosion* InArch, FClientGameobj* InGameobj,
                               Vector const& InPos)
    : Arch(InArch), Gameobj(InGameobj), Pos(InPos), Lifetime(0.0f), Elapsed(-1.0f),
      PrevElapsed(-1.0f)
{
    // Inlined in FUN_005361e0.
    float const LifetimeMin = Arch->Lifetime;    // +0x18
    float const LifetimeMax = Arch->LifetimeMax; // +0x1c
    Lifetime = static_cast<float>(rand()) * kRandScale * (LifetimeMax - LifetimeMin) +
               LifetimeMin;
}

Action_Explode::~Action_Explode()
{
    // FUN_00536430: IAction::~IAction; the deleting form frees through IAction::operator delete.
}

Action_Explode* Action_Explode::Create(Archetype::Explosion* Arch, FClientGameobj* Gameobj,
                                       Vector const* Pos)
{
    // FUN_005361e0.
    Action_Explode* const Action = new Action_Explode(Arch, Gameobj, *Pos);

    // The impulse reaches the player's ship unless it is the one exploding, then every Gameobj
    // of ObjDB kind 10 (Debris) and kind 9.
    if (Arch->Impulse > 0.0f)
    {
        FClientGameobj* const Player = PlayerGameobj();
        if (Player != nullptr)
        {
            unsigned const OwnerId = Gameobj->get_id();
            if (Player->get_id() != OwnerId)
                Action->ApplyImpulse(Player);
        }
        int Kind = static_cast<int>(EClientGameobjKind::Debris);
        FClientGameobj* Go = ClientObjDb_GetKindHead(&Kind);
        while (Go != nullptr)
        {
            Action->ApplyImpulse(Go);
            Go = Go->ListNext;
        }
        Kind = static_cast<int>(EClientGameobjKind::DynamicAsteroid);
        Go = ClientObjDb_GetKindHead(&Kind);
        while (Go != nullptr)
        {
            Action->ApplyImpulse(Go);
            Go = Go->ListNext;
        }
    }

    // A damaging blast whose radius reaches the player's (other) ship's physical sphere flashes
    // the HUD damage indicator towards it (the radius pushed as its second argument is unread).
    if (Arch->HullDamage > 0.0f || Arch->EnergyDamage > 0.0f)
    {
        FClientGameobj* const Player = PlayerGameobj();
        if (Player != nullptr)
        {
            unsigned const PlayerId = Player->get_id();
            if (PlayerId != Gameobj->get_id())
            {
                float PlayerRadius = 0.0f;
                Vector PlayerCenter;
                Player->get_physical_radius(PlayerRadius, PlayerCenter);
                float const Reach = PlayerRadius + Arch->Radius;
                float const Dx = Action->Pos.x - PlayerCenter.x;
                float const Dy = Action->Pos.y - PlayerCenter.y;
                float const Dz = Action->Pos.z - PlayerCenter.z;
                // Summed dx, dz, dy; strictly greater only.
                if (Reach * Reach > Dx * Dx + Dz * Dz + Dy * Dy)
                {
                    HUD_DamageIndicator* const Indicator = GetHudDamageIndicator();
                    if (Indicator != nullptr)
                    {
                        int RadiusBits = 0;
                        std::memcpy(&RadiusBits, &Arch->Radius, sizeof(RadiusBits));
                        Indicator->FlashTowardsPoint(&Action->Pos, RadiusBits);
                    }
                }
            }
        }
    }

    // The first update, with no time passed.
    Action->update(0.0f);
    return Action;
}

bool Action_Explode::LifetimeEndedThisTick() const
{
    // The lifetime is now <= 0 and either this is the first tick or it was still above 0
    // before it. Retail spells this out inline for a negative effect delay, a negative innards
    // start time and the death. Comparisons are ordered (NaN fails).
    if (!(Lifetime <= 0.0f))
        return false;
    // The first tick (PrevElapsed is still the -1.0f bit pattern).
    if (PrevElapsed == -1.0f)
        return true;
    return Elapsed - PrevElapsed + Lifetime > 0.0f;
}

bool Action_Explode::DelayReachedThisTick(float Delay) const
{
    // Inlined in retail for each effect and for the innards. A delay below 0 or NaN takes the
    // lifetime test.
    if (!(Delay >= 0.0f))
        return LifetimeEndedThisTick();
    if (!(PrevElapsed < Delay))
        return false;
    return Elapsed >= Delay;
}

bool Action_Explode::update(float Dt)
{
    // FUN_00536460. The Gameobj is done (slot 86) -- the action is too.
    if (Gameobj->IsDead())
        return false;

    // Elapsed = Dt the first time (still the -1.0f pattern).
    PrevElapsed = Elapsed;
    if (Elapsed == -1.0f)
        Elapsed = Dt;
    else
        Elapsed = Dt + Elapsed;
    Lifetime = Lifetime - Dt;

    // Every effect whose delay was reached starts as a world one-shot at the blast point with
    // the Gameobj's orientation and velocity (DAT_00674c60, no companion).
    std::vector<Archetype::FExplosionEffect> const& Effects = Arch->Effects;
    size_t Index = 0;
    for (Index = 0; Index < Effects.size(); ++Index)
    {
        Archetype::FExplosionEffect const& Effect = Effects[Index];
        if (!DelayReachedThisTick(Effect.Strength))                       // node +0x0c
            continue;
        // Both read before the id test.
        Transform const& GameobjXform = Gameobj->get_transform();
        Transform World;
        World.Rotation = GameobjXform.Rotation;
        World.Translation.x = Pos.x;
        World.Translation.y = Pos.y;
        World.Translation.z = Pos.z;
        Vector const Velocity = Gameobj->get_velocity();
        if (Effect.Fx.IsEmpty())                                          // node +0x08
            continue;
        CliCreateEffectWorldOneShot(FindEffectArch(&Effect.Fx.Id), &World, &Velocity.x,
                                    nullptr);
    }

    if (Arch->InnardsDebrisNum > 0.0f &&
        DelayReachedThisTick(Arch->InnardsDebrisStartTime))
        SpawnInnardsDebris();

    // The lifetime ran out this tick: unless process = none (2) the Gameobj is finished
    // (slot 87), and process = shatter (0) breaks it up.
    if (LifetimeEndedThisTick())
    {
        if (Arch->Process != Archetype::Explosion::ProcessNone)
            Gameobj->MarkForRemoval(true);
        if (Arch->Process == Archetype::Explosion::ProcessShatter)
            Shatter();
    }

    // Still running while Lifetime > 0 (or NaN).
    return !(Lifetime <= 0.0f);
}

void Action_Explode::ApplyImpulse(FClientGameobj* Target)
{
    // FUN_005366a0. The target's object narrowed by class & 1 -- no null test on the result.
    CObject* const Simple = NarrowGameobjObject(Target, CObject::ClassObject);
    Vector const Center = Simple->get_center_of_mass();

    // Strictly inside the blast radius only.
    float const Radius = Arch->Radius;
    float const Dx = Center.x - Pos.x;
    float const Dy = Center.y - Pos.y;
    float const Dz = Center.z - Pos.z;
    if (!(Radius * Radius > Dz * Dz + Dy * Dy + Dx * Dx))
        return;

    // The direction away from the blast, normalised; a centre exactly on the blast point takes
    // random_on_unit_sphere.
    Vector Direction;
    Direction.x = Center.x - Pos.x;
    Direction.y = Center.y - Pos.y;
    Direction.z = Center.z - Pos.z;
    float const LengthSq =
        Direction.z * Direction.z + Direction.y * Direction.y + Direction.x * Direction.x;
    if (LengthSq == 0.0f)
    {
        Direction = random_on_unit_sphere();
    }
    else
    {
        float const InvLength = 1.0f / std::sqrt(LengthSq);              // DAT_005c75dc = 1.0f
        Direction.x = Direction.x * InvLength;
        Direction.y = Direction.y * InvLength;
        Direction.z = Direction.z * InvLength;
    }

    // Applied at the blast point when the object has a physics body.
    float const Impulse = Arch->Impulse;
    Direction.x = Direction.x * Impulse;
    Direction.y = Direction.y * Impulse;
    Direction.z = Direction.z * Impulse;
    if (Simple->BodyPtr != nullptr)
        PhySys::AddImpulseAtPoint(Simple, Direction, Pos);
}

void Action_Explode::Shatter()
{
    // FUN_00536800. Every piece of the Gameobj's instance tree, at most num_child_pieces.
    long const RootInstance = Gameobj->get_index();
    std::vector<CObject*> Pieces;
    CollectChildPieces(RootInstance, Arch->NumChildPieces, Pieces);

    size_t Index = 0;
    for (Index = 0; Index < Pieces.size(); ++Index)
    {
        CObject* const Piece = Pieces[Index];

        // A piece with no radius is released.
        Vector RadiusCenter;
        if (!(Piece->get_physical_radius_r(RadiusCenter) > 0.0f))
        {
            Piece->Release();
            continue;
        }

        // Its debris record, drawn from debris_type.
        FDebrisArch* const DebrisArch = PickExplosionDebrisArch(&Arch->DebrisTypes);
        if (DebrisArch == nullptr)
            continue;

        // A piece with no physics body gets a sphere: mass = radius *
        // 200, the default damping, type 2 with collisions on, no inertia, the current system,
        // and the exploding object's collision group as the name; sphere radius = radius * 0.65.
        if (Piece->BodyPtr == nullptr)
        {
            PhySys::CreateParms Parms;
            std::memset(&Parms, 0, sizeof(Parms));
            Parms.Mass = 1.0f;
            Parms.LinearDamping = PhySys::DEFAULT_LINEAR_DAMPING;
            Parms.AngularDamping.x = PhySys::DEFAULT_ANGULAR_DAMPING.x;
            Parms.AngularDamping.y = PhySys::DEFAULT_ANGULAR_DAMPING.y;
            Parms.AngularDamping.z = PhySys::DEFAULT_ANGULAR_DAMPING.z;
            Parms.SystemId = 0;
            Parms.Type = 2;
            Parms.bPhysicalUnmoveable = 0;
            Parms.bEnableCollisions = 1;
            Parms.bHasInertia = 0;
            Parms.Inertia.x = 0.0f;
            Parms.Inertia.y = 0.0f;
            Parms.Inertia.z = 0.0f;
            Parms.Name[0] = '\0';
            Vector MassCenter;
            Parms.Mass = Piece->get_physical_radius_r(MassCenter) * kPieceMassPerRadius;
            Parms.SystemId = StarSysGetActiveSystemId();
            // The exploding object's collision group copied into the name (an inline strcpy,
            // no length limit).
            char const* Group = PhySys::GetCollisionGroup(NarrowGameobjObject(Gameobj, CObject::ClassObject));
            char* Out = Parms.Name;
            char Ch = '\0';
            do
            {
                Ch = *Group;
                ++Group;
                *Out = Ch;
                ++Out;
            } while (Ch != '\0');
            Vector SphereCenter;
            float const SphereRadius = static_cast<float>(
                static_cast<double>(Piece->get_physical_radius_r(SphereCenter)) *
                kPieceSphereScale);
            PhySys::BuildIVP_Sphere(Piece, SphereRadius, Parms);
        }

        CreateDebrisGameobjForObject(Piece, DebrisArch);

        // The direction from the Gameobj's centre of mass to the piece's; the Gameobj's
        // velocity is read between the two.
        Vector const GameobjCenter = Gameobj->get_center_of_mass();
        Vector const GameobjVelocity = Gameobj->get_velocity();
        Vector const PieceCenter = PhySys::GetCenterOfMass(Piece);
        Vector Direction;
        Direction.x = PieceCenter.x - GameobjCenter.x;
        Direction.y = PieceCenter.y - GameobjCenter.y;
        Direction.z = PieceCenter.z - GameobjCenter.z;

        // Strictly closer than 0.0001: a random direction.
        double const Dy = static_cast<double>(Direction.y);
        double const Dz = static_cast<double>(Direction.z);
        double const Dx = static_cast<double>(Direction.x);
        double const Distance = std::sqrt(Dy * Dy + Dz * Dz + Dx * Dx);
        if (Distance < kPieceMinDistance)
            Direction = random_on_unit_sphere();

        // Scaled to debris_impulse, plus a random point inside a sphere of a quarter of it.
        float const DebrisImpulse = Arch->DebrisImpulse;
        float const Length = std::sqrt(
            Direction.y * Direction.y + Direction.z * Direction.z + Direction.x * Direction.x);
        float const Scale = DebrisImpulse / Length;
        Direction.x = Direction.x * Scale;
        Direction.y = Direction.y * Scale;
        Direction.z = Direction.z * Scale;
        Vector const Scatter = random_inside_sphere(
            static_cast<float>(static_cast<double>(DebrisImpulse) * kPieceScatterShare));
        Direction.x = Direction.x + Scatter.x;
        Direction.y = Direction.y + Scatter.y;
        Direction.z = Direction.z + Scatter.z;

        PhySys::AddToVelocity(Piece, GameobjVelocity);
        if (Piece->BodyPtr != nullptr)
            PhySys::LinearImpulse(Piece, Direction, 1.0f);
        PhySys::AddToAngularVelocityOS(Piece, random_inside_sphere(2.0f));
    }
}

void Action_Explode::CollectChildPieces(long Instance, int MaxPieces,
                                        std::vector<CObject*>& Pieces)
{
    // FUN_00536c20. `this` is not read.
    IEngine* const Engine = DALib::Engine;
    const InstanceIndex Inst = Instance;

    // An instance with children: each child is collected (which detaches it) and the first
    // child is asked for again until there is none.
    InstanceIndex Child = Engine->GetRelatedInstance(Inst, 1u, InvalidInstanceIndex);
    if (Child != InvalidInstanceIndex)
    {
        do
        {
            CollectChildPieces(reinterpret_cast<long>(Child), MaxPieces, Pieces);
            Child = Engine->GetRelatedInstance(Inst, 1u, InvalidInstanceIndex);
        } while (Child != InvalidInstanceIndex);
        return;
    }

    // A leaf: the root (no parent) stays; any other leaf is detached from its parent after both
    // owners are read.
    const InstanceIndex Parent = Engine->GetInstanceParent(Inst);
    if (Parent == InvalidInstanceIndex)
        return;
    CObject* const Owner = static_cast<CObject*>(Engine->GetInstanceUserData(Inst));
    CObject* const ParentOwner = static_cast<CObject*>(Engine->GetInstanceUserData(Parent));
    Engine->DestroyJoint(Parent, Inst);

    // The piece count compares unsigned.
    if (Owner == ParentOwner)
    {
        // A part of the parent's own object: a new object for it, opened on the parent object's
        // archetype, while there is room; else the instance is destroyed.
        if (Pieces.size() < static_cast<unsigned>(MaxPieces))
        {
            CObject* const Piece = CObject::Alloc(CObject::ClassObject);
            Engine->SetInstanceTransform(Inst, static_cast<IEngineInstance*>(Piece));
            Piece->open(ParentOwner->get_archetype());
            Pieces.push_back(Piece);
        }
        else
        {
            Engine->DestroyInstance(Inst);
        }
        return;
    }

    // An object of its own. While there is room: a body in mode 2 is
    // detached from the compound (the default damping, mass = radius * 200, no inertia) and
    // its collisions turned on, any other is deactivated; then it is collected. Without room it
    // is released.
    if (Pieces.size() < static_cast<unsigned>(MaxPieces))
    {
        if (Owner->PhysicsControlMode == 2)
        {
            PhySys::DetachParms Parms;
            std::memset(&Parms, 0, sizeof(Parms));
            Parms.LinearDamping = PhySys::DEFAULT_LINEAR_DAMPING;
            Parms.AngularDamping = PhySys::DEFAULT_ANGULAR_DAMPING;
            Parms.Mass = 1.0f;
            Parms.bHasInertia = 0;
            Parms.Inertia.x = 0.0f;
            Parms.Inertia.y = 0.0f;
            Parms.Inertia.z = 0.0f;
            Parms.Name[0] = '\0';
            Vector Center;
            Parms.Mass = Owner->get_physical_radius_r(Center) * kPieceMassPerRadius;
            PhySys::Detach(Owner, Parms);
            Owner->enable_collisions_r(true);
        }
        else
        {
            PhySys::DeactivatePhysics(Owner);
        }
        Pieces.push_back(Owner);
    }
    else
    {
        Owner->Release();
    }
}

void Action_Explode::SpawnInnardsDebris()
{
    // FUN_00536fb0. The spawn radius: innards_debris_radius when it is > 0, else the Gameobj's
    // physical radius (class & 1 narrowing, no null test).
    CObject* const Simple = NarrowGameobjObject(Gameobj, CObject::ClassObject);
    Vector RadiusCenter;
    float SpawnRadius = Simple->get_physical_radius_r(RadiusCenter);
    if (Arch->InnardsDebrisRadius > 0.0f)
        SpawnRadius = Arch->InnardsDebrisRadius;

    // No innards_debris_object entries is an error. The count goes to FDUMP as the double it
    // was pushed as (the format says %d).
    std::vector<unsigned> const& Objects = Arch->InnardsDebrisObjects;
    int const ObjectCount = static_cast<int>(Objects.size());
    if (ObjectCount <= 0)
    {
        FDUMP(kFdumpError,
              "%s(%d) : *** ERROR: [Explosion] %s has innards_debris_num = %d, but has no "
              "'innards_debris_object =' entries",
              kExplosionSourceFile, 0x21e, Arch->Nickname.get_string(),
              static_cast<double>(Arch->InnardsDebrisNum));
        return;
    }

    if (!(0.0f < Arch->InnardsDebrisNum))
        return;

    int Spawned = 0;
    do
    {
        // Drawn uniformly, rounded towards zero.
        int const Pick = (rand() * ObjectCount) / 32768;
        Vector const Offset = random_inside_sphere(SpawnRadius);
        Vector SpawnPos;
        SpawnPos.x = Offset.x + Pos.x;
        SpawnPos.y = Offset.y + Pos.y;
        SpawnPos.z = Offset.z + Pos.z;
        ID_String ObjectName;
        ObjectName.Id = Objects[Pick];
        Archetype::Root* const ObjectArch =
            Archetype::GetSimple(Archetype::GetSimpleID(ObjectName));
        FDebrisArch* const DebrisArch = PickExplosionDebrisArch(&Arch->DebrisTypes);
        if (DebrisArch != nullptr)
        {
            // Its object (class & 1, no null test) joins the exploding object's collision group.
            FClientDebrisGameobj* const Debris =
                CreateDebrisGameobjFromArch(ObjectArch, DebrisArch, &SpawnPos);
            CObject* const DebrisObject = NarrowGameobjObject(Debris, CObject::ClassObject);
            CObject* const Source = NarrowGameobjObject(Gameobj, CObject::ClassObject);
            PhySys::SetCollisionGroup(DebrisObject, PhySys::GetCollisionGroup(Source));
            Archetype::HookUpInstanceTreeMaterials(DebrisObject->EngineInstanceId);

            // Flung along the spawn offset at rand()/32768 * 2 * debris_impulse, plus the
            // Gameobj's velocity.
            float const Speed2 = Arch->DebrisImpulse + Arch->DebrisImpulse;
            Vector Velocity = Offset;
            float const Scale =
                static_cast<float>(rand()) * kRandScale * Speed2 /
                std::sqrt(Velocity.y * Velocity.y + Velocity.z * Velocity.z +
                          Velocity.x * Velocity.x);
            Velocity.x = Velocity.x * Scale;
            Velocity.y = Velocity.y * Scale;
            Velocity.z = Velocity.z * Scale;
            Vector const GameobjVelocity = Gameobj->get_velocity();
            Velocity.x = Velocity.x + GameobjVelocity.x;
            Velocity.y = Velocity.y + GameobjVelocity.y;
            Velocity.z = Velocity.z + GameobjVelocity.z;
            PhySys::AddToVelocity(DebrisObject, Velocity);
            PhySys::AddToAngularVelocityOS(DebrisObject, random_inside_sphere(5.0f));
        }
        ++Spawned;
    } while (static_cast<float>(Spawned) < Arch->InnardsDebrisNum);
}

FDebrisArch* PickExplosionDebrisArch(std::vector<Archetype::FExplosionDebrisType> const* Types)
{
    // FUN_00536b50.
    float const Roll = static_cast<float>(rand()) * kRandScale;
    // The running sum stays on the x87 stack; double is the closest the port has.
    double Sum = 0.0;
    int const Count = static_cast<int>(Types->size());
    int Index = 0;
    for (Index = 0; Index < Count; ++Index)
    {
        Sum = Sum + static_cast<double>((*Types)[Index].Count);
        if (static_cast<double>(Roll) < Sum)
            break;
    }
    // Past the end: the default record. (Retail's Index == -1 test after this can never be
    // taken.)
    if (Index >= Count)
        return GetDefaultDebrisArch();

    // A miss warns and falls back to the default.
    Archetype::FExplosionDebrisType const& Drawn = (*Types)[Index];
    FDebrisArch* const Found = FindDebrisArch(&Drawn.Debris);
    if (Found != nullptr)
        return Found;
    FDUMP(kFdumpWarning,
          "%s(%d) : *** WARNING: Exploding: Couldn't find debris_type[%s]. using default.",
          kExplosionSourceFile, 0x70, Drawn.Debris.get_string());
    return GetDefaultDebrisArch();
}
