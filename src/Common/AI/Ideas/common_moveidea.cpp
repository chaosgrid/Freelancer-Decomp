// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll MoveIdea (ctor 0x062d4e60). See src/include/common_idea.h.

#include "common_idea.h"
#include "AI/Ideas/common_idea_internal.h"
#include "common_aisteer.h"   // BearingAngleToPoint, GetIfacePositionOrZero
#include "common_behavior.h"
#include "common_cequip.h"    // ThrustEquipType (IObjInspect::get_speed's 2nd argument)
#include "common_csys.h"      // Matrix / Transform
#include "common_iface.h"     // IObjInspect / IObjRW / IBehaviorManager / ObjRwFromWatchable
#include "common_pathfind.h"  // PathfindManager (IBehaviorManager::Pathfind)
#include "common_systemmanager.h" // SystemManager / ThrustManager (BaseIdea::Handle)
#include "x86math.h"          // I3DMathEngine::TransformMatrix

#include <cmath>
#include <cstring>

using namespace CmnIdea;

namespace
{
    // Retail's normalize-in-place tail, shared by FUN_062d5890 and FUN_062d58f0:
    // reciprocal of the length (1.0f is DAT_0639a1d0) kept in double, each component
    // rounded once. No zero-length guard in retail, and none added here.
    void NormalizeInPlace(Vector& V)
    {
        double const Len = std::sqrt((static_cast<double>(V.x) * V.x + static_cast<double>(V.y) * V.y) +
                                     static_cast<double>(V.z) * V.z);
        double const InvLen = 1.0 / Len;
        V.x = static_cast<float>(InvLen * V.x);
        V.y = static_cast<float>(InvLen * V.y);
        V.z = static_cast<float>(InvLen * V.z);
    }

    // MoveIdea::Update's inline speed of a get_velocity() result: sum order x, z, y,
    // unrounded.
    double VelocityMagnitude(Vector const& V)
    {
        return std::sqrt((static_cast<double>(V.x) * V.x + static_cast<double>(V.z) * V.z)
                         + static_cast<double>(V.y) * V.y);
    }
}

// ----------------------------------------------------------------------------
// MoveIdea
// ----------------------------------------------------------------------------

// FUN_062d4e60 (no Ghidra decompile). Field citations are in common_idea.h.
MoveIdea::MoveIdea()
    : BaseIdea()
    , Watcher()
    , Position{}
    , Direction{}
    , Mode(0)
    , MaxThrustBearing(3.14159274f)      // 0x40490fdb
    , ArrivalRange(200.0f)
    , Throttle(1.0f)
    , StopStyle(kMoveStopThrottleCut)
    , TargetKind48(kMoveTargetNone)
    , Offset4c{}
    , Field58(0)
    , bKillEngineOffBearing(1)
    , Pad5a{}
    , MaxSpeed5c(0.0f)
    , MaxAngularSpeedXY60(0.0f)
    , EmbeddedStop()
{
}

// Own vtable slot 0 override -- LAB_062d4f20 (RET 0xc, no Ghidra decompile).
bool MoveIdea::SetContext(void* InObject, void* InHandle, void* InContext)
{
    if (InObject == nullptr || InHandle == nullptr)
    {
        return false;
    }

    Handle = (SystemManager*)InHandle;
    Object = InObject;
    Context = InContext;
    Attached = 1;

    // Slot 0xf0, get_angular_speed_XY(Speed = 0, MaxSpeed = 1.0f): the opposite push
    // order from the slot-0xd8 call below.
    IObjInspect const* const Inspect = ObjInspectOf(InObject);
    MaxAngularSpeedXY60 = Inspect->get_angular_speed_XY(0.0f, 1.0f);

    // Slot 0xd8, get_speed(1.0f, ThrustEquipEngine).
    MaxSpeed5c = Inspect->get_speed(1.0f, ThrustEquipEngine);

    // Retail reloads the three pointers from this object's fields, not the args.
    EmbeddedStop.SetContext(Object, Handle, Context);

    return true;
}

// Own vtable slot 1 override -- LAB_062d4fa0 (RET 4, no Ghidra decompile).
// Returns 3 (no Object), 1 (arrived, after Commit()), 2 (the cruise gate refused)
// or 0. Retail pins EBP to the literal 2 at .text:062d51dc and uses it both as an
// immediate argument and as that return value.
int MoveIdea::Update(float DeltaTime)
{
    if (Object == nullptr)
    {
        return 3;
    }

    // Retail's result slot only ever holds 0 (the store at 062d5676 is dead).
    int const Result = 0;

    float BestDistSq = BitsToFloat(0x7effffffu);

    // FAiTarget::RefreshPos, inlined by retail. Neither Handle nor the +0x84 owner
    // interface is tested.
    FAiTarget* const Nav = &Handle->SelfTarget;
    Nav->RefreshPos();

    // Rotate Offset4c out of the owner's frame with the 3DMathEngine singleton
    // (DAT_063fc974, never tested; slot 0x2c TransformMatrix), then add the cached
    // nav centre.
    Transform const& Xform = Handle->SelfTarget.Iface->get_transform();
    Vector Rotated;
    Acquire3DMathEngine()->TransformMatrix(Rotated, Xform.Rotation, Offset4c);

    Vector const& NavCentre = Nav->CachedPos;
    Vector Lead;
    Lead.x = Rotated.x + NavCentre.x;
    Lead.y = Rotated.y + NavCentre.y;
    Lead.z = Rotated.z + NavCentre.z;

    // Only target kinds 1 and 2 produce a distance; kinds 0 and 3 leave the
    // 0x7effffff sentinel. Retail spells the watcher downcast out twice; hoisted.
    Watchable const* const RawBlock = Watcher.target();
    IObjRW const* const WatchedOuter = ObjRwFromWatchable(const_cast<Watchable*>(RawBlock));

    if (TargetKind48 == kMoveTargetObject)
    {
        // FUN_062e4bb0; the unrounded clearance squared, rounded once.
        double const Clearance = NavInterceptClearanceOfIface(Nav, WatchedOuter);
        BestDistSq = static_cast<float>(Clearance * Clearance);
    }
    else if (TargetKind48 == kMoveTargetPoint)
    {
        // (dz*dz + dy*dy) + dx*dx in double; rounded once.
        double const Dx = static_cast<double>(Lead.x) - Position.x;
        double const Dy = static_cast<double>(Lead.y) - Position.y;
        double const Dz = static_cast<double>(Lead.z) - Position.z;
        BestDistSq = static_cast<float>((Dz * Dz + Dy * Dy) + Dx * Dx);
    }

    // Commit only on strictly-less (unordered continues); Range*Range is unrounded.
    float const Range = ArrivalRange;
    if (BestDistSq < static_cast<double>(Range) * Range)
    {
        Commit();
        return 1;
    }

    // The steering body. Pathfind+0x214 is never tested.
    unsigned char* const SlotBlock = SlotArrayOf(Context) + 0x214;

    bool const bSlotOverride = ((*reinterpret_cast<unsigned int const*>(SlotBlock) >> 1) & 1u) != 0;

    Vector Goal{ 0.0f, 0.0f, 0.0f };
    if (bSlotOverride)
    {
        Goal = *reinterpret_cast<Vector const*>(SlotBlock + 0x18);
    }
    else if (TargetKind48 == kMoveTargetObject)
    {
        GetIfacePositionOrZero(&Goal, WatchedOuter);
    }
    else if (TargetKind48 == kMoveTargetPoint)
    {
        Goal = Position;
    }
    else if (TargetKind48 == kMoveTargetDirection)
    {
        // A point 10 km (DAT_0639f7a4 == 10000.0f) down the requested direction.
        // z's product is rounded to float first; x's and y's stay unrounded until
        // the Lead add.
        double const Far = 10000.0;
        float const FarZ = static_cast<float>(static_cast<double>(Direction.z) * Far);
        Goal.x = static_cast<float>(static_cast<double>(Direction.x) * Far + Lead.x);
        Goal.y = static_cast<float>(static_cast<double>(Direction.y) * Far + Lead.y);
        Goal.z = static_cast<float>(static_cast<double>(FarZ) + Lead.z);
    }

    // Leaving cruise: threshold 3.0f (DAT_063a0408) * 300.0 (DAT_063a0470), squared,
    // against BestDistSq.
    if (Mode == kMoveModeCruise && StopStyle != kMoveStopFlyThrough)
    {
        double const Threshold = 3.0 * 300.0;
        if (Threshold * Threshold > static_cast<double>(BestDistSq))
        {
            Mode = kMoveModeEngine;
        }
    }

    // StopStyle selects the stopping-speed query; StopStyle >= 2 leaves 0.0f
    // (DAT_06399408).
    float Speed = 0.0f;
    if (StopStyle == kMoveStopThrottleCut || StopStyle == kMoveStopIdea)
    {
        Vector const Velocity = Handle->SelfTarget.Iface->get_velocity();
        float const Magnitude = static_cast<float>(VelocityMagnitude(Velocity));

        if (StopStyle == kMoveStopThrottleCut)
        {
            Speed = Behavior_GetCoastDistance(Object, Magnitude);   // FUN_062e5ab0
        }
        else
        {
            // FUN_062e58f0(Object, -1.0f, Magnitude, 0.0f, Type).
            ThrustEquipType const Type = (Mode == kMoveModeTradelane) ? ThrustEquipTradelane : ThrustEquipEngine;
            Speed = Behavior_GetDistanceTravelled(ObjInspectOf(Object), -1.0f, Magnitude, 0.0f, Type);
        }
    }

    // DAT_063a0468 == 0.8f; the product is never stored.
    double const ScaledSpeed = static_cast<double>(Speed) * 0.8f;

    // Read after the possible `Mode = 0` above; the slot override can force 0.
    int ModeEff = Mode;
    if (bSlotOverride)
    {
        if (*reinterpret_cast<int const*>(SlotBlock + 0x2c) == 1)
        {
            ModeEff = 0;
        }
        else if (*reinterpret_cast<float const*>(SlotBlock + 0x28) < 1.0f)
        {
            ModeEff = 0;
        }
    }

    // Gate and its square unrounded; unordered counts as not-close.
    double const Gate = ScaledSpeed + ArrivalRange;
    bool const bClose = (static_cast<double>(BestDistSq) < Gate * Gate);

    // Each ModeEff arm makes its own get_velocity() call, as retail does. Every
    // Thrust receiver is `[this+8] + 0x18`, recomputed per call and never tested.
    if (ModeEff == 2)
    {
        if (bClose)
        {
            Vector const Velocity = Handle->SelfTarget.Iface->get_velocity();
            // Against the double 0.1 (DAT_063a0460); unordered skips.
            if (VelocityMagnitude(Velocity) > 0.1)
            {
                Handle->Thrust.engage_cruise(false);

                if (StopStyle == kMoveStopThrottleCut)
                {
                    Handle->Thrust.SetThrottle(0.0f, kThrottleModeCruise);
                }
                else if (StopStyle == kMoveStopIdea)
                {
                    EmbeddedStop.Update(DeltaTime);
                }
                return Result;
            }
        }

        // Unordered counts as outside the bearing.
        double const Bearing = BearingAngleToPoint(Object, Goal);
        if (Bearing < MaxThrustBearing)
        {
            Handle->Thrust.SetThrottle(Throttle, kThrottleModeCruise);
        }
        else
        {
            Handle->Thrust.engage_cruise(false);
        }
        return Result;
    }

    if (ModeEff == 1)
    {
        if (bClose)
        {
            Vector const Velocity = Handle->SelfTarget.Iface->get_velocity();
            if (VelocityMagnitude(Velocity) > 0.1)                   // double 0.1
            {
                Vector const ZeroVector{ 0.0f, 0.0f, 0.0f };
                Handle->Thrust.SetNudgeDirection(ZeroVector);
                return Result;
            }
        }

        // Nudge along (Goal - Lead), normalized and scaled; each step rounds.
        Vector const Diff{ Goal.x - Lead.x, Goal.y - Lead.y, Goal.z - Lead.z };
        Vector Dir;
        VecNormalize3(&Diff, &Dir);
        Vector const ScaledDir{ Dir.x * Throttle,
                                Dir.y * Throttle,
                                Dir.z * Throttle };
        Handle->Thrust.SetNudgeDirection(ScaledDir);
        return Result;
    }

    if (ModeEff != 0)
    {
        return Result;
    }

    if (bClose)
    {
        Vector const Velocity = Handle->SelfTarget.Iface->get_velocity();
        // This arm compares against the float 0.1f (DAT_0639d20c), not the double
        // the other two arms use.
        if (VelocityMagnitude(Velocity) > static_cast<double>(0.1f))
        {
            if (StopStyle == kMoveStopThrottleCut)
            {
                Handle->Thrust.SetThrottle(0.0f, kThrottleModeEngine);
                Handle->Thrust.SetEngineActive(true);
            }
            else if (StopStyle == kMoveStopIdea)
            {
                EmbeddedStop.Update(DeltaTime);
            }
            return Result;
        }
    }

    // Only strictly-less proceeds to the speed path; unordered counts as off-bearing.
    double const Bearing = BearingAngleToPoint(Object, Goal);
    if (!(Bearing < MaxThrustBearing))
    {
        Handle->Thrust.SetThrottle(0.0f, kThrottleModeEngine);
        Handle->Thrust.SetEngineActive(bKillEngineOffBearing == 0);
        return Result;
    }

    // The smaller of Throttle and one candidate, chosen by the slot block's whole
    // first dword (not the bit-1 test above).
    float Speed2 = Throttle;
    if (*reinterpret_cast<int const*>(SlotBlock) != 0)
    {
        float const SlotValue = *reinterpret_cast<float const*>(SlotBlock + 0x28);
        if (SlotValue < Speed2)
        {
            Speed2 = SlotValue;
        }
    }
    else
    {
        // FUN_062e5c00; the unrounded result is compared before it is stored.
        double const Cost = EstimateTurnCircleApproachCost(
            const_cast<IObjInspect*>(ObjInspectOf(Object)), Goal,
            MaxAngularSpeedXY60, MaxSpeed5c);
        if (Cost < static_cast<double>(Speed2))
        {
            Speed2 = static_cast<float>(Cost);
        }
    }

    // The one call whose result retail tests: false (the gate refused) returns 2.
    if (Handle->Thrust.SetThrottle(Speed2, kThrottleModeEngine))
    {
        return Result;
    }
    return 2;
}

// Own vtable slot 4 override -- LAB_062d56b0. No null test on Handle or Context.
void MoveIdea::Commit()
{
    Vector const ZeroVector{ 0.0f, 0.0f, 0.0f };
    Handle->Thrust.SetNudgeDirection(ZeroVector);

    // Signed range test on StopStyle.
    if (Mode != kMoveModeTradelane && StopStyle >= kMoveStopThrottleCut && StopStyle <= kMoveStopIdea)
    {
        Handle->Thrust.SetThrottle(0.0f, kThrottleModeEngine);
        Handle->Thrust.SetThrottle(0.0f, kThrottleModeCruise);
    }

    // Unconditional: zero Pathfind's +0x64 dword and +0x200..+0x208 Vector,
    // re-reading Context->+0xc each time.
    *reinterpret_cast<int*>(SlotArrayOf(Context) + 0x64) = 0;
    Vector* const Dest = reinterpret_cast<Vector*>(SlotArrayOf(Context) + 0x200);
    Dest->x = 0.0f;
    Dest->y = 0.0f;
    Dest->z = 0.0f;
}

// ---------------------------------------------------------------------------
// MoveIdea's non-virtual setter family. Most share their address with a
// byte-identical method of an unrelated class -- see common_idea.h's "/OPT:ICF
// trap" section for the fold table and the evidence that these are MoveIdea's.
// ---------------------------------------------------------------------------

// FUN_062d5840, RET 4.
void MoveIdea::SetPosition(Vector const& Pos)
{
    TargetKind48 = kMoveTargetPoint;
    Position = Pos;

    // Mirror into Pathfind+0x200, re-read from Position (not Pos).
    unsigned char* const Slots = SlotArrayOf(Context);
    Vector* const Dest = reinterpret_cast<Vector*>(Slots + 0x200);
    Dest->x = Position.x;
    Dest->y = Position.y;
    Dest->z = Position.z;
}

// FUN_062d5890, RET 4.
void MoveIdea::SetDirection(Vector const& Dir)
{
    TargetKind48 = kMoveTargetDirection;
    Direction = Dir;
    NormalizeInPlace(Direction);
}

// FUN_062d58f0, RET 4. Direction = -(Src's +0x08/+0x14/+0x20), i.e. minus column 2
// of a 3x3 basis.
void MoveIdea::SetDirectionFrom(Matrix const* Src)
{

    TargetKind48 = kMoveTargetDirection;

    // All three loads happen before any store (matters only if Src aliased
    // Direction).
    float const SrcX = Src->E[0][2];
    float const SrcY = Src->E[1][2];
    float const SrcZ = Src->E[2][2];
    Direction.x = -SrcX;
    Direction.y = -SrcY;
    Direction.z = -SrcZ;

    NormalizeInPlace(Direction);
}

// FUN_062d5760, RET 4 (Ghidra's decompile is corrupt; ported from the listing).
void MoveIdea::SetWatchObject(void const* Object2)
{
    // Retail captures the slot array pointer before the null test.
    unsigned char* const Slots = SlotArrayOf(Context);

    if (Object2 == nullptr)
    {
        TargetKind48 = kMoveTargetNone;
        Watcher.set(nullptr);
        *reinterpret_cast<int*>(SlotArrayOf(Context) + 0x64) = 0;
        Vector* const Dest = reinterpret_cast<Vector*>(Slots + 0x200);
        Dest->x = 0.0f;
        Dest->y = 0.0f;
        Dest->z = 0.0f;
        return;
    }

    // IObjRW -> Watchable adjustment.
    TargetKind48 = kMoveTargetObject;
    Watcher.set(reinterpret_cast<Watchable const*>(
        static_cast<unsigned char const*>(Object2) + 8));

    // Both mirrors re-read the watcher (not Object2), untested. The id goes through
    // a fresh Context->+0xc, the position through the captured one.
    IObjRW* const Outer = ObjRwFromWatchable(const_cast<Watchable*>(Watcher.target()));
    *reinterpret_cast<int*>(SlotArrayOf(Context) + 0x64) = static_cast<int>(Outer->get_id());
    IObjRW* const OuterAgain = ObjRwFromWatchable(const_cast<Watchable*>(Watcher.target()));
    Vector const& Pos = OuterAgain->get_position();
    Vector* const PosDest = reinterpret_cast<Vector*>(Slots + 0x200);
    PosDest->x = Pos.x;
    PosDest->y = Pos.y;
    PosDest->z = Pos.z;
}

// FUN_062d5980, RET 4. Handle + 0x18 is never tested.
void MoveIdea::SetMode(int NewMode)
{
    int const Old = Mode;
    if (Old != NewMode)
    {
        ThrustManager* const Thrust = &Handle->Thrust;

        if (Old == kMoveModeCruise)
        {
            Thrust->SetThrottle(0.0f, kThrottleModeCruise);
            Mode = NewMode;
            return;
        }

        if (Old == kMoveModeEngine && NewMode != kMoveModeCruise)
        {
            Thrust->SetThrottle(0.0f, kThrottleModeEngine);
            Mode = NewMode;
            return;
        }

        // Only Old == 1 reaches the zero nudge, then falls through to the tail.
        if (Old == kMoveModeTradelane)
        {
            Vector const ZeroVector{ 0.0f, 0.0f, 0.0f };
            Thrust->SetNudgeDirection(ZeroVector);
        }
    }

    Mode = NewMode;
}

// Folded at .text:063275f0, whose other (real, correctly-named) owner is
// TextRenderContext::set_origin_h -- `MOV EAX,[ESP+4]; MOV [ECX+0x38],EAX;
// RET 4`.
void MoveIdea::SetAngle38(float Value)
{
    MaxThrustBearing = Value;
}

// Folded at .text:062a66b0, shared with DeviceTRC::bind_to_dc AND
// CAsteroidField::set_system -- `MOV [ECX+0x3c],EAX; RET 4`.
void MoveIdea::SetRange3c(float Value)
{
    ArrivalRange = Value;
}

// Folded at .text:062a33e0, shared with CmnAsteroid::CAsteroidCube::set_state
// -- `MOV [ECX+0x40],EAX; RET 4`.
void MoveIdea::SetScale40(float Value)
{
    Throttle = Value;
}

// FUN_062d5a20: Throttle = FUN_062e5b70(Object, Value, Type), Type chosen by Mode.
// Retail quirk: the default arm forwards Value's raw float bit pattern as the
// ThrustEquipType. Kept, since Mode is not provably limited to 0..2 (LaunchBehavior
// passes SetMode a variable).
void MoveIdea::SetThrottleScale(float Value)
{
    ThrustEquipType Type;
    switch (Mode)
    {
    case kMoveModeEngine:     Type = ThrustEquipEngine;    break;
    case kMoveModeTradelane:  Type = ThrustEquipTradelane; break;
    case kMoveModeCruise:     Type = ThrustEquipCruise;    break;
    default:
    {
        int ValueBits;
        std::memcpy(&ValueBits, &Value, sizeof(ValueBits));
        Type = (ThrustEquipType)ValueBits;
        break;
    }
    }

    Throttle = Behavior_GetProjectedThrottle(ObjInspectOf(Object), Value, Type);
}

// Folded at .text:062d88b0 -- `MOV [ECX+0x44],EAX; RET 4`. Takes an int: every
// observed argument is 0 or 2 and Commit() reads the field back as a signed int.
void MoveIdea::SetField44(int Value)
{
    StopStyle = Value;
}

// Folded at .text:062d5a10 (also FormationMoveFx::SetEnabled) --
// `MOV byte [ECX+0x58],AL; RET 4`.
void MoveIdea::SetEnabled(unsigned char Value)
{
    Field58 = Value;
}

// .text:062d5a70 -- `MOV byte [ECX+0x59],AL; RET 4`. Single call site
// .text:062c43f4, receiver DockBehavior+0x30.
void MoveIdea::SetField59(unsigned char Value)
{
    bKillEngineOffBearing = Value;
}
