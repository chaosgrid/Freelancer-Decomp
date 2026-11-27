// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Space/HUD/hud_command_dispatch.h"

#include "Core/event_handler.h"

#include "Space/client_gameobj.h"
#include "Space/Cameras/space_camera.h"
#include "Space/Cameras/space_camera_game.h"

#include <algorithm>
#include <list>
#include <string.h>
#include <vector>
#include <windows.h>

namespace
{
    // DAT_0067c278 — the seven-dword event record. DAT_0067c280 is its +0x08 (Modifiers) and
    // DAT_0067c290 its +0x18 byte; they are fields of this one record, not separate globals.
    FHudCmdRecord G_HudCmdRecord = {};

    // DAT_0067c3ac — optional filter when Mode==1 (only matching handler ptr). Set by
    // FUN_00577b40 (HudPushInputFocus), restored by FUN_00577c30 (HudPopInputFocus).
    FHudCmdHandler* G_HudCmdMode1Filter = nullptr;

    // DAT_0067c3a8 — the filters pushed aside by FUN_00577b40: a 0xc-byte std::list object
    // (allocator byte, head, size) allocated on the first push and freed by FUN_00577c30 as soon
    // as it runs empty.
    std::list<FHudCmdHandler*>* G_HudCmdFocusStack = nullptr;

    // DAT_0067c26c (allocator byte) / DAT_0067c270 (sentinel) / DAT_0067c274 (count) — the
    // cancel stack: handlers that FUN_00576360 pushed to receive USER_CANCEL (0xf) in mode 1.
    // Retail builds the list in the static initializer LAB_00576250 and frees it in the atexit
    // LAB_00576290, which also zeroes the count, so a handler destroyed later at exit finds
    // FUN_005762f0's count test at 0 and touches nothing. The port keeps that shape: the node
    // list lives behind a pointer and FHudCmdCancelStackLifetime plays both thunks.
    std::list<FHudCmdHandler*>* G_HudCmdCancelNodes = nullptr;
    int G_HudCmdCancelCount = 0;

    struct FHudCmdCancelStackLifetime
    {
        FHudCmdCancelStackLifetime()
        {
            // LAB_00576250: empty sentinel into DAT_0067c270, DAT_0067c274 = 0.
            G_HudCmdCancelNodes = new std::list<FHudCmdHandler*>();
            G_HudCmdCancelCount = 0;
        }
        ~FHudCmdCancelStackLifetime()
        {
            // LAB_00576290: free every node and the sentinel, zero both globals.
            delete G_HudCmdCancelNodes;
            G_HudCmdCancelNodes = nullptr;
            G_HudCmdCancelCount = 0;
        }
    };
    FHudCmdCancelStackLifetime G_HudCmdCancelStackLifetime;

    // DAT_0067c3a4 — multimap MsgId → Handler* (retail VC6 _Tree with the _Multi byte set at
    // +0xc by FUN_00575e00; port: vector kept sorted by MsgId, equal ids in insertion order).
    struct FHudCmdRegEntry
    {
        unsigned MsgId;
        FHudCmdHandler* Handler;
    };

    // Retail's DAT_0067c3a4 is a pointer to a tree allocated on the first insert (FUN_00575e00),
    // so handlers can register from static constructors in any translation unit (the Win3D input
    // handler does, from LAB_0059d060). The port keeps that shape: a pointer, zero before any
    // dynamic initializer runs, allocated on first use, and freed again (pointer zeroed) when the
    // last registration goes (FUN_00576190 / FUN_00576010 tail).
    std::vector<FHudCmdRegEntry>* G_HudCmdRegistryPtr = nullptr;

    void EnsureHudCmdRegistry()
    {
        // FUN_00575e00's lazy header allocation.
        if (G_HudCmdRegistryPtr == nullptr)
            G_HudCmdRegistryPtr = new std::vector<FHudCmdRegEntry>();
    }

    // FUN_00576190 body, also inlined as the middle of FUN_00576010: erase every node whose
    // +0x10 is Handler, then free the tree when it is empty.
    void EraseAllRegistrations(FHudCmdHandler* Handler)
    {
        if (G_HudCmdRegistryPtr == nullptr)
            return;
        std::vector<FHudCmdRegEntry>& Registry = *G_HudCmdRegistryPtr;
        std::vector<FHudCmdRegEntry>::iterator It = Registry.begin();
        while (It != Registry.end())
        {
            if (It->Handler == Handler)
                It = Registry.erase(It);
            else
                ++It;
        }
        // An empty tree is freed and DAT_0067c3a4 zeroed.
        if (Registry.empty())
        {
            delete G_HudCmdRegistryPtr;
            G_HudCmdRegistryPtr = nullptr;
        }
    }
}

// FUN_005760d0 — register Handler for MsgId into DAT_0067c3a4 multimap (after existing entries
// with the same id; duplicates allowed).
void EventHandler::RegisterEvent(unsigned MsgId)
{
    // FUN_005760d0 → FUN_00575e00: multimap insert. An equal key goes right, so the new entry
    // lands after every existing entry with the same id. The _Multi byte skips the duplicate
    // check: the same handler may be registered twice for one id.
    EnsureHudCmdRegistry();

    FHudCmdRegEntry Entry;
    Entry.MsgId = MsgId;
    Entry.Handler = this;

    std::vector<FHudCmdRegEntry>& Registry = *G_HudCmdRegistryPtr;
    std::vector<FHudCmdRegEntry>::iterator It = Registry.begin();
    while (It != Registry.end() && static_cast<int>(It->MsgId) <= static_cast<int>(MsgId))
        ++It;
    Registry.insert(It, Entry);
}

// FUN_005760f0 — erase Handler's entries for one MsgId (the tree is kept even if it empties).
void EventHandler::UnregisterEvent(unsigned MsgId)
{
    // FUN_005760f0: erase the nodes of equal_range(MsgId) whose +0x10 is this handler. The tree
    // is not freed here even when it empties.
    if (G_HudCmdRegistryPtr == nullptr)
        return;
    std::vector<FHudCmdRegEntry>& Registry = *G_HudCmdRegistryPtr;
    std::vector<FHudCmdRegEntry>::iterator It = Registry.begin();
    while (It != Registry.end())
    {
        if (It->MsgId == MsgId && It->Handler == this)
            It = Registry.erase(It);
        else
            ++It;
    }
}

// FUN_00576190 — erase every entry whose handler == Handler; free the tree once it is empty.
void EventHandler::UnregisterAllEvents()
{
    // FUN_00576190.
    EraseAllRegistrations(this);
}

void EventHandler::RemoveCancelHandler(int Skip)
{
    // FUN_005762f0.
    if (G_HudCmdCancelCount == 0)
        return;
    if (G_HudCmdCancelNodes->empty())
        return;
    // Skip the first Skip nodes holding Handler, unlink every later one.
    std::list<FHudCmdHandler*>::iterator It = G_HudCmdCancelNodes->begin();
    while (It != G_HudCmdCancelNodes->end())
    {
        if (*It == this)
        {
            if (Skip != 0)
            {
                Skip = Skip - 1;
                ++It;
                continue;
            }
            It = G_HudCmdCancelNodes->erase(It);
            G_HudCmdCancelCount = G_HudCmdCancelCount - 1;
            continue;
        }
        ++It;
    }
}

void EventHandler::PushCancelHandler(int Skip)
{
    // FUN_00576360.
    RemoveCancelHandler(Skip);
    G_HudCmdCancelNodes->push_back(this);
    G_HudCmdCancelCount = G_HudCmdCancelCount + 1;
}

EventHandler::~EventHandler()
{
    // FUN_00576010 — the Event::Handler base destructor (vptr 005cbed8); FUN_00576190's erase is
    // inlined.
    HudPopInputFocus(this);
    EraseAllRegistrations(this);
    RemoveCancelHandler(0);
}

int HudDispatchCommand(unsigned MsgId, int Mode)
{
    // FUN_00576410. No tree -> false, before the record is touched.
    if (G_HudCmdRegistryPtr == nullptr)
        return 0;
    std::vector<FHudCmdRegEntry>& Registry = *G_HudCmdRegistryPtr;

    G_HudCmdRecord.MsgId = MsgId;

    // equal_range on MsgId, with the Mode 1 filter DAT_0067c3ac. The handlers go into a
    // 32-slot stack buffer with no bound check, as in retail (never more than 32 per id).
    FHudCmdHandler* Collected[32];
    int Count = 0;
    for (size_t Index = 0; Index < Registry.size(); ++Index)
    {
        FHudCmdRegEntry const& Entry = Registry[Index];
        if (Entry.MsgId != MsgId)
            continue;
        if (Mode == 1 && G_HudCmdMode1Filter != nullptr &&
            Entry.Handler != G_HudCmdMode1Filter)
            continue;
        Collected[Count] = Entry.Handler;
        Count = Count + 1;
    }

    // Called in reverse order; non-zero consumes.
    for (int Index = Count - 1; Index >= 0; --Index)
    {
        FHudCmdHandler* Handler = Collected[Index];
        bool const Handled = Handler->OnEvent(MsgId);
        if (Handled != 0)
            return 1;
    }

    // Mode 1, USER_CANCEL (0xf), a key-down event and a non-empty cancel stack (DAT_0067c274).
    if (Mode != 1 || MsgId != USER_CANCEL)
        return 0;
    if (G_HudCmdRecord.bKeyDown == 0)
        return 0;
    if (G_HudCmdCancelCount <= 0)
        return 0;

    // The last pushed handler is unlinked and called with 0x10; its answer is ignored and the
    // event counts as consumed.
    FHudCmdHandler* const Cancelled = G_HudCmdCancelNodes->back();
    G_HudCmdCancelNodes->pop_back();
    G_HudCmdCancelCount = G_HudCmdCancelCount - 1;
    Cancelled->OnEvent(USER_STACKABLE_CANCEL);
    return 1;
}

int HudPostCommand(unsigned MsgId, int bSkipMods)
{
    // FUN_005763b0, tail-jumping FUN_00576410(msgId, 0).
    if (bSkipMods == 0)
    {
        memset(&G_HudCmdRecord, 0, sizeof(G_HudCmdRecord));

        unsigned Mods = 0;
        if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
            Mods = Mods | 1u;
        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
            Mods = Mods | 4u;
        if ((GetKeyState(VK_MENU) & 0x8000) != 0)
            Mods = Mods | 0x10u;

        G_HudCmdRecord.Modifiers = Mods;
        G_HudCmdRecord.bKeyDown = 1;
    }

    return HudDispatchCommand(MsgId, 0);
}

void HudPushInputFocus(FHudCmdHandler* Handler)
{
    // FUN_00577b40.
    if (G_HudCmdMode1Filter != nullptr)
    {
        // The stack object is allocated on first use.
        if (G_HudCmdFocusStack == nullptr)
            G_HudCmdFocusStack = new std::list<FHudCmdHandler*>();
        G_HudCmdFocusStack->push_back(G_HudCmdMode1Filter);
    }
    G_HudCmdMode1Filter = Handler;
}

void HudPopInputFocus(FHudCmdHandler* Handler)
{
    // FUN_00577c30.
    if (G_HudCmdMode1Filter == Handler)
    {
        if (G_HudCmdFocusStack == nullptr)
        {
            G_HudCmdMode1Filter = nullptr;
            return;
        }
        // The last pushed filter comes back.
        G_HudCmdMode1Filter = G_HudCmdFocusStack->back();
        G_HudCmdFocusStack->pop_back();
    }
    else
    {
        // Not the active filter: unlink the first stacked node holding it, if any.
        if (G_HudCmdFocusStack == nullptr)
            return;
        std::list<FHudCmdHandler*>::iterator It = G_HudCmdFocusStack->begin();
        for (; It != G_HudCmdFocusStack->end(); ++It)
        {
            if (*It == Handler)
            {
                G_HudCmdFocusStack->erase(It);
                break;
            }
        }
    }

    // An empty stack is freed and the pointer cleared.
    if (G_HudCmdFocusStack != nullptr && G_HudCmdFocusStack->empty())
    {
        delete G_HudCmdFocusStack;
        G_HudCmdFocusStack = nullptr;
    }
}

FHudCmdHandler* HudGetInputFocus()
{
    // FUN_00577d50.
    return G_HudCmdMode1Filter;
}

FHudCmdRecord* HudCommandRecord()
{
    // FUN_00576240.
    return &G_HudCmdRecord;
}

bool SpaceSendCommand(FSpaceCommand* Command)
{
    // FUN_0054bcb0. DAT_00678ba0 is the "controller exists" byte.
    if (!IsSpaceCameraControllerReady())
        return false;

    // DAT_00679738 is re-read before every use, as retail does.
    FGameCamera* Camera = GetActiveSpaceCamera();
    if (Camera != nullptr)
    {
        // Slot 0x70; the result is ignored, but a record whose id it cleared to 0 is consumed.
        Camera->HandleCommand70(Command);
        if (Command->Id == 0)
            return true;

        // Slot 0x74; non-zero consumes the record.
        Camera = GetActiveSpaceCamera();
        if (Camera->HandleCommand74(Command) != 0)
            return true;

        Camera = GetActiveSpaceCamera();
    }

    // Ids up to 0xe stop here, unconsumed.
    if (Command->Id <= static_cast<int>(USER_EVENTS))
        return false;

    // The camera's watched object, when it is a ship (ManagerKind 1), gets the record through
    // its slot 115 (+0x1cc; the ship's FUN_00544d70 over EquippedObj's FUN_00534460), result
    // ignored. FSpaceCommand here is FCliEquipFirePacket on the object side.
    if (Camera != nullptr)
    {
        FGameCamera* const GameCamera = Camera;
        unsigned char* const Held = const_cast<unsigned char*>(
            reinterpret_cast<unsigned char const*>(GameCamera->Watched.target()));
        if (Held != nullptr)
        {
            FClientGameobj* const Object = reinterpret_cast<FClientGameobj*>(Held - 8);
            // ManagerKind 1 is the ship bucket: the object is an FClientShipGameobj, whose
            // slot 115 is FClientEquippedObj::HandleEquipMsg (overridden by the ship).
            if (Object != nullptr && Object->ManagerKind == EClientGameobjKind::Ship)
                static_cast<FClientShipGameobj*>(Object)->HandleEquipMsg(
                    reinterpret_cast<FCliEquipFirePacket const*>(Command));
        }
    }

    // Every path past the id test answers true.
    return true;
}
