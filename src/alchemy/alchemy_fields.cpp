// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "alchemy_fields.h"

#include "alchemy_common.h"
#include "alchemy_math.h"
#include "alchemy_ramp.h"

#include <cmath>
#include <cstring>
#include <new>

// alchemy.dll — typed field property sets and field behaviors.
namespace AlchemyImpl
{
    namespace
    {
        // DAT_0624d160 (0x40A00000 = 5.0f): FxCollideField::BeginField's scale of the
        // unit plane normal into ScaledNormal_.
        const float kCollideNormalScale = 5.0f;

        // DAT_0624d168 (double 0.001): FxCollideField::ApplyFieldToVel's post-impact
        // speed gate -- below it only the impact position is written, the velocity is
        // not reflected.
        const double kCollideSpeedEpsilon = 0.001;

        // DAT_062579d4 (.data, 0 at load): read once, by the dust field's Apply
        // (0x06212d80); no instruction in the image writes it.
        unsigned char G_DustAgeMinusUpdateDt = 0;

        // Field property-name hashes (the names of the rows below).
        const unsigned long kGravityFieldGravityHash = 0xE02B8BD4U;
        const unsigned long kAirFieldMagnitudeHash = 0xE5E3524CU;
        const unsigned long kAirFieldApproachHash = 0x10423CEBU;
        const unsigned long kRadialFieldRadiusHash = 0xFF28F620U;
        const unsigned long kRadialFieldAttenuationHash = 0x1A433168U;
        const unsigned long kRadialFieldMagnitudeHash = 0x0064B3B9U;
        const unsigned long kRadialFieldApproachHash = 0xF3CF5EA5U;
        const unsigned long kTurbulenceFieldMagnitudeHash = 0x1CEAC6D1U;
        const unsigned long kTurbulenceFieldApproachHash = 0x1B970B9AU;
        const unsigned long kCollideFieldReflectivityHash = 0xF2468210U;
        const unsigned long kCollideFieldWidthHash = 0x138BDB51U;
        const unsigned long kCollideFieldHeightHash = 0x11336C98U;
        const unsigned long kSphereEmitterMaxRadiusHash = 0x1874DE74U;

        // The field sets' property tables (.data 0x06256240 gravity, 0x06255af4 air,
        // 0x062569c8 radial, 0x06256ccc turbulence, 0x06255d78 collide, 0x06255aa8 dust),
        // each enumerated after the base rows.
        const FNodePropertyRow kGravityPropertyRows[] = {
            {"GravityField_Gravity", kGravityFieldGravityHash, kFxPropAnimatedSingleRamp, 0.0f, kNodePropertyRowMax},
            {nullptr, 0, 0, 0.0f, 1.0f},
        };
        const FNodePropertyRow kAirPropertyRows[] = {
            {"AirField_Magnitude", kAirFieldMagnitudeHash, kFxPropAnimatedSingleRamp, -kNodePropertyRowMax, kNodePropertyRowMax},
            {"AirField_Approach", kAirFieldApproachHash, kFxPropAnimatedSingleRamp, 0.0f, 2.0f},
            {nullptr, 0, 0, 0.0f, 1.0f},
        };
        const FNodePropertyRow kRadialPropertyRows[] = {
            {"RadialField_Radius", kRadialFieldRadiusHash, kFxPropAnimatedSingleRamp, 0.0f, kNodePropertyRowMax},
            {"RadialField_Attenuation", kRadialFieldAttenuationHash, kFxPropSingleRampRamp, -1.0f, 1.0f},
            {"RadialField_Magnitude", kRadialFieldMagnitudeHash, kFxPropAnimatedSingleRamp, -kNodePropertyRowMax, kNodePropertyRowMax},
            {"RadialField_Approach", kRadialFieldApproachHash, kFxPropAnimatedSingleRamp, 0.0f, 10.0f},
            {nullptr, 0, 0, 0.0f, 1.0f},
        };
        const FNodePropertyRow kTurbulencePropertyRows[] = {
            {"TurbulenceField_Magnitude", kTurbulenceFieldMagnitudeHash, kFxPropAnimatedSingleRamp, -kNodePropertyRowMax, kNodePropertyRowMax},
            {"TurbulenceField_Approach", kTurbulenceFieldApproachHash, kFxPropAnimatedSingleRamp, 0.0f, 2.0f},
            {nullptr, 0, 0, 0.0f, 1.0f},
        };
        const FNodePropertyRow kCollidePropertyRows[] = {
            {"CollideField_Reflectivity", kCollideFieldReflectivityHash, kFxPropAnimatedSingleRamp, 0.0f, 1.0f},
            {"CollideField_Width", kCollideFieldWidthHash, kFxPropAnimatedSingleRamp, 0.0f, kNodePropertyRowMax},
            {"CollideField_Height", kCollideFieldHeightHash, kFxPropAnimatedSingleRamp, 0.0f, kNodePropertyRowMax},
            {nullptr, 0, 0, 0.0f, 1.0f},
        };
        const FNodePropertyRow kDustFieldPropertyRows[] = {
            {"SphereEmitter_MaxRadius", kSphereEmitterMaxRadiusHash, kFxPropAnimatedSingleRamp, 60.1f, kNodePropertyRowMax},
            {nullptr, 0, 0, 0.0f, 1.0f},
        };

        // A ramp getter's hit: *Out = the ramp, AddRef, 0.
        GENRESULT HandOutFieldRamp(INestedSingleRamp* Ramp, INestedSingleRamp** Out)
        {
            *Out = Ramp;
            Ramp->AddRef();
            return GR_OK;
        }

        // Collide's owned members unwind in reverse construction order (0625146c),
        // and its destructor advances the EH state before each Release (06251564).
        struct FCollideRampCleanup
        {
            INestedSingleRamp** Slots[4];
            unsigned long Remaining;

            ~FCollideRampCleanup() noexcept(false) { ReleaseRemaining(); }

            void ReleaseRemaining()
            {
                while (Remaining != 0)
                {
                    --Remaining;
                    ReleaseDescriptorSlot(*Slots[Remaining]);
                }
            }
        };

        void MakeCollideAnimatedSingleRamp(float Weight, INestedSingleRamp** Out)
        {
            FAnimatedSingleRamp* const Ramp = new FAnimatedSingleRamp();
            struct FChildReference
            {
                FFxAnimatedSingle* Child;
                ~FChildReference() noexcept(false)
                {
                    if (Child != nullptr)
                        Child->Release();
                }
            };
            FChildReference ChildReference = {nullptr};
            FFxAnimatedSingle* const Child = new FFxAnimatedSingle();
            Child->SetWeight(Weight);
            ChildReference.Child = Child;
            Ramp->AddKey(0.0f, Child);
            *Out = Ramp;
        }
    } // namespace

    // ---- Field property sets ------------------------------------------------

    // Gravity create FUN_06223340: +0x4c a one-key FxAnimatedSingleRamp of 9.8 (0x411ccccd).
    FFxGravityFieldPropertySet::FFxGravityFieldPropertySet() : GravityRamp(nullptr)
    {
        FCollideRampCleanup Cleanup = {{&GravityRamp, nullptr, nullptr, nullptr}, 1};
        MakeAnimatedSingleRamp(kDefaultGravity, &GravityRamp);
        Cleanup.Remaining = 0;
    }

    FFxGravityFieldPropertySet::~FFxGravityFieldPropertySet()
    {
        ReleaseDescriptorSlot(GravityRamp);
    }

    // Air create FUN_06214010: +0x4c and +0x50 one-key FxAnimatedSingleRamps.
    FFxAirFieldPropertySet::FFxAirFieldPropertySet() : ApproachRamp(nullptr), MagnitudeRamp(nullptr)
    {
        FCollideRampCleanup Cleanup = {{&ApproachRamp, &MagnitudeRamp, nullptr, nullptr}, 2};
        MakeAnimatedSingleRamp(kDefaultAirApproach, &ApproachRamp);
        MakeAnimatedSingleRamp(kDefaultAirMagnitude, &MagnitudeRamp);
        Cleanup.Remaining = 0;
    }

    FFxAirFieldPropertySet::~FFxAirFieldPropertySet()
    {
        ReleaseDescriptorSlot(ApproachRamp);
        ReleaseDescriptorSlot(MagnitudeRamp);
    }

    // FUN_06235f50: +0x4c / +0x50 / +0x54 one-key FxAnimatedSingleRamps; +0x58 an
    // FxRampSingleRamp whose child ramp runs 1.0 -> 0.001 in mode 3 (ease-out).
    FFxRadialFieldPropertySet::FFxRadialFieldPropertySet()
        : MagnitudeRamp(nullptr), ApproachRamp(nullptr), RadiusRamp(nullptr), FalloffRamp(nullptr)
    {
        FCollideRampCleanup Cleanup = {{&MagnitudeRamp, &ApproachRamp, &RadiusRamp, &FalloffRamp}, 4};
        MakeAnimatedSingleRamp(kDefaultRadialMagnitude, &MagnitudeRamp);
        MakeAnimatedSingleRamp(kDefaultRadialApproach, &ApproachRamp);
        MakeAnimatedSingleRamp(kDefaultRadialRadius, &RadiusRamp);
        struct FChildReference
        {
            IDAComponent* Child;
            ~FChildReference() noexcept(false)
            {
                if (Child != nullptr)
                    Child->Release();
            }
        };
        {
            FRampSingleRamp* const Ramp = new FRampSingleRamp();
            FChildReference ChildReference = {nullptr};
            IRampSingle* Child = nullptr;
            MakeRampSingle(1.0f, &Child);
            ChildReference.Child = Child;
            Ramp->AddKey(0.0f, Child);
            FalloffRamp = Ramp;
        }
        {
            FChildReference ChildReference = {nullptr};
            FalloffRamp->GetKeyValue(0, &ChildReference.Child);
            IRampSingle* const Child = static_cast<IRampSingle*>(ChildReference.Child);
            const float EndValue = 0.001f;
            Child->AddKey(1.0f, &EndValue);
            Child->SetMode(3);
        }
        Cleanup.Remaining = 0;
    }

    // FUN_062373c0.
    FFxRadialFieldPropertySet::~FFxRadialFieldPropertySet()
    {
        ReleaseDescriptorSlot(MagnitudeRamp);
        ReleaseDescriptorSlot(ApproachRamp);
        ReleaseDescriptorSlot(RadiusRamp);
        ReleaseDescriptorSlot(FalloffRamp);
    }

    // Turbulence create FUN_0623d4b0: +0x4c and +0x50 one-key FxAnimatedSingleRamps.
    FFxTurbulenceFieldPropertySet::FFxTurbulenceFieldPropertySet()
        : ApproachRamp(nullptr), MagnitudeRamp(nullptr)
    {
        FCollideRampCleanup Cleanup = {{&ApproachRamp, &MagnitudeRamp, nullptr}, 2};
        MakeAnimatedSingleRamp(kDefaultTurbulenceApproach, &ApproachRamp);
        MakeAnimatedSingleRamp(kDefaultTurbulenceMagnitude, &MagnitudeRamp);
        Cleanup.Remaining = 0;
    }

    FFxTurbulenceFieldPropertySet::~FFxTurbulenceFieldPropertySet() noexcept(false)
    {
        FCollideRampCleanup Cleanup = {{&ApproachRamp, &MagnitudeRamp, nullptr}, 2};
        Cleanup.ReleaseRemaining();
    }

    // FUN_06219a30: +0x4c / +0x50 / +0x54 one-key FxAnimatedSingleRamps.
    FFxCollideFieldPropertySet::FFxCollideFieldPropertySet()
        : ReflectRamp(nullptr), WidthRamp(nullptr), HeightRamp(nullptr)
    {
        FCollideRampCleanup Cleanup = {{&ReflectRamp, &WidthRamp, &HeightRamp}, 3};
        MakeCollideAnimatedSingleRamp(kDefaultCollideReflect, &ReflectRamp);
        MakeCollideAnimatedSingleRamp(kDefaultCollideWidth, &WidthRamp);
        MakeCollideAnimatedSingleRamp(kDefaultCollideHeight, &HeightRamp);
        Cleanup.Remaining = 0;
    }

    FFxCollideFieldPropertySet::~FFxCollideFieldPropertySet() noexcept(false)
    {
        FCollideRampCleanup Cleanup = {{&ReflectRamp, &WidthRamp, &HeightRamp}, 3};
        Cleanup.ReleaseRemaining();
    }

    // FUN_06212810: +0x4c a one-key FxAnimatedSingleRamp of 60.1 (0x42706666).
    FFxDustFieldPropertySet::FFxDustFieldPropertySet() : RadiusRamp(nullptr)
    {
        FCollideRampCleanup Cleanup = {{&RadiusRamp, nullptr, nullptr}, 1};
        MakeCollideAnimatedSingleRamp(60.1f, &RadiusRamp);
        Cleanup.Remaining = 0;
    }

    FFxDustFieldPropertySet::~FFxDustFieldPropertySet()
    {
        ReleaseDescriptorSlot(RadiusRamp);
    }

    // ---- FxGravityField -------------------------------------------------

    FxGravityField::FxGravityField(const char* PrimaryIid, const char* FactoryName)
        : FGenericFxObjectBase(PrimaryIid, FactoryName), GravityPropertySet_(nullptr), Gravity_(0.0f)
    {
        Cache_[0] = 0.0f;
        Cache_[1] = 0.0f;
        Cache_[2] = 0.0f;
    }

    FxGravityField::~FxGravityField()
    {
    }

    // FUN_06223340 (node slot +0x48).
    void FxGravityField::CreatePropertySet()
    {
        FFxGravityFieldPropertySet* const Set = new FFxGravityFieldPropertySet();
        GravityPropertySet_ = Set;
        AdoptPropertySet(Set);
    }

    // FUN_062234e0: the gravity constructor inline, FUN_06244f90 (FUN_0623fc30 and the
    // +0x98 set cache), then the +0x9c set cache. The borrowed cache is copied after base
    // initialization.
    GENRESULT DACOM_API FxGravityField::CloneNode(IDAComponent** Out)
    {
        FxGravityField* const Node = new FxGravityField(PrimaryIid_, FactoryName_);
        Node->InitNodeFromArchetype(this);
        Node->GravityPropertySet_ = GravityPropertySet_;
        *Out = Node;
        return GR_OK;
    }

    // Set getter LAB_06223310.
    GENRESULT DACOM_API FFxGravityFieldPropertySet::GetPropAnimatedSingleRamp(
        unsigned long Hash, INestedSingleRamp** Out)
    {
        if (Hash != kGravityFieldGravityHash)
            return GR_ALE_CREATE_FAIL;
        return HandOutFieldRamp(GravityRamp, Out);
    }

    // LAB_06223d70: the base rows, then .data 0x06256240.
    int DACOM_API FFxGravityFieldPropertySet::EnumerateProps(FNodePropertyRowCallback Callback,
                                                             void* Context)
    {
        const int Result = FFxNodePropertySet::EnumerateProps(Callback, Context);
        if (Result != 0)
            return Result;
        return WalkPropertyRows(kGravityPropertyRows, Callback, Context);
    }

    // LAB_06223dd0 (outer +0x14): node+0xa0 = Gravity * Dt.
    GENRESULT DACOM_API FxGravityField::UpdateNode(float Dt)
    {
        TickLifetime(Dt);
        Gravity_ = static_cast<float>(
            GravityProps()->GravityRamp->Eval(AppearanceOuterTime_, Age_) * Dt);
        return GR_OK;
    }

    void DACOM_API FxGravityField::BeginField(IDAComponent* Appearance, float /*Dt*/)
    {
        // LAB_062241a0: the down vector in the appearance's space, cached at node+0xa4.
        Transform Xform;
        ComputeRelativeTransform(Appearance, &Xform);
        const Vector Source = { 0.0f, -Gravity_, 0.0f };
        I3DMathEngine* Math = Shared3DMath();
        Vector Result;
        Math->Rotate(Result, Xform, Source);
        std::memcpy(Cache_, &Result, sizeof(Result));
    }

    // LAB_06224160.
    void DACOM_API FxGravityField::ApplyFieldToVel(float* PosVel6, float* /*Age*/, float /*Dt*/)
    {
        float* Vel = PosVel6 + 3;
        Vel[0] += Cache_[0];
        Vel[1] += Cache_[1];
        Vel[2] += Cache_[2];
    }

    // ---- FxAirField -------------------------------------------------------

    FxAirField::FxAirField(const char* PrimaryIid, const char* FactoryName)
        : FGenericFxObjectBase(PrimaryIid, FactoryName), AirPropertySet_(nullptr),
          Approach_(0.0f), Magnitude_(0.0f),
          Blend_(0.0f)
    {
        Cache_[0] = 0.0f;
        Cache_[1] = 0.0f;
        Cache_[2] = 0.0f;
    }

    FxAirField::~FxAirField()
    {
    }

    // FUN_06214010 (node slot +0x48).
    void FxAirField::CreatePropertySet()
    {
        FFxAirFieldPropertySet* const Set = new FFxAirFieldPropertySet();
        AirPropertySet_ = Set;
        AdoptPropertySet(Set);
    }

    // FUN_062141c0: as gravity's, plus -- the only clone that copies node state -- the
    // Begin-time Cache_ (+0xac..+0xb4).
    GENRESULT DACOM_API FxAirField::CloneNode(IDAComponent** Out)
    {
        FxAirField* const Node = new FxAirField(PrimaryIid_, FactoryName_);
        Node->InitNodeFromArchetype(this);
        Node->AirPropertySet_ = AirPropertySet_;
        std::memcpy(Node->Cache_, Cache_, sizeof(Cache_));
        *Out = Node;
        return GR_OK;
    }

    // Set getter LAB_06213fd0: 0x10423CEB AirField_Approach, 0xE5E3524C AirField_Magnitude.
    GENRESULT DACOM_API FFxAirFieldPropertySet::GetPropAnimatedSingleRamp(unsigned long Hash,
                                                                          INestedSingleRamp** Out)
    {
        if (Hash == kAirFieldApproachHash)
            return HandOutFieldRamp(ApproachRamp, Out);
        if (Hash == kAirFieldMagnitudeHash)
            return HandOutFieldRamp(MagnitudeRamp, Out);
        return GR_ALE_CREATE_FAIL;
    }

    // LAB_06214750: the base rows, then .data 0x06255af4.
    int DACOM_API FFxAirFieldPropertySet::EnumerateProps(FNodePropertyRowCallback Callback,
                                                         void* Context)
    {
        const int Result = FFxNodePropertySet::EnumerateProps(Callback, Context);
        if (Result != 0)
            return Result;
        return WalkPropertyRows(kAirPropertyRows, Callback, Context);
    }

    // LAB_062147b0 (outer +0x14): Blend (4.0 = DAT_0624ca50) is kept when <= 1 or unordered.
    GENRESULT DACOM_API FxAirField::UpdateNode(float Dt)
    {
        TickLifetime(Dt);
        FFxAirFieldPropertySet* const Set = AirProps();
        Approach_ = static_cast<float>(Set->ApproachRamp->Eval(AppearanceOuterTime_, Age_));
        Magnitude_ = static_cast<float>(Set->MagnitudeRamp->Eval(AppearanceOuterTime_, Age_));
        Blend_ = Dt * Approach_ * 4.0f;
        if (Blend_ > 1.0f)
            Blend_ = 1.0f;
        return GR_OK;
    }

    void DACOM_API FxAirField::BeginField(IDAComponent* Appearance, float /*Dt*/)
    {
        // LAB_06214bf0: as Gravity, with (0, Magnitude, 0) cached at node+0xac.
        Transform Xform;
        ComputeRelativeTransform(Appearance, &Xform);
        const Vector Source = { 0.0f, Magnitude_, 0.0f };
        I3DMathEngine* Math = Shared3DMath();
        Vector Result;
        Math->Rotate(Result, Xform, Source);
        std::memcpy(Cache_, &Result, sizeof(Result));
    }

    // LAB_06214b80.
    void DACOM_API FxAirField::ApplyFieldToVel(float* PosVel6, float* /*Age*/, float /*Dt*/)
    {
        float* Vel = PosVel6 + 3;
        LerpVel3(Vel, Cache_, Blend_);
    }

    // ---- FxRadialField ------------------------------------------------------

    FxRadialField::FxRadialField(const char* PrimaryIid, const char* FactoryName)
        : FGenericFxObjectBase(PrimaryIid, FactoryName), RadialPropertySet_(nullptr),
          Magnitude_(0.0f), Approach_(0.0f),
          Blend_(0.0f), DistScale_(0.0f)
    {
        Cache_[0] = 0.0f;
        Cache_[1] = 0.0f;
        Cache_[2] = 0.0f;
    }

    FxRadialField::~FxRadialField()
    {
    }

    // FUN_06236280 (node slot +0x48): operator_new(0x64).
    void FxRadialField::CreatePropertySet()
    {
        FFxRadialFieldPropertySet* const Set = new FFxRadialFieldPropertySet();
        AdoptPropertySet(Set);
        RadialPropertySet_ = Set;
    }

    // FUN_06236360: as gravity's.
    GENRESULT DACOM_API FxRadialField::CloneNode(IDAComponent** Out)
    {
        FxRadialField* const Node = new FxRadialField(PrimaryIid_, FactoryName_);
        Node->InitNodeFromArchetype(this);
        Node->RadialPropertySet_ = RadialPropertySet_;
        *Out = Node;
        return GR_OK;
    }

    // Set getter +0x3c LAB_06236250 (kind 0x200).
    GENRESULT DACOM_API FFxRadialFieldPropertySet::GetPropSingleRampRamp(unsigned long Hash,
                                                                         INestedSingleRamp** Out)
    {
        if (Hash != kRadialFieldAttenuationHash)
            return GR_ALE_CREATE_FAIL;
        return HandOutFieldRamp(FalloffRamp, Out);
    }

    // Set getter +0x40 LAB_06236200 (kind 0x202).
    GENRESULT DACOM_API FFxRadialFieldPropertySet::GetPropAnimatedSingleRamp(
        unsigned long Hash, INestedSingleRamp** Out)
    {
        if (Hash == kRadialFieldRadiusHash)
            return HandOutFieldRamp(RadiusRamp, Out);
        if (Hash == kRadialFieldMagnitudeHash)
            return HandOutFieldRamp(MagnitudeRamp, Out);
        if (Hash == kRadialFieldApproachHash)
            return HandOutFieldRamp(ApproachRamp, Out);
        return GR_ALE_CREATE_FAIL;
    }

    // LAB_062369d0: the base rows, then .data 0x062569c8.
    int DACOM_API FFxRadialFieldPropertySet::EnumerateProps(FNodePropertyRowCallback Callback,
                                                            void* Context)
    {
        const int Result = FFxNodePropertySet::EnumerateProps(Callback, Context);
        if (Result != 0)
            return Result;
        return WalkPropertyRows(kRadialPropertyRows, Callback, Context);
    }

    // LAB_06236450: the radial field's own per-node update (outer +0x14; the other fields
    // use the shared FUN_06240180; the inner vtable reaches it through LAB_06236a30).
    // An ordered Radius < 0.001 (double DAT_0624d168) becomes float 0.001 (DAT_0624ee18)
    // before 1 / R. Retail stores the magnitude negated (FCHS); the port negates at its one
    // use in ApplyFieldToVel (exact). Retail also stores Dt at node+0xb0, which nothing
    // reads (not kept).
    GENRESULT DACOM_API FxRadialField::UpdateNode(float Dt)
    {
        TickLifetime(Dt);
        FFxRadialFieldPropertySet* const Set = RadialProps(); // node+0x9c
        const double Radius = Set->RadiusRamp->Eval(AppearanceOuterTime_, Age_);
        if (Radius < 0.001)
            DistScale_ = static_cast<float>(1.0 / static_cast<double>(0.001f));
        else
            DistScale_ = static_cast<float>(1.0 / Radius);
        Approach_ = static_cast<float>(Set->ApproachRamp->Eval(AppearanceOuterTime_, Age_));
        Magnitude_ = static_cast<float>(Set->MagnitudeRamp->Eval(AppearanceOuterTime_, Age_));
        Blend_ = Dt * Approach_ * 4.0f;
        if (Blend_ > 1.0f)
            Blend_ = 1.0f;
        return GR_OK;
    }

    // LAB_06236ea0: the field origin in the appearance's space, cached at node+0xb4.
    void DACOM_API FxRadialField::BeginField(IDAComponent* Appearance, float /*Dt*/)
    {
        Transform Xform = {
            Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f),
            {0.0f, 0.0f, 0.0f}
        };
        ComputeRelativeTransform(Appearance, &Xform);
        const Vector Origin = { 0.0f, 0.0f, 0.0f };
        I3DMathEngine* Math = Shared3DMath();
        Vector TransformedOrigin;
        Math->TransformXform(TransformedOrigin,
            Xform, Origin);
        std::memcpy(Cache_, &TransformedOrigin, sizeof(TransformedOrigin));
    }

    void DACOM_API FxRadialField::ApplyFieldToVel(float* PosVel6, float* /*Age*/, float /*Dt*/)
    {
        float* Vel = PosVel6 + 3;
        // dx, dy are stored as float; dz is stored too, but the unrounded difference stays
        // in ST0 and multiplies the stored float.
        const float Dx = Cache_[0] - PosVel6[0];
        const float Dy = Cache_[1] - PosVel6[1];
        const double DzExt = static_cast<double>(Cache_[2]) - PosVel6[2];
        const float Dz = static_cast<float>(DzExt);
        // Summed in the order z, x, y, all in the FPU.
        const double DistExt =
            std::sqrt(DzExt * Dz + static_cast<double>(Dx) * Dx + static_cast<double>(Dy) * Dy) *
            DistScale_;
        // Kept when <= 1 or unordered.
        float Dist = static_cast<float>(DistExt);
        if (DistExt > 1.0)
            Dist = 1.0f;
        // The falloff ramp is not null-checked.
        const double Scale =
            RadialProps()->FalloffRamp->Eval(AppearanceOuterTime_, Dist) * -Magnitude_;
        // Tx stays in ST0; Ty and Tz are stored as float.
        const double TargetX = static_cast<double>(Dx) * Scale;
        const float TargetY = static_cast<float>(static_cast<double>(Dy) * Scale);
        const float TargetZ = static_cast<float>(static_cast<double>(Dz) * Scale);
        // Retail's node+0xa8: min(Dt * Approach * 4.0f, 1.0f) from the update (Blend_).
        LerpVel3(Vel, TargetX, TargetY, TargetZ, Blend_);
    }

    // ---- FxTurbulenceField --------------------------------------------------

    FxTurbulenceField::FxTurbulenceField(const char* PrimaryIid, const char* FactoryName)
        : FGenericFxObjectBase(PrimaryIid, FactoryName), TurbulencePropertySet_(nullptr),
          Approach_(0.0f), Blend_(0.0f),
          Magnitude_(0.0f)
    {
    }

    FxTurbulenceField::~FxTurbulenceField()
    {
    }

    // FUN_0623d4b0 (node slot +0x48).
    void FxTurbulenceField::CreatePropertySet()
    {
        FFxTurbulenceFieldPropertySet* const Set = new FFxTurbulenceFieldPropertySet();
        TurbulencePropertySet_ = Set;
        AdoptPropertySet(Set);
    }

    // FUN_0623d640: as gravity's.
    GENRESULT DACOM_API FxTurbulenceField::CloneNode(IDAComponent** Out)
    {
        FxTurbulenceField* const Node = new FxTurbulenceField(PrimaryIid_, FactoryName_);
        Node->InitNodeFromArchetype(this);
        Node->TurbulencePropertySet_ = TurbulencePropertySet_;
        *Out = Node;
        return GR_OK;
    }

    // Set getter LAB_0623d470: 0x1B970B9A TurbulenceField_Approach, 0x1CEAC6D1
    // TurbulenceField_Magnitude.
    GENRESULT DACOM_API FFxTurbulenceFieldPropertySet::GetPropAnimatedSingleRamp(
        unsigned long Hash, INestedSingleRamp** Out)
    {
        if (Hash == kTurbulenceFieldApproachHash)
            return HandOutFieldRamp(ApproachRamp, Out);
        if (Hash == kTurbulenceFieldMagnitudeHash)
            return HandOutFieldRamp(MagnitudeRamp, Out);
        return GR_ALE_CREATE_FAIL;
    }

    // LAB_0623df40: the base rows, then .data 0x06256ccc.
    int DACOM_API FFxTurbulenceFieldPropertySet::EnumerateProps(FNodePropertyRowCallback Callback,
                                                                void* Context)
    {
        const int Result = FFxNodePropertySet::EnumerateProps(Callback, Context);
        if (Result != 0)
            return Result;
        return WalkPropertyRows(kTurbulencePropertyRows, Callback, Context);
    }

    // LAB_0623dfa0 (outer +0x14): Blend (4.0 = DAT_0624ca50) is computed in double and
    // kept when <= 1 or unordered.
    GENRESULT DACOM_API FxTurbulenceField::UpdateNode(float Dt)
    {
        TickLifetime(Dt);
        FFxTurbulenceFieldPropertySet* const Set = TurbulenceProps();
        const double Approach = Set->ApproachRamp->Eval(AppearanceOuterTime_, Age_);
        FFxTurbulenceFieldPropertySet* const MagnitudeSet = TurbulenceProps();
        Approach_ = static_cast<float>(Approach);
        Magnitude_ = static_cast<float>(MagnitudeSet->MagnitudeRamp->Eval(AppearanceOuterTime_, Age_));
        const double Blend = static_cast<double>(Dt) * Approach_ * 4.0;
        Blend_ = static_cast<float>(Blend);
        if (Blend > 1.0)
            Blend_ = 1.0f;
        return GR_OK;
    }

    // LAB_0623e4a0: no-op.
    void DACOM_API FxTurbulenceField::BeginField(IDAComponent* /*Appearance*/, float /*Dt*/)
    {
    }

    // LAB_0623e370: a random unit direction (fast inverse square root, DAT_0624bab8 = 0.5,
    // DAT_0624bac0 = 1.5) -- the second draw is x, the first is y -- scaled by
    // rand * Magnitude and blended into the velocity. No zero-length guard.
    void DACOM_API FxTurbulenceField::ApplyFieldToVel(float* PosVel6, float* /*Age*/, float /*Dt*/)
    {
        float* Vel = PosVel6 + 3;
        // FUN_0623f1f0 leaves its product in ST0; R1/R2 are stored as float and R3 stays in
        // the FPU.
        const float R1 = static_cast<float>((AlchemyRand01() - 0.5) * 2.0);
        const float R2 = static_cast<float>((AlchemyRand01() - 0.5) * 2.0);
        const double R3 = (AlchemyRand01() - 0.5) * 2.0;
        const float LengthSq = static_cast<float>(R3 * R3 + static_cast<double>(R2) * R2 +
                                                  static_cast<double>(R1) * R1);
        long Bits = 0;
        std::memcpy(&Bits, &LengthSq, sizeof(Bits));
        const long GuessBits = 0x5f3759dfL - (Bits >> 1);
        float Guess = 0.0f;
        std::memcpy(&Guess, &GuessBits, sizeof(Guess));
        const double Scale =
            (1.5 - static_cast<double>(Guess) * Guess * LengthSq * 0.5) * Guess;
        float Dir[3];
        Dir[0] = static_cast<float>(R2 * Scale);
        Dir[1] = static_cast<float>(R1 * Scale);
        Dir[2] = static_cast<float>(R3 * Scale);
        // The push and the x product stay in the FPU; y and z are stored as float.
        const double Push = AlchemyRand01() * Magnitude_;
        const double TargetX = static_cast<double>(Dir[0]) * Push;
        const float TargetY = static_cast<float>(static_cast<double>(Dir[1]) * Push);
        const float TargetZ = static_cast<float>(static_cast<double>(Dir[2]) * Push);
        LerpVel3(Vel, TargetX, TargetY, TargetZ, Blend_);
    }

    // ---- FxCollideField -----------------------------------------------------

    FxCollideField::FxCollideField(const char* PrimaryIid, const char* FactoryName)
        : FGenericFxObjectBase(PrimaryIid, FactoryName), CollidePropertySet_(nullptr),
          Reflect_(0.0f), UpdateDt_(0.0f)
    {
        unsigned long Index = 0;
        for (Index = 0; Index < 3; ++Index)
        {
            Normal_[Index] = 0.0f;
            Corner_[Index] = 0.0f;
            ScaledNormal_[Index] = 0.0f;
        }
        float* const BeginWords = reinterpret_cast<float*>(&BeginXform_);
        for (Index = 0; Index < 12; ++Index)
            BeginWords[Index] = 0.0f;
        // Retail calls the 3x3 identity constructor, then clears translation.
        BeginXform_.Rotation.E[0][0] = BeginXform_.Rotation.E[1][1] = BeginXform_.Rotation.E[2][2] = 1.0f;
    }

    FxCollideField::~FxCollideField()
    {
    }

    // FUN_06219d30 (node slot +0x48): operator_new(0x60).
    void FxCollideField::CreatePropertySet()
    {
        FFxCollideFieldPropertySet* const Set = new FFxCollideFieldPropertySet();
        CollidePropertySet_ = Set;
        AdoptPropertySet(Set);
    }

    // FUN_06219e10: as gravity's.
    GENRESULT DACOM_API FxCollideField::CloneNode(IDAComponent** Out)
    {
        FxCollideField* const Node = new FxCollideField(PrimaryIid_, FactoryName_);
        Node->InitNodeFromArchetype(this);
        // Retail reloads the source's non-owning cache after the helper returns.
        Node->CollidePropertySet_ = CollidePropertySet_;
        *Out = Node;
        return GR_OK;
    }

    // FUN_0620e510: the constructor inline and FUN_06244f90, nothing else.
    GENRESULT DACOM_API FLBeamField::CloneNode(IDAComponent** Out)
    {
        FLBeamField* const Node = new (std::nothrow) FLBeamField(PrimaryIid_, FactoryName_);
        Node->InitNodeFromArchetype(this);
        *Out = Node;
        return GR_OK;
    }

    // Set getter LAB_06219cb0 (also reached by the +4 thunk 0x0621a6f0); two hashes per
    // ramp.
    GENRESULT DACOM_API FFxCollideFieldPropertySet::GetPropAnimatedSingleRamp(
        unsigned long Hash, INestedSingleRamp** Out)
    {
        if (Hash == kCollideFieldReflectivityHash || Hash == 0xE462B789u)
            return HandOutFieldRamp(ReflectRamp, Out);
        if (Hash == kCollideFieldWidthHash || Hash == 0x061AAF28u)
            return HandOutFieldRamp(WidthRamp, Out);
        if (Hash == kCollideFieldHeightHash || Hash == 0x16FF3474u)
            return HandOutFieldRamp(HeightRamp, Out);
        return GR_ALE_CREATE_FAIL;
    }

    // LAB_0621a720: the base rows, then .data 0x06255d78.
    int DACOM_API FFxCollideFieldPropertySet::EnumerateProps(FNodePropertyRowCallback Callback,
                                                             void* Context)
    {
        const int Result = FFxNodePropertySet::EnumerateProps(Callback, Context);
        if (Result != 0)
            return Result;
        return WalkPropertyRows(kCollidePropertyRows, Callback, Context);
    }

    // LAB_0621a780 (outer +0x14): node+0xa0 Reflect, node+0xa4 Dt.
    GENRESULT DACOM_API FxCollideField::UpdateNode(float Dt)
    {
        TickLifetime(Dt);
        Reflect_ = static_cast<float>(CollideProps()->ReflectRamp->Eval(AppearanceOuterTime_, Age_));
        UpdateDt_ = Dt;
        return GR_OK;
    }

    void DACOM_API FxCollideField::BeginField(IDAComponent* Appearance, float /*Dt*/)
    {
        // LAB_0621ab20: the field in the appearance's space (node+0xcc); the plane normal
        // (node+0xa8), corner (node+0xb4) and scaled normal (node+0xc0) follow.
        ComputeRelativeTransform(Appearance, &BeginXform_);
        const Vector NormalSource = { 0.0f, 1.0f, 0.0f };
        I3DMathEngine* Math = Shared3DMath();
        Vector RotatedNormal;
        Math->Rotate(RotatedNormal, BeginXform_,
                     NormalSource);
        std::memcpy(Normal_, &RotatedNormal, sizeof(RotatedNormal));
        // The length stays in the FPU. No zero-length guard.
        const double LengthSq = static_cast<double>(Normal_[0]) * Normal_[0] +
                                static_cast<double>(Normal_[1]) * Normal_[1] +
                                static_cast<double>(Normal_[2]) * Normal_[2];
        const double InvLength = 1.0 / ComputeAlchemyFpuSquareRoot(LengthSq);
        StoreAlchemyFpuFloat(Normal_[0], InvLength * Normal_[0]);
        StoreAlchemyFpuFloat(Normal_[1], InvLength * Normal_[1]);
        StoreAlchemyFpuFloat(Normal_[2], InvLength * Normal_[2]);
        const Vector CornerSource = { 1.0f, 0.0f, 1.0f };
        Math = Shared3DMath();
        Vector TransformedCorner;
        Math->TransformXform(TransformedCorner, BeginXform_,
                             CornerSource);
        std::memcpy(Corner_, &TransformedCorner, sizeof(TransformedCorner));
        ScaledNormal_[0] = Normal_[0] * kCollideNormalScale;
        ScaledNormal_[1] = Normal_[1] * kCollideNormalScale;
        ScaledNormal_[2] = Normal_[2] * kCollideNormalScale;
    }

    void DACOM_API FxCollideField::ApplyFieldToVel(float* PosVel6, float* /*Age*/, float /*Dt*/)
    {
        // LAB_0621a0e0. Every 3-vector temporary goes through FUN_06206be0 / FUN_06206c10 /
        // FUN_06206ab0 (float per component); dot products and scalars stay in the FPU
        // (double here) and are summed in z, y, x order.
        float* Pos = PosVel6;
        float* Vel = PosVel6 + 3;
        const float* N = Normal_;

        const float T0[3] = { Pos[0] + ScaledNormal_[0], Pos[1] + ScaledNormal_[1],
                              Pos[2] + ScaledNormal_[2] };
        const float T1[3] = { T0[0] - Corner_[0], T0[1] - Corner_[1], T0[2] - Corner_[2] };
        // An ordered D0 < 0 exits; equal and unordered go on.
        const double D0Ext = (static_cast<double>(T1[2]) * N[2] + static_cast<double>(T1[1]) * N[1]) +
                             static_cast<double>(T1[0]) * N[0];
        if (D0Ext < 0.0)
            return;
        const float D0 = static_cast<float>(D0Ext);

        const float S = UpdateDt_;
        const float V1[3] = { S * Vel[0], S * Vel[1], S * Vel[2] };
        const double VnExt = (static_cast<double>(V1[2]) * N[2] + static_cast<double>(V1[1]) * N[1]) +
                             static_cast<double>(V1[0]) * N[0];
        if (VnExt < 0.0)
            return;

        const float V2[3] = { V1[0] - ScaledNormal_[0], V1[1] - ScaledNormal_[1],
                              V1[2] - ScaledNormal_[2] };
        const float V3[3] = { V2[0] + T0[0], V2[1] + T0[1], V2[2] + T0[2] };
        const float V4[3] = { V3[0] - Corner_[0], V3[1] - Corner_[1], V3[2] - Corner_[2] };
        // <= 0 or unordered goes on.
        const double D1 = (static_cast<double>(V4[2]) * N[2] + static_cast<double>(V4[1]) * N[1]) +
                          static_cast<double>(V4[0]) * N[0];
        if (D1 > 0.0)
            return;

        // Uses the float-stored D0.
        const float T = static_cast<float>(static_cast<double>(D0) /
                                           (static_cast<double>(D0) - D1));
        const float V5[3] = { V2[0] * T, V2[1] * T, V2[2] * T };
        Pos[0] = V5[0] + T0[0];
        Pos[1] = V5[1] + T0[1];
        Pos[2] = V5[2] + T0[2];

        // Speed <= 0.001 or unordered returns with the position already written.
        const float Speed = static_cast<float>(
            std::sqrt(static_cast<double>(Vel[0]) * Vel[0] + static_cast<double>(Vel[1]) * Vel[1] +
                      static_cast<double>(Vel[2]) * Vel[2]));
        if (!(static_cast<double>(Speed) > kCollideSpeedEpsilon))
            return;

        const double InvSpeed = 1.0 / static_cast<double>(Speed);
        const float Dir[3] = { static_cast<float>(InvSpeed * Vel[0]),
                               static_cast<float>(InvSpeed * Vel[1]),
                               static_cast<float>(InvSpeed * Vel[2]) };
        // Reflection: FUN_0621a490 scales a copy of N by K; FUN_0621a460 subtracts Dir.
        const double DotDirN =
            (static_cast<double>(Dir[2]) * N[2] + static_cast<double>(Dir[1]) * N[1]) +
            static_cast<double>(Dir[0]) * N[0];
        const float K = static_cast<float>(DotDirN + DotDirN);
        float Reflected[3] = { K * N[0], K * N[1], K * N[2] };
        Reflected[0] = Reflected[0] - Dir[0];
        Reflected[1] = Reflected[1] - Dir[1];
        Reflected[2] = Reflected[2] - Dir[2];
        const double VelScale = -(static_cast<double>(Speed) * Reflect_);
        Vel[0] = static_cast<float>(VelScale * Reflected[0]);
        Vel[1] = static_cast<float>(VelScale * Reflected[1]);
        Vel[2] = static_cast<float>(VelScale * Reflected[2]);

        // The rest of the step, along the reflected velocity.
        const double Residual = (1.0 - static_cast<double>(T)) * S;
        const float W[3] = { static_cast<float>(Residual * Vel[0]),
                             static_cast<float>(Residual * Vel[1]),
                             static_cast<float>(Residual * Vel[2]) };
        Pos[0] = Pos[0] - W[0];
        Pos[1] = Pos[1] - W[1];
        Pos[2] = Pos[2] - W[2];
    }

    // ---- FLBeamField --------------------------------------------------------
    // No overrides: retail's Begin/Apply (LAB_0620ed20 / LAB_0620ed30) are no-ops.

    // ---- FLDustField --------------------------------------------------------

    FLDustField::FLDustField(const char* PrimaryIid, const char* FactoryName)
        : FGenericFxObjectBase(PrimaryIid, FactoryName), DustPropertySet_(nullptr),
          Radius_(0.0f), RadiusSq_(0.0f), UpdateDt_(0.0f)
    {
        unsigned long Index = 0;
        float* const BeginWords = reinterpret_cast<float*>(&BeginXform_);
        for (Index = 0; Index < 12; ++Index)
            BeginWords[Index] = 0.0f;
        BeginXform_.Rotation.E[0][0] = BeginXform_.Rotation.E[1][1] = BeginXform_.Rotation.E[2][2] = 1.0f;
    }

    FLDustField::~FLDustField()
    {
    }

    // FUN_06212960 (node slot +0x48): operator_new(0x58).
    void FLDustField::CreatePropertySet()
    {
        FFxDustFieldPropertySet* const Set = new FFxDustFieldPropertySet();
        DustPropertySet_ = Set;
        AdoptPropertySet(Set);
    }

    // FUN_06212b00: as gravity's.
    GENRESULT DACOM_API FLDustField::CloneNode(IDAComponent** Out)
    {
        FLDustField* const Node = new FLDustField(PrimaryIid_, FactoryName_);
        Node->InitNodeFromArchetype(this);
        Node->DustPropertySet_ = DustPropertySet_;
        *Out = Node;
        return GR_OK;
    }

    // LAB_06212930.
    GENRESULT DACOM_API FFxDustFieldPropertySet::GetPropAnimatedSingleRamp(unsigned long Hash,
                                                                           INestedSingleRamp** Out)
    {
        if (Hash != kSphereEmitterMaxRadiusHash)
            return GR_ALE_CREATE_FAIL;
        return HandOutFieldRamp(RadiusRamp, Out);
    }

    // LAB_062136f0: the base rows, then .data 0x06255aa8.
    int DACOM_API FFxDustFieldPropertySet::EnumerateProps(FNodePropertyRowCallback Callback,
                                                          void* Context)
    {
        const int Result = FFxNodePropertySet::EnumerateProps(Callback, Context);
        if (Result != 0)
            return Result;
        return WalkPropertyRows(kDustFieldPropertyRows, Callback, Context);
    }

    // LAB_06212c20: Dt (+0xd8) is stored before TickLifetime; R^2 (+0xa4) uses the
    // unrounded R.
    GENRESULT DACOM_API FLDustField::UpdateNode(float Dt)
    {
        UpdateDt_ = Dt;
        TickLifetime(Dt);
        const double Radius = DustProps()->RadiusRamp->Eval(AppearanceOuterTime_, Age_);
        Radius_ = static_cast<float>(Radius);
        RadiusSq_ = static_cast<float>(Radius * Radius);
        return GR_OK;
    }

    // LAB_06212c70: node slot 7 (FUN_0623ffb0) into node+0xa8. Apply does not read it.
    void DACOM_API FLDustField::BeginField(IDAComponent* Appearance, float /*Dt*/)
    {
        ComputeRelativeTransform(Appearance, &BeginXform_);
    }

    // LAB_06212c90 (the outer table reaches it through LAB_06213b00): the particle position
    // in the field's space; outside the radius the age becomes FLT_MAX (0x7f7fffff) and
    // retail returns -1 (<= or unordered goes on). Otherwise the age is rewritten from the
    // height and a bottom fade band (DAT_0624c370 1.0, DAT_0624c6fc 0.8, DAT_0624c6f8
    // 10.0). The DAT_062579d4 branch is never taken (no writer); G_DustAgeMinusUpdateDt
    // mirrors it.
    void DACOM_API FLDustField::ApplyFieldToVel(float* PosVel6, float* Age, float /*Dt*/)
    {
        I3DMathEngine* Math = Shared3DMath();
        Vector Local = { 0.0f, 0.0f, 0.0f };
        Math->InverseTransform(Local,
                                          NodeWorldXform_,
                                          *reinterpret_cast<const Vector*>(PosVel6)); // +0x3C
        const double DistSqExt = (static_cast<double>(Local.y) * Local.y +
                                  static_cast<double>(Local.x) * Local.x) +
                                 static_cast<double>(Local.z) * Local.z;
        float DistSq;
        StoreAlchemyFpuFloat(DistSq, DistSqExt);
        if (DistSqExt > RadiusSq_)
        {
            const unsigned long KillBits = kDustKillAgeBits;
            std::memcpy(Age, &KillBits, 4);
            return;
        }
        const double Height =
            1.0 - (static_cast<double>(Radius_) - Local.z) /
                      (static_cast<double>(Radius_) + static_cast<double>(Radius_));
        double Fade = 1.0;
        if (Local.z < 0.0f)
        {
            const float Band = RadiusSq_ * 0.8f;
            if (DistSq > Band)
                Fade = 1.0 - (static_cast<double>(DistSq) - Band) /
                                 (static_cast<double>(RadiusSq_) - Band);
        }
        const double NewAge = Height * Fade * 10.0f;
        *Age = static_cast<float>(NewAge);
        if (G_DustAgeMinusUpdateDt != 0)
            *Age = static_cast<float>(NewAge - UpdateDt_);
    }

} // namespace AlchemyImpl
