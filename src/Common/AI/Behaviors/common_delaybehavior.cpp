// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_delaybehavior.h"
#include "common_pathfind.h" // PathfindManager (IBehaviorManager+0x0C)

// DelayBehavior; layout notes are in common_delaybehavior.h.

// FUN_062d0a10. FUN_062d2020 (ConstructBase), then the IdleIdea member.
DelayBehavior::DelayBehavior(int InTypeId)
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
    , Watcher2c()   // FUN_062d9b10 (IdleIdea's ctor)
    , DelayDuration(0.0f)
    , DelayElapsed(0.0f)
{
    ConstructBase(InTypeId);   // FUN_062d2020
}

// Slot 0 is FUN_062c7090, the scalar-deleting-dtor thunk shared by 7 vtables; no own cleanup.
DelayBehavior::~DelayBehavior()
{
}

// Slot 1, LAB_062d0a70.
bool DelayBehavior::SetContext(void* Param1, void* Param2, void* Param3)
{
    bool const bOk = BindContext(Param1, Param2, Param3);   // FUN_062d2090
    if (bOk)
    {
        Watcher2c.SetContext(OwnerContextPtr14, NavHelperPtr24, Param2);
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
    ModeSelector0xc = Mode;
    return Mode;
}

// Slot 7, FUN_062d0ae0.
int DelayBehavior::Update()
{
    if (OwnerContextPtr14 == nullptr)
    {
        UpdateResult10 = 3;
        return UpdateResult10;
    }

    Watcher2c.Update(DeltaTime1c);   // result discarded

    // Retail compares the unrounded x87 sum; strictly greater.
    double const Sum = static_cast<double>(DelayElapsed) + DeltaTime1c;
    DelayElapsed = static_cast<float>(Sum);
    if (Sum > DelayDuration)
    {
        // Fixed sentinel/reset bit-pattern retail writes verbatim (0xccbebc20).
        unsigned int SentinelBits = 0xccbebc20u;
        DelayElapsed = *reinterpret_cast<float*>(&SentinelBits);

        // 0x14 is DelayBehavior's own index in the BehaviorDispatchTable (GuideBehavior uses 0xe).
        BehaviorMgrPtr20->SetDirectiveResult(0x14, 0);   // FUN_062d29a0
    }

    UpdateResult10 = 1;
    return UpdateResult10;
}

// Slot 10, LAB_062d0b40.
bool DelayBehavior::ApplyDirective(void const* Param)
{
    float Value = *reinterpret_cast<float const*>(static_cast<unsigned char const*>(Param) + 0x0c);

    DelayDuration = Value;
    Flags28 = static_cast<unsigned char>(Flags28 | 0x2);
    DelayElapsed = Value; // retail writes the same value to both fields

    return true;
}


