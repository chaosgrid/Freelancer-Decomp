// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "alchemy_genericfx.h"

#include "alchemy_common.h"
#include "alchemy_ramp.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>

// alchemy.dll — DACOM particle FX scaffolding.
// Out-of-line method bodies for FGenericFxObjectBase (shared COM/tree machinery,
// the property sets and the base set's protocol) and the trivial FxNode leaf.
// Field/emitter/appearance leaves live in alchemy_fields.cpp / alchemy_emitters.cpp /
// alchemy_appearances.cpp.
namespace AlchemyImpl
{
    namespace
    {
        // The base set's hashes (.data 0x062572b0 table below).
        const unsigned long kNodeClassNameHash = 0xF54EF296u;
        const unsigned long kNodeNameHash = 0xEFA8CE01u;
        const unsigned long kNodeLifeSpanHash = 0xF27FDE7Du;
        const unsigned long kNodeTransformHash = 0xE13A59A1u;
        const unsigned long kAppearanceLodCurveHash = 0x0C53B008u;

        // The base set's property table, .data 0x062572b0 (walked by FUN_0623f960).
        const FNodePropertyRow kNodePropertyRows[] = {
            {"Node_ClassName", kNodeClassNameHash, 0x103, 0.0f, 0.0f},
            {"Node_Name", kNodeNameHash, 0x103, 0.0f, 0.0f},
            {"Node_LifeSpan", kNodeLifeSpanHash, 3, 0.0f, kNodePropertyRowMax},
            {"Node_Transform", kNodeTransformHash, 0x105, 0.0f, 0.0f},
            {nullptr, 0, 0, 0.0f, 0.0f},
        };

        // DAT_062573a8 / DAT_06257368 / DAT_062573c4 (ImpFxNode.cpp).
        const char kImpFxNodeCppPath[] =
            "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\ALIL\\ImpFxNode.cpp";

        I3DMathEngine* G_NodeTransform3DMath = nullptr; // DAT_06257bd0.

        I3DMathEngine* NodeTransformMath()
        {
            if (G_NodeTransform3DMath == nullptr)
                G_NodeTransform3DMath = Create3DMath();
            return G_NodeTransform3DMath;
        }

#pragma optimize("", off)
#pragma float_control(except, on, push)
        // FUN_06234ab0: divide before the raw copies, store its float result last.
        __declspec(noinline) void InitializeListParticle(FPooledParticle& Particle,
            FGenericFxObjectBase* Owner, const float* Six, const float& Age, const float& Duration)
        {
            const double Reciprocal = 1.0 / static_cast<double>(Duration);
            std::memcpy(&Particle.PosX, Six, sizeof(float) * 6);
            std::memcpy(&Particle.Age, &Age, sizeof(Age));
            Particle.Appearance = Owner;
            std::memcpy(&Particle.Duration, &Duration, sizeof(Duration));
            Particle.InvDuration = static_cast<float>(Reciprocal);
        }
#pragma float_control(pop)
#pragma optimize("", on)

        // FUN_062443b0's by-value COM argument, including its EH unwind release.
        struct FScopedFieldReference
        {
            explicit FScopedFieldReference(IDAComponent* FieldIn) : Field(FieldIn)
            {
                if (Field != nullptr)
                    Field->AddRef();
            }
            ~FScopedFieldReference()
            {
                if (Field != nullptr)
                    Field->Release();
            }

            FScopedFieldReference(const FScopedFieldReference&) = delete;
            FScopedFieldReference& operator=(const FScopedFieldReference&) = delete;
            IDAComponent* Field;
        };

        template <typename TOwner>
        struct FMemberCleanup
        {
            TOwner* Owner;
            void (TOwner::*Action)();
            bool bActive;

            FMemberCleanup(TOwner* InOwner, void (TOwner::*InAction)())
                : Owner(InOwner), Action(InAction), bActive(true) {}
            ~FMemberCleanup() noexcept(false)
            {
                if (bActive)
                    (Owner->*Action)();
            }
            void Dismiss() { bActive = false; }
            FMemberCleanup(const FMemberCleanup&) = delete;
            FMemberCleanup& operator=(const FMemberCleanup&) = delete;
        };

        template <typename TInterface>
        struct FPointerCleanup
        {
            TInterface*& Slot;
            bool bActive;

            explicit FPointerCleanup(TInterface*& InSlot) : Slot(InSlot), bActive(true) {}
            ~FPointerCleanup() noexcept(false)
            {
                if (bActive)
                    ReleaseDescriptorSlot(Slot);
            }
            void Dismiss() { bActive = false; }
            FPointerCleanup(const FPointerCleanup&) = delete;
            FPointerCleanup& operator=(const FPointerCleanup&) = delete;
        };
    } // namespace

    // ---- FFxHashedName ----

    FFxHashedName::FFxHashedName() : Hash(HashNameCrc(nullptr)), Buffer(nullptr), Length(0), Capacity(0)
    {
    }

    FFxHashedName::~FFxHashedName()
    {
        ReleaseBuffer();
    }

    void FFxHashedName::ReleaseBuffer()
    {
        // Retail name teardown leaves its pointer and scalar fields untouched before
        // Transform::Release. Only disarm the later automatic member destructor.
        if (!bBufferReleased_)
        {
            delete[] Buffer;
            bBufferReleased_ = true;
        }
    }

    // FUN_0623f9e0 (= FUN_06209c50). The port's buffer is always owned once allocated,
    // so the owned flag (+0x34 / +0x0c) is implied by Buffer.
    void FFxHashedName::Assign(const char* Text)
    {
        if (Text == nullptr)
        {
            delete[] Buffer;
            Buffer = nullptr;
            Capacity = 0;
            Length = 0;
        }
        else if (Text != Buffer)
        {
            // Reuse only when length < capacity (signed compare).
            Length = static_cast<unsigned long>(std::strlen(Text));
            if (!(static_cast<long>(Length) < static_cast<long>(Capacity)))
            {
                delete[] Buffer;
                Buffer = new char[Length + 1];
                Capacity = Length;
            }
            std::memcpy(Buffer, Text, Length + 1);
        }
        Hash = HashNameCrc(Buffer);
    }

    // ---- Property sets ----

    // FUN_0623f7b0: +0x48 LifeSpan = 3.0f; a new FxAnimatedTransform (operator_new(0x34),
    // vtable 0x0624bbb8) stored with AddRef and its creation reference released, then
    // Reset; last, the name cleared and its CRC taken.
    FFxNodePropertySet::FFxNodePropertySet() : Transform(nullptr), LifeSpan(3.0f), RefCount_(1)
    {
        FMemberCleanup<FFxNodePropertySet> ConstructionScope(this,
            &FFxNodePropertySet::CleanupFailedConstruction);
        FFxAnimatedTransform* const Created = new (std::nothrow) FFxAnimatedTransform();
        Transform = Created;
        if (Transform != nullptr)
            Transform->AddRef();
        Created->Release();
        Transform->Reset();
        Name.Assign(nullptr);
        ConstructionScope.Dismiss();
    }

    void FFxNodePropertySet::CleanupFailedConstruction()
    {
        Name.ReleaseBuffer();
        ReleaseDescriptorSlot(Transform);
    }

    // FUN_06212ec0 and the inlined base teardown in Gravity 062242d0: Node_Name
    // buffer, Transform, ClassName buffer. Release Name storage here so automatic
    // member destruction cannot defer it past Transform; ClassName still unwinds last.
    FFxNodePropertySet::~FFxNodePropertySet() noexcept(false)
    {
        Name.ReleaseBuffer();
        ReleaseDescriptorSlot(Transform);
    }

    // Outer +0x08 (sphere 0623cde0; the same body per class): the count drops only while
    // non-zero; at zero it is set to 1 around the destructor, then the object is freed.
    unsigned long DACOM_API FFxNodePropertySet::Release()
    {
        if (RefCount_ != 0)
            --RefCount_;
        if (RefCount_ != 0)
            return RefCount_;
        RefCount_ = 1;
        // Retail frees storage only after the virtual destructor returns successfully.
        this->~FFxNodePropertySet();
        ::operator delete(this);
        return 0;
    }

    // Basic 06218670 (table 0x0624ca9c); FxNode's 06240c50 is the same code over
    // 0x0624f8c0. Both tables have offset 0 in every row, so a hit returns this.
    GENRESULT DACOM_API FFxNodePropertySet::QueryInterface(const char* Iid, void** Out)
    {
        if (IidEqual(Iid, PrimaryIid()) || IidEqual(Iid, Iids::DAComponent))
        {
            AddRef();
            *Out = static_cast<IFxNodePropertySet*>(this);
            return GR_OK;
        }
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    const char* FFxNodePropertySet::PrimaryIid() const
    {
        return Iids::FxNodePropertySet;
    }

    // Table 0x0624f8c0: {"1.11_IFxNode", 0}, {"1.11_IDAComponent", 0}.
    const char* FFxNodeOwnPropertySet::PrimaryIid() const
    {
        return Iids::FxNode;
    }

    // FUN_0623f8d0.
    GENRESULT FFxNodePropertySet::Initialize(const AGGDESC* /*Desc*/)
    {
        Transform->Reset();
        Name.Assign(nullptr);
        return GR_OK;
    }

    // FUN_06243ff0: after FUN_0623f7b0, +0x4c the list's allocator byte (uninitialised),
    // +0x50 a new self-linked 0xc head, +0x54 = 0, then +0x58 an FxRampSingleRamp holding
    // one FxRampSingle key 1.0 at time 0.
    FFxAppearancePropertySet::FFxAppearancePropertySet()
        : EffectScaleRamp(nullptr)
    {
        FPointerCleanup<INestedSingleRamp> PublishedScope(EffectScaleRamp);
        FRampSingleRamp* const Ramp = new FRampSingleRamp();
        IRampSingle* TemporaryChild = nullptr;
        FPointerCleanup<IRampSingle> ChildScope(TemporaryChild);
        const float Value = 1.0f;
        FRampSingle* const Child = new FRampSingle();
        Child->SetMode(4);
        Child->AddKey(0.0f, &Value);
        TemporaryChild = Child;
        Ramp->AddKey(0.0f, Child);
        EffectScaleRamp = Ramp;
        ChildScope.Dismiss();
        Child->Release();
        PublishedScope.Dismiss();
    }

    FFxAppearancePropertySet::~FFxAppearancePropertySet()
    {
        ReleaseDescriptorSlot(EffectScaleRamp);
    }

    // The billboard builders zero +0x60 (Flags) and +0x64 (TextureCrc) and seed the blend
    // pair {5, 2}; each class's builder in alchemy_appearances.cpp then builds its ramps.
    FAppearanceRampDescriptor::FAppearanceRampDescriptor()
        : Flags(0), TextureCrc(0), SizeRamp(nullptr), ColorRamp(nullptr),
          PrimarySizeRamp(nullptr), TexFrameRamp(nullptr), AxisOverrideRamp(nullptr),
          FrameRamp(nullptr), CommonTexFrameRamp(nullptr), LocalTransform(nullptr)
    {
        BlendInfo[0] = 5;
        BlendInfo[1] = 2;
    }

    // The ramps and the transform are smart-pointer members in retail; their teardown
    // releases each one (FUN_0620a010 / FUN_06209ff0 / FUN_0620a030 / FUN_0620a0d0).
    FAppearanceRampDescriptor::~FAppearanceRampDescriptor()
    {
        ReleaseDescriptorSlot(SizeRamp);
        ReleaseDescriptorSlot(ColorRamp);
        ReleaseDescriptorSlot(PrimarySizeRamp);
        ReleaseDescriptorSlot(TexFrameRamp);
        ReleaseDescriptorSlot(AxisOverrideRamp);
        ReleaseDescriptorSlot(FrameRamp);
        ReleaseDescriptorSlot(CommonTexFrameRamp);
        ReleaseDescriptorSlot(LocalTransform);
    }

    // FUN_0622f550: both names assigned "" (DAT_06257a70) and hashed, +0x9c = 0.
    FFxParticleAppearancePropertySet::FFxParticleAppearancePropertySet() : Flags(0)
    {
        LifeName.Assign("");
        DeathName.Assign("");
    }

    // FUN_06224710: +0x84 a new FxAnimatedTransform stored with AddRef, the creation
    // reference released; no Reset, unlike the base set's.
    FFxMeshAppearancePropertySet::FFxMeshAppearancePropertySet()
        : MeshId(0), bUseParticleTransform(0), ParticleTransform(nullptr)
    {
        MeshName.Assign("");
        FFxAnimatedTransform* const Created = new FFxAnimatedTransform();
        ParticleTransform = Created;
        ParticleTransform->AddRef();
        Created->Release();
    }

    FFxMeshAppearancePropertySet::~FFxMeshAppearancePropertySet()
    {
        ReleaseDescriptorSlot(ParticleTransform);
    }

    // ---- FGenericFxObjectBase ----

    FGenericFxObjectBase::FGenericFxObjectBase(const char* PrimaryIid, const char* FactoryName)
        : RefCount_(1),
          // Node base init FUN_0623fb80.
          NodeFlags_(0), PrimaryIid_(PrimaryIid), FactoryName_(FactoryName),
          PropertySet_(nullptr),
          AssociatedAppearance_(nullptr),
          AssociatedFields_(nullptr), FieldCount_(0), FieldCapacity_(0),
          AttachParent_(nullptr), FirstChild_(nullptr), PrevSibling_(nullptr),
          NextSibling_(nullptr), LiveInstanceCount_(0),
          LodInput_(1.0f), bIsAppearanceNode_(0), bIsEmitterNode_(0), bIsFieldNode_(0),
          // Appearance init FUN_06244360: node+0xb0 = 0.
          EmitterRefCount_(0),
          InstanceListHead_(nullptr), InstanceListTail_(nullptr),
          Age_(0.0f), Duration_(100.0f), AppearanceOuterTime_(0.0f),
          // +0x12C (FLDustAppearance only); no explicit retail ctor write found.
          DustPrevCameraDistance_(0.0f), Descriptor_(nullptr)
    {
        // +0x1c (raw) and +0x4c (composed) both start as identity.
        unsigned long Index = 0;
        for (Index = 0; Index < 12; ++Index)
        {
            const float Value = (Index == 0 || Index == 4 || Index == 8) ? 1.0f : 0.0f;
            NodeLocalXform_[Index] = Value;
            NodeWorldXform_[Index] = Value;
        }
        // Retail sets the appearance tag (+0x94) in the shared appearance init FUN_06244360
        // and the field tag (+0x96) in each of the seven field constructors. The port has
        // no appearance or field base class, so the tag follows the primary interface
        // here; the emitter tag (+0x95) is set by FFxEmitterBase.
        if (IidEqual(PrimaryIid_, Iids::FxAppearance))
            bIsAppearanceNode_ = 1;
        if (IidEqual(PrimaryIid_, Iids::FxField))
            bIsFieldNode_ = 1;
    }

    namespace
    {
        // DAT_06255804: the name every texture key carries.
        const char kTexInfoKeyName[] = "???";
        // DAT_06257698 / DAT_062576dc.
        const char kTexInfoCppPath[] =
            "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\ALIL\\ImpFxTexInfo.cpp";
        const char kTexInfoResolveFailFmt[] = "couldnt get effect texture %s";

        ITextureLibrary2* TexInfoLibrary()
        {
            return static_cast<ITextureLibrary2*>(G_TextureLibrary);
        }

        // FUN_06244f60 (cdecl): _vsnprintf(Buffer, 0x2000, Format, args); returns Buffer.
        // Retail's MSVCRT _vsnprintf leaves the buffer unterminated on truncation; the
        // one caller formats "???" into a 30-character message, far below 0x2000.
        char* FormatTexInfoMessage(char* Buffer, const char* Format, ...)
        {
            va_list Args;
            va_start(Args, Format);
            _vsnprintf(Buffer, 0x2000, Format, Args);
            va_end(Args);
            return Buffer;
        }
    } // namespace

    // FUN_06244b20.
    FFxTextureWrapper::FFxTextureWrapper()
        : TexCrc(0), Handle(-1), FrameCount(0), Field_0x20(0)
    {
        Frame[0] = 0.0f;
        Frame[1] = 0.0f;
        Frame[2] = 0.0f;
        Frame[3] = 0.0f;
        Frame[4] = 0.0f;
    }

    // FUN_06244b50 (DAT_06257bbc = G_TextureLibrary).
    void FFxTextureWrapper::ReleaseHandle()
    {
        if (Handle != -1 && G_TextureLibrary != nullptr)
        {
            TexInfoLibrary()->DecRefById(Handle);
            Handle = -1;
        }
        TexCrc = 0;
    }

    // FUN_06244b80 (returns 0): FUN_06244b50 inlined.
    void FFxTextureWrapper::SetTexture(const FFxTextureKey* Key)
    {
        if (TexCrc == Key->Crc)
            return;
        if (Handle != -1 && G_TextureLibrary != nullptr)
        {
            TexInfoLibrary()->DecRefById(Handle);
            Handle = -1;
        }
        TexCrc = 0;
        TexCrc = Key->Crc;
    }

    // FUN_06244df0. Unlike ReleaseHandle, retail does not test the library for null here.
    void FFxTextureWrapper::Resolve()
    {
        int Id = -1;
        if (Handle != -1)
            return;
        if (TexCrc == 0)
            return;
        FFxTextureKey Key;
        Key.Crc = TexCrc;
        Key.Name = kTexInfoKeyName;
        void* Record = nullptr;
        if (TexInfoLibrary()->FindTextureByCrc(&Key, &Record) < 0)
        {
            char Message[0x2000];
            FDUMP(0x100002, kFdumpWarningFmt, kTexInfoCppPath, 0x63,
                  FormatTexInfoMessage(Message, kTexInfoResolveFailFmt, kTexInfoKeyName));
            return;
        }
        TexInfoLibrary()->CreatePlaybackGroup(Record, &Id);
        TexInfoLibrary()->GetFrameCount(Record, &FrameCount);
        TexInfoLibrary()->ReleaseTextureRecord(Record);
        Handle = Id;
        Frame[0] = 0.0f;
        TexInfoLibrary()->SetWrapMode(Id, 0);
        TexInfoLibrary()->GetFrame(Handle, -1, reinterpret_cast<unsigned long*>(Frame));
    }

    // FUN_06244f30.
    void FFxTextureWrapper::LookupFrame(int FrameIndex, float* Out)
    {
        if (Handle == -1)
            return;
        TexInfoLibrary()->GetFrame(Handle, FrameIndex, reinterpret_cast<unsigned long*>(Out));
    }

    FGenericFxObjectBase::~FGenericFxObjectBase() noexcept(false)
    {
        if (NameEqual(FactoryName_, "FxRectAppearance"))
        {
            DestroyListAppearance(&G_PoolRect);
            return;
        }
        if (NameEqual(FactoryName_, "FxBasicAppearance"))
        {
            DestroyListAppearance(&G_PoolBasic);
            return;
        }
        // FUN_06219070: the first action is to erase the particle list at +0xd8/+0xdc
        // (FUN_062184c0), fetching each node's next pointer before FUN_06217ae0 unlinks it,
        // drops the live count and frees it to the pool (FUN_0623ecf0 = FreePoolItem).
        // Particles that aged out earlier were already freed by IntegratePool and are no
        // longer on the list.
        if (InstanceListHead_ != nullptr)
        {
            FAppearancePool* InstancePool = PoolForAppearanceFactory(FactoryName_);
            FPooledParticle* Node = InstanceListHead_;
            while (Node != nullptr)
            {
                FPooledParticle* Next = Node->Next;
                if (InstancePool == &G_PoolRect || InstancePool == &G_PoolPerp)
                {
                    Next = EraseListParticle(InstancePool, Node);
                }
                else
                {
                    UnlinkParticleNode(&InstanceListHead_, &InstanceListTail_, Node);
                    if (LiveInstanceCount_ != 0)
                        --LiveInstanceCount_;
                    ReleaseAppearancePoolItem(InstancePool, Node);
                }
                Node = Next;
            }
        }
        // Texture wrapper (+0xb4) released after the list and before the field array
        // (+0x9c/+0xa0).
        Texture_.ReleaseHandle();
        if (AssociatedAppearance_ != nullptr)
            AssociatedAppearance_->Release();
        ReleaseAssociatedFields();
        // The node-base destructor releases +0x7c, the set's outer: the set dies with its
        // last node (archetype and clones share it).
        Descriptor_ = nullptr;
        ReleaseDescriptorSlot(PropertySet_);
    }

    // FLBeamField's set creator FUN_06244fb0: a base set (FUN_0623f7b0) under the field
    // vtables 0x0624fda8 / 0x0624fcf0, whose QI answers "1.11_IFxNodePropertySet". Every
    // other node class overrides this.
    void FGenericFxObjectBase::CreatePropertySet()
    {
        AdoptPropertySet(new FFxNodePropertySet());
    }

    // FxNode's set creator FUN_0623ff00 (node vtable 0x0624f930 slot +0x48): the same base
    // set under FxNode's vtables 0x0624f9e0 / 0x0624f980.
    void FxNode::CreatePropertySet()
    {
        AdoptPropertySet(new (std::nothrow) FFxNodeOwnPropertySet());
    }

    // The tail every creator shares (inlined in FxNode's and sphere's creators): Release
    // the old +0x7c, store the new one with AddRef, Release the creation reference, cache
    // the set.
    void FGenericFxObjectBase::AdoptPropertySet(FFxNodePropertySet* Set)
    {
        if (PropertySet_ != Set)
        {
            if (PropertySet_ != nullptr)
                PropertySet_->Release();
            PropertySet_ = Set;
            if (PropertySet_ != nullptr)
                PropertySet_->AddRef();
        }
        Set->Release();
        OnPropertySetChanged();
    }

    void FGenericFxObjectBase::OnPropertySetChanged()
    {
    }

    // FUN_0623fde0, called by every node factory right after construction (a non-zero
    // result destroys the node). Node slot +0x48 creates the set.
    GENRESULT FGenericFxObjectBase::InitializeFromFactory(const FAggDesc8* Desc)
    {
        CreatePropertySet();
        if (Desc == nullptr)
        {
            FDUMP(0x100002, "%s(%d) : WARNING:General:%s", kImpFxNodeCppPath, 0xb3,
                  "you should set DACOMDESC for create FxNode\n");
        }
        else
        {
            PropertySet_->ClassName.Assign(Desc->NameAt4);
        }
        std::memcpy(&Duration_, &PropertySet_->LifeSpan, sizeof(Duration_));
        return GR_OK;
    }

    // Every node class's outer QI is the same table walk (e.g. Gravity 06223bb0 over
    // 0624dcd8 {"1.11_IFxField", 0}, {"1.11_IFxNode", 0}, {"1.11_IDAComponent", 0}).
    // Retail does not test Out for null.
    GENRESULT DACOM_API FGenericFxObjectBase::QueryInterface(const char* Iid, void** Out)
    {
        if (IidEqual(Iid, PrimaryIid_) || IidEqual(Iid, Iids::DAComponent) ||
            IidEqual(Iid, Iids::FxNode))
        {
            AddRef();
            *Out = static_cast<IGenericFx*>(this);
            return GR_OK;
        }
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    unsigned long DACOM_API FGenericFxObjectBase::Release()
    {
        // FUN_0623a190 / FUN_0623cce0 / FUN_0623dc80: conditional decrement,
        // store 1 at zero, destroy, then free storage only on successful return.
        if (RefCount_ != 0)
            --RefCount_;
        const unsigned long Next = RefCount_;
        if (Next == 0)
        {
            RefCount_ = 1;
            this->~FGenericFxObjectBase();
            ::operator delete(this);
            return 0;
        }
        return Next;
    }

    // FUN_06240180: TickLifetime through the object's own vtable (+0x44).
    GENRESULT DACOM_API FGenericFxObjectBase::Pad5(float Dt)
    {
        TickLifetime(Dt);
        return GR_OK;
    }

    // FUN_062400b0: index 0 -> world (+0x4c), 1 -> local (+0x1c).
    GENRESULT DACOM_API FGenericFxObjectBase::GetNodeTransform(unsigned long Index,
                                                               float** OutSlot)
    {
        if (Index == 0)
        {
            *OutSlot = NodeWorldXform_;
            return GR_OK;
        }
        if (Index == 1)
        {
            *OutSlot = NodeLocalXform_;
            return GR_OK;
        }
        return static_cast<GENRESULT>(0xfffffffe);
    }

    // FUN_062400f0.
    GENRESULT DACOM_API FGenericFxObjectBase::SetNodeTransform(unsigned long Index,
                                                               const float* Source12)
    {
        if (Index == 0)
        {
            std::memcpy(NodeWorldXform_, Source12, sizeof(NodeWorldXform_));
            NodeFlags_ |= 0x8;
            return GR_OK;
        }
        if (Index == 1)
        {
            std::memcpy(NodeLocalXform_, Source12, sizeof(NodeLocalXform_));
            NodeFlags_ |= 0x4;
            return GR_OK;
        }
        return static_cast<GENRESULT>(0xfffffffe);
    }

    // FUN_062401a0. The first argument is never read; every branch takes the parent from
    // the second. anim = [[this+0x80]+0x24]; Mul is I3DMathEngine +0x1c (DAT_06257bd0,
    // cached independently).
    //   flag 0x8 set                  -> clear it, keep the world transform set from outside.
    //   flag 0x4 clear, 0x2 or ident. -> raw = identity, world = *Parent12.
    //   flag 0x4 clear, animated      -> raw = Evaluate(...), world = Mul(Parent, raw).
    //   flag 0x4 set,   0x2 or ident. -> world = Mul(Parent, raw).
    //   flag 0x4 set,   animated      -> world = Mul(Mul(Parent, raw), Evaluate(...)).
    // The IsIdentity call (anim+0x20) is skipped when flag 0x2 is set, as in retail.
    GENRESULT DACOM_API FGenericFxObjectBase::ComposeNodeTransform(void* /*Unused*/,
                                                                   const float* Parent12)
    {
        const unsigned long Flags = NodeFlags_;
        if ((Flags & 0x8) != 0)
        {
            NodeFlags_ = Flags & ~0x8ul;
            return GR_OK;
        }

        const bool bSkipAnim = (Flags & 0x2) != 0 || FindNodeAnimatedTransform()->IsIdentity() != 0;

        if ((Flags & 0x4) == 0)
        {
            if (bSkipAnim)
            {
                unsigned long Index = 0;
                for (Index = 0; Index < 12; ++Index)
                    NodeLocalXform_[Index] = (Index == 0 || Index == 4 || Index == 8) ? 1.0f : 0.0f;
                std::memcpy(NodeWorldXform_, Parent12, sizeof(NodeWorldXform_));
                return GR_OK;
            }
            IAnimatedTransform* const Anim = FindNodeAnimatedTransform();
            unsigned long TimeA = 0;
            unsigned long TimeB = 0;
            std::memcpy(&TimeB, &Age_, sizeof(TimeB));
            std::memcpy(&TimeA, &AppearanceOuterTime_, sizeof(TimeA));
            Anim->Evaluate(TimeA, TimeB, NodeLocalXform_);
            float Composed[12];
            NodeTransformMath()->MulTransform(*reinterpret_cast<FTransform*>(Composed),
                                          *reinterpret_cast<const FTransform*>(Parent12),
                                          *reinterpret_cast<const FTransform*>(NodeLocalXform_));
            std::memcpy(NodeWorldXform_, Composed, sizeof(NodeWorldXform_));
            return GR_OK;
        }

        if (bSkipAnim)
        {
            float Composed[12];
            NodeTransformMath()->MulTransform(*reinterpret_cast<FTransform*>(Composed),
                                          *reinterpret_cast<const FTransform*>(Parent12),
                                          *reinterpret_cast<const FTransform*>(NodeLocalXform_));
            std::memcpy(NodeWorldXform_, Composed, sizeof(NodeWorldXform_));
            return GR_OK;
        }

        float Local[12] = { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f };
        IAnimatedTransform* const Anim = FindNodeAnimatedTransform();
        unsigned long TimeA = 0;
        unsigned long TimeB = 0;
        std::memcpy(&TimeB, &Age_, sizeof(TimeB));
        std::memcpy(&TimeA, &AppearanceOuterTime_, sizeof(TimeA));
        Anim->Evaluate(TimeA, TimeB, Local);
        float Composed[12];
        NodeTransformMath()->MulTransform(*reinterpret_cast<FTransform*>(Composed),
                                      *reinterpret_cast<const FTransform*>(Parent12),
                                      *reinterpret_cast<const FTransform*>(NodeLocalXform_));
        std::memcpy(NodeWorldXform_, Composed, sizeof(NodeWorldXform_));
        NodeTransformMath()->MulTransform(*reinterpret_cast<FTransform*>(Composed),
                                      *reinterpret_cast<const FTransform*>(NodeWorldXform_),
                                      *reinterpret_cast<const FTransform*>(Local));
        std::memcpy(NodeWorldXform_, Composed, sizeof(NodeWorldXform_));
        return GR_OK;
    }

    // FUN_0623ffb0: *Out12 = inverse(Other world) * this world.
    GENRESULT FGenericFxObjectBase::ComputeRelativeTransform(IDAComponent* Other, float* Out12)
    {
        float* OtherWorld = nullptr;
        AsFxNode(Other)->GetNodeTransform(0, &OtherWorld);
        float Inverse[12];
        float W;
        NodeTransformMath()->GeneralInverse(*reinterpret_cast<FTransform*>(Inverse), W,
                             *reinterpret_cast<const FTransform*>(OtherWorld)); // +0x5C
        float Rel[12];
        NodeTransformMath()->MulTransform(*reinterpret_cast<FTransform*>(Rel),
                           *reinterpret_cast<const FTransform*>(Inverse),
                           *reinterpret_cast<const FTransform*>(NodeWorldXform_)); // +0x1C
        std::memcpy(Out12, Rel, sizeof(Rel));
        return GR_OK;
    }

    // FUN_0623fc30. The appearance clone ctors then copy the set cache (basic
    // FUN_06215d80), which OnPropertySetChanged does here.
    void FGenericFxObjectBase::InitNodeFromArchetype(FGenericFxObjectBase* Archetype)
    {
        // Float bits copied without an x87 load/store.
        std::memcpy(&Duration_, &Archetype->PropertySet_->LifeSpan, sizeof(Duration_));
        if (PropertySet_ != nullptr)
        {
            PropertySet_->Release();
            PropertySet_ = nullptr;
        }
        // Retail reloads the archetype's reference after the Release callback.
        PropertySet_ = Archetype->PropertySet_;
        if (PropertySet_ != nullptr)
            PropertySet_->AddRef();
        OnPropertySetChanged();
    }

    // FUN_06244120: FUN_0623fc30, the appearance set cache +0xa8 (the same set as +0x80)
    // copied, then the field vector +0x98 assigned.
    void FGenericFxObjectBase::InitAppearanceFromArchetype(FGenericFxObjectBase* Archetype)
    {
        InitNodeFromArchetype(Archetype);
        CopyAssociatedFieldsFrom(Archetype);
    }

    // FUN_0623fc90: operator_new(0xa0), FxNode's constructor (vtables 0x0624b190 /
    // 0x0624b230) and FUN_0623fc30, both inlined.
    GENRESULT DACOM_API FxNode::CloneNode(IDAComponent** Out)
    {
        FxNode* const Node = new FxNode(PrimaryIid_, FactoryName_);
        Node->InitNodeFromArchetype(this);
        *Out = Node;
        return GR_OK;
    }

    // LAB_06240150: the lifetime bit is set only when Age > Duration (equal skips).
    GENRESULT DACOM_API FGenericFxObjectBase::TickLifetime(float Dt)
    {
        Age_ += Dt;
        if (Age_ > Duration_)
        {
            NodeFlags_ |= 1;
        }
        return GR_OK;
    }

    GENRESULT DACOM_API FGenericFxObjectBase::EmitOne(const float* Six, unsigned long Extra,
                                                        float Duration)
    {
        FAppearancePool* Pool = PoolForAppearanceFactory(FactoryName_);
        if (Pool == nullptr || Six == nullptr || Duration == 0.0f)
            return GR_GENERIC;
        float Age = 0.0f;
        std::memcpy(&Age, &Extra, 4);
        void* PoolItem = EmitOneParticle(Pool, this, Six, Age, Duration);
        if (PoolItem == nullptr)
            return GR_GENERIC;
        // Link onto this instance's list (+0xd8/+0xdc), which retail FUN_06215f40 does
        // inline; EmitOneParticle (alchemy_common.cpp) only sees an opaque owner.
        FPooledParticle* Particle = static_cast<FPooledParticle*>(PoolItem);
        AppendParticleNode(&InstanceListHead_, &InstanceListTail_, Particle);
        ++LiveInstanceCount_;
        return GR_OK;
    }

    GENRESULT DACOM_API FGenericFxObjectBase::GetLiveInstanceCount(unsigned long* Out)
    {
        *Out = ResolveLiveInstanceCount();
        return GR_OK;
    }

    GENRESULT DACOM_API FGenericFxObjectBase::GetQueuedVertCount(unsigned long* Out)
    {
        unsigned long VertsPer = 0;
        unsigned long IndicesPer = 0;
        AppearanceDrawUnits(FactoryName_, &VertsPer, &IndicesPer);
        *Out = ResolveLiveInstanceCount() * VertsPer;
        return GR_OK;
    }

    GENRESULT DACOM_API FGenericFxObjectBase::GetQueuedIndexCount(unsigned long* Out)
    {
        unsigned long VertsPer = 0;
        unsigned long IndicesPer = 0;
        AppearanceDrawUnits(FactoryName_, &VertsPer, &IndicesPer);
        *Out = ResolveLiveInstanceCount() * IndicesPer;
        return GR_OK;
    }

    // ---- The base set's property protocol (vtable 0x0624f8d8; FxNode's 0x0624f9e0 has
    // the same bodies at 0x06240590..0x06240800) ----

    // +0x04 06212e60 / +0x08 06212e70 / +0x0c 06209bc0 / +0x10 06209bd0: -8.
    GENRESULT DACOM_API FFxNodePropertySet::GetPropBool(unsigned long /*Hash*/, unsigned char* /*Out*/)
    {
        return GR_ALE_CREATE_FAIL;
    }

    GENRESULT DACOM_API FFxNodePropertySet::SetPropBool(unsigned long /*Hash*/, unsigned char /*Value*/)
    {
        return GR_ALE_CREATE_FAIL;
    }

    GENRESULT DACOM_API FFxNodePropertySet::GetPropInt(unsigned long /*Hash*/, unsigned long* /*Out*/)
    {
        return GR_ALE_CREATE_FAIL;
    }

    GENRESULT DACOM_API FFxNodePropertySet::SetPropInt(unsigned long /*Hash*/, unsigned long /*Value*/)
    {
        return GR_ALE_CREATE_FAIL;
    }

    // +0x14 FUN_0623faf0 (a dword copy, no x87 load).
    GENRESULT DACOM_API FFxNodePropertySet::GetPropFloat(unsigned long Hash, float* Out)
    {
        if (Hash != kNodeLifeSpanHash)
            return GR_ALE_CREATE_FAIL;
        std::memcpy(Out, &LifeSpan, sizeof(LifeSpan));
        return GR_OK;
    }

    // +0x18 FUN_0623fb20.
    GENRESULT DACOM_API FFxNodePropertySet::SetPropFloat(unsigned long Hash, float Value)
    {
        if (Hash != kNodeLifeSpanHash)
            return GR_ALE_CREATE_FAIL;
        LifeSpan = Value;
        return GR_OK;
    }

    // +0x1c 06212e80 / +0x20 06212e90: -8.
    GENRESULT DACOM_API FFxNodePropertySet::GetPropPair(unsigned long /*Hash*/, unsigned long* /*Out2*/)
    {
        return GR_ALE_CREATE_FAIL;
    }

    GENRESULT DACOM_API FFxNodePropertySet::SetPropPair(unsigned long /*Hash*/,
                                                const unsigned long* /*In2*/)
    {
        return GR_ALE_CREATE_FAIL;
    }

    // +0x24 06209be0 / +0x28 06209bf0 / +0x2c 06209c00: -8 in every set class.
    GENRESULT DACOM_API FFxNodePropertySet::GetPropColorRamp(unsigned long /*Hash*/, IRampColor** /*Out*/)
    {
        return GR_ALE_CREATE_FAIL;
    }

    GENRESULT DACOM_API FFxNodePropertySet::GetPropSingleRamp(unsigned long /*Hash*/,
                                                      IRampSingle** /*Out*/)
    {
        return GR_ALE_CREATE_FAIL;
    }

    GENRESULT DACOM_API FFxNodePropertySet::GetPropAnimatedSingle(unsigned long /*Hash*/,
                                                          IFxAnimatedSingle** /*Out*/)
    {
        return GR_ALE_CREATE_FAIL;
    }

    // +0x30 FUN_0623f9a0.
    GENRESULT DACOM_API FFxNodePropertySet::GetPropName(unsigned long Hash, const char** Out)
    {
        if (Hash == kNodeNameHash)
        {
            *Out = Name.Buffer;
            return GR_OK;
        }
        if (Hash == kNodeClassNameHash)
        {
            *Out = ClassName.Buffer;
            return GR_OK;
        }
        return GR_ALE_CREATE_FAIL;
    }

    // +0x34 FUN_0623f9e0.
    GENRESULT DACOM_API FFxNodePropertySet::SetPropName(unsigned long Hash, const char* Text)
    {
        if (Hash == kNodeNameHash)
        {
            Name.Assign(Text);
            return GR_OK;
        }
        if (Hash == kNodeClassNameHash)
        {
            FDUMP(0x100002, "%s(%d) : WARNING:General:%s", kImpFxNodeCppPath, 0x65,
                  "You can't set class name!!\n");
            return GR_GENERIC;
        }
        return GR_ALE_CREATE_FAIL;
    }

    // +0x38 06212ea0 / +0x3c 06212eb0 / +0x40 06225930: -8.
    GENRESULT DACOM_API FFxNodePropertySet::GetPropColorRampRamp(unsigned long /*Hash*/,
                                                         IRampColorRamp** /*Out*/)
    {
        return GR_ALE_CREATE_FAIL;
    }

    GENRESULT DACOM_API FFxNodePropertySet::GetPropSingleRampRamp(unsigned long /*Hash*/,
                                                          INestedSingleRamp** /*Out*/)
    {
        return GR_ALE_CREATE_FAIL;
    }

    GENRESULT DACOM_API FFxNodePropertySet::GetPropAnimatedSingleRamp(unsigned long /*Hash*/,
                                                              INestedSingleRamp** /*Out*/)
    {
        return GR_ALE_CREATE_FAIL;
    }

    // +0x50 FUN_0623fb50.
    GENRESULT DACOM_API FFxNodePropertySet::GetPropTransform(unsigned long Hash, IAnimatedTransform** Out)
    {
        if (Hash != kNodeTransformHash)
            return GR_ALE_CREATE_FAIL;
        *Out = Transform;
        (*Out)->AddRef();
        return GR_OK;
    }

    // +0x54 FUN_0623f960: the base table.
    int DACOM_API FFxNodePropertySet::EnumerateProps(FNodePropertyRowCallback Callback,
                                                     void* Context)
    {
        return WalkPropertyRows(kNodePropertyRows, Callback, Context);
    }

    // The outer +0x0c / +0x10 / +0x14 (FxNode 06240450 / 06240520 / 06240540, basic
    // 062189c0 / 06218a90 / 06218ab0): Node_Name's assignment, buffer and CRC.
    GENRESULT DACOM_API FFxNodePropertySet::SetInstanceName(const char* Text)
    {
        Name.Assign(Text);
        return GR_OK;
    }

    GENRESULT DACOM_API FFxNodePropertySet::GetInstanceName(const char** Out)
    {
        *Out = Name.Buffer;
        return GR_OK;
    }

    GENRESULT DACOM_API FFxNodePropertySet::GetInstanceNameHash(unsigned long* Out)
    {
        *Out = Name.Hash;
        return GR_OK;
    }

    // FUN_0623f920.
    int FFxNodePropertySet::WalkPropertyRows(const FNodePropertyRow* Rows,
                                               FNodePropertyRowCallback Callback, void* Context)
    {
        const FNodePropertyRow* Row = Rows;
        while (Row->Hash != 0)
        {
            const int Result = Callback(Row, Context);
            if (Result != 0)
                return Result;
            Row += 1;
        }
        return 0;
    }

    // FUN_06244330.
    GENRESULT DACOM_API FFxAppearancePropertySet::GetPropSingleRampRamp(unsigned long Hash,
                                                                        INestedSingleRamp** Out)
    {
        if (Hash != kAppearanceLodCurveHash)
            return GR_ALE_CREATE_FAIL;
        *Out = EffectScaleRamp;
        (*Out)->AddRef();
        return GR_OK;
    }

    GENRESULT DACOM_API FGenericFxObjectBase::AssociateAppearance(IDAComponent* Appearance)
    {
        if (AssociatedAppearance_ == Appearance)
            return GR_OK;
        if (AssociatedAppearance_ != nullptr)
            AssociatedAppearance_->Release();
        AssociatedAppearance_ = Appearance;
        if (AssociatedAppearance_ != nullptr)
            AssociatedAppearance_->AddRef();
        return GR_OK;
    }

    GENRESULT DACOM_API FGenericFxObjectBase::AssociateField(IDAComponent* Field)
    {
        // FUN_062443b0: the argument is a by-value smart pointer, inserted at end through
        // FUN_062447b0 (vector insert), then the copy is Released. A null field is stored
        // as a null slot; retail has no early return. Growth is size + max(size, 1).
        FScopedFieldReference Argument(Field);
        IDAComponent** Next = nullptr;
        unsigned long Capacity = 0;
        unsigned long Index = 0;
        if (FieldCount_ == FieldCapacity_)
        {
            Capacity = FieldCount_ + (FieldCount_ > 1 ? FieldCount_ : 1);
            Next = new IDAComponent*[Capacity];
            for (Index = 0; Index < FieldCount_; ++Index)
            {
                Next[Index] = AssociatedFields_[Index];
                if (Next[Index] != nullptr)
                    Next[Index]->AddRef();
            }
            Next[FieldCount_] = Field;
            if (Field != nullptr)
                Field->AddRef();
            for (Index = 0; Index < FieldCount_; ++Index)
            {
                if (AssociatedFields_[Index] != nullptr)
                {
                    AssociatedFields_[Index]->Release();
                    AssociatedFields_[Index] = nullptr;
                }
            }
            delete[] AssociatedFields_;
            AssociatedFields_ = Next;
            FieldCapacity_ = Capacity;
        }
        else
        {
            AssociatedFields_[FieldCount_] = Field;
            if (Field != nullptr)
                Field->AddRef();
        }
        FieldCount_ += 1;
        return GR_OK;
    }

    // FUN_06244580 (appearance +0xB4, through a this+0xc thunk): each field's +0x9c
    // (BeginField) with the appearance. BeginField's Dt is port-only -- retail passes the
    // appearance alone -- so 0.0f; no field's BeginField reads it (the radial blend that
    // needs Dt is in FxRadialField::Pad5 = LAB_06236450).
    GENRESULT DACOM_API FGenericFxObjectBase::Pad45()
    {
        IDAComponent** Position = AssociatedFields_;
        IDAComponent** End = Position;
        if (FieldCount_ != 0)
            End += FieldCount_;
        while (Position != End)
        {
            AsFxNode(*Position)->BeginField(static_cast<IGenericFx*>(this), 0.0f);
            ++Position;
        }
        return GR_OK;
    }

    // FUN_062445c0 (appearance +0xB8, the same this+0xc thunk on all eight appearance
    // vtables). The emitter calls it before the spawn (+0xBC) and spawns only when it is
    // not negative. Duration < 0.01 (DAT_0624fc60) returns -1; unordered continues.
    // One Six/Age copy lives across the whole field walk. Field +0xa4 is ApplyFieldToVel;
    // its Dt is port-only (retail passes two arguments), so 0.0f.
    GENRESULT DACOM_API FGenericFxObjectBase::Pad46(const float* Six, float Age, float Duration)
    {
        if (static_cast<double>(Duration) < 0.01)
            return GR_GENERIC;

        float SixCopy[6];
        for (unsigned long CopyIndex = 0; CopyIndex < 6; ++CopyIndex)
            SixCopy[CopyIndex] = Six[CopyIndex];

        float AgeCopy = Age;
        for (unsigned long FieldIndex = 0; FieldIndex < FieldCount_; ++FieldIndex)
        {
            IGenericFx* Field = QueryGenericFx(AssociatedFields_[FieldIndex]);
            Field->ApplyFieldToVel(SixCopy, &AgeCopy, 0.0f);
            Field->Release();
            if (AgeCopy != Age && AgeCopy > Duration)
                return GR_GENERIC;
        }
        return GR_OK;
    }

    // FUN_06244660: borrowed field End calls over a captured vector range.
    GENRESULT DACOM_API FGenericFxObjectBase::Pad48()
    {
        IDAComponent** Position = AssociatedFields_;
        IDAComponent** End = Position;
        if (FieldCount_ != 0)
            End += FieldCount_;
        while (Position != End)
        {
            AsFxNode(*Position)->Pad40();
            ++Position;
        }
        return GR_OK;
    }

    // Beam FUN_0620b7d0 / Dust FUN_062110e0: cache the successor, unlink, decrement
    // the item's owning node count when present, and return the item to its pool.
    FPooledParticle* FGenericFxObjectBase::EraseListParticle(FAppearancePool* Pool,
                                                            FPooledParticle* Particle)
    {
        FPooledParticle* const Next = Particle->Next;
        UnlinkParticleNode(&InstanceListHead_, &InstanceListTail_, Particle);
        FGenericFxObjectBase* const Owner =
            static_cast<FGenericFxObjectBase*>(Particle->Appearance);
        if (Owner != nullptr)
            --Owner->LiveInstanceCount_;
        ReleaseAppearancePoolItem(Pool, Particle);
        return Next;
    }

    // Beam FUN_0620e430 / Dust FUN_06212700: the pool's oldest used link identifies
    // an item whose owner performs the list erase; the caller tests FreeCount.
    void FGenericFxObjectBase::EvictOldestListParticle(FAppearancePool* Pool)
    {
        const unsigned long Index =
            static_cast<unsigned long>(Pool->UsedHead - Pool->FreeNodes);
        FPooledParticle* const Particle = reinterpret_cast<FPooledParticle*>(
            static_cast<unsigned char*>(Pool->Items) + Index * Pool->Stride);
        FGenericFxObjectBase* const Owner =
            static_cast<FGenericFxObjectBase*>(Particle->Appearance);
        Owner->EraseListParticle(Pool, Particle);
    }

    // Shared body of the four billboard spawns (see the header): basic FUN_06215f40;
    // rect FUN_0623b410/FUN_0623a2e0, perp FUN_06235db0/FUN_06234ab0 and oriented
    // FUN_0622bd80 are the same code against their own pool globals.
    // A full pool evicts its oldest used item from that item's owner's list (FUN_06217ae0;
    // rect FUN_0623abe0 -> FUN_06239f80, perp FUN_06235690 -> FUN_06234750, oriented
    // FUN_0622d1a0). 1.0 / Duration has no zero check. An alloc failure faults in retail
    // on the append; after the eviction that only happens with an unallocated pool.
    FPooledParticle* FGenericFxObjectBase::SpawnListParticle(FAppearancePool* Pool,
                                                             const float* Six, const float& Age,
                                                             const float& Duration)
    {
        if (Pool->FreeCount == 0)
        {
            const unsigned long OldestIndex =
                static_cast<unsigned long>(Pool->UsedHead - Pool->FreeNodes);
            FPooledParticle* Oldest = reinterpret_cast<FPooledParticle*>(
                static_cast<unsigned char*>(Pool->Items) + OldestIndex * Pool->Stride);
            FGenericFxObjectBase* OldestOwner =
                static_cast<FGenericFxObjectBase*>(Oldest->Appearance);
            UnlinkParticleNode(&OldestOwner->InstanceListHead_, &OldestOwner->InstanceListTail_,
                               Oldest);
            if (OldestOwner != nullptr)
                --OldestOwner->LiveInstanceCount_;
            ReleaseAppearancePoolItem(Pool, Oldest);
        }

        FPooledParticle* Particle = static_cast<FPooledParticle*>(AllocPoolItem(Pool));
        if (Particle != nullptr)
        {
            InitializeListParticle(*Particle, this, Six, Age, Duration);
            ++LiveInstanceCount_;
        }
        AppendParticleNode(&InstanceListHead_, &InstanceListTail_, Particle);
        return Particle;
    }

    // Shared per-node update of the five list appearances: basic FUN_06216420; rect
    // FUN_06238780, perp FUN_06233040, oriented FUN_0622c3d0 and dust FUN_0620fb70 have
    // the same body and differ only in the draw-context counters they zero (basic
    // DAT_062579e6 / DAT_062579e8), the descriptor ramp FrameRamp names (basic +0x84,
    // oriented / rect +0x88, perp / dust +0x80) and their field-walk (FUN_06218780) /
    // integrate (FUN_062188c0) twins. The frame index is (FrameCount - 1) * Pos through
    // an unsigned 64-bit FILD, then ftol.
    void FGenericFxObjectBase::UpdateListAppearance(FAlDrawContext& DrawContext,
                                                    FAppearancePool* Pool, float Dt,
                                                    INestedSingleRamp* FrameRamp)
    {
        DrawContext.Field_0xA = 0;
        DrawContext.PrimCount = 0;
        Age_ += Dt;
        if (EmitterRefCount_ == 0 && InstanceListHead_ == nullptr)
            NodeFlags_ |= 1;

        FFxTextureKey Key;
        Key.Name = kTexInfoKeyName;
        Key.Crc = Descriptor_->TextureCrc;
        Texture_.SetTexture(&Key);
        if (Texture_.Handle == -1)
            Texture_.Resolve();
        if ((Descriptor_->Flags & 8u) == 8u)
        {
            const float Pos = static_cast<float>(FrameRamp->Eval(AppearanceOuterTime_, Age_));
            if (Texture_.FrameCount != 1 && Texture_.Handle != -1)
            {
                const unsigned long Span = static_cast<unsigned long>(Texture_.FrameCount - 1);
                const long long Frame = static_cast<long long>(
                    static_cast<double>(Span) * static_cast<double>(Pos));
                TexInfoLibrary()->GetFrame(Texture_.Handle, static_cast<int>(Frame),
                                           reinterpret_cast<unsigned long*>(Texture_.Frame));
            }
        }
        else if (Texture_.FrameCount != 1)
        {
            Texture_.Advance(Dt);
        }

        ApplyFieldsToPool(this, false);
        IntegratePool(this, Pool, Dt);
    }

    // Internal borrowed first-entry convenience, separate from IFxAppearance+0xDC.
    IDAComponent* DACOM_API FGenericFxObjectBase::GetAssociatedField()
    {
        if (FieldCount_ == 0 || AssociatedFields_ == nullptr)
            return nullptr;
        return AssociatedFields_[0];
    }

    // FUN_06244510, node+0x88 / IFxAppearance+0xDC. The error branch does not
    // write Out; the valid branch writes before AddRef and has no null-field guard.
    GENRESULT DACOM_API FGenericFxObjectBase::GetAssociatedFieldAt(unsigned long Index,
                                                                IDAComponent** Out)
    {
        if (static_cast<long>(Index) < 0 || AssociatedFields_ == nullptr || Index >= FieldCount_)
        {
            FDUMP(0x100001, "%s(%d) : ERROR:General:%s",
                  "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\ALIL\\ImpFxAppearance.cpp",
                  0x5c, "Index range over.\n");
            return GR_GENERIC;
        }
        IDAComponent* Field = AssociatedFields_[Index];
        *Out = Field;
        Field->AddRef();
        return GR_OK;
    }

    IDAComponent* FGenericFxObjectBase::BorrowAssociatedFieldAt(unsigned long Index)
    {
        if (Index >= FieldCount_ || AssociatedFields_ == nullptr)
            return nullptr;
        return AssociatedFields_[Index];
    }

    // FUN_06244430 uses &Value as its iterator, even though the captured end is the
    // associated-field array end (retail bug kept: it walks the caller's stack). Optimizers
    // and Debug /RTC are kept off so the unchecked boundary survives.
#pragma optimize("", off)
#pragma runtime_checks("", off)
    __declspec(noinline) GENRESULT DACOM_API FGenericFxObjectBase::Pad44(IDAComponent* Value)
    {
        IDAComponent* FinalValue = Value;
        if (Value != nullptr)
            Value->AddRef();

        FPointerCleanup<IDAComponent> Cleanup(Value);
        IDAComponent** Cursor = &Value;
        IDAComponent** const ShiftEnd = AssociatedFields_ + FieldCount_;
        if (Cursor + 1 != ShiftEnd)
        {
            do
            {
                if (*Cursor != nullptr)
                {
                    (*Cursor)->Release();
                    *Cursor = nullptr;
                }
                *Cursor = Cursor[1];
                if (*Cursor != nullptr)
                    (*Cursor)->AddRef();
                ++Cursor;
            } while (Cursor + 1 != ShiftEnd);
            FinalValue = Value;
        }

        IDAComponent** const TailEnd = AssociatedFields_ + FieldCount_;
        Cursor = TailEnd - 1;
        while (Cursor != TailEnd)
        {
            if (*Cursor != nullptr)
            {
                (*Cursor)->Release();
                *Cursor = nullptr;
            }
            ++Cursor;
        }
        const unsigned long LiveCount = FieldCount_;
        Cleanup.Dismiss();
        FieldCount_ = LiveCount - 1;
        if (FinalValue != nullptr)
            FinalValue->Release();
        return GR_OK;
    }
#pragma runtime_checks("", restore)
#pragma optimize("", on)

    // FUN_06218780's erase branch: each shifted slot uses FUN_0620d690
    // (Release old, clear, copy next, AddRef new); FUN_0620bd70 then destroys
    // the duplicate last slot before the vector end decreases.
    void FGenericFxObjectBase::RemoveAssociatedFieldAt(unsigned long Index)
    {
        if (AssociatedFields_ == nullptr || Index >= FieldCount_)
            return;
        for (unsigned long Shift = Index; Shift + 1 < FieldCount_; ++Shift)
        {
            if (AssociatedFields_[Shift] != nullptr)
            {
                AssociatedFields_[Shift]->Release();
                AssociatedFields_[Shift] = nullptr;
            }
            AssociatedFields_[Shift] = AssociatedFields_[Shift + 1];
            if (AssociatedFields_[Shift] != nullptr)
                AssociatedFields_[Shift]->AddRef();
        }
        if (AssociatedFields_[FieldCount_ - 1] != nullptr)
            AssociatedFields_[FieldCount_ - 1]->Release();
        AssociatedFields_[FieldCount_ - 1] = nullptr;
        FieldCount_ -= 1;
    }

    // FUN_06226600: captured shift end, freshly read tail end, then live end decrement.
    IDAComponent** FGenericFxObjectBase::EraseAssociatedField(IDAComponent** Position)
    {
        IDAComponent** const ShiftEnd = AssociatedFields_ + FieldCount_;
        IDAComponent** Destination = Position;
        IDAComponent** Source = Position + 1;
        while (Source != ShiftEnd)
        {
            if (*Destination != nullptr)
            {
                (*Destination)->Release();
                *Destination = nullptr;
            }
            *Destination = *Source;
            if (*Destination != nullptr)
                (*Destination)->AddRef();
            Destination = Source;
            Source = Destination + 1;
        }
        IDAComponent** const TailEnd = AssociatedFields_ + FieldCount_;
        Destination = TailEnd - 1;
        while (Destination != TailEnd)
        {
            if (*Destination != nullptr)
            {
                (*Destination)->Release();
                *Destination = nullptr;
            }
            ++Destination;
        }
        --FieldCount_;
        return Position;
    }

    // FUN_06244120's vector assignment (skipped for the same object): element-wise
    // smart-pointer assignment (FUN_0620d690) plus surplus Release (FUN_06244af0), or a
    // copy into new storage (FUN_06244aa0; operator_new not null-tested). Every path
    // ends with the archetype's fields, each AddRef'd once, and the old ones Released.
    void FGenericFxObjectBase::CopyAssociatedFieldsFrom(FGenericFxObjectBase* Source)
    {
        IDAComponent** Next = nullptr;
        unsigned long Index = 0;
        if (Source == this)
            return;
        if (Source->FieldCount_ > FieldCapacity_)
            Next = new IDAComponent*[Source->FieldCount_];
        for (Index = 0; Index < Source->FieldCount_; ++Index)
        {
            if (Source->AssociatedFields_[Index] != nullptr)
                Source->AssociatedFields_[Index]->AddRef();
        }
        for (Index = 0; Index < FieldCount_; ++Index)
        {
            if (AssociatedFields_[Index] != nullptr)
                AssociatedFields_[Index]->Release();
        }
        if (Next != nullptr)
        {
            delete[] AssociatedFields_;
            AssociatedFields_ = Next;
            FieldCapacity_ = Source->FieldCount_;
        }
        for (Index = 0; Index < Source->FieldCount_; ++Index)
            AssociatedFields_[Index] = Source->AssociatedFields_[Index];
        FieldCount_ = Source->FieldCount_;
    }

    void FGenericFxObjectBase::DetachRectList()
    {
        while (InstanceListHead_ != nullptr)
        {
            FPooledParticle* Particle = InstanceListHead_;
            InstanceListHead_ = Particle->Next;
            Particle->Next = nullptr;
            Particle->Prev = nullptr;
        }
        InstanceListHead_ = nullptr;
        InstanceListTail_ = nullptr;
    }

    void FGenericFxObjectBase::ReleaseRectFields()
    {
        IDAComponent** Position = AssociatedFields_;
        IDAComponent** End = Position;
        if (FieldCount_ != 0)
            End += FieldCount_;
        while (Position != End)
        {
            ReleaseDescriptorSlot(*Position);
            ++Position;
        }
        delete[] AssociatedFields_;
        AssociatedFields_ = nullptr;
        FieldCount_ = 0;
        FieldCapacity_ = 0;
    }

    void FGenericFxObjectBase::ReleaseRectPropertySet()
    {
        ReleaseDescriptorSlot(PropertySet_);
        Descriptor_ = nullptr;
    }

    void FGenericFxObjectBase::DestroyListAppearance(FAppearancePool* Pool)
    {
        FMemberCleanup<FGenericFxObjectBase> PropertyScope(this,
            &FGenericFxObjectBase::ReleaseRectPropertySet);
        FMemberCleanup<FGenericFxObjectBase> FieldsScope(this,
            &FGenericFxObjectBase::ReleaseRectFields);
        FMemberCleanup<FFxTextureWrapper> TextureScope(&Texture_, &FFxTextureWrapper::ReleaseHandle);
        FMemberCleanup<FGenericFxObjectBase> ListScope(this, &FGenericFxObjectBase::DetachRectList);
        FPooledParticle* Particle = InstanceListHead_;
        while (Particle != nullptr)
            Particle = EraseListParticle(Pool, Particle);
        ListScope.Dismiss();
        DetachRectList();
        TextureScope.Dismiss();
        Texture_.ReleaseHandle();
        FieldsScope.Dismiss();
        ReleaseRectFields();
        PropertyScope.Dismiss();
        ReleaseRectPropertySet();
    }

    void FGenericFxObjectBase::ReleaseAssociatedFields()
    {
        unsigned long Index = 0;
        for (Index = 0; Index < FieldCount_; ++Index)
        {
            if (AssociatedFields_[Index] != nullptr)
                AssociatedFields_[Index]->Release();
        }
        delete[] AssociatedFields_;
        AssociatedFields_ = nullptr;
        FieldCount_ = 0;
        FieldCapacity_ = 0;
    }

    unsigned long FGenericFxObjectBase::ResolveLiveInstanceCount() const
    {
        // Per-instance count (node+0xe4), not the pool-wide PoolUsedCount. The appearance
        // slots read it raw: basic LAB_06219500 (count), LAB_06219520 (<< 2), LAB_06219540
        // (* 6).
        return LiveInstanceCount_;
    }

} // namespace AlchemyImpl
