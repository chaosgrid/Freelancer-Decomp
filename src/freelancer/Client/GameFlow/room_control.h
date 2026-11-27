// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// RoomControl runtime — retail FUN_00449260 object, sizeof 0x13ac.
// Subobjects: +0x4C FUN_00555a40 (0x18), +0x504 FUN_0059ec40 (0x10),
// +0x1020 FUN_004488d0 (0x18). Camera* at +0x500 = FUN_00556be0.
//
// Base +0x00..+0x2C = FUN_005593d0 RoomObject, registered in DAT_00679a10.

#include "engine/engine_instance.h" // InstanceIndex / ArchetypeIndex
#include "Core/event_handler.h"
#include "GameFlow/anim_script_set.h"
#include "NeuroNet/Economy/client_base_market.h" // FClientMarket
#include "common_goods.h"           // ShipPlaceInfo
#include "math3d.h"                 // Quaternion
#include <list>
#include <stdint.h>
#include <vector>

struct ICamera;
class RoomData;
class BaseData;
struct IRenderPipeline;

struct FRoomRuntime;
struct FGfLightWrapper;
struct FFlashLight;
class RoomButton;
class FObjectShip;
class FDealerShipObject;

namespace Archetype
{
    struct FCliMaterialEntry;
}

// The engine-instance handle a RoomObject answers with when it has none (FUN_00450870 -1).
static const InstanceIndex kRoomObjectNoInstance = InvalidInstanceIndex;

// FRoomObject::RenderMode (FUN_0044af60): how the object is drawn.
enum ERoomRenderMode
{
    kRoomRenderMesh = 0,        // renderer slot 7 (with the "envmapbasic" override when bEnvMap)
    kRoomRenderStarsphere = 1,  // appearance slot 10 (DrawStarsphere)
    kRoomRenderBackdrop = 2     // appearance slot 9 (DrawBackdrop)
};

// RoomObject (RTTI .?AVRoomObject@@) -- the base of every object a room draws. The ctor
// FUN_005593d0(Owner) appends it to DAT_00679a10 and the dtor FUN_005594b0 releases its
// material entries and takes it out again. FUN_0044b570 walks that list three times, once per
// draw slot, for the objects whose Owner is the room being drawn, layer by layer; FUN_00558ac0
// runs slot 5 on every object whose +0x04 byte is clear.
// Vtable 0x005e14a4 (13 slots). Derived: RoomControl (0x005cbea0), Gf::ObjectProp
// (0x005cbc0c), Character (0x005cb930), ObjectShip (0x005cbdf0), 0x005cb8d4.
struct FRoomObject
{
    explicit FRoomObject(FRoomRuntime* InOwner);           // FUN_005593d0
    virtual ~FRoomObject();                                // 0  FUN_00559490 / FUN_005594b0
    virtual bool Slot1();                                  // 1  +0x04 FUN_0044fd00: false
    virtual bool IsVisible();                              // 2  +0x08 LAB_00444050: +0x10
    virtual void SetFloorHeight(float Value);              // 3  +0x0c LAB_00443850
    virtual float GetFloorHeight();                        // 4  +0x10 LAB_00443860
    virtual void Update(double DeltaSeconds) = 0;          // 5  +0x14 _purecall
    virtual InstanceIndex GetEngineInstance();                     // 6  +0x18 FUN_00450870: -1
    virtual void DrawOpaque(ICamera* Camera);              // 7  +0x1c no-op
    virtual void DrawTranslucent(ICamera* Camera);         // 8  +0x20 no-op
    virtual void DrawEffects(ICamera* Camera);             // 9  +0x24 no-op
    // 10..12: the hotspot events FUN_00555f00 sends to a hotspot's owner (gf_hotspot.h) with
    // the hotspot's cookie (+0x08) and the cursor point: 1 enter, 2 leave, 3 click. Only
    // Character (0x005cb930: 0x00445a90 / 0x00445be0 / 0x00445c10) overrides them.
    virtual void OnHotspotEnter(unsigned long Cookie, int const* Point); // 10 +0x28 no-op
    virtual void OnHotspotLeave(unsigned long Cookie, int const* Point); // 11 +0x2c no-op
    virtual void OnHotspotClick(unsigned long Cookie, int const* Point); // 12 +0x30 no-op

    // FUN_00559590 -- AmbientSource's +0x6c (three ints, 0..255 each), or null.
    int const* GetAmbientOverride() const;

    unsigned char bNoUpdate;      // +0x04 FUN_00558ac0 skips slot 5 while it is set
    FRoomRuntime* Owner;          // +0x08 the room whose FUN_0044b570 draws this object
    void* AmbientSource;          // +0x0c FUN_00559590 hands back its +0x6c (three ints)
    unsigned char bVisible;       // +0x10 1 -- slot 2; clear means black ambient
    unsigned char bLit;           // +0x11 1 -- FUN_0044ae80 picks lights only when set
    unsigned char bFog;           // +0x12 1 -- D3DRS_FOGENABLE when room fog is active
    unsigned char bEnvMap;        // +0x13 0 -- FUN_0044af60 render mode 0 "envmapbasic"
    unsigned char bRunningLights; // +0x14 0 -- flashlights updated and drawn (RoomPropInfo)
    unsigned char LightGroup;     // +0x15 0 -- FUN_00557170 group filter
    unsigned char Layer;          // +0x16 0 -- the FUN_0044b570 sort key
    int RenderMode;               // +0x18 0 -- FUN_0044af60: 0 renderer, 1 / 2 appearance
    float FloorHeight;            // +0x1c 0 -- slots 3 / 4; the shadow plane (FUN_0044b370)
    // +0x20..+0x2f: a std::vector of MaterialLib entries (retail's allocator byte at +0x20
    // takes the low byte of the ctor argument); FUN_005594b0 releases every entry through
    // FUN_0041b030. Filled by FUN_0041c540 when the object loads its mesh.
    std::vector<Archetype::FCliMaterialEntry*> MaterialEntries;
};

// FUN_005593c0 -> DAT_00679a10: the list every RoomObject is appended to.
std::list<FRoomObject*>& GetRoomObjectList();

// operator new(0x13ac), the ctor FUN_00449260 (which appends the runtime to DAT_00679a10 through
// FUN_005593d0), then the load FUN_004496b0.
FRoomRuntime* CreateRoomRuntime(RoomData* Room);

// FUN_00449400, the deleting destructor (FUN_00449420; the RoomObject part FUN_005594b0 unlinks
// it from DAT_00679a10).
void DestroyRoomRuntime(FRoomRuntime* Runtime);

// Runtime+0x500 — null until CreateRoomRuntime applied a camera.
ICamera* GetRoomRuntimeCamera(FRoomRuntime* Runtime);

// The same +0x500 as the concrete room camera (room_camera.h) -- what Script+0x30->+0x500
// hands FUN_00450830 when a Thorn camera takes a pool slot.
class FRoomCamera;
FRoomCamera* GetRoomRuntimeRoomCamera(FRoomRuntime* Runtime);

unsigned GetRoomRuntimeByteSize();

// Runtime+0x34 -- the base FUN_004496b0 loaded the room for (Gf::ObjectProp's "$..." template
// indirections, FUN_00446280, read it through the prop's owner).
unsigned GetRoomRuntimeBaseId(FRoomRuntime* Runtime);

// Runtime+0x40 -- the room's RoomData. The Gf script spawn reads it directly for the
// find_camera / find_setpoint lookups.
RoomData* GetRoomRuntimeRoomData(FRoomRuntime* Runtime);

// Runtime+0x38 -- the room's id. CharacterObject reads it through its owner and FUN_00435dd0
// through its room argument for the room state's buckets.
unsigned GetRoomRuntimeRoomId(FRoomRuntime* Runtime);

// One element of the room's character placements (Runtime+0x504, a VC6 std::vector of 8-byte
// elements built by FUN_004356d0 from RoomData+0x174): the placement and the character
// standing on it (null while free).
class CharacterProperties;
struct CharPlaceInfo;
struct FCharacterObject;
struct FRoomCharPlacement
{
    CharPlaceInfo* Info;          // +0x00
    FCharacterObject* Character;  // +0x04
};

class FObjectProp;

// Runtime+0x1340 (bCameraMoveSkippable): the skip latch may end the camera move. Retail has no
// accessor: NavBar's FUN_00441700 stores 1 or 0 into it right before a camera track starts.
void RoomControlSetCameraMoveSkippable(FRoomRuntime* Runtime, unsigned char Value);

// The room camera move (Runtime +0x1338..+0x13a0 and the +0x500 camera):
//   FUN_0044c290 -- snap the room camera to the named RoomData +0x138 camera (any move
//                   stopped first); false when no camera has that name.
//   FUN_0044ba60 -- with no move running and a camera of that name, a 2-second cosine-eased
//                   move to it (+0x1338 = DAT_0066d359 = 1), dropped at once when the camera
//                   is already there; answers +0x1338. Done(A, B, 0) when the move ends.
//   FUN_00449990 -- the per-frame step, from the room update (LAB_0044ab70).
//   FUN_0044bfd0 -- a step (Arg0's low byte: forward / backward) along the room's
//                   "Camera_Track_%d" cameras, +0x13a4 the current number; with bArg4 a
//                   camera already standing there repeats the step (FUN_0044bd50). Answers
//                   FUN_0044ba60's result.
typedef void(__cdecl* FRoomCameraDoneCallback)(void* A, void* B, unsigned char bArg);

// Runtime+0x517, set when a room character's start script ran. FUN_005588a0 reads it directly
// and skips the arrival text while it is set.
unsigned char RoomControlStartScriptRan(FRoomRuntime* Runtime);

// FUN_004489f0 (cdecl) -- DAT_00668ab0 = Value: the player's ship lands in the next room that
// starts its scenes (read by FUN_00448a20 only). Callers: FUN_005588a0 with !bFromIntro;
// FUN_0044a540 clears it.
void RoomControlSetLandingPending(unsigned char Value);

// FUN_00559380 (cdecl) -- free every node of the RoomObject list DAT_00679a10 and zero its size;
// the objects themselves are left alone. Caller: the Gameflow shutdown FUN_00558870.
void ClearRoomObjectList();

// ---------------------------------------------------------------------------
// RoomControl subobjects
// ---------------------------------------------------------------------------

// Runtime+0x4C is the named-script table FAnimScriptSet (anim_script_set.h, FUN_00555a40 ..
// FUN_00555ca0).

// Runtime+0x504 is the character placement vector (FRoomCharPlacement, room_control.h): a VC6
// std::vector of 8-byte elements, built empty by FUN_0059ec40 (the allocator byte is an
// uninitialised stack byte there), filled by FUN_004356d0 and freed by FUN_004de730. The port
// keeps a std::vector, 0x10 bytes in the iterator-debug builds like retail's.

// One ship pad of the room (0x10 bytes): a RoomData ship placement (+0x184, both
// [PlayerShipPlacement] and [ForSaleShipPlacement]) and the ship standing on it.
struct FRoomShipPad
{
    // FUN_004486d0 (thiscall) -- each ship deleted through its slot 0 (with 1) and cleared.
    void Clear();
    // FUN_004487f0 (thiscall) -- the player's ship (Gf ObjectShip, operator new(0xb0)) on this
    // pad: the pad's frame, shadow on, the pad's y, its launching script, the player's ship
    // archetype and equipment list (FUN_004c3e10 / FUN_004c4060).
    void CreatePlayerShip();
    // FUN_00448700 (thiscall, RET 4) -- a DealerShipObject (operator new(0x64)) of the ship
    // package GoodId on this pad: its hull's ship archetype and the package's addon list.
    void CreateForSaleShip(unsigned GoodId);

    FRoomRuntime* Room;        // +0x00
    ShipPlaceInfo* Place;      // +0x04
    FObjectShip* Ship;         // +0x08
    FDealerShipObject* ForSale; // +0x0c
};

// The room's ship-pad table at Runtime+0x1020 (0x18 bytes: a VC6 vector of FRoomShipPad, then
// two ints). Built by FUN_00448a60 at the room load, destroyed by FUN_004488f0.
struct FRoomShipPadTable
{
    FRoomShipPadTable();
    // FUN_004488f0 -- every pad cleared (FUN_004486d0 inlined), the vector freed. The runtime's
    // destructor calls it as the member destructor; FUN_00448e90 is
    // the same teardown on the constructor's unwind path.
    ~FRoomShipPadTable();

    // FUN_004488d0 -- +0x00 the allocator byte, +0x04..+0x10 = 0, +0x14 = -1.
    void Init();
    // FUN_00448a60 (thiscall, RET 8) -- one pad per RoomData ship placement; the first free
    // player pad gets the player's ship (with a ship archetype and "player_ship" enabled); each
    // ship package the market sells (category 3) goes on the next free for-sale pad.
    void Build(FRoomRuntime* Runtime, FClientMarket* Market);
    // FUN_00448a00 -- the player pad has a landing script.
    bool HasLandingScript() const;
    // FUN_00448a20 -- with DAT_00668ab0, a player pad, a landing script and a ship there: the
    // ship plays the landing script; true then.
    bool PlayLanding();

    std::vector<FRoomShipPad> Pads; // +0x00 allocator byte, begin +0x04, end +0x08, cap +0x0c
    int Count;                      // +0x10 the placement count
    int PlayerPad;                  // +0x14 the pad of the player's ship, -1 = none

private:
    FRoomShipPadTable(FRoomShipPadTable const&) = delete;
    FRoomShipPadTable& operator=(FRoomShipPadTable const&) = delete;
};

#if defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL != 0
static_assert(sizeof(FRoomShipPadTable) == 0x18, "FRoomShipPadTable sizeof 0x18");
#endif
static_assert(sizeof(FRoomShipPad) == 0x10, "FRoomShipPad sizeof 0x10");

// FRoomRuntime::SceneFlags (+0x514 .. +0x517).
enum ERoomSceneFlag
{
    // +0x514: the room characters' start script was looked for (FUN_0044aca0, LAB_0044ab70);
    // it is not looked for again.
    kRoomStartScriptChecked = 0,
    // +0x515: FUN_0044a540 started a landing, goods-cart or character start-script sequence;
    // the start script then waits for the request below.
    kRoomEntryScriptStarted = 1,
    // +0x516: the start script requested (FUN_0044ad70, from the sequence stop FUN_004554c0).
    kRoomStartScriptRequested = 2,
    // +0x517: a room character's start script ran (read by FUN_005588a0).
    kRoomStartScriptRan = 3,
};

// Retail FUN_00449260 object (RoomControl). The RoomObject base (+0x00..+0x2f) is
// FRoomObject; RoomControl registers itself with a null owner (FUN_005593d0(0)), so no room's
// draw ever visits it. The Event::Handler face follows at +0x30: vftable 0x005cbe98 over the
// base's 0x005cbed8; registered for id 0xf by FUN_004496b0; torn down by FUN_00576010.
struct FRoomRuntime : public FRoomObject, public EventHandler
{
    FRoomRuntime();
    ~FRoomRuntime() override;

    // Slot 5 is LAB_0044ab70 in RoomControl's vtable 0x005cbea0; slots 1-4 and 6-12 are the
    // RoomObject ones.
    void Update(double DeltaSeconds) override;

    // LAB_0044ad80 -- Event::Handler slot 0 of the +0x30 face (0x005cbe98).
    bool OnEvent(unsigned MsgId) override;

    unsigned BaseId;                   // +0x34 FUN_004496b0 argument 1
    unsigned RoomId;                    // +0x38 FUN_004496b0 argument 2
    BaseData* LocationBase;             // +0x3C the BaseData of BaseId (FUN_0043a2b0)
    RoomData* Room;                     // +0x40 FUN_0043a3d0
    // +0x44 / +0x48: -1 from FUN_004496b0, then FUN_0044a030 takes them from the prop whose
    // RoomPropInfo +0x3f is set: its slot-6 engine instance and its script table's +0x0c script.
    InstanceIndex MainInstance;         // +0x44 RoomData::fixup argument
    unsigned long MainScript;           // +0x48 FUN_00555bf0 on +0x4c when not -1
    FAnimScriptSet Scripts;             // +0x4C FUN_00555a40 (0x18) → +0x64
    // +0x64: the playback of each room animation (FUN_004498b0), -1 when it has none.
    unsigned long Handles64[0x20];      // +0x64 all -1 at first
    // +0xE4: one FUN_00556fd0 wrapper per RoomData+0x144 light (FUN_004496b0).
    FGfLightWrapper* RoomLights[0x80];
    int RoomLightCount;                 // +0x2E4
    // +0x2E8: the per-frame light list FUN_0044b450 builds (at most 0x80).
    FGfLightWrapper* FrameLights[0x80];
    int FrameLightCount;                // +0x4E8
    float FrameAmbient[3];              // +0x4EC RoomData+0xd8..+0xe0, or 0 in single-light mode
    int SingleLightIndex;               // +0x4F8 index into RoomLights for single-light mode
    unsigned char bSingleLight;         // +0x4FC
    unsigned char Pad4FD[3];            // +0x4FD .. +0x4FF
    FRoomCamera* Camera;                // +0x500 FUN_00556be0
    std::vector<FRoomCharPlacement> Placements; // +0x504 FUN_0059ec40 (0x10) → +0x514
    // +0x514 .. +0x517, indexed by ERoomSceneFlag. Kept as one array: a new character with all
    // 0x40 slots full lands here (FRoomRuntime::UpdateCharacter).
    unsigned char SceneFlags[4];        // +0x514
    FCharacterObject* Characters[0x40];    // +0x518 the room's characters (FUN_00449e60)
    // +0x618: the room's props -- FUN_0044a030 appends one Gf::ObjectProp per enabled
    // RoomData+0x158 record, FUN_0044a380 appends one more, FUN_0044a2f0 finds one by name.
    FObjectProp* Props[0x80];           // +0x618
    int PropCount;                      // +0x818
    // +0x81C: the room's FlashLight objects (FUN_0044a3a0 fills them from the RoomData
    // FlashLightSet records); FUN_0044b570 hands them to FUN_00558000.
    FFlashLight* Flashlights[0x200];    // +0x81C
    int FlashlightCount;                // +0x101C
    FRoomShipPadTable ShipPads;         // +0x1020 FUN_004488d0 (0x18) → +0x1038
    // +0x1038 / +0x108c / +0x10e0: the room's "scene =" scripts FUN_0044a540 collects by type
    // (1 ambient, 2 traffic, 3 once), malloc'd paths, each with a flag byte (1 for the room's own
    // records) and a count. No bound check in FUN_0044a540.
    char* SceneAmbient[0x10];           // +0x1038
    unsigned char SceneAmbientFlag[0x10]; // +0x1078
    int SceneAmbientCount;              // +0x1088
    char* SceneTraffic[0x10];           // +0x108C
    unsigned char SceneTrafficFlag[0x10]; // +0x10CC
    int SceneTrafficCount;              // +0x10DC
    char* SceneOnce[0x10];              // +0x10E0
    unsigned char SceneOnceFlag[0x10];  // +0x1120
    int SceneOnceCount;                 // +0x1130
    // +0x1134 the room buttons FUN_0044ced0 builds from the RoomData hotspots (one per
    // hotspot whose behavior is 1..6), count +0x1334 (room_control_hotspot.h).
    RoomButton* Controllers[0x80];      // +0x1134
    int ControllerCount;                // +0x1334
    // The room camera move (FUN_0044ba60 starts it, FUN_00449990 runs it, FUN_0044c290 /
    // FUN_0044c490 stop it). To* is where the camera goes (from a RoomData camera), From* where
    // it was when the move started.
    unsigned char bCameraMoving;        // +0x1338
    unsigned char Pad1339[3];
    float CameraMoveDuration;           // +0x133c 2.0f (FUN_0044ba60)
    // +0x1340 -- NavBar stores it before a move (1, or 0 for the scripted track); FUN_00449990
    // ends a move at once when it and the skip latch are set.
    unsigned char bCameraMoveSkippable; // +0x1340
    unsigned char Pad1341[3];
    float CameraMoveElapsed;            // +0x1344
    Quaternion CameraToQuat;           // +0x1348
    float CameraToPos[3];               // +0x1358
    float CameraToHalfFovX;             // +0x1364
    float CameraToZNear;                // +0x1368
    float CameraToZFar;                 // +0x136c
    Quaternion CameraFromQuat;         // +0x1370
    float CameraFromPos[3];             // +0x1380
    float CameraFromHalfFovX;           // +0x138c
    float CameraFromZNear;              // +0x1390
    float CameraFromZFar;               // +0x1394
    FRoomCameraDoneCallback CameraDone; // +0x1398
    void* CameraDoneA;                  // +0x139c
    void* CameraDoneB;                  // +0x13a0
    // +0x13a4 -- the "Camera_Track_%d" step of FUN_0044bfd0 (FRoomRuntime::TrackScriptCamera).
    int TrackCameraIndex;                   // +0x13a4
    // +0x13a8 -- a dword FUN_0044ceb0 and FUN_0044ced0 zero; nothing in the exe reads it. TODO:
    // meaning not recovered.
    int Field_0x13a8;                   // +0x13a8

    // FUN_0044af60 (thiscall on the room) -- draw one object: FUN_0044ae80 lights, fog and
    // ambient; the "shadow_glare" glare light into the instance's appearance; then by render mode
    // the renderer (0, optionally "envmapbasic") or appearance slot 10 / 9 (1 / 2).
    void DrawObject(FRoomObject* Object);

    // FUN_0044b370 (thiscall on the room) -- the SHADOWS option and "shadow_glare" with
    // the room's shadow light on: appearance slot 12 (bFlag) or 11 with the camera and {A, B}.
    void DrawShadow(unsigned long A, unsigned long B, unsigned char bFlag);

    // FUN_0044ae80 (thiscall on the room) -- fog enable, scene ambient and the lights for Object.
    // CharacterObject's draw calls it directly.
    void LightObject(FRoomObject* Object);

    // FUN_00449c90 -- the +0x518 character whose record key (+0x154) is Key, or null. Caller:
    // FUN_00435dd0.
    FCharacterObject* FindCharacterByKey(unsigned Key);

    // FUN_00449cd0 -- the first +0x518 character on a placement of char type 1 (pc), or null.
    // Callers: FUN_00435cf0, the Gf script spawn.
    FCharacterObject* PlayerCharacter();

    // FUN_00449d10 -- the first +0x518 character of Role (+0x300), or null. Caller: NavBar's
    // SetProperty LAB_0043ee80.
    FCharacterObject* FindCharacterByRole(int Role);

    // FUN_00449d50 -- the first +0x518 character whose placement's setpoint segment is Name (DACOM
    // stricmp), or null. Caller: the Gf script spawn.
    FCharacterObject* FindCharacterByName(char const* Name);

    // FUN_00449e60 -- a character record of this room with a costume: the character of its key
    // updated in place (record, hotspot, idle script), or a new one on a free placement. Caller:
    // the LocationController's upsert FUN_0043abf0.
    void UpdateCharacter(CharacterProperties const& Props);

    // FUN_0044a2f0 -- the first +0x618 prop named Name, or null.
    FObjectProp* FindProp(char const* Name);

    // FUN_0044a380 -- append Prop to +0x618 (no bound check).
    void AddProp(FObjectProp* Prop);

    // FUN_0044c4a0 -- Runtime+0x1338 (bCameraMoving): a room camera move is running. Callers: the
    // NavBar and the Gameflow tick FUN_00558ac0, which restores the software cursor only while it
    // is clear.
    unsigned char IsCameraMoving();

    // FUN_0044c490 -- +0x1338 = 0 and DAT_0066d359 (G_bNnKeepPanelOpen, nn_main_button.h) = 0.
    // Callers: the NavBar (nav_bar.cpp).
    void ClearCameraMoving();

    // FUN_0044d240 -- with C != -1 and B == 3, slot 1 on every +0x1134 room controller whose +0x04
    // or +0x08 dword equals C. A is not read. Callers: the NavBar.
    void NotifyControllers(int A, int B, int C);
    bool SnapCamera(char const* CameraName);
    bool TrackCamera(char const* CameraName, FRoomCameraDoneCallback Done, void* A, void* B);
    void UpdateCameraMove(float Delta);
    bool TrackScriptCamera(int Arg0, FRoomCameraDoneCallback Done, void* A, void* B,
                           unsigned char bArg4);

    // FUN_0044ad70 (thiscall) -- +0x516 = 1: the room update runs the room characters' start script
    // again. Caller: the Gf script sequence stop FUN_004554c0.
    void RequestCharacterScripts();

    // FUN_0044aca0 (thiscall) -- the room's scene scripts started (FUN_0044a540) as Gf script
    // sequences, then the character start script latch. Caller: the room load FUN_00439d80.
    void PlayScenes();

    // FUN_0044b570 (thiscall, no arguments) -- the room draw FUN_0043bbc0 jumps to: viewport and
    // camera, depth / cull / blend, the frame light list (FUN_0044b450), the room fog
    // (FUN_00454760), then the objects this room owns, layer by layer in ascending order: slot 7
    // inside the material-batcher sink (FUN_0041a670 .. FUN_0041a6b0), slot 8, the flashlight
    // flares (FUN_00558000), full-white ambient, the lights unlinked, and slot 9 plus the cue list
    // (FUN_00456740) inside Alchemy BeginDraw / EndDraw with dithering off.
    void Draw();

    // Defined in room_control.cpp.
    void ClearButtons();

    // Defined in room_control.cpp.
    void DeleteButtons();

    // Defined in room_control.cpp.
    void LoadProps();

    // Defined in room_control.cpp.
    void DeleteCharacter(int Slot);

    // Defined in room_control.cpp.
    bool FindStartScript(int* OutCharacterKey, int* OutBehaviorKey);

    // Defined in room_control.cpp.
    void StartScenes();

    // Defined in room_control.cpp.
    void UpdateButtons(float Delta);

    // Defined in room_control.cpp.
    void StartAnimations();

    // Defined in room_control.cpp.
    void BuildButtons();

    // Defined in room_control.cpp.
    void LoadFlashlights();

    // Defined in room_control.cpp.
    void CollectLights(ICamera* /*Camera*/);

    // FUN_0044ab60 (thiscall) -> FUN_00448a00 on the ship-pad table Runtime+0x1020: the room has a
    // player ship pad (+0x1034 != -1) and its ShipPlaceInfo has a landing script (+0x38).
    // Caller: FUN_005588a0.
    bool HasLandingScript();
};
