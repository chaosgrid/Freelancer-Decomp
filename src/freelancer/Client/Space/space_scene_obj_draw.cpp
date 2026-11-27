// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Space/space_scene_obj_draw.h"

#include "alchemy.h"
#include "Space/Effects/beam_effect.h"
#include "Space/client_gameobj.h"
#include "Common/client_cliobj.h" // CliObjGetArchLodGlobals (FUN_004022d0), CliObjEnvmapMaterial
#include "Space/cli_asteroid.h"
#include "Space/cli_nebula.h"
#include "Space/Effects/cli_projectile_fx.h" // CliEffectCompanionClear (FUN_00502d90)
#include "Space/Effects/client_equip_fx.h"   // RemoveAppearanceInstanceValue (FUN_00532170)
#include "Space/client_objdb.h"
#include "common_archetype.h"
#include "common_cobject.h"
#include "common_iface.h"
#include "common_misc.h"
#include "common_universe.h"
#include "Space/Effects/effect_base.h"
#include "Space/Effects/effect_vertex_batch.h"
#include "Common/engine_light.h"
#include "Space/Effects/effect_type_info.h"
#include "Space/Effects/effect_type_list.h"
#include "Core/game_options.h"
#include "Space/Effects/gate_tunnel.h"    // FGateTunnel::Draw (FUN_004ffab0)
#include "Core/globals.h"
#include "Space/HUD/hud_element_list.h"
#include "Space/Effects/jump_effect.h"    // FJumpShipEffect::Tunnel (+0x24)
#include "Space/HUD/hud_waypoint.h"   // HudWaypointGetObjectCount / HudWaypointGetObject (FUN_004c83c0 / 83e0)
#ifdef FREELANCER_RP9
#include "Space/nebula_volume.h"  // NebulaVolume (port-only Direct3D 9 volumetric nebula)
#endif
#include "Common/Graphics/part_cache.h"
#include "Space/Cameras/space_camera.h"
#include "Space/Cameras/space_camera_game.h"
#include "Space/player_behavior.h"
#include "Core/perf_options.h"
#ifdef FREELANCER_RP9
#include "Common/Graphics/render_ex.h"      // RenderEx::AfterSpaceScene (port-only Direct3D 9 extensions)
#endif
#include "Common/Graphics/screen_quad_state.h"
#include "Space/star_sphere.h"    // StarSphereList (FUN_0050c620)
#include "Space/star_sys.h"
#include "Common/Graphics/texture_panels.h"
#include "Interface/win3d_immediate.h"
#include "Interface/win3d_render.h"

#include "DALib.h"
#include "dacom.h"
#include "dacom_iids.h"
#include "engine/engbase.h"
#include "common_csys.h"
#include "x86math.h"
#include "render/IFLAppearance.h"
#include "render/rendcomp.h"
#include "render/IRenderPipeline.h"
#include "render/shading.h"
#include "soundmanager.h"

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <new>
#include <vector>
#include <windows.h>

// ============================================================================
// Retail chain (listing-verified entry points):
//   FUN_0051b250  camera +0x78
//   FUN_0051c200  GetBucket(0xfdff) + Fill(DAT_00613ec8) + enqueue FUN_0051f2c0
//   FUN_0051eda0  sort: MSVC6 introsort DistSq descending (far first)
//                 n<17 insertion; else FUN_0051fd90 partition + FUN_0051fc90
//   FUN_0051f810  FUN_00422910 perspective re-push, then drain nodes
//                 + MaterialBatcher FlushAll(+0x28) per object
//   FUN_00539980  LOD via +0x17c (FUN_00539820) → get_index → Renderer Slot7
//
// Note: +0x17c/+0x180/+0x184 on Gameobj are VTABLE slots, not data fields.
//   +0x17c = FUN_00539820 (LOD pick)
//   +0x180 = empty stub (IDPMsgHandler::OnDisconnect)
//   +0x184 = FUN_00539b40 (vtable method — lighting prep → FUN_005554d0)
// Data fields used by the draw path: +0x34/+0x38/+0x3c on FClientGameobj.
// ============================================================================

namespace
{
    // DAT_006752f0.. — five growable pointer vectors.
    constexpr unsigned kDrawBucketCount = 5;
    constexpr unsigned kObjDbTypeMask = OBJ_SOLAR_NO_WAYPOINT_MASK;
    // DAT_00613ec8 — Fill query radius.
    constexpr float kObjDbFillRange = 10000.0f;
    // DAT_005dc35c — DistSq threshold for near/far bucket bit.
    constexpr float kNearBucketDistSq = 40000.0f;
    // DAT_006164d0 default (FUN_00410130); distance gate in FUN_0051f2c0.
    float G_SpaceDrawRange = 20000.0f;

    // The queue's nodes are retail's ZItem family (RTTI: abstract base `Zitem`, no vftable of
    // its own; four leaves). Two slots each; the push site stores the vftable inline:
    //   ZItemNebula        vftable 0x005dc01c {LAB_0051ba00 Draw, LAB_00517320}  size 0xc
    //   ZItemAsteroidField vftable 0x005dc034 {LAB_0051bdf0 Draw, LAB_00517320}  size 0x10
    //   ZItemPlanetRing    vftable 0x005dc06c {LAB_0051c1e0 Draw, LAB_00517320}  size 0x10
    //   ZItemGameObject    vftable 0x005dc364 {LAB_0051f7a0 Draw, LAB_0051f780}  size 0x10
    // Allocation is FUN_0051ec80 (ZItemAlloc) by size; release is plain free() (FUN_0051ed20,
    // FUN_0051fbc0) -- no destructor slot, and nothing ever returns a node to either Heap.
    // The drain FUN_0051f810 calls slot 0. Slot 1 has no caller in the draw queue; it returns
    // the item's category (game objects: ship 0, solar 1; everything else 2).
    enum EDrawNodeKind : uintptr_t
    {
        kDrawNodeGameobj = 0,
        kDrawNodeAsteroidFar = 1,
        kDrawNodeTexturePanelPiece = 2,
    };
    // Zitem (abstract): vftable +0, sort key +4, the drawn object +8.
    struct FZItem
    {
        virtual void Draw(FSpaceCamera* Cam) = 0; // vt+0x00
        virtual int GetCategory() = 0;                 // vt+0x04
        float DistSq;           // +0x04  sort key
        void* Object;           // +0x08  Gameobj* / FNebulaEffect* / FCliAsteroid* / FTexturePanels*
    };
    static_assert(sizeof(FZItem) == 0xc, "Zitem sizeof 0xc");

    // ZItemNebula, vftable 0x005dc01c; Object = the StarSys nebula.
    struct FZItemNebula final : FZItem
    {
        // LAB_0051ba00: Object vt+0x10 (DrawFogShell)(Cam), then Object vt+0x14
        // (DrawExteriorBitsPass)(Cam). No test of the object or either slot.
        void Draw(FSpaceCamera* Cam) override;
        // LAB_00517320: return 2.
        int GetCategory() override { return 2; }
    };
    static_assert(sizeof(FZItemNebula) == 0xc, "ZItemNebula sizeof 0xc");

    // ZItemGameObject, vftable 0x005dc364.
    struct FZItemGameObject final : FZItem
    {
        // LAB_0051f7a0: Object->vt+0x188(discarded OutLod, Cam, LodWord, 0, 0).
        void Draw(FSpaceCamera* Cam) override;
        // LAB_0051f780: Object +0x1c (ManagerKind) 1 -> 0, 2 -> 1, anything else -> 2.
        int GetCategory() override;
        unsigned LodWord;       // +0x0c  packed LOD
    };
    static_assert(sizeof(FZItemGameObject) == 0x10, "ZItemGameObject sizeof 0x10");

    // ZItemAsteroidField, vftable 0x005dc034.
    struct FZItemAsteroidField final : FZItem
    {
        // LAB_0051bdf0: FUN_00522850(this = Object, Cam, LodWord).
        void Draw(FSpaceCamera* Cam) override;
        // LAB_00517320: return 2.
        int GetCategory() override { return 2; }
        unsigned LodWord;       // +0x0c  piece index
    };
    static_assert(sizeof(FZItemAsteroidField) == 0x10, "ZItemAsteroidField sizeof 0x10");

    // ZItemPlanetRing, vftable 0x005dc06c (the object is the ring's FTexturePanels).
    struct FZItemPlanetRing final : FZItem
    {
        // LAB_0051c1e0: Object vt+0x10 (slot 4 DrawPiece)(Cam, LodWord), dispatched
        // through the object's vtable by TexturePanelsDrawPiece (texture_panels.h).
        void Draw(FSpaceCamera* Cam) override;
        // LAB_00517320: return 2.
        int GetCategory() override { return 2; }
        unsigned LodWord;       // +0x0c  piece index
    };
    static_assert(sizeof(FZItemPlanetRing) == 0x10, "ZItemPlanetRing sizeof 0x10");

    struct FDrawBucket
    {
        std::vector<FZItem*> Nodes;
    };

    // DAT_00675350 / DAT_00675340 (static initializers LAB_0051ec20 / LAB_0051ec50).
    Heap G_DrawNodeHeap(0x10u, -1);
    Heap G_DrawNodeHeap12(0xcu, -1);
    // DAT_006752f0..0x0067533f -- five 0x10-byte vectors (LAB_0051ecb0 constructs them).
    FDrawBucket G_Buckets[kDrawBucketCount];

    // FUN_0051ec80 -- every caller passes 0xc or 0x10.
    void* ZItemAlloc(size_t Size)
    {
        if (Size == 0xc)
            return G_DrawNodeHeap12.Malloc();
        if (Size == 0x10)
            return G_DrawNodeHeap.Malloc();
        return malloc(Size);
    }
    // DAT_0067524c / DAT_00675244 / DAT_00675248 / DAT_00675230 — field enclosure.
    // 4c: 0=none, 1=partial, 2=fully inside. 44: selected field object. 48: radius². 30: best score.
    int G_FieldEnclosure = kFieldEnclosureNone;
    FNebulaEffect* G_FieldController = nullptr;
    float G_FieldRadiusSq = 0.0f;
    // DAT_00675230 is seeded at load by the static initializer LAB_0051b2d0 to DAT_006164d0
    // squared, DAT_006164d0 being 20000 by then.
    float G_FieldBestScore = 20000.0f * 20000.0f;
    // DAT_0067517c (FUN_0050c620) is the live star list; it lives in star_sphere.cpp
    // (StarSphereList), filled by FUN_0050c630 from the solar ini reader's "star" key.

    // DAT_00675368 (draw-queue object DAT_00675360 +8) — the pass's far plane, written only by
    // FUN_0051c200 and read back by FUN_0051f2c0 / FUN_0051f950.
    float G_DrawCtxZFar = 250000.0f;
    // DAT_00675364 (draw-queue object +4) — FUN_0051c200's second argument. Nothing reads it.
    unsigned G_DrawCtxParam2 = 0;
    // DAT_00610534
    constexpr float kDefaultZFar = 250000.0f;
    // DAT_006164d4 — FUN_00410130: 1000+20000; jump-arm SetNearFar far arg.
    float G_SpaceDrawFar = 21000.0f;
    // DAT_00675360 (draw-queue object +0) — FUN_0051c200's first argument.
    unsigned G_SceneLayerFlags = 0;

    // Effect type live lists: G_EffectTypeBuckets in effect_type_list.cpp (DAT_00674ce0..).

    // DAT_006798f8/f9/fa — [Ambient] color bytes, and DAT_0067986c/6d/6e — the working copy
    // after the AMBIENT option add. Both are StarSys globals (star_sys.cpp); FUN_005554d0
    // below is a StarSys.cpp function that reads and writes them.
    unsigned char (&G_AmbientBaseRgb)[3] = G_StarSysAmbientRgb;
    unsigned char (&G_AmbientWorkRgb)[3] = G_StarSysAmbientWorkRgb;
    // DAT_00679868/69/6a — lerped ambient when the object's nebula supplies RGB.
    unsigned char G_AmbientLerpRgb[3] = {0, 0, 0};

    // DAT_00678980..9c — embedded light-grid root node (FUN_00537c30 / FUN_00537d20).
    // Layout: Child[4] @+0, Payload @+0x10, CellX @+0x14, CellZ @+0x18, HalfSize @+0x1c.
    struct FLightGridPayload
    {
        FLightGridPayload* Next; // +0
        FEngineLight* Light;     // +4
        int Ix;                  // +8
        int Iz;                  // +0xc
        int Range;               // +0x10
    };
    struct FLightGridNode
    {
        FLightGridNode* Child[4]; // +0,+4,+8,+0xc
        FLightGridPayload* Payload; // +0x10
        int CellX;                // +0x14
        int CellZ;                // +0x18
        int HalfSize;             // +0x1c
    };
    static_assert(sizeof(FLightGridPayload) == 0x14, "payload node 0x14");
    static_assert(sizeof(FLightGridNode) == 0x20, "tree node 0x20");

    FLightGridNode G_LightGridRoot = {};
    // DAT_00675578 — 256×0x14 payload pool; free head DAT_00676978.
    FLightGridPayload G_LightPayloadPool[256] = {};
    FLightGridPayload* G_LightPayloadFree = nullptr;
    // DAT_0067697c — 256×0x20 tree pool; free head DAT_0067897c.
    FLightGridNode G_LightTreePool[256] = {};
    FLightGridNode* G_LightTreeFree = nullptr;

    bool CameraHasWatchedObject(FSpaceCamera* Cam)
    {
        // FUN_0051b250's head: the watcher at +0xc0 set, and the object it yields (watcher - 8)
        // non-null. There is no test of `this`; the caller test lives in SpaceApp::Update.
        FGameCamera* Gc = reinterpret_cast<FGameCamera*>(Cam);
        const void* Watched = Gc->Watched.target();
        if (Watched == nullptr)
            return false;
        if (reinterpret_cast<uintptr_t>(Watched) == 8u)
            return false;
        return true;
    }

    // FUN_0051ed20 (thiscall on the queue DAT_00675360) -- for each of the five bucket vectors:
    // free() every pointer (null included) then erase the whole range (the storage stays). The
    // nodes came from Heap::Malloc, which on an empty free list is malloc(ElementSize), and
    // nothing ever pushes onto those free lists, so free() is the matching release.
    void ClearBuckets()
    {
        for (unsigned B = 0; B < kDrawBucketCount; ++B)
        {
            FDrawBucket& Buck = G_Buckets[B];
            for (size_t I = 0; I < Buck.Nodes.size(); ++I)
                free(Buck.Nodes[I]);
            Buck.Nodes.clear();
        }
    }

    // FUN_0051f140 -- push_back of Node onto bucket vector 2 (DAT_00675310). The pointer is
    // appended as given, null too.
    void ZItemQueuePushBucket2(FZItem* Node)
    {
        G_Buckets[2].Nodes.push_back(Node);
    }

    // The ZItemNebula push inlined three times in FUN_0051b730; the pointer -- null when the
    // heap failed -- goes to bucket 2 through FUN_0051f140.
    void PushNebulaZItem(float Score, void* Nebula)
    {
        void* const Mem = ZItemAlloc(0xc);
        FZItem* Node = nullptr;
        if (Mem != nullptr)
        {
            Node = new (Mem) FZItemNebula();
            Node->DistSq = Score;
            Node->Object = Nebula;
        }
        ZItemQueuePushBucket2(Node);
    }

    // FUN_0051b730 (thiscall on the camera) -- pick the field controller DAT_00675244 among the
    // StarSys nebulae; every other one is queued as a ZItemNebula in bucket 2 (FUN_0051f140),
    // drawn by FUN_0051f8d0's far pass. No test of the camera, a nebula, its zone or any vtable
    // entry.
    void SelectFieldControllers(FSpaceCamera* Cam)
    {
        // Best score = DAT_006164d0 squared (stored float).
        G_FieldBestScore = static_cast<float>(static_cast<double>(G_SpaceDrawRange) *
                                              G_SpaceDrawRange);
        G_FieldController = nullptr;
        G_FieldEnclosure = kFieldEnclosureNone;
        G_FieldRadiusSq = 0.0f;

        // [EFFECTS] and NEBULA.
        if (GetGameOption(kGameOptEffectsGroup) == 0)
            return;
        if (GetGameOption(kGameOptNebula) == 0)
            return;

        ICamera* const Camera = AsCamera(Cam);

        // FUN_005553a0's list.
        std::list<FNebulaEffect*>& Nebulae = StarSysNebulaList();
        for (std::list<FNebulaEffect*>::iterator It = Nebulae.begin(); It != Nebulae.end(); ++It)
        {
            FNebulaEffect* const Field = *It;

            // d = zone - camera, kept unrounded.
            Universe::IZone* const Zone = Field->GetZone();
            const Vector* const CamPos = Camera->GetPosition();
            float const* const ZonePos = &Zone->GetPos().x;
            const double Dz = static_cast<double>(ZonePos[2]) - CamPos->z;
            const double Dy = static_cast<double>(ZonePos[1]) - CamPos->y;
            const double Dx = static_cast<double>(ZonePos[0]) - CamPos->x;

            // |d| by one Newton step of the 0x5f3759df estimate; the sum and Dist are stored as
            // floats.
            const double SumSq = Dx * Dx + Dy * Dy + Dz * Dz;
            const float SumSqF = static_cast<float>(SumSq);
            int SumBits = 0;
            memcpy(&SumBits, &SumSqF, sizeof(float));
            const int EstimateBits = 0x5f3759df - (SumBits >> 1);
            float Estimate = 0.0f;
            memcpy(&Estimate, &EstimateBits, sizeof(float));
            const double Newton =
                (1.5f - static_cast<double>(Estimate) * Estimate * SumSq * 0.5f) * Estimate;
            const float Dist = static_cast<float>(Newton * SumSq);

            // Each intermediate is stored as a float.
            const float InvDist = static_cast<float>(1.0 / Dist);
            float Dir[3];
            Dir[0] = static_cast<float>(InvDist * Dx);
            Dir[1] = static_cast<float>(InvDist * Dy);
            Dir[2] = static_cast<float>(InvDist * Dz);

            // Dist beyond the zone's extent along that direction (greater or unordered) scores
            // (Dist - hit)^2, otherwise 0.
            const float ZoneHit = (float)Zone->GetRadiusAlong(*(Vector const*)Dir);
            float Score = 0.0f;
            if (!(Dist <= ZoneHit))
            {
                const double Beyond = static_cast<double>(Dist) - ZoneHit;
                Score = static_cast<float>(Beyond * Beyond);
            }

            // Once a controller level is set, every further nebula is deferred with its score.
            if (G_FieldEnclosure != kFieldEnclosureNone)
            {
                PushNebulaZItem(Score, Field);
                continue;
            }

            const float Enclose = Field->ZoneExteriorFactor((const float*)Camera->GetPosition());

            // Fully inside: quality 2, the queue emptied (FUN_0051ed20), done.
            if (Enclose >= 1.0f)
            {
                G_FieldEnclosure = kFieldEnclosureInside;
                G_FieldController = Field;
                const double Radius = Field->GetFogEnd();
                G_FieldRadiusSq = static_cast<float>(Radius * Radius);
                ClearBuckets();
                return;
            }

            // Strictly above 0 (ordered) -- partial: the previous controller is deferred with
            // the best score, this one becomes it at quality 1.
            if (Enclose > 0.0f)
            {
                if (G_FieldController != nullptr)
                    PushNebulaZItem(G_FieldBestScore, G_FieldController);
                G_FieldEnclosure = kFieldEnclosurePartial;
                G_FieldController = Field;
                const double Radius = Field->GetFogEnd();
                G_FieldRadiusSq = static_cast<float>(Radius * Radius);
                continue;
            }

            // Outside -- a score strictly below the best (>= and unordered are deferred)
            // replaces it, deferring the previous controller.
            if (!(Score < G_FieldBestScore))
            {
                PushNebulaZItem(Score, Field);
                continue;
            }
            if (G_FieldController != nullptr)
                PushNebulaZItem(G_FieldBestScore, G_FieldController);
            G_FieldBestScore = Score;
            G_FieldController = Field;
        }
    }



    // DAT_006789d0 -- Gameobj.cpp's lazily acquired math engine (FUN_005b73e0 on first use).
    I3DMathEngine* G_GameobjMathEngine = nullptr;
}

    // FUN_00533880 -- QI DALib::Engine (no null test) for IFLAppearance; a failed QI still
    // releases a non-null local. Then Slot3 with no null test on the interface. The local is a
    // smart pointer in retail; nothing between QI and the release can throw.
    bool SetAppearanceInstanceValue(InstanceIndex InstId, void* ArchPath)
    {
        IFLAppearance* Ifl = nullptr;
        if (DALib::Engine->QueryInterface(Iids::FLAppearance, reinterpret_cast<void**>(&Ifl)) < 0)
        {
            if (Ifl != nullptr)
                Ifl->Release();
            return false;
        }
        bool const bOk = Ifl->SetInstanceValue(static_cast<int>(InstId), ArchPath) >= 0;
        if (Ifl != nullptr)
            Ifl->Release();
        return bOk;
    }

    // FUN_00533930 -- the same shape as FUN_00533880 around Slot8.
    bool GetAppearanceOverrideMaterial(unsigned long Index, void** Out)
    {
        IFLAppearance* Ifl = nullptr;
        if (DALib::Engine->QueryInterface(Iids::FLAppearance, reinterpret_cast<void**>(&Ifl)) < 0)
        {
            if (Ifl != nullptr)
                Ifl->Release();
            return false;
        }
        bool const bOk = Ifl->GetOverrideMaterial(Index, Out) >= 0;
        if (Ifl != nullptr)
            Ifl->Release();
        return bOk;
    }

namespace
{

    // FUN_00532170 is RemoveAppearanceInstanceValue in client_equip_fx.cpp, next to its other
    // caller, the cloak wrapper's notify.


    // FUN_004f70c0 — 1.0 - DAT_006129c8, returned on the FPU, never rounded to a float.
    double EffectPerfOneMinus()
    {
        return 1.0 - static_cast<double>(GetPerfGenericEffectPerf());
    }

    // The body shared by LAB_00537ba0, FUN_00537c30 and FUN_005382f0: the root's first seven
    // dwords (DAT_00678980..98) zeroed, HalfSize (DAT_0067899c) set, and each pool rethreaded
    // through its first dword only (element i -> element i-1, the head the last element).
    void LightGridResetPools(int RootHalfSize)
    {
        G_LightGridRoot.Child[0] = nullptr;
        G_LightGridRoot.Child[1] = nullptr;
        G_LightGridRoot.Child[2] = nullptr;
        G_LightGridRoot.Child[3] = nullptr;
        G_LightGridRoot.Payload = nullptr;
        G_LightGridRoot.CellX = 0;
        G_LightGridRoot.CellZ = 0;
        G_LightGridRoot.HalfSize = RootHalfSize;

        FLightGridPayload* PayloadPrev = nullptr;
        for (int I = 0; I < 256; ++I)
        {
            G_LightPayloadPool[I].Next = PayloadPrev;
            PayloadPrev = &G_LightPayloadPool[I];
        }
        G_LightPayloadFree = PayloadPrev;

        FLightGridNode* TreePrev = nullptr;
        for (int I = 0; I < 256; ++I)
        {
            G_LightTreePool[I].Child[0] = TreePrev;
            TreePrev = &G_LightTreePool[I];
        }
        G_LightTreeFree = TreePrev;
    }

    // DAT_006789a0 -- the grid extent: 0x8ca0 from the static initializer, 0x30d40 once
    // FUN_00537c30 has run. FUN_005382f0 halves it into the root's HalfSize.
    int G_LightGridExtent = 0;

    // LAB_00537ba0 (XC static initializer): extent 0x8ca0, HalfSize 0x4650, pools threaded.
    bool LightGridStaticInit()
    {
        G_LightGridExtent = 0x8ca0;
        LightGridResetPools(0x4650);
        return true;
    }
    bool const G_bLightGridStaticInit = LightGridStaticInit();

    FLightGridNode* LightGridAllocTreeNode()
    {
        FLightGridNode* Node = G_LightTreeFree;
        if (Node == nullptr)
            return nullptr;
        G_LightTreeFree = reinterpret_cast<FLightGridNode*>(Node->Child[0]);
        Node->Child[0] = nullptr;
        Node->Child[1] = nullptr;
        Node->Child[2] = nullptr;
        Node->Child[3] = nullptr;
        Node->Payload = nullptr;
        return Node;
    }

    FLightGridPayload* LightGridAllocPayload()
    {
        FLightGridPayload* Node = G_LightPayloadFree;
        if (Node == nullptr)
            return nullptr;
        G_LightPayloadFree = Node->Next;
        Node->Next = nullptr;
        return Node;
    }

    // FUN_00537d20 — insert light record into the XZ quadtree (thiscall on pool owner).
    void LightGridInsert(FLightGridNode* Node, int const* Record5)
    {
        if (Node == nullptr || Record5 == nullptr)
            return;

        int Radius = Record5[4];
        FLightGridNode* Cur = Node;
        while (Radius <= Cur->HalfSize)
        {
            int RelX = Record5[2] - Cur->CellX;
            int RelZ = Record5[3] - Cur->CellZ;

            if (RelX < Radius)
            {
                if (RelZ < Radius)
                {
                    FLightGridNode* Child = Cur->Child[1];
                    if (Child == nullptr)
                    {
                        Child = LightGridAllocTreeNode();
                        if (Child == nullptr)
                            goto AfterChild1;
                        int Half = Cur->HalfSize / 2;
                        Child->HalfSize = Half;
                        Child->CellX = Cur->CellX - Half;
                        Child->CellZ = Cur->CellZ - Half;
                        Cur->Child[1] = Child;
                    }
                    LightGridInsert(Child, Record5);
                }
            AfterChild1:
                if (-RelZ < Radius)
                {
                    FLightGridNode* Child = Cur->Child[3];
                    if (Child == nullptr)
                    {
                        Child = LightGridAllocTreeNode();
                        if (Child == nullptr)
                            goto AfterChild3;
                        int Half = Cur->HalfSize / 2;
                        Child->HalfSize = Half;
                        Child->CellX = Cur->CellX - Half;
                        Child->CellZ = Cur->CellZ + Half;
                        Cur->Child[3] = Child;
                    }
                    LightGridInsert(Child, Record5);
                }
            }
        AfterChild3:
            if (Radius <= -RelX)
                return;
            if (RelZ < Radius)
            {
                FLightGridNode* Child = Cur->Child[0];
                if (Child == nullptr)
                {
                    Child = LightGridAllocTreeNode();
                    if (Child == nullptr)
                        goto AfterChild0;
                    int Half = Cur->HalfSize / 2;
                    Child->HalfSize = Half;
                    Child->CellX = Cur->CellX + Half;
                    Child->CellZ = Cur->CellZ - Half;
                    Cur->Child[0] = Child;
                }
                LightGridInsert(Child, Record5);
            }
        AfterChild0:
            if (Radius <= -RelZ)
                return;
            {
                FLightGridNode* Child = Cur->Child[2];
                if (Child == nullptr)
                {
                    Child = LightGridAllocTreeNode();
                    if (Child == nullptr)
                        return;
                    int Half = Cur->HalfSize / 2;
                    Child->HalfSize = Half;
                    Child->CellX = Cur->CellX + Half;
                    Child->CellZ = Cur->CellZ + Half;
                    Cur->Child[2] = Child;
                }
                Radius = Record5[4];
                Cur = Child;
            }
        }

        FLightGridPayload* Payload = LightGridAllocPayload();
        if (Payload == nullptr)
            return;
        Payload->Next = Cur->Payload;
        Payload->Light = reinterpret_cast<FEngineLight*>(static_cast<uintptr_t>(
            static_cast<unsigned>(Record5[1])));
        Payload->Ix = Record5[2];
        Payload->Iz = Record5[3];
        Payload->Range = Record5[4];
        Cur->Payload = Payload;
    }

    // FUN_00537c30 — reset pools + insert every Flags&8 light from DAT_00675570.
    void LightGridRebuildImpl()
    {
        G_LightGridExtent = 0x30d40;
        LightGridResetPools(0x186a0);

        for (FEngineLight* Light = EngineLightGameListHead(); Light != nullptr;
             Light = EngineLightGameListNext(Light))
        {
            if ((Light->Flags & kLightFlagEnabled) == 0)
                continue;
            int Record[5];
            Record[0] = 0;
            Record[1] = static_cast<int>(reinterpret_cast<uintptr_t>(Light));
            Record[2] = static_cast<int>(Light->Position.x);
            Record[3] = static_cast<int>(Light->Position.z);
            Record[4] = static_cast<int>(Light->D3d.Range);
            LightGridInsert(&G_LightGridRoot, Record);
        }
    }
} // namespace

void LightGridRebuild()
{
    LightGridRebuildImpl();
}

void LightGridReset()
{
    // FUN_005382f0: HalfSize = DAT_006789a0 / 2 (signed, rounds toward zero), then the same
    // root/pool reset.
    LightGridResetPools(G_LightGridExtent / 2);
}

namespace
{
    // DAT_00674f3c -- the screen-quad list key the effects pass flushes; its static initializer
    // LAB_004f6ed0 sets it to -1 and nothing else writes it. -1 is FUN_00403ea0's "current
    // list" key DAT_0061648c: the container of the current material key is flushed.
    unsigned long G_EffectsListKey = 0xffffffffu;

    // FUN_00538200 (thiscall on the pool DAT_00675578). `this` is only handed on to the
    // recursion, so the port drops it. A null node returns 0; nothing else is tested. The node
    // itself goes first when it holds a payload, then the children in retail's order (signed
    // compares); each writes after what is already collected.
    int LightTreeCollect(FLightGridNode** OutSlot, FLightGridNode* Node, int const* Query)
    {
        if (Node == nullptr)
            return 0;
        int Count = 0;
        if (Node->Payload != nullptr)
        {
            *OutSlot = Node;
            Count = 1;
        }
        int const RelX = Query[2] - Node->CellX;
        int const RelZ = Query[3] - Node->CellZ;
        int const Radius = Query[4];
        if (RelX < Radius)
        {
            if (RelZ < Radius)
                Count += LightTreeCollect(OutSlot + Count, Node->Child[1], Query);
            if (-RelZ < Radius)
                Count += LightTreeCollect(OutSlot + Count, Node->Child[3], Query);
        }
        if (-RelX < Radius)
        {
            if (RelZ < Radius)
                Count += LightTreeCollect(OutSlot + Count, Node->Child[0], Query);
            if (-RelZ < Radius)
                Count += LightTreeCollect(OutSlot + Count, Node->Child[2], Query);
        }
        return Count;
    }

    // FUN_00537f40. Nothing is tested and nothing is capped but the sorted list (0x100).
    int CollectNearbyLights(void** OutSlots, int MaxSlots, float const* Pos3, float Radius,
                            unsigned char Flag)
    {
        // The query's first two dwords are never written or read.
        int const Ix = static_cast<int>(Pos3[0]);
        int const Iz = static_cast<int>(Pos3[2]);
        int const Ir = static_cast<int>(Radius);
        int Query[5] = {};
        Query[2] = Ix;
        Query[3] = Iz;
        Query[4] = Ir;

        // The root first when it holds a payload, then FUN_00538200 into its children in the
        // same order as the recursion. Retail's frame holds 0x101 slots and bounds none of the
        // appends.
        FLightGridNode* Scratch[0x101];
        int Collected = 0;
        if (G_LightGridRoot.Payload != nullptr)
        {
            Scratch[0] = &G_LightGridRoot;
            Collected = 1;
        }
        int const RelX = Ix - G_LightGridRoot.CellX;
        int const RelZ = Iz - G_LightGridRoot.CellZ;
        if (RelX < Ir)
        {
            if (RelZ < Ir)
                Collected += LightTreeCollect(Scratch + Collected, G_LightGridRoot.Child[1], Query);
            if (-RelZ < Ir)
                Collected += LightTreeCollect(Scratch + Collected, G_LightGridRoot.Child[3], Query);
        }
        if (-RelX < Ir)
        {
            if (RelZ < Ir)
                Collected += LightTreeCollect(Scratch + Collected, G_LightGridRoot.Child[0], Query);
            if (-RelZ < Ir)
                Collected += LightTreeCollect(Scratch + Collected, G_LightGridRoot.Child[2], Query);
        }

        // Every payload of every collected node, skipped when a payload of the same light is
        // already listed, else inserted by binary search on the range (descending). The count
        // stops at 0x100; an insertion point at the full count is dropped.
        FLightGridPayload* Sorted[0x100];
        int SortedCount = 0;
        for (int I = 0; I < Collected; ++I)
        {
            for (FLightGridPayload* Pay = Scratch[I]->Payload; Pay != nullptr; Pay = Pay->Next)
            {
                bool bListed = false;
                for (int J = 0; J < SortedCount; ++J)
                {
                    if (Sorted[J]->Light == Pay->Light)
                    {
                        bListed = true;
                        break;
                    }
                }
                if (bListed)
                    continue;

                int Lo = 0;
                int Hi = SortedCount;
                while (Lo < Hi)
                {
                    int const Mid = (Lo + Hi) / 2;
                    if (Pay->Range < Sorted[Mid]->Range)
                        Lo = Mid + 1;
                    else
                        Hi = Mid;
                }
                if (SortedCount < 0x100)
                    ++SortedCount;
                FLightGridPayload* Carry = Pay;
                for (int K = Hi; K < SortedCount; ++K)
                {
                    FLightGridPayload* const Next = Sorted[K];
                    Sorted[K] = Carry;
                    Carry = Next;
                }
            }
        }

        // In sorted order, until MaxSlots are taken: with Flag clear a light whose +0xb4 bit 0
        // is set is passed over; the rest are kept when (x - px)^2 + (z - pz)^2 is below
        // (range + radius)^2 (x87, unrounded; unordered drops the light).
        int FilterCount = 0;
        for (int I = 0; I < SortedCount; ++I)
        {
            if (FilterCount >= MaxSlots)
                break;
            FLightGridPayload* const Pay = Sorted[I];
            if (Flag == 0 && (Pay->Light->Flags & kLightFlagDynamic) != 0)
                continue;
            double const Dx = static_cast<double>(Pos3[0]) - static_cast<double>(Pay->Ix);
            double const Dz = static_cast<double>(Pos3[2]) - static_cast<double>(Pay->Iz);
            double const Reach = static_cast<double>(Pay->Range) + static_cast<double>(Radius);
            if (Reach * Reach > Dz * Dz + Dx * Dx)
            {
                OutSlots[FilterCount] = Pay->Light;
                ++FilterCount;
            }
        }

        // Compacted in place through FUN_00410a40 on each kept light.
        int OutCount = 0;
        for (int I = 0; I < FilterCount; ++I)
        {
            FEngineLight* const Light = static_cast<FEngineLight*>(OutSlots[I]);
            if (Light->AffectsPoint(Pos3, Radius))
            {
                OutSlots[OutCount] = Light;
                ++OutCount;
            }
        }
        return OutCount;
    }

    // FUN_00422440 is FogApplyColorAndRange (cli_nebula.cpp) and FUN_004102c0 is
    // EngineLightApplyList (engine_light.cpp).

    // FUN_005554d0: NebulaBits is the containing nebula (Gameobj +0x38), FogFactorBits the float
    // lerp factor (Gameobj +0x3c); Camera is handed on to FUN_004104e0.
    void StarSysApplyObjectLighting(float const* Pos3, float Radius, void* Camera,
                                    unsigned NebulaBits, unsigned FogFactorBits,
                                    unsigned char Flag)
    {
        Win3DDisable(kWin3DFog); // FUN_00421ed0(0xb60)

        if (GetGameOption(kGameOptLighting) == 0)
        {
            EngineLightClearRegisteredList();
            unsigned char White[3] = {0xff, 0xff, 0xff};
            EngineLightSetAmbientRgbBytes(White);
            return;
        }

        int const AmbientOpt = GetGameOption(kGameOptAmbient);
        for (int C = 0; C < 3; ++C)
        {
            int Sum = static_cast<int>(G_AmbientBaseRgb[C]) + AmbientOpt;
            if (Sum > 0xff)
                Sum = 0xff;
            G_AmbientWorkRgb[C] = static_cast<unsigned char>(Sum);
        }

        // No nebula -> straight to the tail.
        unsigned char const* AmbientPtr = G_AmbientWorkRgb;
        FNebulaEffect* const Nebula = (FNebulaEffect*)(uintptr_t)NebulaBits;
        if (Nebula != nullptr)
        {

            // The nebula's ambient bytes, or null (which skips the lerp but not the fog block).
            unsigned char const* const Src = Nebula->GetAmbientBytes();
            if (Src != nullptr)
            {
                // Per channel: (src - work) * T + work + 0.5, truncated. No clamp: the low byte
                // of the truncated int is kept. The product of a 9-bit int and a float is exact
                // in double, as in the x87 register.
                float T = 0.0f;
                memcpy(&T, &FogFactorBits, sizeof(float));
                for (int C = 0; C < 3; ++C)
                {
                    int const Work = static_cast<int>(G_AmbientWorkRgb[C]);
                    int const Diff = static_cast<int>(Src[C]) - Work;
                    double const V = static_cast<double>(Diff) * static_cast<double>(T) +
                                     static_cast<double>(Work) + 0.5;
                    G_AmbientLerpRgb[C] = static_cast<unsigned char>(static_cast<int>(V));
                }
                AmbientPtr = G_AmbientLerpRgb; // DAT_00679868
            }

            // FUN_00422440 with the arguments evaluated right to left: the fog end for a
            // position (FUN_00509680), the fog start (LAB_00509660), the fog colour
            // (LAB_00509480).
#ifdef FREELANCER_RP9
            // Port-only: while the Direct3D 9 volumetric nebula draws this nebula's inside
            // (nebula_volume.cpp) it fogs the object by its depth; the D3D fog stays off (the
            // FUN_00421ed0(0xb60) above). The ambient lerp above is kept.
            if (!NebulaVolume::ReplacesObjectFog(Nebula))
#endif
            {
                float const FogEnd = Nebula->FogFactor(Pos3);
                float const FogStart = Nebula->GetFogStart();
                float FogColor[3];
                float const* const FogRgb = Nebula->CopyFogRgb(FogColor);
                FogApplyColorAndRange(FogRgb, FogStart, FogEnd);
            }
        }

        // The shared tail: FUN_00410240, FUN_00537f40, FUN_004102c0.
        EngineLightSetAmbientRgbBytes(AmbientPtr);

        void* LightSlots[4] = {};
        int const Count = CollectNearbyLights(LightSlots, 4, Pos3, Radius, Flag);
        EngineLightApplyList(reinterpret_cast<FEngineLight* const*>(LightSlots),
                             static_cast<unsigned>(Count), static_cast<ICamera*>(Camera), Pos3);
    }

    // The GameObject+0x28 list's 0x18-byte node: next, prev, then the 0x10-byte effect
    // companion {slot0, ISoundSource*, particle, GameLight} that FUN_00502d90 clears.
    struct FSweepEffectNode
    {
        FSweepEffectNode* Next;  // +0x00
        FSweepEffectNode* Prev;  // +0x04
        void* Slot0;             // +0x08  companion +0
        ISoundSource* Sound;     // +0x0c  companion +4
        unsigned char* Particle; // +0x10  companion +8
        FGameLight* Light;       // +0x14  companion +0xc
    };
    static_assert(sizeof(FSweepEffectNode) == 0x18, "effect list node 0x18");

    // FUN_004fd040 (thiscall on GameObject+0x28) -- walk from the sentinel's next until the
    // node is the sentinel again. A node whose particle is live (non-null, +8 bit 1 clear) is
    // kept. So is one whose sound's IsActive returns non-zero, and one whose light's anim keys
    // (+0xd4) are null. Every other node is unlinked (prev->next = next, next->prev = prev), its companion cleared (FUN_00502d90),
    // the node freed (FUN_005b7e1d), and the count (+8) decremented. A kept node with a particle
    // gets particle +8 |= 1 and FUN_004f7af0. No test of the list or the sentinel.
    void GameobjSweepEffectList(FGameobjEffectList* List)
    {
        FSweepEffectNode* Node = static_cast<FSweepEffectNode*>(List->Head)->Next;
        while (Node != static_cast<FSweepEffectNode*>(List->Head))
        {
            bool bKeep = Node->Particle != nullptr && (Node->Particle[8] & 2u) == 0;
            if (!bKeep && Node->Sound != nullptr && Node->Sound->IsActive() != 0)
                bKeep = true;
            if (!bKeep && Node->Light != nullptr && Node->Light->Anim.Keys == nullptr)
                bKeep = true;

            if (!bKeep)
            {
                FSweepEffectNode* const Next = Node->Next;
                Node->Prev->Next = Next;
                Node->Next->Prev = Node->Prev;
                CliEffectCompanionClear(&Node->Slot0);
                ::operator delete(Node);
                List->Count = List->Count - 1;
                Node = Next;
                continue;
            }

            if (Node->Particle != nullptr)
            {
                Node->Particle[8] = static_cast<unsigned char>(Node->Particle[8] | 1u);
                EffectTypeListLink(Node->Particle);
            }
            Node = Node->Next;
        }
    }

    // FUN_00539b40 — Gameobj vtable +0x184. No null tests. The perf value (FUN_004f70c0) stays
    // unrounded; strictly below 0.999 (DAT_005c8b68) clears the flag.
    void GameobjUpdateDrawLighting(FClientGameobj* Self, void* Camera)
    {
        Vector Center;
        float const Radius = Self->GetPhysicalRadius(Center);

        unsigned Type;
        Self->get_type(Type);
        unsigned char Flag = static_cast<unsigned char>(((Type & (OBJ_MOON | OBJ_PLANET | OBJ_SUN | OBJ_JUMP_HOLE)) == 0) ? 1 : 0);
        if (EffectPerfOneMinus() < 0.999)
            Flag = 0;

        Vector const& Pos = Self->get_position();
        unsigned FogBits = 0;
        memcpy(&FogBits, &Self->NebulaFogFactor, sizeof(FogBits));
        StarSysApplyObjectLighting(&Pos.x, Radius, Camera,
                                   static_cast<unsigned>(reinterpret_cast<uintptr_t>(Self->CurrentNebula)),
                                   FogBits, Flag);
    }


    // FUN_0051f2c0's node push (inlined twice): the pointer -- null too, when the heap failed --
    // is appended to bucket vector Bucket.
    void PushNode(unsigned Bucket, IObjInspect* Inspect, float DistSq, unsigned LodWord)
    {
        void* const Mem = G_DrawNodeHeap.Malloc();
        FZItem* Node = nullptr;
        if (Mem != nullptr)
        {
            FZItemGameObject* const GameObjectItem = new (Mem) FZItemGameObject();
            GameObjectItem->DistSq = DistSq;
            GameObjectItem->Object = Inspect;
            GameObjectItem->LodWord = LodWord;
            Node = GameObjectItem;
        }
        G_Buckets[Bucket].Nodes.push_back(Node);
    }

    // The ZItemAsteroidField / ZItemPlanetRing pushes inlined at eight sites in FUN_0051ba20 /
    // FUN_0051be10; the pointer -- null when the heap failed -- is appended to bucket 0
    // (FUN_0051efc0) or 2 (FUN_0051f140), the literal each site picks. No test of the object
    // or the bucket; Kind stands for the vftable constant the site stores.
    void PushTypedNode(unsigned Bucket, EDrawNodeKind Kind, void* Object, float DistSq,
                       unsigned PieceOrLod)
    {
        void* const Mem = ZItemAlloc(0x10);
        FZItem* Node = nullptr;
        if (Mem != nullptr)
        {
            if (Kind == kDrawNodeAsteroidFar)
            {
                FZItemAsteroidField* const FieldItem = new (Mem) FZItemAsteroidField();
                FieldItem->LodWord = PieceOrLod;
                Node = FieldItem;
            }
            else
            {
                FZItemPlanetRing* const RingItem = new (Mem) FZItemPlanetRing();
                RingItem->LodWord = PieceOrLod;
                Node = RingItem;
            }
            Node->DistSq = DistSq;
            Node->Object = Object;
        }
        G_Buckets[Bucket].Nodes.push_back(Node);
    }

    // FUN_0040fb60 — write ZNear (+0xa4) and ZFar (+0xa8). No test of `this`.
    void CameraSetNearFar(FSpaceCamera* Cam, float ZNear, float ZFar)
    {
        Cam->ZNear = ZNear;
        Cam->ZFar = ZFar;
    }

    // FUN_0051f2c0 (thiscall on the queue DAT_00675360). No test of either pointer.
    void EnqueueObject(FClientGameobj* Go, FSpaceCamera* Cam)
    {
        ICamera* const Camera = AsCamera(Cam);

        // The difference built through FUN_00422b80 from three float-rounded components
        // (z, y, x in that order).
        const Vector* const CamPosPtr = Camera->GetPosition();
        const float CamX = CamPosPtr->x;
        const float CamY = CamPosPtr->y;
        const float CamZ = CamPosPtr->z;
        Vector const& ObjPos = Go->get_position();
        const float Dz = static_cast<float>(static_cast<double>(ObjPos.z) - CamZ);
        const float Dy = static_cast<float>(static_cast<double>(ObjPos.y) - CamY);
        const float Dx = static_cast<float>(static_cast<double>(ObjPos.x) - CamX);

        // x87: the unrounded sum is compared with DAT_006164d0 squared; below (or unordered)
        // is near.
        const double DistSqRaw = static_cast<double>(Dy) * Dy + static_cast<double>(Dz) * Dz +
                                 static_cast<double>(Dx) * Dx;
        float DistSq = static_cast<float>(DistSqRaw);
        const double RangeSq = static_cast<double>(G_SpaceDrawRange) * G_SpaceDrawRange;

        if (DistSqRaw >= RangeSq)
        {
            // Far -- only a ManagerKind 2 object (a solar), and only outside a field's core.
            if (Go->ManagerKind != EClientGameobjKind::Solar)
                return;
            if (G_FieldEnclosure == kFieldEnclosureInside)
                return;

            // The LOD pick (no cached LOD) against the 250000 far plane, then the plane back to
            // the pass's far.
            CameraSetNearFar(Cam, Camera->GetZNear(), kDefaultZFar);
            unsigned Lod = 0;
            Go->PickLod(&Lod, Cam, nullptr);
            CameraSetNearFar(Cam, Camera->GetZNear(), G_DrawCtxZFar);

            if (static_cast<unsigned short>(Lod) < 3)
                return;
            if (static_cast<short>(Lod >> 16) >= 2)
                return;

            // vt+0x18c (the nebula) must be 0.
            unsigned FogFactorBits = 0;
            if (Go->GetNebula(&FogFactorBits) != 0)
                return;

            PushNode(2, Go, DistSq, Lod); // DAT_00675310, bucket 2
            return;
        }

        // A solar whose narrowed CSolar (null read unchecked) has any of TypeBits 0..2 and no
        // star sphere: DistSq -= r * r, stored float.
        if (Go->ManagerKind == EClientGameobjKind::Solar)
        {
            CObject* const Obj = Go->Object;
            CSolar* Solar = nullptr;
            if (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar)
                Solar = static_cast<CSolar*>(Obj);
            if ((static_cast<unsigned char>(Solar->TypeBits) & 7u) != 0 &&
                static_cast<FClientSolarGameobj*>(Go)->StarSphere == 0)
            {
                Vector Center;
                const double Radius = Go->GetPhysicalRadius(Center);
                DistSq = static_cast<float>(static_cast<double>(DistSq) - Radius * Radius);
            }
        }

        // With a field controller, vt+0x18c's second word (into a float local that starts at
        // 0) is the enclosure E.
        unsigned Bucket = 0;
        if (G_FieldController != nullptr)
        {
            unsigned EncloseBits = 0;
            const unsigned NebulaBits = Go->GetNebula(&EncloseBits);
            float Enclose = 0.0f;
            memcpy(&Enclose, &EncloseBits, sizeof(float));

            float E;
            if (reinterpret_cast<void*>(static_cast<uintptr_t>(NebulaBits)) != G_FieldController)
            {
                // Another nebula -- above 0.25 (double 0x005cbf58) is dropped.
                if (static_cast<double>(Enclose) > 0.25)
                    return;
                E = 0.0f;
            }
            else
            {
                // This controller's enclosure -- strictly above 0 picks bucket 1.
                E = Enclose;
                if (Enclose > 0.0f)
                    Bucket = 1;
            }

            if (G_FieldEnclosure == kFieldEnclosureInside)
            {
                // Above (or unordered) goes on.
                if (E <= 0.0f)
                    return;
            }
            else if (G_FieldEnclosure == kFieldEnclosureNone)
            {
                if (E >= 1.0f)
                    return;
            }
            else
            {
                // E below 1.0f (or unordered) goes on.
                if (E >= 1.0f && DistSq > G_FieldRadiusSq)
                    return;
            }
        }

        // The LOD pick, no cached LOD.
        unsigned Lod = 0;
        Go->PickLod(&Lod, Cam, nullptr);
        if (static_cast<unsigned short>(Lod) < 3)
            return;
        if (static_cast<short>(Lod >> 16) >= 2)
            return;

        // >= and unordered keep the bucket.
        if (DistSq < kNearBucketDistSq)
            Bucket = (G_FieldEnclosure != kFieldEnclosureNone) ? 1u : 0u;

        // A CEqObj whose archetype has the nomad byte (+0x80) set goes three buckets up.
        CObject* const Obj = Go->Object;
        if (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj)
        {
            Archetype::EqObj const* const Arch =
                static_cast<Archetype::EqObj const*>(Obj->Archetype);
            if (Arch != nullptr && Arch->bNomad)
                Bucket += 3;
        }

        PushNode(Bucket, Go, DistSq, Lod);
    }

    // FUN_0051f7d0: the ObjDB bucket head for ManagerKind (FUN_00540fc0), then every Gameobj
    // down the +0x14 chain to FUN_0051f2c0. No other test.
    void EnqueueByManagerKind(EClientGameobjKind ManagerKind, FSpaceCamera* Cam)
    {
        int Kind = static_cast<int>(ManagerKind);
        FClientGameobj* Go = ClientObjDb_GetKindHead(&Kind);
        for (; Go != nullptr; Go = Go->ListNext)
            EnqueueObject(Go, Cam);
    }

    // MSVC6 _ISORT_MAX — retail compares element count to 0x11 / byte span to 0x40.
    constexpr int kSortInsertThreshold = 0x11; // 17

    float NodeKey(FZItem* Node)
    {
        return Node->DistSq;
    }

    // Median of {A,B,C} by DistSq, inlined in FUN_0051fc90 and FUN_0051eda0.
    FZItem* MedianOfThree(FZItem* FirstNode, FZItem* MidNode,
                                  FZItem* LastNode)
    {
        float const KFirst = NodeKey(FirstNode);
        float const KMid = NodeKey(MidNode);
        float const KLast = NodeKey(LastNode);
        FZItem* Pivot = FirstNode;
        if (KFirst <= KMid)
        {
            if (KFirst <= KLast)
            {
                Pivot = MidNode;
                if (KLast < KMid)
                    Pivot = LastNode;
            }
        }
        else
        {
            Pivot = MidNode;
            if (KMid <= KLast)
            {
                Pivot = FirstNode;
                if (KLast < KFirst)
                    Pivot = LastNode;
            }
        }
        return Pivot;
    }

    // FUN_0051fd90 — Hoare-style partition, DistSq descending (far first).
    // Pivot is a node object*; returns the split iterator.
    FZItem** PartitionDescending(FZItem** First, FZItem** Last,
                                         FZItem* Pivot)
    {
        float const PivotKey = NodeKey(Pivot);
        while (true)
        {
            while (PivotKey < NodeKey(*First))
                ++First;
            do
            {
                --Last;
            } while (NodeKey(*Last) < PivotKey);
            if (Last <= First)
                break;
            FZItem* Tmp = *First;
            *First = *Last;
            *Last = Tmp;
            ++First;
        }
        return First;
    }

    // Guarded insertion for [First, Last): rotate-to-front when key > *First.
    // Used for n < 17 and for the first 16 of the introsort finish.
    void InsertionSortGuarded(FZItem** First, FZItem** Last)
    {
        if (First == Last)
            return;
        FZItem** Next = First + 1;
        while (Next != Last)
        {
            FZItem* Key = *Next;
            FZItem** Hole = Next;
            if (NodeKey(*First) < NodeKey(Key))
            {
                for (; First != Hole; --Hole)
                    *Hole = *(Hole - 1);
                *First = Key;
            }
            else
            {
                while (true)
                {
                    if (!(NodeKey(*(Hole - 1)) < NodeKey(Key)))
                        break;
                    *Hole = *(Hole - 1);
                    --Hole;
                }
                *Hole = Key;
            }
            ++Next;
        }
    }

    // Unguarded insertion from First to Last (retail final pass after first 16 sorted).
    void InsertionSortUnguarded(FZItem** First, FZItem** Last)
    {
        for (FZItem** Next = First; Next != Last; ++Next)
        {
            FZItem* Key = *Next;
            FZItem** Hole = Next;
            while (true)
            {
                if (!(NodeKey(*(Hole - 1)) < NodeKey(Key)))
                    break;
                *Hole = *(Hole - 1);
                --Hole;
            }
            *Hole = Key;
        }
    }

    // FUN_0051fc90 — recursive introsort body; leaves runs < 17 for the top finish.
    void SortRangeIntrosort(FZItem** First, FZItem** Last)
    {
        for (;;)
        {
            int const Count = static_cast<int>(Last - First);
            if (Count < kSortInsertThreshold)
                return;

            FZItem* Pivot = MedianOfThree(*First, *(First + Count / 2), *(Last - 1));

            FZItem** Mid = Last;
            FZItem** Left = First;
            float const PivotKey = NodeKey(Pivot);
            while (true)
            {
                while (PivotKey < NodeKey(*Left))
                    ++Left;
                do
                {
                    --Mid;
                } while (NodeKey(*Mid) < PivotKey);
                if (Mid <= Left)
                    break;
                FZItem* Tmp = *Left;
                *Left = *Mid;
                *Mid = Tmp;
                ++Left;
            }

            // Recurse on the smaller partition; iterate on the larger (retail).
            if ((Left - First) < (Last - Left))
            {
                SortRangeIntrosort(First, Left);
                First = Left;
            }
            else
            {
                SortRangeIntrosort(Left, Last);
                Last = Left;
            }
        }
    }

    // FUN_0051eda0 — per-bucket DistSq descending. n<17: guarded insertion;
    // else partition (FUN_0051fd90) + recurse (FUN_0051fc90) until ≤16, then
    // guarded insertion of first 16 + unguarded insertion of the rest.
    void SortBucketNodes(std::vector<FZItem*>& Nodes)
    {
        FZItem** First = Nodes.empty() ? nullptr : &Nodes[0];
        FZItem** Last = First + static_cast<int>(Nodes.size());
        int const Count = static_cast<int>(Nodes.size());
        if (Count < 2)
            return;

        if (Count < kSortInsertThreshold)
        {
            InsertionSortGuarded(First, Last);
            return;
        }

        FZItem** Lo = First;
        FZItem** Hi = Last;
        while ((Hi - Lo) > 16)
        {
            int const Span = static_cast<int>(Hi - Lo);
            FZItem* Pivot = MedianOfThree(*Lo, *(Lo + Span / 2), *(Hi - 1));
            FZItem** Mid = PartitionDescending(Lo, Hi, Pivot);
            if ((Mid - Lo) < (Hi - Mid))
            {
                SortRangeIntrosort(Lo, Mid);
                Lo = Mid;
            }
            else
            {
                SortRangeIntrosort(Mid, Hi);
                Hi = Mid;
            }
        }

        InsertionSortGuarded(First, First + 16);
        InsertionSortUnguarded(First + 16, Last);
    }

    void SortBuckets()
    {
        for (unsigned B = 0; B < kDrawBucketCount; ++B)
            SortBucketNodes(G_Buckets[B].Nodes);
    }

    // FUN_00422910 — re-push Win3D perspective (FUN_00422820) from camera slots before each
    // drain, read in this order.
    void CameraApplyPerspective(FSpaceCamera* Cam)
    {
        float const ZFar = Cam->GetZFar();
        float const ZNear = Cam->GetZNear();
        float const Aspect = Cam->GetAspect();
        float const FovY = Cam->GetFovYDeg();
        Win3DSetPerspective(FovY, Aspect, ZNear, ZFar);
    }

    // FUN_004ffab0 / FUN_004ffbb0 / FUN_004ffe50 (GateTunnel draw, frame, layer strips) are
    // FGateTunnel::Draw / BuildFrame / EmitLayer in gate_tunnel.cpp.

    // FUN_004f7db0 lives in effect_base.cpp as FEffectBase::FreezeToWorld.

    // FUN_004f7810 — walk DAT_00674ce4[Type] with death/LOD arms.
    void EffectTypeBucketWalk(FSpaceCamera* Cam, float Dt, int TypeIndex)
    {
        // No range test.
        void* Node = G_EffectTypeBuckets[TypeIndex].Head;
        while (Node != nullptr)
        {
            unsigned char* const N = static_cast<unsigned char*>(Node);
            void* const Next = *reinterpret_cast<void**>(N + 4);
            void* const Effect = *reinterpret_cast<void**>(N + 8);
            Node = Next;

            // No null test on the node's effect or on anything read through it.
            unsigned char* const E = static_cast<unsigned char*>(Effect);
            if ((*reinterpret_cast<unsigned char*>(E + 8) & 2u) != 0)
            {
                // LAB_004f7910 — already dead: unlink + release (signed DEC, slot +0x20 at <= 0).
                EffectTypeListUnlinkAndRelease(Effect);
                continue;
            }

            // EngineIndex == -1 -> translation (+0x34) += Dt * velocity (+0x4c). x87: the x and
            // y products stay unrounded; the z product is stored to a float first.
            if (*reinterpret_cast<int*>(E + 0x40) == -1)
            {
                float* const Pos = reinterpret_cast<float*>(E + 0x34);
                float const* const Vel = reinterpret_cast<float const*>(E + 0x4c);
                float const StepZ = Dt * Vel[2];
                Pos[0] = static_cast<float>(static_cast<double>(Dt) * Vel[0] + Pos[0]);
                Pos[1] = static_cast<float>(static_cast<double>(Dt) * Vel[1] + Pos[1]);
                Pos[2] = StepZ + Pos[2];
            }

            // Slot +0x14 (update/visibility).
            FEffectBase* const TypedEffect = (FEffectBase*)Effect;
            unsigned char const UpdateOk = TypedEffect->UpdateVisibility(Dt, Cam);

            // FUN_004fd560 (EXIST_OFFSCREEN), read without a null test.
            FEffectTypeInfo const* const Info =
                EffectTypeInfoByIndex(*reinterpret_cast<int*>(E + 0x5c));
            if (Info->VisibilityOffscreen == 0 && UpdateOk == 0)
            {
                // The inlined FUN_004f7af0 link.
                EffectTypeListLink(Effect);
                static_cast<FEffectBase*>(Effect)->FreezeToWorld();
                *reinterpret_cast<unsigned char*>(E + 8) =
                    static_cast<unsigned char>(*reinterpret_cast<unsigned char*>(E + 8) | 2u);
            }
            else
            {
                // Slot +0x18 (tick/draw); slot +0x1c only when both it and the +0x14 result are
                // non-zero.
                unsigned char const DrawOk =
                    TypedEffect->TickDraw(Dt, Cam);
                if (DrawOk != 0 && UpdateOk != 0)
                {
                    TypedEffect->BuildGeometry(Cam);
                }
            }

            // A dead effect goes straight to the unlink; a live one asks slot +0x10 (IsAlive)
            // and is unlinked only when that returns 0.
            if ((*reinterpret_cast<unsigned char*>(E + 8) & 2u) == 0)
            {
                if (TypedEffect->IsAlive() != 0)
                    continue;
            }
            EffectTypeListUnlinkAndRelease(Effect);   // LAB_004f7910
        }
    }

    // FUN_004f7480 — effects RS/Alchemy frame; beam batch reset+flush. FUN_0051c200 calls it
    // with TypeIndex 0x25, 0x1b and -1.
    void SpaceEffectsDrawFrame(FSpaceCamera* Cam, float Dt, int TypeIndex)
    {
        // No camera / pipeline null checks in retail.
        if (GetGameOption(kGameOptEffectsGroup) == 0)
            return;

        CameraApplyPerspective(Cam);   // FUN_00422910

        // Two render-state savers (retail RAII objects): 0x17 -> 4, 0x1a -> 0.
        IRenderPipeline* const Pipe = DALib::Pipeline;
        int SavedRs17 = 0;
        int SavedRs1a = 0;
        Pipe->GetRenderState(D3DRS_ZFUNC, &SavedRs17);
        Pipe->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
        Pipe->GetRenderState(D3DRS_DITHERENABLE, &SavedRs1a);
        Pipe->SetRenderState(D3DRS_DITHERENABLE, 0);

        ScreenQuadSetMaterialKey(DACOM_CRC::GetCRC32("effects"));

        IAlchemy* Alchemy = DALib::Alchemy;
        if (Alchemy != nullptr)
        {
            unsigned char const UseBatcher =
                static_cast<unsigned char>(GetGameOption(kGameOptBatchParticles) != 0 ? 1 : 0);
            Alchemy->SetUseMaterialBatcher(UseBatcher);

            unsigned char Flags[2];
            Flags[0] = static_cast<unsigned char>(GetGameOption(kGameOptUseDynVb) == 1 ? 1 : 0);
            Flags[1] = static_cast<unsigned char>(GetGameOption(kGameOptCopyToDynVb) == 1 ? 1 : 0);
            Alchemy->SetDrawFlags(Flags);
        }

        // No null check on the batcher.
        DALib::MaterialBatcher->SetBinOrder(1u, -1);

        float ClampedDt = Dt;
        if (ClampedDt > 0.1f)
            ClampedDt = 0.1f;

        unsigned char bDidBeamBatch = 0;

        if (TypeIndex < EFT_CUSTOM_EFFECTS)
        {
            if (GetGameOption(kGameOptParticles) != 0 && Alchemy != nullptr)
            {
                GENRESULT const BeginHr = Alchemy->BeginDraw();
                if (static_cast<long>(BeginHr) >= 0)
                {
                    if (TypeIndex >= 0)
                    {
                        EffectTypeBucketWalk(Cam, ClampedDt, TypeIndex);
                    }
                    else
                    {
                        for (int T = 0; T < EFT_CUSTOM_EFFECTS; ++T)
                            EffectTypeBucketWalk(Cam, ClampedDt, T);
                    }
                    Alchemy->EndDraw();
                }
            }
        }

        // TypeIndex < 0 (signed) walks types 0x2c..0x2e. With DAT_00674f58 (live beams) and
        // DAT_00674f54 (the "code_beam" shape) both non-zero the batch DAT_00674c7c is reset
        // inline first, then FUN_0046ecd0 and FUN_004fcb40(1), and FUN_004f9de0 flushes it
        // after the walk.
        if (TypeIndex < 0)
        {
            if (G_BeamEffectLiveCount != 0 && G_BeamEffectTexture != nullptr)
            {
                bDidBeamBatch = 1;
                G_EffectVertexBatch.VertCount = 0;
                G_EffectVertexBatch.bStripMode = 0;
                G_EffectVertexBatch.bCloseLoop = 0;
                G_EffectVertexBatch.StripCounter = 0;
                G_EffectVertexBatch.IndexCount = 0;
                G_EffectVertexBatch.GrowVertices();
                G_EffectVertexBatch.EnsureIndices(1);
            }
            for (int T = EFT_CODE_PROJ_EFFECT; T < kEffectTypeCount; ++T)
                EffectTypeBucketWalk(Cam, ClampedDt, T);
            if (bDidBeamBatch != 0)
            {
                EffectVertexBatchFlush(AsCamera(Cam));
            }
        }

        // Two more savers: 0x17 -> 4, 0xe -> 0.
        int SavedRs17Inner = 0;
        Pipe->GetRenderState(D3DRS_ZFUNC, &SavedRs17Inner);
        Pipe->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
        int SavedRs0e = 0;
        Pipe->GetRenderState(D3DRS_ZWRITEENABLE, &SavedRs0e);
        Pipe->SetRenderState(D3DRS_ZWRITEENABLE, 0);

        ScreenQuadFlush(Pipe, G_EffectsListKey); // FUN_00403ea0(Pipeline, DAT_00674f3c)

        Pipe->SetRenderState(D3DRS_ZWRITEENABLE, SavedRs0e);
        Pipe->SetRenderState(D3DRS_ZFUNC, SavedRs17Inner);

        // The sort kind is a literal, not the beam flag. No null check.
        DALib::MaterialBatcher->SetBinSortKind(
            1u, reinterpret_cast<void*>(static_cast<uintptr_t>(kBatchSortAscending)));

        Pipe->SetRenderState(D3DRS_DITHERENABLE, SavedRs1a);
        Pipe->SetRenderState(D3DRS_ZFUNC, SavedRs17);
    }

    void FZItemGameObject::Draw(FSpaceCamera* Cam)
    {
        // The OutLod written by the slot is never read.
        unsigned OutLod = 0;
        static_cast<FClientGameobj*>(static_cast<IObjInspect*>(Object))
            ->SubmitDraw(&OutLod, Cam, LodWord, nullptr, nullptr);
    }

    int FZItemGameObject::GetCategory()
    {
        EClientGameobjKind const ManagerKind =
            static_cast<FClientGameobj*>(static_cast<IObjInspect*>(Object))->ManagerKind;
        if (ManagerKind == EClientGameobjKind::Ship)
            return 0;
        return (ManagerKind != EClientGameobjKind::Solar) ? 2 : 1;
    }

    void FZItemNebula::Draw(FSpaceCamera* Cam)
    {
        // LAB_0051ba00
        ((FNebulaEffect*)Object)->DrawFogShell(Cam);
        ((FNebulaEffect*)Object)->DrawExteriorBitsPass(Cam);
#ifdef FREELANCER_RP9
        // Port-only: a nebula the Direct3D 9 volumetric nebula draws from beyond the opaque
        // scene's far plane is ray-marched here, in this bucket's painter order (its retail
        // fill sheets and bits above are then left out by their own gates). A no-op otherwise.
        NebulaVolume::DrawBackground((FNebulaEffect*)Object, AsCamera(Cam));
#endif
    }

    void FZItemAsteroidField::Draw(FSpaceCamera* Cam)
    {
        (static_cast<FCliAsteroid*>(Object))->DrawFarPiece(Cam, static_cast<int>(LodWord));
    }

    void FZItemPlanetRing::Draw(FSpaceCamera* Cam)
    {
        ((FTexturePanels*)Object)->DrawPiece(Cam, (int)LodWord);
    }

    // FUN_0051f810 -- the bucket's begin / end are read once. No test of the bucket index, a
    // node, the pipeline or the batcher.
    void DrainBucket(FSpaceCamera* Cam, unsigned Bucket, bool bToggleDepthRs)
    {
        CameraApplyPerspective(Cam);

        std::vector<FZItem*>& Nodes = G_Buckets[Bucket].Nodes;
        size_t const Count = Nodes.size();
        for (size_t I = 0; I < Count; ++I)
        {
            if (bToggleDepthRs)
            {
                DALib::Pipeline->SetRenderState(D3DRS_ZENABLE, 0);
                DALib::Pipeline->SetRenderState(D3DRS_ZWRITEENABLE, 0);
            }
            Nodes[I]->Draw(Cam);
            DALib::MaterialBatcher->FlushAll(1u);
        }
        if (bToggleDepthRs)
        {
            DALib::Pipeline->SetRenderState(D3DRS_ZENABLE, 1);
            DALib::Pipeline->SetRenderState(D3DRS_ZWRITEENABLE, 1);
        }
    }

    // FUN_0051f8d0 -- the far drain of bucket 2 at the 250000 far plane. No test of the camera
    // or the pipeline.
    void DrainBucket2Special(FSpaceCamera* Cam)
    {
        if (G_Buckets[2].Nodes.empty())
            return;
        IRenderPipeline* const Pipe = DALib::Pipeline;
        Pipe->SetRenderState(D3DRS_ZENABLE, 0);
        Pipe->SetRenderState(D3DRS_ZWRITEENABLE, 0);
        CameraSetNearFar(Cam, AsCamera(Cam)->GetZNear(), kDefaultZFar);
        DrainBucket(Cam, 2, true);
    }

    // FUN_0051f950 -- the near drain around the field controller DAT_00675244. Every near/far
    // switch is FUN_0040fb60 with far = DAT_00610534 (250000) or the pass far plane
    // (DAT_00675368). No test of the camera, the pipeline, the controller's vtable entries, a
    // star entry or its slot.
    void DrainMeshAndAsteroids(FSpaceCamera* Cam)
    {
        ICamera* const Camera = AsCamera(Cam);
        IRenderPipeline* const Pipe = DALib::Pipeline;

        CameraSetNearFar(Cam, Camera->GetZNear(), G_DrawCtxZFar);
        Pipe->SetRenderState(D3DRS_ZENABLE, 1);
        Pipe->SetRenderState(D3DRS_ZWRITEENABLE, 1);
        Pipe->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);

        // DAT_0067524c non-zero -> the in-field path.
        if (G_FieldEnclosure == kFieldEnclosureNone)
        {
            if (G_FieldController != nullptr)
            {
                CameraSetNearFar(Cam, Camera->GetZNear(), kDefaultZFar);
                G_FieldController->DrawFogShell(Cam);
                CameraSetNearFar(Cam, Camera->GetZNear(), G_DrawCtxZFar);
                DrainBucket(Cam, 1, false);
                CameraSetNearFar(Cam, Camera->GetZNear(), kDefaultZFar);
                G_FieldController->DrawExteriorBitsPass(Cam);
                CameraSetNearFar(Cam, Camera->GetZNear(), G_DrawCtxZFar);
            }
            // FUN_00509f30 (TexturePanels list), FUN_00520170.
            DrainBucket(Cam, 0, false);
            TexturePanelsDrawAll(Cam);
            CliAsteroidTickFields(Cam);
            DrainBucket(Cam, 3, false);
            return;
        }

        DrainBucket(Cam, 0, false);
        DrainBucket(Cam, 3, false);

        // Partial enclosure.
        if (G_FieldEnclosure == kFieldEnclosurePartial)
        {
            CameraSetNearFar(Cam, Camera->GetZNear(), kDefaultZFar);
            G_FieldController->DrawFogShell(Cam);
            CameraSetNearFar(Cam, Camera->GetZNear(), G_DrawCtxZFar);
        }

        G_FieldController->DrawFogOverlay(Cam);

        // FUN_0050c620's star list. Arguments are evaluated right to left (B first, then the
        // two factors of A); then the star's DrawBillboard (LAB_005118d0).
        FStarSphereListNode* const Sentinel = StarSphereList()->Sentinel;
        for (FStarSphereListNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        {
            FStarSphere* const Star = Node->Sphere;
            float const B = G_FieldController->GetSunBurnthroughScaler();
            float const Scale28 = G_FieldController->GetSunBurnthroughIntensity();
            float const Factor24 = G_FieldController->GetInsideFade();
            float const A = static_cast<float>(static_cast<double>(Factor24) * Scale28);
            Star->DrawBillboard(Cam, A, B);
        }

        if (G_FieldEnclosure == kFieldEnclosurePartial)
        {
            CameraSetNearFar(Cam, Camera->GetZNear(), kDefaultZFar);
            G_FieldController->DrawExteriorBitsPass(Cam);
            CameraSetNearFar(Cam, Camera->GetZNear(), G_DrawCtxZFar);
        }

        DrainBucket(Cam, 1, false);
        G_FieldController->DrawExclusionShell(Cam);
        CliAsteroidTickFields(Cam);
        DrainBucket(Cam, 4, false);
#ifdef FREELANCER_RP9
        // Port-only: the volumetric nebula (nebula_volume.cpp) replaces the interior puffs.
        if (!NebulaVolume::ReplacesClouds(G_FieldController))
#endif
            G_FieldController->DrawCloudsPanels(Cam);
    }

    // DAT_00675240 -- the lazily acquired 3DMathEngine (FUN_005b73e0) shared by FUN_0051ba20
    // and FUN_0051be10 only.
    I3DMathEngine* G_PanelPieceMath = nullptr;

    // FUN_0051ba20 (thiscall on the camera; caller FUN_0051c200): every CliAsteroid field (FUN_005201d0's registry) becomes ZItemAsteroidField nodes. A one-piece
    // field is one node scored by its zone's exterior distance; a multi-piece field is one node
    // per piece that survives the camera's sphere test. Near go to bucket 0 (FUN_0051efc0), far
    // to bucket 2 (FUN_0051f140).
    void EnqueueAsteroidFarHelpers(FSpaceCamera* Cam)
    {
        // 0.25 is the double DAT_005cbf58; both are computed before the option tests.
        float const RangeSq = static_cast<float>(static_cast<double>(G_SpaceDrawRange) *
                                                 G_SpaceDrawRange);
        double const NearRange = static_cast<double>(G_SpaceDrawRange) * 0.25;
        float const NearThresh = static_cast<float>(NearRange * NearRange);

        if (GetGameOption(kGameOptEffectsGroup) == 0)
            return;
        if (GetGameOption(kGameOptAsteroidExt) == 0)
            return;

        struct FCtx
        {
            FSpaceCamera* Cam;
            float RangeSq;
            float NearThresh;
        } Ctx = {Cam, RangeSq, NearThresh};

        // The registry's node payload is read unchecked, as CliAsteroidForEach does.
        CliAsteroidForEach(
            [](FCliAsteroid* Field, void* Raw) {

                FCtx* const C = static_cast<FCtx*>(Raw);
                FSpaceCamera* const LocalCam = C->Cam;
                // No test of the zone.
                Universe::IZone* const Zone = Field->Zone;

                int const Pieces = Field->PieceCount(); // FUN_00524730
                if (Pieces == 1)
                {
                    // The difference built as a float Vector (FUN_00422b80).
                    Vector const& ZonePos = Zone->GetPos();
                    float const ZoneX = ZonePos.x;
                    float const ZoneY = ZonePos.y;
                    float const ZoneZ = ZonePos.z;
                    const float* const CamPos = (const float*)
                        LocalCam->GetPosition();
                    float const Dz = ZoneZ - CamPos[2];
                    float const Dy = ZoneY - CamPos[1];
                    float const Dx = ZoneX - CamPos[0];

                    // The sum stays unrounded (stored as a float only for the bits); one Newton
                    // step of the 0x5f3759df estimate times the sum gives the length.
                    double const SumSq = (static_cast<double>(Dy) * Dy +
                                          static_cast<double>(Dz) * Dz) +
                                         static_cast<double>(Dx) * Dx;
                    float const SumSqF = static_cast<float>(SumSq);
                    int Bits = 0;
                    memcpy(&Bits, &SumSqF, sizeof(float));
                    Bits = 0x5f3759df - (Bits >> 1);
                    float Estimate = 0.0f;
                    memcpy(&Estimate, &Bits, sizeof(float));
                    float const Length = static_cast<float>(
                        (1.5 - static_cast<double>(Estimate) * Estimate * SumSq * 0.5) *
                        Estimate * SumSq);

                    // 1 / Length stays unrounded; each component is stored as a float.
                    double const InvLength = 1.0 / static_cast<double>(Length);
                    Vector Dir;
                    Dir.x = static_cast<float>(Dx * InvLength);
                    Dir.y = static_cast<float>(Dy * InvLength);
                    Dir.z = static_cast<float>(Dz * InvLength);

                    // Greater or unordered -> (Length - hit)^2; otherwise NearThresh.
                    float const Surface = Zone->GetRadiusAlong(Dir);
                    float Score = C->NearThresh;
                    if (!(Length <= Surface))
                    {
                        double const Over = static_cast<double>(Length) - Surface;
                        Score = static_cast<float>(Over * Over);
                    }

                    if (Score < C->NearThresh)
                        Score = C->NearThresh;

                    // >= RangeSq or unordered to bucket 2, strictly below to bucket 0.
                    if (Score < C->RangeSq)
                        PushTypedNode(0, kDrawNodeAsteroidFar, Field, Score, 0);
                    else
                        PushTypedNode(2, kDrawNodeAsteroidFar, Field, Score, 0);
                    return;
                }

                // A count <= 0 enqueues nothing.
                const float* const CamPos = (const float*)
                    LocalCam->GetPosition();
                Transform const* const View =
                    LocalCam->GetViewTransform();
                for (int Piece = 0; Piece < Pieces; ++Piece)
                {
                    // FUN_00524740; the result is not tested.
                    float World[3] = {};
                    Field->GetPiecePos(Piece, World);

                    if (G_PanelPieceMath == nullptr)
                        G_PanelPieceMath = Acquire3DMathEngine();
                    Vector ViewPoint;
                    G_PanelPieceMath->TransformXform(ViewPoint, *View,
                                                     *reinterpret_cast<Vector const*>(World));
                    float ViewPoint3[3] = {ViewPoint.x, ViewPoint.y, ViewPoint.z};

                    // FUN_005247f0 radius; a sphere class below 3 skips the piece.
                    float const Radius = Field->GetPieceRadius(Piece);
                    unsigned short const Class =
                        static_cast<unsigned short>(LocalCam->ClassifySphere((const Vector*)ViewPoint3, Radius));
                    if (Class < 3)
                        continue;

                    // Kept as a double.
                    double const Px = static_cast<double>(World[0]) - CamPos[0];
                    double const Py = static_cast<double>(World[1]) - CamPos[1];
                    double const Pz = static_cast<double>(World[2]) - CamPos[2];
                    double DistSq = (Pz * Pz + Px * Px) + Py * Py;

                    // RangeSq strictly below DistSq -> bucket 2.
                    if (static_cast<double>(C->RangeSq) < DistSq)
                    {
                        PushTypedNode(2, kDrawNodeAsteroidFar, Field, static_cast<float>(DistSq),
                                      static_cast<unsigned>(Piece));
                        continue;
                    }

                    // Only NearThresh strictly above DistSq rescales. DAT_005dc028 is 0.05f
                    // widened to a double, not 0.05. Then bucket 0.
                    if (static_cast<double>(C->NearThresh) > DistSq)
                        DistSq = (static_cast<double>(C->NearThresh) - DistSq) *
                                     static_cast<double>(0.05f) +
                                 C->NearThresh;
                    PushTypedNode(0, kDrawNodeAsteroidFar, Field, static_cast<float>(DistSq),
                                  static_cast<unsigned>(Piece));
                }
            },
            &Ctx);
    }

    char const kGamecamCppPath[] = "E:\\FL\\Scratch\\Source\\Client\\space\\Gamecam.cpp"; // 005dbfe8

    // FUN_0051be10 (thiscall on the camera, called from FUN_0051c200): every TexturePanels
    // instance (FUN_00509f20's list) becomes draw nodes of kind PTR_LAB_005dc06c.
    // A one-piece ring is one node scored by its zone's exterior distance; a multi-piece ring
    // is one node per piece that survives the camera's sphere test. Near pieces go to bucket 0
    // (FUN_0051efc0), far ones to bucket 2 (FUN_0051f140). FTexturePanels has no live vtable in
    // the port, so slots 10 / 6 / 7 / 8 (+0x28 / +0x18 / +0x1c / +0x20) are called by name.
    void EnqueueTexturePanelPieces(FSpaceCamera* Cam)
    {
        // 5000.0f is DAT_005d3d24. The difference of two floats squared is exact in a double.
        double const RangeDiff = static_cast<double>(G_SpaceDrawRange) - 5000.0;
        float const Thresh = static_cast<float>(RangeDiff * RangeDiff);

        if (GetGameOption(kGameOptEffectsGroup) == 0)
            return;


        // FUN_00509f20's list.
        FTexturePanelsGlobalNode* const Sentinel = TexturePanelsListSentinel();
        for (FTexturePanelsGlobalNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        {
            FTexturePanels* const Panels = Node->Panels;
            Universe::IZone const* const Zone = Panels->GetZone(); // vt+0x28
            int const Pieces = Panels->GetPieceCount();            // vt+0x18

            if (Pieces == 1)
            {
                // The difference built as a float Vector (FUN_00422b80).
                const float* const CamPos = (const float*)Cam->GetPosition();
                Vector const& ZonePos = Zone->GetPos();
                float const Dx = ZonePos.x - CamPos[0];
                float const Dy = ZonePos.y - CamPos[1];
                float const Dz = ZonePos.z - CamPos[2];

                // The sum stays unrounded (stored as a float only for the bits); one Newton
                // step of the 0x5f3759df estimate times the sum gives the length.
                double const SumSq = static_cast<double>(Dz) * Dz + static_cast<double>(Dy) * Dy +
                                     static_cast<double>(Dx) * Dx;
                float const SumSqF = static_cast<float>(SumSq);
                int Bits = 0;
                memcpy(&Bits, &SumSqF, sizeof(float));
                Bits = 0x5f3759df - (Bits >> 1);
                float Estimate = 0.0f;
                memcpy(&Estimate, &Bits, sizeof(float));
                float const Length = static_cast<float>(
                    (1.5 - static_cast<double>(Estimate) * Estimate * SumSq * 0.5) * Estimate *
                    SumSq);
                double const InvLength = 1.0 / static_cast<double>(Length);
                Vector Dir;
                Dir.x = static_cast<float>(Dx * InvLength);
                Dir.y = static_cast<float>(Dy * InvLength);
                Dir.z = static_cast<float>(Dz * InvLength);

                // Past the zone surface (or unordered) the
                // score is the squared overshoot, floored at 1.0f; otherwise 1.0f.
                float const Surface = Zone->GetRadiusAlong(Dir);
                float Score = 1.0f;
                if (!(Length <= Surface))
                {
                    double const Over = static_cast<double>(Length) - Surface;
                    Score = static_cast<float>(Over * Over);
                    if (Score < 1.0f)
                        Score = 1.0f;
                }

                // Below the threshold -> bucket 0, else (or unordered) bucket 2.
                if (Score < Thresh)
                    PushTypedNode(0, kDrawNodeTexturePanelPiece, Panels, Score, 0);
                else
                    PushTypedNode(2, kDrawNodeTexturePanelPiece, Panels, Score, 0);
                continue;
            }

            const Vector* const CamPos = Cam->GetPosition();
            Transform const* const View = Cam->GetViewTransform();
            for (int Piece = 0; Piece < Pieces; ++Piece)
            {
                // A null centre is only reported -- retail goes on and reads through it.
                float const Radius = Panels->GetPieceFloat(Piece);
                float const* const Center = Panels->GetPiece(Piece);
                if (Center == nullptr)
                {
                    FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", kGamecamCppPath, 0x1ca,
                          "ring piece returned null position pointer");
                }

                // A sphere class below 3 (unsigned) skips the piece.
                if (G_PanelPieceMath == nullptr)
                    G_PanelPieceMath = Acquire3DMathEngine();
                Vector ViewCenter;
                G_PanelPieceMath->TransformXform(ViewCenter, *View,
                                                 *reinterpret_cast<Vector const*>(Center));
                float ViewCenter3[3] = {ViewCenter.x, ViewCenter.y, ViewCenter.z};
                unsigned short const Class =
                    static_cast<unsigned short>(Cam->ClassifySphere((const Vector*)ViewCenter3, Radius));
                if (Class < kSphereStraddling)
                    continue;

                // Kept as a double.
                double const Px = static_cast<double>(Center[0]) - CamPos->x;
                double const Py = static_cast<double>(Center[1]) - CamPos->y;
                double const Pz = static_cast<double>(Center[2]) - CamPos->z;
                double const DistSq = Px * Px + Pz * Pz + Py * Py;

                // Strictly past the threshold -> bucket 2.
                if (DistSq > static_cast<double>(Thresh))
                {
                    PushTypedNode(2, kDrawNodeTexturePanelPiece, Panels, static_cast<float>(DistSq),
                                  static_cast<unsigned>(Piece));
                    continue;
                }

                // Otherwise bucket 0 with the distance past the piece
                // radius (radius^2 subtracted), 0 when inside it.
                double const RadiusSq = static_cast<double>(Radius) * Radius;
                double Near = 0.0;
                if (!(DistSq < RadiusSq))
                    Near = DistSq - RadiusSq;
                PushTypedNode(0, kDrawNodeTexturePanelPiece, Panels, static_cast<float>(Near),
                              static_cast<unsigned>(Piece));
            }
        }
    }
}

I3DMathEngine* GameobjMathEngine()
{
    if (G_GameobjMathEngine == nullptr)
        G_GameobjMathEngine = Acquire3DMathEngine();
    return G_GameobjMathEngine;
}

void FGameobjEffectList::Sweep()
{
    // FUN_004fd040 -- the body is GameobjSweepEffectList above.
    GameobjSweepEffectList(this);
}

// FUN_00539b40 -- the body is FClientGameobj::UpdateDrawLighting (space_scene_obj_draw.cpp).
void FClientGameobj::UpdateDrawLighting(void* Camera)
{
    // FUN_00539b40 -- the body is GameobjUpdateDrawLighting above.
    GameobjUpdateDrawLighting(this, Camera);
}

double SpaceEffectPerfOneMinus()
{
    // FUN_004f70c0 -- the body is EffectPerfOneMinus above.
    return EffectPerfOneMinus();
}

void SpaceApplyObjectLighting(float const* Pos3, float Radius, void* Camera, unsigned NebulaBits,
                              unsigned FogFactorBits, unsigned char Flag)
{
    // FUN_005554d0 -- the body is StarSysApplyObjectLighting above.
    StarSysApplyObjectLighting(Pos3, Radius, Camera, NebulaBits, FogFactorBits, Flag);
}

void FSpaceCamera::SetNearFar(float ZNear, float ZFar)
{
    // FUN_0040fb60 -- the body is CameraSetNearFar above.
    CameraSetNearFar(this, ZNear, ZFar);
}

// FUN_00539820 -- the body is FClientGameobj::PickLod (space_scene_obj_draw.cpp), which
unsigned* FClientGameobj::PickLod(unsigned* OutLod, void* Camera, unsigned const* CachedLod)
{
    // FUN_00539820. A cached LOD whose low word is non-zero is the answer.
    if (CachedLod != nullptr && static_cast<unsigned short>(*CachedLod) != 0)
    {
        *OutLod = *CachedLod;
        return OutLod;
    }

    Vector Center;
    const float Radius = GetPhysicalRadius(Center);

    // Center into view space through DAT_006789d0.
    ICamera* const Cam = static_cast<ICamera*>(Camera);
    Transform const* const View = Cam->GetViewTransform();
    I3DMathEngine* const Math = GameobjMathEngine();
    Vector Local;
    Math->TransformXform(Local, *View, Center);
    const Vector ViewPoint = Local;

    // ClassifySphere -> low word; the high word is 0xffff and stays so when the class is below
    // 3.
    const int Classify = Cam->ClassifySphere(&ViewPoint, Radius);
    unsigned short LodLow = static_cast<unsigned short>(Classify);
    unsigned short LodHigh = 0xffffu;
    if (LodLow >= kSphereStraddling)
    {
        LodHigh = 0;
        const float DistSq = static_cast<float>(
            static_cast<double>(ViewPoint.y) * ViewPoint.y +
            static_cast<double>(ViewPoint.z) * ViewPoint.z +
            static_cast<double>(ViewPoint.x) * ViewPoint.x);

        // The archetype's companion, or 0 without an object or an archetype; its +0x08 / +0x0c
        // are read with no further test.
        unsigned char const* Companion = nullptr;
        if (Object != nullptr && Object->get_archetype() != nullptr)
            Companion = reinterpret_cast<unsigned char const*>(
                Object->get_archetype()->Companion);
        const float DistRender = *reinterpret_cast<float const*>(Companion + 0x08);
        const float DistLod = *reinterpret_cast<float const*>(Companion + 0x0c);

        // +0x08 > 0 (ordered): DistSq strictly below its square ends the pick; otherwise
        // high = 1.
        bool bDone = false;
        if (DistRender > 0.0f)
        {
            const double RenderSq = static_cast<double>(DistRender) * DistRender;
            if (DistSq < RenderSq)
                bDone = true;
            else
                LodHigh = 1;
        }
        // +0x0c > 0 and DistSq strictly above its square -> high = 2.
        if (!bDone && DistLod > 0.0f)
        {
            const double LodSq = static_cast<double>(DistLod) * DistLod;
            if (DistSq > LodSq)
                LodHigh = 2;
        }
    }

    *OutLod = static_cast<unsigned>(LodLow) | (static_cast<unsigned>(LodHigh) << 16);
    return OutLod;
}

// FUN_00539980 -- the body is FClientGameobj::SubmitDraw (space_scene_obj_draw.cpp).
unsigned* FClientGameobj::SubmitDraw(unsigned* OutLod, void* Camera, unsigned CachedLod,
                                  Transform const* Xform, void* Extra)
{
    // FUN_00539980
    PreSubmitDraw();

    // The pick is written back over the CachedLod argument, which every later read uses.
    unsigned PickedLod = 0;
    CachedLod = *PickLod(&PickedLod, Camera, &CachedLod);
    unsigned short const Lo = static_cast<unsigned short>(CachedLod);
    short const Hi = static_cast<short>(CachedLod >> 16);

    // Low word unsigned, high word signed.
    if (Lo < 3 || Hi >= 2)
    {
        *OutLod = CachedLod;
        return OutLod;
    }

    if (Hi < 1)
        GameobjSweepEffectList(&EffectList);

    UpdateDrawLighting(Camera);

    // 8 unless this object is the player's IObjRW. For the player, FUN_004022d0 (DAT_006101d4)
    // above 0.75 (or unordered) -> 0; otherwise FUN_00402580 with its result unused, and 0x80.
    unsigned long Flags = 0;
    if (static_cast<void*>(static_cast<IObjRW*>(this)) != GetPlayerObjRW())
    {
        Flags = 8u;
    }
    else
    {
        float Scale = 0.0f;
        Archetype::CliObjGetArchLodGlobals(&Scale);
        if (!(static_cast<double>(Scale) <= 0.75))
        {
            Flags = 0u;
        }
        else
        {
            BindRendererLods(static_cast<intptr_t>(get_index()), 1,
                             nullptr);
            Flags = 0x80u;
        }
    }

    if (Lo == 4 && Hi <= 0)
        Flags |= 0x100u;

    Transform const* SubmitXform = nullptr;
    if (Xform != nullptr)
    {
        SubmitXform = Xform;
        Flags |= 0x120u;
    }

    long const Inst = get_index();

    // FUN_005b3510(0) above 0 (NaN skips).
    if (GetPerfOptionSlot(kPerfSlotUseEnvironmentMaps) > 0.0f && Hi < 1 && Extra == nullptr)
    {
        // The companion's envmap name is read with no test; an empty name skips.
        Archetype::ICliObj* Companion = nullptr;
        if (Object != nullptr && Object->get_archetype() != nullptr)
            Companion = Object->get_archetype()->Companion;
        if (Archetype::CliObjEnvmapMaterial(Companion)[0] != '\0')
        {
            // The same lookup again for the name that is passed on.
            Archetype::ICliObj* NameCompanion = nullptr;
            if (Object != nullptr && Object->get_archetype() != nullptr)
                NameCompanion = Object->get_archetype()->Companion;
            char const* const Envmap = Archetype::CliObjEnvmapMaterial(NameCompanion);

            if (GetAppearanceOverrideMaterial(4u, &Extra))
                SetAppearanceInstanceValue(static_cast<intptr_t>(Inst),
                                   const_cast<char*>(Envmap));
        }
    }

    // Renderer Slot7; no test of the renderer.
    int const Result = DALib::Renderer->RenderInstance(Camera, static_cast<intptr_t>(Inst),
                                              1.0f, Flags, SubmitXform, Extra);

    // Signed compare.
    if (Result >= 3)
        DALib::MaterialBatcher->FlushAll(1u);

    *OutLod = static_cast<unsigned>(static_cast<unsigned short>(Result)) | 0xffff0000u;
    return OutLod;
}

namespace
{
    // The CEqObj fetch inlined twice in FUN_00533780.
    CEqObj* EquippedObjEqObj(FClientEquippedObj* Self)
    {
        CObject* const Obj = Self->Object;
        if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj)
            return nullptr;
        return static_cast<CEqObj*>(Obj);
    }
}

// FUN_00533780 -- the body is FClientEquippedObj::SubmitDraw (space_scene_obj_draw.cpp).
unsigned* FClientEquippedObj::SubmitDraw(unsigned* OutLod, void* Camera, unsigned CachedLod,
                                      Transform const* Xform, void* Extra)
{
    // FUN_00533780. Retail also tests `this` for null; a virtual call cannot arrive with a null
    // `this`. The CEqObj's +0x140 is read with no test; not above 0 (NaN included) takes the
    // else branch.
    CEqObj* const EqObj = EquippedObjEqObj(this);
    float const Fade = EqObj->CloakPercent;
    if (Fade > 0.0f)
    {
        // FUN_00533880 with the float's bits.
        float const Opacity = static_cast<float>(1.0 - static_cast<double>(Fade));
        bCloakOverrideActive = 1;
        long const Inst = get_index();
        void* OpacityBits = nullptr;
        memcpy(&OpacityBits, &Opacity, sizeof(float));
        SetAppearanceInstanceValue(static_cast<intptr_t>(Inst), OpacityBits);

        // A non-nomad archetype -> FUN_00533930(3, &Extra), the result ignored.
        CEqObj* const Again = EquippedObjEqObj(this);
        if (Again != nullptr)
        {
            Archetype::EqObj* const Arch = Again->eqobjarch();
            if (Arch != nullptr && !Arch->bNomad)
                GetAppearanceOverrideMaterial(3u, &Extra);
        }
    }
    else if (bCloakOverrideActive != 0)
    {
        bCloakOverrideActive = 0;
        long const Inst = get_index();
        if (Inst != -1)
            RemoveAppearanceInstanceValue(static_cast<int>(Inst));
    }

    // FUN_00539980 called directly (not through the vtable), Extra as possibly rewritten above.
    FClientGameobj::SubmitDraw(OutLod, Camera, CachedLod, Xform, Extra);
    return OutLod;
}

namespace
{
    // FUN_00504890 (thiscall on the Ship's +0x188 JumpShipEffect object): the gate tunnel at
    // +0x24, when there is one, draws for Cam (FUN_004ffab0).
    void JumpShipEffectDrawTunnel(void* JumpFx, FSpaceCamera* Cam)
    {
        FGateTunnel* const Tunnel = static_cast<FJumpShipEffect*>(JumpFx)->Tunnel;
        if (Tunnel != nullptr)
            Tunnel->Draw(Cam);
    }

    // FUN_00504880: the dword at +0x20. On the JumpShipEffect object it is the descriptor's
    // colour bytes (descriptor + 0x63). The nebula's vtable shares the same body as its
    // GetZone slot.
    unsigned char const* JumpShipEffectColor(void* JumpFx)
    {
        return *reinterpret_cast<unsigned char const**>(static_cast<unsigned char*>(JumpFx) +
                                                        0x20);
    }
}

void FGameCamera::DrawScene(unsigned LayerFlags, unsigned Param2)
{
    FSpaceCamera* Cam = (FSpaceCamera*)this;
    // FUN_0051c200. No test of `this`: the callers -- GameCamera slot 0x78, FUN_0051b250 and
    // the first-person scene slot FUN_00518420 -- run on a live camera.
    FGameCamera* const Gc = reinterpret_cast<FGameCamera*>(Cam);
    ICamera* const Camera = AsCamera(Cam);
    IRenderPipeline* const Pipe = DALib::Pipeline;

    // The player's +0x158 state 2 or 3 takes the jump arm. The state is read without a class
    // test.
    void* const PlayerRW = GetPlayerObjRW();
    if (PlayerRW != nullptr)
    {
        FClientShipGameobj* const Ship =
            static_cast<FClientShipGameobj*>(static_cast<IObjRW*>(PlayerRW));
        if (Ship->JumpState == kJumpStateJumpOutTunnel || Ship->JumpState == kJumpStateJumpInTunnel)
        {
            G_FieldController = nullptr;
            CameraSetNearFar(Cam, Camera->GetZNear(), G_SpaceDrawFar);

            // Anything but equal (NaN included) re-applies the base through FUN_0040f8c0.
            if (Camera->GetHalfFovXDeg() != Gc->BaseHalfFovXDeg)
                Cam->SetHalfFovX(Gc->BaseHalfFovXDeg);

            // The +0x188 object is used unchecked.
            JumpShipEffectDrawTunnel(Ship->JumpEffect, Cam);

            // The player's own draw -- the Ship's override FUN_00546050.
            unsigned Lod = 0xffff0000u;
            Ship->SubmitDraw(&Lod, Cam, Lod, nullptr, nullptr);

            // FUN_0042d680 is read afresh for each call.
            if (GetGameOption(kGameOptEffectsGroup) != 0)
            {
                SpaceEffectsDrawFrame(Cam, static_cast<float>(GetClientFrameTimeCapped()), EFT_JUMP_EFFECTS);
                SpaceEffectsDrawFrame(Cam, static_cast<float>(GetClientFrameTimeCapped()), EFT_ENGINE_FIRE);
            }

            // FUN_00504880's colour, when there is one, becomes the clear colour; +0xbc = 0 on
            // both exits.
            unsigned char const* const Rgb = JumpShipEffectColor(Ship->JumpEffect);
            if (Rgb != nullptr)
            {
                const unsigned char B2 = Rgb[2];
                const unsigned char B1 = Rgb[1];
                const unsigned char B0 = Rgb[0];
                Gc->Flags = static_cast<unsigned char>(Gc->Flags | kGameCameraClearColour);
                Gc->ClearColor[0] = B0;
                Gc->ClearColor[1] = B1;
                Gc->ClearColor[2] = B2;
            }
            Cam->FlagBC = 0;
            return;
        }
    }

    // FUN_0051ed20 empties the queue object DAT_00675360, which then takes both arguments;
    // FUN_0051b730 picks the field controller.
    ClearBuckets();
    G_SceneLayerFlags = LayerFlags;
    G_DrawCtxParam2 = Param2;
    SelectFieldControllers(Cam);
#ifdef FREELANCER_RP9
    // Port-only: which nebulae the Direct3D 9 volumetric nebula draws this frame, from inside
    // and from outside (nebula_volume.cpp); the opaque scene outside a nebula's core is drawn
    // out to DAT_006164d4. A no-op on RP8 and with no nebula in range.
    NebulaVolume::BeginFrame(Camera, G_FieldController, G_FieldEnclosure,
                             GetGameOption(kGameOptEffectsGroup) != 0 &&
                                 GetGameOption(kGameOptNebula) != 0,
                             G_SpaceDrawFar);
#endif

    // Outside a field's core, the far helpers enqueue against the
    // 250000 far plane (DAT_00610534), which then drops back to DAT_006164d4.
    if (G_FieldEnclosure != kFieldEnclosureInside)
    {
        CameraSetNearFar(Cam, Camera->GetZNear(), kDefaultZFar);
        EnqueueAsteroidFarHelpers(Cam);   // FUN_0051ba20
        EnqueueTexturePanelPieces(Cam);   // FUN_0051be10
        CameraSetNearFar(Cam, Camera->GetZNear(), G_SpaceDrawFar);
    }

    // The result is unread.
    Camera->GetZFar();

    // The pass's far plane -- DAT_006164d4, or inside a field's core the controller's vt+0x44
    // (handed DAT_006164d4, which it ignores); only a strictly larger value is cut back.
    float Far;
    if (G_FieldEnclosure != kFieldEnclosureInside)
    {
        Far = G_SpaceDrawFar;
    }
    else
    {
        Far = G_FieldController->FogFarClamp(G_SpaceDrawFar);
        if (Far > G_SpaceDrawFar)
            Far = G_SpaceDrawFar;
#ifdef FREELANCER_RP9
        // Port-only: the volumetric nebula's thinner haze would show objects popping in at
        // retail's clamp; while it is drawn the plane goes out to where its haze hides
        // everything (nebula_volume.cpp). Unchanged on RP8 / with the volume off.
        Far = NebulaVolume::InsideFarPlane(Far, G_SpaceDrawFar);
#endif
    }

    // The queue keeps it at +8.
    CameraSetNearFar(Cam, Camera->GetZNear(), Far);
    G_DrawCtxZFar = Far;

    // [OBJECTS] and SOLAR -- the object DB's spatial bucket filled around the camera position
    // out to DAT_00613ec8 = 10000.
    if (GetGameOption(kGameOptObjectsGroup) != 0 && GetGameOption(kGameOptSolar) != 0)
    {
        ISpatialPartition* const Bucket =
            static_cast<ISpatialPartition*>(GetClientObjDb()->GetBucket(kObjDbTypeMask));
        if (Bucket != nullptr)
        {
            Geometry::Sphere Query;
            Query.Radius = kObjDbFillRange;
            const float* const CamPos = (const float*)Camera->GetPosition();
            Query.Center.x = CamPos[0];
            Query.Center.y = CamPos[1];
            Query.Center.z = CamPos[2];
            FSpatialQueryResult Result; // on the stack in retail too
            Result.Capacity = 0x800;
            Result.Count = 0;
            if (SpatialFill(Bucket, &Query, &Result) != 0 && Result.Count != 0)
            {
                for (unsigned Index = 0; Index < Result.Count; ++Index)
                {
                    // A hit that is a GameObject and not one of the large solars (FUN_00540a40,
                    // walked below) goes to FUN_0051f2c0.
                    FClientGameobj* const Go = dynamic_cast<FClientGameobj*>(
                        static_cast<IObjRW*>(Result.Items[Index]));
                    if (Go != nullptr && !ClientObjDb_IsLargeSolar(Go))
                        EnqueueObject(Go, Cam);
                }
            }
        }

        // Then the HUD "Waypoint" item's watched object, one at most (FUN_004c83c0 /
        // FUN_004c83e0). Reached also when the bucket or its fill came back empty.
        int const WaypointObjects = HudWaypointGetObjectCount();
        for (int Index = 0; Index < WaypointObjects; ++Index)
        {
            IObjRW* const WaypointObj = HudWaypointGetObject(Index);
            EnqueueObject(static_cast<FClientGameobj*>(static_cast<IObjInspectImpl*>(WaypointObj)),
                          Cam);
        }
    }

    // Layer bit 0x40 with SOLAR and [OBJECTS] -- every large solar (FUN_00540a00 /
    // FUN_00540a20); with PLANETS off, one whose solar TypeBits has any of bits 0..2 is left
    // out. The narrowed solar pointer is read without a test.
    if ((LayerFlags & kSceneLayerSolars) != 0 && GetGameOption(kGameOptSolar) != 0 &&
        GetGameOption(kGameOptObjectsGroup) != 0)
    {
        size_t It = 0;
        for (FClientGameobj* Go = ClientObjDb_LargeSolarFirst(&It); Go != nullptr;
             Go = ClientObjDb_LargeSolarNext(&It))
        {
            if (GetGameOption(kGameOptPlanets) == 0)
            {
                CObject* const Obj = Go->Object;
                CSolar* Solar = nullptr;
                if (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar)
                    Solar = static_cast<CSolar*>(Obj);
                if ((static_cast<unsigned char>(Solar->TypeBits) & 7u) != 0)
                    continue;
            }
            EnqueueObject(Go, Cam);
        }
    }

    // With [OBJECTS], the ManagerKind walks FUN_0051f7d0 -- each one behind its own option
    // read: bit 0x20 + SHIPS -> 1; bit 0x80 + PROJECTILES -> 4, 5, 6;
    // bit 0x800 + DEBRIS -> 7; bit 0x400 + DEBRIS -> 10.
    if (GetGameOption(kGameOptObjectsGroup) != 0)
    {
        if ((LayerFlags & kSceneLayerShips) != 0 && GetGameOption(kGameOptShips) != 0)
            EnqueueByManagerKind(EClientGameobjKind::Ship, Cam);
        const unsigned ProjectileLayer = LayerFlags & kSceneLayerProjectiles; // kept across the calls
        if (ProjectileLayer != 0 && GetGameOption(kGameOptProjectiles) != 0)
            EnqueueByManagerKind(EClientGameobjKind::Guided, Cam);
        if (ProjectileLayer != 0)
        {
            if (GetGameOption(kGameOptProjectiles) != 0)
                EnqueueByManagerKind(EClientGameobjKind::Mine, Cam);
            if (GetGameOption(kGameOptProjectiles) != 0)
                EnqueueByManagerKind(EClientGameobjKind::CounterMeasure, Cam);
        }
        if ((LayerFlags & kSceneLayerLoot) != 0 && GetGameOption(kGameOptDebris) != 0)
            EnqueueByManagerKind(EClientGameobjKind::Loot, Cam);
        if ((LayerFlags & kSceneLayerDebris) != 0 && GetGameOption(kGameOptDebris) != 0)
            EnqueueByManagerKind(EClientGameobjKind::Debris, Cam);
    }

    SortBuckets(); // FUN_0051eda0
    Pipe->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW);

    // The background star sphere, unless the camera sits fully inside a
    // nebula (DAT_0067524c == 2), BACKGROUND (option 0x19) is off, or layer bit 0 is clear.
#ifdef FREELANCER_RP9
    // Port-only: while the volumetric nebula is drawn with real_background, also fully inside
    // (its haze fogs the stars with distance instead).
    if ((G_FieldEnclosure != kFieldEnclosureInside || NebulaVolume::ShowsRealBackground()) &&
        GetGameOption(kGameOptBackgroundGroup) != 0 && (LayerFlags & kSceneLayerBackground) != 0)
#else
    if (G_FieldEnclosure != kFieldEnclosureInside && GetGameOption(kGameOptBackgroundGroup) != 0 && (LayerFlags & kSceneLayerBackground) != 0)
#endif
        StarSphereDraw(Cam); // FUN_00552e70

    DrainBucket2Special(Cam);   // FUN_0051f8d0
    DrainMeshAndAsteroids(Cam); // FUN_0051f950

#ifdef FREELANCER_RP9
    // Port-only: the volumetric nebula over the opaque scene, before the effects (which retail
    // also draws over its fog). The scene above was drawn with near = GetZNear, far = Far.
    NebulaVolume::Draw(Camera, Camera->GetZNear(), Far);
#endif

    // Layer bit 0x100 -- the effects.
    if ((LayerFlags & kSceneLayerEffects) != 0)
    {
        EngineLightClearRegisteredList(); // FUN_004101e0
        Pipe->SetRenderState(D3DRS_LIGHTING, 0);
        if (GetGameOption(kGameOptEffectsGroup) != 0)
        {
            if (G_FieldEnclosure != kFieldEnclosureNone)
            {
                // FUN_00422440 with a black fog colour; the fog end is evaluated first.
                float Rgb[3] = {0.0f, 0.0f, 0.0f};
                float* const CamPos = (float*)Camera->GetPosition();
                const float FogEnd =
                    G_FieldController->FogFactor(CamPos);
                const float FogStart =
                    G_FieldController->GetFogStart();
                FogApplyColorAndRange(Rgb, FogStart, FogEnd);
            }
            else
            {
                Win3DDisable(kWin3DFog); // FUN_00421ed0(0xb60)
            }
            // Back to the pass's far plane.
            CameraSetNearFar(Cam, Camera->GetZNear(), Far);
            SpaceEffectsDrawFrame(Cam, static_cast<float>(GetClientFrameTimeCapped()), -1);
            Win3DDisable(kWin3DFog);
        }
        // GameCamera slot 0x50 -- FUN_0051c8b0, or the empty 0x00401e90 on FirstPerson.
        ((FGameCamera*)Cam)->ScaleEnvironmentEffects();
    }

    Pipe->SetRenderState(D3DRS_AMBIENT, -1);

    // The clear colour -- the system's space_color (FUN_00555460), or, with a field
    // controller whose vt+0x24 is >= 1.0f (ordered), its fog colour * 255.0f through ftol.
    unsigned char const* const SpaceColor = StarSysSpaceColor();
    unsigned char ClearR = SpaceColor[0];
    unsigned char ClearG = SpaceColor[1];
    unsigned char ClearB = SpaceColor[2];
    if (G_FieldController != nullptr)
    {
        // Port-only (FREELANCER_RP9): with the volumetric nebula's real_background the clear
        // stays the space colour; FillFogRgb still runs (it advances the background-lightning
        // timer and rand()).
        if (G_FieldController->GetInsideFade() >= 1.0f)
        {
            float Col[3];
            G_FieldController->FillFogRgb(Col);
#ifdef FREELANCER_RP9
            if (!NebulaVolume::ShowsRealBackground())
#endif
            {
                ClearR = static_cast<unsigned char>(static_cast<long>(static_cast<double>(Col[0]) * 255.0f));
                ClearG = static_cast<unsigned char>(static_cast<long>(static_cast<double>(Col[1]) * 255.0f));
                ClearB = static_cast<unsigned char>(static_cast<long>(static_cast<double>(Col[2]) * 255.0f));
            }
        }
    }
    Gc->Flags = static_cast<unsigned char>(Gc->Flags | kGameCameraClearColour);
    Gc->ClearColor[0] = ClearR;
    Gc->ClearColor[1] = ClearG;
    Gc->ClearColor[2] = ClearB;
    Cam->FlagBC = 0;
}

void SpaceCameraRenderSlot78(FSpaceCamera* Cam, unsigned LayerFlags, unsigned Param2)
{
    // FUN_0051b250. In-space frame: 0xffff, 0.
    if (!CameraHasWatchedObject(Cam))
        return;

    static_cast<FGameCamera*>(Cam)->FGameCamera::DrawScene(LayerFlags, Param2);

#ifdef FREELANCER_RP9
    // Port-only (no retail counterpart): the Direct3D 9 extension passes that need the finished
    // 3D scene and its depth, before the HUD clears the depth buffer below. A no-op on RP8.
    RenderEx::AfterSpaceScene();
#endif

    if (GetGameOption(kGameOptHud) != 0)
        ClearWin3DBuffers(kWin3DClearDepth);

    // A direct (non-virtual) call of FUN_0051daa0 -- HUDCamera's slot 0x78 body, which reads
    // only GameCamera fields. Its two stack arguments are never read.
    static_cast<FHudCamera*>(Cam)->FHudCamera::DrawScene(LayerFlags, Param2);
}

void FSpaceCamera::EnvFxBeginCockpitPass()
{
    // FUN_0051c970 (thiscall on the camera). Only with options 0x25, 0x24 and 0x28 all on, in
    // that order, each result tested before the next is read.
    if (GetGameOption(kGameOptEffectsGroup) == 0)
        return;
    if (GetGameOption(kGameOptSun) == 0)
        return;
    if (GetGameOption(kGameOptSunEffects) == 0)
        return;

    // Every entry of FUN_0050c620()'s list. Fully inside a
    // field (DAT_0067524c == 2) the entry's +0x34 is set to 1.0f; otherwise its slot 0x10 is
    // called with (camera, 1.0f).
    FStarSphereListNode* const Sent = StarSphereList()->Sentinel;
    for (FStarSphereListNode* N = Sent->Next; N != Sent; N = N->Next)
    {
        FStarSphere* const Item = N->Sphere;
        if (G_FieldEnclosure == kFieldEnclosureInside)
        {
            Item->ZoneOcclusion = 1.0f;
        }
        else
        {
            Item->Draw(this, 1.0f);
        }
    }
}

void FSpaceCamera::EnvFxEndCockpitPass()
{
    // FUN_0051c9e0 (thiscall on the camera): the same three option tests, then slot 0x14 with
    // (camera, 1.0f) on every entry -- no field-quality branch here.
    if (GetGameOption(kGameOptEffectsGroup) == 0)
        return;
    if (GetGameOption(kGameOptSun) == 0)
        return;
    if (GetGameOption(kGameOptSunEffects) == 0)
        return;

    FStarSphereListNode* const Sent = StarSphereList()->Sentinel;
    for (FStarSphereListNode* N = Sent->Next; N != Sent; N = N->Next)
    {
        FStarSphere* const Item = N->Sphere;
        Item->DrawFlare(this, 1.0f);
    }
}

int SpaceCollectNearbyLights(void** OutSlots, int MaxSlots, float const* Pos3, float Radius,
                             unsigned char Flag)
{
    return CollectNearbyLights(OutSlots, MaxSlots, Pos3, Radius, Flag);
}

void SpaceResetFarClip()
{
    // FUN_00410130: DAT_006164d0 = 20000 (DAT_005c8910), DAT_006164d4 = 1000 + 20000.
    G_SpaceDrawRange = 20000.0f;
    G_SpaceDrawFar = 1000.0f + 20000.0f;
}

void SpaceSetFarClip(float FarClip)
{
    // FUN_00410150: FarClip is kept when it is > 1000 (or unordered); otherwise it becomes
    // 1000.
    if (FarClip < 1000.0f)
        FarClip = 1000.0f;
    G_SpaceDrawFar = FarClip;
    G_SpaceDrawRange = FarClip - 1000.0f;
}

void* GetSpaceFieldController()
{
    // DAT_00675244.
    return G_FieldController;
}

int GetSpaceFieldEnclosure()
{
    // DAT_0067524c.
    return G_FieldEnclosure;
}

float GetSpaceFieldRadiusSq()
{
    // DAT_00675248.
    return G_FieldRadiusSq;
}

void SpaceDrawQueueRelease()
{
    // FUN_0051fbc0: Heap::Shrink on both heaps; then, per bucket vector, free() every pointer
    // and erase the whole range. A second pass erases every bucket again -- a no-op after the
    // first. Neither pass releases the vectors' storage.
    G_DrawNodeHeap.Shrink();
    G_DrawNodeHeap12.Shrink();
    for (unsigned B = 0; B < kDrawBucketCount; ++B)
    {
        FDrawBucket& Buck = G_Buckets[B];
        for (size_t I = 0; I < Buck.Nodes.size(); ++I)
            free(Buck.Nodes[I]);
        Buck.Nodes.clear();
    }
    for (unsigned B = 0; B < kDrawBucketCount; ++B)
        G_Buckets[B].Nodes.clear();
}
