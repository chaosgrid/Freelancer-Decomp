// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_cruisebehavior.h"
#include "common_systemmanager.h"  // SystemManager::Thrust -- ThrustManager::engage_cruise

// CruiseBehavior; layout notes are in common_cruisebehavior.h.

// ----------------------------------------------------------------------------
// FUN_062ce360. Only FUN_062d2020 (ConstructBase) and the vptr; no own field is written.
CruiseBehavior::CruiseBehavior(int InTypeId)
    : Behavior()
    , TypeId(InTypeId)
    , CachedHandle08(0)   // deviation: retail leaves this unset until SetContext()
    , ModeSelector0xc(0)
    , UpdateResult10(0)
    , OwnerContextPtr14(nullptr)
    , ObjectDb(nullptr)
    , DeltaTime1c(0.0f)
    , BehaviorMgrPtr20(nullptr)
    , NavHelperPtr24(nullptr)
    , Flags28(0)
    , Flag29(0)
{
    ConstructBase(InTypeId);   // FUN_062d2020
}

// Slot 0 is FUN_062c7090, the scalar-deleting-dtor thunk shared by 7 vtables; no own cleanup.
CruiseBehavior::~CruiseBehavior()
{
}

// Slot 1, LAB_062cf3e0: a bare `JMP FUN_062d2090` (Behavior::BindContext).
bool CruiseBehavior::SetContext(void* Param1, void* Param2, void* Param3)
{
    return BindContext(Param1, Param2, Param3);
}

// Slot 3, FUN_062ce380.
void CruiseBehavior::Engage()
{
    Behavior::OnEngage();                                               // FUN_062d20d0
    NavHelperPtr24->Thrust.engage_cruise(true);
}

// Slot 4, FUN_062ce3a0.
void CruiseBehavior::Disengage()
{
    NavHelperPtr24->Thrust.engage_cruise(false);
    Behavior::OnDisengage();                                               // FUN_062d20f0
}

// Slot 5, LAB_062ce3c0; ICF-shared with IdleBehavior::SetModeFromFlags.
int CruiseBehavior::SetModeFromFlags()
{
    int const Mode = (Flag29 != 0) ? 3 : 1;
    ModeSelector0xc = Mode;
    return Mode;
}

// Slot 7, LAB_062ce3e0.
int CruiseBehavior::Update()
{
    UpdateResult10 = (OwnerContextPtr14 != nullptr) ? 1 : 3;
    return UpdateResult10;
}

