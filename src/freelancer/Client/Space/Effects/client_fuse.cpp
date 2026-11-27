// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// freelancer.exe fuse database and fuse-action classes -- retail .text:00529e10-0052c7f1.
// See client_fuse.h.

#include "Space/Effects/client_fuse.h"

#include "Space/Effects/cli_projectile_fx.h" // CliEffectListRemove (FUN_004fcef0), CliEffectCompanionClear
#include "Space/Effects/client_explosion.h" // Action_Explode::Create (FUN_005361e0)
#include "Space/client_gameobj.h"   // FClientGameobj, ExplosionArchAddRef / Release
#include "Space/client_objdb.h"     // ClientObjDb_GetKindHead (FUN_00540fc0)
#include "common_goods.h"     // FindHardpoint_OS / _PS / HardpointToWorld / HardpointInfo
#include "common_physys.h"    // PhySys::LinearImpulse
#include "Space/Effects/particle_effect.h"  // CliCreateEffectOneShot / CliCreateEffectWorldOneShot(On)
#include "Common/Graphics/vis_effect_arch.h"  // FindEffectArch / FEffectArch::AddRef / FEffectArch::Release
#include "common_ini.h"
#include "DALib.h"
#include "engine/engbase.h"   // IEngine::GetInstanceTransform (vt+0xa0)
#include "x86math.h"          // I3DMathEngine

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace
{
    // DAT_00675430 -- this file's 3DMathEngine, acquired on first use.
    I3DMathEngine* G_FuseMath = nullptr;

    I3DMathEngine* FuseMath()
    {
        if (G_FuseMath == nullptr)
            G_FuseMath = Acquire3DMathEngine();
        return G_FuseMath;
    }

    // .rdata:005c77f4 -- pi/180 as a float (0x3c8efa35).
    float const kDegToRad = 0.01745329238474369f;

    // .rdata:005dbc98 -- double 0.0001: ClientImpulseAction::Trigger's minimum radius / force
    // and its "on top of the point" distance.
    double const kImpulseMinimum = 0.0001;

    // DAT_00675420 -- StartEffectAction::Trigger's "ROOT" hardpoint name, StringAlloc'd (not
    // temporary) by the static initializer LAB_0052b4f0.
    char* G_FuseRootHardpoint = StringAlloc("ROOT", false);

    // One pass of the row rotation StartEffectAction::Slot08 runs for "ori_offset". The angle is
    // in double; its cosine is rounded to float, its sine stays double. For each column:
    // A = cos*A - sin*B, B = sin*A + cos*B (both from the old A and B).
    void RotateRowPair(float* RowA, float* RowB, float Degrees)
    {
        double const Angle = static_cast<double>(Degrees) * static_cast<double>(kDegToRad);
        float const Cos = static_cast<float>(std::cos(Angle));
        double const Sin = std::sin(Angle);
        for (int Column = 0; Column < 3; ++Column)
        {
            float const A = RowA[Column];
            float const B = RowB[Column];
            RowA[Column] = static_cast<float>(static_cast<double>(Cos) * static_cast<double>(A) -
                                              Sin * static_cast<double>(B));
            RowB[Column] = static_cast<float>(Sin * static_cast<double>(A) +
                                              static_cast<double>(Cos) * static_cast<double>(B));
        }
    }
}

// ---------------------------------------------------------------------------
// ClientFuse
// ---------------------------------------------------------------------------

ClientFuse::ClientFuse(ID_String const& Nickname, float Lifetime)
    : Fuse(Nickname, Lifetime)
    , Host(nullptr)
{
}

ClientFuse::ClientFuse(ClientFuse const& Other)
    : Fuse(Other)
    , Host(Other.Host)
{
}

ClientFuse::~ClientFuse()
{
    // FUN_0052aab0: Fuse::~Fuse, then operator delete when bit 0 of the flag is set.
}

bool ClientFuse::Read(INI_Reader& Reader)
{
    // FUN_00529e10.
    if (!ReadFuseValues(Reader))
        return false;
    if (!Reader.read_header())
        return true;

    for (;;)
    {
        ClientFuseAction* Action = nullptr;
        if (Reader.is_header("start_explosion"))
            Action = new StartExplosionAction();
        else if (Reader.is_header("start_cam_particles"))
            Action = new StartCameraParticlesAction();
        else if (Reader.is_header("start_effect"))
            Action = new StartEffectAction();
        else if (Reader.is_header("impulse"))
            Action = new ClientImpulseAction();
        else if (Reader.is_header("ignite_fuse"))
            Action = new ClientIgniteFuseAction();
        else if (Reader.is_header("fuse"))
            return true; // the next fuse starts here; the caller reads it

        if (Action != nullptr)
        {
            // A failed read returns false and leaks the action (retail).
            if (!Action->Read(Reader))
                return false;
            // Keyed by trigger time (FUN_0052aae0 multimap insert).
            Actions.insert(std::multimap<float, FuseAction*>::value_type(
                Action->GetTriggerTime(), Action));
            if (Action->ShouldRandomize())
                bNeedsRandomize = true;
        }

        if (!Reader.read_header())
            return true;
    }
}

void ClientFuse::BindHost(FClientGameobj* NewHost)
{
    // FUN_0052a4d0: +0x34 = host, then every action's +0x24 = host (walks the action tree).
    Host = NewHost;
    for (std::multimap<float, FuseAction*>::iterator It = Actions.begin(); It != Actions.end(); ++It)
        static_cast<ClientFuseAction*>(It->second)->Host = NewHost;
}

// ---------------------------------------------------------------------------
// ClientFuseAction
// ---------------------------------------------------------------------------

ClientFuseAction::ClientFuseAction()
    : Host(nullptr)
{
}

ClientFuseAction::~ClientFuseAction()
{
}

void ClientFuseAction::UnTriggerAndSettle(unsigned short SubObjId)
{
    // LAB_0052b430.
    std::vector<unsigned short>::iterator It =
        std::find(TriggeredSubObjs.begin(), TriggeredSubObjs.end(), SubObjId);
    if (It != TriggeredSubObjs.end())
        TriggeredSubObjs.erase(It);
    if (TriggeredSubObjs.empty())
        bTriggered = false;
}

// ---------------------------------------------------------------------------
// StartExplosionAction
// ---------------------------------------------------------------------------

StartExplosionAction::StartExplosionAction()
    : ExplosionArch(nullptr)
{
    Explosion.clear();
    Hardpoint.clear();
    PosOffset.x = 0.0f;
    PosOffset.y = 0.0f;
    PosOffset.z = 0.0f;
}

FuseAction* StartExplosionAction::Clone() const
{
    // FUN_0052a0f0: new 0x40 object, then its slot 12 copies *this.
    StartExplosionAction* Copy = new StartExplosionAction();
    Copy->CopyArchProperties(*this);
    return Copy;
}

bool StartExplosionAction::Read(INI_Reader& Reader)
{
    // FUN_0052b020.
    while (Reader.read_value())
    {
        if (ReadFuseActionValue(Reader))
            continue;
        if (Reader.is_value("explosion"))
        {
            Explosion.Id = CreateID(Reader.get_value_string());
        }
        else if (Reader.is_value("hardpoint"))
        {
            Hardpoint.Value = StringAlloc(Reader.get_value_string(), false);
        }
        else if (Reader.is_value("pos_offset"))
        {
            float const X = Reader.get_value_float(0);
            float const Y = Reader.get_value_float(1);
            float const Z = Reader.get_value_float(2);
            PosOffset.x = X;
            PosOffset.y = Y;
            PosOffset.z = Z;
        }
    }
    return true;
}

int StartExplosionAction::Load()
{
    // FUN_0052b290.
    ExplosionArch = Archetype::GetExplosion(Explosion);
    if (ExplosionArch != nullptr)
        ExplosionArchAddRef(ExplosionArch);
    return FuseAction::Load();
}

int StartExplosionAction::UnLoad()
{
    // FUN_0052b2c0: the pointer is left as it is after the release.
    if (ExplosionArch != nullptr)
        ExplosionArchRelease(ExplosionArch);
    return FuseAction::UnLoad();
}

void StartExplosionAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    // LAB_0052b150.
    if (!IsTriggered())
    {
        // The explosion is looked up again by name (not +0x3c); only one whose process (+0x14)
        // is 2 starts.
        Archetype::Explosion* const Arch = Archetype::GetExplosion(Explosion);
        if (Arch != nullptr && Arch->Process == Archetype::Explosion::ProcessNone)
        {
            long HpHandle = 0;
            HardpointInfo Info;
            if (FindHardpoint_OS(static_cast<long>(Host->get_index()), Hardpoint.Value, HpHandle,
                                 Info))
            {
                // Into world space on the handle it came back with.
                HardpointToWorld(HpHandle, Info);

                // pos_offset turned by the hardpoint's orientation, added to its position.
                Vector Turned;
                FuseMath()->TransformMatrix(Turned, Info.Orient,
                                            PosOffset);
                Vector Pos;
                Pos.x = Turned.x + Info.Position.x;
                Pos.y = Turned.y + Info.Position.y;
                Pos.z = Turned.z + Info.Position.z;

                Action_Explode* const Explode = Action_Explode::Create(Arch, Host, &Pos);
                if (Explode != nullptr)
                    Host->Actions.add(Explode);
            }
        }
    }

    // FuseAction::Trigger's body, inlined in retail.
    FuseAction::Trigger(SourceId, SubObjId);
}

void StartExplosionAction::UnTrigger(unsigned short SubObjId)
{
    UnTriggerAndSettle(SubObjId);
}

void StartExplosionAction::CopyArchProperties(StartExplosionAction const& Other)
{
    // FUN_0052b110. +0x3c (the loaded arch) is not copied.
    FuseAction::CopyArchProperties(Other);
    Explosion = Other.Explosion;
    Hardpoint = Other.Hardpoint;
    PosOffset = Other.PosOffset;
}

// ---------------------------------------------------------------------------
// StartCameraParticlesAction
// ---------------------------------------------------------------------------

StartCameraParticlesAction::StartCameraParticlesAction()
    : EffectArch(nullptr)
{
    Effect.clear();
}

FuseAction* StartCameraParticlesAction::Clone() const
{
    // FUN_0052a1b0.
    StartCameraParticlesAction* Copy = new StartCameraParticlesAction();
    Copy->CopyArchProperties(*this);
    return Copy;
}

bool StartCameraParticlesAction::Read(INI_Reader& Reader)
{
    // FUN_0052b2e0.
    while (Reader.read_value())
    {
        if (ReadFuseActionValue(Reader))
            continue;
        if (Reader.is_value("effect"))
            Effect.Id = CreateID(Reader.get_value_string());
    }
    return true;
}

int StartCameraParticlesAction::Load()
{
    // FUN_0052b4a0.
    EffectArch = FindEffectArch(&Effect.Id);
    if (EffectArch != nullptr)
        EffectArch->AddRef(0);
    return FuseAction::Load();
}

int StartCameraParticlesAction::UnLoad()
{
    // FUN_0052b4d0: the pointer is not cleared after the release.
    if (EffectArch != nullptr)
        EffectArch->Release();
    return FuseAction::UnLoad();
}

void StartCameraParticlesAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    // LAB_0052b370.
    if (!IsTriggered())
    {
        // get_orientation is folded with get_transform (the object's whole Transform); it is
        // called before the id is tested.
        float const Pos3[3] = {0.0f, 0.0f, 0.0f};
        Matrix const& HostOrient = Host->get_orientation();

        // A non-empty +0x28 is looked up again (not +0x2c) and started on DAT_00674c5c with no
        // companion out.
        if (!Effect.IsEmpty())
        {
            G_CliEffectFactoryCameraParticles.CreateEffectWorldOneShotOn(FindEffectArch(&Effect.Id),
                                                                         reinterpret_cast<Transform const*>(&HostOrient), Pos3,
                                                                         nullptr);
        }
    }

    // FuseAction::Trigger's body, inlined in retail.
    FuseAction::Trigger(SourceId, SubObjId);
}

void StartCameraParticlesAction::UnTrigger(unsigned short SubObjId)
{
    UnTriggerAndSettle(SubObjId);
}

void StartCameraParticlesAction::CopyArchProperties(StartCameraParticlesAction const& Other)
{
    // FUN_0052b350.
    FuseAction::CopyArchProperties(Other);
    Effect = Other.Effect;
}

// ---------------------------------------------------------------------------
// StartEffectAction
// ---------------------------------------------------------------------------

StartEffectAction::StartEffectAction()
    : bAttached(false)
    , EffectArch(nullptr)
{
    // The companion via FUN_005154a0(0,0,0,0).
    Effect.clear();
    Companion[0] = nullptr;
    Companion[1] = nullptr;
    Companion[2] = nullptr;
    Companion[3] = nullptr;
}

StartEffectAction::~StartEffectAction()
{
    // FUN_0052b510. Retail runs the FUN_00502d90 release twice -- the class body's, then the
    // companion member's destructor, which finds everything null. Then the hardpoint vector and
    // ~FuseAction.
    CliEffectCompanionClear(Companion);
    CliEffectCompanionClear(Companion);
}

FuseAction* StartEffectAction::Clone() const
{
    // FUN_0052a280.
    StartEffectAction* Copy = new StartEffectAction();
    Copy->CopyArchProperties(*this);
    return Copy;
}

bool StartEffectAction::Read(INI_Reader& Reader)
{
    // FUN_0052b5f0.
    while (Reader.read_value())
    {
        if (ReadFuseActionValue(Reader))
            continue;
        if (Reader.is_value("effect"))
        {
            Effect.Id = CreateID(Reader.get_value_string());
        }
        else if (Reader.is_value("attached"))
        {
            bAttached = Reader.get_value_bool(0);
        }
        else if (Reader.is_value("hardpoint"))
        {
            // {name, identity matrix, zero offset}, appended (FUN_0052c4d0).
            FStartEffectHardpoint Entry;
            Entry.Hardpoint.clear();
            Entry.Orientation = Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
            Entry.PosOffset.x = 0.0f;
            Entry.PosOffset.y = 0.0f;
            Entry.PosOffset.z = 0.0f;
            Entry.Hardpoint.Value = StringAlloc(Reader.get_value_string(), false);
            Hardpoints.insert(Hardpoints.end(), 1, Entry);
        }
        else if (Reader.is_value("pos_offset"))
        {
            if (Hardpoints.empty())
            {
                Reader.log_link("StartEffectAction::Read() pos_offset precedes any hardpoint.  Ignored.");
            }
            else
            {
                float const X = Reader.get_value_float(0);
                float const Y = Reader.get_value_float(1);
                float const Z = Reader.get_value_float(2);
                FStartEffectHardpoint& Last = Hardpoints.back();
                Last.PosOffset.x = X;
                Last.PosOffset.y = Y;
                Last.PosOffset.z = Z;
            }
        }
        else if (Reader.is_value("ori_offset"))
        {
            if (Hardpoints.empty())
            {
                Reader.log_link("StartEffectAction::Read() ori_offset precedes any hardpoint.  Ignored.");
            }
            else
            {
                // Value 2 builds a rotation about Y; value 0 then rotates rows 1/2, value 1
                // rows 0/1. The Y angle's cosine comes from the unrounded product, its sine from
                // the product rounded to float.
                float const RotRows12 = Reader.get_value_float(0);
                float const RotRows01 = Reader.get_value_float(1);
                double const AngleY =
                    static_cast<double>(Reader.get_value_float(2)) * static_cast<double>(kDegToRad);
                float const AngleYStored = static_cast<float>(AngleY);
                Matrix& M = Hardpoints.back().Orientation;
                double const CosY = std::cos(AngleY);
                M.E[0][1] = 0.0f;
                M.E[1][1] = 1.0f;
                M.E[1][2] = 0.0f;
                M.E[2][1] = 0.0f;
                M.E[1][0] = 0.0f;
                double const SinY = std::sin(static_cast<double>(AngleYStored));
                M.E[0][0] = static_cast<float>(CosY);
                M.E[0][2] = static_cast<float>(SinY);
                M.E[2][0] = static_cast<float>(-SinY);
                M.E[2][2] = static_cast<float>(CosY);
                RotateRowPair(M.E[1], M.E[2], RotRows12);
                RotateRowPair(M.E[0], M.E[1], RotRows01);
            }
        }
    }
    return true;
}

int StartEffectAction::Load()
{
    // FUN_0052bf40.
    EffectArch = FindEffectArch(&Effect.Id);
    if (EffectArch != nullptr)
        EffectArch->AddRef(0);
    return FuseAction::Load();
}

int StartEffectAction::UnLoad()
{
    // FUN_0052bf70: the pointer is not cleared after the release.
    if (EffectArch != nullptr)
        EffectArch->Release();
    return FuseAction::UnLoad();
}

void StartEffectAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    // LAB_0052bb00.
    if (!IsTriggered())
    {
        long EngineIndex = -1;
        Transform Local;
        Local.Rotation =
            Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
        Local.Translation.x = 0.0f;
        Local.Translation.y = 0.0f;
        Local.Translation.z = 0.0f;
        bool bFound = false;

        // No hardpoints -> the host's root.
        int const Count = static_cast<int>(Hardpoints.size());
        if (Count == 0)
        {
            EngineIndex = Host->get_index();
            bFound = true;
        }
        else
        {
            // A random start, rand() * count / 32768 (signed division rounding toward zero).
            int const Start = (std::rand() * Count) / 32768;
            int Index = Start;
            for (;;)
            {
                FStartEffectHardpoint const& Entry = Hardpoints[Index];
                if (_stricmp(Entry.Hardpoint.Value, G_FuseRootHardpoint) == 0) // DACOM stricmp
                {
                    // "ROOT": the host's own index, the entry's frame as the local transform.
                    EngineIndex = Host->get_index();
                    Local.Rotation = Entry.Orientation;
                    Local.Translation = Entry.PosOffset;
                    bFound = true;
                }
                else
                {
                    // The handle FindHardpoint_OS returns becomes the engine index.
                    HardpointInfo Info;
                    if (FindHardpoint_OS(Host->get_index(), Entry.Hardpoint.Value, EngineIndex,
                                         Info))
                    {
                        // The offset turned by the hardpoint's orientation, plus its position.
                        Vector Turned;
                        FuseMath()->TransformMatrix(Turned,
                                                    Info.Orient,
                                                    Entry.PosOffset);
                        Local.Translation.x = Turned.x + Info.Position.x;
                        Local.Translation.y = Turned.y + Info.Position.y;
                        Local.Translation.z = Turned.z + Info.Position.z;
                        // Rotation = hardpoint orientation * entry orientation.
                        Matrix Rotation;
                        FuseMath()->MulMatrix(Rotation,
                                              Info.Orient,
                                              Entry.Orientation);
                        Local.Rotation = Rotation;
                        bFound = true;
                    }
                }

                // Back at the start with nothing found goes straight to the tail; otherwise a
                // found entry ends the search.
                ++Index;
                if (Index == Count)
                    Index = 0;
                if (Index == Start)
                {
                    if (!bFound)
                    {
                        FuseAction::Trigger(SourceId, SubObjId);
                        return;
                    }
                    break;
                }
                if (bFound)
                    break;
            }
        }

        if (bAttached && EngineIndex != -1)
        {
            // Attached: a non-empty +0x28 is looked up again (not +0x40) and started on the
            // host's effect list with the companion at +0x44; the result is unused.
            if (!Effect.IsEmpty())
            {
                CliCreateEffectOneShot(FindEffectArch(&Effect.Id), Host,
                                       static_cast<int>(EngineIndex), &Local, Companion);
            }
        }
        else if (bFound)
        {
            const float* const InstanceRaw = (const float*)DALib::Engine->GetInstanceTransform(
                EngineIndex);
            Transform World;
            FuseMath()->MulTransform(World, *reinterpret_cast<Transform const*>(InstanceRaw),
                                     Local);
            // Taken before the id test.
            Vector const Velocity = Host->get_velocity();
            // A world one-shot moving with the host, no companion out.
            if (!Effect.IsEmpty())
            {
                CliCreateEffectWorldOneShot(FindEffectArch(&Effect.Id), &World, &Velocity.x,
                                            nullptr);
            }
        }
    }

    // FuseAction::Trigger's body, inlined in retail.
    FuseAction::Trigger(SourceId, SubObjId);
}

void StartEffectAction::UnTrigger(unsigned short SubObjId)
{
    // FUN_0052be90.
    std::vector<unsigned short>::iterator It =
        std::find(TriggeredSubObjs.begin(), TriggeredSubObjs.end(), SubObjId);
    if (It != TriggeredSubObjs.end())
        TriggeredSubObjs.erase(It);
    // Only once the list is empty.
    if (!TriggeredSubObjs.empty())
        return;
    // The companion comes off the host's effect list; when it was not on it, it is released
    // right here (the FUN_00502d90 sequence inlined).
    if (!CliEffectListRemove(&Host->EffectList, Companion))
        CliEffectCompanionClear(Companion);
    bTriggered = false;
}

void StartEffectAction::CopyArchProperties(StartEffectAction const& Other)
{
    // FUN_0052b8c0. The vector copy is skipped for self-assignment; elements are copied bitwise
    // (FUN_0052c7d0).
    FuseAction::CopyArchProperties(Other);
    Effect = Other.Effect;
    bAttached = Other.bAttached;
    if (&Hardpoints != &Other.Hardpoints)
        Hardpoints = Other.Hardpoints;
}

// ---------------------------------------------------------------------------
// ClientImpulseAction
// ---------------------------------------------------------------------------

ClientImpulseAction::ClientImpulseAction()
    : Force(0.0f)
    , Radius(0.0f)
{
    Hardpoint.clear();
    PosOffset.x = 0.0f;
    PosOffset.y = 0.0f;
    PosOffset.z = 0.0f;
}

FuseAction* ClientImpulseAction::Clone() const
{
    // FUN_0052a360.
    ClientImpulseAction* Copy = new ClientImpulseAction();
    Copy->CopyArchProperties(*this);
    return Copy;
}

bool ClientImpulseAction::Read(INI_Reader& Reader)
{
    // FUN_0052bf90.
    while (Reader.read_value())
    {
        if (ReadFuseActionValue(Reader))
            continue;
        if (Reader.is_value("hardpoint"))
        {
            Hardpoint.Value = StringAlloc(Reader.get_value_string(), false);
        }
        else if (Reader.is_value("force"))
        {
            Force = Reader.get_value_float(0);
        }
        else if (Reader.is_value("radius"))
        {
            Radius = Reader.get_value_float(0);
        }
        else if (Reader.is_value("pos_offset"))
        {
            // Values read in the order 2, 1, 0.
            float const Z = Reader.get_value_float(2);
            float const Y = Reader.get_value_float(1);
            float const X = Reader.get_value_float(0);
            PosOffset.x = X;
            PosOffset.y = Y;
            PosOffset.z = Z;
        }
    }
    return true;
}

void ClientImpulseAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    // LAB_0052c0c0.
    if (!IsTriggered())
    {
        // FindHardpoint_PS here, not _OS.
        long HpHandle = -1;
        HardpointInfo Info;
        if (FindHardpoint_PS(Host->get_index(), Hardpoint.Value, HpHandle, Info))
        {
            // Into world space on the host's index (not the handle).
            HardpointToWorld(Host->get_index(), Info);

            // pos_offset through the TRANSPOSED hardpoint orientation, plus its position.
            Vector Turned;
            FuseMath()->TransposeTransform(Turned, Info.Orient,
                                           PosOffset);
            Vector Pos;
            Pos.x = Turned.x + Info.Position.x;
            Pos.y = Turned.y + Info.Position.y;
            Pos.z = Turned.z + Info.Position.z;

            // Radius and force must both be strictly above 1e-4 (NaN fails).
            if (static_cast<double>(Radius) > kImpulseMinimum &&
                static_cast<double>(Force) > kImpulseMinimum)
            {
                // The ObjDB kind-10 bucket.
                int Kind = static_cast<int>(EClientGameobjKind::Debris);
                FClientGameobj* Go = ClientObjDb_GetKindHead(&Kind);
                while (Go != nullptr)
                {
                    // Null unless its class has bit 0 set -- and read without a null test
                    // (retail).
                    CObject* Obj = Go->Object;
                    if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & CObject::ClassObject) != CObject::ClassObject)
                        Obj = nullptr;

                    // The offset from the impulse point in floats; its length summed in double,
                    // rounded to float.
                    float const Dz = Obj->WorldPosition.z - Pos.z;
                    float const Dy = Obj->WorldPosition.y - Pos.y;
                    float const Dx = Obj->WorldPosition.x - Pos.x;
                    Vector Impulse;
                    Impulse.x = Dx;
                    Impulse.y = Dy;
                    Impulse.z = Dz;
                    float const Distance = static_cast<float>(
                        std::sqrt(static_cast<double>(Dx) * static_cast<double>(Dx) +
                                  static_cast<double>(Dy) * static_cast<double>(Dy) +
                                  static_cast<double>(Dz) * static_cast<double>(Dz)));

                    if (Force > 0.0f && Distance < Radius)
                    {
                        // A distance below 1e-4 pushes straight up by the force.
                        if (static_cast<double>(Distance) < kImpulseMinimum)
                        {
                            Impulse.x = 0.0f;
                            Impulse.y = Force;
                            Impulse.z = 0.0f;
                        }
                        else
                        {
                            // (1 - d / radius) * force / d, kept double.
                            double const Scale =
                                (1.0 - static_cast<double>(Distance) / static_cast<double>(Radius)) *
                                static_cast<double>(Force) / static_cast<double>(Distance);
                            Impulse.x = static_cast<float>(static_cast<double>(Dx) * Scale);
                            Impulse.y = static_cast<float>(static_cast<double>(Dy) * Scale);
                            Impulse.z = static_cast<float>(static_cast<double>(Dz) * Scale);
                        }
                        // Only an object with a physics body (+0x54).
                        if (Obj->BodyPtr != nullptr)
                            PhySys::LinearImpulse(Obj, Impulse, 1.0f);
                    }

                    Go = Go->ListNext;
                }
            }
        }
    }

    // FuseAction::Trigger's body, inlined in retail.
    FuseAction::Trigger(SourceId, SubObjId);
}

void ClientImpulseAction::CopyArchProperties(ClientImpulseAction const& Other)
{
    // FUN_0052c080. The hardpoint at +0x28 is NOT copied, so a clone keeps the cleared
    // CacheString of its constructor (retail).
    FuseAction::CopyArchProperties(Other);
    Force = Other.Force;
    Radius = Other.Radius;
    PosOffset = Other.PosOffset;
}

// ---------------------------------------------------------------------------
// ClientIgniteFuseAction
// ---------------------------------------------------------------------------

ClientIgniteFuseAction::ClientIgniteFuseAction()
    : bFuseLoaded(false)
{
    FuseId.clear();
}

FuseAction* ClientIgniteFuseAction::Clone() const
{
    // FUN_0052a430.
    ClientIgniteFuseAction* Copy = new ClientIgniteFuseAction();
    Copy->CopyArchProperties(*this);
    return Copy;
}

bool ClientIgniteFuseAction::Read(INI_Reader& Reader)
{
    // FUN_0052c340.
    while (Reader.read_value())
    {
        if (ReadFuseActionValue(Reader))
            continue;
        if (Reader.is_value("fuse"))
            FuseId.Id = CreateID(Reader.get_value_string());
    }
    return true;
}

int ClientIgniteFuseAction::Load()
{
    // LAB_0052c3b0: first time only, Fuse::Load on the target fuse in the exe database; always
    // FuseAction::Load, whose result is returned.
    if (!bFuseLoaded)
    {
        bFuseLoaded = true;
        ClientFuse* Target = ClientFuseDB::Find(FuseId.Id);
        if (Target != nullptr)
            Target->Load();
    }
    return FuseAction::Load();
}

int ClientIgniteFuseAction::UnLoad()
{
    // LAB_0052c420: mirror of Load.
    if (bFuseLoaded)
    {
        bFuseLoaded = false;
        ClientFuse* Target = ClientFuseDB::Find(FuseId.Id);
        if (Target != nullptr)
            Target->UnLoad();
    }
    return FuseAction::UnLoad();
}

void ClientIgniteFuseAction::CopyArchProperties(ClientIgniteFuseAction const& Other)
{
    // FUN_0052b350 (the same body as StartCameraParticlesAction's). bFuseLoaded is not copied.
    FuseAction::CopyArchProperties(Other);
    FuseId = Other.FuseId;
}

// ---------------------------------------------------------------------------
// ClientFuseDB
// ---------------------------------------------------------------------------

// DAT_00675400. Its dynamic initializer (LAB_0052a510 -> FUN_0052a610 tree ctor, atexit
// LAB_0052a530 tree dtor) is the std::map's own construction/destruction.
std::map<unsigned, ClientFuse*> ClientFuseDB::Fuses;

std::map<unsigned, ClientFuse*>& ClientFuseDB::Get()
{
    // FUN_0052a680.
    return Fuses;
}

ClientFuse* ClientFuseDB::Find(unsigned FuseId)
{
    std::map<unsigned, ClientFuse*>::iterator It = Fuses.find(FuseId);
    if (It == Fuses.end())
        return nullptr;
    return It->second;
}

void ClientFuseDB::Clear()
{
    // FUN_0052a690: virtual-delete every non-null prototype, then erase all nodes.
    for (std::map<unsigned, ClientFuse*>::iterator It = Fuses.begin(); It != Fuses.end(); ++It)
    {
        if (It->second != nullptr)
            delete It->second;
    }
    Fuses.clear();
}

void ClientFuseDB::LoadAll()
{
    // FUN_0052a7a0: one FUN_0052a7d0 per path in FuseDB::m_FuseINIFiles (node+0xc = the
    // TString's characters).
    for (std::list<TString<260>>::iterator It = m_FuseINIFiles.begin(); It != m_FuseINIFiles.end(); ++It)
        LoadFile(It->Data);
}

void ClientFuseDB::LoadFile(char const* Path)
{
    // FUN_0052a7d0.
    INI_Reader Reader;
    if (!Reader.open(Path, false))
        return;
    Reader.read_header(); // result unused
    while (!Reader.is_end())
    {
        if (Reader.is_header("fuse"))
        {
            ClientFuse* NewFuse = new ClientFuse(ID_String{CreateID("")}, -1.0f);
            // A failed Read, or a nickname already in the map, leaks NewFuse (retail).
            if (NewFuse->Read(Reader))
                Fuses.insert(std::map<unsigned, ClientFuse*>::value_type(NewFuse->Name().Id, NewFuse));
        }
        else
        {
            Reader.read_header();
        }
    }
}
