// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/button_mover.h"

#include "common_csys.h"
#include "Core/globals.h"
#include "Interface/wincontrol_base.h"

#include "DALib.h"
#include "engine/engbase.h"
#include "x86math.h"

#include <math.h>

namespace
{
    // DAT_005c8a5c -- pi (0x40490fdb).
    float const kPi = 3.14159274f;

    // DAT_00679b10 -- ButtonScaler::Update's lazily acquired 3DMathEngine (FUN_005b73e0).
    I3DMathEngine* G_ButtonScalerMath = nullptr;
}

// ---------------------------------------------------------------------------------------------
// ButtonMover
// ---------------------------------------------------------------------------------------------

// FUN_0055dec0 -- +0x14 / +0x1c stay unwritten.
ButtonMover::ButtonMover()
    : bActive(false)
{
}

// LAB_00401e90.
void ButtonMover::OnMoveFinished()
{
}

// LAB_0043d8a0.
bool ButtonMover::OnMoveStep(WinControl* /*Control*/, Vector const*)
{
    return false;
}

// FUN_0055dee0 (an inlined vector insert-at-end).
void ButtonMover::AddButton(WinControl* Control, Vector const* Start, Vector const* End)
{
    FButtonMoveEntry Entry;
    Entry.Start[0] = Start->x;
    Entry.Start[1] = Start->y;
    Entry.Start[2] = Start->z;
    Entry.End[0] = End->x;
    Entry.End[1] = End->y;
    Entry.End[2] = End->z;
    Entry.Control = Control;
    Entries.push_back(Entry);
}

// FUN_0055e120.
void ButtonMover::Start(float Seconds)
{
    Duration = Seconds;
    bActive = true;
    Elapsed = 0.0f;
    Entries.erase(Entries.begin(), Entries.end());
}

// FUN_0055e170
void ButtonMover::Update()
{
    if (!bActive)
    {
        return;
    }

    // The snap is taken when the unrounded double sum is not below the duration.
    double const Sum = GetClientFrameTimeCapped() + static_cast<double>(Elapsed);
    Elapsed = static_cast<float>(Sum);
    if (!(Sum < static_cast<double>(Duration)))
    {
        Elapsed = Duration;
        bActive = false;
    }

    float const T = Elapsed / Duration;

    int const Count = static_cast<int>(Entries.size());
    for (int Index = 0; Index < Count; ++Index)
    {
        FButtonMoveEntry const& Entry = Entries[Index];
        float const Dz = Entry.End[2] - Entry.Start[2];
        float const DxT = (Entry.End[0] - Entry.Start[0]) * T;
        float const DyT = (Entry.End[1] - Entry.Start[1]) * T;
        Vector Position;
        Position.x = DxT + Entry.Start[0];
        Position.y = DyT + Entry.Start[1];
        Position.z = Dz * T + Entry.Start[2];
        if (!OnMoveStep(Entry.Control, &Position))
        {
            // Property 6 sets the control's position.
            Entry.Control->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(&Position), 0);
        }
    }

    if (!bActive)
    {
        OnMoveFinished();
    }
}

// FUN_0055e290.
void ButtonMover::Finish()
{
    if (!bActive)
    {
        return;
    }
    bActive = false;

    int const Count = static_cast<int>(Entries.size());
    for (int Index = 0; Index < Count; ++Index)
    {
        FButtonMoveEntry const& Entry = Entries[Index];
        Vector Position;
        Position.x = Entry.End[0];
        Position.y = Entry.End[1];
        Position.z = Entry.End[2];
        if (!OnMoveStep(Entry.Control, &Position))
        {
            Entry.Control->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(&Position), 0);
        }
    }

    OnMoveFinished();
}

// ---------------------------------------------------------------------------------------------
// ButtonScaler
// ---------------------------------------------------------------------------------------------

// FUN_0055e340.
ButtonScaler::ButtonScaler()
    : bActive(false)
{
}

// LAB_00401e90.
void ButtonScaler::OnScaleFinished()
{
}

// LAB_0043d8a0.
bool ButtonScaler::OnScaleStep(WinControl* /*Control*/, float const* /*Scale*/)
{
    return false;
}

// FUN_0055e360.
void ButtonScaler::AddButton(WinControl* Control, float From, float To)
{
    FButtonScaleEntry Entry;
    Entry.From = From;
    Entry.To = To;
    Entry.Control = Control;
    Entries.push_back(Entry);
}

// FUN_0055e590.
void ButtonScaler::Start(float Seconds)
{
    Duration = Seconds;
    bActive = true;
    Elapsed = 0.0f;
    Entries.erase(Entries.begin(), Entries.end());
}

// FUN_0055e5e0
void ButtonScaler::Update()
{
    if (!bActive)
    {
        return;
    }

    double const Sum = GetClientFrameTimeCapped() + static_cast<double>(Elapsed);
    Elapsed = static_cast<float>(Sum);
    if (!(Sum < static_cast<double>(Duration)))
    {
        Elapsed = Duration;
        bActive = false;
    }

    // Cosine ease (DAT_005c75dc 1.0, DAT_005c75e0 0.5).
    double const Phase = static_cast<double>(Elapsed / Duration) * kPi;
    float const S = static_cast<float>(1.0 - (cos(Phase) + 1.0) * 0.5);

    int const Count = static_cast<int>(Entries.size());
    for (int Index = 0; Index < Count; ++Index)
    {
        FButtonScaleEntry const& Entry = Entries[Index];
        float Scale = (Entry.To - Entry.From) * S + Entry.From;
        if (Entry.Control == nullptr)
        {
            continue;
        }
        if (OnScaleStep(Entry.Control, &Scale))
        {
            continue;
        }

        // Identity rotation, translation copied from the instance's current transform (+0x24),
        // the rotation scaled by the math engine, then SetInstanceMatrix + UpdateInstance.
        const InstanceIndex Instance = Entry.Control->MeshHolder.EngineInstance;
        Transform Xform;
        Xform.Rotation.E[0][0] = 1.0f;
        Xform.Rotation.E[0][1] = 0.0f;
        Xform.Rotation.E[0][2] = 0.0f;
        Xform.Rotation.E[1][0] = 0.0f;
        Xform.Rotation.E[1][1] = 1.0f;
        Xform.Rotation.E[1][2] = 0.0f;
        Xform.Rotation.E[2][0] = 0.0f;
        Xform.Rotation.E[2][1] = 0.0f;
        Xform.Rotation.E[2][2] = 1.0f;
        const Transform* const Current = DALib::Engine->GetInstanceTransform(Instance);
        Xform.Translation.x = Current->Translation.x;
        Xform.Translation.y = Current->Translation.y;
        Xform.Translation.z = Current->Translation.z;

        if (G_ButtonScalerMath == nullptr)
        {
            G_ButtonScalerMath = Acquire3DMathEngine();
        }
        G_ButtonScalerMath->Scale(Xform.Rotation, Xform.Rotation, Scale);

        DALib::Engine->SetInstanceMatrix(Instance, &Xform);
        DALib::Engine->UpdateInstance(Instance, 0, nullptr);
    }

    if (!bActive)
    {
        OnScaleFinished();
    }
}
