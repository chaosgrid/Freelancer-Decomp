// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll -- IAction (the action interface, with its own operator new/delete) and ActionDB
// (the per-object action list).

#include "common_cequip.h" // IAction, ActionDB

#include <cstdlib>
#include <cstring>
#include <new>

// ============================================================================
// IAction
// ============================================================================

IAction::IAction()
{
    // FUN_06287720
    Next = nullptr;
}

IAction::IAction(IAction const& Other)
{
    // FUN_06287730
    Next = Other.Next;
}

IAction::~IAction()
{
    // FUN_0629ff20
}

IAction& IAction::operator=(IAction const& Other)
{
    // FUN_06287760. No self test.
    Next = Other.Next;
    return *this;
}

void* IAction::operator new(size_t Size)
{
    // FUN_062fd560, ICF-folded with FmtStr::Val::operator new
    return malloc(Size);
}

void IAction::operator delete(void* Ptr)
{
    // FUN_062fd570, ICF-folded with FmtStr::Val::operator delete
    free(Ptr);
}

char const* IAction::get_id() const
{
    // FUN_0629ff10. Returns .rdata:063997b0 "".
    return "";
}

// ============================================================================
// ActionDB
// ============================================================================

ActionDB::ActionDB()
    : Head(nullptr)
    , Tail(nullptr)
{
    // FUN_062877e0
}

ActionDB::~ActionDB()
{
    // FUN_062a00c0
    free();
}

ActionDB& ActionDB::operator=(ActionDB const& Other)
{
    // FUN_06280ee0, ICF-folded with the other 2-dword struct assignments (DamageObjInfo,
    // BaseWatcher, CFLIDMaker, ...). Retail quirk kept: a shallow copy that aliases both
    // ActionDBs onto one list, with no free() of the old actions (double-free hazard).
    Head = Other.Head;
    Tail = Other.Tail;
    return *this;
}

void ActionDB::add(IAction* Action)
{
    // FUN_0629ff30. No null test; Action->Next (+4) is left as is (the ctor and remove() zero it).
    if (Tail != nullptr)
    {
        Tail->Next = Action;
        Tail = Action;
    }
    else
    {
        Head = Action;
        Tail = Action;
    }
}

void ActionDB::update(float Dt)
{
    // FUN_0629ff50. update() is vtable slot 1. Retail inlines remove()'s body when it returns
    // false. Next is read before update(Dt).
    IAction* Current = Head;
    while (Current != nullptr)
    {
        IAction* Next = Current->Next;
        bool bKeep = Current->update(Dt);
        if (!bKeep)
        {
            remove(Current);
        }
        Current = Next;
    }
}

bool ActionDB::remove(IAction* Action)
{
    // FUN_0629ffd0. Retail deletes a non-null Action whether or not it was in the list.
    // remove(nullptr) scans to the end and returns true without deleting anything.
    IAction* Prev = nullptr;
    IAction* Curr = Head;
    while (Curr != nullptr)
    {
        if (Curr == Action)
        {
            IAction* const Next = Curr->Next;
            if (Prev != nullptr)
            {
                Prev->Next = Next;
            }
            else
            {
                Head = Next;
            }
            if (Curr == Tail)
            {
                Tail = Prev;
            }
            Action->Next = nullptr;
            break;
        }
        Prev = Curr;
        Curr = Curr->Next;
    }
    bool const bResult = (Curr == Action);
    if (Action != nullptr)
    {
        delete Action;
    }
    return bResult;
}

IAction* ActionDB::find(char const* Name) const
{
    // FUN_062a0030. No null test on either string.
    for (IAction* Curr = Head; Curr != nullptr; Curr = Curr->Next)
    {
        char const* ActionId = Curr->get_id();
        if (strcmp(Name, ActionId) == 0)
        {
            return Curr;
        }
    }
    return nullptr;
}

int ActionDB::get_action_count() const
{
    // FUN_062a00a0
    int Count = 0;
    for (IAction* Curr = Head; Curr != nullptr; Curr = Curr->Next)
    {
        ++Count;
    }
    return Count;
}

void ActionDB::free()
{
    // FUN_062a00c0
    IAction* Curr = Head;
    while (Curr != nullptr)
    {
        IAction* Next = Curr->Next;
        delete Curr;
        Curr = Next;
    }
    Head = nullptr;
    Tail = nullptr;
}
