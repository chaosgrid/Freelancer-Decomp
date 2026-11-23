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
#include "Space/Cameras/space_camera.h"
#include "Space/space_scene_obj_draw.h"
#include "Space/star_sys.h"
#include "Interface/win3d_immediate.h"
#include "Interface/win3d_render.h"
#include "x86math.h"

#include <list>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <windows.h>

// FUN_00524880's inlined AsteroidExclusionZoneMgr::set_max_cliasteroid_exclusion_distance:
// the unrounded distance is squared and rounded to float once. Befriended by the class
// (common_net.h).
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
    // atexit runs in reverse order, so the teardown's ShutdownSpaceState -> StarSysLeave ->
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

    // ---- Band blob dword accessors (FUN_0054c580 / 0054c630 / 0054d970 / 0054dab0) ----

    float* BandF(unsigned char* Blob, int DwordIndex)
    {
        return reinterpret_cast<float*>(Blob) + DwordIndex;
    }

    unsigned* BandU(unsigned char* Blob, int DwordIndex)
    {
        return reinterpret_cast<unsigned*>(Blob) + DwordIndex;
    }

    // FUN_0054c580 (thiscall on the 0x84 blob), in retail store order. It writes nothing else:
    // +0x08, +0x7c, +0x80 and the pad bytes +0x75..+0x77 keep whatever the storage held --
    // BandParseIni sets +0x08 / +0x58 / +0x7c / +0x80 when a section is read.
    void InitBandBlob(unsigned char* Blob)
    {
        *BandF(Blob, 3) = 1.0f;         // +0x0c..+0x2c
        *BandF(Blob, 4) = 1.0f;
        *BandF(Blob, 5) = 1.0f;
        *BandF(Blob, 6) = 1.0f;
        *BandF(Blob, 7) = 1.0f;
        *BandF(Blob, 8) = 1.0f;
        *BandF(Blob, 9) = 1.0f;
        *BandF(Blob, 10) = 1.0f;
        *BandF(Blob, 11) = 1.0f;
        *BandU(Blob, 0xc) = 0;          // +0x30
        *BandU(Blob, 0xd) = 0;          // +0x34
        *BandU(Blob, 0xe) = 0;          // +0x38
        *BandU(Blob, 0) = 0;            // +0x00
        *BandU(Blob, 1) = 0;            // +0x04
        *BandU(Blob, 0xf) = 0;          // +0x3c
        *BandU(Blob, 0x11) = 0;         // +0x44
        *BandF(Blob, 0x12) = 1000.0f;   // +0x48
        *BandF(Blob, 0x19) = 1500.0f;   // +0x64
        *BandF(Blob, 0x1a) = 2500.0f;   // +0x68
        *BandF(Blob, 0x1b) = 100000.0f; // +0x6c
        *BandF(Blob, 0x1c) = 130000.0f; // +0x70
        *BandU(Blob, 0x1e) = 0;         // +0x78
        Blob[0x1d * 4] = 0;             // byte +0x74
        *BandF(Blob, 0x10) = 1.0f;      // +0x40
        *BandU(Blob, 0x13) = 1;         // +0x4c
        *BandU(Blob, 0x18) = 0;         // +0x60
        *BandU(Blob, 0x16) = 0;         // +0x58
        *BandU(Blob, 0x14) = 0;         // +0x50
        *BandU(Blob, 0x17) = 0;         // +0x5c
        *BandF(Blob, 0x15) = 1.0f;      // +0x54
    }

    // FUN_0054d970(band, diffuse_tint, ambient_tint)
    void BandApplyTints(unsigned char* Blob, float const* Diffuse, float const* Ambient)
    {
        float* Cs0 = BandF(Blob, 9);
        float* OutD = BandF(Blob, 3);
        float* OutA = BandF(Blob, 6);
        OutD[0] = ClampTintUpper(Cs0[0] * Diffuse[0]);
        OutD[1] = ClampTintUpper(Cs0[1] * Diffuse[1]);
        OutD[2] = ClampTintUpper(Cs0[2] * Diffuse[2]);
        OutA[0] = ClampTintUpper(Cs0[0] * Ambient[0]);
        OutA[1] = ClampTintUpper(Cs0[1] * Ambient[1]);
        OutA[2] = ClampTintUpper(Cs0[2] * Ambient[2]);
    }

    // FUN_0054dab0
    void BandSetAmbientIncrease(unsigned char* Blob, float const* Rgb)
    {
        *BandF(Blob, 0xc) = Rgb[0];
        *BandF(Blob, 0xd) = Rgb[1];
        *BandF(Blob, 0xe) = Rgb[2];
    }

    // FUN_0054c8b0 — ExclusionBand / [Band] ring mesh (thiscall on 0x84 blob).
    void BandBuildExclusionRings(unsigned char* Blob);

    // FUN_0054c630 — [Band] / ExclusionBand value keys into 0x84 blob; `shape` is looked up in
    // the field's own ShapeDB through FUN_00512660 (cmn\ShapeDB.h:0xf2 on a miss).
    void BandParseIni(unsigned char* Blob, INI_Reader& Reader, FShapeDB* Shapes)
    {
        Blob[0x7c] = 0;                 // byte +0x7c (the bytes after it untouched)
        *BandU(Blob, 0x20) = 0;         // +0x80
        *BandF(Blob, 2) = 1.0f;         // +0x08
        *BandU(Blob, 0x16) = 1;         // +0x58

        if (!Reader.read_value())
            return;

        do
        {
            if (Reader.is_value("shape"))
            {
                char const* Name = Reader.get_value_string();
                FTexShape const* Shape = Shapes->Find(Name);
                *reinterpret_cast<FTexShape const**>(Blob) = Shape;
            }
            else if (Reader.is_value("texture_aspect"))
            {
                // 1.15 as a double (DAT_005e0ac0), rounded to float once.
                *BandF(Blob, 0x10) =
                    static_cast<float>(static_cast<double>(Reader.get_value_float(0)) * 1.15);
            }
            else if (Reader.is_value("height"))
            {
                *BandF(Blob, 0x12) = Reader.get_value_float(0);
            }
            else if (Reader.is_value("offset_dist"))
            {
                Blob[0x1d * 4] = 0;
                *BandF(Blob, 0x1e) = Reader.get_value_float(0);
            }
            else if (Reader.is_value("offset_percent"))
            {
                Blob[0x1d * 4] = 1;
                *BandF(Blob, 0x1e) = Reader.get_value_float(0);
            }
            else if (Reader.is_value("color_shift"))
            {
                *BandF(Blob, 9) = Reader.get_value_float(0);
                *BandF(Blob, 10) = Reader.get_value_float(1);
                *BandF(Blob, 11) = Reader.get_value_float(2);
            }
            else if (Reader.is_value("fade"))
            {
                *BandF(Blob, 0x19) = Reader.get_value_float(0);
                *BandF(Blob, 0x1a) = Reader.get_value_float(1);
                *BandF(Blob, 0x1b) = Reader.get_value_float(2);
                *BandF(Blob, 0x1c) = Reader.get_value_float(3);
            }
            else if (Reader.is_value("vert_increase"))
            {
                int V = FloatToIntTrunc(Reader.get_value_float(0));
                if (V < 1)
                    V = 1;
                *BandU(Blob, 0x13) = static_cast<unsigned>(V);
            }
            else if (Reader.is_value("render_parts"))
            {
                int V = FloatToIntTrunc(Reader.get_value_float(0));
                if (V < 1)
                    V = 1;
                *BandU(Blob, 0x16) = static_cast<unsigned>(V);
            }
            else if (Reader.is_value("cull_mode"))
            {
                *BandU(Blob, 0x14) = static_cast<unsigned>(FloatToIntTrunc(Reader.get_value_float(0)));
            }
            else if (Reader.is_value("ambient_intensity"))
            {
                *BandF(Blob, 2) = Reader.get_value_float(0);
            }
        } while (Reader.read_value());
    }

    // FUN_0054c600 -- free the ring particle (+0x3c) and spoke (+0x60) buffers of a band blob;
    // +0x3c/+0x44 and +0x60/+0x58 = 0. No null test of the blob, as retail: the callers (the
    // field destructor and BandVectorClear after a null test, and the embedded +0xfc band) never
    // pass null.
    void BandFreeRingMesh(unsigned char* Blob)
    {
        operator delete(*reinterpret_cast<void**>(Blob + 0x3c));
        *reinterpret_cast<void**>(Blob + 0x3c) = nullptr;
        *BandU(Blob, 0x11) = 0;
        operator delete(*reinterpret_cast<void**>(Blob + 0x60));
        *reinterpret_cast<void**>(Blob + 0x60) = nullptr;
        *BandU(Blob, 0x16) = 0;
    }

    // Inlined in FUN_00520850: every non-null exclusion band freed, then the vector erased
    // (end = begin). The storage is kept.
    void BandVectorClear(FCliAsteroid* This)
    {
        if (This->BandBegin != nullptr && This->BandEnd - This->BandBegin > 0)
        {
            for (unsigned char** It = This->BandBegin; It != This->BandEnd; ++It)
            {
                unsigned char* const Blob = *It;
                if (Blob != nullptr)
                {
                    BandFreeRingMesh(Blob);
                    operator delete(Blob);
                }
            }
            This->BandEnd = This->BandBegin;
        }
    }

    void BandVectorPush(FCliAsteroid* This, unsigned char* Blob)
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
            unsigned char** NewBuf =
                static_cast<unsigned char**>(operator new(sizeof(unsigned char*) * NewCap));
            if (This->BandBegin != nullptr && Count != 0)
                memcpy(NewBuf, This->BandBegin, Count * sizeof(unsigned char*));
            operator delete(This->BandBegin);
            This->BandBegin = NewBuf;
            This->BandEnd = NewBuf + Count;
            This->BandCap = NewBuf + NewCap;
        }
        *This->BandEnd = Blob;
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
                    CliDynamicAsteroidDestroy(static_cast<FCliDynamicAsteroid*>(Dyn));
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
        Diffuse[0] = Parent->TintB4 * Dyn->ColorShift0;
        Diffuse[1] = Dyn->ColorShift1 * Parent->TintB8;
        Diffuse[2] = Dyn->ColorShift2 * Parent->TintBC;
        Dyn->TintedDiffuse0 = Diffuse[0];
        Dyn->TintedDiffuse1 = Diffuse[1];
        Dyn->TintedDiffuse2 = Diffuse[2];

        float Ambient[3];
        CliAsteroidMulColor(Ambient, &Dyn->ColorShift0, &Parent->TintC0);
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
    // BillboardParseIni, Flag7C still 0, so the rebuild only frees) and from the detail slider
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

    // FUN_0054feb0 — Float40 = max(A, B), Float44 = min(A, B). Both callers (the field-ini load
    // and CliAsteroidApplyDetail) pass A = FillDist*Scale230, B = CubeSize*1.25 + A.
    void BillboardSetFadePlanes(FAsteroidBillboardBlock* Bb, float A, float B)
    {
        if (A < B)
        {
            Bb->Float44 = A;
            Bb->Float40 = B;
        }
        else
        {
            Bb->Float40 = A;
            Bb->Float44 = B;
        }
    }

    // FUN_0054fee0(bb, &TintB4, &TintC0).
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
        Bb->Flag7C = 1;

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
        // Flag7C is clear).
        if (Bb->Rocks != nullptr)
            operator delete(Bb->Rocks);

        if (Bb->Flag7C == 0)
            return;

        Bb->RockCount = static_cast<int>(static_cast<double>(Bb->Count) * Bb->QualityA);
        if (Bb->RockCount <= 0)
        {
            // An error, not a return -- the allocation still happens.
            FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
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
            Bb->Float00 = static_cast<float>(R);
            Bb->Float04 = static_cast<float>(R + A);
            Bb->Float2C = static_cast<float>(R * Bb->FadeDistPercent);
        }

        // A shape whose frame-0 handle IsTexture rejects (non-zero) is dropped.
        if (Bb->Shape != nullptr)
        {
            void* const Handle = TexShapeGetGpuHandle(Bb->Shape, 0);
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

    // FUN_00524880 — always writes Scale230; the rest only for a loaded field (Flag2C). No null
    // test of `This`, as retail.
    void CliAsteroidApplyDetail(FCliAsteroid* This, float Detail)
    {
        // (1 - d) is kept as a float for the dynamic block below, while Scale230 uses the
        // unrounded value; 0.65f at DAT_005db53c.
        double const OneMinusDetail = 1.0 - static_cast<double>(Detail);
        float const OneMinusDetailF = static_cast<float>(OneMinusDetail);
        This->Scale230 = static_cast<float>(
            OneMinusDetail * static_cast<double>(0.65f) + static_cast<double>(Detail));
        if (This->Flag2C == 0 || This->CmnField == nullptr)
            return;

        int const SyncedFill = static_cast<int>(
            static_cast<double>(This->FillDistAt24) * static_cast<double>(This->Scale230));
        This->CmnField->set_fill_dist(SyncedFill);

        if (This->Flag198 != 0)
        {
            // FillScaled is rounded to float before the add; 1.25f at DAT_005c9804.
            float const FillScaled = static_cast<float>(
                static_cast<double>(This->FillDistAt24) * static_cast<double>(This->Scale230));
            float const FadeFar = static_cast<float>(
                static_cast<double>(This->CubeSize) * static_cast<double>(1.25f) +
                static_cast<double>(FillScaled));
            BillboardSetFadePlanes(&This->Billboard, FillScaled, FadeFar);
            BillboardSetQuality(&This->Billboard, Detail);
        }

        if (This->ExclusionMgr != nullptr)
        {
            // Retail inlines the setter; the distance is squared unrounded.
            double const CliDist = static_cast<double>(This->FillDistAt24) *
                                   static_cast<double>(This->Scale230) *
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
            CliDynamicAsteroidReleaseSlots(Dyn);
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
    // operator new's result. Stores in retail order. Retail writes no byte of +0x00 (Flag00),
    // +0x30..+0x9f (MeshDesc), +0xb0 (FieldB0) or the pads here; each is written before it is
    // read: Flag00 by the field-ini load (= 0 before the section loop), MeshDesc and FieldB0 by
    // the mesh build inside the same load.
    FCliAsteroid* CliAsteroidConstructRetail(FCliAsteroid* This)
    {
        // The cube map at +0x08 (allocator bytes +0x08 / +0x09, byte +0x14, FUN_005274a0).
        CliAsteroidCubeMapConstruct(This);

        This->TintB4 = 1.0f;
        This->TintB8 = 1.0f;
        This->TintBC = 1.0f;
        This->TintC0 = 1.0f;
        This->TintC4 = 1.0f;
        This->TintC8 = 1.0f;

        // FUN_004a2f10 -- the field's ShapeDB at +0xE0 (its tail store +0xf4 = +0xe4 is part
        // of the container's own construction).
        ShapeDBConstructAt(This->ShapeDbE0);

        InitBandBlob(This->BandBlob);

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

        This->Billboard.Float00 = 2500.0f;
        This->Billboard.StartDist = 1500.0f;
        This->Billboard.Float04 = 4000.0f;
        This->Billboard.Rocks = nullptr;
        This->Billboard.Count = 100;
        This->Billboard.RockCount = 100;
        This->Billboard.ZoneRef = nullptr;
        This->Billboard.DensityScale = 1.0f;
        This->Billboard.Shape = nullptr;
        This->Billboard.SizeMin = 500.0f;
        This->Billboard.SizeMax = 100.0f;
        This->Billboard.Float2C = 700.0f;
        This->Billboard.FadeDistPercent = 0.3f;
        This->Billboard.Float40 = 2000.0f;
        This->Billboard.Float44 = 1500.0f;
        This->Billboard.AmbientIntensity = 1.0f;
        This->Billboard.QualityA = 1.0f;
        This->Billboard.QualityB = 1.0f;
        This->Billboard.Flag7C = 0;

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
            FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
                  "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0x11e,
                  "NO cmn field created for client asteroid field");
        }

        This->CubeSize = 0x190;
        This->FillDistAt24 = 0x4b0;
        This->ScaleCC = 1.0f;
        This->Scale230 = 1.0f;
        This->Flag2C = 0;
        This->FlagD3 = 0;
        This->EmptyCubeFreq = 0.0f;
        This->FlagD0 = 0;
        This->FlagD1 = 0;
        This->FlagD2 = 0;

        // Retail then erases the just-built (empty) cube map; nothing to port.
        This->ExclusionMgr = nullptr;
        This->Material = nullptr;
        This->MeshVerts = nullptr;
        This->MeshIndices = nullptr;
        This->MeshIndexCount = 0;
        This->ZeroD4 = 0;
        This->FloatD8 = 100.0f;

        // Retail also clears the (empty) band and dynamic vectors here.
        This->BandVertSum = 0;

        This->Zone = nullptr;
        This->FlagF8 = 0;
        This->Flag180 = 0;
        This->Flag198 = 0;
        This->Flag234 = 1;
        This->Flag21C = 1;

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
                This->FillDistAt24 = FloatToIntTrunc(Reader.get_value_float(0));
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
                This->TintB4 = R;
                This->TintB8 = G;
                This->TintBC = B;
                This->TintC0 = R;
                This->TintC4 = G;
                This->TintC8 = B;
            }
            else if (Reader.is_value("diffuse_color"))
            {
                This->TintB4 = Reader.get_value_float(0) * KInv255;
                This->TintB8 = Reader.get_value_float(1) * KInv255;
                This->TintBC = Reader.get_value_float(2) * KInv255;
            }
            else if (Reader.is_value("ambient_color"))
            {
                This->TintC0 = Reader.get_value_float(0) * KInv255;
                This->TintC4 = Reader.get_value_float(1) * KInv255;
                This->TintC8 = Reader.get_value_float(2) * KInv255;
            }
            else if (Reader.is_value("ambient_increase"))
            {
                This->FlagD0 = static_cast<unsigned char>(FloatToIntTrunc(Reader.get_value_float(0)));
                This->FlagD1 = static_cast<unsigned char>(FloatToIntTrunc(Reader.get_value_float(1)));
                This->FlagD2 = static_cast<unsigned char>(FloatToIntTrunc(Reader.get_value_float(2)));
            }
            else if (Reader.is_value("empty_cube_frequency"))
            {
                This->EmptyCubeFreq = Reader.get_value_float(0);
            }
            else if (Reader.is_value("max_alpha"))
            {
                This->ScaleCC = Reader.get_value_float(0);
            }
            else if (Reader.is_value("test_asteroid_placement"))
            {
                This->Flag00 = 1;
            }
            else if (Reader.is_value("contains_fog_zone"))
            {
                This->FlagD3 = 1;
            }
        } while (Reader.read_value());
    }
}

FCliAsteroid* CliAsteroidConstruct(FCliAsteroid* This)
{
    return CliAsteroidConstructRetail(This);
}

void CliAsteroidLoadFieldIni(FCliAsteroid* This, char const* FieldIniRel)
{
    // No entry null test of `This` or the file name, as retail.
    INI_Reader Reader;

    char FieldIniPath[MAX_PATH] = {};
    GetFileSysPath(FieldIniPath, FieldIniRel, DALib::Data);

    This->CubeSize = 0xfa;
    This->EmptyCubeFreq = 0.0f;
    int Lod = 0;
    This->ScaleCC = 1.0f;
    This->FlagF8 = 0;
    This->Flag198 = 0;
    This->Flag180 = 0;
    This->Flag00 = 0;

    // A field with no zone is reported and loading goes on.
    if (This->Zone == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: Asteroid field (%s) must be hooked up to a zone",
              "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0x187, FieldIniRel);
    }

    BandVectorClear(This);
    DynVectorClear(This);

    // An open failure is reported and still runs the band/billboard/mesh tail.
    if (!Reader.open(FieldIniPath, false))
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: asteroid field ini file (%s) could not be opened",
              "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0x247, FieldIniRel);
    }
    else
    {
        // The reader's state is saved for the common field.
        This->Flag2C = 1;

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
                    ShapeDBAt(This->ShapeDbE0)->Load(Full, nullptr);
                }
            }
            else if (Reader.is_header("Field"))
            {
                ParseFieldSection(This, Reader, Lod);
            }
            else if (Reader.is_header("Band"))
            {
                // The field's own band (+0xfc).
                This->FlagF8 = 1;
                BandParseIni(This->BandBlob, Reader, ShapeDBAt(This->ShapeDbE0));
                float Diffuse[3] = {This->TintB4, This->TintB8, This->TintBC};
                float Ambient[3] = {This->TintC0, This->TintC4, This->TintC8};
                BandApplyTints(This->BandBlob, Diffuse, Ambient);
                float Inc[3];
                ScaleRgb255(Inc, static_cast<float>(This->FlagD0),
                            static_cast<float>(This->FlagD1), static_cast<float>(This->FlagD2));
                BandSetAmbientIncrease(This->BandBlob, Inc);
            }
            else if (Reader.is_header("ExclusionBand"))
            {
                INI_Reader::State ExState;
                Reader.get_state(ExState);

                unsigned char* Blob = static_cast<unsigned char*>(operator new(0x84));
                if (Blob != nullptr)
                    InitBandBlob(Blob);
                BandVectorPush(This, Blob);

                // The vector's last element, no null test, as retail.
                unsigned char* const Last = This->BandEnd[-1];
                BandParseIni(Last, Reader, ShapeDBAt(This->ShapeDbE0));

                // The section again: every "zone" into +4.
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
                        FDUMP(0x100001,
                              "%s(%d) : *** ERROR: Exband (exclusion zone band) linked to "
                              "missing Zone (%s)",
                              "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0x1ea,
                              Nick);
                    }
                    else
                    {
                        *reinterpret_cast<Universe::IZone const**>(Last + 4) = Z;
                    }
                }
            }
            else if (Reader.is_header("AsteroidBillboards"))
            {
                This->Flag198 = 1;
                BillboardSetQuality(&This->Billboard, AsteroidDetailScalar());
                BillboardParseIni(&This->Billboard, Reader, ShapeDBAt(This->ShapeDbE0));
                float Diffuse[3] = {This->TintB4, This->TintB8, This->TintBC};
                float Ambient[3] = {This->TintC0, This->TintC4, This->TintC8};
                BillboardApplyTints(&This->Billboard, Diffuse, Ambient);
            }
            else if (Reader.is_header("DynamicAsteroids"))
            {
                INI_Reader::State SavedDyn;
                Reader.get_state(SavedDyn);

                FCliDynamicAsteroid* const Dyn = CliDynamicAsteroidConstruct(
                    static_cast<FCliDynamicAsteroid*>(operator new(sizeof(FCliDynamicAsteroid))),
                    This->CmnField);
                DynVectorPush(This, Dyn);

                // InvDetailScale divides by the unrounded value; no zero guard.
                double const Detail = AsteroidDetailScalar();
                double const DetailScale = (1.0 - Detail) * 0.5 + Detail;
                Dyn->DetailScale = static_cast<float>(DetailScale);
                Dyn->InvDetailScale = static_cast<float>(1.0 / DetailScale);

                CliDynamicAsteroidParseIni(Dyn, Reader);

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
                                FDUMP(0x100001,
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
                if (bZoneSet == 0 && This->Zone != nullptr)
                    Dyn->Zone = This->Zone;

                CliDynamicAsteroidApplyParentTints(Dyn, This);
            }
        }

        // The common field loads from the saved state; the fill distance is truncated from the
        // unrounded product.
        if (This->CmnField != nullptr)
        {
            Reader.set_state(Saved);
            This->CmnField->load(Reader, This->CubeSize);
            This->CmnField->set_fill_dist(static_cast<int>(
                static_cast<double>(This->FillDistAt24) * static_cast<double>(This->Scale230)));
            This->CmnField->set_empty_freq(This->EmptyCubeFreq);
            This->ExclusionMgr = This->CmnField->ExclusionMgr;
        }
        else
        {
            FDUMP(0x100001,
                  "%s(%d) : *** ERROR: client asteroid field (%s) must be connected to common "
                  "asteroid field before calling load",
                  "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0x22f, FieldIniRel);
        }

        // With billboards, the count raised to the exclusion manager's maximum and
        // DensityScale = old count / new count (no zero test).
        if (This->Flag198 != 0)
        {
            int Use = This->Billboard.Count;
            if (This->ExclusionMgr != nullptr)
            {
                int const MaxBb = This->ExclusionMgr->get_max_billboard_count();
                if (Use < MaxBb)
                    Use = MaxBb;
                int const OldCount = This->Billboard.Count;
                This->Billboard.Count = Use;
                This->Billboard.DensityScale = static_cast<float>(
                    static_cast<double>(OldCount) / static_cast<double>(Use));
                This->ExclusionMgr->normalize_billboard_density(Use);
            }
            else
            {
                This->Billboard.DensityScale = 1.0f;
            }
        }
    }

    // The field's own band gets the zone (+0x100) and its rings; the spoke cursor starts at its
    // spoke count (+0x154).
    int BandSpokeCursor = 0;
    if (This->FlagF8 != 0)
    {
        *reinterpret_cast<Universe::IZone**>(This->BandBlob + 4) = This->Zone;
        BandBuildExclusionRings(This->BandBlob);
        BandSpokeCursor = static_cast<int>(*BandU(This->BandBlob, 0x16));
    }
    // Every exclusion band gets the tints, the ambient increase, its rings and its spoke base
    // (+0x5c), and adds its spoke count (+0x58) to BandVertSum. No null test of the band, as
    // retail.
    This->BandVertSum = 0;
    if (This->BandBegin != nullptr && This->BandEnd != This->BandBegin)
    {
        This->Flag180 = 1;
        for (unsigned char** It = This->BandBegin; It != This->BandEnd; ++It)
        {
            unsigned char* const Blob = *It;
            float Diffuse[3] = {This->TintB4, This->TintB8, This->TintBC};
            float Ambient[3] = {This->TintC0, This->TintC4, This->TintC8};
            BandApplyTints(Blob, Diffuse, Ambient);
            float Inc[3];
            ScaleRgb255(Inc, static_cast<float>(This->FlagD0), static_cast<float>(This->FlagD1),
                        static_cast<float>(This->FlagD2));
            BandSetAmbientIncrease(Blob, Inc);
            BandBuildExclusionRings(Blob);
            *BandU(Blob, 0x17) = static_cast<unsigned>(BandSpokeCursor);
            int const SpokeN = static_cast<int>(*BandU(Blob, 0x16));
            BandSpokeCursor += SpokeN;
            This->BandVertSum += static_cast<unsigned>(SpokeN);
        }
    }

    // FillScaled is rounded to float before the add.
    if (This->Flag198 != 0)
    {
        This->Billboard.ZoneRef = This->Zone;
        BillboardEnsureRocks(&This->Billboard);
        float const FillScaled = static_cast<float>(
            static_cast<double>(This->FillDistAt24) * static_cast<double>(This->Scale230));
        float const FadeFar = static_cast<float>(
            static_cast<double>(This->CubeSize) * 1.25 + static_cast<double>(FillScaled));
        BillboardSetFadePlanes(&This->Billboard, FillScaled, FadeFar);
    }

    // Retail inlines the two setters (CliAsteroid distance squared into +0x14, DynamicAsteroid
    // distance 100^2 into +0x18).
    if (This->ExclusionMgr != nullptr)
    {
        This->ExclusionMgr->set_max_cliasteroid_exclusion_distance(static_cast<float>(
            static_cast<double>(This->FillDistAt24) * static_cast<double>(This->Scale230) * 1.25));
        This->ExclusionMgr->set_max_dynamicasteroid_exclusion_distance(100.0f);
    }

    // The archetype mesh merge into +0xa0..+0xac and the vertex source.
    CliAsteroidMergeCubeMesh(This, Lod, FieldIniRel);
}

bool CliAsteroidCreateAndLoad(char const* FieldIniRel, char const* ZoneNickname)
{
    // Pushed onto the registry before anything can fail -- a field whose zone is missing stays
    // on the list.
    FCliAsteroid* Obj = static_cast<FCliAsteroid*>(operator new(sizeof(FCliAsteroid)));
    if (Obj != nullptr)
        CliAsteroidConstruct(Obj);
    Registry().push_back(Obj);

    Universe::IZone* Zone = Universe::get_non_const_zone(StarSysGetActiveSystemId(), ZoneNickname);
    if (Zone == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: AsteroidField (%s) linked to missing Zone (%s)",
              "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0xc8, FieldIniRel,
              ZoneNickname);
        return false;
    }

    // Any other zone shape only warns.
    unsigned const Shape = Zone->GetShape();
    if (Shape != Universe::ZoneShapeSphere && Shape != Universe::ZoneShapeEllipsoid)
    {
        FDUMP(0x100002,
              "%s(%d) : *** WARNING: Asteroid field (%s) hooked up to zone (%s) which is not a "
              "valid shape",
              "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0xd1, FieldIniRel,
              ZoneNickname);
    }

    Obj->Zone = Zone;
    if (Obj->CmnField != nullptr)
        Obj->CmnField->set_zone(Zone);

    CliAsteroidApplyDetail(Obj, AsteroidDetailScalar());
    CliAsteroidLoadFieldIni(Obj, FieldIniRel);
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
        CliAsteroidUpdate(*It, static_cast<FSpaceCamera*>(Camera), Dt);
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
            CliAsteroidDestruct(Field);
            operator delete(Field);
        }
    }
    List.clear();
}

void CliAsteroidDestruct(FCliAsteroid* This)
{
    // Explicit releases in retail order, then the inlined member destructors.
    if (This->CmnField != nullptr)
    {
        This->CmnField->Release();
        This->CmnField = nullptr;
    }
    if (This->Material != nullptr)
    {
        static_cast<IDAComponent*>(This->Material)->Release();
        This->Material = nullptr;
    }
    operator delete(This->MeshVerts);
    This->MeshVerts = nullptr;
    operator delete(This->MeshIndices);
    This->MeshIndices = nullptr;
    ShapeDBAt(This->ShapeDbE0)->Clear();
    for (unsigned char** It = This->BandBegin; It != This->BandEnd; ++It)
    {
        unsigned char* const Blob = *It;
        if (Blob != nullptr)
        {
            BandFreeRingMesh(Blob);
            operator delete(Blob);
        }
    }
    This->BandEnd = This->BandBegin;
    for (void** It = This->DynBegin; It != This->DynEnd; ++It)
    {
        FCliDynamicAsteroid* const Dyn = static_cast<FCliDynamicAsteroid*>(*It);
        if (Dyn != nullptr)
        {
            CliDynamicAsteroidDestroy(Dyn);
            operator delete(Dyn);
        }
    }
    This->DynEnd = This->DynBegin;
    This->ExclusionMgr = nullptr;
    operator delete(This->DynBegin);
    This->DynBegin = nullptr;
    This->DynEnd = nullptr;
    This->DynCap = nullptr;
    // The Rocks pointer itself is left as it was.
    operator delete(This->Billboard.Rocks);
    This->Billboard.Count = 0;
    This->Billboard.ZoneRef = nullptr;
    operator delete(This->BandBegin);
    This->BandBegin = nullptr;
    This->BandEnd = nullptr;
    This->BandCap = nullptr;
    BandFreeRingMesh(This->BandBlob);
    ShapeDBDestructAt(This->ShapeDbE0);
    CliAsteroidCubeMapDestruct(This);
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

int CliAsteroidPieceCount(FCliAsteroid* Field)
{
    // No null test, as retail.
    int const Primary =
        static_cast<int>(*reinterpret_cast<unsigned*>(Field->BandBlob + 0x58)); // dword 0x16
    return Primary + static_cast<int>(Field->BandVertSum);
}

namespace
{
    // FUN_0054d920 (thiscall on the band) — piece XYZ from the band's spoke table (+0x60,
    // stride 0x18, XYZ at +8). Index - base (+0x5c), a negative result clamped to 0; nothing is
    // tested for null.
    float* BandGetPiecePos(unsigned char* Blob, int PieceIndex, float* OutXYZ)
    {
        int Rel = PieceIndex - static_cast<int>(*reinterpret_cast<unsigned*>(Blob + 0x5c));
        if (Rel < 0)
            Rel = 0;
        unsigned char* const Table = *reinterpret_cast<unsigned char**>(Blob + 0x60);
        float const* const Entry = reinterpret_cast<float const*>(Table + Rel * 0x18 + 8);
        OutXYZ[0] = Entry[0];
        OutXYZ[1] = Entry[1];
        OutXYZ[2] = Entry[2];
        return OutXYZ;
    }

    // FUN_0054d950 (thiscall on the band) — the radius float at spoke entry +0x14, same
    // index clamp, no null test.
    float BandGetPieceRadius(unsigned char* Blob, int PieceIndex)
    {
        int Rel = PieceIndex - static_cast<int>(*reinterpret_cast<unsigned*>(Blob + 0x5c));
        if (Rel < 0)
            Rel = 0;
        unsigned char* const Table = *reinterpret_cast<unsigned char**>(Blob + 0x60);
        return *reinterpret_cast<float const*>(Table + Rel * 0x18 + 0x14);
    }
} // namespace

namespace
{
    // DAT_006140a4 — two-sided band light soft threshold.
    constexpr float kBandLightSoft = 0.75f;

    // FUN_00410960 (EngineLightSample) as an int.
    int BandSampleDirectionalLight(FEngineLight* Light, float const* ZonePos, float* OutDir3,
                                   float* InOutAmbient3, float* OutColor3)
    {
        return EngineLightSample(Light, ZonePos, OutDir3, InOutAmbient3, OutColor3) ? 1 : 0;
    }

    // FUN_0054cfb0 — bake per-particle lit RGB into +0x1c / +0x28 from nearby lights.
    void BandBakeLighting(unsigned char* Blob)
    {
        if (Blob == nullptr)
            return;

        Universe::IZone* Zone = *reinterpret_cast<Universe::IZone**>(Blob + 4);
        if (Zone == nullptr)
            return;

        float ZonePos[3] = {Zone->Pos.x, Zone->Pos.y, Zone->Pos.z};
        void* LightSlots[16] = {};
        int const LightCount =
            SpaceCollectNearbyLights(LightSlots, 16, ZonePos, 100.0f, 0);
        if (LightCount <= 0)
            return;

        unsigned char const* AmbientBytes = StarSysAmbientColor();
        float const AmbScale = *reinterpret_cast<float*>(Blob + 0x08);
        float AmbR =
            (static_cast<float>(AmbientBytes[0]) * 0.003921569f +
             *reinterpret_cast<float*>(Blob + 0x30)) *
            *reinterpret_cast<float*>(Blob + 0x18);
        float AmbG =
            (static_cast<float>(AmbientBytes[1]) * 0.003921569f +
             *reinterpret_cast<float*>(Blob + 0x34)) *
            *reinterpret_cast<float*>(Blob + 0x1c);
        float AmbB =
            (static_cast<float>(AmbientBytes[2]) * 0.003921569f +
             *reinterpret_cast<float*>(Blob + 0x38)) *
            *reinterpret_cast<float*>(Blob + 0x20);

        float LightPack[4 * 6] = {};
        int DirCount = 0;
        float AmbWork[3] = {AmbR, AmbG, AmbB};
        for (int I = 0; I < LightCount && DirCount < 4; ++I)
        {
            FEngineLight* Light = static_cast<FEngineLight*>(LightSlots[I]);
            if (Light == nullptr || Light->Type != 3)
                continue;
            float* Slot = LightPack + DirCount * 6;
            float Color[3] = {};
            if (BandSampleDirectionalLight(Light, ZonePos, Slot, AmbWork, Color) == 0)
                continue;
            float LenSq = Slot[0] * Slot[0] + Slot[1] * Slot[1] + Slot[2] * Slot[2];
            if (LenSq > 0.0f)
            {
                float const Inv = 1.0f / sqrtf(LenSq);
                Slot[0] *= Inv;
                Slot[1] *= Inv;
                Slot[2] *= Inv;
            }
            Slot[3] = Color[0];
            Slot[4] = Color[1];
            Slot[5] = Color[2];
            ++DirCount;
        }

        AmbR = AmbWork[0] * AmbScale;
        AmbG = AmbWork[1] * AmbScale;
        AmbB = AmbWork[2] * AmbScale;

        int const PointCount = static_cast<int>(*reinterpret_cast<unsigned*>(Blob + 0x44));
        float* Particles = *reinterpret_cast<float**>(Blob + 0x3c);
        if (Particles == nullptr || PointCount <= 0)
        {
            Blob[0x7c] = 1;
            return;
        }

        float const DiffR = *reinterpret_cast<float*>(Blob + 0x0c);
        float const DiffG = *reinterpret_cast<float*>(Blob + 0x10);
        float const DiffB = *reinterpret_cast<float*>(Blob + 0x14);

        for (int P = 0; P < PointCount; ++P)
        {
            float* Nrm = Particles + P * 15 + 5;
            float SideA[3] = {AmbR, AmbG, AmbB};
            float SideB[3] = {AmbR, AmbG, AmbB};
            for (int L = 0; L < DirCount; ++L)
            {
                float const* Pack = LightPack + L * 6;
                float const Dot =
                    Pack[0] * Nrm[0] + Pack[1] * Nrm[1] + Pack[2] * Nrm[2];
                if (-kBandLightSoft < Dot)
                {
                    float const T = (kBandLightSoft + Dot) / (kBandLightSoft + 1.0f);
                    SideB[0] += Pack[3] * DiffR * T;
                    SideB[1] += Pack[4] * DiffG * T;
                    SideB[2] += Pack[5] * DiffB * T;
                }
                if (-kBandLightSoft < -Dot)
                {
                    float const T = (-Dot + kBandLightSoft) / (kBandLightSoft + 1.0f);
                    SideA[0] += Pack[3] * DiffR * T;
                    SideA[1] += Pack[4] * DiffG * T;
                    SideA[2] += Pack[5] * DiffB * T;
                }
            }
            for (int C = 0; C < 3; ++C)
            {
                if (SideA[C] > 1.0f)
                    SideA[C] = 1.0f;
                if (SideB[C] > 1.0f)
                    SideB[C] = 1.0f;
            }
            float* Dst = Particles + P * 15;
            Dst[7] = SideA[0];
            Dst[8] = SideA[1];
            Dst[9] = SideA[2];
            Dst[10] = SideB[0];
            Dst[11] = SideB[1];
            Dst[12] = SideB[2];
        }
        Blob[0x7c] = 1;
    }

    // FUN_0054c420 (thiscall on one 0x3c-byte spoke) -- the spoke's fade (+0x34) and facing
    // flag (+0x38) from the camera position and the band's four squared distances. Spoke:
    // position +0x00, axis +0x10. No null tests.
    void BandUpdateSpokeFade(unsigned char* Spoke, Vector const* CamPos, float InnerSq,
                             float NearSq, float FarSq, float OuterSq)
    {
        float* const Pos = reinterpret_cast<float*>(Spoke);
        float* const Axis = reinterpret_cast<float*>(Spoke + 0x10);
        float* const Fade = reinterpret_cast<float*>(Spoke + 0x34);

        // Only the offsets are rounded to float; the squared length (x*x + z*z, then + y*y)
        // stays unrounded.
        float const Dx = CamPos->x - Pos[0];
        float const Dy = CamPos->y - Pos[1];
        float const Dz = CamPos->z - Pos[2];
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

        // The unit offset dotted with the axis, summed z, y, then x.
        double const Inv = 1.0 / sqrt(DistSq);
        double const Dot = (Dz * Inv * Axis[2] + Dy * Inv * Axis[1]) + Dx * Inv * Axis[0];
        Spoke[0x38] = (Dot > 0.0) ? 1 : 0;
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

    // FUN_0054d410 (thiscall on the band) -- the band's per-frame update from CliAsteroidUpdate:
    // the lighting baked on the second call (+0x80 counts up while +0x7c is clear), then, unless
    // the fraction (+0x54) is below 0.001 (DAT_005c75f8), every spoke's fade against the squares
    // of the four distances +0x64..+0x70.
    void BandUpdate(unsigned char* Blob, Vector const* CamPos)
    {
        if (Blob[0x7c] == 0)
        {
            int* const Calls = reinterpret_cast<int*>(Blob + 0x80);
            *Calls = *Calls + 1;
            if (*Calls > 1)
                BandBakeLighting(Blob);
        }

        if (*BandF(Blob, 0x54 / 4) < 0.001)
            return;

        float const A = *BandF(Blob, 0x64 / 4);
        float const B = *BandF(Blob, 0x68 / 4);
        float const C = *BandF(Blob, 0x6c / 4);
        float const D = *BandF(Blob, 0x70 / 4);
        float const ASq = A * A;
        float const BSq = B * B;
        float const CSq = C * C;
        float const DSq = D * D;
        int const SpokeCount = *reinterpret_cast<int*>(Blob + 0x44);
        unsigned char* const Spokes = *reinterpret_cast<unsigned char**>(Blob + 0x3c);
        for (int Index = 0; Index < SpokeCount; ++Index)
            BandUpdateSpokeFade(Spokes + Index * 0x3c, CamPos, ASq, BSq, CSq, DSq);
    }

    // FUN_0054d4e0 (thiscall on the band) — Win3D triangle-pair quads for one spoke piece index.
    // Retail's first stack argument (the camera position CliAsteroidDrawFarPiece passes) is
    // never read, so the port drops it. Nothing is tested for null.
    void BandDrawPiece(unsigned char* Blob, int PieceIndex)
    {
        if (Blob[0x7c] == 0)
            BandBakeLighting(Blob);

        // Rel = piece - base (+0x5c); >= count (+0x58) or < 0 -> 0.
        int Rel = PieceIndex - static_cast<int>(*reinterpret_cast<unsigned*>(Blob + 0x5c));
        int const RenderParts = static_cast<int>(*reinterpret_cast<unsigned*>(Blob + 0x58));
        if (Rel >= RenderParts || Rel < 0)
            Rel = 0;

        // 0.01f at DAT_005c99c8; NaN goes on.
        float const MaxAlpha = *reinterpret_cast<float*>(Blob + 0x54);
        if (MaxAlpha < 0.01f)
            return;

        int const VertInc = static_cast<int>(*reinterpret_cast<unsigned*>(Blob + 0x4c));
        float const InvVertInc = static_cast<float>(1.0 / static_cast<double>(VertInc));
        int const PointCount = static_cast<int>(*reinterpret_cast<unsigned*>(Blob + 0x44));
        int const CullMode = static_cast<int>(*reinterpret_cast<unsigned*>(Blob + 0x50));
        float* Particles = *reinterpret_cast<float**>(Blob + 0x3c);
        unsigned char* Spokes = *reinterpret_cast<unsigned char**>(Blob + 0x60);

        // A shape at +0 binds its frame-0 handle, null included.
        FTexShape const* Shape = *reinterpret_cast<FTexShape const**>(Blob);
        if (Shape != nullptr)
            Win3DBindTexture(TexShapeGetGpuHandle(Shape, 0));

        int const EndIdx = *reinterpret_cast<int*>(Spokes + Rel * 0x18 + 4);
        int const StartIdx = *reinterpret_cast<int*>(Spokes + Rel * 0x18);
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

            int Idx = StartIdx;
            int Remaining = EndIdx - StartIdx;
            int PartOff = Idx * 0x3c;
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

                unsigned char* PartBase = reinterpret_cast<unsigned char*>(Particles);
                unsigned char const FlagA = PartBase[PartOff + 0x38];
                unsigned char const FlagB = PartBase[WrapIdx * 0x3c + 0x38];

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
                    float* PartA = reinterpret_cast<float*>(PartBase + PartOff);
                    float* PartB = Particles + WrapIdx * 15;
                    float AlphaA = PartA[13];
                    float AlphaB = PartB[13];
                    float* ColorA = nullptr;
                    float* ColorB = nullptr;
                    if (FlagA == 0)
                    {
                        if (CullMode == 1)
                            AlphaA = 0.0f;
                        ColorA = PartA + 10;
                    }
                    else
                    {
                        if (CullMode == 2)
                            AlphaA = 0.0f;
                        ColorA = PartA + 7;
                    }
                    if (FlagB == 0)
                    {
                        if (CullMode == 1)
                            AlphaB = 0.0f;
                        ColorB = PartB + 10;
                    }
                    else
                    {
                        if (CullMode == 2)
                            AlphaB = 0.0f;
                        ColorB = PartB + 7;
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
                        float const HalfH = PartA[3];
                        float const Ax = PartA[0];
                        float const Ay = PartA[1];
                        float const Az = PartA[2];
                        float const Bx = PartB[0];
                        float const By = PartB[1];
                        float const Bz = PartB[2];

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

                PartOff += 0x3c;
                ++UCursor;
                ++UNext;
                --Remaining;
            } while (Remaining != 0);
        }
        Win3DEnd();
    }
} // namespace

void CliAsteroidUpdate(FCliAsteroid* This, FSpaceCamera* Cam, float Dt)
{
    // Once (Flag21C, set by the constructor), with a zone: the nebula at the zone's position
    // into +0xd4. The flag is cleared either way.
    if (This->Flag21C != 0 && This->Zone != nullptr)
    {
        This->ZeroD4 = static_cast<unsigned>(reinterpret_cast<uintptr_t>(
            StarSysFindNebula(&This->Zone->GetPos(), nullptr, nullptr)));
    }
    This->Flag21C = 0;

    float const* const Pos3 = AsCamera(Cam)->GetPosition();
    Vector CamPos;
    CamPos.x = Pos3[0];
    CamPos.y = Pos3[1];
    CamPos.z = Pos3[2];

    if (GetGameOption(kGameOptAsteroids) == 0)
        return;

    // The exclusion timer (+0xd8): above 0.5 (DAT_00613848) or on a camera cut (the camera's
    // +0xbc byte), the exclusion manager is re-sorted around the camera.
    This->FloatD8 += Dt;
    if (This->FloatD8 > 0.5f || Cam->FlagBC != 0)
    {
        This->FloatD8 = 0.0f;
        if (This->ExclusionMgr != nullptr)
            This->ExclusionMgr->sort(&CamPos);
    }

    if (GetGameOption(kGameOptObjectsGroup) != 0 && This->CmnField != nullptr)
    {
        FCliAsteroidCubeMap& Cubes = CliAsteroidCubeMap(This);

        // Every change event of the common field, in list order.
        for (CmnAsteroid::asteroid_marker* Event = This->CmnField->get_changes();
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
                CmnAsteroid::CAsteroidCube* const Cube = This->CmnField->find_cube(Event->CubeId);
                if (Cube != nullptr)
                {
                    Value.Cube = Cube;
                    Value.CubeId = Cube->get_id();
                    Value.Orient = Cube->Orient;
                    Value.Pos = Cube->Pos;
                    Value.CubeSize = This->CubeSize;
                }
                else
                {
                    FDUMP(0x100002, "%s(%d) : *** WARNING: %s",
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
        for (void** DynIt = This->DynBegin; DynIt != This->DynEnd; ++DynIt)
        {
            CliDynamicAsteroidUpdate(static_cast<FCliDynamicAsteroid*>(*DynIt), Cam, Dt,
                                     This->ExclusionMgr);
        }
    }

    // [EFFECTS], the common field, ASTEROID_EXT in (0, 3) (signed compares).
    if (GetGameOption(kGameOptEffectsGroup) == 0 || This->CmnField == nullptr)
        return;
    if (GetGameOption(kGameOptAsteroidExt) <= 0)
        return;
    if (GetGameOption(kGameOptAsteroidExt) >= 3)
        return;

    if (This->FlagF8 != 0)
        BandUpdate(This->BandBlob, &CamPos);

    // Exclusion bands: one near-field fraction into every band's +0x54.
    if (This->Flag180 == 0)
        return;
    float const Fraction = This->CmnField->near_field_fraction(CamPos, 1000.0f);
    for (unsigned char** It = This->BandBegin; It != This->BandEnd; ++It)
    {
        *BandF(*It, 0x15) = Fraction;
        BandUpdate(*It, &CamPos);
    }
}

float* CliAsteroidGetPiecePos(FCliAsteroid* Field, int PieceIndex, float* OutXYZ)
{
    // A piece in [0, band count +0x154) comes from the field's own band.
    int const Primary = static_cast<int>(*reinterpret_cast<unsigned*>(Field->BandBlob + 0x58));
    if (PieceIndex < Primary && PieceIndex >= 0)
        return BandGetPiecePos(Field->BandBlob, PieceIndex, OutXYZ);

    // Otherwise the first exclusion band with base (+0x5c) <= piece < base + count (+0x58);
    // entries are read unchecked.
    for (unsigned char** It = Field->BandBegin; It != Field->BandEnd; ++It)
    {
        unsigned char* const Blob = *It;
        int const Base = static_cast<int>(*reinterpret_cast<unsigned*>(Blob + 0x5c));
        if (PieceIndex >= Base &&
            PieceIndex < Base + static_cast<int>(*reinterpret_cast<unsigned*>(Blob + 0x58)))
            return BandGetPiecePos(Blob, PieceIndex, OutXYZ);
    }

    FDUMP(0x100002, "%s(%d) : *** WARNING: %s",
          "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0x699,
          "bad piece number supplied to get_piece_pos in asteroid field");
    OutXYZ[0] = 0.0f;
    OutXYZ[1] = 0.0f;
    OutXYZ[2] = 0.0f;
    return OutXYZ;
}

float CliAsteroidGetPieceRadius(FCliAsteroid* Field, int PieceIndex)
{
    // Same band lookup as CliAsteroidGetPiecePos.
    int const Primary = static_cast<int>(*reinterpret_cast<unsigned*>(Field->BandBlob + 0x58));
    if (PieceIndex < Primary && PieceIndex >= 0)
        return BandGetPieceRadius(Field->BandBlob, PieceIndex);

    for (unsigned char** It = Field->BandBegin; It != Field->BandEnd; ++It)
    {
        unsigned char* const Blob = *It;
        int const Base = static_cast<int>(*reinterpret_cast<unsigned*>(Blob + 0x5c));
        if (PieceIndex >= Base &&
            PieceIndex < Base + static_cast<int>(*reinterpret_cast<unsigned*>(Blob + 0x58)))
            return BandGetPieceRadius(Blob, PieceIndex);
    }

    FDUMP(0x100002, "%s(%d) : *** WARNING: %s",
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

void CliAsteroidDrawFarPiece(FCliAsteroid* Field, FSpaceCamera* Cam, int PieceIndex)
{
    // Nothing is tested for null.
    if (Field->FlagF8 == 0 && Field->Flag180 == 0)
        return;
    if (GetGameOption(kGameOptAsteroidExt) == 0)
        return;
    if (GetGameOption(kGameOptEffectsGroup) == 0)
        return;
    if (GetGameOption(kGameOptAsteroidExt) >= 3)
        return;
    if (Field->Flag234 == 0)
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
        Win3DSetBlendFunc(5, 6);

        int SavedRs18 = 0;
        DALib::Pipeline->GetRenderState(D3DRS_ALPHAREF, &SavedRs18);
        DALib::Pipeline->SetRenderState(D3DRS_ALPHAREF, 5);

        // The own band when piece < count (+0x154) + base (+0x158); otherwise the first
        // exclusion band that holds it, read unchecked. No band -- nothing drawn.
        int const PrimaryCount =
            static_cast<int>(*reinterpret_cast<unsigned*>(Field->BandBlob + 0x58));
        int const PrimaryBase =
            static_cast<int>(*reinterpret_cast<unsigned*>(Field->BandBlob + 0x5c));
        if (PieceIndex < PrimaryCount + PrimaryBase)
        {
            BandDrawPiece(Field->BandBlob, PieceIndex);
        }
        else
        {
            for (unsigned char** It = Field->BandBegin; It != Field->BandEnd; ++It)
            {
                unsigned char* const ExBlob = *It;
                int const ExBase =
                    static_cast<int>(*reinterpret_cast<unsigned*>(ExBlob + 0x5c));
                if (PieceIndex >= ExBase &&
                    PieceIndex < ExBase + static_cast<int>(*reinterpret_cast<unsigned*>(ExBlob + 0x58)))
                {
                    BandDrawPiece(ExBlob, PieceIndex);
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
        float const Margin = (Bb->Float04 + Bb->Float00) * kBillboardMarginScale;

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
        float const* const Orient = Camera->GetOrientation();
        float const AxisX = Orient[2] * Scale;
        float const AxisY = Orient[5] * Scale;
        float const AxisZ = Orient[8] * Scale;

        // With contains_fog_zone (+0xd3): a field controller (DAT_00675244) with the camera
        // inside it (DAT_0067524c) becomes +0xd4 and its value the fog factor; otherwise
        // +0xd4 = 0. Without the flag +0xd4 is left as it is.
        float FogFactor = 1.0f;
        if (Field->FlagD3 != 0)
        {
            void* const Controller = GetSpaceFieldController();
            if (Controller != nullptr && GetSpaceFieldEnclosure() != 0)
            {
                Field->ZeroD4 = static_cast<unsigned>(reinterpret_cast<uintptr_t>(Controller));
                using FControllerFloatFn = float(__thiscall*)(void*);
                FogFactor = ((FNebulaEffect*)Controller)->GetFloat294();
            }
            else
            {
                Field->ZeroD4 = 0;
            }
        }

        // Port-only, FREELANCER_RP9: while the Direct3D 9 volumetric nebula draws the
        // controller, StarSysApplyObjectLighting skips its fog block
        // (NebulaVolume::ReplacesObjectFog); the ambient lerp stays.
        float const* const CamPos = Camera->GetPosition();
        float Point[3];
        Point[0] = AxisX + CamPos[0];
        Point[1] = AxisY + CamPos[1];
        Point[2] = AxisZ + CamPos[2];
        unsigned FogBits = 0;
        memcpy(&FogBits, &FogFactor, sizeof(FogBits));
        SpaceApplyObjectLighting(Point, 250.0f, Cam, Field->ZeroD4, FogBits, 1);

        unsigned char Ambient[3];
        EngineLightGetAmbientRgbBytes(Ambient);
        int R = static_cast<int>(Field->FlagD0) + static_cast<int>(Ambient[0]);
        int G = static_cast<int>(Field->FlagD1) + static_cast<int>(Ambient[1]);
        int B = static_cast<int>(Field->FlagD2) + static_cast<int>(Ambient[2]);
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
        Desc->Flags = 2;
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
        Desc->SrcFvf = 0x252;
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
                if (DALib::Engine->QueryEngineComponent(EngineIndex, const_cast<char*>(Iids::Mesh),
                                                        reinterpret_cast<void**>(&Mesh)) < 0)
                {
                    FDUMP(0x100001, "%s(%d) : *** ERROR: IMesh query failed, on asteroid arch (%s)",
                          "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0x28e,
                          Arch->Nickname.get_string());
                }

                // No mesh -> straight to the releases.
                if (Mesh != nullptr)
                {
                    Mesh->OpenFaceGroup(0, 1);
                    if (Field->Material == nullptr)
                        Mesh->GetFaceGroupMaterial(&Field->Material);
                    Mesh->GetFaceGroupPrimitiveType(&Field->FieldB0);
                    Mesh->CloseFaceGroup();

                    // The LOD clamped to [0, count - 1] and kept for the next archetype.
                    int LodCount = 0;
                    DALib::Renderer->GetArchetypeLodCount(
                        reinterpret_cast<void*>(static_cast<intptr_t>(EngineIndex)), &LodCount);
                    if (Lod + 1 > LodCount)
                        Lod = LodCount - 1;
                    if (Lod < 0)
                        Lod = 0;
                    DALib::Renderer->SetArchetypeLod(
                        reinterpret_cast<void*>(static_cast<intptr_t>(EngineIndex)), Lod);

                    if (DALib::Engine->QueryEngineComponent(
                            EngineIndex, const_cast<char*>(Iids::RenderableMesh),
                            reinterpret_cast<void**>(&RMesh)) < 0)
                    {
                        FDUMP(0x100001,
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
                        RMesh->CopyVertexData(Chunk.Verts, 0x252u, 0, Chunk.VertCount);

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
                            FVector OutPos;
                            CliAsteroidMathEngine()->TransformMatrix(
                                OutPos, reinterpret_cast<FMatrix const&>(Entry.Orient),
                                *reinterpret_cast<FVector const*>(Vert));
                            Vert[0] = OutPos.X;
                            Vert[1] = OutPos.Y;
                            Vert[2] = OutPos.Z;
                            FVector OutNrm;
                            CliAsteroidMathEngine()->TransformMatrix(
                                OutNrm, reinterpret_cast<FMatrix const&>(Entry.Orient),
                                *reinterpret_cast<FVector const*>(Vert + 3));
                            Vert[3] = OutNrm.X;
                            Vert[4] = OutNrm.Y;
                            Vert[5] = OutNrm.Z;
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
            FDUMP(0x100002, "%s(%d) : *** WARNING: no material found for asteroids in field (%s)",
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

    // DAT_005dc3c4 / DAT_005dc394 / DAT_005dc374 -- {state, value} tables, copied from the PE.
    unsigned long const kRsAsteroidCull[] = {0x8d, 0, 0};
    unsigned long const kRsAsteroidBlend[] = {0x89, 1, 0x1d, 0, 0x1b, 1, 0x13, 5, 0x14, 6, 0};
    unsigned long const kRsAsteroidOpaque[] = {0x89, 1, 0x1d, 0, 0x1b, 0, 0};

    // The inlined {state, value} walk.
    void AsteroidApplyRs(IRenderPipeline* Pipe, unsigned long const* Table)
    {
        for (unsigned long const* Pair = Table; Pair[0] != 0; Pair += 2)
            Pipe->SetRenderState((D3DRENDERSTATETYPE)Pair[0], static_cast<int>(Pair[1]));
    }

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
            FDUMP(0x100002, "%s(%d) : *** WARNING: %s",
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
        AsteroidApplyRs(Pipe, kRsAsteroidCull);
        if (Field->ScaleCC < 1.0f)
        {
            AsteroidApplyRs(Pipe, kRsAsteroidBlend);
            Pipe->SetRenderState(D3DRS_ALPHAREF, 5);
        }
        else
        {
            AsteroidApplyRs(Pipe, kRsAsteroidOpaque);
            Pipe->SetRenderState(D3DRS_ALPHAREF, 0x7f);
        }
        return true;
    }

    // Inlined twice in the near-field draw: pipeline state 7 (read into a local seeded with 1)
    // picks the combined view * cube transform through SetModelview; otherwise SetWorld then
    // SetView.
    void AsteroidBindCubeTransform(FTransform const& CubeXform, FTransform const& ViewXform)
    {
        IRenderPipeline* const Pipe = DALib::Pipeline;
        int Combined = 1;
        Pipe->GetPipelineState(7, &Combined);
        if (Combined != 0)
        {
            FTransform Product;
            CliAsteroidMathEngine()->MulTransform(Product, ViewXform, CubeXform);
            FTransform const Copy = Product;
            Pipe->SetModelview(reinterpret_cast<float const*>(&Copy));
        }
        else
        {
            Pipe->SetWorld(reinterpret_cast<float const*>(&CubeXform));
            Pipe->SetView(reinterpret_cast<float const*>(&ViewXform));
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
                    AsteroidApplyRs(Pipe, kRsAsteroidBlend);
                    Pipe->SetRenderState(D3DRS_ALPHAREF, 5);
                    *bBlendState = 1;
                }
            }
            else
            {
                AsteroidApplyRs(Pipe, kRsAsteroidOpaque);
                Pipe->SetRenderState(D3DRS_ALPHAREF, 0x7f);
                *bBlendState = 0;
            }
        }
        Pipe->SetMaterial((const D3DMATERIAL8*)Material);
        if (G_AsteroidSkipDraw == 0)
        {
            Pipe->DrawIndexedPrimitiveVb(Field->FieldB0, Desc->Listing.VertexBuffer,
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
        if (Field->Flag00 != 0)
            return;
        ICamera* const Camera = AsCamera(Cam);
        CmnAsteroid::CAsteroidField* const Cmn = Field->CmnField;

        FTransform ViewXform;
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
        float Orient9[9];
        memcpy(Orient9, Camera->GetOrientation(), sizeof(Orient9));
        float const* const Pos3 = Camera->GetPosition();
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
            Col0 = AsteroidVec(Orient9[0], Orient9[3], Orient9[6]);
            Col1 = AsteroidVec(Orient9[1], Orient9[4], Orient9[7]);
            Col2 = AsteroidVec(Orient9[2], Orient9[5], Orient9[8]);
            memcpy(s_CachedOrient, Orient9, sizeof(s_CachedOrient));
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
                                : static_cast<double>(Field->FillDistAt24) * Field->Scale230;
#else
        double const Fill = static_cast<double>(Field->FillDistAt24) * Field->Scale230;
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
        unsigned char bBlendState = Field->ScaleCC < 1.0f ? 1 : 0;
        unsigned char bFading = Field->ScaleCC < 1.0f ? 1 : 0;

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
        Material[0] = Field->TintB4;
        Material[1] = Field->TintB8;
        Material[2] = Field->TintBC;
        if (GetGameOption(kGameOptLighting) != 0 && bFading == 0)
        {
            Material[4] = Field->TintC0;
            Material[5] = Field->TintC4;
            Material[6] = Field->TintC8;
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
            static_cast<double>(Field->FillDistAt24) * Field->Scale230 + Margin;
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

                    FTransform CubeXform;
                    memcpy(&CubeXform.Rotation, &Orient, sizeof(CubeXform.Rotation));
                    CubeXform.Translation.X = CubePos.x;
                    CubeXform.Translation.Y = CubePos.y;
                    CubeXform.Translation.Z = CubePos.z;
                    AsteroidBindCubeTransform(CubeXform, ViewXform);

                    // Past the fade start (ordered less) the alpha fades over half a cube; at or
                    // under 0.001f -> next cube.
                    if (FadeStart < Depth)
                    {
#ifdef FREELANCER_RP9
                        double Alpha =
                            ((static_cast<double>(Reach) - Depth) / FadeWidth) * Field->ScaleCC;
#else
                        double Alpha =
                            ((static_cast<double>(Reach) - Depth) / HalfCube) * Field->ScaleCC;
#endif
                        if (Alpha > Field->ScaleCC)
                            Alpha = Field->ScaleCC;
                        else if (Alpha < 0.0)
                            Alpha = 0.0f;
                        Material[3] = static_cast<float>(Alpha);
                        if (Alpha <= 0.001f)
                            continue;
                    }
                    else
                    {
                        Material[3] = Field->ScaleCC;
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
                    FTransform CubeXform;
                    memcpy(&CubeXform.Rotation, &Orient, sizeof(CubeXform.Rotation));
                    CubeXform.Translation.X = PlayerPos.x;
                    CubeXform.Translation.Y = PlayerPos.y;
                    CubeXform.Translation.Z = PlayerPos.z;
                    AsteroidBindCubeTransform(CubeXform, ViewXform);
                    Material[3] = Field->ScaleCC;
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
        float const* const Pos3 = AsCamera(Cam)->GetPosition();
        Vector CamPos;
        CamPos.x = Pos3[0];
        CamPos.y = Pos3[1];
        CamPos.z = Pos3[2];

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
            double const RetailFill = static_cast<double>(Field->FillDistAt24) * Field->Scale230;
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
        if (Field->Flag198 != 0 && GetGameOption(kGameOptAsteroidExt) > 1 &&
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
                float const SavedFar = Field->Billboard.Float40;
                float const SavedNear = Field->Billboard.Float44;
                Field->Billboard.Float40 = SpawnReach;
                Field->Billboard.Float44 = static_cast<float>(SpawnReach - ExtFadeBand);
                BillboardDraw(&Field->Billboard, Cam, Field->ExclusionMgr, 2);
                Field->Billboard.Float40 = SavedFar;
                Field->Billboard.Float44 = SavedNear;
            }
            else
#endif
                BillboardDraw(&Field->Billboard, Cam, Field->ExclusionMgr, 2);
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
            if (Field->ScaleCC < 1.0f)
                bAmbientDone = 0;
        }

        for (void** It = Field->DynBegin; It != Field->DynEnd; ++It)
            CliDynamicAsteroidDraw(static_cast<FCliDynamicAsteroid*>(*It), Cam, &bAmbientDone,
                                   Field);
    }
} // namespace

void CliAsteroidLightAround(FCliAsteroid* Field, FSpaceCamera* Cam, float Scale)
{
    CliAsteroidAmbientPass(Field, Cam, Scale);
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
    // FUN_00422b80.
    void BandSetVec3(float* Dst, float X, float Y, float Z)
    {
        Dst[0] = X;
        Dst[1] = Y;
        Dst[2] = Z;
    }

    // MSVC's signed `n % 2 == 0` idiom (mask 0x80000001), as in FUN_0054c8b0.
    bool BandDiffIsEven(int Diff)
    {
        unsigned U = static_cast<unsigned>(Diff) & 0x80000001u;
        if (static_cast<int>(U) < 0)
            U = (U - 1u) | 0xfffffffeu;
        return U == 0;
    }

    // FUN_0054c8b0 (thiscall on the 0x84 band blob) — builds an elliptical particle ring around
    // the zone (+4): count from zone size * pi / (texture_aspect * height), positions in the
    // zone's XZ plane rotated by its orientation, per-particle normals, then render_parts
    // spokes (+0x60).
    void BandBuildExclusionRings(unsigned char* Blob)
    {
        if (Blob == nullptr)
            return;

        void* OldParticles = *reinterpret_cast<void**>(Blob + 0x3c);
        if (OldParticles != nullptr)
            operator delete(OldParticles);

        Blob[0x7c] = 0;
        *BandU(Blob, 0x20) = 0;

        Universe::IZone* Zone = *reinterpret_cast<Universe::IZone**>(Blob + 4);
        if (Zone == nullptr)
            return;

        // Retail calls GetSize (IZone vt+0x28).
        float SizeX = Zone->Size.x;
        float SizeY = Zone->Size.y;
        float SizeZ = Zone->Size.z;
        if (SizeZ < 0.1f)
            SizeZ = SizeX;

        float const Aspect = *BandF(Blob, 0x10); // +0x40 texture_aspect
        float Height = *BandF(Blob, 0x12);       // +0x48 height
        int VertInc = static_cast<int>(*BandU(Blob, 0x13)); // +0x4c vert_increase

        float const CountF =
            ((SizeX + SizeZ) * 3.14159274f) / (Aspect * Height) + 0.5f;
        int PointCount = FloatToIntTrunc(CountF) * VertInc;
        *BandU(Blob, 0x11) = static_cast<unsigned>(PointCount); // +0x44

        float* Particles = nullptr;
        if (PointCount > 0)
        {
            Particles = static_cast<float*>(operator new(static_cast<size_t>(PointCount) * 0x3c));
            if (Particles != nullptr)
            {
                float* Cursor = Particles;
                for (int I = 0; I < PointCount; ++I)
                {
                    BandSetVec3(Cursor + 7, 1.0f, 1.0f, 1.0f);           // +0x1C
                    BandSetVec3(Cursor + 10, 0.2f, 0.2f, 0.2f);          // +0x28
                    Cursor[13] = 1.0f;                                   // +0x34
                    Cursor[3] = 1000.0f;                                 // +0x0C
                    *reinterpret_cast<unsigned char*>(Cursor + 14) = 1; // +0x38
                    Cursor += 15; // 0x3c-byte particle
                }
            }
        }
        *reinterpret_cast<float**>(Blob + 0x3c) = Particles;

        *BandF(Blob, 0x19) = *BandF(Blob, 0x19) * Height;
        *BandF(Blob, 0x1a) = *BandF(Blob, 0x1a) * Height;
        *BandF(Blob, 0x1b) = *BandF(Blob, 0x1b) * Height;
        *BandF(Blob, 0x1c) = *BandF(Blob, 0x1c) * Height;
        Height = Height * 0.5f;
        *BandF(Blob, 0x12) = Height;

        // Retail calls GetSize a second time.
        SizeX = Zone->Size.x;
        SizeY = Zone->Size.y;
        SizeZ = Zone->Size.z;
        if (SizeZ < 0.1f)
            SizeZ = SizeX;

        Matrix const& Orient = Zone->GetOrientation();
        FMatrix MathOrient;
        for (int R = 0; R < 3; ++R)
        {
            for (int C = 0; C < 3; ++C)
                MathOrient.D[R][C] = Orient.E[R][C];
        }
        float const PosX = Zone->Pos.x;
        float const PosY = Zone->Pos.y;
        float const PosZ = Zone->Pos.z;

        float const Offset = *BandF(Blob, 0x1e); // +0x78
        float RadiusX = 0.0f;
        float RadiusY = 0.0f;
        float RadiusZ = 0.0f;
        if (Blob[0x74] == 0)
        {
            // offset_dist
            RadiusX = SizeX - Offset;
            RadiusY = SizeY - Offset;
            RadiusZ = SizeZ - Offset;
        }
        else
        {
            // offset_percent
            RadiusX = SizeX * Offset;
            RadiusY = SizeY * Offset;
            RadiusZ = SizeZ * Offset;
        }
        (void)RadiusY;

        float* P = Particles;
        if (P != nullptr && PointCount > 0)
        {
            for (int I = 0; I < PointCount; ++I)
            {
                float const Angle =
                    (static_cast<float>(I) / static_cast<float>(PointCount)) * 6.28318548f;
                float const LocalX = cosf(Angle) * RadiusX;
                float const LocalZ = sinf(Angle) * RadiusZ;
                FVector Local;
                Local.X = LocalX;
                Local.Y = 0.0f;
                Local.Z = LocalZ;
                FVector World;
                // Retail tests DAT_00679860 inside the loop and acquires it when still null.
                if (G_CloudsMath == nullptr)
                    G_CloudsMath = Acquire3DMathEngine();
                G_CloudsMath->TransformMatrix(World, MathOrient, Local);
                P[0] = World.X + PosX;
                P[1] = World.Y + PosY;
                P[2] = World.Z + PosZ;
                P[3] = Height;
                P += 15;
            }
        }

        // Per-particle normals from neighbour tangents. Retail multiplies several terms by
        // DAT_005c7474 (0.0); kept verbatim.
        PointCount = static_cast<int>(*BandU(Blob, 0x11));
        unsigned char* Base = *reinterpret_cast<unsigned char**>(Blob + 0x3c);
        if (Base != nullptr && PointCount > 0)
        {
            float* NrmAt = reinterpret_cast<float*>(Base + 8);
            for (int I = 1; I <= PointCount; ++I)
            {
                int Prev = I - 2;
                if (Prev < 0)
                    Prev += PointCount;
                int Next = I;
                if (Next >= PointCount)
                    Next -= PointCount;

                float* Cur = NrmAt - 2; // particle pos at -8 bytes from +8
                float* NextP = reinterpret_cast<float*>(Base + Next * 0x3c);
                float* PrevP = reinterpret_cast<float*>(Base + Prev * 0x3c);

                float DxNext = NextP[0] - Cur[0];
                float DyNext = NextP[1] - Cur[1];
                float DzNext = NextP[2] - Cur[2];
                float DxPrev = Cur[0] - PrevP[0];
                float DyPrev = Cur[1] - PrevP[1];
                float DzPrev = Cur[2] - PrevP[2];

                float N1y0 = DyNext * 0.0f;
                float N1z = DzNext - N1y0;
                float N1x = DxNext * 0.0f - DzNext * 0.0f;
                float N1y = N1y0 - DxNext;
                float Inv1 = 1.0f / sqrtf(N1x * N1x + N1y * N1y + N1z * N1z);
                N1z *= Inv1;
                N1x *= Inv1;
                N1y *= Inv1;

                float N2z = DzPrev - DyPrev * 0.0f;
                float N2x = DxPrev * 0.0f - DzPrev * 0.0f;
                float N2y = DyPrev * 0.0f - DxPrev;
                float Inv2 = 1.0f / sqrtf(N2x * N2x + N2y * N2y + N2z * N2z);
                N2z *= Inv2;
                N2x *= Inv2;
                N2y *= Inv2;

                float Nx = N2z + N1z;
                float Ny = N2x + N1x;
                float Nz = N2y + N1y;
                float InvN = 1.0f / sqrtf(Nx * Nx + Ny * Ny + Nz * Nz);
                NrmAt[2] = Nx * InvN; // +0x10
                NrmAt[3] = Ny * InvN; // +0x14
                NrmAt[4] = Nz * InvN; // +0x18

                NrmAt += 15;
            }
        }

        int RenderParts = static_cast<int>(*BandU(Blob, 0x16)); // +0x58
        float const HalfCount = static_cast<float>(PointCount) * 0.5f;
        if (static_cast<float>(RenderParts) > HalfCount)
        {
            RenderParts = FloatToIntTrunc(HalfCount);
            *BandU(Blob, 0x16) = static_cast<unsigned>(RenderParts);
        }

        void* Spokes = operator new(static_cast<size_t>(RenderParts) * 0x18);
        *reinterpret_cast<void**>(Blob + 0x60) = Spokes;

        if (Spokes != nullptr && RenderParts > 0 && PointCount > 0)
        {
            float const Step = static_cast<float>(PointCount) / static_cast<float>(RenderParts);
            float Acc = 0.0f;
            int StartIdx = 0;
            unsigned char* SpokeBytes = static_cast<unsigned char*>(Spokes);
            for (int I = 0; I < RenderParts; ++I)
            {
                Acc += Step;
                *reinterpret_cast<int*>(SpokeBytes) = StartIdx;
                int const EndIdx = FloatToIntTrunc(Acc + 0.0001f);
                *reinterpret_cast<int*>(SpokeBytes + 4) = EndIdx;
                StartIdx = EndIdx;
                SpokeBytes += 0x18;
            }
        }

        if (Spokes != nullptr && RenderParts > 0)
        {
            unsigned char* SpokeBytes = static_cast<unsigned char*>(Spokes);
            float* PartBase = *reinterpret_cast<float**>(Blob + 0x3c);
            for (int I = 0; I < RenderParts; ++I)
            {
                int I0 = *reinterpret_cast<int*>(SpokeBytes);
                int I1 = *reinterpret_cast<int*>(SpokeBytes + 4);
                int Diff = I1 - I0;
                int MidOff = FloatToIntTrunc(static_cast<float>(Diff) * -0.5f);
                int Idx = I0 - MidOff;
                if (Idx >= PointCount)
                    Idx -= PointCount;

                float* OutPos = reinterpret_cast<float*>(SpokeBytes + 8);
                if (BandDiffIsEven(Diff))
                {
                    float* A = PartBase + Idx * 15;
                    int Idx2 = Idx + 1;
                    if (Idx2 >= PointCount)
                        Idx2 -= PointCount;
                    float* B = PartBase + Idx2 * 15;
                    OutPos[0] = (A[0] + B[0]) * 0.5f;
                    OutPos[1] = (A[1] + B[1]) * 0.5f;
                    OutPos[2] = (A[2] + B[2]) * 0.5f;
                }
                else
                {
                    float* A = PartBase + Idx * 15;
                    OutPos[0] = A[0];
                    OutPos[1] = A[1];
                    OutPos[2] = A[2];
                }

                if (I0 >= PointCount)
                    I0 -= PointCount;
                if (I1 >= PointCount)
                    I1 -= PointCount;

                float* P0 = PartBase + I0 * 15;
                float* P1 = PartBase + I1 * 15;
                float D0x = OutPos[0] - P0[0];
                float D0y = OutPos[1] - P0[1];
                float D0z = OutPos[2] - P0[2];
                float Dist0 = sqrtf(D0x * D0x + D0y * D0y + D0z * D0z);
                float D1x = OutPos[0] - P1[0];
                float D1y = OutPos[1] - P1[1];
                float D1z = OutPos[2] - P1[2];
                float Dist1 = sqrtf(D1x * D1x + D1y * D1y + D1z * D1z);
                if (Dist0 < Dist1)
                    Dist0 = Dist1;
                *reinterpret_cast<float*>(SpokeBytes + 0x14) = Dist0;

                SpokeBytes += 0x18;
            }
        }
    }
}
