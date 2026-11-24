// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Space/Effects/particle_effect.h"

#include "Common/Graphics/alchemy_float_forward.h"

#include "Space/Effects/cli_projectile_fx.h"
#include "Space/client_gameobj.h"
#include "Space/Effects/effect_base.h"
#include "Space/Effects/effect_type_info.h"
#include "Space/Effects/effect_type_list.h"
#include "Space/Effects/effect_vertex_batch.h" // G_EffectMathEngine (DAT_00674f50)
#include "Common/engine_light.h"
#include "Common/Graphics/fade_to_black.h"       // FadeOutColoured / FadeInColoured / IsFadeOpaque (CameraParticles)
#include "Core/game_options.h"
#include "Core/globals.h"             // ShouldSkipLocalServerTick (FUN_0042d740)
#include "Common/Audio/menu_sound_arch.h"
#include "Core/perf_options.h"
#include "Space/Cameras/space_camera.h"
#include "Space/space_scene_obj_draw.h" // the field controller DAT_00675244 / 0067524c / 00675248

#include "DALib.h"
#include "common_cobject.h"
#include "common_csys.h"          // Acquire3DMathEngine (FUN_005b73e0)
#include "dacom.h"
#include "soundmanager.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

namespace
{


    void CallKillSlot(FParticleEffect* This)
    {
        This->Kill();
    }

    // DAT_00674f50, filled by FUN_005b73e0 on first use at each site.
    I3DMathEngine* ParticleMath()
    {
        if (G_EffectMathEngine == nullptr)
            G_EffectMathEngine = Acquire3DMathEngine();
        return G_EffectMathEngine;
    }

    FEffectBase* AsEffectBase(FParticleEffect* This)
    {
        return This;
    }

    // IFxEffect slots take their float argument as the raw dword.
    void* FloatArg(float Value)
    {
        void* Arg = nullptr;
        std::memcpy(&Arg, &Value, sizeof(float));
        return Arg;
    }

    // These setters retain the original DWORD after their ordered comparisons.
    constexpr float GEffectFloatZero = 0.0f;
    constexpr float GEffectFloatOne = 1.0f;

    const char kEffectCpp[] = "E:\\FL\\Scratch\\Source\\Client\\space\\Effect.cpp";
} // namespace

unsigned char FJumpParticleEffect::UpdateVisibility(float /*Dt*/, FSpaceCamera* /*Cam*/)
{
    FJumpParticleEffect* const Self = this;
    // LAB_005033e0: +0x7c |= 1, and that bit is the result. Neither argument is read.
    Self->Flags7C = static_cast<unsigned char>(Self->Flags7C | 1u);
    return static_cast<unsigned char>(Self->Flags7C & 1u);
}

unsigned char FParticleTunnel::UpdateVisibility(float /*Dt*/, FSpaceCamera* Cam)
{
    FParticleTunnel* const Self = this;
    // FUN_005134c0: bit 0 of +0x7c = Cam is the first-person, third-person or turret camera
    // (tested in that order); that bit is the result. Dt is not read.
    unsigned char bCockpitView = 0;
    if (Cam == GetFirstPersonSpaceCamera() || Cam == GetThirdPersonSpaceCamera() ||
        Cam == GetTurretSpaceCamera())
    {
        bCockpitView = 1;
    }
    Self->Flags7C = static_cast<unsigned char>(((Self->Flags7C ^ bCockpitView) & 1u) ^
                                               Self->Flags7C);
    return static_cast<unsigned char>(Self->Flags7C & 1u);
}

unsigned char FParticleTunnel::TickDraw(float Dt, FSpaceCamera* Cam)
{
    FParticleTunnel* const Self = this;
    // LAB_00513510: the world orientation with the camera's position becomes the local
    // transform; then the particle's own tick, whose answer is returned.
    FTransform World;
    EffectGetWorldTransform(reinterpret_cast<FEffectBase const*>(Self), &World);
    float const* const CamPos = AsCamera(Cam)->GetPosition();
    FTransform Local;
    Local.Rotation = World.Rotation;
    Local.Translation.X = CamPos[0];
    Local.Translation.Y = CamPos[1];
    Local.Translation.Z = CamPos[2];
    Self->LocalXform = Local;
    return ParticleEffectTickDraw(Self, Dt, Cam);
}

FParticleEffect::FParticleEffect(unsigned TypeId) : FEffectBase(TypeId)
{
    FxEffect = nullptr;
    FxNode = nullptr;
    Flags7C = 0;
    std::memset(Pad7D, 0, sizeof(Pad7D));
    RunTime78 = -1.0f;
    Float80 = 1.0f;
}

void ParticleEffectConstruct(FParticleEffect* This, unsigned TypeId)
{
    ::new (This) FParticleEffect(TypeId);
}

FParticleEffectCruise::FParticleEffectCruise() : FParticleEffect(0)
{
    CruiseScale = 1.0f;
}

float FParticleEffect::GetWorldPos(FVector* Out)
{
    FTransform World;
    EffectGetWorldTransform(this, &World);
    *Out = World.Translation;
    return Radius74;
}


void ParticleEffectCleanupAlchemy(FParticleEffect* This)
{
    // FUN_004f8110. No test of DALib::Alchemy.
    if (This->FxEffect != nullptr)
    {
        This->FxEffect->DestroyNodes(reinterpret_cast<void*>(7));
        static_cast<IDAComponent*>(This->FxEffect)->Release();
        This->FxEffect = nullptr;
    }
    if (This->FxNode != nullptr)
    {
        DALib::Alchemy->DetachNode(This->FxNode);
        This->FxNode->Release();
        This->FxNode = nullptr;
    }
}

unsigned char ParticleEffectCreateAlchemyNodes(FParticleEffect* This)
{
    // FUN_004f8360. No null tests on `this`, the DALib globals or the VisEffect.
    // Already created -> 1; not started (bit 4 clear) -> 0.
    if ((This->Flags7C & 4u) != 0)
        return 1;
    if ((This->Flags7C & 0x10u) == 0)
        return 0;

    AGGDESC Desc = {8, "FxNode"};
    GENRESULT const NodeHr = DALib::Dacom->CreateInstance(
        &Desc, reinterpret_cast<void**>(&This->FxNode));
    if (NodeHr != GR_OK)
    {
        FDUMP(0x100002,
              "%s(%d) : *** WARNING: ParticleEffect::start_effect: couldnt create ale.tree "
              "for %s",
              "E:\\FL\\Scratch\\Source\\Client\\space\\Effect.cpp", 0x3dc, This->Vis->Nickname);
        This->Flags7C = static_cast<unsigned char>(This->Flags7C & 0xefu);
        return 0;
    }

    // FUN_0042e080's answer is not read. Bit 3 records that this effect holds a reference
    // (released by the destructor).
    VisEffectArchAddRef(This->Vis);
    This->Flags7C = static_cast<unsigned char>(This->Flags7C | 8u);

    // The out pointer is the member itself.
    unsigned long const Key = This->Vis->EffectCrc;
    GENRESULT const Hr = DALib::FxEffectLibrary->CreateEffect(
        This->FxNode, reinterpret_cast<void*>(Key), &This->FxEffect);
    if (Hr != GR_OK)
    {
        if (Hr == static_cast<GENRESULT>(-8))
        {
            // No flag change.
            FDUMP(0x100001, "%s(%d) : *** ERROR: could not find alchemy effect (%s) in file (%s)",
                  "E:\\FL\\Scratch\\Source\\Client\\space\\Effect.cpp", 0x3eb, This->Vis->Nickname,
                  This->Vis->AlchemyPath);
            return 0;
        }
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: ParticleEffect::start_effect: couldnt create ale.eff "
              "(%s) in file (%s) ... Effect is most likely corrupted",
              "E:\\FL\\Scratch\\Source\\Client\\space\\Effect.cpp", 0x3f0,
              This->Vis->Nickname, This->Vis->AlchemyPath);
        This->Flags7C = static_cast<unsigned char>(This->Flags7C & 0xefu);
        return 0;
    }

    // No null test on the root.
    void* RootRaw = nullptr;
    DALib::Alchemy->GetRootNode(&RootRaw);
    DALib::Alchemy->AttachNode(RootRaw, This->FxNode);
    reinterpret_cast<IDAComponent*>(RootRaw)->Release();

    This->Flags7C = static_cast<unsigned char>(This->Flags7C | 4u);
    return static_cast<unsigned char>((This->Flags7C >> 2) & 1u);
}

unsigned char FParticleEffect::Start(FEffectArch* EffectArch)
{
    FParticleEffect* const This = this;
    // FUN_004f8160. No null test on `this`. Every failure calls the object's own slot +4
    // (dispatched).
    This->Flags08 = static_cast<unsigned char>(This->Flags08 | 2u);
    This->Flags7C = static_cast<unsigned char>(This->Flags7C & 0xefu);

    if (EffectArch == nullptr)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kEffectCpp, 0x37c,
              "ParticleEffect::start_effect: archetype invalid (null archetype passed "
              "specified)");
        CallKillSlot(This);
        return 0;
    }

    if (GetGameOption(kGameOptParticles) == 0)
    {
        CallKillSlot(This);
        return 0;
    }
    if (DALib::Alchemy == nullptr)
    {
        CallKillSlot(This);
        return 0;
    }

    // Signed: a positive id is kept, zero or negative takes the arch's.
    if (static_cast<int>(This->EffectTypeId) <= 0)
        This->EffectTypeId = static_cast<unsigned>(EffectArch->EffectType);

    // FUN_004fd560 is called twice; the first result only seeds the priority.
    FEffectTypeInfo const* const FirstInfo =
        EffectTypeInfoByIndex(static_cast<int>(This->EffectTypeId));
    if (FirstInfo != nullptr)
        This->Priority = FirstInfo->Priority;

    FEffectTypeInfo const* const TypeInfo =
        EffectTypeInfoByIndex(static_cast<int>(This->EffectTypeId));
    if (TypeInfo == nullptr)
    {
        // The name comes from the VisEffect already at +0x04, not from the arch.
        FDUMP(0x100002, "%s(%d) : *** WARNING: no type info specified for %s", kEffectCpp,
              0x395, This->Vis->Nickname);
        CallKillSlot(This);
        return 0;
    }

    // The full effect unless the priority is strictly (ordered) below the perf level
    // (DAT_006129c8) and the generic priority reaches it (ordered); the generic one only if the
    // arch has one.
    float const Perf = GetPerfGenericEffectPerf();
    if (!(TypeInfo->Priority < Perf) || !(TypeInfo->GenericPriority >= Perf) ||
        EffectArch->VisGeneric == nullptr)
    {
        This->Vis = EffectArch->VisEffect;                 // +0x08
        This->Priority = TypeInfo->Priority;
    }
    else
    {
        This->Vis = EffectArch->VisGeneric;                // +0x10
        This->Priority = TypeInfo->GenericPriority;
    }

    if (This->Vis == nullptr)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kEffectCpp, 0x3b0,
              "ParticleEffect::start_effect: archetype invalid (no visarch specified)");
        CallKillSlot(This);
        return 0;
    }

    This->Flags7C = static_cast<unsigned char>(This->Flags7C | 0x10u);
    This->Flags08 = static_cast<unsigned char>(This->Flags08 & 0xfdu);
    This->RunTime78 = TypeInfo->RunTime;                   // +0x18
    This->Radius74 = TypeInfo->Radius;                     // +0x10

    // FUN_004f7af0 inlined (where the one RefCount increment happens).
    if (This->IsAlive() != 0)
        EffectTypeListLink(This);

    return static_cast<unsigned char>((This->Flags7C >> 4) & 1u);
}

unsigned char ParticleEffectStart(FParticleEffect* This, FEffectArch* EffectArch)
{
    return This->FParticleEffect::Start(EffectArch);
}

void FParticleEffect::Kill()
{
    FParticleEffect* const This = this;
    // LAB_004f84d0 -- slot +4 of both particle tables (.rdata 0x005d9ad8, 0x005dd148).
    FEffectBase* const Base = reinterpret_cast<FEffectBase*>(This);

    if (This->FxEffect == nullptr)
    {
        EffectTypeListLink(This);
        EffectFreezeToWorld(Base);
        This->Flags08 = static_cast<unsigned char>(This->Flags08 | 2u);
        return;
    }

    EffectTypeListLink(This);

    if (This->EngineIndex != -1)
    {
        if (EffectWatchedObject(Base) == nullptr)
        {
            // The watched object is gone: dead, then on to the fade-out below. (The link is a
            // no-op by now.)
            EffectTypeListLink(This);
            EffectFreezeToWorld(Base);
            This->Flags08 = static_cast<unsigned char>(This->Flags08 | 2u);
        }
        else
        {
            // Not marked dead: it keeps drawing while the alchemy effect winds down.
            EffectFreezeToWorld(Base);
        }
    }

    // The remaining run time outside [0, 1] is pinned to 1.0; a NaN is left alone.
    if (This->RunTime78 < 0.0f || This->RunTime78 > 1.0f)
        This->RunTime78 = 1.0f;

    // Stop emitting, let the live particles finish.
    This->FxEffect->DestroyNodes(reinterpret_cast<void*>(1));
}

void ParticleEffectArmDeath(FParticleEffect* This)
{
    This->FParticleEffect::Kill();
}

void FParticleEffect::SetIntensity(float Intensity)
{
    FParticleEffect* const This = this;
    // LAB_004f8c20. Nothing without the IFxEffect; otherwise the value is clamped to [0, 1]
    // (NaN passes through) and handed on as the raw float dword.
    IFxEffect* const Fx = This->FxEffect;
    if (Fx == nullptr)
        return;
    float Value;
    std::memcpy(&Value, &Intensity, sizeof(Value));
    if (Value < 0.0f)
        std::memcpy(&Value, &GEffectFloatZero, sizeof(Value));
    else if (Value > 1.0f)
        std::memcpy(&Value, &GEffectFloatOne, sizeof(Value));
    ForwardAlchemyEffectFloat(Fx, &IFxEffect::SetOuterTime, Value);
}

void ParticleEffectSetIntensity(FParticleEffect* This, float Intensity)
{
    This->FParticleEffect::SetIntensity(Intensity);
}

unsigned char FParticleEffect::UpdateVisibility(float Dt, FSpaceCamera* Cam)
{
    FParticleEffect* const This = this;
    // FUN_004f86c0. Dt is not read.
    if (GetGameOption(kGameOptParticles) == 0)
        return 0;

    FEffectBase* const Base = AsEffectBase(This);
    ICamera* const CamIface = AsCamera(Cam);

    // A watched gameobj of ManagerKind 1 or 2.
    FClientGameobj* const Watched = static_cast<FClientGameobj*>(EffectWatchedObject(Base));
    if (Watched != nullptr && (Watched->ManagerKind == 1 || Watched->ManagerKind == 2))
    {
        // A fully cloaked equipped object hides the effect (NaN does not).
        CObject const* const Obj = Watched->cobject();
        if (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x103u) == 0x103u &&
            static_cast<CEqObj const*>(Obj)->CloakPercent >= 1.0f)
            return 0;

        // Only with a field controller (DAT_00675244).
        void* const Field = GetSpaceFieldController();
        if (Field != nullptr)
        {
            unsigned DrawStateB = 0;
            unsigned const DrawStateA = Watched->GetNebula(&DrawStateB);
            if (DrawStateA == 0)
            {
                // Outside every field while the camera is fully inside one.
                if (GetSpaceFieldEnclosure() == 2)
                    return 0;
            }
            else
            {
                // Inside another field than the camera's.
                if (DrawStateA != static_cast<unsigned>(reinterpret_cast<uintptr_t>(Field)))
                    return 0;

                FTransform World;
                EffectGetWorldTransform(Base, &World);
                float const* const CamPos = CamIface->GetPosition();

                // Each difference is rounded to a float, the sum is unrounded; only strictly
                // beyond the radius (DAT_00675248) hides the effect.
                float const Dx = World.Translation.X - CamPos[0];
                float const Dy = World.Translation.Y - CamPos[1];
                float const Dz = World.Translation.Z - CamPos[2];
                double const DistSq = (static_cast<double>(Dz) * Dz + static_cast<double>(Dy) * Dy) +
                                      static_cast<double>(Dx) * Dx;
                if (DistSq > static_cast<double>(GetSpaceFieldRadiusSq()))
                    return 0;
            }
        }
    }

    // Below the perf level (or unordered) returns the visibility bit untouched.
    if (!(This->Priority >= GetPerfGenericEffectPerf()))
        return static_cast<unsigned char>(This->Flags7C & 1u);

    FVector WorldPos;
    float const Radius = This->GetWorldPos(&WorldPos);

    FTransform const* const ViewXform =
        reinterpret_cast<FTransform const*>(CamIface->GetViewTransform());
    FVector ViewPos;
    ParticleMath()->TransformXform(ViewPos, *ViewXform, WorldPos);

    // On only when ZNear is strictly (ordered) below the far edge.
    float const NegZ = -ViewPos.Z;
    float const FarEdge = NegZ + Radius;
    if (!(CamIface->GetZNear() < FarEdge))
        return static_cast<unsigned char>(This->Flags7C & 1u);

    // No null test on the type info here. The pbubble threshold, unrounded, must be strictly
    // greater than the near distance.
    FEffectTypeInfo const* const TypeInfo =
        EffectTypeInfoByIndex(static_cast<int>(This->EffectTypeId));
    double const NearDistance = static_cast<double>(NegZ) - Radius;
    double const Threshold =
        (static_cast<double>(TypeInfo->PbubbleFar) - TypeInfo->PbubbleNear) *
            (1.0 - static_cast<double>(GetPerfGenericEffectPerf())) +
        TypeInfo->PbubbleNear;
    if (!(Threshold > NearDistance))
        return static_cast<unsigned char>(This->Flags7C & 1u);

    // ALCHEMY_LOD: a false LOD blend zeroes +0x80 and leaves the visibility bit as it is.
    if (GetGameOption(kGameOptAlchemyLod) != 0)
    {
        if (!EffectTypeLodBlend(static_cast<int>(This->EffectTypeId), CamIface, NegZ,
                                &This->Float80))
        {
            This->Float80 = 0.0f;
            return static_cast<unsigned char>(This->Flags7C & 1u);
        }
    }

    // The two side planes against the radius; inside only when the distance is below or equal.
    double const DistX = std::fabs(static_cast<double>(ViewPos.X)) * Cam->PlaneX[0] +
                         static_cast<double>(ViewPos.Z) * Cam->PlaneX[2];
    unsigned char Inside = 0;
    if (DistX <= Radius)
    {
        double const DistY = std::fabs(static_cast<double>(ViewPos.Y)) * Cam->PlaneY[1] +
                             static_cast<double>(ViewPos.Z) * Cam->PlaneY[2];
        if (DistY <= Radius)
            Inside = 1;
    }
    This->Flags7C = static_cast<unsigned char>((This->Flags7C & 0xfeu) | Inside);
    return static_cast<unsigned char>(This->Flags7C & 1u);
}

unsigned char ParticleEffectUpdateVisibility(FParticleEffect* This, float Dt, FSpaceCamera* Cam)
{
    return This->FParticleEffect::UpdateVisibility(Dt, Cam);
}

unsigned char FParticleEffect::TickDraw(float Dt, FSpaceCamera* Cam)
{
    FParticleEffect* const This = this;
    // FUN_004f8a20. Cam is not read.
    if (GetGameOption(kGameOptParticles) == 0)
        return 0;

    if (ShouldSkipLocalServerTick())
        return 1;

    // +0x7c is read once. Without the start bit (0x10) the effect is finished.
    unsigned char const Flags = This->Flags7C;
    bool bFinished = true;
    if ((Flags & 0x10u) != 0)
    {
        bFinished = false;
        // A run time above zero counts down by Dt; the unrounded result at or below zero
        // finishes the effect.
        if (This->RunTime78 > 0.0f)
        {
            double const Left = static_cast<double>(This->RunTime78) - Dt;
            This->RunTime78 = static_cast<float>(Left);
            if (Left <= 0.0)
                bFinished = true;
        }
        // With the ale bit (4), a finished IFxEffect also finishes it.
        if (!bFinished && (Flags & 4u) != 0 &&
            static_cast<unsigned char>(This->FxEffect->IsFinished(reinterpret_cast<void*>(4))) != 0)
            bFinished = true;
    }
    if (bFinished)
    {
        EffectTypeListLink(This);
        EffectFreezeToWorld(AsEffectBase(This));
        This->Flags08 = static_cast<unsigned char>(This->Flags08 | 2u);
        return 0;
    }

    FEffectTypeInfo const* const TypeInfo =
        EffectTypeInfoByIndex(static_cast<int>(This->EffectTypeId));
    if (TypeInfo == nullptr)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kEffectCpp, 0x4c8,
              "effect shouldnt have gotten created w/o type info");
        return 0;
    }

    // Neither UPDATE_OFFSCREEN nor visible (+0x7c re-read) -> 0.
    if (TypeInfo->UpdateOffscreen == 0 && (This->Flags7C & 1u) == 0)
        return 0;

    if (ParticleEffectCreateAlchemyNodes(This) == 0)
    {
        CallKillSlot(This);
        return 0;
    }

    if (This->FxEffect != nullptr)
    {
        FTransform World;
        EffectGetWorldTransform(AsEffectBase(This), &World);
        This->SetLod(This->Float80);
        IFxEffect* const Fx = This->FxEffect;
        Fx->SetTransform(nullptr, &World);
        Fx->ComposeTransforms(nullptr);
        Fx->Update(FloatArg(Dt));
    }
    return 1;
}

unsigned char ParticleEffectTickDraw(FParticleEffect* This, float Dt, FSpaceCamera* Cam)
{
    return This->FParticleEffect::TickDraw(Dt, Cam);
}

unsigned char FParticleEffect::BuildGeometry(FSpaceCamera* Cam)
{
    FParticleEffect* const This = this;
    // FUN_004f8be0: the visibility bit is cleared first, before the option test.
    This->Flags7C = static_cast<unsigned char>(This->Flags7C & 0xfeu);
    if (GetGameOption(kGameOptParticles) == 0)
        return 0;
    if (This->FxEffect != nullptr)
        This->FxEffect->Draw(Cam);
    return 1;
}

unsigned char ParticleEffectBuildGeometry(FParticleEffect* This, FSpaceCamera* Cam)
{
    return This->FParticleEffect::BuildGeometry(Cam);
}

void FParticleEffect::SetLod(float Lod)
{
    FParticleEffect* const This = this;
    // LAB_004f8c80: the same [0, 1] clamp as SetIntensity (NaN passes through).
    IFxEffect* const Fx = This->FxEffect;
    if (Fx == nullptr)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kEffectCpp, 0x511,
              "Trying to set LOD on effect that does not exist - ADO");
        return;
    }
    float Value;
    std::memcpy(&Value, &Lod, sizeof(Value));
    if (Value < 0.0f)
        std::memcpy(&Value, &GEffectFloatZero, sizeof(Value));
    else if (Value > 1.0f)
        std::memcpy(&Value, &GEffectFloatOne, sizeof(Value));
    ForwardAlchemyEffectFloat(Fx, &IFxEffect::SetLodInput, Value);
}

void ParticleEffectSetLod(FParticleEffect* This, float Lod)
{
    This->FParticleEffect::SetLod(Lod);
}

float FParticleEffectCruise::GetWorldPos(FVector* Out)
{
    FParticleEffectCruise* const This = this;
    // FUN_004f91c0. The unrounded product strictly below 10.0 becomes 10.0f.
    double const Product = static_cast<double>(This->CruiseScale) * This->Radius74;
    float Radius = static_cast<float>(Product);
    if (Product < 10.0)
        Radius = 10.0f;

    FTransform World;
    EffectGetWorldTransform(AsEffectBase(This), &World);
    *Out = World.Translation;
    return Radius;
}

float ParticleEffectCruiseGetWorldPos(FParticleEffectCruise* This, FVector* Out)
{
    return This->FParticleEffectCruise::GetWorldPos(Out);
}

void FParticleEffectCruise::SetIntensity(float Scale)
{
    FParticleEffectCruise* const This = this;
    // FUN_004f92f0: the scale is stored unclamped; the effect gets SetIntensity's clamp.
    std::memcpy(&This->CruiseScale, &Scale, sizeof(Scale));
    IFxEffect* const Fx = This->FxEffect;
    if (Fx == nullptr)
        return;
    float Value;
    std::memcpy(&Value, &Scale, sizeof(Value));
    if (Value < 0.0f)
        std::memcpy(&Value, &GEffectFloatZero, sizeof(Value));
    else if (Value > 1.0f)
        std::memcpy(&Value, &GEffectFloatOne, sizeof(Value));
    ForwardAlchemyEffectFloat(Fx, &IFxEffect::SetOuterTime, Value);
}

void ParticleEffectCruiseSetScale(FParticleEffectCruise* This, float Scale)
{
    This->FParticleEffectCruise::SetIntensity(Scale);
}

unsigned char FParticleEffectCruise::UpdateVisibility(float Dt, FSpaceCamera* Cam)
{
    FParticleEffectCruise* const This = this;
    // FUN_004f9270: +0x80 saved first, then the base visibility (a direct call). A hidden
    // effect whose saved +0x80 was above zero stays visible while +0x80 fades out, clamped at 0.
    float const SavedLod = This->Float80;
    unsigned char const Visible = ParticleEffectUpdateVisibility(This, Dt, Cam);
    This->Flags7C = static_cast<unsigned char>((This->Flags7C & 0xfeu) | (Visible & 1u));
    if ((This->Flags7C & 1u) == 0 && SavedLod > 0.0f)
    {
        double Faded = static_cast<double>(SavedLod) - static_cast<double>(Dt) * 0.5;
        if (Faded < 0.0)
            Faded = 0.0;
        This->Float80 = static_cast<float>(Faded);
        This->Flags7C = static_cast<unsigned char>(This->Flags7C | 1u);
    }
    return static_cast<unsigned char>(This->Flags7C & 1u);
}

unsigned char ParticleEffectCruiseUpdateVisibility(FParticleEffectCruise* This, float Dt, FSpaceCamera* Cam)
{
    return This->FParticleEffectCruise::UpdateVisibility(Dt, Cam);
}

FParticleEffect::~FParticleEffect()
{
    if ((Flags7C & 4u) != 0)
        ParticleEffectCleanupAlchemy(this);
    if ((Flags7C & 8u) != 0 && Vis != nullptr)
        VisEffectArchRelease(Vis);
}

void ParticleEffectDestruct(FParticleEffect* This)
{
    This->FParticleEffect::~FParticleEffect();
}


void ParticleEffectDestroy(FParticleEffect* This, int FreeMemory)
{
    if ((FreeMemory & 1) != 0)
        delete This;
    else
        This->FParticleEffect::~FParticleEffect();
}


void ParticleEffectCruiseDestroy(FParticleEffect* This, int FreeMemory)
{
    if ((FreeMemory & 1) != 0)
        delete This;
    else
        ((FParticleEffectCruise*)This)->FParticleEffectCruise::~FParticleEffectCruise();
}


void ParticleEffectAddRef(FParticleEffect* This)
{
    // FUN_004f7a80.
    This->RefCount = This->RefCount + 1;
}

int ParticleEffectRelease(FParticleEffect* This)
{
    return EffectRelease(This);
}


void ParticleEffectAttach(FParticleEffect* This, void* WatchableOwner, int EngineIndex,
                          FTransform const* LocalXform)
{
    // FUN_004f7d20, the shared Effect attach.
    This->Attach(WatchableOwner, EngineIndex, LocalXform);
}

FParticleEffect* ParticleEffectCreate()
{
    // LAB_004f6e70: FUN_004f79a0(0x84) (the Heap pool), then FUN_004f8060(0).
    void* const Mem = EffectAlloc(0x84u);
    if (Mem == nullptr)
        return nullptr;
    FParticleEffect* const Effect = static_cast<FParticleEffect*>(Mem);
    ParticleEffectConstruct(Effect, 0);
    return Effect;
}

FParticleEffect* ParticleEffectCreateCruise()
{
    // Inlined in FUN_00528980: FUN_004f79a0(0x90), vptr PTR_FUN_005dd144.
    void* const Mem = EffectAlloc(0x90u);
    if (Mem == nullptr)
        return nullptr;
    FParticleEffectCruise* Effect = static_cast<FParticleEffectCruise*>(Mem);
    ::new (Effect) FParticleEffectCruise();
    return Effect;
}

FParticleEffect* ParticleEffectCreateJump()
{
    // LAB_005033a0: FUN_004f79a0(0x84), FUN_004f8060(0x25), vptr PTR_FUN_005daac4.
    void* const Mem = EffectAlloc(0x84u);
    if (Mem == nullptr)
        return nullptr;
    FParticleEffect* const Effect = static_cast<FParticleEffect*>(Mem);
    ::new (Effect) FJumpParticleEffect();
    return Effect;
}

FParticleEffect* ParticleTunnelCreate()
{
    // LAB_00513440 / LAB_005134a0: FUN_004f79a0(0x84), FUN_004f8060(0), vptr PTR_FUN_005db770.
    void* const Mem = EffectAlloc(0x84u);
    if (Mem == nullptr)
        return nullptr;
    FParticleEffect* const Effect = static_cast<FParticleEffect*>(Mem);
    ::new (Effect) FParticleTunnel();
    return Effect;
}

namespace
{
    // .rdata 0x005d9a74 (0.4f): the fade duration both halves pass, and the timer's seed twice.
    constexpr float kCameraParticlesFadeTime = 0.4f;
    // .rdata 0x005d9a7c / 0x005d9a78: the near and far distances of Start's fade factor.
    constexpr float kCameraParticlesNearDistance = 2000.0f;
    constexpr float kCameraParticlesFarDistance = 3000.0f;
    // The colour both fades are given.
    float const kCameraParticlesWhite[3] = {1.0f, 1.0f, 1.0f};


} // namespace

unsigned char FCameraParticles::Start(FEffectArch* Arch)
{
    FCameraParticles* const Self = this;
    // LAB_004f8e10.
    FTransform World;
    EffectGetWorldTransform(Self, &World);

    // Stored before the test.
    void* const Camera = GetClientSoundListenerObject();
    Self->Camera = Camera;
    if (Camera != nullptr)
    {
        float const* const CamPos =
            ((ICamera*)Camera)->GetPosition();
        float const Dx = CamPos[0] - World.Translation.X;
        float const Dy = CamPos[1] - World.Translation.Y;
        float const Dz = CamPos[2] - World.Translation.Z;
        float const Distance = std::sqrt(Dz * Dz + Dx * Dx + Dy * Dy);
        float const Factor = (Distance - kCameraParticlesNearDistance) /
                             (kCameraParticlesFarDistance - kCameraParticlesNearDistance);

        // A NaN factor gives 0.
        Self->bStartOpaque = static_cast<unsigned char>((Factor <= 0.0f) ? 1 : 0);
        if (Self->bStartOpaque == 0)
        {
            // Strictly greater returns 0 before anything is linked or faded.
            if (Factor > 1.0f)
                return 0;
            Self->Timer = Factor * Self->Timer;
        }
    }

    EffectTypeListLink(Self);

    FadeOutColoured(kCameraParticlesWhite, kCameraParticlesFadeTime);

    // LAB_004f7ab0's body inlined; returns 1.
    return EffectBaseStart(Self, Arch);
}

float FCameraParticles::GetWorldPos(FVector* Out)
{
    FCameraParticles* const Self = this;
    // LAB_004f8df0.
    Out->X = 0.0f;
    Out->Y = 0.0f;
    Out->Z = 0.0f;
    return 100.0f;
}

unsigned char FCameraParticles::TickDraw(float Dt, FSpaceCamera* Cam)
{
    FCameraParticles* const Self = this;
    // LAB_004f8fd0. A Start camera that is not the one drawing re-seeds the timer.
    if (Self->Camera != nullptr && Self->Camera != static_cast<void*>(Cam))
        Self->Timer = kCameraParticlesFadeTime + kCameraParticlesFadeTime;

    // Every return is the negation of the opaque test. An opaque fade goes straight to the
    // fade-in and the kill.
    if (!IsFadeOpaque())
    {
        // A timer that is not above zero (or NaN) keeps the effect alive and does nothing.
        if (!(Self->Timer > 0.0f))
            return 1;
        // NaN also returns 1.
        Self->Timer = Self->Timer - Dt;
        if (!(Self->Timer <= 0.0f))
            return 1;
    }

    FadeInColoured(kCameraParticlesWhite, kCameraParticlesFadeTime, Self->bStartOpaque != 0);

    // FUN_004f7bd0's body inlined.
    EffectKill(Self);
    return 0;
}

FCameraParticles::FCameraParticles() : FEffectBase(0x2cu)
{
    Camera = nullptr;
    bStartOpaque = 1;
    Timer = kCameraParticlesFadeTime + kCameraParticlesFadeTime;
}

void CameraParticlesConstruct(FCameraParticles* This)
{
    ::new (This) FCameraParticles();
}


FEffectBase* CameraParticlesCreate()
{
    // LAB_004f6e40: FUN_004f79a0(0x78), then FUN_004f8d00.
    void* const Mem = EffectAlloc(sizeof(FCameraParticles));
    if (Mem == nullptr)
        return nullptr;
    FCameraParticles* const Effect = static_cast<FCameraParticles*>(Mem);
    CameraParticlesConstruct(Effect);
    return Effect;
}

namespace
{
    FGameLight* CreateEffectGameLight()
    {
        // LAB_00513480 (slot 2 of every factory).
        void* const Mem = GameLightHeapAlloc(0x12c);
        if (Mem == nullptr)
            return nullptr;
        return GameLightConstruct(static_cast<FGameLight*>(Mem));
    }
}

FEffectBase* FStandardCliEffectFactory::CreateParticle() const { return ParticleEffectCreate(); }
ISoundSource* FStandardCliEffectFactory::CreateSound() const { return (ISoundSource*)CreateClientSoundSource3D(); }
FGameLight* FStandardCliEffectFactory::CreateLight() const { return CreateEffectGameLight(); }
FEffectBase* FJumpCliEffectFactory::CreateParticle() const { return ParticleEffectCreateJump(); }
FEffectBase* FCameraCliEffectFactory::CreateParticle() const { return CameraParticlesCreate(); }
ISoundSource* FCameraCliEffectFactory::CreateSound() const { return (ISoundSource*)CreateClientSoundSource(); }
FEffectBase* FTunnelCliEffectFactory::CreateParticle() const { return ParticleTunnelCreate(); }

ISoundSource* FJumpCliEffectFactory::CreateSound() const { return (ISoundSource*)CreateClientSoundSource3D(); }
FGameLight* FJumpCliEffectFactory::CreateLight() const { return CreateEffectGameLight(); }
FGameLight* FCameraCliEffectFactory::CreateLight() const { return CreateEffectGameLight(); }
ISoundSource* FTunnelCliEffectFactory::CreateSound() const { return (ISoundSource*)CreateClientSoundSource(); }
FGameLight* FTunnelCliEffectFactory::CreateLight() const { return CreateEffectGameLight(); }

FStandardCliEffectFactory const G_CliEffectFactoryC60;
FJumpCliEffectFactory const G_CliEffectFactoryJump;
FCameraCliEffectFactory const G_CliEffectFactoryCameraParticles;
FTunnelCliEffectFactory const G_CliEffectFactoryTunnel;

unsigned char CreateEffectCompanionAlive(void* CompanionLocal)
{
    // FUN_00503330 — this = &{arch, sound, particle, light}. No test of `this`; every caller
    // passes an embedded companion's address.
    unsigned char* Base = static_cast<unsigned char*>(CompanionLocal);
    FEffectBase* Particle = *reinterpret_cast<FEffectBase**>(Base + 8);
    if (Particle != nullptr && (Particle->Flags08 & 2u) == 0)
        return 1;
    ISoundSource* Sound = *reinterpret_cast<ISoundSource**>(Base + 4);
    if (Sound != nullptr && Sound->IsActive() != 0)
        return 1;
    void* Light = *reinterpret_cast<void**>(Base + 0xc);
    if (Light != nullptr)
    {
        // Light+0xd4 == 0 → alive. Gamelit layout not owned here.
        int const FieldD4 = *reinterpret_cast<int*>(static_cast<unsigned char*>(Light) + 0xd4);
        if (FieldD4 == 0)
            return 1;
    }
    return 0;
}

unsigned char CliCreateEffectOneShot(FEffectArch* EffectArch, void* ListOwner,
                                     int EngineIndex, FTransform const* LocalXform,
                                     void* CompanionOut)
{
    return CliCreateEffectOneShotOn(&G_CliEffectFactoryC60, EffectArch, ListOwner, EngineIndex,
                                    LocalXform, CompanionOut);
}

unsigned char CliCreateEffectOneShotOn(FCliEffectFactory const* Factory, FEffectArch* EffectArch,
                                       void* ListOwner, int EngineIndex,
                                       FTransform const* LocalXform, void* CompanionOut)
{
    // FUN_00503070 (on the factory).
    if (GetGameOption(kGameOptEffectsGroup) == 0 || EffectArch == nullptr)
        return 0;

    FEffectBase* Particle = nullptr;
    ISoundSource* Sound = nullptr;
    FGameLight* Light = nullptr;

    if (GetGameOption(kGameOptParticles) != 0 && EffectArch->VisEffect != nullptr &&
        EffectArch->VisEffect->Nickname[0] != '\0')
    {
        Particle = Factory->CreateParticle();
    }
    if (EffectArch->SndEffect != nullptr)
        Sound = static_cast<ISoundSource*>(Factory->CreateSound());
    if (EffectArch->LgtRadius > 0.0f)
        Light = Factory->CreateLight();

    // ListOwner is a CliProjectile or a Gameobj.
    if (Particle != nullptr)
        EffectAttach(Particle, ListOwner, EngineIndex, LocalXform);
    if (Sound != nullptr && LocalXform != nullptr)
        Sound->SetPose(EngineIndex, reinterpret_cast<float const*>(LocalXform));
    if (Light != nullptr && LocalXform != nullptr)
        CliExplosionAttachLocalXform(Light, EngineIndex, LocalXform);

    if (Particle != nullptr)
    {
        if (Particle->Start(EffectArch) == 0)
        {
            EffectRelease(Particle);
            Particle = nullptr;
        }
    }
    if (Sound != nullptr)
    {
        Sound->SetLooping(static_cast<unsigned char>(EffectArch->Flags & 1u));
        if (EffectArch->SndEffect == nullptr ||
            Sound->Start(EffectArch->SndEffect, 0) == 0)
        {
            Sound->Release();
            Sound = nullptr;
        }
    }
    if (Light != nullptr)
    {
        EngineLightSetType((FEngineLight*)Light, 1, true);
        unsigned char const Rgb[3] = {EffectArch->LgtColorR, EffectArch->LgtColorG,
                                      EffectArch->LgtColorB};
        EngineLightSetColorBytesAlt((FEngineLight*)Light, Rgb);
        EngineLightSetRadius((FEngineLight*)Light, EffectArch->LgtRadius);
        if (!GameLightEnable((FEngineLight*)Light))
        {
            GameLightRelease(Light);
            Light = nullptr;
        }
        else if (EffectArch->LgtEffectLen != 0)
        {
            Light::AnimationArch* Anim = GameLightFindAnimByName(EffectArch->LgtEffect);
            if (Anim != nullptr)
                GameLightAttachAnim(Light, Anim, EffectArch->LgtRangeScale,
                                    EffectArch->LgtTimeScale, 0.0f);
        }
    }

    unsigned char LocalCompanion[0x10];
    std::memset(LocalCompanion, 0, sizeof(LocalCompanion));
    *reinterpret_cast<FEffectArch**>(LocalCompanion + 0) = EffectArch;
    *reinterpret_cast<ISoundSource**>(LocalCompanion + 4) = Sound;
    *reinterpret_cast<FEffectBase**>(LocalCompanion + 8) = Particle;
    *reinterpret_cast<FGameLight**>(LocalCompanion + 0xc) = Light;

    if (CreateEffectCompanionAlive(LocalCompanion) == 0)
    {
        if (Particle != nullptr)
            EffectRelease(Particle);
        if (Sound != nullptr)
            Sound->Release();
        if (Light != nullptr)
            GameLightRelease(Light);
        return 0;
    }

    if (CompanionOut != nullptr)
    {
        // FUN_00502d90.
        unsigned char* Out = static_cast<unsigned char*>(CompanionOut);
        std::memset(Out, 0, 0x10);
        *reinterpret_cast<FEffectArch**>(Out + 0) = EffectArch;
        *reinterpret_cast<ISoundSource**>(Out + 4) = Sound;
        *reinterpret_cast<FEffectBase**>(Out + 8) = Particle;
        *reinterpret_cast<FGameLight**>(Out + 0xc) = Light;
        if (Particle != nullptr)
            EffectAddRef(Particle);
        if (Sound != nullptr)
            Sound->AddRef();
        if (Light != nullptr)
            GameLightAddRef(Light);
    }

    if (ListOwner != nullptr)
        CliEffectListInsert(static_cast<unsigned char*>(ListOwner) + 0x28, LocalCompanion);

    if (Particle != nullptr)
        EffectRelease(Particle);
    if (Sound != nullptr)
        Sound->Release();
    if (Light != nullptr)
        GameLightRelease(Light);
    return 1;
}

unsigned char CliCreateEffectWorldOneShot(FEffectArch* EffectArch, FTransform const* LocalXform,
                                          float const* Pos3, void* CompanionOut)
{
    return CliCreateEffectWorldOneShotOn(&G_CliEffectFactoryC60, EffectArch, LocalXform, Pos3,
                                         CompanionOut);
}

unsigned char CliCreateEffectWorldOneShotOn(FCliEffectFactory const* Factory,
                                            FEffectArch* EffectArch, FTransform const* LocalXform,
                                            float const* Pos3, void* CompanionOut)
{
    // FUN_00503f40. The effects option off and a null EffectArch return 0; neither pointer
    // argument is tested.
    if (GetGameOption(kGameOptEffectsGroup) == 0 || EffectArch == nullptr)
        return 0;

    FEffectBase* Particle = nullptr;
    ISoundSource* Sound = nullptr;
    FGameLight* Light = nullptr;

    if (GetGameOption(kGameOptParticles) != 0 && EffectArch->VisEffect != nullptr &&
        EffectArch->VisEffect->Nickname[0] != '\0')
    {
        Particle = Factory->CreateParticle();
    }
    if (EffectArch->SndEffect != nullptr)
        Sound = static_cast<ISoundSource*>(Factory->CreateSound());
    if (EffectArch->LgtRadius > 0.0f)
        Light = Factory->CreateLight();

    if (Particle != nullptr)
        std::memcpy(&Particle->LocalXform, LocalXform, sizeof(FTransform));

    // A plain (non-3D) Source from DAT_00674c5c's factory ignores the position.
    if (Sound != nullptr)
    {
        Sound->SetPosition(const_cast<float*>(&LocalXform->Translation.X));
        Sound->SetVelocity(Pos3);
    }

    if (Light != nullptr)
    {
        if (static_cast<int>(Light->EngineIndex) == -1)
        {
            EngineLightSetTransform((FEngineLight*)Light, &LocalXform->Rotation.D[0][0],
                                    &LocalXform->Translation.X);
        }
        else
        {
            std::memcpy(Light->LocalXform, LocalXform, sizeof(FTransform));
        }
        GameLightSetFreeMotionOffset(Light, Pos3);
    }

    // Pos3 is the particle's frozen velocity (+0x4c).
    if (Particle != nullptr)
    {
        float* const Velocity = reinterpret_cast<float*>(Particle->FrozenVelocity);
        Velocity[0] = Pos3[0];
        Velocity[1] = Pos3[1];
        Velocity[2] = Pos3[2];
    }

    // The factory's class decides (CameraParticles for DAT_00674c5c).
    if (Particle != nullptr)
    {
        if (Particle->Start(EffectArch) == 0)
        {
            EffectRelease(Particle);
            Particle = nullptr;
        }
    }
    if (Sound != nullptr)
    {
        Sound->SetLooping(static_cast<unsigned char>(EffectArch->Flags & 1u));
        if (EffectArch->SndEffect == nullptr ||
            Sound->Start(EffectArch->SndEffect, 0) == 0)
        {
            Sound->Release();
            Sound = nullptr;
        }
    }
    if (Light != nullptr)
    {
        EngineLightSetType((FEngineLight*)Light, 1, true);
        unsigned char const Rgb[3] = {EffectArch->LgtColorR, EffectArch->LgtColorG,
                                      EffectArch->LgtColorB};
        EngineLightSetColorBytesAlt((FEngineLight*)Light, Rgb);
        EngineLightSetRadius((FEngineLight*)Light, EffectArch->LgtRadius);
        if (!GameLightEnable((FEngineLight*)Light))
        {
            GameLightRelease(Light);
            Light = nullptr;
        }
        else if (EffectArch->LgtEffectLen != 0)
        {
            Light::AnimationArch* Anim = GameLightFindAnimByName(EffectArch->LgtEffect);
            if (Anim != nullptr)
                GameLightAttachAnim(Light, Anim, EffectArch->LgtRangeScale,
                                    EffectArch->LgtTimeScale, 0.0f);
        }
    }

    unsigned char LocalCompanion[0x10];
    std::memset(LocalCompanion, 0, sizeof(LocalCompanion));
    *reinterpret_cast<FEffectArch**>(LocalCompanion + 0) = EffectArch;
    *reinterpret_cast<ISoundSource**>(LocalCompanion + 4) = Sound;
    *reinterpret_cast<FEffectBase**>(LocalCompanion + 8) = Particle;
    *reinterpret_cast<FGameLight**>(LocalCompanion + 0xc) = Light;

    if (CreateEffectCompanionAlive(LocalCompanion) == 0)
    {
        if (Particle != nullptr)
            EffectRelease(Particle);
        if (Sound != nullptr)
            Sound->Release();
        if (Light != nullptr)
            GameLightRelease(Light);
        return 0;
    }

    if (CompanionOut != nullptr)
    {
        unsigned char* Out = static_cast<unsigned char*>(CompanionOut);
        std::memset(Out, 0, 0x10);
        *reinterpret_cast<FEffectArch**>(Out + 0) = EffectArch;
        *reinterpret_cast<ISoundSource**>(Out + 4) = Sound;
        *reinterpret_cast<FEffectBase**>(Out + 8) = Particle;
        *reinterpret_cast<FGameLight**>(Out + 0xc) = Light;
        if (Particle != nullptr)
            EffectAddRef(Particle);
        if (Sound != nullptr)
            Sound->AddRef();
        if (Light != nullptr)
            GameLightAddRef(Light);
    }

    // No list insert (unlike FUN_00503070).
    if (Particle != nullptr)
        EffectRelease(Particle);
    if (Sound != nullptr)
        Sound->Release();
    if (Light != nullptr)
        GameLightRelease(Light);
    return 1;
}
