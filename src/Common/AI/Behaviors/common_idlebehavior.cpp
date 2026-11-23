// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_idlebehavior.h"

// FUN_062cd530 (vtable .rdata:0639fc5c).
IdleBehavior::IdleBehavior(int InTypeId)
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
    , Watcher2c()   // FUN_062d9b10, IdleIdea's ctor
{
    ConstructBase(InTypeId);   // FUN_062d2020
}

// Scalar deleting FUN_062c7090 (shared by 7 vtables) only chains to ~Behavior; no
// IdleBehavior-specific cleanup.
IdleBehavior::~IdleBehavior()
{
}

// Slot 1, LAB_062cd590. The idea is bound only when BindContext (FUN_062d2090) succeeds.
bool IdleBehavior::SetContext(void* Param1, void* Param2, void* Param3)
{
    bool const bOk = BindContext(Param1, Param2, Param3);
    if (bOk)
        Watcher2c.SetContext(OwnerContextPtr14, NavHelperPtr24, Param2);
    return bOk;
}

// Slot 3, LAB_062cd5d0: jumps to Behavior's slot 3 (FUN_062d20d0).
void IdleBehavior::ResetTickState()
{
    Behavior::OnEngage();
}

// Slot 4, FUN_062cd5e0.
void IdleBehavior::Disengage()
{
    Watcher2c.Commit();     // IdleIdea's Commit is a bare RET
    Behavior::OnDisengage();
}

// Slot 5, LAB_062ce3c0.
int IdleBehavior::SetModeFromFlags()
{
    int const Mode = (Flag29 != 0) ? 3 : 1;
    ModeSelector0xc = Mode;
    return Mode;
}

// Slot 7, FUN_062cd600.
int IdleBehavior::Update()
{
    if (OwnerContextPtr14 == nullptr)
    {
        UpdateResult10 = 3;
        return UpdateResult10;
    }

    // Result discarded.
    Watcher2c.Update(DeltaTime1c);

    UpdateResult10 = 1;
    return UpdateResult10;
}

// Slot 10, LAB_062cd630 -- ICF-shared with FormationBehavior::OnApplyDirective; reproduced as
// its own method rather than a cast to that unrelated type.
bool IdleBehavior::RequestFlag()
{
    Flags28 = static_cast<unsigned char>(Flags28 | 0x2);
    return true;
}

