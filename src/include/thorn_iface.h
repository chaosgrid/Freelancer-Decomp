// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "dacom.h"
#include "math3d.h"

// 8-slot IMotionPath (SplinePath.cpp FSplinePath). T is the path parameter.
struct IMotionPath : IDAComponent
{
    virtual GENRESULT DACOM_API GetTransformAt(float T, Transform* OutXform) = 0;      // 3  RET 0xC, 3x3 + pos
    virtual GENRESULT DACOM_API GetPositionAt(float T, Vector* OutVec3) = 0;        // 4  RET 0xC, LAB_06f343f0
    virtual GENRESULT DACOM_API GetOrientationAt(float T, Quaternion* OutQuat) = 0;     // 5  RET 0xC, quaternion
    virtual GENRESULT DACOM_API GetTangentAt(float T, Vector* OutVec3) = 0;         // 6  RET 0xC, LOOK_AT direction
    // 7  RET 0x8 LAB_06f34670: *OutLength = inner+0x4 (FUN_06f224e0), the
    // un-normalised total arc length accumulated by FUN_06f22630; -1 when the
    // inner spline core has not been created yet.
    virtual GENRESULT DACOM_API GetPathLength(float* OutLength) = 0;
};


// thorn.dll public ABI: IScriptEngine, the THN playback engine other modules drive.
// Source: EXE/Disasm/thorn.dll, docs/re/thorn.md. The rest of the port's
// Thorn internals stay in src/Thorn/Thorn.h.
//
// The file is thorn_iface.h and not thorn.h on purpose: NTFS is case-insensitive, so a
// public "thorn.h" would be picked up instead of src/Thorn/Thorn.h by every include inside
// the module itself.
//
// Common (common_roomdata.cpp) and freelancer's Gf script layer (gf_script / gf_script_object /
// gf_script_spawn, the Thorn_* component classes in gf_thorn_*.cpp, gf_character_object,
// space_camera_script) both drive this interface (docs/re/cross_dll_protocol.md).

// ---------------------------------------------------------------------------------------
// Entity and event records the slots below hand out and take in. Declared here once, for
// thorn.dll (src/Thorn/ThornScene.h) and freelancer's Gf script layer (gf_script_object.h,
// gf_thorn_*.h); no module may keep a private copy.
// ---------------------------------------------------------------------------------------

// Entity types (snapshot +0x00). Retail: the per-type entity constructors of the scene
// loader (thorn.dll FUN_06f25xxx switch), thorn_thn_format.md.
static const int kThornEntityCompound = 1;
static const int kThornEntityDeformable = 2;
static const int kThornEntityCamera = 3;
static const int kThornEntityMonitor = 4;
static const int kThornEntityLight = 5;
static const int kThornEntitySound = 6;
static const int kThornEntityMarker = 7;
static const int kThornEntityHardpoint = 8;
static const int kThornEntityScene = 9;
static const int kThornEntitySubScene = 10;
static const int kThornEntityMotionPath = 11;
static const int kThornEntityDeleted = 12; // script keyword DELETED; never a live entity
static const int kThornEntityPsys = 13;

// Event types (event descriptor +0x00), the switch at thorn.dll 0x06f25953.
static const int kThornEventTypeMin = 1;
static const int kThornEventTypeMax = 19;
static const int kThornTypeUserEvent = 1;
static const int kThornTypeSetCamera = 2;
static const int kThornTypeStartSound = 3;
static const int kThornTypeLightPropAnim = 4;
static const int kThornTypeCameraPropAnim = 5;
static const int kThornTypePathAnim = 6;
static const int kThornTypeSpatialPropAnim = 7;
static const int kThornTypeAttachEntity = 8;
static const int kThornTypeConnectHardpoints = 9;
static const int kThornTypeStartMotion = 10;
static const int kThornTypeStartIk = 11;
static const int kThornTypeStartSubScene = 12;
static const int kThornTypeStartPsys = 13;
static const int kThornTypePsysPropAnim = 14;
static const int kThornTypeAudioPropAnim = 15;
static const int kThornTypeFogPropAnim = 16;
static const int kThornTypeReverbPropAnim = 17;
static const int kThornTypeFloorHeightAnim = 18;
static const int kThornTypeSubtitle = 19;
// Script keyword START: a SOUND primary becomes START_SOUND, any other START_MOTION (the load parser).
static const int kThornTypeScriptStart = 23;

// FThornLightProps::Type, the script keywords L_POINT / L_SPOT / L_DIRECT.
enum EThornLightType
{
    kThornLightPoint = 1,
    kThornLightSpot = 2,
    kThornLightDirect = 3,
};

// FUN_06f27fe0 param_2: 0x24 dwords. Color = _ftol ints; cutoff/theta stored in radians.
// The LIGHT entity's descriptor tail (+0x44) and the light event's target.
struct FThornLightProps
{
    int Color[3];
    Vector Direction;
    float RangeTemp;
    float CutoffTemp;
    int On;
    int Infinite;
    int Type;
    float Diffuse[4];
    float Specular[4];
    float Ambient[4];
    int Unused17[6];
    float Range;
    int Unused1e;
    float Atten[3];
    float Theta;
    float Cutoff;
};

static_assert(sizeof(FThornLightProps) == 0x90, "FUN_06f27fe0 lightprops is 0x24 dwords");

// IPersistString facet retained by MOTION_PATH descriptors. RET includes the interface receiver.
struct IPersistString : IDAComponent
{
    virtual GENRESULT DACOM_API Load(const char* Text) = 0;                      // +0x0c RET 8
    virtual GENRESULT DACOM_API Save(char* Buffer, unsigned long Size) = 0;      // +0x10 RET 12
    virtual unsigned long DACOM_API EstimateSize() = 0;                          // +0x14 RET 4
};

// The MOTION_PATH entity's descriptor tail (+0x44). Retail stores a VC6 string plus a
// path-control interface there; the port keeps the observed semantic payload.
struct FThornMotionPathDesc
{
    const char* PathType;
    IPersistString* MotionPath;
};

// FThornEntitySnap::Flags bits, the script keywords of an entity's `flags` key. Bits 2, 4 and 8
// mean something different per entity type.
enum EThornEntityFlag
{
    kThornEntityFlagReference = 0x01,         // REFERENCE
    kThornEntityFlagHidden = 0x10,            // HIDDEN
    kThornEntityFlagLitAmbient = 0x02,        // LIT_AMBIENT: COMPOUND / DEFORMABLE / PSYS
    kThornEntityFlagLitDynamic = 0x04,        // LIT_DYNAMIC: COMPOUND / DEFORMABLE / PSYS
    kThornEntityFlagSoundSpatial = 0x02,      // SPATIAL: SOUND
    kThornEntityFlagSoundStream = 0x04,       // STREAM: SOUND
    kThornEntityFlagSoundLoop = 0x08,         // LOOP: SOUND
    kThornEntityFlagFogPropsRemoved = 0x20,   // FOG_PROPS_REMOVED: SCENE
};

// Axis ids of a SCENE's up / front and of LOOK_AT / axis-rotation events, the script keywords
// X_AXIS .. NEG_Z_AXIS. 6 in a scene descriptor means "not given" (replaced when the entity is built).
enum EThornAxis
{
    kThornAxisX = 0,
    kThornAxisY = 1,
    kThornAxisZ = 2,
    kThornAxisNegX = 3,
    kThornAxisNegY = 4,
    kThornAxisNegZ = 5,
    kThornAxisUnset = 6,
};

// An entity descriptor: IScriptEngine slots 8 / 9 hand out the one the entity holds, slots
// 35 / 37 take one in. Retail's per-type entity keeps it contiguous (the LIGHT entity's
// slot-7 accessor LAB_06f4a110 is `LEA EAX,[ECX+0x84]`, and its tail is copied in whole by
// the type's ApplyDesc), so the tail that follows the common 0x44 bytes is the one the
// descriptor was loaded with. The tail is read by type:
//   CAMERA / MONITOR  +0x44 fovh, near, far, aspect
//   COMPOUND / DEFORMABLE +0x44 floor height
//   LIGHT             +0x44 FThornLightProps (0x90)
//   SOUND             +0x44 eight audio floats
//   PSYS              +0x44 the sparam
//   SCENE             +0x44 the 15-dword fog/reverb blob (thorn FUN_06f43110)
//   MOTION_PATH       +0x44 FThornMotionPathDesc
struct FThornEntitySnap
{
    int Type;             // +0x00 kThornEntity*
    int Flags;            // +0x04 bit 0: a reference entity (resolved, not created)
    Vector Pos;           // +0x08
    Matrix Orient;        // +0x14
    char* Name;           // +0x38 the entity's name in the script
    char* TemplateName;   // +0x3c the template (archetype nickname, "PlayerShip", ...)
    unsigned char LtGrp;  // +0x40
    unsigned char SrtGrp; // +0x41
    unsigned char UsrFlg; // +0x42
    unsigned char Pad43;
    union
    {
        struct
        {
            float Fovh;      // +0x44
            float NearPlane; // +0x48
            float FarPlane;  // +0x4c
            float Aspect;    // +0x50
        };
        float FloorHeight;
        FThornLightProps Light;
        float Audio[8];
        float PsysSparam;
        unsigned int SceneBlob[15];
        FThornMotionPathDesc MotionPath;
        float Extra[36]; // the raw tail
    };
};

static_assert(sizeof(void*) != 4 || sizeof(FThornEntitySnap) == 0xd4,
              "the common 0x44 bytes plus the largest tail, the LIGHT props");

// Modern internal output transport: an x87 value, not a retail object layout.
struct FThornExtendedProgress
{
    unsigned char Bytes[10];
};
static_assert(sizeof(FThornExtendedProgress) == 10, "x87 extended value storage");

// Canonical modern DACOM interface: 17 original indices plus 2 output methods.
// DACOM_API applies to every interface method; RET comments below describe retail
// reference arities only, not the modern stdcall ABI (which also passes this).
// Two appended methods are modern internal transport contracts, not retail slots.
struct IParamCurve : IDAComponent
{
    virtual float DACOM_API Evaluate(float T) = 0;                               // 3  RET 4, ST(0)
    virtual unsigned char DACOM_API ReplaceKey(int Id, float F0, float F1, float F2,
                                               float F3) = 0;                    // 4  RET 0x14
    virtual unsigned char DACOM_API ReadKey(int Id, float* Out0, float* Out1, float* Out2,
                                            float* Out3) = 0;                    // 5  RET 0x14
    virtual int DACOM_API AddKey(float F0, float F1, float F2, float F3) = 0;     // 6  RET 0x10
    virtual unsigned char DACOM_API DeleteKey(int Id) = 0;                       // 7  RET 4
    virtual unsigned int DACOM_API GetKeyCount() = 0;                            // 8 RET 0, LAB_06f2f7b0
    virtual int DACOM_API GetKeyId(int Index) = 0;                               // 9  RET 4
    virtual const char* DACOM_API GetClassId() = 0;                              // 10 RET 0
    virtual const char* DACOM_API GetDisplayName() = 0;                          // 11 RET 0
    // 12: Evaluate((Elapsed % Period) / Period), signed IDIV (2f810).
    virtual float DACOM_API WrapEvaluate(int Elapsed, int Period) = 0;          // 12 RET 8
    virtual void DACOM_API Rebuild() = 0;                                        // 13 RET 0
    virtual void* DACOM_API Slot14Dtor(unsigned char DeleteFlag) = 0;            // 14
    virtual int DACOM_API Slot15PersistStub(void* Unused) = 0;                   // 15 RET 4 → 0
    virtual int DACOM_API Slot16PersistStub(void* Unused) = 0;                   // 16 RET 4 → 0
    virtual void DACOM_API EvaluateToExtended(const float* T, FThornExtendedProgress* Output) = 0;
    virtual void DACOM_API WrapEvaluateToExtended(int A, int B, FThornExtendedProgress* Output) = 0;
};

// The common eleven-word event descriptor (retail: event object +0x28, copied by
// FUN_06f438c0). IScriptEngine slots 15 / 16 hand out the event's, slot 38 takes one in.
struct FThornEventDesc
{
    int Type;           // +0x00 kThornType*
    int StartMs;        // +0x04
    int Flags;          // +0x08 bit 1 "loop" for START_MOTION
    int EventFlags;     // +0x0c
    int Field10;        // +0x10 copied by FUN_06f438c0; no loader key for it identified
    int UniqueId;       // +0x14
    int DurationMs;     // +0x18
    int PrimaryIndex;   // +0x1c the entity the event drives
    int SecondaryIndex; // +0x20
    IParamCurve* ParamCurve; // +0x24 curve, or null
    int ParamCurvePeriod;        // +0x28 the pcurve_period key (thorn.dll loader, string 06f5eadc), ftol'd
};

static_assert(sizeof(void*) != 4 || sizeof(FThornEventDesc) == 0x2c,
              "eleven dwords (FUN_06f438c0 copies 0xb)");

// PATH_MOTION: 450a0 copies eight payload words; 4a4b0 exposes their live owner.
struct FThornPathEventDesc
{
    FThornEventDesc Common;
    float StartPercent;
    float StopPercent;
    float PercentDelta;
    Vector Offset;
    int LookAtUpAxis;
    int LookAtFrontAxis;
};
static_assert(sizeof(void*) != 4 || sizeof(FThornPathEventDesc) == 0x4c,
              "PATH_MOTION common and eight payload words");

// CAMERA_PROP: 46060 copies mask+0x2c and fovh/aspect/near/far at +0x30.
// 4a510 returns this same live sixteen-word descriptor.
struct FThornCameraEventDesc
{
    FThornEventDesc Common;
    int Mask;
    float Target[4];
};
static_assert(sizeof(void*) != 4 || sizeof(FThornCameraEventDesc) == 0x40,
              "CAMERA_PROP common, mask and four target words");

// USER_EVENT: FUN_06f44bf0 copies descriptor+0x2c as an opaque dword.
// Lua user_event_string is passed separately to the load-time COM hook;
// it does not populate this word (06f25f49 initializes it to zero).
struct FThornUserEventDesc
{
    FThornEventDesc Common;
    // Opaque external representation: Thorn copies these bits without interpreting them.
    // Thorn does not own any pointer represented by this word.
    unsigned int ExternalPayloadWord;
};

static_assert(sizeof(void*) != 4 || sizeof(FThornUserEventDesc) == 0x30,
              "USER_EVENT common descriptor plus opaque payload");

// ATTACH: FUN_06f477e0; writer2b6f1..2b74b and tick47e13 prove the tail.
struct FThornAttachEventDesc
{
    FThornEventDesc Common;
    Vector Offset;
    int TargetType;
    int UpAxis;
    int FrontAxis;
    const char* TargetPart;
};

static_assert(sizeof(void*) != 4 || sizeof(FThornAttachEventDesc) == 0x48,
              "ATTACH descriptor common eleven words and seven payload words");

// START_SOUND: FUN_06f449b0 copies input+0x2c to event+0x54.
// LAB_06f44a20/GetDesc hand this to the client; LAB_004574d0 computes
// playback offset = SoundStartTime - Common.StartMs + TimeMs.
struct FThornSoundEventDesc
{
    FThornEventDesc Common;
    int SoundStartTime;
};

static_assert(sizeof(void*) != 4 || sizeof(FThornSoundEventDesc) == 0x30,
              "START_SOUND common descriptor plus start_time");

// START_SUB_SCENE: input+0x2c -> event+0x54; getter exposes twelve words.
struct FThornSubSceneEventDesc
{
    FThornEventDesc Common;
    float StartTime;
};

static_assert(sizeof(void*) != 4 || sizeof(FThornSubSceneEventDesc) == 0x30,
              "START_SUB_SCENE common descriptor plus start_time");

// SUBTITLE: FUN_06f49d00 stores word 11 at event +0x54; getter
// LAB_06f4a730 exposes the complete twelve-word descriptor.
struct FThornSubtitleEventDesc
{
    FThornEventDesc Common;
    unsigned int Strid;
};

static_assert(sizeof(FThornSubtitleEventDesc) == 0x30,
              "SUBTITLE common descriptor plus string resource id");

// START_MOTION's descriptor (retail object 0x9c, FUN_06f439b0; the descriptor at +0x28 is
// filled by FUN_06f43af0 and by the loader's Lua reads 0x06f25b5c..0x06f25e5f). This is what
// ICompoundControl slots 3 / 4 receive for a motion event and what slots 15 / 16 hand out.
struct FThornMotionEventDesc
{
    FThornEventDesc Common;       // +0x00 Type == kThornTypeStartMotion
    float StartTime;              // +0x2c "start_time"
    float TransTime;              // +0x30 "trans_time"
    float TimeScale;              // +0x34 "time_scale"
    float Weight;                 // +0x38 "weight"
    char const* Animation;        // +0x3c "animation" (the script name CharacterInfo knows)
    unsigned char Field40;        // +0x40 FUN_06f43af0 copies it; the Lua loader sets no key here
    unsigned char Pad41[3];
    float Heading;                // +0x44 "heading"
    char const* LockedBone;       // +0x48 "locked_bone"
    float TransScale;             // +0x4c "trans_scale"
    int Field50;                  // +0x50 FUN_06f43af0 copies it; the Lua loader sets no key here
};

static_assert(sizeof(void*) != 4 || sizeof(FThornMotionEventDesc) == 0x54,
              "FUN_06f43af0 reads the descriptor up to +0x50");

// START_IK's descriptor as the event hands it out (retail: event +0x28, filled by FUN_06f43f40
// from the loader's input, whose Lua reads are 0x06f2a5ca..0x06f2a8d6). This is what IIKControl
// slots 3 / 4 / 5 receive and what IScriptEngine slots 15 / 16 hand out for an IK event. It is
// NOT the loader's input record (ThornScene.h FThornIkEventDesc): FUN_06f43f40 drops the input's
// "ik_id" (+0x2c) and puts the event object's own address there, 0x06f43f73
// `MOV [EBP+0x54],EBP` -- the key freelancer's Thorn_Deformable matches a stop or an update
// against (LAB_004523a0 is the motion twin; FUN_00452a30 / LAB_00452680 read +0x2c).
// Defaults when Type is not START_IK (0x06f440e4..0x06f44108): count 0, up 1, front 5,
// damping 1.0, both bytes 0, transition 1.0, target type 0, both names null.
struct FThornStartIkDesc
{
    FThornEventDesc Common;       // +0x00 Type == kThornTypeStartIk
    void const* Key;              // +0x2c the event object itself
    int CountToRoot;              // +0x30 "count_to_root"
    int LookAtUpAxis;             // +0x34 "up"
    int LookAtFrontAxis;          // +0x38 "front"
    float Damping;                // +0x3c "damping"
    unsigned char bPointAt;       // +0x40 "point_at"
    unsigned char bMoveTo;        // +0x41 "move_to"
    unsigned char Pad42[2];
    float TransitionDuration;     // +0x44 "transition_duration"
    char const* EndEffector;      // +0x48 "end_effector" (c_str of the event's copy)
    char const* TargetPart;       // +0x4c "target_part"
    int TargetType;               // +0x50 "target_type"
};

static_assert(sizeof(void*) != 4 || sizeof(FThornStartIkDesc) == 0x54,
              "FUN_06f43f40 writes the descriptor up to +0x50 (event +0x78)");

// CONNECT_HARDPOINTS' descriptor, as ICompoundControl slots 7 / 8 receive it (ThornScene
// PackConnectDesc). freelancer reads exactly these four fields: Thorn_Compound FUN_00451e50 /
// FUN_00451e90 hand (PrimaryIndex, Hardpoint, SecondaryIndex, ParentHardpoint) to
// FUN_0044fdd0 / FUN_0044ff30; Thorn_Deformable LAB_00452d70 / LAB_00452db0 swap the pairs.
struct FThornConnectEventDesc
{
    FThornEventDesc Common;       // +0x00 Type == kThornTypeConnectHardpoints
    char const* Hardpoint;        // +0x2c on the primary entity
    char const* ParentHardpoint;  // +0x30 on the secondary entity
};

static_assert(sizeof(void*) != 4 || sizeof(FThornConnectEventDesc) == 0x34,
              "FUN_00451e50 reads the descriptor up to +0x30");

// FThornFog8::Mode, the script keywords F_NONE / F_EXP / F_EXP2 / F_LINEAR.
enum EThornFogMode
{
    kThornFogNone = 0,
    kThornFogExp = 1,
    kThornFogExp2 = 2,
    kThornFogLinear = 3,
};

// Fog property bits of a SCENE's fog keys and of START_FOG_PROP_ANIM masks.
enum EThornFogBit
{
    kThornFogBitOn = 0x01,      // fogon
    kThornFogBitTable = 0x02,   // fogtable
    kThornFogBitColor = 0x04,   // fogcolor
    kThornFogBitMode = 0x08,    // fogmode
    kThornFogBitStart = 0x10,   // fogstart
    kThornFogBitEnd = 0x20,     // fogend
    kThornFogBitDensity = 0x40, // fogdensity
};

// Event+0x58 / capture+0x78 / entity+0x138: 8 dword fog record (FUN_06f27c50 / FUN_06f48de0).
struct FThornFog8
{
    unsigned char On;
    unsigned char Table;
    unsigned char Pad[2];
    int Color[3];
    int Mode;
    float Start;
    float End;
    float Density;
};

static_assert(sizeof(FThornFog8) == 32, "fog 8-dword record");

// SPATIAL_PROP_ANIM: 465f0 stores orientation/position, quaternion and
// axis/target data; table 5b2cc getter 4a570 exposes all thirty-two words.
struct FThornSpatialEventDesc
{
    FThornEventDesc Common;
    int Mask;
    Matrix Orient;
    Vector Position;
    ::Quaternion Quaternion;
    float AxisRotAngle;
    int AxisRotAxis;
    const char* TargetPart;
    int TargetType;
};

static_assert(sizeof(void*) != 4 || sizeof(FThornSpatialEventDesc) == 0x80,
              "SPATIAL descriptor thirty-two words");

// LIGHT_PROP_ANIM: 45660 copies mask + 36 raw target words;
// 4a4e0 returns the live 48-word descriptor at event +0x28.
struct FThornLightEventDesc
{
    FThornEventDesc Common;
    int Mask;
    FThornLightProps Target;
};

static_assert(sizeof(void*) != 4 || sizeof(FThornLightEventDesc) == 0xc0,
              "LIGHT descriptor forty-eight words");

// Reverb property bits of a SCENE's reverb keys and of START_REVERB_PROP_ANIM masks.
enum EThornReverbBit
{
    kThornReverbBitEnvironment = 0x1, // env
    kThornReverbBitVolume = 0x2,      // vol
    kThornReverbBitDecay = 0x4,       // decay
    kThornReverbBitDamping = 0x8,     // damping
};

// REVERB_PROP_ANIM: 49150 copies mask plus four raw words; 4a6d0
// in table 5b3cc exposes environment integer bits and three floats.
struct FThornReverb4
{
    unsigned int Environment;
    float Volume;
    float Decay;
    float Damping;
};

static_assert(sizeof(FThornReverb4) == 0x10, "REVERB four raw words");

struct FThornReverbEventDesc
{
    FThornEventDesc Common;
    int Mask;
    FThornReverb4 Target;
};

static_assert(sizeof(void*) != 4 || sizeof(FThornReverbEventDesc) == 0x40,
              "REVERB descriptor sixteen words");

// Audio property bits of START_AUDIO_PROP_ANIM masks (the `audioprops` keys).
enum EThornAudioBit
{
    kThornAudioBitAttenuation = 0x01, // attenuation
    kThornAudioBitPan = 0x02,         // pan
    kThornAudioBitDmin = 0x04,        // dmin
    kThornAudioBitDmax = 0x08,        // dmax
    kThornAudioBitAin = 0x10,         // ain
    kThornAudioBitAout = 0x20,        // aout
    kThornAudioBitAtout = 0x40,       // atout
    kThornAudioBitRmix = 0x80,        // rmix
};

// AUDIO_PROP_ANIM: 47280 copies mask and eight raw target words;
// 4a540 returns the complete twenty-word descriptor. ain/aout retain integer bits.
struct FThornAudio8
{
    float Attenuation;
    float Pan;
    float Dmin;
    float Dmax;
    int Ain;
    int Aout;
    float Atout;
    float Rmix;
};

static_assert(sizeof(FThornAudio8) == 0x20, "AUDIO six floats and two integer cones");

struct FThornAudioPropEventDesc
{
    FThornEventDesc Common;
    int Mask;
    FThornAudio8 Target;
};

static_assert(sizeof(void*) != 4 || sizeof(FThornAudioPropEventDesc) == 0x50,
              "AUDIO_PROP descriptor common + mask + eight raw words");

// START_PSYS_PROP_ANIM mask bit: the `psysprops` key sparam.
enum EThornPsysBit
{
    kThornPsysBitSparam = 0x1,
};

// PSYS_PROP_ANIM: 48920 stores mask and sparam; the 4a670 getter
// in table 5b38c exposes both after the common eleven words.
struct FThornPsysPropEventDesc
{
    FThornEventDesc Common;
    int Mask;
    float Sparam;
};

static_assert(sizeof(void*) != 4 || sizeof(FThornPsysPropEventDesc) == 0x34,
              "PSYS_PROP descriptor common + mask + sparam");

// FOG_PROP_ANIM: 48cc0 copies mask and eight payload words; table 5b3ac
// slot 2 (4a6a0) exposes the contiguous twenty-word descriptor.
struct FThornFogEventDesc
{
    FThornEventDesc Common;
    int Mask;
    FThornFog8 Target;
};

static_assert(sizeof(void*) != 4 || sizeof(FThornFogEventDesc) == 0x50,
              "FOG descriptor common + mask + eight payload words");

// FLOOR_HEIGHT_ANIM: 495b7 copies a float, 495bd/49671 copy name/type;
// table 5b3ec slot 2 (4a700) exposes all fourteen words.
struct FThornFloorEventDesc
{
    FThornEventDesc Common;
    float FloorHeight;
    const char* TargetPart;
    int TargetType;
};

static_assert(sizeof(void*) != 4 || sizeof(FThornFloorEventDesc) == 0x38,
              "FLOOR descriptor common + float + name + type");

// Supplied ScriptEngine context facet, stored directly by 06f3a380 (no QI).
// No recovered IID/provider identity. This is distinct from IUserEventControl.
// Descriptor is the complete event packet, including type-specific trailing words.
struct IThornComContext : IDAComponent
{
    virtual GENRESULT DACOM_API SetUserEventString(FThornUserEventDesc* Descriptor, const char* String) = 0; // +0x0c, 25fe4; return ignored
    virtual GENRESULT DACOM_API GetUserEventString(FThornUserEventDesc* Descriptor, char* Buffer, unsigned int Size) = 0; // +0x10, 2b509
    virtual unsigned int DACOM_API GetUserEventStringLength(FThornUserEventDesc* Descriptor) = 0; // +0x14, 2b4e1
};

// Six-slot borrowed load parser, retail vtable 06f5a1ec. The explicit
// destroy slot is used only for the temporary default parser.
struct IScriptEngine;
struct IFileSystem;
struct IThornLoadParser
{
    virtual void* DACOM_API Destroy(unsigned int Flags) = 0; // 0, retail 06f250a0
    virtual unsigned int DACOM_API GetEntityCount() = 0;     // 1, retail 06f2d4d0
    virtual unsigned int DACOM_API GetEventCount() = 0;      // 2, retail 06f2d4e0
    virtual unsigned int DACOM_API GetDuration() = 0;        // 3, retail 06f2d4f0
    virtual GENRESULT DACOM_API Load(IFileSystem* FileSystem) = 0;               // 4, 06f250e0
    virtual GENRESULT DACOM_API Build(IFileSystem* FileSystem, IScriptEngine* Engine,
                                      IThornComContext* ComContext) = 0; // 5, 06f2a910
};

// Four-slot borrowed save parser. Retail vtable 06f5aef4; all methods
// use the modern DACOM calling convention across our DLL boundary.
struct IScriptEngine;
struct IFileSystem;
struct IThornSaveParser
{
    virtual void DACOM_API SetEntityCount(unsigned int Count) = 0; // 0, retail 06f2a9e0
    virtual void DACOM_API SetEventCount(unsigned int Count) = 0;  // 1, retail 06f2a9f0
    virtual void DACOM_API SetDuration(unsigned int Ms) = 0;       // 2, retail 06f2aa00
    virtual GENRESULT DACOM_API Save(IFileSystem* FileSystem, IScriptEngine* Engine,
                                     IThornComContext* ComContext) = 0; // 3, retail 06f2aa10
};

// 45-slot IScriptEngine (PTR_LAB_06f5af50), implemented by thorn's FScriptEngine (Thorn.cpp).
// Slot = offset/4. Names come from what the retail bodies do; slots 3, 25 and 27..30 are
// stubs or an alias with no recovered purpose and keep placeholders.
struct IScriptEngine : IDAComponent
{
    virtual GENRESULT DACOM_API Slot3(void* A, void* B) = 0;                          // 3  RET 0xC, returns 0
    virtual GENRESULT DACOM_API Load(void* FileSystem) = 0;                      // 4  RET 0x8
    virtual GENRESULT DACOM_API Save(void* FileSystem) = 0;                      // 5  RET 0x8, FUN_06f2aa10
    virtual unsigned long DACOM_API GetEntityCount() = 0;                             // 6  RET 0x4
    virtual void* DACOM_API GetEntityHandle(int Order) = 0;                           // 7  RET 0x8, in-order walk
    virtual void* DACOM_API GetEntityDescInOrder(int Order) = 0;                      // 8  RET 0x8, same walk
    virtual void* DACOM_API GetEntityDesc(int Index) = 0;                             // 9  RET 0x8
    virtual GENRESULT DACOM_API GetEntityPosition(int Index, Vector* OutVec3) = 0;     // 10 RET 0xC
    virtual GENRESULT DACOM_API GetEntityOrientation(int Index, Matrix* OutMat9) = 0;  // 11 RET 0xC
    virtual GENRESULT DACOM_API AttachEntity(int Index, void* EngineObj) = 0;         // 12 RET 0xC
    virtual GENRESULT DACOM_API DetachEntity(int Index) = 0;                          // 13 RET 0x8
    virtual unsigned long DACOM_API GetEventCount() = 0;                              // 14 RET 0x4
    virtual void* DACOM_API GetEventByIndex(int Index) = 0;                           // 15 RET 0x8
    virtual void* DACOM_API GetEventById(int Id) = 0;                                 // 16 RET 0x8
    virtual int DACOM_API GetScriptDuration() = 0;                                    // 17 RET 0x4
    virtual GENRESULT DACOM_API SetScriptDuration(int Ms) = 0;                        // 18 RET 0x8, this+0x20
    virtual int DACOM_API GetClockMs() = 0;                                           // 19 RET 0x4
    // LAB_06f39430: this in ECX, byte on stack -> this+0x30, XOR EAX,EAX, RET 4.
    virtual GENRESULT __thiscall SetLooping(unsigned char Value) = 0;                 // 20 thiscall
    virtual GENRESULT DACOM_API GetPlayState() = 0;                                   // 21 RET 0x4, this+0x2c
    virtual GENRESULT DACOM_API StartPlayback(int Arg) = 0;                           // 22 RET 0x8
    virtual GENRESULT DACOM_API StopPlayback() = 0;                                   // 23 RET 0x4
    virtual GENRESULT DACOM_API PausePlayback() = 0;                                  // 24 RET 0x4, LAB_06f397f0 6|7 -> 5
    virtual GENRESULT DACOM_API Slot25Start(int Arg) = 0;                             // 25 JMP vtbl+0x58 = 22
    virtual GENRESULT DACOM_API TickPlayback(int DtMs) = 0;                           // 26 RET 0x8
    virtual GENRESULT DACOM_API Slot27(void* A) = 0;                                  // 27 RET 0x8, returns 0
    virtual GENRESULT DACOM_API Slot28(void* A, void* B) = 0;                         // 28 RET 0xC, returns 0
    virtual GENRESULT DACOM_API Slot29(void* A, void* B, void* C) = 0;                // 29 RET 0x10, returns -1
    virtual GENRESULT DACOM_API Slot30(void* A, void* B, void* C) = 0;                // 30 RET 0x10, returns -1
    virtual unsigned long DACOM_API GetUserPropCount(int Index) = 0;                  // 31 RET 0x8
    virtual const char* DACOM_API GetUserPropKey(int Index, int N) = 0;               // 32 RET 0xC, N-th key
    virtual GENRESULT DACOM_API GetUserProp(void* Handle, const char* Key,
                                            char** Out) = 0;                          // 33 RET 0x10
    virtual GENRESULT DACOM_API SetUserProp(int Index, const char* Key,
                                            const char* Value) = 0;                   // 34 RET 0x10
    virtual GENRESULT DACOM_API AddEntity(void* Desc, int* OutIndex) = 0;             // 35 RET 0xC
    virtual GENRESULT DACOM_API RemoveEntity(int Index) = 0;                          // 36 RET 0x8
    virtual GENRESULT DACOM_API UpdateEntity(int Index, void* Desc) = 0;              // 37 RET 0xC
    virtual GENRESULT DACOM_API AddEvent(const void* Desc, int* OutId) = 0;           // 38 RET 0xC
    virtual GENRESULT DACOM_API RemoveEvent(int Id) = 0;                              // 39 RET 0x8
    virtual GENRESULT DACOM_API SetComContext(IThornComContext* Com) = 0;             // 40 RET 0x8, this+0x1c
    virtual GENRESULT DACOM_API SetLoadParser(IThornLoadParser* Parser) = 0;          // 41 RET 0x8, this+0x100
    virtual GENRESULT DACOM_API SetSaveParser(IThornSaveParser* Parser) = 0;          // 42 RET 0x8, this+0x104
    virtual GENRESULT DACOM_API LoadFromPath(const char* Path) = 0;                   // 43 RET 0x8 -> slot 4
    virtual GENRESULT DACOM_API SaveToPath(const char* Path) = 0;                     // 44 RET 0x8 -> slot 5
};
