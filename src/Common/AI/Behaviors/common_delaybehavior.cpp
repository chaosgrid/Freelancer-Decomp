// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_delaybehavior.h"
#include "common_pathfind.h" // PathfindManager (IBehaviorManager+0x0C)

// DelayBehavior; layout notes are in common_delaybehavior.h.

// FUN_062d0a10. FUN_062d2020 (Behavior's ctor), then the IdleIdea member.
DelayBehavior::DelayBehavior(int InTypeId)
    : Behavior(InTypeId)
    , Watcher2c()   // FUN_062d9b10 (IdleIdea's ctor)
    , DelayDuration(0.0f)
    , DelayElapsed(0.0f)
{
}

// Slot 0 is FUN_062c7090, the scalar-deleting-dtor thunk shared by 7 vtables; no own cleanup.
DelayBehavior::~DelayBehavior()
{
}

// Slot 1, LAB_062d0a70.
bool DelayBehavior::SetContext(IObjRW* Param1, IBehaviorManager* Param2, SystemManager* Param3)
{
    bool const bOk = BindContext(Param1, Param2, Param3);   // FUN_062d2090
    if (bOk)
    {
        Watcher2c.SetContext(OwnerContext, SysMgr, Param2);
        DelayDuration = 0.0f;
        DelayElapsed = 0.0f;
    }
    return bOk;
}

// Slot 3, FUN_062d0ac0.
void DelayBehavior::ResetTickState()
{
    Behavior::OnEngage();      // FUN_062d20d0
    DelayElapsed = 0.0f;
}

// Slot 4, FUN_062cd5e0; ICF-shared with IdleBehavior::Disengage().
void DelayBehavior::Disengage()
{
    Watcher2c.Commit();     // IdleIdea, bare RET
    Behavior::OnDisengage();      // FUN_062d20f0
}

// Slot 5; ICF-shared with CruiseBehavior/IdleBehavior::SetModeFromFlags().
int DelayBehavior::SetModeFromFlags()
{
    int const Mode = (Flag29 != 0) ? 3 : 1;
    Readiness = Mode;
    return Mode;
}

// Slot 7, FUN_062d0ae0.
int DelayBehavior::Update()
{
    if (OwnerContext == nullptr)
    {
        UpdateResult = kBehaviorUpdateAborted;
        return UpdateResult;
    }

    Watcher2c.Update(DeltaTime);   // result discarded

    // Retail compares the unrounded x87 sum; strictly greater.
    double const Sum = static_cast<double>(DelayElapsed) + DeltaTime;
    DelayElapsed = static_cast<float>(Sum);
    if (Sum > DelayDuration)
    {
        // Fixed sentinel/reset bit-pattern retail writes verbatim (0xccbebc20).
        unsigned int SentinelBits = 0xccbebc20u;
        DelayElapsed = *reinterpret_cast<float*>(&SentinelBits);

        // 0x14 is DelayBehavior's own index in the BehaviorDispatchTable (GuideBehavior uses 0xe).
        BehaviorMgr->SetDirectiveResult(BEHAVIOR_DELAY, 0);   // FUN_062d29a0
    }

    UpdateResult = kBehaviorUpdateRunning;
    return UpdateResult;
}

// Slot 10, LAB_062d0b40.
bool DelayBehavior::ApplyDirective(void const* Param)
{
    float Value = static_cast<pub::AI::DirectiveDelayOp const*>(Param)->DelaySeconds;

    DelayDuration = Value;
    Flags28 = static_cast<unsigned char>(Flags28 | kBehaviorFlagDirectiveAccepted);
    DelayElapsed = Value; // retail writes the same value to both fields

    return true;
}


