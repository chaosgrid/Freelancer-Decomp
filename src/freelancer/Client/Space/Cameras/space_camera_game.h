// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The two intermediate camera classes every concrete in-flight camera derives from.
//
// All seven named camera classes share the base's first eighteen slots verbatim and override
// exactly slots 0x48 and 0x4c with the same pair of functions, so those overrides live in a
// common intermediate, "GameCamera" (vtable .rdata 0x005dbf5c, ctor 0x0051b2f0). A second
// intermediate, "HUDCamera" (vtable 0x005dc08c, ctor 0x0051ca80), adds a secondary base and is
// what the four cockpit cameras derive from:
//
//   FSpaceCamera        0x000..0x0c0   vtable 0x005c8944, ctor 0x0040f150
//     FGameCamera       0x0c0..0x0f8   vtable 0x005dbf5c, ctor 0x0051b2f0
//       FHudCamera      0x0f8..0x100   vtable 0x005dc08c, ctor 0x0051ca80
//         FirstPerson   0x005db944  ctor 0x00517180
//         ThirdPerson   0x005dbb14  ctor 0x00518600
//         Turret        0x005dbcc4
//         RearView      0x005dbe24  ctor 0x0051ad30
//       ScriptCamera    0x005dc164  (derives from FGameCamera, not FHudCamera)
//
// Calling conventions: the camera vtable MIXES two. Slots 0x00..0x44 take `this` as an explicit
// stack argument (0x0051b431 calls slot 0x18 as `push edi; call [ecx+0x18]`); 0x48 and 0x4c are
// __thiscall (0x0051b3fe). The port reproduces the mix (space_camera.h: DACOM_API/stdcall for
// 0x00..0x44, plain virtual for GetVelocity and SetProperty; see docs/re/deviations.md).

#include "Space/Cameras/space_camera.h"
#include "Core/event_handler.h"

#include "common_goods.h"    // BaseWatcher — retail type at FGameCamera+0xc0

#include <stddef.h>

// Extra vtable slots the GameCamera layer adds past the base's twenty, with the retail
// addresses of the implementations each concrete class installs.
//
//   +0x50  0x0051c8b0 (FirstPerson uses 0x00401e90 instead)
//   +0x54  the class name literal
//   +0x58  attach: GameCamera 0x0051b3c0, others their own
//   +0x5c  0x0051cb90 / GameCamera 0x0051b490
//   +0x60  0x005172f0 "no object watched"
//   +0x64  per class
//   +0x68  0x0051b0b0 / GameCamera 0x0051b480 (tail-jumps to 0x00401e90)
//   +0x6c  per class
//   +0x70  0x0051ad20 (tail-jumps to 0x0051da40) / GameCamera 0x0053e180
//   +0x74  0x0051da70
//   +0x78  0x0051b250 / GameCamera 0x0051c200
//   +0x7c  the sound environment the frame hands FUN_00428030 (0x0054b0b9): LAB_00517320
//          (`MOV EAX,2`) in HUDCamera and all four cockpit cameras, LAB_0051ca40 (`MOV EAX,1`)
//          in GameCamera and the cameras built on it (.rdata 0x005dbfd8, 0x005dc1e0, 0x005dc350)
enum EGameCameraSlot
{
    kGameCamSlotScaleEnvFx   = 0x50,
    kGameCamSlotGetName      = 0x54,
    kGameCamSlotAttach       = 0x58,
    kGameCamSlotApplyViewport = 0x5c,
    kGameCamSlotHasNoTarget  = 0x60,
    kGameCamSlotActivate     = 0x64,
    kGameCamSlotDeactivate   = 0x68,
    kGameCamSlotUpdate       = 0x6c,
    kGameCamSlotCommand70    = 0x70,
    kGameCamSlotCommand74    = 0x74,
    kGameCamSlotDrawScene    = 0x78,
    kGameCamSlotSoundMode    = 0x7c,
    kGameCamSlotCount        = 32          // the vtable is 32 slots, 0x80 bytes (HUDCamera's
                                           // 0x005dc08c..0x005dc108, the name string at 0x005dc10c)
};

// FGameCamera::Flags (+0xe8), read by FUN_0051b500 (FGameCamera::Clear).
enum EGameCameraFlag
{
    kGameCameraClearColour = 0x1, // clear the colour buffer to ClearColor
    kGameCameraClearDepth = 0x2   // clear the depth buffer
};

// Retail layout, every offset traced to the store in FUN_0051b2f0.
struct FGameCamera : FSpaceCamera
{
    ~FGameCamera() = default;
    void GetVelocity(Vector* Out3) override;
    int SetProperty(char const* Key, char const* Value) override;
    virtual void ScaleEnvironmentEffects();               // formerly slot +0x50
    virtual char const* GetName();                        // formerly slot +0x54
    virtual void Attach(void* Object);                   // formerly slot +0x58
    virtual void ApplyViewport(int const* ViewRect4);     // formerly slot +0x5c
    virtual bool IsFinished();                           // formerly slot +0x60
    virtual bool Activate(int const* ViewRect4);          // formerly slot +0x64
    virtual void Deactivate();                           // formerly slot +0x68
    virtual void Update(float DeltaSeconds);              // formerly slot +0x6c
    virtual int HandleCommand70(void* Command);           // formerly slot +0x70
    virtual int HandleCommand74(void* Command);           // formerly slot +0x74
    virtual void DrawScene(unsigned LayerFlags, unsigned Param2); // formerly slot +0x78
    virtual int GetSoundMode();                          // formerly slot +0x7c


    BaseWatcher    Watched;          // +0x0c0  Common.dll BaseWatcher (sizeof 8)
    float          BaseHalfFovXDeg;  // +0x0c8  35.0f; re-cached whenever fovx/fovy is set
    unsigned char  Pad0CC[4];        // +0x0cc  the ctor never touches this
    float          LastPosition[3];  // +0x0d0  zeroed; the position as of the last update
    float          Velocity[3];      // +0x0dc  zeroed; slot 0x48 hands this out
    unsigned char  Flags;            // +0x0e8  the ctor clears its low two bits; bit 0 = clear the
                                     //         colour buffer, bit 1 = clear depth (FUN_0051b500)
    unsigned char  ClearColor[3];    // +0x0e9  R, G, B bytes (written at 0x0051c303, 0x0051c881,
                                     //         0x00549cfe; read by FUN_0051b500)
    // +0xec..+0xf6: zeroed by the ctor; no access in the camera code (0x00516f00..0x0051e200)
    // but the ctor's stores.
    unsigned       FieldEC;          // +0x0ec
    unsigned       FieldF0;          // +0x0f0
    unsigned char  bFlagF4;          // +0x0f4
    unsigned char  bFlagF5;          // +0x0f5
    unsigned char  bFlagF6;          // +0x0f6
    unsigned char  Pad0F7;

    // Field initialisation only; C++ construction owns the vptrs.
    void Construct();

    // FUN_0051b4b0 (non-virtual, thiscall): the space frame's per-camera preamble, called from
    // FUN_0054b000 at 0x0054b0fd right before the camera's slot 0x78. Advances the letterbox with
    // the capped frame time (FUN_0041a420), runs FUN_0051b500 when +0xe8 has either clear bit, then
    // sets this camera's viewport (FUN_00422610, "Gamecam.cpp" line 0x85) and applies it to Win3D
    // (FUN_00422950).
    void BeginFrame();

    // FUN_0051b500: +0xe8 bit 0 clears the colour buffer with ClearColor/255 (black first over the
    // whole surface when the letterbox shows and the colour is not black, then the colour inside
    // the camera's viewport), bit 1 adds the depth buffer; the first clear is always full-surface.
    void Clear();
};

// The HUDCamera ctor's first own write is the vptr at +0xf8, so FGameCamera is exactly 0xf8.
static_assert(sizeof(FGameCamera) == 0x0f8, "FGameCamera must be 0xf8");
static_assert(offsetof(FGameCamera, Watched) == 0x0c0, "GC +0x0c0");
static_assert(offsetof(FGameCamera, BaseHalfFovXDeg) == 0x0c8, "GC +0x0c8");
static_assert(offsetof(FGameCamera, LastPosition) == 0x0d0, "GC +0x0d0");
static_assert(offsetof(FGameCamera, Velocity) == 0x0dc, "GC +0x0dc");
static_assert(offsetof(FGameCamera, Flags) == 0x0e8, "GC +0x0e8");
static_assert(offsetof(FGameCamera, FieldEC) == 0x0ec, "GC +0x0ec");
static_assert(offsetof(FGameCamera, FieldF0) == 0x0f0, "GC +0x0f0");
static_assert(offsetof(FGameCamera, bFlagF4) == 0x0f4, "GC +0x0f4");
static_assert(offsetof(FGameCamera, bFlagF6) == 0x0f6, "GC +0x0f6");

// FUN_0051ca80. Adds a SECOND base (EventHandler) whose vptr lives at +0xf8, with one virtual
// (abstract table .rdata 0x005cbed8; implementations 0x0051d7f0 for HUDCamera and RearView,
// 0x0051a190 for ThirdPerson). An input sink: the camera gets first refusal on a command.
struct FHudCamera : FGameCamera, EventHandler
{
    ~FHudCamera() = default;
    char const* GetName() override;
    void Attach(void* Object) override;
    void ApplyViewport(int const* ViewRect4) override;
    bool IsFinished() override;
    bool Activate(int const* ViewRect4) override;
    void Deactivate() override;
    void Update(float DeltaSeconds) override;
    int HandleCommand70(void* Command) override;
    int HandleCommand74(void* Command) override;
    void DrawScene(unsigned LayerFlags, unsigned Param2) override;
    int GetSoundMode() override;
    bool OnEvent(unsigned MsgId) override;

    unsigned char bFlagFC;          // +0x0fc  ctor sets 0
    unsigned char bFlagFD;          // +0x0fd  only the FirstPerson ctor clears it
    unsigned char bFlagFE;          // +0x0fe  ctor sets 1
    unsigned char PadFF;

    void Construct();

    // FUN_0051b0b0 -- slot 0x68 of all four HUDCamera subclasses (FirstPerson .rdata 0x005db9ac,
    // ThirdPerson 0x005dbb7c, Turret 0x005dbd2c, RearView 0x005dbe8c): unregister the Event::Handler
    // face at +0xf8 from every command (FUN_00576190), then HUDCamera's own slot 0x68 (FUN_0051cb70).
    // Each cockpit camera overrides Deactivate with this shared body.
    void DerivedDeactivate();

    // FUN_0051db00: decay the cockpit shake amplitude DAT_00675294 by DAT_0061373c (zero under
    // 1e-4), then raise it from the watched client ship's hit flags (Gameobj +0x18c) and jump effect
    // (+0x158/+0x188).
    void UpdateShake();

    // FUN_0051dbd0: FHudCamera::UpdateShake, then with a positive amplitude two random rotations of the
    // 3x3 orientation (rows 0/2, then rows 0/1) by angles in [-amp, amp). Called by the FirstPerson
    // and RearView updates.
    void ApplyShake(Matrix* Orientation);
};

// The RearView ctor's first own write is +0x100, so FHudCamera is exactly 0x100.
static_assert(sizeof(FHudCamera) == 0x100, "FHudCamera must be 0x100");
static_assert(offsetof(FHudCamera, bFlagFC) == 0x0fc, "HC +0x0fc");
static_assert(offsetof(FHudCamera, bFlagFE) == 0x0fe, "HC +0x0fe");

// The FSpaceCamera subobject is always at offset zero.
inline FSpaceCamera* SpaceCameraOf(FGameCamera* Cam) { return Cam; }
inline FSpaceCamera* SpaceCameraOf(FHudCamera* Cam)  { return Cam; }

// The half-FOV both intermediate ctors start from: 0x420c0000.
extern const float kGameCameraDefaultHalfFovXDeg;

// Slot 0x54: the two intermediate classes' own name literals (0x0051b3b0 and 0x0051cac0).
const char* GameCameraGetName();
const char* HudCameraGetName();


// Slots 0x70 (FUN_0051da40 / LAB_0051ad20) and 0x74 (LAB_0051da70) of every HUDCamera: offer a
// command to the HUD elements unless a NeuroNet window is up. GameCamera's own 0x70/0x74
// (LAB_0053e180) return 0.
int HudCameraCommand70(void* Command);
int HudCameraCommand74(void* Command);

// The space init's store DAT_00675294 = 0 (0x0054ad78).
void ResetCameraShakeAmount();

// FUN_00516fe0 (called by the third-person solver FUN_00518f20 and the turret FUN_0051a7d0).
// Keeps M's forward column (column 2) and rebuilds the other two around Up:
// right = normalise(Up x F) unless |F . Up| >= 0.999, when M's column 0 is kept; then
// C = F x right. Out is row-major {right, C, F} by column. Out must not alias M.
void CameraOrientFromForwardAndUp(Matrix* Out, Matrix const* M, const Vector* Up);

// The IObjRW the watcher currently holds, or null. Retail spells this `watched - 8` inline at
// every use site.
void* GameCameraGetTarget(const FGameCamera* Cam);

struct CEqObj;
struct IObjRW;

// The object's CEqObj when its class has ship mask 0x503, else null. Retail inlines the chain
// at every use: the CBase sub-object (IObjRW + 0x0c, null-tested), then CBase::Object (+0x10).
CEqObj* GameCameraShipOf(IObjRW* Object);
// GameCameraShipOf on the watched object.
CEqObj* GameCameraWatchedShip(const FGameCamera* Cam);
