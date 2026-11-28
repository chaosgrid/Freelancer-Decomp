// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_cliobj.h"

#include <cstddef>

// Client ICliObj companions + factory (retail exe DAT_006164a0 / FUN_0040ca50).
// Every retail Archetype::Cli* class (CliSolar, CliShip, CliAsteroid, CliDynamicAsteroid,
// CliEQEngine/Shield/ShieldGenerator/Thruster/Launcher/Gun/Light/Tractor/AttachedFX/
// InternalFX/RepairDroid/TradeLane, CliProj, CliMunition) has its own typed class in
// this header (their member functions are in client_cliobj.cpp); the type -> class table is
// documented on FCliObjFactory::Create.
// Types with no class of their own get the bare CliRoot (0x54, FUN_004068d0).
// LOD list at +0x04 (FUN_004023f0). See docs/re/common_archetype.md.
//
// All of this is freelancer.exe code in retail; Common.dll only sees ICliObjFactory
// (src/include/common_cliobj.h). Nothing here is exported from any DLL.

struct IFileSystem;
struct FEffectArch; // vis_effect_arch.h
struct EquipDescVector; // common_goods.h

namespace Archetype
{
    // MaterialLib path-cache entry (Client\cmn\MaterialLib.cpp); layout in client_cliobj.cpp.
    struct FCliMaterialEntry;

    // VC6 std::vector empty-allocator shape: 1-byte cookie + 3 pad + begin/end/cap. The
    // companions' material-path list (ICliObj +0x20) and the list Gf::ObjectProp builds from a
    // PetalDB record (FUN_00446160) both have this shape and both reach FUN_0041af40.
    template <typename T>
    struct FCliVc6Vector
    {
        unsigned char Cookie;
        unsigned char Pad01[3];
        T* Begin;
        T* End;
        T* Capacity;
    };

    static_assert(sizeof(FCliVc6Vector<void*>) == 0x10, "VC6 vector header 0x10");

    // MaterialLib path pair (stride 8). First dword is char* path into FUN_0041aa50 /
    // MaterialLib::Load — not a CreateID hash. Arg is FUN_0041aa50's third
    // argument (-1 from FUN_00446160).
    struct FCliMaterialPathPair
    {
        char const* Path;
        unsigned Arg;
    };

    static_assert(sizeof(FCliMaterialPathPair) == 8, "material path pair stride 8");

    // FUN_0040d830 insert-at-end for a material path list (stride 8).
    void PushMaterialPathPair(FCliVc6Vector<FCliMaterialPathPair>* Vec, char const* Path,
                              unsigned Arg);

    // FUN_0041aa50 (cdecl) — find-or-load the MaterialLib cache entry for Path, ++RefCount.
    // bForceApply = retail arg 2, Arg = retail arg 3 (-1 from FUN_00420dd0 / FUN_00420f30).
    FCliMaterialEntry* ResolveMaterialEntry(char const* Path, bool bForceApply, unsigned Arg);

    // FUN_0041ade0 (cdecl) — the same cache lookup for a file system the caller already
    // opened (FUN_0041c540 passes the .cmp's own): find-or-insert Path, and for a new entry
    // load its textures, materials, material animations and meshes from Filesys.
    FCliMaterialEntry* ResolveMaterialEntryFromFs(IFileSystem* Filesys, char const* Path,
                                                  bool bForceApply, unsigned Arg);

    // FUN_0041af40 (cdecl) — resolve at most Max entries of Paths through FUN_0041aa50 (with
    // bForceApply) into Out, FDUMP an error first when Paths holds more than Max. Returns the
    // number written; 0 for a null Paths.
    int ResolveMaterialEntryList(FCliVc6Vector<FCliMaterialPathPair> const* Paths,
                                 FCliMaterialEntry** Out, int Max);

    // FUN_0041b030 — DecRef a cache entry through its slot; frees libs + erases at 0.
    int ReleaseMaterialEntrySlot(FCliMaterialEntry** Slot);

    // FUN_0041b150 (cdecl) — WinMain teardown: give every cached entry's texture,
    // material, material-animation and mesh handles back to their libraries regardless of the
    // reference counts, empty the cache, return how many entries there were.
    int FlushMaterialCache();

    // The companion (ICliObj) fields and methods other client code reads by raw offset in
    // retail: +0x10 the envmap material name (CacheString), +0x20 the material path list, and
    // FUN_00406cd0, the explicit-parameter LOD recompute.
    char const* CliObjEnvmapMaterial(ICliObj* Companion);
    FCliVc6Vector<FCliMaterialPathPair> const* CliObjMaterialPaths(ICliObj* Companion);
    // A solar archetype's companion (CliSolar) +0x60: its shape_name text. Companion must be a
    // CliSolar (retail reads the offset without a type test).
    char const* CliSolarShapeName(ICliObj* Companion);
    void CliObjRecomputeLodPickExplicit(ICliObj* Companion, long EngineIndex,
                                        float const* BlendSource, bool bSolarOrShip);
    // FUN_00406c50 -- recompute the companion's LodPick from its bound archetype (kept as is
    // while that archetype is not loaded). Thiscall on the companion in retail.
    void CliObjRecomputeLodPick(ICliObj* Companion, float const* BlendSource);
    // FUN_004022b0 (cdecl) -- store the two ArchLod globals: DAT_00616470 (byte) and
    // DAT_006101d4 (float, the LOD distance scale). Caller: the level_of_detail apply
    // LAB_005b4560.
    void CliObjSetArchLodGlobals(unsigned char bMute, float Scale);
    // FUN_004022d0 (cdecl) -- *OutScale = DAT_006101d4 when OutScale is not null; returns the
    // byte DAT_00616470. Caller: the gameobj draw submit FUN_00539980.
    unsigned char CliObjGetArchLodGlobals(float* OutScale);

    // freelancer.exe FUN_004023f0 node: 0x34, next at +0x30, CRC at +0x24.
    struct FLodNode
    {
        float Ranges[8];
        float Terminator;
        unsigned ObjCrc;
        unsigned char Unk28;    // 0 at creation (FUN_00402310 / FUN_004023f0); 1 on the head and
                                // the matched node once applied (.text:00401fbe); no reader found
        unsigned char Pad29[3];
        int MinSpecLod;
        FLodNode* Next;
    };

    static_assert(sizeof(FLodNode) == 0x34, "LOD node sizeof 0x34");
    static_assert(offsetof(FLodNode, ObjCrc) == 0x24, "LOD CRC +0x24");
    static_assert(offsetof(FLodNode, MinSpecLod) == 0x2C, "MinSpecLOD +0x2C");
    static_assert(offsetof(FLodNode, Next) == 0x30, "LOD next +0x30");

    // Distance / FX record at +0x44 (stride 0x24). EffectArch id list + live instances.
    // Layout from FUN_00555810 / 00555860 / 005559c0 (not StarSys code).
    // +0x04/+0x14 are VC6 vector cookies (FUN_00406dc0 surface_hit_effects pushes into +0x04).
    struct FCliDistanceRecord
    {
        float DistanceThreshold; // +0x00
        unsigned char EffectIdCookie; // +0x04
        unsigned char EffectIdPad[3];
        unsigned* EffectIdBegin; // +0x08  CreateID keys → FUN_0040f080
        unsigned* EffectIdEnd;   // +0x0C
        unsigned* EffectIdCap;   // +0x10
        unsigned char LiveCookie; // +0x14  IDPMsgHandler this for live vector
        unsigned char LivePad[3];
        FEffectArch** LiveBegin; // +0x18  EffectArch*
        FEffectArch** LiveEnd;   // +0x1C
        FEffectArch** LiveCap;   // +0x20
    };

    static_assert(sizeof(FCliDistanceRecord) == 0x24, "distance record stride 0x24");
    static_assert(offsetof(FCliDistanceRecord, EffectIdBegin) == 0x08, "effect ids +0x08");
    static_assert(offsetof(FCliDistanceRecord, LiveBegin) == 0x18, "live effects +0x18");
    static_assert(offsetof(FCliDistanceRecord, EffectIdCookie) == 0x04, "id vector header +0x04");
    static_assert(offsetof(FCliDistanceRecord, LiveCookie) == 0x14, "live vector header +0x14");

    // Retail default case FUN_0040ca50: operator_new(0x54) + FUN_004068d0.
    // Layout +0x1C…+0x50: see the CliRoot table in docs/re/freelancer_cliobj.md.
    class FCliObj : public ICliObj
    {
    public:
        FCliObj();
        ~FCliObj();

        void AddRef() override;
        void Release() override;
        bool ReadUnknown(INI_Reader* Reader) override;
        void Lod(INI_Reader* Reader) override;
        void Redefine(ICliObj* Donor) override;
        void Bind(Root* Record) override;
        void FreeResources() override;
        void Inherit(ICliObj* Donor) override;
        unsigned long Load(int LoadMode) override;
        void* ScalarDeletingDtor(unsigned Flags) override;
        void RealizeMaterials() override;
        void ReleaseResources() override;
        void LoadHitEffects(unsigned Arg) override;
        void ReleaseHitEffects() override;

        // FUN_00407310 — public non-virtual thunk into FUN_00407100.
        bool PreloadMaterials(bool bApplyNow) { return BuildMaterialCache(bApplyNow); }

        // FUN_00407450 — not a vtable slot; distance-threshold pick into record payload.
        FEffectArch* PickDistanceRecord(float Distance);

        // FUN_00406c50 — recompute LodPick from BoundRoot; keeps stale value if !is_loaded.
        void RecomputeLodPick(float const* BlendSource);
        // FUN_00406cd0 — same with caller-supplied engine index / flag (no Root lookup).
        void RecomputeLodPickExplicit(long EngineIndex, float const* BlendSource,
                                      bool bSolarOrShip);

        // Layout public so offsetof can verify retail offsets (like Archetype::Root).
        FLodNode* LodHead;              // +0x04
        float DistanceRender;           // +0x08 default -1
        float LodPick;                  // +0x0C default -1
        CacheString EnvmapMaterial;     // +0x10
        Root* BoundRoot;                // +0x14
        int RefCount;                   // +0x18
        void* PendingLoad;             // +0x1C
        FCliVc6Vector<FCliMaterialPathPair> MaterialPaths;   // +0x20…+0x2C
        FCliVc6Vector<FCliMaterialEntry*> ResolvedEntries;   // +0x30…+0x3C
        FCliVc6Vector<FCliDistanceRecord> DistanceRecords;   // +0x40…+0x4C
        unsigned char StateFlags;       // +0x50
        unsigned char Pad51[3];

    protected:
        // FUN_004086c0 -- CliRoot::operator= (compiler-generated): +0x04..+0x1C copied raw
        // (LodHead shallow), the three vectors assigned, the +0x50 flag bits and then the
        // whole +0x50 dword. Every derived Inherit (slot 7) runs it, inlined or called.
        void AssignRootMembers(FCliObj const* Src);
        bool BuildMaterialCache(bool bApplyNow); // FUN_00407100
        void PushResolvedEntry(FCliMaterialEntry* Entry);
        void FreeVectorBuffers();
        // Not an ICliObj slot — appended after Slot13 so retail 0–13 stay aligned.
        virtual void DestroyMostDerived();
    };

    static_assert(sizeof(FCliObj) == 0x54, "ICliObj base sizeof 0x54");
    static_assert(offsetof(FCliObj, PendingLoad) == 0x1C, "ICliObj+0x1C PendingLoad");
    static_assert(offsetof(FCliObj, MaterialPaths) == 0x20, "ICliObj+0x20 MaterialPaths");
    static_assert(offsetof(FCliObj, ResolvedEntries) == 0x30, "ICliObj+0x30 ResolvedEntries");
    static_assert(offsetof(FCliObj, DistanceRecords) == 0x40, "ICliObj+0x40 DistanceRecords");
    static_assert(offsetof(FCliObj, StateFlags) == 0x50, "ICliObj+0x50 StateFlags");

    // Retail CliShip — operator_new(0x88), cockpit CacheString at +0x60.
    class FCliShip : public FCliObj
    {
    public:
        FCliShip();

        bool ReadUnknown(INI_Reader* Reader) override;
        void Redefine(ICliObj* Donor) override;
        void Inherit(ICliObj* Donor) override;

    protected:
        void DestroyMostDerived() override;

    public:
        // Layout public for offsetof static_asserts (same pattern as FCliObj).
        unsigned Pad54;              // +0x54
        ID_String BayDoorsOpenSnd;   // +0x58 bay_doors_open_snd
        ID_String BayDoorsCloseSnd;  // +0x5C bay_doors_close_snd
        CacheString CockpitIni;      // +0x60 shiparch cockpit = …
        ID_String PilotMesh;         // +0x64 pilot_mesh
        float CameraOffsetX;         // +0x68
        float CameraOffsetY;         // +0x6C
        float CamAngularAccel;       // +0x70
        float CamAngularSlerp;       // +0x74
        float CamHorizontalTurnAngle;   // +0x78 camera_horizontal_turn_angle (radians)
        float CamVerticalTurnUpAngle;   // +0x7C camera_vertical_turn_up_angle
        float CamVerticalTurnDownAngle; // +0x80 camera_vertical_turn_down_angle
        float CamTurnLookAhead;      // +0x84
    };

    static_assert(sizeof(FCliShip) == 0x88, "CliShip sizeof 0x88");
    static_assert(offsetof(FCliShip, CockpitIni) == 0x60, "CliShip cockpit +0x60");
    static_assert(offsetof(FCliShip, CamAngularSlerp) == 0x74, "CliShip cam slerp +0x74");

    // Retail case 2: operator_new(0x88), FUN_004068d0, PTR_FUN_005c7908.
    // Two ID_String clears at +0x54/+0x58 (not init_docking_points).
    // Inline texture at +0x5C length / +0x60 32-byte "DEFAULT_TEXTURE".
    class FCliSolar : public FCliObj
    {
    public:
        FCliSolar();

        bool ReadUnknown(INI_Reader* Reader) override;
        void Redefine(ICliObj* Donor) override;   // FUN_0040a0e0
        void Inherit(ICliObj* Donor) override;

        // +0x54 open_sound / +0x58 close_sound, read raw by the solar notify FUN_005478f0.
        ID_String const* DoorSound(bool bOpen) const
        {
            return bOpen ? &OpenSound : &CloseSound;
        }

        // +0x60 shape_name (default "DEFAULT_TEXTURE"), read raw by the nav map.
        char const* ShapeName() const
        {
            return ShapeNameText;
        }

    protected:
        void DestroyMostDerived() override;

    private:
        ID_String OpenSound;        // +0x54 open_sound
        ID_String CloseSound;       // +0x58 close_sound
        unsigned ShapeNameLen;      // +0x5C
        char ShapeNameText[32];     // +0x60 shape_name
        unsigned IdsInfo;           // +0x80 infocard_ids / ids_info
        unsigned char bAnimatedTextures; // +0x84 animated_textures
        unsigned char Pad85[3];
    };

    static_assert(sizeof(FCliSolar) == 0x88, "CliSolar sizeof 0x88");

    // Retail Archetype::CliProj (RTTI .?AUCliProj@Archetype@@, vtable 005c79a8), sizeof 0x60.
    // Factory types 0x20 (Mine) / 0x21 (CounterMeasure) at 0040cc44 / 0040cc92, and the
    // base of CliMunition. Slots: ReadUnknown 00407e60, Redefine 00407f00, Inherit 00407fc0.
    class FCliProj : public FCliObj
    {
    public:
        FCliProj();

        bool ReadUnknown(INI_Reader* Reader) override;
        void Redefine(ICliObj* Donor) override;
        void Inherit(ICliObj* Donor) override;

        float ConstEffectDelay;     // +0x54 (ctor stores dword 0)
        ID_String ConstEffect;      // +0x58
        ID_String OneShotSound;     // +0x5C
    };

    static_assert(sizeof(FCliProj) == 0x60, "CliProj sizeof 0x60");
    static_assert(offsetof(FCliProj, ConstEffectDelay) == 0x54, "CliProj +0x54");
    static_assert(offsetof(FCliProj, OneShotSound) == 0x5C, "CliProj +0x5c");

    // Retail Archetype::CliEQAttachedFX (vtable 005c7fe0), sizeof 0x5C, factory type 0x17 at
    // 0040cfda. Slots: ReadUnknown 0040b6d0, Redefine 0040b740, Inherit 0040b7f0.
    class FCliEQAttachedFX : public FCliObj
    {
    public:
        FCliEQAttachedFX();

        bool ReadUnknown(INI_Reader* Reader) override;
        void Redefine(ICliObj* Donor) override;
        void Inherit(ICliObj* Donor) override;

        ID_String Particles;         // +0x54 "particles"
        bool bUseThrottle;           // +0x58 "use_throttle"
        unsigned char Pad59[3];
    };

    static_assert(sizeof(FCliEQAttachedFX) == 0x5C, "CliEQAttachedFX sizeof 0x5c");
    static_assert(offsetof(FCliEQAttachedFX, bUseThrottle) == 0x58, "CliEQAttachedFX +0x58");

    // Retail Archetype::CliAsteroid (vtable 005c7a3c), sizeof 0x54, factory type 4 at 0040ccc2.
    // ReadUnknown 00408ac0 only forwards to FUN_00406dc0 (the base parser, so no override);
    // Inherit 00408ae0 is CliRoot::operator= without the base Inherit's FUN_00402500 step.
    class FCliAsteroid : public FCliObj
    {
    public:
        void Inherit(ICliObj* Donor) override;
    };

    static_assert(sizeof(FCliAsteroid) == 0x54, "CliAsteroid sizeof 0x54");

    // Retail Archetype::CliDynamicAsteroid (vtable 005c7a78), sizeof 0x5C, factory type 5 at
    // 0040ccf8. Slots: ReadUnknown 00408cd0, Inherit 00408d20 (Redefine is the base 00407050).
    class FCliDynamicAsteroid : public FCliObj
    {
    public:
        FCliDynamicAsteroid();

        bool ReadUnknown(INI_Reader* Reader) override;
        void Inherit(ICliObj* Donor) override;

        ID_String ParticleEffect;    // +0x54 "particle_effect"
        bool bHasParticleEffect;     // +0x58 set with it
        unsigned char Pad59[3];
    };

    static_assert(sizeof(FCliDynamicAsteroid) == 0x5C, "CliDynamicAsteroid sizeof 0x5c");
    static_assert(offsetof(FCliDynamicAsteroid, bHasParticleEffect) == 0x58, "+0x58");

    // Retail Archetype::CliEQTractor (vtable 005c7f78), sizeof 0x60, factory type 0x16 at
    // 0040cf7c. Slots: ReadUnknown 0040b320, Redefine 0040b3f0, Inherit 0040b4c0.
    class FCliTractor : public FCliObj
    {
    public:
        FCliTractor();

        bool ReadUnknown(INI_Reader* Reader) override;
        void Redefine(ICliObj* Donor) override;
        void Inherit(ICliObj* Donor) override;

        unsigned char Color[3];      // +0x54 "color" (bytes via ftol), ctor 0xff each
        unsigned char Pad57;         // +0x57 never written by retail
        ID_String OperatingEffect;   // +0x58 "operating_effect"
        ID_String CompleteSound;     // +0x5c "tractor_complete_snd"
    };

    static_assert(sizeof(FCliTractor) == 0x60, "CliEQTractor sizeof 0x60");
    static_assert(offsetof(FCliTractor, OperatingEffect) == 0x58, "CliEQTractor +0x58");

    // Retail Archetype::CliEQThruster (vtable 005c7d98), sizeof 0x5C, factory type 0xF at
    // 0040cf27. Slots: ReadUnknown 0040a060, Redefine 0040a0e0 (shared with CliSolar),
    // Inherit 0040a190.
    class FCliEQThruster : public FCliObj
    {
    public:
        FCliEQThruster();

        bool ReadUnknown(INI_Reader* Reader) override;
        void Redefine(ICliObj* Donor) override;
        void Inherit(ICliObj* Donor) override;

        ID_String Particles;        // +0x54 "particles"
        CacheString HpParticles;    // +0x58 "hp_particles"
    };

    static_assert(sizeof(FCliEQThruster) == 0x5C, "CliEQThruster sizeof 0x5c");
    static_assert(offsetof(FCliEQThruster, HpParticles) == 0x58, "CliEQThruster +0x58");

    // Retail Archetype::CliEQInternalFX (vtable 005c802c), sizeof 0x64, factory type 0x18 at
    // 0040d029. Slots: ReadUnknown 0040b9f0, Redefine 0040bab0, Inherit 0040bb90.
    class FCliInternalFx : public FCliObj
    {
    public:
        FCliInternalFx();

        bool ReadUnknown(INI_Reader* Reader) override;
        void Redefine(ICliObj* Donor) override;
        void Inherit(ICliObj* Donor) override;

        ID_String UseSound;              // +0x54 "use_sound"
        float InsideSoundCone;           // +0x58 "inside_sound_cone", ctor 360
        float OutsideSoundCone;          // +0x5c "outside_sound_cone", ctor 360
        float OutsideConeAttenuation;    // +0x60 "outside_cone_attenuation", ctor 0
    };

    static_assert(sizeof(FCliInternalFx) == 0x64, "CliEQInternalFX sizeof 0x64");
    static_assert(offsetof(FCliInternalFx, OutsideConeAttenuation) == 0x60, "+0x60");

    // Retail Archetype::CliEQLight (vtable 005c7eb8), sizeof 0x98, factory type 0x15 at
    // 0040cef8 (ctor FUN_0040a9e0). Slots: ReadUnknown 0040ab30, Redefine 0040ae70, Inherit
    // 0040b0a0. flash_light.cpp reads the fields by these offsets.
    class FCliEQLight : public FCliObj
    {
    public:
        FCliEQLight();

        bool ReadUnknown(INI_Reader* Reader) override;
        void Redefine(ICliObj* Donor) override;
        void Inherit(ICliObj* Donor) override;

        float Color[3];          // +0x54 "color" / 255
        float GlowColor[3];      // +0x60 "glow_color" / 255 (seeded by color while all zero)
        float MinColor[3];       // +0x6c "min_color" / 255
        float BulbSize;          // +0x78 "bulb_size", ctor 1
        float GlowSize;          // +0x7c "glow_size", ctor 1
        float AvgDelay;          // +0x80 "avg_delay", ctor 0
        float BlinkDuration;     // +0x84 "blink_duration", ctor 0.3
        float Intensity;         // +0x88 "intensity", ctor 0
        float FlareConeCos[2];   // +0x8c cos(value 1), +0x90 cos(value 0); ctor -1, -1
        float LightsourceCone;   // +0x94 "lightsource_cone", ctor 180
    };

    static_assert(sizeof(FCliEQLight) == 0x98, "CliEQLight sizeof 0x98");
    static_assert(offsetof(FCliEQLight, MinColor) == 0x6c, "CliEQLight +0x6c");
    static_assert(offsetof(FCliEQLight, BulbSize) == 0x78, "CliEQLight +0x78");
    static_assert(offsetof(FCliEQLight, FlareConeCos) == 0x8c, "CliEQLight +0x8c");
    static_assert(offsetof(FCliEQLight, LightsourceCone) == 0x94, "CliEQLight +0x94");

    // Retail Archetype::CliMunition : CliProj (vtable 005c8420), sizeof 0x84, factory type
    // 0x1F at 0040cbbd. Slots: ReadUnknown 004083a0, Redefine 004084b0, Inherit 00408520,
    // Load 004081c0, scalar dtor 0040d1c0 (body 0040d1e0), Slot11 004089a0.
    // Hit-effect CreateID list at +0x64; live EffectArch* at +0x74.
    class FCliMunition : public FCliProj
    {
    public:
        FCliMunition();
        ~FCliMunition();

        bool ReadUnknown(INI_Reader* Reader) override;
        void Redefine(ICliObj* Donor) override;
        void Inherit(ICliObj* Donor) override;
        unsigned long Load(int LoadMode) override;
        void ReleaseResources() override;

        // FUN_00408a70 — random pick from LiveEffects.
        FEffectArch* PickLiveEffect();

    protected:
        void DestroyMostDerived() override;

        void ReleaseLiveEffects();
        void RebuildLiveEffects(unsigned Arg);

    public:
        CacheString HpTrailParent;                 // +0x60
        FCliVc6Vector<unsigned> HitEffectIds;      // +0x64…+0x70
        FCliVc6Vector<FEffectArch*> LiveEffects;   // +0x74…+0x80
    };

    static_assert(sizeof(FCliMunition) == 0x84, "CliMunition sizeof 0x84");
    static_assert(offsetof(FCliMunition, HitEffectIds) == 0x64, "hit ids +0x64");
    static_assert(offsetof(FCliMunition, LiveEffects) == 0x74, "live effects +0x74");

    // Retail Archetype::CliEQShield (vtable 005c83e4), sizeof 0x54, factory type 0xD at
    // 0040cd7c (FUN_004068d0, then the vptr). Every slot is CliRoot's except slot 10
    // (005c840c), which is 00401e90 -- a bare RET: a shield companion never realizes its
    // resolved materials nor unloads engine components the way FUN_00407320 does.
    class FCliEQShield : public FCliObj
    {
    public:
        void RealizeMaterials() override;
    };

    static_assert(sizeof(FCliEQShield) == 0x54, "CliEQShield sizeof 0x54");

    // Retail Archetype::CliEQRepairDroid (vtable 005c83a8), sizeof 0x58, factory type 0x19 at
    // 0040d0ab. Slots: ReadUnknown 0040c810, Redefine 00409bd0 (shared with
    // CliEQShieldGenerator), Inherit 0040c860.
    class FCliRepairDroid : public FCliObj
    {
    public:
        FCliRepairDroid();

        bool ReadUnknown(INI_Reader* Reader) override;
        void Redefine(ICliObj* Donor) override;
        void Inherit(ICliObj* Donor) override;

        ID_String ActiveEffect;      // +0x54 "active_effect"
    };

    static_assert(sizeof(FCliRepairDroid) == 0x58, "CliEQRepairDroid sizeof 0x58");

    // Retail Archetype::CliEQShieldGenerator (vtable 005c7cfc), sizeof 0x70, factory type 0xE
    // at 0040cdba. Slots: ReadUnknown 004098b0, Redefine 00409bd0, Inherit 00409c80, scalar
    // dtor 00409780 (body 004097a0), Slot12 00409fb0, Slot13 0040a010. The +0x60 vector holds
    // the same 0x24-byte records as CliRoot's surface_hit_effects list at +0x40.
    class FCliShieldGenerator : public FCliObj
    {
    public:
        FCliShieldGenerator();
        ~FCliShieldGenerator();

        bool ReadUnknown(INI_Reader* Reader) override;
        void Redefine(ICliObj* Donor) override;
        void Inherit(ICliObj* Donor) override;
        void LoadHitEffects(unsigned Arg) override;
        void ReleaseHitEffects() override;

        // FUN_00409f10 -- not a vtable slot; the shield-hit twin of PickDistanceRecord over
        // the +0x60 list. Retail callers 0x0052e81a / 0x0052e96a.
        FEffectArch* PickShieldHitEffect(float Distance);

    protected:
        void DestroyMostDerived() override;

    public:
        ID_String CollapseParticle;                       // +0x54 "shield_collapse_particle"
        ID_String CollapseSound;                           // +0x58 "shield_collapse_sound"
        ID_String RebuiltSound;                            // +0x5c "shield_rebuilt_sound"
        FCliVc6Vector<FCliDistanceRecord> ShieldHitEffects; // +0x60 "shield_hit_effects"
    };

    static_assert(sizeof(FCliShieldGenerator) == 0x70, "CliEQShieldGenerator sizeof 0x70");
    static_assert(offsetof(FCliShieldGenerator, ShieldHitEffects) == 0x60, "+0x60 hit effects");

    // Retail Archetype::CliEQLauncher (vtable 005c7df0), sizeof 0x84, factory types 0x12 / 0x13
    // (MineDropper / CounterMeasureDropper) at 0040ce88. Slots: ReadUnknown 0040a4d0, Redefine
    // 0040a5e0, Inherit 0040a720, Load 0040a3b0.
    class FCliLauncher : public FCliObj
    {
    public:
        FCliLauncher();

        bool ReadUnknown(INI_Reader* Reader) override;
        void Redefine(ICliObj* Donor) override;
        void Inherit(ICliObj* Donor) override;
        unsigned long Load(int LoadMode) override;

        ID_String FlashParticle;     // +0x54 "flash_particle_name"
        float FlashRadius;           // +0x58 "flash_radius", ctor 4.0
        unsigned LightAnimLength;    // +0x5c
        char LightAnim[0x20];        // +0x60 "light_anim", at most 0x1f chars + NUL
        ID_String DryFireSound;      // +0x80 "dry_fire_sound"

    protected:
        // The fixed-string assign retail inlines at 0040a524, 0040a6b7 and 0040a7e7: a null
        // source empties it; otherwise strlen clamped to 0x1f, copied, NUL-terminated, and the
        // length stored at +0x5c.
        void AssignLightAnim(char const* Text);
    };

    static_assert(sizeof(FCliLauncher) == 0x84, "CliEQLauncher sizeof 0x84");
    static_assert(offsetof(FCliLauncher, LightAnim) == 0x60, "CliEQLauncher +0x60");
    static_assert(offsetof(FCliLauncher, DryFireSound) == 0x80, "CliEQLauncher +0x80");

    // Retail Archetype::CliEQGun : CliEQLauncher (vtable 005c7e6c), sizeof 0x88, factory type
    // 0x11 at 0040ce1a. Slots: ReadUnknown 0040a850 (chains to 0040a4d0 first), Redefine
    // 0040a5e0 and Load 0040a3b0 inherited, Inherit 0040a8a0.
    class FCliGun : public FCliLauncher
    {
    public:
        FCliGun();

        bool ReadUnknown(INI_Reader* Reader) override;
        void Inherit(ICliObj* Donor) override;

        ID_String TurretSound;       // +0x84 "turret_sound"
    };

    static_assert(sizeof(FCliGun) == 0x88, "CliEQGun sizeof 0x88");
    static_assert(offsetof(FCliGun, TurretSound) == 0x84, "CliEQGun +0x84");

    // Retail Archetype::CliEQEngine (vtable 005c7ac4), sizeof 0xAC, factory type 0xC at
    // 0040cd4d (ctor FUN_00408f20). Slots: ReadUnknown 00408ff0, Redefine 004093e0, Inherit
    // 004094e0.
    class FCliEngine : public FCliObj
    {
    public:
        FCliEngine();

        bool ReadUnknown(INI_Reader* Reader) override;
        void Redefine(ICliObj* Donor) override;
        void Inherit(ICliObj* Donor) override;

        unsigned InsideSoundCone;         // +0x54 get_value_int, ctor 0
        unsigned OutsideSoundCone;        // +0x58 get_value_int, ctor 0x168
        float OutsideConeAttenuation;     // +0x5c
        ID_String RumbleSound;            // +0x60
        int RumblePitchRange[2];          // +0x64
        int RumbleAttenRange[2];          // +0x6c
        ID_String CharacterStartSound;    // +0x74
        ID_String CharacterLoopSound;     // +0x78
        int CharacterPitchRange[2];       // +0x7c
        ID_String EngineKillSound;        // +0x84
        ID_String CruiseStartSound;       // +0x88
        ID_String CruiseLoopSound;        // +0x8c
        ID_String CruiseStopSound;        // +0x90
        ID_String CruiseDisruptSound;     // +0x94
        ID_String CruiseBackfireSound;    // +0x98
        ID_String FlameEffect;            // +0x9c
        ID_String TrailEffect;            // +0xa0
        ID_String TrailEffectPlayer;      // +0xa4
        ID_String CruiseDisruptEffect;    // +0xa8
    };

    static_assert(sizeof(FCliEngine) == 0xAC, "CliEQEngine sizeof 0xac");
    static_assert(offsetof(FCliEngine, RumbleSound) == 0x60, "CliEQEngine +0x60");
    static_assert(offsetof(FCliEngine, CharacterPitchRange) == 0x7c, "CliEQEngine +0x7c");
    static_assert(offsetof(FCliEngine, EngineKillSound) == 0x84, "CliEQEngine +0x84");
    static_assert(offsetof(FCliEngine, CruiseDisruptEffect) == 0xa8, "CliEQEngine +0xa8");

    // Retail Archetype::CliEQTradeLane (vtable 005c8074), sizeof 0xC8, factory type 0x1D at
    // 0040d083 (ctor FUN_0040bd90). Slots: ReadUnknown 0040be90, Redefine 0040c290, Inherit
    // 0040c4d0. Non-virtual: FUN_0040c690 (effect AddRef, caller 0x00516cb0) and FUN_0040c7d0
    // (effect Release, caller 0x00516be4).
    class FCliTradeLane : public FCliObj
    {
    public:
        FCliTradeLane();

        bool ReadUnknown(INI_Reader* Reader) override;
        void Redefine(ICliObj* Donor) override;
        void Inherit(ICliObj* Donor) override;

        void AddEffectRef();          // FUN_0040c690
        int ReleaseEffectRef();       // FUN_0040c7d0

        ID_String PlayerEnter;        // +0x54 "tl_player_enter"
        ID_String PlayerTravel;       // +0x58 "tl_player_travel"
        ID_String PlayerSplash;       // +0x5c "tl_player_splash"
        ID_String PlayerExit;         // +0x60 "tl_player_exit"
        ID_String PlayerDisrupt;      // +0x64 "tl_player_disrupt"
        ID_String ShipEnter;          // +0x68 "tl_ship_enter"
        ID_String ShipTravel;         // +0x6c "tl_ship_travel"
        ID_String ShipSplash;         // +0x70 "tl_ship_splash"
        ID_String ShipExit;           // +0x74 "tl_ship_exit"
        ID_String ShipDisrupt;        // +0x78 "tl_ship_disrupt"
        ID_String RingActive;         // +0x7c "tl_ring_active"
        ID_String RingShield;         // +0x80 "tl_ring_shield"
        ID_String RingTube;           // +0x84 "tl_ring_tube"
        ID_String RingActiveStart;    // +0x88 "tl_ring_active_start"
        ID_String RingActiveLoop;     // +0x8c "tl_ring_active_loop"
        ID_String RingActiveEnd;      // +0x90 "tl_ring_active_end"
        float SecsBeforeEnter;        // +0x94 ctor 1
        float SecsBeforeExit;         // +0x98 ctor 1
        float SecsBeforeSplash;       // +0x9c ctor 1
        float ActivationStart;        // +0xa0 ctor 600
        float ActivationEnd;          // +0xa4 ctor 300
        float SpinAccel;              // +0xa8 ctor 25
        float SpinMax;                // +0xac ctor 50
        int EffectRefCount;           // +0xb0 ctor 0
        FEffectArch* Effects[5];      // +0xb4 player effect, else the ship one (0040c690)
    };

    static_assert(sizeof(FCliTradeLane) == 0xC8, "CliEQTradeLane sizeof 0xc8");
    static_assert(offsetof(FCliTradeLane, PlayerEnter) == 0x54, "CliEQTradeLane +0x54");
    static_assert(offsetof(FCliTradeLane, RingActiveEnd) == 0x90, "CliEQTradeLane +0x90");
    static_assert(offsetof(FCliTradeLane, SpinMax) == 0xac, "CliEQTradeLane +0xac");
    static_assert(offsetof(FCliTradeLane, Effects) == 0xb4, "CliEQTradeLane +0xb4");

    class FCliObjFactory final : public ICliObjFactory
    {
    public:
        void Create(int TypeId, ICliObj** Out) override;
    };

    // Retail DAT_006164a0 — the factory WinMain hands to DataConfig::startup.
    extern FCliObjFactory GCliObjFactory;

    // FUN_00407310 — non-virtual thunk into FUN_00407100 (build the companion's material
    // cache; retail's zero applies now, nonzero defers application). Caller: FUN_00553100.
    bool CliObjPreloadMaterials(ICliObj* Companion, bool bDeferApply);

    // FUN_0040c690 -- a TradeLane companion's (CliEQTradeLane) effect reference: the first one
    // resolves its five player-or-ship EffectArch pointers and AddRefs them. Caller: the
    // TradeLane equipment FX constructor FUN_00516c30.
    void CliTradeLaneAddEffectRef(ICliObj* Companion);
    // FUN_0040c7d0 -- the matching release; the last one releases and clears the five.
    // Returns the remaining count. Caller: the TradeLane equipment FX dtor FUN_00516bc0.
    int CliTradeLaneReleaseEffectRef(ICliObj* Companion);

    // FUN_00407450 (thiscall on the companion, RET 4) -- CliRoot's distance-record pick: a
    // random live effect of the record the distance falls into, or null. Callers: GameObject
    // slots 104 and 105.
    FEffectArch* CliObjPickDistanceRecord(ICliObj* Companion, float Distance);
    // FUN_00409f10 (thiscall on the companion, RET 4) -- the same pick over a
    // CliEQShieldGenerator's shield_hit_effects.
    FEffectArch* CliShieldGeneratorPickHitEffect(ICliObj* Companion, float Distance);
    // FUN_00408a70 (thiscall on the companion) -- a random entry of a CliMunition's live
    // effects, or null.
    FEffectArch* CliMunitionPickLiveEffect(ICliObj* Companion);
    // A CliShip companion's bay door sound ids: +0x58 bay_doors_open_snd when bOpen, else +0x5c
    // bay_doors_close_snd. Reader: the ship notify FUN_00545670 case 2.
    ID_String const* CliShipBayDoorSound(ICliObj* Companion, bool bOpen);
    // A CliSolar companion's +0x54 open_sound when bOpen, else +0x58 close_sound. Reader: the
    // solar notify FUN_005478f0 codes 0 and 1.
    ID_String const* CliSolarDoorSound(ICliObj* Companion, bool bOpen);
    // A CliEQEngine companion's +0x9c flame_effect, +0x60 rumble_sound and +0x78
    // character_loop_sound. Companion must be a CliEQEngine (retail reads the offsets after an
    // Engine class test on the archetype). Readers: Gf ObjectShip FUN_00446ca0 and FUN_00447f90.
    ID_String const* CliEngineFlameEffect(ICliObj* Companion);
    ID_String const* CliEngineRumbleSound(ICliObj* Companion);
    ID_String const* CliEngineCharacterLoopSound(ICliObj* Companion);

    // A CliProj companion's (projectiles: CliMunition and the rest) +0x58 const_effect and
    // +0x54 const_effect_delay. Readers: Projectile slot 92 FUN_005426e0 and slot 93
    // FUN_00542790.
    ID_String const* CliProjConstEffect(ICliObj* Companion);
    float CliProjConstEffectDelay(ICliObj* Companion);
    // A CliMunition companion's +0x60 HP_trail_parent (CacheString text; "" when unset, never
    // null after the ctor's clear). Reader: Guided slot 112 LAB_0053f030.
    char const* CliMunitionHpTrailParent(ICliObj* Companion);
    // A CliDynamicAsteroid companion's +0x58 flag and +0x54 particle_effect. Reader: the
    // dynamic asteroid update FUN_00525c80, no class test.
    bool CliDynamicAsteroidHasParticleEffect(ICliObj* Companion);
    ID_String const* CliDynamicAsteroidParticleEffect(ICliObj* Companion);

    // FUN_004252d0 (cdecl) -- Load(LoadMode) the companion (Root +0x04, vt+0x20) of every
    // mounted, non-internal equipment in the vector whose archetype resolves. Callers: the
    // player ship build FUN_0054b520 (LoadMode 1) and three gameobj sites.
    void LoadMountedEquipmentCompanions(EquipDescVector const* Items, int LoadMode);
    // FUN_00425480 (cdecl) -- hook up the materials (IMatHookUp slot 3) of an engine instance
    // and of every instance IEngine::GetRelatedInstance(Instance, 1, ...) walks to. Callers:
    // FUN_0054b520 and eight Gameobj/equipment paths.
    void HookUpInstanceTreeMaterials(long EngineIndex);
}
