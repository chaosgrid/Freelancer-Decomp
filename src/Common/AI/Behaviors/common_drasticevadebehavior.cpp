// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_drasticevadebehavior.h"

#include <cstring>

// DrasticEvadeBehavior; see common_drasticevadebehavior.h.

// FUN_062c8730: FUN_062d2020(TypeId), +0x40 = 0, +0x30 = 4.0f, +0x34..+0x3c = 0. Retail leaves
// +0x2c to OnEngage; the port zeroes it (OnEngage runs before OnUpdate reads it).
DrasticEvadeBehavior::DrasticEvadeBehavior(int InTypeId)
    : Behavior()
    , TypeId(InTypeId)
    , CachedHandle08(0)
    , ModeSelector0xc(0)
    , UpdateResult10(0)
    , OwnerContextPtr14(nullptr)
    , ObjectDb(nullptr)
    , DeltaTime1c(0.0f)
    , BehaviorMgrPtr20(nullptr)
    , NavHelperPtr24(nullptr)
    , Flags28(0)
    , Flag29(0)
    , Pad2a{}
    , StateTimer2c(0.0f)
    , DurationThreshold30(4.0f)
    , TargetOffset{}
    , TargetKind40(0)
{
    ConstructBase(InTypeId);   // FUN_062d2020
}

// Slot 0 is FUN_062c7090, the scalar-deleting dtor ICF-shared by seven behavior vtables.
DrasticEvadeBehavior::~DrasticEvadeBehavior()
{
}

// Slot 1, LAB_062cf3e0: `JMP FUN_062d2090`.
bool DrasticEvadeBehavior::SetContext(void* Owner, void* Manager, void* Sys)
{
    return BindContext(Owner, Manager, Sys);
}

// Slot 3, FUN_062c8790.
void DrasticEvadeBehavior::OnEngage()
{
    Behavior::OnEngage();
    StateTimer2c = 0.0f;
}

// Slot 4, FUN_062c87b0.
void DrasticEvadeBehavior::OnDisengage()
{
    Behavior::OnDisengage();
    Flags28 = static_cast<unsigned char>(Flags28 | 0x2);
}

// Slot 5, LAB_062c8760.
int DrasticEvadeBehavior::OnEvaluateReadiness(float Unused)
{
    (void)Unused;
    int Result = 2;
    if ((Flags28 & 0x2) != 0)
        Result = 0;
    if (Flag29 != 0)
        Result = 3;
    ModeSelector0xc = Result;
    return Result;
}

// Slot 7, LAB_062c8800.
int DrasticEvadeBehavior::OnUpdate()
{
    if (TargetKind40 == 0)
    {
        // Strict timer < duration on the unrounded x87 sum (FST, not FSTP).
        double const Timer = static_cast<double>(DeltaTime1c) + static_cast<double>(StateTimer2c);
        StateTimer2c = static_cast<float>(Timer);
        IObjAffect* const Affect = static_cast<IObjRW*>(OwnerContextPtr14);   // [+0x14]+4
        if (Timer < static_cast<double>(DurationThreshold30))
        {
            Vector const Nudge = { TargetOffset.x, TargetOffset.y, 0.0f };
            Affect->set_nudge_vec(Nudge);
            UpdateResult10 = 1;
            return 1;
        }
        Vector const Zero = { 0.0f, 0.0f, 0.0f };
        Affect->set_nudge_vec(Zero);
        UpdateResult10 = 2;
        return 2;
    }

    if (TargetKind40 == 1)
    {
        // Same accumulate/compare; FUN_062e89a0 with full reverse while running, 0 when expired.
        double const Timer = static_cast<double>(DeltaTime1c) + static_cast<double>(StateTimer2c);
        StateTimer2c = static_cast<float>(Timer);
        ThrustManager* const Thrust = &NavHelperPtr24->Thrust;
        if (Timer < (double)DurationThreshold30)
        {
            Thrust->SetThrottle(-1.0f, 0);
            UpdateResult10 = 1;
            return 1;
        }
        Thrust->SetThrottle(0.0f, 0);
        UpdateResult10 = 2;
        return 2;
    }

    UpdateResult10 = 3;
    return 3;
}

// Slot 10, LAB_062c87c0.
bool DrasticEvadeBehavior::OnApplyDirective(void* DirectiveOp)
{
    unsigned char const* const Op = static_cast<unsigned char const*>(DirectiveOp);
    std::memcpy(&TargetKind40, Op + 0xc, sizeof(TargetKind40));
    if (TargetKind40 == 0)
        std::memcpy(&TargetOffset, Op + 0x10, sizeof(TargetOffset));
    Flags28 = static_cast<unsigned char>(Flags28 | 0x2);
    std::memcpy(&DurationThreshold30, Op + 0x1c, sizeof(DurationThreshold30));
    return true;
}
