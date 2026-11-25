// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "../include/common_server.h"
#include "../include/common_id.h"
#include "../include/common_goods.h"
#include "../include/common_archetype.h"
#include "../include/common_reputation.h"
#include "../include/common_cobjdb.h"
#include "../include/common_cobject.h"
#include "../include/server_exports.h"
#include "../include/server_cobject.h"
#include "../include/common_physys.h"
#include "../include/common_misc.h"
#include "../include/rpc_iclient.h"
#include "../include/common_casteroid.h"
#include "server_internal.h"
#include <windows.h>
#include <cmath>
#include <cstring>     // std::memcpy (SpaceObj::Create EquipIdMaker seed)
#include <list>
#include <cstdlib>     // rand, wcstombs
#include "../include/server_iserverimpl.h"   // IServerImpl, Server
#include "../include/common_ini.h"
#include "../include/common_server_controller.h"
#include "../include/content.h"   // IDLL, CreateInstance / DestroyInstance (controller DLL entry points)
#include <cstddef>

static IClient* GLoadedClientInterface = nullptr;          // .data:06d8f620

static IChatClient* GLoadedChatClientInterface = nullptr;  // .data:06d8f624

// Retail keeps TWO pairs of client-interface pointers:
//   .data:06d8f618  HMODULE of the client DLL (FUN_06d43340 / FUN_06d43390)
//   .data:06d8f620  IClient*     as loaded
//   .data:06d8f624  IChatClient* as loaded
//   .data:06d8fb14  IClient*     live, used by every caller; zeroed by Shutdown
//   .data:06d8fb18  IChatClient* live, used by every caller; never zeroed
// Startup copies the loaded pair into the live pair through FUN_06d433d0 /
// FUN_06d433c0; FreeClientDll clears only the loaded pair.
static HMODULE g_hClientDll = nullptr;

    // Retail IServerImpl is twelve bytes and holds no player storage: IServer vptr at +0,
    // IChatServer vptr at +4, one dword (Field_0x8, server_iserverimpl.h) at +8 (ctor
    // .text:06cef560; copy ctor 06cef580 and operator= 06cef5b0 copy that dword; `Server`,
    // ordinal 524, .data:06d8facc). The player table is the exported global `Players`.

// FUN_06d43340. Retail keeps the module even when LoadLibraryA fails, and on success calls
// BOTH GetProcAddress results with no null test. Startup ignores the return value.
IClient* Server_LoadClientDll(const char* module_path) {
    g_hClientDll = LoadLibraryA(module_path);
    if (g_hClientDll) {
        typedef IClient* (__cdecl *GetClientInterfaceFn)();
        typedef IChatClient* (__cdecl *GetChatClientInterfaceFn)();
        GetClientInterfaceFn pfn = (GetClientInterfaceFn)GetProcAddress(g_hClientDll, "GetClientInterface");
        GLoadedClientInterface = pfn();
        GetChatClientInterfaceFn pfn2 = (GetChatClientInterfaceFn)GetProcAddress(g_hClientDll, "GetChatClientInterface");
        GLoadedChatClientInterface = pfn2();
    }
    return GLoadedClientInterface;
}

// FUN_06d43390. Only the LOADED pair is cleared; Shutdown zeroes the live IClient, the
// live IChatClient is never zeroed in retail. Shutdown pushes an argument the body never
// reads, so the port takes none.
void Server_FreeClientDll() {
    if (g_hClientDll) {
        FreeLibrary(g_hClientDll);
        g_hClientDll = nullptr;
        GLoadedClientInterface = nullptr;
        GLoadedChatClientInterface = nullptr;
    }
}

// FUN_06d433c0; sole caller Startup.
IChatClient* Server_GetLoadedChatClientInterface() {
    return GLoadedChatClientInterface;
}

// FUN_06d433d0; sole caller Startup.
IClient* Server_GetLoadedClientInterface() {
    return GLoadedClientInterface;
}

// .data:06d8f650 / 06d8f654 -- a TString<0x104> {length, chars}: the `path` value of the
// [Initial SP/MP DLLs] section, seeded empty by the static initializer 0x06d43410.
// pub::DLL_LoadLibrary (below) reads it as the directory for every relative
// controller DLL name, so it keeps its value for the rest of the session.
unsigned int GDllPathLength = 0;

char GDllPath[0x104] = {0};

// Controller / timer / physics infrastructure.

// Controller node, 0x20 bytes.
struct FControllerNode {
    FControllerNode* Prev;        // +0x00
    FControllerNode* Next;        // +0x04
    IDLL* InterfacePtr;           // +0x08  CreateInstance result (Content.dll's IDLL)
    HMODULE ModuleHandle;         // +0x0C
    float HeartbeatPeriod;        // +0x10  seconds between Heartbeat messages; < 0 = none
    float HeartbeatAccum;         // +0x14  time accumulated towards the next one
    unsigned int FLID;            // +0x18  unique controller ID
    unsigned int Priority;        // +0x1C  sort order
};

static FControllerNode g_ControllerListSentinel = { &g_ControllerListSentinel, &g_ControllerListSentinel, nullptr, nullptr, 0, 0, 0, 0 };

static FControllerNode g_DeferredListSentinel = { &g_DeferredListSentinel, &g_DeferredListSentinel, nullptr, nullptr, 0, 0, 0, 0 };

// FUN_06d43600: the ACTIVE list (DAT_06d8f644), then the DEFERRED-create list
// (DAT_06d8f638), so a controller created during a broadcast is already addressable.
// Retail returns &node+8 (the record); the port returns the node.
static FControllerNode* Server_ControllerFind(unsigned int Flid) {
    FControllerNode* Node = g_ControllerListSentinel.Next;
    while (Node != &g_ControllerListSentinel) {
        if (Node->FLID == Flid) return Node;
        Node = Node->Next;
    }
    Node = g_DeferredListSentinel.Next;
    while (Node != &g_DeferredListSentinel) {
        if (Node->FLID == Flid) return Node;
        Node = Node->Next;
    }
    return nullptr;
}

static_assert(sizeof(FControllerNode) == 0x20, "FControllerNode is 0x20 bytes");

static unsigned int g_DeferredCount = 0;         // DAT_06d8f63c

static unsigned int g_ControllerCount = 0;       // DAT_06d8f648

static void ListRemove(FControllerNode* node) {
    node->Prev->Next = node->Next;
    node->Next->Prev = node->Prev;
    node->Prev = node;
    node->Next = node;
}

// Deferred-create drain, shared by the heartbeat walk and by the broadcast arm of
// Server_ControllerDispatch (retail inlines it in both places).
static void Server_ControllerDrainDeferred() {
    FControllerNode* cur = g_DeferredListSentinel.Next;
    while (cur != &g_DeferredListSentinel) {
        FControllerNode* next = cur->Next;
        ListRemove(cur);
        g_DeferredCount--;
        FControllerNode* insert_cur = g_ControllerListSentinel.Next;
        while (insert_cur != &g_ControllerListSentinel) {
            if (cur->Priority <= insert_cur->Priority) break;
            insert_cur = insert_cur->Next;
        }
        cur->Next = insert_cur;
        cur->Prev = insert_cur->Prev;
        insert_cur->Prev->Next = cur;
        insert_cur->Prev = cur;
        g_ControllerCount++;
        cur = next;
    }
}

// Retail DAT_06d8f628/62c/630: a std::list of POINTERS to active-list nodes (0xc-byte
// list nodes, ctor 0x06d43560 / dtor 0x06d435a0). pub::Controller::Destroy only queues
// here; the heartbeat walk (FUN_06d43680) does the real teardown next tick.
static std::list<FControllerNode*> g_DestroyPending;

static unsigned int g_ReentrancyGuard = 0;       // DAT_06d8f764

typedef decltype(&::DestroyInstance) FControllerDestroyInstanceFn;

// pub::Controller::_SendMessage's dispatch core -- retail 0x06d44080. FUN_06d43650
// (Server_SendMessage) only forwards with Flid 0. A zero (masked) Flid broadcasts to every
// controller under the re-entrancy guard (DAT_06d8f764), draining the deferred-create
// queue when it drops back to zero; otherwise the target is looked up (-2 when missing).
// The interface is Content.dll's IDLL (content.h); slot 0, IDLL::HandleMessage, is the
// only entry point Server.dll calls on it. Teardown goes through DestroyInstance, never
// slot 1 (the destructor).

static void Server_ControllerDrainDeferred();

// FUN_06d43680 (cdecl): the controller heartbeat walk, called every ElapseTime and once by
// the cleanup. First the queued Destroys are torn down (GetModuleFileNameA's result
// unused), then, under the re-entrancy guard, every node with a period >= 0 accumulates dt
// and gets 0x20 Heartbeat with the whole periods elapsed.
void Server_TimerTick(float dt) {
    for (std::list<FControllerNode*>::iterator It = g_DestroyPending.begin();
         It != g_DestroyPending.end(); ++It) {
        FControllerNode* Node = *It;
        FControllerDestroyInstanceFn DestroyInstance =
            (FControllerDestroyInstanceFn)GetProcAddress(Node->ModuleHandle, "DestroyInstance");
        DestroyInstance(Node->InterfacePtr);
        char ModuleName[0x104];
        GetModuleFileNameA(Node->ModuleHandle, ModuleName, 0x104);
        FreeLibrary(Node->ModuleHandle);
        ListRemove(Node);
        free(Node);
        g_ControllerCount--;
    }
    g_DestroyPending.clear();

    g_ReentrancyGuard++;
    FControllerNode* cur = g_ControllerListSentinel.Next;
    while (cur != &g_ControllerListSentinel) {
        if (cur->HeartbeatPeriod >= 0.0f) {
            // x87: the compare and the divide use the unrounded sum; only the store is
            // rounded.
            const double Sum = static_cast<double>(dt) + static_cast<double>(cur->HeartbeatAccum);
            cur->HeartbeatAccum = static_cast<float>(Sum);
            const double Period = static_cast<double>(cur->HeartbeatPeriod);
            if (Sum >= Period) {
                float WrapTime = static_cast<float>(floor(Sum / Period) * Period);
                cur->InterfacePtr->HandleMessage(pub::Controller::EMessage::Heartbeat, &WrapTime);
                cur->HeartbeatAccum = cur->HeartbeatAccum - WrapTime;
            }
        }
        cur = cur->Next;
    }
    g_ReentrancyGuard--;
    if (g_ReentrancyGuard == 0)
        Server_ControllerDrainDeferred();
}

static_assert(offsetof(FControllerNode, HeartbeatPeriod) == 0x10, "SetHeartbeatInterval stores the period at +0x10");

// FUN_06d43820 -- is this Flid a live or pending controller. Sole caller:
// pub::SpaceObj::Create, passing &ShipInfo, whose first dword is therefore the Flid.
extern "C" bool __cdecl Server_ControllerExists(const unsigned int& Flid) {
    return Server_ControllerFind(Flid) != nullptr;
}

// Controller DLL entry points CreateInstance(Flid, Name, Parms) and
// DestroyInstance(Interface), both cdecl. Retail resolves them with GetProcAddress; the
// only controller DLL ever named is Content.dll, so the pointer types come from content.h.
typedef decltype(&::CreateInstance) FControllerCreateInstanceFn;

// Linked list helpers
static void ListInsertBefore(FControllerNode* sentinel, FControllerNode* node) {
    node->Next = sentinel;
    node->Prev = sentinel->Prev;
    sentinel->Prev->Next = node;
    sentinel->Prev = node;
}

// pub::Controller::Create -- retail 0x06d43ce0 (ordinal 85), cdecl. A failed
// DLL_LoadLibrary FDUMPs and carries on. During a broadcast the record goes to the
// DEFERRED list, else into the ACTIVE list before the first node with priority >= this one
// (signed). GetModuleFileNameA's result is unused. Neither GetProcAddress result is tested
// before the call; a null instance is destroyed, unloaded and unlinked, returning 0.
unsigned int Server_ControllerCreate(const char* dll_path, const char* instance_name,
                                             void* parms, unsigned int priority) {
    unsigned int flid = CFLIDMaker::GetInstance()->AllocateObjectID();
    if (flid == 0) return 0;

    HMODULE hMod = pub::DLL_LoadLibrary(dll_path);
    if (hMod == nullptr) {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: failed create DLL instance '%s' (make sure file exists)\n",
              "E:\\FL\\Scratch\\Source\\Server\\dlls.cpp", 0xa7, dll_path);
    }

    FControllerNode* node = (FControllerNode*)malloc(sizeof(FControllerNode));
    node->InterfacePtr = nullptr;
    node->ModuleHandle = hMod;
    node->HeartbeatAccum = 0.0f;
    node->HeartbeatPeriod = -1.0f;
    node->FLID = flid;
    node->Priority = priority;

    const bool bDeferred = (g_ReentrancyGuard != 0);
    if (bDeferred) {
        ListInsertBefore(&g_DeferredListSentinel, node);
        g_DeferredCount++;
    } else {
        FControllerNode* cur = g_ControllerListSentinel.Next;
        while (cur != &g_ControllerListSentinel) {
            if (static_cast<int>(cur->Priority) >= static_cast<int>(priority)) break;
            cur = cur->Next;
        }
        ListInsertBefore(cur, node);
        g_ControllerCount++;
    }

    RefreshLoadingProgress();
    char ModuleName[0x104];
    GetModuleFileNameA(node->ModuleHandle, ModuleName, 0x104);
    FControllerCreateInstanceFn CreateInstance =
        (FControllerCreateInstanceFn)GetProcAddress(node->ModuleHandle, "CreateInstance");
    node->InterfacePtr = CreateInstance(flid, instance_name, parms);
    if (node->InterfacePtr != nullptr) return flid;

    FControllerDestroyInstanceFn DestroyInstance =
        (FControllerDestroyInstanceFn)GetProcAddress(node->ModuleHandle, "DestroyInstance");
    DestroyInstance(node->InterfacePtr);
    GetModuleFileNameA(node->ModuleHandle, ModuleName, 0x104);
    FreeLibrary(node->ModuleHandle);
    ListRemove(node);
    if (bDeferred) g_DeferredCount--;
    else g_ControllerCount--;
    free(node);
    return 0;
}

// FUN_06d43840 (cdecl). Startup passes the INI path. The priority word is looked up in
// the 5-row table at .data:06d8d080 (char[16] name, int value). No match leaves the
// priority holding whatever it held before: the previous row's priority, or
// uninitialised stack for the first row; the port starts it at 0 (HIGHEST).
void Server_LoadInitialDlls(const char* IniPath) {
    struct FPriorityRow { const char* Name; unsigned int Value; };
    static const FPriorityRow PriorityTable[5] = {
        { "HIGHEST", 0 }, { "ABOVE_NORMAL", 1 }, { "NORMAL", 2 },
        { "BELOW_NORMAL", 3 }, { "LOWEST", 4 },
    };
    // FUN_06d03cf0: copy min(strlen, Size-1) chars, terminate, return the length. Its
    // null arm is inlined by every caller here.
    struct FBounded {
        static unsigned int Copy(char* Dst, unsigned int Size, const char* Src) {
            unsigned int Len = static_cast<unsigned int>(strlen(Src));
            if (!(Len < Size)) Len = Size - 1;
            memcpy(Dst, Src, Len);
            Dst[Len] = '\0';
            return Len;
        }
    };

    INI_Reader Reader;
    unsigned int Priority = 0;
    if (Reader.open(IniPath, false) && Reader.read_header()) {
        do {
            RefreshLoadingProgress();
            if (((Reader.is_header("Initial SP DLLs") && SinglePlayer()) ||
                 (Reader.is_header("Initial MP DLLs") && !SinglePlayer())) &&
                Reader.read_value()) {
                do {
                    if (Reader.is_value("path")) {
                        const char* PathValue = Reader.get_value_string();
                        if (PathValue == nullptr) {
                            GDllPathLength = 0;
                            GDllPath[0] = '\0';
                        } else {
                            GDllPathLength = FBounded::Copy(GDllPath, sizeof(GDllPath), PathValue);
                        }
                    } else if (Reader.is_value("DLL")) {
                        char DllName[0x104];
                        char InterfaceName[0x104];
                        char PriorityName[0x20];
                        const char* Value = Reader.get_value_string(0);
                        DllName[0] = '\0';
                        if (Value != nullptr) FBounded::Copy(DllName, sizeof(DllName), Value);
                        Value = Reader.get_value_string(1);
                        InterfaceName[0] = '\0';
                        if (Value != nullptr) FBounded::Copy(InterfaceName, sizeof(InterfaceName), Value);
                        Value = Reader.get_value_string(2);
                        PriorityName[0] = '\0';
                        if (Value != nullptr) FBounded::Copy(PriorityName, sizeof(PriorityName), Value);
                        for (unsigned int Row = 0; Row < 5; ++Row) {
                            if (_stricmp(PriorityTable[Row].Name, PriorityName) == 0) {
                                Priority = PriorityTable[Row].Value;
                                break;
                            }
                        }
                        Server_ControllerCreate(DllName, InterfaceName, nullptr, Priority);
                    }
                } while (Reader.read_value());
            }
        } while (Reader.read_header());
    }
}

// Neither arm tests the record's interface pointer before calling it.
extern "C" int __cdecl Server_ControllerDispatch(unsigned int Flid, pub::Controller::EMessage MsgId,
                                                 const void* MsgData) {
    RefreshLoadingProgress();
    if ((Flid & 0x3fffffff) == 0) {
        g_ReentrancyGuard++;
        FControllerNode* cur = g_ControllerListSentinel.Next;
        while (cur != &g_ControllerListSentinel) {
            cur->InterfacePtr->HandleMessage(MsgId, MsgData);
            cur = cur->Next;
        }
        g_ReentrancyGuard--;
        if (g_ReentrancyGuard == 0)
            Server_ControllerDrainDeferred();
        return 0;
    }
    FControllerNode* node = Server_ControllerFind(Flid);
    if (node == nullptr) return -2;
    node->InterfacePtr->HandleMessage(MsgId, MsgData);
    return 0;
}

// pub::Controller::Destroy -- retail 0x06d43f70 (ordinal 103), cdecl. Queues every ACTIVE
// node with that Flid and broadcasts 0x0c. Nothing is unlinked or unloaded here -- that
// happens at the top of the next heartbeat walk (FUN_06d43680), so a broadcast in progress
// never sees a freed node. The deferred-create list is not searched.
void Server_ControllerDestroy(unsigned int flid) {
    for (std::list<FControllerNode*>::iterator It = g_DestroyPending.begin();
         It != g_DestroyPending.end(); ++It) {
        if ((*It)->FLID == flid) return;
    }
    FControllerNode* cur = g_ControllerListSentinel.Next;
    while (cur != &g_ControllerListSentinel) {
        if (cur->FLID == flid) {
            g_DestroyPending.push_back(cur);
            unsigned int DestroyedFlid = cur->FLID;
            Server_ControllerDispatch(0, pub::Controller::EMessage::ControllerDestroyed, &DestroyedFlid);
        }
        cur = cur->Next;
    }
}

// FUN_06d43c00, called by Shutdown: sort the active list by Flid (FUN_06d441c0, unsigned,
// stable), Destroy every controller from the LAST node back, then one heartbeat walk with
// 0.0 tears down everything Destroy just queued. The deferred-create list is left alone.
void Server_ControllerCleanup() {
    // FUN_06d441c0: stable ascending sort by Flid. A stable insertion into a fresh
    // chain gives the same order as the retail merge sort.
    if (g_ControllerListSentinel.Next != &g_ControllerListSentinel) {
        FControllerNode* Pending = g_ControllerListSentinel.Next;
        g_ControllerListSentinel.Prev->Next = nullptr;
        g_ControllerListSentinel.Next = &g_ControllerListSentinel;
        g_ControllerListSentinel.Prev = &g_ControllerListSentinel;
        while (Pending != nullptr) {
            FControllerNode* NextPending = Pending->Next;
            FControllerNode* Before = g_ControllerListSentinel.Prev;
            while (Before != &g_ControllerListSentinel && Pending->FLID < Before->FLID)
                Before = Before->Prev;
            ListInsertBefore(Before->Next, Pending);
            Pending = NextPending;
        }
    }

    FControllerNode* cur = &g_ControllerListSentinel;
    if (cur != cur->Next) {
        do {
            Server_ControllerDestroy(cur->Prev->FLID);
            cur = cur->Prev;
        } while (cur != g_ControllerListSentinel.Next);
    }
    Server_TimerTick(0.0f);
}

namespace pub {

// pub::DLL_LoadLibrary -- retail 0x06d43c40 (ordinal 93). A relative path is resolved
// against the [Initial * DLLs] `path` (GDllPath, DAT_06d8f654).
SERVER_API HINSTANCE __cdecl DLL_LoadLibrary(const char* DllPath) {
    char FullPath[0x104];
    if (DllPath[0] == '\\' || DllPath[1] == ':') {
        strcpy(FullPath, DllPath);
    } else {
        char Joined[0x104];
        _makepath(Joined, nullptr, GDllPath, DllPath, nullptr);
        _fullpath(FullPath, Joined, 0x104);
    }
    HMODULE Module = LoadLibraryA(FullPath);
    if (Module == nullptr) {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: failed to load mission DLL '%s'\n",
              "E:\\FL\\Scratch\\Source\\Server\\dlls.cpp", 0x1de, FullPath);
    }
    return Module;
}

// ===========================================================================
// pub::Controller exports
// ===========================================================================

namespace Controller {

SERVER_API unsigned int __cdecl Create(const char* dll_path, const char* instance_name,
                            const CreateParms* create_parms, PRIORITY priority) {
    return ::Server_ControllerCreate(dll_path, instance_name, (void*)create_parms, (unsigned int)priority);
}

SERVER_API void __cdecl Destroy(unsigned int flid) {
    ::Server_ControllerDestroy(flid);
}

} // namespace Controller
} // namespace pub

// pub::Controller::SetHeartbeatInterval's body -- retail 0x06d44030: node+0x10 = Interval
// (the period, also the gate: the heartbeat walk FUN_06d43680 runs it when >= 0.0), node+0x14
// = rand() / 32768 * Interval, so freshly configured controllers do not all fire on the
// same frame; -2 when not found.
extern "C" int __cdecl Server_ControllerSetHeartbeat(unsigned int Flid, float Interval) {
    FControllerNode* node = Server_ControllerFind(Flid);
    if (node == nullptr) return -2;
    node->HeartbeatPeriod = Interval;
    node->HeartbeatAccum = (float)rand() * (1.0f / 32768.0f) * Interval;
    return 0;
}

namespace pub {
namespace Controller {

// pub::Controller::SetHeartbeatInterval -- retail 0x06d44030. Sets the controller node's
// interval (+0x10) and a random phase (+0x14) = rand() * 2^-15 * Interval; -2 when the
// node is not found. No controller message is sent.
SERVER_API int __cdecl SetHeartbeatInterval(const unsigned int& Flid, float Interval) {
    return Server_ControllerSetHeartbeat(Flid, Interval);
}

// pub::Controller::_SendMessage -- retail 0x06d44080. Flid 0 is the broadcast arm, the one
// FUN_06d43650 (every bare "send message N" in Server.dll) uses.
SERVER_API int __cdecl _SendMessage(const unsigned int& Flid, EMessage MsgId, const void* MsgData) {
    return Server_ControllerDispatch(Flid, MsgId, MsgData);
}

} // namespace Controller
} // namespace pub
