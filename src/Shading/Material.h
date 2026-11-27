// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "ShadingInternal.h"

class FMaterial;

// Property accessors return records from these shared contiguous schemas.
FShadingPropertyRecord* GetBtDetailPropertySchema();
FShadingPropertyRecord* GetDetailMapPropertySchema();
FShadingPropertyRecord* GetAtmospherePropertySchema();
FShadingPropertyRecord* GetSinglePassPropertySchema();
FShadingPropertyRecord* GetIllumDetailPropertySchema();
FShadingPropertyRecord* GetEcEtPropertySchema();
FShadingPropertyRecord* GetDcDtEtPropertySchema();

// The nondelegating controller at the end of every material object: {vptr, RefCount, Owner}.
class FMaterialInner final : public IDAComponent
{
public:
    explicit FMaterialInner(FMaterial* InOwner);

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;

    unsigned long RefCount; // +4
    FMaterial* Owner;       // +8
};
static_assert(sizeof(FMaterialInner) == 0x0C, "inner controller is vptr, count, owner");

// The part every Shading material shares (retail 0x68 bytes). The sixteen material classes
// derive from it, add their own members from +0x68, and TMaterialObject<> appends the
// aggregation tail. A virtual a class does not override is the body retail's vtables share.
class FMaterial : public IMaterial
{
public:
    GENRESULT DACOM_API Initialize(IDAComponent* Outer) override;                  // FUN_06eb1200
    GENRESULT DACOM_API LoadFromFileSystem(IFileSystem* FileSystem) override;             // LAB_06eb1370
    GENRESULT DACOM_API SaveProperties(IFileSystem* FileSystem) override;                 // FUN_06eb1720
    GENRESULT DACOM_API ApplyVerify(void* Context, void* Extra) override;          // LAB_06eb3aa0
    GENRESULT DACOM_API ResolveTextures(float Delta) override;                     // LAB_06eb3ae0
    GENRESULT DACOM_API Bind(FMaterialSubmitContext* Context) override;                              // LAB_06ec9560
    GENRESULT DACOM_API Teardown(FMaterialSubmitContext* Context) override;                          // LAB_06eb1a10
    GENRESULT DACOM_API Submit(FMaterialSubmitContext* Context, unsigned long PrimType, unsigned long Start,
                               unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                               unsigned long Flags) override;                      // FUN_06eb3b90
    GENRESULT DACOM_API SetName(const char* Name) override;
    GENRESULT DACOM_API GetName(char* Out, unsigned long Size) override;
    GENRESULT DACOM_API CopyClassName(char* Out, unsigned long Size) override;
    GENRESULT DACOM_API GetPassCount(int* Out) override;                           // LAB_06eb1b40
    GENRESULT DACOM_API GetRenderFlags(unsigned long* Out) override;               // LAB_06eb80b0
    GENRESULT DACOM_API GetParameterCount(unsigned long* OutCount) override;
    GENRESULT DACOM_API GetParameterDescriptor(unsigned long Index, void* OutDesc) override;
    GENRESULT DACOM_API GetParameterValue(unsigned long Index, void* InOutDesc) override;
    GENRESULT DACOM_API SetParameterValue(unsigned long Index, void* Value) override;
    GENRESULT __thiscall InitializeFromDescriptor(void* Arg) override;             // FUN_06eb1a00
    GENRESULT __thiscall ApplyInitialRenderStates(void* Arg) override;
    GENRESULT __thiscall Setup() override;                                         // LAB_06eb2090
    bool __thiscall ValueWithinDeclaredRange(unsigned long Index, void* Entry, void* Value) override;
    GENRESULT __thiscall StoreParameterValue(unsigned long Index, void* Entry, void* Value) override; // FUN_06eb23c0

    IRenderPipeline* Pipeline;   // +4
    ITextureLibrary2* TexLib;    // +8   borrowed from the outer component
    IVertexBufferManager* VBMgr; // +C
    IRPDraw* RPDraw;             // +10
    const char* InstanceName;    // +14
    FTexBlock* TexBlocks;        // +18
    unsigned long TexCount;      // +1C
    float DiffuseR;              // +20
    float DiffuseG;
    float DiffuseB;
    float Opacity;               // +2C
    float Ambient[4];            // +30
    float Specular[4];           // +40
    float Emissive[4];           // +50
    float SpecularPower;         // +60
    unsigned char bAlphablend;   // +64  "Oc"
    unsigned char bAlphatest;    // +65
    unsigned char Pad66[2];

protected:
    FMaterial();
    ~FMaterial() noexcept(false);

    void SetInstanceName(const char* Name);
    GENRESULT InitializeOwnedSchema();
    GENRESULT LoadDeclaredSchema(IFileSystem* FileSystem);
    void ApplyOcFlag();
    GENRESULT SubmitIndexedListing(FMaterialSubmitContext* Context, unsigned long PrimType,
                                   unsigned long Start, unsigned long Count, unsigned long IbStart,
                                   unsigned long IndexSpan);
    GENRESULT SubmitCopiedVertices(FMaterialSubmitContext* Context, unsigned long PrimType,
                                   unsigned long Start, unsigned long Count, unsigned long IbStart,
                                   unsigned long IndexSpan);
    GENRESULT SubmitKernel(FMaterialSubmitContext* Context, unsigned long PrimType, unsigned long Start, unsigned long Count,
                           unsigned long IbStart, unsigned long IndexSpan, unsigned long Flags);
    // The back-face pass of the "Two" materials: the vertices are copied with negated normals
    // and drawn with the cull mode flipped through the class own lookup table.
    GENRESULT SubmitBackFacePass(FMaterialSubmitContext* Context, unsigned long PrimType, unsigned long Start,
                                 unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                                 const unsigned long* CullFlip);
};
static_assert(sizeof(FMaterial) == 0x68, "the common material part is 0x68 bytes");

// FSinglePassMaterial parameter ids (the rows of its property schema).
enum ESinglePassParam
{
    kSinglePassAmbient = 0,
    kSinglePassDiffuse = 1,
    kSinglePassEmissive = 2,
    kSinglePassSpecular = 3,
    kSinglePassSpecularPower = 4,
    kSinglePassOpacity = 5,
    kSinglePassTextureName = 6,
    kSinglePassTextureWrap = 7,
    kSinglePassTextureAddressU = 8,
    kSinglePassTextureAddressV = 9,
};

// "SinglePassMaterial": DcDt, DcDtEc, DcDtOcOt, DcDtEcOcOt (object vtable 06ed27f0).
class FSinglePassMaterial : public FMaterial
{
public:
    GENRESULT DACOM_API ApplyVerify(void* Context, void* Extra) override;          // LAB_06ebd810
    GENRESULT DACOM_API Bind(FMaterialSubmitContext* Context) override;                              // FUN_06ebd8b0
    GENRESULT DACOM_API Teardown(FMaterialSubmitContext* Context) override;                          // FUN_06ebda30
    GENRESULT DACOM_API Submit(FMaterialSubmitContext* Context, unsigned long PrimType, unsigned long Start,
                               unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                               unsigned long Flags) override;                      // LAB_06ebda80
    const char* __thiscall ClassName() override;
    unsigned long __thiscall GetParameterTable(void** OutBase) override;           // 06ebd410
    void* __thiscall GetParameterAddress(unsigned long Index) override;            // LAB_06ebd770

    IImaInstance* MatAnim; // +68

protected:
    FSinglePassMaterial();
    ~FSinglePassMaterial() noexcept(false);                                         // FUN_06ebe490
};

// "SinglePassTwoMaterial": the four *Two names (object vtable 06ed2870).
class FSinglePassTwoMaterial : public FSinglePassMaterial
{
public:
    GENRESULT DACOM_API Submit(FMaterialSubmitContext* Context, unsigned long PrimType, unsigned long Start,
                               unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                               unsigned long Flags) override;                      // LAB_06ebdfc0
    GENRESULT DACOM_API GetRenderFlags(unsigned long* Out) override;               // LAB_06ebdfa0
    const char* __thiscall ClassName() override;

private:
    GENRESULT SubmitSecondPass(FMaterialSubmitContext* Context, unsigned long PrimType, unsigned long Start,
                               unsigned long Count, unsigned long IbStart, unsigned long IndexSpan);
};

// FDcDtEtMaterial parameter ids: diffuse colour, diffuse texture, emissive texture.
enum EDcDtEtParam
{
    kDcDtEtDiffuse = 0,
    kDcDtEtDiffuseTextureName = 1,
    kDcDtEtDiffuseTextureWrap = 2,
    kDcDtEtDiffuseTextureAddressU = 3,
    kDcDtEtDiffuseTextureAddressV = 4,
    kDcDtEtEmissiveTextureName = 5,
    kDcDtEtEmissiveTextureWrap = 6,
    kDcDtEtEmissiveTextureAddressU = 7,
    kDcDtEtEmissiveTextureAddressV = 8,
};

// "DcDtEt" (object vtable 06ed17b8).
class FDcDtEtMaterial : public FMaterial
{
public:
    GENRESULT DACOM_API ApplyVerify(void* Context, void* Extra) override;          // LAB_06eb63b0
    GENRESULT DACOM_API ResolveTextures(float Delta) override;                     // 06eb6170
    GENRESULT DACOM_API Bind(FMaterialSubmitContext* Context) override;                              // FUN_06eb61e0
    GENRESULT DACOM_API GetRenderFlags(unsigned long* Out) override;               // LAB_06eb6150
    const char* __thiscall ClassName() override;
    unsigned long __thiscall GetParameterTable(void** OutBase) override;           // 06eb5db0
    void* __thiscall GetParameterAddress(unsigned long Index) override;            // 06eb60b0

    bool bSecondaryTextureEnabled; // +68

protected:
    FDcDtEtMaterial();
};

// FEcEtMaterial parameter ids: emissive colour and emissive texture.
enum EEcEtParam
{
    kEcEtEmissive = 0,
    kEcEtEmissiveTextureName = 1,
    kEcEtEmissiveTextureWrap = 2,
    kEcEtEmissiveTextureAddressU = 3,
    kEcEtEmissiveTextureAddressV = 4,
};

// "EcEt" (object vtable 06ed1d28).
class FEcEtMaterial : public FMaterial
{
public:
    GENRESULT DACOM_API Bind(FMaterialSubmitContext* Context) override;                              // FUN_06eb8a40
    const char* __thiscall ClassName() override;
    unsigned long __thiscall GetParameterTable(void** OutBase) override;           // 06eb8770
    void* __thiscall GetParameterAddress(unsigned long Index) override;            // LAB_06eb89e0
};

// "EcEtTwo" (object vtable 06ed1da8).
class FEcEtTwoMaterial : public FEcEtMaterial
{
public:
    GENRESULT DACOM_API Submit(FMaterialSubmitContext* Context, unsigned long PrimType, unsigned long Start,
                               unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                               unsigned long Flags) override;                      // 06eb8d90
    GENRESULT DACOM_API GetRenderFlags(unsigned long* Out) override;               // LAB_06ebdfa0
    const char* __thiscall ClassName() override;
};

// FAtmosphereMaterial parameter ids.
enum EAtmosphereParam
{
    kAtmosphereAmbient = 0,
    kAtmosphereDiffuse = 1,
    kAtmosphereFadeTextureName = 2,
    kAtmosphereFadeTextureWrap = 3,
    kAtmosphereFadeTextureAddressU = 4,
    kAtmosphereFadeTextureAddressV = 5,
    kAtmosphereFadeOff = 6,
    kAtmosphereAlpha = 7,
    kAtmosphereScale = 8,
};

// "AtmosphereMaterial" (object vtable 06ed11f0).
class FAtmosphereMaterial : public FMaterial
{
public:
    GENRESULT DACOM_API ApplyVerify(void* Context, void* Extra) override;          // LAB_06eb2cb0
    GENRESULT DACOM_API ResolveTextures(float Delta) override;                     // LAB_06eb2c40
    GENRESULT DACOM_API Bind(FMaterialSubmitContext* Context) override;                              // FUN_06eb2b40
    GENRESULT DACOM_API Submit(FMaterialSubmitContext* Context, unsigned long PrimType, unsigned long Start,
                               unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                               unsigned long Flags) override;                      // LAB_06eb2d40
    GENRESULT DACOM_API GetRenderFlags(unsigned long* Out) override;               // 06eb2b30
    const char* __thiscall ClassName() override;
    GENRESULT __thiscall InitializeFromDescriptor(void* Arg) override;             // FUN_06eb2b10
    unsigned long __thiscall GetParameterTable(void** OutBase) override;           // LAB_06eb25c0
    void* __thiscall GetParameterAddress(unsigned long Index) override;            // 06eb2a90

    float FadeOff;                  // +68  property 6, default 1.0 (DAT_06ed11c4)
    float Alpha;                    // +6C  property 7, default 0.75 (DAT_06ed11c8)
    float Scale;                    // +70  property 8, default DAT_06ed11cc (about 1.025)
    unsigned char bAtmosphereReady; // +74
    unsigned char Pad75[3];

protected:
    FAtmosphereMaterial();
    GENRESULT SubmitAtmosphereLock(FMaterialSubmitContext* Context, unsigned long PrimType, unsigned long Start,
                                   unsigned long Count, unsigned long IbStart, unsigned long IndexSpan);
};

// FBtDetailMapMaterial parameter ids (the single-pass rows, then the "Bt" detail texture).
enum EBtDetailParam
{
    kBtDetailAmbient = 0,
    kBtDetailDiffuse = 1,
    kBtDetailEmissive = 2,
    kBtDetailSpecular = 3,
    kBtDetailSpecularPower = 4,
    kBtDetailOpacity = 5,
    kBtDetailTextureName = 6,
    kBtDetailTextureWrap = 7,
    kBtDetailTextureAddressU = 8,
    kBtDetailTextureAddressV = 9,
    kBtDetailDetailTextureName = 10,
    kBtDetailDetailTextureWrap = 11,
    kBtDetailDetailTextureAddressU = 12,
    kBtDetailDetailTextureAddressV = 13,
};

// "BtDetailMapMaterial" (object vtable 06ed1598).
class FBtDetailMapMaterial : public FMaterial
{
public:
    GENRESULT DACOM_API ApplyVerify(void* Context, void* Extra) override;          // LAB_06eb52b0
    GENRESULT DACOM_API Bind(FMaterialSubmitContext* Context) override;                              // FUN_06eb5100
    GENRESULT DACOM_API Teardown(FMaterialSubmitContext* Context) override;                          // FUN_06eb50a0
    GENRESULT DACOM_API GetRenderFlags(unsigned long* Out) override;               // LAB_06eb6150
    const char* __thiscall ClassName() override;
    unsigned long __thiscall GetParameterTable(void** OutBase) override;           // 06eb4990
    void* __thiscall GetParameterAddress(unsigned long Index) override;            // 06eb4fd0

    bool bSecondaryTextureEnabled;  // +68
    bool bSecondaryTextureVerified; // +69

protected:
    FBtDetailMapMaterial();
};

// "BtDetailMapTwoMaterial" (object vtable 06ed1618).
class FBtDetailMapTwoMaterial : public FBtDetailMapMaterial
{
public:
    GENRESULT DACOM_API Submit(FMaterialSubmitContext* Context, unsigned long PrimType, unsigned long Start,
                               unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                               unsigned long Flags) override;                      // LAB_06eb55b0
    GENRESULT DACOM_API GetRenderFlags(unsigned long* Out) override;               // LAB_06eb5590
    const char* __thiscall ClassName() override;
};

// Parameter ids shared by FDetailMap2PassMaterial and FDetailMapMaterial (retail shares LAB_06eb6b80 / FUN_06eb6c40).
enum EDetailMapParam
{
    kDetailMapAmbient = 0,
    kDetailMapDiffuse = 1,
    kDetailMapDiffuseTextureName = 2,
    kDetailMapDiffuseTextureWrap = 3,
    kDetailMapDiffuseTextureAddressU = 4,
    kDetailMapDiffuseTextureAddressV = 5,
    kDetailMapFlipU = 6,
    kDetailMapFlipV = 7,
    kDetailMapDetailTextureName = 8,
    kDetailMapDetailTextureWrap = 9,
    kDetailMapDetailTextureAddressU = 10,
    kDetailMapDetailTextureAddressV = 11,
    kDetailMapTileRate = 12,
};

// "DetailMap2Dm1Msk2PassMaterial" (object vtable 06ed19c8).
class FDetailMap2PassMaterial : public FMaterial
{
public:
    GENRESULT DACOM_API ApplyVerify(void* Context, void* Extra) override;          // LAB_06eb6ef0
    GENRESULT DACOM_API Teardown(FMaterialSubmitContext* Context) override;                          // FUN_06eb6e80
    GENRESULT DACOM_API Submit(FMaterialSubmitContext* Context, unsigned long PrimType, unsigned long Start,
                               unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                               unsigned long Flags) override;                      // LAB_06eb7560
    GENRESULT DACOM_API GetPassCount(int* Out) override;                           // LAB_06eb6e60
    GENRESULT DACOM_API GetRenderFlags(unsigned long* Out) override;               // 06ebb3e0
    const char* __thiscall ClassName() override;
    GENRESULT __thiscall StoreParameterValue(unsigned long Index, void* Entry, void* Value) override; // FUN_06eb6c40
    unsigned long __thiscall GetParameterTable(void** OutBase) override;           // 06eb67c0
    void* __thiscall GetParameterAddress(unsigned long Index) override;            // LAB_06eb6b80

    unsigned long DetailFlipU;        // +68
    unsigned long DetailFlipV;        // +6C
    float DetailTileRate0;            // +70
    unsigned long DetailPassCount;    // +74  1 or 2
    bool bDetailMultiPassEnabled;     // +78  global record 0 gate
    bool bDetailSecondStageVerified;  // +79

protected:
    FDetailMap2PassMaterial();
    bool ApplyDetailPassState(unsigned long Pass);                                  // FUN_06eb71f0
};

// "DetailMapMaterial" (object vtable 06ed1ba0).
class FDetailMapMaterial : public FMaterial
{
public:
    GENRESULT DACOM_API ApplyVerify(void* Context, void* Extra) override;          // LAB_06eb80d0
    GENRESULT DACOM_API Bind(FMaterialSubmitContext* Context) override;                              // LAB_06eb81e0
    GENRESULT DACOM_API Teardown(FMaterialSubmitContext* Context) override;                          // FUN_06eb8170
    const char* __thiscall ClassName() override;
    GENRESULT __thiscall StoreParameterValue(unsigned long Index, void* Entry, void* Value) override; // FUN_06eb6c40
    unsigned long __thiscall GetParameterTable(void** OutBase) override;           // 06eb7ce0
    void* __thiscall GetParameterAddress(unsigned long Index) override;            // LAB_06eb6b80

    unsigned long DetailFlipU;        // +68
    unsigned long DetailFlipV;        // +6C
    float DetailTileRate0;            // +70
    bool bDetailMultiPassEnabled;     // +74
    bool bDetailSecondStageVerified;  // +75

protected:
    FDetailMapMaterial();
};

// Parameter ids shared by FIllumDetailMapMaterial and FMasked2DetailMapMaterial (FTwoDetailMapMaterial).
enum ETwoDetailMapParam
{
    kTwoDetailMapAmbient = 0,
    kTwoDetailMapDiffuse = 1,
    kTwoDetailMapDiffuseTextureName = 2,
    kTwoDetailMapDiffuseTextureWrap = 3,
    kTwoDetailMapDiffuseTextureAddressU = 4,
    kTwoDetailMapDiffuseTextureAddressV = 5,
    kTwoDetailMapFlipU = 6,
    kTwoDetailMapFlipV = 7,
    kTwoDetailMapAlpha0TextureName = 8,
    kTwoDetailMapAlpha0TextureWrap = 9,
    kTwoDetailMapAlpha0TextureAddressU = 10,
    kTwoDetailMapAlpha0TextureAddressV = 11,
    kTwoDetailMapTileRate0 = 12,
    kTwoDetailMapAlpha1TextureName = 13,
    kTwoDetailMapAlpha1TextureWrap = 14,
    kTwoDetailMapAlpha1TextureAddressU = 15,
    kTwoDetailMapAlpha1TextureAddressV = 16,
    kTwoDetailMapTileRate1 = 17,
};

// What IllumDetailMapMaterial and Masked2DetailMapMaterial have in common: retail shares
// GetPassCount, GetRenderFlags, StoreParameterValue and GetParameterAddress between the two.
class FTwoDetailMapMaterial : public FMaterial
{
public:
    GENRESULT DACOM_API GetPassCount(int* Out) override;                           // 06ec9a50
    GENRESULT DACOM_API GetRenderFlags(unsigned long* Out) override;               // 06ebb3e0
    GENRESULT __thiscall StoreParameterValue(unsigned long Index, void* Entry, void* Value) override; // FUN_06ebb3f0
    void* __thiscall GetParameterAddress(unsigned long Index) override;            // LAB_06ebb2e0

    unsigned long DetailFlipU;        // +68
    unsigned long DetailFlipV;        // +6C
    float DetailTileRate0;            // +70
    float DetailTileRate1;            // +74
    unsigned long DetailPassCount;    // +78  1 or 2
    bool bDetailMultiPassEnabled;     // +7C  global record 0 gate
    bool bDetailSecondStageVerified;  // +7D

protected:
    FTwoDetailMapMaterial();
};

// "IllumDetailMapMaterial" (object vtable 06ed1fd8).
class FIllumDetailMapMaterial : public FTwoDetailMapMaterial
{
public:
    GENRESULT DACOM_API ApplyVerify(void* Context, void* Extra) override;          // LAB_06eb9920
    GENRESULT DACOM_API Teardown(FMaterialSubmitContext* Context) override;                          // FUN_06eb98b0
    GENRESULT DACOM_API Submit(FMaterialSubmitContext* Context, unsigned long PrimType, unsigned long Start,
                               unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                               unsigned long Flags) override;                      // LAB_06eb9e40
    const char* __thiscall ClassName() override;
    unsigned long __thiscall GetParameterTable(void** OutBase) override;           // 06eb93c0

protected:
    bool ApplyIllumDetailPassState(unsigned long Pass);
};

// "Masked2DetailMapMaterial" (object vtable 06ed2338).
class FMasked2DetailMapMaterial : public FTwoDetailMapMaterial
{
public:
    GENRESULT DACOM_API ApplyVerify(void* Context, void* Extra) override;          // LAB_06ebb680
    GENRESULT DACOM_API Teardown(FMaterialSubmitContext* Context) override;                          // FUN_06ebb610
    GENRESULT DACOM_API Submit(FMaterialSubmitContext* Context, unsigned long PrimType, unsigned long Start,
                               unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                               unsigned long Flags) override;                      // LAB_06ebbc40
    const char* __thiscall ClassName() override;
    unsigned long __thiscall GetParameterTable(void** OutBase) override;           // 06ebae00

protected:
    bool ApplyMaskedDetailPassState(unsigned long Pass);
};

// Parameter ids shared by FNebulaMaterial and FLightMapMaterial (one texture; retail shares LAB_06ebc5a0).
enum ENebulaParam
{
    kNebulaTextureName = 0,
    kNebulaTextureWrap = 1,
    kNebulaTextureAddressU = 2,
    kNebulaTextureAddressV = 3,
};

// "LightMap" (object vtable 06ed2138).
class FLightMapMaterial : public FMaterial
{
public:
    GENRESULT DACOM_API Bind(FMaterialSubmitContext* Context) override;                              // FUN_06eba720
    GENRESULT DACOM_API Submit(FMaterialSubmitContext* Context, unsigned long PrimType, unsigned long Start,
                               unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                               unsigned long Flags) override;                      // LAB_06eba860
    GENRESULT DACOM_API GetRenderFlags(unsigned long* Out) override;               // 06eba700
    const char* __thiscall ClassName() override;
    unsigned long __thiscall GetParameterTable(void** OutBase) override;           // 06eba540
    void* __thiscall GetParameterAddress(unsigned long Index) override;            // LAB_06ebc5a0
};

// "Nebula" (object vtable 06ed24a0).
class FNebulaMaterial : public FMaterial
{
public:
    GENRESULT DACOM_API Bind(FMaterialSubmitContext* Context) override;                              // FUN_06ebc600
    GENRESULT DACOM_API GetRenderFlags(unsigned long* Out) override;               // 06eba700
    const char* __thiscall ClassName() override;
    unsigned long __thiscall GetParameterTable(void** OutBase) override;           // 06ebc3f0
    void* __thiscall GetParameterAddress(unsigned long Index) override;            // LAB_06ebc5a0
};

// "NebulaTwo" (object vtable 06ed2520).
class FNebulaTwoMaterial : public FNebulaMaterial
{
public:
    GENRESULT DACOM_API Submit(FMaterialSubmitContext* Context, unsigned long PrimType, unsigned long Start,
                               unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                               unsigned long Flags) override;                      // 06ebc970
    GENRESULT DACOM_API GetRenderFlags(unsigned long* Out) override;               // 06ebc950
    const char* __thiscall ClassName() override;
};

// "NullMaterial" (object vtable 06ed25d8).
class FNullMaterial : public FMaterial
{
public:
    GENRESULT DACOM_API ApplyVerify(void* Context, void* Extra) override;          // 06ebcf70
    GENRESULT DACOM_API Teardown(FMaterialSubmitContext* Context) override;                          // LAB_06ec9560
    GENRESULT DACOM_API Submit(FMaterialSubmitContext* Context, unsigned long PrimType, unsigned long Start,
                               unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                               unsigned long Flags) override;                      // 06ebcf80
    GENRESULT DACOM_API GetRenderFlags(unsigned long* Out) override;               // 06ebcf60
    const char* __thiscall ClassName() override;
    unsigned long __thiscall GetParameterTable(void** OutBase) override;           // LAB_06eb3da0
    void* __thiscall GetParameterAddress(unsigned long Index) override;            // FUN_06eb1a00
};

// The complete object retail allocates: the material class, then the aggregation tail
// {Outer, Inner}. The tail sits at a different offset per class, which is why retail has one
// QueryInterface / AddRef / Release triple per object size.
template <class TImpl>
class TMaterialObject final : public TImpl
{
public:
    TMaterialObject() : Outer(&Inner), Inner(this) {}

    // The public face always delegates to the controlling outer (06eb91e0 / 06eb91f0 / 06eb9200
    // for the 0x78-byte objects).
    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override
    {
        return Outer->QueryInterface(Iid, Out);
    }
    unsigned long DACOM_API AddRef() override { return Outer->AddRef(); }
    unsigned long DACOM_API Release() override { return Outer->Release(); }
    GENRESULT DACOM_API InnerQueryInterface(const char* Iid, void** Out) override
    {
        return Inner.QueryInterface(Iid, Out);
    }
    unsigned long DACOM_API InnerAddRef() override { return Inner.AddRef(); }
    unsigned long DACOM_API InnerRelease() override { return Inner.Release(); }

    // Inlined into every retail CreateInstance (06ebced5 in NullMaterial's).
    void ConfigureAggregation(FMaterialAggregateDesc* Desc)
    {
        Outer = Desc->OuterAt8;
        if (Outer == nullptr)
            Outer = &Inner;
        else
            *Desc->InnerOutAtC = &Inner;
    }

    void* __thiscall ScalarDeletingDestructor(unsigned char bDelete) override
    {
        void* Storage = this;
        this->~TMaterialObject();
        // Destruction has already run; the flag selects deallocation only.
        if ((bDelete & 1) != 0)
            ::operator delete(Storage);
        return Storage;
    }

    IDAComponent* Outer;  // the controlling outer: the aggregating component, or Inner
    FMaterialInner Inner;
};

// Object sizes are the operator_new arguments of the sixteen retail CreateInstance bodies.
static_assert(sizeof(TMaterialObject<FSinglePassMaterial>) == 0x7C, "SinglePassMaterial");
static_assert(sizeof(TMaterialObject<FSinglePassTwoMaterial>) == 0x7C, "SinglePassTwoMaterial");
static_assert(sizeof(TMaterialObject<FDcDtEtMaterial>) == 0x7C, "DcDtEt");
static_assert(sizeof(TMaterialObject<FEcEtMaterial>) == 0x78, "EcEt");
static_assert(sizeof(TMaterialObject<FEcEtTwoMaterial>) == 0x78, "EcEtTwo");
static_assert(sizeof(TMaterialObject<FAtmosphereMaterial>) == 0x88, "AtmosphereMaterial");
static_assert(sizeof(TMaterialObject<FBtDetailMapMaterial>) == 0x7C, "BtDetailMapMaterial");
static_assert(sizeof(TMaterialObject<FBtDetailMapTwoMaterial>) == 0x7C, "BtDetailMapTwoMaterial");
static_assert(sizeof(TMaterialObject<FDetailMap2PassMaterial>) == 0x8C, "DetailMap2Dm1Msk2PassMaterial");
static_assert(sizeof(TMaterialObject<FDetailMapMaterial>) == 0x88, "DetailMapMaterial");
static_assert(sizeof(TMaterialObject<FIllumDetailMapMaterial>) == 0x90, "IllumDetailMapMaterial");
static_assert(sizeof(TMaterialObject<FMasked2DetailMapMaterial>) == 0x90, "Masked2DetailMapMaterial");
static_assert(sizeof(TMaterialObject<FLightMapMaterial>) == 0x78, "LightMap");
static_assert(sizeof(TMaterialObject<FNebulaMaterial>) == 0x78, "Nebula");
static_assert(sizeof(TMaterialObject<FNebulaTwoMaterial>) == 0x78, "NebulaTwo");
static_assert(sizeof(TMaterialObject<FNullMaterial>) == 0x78, "NullMaterial");

class FBatchedFace final : public IBatchedMaterial
{
public:
    FBatchedFace();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;
    GENRESULT DACOM_API CreateInnerMaterial(void* Name) override;
    GENRESULT DACOM_API SetBatchBin(void* Value) override;
    GENRESULT DACOM_API GetBatchBin(void** Out) override;
    GENRESULT DACOM_API Bind(FMaterialSubmitContext* Context) override;
    GENRESULT DACOM_API Teardown(FMaterialSubmitContext* Context) override;
    GENRESULT DACOM_API Submit(FMaterialSubmitContext* Context, unsigned long PrimType, unsigned long Start, unsigned long Count,
                              unsigned long IbStart, unsigned long IndexSpan, unsigned long Flags) override;

    class FBatchedMaterial* OwnerFromThis();
};

class FBatchedController final : public IDAComponent
{
public:
    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;
    class FBatchedMaterial* OwnerFromThis();
};

class FBatchedMaterial final : public IMaterial
{
public:
    FBatchedMaterial();
    ~FBatchedMaterial() noexcept(false);

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;

    GENRESULT DACOM_API Initialize(IDAComponent* Outer) override;
    GENRESULT DACOM_API LoadFromFileSystem(IFileSystem* FileSystem) override;
    GENRESULT DACOM_API SaveProperties(IFileSystem* FileSystem) override;
    GENRESULT DACOM_API ApplyVerify(void* Context, void* Extra) override;
    GENRESULT DACOM_API ResolveTextures(float Delta) override;
    GENRESULT DACOM_API Bind(FMaterialSubmitContext* Context) override;
    GENRESULT DACOM_API Teardown(FMaterialSubmitContext* Context) override;
    GENRESULT DACOM_API Submit(FMaterialSubmitContext* Context, unsigned long PrimType, unsigned long Start,
                               unsigned long Count, unsigned long IbStart, unsigned long IndexSpan,
                               unsigned long Flags) override;
    GENRESULT DACOM_API SetName(const char* Name) override;
    GENRESULT DACOM_API GetName(char* Out, unsigned long Size) override;
    GENRESULT DACOM_API CopyClassName(char* Out, unsigned long Size) override;
    GENRESULT DACOM_API GetPassCount(int* Out) override;
    GENRESULT DACOM_API GetRenderFlags(unsigned long* Out) override;
    GENRESULT DACOM_API GetParameterCount(unsigned long* OutCount) override;
    GENRESULT DACOM_API GetParameterDescriptor(unsigned long Index, void* OutDesc) override;
    GENRESULT DACOM_API GetParameterValue(unsigned long Index, void* InOutDesc) override;
    GENRESULT DACOM_API SetParameterValue(unsigned long Index, void* Value) override;
    const char* __thiscall ClassName() override;
    void* __thiscall ScalarDeletingDestructor(unsigned char bDelete) override;
    GENRESULT __thiscall InitializeFromDescriptor(void* Arg) override;
    GENRESULT __thiscall ApplyInitialRenderStates(void* Arg) override;
    GENRESULT __thiscall Setup() override;
    bool __thiscall ValueWithinDeclaredRange(unsigned long Index, void* Entry, void* Value) override;
    GENRESULT __thiscall StoreParameterValue(unsigned long Index, void* Entry, void* Value) override;
    unsigned long __thiscall GetParameterTable(void** OutBase) override;
    void* __thiscall GetParameterAddress(unsigned long Index) override;
    GENRESULT DACOM_API InnerQueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API InnerAddRef() override;
    unsigned long DACOM_API InnerRelease() override;
    void ConfigureAggregation(FMaterialAggregateDesc* Desc);
    GENRESULT InitializeFromDescriptor(FMaterialAggregateDesc* Desc);
    FBatchedFace Batched; // +4  QI 1.11_IBatchedMaterial (vptr only)
    unsigned long BatchBin; // +8  LAB_06eb4480 / FUN_06eb40f0 default -1
    IDAComponent* InnerController; // +C retained nondelegating factory controller
    IMaterial* Inner;     // +10 LAB_06eb4380
    IMaterialBatcher2* Batcher; // +14 Initialize QI IMaterialBatcher
    unsigned long RefCount;
    IDAComponent* Outer;

    FBatchedController Controller;
    IMaterial* InnerMaterial() const;
};
