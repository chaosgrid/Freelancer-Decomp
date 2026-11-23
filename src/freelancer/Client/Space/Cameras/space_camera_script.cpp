// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// ScriptCamera (retail `E:\FL\Scratch\Source\Client\space\scriptcam.cpp`):
// ctor 0x0051e100, dtor 0x0051dfc0, setup 0x0051e3b0, and the five overridden vtable slots --
// 0x54 name (0x0051e070), 0x60 finished (0x0051e810), 0x64 activate (0x0051e4c0), 0x68
// deactivate (0x0051e660), 0x6c update (0x0051e740). The two transform sinks and their shared
// DACOM plumbing (0x0051e830 QI, 0x0051e8e0 AddRef, 0x0041a800 Release) are here too.

#include "Space/Cameras/space_camera_script.h"

#include "Space/Cameras/space_camera.h"
#include "Space/space_letterbox.h"

#include "DALib.h"
#include "common_exports.h"
#include "common_csys.h"   // Acquire3DMathEngine (FUN_005b73e0, engine_init.cpp)
#include "common_iface.h"  // IObjInspect -- the watched object's thiscall getters
#include "common_misc.h"   // GetFullFilename
#include "dacom.h"
#include "thorn_iface.h"
#include "x86math.h"

#include <new>
#include <stdlib.h>
#include <string.h>

namespace
{
    // A watcher holds the Watchable subobject, eight bytes into the IObjRW.
    const int kWatchableOffset = 8;

    // The name literal at .rdata 0x005dc1e4, returned by slot 0x54.
    const char kScriptCameraClassName[] = "ScriptCamera";

    // The DACOM factory name the camera creates.
    const char kScriptEngineFactory[] = "IScriptEngine";

    // The user property that marks the entity the camera should follow.
    const char kMainObjectProperty[] = "main_object";

    // The source file and line retail names in its load warning.
    const char kScriptCamSource[] = "E:\\FL\\Scratch\\Source\\Client\\space\\scriptcam.cpp";
    const int kScriptCamLoadWarningLine = 0x9a;

    // The script engine's slot 21 state the camera treats as "done".
    const int kScriptStateFinished = 4;

    // The entity type the camera binds its own transform sink to.
    const int kScriptEntityTypeCamera = 4;

    // The code a failed QueryInterface returns (not in dacom.h).
    const GENRESULT kGrInterfaceUnsupported = static_cast<GENRESULT>(0xFFFFFFFD);

    // .rdata 0x005d3f50: the script engine is ticked in whole MILLISECONDS.
    const double kSecondsToMilliseconds = 1000.0;

    // The bars slide over a third of a second either way.
    const float kLetterboxSpeed = 3.0f;

    IScriptEngine* ScriptOf(void* Script)
    {
        return static_cast<IScriptEngine*>(Script);
    }

    // DAT_006752c8 -- scriptcam.cpp's own lazily acquired 3DMathEngine (FUN_005b73e0). No null
    // test on the result.
    I3DMathEngine* G_ScriptCameraMathEngine = nullptr;

    I3DMathEngine* ScriptCameraMathEngine()
    {
        if (G_ScriptCameraMathEngine == nullptr)
            G_ScriptCameraMathEngine = Acquire3DMathEngine();
        return G_ScriptCameraMathEngine;
    }

    // A {Orientation[9], Position[3]} pair laid out back to back is exactly x86math's FTransform
    // (0x30 bytes); retail hands the engine the address of the orientation.
    FTransform const& AsTransform(const float* Orientation9)
    {
        return *reinterpret_cast<FTransform const*>(Orientation9);
    }

    void Transpose3x3(float* Dst, const float* Src)
    {
        Dst[0] = Src[0]; Dst[1] = Src[3]; Dst[2] = Src[6];
        Dst[3] = Src[1]; Dst[4] = Src[4]; Dst[5] = Src[7];
        Dst[6] = Src[2]; Dst[7] = Src[5]; Dst[8] = Src[8];
    }

    void SetIdentity(float* Orientation9, float* Position3)
    {
        Orientation9[0] = 1.0f; Orientation9[1] = 0.0f; Orientation9[2] = 0.0f;
        Orientation9[3] = 0.0f; Orientation9[4] = 1.0f; Orientation9[5] = 0.0f;
        Orientation9[6] = 0.0f; Orientation9[7] = 0.0f; Orientation9[8] = 1.0f;
        Position3[0] = 0.0f; Position3[1] = 0.0f; Position3[2] = 0.0f;
    }

    // The stored Watchable pointer minus eight (retail's inlined `Watchable::get()`).
    void* WatchedObjectOf(const BaseWatcher& Watcher)
    {
        if (Watcher.target() == nullptr)
            return nullptr;
        unsigned char* const Object =
            const_cast<unsigned char*>(
                reinterpret_cast<const unsigned char*>(Watcher.target())) -
            kWatchableOffset;
        return Object;
    }

    // The shared head of the setup (0x0051e3b0) and the deactivate (0x0051e660): hand both
    // entity handles back to the script engine, release it, and forget the name and the driven
    // object.
    void TearDownScript(FScriptCamera* Cam)
    {
        if (Cam->Script == nullptr)
            return;

        IScriptEngine* const Script = ScriptOf(Cam->Script);

        // Each handle is returned only if bound, and reset to -1.
        if (Cam->CameraEntity != -1)
        {
            Script->Slot13Detach(Cam->CameraEntity);
            Cam->CameraEntity = -1;
        }
        if (Cam->ObjectEntity != -1)
        {
            // Retail re-reads Script from the object here rather than reusing the register.
            ScriptOf(Cam->Script)->Slot13Detach(Cam->ObjectEntity);
            Cam->ObjectEntity = -1;
        }

        reinterpret_cast<IDAComponent*>(Cam->Script)->Release();

        Cam->Script = nullptr;
        Cam->NameLength = 0;
        Cam->Name[0] = '\0';

        // Retail tests the watcher's stored pointer, so an empty watcher is left alone. The sink
        // pointer at +0x100 itself is NOT tested.
        if (Cam->ObjectTransform->Watched.target() != nullptr)
            Cam->ObjectTransform->Watched.set(nullptr);
    }
}

// ---------------------------------------------------------------- the two transform sinks

GENRESULT DACOM_API FScriptCamSink::QueryInterface(const char* Iid, void** Out)
{
    // LAB_0051e830. Retail walks a {name, offset} table at .rdata 0x005dc2a8 with exactly one
    // live row (the geo IID at offset zero). Neither Iid nor Out is null-checked.
    if (strcmp(Iid, Iids::GeoTransformControl) == 0)
    {
        AddRef();
        *Out = static_cast<IGeoTransformControl*>(this);
        return GR_OK;
    }

    *Out = nullptr;
    return kGrInterfaceUnsupported;
}

unsigned long DACOM_API FScriptCamSink::AddRef()
{
    // LAB_0051e8e0: returns the NEW value.
    ++RefCount;
    return static_cast<unsigned long>(RefCount);
}

unsigned long DACOM_API FScriptCamSink::Release()
{
    // LAB_0041a800, shared with FRoomBatcherSink::Release (room_control.cpp). An UNSIGNED
    // compare: only an exact zero skips the decrement.
    if (RefCount != 0)
        --RefCount;

    if (RefCount != 0)
        return static_cast<unsigned long>(RefCount);

    // The count is put back to 1 before the free, so a re-entrant release cannot free twice;
    // zero comes back. The port's sinks are malloc'd (see ScriptCameraConstruct).
    RefCount = 1;
    free(this);
    return 0;
}

// ------------------------------------------------------------------ camera sink slots

GENRESULT DACOM_API FScriptCamTransform::SetPosition(float* Position3)
{
    // LAB_0051e080.
    Position[0] = Position3[0];
    Position[1] = Position3[1];
    Position[2] = Position3[2];
    return GR_OK;
}

GENRESULT DACOM_API FScriptCamTransform::SetOrientation(float* Orientation9)
{
    // LAB_0051e0a0.
    memcpy(Orientation, Orientation9, 9 * sizeof(float));
    return GR_OK;
}

GENRESULT DACOM_API FScriptCamTransform::GetPosition(float* OutPosition3)
{
    // LAB_0051e0c0.
    OutPosition3[0] = Position[0];
    OutPosition3[1] = Position[1];
    OutPosition3[2] = Position[2];
    return GR_OK;
}

GENRESULT DACOM_API FScriptCamTransform::GetOrientation(float* OutOrientation9)
{
    // LAB_0051e0e0.
    memcpy(OutOrientation9, Orientation, 9 * sizeof(float));
    return GR_OK;
}

// ------------------------------------------------------------------ object sink slots

FScriptCamObjectTransform::FScriptCamObjectTransform()
{
    // Unlike the camera's sink, this one IS fully initialised (retail zeroes both watcher
    // dwords; BaseWatcher's default ctor does the same).
    SetIdentity(OrientationA, PositionA);
    SetIdentity(OrientationB, PositionB);
}

GENRESULT DACOM_API FScriptCamObjectTransform::SetPosition(float* /*Position3*/)
{
    // LAB_00451370, a `return 0` stub shared by both setters: the object sink discards writes.
    return GR_OK;
}

GENRESULT DACOM_API FScriptCamObjectTransform::SetOrientation(float* /*Orientation9*/)
{
    // LAB_00451370.
    return GR_OK;
}

GENRESULT DACOM_API FScriptCamObjectTransform::GetPosition(float* OutPosition3)
{
    // LAB_0051e250.
    void* const Object = WatchedObjectOf(Watched);
    if (Object != nullptr)
    {
        // Cached in the sink's SECOND position field.
        const Vector& World = static_cast<IObjInspect*>(Object)->get_position();
        PositionB[0] = World.x;
        PositionB[1] = World.y;
        PositionB[2] = World.z;
    }

    // That position in the FIRST transform's frame. The cache is used even with no object, so
    // a sink that lost its object keeps reporting the last place it saw it.
    FVector Local;
    ScriptCameraMathEngine()->InverseTransform(Local, AsTransform(OrientationA),
                                               *reinterpret_cast<FVector const*>(PositionB));
    OutPosition3[0] = Local.X;
    OutPosition3[1] = Local.Y;
    OutPosition3[2] = Local.Z;
    return GR_OK;
}

GENRESULT DACOM_API FScriptCamObjectTransform::GetOrientation(float* OutOrientation9)
{
    // LAB_0051e2e0.
    void* const Object = WatchedObjectOf(Watched);
    if (Object != nullptr)
    {
        // get_transform (slot 3, folded with get_orientation); nine floats cached.
        const Transform& World = static_cast<IObjInspect*>(Object)->get_transform();
        memcpy(OrientationB, &World, 9 * sizeof(float));
    }

    // transpose(first orientation) x cached one.
    float Transposed[9];
    Transpose3x3(Transposed, OrientationA);
    FMatrix Product;
    ScriptCameraMathEngine()->MulMatrix(Product, *reinterpret_cast<FMatrix const*>(Transposed),
                                        *reinterpret_cast<FMatrix const*>(OrientationB));
    memcpy(OutOrientation9, &Product, 9 * sizeof(float));
    return GR_OK;
}

// ---------------------------------------------------------------- the ScriptCamera vtable
//
// .rdata 0x005dc164: FGameCamera's 0x005dbf5c with exactly five slots replaced --
//
//   0x54  0x0051e070  (GameCamera 0x0051b3b0)   name
//   0x60  0x0051e810  (GameCamera 0x0044fd00)   finished
//   0x64  0x0051e4c0  (GameCamera 0x0051b450)   activate
//   0x68  0x0051e660  (GameCamera 0x0051b480)   deactivate
//   0x6c  0x0051e740  (GameCamera 0x0051b650)   update

void ScriptCameraConstruct(FScriptCamera* Cam)
{
    // ScriptCamera skips HUDCamera entirely. No null test on `this` (the one caller passes a
    // static).
    GameCameraConstruct((FGameCamera*)Cam);

    Cam->NameLength = 0;
    Cam->Name[0] = '\0';

    SetIdentity(Cam->Orientation, Cam->Position);

    // Retail installs the ScriptCamera vptr here.

    Cam->Script = nullptr;

    // operator new(0x38); retail stores null on failure rather than throwing. Port: malloc +
    // placement new, because Release() frees with free(). Retail leaves the Csys uninitialised
    // (the script writes it before the camera reads it); the port does not zero it either.
    Cam->CameraTransform = static_cast<FScriptCamTransform*>(malloc(sizeof(FScriptCamTransform)));
    if (Cam->CameraTransform != nullptr)
        new (Cam->CameraTransform) FScriptCamTransform();

    // operator new(0x70).
    Cam->ObjectTransform =
        static_cast<FScriptCamObjectTransform*>(malloc(sizeof(FScriptCamObjectTransform)));
    if (Cam->ObjectTransform != nullptr)
        new (Cam->ObjectTransform) FScriptCamObjectTransform();

    Cam->CameraEntity = -1;
    Cam->ObjectEntity = -1;
}

void ScriptCameraDestruct(FScriptCamera* Cam)
{
    Cam->~FScriptCamera();
}

FScriptCamera::~FScriptCamera()
{
    // FUN_0051dfc0. No null test on `this` (every caller passes a static camera).

    // The camera's own sink: its inlined dtor only rewrites its vptr, then operator delete; the
    // port's sinks are malloc'd, so free(). The member is not cleared afterwards.
    if (this->CameraTransform != nullptr)
        free(this->CameraTransform);

    // The object sink: its watcher is unhooked first, THEN the object is deleted.
    if (this->ObjectTransform != nullptr)
    {
        if (this->ObjectTransform->Watched.target() != nullptr)
            this->ObjectTransform->Watched.set(nullptr);
        free(this->ObjectTransform);
    }

    // The inlined FGameCamera and FSpaceCamera dtors only unhook the +0xc0 watcher and store
    // dead vptrs.

    // Retail's destructor never releases Script -- it relies on slot 0x68 having run. The port
    // keeps that asymmetry deliberately; see docs/re/deviations.md.
}

void ScriptCameraSetup(FScriptCamera* Cam, const char* ScriptName, void* Object,
                       const float* Transform12)
{
    // FUN_0051e3b0. No null test on `this` or on the +0x100 sink.

    // An already-loaded script is torn down first, so this doubles as the reset.
    TearDownScript(Cam);

    // The object sink watches the new object (same short-circuit as GameCameraAttach).
    Watchable const* const Target =
        (Object != nullptr)
            ? reinterpret_cast<Watchable const*>(static_cast<unsigned char*>(Object) +
                                                 kWatchableOffset)
            : nullptr;

    if (Target != Cam->ObjectTransform->Watched.target())
        Cam->ObjectTransform->Watched.set(Target);

    if (ScriptName == nullptr)
    {
        Cam->NameLength = 0;
        Cam->Name[0] = '\0';
    }
    else
    {
        // An over-long name is truncated to 0x103, never rejected.
        size_t Length = strlen(ScriptName);
        if (Length > static_cast<size_t>(kScriptCameraNameMax))
            Length = static_cast<size_t>(kScriptCameraNameMax);

        memcpy(Cam->Name, ScriptName, Length);
        Cam->Name[Length] = '\0';
        Cam->NameLength = static_cast<unsigned>(Length);
    }

    // Nine floats of orientation then three of position; null leaves the transform untouched.
    if (Transform12 != nullptr)
    {
        memcpy(Cam->Orientation, Transform12, 9 * sizeof(float));
        memcpy(Cam->Position, Transform12 + 9, 3 * sizeof(float));
    }
}

const char* ScriptCameraName()
{
    return kScriptCameraClassName;
}

bool ScriptCameraIsFinished(const FScriptCamera* Cam)
{
    // 0x0051e810. No script counts as finished.
    if (Cam->Script == nullptr)
        return true;

    // Lets the camera controller take the view back when a cutscene ends.
    const GENRESULT State = ScriptOf(Cam->Script)->Slot21GetState();
    return State == kScriptStateFinished;
}

void* ScriptCameraWatchedObject(const FScriptCamera* Cam)
{
    if (Cam == nullptr || Cam->ObjectTransform == nullptr)
        return nullptr;
    return WatchedObjectOf(Cam->ObjectTransform->Watched);
}

bool ScriptCameraActivate(FScriptCamera* Cam, const int* ViewRect4)
{
    // 0x0051e4c0. Retail's descriptor is 0x14 bytes, not dacom.h's 0xC AGGDESC (the same shape
    // ThornScriptLoad uses), so it is declared locally at the retail size.
    struct FScriptAggDesc
    {
        unsigned long Size;
        const char* Name;
        void* Field08;
        void* Field0C;
        void* Field10;
    };
    static_assert(sizeof(FScriptAggDesc) == 0x14, "retail descriptor is 0x14 bytes");

    // No null test on DALib::Dacom.
    ICOManager* const Manager = DALib::Dacom;

    FScriptAggDesc Desc;
    Desc.Size = 0x14;
    Desc.Name = kScriptEngineFactory;
    Desc.Field08 = nullptr;
    Desc.Field0C = nullptr;
    Desc.Field10 = nullptr;

    // Neither the instance nor any result is tested.
    void* Instance = nullptr;
    Manager->CreateInstance(reinterpret_cast<AGGDESC*>(&Desc), &Instance);

    IDAComponent* const Component = reinterpret_cast<IDAComponent*>(Instance);
    Component->QueryInterface(Iids::ScriptEngine, &Cam->Script);

    // Only the queried interface is kept.
    Component->Release();

    // The starting transform goes into the OBJECT sink's first Csys before the script loads, so
    // the script sees the frame `main_object` is placed relative to. Sink pointer not tested.
    memcpy(Cam->ObjectTransform->OrientationA, Cam->Orientation, 9 * sizeof(float));
    memcpy(Cam->ObjectTransform->PositionA, Cam->Position, 3 * sizeof(float));

    // The stored name is a DATA-relative path.
    const char* const FullPath = GetFullFilename(Cam->Name);

    const GENRESULT LoadResult = ScriptOf(Cam->Script)->Slot43LoadPath(FullPath);
    if (LoadResult != GR_OK)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: ScriptCamera load failed: %s\n",
              kScriptCamSource, kScriptCamLoadWarningLine, FullPath);
        // The engine is NOT released and Script is NOT cleared; only the deactivate slot cleans
        // up. Kept as retail.
        return false;
    }

    // Walk every entity once and bind the two sinks.
    const unsigned long EntityCount = ScriptOf(Cam->Script)->Slot6EntityCount();

    for (int Index = 0; Index < static_cast<int>(EntityCount); ++Index)
    {
        // The FIRST dword is the entity type; dereferenced without a test.
        const int* const Entity =
            static_cast<const int*>(ScriptOf(Cam->Script)->Slot8EntitySnapInorder(Index));

        if (*Entity == kScriptEntityTypeCamera)
        {
            ScriptOf(Cam->Script)->Slot12Attach(Index, Cam->CameraTransform);
            Cam->CameraEntity = Index;
        }

        // INDEPENDENTLY of the type (no `else`), an entity carrying `main_object` drives the
        // object sink; the last matching index wins in each case.
        const char* const MainObject = ThornGetUserString(
            reinterpret_cast<IScriptEngine*>(Cam->Script), Index, kMainObjectProperty);
        if (MainObject != nullptr)
        {
            ScriptOf(Cam->Script)->Slot12Attach(Index, Cam->ObjectTransform);
            Cam->ObjectEntity = Index;
        }
    }

    // The script starts at time zero.
    ScriptOf(Cam->Script)->Slot22Start(0);

    // FGameCamera's activate (FUN_0051b450), which also runs the slot-0x5c viewport reset.
    const bool bBaseResult = GameCameraActivate((FGameCamera*)Cam, ViewRect4);

    SetLetterboxTarget(1.0f, kLetterboxSpeed);

    // The ship and the server are told the view has gone cinematic.
    NotifyCinematicView(true);

    return bBaseResult;
}

void ScriptCameraDeactivate(FScriptCamera* Cam)
{
    // 0x0051e660. The rest runs whether or not there was a script.
    TearDownScript(Cam);

    // The base deactivate is the empty IDPMsgHandler::OnDisconnect.

    NotifyCinematicView(false);

    SetLetterboxTarget(0.0f, kLetterboxSpeed);
}

void ScriptCameraUpdate(FScriptCamera* Cam, float DeltaSeconds)
{
    // 0x0051e740. No script: the camera keeps its last world transform.
    if (Cam->Script == nullptr)
        return;

    // Whole milliseconds, truncated.
    const int DeltaMs = static_cast<int>(static_cast<double>(DeltaSeconds) * kSecondsToMilliseconds);
    ScriptOf(Cam->Script)->Slot26Tick(DeltaMs);

    // The camera sink (+0xfc, no null test) is read back through its slots, not off the fields.
    float LocalPosition[3];
    float LocalOrientation[9];
    Cam->CameraTransform->GetPosition(LocalPosition);        // 0x14
    Cam->CameraTransform->GetOrientation(LocalOrientation);  // 0x18

    // The script's transform is composed ONTO the stored starting one, so a script written
    // around the origin lands wherever the setup put the camera.
    FTransform Local;
    memcpy(&Local.Rotation, LocalOrientation, 9 * sizeof(float));
    memcpy(&Local.Translation, LocalPosition, 3 * sizeof(float));
    FTransform World;
    ScriptCameraMathEngine()->MulTransform(World, AsTransform(Cam->Orientation), Local);

    FSpaceCamera* const Base = (FSpaceCamera*)Cam;
    memcpy(Base->World, &World.Rotation, 9 * sizeof(float));
    memcpy(Base->WorldPos, &World.Translation, 3 * sizeof(float));
    Base->bViewDirty = 1;
}

char const* FScriptCamera::GetName()
{
    // 0x0051e070.
    return ScriptCameraName();
}

bool FScriptCamera::IsFinished()
{
    return ScriptCameraIsFinished(this);
}

bool FScriptCamera::Activate(int const* ViewRect4)
{
    return ScriptCameraActivate(this, ViewRect4);
}

void FScriptCamera::Deactivate()
{
    ScriptCameraDeactivate(this);
}

void FScriptCamera::Update(float DeltaSeconds)
{
    ScriptCameraUpdate(this, DeltaSeconds);
}
