// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Public DACOM contracts for EngBase and Deformable2.

#include "dacom.h"
#include "engine/engine_instance.h" // IEngineInstance, InstanceIndex, ArchetypeIndex

struct IFileSystem;
struct IChannelArchetype;

#include <cstddef>
#include <cstring>


// CreateCompound / FindArchetype name wrapper (handle+0 / CRC+4).
struct FEngineName
{
    const char* Text;
    unsigned long Crc;
};

// Joint payload contains 23 DWORDs; mixed values are copied without numeric conversion.
static const unsigned long G_EngineJointPayloadDwords = 0x17;

// The joint block of an instance: the payload CreateJoint copies in and GetInstanceRelative
// (slot 49) hands out, null for a root. EngBase keeps it as raw dwords at instance +0x5C and
// writes Type = 0xFFFFFFFF for no joint. Layout as Common reads it (FUN_062940c0 builds a
// revolute block, CEGun and PhySys read it); which of the fields a joint type uses varies:
// EngBase's fixed joint takes its translation from +0x10, Deformable2's sphere test reads the
// vector at +0x10 as an axis.
struct FEngineJointBlock
{
    unsigned long Type;   // +0x00  EngBase's joint kind (revolute is 2)
    Vector ParentPos;     // +0x04  the parent hardpoint position
    Vector ChildPos;      // +0x10  the child hardpoint position
    Matrix Orient;        // +0x1c  parent * child hardpoint orientation
    Vector Axis;          // +0x40  the rotation axis in that frame
    float Min;            // +0x4c  angle limits
    float Max;            // +0x50
    // +0x54 / +0x58 are never written by FUN_062940c0 (stack garbage in retail); a revolute
    // joint does not use them.
    unsigned long Field_0x54;
    unsigned long Field_0x58;
};
static_assert(sizeof(FEngineJointBlock) == G_EngineJointPayloadDwords * sizeof(unsigned long),
              "FEngineJointBlock is the whole IEngine joint block");

struct IEngine : IDAComponent
{
    virtual GENRESULT DACOM_API LoadEngineComponents(IDAComponent* SystemContainer) = 0; // 3 +0x0C
    virtual unsigned long DACOM_API EnumerateEngineComponents(
        int(__cdecl* Callback)(const char*, void*)) = 0;                                 // 4
    virtual GENRESULT DACOM_API NotifyEngineComponents(void* Arg) = 0;                 // 5
    virtual GENRESULT DACOM_API SetSearchPath(const char* Path) = 0;                     // 6
    virtual GENRESULT DACOM_API QuerySearchPath(char* Dest, unsigned long MaxChars) = 0; // 7
    virtual GENRESULT DACOM_API SetFilesystem(IDAComponent* NewPath) = 0;                 // 8
    virtual GENRESULT DACOM_API GetSearchPath(IDAComponent** Out) = 0;                  // 9
    // 10 -- Factory (null: DACOM for an absolute path, else the search path) creates the
    // file system for Path through its CreateInstance.
    virtual GENRESULT DACOM_API CreateFileSystem(const char* Path, IFileSystem** Out,
                                                IComponentFactory* Factory) = 0;        // 10
    virtual ArchetypeIndex DACOM_API CreateCompoundArchetype(FEngineName* Name) = 0;    // 11
    virtual ArchetypeIndex DACOM_API CreateArchetypeHandle(FEngineName* Name,
                                                           IFileSystem* FileSystem) = 0; // 12
    virtual ArchetypeIndex DACOM_API DuplicateArchetype(ArchetypeIndex ArchIn,
                                                  FEngineName* Name) = 0;               // 13
    virtual unsigned long DACOM_API AddRefHandle(ArchetypeIndex Handle) = 0;             // 14
    virtual unsigned long DACOM_API ReleaseHandle(ArchetypeIndex Handle) = 0;            // 15
    // LAB_06621850 RET 0xC: Out is the FIRST argument ([ESP+8]), Handle the second
    // ([ESP+0xC]) -- same order as slot 26; FUN_06622860 pushes Handle, &Out, this.
    virtual FEngineName* DACOM_API GetArchetypeName(FEngineName* Out,
                                                    ArchetypeIndex Handle) = 0;         // 16
    virtual ArchetypeIndex DACOM_API FindArchetype(FEngineName* Name) = 0;                // 17
    // 18 -- FUN_06621350: IEngineComponent::QueryArchetype(Archetype, Iid, Out) on every engine
    // component until one answers >= 0.
    virtual GENRESULT DACOM_API QueryEngineComponent(ArchetypeIndex Archetype, const char* Iid,
                                                     void** Out) = 0;                   // 18
    virtual unsigned long DACOM_API IsCompoundArchetype(ArchetypeIndex Handle) = 0;       // 19
    // LAB_06622360: walks the ARCHETYPE handle's constraint records (+0x2C, stride 0x64);
    // Callback is cdecl (parent part handle, child part handle, record + 8, ctx), AL 0 stops.
    virtual GENRESULT DACOM_API EnumerateArchetypeConstraints(
        ArchetypeIndex Handle,
        int(__cdecl* Callback)(ArchetypeIndex, ArchetypeIndex, void*, void*),
        void* Context) = 0;                                                              // 20
    // Callback is cdecl (parent, childHandle, 0, ctx) — retail FUN_066223b0.
    virtual GENRESULT DACOM_API EnumerateArchetypeChildren(
        ArchetypeIndex Handle,
        int(__cdecl* Callback)(ArchetypeIndex, ArchetypeIndex, int, void*),
        void* Context) = 0;                                                              // 21
    // 22 / 23 / 28: Callbacks null -> the engine's own IEngineInstance (FInstanceTransform).
    virtual InstanceIndex DACOM_API CreateInstanceFromName(FEngineName* Name,
                                                           IFileSystem* FileSystem,
                                                           IEngineInstance* Callbacks) = 0; // 22
    virtual InstanceIndex DACOM_API CreateInstance(ArchetypeIndex Handle,
                                                   IEngineInstance* Callbacks) = 0;     // 23
    virtual GENRESULT DACOM_API DestroyInstance(InstanceIndex Instance) = 0;            // 24
    virtual ArchetypeIndex DACOM_API GetInstanceArchetype(InstanceIndex Instance) = 0;  // 25
    virtual FEngineName* DACOM_API GetInstanceName(FEngineName* Out,
                                                   InstanceIndex Instance) = 0;         // 26
    virtual GENRESULT DACOM_API UpdateInstance(InstanceIndex Instance, unsigned long Flags,
                                              void* Arg) = 0;                           // 27
    virtual GENRESULT DACOM_API SetInstanceTransform(InstanceIndex Instance,
                                                     IEngineInstance* Callbacks) = 0;   // 28
    virtual GENRESULT DACOM_API GetInstanceTransformObject(InstanceIndex Instance,
                                                           IEngineInstance** Out) = 0;  // 29
    virtual GENRESULT DACOM_API QueryInstance(InstanceIndex Instance, const char* Iid,
                                              void** Out) = 0;                          // 30
    virtual GENRESULT DACOM_API SetInstanceUserData(InstanceIndex Instance,
                                                    void* Value) = 0;                   // 31
    virtual void* DACOM_API GetInstanceUserData(InstanceIndex Instance) = 0;           // 32
    // 33 / 34: the bounding sphere (radius, centre in the instance's frame).
    virtual GENRESULT DACOM_API SetInstanceExtent(InstanceIndex Instance, unsigned long Flags,
                                                 float Extent, const Vector* Center) = 0; // 33
    virtual GENRESULT DACOM_API GetInstanceExtent(InstanceIndex Instance, unsigned long Flags,
                                                 float* OutExtent, Vector* OutCenter) = 0; // 34
    // 35..40 forward to the instance's IEngineInstance (slots 3..8).
    virtual void DACOM_API SetInstancePosition(InstanceIndex Instance,
                                               const Vector* Position) = 0;            // 35
    virtual const Vector* DACOM_API GetInstancePosition(InstanceIndex Instance) = 0;   // 36
    virtual void DACOM_API SetInstanceOrientation(InstanceIndex Instance,
                                                  const Matrix* Orient) = 0;           // 37
    virtual const Matrix* DACOM_API GetInstanceOrientation(InstanceIndex Instance) = 0; // 38
    virtual void DACOM_API SetInstanceMatrix(InstanceIndex Instance,
                                             const Transform* Xform) = 0;              // 39
    virtual const Transform* DACOM_API GetInstanceTransform(InstanceIndex Instance) = 0; // 40
    virtual GENRESULT DACOM_API NoOp41(void* A, void* B) = 0; // 41 TODO: original operation purpose.
    // 42 / 44: never called in the tree; both answer the identity's zero translation.
    virtual float* DACOM_API GetIdentityMatrix(void* Unused) = 0;                      // 42
    virtual GENRESULT DACOM_API NoOp43(void* A, void* B) = 0; // 43 TODO: original operation purpose.
    virtual float* DACOM_API GetIdentityMatrix2(void* Unused) = 0;                     // 44 = 42
    virtual void DACOM_API SetTransformUser(InstanceIndex Instance, unsigned long User) = 0; // 45
    virtual unsigned long DACOM_API GetTransformUser(InstanceIndex Instance) = 0;      // 46
    virtual unsigned long DACOM_API CreateJoint(InstanceIndex Parent, InstanceIndex Child,
                                                 const unsigned long* Payload) = 0;     // 47
    // Detach Child from its current parent; Parent is diagnostic context.
    virtual unsigned long DACOM_API DestroyJoint(InstanceIndex Parent,
                                                 InstanceIndex Child) = 0;             // 48
    virtual FEngineJointBlock* DACOM_API GetInstanceRelative(InstanceIndex Instance) = 0; // 49
    virtual unsigned long DACOM_API SetJointParams(InstanceIndex Instance, unsigned long Index,
                                                    const float* Values) = 0;          // 50
    virtual unsigned long DACOM_API GetJointParams(InstanceIndex Instance, unsigned long Index,
                                                    float* Out) = 0;                   // 51
    // 52 -- LAB_066222a0: an iterator over the engine's hierarchy-root map. -1 -> the first
    // root (or -1 when there is none); a mapped root -> the next root in key order (or -1 at
    // the end); anything not in the map -> -1. The port name is a label, not retail's.
    virtual InstanceIndex DACOM_API FindMappedInstance(InstanceIndex Instance) = 0;    // 52
    virtual InstanceIndex DACOM_API GetInstanceParent(InstanceIndex Instance) = 0;     // 53
    virtual InstanceIndex DACOM_API GetRelatedInstance(InstanceIndex From, unsigned long Flags,
                                                       InstanceIndex Cursor) = 0;      // 54
    virtual InstanceIndex DACOM_API GetInstanceRoot(InstanceIndex Instance) = 0;       // 55
    // 56 -- the archetype's child record (FArchetypeChild in EngBase) whose handle is
    // ChildHandle, or null.
    virtual void* DACOM_API FindArchetypeChild(ArchetypeIndex Handle,
                                               ArchetypeIndex ChildHandle) = 0;        // 56
    virtual unsigned long DACOM_API RejectOperation(void* A, void* B) = 0; // 57 TODO: original operation purpose.
};

#ifndef FREELANCER_IENGINE2_DEFINED
#define FREELANCER_IENGINE2_DEFINED
// Same vtable as IEngine. DALib.h keeps a distinct incomplete IEngine2 for export mangling.
using IEngine2 = IEngine;
#endif

#ifndef FREELANCER_IENGINECOMPONENT
#define FREELANCER_IENGINECOMPONENT
// EngineComponent dispatch fans out archetype, instance and notification operations.
struct IEngineComponent : IDAComponent
{
    virtual int DACOM_API Initialize() = 0;                                  // 3  +0x0C
    // 4 +0x10 -- (archetype, IFileSystem). RendComp loads the archetype's LODs from it.
    virtual int DACOM_API CreateArchetype(ArchetypeIndex Archetype, IFileSystem* FileSystem) = 0;
    // 5 +0x14 -- (new archetype, old archetype). RendComp AddRefs each LOD handle.
    virtual int DACOM_API DuplicateArchetype(ArchetypeIndex NewArchetype,
                                             ArchetypeIndex OldArchetype) = 0;
    // 6 +0x18 -- (archetype). RendComp destroys every LOD handle and drops the entry.
    virtual int DACOM_API DestroyArchetype(ArchetypeIndex Archetype) = 0;
    // 7 +0x1C -- (archetype, name, out). First result >= 0 wins across the component list.
    virtual int DACOM_API QueryArchetype(ArchetypeIndex Archetype, const char* Name, void** Out) = 0;
    // 8 +0x20 -- (instance, archetype). RendComp rolls the LODs back if one refuses.
    virtual int DACOM_API CreateInstance(InstanceIndex Instance, ArchetypeIndex Archetype) = 0;
    virtual int DACOM_API DestroyInstance(InstanceIndex Instance) = 0;                // 9  +0x24
    virtual int DACOM_API UpdateInstance(InstanceIndex Instance, void* Arg) = 0;      // 10 +0x28
    // 11 +0x2C -- (instance, iid, out); resolves the instance to its archetype first.
    virtual int DACOM_API QueryInstance(InstanceIndex Instance, const char* Iid, void** Out) = 0;
    virtual int DACOM_API Notify(void* Arg) = 0;                             // 12 +0x30
};
#endif

// Raw DWORD tokens use explicit bit-preserving transport at animation boundaries.
inline void* AsOpaqueDword(float Value)
{
    unsigned long Bits = 0;
    std::memcpy(&Bits, &Value, sizeof(Bits));
    return (void*)Bits;
}

inline void* AsOpaqueDword(int Value)
{
    return (void*)((ptrdiff_t)Value);
}

inline void* AsOpaqueDword(unsigned long Value)
{
    return (void*)Value;
}

// The other direction: a dword a caller is carrying as void* going into one of the slots
// engbase.h types as float (EvalPlayback's Time / Scale).
inline float FloatFromOpaqueDword(void* Value)
{
    float Out = 0.0f;
    std::memcpy(&Out, &Value, sizeof(Out));
    return Out;
}

inline void* AsOpaqueDword(const char* Value)
{
    return (char*)Value; // Borrowed read-only string token; the carrier drops const.
}

// IAnimation2 enumeration callbacks (cdecl; EngBase calls them through these types and
// ignores their result, EnumObjects excepted).
// EnumScriptNames / EnumFsScripts: one script or animation name.
typedef void(__cdecl* FAnimNameProc)(const char* Name, void* Context);
// FindScriptName: one channel archetype of the named animation, AddRef'd.
typedef void(__cdecl* FAnimChannelProc)(IChannelArchetype* Channel, void* Context);
// EnumObjects: one typed channel of the playback (EngBase's FTypedChannel); 0 stops the walk.
typedef unsigned char(__cdecl* FAnimObjectProc)(void* Channel, void* Context);

struct IAnimation2 : IDAComponent
{
    virtual void* DACOM_API OpenScript(IFileSystem* FileSystem) = 0;                            // 3
    virtual GENRESULT DACOM_API CloseScript(void* Script) = 0;                           // 4
    // Duration/destroy/filter slots carry a playback ID encoded as void*.
    virtual float DACOM_API GetScriptDuration(void* PlaybackId) = 0;                    // 5
    virtual float DACOM_API GetNamedDuration(void* Script, const char* Name) = 0;       // 6
    virtual GENRESULT DACOM_API EnumScriptNames(FAnimNameProc Callback, void* Script,
                                                void* Context) = 0;                     // 7
    // Callback(IChannelArchetype*, Context) cdecl once per channel of the named animation;
    // each archetype arrives AddRef'd (engbase FUN_06614d40). -2 for a miss, else 0.
    virtual GENRESULT DACOM_API FindScriptName(FAnimChannelProc Callback, void* Script,
                                              const char* Name, void* Context) = 0;    // 8
    // Root precedes Name. Sink (an IChannelSink*) and SinkContext go to every bound channel; a
    // track bound to the root itself (an event channel) needs a sink.
    virtual unsigned long DACOM_API PlayScript(void* Script, InstanceIndex Root, const char* Name,
                                               void* Sink, void* SinkContext) = 0;     // 9
    virtual GENRESULT DACOM_API DestroyScript(void* PlaybackId) = 0;                     // 10
    // Speed is |Speed|, negated when Flags bit 0 (reverse) is set. The last four are play
    // header [3]..[6] as raw dwords: blend duration, scale and translation scale (floats) and
    // the type-6 transform block.
    virtual unsigned long DACOM_API EvalPlayback(unsigned long PlaybackId, unsigned long Flags,
                                                 float Time, float Speed, void* BlendDuration,
                                                 void* Scale, void* TranslationScale,
                                                 const Transform* Xform) = 0;            // 11
    virtual unsigned long DACOM_API EvalStored(unsigned long PlaybackId) = 0;             // 12
    virtual unsigned long DACOM_API ApplyChannels(unsigned long PlaybackId) = 0;          // 13
    virtual unsigned long DACOM_API StopPlayback(unsigned long PlaybackId) = 0;           // 14
    virtual unsigned long DACOM_API StopChannels(unsigned long PlaybackId, void* Unused) = 0; // 15
    virtual float DACOM_API GetStoredDuration(unsigned long PlaybackId) = 0;              // 16
    virtual unsigned long DACOM_API FilterObjects(void* PlaybackId) = 0;                 // 17
    virtual unsigned long DACOM_API SetOverlayPos(unsigned long PlaybackId, const Vector* Pos) = 0; // 18
    virtual unsigned long DACOM_API SetOverlayExtra(unsigned long PlaybackId,
                                                    const Matrix* Extra) = 0;          // 19
    virtual unsigned long DACOM_API HasObject(unsigned long PlaybackId, void* Object) = 0; // 20
    virtual unsigned long DACOM_API TouchMapped(unsigned long PlaybackId, Vector* OutPos,
                                               Vector* OutSampledPos) = 0;             // 21
    virtual unsigned long DACOM_API AddOverlayPos(unsigned long PlaybackId,
                                                  const Vector* Delta) = 0;            // 22
    virtual unsigned long DACOM_API GetFirstInstanceField(unsigned long PlaybackId,
                                                      unsigned long* Out) = 0;         // 23
    virtual unsigned long DACOM_API GetDurationIfNamed(unsigned long PlaybackId,
                                                     float* Out) = 0;                  // 24
    virtual unsigned long DACOM_API GetNamedDurationOut(void* Script, const char* Name,
                                                         float* Out) = 0;              // 25
    virtual unsigned long DACOM_API FindObject(unsigned long PlaybackId, void* Query) = 0; // 26
    virtual unsigned long DACOM_API MixerWalk(unsigned long PlaybackId, float Time) = 0; // 27
    virtual unsigned long DACOM_API MixerWalkAll(float Time) = 0;                      // 28
    virtual void* DACOM_API CreateEmptyScript() = 0;                                      // 29
    virtual unsigned long DACOM_API AddScriptNames(void* Script, IFileSystem* FileSystem,
                                                   const char** Names, int Count) = 0; // 30
    virtual unsigned long DACOM_API RemoveScriptNames(void* Script, const char** Names,
                                                    int Count) = 0;                    // 31
    // Callback(name, Context) cdecl per "Animation\Script\<dir>" (engbase FUN_06611a40; the
    // callback is argument 1, not 3 -- freelancer.exe 0x00435272..0x00435282 agrees).
    virtual unsigned long DACOM_API EnumFsScripts(FAnimNameProc Callback, IFileSystem* FileSystem,
                                                  void* Context) = 0;                  // 32
    virtual unsigned long DACOM_API HasScriptName(void* Script, const char* Name) = 0; // 33
    virtual unsigned long DACOM_API GetScriptNameCount(void* Script, unsigned long* Out) = 0; // 34
    virtual unsigned long DACOM_API EnumObjects(FAnimObjectProc Callback, unsigned long PlaybackId,
                                                 void* Context) = 0;                    // 35
    virtual unsigned long DACOM_API RegisterHandle(unsigned long Key) = 0;              // 36
    virtual unsigned long DACOM_API HasHandle(unsigned long Key, unsigned char* Out) = 0; // 37
    virtual unsigned long DACOM_API HasAnyHandle(unsigned long Key) = 0;                 // 38
};

using IAnimation = IAnimation2;

// Owner callbacks receive samples, channel key, bind descriptor and time.
struct IChannelOwner
{
    virtual int DACOM_API Apply(float* Sampled, unsigned long ChannelKey, void* BindDesc,
                                float Time) = 0; // +0x00
};
// Keys an event channel (archetype flags 8) crossed in one tick: FUN_0661b120 fills at most
// 0x40 {time, value} pairs, FUN_0661b420 copies them here with the count behind them
// (frame +0xA0 .. +0x2A0).
struct FChannelKeyBatch
{
    float Keys[0x40][2];
    unsigned long Count;
};
// Key batches and loop transforms borrow context from the bind descriptor.
struct IChannelSink
{
    virtual void DACOM_API OnKeys(unsigned long ChannelKey, void* Context,
                                  FChannelKeyBatch* Batch) = 0; // +0x00
    // TODO: purpose of both opaque notification arguments remains unknown.
    virtual void DACOM_API OnOpaqueNotification(void* A, void* B) = 0; // +0x04
    virtual void DACOM_API OnLoop(unsigned long ChannelKey, Transform* Xform,
                                  void* Context) = 0; // +0x08
};

struct IChannel : IDAComponent
{
    virtual unsigned long DACOM_API CreateTyped(void* Owner, unsigned long Type, void* BindA,
                                                  void* BindB) = 0;                     // 3
    virtual unsigned long DACOM_API CreateFromObject(void* Object, void* BindA,
                                                     void* BindB) = 0;                  // 4
    virtual unsigned long DACOM_API Destroy(unsigned long Key) = 0;                    // 5
    virtual unsigned long DACOM_API GetObject(unsigned long Key, void** Out) = 0;       // 6
    virtual unsigned long DACOM_API Play(unsigned long Key, float* Time) = 0;          // 7
    virtual unsigned long DACOM_API StopGraph(unsigned long Key) = 0;                  // 8
    virtual float DACOM_API GetDuration(unsigned long Key) = 0;                         // 9
    virtual unsigned long DACOM_API Blend(unsigned long Key, float Duration) = 0;        // 10
    virtual GENRESULT DACOM_API SetUser(unsigned long Key, unsigned long User) = 0;         // 11
    virtual float DACOM_API GetUser(unsigned long Key) = 0;                            // 12
    virtual GENRESULT DACOM_API SetOverlayPos(unsigned long Key, const Vector* Pos) = 0;  // 13
    virtual GENRESULT DACOM_API SetOverlayExtra(unsigned long Key, const Matrix* Extra) = 0; // 14
    virtual unsigned long DACOM_API InMixer(unsigned long Key) = 0;                    // 15
    // Hidden result buffer precedes Key and receives the 16-byte bind descriptor.
    virtual unsigned long* DACOM_API GetHeader(unsigned long* Out, unsigned long Key) = 0;
    virtual GENRESULT DACOM_API AddOverlayPos(unsigned long Key, const Vector* Delta) = 0; // 17
    virtual GENRESULT DACOM_API GetMapped(unsigned long Key, unsigned char* Out) = 0;      // 18
};

// IRootData — 4 slots. Facet at Animation+0xC.
struct IRootData : IDAComponent
{
    // As IAnimation2::EvalPlayback without a transform block; then the first channel of type 2
    // or 6 is touched (type 6 writes its sampled local position to OutSampledPos) and the
    // playback stops. Nonzero when such a channel exists.
    virtual unsigned char DACOM_API Eval(unsigned long PlaybackId, Vector* OutSampledPos,
                                         unsigned long Flags, float Time, float Speed,
                                         void* BlendDuration, void* Scale,
                                         float TranslationScale) = 0; // 3
};

// Hardpoint lookup key: name and its CRC.
struct FHardpointKey
{
    char const* Name;
    unsigned long Crc;
};

// Enumerates borrowed lowercase names in insertion order; callback result is discarded.
typedef void(__cdecl* FHardpointEnumProc)(char const* Name, void* Context);

// IHardpoint — 8 slots.
struct IHardpoint : IDAComponent
{
    // 3 -- look one hardpoint up by name on an archetype handle and copy its 0x48-byte blob
    // into Out. Non-zero on a hit.
    virtual unsigned long DACOM_API GetHardpoint(ArchetypeIndex Handle, FHardpointKey const* Key,
                                                 void* Out) = 0;                        // 3
    virtual unsigned long DACOM_API AddHardpoint(ArchetypeIndex Handle, FHardpointKey const* Key,
                                                  const void* Data) = 0;                // 4
    virtual GENRESULT DACOM_API EnumerateHardpoints(FHardpointEnumProc Callback,
                                                    ArchetypeIndex Handle,
                                                    void* Context) = 0;                 // 5
    // Nonzero reports failure when connecting two hardpoints.
    virtual unsigned long DACOM_API ConnectHardpoints(InstanceIndex InstA, FHardpointKey const* KeyA,
                                                      InstanceIndex InstB,
                                                      FHardpointKey const* KeyB) = 0;     // 6
    // Lookup by a bare CRC; returns a borrowed lowercase name or nullptr.
    virtual char const* DACOM_API FindHardpoint(ArchetypeIndex Handle,
                                                unsigned long const* Crc) = 0;          // 7
};

// ChannelArchetype — 11 slots. Not in [Engine] INI.
struct IChannelArchetype : IDAComponent
{
    virtual GENRESULT DACOM_API Load(IFileSystem* FileSystem) = 0;                             // 3
    virtual GENRESULT DACOM_API GetDuration(float* Out) = 0;                             // 4
    virtual GENRESULT DACOM_API GetFlags(unsigned long* Out) = 0;                         // 5
    virtual GENRESULT DACOM_API Sample(float Time, const unsigned char** OutA,
                                        const unsigned char** OutB, float* Alpha) = 0; // 6
    virtual GENRESULT DACOM_API SamplePair(float Time, const unsigned char** OutLo,
                                            const unsigned char** OutHi, float* TimeLo,
                                            float* TimeHi, float* Alpha) = 0;           // 7
    virtual GENRESULT DACOM_API GetFrame(unsigned char bLast, const unsigned char** Out) = 0; // 8
    virtual GENRESULT DACOM_API CollectRange(float Time0, float Time1, float* OutKeys,
                                                unsigned long Capacity, unsigned long* Count) = 0; // 9
    virtual GENRESULT DACOM_API CollectAt(float Time, float* OutKeys, unsigned long Capacity,
                                             unsigned long* Count) = 0;                // 10
};

// Aim event parameters; target pointers are borrowed.
struct FDeformAimDesc
{
    char const* Name;    // +0x00
    long RootPart;       // +0x04 the chain's root (end effector's count_to_root-th ancestor)
    long EndPart;        // +0x08 the end effector
    unsigned long Flags; // +0x0c
    float* TargetPos;    // +0x10 three floats the aim follows
    float* TargetOrient; // +0x14 nine floats
    float Damping;       // +0x18
    int Field1C;         // +0x1c freelancer passes 0; no reader identified
};

// The part table IDeformable::Create / CreateAlias read (Deformable2 FUN_065f36c0):
// Parts[0] = entry count, Parts[1] = FDeformPartDesc* (stride 0x18). Read and written by
// 065f3749..065f3bf3.
struct FDeformLodDesc
{
    // 065f3773..3779: used only when the entry's Path is null -- the part file is then
    // Name opened through FileSystem (DACOM_Acquire() when FileSystem is null too).
    IComponentFactory* FileSystem; // +0x00
    char const* Name;              // +0x04
    float Fraction;                // +0x08 written back: the "Fractions" value of each LOD loaded
};

struct FDeformPartDesc
{
    int LodCount;           // +0x00 in: the most LODs to load; out: how many were loaded
    FDeformLodDesc* Lods;   // +0x04
    int Field08;            // +0x08 not read by 065f36c0 (Create / CreateAlias)
    char const* Path;       // +0x0C the part file, opened through DACOM_Acquire()
    unsigned long Flags;    // +0x10 bit 1 (2): skip LOD 0; bit 0 (1): stop after LOD 0
    unsigned long LodLimit; // +0x14 a mesh whose LOD index is below this (unsigned) gets
                            //       mesh+0x70 = 1 (065f3b3a SETC)
};

// IDeformable — 39 slots (docs/re/deformable2.md). Facet at object+0.
struct IDeformable : IDAComponent
{
    virtual GENRESULT DACOM_API Create(void** OutInstance, int* Parts, void* Arg3, void* Arg4,
                                       void* Arg5) = 0;                                  // 3
    virtual GENRESULT DACOM_API CreateUnused(void* A, void* B, void* C, void* D, void* E,
                                              void* F, void* G) = 0;                     // 4 always -1
    virtual GENRESULT DACOM_API CreateAlias(void** OutInstance, int* Parts, void* Arg3, void* Arg4,
                                            void* Arg5) = 0;                             // 5
    virtual GENRESULT DACOM_API Destroy(void* Instance) = 0;                              // 6
    virtual GENRESULT DACOM_API Play(void* Instance, int* Out, void* Name, void* Extra, void* ArgC,
                                      void* ArgD, void* ArgE, void* ArgF, void* ArgG, void* ArgH,
                                      void* Script) = 0;                                 // 7
    virtual GENRESULT DACOM_API PlayAlt(void* Instance, int* Out, void* Name, void* Extra,
                                          void* ArgC, void* ArgD, void* ArgE, void* ArgF,
                                          void* ArgG, void* ArgH, InstanceIndex Joint) = 0;    // 8
    virtual GENRESULT DACOM_API DestroyAll(void* Instance) = 0;                          // 9
    virtual GENRESULT DACOM_API DestroyHandle(void* Instance, int Handle) = 0;         // 10
    virtual GENRESULT DACOM_API StartAim(void* Instance, void** Out, unsigned char* AimDesc,
                                          void* Arg3) = 0;                              // 11
    virtual GENRESULT DACOM_API StopAim(void* Instance, void* Node) = 0;                // 12
    virtual GENRESULT DACOM_API Unused13(void* A) = 0;                                  // 13
    virtual GENRESULT DACOM_API Unused14(void* A) = 0;                                  // 14
    // Dt precedes the per-part LOD array.
    virtual GENRESULT DACOM_API SetLod(void* Instance, float Dt, int* LodIds) = 0;     // 15
    virtual GENRESULT DACOM_API Draw(void* Instance, void* Camera, void* UnusedB, void* RenderArg,
                                       int* OutLod, unsigned int Flags) = 0;            // 16
    virtual GENRESULT DACOM_API Unused17(void* A, void* B, void* C, void* D) = 0;         // 17
    virtual GENRESULT DACOM_API GetFirstBone(void* Instance, int* Out) = 0;             // 18
    virtual GENRESULT DACOM_API SetPosePos(void* Instance, const Vector* Pos) = 0;     // 19
    virtual GENRESULT DACOM_API SetPoseOri(void* Instance, const Matrix* Ori) = 0;       // 20
    virtual GENRESULT DACOM_API Unused21(void* A, void* B) = 0;                        // 21
    virtual GENRESULT DACOM_API Unused22(void* A, void* B) = 0;                        // 22
    virtual GENRESULT DACOM_API Unused23(void* A, void* B) = 0;                        // 23
    virtual GENRESULT DACOM_API SetHeight(void* Instance, float Height) = 0;             // 24
    virtual GENRESULT DACOM_API Unused25(void* A, void* B, void* C, void* D) = 0;      // 25
    virtual GENRESULT DACOM_API GetListCount(void* Instance, int* Out) = 0;             // 26
    virtual GENRESULT DACOM_API CopyList(void* Instance, int* Out) = 0;                   // 27
    virtual GENRESULT DACOM_API Unused28(void* A, void* B) = 0;                        // 28
    virtual GENRESULT DACOM_API Unused29(void* A, void* B, void* C, void* D, void* E, void* F,
                                          void* G, void* H, void* I) = 0;                // 29
    virtual GENRESULT DACOM_API Unused30(void* A, void* B) = 0;                        // 30
    virtual GENRESULT DACOM_API Unused31(void* A, void* B) = 0;                        // 31
    virtual GENRESULT DACOM_API Unused32(void* A, void* B) = 0;                        // 32
    virtual GENRESULT DACOM_API Unused33(void* A, void* B, void* C) = 0;               // 33
    virtual GENRESULT DACOM_API Unused34(void* A, void* B, void* C, void* D) = 0;      // 34
    virtual GENRESULT DACOM_API GetOffset(void* Instance, float* Out) = 0;            // 35
    virtual GENRESULT DACOM_API WalkHardpoints(void* Instance, void* UserFn,
                                               void* UserData) = 0;                    // 36
    virtual GENRESULT DACOM_API GetMeshStats(void* Instance, unsigned int* Out) = 0;      // 37
    virtual GENRESULT DACOM_API GetBoundSphere(void* Instance, float* OutRadius,
                                                 Vector* OutCenter) = 0;                // 38
};
