// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "alchemy_material.h"

#include "alchemy_alchemy.h"
#include "alchemy_common.h"

// alchemy.dll — DACOM particle FX scaffolding.
// Out-of-line method bodies for FALMaterial / FALBatchedMaterialFacet
// (see alchemy_material.h).
namespace AlchemyImpl
{
    FALMaterial G_DefaultALMaterial;

    // ---- FALBatchedMaterialFacet ("1.11_IBatchedMaterial", object+4) -----

    GENRESULT DACOM_API FALBatchedMaterialFacet::QueryInterface(const char* Iid, void** Out)
    {
        // LAB_06203440: this -= 4, then the outer QI (LAB_06203350).
        return Outer_->QueryInterface(Iid, Out);
    }

    unsigned long DACOM_API FALBatchedMaterialFacet::AddRef()
    {
        // LAB_06203450 -> LAB_06203400.
        return Outer_->AddRef();
    }

    unsigned long DACOM_API FALBatchedMaterialFacet::Release()
    {
        // LAB_06203460 -> LAB_06203410.
        return Outer_->Release();
    }

    GENRESULT DACOM_API FALBatchedMaterialFacet::CreateInnerMaterial(void* /*A*/)
    {
        // LAB_062032a0.
        return -1;
    }

    GENRESULT DACOM_API FALBatchedMaterialFacet::SetBatchBin(void* /*A*/)
    {
        // LAB_062032b0.
        return -1;
    }

    GENRESULT DACOM_API FALBatchedMaterialFacet::GetBatchBin(unsigned long* Out)
    {
        // LAB_062032c0.
        *Out = 1;
        return GR_OK;
    }

    GENRESULT DACOM_API FALBatchedMaterialFacet::Bind(void* /*A*/)
    {
        // LAB_06203300.
        return FALMaterial::ApplyDefaultRenderPipelineState();
    }

    GENRESULT DACOM_API FALBatchedMaterialFacet::Teardown(void* /*A*/)
    {
        // LAB_06203310.
        return GR_OK;
    }

    GENRESULT DACOM_API FALBatchedMaterialFacet::Submit(void* /*A*/, void* /*B*/, void* /*C*/,
                                                        void* /*D*/, void* /*E*/, void* /*F*/,
                                                        void* /*G*/)
    {
        // LAB_062032f0.
        return GR_OK;
    }

    // ---- FALMaterial ("1.11_IMaterial", object+0) -------------------------

    GENRESULT DACOM_API FALMaterial::QueryInterface(const char* Iid, void** Out)
    {
        // LAB_06203350: a 2-entry IID table walk ("1.11_IBatchedMaterial" -> offset 4,
        // "1.11_IMaterial" -> offset 0).
        if (IidEqual(Iid, Iids::BatchedMaterial))
        {
            Facet_.AddRef();
            *Out = static_cast<IALBatchedMaterial*>(&Facet_);
            return GR_OK;
        }
        if (IidEqual(Iid, Iids::Material))
        {
            AddRef();
            *Out = static_cast<IALMaterial*>(this);
            return GR_OK;
        }
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    unsigned long DACOM_API FALMaterial::Release()
    {
        // LAB_06203410: re-arms the count to 1 at zero before freeing (FUN_062460e0).
        if (RefCount_ != 0)
            --RefCount_;
        const unsigned long Next = RefCount_;
        if (Next == 0)
        {
            RefCount_ = 1;
            delete this;
            return 0;
        }
        return Next;
    }

    GENRESULT DACOM_API FALMaterial::Initialize(void* /*A*/)
    {
        // LAB_062031d0.
        return GR_OK;
    }

    GENRESULT DACOM_API FALMaterial::LoadFromFileSystem(void* /*A*/)
    {
        // LAB_062031e0.
        return GR_OK;
    }

    GENRESULT DACOM_API FALMaterial::SaveProperties(void* /*A*/)
    {
        // LAB_062031f0.
        return GR_OK;
    }

    GENRESULT DACOM_API FALMaterial::ApplyVerify(void* /*A*/, void* /*B*/)
    {
        // LAB_06203200.
        return GR_OK;
    }

    GENRESULT DACOM_API FALMaterial::ResolveTextures(void* /*A*/)
    {
        // LAB_06203210.
        return GR_OK;
    }

    GENRESULT DACOM_API FALMaterial::Bind(void* /*A*/)
    {
        // LAB_06203320.
        return ApplyDefaultRenderPipelineState();
    }

    GENRESULT DACOM_API FALMaterial::Teardown(void* /*A*/)
    {
        // LAB_06203330.
        return GR_OK;
    }

    GENRESULT DACOM_API FALMaterial::Submit(void* /*A*/, void* /*B*/, void* /*C*/, void* /*D*/,
                                            void* /*E*/, void* /*F*/, void* /*G*/)
    {
        // LAB_062032e0.
        return -1;
    }

    GENRESULT DACOM_API FALMaterial::SetName(void* /*A*/)
    {
        // LAB_06203220.
        return -1;
    }

    GENRESULT DACOM_API FALMaterial::GetName(void* /*A*/, void* /*B*/)
    {
        // LAB_06203230.
        return -1;
    }

    GENRESULT DACOM_API FALMaterial::CopyClassName(void* /*A*/, void* /*B*/)
    {
        // LAB_06203240.
        return -1;
    }

    GENRESULT DACOM_API FALMaterial::GetPassCount(unsigned long* Out)
    {
        // LAB_06203250.
        *Out = 1;
        return GR_OK;
    }

    GENRESULT DACOM_API FALMaterial::GetRenderFlags(unsigned long* Out)
    {
        // LAB_062032d0.
        *Out = 2;
        return GR_OK;
    }

    GENRESULT DACOM_API FALMaterial::GetParameterCount(unsigned long* Out)
    {
        // LAB_06203260.
        *Out = 0;
        return GR_OK;
    }

    GENRESULT DACOM_API FALMaterial::GetParameterDescriptor(void* /*A*/, void* /*B*/)
    {
        // LAB_06203270.
        return -1;
    }

    GENRESULT DACOM_API FALMaterial::GetParameterValue(void* /*A*/, void* /*B*/)
    {
        // LAB_06203280.
        return -1;
    }

    GENRESULT DACOM_API FALMaterial::SetParameterValue(void* /*A*/, void* /*B*/)
    {
        // LAB_06203290.
        return -1;
    }

    const char* __thiscall FALMaterial::GetClassName()
    {
        // LAB_06203340 returns the class string without accessing the receiver.
        return "ALMaterial";
    }

    GENRESULT FALMaterial::ApplyDefaultRenderPipelineState()
    {
        // FUN_0623f450 = ApplyParticleDrawRenderState (alchemy_alchemy.cpp). Both retail
        // callers (LAB_06203300, LAB_06203320) discard its result and return 0.
        ApplyParticleDrawRenderState();
        return GR_OK;
    }

} // namespace AlchemyImpl
