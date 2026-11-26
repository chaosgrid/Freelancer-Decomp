// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// IScriptEngine scene: entities and events loaded from Lua, plus the playback
// clock. Layout: docs/re/thorn.md and thorn_thn_format.md.

#include "Thorn.h"
#include "thorn_controls.h"
#include <list>
#include <map>
#include <memory>
#include <string>
#include <optional>
#include <set>
#include <vector>

static const int kThornPlaybackLoaded = 2;
static const int kThornPlaybackReset = 3;
static const int kThornPlaybackDone = 4;
static const int kThornPlaybackPaused = 5;
static const int kThornPlaybackPlay = 6;
static const int kThornPlaybackLoop = 7;

// The entity / event type constants, FThornLightProps, FThornMotionPathDesc and
// FThornEntitySnap are in src/include/thorn_iface.h (through Thorn.h).

static const int kThornTargetRoot = 0;
static const int kThornTargetHardpoint = 1;
static const int kThornTargetPart = 2;

static const int kThornFlagUseScriptDuration = 1;
static const int kThornFlagPosition = 2;
static const int kThornFlagOrientation = 4;
static const int kThornFlagLookAt = 8;
static const int kThornFlagQuatOrient = 0x10;
static const int kThornFlagEntityRelative = 0x20;
static const int kThornFlagParentChild = 0x40;
static const int kThornFlagOrientRelative = 0x80;

static const int kThornSpatialBitPos = 0x1;
static const int kThornSpatialBitOrient = 0x2;
static const int kThornSpatialBitAxisRot = 0x4;

// Type 5 load table PTR_DAT_06f5e148: fovh/aspect/near/far -> bits 1/2/4/8
// (not the GET order).
static const int kThornCamBitFovh = 0x1;
static const int kThornCamBitAspect = 0x2;
static const int kThornCamBitNear = 0x4;
static const int kThornCamBitFar = 0x8;

static const int kThornLightBitColor = 0x1;
static const int kThornLightBitDirection = 0x2;
static const int kThornLightBitRange = 0x4;
static const int kThornLightBitCutoff = 0x8;
static const int kThornLightBitOn = 0x10;
static const int kThornLightBitInfinite = 0x20;
static const int kThornLightBitAtten = 0x40;
static const int kThornLightBitTheta = 0x80;
static const int kThornLightBitType = 0x100;
static const int kThornLightBitSpecular = 0x200;
static const int kThornLightBitAmbient = 0x400;
static const int kThornLightBitDiffuse = 0x800;

// Retail Csys: orient 3x3 at +0, pos at +0x24 (FUN_06f3b970; event +0x4); a Transform.
struct FThornCsys : Transform
{
};

static_assert(sizeof(FThornCsys) == 0x30, "Csys is the orientation then the position");

struct FThornUserPropLess
{
    bool operator()(const std::string& Left, const std::string& Right) const;
};

typedef std::map<std::string, std::string, FThornUserPropLess> FThornUserProps;

// START_IK payload as the Lua parser builds it (port-side; no retail layout).
struct FThornIkEventDesc
{
    int Desc[11];
    int CountToRoot;
    int LookAtUpAxis;
    int LookAtFrontAxis;
    float Damping;
    unsigned char bPointAt;
    unsigned char bMoveTo;
    unsigned char Pad[2];
    float TransitionDuration;
    const char* EndEffector;
    const char* TargetPart;
    int TargetType;
    int IkId;
};

class FThornEntity;

// Circular list of linked monitor entities. Walkers read Next only after each
// callback returns, as FUN_06f41490/06f414d0/06f41540 do.
struct FThornMonitorLinkNode
{
    FThornMonitorLinkNode* Next;
    FThornMonitorLinkNode* Previous;
    FThornEntity* Entity;
};

class FThornMonitorLinks
{
private:
    FThornMonitorLinkNode InactiveSentinel;
    std::unique_ptr<FThornMonitorLinkNode> OwnedSentinel;

public:
    FThornMonitorLinks();
    ~FThornMonitorLinks();
    void Initialize();
    void Reset();
    void Erase(FThornMonitorLinkNode* Node);
    void Clear();
    FThornMonitorLinks(const FThornMonitorLinks&) = delete;
    FThornMonitorLinks& operator=(const FThornMonitorLinks&) = delete;

    FThornMonitorLinkNode* Sentinel;
    unsigned int Count;
};

// Scene entity (plain C++, not DACOM). Retail has one class per type (vtables
// PTR_FUN_06f5b05c / 06f5b084 / ...); slot 3 (+0x0c) / slot 4 (+0x10) are
// SetPos/SetOrient (FUN_06f40900 / FUN_06f40ab0, CAMERA FUN_06f41490 / ...).
class FThornEntity
{
public:
    FThornEntity();
    FThornEntity(int Type, const FThornEntitySnap* Desc, FThornScene* OwnerScene);
    virtual ~FThornEntity() noexcept(false);
    void ReleaseCommonEntityState();
    char* AssignEntityName(const char* Source);
    char* AssignEntityTemplate(const char* Source);
    virtual void InnerUnused1();
    virtual void InnerApplyDesc(const FThornEntitySnap* SnapIn);
    Vector* InnerGetPos();
    Matrix* InnerGetOrient();
    virtual void InnerSetPos(const Vector* Pos);
    virtual void InnerSetOrient(const Matrix* Orient);

    // FUN_06f423d0: refresh the live audio payload, then copy all eight words out.
    void CaptureAudioProps(float* Out8);

    // FUN_06f42210: SOUND defaults (ain/aout = 0x168) before the descriptor
    // payload is applied.
    void InitializeSoundDefaults();

    // FUN_06f41a20: releases the light controls before common entity cleanup.
    void ReleaseLightControls();

    // Type-specific controls, released before the common data and User/Geo.
    void ReleaseDerivedControls();
    // Entity vtable +0x24 (unbind); not the destructor's cleanup.
    void InnerUnbindControls();

    // FUN_06f433c0. Leaves ReverbControl held until destruction, as retail does.
    void ReleaseSceneBoundControls();

    // FUN_06f42c60: the motion-path three-control teardown.
    void ReleaseMotionPathControls();

    // FUN_06f427d0: called once by the MOTION_PATH constructor (FUN_06f426f0)
    // after ApplyDesc. Retains the descriptor's path object again (+0xe8) and
    // queries "1.11_IMotionPath" from it into MotionPath (+0xd0).
    // UpdateEntityFromDesc never re-enters this; Attach later rebinds
    // MotionPath from the engine object.
    void BindMotionPathFromDescObject();

private:
    // Distinct from the published descriptor pointers; the constructor's
    // tail memset skips these.
    std::optional<std::string> NameStorage;
    std::optional<std::string> TemplateStorage;
public:
    FThornMonitorLinks MonitorLinks;
    std::optional<FThornUserProps> UserProps;
    FThornEntitySnap Snap;
    // Entity +0x38 / +0x44 (FUN_06f40c40 / FUN_06f40c60): the pose loaded by
    // the last ApplyDesc, in the parent Csys when there is one. SetPos/
    // SetOrient and the slot-5 refresh update Snap only; IScriptEngine slots
    // 10/11 (LAB_06f38950 / LAB_06f38a80), and so the serializer, read these.
    Vector LoadPos;
    Matrix LoadOrient;
    FThornCsys* ParentCsys;
    int BindState;
    unsigned char bBound;
    // Type whose controls are currently bound; the descriptor type may have
    // changed since Attach.
    int BoundControlType;
    IGeoTransformControl* GeoControl;
    ICameraControl* CameraControl;
    ICompoundControl* CompoundControl;
    IIKControl* IkControl;
    ILightControl* LightControl;
    ID3DLightControl* D3dLightControl;
    ISoundControl* SoundControl;
    IUserEventControl* UserEventControl;
    ISubtitleControl* SubtitleControl;
    IFogControl* FogControl;
    IReverbControl* ReverbControl;
    IPSysControl* PsysControl;
    IMotionPath* MotionPath;
    // Parse-time reference only; the owning +0xcc is Snap.MotionPath.MotionPath.
    IPersistString* ParsedMotionPathDescObject;
    // MOTION_PATH +0xe8: the path object retained at construction, separate
    // from +0xcc; FUN_06f42cb0 releases both before PathType and +0xd0.
    IPersistString* MotionPathBoundObject;
    // Set when constructed as MOTION_PATH (not by the Lua staging path).
    bool bMotionPathDescriptorOwner;
    // Before this is set, destruction undoes only the string, the runtime
    // interface and the common base.
    bool bMotionPathConstructionComplete;
    IScriptControl* ScriptControl;


    // Type the entity was constructed as; Snap.Type can change on updates.
    int DescriptorOwnerType;
    // FUN_06f41540 / 06f460f0: runtime +0xec..+0xf8, separate from Snap +0xd4..+0xe0.
    float CameraRuntimeProps[4];

    // Type-specific save/load (FUN_06f2aa10 / FUN_06f28a60); not in the 0x54 common snapshot.
    float FloorHeight;
    int LightMask;
    int LightOn;
    int LightInfinite;
    int LightType;
    float LightColor[3];
    float LightDiffuse[3];
    float LightSpecular[3];
    float LightAmbient[3];
    Vector LightDirection;
    float LightRange;
    float LightCutoff;
    float LightTheta;
    float LightAtten[3];
    // FUN_06f41960 clears the two light-entity runtime words at retail +0x158/+0x15c.
    int LightRuntimeState[2];
    // The light payload is initialized in the constructor, not on each update.
    unsigned char bLightStateInitialized;
    // FUN_06f41b00 copies the 0x24-dword descriptor to retail this+0x160.
    FThornLightProps LightEntityDescriptor;
    float Audio[8];
    int AudioMask;
    float PsysRuntimeSparam; // Retail +0xd0; retained input lives in Snap.PsysSparam.
    char* PathType;
    std::size_t PathTypeCapacity;
    const char* MotionPathBoundType; // Retail +0xe4: constructor cache of PathType.
    char* PathData;
    int SceneAmbient[3];
    // Entity +0x11c: the live 15-dword scene blob (ambient 0..2, up/front axes
    // 5/6, the 8-dword fog block at [7] = +0x138); Snap.SceneBlob is the
    // descriptor's copy.
    unsigned int LiveSceneBlob[15];
    // FUN_06f431d0 mirrors blob words 5/6 (+0x130/+0x134, the up/front axes)
    // through these; the scene binds them to its own axis fields.
    unsigned int* UpAxisSink;
    unsigned int* FrontAxisSink;
};

class FThornScene;

// Owned event string (retail: VC6 basic_string); the published descriptor
// pointers are separate.
struct FThornConnectString
{
    std::unique_ptr<char[]> Data;
    size_t Length = 0;
    size_t Capacity = 0;
    FThornConnectString() = default;
    FThornConnectString(const FThornConnectString&) = delete;
    FThornConnectString& operator=(const FThornConnectString&) = delete;
    bool Assign(const char* Source);
    char* GetPublishedData();
    void Reset();
};

// C++ event vtable (not DACOM). Slot 2 = GetDesc → this+0x28 (LAB_06f4a470).
// Member order matches FUN_06f43940 + desc copy @ +0x28.
class FThornEvent
{
public:
    FThornEvent();
    explicit FThornEvent(int DescriptorStorageType); // storage selection only; no descriptor callbacks
    FThornEvent(int Type, const void* Desc);
    virtual ~FThornEvent() noexcept(false);
    virtual void ApplyDesc(const void* Desc);
    void ApplyCommonDesc(const void* Desc);
    void FinalizeDesc();
    void ApplySetCameraDesc(const void* Desc);
    void ApplyCameraPropAnimDesc(const void* Desc);
    void ApplyStartSoundDesc(const void* Desc);
    void ApplyFloorHeightDesc(const void* Desc);
    void ApplyStartPsysDesc(const void* Desc);
    void ApplyAudioPropAnimDesc(const void* Desc);
    void ApplyPsysPropAnimDesc(const void* Desc);
    void ApplyUserEventDesc(const void* Desc);
    virtual void* GetDesc();
    virtual unsigned char Start(FThornScene* Scene, int TimeMs);
    void DispatchSubtitleControl(FThornScene* Scene, bool bShow);
    virtual unsigned char Stop(FThornScene* Scene);
    virtual unsigned char StillRunning(int TimeMs, FThornScene* Scene);

    // Retail's deleting destructor, called from the scene.
    static void DestroyNative(FThornEvent*& Event);

    // FUN_06f49d00: subtitle descriptor; a type mismatch keeps the old strid.
    void ApplySubtitleDesc(const void* Desc);

    // LAB_06f49b90: event progress (also the type-18 vtable's).
    float EvaluateProgress(int TimeMs) const;

    // LAB_06f493e0: REVERB never takes the period branch and always rounds
    // its ratio before the curve call or return.
    float EvaluateProgressNoPeriodWrap(int TimeMs) const;
    void EvaluateProgressExtended(int TimeMs, FThornExtendedProgress* Result,
                                  bool bNoPeriodWrap) const;

    // Event vtable slot 7 (+0x1c): constant type name ("MOTION", "IK", ...
    // "SUBTITLE"); the 19 per-type retail getters collapse into this switch.
    const char* TypeName() const;

    // LAB_06f44ec0: resolve both endpoints, link the monitor, copy camera
    // properties, then the source's live pose.
    unsigned char StartSetCamera(FThornScene* Scene);
    unsigned char StartCameraPropAnim(FThornScene* Scene);
    unsigned char StartFogPropAnim(FThornScene* Scene);
    unsigned char StartPsysPropAnim(FThornScene* Scene);
    unsigned char StartReverbPropAnim(FThornScene* Scene);
    unsigned char TickReverbPropAnim(int TimeMs, FThornScene* Scene);
    unsigned char StartAudioPropAnim(FThornScene* Scene);
    unsigned char TickAudioPropAnim(int TimeMs, FThornScene* Scene);
    unsigned char TickFogPropAnim(int TimeMs, FThornScene* Scene);
    unsigned char StartAttachEntity(FThornScene* Scene);
    unsigned char StartSpatialPropAnim(FThornScene* Scene);
    unsigned char TickCameraPropAnim(int TimeMs, FThornScene* Scene);
    unsigned char TickPsysPropAnim(int TimeMs, FThornScene* Scene);
    unsigned char TickLightPropAnim(int TimeMs, FThornScene* Scene);
    unsigned char TickSpatialPropAnim(int TimeMs, FThornScene* Scene);
    unsigned char TickAttachEntity(int TimeMs, FThornScene* Scene);
    unsigned char TickPathAnim(int TimeMs, FThornScene* Scene);
    unsigned char StartFloorHeightAnim(FThornScene* Scene);
    unsigned char TickFloorHeightAnim(int TimeMs, FThornScene* Scene);

    void ApplyIkExtendedDesc(const FThornIkEventDesc* Desc);
    void ApplyStartIkDesc(const void* Desc);
    void SetIkEndEffector(const char* Name);
    void SetIkTargetPart(const char* Name);

    // FUN_06f484e0: type 12, one descriptor word after the common eleven.
    void ApplySubSceneDesc(const void* Desc, float StartTimeValue);

    // FUN_06f477e0: type 8 (attach), six words after the common eleven
    // (offset xyz, TargetType, LookAtUpAxis, LookAtFrontAxis) plus a
    // ref-counted target-part string. A type mismatch resets the six words
    // to the constructor defaults.
    void ApplyAttachDesc(const void* Desc);
    void ApplyConnectDesc(const void* Desc);
    // FUN_06f48050: the setter runs twice, then the event ID is written.
    void InitializeConnectDesc(const void* Desc);
    void SetConnectHardpoint(const char* Name);
    void SetConnectParentHardpoint(const char* Name);

    // FUN_06f450a0: type 6 (path animation), eight words after the common
    // eleven (StartPercent, StopPercent, PercentDelta, PathOffset xyz,
    // LookAtUpAxis, LookAtFrontAxis). A type mismatch resets them to the
    // constructor defaults.
    void ApplyPathAnimDesc(const void* Desc);

    // Audio subclass cleanup: releases the +0x4c curve (ParamCurve).
    void ReleaseAudioOwnedState();

    // FUN_06f46010: releases only the curve before common cleanup.
    void ReleaseCameraOwnedState();

    // FUN_06f46550: releases the curve and the ref-counted target-part
    // string before common cleanup.
    void ReleaseSpatialOwnedState();

    // Attach subclass cleanup: curve and target-part string.
    void ReleaseAttachOwnedState();

    // FUN_06f43ec0: releases the two IK strings.
    void ReleaseIkOwnedState();

    // FUN_06f43a70: releases the two motion strings.
    void ReleaseMotionOwnedState();

    // PATH progress (FUN_06f454f0): curve * Scale + StartPercent.
    float EvaluateControlBackedCurve(float Scale, int TimeMs);

    // FUN_06f45660: type 4 (light prop anim) descriptor.
    void ApplyLightEventDesc(const void* Desc);

    // FUN_06f465f0: type 7 (SPATIAL PROP ANIM). After the common eleven: mask,
    // target orientation/position, optional quaternion (only with
    // kThornFlagQuatOrient, else derived from the matrix), axis-rotation
    // angle/axis, target part/type.
    void ApplySpatialPropAnimDesc(const void* Desc);

    // FUN_06f43af0: START_MOTION descriptor.
    void ApplyMotionDesc(const void* Desc);

    // FUN_06f48cc0 / FUN_06f49150: types 16 / 17, mask plus eight / four words
    // after the common eleven.
    void ApplyFogPropAnimDesc(const void* DescIn);
    void ApplyReverbPropAnimDesc(const void* DescIn);

    FThornCsys* ParentCsys;
    unsigned char bOwnsParentCsys;
    const unsigned int* SceneUpAxis;
    const unsigned int* SceneFrontAxis;
    int StartTimeMs;
    int UniqueId;
    int Type;
    int PrimaryIndex;
    int SecondaryIndex;
    int EndTimeMs;
    int Unused20;
    const bool bFloorEventOwner;
    // Type the event was constructed as; Type may change later.
    const int NativeEventType;
    // Per-type descriptor storage; Desc/ParamCurve/... below reference the
    // one selected at construction.
    FThornUserEventDesc UserEventDesc;
    FThornFogEventDesc FogDesc;
    FThornPsysPropEventDesc PsysPropDesc;
    FThornAudioPropEventDesc AudioPropDesc;
    FThornLightEventDesc LightDesc;
    FThornReverbEventDesc ReverbDesc;
    FThornCameraEventDesc CameraDesc;
    FThornEventDesc SetCameraDesc; // Type 2, common words only (FUN_06f44e60).
    FThornFloorEventDesc FloorDesc; // Type 18, 56 bytes (FUN_06f49570).
    FThornPathEventDesc PathDesc; // Type 6, 76 bytes (FUN_06f450a0).
    IParamCurve*& ParamCurve;
    FThornEventDesc& Desc;
    int& PcurvePeriod;
    Vector TargetPos;
    Matrix TargetOrient;
    Vector CapturePos;
    Matrix CaptureOrient;
    Quaternion TargetQuat;
    Quaternion CaptureQuat;
    int SpatialMask;
    FThornSpatialEventDesc SpatialDesc;
    float AxisRotAngle;
    int AxisRotAxis;
    float AxisRotApplied;
    float SpatialWrapLast;
    bool bHasTargetPos;
    bool bHasTargetOrient;
    bool bCaptured;

    char* UserEventString;
    int& CamMask;
    // +0x58: fovh/aspect/near/far. The Start capture (+0x68) GETs slots
    // 8/11/12/10: fovh/near/far/aspect.
    float (&CamTarget)[4];
    float CamCapture[4];
    int& LightMask;
    FThornLightProps& LightTarget;
    FThornLightProps LightCapture;
    float& StartPercent;
    float& StopPercent;
    float& PercentDelta;
    Vector& PathOffset;
    char* Animation;
    char* LockedBone;
    float TransTime;
    float TimeScale;
    float Weight;
    float Heading;
    float TransScale;
    float MotionStartTime;
    unsigned char Field_0x40; // Motion descriptor +0x40; meaning unknown.
    int Field_0x50; // Motion descriptor +0x50; meaning unknown.
    float SubSceneStartTime;
    char* Hardpoint;
    char* ParentHardpoint;
    FThornConnectString ConnectHardpointStorage;
    FThornConnectString ConnectParentHardpointStorage;
    bool bConnectStorageActive = false;
    void AdoptConnectStringStorage();
    void ReleaseConnectStringStorage();
    char* EndEffector;
    const char* LegacyTargetPart;
    const char*& TargetPart;
    FThornConnectString IkEndEffectorStorage;
    FThornConnectString IkTargetPartStorage;
    bool bIkStorageActive = false;
    FThornConnectString MotionAnimationStorage;
    FThornConnectString MotionLockedBoneStorage;
    bool bMotionStorageActive = false;
    FThornConnectString AttachTargetPartStorage;
    bool bAttachStorageActive = false;
    FThornConnectString SpatialTargetPartStorage;
    bool bSpatialStorageActive = false;
    void AdoptSpatialStringStorage();
    void SetSpatialTargetPart(const char* Name);
    FThornConnectString FloorTargetPartStorage;
    bool bFloorStorageActive = false;
    void AdoptFloorStringStorage();
    void SetFloorTargetPart(const char* Name);
    void ReleaseFloorOwnedState();
    int LegacyTargetType;
    int& TargetType;
    int& LookAtUpAxis;
    int& LookAtFrontAxis;
    float PointAt[3];
    unsigned char bPointAt;
    unsigned char bMoveTo;
    Transform AttachXform;
    // Written by the primary hardpoint callback; kept between IK ticks.
    Transform IkEffectorXform;
    int IkId;
    int CountToRoot;
    float Damping;
    float TransitionDuration;
    float& SparamTarget;
    float SparamCapture;
    int& PsysMask;
    int& AudioMask;
    FThornAudio8& AudioTarget;
    FThornAudio8 AudioCapture;
    int& FogMask;
    FThornFog8& FogTarget;
    int FogCaptureOn;
    float FogCaptureColor[3];
    float FogCaptureStart;
    float FogCaptureEnd;
    float FogCaptureDensity;
    FThornFog8 FogCapturePacked;
    int& ReverbMask;
    FThornReverb4& ReverbTarget;
    float ReverbCapture[4];
    float& FloorTarget;
    float FloorCapture;
    float FloorWrapLast;
    int Strid;
    FThornSubtitleEventDesc SubtitleDesc;
    FThornAttachEventDesc AttachDesc;
    void PackAttachDesc();
    bool bUserDescriptorOwner = false; // USER_EVENT: FUN_06f44be0 releases no curve.
    int SoundStartTime;
    FThornSubSceneEventDesc SubSceneDesc;
    FThornSoundEventDesc SoundDesc;
    void PackSoundDesc();

    // START_MOTION's published descriptor (thorn_iface.h FThornMotionEventDesc): retail keeps
    // the common words and motion fields contiguous at event +0x28 and hands that to
    // ICompoundControl slots 3 / 4 and IScriptEngine slots 15 / 16. PackMotionDesc refreshes
    // it before each use.
    FThornMotionEventDesc MotionDesc;
    void PackMotionDesc();

    // START_IK's published descriptor (thorn_iface.h FThornStartIkDesc), the IK twin of
    // MotionDesc: retail FUN_06f43f40 lays the IK fields out after the common words at event
    // +0x28, with the event's own address at +0x2c, and IIKControl slots 3 / 4 / 5 and
    // IScriptEngine slots 15 / 16 receive that. Refreshed by PackIkDesc before each use.
    FThornStartIkDesc IkDesc;
    void PackIkDesc();

    // CONNECT_HARDPOINTS' published descriptor (thorn_iface.h FThornConnectEventDesc): retail
    // keeps the common words and the two hardpoint name pointers contiguous at event +0x28
    // (+0x54 / +0x58 = desc +0x2c / +0x30) and hands event +0x28 to ICompoundControl slot 7
    // (LAB_06f48340) and slot 8 (LAB_06f483c0). Refreshed by PackConnectDesc before each use.
    FThornConnectEventDesc ConnectDesc;
    void PackConnectDesc();
};

class FThornScene
{
public:
    FThornScene();
    ~FThornScene();

    void Clear();
    FThornEntity* AddEntity();
    FThornEvent* AddEvent(int DescriptorStorageType = 0);
    void BindEntitySceneAxes(FThornEntity* Entity);
    void RegisterFrameEntity(FThornEntity* Entity);
    void BindEventSceneAxes(FThornEvent* Event);
    FThornCsys& ForwardCsys() { return SceneForwardCsys_; }
    void BuildInverseCsys(Transform* Out) const;
    void MarkLoaded() { PlaybackState_ = kThornPlaybackLoaded; }
    void FinalizeLoad(int DurationMs);

    int FindEntityByName(const char* Name) const;
    FThornEntity** FindEntityEntry(int Index);
    FThornEntity* EntityAt(int Index);
    const FThornEntity* EntityAt(int Index) const;

    unsigned long EntityCount() const;
    unsigned long LiveEntityCount() const;
    void* EntityHandle(int Index);
    FThornEntitySnap* EntitySnap(int Index);
    FThornEntitySnap* EntitySnapInorder(int Index);
    unsigned long UserPropCount(int Index) const;
    const char* UserPropKey(int Index, int N) const;
    GENRESULT GetEntityPos(int Index, Vector* OutVec3);
    GENRESULT GetEntityOrient(int Index, Matrix* OutMat9);
    GENRESULT Attach(int Index, void* EngineObj);
    GENRESULT Detach(int Index);
    void PushBoundGeoFromScene(const Transform& OldInverse, bool bOrient);
    GENRESULT UserProp(void* Handle, const char* Key, char** Out);
    GENRESULT SetUserProp(int Index, const char* Key, const char* Value);
    GENRESULT AddEntityFromDesc(void* Desc, int* OutIndex);
    GENRESULT RemoveEntityAt(int Index);
    GENRESULT UpdateEntityFromDesc(int Index, void* Desc);
    GENRESULT AddEventFromDesc(const void* Desc, int* OutId);
    GENRESULT RemoveEventById(int Id);
    GENRESULT SaveToFileSystem(void* FileSystem, IThornComContext* ComContext,
                               unsigned int SavedDurationMs);

    unsigned long EventCount() const { return static_cast<unsigned long>(MasterCount_); }
    void* EventDescByIndex(int Index);
    void* EventDescById(int Id);
    bool HasEventType(int Type) const;

    int DurationMs() const { return DurationMs_; }
    void SetDuration(int Ms) { DurationMs_ = Ms; }
    int ClockMs() const { return ClockMs_; }
    int PlaybackState() const { return PlaybackState_; }
    unsigned char LoopFlag() const { return bLoop_; }
    // LAB_06f39430: stores the raw byte at +0x30, not normalized to 0/1.
    void SetLoop(unsigned char Value) { bLoop_ = Value; }

    GENRESULT StartPlayback(int Arg);
    GENRESULT StopPlayback();
    GENRESULT PausePlayback();
    GENRESULT Tick(int DtMs);

private:
    friend class FThornEntity;
    void GrowMaster();
    void GrowPending();
    void GrowActive();
    void RebuildPending();
    void StopAll();
    void DispatchWindow(int OldTime, int NewTime);
    struct FActiveGroup
    {
        std::vector<FThornEvent*> Events;
        bool bVisited = false;
    };
    void RebuildActiveGroups();
    void EmitActiveGroup(std::map<int, FActiveGroup>& Groups,
                         std::map<int, FActiveGroup>::iterator Group);
    void AppendActiveGroupEvent(FThornEvent* Event);
    void FrameUpdateEntities(int TimeMs);
    void UpdateActive(int TimeMs);
    void DestroyEntity(FThornEntity* Entity);
    void ApplyEntitySnap(FThornEntity* Entity, const FThornEntitySnap* Snap);
    bool EntityExists(int Index) const;
    void CompactPendingFrom(FThornEvent** SelectedBegin, FThornEvent** SelectedEnd);
    void DrainTimedStops(const int* TimeMs, bool bBeforeStartTime, std::set<int>* UpdatedTimes);
    FThornEvent** EraseMasterEventAt(FThornEvent** Slot);
    static int CompareEventStart(const void* Left, const void* Right);
    static bool EventAllowsMissingSecondary(int Type);

    std::map<int, FThornEntity*> Entities_;
    int EntityCount_;
    FThornEvent** Master_;
    int MasterCount_;
    int MasterCap_;

    FThornEvent** Pending_;
    int PendingCount_;
    int PendingCap_;
    int PendingBegin_;

    FThornEvent** Active_;
    int ActiveCount_;
    int ActiveCap_;

    std::optional<std::list<float>> StopTimes_;
    // Retail +0xf8/+0xfc; destroyed before StopTimes_.
    std::optional<std::list<FThornEntity*>> FrameEntities_;

    int DurationMs_;
    int ClockMs_;
    int ClockAfterDt_;
    int PlaybackState_;
    unsigned char bLoop_;
    int LoopCarryMs_;
    unsigned int SceneUpAxis_;
    unsigned int SceneFrontAxis_;
    FThornCsys SceneForwardCsys_;
};

// FUN_06f41840 light defaults; FUN_06f2eb20 compact-record conversion.
void ThornInitLightEntityDefaults(FThornLightProps* Props);
void ThornConvertCompactLight(FThornLightProps* Blob);
void ThornCopyLightPropsToEntity(FThornEntity* Entity, const FThornLightProps* Props);
// Fill the published descriptor tail (Snap past +0x44) from the entity's native fields.
// Called once by the Lua entity loader, after every per-type field is read.
void ThornPublishEntityDescTail(FThornEntity* Entity);
