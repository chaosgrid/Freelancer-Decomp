// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll BaseIdea family, shared part: BaseIdea, StopIdea, AfterburnerIdea, IdleIdea,
// the FDirectionPicker base of DodgeIdea / BreakAwayIdea, and GenericWeightedPick
// (FUN_062d76d0, the weighted pick over FDirectionPicker's tag + four weights).
// See src/include/common_idea.h for the RTTI/layout writeup. Most functions in this
// cluster have no Ghidra decompile; the raw disassembly is authoritative.
// The other ideas each have their own common_*idea.cpp; shared helpers are in
// common_idea_internal.h.

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
#include <cstdlib>
#include <cstring>

using namespace CmnIdea;

// ----------------------------------------------------------------------------
// BaseIdea
// ----------------------------------------------------------------------------

BaseIdea::BaseIdea()
    : Object(nullptr)
    , Handle(nullptr)
    , Context(nullptr)
    , Attached(0)
    , Pad11{}
{
}

// Slot 0 default implementation -- LAB_062d7920. Shared by 5 siblings (see
// common_idea.h); StopIdea/MoveIdea/FaceIdea override it.
bool BaseIdea::SetContext(void* InObject, void* InHandle, void* InContext)
{
    if (InObject == nullptr || InHandle == nullptr)
    {
        return false;
    }

    Handle = (SystemManager*)InHandle;
    Context = InContext;
    Object = InObject;
    Attached = 1;
    return true;
}

// FUN_062d4e50; no null tests.
void BaseIdea::PublishTargetId(unsigned int Id)
{
    static_cast<IBehaviorManager*>(Context)->Pathfind->Field_064 = static_cast<int>(Id);
}

// ----------------------------------------------------------------------------
// StopIdea
// ----------------------------------------------------------------------------

// Retail's ctor only zeroes BaseIdea's span and installs the vtable.
StopIdea::StopIdea()
    : BaseIdea()
{
}

// Own vtable slot 0 override -- LAB_062d76a0, `MOV AL,1; RET 0xc`.
// Unconditionally succeeds; ignores all 3 incoming pointers.
bool StopIdea::SetContext(void* /*InObject*/, void* /*InHandle*/, void* /*InContext*/)
{
    return true;
}

// Own vtable slot 1 override -- LAB_0632b010 (`33 C0 C2 04 00`: `XOR EAX,EAX; RET 4`,
// ICF-folded in retail Common.dll).
int StopIdea::Update(float /*DeltaTime*/)
{
    return 0;
}

// ----------------------------------------------------------------------------
// AfterburnerIdea
// ----------------------------------------------------------------------------

// FUN_062da500. No fields beyond BaseIdea; vtable PTR_LAB_063a0584.
AfterburnerIdea::AfterburnerIdea()
    : BaseIdea()
{
}

// Own vtable slot 1 override -- FUN_062da520. DeltaTime is never read.
// Handle+0x18 is recomputed for each call and never tested.
int AfterburnerIdea::Update(float /*DeltaTime*/)
{
    if (Object == nullptr)
    {
        return 3;
    }

    Handle->Thrust.SetThrustersActive(true);
    Handle->Thrust.SetThrottle(1.0f, 0);

    return 0;
}

// Own vtable slot 4 override -- LAB_062da570 (.rdata:063a0594). No null test on
// Handle.
void AfterburnerIdea::Commit()
{
    Handle->Thrust.SetThrustersActive(false);
}

// ----------------------------------------------------------------------------
// IdleIdea
// ----------------------------------------------------------------------------

// FUN_062d9b10; vtable 063a054c.
IdleIdea::IdleIdea()
    : BaseIdea()
{
}

// LAB_062d9b30. No null test on Handle; retail re-zeroes the scratch Vector
// between calls.
int IdleIdea::Update(float /*DeltaTime*/)
{
    if (Object == nullptr)
        return 3;

    ThrustManager* const Thrust = &Handle->Thrust;
    Thrust->SetThrottle(0.0f, 2);
    Thrust->SetThrottle(0.0f, 0);
    Vector Zero;
    Zero.x = 0.0f;
    Zero.y = 0.0f;
    Zero.z = 0.0f;
    Thrust->SetAxisThrottle(Zero);
    Zero.x = 0.0f;
    Zero.y = 0.0f;
    Zero.z = 0.0f;
    Thrust->SetNudgeDirection(Zero);
    Thrust->SetStrafeDir(StrafeNone);
    Thrust->SetThrustersActive(false);
    return 0;
}

// ----------------------------------------------------------------------------
// FDirectionPicker -- the shared second base (+0x14..+0x33) of DodgeIdea and
// BreakAwayIdea; retail inlines this init in both ctors. Flag1c is set by each
// derived ctor.
// ----------------------------------------------------------------------------
FDirectionPicker::FDirectionPicker()
    : LastDirection(0)
    , WeightLeft(1.0f)
    , WeightRight(1.0f)
    , WeightUp(1.0f)
    , WeightDown(1.0f)
    , Watcher()
    , Flag1c(0)       // each derived ctor overwrites this with its own constant
    , Pad1d{}
{
}

// FUN_062d88e0 (RET 4). The receiver register is the Idea; +0x28 / +0x30 are this
// picker's Watcher / Flag1c.
void FDirectionPicker::SetWatchObject(void const* Object)
{
    // IObjRW -> Watchable.
    Watchable const* const Target =
        (Object != nullptr)
            ? reinterpret_cast<Watchable const*>(static_cast<unsigned char const*>(Object) + 8)
            : nullptr;
    Watcher.set(Target);
    Flag1c = 1;            // unconditional
}

// FUN_062d76b0.
void FDirectionPicker::SetWeights(float Left, float Right, float Up, float Down)
{
    WeightLeft = Left;
    WeightRight = Right;
    WeightUp = Up;
    WeightDown = Down;
}

// FUN_062d76d0. See common_aisteer.h for the declaration comment.
int GenericWeightedPick(int* State)
{
    // Local copies of the four weights, the current tag's zeroed.
    int const Tag = State[0];
    float const* const W = reinterpret_cast<float const*>(State + 1);
    float const W1 = (Tag == 1) ? 0.0f : W[0];
    float const W2 = (Tag == 2) ? 0.0f : W[1];
    float const W3 = (Tag == 3) ? 0.0f : W[2];
    float const W4 = (Tag == 4) ? 0.0f : W[3];

    // Sum order W4+W3+W2+W1; DAT_063997d0 == 2^-15. The draw is never rounded.
    double const Draw = ((double)rand() * ((((double)W4 + W3) + W2) + W1)) * (1.0 / 32768.0);

    // Each test falls through on not-less or unordered, so a NaN draw ends at 4.
    // The running sums are rebuilt each time, also unrounded.
    int NewTag;
    if (Draw < W1)
        NewTag = 1;
    else if (Draw < (double)W2 + W1)
        NewTag = 2;
    else if (Draw < ((double)W3 + W2) + W1)
        NewTag = 3;
    else
        NewTag = 4;

    State[0] = NewTag;
    return NewTag;
}
