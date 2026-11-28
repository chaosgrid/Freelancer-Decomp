// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Space/Effects/effect_type_list.h"
#include "Space/Effects/effect_base.h"

FEffectTypeBucket G_EffectTypeBuckets[kEffectTypeBucketCount] = {};

namespace
{
    void** NodePrev(void* Node)
    {
        return &static_cast<FEffectTypeLink*>(Node)->Prev;
    }

    void** NodeNext(void* Node)
    {
        return &static_cast<FEffectTypeLink*>(Node)->Next;
    }

    // The owning effect's LinkSelf.
    void* NodeOwner(void* Node)
    {
        return static_cast<FEffectTypeLink*>(Node)->Owner;
    }

    void* EffectLinkNode(FEffectBase* Effect)
    {
        return &Effect->LinkPrev;
    }
}

void FEffectTypeBucket::PushFront(void* Node)
{
    // FUN_004fc920 (on the bucket): only the node is tested.
    if (Node == nullptr)
        return;

    *NodeNext(Node) = Head;
    *NodePrev(Node) = nullptr;
    if (Head != nullptr)
    {
        *NodePrev(Head) = Node;
        Head = Node;
        ++Count;
        return;
    }
    Tail = Node;
    Head = Node;
    ++Count;
}

void EffectTypeListLink(void* EffectPtr)
{
    // FUN_004f7af0: no null test, no range test on the type id.
    FEffectBase* const Effect = static_cast<FEffectBase*>(EffectPtr);
    if (Effect->LinkSelf != nullptr)
        return;
    if ((Effect->Flags08 & kEffectFlagDeactivated) != 0)
        return;

    unsigned const TypeId = Effect->EffectTypeId;

    void* const Node = EffectLinkNode(Effect);
    Effect->LinkSelf = Effect;
    G_EffectTypeBuckets[TypeId].PushFront(Node);
    Effect->RefCount += 1;
}

void EffectTypeListUnlinkAndRelease(void* EffectPtr)
{
    // Inlined in retail's bucket walk, FUN_004f7140 and FUN_004f73c0: only a linked effect is
    // touched; +0x68 is cleared FIRST, the +0x60 node is unlinked from bucket[+0x5c], the bucket
    // count drops, then the link's reference is released (signed; below 1 deletes).
    FEffectBase* const Effect = static_cast<FEffectBase*>(EffectPtr);
    if (Effect->LinkSelf == nullptr)
        return;

    FEffectTypeBucket* const Bucket = &G_EffectTypeBuckets[Effect->EffectTypeId];
    void* const Node = EffectLinkNode(Effect);
    Effect->LinkSelf = nullptr;

    void* const Prev = *NodePrev(Node);
    void* const Next = *NodeNext(Node);
    if (Prev == nullptr)
    {
        Bucket->Head = Next;
        if (Next != nullptr)
            *NodePrev(Next) = nullptr;
        else
            Bucket->Tail = nullptr;
    }
    else if (Next == nullptr)
    {
        Bucket->Tail = Prev;
        *NodeNext(Prev) = nullptr;
    }
    else
    {
        *NodeNext(Prev) = Next;
        *NodePrev(Next) = Prev;
    }
    *NodePrev(Node) = nullptr;
    *NodeNext(Node) = nullptr;
    --Bucket->Count;

    int const Remaining = static_cast<int>(Effect->RefCount) - 1;
    Effect->RefCount = static_cast<unsigned>(Remaining);
    if (Remaining <= 0)
        delete Effect;
}

void EffectTypeListKillAll()
{
    // FUN_004f73c0 (and the first half of FUN_004f7140): for every bucket, as long as it has a
    // head, take the head's effect (node +8, i.e. Effect+0x68), run its slot +4 (kill -- which
    // links it if needed), then unlink it and drop the link's reference. The head is re-read
    // each time, so the loop ends when the bucket is empty.
    for (int Index = 0; Index < kEffectTypeBucketCount; ++Index)
    {
        FEffectTypeBucket* const Bucket = &G_EffectTypeBuckets[Index];
        while (Bucket->Head != nullptr)
        {
            FEffectBase* const Effect = static_cast<FEffectBase*>(NodeOwner(Bucket->Head));
            Effect->Kill();
            EffectTypeListUnlinkAndRelease(Effect);
        }
    }
}
