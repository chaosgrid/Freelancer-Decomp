// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/win3d_engine_submit.h"

#include "common_csys.h"
#include "DALib.h"
#include "dacom.h"
#include "Core/game_options.h"
#include "Core/globals.h"
#include "render/IFLAppearance.h"
#include "render/IMaterial.h"
#include "render/IRenderPipeline.h"
#include "render/rendcomp.h"
#include "x86math.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <windows.h>

FSpaceCamera G_Win3DSceneCamera;

// DAT_0067dd10
I3DMathEngine* G_Win3DObjectMath = nullptr;

namespace
{
    constexpr float kOne = 1.0f;

    // RS 0x0e / 0x17.
    constexpr D3DRENDERSTATETYPE kRsZWriteEnable = D3DRS_ZWRITEENABLE;
    constexpr D3DRENDERSTATETYPE kRsDepthFunction = D3DRS_ZFUNC;

    // LAB_0059d020. Retail's atexit dtor only puts the camera vtables back -- nothing for the
    // port's plain FSpaceCamera to undo.
    struct FWin3DSceneCameraStaticInit
    {
        FWin3DSceneCameraStaticInit()
        {
            SpaceCameraConstruct(&G_Win3DSceneCamera);
        }
    };
    FWin3DSceneCameraStaticInit G_Win3DSceneCameraStaticInit;

    // DAT_0067dd4c -- the ray-box test's lazily acquired 3DMathEngine.
    I3DMathEngine* G_RayBoxMathEngine = nullptr;

    IRenderPipeline* GetPipe()
    {
        return DALib::Pipeline;
    }

    // DAT_00667e38 -- the material and tinted submits' lazily acquired 3DMathEngine; nothing
    // releases it.
    I3DMathEngine* G_Win3DSubmitMath = nullptr;

    // The scaled-identity block the three submits each inline: Scale on the local identity's
    // rotation, in place.
    void ScaleIdentityRotation(I3DMathEngine*& Cache, float* Mtx12, float Scale)
    {
        if (Cache == nullptr)
            Cache = Acquire3DMathEngine();
        Cache->Scale(*reinterpret_cast<FMatrix*>(Mtx12), *reinterpret_cast<FMatrix const*>(Mtx12),
                     Scale);
    }

    void ApplyDepthStates(IRenderPipeline* Pipe, unsigned char bDepthTested)
    {
        // Non-zero: D3DCMP_LESSEQUAL (4), no z write; else D3DCMP_ALWAYS (8), z write on.
        if (bDepthTested != 0)
        {
            Pipe->SetRenderState(kRsDepthFunction, 4);
            Pipe->SetRenderState(kRsZWriteEnable, 0);
        }
        else
        {
            Pipe->SetRenderState(kRsDepthFunction, 8);
            Pipe->SetRenderState(kRsZWriteEnable, 1);
        }
    }

    void FillIdentity3x4(float Out[12])
    {
        memset(Out, 0, 12 * sizeof(float));
        Out[0] = 1.0f;
        Out[4] = 1.0f;
        Out[8] = 1.0f;
    }

    bool SlabHitStrict(float OriginA, float OriginB, float OriginC, float DirA, float DirB,
                       float DirC, float HalfB, float HalfC)
    {
        // t is rounded to float.
        if (DirA == 0.0f)
            return false;
        float const T = -(OriginA / DirA);
        if (!(T > 0.0f))
            return false;
        if (!(std::fabs(OriginB + DirB * T) < HalfB))
            return false;
        if (!(std::fabs(OriginC + DirC * T) < HalfC))
            return false;
        return true;
    }
}

FSpaceCamera* GetWin3DSceneCamera()
{
    // FUN_0059da60. The camera's viewport is written only by Win3DSetViewportSize, with the
    // created surface size.
    return &G_Win3DSceneCamera;
}

void Win3DUnprojectScreenRayDir(FSpaceCamera* Cam, float ScreenX, float ScreenY, float OutDir[3])
{
    // LAB_0055b660 + LAB_0040f520: the near-plane point through the camera's rotation, then
    // normalized.
    if (Cam == nullptr || OutDir == nullptr)
        return;

    float const Lx = (ScreenX - Cam->ScreenCenterX) * (Cam->ZNear / Cam->XScale);
    float const Ly = (ScreenY - Cam->ScreenCenterY) * (Cam->ZNear / Cam->YScale);
    float const Lz = -Cam->ZNear;

    float const* R = Cam->World;
    float Wx = R[0] * Lx + R[1] * Ly + R[2] * Lz;
    float Wy = R[3] * Lx + R[4] * Ly + R[5] * Lz;
    float Wz = R[6] * Lx + R[7] * Ly + R[8] * Lz;

    float const LenSq = Wx * Wx + Wy * Wy + Wz * Wz;
    if (LenSq > 0.0f)
    {
        float const InvLen = 1.0f / std::sqrt(LenSq);
        Wx *= InvLen;
        Wy *= InvLen;
        Wz *= InvLen;
    }
    OutDir[0] = Wx;
    OutDir[1] = Wy;
    OutDir[2] = Wz;
}

void SubmitWin3DEngineInstance(FClientMeshHolder* Holder, void* ArchAnchor,
                               unsigned char bDepthTested, float Scale, unsigned long Flags)
{
    // FUN_0059efa0 (thiscall on the holder, RET 0x10). No test of the holder, its instance, the
    // renderer or the pipeline; the depth states are not restored.
    ApplyDepthStates(GetPipe(), bDepthTested);

    // Scale != 1 passes a scaled identity with Flags | 0x30; else no matrix.
    void* Matrix = nullptr;
    unsigned long SlotFlags = Flags;
    float ScaleMtx[12];
    if (Scale != kOne)
    {
        FillIdentity3x4(ScaleMtx);
        ScaleIdentityRotation(G_Win3DObjectMath, ScaleMtx, Scale);
        Matrix = ScaleMtx;
        SlotFlags |= 0x30u;
    }

    DALib::Renderer->RenderInstance(ArchAnchor, Holder->EngineInstance, 1.0f, SlotFlags, Matrix, nullptr);
}

void SubmitWin3DTintedEngineInstance(void* ArchAnchor, void* EngineInstance,
                                     unsigned char MaterialSelector, unsigned long Flags,
                                     unsigned char const ColorRgba[4],
                                     unsigned char bDepthTested, float Scale)
{
    // FUN_00434310 (cdecl, 7 args).
    if (GetGameOption(kGameOptShipWires) == 0)
        return;
    if (EngineInstance == reinterpret_cast<void*>(static_cast<ptrdiff_t>(-1)))
        return;

    // The two depth states saved, then set; restored at the end.
    IRenderPipeline* const Pipe = GetPipe();
    int SavedDepthFunc = 0;
    int SavedZWrite = 0;
    Pipe->GetRenderState(kRsDepthFunction, &SavedDepthFunc);
    Pipe->GetRenderState(kRsZWriteEnable, &SavedZWrite);
    ApplyDepthStates(Pipe, bDepthTested);

    // Neither GetOverrideMaterial's result nor the material is tested, as retail.
    IMaterial* Material = nullptr;
    IFLAppearance* Ifl = nullptr;
    if (DALib::Engine->QueryInterface(Iids::FLAppearance, reinterpret_cast<void**>(&Ifl)) >= 0)
    {
        void* OutMat = nullptr;
        Ifl->GetOverrideMaterial(static_cast<unsigned long>(MaterialSelector), &OutMat);
        Material = reinterpret_cast<IMaterial*>(OutMat);
        Ifl->Release();
    }
    else if (Ifl != nullptr)
    {
        Ifl->Release();
    }

    // Parameter 0 = the colour (value type 4, each byte times the double 1/255, as floats).
    alignas(4) unsigned char ValueBlob[4 + 16];
    *reinterpret_cast<unsigned long*>(ValueBlob) = 4u;
    float* const Floats = reinterpret_cast<float*>(ValueBlob + 4);
    double const ByteToUnit = 0.00392156862745098; // DAT_005c89c8
    Floats[0] = static_cast<float>(static_cast<double>(ColorRgba[0]) * ByteToUnit);
    Floats[1] = static_cast<float>(static_cast<double>(ColorRgba[1]) * ByteToUnit);
    Floats[2] = static_cast<float>(static_cast<double>(ColorRgba[2]) * ByteToUnit);
    Floats[3] = static_cast<float>(static_cast<double>(ColorRgba[3]) * ByteToUnit);
    Material->SetParameterValue(0, ValueBlob);

    void* Matrix = nullptr;
    unsigned long SlotFlags = Flags;
    float ScaleMtx[12];
    if (Scale != kOne)
    {
        FillIdentity3x4(ScaleMtx);
        ScaleIdentityRotation(G_Win3DSubmitMath, ScaleMtx, Scale);
        Matrix = ScaleMtx;
        SlotFlags |= 0x30u;
    }

    DALib::Renderer->RenderInstance(ArchAnchor, EngineInstance, 1.0f, SlotFlags, Matrix, Material);
    Pipe->SetRenderState(kRsDepthFunction, SavedDepthFunc);
    Pipe->SetRenderState(kRsZWriteEnable, SavedZWrite);
}

void SubmitWin3DTintedEngineInstanceWrapped(void* ArchAnchor, void* EngineInstance,
                                            unsigned char bFrontCull,
                                            unsigned char const ColorRgba[4],
                                            unsigned char bDepthTested, float Scale,
                                            unsigned long Flags)
{
    // FUN_00434630.
    unsigned long OrFlags = Flags | 0x8200u;
    if (bFrontCull == 0)
        OrFlags |= 0x2000u;
    // The material selector is the constant 1, not the depth byte.
    SubmitWin3DTintedEngineInstance(ArchAnchor, EngineInstance, 1, OrFlags, ColorRgba,
                                    bDepthTested, Scale);
}

int SubmitWin3DMaterialEngineInstance(void* Camera, void* EngineInstance, unsigned char bFrontCull,
                                      unsigned char const ColorRgba[4],
                                      unsigned char bDepthTested, float Scale,
                                      unsigned char bParam10, unsigned long Flags)
{
    // FUN_00433f40
    if (EngineInstance == reinterpret_cast<void*>(static_cast<ptrdiff_t>(-1)))
        return 1;

    IRenderPipeline* const Pipe = DALib::Pipeline;
    int SavedDepthFunc = 0;
    int SavedZWrite = 0;
    Pipe->GetRenderState(kRsDepthFunction, &SavedDepthFunc);
    Pipe->GetRenderState(kRsZWriteEnable, &SavedZWrite);
    ApplyDepthStates(Pipe, bDepthTested);

    // Retail does not test the material before using it.
    IMaterial* Material = nullptr;
    IFLAppearance* Ifl = nullptr;
    if (DALib::Engine->QueryInterface(Iids::FLAppearance, reinterpret_cast<void**>(&Ifl)) >= 0)
    {
        void* OutMat = nullptr;
        Ifl->GetOverrideMaterial(0, &OutMat);
        Material = reinterpret_cast<IMaterial*>(OutMat);
        Ifl->Release();
    }
    else if (Ifl != nullptr)
    {
        Ifl->Release();
    }

    // Parameter 0 = the colour (type 4), parameter 0xa = bParam10 (type 1).
    alignas(4) unsigned char ColourValue[4 + 16];
    *reinterpret_cast<unsigned long*>(ColourValue) = 4u;
    float* const Colour = reinterpret_cast<float*>(ColourValue + 4);
    double const ByteToUnit = 0.00392156862745098; // DAT_005c89c8
    Colour[0] = static_cast<float>(static_cast<double>(ColorRgba[0]) * ByteToUnit);
    Colour[1] = static_cast<float>(static_cast<double>(ColorRgba[1]) * ByteToUnit);
    Colour[2] = static_cast<float>(static_cast<double>(ColorRgba[2]) * ByteToUnit);
    Colour[3] = static_cast<float>(static_cast<double>(ColorRgba[3]) * ByteToUnit);
    Material->SetParameterValue(0, ColourValue);
    alignas(4) unsigned char FlagValue[4 + 16];
    memset(FlagValue, 0, sizeof(FlagValue));
    *reinterpret_cast<unsigned long*>(FlagValue) = 1u;
    *reinterpret_cast<unsigned long*>(FlagValue + 4) = (bParam10 != 0) ? 1u : 0u;
    Material->SetParameterValue(0xa, FlagValue);

    unsigned long SlotFlags = Flags | 0x100u;
    if (bFrontCull == 0)
        SlotFlags |= 0x2000u;
    void* Matrix = nullptr;
    float ScaleMtx[12];
    if (Scale != 1.0f)
    {
        FillIdentity3x4(ScaleMtx);
        ScaleIdentityRotation(G_Win3DSubmitMath, ScaleMtx, Scale);
        Matrix = ScaleMtx;
        SlotFlags |= 0x30u;
    }
    int const Result =
        DALib::Renderer->RenderInstance(Camera, EngineInstance, 1.0f, SlotFlags, Matrix, Material);

    Pipe->SetRenderState(kRsDepthFunction, SavedDepthFunc);
    Pipe->SetRenderState(kRsZWriteEnable, SavedZWrite);
    return Result;
}

void SubmitWin3DOutlinedEngineInstance(void* Camera, void* EngineInstance,
                                       unsigned char bFrontCull, unsigned char const ColorRgba[4],
                                       float Scale, unsigned long Flags)
{
    // FUN_00434670
    unsigned char const Black[4] = {0, 0, 0, 0};
    SubmitWin3DMaterialEngineInstance(Camera, EngineInstance, bFrontCull, Black, 0, Scale, 0,
                                      Flags);
    unsigned long OrFlags = Flags | 0x8100u;
    if (bFrontCull == 0)
        OrFlags |= 0x2000u;
    SubmitWin3DTintedEngineInstance(Camera, EngineInstance, 1, OrFlags, ColorRgba, 1, Scale);
}

bool Win3DRayHitsAabbHalfExtents(float const HalfExtents[3], float const Matrix3x4[12],
                                 float const RayDir[3])
{
    // FUN_005a1130 (thiscall on HalfExtents, RET 8). The ray direction is inverse-transformed
    // as a point: LocalDir = Out + Translation, LocalOrigin = -Translation. Then the plane
    // crossing on each axis whose direction is non-zero: t > 0 and both other coordinates
    // strictly inside their halves.
    if (HalfExtents[0] == 0.0f && HalfExtents[1] == 0.0f && HalfExtents[2] == 0.0f)
        return false;

    if (G_RayBoxMathEngine == nullptr)
        G_RayBoxMathEngine = Acquire3DMathEngine();

    static_assert(sizeof(FTransform) == 12 * sizeof(float), "FTransform is the 3x4 transform");
    FVector Out;
    G_RayBoxMathEngine->InverseTransform(Out, *reinterpret_cast<FTransform const*>(Matrix3x4),
                                         *reinterpret_cast<FVector const*>(RayDir));

    float const LocalDir[3] = {Out.X + Matrix3x4[9], Out.Y + Matrix3x4[10],
                               Out.Z + Matrix3x4[11]};
    float const LocalOrigin[3] = {-Matrix3x4[9], -Matrix3x4[10], -Matrix3x4[11]};

    if (SlabHitStrict(LocalOrigin[0], LocalOrigin[1], LocalOrigin[2], LocalDir[0], LocalDir[1],
                      LocalDir[2], HalfExtents[1], HalfExtents[2]))
        return true;
    if (SlabHitStrict(LocalOrigin[1], LocalOrigin[0], LocalOrigin[2], LocalDir[1], LocalDir[0],
                      LocalDir[2], HalfExtents[0], HalfExtents[2]))
        return true;
    if (SlabHitStrict(LocalOrigin[2], LocalOrigin[0], LocalOrigin[1], LocalDir[2], LocalDir[0],
                      LocalDir[1], HalfExtents[0], HalfExtents[1]))
        return true;
    return false;
}
