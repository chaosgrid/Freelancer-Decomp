// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/win3d.h"

#include "Common/client_cliobj.h"
#include "Common/Graphics/client_mesh_load.h"
#include "Common/engine_light.h"
#include "Space/HUD/hud_command_dispatch.h"
#include "Space/HUD/hud_widescreen.h"    // port-only (FREELANCER_WIDESCREEN_SUPPORT)
#include "Core/event_handler.h"
#include "Common/input_manager.h"
#include "NeuroNet/nn_rollover.h"
#include "NeuroNet/nn_tooltip.h"
#include "NeuroNet/nn_ui_pulse.h"
#include "Space/Cameras/space_camera.h"
#include "Space/space_scene_obj_draw.h"
#include "Interface/win3d_engine_submit.h"
#include "Interface/win3d_immediate.h"
#include "Interface/win3d_render.h"
#include "Interface/win3d_renderables.h"
#include "Interface/wincontrol_base.h"

#include "common_csys.h"
#include "DALib.h"
#include "readfile.h"
#include "render/IRenderPipeline.h"
#include "render/shading.h"
#include "x86math.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------------------
// Manager state (Win3D.cpp .data / .bss)
// ---------------------------------------------------------------------------------------

namespace
{
    // FUN_0059e810's list: a count and a doubly linked chain of 0xc-byte nodes
    // { prev +0, next +4, payload +8 }.
    struct FWin3DListNode
    {
        FWin3DListNode* Prev;
        FWin3DListNode* Next;
        void* Payload;
    };

    struct FWin3DList
    {
        int Count;            // +0
        FWin3DListNode* Head; // +4
        FWin3DListNode* Tail; // +8

        // The atexit bodies LAB_0059d480 / LAB_0059d4f0. The payloads are not touched.
        ~FWin3DList()
        {
            FWin3DListNode* Node = Head;
            while (Node != nullptr)
            {
                FWin3DListNode* const Next = Node->Next;
                operator delete(Node);
                Node = Next;
            }
            Count = 0;
            Tail = nullptr;
            Head = nullptr;
        }
    };

    // DAT_0067dbe4 -- the registry, payload = Win3D*.
    FWin3DList G_Registry = {0, nullptr, nullptr};
    // DAT_0067dbd8 -- nodes queued for removal, payload = registry node.
    FWin3DList G_PendingRemove = {0, nullptr, nullptr};

    // DAT_0067dcdc keyboard focus, DAT_0067dce0 capture stack head (link at +0x38),
    // DAT_0067dcd8 object under the mouse.
    Win3D* G_KeyFocus = nullptr;
    Win3D* G_CaptureHead = nullptr;
    Win3D* G_Hover = nullptr;

    // DAT_0067dbcc -- the mouse ray, seeded (0, 0, -1); written by the Win3D input handler
    // FUN_0059d140 case 2. A plain array: every caller of Win3DPick / Win3DPickWithin passes it,
    // and the compiled code only folds it into them (and into HitTestWidescreen) as an array.
    float G_MouseRay[3] = {0.0f, 0.0f, -1.0f};

    // DAT_0067dcd0 -- how many registry nodes the last Win3DUpdateAll walked.
    int G_UpdateNodeCount = 0;
    // DAT_0067dcf0 -- read by Win3DDrawAll (positive skips the whole draw). No writer exists in
    // the binary, so it stays zero.
    int G_DrawSuppressCount = 0;

    // FUN_0059e810 (thiscall on the list, RET 4). A null allocation still runs the tail with
    // node 0, as retail does.
    bool ListPushBack(FWin3DList* List, void* Payload)
    {
        FWin3DListNode* Node = static_cast<FWin3DListNode*>(operator new(sizeof(FWin3DListNode)));
        if (Node != nullptr)
        {
            Node->Next = nullptr;
            Node->Prev = nullptr;
            Node->Payload = Payload;
        }
        if (List->Tail != nullptr)
            List->Tail->Next = Node;
        else
            List->Head = Node;
        Node->Prev = List->Tail;
        List->Tail = Node;
        Node->Next = nullptr;
        List->Count = List->Count + 1;
        return true;
    }

    // FUN_0059e7d0 (thiscall on a LinkTree's child-head field, RET 4): unlink Child from the
    // sibling chain starting at *ChildHeadField. Silently returns when it is not there.
    void LinkTreeRemoveChild(Win3D** ChildHeadField, Win3D* Child)
    {
        Win3D* Walk = *ChildHeadField;
        Win3D* Prev = nullptr;
        if (Walk == nullptr)
            return;
        while (Walk != Child)
        {
            Prev = Walk;
            Walk = Walk->NextSibling;
            if (Walk == nullptr)
                return;
        }
        if (Prev != nullptr)
            Prev->NextSibling = Walk->NextSibling;
        else
            *ChildHeadField = Walk->NextSibling;
    }

    // The unlink-and-free of one registry node, repeated inline four times in retail.
    void RegistryEraseNode(FWin3DListNode* Node)
    {
        FWin3DListNode* const Prev = Node->Prev;
        FWin3DListNode* const Next = Node->Next;
        Node->Payload = nullptr;
        operator delete(Node);
        if (Prev != nullptr)
            Prev->Next = Next;
        else
            G_Registry.Head = Next;
        if (Next != nullptr)
            Next->Prev = Prev;
        else
            G_Registry.Tail = Prev;
        G_Registry.Count = G_Registry.Count - 1;
    }

    // Erase every registry node the pending list names, free the pending nodes, then clear the
    // pending list header (inlined twice in Win3DUpdateAll).
    void DrainPendingRemove()
    {
        for (FWin3DListNode* Pending = G_PendingRemove.Head; Pending != nullptr;
             Pending = Pending->Next)
        {
            FWin3DListNode* const Node = static_cast<FWin3DListNode*>(Pending->Payload);
            if (Node != nullptr)
                RegistryEraseNode(Node);
        }
        FWin3DListNode* Pending = G_PendingRemove.Head;
        while (Pending != nullptr)
        {
            FWin3DListNode* const Next = Pending->Next;
            operator delete(Pending);
            Pending = Next;
        }
        G_PendingRemove.Count = 0;
        G_PendingRemove.Tail = nullptr;
        G_PendingRemove.Head = nullptr;
    }

    // The sorted draw used for roots and children: zero layer draws at once, the rest go into a
    // 0x400-entry stack array ordered by the unsigned layer, then by +0x68 with an ordered-less
    // compare, equal keys keeping insertion order.
    void InsertSorted(Win3D** Sorted, unsigned* Count, Win3D* Object)
    {
        unsigned const Layer = Object->DrawLayer;
        unsigned const Size = *Count;
        if (Size == 0 || Layer > Sorted[Size - 1]->DrawLayer)
        {
            if (Size >= 0x400)
                return;
            Sorted[Size] = Object;
            *Count = Size + 1;
            return;
        }

        unsigned InsertAt = 0;
        while (InsertAt < Size)
        {
            Win3D* const Other = Sorted[InsertAt];
            if (Layer < Other->DrawLayer)
                break;
            if (Layer == Other->DrawLayer && Object->Position.z < Other->Position.z)
                break;
            InsertAt = InsertAt + 1;
        }
        // Retail: no capacity check on the insertion path (only the append is bounded,
        // FUN_0059e5c0 0059e656 `cmp edx,400h`), so more than 0x400 visible layered objects
        // overrun the array.
        if (Size - InsertAt != 0)
            memmove(&Sorted[InsertAt + 1], &Sorted[InsertAt], (Size - InsertAt) * sizeof(Win3D*));
        Sorted[InsertAt] = Object;
        *Count = Size + 1;
    }

#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only (hud_widescreen.h): every draw, update, hit test and mouse call on an object
    // runs with the Win3D camera shifted for that object's HUD corner window (no-op for
    // everything else, and on a 4:3 surface).
    void DrawWidescreen(Win3D* Object)
    {
        FWidescreenHudShiftScope const Scope(WidescreenHudShiftPixelsFor(Object), true);
        Object->Draw();
    }

    void UpdateWidescreen(Win3D* Object)
    {
        FWidescreenHudShiftScope const Scope(WidescreenHudShiftPixelsFor(Object), false);
        if ((Object->Flags & kWin3DActive) != 0)
            Object->Update();
        else
            Object->UpdateInactive();
    }

    // Ray is the frame's mouse ray; a shifted object is tested against the cursor's ray through
    // the shifted camera instead.
    bool HitTestWidescreen(Win3D* Object, Vector const* Ray)
    {
        float const Shift = WidescreenHudShiftPixelsFor(Object);
        if (Shift == WidescreenHudCurrentShift())
            return Object->HitTest(Ray);
        FWidescreenHudShiftScope const Scope(Shift, false);
        Vector Shifted;
        return Object->HitTest(Win3DGetMouseRay(&Shifted));
    }

    enum class EMouseCall
    {
        Enter,
        Over,
        Leave
    };

    void MouseCallWidescreen(Win3D* Object, EMouseCall Call)
    {
        FWidescreenHudShiftScope const Scope(WidescreenHudShiftPixelsFor(Object), false);
        Vector Ray;
        Win3DGetMouseRay(&Ray);
        if (Call == EMouseCall::Enter)
            Object->MouseEnter(&Ray);
        else if (Call == EMouseCall::Over)
            Object->MouseOver(&Ray);
        else
            Object->MouseLeave(&Ray);
    }

    bool MouseEventWidescreen(Win3D* Object, FWin3DMouseEvent const* Event)
    {
        FWidescreenHudShiftScope const Scope(WidescreenHudShiftPixelsFor(Object), false);
        return Object->MouseEvent(Event);
    }

    bool WheelWidescreen(Win3D* Object, int Wheel)
    {
        FWidescreenHudShiftScope const Scope(WidescreenHudShiftPixelsFor(Object), false);
        return Object->MouseWheelEvent(Wheel);
    }
#endif

    // Body shared by FUN_0059e5c0 and FUN_0059e6e0.
    void DrawChildrenSorted(Win3D* Parent)
    {
        Win3D* Sorted[0x400];
        unsigned Count = 0;
        for (Win3D* Child = Parent->ChildHead; Child != nullptr; Child = Child->NextSibling)
        {
            if ((Child->Flags & kWin3DVisible) == 0)
                continue;
            if (Child->DrawLayer == 0)
            {
#ifdef FREELANCER_WIDESCREEN_SUPPORT
                DrawWidescreen(Child);
#else
                Child->Draw();
#endif
                continue;
            }
            InsertSorted(Sorted, &Count, Child);
        }
        for (unsigned Index = 0; Index < Count; ++Index)
#ifdef FREELANCER_WIDESCREEN_SUPPORT
            DrawWidescreen(Sorted[Index]);
#else
            Sorted[Index]->Draw();
#endif
    }

    char const* const kWin3DSourceFile = "E:\\FL\\Scratch\\Source\\Client\\Interface\\Win3D.cpp";
}

int G_Win3DLiveCount = 0;

// DAT_0067dccc (win3d.h).
int G_ModalCaptureCount = 0;

// ---------------------------------------------------------------------------------------
// Win3D
// ---------------------------------------------------------------------------------------

Win3D::Win3D()
{
    // FUN_004b6790. Name[1..31], Pad6d and the upper flag bits are left as allocated.
    ChildHead = nullptr;
    NextSibling = nullptr;
    Parent = nullptr;
    NameLength = 0;
    Name[0] = '\0';
    Orientation.E[2][1] = 0.0f;
    Orientation.E[2][0] = 0.0f;
    Orientation.E[1][2] = 0.0f;
    Orientation.E[1][0] = 0.0f;
    Orientation.E[0][2] = 0.0f;
    Orientation.E[0][1] = 0.0f;
    Orientation.E[2][2] = 1.0f;
    Orientation.E[1][1] = 1.0f;
    Orientation.E[0][0] = 1.0f;
    Position.z = 0.0f;
    Position.y = 0.0f;
    Position.x = 0.0f;
    ClassName = nullptr;
    DrawLayer = 0;
    CaptureNext = nullptr;
    HitPriority = 0;
    DrawSubLayer = 0;
    Flags = static_cast<unsigned char>((Flags & ~(kWin3DActive | kWin3DVisible | kWin3DUnregistered)) | kWin3DPickable);
    G_Win3DLiveCount = G_Win3DLiveCount + 1;
}

Win3D::~Win3D()
{
    // FUN_004b4fd0. Children are destroyed, not deleted.
    G_Win3DLiveCount = G_Win3DLiveCount - 1;
    Flags = static_cast<unsigned char>(Flags & ~(kWin3DActive | kWin3DVisible));
    for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
        Child->Destroy();
    Win3DUnregister(this);
}

bool Win3D::Slot1(unsigned /*A*/, unsigned /*B*/)
{
    // LAB_0043d8a0.
    return false;
}

WinControl* Win3D::AsWinControl()
{
    // FUN_004b5010.
    return nullptr;
}

bool Win3D::IsDescendantOf(Win3D* Ancestor)
{
    // LAB_0043d8b0.
    Win3D* const Up = Parent;
    if (Up == Ancestor)
        return true;
    if (Up != nullptr)
        return Up->IsDescendantOf(Ancestor);
    return false;
}

bool Win3D::HitTest(Vector const* /*Ray*/)
{
    // FUN_0053f330.
    return false;
}

void Win3D::SetPosition(Vector const* NewPosition)
{
    // LAB_004b5020.
    Position.x = NewPosition->x;
    Position.y = NewPosition->y;
    Position.z = NewPosition->z;
}

bool Win3D::IsPositionInViewport(unsigned /*A*/)
{
    // LAB_0043d8e0.
    return true;
}

void Win3D::SetOrientation(Matrix const* NewOrientation)
{
    // FUN_004b5040.
    memmove(&Orientation, NewOrientation, sizeof(Orientation));
}

void Win3D::SetTransform(Transform const* NewXform)
{
    // LAB_00595f70: both virtual calls.
    SetOrientation(&NewXform->Rotation);
    SetPosition(&NewXform->Translation);
}

bool Win3D::Create(Win3D* NewParent, void* /*Data*/)
{
    // FUN_004b5060 (RET 8).
    Win3DReparent(this, NewParent);
    Win3DRegister(this);
    Flags = static_cast<unsigned char>((Flags & ~kWin3DUnregistered) | kWin3DActive | kWin3DVisible);
    return true;
}

void Win3D::Destroy()
{
    // FUN_004b5090.
    Flags = static_cast<unsigned char>(Flags & ~(kWin3DActive | kWin3DVisible));
    for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
        Child->Destroy();
    Win3DUnregister(this);
}

void Win3D::Update()
{
    // FUN_005a17b0: the next sibling is read before each call.
    Win3D* Child = ChildHead;
    while (Child != nullptr)
    {
        Win3D* const Next = Child->NextSibling;
#ifdef FREELANCER_WIDESCREEN_SUPPORT
        UpdateWidescreen(Child);
#else
        if ((Child->Flags & kWin3DActive) != 0)
            Child->Update();
        else
            Child->UpdateInactive();
#endif
        Child = Next;
    }
}

void Win3D::UpdateInactive()
{
    // LAB_004682f0: the next sibling is read after each call.
    for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
    {
        if ((Child->Flags & kWin3DActive) != 0)
            continue;

#ifdef FREELANCER_WIDESCREEN_SUPPORT
        UpdateWidescreen(Child);
#else
        Child->UpdateInactive();
#endif
    }
}

void Win3D::Draw()
{
    // FUN_0059e5c0.
    DrawChildren();
}

void Win3D::MouseOver(Vector const* /*Ray*/)
{
    // FUN_0053e4a0.
}

void Win3D::MouseEnter(Vector const* /*Ray*/)
{
    // FUN_0053e4a0.
}

void Win3D::MouseLeave(Vector const* /*Ray*/)
{
    // FUN_0053e4a0.
}

void Win3D::Slot17()
{
    // FUN_00401e90.
}

bool Win3D::MouseWheelEvent(int /*Value*/)
{
    // FUN_0053f330.
    return false;
}

bool Win3D::MouseEvent(FWin3DMouseEvent const* Event)
{
    // LAB_004b50c0.
    Win3D* const Up = Parent;
    if (Up != nullptr)
        return Up->MouseEvent(Event);
    return false;
}

bool Win3D::KeyEvent(unsigned /*Key*/, unsigned char* /*Modifiers*/)
{
    // LAB_0043d8a0.
    return false;
}

bool Win3D::Slot21(unsigned /*A*/, unsigned /*B*/, unsigned /*C*/, unsigned /*D*/)
{
    // LAB_0043d8f0.
    return false;
}

bool Win3D::CharEvent(unsigned /*A*/, unsigned /*B*/)
{
    // LAB_0043d8a0.
    return false;
}

bool Win3D::ProcessMessage(unsigned /*Msg*/, unsigned /*A*/, unsigned /*B*/)
{
    // LAB_004b50e0.
    return false;
}

void Win3D::Slot24(unsigned A)
{
    // LAB_0043d900.
    for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
        Child->Slot24(A);
}

float Win3D::Slot25()
{
    // LAB_0043d930.
    return 1.0f;
}

float Win3D::Slot26()
{
    // LAB_0043d930 (the same body).
    return 1.0f;
}

int Win3D::GetHitPriority()
{
    // FUN_0043d940.
    return HitPriority;
}

void Win3D::SetName(char const* NewName)
{
    // Inlined in WinFactoryCreate.
    if (NewName == nullptr)
    {
        NameLength = 0;
        Name[0] = '\0';
        return;
    }
    unsigned Length = static_cast<unsigned>(strlen(NewName));
    if (Length >= 0x20)
        Length = 0x1f;
    memcpy(Name, NewName, Length);
    Name[Length] = '\0';
    NameLength = Length;
}

// ---------------------------------------------------------------------------------------
// Manager
// ---------------------------------------------------------------------------------------

void Win3DSetKeyFocus(Win3D* Focus)
{
    // FUN_0059d8c0.
    G_KeyFocus = Focus;
}

void Win3DPushCapture(Win3D* Object)
{
    // FUN_0059d8d0. A non-zero +0x38 means the object may already be on the stack: it is moved
    // to the head. A zero +0x38 means it is the tail or absent -- either way it is pushed
    // without a search. Retail: pushing the tail (or the only entry) again makes the stack
    // cyclic, and every capture walk then loops forever.
    Win3D* const OldNext = Object->CaptureNext;
    if (OldNext == nullptr)
    {
        Object->CaptureNext = G_CaptureHead;
        G_CaptureHead = Object;
        return;
    }

    Win3D* const Head = G_CaptureHead;
    Win3D* Walk = Head;
    Win3D* Prev = nullptr;
    while (Walk != nullptr)
    {
        if (Walk == Object)
        {
            if (Prev != nullptr)
            {
                Prev->CaptureNext = OldNext;
                Object->CaptureNext = nullptr;
                Object->CaptureNext = G_CaptureHead;
                G_CaptureHead = Object;
                return;
            }
            G_CaptureHead = OldNext;
            Object->CaptureNext = nullptr;
            Object->CaptureNext = G_CaptureHead;
            G_CaptureHead = Object;
            return;
        }
        Prev = Walk;
        Walk = Walk->CaptureNext;
    }
    Object->CaptureNext = Head;
    G_CaptureHead = Object;
}

void Win3DRemoveCapture(Win3D* Object)
{
    // FUN_0059d960.
    Win3D* Walk = G_CaptureHead;
    Win3D* Prev = nullptr;
    while (Walk != nullptr)
    {
        if (Walk == Object)
        {
            if (Prev != nullptr)
            {
                Prev->CaptureNext = Object->CaptureNext;
                Object->CaptureNext = nullptr;
                return;
            }
            G_CaptureHead = Object->CaptureNext;
            Object->CaptureNext = nullptr;
            return;
        }
        Prev = Walk;
        Walk = Walk->CaptureNext;
    }
}

Win3D* Win3DGetCapture()
{
    // FUN_0059d9b0.
    return G_CaptureHead;
}

Win3D* Win3DFindByClassName(char const* ClassName)
{
    // FUN_0059d9c0: nodes with a null payload or a null +0x10 are skipped.
    for (FWin3DListNode* Node = G_Registry.Head; Node != nullptr; Node = Node->Next)
    {
        Win3D* const Object = static_cast<Win3D*>(Node->Payload);
        if (Object == nullptr || Object->ClassName == nullptr)
            continue;
        if (_stricmp(Object->ClassName, ClassName) == 0)
            return Object;
    }
    return nullptr;
}

Win3D* Win3DFindByName(char const* Name)
{
    // FUN_0059da10.
    for (FWin3DListNode* Node = G_Registry.Head; Node != nullptr; Node = Node->Next)
    {
        Win3D* const Object = static_cast<Win3D*>(Node->Payload);
        if (Object == nullptr)
            continue;
        if (_stricmp(Object->Name, Name) == 0)
            return Object;
    }
    return nullptr;
}

Win3D* Win3DGetFirstRegistered()
{
    // FUN_0059da50.
    if (G_Registry.Head != nullptr)
        return static_cast<Win3D*>(G_Registry.Head->Payload);
    return nullptr;
}

Vector* Win3DGetMouseRay(Vector* Out)
{
    // FUN_0059da70.
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only: inside a shifted HUD window the ray comes through the shifted camera.
    if (WidescreenHudCurrentShift() != 0.0f)
    {
        WidescreenHudCursorRay(Out);
        return Out;
    }
#endif
    Out->x = G_MouseRay[0];
    Out->y = G_MouseRay[1];
    Out->z = G_MouseRay[2];
    return Out;
}

Vector* Win3DMouseRayStorage()
{
    return reinterpret_cast<Vector*>(G_MouseRay);
}

FSpaceCamera* Win3DGetCamera()
{
    // FUN_0059da60 (G_Win3DSceneCamera, win3d_engine_submit.cpp).
    return GetWin3DSceneCamera();
}

void Win3DSetViewportSize(int Width, int Height)
{
    // FUN_0059dcc0 (cdecl). The second argument of 1 is dead in the callee.
    int ViewRect[4];
    ViewRect[0] = 0;
    ViewRect[1] = 0;
    ViewRect[2] = Width - 1;
    ViewRect[3] = Height - 1;
    Win3DGetCamera()->SetViewportEx(ViewRect, 1);
}

Win3D* Win3DGetHover()
{
    // FUN_0059daa0.
    return G_Hover;
}

void Win3DRegister(Win3D* Object)
{
    // FUN_0059dbf0.
    ListPushBack(&G_Registry, Object);
}

bool Win3DUnregister(Win3D* Object)
{
    // FUN_0059dc00.
    if ((Object->Flags & kWin3DUnregistered) != 0)
        return true;
    Object->Flags = static_cast<unsigned char>(Object->Flags | kWin3DUnregistered);
    if (G_KeyFocus == Object)
        G_KeyFocus = nullptr;
    // Retail: the whole capture stack is dropped, not just its head popped.
    if (G_CaptureHead == Object)
        G_CaptureHead = nullptr;
    if (G_Hover == Object)
        G_Hover = nullptr;

    FWin3DListNode* Node = G_Registry.Head;
    while (Node != nullptr && Node->Payload != Object)
        Node = Node->Next;
    if (Node == nullptr || Node->Payload == nullptr)
        return false;

    Win3D* const Up = Object->Parent;
    if (Up != nullptr)
    {
        LinkTreeRemoveChild(&Up->ChildHead, Object);
        Object->Parent = nullptr;
    }
    ListPushBack(&G_PendingRemove, Node);
    Node->Payload = nullptr;
    return true;
}

void Win3DReparent(Win3D* Child, Win3D* NewParent)
{
    // FUN_0059dd00 (cdecl).
    if (Child == nullptr)
        return;
    Win3D* const OldParent = Child->Parent;
    if (OldParent == NewParent)
        return;
    if (OldParent != nullptr)
        LinkTreeRemoveChild(&OldParent->ChildHead, Child);
    Child->Parent = NewParent;
    if (NewParent == nullptr)
        return;
    Win3D* Tail = NewParent->ChildHead;
    if (Tail == nullptr)
    {
        Child->NextSibling = nullptr;
        NewParent->ChildHead = Child;
        return;
    }
    while (Tail->NextSibling != nullptr)
        Tail = Tail->NextSibling;
    Tail->NextSibling = Child;
    Child->NextSibling = nullptr;
}

bool Win3DDispatchKey(FHudCmdRecord const* Record)
{
    // FUN_0059dfd0 (cdecl). Retail quirk kept: the modifier byte is built in the argument's own
    // stack slot, so bits 5..7 are the low byte of the Record pointer; only bits 0..4 are
    // written.
    unsigned char Modifiers =
        static_cast<unsigned char>(reinterpret_cast<uintptr_t>(Record) & 0xe0);
    unsigned char const bKeyDown = Record->bKeyDown;
    Modifiers = static_cast<unsigned char>(Modifiers | ((bKeyDown & 1) << 1));
    if (bKeyDown == 0)
        Modifiers = static_cast<unsigned char>(Modifiers | kWin3DKeyReleased);
    unsigned const Shift = Record->Modifiers;
    if ((Shift & 0x03) != 0)
        Modifiers = static_cast<unsigned char>(Modifiers | kWin3DKeyShift);
    if ((Shift & 0x0c) != 0)
        Modifiers = static_cast<unsigned char>(Modifiers | kWin3DKeyControl);
    if ((Shift & 0x30) != 0)
        Modifiers = static_cast<unsigned char>(Modifiers | kWin3DKeyAlt);
    unsigned const Key = Record->Key;

    // The focus object alone gets the key, and its answer is returned.
    if (G_KeyFocus != nullptr)
        return G_KeyFocus->KeyEvent(Key, &Modifiers);

    // Otherwise every registered object in order until one takes it.
    bool bHandled = false;
    for (FWin3DListNode* Node = G_Registry.Head; Node != nullptr; Node = Node->Next)
    {
        Win3D* const Object = static_cast<Win3D*>(Node->Payload);
        if (Object == nullptr)
            continue;
        bHandled = Object->KeyEvent(Key, &Modifiers) || bHandled;
        if (bHandled)
            break;
    }
    return bHandled;
}

Win3D* Win3DPick(Vector const* Ray)
{
    // FUN_0059dd80.
    Win3D* Best = nullptr;
    int BestDepth = -1;
    for (FWin3DListNode* Node = G_Registry.Head; Node != nullptr; Node = Node->Next)
    {
        Win3D* const Object = static_cast<Win3D*>(Node->Payload);
        if (Object == nullptr)
            continue;
        unsigned char const ObjectFlags = Object->Flags;
        if ((ObjectFlags & kWin3DActive) == 0 || (ObjectFlags & kWin3DVisible) == 0 ||
            (ObjectFlags & kWin3DPickable) == 0)
            continue;

        // Every ancestor (and the object itself) must be visible.
        bool bVisibleChain = true;
        for (Win3D* Walk = Object; Walk != nullptr; Walk = Walk->Parent)
        {
            if ((Walk->Flags & kWin3DVisible) == 0)
            {
                bVisibleChain = false;
                break;
            }
        }
        if (!bVisibleChain)
            continue;

        int Depth = Object->GetHitPriority();
        if (Depth == 0)
        {
            for (Win3D* Walk = Object; Walk != nullptr; Walk = Walk->Parent)
                Depth = Depth + 1;
        }
        if (Depth > BestDepth)
        {
#ifdef FREELANCER_WIDESCREEN_SUPPORT
            if (HitTestWidescreen(Object, Ray))
#else
            if (Object->HitTest(Ray))
#endif
            {
                Best = Object;
                BestDepth = Depth;
            }
            continue;
        }
        if (Depth != BestDepth)
            continue;
        // Only a strictly greater +0x68 competes (unordered does not).
        if (!(Object->Position.z > Best->Position.z))
            continue;
#ifdef FREELANCER_WIDESCREEN_SUPPORT
        if (HitTestWidescreen(Object, Ray))
#else
        if (Object->HitTest(Ray))
#endif
        {
            Best = Object;
            BestDepth = Depth;
        }
    }
    return Best;
}

Win3D* Win3DPickWithin(Win3D* Capture, Vector const* Ray)
{
    // FUN_0059de50 (cdecl). Unlike Win3DPick the ancestor-visibility walk runs only after the
    // depth comparison, and only on the "deeper" branch.
    Win3D* Best = nullptr;
    int BestDepth = -1;
    for (FWin3DListNode* Node = G_Registry.Head; Node != nullptr; Node = Node->Next)
    {
        Win3D* const Object = static_cast<Win3D*>(Node->Payload);
        if (Object == nullptr)
            continue;
        unsigned char const ObjectFlags = Object->Flags;
        if ((ObjectFlags & kWin3DActive) == 0 || (ObjectFlags & kWin3DVisible) == 0 ||
            (ObjectFlags & kWin3DPickable) == 0)
            continue;
        if (!Object->IsDescendantOf(Capture) && Object != Capture)
            continue;

        int Depth = Object->GetHitPriority();
        if (Depth == 0)
        {
            for (Win3D* Walk = Object; Walk != nullptr; Walk = Walk->Parent)
                Depth = Depth + 1;
        }
        if (Depth > BestDepth)
        {
            bool bVisibleChain = true;
            for (Win3D* Walk = Object; Walk != nullptr; Walk = Walk->Parent)
            {
                if ((Walk->Flags & kWin3DVisible) == 0)
                {
                    bVisibleChain = false;
                    break;
                }
            }
            if (!bVisibleChain)
                continue;
#ifdef FREELANCER_WIDESCREEN_SUPPORT
            if (HitTestWidescreen(Object, Ray))
#else
            if (Object->HitTest(Ray))
#endif
            {
                Best = Object;
                BestDepth = Depth;
            }
            continue;
        }
        if (Depth != BestDepth)
            continue;
        if (!(Object->Position.z > Best->Position.z))
            continue;
#ifdef FREELANCER_WIDESCREEN_SUPPORT
        if (HitTestWidescreen(Object, Ray))
#else
        if (Object->HitTest(Ray))
#endif
        {
            Best = Object;
            BestDepth = Depth;
        }
    }
    return Best;
}

bool Win3DBroadcast(unsigned Msg, unsigned A, unsigned B)
{
    // FUN_0059df40.
    bool bHandled = false;
    for (FWin3DListNode* Node = G_Registry.Head; Node != nullptr; Node = Node->Next)
    {
        Win3D* const Object = static_cast<Win3D*>(Node->Payload);
        if (Object == nullptr)
            continue;
        bHandled = bHandled || Object->ProcessMessage(Msg, A, B);
        if (bHandled)
            break;
    }
    return bHandled;
}

void Win3DSetHover(Win3D* Object)
{
    // FUN_0059df80. Leave/enter only on a change; slot 14 runs on every call with a
    // non-null Object.
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    if (G_Hover != Object)
    {
        if (G_Hover != nullptr)
            MouseCallWidescreen(G_Hover, EMouseCall::Leave);
        G_Hover = Object;
        if (Object != nullptr)
            MouseCallWidescreen(Object, EMouseCall::Enter);
    }
    if (Object != nullptr)
        MouseCallWidescreen(Object, EMouseCall::Over);
#else
    if (G_Hover != Object)
    {
        if (G_Hover != nullptr)
            G_Hover->MouseLeave(G_MouseRay);
        G_Hover = Object;
        if (Object != nullptr)
            Object->MouseEnter(G_MouseRay);
    }
    if (Object != nullptr)
        Object->MouseOver(G_MouseRay);
#endif
}

void Win3DUpdateAll(double DeltaSeconds)
{
    // FUN_0059e080 (cdecl, double dt).
    NnUiPulseAdvance(DeltaSeconds);
    NnToolTipTickGlobal(DeltaSeconds);
    NnRolloverTickGlobal(DeltaSeconds);

    // Picking only while the software cursor is drawn.
    Win3D* Picked = nullptr;
    if (GetClientMouseCursorDrawFlag() != 0)
    {
        if (G_CaptureHead == nullptr)
        {
            Picked = Win3DPick(reinterpret_cast<Vector const*>(G_MouseRay));
        }
        else
        {
            Picked = Win3DPickWithin(G_CaptureHead, reinterpret_cast<Vector const*>(G_MouseRay));
            if (Picked == nullptr)
                Picked = G_CaptureHead;
        }
    }

    // Win3DSetHover inlined, but MouseOver runs only on a change.
    if (G_Hover != Picked)
    {
#ifdef FREELANCER_WIDESCREEN_SUPPORT
        if (G_Hover != nullptr)
            MouseCallWidescreen(G_Hover, EMouseCall::Leave);
        G_Hover = Picked;
        if (Picked != nullptr)
        {
            MouseCallWidescreen(Picked, EMouseCall::Enter);
            MouseCallWidescreen(Picked, EMouseCall::Over);
        }
#else
        if (G_Hover != nullptr)
            G_Hover->MouseLeave(G_MouseRay);
        G_Hover = Picked;
        if (Picked != nullptr)
        {
            Picked->MouseEnter(G_MouseRay);
            Picked->MouseOver(G_MouseRay);
        }
#endif
    }

    G_UpdateNodeCount = 0;
    if (G_ModalCaptureCount != 0)
    {
        // The first registered WinControl holding the mouse gets the only update of the frame,
        // then the pending list is drained.
        for (FWin3DListNode* Node = G_Registry.Head; Node != nullptr; Node = Node->Next)
        {
            Win3D* const Object = static_cast<Win3D*>(Node->Payload);
            if (Object == nullptr)
                continue;
            WinControl* const Control = Object->AsWinControl();
            if (Control == nullptr || Control->bCaptured == 0)
                continue;
#ifdef FREELANCER_WIDESCREEN_SUPPORT
            UpdateWidescreen(Object);
#else
            if ((Object->Flags & kWin3DActive) != 0)
                Object->Update();
            else
                Object->UpdateInactive();
#endif
            DrainPendingRemove();
            return;
        }
    }

    // Every registered root, counting every node walked.
    for (FWin3DListNode* Node = G_Registry.Head; Node != nullptr; Node = Node->Next)
    {
        Win3D* const Object = static_cast<Win3D*>(Node->Payload);
        if (Object != nullptr && Object->Parent == nullptr)
        {
#ifdef FREELANCER_WIDESCREEN_SUPPORT
            UpdateWidescreen(Object);
#else
            if ((Object->Flags & kWin3DActive) != 0)
                Object->Update();
            else
                Object->UpdateInactive();
#endif
        }
        G_UpdateNodeCount = G_UpdateNodeCount + 1;
    }
    DrainPendingRemove();
}

void Win3DBeginDraw()
{
    // FUN_0059e500.
    Win3DDisable(kWin3DLightingCap);
    Win3DDisable(kWin3DTexture2D);
    Win3DDisable(kWin3DCullFace);
    Win3DDisable(kWin3DDepthTest);
    Win3DDisable(kWin3DFog);
    ICamera* const Camera = AsCamera(Win3DGetCamera());
    Win3DSetViewport(Camera, kWin3DSourceFile, 0x487);
    Win3DApplyCamera(Camera);
    DALib::MaterialBatcher->SetBinOrder(1, -1);
    EngineLightClearRegisteredList();
    IRenderPipeline* const Pipe = DALib::Pipeline;
    Pipe->SetRenderState(D3DRS_AMBIENT, -1);
    Pipe->SetRenderState(D3DRS_DITHERENABLE, 0);
}

void Win3DEndDraw()
{
    // FUN_0059e5a0.
    DALib::MaterialBatcher->SetBinOrder(1, 1);
}

void Win3DDrawAll()
{
    // FUN_0059e300.
    if (G_DrawSuppressCount > 0)
        return;

    Win3DBeginDraw();          // inlined in retail
    Win3DImmedBatchReset();
    ButtonMontageEnsure();

    Win3D* Sorted[0x400];
    unsigned Count = 0;
    for (FWin3DListNode* Node = G_Registry.Head; Node != nullptr; Node = Node->Next)
    {
        Win3D* const Object = static_cast<Win3D*>(Node->Payload);
        if (Object == nullptr || Object->Parent != nullptr)
            continue;
        if ((Object->Flags & kWin3DVisible) == 0)
            continue;
        if (Object->DrawLayer == 0)
        {
#ifdef FREELANCER_WIDESCREEN_SUPPORT
            DrawWidescreen(Object);
#else
            Object->Draw();
#endif
            continue;
        }
        InsertSorted(Sorted, &Count, Object);
    }

    for (int SubLayer = 0; SubLayer < 8; ++SubLayer)
    {
        for (unsigned Index = 0; Index < Count; ++Index)
        {
            if (Sorted[Index]->DrawSubLayer == SubLayer)
#ifdef FREELANCER_WIDESCREEN_SUPPORT
                DrawWidescreen(Sorted[Index]);
#else
                Sorted[Index]->Draw();
#endif
        }
    }

    ButtonMontageFlushBatch();
    Win3DImmedBatchFlushAll();
    UiOverlayPrepPipeline();
    ImeFocusDrawPass();
    MouseFrameUpdate();
    DALib::MaterialBatcher->SetBinOrder(1, 1);
}

void Win3D::DrawChildren()
{
    // FUN_0059e5c0.
    DrawChildrenSorted(this);
}

void Win3DDrawChildrenOf(Win3D* Parent)
{
    // FUN_0059e6e0.
    DrawChildrenSorted(Parent);
}

void Win3DDeleteTree(Win3D* Root)
{
    // FUN_0043d2a0 (cdecl). The next sibling is read before the child's subtree goes.
    if (Root == nullptr)
        return;
    Win3D* Child = Root->ChildHead;
    while (Child != nullptr)
    {
        Win3D* const Next = Child->NextSibling;
        Win3DDeleteTree(Child);
        Child = Next;
    }
    Root->Destroy();
    delete Root;
}

bool Win3DAnyCaptured()
{
    // FUN_0059e6a0.
    for (FWin3DListNode* Node = G_Registry.Head; Node != nullptr; Node = Node->Next)
    {
        Win3D* const Object = static_cast<Win3D*>(Node->Payload);
        if (Object == nullptr)
            continue;
        WinControl* const Control = Object->AsWinControl();
        if (Control != nullptr && Control->bCaptured != 0)
            return true;
    }
    return false;
}

// DAT_0067dbc8 -- the material-cache entry of interface\interface.generic.vms.
Archetype::FCliMaterialEntry* G_InterfaceGenericMaterial = nullptr;

void Win3DStartupManager(int Width, int Height)
{
    // FUN_0059d5a0 (cdecl), with the options object's size (DAT_00679bc8 / DAT_00679bcc).
    FSpaceCamera* const WinCamera = Win3DGetCamera();

    int ViewRect[4];
    ViewRect[0] = 0;
    ViewRect[1] = 0;
    ViewRect[2] = Width - 1;
    ViewRect[3] = Height - 1;
    WinCamera->SetViewport(ViewRect);
    WinCamera->SetNearFar(0.1f, 1000.0f);
    {
        unsigned int const kHalfFovBits = 0x41d9ba5eu;
        float HalfFovX;
        memcpy(&HalfFovX, &kHalfFovBits, sizeof(HalfFovX));
        WinCamera->SetHalfFovX(HalfFovX);
    }
    // mouse.ini shapes and cursors.
    MouseInitCursors();
    Win3DImmedBatchCreateAll();

    // A "FileSystem" for interface\interface.generic.vms under DALib::Data, the description
    // filled like every other client call site.
    FFileSystemAggDesc Desc;
    memset(&Desc, 0, sizeof(Desc));
    Desc.Size = 0x34;
    Desc.ComponentName = "FileSystem";
    Desc.DataPath = "interface\\interface.generic.vms";
    Desc.Access = GENERIC_READ;
    Desc.ShareMode = FILE_SHARE_READ;
    Desc.Creation = OPEN_EXISTING;
    Desc.Flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN;
    Desc.FindHandle = 0xffffffffu;
    IFileSystem* FsOut = nullptr;
    if (static_cast<IFileSystem*>(DALib::Data)->CreateInstance(reinterpret_cast<AGGDESC*>(&Desc),
                                                               reinterpret_cast<void**>(&FsOut)) >= 0)
    {
        G_InterfaceGenericMaterial = Archetype::ResolveMaterialEntryFromFs(
            FsOut, Desc.DataPath, true, 0xffffffffu);
    }
    // Released whenever the out pointer is non-null, whatever the create returned.
    if (FsOut != nullptr)
        FsOut->Release();
}

void Win3DShutdownManager()
{
    // FUN_0059d6d0.
    G_KeyFocus = nullptr;
    G_CaptureHead = nullptr;

    // Erase whatever is already pending.
    for (FWin3DListNode* Pending = G_PendingRemove.Head; Pending != nullptr;
         Pending = Pending->Next)
    {
        FWin3DListNode* const Node = static_cast<FWin3DListNode*>(Pending->Payload);
        if (Node != nullptr)
            RegistryEraseNode(Node);
    }
    FWin3DListNode* Pending = G_PendingRemove.Head;
    while (Pending != nullptr)
    {
        FWin3DListNode* const Next = Pending->Next;
        operator delete(Pending);
        Pending = Next;
    }
    G_PendingRemove.Count = 0;
    G_PendingRemove.Tail = nullptr;
    G_PendingRemove.Head = nullptr;

    // Win3DUnregister inlined on every registered object (the next node is read first).
    FWin3DListNode* Node = G_Registry.Head;
    while (Node != nullptr)
    {
        FWin3DListNode* const Next = Node->Next;
        Win3D* const Object = static_cast<Win3D*>(Node->Payload);
        if (Object != nullptr && (Object->Flags & kWin3DUnregistered) == 0)
        {
            Object->Flags = static_cast<unsigned char>(Object->Flags | kWin3DUnregistered);
            if (G_KeyFocus == Object)
                G_KeyFocus = nullptr;
            if (G_CaptureHead == Object)
                G_CaptureHead = nullptr;
            if (G_Hover == Object)
                G_Hover = nullptr;
            FWin3DListNode* Found = G_Registry.Head;
            while (Found != nullptr && Found->Payload != Object)
                Found = Found->Next;
            if (Found != nullptr && Found->Payload != nullptr)
            {
                Win3D* const Up = Object->Parent;
                if (Up != nullptr)
                {
                    LinkTreeRemoveChild(&Up->ChildHead, Object);
                    Object->Parent = nullptr;
                }
                ListPushBack(&G_PendingRemove, Found);
                Found->Payload = nullptr;
            }
        }
        Node = Next;
    }

    // Erase the pending registry nodes, then free the pending list itself.
    for (FWin3DListNode* Drain = G_PendingRemove.Head; Drain != nullptr; Drain = Drain->Next)
    {
        FWin3DListNode* const Erase = static_cast<FWin3DListNode*>(Drain->Payload);
        if (Erase != nullptr)
            RegistryEraseNode(Erase);
    }
    FWin3DListNode* Free = G_PendingRemove.Head;
    while (Free != nullptr)
    {
        FWin3DListNode* const Next = Free->Next;
        operator delete(Free);
        Free = Next;
    }
    G_PendingRemove.Count = 0;
    G_PendingRemove.Tail = nullptr;
    G_PendingRemove.Head = nullptr;

    // Free whatever registry nodes remain and clear the header.
    FWin3DListNode* Rest = G_Registry.Head;
    while (Rest != nullptr)
    {
        FWin3DListNode* const Next = Rest->Next;
        operator delete(Rest);
        Rest = Next;
    }
    G_Registry.Count = 0;
    G_Registry.Tail = nullptr;
    G_Registry.Head = nullptr;

    // The model cache DAT_0067dcf4.
    Win3DModelCacheClear();

    // Retail's tail jump to the camera's empty LAB_00401e90 does nothing.
    Archetype::ReleaseMaterialEntrySlot(&G_InterfaceGenericMaterial);
    MouseShutdown();
    Win3DImmedBatchDestroyAll();
}

// ---------------------------------------------------------------------------------------
// Input router DAT_0067dbf0
// ---------------------------------------------------------------------------------------

namespace
{
    // DAT_006679d8 -- FUN_00423b90's lazily acquired 3DMathEngine.
    I3DMathEngine* G_TransformMathEngine = nullptr;

    // FUN_00423b90
    void TransformByMatrix(Vector& Out, Matrix const& Mat, Vector const& Source)
    {
        if (G_TransformMathEngine == nullptr)
            G_TransformMathEngine = Acquire3DMathEngine();
        Vector Result;
        G_TransformMathEngine->TransformMatrix(Result, Mat, Source);
        Out = Result;
    }

    // FUN_0041a390 (thiscall on the vector): scale by 1 / |v|. There is no zero test: a zero
    // vector divides by zero exactly as retail does.
    void NormalizeInPlace(Vector& V)
    {
        double const X = V.x;
        double const Y = V.y;
        double const Z = V.z;
        double const Inv = 1.0 / sqrt(X * X + Y * Y + Z * Z);
        V.x = static_cast<float>(Inv * V.x);
        V.y = static_cast<float>(Inv * V.y);
        V.z = static_cast<float>(Inv * V.z);
    }

    // The handler object at DAT_0067dbf0 (vftable 0x005e5dcc, one slot: the FHudCmdHandler
    // shape the event dispatch calls through).
    class FWin3DInputHandler : public EventHandler
    {
    public:
        FWin3DInputHandler();
        ~FWin3DInputHandler();

        // FUN_0059d140 (thiscall, RET 4).
        bool OnEvent(unsigned MsgId) override;

    };

    FWin3DInputHandler::FWin3DInputHandler()
    {
        // FUN_0059d080: the twelve ids registered in this order.
        static unsigned const kIds[12] = {USER_MOVE,    USER_WHEEL,   USER_BUTTON0, USER_BUTTON1,
                                          USER_BUTTON2, USER_BUTTON3, USER_BUTTON4, USER_BUTTON5,
                                          USER_BUTTON6, USER_BUTTON7, USER_KEY,     USER_CHAR};
        for (unsigned Index = 0; Index < 12; ++Index)
            RegisterEvent(kIds[Index]);
    }

    FWin3DInputHandler::~FWin3DInputHandler()
    {
        // FUN_0059d410. The EventHandler base dtor repeats the erase, which finds nothing left.
        UnregisterAllEvents();
    }

    bool FWin3DInputHandler::OnEvent(unsigned MsgId)
    {
        // FUN_0059d140. Ids outside 1..13, and 3, are not handled.
        if (MsgId - 1 > 12 || MsgId == USER_BUTTON)
            return false;

        if (MsgId == USER_KEY)
        {
            return Win3DDispatchKey(HudCommandRecord());
        }

        if (MsgId == USER_CHAR)
        {
            // CharEvent to the focus object and its answer returned; without a focus, to every
            // registered object until one returns true.
            unsigned const B = HudCommandRecord()->Arg14;
            unsigned const A = HudCommandRecord()->Arg10;
            if (G_KeyFocus != nullptr)
                return G_KeyFocus->CharEvent(A, B);
            bool bHandled = false;
            for (FWin3DListNode* Node = G_Registry.Head; Node != nullptr; Node = Node->Next)
            {
                Win3D* const Object = static_cast<Win3D*>(Node->Payload);
                if (Object == nullptr)
                    continue;
                bHandled = Object->CharEvent(A, B) || bHandled;
                if (bHandled)
                    break;
            }
            return bHandled;
        }

        if (MsgId == USER_WHEEL)
        {
            // The wheel (DAT_0061684c): the capture head alone, otherwise the picked object.
#ifdef FREELANCER_WIDESCREEN_SUPPORT
            if (G_CaptureHead != nullptr)
                return WheelWidescreen(G_CaptureHead, GetClientMouseWheel());
            Win3D* const Picked = Win3DPick(reinterpret_cast<Vector const*>(G_MouseRay));
            if (Picked == nullptr)
                return false;
            return WheelWidescreen(Picked, GetClientMouseWheel());
#else
            if (G_CaptureHead != nullptr)
                return G_CaptureHead->MouseWheelEvent(GetClientMouseWheel());
            Win3D* const Picked = Win3DPick(G_MouseRay);
            if (Picked == nullptr)
                return false;
            return Picked->MouseWheelEvent(GetClientMouseWheel());
#endif
        }

        if (MsgId == USER_MOVE)
        {
            // Mouse move: the cursor pixel (DAT_00616840 / DAT_00616844) to the Win3D camera's
            // near plane, into world space through a copy of the camera's orientation, and
            // normalised into the stored ray.
            int CursorX = 0;
            int CursorY = 0;
            GetClientSoftwareCursor(&CursorX, &CursorY, nullptr);
            FSpaceCamera* const Cam = Win3DGetCamera();
            Vector Local;
            Cam->ScreenToNearPlane(Local, static_cast<float>(CursorX), static_cast<float>(CursorY));
            Matrix Orientation;
            memcpy(&Orientation, &Cam->World, sizeof(Orientation));
            Vector World;
            TransformByMatrix(World, Orientation, Local);
            NormalizeInPlace(World);
            G_MouseRay[0] = World.x;
            G_MouseRay[1] = World.y;
            G_MouseRay[2] = World.z;

            // With a capture, the pick inside it, falling back to the capture head itself;
            // without one, the global pick. Consumed when there is a hover object.
            Win3D* Picked = nullptr;
            if (G_CaptureHead == nullptr)
            {
                Picked = Win3DPick(reinterpret_cast<Vector const*>(G_MouseRay));
            }
            else
            {
                Picked = Win3DPickWithin(G_CaptureHead, reinterpret_cast<Vector const*>(G_MouseRay));
                if (Picked == nullptr)
                    Picked = G_CaptureHead;
            }
            Win3DSetHover(Picked);
            return Picked != nullptr;
        }

        // Mouse buttons 6..13. The "else 0" arm is unreachable from these ids.
        FWin3DMouseEvent Event;
        unsigned const ButtonIndex = MsgId - 6;
        if (ButtonIndex < 8)
            Event.Button = -1 - static_cast<int>(ButtonIndex);
        else
            Event.Button = 0;
        unsigned char const bDoubleClick = HudCommandRecord()->bDoubleClick;
        if (HudCommandRecord()->bKeyDown != 0)
            Event.Action = kWin3DMousePress + (bDoubleClick != 0 ? 1 : 0);
        else
            Event.Action = kWin3DMouseRelease;

        // Walk the capture chain. For each capture object the pick inside it gets the event
        // first; the capture object itself gets it when the pick missed or
        // declined, unless the pick WAS the capture object. The walk stops once handled.
        bool bHandled = false;
        Win3D* Capture = G_CaptureHead;
        if (Capture != nullptr)
        {
            do
            {
                if (bHandled)
                    return bHandled;
                Win3D* const Next = Capture->CaptureNext;
                Win3D* const Hit = Win3DPickWithin(Capture, reinterpret_cast<Vector const*>(G_MouseRay));
#ifdef FREELANCER_WIDESCREEN_SUPPORT
                if (Hit != nullptr)
                    bHandled = MouseEventWidescreen(Hit, &Event);
                if (!bHandled && Hit != Capture)
                    bHandled = MouseEventWidescreen(Capture, &Event);
#else
                if (Hit != nullptr)
                    bHandled = Hit->MouseEvent(&Event);
                if (!bHandled && Hit != Capture)
                    bHandled = Capture->MouseEvent(&Event);
#endif
                Capture = Next;
            } while (Capture != nullptr);
            if (bHandled)
                return bHandled;
        }

        // Nothing in the capture chain took it; the global pick gets it.
        Win3D* const Picked = Win3DPick(reinterpret_cast<Vector const*>(G_MouseRay));
        if (Picked == nullptr)
            return bHandled;
#ifdef FREELANCER_WIDESCREEN_SUPPORT
        return MouseEventWidescreen(Picked, &Event);
#else
        return Picked->MouseEvent(&Event);
#endif
    }

    // DAT_0067dbf0 (static init LAB_0059d060).
    FWin3DInputHandler G_Win3DInputHandler;
}

void Win3DTransformByMatrix(Vector& Out, Matrix const& Mat, Vector const& Source)
{
    // FUN_00423b90
    TransformByMatrix(Out, Mat, Source);
}

I3DMathEngine* Win3DTransformMathEngine()
{
    if (G_TransformMathEngine == nullptr)
        G_TransformMathEngine = Acquire3DMathEngine();
    return G_TransformMathEngine;
}

void FSpaceCamera::ScreenToNearPlane(Vector& Out, float ScreenX, float ScreenY) const
{
    // FUN_004bdd40 (thiscall on the camera). Each product is rounded to float.
    double const Near = static_cast<double>(ZNear);
    Out.x = static_cast<float>((static_cast<double>(ScreenX) - ScreenCenterX) * (Near / XScale));
    Out.y = static_cast<float>((static_cast<double>(ScreenY) - ScreenCenterY) * (Near / YScale));
    Out.z = static_cast<float>(-Near);
}
