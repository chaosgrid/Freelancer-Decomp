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
#include <list>
#include <stdint.h>
#include <vector>

struct ICamera;
class RoomData;
struct IRenderPipeline;

struct FRoomRuntime;

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

// FUN_0044af60 (thiscall on the room) -- draw one object: FUN_0044ae80 lights, fog and
// ambient; the "shadow_glare" glare light into the instance's appearance; then by render mode
// the renderer (0, optionally "envmapbasic") or appearance slot 10 / 9 (1 / 2).
void RoomControlDrawObject(FRoomRuntime* Room, FRoomObject* Object);

// FUN_0044b370 (thiscall on the room) -- the SHADOWS option and "shadow_glare" with
// the room's shadow light on: appearance slot 12 (bFlag) or 11 with the camera and {A, B}.
void RoomControlDrawShadow(FRoomRuntime* Room, unsigned long A, unsigned long B,
                           unsigned char bFlag);

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

// FUN_0044ae80 (thiscall on the room) -- fog enable, scene ambient and the lights for Object.
// CharacterObject's draw calls it directly.
void RoomControlLightObject(FRoomRuntime* Room, FRoomObject* Object);

// FUN_00449c90 -- the +0x518 character whose record key (+0x154) is Key, or null. Caller:
// FUN_00435dd0.
FCharacterObject* RoomControlFindCharacterByKey(FRoomRuntime* Runtime, unsigned Key);
// FUN_00449cd0 -- the first +0x518 character on a placement of char type 1 (pc), or null.
// Callers: FUN_00435cf0, the Gf script spawn.
FCharacterObject* RoomControlPlayerCharacter(FRoomRuntime* Runtime);
// FUN_00449d10 -- the first +0x518 character of Role (+0x300), or null. Caller: NavBar's
// SetProperty LAB_0043ee80.
FCharacterObject* RoomControlFindCharacterByRole(FRoomRuntime* Runtime, int Role);
// FUN_00449d50 -- the first +0x518 character whose placement's setpoint segment is Name (DACOM
// stricmp), or null. Caller: the Gf script spawn.
FCharacterObject* RoomControlFindCharacterByName(FRoomRuntime* Runtime, char const* Name);
// FUN_00449e60 -- a character record of this room with a costume: the character of its key
// updated in place (record, hotspot, idle script), or a new one on a free placement. Caller:
// the LocationController's upsert FUN_0043abf0.
void RoomControlUpdateCharacter(FRoomRuntime* Runtime, CharacterProperties const& Props);

class FObjectProp;
// FUN_0044a2f0 -- the first +0x618 prop named Name, or null.
FObjectProp* RoomControlFindProp(FRoomRuntime* Runtime, char const* Name);
// FUN_0044a380 -- append Prop to +0x618 (no bound check).
void RoomControlAddProp(FRoomRuntime* Runtime, FObjectProp* Prop);

// FUN_0044c4a0 -- Runtime+0x1338 (bCameraMoving): a room camera move is running. Callers: the
// NavBar and the Gameflow tick FUN_00558ac0, which restores the software cursor only while it
// is clear.
unsigned char RoomControlIsCameraMoving(FRoomRuntime* Runtime);

// FUN_0044c490 -- +0x1338 = 0 and DAT_0066d359 (G_bNnKeepPanelOpen, nn_main_button.h) = 0.
// Callers: the NavBar (nav_bar.cpp).
void RoomControlClearCameraMoving(FRoomRuntime* Runtime);

// Runtime+0x1340 (bCameraMoveSkippable): the skip latch may end the camera move. Retail has no
// accessor: NavBar's FUN_00441700 stores 1 or 0 into it right before a camera track starts.
void RoomControlSetCameraMoveSkippable(FRoomRuntime* Runtime, unsigned char Value);

// FUN_0044d240 -- with C != -1 and B == 3, slot 1 on every +0x1134 room controller whose +0x04
// or +0x08 dword equals C. A is not read. Callers: the NavBar.
void RoomControlNotifyControllers(FRoomRuntime* Runtime, int A, int B, int C);

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
bool RoomControlSnapCamera(FRoomRuntime* Runtime, char const* CameraName);
bool RoomControlTrackCamera(FRoomRuntime* Runtime, char const* CameraName,
                            FRoomCameraDoneCallback Done, void* A, void* B);
void RoomControlUpdateCameraMove(FRoomRuntime* Runtime, float Delta);
bool RoomControlTrackScriptCamera(FRoomRuntime* Runtime, int Arg0, FRoomCameraDoneCallback Done,
                                  void* A, void* B, unsigned char bArg4);

// Runtime+0x517, set when a room character's start script ran. FUN_005588a0 reads it directly
// and skips the arrival text while it is set.
unsigned char RoomControlStartScriptRan(FRoomRuntime* Runtime);

// FUN_0044ad70 (thiscall) -- +0x516 = 1: the room update runs the room characters' start script
// again. Caller: the Gf script sequence stop FUN_004554c0.
void RoomControlRequestCharacterScripts(FRoomRuntime* Runtime);

// FUN_0044aca0 (thiscall) -- the room's scene scripts started (FUN_0044a540) as Gf script
// sequences, then the character start script latch. Caller: the room load FUN_00439d80.
void RoomControlPlayScenes(FRoomRuntime* Runtime);

// FUN_0044ab60 (thiscall) -> FUN_00448a00 on the ship-pad table Runtime+0x1020: the room has a
// player ship pad (+0x1034 != -1) and its ShipPlaceInfo has a landing script (+0x38).
// Caller: FUN_005588a0.
bool RoomControlHasLandingScript(FRoomRuntime* Runtime);

// FUN_004489f0 (cdecl) -- DAT_00668ab0 = Value: the player's ship lands in the next room that
// starts its scenes (read by FUN_00448a20 only). Callers: FUN_005588a0 with !bFromIntro;
// FUN_0044a540 clears it.
void RoomControlSetLandingPending(unsigned char Value);

// FUN_00559380 (cdecl) -- free every node of the RoomObject list DAT_00679a10 and zero its size;
// the objects themselves are left alone. Caller: the Gameflow shutdown FUN_00558870.
void ClearRoomObjectList();

// FUN_0044b570 (thiscall, no arguments) -- the room draw FUN_0043bbc0 jumps to: viewport and
// camera, depth / cull / blend, the frame light list (FUN_0044b450), the room fog
// (FUN_00454760), then the objects this room owns, layer by layer in ascending order: slot 7
// inside the material-batcher sink (FUN_0041a670 .. FUN_0041a6b0), slot 8, the flashlight
// flares (FUN_00558000), full-white ambient, the lights unlinked, and slot 9 plus the cue list
// (FUN_00456740) inside Alchemy BeginDraw / EndDraw with dithering off.
void RoomControlDraw(FRoomRuntime* Runtime);
