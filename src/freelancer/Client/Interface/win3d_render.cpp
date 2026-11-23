// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/win3d_render.h"
#include "Interface/win3d_immediate.h"

#include "DALib.h"
#include "Core/globals.h"
#include "render/IRenderPipeline.h"
#include "Space/space_state.h"
#include "common_misc.h"

#include <string.h>

#include <windows.h>

// The depth cache and the glEnable/glDisable translations live in win3d_immediate.cpp.

void ClearWin3DBuffers(unsigned int Bits)
{
    // FUN_004215e0. No null test on the pipeline.
    IRenderPipeline* const Pipe = DALib::Pipeline;

    unsigned long ClearFlags = (Bits & kWin3DClearColour) != 0 ? 1u : 0u;
    if ((Bits & kWin3DClearDepth) != 0)
        ClearFlags |= 2u;

    // The depth range is reset to [0, 1] on every clear, before the clear itself.
    Pipe->SetDepthRange(0.0f, 1.0f);
    // The pipeline is re-read for the second call, as retail.
    DALib::Pipeline->ClearBuffers(ClearFlags, nullptr);
}

void ClearWin3DBuffersToColor(unsigned char const* Rgba)
{
    // FUN_00564f90. Each byte times the double 1/255, rounded to float.
    double const Scale = 1.0 / 255.0;
    Win3DSetPipelineClearColor(static_cast<float>(Rgba[0] * Scale), static_cast<float>(Rgba[1] * Scale),
                               static_cast<float>(Rgba[2] * Scale), static_cast<float>(Rgba[3] * Scale));
    ClearWin3DBuffers(kWin3DClearColour | kWin3DClearDepth);
}

void Win3DApplyCamera(ICamera* Camera)
{
    // FUN_00422950. No null checks: retail dereferences the camera and the pipeline
    // unconditionally. SetWorld gets the camera's own view pointer.
    float const ZFar = Camera->GetZFar();
    float const ZNear = Camera->GetZNear();
    float const Aspect = Camera->GetAspect();
    float const FovY = Camera->GetFovYDeg();
    Win3DSetPerspective(FovY, Aspect, ZNear, ZFar);

    float const* const View12 = Camera->GetViewTransform();
    memcpy(GWin3DModelview, View12, sizeof(float) * 12);

    IRenderPipeline* const Pipe = DALib::Pipeline;
    int bViewspace = 1;                           // the out value is seeded with 1
    Pipe->GetPipelineState(7, &bViewspace);
    if (bViewspace != 0)
    {
        Pipe->SetModelview(GWin3DModelview);
        return;
    }

    float Identity[12] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                          0.0f, 0.0f, 0.0f};
    Pipe->SetView(Identity);
    Pipe->SetWorld(View12);
}
