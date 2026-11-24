// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail Client\space\HUD.cpp (FDUMP path at .rdata 0x005d6dcc) -- the HUD element registry, the
// HUDItem base class every HUD panel derives from, the HUDFACTORY<T> self-registering factories,
// and HUD_Steer (the mouse-flight steering arrow).
//
// Globals (.data):
//   DAT_00673510 / DAT_00673514 -- factory chain head / tail   (LAB_004c72a0 zeroes them)
//   DAT_00673518 / DAT_0067351c -- active HUDItem list head / tail (LAB_004c7260)
//   DAT_00673408 / DAT_0067340c -- the stored hud.ini path, length + char[0x104] (LAB_004c7360)
//   DAT_00673520 -- the HUD ShapeDB (G_HudShapeDB, texture_shapes.h)
//   DAT_00673570 -- the [HUD] static_txm texture reference (static ctor LAB_004cc950)
//   DAT_00612374 -- the [HUD] static_blend int (initial value 2)
//   DAT_00673548 -- HUD_Steer::Render's lazily acquired 3DMathEngine (FUN_005b73e0)

#include "Common/Graphics/client_mesh_load.h"
#include "common_goods.h"  // BaseWatcher
#include "common_tstring.h"

class INI_Reader;
struct IObjRW;
struct FSpaceCamera;
struct FTextureRef;

// The USER_* commands the HUD panels register, handle and post. The id is the index of the
// nickname in retail's KeyMap table (PTR_s_USER_NONE_00614dd8, FKeyCmd::NicknameId), which is
// what the handler tree dispatches as the event id. Only the ids the HUD uses are listed.
enum EUserCmd : unsigned
{
    USER_WHEEL                      = 0x04,
    USER_BUTTON0                    = 0x06,
    USER_CANCEL                     = 0x0f,
    USER_ACTIVATE_ITEM              = 0x19,
    USER_SET_TARGET                 = 0x1c,
    USER_NEXT_TARGET                = 0x1d,
    USER_PREV_TARGET                = 0x1e,
    USER_NEXT_SUBTARGET             = 0x21,
    USER_PREV_SUBTARGET             = 0x22,
    USER_CLEAR_TARGET               = 0x23,
    USER_NEXT_ENEMY                 = 0x24,
    USER_PREV_ENEMY                 = 0x25,
    USER_CLOSEST_ENEMY              = 0x26,
    USER_X_ROTATE                   = 0x2a,
    USER_X_UNROTATE                 = 0x2b,
    USER_Y_ROTATE                   = 0x2c,
    USER_Y_UNROTATE                 = 0x2d,
    USER_LOOK_ROTATE_CAMERA_LEFT    = 0x32,
    USER_LOOK_ROTATE_CAMERA_RIGHT   = 0x33,
    USER_LOOK_ROTATE_CAMERA_UP      = 0x34,
    USER_LOOK_ROTATE_CAMERA_DOWN    = 0x35,
    USER_AFTERBURN                  = 0x3a,
    USER_AFTERBURN_OFF              = 0x3b,
    USER_AFTERBURN_ON               = 0x3c,
    USER_TOGGLE_AUTO_AVOIDANCE      = 0x3d,
    USER_TOGGLE_AUTO_LEVEL          = 0x3e,
    USER_INC_THROTTLE               = 0x43,
    USER_DEC_THROTTLE               = 0x44,
    USER_CRUISE                     = 0x45,
    USER_CONTACT_LIST               = 0x54,
    USER_SWITCH_TO_PLAYER_SHIP      = 0x56,
    USER_SWITCH_TO_WEAPON_LIST      = 0x57,
    USER_SWITCH_TO_TARGET           = 0x58,
    USER_FORMATION_LIST             = 0x59,
    USER_STATUS                     = 0x5e,
    USER_TARGET                     = 0x5f,
    USER_MINIMIZE_HUD               = 0x6d,
    USER_COLLECT_LOOT               = 0x6f,
    USER_MANEUVER_TRAIL             = 0x74,
    USER_MANEUVER_EVADE             = 0x76,
    USER_MANEUVER_ENGINEKILL        = 0x77,
    USER_MANEUVER_BRAKE_REVERSE     = 0x78,
    USER_MANEUVER_DOCK              = 0x79,
    USER_MANEUVER_GOTO              = 0x7a,
    USER_MANEUVER_FACE              = 0x7b,
    USER_MANEUVER_DRASTIC_EVADE     = 0x7e,
    USER_MANEUVER_FORMATION         = 0x7f,
    USER_MANEUVER_STRAFE            = 0x80,
    USER_MANEUVER_TRAIL_CLOSER      = 0x81,
    USER_MANEUVER_TRAIL_FARTHER     = 0x82,
    USER_MANEUVER_CORKSCREW_EVADE   = 0x83,
    USER_MANEUVER_SLIDE_EVADE       = 0x84,
    USER_MANEUVER_SLIDE_EVADE_LEFT  = 0x85,
    USER_MANEUVER_SLIDE_EVADE_RIGHT = 0x86,
    USER_MANEUVER_SLIDE_EVADE_UP    = 0x87,
    USER_MANEUVER_SLIDE_EVADE_DOWN  = 0x88,
    USER_SCAN_CARGO                 = 0x8a,
    USER_TRACTOR_BEAM               = 0x8b,
    USER_REPAIR_HEALTH              = 0x8f,
    USER_REPAIR_SHIELD              = 0x90,
    USER_WEAPON_GROUP1              = 0x91,
    USER_WEAPON_GROUP2              = 0x92,
    USER_WEAPON_GROUP3              = 0x93,
    USER_WEAPON_GROUP4              = 0x94,
    USER_WEAPON_GROUP5              = 0x95,
    USER_WEAPON_GROUP6              = 0x96,
    USER_TOGGLE_WEAPON1             = 0x97,
    USER_TOGGLE_WEAPON2             = 0x98,
    USER_TOGGLE_WEAPON3             = 0x99,
    USER_TOGGLE_WEAPON4             = 0x9a,
    USER_TOGGLE_WEAPON5             = 0x9b,
    USER_TOGGLE_WEAPON6             = 0x9c,
    USER_TOGGLE_WEAPON7             = 0x9d,
    USER_TOGGLE_WEAPON8             = 0x9e,
    USER_TOGGLE_WEAPON9             = 0x9f,
    USER_TOGGLE_WEAPON10            = 0xa0,
    USER_FIRE_WEAPON1               = 0xa1,
    USER_FIRE_WEAPON2               = 0xa2,
    USER_FIRE_WEAPON3               = 0xa3,
    USER_FIRE_WEAPON4               = 0xa4,
    USER_FIRE_WEAPON5               = 0xa5,
    USER_FIRE_WEAPON6               = 0xa6,
    USER_FIRE_WEAPON7               = 0xa7,
    USER_FIRE_WEAPON8               = 0xa8,
    USER_FIRE_WEAPON9               = 0xa9,
    USER_FIRE_WEAPON10              = 0xaa,
    USER_FIRE_WEAPON_GROUP1         = 0xab,
    USER_FIRE_WEAPON_GROUP2         = 0xac,
    USER_FIRE_WEAPON_GROUP3         = 0xad,
    USER_FIRE_WEAPON_GROUP4         = 0xae,
    USER_FIRE_WEAPON_GROUP5         = 0xaf,
    USER_FIRE_WEAPON_GROUP6         = 0xb0,
    USER_ASSIGN_WEAPON_GROUP1       = 0xb1,
    USER_ASSIGN_WEAPON_GROUP2       = 0xb2,
    USER_ASSIGN_WEAPON_GROUP3       = 0xb3,
    USER_ASSIGN_WEAPON_GROUP4       = 0xb4,
    USER_ASSIGN_WEAPON_GROUP5       = 0xb5,
    USER_ASSIGN_WEAPON_GROUP6       = 0xb6,
    USER_REMAPPABLE_LEFT            = 0xb7,
    USER_REMAPPABLE_RIGHT           = 0xb8,
    USER_REMAPPABLE_UP              = 0xb9,
    USER_REMAPPABLE_DOWN            = 0xba,
    USER_FIRE_FORWARD               = 0xbb,
    USER_LAUNCH_MISSILES            = 0xbc,
    USER_LAUNCH_MINES               = 0xbd,
    USER_LAUNCH_COUNTERMEASURES     = 0xbe,
    USER_AUTO_TURRET                = 0xbf,
    USER_LAUNCH_TORPEDOS            = 0xc0,
    USER_LAUNCH_CRUISE_DISRUPTORS   = 0xc1,
    USER_MANEUVER_FREE_FLIGHT       = 0xc6,
    USER_GROUP_INVITE               = 0xc9,
    USER_TRADE_REQUEST              = 0xca,
};

// -------------------------------------------------------------------------------------------
// HUDItem (RTTI .?AVHUDItem@@, vftable 0x005d6eac, 11 slots, 0x38 bytes).
//
// No out-of-line constructor exists: every derived constructor inlines it (e.g. 0x004c84d8 in
// HUD_Steer's factory, 0x004c8de3 in HUD_Button's), including a redundant inlined SetTarget(0).
// Destructor FUN_004c8630 (vptr back, watcher cleared); scalar deleting FUN_004c8650.
//
// Slots (retail byte offset; implementation in the HUDItem vftable):
//   0 +0x00 FUN_004c8650  scalar deleting dtor
//   1 +0x04 FUN_0044fd00  false -- HUD_Steer answers its +0x38 byte (LAB_004c8670)
//   2 +0x08 FUN_0053e4a0  no-op -- broadcast by FUN_004c78a0 from the HUD camera's slot 0x5c,
//                          whose argument is the ViewRect it just applied
//   3 +0x0c FUN_004b6c50  true -- FUN_004c73d0 keeps the element only when true
//   4 +0x10 FUN_00401e90  no-op -- FUN_004c77a0's first pass
//   5 +0x14 FUN_004c81a0  watch an IObjRW (broadcast by FUN_004c7330)
//   6 +0x18 FUN_0053aaa0  no-op -- FUN_004c8090 (camera, frame time)
//   7 +0x1c FUN_0053f330  false -- polled by FUN_004c7820 (camera slot 0x70)
//   8 +0x20 FUN_0053f330  -- polled by FUN_004c7860 (camera slot 0x74)
//   9 +0x24 FUN_0053f330
//  10 +0x28 FUN_00401e90  no-op
// Slots 7 and 8 receive the command record FUN_0054bcb0 hands the active camera (a 0x28-byte
// block whose first dword is the command id -- HUD_Button::MouseEvent 0x004c9430 builds one).
// None of the twelve HUDItem-derived vftables overrides slots 7..10, and only HUD_Steer's
// overrides slot 1, so their names stay open.
// -------------------------------------------------------------------------------------------

class HUDItem
{
public:
    HUDItem();
    // FUN_004c8630
    virtual ~HUDItem();

    // slot 1 (+0x04) FUN_0044fd00
    virtual bool Slot1();
    // slot 2 (+0x08) FUN_0053e4a0
    virtual void SetViewport(int const* ViewRect4);
    // slot 3 (+0x0c) FUN_004b6c50
    virtual bool Init();
    // slot 4 (+0x10) FUN_00401e90
    virtual void Shutdown();
    // slot 5 (+0x14) FUN_004c81a0 -- watch Target's Watchable (IObjRW + 8), or nothing
    virtual void SetTarget(IObjRW* Target);
    // slot 6 (+0x18) FUN_0053aaa0
    virtual void Render(FSpaceCamera* Camera, float DeltaSeconds);
    // slots 7 / 8 / 9 (+0x1c / +0x20 / +0x24) FUN_0053f330
    virtual bool Slot7(void* Command);
    virtual bool Slot8(void* Command);
    virtual bool Slot9(void* Command);
    // slot 10 (+0x28) FUN_00401e90
    virtual void Slot10();

    // FUN_004c81d0 -- with a tool-tip id at +0x30, feed the ToolTip window (FUN_004be370) with
    // a rich-text copy of that string. HUD_Button::Update calls it while the button is hovered.
    void ShowToolTip();

    TString<32> Name;          // +0x04
    BaseWatcher Target;        // +0x28
    int ToolTipIds;            // +0x30 ("tool_tip_id" in the HUD_Button ini)
    HUDItem* Next;             // +0x34 active-list link

private:
    HUDItem(HUDItem const&) = delete;
    HUDItem& operator=(HUDItem const&) = delete;
};

static_assert(sizeof(HUDItem) == 0x38, "retail HUDItem is 0x38 bytes");

// -------------------------------------------------------------------------------------------
// HUDItem_Factory (RTTI .?AVHUDItem_Factory@@, vftable 0x005d6e30: two slots, both
// FUN_0053e180 returning null) and HUDFACTORY<T>.
//
// Layout: +0x00 vptr, +0x04 next in the factory chain, +0x08 TString<32> section name. The
// constructor is inlined into every HUDFACTORY<T> constructor (0x004cd270, 0x004d9990, ...)
// and appends to the chain (FUN_004c72b0). The destructor is not virtual: the atexit thunks
// call LAB_004c8350 directly, which unlinks the factory (FUN_004c72e0's body, inlined).
// -------------------------------------------------------------------------------------------

class HUDItem_Factory
{
public:
    explicit HUDItem_Factory(char const* SectionName);
    // LAB_004c8350
    ~HUDItem_Factory();

    // slot 0 (+0x00) -- build the element for the [SectionName] section the reader sits on.
    // FUN_004c73d0 passes its INI_Reader by address.
    virtual HUDItem* Create(INI_Reader& Reader);
    // slot 1 (+0x04) FUN_0053e180 -- never overridden, never called through a traced site
    virtual HUDItem* Slot1(void* Arg);

    HUDItem_Factory* Next;     // +0x04
    TString<32> SectionName;   // +0x08

private:
    HUDItem_Factory(HUDItem_Factory const&) = delete;
    HUDItem_Factory& operator=(HUDItem_Factory const&) = delete;
};

// Retail HUDFACTORY<T>::Create (e.g. FUN_004d9a00 for HUD_Maneuvers, FUN_004cd2e0 for
// HUD_Receiver): operator new(sizeof(T)), T(Reader), and the HUDItem face of the result -- which
// is not the object's address when HUDItem is not T's first base (HUD_Receiver: +0x7c).
template <class T>
class HUDFACTORY : public HUDItem_Factory
{
public:
    explicit HUDFACTORY(char const* SectionName) : HUDItem_Factory(SectionName) {}

    HUDItem* Create(INI_Reader& Reader) override
    {
        T* const Element = new T(Reader);
        return Element;
    }
};

// -------------------------------------------------------------------------------------------
// HUD_Steer (RTTI .?AUHUD_Steer@@ : HUDItem, vftable 0x005d6e7c, 0x74 bytes).
// Built by UHUD_Steer::HUDFACTORY::Create FUN_004c84a0 (the constructor is inlined there),
// destroyed by FUN_004c8c20 (scalar deleting FUN_004c8c00). Its factory is the static object
// DAT_006733dc (ctor FUN_004c8400 from the XC entry LAB_004c82f0, "Steer").
// Overrides: 0 FUN_004c8c00, 1 LAB_004c8670 (the +0x38 byte), 6 LAB_004c8680 Render.
// -------------------------------------------------------------------------------------------

struct HUD_Steer : public HUDItem
{
    // FUN_004c84a0's inlined constructor
    explicit HUD_Steer(INI_Reader& Reader);
    // FUN_004c8c20
    ~HUD_Steer() override;

    // LAB_004c8670 -- returns Byte38
    bool Slot1() override;
    // LAB_004c8680
    void Render(FSpaceCamera* Camera, float DeltaSeconds) override;

    unsigned char Byte38;          // +0x38 -- the ctor clears it; nothing in the PE sets it
    unsigned char Pad39[3];
    float Radius;                  // +0x3c "radius", 40.0f
    float Range;                   // +0x40 "range", 200.0f
    float Field44;                 // +0x44 0.2f, never read
    unsigned char Byte48;          // +0x48 cleared, never read
    unsigned char Pad49[3];
    FClientMeshHolder MeshHolder;  // +0x4c interface\hud\HUD_steeringarrow.3db
};

static_assert(sizeof(HUD_Steer) == 0x74, "retail HUD_Steer is 0x74 bytes");

// FUN_004c8400 -- UHUD_Steer::HUDFACTORY: the factory whose Create builds HUD_Steer. Retail
// keeps a separate out-of-line copy of the base constructor for it (with the chain append
// inlined) instead of the shared inlined form; the effect is the same.
class HUD_SteerFactory : public HUDItem_Factory
{
public:
    explicit HUD_SteerFactory(char const* SectionName) : HUDItem_Factory(SectionName) {}
    // FUN_004c84a0
    HUDItem* Create(INI_Reader& Reader) override;
};

// -------------------------------------------------------------------------------------------
// Registry functions.
// -------------------------------------------------------------------------------------------

// LAB_004c72a0 -- zero the factory-chain head/tail.
void HudElementFactoryListInit();
// FUN_004c72b0 -- append to the factory chain's tail.
void HudElementFactoryListAdd(HUDItem_Factory* Factory);
// FUN_004c72e0 -- unlink a factory from the chain (no-op if not present).
void HudElementFactoryListRemove(HUDItem_Factory* Factory);

// LAB_004c7260 -- zero the active-element list head/tail.
void HudElementListInit();
// FUN_004c7330 (cdecl) -- HUDItem::SetTarget(Target) on every active element.
void HudElementListSetTarget(IObjRW* Target);
// FUN_004c78a0 (cdecl) -- HUDItem::SetViewport(ViewRect4) on every active element.
void HudElementListSetViewport(int const* ViewRect4);
// FUN_004c8090 (cdecl) -- HUDItem::Render on every active element, bracketed by the material
// batcher's bin order (1, -1) / (1, 1). Its one caller is the HUD camera's draw FUN_0051daa0.
void HudElementListRender(FSpaceCamera* Camera, float DeltaSeconds);
// FUN_004c7820 (cdecl) -- true at the first active element whose Slot7(Command) is true.
bool HudElementListAnySlot7(void* Command);
// FUN_004c7860 (cdecl) -- the same over Slot8.
bool HudElementListAnySlot8(void* Command);
// FUN_004c77a0 -- release the static_txm texture, Shutdown() on every element, delete every
// element, empty the list, then clear the HUD ShapeDB (FUN_00425a20). Called by the space
// teardown.
void HudElementListShutdown();

// FUN_004c7370 -- ProgressCB's `HUD = ...` key stores the resolved path (cap 0x103).
void SetHudIniPath(char const* Path);
// The path last stored by SetHudIniPath (DAT_0067340c).
char const* GetHudIniPath();

// FUN_004c73d0 -- open the stored hud.ini; [HUD] loads shapes / static_txm / static_blend,
// every other section is offered to each factory in chain order until one builds an element;
// an element whose Init() is false is deleted, a section nobody builds is FDUMPed. Then the
// mouse-flight state is reset. Called by the space init.
void LoadHudElementsFromIni();

// DAT_00673570 / DAT_00612374 -- read by the comm window's static-noise draw (FUN_004cc9a0).
FTextureRef* GetHudStaticTexture();
int GetHudStaticBlend();

// FUN_004c8b10 (cdecl Out, In, Length) -- Out = In * (1 / Length).
void HudVec3ScaleByInverse(float* Out, float const* In, float Length);

// The three rotation helpers on a row-major 3x3 matrix (retail thiscall on the matrix).
// FUN_004c8b40 -- rotation about Y: [ cos 0 sin ; 0 1 0 ; -sin 0 cos ].
void HudMatrixRotationY(float* M, float Angle);
// FUN_004c8b80 -- rows 1 and 2 turned column by column:
//   row1 = row1 * cos - sin * row2;  row2 = row1 * sin + cos * row2.
void HudMatrixRotateRows12(float* M, float Angle);
// FUN_004c8bc0 -- rows 0 and 1 turned the same way.
void HudMatrixRotateRows01(float* M, float Angle);

// FUN_0053eb10 (cdecl X, Y) -- the angle of (X, Y) measured from the +Y axis: 0 for (0, 0);
// with |X| > |Y| it is atan(Y / |X|) -/+ pi/2, otherwise atan(-X / |Y|), shifted by +/- pi
// when Y < 0 (sign chosen by the sign of that arctangent).
float HudVectorAngle(float X, float Y);
