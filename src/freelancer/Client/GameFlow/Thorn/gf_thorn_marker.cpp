// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "GameFlow/Thorn/gf_thorn_marker.h"

#include "GameFlow/Thorn/gf_thorn_light.h" // the reference forwarders shared with the light reference

#include <cstdint>
#include <cstring>

// ---------------------------------------------------------------------------------------
// Thorn_Marker
// ---------------------------------------------------------------------------------------

// FUN_00453690.
FThornMarker::FThornMarker(Csys const& ScriptFrame, FGfScript* InOwner,
                           FThornEntitySnap const* Desc)
    : FThornComponent(ScriptFrame, InOwner, Desc)
{
    World.Pos.x = 0.0f;
    World.Pos.y = 0.0f;
    World.Pos.z = 0.0f;
    MatrixSetIdentity(World.Orient);
}

// LAB_004537d0.
GENRESULT DACOM_API FThornMarker::SetTransform(float const* Transform12)
{
    World.Pos = Frame.trans_from_frame(*reinterpret_cast<Vector const*>(Transform12 + 9));
    World.Orient = Frame.trans_from_frame(*reinterpret_cast<Matrix const*>(Transform12));
    return GR_OK;
}

// LAB_004538b0.
GENRESULT DACOM_API FThornMarker::GetTransform(float* OutTransform12)
{
    Vector const LocalPos = Frame.trans_to_frame(World.Pos);
    OutTransform12[9] = LocalPos.x;
    OutTransform12[10] = LocalPos.y;
    OutTransform12[11] = LocalPos.z;
    Matrix const LocalOrient = Frame.trans_to_frame(World.Orient);
    memcpy(OutTransform12, &LocalOrient, sizeof(float) * 9);
    return GR_OK;
}

// LAB_00453750.
GENRESULT DACOM_API FThornMarker::SetPosition(float* Position)
{
    World.Pos = Frame.trans_from_frame(*reinterpret_cast<Vector const*>(Position));
    return GR_OK;
}

// LAB_00453790.
GENRESULT DACOM_API FThornMarker::SetOrientation(float* Orient9)
{
    World.Orient = Frame.trans_from_frame(*reinterpret_cast<Matrix const*>(Orient9));
    return GR_OK;
}

// LAB_00453830.
GENRESULT DACOM_API FThornMarker::GetPosition(float* OutPosition)
{
    Vector const Local = Frame.trans_to_frame(World.Pos);
    OutPosition[0] = Local.x;
    OutPosition[1] = Local.y;
    OutPosition[2] = Local.z;
    return GR_OK;
}

// LAB_00453870.
GENRESULT DACOM_API FThornMarker::GetOrientation(float* OutOrient9)
{
    Matrix const Local = Frame.trans_to_frame(World.Orient);
    memcpy(OutOrient9, &Local, sizeof(float) * 9);
    return GR_OK;
}

// FUN_00450870.
void* FThornMarker::GetEngineInstance()
{
    return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
}

// ---------------------------------------------------------------------------------------
// Thorn_MarkerReference
// ---------------------------------------------------------------------------------------

// FUN_00453910.
FThornMarkerReference::FThornMarkerReference(Csys const& ScriptFrame, FGfScript* InOwner,
                                             FThornEntitySnap const* Desc)
    : FThornComponent(ScriptFrame, InOwner, Desc),
      Target(nullptr)
{
}

GENRESULT DACOM_API FThornMarkerReference::SetTransform(float const* Transform12)
{
    return ThornReferenceSetTransform(Target, Transform12);
}

GENRESULT DACOM_API FThornMarkerReference::GetTransform(float* OutTransform12)
{
    return ThornReferenceGetTransform(Target, OutTransform12);
}

GENRESULT DACOM_API FThornMarkerReference::SetPosition(float* Position)
{
    return ThornReferenceSetPosition(Target, Position);
}

GENRESULT DACOM_API FThornMarkerReference::SetOrientation(float* Orient9)
{
    return ThornReferenceSetOrientation(Target, Orient9);
}

GENRESULT DACOM_API FThornMarkerReference::GetPosition(float* OutPosition)
{
    return ThornReferenceGetPosition(Target, OutPosition);
}

GENRESULT DACOM_API FThornMarkerReference::GetOrientation(float* OutOrient9)
{
    return ThornReferenceGetOrientation(Target, OutOrient9);
}

// LAB_00450cd0.
void FThornMarkerReference::Stop()
{
    Target = nullptr;
}

// LAB_00453550.
void* FThornMarkerReference::GetEngineInstance()
{
    return ThornReferenceGetEngineInstance(Target);
}
