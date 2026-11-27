// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "dacom.h"
#include "render/IBatchedMaterial.h"
#include "render/IMaterial.h"
#include "render/IFLAppearance.h"
#include "render/RenderPipeline.h"
#include "render/mesh_iface.h"
#include "render/shading.h"


struct IEngine;

// Registration uses this opaque token; callers remove const explicitly.
static const void* kRegisterPriority = (void*)0x80000000;



static const char* const G_NameGlassMaterial = "GlassMaterial";
static const char* const G_NameHighGlassMaterial = "HighGlassMaterial";
static const char* const G_NameGfGlassMaterial = "GFGlassMaterial";
static const char* const G_NameNomadMaterial = "NomadMaterial";
static const char* const G_NameNomadNoBendy = "NomadMaterialNoBendy";
static const char* const G_NameBackdropMaterial = "BackdropMaterial";
static const char* const G_NameStarsphereMaterial = "StarsphereMaterial";
static const char* const G_NameHudIconMaterial = "HUDIconMaterial";
static const char* const G_NameHudAnimMaterial = "HUDAnimMaterial";
static const char* const G_NameColorizeMaterial = "ColorizeMaterial";
static const char* const G_NameCloakMaterial = "CloakMaterial";
static const char* const G_NameWireMaterial = "WireMaterial";
static const char* const G_NameWireMaterial2 = "WireMaterial2";
static const char* const G_NameExclusionZoneMaterial = "ExclusionZoneMaterial";
static const char* const G_NameEnvMapMaterial = "EnvMapMaterial";
static const char* const G_NameShadowMaterial = "ShadowMaterial";
static const char* const G_NameEngineGlowMaterial = "EngineGlowMaterial";
static const char* const G_NameVisibleCounterMaterial = "VisibleCounterMaterial";
static const char* const G_NamePlanetWaterMaterial = "PlanetWaterMaterial";
static const char* const G_NameFlAppearance = "FLAppearance";

bool FlmNameEqual(const char* Left, const char* Right);
void FlmFdump(int Flags, const char* File, int Line, const char* Kind, const char* Message);

GENRESULT FlmCreateAppearanceInstance(FAggDesc18* Desc, void** Instance);

// VisibleCounter slot 10 / IFLAppearance slot 13 (DAT_067045B4 / B8 / BC).
extern unsigned long G_VisibleCounterA;
extern unsigned long G_VisibleCounterB;
extern unsigned long G_VisibleCounterC;
void FlmVisibleCounterReset();
void FlmVisibleCounterCopy(unsigned long* Out3);

// Borrowed engine outer component cached by FLAppearance initialization.
extern IEngine* G_FlmEngineOuter;

// Look up the root-instance fade value; leave Out untouched on a miss.
bool FlmLookupIdValue(int Id, void** Out);

// FUN_067006d0: uncached factory; the Nomad and camera callers own separate lazy caches.
struct I3DMathEngine;
I3DMathEngine* FlmCreateMathEngine();

// The part every flmaterials material shares (retail 0x68 bytes, base vtable 06701428; the same
// layout as Shading's material base). The fifteen material classes derive from it, add their
// own members from +0x68, and TFlmMaterialObject<> appends the aggregation tail. A virtual a
// class does not override is the body retail's vtables share.
class FMaterialImpl : public IMaterial
{
public:
    // The nondelegating controller at the end of every material object.
    class FInner final : public IDAComponent
    {
    public:
        explicit FInner(FMaterialImpl* Owner) : RefCount(1), Owner(Owner) {}
        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
        unsigned long DACOM_API AddRef() override;
        unsigned long DACOM_API Release() override;
        unsigned long RefCount;
        FMaterialImpl* Owner;
    };

    GENRESULT DACOM_API Initialize(IDAComponent* Outer) override;                  // 066f1210
    GENRESULT DACOM_API LoadFromFileSystem(IFileSystem* FileSystem) override;             // 066f13e0
    GENRESULT DACOM_API SaveProperties(IFileSystem* FileSystem) override;                 // 066f14f0
    GENRESULT DACOM_API ApplyVerify(void* Context, void* Extra) override;          // 066f15d0
    GENRESULT DACOM_API ResolveTextures(float Delta) override;                     // LAB_066f1610
    GENRESULT DACOM_API Teardown(FMaterialSubmitContext* Context) override;                          // 066f3330
    GENRESULT DACOM_API Submit(FMaterialSubmitContext* Context, unsigned long PrimType, unsigned long Start,
                               unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                               unsigned long Flags) override;                      // LAB_066f3370
    GENRESULT DACOM_API SetName(const char* Name) override;                        // LAB_066f1670
    GENRESULT DACOM_API GetName(char* Out, unsigned long Size) override;
    GENRESULT DACOM_API CopyClassName(char* Out, unsigned long Size) override;
    GENRESULT DACOM_API GetPassCount(int* Out) override;
    GENRESULT DACOM_API GetRenderFlags(unsigned long* Out) override;               // 066f2d80
    GENRESULT DACOM_API GetParameterCount(unsigned long* OutCount) override;
    GENRESULT DACOM_API GetParameterDescriptor(unsigned long Index, void* OutDesc) override;
    GENRESULT DACOM_API GetParameterValue(unsigned long Index, void* InOutDesc) override;
    GENRESULT DACOM_API SetParameterValue(unsigned long Index, void* Value) override;
    GENRESULT __thiscall InitializeFromDescriptor(void* Arg) override;             // 066fad90
    GENRESULT __thiscall ApplyInitialRenderStates(void* Arg) override;             // 066fad90
    GENRESULT __thiscall Setup() override;                                         // 066f1da0
    bool __thiscall ValueWithinDeclaredRange(unsigned long Index, void* Entry, void* Value) override;
    GENRESULT __thiscall StoreParameterValue(unsigned long Index, void* Entry, void* Value) override; // 066f2ad0
    unsigned long __thiscall GetParameterTable(void** OutBase) override;           // FUN_066F2CD0
    void* __thiscall GetParameterAddress(unsigned long Index) override;            // 066fad90

protected:
    FMaterialImpl();
    ~FMaterialImpl() noexcept(false);

    void ReleaseMaterialResources();
    void SeedWhiteMaterialBlock();
    void RecomputeOpacityFlags();
    void TeardownCandidateIgnoringResult(FMaterialSubmitContext* Context);
    GENRESULT SubmitCandidateGeometry(FMaterialSubmitContext* Context, unsigned long PrimType,
                                     unsigned long Start, unsigned long Count,
                                     unsigned long IbStart, unsigned long IndexSpan,
                                     unsigned long Flags);
    void ApplyTssFromTextureBlock(unsigned long Stage, const unsigned long* Block);
    unsigned long TextureHandleAt(const unsigned long* Block) const;
    bool ContextWantsColorVertex(FMaterialSubmitContext* Context) const;
    float GetDiffuseAlpha() const;

    IRenderPipeline* Pipeline_;                  // +4
    ITextureLibrary2* TextureLibrary_;           // +8   borrowed from the outer component
    IVertexBufferManager* VertexBufferManager_;  // +C
    IRPDraw* RpDraw_;                            // +10
    char* InstanceName_;                         // +14  owned, lowercase, at most 64 bytes
    unsigned long* TextureBlock_;                // +18  0x2C-byte texture records
    unsigned long TextureVerifyCount_;           // +1C  number of texture records
    float MaterialBlock_[17];                    // +20
    // +0x64: alpha-blended draw (diffuse alpha below the threshold, or a texture with more than
    // one alpha bit); selects the blend RS tables and render flag 2.
    unsigned char bAlphaBlend_;
    // +0x65: the texture's format has a 1-bit alpha (descriptor +0x38 == 1).
    unsigned char bOneBitAlpha_;
};
static_assert(sizeof(FMaterialImpl) == 0x68, "the common material part is 0x68 bytes");

// FGenericPropertyMaterial parameter ids (the rows of its property schema).
enum EGenericPropertyParam
{
    kGenericPropertyAmbient = 0,
    kGenericPropertyDiffuse = 1,
    kGenericPropertyEmissive = 2,
    kGenericPropertySpecular = 3,
    kGenericPropertySpecularPower = 4,
    kGenericPropertyOpacity = 5,
    kGenericPropertyTextureName = 6,
    kGenericPropertyTextureWrap = 7,
    kGenericPropertyTextureAddressU = 8,
    kGenericPropertyTextureAddressV = 9,
};

// The seven classes whose GetParameterAddress is retail's shared 066f7f70 map.
class FGenericPropertyMaterial : public FMaterialImpl
{
public:
    void* __thiscall GetParameterAddress(unsigned long Index) override;            // 066f7f70
};

// "BackdropMaterial" / "StarsphereMaterial" (object vtable 067010e0).
class FBackdropMaterial : public FMaterialImpl
{
public:
    GENRESULT DACOM_API Bind(FMaterialSubmitContext* Context) override;                              // 066f2da0
    GENRESULT DACOM_API Teardown(FMaterialSubmitContext* Context) override;                          // 066f2e30
    GENRESULT DACOM_API Submit(FMaterialSubmitContext* Context, unsigned long PrimType, unsigned long Start,
                               unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                               unsigned long Flags) override;                      // 066f2ec0
    const char* __thiscall ClassName() override;
    GENRESULT __thiscall InitializeFromDescriptor(void* Arg) override;             // 066f2cf0

protected:
    FBackdropMaterial();
    GENRESULT TeardownBackdrop(FMaterialSubmitContext* Context);
    GENRESULT DrawBackdrop(FMaterialSubmitContext* Context, unsigned long Prim, unsigned long Start, unsigned long Count,
                           unsigned long IbStart, unsigned long IndexSpan, unsigned long Flags);

    unsigned char bBackdrop_;              // +68  1 for BackdropMaterial, 0 for StarsphereMaterial
    unsigned char bBackdropNameMatchCopy_; // +69  the same predicate, stored twice
};

// "CloakMaterial" (object vtable 06701578).
class FCloakMaterial : public FGenericPropertyMaterial
{
public:
    GENRESULT DACOM_API ApplyVerify(void* Context, void* Extra) override;          // 066f4000
    GENRESULT DACOM_API Bind(FMaterialSubmitContext* Context) override;                              // 066f4010
    GENRESULT DACOM_API Teardown(FMaterialSubmitContext* Context) override;                          // LAB_066f42e0
    GENRESULT DACOM_API Submit(FMaterialSubmitContext* Context, unsigned long PrimType, unsigned long Start,
                               unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                               unsigned long Flags) override;                      // 066f4220
    GENRESULT DACOM_API GetRenderFlags(unsigned long* Out) override;               // 066f3ff0
    const char* __thiscall ClassName() override;
    unsigned long __thiscall GetParameterTable(void** OutBase) override;           // 066f37d0
};

// FColorizeMaterial parameter ids: the generic rows, then the "flcopt" options dword.
enum EColorizeParam
{
    kColorizeAmbient = 0,
    kColorizeDiffuse = 1,
    kColorizeEmissive = 2,
    kColorizeSpecular = 3,
    kColorizeSpecularPower = 4,
    kColorizeOpacity = 5,
    kColorizeTextureName = 6,
    kColorizeTextureWrap = 7,
    kColorizeTextureAddressU = 8,
    kColorizeTextureAddressV = 9,
    kColorizeOptions = 10,
};

// "ColorizeMaterial" (object vtable 067017c0).
class FColorizeMaterial : public FMaterialImpl
{
public:
    GENRESULT DACOM_API ApplyVerify(void* Context, void* Extra) override;          // 066f4000
    GENRESULT DACOM_API Bind(FMaterialSubmitContext* Context) override;                              // 066f4d20
    GENRESULT DACOM_API Teardown(FMaterialSubmitContext* Context) override;                          // LAB_066f5010
    GENRESULT DACOM_API GetRenderFlags(unsigned long* Out) override;               // 066f4a90
    const char* __thiscall ClassName() override;
    GENRESULT __thiscall StoreParameterValue(unsigned long Index, void* Entry, void* Value) override; // 066f4ad0
    unsigned long __thiscall GetParameterTable(void** OutBase) override;           // 066f4680
    void* __thiscall GetParameterAddress(unsigned long Index) override;            // 066f4a00

protected:
    FColorizeMaterial();

    float ColorizeExtraW_;               // +68  alpha component: render flags and base opacity
    unsigned long ColorizeOptionsFlags_; // +6C  raw "Options" flags word
};

// "EngineGlowMaterial" (object vtable 06701900).
class FEngineGlowMaterial : public FGenericPropertyMaterial
{
public:
    GENRESULT DACOM_API ApplyVerify(void* Context, void* Extra) override;          // 066f5700
    GENRESULT DACOM_API Bind(FMaterialSubmitContext* Context) override;                              // 066f5760
    GENRESULT DACOM_API GetRenderFlags(unsigned long* Out) override;               // 066f56e0
    const char* __thiscall ClassName() override;
    GENRESULT __thiscall InitializeFromDescriptor(void* Arg) override;             // 066f6a40
    unsigned long __thiscall GetParameterTable(void** OutBase) override;           // 066f5370

protected:
    FEngineGlowMaterial();

    unsigned long Unread68_; // +68  zeroed by InitializeFromDescriptor, never read. TODO: meaning unknown.
    unsigned long Pad6C_;    // +6C  the object is 0x80 bytes; nothing in the port touches this word
};

// "EnvMapMaterial" (object vtable 06701a58).
class FEnvMapMaterial : public FMaterialImpl
{
public:
    GENRESULT DACOM_API Bind(FMaterialSubmitContext* Context) override;                              // 066f5af0
    GENRESULT DACOM_API Teardown(FMaterialSubmitContext* Context) override;                          // 066f6240
    GENRESULT DACOM_API Submit(FMaterialSubmitContext* Context, unsigned long PrimType, unsigned long Start,
                               unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                               unsigned long Flags) override;                      // 066f5dc0
    GENRESULT DACOM_API GetRenderFlags(unsigned long* Out) override;               // 066f56e0
    const char* __thiscall ClassName() override;

protected:
    FEnvMapMaterial();
    ~FEnvMapMaterial() noexcept(false);                                             // FUN_066f6450

    unsigned long EnvMapTextureRecord_;  // +68  retained texture record
    unsigned long EnvMapFlag_;
    unsigned long EnvMapResolvedHandle_; // stage texture handle resolved by Bind and reused by Submit
    unsigned long EnvMapTexNameCrc_;     // CRC of the texture name retained in EnvMapTextureRecord_
};

// "ExclusionZoneMaterial" (object vtable 06701c78).
class FExclusionZoneMaterial : public FGenericPropertyMaterial
{
public:
    GENRESULT DACOM_API Bind(FMaterialSubmitContext* Context) override;                              // 066f6a60
    GENRESULT DACOM_API Submit(FMaterialSubmitContext* Context, unsigned long PrimType, unsigned long Start,
                               unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                               unsigned long Flags) override;                      // 066f6b40
    GENRESULT DACOM_API GetRenderFlags(unsigned long* Out) override;               // 066f8010
    const char* __thiscall ClassName() override;
    GENRESULT __thiscall InitializeFromDescriptor(void* Arg) override;             // 066f6a40
    unsigned long __thiscall GetParameterTable(void** OutBase) override;           // 066f66e0

protected:
    FExclusionZoneMaterial();

    unsigned long Unread68_; // +68  zeroed by InitializeFromDescriptor, never read. TODO: meaning unknown.
};

// "GlassMaterial" / "HighGlassMaterial" / "GFGlassMaterial" (object vtable 06701e20).
class FGlassMaterial : public FGenericPropertyMaterial
{
public:
    GENRESULT DACOM_API Bind(FMaterialSubmitContext* Context) override;                              // 066f7500
    GENRESULT DACOM_API Submit(FMaterialSubmitContext* Context, unsigned long PrimType, unsigned long Start,
                               unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                               unsigned long Flags) override;                      // 066f7640
    const char* __thiscall ClassName() override;
    unsigned long __thiscall GetParameterTable(void** OutBase) override;           // 066f7190
};

// "HUDIconMaterial" (object vtable 06701fb8).
class FHudIconMaterial : public FGenericPropertyMaterial
{
public:
    GENRESULT DACOM_API Bind(FMaterialSubmitContext* Context) override;                              // 066f8030
    GENRESULT DACOM_API GetRenderFlags(unsigned long* Out) override;               // 066f8010
    const char* __thiscall ClassName() override;
    unsigned long __thiscall GetParameterTable(void** OutBase) override;           // 066f7c10
};

// FNomadMaterial parameter ids: diffuse colour, diffuse texture, then the "nt" texture.
enum ENomadParam
{
    kNomadDiffuse = 0,
    kNomadDiffuseTextureName = 1,
    kNomadDiffuseTextureWrap = 2,
    kNomadDiffuseTextureAddressU = 3,
    kNomadDiffuseTextureAddressV = 4,
    kNomadNomadTextureName = 5,
    kNomadNomadTextureWrap = 6,
    kNomadNomadTextureAddressU = 7,
    kNomadNomadTextureAddressV = 8,
};

// "NomadMaterial" / "NomadMaterialNoBendy" (object vtable 06702160).
class FNomadMaterial : public FMaterialImpl
{
public:
    GENRESULT DACOM_API ApplyVerify(void* Context, void* Extra) override;          // 066f8bf0
    GENRESULT DACOM_API ResolveTextures(float Delta) override;                     // 066f88b0
    GENRESULT DACOM_API Bind(FMaterialSubmitContext* Context) override;                              // 066f8940
    GENRESULT DACOM_API Teardown(FMaterialSubmitContext* Context) override;                          // 066f8bd0
    GENRESULT DACOM_API Submit(FMaterialSubmitContext* Context, unsigned long PrimType, unsigned long Start,
                               unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                               unsigned long Flags) override;                      // 066f8c50
    GENRESULT DACOM_API GetRenderFlags(unsigned long* Out) override;               // 066f8930
    const char* __thiscall ClassName() override;
    GENRESULT __thiscall InitializeFromDescriptor(void* Arg) override;             // 066f8840
    unsigned long __thiscall GetParameterTable(void** OutBase) override;           // 066f84a0
    void* __thiscall GetParameterAddress(unsigned long Index) override;            // 066f87a0

protected:
    FNomadMaterial();

    // +68: seconds accumulated by ResolveTextures only for the bendy variant; no reader in the port.
    float NomadBendElapsed_;
    // +6C: seconds accumulated by ResolveTextures; drives the second texture's phase in Submit.
    float NomadElapsed_;
    unsigned long Pad70_;         // +70  not touched by the port
    float NomadDiffuseColor_[3];  // +74  property 0, "Diffuse Color"
    // The object is 0x98 bytes; the order of the three members below is not verified.
    unsigned char bNomadRegular_;   // 0 for NomadMaterialNoBendy
    unsigned char bSecondTexture_;
    unsigned long StageCounter_;
};

// "PlanetWaterMaterial" (object vtable 067023d8).
class FPlanetWaterMaterial : public FGenericPropertyMaterial
{
public:
    GENRESULT DACOM_API ApplyVerify(void* Context, void* Extra) override;          // 066f9c00
    GENRESULT DACOM_API Bind(FMaterialSubmitContext* Context) override;                              // 066f9cd0
    GENRESULT DACOM_API Teardown(FMaterialSubmitContext* Context) override;                          // 066f9e50
    GENRESULT DACOM_API Submit(FMaterialSubmitContext* Context, unsigned long PrimType, unsigned long Start,
                               unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                               unsigned long Flags) override;                      // FUN_066f9ea0
    const char* __thiscall ClassName() override;
    unsigned long __thiscall GetParameterTable(void** OutBase) override;           // 066f9890

protected:
    FPlanetWaterMaterial();
    ~FPlanetWaterMaterial() noexcept(false);                                        // 066fa160

    IImaInstance* MatAnim_; // +68
};

// "ShadowMaterial" (object vtable 067024e0).
class FShadowMaterial : public FMaterialImpl
{
public:
    GENRESULT DACOM_API Bind(FMaterialSubmitContext* Context) override;                              // 066fa3f0
    GENRESULT DACOM_API Teardown(FMaterialSubmitContext* Context) override;                          // 066fa530
    GENRESULT DACOM_API Submit(FMaterialSubmitContext* Context, unsigned long PrimType, unsigned long Start,
                               unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                               unsigned long Flags) override;                      // 066fa5c0
    const char* __thiscall ClassName() override;

protected:
    FShadowMaterial();
    ~FShadowMaterial() noexcept(false);                                             // 066faad0
    GENRESULT ShadowCandidateDispatch(FMaterialSubmitContext* Context);
    GENRESULT DrawShadow(FMaterialSubmitContext* Context, unsigned long Prim, unsigned long Start, unsigned long Count,
                         unsigned long IbStart, unsigned long IndexSpan, unsigned long Flags);

    unsigned long ShadowLayerCount_;         // +68  candidate layer count
    ITextureLibrary2* ShadowTextureLibrary_; // +6C  borrowed
    unsigned long ShadowResolvedHandle_;     // +70  pipeline texture
    void* ShadowTextureRecord_;              // +74  retained
    bool bShadowTextureInitialized_;         // +78  one-shot lookup
};

// "VisibleCounterMaterial" (object vtable 06702628).
class FVisibleCounterMaterial : public FMaterialImpl
{
public:
    GENRESULT DACOM_API Bind(FMaterialSubmitContext* Context) override;                              // 066fadb0
    GENRESULT DACOM_API Teardown(FMaterialSubmitContext* Context) override;                          // 066fadb0
    GENRESULT DACOM_API Submit(FMaterialSubmitContext* Context, unsigned long PrimType, unsigned long Start,
                               unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                               unsigned long Flags) override;                      // LAB_066FADC0
    GENRESULT DACOM_API GetRenderFlags(unsigned long* Out) override;               // 066f56e0
    const char* __thiscall ClassName() override;
};

// "WireMaterial" (object vtable 06702788).
class FWireMaterial : public FMaterialImpl
{
public:
    GENRESULT DACOM_API Bind(FMaterialSubmitContext* Context) override;                              // 066fb490
    GENRESULT DACOM_API Submit(FMaterialSubmitContext* Context, unsigned long PrimType, unsigned long Start,
                               unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                               unsigned long Flags) override;                      // 066fb540
    GENRESULT DACOM_API GetRenderFlags(unsigned long* Out) override;               // 066fb1e0
    const char* __thiscall ClassName() override;
    GENRESULT __thiscall InitializeFromDescriptor(void* Arg) override;             // 066fb1b0
    GENRESULT __thiscall StoreParameterValue(unsigned long Index, void* Entry, void* Value) override; // 066fb1f0
    unsigned long __thiscall GetParameterTable(void** OutBase) override;           // 066fb090
    void* __thiscall GetParameterAddress(unsigned long Index) override;            // 066fbe40

protected:
    FWireMaterial();

    float WireColorR_;             // +68  RGBA, initially white
    float WireColorG_;
    float WireColorB_;
    float WireColorA_;
    unsigned long WireColorArgb_;  // +78
    unsigned long WireGroupIndex_; // +7C  cached facegroup cursor; group zero is retried once
};

// "WireMaterial2" (object vtable 06702900).
class FWire2Material : public FMaterialImpl
{
public:
    GENRESULT DACOM_API ApplyVerify(void* Context, void* Extra) override;          // 066fc0d0
    GENRESULT DACOM_API Bind(FMaterialSubmitContext* Context) override;                              // 066f32e0
    GENRESULT DACOM_API Submit(FMaterialSubmitContext* Context, unsigned long PrimType, unsigned long Start,
                               unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                               unsigned long Flags) override;                      // 066fc150
    GENRESULT DACOM_API GetRenderFlags(unsigned long* Out) override;               // 066fb1e0
    const char* __thiscall ClassName() override;
    GENRESULT __thiscall InitializeFromDescriptor(void* Arg) override;             // 066fbe60
    GENRESULT __thiscall ApplyInitialRenderStates(void* Arg) override;             // 066fc060
    GENRESULT __thiscall StoreParameterValue(unsigned long Index, void* Entry, void* Value) override; // 066fbe90
    unsigned long __thiscall GetParameterTable(void** OutBase) override;           // 066fbd20
    void* __thiscall GetParameterAddress(unsigned long Index) override;            // 066fbe40

protected:
    FWire2Material();

    float WireColorR_;               // +68  RGBA, initially white
    float WireColorG_;
    float WireColorB_;
    float WireColorA_;
    unsigned long WireColorArgb_;    // +78
    unsigned long WireColorRgb555_;  // +7C  reduced RGB for the shifted duplicate points
    unsigned long WireGroupIndex_;   // +80  cached facegroup cursor; group zero is retried once
};

// "HUDAnimMaterial" (object vtable 06703158).
class FHudAnimMaterial : public FGenericPropertyMaterial
{
public:
    GENRESULT DACOM_API Initialize(IDAComponent* Outer) override;                  // LAB_066ffee0
    GENRESULT DACOM_API ResolveTextures(float Delta) override;                     // LAB_066fff20
    GENRESULT DACOM_API Bind(FMaterialSubmitContext* Context) override;                              // 06700070
    GENRESULT DACOM_API Teardown(FMaterialSubmitContext* Context) override;                          // 06700200
    GENRESULT DACOM_API Submit(FMaterialSubmitContext* Context, unsigned long PrimType, unsigned long Start,
                               unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                               unsigned long Flags) override;                      // LAB_06700260
    GENRESULT DACOM_API GetRenderFlags(unsigned long* Out) override;               // 066f56e0
    const char* __thiscall ClassName() override;
    unsigned long __thiscall GetParameterTable(void** OutBase) override;           // 066ffb70

protected:
    FHudAnimMaterial();
    ~FHudAnimMaterial() noexcept(false);                                            // 06700590

    IImaInstance* MatAnim_;             // +68
    // The object is 0x84 bytes; the order of the two members below is not verified.
    unsigned long HudAnimFlag_;
    IMaterialLibrary* MaterialLibrary_; // borrowed from the outer container
};

// The complete object retail allocates: the material class, then the aggregation tail
// {controlling outer, inner controller}.
template <class TImpl>
class TFlmMaterialObject final : public TImpl
{
public:
    TFlmMaterialObject() : ControllingOuter_(&Inner_), Inner_(this) {}

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override
    {
        // LAB_066f51c0: the public face always delegates to its controlling outer.
        return ControllingOuter_->QueryInterface(Iid, Out);
    }
    unsigned long DACOM_API AddRef() override { return ControllingOuter_->AddRef(); }   // LAB_066f58f0
    unsigned long DACOM_API Release() override { return ControllingOuter_->Release(); } // LAB_066f5900
    GENRESULT DACOM_API InnerQueryInterface(const char* Iid, void** Out) override
    {
        return Inner_.QueryInterface(Iid, Out);
    }
    unsigned long DACOM_API InnerAddRef() override { return Inner_.AddRef(); }
    unsigned long DACOM_API InnerRelease() override { return Inner_.Release(); }

    // Inlined into every retail CreateInstance (066f52cb in EngineGlow's): the controller is
    // installed before Slot22.
    void ConfigureAggregation(FMaterialAggregateDesc* Desc)
    {
        ControllingOuter_ = Desc->OuterAt8;
        if (ControllingOuter_ == nullptr)
            ControllingOuter_ = &Inner_;
        else
            *Desc->InnerOutAtC = &Inner_;
    }

    void* __thiscall ScalarDeletingDestructor(unsigned char bDelete) override
    {
        // Destruction runs once; bit 0 selects deallocation. Return the original address.
        void* Storage = this;
        this->~TFlmMaterialObject();
        if ((bDelete & 1) != 0)
            ::operator delete(Storage);
        return Storage;
    }

    IDAComponent* ControllingOuter_;
    FMaterialImpl::FInner Inner_;
};

// Object sizes are the operator_new arguments of the fifteen retail CreateInstance bodies.
static_assert(sizeof(TFlmMaterialObject<FBackdropMaterial>) == 0x7C, "BackdropMaterial");
static_assert(sizeof(TFlmMaterialObject<FCloakMaterial>) == 0x78, "CloakMaterial");
static_assert(sizeof(TFlmMaterialObject<FColorizeMaterial>) == 0x80, "ColorizeMaterial");
static_assert(sizeof(TFlmMaterialObject<FEngineGlowMaterial>) == 0x80, "EngineGlowMaterial");
static_assert(sizeof(TFlmMaterialObject<FEnvMapMaterial>) == 0x88, "EnvMapMaterial");
static_assert(sizeof(TFlmMaterialObject<FExclusionZoneMaterial>) == 0x7C, "ExclusionZoneMaterial");
static_assert(sizeof(TFlmMaterialObject<FGlassMaterial>) == 0x78, "GlassMaterial");
static_assert(sizeof(TFlmMaterialObject<FHudIconMaterial>) == 0x78, "HUDIconMaterial");
static_assert(sizeof(TFlmMaterialObject<FNomadMaterial>) == 0x98, "NomadMaterial");
static_assert(sizeof(TFlmMaterialObject<FPlanetWaterMaterial>) == 0x7C, "PlanetWaterMaterial");
static_assert(sizeof(TFlmMaterialObject<FShadowMaterial>) == 0x8C, "ShadowMaterial");
static_assert(sizeof(TFlmMaterialObject<FVisibleCounterMaterial>) == 0x78, "VisibleCounterMaterial");
static_assert(sizeof(TFlmMaterialObject<FWireMaterial>) == 0x90, "WireMaterial");
static_assert(sizeof(TFlmMaterialObject<FWire2Material>) == 0x94, "WireMaterial2");
static_assert(sizeof(TFlmMaterialObject<FHudAnimMaterial>) == 0x84, "HUDAnimMaterial");
