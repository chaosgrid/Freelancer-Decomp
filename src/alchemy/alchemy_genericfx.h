// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "alchemy_types.h"
#include "alchemy_common.h"

#include <cstring>

// alchemy.dll — DACOM particle FX scaffolding.
//
// FGenericFxObjectBase: shared COM/tree machinery and the property-set protocol
// (each class's retail property set, see FFxNodePropertySet) for every
// FxNode/emitter/field/appearance concrete type. Each of the 19 retail types is
// its own C++ leaf class with only the state it needs.
//
// Concrete leaves (declared in their own family headers):
//   FxNode                                            — this file (trivial)
//   FxSphereEmitter/FxConeEmitter/FxCubeEmitter        — alchemy_emitters.h
//   FxAirField/FxGravityField/FxCollideField/
//     FxRadialField/FxTurbulenceField/
//     FLDustField/FLBeamField                          — alchemy_fields.h
//   FxBasicAppearance/FxParticleAppearance/
//     FxRectAppearance/FxPerpAppearance/
//     FxMeshAppearance/FxOrientedAppearance/
//     FLDustAppearance/FLBeamAppearance                — alchemy_appearances.h
//
// Construction: alchemy_factory.cpp's CreateComponent constructs the leaf class
// by retail component name.
//
// Virtual hooks a leaf overrides (no-op by default): BeginField/ApplyFieldToVel
// (IGenericFx; the 7 field leaves, though FLBeamField keeps the no-op like retail
// LAB_0620ed20/30) and OnAfterIntegrate (protected; FxMeshAppearance only, for the
// Extra2/PoseMeshExtra2 case). The emitter machinery (FUN_06245bf0 and the shape
// samplers) lives on FFxEmitterBase in alchemy_emitters.h.
//
// BuildDefaultAppearanceRamps() is a non-virtual helper the perp and dust ctors call
// after base construction; basic, rect, oriented and beam have their own builders in
// alchemy_appearances.cpp.
namespace AlchemyImpl
{
    // The lookup key ITextureLibrary +0x30 (Slot12) takes: name + CRC, as
    // the other modules' {Name, Crc} queries. The appearance texture path
    // (FUN_06244b80 / FUN_06244df0) always passes the name "???"
    // (DAT_06255804) and the descriptor's texture CRC.
    struct FFxTextureKey
    {
        const char* Name;
        unsigned long Crc;
    };

    // Retail ImpFxTexInfo (FDUMP path "...\Src\ALIL\ImpFxTexInfo.cpp"): the
    // texture state every billboard appearance embeds (node+0xb4; the beam
    // node's at node+0xb8), 0x24 bytes, built by FUN_06244b20. The texture
    // CRC comes from the appearance descriptor (+0x64), which the
    // descriptor's name property 0x1BA23359 sets to GetCRC32(name)
    // (e.g. LAB_062328c0 on the perp descriptor).
    struct FFxTextureWrapper
    {
        unsigned long TexCrc;     // +0x00
        int Handle;               // +0x04 ITextureLibrary +0x34 id, -1 = none
        // +0x08..+0x18: ITextureLibrary +0x70 GetFrame output -- the frame's texture
        // dword, then its UV rect. The draw fills read [1..4] as floats and copy [0]
        // out bitwise.
        float Frame[5];
        int FrameCount;           // +0x1C ITextureLibrary +0x68
        // +0x20: zeroed by FUN_06244b20; no other access in alchemy.dll (every
        // `+ 0xd4]` operand on an appearance is a vtable call). TODO: meaning unknown.
        unsigned long Field_0x20;

        FFxTextureWrapper();                        // FUN_06244b20
        void ReleaseHandle();                       // FUN_06244b50
        void SetTexture(const FFxTextureKey* Key);  // FUN_06244b80
        void Resolve();                             // FUN_06244df0
        void Advance(const float& Dt);              // FUN_06244ef0
        void LookupFrame(int FrameIndex, float* Out); // FUN_06244f30
    };

    // One case of a billboard descriptor's bool setter (descriptor vt+0x20): the hash and
    // the Flags (descriptor +0x60) bit it sets or clears. Mask 0 = accepted and ignored
    // (the beam's 0x03503B61 / 0xE2F60EEB / 0x0ABE0402 jump straight to `return 0`).
    struct FDescriptorFlagBit
    {
        unsigned long Hash;
        unsigned long Mask;
    };

    class FGenericFxObjectBase : public IGenericFx
    {
        // IntegratePool and ApplyFieldsToPool (alchemy_common.cpp) walk the instance
        // list and IntegratePool drops LiveInstanceCount_ on expiry.
        friend void IntegratePool(FGenericFxObjectBase* Owner, FAppearancePool* Pool, const float& Dt);
        friend void ApplyFieldsToPool(FGenericFxObjectBase* Owner, bool bLiveItemsOnly);

    public:
        FGenericFxObjectBase(const char* PrimaryIid, const char* FactoryName);
        virtual ~FGenericFxObjectBase() noexcept(false);

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
        unsigned long DACOM_API AddRef() override { return ++RefCount_; }
        unsigned long DACOM_API Release() override;

        // Outer +0x0C, CloneNode: each leaf class overrides it with its retail clone.
        // FUN_062401a0, the shared node-base compose (node vtable slot 2). The mesh
        // appearance overrides it with FUN_06225240 (compose, then pose its meshes).
        GENRESULT DACOM_API ComposeNodeTransform(void* Unused, const float* Parent12) override;
        // Per-node update. FUN_06240180 (node vtable slot 3): TickLifetime(Dt) through the
        // object's own vtable. FLBeamAppearance and the three emitters override it
        // (FUN_0620de30; LAB_0621b9d0 / LAB_0621d360 / LAB_0623bcf0).
        GENRESULT DACOM_API UpdateNode(float Dt) override;
        // Outer +0x18, the per-node draw. FUN_062403f0 (node vtable slot 4 default)
        // returns 0. The appearances override it with their draw.
        GENRESULT DACOM_API DrawOrEnqueue() override { return GR_OK; }
        // Outer +0x1C (FxNode LAB_062026a0 and every sibling): `[node+4] >> 31`.
        GENRESULT DACOM_API IsNodeDead() override
        {
            return static_cast<GENRESULT>(NodeFlags_ >> 31);
        }
        // Outer +0x20 / +0x24 (FxNode LAB_062026d0 / LAB_062026f0): the flags dword.
        GENRESULT DACOM_API GetNodeFlags(unsigned long* Out) override
        {
            *Out = NodeFlags_;
            return GR_OK;
        }
        GENRESULT DACOM_API SetNodeFlags(unsigned long Flags) override
        {
            NodeFlags_ = Flags;
            return GR_OK;
        }
        IDAComponent* DACOM_API GetAttachParent() override { return AttachParent_; }
        IDAComponent* DACOM_API GetFirstChild() override { return FirstChild_; }
        IDAComponent* DACOM_API GetPrevSibling() override { return PrevSibling_; }
        IDAComponent* DACOM_API GetNextSibling() override { return NextSibling_; }
        // Outer +0x38 returns one constant per family: FxNode 0x190 (LAB_06202740); every
        // appearance 0x64 (beam 0620dfa0, dust 06212300, basic 06218e80, 06226f60, 0622ea90,
        // 06231ea0, 062357c0, 0623ad10); every field 0xc8 (0620eaa0, 06213880, air 06214900,
        // 0621a890, 06223ee0, 06236b10, 0623e0f0); every emitter 0x12c (cone 0621c220, cube
        // 0621da00, sphere 0623c420). LinkNodeTree (FUN_06201e00) orders siblings by it. The
        // port has no family base classes, so the family tag bytes (+0x94..+0x96) select it.
        int DACOM_API GetSortKey() override
        {
            if (bIsAppearanceNode_ != 0)
                return 0x64;
            if (bIsFieldNode_ != 0)
                return 0xc8;
            if (bIsEmitterNode_ != 0)
                return 0x12c;
            return 0x190;
        }
        // FUN_062400f0 / FUN_062400b0 (node vtable slots 6 / 5).
        GENRESULT DACOM_API SetNodeTransform(unsigned long Index, const float* Source12) override;
        GENRESULT DACOM_API GetNodeTransform(unsigned long Index, float** OutSlot) override;
        // Age_ += Dt; latches the expired bit once Age_ > Duration_ (strictly).
        GENRESULT DACOM_API TickLifetime(float Dt) override;
        GENRESULT DACOM_API SetParent(IDAComponent* Parent) override
        {
            AttachParent_ = Parent;
            return GR_OK;
        }
        GENRESULT DACOM_API SetFirstChild(IDAComponent* Child) override
        {
            FirstChild_ = Child;
            return GR_OK;
        }
        GENRESULT DACOM_API SetPrevSibling(IDAComponent* Prev) override
        {
            PrevSibling_ = Prev;
            return GR_OK;
        }
        GENRESULT DACOM_API SetNextSibling(IDAComponent* Next) override
        {
            NextSibling_ = Next;
            return GR_OK;
        }
        // Outer +0x58..+0x6C (FxNode LAB_062027c0..LAB_06202860): get/set of node+0x8c,
        // +0x84 and +0x88 -- see the block comment in IGenericFx (alchemy_types.h).
        GENRESULT DACOM_API GetNodeOuterTime(float* Out) override
        {
            *Out = AppearanceOuterTime_;
            return GR_OK;
        }
        GENRESULT DACOM_API SetNodeOuterTime(float OuterTime) override
        {
            AppearanceOuterTime_ = OuterTime;
            return GR_OK;
        }
        GENRESULT DACOM_API GetNodeAge(float* Out) override
        {
            *Out = Age_;
            return GR_OK;
        }
        GENRESULT DACOM_API SetNodeAge(float Age) override
        {
            Age_ = Age;
            return GR_OK;
        }

        // FUN_06215f40 / FUN_0620e430 / FUN_0620be90. Not at +0x68 (that is
        // GetSubtreeDuration); declared at the end of IGenericFx.
        GENRESULT DACOM_API EmitOne(const float* Six, unsigned long Extra,
                                    float Duration) override;
        // Outer +0x68 (FxNode LAB_06202840): node+0x88.
        GENRESULT DACOM_API GetSubtreeDuration(float* OutSeconds) override
        {
            *OutSeconds = Duration_;
            return GR_OK;
        }
        // Outer +0x6C (FxNode LAB_06202860).
        GENRESULT DACOM_API SetNodeDuration(float Value) override
        {
            Duration_ = Value;
            return GR_OK;
        }
        // Outer +0x70 (beam LAB_0620def0, field LAB_0620e9f0): the factory name.
        const char* DACOM_API GetFxFactoryName() override { return PropertySet_->ClassName.Buffer; }
        // Outer +0x74 / +0x78 / +0x7C (FxNode LAB_062028b0 / LAB_062028d0 / FUN_062028f0):
        // tail jumps into the property set's outer +0x14 / +0x10 / +0xC (Node_Name hash,
        // buffer, assign). The name belongs to the shared set, so a clone renames its
        // archetype too.
        GENRESULT DACOM_API GetInstanceNameHash(unsigned long* Out) override
        {
            return PropertySet_->GetInstanceNameHash(Out);
        }
        GENRESULT DACOM_API GetInstanceName(const char** Out) override
        {
            return PropertySet_->GetInstanceName(Out);
        }
        GENRESULT DACOM_API SetInstanceName(const char* Name) override
        {
            return PropertySet_->SetInstanceName(Name);
        }
        // Outer +0x80 (FxNode 06202880 and its fourteen twins): the set's outer, AddRef'd.
        GENRESULT DACOM_API GetPropertySet(IFxNodePropertySet** Out) override
        {
            *Out = PropertySet_;
            PropertySet_->AddRef();
            return GR_OK;
        }
        // Outer +0x84 / +0x88 (FxNode LAB_06202910 / LAB_06202930): node+0x90.
        GENRESULT DACOM_API GetNodeLodInput(float* Out) override
        {
            *Out = LodInput_;
            return GR_OK;
        }
        GENRESULT DACOM_API SetNodeLodInput(float LodInput) override
        {
            LodInput_ = LodInput;
            return GR_OK;
        }
        // Outer +0x8C for every node that has no sphere (FxNode FUN_06202950, cone emitter
        // LAB_0621c430, field LAB_0620ecb0): 0xfffffff9, nothing written. Every appearance
        // overrides it.
        GENRESULT DACOM_API GetBoundingSphere(float* /*OutSphere*/) override
        {
            return static_cast<GENRESULT>(0xfffffff9);
        }
        // Outer +0x90 / +0x94 / +0x98 (FxNode LAB_06202960 / 980 / 9a0): the family tag
        // bytes node+0x94 (appearance) / +0x95 (emitter) / +0x96 (field).
        GENRESULT DACOM_API GetIsAppearance(unsigned char* Out) override
        {
            *Out = bIsAppearanceNode_;
            return GR_OK;
        }
        GENRESULT DACOM_API GetIsEmitter(unsigned char* Out) override
        {
            *Out = bIsEmitterNode_;
            return GR_OK;
        }
        GENRESULT DACOM_API GetIsField(unsigned char* Out) override
        {
            *Out = bIsFieldNode_;
            return GR_OK;
        }
        GENRESULT DACOM_API EndField() override { return GR_OK; }
        GENRESULT DACOM_API FillBatchedGeometry() override { return GR_OK; }
        // Batch begin / end-and-draw (IGenericFx, retail appearance +0x9C / +0xA0). The four
        // batched appearance kinds override them. Every other appearance reaches the
        // appearance node defaults (node vtable 0x0624fbd0 slots +0x4c / +0x50): FUN_06244690
        // and FUN_062446a0 return 0, no side effect. (FillBatchedGeometry's default above is
        // the third one, FUN_062446b0.)
        GENRESULT DACOM_API BeginBatchedDraw(unsigned long /*Verts*/,
                                             unsigned long /*Indices*/) override
        {
            return GR_OK;
        }
        GENRESULT DACOM_API EndBatchedDraw() override { return GR_OK; }
        // Appearance +0xA8: every retail body returns 0xfffffff9.
        GENRESULT DACOM_API Pad42() override { return static_cast<GENRESULT>(0xfffffff9); }
        // FUN_06244430, inner +0x5c / appearance +0xB0 (remove a field): the unchecked
        // iterator starts at the by-value argument itself. Retail's malformed algorithm is
        // kept, not a search by field or index.
        GENRESULT DACOM_API RemoveField(IDAComponent* Field) override;
        // Appearance +0xB4 (FUN_06244580 via a this+0xc thunk, basic LAB_062195a0): begins
        // every associated field.
        GENRESULT DACOM_API BeginFields() override;
        // Appearance +0xB8 (FUN_062445c0 via a this+0xc thunk, basic LAB_062195b0): will a
        // particle spawned with (Six, Age, Duration) survive this frame's fields?
        GENRESULT DACOM_API TestParticleSpawn(const float* Six, float Age, float Duration) override;
        // Appearance +0xBC: the spawn. Every appearance has its own body (basic
        // FUN_06215f40, rect FUN_0623b410, perp FUN_06235db0, oriented FUN_0622bd80,
        // beam FUN_0620e430, dust FUN_06212700, particle FUN_0622f6a0, mesh
        // FUN_06224c20) and overrides it; this default spawns nothing.
        GENRESULT DACOM_API SpawnParticle(const float* /*Six*/, float /*Age*/,
                                          float /*Duration*/) override
        {
            return GR_OK;
        }
        // Appearance +0xC0 (FUN_06244660 via a this+0xc thunk, basic LAB_062195d0): ends
        // every associated field (field +0xa0).
        GENRESULT DACOM_API EndFields() override;
        // Appearance +0xC4 / +0xC8 (basic LAB_062195e0 / LAB_06219600): INC / DEC of
        // node+0xb0. Only appearance vtables have these slots in retail.
        GENRESULT DACOM_API AddEmitterRef() override
        {
            ++EmitterRefCount_;
            return GR_OK;
        }
        GENRESULT DACOM_API ReleaseEmitterRef() override
        {
            --EmitterRefCount_;
            return GR_OK;
        }

        GENRESULT DACOM_API GetLiveInstanceCount(unsigned long* Out) override;
        GENRESULT DACOM_API GetQueuedVertCount(unsigned long* Out) override;
        GENRESULT DACOM_API GetQueuedIndexCount(unsigned long* Out) override;

        GENRESULT DACOM_API SetLiveInstanceCount(unsigned long Count) override
        {
            LiveInstanceCount_ = Count;
            return GR_OK;
        }

        // The node's property set (retail +0x7c outer / +0x80 set). Never null once the
        // node went through InitializeFromFactory or InitNodeFromArchetype. The property
        // protocol is the set's (IFxNodePropertySet, alchemy_types.h); each node class's
        // CreatePropertySet builds its own set class.
        FFxNodePropertySet* NodePropertySet() const { return PropertySet_; }

        // FUN_0623fde0 (node vtable slot 1, called by every node factory after
        // construction): create this class's property set (slot +0x48, CreatePropertySet),
        // give it the class name (the creation descriptor's +4 name, hashed), then
        // Duration = set LifeSpan. A null descriptor FDUMPs "you should set DACOMDESC for
        // create FxNode" and skips the name.
        GENRESULT InitializeFromFactory(const FAggDesc8* Desc);

        // Retail IFxEmitter+0x9C (this, appearance). One slot; last wins. No emit.
        GENRESULT DACOM_API AssociateAppearance(IDAComponent* Appearance) override;

        // Retail IFxAppearance+0xAC = FUN_062443b0: AddRef, insert at end of the
        // field vector (node+0x98 object, begin +0x9c / end +0xa0), Release. A null
        // field is stored too. Walk is FUN_06244580, not last-wins.
        GENRESULT DACOM_API AssociateField(IDAComponent* Field) override;

        IDAComponent* DACOM_API GetAssociatedField() override;
        GENRESULT DACOM_API GetAssociatedFieldCount(unsigned long* Out) override
        {
            *Out = FieldCount_;
            return GR_OK;
        }
        GENRESULT DACOM_API GetAssociatedFieldAt(unsigned long Index, IDAComponent** Out) override;
        // Internal field-update loops borrow a vector entry directly in retail.
        unsigned long BorrowAssociatedFieldCount() const { return FieldCount_; }
        IDAComponent* BorrowAssociatedFieldAt(unsigned long Index);
        // Retail's erase of an expired field (FUN_06218780 else-branch, FUN_0620d690 /
        // FUN_0620bd70 shift-erase), called by ApplyFieldsToPool when a field's +0x1C
        // reports it dead. Port-only helper, not part of IGenericFx.
        void RemoveAssociatedFieldAt(unsigned long Index);
        IDAComponent** EraseAssociatedField(IDAComponent** Position);
        // FUN_06244120 (the appearance archetype copy base, called by all eight
        // appearance clone ctors) after FUN_0623fc30: vector<ComPtr> operator= of the
        // field list (node+0x98).
        void CopyAssociatedFieldsFrom(FGenericFxObjectBase* Source);
        const FAppearanceRampDescriptor* DACOM_API GetAppearanceRampDescriptor() override
        {
            return Descriptor_;
        }

        // node+0x8c, the same storage as Get/SetNodeOuterTime.
        float DACOM_API GetAppearanceOuterTime() override { return AppearanceOuterTime_; }
        GENRESULT DACOM_API SetAppearanceOuterTime(float Time) override
        {
            AppearanceOuterTime_ = Time;
            return GR_OK;
        }

        // The billboard blend pair (descriptor +0x68/+0x6c) is the constant {5 SRCALPHA,
        // 2 ONE} every retail descriptor builder writes; no property overrides it.

        // The field leaves (alchemy_fields.h) override these; FLBeamField keeps the
        // no-op, as retail's does.
        void DACOM_API BeginField(IDAComponent* /*Appearance*/, float /*Dt*/) override {}
        void DACOM_API ApplyFieldToVel(float* /*PosVel6*/, float* /*Age*/, float /*Dt*/) override {}

        // Read-only head of this instance's particle list (node+0xd8).
        const FPooledParticle* GetFirstInstanceParticle() const { return InstanceListHead_; }

        // Retail outer +4 / +8: the batch-list links FUN_06201c70 writes (prev, next) and
        // FUN_06201cc0 walks through [outer+8]. See FBatchList (alchemy_types.h).
        IGenericFx* BatchPrev_ = nullptr;
        IGenericFx* BatchNext_ = nullptr;

        // FUN_0623fc30 -- the node-base half of every class's archetype copy constructor
        // (FUN_06244120 / FUN_06244f90 / FUN_06245b30, 18 callers). Duration_ =
        // [[Archetype+0x80]+0x48] (Node_LifeSpan); the own +0x7c is Released, the
        // archetype's copied and AddRef'd, +0x80 copied: the clone shares the archetype's
        // property set. The billboard set cache (node+0xf0, dust +0xe8) follows it, as
        // every appearance clone ctor copies it (basic FUN_06215d80).
        void InitNodeFromArchetype(FGenericFxObjectBase* Archetype);

        // FUN_06244120, the appearance copy base all eight appearance clones call:
        // FUN_0623fc30, the appearance set cache +0xa8 copied (PropertySet_ here), then the
        // field list (+0x98) assigned from the archetype's (CopyAssociatedFieldsFrom).
        void InitAppearanceFromArchetype(FGenericFxObjectBase* Archetype);

        // The composed world transform, node+0x4c (what GetNodeTransform(0) hands out).
        // I3DMathEngine FTransform layout: 3x3 row-major rotation, then translation.
        const float* GetNodeWorldXform() const { return NodeWorldXform_; }

        // FUN_0623ffb0 -- node vtable slot 7 (+0x1c), shared by all 21 node vtables:
        // Out12 = GeneralInverse(Other's composed transform) * this node's composed
        // transform, i.e. this node expressed in Other's space. Other is an outer
        // object (its +0x40 GetNodeTransform(0) is called). Not an IGenericFx slot here:
        // the outer vtables do forward +0x44 to it (FxNode LAB_06202770), but the port's
        // +0x44 is TickLifetime and every known caller reaches this through the node
        // sub-object (emitter body FUN_06245bf0, gravity field Begin, ...).
        GENRESULT ComputeRelativeTransform(IDAComponent* Other, float* Out12);

        // [[this+0x80]+0x24]: the property set's Node_Transform IAnimatedTransform (the
        // set constructor's default FxAnimatedTransform, filled in place by FUN_062280a0
        // when the .ale has the property). No reference is added.
        virtual IAnimatedTransform* FindNodeAnimatedTransform() const { return PropertySet_->Transform; }

    protected:
        // Node slot +0x48: create this class's property set. The base body is FLBeamField's
        // FUN_06244fb0 (a base set under the field vtables 0x0624fda8 / 0x0624fcf0); every
        // other class, FxNode included (FUN_0623ff00), overrides it with its own creator.
        virtual void CreatePropertySet();
        // Store a freshly created set the way every creator does (Release the old +0x7c,
        // store and AddRef the new, Release the creation reference) and cache it (+0x80
        // and the class caches).
        void AdoptPropertySet(FFxNodePropertySet* Set);
        // Port-only: the class cache (retail +0x9c / +0xa8 / +0xf0) of a leaf that keeps a
        // typed pointer next to PropertySet_; called whenever PropertySet_ changes.
        virtual void OnPropertySetChanged();

        // The particle list at node+0xd8 = {head +0xd8, tail +0xdc, pool +0xe0,
        // count +0xe4} is InstanceListHead_ / InstanceListTail_ / the leaf's pool /
        // LiveInstanceCount_. These two helpers are the list operations the four
        // billboard appearances (basic, rect, perp, oriented) share; retail compiles
        // a copy per class.
        //
        // SpawnListParticle: basic FUN_06215f40, rect FUN_0623b410 + FUN_0623a2e0,
        // perp FUN_06235db0 + FUN_06234ab0, oriented FUN_0622bd80. Returns the new
        // particle (the list tail) so perp can seed its axis.
        FPooledParticle* SpawnListParticle(FAppearancePool* Pool, const float* Six, const float& Age,
                                           const float& Duration);
        FPooledParticle* EraseListParticle(FAppearancePool* Pool, FPooledParticle* Particle);
        void EvictOldestListParticle(FAppearancePool* Pool);

        void ReleaseAssociatedFields();
        void DestroyListAppearance(FAppearancePool* Pool);
        void ReleaseRectFields();
        void DetachRectList();
        void ReleaseRectPropertySet();
        unsigned long ResolveLiveInstanceCount() const;

        unsigned long RefCount_;

        // Node-base +0x4 (FUN_0623fb80 zeroes it), the node flags:
        //   0x1 -- lifetime over (TickLifetime, LAB_06240150: Age_ > Duration_),
        //   0x2 -- skip the Node_Transform animation (read by the compose FUN_062401a0),
        //   0x4 -- the raw transform was set from outside (SetNodeTransform index 1),
        //   0x8 -- the composed transform was set from outside (SetNodeTransform index
        //          0); the next compose clears it and keeps the value.
        // The sign bit means "dead" (IsNodeDead, outer +0x1C); the emitter body
        // FUN_06245bf0 tests it on itself.
        unsigned long NodeFlags_;

        const char* PrimaryIid_;
        const char* FactoryName_;
        // Retail node +0x7c (the set's outer, AddRef'd) / +0x80 (the set): one pointer
        // holding one reference. Null until InitializeFromFactory or InitNodeFromArchetype.
        FFxNodePropertySet* PropertySet_;
        IDAComponent* AssociatedAppearance_;
        IDAComponent** AssociatedFields_;
        unsigned long FieldCount_;
        unsigned long FieldCapacity_;
        IDAComponent* AttachParent_;
        IDAComponent* FirstChild_;
        IDAComponent* PrevSibling_;
        IDAComponent* NextSibling_;
        unsigned long LiveInstanceCount_;

        // Node-base +0x1c: the raw (local) transform, identity from FUN_0623fb80. The
        // compose overwrites it with the evaluated Node_Transform while flag 0x4 is clear;
        // SetNodeTransform(1) sets it and flag 0x4.
        float NodeLocalXform_[12];
        // Node-base +0x4c: the composed (world) transform, identity from FUN_0623fb80.
        // Written by the compose FUN_062401a0 and by SetNodeTransform(0); read through
        // GetNodeTransform(0) and by every emitter / appearance / bounds consumer.
        float NodeWorldXform_[12];

        // Node-base +0x90, 1.0f from FUN_0623fb80; outer +0x84/+0x88.
        float LodInput_;
        // Node-base +0x94 / +0x95 / +0x96, zero from FUN_0623fb80; the family
        // constructors set one of them (see IGenericFx::GetIsAppearance). The appearance
        // and field leaves set theirs in this class's constructor from PrimaryIid_
        // (retail sets them in the shared appearance init FUN_06244360 and in each field
        // constructor), FFxEmitterBase sets +0x95.
        unsigned char bIsAppearanceNode_;
        unsigned char bIsEmitterNode_;
        unsigned char bIsFieldNode_;
        // Appearance node+0xb0 (outer +0xbc): how many emitters feed this appearance
        // (AddEmitterRef / ReleaseEmitterRef). Zero-initialised like the rest of the
        // appearance block.
        long EmitterRefCount_;

        // Node+0xd8 / +0xdc: this instance's own live-particle list, threaded through
        // FPooledParticle::Next/Prev; separate from the pool's own used/free links.
        // Retail: link FUN_06215f40, age FUN_062188c0, unlink+free FUN_06217ae0, field
        // walk FUN_06218780, destructor erase FUN_06219070.
        FPooledParticle* InstanceListHead_;
        FPooledParticle* InstanceListTail_;

        // Node +0x84 / +0x88. FUN_0623fb80 seeds Age_ = 0 and Duration_ = 100.0f;
        // InitializeFromFactory / InitNodeFromArchetype then copy Node_LifeSpan (default
        // 3.0f from FUN_0623f7b0) into Duration_.
        float Age_;
        float Duration_;

        // Node +0x8c: the ramp Eval "OuterTime" every property evaluation passes
        // (emitter props in FUN_06245bf0, the compose, the mesh pose). FUN_0623fb80
        // zeroes it; the writer is outer +0x5C SetNodeOuterTime (cone LAB_0621c2c0),
        // which IFxEffect Slot6 (LAB_0621f820) walks over every node.
        float AppearanceOuterTime_;

        // Retail node+0xb4 (the beam's +0xb8): the appearance's texture state,
        // FUN_06244b20 at construction, FUN_06244b50 in every appearance destructor.
        // The port keeps it on the shared base; a node that never resolves a texture
        // keeps Handle -1, for which ReleaseHandle is a no-op.
        FFxTextureWrapper Texture_;

        // Retail node+0xf0 (dust +0xe8): the billboard set, i.e. PropertySet_ typed, for
        // the six billboard appearances; nullptr for every other node. Set by
        // OnPropertySetChanged of those six leaves.
        FAppearanceRampDescriptor* Descriptor_;
    };

    // RegisterComponent #15 (FxNode). A plain node has no field/emitter/appearance
    // behaviour, so every inherited hook is the retail one. (#14 "FxNodePropertySet" is
    // not a node: its factory builds FxNode's own property set, FFxNodeOwnPropertySet;
    // see alchemy_factory.cpp.)
    class FxNode final : public FGenericFxObjectBase
    {
    public:
        FxNode(const char* PrimaryIid, const char* FactoryName)
            : FGenericFxObjectBase(PrimaryIid, FactoryName)
        {
        }

        // LAB_06240840 (node +0x20), outer alias LAB_06202740: constant 0x190.
        int DACOM_API GetSortKey() override { return 0x190; }

        // FUN_0623fc90 (outer thunk 062026b0): the node (0xa0 bytes) and FUN_0623fc30
        // both inlined.
        GENRESULT DACOM_API CloneNode(IDAComponent** Out) override;

    protected:
        // FUN_0623ff00 (node slot +0x48): operator_new(0x54), FUN_0623f7b0, inner vtable
        // 0x0624f9e0, outer 0x0624f980, count 1, then the adoption tail.
        void CreatePropertySet() override;
    };

} // namespace AlchemyImpl
