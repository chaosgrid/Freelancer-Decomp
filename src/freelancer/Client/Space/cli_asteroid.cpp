// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Space/cli_asteroid.h"

#include "Space/asteroid_billboard.h"
#include "Space/cli_asteroid_dynamic.h"
#include "Space/cli_asteroid_med.h"
#include "Space/cli_nebula.h" // G_CloudsMath (DAT_00679860), read by the band rings (FUN_0054c8b0)
#include "DALib.h"
#include "common_archetype.h"
#include "common_csys.h"
#include "common_id.h"
#include "common_misc.h"
#include "common_net.h"
#include "common_universe.h"
#include "dacom_iids.h"
#include "Common/engine_light.h"
#include "Core/game_options.h"
#ifdef FREELANCER_RP9
#include "Space/nebula_volume.h" // NebulaVolume::AsteroidSpawnReach (port-only Direct3D 9 nebula)
#endif
#include "render/RenderPipeline.h"
#include "render/mesh_iface.h"
#include "render/rendcomp.h"
#include "render/shading.h"
#include "render/slots.h"      // Render::EPipelineState
#include "Space/Cameras/space_camera.h"
#include "Space/space_scene_obj_draw.h"
#include "Space/star_sys.h"
#include "Interface/win3d_immediate.h"
#include "Interface/win3d_render.h"
#include "x86math.h"

#include <list>
#include <math.h>
#include <new>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <windows.h>

// The inlined AsteroidExclusionZoneMgr::set_max_cliasteroid_exclusion_distance of FUN_00524880
// and FUN_00520850: the unrounded distance is squared and rounded to float once. Befriended by
// the class (common_net.h).
struct FCliAsteroidExclusionInline
{
    static void SetMaxCliDist(Exclusion::AsteroidExclusionZoneMgr* Mgr, double Dist)
    {
        Mgr->MaxCliDist = static_cast<float>(Dist * Dist);
    }

#ifdef FREELANCER_RP9
    // Port-only (CliAsteroidTickOne's extended spawn distance under the volumetric nebula):
    // the squared value as stored, read and put back around one near-field draw.
    static float GetMaxCliDistSq(Exclusion::AsteroidExclusionZoneMgr const* Mgr)
    {
        return Mgr->MaxCliDist;
    }

    static void SetMaxCliDistSq(Exclusion::AsteroidExclusionZoneMgr* Mgr, float DistSq)
    {
        Mgr->MaxCliDist = DistSq;
    }
#endif
};

// DAT_005dc374 / DAT_005dc394, copied from the PE.
unsigned const kRsAsteroidOpaque[7] = {D3DRS_LIGHTING, 1, D3DRS_SPECULARENABLE, 0, D3DRS_ALPHABLENDENABLE, 0, 0};
unsigned const kRsAsteroidBlend[11] = {D3DRS_LIGHTING, 1, D3DRS_SPECULARENABLE, 0, D3DRS_ALPHABLENDENABLE, 1,
                                       D3DRS_SRCBLEND, D3DBLEND_SRCALPHA, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA, 0};

namespace
{
    // Retail DAT_00613844 — graphics/detail scalar (default 1.0f in .data).
    float& AsteroidDetailScalar()
    {
        static float s_Detail = 1.0f;
        return s_Detail;
    }

    // DAT_006753c0 / c4 / c8 — a VC6 std::list<CliAsteroid*> (allocator byte, head node, size):
    // XC LAB_0051fe10 (initializer slot .data:0060fa74) builds the self-linked head
    // (operator new(0xc)) with size 0 and registers LAB_0051fe50 with atexit to free the nodes.
    // Fields are appended at the tail and walked head->next forward.
    //
    // LIFETIME. The list is a namespace-scope static on purpose, not a function-local one: it
    // is constructed during CRT start-up, like the XC initializer, so its destructor is
    // registered with atexit before WinMain registers ClientAtExitTeardown (LAB_005b2190).
    // atexit runs in reverse order, so the teardown's SpaceApp::Shutdown -> StarSysLeave ->
    // CliAsteroidDestroyAll still sees a live list. A function-local static would be
    // constructed in game, after that registration, and so destroyed before the teardown.
    std::list<FCliAsteroid*> GCliAsteroidFields;

    std::list<FCliAsteroid*>& Registry()
    {
        return GCliAsteroidFields;
    }

    void CliAsteroidFillMeshDesc(FRpVertexSrcDesc* Desc, void* Verts, unsigned short VertCount);
    void CliAsteroidMergeCubeMesh(FCliAsteroid* Field, int Lod, char const* FieldIniRel);

    // The tint clamp FUN_0054d970 / FUN_0054fee0 inline six times (1.0f at DAT_005c75dc): only
    // "less" keeps V; greater, equal and NaN store 1.0. There is no lower bound.
    float ClampTintUpper(float V)
    {
        if (V < 1.0f)
            return V;
        return 1.0f;
    }

    int FloatToIntTrunc(float V)
    {
        return static_cast<int>(V);
    }

    // ---- Band (FUN_0054c580 / 0054c630 / 0054d970 / 0054dab0) ----

    // FUN_0054c580 (thiscall on the band), in retail store order. It writes nothing else:
    // AmbientIntensity, bLit, UpdateCount and the pad bytes after bOffsetPercent keep whatever
    // the storage held -- BandParseIni sets AmbientIntensity / RenderParts / bLit / UpdateCount
    // when a section is read.
    void InitBand(FAsteroidBand* Band)
    {
        Band->TintedDiffuse[0] = 1.0f;
        Band->TintedDiffuse[1] = 1.0f;
        Band->TintedDiffuse[2] = 1.0f;
        Band->TintedAmbient[0] = 1.0f;
        Band->TintedAmbient[1] = 1.0f;
        Band->TintedAmbient[2] = 1.0f;
        Band->ColorShift[0] = 1.0f;
        Band->ColorShift[1] = 1.0f;
        Band->ColorShift[2] = 1.0f;
        Band->AmbientIncrease[0] = 0.0f;
        Band->AmbientIncrease[1] = 0.0f;
        Band->AmbientIncrease[2] = 0.0f;
        Band->Shape = nullptr;
        Band->Zone = nullptr;
        Band->Particles = nullptr;
        Band->ParticleCount = 0;
        Band->Height = 1000.0f;
        Band->FadeDist[0] = 1500.0f;
        Band->FadeDist[1] = 2500.0f;
        Band->FadeDist[2] = 100000.0f;
        Band->FadeDist[3] = 130000.0f;
        Band->Offset = 0.0f;
        Band->bOffsetPercent = 0;
        Band->TextureAspect = 1.0f;
        Band->VertIncrease = 1;
        Band->Pieces = nullptr;
        Band->RenderParts = 0;
        Band->CullMode = 0;
        Band->FirstPiece = 0;
        Band->MaxAlpha = 1.0f;
    }

    // FUN_0054d970(band, diffuse_tint, ambient_tint)
    void BandApplyTints(FAsteroidBand* Band, float const* Diffuse, float const* Ambient)
    {
        float const* const Cs = Band->ColorShift;
        Band->TintedDiffuse[0] = ClampTintUpper(Cs[0] * Diffuse[0]);
        Band->TintedDiffuse[1] = ClampTintUpper(Cs[1] * Diffuse[1]);
        Band->TintedDiffuse[2] = ClampTintUpper(Cs[2] * Diffuse[2]);
        Band->TintedAmbient[0] = ClampTintUpper(Cs[0] * Ambient[0]);
        Band->TintedAmbient[1] = ClampTintUpper(Cs[1] * Ambient[1]);
        Band->TintedAmbient[2] = ClampTintUpper(Cs[2] * Ambient[2]);
    }

    // FUN_0054dab0
    void BandSetAmbientIncrease(FAsteroidBand* Band, float const* Rgb)
    {
        Band->AmbientIncrease[0] = Rgb[0];
        Band->AmbientIncrease[1] = Rgb[1];
        Band->AmbientIncrease[2] = Rgb[2];
    }

    // FUN_0054c8b0 — the particle ring and its pieces (thiscall on the band).
    void BandBuildExclusionRings(FAsteroidBand* Band);

    // FUN_0054c630 — [Band] / ExclusionBand value keys into the band; `shape` is looked up in
    // the field's own ShapeDB through FUN_00512660 (cmn\ShapeDB.h:0xf2 on a miss).
    void BandParseIni(FAsteroidBand* Band, INI_Reader& Reader, FShapeDB* Shapes)
    {
        Band->bLit = 0; // the pad bytes after it untouched
        Band->UpdateCount = 0;
        Band->AmbientIntensity = 1.0f;
        Band->RenderParts = 1;

        if (!Reader.read_value())
            return;

        do
        {
            if (Reader.is_value("shape"))
            {
                char const* Name = Reader.get_value_string();
                Band->Shape = Shapes->Find(Name);
            }
            else if (Reader.is_value("texture_aspect"))
            {
                // 1.15 as a double (DAT_005e0ac0), rounded to float once.
                Band->TextureAspect =
                    static_cast<float>(static_cast<double>(Reader.get_value_float(0)) * 1.15);
            }
            else if (Reader.is_value("height"))
            {
                Band->Height = Reader.get_value_float(0);
            }
            else if (Reader.is_value("offset_dist"))
            {
                Band->bOffsetPercent = 0;
                Band->Offset = Reader.get_value_float(0);
            }
            else if (Reader.is_value("offset_percent"))
            {
                Band->bOffsetPercent = 1;
                Band->Offset = Reader.get_value_float(0);
            }
            else if (Reader.is_value("color_shift"))
            {
                Band->ColorShift[0] = Reader.get_value_float(0);
                Band->ColorShift[1] = Reader.get_value_float(1);
                Band->ColorShift[2] = Reader.get_value_float(2);
            }
            else if (Reader.is_value("fade"))
            {
                Band->FadeDist[0] = Reader.get_value_float(0);
                Band->FadeDist[1] = Reader.get_value_float(1);
                Band->FadeDist[2] = Reader.get_value_float(2);
                Band->FadeDist[3] = Reader.get_value_float(3);
            }
            else if (Reader.is_value("vert_increase"))
            {
                int V = FloatToIntTrunc(Reader.get_value_float(0));
                if (V < 1)
                    V = 1;
                Band->VertIncrease = V;
            }
            else if (Reader.is_value("render_parts"))
            {
                int V = FloatToIntTrunc(Reader.get_value_float(0));
                if (V < 1)
                    V = 1;
                Band->RenderParts = V;
            }
            else if (Reader.is_value("cull_mode"))
            {
                Band->CullMode = FloatToIntTrunc(Reader.get_value_float(0));
            }
            else if (Reader.is_value("ambient_intensity"))
            {
                Band->AmbientIntensity = Reader.get_value_float(0);
            }
        } while (Reader.read_value());
    }

    // FUN_0054c600 -- free the band's particles and pieces; both pointers and both counts
    // cleared. No null test of the band, as retail: the callers (the field destructor and
    // BandVectorClear after a null test, and the embedded +0xfc band) never pass null.
    void BandFreeRingMesh(FAsteroidBand* Band)
    {
        operator delete(Band->Particles);
        Band->Particles = nullptr;
        Band->ParticleCount = 0;
        operator delete(Band->Pieces);
        Band->Pieces = nullptr;
        Band->RenderParts = 0;
    }

    // Inlined in FUN_00520850: every non-null exclusion band freed, then the vector erased
    // (end = begin). The storage is kept.
    void BandVectorClear(FCliAsteroid* This)
    {
        if (This->BandBegin != nullptr && This->BandEnd - This->BandBegin > 0)
        {
            for (FAsteroidBand** It = This->BandBegin; It != This->BandEnd; ++It)
            {
                FAsteroidBand* const Band = *It;
                if (Band != nullptr)
                {
                    BandFreeRingMesh(Band);
                    operator delete(Band);
                }
            }
            This->BandEnd = This->BandBegin;
        }
    }

    void BandVectorPush(FCliAsteroid* This, FAsteroidBand* Band)
    {
        size_t Count = 0;
        size_t Cap = 0;
        if (This->BandBegin != nullptr)
        {
            Count = static_cast<size_t>(This->BandEnd - This->BandBegin);
            Cap = static_cast<size_t>(This->BandCap - This->BandBegin);
        }
        if (Count + 1 > Cap)
        {
            size_t NewCap = Cap == 0 ? 4 : Cap * 2;
            FAsteroidBand** NewBuf =
                static_cast<FAsteroidBand**>(operator new(sizeof(FAsteroidBand*) * NewCap));
            if (This->BandBegin != nullptr && Count != 0)
                memcpy(NewBuf, This->BandBegin, Count * sizeof(FAsteroidBand*));
            operator delete(This->BandBegin);
            This->BandBegin = NewBuf;
            This->BandEnd = NewBuf + Count;
            This->BandCap = NewBuf + NewCap;
        }
        *This->BandEnd = Band;
        ++This->BandEnd;
    }

    // ---- FCliDynamicAsteroid vector (the manager itself is in cli_asteroid_dynamic.cpp) ----

    void DynVectorPush(FCliAsteroid* This, FCliDynamicAsteroid* Dyn)
    {
        size_t Count = 0;
        size_t Cap = 0;
        if (This->DynBegin != nullptr)
        {
            Count = static_cast<size_t>(This->DynEnd - This->DynBegin);
            Cap = static_cast<size_t>(This->DynCap - This->DynBegin);
        }
        if (Count + 1 > Cap)
        {
            size_t NewCap = Cap == 0 ? 4 : Cap * 2;
            void** NewBuf = static_cast<void**>(operator new(sizeof(void*) * NewCap));
            if (This->DynBegin != nullptr && Count != 0)
                memcpy(NewBuf, This->DynBegin, Count * sizeof(void*));
            operator delete(This->DynBegin);
            This->DynBegin = NewBuf;
            This->DynEnd = NewBuf + Count;
            This->DynCap = NewBuf + NewCap;
        }
        *This->DynEnd = Dyn;
        ++This->DynEnd;
    }

    // Inlined in FUN_00520850: every non-null dynamic asteroid manager destroyed and freed,
    // then the vector erased (end = begin). The storage is kept.
    void DynVectorClear(FCliAsteroid* This)
    {
        if (This->DynBegin != nullptr && This->DynEnd - This->DynBegin > 0)
        {
            for (void** It = This->DynBegin; It != This->DynEnd; ++It)
            {
                void* const Dyn = *It;
                if (Dyn != nullptr)
                {
                    (static_cast<FCliDynamicAsteroid*>(Dyn))->Destroy();
                    operator delete(Dyn);
                }
            }
            This->DynEnd = This->DynBegin;
        }
    }

    // Inlined in FUN_00520850: a new manager's tints from the field's -- diffuse (+0xbc) =
    // color_shift * the field's diffuse (+0xb4), ambient (+0xc8) = color_shift * the field's
    // ambient (+0xc0), both clamped to 1.0 from above only.
    void CliDynamicAsteroidApplyParentTints(FCliDynamicAsteroid* Dyn, FCliAsteroid* Parent)
    {
        float Diffuse[3];
        Diffuse[0] = Parent->DiffuseColor0 * Dyn->ColorShift0;
        Diffuse[1] = Dyn->ColorShift1 * Parent->DiffuseColor1;
        Diffuse[2] = Dyn->ColorShift2 * Parent->DiffuseColor2;
        Dyn->TintedDiffuse0 = Diffuse[0];
        Dyn->TintedDiffuse1 = Diffuse[1];
        Dyn->TintedDiffuse2 = Diffuse[2];

        float Ambient[3];
        CliAsteroidMulColor(Ambient, &Dyn->ColorShift0, &Parent->AmbientColor0);
        Dyn->TintedAmbient0 = Ambient[0];
        Dyn->TintedAmbient1 = Ambient[1];
        Dyn->TintedAmbient2 = Ambient[2];

        CliAsteroidClampColorToOne(&Dyn->TintedDiffuse0);
        CliAsteroidClampColorToOne(&Dyn->TintedAmbient0);
    }

    // ---- Billboard block ----

    void BillboardEnsureRocks(FAsteroidBillboardBlock* Bb);

    // FUN_00550000 — the two quality scalars, then BillboardEnsureRocks, which rebuilds the rock
    // array at the new QualityA / QualityB. Called from the [AsteroidBillboards] header (before
    // BillboardParseIni, bParsed still 0, so the rebuild only frees) and from the detail slider
    // (CliAsteroidApplyDetail).
    void BillboardSetQuality(FAsteroidBillboardBlock* Bb, float Detail)
    {
        // 0.25 at DAT_005c94f8.
        Bb->QualityA = (1.0f - Detail) * 0.25f + Detail;
        // d*d stays unrounded; 0.75 at DAT_005c8930.
        double const DetailSq = static_cast<double>(Detail) * Detail;
        Bb->QualityB = static_cast<float>((1.0 - DetailSq) * 0.75 + DetailSq);
        BillboardEnsureRocks(Bb);
    }

    // FUN_0054feb0 — NearFadeEnd = max(A, B), NearFadeStart = min(A, B). Both callers (the field-ini load
    // and CliAsteroidApplyDetail) pass A = FillDist*FillDetailScale, B = CubeSize*1.25 + A.
    void BillboardSetFadePlanes(FAsteroidBillboardBlock* Bb, float A, float B)
    {
        if (A < B)
        {
            Bb->NearFadeStart = A;
            Bb->NearFadeEnd = B;
        }
        else
        {
            Bb->NearFadeEnd = A;
            Bb->NearFadeStart = B;
        }
    }

    // FUN_0054fee0(bb, &DiffuseColor0, &AmbientColor0).
    void BillboardApplyTints(FAsteroidBillboardBlock* Bb, float const* Diffuse, float const* Ambient)
    {
        Bb->TintedDiffuse0 = ClampTintUpper(Bb->ColorShift0 * Diffuse[0]);
        Bb->TintedDiffuse1 = ClampTintUpper(Bb->ColorShift1 * Diffuse[1]);
        Bb->TintedDiffuse2 = ClampTintUpper(Bb->ColorShift2 * Diffuse[2]);
        Bb->TintedAmbient0 = ClampTintUpper(Ambient[0]);
        Bb->TintedAmbient1 = ClampTintUpper(Ambient[1]);
        Bb->TintedAmbient2 = ClampTintUpper(Ambient[2]);
    }

    // FUN_0054dbe0 — [AsteroidBillboards] values into the +0x19C block; `shape` is found in the
    // field's ShapeDB passed as the second argument (retail inlines the find).
    void BillboardParseIni(FAsteroidBillboardBlock* Bb, INI_Reader& Reader, FShapeDB* Shapes)
    {
        Bb->FadeDistPercent = 0.3f;
        Bb->bParsed = 1;

        if (!Reader.read_value())
            return;

        do
        {
            if (Reader.is_value("shape"))
            {
                char const* Name = Reader.get_value_string();
                FTexShape const* Shape = Shapes->Find(Name);
                Bb->Shape = Shape;
            }
            else if (Reader.is_value("count"))
            {
                Bb->Count = FloatToIntTrunc(Reader.get_value_float(0));
            }
            else if (Reader.is_value("size"))
            {
                float A = Reader.get_value_float(0);
                float B = Reader.get_value_float(1);
                if (B < A)
                {
                    float T = A;
                    A = B;
                    B = T;
                }
                Bb->SizeMin = A;
                Bb->SizeMax = B;
            }
            else if (Reader.is_value("start_dist"))
            {
                Bb->StartDist = Reader.get_value_float(0);
            }
            else if (Reader.is_value("fade_dist_percent"))
            {
                float V = Reader.get_value_float(0);
                if (V > 1.0f)
                    V = 1.0f;
                else if (V < 0.0f)
                    V = 0.0f;
                Bb->FadeDistPercent = V;
            }
            else if (Reader.is_value("ambient_intensity"))
            {
                Bb->AmbientIntensity = Reader.get_value_float(0);
            }
            else if (Reader.is_value("color_shift"))
            {
                Bb->ColorShift0 = Reader.get_value_float(0);
                Bb->ColorShift1 = Reader.get_value_float(1);
                Bb->ColorShift2 = Reader.get_value_float(2);
            }
        } while (Reader.read_value());
    }

    // Retail rand() returns 0..0x7fff; DAT_005caeec is 1/32768. 1 only when strictly above 0.5.
    unsigned char BillboardRandomBool()
    {
        double const Unit = static_cast<double>(rand()) * 3.0517578125e-05;
        return Unit > 0.5 ? 1 : 0;
    }

    // FUN_0054de10 (thiscall on the billboard block) — rebuild the rock array.
    void BillboardEnsureRocks(FAsteroidBillboardBlock* Bb)
    {
        // The pointer is not cleared, as retail (it is overwritten below, or left alone when
        // bParsed is clear).
        if (Bb->Rocks != nullptr)
            operator delete(Bb->Rocks);

        if (Bb->bParsed == 0)
            return;

        Bb->RockCount = static_cast<int>(static_cast<double>(Bb->Count) * Bb->QualityA);
        if (Bb->RockCount <= 0)
        {
            // An error, not a return -- the allocation still happens.
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s",
                  "E:\\FL\\Scratch\\Source\\Client\\Space\\SpaceTerrainRenderComp.cpp", 0x297,
                  "Billboard rock count is not > 0  If we have no rocks to draw, this instance "
                  "should not exist");
        }

        // 1.0 at DAT_005c89b8 is a double.
        Bb->Rocks = static_cast<FAsteroidBillboardRock*>(
            operator new(static_cast<unsigned>(Bb->RockCount) << 5));
        float const Range = Bb->SizeMax - Bb->SizeMin;
        float const InvCount = static_cast<float>(1.0 / static_cast<double>(Bb->RockCount));

        int Index = 0;
        for (Index = 0; Index < Bb->RockCount; ++Index)
        {
            FAsteroidBillboardRock* const Rock = &Bb->Rocks[Index];
            Rock->Pos[0] = 0.0f;
            Rock->Pos[1] = 0.0f;
            Rock->Pos[2] = 0.0f;
            // One unrounded chain, rounded to float once.
            Rock->HalfSize = static_cast<float>(
                (static_cast<double>(rand()) * Range * 3.0517578125e-05 + Bb->SizeMin) * 0.5);
            Rock->Alpha = 1.0f;
            Rock->bVisible = 1;
            // Four rand() draws, in field order.
            Rock->bFlipU = BillboardRandomBool();
            Rock->bFlipV = BillboardRandomBool();
            Rock->bSwapAxes = BillboardRandomBool();
            Rock->bNegateApex = BillboardRandomBool();
            Rock->Phase = static_cast<float>(static_cast<double>(Index) * InvCount);
        }

        // Only for start_dist > 0 (NaN skips). T = tan(35 degrees, DAT_005e0b00).
        if (Bb->StartDist > 0.0f)
        {
            double const A = static_cast<double>(Bb->QualityB) * Bb->StartDist;
            double const T = tan(0.6108652353286743);
            double const R = A * T / (1.0 - T);
            Bb->SphereRadius = static_cast<float>(R);
            Bb->SphereAhead = static_cast<float>(R + A);
            Bb->SphereFadeWidth = static_cast<float>(R * Bb->FadeDistPercent);
        }

        // A shape whose frame-0 handle IsTexture rejects (non-zero) is dropped.
        if (Bb->Shape != nullptr)
        {
            void* const Handle = Bb->Shape->GetGpuHandle(0);
            if (DALib::Pipeline->IsTexture(Handle) != 0)
                Bb->Shape = nullptr;
        }
    }

    void ScaleRgb255(float* Out, float R, float G, float B)
    {
        Out[0] = R * (1.0f / 255.0f);
        Out[1] = G * (1.0f / 255.0f);
        Out[2] = B * (1.0f / 255.0f);
    }

    // FUN_00524880 — always writes FillDetailScale; the rest only for a loaded field (bLoaded). No null
    // test of `This`, as retail.
    void CliAsteroidApplyDetail(FCliAsteroid* This, float Detail)
    {
        // (1 - d) is kept as a float for the dynamic block below, while FillDetailScale uses the
        // unrounded value; 0.65f at DAT_005db53c.
        double const OneMinusDetail = 1.0 - static_cast<double>(Detail);
        float const OneMinusDetailF = static_cast<float>(OneMinusDetail);
        This->FillDetailScale = static_cast<float>(
            OneMinusDetail * static_cast<double>(0.65f) + static_cast<double>(Detail));
        if (This->bLoaded == 0 || This->CmnField == nullptr)
            return;

        int const SyncedFill = static_cast<int>(
            static_cast<double>(This->FillDist) * static_cast<double>(This->FillDetailScale));
        This->CmnField->set_fill_dist(SyncedFill);

        if (This->bHasBillboards != 0)
        {
            // FillScaled is rounded to float before the add; 1.25f at DAT_005c9804.
            float const FillScaled = static_cast<float>(
                static_cast<double>(This->FillDist) * static_cast<double>(This->FillDetailScale));
            float const FadeFar = static_cast<float>(
                static_cast<double>(This->CubeSize) * static_cast<double>(1.25f) +
                static_cast<double>(FillScaled));
            BillboardSetFadePlanes(&This->Billboard, FillScaled, FadeFar);
            BillboardSetQuality(&This->Billboard, Detail);
        }

        if (This->ExclusionMgr != nullptr)
        {
            // Retail inlines the setter; the distance is squared unrounded.
            double const CliDist = static_cast<double>(This->FillDist) *
                                   static_cast<double>(This->FillDetailScale) *
                                   static_cast<double>(1.25f);
            FCliAsteroidExclusionInline::SetMaxCliDist(This->ExclusionMgr, CliDist);
        }

        // Every dynamic asteroid manager: DetailScale and its inverse (no zero guard) computed
        // once, then per manager the slots released (FUN_005267b0's loop, inlined), the array
        // freed and a new zeroed one allocated (operator new even for 0 slots).
        if (This->DynBegin == This->DynEnd)
            return;
        // Each rounded to float before the next step.
        float const DynDetail = static_cast<float>(
            static_cast<double>(OneMinusDetailF) * static_cast<double>(0.5f) +
            static_cast<double>(Detail));
        float const InvDyn = static_cast<float>(1.0 / static_cast<double>(DynDetail));
        for (void** It = This->DynBegin; It != This->DynEnd; ++It)
        {
            FCliDynamicAsteroid* const Dyn = static_cast<FCliDynamicAsteroid*>(*It);
            Dyn->ReleaseSlots();
            operator delete(Dyn->SlotPtrs);
            Dyn->DetailScale = DynDetail;
            Dyn->SlotPtrs = nullptr;
            Dyn->InvDetailScale = InvDyn;
            Dyn->SlotCount =
                static_cast<int>(static_cast<double>(Dyn->CountIni) * DynDetail + 0.5f);
            Dyn->SlotPtrs = static_cast<FCliDynamicAsteroidGameobj**>(operator new(
                static_cast<unsigned>(Dyn->SlotCount) * sizeof(FCliDynamicAsteroidGameobj*)));
            for (int I = 0; I < Dyn->SlotCount; ++I)
                Dyn->SlotPtrs[I] = nullptr;
        }
    }

    // FUN_005201e0. No null test of `this`: its one caller (CliAsteroidCreateAndLoad) tests
    // operator new's result. Stores in retail order. Retail writes no byte of +0x00 (bTestPlacement),
    // +0x30..+0x9f (MeshDesc), +0xb0 (PrimitiveType) or the pads here; each is written before it is
    // read: bTestPlacement by the field-ini load (= 0 before the section loop), MeshDesc and PrimitiveType by
    // the mesh build inside the same load.
    FCliAsteroid* CliAsteroidConstructRetail(FCliAsteroid* This)
    {
        // The cube map at +0x08 (allocator bytes +0x08 / +0x09, byte +0x14, FUN_005274a0).
        This->CubeMapConstruct();

        This->DiffuseColor0 = 1.0f;
        This->DiffuseColor1 = 1.0f;
        This->DiffuseColor2 = 1.0f;
        This->AmbientColor0 = 1.0f;
        This->AmbientColor1 = 1.0f;
        This->AmbientColor2 = 1.0f;

        // FUN_004a2f10 -- the field's ShapeDB at +0xE0 (its tail store +0xf4 = +0xe4 is part
        // of the container's own construction).
        ShapeDBConstructAt(This->ShapeDbStorage);

        InitBand(&This->Band);

        This->BandBegin = nullptr;
        This->BandEnd = nullptr;
        This->BandCap = nullptr;

        This->Billboard.Up[0] = 0.0f;
        This->Billboard.Up[1] = 1.0f;
        This->Billboard.Up[2] = 0.0f;
        This->Billboard.MaxLights = 4;

        This->Billboard.TintedDiffuse0 = 1.0f;
        This->Billboard.TintedDiffuse1 = 1.0f;
        This->Billboard.TintedDiffuse2 = 1.0f;
        This->Billboard.TintedAmbient0 = 1.0f;
        This->Billboard.TintedAmbient1 = 1.0f;
        This->Billboard.TintedAmbient2 = 1.0f;
        This->Billboard.ColorShift0 = 1.0f;
        This->Billboard.ColorShift1 = 1.0f;
        This->Billboard.ColorShift2 = 1.0f;

        This->Billboard.SphereRadius = 2500.0f;
        This->Billboard.StartDist = 1500.0f;
        This->Billboard.SphereAhead = 4000.0f;
        This->Billboard.Rocks = nullptr;
        This->Billboard.Count = 100;
        This->Billboard.RockCount = 100;
        This->Billboard.ZoneRef = nullptr;
        This->Billboard.DensityScale = 1.0f;
        This->Billboard.Shape = nullptr;
        This->Billboard.SizeMin = 500.0f;
        This->Billboard.SizeMax = 100.0f;
        This->Billboard.SphereFadeWidth = 700.0f;
        This->Billboard.FadeDistPercent = 0.3f;
        This->Billboard.NearFadeEnd = 2000.0f;
        This->Billboard.NearFadeStart = 1500.0f;
        This->Billboard.AmbientIntensity = 1.0f;
        This->Billboard.QualityA = 1.0f;
        This->Billboard.QualityB = 1.0f;
        This->Billboard.bParsed = 0;

        This->DynBegin = nullptr;
        This->DynEnd = nullptr;
        This->DynCap = nullptr;

        CmnAsteroid::CAsteroidField* Field =
            CmnAsteroid::CreateAsteroidField(StarSysGetActiveSystemId());
        This->CmnField = Field;
        if (Field != nullptr)
        {
            Field->AddRef();
        }
        else
        {
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s",
                  "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0x11e,
                  "NO cmn field created for client asteroid field");
        }

        This->CubeSize = 0x190;
        This->FillDist = 0x4b0;
        This->MaxAlpha = 1.0f;
        This->FillDetailScale = 1.0f;
        This->bLoaded = 0;
        This->bContainsFogZone = 0;
        This->EmptyCubeFreq = 0.0f;
        This->AmbientIncrease0 = 0;
        This->AmbientIncrease1 = 0;
        This->AmbientIncrease2 = 0;

        // Retail then erases the just-built (empty) cube map; nothing to port.
        This->ExclusionMgr = nullptr;
        This->Material = nullptr;
        This->MeshVerts = nullptr;
        This->MeshIndices = nullptr;
        This->MeshIndexCount = 0;
        This->FogNebula = 0;
        This->ExclusionSortTimer = 100.0f;

        // Retail also clears the (empty) band and dynamic vectors here.
        This->ExclusionPieceCount = 0;

        This->Zone = nullptr;
        This->bHasBand = 0;
        This->bHasExclusionBands = 0;
        This->bHasBillboards = 0;
        This->bDrawFarBands = 1;
        This->bNebulaLookupPending = 1;

        return This;
    }

    // FUN_00520850's [Field] section. Lod is a local of the caller.
    void ParseFieldSection(FCliAsteroid* This, INI_Reader& Reader, int& Lod)
    {
        if (!Reader.read_value())
            return;

        float const KInv255 = 1.0f / 255.0f;

        do
        {
            if (Reader.is_value("cube_size"))
            {
                This->CubeSize = FloatToIntTrunc(Reader.get_value_float(0));
            }
            else if (Reader.is_value("fill_dist"))
            {
                This->FillDist = FloatToIntTrunc(Reader.get_value_float(0));
            }
            else if (Reader.is_value("LOD"))
            {
                // The mesh merge clamps it and hands it to the renderer.
                Lod = FloatToIntTrunc(Reader.get_value_float(0));
            }
            else if (Reader.is_value("tint_field"))
            {
                float R = Reader.get_value_float(0) * KInv255;
                float G = Reader.get_value_float(1) * KInv255;
                float B = Reader.get_value_float(2) * KInv255;
                This->DiffuseColor0 = R;
                This->DiffuseColor1 = G;
                This->DiffuseColor2 = B;
                This->AmbientColor0 = R;
                This->AmbientColor1 = G;
                This->AmbientColor2 = B;
            }
            else if (Reader.is_value("diffuse_color"))
            {
                This->DiffuseColor0 = Reader.get_value_float(0) * KInv255;
                This->DiffuseColor1 = Reader.get_value_float(1) * KInv255;
                This->DiffuseColor2 = Reader.get_value_float(2) * KInv255;
            }
            else if (Reader.is_value("ambient_color"))
            {
                This->AmbientColor0 = Reader.get_value_float(0) * KInv255;
                This->AmbientColor1 = Reader.get_value_float(1) * KInv255;
                This->AmbientColor2 = Reader.get_value_float(2) * KInv255;
            }
            else if (Reader.is_value("ambient_increase"))
            {
                This->AmbientIncrease0 = static_cast<unsigned char>(FloatToIntTrunc(Reader.get_value_float(0)));
                This->AmbientIncrease1 = static_cast<unsigned char>(FloatToIntTrunc(Reader.get_value_float(1)));
                This->AmbientIncrease2 = static_cast<unsigned char>(FloatToIntTrunc(Reader.get_value_float(2)));
            }
            else if (Reader.is_value("empty_cube_frequency"))
            {
                This->EmptyCubeFreq = Reader.get_value_float(0);
            }
            else if (Reader.is_value("max_alpha"))
            {
                This->MaxAlpha = Reader.get_value_float(0);
            }
            else if (Reader.is_value("test_asteroid_placement"))
            {
                This->bTestPlacement = 1;
            }
            else if (Reader.is_value("contains_fog_zone"))
            {
                This->bContainsFogZone = 1;
            }
        } while (Reader.read_value());
    }
}

FCliAsteroid* FCliAsteroid::Construct()
{
    return CliAsteroidConstructRetail(this);
}

void FCliAsteroid::LoadFieldIni(char const* FieldIniRel)
{
    // No entry null test of `This` or the file name, as retail.
    INI_Reader Reader;

    char FieldIniPath[MAX_PATH] = {};
    GetFileSysPath(FieldIniPath, FieldIniRel, DALib::Data);

    CubeSize = 0xfa;
    EmptyCubeFreq = 0.0f;
    int Lod = 0;
    MaxAlpha = 1.0f;
    bHasBand = 0;
    bHasBillboards = 0;
    bHasExclusionBands = 0;
    bTestPlacement = 0;

    // A field with no zone is reported and loading goes on.
    if (Zone == nullptr)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Asteroid field (%s) must be hooked up to a zone",
              "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0x187, FieldIniRel);
    }

    BandVectorClear(this);
    DynVectorClear(this);

    // An open failure is reported and still runs the band/billboard/mesh tail.
    if (!Reader.open(FieldIniPath, false))
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: asteroid field ini file (%s) could not be opened",
              "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0x247, FieldIniRel);
    }
    else
    {
        // The reader's state is saved for the common field.
        bLoaded = 1;

        INI_Reader::State Saved;
        Reader.get_state(Saved);

        while (Reader.read_header())
        {
            if (Reader.is_header("TexturePanels"))
            {
                while (Reader.read_value())
                {
                    if (!Reader.is_value("file"))
                        continue;
                    char const* Rel = Reader.get_value_string();
                    char const* Full = GetFullFilename(Rel);
                    ShapeDBAt(ShapeDbStorage)->Load(Full, nullptr);
                }
            }
            else if (Reader.is_header("Field"))
            {
                ParseFieldSection(this, Reader, Lod);
            }
            else if (Reader.is_header("Band"))
            {
                // The field's own band (+0xfc).
                bHasBand = 1;
                BandParseIni(&this->Band, Reader, ShapeDBAt(ShapeDbStorage));
                float Diffuse[3] = {DiffuseColor0, DiffuseColor1, DiffuseColor2};
                float Ambient[3] = {AmbientColor0, AmbientColor1, AmbientColor2};
                BandApplyTints(&this->Band, Diffuse, Ambient);
                float Inc[3];
                ScaleRgb255(Inc, static_cast<float>(AmbientIncrease0),
                            static_cast<float>(AmbientIncrease1),
                            static_cast<float>(AmbientIncrease2));
                BandSetAmbientIncrease(&this->Band, Inc);
            }
            else if (Reader.is_header("ExclusionBand"))
            {
                INI_Reader::State ExState;
                Reader.get_state(ExState);

                FAsteroidBand* Band =
                    static_cast<FAsteroidBand*>(operator new(sizeof(FAsteroidBand)));
                if (Band != nullptr)
                    InitBand(Band);
                BandVectorPush(this, Band);

                // The vector's last element, no null test, as retail.
                FAsteroidBand* const Last = BandEnd[-1];
                BandParseIni(Last, Reader, ShapeDBAt(ShapeDbStorage));

                // The section again: every "zone" into the band's Zone.
                Reader.set_state(ExState);
                while (Reader.read_value())
                {
                    if (!Reader.is_value("zone"))
                        continue;
                    char const* Nick = Reader.get_value_string();
                    Universe::IZone const* Z =
                        Universe::get_zone(StarSysGetActiveSystemId(), Nick);
                    if (Z == nullptr)
                    {
                        FDUMP(kFdumpError,
                              "%s(%d) : *** ERROR: Exband (exclusion zone band) linked to "
                              "missing Zone (%s)",
                              "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0x1ea,
                              Nick);
                    }
                    else
                    {
                        Last->Zone = const_cast<Universe::IZone*>(Z);
                    }
                }
            }
            else if (Reader.is_header("AsteroidBillboards"))
            {
                bHasBillboards = 1;
                BillboardSetQuality(&Billboard, AsteroidDetailScalar());
                BillboardParseIni(&Billboard, Reader, ShapeDBAt(ShapeDbStorage));
                float Diffuse[3] = {DiffuseColor0, DiffuseColor1, DiffuseColor2};
                float Ambient[3] = {AmbientColor0, AmbientColor1, AmbientColor2};
                BillboardApplyTints(&Billboard, Diffuse, Ambient);
            }
            else if (Reader.is_header("DynamicAsteroids"))
            {
                INI_Reader::State SavedDyn;
                Reader.get_state(SavedDyn);

                FCliDynamicAsteroid* const Dyn = (static_cast<FCliDynamicAsteroid*>(operator new(sizeof(FCliDynamicAsteroid))))->Construct(CmnField);
                DynVectorPush(this, Dyn);

                // InvDetailScale divides by the unrounded value; no zero guard.
                double const Detail = AsteroidDetailScalar();
                double const DetailScale = (1.0 - Detail) * 0.5 + Detail;
                Dyn->DetailScale = static_cast<float>(DetailScale);
                Dyn->InvDetailScale = static_cast<float>(1.0 / DetailScale);

                Dyn->ParseIni(Reader);

                // The section again: every "zone" into +0; no zone found -> the field's zone,
                // when it has one.
                Reader.set_state(SavedDyn);
                unsigned char bZoneSet = 0;
                if (Reader.read_value())
                {
                    do
                    {
                        if (Reader.is_value("zone"))
                        {
                            char const* Nick = Reader.get_value_string();
                            Universe::IZone* Z = const_cast<Universe::IZone*>(
                                Universe::get_zone(StarSysGetActiveSystemId(), Nick));
                            if (Z == nullptr)
                            {
                                FDUMP(kFdumpError,
                                      "%s(%d) : *** ERROR: DynamicAsteroids, linked to missing "
                                      "Zone (%s)",
                                      "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp",
                                      0x212, Nick);
                            }
                            else
                            {
                                Dyn->Zone = Z;
                                bZoneSet = 1;
                            }
                        }
                    } while (Reader.read_value());
                }
                if (bZoneSet == 0 && Zone != nullptr)
                    Dyn->Zone = Zone;

                CliDynamicAsteroidApplyParentTints(Dyn, this);
            }
        }

        // The common field loads from the saved state; the fill distance is truncated from the
        // unrounded product.
        if (CmnField != nullptr)
        {
            Reader.set_state(Saved);
            CmnField->load(Reader, CubeSize);
            CmnField->set_fill_dist(static_cast<int>(
                static_cast<double>(FillDist) * static_cast<double>(FillDetailScale)));
            CmnField->set_empty_freq(EmptyCubeFreq);
            ExclusionMgr = CmnField->ExclusionMgr;
        }
        else
        {
            FDUMP(kFdumpError,
                  "%s(%d) : *** ERROR: client asteroid field (%s) must be connected to common "
                  "asteroid field before calling load",
                  "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0x22f, FieldIniRel);
        }

        // With billboards, the count raised to the exclusion manager's maximum and
        // DensityScale = old count / new count (no zero test).
        if (bHasBillboards != 0)
        {
            int Use = Billboard.Count;
            if (ExclusionMgr != nullptr)
            {
                int const MaxBb = ExclusionMgr->get_max_billboard_count();
                if (Use < MaxBb)
                    Use = MaxBb;
                int const OldCount = Billboard.Count;
                Billboard.Count = Use;
                Billboard.DensityScale = static_cast<float>(
                    static_cast<double>(OldCount) / static_cast<double>(Use));
                ExclusionMgr->normalize_billboard_density(Use);
            }
            else
            {
                Billboard.DensityScale = 1.0f;
            }
        }
    }

    // The field's own band gets the zone (+0x100) and its rings; the piece cursor starts at its
    // piece count (+0x154).
    int PieceCursor = 0;
    if (bHasBand != 0)
    {
        this->Band.Zone = Zone;
        BandBuildExclusionRings(&this->Band);
        PieceCursor = this->Band.RenderParts;
    }
    // Every exclusion band gets the tints, the ambient increase, its rings and its first piece
    // index, and adds its piece count to ExclusionPieceCount. No null test of the band, as
    // retail.
    ExclusionPieceCount = 0;
    if (BandBegin != nullptr && BandEnd != BandBegin)
    {
        bHasExclusionBands = 1;
        for (FAsteroidBand** It = BandBegin; It != BandEnd; ++It)
        {
            FAsteroidBand* const Band = *It;
            float Diffuse[3] = {DiffuseColor0, DiffuseColor1, DiffuseColor2};
            float Ambient[3] = {AmbientColor0, AmbientColor1, AmbientColor2};
            BandApplyTints(Band, Diffuse, Ambient);
            float Inc[3];
            ScaleRgb255(Inc, static_cast<float>(AmbientIncrease0),
                        static_cast<float>(AmbientIncrease1),
                        static_cast<float>(AmbientIncrease2));
            BandSetAmbientIncrease(Band, Inc);
            BandBuildExclusionRings(Band);
            Band->FirstPiece = PieceCursor;
            int const PieceCount = Band->RenderParts;
            PieceCursor += PieceCount;
            ExclusionPieceCount += static_cast<unsigned>(PieceCount);
        }
    }

    // FillScaled is rounded to float before the add.
    if (bHasBillboards != 0)
    {
        Billboard.ZoneRef = Zone;
        BillboardEnsureRocks(&Billboard);
        float const FillScaled = static_cast<float>(
            static_cast<double>(FillDist) * static_cast<double>(FillDetailScale));
        float const FadeFar = static_cast<float>(
            static_cast<double>(CubeSize) * 1.25 + static_cast<double>(FillScaled));
        BillboardSetFadePlanes(&Billboard, FillScaled, FadeFar);
    }

    // Retail inlines the two setters: the CliAsteroid distance squared unrounded into +0x14, and
    // the constant 10000.0f into +0x18 (what the exported setter stores for 100).
    if (ExclusionMgr != nullptr)
    {
        FCliAsteroidExclusionInline::SetMaxCliDist(
            ExclusionMgr, static_cast<double>(FillDist) *
                                    static_cast<double>(FillDetailScale) *
                                    static_cast<double>(1.25f));
        ExclusionMgr->set_max_dynamicasteroid_exclusion_distance(100.0f);
    }

    // The archetype mesh merge into +0xa0..+0xac and the vertex source.
    CliAsteroidMergeCubeMesh(this, Lod, FieldIniRel);
}

bool CliAsteroidCreateAndLoad(char const* FieldIniRel, char const* ZoneNickname)
{
    // Pushed onto the registry before anything can fail -- a field whose zone is missing stays
    // on the list.
    FCliAsteroid* Obj = static_cast<FCliAsteroid*>(operator new(sizeof(FCliAsteroid)));
    if (Obj != nullptr)
        Obj->Construct();
    Registry().push_back(Obj);

    Universe::IZone* Zone = Universe::get_non_const_zone(StarSysGetActiveSystemId(), ZoneNickname);
    if (Zone == nullptr)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: AsteroidField (%s) linked to missing Zone (%s)",
              "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0xc8, FieldIniRel,
              ZoneNickname);
        return false;
    }

    // Any other zone shape only warns.
    unsigned const Shape = Zone->GetShape();
    if (Shape != Universe::ZoneShapeSphere && Shape != Universe::ZoneShapeEllipsoid)
    {
        FDUMP(kFdumpWarning,
              "%s(%d) : *** WARNING: Asteroid field (%s) hooked up to zone (%s) which is not a "
              "valid shape",
              "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0xd1, FieldIniRel,
              ZoneNickname);
    }

    Obj->Zone = Zone;
    if (Obj->CmnField != nullptr)
        Obj->CmnField->set_zone(Zone);

    CliAsteroidApplyDetail(Obj, AsteroidDetailScalar());
    Obj->LoadFieldIni(FieldIniRel);
    return true;
}

unsigned CliAsteroidRegistryCount()
{
    // FUN_005201c0: the list count (DAT_006753c8).
    return static_cast<unsigned>(Registry().size());
}

void CliAsteroidUpdateFields(void* Camera, float Dt)
{
    // No null test of the field, as retail.
    if (GetGameOption(kGameOptAsteroids) == 0)
        return;
    std::list<FCliAsteroid*>& List = Registry();
    for (std::list<FCliAsteroid*>::iterator It = List.begin(); It != List.end(); ++It)
        (*It)->Update(static_cast<FSpaceCamera*>(Camera), Dt);
}

void CliAsteroidDestroyAll()
{
    // Retail returns 1, which no caller reads.
    std::list<FCliAsteroid*>& List = Registry();
    for (std::list<FCliAsteroid*>::iterator It = List.begin(); It != List.end(); ++It)
    {
        FCliAsteroid* const Field = *It;
        if (Field != nullptr)
        {
            Field->Destruct();
            operator delete(Field);
        }
    }
    List.clear();
}

void FCliAsteroid::Destruct()
{
    // Explicit releases in retail order, then the inlined member destructors.
    if (CmnField != nullptr)
    {
        CmnField->Release();
        CmnField = nullptr;
    }
    if (Material != nullptr)
    {
        static_cast<IDAComponent*>(Material)->Release();
        Material = nullptr;
    }
    operator delete(MeshVerts);
    MeshVerts = nullptr;
    operator delete(MeshIndices);
    MeshIndices = nullptr;
    ShapeDBAt(ShapeDbStorage)->Clear();
    for (FAsteroidBand** It = BandBegin; It != BandEnd; ++It)
    {
        FAsteroidBand* const Band = *It;
        if (Band != nullptr)
        {
            BandFreeRingMesh(Band);
            operator delete(Band);
        }
    }
    BandEnd = BandBegin;
    for (void** It = DynBegin; It != DynEnd; ++It)
    {
        FCliDynamicAsteroid* const Dyn = static_cast<FCliDynamicAsteroid*>(*It);
        if (Dyn != nullptr)
        {
            Dyn->Destroy();
            operator delete(Dyn);
        }
    }
    DynEnd = DynBegin;
    ExclusionMgr = nullptr;
    operator delete(DynBegin);
    DynBegin = nullptr;
    DynEnd = nullptr;
    DynCap = nullptr;
    // The Rocks pointer itself is left as it was.
    operator delete(Billboard.Rocks);
    Billboard.Count = 0;
    Billboard.ZoneRef = nullptr;
    operator delete(BandBegin);
    BandBegin = nullptr;
    BandEnd = nullptr;
    BandCap = nullptr;
    BandFreeRingMesh(&this->Band);
    ShapeDBDestructAt(ShapeDbStorage);
    CliAsteroidCubeMapDestruct(this);
}

void CliAsteroidClampColorToOne(float* Rgb)
{
    for (int Index = 0; Index < 3; ++Index)
    {
        if (!(Rgb[Index] < 1.0f))
            Rgb[Index] = 1.0f;
    }
}

float* CliAsteroidMulColor(float* Out, float const* A, float const* B)
{
    // Products computed z, y, x, stored x, y, z.
    float const Z = A[2] * B[2];
    float const Y = A[1] * B[1];
    float const X = A[0] * B[0];
    Out[0] = X;
    Out[1] = Y;
    Out[2] = Z;
    return Out;
}

I3DMathEngine* CliAsteroidMathEngine()
{
    // Retail tests DAT_006753d8 and fills it at each of its five use sites.
    static I3DMathEngine* s_Math = nullptr;
    if (s_Math == nullptr)
        s_Math = Acquire3DMathEngine();
    return s_Math;
}

void CliAsteroidApplyGlobalDetail(float Detail)
{
    AsteroidDetailScalar() = Detail;
    RaiseActiveSpaceCameraCut(); // the camera's +0xbc flag re-scatters the billboards
    for (std::list<FCliAsteroid*>::iterator It = Registry().begin(); It != Registry().end(); ++It)
        CliAsteroidApplyDetail(*It, Detail);
}

int FCliAsteroid::PieceCount()
{
    // No null test, as retail.
    return Band.RenderParts + static_cast<int>(ExclusionPieceCount);
}

namespace
{
    // FUN_0054d920 (thiscall on the band) — a piece's centre. Index - FirstPiece, a negative
    // result clamped to 0; nothing is tested for null.
    float* BandGetPiecePos(FAsteroidBand* Band, int PieceIndex, float* OutXYZ)
    {
        int Rel = PieceIndex - Band->FirstPiece;
        if (Rel < 0)
            Rel = 0;
        float const* const Center = Band->Pieces[Rel].Center;
        OutXYZ[0] = Center[0];
        OutXYZ[1] = Center[1];
        OutXYZ[2] = Center[2];
        return OutXYZ;
    }

    // FUN_0054d950 (thiscall on the band) — a piece's radius, same index clamp, no null test.
    float BandGetPieceRadius(FAsteroidBand* Band, int PieceIndex)
    {
        int Rel = PieceIndex - Band->FirstPiece;
        if (Rel < 0)
            Rel = 0;
        return Band->Pieces[Rel].Radius;
    }
} // namespace

namespace
{
    // DAT_006140a4 — two-sided band light soft threshold.
    constexpr float kBandLightSoft = 0.75f;
    // DAT_005c7f68 (0x3b808081), 1/255 as a float.
    constexpr float kBandInv255 = 0.003921569f;

    // FUN_0054cfb0 (thiscall on the band) — bake each particle's two lit colours from up to four
    // directional lights near the zone. Nothing is tested for null; with no light at all bLit
    // stays clear. x87 intermediates are modelled as doubles where retail keeps them unrounded.
    void BandBakeLighting(FAsteroidBand* Band)
    {
        Vector const& ZoneVec = Band->Zone->GetPos();
        float ZonePos[3] = {ZoneVec.x, ZoneVec.y, ZoneVec.z};
        void* LightSlots[16];
        int const LightCount = SpaceCollectNearbyLights(LightSlots, 16, ZonePos, 100.0f, 0);
        if (LightCount == 0)
            return;

        // (system ambient / 255 + ambient_increase) * the tinted ambient. Red is rounded to float
        // at every step; green keeps the scaled byte and the sum unrounded, blue the sum.
        unsigned char const* const AmbientBytes = StarSysAmbientColor();
        double const Green255 = static_cast<double>(AmbientBytes[1]) * kBandInv255;
        float const Blue255 = static_cast<float>(AmbientBytes[2]) * kBandInv255;
        float Ambient[3];
        Ambient[0] = (static_cast<float>(AmbientBytes[0]) * kBandInv255 + Band->AmbientIncrease[0]) *
                     Band->TintedAmbient[0];
        Ambient[1] = static_cast<float>((Green255 + Band->AmbientIncrease[1]) *
                                        Band->TintedAmbient[1]);
        Ambient[2] = static_cast<float>(
            (static_cast<double>(Blue255) + Band->AmbientIncrease[2]) * Band->TintedAmbient[2]);

        // Type 3 lights only, at most four: direction (normalised, no zero guard) then colour.
        // The sample's result is not read, and every sample adds to Ambient.
        float Lights[4][6];
        int DirCount = 0;
        for (int I = 0; I < LightCount && DirCount < 4; ++I)
        {
            FEngineLight* const Light = static_cast<FEngineLight*>(LightSlots[I]);
            if (Light->Type != kLightDirectional)
                continue;
            float* const Dir = Lights[DirCount];
            Light->Sample(ZonePos, Dir, Ambient, Dir + 3);
            ++DirCount;
            double const Inv =
                1.0 / sqrt((static_cast<double>(Dir[0]) * Dir[0] + static_cast<double>(Dir[1]) * Dir[1]) +
                           static_cast<double>(Dir[2]) * Dir[2]);
            Dir[0] = static_cast<float>(Dir[0] * Inv);
            Dir[1] = static_cast<float>(Dir[1] * Inv);
            Dir[2] = static_cast<float>(Dir[2] * Inv);
        }

        float const Scale = Band->AmbientIntensity;
        Ambient[0] = Ambient[0] * Scale;
        Ambient[1] = Ambient[1] * Scale;
        Ambient[2] = Ambient[2] * Scale;

        double const SoftDenom = static_cast<double>(kBandLightSoft) + 1.0f;
        for (int P = 0; P < Band->ParticleCount; ++P)
        {
            FAsteroidBandParticle* const Part = &Band->Particles[P];
            float Front[3] = {Ambient[0], Ambient[1], Ambient[2]};
            float Back[3] = {Ambient[0], Ambient[1], Ambient[2]};
            for (int L = 0; L < DirCount; ++L)
            {
                float const* const Dir = Lights[L];
                float const* const Color = Lights[L] + 3;
                float const* const N = Part->Normal;
                // Summed x, z, then y. The first test reads the unrounded dot, everything after
                // it the float copy.
                double const Dot = (static_cast<double>(Dir[0]) * N[0] + static_cast<double>(Dir[2]) * N[2]) +
                                   static_cast<double>(Dir[1]) * N[1];
                float const DotF = static_cast<float>(Dot);
                if (-kBandLightSoft < Dot)
                {
                    double const T = (static_cast<double>(kBandLightSoft) + DotF) / SoftDenom;
                    for (int C = 0; C < 3; ++C)
                        Back[C] = Back[C] + static_cast<float>(
                                                static_cast<double>(Color[C]) * Band->TintedDiffuse[C] * T);
                }
                if (-kBandLightSoft < -DotF)
                {
                    double const T = (static_cast<double>(-DotF) + kBandLightSoft) / SoftDenom;
                    for (int C = 0; C < 3; ++C)
                        Front[C] = Front[C] + static_cast<float>(
                                                  static_cast<double>(Color[C]) * Band->TintedDiffuse[C] * T);
                }
            }
            // Capped from above only; 1.0 and NaN become 1.0.
            for (int C = 0; C < 3; ++C)
            {
                Part->ColorFront[C] = ClampTintUpper(Front[C]);
                Part->ColorBack[C] = ClampTintUpper(Back[C]);
            }
        }
        Band->bLit = 1;
    }

    // FUN_0054c420 (thiscall on one particle) -- the particle's fade and facing flag from the
    // camera position and the band's four squared fade distances. No null tests.
    void BandUpdateParticleFade(FAsteroidBandParticle* Part, Vector const* CamPos, float InnerSq,
                                float NearSq, float FarSq, float OuterSq)
    {
        float* const Fade = &Part->Fade;

        // Only the offsets are rounded to float; the squared length (x*x + z*z, then + y*y)
        // stays unrounded.
        float const Dx = CamPos->x - Part->Pos[0];
        float const Dy = CamPos->y - Part->Pos[1];
        float const Dz = CamPos->z - Part->Pos[2];
        double const DistSq = (static_cast<double>(Dx) * Dx + static_cast<double>(Dz) * Dz) +
                              static_cast<double>(Dy) * Dy;

        // bInside is set only by the inner branch. Each compare is ordered (NaN takes the
        // "else" way).
        bool bInside = false;
        if (DistSq > NearSq)
        {
            if (DistSq < FarSq)
            {
                *Fade = 1.0f;
            }
            else if (DistSq > OuterSq)
            {
                *Fade = 0.0f;
            }
            else
            {
                *Fade = static_cast<float>((OuterSq - DistSq) / (OuterSq - FarSq));
            }
        }
        else if (DistSq < InnerSq)
        {
            *Fade = 0.0f;
            bInside = true;
        }
        else
        {
            *Fade = static_cast<float>((DistSq - InnerSq) / (NearSq - InnerSq));
        }

        if (!(*Fade > 0.0f) && !bInside)
            return;

        // The unit offset dotted with the normal, summed z, y, then x.
        float const* const N = Part->Normal;
        double const Inv = 1.0 / sqrt(DistSq);
        double const Dot = (Dz * Inv * N[2] + Dy * Inv * N[1]) + Dx * Inv * N[0];
        Part->bFacing = (Dot > 0.0) ? 1 : 0;
        // |Dot| rounded to float; 0.3 at DAT_005d3e08, 0.5 at DAT_005c75e0, 5 at DAT_005e0a60.
        float const AbsDot = static_cast<float>(fabs(Dot));
        if (AbsDot < 0.3f)
        {
            *Fade = 0.0f;
            return;
        }
        if (AbsDot < 0.5f)
            *Fade = (AbsDot - 0.3f) * *Fade * 5.0f;
    }

    // FUN_0054d410 (thiscall on the band) -- the band's per-frame update from FCliAsteroid::Update:
    // the lighting baked on the second call (UpdateCount counts up while bLit is clear), then,
    // unless MaxAlpha is below 0.001 (DAT_005c75f8), every particle's fade against the squares
    // of the four fade distances.
    void BandUpdate(FAsteroidBand* Band, Vector const* CamPos)
    {
        if (Band->bLit == 0)
        {
            Band->UpdateCount = Band->UpdateCount + 1;
            if (Band->UpdateCount > 1)
                BandBakeLighting(Band);
        }

        if (Band->MaxAlpha < 0.001)
            return;

        float const A = Band->FadeDist[0];
        float const B = Band->FadeDist[1];
        float const C = Band->FadeDist[2];
        float const D = Band->FadeDist[3];
        float const ASq = A * A;
        float const BSq = B * B;
        float const CSq = C * C;
        float const DSq = D * D;
        int const ParticleCount = Band->ParticleCount;
        FAsteroidBandParticle* const Particles = Band->Particles;
        for (int Index = 0; Index < ParticleCount; ++Index)
            BandUpdateParticleFade(&Particles[Index], CamPos, ASq, BSq, CSq, DSq);
    }

    // FUN_0054d4e0 (thiscall on the band) — Win3D triangle-pair quads for one piece index.
    // Retail's first stack argument (the camera position FCliAsteroid::DrawFarPiece passes) is
    // never read, so the port drops it. Nothing is tested for null.
    void BandDrawPiece(FAsteroidBand* Band, int PieceIndex)
    {
        if (Band->bLit == 0)
            BandBakeLighting(Band);

        // Rel = piece - FirstPiece; >= RenderParts or < 0 -> 0.
        int Rel = PieceIndex - Band->FirstPiece;
        int const RenderParts = Band->RenderParts;
        if (Rel >= RenderParts || Rel < 0)
            Rel = 0;

        // 0.01f at DAT_005c99c8; NaN goes on.
        float const MaxAlpha = Band->MaxAlpha;
        if (MaxAlpha < 0.01f)
            return;

        int const VertInc = Band->VertIncrease;
        float const InvVertInc = static_cast<float>(1.0 / static_cast<double>(VertInc));
        int const PointCount = Band->ParticleCount;
        int const CullMode = Band->CullMode;
        FAsteroidBandParticle* const Particles = Band->Particles;

        // A shape binds its frame-0 handle, null included.
        FTexShape const* Shape = Band->Shape;
        if (Shape != nullptr)
            Win3DBindTexture(Shape->GetGpuHandle(0));

        int const EndIdx = Band->Pieces[Rel].EndParticle;
        int const StartIdx = Band->Pieces[Rel].FirstParticle;
        int UCursor = 0;
        int UNext = 1;
        Win3DBegin(kWin3DTriangles);

        for (int Pass = 0; Pass < 2; ++Pass)
        {
            // Cull mode 1 drops pass 0, mode 2 drops pass 1.
            if (Pass + 1 == CullMode)
                continue;
            if (StartIdx >= EndIdx)
                continue;

            // A walks the piece's particles unwrapped; B is the next one, wrapped.
            int Idx = StartIdx;
            int Remaining = EndIdx - StartIdx;
            int AIdx = Idx;
            do
            {
                ++Idx;
                if (UNext > VertInc)
                {
                    UCursor -= VertInc;
                    UNext -= VertInc;
                }
                int WrapIdx = Idx;
                if (WrapIdx >= PointCount)
                    WrapIdx -= PointCount;

                FAsteroidBandParticle* const PartA = &Particles[AIdx];
                FAsteroidBandParticle* const PartB = &Particles[WrapIdx];
                unsigned char const FlagA = PartA->bFacing;
                unsigned char const FlagB = PartB->bFacing;

                bool bDraw = false;
                if (Pass == 0)
                {
                    if (FlagA == 0 || FlagB == 0)
                        bDraw = true;
                }
                else if (Pass != 1 || (FlagA != 0 && FlagB != 0))
                {
                    bDraw = true;
                }

                if (bDraw)
                {
                    float AlphaA = PartA->Fade;
                    float AlphaB = PartB->Fade;
                    float* ColorA = nullptr;
                    float* ColorB = nullptr;
                    if (FlagA == 0)
                    {
                        if (CullMode == 1)
                            AlphaA = 0.0f;
                        ColorA = PartA->ColorBack;
                    }
                    else
                    {
                        if (CullMode == 2)
                            AlphaA = 0.0f;
                        ColorA = PartA->ColorFront;
                    }
                    if (FlagB == 0)
                    {
                        if (CullMode == 1)
                            AlphaB = 0.0f;
                        ColorB = PartB->ColorBack;
                    }
                    else
                    {
                        if (CullMode == 2)
                            AlphaB = 0.0f;
                        ColorB = PartB->ColorFront;
                    }

                    // Skipped only when both alphas are strictly below 0.01f (NaN draws).
                    if (!(AlphaA < 0.01f) || !(AlphaB < 0.01f))
                    {
                        if (MaxAlpha < AlphaA)
                            AlphaA = MaxAlpha;
                        if (MaxAlpha < AlphaB)
                            AlphaB = MaxAlpha;

                        float const U0 = static_cast<float>(UCursor) * InvVertInc;
                        float const U1 = static_cast<float>(UNext) * InvVertInc;
                        float const HalfH = PartA->HalfHeight;
                        float const Ax = PartA->Pos[0];
                        float const Ay = PartA->Pos[1];
                        float const Az = PartA->Pos[2];
                        float const Bx = PartB->Pos[0];
                        float const By = PartB->Pos[1];
                        float const Bz = PartB->Pos[2];

                        Win3DColor4f(ColorA[0], ColorA[1], ColorA[2], AlphaA);
                        Win3DTexCoord2f(U0, 1.0f);
                        Win3DVertex3f(Ax, Ay + HalfH, Az);
                        Win3DTexCoord2f(U0, 0.0f);
                        Win3DVertex3f(Ax, Ay - HalfH, Az);

                        Win3DColor4f(ColorB[0], ColorB[1], ColorB[2], AlphaB);
                        Win3DTexCoord2f(U1, 0.0f);
                        Win3DVertex3f(Bx, By - HalfH, Bz);
                        Win3DTexCoord2f(U1, 0.0f);
                        Win3DVertex3f(Bx, By - HalfH, Bz);
                        Win3DTexCoord2f(U1, 1.0f);
                        Win3DVertex3f(Bx, By + HalfH, Bz);

                        Win3DColor4f(ColorA[0], ColorA[1], ColorA[2], AlphaA);
                        Win3DTexCoord2f(U0, 1.0f);
                        Win3DVertex3f(Ax, Ay + HalfH, Az);
                    }
                }

                ++AIdx;
                ++UCursor;
                ++UNext;
                --Remaining;
            } while (Remaining != 0);
        }
        Win3DEnd();
    }
} // namespace

void FCliAsteroid::Update(FSpaceCamera* Cam, float Dt)
{
    // Once (bNebulaLookupPending, set by the constructor), with a zone: the nebula at the zone's position
    // into +0xd4. The flag is cleared either way.
    if (bNebulaLookupPending != 0 && Zone != nullptr)
    {
        FogNebula = static_cast<unsigned>(reinterpret_cast<uintptr_t>(
            StarSysFindNebula(&Zone->GetPos(), nullptr, nullptr)));
    }
    bNebulaLookupPending = 0;

    const Vector* const Pos3 = AsCamera(Cam)->GetPosition();
    Vector CamPos;
    CamPos.x = Pos3->x;
    CamPos.y = Pos3->y;
    CamPos.z = Pos3->z;

    if (GetGameOption(kGameOptAsteroids) == 0)
        return;

    // The exclusion timer (+0xd8): above 0.5 (DAT_00613848) or on a camera cut (the camera's
    // +0xbc byte), the exclusion manager is re-sorted around the camera.
    ExclusionSortTimer += Dt;
    if (ExclusionSortTimer > 0.5f || Cam->FlagBC != 0)
    {
        ExclusionSortTimer = 0.0f;
        if (ExclusionMgr != nullptr)
            ExclusionMgr->sort(&CamPos);
    }

    if (GetGameOption(kGameOptObjectsGroup) != 0 && CmnField != nullptr)
    {
        FCliAsteroidCubeMap& Cubes = CliAsteroidCubeMap(this);

        // Every change event of the common field, in list order.
        for (CmnAsteroid::asteroid_marker* Event = CmnField->get_changes();
             Event != nullptr; Event = Event->Next)
        {
            FCliAsteroidCubeMap::iterator It = Cubes.find(Event->CubeId);
            if (It == Cubes.end())
            {
                It = Cubes.insert(std::pair<unsigned long const, FCliAsteroidCube>(
                                      Event->CubeId, FCliAsteroidCube()))
                         .first;
                FCliAsteroidCube& Value = It->second;
                // The common cube copied in (looked up by value).
                CmnAsteroid::CAsteroidCube* const Cube = CmnField->find_cube(Event->CubeId);
                if (Cube != nullptr)
                {
                    Value.Cube = Cube;
                    Value.CubeId = Cube->get_id();
                    Value.Orient = Cube->Orient;
                    Value.Pos = Cube->Pos;
                    Value.CubeSize = CubeSize;
                }
                else
                {
                    FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s",
                          "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0x31d,
                          "no cmn cube found");
                }
            }
            It->second.ApplyEvent(Event);
        }

        // Cubes with no asteroid left are erased.
        FCliAsteroidCubeMap::iterator CubeIt = Cubes.begin();
        while (CubeIt != Cubes.end())
        {
            if (CubeIt->second.Asteroids.size() == 0)
                CubeIt = Cubes.erase(CubeIt);
            else
                ++CubeIt;
        }

        // The exclusion manager is re-read per manager.
        for (void** DynIt = DynBegin; DynIt != DynEnd; ++DynIt)
        {
            (static_cast<FCliDynamicAsteroid*>(*DynIt))->Update(Cam, Dt,
                                                                ExclusionMgr);
        }
    }

    // [EFFECTS], the common field, ASTEROID_EXT in (0, 3) (signed compares).
    if (GetGameOption(kGameOptEffectsGroup) == 0 || CmnField == nullptr)
        return;
    if (GetGameOption(kGameOptAsteroidExt) <= 0)
        return;
    if (GetGameOption(kGameOptAsteroidExt) >= 3)
        return;

    if (bHasBand != 0)
        BandUpdate(&Band, &CamPos);

    // Exclusion bands: one near-field fraction into every band's MaxAlpha.
    if (bHasExclusionBands == 0)
        return;
    float const Fraction = CmnField->near_field_fraction(CamPos, 1000.0f);
    for (FAsteroidBand** It = BandBegin; It != BandEnd; ++It)
    {
        (*It)->MaxAlpha = Fraction;
        BandUpdate(*It, &CamPos);
    }
}

float* FCliAsteroid::GetPiecePos(int PieceIndex, float* OutXYZ)
{
    // A piece in [0, band count +0x154) comes from the field's own band.
    int const Primary = this->Band.RenderParts;
    if (PieceIndex < Primary && PieceIndex >= 0)
        return BandGetPiecePos(&this->Band, PieceIndex, OutXYZ);

    // Otherwise the first exclusion band with FirstPiece <= piece < FirstPiece + RenderParts;
    // entries are read unchecked.
    for (FAsteroidBand** It = BandBegin; It != BandEnd; ++It)
    {
        FAsteroidBand* const Band = *It;
        int const Base = Band->FirstPiece;
        if (PieceIndex >= Base && PieceIndex < Base + Band->RenderParts)
            return BandGetPiecePos(Band, PieceIndex, OutXYZ);
    }

    FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s",
          "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0x699,
          "bad piece number supplied to get_piece_pos in asteroid field");
    OutXYZ[0] = 0.0f;
    OutXYZ[1] = 0.0f;
    OutXYZ[2] = 0.0f;
    return OutXYZ;
}

float FCliAsteroid::GetPieceRadius(int PieceIndex)
{
    // Same band lookup as FCliAsteroid::GetPiecePos.
    int const Primary = this->Band.RenderParts;
    if (PieceIndex < Primary && PieceIndex >= 0)
        return BandGetPieceRadius(&this->Band, PieceIndex);

    for (FAsteroidBand** It = BandBegin; It != BandEnd; ++It)
    {
        FAsteroidBand* const Band = *It;
        int const Base = Band->FirstPiece;
        if (PieceIndex >= Base && PieceIndex < Base + Band->RenderParts)
            return BandGetPieceRadius(Band, PieceIndex);
    }

    FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s",
          "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0x6ad,
          "bad piece number supplied to get_piece_radius in asteroid field");
    return 1.0f; // DAT_005c75dc
}

void CliAsteroidForEach(FCliAsteroidVisitFn Fn, void* Ctx)
{
    // The list walk FUN_0051ba20 and FUN_00511b90 inline; both read the entry unchecked.
    // Entries come only from CliAsteroidCreateAndLoad's operator new.
    std::list<FCliAsteroid*>& List = Registry();
    for (std::list<FCliAsteroid*>::iterator It = List.begin(); It != List.end(); ++It)
        Fn(*It, Ctx);
}

void FCliAsteroid::DrawFarPiece(FSpaceCamera* Cam, int PieceIndex)
{
    // Nothing is tested for null.
    if (bHasBand == 0 && bHasExclusionBands == 0)
        return;
    if (GetGameOption(kGameOptAsteroidExt) == 0)
        return;
    if (GetGameOption(kGameOptEffectsGroup) == 0)
        return;
    if (GetGameOption(kGameOptAsteroidExt) >= 3)
        return;
    if (bDrawFarBands == 0)
        return;

    // Retail also fetches the camera orientation and position and the zone position; they are
    // const getters whose results nothing reads (the position is only BandDrawPiece's unread
    // first argument), so the port does not make the calls.
    // FUN_0040fb60(near, 250000.0f DAT_00610534): the two stores at camera +0xa4 / +0xa8.
    ICamera* const Camera = AsCamera(Cam);
    float const SavedNear = Camera->GetZNear();
    float const SavedFar = Camera->GetZFar();
    Cam->ZNear = SavedNear;
    Cam->ZFar = 250000.0f;

    // DAT_00613850: .data 1, read here only, no writer.
    static unsigned long s_BandDrawEnable = 1;
    if (s_BandDrawEnable != 0)
    {
        Win3DDisable(kWin3DLightingCap);
        Win3DDisable(kWin3DCullFace);
        Win3DSetDepthWrite(0);
        Win3DDisable(kWin3DDepthTest);
        Win3DEnable(kWin3DBlend);
        Win3DApplyCamera(Camera);
        Win3DSetBlendFunc(kWin3DBlendSrcAlpha, kWin3DBlendInvSrcAlpha);

        int SavedRs18 = 0;
        DALib::Pipeline->GetRenderState(D3DRS_ALPHAREF, &SavedRs18);
        DALib::Pipeline->SetRenderState(D3DRS_ALPHAREF, 5);

        // The own band when piece < count (+0x154) + base (+0x158); otherwise the first
        // exclusion band that holds it, read unchecked. No band -- nothing drawn.
        int const PrimaryCount = Band.RenderParts;
        int const PrimaryBase = Band.FirstPiece;
        if (PieceIndex < PrimaryCount + PrimaryBase)
        {
            BandDrawPiece(&Band, PieceIndex);
        }
        else
        {
            for (FAsteroidBand** It = BandBegin; It != BandEnd; ++It)
            {
                FAsteroidBand* const ExBand = *It;
                int const ExBase = ExBand->FirstPiece;
                if (PieceIndex >= ExBase && PieceIndex < ExBase + ExBand->RenderParts)
                {
                    BandDrawPiece(ExBand, PieceIndex);
                    break;
                }
            }
        }

        Win3DDisable(kWin3DBlend);
        Win3DEnable(kWin3DDepthTest);
        Win3DSetDepthWrite(1);
        Win3DEnable(kWin3DCullFace);
        DALib::Pipeline->SetRenderState(D3DRS_ALPHAREF, SavedRs18);
    }

    Cam->ZNear = SavedNear;
    Cam->ZFar = SavedFar;
    Win3DApplyCamera(Camera);
}

namespace
{
    constexpr float kBillboardMarginScale = 1.25f; // DAT_005c9804
    constexpr float kAmbientScaleArg = 100.0f;     // pushed into FUN_00522700

    // FUN_0054e040 — zone-proximity gate for the billboard draw. The block is never tested.
    bool BillboardNearCamera(FAsteroidBillboardBlock* Bb, Vector const& CamPos)
    {
        Universe::IZone* Zone = Bb->ZoneRef;
        if (Zone == nullptr)
            return true;

        // Stored as a float.
        float const Margin = (Bb->SphereAhead + Bb->SphereRadius) * kBillboardMarginScale;

        Vector const& ZonePos = Zone->GetPos();
        Vector Dir;
        Dir.x = CamPos.x - ZonePos.x;
        Dir.y = CamPos.y - ZonePos.y;
        Dir.z = CamPos.z - ZonePos.z;

        // Reach and the squared distance are compared unrounded; the float copy of the
        // distance feeds the sqrt below.
        double const Reach = static_cast<double>(Zone->GetRadius()) + Margin;
        double const DistSq = (static_cast<double>(Dir.x) * Dir.x + static_cast<double>(Dir.z) * Dir.z) +
                              static_cast<double>(Dir.y) * Dir.y;
        float const DistSqF = static_cast<float>(DistSq);
        if (DistSq > Reach * Reach)
            return false;

        // No zero test.
        float const Dist = static_cast<float>(sqrt(static_cast<double>(DistSqF)));
        double const Inv = 1.0f / static_cast<double>(Dist);
        Dir.x = static_cast<float>(Dir.x * Inv);
        Dir.y = static_cast<float>(Dir.y * Inv);
        Dir.z = static_cast<float>(Dir.z * Inv);

        // NaN -> false.
        return (static_cast<double>(Dist) - Zone->GetRadiusAlong(Dir)) <= Margin;
    }

    // FUN_00522700 (thiscall on the field) -- light the field around a point Scale along the
    // camera's view axis: StarSys object lighting at that point with radius 250, then the
    // ambient read back, raised by ambient_increase (+0xd0..+0xd2, each channel capped at 0xff)
    // and written again.
    void CliAsteroidAmbientPass(FCliAsteroid* Field, FSpaceCamera* Cam, float Scale)
    {
        ICamera* const Camera = AsCamera(Cam);

        // The orientation's third column times Scale, each product stored as a float.
        const float* const Orient = (const float*)Camera->GetOrientation();
        float const AxisX = Orient[2] * Scale;
        float const AxisY = Orient[5] * Scale;
        float const AxisZ = Orient[8] * Scale;

        // With contains_fog_zone (+0xd3): a field controller (DAT_00675244) with the camera
        // inside it (DAT_0067524c) becomes +0xd4 and its value the fog factor; otherwise
        // +0xd4 = 0. Without the flag +0xd4 is left as it is.
        float FogFactor = 1.0f;
        if (Field->bContainsFogZone != 0)
        {
            void* const Controller = GetSpaceFieldController();
            if (Controller != nullptr && GetSpaceFieldEnclosure() != kFieldEnclosureNone)
            {
                Field->FogNebula = static_cast<unsigned>(reinterpret_cast<uintptr_t>(Controller));
                FogFactor = ((FNebulaEffect*)Controller)->GetInsideFade();
            }
            else
            {
                Field->FogNebula = 0;
            }
        }

        // Port-only, FREELANCER_RP9: while the Direct3D 9 volumetric nebula draws the
        // controller, StarSysApplyObjectLighting skips its fog block
        // (NebulaVolume::ReplacesObjectFog); the ambient lerp stays.
        const float* const CamPos = (const float*)Camera->GetPosition();
        float Point[3];
        Point[0] = AxisX + CamPos[0];
        Point[1] = AxisY + CamPos[1];
        Point[2] = AxisZ + CamPos[2];
        unsigned FogBits = 0;
        memcpy(&FogBits, &FogFactor, sizeof(FogBits));
        SpaceApplyObjectLighting(Point, 250.0f, Cam, Field->FogNebula, FogBits, 1);

        unsigned char Ambient[3];
        EngineLightGetAmbientRgbBytes(Ambient);
        int R = static_cast<int>(Field->AmbientIncrease0) + static_cast<int>(Ambient[0]);
        int G = static_cast<int>(Field->AmbientIncrease1) + static_cast<int>(Ambient[1]);
        int B = static_cast<int>(Field->AmbientIncrease2) + static_cast<int>(Ambient[2]);
        if (R > 0xff)
            R = 0xff;
        if (G > 0xff)
            G = 0xff;
        if (B > 0xff)
            B = 0xff;
        unsigned char Raised[3];
        Raised[0] = static_cast<unsigned char>(R);
        Raised[1] = static_cast<unsigned char>(G);
        Raised[2] = static_cast<unsigned char>(B);
        EngineLightSetAmbientRgbBytes(Raised);
    }

    // FUN_0051feb0 — no null test of Desc or Verts, as retail.
    void CliAsteroidFillMeshDesc(FRpVertexSrcDesc* Desc, void* Verts, unsigned short VertCount)
    {
        unsigned char* const Base = static_cast<unsigned char*>(Verts);
        unsigned int const Count = static_cast<unsigned int>(VertCount);
        Desc->Flags = kRpVertexSrcInterleaved;
        Desc->Position.Base = Base;
        Desc->Position.ByteSize = 0xC;
        Desc->Position.Stride = 0x2C;
        Desc->Position.IndexArray = nullptr;
        Desc->Position.Count = Count;
        Desc->Normal.Base = Base + 0xC;
        Desc->Normal.ByteSize = 0xC;
        Desc->Normal.Stride = 0x2C;
        Desc->Normal.IndexArray = nullptr;
        Desc->Normal.Count = Count;
        Desc->Diffuse.Base = Base + 0x18;
        Desc->Diffuse.ByteSize = 4;
        Desc->Diffuse.Stride = 0x2C;
        Desc->Diffuse.IndexArray = nullptr;
        Desc->Diffuse.Count = Count;
        Desc->Tex[0].Base = Base + 0x1C;
        Desc->Tex[0].ByteSize = 8;
        Desc->Tex[0].Stride = 0x2C;
        Desc->Tex[0].IndexArray = nullptr;
        Desc->Tex[0].Count = Count;
        Desc->Tex[1].Base = Base + 0x24;
        Desc->Tex[1].ByteSize = 8;
        Desc->Tex[1].Stride = 0x2C;
        Desc->Tex[1].IndexArray = nullptr;
        Desc->Tex[1].Count = Count;
        Desc->SrcFvf = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX2;
        Desc->SrcCount = Count;
    }

    // Stride-0x2C merged cube vertex (FVF 0x252): pos/nrm/diff/spec/uv.
    static unsigned const kCubeVertStride = 0x2C;

    // FUN_00520850's local std::list element (node+0x08..+0x14, node 0x18 bytes): the
    // vertices and indices of one asteroid archetype. Retail pushes an all-zero temporary
    // (copied with FUN_0045a080, four dwords); both the temporary and every node at the end
    // free the two buffers, so the element owns them. Built in place here, never copied.
    struct FAsteroidMergeChunk
    {
        unsigned char* Verts;    // node+0x08
        int VertCount;           // node+0x0c
        unsigned short* Indices; // node+0x10
        int IndexCount;          // node+0x14

        FAsteroidMergeChunk()
            : Verts(nullptr)
            , VertCount(0)
            , Indices(nullptr)
            , IndexCount(0)
        {
        }
        FAsteroidMergeChunk(FAsteroidMergeChunk const&) = delete;
        FAsteroidMergeChunk& operator=(FAsteroidMergeChunk const&) = delete;
        ~FAsteroidMergeChunk()
        {
            operator delete(Verts);
            operator delete(Indices);
        }
    };

    // The tail of FUN_00520850 (inline in retail, split out here). Lod is the caller's local
    // (the [Field] "LOD" key, 0 when absent); FieldIniRel is its file argument, named by the
    // WARNING FDUMP.
    void CliAsteroidMergeCubeMesh(FCliAsteroid* Field, int Lod, char const* FieldIniRel)
    {
        std::list<FAsteroidMergeChunk> Chunks;
        int TotalVerts = 0;
        int TotalIdx = 0;

        if (Field->CmnField != nullptr)
        {
            std::list<CmnAsteroid::AsteroidCubeArchEntry> const& ArchList =
                Field->CmnField->ArchList;

            // A material from an earlier load is released and the pointer is left as it was
            // (so it is not queried again below).
            if (Field->Material != nullptr)
                static_cast<IDAComponent*>(Field->Material)->Release();

            for (std::list<CmnAsteroid::AsteroidCubeArchEntry>::const_iterator EntryIt = ArchList.begin();
                 EntryIt != ArchList.end(); ++EntryIt)
            {
                CmnAsteroid::AsteroidCubeArchEntry const& Entry = *EntryIt;
                Archetype::Asteroid* const Arch = Entry.Arch;
                IMesh* Mesh = nullptr;
                IRenderableMesh* RMesh = nullptr;

                int const EngineIndex = static_cast<int>(Arch->EngineIndex);
                if (DALib::Engine->QueryEngineComponent(EngineIndex, Iids::Mesh,
                                                        reinterpret_cast<void**>(&Mesh)) < 0)
                {
                    FDUMP(kFdumpError, "%s(%d) : *** ERROR: IMesh query failed, on asteroid arch (%s)",
                          "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0x28e,
                          Arch->Nickname.get_string());
                }

                // No mesh -> straight to the releases.
                if (Mesh != nullptr)
                {
                    Mesh->OpenFaceGroup(0, kMeshOpenRead);
                    if (Field->Material == nullptr)
                        Mesh->GetFaceGroupMaterial(&Field->Material);
                    Mesh->GetFaceGroupPrimitiveType(&Field->PrimitiveType);
                    Mesh->CloseFaceGroup();

                    // The LOD clamped to [0, count - 1] and kept for the next archetype.
                    int LodCount = 0;
                    DALib::Renderer->GetArchetypeLodCount(
                        static_cast<intptr_t>(EngineIndex), &LodCount);
                    if (Lod + 1 > LodCount)
                        Lod = LodCount - 1;
                    if (Lod < 0)
                        Lod = 0;
                    DALib::Renderer->SetArchetypeLod(
                        static_cast<intptr_t>(EngineIndex), Lod);

                    if (DALib::Engine->QueryEngineComponent(
                            EngineIndex, Iids::RenderableMesh,
                            reinterpret_cast<void**>(&RMesh)) < 0)
                    {
                        FDUMP(kFdumpError,
                              "%s(%d) : *** ERROR: IRenderableMesh query failed, on asteroid arch "
                              "(%s)",
                              "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0x2a7,
                              Arch->Nickname.get_string());
                    }

                    if (RMesh != nullptr)
                    {
                        Chunks.emplace_back();
                        FAsteroidMergeChunk& Chunk = Chunks.back();

                        // Retail's per-vertex texcoord constructors (FUN_0043d950) write nothing.
                        RMesh->GetVertexCount(&Chunk.VertCount);
                        Chunk.Verts = static_cast<unsigned char*>(
                            operator new(static_cast<unsigned>(Chunk.VertCount) * kCubeVertStride));
                        TotalVerts += Chunk.VertCount;
                        RMesh->CopyVertexData(Chunk.Verts, D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX2, 0, Chunk.VertCount);

                        RMesh->GetIndexCount(&Chunk.IndexCount);
                        Chunk.Indices = static_cast<unsigned short*>(
                            operator new(static_cast<unsigned>(Chunk.IndexCount) * 2));
                        RMesh->CopyIndices(&Chunk.Indices, Chunk.IndexCount);
                        TotalIdx += Chunk.IndexCount;

                        // Position and normal rotated by the node's orientation (node+0x18),
                        // then the node's offset (node+0x0c) added to the position.
                        for (unsigned Vi = 0; Vi < static_cast<unsigned>(Chunk.VertCount); ++Vi)
                        {
                            float* const Vert =
                                reinterpret_cast<float*>(Chunk.Verts + Vi * kCubeVertStride);
                            Vector OutPos;
                            CliAsteroidMathEngine()->TransformMatrix(
                                OutPos, Entry.Orient,
                                *reinterpret_cast<Vector const*>(Vert));
                            Vert[0] = OutPos.x;
                            Vert[1] = OutPos.y;
                            Vert[2] = OutPos.z;
                            Vector OutNrm;
                            CliAsteroidMathEngine()->TransformMatrix(
                                OutNrm, Entry.Orient,
                                *reinterpret_cast<Vector const*>(Vert + 3));
                            Vert[3] = OutNrm.x;
                            Vert[4] = OutNrm.y;
                            Vert[5] = OutNrm.z;
                            Vert[0] = Entry.Extent.x + Vert[0];
                            Vert[1] = Vert[1] + Entry.Extent.y;
                            Vert[2] = Vert[2] + Entry.Extent.z;
                        }
                    }
                }

                if (RMesh != nullptr)
                    RMesh->Release();
                if (Mesh != nullptr)
                    Mesh->Release();
            }
        }

        if (Field->Material == nullptr)
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: no material found for asteroids in field (%s)",
                  "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0x2ca, FieldIniRel);
        }

        // Whatever the two pointers held before is not freed, as retail.
        Field->MeshVerts = operator new(static_cast<unsigned>(TotalVerts) * kCubeVertStride);
        Field->MeshIndices = operator new(static_cast<unsigned>(TotalIdx) * 2);

        // Each chunk's indices rebased by the vertices before it (16-bit add).
        int VertBase = 0;
        int IdxBase = 0;
        for (std::list<FAsteroidMergeChunk>::const_iterator It = Chunks.begin(); It != Chunks.end();
             ++It)
        {
            FAsteroidMergeChunk const& Chunk = *It;
            memcpy(static_cast<unsigned char*>(Field->MeshVerts) +
                       static_cast<unsigned>(VertBase) * kCubeVertStride,
                   Chunk.Verts, static_cast<unsigned>(Chunk.VertCount) * kCubeVertStride);
            unsigned short* const Dst = static_cast<unsigned short*>(Field->MeshIndices);
            for (unsigned Ii = 0; Ii < static_cast<unsigned>(Chunk.IndexCount); ++Ii)
            {
                Dst[static_cast<unsigned>(IdxBase) + Ii] = static_cast<unsigned short>(
                    Chunk.Indices[Ii] + static_cast<unsigned short>(VertBase));
            }
            IdxBase += Chunk.IndexCount;
            VertBase += Chunk.VertCount;
        }

        CliAsteroidFillMeshDesc(&Field->MeshDesc, Field->MeshVerts,
                                static_cast<unsigned short>(TotalVerts));
        Field->MeshIndexCount = static_cast<unsigned>(TotalIdx);
    }

    // DAT_00613854 / DAT_00613855 -- .data bytes, initial 1, read only by the near-field draw
    // and written nowhere in the image: the per-cube plane test and the per-cube blend switch
    // both always run.
    unsigned char G_AsteroidPlaneTest = 1;
    unsigned char G_AsteroidAlphaSwitch = 1;
    // DAT_006753e0 / DAT_006753e4 -- .bss dwords read only by the near-field draw and written
    // nowhere: the orientation cache is filled but never read back, and no draw is skipped.
    int G_AsteroidUseOrientCache = 0;
    int G_AsteroidSkipDraw = 0;
    // DAT_006753b4 / DAT_006753b8 / DAT_006753bc -- zeroed by CliAsteroidTickFields and
    // accumulated at the end of the near-field draw: triangles drawn, cubes visited, cubes
    // drawn. Nothing reads the totals.
    unsigned G_AsteroidTrianglesDrawn = 0;
    unsigned G_AsteroidCubesVisited = 0;
    unsigned G_AsteroidCubesDrawn = 0;

    // DAT_005dc3c4 -- a {state, value} table, copied from the PE: COLORVERTEX 0. The blend
    // and opaque tables are shared with the dynamic asteroids (cli_asteroid.h).
    unsigned const kRsAsteroidCull[] = {D3DRS_COLORVERTEX, 0, 0};
    // The inlined {state, value} walk is BillboardApplyRenderStates (asteroid_billboard.h).

    // MSVCRT _ftol (FUN_005b7ec0): truncation toward zero; the callers keep the low dword.
    int AsteroidFtol(double X)
    {
        return static_cast<int>(static_cast<long long>(X));
    }

    // Inlined fifteen times: a snapped coordinate strictly greater than 0 gets + 0.1f
    // (DAT_005d3ee0), anything else (NaN included) - 0.1f, unrounded, then _ftol.
    int AsteroidCubeCoord(float V)
    {
        if (V > 0.0f)
            return AsteroidFtol(static_cast<double>(V) + 0.1f);
        return AsteroidFtol(static_cast<double>(V) - 0.1f);
    }

    // The bound update after every later seed: else-if, as retail.
    void AsteroidExtend(int V, int* Min, int* Max)
    {
        if (V < *Min)
            *Min = V;
        else if (V > *Max)
            *Max = V;
    }

    Vector AsteroidVec(float X, float Y, float Z)
    {
        Vector V;
        V.x = X;
        V.y = Y;
        V.z = Z;
        return V;
    }

    // FUN_00422b80 fed with three products, each rounded to float.
    Vector AsteroidScale(Vector const& V, double S)
    {
        return AsteroidVec(static_cast<float>(V.x * S), static_cast<float>(V.y * S),
                           static_cast<float>(V.z * S));
    }

    // FUN_00423b30 (Out = A + B) and FUN_00423b60 (Out = A - B), per component.
    Vector AsteroidAdd(Vector const& A, Vector const& B)
    {
        return AsteroidVec(A.x + B.x, A.y + B.y, A.z + B.z);
    }

    Vector AsteroidSub(Vector const& A, Vector const& B)
    {
        return AsteroidVec(A.x - B.x, A.y - B.y, A.z - B.z);
    }

    // The side-plane dot product in retail's order: (d.x*v.x + d.z*v.z) + d.y*v.y.
    double AsteroidPlaneDot(Vector const& D, Vector const& V)
    {
        return (static_cast<double>(D.x) * V.x + static_cast<double>(D.z) * V.z) +
               static_cast<double>(D.y) * V.y;
    }

    // The one upload of the field mesh, inlined twice in the near-field draw (first cube, and the
    // camera's own cube). A lock failure FDUMPs with the caller's line (0x524 / 0x580) and the
    // caller returns without the render-state restore. max_alpha (+0xcc) below 1.0 (NaN not)
    // picks the blend states.
    bool AsteroidUploadMesh(FCliAsteroid* Field, FVertexMemoryLock* Desc, int Line)
    {
        IVertexBufferManager* const VbMgr = DALib::VertexBuffer;
        if (VbMgr->LockVertexMemory(Field->MeshDesc.SrcFvf, Field->MeshDesc.SrcCount, Desc) < 0)
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s",
                  "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", Line,
                  "Vertex buffer copy in asteroid field failed");
            return false;
        }
        VbMgr->CopyVertexData(Desc->Vertices, Desc->Listing.Fvf, &Field->MeshDesc, 0, Field->MeshDesc.SrcCount);
        VbMgr->UnlockVertexMemory(Desc);
        if (Field->Material != nullptr)
        {
            Field->Material->Bind(nullptr);
        }
        IRenderPipeline* const Pipe = DALib::Pipeline;
        BillboardApplyRenderStates(Pipe, kRsAsteroidCull);
        if (Field->MaxAlpha < 1.0f)
        {
            BillboardApplyRenderStates(Pipe, kRsAsteroidBlend);
            Pipe->SetRenderState(D3DRS_ALPHAREF, 5);
        }
        else
        {
            BillboardApplyRenderStates(Pipe, kRsAsteroidOpaque);
            Pipe->SetRenderState(D3DRS_ALPHAREF, 0x7f);
        }
        return true;
    }

    // Inlined twice in the near-field draw: pipeline state 7 (read into a local seeded with 1)
    // picks the combined view * cube transform through SetModelview; otherwise SetWorld then
    // SetView.
    void AsteroidBindCubeTransform(Transform const& CubeXform, Transform const& ViewXform)
    {
        IRenderPipeline* const Pipe = DALib::Pipeline;
        int Combined = 1;
        Pipe->GetPipelineState(Render::PipelineStateViewspaceLights, &Combined);
        if (Combined != 0)
        {
            Transform Product;
            CliAsteroidMathEngine()->MulTransform(Product, ViewXform, CubeXform);
            Transform const Copy = Product;
            Pipe->SetModelview(&Copy);
        }
        else
        {
            Pipe->SetWorld(&CubeXform);
            Pipe->SetView(&ViewXform);
        }
    }

    // Inlined twice in the near-field draw: a material alpha below 1.0 (NaN not) applies the
    // blend states once (the state byte); anything else applies the opaque states every time
    // and clears the byte.
    void AsteroidDrawCube(FCliAsteroid* Field, float const* Material, unsigned char* bBlendState,
                          const FVertexMemoryLock* Desc)
    {
        IRenderPipeline* const Pipe = DALib::Pipeline;
        if (G_AsteroidAlphaSwitch != 0)
        {
            if (Material[3] < 1.0)
            {
                if (*bBlendState == 0)
                {
                    BillboardApplyRenderStates(Pipe, kRsAsteroidBlend);
                    Pipe->SetRenderState(D3DRS_ALPHAREF, 5);
                    *bBlendState = 1;
                }
            }
            else
            {
                BillboardApplyRenderStates(Pipe, kRsAsteroidOpaque);
                Pipe->SetRenderState(D3DRS_ALPHAREF, 0x7f);
                *bBlendState = 0;
            }
        }
        Pipe->SetMaterial((const D3DMATERIAL8*)Material);
        if (G_AsteroidSkipDraw == 0)
        {
            Pipe->DrawIndexedPrimitiveVb(Field->PrimitiveType, Desc->Listing.VertexBuffer,
                                         static_cast<unsigned int>(Desc->Listing.BaseVertex),
                                         Field->MeshDesc.SrcCount, Field->MeshIndices,
                                         Field->MeshIndexCount, 0);
        }
    }

    // FUN_00522a60 (thiscall on the field). Two tests only -- the common field (+0x04) and the
    // byte +0x00; nothing on the camera, the zone, the pipeline or the vertex-buffer manager.
#ifdef FREELANCER_RP9
    // Port-only parameters (CliAsteroidTickOne, the volumetric nebula's extended spawn
    // distance): FillOverride > 0 replaces the fill distance (fill_dist * detail) of the cube
    // walk, FadeBandOverride > 0 the width of the alpha fade before the reach (retail: half a
    // cube). Both 0: retail exactly.
    void CliAsteroidDrawNearField(FCliAsteroid* Field, FSpaceCamera* Cam, double FillOverride,
                                  double FadeBandOverride)
#else
    void CliAsteroidDrawNearField(FCliAsteroid* Field, FSpaceCamera* Cam)
#endif
    {
        if (Field->CmnField == nullptr)
            return;
        if (Field->bTestPlacement != 0)
            return;
        ICamera* const Camera = AsCamera(Cam);
        CmnAsteroid::CAsteroidField* const Cmn = Field->CmnField;

        Transform ViewXform;
        memcpy(&ViewXform, Camera->GetViewTransform(), sizeof(ViewXform));

        // DAT_006753b0 bits 0 and 1 guard two function-local statics whose atexit destructors
        // (LAB_00524720 / LAB_00524710) are empty: the cached eye position DAT_00675380 and
        // orientation DAT_0067538c.
        static float s_CachedPos[3];
        static float s_CachedOrient[9];

        // The two angles in radians (DAT_005c77f4), stored as floats; sine and cosine of pi/2
        // (DAT_005dc88c) minus each.
        float const FovY =
            static_cast<float>(static_cast<double>(Camera->GetFovYDeg()) * 0.017453292f);
#ifdef FREELANCER_WIDESCREEN_SUPPORT
        // Port-only Hor+: slot 6 keeps the 4:3 angle, but the side planes of this cull cone must
        // match the frustum, whose horizontal tangent is tan(fovy) * aspect.
        float const HalfFovX = static_cast<float>(
            atan(tan(static_cast<double>(FovY)) * static_cast<double>(Camera->GetAspect())));
#else
        float const HalfFovX =
            static_cast<float>(static_cast<double>(Camera->GetHalfFovXDeg()) * 0.017453292f);
#endif
        double const AngleX = static_cast<double>(1.5707964f) - HalfFovX;
        double const AngleY = static_cast<double>(1.5707964f) - FovY;
        float const SinX = static_cast<float>(sin(AngleX));
        float const CosX = static_cast<float>(cos(AngleX));
        float const SinY = static_cast<float>(sin(AngleY));
        float const CosY = static_cast<float>(cos(AngleY));

        // Retail calls GetHalfFovXDeg once more and discards the result.
        Matrix Orient9;
        memcpy(&Orient9, Camera->GetOrientation(), sizeof(Orient9));
        const float* const Pos3 = (const float*)Camera->GetPosition();
        Vector const CamPos = AsteroidVec(Pos3[0], Pos3[1], Pos3[2]);
        Camera->GetHalfFovXDeg();

        // From the cache while DAT_006753e0 is set (never), otherwise from the camera, refilling
        // the cache.
        Vector Col0;
        Vector Col1;
        Vector Col2;
        Vector Eye;
        if (G_AsteroidUseOrientCache != 0)
        {
            Col0 = AsteroidVec(s_CachedOrient[0], s_CachedOrient[3], s_CachedOrient[6]);
            Col1 = AsteroidVec(s_CachedOrient[1], s_CachedOrient[4], s_CachedOrient[7]);
            Col2 = AsteroidVec(s_CachedOrient[2], s_CachedOrient[5], s_CachedOrient[8]);
            Eye = AsteroidVec(s_CachedPos[0], s_CachedPos[1], s_CachedPos[2]);
        }
        else
        {
            Col0 = AsteroidVec(Orient9.E[0][0], Orient9.E[1][0], Orient9.E[2][0]);
            Col1 = AsteroidVec(Orient9.E[0][1], Orient9.E[1][1], Orient9.E[2][1]);
            Col2 = AsteroidVec(Orient9.E[0][2], Orient9.E[1][2], Orient9.E[2][2]);
            memcpy(s_CachedOrient, &Orient9, sizeof(s_CachedOrient));
            s_CachedPos[0] = CamPos.x;
            s_CachedPos[1] = CamPos.y;
            s_CachedPos[2] = CamPos.z;
            Eye = CamPos;
        }

        // The four side-plane directions.
        Vector const BackX = AsteroidScale(Col2, CosX);
        Vector const D0 = AsteroidSub(AsteroidScale(Col0, -SinX), BackX);
        Vector const D1 = AsteroidSub(AsteroidScale(Col0, SinX), BackX);
        Vector const BackY = AsteroidScale(Col2, CosY);
        Vector const D2 = AsteroidSub(AsteroidScale(Col1, -SinY), BackY);
        Vector const D3 = AsteroidSub(AsteroidScale(Col1, SinY), BackY);

        // 1.25f at DAT_005c9804.
        float const Margin = static_cast<float>(static_cast<double>(Field->CubeSize) * 1.25f);
        Vector const NearPt = AsteroidAdd(Eye, AsteroidScale(Col2, Margin));

        // The tangents are rounded to float, the fill stays unrounded.
        float const TanX = static_cast<float>(tan(static_cast<double>(HalfFovX)));
        float const TanY = static_cast<float>(tan(static_cast<double>(FovY)));
#ifdef FREELANCER_RP9
        double const Fill = FillOverride > 0.0
                                ? FillOverride
                                : static_cast<double>(Field->FillDist) * Field->FillDetailScale;
#else
        double const Fill = static_cast<double>(Field->FillDist) * Field->FillDetailScale;
#endif
        int const ExtX = AsteroidFtol(TanX * Fill);
        int const ExtY = AsteroidFtol(TanY * Fill);

        // The first seed sets both bounds on every axis.
        int MinX = 0;
        int MaxX = 0;
        int MinY = 0;
        int MaxY = 0;
        int MinZ = 0;
        int MaxZ = 0;
        {
            Vector const Seed =
                Cmn->closest_cube_pos(AsteroidAdd(AsteroidScale(Col2, Field->CubeSize), Eye));
            MinX = MaxX = AsteroidCubeCoord(Seed.x);
            MinY = MaxY = AsteroidCubeCoord(Seed.y);
            MinZ = MaxZ = AsteroidCubeCoord(Seed.z);
        }

        // NegZ is kept for the depth test.
        Vector const NegZ = AsteroidVec(-Col2.x, -Col2.y, -Col2.z);
        Vector const FarCentre = AsteroidAdd(AsteroidScale(NegZ, Fill), Eye);
        Vector const LatX = AsteroidScale(Col0, ExtX);
        Vector const LatY = AsteroidScale(Col1, ExtY);

        // Four more seeds at the far corners, in retail order.
        {
            Vector const FarXp = AsteroidAdd(FarCentre, LatX);
            Vector const FarXm = AsteroidSub(FarCentre, LatX);
            Vector Seeds[4];
            Seeds[0] = AsteroidAdd(FarXp, LatY);
            Seeds[1] = AsteroidSub(FarXp, LatY);
            Seeds[2] = AsteroidSub(FarXm, LatY);
            Seeds[3] = AsteroidAdd(FarXm, LatY);
            for (int I = 0; I < 4; ++I)
            {
                Vector const Snapped = Cmn->closest_cube_pos(Seeds[I]);
                AsteroidExtend(AsteroidCubeCoord(Snapped.x), &MinX, &MaxX);
                AsteroidExtend(AsteroidCubeCoord(Snapped.y), &MinY, &MaxY);
                AsteroidExtend(AsteroidCubeCoord(Snapped.z), &MinZ, &MaxZ);
            }
        }

        IRenderPipeline* const Pipe = DALib::Pipeline;
        int Saved18 = 0;
        Pipe->GetRenderState(D3DRS_ALPHAREF, &Saved18);
        unsigned Drawn = 0;
        unsigned Visited = 0;

        // max_alpha (+0xcc) below 1.0f (NaN not) sets both bytes.
        unsigned char bBlendState = Field->MaxAlpha < 1.0f ? 1 : 0;
        unsigned char bFading = Field->MaxAlpha < 1.0f ? 1 : 0;

        // A fading field turns z-writes off, clears the registered lights and sets a white
        // ambient.
        int SavedE = 0;
        Pipe->GetRenderState(D3DRS_ZWRITEENABLE, &SavedE);
        if (bFading != 0)
        {
            Pipe->SetRenderState(D3DRS_ZWRITEENABLE, 0);
            EngineLightClearRegisteredList();
            unsigned char const White[3] = {0xff, 0xff, 0xff};
            EngineLightSetAmbientRgbBytes(White);
        }

        // The 0x44-byte material. Without LIGHTING, or when fading, the ambient is the diffuse,
        // alpha included (still 0 here).
        float Material[17];
        memset(Material, 0, sizeof(Material));
        Material[0] = Field->DiffuseColor0;
        Material[1] = Field->DiffuseColor1;
        Material[2] = Field->DiffuseColor2;
        if (GetGameOption(kGameOptLighting) != 0 && bFading == 0)
        {
            Material[4] = Field->AmbientColor0;
            Material[5] = Field->AmbientColor1;
            Material[6] = Field->AmbientColor2;
        }
        else
        {
            Material[4] = Material[0];
            Material[5] = Material[1];
            Material[6] = Material[2];
            Material[7] = Material[3];
        }

        // 0.5f at DAT_005c75e0. The reach sum is unrounded until stored; the fade start is taken
        // from the unrounded reach.
        float const HalfCube = static_cast<float>(static_cast<double>(Field->CubeSize) * 0.5f);
#ifdef FREELANCER_RP9
        double const ReachD = Fill + Margin; // retail: fill_dist * detail + margin
        float const Reach = static_cast<float>(ReachD);
        // Port-only: the fade band of the extended spawn distance (retail: half a cube).
        double const FadeWidth =
            FadeBandOverride > 0.0 ? FadeBandOverride : static_cast<double>(HalfCube);
        float const FadeStart = static_cast<float>(ReachD - FadeWidth);
#else
        double const ReachD =
            static_cast<double>(Field->FillDist) * Field->FillDetailScale + Margin;
        float const Reach = static_cast<float>(ReachD);
        float const FadeStart = static_cast<float>(ReachD - HalfCube);
#endif

        // The camera's own cube id (from the camera position, not the cached eye).
        unsigned long const PlayerCubeId =
            CmnAsteroid::compute_cube_id(Cmn->closest_cube_pos(CamPos));
        unsigned char bPlayerCubeSeen = 0;
        unsigned char bFirst = 1;
        FVertexMemoryLock Desc = {};
        // The plane test's depth, read by the fade. DAT_00613854 is never cleared, so the test
        // always writes it first.
        double Depth = 0.0;

        // Steps of CubeSize, re-read at every step; signed compares.
        for (int X = MinX; X <= MaxX; X += Field->CubeSize)
        {
            for (int Y = MinY; Y <= MaxY; Y += Field->CubeSize)
            {
                for (int Z = MinZ; Z <= MaxZ; Z += Field->CubeSize)
                {
                    Vector const CubePos = Cmn->closest_cube_pos(AsteroidVec(
                        static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z)));
                    Vector const Delta = AsteroidSub(CubePos, NearPt);
                    ++Visited;

                    if (G_AsteroidPlaneTest != 0)
                    {
                        // Kept unrounded; beyond the reach (strictly) -> next cube.
                        Depth = fabs(AsteroidPlaneDot(NegZ, Delta));
                        if (Depth > Reach)
                            continue;
                        double const Inv = 1.0 / Depth;

                        // Only a slope not within TanX (NaN included) tests a side plane: D0 for
                        // a positive slope, D1 otherwise; a dot ordered-less than 0 -> next cube.
                        double const SlopeX = AsteroidPlaneDot(Delta, Col0) * Inv;
                        if (!(TanX >= fabs(SlopeX)))
                        {
                            if (SlopeX > 0.0)
                            {
                                if (AsteroidPlaneDot(D0, Delta) < 0.0)
                                    continue;
                            }
                            else if (AsteroidPlaneDot(D1, Delta) < 0.0)
                                continue;
                        }

                        double const SlopeY = AsteroidPlaneDot(Delta, Col1) * Inv;
                        if (!(TanY >= fabs(SlopeY)))
                        {
                            if (SlopeY > 0.0)
                            {
                                if (AsteroidPlaneDot(D2, Delta) < 0.0)
                                    continue;
                            }
                            else if (AsteroidPlaneDot(D3, Delta) < 0.0)
                                continue;
                        }
                    }

                    if (!(Field->Zone->GetImmersion(CubePos) > 0.001f)) // DAT_005d1494
                        continue;

                    // An exclusion distance not below 0 replaces the frequency when not
                    // excluded.
                    float Freq = Field->EmptyCubeFreq;
                    unsigned char bNotExcluded = 1;
                    if (Field->ExclusionMgr != nullptr)
                    {
                        float Dist = -1.0f;
                        bNotExcluded = Field->ExclusionMgr->is_excluded(
                                           CubePos, Exclusion::EOT_CliAsteroid, &Dist)
                                           ? 0
                                           : 1;
                        if (bNotExcluded != 0 && Dist >= 0.0f)
                            Freq = Dist;
                    }

                    unsigned long const CubeId = CmnAsteroid::compute_cube_id(CubePos);
                    if (CubeId == 0)
                        continue;
                    if (bNotExcluded == 0)
                        continue;
                    if (CubeId == PlayerCubeId)
                        bPlayerCubeSeen = 1;

                    Matrix Orient(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
                    if (!CmnAsteroid::compute_cube_orientation(CubeId, &Orient, Cmn->Rotations,
                                                               Freq))
                        continue;

                    // The first cube uploads the mesh (byte cleared before the lock); a failed
                    // lock returns.
                    if (bFirst != 0)
                    {
                        bFirst = 0;
                        if (!AsteroidUploadMesh(Field, &Desc, 0x524))
                            return;
                    }

                    Transform CubeXform;
                    memcpy(&CubeXform.Rotation, &Orient, sizeof(CubeXform.Rotation));
                    CubeXform.Translation.x = CubePos.x;
                    CubeXform.Translation.y = CubePos.y;
                    CubeXform.Translation.z = CubePos.z;
                    AsteroidBindCubeTransform(CubeXform, ViewXform);

                    // Past the fade start (ordered less) the alpha fades over half a cube; at or
                    // under 0.001f -> next cube.
                    if (FadeStart < Depth)
                    {
#ifdef FREELANCER_RP9
                        double Alpha =
                            ((static_cast<double>(Reach) - Depth) / FadeWidth) * Field->MaxAlpha;
#else
                        double Alpha =
                            ((static_cast<double>(Reach) - Depth) / HalfCube) * Field->MaxAlpha;
#endif
                        if (Alpha > Field->MaxAlpha)
                            Alpha = Field->MaxAlpha;
                        else if (Alpha < 0.0)
                            Alpha = 0.0f;
                        Material[3] = static_cast<float>(Alpha);
                        if (Alpha <= 0.001f)
                            continue;
                    }
                    else
                    {
                        Material[3] = Field->MaxAlpha;
                    }

                    AsteroidDrawCube(Field, Material, &bBlendState, &Desc);
                    ++Drawn;
                }
            }
        }

        // When the walk never met the camera's own (non-zero) cube, that cube is drawn on its own
        // with the same tests and the full max_alpha (no fade).
        if (bPlayerCubeSeen == 0 && PlayerCubeId != 0)
        {
            Vector const PlayerPos = Cmn->closest_cube_pos(CamPos);
            if (Field->Zone->GetImmersion(PlayerPos) > 0.001f)
            {
                float Freq = Field->EmptyCubeFreq;
                unsigned char bNotExcluded = 1;
                if (Field->ExclusionMgr != nullptr)
                {
                    float Dist = -1.0f;
                    bNotExcluded = Field->ExclusionMgr->is_excluded(
                                       PlayerPos, Exclusion::EOT_CliAsteroid, &Dist)
                                       ? 0
                                       : 1;
                    if (bNotExcluded != 0 && Dist >= 0.0f)
                        Freq = Dist;
                }
                Matrix Orient(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
                if (bNotExcluded != 0 &&
                    CmnAsteroid::compute_cube_orientation(PlayerCubeId, &Orient, Cmn->Rotations,
                                                          Freq))
                {
                    if (bFirst != 0 && !AsteroidUploadMesh(Field, &Desc, 0x580))
                        return;
                    Transform CubeXform;
                    memcpy(&CubeXform.Rotation, &Orient, sizeof(CubeXform.Rotation));
                    CubeXform.Translation.x = PlayerPos.x;
                    CubeXform.Translation.y = PlayerPos.y;
                    CubeXform.Translation.z = PlayerPos.z;
                    AsteroidBindCubeTransform(CubeXform, ViewXform);
                    Material[3] = Field->MaxAlpha;
                    AsteroidDrawCube(Field, Material, &bBlendState, &Desc);
                    ++Drawn;
                }
            }
        }

        Pipe->SetRenderState(D3DRS_ALPHAREF, Saved18);
        Pipe->SetRenderState(D3DRS_ZWRITEENABLE, SavedE);
        G_AsteroidCubesDrawn += Drawn;
        G_AsteroidCubesVisited += Visited;
        G_AsteroidTrianglesDrawn += (Field->MeshIndexCount / 3) * Drawn;
    }

#ifdef FREELANCER_RP9
    // Port-only: CAsteroidField::near_field (Common .text:062a53c0) with an extended fill
    // distance -- the margin (cube + fill) * 1.74f around the zone, the zone's extent along the
    // line to its centre. The common field's own FillDist is left alone (it also drives the
    // server-side asteroid population).
    bool CliAsteroidNearFieldExtended(FCliAsteroid const* Field, Vector const& Pos, double Fill)
    {
        Universe::IZone const* const Zone = Field->Zone;
        if (Zone == nullptr)
            return true;
        double const Margin = (static_cast<double>(Field->CubeSize) + Fill) * 1.74;
        Vector const& Center = Zone->GetPos();
        double const Dx = static_cast<double>(Pos.x) - Center.x;
        double const Dy = static_cast<double>(Pos.y) - Center.y;
        double const Dz = static_cast<double>(Pos.z) - Center.z;
        double const Reach = static_cast<double>(Zone->GetRadius()) + Margin;
        double const DistSq = Dx * Dx + Dy * Dy + Dz * Dz;
        if (DistSq > Reach * Reach)
            return false;
        double const Dist = sqrt(DistSq);
        if (!(Dist > 0.0001))
            return true;
        Vector Dir;
        Dir.x = static_cast<float>(Dx / Dist);
        Dir.y = static_cast<float>(Dy / Dist);
        Dir.z = static_cast<float>(Dz / Dist);
        return Dist - Zone->GetRadiusAlong(Dir) <= Margin;
    }
#endif

    // FUN_005225d0 (thiscall on the field). No null test of the field or the camera.
    void CliAsteroidTickOne(FCliAsteroid* Field, FSpaceCamera* Cam)
    {
        unsigned char bAmbientDone = 0;
        const Vector* const Pos3 = AsCamera(Cam)->GetPosition();
        Vector CamPos;
        CamPos.x = Pos3->x;
        CamPos.y = Pos3->y;
        CamPos.z = Pos3->z;

#ifdef FREELANCER_RP9
        // Port-only: under the Direct3D 9 volumetric nebula (nebula_volume.cpp) the haze is far
        // thinner than retail's linear fog, which hid the end of the cube walk (fill_dist *
        // detail + 1.25 cubes, faded over its last half cube) and the billboards' hand-over
        // there. While a drawn nebula overlaps this field the cube walk goes out to where the
        // haze leaves asteroid_spawn_transmittance (at most asteroid_max_spawn_distance), fades
        // over asteroid_fade_fraction of that depth, the billboards' near fade planes and the
        // exclusion zones' cut-off follow it, and the near-field test widens with it. 0 = retail.
        float SpawnFadeFraction = 0.0f;
        float const SpawnReach = NebulaVolume::AsteroidSpawnReach(Field->Zone, &SpawnFadeFraction);
        double ExtFill = 0.0;
        double ExtFadeBand = 0.0;
        if (SpawnReach > 0.0f)
        {
            double const RetailFill = static_cast<double>(Field->FillDist) * Field->FillDetailScale;
            double const Margin =
                static_cast<double>(static_cast<float>(static_cast<double>(Field->CubeSize) * 1.25f));
            double const HalfCube = static_cast<double>(Field->CubeSize) * 0.5;
            if (static_cast<double>(SpawnReach) - Margin > RetailFill)
            {
                ExtFill = static_cast<double>(SpawnReach) - Margin;
                ExtFadeBand = static_cast<double>(SpawnReach) * SpawnFadeFraction;
                if (ExtFadeBand < HalfCube)
                    ExtFadeBand = HalfCube;
            }
        }
#endif

        // Each gate is read only when the one before passed; ASTEROID_EXT is a signed compare.
        if (Field->bHasBillboards != 0 && GetGameOption(kGameOptAsteroidExt) > 1 &&
            GetGameOption(kGameOptEffectsGroup) != 0 &&
            BillboardNearCamera(&Field->Billboard, CamPos))
        {
            CliAsteroidAmbientPass(Field, Cam, kAmbientScaleArg);
            bAmbientDone = 1;
#ifdef FREELANCER_RP9
            if (ExtFill > 0.0)
            {
                // Port-only: the billboards fade in over the cubes' extended fade band (retail:
                // from the fill distance to the reach, BillboardSetFadePlanes), then the planes
                // go back.
                float const SavedFar = Field->Billboard.NearFadeEnd;
                float const SavedNear = Field->Billboard.NearFadeStart;
                Field->Billboard.NearFadeEnd = SpawnReach;
                Field->Billboard.NearFadeStart = static_cast<float>(SpawnReach - ExtFadeBand);
                Field->Billboard.Draw(Cam, Field->ExclusionMgr, 2);
                Field->Billboard.NearFadeEnd = SavedFar;
                Field->Billboard.NearFadeStart = SavedNear;
            }
            else
#endif
                Field->Billboard.Draw(Cam, Field->ExclusionMgr, 2);
        }

        // After the near-field draw the ambient byte is cleared again when max_alpha (+0xcc) is
        // strictly below 1.0f (DAT_005c75dc; NaN does not clear).
        if (GetGameOption(kGameOptAsteroids) != 0 && GetGameOption(kGameOptObjectsGroup) != 0 &&
            Field->CmnField != nullptr &&
#ifdef FREELANCER_RP9
            (ExtFill > 0.0 ? CliAsteroidNearFieldExtended(Field, CamPos, ExtFill)
                           : Field->CmnField->near_field(CamPos)))
#else
            Field->CmnField->near_field(CamPos))
#endif
        {
            if (bAmbientDone == 0)
            {
                CliAsteroidAmbientPass(Field, Cam, kAmbientScaleArg);
                bAmbientDone = 1;
            }
#ifdef FREELANCER_RP9
            // Port-only: the fading cubes write depth while translucent; the volume adds the
            // medium behind them for the part they let through (their alpha reaches 0 at the
            // view depth ExtFill, see CliAsteroidDrawNearField).
            if (ExtFill > 0.0)
                NebulaVolume::NoteAsteroidFade(static_cast<float>(ExtFill),
                                               static_cast<float>(ExtFadeBand),
                                               static_cast<float>(Field->CubeSize));
            if (ExtFill > 0.0 && Field->ExclusionMgr != nullptr)
            {
                // Port-only: the exclusion zones are cut off at fill * 1.25 from the camera
                // (CliAsteroidApplyDetail, MaxCliDist); the extended walk needs them as far as it
                // goes.
                float const SavedDistSq =
                    FCliAsteroidExclusionInline::GetMaxCliDistSq(Field->ExclusionMgr);
                double const ExtDist = ExtFill * 1.25;
                float const ExtDistSq = static_cast<float>(ExtDist * ExtDist);
                if (ExtDistSq > SavedDistSq)
                    FCliAsteroidExclusionInline::SetMaxCliDistSq(Field->ExclusionMgr, ExtDistSq);
                CliAsteroidDrawNearField(Field, Cam, ExtFill, ExtFadeBand);
                FCliAsteroidExclusionInline::SetMaxCliDistSq(Field->ExclusionMgr, SavedDistSq);
            }
            else
                CliAsteroidDrawNearField(Field, Cam, ExtFill, ExtFadeBand);
#else
            CliAsteroidDrawNearField(Field, Cam);
#endif
            if (Field->MaxAlpha < 1.0f)
                bAmbientDone = 0;
        }

        for (void** It = Field->DynBegin; It != Field->DynEnd; ++It)
            (static_cast<FCliDynamicAsteroid*>(*It))->Draw(Cam, &bAmbientDone,
                                                           Field);
    }
} // namespace

void FCliAsteroid::LightAround(FSpaceCamera* Cam, float Scale)
{
    CliAsteroidAmbientPass(this, Cam, Scale);
}

void CliAsteroidFillVertexSource(FRpVertexSrcDesc* Desc, void* Verts, unsigned short VertCount)
{
    CliAsteroidFillMeshDesc(Desc, Verts, VertCount);
}

void CliAsteroidTickFields(FSpaceCamera* Cam)
{
    // No null test of the camera.
    if (GetGameOption(kGameOptAsteroids) == 0)
        return;
    G_AsteroidTrianglesDrawn = 0;
    G_AsteroidCubesVisited = 0;
    G_AsteroidCubesDrawn = 0;

    std::list<FCliAsteroid*>& List = Registry();
    for (std::list<FCliAsteroid*>::iterator It = List.begin(); It != List.end(); ++It)
        CliAsteroidTickOne(*It, Cam);
}

namespace
{
    // MSVC's signed `n % 2 == 0` idiom, as in FUN_0054c8b0: mask 0x80000001, and for a
    // negative value dec / or 0xfffffffe / inc.
    bool BandDiffIsEven(int Diff)
    {
        unsigned U = static_cast<unsigned>(Diff) & 0x80000001u;
        if (static_cast<int>(U) < 0)
            U = ((U - 1u) | 0xfffffffeu) + 1u;
        return U == 0;
    }

    // FUN_0054c8b0 (thiscall on the band) — builds the particle ring around the zone: the count
    // from zone size * pi / (texture_aspect * height), positions on an ellipse in the zone's XZ
    // plane rotated by its orientation, per-particle normals, then render_parts pieces. Nothing
    // is tested for null. x87 intermediates are modelled as doubles where retail keeps them
    // unrounded. Both allocations use the nothrow form: VC6's operator new returns null on
    // failure (a negative count) instead of throwing.
    void BandBuildExclusionRings(FAsteroidBand* Band)
    {
        if (Band->Particles != nullptr)
            operator delete(Band->Particles);

        Band->bLit = 0;
        Band->UpdateCount = 0;

        Universe::IZone* const Zone = Band->Zone;
        Vector Size = Zone->GetSize();
        // A z extent below 0.1 (NaN not) takes the x extent.
        if (Size.z < 0.1f)
            Size.z = Size.x;

        // ((x + z) * pi) / (aspect * height) + 0.5, truncated, times vert_increase.
        double const CountD = (static_cast<double>(Size.x) + Size.z) * 3.14159274f /
                                  (static_cast<double>(Band->TextureAspect) * Band->Height) +
                              0.5f;
        int const PointCount = AsteroidFtol(CountD) * Band->VertIncrease;
        Band->ParticleCount = PointCount;

        FAsteroidBandParticle* const Particles = static_cast<FAsteroidBandParticle*>(
            operator new(static_cast<size_t>(static_cast<unsigned>(PointCount) * 0x3cu),
                         std::nothrow));
        if (Particles != nullptr)
        {
            for (int I = 0; I < PointCount; ++I)
            {
                FAsteroidBandParticle* const Part = &Particles[I];
                // Two FUN_00422b80 vectors, then three single stores.
                Part->ColorFront[0] = 1.0f;
                Part->ColorFront[1] = 1.0f;
                Part->ColorFront[2] = 1.0f;
                Part->ColorBack[0] = 0.2f;
                Part->ColorBack[1] = 0.2f;
                Part->ColorBack[2] = 0.2f;
                Part->Fade = 1.0f;
                Part->HalfHeight = 1000.0f;
                Part->bFacing = 1;
            }
        }
        Band->Particles = Particles;

        // The fade distances scale with the full height, then the height is halved.
        Band->FadeDist[0] = Band->FadeDist[0] * Band->Height;
        Band->FadeDist[1] = Band->FadeDist[1] * Band->Height;
        Band->FadeDist[2] = Band->FadeDist[2] * Band->Height;
        Band->FadeDist[3] = Band->FadeDist[3] * Band->Height;
        Band->Height = Band->Height * 0.5f;

        // GetSize again, then the orientation and the position.
        Size = Zone->GetSize();
        Matrix const& Orient = Zone->GetOrientation();
        Matrix MathOrient;
        for (int R = 0; R < 3; ++R)
        {
            for (int C = 0; C < 3; ++C)
                MathOrient.E[R][C] = Orient.E[R][C];
        }
        Vector const ZonePos = Zone->GetPos();
        if (Size.z < 0.1f)
            Size.z = Size.x;

        // The ring radii, each rounded to float. Retail also computes the y radius and never
        // reads it.
        float const Offset = Band->Offset;
        float RadiusX = 0.0f;
        float RadiusZ = 0.0f;
        if (Band->bOffsetPercent != 0)
        {
            RadiusX = Size.x * Offset;
            RadiusZ = Offset * Size.z;
        }
        else
        {
            RadiusX = Size.x - Offset;
            RadiusZ = Size.z - Offset;
        }

        // The count is re-read from the band on every pass.
        FAsteroidBandParticle* Part = Particles;
        for (int I = 0; I < Band->ParticleCount; ++I, ++Part)
        {
            // (I / count) * 2pi, unrounded; cos and sin of the same angle (6.2831855f at
            // DAT_005da774).
            double const Angle =
                (static_cast<double>(I) / Band->ParticleCount) * 6.28318548f;
            Vector Local;
            Local.x = static_cast<float>(cos(Angle) * RadiusX);
            Local.y = 0.0f;
            Local.z = static_cast<float>(sin(Angle) * RadiusZ);
            Part->Pos[0] = Local.x;
            Part->Pos[1] = Local.y;
            Part->Pos[2] = Local.z;
            // Retail tests DAT_00679860 inside the loop and acquires it when still null.
            if (G_CloudsMath == nullptr)
                G_CloudsMath = Acquire3DMathEngine();
            Vector World;
            G_CloudsMath->TransformMatrix(World, MathOrient, Local);
            Part->Pos[0] = World.x + ZonePos.x;
            Part->Pos[1] = World.y + ZonePos.y;
            Part->Pos[2] = World.z + ZonePos.z;
            Part->HalfHeight = Band->Height;
        }

        // Each normal: up x (next - this) plus up x (this - previous), each normalised, then the
        // sum normalised (no zero guards). Retail keeps the products with its 0.0 constant
        // (DAT_005c7474), so a NaN offset stays NaN; the roundings follow its stores.
        for (int I = 1; I <= Band->ParticleCount; ++I)
        {
            int const Count = Band->ParticleCount;
            int Prev = I - 2;
            if (Prev < 0)
                Prev += Count;
            int Next = I;
            if (Next >= Count)
                Next -= Count;

            FAsteroidBandParticle* const Cur = &Band->Particles[I - 1];
            float const* const NextPos = Band->Particles[Next].Pos;
            float const* const PrevPos = Band->Particles[Prev].Pos;

            double const DxN = static_cast<double>(NextPos[0]) - Cur->Pos[0];
            double const DyN = static_cast<double>(NextPos[1]) - Cur->Pos[1];
            double const DzN = static_cast<double>(NextPos[2]) - Cur->Pos[2];
            float const DxP = Cur->Pos[0] - PrevPos[0];
            float const DyP = Cur->Pos[1] - PrevPos[1];
            float const DzP = Cur->Pos[2] - PrevPos[2];

            double const ZeroN = DyN * 0.0;
            float const N1x = static_cast<float>(DzN - ZeroN);
            double const N1y = DxN * 0.0 - DzN * 0.0;
            float const N1z = static_cast<float>(static_cast<double>(static_cast<float>(ZeroN)) - DxN);
            float const Inv1 = static_cast<float>(
                1.0 / sqrt((static_cast<double>(N1x) * N1x + static_cast<double>(N1z) * N1z) +
                           N1y * N1y));
            float const N1xU = N1x * Inv1;
            double const N1yU = N1y * Inv1;
            double const N1zU = static_cast<double>(N1z) * Inv1;

            double const ZeroP = static_cast<double>(DyP) * 0.0;
            double const N2x = static_cast<double>(DzP) - ZeroP;
            float const N2y =
                static_cast<float>(static_cast<double>(DxP) * 0.0 - static_cast<double>(DzP) * 0.0);
            float const N2z = static_cast<float>(ZeroP - DxP);
            float const Inv2 = static_cast<float>(
                1.0 / sqrt((N2x * N2x + static_cast<double>(N2z) * N2z) +
                           static_cast<double>(N2y) * N2y));
            double const N2xU = N2x * Inv2;
            double const N2yU = static_cast<double>(N2y) * Inv2;
            float const N2zU = static_cast<float>(static_cast<double>(N2z) * Inv2);

            float const Sx = static_cast<float>(N2xU + N1xU);
            float const Sy = static_cast<float>(N2yU + N1yU);
            double const Sz = static_cast<double>(N2zU) + N1zU;
            double const InvN =
                1.0 / sqrt((static_cast<double>(Sx) * Sx + Sz * Sz) + static_cast<double>(Sy) * Sy);
            Cur->Normal[0] = static_cast<float>(Sx * InvN);
            Cur->Normal[1] = static_cast<float>(Sy * InvN);
            Cur->Normal[2] = static_cast<float>(Sz * InvN);
        }

        // render_parts capped at half the particle count: an unrounded compare (NaN and equal
        // keep it), truncated.
        double const Half = static_cast<double>(Band->ParticleCount) * 0.5f;
        if (static_cast<double>(Band->RenderParts) > Half)
            Band->RenderParts = AsteroidFtol(Half);

        Band->Pieces = static_cast<FAsteroidBandPiece*>(operator new(
            static_cast<size_t>(static_cast<unsigned>(Band->RenderParts) * 0x18u), std::nothrow));

        // Even pieces: count / render_parts rounded to float once (no zero test), the running sum
        // unrounded; each end is the sum + 0.0001f (DAT_005db098), truncated.
        float const Step =
            static_cast<float>(static_cast<double>(Band->ParticleCount) / Band->RenderParts);
        double Acc = 0.0;
        int Start = 0;
        for (int I = 0; I < Band->RenderParts; ++I)
        {
            Acc += Step;
            Band->Pieces[I].FirstParticle = Start;
            Start = AsteroidFtol(Acc + 0.0001f);
            Band->Pieces[I].EndParticle = Start;
        }

        // Each piece's centre and radius.
        for (int I = 0; I < Band->RenderParts; ++I)
        {
            FAsteroidBandPiece* const Piece = &Band->Pieces[I];
            int First = Piece->FirstParticle;
            int End = Piece->EndParticle;
            int const Diff = End - First;
            int Mid = First - AsteroidFtol(static_cast<double>(Diff) * -0.5f);
            int const Count = Band->ParticleCount;
            if (Mid >= Count)
                Mid -= Count;

            if (BandDiffIsEven(Diff))
            {
                // The two middle particles, summed then halved, each step rounded to float.
                float const* const A = Band->Particles[Mid].Pos;
                int Mid2 = Mid + 1;
                if (Mid2 >= Count)
                    Mid2 -= Count;
                float const* const B = Band->Particles[Mid2].Pos;
                Piece->Center[0] = A[0] + B[0];
                Piece->Center[1] = A[1] + B[1];
                Piece->Center[2] = A[2] + B[2];
                Piece->Center[0] = Piece->Center[0] * 0.5f;
                Piece->Center[1] = Piece->Center[1] * 0.5f;
                Piece->Center[2] = Piece->Center[2] * 0.5f;
            }
            else
            {
                float const* const A = Band->Particles[Mid].Pos;
                Piece->Center[0] = A[0];
                Piece->Center[1] = A[1];
                Piece->Center[2] = A[2];
            }

            if (First >= Count)
                First -= Count;
            if (End >= Count)
                End -= Count;

            // The distances to the two end particles, (z*z + y*y) + x*x, unrounded; the larger
            // one is kept (the first on a tie or NaN).
            float const* const P0 = Band->Particles[First].Pos;
            float const* const P1 = Band->Particles[End].Pos;
            double const D0x = static_cast<double>(Piece->Center[0]) - P0[0];
            double const D0y = static_cast<double>(Piece->Center[1]) - P0[1];
            double const D0z = static_cast<double>(Piece->Center[2]) - P0[2];
            double const Dist0 = sqrt((D0z * D0z + D0y * D0y) + D0x * D0x);
            double const D1x = static_cast<double>(Piece->Center[0]) - P1[0];
            double const D1y = static_cast<double>(Piece->Center[1]) - P1[1];
            double const D1z = static_cast<double>(Piece->Center[2]) - P1[2];
            double const Dist1 = sqrt((D1z * D1z + D1y * D1y) + D1x * D1x);
            Piece->Radius = static_cast<float>(Dist1 > Dist0 ? Dist1 : Dist0);
        }
    }
}
