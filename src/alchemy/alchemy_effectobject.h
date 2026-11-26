// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "alchemy_types.h"

// alchemy.dll — DACOM particle FX scaffolding.
// FFxEffectObject: the IFxEffect component (instantiated node tree bound to
// an FFxEffectArchetype).
namespace AlchemyImpl
{
    // The node-class mask IFxEffect::IsFinished / DestroyNodes take (FFxEffectObject::
    // WalkNodesFlagged, DispatchFlagsToAlchemy): which of IGenericFx::GetIsEmitter /
    // GetIsField / GetIsAppearance a node must answer.
    enum EFxNodeClass : unsigned long
    {
        kFxNodeClassEmitter = 0x1,
        kFxNodeClassField = 0x2,
        kFxNodeClassAppearance = 0x4
    };

    class FFxEffectObject final : public IFxEffect
    {
    public:
        FFxEffectObject()
            : RefCount_(1), Archetype_(nullptr), TransformExtra_(nullptr), Nodes_(nullptr),
              NodeCount_(0), NodeCapacity_(0), RootNodeCount_(0)
        {
        }

        ~FFxEffectObject() noexcept(false);

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
        unsigned long DACOM_API AddRef() override { return ++RefCount_; }
        unsigned long DACOM_API Release() override;

        // FUN_0621f610: when TransformExtra_ (this+0x18) is set, only that node's
        // SetTransform (vtable+0x3C) runs and its result is returned. Otherwise every
        // node not disabled (vtable+0x1C) gets SetTransform(unsigned long, const Transform*), each failure FDUMPs
        // "Failed to set node transform. Continuing." (0x100002), and the result is
        // GR_OK. This slot never writes this+0x18. A is the node transform slot index
        // (0 composed, 1 raw -- FUN_062400f0), B the 12-float source; ScriptPsys Tick
        // passes (1, Xform).
        GENRESULT DACOM_API SetTransform(unsigned long A, const Transform* B) override;
        // LAB_0621f6a0: TransformExtra_'s node IFxNode+0x40 (A, B) when set, else the
        // first list node's; no count or null test.
        GENRESULT DACOM_API GetTransform(unsigned long A, Transform** B) override;
        GENRESULT DACOM_API SetAge(float Age) override;
        GENRESULT DACOM_API SetOuterTime(float OuterTime) override;
        GENRESULT DACOM_API ComposeTransforms(void* Arg) override;
        GENRESULT DACOM_API Update(void* Arg) override;
        GENRESULT DACOM_API Draw(void* Arg) override;
        // LAB_0621fa40: find list rec+8 == id, *out = rec[0]. Empty → -1.
        GENRESULT DACOM_API FindNodeById(void* IdRaw, void* Out) override;
        // LAB_0621fa80: empty list → AL=1 (kept). Non-empty: per-node flag
        // dispatch to +0x94/+0x98/+0x90, aborting (return 0) on flags==7 or
        // on any callback out-flag. See WalkNodesFlagged.
        GENRESULT DACOM_API IsFinished(void* Arg) override;
        // LAB_0621fb50: per-node {Id,Node} enumeration callback. See WalkNodesEnumerate.
        GENRESULT DACOM_API EnumerateNodes(void* A, void* B) override;
        // FUN_0621fba0: destroy/free effect nodes by bitmask (1=emitters, 2=fields,
        // 4=appearances, 7=all).
        GENRESULT DACOM_API DestroyNodes(void* Arg) override;
        // LAB_0621f850: per-node call to node+0x88(node, Arg). See WalkNodes.
        GENRESULT DACOM_API SetLodInput(float LodInput) override;
        // LAB_0621f880: combined bounding-sphere merge over node+0x8C
        // spheres (Ritter two-sphere merge). See MergeNodeBoundsSphere.
        GENRESULT DACOM_API GetBoundingSphere(void* Arg) override;
        GENRESULT DACOM_API GetBoundComponent(void** Out) override;
        // FUN_06220d70 thiscall (this, flag): Release +0x1c; flag&1 → delete.
        // Port: unbind only. Lifetime stays on Release (factory fail path).
        GENRESULT DACOM_API DestructorSlot() override;

        GENRESULT DACOM_API Initialize(AGGDESC* Desc) override;

        void BindArchetype(IFxEffectArchetype* Archetype);
        void Unbind();

    private:
        // LAB_0621f7f0: empty list → 0. Else CALL [node+Off](node, arg) stdcall RET 8.
        // Slot 5 Off=0x64 = SetNodeAge (node+0x84 store, e.g. cone LAB_0621c300).
        // Slot 6 (LAB_0621f820) Off=0x5C = SetNodeOuterTime (node+0x8c).
        // Slot 14 (LAB_0621f850) Off=0x88 = SetNodeLodInput (node+0x90).
        // LAB_0621f820, FUN_0621f6d0, FUN_0621f730 and FUN_0621f790 are each a
        // single-offset walk (0x5C / 0x3C / 0x40 / 0x44); none also calls 0x64.

        // FUN_0621f6d0 / 730 / 790: if node+0x1C AL==0, Alchemy+Off(dt, node).
        GENRESULT WalkNodes(GENRESULT (DACOM_API IGenericFx::*Method)(float), float Value);
        GENRESULT WalkNodesAlchemy(GENRESULT (DACOM_API IAlchemy::*Method)(void*, void*),
                                   void* Arg);

        // LAB_0621fa80: empty list → 1. Else per node (skipping a self match
        // against *TransformExtra_, then any node+0x1C-disabled node): flags==7
        // aborts (return 0) immediately; else each set bit in {1,2,4} calls
        // node+0x94/+0x98/+0x90(node, &outFlag) and aborts (return 0) if the
        // callee sets outFlag. Falling through the whole list returns 1.
        GENRESULT WalkNodesFlagged(unsigned long Flags);

        // FUN_0621fba0, flags != 7: same +0x94/+0x98/+0x90 shape as
        // WalkNodesFlagged but never aborts; a set out-flag additionally
        // submits the node to IAlchemy::SubmitNode (+0x5C, DAT_06257a3c).
        // flags==7 submits the enabled root prefix, then destroys all node
        // records and sets vector end=begin without freeing its capacity.
        GENRESULT DispatchFlagsToAlchemy(unsigned long Flags);

        // LAB_0621fb50: for each node build a {Id, Node} pair and invoke the
        // caller-supplied CDECL Callback(&pair, Context) until it returns
        // nonzero or the list is exhausted. Always returns GR_OK.
        GENRESULT WalkNodesEnumerate(void* Callback, void* Context);

        // LAB_0621f880: merge the captured node range's float[4] spheres.
        // Distance uses 0x5f3759df and one Newton refinement. The float
        // stores, strict containment tests and unordered epsilon branch
        // are retained; no contributing sphere returns GR_GENERIC.
        GENRESULT MergeNodeBoundsSphere(void* Arg);

        // FUN_0621ee50: recursive max-duration walk over a sibling chain.
        // Per sibling: read its own duration via +0x68 (out-param accessor;
        // this call shape differs from IGenericFx::EmitOne's declared 3-arg
        // form at the same offset, so it is read through a local vtable
        // cast rather than the interface's virtual), recurse into its child
        // (+0x2C) for the subtree below it, and continue to the next
        // sibling (+0x34). Returns the max duration seen anywhere in the
        // chain/subtree.
        float ComputeMaxSubtreeDuration(IDAComponent* FirstSibling);

        // FUN_0621fa80 / FUN_0621fba0 per-node callback: calls
        // node+Off(node, &OutFlag) (OutFlag zeroed first) and returns
        // whatever the callee set. The call's own GENRESULT return value is
        // ignored by retail in both slots.
        static unsigned char CallNodeFlagSlot(
            IDAComponent* Node, GENRESULT (DACOM_API IGenericFx::*Query)(unsigned char*));

        // LAB_0621fb50 enumeration record built per node for EnumerateNodes' callback.
        struct FNodeIdPair
        {
            unsigned long Id;
            IDAComponent* Node;
        };

        void InsertNodeRecords(unsigned long InsertIndex, unsigned long InsertCount,
                               const FFxEffectNodeRec& Record);
        FFxEffectNodeRec* InsertSingleNodeRecord(unsigned long InsertIndex,
                                                const FFxEffectNodeRec& Record);
        FFxEffectNodeRec* NodeRecordsEnd() const;
        GENRESULT InstantiateNodesFromArchetype(IFxEffectArchetype* InitialArchetype,
                                               const FFxEffectCreateDesc* Desc);
        IDAComponent* FindNodeById(unsigned long Id) const;

        // 0621f2ce: root parent comes from the descriptor; the captured end is
        // reused after the association phase for the duration walk.
        FFxEffectNodeRec* AttachAlchemyTree(const FFxEffectCreateDesc* Desc);

        // FUN_0621eed0 after GetRec8Count (+0x30) / GetRec8 (+0x38):
        // rec8.A / rec8.B are node ids. QI A as IFxEmitter -> B as
        // IFxAppearance, then [emitter+0x9C](appearance), release both
        // temp interfaces. Else QI A as IFxAppearance -> B as IFxField,
        // then [appearance+0xAC](field), release both temp interfaces.
        // Else FDUMP line 0x17b. The pairs are not kept: the destructor
        // (FUN_0621ed80 / FUN_06220d90) releases only Archetype_ (+0x1c) and the node
        // list (+0xc/+0x10).
        void BindRec8Associates(IFxEffectArchetype* InitialArchetype);

        void ClearNodes();
        void DestroyNodeRecords();

        unsigned long RefCount_;
        IFxEffectArchetype* Archetype_;
        FFxEffectNodeRec* TransformExtra_;
        FFxEffectNodeRec* Nodes_;
        unsigned long NodeCount_;
        unsigned long NodeCapacity_;
        long RootNodeCount_;
    };

} // namespace AlchemyImpl
