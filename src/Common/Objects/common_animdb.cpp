// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// common_animdb.cpp -- AnimDB subsystem (Common.dll). Shutdown FUN_0627d470,
// Add(VChannel*) FUN_0627d4a0, Rem(VChannel*) FUN_0627d680, Add(int) FUN_0627d8a0,
// Rem(int) FUN_0627dc80, Update FUN_0627dda0, Count FUN_0627e480.
//
// Two collections share one 0x10-byte value type, FAnimSub:
//   - the VChannel list (DAT_063fbff0), whose nodes embed a VC6 std::list sub-object;
//   - the PlaybackId collection (DAT_063fbffc), a VC6 std::_Tree with unique keys (its insert
//     FUN_0627eae0 returns pair<iterator,bool> and Add(int) stops on false), ported as
//     std::map<int, FAnimSub>.
// The sub-lists hold CObject* (the GetInstanceUserData result; Update reads each element's
// +4 as an engine instance id).

#include "common_cobject.h"
#include "common_csys.h"  // Acquire3DMathEngine
#include "common_physys.h"  // FLControllerGolem entry points, PhySys::GetCurrentTime
#include "ivp_physics/ivp_core.h"  // ::IVP_Time, GetCurrentTime's return type
#include "common_animdb.h"
#include "dacom.h"
#include "x86math.h"  // I3DMathEngine::MatrixToQuaternion, Quaternion, Matrix

#include <DALib.h>

#include <algorithm>
#include <list>
#include <map>
#include <vector>

// AnimDB::VChannel (abstract, Tick slot 0 / GetId slot 1, both __thiscall) is declared in
// common_animdb.h.

namespace AnimDB {

// ============================================================================
// FAnimSub -- the value type of both collections. Update() pushes a pointer to it from
// either collection (tree node +0x10, list node +0x0C) into one vector<FAnimSub*>.
//   +0x00  Root (CObject*) -- GetRoot() of the resolved engine instance
//   +0x04  embedded std::list<CObject*>: allocator byte, _Myhead (+0x08), _Mysize (+0x0C)
// ============================================================================

struct FAnimSub
{
    CObject* Root = nullptr;     // +0x00
    std::list<CObject*> Members; // +0x04 alloc / +0x08 _Myhead / +0x0C _Mysize
};

// ============================================================================
// FAnimEntry -- node of the VChannel list (DAT_063fbff0 sentinel), 0x1c bytes in retail.
// ============================================================================

struct FAnimEntry
{
    FAnimEntry* Next;   // +0x00
    FAnimEntry* Prev;   // +0x04
    VChannel* Channel;  // +0x08
    FAnimSub Sub;       // +0x0C .. +0x1B
};

// ============================================================================
// The PlaybackId collection (DAT_063fbffc). Retail _Tree node: +0x00 Left, +0x04 Parent,
// +0x08 Right, +0x0C key (PlaybackId), +0x10..+0x1F FAnimSub, +0x20/+0x21 Color/Isnil
// (helpers FUN_0627f3b0/f600/f620/f670/f6a0/f750/f780).
// ============================================================================

using PlayMap = std::map<int, FAnimSub>;

// ============================================================================
// CObject fields AnimDB::Update reads (see common_cobject.h for how each was established).
// ============================================================================

static_assert(offsetof(CObject, PhysicsSystemId) == 0x50, "CObject+0x50 PhySys system id");
static_assert(offsetof(CObject, GolemController) == 0x60, "CObject+0x60 golem force controller");
static_assert(offsetof(CObject, PhysicsControlMode) == 0x74, "CObject+0x74 physics control mode");
static_assert(sizeof(CObject) == 0x9c, "CObject sizeof 0x9c -- every offset above is inside it");

static int GetTransformMode(CObject const* Obj)
{
    return Obj->PhysicsControlMode;
}

static IVP_Controller* GetGolemController(CObject const* Obj)
{
    return Obj->GolemController;
}

// ============================================================================
// Global state — mirrors retail statics
// ============================================================================

static FAnimEntry* G_VChannelSentinel = nullptr;  // DAT_063fbff0
static int G_VChannelCount = 0;                    // DAT_063fbff4

static PlayMap G_PlayMap;                          // DAT_063fbffc (std::_Tree)

static FAnimEntry* G_FreeList = nullptr;           // DAT_063fc01c
static int G_FreeCount = 0;                        // DAT_063fc028

// ============================================================================
// Internal helpers
// ============================================================================

static FAnimEntry* AllocEntry()
{
    // Retail FUN_0627f210 pops the free list (DAT_063fc01c) or operator_new(0x1c)s a node;
    // FUN_0627f810 then copy-constructs the payload. Recycled entries stay live C++ objects,
    // so their sub-list is already empty; the clear() is defensive.
    FAnimEntry* Entry;
    if (G_FreeList != nullptr)
    {
        Entry = G_FreeList;
        G_FreeList = G_FreeList->Next;
        G_FreeCount--;
    }
    else
    {
        Entry = new FAnimEntry();
    }
    Entry->Next = nullptr;
    Entry->Prev = nullptr;
    Entry->Channel = nullptr;
    Entry->Sub.Root = nullptr;
    Entry->Sub.Members.clear();
    return Entry;
}

static void FreeEntry(FAnimEntry* Entry)
{
    // No null test: every retail recycle site hands it a live node. Retail destroys the
    // embedded sub-list (FUN_0627dbb0) before recycling the node; clear() is the equivalent.
    Entry->Sub.Members.clear();
    Entry->Channel = nullptr;
    Entry->Sub.Root = nullptr;
    if (G_FreeCount < 0x7FFFFFFF)
    {
        Entry->Next = G_FreeList;
        G_FreeList = Entry;
        G_FreeCount++;
    }
    else
    {
        delete Entry;
    }
}

static void UnlinkVChannel(FAnimEntry* Entry)
{
    Entry->Prev->Next = Entry->Next;
    Entry->Next->Prev = Entry->Prev;
}

// Frees the recycled-node pool (DAT_063fc01c, depth DAT_063fc028). Retail inlines this loop
// in Shutdown, the list ctor and the list dtor.
static void FlushFreeList()
{
    FAnimEntry* Cur = G_FreeList;
    while (Cur != nullptr)
    {
        FAnimEntry* Next = Cur->Next;
        delete Cur;
        Cur = Next;
    }
    G_FreeList = nullptr;
    G_FreeCount = 0;
}

// DAT_063fc020: how many live lists share the node pool. Only the global VChannel list
// touches it, so it is 1 from DLL load until the atexit dtor drops it to 0 and drains the pool.
static int G_PoolListCount = 0;

// The VChannel list object DAT_063fbfec (+0 allocator, +4 head = DAT_063fbff0,
// +8 size = DAT_063fbff4). Static initialiser LAB_0627d390 (table slot 063ea04c) calls the
// ctor FUN_0627d3b0 and registers atexit(LAB_0627d460), which jumps to the dtor FUN_0627e690.
struct FVChannelList
{
    // FUN_0627d3b0
    FVChannelList()
    {
        // Two allocator copies are counted (the default-argument temporary and the list's own).
        G_PoolListCount += 2;

        FAnimEntry* Head;
        if (G_FreeList != nullptr)
        {
            Head = G_FreeList;
            G_FreeList = Head->Next;
            --G_FreeCount;
        }
        else
        {
            Head = new FAnimEntry();
        }

        Head->Next = Head;
        Head->Prev = Head;
        G_VChannelSentinel = Head;
        G_VChannelCount = 0;

        // The temporary allocator dies; drain the pool at zero.
        if (--G_PoolListCount == 0)
            FlushFreeList();
    }

    // FUN_0627e690
    ~FVChannelList()
    {
        FAnimEntry* const Head = G_VChannelSentinel;
        FAnimEntry* Node = Head->Next;
        while (Node != Head)
        {
            FAnimEntry* const Next = Node->Next;
            Node->Prev->Next = Node->Next;
            Node->Next->Prev = Node->Prev;
            FreeEntry(Node);  // FUN_0627f900 payload dtor, then pool-or-delete
            --G_VChannelCount;
            Node = Next;
        }

        // The head goes back to the pool too.
        FreeEntry(Head);

        G_VChannelSentinel = nullptr;
        G_VChannelCount = 0;

        if (--G_PoolListCount == 0)
            FlushFreeList();
    }
};

static FVChannelList G_VChannelList;   // DAT_063fbfec

// ============================================================================
// Exported functions
// ============================================================================

void __cdecl Shutdown()
{
    // FUN_0627d470. Frees only the recycled-node pool; the live VChannel list and PlaybackId
    // map are left alone (the list head lives until the atexit dtor).
    FlushFreeList();
}

void __cdecl Add(VChannel* Chan)
{
    // FUN_0627d4a0. No null test on Chan.
    FAnimEntry* Cur = G_VChannelSentinel->Next;
    while (Cur != G_VChannelSentinel)
    {
        if (Cur->Channel == Chan)
            return;
        Cur = Cur->Next;
    }

    int ChanId = Chan->GetId();
    if (ChanId == -1)
        return;  // Unbound channel, skip

    FAnimEntry* Entry = AllocEntry();
    Entry->Next = G_VChannelSentinel;
    Entry->Prev = G_VChannelSentinel->Prev;
    G_VChannelSentinel->Prev->Next = Entry;
    G_VChannelSentinel->Prev = Entry;
    G_VChannelCount++;

    Entry->Channel = Chan;

    // Retail calls GetId() three times and does not test Engine or the user data.
    CObject* const InstObj = static_cast<CObject*>(DALib::Engine->GetInstanceUserData(Chan->GetId()));
    Entry->Sub.Root = GetRoot(InstObj);

    if (InstObj->EngineInstanceId == Chan->GetId())
    {
        // The element is the engine instance's CObject, not the channel.
        Entry->Sub.Members.push_back(InstObj);
    }
}

void __cdecl Rem(VChannel const* Chan)
{
    // FUN_0627d680. First entry with Channel == Chan (pointer compare, no null test).
    FAnimEntry* Cur = G_VChannelSentinel->Next;
    while (Cur != G_VChannelSentinel)
    {
        if (Cur->Channel == Chan)
        {
            UnlinkVChannel(Cur);
            FreeEntry(Cur);
            G_VChannelCount--;
            return;
        }
        Cur = Cur->Next;
    }
}

// LAB_0627d700 (__cdecl): the IAnimation2::EnumObjects callback Add(int) passes. Context is the
// std::vector; always returns 1 so enumeration continues.
static unsigned char __cdecl CollectChannel(void* Channel, void* Context)
{
    static_cast<std::vector<void*>*>(Context)->push_back(Channel);
    return 1;
}

COMMON_API void __cdecl Add(int PlaybackId)
{
    // FUN_0627d8a0. No guards on the id, DALib::Anim / Channel / Engine or the resolved
    // instance, as in retail. An id already present is a no-op. The scratch-list pool
    // bookkeeping around the insert (DAT_063fc420/424/42c) is VC6 allocator internals.
    std::pair<PlayMap::iterator, bool> const Ins = G_PlayMap.try_emplace(PlaybackId);
    PlayMap::iterator const It = Ins.first;
    if (!Ins.second)
        return;
    FAnimSub& Entry = It->second;

    // IAnimation2 slot 35 EnumObjects; its result is ignored.
    std::vector<void*> Channels;
    DALib::Anim->EnumObjects(&CollectChannel, (unsigned long)PlaybackId, &Channels);

    // LastInstance is written before the EngineInstanceId compare, so a non-matching instance
    // still counts as the last one resolved.
    CObject* LastInstance = nullptr;
    for (std::vector<void*>::const_iterator ChanIt = Channels.begin(); ChanIt != Channels.end(); ++ChanIt)
    {
        unsigned long Header[4];
        DALib::Channel->GetHeader(Header, (unsigned long)*ChanIt);   // IChannel slot 16
        if (Header[0] != 0)
            continue;
        CObject* const Inst = static_cast<CObject*>(DALib::Engine->GetInstanceUserData(Header[3]));
        LastInstance = Inst;
        if (Inst->EngineInstanceId == (int)Header[3])   // no null test
        {
            Entry.Members.push_back(Inst);
        }
    }

    if (LastInstance != nullptr)
    {
        Entry.Root = GetRoot(LastInstance);
    }
    else
    {
        // Retail re-derives [lower_bound, upper_bound) for PlaybackId and erases it
        // (FUN_0627f6a0/f670/f920/f0d0), which removes the entry just inserted.
        G_PlayMap.erase(It);
    }
}

COMMON_API void __cdecl Rem(int PlaybackId)
{
    // FUN_0627dc80. Retail erases [lower_bound, upper_bound) with a whole-tree fast path.
    std::pair<PlayMap::iterator, PlayMap::iterator> const Range = G_PlayMap.equal_range(PlaybackId);
    G_PlayMap.erase(Range.first, Range.second);
}

// ============================================================================
// Update() helpers
// ============================================================================

// FUN_0627e7d0: linear find (FUN_0627f260, not a sorted lower_bound), append on a miss, re-find
// after the insert. Update() open-codes it three more times. The scratch sets stay
// insertion-ordered vectors because that order is retail's phase-C visiting order.
template <typename T>
static std::pair<typename std::vector<T>::iterator, bool> InsertUnique(std::vector<T>& Vec, T Value)
{
    typename std::vector<T>::iterator It = std::find(Vec.begin(), Vec.end(), Value);
    if (It != Vec.end())
        return std::pair<typename std::vector<T>::iterator, bool>(It, false);
    Vec.insert(Vec.end(), 1, Value);
    It = std::find(Vec.begin(), Vec.end(), Value);
    return std::pair<typename std::vector<T>::iterator, bool>(It, true);
}

// FUN_0627e3b0, recursive scene-graph walk. A child with its own CObject (non-null and not Obj)
// is added and becomes the current object; otherwise the walk recurses with Obj. Either way it
// descends on the child's instance id, so instances without a CObject are still traversed.
static void CollectInstanceChildren(CObject* Obj, long InstanceId, std::vector<CObject*>& Objs)
{
    // IEngine carries the instance id in a void* (engine/engbase.h).
    long Child = (long)(intptr_t)DALib::Engine->GetRelatedInstance(InstanceId, 1,
                                                                   InvalidInstanceIndex);
    while (Child != -1)
    {
        CObject* const ChildObj = static_cast<CObject*>(DALib::Engine->GetInstanceUserData(Child));
        if (ChildObj != nullptr && ChildObj != Obj)
        {
            InsertUnique(Objs, ChildObj);
            CollectInstanceChildren(ChildObj, Child, Objs);
        }
        else
        {
            CollectInstanceChildren(Obj, Child, Objs);
        }
        Child = (long)(intptr_t)DALib::Engine->GetRelatedInstance(InstanceId, 1,
                                                                  (intptr_t)Child);
    }
}

void __cdecl Update(float Dt)
{
    // FUN_0627dda0. Phases, with three scratch sets (V1 instance ids, V2 FAnimSub* whose Root
    // has a body, V3 CObject* to push into physics):
    //   A. PlaybackId map: MixerWalk each Root's instance id the first time it enters V1;
    //      record physical Roots in V2.
    //   B. VChannel list: Tick each channel, same V1 insert (no MixerWalk) and V2 insert.
    //   C. Engine->UpdateInstance for every id in V1.
    //   D. Every member of every V2 entry, plus its engine child subtree, goes into V3.
    //   E. Push each V3 object's transform into physics by PhysicsControlMode.
    //   F. Clear the VChannel list (FUN_0627f190): Add(VChannel*) registration is per frame.
    //   G. Erase every map entry for which FilterObjects(key) is non-zero.
    // Retail has no early-out and no null checks on Root, the members, DALib::Anim,
    // DALib::Engine or the math singleton.

    std::vector<int> SeenIds;     // retail V1
    std::vector<FAnimSub*> Subs;  // retail V2

    // ---- Phase A ----------------------------------------------------------
    for (PlayMap::iterator It = G_PlayMap.begin(); It != G_PlayMap.end(); ++It)
    {
        FAnimSub& Sub = It->second;
        int const InstanceId = Sub.Root->EngineInstanceId;
        if (InsertUnique(SeenIds, InstanceId).second)
        {
            // IAnimation2 slot 27. engbase.h calls the parameter "PlaybackId", but retail
            // passes the root's engine instance id, not the map key.
            DALib::Anim->MixerWalk((unsigned long)InstanceId, Dt);
        }
        if (Sub.Root->BodyPtr != nullptr)
            Subs.push_back(&Sub);
    }

    // ---- Phase B ----------------------------------------------------------
    // The head always exists (static FVChannelList).
    for (FAnimEntry* Cur = G_VChannelSentinel->Next; Cur != G_VChannelSentinel; Cur = Cur->Next)
    {
        Cur->Channel->Tick(Dt);
        // Result discarded; the insert keeps phase C from updating an instance twice.
        InsertUnique(SeenIds, Cur->Sub.Root->EngineInstanceId);
        if (Cur->Sub.Root->BodyPtr != nullptr)
            Subs.push_back(&Cur->Sub);
    }

    // ---- Phase C: IEngine slot 27 -----------------------------------------
    for (std::vector<int>::const_iterator It = SeenIds.begin(); It != SeenIds.end(); ++It)
        DALib::Engine->UpdateInstance(*It, 0, nullptr);

    // ---- Phase D: FUN_0627e3b0 is open-coded one level deep at the call site --
    std::vector<CObject*> Objs;  // retail V3
    for (std::vector<FAnimSub*>::const_iterator SubIt = Subs.begin(); SubIt != Subs.end(); ++SubIt)
    {
        FAnimSub const& Sub = **SubIt;
        for (std::list<CObject*>::const_iterator MemberIt = Sub.Members.begin();
             MemberIt != Sub.Members.end(); ++MemberIt)
        {
            CObject* const Member = *MemberIt;
            InsertUnique(Objs, Member);
            CollectInstanceChildren(Member, Member->EngineInstanceId, Objs);
        }
    }

    // ---- Phase E ----------------------------------------------------------
    for (std::vector<CObject*>::const_iterator It = Objs.begin(); It != Objs.end(); ++It)
    {
        CObject* const Obj = *It;
        int const Mode = GetTransformMode(Obj);  // CObject+0x74
        if (Mode == PhySys::ANIM_TECHNIQUE_GOLEM)
        {
            // Mode 1: tell the golem controller (+0x60) "be here, at rest" with the live
            // position, IVP-ordered orientation and one PhySys time; the controller's own tick
            // does the blending. Setters FUN_06343580 then FUN_06343670.
            FLControllerGolem* const Golem =
                reinterpret_cast<FLControllerGolem*>(GetGolemController(Obj));  // CObject+0x60
            if (Golem != nullptr)
            {
                IVP_Time const Now = PhySys::GetCurrentTime(Obj->PhysicsSystemId);

                // Lazily cached singleton (DAT_063fc018), no null test.
                I3DMathEngine* const GolemMath = Acquire3DMathEngine();

                // Same (W,X,Y,Z) -> (X,Y,Z,W) rotation as the mode-2 branch.
                Quaternion GolemQuat = {0.0f, 0.0f, 0.0f, 0.0f};
                GolemMath->MatrixToQuaternion(GolemQuat, Obj->Orientation);
                IVP_U_Quat const GolemIvpQuat = {GolemQuat.x, GolemQuat.y, GolemQuat.z, GolemQuat.w};

                Vector const GolemZeroSpeed = {0.0f, 0.0f, 0.0f};

                PhySys::GolemSetTargetPosition(Golem, Obj->WorldPosition, GolemZeroSpeed, Now);
                PhySys::GolemSetTargetQuaternion(Golem, &GolemIvpQuat, Now, nullptr, 0.0f);
            }
            continue;
        }
        if (Mode != PhySys::ANIM_TECHNIQUE_MERGED)
            continue;

        Vector const Pos = Obj->WorldPosition;  // +0x2c..+0x34

        // Lazily cached singleton (DAT_063fc018), no null test.
        I3DMathEngine* const Math = Acquire3DMathEngine();

        Quaternion MathQuat = {0.0f, 0.0f, 0.0f, 0.0f};
        Math->MatrixToQuaternion(MathQuat, Obj->Orientation);   // slot +0x50

        // Retail rotates the dwords by one slot: Quaternion (W,X,Y,Z) becomes IVP's (X,Y,Z,W).
        IVP_U_Quat const IvpQuat = {MathQuat.x, MathQuat.y, MathQuat.z, MathQuat.w};

        // FUN_06343cc0(0, BodyPtr, IvpQuat, &Pos, 0), see common_controllergolem.cpp. The fifth
        // argument is never read by the callee, so the port drops it.
        PhySys::BeamBodyToWorldPose(nullptr, Obj->BodyPtr, &IvpQuat, Pos);
    }

    // ---- Phase F: FUN_0627f190, list::erase(begin, end) ---------------------
    {
        FAnimEntry* Cur = G_VChannelSentinel->Next;
        while (Cur != G_VChannelSentinel)
        {
            FAnimEntry* const Next = Cur->Next;  // FreeEntry overwrites Cur->Next
            FreeEntry(Cur);
            Cur = Next;
        }
        G_VChannelSentinel->Next = G_VChannelSentinel;
        G_VChannelSentinel->Prev = G_VChannelSentinel;
        G_VChannelCount = 0;
    }

    // ---- Phase G: IAnimation2 slot 17 FilterObjects, with the map key ------
    PlayMap::iterator PlayIt = G_PlayMap.begin();
    while (PlayIt != G_PlayMap.end())
    {
        if (DALib::Anim->FilterObjects((void*)(intptr_t)PlayIt->first) != 0)
            PlayIt = G_PlayMap.erase(PlayIt);
        else
            ++PlayIt;
    }
}

unsigned int __cdecl Count()
{
    // FUN_0627e480
    return (unsigned int)G_PlayMap.size() + (unsigned int)G_VChannelCount;
}

} // namespace AnimDB
