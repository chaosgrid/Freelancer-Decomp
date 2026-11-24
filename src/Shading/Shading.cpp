// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Material.h"
#include "MaterialAnimation.h"
#include "MaterialBatcher.h"
#include "MaterialLibrary.h"
#include "ShadingInternal.h"
#include "TextureLibrary.h"

namespace
{
    // The part every Shading component factory shares: retail's 0xC-byte object {vtable,
    // reference count, registered name}. QueryInterface, AddRef and Release are one body for all
    // 21 factory classes; each class adds only its CreateInstance.
    class FComponentFactory : public IComponentFactory
    {
    public:
        explicit FComponentFactory(const char* Name) : RefCount(1), Name(Name) {}

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override
        {
            // LAB_06eb1050: mandatory output, exact IID including terminator.
            *Out = nullptr;
            if (std::strncmp(Iid, Iids::ComponentFactory, sizeof(Iids::ComponentFactory)) != 0)
                return GR_INTERFACE_UNSUPPORTED;
            *Out = (IComponentFactory*)this;
            AddRef();
            return GR_OK;
        }

        unsigned long DACOM_API AddRef() override { return ++RefCount; } // 06ecb930

        unsigned long DACOM_API Release() override
        {
            // FUN_06eb1090: saturates at zero. A factory has no destructor; the block is freed
            // directly.
            if (RefCount > 0)
                --RefCount;
            const unsigned long Next = RefCount;
            if (Next == 0)
            {
                RefCount = 1;
                ::operator delete(this);
            }
            return Next;
        }

        unsigned long RefCount; // +4
        const char* Name;       // +8  the registered component name
    };
    static_assert(sizeof(FComponentFactory) == 0xC, "a retail factory is 0xC bytes");

    // The sixteen material factory classes, one per material class. Each retail CreateInstance is
    // this body with the class's constructor inlined: Atmosphere 06eb10c0, BtDetailMap 06eb4840,
    // BtDetailMapTwo 06eb5430, DcDtEt 06eb5c70, DetailMap2Pass 06eb6670, DetailMap 06eb7b90,
    // EcEt 06eb8630, EcEtTwo 06eb8c40, IllumDetailMap 06eb9260, LightMap 06eba400,
    // Masked2DetailMap 06ebaca0, Nebula 06ebc2b0, NebulaTwo 06ebc800, Null 06ebce10,
    // SinglePass (the four DcDt names) 06ebd2d0, SinglePassTwo (the four DcDt*Two names) 06ebde50.
    template <class TMaterial>
    class TMaterialFactory final : public FComponentFactory
    {
    public:
        using FComponentFactory::FComponentFactory;

        GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Instance) override
        {
            FMaterialAggregateDesc* MaterialDesc = (FMaterialAggregateDesc*)Desc;
            if (MaterialDesc->Size != sizeof(FMaterialAggregateDesc) ||
                std::strcmp(MaterialDesc->NameAt4, Name) != 0)
            {
                *Instance = nullptr;
                return GR_INTERFACE_UNSUPPORTED;
            }
            TMaterialObject<TMaterial>* Material = new (std::nothrow) TMaterialObject<TMaterial>();
            if (Material == nullptr)
            {
                *Instance = nullptr;
                return GR_OUT_OF_MEMORY;
            }
            Material->ConfigureAggregation(MaterialDesc);
            const GENRESULT Result = Material->InitializeFromDescriptor(MaterialDesc);
            if (Result != GR_OK)
            {
                if (MaterialDesc->InnerOutAtC != nullptr)
                    *MaterialDesc->InnerOutAtC = nullptr;
                Material->ScalarDeletingDestructor(1);
                *Instance = nullptr;
                return Result;
            }
            *Instance = (IMaterial*)Material;
            return Result;
        }
    };

    // "BatchedMaterial" (factory vtable 06ed1310).
    class FBatchedMaterialFactory final : public FComponentFactory
    {
    public:
        using FComponentFactory::FComponentFactory;

        GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Instance) override
        {
            // 06eb4530: FUN_06eb3e80 runs after the controller is published.
            FMaterialAggregateDesc* BatchedDesc = (FMaterialAggregateDesc*)Desc;
            if (BatchedDesc->Size != sizeof(FMaterialAggregateDesc) ||
                std::strcmp(BatchedDesc->NameAt4, Name) != 0)
            {
                *Instance = nullptr;
                return GR_INTERFACE_UNSUPPORTED;
            }
            FBatchedMaterial* Batched = new (std::nothrow) FBatchedMaterial();
            if (Batched == nullptr)
            {
                *Instance = nullptr;
                return GR_OUT_OF_MEMORY;
            }
            Batched->ConfigureAggregation(BatchedDesc);
            const GENRESULT Result = Batched->InitializeFromDescriptor(BatchedDesc);
            if (Result != GR_OK)
            {
                if (BatchedDesc->InnerOutAtC != nullptr)
                    *BatchedDesc->InnerOutAtC = nullptr;
                delete Batched;
                *Instance = nullptr;
                return Result;
            }
            *Instance = (IMaterial*)Batched;
            return Result;
        }
    };

    // "TextureLibrary" (factory vtable 06ed29ec).
    class FTextureLibraryFactory final : public FComponentFactory
    {
    public:
        using FComponentFactory::FComponentFactory;

        GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Instance) override
        {
            // FUN_06ec6760: descriptor initialization is distinct from aggregate Initialize.
            FSystemAggDesc* TextureDesc = (FSystemAggDesc*)Desc;
            if (TextureDesc->Size != sizeof(FSystemAggDesc) ||
                std::strcmp(TextureDesc->Name, Name) != 0)
            {
                *Instance = nullptr;
                return GR_INTERFACE_UNSUPPORTED;
            }
            FTextureLibrary* Texture = new (std::nothrow) FTextureLibrary();
            if (Texture == nullptr)
            {
                *Instance = nullptr;
                return GR_OUT_OF_MEMORY;
            }
            Texture->Outer = TextureDesc->Outer;
            if (Texture->Outer != nullptr)
                *TextureDesc->InnerOut = (IDAComponent*)(&Texture->Controller);
            const GENRESULT Result = Texture->InitializeFromDescriptor(TextureDesc);
            if (Result != GR_OK)
            {
                if (TextureDesc->InnerOut != nullptr)
                    *TextureDesc->InnerOut = nullptr;
                delete Texture;
                *Instance = nullptr;
                return Result;
            }
            *Instance = (ITextureLibrary2*)Texture;
            return Result;
        }
    };

    // "MaterialAnimation" (factory vtable 06ed2b2c).
    class FMaterialAnimationFactory final : public FComponentFactory
    {
    public:
        using FComponentFactory::FComponentFactory;

        GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Instance) override
        {
            // LAB_06ec6d40.
            FMaterialAggregateDesc* AnimationDesc = (FMaterialAggregateDesc*)Desc;
            if (AnimationDesc->Size != sizeof(FMaterialAggregateDesc) ||
                std::strcmp(AnimationDesc->NameAt4, Name) != 0)
            {
                *Instance = nullptr;
                return GR_INTERFACE_UNSUPPORTED;
            }
            FMaterialAnimation* Animation = new (std::nothrow) FMaterialAnimation();
            if (Animation == nullptr)
            {
                *Instance = nullptr;
                return GR_OUT_OF_MEMORY;
            }
            Animation->Outer = AnimationDesc->OuterAt8;
            if (Animation->Outer != nullptr)
                *AnimationDesc->InnerOutAtC = (IDAComponent*)(&Animation->Controller);
            *Instance = (IMatAnimLibrary*)Animation;
            return GR_OK;
        }
    };

    // "MaterialBatcher" (factory vtable 06ed2c1c).
    class FMaterialBatcherFactory final : public FComponentFactory
    {
    public:
        using FComponentFactory::FComponentFactory;

        GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Instance) override
        {
            // FUN_06ecb940: controller publication precedes descriptor init.
            FMaterialAggregateDesc* BatcherDesc = (FMaterialAggregateDesc*)Desc;
            if (BatcherDesc->Size != sizeof(FMaterialAggregateDesc) ||
                std::strcmp(BatcherDesc->NameAt4, Name) != 0)
            {
                *Instance = nullptr;
                return GR_INTERFACE_UNSUPPORTED;
            }
            FMaterialBatcher* Batcher = new (std::nothrow) FMaterialBatcher();
            if (Batcher == nullptr)
            {
                *Instance = nullptr;
                return GR_OUT_OF_MEMORY;
            }
            Batcher->ControllingUnknown = BatcherDesc->OuterAt8 != nullptr
                ? BatcherDesc->OuterAt8 : (IDAComponent*)(&Batcher->Controller);
            if (BatcherDesc->OuterAt8 != nullptr)
                *BatcherDesc->InnerOutAtC = (IDAComponent*)(&Batcher->Controller);
            const GENRESULT Result = Batcher->InitializeFromDescriptor(BatcherDesc);
            if (Result != GR_OK)
            {
                if (BatcherDesc->InnerOutAtC != nullptr)
                    *BatcherDesc->InnerOutAtC = nullptr;
                delete Batcher;
                *Instance = nullptr;
                return Result;
            }
            *Instance = (IMaterialBatcher2*)Batcher;
            return Result;
        }
    };

    // "MaterialLibrary" (factory vtable 06ed2e60).
    class FMaterialLibraryFactory final : public FComponentFactory
    {
    public:
        using FComponentFactory::FComponentFactory;

        GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Instance) override
        {
            // 06eceff0: the nondelegating face is published only when aggregated;
            // descriptor initialization runs either way.
            FMaterialAggregateDesc* LibraryDesc = (FMaterialAggregateDesc*)Desc;
            if (LibraryDesc->Size != sizeof(FMaterialAggregateDesc) ||
                std::strcmp(LibraryDesc->NameAt4, Name) != 0)
            {
                *Instance = nullptr;
                return GR_INTERFACE_UNSUPPORTED;
            }
            FMaterialLibrary* Library = new (std::nothrow) FMaterialLibrary();
            if (Library == nullptr)
            {
                *Instance = nullptr;
                return GR_OUT_OF_MEMORY;
            }
            Library->ControllingUnknown = LibraryDesc->OuterAt8 != nullptr
                ? LibraryDesc->OuterAt8 : (IDAComponent*)(&Library->Controller);
            if (LibraryDesc->OuterAt8 != nullptr)
                *LibraryDesc->InnerOutAtC = (IDAComponent*)(&Library->Controller);
            const GENRESULT Result = Library->InitializeFromDescriptor(LibraryDesc);
            if (Result != GR_OK)
            {
                if (LibraryDesc->InnerOutAtC != nullptr)
                    *LibraryDesc->InnerOutAtC = nullptr;
                delete Library;
                *Instance = nullptr;
                return Result;
            }
            *Instance = (IMaterialLibrary*)Library;
            return Result;
        }
    };

    // 06ec3810: only the texture registration acquires before allocation.
    void RegisterTextureLibraryComponent()
    {
        ICOManager* Manager = DACOM_Acquire();
        if (Manager == nullptr)
            return;
        FTextureLibraryFactory* Factory = new (std::nothrow) FTextureLibraryFactory("TextureLibrary");
        if (Factory == nullptr)
            return;
        // Opaque priority token: keep const removal explicit.
        Manager->RegisterComponent(Factory, Factory->Name, const_cast<void*>(kRegisterPriority));
        Factory->Release();
    }

    // Each of the 26 registration callees of FUN_06ecf440 is this body with its own factory
    // class and name; they allocate before acquiring the manager.
    template <class TFactory>
    bool RegisterComponent(const char* Name)
    {
        TFactory* Factory = new (std::nothrow) TFactory(Name);
        if (Factory == nullptr)
            return false;
        ICOManager* Manager = DACOM_Acquire();
        // Opaque priority token: keep const removal explicit.
        Manager->RegisterComponent(Factory, Factory->Name, const_cast<void*>(kRegisterPriority));
        Factory->Release();
        return true;
    }

    template <class TMaterial>
    bool RegisterMaterial(const char* Name)
    {
        return RegisterComponent<TMaterialFactory<TMaterial>>(Name);
    }

    void RegisterMaterialComponents()
    {
        // FUN_06ecf440, in retail order; each line's address is its registration function.
        RegisterComponent<FMaterialLibraryFactory>("MaterialLibrary");              // 06ecbd20
        RegisterComponent<FMaterialBatcherFactory>("MaterialBatcher");              // 06ec83f0
        RegisterComponent<FMaterialAnimationFactory>("MaterialAnimation");          // 06ec6cf0
        RegisterComponent<FBatchedMaterialFactory>("BatchedMaterial");              // 06eb3dc0
        RegisterMaterial<FSinglePassMaterial>("DcDtEcOcOt");                        // 06ebd140
        RegisterMaterial<FSinglePassTwoMaterial>("DcDtEcOcOtTwo");                  // 06ebd280
        RegisterMaterial<FSinglePassMaterial>("DcDtEc");                            // 06ebd0a0
        RegisterMaterial<FSinglePassTwoMaterial>("DcDtEcTwo");                      // 06ebd1e0
        RegisterMaterial<FDcDtEtMaterial>("DcDtEt");                                // 06eb5c20
        RegisterMaterial<FSinglePassMaterial>("DcDt");                              // 06ebd050
        RegisterMaterial<FSinglePassTwoMaterial>("DcDtTwo");                        // 06ebd190
        RegisterMaterial<FSinglePassMaterial>("DcDtOcOt");                          // 06ebd0f0
        RegisterMaterial<FSinglePassTwoMaterial>("DcDtOcOtTwo");                    // 06ebd230
        RegisterMaterial<FEcEtMaterial>("EcEt");                                    // 06eb8590
        RegisterMaterial<FEcEtTwoMaterial>("EcEtTwo");                              // 06eb85e0
        ActivateMaterialGlobalRecord(2);
        ActivateMaterialGlobalRecord(1);

        RegisterMaterial<FAtmosphereMaterial>("AtmosphereMaterial");                // 06eb1000
        RegisterMaterial<FNebulaMaterial>("Nebula");                                // 06ebc210
        RegisterMaterial<FNebulaTwoMaterial>("NebulaTwo");                          // 06ebc260
        RegisterMaterial<FNullMaterial>("NullMaterial");                            // 06ebcdc0
        RegisterMaterial<FLightMapMaterial>("LightMap");                            // 06eba3b0
        ActivateMaterialGlobalRecord(3);

        RegisterMaterial<FDetailMapMaterial>("DetailMapMaterial");                  // 06eb7b40
        RegisterMaterial<FDetailMap2PassMaterial>("DetailMap2Dm1Msk2PassMaterial"); // 06eb6620
        RegisterMaterial<FMasked2DetailMapMaterial>("Masked2DetailMapMaterial");    // 06ebac50
        RegisterMaterial<FIllumDetailMapMaterial>("IllumDetailMapMaterial");        // 06eb9210
        ActivateMaterialGlobalRecord(0);
        RegisterMaterial<FBtDetailMapMaterial>("BtDetailMapMaterial");              // 06eb47a0
        RegisterMaterial<FBtDetailMapTwoMaterial>("BtDetailMapTwoMaterial");        // 06eb47f0
    }
}

void RegisterShadingComponents()
{
    RegisterTextureLibraryComponent();
    RegisterMaterialComponents();
}

BOOL WINAPI DllMain(HINSTANCE Instance, DWORD Reason, LPVOID /*Reserved*/)
{
    if (Reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(Instance);
        RegisterShadingComponents();
    }
    return TRUE;
}
