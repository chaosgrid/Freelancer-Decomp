// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CELauncher (retail CELauncher.cpp). Declared in common_cequip.h; helpers shared with the
// sibling TUs are in common_cequip_internal.h (namespace CmnCequip).
#include "common_cequip.h"
#include "x86math.h"

#include "common_animdb.h"

#include "common_cobject.h"
#include "common_csys.h"     // Acquire3DMathEngine
#include "common_fuses.h"
#include "common_iface.h"    // EquipStatus, EquipStatusFlag*
#include "common_ini.h"
#include "common_misc.h"     // SUBOBJ_ID_NONE
#include "common_physys.h"
#include "common_spatial.h"
#include "common_timing.h"
#include "common_universe.h"
#include "DALib.h"
#include "dacom.h"
#include "ivp_physics/ivp_real_object.h"
#include "ivp_physics/ivp_controller.h"

#include <cstdarg>
#include <cstdint>
#include <math.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Equipment/common_cequip_internal.h"

using namespace CmnCequip;

namespace
{
    const char kCELauncherSource[] = "E:\\FL\\Scratch\\Source\\Common\\CELauncher.cpp";

    // The barrel count every CELauncher member inlines: the leading run of bound barrels.
    // Deviation: retail has no upper bound, so with all eight bound it reads past Barrels[7]
    // (CEGun's yaw joint, or whatever follows) until it meets a -1; the port stops at eight.
    unsigned CountLauncherBarrels(CELauncher const& Launcher)
    {
        unsigned Count = 0;
        unsigned Barrel = 0;
        for (; Barrel < 8; ++Barrel)
        {
            if (Launcher.Barrels[Barrel].EngineIndex == -1)
                break;
            Count += 1;
        }
        return Count;
    }

    // A barrel-local point through the barrel instance's engine transform (inlined in three
    // members). No guards.
    Vector BarrelPointWS(long Instance, Vector const& Local)
    {
        Transform const Xform = *DALib::Engine->GetInstanceTransform(Instance);
        Vector Out;
        Acquire3DMathEngine()->TransformXform(Out, Xform, Local);
        return Out;
    }

    // A barrel's world firing direction: -Z of (instance orientation * hardpoint orientation).
    // No guards.
    Vector BarrelDirWS(long Instance, Matrix const& HpOrient)
    {
        Matrix const Ori = *DALib::Engine->GetInstanceOrientation(Instance);
        Matrix World;
        Acquire3DMathEngine()->MulMatrix(World, Ori, HpOrient);
        Vector Out;
        Out.x = -World.E[0][2];
        Out.y = -World.E[1][2];
        Out.z = -World.E[2][2];
        return Out;
    }

    // DAT_063eb6b0: Connect's hardpoint name ("HPFire0", capital P in the raw PE); the digit at
    // +7 is patched per barrel.
    char GLauncherHpFireName[9] = { 'H', 'P', 'F', 'i', 'r', 'e', '0', '\0', '\0' };
}

CELauncher::CELauncher(unsigned TypeBits, CEqObj* OwnerObj, unsigned short EquipId,
                       Archetype::Launcher const* ArchObj, bool bMissionFlag)
    : CAttachedEquip(TypeBits, OwnerObj, EquipId, ArchObj, bMissionFlag)
    , Projectile(nullptr)
    , RefireElapsed(0.0f)
{
    // FUN_062992b0. Deviation: retail only sets each barrel's EngineIndex; the port zeroes the
    // rest of the records.
    memset(Barrels, 0, sizeof(Barrels));
    unsigned Barrel = 0;
    for (; Barrel < 8; ++Barrel)
        Barrels[Barrel].EngineIndex = -1;

    Archetype::Launcher* LauncherArch = (Archetype::Launcher*)(Arch);
    Projectile = Archetype::GetEquipment(LauncherArch->ProjectileArchetype.Id);
    if (Projectile == nullptr)
    {
        FDUMP(kFdumpError,
              "%s(%d) : *** ERROR: Failed to find projectile archetype for gun.  Archetype is \"%s\"",
              kCELauncherSource, 0x22, LauncherArch->ProjectileArchetype.get_string());
    }
    else
    {
        Projectile->load();
    }
}

CELauncher::CELauncher(CELauncher const& Other)
    : CAttachedEquip(Other)
    , Projectile(Other.Projectile)
    , RefireElapsed(Other.RefireElapsed)
{
    memcpy(Barrels, Other.Barrels, sizeof(Barrels));
}

CELauncher::~CELauncher()
{
    // FUN_06299380. The projectile is released with no null test (a missing one was only
    // reported by the ctor).
    CExternalEquip::Destroy();
    Projectile->Release();
}

CELauncher* __cdecl CELauncher::cast(CEquip* Equip)
{
    if (Equip == nullptr)
        return nullptr;
    unsigned TypeBits = Equip->GetType();
    if (TypeBits != EquipTypeGun && TypeBits != EquipTypeMineDropper &&
        TypeBits != EquipTypeCmDropper)
        return nullptr;
    return (CELauncher*)(Equip);
}

CELauncher const* __cdecl CELauncher::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast(const_cast<CEquip*>(Equip));
}

bool CELauncher::Update(float Dt, unsigned Flags)
{
    if (RefireElapsed < 10000.0f)
        RefireElapsed = Dt + RefireElapsed;
    return CAttachedEquip::Update(Dt, Flags);
}

bool CELauncher::IsDisabled() const
{
    // FUN_06299470. NaN health is not disabled. No Owner null test.
    if (Owner->bWeaponsOffline == 0)
        return false;
    return GetRelativeHealth() <= 0.0f;
}

void CELauncher::GetStatus(EquipStatus& OutStatus) const
{
    // RVA 0x39410. EquipFlags: 4 out of ammo (and stop), 8 power met, 2 refire elapsed.
    CEquip::GetStatus(OutStatus);
    if ((OutStatus.Flags & (EquipStatusFlagDestroyed | EquipStatusFlagDisabled)) == 0)
    {
        if (GetAmmoCount() == 0)
        {
            OutStatus.EquipFlags |= LauncherStatusInsufficientAmmo;
            return;
        }
        if (PowerNeedsMet())
            OutStatus.EquipFlags |= LauncherStatusPowerNeedsMet;
        if (RefireDelayElapsed())
            OutStatus.EquipFlags |= LauncherStatusRefireElapsed;
    }
}

bool CELauncher::Connect(char const* HpName)
{
    // FUN_062994a0. Retail patches the shared .data name buffer in place for every barrel.
    if (!CAttachedEquip::Connect(HpName))
        return false;

    unsigned Barrel = 0;
    for (; Barrel < 8; ++Barrel)
    {
        GLauncherHpFireName[7] = (char)('1' + Barrel);
        long Found = -1;   // retail reuses the HpName argument slot; read only after a hit
        HardpointInfo Info;
        if (!FindHardpoint_OS(GetRootIndex(), GLauncherHpFireName, Found, Info))
        {
            if (Barrel != 0)
                return true;
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s has no HpFire01 hardpoint.", kCELauncherSource,
                  0x6f, Arch->DaArchetype.Value);
            return false;
        }
        Barrels[Barrel].EngineIndex = (int)(Found);
        memcpy(&Barrels[Barrel].Orient, &Info.Orient, sizeof(Matrix));
        memcpy(&Barrels[Barrel].Position, &Info.Position, sizeof(Vector));
    }
    return true;
}

void CELauncher::Disconnect()
{
    // RVA 0x3a1f0
    for (unsigned Barrel = 0; Barrel < 8; ++Barrel)
        Barrels[Barrel].EngineIndex = kInvalidEngineInstance;
    CAttachedEquip::Disconnect();
}

FireResult CELauncher::Fire(Vector const& Target)
{
    FireResult Result = CanFire(Target);
    if (Result != FireRefireDelay)
        RefireElapsed = 0.0f;
    return Result;
}

void CELauncher::ConsumeFireResources()
{
    int Count = (int)(CountLauncherBarrels(*this));
    DrawAmmoFromCargo(Count);
    DrawPower(Count);
}

void CELauncher::ComputeLaunchInfo(std::vector<ProjLaunchInfo>& Out, Vector const& Target) const
{
    // FUN_06299630 (push_back is FUN_0629a4c0). Deviation: the default record's Orient and pad
    // bytes are unwritten in retail; zeroed here.
    unsigned Count = CountLauncherBarrels(*this);
    unsigned Barrel = 0;
    for (; Barrel < Count; ++Barrel)
    {
        ProjLaunchInfo Info;
        memset(&Info, 0, sizeof(Info));
        Info.LockSubId = SUBOBJ_ID_NONE;
        Out.push_back(Info);
        ComputeLaunchInfo_OneBarrel(Out.back(), (int)(Barrel), Target);
    }
}

double CELauncher::GetPowerDrawPerFire() const
{
    // FUN_06299580: returned unrounded in ST0. No Arch test.
    return (double)CountLauncherBarrels(*this) * LauncherArch()->PowerUsage;
}

int CELauncher::GetAmmoCount() const
{
    // FUN_06299a10: -1 when no ammo is needed, else the first matching cargo's count, else 0.
    // No Projectile / Owner / Arch tests.
    Archetype::Projectile const* Proj = (Archetype::Projectile const*)(Projectile);
    if (!Proj->bRequiresAmmo)
        return -1;

    CEquipTraverser Walk(EquipTypeCargo);
    CEquip* Equip = Owner->EquipManager.Traverse(Walk);
    while (Equip != nullptr)
    {
        if (Equip->Arch->Nickname.Id == Projectile->Nickname.Id)
            return (int)(((CECargo*)Equip)->Count);
        Equip = Owner->EquipManager.Traverse(Walk);
    }
    return 0;
}

bool CELauncher::AmmoNeedsMet() const
{
    int Ammo = GetAmmoCount();
    if (Ammo < 0)
        return true;
    unsigned Need = CountLauncherBarrels(*this);
    if (Need != 0 && (unsigned)(Ammo) < Need)
        return false;
    return true;
}

FireResult CELauncher::CanFire(Vector const& Target) const
{
    // FUN_06299f80. CloakPercent is read with no Owner test, yet the tradelane check tests
    // Owner -- both as in retail.
    (void)Target;
    if (IsDestroyed())
        return FireDestroyed;
    if (IsDisabled())
        return FireDisabled;
    if (!RefireDelayElapsed())
        return FireRefireDelay;
    if (!PowerNeedsMet())
        return FireNoPower;
    if (!AmmoNeedsMet())
        return FireNoAmmo;
    if (Owner->CloakPercent >= 1.0f)
        return FireCloakActive;
    if (Owner != nullptr && (Owner->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
    {
        CShip* Ship = (CShip*)(Owner);
        if (Ship->is_using_tradelane())
            return FireTradelane;
    }
    return FireOk;
}

bool CELauncher::PowerNeedsMet() const
{
    // FUN_0629a140: unrounded; NaN fails. No Owner test.
    double const Left = (double)(Owner->Power) - GetPowerDrawPerFire();
    return Left >= 0.0;
}

void CELauncher::DrawPower(int BarrelCount)
{
    // FUN_0629a180: unrounded until the store, clamped at 0. No Owner / Arch tests.
    double const Cost = (double)(BarrelCount) *
                        LauncherArch()->PowerUsage;
    if (!(Cost > 0.0))
        return;
    double Next = (double)(Owner->Power) - Cost;
    if (0.0 > Next)
        Next = 0.0;
    Owner->Power = (float)(Next);
}

void CELauncher::DrawAmmoFromCargo(int BarrelCount)
{
    // FUN_0629a0a0: the first matching cargo gives up the ammo. No null guards.
    Archetype::Projectile* Proj = (Archetype::Projectile*)(Projectile);
    if (!Proj->bRequiresAmmo)
        return;

    CEquipTraverser Walk(EquipTypeCargo);
    CEquip* Equip = Owner->EquipManager.Traverse(Walk);
    while (Equip != nullptr)
    {
        if (Equip->Arch->Nickname.Id == Projectile->Nickname.Id)
        {
            ((CECargo*)Equip)->RemoveFromStack((unsigned)(BarrelCount));
            return;
        }
        Equip = Owner->EquipManager.Traverse(Walk);
    }
    FDUMP(kFdumpWarning,
          "%s(%d) : *** WARNING: CELauncher::DrawAmmoFromCargo(%d).  Ammo pack not found!",
          kCELauncherSource, 0x197, BarrelCount);
}

bool CELauncher::RefireDelayElapsed() const
{
    // FUN_0629a1d0: NaN fails. No Arch test.
    return LauncherArch()->RefireDelay <= RefireElapsed;
}

void CELauncher::ComputeLaunchInfo_OneBarrel(ProjLaunchInfo& Out, int Barrel,
                                            Vector const& Target) const
{
    // FUN_062996e0. Leaves LockTarget / LockSubId as the caller's default. No Owner test.
    Vector const OwnerVel = Owner->get_velocity();
    // FUN_06299880 for the barrel point.
    Vector Muzzle;
    Muzzle.x = 0.0f;
    Muzzle.y = 0.0f;
    Muzzle.z = 0.0f;
    if (!IsDestroyed())
        Muzzle = BarrelPointWS(Barrels[Barrel].EngineIndex, Barrels[Barrel].Position);
    Vector Delta;
    Delta.x = Target.x - Muzzle.x;
    Delta.y = Target.y - Muzzle.y;
    Delta.z = Target.z - Muzzle.z;
    // Unrounded; no zero-length guard.
    double const Scale = (double)(LauncherArch()->MuzzleVelocity) /
                         sqrt(((double)(Delta.y) * Delta.y +
                               (double)(Delta.z) * Delta.z) +
                              (double)(Delta.x) * Delta.x);
    Vector DirVel;
    DirVel.x = (float)(Delta.x * Scale);
    DirVel.y = (float)(Delta.y * Scale);
    DirVel.z = (float)(Delta.z * Scale);
    Out.Pos = Muzzle;
    Out.Velocity.x = OwnerVel.x + DirVel.x;
    Out.Velocity.y = DirVel.y + OwnerVel.y;
    Out.Velocity.z = DirVel.z + OwnerVel.z;
    // Oriented along the muzzle velocity alone, not the total.
    Out.Orient = LookMatrix(DirVel);
}

void CELauncher::ComputeProjectilePosAndVelocity_OneBarrel(Vector& Pos, Vector& Vel, int Barrel,
                                                          Vector const& Target) const
{
    // FUN_062998d0. Unlike ComputeLaunchInfo_OneBarrel, dx and dy stay unrounded, dz and the
    // scale are rounded to float. No zero-length guard.
    Vector Muzzle;
    Muzzle.x = 0.0f;
    Muzzle.y = 0.0f;
    Muzzle.z = 0.0f;
    if (!IsDestroyed())
        Muzzle = BarrelPointWS(Barrels[Barrel].EngineIndex, Barrels[Barrel].Position);
    double const Dx = (double)(Target.x) - Muzzle.x;
    double const Dy = (double)(Target.y) - Muzzle.y;
    float const Dz = (float)((double)(Target.z) - Muzzle.z);
    Pos = Muzzle;
    float const Scale = (float)(
        (double)(LauncherArch()->MuzzleVelocity) /
        sqrt(((double)(Dz) * Dz + Dy * Dy) + Dx * Dx));
    Vel.x = (float)(Dx * Scale);
    Vel.y = (float)(Dy * Scale);
    Vel.z = (float)((double)(Dz) * Scale);
}

unsigned CELauncher::GetProjectilesPerFire() const
{
    return CountLauncherBarrels(*this);
}

CELauncher::Barrel const* CELauncher::GetBarrelInfo(unsigned Index) const
{
    // FUN_06299a90; no index test.
    if (IsDestroyed())
        return nullptr;
    if (Barrels[Index].EngineIndex == -1)
        return nullptr;
    return &Barrels[Index];
}

Vector CELauncher::GetBarrelPosWS(unsigned Barrel) const
{
    // FUN_06299ac0. No index or -1 test.
    Vector Out;
    Out.x = 0.0f;
    Out.y = 0.0f;
    Out.z = 0.0f;
    if (!IsDestroyed())
        Out = BarrelPointWS(Barrels[Barrel].EngineIndex, Barrels[Barrel].Position);
    return Out;
}

Vector CELauncher::GetBarrelDirWS(unsigned Barrel) const
{
    // FUN_06299b70. No index or -1 test.
    Vector Out;
    Out.x = 0.0f;
    Out.y = 0.0f;
    Out.z = 0.0f;
    if (!IsDestroyed())
        Out = BarrelDirWS(Barrels[Barrel].EngineIndex, Barrels[Barrel].Orient);
    return Out;
}

Vector CELauncher::GetAvgBarrelPosWS() const
{
    // FUN_06299c40: float sums times an unrounded reciprocal (no zero-count guard). Not called
    // inside Common.dll.
    Vector Sum;
    Sum.x = 0.0f;
    Sum.y = 0.0f;
    Sum.z = 0.0f;
    unsigned Count = CountLauncherBarrels(*this);
    for (unsigned Barrel = 0; Barrel < Count; ++Barrel)
    {
        Vector Pos = GetBarrelPosWS(Barrel);
        Sum.x = Pos.x + Sum.x;
        Sum.y = Pos.y + Sum.y;
        Sum.z = Pos.z + Sum.z;
    }
    double const Inv = 1.0 / (double)((int)(Count));
    Vector Out;
    Out.x = (float)(Sum.x * Inv);
    Out.y = (float)(Sum.y * Inv);
    Out.z = (float)(Sum.z * Inv);
    return Out;
}

Vector CELauncher::GetAvgBarrelDirWS() const
{
    // FUN_06299da0, as GetAvgBarrelPosWS. Not called inside Common.dll.
    Vector Sum;
    Sum.x = 0.0f;
    Sum.y = 0.0f;
    Sum.z = 0.0f;
    unsigned Count = CountLauncherBarrels(*this);
    for (unsigned Barrel = 0; Barrel < Count; ++Barrel)
    {
        Vector Dir = GetBarrelDirWS(Barrel);
        Sum.x = Dir.x + Sum.x;
        Sum.y = Dir.y + Sum.y;
        Sum.z = Dir.z + Sum.z;
    }
    double const Inv = 1.0 / (double)((int)(Count));
    Vector Out;
    Out.x = (float)(Sum.x * Inv);
    Out.y = (float)(Sum.y * Inv);
    Out.z = (float)(Sum.z * Inv);
    return Out;
}

Archetype::Launcher const* CELauncher::LauncherArch() const
{
    return (Archetype::Launcher const*)(Arch);
}

Archetype::Projectile const* CELauncher::ProjectileArch() const
{
    // RVA 0x369c0 (ICF-shared).
    return (Archetype::Projectile const*)(Projectile);
}
