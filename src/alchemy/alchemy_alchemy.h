// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "alchemy_types.h"
#include "alchemy_common.h"
#include "alchemy_nodelibrary.h"

// alchemy.dll — DACOM particle FX scaffolding.
// FAlchemy: the IAlchemy / IFxNodeLibrary component.
namespace AlchemyImpl
{
    // FUN_0623f450: the fixed particle-draw render-state and texture-stage
    // block, applied to G_RenderPipeline (DAT_06257bc0). Retail
    // callers: FAlchemy::BeginDraw (0x06201b5e) and the two material Bind slots
    // in alchemy_material.cpp (LAB_06203300 / LAB_06203320).
    void ApplyParticleDrawRenderState();

    class FAlchemy final : public IAlchemy
    {
    public:
        // DLL-global callbacks set by LAB_062024c0 / LAB_062024d0.
        using FnUpdateCallback = void(__cdecl*)(unsigned long, void*, unsigned long);
        using FnFilterCallback = unsigned long(__cdecl*)(void*, void*);

        FAlchemy()
            : Inner_(this), Controller_(&Inner_), Attaches_(nullptr), AttachCount_(0),
              AttachCapacity_(0), InitNode_(nullptr)
        {
            SavedRsCullMode_ = 0;
            SavedRsAlphaBlend_ = 0;
            SavedRsSrcBlend_ = 0;
            SavedRsDestBlend_ = 0;
            SavedRsLighting_ = 0;
            SavedRsZWriteEnable_ = 0;
        }

        ~FAlchemy() noexcept(false);

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
        unsigned long DACOM_API AddRef() override;
        unsigned long DACOM_API Release() override;

        void SetController(IDAComponent* OuterUnknown)
        {
            Controller_ = OuterUnknown != nullptr ? OuterUnknown : &Inner_;
        }
        IDAComponent* InnerUnknown() { return &Inner_; }

        GENRESULT DACOM_API LoadNodeLibrary(IFileSystem* Filesys) override;
        GENRESULT DACOM_API SaveNodeLibrary(IFileSystem* Filesys) override;
        unsigned long DACOM_API HashNodeName(const char* Name) override
        {
            return HashNameCrc(Name);
        }
        // FUN_062274b0: name, prototype, optional output hash.
        GENRESULT DACOM_API InsertNode(const char* Name, IDAComponent* Node,
                                       unsigned long* OutHash) override;
        GENRESULT DACOM_API RemoveNodeByHash(unsigned long Hash) override;
        // FUN_06228b00: enumerate node-lib entries, Callback(Node, Context),
        // stop at first nonzero result.
        GENRESULT DACOM_API EnumerateNodes(FNodeLibraryEnumCallback Callback, void* Context) override;
        GENRESULT DACOM_API ClearNodeLibrary() override;
        // FUN_062277e0: MakeUniqueName-style "#N" collision avoidance.
        GENRESULT DACOM_API MakeUniqueName(void* Name, void* Out) override;
        // LAB_06227710: find prototype by hash, then CloneNode(Out).
        GENRESULT DACOM_API CreateNodeByHash(unsigned long Hash, IDAComponent** Out) override;
        // LAB_06227770: find CRC, write stored ptr, AddRef.
        GENRESULT DACOM_API FindNodeByHash(unsigned long Hash, IDAComponent** Out) override;

        GENRESULT DACOM_API CreateVertexBuffer() override;
        GENRESULT DACOM_API ReleaseVertexBuffer() override;
        GENRESULT DACOM_API ComposeNodeTransforms(void* A, void* B) override;
        GENRESULT DACOM_API UpdateNodes(void* A, void* B) override;
        GENRESULT DACOM_API DrawNodes(void* Cookie, void* StartNode) override;
        GENRESULT DACOM_API GetDrawCookie() override
        {
            return static_cast<GENRESULT>(G_DrawCookie);
        }
        GENRESULT DACOM_API CreateNamedNode(const char* Name, void* Unused) override;
        GENRESULT DACOM_API AttachNode(void* ParentRaw, void* ChildRaw) override;
        GENRESULT DACOM_API AttachNodeFiltered(void* A, void* B) override;
        GENRESULT DACOM_API DetachNode(void* Node) override;
        GENRESULT DACOM_API SubmitNode(void* Node) override;
        GENRESULT DACOM_API SubmitChildNodes(void* Node) override;
        GENRESULT DACOM_API GetRootNode(void** Out) override;
        // FUN_06202180: Out is written through; StartNode is never read.
        GENRESULT DACOM_API FindNode(void* Out, void* SearchKey, void* StartNode) override;
        GENRESULT DACOM_API SetUpdateCallback(void* Fn) override;
        GENRESULT DACOM_API SetFilterCallback(void* Fn) override;

        GENRESULT DACOM_API BeginDraw() override;
        GENRESULT DACOM_API EndDraw() override;

        GENRESULT DACOM_API SetUseMaterialBatcher(unsigned char Value) override
        {
            G_Defaults.bUseMaterialBatcher = Value;
            return GR_OK;
        }
        GENRESULT DACOM_API GetUseMaterialBatcher() override
        {
            return static_cast<GENRESULT>(G_Defaults.bUseMaterialBatcher);
        }
        GENRESULT DACOM_API EnqueueAppearance(void* ListIndexRaw, void* AppearanceRaw) override;
        void* DACOM_API GetVertexBufferHandle() override
        {
            return G_VbHandle;
        }
        GENRESULT DACOM_API SetDrawFlags(void* Pair) override;
        GENRESULT DACOM_API CopyCounters(void* Out9Dwords) override;

        GENRESULT DACOM_API Initialize(AGGDESC* Desc) override;

        GENRESULT DACOM_API Slot38() override { return GR_OK; }
        GENRESULT DACOM_API GetFlagA() override
        {
            return static_cast<GENRESULT>(G_DrawFlagA);
        }
        GENRESULT DACOM_API GetFlagB() override
        {
            return static_cast<GENRESULT>(G_DrawFlagB);
        }

    private:
        class FInnerUnknown final : public IDAComponent
        {
        public:
            explicit FInnerUnknown(FAlchemy* Owner) : RefCount_(1), Owner_(Owner) {}

            GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
            unsigned long DACOM_API AddRef() override;
            unsigned long DACOM_API Release() override;

        private:
            unsigned long RefCount_;
            FAlchemy* Owner_;
        };

        GENRESULT AcquirePipelineInterfaces(AGGDESC* Desc);
        void BindInitFxNode();
        IDAComponent* FindNodeEntry(unsigned long Hash);
        GENRESULT InsertNodeByName(const char* Name, IDAComponent* Node,
                                  unsigned long* OutHash = nullptr);
        void ClearNodeList();
        void ClearAttaches();
        // FUN_06202070: shared submit walk used by SubmitNode/SubmitChildNodes.
        void SubmitNodeChain(IDAComponent* Node);

        FInnerUnknown Inner_;
        IDAComponent* Controller_;
        FNodePrototypeRegistry NodeRegistry_;
        FAlchemyAttach* Attaches_;
        unsigned long AttachCount_;
        unsigned long AttachCapacity_;
        IDAComponent* InitNode_;
        int SavedRsCullMode_;
        int SavedRsAlphaBlend_;
        int SavedRsSrcBlend_;
        int SavedRsDestBlend_;
        int SavedRsLighting_;
        int SavedRsZWriteEnable_;
    };

} // namespace AlchemyImpl
