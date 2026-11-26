// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Common/Graphics/fl_fx_mesh.h"

#include "Common/client_cliobj.h"     // Archetype::ReleaseMaterialEntrySlot (FUN_0041b030)
#include "Common/Graphics/client_mesh_load.h"  // FMeshHolder (retail ObjectHandle) and its FUN_00420xxx helpers

#include "alchemy.h"           // IFxMesh, IFxMeshFactory
#include "common_archetype.h"  // Archetype::Load
#include "DALib.h"
#include "dacom.h"
#include "engine/engbase.h"
#include "render/IRenderPipeline.h"

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>

IFxMeshFactory* G_FLFxMeshFactory = nullptr;

namespace
{
    // Retail keeps DAT_00674cdc as a DACOM smart pointer: static initializer LAB_004f6f80 zeroes
    // it and registers LAB_004f6fa0 through atexit. This guard's destructor is that atexit entry.
    struct FFxMeshFactoryAtExit
    {
        ~FFxMeshFactoryAtExit()
        {
            if (G_FLFxMeshFactory != nullptr)
            {
                G_FLFxMeshFactory->Release();
                G_FLFxMeshFactory = nullptr;
            }
        }
    };
    FFxMeshFactoryAtExit G_FxMeshFactoryAtExit;

    // FUN_004f6ac0's destination: a length and 32 characters, 0x24 bytes.
    struct FFxMeshPath
    {
        unsigned Length; // +0x00
        char Text[32];   // +0x04
    };
    static_assert(sizeof(FFxMeshPath) == 0x24, "FUN_004f6ac0 string is 0x24 bytes");

    // s_fx\misc\%s.3db_005d98d0.
    char const kFxMeshPathFormat[] = "fx\\misc\\%s.3db";

    // FUN_004f6ac0 (cdecl Out, Format, ...): vsprintf into a 0x20-byte local (unbounded).
    void FxMeshPathFormat(FFxMeshPath* Out, char const* Format, ...)
    {
        char Buffer[0x20];
        va_list Args;
        va_start(Args, Format);
        vsprintf(Buffer, Format, Args);
        va_end(Args);
        size_t Length = strlen(Buffer);
        if (Length >= 0x20)
            Length = 0x1f;
        memcpy(Out->Text, Buffer, Length);
        Out->Text[Length] = '\0';
        Out->Length = static_cast<unsigned>(Length);
    }

    // The two render-state guards FUN_004f6970 keeps on its stack (EH states 0 and 1).
    struct FFxMeshRenderState
    {
        IRenderPipeline* Pipe;
        int State;
        int Saved;

        explicit FFxMeshRenderState(int InState)
            : Pipe(DALib::Pipeline), State(InState), Saved(0)
        {
            Pipe->GetRenderState((D3DRENDERSTATETYPE)State, &Saved);
        }

        ~FFxMeshRenderState()
        {
            Pipe->SetRenderState((D3DRENDERSTATETYPE)State, Saved);
        }
    };

    struct FLFxMesh : IFxMesh, FMeshHolder
    {
        ~FLFxMesh() override;

        GENRESULT DACOM_API SetTransform(const Transform* Xform) override;
        const Transform* DACOM_API GetTransform() override;
        GENRESULT DACOM_API Update(void* Arg) override;
        GENRESULT DACOM_API Render(void* Camera) override;
        GENRESULT DACOM_API Destroy() override;
        GENRESULT DACOM_API Slot8(void* Arg) override;
    };
    static_assert(offsetof(FLFxMesh, EngineInstance) == 0x08, "FLFxMesh +0x04 ObjectHandle base");
    static_assert(sizeof(FLFxMesh) == 0x14, "FLFxMesh base is 0x14");

    struct FLFxMeshComponent : FLFxMesh
    {
        unsigned long RefCount; // +0x14 DAComponent<FLFxMesh>

        FLFxMeshComponent() : RefCount(1) {}

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
        unsigned long DACOM_API AddRef() override;
        unsigned long DACOM_API Release() override;
    };
    static_assert(offsetof(FLFxMeshComponent, EngineInstance) == 0x08, "FLFxMeshComponent instance +0x08");
    static_assert(offsetof(FLFxMeshComponent, RefCount) == 0x14, "FLFxMeshComponent reference count +0x14");
    static_assert(sizeof(FLFxMeshComponent) == 0x18, "FLFxMeshComponent is operator new(0x18)");

    FLFxMesh::~FLFxMesh()
    {
        // FUN_004f6b50 (entered on the ObjectHandle face): FUN_004f6a70 called directly (not
        // through the vtable), then FUN_00420d10.
        FLFxMesh::Destroy();
    }

    GENRESULT DACOM_API FLFxMeshComponent::QueryInterface(const char* Iid, void** Out)
    {
        // LAB_004f6c20: the one-entry table at 005d9960 {"1.11_IFxMesh", offset 0}.
        if (strcmp(Iids::FxMesh, Iid) == 0)
        {
            AddRef();
            *Out = static_cast<IFxMesh*>(this);
            return GR_OK;
        }
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    unsigned long DACOM_API FLFxMeshComponent::AddRef()
    {
        // LAB_004f6cd0.
        RefCount += 1;
        return RefCount;
    }

    unsigned long DACOM_API FLFxMeshComponent::Release()
    {
        // LAB_004f6ce0: at zero the count is set back to 1 before the scalar deleting destructor
        // (FUN_004f6bc0) runs.
        if (RefCount > 0)
            RefCount -= 1;
        if (RefCount != 0)
            return RefCount;
        RefCount = 1;
        delete this;
        return 0;
    }

    GENRESULT DACOM_API FLFxMesh::SetTransform(const Transform* Xform)
    {
        // LAB_004f6910 (RET 8).
        ClientMeshHolderSetMatrix(this, Xform);
        return GR_OK;
    }

    const Transform* DACOM_API FLFxMesh::GetTransform()
    {
        // LAB_004f6930 (RET 4).
        return ClientMeshHolderGetTransform(this);
    }

    GENRESULT DACOM_API FLFxMesh::Update(void* Arg)
    {
        // LAB_004f6940 (RET 8).
        DALib::Engine->UpdateInstance(EngineInstance, 0, Arg);
        return GR_OK;
    }

    GENRESULT DACOM_API FLFxMesh::Render(void* Camera)
    {
        // FUN_004f6970 (RET 8). D3DRS_ZWRITEENABLE (0xe) off.
        FFxMeshRenderState ZWrite(0xe);
        ZWrite.Pipe->SetRenderState((D3DRENDERSTATETYPE)ZWrite.State, 0);

        // D3DRS_ZFUNC (0x17) = D3DCMP_LESSEQUAL.
        FFxMeshRenderState ZFunc(0x17);
        ZFunc.Pipe->SetRenderState((D3DRENDERSTATETYPE)ZFunc.State, 4);

        ClientMeshHolderRender(this, Camera, 0x100u);

        // The guards restore ZFUNC, then ZWRITEENABLE.
        return GR_OK;
    }

    GENRESULT DACOM_API FLFxMesh::Destroy()
    {
        // FUN_004f6a70 (RET 4). The auxiliary material entry is left alone.
        Archetype::ReleaseMaterialEntrySlot(&MaterialEntry);
        if (EngineInstance != InvalidInstanceIndex)
        {
            DALib::Engine->DestroyInstance(EngineInstance);
            EngineInstance = InvalidInstanceIndex;
        }
        return GR_OK;
    }

    GENRESULT DACOM_API FLFxMesh::Slot8(void* /*Arg*/)
    {
        // LAB_004f6ab0.
        return GR_GENERIC;
    }

    struct FLFxMeshFactory : IFxMeshFactory
    {
        virtual ~FLFxMeshFactory() {} // FUN_004f6bf0; private implementation slot 4.

        GENRESULT DACOM_API CreateMesh(void* ArgA, void* ArgB, float const* Position,
                                  IFxMesh** Out) override;
    };
    static_assert(sizeof(FLFxMeshFactory) == 0x4, "FLFxMeshFactory abstract base is 0x4");

    struct FLFxMeshFactoryComponent : FLFxMeshFactory
    {
        unsigned long RefCount; // +0x04 DAComponent<FLFxMeshFactory>

        FLFxMeshFactoryComponent() : RefCount(1) {}

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
        unsigned long DACOM_API AddRef() override;
        unsigned long DACOM_API Release() override;
    };
    static_assert(offsetof(FLFxMeshFactoryComponent, RefCount) == 0x4, "FLFxMeshFactoryComponent reference count +0x4");
    static_assert(sizeof(FLFxMeshFactoryComponent) == 0x8, "FLFxMeshFactoryComponent is operator new(8)");

    GENRESULT DACOM_API FLFxMeshFactoryComponent::QueryInterface(const char* Iid, void** Out)
    {
        // LAB_004f6d10: the one-entry table at 005d9990 {"1.11_IFxMeshFactory", offset 0}.
        if (strcmp(Iids::FxMeshFactory, Iid) == 0)
        {
            AddRef();
            *Out = static_cast<IFxMeshFactory*>(this);
            return GR_OK;
        }
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    unsigned long DACOM_API FLFxMeshFactoryComponent::AddRef()
    {
        // LAB_0051e8e0 (shared with other DAComponent<> instantiations whose count is at +4).
        RefCount += 1;
        return RefCount;
    }

    unsigned long DACOM_API FLFxMeshFactoryComponent::Release()
    {
        // LAB_004f6dc0: as FLFxMesh::Release, but the destructor is this face's own slot 4
        // (FUN_004f6b30).
        if (RefCount > 0)
            RefCount -= 1;
        if (RefCount != 0)
            return RefCount;
        RefCount = 1;
        delete this;
        return 0;
    }

    GENRESULT DACOM_API FLFxMeshFactory::CreateMesh(void* /*ArgA*/, void* /*ArgB*/,
                                               float const* Position, IFxMesh** Out)
    {
        // FUN_004f6810 (RET 0x14). ArgA and ArgB (alchemy's appearance +0x60 / +0x74) are never
        // read. A failed allocation leaves a null mesh that the success path below would
        // dereference, as in retail.
        FLFxMeshComponent* const Mesh = new (std::nothrow) FLFxMeshComponent;

        // Retail bug kept: "fx\misc\%s.3db" is formatted with the empty path object itself
        // copied by value as the variadic argument, so %s reads its first dword, 0: the name is
        // always "(null)".
        FFxMeshPath Path;
        Path.Length = 0;
        Path.Text[0] = '\0';
        FxMeshPathFormat(&Path, kFxMeshPathFormat, Path);

        // Failure returns 0 with *Out untouched and the new mesh never released.
        long ArchHandle = 0;
        if (!Archetype::Load(Path.Text, &ArchHandle, nullptr))
            return GR_OK;

        *Out = Mesh;
        const ArchetypeIndex Loaded = ArchHandle;
        ClientMeshHolderSetMaterials(Mesh, Path.Text, nullptr);
        ClientMeshHolderBindInstance(Mesh, Loaded);
        ClientMeshHolderSetPosition(Mesh, reinterpret_cast<Vector const*>(Position));
        DALib::Engine->ReleaseHandle(Loaded);
        return GR_OK;
    }
}

bool FLFxMeshFactoryCreate(IFxMeshFactory** Out)
{
    // FUN_004f67e0 (vtable 005d98bc). True even when the allocation fails.
    FLFxMeshFactoryComponent* const Factory = new (std::nothrow) FLFxMeshFactoryComponent;
    *Out = Factory;
    return true;
}
