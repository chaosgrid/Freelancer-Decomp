// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_csys.h"      // Matrix (FaceIdea::SetBasis's 9-dword REP MOVSD source)
#include "common_goods.h"     // BaseWatcher
#include "common_ini.h"       // Vector (MoveIdea::Position / MoveIdea::Direction)

#include <cstddef>

// Forward declaration only -- SplineMoveIdea::Spline is a POINTER, so the full
// definition (common_aisteer.h) is not needed here and including it would pull
// a much heavier header into every Idea consumer. COMMON_API is repeated so the
// declaration matches common_aisteer.h's own `struct COMMON_API ICRSplineSegment`
// exactly, whichever header a translation unit happens to see first.
struct COMMON_API ICRSplineSegment;
class SystemManager;    // BaseIdea::Handle (pointer only)
struct IObjRW;          // BaseIdea::Object (pointer only)
class IBehaviorManager; // BaseIdea::Context (pointer only)

// ============================================================================
// BaseIdea family -- RTTI-identified AI "Idea" decision-object hierarchy.
// ============================================================================
//
// The Idea objects every Behavior subclass embeds as its low-level execution
// primitives (MoveIdea drives toward a point, FaceIdea orients, ...). All are
// single-inheritance children of BaseIdea (mdisp 0) except DodgeIdea /
// BreakAwayIdea, which also inherit DirectionPicker (mdisp 0x14).
//
// Vtable -> class map (each table six slots, COL at [-4]):
//   .rdata:063a0410  COL 063a9970  BaseIdea           (abstract; slot 0 is the
//                                                      pure-call stub 06391fb0)
//   .rdata:063a042c  COL 063a9a00  MoveIdea           ctor FUN_062d4e60
//   .rdata:063a0448  COL 063a99b8  StopIdea           (also installed inline at
//                                                      MoveIdea+0x64)
//   .rdata:063a047c  COL 063a9a48  MatchThrottle2Idea ctor FUN_062d5aa0
//   .rdata:063a04ac  COL 063a9a90  FaceIdea           ctor FUN_062d6750
//   .rdata:063a04c8  COL 063a9af4  DodgeIdea          (+ DirectionPicker base)
//   .rdata:063a04f8  COL 063a9b40  BreakAwayIdea      (+ DirectionPicker base)
//   .rdata:063a0524  COL 063a9b88  DodgeMissileIdea
//   .rdata:063a054c  COL 063a9bd0  IdleIdea
//   .rdata:063a0568  COL 063a9c18  SplineMoveIdea     ctor FUN_062d9bf0
//   .rdata:063a0584  COL 063a9c60  AfterburnerIdea    ctor FUN_062da500
//
// BaseIdea slots (no destructor slot: Ideas are always embedded by value, so
// BaseIdea declares no virtual destructor):
//   0 SetContext(void*,void*,void*)->bool  shared default LAB_062d7920, used by
//     IdleIdea/AfterburnerIdea/DodgeIdea/BreakAwayIdea/DodgeMissileIdea;
//     StopIdea/MoveIdea/FaceIdea override it.
//   1 Update(float)->int                    always overridden.
//   2 SyncCruise(float)                     see below.
//   3 GetKind()->short                      `MOV AX,<k>; RET` (Move 6, Face 0x10, Stop 2).
//   4 Commit()                              thiscall, no args; real logic in MoveIdea
//     (LAB_062d56b0) / FaceIdea (LAB_062d74e0), ICF no-op in StopIdea.
//   5 Slot5(void*,void*)                    see below.
//
// BaseIdea's own data is +0x04..+0x10 (size 0x14): AfterburnerIdea's ctor and
// MoveIdea's embedded StopIdea stop at +0x11. The BaseWatcher at +0x14 is
// MoveIdea's / FaceIdea's own first field.
// ============================================================================

class BaseIdea
{
public:
    BaseIdea();

    // Slot 0, LAB_062d7920: false if InObject or InHandle is null; else stores
    // Handle/Context/Object, sets Attached = 1 and returns true.
    virtual bool SetContext(IObjRW* InObject, SystemManager* InHandle, IBehaviorManager* InContext);

    // Slot 1.
    virtual int Update(float DeltaTime) = 0;

    // Slot 2. Every table but MatchThrottle2Idea's (LAB_062d5bf0) holds
    // .text:062ed730, a bare `RET 4`. The one caller found, TrailBehavior slot 8
    // (.text:062caa4a), pushes the behavior's DeltaTime. Retail has no name for it.
    virtual void SyncCruise(float DeltaTime) { (void)DeltaTime; }

    // Slot 3. Kind tag.
    virtual short GetKind() const = 0;

    // Slot 4. No arguments.
    virtual void Commit() = 0;

    // Slot 5. All eleven tables hold .text:0632c0c0, `XOR AL,AL; RET 8`. No caller
    // found; retail has no name for it.
    virtual bool Slot5(void* Arg1, void* Arg2) { (void)Arg1; (void)Arg2; return false; }

    // FUN_062d4e50, thiscall, RET 4: `MOV EAX,[ECX+0xc];
    // MOV ECX,[EAX+0xc]; MOV [ECX+0x64],arg` -- Context is the IBehaviorManager, its +0x0C
    // the PathfindManager, whose AvoidExcludeId receives the id. No null tests. Called on the
    // MoveIdea members of DockBehavior (.text:062c3822/062c3834), BuzzBehavior
    // (FUN_062c6170) and StrafeBehavior (FUN_062ca310). Retail name unknown.
    void PublishTargetId(unsigned int Id);

    IObjRW* Object;            // +0x04. SetContext()'s 1st arg (ctor-zeroed).
    SystemManager* Handle;      // +0x08. SetContext()'s 2nd arg (ctor-zeroed): the owner's
                                // SystemManager, the behaviors' +0x24 SysMgr.
    IBehaviorManager* Context;    // +0x0c. SetContext()'s 3rd arg. Retail's ctors leave it
                                   // uninitialized; the port zeroes it.
    unsigned char Attached;         // +0x10. Ctor-zeroed; SetContext() sets 1
                                     // on success.
    unsigned char Pad11[3];          // +0x11, alignment only.
};
static_assert(offsetof(BaseIdea, Object) == 0x04, "BaseIdea::Object offset");
static_assert(offsetof(BaseIdea, Handle) == 0x08, "BaseIdea::Handle offset");
static_assert(offsetof(BaseIdea, Context) == 0x0c, "BaseIdea::Context offset");
static_assert(offsetof(BaseIdea, Attached) == 0x10, "BaseIdea::Attached offset");
static_assert(sizeof(BaseIdea) == 0x14, "BaseIdea size (own data ends at +0x14 -- see class comment)");

// ============================================================================
// StopIdea : public BaseIdea -- Common.dll RTTI name ".?AVStopIdea@@".
// ============================================================================
// No fields beyond BaseIdea (MoveIdea embeds one at +0x64, ending at +0x78).
// Slot 0 LAB_062d76a0 `MOV AL,1; RET 0xc` (ICF-shared with NullBehavior::SetContext);
// slot 3 LAB_062da560 returns 2; slot 4 is an ICF-folded no-op.
// ============================================================================
class StopIdea : public BaseIdea
{
public:
    StopIdea();

    // Slot 0: returns true, touches nothing.
    bool SetContext(IObjRW* InObject, SystemManager* InHandle, IBehaviorManager* InContext) override;

    // Slot 1, LAB_0632b010 (`XOR EAX,EAX; RET 4`, ICF-folded): returns 0.
    int Update(float DeltaTime) override;

    short GetKind() const override { return 2; }

    void Commit() override {}
};

// ============================================================================
// AfterburnerIdea : public BaseIdea -- Common.dll RTTI name
// ".?AVAfterburnerIdea@@". Ctor FUN_062da500 zeroes BaseIdea's span only (no own
// fields); also embedded as DockBehavior::GotoDirectiveHandle::Idea.
// Slot 0 is BaseIdea's default; slot 3 (LAB_062da560) is StopIdea's, returning 2.
// ============================================================================
class AfterburnerIdea : public BaseIdea
{
public:
    AfterburnerIdea();

    // Slot 1, FUN_062da520: Object null -> 3; else SetThrustersActive(true) and
    // SetThrottle(1.0f, 0) on Handle->Thrust; returns 0.
    int Update(float DeltaTime) override;

    short GetKind() const override { return 2; }

    // Slot 4, LAB_062da570: SetThrustersActive(false). No Handle null test (retail).
    void Commit() override;
};

// ============================================================================
// IdleIdea : public BaseIdea -- RTTI COL 063a9bd0 at [vtable-4] of .rdata:063a054c
// (see the table at the head of this header). Ctor FUN_062d9b10: zeroes
// BaseIdea's +0x04/+0x08/+0x10 and installs 063a054c -- no fields of its own (0x14 bytes).
// Embedded at IdleBehavior+0x2c and DelayBehavior+0x2c (FUN_062d0a10). Vtable:
//   0 LAB_062d7920 BaseIdea::SetContext      1 LAB_062d9b30 Update (below)
//   2 062ed730 `RET 4`                        3 LAB_062d9be0 `MOV AX,0x12; RET`
//   4 062a8920 bare `RET` (Commit)           5 0632c0c0 `XOR AL,AL; RET 8`
// ============================================================================
class IdleIdea : public BaseIdea
{
public:
    IdleIdea();

    // LAB_062d9b30, RET 4: Object null -> 3; else on Handle+0x18 (the
    // ThrustManager): FUN_062e89a0(0, 2), FUN_062e89a0(0, 0), FUN_062e8c20(&zero),
    // FUN_062e8ce0(&zero), FUN_062e9030(0), FUN_062e9120(0); returns 0.
    int Update(float DeltaTime) override;

    // Slot 3, LAB_062d9be0: `MOV AX,0x12; RET`.
    short GetKind() const override { return 0x12; }

    // Slot 4, .text:062a8920 (ICF-folded bare RET).
    void Commit() override {}
};
static_assert(sizeof(IdleIdea) == 0x14, "IdleIdea adds no fields to BaseIdea");

// MoveIdea::Mode (+0x34), selected by SetMode. Picks the thrust equipment the idea throttles
// with (SetThrottleScale: ThrustEquipEngine / ThrustEquipTradelane / ThrustEquipCruise) and
// which ThrustManager throttle Update drives (cruise for 2, engine for the rest).
enum EMoveMode
{
    kMoveModeEngine = 0,
    kMoveModeTradelane = 1,
    kMoveModeCruise = 2,
};

// MoveIdea::TargetKind48 (+0x48), stamped by the four target setters.
enum EMoveTargetKind
{
    kMoveTargetNone = 0,
    kMoveTargetObject = 1,      // SetWatchObject
    kMoveTargetPoint = 2,       // SetPosition
    kMoveTargetDirection = 3,   // SetDirection / SetDirectionFrom
};

// MoveIdea::StopStyle (+0x44, SetField44), the stopping-speed lookahead Update uses once the
// target is within reach (see StopStyle below). GotoBehavior stores `bFlyThrough ? 2 : 0`.
enum EMoveStopStyle
{
    kMoveStopThrottleCut = 0,   // SetThrottle(0), lookahead FUN_062e5ab0
    kMoveStopIdea = 1,          // the embedded StopIdea, lookahead FUN_062e58f0
    kMoveStopFlyThrough = 2,    // no lookahead, nothing stopped
};

// ============================================================================
// MoveIdea : public BaseIdea -- ".?AVMoveIdea@@", ctor FUN_062d4e60, 0x78 bytes.
// ============================================================================
// Ctor: zeroes Watcher (+0x14), Position/Direction (+0x1c/+0x28), Mode (+0x34),
// +0x44, TargetKind48, Offset4c, +0x58, +0x5c/+0x60; +0x38 = pi, +0x3c = 200.0f,
// +0x40 = 1.0f, +0x59 = 1; builds the StopIdea at +0x64 inline (vptr 063a0448,
// rel+0x0c untouched).
//
// Slot 0, LAB_062d4f20: BaseIdea's guard and stores, then
//   .text:062d4f55 Object->get_angular_speed_XY(0.0f, 1.0f) (vt+0xf0) -> +0x60
//   .text:062d4f68 Object->get_speed(1.0f, 1)              (vt+0xd8) -> +0x5c
//   .text:062d4f83 EmbeddedStop.SetContext(Object, Handle, Context)
//
// Slot 1, LAB_062d4fa0 (596 instructions): the mid-body `PUSH EBX` (062d50e5) /
// `POP EBX` (062d5325) shift every later [ESP+disp] by four. Handle+0x14fc is the
// FAiTarget block: its (radius, centre) pair is refreshed at most once per tick
// through the gate byte Handle[0x157a] (062d4fc8/062d4fed), and its +0x84 is the
// owner's IObjInspect*. Returns 0 steering, 1 arrived (Commit() called), 2 cruise
// gate refused, 3 no Object.
//
// Slot 3, LAB_062d6540 (`MOV AX,0x6; RET`), shared with MatchThrottle2Idea's slot 3.
//
// Slot 4, LAB_062d56b0:
//   Handle->Thrust.SetNudgeDirection(zero)                       (FUN_062e8ce0)
//   if (Mode != 1 && 0 <= StopStyle && StopStyle <= 1)           (signed JL/JG)
//       SetThrottle(0.0f, 0); SetThrottle(0.0f, 2);              (FUN_062e89a0)
//   then [[Context+0xc]]+0x64 = 0 and +0x200..+0x208 = {0,0,0}.
//
// ----------------------------------------------------------------------------
// Non-virtual setters and their /OPT:ICF folds. Retail calls them by address,
// and most share an address with an unrelated class's identical method, so
// Ghidra labels the call sites with the other owner's name. Ownership comes from
// the receivers: GotoBehavior+0x2c and TrailBehavior+0x148 are MoveIdeas (their
// ctors call FUN_062d4e60 on them), and TrailBehavior's block at
// .text:062cb03a-062cb068 calls five setters in a row on EBP+0x148.
//   .text:063275f0 `MOV [ECX+0x38],EAX; RET 4`  also TextRenderContext::set_origin_h
//   .text:062a66b0 `MOV [ECX+0x3c],EAX; RET 4`  also DeviceTRC::bind_to_dc,
//                                              CAsteroidField::set_system
//   .text:062a33e0 `MOV [ECX+0x40],EAX; RET 4`  also CAsteroidCube::set_state
//   .text:062d88b0 `MOV [ECX+0x44],EAX; RET 4`  21 call sites, linker glue
//   .text:062d5a10 `MOV [ECX+0x58],AL;  RET 4`
// ============================================================================
class MoveIdea : public BaseIdea
{
public:
    MoveIdea();

    // Slot 0, LAB_062d4f20.
    bool SetContext(IObjRW* InObject, SystemManager* InHandle, IBehaviorManager* InContext) override;

    // Slot 1, LAB_062d4fa0. Returns 0/1/2/3 (see above).
    int Update(float DeltaTime) override;

    short GetKind() const override { return 6; }

    // Slot 4, LAB_062d56b0.
    void Commit() override;

    // ---- Non-virtual setters (see the fold table above). ----

    // FUN_062d5840, RET 4. TargetKind48 = 2, Position = Pos, then mirrors
    // Position into the owning slot array at `[[Context+0xc]]+0x200..+0x208`.
    void SetPosition(Vector const& Pos);

    // FUN_062d5890, RET 4. TargetKind48 = 3, Direction = Dir, then renormalizes
    // Direction in place (no zero-length guard, matching retail).
    void SetDirection(Vector const& Dir);

    // FUN_062d58f0, RET 4. TargetKind48 = 3, Direction = -(Src+0x08, Src+0x14,
    // Src+0x20) -- a foreign object's basis-vector column -- then renormalizes.
    void SetDirectionFrom(Matrix const* Src);

    // FUN_062d5760, RET 4. TargetKind48 = 1 and Watcher.set(Object+8) when
    // Object is non-null, else TargetKind48 = 0 and Watcher.set(nullptr). Then
    // mirrors the watched object's id into the slot array's +0x64 and its
    // position into +0x200..+0x208 (or zeroes both when cleared).
    void SetWatchObject(void const* Object);

    // FUN_062d5980, RET 4. The Mode 0/1/2 transition machine -- see the .cpp
    // for the exact branch table. Ends with Mode = NewMode on every path.
    void SetMode(int NewMode);

    // Folded at .text:063275f0 (= TextRenderContext::set_origin_h).
    // MaxThrustBearing = Value. Most call sites pass 0x40490fdb (pi, the ctor
    // default); DockBehavior passes 0x3eb2b8c3 at .text:062c4400.
    void SetAngle38(float Value);

    // Folded at .text:062a66b0 (= DeviceTRC::bind_to_dc). ArrivalRange =
    // Value. Retail passes a dword straight through, so callers holding the
    // value as raw bits must reinterpret rather than convert.
    void SetRange3c(float Value);

    // Folded at .text:062a33e0 (= CmnAsteroid::CAsteroidCube::set_state).
    // Throttle = Value.
    void SetScale40(float Value);

    // FUN_062d5a20, RET 4. Throttle = Object->get_projected_throttle(Value, T)
    // (vt+0xd4 via FUN_062e5b70), T chosen by Mode: 0 -> 1 (Engine), 1 -> 3
    // (Tradelane), 2 -> 4 (Cruise). Retail's default arm forwards Value's raw float
    // bits as the enum; reproduced (see the .cpp).
    void SetThrottleScale(float Value);

    // Folded at .text:062d88b0. StopStyle = Value (callers pass 0 and 2).
    void SetField44(int Value);

    // Folded at .text:062d5a10. Field58 = the raw byte (GotoBehavior::Slot10
    // passes op+0x59 unnormalised).
    void SetEnabled(unsigned char Value);

    // .text:062d5a70: bKillEngineOffBearing = the raw byte. One call site,
    // .text:062c43f4, on DockBehavior's MoveIdea (EDI = ESI+0x30 at .text:062c4380).
    void SetField59(unsigned char Value);

    // ---- Fields. Field58 has no reader in MoveIdea's code (062d4e60-062d5a80). ----
    BaseWatcher Watcher;        // +0x14, ends +0x1c.
    Vector Position;              // +0x1c, ends +0x28. Written by SetPosition()
                                   // (which also stamps TargetKind48 = 2) and
                                   // mirrored into the owning slot array.
    Vector Direction;               // +0x28, ends +0x34. Written by
                                     // SetDirection()/SetDirectionFrom() (both
                                     // stamp TargetKind48 = 3), always kept
                                     // normalized by those setters.
    int Mode;                         // +0x34. Ctor-zeroed. SetMode()'s 0/1/2
                                       // transition state; Commit() compares it
                                       // against 1.
    // +0x38. Ctor default PI (0x40490fdb); SetAngle38().
    // Update() FCOMPs BearingAngleToPoint(Object, Goal) against it (062d5422,
    // 062d55fe): strictly below -> thrust toward Goal (SetThrottle at 062d543a /
    // 062d5667); otherwise cruise is dropped (062d544e) or the throttle is cut to 0
    // and the engine switched per bKillEngineOffBearing (062d5683-062d569f).
    float MaxThrustBearing;
    // +0x3c. Ctor default 200.0f; SetRange3c(). Update()
    // commits and returns 1 when the squared target distance is below its square
    // (062d50b4-062d50dc), and adds it to the stopping lookahead at 062d5340.
    float ArrivalRange;
    // +0x40. Ctor default 1.0f; SetScale40(), and
    // SetThrottleScale() stores get_projected_throttle into it. Update() passes it as
    // ThrustManager::SetThrottle's throttle (062d5435-062d543a cruise, 062d560b the
    // cap of the engine throttle) and scales the nudge direction by it (062d5511).
    float Throttle;
    // +0x44. Ctor-zeroed; SetField44(). How Update() slows down
    // once the stopping lookahead reaches the target while still moving:
    // 0 = throttle cut (SetThrottle(0), lookahead FUN_062e5ab0), 1 = EmbeddedStop
    // (StopIdea) Update, lookahead FUN_062e58f0; 2 (any other) = no
    // lookahead, nothing done, and Mode 2 is not dropped near the target
    // (062d51e5). Commit() zeroes both throttles only for 0..1 (062d56e4).
    int StopStyle;
    int TargetKind48;                           // +0x48. Ctor-zeroed. 0 = nothing,
                                                 // 1 = watched object, 2 = fixed point,
                                                 // 3 = direction vector -- stamped by
                                                 // the four target setters above.
    Vector Offset4c;                              // +0x4c, ends +0x58. Ctor-zeroed.
                                                   // An offset in the owner's frame:
                                                   // Update() rotates it into world
                                                   // space (TransformMatrix Source,
                                                   // .text:062d5023) and adds it to
                                                   // the cached nav centre.
    unsigned char Field58;                              // +0x58. Ctor-zeroed byte.
                                                         // SetEnabled(). No reader in
                                                         // MoveIdea's own code.
    // +0x59. Ctor default 1; SetField59(). Read only at 062d5692:
    // when the bearing is not below MaxThrustBearing, Update() calls
    // ThrustManager::SetEngineActive(bKillEngineOffBearing == 0).
    unsigned char bKillEngineOffBearing;
    unsigned char Pad5a[0x5c - 0x5a];                       // +0x5a, alignment only.
    float MaxSpeed5c;                                         // +0x5c. Ctor-zeroed;
                                                               // SetContext() fills it from
                                                               // Object->get_speed(1.0f, 1).
    float MaxAngularSpeedXY60;                                  // +0x60. Ctor-zeroed;
                                                                 // SetContext() fills it from
                                                                 // Object->
                                                                 // get_angular_speed_XY(0,1).
    StopIdea EmbeddedStop;         // +0x64, ends +0x78.
};
static_assert(offsetof(MoveIdea, Watcher) == 0x14, "MoveIdea::Watcher offset");
static_assert(offsetof(MoveIdea, Position) == 0x1c, "MoveIdea::Position offset");
static_assert(offsetof(MoveIdea, Direction) == 0x28, "MoveIdea::Direction offset");
static_assert(offsetof(MoveIdea, Mode) == 0x34, "MoveIdea::Mode offset");
static_assert(offsetof(MoveIdea, MaxThrustBearing) == 0x38, "MoveIdea::MaxThrustBearing offset");
static_assert(offsetof(MoveIdea, ArrivalRange) == 0x3c, "MoveIdea::ArrivalRange offset");
static_assert(offsetof(MoveIdea, Throttle) == 0x40, "MoveIdea::Throttle offset");
static_assert(offsetof(MoveIdea, StopStyle) == 0x44, "MoveIdea::StopStyle offset");
static_assert(offsetof(MoveIdea, TargetKind48) == 0x48, "MoveIdea::TargetKind48 offset");
static_assert(offsetof(MoveIdea, Offset4c) == 0x4c, "MoveIdea::Offset4c offset");
static_assert(offsetof(MoveIdea, Field58) == 0x58, "MoveIdea::Field58 offset");
static_assert(offsetof(MoveIdea, bKillEngineOffBearing) == 0x59, "MoveIdea::bKillEngineOffBearing offset");
static_assert(offsetof(MoveIdea, MaxSpeed5c) == 0x5c, "MoveIdea::MaxSpeed5c offset");
static_assert(offsetof(MoveIdea, MaxAngularSpeedXY60) == 0x60, "MoveIdea::MaxAngularSpeedXY60 offset");
static_assert(offsetof(MoveIdea, EmbeddedStop) == 0x64, "MoveIdea::EmbeddedStop offset");
static_assert(sizeof(MoveIdea) == 0x78, "MoveIdea size 0x78");

// ============================================================================
// MatchThrottle2Idea : public BaseIdea -- RTTI COL 063a9a48 at [vtable-4] of
// .rdata:063a047c names ".?AVMatchThrottle2Idea@@"; its ClassHierarchyDescriptor
// (063a9a38, numBaseClasses 2) is [MatchThrottle2Idea, BaseIdea]. Ctor FUN_062d5aa0,
// sizeof 0x5c. Embedded at TrailBehavior+0x2c and at +0x34 of the FUN_062cb2b0
// family (FollowBehavior, FormationBehavior).
//
// Its layout is not MoveIdea's: the ctor never writes +0x28, stores pi/4 as a float
// at +0x34, and nothing reads +0x48. MoveIdea's setters FUN_062d5760/5840/5890/58f0/
// 5980 are not its methods (every call site's receiver is a MoveIdea).
//
// The class matches the watched ship's throttle (and optionally its cruise and
// nudge): Update measures the distance and closing speed to the watched object,
// converts them through the owner's thrust model (IObjInspect get_projected_throttle,
// get_speed, get_time_to_accelerate, get_distance_travelled) into a throttle and
// drives the ThrustManager with it.
//
// Vtable .rdata:063a047c, six slots:
//   0 LAB_062d5b50 SetContext   1 LAB_062d5ce0 Update   2 LAB_062d5bf0 Slot2
//   3 LAB_062d6540 GetKind (6, folded with MoveIdea's)  4 LAB_062d6550 Commit
//   5 .text:0632c0c0 BaseIdea::Slot5
// Non-virtual members: FUN_062d65f0 SetWatchAndPosition, FUN_062d6670
// SetWatchAndDistance, FUN_062d6700..062d6740 the five one-store setters, and
// BaseIdea::PublishTargetId (FUN_062d4e50, called on TrailBehavior+0x2c).
// ============================================================================
class MatchThrottle2Idea : public BaseIdea
{
public:
    // FUN_062d5aa0 (.text:062d5aa0-062d5b48).
    MatchThrottle2Idea();

    // Slot 0, LAB_062d5b50 (RET 0xc): rejects a null Object or Handle; otherwise
    // stores the three, Attached = 1, and seeds Speed4c / Speed50 from the
    // owner's thrust model (cruise and engine).
    bool SetContext(IObjRW* InObject, SystemManager* InHandle, IBehaviorManager* InContext) override;

    // Slot 1, LAB_062d5ce0 (RET 4, 677 instructions).
    int Update(float DeltaTime) override;

    // Slot 2, LAB_062d5bf0 (RET 4, argument unread): engage or drop cruise to
    // follow the watched ship.
    void SyncCruise(float DeltaTime) override;

    // Slot 3, LAB_062d6540 `MOV AX,0x6; RET` -- the same address as MoveIdea's.
    short GetKind() const override { return 6; }

    // Slot 4, LAB_062d6550 (RET 0).
    void Commit() override;

    // FUN_062d65f0 (RET 8): watch Object, aim at Point, Distance28 = 0, and mirror
    // the watched object's position into PathfindManager::CachedTargetPosition.
    void SetWatchAndPosition(void const* Object, Vector const& Point);

    // FUN_062d6670 (RET 8): watch Object, Position = 0, Distance28 = Distance, and
    // the same mirror.
    void SetWatchAndDistance(void const* Object, float Distance);

    // FUN_062d6700 / 062d6710 / 062d6720 / 062d6730: `MOV AL,[ESP+4]; MOV
    // [ECX+0x38+n],AL; RET 4` -- the raw argument byte is stored, not normalised
    // (FollowBehavior::Slot27 passes Flag28c's byte through at 062cd205-062cd210).
    void SetFlag38(unsigned char Value) { Flag38 = Value; }
    void SetFlag39(unsigned char Value) { Flag39 = Value; }
    void SetFlag3a(unsigned char Value) { Flag3a = Value; }
    void SetFlag3b(unsigned char Value) { Flag3b = Value; }

    // FUN_062d6740: `MOV EAX,[ESP+4]; MOV [ECX+0x58],EAX; RET 4`.
    void SetCatchUpRange(float Range) { CatchUpRange58 = Range; }

    // .text:063275d0 `MOV [ECX+0x34],EAX; RET 4` (ICF-folded; one call site on this
    // class, TrailBehavior::SetContext at .text:062ca5cf with pi).
    void SetMaxBearing(float Angle) { MaxBearing34 = Angle; }

    // ---- own fields (+0x14..+0x5b) ----
    BaseWatcher Watcher;          // +0x14  the ship being matched
    Vector Position;              // +0x1c  Update: the watched centre; FUN_062d65f0: the point
    float Distance28;             // +0x28  standoff distance; NOT written by the ctor
    float Speed2c;                // +0x2c  ctor 0; Update: Speed4c / Speed50 or a travel distance
    float ThrottleBias30;         // +0x30  ctor 0; Update's throttle correction
    float MaxBearing34;           // +0x34  ctor pi/4 (0x3f490fdb)
    unsigned char Flag38;         // +0x38  ctor 1: query the watched ship's cruise state
    unsigned char Flag39;         // +0x39  ctor 0: Commit keeps cruise when set
    unsigned char Flag3a;         // +0x3a  ctor 0: match the watched ship's nudge
    unsigned char Flag3b;         // +0x3b  ctor 0: afterburner allowed
    unsigned char Flag3c;         // +0x3c  ctor 1: catch up beyond CatchUpRange58
    unsigned char Flag3d;         // +0x3d  catching up this tick (Update writes it)
    unsigned char Pad3e[2];
    float ClosingFloor40;         // +0x40  ctor -200.0f
    float ClosingBias44;          // +0x44  ctor -0.5f
    float Field48;                // +0x48  ctor 0.05f (0x3d4ccccd); no reader in this class's code
    float Speed4c;                // +0x4c  SetContext: cruise-type speed + 300
    float Speed50;                // +0x50  SetContext: engine-type speed + 50
    float ClosingWindow54;        // +0x54  ctor 5.0f
    float CatchUpRange58;         // +0x58  ctor 500.0f; FUN_062d6740
};
static_assert(offsetof(MatchThrottle2Idea, Watcher) == 0x14, "MatchThrottle2Idea::Watcher offset");
static_assert(offsetof(MatchThrottle2Idea, Position) == 0x1c, "MatchThrottle2Idea::Position offset");
static_assert(offsetof(MatchThrottle2Idea, Distance28) == 0x28, "MatchThrottle2Idea::Distance28 offset");
static_assert(offsetof(MatchThrottle2Idea, Speed2c) == 0x2c, "MatchThrottle2Idea::Speed2c offset");
static_assert(offsetof(MatchThrottle2Idea, ThrottleBias30) == 0x30, "MatchThrottle2Idea::ThrottleBias30 offset");
static_assert(offsetof(MatchThrottle2Idea, MaxBearing34) == 0x34, "MatchThrottle2Idea::MaxBearing34 offset");
static_assert(offsetof(MatchThrottle2Idea, Flag38) == 0x38, "MatchThrottle2Idea::Flag38 offset");
static_assert(offsetof(MatchThrottle2Idea, Flag3d) == 0x3d, "MatchThrottle2Idea::Flag3d offset");
static_assert(offsetof(MatchThrottle2Idea, ClosingFloor40) == 0x40, "MatchThrottle2Idea::ClosingFloor40 offset");
static_assert(offsetof(MatchThrottle2Idea, Field48) == 0x48, "MatchThrottle2Idea::Field48 offset");
static_assert(offsetof(MatchThrottle2Idea, Speed4c) == 0x4c, "MatchThrottle2Idea::Speed4c offset");
static_assert(offsetof(MatchThrottle2Idea, ClosingWindow54) == 0x54, "MatchThrottle2Idea::ClosingWindow54 offset");
static_assert(offsetof(MatchThrottle2Idea, CatchUpRange58) == 0x58, "MatchThrottle2Idea::CatchUpRange58 offset");
static_assert(sizeof(MatchThrottle2Idea) == 0x5c, "MatchThrottle2Idea size (operator-new-free embed; next member at +0x5c in every owner)");

// FaceIdea::TargetKind (+0xa0), the aim-mode tag the target setters stamp and Update switches on.
enum EFaceTargetKind
{
    kFaceTargetNone = 0,
    kFaceTargetObject = 1,      // SetWatchObject
    kFaceTargetPoint = 2,       // SetPosition
    kFaceTargetDirection = 3,   // SetDirection
    kFaceTargetBasis = 4,       // SetBasis
    kFaceTargetAxis = 5,        // SetAxis
};

// ============================================================================
// FaceIdea : public BaseIdea -- ".?AVFaceIdea@@", ctor FUN_062d6750, 0xc0 bytes.
// ============================================================================
// Ctor: zeroes Watcher, Direction28, ZeroBlock34 (REP STOSD, 9 dwords), Field64,
// TargetKind; +0x8c = 0.098175f, +0x90 = FUN_062e5ba0(Object, 1.0f) (0.0f, Object
// still null), +0x94 = 1.0f, +0x98 = 0.61f, +0x9c = pi/16, +0xa8 = pi,
// +0xbc = 0.976563f. Other bytes are left uninitialized.
//
// Slot 0 LAB_062d6820, slot 1 LAB_062d68b0 (common_faceidea.cpp), slot 3
// LAB_062d74d0 (0x10), slot 4 LAB_062d74e0: Handle->Thrust.SetAxisThrottle(zero).
//
// Setters: GotoBehavior+0xa4 and BuzzBehavior+0xa4 are FaceIdeas (FUN_062d6750 on
// them), and GotoBehavior::Update calls FUN_062d75e0/7560/7510/7610/9490 on +0xa4.
// Each tag setter writes +0xa0 (TargetKind, the analogue of MoveIdea::TargetKind48):
//   FUN_062d7510  tag 1 / 0   watch an object      (Watcher at +0x14)
//   FUN_062d7560  tag 2       face a fixed point   (Vector at +0x1c)
//   FUN_062d7590  tag 3       face a direction     (Vector at +0x28)
//   FUN_062d75c0  tag 4       face along a basis   (9 dwords at +0x34)
//   FUN_062d75e0  tag 5       face an axis         (Vector at +0x58)
//   FUN_062d7640  (no tag)    optional up-vector   (flag +0xad, Vector +0xb0)
//   FUN_062d7610  (no tag)    throttle scale       (+0x90 raw, +0x94 derived)
// .text:062d93c0 (+0x8c store) and .text:062d9490 (+0x94 store) are one-instruction
// setters DodgeIdea / BreakAwayIdea may share by ICF (their ctors write those
// offsets); the call sites at .text:062c84dd/062c84ee/062c84ff are probably
// BreakAwayIdea's. Each caller has to prove its own receiver.
// ============================================================================
class FaceIdea : public BaseIdea
{
public:
    FaceIdea();

    // Slot 0, LAB_062d6820: BaseIdea's binding, then AngleWeight98 from the
    // object's angular speed / distance (FUN_062e5b20 / FUN_062e5950), a discarded
    // FUN_062e5b50 query, AngleWeight9c = pi/16 and bUpValid = 0.
    bool SetContext(IObjRW* InObject, SystemManager* InHandle, IBehaviorManager* InContext) override;

    // Slot 1, LAB_062d68b0 (common_faceidea.cpp; helpers shared with
    // DodgeIdea::Update in common_aisteer.cpp).
    int Update(float DeltaTime) override;

    short GetKind() const override { return 0x10; }

    // Slot 4, LAB_062d74e0.
    void Commit() override;

    // ---- Non-virtual setters (RET 4; see the table above). ----

    // FUN_062d7510. TargetKind = 1 and Watcher.set(Object + 8) when Object is
    // non-null, else TargetKind = 0 and Watcher.set(nullptr). No slot-array mirror.
    void SetWatchObject(void const* Object);

    // FUN_062d7560. TargetKind = 2, Position1c = Point.
    void SetPosition(Vector const& Point);

    // FUN_062d7590. TargetKind = 3, Direction28 = Dir; not renormalized (unlike
    // MoveIdea::SetDirection).
    void SetDirection(Vector const& Dir);

    // FUN_062d75c0. TargetKind = 4, then `MOV ECX,0x9; REP MOVSD` from Basis into
    // ZeroBlock34.
    void SetBasis(Matrix const& Basis);

    // FUN_062d75e0. TargetKind = 5, Vector58 = Axis.
    void SetAxis(Vector const& Axis);

    // FUN_062d7610. Field90 = Value (raw) and TurnThrottle =
    // Object->get_projected_axis_throttle_XY(Value) (FUN_062e5ba0) -- the reverse of
    // the ctor, which puts the derived value in +0x90 and 1.0f in +0x94 (retail).
    void SetThrottleScale(float Value);

    // FUN_062d7640. If every component of Up equals 0.0f (`TEST AH,0x44; JP`),
    // bUpValid = 0 and nothing else is written; otherwise bUpValid = 1, UpB0 = Up.
    void SetUpVector(Vector const& Up);

    // .text:062d93c0: FacingTolerance = Value. Possible ICF fold (see above).
    void SetAngleWeight8c(float Value);

    // .text:062d9490: TurnThrottle = Value. Receiver proved FaceIdea at
    // .text:062fe34f; same fold caveat.
    void SetScale94(float Value);

    // ---- Fields. ----
    BaseWatcher Watcher;             // +0x14, ends +0x1c.
    Vector Position1c;                 // +0x1c, ends +0x28. SetPosition(), tag 2.
    Vector Direction28;                  // +0x28, ends +0x34. Ctor-zeroed (three
                                          // dwords). SetDirection(), tag 3.
    Matrix ZeroBlock34;                    // +0x34..+0x57. Ctor-zeroed; SetBasis(), tag 4.
    Vector Vector58;                          // +0x58, ends +0x64. SetAxis(), tag 5.
    // +0x64. Ctor-zeroed byte. Update() reads it twice: case 1 aims along the
    // watched node's up only when it is set (062d6959), case 2 takes the floats at
    // +0x6c/+0x78/+0x84 as the up hint when it is set (062d6c53). No writer other
    // than the ctor was found, so the meaning stays open.
    unsigned char Field64;
    unsigned char Opaque65[0x8c - 0x65];          // +0x65, unconfirmed.
    // +0x8c. Ctor default 0.098175f (0x3dc90fdb);
    // SetAngleWeight8c(). Update() returns 1 (facing reached) when RemainingAngle is
    // below it, 0 otherwise (062d7483-062d7499).
    float FacingTolerance;
    float Field90;                                         // +0x90. Ctor: FSTP of
                                                            // FUN_062e5ba0(Object, 1.0f)
                                                            // (0.0f, Object is null);
                                                            // SetThrottleScale() stores its
                                                            // raw argument. Never read by
                                                            // Update (062d68b0-062d74a9).
    // +0x94. Ctor default 1.0f; SetScale94(), and
    // SetThrottleScale() stores get_projected_axis_throttle_XY into it. Update() caps
    // the length of the axis-throttle vector it hands to ThrustManager::
    // SetAxisThrottle at this value (062d7385-062d7406).
    float TurnThrottle;
    float AngleWeight98;                                       // +0x98. Ctor default 0.61f
                                                                // (0x3f1c28f6).
    float AngleWeight9c;                                         // +0x9c. Ctor default
                                                                  // 0.19634955f == pi/16 (0x3e490fdb).
    // +0xa0. Ctor-zeroed. The 0..5 aim-mode tag every setter
    // above stamps and Update() switches on (062d68db) -- FaceIdea's analogue of
    // MoveIdea::TargetKind48.
    int TargetKind;
    unsigned char OpaqueA4[0xa8 - 0xa4];                             // +0xa4, unconfirmed.
    // +0xa8. Ctor default PI (0x40490fdb). Update() passes
    // its address as TurnHelper::get_angular_throttle's OutAngle (062d7083), i.e. the
    // rotation angle still between the current orientation and the target frame.
    float RemainingAngle;
    unsigned char PadAc;                                                 // +0xac, unconfirmed.
    unsigned char bUpValid;                                               // +0xad. SetUpVector()
                                                                           // writes 1 when Up is
                                                                           // any non-zero vector,
                                                                           // 0 otherwise. NOT
                                                                           // ctor-initialized in
                                                                           // retail.
    unsigned char PadAe[0xb0 - 0xae];                                       // +0xae, alignment.
    Vector UpB0;                                                              // +0xb0, ends +0xbc.
                                                                               // SetUpVector(), only
                                                                               // written when
                                                                               // bUpValid is set.
    float ThresholdBc;                                                     // +0xbc. Ctor
                                                                            // default 0.976563f
                                                                            // (0x3f7ae148).
};
static_assert(offsetof(FaceIdea, Watcher) == 0x14, "FaceIdea::Watcher offset");
static_assert(offsetof(FaceIdea, Position1c) == 0x1c, "FaceIdea::Position1c offset");
static_assert(offsetof(FaceIdea, Direction28) == 0x28, "FaceIdea::Direction28 offset");
static_assert(offsetof(FaceIdea, ZeroBlock34) == 0x34, "FaceIdea::ZeroBlock34 offset");
static_assert(offsetof(FaceIdea, Vector58) == 0x58, "FaceIdea::Vector58 offset");
static_assert(offsetof(FaceIdea, Field64) == 0x64, "FaceIdea::Field64 offset");
static_assert(offsetof(FaceIdea, FacingTolerance) == 0x8c, "FaceIdea::FacingTolerance offset");
static_assert(offsetof(FaceIdea, Field90) == 0x90, "FaceIdea::Field90 offset");
static_assert(offsetof(FaceIdea, TurnThrottle) == 0x94, "FaceIdea::TurnThrottle offset");
static_assert(offsetof(FaceIdea, AngleWeight98) == 0x98, "FaceIdea::AngleWeight98 offset");
static_assert(offsetof(FaceIdea, AngleWeight9c) == 0x9c, "FaceIdea::AngleWeight9c offset");
static_assert(offsetof(FaceIdea, TargetKind) == 0xa0, "FaceIdea::TargetKind offset");
static_assert(offsetof(FaceIdea, RemainingAngle) == 0xa8, "FaceIdea::RemainingAngle offset");
static_assert(offsetof(FaceIdea, bUpValid) == 0xad, "FaceIdea::bUpValid offset");
static_assert(offsetof(FaceIdea, UpB0) == 0xb0, "FaceIdea::UpB0 offset");
static_assert(offsetof(FaceIdea, ThresholdBc) == 0xbc, "FaceIdea::ThresholdBc offset");
static_assert(sizeof(FaceIdea) == 0xc0, "FaceIdea size 0xc0");

// ============================================================================
// SplineMoveIdea : public BaseIdea -- Common.dll RTTI name
// ".?AVSplineMoveIdea@@". Ctor FUN_062d9bf0 (vptr 063a0568 at .text:062d9c46).
// ============================================================================
// Vtable 063a0568:
//   0 FUN_062d9c80 RET 0xc  SetContext     1 LAB_062d9d00 RET 4  Update
//   3 LAB_062da300 `MOV AX,0x16; RET`      4 LAB_062da310        Commit
//   2 / 5 BaseIdea's shared stubs
//
// Ctor, in order:
//   [ESI]          = PTR_LAB_063a0410 (BaseIdea's own vtable -- the base
//                    sub-object ctor, re-stamped at the end of the body)
//   [ESI+4]/[+8]   = 0, [ESI+0x10] byte = 0        -- BaseIdea's own span
//   ECX = ESI+0x14; CALL FUN_062d4e60              -- MoveIdea Move  @ +0x14
//   ECX = ESI+0x8c; CALL FUN_062d6750              -- FaceIdea Face  @ +0x8c
//   [ESI+0x150] byte = 0
//   [ESI+0x158]      = 0
//   [ESI]          = PTR_FUN_063a0568              -- final vtable install
//   [ESI+0x168]      = 0x41200000 (10.0f)
//   [ESI+0x16c]      = 0x42480000 (50.0f)
//   [ESI+0x154]      = 0x3f800000 (1.0f)
// Size 0x174 from the embedders' gaps: GotoBehavior +0x164..+0x2d8, DockBehavior
// (FUN_062c2e60) +0x168..+0x2dc.
//
// The +0x1c/+0x20/+0x48/+0x4c/+0x54/+0x58/+0x5c offsets Update() writes are the embedded
// MoveIdea's (this+0x14+N): Move.Handle / Context / Mode / MaxThrustBearing (restored to
// its ctor default pi) / Throttle / StopStyle / TargetKind48.
// ============================================================================
class SplineMoveIdea : public BaseIdea
{
public:
    SplineMoveIdea();

    // Slot 0, FUN_062d9c80, RET 0xc.
    bool SetContext(IObjRW* InObject, SystemManager* InHandle, IBehaviorManager* InContext) override;

    // Slot 1, LAB_062d9d00, RET 4. Follows the spline: steers the embedded Move to its P2
    // and the embedded Face to the far point (or the P2->P3 chord), and commits itself
    // once the end is reached. 3 = not ready, 0 = steering, 1 = done.
    int Update(float DeltaTime) override;

    short GetKind() const override { return 0x16; }

    // FUN_062da3f0 (.text:062da3f0-062da3f6): `FLD float ptr [ECX+0x134]; RET` -- the
    // embedded FaceIdea's RemainingAngle (+0x8c + 0xa8). Only caller: DockBehavior's
    // approach tail at .text:062c4de2 on its SplineMoveIdea (+0x168).
    float GetFaceAngle() const { return Face.RemainingAngle; }

    // Slot 4, LAB_062da310.
    void Commit() override;

    // Folded at .text:062da4d0 (also IBehaviorManager::set_camera_mode, hence
    // Ghidra's label): `MOV [ECX+0x154],EAX; RET 4`. One call site, .text:062fe148,
    // ECX = GotoBehavior+0x164 -- a SplineMoveIdea, not the manager.
    void SetBankBlendCap(float Value);

    // FUN_062da4e0, RET 4: BankBlendCap = Object->get_projected_throttle(Value, 1)
    // (`PUSH 0x1` at .text:062da4ea; no Mode ladder, unlike MoveIdea's). Callers
    // GotoBehavior::Update (.text:062fe190), DockBehavior::AdvancePreamble (062c48e6).
    void SetThrottleScale(float Value);

    // FUN_062da410, RET 4. Sole call site .text:062fe74b (GotoBehavior+0x164).
    // WideMode = the raw byte, then an inlined Move.SetMode(...) (its [ESI+0x48] is
    // Move.Mode, [ESI+0x1c] Move.Handle).
    void SetWideMode(unsigned char Value);

    // FUN_062da400, RET 4: StopStyle = Value, an integer (0 or 2). Callers
    // .text:062fe89f (GotoBehavior+0x164) and .text:062c435e (DockBehavior+0x168).
    void SetStopStyle(int Value);

    // FUN_062da380, RET 4. Callers .text:062fe73a (GotoBehavior+0x164), 062c5a62.
    // FUN_062e5ab0 is Behavior_GetCoastDistance (DAT_0639e6c8 = 0.001f).
    //
    // Retail's body, in this class's own field names (its `[ESI+0x50]` is
    // 0x14+0x3c == Move.ArrivalRange, its `[ESI+0x58]` is 0x14+0x44 ==
    // Move.StopStyle and its `[ESI+0x6d]` is 0x14+0x59 == Move.bKillEngineOffBearing):
    //   Move.ArrivalRange          = ExtrapolateBase;       // 062da387/062da397
    //   Spline                     = NewSpline;             // 062da39b
    //   Move.StopStyle             = 2;                     // 062da3a1
    //   Move.bKillEngineOffBearing = 0;                     // 062da3a8
    //   MaxSpeed15c  = Behavior_GetSpeed(Object, 1.0f, 1);  // 062da3ac
    //   CoastDistance = Behavior_GetCoastDistance(Object, MaxSpeed15c);
    //   SplineLength    = Spline->get_spline_length();         // 062da3d8
    // The middle pair is a genuine chain: retail FSTs the first result to a stack
    // slot at 062da3b1, reloads it as a raw dword at 062da3b5 and pushes THAT as
    // the second call's argument, so the two are not independent queries.
    void SetSpline(ICRSplineSegment* NewSpline);

    // ---- Fields ----
    MoveIdea Move;               // +0x14, ends +0x8c.
    FaceIdea Face;               // +0x8c, ends +0x14c.
    ICRSplineSegment* Spline;    // +0x14c. Not ctor-initialized in retail;
                                  // SetContext() nulls it.
    unsigned char WideMode;        // +0x150. Ctor-zeroed byte.
    unsigned char Pad151[0x154 - 0x151];
    float BankBlendCap;             // +0x154. Ctor default 1.0f.
    int StopStyle;                   // +0x158. Ctor-zeroed. Integer (callers push
                                      // 0 / 2).
    float MaxSpeed15c;               // +0x15c. Not ctor-initialized. SetSpline():
                                      // get_speed(1.0f, Engine) (.text:062da3b9),
                                      // as MoveIdea::MaxSpeed5c.
    float CoastDistance;            // +0x160. NOT ctor-initialized. SetSpline():
                                     // Behavior_GetCoastDistance(Object, MaxSpeed15c),
                                     // the distance coasted from full speed.
    float SplineLength;              // +0x164. NOT ctor-initialized. SetSpline():
                                      // ICRSplineSegment::get_spline_length().
    float StepNumerator;              // +0x168. Ctor default 10.0f.
    float ExtrapolateBase;             // +0x16c. Ctor default 50.0f.
    float PersistedT;                   // +0x170. NOT ctor-initialized;
                                         // SetContext()/Commit() both zero it.
};
static_assert(offsetof(SplineMoveIdea, Move) == 0x14, "SplineMoveIdea::Move offset");
static_assert(offsetof(SplineMoveIdea, Face) == 0x8c, "SplineMoveIdea::Face offset");
static_assert(offsetof(SplineMoveIdea, Spline) == 0x14c, "SplineMoveIdea::Spline offset");
static_assert(offsetof(SplineMoveIdea, WideMode) == 0x150, "SplineMoveIdea::WideMode offset");
static_assert(offsetof(SplineMoveIdea, BankBlendCap) == 0x154, "SplineMoveIdea::BankBlendCap offset");
static_assert(offsetof(SplineMoveIdea, StopStyle) == 0x158, "SplineMoveIdea::StopStyle offset");
static_assert(offsetof(SplineMoveIdea, MaxSpeed15c) == 0x15c, "SplineMoveIdea::MaxSpeed15c offset");
static_assert(offsetof(SplineMoveIdea, CoastDistance) == 0x160, "SplineMoveIdea::CoastDistance offset");
static_assert(offsetof(SplineMoveIdea, SplineLength) == 0x164, "SplineMoveIdea::SplineLength offset");
static_assert(offsetof(SplineMoveIdea, StepNumerator) == 0x168, "SplineMoveIdea::StepNumerator offset");
static_assert(offsetof(SplineMoveIdea, ExtrapolateBase) == 0x16c, "SplineMoveIdea::ExtrapolateBase offset");
static_assert(offsetof(SplineMoveIdea, PersistedT) == 0x170, "SplineMoveIdea::PersistedT offset");
static_assert(sizeof(SplineMoveIdea) == 0x174, "SplineMoveIdea size (0x164+0x174==0x2d8 in GotoBehavior, 0x168+0x174==0x2dc in FUN_062c2e60)");

// The direction GenericWeightedPick (FUN_062d76d0) draws, stored in FDirectionPicker::LastDirection:
// the tag of the weight it drew, in the order of SetWeights(Left, Right, Up, Down). 0 before the
// first draw.
enum EDodgeDirection
{
    kDodgeDirLeft = 1,
    kDodgeDirRight = 2,
    kDodgeDirUp = 3,
    kDodgeDirDown = 4,
};

// ============================================================================
// DirectionPicker -- the second base of DodgeIdea and BreakAwayIdea.
// ============================================================================
// RTTI: DodgeIdea's CHD (.rdata:063a9ae4, numBaseClasses 3) lists
// [DodgeIdea, BaseIdea, 063a9aa4]; 063a9aa4's TypeDescriptor is DirectionPicker,
// mdisp 0x14. FaceIdea's CHD does not list it.
//
// Size from the two derived ctors, FUN_062d77d0 (DodgeIdea) and FUN_062d8970
// (BreakAwayIdea), which initialise +0x14..+0x33 inline with identical code
// except one constant:
//   [this+0x14] = 0                             062d780c / 062d89aa
//   [this+0x18] = [this+0x1c] = [this+0x20]
//             = [this+0x24] = 1.0f              062d7800-062d7806 / 062d899e-062d89a7
//   LEA ECX,[this+0x28]; two dwords zeroed;
//   then the `CMP [ECX],0` + BaseWatcher::
//   set_pointer(null) pattern every BaseWatcher
//   member in this family uses                  062d77fd-062d7860 / 062d899b-062d89c9
//   [this+0x30] byte = 0 (Dodge) / 1 (BreakAway) 062d7877 / 062d8a0e
// then diverge (+0x34 = 5.0f in DodgeIdea, 0 in BreakAwayIdea): size 0x20.
//
// No vtable at +0x14, so no virtuals. Modeled as a member, not a base, so the
// offsets stay pinned by static_assert.
// ============================================================================
struct FDirectionPicker
{
    // No retail function: both derived ctors inline this prologue.
    FDirectionPicker();

    // FUN_062d88e0 (RET 4). Retail's receiver is
    // the enclosing Idea (DodgeIdea at EvadeBehavior+0x30, BreakAwayIdea at +0x110 --
    // .text:062c8295/062c82b1), and it touches only the Idea's +0x28 and +0x30, i.e. this
    // picker's Watcher (rel +0x14) and Flag1c (rel +0x1c): Watcher.set(Object ? Object+8 :
    // null) with set()'s own dedup, then Flag1c = 1. Declared on the picker because both
    // receiving Idea classes hold it at +0x14 and the body reads nothing else.
    void SetWatchObject(void const* Object);

    // FUN_062d76b0 (.text:062d76b0-062d76cc, RET 0x10): the four weights, in argument
    // order. Called on <Idea>+0x14 (.text:062c840d `LEA ECX,[ESI+0x44]`, 062c8523
    // `LEA ECX,[ESI+0x124]`), so the receiver really is this sub-object. Every caller passes
    // the four <Block>DirectionWeight{Left,Right,Up,Down} keys in that order (EvadeDodge /
    // EvadeBreak at .text:062c83ee-062c840c / 062c8504-062c8522, BuzzDodge / BuzzBreak in
    // BuzzBehavior::RecomputeApproach), which names the four weights.
    void SetWeights(float Left, float Right, float Up, float Down);

    // rel +0x00. Ctor-zeroed. GenericWeightedPick (FUN_062d76d0) zeroes the
    // weight of the direction stored here (1 Left, 2 Right, 3 Up, 4 Down), draws a new one and
    // stores it back -- so it is the last picked direction, never picked twice running.
    int LastDirection;
    float WeightLeft;          // rel +0x04. Ctor default 1.0f.
    float WeightRight;          // rel +0x08. Ctor default 1.0f.
    float WeightUp;             // rel +0x0c. Ctor default 1.0f.
    float WeightDown;           // rel +0x10. Ctor default 1.0f.
    BaseWatcher Watcher;        // rel +0x14, ends rel +0x1c.
    unsigned char Flag1c;       // rel +0x1c. DodgeIdea's ctor writes 0,
                                 // BreakAwayIdea's writes 1.
    unsigned char Pad1d[3];     // rel +0x1d, alignment only.
};
static_assert(offsetof(FDirectionPicker, WeightLeft) == 0x04, "FDirectionPicker::WeightLeft offset");
static_assert(offsetof(FDirectionPicker, WeightDown) == 0x10, "FDirectionPicker::WeightDown offset");
static_assert(offsetof(FDirectionPicker, Watcher) == 0x14, "FDirectionPicker::Watcher offset");
static_assert(offsetof(FDirectionPicker, Flag1c) == 0x1c, "FDirectionPicker::Flag1c offset");
static_assert(sizeof(FDirectionPicker) == 0x20, "FDirectionPicker size (both derived ctors' shared region is +0x14..+0x34)");

// DodgeIdea::DodgeStyle (+0xb4), named after the EvadeDodgeStyleWeight* personality keys
// EvadeBehavior::PickDodgeStyle (FUN_062c8690) draws it from.
enum EDodgeStyle
{
    kDodgeStyleWaggle = 0,
    kDodgeStyleWaggleRandom = 1,
    kDodgeStyleSlide = 2,
    kDodgeStyleCorkscrew = 3,
};

// ============================================================================
// DodgeIdea : public BaseIdea (+ DirectionPicker at +0x14) -- Common.dll RTTI
// name ".?AVDodgeIdea@@". Ctor FUN_062d77d0 (vptr 063a04c8 at .text:062d7819).
// ============================================================================
// BuzzBehavior embeds one at +0x178 (its Dodge). Size 0xe0: BuzzBehavior's ctor
// builds the next member (BreakAwayIdea) at +0x258, and this ctor's highest write
// is +0xdc (.text:062d78f5).
//
// Vtable .rdata:063a04c8:
//   0 LAB_062d7920  BaseIdea::SetContext
//   1 LAB_062d7950  Update (1163 instructions, common_dodgeidea.cpp)
//   3 LAB_062d9be0  `MOV AX,0x12; RET`
//   4 LAB_062d8830  Commit
//   2 / 5           BaseIdea's shared stubs
// ============================================================================
class DodgeIdea : public BaseIdea
{
public:
    DodgeIdea();

    // Slot 1, LAB_062d7950.
    int Update(float DeltaTime) override;

    short GetKind() const override { return 0x12; }

    // Slot 4, LAB_062d8830.
    void Commit() override;

    // FUN_062d88c0 (RET 4): copies the three dwords of *Value
    // into +0x58..+0x60, then `MOV byte [ECX+0x64],1`. Receivers: EvadeBehavior+0x30
    // (.text:062c85c9) and BuzzBehavior's DodgeIdea.
    void SetPreferredForward(Vector const& Value);

    // ---- Confirmed own-data fields, ascending offset order. Every constant is
    // ---- the raw IEEE-754 dword the ctor stores, decoded from .text bytes. ----
    FDirectionPicker Picker;         // +0x14, ends +0x34. The second base; see
                                      // FDirectionPicker above. Flag1c gets 0.
    // Field names from +0x34 on follow the personality keys both owners copy in, raw dword
    // for raw dword: EvadeBehavior::Slot3 (.text:062c8308-062c83f7, [EvadeDodgeBlock]) and
    // BuzzBehavior::RecomputeApproach (.text:062c5fa7-062c605b, [BuzzHeadTowardBlock]) store
    // the same key of their block through the same setter address.
    float IntervalTime;                // +0x34. Ctor default 5.0f
                                       // (0x40a00000). EvadeDodgeIntervalTime / BuzzDodgeIntervalTime /
                                       // BuzzSlideIntervalTime (.text:063275d0).
    float IntervalTimeVariancePercent; // +0x38. Ctor-zeroed. Update:
                                       // IntervalTimeLeft = IntervalTime - IntervalTime *
                                       // this * r (.text:062d79b0-062d79d7). The ...IntervalTime-
                                       // VariancePercent keys (.text:063275f0).
    float ConeAngle;                   // +0x3c. Ctor default 1.0471377f
                                       // (0x3f860a92) -- close to pi/3 but NOT equal to it (pi/3
                                       // is 0x3f860c48), so it is recorded as raw bits.
                                       // EvadeDodgeConeAngle / BuzzDodgeConeAngle (.text:062a66b0).
    float ConeAngleVariancePercent;    // +0x40. Ctor-zeroed. `FMUL [EBP+0x40]`
                                       // at 062d7c0f/7e3f. ...DodgeConeAngleVariancePercent.
    float WaggleAxisConeAngle;         // +0x44. Ctor-zeroed. `FMUL/FSUB
                                       // [EBP+0x44]` at 062d7e6d/7e72. ...DodgeWaggleAxisConeAngle.
    float RollAngle;                   // +0x48. Ctor-zeroed. Update pushes it as
                                       // FUN_062e2630's bank angle (062d7df9). ...DodgeRollAngle.
    float DodgeDistance;               // +0x4c. Ctor default 150.0f; Evade's
                                       // SetContext seeds 50.0f. EvadeDodgeDistance
                                       // (.text:062a33d0); the limit in Update is Field50 * this.
    float DodgeDistanceScale;                      // +0x50. Ctor-zeroed; Commit()
                                                    // zeroes it again. Update FSTPs
                                                    // (062d79fa) and FLDs (062d829f) it.
    float DodgeDistanceVariance;                     // +0x54. Ctor default 0.2f
                                                      // (0x3e4ccccd).
    Vector PreferredForward;                           // +0x58, untouched by the ctor. Written
                                                        // only by SetPreferredForward (FUN_062d88c0).
    unsigned char bHasPreferredForward;                  // +0x64. Ctor-zeroed byte.
    unsigned char Pad65[0x68 - 0x65];                      // +0x65, alignment only.
    Matrix WorkingFrame;                                     // +0x68..+0x8b. Ctor-zeroed
                                                              // by `MOV ECX,9; REP STOSD`;
                                                              // Update's steering frame
                                                              // (every `LEA EDI,[EBP+0x68];
                                                              // MOV ECX,9; REP MOVSD`).
    float IntervalTimeLeft;            // +0x8c. Ctor-zeroed; Commit() zeroes
                                       // it again. Update counts it down by DeltaTime (062d7975)
                                       // and re-arms it from IntervalTime/-VariancePercent.
    float TurnThrottle;                // +0x90. Ctor default 1.0f. Scales the
                                       // angular throttle (062d8682-062d86d0).
                                       // EvadeDodgeTurnThrottle / BuzzDodgeTurnThrottle (FUN_062d8910).
    float Throttle;                    // +0x94. Untouched by the ctor. Update
                                       // feeds it to the throttle command (062d8216/062d82bf).
                                       // Writers (FUN_062d9490): EvadeBehavior::Slot3 passes the
                                       // owner's IObjInspect::get_throttle (062c82ed),
                                       // BuzzBehavior BuzzHeadTowardEngineThrottle.
    unsigned char Field98;                                           // +0x98. Ctor-zeroed byte.
    unsigned char Opaque99[0xb4 - 0x99];                               // +0x99, untouched.
    // +0xb4. Ctor default 1. Update's two switches dispatch on it.
    // EvadeBehavior::PickDodgeStyle (FUN_062c8690) picks it from the EvadeDodgeStyleWeight*
    // keys: 0 = Waggle, 1 = WaggleRandom, 2 = Slide, 3 = Corkscrew. BuzzBehavior writes 2 with
    // the BuzzSlide* keys, 0 for Waggle and 3 when BuzzHeadTowardRollThrottle != 0.
    int DodgeStyle;
    Vector VectorB8;                                                      // +0xb8, ends +0xc4.
                                                                           // Ctor-zeroed.
    float FieldC4;                                                          // +0xc4. Ctor default
                                                                             // 30.0f (0x41f00000).
    float SlideThrottle;               // +0xc8. Ctor default 1.0f. Scales the
                                       // slide nudge (062d86d0..). EvadeDodgeSlideThrottle /
                                       // BuzzSlideThrottle (FUN_062d8930).
    // +0xcc, ends +0xd8. Ctor default {0.1f, 0.0f, 1.0f}; Update transforms it by the own
    // orientation as one Vector. .x is EvadeDodgeCorkscrewTurnThrottle (FUN_062d8940),
    // .z is EvadeDodgeCorkscrewRollThrottle / BuzzHeadTowardRollThrottle (FUN_062d8950).
    Vector VectorCc;
    unsigned char bRollFlipDirection;  // +0xd8. Ctor-zeroed. Set: each style-3 reroll
                                       // negates VectorCc.z (062d8160). bEvadeDodgeCorkscrewRollFlipDirection /
                                       // bBuzzHeadTowardRollFlipDirection (FUN_062d8960).
    unsigned char FlagD9;                                                          // +0xd9. Ctor-zeroed.
    unsigned char FlagDa;                                                           // +0xda. Ctor-zeroed.
    unsigned char PadDb;                                                             // +0xdb, alignment.
    float ChaseSpeedScale;                                                            // +0xdc. Ctor
                                                                                       // default 1.0f.
};
static_assert(offsetof(DodgeIdea, Picker) == 0x14, "DodgeIdea::Picker offset");
static_assert(offsetof(DodgeIdea, IntervalTime) == 0x34, "DodgeIdea::IntervalTime offset");
static_assert(offsetof(DodgeIdea, ConeAngle) == 0x3c, "DodgeIdea::ConeAngle offset");
static_assert(offsetof(DodgeIdea, DodgeDistance) == 0x4c, "DodgeIdea::DodgeDistance offset");
static_assert(offsetof(DodgeIdea, DodgeDistanceVariance) == 0x54, "DodgeIdea::DodgeDistanceVariance offset");
static_assert(offsetof(DodgeIdea, bHasPreferredForward) == 0x64, "DodgeIdea::bHasPreferredForward offset");
static_assert(offsetof(DodgeIdea, WorkingFrame) == 0x68, "DodgeIdea::WorkingFrame offset");
static_assert(offsetof(DodgeIdea, DodgeDistanceScale) == 0x50, "DodgeIdea::DodgeDistanceScale offset");
static_assert(offsetof(DodgeIdea, IntervalTimeLeft) == 0x8c, "DodgeIdea::IntervalTimeLeft offset");
static_assert(offsetof(DodgeIdea, TurnThrottle) == 0x90, "DodgeIdea::TurnThrottle offset");
static_assert(offsetof(DodgeIdea, Throttle) == 0x94, "DodgeIdea::Throttle offset");
static_assert(offsetof(DodgeIdea, Field98) == 0x98, "DodgeIdea::Field98 offset");
static_assert(offsetof(DodgeIdea, DodgeStyle) == 0xb4, "DodgeIdea::DodgeStyle offset");
static_assert(offsetof(DodgeIdea, VectorB8) == 0xb8, "DodgeIdea::VectorB8 offset");
static_assert(offsetof(DodgeIdea, FieldC4) == 0xc4, "DodgeIdea::FieldC4 offset");
static_assert(offsetof(DodgeIdea, VectorCc) == 0xcc, "DodgeIdea::VectorCc offset");
static_assert(offsetof(DodgeIdea, SlideThrottle) == 0xc8, "DodgeIdea::SlideThrottle offset");
static_assert(offsetof(DodgeIdea, bRollFlipDirection) == 0xd8, "DodgeIdea::bRollFlipDirection offset");
static_assert(offsetof(DodgeIdea, ChaseSpeedScale) == 0xdc, "DodgeIdea::ChaseSpeedScale offset");
static_assert(sizeof(DodgeIdea) == 0xe0, "DodgeIdea size (BuzzBehavior's 0x178..0x258 ctor gap meets the ctor's own +0xdc write)");

// BreakAwayIdea::BreakStyle (+0x34), named after the EvadeBreakStyleWeight* personality keys
// EvadeBehavior::OnEngage draws it from (see BreakStyle below).
enum EBreakStyle
{
    kBreakStyleOutrun = 0,     // the "drive forward" arm
    kBreakStyleSideways = 1,
    kBreakStyleReverse = 2,    // runs the AttemptReverseTime / ReverseDistance checks
};

// ============================================================================
// BreakAwayIdea : public BaseIdea (+ DirectionPicker at +0x14) -- Common.dll
// RTTI name ".?AVBreakAwayIdea@@". Ctor FUN_062d8970 (vptr 063a04f8 at .text:062d89b6).
// ============================================================================
// BuzzBehavior embeds one at +0x258 (its BreakAway). Size 0x9c: BuzzBehavior's next
// member is at +0x2f4, and this ctor's highest write is +0x98 (.text:062d89e0).
//
// Vtable .rdata:063a04f8:
//   0 LAB_062d7920  BaseIdea::SetContext
//   1 LAB_062d8a50  Update (634 instructions)
//   3 LAB_062d93d0  `MOV AX,0x52; RET`
//   4 LAB_062d93e0  Commit: skips its notification when the owner's
//                   IObjInspect::is_player() (vt+0xbc, .text:062d9434) is true
//   2 / 5           BaseIdea's shared stubs
// ============================================================================
class BreakAwayIdea : public BaseIdea
{
public:
    BreakAwayIdea();

    // Slot 1, LAB_062d8a50, RET 4. Returns 3 (no Object), 1 (done) or 0.
    int Update(float DeltaTime) override;

    // FUN_062d9220, non-virtual; only caller Update (.text:062d8d9b, ECX = this).
    // Returns 2 (nothing watched / still closing), 1 (clear -- break off) or 0.
    int EvaluateBreakAwayClearance();

    // FUN_062d9450, RET 4, non-virtual. One caller, .text:062c6caf, on
    // BuzzBehavior+0x258 (LEA at .text:062c6c8e; the same EDI dispatches to
    // LAB_062d8a50 at 062c6cbc). Picker.Watcher.set(Object ? Object + 8 : nullptr),
    // then Picker.Flag1c = 0 -- cleared, unlike FaceIdea/MoveIdea's tag setters.
    void SetWatchObject(void const* Object);

    short GetKind() const override { return 0x52; }

    // Slot 4, LAB_062d93e0.
    void Commit() override;

    // ---- Fields ----
    FDirectionPicker Picker;         // +0x14, ends +0x34. Flag1c gets 1 here.
    // Names below follow the personality keys the owners copy in (EvadeBehavior::Slot3
    // .text:062c84aa-062c8568, [EvadeBreakBlock]; BuzzBehavior::Update .text:062c6c97,
    // [BuzzPassByBlock]; BaseFollowerBehavior .text:062cbe60, FormationExitRollOutrunThrottle).
    //
    // +0x34. Ctor-zeroed. Update() dispatches on it (062d8d75). Evade picks it
    // from the EvadeBreakStyleWeight* keys (.text:062c843b-062c84a7): 0 = Outrun (the "drive
    // forward" arm; BaseFollowerBehavior's FormationExitRollOutrunThrottle path writes 0),
    // 1 = Sideways, 2 = Reverse (the arm that runs the AttemptReverseTime / ReverseDistance
    // checks; EvadeBehavior case 3 writes 2). BuzzBehavior writes 1.
    int BreakStyle;
    float BreakAwayRange;               // +0x38. Ctor default 350.0f (0x43af0000). The range
                                        // Update() hands IsWithinInterceptRangeOfIface
                                        // (062d90d4 / 062d91dc). Evade and Buzz never set it.
    unsigned char Flag3c;                // +0x3c. Ctor default 1.
    unsigned char Pad3d[3];               // +0x3d, alignment only.
    float Throttle;                     // +0x40. Ctor default 1.0f. Update()
                                        // passes it to ThrustManager::SetThrottle (062d8e31,
                                        // 062d9182). Its one writer is EvadeBehavior::Slot3 with
                                        // the owner's IObjInspect::get_throttle (062c8303).
    unsigned char Flag44;                    // +0x44. Ctor-zeroed byte.
    unsigned char Pad45[3];                   // +0x45, alignment only.
    float AttemptReverseElapsed;        // +0x48. Ctor-zeroed. BreakStyle 2: DeltaTime sum (062d8dbb) compared
                                        // against AttemptReverseTime (062d8d8e).
    float AttemptReverseTime;           // +0x4c. Ctor default 4.0f.
                                        // EvadeBreakAttemptReverseTime (.text:062a33d0).
    float ReverseHoldElapsed;                       // +0x50. Ctor-zeroed. `FADD [EBP+0x50];
                                                     // FST; FCOMP [EBP+0x58]` (062d8def).
    float ReverseDistance;              // +0x54. Ctor default 100.0f. The
                                        // clearance threshold EvaluateBreakAwayClearance()
                                        // compares against (062d92f9). EvadeBreakReverseDistance
                                        // (FUN_062d93b0).
    float ReverseHoldTime;                              // +0x58. Ctor default 6.0f -- the
                                                         // cap on ReverseHoldElapsed.
    unsigned char Flag5c;                                 // +0x5c. Ctor-zeroed byte.
    unsigned char Pad5d[0x60 - 0x5d];                       // +0x5d, alignment only.
    Matrix Frame60;                                           // +0x60, ends +0x84. Not
                                                               // ctor-initialized. Update's
                                                               // reroll copies
                                                               // BuildOrientationBasis's
                                                               // output here (062d8c3b);
                                                               // the switch arms rotate its
                                                               // rows and pass it as a Matrix.
    float IntervalElapsed;              // +0x84. Ctor default 100000.0f
                                        // (0x47c35000); Commit() restores it. The reroll timer:
                                        // Update() adds DeltaTime and re-picks the break-away
                                        // direction once it reaches IntervalTime (062d8b9e).
    float IntervalTime;                 // +0x88. Ctor default 100000.0f.
                                        // EvadeBreakIntervalTime (FUN_062d9480).
    float AfterburnerDelay;             // +0x8c. Ctor-zeroed. Evade:
                                        // EvadeBreakAfterburnerDelay minus a random share of
                                        // ...AfterburnerDelayVariancePercent (FUN_062d93c0).
    float AfterburnerElapsed;           // +0x90. Ctor-zeroed; Commit() zeroes it.
                                        // DeltaTime sum; once it reaches AfterburnerDelay Update()
                                        // calls ThrustManager::SetThrustersActive(true)
                                        // (062d90a0-062d90c1, 062d9199-062d91ba).
    float TurnThrottle;                 // +0x94. Ctor default 1.0f. Scales the
                                        // steering command (062d8fa7). EvadeBreakTurnThrottle /
                                        // BuzzBreakTurnThrottle (FUN_062d9490).
    float RollThrottle;                 // +0x98. Ctor default 1.0f. The z of the
                                        // {0, 0, RollThrottle} command (062d9072, 062d9132).
                                        // EvadeBreakRollThrottle / FormationExitRollOutrunThrottle
                                        // (FUN_062d94a0).
};
static_assert(offsetof(BreakAwayIdea, Picker) == 0x14, "BreakAwayIdea::Picker offset");
static_assert(offsetof(BreakAwayIdea, BreakStyle) == 0x34, "BreakAwayIdea::BreakStyle offset");
static_assert(offsetof(BreakAwayIdea, BreakAwayRange) == 0x38, "BreakAwayIdea::BreakAwayRange offset");
static_assert(offsetof(BreakAwayIdea, Flag3c) == 0x3c, "BreakAwayIdea::Flag3c offset");
static_assert(offsetof(BreakAwayIdea, Throttle) == 0x40, "BreakAwayIdea::Throttle offset");
static_assert(offsetof(BreakAwayIdea, Flag44) == 0x44, "BreakAwayIdea::Flag44 offset");
static_assert(offsetof(BreakAwayIdea, AttemptReverseTime) == 0x4c, "BreakAwayIdea::AttemptReverseTime offset");
static_assert(offsetof(BreakAwayIdea, ReverseDistance) == 0x54, "BreakAwayIdea::ReverseDistance offset");
static_assert(offsetof(BreakAwayIdea, ReverseHoldTime) == 0x58, "BreakAwayIdea::ReverseHoldTime offset");
static_assert(offsetof(BreakAwayIdea, Flag5c) == 0x5c, "BreakAwayIdea::Flag5c offset");
static_assert(offsetof(BreakAwayIdea, Frame60) == 0x60, "BreakAwayIdea::Frame60 offset");
static_assert(offsetof(BreakAwayIdea, IntervalElapsed) == 0x84, "BreakAwayIdea::IntervalElapsed offset");
static_assert(offsetof(BreakAwayIdea, AfterburnerDelay) == 0x8c, "BreakAwayIdea::AfterburnerDelay offset");
static_assert(offsetof(BreakAwayIdea, TurnThrottle) == 0x94, "BreakAwayIdea::TurnThrottle offset");
static_assert(offsetof(BreakAwayIdea, RollThrottle) == 0x98, "BreakAwayIdea::RollThrottle offset");
static_assert(sizeof(BreakAwayIdea) == 0x9c, "BreakAwayIdea size (BuzzBehavior's 0x258..0x2f4 upper bound meets the ctor's own +0x98 write)");

// DodgeMissileIdea::Mode1c (+0x1c), set by EvadeBehavior::OnUpdate's mode 4 from which of the
// Personality MissileReaction EvadeBreak / EvadeSlide / EvadeAfterburn reaction times the
// incoming missile is inside (SystemManagerTimerTable::MissileWithinReaction00 / 04 / 08).
enum EDodgeMissileMode
{
    kDodgeMissileAfterburn = 0,   // zero steering command, thrusters on
    kDodgeMissileBreak = 1,       // fly the perpendicular escape frame
    kDodgeMissileSlide = 2,       // nudge sideways
};

// ============================================================================
// DodgeMissileIdea : public BaseIdea -- Common.dll RTTI name
// ".?AVDodgeMissileIdea@@". Ctor FUN_062d94b0 (vptr 063a0524 at .text:062d94dc;
// COL 063a9b88, CHD 063a9b78 numBaseClasses 2: single inheritance).
// ============================================================================
// Size 0x20: Update reads +0x1c (.text:062d964b), and the one embedder,
// FUN_062c7790 (EvadeBehavior's ctor), builds it at +0x1ac and its next member at
// +0x1cc (.text:062c77d6/062c77e6).
//
// Vtable .rdata:063a0524:
//   0 LAB_062d7920  BaseIdea::SetContext
//   1 LAB_062d94f0  Update (428 instructions, RET 4 on all five exits)
//   3 LAB_062d9a60  `MOV AX,0x56; RET`
//   4 LAB_062d9a70  Commit
//   2 / 5           BaseIdea's shared stubs
//
// A thin dispatcher: it turns one watched incoming object (MissileWatcher) and a
// mode (Mode1c) into nav commands through shared helpers
// (AiVolumeProbeApproachDirection, BuildOrientationBasis, BuildRandomSpreadFrame,
// TurnHelper::get_angular_throttle, ComputeBankedSteeringFrame). The dodge
// direction is normalize(DirToMissile x Axis), Axis being the ship's up or forward
// column depending on how head-on the missile is.
// ============================================================================
class DodgeMissileIdea : public BaseIdea
{
public:
    DodgeMissileIdea();

    // Slot 1, LAB_062d94f0. Returns 3 (no Object), 1 (nothing watched) or 0.
    int Update(float DeltaTime) override;

    short GetKind() const override { return 0x56; }

    // Slot 4, LAB_062d9a70.
    void Commit() override;

    // FUN_062d9ae0 (.text:062d9ae0-062d9aff, RET 4): MissileWatcher.set(Object ? Object+8 :
    // null), retail's own `CMP EAX,[ECX]; JZ` dedup then a tail JMP into set_pointer.
    // No tag byte. Only caller: EvadeBehavior::Slot7 mode 4 (.text:062c7f30/7f62/7f75/7faa).
    void SetWatchObject(void const* Object);

    // ---- Fields ----

    // +0x14, ends +0x1c. A bare BaseWatcher (the ctor zeroes only these two dwords,
    // .text:062d94d2/062d94d5), not an FDirectionPicker.
    BaseWatcher MissileWatcher;

    // +0x1c. Update's switch (.text:062d964b-062d965f): 0, 1, 2; anything else
    // returns 0. Not ctor-initialized in retail; the port zeroes it. Written by
    // EvadeBehavior::Slot7 mode 4 through .text:0631d950 (`MOV [ECX+0x1c],EAX;
    // RET 4`, ICF-labelled CEquip::ConnectClientEquip) on this+0x1ac (062c7fb1)
    // with 1, 2 or 0.
    int Mode1c;
};
static_assert(offsetof(DodgeMissileIdea, MissileWatcher) == 0x14, "DodgeMissileIdea::MissileWatcher offset");
static_assert(offsetof(DodgeMissileIdea, Mode1c) == 0x1c, "DodgeMissileIdea::Mode1c offset");
static_assert(sizeof(DodgeMissileIdea) == 0x20, "DodgeMissileIdea size (host FUN_062c7790's 0x1ac..0x1cc sub-object span meets Update's own +0x1c read)");
