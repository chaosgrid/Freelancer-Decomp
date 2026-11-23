// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "alchemy_types.h"

// alchemy.dll — DACOM particle FX scaffolding.
//
// FALMaterial ("ALMaterial", retail 0x062031b0..0x06203460): a stub material that
// answers "1.11_IMaterial" (offset 0) and "1.11_IBatchedMaterial" (offset 4, adjustor
// thunks LAB_06203440/50/60). It is not one of the RegisterComponent names. Its
// interfaces are an alchemy-local shape (21 and 9 slots), smaller than flmaterials'
// IMaterial; the slot names follow the shared IMaterial / IBatchedMaterial ones.
// The refcount is at this+8; Release frees via FUN_062460e0.
//
// One global instance is built at DLL load by a CRT static initializer (LAB_062031b0,
// table DAT_06255000..0625504c). No code in alchemy.dll queries it; the consumer, if
// any, is outside the module (possibly the Alchemy.useMaterialBatcher path).
namespace AlchemyImpl
{
    class FALMaterial;

    // Retail vtable2 (PTR_LAB_0624b334, "1.11_IBatchedMaterial"): a facet at object+4
    // whose QueryInterface/AddRef/Release delegate to the outer object.
    struct IALBatchedMaterial : IDAComponent
    {
        virtual GENRESULT DACOM_API CreateInnerMaterial(void* A) = 0;       // +0x0C LAB_062032a0, -1
        virtual GENRESULT DACOM_API SetBatchBin(void* A) = 0;               // +0x10 LAB_062032b0, -1
        virtual GENRESULT DACOM_API GetBatchBin(unsigned long* Out) = 0;    // +0x14 LAB_062032c0, *Out=1
        virtual GENRESULT DACOM_API Bind(void* A) = 0;                      // +0x18 LAB_06203300
        virtual GENRESULT DACOM_API Teardown(void* A) = 0;                  // +0x1C LAB_06203310
        virtual GENRESULT DACOM_API Submit(void* A, void* B, void* C, void* D,
                                           void* E, void* F, void* G) = 0;  // +0x20 LAB_062032f0
    };

    class FALBatchedMaterialFacet final : public IALBatchedMaterial
    {
    public:
        explicit FALBatchedMaterialFacet(FALMaterial* Outer) : Outer_(Outer) {}

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
        unsigned long DACOM_API AddRef() override;
        unsigned long DACOM_API Release() override;

        GENRESULT DACOM_API CreateInnerMaterial(void* A) override;
        GENRESULT DACOM_API SetBatchBin(void* A) override;
        GENRESULT DACOM_API GetBatchBin(unsigned long* Out) override;
        GENRESULT DACOM_API Bind(void* A) override;
        GENRESULT DACOM_API Teardown(void* A) override;
        GENRESULT DACOM_API Submit(void* A, void* B, void* C, void* D, void* E, void* F,
                                   void* G) override;

    private:
        FALMaterial* Outer_;
    };

    // Retail vtable1 (PTR_LAB_0624b358, "1.11_IMaterial").
    struct IALMaterial : IDAComponent
    {
        virtual GENRESULT DACOM_API Initialize(void* A) = 0;                // +0x0C LAB_062031d0
        virtual GENRESULT DACOM_API LoadFromFileSystem(void* A) = 0;        // +0x10 LAB_062031e0
        virtual GENRESULT DACOM_API SaveProperties(void* A) = 0;            // +0x14 LAB_062031f0
        virtual GENRESULT DACOM_API ApplyVerify(void* A, void* B) = 0;      // +0x18 LAB_06203200
        virtual GENRESULT DACOM_API ResolveTextures(void* A) = 0;           // +0x1C LAB_06203210
        virtual GENRESULT DACOM_API Bind(void* A) = 0;                      // +0x20 LAB_06203320
        virtual GENRESULT DACOM_API Teardown(void* A) = 0;                  // +0x24 LAB_06203330
        virtual GENRESULT DACOM_API Submit(void* A, void* B, void* C, void* D,
                                           void* E, void* F, void* G) = 0;  // +0x28 LAB_062032e0, -1
        virtual GENRESULT DACOM_API SetName(void* A) = 0;                   // +0x2C LAB_06203220, -1
        virtual GENRESULT DACOM_API GetName(void* A, void* B) = 0;          // +0x30 LAB_06203230, -1
        virtual GENRESULT DACOM_API CopyClassName(void* A, void* B) = 0;    // +0x34 LAB_06203240, -1
        virtual GENRESULT DACOM_API GetPassCount(unsigned long* Out) = 0;   // +0x38 LAB_06203250, *Out=1
        virtual GENRESULT DACOM_API GetRenderFlags(unsigned long* Out) = 0; // +0x3C LAB_062032d0, *Out=2
        virtual GENRESULT DACOM_API GetParameterCount(unsigned long* Out) = 0; // +0x40 LAB_06203260, *Out=0
        virtual GENRESULT DACOM_API GetParameterDescriptor(void* A, void* B) = 0; // +0x44 LAB_06203270, -1
        virtual GENRESULT DACOM_API GetParameterValue(void* A, void* B) = 0; // +0x48 LAB_06203280, -1
        virtual GENRESULT DACOM_API SetParameterValue(void* A, void* B) = 0; // +0x4C LAB_06203290, -1
        // +0x50 LAB_06203340 -> "ALMaterial". Pops no stack argument, so __thiscall, as the
        // shared IMaterial::ClassName (render/IMaterial.h) declares the same slot.
        virtual const char* __thiscall GetClassName() = 0;
    };

    class FALMaterial final : public IALMaterial
    {
    public:
        FALMaterial() : Facet_(this), RefCount_(1) {}

        FALBatchedMaterialFacet* Facet() { return &Facet_; }

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
        unsigned long DACOM_API AddRef() override { return ++RefCount_; }
        unsigned long DACOM_API Release() override;

        GENRESULT DACOM_API Initialize(void* A) override;
        GENRESULT DACOM_API LoadFromFileSystem(void* A) override;
        GENRESULT DACOM_API SaveProperties(void* A) override;
        GENRESULT DACOM_API ApplyVerify(void* A, void* B) override;
        GENRESULT DACOM_API ResolveTextures(void* A) override;
        GENRESULT DACOM_API Bind(void* A) override;
        GENRESULT DACOM_API Teardown(void* A) override;
        GENRESULT DACOM_API Submit(void* A, void* B, void* C, void* D, void* E, void* F,
                                   void* G) override;
        GENRESULT DACOM_API SetName(void* A) override;
        GENRESULT DACOM_API GetName(void* A, void* B) override;
        GENRESULT DACOM_API CopyClassName(void* A, void* B) override;
        GENRESULT DACOM_API GetPassCount(unsigned long* Out) override;
        GENRESULT DACOM_API GetRenderFlags(unsigned long* Out) override;
        GENRESULT DACOM_API GetParameterCount(unsigned long* Out) override;
        GENRESULT DACOM_API GetParameterDescriptor(void* A, void* B) override;
        GENRESULT DACOM_API GetParameterValue(void* A, void* B) override;
        GENRESULT DACOM_API SetParameterValue(void* A, void* B) override;
        const char* __thiscall GetClassName() override;

        // LAB_06203300 / LAB_06203320 call the same render/texture-stage
        // state block as BeginDraw (FUN_0623f450), then return success.
        static GENRESULT ApplyDefaultRenderPipelineState();

    private:
        // Both interface identities share RefCount_; the facet keeps an outer pointer
        // instead of relying on the retail layout (deviation).
        FALBatchedMaterialFacet Facet_;
        unsigned long RefCount_;
    };

    // The global instance (DAT_06257920/24/28) built by LAB_062031b0 at DLL load.
    extern FALMaterial G_DefaultALMaterial;

} // namespace AlchemyImpl
