// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Gf ObjectShip (E:\FL\Scratch\Source\Client\Gf\ObjectShip.cpp; RTTI .?AVShipObject@@ at
// vtable 0x005cbdf0 [-4] -> COL 0x005e8c64) -- the
// player's ship as a room object in the base view, a singleton at DAT_00668a9c (written by
// RebuildModel, cleared by the destructor). Size 0xb0: the room's player-ship pad builds it
// (FUN_004487f0, room_control.cpp), and the pad table deletes it through slot 0
// (FUN_004486d0 / FUN_004488f0).
//
// Vtable (13 slots over RoomObject 0x005e14a4): 0 FUN_00446f90 (deleting dtor), 1 / 2
// FUN_0044fd00 (false), 3 / 4 the RoomObject float, 5 LAB_004473a0, 6 LAB_00451360, 7
// LAB_00447500, 8 LAB_00447570, 9 LAB_004475b0, 10..12 the shared no-op
// (IDPMsgHandler::OnReceive).

#include "common_csys.h"
#include "Space/Effects/equip_visual_set.h"
#include "Common/Graphics/lights_camera.h"
#include "GameFlow/room_control.h"

class EquipDescList;
class HardpointSummary;
struct CacheString;
struct ICamera;
struct ISoundSource;

namespace Archetype
{
    struct Ship;
}

// The description FUN_004487f0 builds on the stack and the ctor reads (0x44 bytes).
struct FObjectShipDesc
{
    // FUN_00446bf0 -- everything zero, the orientation identity.
    FObjectShipDesc();
    // FUN_00446c50 -- malloc_strfree on +0x34.
    ~FObjectShipDesc();

    Archetype::Ship* Ship;             // +0x00
    Vector Pos;                        // +0x04
    Matrix Orient;                     // +0x10
    char* LaunchScript;                // +0x34 malloc_strcpy'd; FUN_004487f0: the pad's
                                       //       ShipPlaceInfo::LaunchingScript
    unsigned char bShadow;             // +0x38 FUN_004487f0: 1
    float FloorHeight;                 // +0x3c RoomObject slot 3; FUN_004487f0: the pad's y
    EquipDescList const* Equipment;    // +0x40 copied into the ship's own list

private:
    FObjectShipDesc(FObjectShipDesc const&) = delete;
    FObjectShipDesc& operator=(FObjectShipDesc const&) = delete;
};

// One engine flame effect (0x44 bytes, array +0xa0, one per hardpoint of the ship model).
struct FObjectShipEngineFx
{
    // LAB_00446c60 -- Particles zero, Instance -1, bStopping 0, Lifetime 0.
    FObjectShipEngineFx();
    // FUN_00446c80 -- FUN_005574a0 (Particles.Destroy), Instance -1, bStopping 0, Lifetime 0.
    ~FObjectShipEngineFx();

    // FUN_00446ca0 -- the old effect destroyed, the frame kept, and for an engine (class 0xc)
    // with a flame_effect whose EffectArch resolves: the effect created and, on success,
    // Lifetime = InLifetime. An unknown flame effect FDUMPs (ObjectShip.cpp 0x84).
    void Start(unsigned ArchId, float InLifetime, void* InInstance, Vector const& InPos,
               Matrix const& InOrient);
    // FUN_00446d90 -- with an effect: its frame placed and updated; then either the stop
    // count-down (a negative Lifetime never ends) or, once stopping, destroyed when finished.
    void Update(float Delta);

    FLightsCameraParticles Particles;  // +0x00
    void* Instance;                    // +0x08 the hardpoint's engine instance
    Vector Pos;                        // +0x0c the hardpoint's local position
    Matrix Orient;                     // +0x18 the hardpoint's local orientation
    unsigned char bStopping;           // +0x3c
    float Lifetime;                    // +0x40 seconds left, negative = endless

private:
    FObjectShipEngineFx(FObjectShipEngineFx const&) = delete;
    FObjectShipEngineFx& operator=(FObjectShipEngineFx const&) = delete;
};

// One engine sound (8 bytes, arrays +0xa4 / +0xa8).
struct FObjectShipEngineSound
{
    // LAB_00446eb0 -- Instance -1, Source 0.
    FObjectShipEngineSound();
    // FUN_00446ec0 -- the source's looping is cleared, but the reference is not released
    // (retail).
    ~FObjectShipEngineSound();

    void* Instance;                    // +0x00 the hardpoint's engine instance
    ISoundSource* Source;              // +0x04

private:
    FObjectShipEngineSound(FObjectShipEngineSound const&) = delete;
    FObjectShipEngineSound& operator=(FObjectShipEngineSound const&) = delete;
};

// FObjectShip::ScriptState.
enum EShipScriptState : int
{
    kShipScriptNone = 0,
    kShipScriptLaunching = 1, // PlayLaunchScript (FUN_00447cd0)
    kShipScriptLanding = 2,   // PlayLandingScript (FUN_00447c30)
};

class FObjectShip : public FRoomObject
{
public:
    // FUN_00446ef0 -- the members cleared (+0x70 = -1), then Load(Desc); Pos / Orient are left
    // for Load to write.
    FObjectShip(FRoomRuntime* InOwner, FObjectShipDesc const& Desc);
    // FUN_00446fb0 (FUN_00446f90 is the deleting form).
    ~FObjectShip() override;

    bool IsVisible() override;                      // 2  FUN_0044fd00
    void Update(double DeltaSeconds) override;      // 5  LAB_004473a0
    void* GetEngineInstance() override;             // 6  LAB_00451360
    void DrawOpaque(ICamera* Camera) override;      // 7  LAB_00447500
    void DrawTranslucent(ICamera* Camera) override; // 8  LAB_00447570
    void DrawEffects(ICamera* Camera) override;     // 9  LAB_004475b0

    // FUN_0043d940. Caller: the Gf spawn's PlayerShip target.
    HardpointSummary* GetHardpointSummary() const { return Visuals.Summary; }

    // FUN_00447d70 -- start the engine particle effects: on each "HpEngine0<n>" hardpoint
    // (n = 1..the ship's NumExhaustNozzles) of the model, the flame effect of the first engine
    // in the player's equipment. Caller: Thorn_PlayerEnginePsys IPSysControl slot 3
    // (LAB_00454150).
    void StartEngineEffects();
    // FUN_00447f00 -- every effect told to stop. Caller: slot 4 (LAB_00454170).
    void StopEngineEffects();
    // FUN_00447f50 -- slot 6 on every effect. Caller: slot 6 (LAB_004541a0).
    void SetEngineSparam(float Sparam);

    // FUN_00447f90 -- start the engine sounds: on the same hardpoints, the engines'
    // character_loop_sound (+0xa4) and rumble_sound (+0xa8), looped and positioned. Caller:
    // Thorn_PlayerEngineSound ISoundControl slot 3 (LAB_004579a0).
    void StartEngineSound();
    // FUN_004481b0 -- every sound's looping cleared. Callers: slot 4 (LAB_004579c0) and
    // identity slot 14 (LAB_00457980).
    void StopEngineSound();
    // FUN_00448210 -- every sound's attenuation. Callers: slots 5 / 9 (LAB_004579e0 /
    // LAB_00457a30).
    void SetEngineSoundAttenuation(float Attenuation);

    // FUN_00448540 -- replace the ship's equipment list (+0x6c) with a copy of List. Every
    // caller follows it with RebuildModel; one is NN_Inventory with the player's +0x34c list.
    void SetEquipment(EquipDescList const* List);
    // FUN_00448680.
    void SetShipArchetype(unsigned ArchId);
    // FUN_00447210 -- rebuild the ship model; DAT_00668a9c = this. Also reached through the
    // thunk 0x004482a0.
    void RebuildModel();
    // FUN_004477d0 -- mount a newly added item. The cargo bay (EquipDesc::CARGO_BAY_HP_NAME,
    // by pointer) -> nothing; a hardpoint the model lacks -> FDUMP (ObjectShip.cpp 0x275);
    // bCombined clear -> attached. Id, Count and Health are not read.
    void MountEquipment(int bCombined, unsigned ArchId, unsigned short Id,
                        CacheString const* Hardpoint, int Count, float Health);
    // FUN_00447850 -- unmount a removed item. Cargo bay -> nothing; missing hardpoint -> FDUMP
    // (ObjectShip.cpp 0x295); RemoveResult 0 (the item erased) -> detached. ArchId, Id and
    // Count (what the player-equipment callers pass) are not read.
    void UnmountEquipment(int RemoveResult, unsigned ArchId, unsigned Id,
                          CacheString const* Hardpoint, unsigned Count);

    // FUN_00447cd0 -- with a launch script (+0x64): a Gf sequence playing it, +0xac = 1, then
    // FUN_00558c00; without one FUN_00558a30.
    void PlayLaunchScript();
    // FUN_00447c30 -- cut to black and fade in, then a Gf sequence playing Script, +0xac = 2.
    // Caller: the room pad FUN_00448a20 with the pad's landing script.
    void PlayLandingScript(char const* Script);

    Archetype::Ship* Ship;              // +0x30
    Vector Pos;                         // +0x34
    Matrix Orient;                      // +0x40
    char* LaunchScript;                 // +0x64 malloc_strcpy'd
    unsigned char bShadow;              // +0x68
    EquipDescList* Equipment;           // +0x6c owned (operator new(0xc))
    void* Instance;                     // +0x70 -1 when no model
    FEquipVisualSet Visuals;            // +0x74 (summary +0x74, count +0x78)
    FObjectShipEngineFx* EngineFx;      // +0xa0 new[count]
    FObjectShipEngineSound* LoopSounds; // +0xa4 new[count] character_loop_sound
    FObjectShipEngineSound* RumbleSounds; // +0xa8 new[count] rumble_sound
    int ScriptState;                    // +0xac EShipScriptState

private:
    // FUN_00447120.
    void Load(FObjectShipDesc const& Desc);
    // FUN_00447690 -- the three per-hardpoint arrays, the sounds bound to the hardpoints'
    // instances.
    void BuildHardpointArrays();
    // LAB_00448270 (cdecl) -- the Gf sequence stop callback: +0xac 1 -> FUN_00558a30; 2 ->
    // +0xac = 0; 0 -> nothing.
    static void __cdecl OnScriptStopped(void* ShipPtr, void* Unused, unsigned char bArg);

    FObjectShip(FObjectShip const&) = delete;
    FObjectShip& operator=(FObjectShip const&) = delete;
};

// FUN_00446be0 -- DAT_00668a9c.
FObjectShip* GfObjectShip();

// The description FUN_00448700 builds on the stack for a ship dealer's for-sale ship (0x40
// bytes, ctor FUN_00443770; its destructor is the shared empty function at 0x004487cc).
struct FDealerShipDesc
{
    // FUN_00443770 -- everything zero, the orientation identity.
    FDealerShipDesc();

    Archetype::Ship* Ship;             // +0x00
    Vector Pos;                        // +0x04
    Matrix Orient;                     // +0x10
    unsigned char bShadow;             // +0x34 FUN_00448700: 1
    float Height;                      // +0x38 FUN_00448700: the pad's y
    EquipDescList const* Loadout;      // +0x3c the package's addon list (GoodInfo +0x94)

private:
    FDealerShipDesc(FDealerShipDesc const&) = delete;
    FDealerShipDesc& operator=(FDealerShipDesc const&) = delete;
};

// DealerShipObject (RTTI .?AVDealerShipObject@@, vtable 0x005cb8d4, size 0x64) -- a ship for
// sale standing on a ship dealer's pad, built by the room's pad table (FUN_00448700) for every
// ship package the base's market sells.
//
// Vtable (13 slots): 0 FUN_00443880 (deleting dtor), 1 / 2 FUN_0044fd00 (false), 3 / 4 the
// RoomObject float, 5 LAB_00443a10, 6 LAB_00443870 (ICF-shared with Gf::ObjectProp), 7
// FUN_00443a30, 8 / 9 and 10..12 the shared no-ops.
class FDealerShipObject : public FRoomObject
{
public:
    // FUN_004437d0.
    FDealerShipObject(FRoomRuntime* InOwner, FDealerShipDesc const& Desc);
    // FUN_004438a0 (FUN_00443880 the deleting form).
    ~FDealerShipObject() override;

    bool IsVisible() override;                      // 2  FUN_0044fd00
    void Update(double DeltaSeconds) override;      // 5  LAB_00443a10
    void* GetEngineInstance() override;             // 6  LAB_00443870
    void DrawOpaque(ICamera* Camera) override;      // 7  FUN_00443a30

    void* Instance;                    // +0x30 -1 when no model
    unsigned char bShadow;             // +0x34
    FEquipVisualSet Visuals;           // +0x38 (count +0x3c)

private:
    // FUN_00443920.
    void Load(FDealerShipDesc const& Desc);

    FDealerShipObject(FDealerShipObject const&) = delete;
    FDealerShipObject& operator=(FDealerShipObject const&) = delete;
};

// FUN_004482b0 (cdecl) -- the player's collision-group damage on a ship model: every part of
// the compound whose group is destroyed (health 0) is removed, and each such group's dmg_obj
// meshes are hung on their dmg_hp hardpoints ("DpConnect"). Callers: RebuildModel and
// ShipViewControl.
void GfShipApplyCollisionDamage(void* Instance);

// FUN_00448c40 (cdecl) -- place Instance so that its "HpMount" hardpoint (identity when it has
// none) lands on Frame, then Engine UpdateInstance(Instance, 0, 0). Callers: the
// FObjectShip and FDealerShipObject model loads.
void GfShipPlaceOnMount(void* Instance, Csys const* Frame);

// FUN_004478c0 (cdecl, hidden return) -- HpName's frame relative to RootInstance: the inverse
// of the hardpoint's local frame times the inverse of its instance's world transform times the
// root's world transform; {0, identity} when Summary does not know HpName. Caller: the Gf
// spawn's PlayerShip target with "HpMount".
Csys GfShipMountOffset(HardpointSummary const* Summary, char const* HpName, void* RootInstance);

// FUN_00447be0 (cdecl Out, M, V) -- Out = M^T * V through ObjectShip.cpp's lazily acquired
// 3DMathEngine DAT_00668a98 (TransposeTransform, slot +0x38) into a local, copied to *Out.
// Retail has one body for all five call sites: FUN_004478c0 (twice), FUN_00448c40, the engine
// nozzles FUN_00528d70 and Debris.cpp FUN_005301d0.
struct FVector;
struct FMatrix;
void ShipTransposeRotate(FVector& Out, FMatrix const& M, FVector const& V);
