// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "Space/Effects/beam_effect.h"
#include "Space/Effects/effect_base.h" // FEffectBase (FCameraParticles head)
#include "Common/Graphics/vis_effect_arch.h"
#include "x86math.h"

#include "alchemy.h"
#include "common_goods.h"

#include <cstddef>

struct FGameLight; // engine_light.h

// Retail ParticleEffect — RTTI / Effect.cpp. Vtable PTR_FUN_005d9ad4.
// Same 0x84 footprint as BeamEffect, but +0x04 = VisEffect*, +0x6c = IFxEffect*,
// +0x70 = FxNode*, +0x7c = Alchemy flags (bit2 created, bit3 ale loaded, bit4 start).

// FParticleEffect::Flags7C (+0x7c).
enum EParticleEffectFlag
{
    kParticleFlagVisible = 0x1,   // the result of UpdateVisibility
    kParticleFlagFxCreated = 0x4, // the alchemy ale.eff exists (FxNode / FxEffect), made by FUN_004f8360
    kParticleFlagArchRef = 0x8,   // holds a reference on the VisEffect archetype; the destructor releases it
    kParticleFlagStarted = 0x10   // Start succeeded (the effect runs)
};

struct FParticleEffect : FEffectBase
{
    explicit FParticleEffect(unsigned TypeId);
    unsigned char Start(FEffectArch* EffectArch) override;
    void Kill() override;
    float GetWorldPos(Vector* Out) override;
    void SetIntensity(float Intensity) override;
    unsigned char IsAlive() override { return 1; }
    unsigned char UpdateVisibility(float Dt, FSpaceCamera* Cam) override;
    unsigned char TickDraw(float Dt, FSpaceCamera* Cam) override;
    unsigned char BuildGeometry(FSpaceCamera* Cam) override;
    ~FParticleEffect() override;
    virtual void SetLod(float Lod);

    IFxEffect* FxEffect;           // +0x6C
    IDAComponent* FxNode;          // +0x70
    float Radius74;                // +0x74  from EffectTypeInfo.Radius
    float RunTime78;               // +0x78  from EffectTypeInfo.RunTime
    unsigned char Flags7C;         // +0x7C  bit2=ale.eff, bit3=ale loaded, bit4=start
    unsigned char Pad7D[3];
    float Float80;                 // +0x80  = 1.0; the ALCHEMY_LOD blend (FUN_0042dcd0)

    // FUN_004f8110 — destroy and release the IFxEffect, detach and release the FxNode.
    void CleanupAlchemy();

    // FUN_004f8360 — CreateInstance FxNode + LoadAle + CreateEffect + AttachNode(root).
    unsigned char CreateAlchemyNodes();

    // FUN_004f8030 — slot +0x20 of PTR_FUN_005d9ad4: destruct; FreeMemory&1 → Heap::Free on the
    // 0x84 pool (EffectFree).
    void Destroy(int FreeMemory);
};

static_assert(sizeof(FParticleEffect) == 0x84, "ParticleEffect 0x84");
static_assert(offsetof(FParticleEffect, Vis) == 0x04, "Vis @ +0x04");
static_assert(offsetof(FParticleEffect, FxEffect) == 0x6c, "FxEffect @ +0x6c");
static_assert(offsetof(FParticleEffect, FxNode) == 0x70, "FxNode @ +0x70");
static_assert(offsetof(FParticleEffect, Flags7C) == 0x7c, "Flags7C @ +0x7c");

// NPC cruise exhaust (made in FUN_00528980): sizeof 0x90 = ParticleEffect + float@+0x84.
// Vtable PTR_FUN_005dd144 — Start/ArmDeath shared with trail; destroy = FUN_00528cc0
// (frees 0x90); slots +0x08/+0x0c/+0x14 override radius/visibility for Scale@+0x84.
struct FParticleEffectCruise : FParticleEffect
{
    FParticleEffectCruise();
    float GetWorldPos(Vector* Out) override;
    void SetIntensity(float Scale) override;
    unsigned char UpdateVisibility(float Dt, FSpaceCamera* Cam) override;
    float CruiseScale;         // +0x84  = 1.0f at factory; FUN_004f92f0 setter
    unsigned char Pad88[8];    // +0x88 … +0x8f — retail alloc 0x90
};

static_assert(sizeof(FParticleEffectCruise) == 0x90, "ParticleEffectCruise 0x90");
static_assert(offsetof(FParticleEffectCruise, CruiseScale) == 0x84, "CruiseScale @ +0x84");

struct FJumpParticleEffect : FParticleEffect
{
    FJumpParticleEffect() : FParticleEffect(0x25u) {}
    unsigned char UpdateVisibility(float Dt, FSpaceCamera* Cam) override;
};

struct FParticleTunnel : FParticleEffect
{
    FParticleTunnel() : FParticleEffect(0) {}
    unsigned char UpdateVisibility(float Dt, FSpaceCamera* Cam) override;
    unsigned char TickDraw(float Dt, FSpaceCamera* Cam) override;
};

// FUN_004f8060 — construct; param = initial EffectTypeId (0 from factory).
void ParticleEffectConstruct(FParticleEffect* This, unsigned TypeId);

struct FSpaceCamera;

// FUN_004f7f70 — destructor body (alchemy cleanup, base vptr, watcher cleared).
void ParticleEffectDestruct(FParticleEffect* This);

// FUN_00528cc0 — slot +0x20 of PTR_FUN_005dd144 (cruise): destruct; FreeMemory&1 →
// FUN_004f79c0(this, 0x90), i.e. free().
void ParticleEffectCruiseDestroy(FParticleEffect* This, int FreeMemory);

// LAB_004f6e70 — EffectAlloc(0x84) (Heap pool) + Construct(0).
FParticleEffect* ParticleEffectCreate();

// FUN_00528980 NPC-cruise arm — EffectAlloc(0x90) (malloc) + Construct(0) + CruiseScale=1 +
// PTR_FUN_005dd144. Destroy path: FUN_00528cc0 (ParticleEffectCruiseDestroy).
FParticleEffect* ParticleEffectCreateCruise();

// LAB_005033a0 (slot 0 of the jump-effect factory, .rdata 005daab4) -- EffectAlloc(0x84) (the
// Heap pool), FUN_004f8060(0x25), then the jump particle's own vtable PTR_FUN_005daac4: the
// particle's table with slot +0x14 LAB_005033e0 (bit 0 of +0x7c raised, never culled) and slot
// +0x20 FUN_005033f0 (destruct, then FUN_004f79c0(this, 0x84)).
FParticleEffect* ParticleEffectCreateJump();

// LAB_00513440 (slot 0 of the tradelane factory DAT_006751cc, .rdata 005db760) -- EffectAlloc(0x84)
// (the Heap pool), then LAB_005134a0: FUN_004f8060(0) and the ParticleTunnel vtable
// PTR_FUN_005db770 (RTTI .?AUParticleTunnel@@): the particle's table with +0x14 FUN_005134c0 (bit 0
// of +0x7c = the drawing camera is a cockpit camera), +0x18 LAB_00513510 (follow the camera's
// position, then the particle tick) and +0x20 FUN_005033f0.
FParticleEffect* ParticleTunnelCreate();

// Retail CameraParticles (RTTI .?AUCameraParticles@@ at vtable PTR_LAB_005d9d84[-4], nine
// slots) -- the particle of the tradelane factory DAT_00674c5c. No Alchemy: it is a white
// screen flash. Start fades the screen out to white when the listener camera is near the effect;
// the draw tick counts the timer down, fades back in and kills the effect. 0x78 bytes: the
// Effect head (FUN_004f79f0's body inlined into the ctor FUN_004f8d00, type id 0x2c), then three
// fields of its own.
struct FCameraParticles : FEffectBase
{
    FCameraParticles();
    unsigned char Start(FEffectArch* Arch) override;
    float GetWorldPos(Vector* Out) override;
    unsigned char IsAlive() override { return 1; }
    unsigned char UpdateVisibility(float Dt, FSpaceCamera* Cam) override { return 1; }
    unsigned char TickDraw(float Dt, FSpaceCamera* Cam) override;
    unsigned char BuildGeometry(FSpaceCamera* Cam) override { return 1; }
    float Timer;                   // +0x6c  ctor: 0.4f + 0.4f (.rdata 0x005d9a74 doubled)
    unsigned char bStartOpaque;    // +0x70  ctor 1; Start: distance factor <= 0.0f
    unsigned char Pad71[3];
    void* Camera;                  // +0x74  ctor 0; Start: FUN_0040f130 (the listener camera)
};
static_assert(sizeof(FCameraParticles) == 0x78, "CameraParticles 0x78 (LAB_004f6e40)");
static_assert(offsetof(FCameraParticles, Timer) == 0x6c, "Timer @ +0x6c");
static_assert(offsetof(FCameraParticles, bStartOpaque) == 0x70, "bStartOpaque @ +0x70");
static_assert(offsetof(FCameraParticles, Camera) == 0x74, "Camera @ +0x74");

// FUN_004f8d00 -- the CameraParticles constructor.
void CameraParticlesConstruct(FCameraParticles* This);

// LAB_004f6e40 (slot 0 of the factory DAT_00674c5c, .rdata 005d99b8) -- FUN_004f79a0(0x78),
// which is malloc for any size but 0x84, then a tail jump into FUN_004f8d00; null memory returns
// null. The factory slot is the generic Effect* slot the one-shot creators read through the
// shared head only (+0x10, +0x4c, +0x58, vtable slots 0 and 8), so the object goes out as the
// slot's FParticleEffect* type; nothing reads a ParticleEffect-only field through it.
FEffectBase* CameraParticlesCreate();

// FUN_00503330 — companion alive? (particle Flags08 bit1 clear / sound / light).
unsigned char CreateEffectCompanionAlive(void* CompanionLocal);

// FUN_00503070 — createeffect one-shot (particle, sound and light). A non-null ListOwner gets
// the companion inserted into its effect list (FUN_004fcea0 on ListOwner+0x28).
// This wrapper is FUN_00503070 on DAT_00674c60, the factory almost every caller passes.
unsigned char CliCreateEffectOneShot(FEffectArch* EffectArch, void* ListOwner,
                                     int EngineIndex, Transform const* LocalXform,
                                     void* CompanionOut);

struct FCliEffectFactory;

// The effect factories FUN_00503f40 / FUN_00503070 are called on: .data objects holding only
// a vptr, stamped by static initializers, whose three slots make the particle (+0), the sound
// (+4) and the light (+8):
//   DAT_00674c60 (LAB_004f6e20) -> .rdata 005d99a8 {LAB_004f6e70, LAB_00503420, LAB_00513480}
//   DAT_00674c5c (LAB_004f6e30) -> .rdata 005d99b8 {LAB_004f6e40, LAB_00513460, LAB_00513480}
//   DAT_00674c58 (LAB_004f6e60) -> .rdata 005d99c8 {LAB_004f6e70, LAB_004b5010, LAB_00513480}
//   DAT_00674fcc (LAB_00503390) -> .rdata 005daab4 {LAB_005033a0, LAB_00503420, LAB_00513480}
//   DAT_006751cc (LAB_00513430) -> .rdata 005db760 {LAB_00513440, LAB_00513460, LAB_00513480}
// All but DAT_00674c58 have a table here; nothing but its own static init touches that one (its
// sound slot LAB_004b5010 returns null). The particle slot is the
// generic Effect* slot: the one-shot creators touch the object only through the shared head
// and vtable slots 0 and 8, so a CameraParticles goes through it as well.
struct ISoundSource;
struct FCliEffectFactory
{
    virtual FEffectBase* CreateParticle() const = 0;
    virtual ISoundSource* CreateSound() const = 0;
    virtual FGameLight* CreateLight() const = 0;

    // FUN_00503070 with its `this` factory explicit. Callers on another factory: FUN_00514750
    // (DAT_006751cc) and FUN_00514070, which forwards its own factory (DAT_006751cc or DAT_00674c60).
    unsigned char CreateEffectOneShotOn(FEffectArch* EffectArch, void* ListOwner, int EngineIndex,
                                        Transform const* LocalXform, void* CompanionOut) const;

    // FUN_00503f40 (on Factory) — world-space one-shot (no ListOwner attach).
    // Copies 0x30 LocalXform into particle+0x10 / light+0xf0 (or EngineLightSetTransform when
    // light EngIndex==-1); Pos3 → particle+0x4c / sound SetVelocity / GameLight+0x120.
    // CompanionOut may be null (tradelane player leave/approach). No FUN_004fcea0.
    unsigned char CreateEffectWorldOneShotOn(FEffectArch* EffectArch, Transform const* LocalXform,
                                             float const* Pos3, void* CompanionOut) const;
};

struct FStandardCliEffectFactory : FCliEffectFactory
{
    FEffectBase* CreateParticle() const override;
    ISoundSource* CreateSound() const override;
    FGameLight* CreateLight() const override;
};

struct FJumpCliEffectFactory : FCliEffectFactory
{
    FEffectBase* CreateParticle() const override;
    ISoundSource* CreateSound() const override;
    FGameLight* CreateLight() const override;
};

struct FCameraCliEffectFactory : FCliEffectFactory
{
    FEffectBase* CreateParticle() const override;
    ISoundSource* CreateSound() const override;
    FGameLight* CreateLight() const override;
};

struct FTunnelCliEffectFactory : FCliEffectFactory
{
    FEffectBase* CreateParticle() const override;
    ISoundSource* CreateSound() const override;
    FGameLight* CreateLight() const override;
};

extern FStandardCliEffectFactory const G_CliEffectFactoryC60;
extern FJumpCliEffectFactory const G_CliEffectFactoryJump;
extern FCameraCliEffectFactory const G_CliEffectFactoryCameraParticles;
extern FTunnelCliEffectFactory const G_CliEffectFactoryTunnel;

// FUN_00503f40 on DAT_00674c60 (the explosion's factory, among others). The DAT_00674c5c
// callers go through FCliEffectFactory::CreateEffectWorldOneShotOn with G_CliEffectFactoryCameraParticles.
unsigned char CliCreateEffectWorldOneShot(FEffectArch* EffectArch, Transform const* LocalXform,
                                          float const* Pos3, void* CompanionOut);
