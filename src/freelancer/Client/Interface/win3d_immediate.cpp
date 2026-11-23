// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// The OpenGL immediate-mode emulator (retail `E:\FL\Scratch\Source\Client\cmn\OpenGL.cpp`), and
// at the end of the file the fuzzy-line drawers with their vector helpers.

#include "Interface/win3d_immediate.h"

#include "Core/game_options.h"
#include "Space/Cameras/space_camera.h"
#include "Space/star_sphere.h"
#include "Common/engine_light.h"
#include "Interface/win3d.h"

#include "common_misc.h"
#include "DALib.h"
#include "dacom.h"
#include "Core/globals.h"
#include "render/IRenderPipeline.h"
#include "render/shading.h"
#include "x86math.h"

#include <math.h>
#include <string.h>

namespace
{
    constexpr D3DRENDERSTATETYPE kRsZEnable = D3DRS_ZENABLE;
    constexpr D3DRENDERSTATETYPE kRsZWriteEnable = D3DRS_ZWRITEENABLE;
    constexpr D3DRENDERSTATETYPE kRsSrcBlend = D3DRS_SRCBLEND;
    constexpr D3DRENDERSTATETYPE kRsDestBlend = D3DRS_DESTBLEND;
    constexpr D3DRENDERSTATETYPE kRsZFunc = D3DRS_ZFUNC;
    constexpr D3DRENDERSTATETYPE kRsCullMode = D3DRS_CULLMODE;
    constexpr D3DRENDERSTATETYPE kRsAlphaBlendEnable = D3DRS_ALPHABLENDENABLE;
    constexpr D3DRENDERSTATETYPE kRsClipping = D3DRS_CLIPPING;
    constexpr D3DRENDERSTATETYPE kRsLighting = D3DRS_LIGHTING;
    constexpr D3DRENDERSTATETYPE kRsColorVertex = D3DRS_COLORVERTEX;
    constexpr D3DRENDERSTATETYPE kRsDiffuseMaterialSource = D3DRS_DIFFUSEMATERIALSOURCE;

    // D3DTSS_*, the six the two tables below drive.
    enum EWin3DStageState
    {
        kTssColorOp   = 1,
        kTssColorArg1 = 2,
        kTssColorArg2 = 3,
        kTssAlphaOp   = 4,
        kTssAlphaArg1 = 5,
        kTssAlphaArg2 = 6
    };

    // .rdata 0x005c96c0 -- what a textured stage is set to: MODULATE of the texture against the
    // vertex diffuse, on both colour and alpha.
    const int kStageTextured[6] = {4, 2, 0, 4, 2, 0};

    // .rdata 0x005c96f8 -- and what an untextured stage is set to: DISABLE.
    const int kStageDisabled[6] = {1, 2, 1, 1, 2, 1};

    // XYZ | DIFFUSE | TEX1.
    const unsigned kWin3DFvf = 0x142;

    // DAT_006168a4 / DAT_006168b0: retail keeps the capacities as globals, but every writer
    // stores 0x3000, so a constant is the same value.
    const int kMaxVertices = 0x3000;
    const int kMaxIndices = 0x3000;

    // The quad path caps the index count it will triangulate.
    const unsigned kMaxQuadIndices = 0xaaa;

    // The accumulator. Retail's vertex buffer is .data 0x006168b8 with a 0x18 stride, and the
    // index buffer is .data 0x0065e8b8, sixteen bits per entry.
    struct FWin3DVertex
    {
        float         X;
        float         Y;
        float         Z;
        unsigned long Diffuse;
        float         U;
        float         V;
    };
    static_assert(sizeof(FWin3DVertex) == 0x18, "the FVF 0x142 vertex is 24 bytes");

    FWin3DVertex   G_Vertices[kMaxVertices];
    unsigned short G_Indices[kMaxIndices];

    // DAT_00616888 / DAT_006168a8 / DAT_006168b4 / DAT_006168ac.
    int           G_PrimitiveMode = -1;
    int           G_VertexCount = 0;
    int           G_IndexCount = 0;
    unsigned char G_bBufferLocked = 0;

    // DAT_00616898 / DAT_0061689c / DAT_006168a0 -- the current vertex attributes. Only the
    // colour setters write the colour.
    unsigned long G_CurrentColour = 0;
    float         G_CurrentU = 0.0f;
    float         G_CurrentV = 0.0f;

    // DAT_006648d0 / DAT_006648d4 -- whether a texture is bound, and which.
    unsigned char G_bTextureBound = 0;
    void*         G_BoundTexture = nullptr;

    // DAT_006648d8 -- the lighting flag glEnd installs while it draws.
    unsigned char G_bLighting = 0;

    // DAT_006648b8..DAT_006648cc -- the cached render state the setters below re-apply alongside
    // the one they change. Initial values from the static initializer LAB_00421160, the same ones
    // Win3DResetState restores.
    int           G_AlphaBlendEnable = 0;
    int           G_SrcBlend = 2;
    int           G_DestBlend = 2;
    unsigned char G_bDepthTest = 1;
    int           G_DepthWrite = 1;
    int           G_DepthFunc = 4;

    // The pipeline-state id the projection setter asks about. Non-zero means the pipeline wants
    // one combined modelview rather than a separate world and view.
    const int kPipelineStateViewspace = 7;

    // The only kind the ortho setter stores, so it doubles as "an ortho projection is
    // installed".
    const int kProjectionKindOrtho = 2;

    const int kProjectionParamCount = 6;

    // DAT_00610844 -- 1 in .data, so the cache is on out of the box.
    unsigned char G_bProjectionCacheEnabled = 1;

    // DAT_0066490c / DAT_00664910 / DAT_00664990 / DAT_0066498c.
    int   G_ProjectionKind = 0;
    float G_ProjectionParams[kProjectionParamCount] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    int   G_ProjectionSetCount = 0;
    int   G_ProjectionSkipCount = 0;

    // The perspective setter's own cache kind, sharing the ortho cache but only ever comparing /
    // storing the first four slots.
    const int kProjectionKindPerspective = 1;

    // DAT_00664928 / DAT_0066492c / DAT_00664948 -- Win3DPushProjection's shadow copy: the saved
    // cache kind (0 = "nothing saved" / "matrix path"), the saved cache params, and the raw
    // projection matrix saved when the cache is off.
    int   G_SavedProjectionKind = 0;
    float G_SavedProjectionParams[kProjectionParamCount] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    float G_SavedProjectionMatrix[16] = {};

    // DAT_00664994..DAT_006649a0 -- the last viewport Win3DSetViewport installed, kept only for
    // Win3DGetViewport. The stored Y is the flipped origin, as retail.
    int G_ViewportLeft = 0;
    int G_ViewportFlippedTop = 0;
    int G_ViewportWidth = 0;
    int G_ViewportHeight = 0;

    const int kWin3DQueryViewport = 0xba2;

    // No function in OpenGL.cpp tests the pipeline for null before calling through it.
    IRenderPipeline* GetPipeline()
    {
        return DALib::Pipeline;
    }

    void ApplyStageTable(IRenderPipeline* Pipe, int Stage, const int* Table)
    {
        // Both retail loops walk a {state, value} table whose terminator is a zero STATE, and
        // both start from state 1 -- so the six entries are states 1..6 in order.
        for (int I = 0; I < 6; ++I)
            Pipe->SetTextureStageState(Stage, kTssColorOp + I, Table[I]);
    }

    // FUN_00421290. Decides what the two texture stages should be doing, given whether a texture
    // is bound and whether the pipeline still recognises it.
    void ApplyTextureStages()
    {
        IRenderPipeline* const Pipe = GetPipeline();

        if (G_bTextureBound == 0)
        {
            Pipe->SetTextureStageTexture(0, nullptr);
            ApplyStageTable(Pipe, 0, kStageDisabled);
            return;
        }

        // A handle the pipeline no longer knows leaves the stage disabled, which is what stops a
        // freed texture from being drawn with.
        const GENRESULT Known = Pipe->IsTexture(G_BoundTexture);
        if (Known != GR_OK)
        {
            Pipe->SetTextureStageTexture(0, nullptr);
            ApplyStageTable(Pipe, 0, kStageDisabled);
            // The stale handle is dropped (retail stores the table loop's zero terminator); the
            // bound flag stays set.
            G_BoundTexture = nullptr;
            return;
        }

        Pipe->SetTextureStageTexture(0, G_BoundTexture);

        // Stage state 0xb (D3D8 D3DTSS_TEXCOORDINDEX) is set to 0 before the table runs.
        Pipe->SetTextureStageState(0, 0xb, 0);
        ApplyStageTable(Pipe, 0, kStageTextured);

        // Stage ONE is always disabled, textured or not.
        ApplyStageTable(Pipe, 1, kStageDisabled);
    }

    // DAT_005c75d8.
    const float kColourByteScale = 255.0f;

    int ScaleClampToByte(float Component)
    {
        // x87: the product stays double, ftol truncates, and the clamp comes afterwards, so it
        // catches what the conversion produced, not what it was given.
        const double Scaled = static_cast<double>(Component) * static_cast<double>(kColourByteScale);
        int Value = static_cast<int>(Scaled);
        if (Value < 0)
            Value = 0;
        else if (Value > 0xff)
            Value = 0xff;
        return Value;
    }
}

float GWin3DModelview[12] = {1.0f, 0.0f, 0.0f,
                             0.0f, 1.0f, 0.0f,
                             0.0f, 0.0f, 1.0f,
                             0.0f, 0.0f, 0.0f};

void Win3DResetState()
{
    // FUN_004213d0 (cdecl; the caller's surface width and height are not read). The same stores
    // as the static initializer LAB_00421160.
    G_SrcBlend = 2;
    G_DestBlend = 2;
    G_PrimitiveMode = -1;
    G_VertexCount = 0;
    G_bBufferLocked = 0;
    G_IndexCount = 0;
    GWin3DModelview[0] = 1.0f; GWin3DModelview[4] = 1.0f; GWin3DModelview[8] = 1.0f;
    GWin3DModelview[7] = 0.0f; GWin3DModelview[6] = 0.0f; GWin3DModelview[5] = 0.0f;
    GWin3DModelview[3] = 0.0f; GWin3DModelview[2] = 0.0f; GWin3DModelview[1] = 0.0f;
    GWin3DModelview[11] = 0.0f; GWin3DModelview[10] = 0.0f; GWin3DModelview[9] = 0.0f;
    G_AlphaBlendEnable = 0;
    G_bDepthTest = 1;
    G_DepthWrite = 1;
    G_DepthFunc = 4;
    G_bTextureBound = 0;
    G_BoundTexture = nullptr;
    G_bLighting = 0;
    G_ProjectionKind = 0;
    G_SavedProjectionKind = 0;
}

void Win3DBegin(int PrimitiveMode)
{
    // FUN_00421670.
    G_PrimitiveMode = PrimitiveMode;
    G_IndexCount = 0;

    // The vertex counter survives a begin while the buffer is locked, so a caller can build one
    // vertex pool and issue several index runs against it.
    if (G_bBufferLocked == 0)
        G_VertexCount = 0;
}

void Win3DVertex3f(float X, float Y, float Z)
{
    // FUN_00421cc0.
    if (G_IndexCount >= kMaxIndices || G_VertexCount >= kMaxVertices)
    {
        // The vertex is dropped; a function-static rate limiter (DAT_00616878 / DAT_0061687c)
        // warns at most once per second of client time. Only a strictly greater gap warns
        // (unordered skips).
        static float LastWarnTime = 0.0f;
        static float WarnInterval = 1.0f;
        const double Now = GetClientElapsedTime();
        if (Now - static_cast<double>(LastWarnTime) > static_cast<double>(WarnInterval))
        {
            LastWarnTime = static_cast<float>(Now);
            FDUMP(0x100002, "%s(%d) : *** WARNING: GL: %s\n",
                  "E:\\FL\\Scratch\\Source\\Client\\cmn\\OpenGL.cpp", 0x59, "glVertex overflow");
        }
        return;
    }

    // The index is written first and points at the vertex about to be appended.
    G_Indices[G_IndexCount] = static_cast<unsigned short>(G_VertexCount);
    ++G_IndexCount;

    FWin3DVertex& Vertex = G_Vertices[G_VertexCount];
    ++G_VertexCount;

    Vertex.X = X;
    Vertex.Y = Y;
    Vertex.Z = Z;
    Vertex.Diffuse = G_CurrentColour;
    Vertex.U = G_CurrentU;
    Vertex.V = G_CurrentV;
}

void Win3DColor4f(float R, float G, float B, float A)
{
    // FUN_00421bd0. Retail scales by 255 and packs ARGB.
    const int Red = ScaleClampToByte(R);
    const int Green = ScaleClampToByte(G);
    const int Blue = ScaleClampToByte(B);
    const int Alpha = ScaleClampToByte(A);

    G_CurrentColour = (static_cast<unsigned long>(Alpha) << 24) |
                      (static_cast<unsigned long>(Red) << 16) |
                      (static_cast<unsigned long>(Green) << 8) |
                      static_cast<unsigned long>(Blue);
}

void Win3DColor3f(float R, float G, float B)
{
    // FUN_00421ad0.
    const int Red = ScaleClampToByte(R);
    const int Green = ScaleClampToByte(G);
    const int Blue = ScaleClampToByte(B);

    G_CurrentColour = 0xff000000ul |
                      (static_cast<unsigned long>(Red) << 16) |
                      (static_cast<unsigned long>(Green) << 8) |
                      static_cast<unsigned long>(Blue);
}

void Win3DSetPipelineClearColor(float R, float G, float B, float A)
{
    // FUN_004214d0.
    IRenderPipeline* const Pipe = GetPipeline();

    const int Red = ScaleClampToByte(R);
    const int Green = ScaleClampToByte(G);
    const int Blue = ScaleClampToByte(B);
    const int Alpha = ScaleClampToByte(A);

    const unsigned long Packed = (static_cast<unsigned long>(Alpha) << 24) |
                                  (static_cast<unsigned long>(Red) << 16) |
                                  (static_cast<unsigned long>(Green) << 8) |
                                  static_cast<unsigned long>(Blue);

    const int kPipelineStateClearColor = 8;
    Pipe->SetPipelineState(kPipelineStateClearColor, static_cast<int>(Packed));
}

void Win3DColor3ub(unsigned char R, unsigned char G, unsigned char B)
{
    // FUN_00421a70: alpha 0xff with the three components shifted in under it.
    G_CurrentColour = ((static_cast<unsigned long>(R) | 0xffffff00u) << 8 |
                       static_cast<unsigned long>(G)) << 8 |
                      static_cast<unsigned long>(B);
}

void Win3DColor3ubv(const unsigned char* Rgb)
{
    // FUN_00421aa0.
    G_CurrentColour = ((static_cast<unsigned long>(Rgb[0]) | 0xffffff00u) << 8 |
                       static_cast<unsigned long>(Rgb[1])) << 8 |
                      static_cast<unsigned long>(Rgb[2]);
}

void Win3DColor4ub(unsigned char R, unsigned char G, unsigned char B, unsigned char A)
{
    // FUN_00421b70.
    G_CurrentColour = (static_cast<unsigned long>(A) << 24) |
                      (static_cast<unsigned long>(R) << 16) |
                      (static_cast<unsigned long>(G) << 8) |
                      static_cast<unsigned long>(B);
}

void Win3DColor4ubv(const unsigned char* Rgba)
{
    // FUN_00421ba0.
    G_CurrentColour = (static_cast<unsigned long>(Rgba[3]) << 24) |
                      (static_cast<unsigned long>(Rgba[0]) << 16) |
                      (static_cast<unsigned long>(Rgba[1]) << 8) |
                      static_cast<unsigned long>(Rgba[2]);
}

void Win3DTexCoord2f(float U, float V)
{
    // FUN_00421ca0.
    G_CurrentU = U;
    G_CurrentV = V;
}

void Win3DBindTexture(void* TextureHandle)
{
    // FUN_004220a0.
    G_BoundTexture = TextureHandle;
    G_bTextureBound = 1;
}

void Win3DEnable(int Capability)
{
    // FUN_00421dd0.
    IRenderPipeline* const Pipe = GetPipeline();

    switch (Capability)
    {
    case kWin3DDepthTest:
        // All three depth states are written, the other two from the cache. (Retail then
        // branches on the flag it has just set, to the same path either way.)
        G_bDepthTest = 1;
        Pipe->SetRenderState(kRsZFunc, G_DepthFunc);
        Pipe->SetRenderState(kRsZEnable, 1);
        Pipe->SetRenderState(kRsZWriteEnable, G_DepthWrite);
        return;

    case kWin3DCullFace:
        // D3DCULL_CW (2). glDisable writes 1 (NONE) into the same state.
        Pipe->SetRenderState(kRsCullMode, 2);
        return;

    case kWin3DLightingCap:
        // No pipeline call -- the flag is only read by glEnd.
        G_bLighting = 1;
        return;

    case kWin3DBlend:
        G_AlphaBlendEnable = 1;
        Pipe->SetRenderState(kRsAlphaBlendEnable, 1);
        return;

    case kWin3DTexture2D:
        // A flag only; the stage apply in glEnd acts on it.
        G_bTextureBound = 1;
        return;

    default:
        FDUMP(0x100002, "%s(%d) : *** WARNING: GL: %s\n",
              "E:\\FL\\Scratch\\Source\\Client\\cmn\\OpenGL.cpp", 0x287, "glEnable - invalid");
        return;
    }
}

void Win3DDisable(int Capability)
{
    // FUN_00421ed0, the mirror image.
    IRenderPipeline* const Pipe = GetPipeline();

    switch (Capability)
    {
    case kWin3DDepthTest:
        G_bDepthTest = 0;
        // The ZFUNC and ZWRITE writes still happen, from the cache -- disabling the test
        // does not disable the write.
        Pipe->SetRenderState(kRsZFunc, G_DepthFunc);
        Pipe->SetRenderState(kRsZEnable, 0);
        Pipe->SetRenderState(kRsZWriteEnable, G_DepthWrite);
        return;

    case kWin3DCullFace:
        Pipe->SetRenderState(kRsCullMode, 1);   // D3DCULL_NONE
        return;

    case kWin3DLightingCap:
        G_bLighting = 0;
        return;

    case kWin3DFog:
        // Seven fog states zeroed in this order. glEnable has no fog case.
        Pipe->SetRenderState(D3DRS_FOGENABLE, 0);
        Pipe->SetRenderState(D3DRS_FOGTABLEMODE, 0);
        Pipe->SetRenderState(D3DRS_FOGVERTEXMODE, 0);
        Pipe->SetRenderState(D3DRS_FOGCOLOR, 0);
        Pipe->SetRenderState(D3DRS_FOGSTART, 0);
        Pipe->SetRenderState(D3DRS_FOGEND, 0);
        Pipe->SetRenderState(D3DRS_FOGDENSITY, 0);
        return;

    case kWin3DBlend:
        G_AlphaBlendEnable = 0;
        Pipe->SetRenderState(kRsAlphaBlendEnable, 0);
        return;

    case kWin3DTexture2D:
        G_bTextureBound = 0;
        Pipe->SetTextureStageTexture(0, nullptr);
        return;

    default:
        FDUMP(0x100002, "%s(%d) : *** WARNING: GL: %s\n",
              "E:\\FL\\Scratch\\Source\\Client\\cmn\\OpenGL.cpp", 0x2bf, "glDisable");
        return;
    }
}

void* Win3DSetTextureFilterLinear(void* PassThrough)
{
    // FUN_004223d0. Stage 0's MAG/MIN filter, both to D3DTEXF_LINEAR (3).
    IRenderPipeline* const Pipe = GetPipeline();
    const int kTssMagFilter = 0xd;
    const int kTssMinFilter = 0xe;
    const int kTexfLinear = 3;
    Pipe->SetTextureStageState(0, kTssMagFilter, kTexfLinear);
    Pipe->SetTextureStageState(0, kTssMinFilter, kTexfLinear);
    return PassThrough;
}

void Win3DSetTextureFilterPoint()
{
    // FUN_00422410. The mirror image, D3DTEXF_POINT (1).
    IRenderPipeline* const Pipe = GetPipeline();
    const int kTssMagFilter = 0xd;
    const int kTssMinFilter = 0xe;
    const int kTexfPoint = 1;
    Pipe->SetTextureStageState(0, kTssMagFilter, kTexfPoint);
    Pipe->SetTextureStageState(0, kTssMinFilter, kTexfPoint);
}

void Win3DSetDepthFunc(int CompareFunc)
{
    // FUN_00422120
    G_DepthFunc = CompareFunc;

    IRenderPipeline* const Pipe = GetPipeline();

    Pipe->SetRenderState(kRsZFunc, CompareFunc);
    Pipe->SetRenderState(kRsZEnable, G_bDepthTest != 0 ? 1 : 0);
    Pipe->SetRenderState(kRsZWriteEnable, G_DepthWrite);
}
void Win3DSetBlendFunc(int SourceFactor, int DestFactor)
{
    // FUN_004220c0. Also re-applies the alpha-blend enable from the cache.
    G_SrcBlend = SourceFactor;
    G_DestBlend = DestFactor;

    IRenderPipeline* const Pipe = GetPipeline();

    Pipe->SetRenderState(kRsSrcBlend, SourceFactor);
    Pipe->SetRenderState(kRsDestBlend, G_DestBlend);
    Pipe->SetRenderState(kRsAlphaBlendEnable, G_AlphaBlendEnable);
}

void Win3DSetDepthWrite(int bEnabled)
{
    // FUN_00422190
    G_DepthWrite = bEnabled;

    IRenderPipeline* const Pipe = GetPipeline();

    Pipe->SetRenderState(kRsZFunc, G_DepthFunc);
    Pipe->SetRenderState(kRsZEnable, G_bDepthTest != 0 ? 1 : 0);
    Pipe->SetRenderState(kRsZWriteEnable, G_DepthWrite);
}

void Win3DSetViewport(ICamera* Cam, const char* SourceFile, int SourceLine)
{
    // FUN_00422610. Nothing consumes the file and line.
    (void)SourceFile;
    (void)SourceLine;

    IRenderPipeline* const Pipe = GetPipeline();

    // No camera: the whole surface, DAT_00610850 / DAT_00610854 (the size ApplyGameWindowLayout
    // recorded, not the requested display mode).
    int Left = 0;
    int Top = 0;
    int Width = static_cast<int>(G_RenderSurfaceWidth);
    int Height = static_cast<int>(G_RenderSurfaceHeight);

    if (Cam != nullptr)
    {
        // Inclusive edges: the extents are the difference plus one, as SpaceCameraSetViewport.
        const int* const Rect = Cam->GetViewport();
        Left = Rect[0];
        Top = Rect[1];
        Width = Rect[2] - Rect[0] + 1;
        Height = Rect[3] - Rect[1] + 1;
    }

    // Retail stores a flipped origin and hands the pipeline its flip again, which cancels back
    // to Top; the port passes Top directly. The flipped value is still stored for
    // Win3DGetViewport.
    G_ViewportLeft = Left;
    G_ViewportWidth = Width;
    G_ViewportHeight = Height;
    G_ViewportFlippedTop = static_cast<int>(G_RenderSurfaceHeight) - Height - Top;

    Pipe->SetViewportRect(Left, Top, Width, Height);
}

void Win3DGetViewport(int Query, int* Out4)
{
    // FUN_00422390. The out array is written untested.
    if (Query == kWin3DQueryViewport)
    {
        Out4[0] = G_ViewportLeft;
        Out4[1] = G_ViewportFlippedTop;
        Out4[2] = G_ViewportWidth;
        Out4[3] = G_ViewportHeight;
    }
}

void Win3DSetViewportRect(int X, int Y, int Width, int Height, const char* SourceFile,
                          int SourceLine)
{
    // FUN_00421620. The stores come first, then the pipeline with the flipped Y.
    (void)SourceFile;
    (void)SourceLine;
    G_ViewportLeft = X;
    G_ViewportFlippedTop = Y;
    G_ViewportWidth = Width;
    G_ViewportHeight = Height;
    GetPipeline()->SetViewportRect(X, static_cast<int>(G_RenderSurfaceHeight) - Y - Height, Width,
                                   Height);
}

void Win3DSetOrtho2DForCamera(FSpaceCamera* Cam)
{
    // FUN_004227d0
    const int* const Rect = AsCamera(Cam)->GetViewport();
    const float Width = static_cast<float>(Rect[2] - Rect[0] + 1);
    const float Height = static_cast<float>(Rect[3] - Rect[1] + 1);
    Win3DSetProjection(0.0f, Width, Height, 0.0f, -1.0f, 1.0f);
}

void Win3DSetProjection(float Left, float Right, float Bottom, float Top, float NearPlane,
                        float FarPlane)
{
    // FUN_004221f0. The modelview reset comes FIRST, before any cache test.
    GWin3DModelview[0] = 1.0f; GWin3DModelview[1] = 0.0f; GWin3DModelview[2] = 0.0f;
    GWin3DModelview[3] = 0.0f; GWin3DModelview[4] = 1.0f; GWin3DModelview[5] = 0.0f;
    GWin3DModelview[6] = 0.0f; GWin3DModelview[7] = 0.0f; GWin3DModelview[8] = 1.0f;
    GWin3DModelview[9] = 0.0f; GWin3DModelview[10] = 0.0f; GWin3DModelview[11] = 0.0f;

    IRenderPipeline* const Pipe = GetPipeline();
    // The out-parameter is seeded with 1, so a pipeline that does not answer takes the
    // combined-modelview path.
    int PipelineState = 1;
    Pipe->GetPipelineState(kPipelineStateViewspace, &PipelineState);

    if (PipelineState != 0)
    {
        Pipe->SetModelview(GWin3DModelview);
    }
    else
    {
        Pipe->SetWorld(GWin3DModelview);
        Pipe->SetView(GWin3DModelview);
    }

    if (G_bProjectionCacheEnabled != 0)
    {
        const float Args[kProjectionParamCount] = {Left, Right, Bottom, Top, NearPlane, FarPlane};

        // A different projection KIND always counts as a change.
        bool bChanged = (G_ProjectionKind != kProjectionKindOrtho);

        // The compare and the store share one loop, and the store is unconditional.
        for (int I = 0; I < kProjectionParamCount; ++I)
        {
            if (!bChanged && G_ProjectionParams[I] != Args[I])
                bChanged = true;
            G_ProjectionParams[I] = Args[I];
        }

        if (!bChanged)
        {
            ++G_ProjectionSkipCount;
            return;
        }

        ++G_ProjectionSetCount;
        G_ProjectionKind = kProjectionKindOrtho;
    }

    Pipe->SetOrtho(Left, Right, Bottom, Top, NearPlane, FarPlane);
}

void Win3DSetOrtho2D()
{
    // FUN_004225e0: the recorded surface size, y DOWN.
    Win3DSetProjection(0.0f, static_cast<float>(static_cast<int>(G_RenderSurfaceWidth)),
                       static_cast<float>(static_cast<int>(G_RenderSurfaceHeight)), 0.0f, -1.0f,
                       1.0f);
}

void Win3DSetPerspective(float FovDeg, float Aspect, float NearPlane, float FarPlane)
{
    // FUN_00422820. No modelview reset here, unlike the ortho setter.
    IRenderPipeline* const Pipe = GetPipeline();

    if (G_bProjectionCacheEnabled != 0)
    {
        const float Args[4] = {FovDeg, Aspect, NearPlane, FarPlane};

        bool bChanged = (G_ProjectionKind != kProjectionKindPerspective);
        for (int I = 0; I < 4; ++I)
        {
            if (!bChanged && G_ProjectionParams[I] != Args[I])
                bChanged = true;
            G_ProjectionParams[I] = Args[I];
        }

        if (!bChanged)
        {
            ++G_ProjectionSkipCount;
            return;
        }

        ++G_ProjectionSetCount;
        G_ProjectionKind = kProjectionKindPerspective;
    }

    Pipe->SetPerspective(FovDeg, Aspect, NearPlane, FarPlane);
}

void Win3DApplyCameraPerspective(ICamera* Camera)
{
    // FUN_00422910: the four getters in retail's call order.
    const float FarPlane = Camera->GetZFar();
    const float NearPlane = Camera->GetZNear();
    const float Aspect = Camera->GetAspect();
    const float FovDeg = Camera->GetFovYDeg();
    Win3DSetPerspective(FovDeg, Aspect, NearPlane, FarPlane);
}

void Win3DPushProjection()
{
    // FUN_00422a70. With the cache on, the cache's kind and params; with it off, the pipeline's
    // 4x4 projection matrix.
    if (G_bProjectionCacheEnabled != 0)
    {
        G_SavedProjectionKind = G_ProjectionKind;
        for (int I = 0; I < kProjectionParamCount; ++I)
            G_SavedProjectionParams[I] = G_ProjectionParams[I];
        return;
    }

    GetPipeline()->GetProjection(G_SavedProjectionMatrix);
}

void Win3DPopProjection()
{
    // FUN_00422ab0
    if (G_bProjectionCacheEnabled == 0)
    {
        GetPipeline()->SetProjection(G_SavedProjectionMatrix);
        return;
    }

    if (G_SavedProjectionKind == kProjectionKindPerspective)
    {
        Win3DSetPerspective(G_SavedProjectionParams[0], G_SavedProjectionParams[1],
                            G_SavedProjectionParams[2], G_SavedProjectionParams[3]);
    }
    else if (G_SavedProjectionKind == kProjectionKindOrtho)
    {
        Win3DSetProjection(G_SavedProjectionParams[0], G_SavedProjectionParams[1],
                           G_SavedProjectionParams[2], G_SavedProjectionParams[3],
                           G_SavedProjectionParams[4], G_SavedProjectionParams[5]);
    }

    G_SavedProjectionKind = 0;
}

int Win3DGetProjectionSetCount()
{
    return G_ProjectionSetCount;
}

int Win3DGetProjectionSkipCount()
{
    return G_ProjectionSkipCount;
}

// FUN_004214a0
void Win3DOnPresentFrame()
{
    G_ProjectionSetCount = 0;
    G_ProjectionSkipCount = 0;
    G_bProjectionCacheEnabled =
        static_cast<unsigned char>(GetGameOption(kGameOptLikePetal) == 0 ? 1 : 0);
}

void Win3DEnd()
{
    // FUN_00421690.
    IRenderPipeline* const Pipe = GetPipeline();

    // Clipping goes on first and stays on.
    Pipe->SetRenderState(kRsClipping, 1);

    ApplyTextureStages();

    if (G_VertexCount > 0)
    {
        // The vertex colour drives the diffuse material (.rdata 0x005c9698).
        Pipe->SetRenderState(kRsColorVertex, 1);
        Pipe->SetRenderState(kRsDiffuseMaterialSource, 1);

        int SavedLighting = 0;
        Pipe->GetRenderState(kRsLighting, &SavedLighting);
        Pipe->SetRenderState(kRsLighting, G_bLighting);

        Pipe->SetModelview(GWin3DModelview);

        const unsigned VertexCount = static_cast<unsigned>(G_VertexCount);

        switch (G_PrimitiveMode)
        {
        case kWin3DPoints:
            Pipe->DrawPrimitive(1, kWin3DFvf, G_Vertices, VertexCount, nullptr);
            break;

        case kWin3DLines:
            // The locked path draws through the index buffer, since a locked pool's vertices
            // are not in draw order.
            if (G_bBufferLocked == 0)
                Pipe->DrawPrimitive(2, kWin3DFvf, G_Vertices, VertexCount, nullptr);
            else
                Pipe->DrawIndexedPrimitiveUp(2, kWin3DFvf, G_Vertices, VertexCount, G_Indices,
                                             static_cast<unsigned>(G_IndexCount), 0);
            break;

        case kWin3DLineStrip:
            Pipe->DrawPrimitive(3, kWin3DFvf, G_Vertices, VertexCount, nullptr);
            break;

        case kWin3DTriangles:
            if (G_bBufferLocked == 0)
                Pipe->DrawPrimitive(4, kWin3DFvf, G_Vertices, VertexCount, nullptr);
            else
                Pipe->DrawIndexedPrimitiveUp(4, kWin3DFvf, G_Vertices, VertexCount, G_Indices,
                                             static_cast<unsigned>(G_IndexCount), 0);
            break;

        case kWin3DTriangleStrip:
            Pipe->DrawPrimitive(5, kWin3DFvf, G_Vertices, VertexCount, nullptr);
            break;

        case kWin3DTriangleFan:
        case kWin3DPolygon:
            // A polygon and a fan are the same draw; retail shares the case outright.
            Pipe->DrawPrimitive(6, kWin3DFvf, G_Vertices, VertexCount, nullptr);
            break;

        case kWin3DLineLoop:
        {
            // A strip over everything, then one indexed line from the last vertex to the first.
            Pipe->DrawPrimitive(3, kWin3DFvf, G_Vertices, VertexCount, nullptr);

            const unsigned short Closing[2] = {
                static_cast<unsigned short>(G_VertexCount - 1), 0};
            Pipe->DrawIndexedPrimitiveUp(2, kWin3DFvf, G_Vertices, VertexCount, Closing, 2, 0);
            break;
        }

        case kWin3DQuads:
        {
            // Each quad becomes (0,1,2) and (0,2,3); the run is capped.
            unsigned Usable = static_cast<unsigned>(G_IndexCount);
            if (Usable > kMaxQuadIndices)
                Usable = kMaxQuadIndices;

            static unsigned short QuadIndices[(kMaxQuadIndices / 4) * 6];
            unsigned Out = 0;
            for (unsigned I = 3; I < Usable; I += 4)
            {
                const unsigned short A = G_Indices[I - 3];
                const unsigned short B = G_Indices[I - 2];
                const unsigned short C = G_Indices[I - 1];
                const unsigned short D = G_Indices[I];

                QuadIndices[Out + 0] = A;
                QuadIndices[Out + 1] = B;
                QuadIndices[Out + 2] = C;
                QuadIndices[Out + 3] = A;
                QuadIndices[Out + 4] = C;
                QuadIndices[Out + 5] = D;
                Out += 6;
            }

            Pipe->DrawIndexedPrimitiveUp(4, kWin3DFvf, G_Vertices, VertexCount, QuadIndices, Out, 0);
            break;
        }

        default:
            FDUMP(0x100002, "%s(%d) : *** WARNING: glEnd invalid vertex mode (0x%X)\n",
                  "E:\\FL\\Scratch\\Source\\Client\\cmn\\OpenGL.cpp", 0x1d2, G_PrimitiveMode);
            break;
        }

        Pipe->SetRenderState(kRsColorVertex, 0);
        Pipe->SetRenderState(kRsLighting, SavedLighting);
    }

    // Everything is reset whether or not anything was drawn.
    G_PrimitiveMode = -1;
    G_VertexCount = 0;
    G_IndexCount = 0;
    G_bBufferLocked = 0;
    G_bTextureBound = 0;
    G_BoundTexture = nullptr;

    Pipe->SetTextureStageTexture(0, nullptr);
}

int Win3DGetVertexCount()
{
    return G_VertexCount;
}

int Win3DGetIndexCount()
{
    return G_IndexCount;
}

unsigned long Win3DGetCurrentColour()
{
    return G_CurrentColour;
}

bool Win3DGetVertex(int Index, float* OutXyz3, unsigned long* OutColour)
{
    if (Index < 0 || Index >= G_VertexCount)
        return false;

    const FWin3DVertex& Vertex = G_Vertices[Index];
    if (OutXyz3 != nullptr)
    {
        OutXyz3[0] = Vertex.X;
        OutXyz3[1] = Vertex.Y;
        OutXyz3[2] = Vertex.Z;
    }
    if (OutColour != nullptr)
        *OutColour = Vertex.Diffuse;
    return true;
}

namespace
{
    // Retail's two 256-entry stack tables, indexed 1..Cols.
    const int kMaxSphereCols = 256;
}

void Win3DBuildSpherePoints(float* OutPoints, float Radius, int Cols, int Rows)
{
    // FUN_00422fe0 (cdecl).
    float CosTable[kMaxSphereCols + 1];
    float SinTable[kMaxSphereCols + 1];

    // pi as a float and 2 pi widened from float, each step rounded to float.
    const float PhiStep =
        static_cast<float>(static_cast<double>(3.1415927f) / static_cast<double>(Rows - 1));
    const float ThetaStep =
        static_cast<float>(static_cast<double>(6.2831855f) / static_cast<double>(Cols));

    // x87: theta stays double for the whole loop; cos / sin of the unrounded angle are rounded
    // to float.
    double Theta = 0.0;
    for (int Index = 1; Index <= Cols; ++Index)
    {
        CosTable[Index] = static_cast<float>(cos(Theta));
        SinTable[Index] = static_cast<float>(sin(Theta));
        Theta += static_cast<double>(ThetaStep);
    }

    // Phi the same way, one ring per row; Y and sin(phi) * Radius are rounded once per ring.
    if (Rows > 0)
    {
        double Phi = 0.0;
        float* Out = OutPoints;
        int RowsLeft = Rows;
        do
        {
            const float Y = static_cast<float>(cos(Phi) * static_cast<double>(Radius));
            const float SinPhiRadius = static_cast<float>(sin(Phi) * static_cast<double>(Radius));
            for (int Index = 1; Index <= Cols; ++Index)
            {
                Out[1] = Y;
                Out[0] = static_cast<float>(static_cast<double>(SinPhiRadius) *
                                            static_cast<double>(CosTable[Index]));
                Out[2] = static_cast<float>(static_cast<double>(SinPhiRadius) *
                                            static_cast<double>(SinTable[Index]));
                Out += 3;
            }
            --RowsLeft;
            Phi += static_cast<double>(PhiStep);
        } while (RowsLeft != 0);
    }
}

void Win3DDrawSphereGrid(const float* Points, int Cols, int Rows)
{
    // FUN_004230c0 (cdecl). Untextured.
    Win3DDisable(kWin3DTexture2D);

    // Skip the first ring (the pole row) and draw every ring up to but not including the last as
    // a closed loop.
    const float* Row = Points + static_cast<size_t>(Cols) * 3;
    if (Rows - 1 > 1)
    {
        int RingsLeft = Rows - 2;
        do
        {
            Win3DBegin(kWin3DLineLoop);
            for (int Col = 0; Col < Cols; ++Col)
            {
                Win3DVertex3f(Row[0], Row[1], Row[2]);
                Row += 3;
            }
            Win3DEnd();
            --RingsLeft;
        } while (RingsLeft != 0);
    }

    // Every longitude line: all Rows points of one column, as a line strip.
    if (Cols > 0)
    {
        const float* ColStart = Points;
        int ColsLeft = Cols;
        do
        {
            Win3DBegin(kWin3DLineStrip);
            const float* V = ColStart;
            for (int RowIndex = 0; RowIndex < Rows; ++RowIndex)
            {
                Win3DVertex3f(V[0], V[1], V[2]);
                V += static_cast<size_t>(Cols) * 3;
            }
            Win3DEnd();
            ColStart += 3;
            --ColsLeft;
        } while (ColsLeft != 0);
    }
}

void Win3DDrawWireSphere(void* Camera, float const* Xform12, float Radius, int Cols, int Rows)
{
    // FUN_00423180. DAT_006649c8 is a function static with an empty destructor; 0x3000 bytes,
    // 0x400 points.
    static float Points[0xc00];
    Win3DBuildSpherePoints(Points, Radius, Cols, Rows);
    Win3DBindModelview(Camera, Xform12);
    Win3DDrawSphereGrid(Points, Cols, Rows);
}

void Win3DVec3Add(float* Out, float const* A, float const* B)
{
    // FUN_00423b30
    Out[0] = A[0] + B[0];
    Out[1] = A[1] + B[1];
    Out[2] = A[2] + B[2];
}

void Win3DVec3Sub(float* Out, float const* A, float const* B)
{
    // FUN_00423b60.
    Out[0] = A[0] - B[0];
    Out[1] = A[1] - B[1];
    Out[2] = A[2] - B[2];
}

namespace
{
    // DAT_005c9804 (1.25f) and DAT_005c9800 (1.2f): how far past the segment's half-length the
    // straight and the curved drawer stretch each quad.
    constexpr float kFuzzyLineQuadScale = 1.25f;
    constexpr float kFuzzyArcQuadScale = 1.2f;

    // FUN_00410190 (thiscall on the vector): sqrt((x*x + y*y) + z*z), unrounded.
    double FuzzyVecLength(float const* V)
    {
        return sqrt(static_cast<double>(V[0]) * V[0] + static_cast<double>(V[1]) * V[1] +
                    static_cast<double>(V[2]) * V[2]);
    }

    // The drawers' inline length, `(y*y + z*z) + x*x` (or z first -- the same sum), unrounded.
    double FuzzyInlineLength(float const* V)
    {
        return sqrt(static_cast<double>(V[1]) * V[1] + static_cast<double>(V[2]) * V[2] +
                    static_cast<double>(V[0]) * V[0]);
    }

    // The cross product both drawers build inline: Out = D x E, each component rounded to float.
    void FuzzyCross(float* Out, float const* D, float const* E)
    {
        Out[0] = static_cast<float>(static_cast<double>(E[2]) * D[1] -
                                    static_cast<double>(E[1]) * D[2]);
        Out[1] = static_cast<float>(static_cast<double>(D[2]) * E[0] -
                                    static_cast<double>(E[2]) * D[0]);
        Out[2] = static_cast<float>(static_cast<double>(E[1]) * D[0] -
                                    static_cast<double>(D[1]) * E[0]);
    }

    // Common's RandomVector(Radius) added to P.
    void FuzzyJitter(float* Out, float const* P, float Radius)
    {
        Vector const Offset = RandomVector(Radius);
        float const OffsetXyz[3] = {Offset.x, Offset.y, Offset.z};
        Win3DVec3Add(Out, P, OffsetXyz);
    }

    // Scale V in place by Wanted / |V|, each product rounded to float.
    void FuzzyRescale(float* V, float Wanted)
    {
        double const Scale = static_cast<double>(Wanted) / FuzzyVecLength(V);
        V[0] = static_cast<float>(V[0] * Scale);
        V[1] = static_cast<float>(V[1] * Scale);
        V[2] = static_cast<float>(V[2] * Scale);
    }

    // One camera-facing quad: (0,0) Mid + Along, (1,0) Mid + Side, (1,1) Mid - Along,
    // (0,1) Mid - Side.
    void FuzzyEmitQuad(unsigned long Handle, float const* Mid, float const* Along,
                       float const* Side)
    {
        float Corner[3];
        Win3DBindTexture(reinterpret_cast<void*>(Handle));
        Win3DBegin(kWin3DQuads);
        Win3DTexCoord2f(0.0f, 0.0f);
        Win3DVec3Add(Corner, Mid, Along);
        Win3DVertex3f(Corner[0], Corner[1], Corner[2]);
        Win3DTexCoord2f(1.0f, 0.0f);
        Win3DVec3Add(Corner, Mid, Side);
        Win3DVertex3f(Corner[0], Corner[1], Corner[2]);
        Win3DTexCoord2f(1.0f, 1.0f);
        Win3DVec3Sub(Corner, Mid, Along);
        Win3DVertex3f(Corner[0], Corner[1], Corner[2]);
        Win3DTexCoord2f(0.0f, 1.0f);
        Win3DVec3Sub(Corner, Mid, Side);
        Win3DVertex3f(Corner[0], Corner[1], Corner[2]);
        Win3DEnd();
    }
}

void Win3DDrawFuzzyLine(FWin3DFuzzyLine const* Line)
{
    // FUN_00423be0.
    if (Line->bCurved != 0)
    {
        Win3DDrawFuzzyArc(Line);
        return;
    }

    // The length is kept as a float, but divided unrounded for the segment count.
    float Dir[3];
    Win3DVec3Sub(Dir, Line->End, Line->Start);
    double const LengthExt = FuzzyInlineLength(Dir);
    float const Length = static_cast<float>(LengthExt);
    int const Count = static_cast<int>(floor(LengthExt / Line->Width) + 1.0);

    float const Step = static_cast<float>(static_cast<double>(Length) / Count);
    float Unit[3] = {Dir[0], Dir[1], Dir[2]};
    FuzzyRescale(Unit, Step);

    // The colour, on both paths.
    Win3DColor3f(Line->Color[0], Line->Color[1], Line->Color[2]);

    float const Jitter = Line->JitterScale * Line->Width;
    if (Line->Texture == nullptr)
    {
        // Start, the Count - 1 jittered interior points and End as one line strip. x87: the x
        // product stays double into its sum; y and z are rounded to float first.
        Win3DDisable(kWin3DTexture2D);
        Win3DDisable(kWin3DBlend);
        Win3DBegin(kWin3DLineStrip);
        Win3DVertex3f(Line->Start[0], Line->Start[1], Line->Start[2]);
        for (int Index = 1; Index < Count; ++Index)
        {
            double const Scale = static_cast<double>(Index);
            float const StepY = static_cast<float>(Unit[1] * Scale);
            float const StepZ = static_cast<float>(Unit[2] * Scale);
            float const OnLine[3] = {
                static_cast<float>(Unit[0] * Scale + Line->Start[0]),
                StepY + Line->Start[1],
                StepZ + Line->Start[2]};
            float Point[3];
            FuzzyJitter(Point, OnLine, Jitter);
            Win3DVertex3f(Point[0], Point[1], Point[2]);
        }
        Win3DVertex3f(Line->End[0], Line->End[1], Line->End[2]);
        Win3DEnd();
        return;
    }

    // Frame 0 of the texture, additive blend.
    float Cur[3] = {Line->Start[0], Line->Start[1], Line->Start[2]};
    unsigned long Frame[5];
    DALib::TextureLib->GetTextureFrame(Line->Texture, 0, Frame);
    Win3DEnable(kWin3DBlend);
    Win3DSetBlendFunc(kWin3DBlendOne, kWin3DBlendOne);

    for (int Index = 1; Index <= Count; ++Index)
    {
        // The last point is End itself, the others Start + Unit * i jittered.
        float Next[3];
        if (Index == Count)
        {
            Next[0] = Line->End[0];
            Next[1] = Line->End[1];
            Next[2] = Line->End[2];
        }
        else
        {
            float Offset[3];
            float OnLine[3];
            ScaleVec3(Offset, Unit, static_cast<float>(Index));
            Win3DVec3Add(OnLine, Line->Start, Offset);
            FuzzyJitter(Next, OnLine, Jitter);
        }

        // The side direction (Next - Cur) x (Eye - Cur).
        float ToEye[3];
        float Segment[3];
        float Side[3];
        Win3DVec3Sub(ToEye, Line->EyePos, Cur);
        Win3DVec3Sub(Segment, Next, Cur);
        FuzzyCross(Side, Segment, ToEye);

        // The midpoint, and the side rescaled to half of |Mid - Cur| (0.5 as a double).
        float Half[3];
        float HalfScaled[3];
        float Mid[3];
        float FromCur[3];
        Win3DVec3Sub(Half, Next, Cur);
        ScaleVec3(HalfScaled, Half, 0.5f);
        Win3DVec3Add(Mid, Cur, HalfScaled);
        Win3DVec3Sub(FromCur, Mid, Cur);
        FuzzyRescale(Side, static_cast<float>(FuzzyInlineLength(FromCur) * 0.5));

        // Along = (Cur - Mid) * 1.25, Side * 1.25 * SideScale.
        float Back[3];
        Win3DVec3Sub(Back, Cur, Mid);
        float const Along[3] = {Back[0] * kFuzzyLineQuadScale, Back[1] * kFuzzyLineQuadScale,
                                Back[2] * kFuzzyLineQuadScale};
        float const SideWide[3] = {Side[0] * kFuzzyLineQuadScale, Side[1] * kFuzzyLineQuadScale,
                                   Side[2] * kFuzzyLineQuadScale};
        float const SideQuad[3] = {SideWide[0] * Line->SideScale, SideWide[1] * Line->SideScale,
                                   SideWide[2] * Line->SideScale};

        FuzzyEmitQuad(Frame[0], Mid, Along, SideQuad);

        Cur[0] = Next[0];
        Cur[1] = Next[1];
        Cur[2] = Next[2];
    }
}

void Win3DDrawFuzzyArc(FWin3DFuzzyLine const* Line)
{
    // FUN_004231f0.
    float FromCentre[3];
    float ToEnd[3];
    Win3DVec3Sub(FromCentre, Line->Start, Line->Control);
    Win3DVec3Sub(ToEnd, Line->End, Line->Control);

    // Either radius below 0.001 (compared with the unrounded root) draws nothing.
    double const RadiusExt = FuzzyInlineLength(FromCentre);
    float const Radius = static_cast<float>(RadiusExt);
    if (RadiusExt < 0.001)
        return;
    if (FuzzyInlineLength(ToEnd) < 0.001)
        return;

    // ToEnd rescaled to the start radius.
    float const Jitter = Line->JitterScale * Line->Width;
    FuzzyRescale(ToEnd, Radius);

    // The swept angle. The cosine is compared unrounded with -1.0f and then, rounded, with 1.0f;
    // NaN passes both.
    float const RadiusSq = Radius * Radius;
    double const CosineExt = (static_cast<double>(ToEnd[2]) * FromCentre[2] +
                              static_cast<double>(ToEnd[1]) * FromCentre[1] +
                              static_cast<double>(ToEnd[0]) * FromCentre[0]) /
                             RadiusSq;
    float Cosine = static_cast<float>(CosineExt);
    if (CosineExt < -1.0)
        Cosine = -1.0f;
    else if (Cosine > 1.0f)
        Cosine = 1.0f;
    float const Angle = static_cast<float>(acos(static_cast<double>(Cosine)));

    // The angle one chord of Width subtends, acos(1 - W^2 / (2 R^2)), clamped on the float.
    double const Width = Line->Width;
    float ChordCosine = static_cast<float>(
        1.0 - Width * Width / (static_cast<double>(RadiusSq) + RadiusSq));
    if (ChordCosine < -1.0f)
        ChordCosine = -1.0f;
    else if (ChordCosine > 1.0f)
        ChordCosine = 1.0f;

    int const Count = static_cast<int>(
        floor(static_cast<double>(Angle) / acos(static_cast<double>(ChordCosine))) + 1.0);
    float const Step = static_cast<float>(static_cast<double>(Angle) / Count);

    float Axis[3];
    FuzzyCross(Axis, FromCentre, ToEnd);

    FVector const Source = {FromCentre[0], FromCentre[1], FromCentre[2]};

    if (Line->Texture == nullptr)
    {
        // Start, the Count - 1 rotated and jittered interior points, and Control + ToEnd as a
        // line strip.
        Win3DDisable(kWin3DTexture2D);
        Win3DDisable(kWin3DBlend);
        Win3DColor3f(Line->Color[0], Line->Color[1], Line->Color[2]);
        Win3DBegin(kWin3DLineStrip);
        Win3DVertex3f(Line->Start[0], Line->Start[1], Line->Start[2]);
        for (int Index = 1; Index < Count; ++Index)
        {
            // x87: the half angle stays double; the axis length is the inline
            // (x*x + z*z) + y*y root.
            double const HalfAngle = static_cast<double>(Index) * Step * 0.5f;
            double const AxisLength = sqrt(static_cast<double>(Axis[0]) * Axis[0] +
                                           static_cast<double>(Axis[2]) * Axis[2] +
                                           static_cast<double>(Axis[1]) * Axis[1]);
            FQuaternion Rotation;
            Rotation.W = static_cast<float>(cos(HalfAngle));
            float Vec[3];
            ScaleVec3(Vec, Axis, static_cast<float>(sin(HalfAngle) / AxisLength));
            Rotation.X = Vec[0];
            Rotation.Y = Vec[1];
            Rotation.Z = Vec[2];

            // Through DAT_006679d8 directly.
            FMatrix Matrix;
            Win3DTransformMathEngine()->QuaternionToMatrix(Matrix, Rotation);
            FVector Rotated;
            Win3DTransformMathEngine()->TransformMatrix(Rotated, Matrix, Source);

            float const OnArc[3] = {Rotated.X + Line->Control[0], Rotated.Y + Line->Control[1],
                                    Rotated.Z + Line->Control[2]};
            float Point[3];
            FuzzyJitter(Point, OnArc, Jitter);
            Win3DVertex3f(Point[0], Point[1], Point[2]);
        }
        Win3DVertex3f(ToEnd[0] + Line->Control[0], ToEnd[1] + Line->Control[1],
                      ToEnd[2] + Line->Control[2]);
        Win3DEnd();
        return;
    }

    // Frame 0 of the texture, additive blend. No colour is set on this path.
    float Cur[3] = {Line->Start[0], Line->Start[1], Line->Start[2]};
    unsigned long Frame[5];
    DALib::TextureLib->GetTextureFrame(Line->Texture, 0, Frame);
    Win3DEnable(kWin3DBlend);
    Win3DSetBlendFunc(kWin3DBlendOne, kWin3DBlendOne);

    for (int Index = 1; Index <= Count; ++Index)
    {
        float Next[3];
        if (Index == Count)
        {
            Win3DVec3Add(Next, Line->Control, ToEnd);
        }
        else
        {
            // x87: the sine is of the unrounded half angle, the cosine of the rounded float.
            double const HalfAngleExt = static_cast<double>(Index) * Step * 0.5f;
            float const HalfAngle = static_cast<float>(HalfAngleExt);
            float const Sine = static_cast<float>(sin(HalfAngleExt));
            FQuaternion Rotation;
            Rotation.W = static_cast<float>(cos(static_cast<double>(HalfAngle)));
            float Vec[3];
            ScaleVec3(Vec, Axis, static_cast<float>(Sine / FuzzyVecLength(Axis)));
            Rotation.X = Vec[0];
            Rotation.Y = Vec[1];
            Rotation.Z = Vec[2];

            FMatrix Matrix;
            Win3DTransformMathEngine()->QuaternionToMatrix(Matrix, Rotation);
            FVector Rotated;
            Win3DTransformByMatrix(Rotated, Matrix, Source);
            float const RotatedXyz[3] = {Rotated.X, Rotated.Y, Rotated.Z};
            float OnArc[3];
            Win3DVec3Add(OnArc, Line->Control, RotatedXyz);
            FuzzyJitter(Next, OnArc, Jitter);
        }

        float ToEye[3];
        float Segment[3];
        float Side[3];
        Win3DVec3Sub(ToEye, Line->EyePos, Cur);
        Win3DVec3Sub(Segment, Next, Cur);
        FuzzyCross(Side, Segment, ToEye);

        // The side rescaled to |Mid - Cur| (no halving here, unlike the straight drawer).
        float Half[3];
        float HalfScaled[3];
        float Mid[3];
        float FromCur[3];
        Win3DVec3Sub(Half, Next, Cur);
        ScaleVec3(HalfScaled, Half, 0.5f);
        Win3DVec3Add(Mid, HalfScaled, Cur);
        Win3DVec3Sub(FromCur, Mid, Cur);
        FuzzyRescale(Side, static_cast<float>(FuzzyInlineLength(FromCur)));

        // Along = (Cur - Mid) * 1.2, Side * 1.2 (no SideScale).
        float Back[3];
        Win3DVec3Sub(Back, Cur, Mid);
        float const Along[3] = {Back[0] * kFuzzyArcQuadScale, Back[1] * kFuzzyArcQuadScale,
                                Back[2] * kFuzzyArcQuadScale};
        float const SideQuad[3] = {Side[0] * kFuzzyArcQuadScale, Side[1] * kFuzzyArcQuadScale,
                                   Side[2] * kFuzzyArcQuadScale};

        FuzzyEmitQuad(Frame[0], Mid, Along, SideQuad);

        Cur[0] = Next[0];
        Cur[1] = Next[1];
        Cur[2] = Next[2];
    }
}
