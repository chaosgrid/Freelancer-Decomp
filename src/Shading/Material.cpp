// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Material.h"
#include "MaterialAnimation.h"
#include "MaterialLibrary.h"
#include "engine/engbase.h"
#include "render/slots.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>

// Retail Shading.dll state tables: 0-terminated {state, value} pairs (render-state or
// texture-stage-state); the name suffix is the DAT_ address.
static const unsigned long GDetailTable_06ed188c[] = {
    D3DRS_LIGHTING, TRUE, D3DRS_SPECULARENABLE, FALSE, D3DRS_ALPHABLENDENABLE, FALSE, 0, 0};
static const unsigned long GDetailTable_06ed18ac[] = {
    D3DRS_LIGHTING, TRUE, D3DRS_SPECULARENABLE, FALSE, D3DRS_ALPHABLENDENABLE, TRUE,
    D3DRS_SRCBLEND, D3DBLEND_SRCALPHA, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA, 0, 0};
static const unsigned long GDetailTable_06ed18dc[] = {
    D3DTSS_COLOROP, D3DTOP_MODULATE, D3DTSS_COLORARG1, D3DTA_TEXTURE,
    D3DTSS_COLORARG2, D3DTA_DIFFUSE, D3DTSS_ALPHAOP, D3DTOP_MODULATE,
    D3DTSS_ALPHAARG1, D3DTA_TEXTURE, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE, 0, 0};
static const unsigned long GDetailTable_06ed1914[] = {
    D3DTSS_COLOROP, D3DTOP_MODULATE2X, D3DTSS_COLORARG1, D3DTA_TEXTURE,
    D3DTSS_COLORARG2, D3DTA_CURRENT, D3DTSS_ALPHAOP, D3DTOP_MODULATE,
    D3DTSS_ALPHAARG1, D3DTA_TEXTURE, D3DTSS_ALPHAARG2, D3DTA_CURRENT, 0, 0};
static const unsigned long GDetailTable_06ed194c[] = {
    D3DTSS_COLOROP, D3DTOP_DISABLE, D3DTSS_COLORARG1, D3DTA_TEXTURE,
    D3DTSS_COLORARG2, D3DTA_CURRENT, D3DTSS_ALPHAOP, D3DTOP_DISABLE,
    D3DTSS_ALPHAARG1, D3DTA_TEXTURE, D3DTSS_ALPHAARG2, D3DTA_CURRENT, 0, 0};
static const unsigned long GDetailTable_06ed1e7c[] = {
    D3DRS_LIGHTING, TRUE, D3DRS_SPECULARENABLE, FALSE, D3DRS_ALPHABLENDENABLE, FALSE, 0, 0};
static const unsigned long GDetailTable_06ed1e9c[] = {
    D3DRS_LIGHTING, FALSE, D3DRS_SPECULARENABLE, FALSE, D3DRS_ALPHABLENDENABLE, TRUE,
    D3DRS_SRCBLEND, D3DBLEND_SRCALPHA, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA, 0, 0};
static const unsigned long GDetailTable_06ed1ecc[] = {
    D3DTSS_COLOROP, D3DTOP_MODULATE, D3DTSS_COLORARG1, D3DTA_TEXTURE,
    D3DTSS_COLORARG2, D3DTA_DIFFUSE, D3DTSS_ALPHAOP, D3DTOP_MODULATE,
    D3DTSS_ALPHAARG1, D3DTA_TEXTURE, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE, 0, 0};
static const unsigned long GDetailTable_06ed1f04[] = {
    D3DTSS_COLOROP, D3DTOP_MODULATE2X, D3DTSS_COLORARG1, D3DTA_TEXTURE,
    D3DTSS_COLORARG2, D3DTA_CURRENT, D3DTSS_ALPHAOP, D3DTOP_MODULATE,
    D3DTSS_ALPHAARG1, D3DTA_TEXTURE, D3DTSS_ALPHAARG2, D3DTA_CURRENT, 0, 0};
static const unsigned long GDetailTable_06ed1f3c[] = {
    D3DTSS_COLOROP, D3DTOP_SELECTARG1, D3DTSS_COLORARG1, D3DTA_TEXTURE,
    D3DTSS_COLORARG2, D3DTA_DIFFUSE, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1,
    D3DTSS_ALPHAARG1, D3DTA_TEXTURE, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE, 0, 0};
static const unsigned long GDetailTable_06ed1f74[] = {
    D3DTSS_COLOROP, D3DTOP_DISABLE, D3DTSS_COLORARG1, D3DTA_TEXTURE,
    D3DTSS_COLORARG2, D3DTA_CURRENT, D3DTSS_ALPHAOP, D3DTOP_DISABLE,
    D3DTSS_ALPHAARG1, D3DTA_TEXTURE, D3DTSS_ALPHAARG2, D3DTA_CURRENT, 0, 0};
static const unsigned long GDetailTable_06ed220c[] = {
    D3DRS_LIGHTING, TRUE, D3DRS_SPECULARENABLE, FALSE, D3DRS_ALPHABLENDENABLE, FALSE, 0, 0};
static const unsigned long GDetailTable_06ed222c[] = {
    D3DRS_LIGHTING, TRUE, D3DRS_SPECULARENABLE, FALSE, D3DRS_ALPHABLENDENABLE, TRUE,
    D3DRS_SRCBLEND, D3DBLEND_SRCALPHA, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA, 0, 0};
static const unsigned long GDetailTable_06ed225c[] = {
    D3DTSS_COLOROP, D3DTOP_MODULATE, D3DTSS_COLORARG1, D3DTA_TEXTURE,
    D3DTSS_COLORARG2, D3DTA_DIFFUSE, D3DTSS_ALPHAOP, D3DTOP_MODULATE,
    D3DTSS_ALPHAARG1, D3DTA_TEXTURE, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE, 0, 0};
static const unsigned long GDetailTable_06ed2294[] = {
    D3DTSS_COLOROP, D3DTOP_MODULATE2X, D3DTSS_COLORARG1, D3DTA_TEXTURE,
    D3DTSS_COLORARG2, D3DTA_CURRENT, D3DTSS_ALPHAOP, D3DTOP_MODULATE,
    D3DTSS_ALPHAARG1, D3DTA_TEXTURE, D3DTSS_ALPHAARG2, D3DTA_CURRENT, 0, 0};
static const unsigned long GDetailTable_06ed22cc[] = {
    D3DTSS_COLOROP, D3DTOP_DISABLE, D3DTSS_COLORARG1, D3DTA_TEXTURE,
    D3DTSS_COLORARG2, D3DTA_CURRENT, D3DTSS_ALPHAOP, D3DTOP_DISABLE,
    D3DTSS_ALPHAARG1, D3DTA_TEXTURE, D3DTSS_ALPHAARG2, D3DTA_CURRENT, 0, 0};

// LAB_06eb81e0.
static const unsigned long GDetailMapTable_06ed1a9c[] = {
    D3DRS_LIGHTING, TRUE, D3DRS_SPECULARENABLE, FALSE, D3DRS_ALPHABLENDENABLE, FALSE, 0, 0};
static const unsigned long GDetailMapTable_06ed1abc[] = {
    D3DTSS_COLOROP, D3DTOP_MODULATE, D3DTSS_COLORARG1, D3DTA_TEXTURE,
    D3DTSS_COLORARG2, D3DTA_DIFFUSE, D3DTSS_ALPHAOP, D3DTOP_MODULATE,
    D3DTSS_ALPHAARG1, D3DTA_TEXTURE, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE, 0, 0};
static const unsigned long GDetailMapTable_06ed1af4[] = {
    D3DTSS_COLOROP, D3DTOP_MODULATE2X, D3DTSS_COLORARG1, D3DTA_TEXTURE,
    D3DTSS_COLORARG2, D3DTA_CURRENT, D3DTSS_ALPHAOP, D3DTOP_MODULATE,
    D3DTSS_ALPHAARG1, D3DTA_TEXTURE, D3DTSS_ALPHAARG2, D3DTA_CURRENT, 0, 0};
static const unsigned long GDetailMapTable_06ed1b2c[] = {
    D3DTSS_COLOROP, D3DTOP_DISABLE, D3DTSS_COLORARG1, D3DTA_TEXTURE,
    D3DTSS_COLORARG2, D3DTA_CURRENT, D3DTSS_ALPHAOP, D3DTOP_DISABLE,
    D3DTSS_ALPHAARG1, D3DTA_TEXTURE, D3DTSS_ALPHAARG2, D3DTA_CURRENT, 0, 0};

// State tables for the material Bind callbacks.
static const unsigned long GBindTable_06ed209c[] = {
    D3DRS_COLORVERTEX, FALSE, 0, 0};
static const unsigned long GBindTable_06ed20ac[] = {
    D3DTSS_COLOROP, D3DTOP_SELECTARG1, D3DTSS_COLORARG1, D3DTA_TEXTURE,
    D3DTSS_COLORARG2, D3DTA_DIFFUSE, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1,
    D3DTSS_ALPHAARG1, D3DTA_TEXTURE, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE, 0, 0};
static const unsigned long GBindTable_06ed20e4[] = {
    D3DTSS_COLOROP, D3DTOP_DISABLE, D3DTSS_COLORARG1, D3DTA_TEXTURE,
    D3DTSS_COLORARG2, D3DTA_CURRENT, D3DTSS_ALPHAOP, D3DTOP_DISABLE,
    D3DTSS_ALPHAARG1, D3DTA_TEXTURE, D3DTSS_ALPHAARG2, D3DTA_CURRENT, 0, 0};
static const unsigned long GBindTable_06ed206c[] = {
    D3DRS_LIGHTING, FALSE, D3DRS_SPECULARENABLE, FALSE, D3DRS_ALPHABLENDENABLE, TRUE,
    D3DRS_SRCBLEND, D3DBLEND_DESTCOLOR, D3DRS_DESTBLEND, D3DBLEND_ZERO, 0, 0};

static const unsigned long GBindTable_06ed26dc[] = {
    D3DTSS_COLOROP, D3DTOP_MODULATE, D3DTSS_COLORARG1, D3DTA_TEXTURE,
    D3DTSS_COLORARG2, D3DTA_DIFFUSE, D3DTSS_ALPHAOP, D3DTOP_MODULATE,
    D3DTSS_ALPHAARG1, D3DTA_TEXTURE, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE, 0, 0};
static const unsigned long GBindTable_06ed2714[] = {
    D3DTSS_COLOROP, D3DTOP_DISABLE, D3DTSS_COLORARG1, D3DTA_TEXTURE,
    D3DTSS_COLORARG2, D3DTA_CURRENT, D3DTSS_ALPHAOP, D3DTOP_DISABLE,
    D3DTSS_ALPHAARG1, D3DTA_TEXTURE, D3DTSS_ALPHAARG2, D3DTA_CURRENT, 0, 0};
static const unsigned long GBindTable_06ed26ac[] = {
    D3DRS_LIGHTING, TRUE, D3DRS_SPECULARENABLE, FALSE, D3DRS_ALPHABLENDENABLE, TRUE,
    D3DRS_SRCBLEND, D3DBLEND_SRCALPHA, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA, 0, 0};
static const unsigned long GBindTable_06ed268c[] = {
    D3DRS_LIGHTING, TRUE, D3DRS_SPECULARENABLE, FALSE, D3DRS_ALPHABLENDENABLE, FALSE, 0, 0};

static const unsigned long GBindTable_06ed1484[] = {
    D3DRS_LIGHTING, TRUE, D3DRS_SPECULARENABLE, FALSE, D3DRS_ALPHABLENDENABLE, FALSE, 0, 0};
static const unsigned long GBindTable_06ed14a4[] = {
    D3DTSS_COLOROP, D3DTOP_MODULATE, D3DTSS_COLORARG1, D3DTA_TEXTURE,
    D3DTSS_COLORARG2, D3DTA_DIFFUSE, D3DTSS_ALPHAOP, D3DTOP_MODULATE,
    D3DTSS_ALPHAARG1, D3DTA_TEXTURE, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE, 0, 0};
static const unsigned long GBindTable_06ed14dc[] = {
    D3DTSS_COLOROP, D3DTOP_MODULATE2X, D3DTSS_COLORARG1, D3DTA_TEXTURE,
    D3DTSS_COLORARG2, D3DTA_CURRENT, D3DTSS_ALPHAOP, D3DTOP_MODULATE,
    D3DTSS_ALPHAARG1, D3DTA_TEXTURE, D3DTSS_ALPHAARG2, D3DTA_CURRENT, 0, 0};
static const unsigned long GBindTable_06ed1514[] = {
    D3DTSS_COLOROP, D3DTOP_DISABLE, D3DTSS_COLORARG1, D3DTA_TEXTURE,
    D3DTSS_COLORARG2, D3DTA_CURRENT, D3DTSS_ALPHAOP, D3DTOP_DISABLE,
    D3DTSS_ALPHAARG1, D3DTA_TEXTURE, D3DTSS_ALPHAARG2, D3DTA_CURRENT, 0, 0};

static const unsigned long GBindTable_06ed16cc[] = {
    D3DRS_LIGHTING, TRUE, D3DRS_SPECULARENABLE, FALSE, D3DRS_ALPHABLENDENABLE, FALSE, 0, 0};
static const unsigned long GBindTable_06ed16ec[] = {
    D3DTSS_COLOROP, D3DTOP_MODULATE, D3DTSS_COLORARG1, D3DTA_TEXTURE,
    D3DTSS_COLORARG2, D3DTA_DIFFUSE, D3DTSS_ALPHAOP, D3DTOP_MODULATE,
    D3DTSS_ALPHAARG1, D3DTA_TEXTURE, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE, 0, 0};
static const unsigned long GBindTable_06ed1724[] = {
    D3DTSS_COLOROP, D3DTOP_ADD, D3DTSS_COLORARG1, D3DTA_TEXTURE,
    D3DTSS_COLORARG2, D3DTA_CURRENT, D3DTSS_ALPHAOP, D3DTOP_MODULATE,
    D3DTSS_ALPHAARG1, D3DTA_TEXTURE, D3DTSS_ALPHAARG2, D3DTA_CURRENT, 0, 0};
static const unsigned long GBindTable_06ed175c[] = {
    D3DTSS_COLOROP, D3DTOP_DISABLE, D3DTSS_COLORARG1, D3DTA_TEXTURE,
    D3DTSS_COLORARG2, D3DTA_CURRENT, D3DTSS_ALPHAOP, D3DTOP_DISABLE,
    D3DTSS_ALPHAARG1, D3DTA_TEXTURE, D3DTSS_ALPHAARG2, D3DTA_CURRENT, 0, 0};

static const unsigned long GBindTable_06ed1c84[] = {
    D3DTSS_COLOROP, D3DTOP_MODULATE, D3DTSS_COLORARG1, D3DTA_TEXTURE,
    D3DTSS_COLORARG2, D3DTA_DIFFUSE, D3DTSS_ALPHAOP, D3DTOP_MODULATE,
    D3DTSS_ALPHAARG1, D3DTA_TEXTURE, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE, 0, 0};
static const unsigned long GBindTable_06ed1cbc[] = {
    D3DTSS_COLOROP, D3DTOP_DISABLE, D3DTSS_COLORARG1, D3DTA_TEXTURE,
    D3DTSS_COLORARG2, D3DTA_CURRENT, D3DTSS_ALPHAOP, D3DTOP_DISABLE,
    D3DTSS_ALPHAARG1, D3DTA_TEXTURE, D3DTSS_ALPHAARG2, D3DTA_CURRENT, 0, 0};
static const unsigned long GBindTable_06ed1c54[] = {
    D3DRS_LIGHTING, TRUE, D3DRS_SPECULARENABLE, FALSE, D3DRS_ALPHABLENDENABLE, TRUE,
    D3DRS_SRCBLEND, D3DBLEND_ONE, D3DRS_DESTBLEND, D3DBLEND_ONE, 0, 0};

// FUN_06eb7120: one texture block, four calls in the original order.
static void BindDetailTexture(IRenderPipeline* Pipeline, const FTexBlock& Block,
                              unsigned long Stage)
{
    Pipeline->SetTextureStageTexture((int)Stage,
                                    (void*)(Block.Handle));
    Pipeline->SetTextureStageState((int)Stage, D3DTSS_TEXCOORDINDEX,
                                  Block.Flags >= 4 ? Block.Flags - 4 : 0);
    Pipeline->SetTextureStageState((int)Stage, D3DTSS_ADDRESSU, Block.AddressU + 1);
    Pipeline->SetTextureStageState((int)Stage, D3DTSS_ADDRESSV, Block.AddressV + 1);
}

static void ApplyDetailTable(IRenderPipeline* Pipeline, const unsigned long* Table,
                             bool bTextureStage, unsigned long Stage)
{
    for (unsigned long Index = 0; Table[Index] != 0; Index += 2)
    {
        if (bTextureStage)
            Pipeline->SetTextureStageState((int)Stage, Table[Index], Table[Index + 1]);
        else
            Pipeline->SetRenderState((D3DRENDERSTATETYPE)Table[Index], Table[Index + 1]);
    }
}

static void BindMaterialAndContext(IRenderPipeline* Pipeline, const float* Material,
                                   void* Context)
{
    SetMaterialBlock(Pipeline, Material);
    if (Context != nullptr)
    {
        const FRpVertexSrcDesc* Source = ((const FMaterialSubmitContext*)Context)->SourceVertices;
        if (Source != nullptr && Source->Diffuse.Base != nullptr)
            Pipeline->SetRenderState(D3DRS_COLORVERTEX, TRUE);
    }
}

static void SetDetailTileTransform(IRenderPipeline* Pipeline, float TileRate)
{
    // Only elements 0 and 5 are written after the sixteen-dword zero fill.
    float Matrix[16] = {};
    Matrix[0] = TileRate;
    Matrix[5] = TileRate;
    Pipeline->SetTextureStageTransform(1, Matrix);
}

bool FDetailMap2PassMaterial::ApplyDetailPassState(unsigned long Pass)
{
    // FUN_06eb71f0: +0x74 is the pass count, +0x79 is stage-1 verification.
    if (DetailPassCount == 1)
    {
        SetMaterialBlock(Pipeline, &DiffuseR);
        BindDetailTexture(Pipeline, TexBlocks[0], 0);
        ApplyDetailTable(Pipeline, GDetailTable_06ed18dc, true, 0);
        ApplyDetailTable(Pipeline, GDetailTable_06ed194c, true, 1);
        ApplyDetailTable(Pipeline, GDetailTable_06ed188c, false, 0);
        return true;
    }
    if (Pass == 0)
    {
        SetMaterialBlock(Pipeline, &DiffuseR);
        BindDetailTexture(Pipeline, TexBlocks[0], 0);
        ApplyDetailTable(Pipeline, GDetailTable_06ed18dc, true, 0);
        unsigned long DisabledStage = 1;
        if (bDetailSecondStageVerified)
        {
            BindDetailTexture(Pipeline, TexBlocks[1], 1);
            ApplyDetailTable(Pipeline, GDetailTable_06ed1914, true, 1);
            Pipeline->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 0);
            Pipeline->SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
            SetDetailTileTransform(Pipeline, DetailTileRate0);
            DisabledStage = 2;
        }
        ApplyDetailTable(Pipeline, GDetailTable_06ed194c, true, DisabledStage);
        ApplyDetailTable(Pipeline, GDetailTable_06ed188c, false, 0);
    }
    else if (Pass == 1 && bDetailSecondStageVerified)
    {
        ApplyDetailTable(Pipeline, GDetailTable_06ed194c, true, 1);
        ApplyDetailTable(Pipeline, GDetailTable_06ed18ac, false, 0);
        Pipeline->SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    }
    return true;
}

bool FIllumDetailMapMaterial::ApplyIllumDetailPassState(unsigned long Pass)
{
    // FUN_06eb9b10: no pass-count test; pass 1 always uses the third block.
    if (Pass == 0)
    {
        SetMaterialBlock(Pipeline, &DiffuseR);
        BindDetailTexture(Pipeline, TexBlocks[0], 0);
        ApplyDetailTable(Pipeline, GDetailTable_06ed1ecc, true, 0);
        unsigned long DisabledStage = 1;
        if (bDetailSecondStageVerified)
        {
            BindDetailTexture(Pipeline, TexBlocks[1], 1);
            ApplyDetailTable(Pipeline, GDetailTable_06ed1f04, true, 1);
            Pipeline->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 0);
            Pipeline->SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
            SetDetailTileTransform(Pipeline, DetailTileRate0);
            DisabledStage = 2;
        }
        ApplyDetailTable(Pipeline, GDetailTable_06ed1f74, true, DisabledStage);
        ApplyDetailTable(Pipeline, GDetailTable_06ed1e7c, false, 0);
    }
    else if (Pass == 1)
    {
        ApplyDetailTable(Pipeline, GDetailTable_06ed1f3c, true, 0);
        ApplyDetailTable(Pipeline, GDetailTable_06ed1f04, true, 1);
        ApplyDetailTable(Pipeline, GDetailTable_06ed1f74, true, 2);
        ApplyDetailTable(Pipeline, GDetailTable_06ed1e9c, false, 0);
        BindDetailTexture(Pipeline, TexBlocks[2], 1);
        Pipeline->SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
        SetDetailTileTransform(Pipeline, DetailTileRate1);
    }
    return true;
}

bool FMasked2DetailMapMaterial::ApplyMaskedDetailPassState(unsigned long Pass)
{
    // FUN_06ebb890: mode 1 ignores the requested pass.
    if (DetailPassCount == 1)
    {
        SetMaterialBlock(Pipeline, &DiffuseR);
        BindDetailTexture(Pipeline, TexBlocks[0], 0);
        ApplyDetailTable(Pipeline, GDetailTable_06ed225c, true, 0);
        ApplyDetailTable(Pipeline, GDetailTable_06ed22cc, true, 1);
        ApplyDetailTable(Pipeline, GDetailTable_06ed220c, false, 0);
        return true;
    }
    if (Pass == 0)
    {
        SetMaterialBlock(Pipeline, &DiffuseR);
        BindDetailTexture(Pipeline, TexBlocks[0], 0);
        BindDetailTexture(Pipeline, TexBlocks[1], 1);
        ApplyDetailTable(Pipeline, GDetailTable_06ed225c, true, 0);
        ApplyDetailTable(Pipeline, GDetailTable_06ed2294, true, 1);
        ApplyDetailTable(Pipeline, GDetailTable_06ed22cc, true, 2);
        ApplyDetailTable(Pipeline, GDetailTable_06ed220c, false, 0);
        Pipeline->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 0);
        Pipeline->SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
        SetDetailTileTransform(Pipeline, DetailTileRate0);
    }
    else if (Pass == 1)
    {
        ApplyDetailTable(Pipeline, GDetailTable_06ed2294, true, 1);
        ApplyDetailTable(Pipeline, GDetailTable_06ed22cc, true, 2);
        ApplyDetailTable(Pipeline, GDetailTable_06ed222c, false, 0);
        BindDetailTexture(Pipeline, TexBlocks[2], 1);
        Pipeline->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 0);
        Pipeline->SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
        SetDetailTileTransform(Pipeline, DetailTileRate1);
    }
    return true;
}

// Raw retail tables at 06ed1140/1178 and 06ed23fc/2434 are identical.
static const unsigned long GAtmosphereNebulaTss0[] = {
    D3DTSS_COLOROP, D3DTOP_MODULATE, D3DTSS_COLORARG1, D3DTA_TEXTURE,
    D3DTSS_COLORARG2, D3DTA_DIFFUSE, D3DTSS_ALPHAOP, D3DTOP_MODULATE,
    D3DTSS_ALPHAARG1, D3DTA_TEXTURE, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE, 0, 0};
static const unsigned long GAtmosphereNebulaTss1[] = {
    D3DTSS_COLOROP, D3DTOP_DISABLE, D3DTSS_COLORARG1, D3DTA_TEXTURE,
    D3DTSS_COLORARG2, D3DTA_CURRENT, D3DTSS_ALPHAOP, D3DTOP_DISABLE,
    D3DTSS_ALPHAARG1, D3DTA_TEXTURE, D3DTSS_ALPHAARG2, D3DTA_CURRENT, 0, 0};
// Raw retail RS tables 06ed1100 and 06ed23cc.
static const unsigned long GAtmosphereRs[] = {
    D3DRS_LIGHTING, TRUE, D3DRS_SPECULARENABLE, FALSE, D3DRS_ALPHABLENDENABLE, TRUE,
    D3DRS_SRCBLEND, D3DBLEND_SRCALPHA, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA, 0, 0};
static const unsigned long GNebulaRs[] = {
    D3DRS_LIGHTING, FALSE, D3DRS_SPECULARENABLE, FALSE, D3DRS_ALPHABLENDENABLE, TRUE,
    D3DRS_SRCBLEND, D3DBLEND_SRCALPHA, D3DRS_DESTBLEND, D3DBLEND_ONE, 0, 0};

namespace
{
// Shared contiguous property schemas; lazy initialization publishes the guard first.
char GPropertyMetadata_06ed56c4[4] = {};
FShadingPropertyRecord GAtmospherePropertySchema[9] = {};
bool GbAtmospherePropertySchemaInitialized = false;

// Single-pass and detail families share their respective contiguous property tables.
FShadingPropertyRecord GSinglePassPropertySchema[10] = {};
bool GbSinglePassPropertySchemaInitialized = false;
FShadingPropertyRecord GIllumDetailPropertySchema[18] = {};
bool GbIllumDetailPropertySchemaInitialized = false;
FShadingPropertyRecord GMaskedDetailPropertySchema[18] = {};
bool GbMaskedDetailPropertySchemaInitialized = false;
FShadingPropertyRecord GBtDetailPropertySchema[13] = {};
bool GbBtDetailPropertySchemaInitialized = false;
FShadingPropertyRecord GDetailMapPropertySchema[13] = {};
bool GbDetailMapPropertySchemaInitialized = false;
FShadingPropertyRecord GDetail2PassPropertySchema[13] = {};
bool GbDetail2PassPropertySchemaInitialized = false;
FShadingPropertyRecord GEcEtPropertySchema[5] = {};
bool GbEcEtPropertySchemaInitialized = false;
FShadingPropertyRecord GDcDtEtPropertySchema[9] = {};
bool GbDcDtEtPropertySchemaInitialized = false;
FShadingPropertyRecord GNebulaPropertySchema[4] = {};
bool GbNebulaPropertySchemaInitialized = false;
FShadingPropertyRecord GLightMapPropertySchema[4] = {};
bool GbLightMapPropertySchemaInitialized = false;

// FUN_06eb2a60 clears only metadata, tags and first payload words.
FShadingPropertyRecord* ConstructPropertyRecord(FShadingPropertyRecord* Record)
{
    Record->Current.Type = 0;
    Record->Current.Payload0 = 0;
    Record->Minimum.Type = 0;
    Record->Minimum.Payload0 = 0;
    Record->Maximum.Type = 0;
    Record->Maximum.Payload0 = 0;
    Record->Flags = 0;
    Record->Name = nullptr;
    Record->Description = nullptr;
    Record->Callback = nullptr;
    return Record;
}

void ConstructPropertyRecords(FShadingPropertyRecord* Records, unsigned long Count)
{
    for (unsigned long Index = 0; Index < Count; ++Index)
        ConstructPropertyRecord(&Records[Index]);
}

// All schema on-exit bodies clear the COM pointer before calling Release.
void ReleaseSchemaPayload(FShadingPropertyValue& Value)
{
    if (Value.Type == kMaterialParamComponent)
    {
        IDAComponent* Component = (IDAComponent*)(Value.Payload0);
        Value.Payload0 = 0;
        if (Component != nullptr)
            Component->Release();
        Value.Payload0 = 0;
    }
    else
        Value.ReleaseOwnedPayload();
}

void DestroyPropertyRecords(FShadingPropertyRecord* Records, unsigned long Count)
{
    for (unsigned long Remaining = Count; Remaining != 0; --Remaining)
    {
        FShadingPropertyRecord& Record = Records[Remaining - 1];
        Record.Flags = 0;
        Record.Name = nullptr;
        Record.Description = nullptr;
        Record.Callback = nullptr;
        ReleaseSchemaPayload(Record.Maximum);
        ReleaseSchemaPayload(Record.Minimum);
        ReleaseSchemaPayload(Record.Current);
    }
}

void DestroyAtmospherePropertySchema()
{
    DestroyPropertyRecords(GAtmospherePropertySchema, 9);
}

void DestroySinglePassPropertySchema()
{
    DestroyPropertyRecords(GSinglePassPropertySchema, 10);
}

void DestroyIllumDetailPropertySchema()
{
    DestroyPropertyRecords(GIllumDetailPropertySchema, 18);
}

void DestroyEcEtPropertySchema()
{
    DestroyPropertyRecords(GEcEtPropertySchema, 5);
}

void DestroyDcDtEtPropertySchema()
{
    DestroyPropertyRecords(GDcDtEtPropertySchema, 9);
}

void SetAtmospherePropertyRecord(FShadingPropertyRecord& Record, unsigned long Flags,
                                 const char* Name, const char* Description,
                                 unsigned long CurrentType, const void* Current,
                                 unsigned long MinimumType, const void* Minimum,
                                 unsigned long MaximumType, const void* Maximum)
{
    Record.Flags = Flags;
    Record.Name = Name;
    Record.Description = Description;
    Record.Callback = GPropertyMetadata_06ed56c4;
    Record.Current.Assign(CurrentType, Current);
    Record.Minimum.Assign(MinimumType, Minimum);
    Record.Maximum.Assign(MaximumType, Maximum);
}

void SetSinglePassPropertyRecord(FShadingPropertyRecord& Record, unsigned long Flags,
                                 const char* Name, const char* Description,
                                 unsigned long CurrentType, const void* Current,
                                 unsigned long MinimumType, const void* Minimum,
                                 unsigned long MaximumType, const void* Maximum)
{
    Record.Flags = Flags;
    Record.Name = Name;
    Record.Description = Description;
    // Retail stores the address 06ed56c4 itself, not its contents.
    Record.Callback = GPropertyMetadata_06ed56c4;
    Record.Current.Assign(CurrentType, Current);
    Record.Minimum.Assign(MinimumType, Minimum);
    Record.Maximum.Assign(MaximumType, Maximum);
}

void InitializeAtmospherePropertySchema()
{
    if (!GbAtmospherePropertySchemaInitialized)
    {
        GbAtmospherePropertySchemaInitialized = true;
        ConstructPropertyRecords(GAtmospherePropertySchema, 9);
        std::atexit(DestroyAtmospherePropertySchema);
    }
    if (GAtmospherePropertySchema[0].Current.Type != 0)
        return;

    static const unsigned long Payload_0_0[] = {0x3F800000, 0x3F800000, 0x3F800000};
    static const unsigned long Payload_0_1[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_0_2[] = {0x3F800000, 0x3F800000, 0x3F800000};
    SetAtmospherePropertyRecord(GAtmospherePropertySchema[kAtmosphereAmbient], 0x0, "Ac", "Ambient Color",
                                kMaterialParamVector3, Payload_0_0,
                                kMaterialParamVector3, Payload_0_1,
                                kMaterialParamVector3, Payload_0_2);
    static const unsigned long Payload_1_0[] = {0x3F800000, 0x3F800000, 0x3F800000};
    static const unsigned long Payload_1_1[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_1_2[] = {0x41200000, 0x41200000, 0x41200000};
    SetAtmospherePropertyRecord(GAtmospherePropertySchema[kAtmosphereDiffuse], 0x0, "Dc", "Diffuse Color",
                                kMaterialParamVector3, Payload_1_0,
                                kMaterialParamVector3, Payload_1_1,
                                kMaterialParamVector3, Payload_1_2);
    SetAtmospherePropertyRecord(GAtmospherePropertySchema[kAtmosphereFadeTextureName], 0x0, "Dt_name", "Fade 1D Texture Name",
                                kMaterialParamTextureName, "(None)",
                                kMaterialParamTextureName, nullptr,
                                kMaterialParamTextureName, nullptr);
    SetAtmospherePropertyRecord(GAtmospherePropertySchema[kAtmosphereFadeTextureWrap], 0x0, "Dt_flags", "Fade 1D Texture Wrap Mode",
                                kMaterialParamWrapMode, (void*)4,
                                kMaterialParamWrapMode, nullptr,
                                kMaterialParamWrapMode, nullptr);
    SetAtmospherePropertyRecord(GAtmospherePropertySchema[kAtmosphereFadeTextureAddressU], kMaterialParamFlagAddressU, "Dt_flags", "Fade 1D Texture U Address Mode",
                                kMaterialParamAddressMode, (void*)2,
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr);
    SetAtmospherePropertyRecord(GAtmospherePropertySchema[kAtmosphereFadeTextureAddressV], kMaterialParamFlagAddressV, "Dt_flags", "Fade 1D Texture V Address Mode",
                                kMaterialParamAddressMode, (void*)2,
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr);
    static const unsigned long Payload_6_0[] = {0x3F800000};
    static const unsigned long Payload_6_1[] = {0xFF7FFFFF};
    static const unsigned long Payload_6_2[] = {0x7F7FFFFF};
    SetAtmospherePropertyRecord(GAtmospherePropertySchema[kAtmosphereFadeOff], 0x0, "Fade", "Fade Off",
                                kMaterialParamFloat, Payload_6_0,
                                kMaterialParamFloat, Payload_6_1,
                                kMaterialParamFloat, Payload_6_2);
    static const unsigned long Payload_7_0[] = {0x3F400000};
    static const unsigned long Payload_7_1[] = {0xFF7FFFFF};
    static const unsigned long Payload_7_2[] = {0x7F7FFFFF};
    SetAtmospherePropertyRecord(GAtmospherePropertySchema[kAtmosphereAlpha], 0x0, "Alpha", "Alpha",
                                kMaterialParamFloat, Payload_7_0,
                                kMaterialParamFloat, Payload_7_1,
                                kMaterialParamFloat, Payload_7_2);
    static const unsigned long Payload_8_0[] = {0x3F833333};
    static const unsigned long Payload_8_1[] = {0xFF7FFFFF};
    static const unsigned long Payload_8_2[] = {0x7F7FFFFF};
    SetAtmospherePropertyRecord(GAtmospherePropertySchema[kAtmosphereScale], 0x0, "Scale", "Scale",
                                kMaterialParamFloat, Payload_8_0,
                                kMaterialParamFloat, Payload_8_1,
                                kMaterialParamFloat, Payload_8_2);
}

void InitializeSinglePassPropertySchema()
{
    if (!GbSinglePassPropertySchemaInitialized)
    {
        GbSinglePassPropertySchemaInitialized = true;
        ConstructPropertyRecords(GSinglePassPropertySchema, 10);
        std::atexit(DestroySinglePassPropertySchema);
    }
    if (GSinglePassPropertySchema[0].Current.Type != 0)
        return;

    static const unsigned long Payload_0_0[] = {0x3F800000, 0x3F800000, 0x3F800000};
    static const unsigned long Payload_0_1[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_0_2[] = {0x3F800000, 0x3F800000, 0x3F800000};
    SetSinglePassPropertyRecord(GSinglePassPropertySchema[kSinglePassAmbient], 0x0, "Ac", "Ambient Color",
                                kMaterialParamVector3, Payload_0_0,
                                kMaterialParamVector3, Payload_0_1,
                                kMaterialParamVector3, Payload_0_2);
    static const unsigned long Payload_1_0[] = {0x3F800000, 0x3F800000, 0x3F800000};
    static const unsigned long Payload_1_1[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_1_2[] = {0x3F800000, 0x3F800000, 0x3F800000};
    SetSinglePassPropertyRecord(GSinglePassPropertySchema[kSinglePassDiffuse], 0x0, "Dc", "Diffuse Color",
                                kMaterialParamVector3, Payload_1_0,
                                kMaterialParamVector3, Payload_1_1,
                                kMaterialParamVector3, Payload_1_2);
    static const unsigned long Payload_2_0[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_2_1[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_2_2[] = {0x3F800000, 0x3F800000, 0x3F800000};
    SetSinglePassPropertyRecord(GSinglePassPropertySchema[kSinglePassEmissive], 0x0, "Ec", "Emissive Color",
                                kMaterialParamVector3, Payload_2_0,
                                kMaterialParamVector3, Payload_2_1,
                                kMaterialParamVector3, Payload_2_2);
    static const unsigned long Payload_3_0[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_3_1[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_3_2[] = {0x3F800000, 0x3F800000, 0x3F800000};
    SetSinglePassPropertyRecord(GSinglePassPropertySchema[kSinglePassSpecular], 0x0, "Sc", "Specular Color",
                                kMaterialParamVector3, Payload_3_0,
                                kMaterialParamVector3, Payload_3_1,
                                kMaterialParamVector3, Payload_3_2);
    static const unsigned long Payload_4_0[] = {0x00000000};
    static const unsigned long Payload_4_1[] = {0x00000000};
    static const unsigned long Payload_4_2[] = {0x3F800000};
    SetSinglePassPropertyRecord(GSinglePassPropertySchema[kSinglePassSpecularPower], 0x0, "Sp", "Specular Power",
                                kMaterialParamFloat, Payload_4_0,
                                kMaterialParamFloat, Payload_4_1,
                                kMaterialParamFloat, Payload_4_2);
    static const unsigned long Payload_5_0[] = {0x3F800000};
    static const unsigned long Payload_5_1[] = {0x00000000};
    static const unsigned long Payload_5_2[] = {0x3F800000};
    SetSinglePassPropertyRecord(GSinglePassPropertySchema[kSinglePassOpacity], 0x0, "Oc", "Opacity ",
                                kMaterialParamFloat, Payload_5_0,
                                kMaterialParamFloat, Payload_5_1,
                                kMaterialParamFloat, Payload_5_2);
    SetSinglePassPropertyRecord(GSinglePassPropertySchema[kSinglePassTextureName], 0x0, "Dt_name", "Texture Name",
                                kMaterialParamTextureName, "(None)",
                                kMaterialParamTextureName, nullptr,
                                kMaterialParamTextureName, nullptr);
    SetSinglePassPropertyRecord(GSinglePassPropertySchema[kSinglePassTextureWrap], 0x0, "Dt_flags", "Texture Wrap Mode",
                                kMaterialParamWrapMode, (void*)4,
                                kMaterialParamWrapMode, nullptr,
                                kMaterialParamWrapMode, nullptr);
    SetSinglePassPropertyRecord(GSinglePassPropertySchema[kSinglePassTextureAddressU], kMaterialParamFlagAddressU, "Dt_flags", "Texture U Address Mode",
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr);
    SetSinglePassPropertyRecord(GSinglePassPropertySchema[kSinglePassTextureAddressV], kMaterialParamFlagAddressV, "Dt_flags", "Texture V Address Mode",
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr);
}

void InitializeDetailPropertyRecords(FShadingPropertyRecord* Records)
{
    static const unsigned long kZero3[3] = {0, 0, 0};
    static const unsigned long kWhite3[3] = {0x3F800000, 0x3F800000, 0x3F800000};
    static const unsigned long kFourBits = 0x40800000;
    static const unsigned long kFloatMinBits = 0xFF7FFFFF;
    static const unsigned long kFloatMaxBits = 0x7F7FFFFF;

    SetSinglePassPropertyRecord(Records[kTwoDetailMapAmbient], 0, "Ac", "Ambient Color", kMaterialParamVector3,
                                kWhite3, kMaterialParamVector3, kZero3, kMaterialParamVector3, kWhite3);
    SetSinglePassPropertyRecord(Records[kTwoDetailMapDiffuse], 0, "Dc", "Diffuse Color", kMaterialParamVector3,
                                kWhite3, kMaterialParamVector3, kZero3, kMaterialParamVector3, kWhite3);
    SetSinglePassPropertyRecord(Records[kTwoDetailMapDiffuseTextureName], 0, "Dt_name",
                                "Diffuse Texture Name", kMaterialParamTextureName, "(None)", kMaterialParamTextureName, nullptr, kMaterialParamTextureName, nullptr);
    SetSinglePassPropertyRecord(Records[kTwoDetailMapDiffuseTextureWrap], 0, "Dt_flags",
                                "Diffuse Texture Wrap Mode", kMaterialParamWrapMode, (void*)4,
                                kMaterialParamWrapMode, nullptr, kMaterialParamWrapMode, nullptr);
    SetSinglePassPropertyRecord(Records[kTwoDetailMapDiffuseTextureAddressU], kMaterialParamFlagAddressU, "Dt_flags",
                                "Diffuse Texture U Address Mode", kMaterialParamAddressMode, (void*)2, kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr);
    SetSinglePassPropertyRecord(Records[kTwoDetailMapDiffuseTextureAddressV], kMaterialParamFlagAddressV, "Dt_flags",
                                "Diffuse Texture V Address Mode", kMaterialParamAddressMode, (void*)2, kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr);
    SetSinglePassPropertyRecord(Records[kTwoDetailMapFlipU], 0, "flip u", "Flip U ", kMaterialParamInteger,
                                nullptr, kMaterialParamInteger, nullptr, kMaterialParamInteger, (void*)1);
    SetSinglePassPropertyRecord(Records[kTwoDetailMapFlipV], 0, "flip v", "Flip V ", kMaterialParamInteger,
                                nullptr, kMaterialParamInteger, nullptr, kMaterialParamInteger, (void*)1);
    SetSinglePassPropertyRecord(Records[kTwoDetailMapAlpha0TextureName], 0, "Dm0_name",
                                "Detail Texture Alpha0 Name", kMaterialParamTextureName, "(None)", kMaterialParamTextureName, nullptr, kMaterialParamTextureName,
                                nullptr);
    SetSinglePassPropertyRecord(Records[kTwoDetailMapAlpha0TextureWrap], 0, "Dm0_flags",
                                "Detail Texture Alpha0 Wrap Mode", kMaterialParamWrapMode,
                                (void*)4, kMaterialParamWrapMode, nullptr, kMaterialParamWrapMode, nullptr);
    SetSinglePassPropertyRecord(Records[kTwoDetailMapAlpha0TextureAddressU], kMaterialParamFlagAddressU, "Dm0_flags",
                                "Detail Texture Alpha0 U Address Mode", kMaterialParamAddressMode, nullptr, kMaterialParamAddressMode,
                                nullptr, kMaterialParamAddressMode, nullptr);
    SetSinglePassPropertyRecord(Records[kTwoDetailMapAlpha0TextureAddressV], kMaterialParamFlagAddressV, "Dm0_flags",
                                "Detail Texture Alpha0 V Address Mode", kMaterialParamAddressMode, nullptr, kMaterialParamAddressMode,
                                nullptr, kMaterialParamAddressMode, nullptr);
    SetSinglePassPropertyRecord(Records[kTwoDetailMapTileRate0], 0, "TileRate0",
                                "Tile Rate Alpha0", kMaterialParamFloat, &kFourBits, kMaterialParamFloat, &kFloatMinBits, kMaterialParamFloat,
                                &kFloatMaxBits);
    SetSinglePassPropertyRecord(Records[kTwoDetailMapAlpha1TextureName], 0, "Dm1_name",
                                "Detail Texture Alpha1 Name", kMaterialParamTextureName, "(None)", kMaterialParamTextureName, nullptr, kMaterialParamTextureName,
                                nullptr);
    SetSinglePassPropertyRecord(Records[kTwoDetailMapAlpha1TextureWrap], 0, "Dm1_flags",
                                "Detail Texture Alpha1 Wrap Mode", kMaterialParamWrapMode,
                                (void*)4, kMaterialParamWrapMode, nullptr, kMaterialParamWrapMode, nullptr);
    SetSinglePassPropertyRecord(Records[kTwoDetailMapAlpha1TextureAddressU], kMaterialParamFlagAddressU, "Dm1_flags",
                                "Detail Texture Alpha1 U Address Mode", kMaterialParamAddressMode, nullptr, kMaterialParamAddressMode,
                                nullptr, kMaterialParamAddressMode, nullptr);
    SetSinglePassPropertyRecord(Records[kTwoDetailMapAlpha1TextureAddressV], kMaterialParamFlagAddressV, "Dm1_flags",
                                "Detail Texture Alpha1 V Address Mode", kMaterialParamAddressMode, nullptr, kMaterialParamAddressMode,
                                nullptr, kMaterialParamAddressMode, nullptr);
    SetSinglePassPropertyRecord(Records[kTwoDetailMapTileRate1], 0, "TileRate1",
                                "Tile Rate Alpha1", kMaterialParamFloat, &kFourBits, kMaterialParamFloat, &kFloatMinBits, kMaterialParamFloat,
                                &kFloatMaxBits);
}

void InitializeIllumDetailPropertySchema()
{
    if (!GbIllumDetailPropertySchemaInitialized)
    {
        GbIllumDetailPropertySchemaInitialized = true;
        ConstructPropertyRecords(GIllumDetailPropertySchema, 18);
        std::atexit(DestroyIllumDetailPropertySchema);
    }
    // Retail's second guard tests record 0's Current.Type on its own.
    if (GIllumDetailPropertySchema[0].Current.Type != 0)
        return;
    InitializeDetailPropertyRecords(GIllumDetailPropertySchema);
}

void DestroyMaskedDetailPropertySchema()
{
    DestroyPropertyRecords(GMaskedDetailPropertySchema, 18);
}

void InitializeMaskedDetailPropertySchema()
{
    if (!GbMaskedDetailPropertySchemaInitialized)
    {
        GbMaskedDetailPropertySchemaInitialized = true;
        ConstructPropertyRecords(GMaskedDetailPropertySchema, 18);
        std::atexit(DestroyMaskedDetailPropertySchema);
    }
    // Retail's second guard tests record 0's Current.Type on its own.
    if (GMaskedDetailPropertySchema[0].Current.Type != 0)
        return;
    InitializeDetailPropertyRecords(GMaskedDetailPropertySchema);
}

void InitializeDetail2PassPropertyRecords(FShadingPropertyRecord* Records)
{
    static const unsigned long kZero3[3] = {0, 0, 0};
    static const unsigned long kWhite3[3] = {0x3F800000, 0x3F800000, 0x3F800000};
    static const unsigned long kTen3[3] = {0x41200000, 0x41200000, 0x41200000};
    static const unsigned long kFourBits = 0x40800000;
    static const unsigned long kFloatMinBits = 0xFF7FFFFF;
    static const unsigned long kFloatMaxBits = 0x7F7FFFFF;

    SetSinglePassPropertyRecord(Records[kDetailMapAmbient], 0, "Ac", "Ambient Color", kMaterialParamVector3,
                                kWhite3, kMaterialParamVector3, kZero3, kMaterialParamVector3, kWhite3);
    SetSinglePassPropertyRecord(Records[kDetailMapDiffuse], 0, "Dc", "Diffuse Color", kMaterialParamVector3,
                                kWhite3, kMaterialParamVector3, kZero3, kMaterialParamVector3, kTen3);
    SetSinglePassPropertyRecord(Records[kDetailMapDiffuseTextureName], 0, "Dt_name",
                                "Diffuse Texture Name", kMaterialParamTextureName, "(None)", kMaterialParamTextureName, nullptr, kMaterialParamTextureName, nullptr);
    SetSinglePassPropertyRecord(Records[kDetailMapDiffuseTextureWrap], 0, "Dt_flags",
                                "Diffuse Texture Wrap Mode", kMaterialParamWrapMode, (void*)4,
                                kMaterialParamWrapMode, nullptr, kMaterialParamWrapMode, nullptr);
    SetSinglePassPropertyRecord(Records[kDetailMapDiffuseTextureAddressU], kMaterialParamFlagAddressU, "Dt_flags",
                                "Diffuse Texture U Address Mode", kMaterialParamAddressMode, (void*)2, kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr);
    SetSinglePassPropertyRecord(Records[kDetailMapDiffuseTextureAddressV], kMaterialParamFlagAddressV, "Dt_flags",
                                "Diffuse Texture V Address Mode", kMaterialParamAddressMode, (void*)2, kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr);
    SetSinglePassPropertyRecord(Records[kDetailMapFlipU], 0, "flip u", "Flip U ", kMaterialParamInteger,
                                nullptr, kMaterialParamInteger, nullptr, kMaterialParamInteger, (void*)1);
    SetSinglePassPropertyRecord(Records[kDetailMapFlipV], 0, "flip v", "Flip V ", kMaterialParamInteger,
                                nullptr, kMaterialParamInteger, nullptr, kMaterialParamInteger, (void*)1);
    SetSinglePassPropertyRecord(Records[kDetailMapDetailTextureName], 0, "Dm1_name",
                                "Detail Texture Name", kMaterialParamTextureName, "(None)", kMaterialParamTextureName, nullptr, kMaterialParamTextureName,
                                nullptr);
    SetSinglePassPropertyRecord(Records[kDetailMapDetailTextureWrap], 0, "Dm1_flags",
                                "Detail Texture Wrap Mode", kMaterialParamWrapMode,
                                (void*)4, kMaterialParamWrapMode, nullptr, kMaterialParamWrapMode, nullptr);
    SetSinglePassPropertyRecord(Records[kDetailMapDetailTextureAddressU], kMaterialParamFlagAddressU, "Dm1_flags",
                                "Detail Texture U Address Mode", kMaterialParamAddressMode, nullptr, kMaterialParamAddressMode,
                                nullptr, kMaterialParamAddressMode, nullptr);
    SetSinglePassPropertyRecord(Records[kDetailMapDetailTextureAddressV], kMaterialParamFlagAddressV, "Dm1_flags",
                                "Detail Texture V Address Mode", kMaterialParamAddressMode, nullptr, kMaterialParamAddressMode,
                                nullptr, kMaterialParamAddressMode, nullptr);
    SetSinglePassPropertyRecord(Records[kDetailMapTileRate], 0, "TileRate",
                                "Tile Rate ", kMaterialParamFloat, &kFourBits, kMaterialParamFloat, &kFloatMinBits, kMaterialParamFloat,
                                &kFloatMaxBits);
}

void DestroyDetail2PassPropertySchema()
{
    DestroyPropertyRecords(GDetail2PassPropertySchema, 13);
}

void InitializeDetail2PassPropertySchema()
{
    if (!GbDetail2PassPropertySchemaInitialized)
    {
        GbDetail2PassPropertySchemaInitialized = true;
        ConstructPropertyRecords(GDetail2PassPropertySchema, 13);
        std::atexit(DestroyDetail2PassPropertySchema);
    }
    // Retail's second guard tests record 0's Current.Type on its own.
    if (GDetail2PassPropertySchema[0].Current.Type != 0)
        return;
    InitializeDetail2PassPropertyRecords(GDetail2PassPropertySchema);
}

// LAB_06eb7ce0/7d00.
void DestroyDetailMapPropertySchema()
{
    DestroyPropertyRecords(GDetailMapPropertySchema, 13);
}

void InitializeDetailMapPropertySchema()
{
    if (!GbDetailMapPropertySchemaInitialized)
    {
        GbDetailMapPropertySchemaInitialized = true;
        ConstructPropertyRecords(GDetailMapPropertySchema, 13);
        std::atexit(DestroyDetailMapPropertySchema);
    }
    if (GDetailMapPropertySchema[0].Current.Type != 0)
        return;

    static const unsigned long Payload_0_0[] = {0x3F800000, 0x3F800000, 0x3F800000};
    static const unsigned long Payload_0_1[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_0_2[] = {0x3F800000, 0x3F800000, 0x3F800000};
    SetSinglePassPropertyRecord(GDetailMapPropertySchema[kDetailMapAmbient], 0x0, "Ac", "Ambient Color",
                                kMaterialParamVector3, Payload_0_0,
                                kMaterialParamVector3, Payload_0_1,
                                kMaterialParamVector3, Payload_0_2);
    static const unsigned long Payload_1_0[] = {0x3F800000, 0x3F800000, 0x3F800000};
    static const unsigned long Payload_1_1[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_1_2[] = {0x41200000, 0x41200000, 0x41200000};
    SetSinglePassPropertyRecord(GDetailMapPropertySchema[kDetailMapDiffuse], 0x0, "Dc", "Diffuse Color",
                                kMaterialParamVector3, Payload_1_0,
                                kMaterialParamVector3, Payload_1_1,
                                kMaterialParamVector3, Payload_1_2);
    SetSinglePassPropertyRecord(GDetailMapPropertySchema[kDetailMapDiffuseTextureName], 0x0, "Dt_name", "Diffuse Texture Name",
                                kMaterialParamTextureName, "(None)",
                                kMaterialParamTextureName, nullptr,
                                kMaterialParamTextureName, nullptr);
    SetSinglePassPropertyRecord(GDetailMapPropertySchema[kDetailMapDiffuseTextureWrap], 0x0, "Dt_flags", "Diffuse Texture Wrap Mode",
                                kMaterialParamWrapMode, (void*)4,
                                kMaterialParamWrapMode, nullptr,
                                kMaterialParamWrapMode, nullptr);
    SetSinglePassPropertyRecord(GDetailMapPropertySchema[kDetailMapDiffuseTextureAddressU], kMaterialParamFlagAddressU, "Dt_flags", "Diffuse Texture U Address Mode",
                                kMaterialParamAddressMode, (void*)2,
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr);
    SetSinglePassPropertyRecord(GDetailMapPropertySchema[kDetailMapDiffuseTextureAddressV], kMaterialParamFlagAddressV, "Dt_flags", "Diffuse Texture V Address Mode",
                                kMaterialParamAddressMode, (void*)2,
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr);
    SetSinglePassPropertyRecord(GDetailMapPropertySchema[kDetailMapFlipU], 0x0, "flip u", "Flip U ",
                                kMaterialParamInteger, nullptr,
                                kMaterialParamInteger, nullptr,
                                kMaterialParamInteger, (void*)1);
    SetSinglePassPropertyRecord(GDetailMapPropertySchema[kDetailMapFlipV], 0x0, "flip v", "Flip V ",
                                kMaterialParamInteger, nullptr,
                                kMaterialParamInteger, nullptr,
                                kMaterialParamInteger, (void*)1);
    SetSinglePassPropertyRecord(GDetailMapPropertySchema[kDetailMapDetailTextureName], 0x0, "Dm_name", "Detail Texture Name",
                                kMaterialParamTextureName, "(None)",
                                kMaterialParamTextureName, nullptr,
                                kMaterialParamTextureName, nullptr);
    SetSinglePassPropertyRecord(GDetailMapPropertySchema[kDetailMapDetailTextureWrap], 0x0, "Dm_flags", "Detail Texture Wrap Mode",
                                kMaterialParamWrapMode, (void*)4,
                                kMaterialParamWrapMode, nullptr,
                                kMaterialParamWrapMode, nullptr);
    SetSinglePassPropertyRecord(GDetailMapPropertySchema[kDetailMapDetailTextureAddressU], kMaterialParamFlagAddressU, "Dm_flags", "Detail Texture U Address Mode",
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr);
    SetSinglePassPropertyRecord(GDetailMapPropertySchema[kDetailMapDetailTextureAddressV], kMaterialParamFlagAddressV, "Dm_flags", "Detail Texture V Address Mode",
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr);
    static const unsigned long Payload_12_0[] = {0x40800000};
    static const unsigned long Payload_12_1[] = {0xFF7FFFFF};
    static const unsigned long Payload_12_2[] = {0x7F7FFFFF};
    SetSinglePassPropertyRecord(GDetailMapPropertySchema[kDetailMapTileRate], 0x0, "TileRate", "Tile Rate ",
                                kMaterialParamFloat, Payload_12_0,
                                kMaterialParamFloat, Payload_12_1,
                                kMaterialParamFloat, Payload_12_2);
}

void DestroyBtDetailPropertySchema()
{
    DestroyPropertyRecords(GBtDetailPropertySchema, 13);
}

// The fourteenth Bt descriptor overlaps Atmosphere by one word. Keep each
// aliased tag/payload write live during foreign Release, without forming a
// second C++ record object over the first one's storage.
void AssignOverlappingZeroAddressMode(void* TypeAddress, unsigned long& Payload)
{
    unsigned long PreviousType;
    std::memcpy(&PreviousType, TypeAddress, sizeof(PreviousType));
    if (PreviousType != kMaterialParamAddressMode)
    {
        if (PreviousType == kMaterialParamString || PreviousType == kMaterialParamTextureName)
        {
            if (Payload != 0)
                HeapFree(GetProcessHeap(), 0, (void*)Payload);
        }
        else if (PreviousType == kMaterialParamComponent)
        {
            IDAComponent* Component = (IDAComponent*)Payload;
            Payload = 0;
            if (Component != nullptr)
                Component->Release();
        }
        Payload = 0;
    }
    const unsigned long NewType = kMaterialParamAddressMode;
    std::memcpy(TypeAddress, &NewType, sizeof(NewType));
    Payload = 0;
}

void InitializeBtDetailPropertySchema()
{
    if (!GbBtDetailPropertySchemaInitialized)
    {
        GbBtDetailPropertySchemaInitialized = true;
        ConstructPropertyRecords(GBtDetailPropertySchema, 13);
        std::atexit(DestroyBtDetailPropertySchema);
    }
    if (GBtDetailPropertySchema[0].Current.Type != 0)
        return;

    static const unsigned long Payload_0_0[] = {0x3F800000, 0x3F800000, 0x3F800000};
    static const unsigned long Payload_0_1[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_0_2[] = {0x3F800000, 0x3F800000, 0x3F800000};
    SetSinglePassPropertyRecord(GBtDetailPropertySchema[kBtDetailAmbient], 0x0, "Ac", "Ambient Color",
                                kMaterialParamVector3, Payload_0_0,
                                kMaterialParamVector3, Payload_0_1,
                                kMaterialParamVector3, Payload_0_2);
    static const unsigned long Payload_1_0[] = {0x3F800000, 0x3F800000, 0x3F800000};
    static const unsigned long Payload_1_1[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_1_2[] = {0x3F800000, 0x3F800000, 0x3F800000};
    SetSinglePassPropertyRecord(GBtDetailPropertySchema[kBtDetailDiffuse], 0x0, "Dc", "Diffuse Color",
                                kMaterialParamVector3, Payload_1_0,
                                kMaterialParamVector3, Payload_1_1,
                                kMaterialParamVector3, Payload_1_2);
    static const unsigned long Payload_2_0[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_2_1[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_2_2[] = {0x3F800000, 0x3F800000, 0x3F800000};
    SetSinglePassPropertyRecord(GBtDetailPropertySchema[kBtDetailEmissive], 0x0, "Ec", "Emissive Color",
                                kMaterialParamVector3, Payload_2_0,
                                kMaterialParamVector3, Payload_2_1,
                                kMaterialParamVector3, Payload_2_2);
    static const unsigned long Payload_3_0[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_3_1[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_3_2[] = {0x3F800000, 0x3F800000, 0x3F800000};
    SetSinglePassPropertyRecord(GBtDetailPropertySchema[kBtDetailSpecular], 0x0, "Sc", "Specular Color",
                                kMaterialParamVector3, Payload_3_0,
                                kMaterialParamVector3, Payload_3_1,
                                kMaterialParamVector3, Payload_3_2);
    static const unsigned long Payload_4_0[] = {0x00000000};
    static const unsigned long Payload_4_1[] = {0x00000000};
    static const unsigned long Payload_4_2[] = {0x3F800000};
    SetSinglePassPropertyRecord(GBtDetailPropertySchema[kBtDetailSpecularPower], 0x0, "Sp", "Specular Power",
                                kMaterialParamFloat, Payload_4_0,
                                kMaterialParamFloat, Payload_4_1,
                                kMaterialParamFloat, Payload_4_2);
    static const unsigned long Payload_5_0[] = {0x3F800000};
    static const unsigned long Payload_5_1[] = {0x00000000};
    static const unsigned long Payload_5_2[] = {0x3F800000};
    SetSinglePassPropertyRecord(GBtDetailPropertySchema[kBtDetailOpacity], 0x0, "Oc", "Opacity ",
                                kMaterialParamFloat, Payload_5_0,
                                kMaterialParamFloat, Payload_5_1,
                                kMaterialParamFloat, Payload_5_2);
    SetSinglePassPropertyRecord(GBtDetailPropertySchema[kBtDetailTextureName], 0x0, "Dt_name", "Texture Name",
                                kMaterialParamTextureName, "(None)",
                                kMaterialParamTextureName, nullptr,
                                kMaterialParamTextureName, nullptr);
    SetSinglePassPropertyRecord(GBtDetailPropertySchema[kBtDetailTextureWrap], 0x0, "Dt_flags", "Texture Wrap Mode",
                                kMaterialParamWrapMode, (void*)4,
                                kMaterialParamWrapMode, nullptr,
                                kMaterialParamWrapMode, nullptr);
    SetSinglePassPropertyRecord(GBtDetailPropertySchema[kBtDetailTextureAddressU], kMaterialParamFlagAddressU, "Dt_flags", "Texture U Address Mode",
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr);
    SetSinglePassPropertyRecord(GBtDetailPropertySchema[kBtDetailTextureAddressV], kMaterialParamFlagAddressV, "Dt_flags", "Texture V Address Mode",
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr);
    SetSinglePassPropertyRecord(GBtDetailPropertySchema[kBtDetailDetailTextureName], 0x0, "Bt_name", "DetailTexture Name",
                                kMaterialParamTextureName, "(None)",
                                kMaterialParamTextureName, nullptr,
                                kMaterialParamTextureName, nullptr);
    SetSinglePassPropertyRecord(GBtDetailPropertySchema[kBtDetailDetailTextureWrap], 0x0, "Bt_flags", "DetailTexture Wrap Mode",
                                kMaterialParamWrapMode, (void*)4,
                                kMaterialParamWrapMode, nullptr,
                                kMaterialParamWrapMode, nullptr);
    SetSinglePassPropertyRecord(GBtDetailPropertySchema[kBtDetailDetailTextureAddressU], kMaterialParamFlagAddressU, "Bt_flags", "DetailTexture U Address Mode",
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr);

    // 06eb4d94/4d99: flags alias the Atmosphere construction guard; the
    // following words overlap record zero and are published in setter order.
    GbAtmospherePropertySchemaInitialized = false;
    FShadingPropertyRecord& Atmosphere = GAtmospherePropertySchema[0];
    Atmosphere.Flags = (unsigned long)("Bt_flags");
    Atmosphere.Name = "DetailTexture V Address Mode";
    Atmosphere.Description = GPropertyMetadata_06ed56c4;
    AssignOverlappingZeroAddressMode(&Atmosphere.Callback, Atmosphere.Current.Type);
    AssignOverlappingZeroAddressMode(&Atmosphere.Current.Payload3, Atmosphere.Minimum.Type);
    AssignOverlappingZeroAddressMode(&Atmosphere.Minimum.Payload3, Atmosphere.Maximum.Type);
}

// LAB_06ebc3f0 / LAB_06eba540.
void InitializeTexturePropertyRecords(FShadingPropertyRecord* Records)
{
    SetSinglePassPropertyRecord(Records[kNebulaTextureName], 0, "Dt_name", "Texture Name",
                                kMaterialParamTextureName, "(None)",
                                kMaterialParamTextureName, nullptr,
                                kMaterialParamTextureName, nullptr);
    SetSinglePassPropertyRecord(Records[kNebulaTextureWrap], 0, "Dt_flags", "Texture Wrap Mode",
                                kMaterialParamWrapMode, (const void*)4UL,
                                kMaterialParamWrapMode, nullptr,
                                kMaterialParamWrapMode, nullptr);
    SetSinglePassPropertyRecord(Records[kNebulaTextureAddressU], kMaterialParamFlagAddressU, "Dt_flags", "Texture U Address Mode",
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr);
    SetSinglePassPropertyRecord(Records[kNebulaTextureAddressV], kMaterialParamFlagAddressV, "Dt_flags", "Texture V Address Mode",
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr);
}

void DestroyNebulaPropertySchema()
{
    // FUN_06ebc720: four records in reverse Saved/Default/Current order.
    DestroyPropertyRecords(GNebulaPropertySchema, 4);
}

void InitializeNebulaPropertySchema()
{
    if (!GbNebulaPropertySchemaInitialized)
    {
        GbNebulaPropertySchemaInitialized = true;
        ConstructPropertyRecords(GNebulaPropertySchema, 4);
        std::atexit(DestroyNebulaPropertySchema);
    }
    if (GNebulaPropertySchema[0].Current.Type != 0)
        return;
    InitializeTexturePropertyRecords(GNebulaPropertySchema);
}

void DestroyLightMapPropertySchema()
{
    // FUN_06ebaab0: separate four-record table and registration.
    DestroyPropertyRecords(GLightMapPropertySchema, 4);
}

void InitializeLightMapPropertySchema()
{
    if (!GbLightMapPropertySchemaInitialized)
    {
        GbLightMapPropertySchemaInitialized = true;
        ConstructPropertyRecords(GLightMapPropertySchema, 4);
        std::atexit(DestroyLightMapPropertySchema);
    }
    if (GLightMapPropertySchema[0].Current.Type != 0)
        return;
    InitializeTexturePropertyRecords(GLightMapPropertySchema);
}

void InitializeEcEtPropertySchema()
{
    if (!GbEcEtPropertySchemaInitialized)
    {
        GbEcEtPropertySchemaInitialized = true;
        ConstructPropertyRecords(GEcEtPropertySchema, 5);
        std::atexit(DestroyEcEtPropertySchema);
    }
    if (GEcEtPropertySchema[0].Current.Type != 0)
        return;

    static const unsigned long Payload_0_0[] = {0x3F800000, 0x3F800000, 0x3F800000};
    static const unsigned long Payload_0_1[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_0_2[] = {0x3F800000, 0x3F800000, 0x3F800000};
    SetSinglePassPropertyRecord(GEcEtPropertySchema[kEcEtEmissive], 0x4, "Ec", "Emissive Color",
                                kMaterialParamVector3, Payload_0_0,
                                kMaterialParamVector3, Payload_0_1,
                                kMaterialParamVector3, Payload_0_2);
    SetSinglePassPropertyRecord(GEcEtPropertySchema[kEcEtEmissiveTextureName], 0x0, "Et_name", "Emissive Texture Name",
                                kMaterialParamTextureName, "(None)",
                                kMaterialParamTextureName, nullptr,
                                kMaterialParamTextureName, nullptr);
    SetSinglePassPropertyRecord(GEcEtPropertySchema[kEcEtEmissiveTextureWrap], 0x0, "Et_flags", "Emissive Texture Wrap Mode",
                                kMaterialParamWrapMode, (void*)4,
                                kMaterialParamWrapMode, nullptr,
                                kMaterialParamWrapMode, nullptr);
    SetSinglePassPropertyRecord(GEcEtPropertySchema[kEcEtEmissiveTextureAddressU], kMaterialParamFlagAddressU, "Et_flags", "Emissive Texture U Address Mode",
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr);
    SetSinglePassPropertyRecord(GEcEtPropertySchema[kEcEtEmissiveTextureAddressV], kMaterialParamFlagAddressV, "Et_flags", "Emissive Texture V Address Mode",
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr);
}

void InitializeDcDtEtPropertySchema()
{
    if (!GbDcDtEtPropertySchemaInitialized)
    {
        GbDcDtEtPropertySchemaInitialized = true;
        ConstructPropertyRecords(GDcDtEtPropertySchema, 9);
        std::atexit(DestroyDcDtEtPropertySchema);
    }
    if (GDcDtEtPropertySchema[0].Current.Type != 0)
        return;

    static const unsigned long Payload_0_0[] = {0x3F800000, 0x3F800000, 0x3F800000};
    static const unsigned long Payload_0_1[] = {0x00000000, 0x00000000, 0x00000000};
    static const unsigned long Payload_0_2[] = {0x3F800000, 0x3F800000, 0x3F800000};
    SetSinglePassPropertyRecord(GDcDtEtPropertySchema[kDcDtEtDiffuse], 0x0, "Dc", "Diffuse Color",
                                kMaterialParamVector3, Payload_0_0,
                                kMaterialParamVector3, Payload_0_1,
                                kMaterialParamVector3, Payload_0_2);
    SetSinglePassPropertyRecord(GDcDtEtPropertySchema[kDcDtEtDiffuseTextureName], 0x0, "Dt_name", "Diffuse Texture Name",
                                kMaterialParamTextureName, "(None)",
                                kMaterialParamTextureName, nullptr,
                                kMaterialParamTextureName, nullptr);
    SetSinglePassPropertyRecord(GDcDtEtPropertySchema[kDcDtEtDiffuseTextureWrap], 0x0, "Dt_flags", "Diffuse Texture Wrap Mode",
                                kMaterialParamWrapMode, (void*)4,
                                kMaterialParamWrapMode, nullptr,
                                kMaterialParamWrapMode, nullptr);
    SetSinglePassPropertyRecord(GDcDtEtPropertySchema[kDcDtEtDiffuseTextureAddressU], kMaterialParamFlagAddressU, "Dt_flags", "Diffuse Texture U Address Mode",
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr);
    SetSinglePassPropertyRecord(GDcDtEtPropertySchema[kDcDtEtDiffuseTextureAddressV], kMaterialParamFlagAddressV, "Dt_flags", "Diffuse Texture V Address Mode",
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr);
    SetSinglePassPropertyRecord(GDcDtEtPropertySchema[kDcDtEtEmissiveTextureName], 0x0, "Et_name", "Emissive Texture Name",
                                kMaterialParamTextureName, "(None)",
                                kMaterialParamTextureName, nullptr,
                                kMaterialParamTextureName, nullptr);
    SetSinglePassPropertyRecord(GDcDtEtPropertySchema[kDcDtEtEmissiveTextureWrap], 0x0, "Et_flags", "Emissive Texture Wrap Mode",
                                kMaterialParamWrapMode, (void*)4,
                                kMaterialParamWrapMode, nullptr,
                                kMaterialParamWrapMode, nullptr);
    SetSinglePassPropertyRecord(GDcDtEtPropertySchema[kDcDtEtEmissiveTextureAddressU], kMaterialParamFlagAddressU, "Et_flags", "Emissive Texture U Address Mode",
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr);
    SetSinglePassPropertyRecord(GDcDtEtPropertySchema[kDcDtEtEmissiveTextureAddressV], kMaterialParamFlagAddressV, "Et_flags", "Emissive Texture V Address Mode",
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr,
                                kMaterialParamAddressMode, nullptr);
}
} // namespace

FShadingPropertyRecord* GetBtDetailPropertySchema()
{
    InitializeBtDetailPropertySchema();
    return GBtDetailPropertySchema;
}

FShadingPropertyRecord* GetDetailMapPropertySchema()
{
    InitializeDetailMapPropertySchema();
    return GDetailMapPropertySchema;
}

FShadingPropertyRecord* GetAtmospherePropertySchema()
{
    InitializeAtmospherePropertySchema();
    return GAtmospherePropertySchema;
}

FShadingPropertyRecord* GetSinglePassPropertySchema()
{
    InitializeSinglePassPropertySchema();
    return GSinglePassPropertySchema;
}

FShadingPropertyRecord* GetIllumDetailPropertySchema()
{
    InitializeIllumDetailPropertySchema();
    return GIllumDetailPropertySchema;
}

FShadingPropertyRecord* GetEcEtPropertySchema()
{
    InitializeEcEtPropertySchema();
    return GEcEtPropertySchema;
}

FShadingPropertyRecord* GetDcDtEtPropertySchema()
{
    InitializeDcDtEtPropertySchema();
    return GDcDtEtPropertySchema;
}

FMaterialInner::FMaterialInner(FMaterial* InOwner) : RefCount(1), Owner(InOwner) {}

GENRESULT DACOM_API FMaterialInner::QueryInterface(const char* Iid, void** Out)
{
    // LAB_06ebe3a0; raw PE table 06ed28f0 = {IMaterial, 0, nullptr, 0}.
    if (std::strcmp(Iid, Iids::Material) == 0)
    {
        Owner->AddRef();
        *Out = (IMaterial*)Owner;
        return GR_OK;
    }
    *Out = nullptr;
    return GR_INTERFACE_UNSUPPORTED;
}

unsigned long DACOM_API FMaterialInner::AddRef()
{
    return ++RefCount;
}

unsigned long DACOM_API FMaterialInner::Release()
{
    // LAB_06eb65f0: the inner count never delegates to the aggregate outer.
    if (RefCount != 0)
        --RefCount;
    const unsigned long Remaining = RefCount;
    if (Remaining == 0)
    {
        RefCount = 1;
        Owner->ScalarDeletingDestructor(1);
    }
    return Remaining;
}

FMaterial::FMaterial()
    : Pipeline(nullptr),
      TexLib(nullptr),
      VBMgr(nullptr),
      RPDraw(nullptr),
      InstanceName(nullptr),
      TexBlocks(nullptr),
      TexCount(0),
      DiffuseR(1.0f),
      DiffuseG(1.0f),
      DiffuseB(1.0f),
      Opacity(1.0f),
      SpecularPower(0.0f),
      bAlphablend(0),
      bAlphatest(0)
{
    Ambient[0] = 1.0f;
    Ambient[1] = 1.0f;
    Ambient[2] = 1.0f;
    Ambient[3] = 1.0f;
    Specular[0] = 0.0f;
    Specular[1] = 0.0f;
    Specular[2] = 0.0f;
    Specular[3] = 0.0f;
    Emissive[0] = 0.0f;
    Emissive[1] = 0.0f;
    Emissive[2] = 0.0f;
    Emissive[3] = 0.0f;
    Pad66[0] = 0;
    Pad66[1] = 0;
}

FSinglePassMaterial::FSinglePassMaterial() : MatAnim(nullptr) {}

FSinglePassMaterial::~FSinglePassMaterial() noexcept(false)
{
    // FUN_06ebe490: the held animation is released before any texture group.
    if (MatAnim != nullptr)
    {
        MatAnim->Release();
        MatAnim = nullptr;
    }
}

FDcDtEtMaterial::FDcDtEtMaterial() : bSecondaryTextureEnabled(true) {}

FAtmosphereMaterial::FAtmosphereMaterial() : FadeOff(1.0f), Alpha(0.75f), bAtmosphereReady(0)
{
    Pad75[0] = 0;
    Pad75[1] = 0;
    Pad75[2] = 0;
    const unsigned long ScaleBits = 0x3F833333;
    std::memcpy(&Scale, &ScaleBits, sizeof(Scale));
}

FBtDetailMapMaterial::FBtDetailMapMaterial()
    : bSecondaryTextureEnabled(true), bSecondaryTextureVerified(true)
{
}

FDetailMap2PassMaterial::FDetailMap2PassMaterial()
    : DetailFlipU(0),
      DetailFlipV(0),
      DetailTileRate0(4.0f),
      DetailPassCount(2),
      bDetailMultiPassEnabled(true),
      bDetailSecondStageVerified(true)
{
}

FDetailMapMaterial::FDetailMapMaterial()
    : DetailFlipU(0),
      DetailFlipV(0),
      DetailTileRate0(4.0f),
      bDetailMultiPassEnabled(true),
      bDetailSecondStageVerified(true)
{
}

FTwoDetailMapMaterial::FTwoDetailMapMaterial()
    : DetailFlipU(0),
      DetailFlipV(0),
      DetailTileRate0(4.0f),
      DetailTileRate1(4.0f),
      DetailPassCount(2),
      bDetailMultiPassEnabled(true),
      bDetailSecondStageVerified(true)
{
}

FMaterial::~FMaterial() noexcept(false)
{
    // FUN_06ebc150: forward group release uses the live count.
    for (unsigned long Index = 0; Index < TexCount; ++Index)
    {
        if (TexBlocks[Index].TextureId != -1)
        {
            TexLib->DecRefById(TexBlocks[Index].TextureId);
            TexBlocks[Index].TextureId = -1;
        }
    }
    // The vector delete destroys the names in reverse over the allocation's count, not TexCount.
    delete[] TexBlocks;
    TexBlocks = nullptr;
    TexLib = nullptr;
    Pipeline = nullptr;
    VBMgr = nullptr;
    delete[] InstanceName;
    InstanceName = nullptr;
}

static I3DMathEngine* G_MathEngine = nullptr;

void FMaterial::SetInstanceName(const char* Name)
{
    SetName(Name);
}

static const float kAtmosphereOne = 1.0f;
static const float kAtmosphereHalf = 0.5f;
static const unsigned long kAtmosphereEpsBits = 0x33D6BF95;

static_assert(offsetof(FMaterial, Pipeline) == 0x04, "Pipeline +4");
static_assert(offsetof(FMaterial, TexLib) == 0x08, "TexLib +8");
static_assert(offsetof(FMaterial, VBMgr) == 0x0C, "VBMgr +C");
static_assert(offsetof(FMaterial, RPDraw) == 0x10, "IRPDraw +10");
static_assert(offsetof(FMaterial, InstanceName) == 0x14, "instance name +14");
static_assert(offsetof(FMaterial, TexBlocks) == 0x18, "texblock +18");
static_assert(offsetof(FMaterial, TexCount) == 0x1C, "tex count +1C");
static_assert(offsetof(FMaterial, DiffuseR) == 0x20, "Diffuse +20");
static_assert(offsetof(FMaterial, Opacity) == 0x2C, "Opacity +2C");
static_assert(offsetof(FMaterial, Ambient) == 0x30, "Ambient +30");
static_assert(offsetof(FMaterial, Specular) == 0x40, "Specular +40");
static_assert(offsetof(FMaterial, Emissive) == 0x50, "Emissive +50");
static_assert(offsetof(FMaterial, SpecularPower) == 0x60, "SpecularPower +60");
static_assert(offsetof(FMaterial, bAlphablend) == 0x64, "Oc flag +64");
static_assert(offsetof(FMaterial, bAlphatest) == 0x65, "alphatest +65");
static_assert(offsetof(FSinglePassMaterial, MatAnim) == 0x68, "MatAnim +68");
static_assert(offsetof(TMaterialObject<FSinglePassMaterial>, Inner) == 0x70, "SinglePass inner controller +70");
static_assert(offsetof(FAtmosphereMaterial, bAtmosphereReady) == 0x74, "Atmosphere +74");
static_assert(offsetof(FBtDetailMapMaterial, bSecondaryTextureVerified) == 0x69, "BtDetail +69");
static_assert(offsetof(FDetailMap2PassMaterial, bDetailSecondStageVerified) == 0x79, "DetailMap2Dm1Msk2Pass +79");
static_assert(offsetof(FDetailMapMaterial, bDetailSecondStageVerified) == 0x75, "DetailMap +75");
static_assert(offsetof(FTwoDetailMapMaterial, bDetailSecondStageVerified) == 0x7D, "Illum / Masked2 +7D");

GENRESULT DACOM_API FMaterial::Initialize(IDAComponent* Component)
{
    // FUN_06eb1200: these are borrowed component pointers after immediate Release.
    if (Component == nullptr)
        return GR_GENERIC;
    if (Component->QueryInterface(Iids::TextureLibraryA, (void**)&TexLib) < 0)
        return GR_GENERIC;
    TexLib->Release();
    if (Component->QueryInterface(Iids::RenderPipeline8B, (void**)(&Pipeline)) < 0)
        return GR_GENERIC;
    Pipeline->Release();
    if (Component->QueryInterface(Iids::RPDraw, (void**)&RPDraw) < 0)
        return GR_GENERIC;
    RPDraw->Release();
    if (Component->QueryInterface(Iids::VertexBufferManager, (void**)&VBMgr) < 0)
        return GR_GENERIC;
    VBMgr->Release();

    return InitializeOwnedSchema();
}

GENRESULT FMaterial::InitializeOwnedSchema()
{
    void* Table;
    const unsigned long Count = GetParameterTable(&Table);
    FShadingPropertyRecord* Records = (FShadingPropertyRecord*)Table;
    TexCount = 0;
    for (unsigned long Index = 0; Index < Count; ++Index)
    {
        if (Records[Index].Current.Type == kMaterialParamTextureName)
            ++TexCount;
    }
    if (TexCount != 0)
    {
        TexBlocks = new (std::nothrow) FTexBlock[TexCount];
        if (TexBlocks != nullptr)
        {
            for (unsigned long Index = 0; Index < TexCount; ++Index)
            {
                TexBlocks[Index].Name = nullptr;
                TexBlocks[Index].TextureId = -1;
                TexBlocks[Index].Handle = 0;
                TexBlocks[Index].Flags = 4;
                TexBlocks[Index].AddressU = 0;
                TexBlocks[Index].AddressV = 0;
                TexBlocks[Index].bSkipResolve = 0;
            }
        }
    }
    for (unsigned long Index = 0; Index < Count; ++Index)
        StoreParameterValue(Index, &Records[Index], &Records[Index].Current);
    return GR_OK;
}

namespace
{
    // FUN_06eb3450 / FUN_06eb3510: scalar output is committed only on full success.
    GENRESULT ReadSinglePassWord(IFileSystem* FileSystem, const char* Name, unsigned long* Output,
                                 bool bFloat = false)
    {
        FFileSystemDesc Desc;
        InitializeShadingFileDesc(&Desc, Name);
        const int Handle = FileSystem->OpenByDesc((FFileSystemDesc*)(&Desc));
        if (Handle == -1)
            return GR_GENERIC;
        unsigned long ReadValue;
        unsigned long BytesRead;
        const int ReadResult = FileSystem->Read(Handle, &ReadValue, sizeof(ReadValue), &BytesRead, 0);
        const bool bSuccess = ReadResult != 0 && BytesRead == sizeof(ReadValue);
        if (bSuccess)
        {
            if (bFloat)
            {
                __asm
                {
                    fld dword ptr ReadValue
                    mov eax, Output
                    fstp dword ptr [eax]
                }
            }
            else
                *Output = ReadValue;
        }
        FileSystem->CloseFile(Handle);
        return bSuccess ? GR_OK : GR_GENERIC;
    }
}

namespace
{
    // FUN_06eb35d0: vector reads write directly; optional count is in float elements.
    GENRESULT ReadSinglePassVector(IFileSystem* FileSystem, const char* Name,
                                   unsigned long Count, void* Out, unsigned long* OutCount)
    {
        FFileSystemDesc Desc;
        InitializeShadingFileDesc(&Desc, Name);
        const int Handle = FileSystem->OpenByDesc((FFileSystemDesc*)(&Desc));
        if (Handle == -1)
            return GR_GENERIC;
        if (OutCount != nullptr)
        {
            const unsigned long Available = FileSystem->GetSize(Handle, 0) >> 2;
            if (Available < Count)
                Count = Available;
            *OutCount = Count;
        }
        const unsigned long Size = Count * 4;
        unsigned long BytesRead;
        const int ReadResult = FileSystem->Read(Handle, Out, Size, &BytesRead, 0);
        const bool bSuccess = ReadResult != 0 && BytesRead == Size;
        FileSystem->CloseFile(Handle);
        return bSuccess ? GR_OK : GR_GENERIC;
    }
}

namespace
{
    // FUN_06eb36b0 publishes allocation before Read, including on a failed read.
    GENRESULT ReadSinglePassString(IFileSystem* FileSystem, const char* Name,
                                   unsigned long Size, char** Out)
    {
        FFileSystemDesc Desc;
        InitializeShadingFileDesc(&Desc, Name);
        const int Handle = FileSystem->OpenByDesc((FFileSystemDesc*)(&Desc));
        if (Handle == -1)
            return GR_GENERIC;
        if (Size == 0)
            Size = FileSystem->GetSize(Handle, 0);
        char* Storage = new (std::nothrow) char[Size];
        bool bSuccess = false;
        if (Storage != nullptr)
        {
            *Out = Storage;
            unsigned long BytesRead;
            const int ReadResult = FileSystem->Read(Handle, Storage, Size, &BytesRead, 0);
            bSuccess = ReadResult != 0 && BytesRead == Size;
        }
        FileSystem->CloseFile(Handle);
        return bSuccess ? GR_OK : GR_GENERIC;
    }
}

GENRESULT FMaterial::LoadDeclaredSchema(IFileSystem* FileSystem)
{
    // LAB_06eb1370: walk the actual schema in order; no aliases or early texture resolve.
    void* Table;
    const unsigned long Count = GetParameterTable(&Table);
    FShadingPropertyRecord* Records = (FShadingPropertyRecord*)Table;
    for (unsigned long Index = 0; Index < Count; ++Index)
    {
        FShadingPropertyRecord& Record = Records[Index];
        if ((Record.Flags & kMaterialParamFlagNotSaved) != 0)
            continue;
        FShadingPropertyValue Value = {};
        Value.Type = Record.Current.Type;
        GENRESULT Result = GR_OK;
        switch (Value.Type)
        {
        case kMaterialParamInteger:
            Result = ReadSinglePassWord(FileSystem, Record.Name, &Value.Payload0);
            break;
        case kMaterialParamFloat:
            Result = ReadSinglePassWord(FileSystem, Record.Name, &Value.Payload0, true);
            break;
        case kMaterialParamVector3:
        case kMaterialParamVector4:
            Result = ReadSinglePassVector(FileSystem, Record.Name, Value.Type == kMaterialParamVector3 ? 3 : 4,
                                          &Value.Payload0, nullptr);
            break;
        case kMaterialParamString:
        case kMaterialParamTextureName:
        {
            char* RawString;
            Result = ReadSinglePassString(FileSystem, Record.Name, 0, &RawString);
            if (Result >= 0)
            {
                Value.Assign(Value.Type, RawString);
                delete[] RawString;
            }
            // Retail's failed read does not transfer RawString into Value.
            break;
        }
        case kMaterialParamWrapMode:
        case kMaterialParamAddressMode:
        {
            unsigned long Packed;
            Result = ReadSinglePassWord(FileSystem, Record.Name, &Packed);
            if (Result >= 0)
            {
                if (Value.Type == kMaterialParamWrapMode)
                    Value.Payload0 = (Packed >> 4) & 0xF;
                else
                {
                    const bool bV = (Record.Flags & kMaterialParamFlagAddressU) == 0 &&
                                    (Record.Flags & kMaterialParamFlagAddressV) != 0;
                    Value.Payload0 = (Packed >> (bV ? 2 : 0)) & 3;
                }
            }
            break;
        }
        default:
            // Retail type6 and unknown tags fall through to the property store.
            break;
        }
        if (Result >= 0)
            StoreParameterValue(Index, &Record, &Value);
        Value.ReleaseOwnedPayload();
    }
    return GR_OK;
}

GENRESULT DACOM_API FMaterial::LoadFromFileSystem(void* FileSystem)
{
    // LAB_06eb1370: every family walks its actual table, including Null's zero count.
    return LoadDeclaredSchema((IFileSystem*)FileSystem);
}

namespace
{
    // FUN_06eb3780 (word), 3840 (float elements), 3900 (bytes).
    GENRESULT WriteMaterialPropertyBytes(IFileSystem* FileSystem, const char* Name,
                                         const void* Buffer, unsigned long Size)
    {
        FFileSystemDesc Desc;
        InitializeShadingFileDesc(&Desc, Name);
        Desc.Implementation = "UTF";
        Desc.Access = GENERIC_READ | GENERIC_WRITE;
        Desc.Creation = CREATE_ALWAYS;
        const int Handle = FileSystem->OpenByDesc(&Desc);
        if (Handle == -1)
            return (GENRESULT)0x80004005UL;
        unsigned long BytesWritten;
        const int WriteResult = FileSystem->Write(
            (unsigned long)Handle, Buffer, Size, &BytesWritten, nullptr);
        const bool bSuccess = WriteResult != 0 && BytesWritten == Size;
        FileSystem->CloseFile(Handle);
        return bSuccess ? GR_OK : (GENRESULT)0x80004005UL;
    }
}

GENRESULT DACOM_API FMaterial::SaveProperties(void* FileSystem)
{
    // FUN_06eb1720: table-driven snapshot, then write, then typed cleanup.
    void* Table;
    const unsigned long Count = GetParameterTable(&Table);
    FShadingPropertyRecord* Records = (FShadingPropertyRecord*)Table;
    IFileSystem* Files = (IFileSystem*)FileSystem;
    GENRESULT Result = GR_OK;
    for (unsigned long Index = 0; Index < Count; ++Index)
    {
        FShadingPropertyRecord& Record = Records[Index];
        if ((Record.Flags & kMaterialParamFlagNotSaved) != 0)
            continue;
        FShadingPropertyValue Value = {};
        Value.Type = Record.Current.Type;
        const unsigned long* Target = (const unsigned long*)(GetParameterAddress(Index));
        if (Target == nullptr)
        {
            Value.ReleaseOwnedPayload();
            Result = GR_GENERIC;
            continue;
        }
        switch (Value.Type)
        {
        case kMaterialParamVector4:
            Value.Payload3 = Target[3];
            [[fallthrough]];
        case kMaterialParamVector3:
            Value.Payload2 = Target[2];
            Value.Payload1 = Target[1];
            [[fallthrough]];
        default:
            Value.Payload0 = Target[0];
            break;
        case kMaterialParamString:
        case kMaterialParamTextureName:
        case kMaterialParamComponent:
            Value.Assign(Value.Type, (const void*)(Target[0]));
            break;
        }
        GENRESULT WriteResult = GR_OK;
        switch (Value.Type)
        {
        case kMaterialParamInteger:
        case kMaterialParamFloat:
            WriteResult = WriteMaterialPropertyBytes(Files, Record.Name, &Value.Payload0, 4);
            break;
        case kMaterialParamVector3:
        case kMaterialParamVector4:
            WriteResult = WriteMaterialPropertyBytes(Files, Record.Name, &Value.Payload0,
                                                     Value.Type == kMaterialParamVector3 ? 12 : 16);
            break;
        case kMaterialParamString:
        case kMaterialParamTextureName:
        {
            const char* Text = (const char*)(Value.Payload0);
            WriteResult = WriteMaterialPropertyBytes(Files, Record.Name, Text,
                (unsigned long)(std::strlen(Text) + 1));
            break;
        }
        case kMaterialParamWrapMode:
        case kMaterialParamAddressMode:
        {
            unsigned long Packed = 0;
            ReadSinglePassWord(Files, Record.Name, &Packed); // Read failure is ignored.
            if (Value.Type == kMaterialParamWrapMode)
                Packed = (Packed & ~0xF0UL) | ((Value.Payload0 << 4) & 0xF0UL);
            else
            {
                const bool bV = (Record.Flags & kMaterialParamFlagAddressU) == 0 &&
                                (Record.Flags & kMaterialParamFlagAddressV) != 0;
                if (!bV)
                    Packed = (Packed & ~3UL) | (Value.Payload0 & 3UL);
                // Retail quirk (06eb196c): U also writes the V bits.
                Packed = (Packed & ~0xCUL) | ((Value.Payload0 << 2) & 0xCUL);
            }
            WriteResult = WriteMaterialPropertyBytes(Files, Record.Name, &Packed, 4);
            break;
        }
        default:
            // Interface/tag0/10/future values are snapshotted and cleaned but not written.
            break;
        }
        if (WriteResult < 0)
            Result = GR_GENERIC;
        Value.ReleaseOwnedPayload();
    }
    return Result;
}

GENRESULT DACOM_API FSinglePassMaterial::ApplyVerify(void* /*Context*/, void* /*Extra*/)
{
    // LAB_06ebd810..d8ac: retain the QI output across the whole verification.
    IMatAnimLibrary* AnimationLibrary = nullptr;
    const GENRESULT QueryResult = Pipeline->QueryInterface(Iids::MatAnimLibrary, (void**)&AnimationLibrary);
    if (QueryResult >= 0)
    {
        unsigned long Query[2];
        Query[0] = (unsigned long)InstanceName;
        Query[1] = DACOM_CRC::GetCRC32(InstanceName);
        if (MatAnim != nullptr)
        {
            MatAnim->Release();
            MatAnim = nullptr;
        }
        AnimationLibrary->CreateAnimationInstance(-1, Query, &MatAnim);
    }
    Setup();
    Bind(nullptr);
    const GENRESULT Result = Pipeline->VerifyState();
    Teardown(nullptr);
    if (AnimationLibrary != nullptr)
        AnimationLibrary->Release();
    return Result;
}


GENRESULT DACOM_API FDetailMap2PassMaterial::ApplyVerify(void* /*Context*/, void* /*Extra*/)
{
    // LAB_06eb6ef0: two verifies, then single-pass fallback.
    DetailPassCount = 2;
    bDetailMultiPassEnabled = GetMaterialGlobalRecordValue(0) >= 1;
    Setup();
    bDetailSecondStageVerified = true;
    ApplyDetailPassState(0);
    const bool bFirstVerified = Pipeline->VerifyState() >= 0;
    ApplyDetailPassState(1);
    GENRESULT Result = Pipeline->VerifyState();
    const bool bSecondVerified = Result >= 0;
    bDetailSecondStageVerified = bFirstVerified && bSecondVerified;
    if (!bFirstVerified || !bSecondVerified || !bDetailMultiPassEnabled)
    {
        if (TexBlocks[1].TextureId != -1)
        {
            TexLib->DecRefById(TexBlocks[1].TextureId);
            TexBlocks[1].TextureId = -1;
        }
        bDetailSecondStageVerified = false;
        bDetailMultiPassEnabled = false;
        DetailPassCount = 1;
        ApplyDetailPassState(0);
        Result = Pipeline->VerifyState();
    }
    Teardown(nullptr);
    return Result;
}

GENRESULT DACOM_API FMasked2DetailMapMaterial::ApplyVerify(void* /*Context*/, void* /*Extra*/)
{
    // LAB_06ebb680: two verifies, then single-pass fallback.
    DetailPassCount = 2;
    bDetailMultiPassEnabled = GetMaterialGlobalRecordValue(0) >= 1;
    Setup();
    bDetailSecondStageVerified = true;
    ApplyMaskedDetailPassState(0);
    const bool bFirstVerified = Pipeline->VerifyState() >= 0;
    ApplyMaskedDetailPassState(1);
    GENRESULT Result = Pipeline->VerifyState();
    const bool bSecondVerified = Result >= 0;
    bDetailSecondStageVerified = bFirstVerified && bSecondVerified;
    if (!bFirstVerified || !bSecondVerified || !bDetailMultiPassEnabled)
    {
        if (TexBlocks[1].TextureId != -1)
        {
            TexLib->DecRefById(TexBlocks[1].TextureId);
            TexBlocks[1].TextureId = -1;
        }
        bDetailSecondStageVerified = false;
        bDetailMultiPassEnabled = false;
        DetailPassCount = 1;
        ApplyMaskedDetailPassState(0);
        Result = Pipeline->VerifyState();
    }
    Teardown(nullptr);
    return Result;
}

GENRESULT DACOM_API FIllumDetailMapMaterial::ApplyVerify(void* /*Context*/, void* /*Extra*/)
{
    // LAB_06eb9920 returns zero on accepted pass configurations, not Verify's value.
    DetailPassCount = 2;
    bDetailMultiPassEnabled = GetMaterialGlobalRecordValue(0) >= 1;
    Setup();
    bool bFirstVerified = false;
    bool bSecondVerified = false;
    bDetailSecondStageVerified = true;
    if (bDetailMultiPassEnabled)
    {
        ApplyIllumDetailPassState(0);
        bFirstVerified = Pipeline->VerifyState() >= 0;
        ApplyIllumDetailPassState(1);
        bSecondVerified = Pipeline->VerifyState() >= 0;
    }
    bDetailSecondStageVerified = bFirstVerified && bSecondVerified;
    GENRESULT Result = GR_OK;
    if (!bFirstVerified || !bSecondVerified || !bDetailMultiPassEnabled)
    {
        DetailPassCount = 1;
        bDetailSecondStageVerified = bFirstVerified;
        if (!bFirstVerified || !bDetailMultiPassEnabled)
        {
            if (TexBlocks[1].TextureId != -1)
            {
                TexLib->DecRefById(TexBlocks[1].TextureId);
                TexBlocks[1].TextureId = -1;
            }
            DetailPassCount = 1;
            bDetailSecondStageVerified = false;
            bDetailMultiPassEnabled = false;
            ApplyIllumDetailPassState(0);
            Result = Pipeline->VerifyState();
        }
    }
    Teardown(nullptr);
    return Result;
}

GENRESULT DACOM_API FNullMaterial::ApplyVerify(void* /*Context*/, void* /*Extra*/)
{
    return GR_OK;
}

GENRESULT DACOM_API FBtDetailMapMaterial::ApplyVerify(void* /*Context*/, void* /*Extra*/)
{
    // LAB_06eb52b0: global base+4 is record 0.Value.
    bSecondaryTextureEnabled = GetMaterialGlobalRecordValue(0) >= 1;
    Setup();
    bSecondaryTextureVerified = true;
    Bind(nullptr);
    GENRESULT Result = Pipeline->VerifyState();
    bSecondaryTextureVerified = Result >= 0;
    if (!bSecondaryTextureVerified || !bSecondaryTextureEnabled)
    {
        FTexBlock& Secondary = TexBlocks[1];
        if (Secondary.TextureId != -1)
        {
            TexLib->DecRefById(Secondary.TextureId);
            Secondary.TextureId = -1;
        }
        // Retail rebinds before clearing both readiness flags.
        Bind(nullptr);
        bSecondaryTextureVerified = false;
        bSecondaryTextureEnabled = false;
        Result = Pipeline->VerifyState();
    }
    Teardown(nullptr);
    return Result;
}

GENRESULT DACOM_API FDcDtEtMaterial::ApplyVerify(void* /*Context*/, void* /*Extra*/)
{
    // LAB_06eb63b0: global base+14 is record 2.Value.
    bSecondaryTextureEnabled = GetMaterialGlobalRecordValue(2) >= 1;
    Setup();
    Bind(nullptr);
    GENRESULT Result = Pipeline->VerifyState();
    if (Result < 0 || !bSecondaryTextureEnabled)
    {
        FTexBlock& Secondary = TexBlocks[1];
        if (Secondary.TextureId != -1)
        {
            TexLib->DecRefById(Secondary.TextureId);
            Secondary.TextureId = -1;
        }
        if (bSecondaryTextureEnabled)
        {
            Bind(nullptr);
            Result = Pipeline->VerifyState();
        }
    }
    Teardown(nullptr);
    return Result;
}

GENRESULT DACOM_API FDetailMapMaterial::ApplyVerify(void* /*Context*/, void* /*Extra*/)
{
    // LAB_06eb80d0: two distinct flags.
    bDetailMultiPassEnabled = GetMaterialGlobalRecordValue(0) >= 1;
    Setup();
    bDetailSecondStageVerified = true;
    Bind(nullptr);
    GENRESULT Result = Pipeline->VerifyState();
    bDetailSecondStageVerified = Result >= 0;
    if (!bDetailSecondStageVerified || !bDetailMultiPassEnabled)
    {
        FTexBlock& Detail = TexBlocks[1];
        if (Detail.TextureId != -1)
        {
            TexLib->DecRefById(Detail.TextureId);
            Detail.TextureId = -1;
        }
        Bind(nullptr);
        bDetailSecondStageVerified = false;
        bDetailMultiPassEnabled = false;
        Result = Pipeline->VerifyState();
    }
    Teardown(nullptr);
    return Result;
}

GENRESULT DACOM_API FAtmosphereMaterial::ApplyVerify(void* /*Context*/, void* /*Extra*/)
{
    // LAB_06eb2cb0 verifies once, preserving the resolved readiness byte.
    Setup();
    const unsigned char SavedReady = bAtmosphereReady;
    bAtmosphereReady = 1;
    Bind(nullptr);
    bAtmosphereReady = SavedReady;
    const GENRESULT Result = Pipeline->VerifyState();
    if (Result != GR_OK)
    {
        char Message[0x2000];
        std::sprintf(Message, "failed to verify %s", InstanceName);
        FDUMP(kFdumpNotice, "%s(%d) : NOTICE:General:%s",
            "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Shading\\Materials\\AtmosphereMaterial.cpp",
            0x97, Message);
    }
    Teardown(nullptr);
    return Result;
}

GENRESULT DACOM_API FMaterial::ApplyVerify(void* /*Context*/, void* /*Extra*/)
{
    // LAB_06eb3aa0: the strict shared lifecycle.
    Setup();
    Bind(nullptr);
    const GENRESULT Result = Pipeline->VerifyState();
    Teardown(nullptr);
    return Result;
}

GENRESULT DACOM_API FAtmosphereMaterial::ResolveTextures(float /*Delta*/)
{
    // LAB_06eb2c40: unsigned count, mandatory block/library on lookup.
    for (unsigned long Index = 0; Index < TexCount; ++Index)
    {
        FTexBlock& Entry = TexBlocks[Index];
        if (Entry.TextureId == -1)
            Entry.Handle = 0;
        else if (Entry.bSkipResolve == 0)
            TexLib->GetFrame(Entry.TextureId, -1, &Entry.Handle);
    }
    // +0xC from the record base is record 1.Value, not Active.
    bAtmosphereReady = GetMaterialGlobalRecordValue(1) >= 1 ? 1 : 0;
    return GR_OK;
}

GENRESULT DACOM_API FMaterial::ResolveTextures(float /*Delta*/)
{
    // LAB_06eb3ae0: FTexBlock stride 0x2C; GetFrame is TexLib +0x70.
    for (unsigned long Index = 0; Index < TexCount; ++Index)
    {
        FTexBlock* Entry = &TexBlocks[Index];
        if (Entry->TextureId == -1)
        {
            Entry->Handle = 0;
            continue;
        }
        if (Entry->bSkipResolve != 0)
            continue;
        TexLib->GetFrame(Entry->TextureId, -1, &Entry->Handle);
    }
    return GR_OK;
}

GENRESULT DACOM_API FDcDtEtMaterial::ResolveTextures(float Delta)
{
    // 06eb6170: the shared walk, then the secondary-texture gate from global record 2.
    FMaterial::ResolveTextures(Delta);
    bSecondaryTextureEnabled = GetMaterialGlobalRecordValue(2) >= 1;
    return GR_OK;
}

GENRESULT DACOM_API FSinglePassMaterial::Bind(void* Context)
{
    // FUN_06ebd8b0: all SinglePass variants share this body.
    BindMaterialAndContext(Pipeline, &DiffuseR, Context);
    BindDetailTexture(Pipeline, TexBlocks[0], 0);
    ApplyDetailTable(Pipeline, GBindTable_06ed26dc, true, 0);
    ApplyDetailTable(Pipeline, GBindTable_06ed2714, true, 1);
    ApplyDetailTable(Pipeline, bAlphablend != 0 ? GBindTable_06ed26ac :
                     GBindTable_06ed268c, false, 0);
    if (MatAnim != nullptr)
    {
        float Matrix[16];
        IImaInstance* Instance = MatAnim;
        if (Instance->FillUvMatrix(Matrix) != 0)
        {
            Pipeline->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
            Pipeline->SetTextureStageTransform(0, Matrix);
        }
    }
    return GR_OK;
}

GENRESULT DACOM_API FLightMapMaterial::Bind(void* Context)
{
    // FUN_06eba720: unconditional stage tables.
    BindMaterialAndContext(Pipeline, &DiffuseR, Context);
    ApplyDetailTable(Pipeline, GBindTable_06ed209c, false, 0);
    BindDetailTexture(Pipeline, TexBlocks[0], 0);
    ApplyDetailTable(Pipeline, GBindTable_06ed20ac, true, 0);
    ApplyDetailTable(Pipeline, GBindTable_06ed20e4, true, 1);
    ApplyDetailTable(Pipeline, GBindTable_06ed206c, false, 0);
    return GR_OK;
}

GENRESULT DACOM_API FNebulaMaterial::Bind(void* Context)
{
    // FUN_06ebc600: material first, then the optional mesh COLORVERTEX write.
    SetMaterialBlock(Pipeline, &DiffuseR);
    if (Context != nullptr)
    {
        const FRpVertexSrcDesc* Source = ((const FMaterialSubmitContext*)Context)->SourceVertices;
        if (Source != nullptr && Source->Diffuse.Base != nullptr)
            PipeSetRs(Pipeline, kRsColorVertex, TRUE);
    }
    FTexBlock* Block = &TexBlocks[0];
    PipeSetTexture(Pipeline, 0, Block->Handle);
    PipeSetTss(Pipeline, 0, kTssTexCoordIndex, Block->Flags < 4 ? 0 : Block->Flags - 4);
    PipeSetTss(Pipeline, 0, kTssAddressU, Block->AddressU + 1);
    PipeSetTss(Pipeline, 0, kTssAddressV, Block->AddressV + 1);
    ApplyStateTable(Pipeline, GAtmosphereNebulaTss0, true, 0);
    ApplyStateTable(Pipeline, GAtmosphereNebulaTss1, true, 1);
    ApplyStateTable(Pipeline, GNebulaRs, false, 0);
    return GR_OK;
}

GENRESULT DACOM_API FAtmosphereMaterial::Bind(void* /*Context*/)
{
    // FUN_06eb2b40: nothing is bound until the textures are resolved.
    if (bAtmosphereReady == 0)
        return GR_OK;
    SetMaterialBlock(Pipeline, &DiffuseR);
    FTexBlock* Block = &TexBlocks[0];
    PipeSetTexture(Pipeline, 0, Block->Handle);
    PipeSetTss(Pipeline, 0, kTssTexCoordIndex, Block->Flags < 4 ? 0 : Block->Flags - 4);
    PipeSetTss(Pipeline, 0, kTssAddressU, Block->AddressU + 1);
    PipeSetTss(Pipeline, 0, kTssAddressV, Block->AddressV + 1);
    ApplyStateTable(Pipeline, GAtmosphereNebulaTss0, true, 0);
    ApplyStateTable(Pipeline, GAtmosphereNebulaTss1, true, 1);
    ApplyStateTable(Pipeline, GAtmosphereRs, false, 0);
    return GR_OK;
}

GENRESULT DACOM_API FDcDtEtMaterial::Bind(void* Context)
{
    // FUN_06eb61e0: enabled flag and the SECOND block's id control stage 1.
    BindMaterialAndContext(Pipeline, &DiffuseR, Context);
    ApplyDetailTable(Pipeline, GBindTable_06ed16cc, false, 0);
    BindDetailTexture(Pipeline, TexBlocks[0], 0);
    ApplyDetailTable(Pipeline, GBindTable_06ed16ec, true, 0);
    unsigned long DisabledStage = 1;
    if (bSecondaryTextureEnabled && TexBlocks[1].TextureId != -1)
    {
        Pipeline->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 0);
        BindDetailTexture(Pipeline, TexBlocks[1], 1);
        ApplyDetailTable(Pipeline, GBindTable_06ed1724, true, 1);
        DisabledStage = 2;
    }
    ApplyDetailTable(Pipeline, GBindTable_06ed175c, true, DisabledStage);
    return GR_OK;
}

GENRESULT DACOM_API FEcEtMaterial::Bind(void* Context)
{
    // FUN_06eb8a40: mandatory material, optional marker, block0, fixed tables.
    BindMaterialAndContext(Pipeline, &DiffuseR, Context);
    BindDetailTexture(Pipeline, TexBlocks[0], 0);
    ApplyDetailTable(Pipeline, GBindTable_06ed1c84, true, 0);
    ApplyDetailTable(Pipeline, GBindTable_06ed1cbc, true, 1);
    ApplyDetailTable(Pipeline, GBindTable_06ed1c54, false, 0);
    return GR_OK;
}

GENRESULT DACOM_API FDetailMapMaterial::Bind(void* Context)
{
    // LAB_06eb81e0: mandatory material first, then optional mesh marker.
    SetMaterialBlock(Pipeline, &DiffuseR);
    if (Context != nullptr)
    {
        const FRpVertexSrcDesc* Source = ((const FMaterialSubmitContext*)Context)->SourceVertices;
        if (Source != nullptr && Source->Diffuse.Base != nullptr)
            Pipeline->SetRenderState(D3DRS_COLORVERTEX, TRUE);
    }
    BindDetailTexture(Pipeline, TexBlocks[0], 0);
    ApplyDetailTable(Pipeline, GDetailMapTable_06ed1abc, true, 0);
    unsigned long DisabledStage = 1;
    if (bDetailSecondStageVerified)
    {
        BindDetailTexture(Pipeline, TexBlocks[1], 1);
        ApplyDetailTable(Pipeline, GDetailMapTable_06ed1af4, true, 1);
        Pipeline->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 0);
        Pipeline->SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
        float Matrix[16] = {};
        Matrix[0] = DetailTileRate0;
        Matrix[5] = DetailTileRate0;
        Pipeline->SetTextureStageTransform(1, Matrix);
        DisabledStage = 2;
    }
    ApplyDetailTable(Pipeline, GDetailMapTable_06ed1b2c, true, DisabledStage);
    ApplyDetailTable(Pipeline, GDetailMapTable_06ed1a9c, false, 0);
    return GR_OK;
}

GENRESULT DACOM_API FBtDetailMapMaterial::Bind(void* Context)
{
    // FUN_06eb5100: +69 alone selects the optional second-stage body.
    BindMaterialAndContext(Pipeline, &DiffuseR, Context);
    ApplyDetailTable(Pipeline, GBindTable_06ed1484, false, 0);
    BindDetailTexture(Pipeline, TexBlocks[0], 0);
    ApplyDetailTable(Pipeline, GBindTable_06ed14a4, true, 0);
    unsigned long DisabledStage = 1;
    if (bSecondaryTextureVerified)
    {
        BindDetailTexture(Pipeline, TexBlocks[1], 1);
        ApplyDetailTable(Pipeline, GBindTable_06ed14dc, true, 1);
        DisabledStage = 2;
    }
    ApplyDetailTable(Pipeline, GBindTable_06ed1514, true, DisabledStage);
    return GR_OK;
}

GENRESULT DACOM_API FMaterial::Bind(void* /*Context*/)
{
    // LAB_06ec9560: Null and the two-pass detail classes bind nothing here.
    return GR_OK;
}

GENRESULT DACOM_API FMaterial::Teardown(void* Context)
{
    // LAB_06eb1a10: only the conditional COLORVERTEX reset.
    if (Context != nullptr)
    {
        const FRpVertexSrcDesc* Source = ((const FMaterialSubmitContext*)Context)->SourceVertices;
        if (Source != nullptr && Source->Diffuse.Base != nullptr)
            Pipeline->SetRenderState(D3DRS_COLORVERTEX, FALSE);
    }
    return GR_OK;
}

GENRESULT DACOM_API FNullMaterial::Teardown(void* /*Context*/)
{
    return GR_OK;
}

GENRESULT DACOM_API FSinglePassMaterial::Teardown(void* Context)
{
    // FUN_06ebda30: the animated texture-stage reset comes before Context is read.
    if (MatAnim != nullptr)
        Pipeline->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    return FMaterial::Teardown(Context);
}

GENRESULT DACOM_API FBtDetailMapMaterial::Teardown(void* Context)
{
    // FUN_06eb50a0: all six stage-1 states are reset before Context is inspected.
    ApplyDetailTable(Pipeline, GBindTable_06ed1514, true, 1);
    return FMaterial::Teardown(Context);
}

GENRESULT DACOM_API FDetailMap2PassMaterial::Teardown(void* /*Context*/)
{
    // FUN_06eb6e80: the stage-1 table, then three texture-stage resets; Context is ignored.
    ApplyDetailTable(Pipeline, GDetailTable_06ed194c, true, 1);
    Pipeline->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    Pipeline->SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    Pipeline->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1);
    return GR_OK;
}

GENRESULT DACOM_API FIllumDetailMapMaterial::Teardown(void* /*Context*/)
{
    // FUN_06eb98b0: the stage-1 table, then three texture-stage resets; Context is ignored.
    ApplyDetailTable(Pipeline, GDetailTable_06ed1f74, true, 1);
    Pipeline->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    Pipeline->SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    Pipeline->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1);
    return GR_OK;
}

GENRESULT DACOM_API FMasked2DetailMapMaterial::Teardown(void* /*Context*/)
{
    // FUN_06ebb610: the stage-1 table, then three texture-stage resets; Context is ignored.
    ApplyDetailTable(Pipeline, GDetailTable_06ed22cc, true, 1);
    Pipeline->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    Pipeline->SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    Pipeline->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1);
    return GR_OK;
}

GENRESULT DACOM_API FDetailMapMaterial::Teardown(void* /*Context*/)
{
    // FUN_06eb8170: the stage-1 table, then three texture-stage resets; Context is ignored.
    ApplyDetailTable(Pipeline, GDetailMapTable_06ed1b2c, true, 1);
    Pipeline->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    Pipeline->SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    Pipeline->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1);
    return GR_OK;
}

static I3DMathEngine* AcquireMathEngine()
{
    // FUN_06ed0b10 creates on every call; each caller owns its lazy global.
    FMathDesc Desc;
    Desc.Size = sizeof(Desc);
    Desc.InterfaceName = "3DMathEngine";
    Desc.Implementation = nullptr;
    void* Created;
    ICOManager* Manager = DACOM_Acquire();
    Manager->CreateInstance((AGGDESC*)(&Desc), &Created);
    return (I3DMathEngine*)Created;
}

static float AtmosphereEps()
{
    float Value;
    std::memcpy(&Value, &kAtmosphereEpsBits, sizeof(Value));
    return Value;
}

static void NegateTwoPassNormals(const FRpVertexSrcDesc* Source, unsigned long Start,
                                 unsigned long Count, void* Vertices, unsigned long DestStride,
                                 unsigned long DestPosSize)
{
    // LAB_06ebe1ff: read original source normals, not the copied destination.
    uintptr_t Destination = (uintptr_t)Vertices + DestPosSize;
    if ((Source->Flags & kRpVertexSrcIndexed) != 0)
    {
        const unsigned long* Indices = Source->Normal.IndexArray + Start;
        for (unsigned long Index = 0; Index < Count; ++Index)
        {
            const float* Normal = (const float*)(
                (uintptr_t)(Source->Normal.Base) + Indices[Index] * Source->Normal.Stride);
            float* Out = (float*)Destination;
            Out[0] = -Normal[0];
            Out[1] = -Normal[1];
            Out[2] = -Normal[2];
            Destination += DestStride;
        }
        return;
    }
    const unsigned long SourceStride = FvfPosSize(Source->SrcFvf) + FvfNormalSize(Source->SrcFvf) +
                                       FvfColorSize(Source->SrcFvf) + FvfTexSize(Source->SrcFvf);
    uintptr_t NormalAddress = (uintptr_t)(Source->Normal.Base) + SourceStride * Start;
    for (unsigned long Index = 0; Index < Count; ++Index)
    {
        const float* Normal = (const float*)NormalAddress;
        float* Out = (float*)Destination;
        Out[0] = -Normal[0];
        Out[1] = -Normal[1];
        Out[2] = -Normal[2];
        NormalAddress += SourceStride;
        Destination += DestStride;
    }
}

GENRESULT FSinglePassTwoMaterial::SubmitSecondPass(void* Context, unsigned long PrimType, unsigned long Start,
                                            unsigned long Count, unsigned long IbStart, unsigned long IndexSpan)
{
    // LAB_06ebe0d2..06ebe38d: the caller has already tested first result and source presence.
    FMaterialSubmitContext* SubmitContext = (FMaterialSubmitContext*)Context;
    const FRpVertexSrcDesc* Source = SubmitContext->SourceVertices;
    IVertexBufferManager* Manager = VBMgr;
    FVertexMemoryLock Lock;
    const GENRESULT Locked = Manager->LockVertexMemory(Source->SrcFvf, Count, &Lock);
    if (Locked < 0)
        return Locked;
    const unsigned long DestPosSize = FvfPosSize(Lock.Listing.Fvf);
    const unsigned long DestStride = DestPosSize + FvfNormalSize(Lock.Listing.Fvf) +
                                     FvfColorSize(Lock.Listing.Fvf) + FvfTexSize(Lock.Listing.Fvf);
    void* Destination = (void*)((uintptr_t)(Lock.Vertices) - DestStride * Start);
    Manager->CopyVertexData(Destination, Lock.Listing.Fvf,
                           SubmitContext->SourceVertices, Start,
                           Count);
    if ((Source->SrcFvf & D3DFVF_NORMAL) != 0)
        NegateTwoPassNormals(Source, Start, Count, Lock.Vertices, DestStride, DestPosSize);
    Manager->UnlockVertexMemory(&Lock);
    int SavedCull;
    Pipeline->GetRenderState(D3DRS_CULLMODE, &SavedCull);
    // kCullFlip is DAT_06ed4848 {0,1,3,2}.
    Pipeline->SetRenderState(D3DRS_CULLMODE, kCullFlip[SavedCull]);
    const GENRESULT Result = Pipeline->DrawIndexedPrimitiveVb(PrimType, Lock.Listing.VertexBuffer,
        Lock.Listing.BaseVertex, Count,
        (unsigned short*)((uintptr_t)(SubmitContext->Indices) + IbStart * 2),
        IndexSpan, 0);
    Pipeline->SetRenderState(D3DRS_CULLMODE, SavedCull);
    return Result;
}

// Each two-sided class has its own {-, 1, 3, 2} cull lookup indexed by D3DCULL (1..3). Word
// zero is whatever precedes the table in .rdata: DAT_06ed44cc aliases the "Ec" string,
// DAT_06ed46a8 the tail of another name, DAT_06ed4824 a zero.
static const unsigned long GBtDetailTwoCullFlip[4] = {0x00006345, D3DCULL_NONE, D3DCULL_CCW, D3DCULL_CW};
static const unsigned long GEcEtTwoCullFlip[4] = {0x00656d61, D3DCULL_NONE, D3DCULL_CCW, D3DCULL_CW};
static const unsigned long GNebulaTwoCullFlip[4] = {0, D3DCULL_NONE, D3DCULL_CCW, D3DCULL_CW};

GENRESULT FMaterial::SubmitBackFacePass(void* Context, unsigned long PrimType, unsigned long Start,
                                       unsigned long Count, unsigned long IbStart,
                                       unsigned long IndexSpan, const unsigned long* CullFlip)
{
    // LAB_06eb5655..593a (BtDetailMapTwo), and the same code in EcEtTwo 06eb8d90 and
    // NebulaTwo 06ebc970: no alpha-state scope surrounds this second pass.
    FMaterialSubmitContext* SubmitContext = (FMaterialSubmitContext*)Context;
    const FRpVertexSrcDesc* Source = SubmitContext->SourceVertices;
    FVertexMemoryLock Lock;
    const GENRESULT Locked = VBMgr->LockVertexMemory(
        Source->SrcFvf, Count, &Lock);
    if (Locked < 0)
        return Locked;
    Source = SubmitContext->SourceVertices;
    const bool bHasNormals = (Source->SrcFvf & D3DFVF_NORMAL) != 0;
    const unsigned long DestPosSize = FvfPosSize(Lock.Listing.Fvf);
    const unsigned long DestStride = DestPosSize + FvfNormalSize(Lock.Listing.Fvf) +
                                     FvfColorSize(Lock.Listing.Fvf) + FvfTexSize(Lock.Listing.Fvf);
    void* Destination = (void*)((uintptr_t)(Lock.Vertices) -
                                                DestStride * Start);
    VBMgr->CopyVertexData(
        Destination, Lock.Listing.Fvf,
        SubmitContext->SourceVertices, Start,
        Count);
    if (bHasNormals)
    {
        // Retail re-reads the descriptor after CopyVertexData.
        Source = SubmitContext->SourceVertices;
        NegateTwoPassNormals(Source, Start, Count, Lock.Vertices, DestStride, DestPosSize);
    }
    VBMgr->UnlockVertexMemory(&Lock);
    int SavedCull;
    Pipeline->GetRenderState(D3DRS_CULLMODE, &SavedCull);
    Pipeline->SetRenderState(D3DRS_CULLMODE, CullFlip[SavedCull]);
    const GENRESULT Result = Pipeline->DrawIndexedPrimitiveVb(PrimType, Lock.Listing.VertexBuffer,
        Lock.Listing.BaseVertex, Count,
        (unsigned short*)((uintptr_t)(SubmitContext->Indices) +
                                           IbStart * 2), IndexSpan, 0);
    Pipeline->SetRenderState(D3DRS_CULLMODE, SavedCull);
    return Result;
}

GENRESULT FAtmosphereMaterial::SubmitAtmosphereLock(void* Context, unsigned long PrimType, unsigned long Start,
                                          unsigned long Count, unsigned long IbStart, unsigned long IndexSpan)
{
    // LAB_06eb2d40: readiness is the only early success branch.
    if (bAtmosphereReady == 0)
        return GR_OK;
    FMaterialSubmitContext* SubmitContext = (FMaterialSubmitContext*)Context;
    const FRpVertexSrcDesc* Source = SubmitContext->SourceVertices;
    IVertexBufferManager* Manager = VBMgr;
    FVertexMemoryLock Lock;
    const GENRESULT Locked = Manager->LockVertexMemory(Source->SrcFvf, Count, &Lock);
    if (Locked < 0)
        return Locked;
    const unsigned long DestStride = FvfTexSize(Lock.Listing.Fvf) + FvfColorSize(Lock.Listing.Fvf) +
                                     FvfNormalSize(Lock.Listing.Fvf) + FvfPosSize(Lock.Listing.Fvf);
    void* Destination = (void*)((uintptr_t)(Lock.Vertices) -
                                                 DestStride * Start);
    Manager->CopyVertexData(Destination, Lock.Listing.Fvf,
                           SubmitContext->SourceVertices, Start,
                           Count);
    const unsigned long TexOffset = FvfColorSize(Lock.Listing.Fvf) + FvfNormalSize(Lock.Listing.Fvf) +
                                    FvfPosSize(Lock.Listing.Fvf);
    uintptr_t DestUv = (uintptr_t)(Lock.Vertices) + TexOffset;

    Transform ScaleTransform;
    std::memset(&ScaleTransform, 0, sizeof(ScaleTransform));
    ScaleTransform.Rotation.E[0][0] = Scale;
    ScaleTransform.Rotation.E[1][1] = Scale;
    ScaleTransform.Rotation.E[2][2] = Scale;
    Transform Temporary;
    const Transform& World = *SubmitContext->World;
    const Transform& View = *SubmitContext->View;
    if (G_MathEngine == nullptr)
        G_MathEngine = AcquireMathEngine();
    G_MathEngine->MulTransform(Temporary, World, ScaleTransform);
    const Transform ScaledWorld = Temporary;
    int State7 = 1;
    Pipeline->GetPipelineState(Render::PipelineStateViewspaceLights, &State7);
    if (State7 != 0)
    {
        if (G_MathEngine == nullptr)
            G_MathEngine = AcquireMathEngine();
        G_MathEngine->MulTransform(Temporary, View, ScaledWorld);
        const Transform Modelview = Temporary;
        Pipeline->SetModelview(&Modelview);
    }
    else
        Pipeline->SetWorld(&ScaledWorld);

    Vector Center = {};
    float Radius = 1.0f;
    if (SubmitContext->MaterialContext != nullptr)
        ((IEngine*)(SubmitContext->MaterialContext))->GetInstanceExtent(
            SubmitContext->Inst, 0, &Radius, &Center);
    const float RadiusOverScale = (float)((double)Radius / Scale);
    if (G_MathEngine == nullptr)
        G_MathEngine = AcquireMathEngine();
    G_MathEngine->MulTransform(Temporary, View, World);
    const Transform ViewWorld = Temporary;
    const Vector Origin = {};
    Vector Camera;
    if (G_MathEngine == nullptr)
        G_MathEngine = AcquireMathEngine();
    G_MathEngine->InverseTransform(Camera, ViewWorld, Origin);

    // (X*X + Y*Y) + Z*Z; the double sqrt is compared before rounding to float.
    const double SquaredXY = (double)(Camera.x) * Camera.x +
                             (double)(Camera.y) * Camera.y;
    const double LengthWide = std::sqrt(SquaredXY + (double)(Camera.z) * Camera.z);
    float Length = (float)LengthWide;
    // Only an ordered <= 0 clamps; NaN is kept.
    if (LengthWide <= 0.0)
        Length = AtmosphereEps();
    const double InverseLength = 1.0 / Length;
    const Vector Direction = {
        (float)((double)(Camera.x) * InverseLength),
        (float)((double)(Camera.y) * InverseLength),
        (float)((double)(Camera.z) * InverseLength)};
    const float Threshold = (float)(InverseLength * Radius);
    double Shell = ((double)Scale - 1.0) * RadiusOverScale;
    // Same ordered <= 0 clamp; Shell stays double.
    if (Shell <= 0.0)
        Shell = AtmosphereEps();
    const double Distance = (double)Length - Radius;
    const double Factor = Distance / (Shell + Distance);
    const float FactorCubed = (float)((Factor * Factor) * Factor);
    double Fade = 1.0 - (double)Alpha;
    // Unordered skips the scale.
    if ((double)FactorCubed < 0.5)
    {
        Fade *= FactorCubed;
        Fade += Fade;
    }
    uintptr_t NormalAddress = (uintptr_t)(Source->Normal.Base) + Start * 12UL;
    uintptr_t UvCursor = (uintptr_t)(Source->Tex[0].Base) + Start * 8UL;
    const uintptr_t UvEnd = UvCursor + Count * 8UL;
    if ((Source->Flags & kRpVertexSrcIndexed) == 0)
    {
        while (UvCursor != UvEnd)
        {
            float* Out = (float*)DestUv;
            Out[1] = (float)Fade;
            const float* Normal = (const float*)NormalAddress;
            // Retail sums Z, then Y, then X, in double precision.
            const double DotZY = (double)(Direction.z) * Normal[2] +
                                 (double)(Direction.y) * Normal[1];
            const double Dot = DotZY + (double)(Direction.x) * Normal[0];
            // Unordered takes the formula branch.
            if (Dot < (double)Threshold)
                Out[0] = 0.0f;
            else
                Out[0] = (float)(((Dot - Threshold) / (1.0 - Threshold)) *
                                             FadeOff * FactorCubed);
            DestUv += DestStride;
            UvCursor += 8;
            NormalAddress += 12;
        }
    }
    Manager->UnlockVertexMemory(&Lock);
    int SavedZFunc;
    Pipeline->GetRenderState(D3DRS_ZFUNC, &SavedZFunc);
    Pipeline->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    int SavedZWrite;
    Pipeline->GetRenderState(D3DRS_ZWRITEENABLE, &SavedZWrite);
    Pipeline->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    Pipeline->DrawIndexedPrimitiveVb(PrimType, Lock.Listing.VertexBuffer, Lock.Listing.BaseVertex, Count,
        (unsigned short*)((uintptr_t)(SubmitContext->Indices) + IbStart * 2),
        IndexSpan, 0);
    Pipeline->SetRenderState(D3DRS_ZFUNC, SavedZFunc);
    Pipeline->SetRenderState(D3DRS_ZWRITEENABLE, SavedZWrite);
    // DAT_06ed1130 {COLORVERTEX, 0}, whether or not the mesh has diffuse colours.
    Pipeline->SetRenderState(D3DRS_COLORVERTEX, FALSE);
    return GR_OK;
}

GENRESULT FMaterial::SubmitIndexedListing(FMaterialSubmitContext* Context,
                                         unsigned long PrimType, unsigned long Start,
                                         unsigned long Count, unsigned long IbStart,
                                         unsigned long IndexSpan)
{
    // FUN_06eb59a0: only the listing pointer is optional.
    if (Context->VbListing == nullptr)
        return -1;
    return Pipeline->DrawIndexedPrimitiveVb(PrimType, Context->VbListing->VertexBuffer,
        Context->VbListing->BaseVertex + Start, Count,
        (unsigned short*)((uintptr_t)(Context->Indices) + IbStart * 2),
        IndexSpan, 0);
}

GENRESULT FMaterial::SubmitCopiedVertices(FMaterialSubmitContext* Context,
                                         unsigned long PrimType, unsigned long Start,
                                         unsigned long Count, unsigned long IbStart,
                                         unsigned long IndexSpan)
{
    // FUN_06eb59f0: all dependencies are mandatory; only lock failure exits early.
    const FRpVertexSrcDesc* Source = Context->SourceVertices;
    IVertexBufferManager* Manager = VBMgr;
    FVertexMemoryLock Lock;
    const GENRESULT Result = Manager->LockVertexMemory(Source->SrcFvf, Count, &Lock);
    if (Result < 0)
        return Result;
    const unsigned long Stride = FvfPosSize(Lock.Listing.Fvf) + FvfNormalSize(Lock.Listing.Fvf) +
                                 FvfColorSize(Lock.Listing.Fvf) + FvfTexSize(Lock.Listing.Fvf);
    void* Destination = (void*)((uintptr_t)(Lock.Vertices) -
                                               Stride * Start);
    Manager->CopyVertexData(Destination, Lock.Listing.Fvf,
                           Context->SourceVertices, Start,
                           Count);
    Manager->UnlockVertexMemory(&Lock);
    return Pipeline->DrawIndexedPrimitiveVb(PrimType, Lock.Listing.VertexBuffer, Lock.Listing.BaseVertex, Count,
        (unsigned short*)((uintptr_t)(Context->Indices) + IbStart * 2),
        IndexSpan, 0);
}

GENRESULT FMaterial::SubmitKernel(void* Context, unsigned long PrimType, unsigned long Start,
                                  unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                                  unsigned long Flags)
{
    // FUN_06eb3b90: direct draw, indexed listing, then vertex-copy fallback.
    FMaterialSubmitContext* SubmitContext = (FMaterialSubmitContext*)Context;
    GENRESULT Result = -1;
    if ((Flags & kMaterialSubmitUseBuffers) != 0)
    {
        Result = RPDraw->DrawIndexedPrimitive(PrimType, Start, Count,
            SubmitContext->IbScratch->Bias + IbStart, IndexSpan);
        if (Result >= 0)
            return Result;
    }
    Result = SubmitIndexedListing(SubmitContext, PrimType, Start, Count, IbStart, IndexSpan);
    if (Result < 0 && SubmitContext->SourceVertices != nullptr && Count > 0 && IndexSpan > 0)
        Result = SubmitCopiedVertices(SubmitContext, PrimType, Start, Count, IbStart, IndexSpan);
    return Result;
}

GENRESULT DACOM_API FSinglePassMaterial::Submit(void* Context, unsigned long PrimType, unsigned long Start,
                                                unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                                                unsigned long Flags)
{
    // LAB_06ebda80: alpha scope surrounds the complete FUN_06eb3b90 draw route.
    const bool bSetAlphaTest = bAlphatest != 0 && (Flags & kMaterialSubmitNoAlphaTest) == 0;
    int Saved18;
    int Saved19;
    int Saved0F;
    if (bSetAlphaTest)
    {
        Pipeline->GetRenderState(D3DRS_ALPHAREF, &Saved18);
        Pipeline->SetRenderState(D3DRS_ALPHAREF, 0x80);
        Pipeline->GetRenderState(D3DRS_ALPHAFUNC, &Saved19);
        Pipeline->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
        Pipeline->GetRenderState(D3DRS_ALPHATESTENABLE, &Saved0F);
        Pipeline->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
    }
    const GENRESULT Result = SubmitKernel(Context, PrimType, Start, Count, IbStart, IndexSpan, Flags);
    if (bSetAlphaTest)
    {
        Pipeline->SetRenderState(D3DRS_ALPHAREF, Saved18);
        Pipeline->SetRenderState(D3DRS_ALPHAFUNC, Saved19);
        Pipeline->SetRenderState(D3DRS_ALPHATESTENABLE, Saved0F);
    }
    return Result;
}

GENRESULT DACOM_API FMaterial::Submit(void* Context, unsigned long PrimType, unsigned long Start,
                                      unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                                      unsigned long Flags)
{
    return SubmitKernel(Context, PrimType, Start, Count, IbStart, IndexSpan, Flags);
}

GENRESULT DACOM_API FNullMaterial::Submit(void* /*Context*/, unsigned long /*PrimType*/, unsigned long /*Start*/,
                                          unsigned long /*Count*/, unsigned long /*IbStart*/, unsigned long /*IndexSpan*/,
                                          unsigned long /*Flags*/)
{
    return GR_OK;
}

GENRESULT DACOM_API FAtmosphereMaterial::Submit(void* Context, unsigned long PrimType, unsigned long Start,
                                                unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                                                unsigned long /*Flags*/)
{
    return SubmitAtmosphereLock(Context, PrimType, Start, Count, IbStart, IndexSpan);
}

GENRESULT DACOM_API FLightMapMaterial::Submit(void* Context, unsigned long PrimType, unsigned long Start,
                                              unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                                              unsigned long Flags)
{
    // LAB_06eba860: temporary ZFUNC=4; all draw/lock results are discarded.
    int Saved17;
    Pipeline->GetRenderState(D3DRS_ZFUNC, &Saved17);
    Pipeline->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    SubmitKernel(Context, PrimType, Start, Count, IbStart, IndexSpan, Flags);
    Pipeline->SetRenderState(D3DRS_ZFUNC, Saved17);
    return GR_OK;
}

GENRESULT DACOM_API FIllumDetailMapMaterial::Submit(void* Context, unsigned long PrimType, unsigned long Start,
                                                    unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                                                    unsigned long Flags)
{
    // LAB_06eb9e40: first pass ALPHATEST=0 in either mode.
    int Saved0F;
    Pipeline->GetRenderState(D3DRS_ALPHATESTENABLE, &Saved0F);
    Pipeline->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    ApplyIllumDetailPassState(0);
    SubmitKernel(Context, PrimType, Start, Count, IbStart, IndexSpan, Flags);
    if (DetailPassCount == 1)
    {
        Pipeline->SetRenderState(D3DRS_ALPHATESTENABLE, Saved0F);
        Teardown(nullptr);
        return GR_OK;
    }
    Pipeline->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
    int Saved18;
    int Saved19;
    int Saved17;
    int Saved0E;
    Pipeline->GetRenderState(D3DRS_ALPHAREF, &Saved18);
    Pipeline->SetRenderState(D3DRS_ALPHAREF, 1);
    Pipeline->GetRenderState(D3DRS_ALPHAFUNC, &Saved19);
    Pipeline->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
    Pipeline->GetRenderState(D3DRS_ZFUNC, &Saved17);
    Pipeline->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    Pipeline->GetRenderState(D3DRS_ZWRITEENABLE, &Saved0E);
    Pipeline->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    ApplyIllumDetailPassState(1);
    SubmitKernel(Context, PrimType, Start, Count, IbStart, IndexSpan, Flags);
    Pipeline->SetRenderState(D3DRS_ZWRITEENABLE, Saved0E);
    Pipeline->SetRenderState(D3DRS_ZFUNC, Saved17);
    Pipeline->SetRenderState(D3DRS_ALPHAFUNC, Saved19);
    Pipeline->SetRenderState(D3DRS_ALPHAREF, Saved18);
    Pipeline->SetRenderState(D3DRS_ALPHATESTENABLE, Saved0F);
    Teardown(nullptr);
    return GR_OK;
}

GENRESULT DACOM_API FMasked2DetailMapMaterial::Submit(void* Context, unsigned long PrimType, unsigned long Start,
                                                      unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                                                      unsigned long Flags)
{
    // LAB_06ebbc40: one-pass fallback, or two unconditional kernel draws.
    int Saved0F;
    Pipeline->GetRenderState(D3DRS_ALPHATESTENABLE, &Saved0F);
    Pipeline->SetRenderState(D3DRS_ALPHATESTENABLE, DetailPassCount == 1 ? 0 : 1);
    if (DetailPassCount == 1)
    {
        ApplyMaskedDetailPassState(0);
        SubmitKernel(Context, PrimType, Start, Count, IbStart, IndexSpan, Flags);
        Pipeline->SetRenderState(D3DRS_ALPHATESTENABLE, Saved0F);
        Teardown(nullptr);
        return GR_OK;
    }
    int Saved18;
    int Saved19;
    int Saved17;
    Pipeline->GetRenderState(D3DRS_ALPHAREF, &Saved18);
    Pipeline->SetRenderState(D3DRS_ALPHAREF, 0xFE);
    Pipeline->GetRenderState(D3DRS_ALPHAFUNC, &Saved19);
    Pipeline->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_LESS);
    ApplyMaskedDetailPassState(0);
    SubmitKernel(Context, PrimType, Start, Count, IbStart, IndexSpan, Flags);
    Pipeline->SetRenderState(D3DRS_ALPHAREF, 1);
    Pipeline->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
    Pipeline->GetRenderState(D3DRS_ZFUNC, &Saved17);
    Pipeline->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    ApplyMaskedDetailPassState(1);
    SubmitKernel(Context, PrimType, Start, Count, IbStart, IndexSpan, Flags);
    Pipeline->SetRenderState(D3DRS_ZFUNC, Saved17);
    Pipeline->SetRenderState(D3DRS_ALPHAFUNC, Saved19);
    Pipeline->SetRenderState(D3DRS_ALPHAREF, Saved18);
    Pipeline->SetRenderState(D3DRS_ALPHATESTENABLE, Saved0F);
    Teardown(nullptr);
    return GR_OK;
}

GENRESULT DACOM_API FDetailMap2PassMaterial::Submit(void* Context, unsigned long PrimType, unsigned long Start,
                                                    unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                                                    unsigned long Flags)
{
    // LAB_06eb7560: mode 1 draws once; mode 2 always draws both passes.
    int Saved0F;
    Pipeline->GetRenderState(D3DRS_ALPHATESTENABLE, &Saved0F);
    Pipeline->SetRenderState(D3DRS_ALPHATESTENABLE, DetailPassCount == 1 ? 0 : 1);
    if (DetailPassCount == 1)
    {
        ApplyDetailPassState(0);
        SubmitKernel(Context, PrimType, Start, Count, IbStart, IndexSpan, Flags);
        Pipeline->SetRenderState(D3DRS_ALPHATESTENABLE, Saved0F);
        Teardown(nullptr);
        return GR_OK;
    }
    int Saved18;
    int Saved19;
    int Saved17;
    Pipeline->GetRenderState(D3DRS_ALPHAREF, &Saved18);
    Pipeline->SetRenderState(D3DRS_ALPHAREF, 0xFE);
    Pipeline->GetRenderState(D3DRS_ALPHAFUNC, &Saved19);
    Pipeline->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_LESS);
    ApplyDetailPassState(0);
    SubmitKernel(Context, PrimType, Start, Count, IbStart, IndexSpan, Flags);
    Pipeline->SetRenderState(D3DRS_ALPHAREF, 1);
    Pipeline->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
    Pipeline->GetRenderState(D3DRS_ZFUNC, &Saved17);
    Pipeline->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    ApplyDetailPassState(1);
    SubmitKernel(Context, PrimType, Start, Count, IbStart, IndexSpan, Flags);
    // Scope destructors +28/+34/+10/+1C: ZFUNC, DESTBLEND, SRCBLEND, ALPHATEST.
    Pipeline->SetRenderState(D3DRS_ZFUNC, Saved17);
    Pipeline->SetRenderState(D3DRS_ALPHAFUNC, Saved19);
    Pipeline->SetRenderState(D3DRS_ALPHAREF, Saved18);
    Pipeline->SetRenderState(D3DRS_ALPHATESTENABLE, Saved0F);
    Teardown(nullptr);
    return GR_OK;
}

GENRESULT DACOM_API FBtDetailMapTwoMaterial::Submit(void* Context, unsigned long PrimType, unsigned long Start,
                                                    unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                                                    unsigned long Flags)
{
    // LAB_06eb55b0: a successful first pass and a source descriptor enable the back-face pass.
    const GENRESULT Result = SubmitKernel(Context, PrimType, Start, Count, IbStart, IndexSpan, Flags);
    if (Result < 0 || ((FMaterialSubmitContext*)Context)->SourceVertices == nullptr)
        return Result;
    return SubmitBackFacePass(Context, PrimType, Start, Count, IbStart, IndexSpan, GBtDetailTwoCullFlip);
}

GENRESULT DACOM_API FEcEtTwoMaterial::Submit(void* Context, unsigned long PrimType, unsigned long Start,
                                             unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                                             unsigned long Flags)
{
    // 06eb8d90: a successful first pass and a source descriptor enable the back-face pass.
    const GENRESULT Result = SubmitKernel(Context, PrimType, Start, Count, IbStart, IndexSpan, Flags);
    if (Result < 0 || ((FMaterialSubmitContext*)Context)->SourceVertices == nullptr)
        return Result;
    return SubmitBackFacePass(Context, PrimType, Start, Count, IbStart, IndexSpan, GEcEtTwoCullFlip);
}

GENRESULT DACOM_API FNebulaTwoMaterial::Submit(void* Context, unsigned long PrimType, unsigned long Start,
                                               unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                                               unsigned long Flags)
{
    // 06ebc970: a successful first pass and a source descriptor enable the back-face pass.
    const GENRESULT Result = SubmitKernel(Context, PrimType, Start, Count, IbStart, IndexSpan, Flags);
    if (Result < 0 || ((FMaterialSubmitContext*)Context)->SourceVertices == nullptr)
        return Result;
    return SubmitBackFacePass(Context, PrimType, Start, Count, IbStart, IndexSpan, GNebulaTwoCullFlip);
}

GENRESULT DACOM_API FSinglePassTwoMaterial::Submit(void* Context, unsigned long PrimType, unsigned long Start,
                                                   unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                                                   unsigned long Flags)
{
    // LAB_06ebdfc0: the first pass restores its alpha scope before the result and mesh tests.
    const GENRESULT Result =
        FSinglePassMaterial::Submit(Context, PrimType, Start, Count, IbStart, IndexSpan, Flags);
    if (Result < 0 || ((FMaterialSubmitContext*)Context)->SourceVertices == nullptr)
        return Result;
    return SubmitSecondPass(Context, PrimType, Start, Count, IbStart, IndexSpan);
}

GENRESULT DACOM_API FMaterial::SetName(const char* Name)
{
    // LAB_06eb1a50: release first, then copy at most 0x40 bytes including NUL.
    delete[] InstanceName;
    InstanceName = nullptr;
    if (Name == nullptr)
        return GR_OK;
    size_t Length = std::strlen(Name) + 1;
    if (Length > 0x40)
        Length = 0x40;
    char* Copy = new char[Length];
    InstanceName = Copy;
    std::strncpy(Copy, Name, Length);
    Copy[Length - 1] = '\0';
    _strlwr(Copy);
    return GR_OK;
}

GENRESULT DACOM_API FMaterial::GetName(char* Out, unsigned long Size)
{
    // LAB_06eb1ac0.
    if (InstanceName == nullptr)
        return GR_GENERIC;
    std::strncpy(Out, InstanceName, Size);
    Out[Size - 1] = '\0';
    return GR_OK;
}

GENRESULT DACOM_API FMaterial::CopyClassName(char* Out, unsigned long Size)
{
    if (ClassName() == nullptr)
        return GR_GENERIC;
    std::strncpy(Out, ClassName(), Size);
    Out[Size - 1] = '\0';
    return GR_OK;
}

GENRESULT DACOM_API FMaterial::GetPassCount(int* Out)
{
    *Out = 1;
    return GR_OK;
}

GENRESULT DACOM_API FDetailMap2PassMaterial::GetPassCount(int* Out)
{
    // LAB_06eb6e60 reads the live +0x74.
    *Out = (int)DetailPassCount;
    return GR_OK;
}

GENRESULT DACOM_API FTwoDetailMapMaterial::GetPassCount(int* Out)
{
    // 06ec9a50 reads the live +0x78.
    *Out = (int)DetailPassCount;
    return GR_OK;
}

GENRESULT DACOM_API FMaterial::GetRenderFlags(unsigned long* Out)
{
    *Out = bAlphablend ? 0xA : 8; // LAB_06eb80b0: SinglePass, EcEt, DetailMap
    return GR_OK;
}

GENRESULT DACOM_API FSinglePassTwoMaterial::GetRenderFlags(unsigned long* Out)
{
    *Out = bAlphablend ? 0x03 : 0x01; // LAB_06ebdfa0
    return GR_OK;
}

GENRESULT DACOM_API FEcEtTwoMaterial::GetRenderFlags(unsigned long* Out)
{
    *Out = bAlphablend ? 0x03 : 0x01; // LAB_06ebdfa0
    return GR_OK;
}

GENRESULT DACOM_API FDcDtEtMaterial::GetRenderFlags(unsigned long* Out)
{
    *Out = bAlphablend ? 0x4A : 0x48; // LAB_06eb6150
    return GR_OK;
}

GENRESULT DACOM_API FBtDetailMapMaterial::GetRenderFlags(unsigned long* Out)
{
    *Out = bAlphablend ? 0x4A : 0x48; // LAB_06eb6150
    return GR_OK;
}

GENRESULT DACOM_API FBtDetailMapTwoMaterial::GetRenderFlags(unsigned long* Out)
{
    *Out = bAlphablend ? 0x43 : 0x41; // LAB_06eb5590
    return GR_OK;
}

GENRESULT DACOM_API FAtmosphereMaterial::GetRenderFlags(unsigned long* Out)
{
    *Out = 0xA2;
    return GR_OK;
}

GENRESULT DACOM_API FDetailMap2PassMaterial::GetRenderFlags(unsigned long* Out)
{
    *Out = 0x48;
    return GR_OK;
}

GENRESULT DACOM_API FTwoDetailMapMaterial::GetRenderFlags(unsigned long* Out)
{
    *Out = 0x48;
    return GR_OK;
}

GENRESULT DACOM_API FLightMapMaterial::GetRenderFlags(unsigned long* Out)
{
    *Out = 0xA; // 06eba700: the alpha flag is ignored
    return GR_OK;
}

GENRESULT DACOM_API FNebulaMaterial::GetRenderFlags(unsigned long* Out)
{
    *Out = 0xA; // 06eba700: the alpha flag is ignored
    return GR_OK;
}

GENRESULT DACOM_API FNebulaTwoMaterial::GetRenderFlags(unsigned long* Out)
{
    *Out = 0x03; // 06ebc950
    return GR_OK;
}

GENRESULT DACOM_API FNullMaterial::GetRenderFlags(unsigned long* Out)
{
    *Out = 8;
    return GR_OK;
}

GENRESULT DACOM_API FMaterial::GetParameterCount(unsigned long* OutCount)
{
    // LAB_06eb1b50.
    *OutCount = GetParameterTable(nullptr);
    return GR_OK;
}

static void CopyPropertyValue(FShadingPropertyValue* Destination, const FShadingPropertyValue* Source)
{
    // FUN_06eb1d40: capture the tag, then release before reading the live source payload.
    const unsigned long Type = Source->Type;
    Destination->SetType(Type);
    switch (Type)
    {
    case kMaterialParamVector4:
        Destination->Payload3 = Source->Payload3;
        [[fallthrough]];
    case kMaterialParamVector3:
        Destination->Payload2 = Source->Payload2;
        Destination->Payload1 = Source->Payload1;
        [[fallthrough]];
    default:
        Destination->Payload0 = Source->Payload0;
        break;
    case kMaterialParamString:
    case kMaterialParamTextureName:
    {
        const char* Text = (const char*)(Source->Payload0);
        if (Destination->Payload0 != 0)
            HeapFree(GetProcessHeap(), 0, (void*)(Destination->Payload0));
        Destination->Payload0 = 0;
        if (Text != nullptr)
        {
            char* Copy = (char*)(HeapAlloc(GetProcessHeap(), 0, std::strlen(Text) + 1));
            Destination->Payload0 = (unsigned long)Copy;
            std::strcpy(Copy, Text);
        }
        break;
    }
    case kMaterialParamComponent:
    {
        IDAComponent* Component = (IDAComponent*)(Source->Payload0);
        IDAComponent* Previous = (IDAComponent*)(Destination->Payload0);
        Destination->Payload0 = (unsigned long)Component;
        if (Component != nullptr)
            Component->AddRef();
        if (Previous != nullptr)
            Previous->Release();
        break;
    }
    }
}

GENRESULT DACOM_API FMaterial::GetParameterDescriptor(unsigned long Index, void* OutDesc)
{
    // LAB_06eb1b70: index is checked before the mandatory schema/output is used.
    FShadingPropertyRecord* Destination = (FShadingPropertyRecord*)OutDesc;
    void* Base;
    const unsigned long Count = GetParameterTable(&Base);
    if (Index >= Count)
        return GR_GENERIC;

    const FShadingPropertyRecord* Source = (const FShadingPropertyRecord*)Base + Index;
    Destination->Flags = Source->Flags;
    Destination->Name = Source->Name;
    Destination->Description = Source->Description;
    Destination->Callback = Source->Callback;
    CopyPropertyValue(&Destination->Current, &Source->Current);
    CopyPropertyValue(&Destination->Minimum, &Source->Minimum);
    CopyPropertyValue(&Destination->Maximum, &Source->Maximum);
    return GR_OK;
}

GENRESULT DACOM_API FMaterial::GetParameterValue(unsigned long Index, void* InOutDesc)
{
    FShadingPropertyValue* Result = (FShadingPropertyValue*)InOutDesc;
    void* Base;
    const unsigned long Count = GetParameterTable(&Base);
    if (Index >= Count)
        return GR_GENERIC;

    FShadingPropertyRecord* EntryRecord = (FShadingPropertyRecord*)Base + Index;
    const unsigned long Type = EntryRecord->Current.Type;
    if (Result->Type != Type)
        Result->ReleaseOwnedPayload();
    Result->Type = Type;

    void* Target = GetParameterAddress(Index);
    if (Target == nullptr)
        return GR_GENERIC;

    switch (Result->Type)
    {
    case kMaterialParamInteger:
    case kMaterialParamFloat:
    case kMaterialParamWrapMode:
    case kMaterialParamAddressMode:
    case 10:
        Result->Payload0 = *(unsigned long*)Target;
        break;
    case kMaterialParamVector4:
        Result->Payload3 = ((unsigned long*)Target)[3];
        [[fallthrough]];
    case kMaterialParamVector3:
        Result->Payload2 = ((unsigned long*)Target)[2];
        Result->Payload1 = ((unsigned long*)Target)[1];
        Result->Payload0 = ((unsigned long*)Target)[0];
        break;
    case kMaterialParamString:
    case kMaterialParamTextureName:
    {
        const char* Source = *(const char* const*)Target;
        if (Result->Payload0 != 0)
            HeapFree(GetProcessHeap(), 0, (void*)(Result->Payload0));
        Result->Payload0 = 0;
        if (Source != nullptr)
        {
            const size_t Size = std::strlen(Source) + 1;
            char* Copy = (char*)(HeapAlloc(GetProcessHeap(), 0, Size));
            Result->Payload0 = (unsigned long)Copy;
            std::strcpy(Copy, Source);
        }
        break;
    }
    case kMaterialParamComponent:
    {
        IDAComponent* Component = *(IDAComponent**)Target;
        IDAComponent* Previous = (IDAComponent*)(Result->Payload0);
        Result->Payload0 = (unsigned long)Component;
        if (Component != nullptr)
            Component->AddRef();
        if (Previous != nullptr)
            Previous->Release();
        break;
    }
    default:
        // Retail copies the first dword for unknown types too.
        Result->Payload0 = *(unsigned long*)Target;
        break;
    }
    return GR_OK;
}

GENRESULT DACOM_API FMaterial::SetParameterValue(unsigned long Index, void* Value)
{
    void* Base;
    const unsigned long Count = GetParameterTable(&Base);
    if (Index >= Count)
        return GR_GENERIC;

    FShadingPropertyRecord* EntryRecord = (FShadingPropertyRecord*)Base + Index;
    if (!ValueWithinDeclaredRange(Index, EntryRecord, Value))
        return GR_GENERIC;

    // Retail re-reads the table entry after the virtual range check.
    StoreParameterValue(Index, (FShadingPropertyRecord*)Base + Index, Value);
    return GR_OK;
}

// Slot 20: each class answers with its own name.
const char* __thiscall FSinglePassMaterial::ClassName() { return "SinglePassMaterial"; }
const char* __thiscall FSinglePassTwoMaterial::ClassName() { return "SinglePassTwoMaterial"; }
const char* __thiscall FDcDtEtMaterial::ClassName() { return "DcDtEt"; }
const char* __thiscall FEcEtMaterial::ClassName() { return "EcEt"; }
const char* __thiscall FEcEtTwoMaterial::ClassName() { return "EcEtTwo"; }
const char* __thiscall FAtmosphereMaterial::ClassName() { return "AtmosphereMaterial"; }
const char* __thiscall FBtDetailMapMaterial::ClassName() { return "BtDetailMapMaterial"; }
const char* __thiscall FBtDetailMapTwoMaterial::ClassName() { return "BtDetailMapTwoMaterial"; }
const char* __thiscall FDetailMap2PassMaterial::ClassName() { return "DetailMap2Dm1Msk2PassMaterial"; }
const char* __thiscall FDetailMapMaterial::ClassName() { return "DetailMapMaterial"; }
const char* __thiscall FIllumDetailMapMaterial::ClassName() { return "IllumDetailMapMaterial"; }
const char* __thiscall FMasked2DetailMapMaterial::ClassName() { return "Masked2DetailMapMaterial"; }
const char* __thiscall FLightMapMaterial::ClassName() { return "LightMap"; }
const char* __thiscall FNebulaMaterial::ClassName() { return "Nebula"; }
const char* __thiscall FNebulaTwoMaterial::ClassName() { return "NebulaTwo"; }
const char* __thiscall FNullMaterial::ClassName() { return "NullMaterial"; }

GENRESULT __thiscall FMaterial::InitializeFromDescriptor(void* /*Arg*/)
{
    return GR_OK;
}

GENRESULT __thiscall FAtmosphereMaterial::InitializeFromDescriptor(void* /*Arg*/)
{
    // FUN_06eb2b10.
    bAtmosphereReady = 1;
    return GR_OK;
}

GENRESULT __thiscall FMaterial::ApplyInitialRenderStates(void* Arg)
{
    return GR_OK;
}

GENRESULT __thiscall FMaterial::Setup()
{
    // LAB_06eb2090: texture lookup and alpha-mode detection.
    struct FTextureQuery
    {
        const char* Name;
        unsigned long Crc;
    };
    void* Record; // Retail does not clear this before the lookup.
    for (unsigned long Index = 0; Index < TexCount; ++Index)
    {
        FTexBlock* Block = &TexBlocks[Index];
        ITextureLibrary2* Library = TexLib;
        const int PreviousId = Block->TextureId;
        Block->Handle = 0;
        Block->TextureId = -1;
        Block->bSkipResolve = 1;
        bool bSuppressNotice = true;
        if (Block->Name != nullptr)
        {
            FTextureQuery Query;
            Query.Name = Block->Name;
            Query.Crc = DACOM_CRC::GetCRC32(Block->Name);
            if (Library->FindTextureByCrc(&Query, &Record) >= 0)
            {
                int FrameCount;
                Library->GetFrameCount(Record, &FrameCount);
                Block->bSkipResolve = FrameCount == 1 ? 1 : 0;
                if (Library->CreatePlaybackGroup(Record, &Block->TextureId) >= 0 &&
                    Block->bSkipResolve != 0)
                {
                    Library->GetFrame(Block->TextureId, 0, &Block->Handle);
                }
                Library->ReleaseTextureRecord(Record);
            }
            else
            {
                // Retail compares exactly seven bytes, including the terminating NUL.
                bSuppressNotice = std::strncmp(Block->Name, "(None)", 7) == 0;
            }
        }
        if (PreviousId != -1)
            Library->DecRefById(PreviousId);
        if (!bSuppressNotice)
            FDUMP(kFdumpNotice, "%s(%d) : NOTICE:General:texture verify failed: %s: %s",
                  __FILE__, __LINE__, InstanceName, TexBlocks[Index].Name);
    }
    // Only an ordered Opacity < 1 blends; NaN does not.
    bAlphablend = Opacity < 1.0f ? 1 : 0;
    bAlphatest = 0;
    if (bAlphablend != 0)
        return GR_OK;
    int FormatIndex = 0; // FUN_06ed0b60(0), fixed DAT_06ed51d8 first entry.
    const unsigned long Handle = TexBlocks[0].Handle;
    if (Handle == 0)
        return GR_OK;
    if (Pipeline->GetTextureFormat((void*)Handle, &FormatIndex) < 0)
        return GR_OK;
    bAlphatest = PixelFormatAlphaBits(FormatIndex) == 1 ? 1 : 0;
    const bool bHasAlpha = (FormatIndex >= kTextureFormatDxt1 && FormatIndex <= kTextureFormatDxt5) ||
                           PixelFormatHasAlpha(FormatIndex) != 0;
    bAlphablend = bHasAlpha && bAlphatest == 0 ? 1 : 0;
    return GR_OK;
}

static float ShadingPropertyFloat(unsigned long Bits)
{
    float Value = 0.0f;
    std::memcpy(&Value, &Bits, sizeof(Value));
    return Value;
}

static bool IsShadingPropertyValueWithinRange(unsigned long ValueBits, unsigned long MinimumBits,
                                              unsigned long MaximumBits)
{
    // Compare widened bounds against the stored float without rounding the bounds.
    static const unsigned long kRangeEpsilonBits = 0x34000000;
    const double Value = ShadingPropertyFloat(ValueBits);
    const double Epsilon = ShadingPropertyFloat(kRangeEpsilonBits);
    const double Upper = (double)(ShadingPropertyFloat(MaximumBits)) + Epsilon;
    if (Upper <= Value)
        return false;
    const double Lower = (double)(ShadingPropertyFloat(MinimumBits)) - Epsilon;
    return !(Lower >= Value);
}

bool __thiscall FMaterial::ValueWithinDeclaredRange(unsigned long Index, void* Entry, void* Value)
{
    // Require matching value types before checking the declared range.
    const FShadingPropertyRecord* Record = (const FShadingPropertyRecord*)Entry;
    const FShadingPropertyValue* Typed = (const FShadingPropertyValue*)Value;
    if (Typed->Type != Record->Current.Type)
        return false;

    if (Typed->Type == kMaterialParamFloat)
    {
        return IsShadingPropertyValueWithinRange(Typed->Payload0, Record->Minimum.Payload0,
                                                 Record->Maximum.Payload0);
    }
    if (Typed->Type == kMaterialParamVector3 || Typed->Type == kMaterialParamVector4)
    {
        const unsigned long ComponentCount = Typed->Type == kMaterialParamVector3 ? 3UL : 4UL;
        const unsigned long* ValuePayload = &Typed->Payload0;
        const unsigned long* MinimumPayload = &Record->Minimum.Payload0;
        const unsigned long* MaximumPayload = &Record->Maximum.Payload0;
        for (unsigned long ComponentIndex = 0; ComponentIndex < ComponentCount; ++ComponentIndex)
        {
            if (!IsShadingPropertyValueWithinRange(ValuePayload[ComponentIndex],
                                                   MinimumPayload[ComponentIndex],
                                                   MaximumPayload[ComponentIndex]))
            {
                return false;
            }
        }
    }
    return true;
}

GENRESULT __thiscall FMaterial::StoreParameterValue(unsigned long Index, void* Entry, void* Value)
{
    // FUN_06eb23c0: the target guard covers typed copy only.
    const FShadingPropertyValue* Typed = (const FShadingPropertyValue*)Value;
    const FShadingPropertyRecord* Record = (const FShadingPropertyRecord*)Entry;
    void* Target = GetParameterAddress(Index);
    if (Target != nullptr)
    {
        unsigned long* Words = (unsigned long*)Target;
        switch (Typed->Type)
        {
        case kMaterialParamVector4:
            Words[3] = Typed->Payload3;
            [[fallthrough]];
        case kMaterialParamVector3:
            Words[2] = Typed->Payload2;
            Words[1] = Typed->Payload1;
            [[fallthrough]];
        case kMaterialParamInteger:
        case kMaterialParamFloat:
        case kMaterialParamWrapMode:
        case kMaterialParamAddressMode:
        case 10:
            Words[0] = Typed->Payload0;
            break;
        case kMaterialParamString:
        case kMaterialParamTextureName:
        {
            char** TextTarget = (char**)Target;
            delete[] *TextTarget;
            *TextTarget = nullptr;
            const char* Source = (const char*)(Typed->Payload0);
            if (Source != nullptr)
            {
                const size_t Size = std::strlen(Source) + 1;
                char* Copy = new char[Size];
                *TextTarget = Copy;
                // Retail re-reads the source after storing the allocation.
                std::strcpy(Copy, (const char*)(Typed->Payload0));
            }
            break;
        }
        case kMaterialParamComponent:
        {
            IDAComponent** ComponentTarget = (IDAComponent**)Target;
            IDAComponent* Previous = *ComponentTarget;
            IDAComponent* Component = (IDAComponent*)(Typed->Payload0);
            *ComponentTarget = Component;
            if (Component != nullptr)
                Component->AddRef();
            if (Previous != nullptr)
                Previous->Release();
            break;
        }
        default:
            break;
        }
    }
    if (std::strcmp(Record->Name, "Dc") == 0)
        std::memcpy(Ambient, &DiffuseR, 4 * sizeof(float));
    else if (std::strcmp(Record->Name, "Oc") == 0)
        ApplyOcFlag();
    return GR_OK;
}

GENRESULT __thiscall FDetailMap2PassMaterial::StoreParameterValue(unsigned long Index, void* Entry, void* Value)
{
    // FUN_06eb6c40: the shared store, then the flip flags are normalised to 0 / 1.
    FMaterial::StoreParameterValue(Index, Entry, Value);
    if (Index == kDetailMapFlipU)
        DetailFlipU = (int)DetailFlipU > 0 ? 1UL : 0UL;
    else if (Index == kDetailMapFlipV)
        DetailFlipV = (int)DetailFlipV > 0 ? 1UL : 0UL;
    return GR_OK;
}

GENRESULT __thiscall FDetailMapMaterial::StoreParameterValue(unsigned long Index, void* Entry, void* Value)
{
    // FUN_06eb6c40: the shared store, then the flip flags are normalised to 0 / 1.
    FMaterial::StoreParameterValue(Index, Entry, Value);
    if (Index == kDetailMapFlipU)
        DetailFlipU = (int)DetailFlipU > 0 ? 1UL : 0UL;
    else if (Index == kDetailMapFlipV)
        DetailFlipV = (int)DetailFlipV > 0 ? 1UL : 0UL;
    return GR_OK;
}

GENRESULT __thiscall FTwoDetailMapMaterial::StoreParameterValue(unsigned long Index, void* Entry, void* Value)
{
    // FUN_06ebb3f0: the shared store, then the flip flags are normalised to 0 / 1.
    FMaterial::StoreParameterValue(Index, Entry, Value);
    if (Index == kTwoDetailMapFlipU)
        DetailFlipU = (int)DetailFlipU > 0 ? 1UL : 0UL;
    else if (Index == kTwoDetailMapFlipV)
        DetailFlipV = (int)DetailFlipV > 0 ? 1UL : 0UL;
    return GR_OK;
}

unsigned long __thiscall FNebulaMaterial::GetParameterTable(void** OutBase)
{
    InitializeNebulaPropertySchema();
    if (OutBase != nullptr)
        *OutBase = GNebulaPropertySchema;
    return 4;
}

unsigned long __thiscall FLightMapMaterial::GetParameterTable(void** OutBase)
{
    InitializeLightMapPropertySchema();
    if (OutBase != nullptr)
        *OutBase = GLightMapPropertySchema;
    return 4;
}

unsigned long __thiscall FAtmosphereMaterial::GetParameterTable(void** OutBase)
{
    // LAB_06eb25c0 / 06eb28ce; retail schema at DAT_06ed7ca0.
    FShadingPropertyRecord* Schema = GetAtmospherePropertySchema();
    if (OutBase != nullptr)
        *OutBase = Schema;
    return 9;
}

unsigned long __thiscall FSinglePassMaterial::GetParameterTable(void** OutBase)
{
    FShadingPropertyRecord* Schema = GetSinglePassPropertySchema();
    if (OutBase != nullptr)
        *OutBase = Schema;
    return 10;
}

unsigned long __thiscall FEcEtMaterial::GetParameterTable(void** OutBase)
{
    FShadingPropertyRecord* Schema = GetEcEtPropertySchema();
    if (OutBase != nullptr)
        *OutBase = Schema;
    return 5;
}

unsigned long __thiscall FDcDtEtMaterial::GetParameterTable(void** OutBase)
{
    FShadingPropertyRecord* Schema = GetDcDtEtPropertySchema();
    if (OutBase != nullptr)
        *OutBase = Schema;
    return 9;
}

unsigned long __thiscall FBtDetailMapMaterial::GetParameterTable(void** OutBase)
{
    InitializeBtDetailPropertySchema();
    if (OutBase != nullptr)
        *OutBase = GBtDetailPropertySchema;
    return 13;
}

unsigned long __thiscall FDetailMapMaterial::GetParameterTable(void** OutBase)
{
    InitializeDetailMapPropertySchema();
    if (OutBase != nullptr)
        *OutBase = GDetailMapPropertySchema;
    return 13;
}

unsigned long __thiscall FDetailMap2PassMaterial::GetParameterTable(void** OutBase)
{
    InitializeDetail2PassPropertySchema();
    if (OutBase != nullptr)
        *OutBase = GDetail2PassPropertySchema;
    return 13;
}

unsigned long __thiscall FIllumDetailMapMaterial::GetParameterTable(void** OutBase)
{
    InitializeIllumDetailPropertySchema();
    if (OutBase != nullptr)
        *OutBase = GIllumDetailPropertySchema;
    return 18;
}

unsigned long __thiscall FMasked2DetailMapMaterial::GetParameterTable(void** OutBase)
{
    InitializeMaskedDetailPropertySchema();
    if (OutBase != nullptr)
        *OutBase = GMaskedDetailPropertySchema;
    return 18;
}

unsigned long __thiscall FNullMaterial::GetParameterTable(void** OutBase)
{
    // LAB_06eb3da0: NullMaterial has no properties.
    if (OutBase != nullptr)
        *OutBase = nullptr;
    return 0;
}

void* __thiscall FNullMaterial::GetParameterAddress(unsigned long /*Index*/)
{
    // FUN_06eb1a00.
    return nullptr;
}

void* __thiscall FNebulaMaterial::GetParameterAddress(unsigned long Index)
{
    // LAB_06ebc5a0: actual texture block, not fields of the material itself.
    switch (Index)
    {
    case kNebulaTextureName: return &TexBlocks[0].Name;
    case kNebulaTextureWrap: return &TexBlocks[0].Flags;
    case kNebulaTextureAddressU: return &TexBlocks[0].AddressU;
    case kNebulaTextureAddressV: return &TexBlocks[0].AddressV;
    default: return nullptr;
    }
}

void* __thiscall FLightMapMaterial::GetParameterAddress(unsigned long Index)
{
    // LAB_06ebc5a0: actual texture block, not fields of the material itself.
    switch (Index)
    {
    case kNebulaTextureName: return &TexBlocks[0].Name;
    case kNebulaTextureWrap: return &TexBlocks[0].Flags;
    case kNebulaTextureAddressU: return &TexBlocks[0].AddressU;
    case kNebulaTextureAddressV: return &TexBlocks[0].AddressV;
    default: return nullptr;
    }
}

void* __thiscall FAtmosphereMaterial::GetParameterAddress(unsigned long Index)
{
    switch (Index)
    {
    case kAtmosphereAmbient:
        return Ambient;
    case kAtmosphereDiffuse:
        return &DiffuseR;
    case kAtmosphereFadeTextureName:
        return &TexBlocks[0].Name;
    case kAtmosphereFadeTextureWrap:
        return &TexBlocks[0].Flags;
    case kAtmosphereFadeTextureAddressU:
        return &TexBlocks[0].AddressU;
    case kAtmosphereFadeTextureAddressV:
        return &TexBlocks[0].AddressV;
    case kAtmosphereFadeOff:
        return &FadeOff;
    case kAtmosphereAlpha:
        return &Alpha;
    case kAtmosphereScale:
        return &Scale;
    default:
        return nullptr;
    }
}

void* __thiscall FEcEtMaterial::GetParameterAddress(unsigned long Index)
{
    // LAB_06eb89e0: EcEt's five-record table addresses Emissive at
    // +0x50 and the single Et texture block at +0x18/+1C/+20/+24.
    switch (Index)
    {
    case kEcEtEmissive:
        return Emissive;
    case kEcEtEmissiveTextureName:
        return (char**)(&TexBlocks[0].Name);
    case kEcEtEmissiveTextureWrap:
        return &TexBlocks[0].Flags;
    case kEcEtEmissiveTextureAddressU:
        return &TexBlocks[0].AddressU;
    case kEcEtEmissiveTextureAddressV:
        return &TexBlocks[0].AddressV;
    default:
        return nullptr;
    }
}

void* __thiscall FDcDtEtMaterial::GetParameterAddress(unsigned long Index)
{
    // LAB_06eb5dd0: Dc, then the diffuse and emissive texture blocks.
    switch (Index)
    {
    case kDcDtEtDiffuse:
        return &DiffuseR;
    case kDcDtEtDiffuseTextureName:
        return (char**)(&TexBlocks[0].Name);
    case kDcDtEtDiffuseTextureWrap:
        return &TexBlocks[0].Flags;
    case kDcDtEtDiffuseTextureAddressU:
        return &TexBlocks[0].AddressU;
    case kDcDtEtDiffuseTextureAddressV:
        return &TexBlocks[0].AddressV;
    case kDcDtEtEmissiveTextureName:
        return (char**)(&TexBlocks[1].Name);
    case kDcDtEtEmissiveTextureWrap:
        return &TexBlocks[1].Flags;
    case kDcDtEtEmissiveTextureAddressU:
        return &TexBlocks[1].AddressU;
    case kDcDtEtEmissiveTextureAddressV:
        return &TexBlocks[1].AddressV;
    default:
        return nullptr;
    }
}

void* __thiscall FBtDetailMapMaterial::GetParameterAddress(unsigned long Index)
{
    switch (Index)
    {
    case kBtDetailAmbient: return Ambient;
    case kBtDetailDiffuse: return &DiffuseR;
    case kBtDetailEmissive: return Emissive;
    case kBtDetailSpecular: return Specular;
    case kBtDetailSpecularPower: return &SpecularPower;
    case kBtDetailOpacity: return &Opacity;
    case kBtDetailTextureName: return &TexBlocks[0].Name;
    case kBtDetailTextureWrap: return &TexBlocks[0].Flags;
    case kBtDetailTextureAddressU: return &TexBlocks[0].AddressU;
    case kBtDetailTextureAddressV: return &TexBlocks[0].AddressV;
    case kBtDetailDetailTextureName: return &TexBlocks[1].Name;
    case kBtDetailDetailTextureWrap: return &TexBlocks[1].Flags;
    case kBtDetailDetailTextureAddressU: return &TexBlocks[1].AddressU;
    case kBtDetailDetailTextureAddressV: return &TexBlocks[1].AddressV;
    default: return nullptr;
    }
}

void* __thiscall FDetailMap2PassMaterial::GetParameterAddress(unsigned long Index)
{
    // LAB_06eb6b80, shared with DetailMapMaterial.
    switch (Index)
    {
    case kDetailMapAmbient: return Ambient;
    case kDetailMapDiffuse: return &DiffuseR;
    case kDetailMapDiffuseTextureName: return &TexBlocks[0].Name;
    case kDetailMapDiffuseTextureWrap: return &TexBlocks[0].Flags;
    case kDetailMapDiffuseTextureAddressU: return &TexBlocks[0].AddressU;
    case kDetailMapDiffuseTextureAddressV: return &TexBlocks[0].AddressV;
    case kDetailMapFlipU: return &DetailFlipU;
    case kDetailMapFlipV: return &DetailFlipV;
    case kDetailMapDetailTextureName: return &TexBlocks[1].Name;
    case kDetailMapDetailTextureWrap: return &TexBlocks[1].Flags;
    case kDetailMapDetailTextureAddressU: return &TexBlocks[1].AddressU;
    case kDetailMapDetailTextureAddressV: return &TexBlocks[1].AddressV;
    case kDetailMapTileRate: return &DetailTileRate0;
    default: return nullptr;
    }
}

void* __thiscall FDetailMapMaterial::GetParameterAddress(unsigned long Index)
{
    // LAB_06eb6b80, shared with DetailMap2Dm1Msk2PassMaterial.
    switch (Index)
    {
    case kDetailMapAmbient: return Ambient;
    case kDetailMapDiffuse: return &DiffuseR;
    case kDetailMapDiffuseTextureName: return &TexBlocks[0].Name;
    case kDetailMapDiffuseTextureWrap: return &TexBlocks[0].Flags;
    case kDetailMapDiffuseTextureAddressU: return &TexBlocks[0].AddressU;
    case kDetailMapDiffuseTextureAddressV: return &TexBlocks[0].AddressV;
    case kDetailMapFlipU: return &DetailFlipU;
    case kDetailMapFlipV: return &DetailFlipV;
    case kDetailMapDetailTextureName: return &TexBlocks[1].Name;
    case kDetailMapDetailTextureWrap: return &TexBlocks[1].Flags;
    case kDetailMapDetailTextureAddressU: return &TexBlocks[1].AddressU;
    case kDetailMapDetailTextureAddressV: return &TexBlocks[1].AddressV;
    case kDetailMapTileRate: return &DetailTileRate0;
    default: return nullptr;
    }
}

void* __thiscall FTwoDetailMapMaterial::GetParameterAddress(unsigned long Index)
{
    // LAB_06ebb2e0: all 18 targets, including the distinct third texture block.
    switch (Index)
    {
    case kTwoDetailMapAmbient:
        return Ambient;
    case kTwoDetailMapDiffuse:
        return &DiffuseR;
    case kTwoDetailMapDiffuseTextureName:
        return &TexBlocks[0].Name;
    case kTwoDetailMapDiffuseTextureWrap:
        return &TexBlocks[0].Flags;
    case kTwoDetailMapDiffuseTextureAddressU:
        return &TexBlocks[0].AddressU;
    case kTwoDetailMapDiffuseTextureAddressV:
        return &TexBlocks[0].AddressV;
    case kTwoDetailMapFlipU:
        return &DetailFlipU;
    case kTwoDetailMapFlipV:
        return &DetailFlipV;
    case kTwoDetailMapAlpha0TextureName:
        return &TexBlocks[1].Name;
    case kTwoDetailMapAlpha0TextureWrap:
        return &TexBlocks[1].Flags;
    case kTwoDetailMapAlpha0TextureAddressU:
        return &TexBlocks[1].AddressU;
    case kTwoDetailMapAlpha0TextureAddressV:
        return &TexBlocks[1].AddressV;
    case kTwoDetailMapTileRate0:
        return &DetailTileRate0;
    case kTwoDetailMapAlpha1TextureName:
        return &TexBlocks[2].Name;
    case kTwoDetailMapAlpha1TextureWrap:
        return &TexBlocks[2].Flags;
    case kTwoDetailMapAlpha1TextureAddressU:
        return &TexBlocks[2].AddressU;
    case kTwoDetailMapAlpha1TextureAddressV:
        return &TexBlocks[2].AddressV;
    case kTwoDetailMapTileRate1:
        return &DetailTileRate1;
    default:
        return nullptr;
    }
}

void* __thiscall FSinglePassMaterial::GetParameterAddress(unsigned long Index)
{
    // LAB_06ebd770: ten SinglePass targets; unsigned out-of-range returns null.
    switch (Index)
    {
    case kSinglePassAmbient:
        return Ambient;
    case kSinglePassDiffuse:
        return &DiffuseR;
    case kSinglePassEmissive:
        return Emissive;
    case kSinglePassSpecular:
        return Specular;
    case kSinglePassSpecularPower:
        return &SpecularPower;
    case kSinglePassOpacity:
        return &Opacity;
    case kSinglePassTextureName:
        return (char**)(&TexBlocks[0].Name);
    case kSinglePassTextureWrap:
        return &TexBlocks[0].Flags;
    case kSinglePassTextureAddressU:
        return &TexBlocks[0].AddressU;
    case kSinglePassTextureAddressV:
        return &TexBlocks[0].AddressV;
    default:
        return nullptr;
    }
}

void FMaterial::ApplyOcFlag()
{
    // Run by FUN_06eb6c40 / FUN_06eb23c0 after a store to "Oc".
    const bool bOpacityBlend = (Opacity < 1.0f);
    bAlphablend = bOpacityBlend ? 1 : 0;
    bAlphatest = 0;
    if (bOpacityBlend)
        return;
    const unsigned long Handle = TexBlocks[0].Handle;
    if (Handle == 0)
        return;
    // FUN_06ed0b60(0) = 0; GetTextureFormat (pipeline +0xC8) overwrites it.
    int FormatIndex = 0;
    const GENRESULT Result = Pipeline->GetTextureFormat((void*)Handle, &FormatIndex);
    if (Result < 0)
        return;
    bAlphatest = (PixelFormatAlphaBits(FormatIndex) == 1) ? 1 : 0;
    const bool bDxtRange =
        (FormatIndex >= kDxtFormatIndexMin && FormatIndex <= kDxtFormatIndexMax);
    const bool bHasAlpha = bDxtRange || (PixelFormatHasAlpha(FormatIndex) != 0);
    if (bHasAlpha && bAlphatest == 0)
        bAlphablend = 1;
    else
        bAlphablend = 0;
}

FBatchedFace::FBatchedFace() {}

FBatchedMaterial* FBatchedFace::OwnerFromThis()
{
    return (FBatchedMaterial*)((unsigned char*)this -
                                               offsetof(FBatchedMaterial, Batched));
}

GENRESULT DACOM_API FBatchedFace::QueryInterface(const char* Iid, void** Out)
{
    FBatchedMaterial* Owner = OwnerFromThis();
    if (Owner == nullptr)
        return GR_INTERFACE_UNSUPPORTED;
    return Owner->QueryInterface(Iid, Out);
}

unsigned long DACOM_API FBatchedFace::AddRef()
{
    FBatchedMaterial* Owner = OwnerFromThis();
    return Owner->AddRef();
}

unsigned long DACOM_API FBatchedFace::Release()
{
    FBatchedMaterial* Owner = OwnerFromThis();
    return Owner->Release();
}

GENRESULT DACOM_API FBatchedFace::CreateInnerMaterial(void* Name)
{
    // LAB_06eb4380: retained controller and borrowed public material are distinct.
    FBatchedMaterial* Owner = OwnerFromThis();
    if (Owner->InnerController != nullptr)
    {
        Owner->InnerController->Release();
        Owner->InnerController = nullptr;
    }
    FMaterialAggregateDesc Desc = {};
    Desc.Size = sizeof(Desc);
    Desc.NameAt4 = (const char*)Name;
    Desc.OuterAt8 = (IBatchedMaterial*)this;
    Desc.InnerOutAtC = (void**)(&Owner->InnerController);
    Desc.ExtraAt10 = nullptr;
    void* Created = nullptr;
    const GENRESULT Result = DACOM_Acquire()->CreateInstance((AGGDESC*)(&Desc), &Created);
    if (Result >= 0)
    {
        if (Owner->InnerController->QueryInterface(Iids::Material,
                                                   (void**)(&Owner->Inner)) >= 0)
        {
            // Public QI added a reference to this wrapper; retain only inner controller.
            Owner->Inner->Release();
        }
        else if (Created != nullptr)
        {
            ((IDAComponent*)Created)->Release();
        }
    }
    else
    {
        char Message[8192];
        std::sprintf(Message, "Batched material references invalid super type (%s)\n",
                     (const char*)Name);
        FDUMP(kFdumpNotice, "%s(%d) : NOTICE:General:%s",
              "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Shading\\Materials\\BatchedMaterial.cpp", 0x159,
              Message);
    }
    return GR_OK;
}

GENRESULT DACOM_API FBatchedFace::SetBatchBin(void* Value)
{
    FBatchedMaterial* Owner = OwnerFromThis();
    Owner->BatchBin = (unsigned long)Value;
    return GR_OK;
}

GENRESULT DACOM_API FBatchedFace::GetBatchBin(void** Out)
{
    FBatchedMaterial* Owner = OwnerFromThis();
    *Out = (void*)(Owner->BatchBin);
    return GR_OK;
}

GENRESULT DACOM_API FBatchedFace::Bind(void* Context)
{
    IMaterial* Inner = OwnerFromThis() != nullptr ? OwnerFromThis()->InnerMaterial() : nullptr;
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->Bind(Context);
}

GENRESULT DACOM_API FBatchedFace::Teardown(void* Context)
{
    IMaterial* Inner = OwnerFromThis() != nullptr ? OwnerFromThis()->InnerMaterial() : nullptr;
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->Teardown(Context);
}

GENRESULT DACOM_API FBatchedFace::Submit(void* Context, unsigned long PrimType, unsigned long Start,
                                        unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                                        unsigned long Flags)
{
    IMaterial* Inner = OwnerFromThis()->InnerMaterial();
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->Submit(Context, PrimType, Start, Count, IbStart, IndexSpan, Flags);
}

FBatchedMaterial::FBatchedMaterial()
    : BatchBin(0xFFFFFFFF), InnerController(nullptr), Inner(nullptr), Batcher(nullptr), RefCount(1), Outer(nullptr)
{
}

FBatchedMaterial::~FBatchedMaterial() noexcept(false)
{
    // FUN_06eb3e50: release the factory controller, then clear the borrowed face.
    if (InnerController != nullptr)
    {
        InnerController->Release();
        InnerController = nullptr;
    }
    Inner = nullptr;
}

IMaterial* FBatchedMaterial::InnerMaterial() const
{
    return Inner;
}

static_assert(offsetof(FBatchedMaterial, Batched) == 0x04, "IBatchedMaterial +4");
static_assert(offsetof(FBatchedMaterial, BatchBin) == 0x08, "BatchBin +8");
static_assert(offsetof(FBatchedMaterial, InnerController) == 0x0C, "retained controller +C");
static_assert(offsetof(FBatchedMaterial, Inner) == 0x10, "inner IMaterial +10");
static_assert(offsetof(FBatchedMaterial, Batcher) == 0x14, "batcher +14");

FBatchedMaterial* FBatchedController::OwnerFromThis()
{
    return (FBatchedMaterial*)((unsigned char*)this -
                                               offsetof(FBatchedMaterial, Controller));
}

GENRESULT DACOM_API FBatchedController::QueryInterface(const char* Iid, void** Out)
{
    // LAB_06eb4640: interface table DAT_06ed1320 (IMaterial +0, IBatchedMaterial +4).
    FBatchedMaterial* Owner = OwnerFromThis();
    IDAComponent* Face;
    if (std::strcmp(Iid, Iids::Material) == 0)
        Face = (IMaterial*)Owner;
    else if (std::strcmp(Iid, Iids::BatchedMaterial) == 0)
        Face = (IBatchedMaterial*)(&Owner->Batched);
    else
    {
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }
    Face->AddRef();
    *Out = Face;
    return GR_OK;
}

unsigned long DACOM_API FBatchedController::AddRef()
{
    return ++OwnerFromThis()->RefCount;
}

unsigned long DACOM_API FBatchedController::Release()
{
    FBatchedMaterial* Owner = OwnerFromThis();
    if (Owner->RefCount != 0)
        --Owner->RefCount;
    const unsigned long Remaining = Owner->RefCount;
    if (Remaining == 0)
    {
        Owner->RefCount = 1;
        void* Storage = Owner;
        Owner->~FBatchedMaterial();
        // The destructor has already run; release only its storage.
        ::operator delete(Storage);
    }
    return Remaining;
}

GENRESULT FBatchedMaterial::InitializeFromDescriptor(FMaterialAggregateDesc* Desc)
{
    // FUN_06eb3e80; the factory (06eb4530) calls it after publishing the controller.
    InnerController = nullptr;
    Inner = nullptr;
    BatchBin = 0xFFFFFFFFu;
    return GR_OK;
}

void FBatchedMaterial::ConfigureAggregation(FMaterialAggregateDesc* Desc)
{
    Outer = Desc->OuterAt8;
    if (Outer != nullptr)
        *Desc->InnerOutAtC = (IDAComponent*)(&Controller);
}

GENRESULT DACOM_API FBatchedMaterial::QueryInterface(const char* Iid, void** Out)
{
    return Outer != nullptr ? Outer->QueryInterface(Iid, Out) : Controller.QueryInterface(Iid, Out);
}

unsigned long DACOM_API FBatchedMaterial::AddRef()
{
    return Outer != nullptr ? Outer->AddRef() : Controller.AddRef();
}

unsigned long DACOM_API FBatchedMaterial::Release()
{
    return Outer != nullptr ? Outer->Release() : Controller.Release();
}

GENRESULT DACOM_API FBatchedMaterial::InnerQueryInterface(const char* Iid, void** Out)
{
    return Controller.QueryInterface(Iid, Out);
}
unsigned long DACOM_API FBatchedMaterial::InnerAddRef()
{
    return Controller.AddRef();
}
unsigned long DACOM_API FBatchedMaterial::InnerRelease()
{
    return Controller.Release();
}

GENRESULT DACOM_API FBatchedMaterial::Initialize(IDAComponent* Component)
{
    // FUN_06eb3ea0: direct out-parameter writes persist even on failed QI.
    if (Component->QueryInterface(Iids::MaterialBatcher, &Batcher) < 0)
    {
        FDUMP(kFdumpError, "%s(%d) : ERROR:General:%s",
              "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Shading\\Materials\\BatchedMaterial.cpp", 0x77,
              "BatchedMaterial: initialize: unable to acquire IID_IMaterialBatcher");
        return GR_GENERIC;
    }
    ((IDAComponent*)Batcher)->Release();
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->Initialize(Component);
}

GENRESULT DACOM_API FBatchedMaterial::LoadFromFileSystem(void* FileSystem)
{
    if (Inner == nullptr)
        return GR_GENERIC;
    if (!ReadNamedBytes(FileSystem, "Bin", &BatchBin, sizeof(BatchBin)))
        BatchBin = 0xFFFFFFFF;
    return Inner->LoadFromFileSystem(FileSystem);
}

GENRESULT DACOM_API FBatchedMaterial::SaveProperties(void* FileSystem)
{
    if (Inner == nullptr)
        return GR_GENERIC;
    if (!WriteNamedBytes(FileSystem, "Bin", &BatchBin, sizeof(BatchBin)))
        BatchBin = 0xFFFFFFFF;
    return Inner->SaveProperties(FileSystem);
}

GENRESULT DACOM_API FBatchedMaterial::ApplyVerify(void* Context, void* Extra)
{
    if (Inner == nullptr)
        return GR_GENERIC;
    const GENRESULT Result = Inner->ApplyVerify(Context, Extra);
    // FUN_06eb40f0 caches bit 1 of the low render-flags byte after the first
    // successful verification; BatchBin == -1 means not yet cached.
    if (BatchBin == 0xFFFFFFFF && Result >= 0)
    {
        unsigned long Flags = 0;
        Inner->GetRenderFlags(&Flags);
        BatchBin = ((unsigned char)Flags >> 1) & 1;
    }
    return Result;
}

GENRESULT DACOM_API FBatchedMaterial::ResolveTextures(float Delta)
{
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->ResolveTextures(Delta);
}

GENRESULT DACOM_API FBatchedMaterial::Bind(void* Context)
{
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->Bind(Context);
}

GENRESULT DACOM_API FBatchedMaterial::Teardown(void* Context)
{
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->Teardown(Context);
}

GENRESULT DACOM_API FBatchedMaterial::Submit(void* Context, unsigned long PrimType, unsigned long Start, unsigned long Count, unsigned long IbStart, unsigned long IndexSpan, unsigned long Flags)
{
    // LAB_06eb4180: only the inner material is optional; the batcher is initialized.
    if (Inner == nullptr)
        return GR_GENERIC;

    ((IMaterialBatcher2*)Batcher)->SelectBin(BatchBin);

    unsigned long DepthBits = 0;
    if ((Flags & kMaterialSubmitComputeDepth) != 0)
    {
        // Retail captures both pointers before the lazy math-engine creation.
        const FMaterialSubmitContext* SubmitContext = (const FMaterialSubmitContext*)Context;
        const Transform* View = SubmitContext->View;
        const Vector* Translation = &SubmitContext->World->Translation;
        static I3DMathEngine* GBatchedMathEngine = nullptr; // retail DAT_06ed56c8
        Flags |= kMaterialSubmitSortDepth;
        if (GBatchedMathEngine == nullptr)
            GBatchedMathEngine = AcquireMathEngine();
        Vector Transformed;
        GBatchedMathEngine->TransformXform(Transformed, *View, *Translation);
        const float Depth = -Transformed.z;
        std::memcpy(&DepthBits, &Depth, sizeof(Depth));
    }
    // Retail re-reads Batcher and Inner after the math call.
    return ((IMaterialBatcher2*)Batcher)->Submit(Inner, Context, PrimType, Start, Count, IbStart, IndexSpan,
                                                         (void*)DepthBits, Flags);
}

GENRESULT DACOM_API FBatchedMaterial::SetName(const char* Name)
{
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->SetName(Name);
}

GENRESULT DACOM_API FBatchedMaterial::GetName(char* Out, unsigned long Size)
{
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->GetName(Out, Size);
}

GENRESULT DACOM_API FBatchedMaterial::CopyClassName(char* Out, unsigned long Size)
{
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->CopyClassName(Out, Size);
}

GENRESULT DACOM_API FBatchedMaterial::GetPassCount(int* Out)
{
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->GetPassCount(Out);
}

GENRESULT DACOM_API FBatchedMaterial::GetRenderFlags(unsigned long* Out)
{
    if (Inner == nullptr)
        return GR_GENERIC;
    unsigned long Value = 0;
    if (Inner->GetRenderFlags(&Value) < GR_OK)
        return GR_GENERIC;
    *Out = Value | kMaterialRenderBatched;
    return GR_OK;
}

GENRESULT DACOM_API FBatchedMaterial::GetParameterCount(unsigned long* OutCount)
{
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->GetParameterCount(OutCount);
}

GENRESULT DACOM_API FBatchedMaterial::GetParameterDescriptor(unsigned long Index, void* OutDesc)
{
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->GetParameterDescriptor(Index, OutDesc);
}

GENRESULT DACOM_API FBatchedMaterial::GetParameterValue(unsigned long Index, void* InOutDesc)
{
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->GetParameterValue(Index, InOutDesc);
}

GENRESULT DACOM_API FBatchedMaterial::SetParameterValue(unsigned long Index, void* Value)
{
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->SetParameterValue(Index, Value);
}

const char* __thiscall FBatchedMaterial::ClassName()
{
    if (Inner != nullptr)
        return Inner->ClassName();
    return "BatchedMaterial";
}

void* __thiscall FBatchedMaterial::ScalarDeletingDestructor(unsigned char bDelete)
{
    if ((bDelete & 1) != 0)
    {
        delete this;
        return nullptr;
    }
    return this;
}

GENRESULT __thiscall FBatchedMaterial::InitializeFromDescriptor(void* Arg)
{
    return GR_OK;
}

GENRESULT __thiscall FBatchedMaterial::ApplyInitialRenderStates(void* Arg)
{
    return GR_OK;
}

GENRESULT __thiscall FBatchedMaterial::Setup()
{
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->Setup();
}

bool __thiscall FBatchedMaterial::ValueWithinDeclaredRange(unsigned long Index, void* Entry, void* Value)
{
    if (Inner == nullptr)
        return false;
    return Inner->ValueWithinDeclaredRange(Index, Entry, Value);
}

GENRESULT __thiscall FBatchedMaterial::StoreParameterValue(unsigned long Index, void* Entry, void* Value)
{
    if (Inner == nullptr)
        return GR_GENERIC;
    return Inner->StoreParameterValue(Index, Entry, Value);
}

unsigned long __thiscall FBatchedMaterial::GetParameterTable(void** OutBase)
{
    if (OutBase != nullptr)
        *OutBase = nullptr;
    return 0;
}

void* __thiscall FBatchedMaterial::GetParameterAddress(unsigned long Index)
{
    return nullptr;
}
