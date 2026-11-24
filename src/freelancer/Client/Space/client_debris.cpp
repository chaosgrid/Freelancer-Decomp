// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Space/client_debris.h"

#include "Space/Effects/client_explosion.h"   // Action_Explode (FUN_005361e0)
#include "Space/client_objdb.h"       // ClientObjDb_RegisterGameobj (FUN_00540c30)
#include "common_csys.h"        // LookMatrix, Acquire3DMathEngine
#include "common_ini.h"         // INI_Reader
#include "common_physys.h"
#include "common_universe.h"    // Archetype::GetExplosion
#include "dacom.h"              // FDUMP
#include "GameFlow/gf_object_ship.h"     // ShipTransposeRotate (FUN_00447be0)
#include "Space/Effects/particle_effect.h"    // CliCreateEffectOneShot (FUN_00503070)
#include "Space/star_sys.h"           // StarSysGetActiveSystemId / StarSysFindNebula
#include "Common/Graphics/vis_effect_arch.h"    // FindEffectArch (FUN_0040f080)
#include "x86math.h"            // I3DMathEngine, FTransform

#include <cfloat>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <utility>

namespace
{
    char const kDebrisSourceFile[] = "E:\\FL\\Scratch\\Source\\Client\\space\\Debris.cpp";

    // DAT_005caeec: 1/32768, the rand() scale.
    constexpr float kRandScale = 3.0517578125e-05f;

    // DAT_006754f4 -- the [Debris] table, keyed by the nickname id (the static initialiser
    // LAB_00530860 builds it, LAB_00530890 tears it down at exit).
    std::map<unsigned, FDebrisArch> G_DebrisArchDb;

    // DAT_006754d0 -- the record FUN_00530a10 hands out when a name is unknown (LAB_00530960).
    FDebrisArch G_DefaultDebrisArch;

    // DAT_00675514 -- Debris.cpp's lazily acquired 3DMathEngine.
    I3DMathEngine* G_DebrisMathEngine = nullptr;

    // The object narrowed by (ObjectClass & Mask) == Mask, null otherwise (inlined by
    // FUN_005306e0).
    CObject* NarrowDebrisObject(FClientGameobj* Gameobj, unsigned Mask)
    {
        if (Gameobj == nullptr)
            return nullptr;
        CObject* const Obj = Gameobj->Object;
        if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & Mask) != Mask)
            return nullptr;
        return Obj;
    }
}

// ===========================================================================
// The [Debris] table
// ===========================================================================

FDebrisArch::FDebrisArch()
{
    LifetimeMin = 0.0f;
    LifetimeMax = FLT_MAX; // 0x7f7fffff
    DeathMethod = 1;
    Explosion.clear();
    Trail.clear();
    LinearDrag = 0.0f;
    AngularDrag.x = 0.0f;
    AngularDrag.y = 0.0f;
    AngularDrag.z = 0.0f;
}

bool ReadDebrisArch(INI_Reader* Reader, bool bQuiet)
{
    // FUN_00530a20
    FDebrisArch Arch;
    ID_String Nickname;
    Nickname.clear();

    while (Reader->read_value())
    {
        if (Reader->is_value("nickname"))
        {
            Nickname.Id = CreateID(Reader->get_value_string());
        }
        else if (Reader->is_value("trail"))
        {
            Arch.Trail.Id = CreateID(Reader->get_value_string());
        }
        else if (Reader->is_value("lifetime"))
        {
            // The second value is read again for the store.
            Arch.LifetimeMin = Reader->get_value_float(0);
            if (Reader->is_value_empty(1))
                Arch.LifetimeMax = Arch.LifetimeMin;
            else if (Reader->get_value_float(1) < Arch.LifetimeMin)
                Arch.LifetimeMax = Arch.LifetimeMin;
            else
                Arch.LifetimeMax = Reader->get_value_float(1);
        }
        else if (Reader->is_value("explosion"))
        {
            Arch.Explosion.Id = CreateID(Reader->get_value_string());
        }
        else if (Reader->is_value("death_method"))
        {
            // Retail compares a 15-character copy; the cut cannot turn a longer value into
            // either name, so comparing the value itself decides the same. Any other value
            // leaves the method as it was.
            char const* const Method = Reader->get_value_string();
            if (Method != nullptr)
            {
                if (_stricmp(Method, "exploding") == 0)
                    Arch.DeathMethod = 0;
                else if (_stricmp(Method, "vanishing") == 0)
                    Arch.DeathMethod = 1;
            }
        }
        else if (Reader->is_value("linear_drag"))
        {
            Arch.LinearDrag = Reader->get_value_float(0);
        }
        else if (Reader->is_value("angular_drag"))
        {
            // Read as values 2, 1, 0.
            float const Z = Reader->get_value_float(2);
            float const Y = Reader->get_value_float(1);
            float const X = Reader->get_value_float(0);
            Arch.AngularDrag.x = X;
            Arch.AngularDrag.y = Y;
            Arch.AngularDrag.z = Z;
        }
    }

    // An existing nickname keeps its first record (FUN_005310c0 does not replace), despite
    // what the warning text says.
    if (Nickname.IsEmpty())
        return false;
    std::pair<std::map<unsigned, FDebrisArch>::iterator, bool> const Inserted =
        G_DebrisArchDb.insert(std::make_pair(Nickname.Id, Arch));
    if (Inserted.second)
        return true;
    if (!bQuiet)
    {
        FDUMP(0x100002,
              "%s(%d) : *** WARNING: Duplicate [Debris] name(%s).  First overwritten with second!",
              kDebrisSourceFile, 0xf8, Nickname.get_string());
    }
    return false;
}

int LoadDebrisArchFile(char const* Path, bool bQuiet)
{
    // FUN_00530d20
    size_t const SizeBefore = G_DebrisArchDb.size();
    INI_Reader Reader;
    if (Reader.open(Path, false))
    {
        while (Reader.read_header())
        {
            if (Reader.is_header("Debris"))
                ReadDebrisArch(&Reader, bQuiet);
        }
    }
    else
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: Failed to open debris file[%s].",
              kDebrisSourceFile, 0x147, Path);
    }
    return static_cast<int>(G_DebrisArchDb.size() - SizeBefore);
}

void ShutdownDebrisArchDb()
{
    // FUN_00530fa0
    G_DebrisArchDb.clear();
}

FDebrisArch* FindDebrisArch(ID_String const* Nickname)
{
    // FUN_005309c0
    std::map<unsigned, FDebrisArch>::iterator const It = G_DebrisArchDb.find(Nickname->Id);
    if (It == G_DebrisArchDb.end())
        return nullptr;
    return &It->second;
}

FDebrisArch* GetDefaultDebrisArch()
{
    // FUN_00530a10
    return &G_DefaultDebrisArch;
}

// ===========================================================================
// Debris
// ===========================================================================

FClientDebrisGameobj::FClientDebrisGameobj()
    : FClientGameobj(EClientGameobjKind::Debris)
{
    // The death method and the two lifetimes are left for Init.
    Explosion = nullptr;
    Remaining = 0.0f;
    Field_0xbc = 1;
}

FClientDebrisGameobj::~FClientDebrisGameobj()
{
    // FUN_00530160, then GameObject's destructor FUN_005394b0.
    if (Explosion != nullptr)
        ExplosionArchRelease(Explosion);
}

int FClientDebrisGameobj::get_type(uint& OutType) const
{
    // LAB_00530610
    OutType = 0x80000000u;
    return 0;
}

void FClientDebrisGameobj::create(Archetype::Root* Arch)
{
    // FUN_005304b0: no narrowing, no null test on the new object.
    GameobjCreateOfClass(this, CObject::ClassObject, Arch);
    Object->open(Arch);
}

bool FClientDebrisGameobj::find(unsigned const& Id)
{
    // FUN_0053f330
    (void)Id;
    return false;
}

void FClientDebrisGameobj::update(float Dt)
{
    // LAB_005304d0
    Object->update(Dt, CObject::UPDATE_DEFAULT);
    FClientGameobj::update(Dt);

    if (!(Remaining > 0.0f))
        return;
    Remaining = Remaining - Dt;

    // A vanishing piece (method 1) with Field_0xbc clear goes at once when it has lived
    // longer than lifetime min.
    int const Method = DeathMethod;
    if (Method == 1 && Field_0xbc == 0)
    {
        if (LifetimeMax - Remaining > LifetimeMin)
            Remaining = 0.0f;
    }

    // At 0 or below (ordered): exploding (0) goes through slot 108 with no position,
    // vanishing (1) finishes the Gameobj (slot 87).
    if (Remaining <= 0.0f)
    {
        if (Method == 0)
        {
            Explode(nullptr);
            return;
        }
        if (Method == 1)
            MarkForRemoval(true);
    }
}

unsigned* FClientDebrisGameobj::PickLod(unsigned* OutLod, void* Camera,
                                        unsigned const* CachedLod)
{
    // FUN_005300f0: GameObject's pick FUN_00539820 into a local, its return value ignored.
    unsigned Lod = 0;
    FClientGameobj::PickLod(&Lod, Camera, CachedLod);
    // Unsigned low word, signed high word.
    unsigned short const LowWord = static_cast<unsigned short>(Lod & 0xffffu);
    short const HighWord = static_cast<short>(static_cast<unsigned short>(Lod >> 16));
    if (LowWord < 3u || HighWord >= 2)
        Field_0xbc = 0;
    *OutLod = Lod;
    return OutLod;
}

void FClientDebrisGameobj::Explode(Vector const* Pos)
{
    // LAB_005305a0
    if (Explosion == nullptr)
    {
        FClientGameobj::Explode(Pos); // FUN_0053a880
        return;
    }
    // Unlike GameObject's slot 108 the effect list is not cleared.
    Vector const* const Where = (Pos != nullptr) ? Pos : &get_position();
    Vector const Copy = *Where;
    Actions.add(Action_Explode::Create(Explosion, this, &Copy));
}

void FClientDebrisGameobj::Init(FDebrisArch const* Arch)
{
    // FUN_005301d0
    // GameObject's slot 92 FUN_00539740, called directly. It reads bWatchListed only after
    // get_type, and Debris's get_type (0x80000000) has none of the bits it tests.
    FClientGameobj::PostCreateInit();

    // The retail dword store at +0xa8 overwrites the bWatchListed byte too.
    DeathMethod = Arch->DeathMethod;
    bWatchListed = static_cast<unsigned char>(DeathMethod & 0xff);
    LifetimeMin = Arch->LifetimeMin;
    LifetimeMax = Arch->LifetimeMax;

    if (!Arch->Explosion.IsEmpty())
    {
        Explosion = Archetype::GetExplosion(Arch->Explosion);
        if (Explosion != nullptr)
            ExplosionArchAddRef(Explosion);
    }
    else
    {
        Explosion = nullptr;
    }

    float const Min = LifetimeMin;
    float const Max = LifetimeMax;
    Remaining = static_cast<float>(rand()) * kRandScale * (Max - Min) + Min;

    // The trail -- a one-shot on this Gameobj, pointing along the velocity at the centre of
    // mass (get_position() without a body), expressed in the object's frame.
    if (!Arch->Trail.IsEmpty())
    {
        Vector Where;
        if (Object->BodyPtr != nullptr)
            Where = Object->get_center_of_mass();
        else
            Where = get_position();
        Vector const Velocity = get_velocity();
        Matrix const Look = LookMatrix(Velocity);
        FTransform World;
        World.Rotation = reinterpret_cast<FMatrix const&>(Look);
        World.Translation.X = Where.x;
        World.Translation.Y = Where.y;
        World.Translation.Z = Where.z;

        // The inverse of the object's frame: rotation transposed (FUN_00447b60), translation
        // -(M^T * P) (FUN_00447be0); FUN_00447bb0 packs the two.
        Transform const& ObjectFrame = reinterpret_cast<Transform const&>(get_orientation());
        FMatrix const& Rotation = reinterpret_cast<FMatrix const&>(ObjectFrame.Rotation);
        FVector ObjectPos;
        ObjectPos.X = ObjectFrame.Translation.x;
        ObjectPos.Y = ObjectFrame.Translation.y;
        ObjectPos.Z = ObjectFrame.Translation.z;
        FVector Rotated;
        ShipTransposeRotate(Rotated, Rotation, ObjectPos);
        FTransform Inverse;
        int Row = 0;
        for (Row = 0; Row < 3; ++Row)
        {
            int Col = 0;
            for (Col = 0; Col < 3; ++Col)
                Inverse.Rotation.D[Row][Col] = Rotation.D[Col][Row];
        }
        Inverse.Translation.X = -Rotated.X;
        Inverse.Translation.Y = -Rotated.Y;
        Inverse.Translation.Z = -Rotated.Z;

        if (G_DebrisMathEngine == nullptr)
            G_DebrisMathEngine = Acquire3DMathEngine();
        FTransform Local;
        G_DebrisMathEngine->MulTransform(Local, Inverse, World);

        // The trail id is tested again (retail).
        int const Index = static_cast<int>(get_index());
        if (!Arch->Trail.IsEmpty())
            CliCreateEffectOneShot(FindEffectArch(&Arch->Trail.Id), this, Index, &Local, nullptr);
    }

    // Drag divided by the mass and by the moment of inertia (zero when the body is gone by the
    // second test).
    if (Object->BodyPtr != nullptr)
    {
        float const Linear = Arch->LinearDrag / Object->get_mass();
        Vector Inertia;
        if (Object->BodyPtr != nullptr)
        {
            Inertia = PhySys::GetMomentOfInertia(Object);
        }
        else
        {
            Inertia.x = 0.0f;
            Inertia.y = 0.0f;
            Inertia.z = 0.0f;
        }
        Vector Angular;
        Angular.x = Arch->AngularDrag.x / Inertia.x;
        Angular.y = Arch->AngularDrag.y / Inertia.y;
        Angular.z = Arch->AngularDrag.z / Inertia.z;
        PhySys::SetDamping(Object, Linear, Angular);
    }

    // The nebula at get_position() into +0x38, its fog factor into +0x3c (FUN_005553b0).
    FNebulaEffect* const Nebula =
        StarSysFindNebula(&get_position(), &NebulaFogFactor, nullptr);
    CurrentNebula = Nebula;
}

FClientDebrisGameobj* CreateDebrisGameobjForObject(CObject* Object, FDebrisArch const* Arch)
{
    // FUN_00530620
    FClientDebrisGameobj* const Debris = new FClientDebrisGameobj();
    Debris->Object = Object;
    Debris->advise(true);
    Debris->Init(Arch);
    ClientObjDb_RegisterGameobj(Debris, false);
    return Debris;
}

FClientDebrisGameobj* CreateDebrisGameobjFromArch(Archetype::Root* Arch,
                                                  FDebrisArch const* DebrisArch,
                                                  Vector const* Pos)
{
    // FUN_005306e0
    FClientDebrisGameobj* const Debris = new FClientDebrisGameobj();
    Arch->load();
    Arch->Companion->Load(1);
    Debris->create(Arch);
    Arch->Release();

    // No null test on the narrowed object.
    CObject* const Simple = NarrowDebrisObject(Debris, 1u);
    Simple->WorldPosition = *Pos;

    // The current system (FUN_00554e90) is read last.
    PhySys::CreateParms Parms;
    std::memset(&Parms, 0, sizeof(Parms));
    Parms.Mass = 1.0f;
    Parms.LinearDamping = PhySys::DEFAULT_LINEAR_DAMPING;
    Parms.AngularDamping[0] = PhySys::DEFAULT_ANGULAR_DAMPING.x;
    Parms.AngularDamping[1] = PhySys::DEFAULT_ANGULAR_DAMPING.y;
    Parms.AngularDamping[2] = PhySys::DEFAULT_ANGULAR_DAMPING.z;
    Parms.SystemId = 0;
    Parms.Type = 2;
    Parms.bPhysicalUnmoveable = 0;
    Parms.bEnableCollisions = 1;
    Parms.bHasInertia = 0;
    Parms.Inertia.x = 0.0f;
    Parms.Inertia.y = 0.0f;
    Parms.Inertia.z = 0.0f;
    Parms.Name[0] = '\0';
    Parms.SystemId = StarSysGetActiveSystemId();
    PhySys::BuildIVP_Real(Simple, Parms);

    Debris->Init(DebrisArch);
    ClientObjDb_RegisterGameobj(Debris, false);
    return Debris;
}
