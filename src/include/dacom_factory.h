// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "dacom.h"
#include "dacom_iids.h"

#include <cstring>
#include <new>

// The DACOM component factory several modules register from DllMain: retail's 0xC-byte object
// {vtable, reference count, registered name}, created with a count of 1. Every module carries
// its own copy of the three IUnknown slots (each module's retail addresses are given at its
// factory class); the copies agree in effect:
//   QueryInterface  *Out = 0, then an exact compare of the IID with "1.11_IComponentFactory"
//                   (REPE CMPSB over the 0x17 bytes including the terminator; no null test of
//                   Iid or Out), then *Out = this and AddRef.
//   AddRef          ++RefCount, returned.
//   Release         RefCount saturates at zero; on zero it is reset to 1 and the block freed
//                   with operator delete (a factory has no destructor). Returns the new count.
// Derived factories add only CreateInstance (and, in some modules, members after +0xC).
class FDacomComponentFactory : public IComponentFactory
{
public:
    explicit FDacomComponentFactory(const char* InName) : RefCount(1), Name(InName) {}

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override
    {
        *Out = nullptr;
        if (std::strncmp(Iid, Iids::ComponentFactory, sizeof(Iids::ComponentFactory)) != 0)
            return GR_INTERFACE_UNSUPPORTED;
        *Out = static_cast<IComponentFactory*>(this);
        AddRef();
        return GR_OK;
    }

    unsigned long DACOM_API AddRef() override { return ++RefCount; }

    unsigned long DACOM_API Release() override
    {
        if (RefCount > 0)
            --RefCount;
        const unsigned long Remaining = RefCount;
        if (Remaining == 0)
        {
            RefCount = 1;
            ::operator delete(this);
        }
        return Remaining;
    }

    unsigned long RefCount; // +0x04
    const char* Name;       // +0x08  the registered component name
};

static_assert(sizeof(FDacomComponentFactory) == 0xC, "a retail factory is 0xC bytes");
static_assert(sizeof(Iids::ComponentFactory) == 0x17, "the IID compare covers 0x17 bytes");
