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
    : Behavior(InTypeId)
    , StateTimer2c(0.0f)
    , DurationThreshold30(4.0f)
    , NudgeOffset{}
    , EvadeMode(0)
{
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
    Readiness = Result;
    return Result;
}

// Slot 7, LAB_062c8800.
int DrasticEvadeBehavior::OnUpdate()
{
    if (EvadeMode == 0)
    {
        // Strict timer < duration on the unrounded x87 sum (FST, not FSTP).
        double const Timer = static_cast<double>(DeltaTime) + static_cast<double>(StateTimer2c);
        StateTimer2c = static_cast<float>(Timer);
        IObjAffect* const Affect = OwnerContext;   // [+0x14]+4
        if (Timer < static_cast<double>(DurationThreshold30))
        {
            Vector const Nudge = { NudgeOffset.x, NudgeOffset.y, 0.0f };
            Affect->set_nudge_vec(Nudge);
            UpdateResult = 1;
            return 1;
        }
        Vector const Zero = { 0.0f, 0.0f, 0.0f };
        Affect->set_nudge_vec(Zero);
        UpdateResult = 2;
        return 2;
    }

    if (EvadeMode == 1)
    {
        // Same accumulate/compare; FUN_062e89a0 with full reverse while running, 0 when expired.
        double const Timer = static_cast<double>(DeltaTime) + static_cast<double>(StateTimer2c);
        StateTimer2c = static_cast<float>(Timer);
        ThrustManager* const Thrust = &SysMgr->Thrust;
        if (Timer < (double)DurationThreshold30)
        {
            Thrust->SetThrottle(-1.0f, 0);
            UpdateResult = 1;
            return 1;
        }
        Thrust->SetThrottle(0.0f, 0);
        UpdateResult = 2;
        return 2;
    }

    UpdateResult = 3;
    return 3;
}

// Slot 10, LAB_062c87c0.
bool DrasticEvadeBehavior::OnApplyDirective(void* DirectiveOp)
{
    pub::AI::DirectiveDrasticEvadeOp const* const Op =
        static_cast<pub::AI::DirectiveDrasticEvadeOp const*>(DirectiveOp);
    EvadeMode = static_cast<int>(Op->Mode);
    if (EvadeMode == 0)
        std::memcpy(&NudgeOffset, &Op->NudgeX, sizeof(NudgeOffset));   // three float-bit dwords
    Flags28 = static_cast<unsigned char>(Flags28 | 0x2);
    DurationThreshold30 = Op->Duration;
    return true;
}
