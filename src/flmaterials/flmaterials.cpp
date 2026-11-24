// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "flmaterials.h"

#include <cstdio>
#include <cstring>
#include <new>

#pragma comment(lib, "dacom.lib")

unsigned long G_VisibleCounterA = 0;
unsigned long G_VisibleCounterB = 0;
unsigned long G_VisibleCounterC = 0;

void FlmVisibleCounterReset()
{
    G_VisibleCounterA = 0;
    G_VisibleCounterB = 0;
    G_VisibleCounterC = 0;
}

void FlmVisibleCounterCopy(unsigned long* Out3)
{
    Out3[0] = G_VisibleCounterA;
    Out3[1] = G_VisibleCounterB;
    Out3[2] = G_VisibleCounterC;
}

bool FlmIidEqual(const char* Left, const char* Right)
{
    if (Left == nullptr || Right == nullptr)
        return false;
    return std::strcmp(Left, Right) == 0;
}

bool FlmNameEqual(const char* Left, const char* Right)
{
    if (Left == nullptr || Right == nullptr)
        return false;
    return std::strcmp(Left, Right) == 0;
}

void FlmFdump(int Flags, const char* File, int Line, const char* Kind, const char* Message)
{
    const char* Format = "%s(%d) : ERROR:General:%s";
    if (Flags == kFdumpTrace1)
        Format = "%s(%d) : TRACE_1:General:%s";
    else if (Flags == kFdumpNotice)
        Format = "%s(%d) : NOTICE:General:%s";
    (void)Kind;
    if (FDUMP != nullptr)
        FDUMP(Flags, Format, File != nullptr ? File : "flmaterials.cpp", Line,
              Message != nullptr ? Message : "");
}

namespace
{
    // The part every flmaterials component factory shares: retail's 0xC-byte object {vtable,
    // reference count, registered name}. QueryInterface, AddRef and Release are one body for all
    // 16 factory classes; each class adds only its CreateInstance.
    class FComponentFactory : public IComponentFactory
    {
    public:
        explicit FComponentFactory(const char* Name) : RefCount_(1), Name_(Name) {}

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override
        {
            // 066f44c0: the output pointer is mandatory.
            *Out = nullptr;
            if (!FlmIidEqual(Iid, Iids::ComponentFactory))
                return GR_INTERFACE_UNSUPPORTED;
            *Out = (IComponentFactory*)this;
            AddRef();
            return GR_OK;
        }

        unsigned long DACOM_API AddRef() override
        {
            // 066f63f0.
            RefCount_ += 1;
            return RefCount_;
        }

        unsigned long DACOM_API Release() override
        {
            // FUN_066f4500: saturates at zero. A factory has no destructor; the block is freed
            // directly.
            if (RefCount_ != 0)
                --RefCount_;
            const unsigned long Remaining = RefCount_;
            if (Remaining == 0)
            {
                RefCount_ = 1;
                ::operator delete(this);
            }
            return Remaining;
        }

        const char* Name() const { return Name_; }

    protected:
        unsigned long RefCount_; // +4
        const char* Name_;       // +8  the registered component name
    };
    static_assert(sizeof(FComponentFactory) == 0xC, "a retail factory is 0xC bytes");

    // The fifteen material factory classes, one per material class. Each retail CreateInstance is
    // this body with the class's constructor inlined: Backdrop 066f10c0, Cloak 066f3680,
    // Colorize 066f4530, EngineGlow 066f5220, EnvMap 066f5980, ExclusionZone 066f6590,
    // Glass 066f7040, HUDIcon 066f7ac0, Nomad 066f8340, PlanetWater 066f9740, Shadow 066fa280,
    // VisibleCounter 066fac40, Wire 066faf30, Wire2 066fbbc0, HUDAnim 066ffa10.
    template <class TMaterial>
    class TMaterialFactory final : public FComponentFactory
    {
    public:
        using FComponentFactory::FComponentFactory;

        GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Instance) override
        {
            // Validate size and name, construct the object and its controller, then Slot22.
            if (Desc->size != 0x14)
            {
                *Instance = nullptr;
                return GR_INTERFACE_UNSUPPORTED;
            }
            FMaterialAggregateDesc* Agg = (FMaterialAggregateDesc*)Desc;
            if (std::strcmp(Agg->NameAt4, Name_) != 0)
            {
                *Instance = nullptr;
                return GR_INTERFACE_UNSUPPORTED;
            }

            TFlmMaterialObject<TMaterial>* Object = new (std::nothrow) TFlmMaterialObject<TMaterial>();
            if (Object == nullptr)
            {
                *Instance = nullptr;
                return GR_OUT_OF_MEMORY;
            }
            Object->ConfigureAggregation(Agg);
            const GENRESULT Result = Object->InitializeFromDescriptor(Agg);
            if (Result != GR_OK)
            {
                if (Agg->InnerOutAtC != nullptr)
                    *Agg->InnerOutAtC = nullptr;
                Object->ScalarDeletingDestructor(1);
                *Instance = nullptr;
                return Result;
            }
            *Instance = (IMaterial*)Object;
            return Result;
        }
    };

    // "FLAppearance" (factory vtable 06702b94).
    class FAppearanceFactory final : public FComponentFactory
    {
    public:
        using FComponentFactory::FComponentFactory;

        GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Instance) override
        {
            // LAB_066fd830: descriptor and result pointer are mandatory.
            if (Desc->size != 0x18)
            {
                *Instance = nullptr;
                return GR_INTERFACE_UNSUPPORTED;
            }
            FAggDesc18* Agg = (FAggDesc18*)Desc;
            if (std::strcmp(Agg->NameAt4, Name_) != 0)
            {
                *Instance = nullptr;
                return GR_INTERFACE_UNSUPPORTED;
            }
            return FlmCreateAppearanceInstance(Agg, Instance);
        }
    };

    // Each of retail's nineteen material registration functions is this body with its own
    // factory class and name.
    template <class TMaterial>
    bool RegisterMaterial(const char* Name)
    {
        TMaterialFactory<TMaterial>* Factory = new (std::nothrow) TMaterialFactory<TMaterial>(Name);
        if (Factory == nullptr)
            return false;
        ICOManager* Manager = DACOM_Acquire();
        // Opaque priority token: keep const removal explicit.
        Manager->RegisterComponent(Factory, Factory->Name(), const_cast<void*>(kRegisterPriority));
        Factory->Release();
        return true;
    }

    void RegisterAll()
    {
        // FUN_066fcf30 captures this manager before the individual material wrappers.
        ICOManager* AppearanceManager = DACOM_Acquire();
        RegisterMaterial<FNomadMaterial>(G_NameNomadMaterial);               // 066f8280
        RegisterMaterial<FNomadMaterial>(G_NameNomadNoBendy);                // 066f82e0
        RegisterMaterial<FHudIconMaterial>(G_NameHudIconMaterial);           // 066f7a70
        RegisterMaterial<FHudAnimMaterial>(G_NameHudAnimMaterial);           // 066ff9c0
        RegisterMaterial<FColorizeMaterial>(G_NameColorizeMaterial);         // 066f4460
        RegisterMaterial<FCloakMaterial>(G_NameCloakMaterial);               // 066f3620
        RegisterMaterial<FWireMaterial>(G_NameWireMaterial);                 // 066faee0
        RegisterMaterial<FWire2Material>(G_NameWireMaterial2);               // 066fbb70
        RegisterMaterial<FExclusionZoneMaterial>(G_NameExclusionZoneMaterial); // 066f6530
        RegisterMaterial<FGlassMaterial>(G_NameGlassMaterial);               // 066f6f50
        RegisterMaterial<FGlassMaterial>(G_NameHighGlassMaterial);           // 066f6fa0
        RegisterMaterial<FGlassMaterial>(G_NameGfGlassMaterial);             // 066f6ff0
        RegisterMaterial<FEnvMapMaterial>(G_NameEnvMapMaterial);             // 066f5930
        RegisterMaterial<FBackdropMaterial>(G_NameBackdropMaterial);         // 066f1020
        RegisterMaterial<FBackdropMaterial>(G_NameStarsphereMaterial);       // 066f1070
        RegisterMaterial<FShadowMaterial>(G_NameShadowMaterial);             // 066fa230
        RegisterMaterial<FEngineGlowMaterial>(G_NameEngineGlowMaterial);     // 066f51d0
        RegisterMaterial<FVisibleCounterMaterial>(G_NameVisibleCounterMaterial); // 066fabf0
        RegisterMaterial<FPlanetWaterMaterial>(G_NamePlanetWaterMaterial);   // 066f96f0

        if (AppearanceManager != nullptr)
        {
            FAppearanceFactory* Appearance = new (std::nothrow) FAppearanceFactory(G_NameFlAppearance);
            if (Appearance != nullptr)
            {
                // Opaque priority token: keep const removal explicit.
                AppearanceManager->RegisterComponent(Appearance, G_NameFlAppearance,
                                                     const_cast<void*>(kRegisterPriority));
                Appearance->Release();
            }
        }
    }
}

BOOL WINAPI DllMain(HINSTANCE Instance, DWORD Reason, LPVOID /*Reserved*/)
{
    if (Reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(Instance);
        RegisterAll();
    }
    return TRUE;
}
