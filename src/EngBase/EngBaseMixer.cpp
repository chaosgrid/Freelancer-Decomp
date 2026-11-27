// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Animation mixer: linked leaf, blend and overlay nodes evaluate channel values.

#include "x86math.h"
#include "EngBase.h"

#include <cmath>
#include <cstring>
#include <new>

namespace
{
static_assert(sizeof(Quaternion) == 4 * sizeof(float), "quaternion transport is four floats");
static_assert(sizeof(Matrix) == 9 * sizeof(float), "matrix transport is nine floats");

enum EMixerNodeType : unsigned long
{
    MixerLeafNode = 0,
    MixerBlendNode = 1,
    MixerOverlayNode = 2
};

// FDUMP arguments at 06615553: flags 0x100000, line 0x3D0 and retail's __FILE__
// (.data:0662a208, read from the PE bytes).
const int G_MixerFdumpFatal = 0x100000;
const char* const G_MixerFdumpFile =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\EngBase\\Anim\\AnimComponent.cpp";

// DAT_06629508, double 0x3F893B352540C735 (pi / 255).
const double G_MixerEaseStep = 0x1.93B352540C735p-7;

// DAT_0662acf8 float[256] and its ready byte DAT_0662b0fc.
float G_MixerEaseTable[0x100];
unsigned char G_bMixerEaseReady = 0;

// MSVCRT _ftol: truncation to a 64-bit integer; the callers keep EAX only.
long MixerFtol(double Value)
{
    return (long)((long long)(Value));
}

// The joint-type table FUN_066152d0 and FUN_0661d5b0 share: IEngine+0xC4's first dword to the
// number of floats the joint's parameters take.
unsigned long MixerJointValueCount(unsigned long JointType)
{
    switch (JointType)
    {
    case kJointTypePris:
    case kJointTypeRev:
        return 1;
    case kJointTypeCyl:
        return 2;
    case kJointTypeSphere:
        return 4;
    case kJointTypeTrans:
        return 3;
    case kJointTypeLoose:
        return 7;
    default:
        return 0;
    }
}

// FUN_0661d050: the ease table, built on first use, indexed without a range check.
float MixerEase(long Index)
{
    if (G_bMixerEaseReady == 0)
    {
        long Step = 0;
        while (Step < 0x100)
        {
            G_MixerEaseTable[Step] = (float)(
                (1.0 - std::cos((double)(Step) * G_MixerEaseStep)) * 0.5);
            Step += 1;
        }
        G_bMixerEaseReady = 1;
    }
    return G_MixerEaseTable[Index];
}

// FUN_0661d460: vec3 lerp, then 3DMath +0x58 slerp of the quaternions at +0xC into a local,
// copied to Out+3.
void MixerBlendPosQuat(float* Out, const float* A, const float* B, float T)
{
    float Local[4];
    Local[0] = (float)(((double)(B[0]) - A[0]) * T + A[0]);
    Local[1] = (float)(((double)(B[1]) - A[1]) * T + A[1]);
    Local[2] = (float)(((double)(B[2]) - A[2]) * T + A[2]);
    Out[0] = Local[0];
    Out[1] = Local[1];
    Out[2] = Local[2];
    I3DMathEngine* Math = Acquire3DMathEngine();                 // DAT_0662b0f8
    Math->QuatSlerp(*reinterpret_cast<Quaternion*>(Local),
                    *reinterpret_cast<const Quaternion*>(A + 3),
                    *reinterpret_cast<const Quaternion*>(B + 3), T);
    Out[3] = Local[0];
    Out[4] = Local[1];
    Out[5] = Local[2];
    Out[6] = Local[3];
}

// FUN_0661d510: both quaternions go through locals before the slerp.
void MixerSlerpQuat(float* Out, const float* A, const float* B, float T)
{
    Quaternion LocalB;
    Quaternion LocalA;
    Quaternion Result;
    std::memcpy(&LocalB, B, sizeof(LocalB));
    std::memcpy(&LocalA, A, sizeof(LocalA));
    I3DMathEngine* Math = Acquire3DMathEngine();                 // DAT_0662b0f8
    Math->QuatSlerp(Result, LocalA, LocalB, T);
    std::memcpy(Out, &Result, sizeof(Result));
}

// Tail append, inline in FUN_06616090 and FUN_066152d0.
void MixerAppend(FMixerList* List, FMixerNode* Node)
{
    FMixerNode* Tail = List->Tail;
    List->Tail = Node;
    if (Tail == nullptr)
        List->Head = Node;
    else
        Tail->Next = Node;
    Node->Next = nullptr;
    Node->Prev = Tail;
    List->Count += 1;
}

// Insert before Where, inline in FUN_066152d0 and FUN_06615ec0. A null Where makes the node
// the tail.
void MixerInsertBefore(FMixerList* List, FMixerNode* Where, FMixerNode* Node)
{
    FMixerNode* Before = nullptr;
    if (Where == nullptr)
    {
        Before = List->Tail;
        List->Tail = Node;
    }
    else
    {
        Before = Where->Prev;
        Where->Prev = Node;
    }
    if (Before == nullptr)
        List->Head = Node;
    else
        Before->Next = Node;
    Node->Next = Where;
    Node->Prev = Before;
    List->Count += 1;
}

// Sets PlayFlags (+0x34) bit 1 on a leaf that is about to be dropped.
void MixerMarkDropped(FMixerNode* Node)
{
    if (Node->Type == MixerLeafNode)
        static_cast<FMixerLeaf*>(Node)->Typed->PlayFlags |= G_ChannelBlendFlag;
}
} // namespace

// FUN_0661d0b0. No bound check in retail.
void FMixerStack::Push(FMixerNode* Node)
{
    Nodes[Count] = Node;
    Count += 1;
}

// FUN_0661d0d0: 0 when empty.
FMixerNode* FMixerStack::Pop()
{
    if (Count == 0)
        return nullptr;
    Count -= 1;
    return Nodes[Count];
}

// FUN_0661d0f0: pop, then tail-jump into the node's Evaluate, reusing the Kind argument slot
// for the stack pointer. Kind is never read.
void FMixerStack::Evaluate(float* Out, unsigned long Kind)
{
    (void)Kind;
    FMixerNode* Node = Pop();
    if (Node == nullptr)
        return;
    Node->Evaluate(Out, this);
}

// ---------------------------------------------------------------------------------------------
// Node methods
// ---------------------------------------------------------------------------------------------

// LAB_0661d110: tail-jumps to FUN_0661b420.
unsigned long FMixerLeaf::Tick(float Dt)
{
    return Typed->Tick(Dt);
}

// LAB_0661d120: typed slot 0x18. The stack argument is not read.
void FMixerLeaf::Evaluate(float* Out, FMixerStack* Stack)
{
    (void)Stack;
    Typed->CopySampled(Out);
}

// LAB_06616400: tail-jumps to typed slot 0x34.
void FMixerLeaf::UpdateDeltaRate(float Dt)
{
    Typed->UpdateDeltaRate(Dt);
}

// FUN_06616410: typed slot 0x38.
Vector* FMixerLeaf::SampleDisplacement(Vector* Out, float Dt)
{
    Typed->Step(Out, Dt);
    return Out;
}

// LAB_0661d130: the unrounded sum is compared; "less" and unordered answer 0, so only
// Sum >= Duration answers 1.
unsigned long FMixerBlend::Tick(float Dt)
{
    const double Sum = (double)(Dt) + (double)(Time);
    Time = (float)(Sum);
    if (Sum >= (double)(Duration))
        return 1;
    return 0;
}

// LAB_0661d160.
void FMixerBlend::Evaluate(float* Out, FMixerStack* Stack)
{
    FMixerNode* Right = Stack->Pop();
    FMixerNode* Left = Stack->Pop();
    float LeftValue[7];
    float RightValue[7];
    Left->Evaluate(LeftValue, Stack);
    Right->Evaluate(RightValue, Stack);

    // "Equal", "less" and unordered all keep 0xFF; only Duration > 0 computes an index.
    long WeightIndex = 0xFF;
    if (Duration > 0.0f)
    {
        WeightIndex = MixerFtol(std::floor((double)(InvDuration) *
                                           (double)(Time) * 255.0));
    }
    const float Weight = MixerEase(WeightIndex);

    // The right-hand node if it is a leaf, else the left-hand one if that is.
    FMixerNode* Leaf = nullptr;
    if (Right->Type == MixerLeafNode)
        Leaf = Right;
    else if (Left->Type == MixerLeafNode)
        Leaf = Left;

    if (Leaf != nullptr)
    {
        FTypedChannel* Typed = static_cast<FMixerLeaf*>(Leaf)->Typed;
        // Typed slot 0x0C.
        Typed->InterpolatePose(Out, LeftValue, RightValue,
                                             (double)(Weight));
        if ((Flags & G_MixerFlagPose) == 0)
            return;

        // One axis of the result survives the pose curve, picked by the channel's LoopFlags
        // bits 3/4/5.
        const unsigned long Axis = Typed->LoopFlags;
        float Kept = 0.0f;
        if ((Axis & G_LoopFlagPoseAxisX) != 0)
            Kept = Out[0];
        else if ((Axis & G_LoopFlagPoseAxisY) != 0)
            Kept = Out[1];
        else if ((Axis & G_LoopFlagPoseAxisZ) != 0)
            Kept = Out[2];

        // The float/double split follows retail's rounding points: float locals are spilled
        // to the stack, double ones stay on the FPU.
        const double Inv = (double)(InvDuration);
        const double Now = (double)(Time);
        const double Ctrl0 = (double)(PoseCtrl.x) * 0.5;
        const double Ctrl1 = (double)(PoseCtrl.y) * 0.5;
        const float Ctrl2 = (float)((double)(PoseCtrl.z) * 0.5);
        const float Ctrl0Inv = (float)(Ctrl0 * Inv);
        const double Ctrl1Inv = Ctrl1 * Inv;
        const double Ctrl2InvNow = (double)(Ctrl2) * Inv * Now;
        const float Term0 = (float)((double)(Ctrl0Inv) * Now);
        const float Term1 = (float)(Ctrl1Inv * Now);
        const double Rate0 = (double)(Term0) + PoseRate.x;
        const float Rate1 = (float)((double)(Term1) + PoseRate.y);
        const float Rate2 = (float)(Ctrl2InvNow + PoseRate.z);
        const double Pos0 = Rate0 * Now;
        const double Pos1 = (double)(Rate1) * Now;
        const float Pos2 = (float)((double)(Rate2) * Now);
        Out[0] = (float)(Pos0 + PoseStart.x);
        Out[1] = (float)(Pos1 + PoseStart.y);
        Out[2] = (float)((double)(Pos2) + PoseStart.z);

        // Put the kept axis back. With none of the three bits set retail discards it.
        if ((Axis & G_LoopFlagPoseAxisX) != 0)
            Out[0] = Kept;
        else if ((Axis & G_LoopFlagPoseAxisY) != 0)
            Out[1] = Kept;
        else if ((Axis & G_LoopFlagPoseAxisZ) != 0)
            Out[2] = Kept;
        return;
    }

    // Two overlays, switched on the left one's value count.
    if (Left->Type != MixerOverlayNode || Right->Type != MixerOverlayNode)
        return;
    const FMixerOverlay* LeftOverlay = static_cast<const FMixerOverlay*>(Left);
    const FMixerOverlay* RightOverlay = static_cast<const FMixerOverlay*>(Right);
    const double W = (double)(Weight);
    switch (LeftOverlay->Count)
    {
    case G_MixKindScalar:
        Out[0] = (float)(
            ((double)(RightOverlay->Values[0]) - LeftOverlay->Values[0]) * W +
            LeftOverlay->Values[0]);
        break;
    case G_MixKindVec3:
    {
        float Lerped[3];
        unsigned long Index = 0;
        while (Index < 3)
        {
            Lerped[Index] = (float)(
                ((double)(RightOverlay->Values[Index]) - LeftOverlay->Values[Index]) *
                    W +
                LeftOverlay->Values[Index]);
            Index += 1;
        }
        Out[0] = Lerped[0];
        Out[1] = Lerped[1];
        Out[2] = Lerped[2];
        break;
    }
    case G_MixKindQuat:
        MixerSlerpQuat(Out, LeftOverlay->Values, RightOverlay->Values, Weight);
        break;
    case G_MixKindPosQuat:
        MixerBlendPosQuat(Out, LeftOverlay->Values, RightOverlay->Values, Weight);
        break;
    default:                                                     // 2, 5, 6 and out of range
        break;
    }
}

// FUN_0661ad30: empty.
void FMixerBlend::UpdateDeltaRate(float Dt)
{
    (void)Dt;
}

// LAB_066156d0: forwards to the predecessor.
Vector* FMixerBlend::SampleDisplacement(Vector* Out, float Dt)
{
    Prev->SampleDisplacement(Out, Dt);
    return Out;
}

// FUN_0661d5b0.
FMixerOverlay::FMixerOverlay(const FAnimBindOut* Desc)
{
    Next = nullptr;
    Prev = nullptr;
    bRemove = 0;
    Type = MixerOverlayNode;
    if (Desc->Kind == G_BindKindJoint)
    {
        const FEngineJointBlock* Relative = G_AnimEngine->GetInstanceRelative((InstanceIndex)Desc->Instance);
        if (Relative != nullptr)
        {
            Count = MixerJointValueCount(Relative->Type);
            G_AnimEngine->GetJointParams((InstanceIndex)Desc->Instance, 0, Values);
        }
        // Relative == null leaves Count as operator new handed it over; retail writes nothing
        // there either.
        return;
    }
    if (Desc->Kind == G_BindKindObject)
    {
        Count = G_MixKindPosQuat;
        const Vector* Position = G_AnimEngine->GetInstancePosition((InstanceIndex)Desc->Instance); // IEngine+0x90
        Values[0] = Position->x;
        Values[1] = Position->y;
        Values[2] = Position->z;
        const Matrix* Orientation = G_AnimEngine->GetInstanceOrientation((InstanceIndex)Desc->Instance); // +0x98
        I3DMathEngine* Math = Acquire3DMathEngine();             // DAT_0662b0f8
        Quaternion Quat;
        Math->MatrixToQuaternion(Quat,
                                 *Orientation); // +0x50
        Values[3] = Quat.w;
        Values[4] = Quat.x;
        Values[5] = Quat.y;
        Values[6] = Quat.z;
        return;
    }
    Count = 0;
}

// FUN_0661d700: returns 0.
unsigned long FMixerOverlay::Tick(float Dt)
{
    (void)Dt;
    return 0;
}

// LAB_0661d710: Count dwords from +0x18.
void FMixerOverlay::Evaluate(float* Out, FMixerStack* Stack)
{
    (void)Stack;
    if (Count != 0)
        std::memcpy(Out, Values, Count * sizeof(float));
}

// FUN_0661ad30: empty.
void FMixerOverlay::UpdateDeltaRate(float Dt)
{
    (void)Dt;
}

// LAB_06612170: zero three floats.
Vector* FMixerOverlay::SampleDisplacement(Vector* Out, float Dt)
{
    (void)Dt;
    Out->x = 0.0f;
    Out->y = 0.0f;
    Out->z = 0.0f;
    return Out;
}

// FUN_06612b20 (this = Left). Kind and Instance compare signed, Sink unsigned.
bool FAnimBindLess::operator()(const FAnimBindOut& Left, const FAnimBindOut& Right) const
{
    if (Left.Kind != Right.Kind)
        return (long)(Left.Kind) < (long)(Right.Kind);
    if (Left.Kind == G_BindKindJoint || Left.Kind == G_BindKindObject)
        return (ptrdiff_t)(Left.Instance) < (ptrdiff_t)(Right.Instance);
    if (Left.Kind == G_BindKindRoot)
        return (size_t)(Left.Sink) < (size_t)(Right.Sink);
    return false;
}

// ---------------------------------------------------------------------------------------------
// List primitives
// ---------------------------------------------------------------------------------------------

// FUN_066281d0: operator delete. The node classes have no destructor in retail's vtables.
void MixerFreeNode(FMixerNode* Node)
{
    ::operator delete(Node);
}

// FUN_06617220.
void FMixerList::Unlink(FMixerNode* Node)
{
    if (Node->Prev == nullptr)
        Head = Node->Next;
    else
        Node->Prev->Next = Node->Next;
    if (Node->Next == nullptr)
        Tail = Node->Prev;
    else
        Node->Next->Prev = Node->Prev;
    Count -= 1;
}

// FUN_06617260.
void FMixerList::UnlinkAndFree(FMixerNode* Node)
{
    Unlink(Node);
    MixerFreeNode(Node);
}

// FUN_066172c0: the head is read once, then each successor is taken before its node goes.
void FMixerList::FreeList()
{
    FMixerNode* Node = Head;
    while (Count != 0)
    {
        FMixerNode* Next = Node->Next;
        Unlink(Node);
        MixerFreeNode(Node);
        Node = Next;
    }
}

// FUN_066156f0: drop the blend, what it was fading from and, with bOuter, what it was fading
// to. A blend two back is unwound the same way, recursively.
void FMixerList::UnlinkParentBlend(FMixerNode* Node, unsigned char bOuter)
{
    FMixerNode* Inner = Node->Prev;
    FMixerNode* Outer = Inner->Prev;
    if (Outer->Type == MixerBlendNode)
        UnlinkParentBlend(Outer, 1);
    else
    {
        MixerMarkDropped(Outer);
        Unlink(Outer);
        MixerFreeNode(Outer);
    }
    if (bOuter != 0)
    {
        MixerMarkDropped(Inner);
        Unlink(Inner);
        MixerFreeNode(Inner);
    }
    Unlink(Node);
    MixerFreeNode(Node);
}

// ---------------------------------------------------------------------------------------------
// Maps
// ---------------------------------------------------------------------------------------------

// FUN_06614f70: lower_bound on the signed key plus the equality check; null on a miss.
FMixerDescMap* FAnimationObject::MixerFindKey(long Key)
{
    FMixerKeyMap::iterator It = Mixers->find(Key);
    if (It == Mixers->end())
        return nullptr;
    return &It->second;
}

// FUN_06614e20: find-or-insert, returns the inner map.
FMixerDescMap* FAnimationObject::MixerFindOrAddKey(long Key)
{
    return &(*Mixers)[Key];
}

// The +0x3C half of FUN_06616d90: FUN_06618760 erases every outer node, which runs every
// list's destructor FUN_066172c0. The port also frees the map object here (retail holds it in
// place).
void FAnimationObject::MixerDestroyAll()
{
    FMixerKeyMap::iterator Outer = Mixers->begin();
    while (Outer != Mixers->end())
    {
        FMixerDescMap::iterator Entry = Outer->second.begin();
        while (Entry != Outer->second.end())
        {
            Entry->second.FreeList();
            ++Entry;
        }
        ++Outer;
    }
    delete Mixers;
    Mixers = nullptr;
}

// FUN_066152d0: snapshot one bind key's node list, then tick every node and finish the blends
// that complete. Retail: the 0x140-slot stack snapshot has no bound test (066152f0).
void MixerWalkPlayback(FAnimationObject* Animation, const FAnimBindOut* Desc, FMixerList* List,
                       float Dt)
{
    (void)Animation;

    FMixerNode* Snapshot[0x140];
    long SnapshotCount = 0;
    FMixerNode* Cursor = List->Head;
    while (Cursor != nullptr)
    {
        Snapshot[SnapshotCount] = Cursor;
        SnapshotCount += 1;
        Cursor = Cursor->Next;
    }

    long Index = 0;
    while (Index < SnapshotCount)
    {
        FMixerNode* Node = Snapshot[Index];
        if (Node->Tick(Dt) == 1)
        {
            if (Node->Type == MixerBlendNode)
            {
                FMixerBlend* Blend = static_cast<FMixerBlend*>(Node);
                if ((Blend->Flags & G_MixerFlagPose) != 0)
                {
                    FTypedChannel* PrevTyped = static_cast<FMixerLeaf*>(Blend->Prev)->Typed;
                    const Vector* Position = G_AnimEngine->GetInstancePosition((InstanceIndex)Desc->Instance);
                    PrevTyped->SetOverlayPos(Position);
                }
                if ((Blend->Flags & G_MixerFlagKeep) != 0)
                {
                    FMixerNode* Outer = Blend->Prev->Prev;
                    if (Outer->Type == MixerBlendNode)
                    {
                        List->UnlinkParentBlend(Outer, 1);
                        List->UnlinkAndFree(Node);
                    }
                    else
                    {
                        MixerMarkDropped(Outer);
                        List->Unlink(Outer);
                        MixerFreeNode(Outer);
                        List->UnlinkAndFree(Node);
                    }
                }
                else
                {
                    List->Unlink(Node);
                    MixerFreeNode(Node);
                }
            }
            else if (Node->Type == MixerLeafNode)
            {
                FMixerLeaf* Leaf = static_cast<FMixerLeaf*>(Node);
                FMixerNode* Next = Node->Next;
                if (Next != nullptr)
                {
                    if (Next->Type == MixerBlendNode || (Next->Next != nullptr && Next->Next->Type == MixerBlendNode))
                        Leaf->Typed->ApplyFromArchetype();
                    else
                    {
                        List->Unlink(Node);
                        MixerFreeNode(Node);
                    }
                }
                else
                {
                    FMixerNode* Before = Node->Prev;
                    if (Before == nullptr)
                    {
                        Leaf->Typed->ApplyFromArchetype();
                        Node->bRemove = 1;
                    }
                    else
                    {
                        // A blend back from this leaf to what precedes it.
                        FMixerBlend* Back = new FMixerBlend;
                        Back->Next = nullptr;
                        Back->Duration = List->BlendDuration;
                        Back->Prev = nullptr;
                        Back->Time = 0.0f;
                        Back->bRemove = 0;
                        Back->Type = MixerBlendNode;
                        // (byte & 0xFD) | 1 on memory operator new did not clear: bit 1 off,
                        // bit 0 on; bits 2..7 are never read, so the port stores them as zero.
                        Back->Flags = 1;
                        Back->InvDuration = (float)(1.0 / (double)(Back->Duration));
                        Leaf->Typed->ApplyFromArchetype();
                        List->Unlink(Node);
                        MixerInsertBefore(List, Before, Node);
                        MixerAppend(List, Back);
                    }
                }
            }
        }
        Index += 1;
    }

    // Every node, head first, onto a zeroed stack.
    FMixerStack Stack;
    std::memset(&Stack, 0, sizeof(Stack));
    Cursor = List->Head;
    while (Cursor != nullptr)
    {
        Stack.Push(Cursor);
        Cursor = Cursor->Next;
    }

    if (Stack.Count != 0)
    {
        unsigned long Kind = 0;
        if (Desc->Kind == G_BindKindJoint)
        {
            const FEngineJointBlock* Relative = G_AnimEngine->GetInstanceRelative((InstanceIndex)Desc->Instance);
            if (Relative != nullptr)
                Kind = MixerJointValueCount(Relative->Type);
        }
        else if (Desc->Kind == G_BindKindObject)
            Kind = G_MixKindPosQuat;
        else if (Desc->Kind == G_BindKindRoot)
            Kind = 0;
        else
        {
            EngBaseFdump(G_MixerFdumpFatal, G_MixerFdumpFile, 0x3D0, "FATAL",
                         "Unknown data type!\n");
            Kind = (unsigned long)(Index);            // retail reuses the loop counter's register
        }

        float Out[7];
        Stack.Evaluate(Out, Kind);

        if (Desc->Kind == G_BindKindJoint)
            G_AnimEngine->SetJointParams((InstanceIndex)Desc->Instance, 0, Out);
        else if (Desc->Kind == G_BindKindObject)
        {
            G_AnimEngine->SetInstancePosition((InstanceIndex)Desc->Instance, reinterpret_cast<const Vector*>(Out)); // IEngine+0x8C
            Matrix Mat;
            I3DMathEngine* Math = Acquire3DMathEngine();                   // DAT_0662ace8
            Math->QuaternionToMatrix(Mat,
                                     *reinterpret_cast<const Quaternion*>(Out + 3)); // +0x54
            G_AnimEngine->SetInstanceOrientation((InstanceIndex)Desc->Instance, &Mat); // IEngine+0x94
        }
    }

    // Drop the nodes flagged in the first pass.
    FMixerNode* Walk = List->Head;
    while (Walk != nullptr)
    {
        FMixerNode* Next = Walk->Next;
        if (Walk->bRemove != 0)
        {
            List->Unlink(Walk);
            MixerFreeNode(Walk);
        }
        Walk = Next;
    }
}

void MixerBlendAttach(FAnimationObject* Animation, FTypedChannel* Typed, float Duration,
                      unsigned long Flags)
{
    FMixerDescMap* Inner = Animation->MixerFindOrAddKey(TypedMixerKey(Typed));
    if (Inner == nullptr)                                        // retail tests it
        return;

    FAnimBindOut* Desc = TypedBindDesc(Typed);
    FMixerDescMap::iterator Entry = Inner->find(*Desc);          // FUN_06618fa0
    if (Entry == Inner->end())
    {
        // The list's +0xC word is copied from an uninitialised stack slot in retail; the port
        // starts it at 0.
        FMixerList Empty;
        Empty.Head = nullptr;
        Empty.Tail = nullptr;
        Empty.Count = 0;
        Empty.BlendDuration = 0.0f;
        Entry = Inner->insert(FMixerDescMap::value_type(*Desc, Empty)).first;
    }
    FMixerList* List = &Entry->second;

    FMixerNode* Walk = List->Head;
    while (Walk != nullptr)
    {
        if (Walk->Type == MixerLeafNode && static_cast<FMixerLeaf*>(Walk)->Typed == Typed)
            return;
        Walk = Walk->Next;
    }

    FMixerNode* Anchor = nullptr;
    unsigned char bBlend = 0;
    // "Equal", "less" and unordered skip the blend; only Duration > 0 falls through.
    if (Duration > 0.0f)
    {
        bBlend = 1;
        if (List->Count == 0)
        {
            Flags &= ~G_LoopFlagKeepOutgoing;
            FMixerOverlay* Overlay = new FMixerOverlay(&Entry->first);
            MixerAppend(List, Overlay);
            Anchor = Overlay;
        }
        else
            Anchor = List->Tail;
    }
    else
    {
        if (List->Count != 0)
        {
            Walk = List->Head;
            while (Walk != nullptr)
            {
                MixerMarkDropped(Walk);
                Walk = Walk->Next;
            }
            Walk = List->Head;
            while (List->Count != 0)
            {
                FMixerNode* Next = Walk->Next;
                List->Unlink(Walk);
                MixerFreeNode(Walk);
                Walk = Next;
            }
        }
    }

    FMixerLeaf* Leaf = new FMixerLeaf;
    Leaf->Typed = Typed;
    Leaf->Next = nullptr;
    Leaf->Prev = nullptr;
    Leaf->bRemove = 0;
    Leaf->Type = MixerLeafNode;
    MixerAppend(List, Leaf);
    if (bBlend == 0)
        return;

    FMixerBlend* Blend = new FMixerBlend;
    Blend->InvDuration = (float)(1.0 / (double)(Duration));
    Blend->Next = nullptr;
    Blend->Prev = nullptr;
    Blend->bRemove = 0;
    Blend->Type = MixerBlendNode;
    Blend->Time = 0.0f;
    Blend->Duration = Duration;
    // (byte & 0xFD) | 1 on uncleared memory; bits 2..7 are never read, stored as zero here.
    Blend->Flags = G_MixerFlagKeep;
    if ((signed char)(Flags & 0xFF) < 0)              // Flags bit 7: keep the outgoing side
        Blend->Flags &= ~G_MixerFlagKeep;
    MixerAppend(List, Blend);
    List->BlendDuration = Duration;

    if (Typed->Kind != G_BindKindObject)
        return;
    if ((Flags & (G_LoopFlagPoseAxisX | G_LoopFlagPoseAxisY | G_LoopFlagPoseAxisZ)) == 0)
        return;

    Vector Predicted;
    Leaf->SampleDisplacement(&Predicted, Duration);
    Blend->Flags |= G_MixerFlagPose;
    const Vector* Start = G_AnimEngine->GetInstancePosition(Typed->Instance);
    Blend->PoseStart.x = Start->x;
    Blend->PoseStart.y = Start->y;
    Blend->PoseStart.z = Start->z;
    Vector AnchorOut;
    const Vector* Rate = Anchor->SampleDisplacement(&AnchorOut, Duration);
    Blend->PoseRate.x = Rate->x;
    Blend->PoseRate.y = Rate->y;
    Blend->PoseRate.z = Rate->z;

    // Scaled1/Scaled2 are spilled to float; component 0 stays on the FPU.
    const double Inv = (double)(Blend->InvDuration);
    Blend->PoseRate.x = (float)(Inv * Blend->PoseRate.x);
    Blend->PoseRate.y = (float)(Inv * Blend->PoseRate.y);
    Blend->PoseRate.z = (float)(Inv * Blend->PoseRate.z);
    const float Scaled1 = (float)((double)(Predicted.y) * Inv);
    const float Scaled2 = (float)((double)(Predicted.z) * Inv);
    Blend->PoseCtrl.x =
        (float)((double)(Predicted.x) * Inv - Blend->PoseRate.x);
    Blend->PoseCtrl.y = (float)((double)(Scaled1) - Blend->PoseRate.y);
    Blend->PoseCtrl.z = (float)((double)(Scaled2) - Blend->PoseRate.z);
}

void MixerUnbindTyped(FAnimationObject* Animation, FTypedChannel* Typed)
{
    FMixerDescMap* Inner = Animation->MixerFindKey(TypedMixerKey(Typed));
    if (Inner == nullptr)
        return;
    FMixerDescMap::iterator Entry = Inner->find(*TypedBindDesc(Typed));
    if (Entry == Inner->end())
        return;
    FMixerList* List = &Entry->second;

    FMixerNode* Leaf = List->Head;
    while (Leaf != nullptr)
    {
        if (Leaf->Type == MixerLeafNode && static_cast<FMixerLeaf*>(Leaf)->Typed == Typed)
            break;
        Leaf = Leaf->Next;
    }

    if (Leaf != nullptr)
    {
        FMixerNode* After = Leaf->Next;
        if (After != nullptr && After->Next != nullptr && After->Next->Type == MixerBlendNode)
        {
            // The leaf is what a later blend fades FROM. Freeze its value in an overlay where
            // the leaf was and let that blend drop the overlay when done.
            FMixerOverlay* Overlay = new FMixerOverlay(&Entry->first);
            MixerInsertBefore(List, Leaf->Next, Overlay);
            if (Overlay->Next != nullptr)
                static_cast<FMixerBlend*>(Overlay->Next->Next)->Flags |= 1;
        }
        else if (After != nullptr && After->Type == MixerBlendNode)
        {
            // The leaf is what After fades TO.
            FMixerNode* AfterNext = After->Next;
            if (AfterNext != nullptr && AfterNext->Next != nullptr && AfterNext->Next->Type == MixerBlendNode)
            {
                FMixerOverlay* Overlay = new FMixerOverlay(&Entry->first);
                MixerInsertBefore(List, After->Next, Overlay);
            }
            FMixerNode* Outer = After->Prev->Prev;               // no null check
            if (Outer->Type == MixerBlendNode)
                List->UnlinkParentBlend(Outer, 1);
            else
            {
                MixerMarkDropped(Outer);
                List->Unlink(Outer);
                MixerFreeNode(Outer);
            }
            List->UnlinkAndFree(After);
        }
        List->Unlink(Leaf);
        MixerFreeNode(Leaf);
    }

    if (List->Count == 0)                                        // FUN_06618ad0
        Inner->erase(Entry);
}

// FUN_066168f0, after the channel key has been resolved.
unsigned char MixerHasLeaf(FAnimationObject* Animation, FTypedChannel* Typed)
{
    FMixerDescMap* Inner = Animation->MixerFindKey(TypedMixerKey(Typed));
    if (Inner == nullptr)
        return 0;
    FMixerDescMap::iterator Entry = Inner->find(*TypedBindDesc(Typed));
    if (Entry == Inner->end())
        return 0;
    FMixerNode* Node = Entry->second.Head;
    while (Node != nullptr)
    {
        if (Node->Type == MixerLeafNode && static_cast<FMixerLeaf*>(Node)->Typed == Typed)
            return 1;
        Node = Node->Next;
    }
    return 0;
}

// LAB_06616800, after the receiver.
void MixerWalkKey(FAnimationObject* Animation, long Key, float Dt)
{
    if (Key == -1)
        return;
    FMixerDescMap* Inner = Animation->MixerFindKey(Key);
    if (Inner == nullptr)
        return;
    FMixerDescMap::iterator Entry = Inner->begin();
    while (Entry != Inner->end())
    {
        MixerWalkPlayback(Animation, &Entry->first, &Entry->second, Dt);
        if (Entry->second.Count == 0)
            Entry = Inner->erase(Entry);                         // FUN_06618ad0
        else
            ++Entry;
    }
    if (Inner->empty())
        Animation->Mixers->erase(Key);
}

// LAB_066157f0, after the receiver.
void MixerWalkAll(FAnimationObject* Animation, float Dt)
{
    // Clear bit 2 ("ticked this frame") on every channel.
    FTypedChannelSet::iterator Channel = Animation->Channels->begin();
    while (Channel != Animation->Channels->end())
    {
        (*Channel)->PlayFlags &= ~G_ChannelHoldFlag;
        ++Channel;
    }

    // Walk every list; drop lists that emptied, then roots with no lists left.
    FMixerKeyMap::iterator Outer = Animation->Mixers->begin();
    while (Outer != Animation->Mixers->end())
    {
        FMixerDescMap& Inner = Outer->second;
        FMixerDescMap::iterator Entry = Inner.begin();
        while (Entry != Inner.end())
        {
            MixerWalkPlayback(Animation, &Entry->first, &Entry->second, Dt);
            if (Entry->second.Count == 0)
                Entry = Inner.erase(Entry);                      // FUN_06618ad0
            else
                ++Entry;
        }
        if (Inner.empty())
            Outer = Animation->Mixers->erase(Outer);             // FUN_066181e0
        else
            ++Outer;
    }

    // Tick every playing channel no mixer leaf ticked; a channel that runs out stops playing
    // and leaves its mixer.
    Channel = Animation->Channels->begin();
    while (Channel != Animation->Channels->end())
    {
        FTypedChannel* Typed = *Channel;
        if ((Typed->PlayState & G_PlayStatePlaying) != 0 && ((Typed->PlayFlags >> 2) & 1) == 0)
        {
            if (Typed->Tick(Dt) == 1)
            {
                Typed->PlayState &= ~G_PlayStatePlaying;
                MixerUnbindTyped(Animation, Typed);
            }
        }
        ++Channel;
    }
}

// FUN_06612a70.
void FAnimPlayback::WriteOverlayPos(const Vector* Pos, FMixerDescMap* Inner)
{
    unsigned long Index = 0;
    while (Index < this->Typed.size())
    {
        FTypedChannel* Typed = this->Typed[Index];
        Typed->SetOverlayPos(Pos);
        if (Typed->Kind == G_BindKindObject)
        {
            FMixerDescMap::iterator Entry = Inner->find(*TypedBindDesc(Typed));
            if (Entry != Inner->end())
            {
                FMixerNode* Node = Entry->second.Head;
                while (Node != nullptr)
                {
                    if (Node->Type == MixerOverlayNode)
                    {
                        FMixerOverlay* Overlay = static_cast<FMixerOverlay*>(Node);
                        Overlay->Values[0] = Pos->x;
                        Overlay->Values[1] = Pos->y;
                        Overlay->Values[2] = Pos->z;
                    }
                    Node = Node->Next;
                }
            }
        }
        Index += 1;
    }
}

// FUN_06612b80: the orientation goes to the channels as the matrix it came in as and to the
// overlays as a quaternion, converted once up front.
void FAnimPlayback::WriteOverlayOrient(const Matrix* Mat, FMixerDescMap* Inner)
{
    I3DMathEngine* Math = Acquire3DMathEngine();                 // DAT_0662ace0
    Quaternion Quat;
    Math->MatrixToQuaternion(Quat,
                             *Mat);
    unsigned long Index = 0;
    while (Index < this->Typed.size())
    {
        FTypedChannel* Typed = this->Typed[Index];
        Typed->SetOverlayExtra(Mat);
        if (Typed->Kind == G_BindKindObject)
        {
            FMixerDescMap::iterator Entry = Inner->find(*TypedBindDesc(Typed));
            if (Entry != Inner->end())
            {
                FMixerNode* Node = Entry->second.Head;
                while (Node != nullptr)
                {
                    if (Node->Type == MixerOverlayNode)
                    {
                        FMixerOverlay* Overlay = static_cast<FMixerOverlay*>(Node);
                        Overlay->Values[3] = Quat.w;
                        Overlay->Values[4] = Quat.x;
                        Overlay->Values[5] = Quat.y;
                        Overlay->Values[6] = Quat.z;
                    }
                    Node = Node->Next;
                }
            }
        }
        Index += 1;
    }
}

// FUN_06612c60.
void FAnimPlayback::AddOverlayPos(const Vector* Delta, FMixerDescMap* Inner)
{
    unsigned long Index = 0;
    while (Index < this->Typed.size())
    {
        FTypedChannel* Typed = this->Typed[Index];
        Typed->AddOverlayPos(Delta);
        if (Typed->Kind == G_BindKindObject)
        {
            FMixerDescMap::iterator Entry = Inner->find(*TypedBindDesc(Typed));
            if (Entry != Inner->end())
            {
                FMixerNode* Node = Entry->second.Head;
                while (Node != nullptr)
                {
                    if (Node->Type == MixerOverlayNode)
                    {
                        FMixerOverlay* Overlay = static_cast<FMixerOverlay*>(Node);
                        Overlay->Values[0] = (float)((double)(Delta->x) + Overlay->Values[0]);
                        Overlay->Values[1] = (float)((double)(Delta->y) + Overlay->Values[1]);
                        Overlay->Values[2] = (float)((double)(Delta->z) + Overlay->Values[2]);
                    }
                    Node = Node->Next;
                }
            }
        }
        Index += 1;
    }
}
