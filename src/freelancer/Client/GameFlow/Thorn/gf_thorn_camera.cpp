// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "GameFlow/Thorn/gf_thorn_camera.h"

#include "dacom.h"
#include "GameFlow/Thorn/gf_script.h"
#include "GameFlow/room_camera.h"

#include <cstdint>
#include <cstring>

namespace
{
    char const* const GScriptCameraFile = "E:\\FL\\Scratch\\Source\\Client\\Gf\\ScriptCamera.cpp";
    char const* const GInvalidCameraReference =
        "%s(%d) : *** ERROR: Using invalid camera reference %s\n";

    // The pool slot is indexed without a range check by every body below. A slot of -1 --
    // Allocate found the 256 slots full -- would reach the 0x40 bytes before the pool, exactly
    // as retail does.
    FGfCameraSlot& PoolSlotAt(int Slot)
    {
        return G_GfCameraPool.Slots[Slot];
    }

    // The head of LAB_00450880 / LAB_00450940: to world through the script frame, into the slot,
    // FUN_00556cb0 when this slot is DAT_0066d100.
    void StorePoolPosition(Csys const& Frame, int Slot, float const* Local3)
    {
        Vector const World = Frame.trans_from_frame(*reinterpret_cast<Vector const*>(Local3));
        FGfCameraSlot& Target = PoolSlotAt(Slot);
        Target.Pos[0] = World.x;
        Target.Pos[1] = World.y;
        Target.Pos[2] = World.z;
        if (Slot == G_GfCameraPool.ActiveIndex)
            RoomCameraSetPosition(Target.Camera, Target.Pos);
    }

    // LAB_004508e0 / the tail of LAB_00450940: the same for the orientation, FUN_00556cc0.
    void StorePoolOrientation(Csys const& Frame, int Slot, float const* Local9)
    {
        Matrix const World = Frame.trans_from_frame(*reinterpret_cast<Matrix const*>(Local9));
        FGfCameraSlot& Target = PoolSlotAt(Slot);
        memcpy(Target.Orient, &World, sizeof(Target.Orient));
        if (Slot == G_GfCameraPool.ActiveIndex)
            RoomCameraSetOrientation(Target.Camera, Target.Orient);
    }

    void LoadPoolPosition(Csys const& Frame, int Slot, float* Out3)
    {
        FGfCameraSlot const& Source = PoolSlotAt(Slot);
        Vector const Local = Frame.trans_to_frame(*reinterpret_cast<Vector const*>(Source.Pos));
        Out3[0] = Local.x;
        Out3[1] = Local.y;
        Out3[2] = Local.z;
    }

    void LoadPoolOrientation(Csys const& Frame, int Slot, float* Out9)
    {
        FGfCameraSlot const& Source = PoolSlotAt(Slot);
        Matrix const Local = Frame.trans_to_frame(*reinterpret_cast<Matrix const*>(Source.Orient));
        memcpy(Out9, &Local, sizeof(float) * 9);
    }

    GENRESULT ReportInvalidReference(char const* Name, int Line)
    {
        FDUMP(kFdumpError, GInvalidCameraReference, GScriptCameraFile, Line, Name);
        return kThornReferenceMissing;
    }
}

// ---------------------------------------------------------------------------------------
// Thorn_Camera
// ---------------------------------------------------------------------------------------

// FUN_004507a0.
FThornCamera::FThornCamera(Csys const& ScriptFrame, FGfScript* InOwner,
                           FThornEntitySnap const* Desc)
    : FThornComponent(ScriptFrame, InOwner, Desc),
      PoolSlot(-1)
{
}

// FUN_00450830.
void FThornCamera::Register(FRoomCamera* Camera, int Priority, unsigned char bCanBeActive)
{
    PoolSlot = G_GfCameraPool.Allocate(Priority, Camera, bCanBeActive);
}

// LAB_00450940. The position first, then the rotation.
GENRESULT DACOM_API FThornCamera::SetTransform(float const* Transform12)
{
    StorePoolPosition(Frame, PoolSlot, Transform12 + 9);
    StorePoolOrientation(Frame, PoolSlot, Transform12);
    return GR_OK;
}

// LAB_00450aa0.
GENRESULT DACOM_API FThornCamera::GetTransform(float* OutTransform12)
{
    LoadPoolPosition(Frame, PoolSlot, OutTransform12 + 9);
    LoadPoolOrientation(Frame, PoolSlot, OutTransform12);
    return GR_OK;
}

// LAB_00450880.
GENRESULT DACOM_API FThornCamera::SetPosition(float* Position)
{
    StorePoolPosition(Frame, PoolSlot, Position);
    return GR_OK;
}

// LAB_004508e0.
GENRESULT DACOM_API FThornCamera::SetOrientation(float* Orient9)
{
    StorePoolOrientation(Frame, PoolSlot, Orient9);
    return GR_OK;
}

// LAB_004509f0.
GENRESULT DACOM_API FThornCamera::GetPosition(float* OutPosition)
{
    LoadPoolPosition(Frame, PoolSlot, OutPosition);
    return GR_OK;
}

// LAB_00450a50.
GENRESULT DACOM_API FThornCamera::GetOrientation(float* OutOrient9)
{
    LoadPoolOrientation(Frame, PoolSlot, OutOrient9);
    return GR_OK;
}

// LAB_00450b40. FUN_00556ce0 only when the slot is the active one (DAT_0066d100).
GENRESULT DACOM_API FThornCamera::SetFov(const float* Fov)
{
    FGfCameraSlot& Slot = PoolSlotAt(PoolSlot);
    float Value;
    std::memcpy(&Value, Fov, sizeof(Value));
    std::memcpy(&Slot.HalfFovXDeg, &Value, sizeof(Value));
    if (PoolSlot == G_GfCameraPool.ActiveIndex)
        RoomCameraSetHalfFovXValue(Slot.Camera, &Value);
    return GR_OK;
}

// LAB_00450b80 -- slot +0x38, FUN_00556e00.
GENRESULT DACOM_API FThornCamera::SetNear(const float* Near)
{
    FGfCameraSlot& Slot = PoolSlotAt(PoolSlot);
    float Value;
    std::memcpy(&Value, Near, sizeof(Value));
    std::memcpy(&Slot.ZNear, &Value, sizeof(Value));
    if (PoolSlot == G_GfCameraPool.ActiveIndex)
        RoomCameraSetZNearValue(Slot.Camera, &Value);
    return GR_OK;
}

// LAB_00450bc0 -- slot +0x3c, FUN_00556f10.
GENRESULT DACOM_API FThornCamera::SetFar(const float* Far)
{
    FGfCameraSlot& Slot = PoolSlotAt(PoolSlot);
    float Value;
    std::memcpy(&Value, Far, sizeof(Value));
    std::memcpy(&Slot.ZFar, &Value, sizeof(Value));
    if (PoolSlot == G_GfCameraPool.ActiveIndex)
        RoomCameraSetZFarValue(Slot.Camera, &Value);
    return GR_OK;
}

// LAB_00450c00 / LAB_00450c20 / LAB_00450c40.
GENRESULT DACOM_API FThornCamera::GetFov(float* OutFov)
{
    *OutFov = PoolSlotAt(PoolSlot).HalfFovXDeg;
    return GR_OK;
}

GENRESULT DACOM_API FThornCamera::GetNear(float* OutNear)
{
    *OutNear = PoolSlotAt(PoolSlot).ZNear;
    return GR_OK;
}

GENRESULT DACOM_API FThornCamera::GetFar(float* OutFar)
{
    *OutFar = PoolSlotAt(PoolSlot).ZFar;
    return GR_OK;
}

// LAB_00450860 -- G_GfCameraPool.Free(+0x6c). +0x6c keeps its value.
void FThornCamera::Stop()
{
    G_GfCameraPool.Free(PoolSlot);
}

// FUN_00450870 (ICF-folded with every "-1" getter of the exe).
void* FThornCamera::GetEngineInstance()
{
    return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
}

// ---------------------------------------------------------------------------------------
// Thorn_CameraReference -- every forwarder: no target -> the "Using invalid camera reference"
// FDUMP and -1; otherwise the target's same facet slot.
// ---------------------------------------------------------------------------------------

// FUN_00450c60.
FThornCameraReference::FThornCameraReference(Csys const& ScriptFrame, FGfScript* InOwner,
                                             FThornEntitySnap const* Desc)
    : FThornComponent(ScriptFrame, InOwner, Desc),
      Target(nullptr)
{
}

// LAB_00450dc0.
GENRESULT DACOM_API FThornCameraReference::SetTransform(float const* Transform12)
{
    if (Target == nullptr)
        return ReportInvalidReference(Name, 0x226);
    return Target->SetTransform(Transform12);
}

// LAB_00450ea0.
GENRESULT DACOM_API FThornCameraReference::GetTransform(float* OutTransform12)
{
    if (Target == nullptr)
        return ReportInvalidReference(Name, 0x24d);
    return Target->GetTransform(OutTransform12);
}

// LAB_00450d20.
GENRESULT DACOM_API FThornCameraReference::SetPosition(float* Position)
{
    if (Target == nullptr)
        return ReportInvalidReference(Name, 0x20c);
    return Target->SetPosition(Position);
}

// LAB_00450d70.
GENRESULT DACOM_API FThornCameraReference::SetOrientation(float* Orient9)
{
    if (Target == nullptr)
        return ReportInvalidReference(Name, 0x219);
    return Target->SetOrientation(Orient9);
}

// LAB_00450e00.
GENRESULT DACOM_API FThornCameraReference::GetPosition(float* OutPosition)
{
    if (Target == nullptr)
        return ReportInvalidReference(Name, 0x233);
    return Target->GetPosition(OutPosition);
}

// LAB_00450e50.
GENRESULT DACOM_API FThornCameraReference::GetOrientation(float* OutOrient9)
{
    if (Target == nullptr)
        return ReportInvalidReference(Name, 0x240);
    return Target->GetOrientation(OutOrient9);
}

// LAB_00450ee0.
GENRESULT DACOM_API FThornCameraReference::SetFov(const float* Fov)
{
    if (Target == nullptr)
        return ReportInvalidReference(Name, 0x25a);
    return Target->SetFov(Fov);
}

// LAB_00450f30.
GENRESULT DACOM_API FThornCameraReference::SetNear(const float* Near)
{
    if (Target == nullptr)
        return ReportInvalidReference(Name, 0x267);
    return Target->SetNear(Near);
}

// LAB_00450f80.
GENRESULT DACOM_API FThornCameraReference::SetFar(const float* Far)
{
    if (Target == nullptr)
        return ReportInvalidReference(Name, 0x274);
    return Target->SetFar(Far);
}

// LAB_00450fd0.
GENRESULT DACOM_API FThornCameraReference::GetFov(float* OutFov)
{
    if (Target == nullptr)
        return ReportInvalidReference(Name, 0x281);
    return Target->GetFov(OutFov);
}

// LAB_00451020.
GENRESULT DACOM_API FThornCameraReference::GetNear(float* OutNear)
{
    if (Target == nullptr)
        return ReportInvalidReference(Name, 0x28e);
    return Target->GetNear(OutNear);
}

// LAB_00451070.
GENRESULT DACOM_API FThornCameraReference::GetFar(float* OutFar)
{
    if (Target == nullptr)
        return ReportInvalidReference(Name, 0x29b);
    return Target->GetFar(OutFar);
}

// LAB_00450cd0 -- +0x6c = 0 (shared with the light, marker and sound references).
void FThornCameraReference::Stop()
{
    Target = nullptr;
}

// LAB_00450ce0.
void* FThornCameraReference::GetEngineInstance()
{
    if (Target == nullptr)
    {
        ReportInvalidReference(Name, 0x1ff);
        return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    }
    return Target->GetEngineInstance();
}
