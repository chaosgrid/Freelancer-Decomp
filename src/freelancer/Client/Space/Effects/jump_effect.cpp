// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Space/Effects/jump_effect.h"

#include "Space/Effects/cli_projectile_fx.h" // CliEffectCompanionArmStop / Clear, CliEffectListRemove
#include "Space/client_gameobj.h"
#include "common_cobject.h"    // CObject / CSolar
#include "common_goods.h"      // FindHardpoint_PS / HardpointToWorld / HardpointInfo
#include "common_ini.h"        // INI_Reader, StringAlloc
#include "common_misc.h"       // GetFileSysPath
#include "Space/Effects/gate_tunnel.h"
#include "Core/globals.h"           // g_display (DAT_00679bc0), [Data] jump_effect at +0x770
#include "Space/Effects/particle_effect.h"   // the effect factories, FUN_00503f40 / FUN_00503070 / FUN_004f7d20
#include "Space/player_behavior.h"   // GetPlayerObjRW (FUN_0054baf0)
#include "Common/Graphics/vis_effect_arch.h"   // FindEffectArch (FUN_0040f080)

#include "dacom.h" // FDUMP
#include "soundmanager.h"      // ISoundSource::SetPose

#include <list>
#include <new>

namespace
{
    // .rdata 0x005da80c.
    char const* const kJumpEffectCpp = "E:\\FL\\Scratch\\Source\\Client\\Space\\JumpEffect.cpp";

    // DAT_00674fd0 -- the loaded (or lazily defaulted) JumpShipEffectDesc. Freed by
    // FUN_00502aa0 (JumpEffectDescsClear) at space teardown.
    JumpShipEffectDesc* G_JumpShipEffectDesc = nullptr;

    // DAT_00674fd8 (sentinel) / DAT_00674fdc (count) -- the [JumpGateEffect] records, appended
    // by JumpEffectIniLoad (FUN_005020a0) and emptied by JumpEffectDescsClear (FUN_00502aa0).
    std::list<FJumpEffectDesc> G_JumpEffectDescs;

    // DAT_005d1f04 -- the jump camera-shake amplitude.
    constexpr float kJumpShakeAmplitude = 0.015f;

    // FUN_004215b0 on a Transform's rotation, then a zero translation (inlined in retail).
    void SetIdentityTransform(Transform& Xform)
    {
        Xform.Rotation.E[0][0] = 1.0f;
        Xform.Rotation.E[0][1] = 0.0f;
        Xform.Rotation.E[0][2] = 0.0f;
        Xform.Rotation.E[1][0] = 0.0f;
        Xform.Rotation.E[1][1] = 1.0f;
        Xform.Rotation.E[1][2] = 0.0f;
        Xform.Rotation.E[2][0] = 0.0f;
        Xform.Rotation.E[2][1] = 0.0f;
        Xform.Rotation.E[2][2] = 1.0f;
        Xform.Translation.x = 0.0f;
        Xform.Translation.y = 0.0f;
        Xform.Translation.z = 0.0f;
    }

    // The inline CSolar fetch: the object when its class has all of 0x303, else null.
    CSolar const* AsSolar(CObject const* Obj)
    {
        if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & CObject::ClassSolar) != CObject::ClassSolar)
            return nullptr;
        return static_cast<CSolar const*>(Obj);
    }
}

FJumpFxCompanion::~FJumpFxCompanion()
{
    // FUN_00502d90.
    CliEffectCompanionClear(this);
}

FJumpGateRingFx::FJumpGateRingFx()
{
    // LAB_00502de0.
    bCreated = 0;
    CreateTime = 0.0f;
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
}

FJumpGateEffect::FJumpGateEffect(FClientSolarGameobj* InSolar)
{
    // FUN_00502ba0. A null solar is reported but used.
    Solar = InSolar;
    if (InSolar == nullptr)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", kJumpEffectCpp, 0xe3,
              "null solar passed to JumpGateEffect constructor");
    }

    // The solar's +0x200 id, or "jump_effect_default" when it is empty.
    Desc = nullptr;
    ID_String Wanted = Solar->JumpEffectId;
    if (Wanted.IsEmpty())
        Wanted.Id = CreateID("jump_effect_default");
    for (std::list<FJumpEffectDesc>::iterator It = G_JumpEffectDescs.begin();
         It != G_JumpEffectDescs.end(); ++It)
    {
        if (Wanted.Id == It->Nickname.Id)
        {
            Desc = &*It;
            break;
        }
    }
    if (Desc == nullptr)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Missing jump effect: %s\n", kJumpEffectCpp, 0xf8,
              Wanted.get_string());
    }

    Clock = 0.0f;
    bFire = 0;

    // The JumpOutHp lookup's result is not tested, the position is copied either way. The
    // archetype read has no test (a non-solar crashes).
    CSolar const* const SolarObj = AsSolar(Solar != nullptr ? Solar->Object : nullptr);
    Archetype::Solar const* const Arch = static_cast<Archetype::Solar const*>(SolarObj->Archetype);
    HardpointInfo Info;
    long HpHandle = 0;
    FindHardpoint_PS(Solar->get_index(), Arch->JumpOutHp.Value, HpHandle, Info);
    JumpOutPosition = Info.Position;

    // Each descriptor ring (count re-read every lap, the descriptor used with no test).
    for (int Ring = 0; Ring < Desc->RingCount; ++Ring)
    {
        FindHardpoint_PS(Solar->get_index(), Desc->Rings[Ring].Hp.Value, HpHandle, Info);
        Rings[Ring].Position = Info.Position;
        Rings[Ring].CreateTime = Desc->Rings[Ring].CreateTime;
    }
}

FJumpGateEffect::~FJumpGateEffect()
{
    // FUN_00502e40. The descriptor is used with no test; the rings and companions follow as
    // member destruction.
    if (Solar != nullptr)
    {
        for (int Ring = 0; Ring < Desc->RingCount; ++Ring)
            CliEffectListRemove(&Solar->EffectList, &Rings[Ring].Fx);
    }
}

void FJumpGateEffect::Update(float Dt)
{
    // FUN_00502f00. The clock stops once fired.
    if (bFire == 0)
        Clock = Clock + Dt;

    // The count is re-read after each lap and the descriptor used with no test.
    for (int Ring = 0; Ring < Desc->RingCount; ++Ring)
    {
        FJumpGateRingFx& RingFx = Rings[Ring];
        // Once only, and only when the clock is above the ring's create time (NaN skips).
        if (RingFx.bCreated != 0)
            continue;
        if (!(Clock > RingFx.CreateTime))
            continue;
        RingFx.bCreated = 1;

        // Identity orientation at the ring's hardpoint position, on the solar's own instance.
        Transform Local;
        for (int Row = 0; Row < 3; ++Row)
        {
            for (int Col = 0; Col < 3; ++Col)
                Local.Rotation.E[Row][Col] = (Row == Col) ? 1.0f : 0.0f;
        }
        Local.Translation.x = RingFx.Position.x;
        Local.Translation.y = RingFx.Position.y;
        Local.Translation.z = RingFx.Position.z;
        int const Index = static_cast<int>(Solar->get_index());

        // The solar is the list owner.
        ID_String const& Effect = Desc->Rings[Ring].Effect;
        if (!Effect.IsEmpty() &&
            CliCreateEffectOneShot(FindEffectArch(&Effect.Id), Solar, Index, &Local,
                                   &RingFx.Fx) != 0)
            continue;

        // An empty name or a failed start warns.
        FDUMP(kFdumpWarning,
              "%s(%d) : *** WARNING: jump gate effect number (%d) named (%s) did not create.",
              kJumpEffectCpp, 0x12c, Ring, Effect.get_string());
    }
}

void FJumpGateEffect::Fire()
{
    // FUN_00503380.
    bFire = 1;
}

JumpShipEffectDesc* JumpShipEffectDescGet()
{
    // FUN_00502b00: DAT_00674fd0 when set; otherwise a warning and a default (FUN_00503a50
    // inlined); a failed new stores and returns null.
    if (G_JumpShipEffectDesc == nullptr)
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kJumpEffectCpp, 0xd5,
              "no JumpShipEffectDesc was loaded, creating a default");
        G_JumpShipEffectDesc = new (std::nothrow) JumpShipEffectDesc();
    }
    return G_JumpShipEffectDesc;
}

void JumpEffectIniLoad()
{
    // FUN_005020a0. The [Data] jump_effect path (DAT_0067a334).
    INI_Reader Reader;
    char Path[MAX_PATH];
    GetFileSysPath(Path, g_display.jump_effect.Text, nullptr);
    bool const bOpened = Reader.open(Path, false);

    // The previous JumpShipEffectDesc is freed whether or not the file opened.
    if (G_JumpShipEffectDesc != nullptr)
        delete G_JumpShipEffectDesc;
    G_JumpShipEffectDesc = nullptr;

    // The three per-ring key counts are zeroed ONCE, and the record lives for the whole
    // function: a later [JumpGateEffect] section that omits a key inherits the previous
    // section's value, as in retail. Only the ids are re-cleared per section. Retail leaves the
    // first section's unset fields as stack garbage; the port starts them at zero.
    int EffectCount = 0;
    int HpCount = 0;
    int TimeCount = 0;
    FJumpEffectDesc Record = FJumpEffectDesc();

    if (!bOpened)
        return;
    while (Reader.read_header())
    {
        if (Reader.is_header("JumpGateEffect"))
        {
            Record.Nickname.clear();
            for (int Ring = 0; Ring < 5; ++Ring)
            {
                // FUN_00502a80.
                Record.Rings[Ring].Effect.clear();
                Record.Rings[Ring].Hp.clear();
            }
            Record.JumpTunnelEffect.clear();
            Record.JumpTunnel.clear();

            while (Reader.read_value())
            {
                if (Reader.is_value("nickname"))
                {
                    Record.Nickname.Id = CreateID(Reader.get_value_string());
                }
                else if (Reader.is_value("glow_ring_effect"))
                {
                    // Every non-empty value i of the five sets the count to i + 1.
                    for (int Index = 0; Index < 5; ++Index)
                    {
                        if (!Reader.is_value_empty(static_cast<unsigned>(Index)))
                        {
                            EffectCount = Index + 1;
                            Record.Rings[Index].Effect.Id =
                                CreateID(Reader.get_value_string(static_cast<unsigned>(Index)));
                        }
                    }
                }
                else if (Reader.is_value("glow_ring_hp"))
                {
                    for (int Index = 0; Index < 5; ++Index)
                    {
                        if (!Reader.is_value_empty(static_cast<unsigned>(Index)))
                        {
                            HpCount = Index + 1;
                            Record.Rings[Index].Hp.Value = StringAlloc(
                                Reader.get_value_string(static_cast<unsigned>(Index)), false);
                        }
                    }
                }
                else if (Reader.is_value("glow_create_time"))
                {
                    for (int Index = 0; Index < 5; ++Index)
                    {
                        if (!Reader.is_value_empty(static_cast<unsigned>(Index)))
                        {
                            TimeCount = Index + 1;
                            Record.Rings[Index].CreateTime =
                                Reader.get_value_float(static_cast<unsigned>(Index));
                        }
                    }
                }
                else if (Reader.is_value("jump_out_time"))
                {
                    Record.JumpOutTime = Reader.get_value_float(0);
                }
                else if (Reader.is_value("jump_out_tunnel_time"))
                {
                    Record.JumpOutTunnelTime = Reader.get_value_float(0);
                }
                else if (Reader.is_value("jump_in_tunnel_time"))
                {
                    Record.JumpInTunnelTime = Reader.get_value_float(0);
                }
                else if (Reader.is_value("jump_in_time"))
                {
                    Record.JumpInTime = Reader.get_value_float(0);
                }
                else if (Reader.is_value("kill_time_before_done"))
                {
                    Record.KillTimeBeforeDone = Reader.get_value_float(0);
                }
                else if (Reader.is_value("jump_tunnel_effect"))
                {
                    Record.JumpTunnelEffect.Id = CreateID(Reader.get_value_string());
                }
                else if (Reader.is_value("jump_tunnel"))
                {
                    Record.JumpTunnel.Id = CreateID(Reader.get_value_string());
                }
                else if (Reader.is_value("jump_ambient"))
                {
                    // Each float truncated, low byte stored.
                    for (int Index = 0; Index < 3; ++Index)
                    {
                        Record.JumpAmbient[Index] = static_cast<unsigned char>(
                            static_cast<int>(Reader.get_value_float(static_cast<unsigned>(Index))));
                    }
                }
                else if (Reader.is_value("jump_background_color"))
                {
                    for (int Index = 0; Index < 3; ++Index)
                    {
                        Record.JumpBackgroundColor[Index] = static_cast<unsigned char>(
                            static_cast<int>(Reader.get_value_float(static_cast<unsigned>(Index))));
                    }
                }
                else
                {
                    FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kJumpEffectCpp, 0x84,
                          "UNKNOWN jumpeffect parameter");
                }
            }

            // The ring count is the smallest of the three key counts (signed).
            int Count = (TimeCount < HpCount) ? TimeCount : HpCount;
            if (EffectCount < Count)
                Count = EffectCount;
            Record.RingCount = Count;

            // A plain memberwise copy (FUN_005048a0): the CacheString pointers are shared, not
            // duplicated.
            G_JumpEffectDescs.push_back(Record);
        }
        else if (Reader.is_header("JumpShipEffect"))
        {
            if (G_JumpShipEffectDesc != nullptr)
            {
                delete G_JumpShipEffectDesc;
                G_JumpShipEffectDesc = nullptr;
            }
            G_JumpShipEffectDesc = new (std::nothrow) JumpShipEffectDesc();

            // Each key stores with no test of the descriptor; the id at +0 is never written here.
            while (Reader.read_value())
            {
                if (Reader.is_value("jump_out_effect"))
                {
                    G_JumpShipEffectDesc->Ids[1].Id = CreateID(Reader.get_value_string());
                }
                else if (Reader.is_value("jump_in_effect"))
                {
                    G_JumpShipEffectDesc->Ids[2].Id = CreateID(Reader.get_value_string());
                }
                else if (Reader.is_value("jump_done_effect_player"))
                {
                    G_JumpShipEffectDesc->Ids[3].Id = CreateID(Reader.get_value_string());
                }
                else if (Reader.is_value("jump_done_effect_nonplayer"))
                {
                    G_JumpShipEffectDesc->Ids[4].Id = CreateID(Reader.get_value_string());
                }
                else
                {
                    FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kJumpEffectCpp, 0xb1,
                          "UNKNOWN jump ship effect parameter");
                }
            }
        }
    }
}

void JumpEffectDescsClear()
{
    // FUN_00502aa0. No record destructor runs: the glow_ring_hp strings are not freed.
    G_JumpEffectDescs.clear();
    if (G_JumpShipEffectDesc != nullptr)
    {
        delete G_JumpShipEffectDesc;
        G_JumpShipEffectDesc = nullptr;
    }
}

FJumpShipEffect::FJumpShipEffect(FClientShipGameobj* InShip)
{
    // FUN_005038c0. The four companions are zeroed by their constructors.
    TunnelEffect.clear();

    SetIdentityTransform(GateHardpointFrame);

    // FUN_00502b00 inlined.
    Desc = JumpShipEffectDescGet();
    if (Desc == nullptr)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", kJumpEffectCpp, 0x1d5,
              "no JumpShipEffect descriptor found");
    }

    JumpOutTunnelTime = 5.0f;
    JumpInTunnelTime = 5.0f;
    KillTimeBeforeDone = 0.5f;
    JumpInTime = 0.5f;
    Tunnel = nullptr;
    Field_0x6c = 0;
    bTunnelEffectKilled = 0;
    Clock = 0.0f;
    bSkipFirstJumpInTick = 0;
    JumpOutTime = 0.0f;
    Ambient = nullptr;
    Color = nullptr;
    Ship = InShip;
}

FJumpShipEffect::FJumpShipEffect(FClientShipGameobj* InShip, FClientSolarGameobj* GateSolar)
{
    // FUN_00503440.
    TunnelEffect.clear();
    SetIdentityTransform(GateHardpointFrame);

    // A null gate solar or ship returns at once, everything else as the member constructors
    // left it.
    if (GateSolar == nullptr || InShip == nullptr)
        return;

    Desc = JumpShipEffectDescGet();
    if (Desc == nullptr)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", kJumpEffectCpp, 0x1d5,
              "no JumpShipEffect descriptor found");
    }

    Ship = InShip;
    Tunnel = nullptr;
    Field_0x6c = 0;
    bTunnelEffectKilled = 0;
    Clock = 0.0f;
    bSkipFirstJumpInTick = 0;
    JumpOutTime = 0.0f;
    JumpOutTunnelTime = 5.0f;
    JumpInTunnelTime = 5.0f;
    JumpInTime = 0.0f;
    KillTimeBeforeDone = 0.5f;
    Ambient = nullptr;
    Color = nullptr;

    // The solar's +0x204 JumpGateEffect (FUN_00548890); without one a temporary is built, and a
    // failed new skips to the hardpoint.
    FJumpGateEffect* Gate = static_cast<FJumpGateEffect*>(GateSolar->JumpGateEffect);
    FJumpGateEffect* Temp = nullptr;
    FJumpEffectDesc* GateDesc = nullptr;
    bool bHaveGate = true;
    if (Gate == nullptr)
    {
        Gate = new (std::nothrow) FJumpGateEffect(GateSolar);
        Temp = Gate;
        bHaveGate = Gate != nullptr;
    }

    if (bHaveGate)
    {
        // With the gate's descriptor, copy its ship parameters (JumpOutTime 0 when the solar's
        // type has bit 0x800).
        unsigned Type = 0;
        GateSolar->get_type(Type);
        bool const bNoJumpOut = ((Type >> 11) & 1u) != 0;
        GateDesc = Gate->Desc;
        if (GateDesc != nullptr)
        {
            JumpOutTime = bNoJumpOut ? 0.0f : GateDesc->JumpOutTime;
            JumpOutTunnelTime = GateDesc->JumpOutTunnelTime;
            JumpInTunnelTime = GateDesc->JumpInTunnelTime;
            JumpInTime = GateDesc->JumpInTime;
            KillTimeBeforeDone = GateDesc->KillTimeBeforeDone;
            TunnelEffect = GateDesc->JumpTunnelEffect;
            Ambient = GateDesc->JumpAmbient;
            Color = GateDesc->JumpBackgroundColor;
        }
        else
        {
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", kJumpEffectCpp, 0x181,
                  "no jump descriptor in JumpGateEffect");
        }
    }

    // The gate solar's CSolar is used with no test; its archetype's JumpOutHp is looked up on
    // its engine instance.
    CSolar const* const SolarObj = AsSolar(GateSolar->cobject());
    long const Instance = SolarObj->EngineInstanceId;
    Archetype::Solar const* const Arch = static_cast<Archetype::Solar const*>(SolarObj->Archetype);
    SetIdentityTransform(GateHardpointFrame);
    HardpointInfo Info;
    long HpHandle = 0;
    if (FindHardpoint_PS(Instance, Arch->JumpOutHp.Value, HpHandle, Info))
    {
        HardpointToWorld(Instance, Info);
        GateHardpointFrame.Rotation = Info.Orient;
        GateHardpointFrame.Translation = Info.Position;
    }
    else
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Hardpoint (%s) not found on jumpgate",
              kJumpEffectCpp, 0x196, Arch->JumpOutHp.Value);
        SetIdentityTransform(GateHardpointFrame);
    }

    // For the player's ship, the gate descriptor's jump_tunnel. Retail quirk kept: the
    // not-found error passes the raw id dword where the format wants a string.
    if (static_cast<void*>(static_cast<IObjRW*>(Ship)) == GetPlayerObjRW())
    {
        FGateTunnel* Found = nullptr;
        if (GateDesc != nullptr)
        {
            Found = GateTunnelFind(&GateDesc->JumpTunnel);
            if (Found == nullptr)
            {
                FDUMP(kFdumpError, "%s(%d) : *** ERROR: gate tunnel '%s' not found.",
                      kJumpEffectCpp, 0x1a5, GateDesc->JumpTunnel.Id);
            }
        }
        Tunnel = Found;
        if (Found != nullptr)
        {
            Found->SetShip(Ship);
            Found->Frame = GateHardpointFrame;
        }
        DestSystem = SolarObj->DestSystem;
    }

    delete Temp;
}

FJumpShipEffect::~FJumpShipEffect()
{
    // FUN_00503a80. The order is A, C, D, B; the ship pointer is used with no test.
    CliEffectCompanionArmStop(&FxA);
    CliEffectCompanionClear(&FxA);
    CliEffectCompanionArmStop(&FxC);
    CliEffectCompanionClear(&FxC);
    CliEffectListRemove(&Ship->EffectList, &FxD);
    CliEffectCompanionArmStop(&FxB);
    CliEffectCompanionClear(&FxB);
}

double FJumpShipEffect::ShakeAmplitude() const
{
    // FUN_00503e20: on the ship's +0x158 jump state.
    switch (Ship->JumpState)
    {
    case kJumpStateJumpOut:
        // A JumpOutTime not above 0 (NaN included) returns the amplitude itself.
        if (JumpOutTime > 0.0f)
            return static_cast<double>(kJumpShakeAmplitude) / JumpOutTime * Clock;
        return kJumpShakeAmplitude;
    case kJumpStateJumpOutTunnel:
    case kJumpStateJumpInTunnel:
        return kJumpShakeAmplitude;
    case kJumpStateJumpIn:
        // A JumpInTime not above 0 (NaN included) gives 0.
        if (JumpInTime > 0.0f)
            return (1.0 - static_cast<double>(Clock) / JumpInTime) * kJumpShakeAmplitude;
        return 0.0;
    default:
        return 0.0;
    }
}

namespace
{
    // The identity-rotation local transform the four state hooks build.
    Transform MakeLocalXform(float X, float Y, float Z)
    {
        Transform Xform;
        for (int Row = 0; Row < 3; ++Row)
        {
            for (int Col = 0; Col < 3; ++Col)
                Xform.Rotation.E[Row][Col] = (Row == Col) ? 1.0f : 0.0f;
        }
        Xform.Translation.x = X;
        Xform.Translation.y = Y;
        Xform.Translation.z = Z;
        return Xform;
    }

    // One companion moved onto the ship (each part only when present).
    void AttachCompanionToShip(FJumpFxCompanion& Fx, FClientShipGameobj* Ship, int Index,
                               Transform const& Xform)
    {
        if (Fx.Particle != nullptr)
            Fx.Particle->Attach(Ship, Index, &Xform);
        if (Fx.Sound != nullptr)
            Fx.Sound->SetPose(Index, &Xform);
        if (Fx.Light != nullptr)
            CliExplosionAttachLocalXform(Fx.Light, Index, &Xform);
    }
}

void FJumpShipEffect::StartJumpOut()
{
    // FUN_00503ea0. Nothing without a tunnel.
    if (Tunnel == nullptr)
        return;

    Tunnel->SetState(1);

    // The tunnel effect starts at the gate hardpoint through the jump-effect factory
    // DAT_00674fcc. An empty name, or a start that fails, warns.
    float const Velocity[3] = {0.0f, 0.0f, 0.0f};
    bool bStarted = false;
    if (!TunnelEffect.IsEmpty())
    {
        bStarted = G_CliEffectFactoryJump.CreateEffectWorldOneShotOn(FindEffectArch(&TunnelEffect.Id),
                       &GateHardpointFrame, Velocity, &FxB) != 0;
    }
    if (!bStarted)
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: jump out effect (%s) did not create.",
              kJumpEffectCpp, 0x246, TunnelEffect.get_string());
    }

    bTunnelEffectKilled = 0;
}

void FJumpShipEffect::EnterTunnel()
{
    // FUN_00504210.
    if (Tunnel != nullptr)
        Tunnel->SetState(2);

    {
        Transform const Local = MakeLocalXform(0.0f, 0.0f, 0.0f);
        int const Index = static_cast<int>(Ship->get_index());
        AttachCompanionToShip(FxB, Ship, Index, Local);
    }

    Vector const& ShipPos = Ship->get_position();
    Transform const World = MakeLocalXform(ShipPos.x, ShipPos.y, ShipPos.z);
    float const Velocity[3] = {0.0f, 0.0f, 0.0f};

    ID_String const& JumpOutEffect = Desc->Ids[1];
    if (!JumpOutEffect.IsEmpty() &&
        G_CliEffectFactoryJump.CreateEffectWorldOneShotOn(FindEffectArch(&JumpOutEffect.Id),
                                                          &World, Velocity, &FxA) != 0)
    {
        Transform const Local = MakeLocalXform(0.0f, 0.0f, 0.0f);
        int const Index = static_cast<int>(Ship->get_index());
        AttachCompanionToShip(FxA, Ship, Index, Local);
        return;
    }

    // Retail quirk kept: the warning names the TUNNEL effect, not the one that failed.
    FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: jump out effect (%s) did not create.",
          kJumpEffectCpp, 0x25e, TunnelEffect.get_string());
}

void FJumpShipEffect::StartJumpIn()
{
    // FUN_00504470.
    if (Tunnel != nullptr)
        Tunnel->SetState(3);

    Vector const& ShipPos = Ship->get_position();
    Transform const World = MakeLocalXform(ShipPos.x, ShipPos.y, ShipPos.z);
    float const Velocity[3] = {0.0f, 0.0f, 0.0f};

    ID_String const& JumpInEffect = Desc->Ids[2];
    if (!JumpInEffect.IsEmpty() &&
        G_CliEffectFactoryJump.CreateEffectWorldOneShotOn(FindEffectArch(&JumpInEffect.Id),
                                                          &World, Velocity, &FxC) != 0)
    {
        Transform const Local = MakeLocalXform(0.0f, 0.0f, 0.0f);
        int const Index = static_cast<int>(Ship->get_index());
        AttachCompanionToShip(FxC, Ship, Index, Local);
    }
    else
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: jump in effect (%s) did not create.",
              kJumpEffectCpp, 0x277, JumpInEffect.get_string());
    }

    // On every path.
    bSkipFirstJumpInTick = 1;
}

void FJumpShipEffect::FinishJump()
{
    // FUN_00504640.
    if (Tunnel != nullptr)
        Tunnel->SetState(0);

    // The player's ship takes jump_done_effect_player, any other jump_done_effect_nonplayer.
    bool const bPlayer = static_cast<void*>(static_cast<IObjRW*>(Ship)) == GetPlayerObjRW();
    ID_String const& DoneEffect = bPlayer ? Desc->Ids[3] : Desc->Ids[4];

    // An empty name returns quietly.
    if (DoneEffect.IsEmpty())
        return;

    // The name is tested a second time (retail); the ship is the list owner.
    Transform const Local = MakeLocalXform(0.0f, 0.0f, 0.0f);
    int const Index = static_cast<int>(Ship->get_index());
    if (!DoneEffect.IsEmpty() &&
        CliCreateEffectOneShot(FindEffectArch(&DoneEffect.Id), Ship, Index, &Local, &FxD) != 0)
        return;

    if (bPlayer)
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: player jump done effect (%s) did not create.",
              kJumpEffectCpp, 0x293, DoneEffect.get_string());
    }
    else
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: non-player jump done effect (%s) did not create.",
              kJumpEffectCpp, 0x28b, DoneEffect.get_string());
    }
}

void FJumpShipEffect::Update(float Dt)
{
    // FUN_00503cd0. The effect's clock (+0x70) is not advanced here.
    int const State = Ship->JumpState;
    if ((State == kJumpStateJumpOutTunnel || State == kJumpStateJumpInTunnel) && Tunnel != nullptr)
        Tunnel->Update(Dt);

    // Everything else only in state 4 (jump in), re-read from the ship.
    if (Ship->JumpState != kJumpStateJumpIn)
        return;

    // The first update after StartJumpIn only clears the flag.
    if (bSkipFirstJumpInTick != 0)
    {
        bSkipFirstJumpInTick = 0;
        return;
    }

    // Once only, and only when JumpInTime - KillTimeBeforeDone < clock (ordered). The
    // difference is unrounded, hence the double arithmetic.
    if (bTunnelEffectKilled != 0)
        return;
    if (!(static_cast<double>(JumpInTime) - static_cast<double>(KillTimeBeforeDone) <
          static_cast<double>(Clock)))
        return;

    // The tunnel effect companion is stopped and released (both inlined in retail).
    CliEffectCompanionArmStop(&FxB);
    CliEffectCompanionClear(&FxB);
    bTunnelEffectKilled = 1;
}
