// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CEGun, CEGun::JointChannel and the turret helpers (retail CEGun.cpp). Declared in
// common_cequip.h; helpers shared with the sibling TUs are in common_cequip_internal.h
// (namespace CmnCequip).
#include "common_cequip.h"
#include "x86math.h"

#include "common_animdb.h"  // AnimDB::Add/Rem

#include "common_cobject.h"
#include "common_csys.h"     // Acquire3DMathEngine
#include "common_fuses.h"
#include "common_iface.h"
#include "common_ini.h"
#include "common_misc.h"
#include "common_physys.h"
#include "common_spatial.h"
#include "common_timing.h"
#include "common_universe.h"
#include "DALib.h"
#include "dacom.h"
#include "x87_trig.h"        // retail's inline FCOS / FPTAN / FPATAN
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
    const char kCEGunSource[] = "E:\\FL\\Scratch\\Source\\Common\\CEGun.cpp";
    const float kDegToRad = 0.017453292f;   // DAT_0639d210
    const float kGunPi = 3.1415927f;
    const float kGunTwoPi = 6.2831855f;
    const float kSeeTargetAngle = 0.05235988f;
    const float kTargetPointHold = 2.0f;
    const float kSeekerLeadSpeed = 1.0e9f;

    const unsigned kWatchableOffset = 0x9c;   // offsetof(CSimple, SelfWatchable)

    // CEGun's lock watcher (+0x250) watches CSimple::SelfWatchable; retail recovers the CSimple
    // with `W ? W - 0x9c : 0` and then tests the result for null as well (W == 0x9c).
    CSimple* DecodeLockWatchable(Watchable const* Watch)
    {
        if (Watch == nullptr)
            return nullptr;
        // The owning CSimple of its SelfWatchable member (container-of; no base class to cast to).
        return (CSimple*)((char*)Watch - kWatchableOffset);
    }

    Watchable const* EncodeLockWatchable(CSimple* Target)
    {
        if (Target == nullptr)
            return nullptr;
        return &Target->SelfWatchable;
    }

    double JointAbsDelta(CEGun::JointChannel const& Joint)
    {
        // Inlined in Update, CanSeeTargetObject and FUN_06297560. Retail: an unlimited joint
        // folds a delta above pi to pi - delta, not 2pi - delta (FSUBR pi, .text:06298611).
        double Delta = fabs((double)(Joint.Current) - Joint.Desired);
        if (Joint.bLimited == 0 && kGunPi < Delta)
            Delta = (double)(kGunPi) - Delta;
        return Delta;
    }

    bool JointWithin(CEGun::JointChannel const& Joint, float Threshold)
    {
        if (Joint.EngineIndex == kInvalidEngineInstance)
            return true;
        return JointAbsDelta(Joint) < Threshold;
    }

    void ComputeAimAngles(CEGun const& Gun, float& OutYaw, float& OutPitch, bool bUseLead,
                          Vector const& StoredRel)
    {
        // Inlined three times. Uses the fixed parent connector, not the rotating gun root:
        // Local = transpose(ConnectorOrientation * TurretFrame) * Dir.
        Vector Dir;
        if (bUseLead)
        {
            // Deviation: retail ignores ComputeTgtLeadPosition's result, and on false (a
            // destroyed gun) the lead is uninitialised stack. Zeroed here.
            Vector Lead;
            Lead.x = 0.0f;
            Lead.y = 0.0f;
            Lead.z = 0.0f;
            Gun.ComputeTgtLeadPosition(Lead);
            Vector Origin = Gun.GetTurretOrigin();
            Dir.x = Lead.x - Origin.x;
            Dir.y = Lead.y - Origin.y;
            Dir.z = Lead.z - Origin.z;
        }
        else
        {
            Dir = StoredRel;
        }

        long const Connector = Gun.GetParentConnector(false);
        const Matrix* const Ori = DALib::Engine->GetInstanceOrientation(
            Connector);
        Matrix ConnectorOri(Ori->E[0][0], Ori->E[0][1], Ori->E[0][2], Ori->E[1][0], Ori->E[1][1], Ori->E[1][2], Ori->E[2][0], Ori->E[2][1], Ori->E[2][2]);
        Matrix Combined;
        Acquire3DMathEngine()->MulMatrix(Combined, ConnectorOri, Gun.TurretFrame);
        Matrix Inv(Combined.E[0][0], Combined.E[1][0], Combined.E[2][0],
                   Combined.E[0][1], Combined.E[1][1], Combined.E[2][1],
                   Combined.E[0][2], Combined.E[1][2], Combined.E[2][2]);
        Vector Local;
        Acquire3DMathEngine()->TransformMatrix(Local, Inv, Dir);
        // x87 FPATAN with one float rounding of each angle; the pitch keeps the zero-direction
        // NaN of y / sqrt(z*z + x*x).
        OutYaw = -X87Atan2F((double)(Local.x), -(double)(Local.z));
        double const Horiz = sqrt((double)(Local.z) * Local.z +
                                  (double)(Local.x) * Local.x);
        OutPitch = X87AtanF((double)(Local.y) / Horiz);
    }
}

// ---------------------------------------------------------------------------
// CEGun::JointChannel (vftable .rdata:0639d200)
// ---------------------------------------------------------------------------

// Inlined in CEGun::CEGun and CEGun::LoseJoints.
CEGun::JointChannel::JointChannel()
    : Owner(nullptr)
    , EngineIndex(kInvalidEngineInstance)
    , bLimited(1)
    , Desired(0.0f)
    , Current(0.0f)
    , bSettled(0)
{
}

// FUN_06299080
void CEGun::JointChannel::Bind(CEGun* NewOwner, long Instance)
{
    Owner = NewOwner;
    EngineIndex = (int)(Instance);
    // A full-rotation joint (range >= 2pi, DAT_0639c9e0) is unlimited.
    FEngineJointBlock const* const Rel = InstanceRelative(Instance);
    if (Rel != nullptr)
    {
        if (fabs((double)Rel->Min - Rel->Max) >= 6.2831854820251465)
            bLimited = 0;
    }
    if (DALib::Engine->GetJointParams(EngineIndex, 0, &Current) == 0)
    {
        // No null guards on the owner chain.
        FDUMP(kFdumpError,
              "%s(%d) : *** ERROR: ID(0x%X) Failed to get_joint_state() for gun.  Get Scott. ARCH(%s) FILE(%s)",
              kCEGunSource, 0x336, Owner->Owner->Id, Owner->Arch->Nickname.get_string(),
              Owner->Arch->DaArchetype.Value);
    }
}

// FUN_06299130. Values kept on the x87 stack (Delta, the unrounded Step) are doubles here.
void CEGun::JointChannel::Tick(float Dt)
{
    double Delta = (double)(Desired) - (double)(Current);
    if (bLimited == 0)
    {
        if (Delta > (double)(kGunPi))
            Delta -= (double)(kGunTwoPi);
        else if (Delta < -(double)(kGunPi))
            Delta += (double)(kGunTwoPi);
    }

    float Next = Current;
    // The step is compared unrounded but added as a float.
    float const Rate =
        (Owner != nullptr) ? Owner->GunArch()->TurnRate : 0.0f;
    double const Step = (double)(Rate) * (double)(Dt);
    float const StepF = (float)(Step);
    // A step past the remaining delta lands exactly on Desired; NaN steps backwards.
    if (Step > fabs(Delta))
    {
        Next = (float)((double)(Next) + Delta);
    }
    else if (Delta > 0.0)
    {
        Next = StepF + Next;
    }
    else
    {
        Next = Next - StepF;
    }

    if (Next > kGunPi)
        Next = Next - kGunTwoPi;
    else if (Next < -kGunPi)
        Next = Next + kGunTwoPi;

    // Set, then read back into the same local; results ignored, no guards.
    DALib::Engine->SetJointParams(EngineIndex, 0, &Next);
    DALib::Engine->GetJointParams(EngineIndex, 0, &Next);

    // Settled when the move was below 1e-4 (DAT_0639c2b8); NaN is not settled.
    bSettled = (fabs((double)(Current) - (double)(Next)) < 1.0e-4) ? 1 : 0;
    Current = Next;
}

// 0x062a66a0 (ICF-shared).
int CEGun::JointChannel::GetId() const
{
    return EngineIndex;
}

CEGun::CEGun(CEqObj* OwnerObj, unsigned short EquipId, Archetype::Gun const* ArchObj,
             bool bMissionFlag)
    : CELauncher(EquipTypeGun, OwnerObj, EquipId, ArchObj, bMissionFlag)
    , LockWatcher()
    , LockSubId(SUBOBJ_ID_NONE)
    , LockElapsed(0.0f)
{
    // FUN_06296f40. TargetPointAge starts at FLT_MAX; +0x25a is padding, not written.
    MatrixSetIdentity(TurretFrame);
    TargetPoint.x = 0.0f;
    TargetPoint.y = 0.0f;
    TargetPoint.z = 0.0f;
    unsigned AgeBits = 0x7f7fffffu;
    memcpy(&TargetPointAge, &AgeBits, sizeof(TargetPointAge));
}

CEGun::CEGun(CEGun const& Other)
    : CELauncher(Other)
    , Yaw(Other.Yaw)
    , Pitch(Other.Pitch)
    , TurretFrame(Other.TurretFrame)
    , LockWatcher()
    , LockSubId(Other.LockSubId)
    , TargetPoint(Other.TargetPoint)
    , TargetPointAge(Other.TargetPointAge)
    , LockElapsed(Other.LockElapsed)
{
    // FUN_06296d00: the watcher starts empty and is linked into the same target's watcher
    // chain as Other's.
    CSimple* OtherTarget = DecodeLockWatchable(Other.LockWatcher.target());
    if (OtherTarget != nullptr)
        LockWatcher.set(EncodeLockWatchable(OtherTarget));
}

CEGun::~CEGun()
{
    // RVA 0x37070: Destroy() with CEGun's vtable still active, so its Disconnect reaches
    // CEGun::Disconnect (LoseJoints); ~CELauncher destroys again. LockWatcher's destructor then
    // unlinks the gun, in retail's order.
    CExternalEquip::Destroy();
}

CEGun* __cdecl CEGun::cast(CEquip* Equip)
{
    if (Equip == nullptr || Equip->GetType() != EquipTypeGun)
        return nullptr;
    return (CEGun*)(Equip);
}

CEGun const* __cdecl CEGun::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast(const_cast<CEquip*>(Equip));
}

bool CEGun::Activate(bool bOn)
{
    // RVA 0x370e0: a change dirties the owning ship's gun stats.
    bool WasActive = IsActive();
    bool Result = CEquip::Activate(bOn);
    if (WasActive != Result && Owner != nullptr &&
        (Owner->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
    {
        ((CShip*)Owner)->GunStatsDirty = 1;
    }
    return Result;
}

bool CEGun::is_guided() const
{
    // FUN_06296ce0; no null or class test.
    return MunitionArch()->Seeker != Archetype::Munition::SeekerNone;
}

FireResult CEGun::Fire(Vector const& Target)
{
    // FUN_062976e0
    return CELauncher::Fire(Target);
}

FireResult CEGun::CanFire(Vector const& Target) const
{
    // FUN_062976f0. No barrel-count or zero-length guard: a zero Delta gives a NaN dot, which
    // lands on FireMuzzleCone.
    FireResult Result = CELauncher::CanFire(Target);
    if (Result != FireOk)
        return Result;

    // Rounded to float before the FCOS.
    float const ConeAngle = MUZZLE_CONE_ANGLE * kDegToRad;
    Vector Muzzle = GetBarrelPosWS(0);
    Vector Delta;
    Delta.x = Target.x - Muzzle.x;
    Delta.y = Target.y - Muzzle.y;
    Delta.z = Target.z - Muzzle.z;
    // Length and inverse kept on the x87 stack; each component rounded to float.
    double const Len = sqrt((double)(Delta.z) * Delta.z + (double)(Delta.y) * Delta.y +
                            (double)(Delta.x) * Delta.x);
    double const Inv = 1.0 / Len;
    Vector Norm;
    Norm.x = (float)(Delta.x * Inv);
    Norm.y = (float)(Delta.y * Inv);
    Norm.z = (float)(Delta.z * Inv);

    Vector Dir = GetBarrelDirWS(0);
    // The dot is unrounded; only cos < dot is inside the cone.
    double const Dot = (double)(Norm.z) * Dir.z + (double)(Norm.y) * Dir.y +
                       (double)(Norm.x) * Dir.x;
    double const ConeCos = X87Cos((double)(ConeAngle));
    if (ConeCos < Dot)
    {
        if (Owner != nullptr &&
            (Owner->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
        {
            CShip* Ship = (CShip*)(Owner);
            if (Ship->is_cruise_active())
                return FireCruise;
        }
        return FireOk;
    }
    return FireMuzzleCone;
}

void CEGun::ConsumeFireResources()
{
    CELauncher::ConsumeFireResources();
    LockElapsed = 0.0f;
}

void CEGun::ComputeLaunchInfo_OneBarrel(ProjLaunchInfo& Out, int Barrel,
                                       Vector const& Target) const
{
    // FUN_06297870. LockTarget / LockSubId keep the caller's default unless the lock block at
    // the end writes them. No null or class tests on Projectile, Owner or Arch.
    Archetype::Munition const* const Munition = (Archetype::Munition const*)(Projectile);
    if (Munition->Seeker == Archetype::Munition::SeekerNone)
    {
        // Unguided: muzzle velocity plus the owner's, oriented along the total.
        Vector const OwnerVel = Owner->get_velocity();
        Vector DirVel;
        ComputeProjectilePosAndVelocity_OneBarrel(Out.Pos, DirVel, Barrel, Target);
        Out.Velocity.x = DirVel.x + OwnerVel.x;
        Out.Velocity.y = DirVel.y + OwnerVel.y;
        Out.Velocity.z = DirVel.z + OwnerVel.z;
        Out.Orient = LookMatrix(Out.Velocity);
    }
    else
    {
        CELauncher::ComputeLaunchInfo_OneBarrel(Out, Barrel, Target);
    }

    // Dispersion. Intermediates stay on the x87 stack where noted; each float cast is a store.
    float const Spread = GunArch()->DispersionAngle;
    if (Spread > 0.0f)
    {
        Vector& Vel = Out.Velocity;
        Vector const Rand = RandomVector(1.0f);
        Vector Perp;
        Perp.x = (float)((double)(Vel.z) * Rand.y - (double)(Rand.z) * Vel.y);
        Perp.y = (float)((double)(Rand.z) * Vel.x - (double)(Vel.z) * Rand.x);
        Perp.z = (float)((double)(Vel.y) * Rand.x - (double)(Vel.x) * Rand.y);
        float const Speed = (float)(sqrt(((double)(Vel.x) * Vel.x +
                                                     (double)(Vel.y) * Vel.y) +
                                                    (double)(Vel.z) * Vel.z));
        // 2^-15 is DAT_063997d0; FPTAN; no zero-length guard.
        int const RandValue = rand();
        double const Lean = X87Tan((double)(RandValue) * (1.0 / 32768.0) * Spread) * Speed;
        double const Scale = Lean / sqrt(((double)(Perp.z) * Perp.z +
                                          (double)(Perp.y) * Perp.y) +
                                         (double)(Perp.x) * Perp.x);
        // x keeps its unrounded product; y and z are rounded first.
        double const LeanX = Perp.x * Scale;
        float const LeanY = (float)(Perp.y * Scale);
        float const LeanZ = (float)(Perp.z * Scale);
        Vel.x = (float)(LeanX + Vel.x);
        Vel.y = LeanY + Vel.y;
        Vel.z = LeanZ + Vel.z;
        // Rescale to the original speed.
        double const Keep = (double)(Speed) /
                            sqrt(((double)(Vel.x) * Vel.x + (double)(Vel.y) * Vel.y) +
                                 (double)(Vel.z) * Vel.z);
        Vel.x = (float)(Keep * Vel.x);
        Vel.y = (float)(Keep * Vel.y);
        Vel.z = (float)(Keep * Vel.z);
        Out.Orient = LookMatrix(Vel);
    }

    // A lock seeker that has finished locking (NaN does not) hands over its target.
    if (Munition->Seeker == Archetype::Munition::SeekerLock)
    {
        double const Remaining = (double)(Munition->TimeToLock) - LockElapsed;
        if (Remaining <= 0.0)
        {
            CSimple* const Locked = DecodeLockWatchable(LockWatcher.target());
            if (Locked != nullptr)
            {
                Out.LockTarget = Locked;
                Out.LockSubId = LockSubId;
            }
        }
    }
}

bool CEGun::SetTarget(CSimple* Target, unsigned short SubId)
{
    // RVA 0x381e0: re-aims the turret at once when it has joints and no fresh target point.
    CSimple* Current = DecodeLockWatchable(LockWatcher.target());
    if (Target == Current && SubId == LockSubId)
        return false;
    LockElapsed = 0.0f;
    LockWatcher.set(EncodeLockWatchable(Target));
    LockSubId = SubId;
    if ((Yaw.EngineIndex != kInvalidEngineInstance ||
         Pitch.EngineIndex != kInvalidEngineInstance) &&
        DecodeLockWatchable(LockWatcher.target()) != nullptr && kTargetPointHold < TargetPointAge)
    {
        float YawRad = 0.0f;
        float PitchRad = 0.0f;
        ComputeAimAngles(*this, YawRad, PitchRad, true, TargetPoint);
        if (Yaw.EngineIndex != kInvalidEngineInstance &&
            (double)(kSeeTargetAngle) * 0.1f < fabs((double)(YawRad) - Yaw.Current))
            AnimDB::Add(&Yaw);
        Yaw.Desired = YawRad;
        if (Pitch.EngineIndex != kInvalidEngineInstance &&
            (double)(kSeeTargetAngle) * 0.1f < fabs((double)(PitchRad) - Pitch.Current))
            AnimDB::Add(&Pitch);
        Pitch.Desired = PitchRad;
    }
    return true;
}

void CEGun::GetTarget(CSimple*& OutTarget, unsigned short& OutSubId) const
{
    // RVA 0x38440
    OutTarget = DecodeLockWatchable(LockWatcher.target());
    OutSubId = LockSubId;
}

void CEGun::SetTargetPoint(Vector const& Point)
{
    // RVA 0x38470: stored relative to the turret origin.
    Vector Origin = GetTurretOrigin();
    TargetPoint.x = Point.x - Origin.x;
    TargetPoint.y = Point.y - Origin.y;
    TargetPoint.z = Point.z - Origin.z;
    TargetPointAge = 0.0f;
}

bool CEGun::Update(float Dt, unsigned Flags)
{
    // FUN_06297120: turret aim, then the seeker lock.
    if (!CELauncher::Update(Dt, Flags))
        return false;

    if (Yaw.EngineIndex != kInvalidEngineInstance ||
        Pitch.EngineIndex != kInvalidEngineInstance)
    {
        // FST stores float age, but the following comparison retains the unrounded sum.
        double const NewAge = (double)(Dt) + TargetPointAge;
        TargetPointAge = (float)(NewAge);
        float YawRad = -0.0f;
        float PitchRad = 0.0f;
        // NaN takes the lead branch.
        if (NewAge < kTargetPointHold)
        {
            ComputeAimAngles(*this, YawRad, PitchRad, false, TargetPoint);
        }
        else if (DecodeLockWatchable(LockWatcher.target()) != nullptr)
        {
            ComputeAimAngles(*this, YawRad, PitchRad, true, TargetPoint);
        }
        if (Yaw.EngineIndex != kInvalidEngineInstance &&
            (double)(kSeeTargetAngle) * 0.1f < fabs((double)(YawRad) - Yaw.Current))
            AnimDB::Add(&Yaw);
        Yaw.Desired = YawRad;
        if (Pitch.EngineIndex != kInvalidEngineInstance &&
            (double)(kSeeTargetAngle) * 0.1f < fabs((double)(PitchRad) - Pitch.Current))
            AnimDB::Add(&Pitch);
        Pitch.Desired = PitchRad;
    }

    if (DecodeLockWatchable(LockWatcher.target()) == nullptr)
        return true;
    if (!(MunitionArch()->TimeToLock > 0.0f))
        return true;

    bool bIncrement = false;
    if ((Yaw.EngineIndex == kInvalidEngineInstance &&
         Pitch.EngineIndex == kInvalidEngineInstance) ||
        !(kTargetPointHold < TargetPointAge))
    {
        Vector Lead;
        if (ComputeTgtLeadPosition(Lead) && CanSeeTargetPoint(Lead, kSeeTargetAngle))
            bIncrement = true;
    }
    else
    {
        // NaN resets.
        if (!JointWithin(Yaw, kSeeTargetAngle))
        {
            LockElapsed = 0.0f;
            return true;
        }
        // FUN_06297560: an unbound pitch is within.
        if (JointWithin(Pitch, kSeeTargetAngle))
            bIncrement = true;
    }

    if (bIncrement)
        LockElapsed = Dt + LockElapsed;
    else
        LockElapsed = 0.0f;
    return true;
}

// The joint walk inlined in CEGun::Connect and CEGun::LocateJoints. The root's relative block is
// dereferenced with no null test; a revolute root is the yaw joint, and the last revolute child
// is the pitch joint.
static void BindTurretJoints(CEGun& Gun, long Root)
{
    const InstanceIndex RootInst = Root;
    if (InstanceRelative(Root)->Type == kJointTypeRev)
        Gun.Yaw.Bind(&Gun, Root);

    long Child = DALib::Engine->GetRelatedInstance(RootInst, 1, InvalidInstanceIndex);
    while (Child != kInvalidEngineInstance)
    {
        FEngineJointBlock const* const ChildRel = InstanceRelative(Child);
        if (ChildRel != nullptr && ChildRel->Type == kJointTypeRev)
            Gun.Pitch.Bind(&Gun, Child);
        Child = DALib::Engine->GetRelatedInstance(RootInst, 1, Child);
    }
}

bool CEGun::Connect(char const* HpName)
{
    // FUN_062975b0
    if (!CELauncher::Connect(HpName))
        return false;
    BindTurretJoints(*this, GetRootIndex());
    ComputeTurretFrame();
    return true;
}

void CEGun::Disconnect()
{
    // RVA 0x37670
    LoseJoints();
    CELauncher::Disconnect();
}

void CEGun::ComputeTurretFrame()
{
    // FUN_06297c70 (reconstructed from the listing; Ghidra's output for it is unusable).
    // TurretFrame = Transpose(ConnOri) * [PitchAxisWS | YawAxisWS | PitchAxisWS x YawAxisWS],
    // i.e. the turret basis relative to the connector, not a world matrix. The two fallbacks
    // read different sources: unbound yaw uses the root's relative rest orientation, unbound
    // pitch the root's raw instance orientation. No null tests anywhere.
    long const Root = GetRootIndex();
    long const Conn = GetParentConnector(false);
    Matrix const ConnOri = *DALib::Engine->GetInstanceOrientation(Conn);

    // Bound yaw: the root's relative axis; unbound: (0,1,0) through the rest orientation. Either
    // way it then goes through ConnOri.
    Vector LocalYaw;
    if (Yaw.EngineIndex != kInvalidEngineInstance)
    {
        LocalYaw = InstanceRelative(Root)->Axis;
    }
    else
    {
        FEngineJointBlock const* const Rel = InstanceRelative(Root);
        Vector Up;
        Up.x = 0.0f;
        Up.y = 1.0f;
        Up.z = 0.0f;
        Acquire3DMathEngine()->TransformMatrix(LocalYaw, Rel->Orient, Up);
    }
    Vector YawAxisWS;
    Acquire3DMathEngine()->TransformMatrix(YawAxisWS, ConnOri, LocalYaw);

    // Bound pitch: its relative axis through its engine parent's orientation; unbound: (1,0,0)
    // through the root's orientation.
    Vector PitchAxisWS;
    if (Pitch.EngineIndex != kInvalidEngineInstance)
    {
        const InstanceIndex Parent = DALib::Engine->GetInstanceParent((long)Pitch.EngineIndex);
        Matrix const ParentOri = *DALib::Engine->GetInstanceOrientation(Parent);
        FEngineJointBlock const* const Rel = InstanceRelative(Pitch.EngineIndex);
        Acquire3DMathEngine()->TransformMatrix(PitchAxisWS, ParentOri, Rel->Axis);
    }
    else
    {
        Matrix const RootOri = *DALib::Engine->GetInstanceOrientation(Root);
        Vector Right;
        Right.x = 1.0f;
        Right.y = 0.0f;
        Right.z = 0.0f;
        Acquire3DMathEngine()->TransformMatrix(PitchAxisWS, RootOri, Right);
    }

    // Each component rounded once to float.
    Vector CrossAxisWS;
    CrossAxisWS.x = (float)((double)(YawAxisWS.z) * PitchAxisWS.y -
                                       (double)(PitchAxisWS.z) * YawAxisWS.y);
    CrossAxisWS.y = (float)((double)(PitchAxisWS.z) * YawAxisWS.x -
                                       (double)(YawAxisWS.z) * PitchAxisWS.x);
    CrossAxisWS.z = (float)((double)(YawAxisWS.y) * PitchAxisWS.x -
                                       (double)(PitchAxisWS.y) * YawAxisWS.x);

    // FUN_06284f70 (the 9-float Matrix ctor) fed ConnOri transposed.
    Matrix const ConnOriT(ConnOri.E[0][0], ConnOri.E[1][0], ConnOri.E[2][0],
                          ConnOri.E[0][1], ConnOri.E[1][1], ConnOri.E[2][1],
                          ConnOri.E[0][2], ConnOri.E[1][2], ConnOri.E[2][2]);
    Matrix const Basis(PitchAxisWS.x, YawAxisWS.x, CrossAxisWS.x,
                       PitchAxisWS.y, YawAxisWS.y, CrossAxisWS.y,
                       PitchAxisWS.z, YawAxisWS.z, CrossAxisWS.z);
    Matrix Frame;
    Acquire3DMathEngine()->MulMatrix(Frame, ConnOriT, Basis);
    TurretFrame = Frame;
}

void CEGun::DecomposeJointMotion(Vector const& Dir, float& OutYaw, float& OutPitch) const
{
    // FUN_06297fc0: Dir in the transposed (ConnOri * TurretFrame) frame, then yaw = atan2(x, -z)
    // and pitch = atan(y / hypot(x, z)) through x87 FPATAN (math engine DAT_063fc184). No null
    // tests.
    long Conn = GetParentConnector(false);
    const Matrix* Ori = DALib::Engine->GetInstanceOrientation(Conn);
    Matrix ConnOri(Ori->E[0][0], Ori->E[0][1], Ori->E[0][2], Ori->E[1][0], Ori->E[1][1], Ori->E[1][2], Ori->E[2][0], Ori->E[2][1], Ori->E[2][2]);

    I3DMathEngine* Math = Acquire3DMathEngine();
    Matrix Combined;
    Math->MulMatrix(Combined, ConnOri, TurretFrame);
    Matrix Inv(Combined.E[0][0], Combined.E[1][0], Combined.E[2][0],
               Combined.E[0][1], Combined.E[1][1], Combined.E[2][1],
               Combined.E[0][2], Combined.E[1][2], Combined.E[2][2]);

    Math = Acquire3DMathEngine();
    Vector Local;
    Math->TransformMatrix(Local, Inv, Dir);

    OutYaw = X87Atan2F((double)(Local.x), -(double)(Local.z));
    double Horiz = sqrt((double)(Local.z) * Local.z + (double)(Local.x) * Local.x);
    OutPitch = X87AtanF((double)(Local.y) / Horiz);
}

void CEGun::LocateJoints()
{
    // FUN_062980d0: Connect's joint walk without the CELauncher::Connect gate. Referenced only
    // by its export.
    BindTurretJoints(*this, GetRootIndex());
    ComputeTurretFrame();
}

Vector CEGun::GetTurretOrigin() const
{
    // FUN_06297b20. No null tests.
    if (Pitch.EngineIndex != kInvalidEngineInstance)
    {
        // The pitch joint's world position minus its oriented relative child position.
        const InstanceIndex PitchInst = (long)Pitch.EngineIndex;
        Matrix const PitchOri = *DALib::Engine->GetInstanceOrientation(PitchInst);
        Vector const PitchPos = *DALib::Engine->GetInstancePosition(PitchInst);
        FEngineJointBlock const* const Rel = InstanceRelative(Pitch.EngineIndex);
        Vector Offset;
        Acquire3DMathEngine()->TransformMatrix(Offset, PitchOri, Rel->ChildPos);
        Vector Origin;
        Origin.x = PitchPos.x - Offset.x;
        Origin.y = PitchPos.y - Offset.y;
        Origin.z = PitchPos.z - Offset.z;
        return Origin;
    }
    if (Yaw.EngineIndex != kInvalidEngineInstance)
        return *DALib::Engine->GetInstancePosition((long)Yaw.EngineIndex);
    return *DALib::Engine->GetInstancePosition(GetRootIndex());
}

bool CEGun::IsJointInMotion() const
{
    // FUN_06297b00. Despite the export's name, this is true when both joints have settled.
    return Yaw.bSettled != 0 && Pitch.bSettled != 0;
}

bool CEGun::IsMovable() const
{
    // RVA 0x37c50
    return !(Yaw.EngineIndex == kInvalidEngineInstance && Pitch.EngineIndex == kInvalidEngineInstance);
}

float CEGun::GetMunitionRange() const
{
    // FUN_06298e70. No null or class tests.
    if (IsDestroyed())
        return 0.0f;
    Archetype::Munition const* const Proj = (Archetype::Munition const*)(Projectile);
    if (Proj->Seeker == Archetype::Munition::SeekerLock && Proj->SeekerRange > 0.0f && Proj->SeekerFovCos < 1.0f)
        return Proj->SeekerRange;
    return GunArch()->get_munition_range();
}

float CEGun::GetSecondsTillLock() const
{
    // FUN_06298ee0: clamped at 0 (NaN passes through). No null or class tests.
    Archetype::Munition const* const Proj = (Archetype::Munition const*)(Projectile);
    if (Proj->Seeker != Archetype::Munition::SeekerLock)
        return 0.0f;
    double const Remaining = (double)(Proj->TimeToLock) - LockElapsed;
    if (0.0 > Remaining)
        return 0.0f;
    return (float)(Remaining);
}

bool CEGun::CanSeeTargetPoint(Vector const& Point, float Angle) const
{
    // FUN_062984d0. No zero-length guard: a zero Delta gives a NaN dot, which is false.
    // x and y stay unrounded; z is rounded but its unrounded value feeds one factor of z*z.
    Vector Muzzle = GetBarrelPosWS(0);
    double const Dx = (double)(Point.x) - Muzzle.x;
    double const Dy = (double)(Point.y) - Muzzle.y;
    double const DzRetained = (double)(Point.z) - Muzzle.z;
    float const Dz = (float)(DzRetained);
    double const Len = sqrt((DzRetained * Dz + Dx * Dx) + Dy * Dy);
    float const Inv = (float)(1.0 / Len);
    float const Nx = (float)(Dx * Inv);
    float const Ny = (float)(Dy * Inv);
    float const Nz = (float)((double)(Dz) * Inv);
    // Only cos < dot is inside.
    Vector Dir = GetBarrelDirWS(0);
    double const Dot = (double)(Dir.z) * Nz + (double)(Dir.y) * Ny +
                       (double)(Nx) * Dir.x;
    double const ConeCos = X87Cos((double)(Angle));
    return ConeCos < Dot;
}

bool CEGun::CanSeeTargetObject(float Angle) const
{
    // FUN_06298590
    if (DecodeLockWatchable(LockWatcher.target()) == nullptr)
        return false;

    // A turret without a fresh target point answers from its joints alone (pi is DAT_0639d208).
    if ((Pitch.EngineIndex != kInvalidEngineInstance || Yaw.EngineIndex != kInvalidEngineInstance) &&
        kTargetPointHold < TargetPointAge)
    {
        if (!JointWithin(Yaw, Angle))
            return false;
        if (!JointWithin(Pitch, Angle))
            return false;
        return true;
    }

    // CanSeeTargetPoint is inlined here in retail.
    Vector Lead;
    if (!ComputeTgtLeadPosition(Lead))
        return false;
    return CanSeeTargetPoint(Lead, Angle);
}

namespace
{
    // One joint's leg of CEGun::CanPointAt (inlined twice). The window [-Angle, Angle] is widened
    // by a bound joint's limits (no null test), the low end rounded to float, then normalised by
    // steps of 2pi (DAT_0639c9e8) so Lo <= 0 <= Hi. NaN fails.
    bool JointWindowContains(int EngineIndex, float Angle, float NegAngle, float Value)
    {
        float Lo = NegAngle;
        double Hi = (double)(Angle);
        if (EngineIndex != kInvalidEngineInstance)
        {
            FEngineJointBlock const* const Rel = InstanceRelative(EngineIndex);
            Lo = (float)((double)NegAngle + Rel->Min);
            Hi = (double)Angle + Rel->Max;
        }
        double const TwoPi = (double)(kGunTwoPi);
        double LoX = (double)(Lo);
        while (LoX > 0.0)
            LoX -= TwoPi;
        while (Hi < 0.0)
            Hi += TwoPi;
        if (!(LoX <= (double)(Value)))
            return false;
        if (!((double)(Value) <= Hi))
            return false;
        return true;
    }
}

bool CEGun::CanPointAt(Vector const& Point, float Angle) const
{
    // FUN_06298860
    if (IsDestroyed())
        return false;

    Vector Origin = GetTurretOrigin();
    Vector Dir;
    Dir.x = Point.x - Origin.x;
    Dir.y = Point.y - Origin.y;
    Dir.z = Point.z - Origin.z;
    // DecomposeJointMotion is inlined here in retail.
    float YawRad = 0.0f;
    float PitchRad = 0.0f;
    DecomposeJointMotion(Dir, YawRad, PitchRad);

    // Yaw is tested negated, pitch as is.
    float const NegAngle = -Angle;
    if (!JointWindowContains(Yaw.EngineIndex, Angle, NegAngle, -YawRad))
        return false;
    if (!JointWindowContains(Pitch.EngineIndex, Angle, NegAngle, PitchRad))
        return false;
    return true;
}

bool CEGun::GetTargetObjectPos(Vector& Out) const
{
    // FUN_06298750. Out is untouched on false.
    CSimple* Target = DecodeLockWatchable(LockWatcher.target());
    if (Target == nullptr)
        return false;
    if (LockSubId != SUBOBJ_ID_NONE &&
        ((unsigned)(Target->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj)
    {
        CEqObj const* EqTarget = (CEqObj const*)(Target);
        if (EqTarget->get_sub_obj_center_of_mass(LockSubId, Out))
            return true;
    }
    Out = Target->get_center_of_mass();
    return true;
}

bool CEGun::ComputeTimeToTgt(Vector const& RelPos, Vector const& RelVel, float Speed,
                             float& OutTime)
{
    // FUN_06298f40. Only the quadratic coefficient and the roots are rounded to float; the rest
    // stays on the x87 stack.
    double const Dot = (double)(RelPos.y) * RelVel.y +
                       (double)(RelPos.z) * RelVel.z +
                       (double)(RelPos.x) * RelVel.x;
    float const Vel2 = (float)((double)(RelVel.x) * RelVel.x +
                                         (double)(RelVel.y) * RelVel.y +
                                         (double)(RelVel.z) * RelVel.z -
                                         (double)(Speed) * Speed);
    double const Pos2 = (double)(RelPos.x) * RelPos.x +
                        (double)(RelPos.y) * RelPos.y +
                        (double)(RelPos.z) * RelPos.z;
    double const Disc = Dot * Dot - Pos2 * Vel2;
    if (!(Disc >= 0.0))
        return false;
    double const Root = sqrt(Disc);
    double const Inv = 1.0 / Vel2; // retail has no zero-denominator guard
    float const T1 = (float)((Root - Dot) * Inv);
    float const T2 = (float)((-Dot - Root) * Inv);
    bool const bFirstPositive = T1 > 0.0f;
    bool const bSecondPositive = T2 > 0.0f;
    if (!bFirstPositive && !bSecondPositive)
        return false;
    // Both positive: the smaller, T2 on a tie or NaN.
    OutTime = bFirstPositive && (!bSecondPositive || T1 < T2) ? T1 : T2;
    return true;
}

bool CEGun::ComputeTgtLeadPosition(Vector& Out) const
{
    // FUN_06298b10. Out is written only on the true paths.
    CSimple* Target = DecodeLockWatchable(LockWatcher.target());
    if (Target == nullptr)
        return false;
    if (IsDestroyed())
        return false;

    Vector TgtPos;
    Vector TgtVel;
    if (LockSubId != SUBOBJ_ID_NONE &&
        ((unsigned)(Target->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj)
    {
        CEqObj const* EqTarget = (CEqObj const*)(Target);
        if (!EqTarget->get_sub_obj_center_of_mass(LockSubId, TgtPos))
            TgtPos = Target->get_center_of_mass();
        if (!EqTarget->get_sub_obj_velocity(LockSubId, TgtVel))
            TgtVel = Target->get_velocity();
    }
    else
    {
        TgtPos = Target->get_center_of_mass();
        TgtVel = Target->get_velocity();
    }

    // The shooter is barrel 0's object (looked up unconditionally), else the owner.
    CObject* Shooter = (CObject*)(DALib::Engine->GetInstanceUserData(
        (long)(Barrels[0].EngineIndex)));
    if (Shooter == nullptr)
        Shooter = Owner;
    Vector RelPos;
    RelPos.x = TgtPos.x - Shooter->WorldPosition.x;
    RelPos.y = TgtPos.y - Shooter->WorldPosition.y;
    RelPos.z = TgtPos.z - Shooter->WorldPosition.z;
    Vector ShootVel = Shooter->get_velocity();
    Vector RelVel;
    RelVel.x = TgtVel.x - ShootVel.x;
    RelVel.y = TgtVel.y - ShootVel.y;
    RelVel.z = TgtVel.z - ShootVel.z;

    // A guided munition leads as if instantaneous. No class or null test.
    float Speed = GunArch()->MuzzleVelocity;
    if (MunitionArch()->Seeker != Archetype::Munition::SeekerNone)
        Speed = kSeekerLeadSpeed;

    float Time = 0.0f;
    if (!ComputeTimeToTgt(RelPos, RelVel, Speed, Time))
    {
        Out = TgtPos;
        return true;
    }
    // Retail leads with the relative velocity (target minus shooter); product unrounded.
    Out.x = (float)((double)(RelVel.x) * Time + TgtPos.x);
    Out.y = (float)((double)(RelVel.y) * Time + TgtPos.y);
    Out.z = (float)((double)(RelVel.z) * Time + TgtPos.z);
    return true;
}

// Exported constant (ordinal 2046, .rdata 0x0639cedc).
float const CEGun::TARGET_POINT_VALID_TIME = 2.0f;

Archetype::Gun const* CEGun::GunArch() const
{
    return (Archetype::Gun const*)(Arch);
}

Archetype::Munition const* CEGun::MunitionArch() const
{
    // RVA 0x369c0 (ICF-shared).
    return (Archetype::Munition const*)(Projectile);
}

void CEGun::LoseJoints()
{
    // FUN_06298170: JointChannel's ctor body inlined per joint (the vftable is left alone).
    if (Yaw.EngineIndex != kInvalidEngineInstance)
        AnimDB::Rem(&Yaw);
    Yaw.Owner = nullptr;
    Yaw.EngineIndex = kInvalidEngineInstance;
    Yaw.bLimited = 1;
    Yaw.Desired = 0.0f;
    Yaw.Current = 0.0f;
    Yaw.bSettled = 0;
    if (Pitch.EngineIndex != kInvalidEngineInstance)
        AnimDB::Rem(&Pitch);
    Pitch.Owner = nullptr;
    Pitch.EngineIndex = kInvalidEngineInstance;
    Pitch.Desired = 0.0f;
    Pitch.Current = 0.0f;
    Pitch.bSettled = 0;
    Pitch.bLimited = 1;
}
