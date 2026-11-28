// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// The death camera: built inline by the controller ctor (0x00549371), slots LAB_00549470
// (0x58), LAB_00549450 (0x60) and LAB_00549620 (0x6c).

#include "Space/Cameras/space_camera_death.h"

#include "common_iface.h"   // IObjInspect -- the watched object's thiscall getters

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    // The four literals the controller writes after the base ctor.
    const float kDeathDriftSpeed = 1.0f;        // 0x3f800000
    const float kDeathShakeDuration = 1.5f;     // 0x3fc00000
    const float kDeathDriftDamping = 0.85f;     // 0x3f59999a
    const float kDeathMaxDriftSpeed = 100.0f;   // 0x42c80000

    // The attach slot's override for a live ship: half the speed cap, and the same damping the
    // controller already wrote -- retail rewrites it anyway.
    const float kDeathShipMaxDriftSpeed = 50.0f;   // 0x42480000

    // The no-object drift direction: straight along -X.
    const float kDeathFallbackDirectionX = -1.0f;  // 0xbf800000

    // The shake lives inside a two-second window (.rdata 0x005d55b4).
    const float kShakeWindowSeconds = 2.0f;

    // .rdata 0x005c75e0.
    const float kShakeTimeScale = 0.5f;

    // .rdata 0x005d4c88, a DOUBLE. The amplitude is quadratic in the time left.
    const double kShakeAmplitudeScale = 0.03;

    // .rdata 0x005caeec = 1/32768, one over RAND_MAX+1. Retail scales rand() by this, not by
    // 1/RAND_MAX; on this CRT they differ by one.
    const float kRandToUnit = 3.0517578125e-05f;

    // The CEqObj type mask the attach slot tests. Both bits must be set.
    const unsigned kEqObjShipTypeMask = CObject::ClassShip;

    const int kWatchableOffset = 8;

    // An angle uniformly in [-Amplitude, +Amplitude], spelled `frac * (hi - lo) + lo` with
    // lo = -hi held as a float. The angle itself is never stored: it stays on the x87 stack for
    // FCOS / FSIN.
    double RandomShakeAngle(float Amplitude)
    {
        const float Low = -Amplitude;
        const double Fraction = static_cast<double>(rand()) * static_cast<double>(kRandToUnit);
        return Fraction * (static_cast<double>(Amplitude) - static_cast<double>(Low)) +
               static_cast<double>(Low);
    }

    // The two shake loops: the same three-iteration rotation over a different pair of rows. The
    // cosine is rounded to a float temporary; the sine stays double on the x87 stack.
    //
    // Rows 0 and 2 -- a rotation about Y: row0 = sin*B + A*cos, row2 = cos*B - A*sin.
    void RotateRowsAboutY(float* M, float Cosine, double Sine)
    {
        for (int I = 0; I < 3; ++I)
        {
            const float A = M[I];
            const float B = M[I + 6];
            M[I] = static_cast<float>(Sine * static_cast<double>(B) +
                                      static_cast<double>(A) * static_cast<double>(Cosine));
            M[I + 6] = static_cast<float>(static_cast<double>(Cosine) * static_cast<double>(B) -
                                          static_cast<double>(A) * Sine);
        }
    }

    // Rows 0 and 1 -- a rotation about Z, signs mirrored from the loop above:
    // row0 = cos*A - sin*B, row1 = cos*B + A*sin.
    void RotateRowsAboutZ(float* M, float Cosine, double Sine)
    {
        for (int I = 0; I < 3; ++I)
        {
            const float A = M[I];
            const float B = M[I + 3];
            M[I] = static_cast<float>(static_cast<double>(Cosine) * static_cast<double>(A) -
                                      Sine * static_cast<double>(B));
            M[I + 3] = static_cast<float>(static_cast<double>(Cosine) * static_cast<double>(B) +
                                          static_cast<double>(A) * Sine);
        }
    }

    // Vtable PTR_LAB_005e061c: the GameCamera table with the three death slots swapped in.
    // Slot 0x54 stays GameCamera's own name (LAB_0051b3b0).
}

void DeathCameraConstruct(FDeathCamera* Cam)
{
    // Inlined into the controller ctor on ESI + 0x60c (0x00549364): no null test in retail.
    Cam->FGameCamera::Construct();

    // Four floats and nothing else; the header says what stays uninitialised.
    Cam->DriftSpeed = kDeathDriftSpeed;
    Cam->ShakeDuration = kDeathShakeDuration;
    Cam->DriftDamping = kDeathDriftDamping;
    Cam->MaxDriftSpeed = kDeathMaxDriftSpeed;

    // Retail then sets the vptr to PTR_LAB_005e061c.
}

void FDeathCamera::Attach(IObjRW* Object)
{
    // No null test on `this` in retail.
    Watchable const* const Target =
        (Object != nullptr) ? static_cast<Watchable const*>(Object) : nullptr;

    if (Target != this->Watched.target())
        this->Watched.set(Target);

    // With nothing watched, everything is zeroed and the drift points along -X. Retail tests the
    // stored pointer AND the pointer minus eight (inlined Watchable::get()).
    unsigned char* const Watched =
        const_cast<unsigned char*>(
            reinterpret_cast<const unsigned char*>(this->Watched.target()));
    if (Watched == nullptr || (Watched - kWatchableOffset) == nullptr)
    {
        BasePosition[0] = 0.0f;
        BasePosition[1] = 0.0f;
        BasePosition[2] = 0.0f;
        DriftDirection[0] = kDeathFallbackDirectionX;
        DriftDirection[1] = 0.0f;
        DriftDirection[2] = 0.0f;
        return;
    }

    IObjRW* const Watchee = reinterpret_cast<IObjRW*>(Watched - kWatchableOffset);

    // A live SHIP gets a lower speed cap: the IObjRW's CObject, its class
    // masked with 0x503, and a non-null CShip::AtmosphereSolar (read before the class test).
    // Any link missing leaves the controller's 100.0f.
    CShip const* const EqObj =
        static_cast<CShip const*>(static_cast<IObjInspectImpl*>(Watchee)->Object);
    if (EqObj != nullptr)
    {
        const unsigned TypeWord = static_cast<unsigned>(EqObj->ObjectClass);
        CSolar const* const AtmosphereSolar = EqObj->AtmosphereSolar;
        if ((TypeWord & kEqObjShipTypeMask) == kEqObjShipTypeMask && AtmosphereSolar != nullptr)
        {
            MaxDriftSpeed = kDeathShipMaxDriftSpeed;
            DriftDamping = kDeathDriftDamping;
        }
    }

    IObjInspect* const Inspect = Watchee;
    const Vector& World = Inspect->get_position();
    BasePosition[0] = World.x;
    BasePosition[1] = World.y;
    BasePosition[2] = World.z;

    const Vector Velocity = Inspect->get_velocity();
    DriftDirection[0] = Velocity.x;
    DriftDirection[1] = Velocity.y;
    DriftDirection[2] = Velocity.z;

    // The drift is CLAMPED, not normalised: a slow wreck drifts slowly, a fast one is capped.
    // The length stays double; only strictly above the cap is clamped (equal/unordered are not).
    const double X = DriftDirection[0];
    const double Y = DriftDirection[1];
    const double Z = DriftDirection[2];
    const double Length = sqrt((X * X + Y * Y) + Z * Z);
    if (Length > static_cast<double>(MaxDriftSpeed))
    {
        // inv*y and inv*z are rounded through float temporaries; inv*x is not.
        const double Inv = 1.0 / Length;
        const double Cap = MaxDriftSpeed;
        const float InvY = static_cast<float>(Inv * Y);
        const float InvZ = static_cast<float>(Inv * Z);
        DriftDirection[0] = static_cast<float>(Inv * X * Cap);
        DriftDirection[1] = static_cast<float>(static_cast<double>(InvY) * Cap);
        DriftDirection[2] = static_cast<float>(static_cast<double>(InvZ) * Cap);
    }
}

bool FDeathCamera::IsFinished()
{
    // No null test on `this`. Exact equality with 0.0 only (NaN is false).
    return ShakeTimer == 0.0f;
}

void FDeathCamera::Update(float DeltaSeconds)
{
    // No null test on `this`. Retail asks its own slot 0x60 first; a finished camera does
    // nothing, not even copy its orientation out.
    if (FDeathCamera::IsFinished())
        return;

    // The timer runs down and is clamped at zero, which keeps the equality test above
    // reachable. The UNROUNDED difference is what is compared with zero.
    if (ShakeTimer > 0.0f)
    {
        const double Left = static_cast<double>(ShakeTimer) - static_cast<double>(DeltaSeconds);
        ShakeTimer = static_cast<float>(Left);
        if (Left < 0.0)
            ShakeTimer = 0.0f;
    }

    // Elapsed death time, kept double. The shake only happens while it is strictly below 2s.
    const double Elapsed =
        static_cast<double>(ShakeDuration) - static_cast<double>(ShakeTimer);
    if (Elapsed < static_cast<double>(kShakeWindowSeconds))
    {
        // Amplitude falls off with the SQUARE of the time left in the window; rounded to float
        // only at the end.
        const double Scaled = (static_cast<double>(kShakeWindowSeconds) - Elapsed) *
                              static_cast<double>(kShakeTimeScale);
        const float Amplitude = static_cast<float>(Scaled * Scaled * kShakeAmplitudeScale);

        // Two rand() draws in this order: the second happens after the first rotation is applied.
        const double AngleY = RandomShakeAngle(Amplitude);
        RotateRowsAboutY(ShakeOrientation, static_cast<float>(cos(AngleY)), sin(AngleY));

        const double AngleZ = RandomShakeAngle(Amplitude);
        RotateRowsAboutZ(ShakeOrientation, static_cast<float>(cos(AngleZ)), sin(AngleZ));
    }

    FSpaceCamera* const Base = this;

    memcpy(&Base->World, ShakeOrientation, 9 * sizeof(float));

    // The drift decays; the UNROUNDED result is compared with zero.
    const double Speed = static_cast<double>(DriftSpeed) -
                         static_cast<double>(DeltaSeconds) * static_cast<double>(DriftDamping);
    DriftSpeed = static_cast<float>(Speed);
    Base->bViewDirty = 1;
    if (Speed < 0.0)
        DriftSpeed = 0.0f;

    // One step along the drift direction with the speed AFTER the decay (reloaded from memory).
    // step*dir.y and step*dir.z are rounded through float temporaries, step*dir.x is not. The
    // accumulator -- not the base position -- becomes the world position.
    const double Step = static_cast<double>(DeltaSeconds) * static_cast<double>(DriftSpeed);
    const float StepY = static_cast<float>(Step * static_cast<double>(DriftDirection[1]));
    const float StepZ = static_cast<float>(Step * static_cast<double>(DriftDirection[2]));
    DriftPosition[0] = static_cast<float>(Step * static_cast<double>(DriftDirection[0]) +
                                               static_cast<double>(DriftPosition[0]));
    DriftPosition[1] = static_cast<float>(static_cast<double>(StepY) +
                                               static_cast<double>(DriftPosition[1]));
    DriftPosition[2] = static_cast<float>(static_cast<double>(StepZ) +
                                               static_cast<double>(DriftPosition[2]));

    Base->WorldPos.x = DriftPosition[0];
    Base->WorldPos.y = DriftPosition[1];
    Base->WorldPos.z = DriftPosition[2];
    Base->bViewDirty = 1;
}
