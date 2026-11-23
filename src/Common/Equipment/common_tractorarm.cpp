// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// TractorArm -- one grab arm of a CETractor (retail CETractor TU). Declared in common_cequip.h.
#include "common_cequip.h"
#include "common_cobject.h" // CLoot, CSimple::SelfWatchable
#include "common_archetype.h" // Archetype::Tractor (ReachSpeed)

#include <cmath>
#include <cstddef>

namespace
{
    // CLoot* -> its CSimple::SelfWatchable (+0x9c); null stays null, as retail tests it.
    Watchable const* LootWatchable(CLoot* Target)
    {
        if (Target == nullptr)
            return nullptr;
        return (Watchable const*)((unsigned char const*)Target + offsetof(CSimple, SelfWatchable));
    }
}

TractorArm::TractorArm(CETractor* OwnerTractor)
    : Owner(OwnerTractor)
    , State(MODE_0)
    , PullOffset{0.0f, 0.0f, 0.0f}
    , Timer(0.0f)
    , ErrorCode(TractorOk)
{
    // FUN_0629db80. TargetWatch (+0x08/+0x0c) default-constructs unlinked.
}

TractorArm::TractorArm(TractorArm const& Other)
    : Owner(Other.Owner)
    , State(Other.State)
    , PullOffset(Other.PullOffset)
    , Timer(Other.Timer)
    , ErrorCode(Other.ErrorCode)
{
    // FUN_0629d890. Retail relinks through the CLoot round trip (W - 0x9c, then + 0x9c, each
    // step tested), so a watched pointer equal to 0x9c is not relinked.
    CLoot* Target = Other.GetTarget();
    if (Target != nullptr)
        TargetWatch.set(LootWatchable(Target));
}

TractorArm& TractorArm::operator=(TractorArm const& Other)
{
    // FUN_0629d920. Same CLoot round trip as the copy ctor.
    Owner = Other.Owner;
    State = Other.State;
    TargetWatch.set(LootWatchable(Other.GetTarget()));
    PullOffset = Other.PullOffset;
    Timer = Other.Timer;
    ErrorCode = Other.ErrorCode;
    return *this;
}

TractorArm::~TractorArm()
{
    // FUN_0629dbd0. Retail unwatches here and then again in the BaseWatcher member destructor.
    TargetWatch.set(nullptr);
    State = MODE_0;
    PullOffset = Vector{0.0f, 0.0f, 0.0f};
    Timer = 0.0f;
    ErrorCode = TractorOk;
}

CLoot* TractorArm::GetTarget() const
{
    // FUN_0629d870
    Watchable const* Watched = TargetWatch.target();
    if (Watched == nullptr)
        return nullptr;
    return reinterpret_cast<CLoot*>(
        const_cast<unsigned char*>(reinterpret_cast<unsigned char const*>(Watched)) -
        offsetof(CSimple, SelfWatchable));
}

void TractorArm::SetTarget(CLoot* Target)
{
    // FUN_0629ded0
    TargetWatch.set(LootWatchable(Target));
    PullOffset = Vector{0.0f, 0.0f, 0.0f};
    Timer = 0.0f;
    ErrorCode = TractorOk;
    State = MODE_1;
}

void TractorArm::TurnOff()
{
    // FUN_0629dea0
    TargetWatch.set(nullptr);
    State = MODE_0;
    PullOffset = Vector{0.0f, 0.0f, 0.0f};
    Timer = 0.0f;
    ErrorCode = TractorOk;
}

void TractorArm::Update(float Dt)
{
    // FUN_0629dc40. Retail's FUN_0629de50 is CETractor::GetSourcePos here. MODE_1 extends the tip
    // toward the target at up to ReachSpeed units/sec; when this frame's step reaches it,
    // PullOffset snaps onto the target and MODE_2 counts ACQUIRE_DELAY down to Event_0x18.
    if (State == MODE_0)
        return;

    CLoot* Target = GetTarget();
    TractorFailureCode const Code = Owner->VerifyTarget(Target);
    if (Code != TractorOk)
    {
        ErrorCode = Code;
        Owner->Notify(INotify::Event_0x15, this);
        TurnOff();
        return;
    }

    if (State == MODE_1)
    {
        Vector const SourcePos = Owner->GetSourcePos();
        Vector const Candidate{SourcePos.x + PullOffset.x, SourcePos.y + PullOffset.y,
                                SourcePos.z + PullOffset.z};
        Vector const Delta{Target->WorldPosition.x - Candidate.x, Target->WorldPosition.y - Candidate.y,
                            Target->WorldPosition.z - Candidate.z};

        // x87 precision: dx and dz are rounded to float, dy, MaxStep, the sum and Frac are not.
        // Only "greater" pulls; equal or unordered snap. dx * Frac is added unrounded, the y and
        // z steps are rounded to float first.
        double const Dx = Delta.x;
        double const Dy = (double)Target->WorldPosition.y - Candidate.y;
        double const Dz = Delta.z;
        double const MaxStep = (double)Dt * ((Archetype::Tractor const*)Owner->EquipArch())->ReachSpeed;
        double const DistSq = Dz * Dz + Dy * Dy + Dx * Dx;
        if (DistSq > MaxStep * MaxStep)
        {
            double const Frac = MaxStep / sqrt(DistSq);
            float const StepY = (float)(Dy * Frac);
            float const StepZ = (float)(Dz * Frac);
            PullOffset.x = (float)(Dx * Frac + PullOffset.x);
            PullOffset.y = StepY + PullOffset.y;
            PullOffset.z = StepZ + PullOffset.z;
            return;
        }

        PullOffset.x = Target->WorldPosition.x - SourcePos.x;
        PullOffset.y = Target->WorldPosition.y - SourcePos.y;
        PullOffset.z = Target->WorldPosition.z - SourcePos.z;
        Owner->Notify(INotify::Event_0x17, this);
        State = MODE_2;
        Timer = ACQUIRE_DELAY;
        return;
    }

    if (State == MODE_2)
    {
        Timer -= Dt;
        if (Timer < 0.0f)
            Owner->Notify(INotify::Event_0x18, this);
    }
}

Vector TractorArm::GetTipPos() const
{
    // FUN_0629df10. MODE_1: source + PullOffset. MODE_2: the target's position, or the source if
    // it is gone. Otherwise the source. Retail's read of the literal 0x2c/0x30/0x34 sits behind a
    // re-test of an already non-null pointer, so it is dead.
    if (State == MODE_1)
    {
        Vector const Source = Owner->GetSourcePos();
        return Vector{Source.x + PullOffset.x, Source.y + PullOffset.y, Source.z + PullOffset.z};
    }

    if (State == MODE_2)
    {
        CLoot* Target = GetTarget();
        if (Target != nullptr)
            return Target->WorldPosition;
    }

    return Owner->GetSourcePos();
}

// Ordinals 1174 and 1481, .rdata 0x0639d80c / 0x0639d810. ACQUIRE_DELAY is the MODE_2 countdown
// (TractorArm+0x1c) before the towed CLoot is consumed. GRAB_TOLERANCE_SQUARED has no reader in
// Common.dll; exported for symbol parity only.
float const TractorArm::ACQUIRE_DELAY = 1.0f;
float const TractorArm::GRAB_TOLERANCE_SQUARED = 1.0f;
