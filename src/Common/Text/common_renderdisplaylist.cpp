// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// RenderDisplayList, StyleCollection, LayoutRichText and lex_compare (retail
// RichDisplayList.cpp).

#include "common_text.h"
#include "Text/common_text_internal.h"
#include "dacom.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <map>
#include <string>
#include <deque>
#include <list>
#include <vector>
#include <windows.h>

// LayoutRichText (.text:06326600). Lays Rdl out into OutRdl as lines no wider than MaxWidth:
// every committed node is cloned into OutRdl behind a PositionNode that places it. OriginH /
// OriginV are the H/V origin, bSingleLine is single-line mode (truncate with a "." marker),
// OutHeight receives the laid-out height, bWordWrap allows word wrap. The result is true when
// anything was split, deferred or dropped.
//
// The "." is measured in the font of its TRANode {Attrs 0x10, mask 0xff}; in single-line mode a
// dot narrower than MaxWidth reserves its width. RDLRefNode and StyleNode push a frame over their
// list and are not laid out themselves. A node that does not fit tries, in order: a word split
// (flag 0, only with bWordWrap and not bSingleLine), a deferral to the next line (bWordWrap and
// the line not empty), a hard split (flag 1); a node that cannot be split is committed whole
// (dropped in single-line mode). Justify 1 centres, 2 right-aligns, anything else is left. Retail clones each
// node into a temporary before the OutRdl insert (FUN_0632c490) and deletes it after; the port
// keeps that.
namespace
{
    // One frame of retail's deque (8 bytes): the next list node and the list's sentinel.
    struct FLayoutFrame
    {
        FRdlListNode* Cur;
        FRdlListNode* End;
    };

    // One line entry (retail payload 0x18 of the 0x20-byte node, copied by FUN_0632d5a0):
    // VisualSize +0x00, content +0x10, tag byte +0x14 (1 = paragraph end, not emitted).
    struct FLayoutLineEntry
    {
        VisualSize Size;
        RDLNode* Content;
        unsigned char Tag;
    };
}

namespace CmnText
{
    // FUN_0632c490: RenderDisplayList insert before Where (a null prev link is replaced by the
    // new node itself); the payload is FUN_0632d550, a Clone() of *Value. Every caller passes
    // the list's sentinel, i.e. an append.
    void InsertRdlCloneBefore(RenderDisplayList& Rdl, FRdlListNode* Where, RDLNode* const& Value)
    {
        FRdlListNode* const NewEntry = (FRdlListNode*)malloc(sizeof(FRdlListNode));
        FRdlListNode* Prev = Where->Prev;
        NewEntry->Next = Where;
        if (Prev == nullptr)
        {
            Prev = NewEntry;
        }
        NewEntry->Prev = Prev;
        Where->Prev = NewEntry;
        NewEntry->Prev->Next = NewEntry;
        NewEntry->Node = Value->Clone();
        ++Rdl.Count;
    }
}

bool __cdecl LayoutRichText(TextRenderContext& Ctx, RenderDisplayList const& Rdl, int MaxWidth, RenderDisplayList& OutRdl, int OriginH, int OriginV, bool bSingleLine, int* OutHeight, bool bWordWrap)
{
    bool bResult = false;
    std::list<RDLNode*> Fragments;        // split fragments, deleted at the end

    TextRenderContext::State SavedState;
    Ctx.get_state(SavedState);
    Ctx.StateStack.push_front(SavedState);
    const int FullWidth = MaxWidth;

    // Retail builds the TRANode without a ctor call: set_attributes, +0x10 zeroed by hand.
    TextNode Ellipsis;
    Ellipsis.set_text(L".", -1);
    TextRenderAttributes EllipsisAttrs;
    EllipsisAttrs.Packed = 0x10;
    TRANode EllipsisAttrNode;
    EllipsisAttrNode.set_attributes(EllipsisAttrs, 0xff);
    EllipsisAttrNode.set_default_mask(0);

    Ctx.get_state(SavedState);
    Ctx.StateStack.push_front(SavedState);
    {
        // TRANode::Execute inlined.
        const unsigned long Current = Ctx.CurrentAttrs.Packed;
        const unsigned long Default = Ctx.get_default_attributes().Packed;
        const unsigned long DefMask = EllipsisAttrNode.get_default_mask();
        const unsigned long Mask = EllipsisAttrNode.get_mask();
        const unsigned long Attrs = EllipsisAttrNode.get_attributes().Packed;
        Ctx.CurrentAttrs.Packed = ((((~DefMask) & Attrs) | (DefMask & Default)) & Mask) | ((~Mask) & Current);
    }
    VisualSize EllipsisSize;
    {
        const std::wstring& Dot = Ellipsis.get_text();
        Ctx.get_text_visual_size(Dot.c_str(), EllipsisSize, (int)Dot.size());
    }
    Ctx.set_state(Ctx.StateStack.front());
    Ctx.StateStack.pop_front();

    if (bSingleLine && EllipsisSize.Width < MaxWidth)
    {
        MaxWidth -= EllipsisSize.Width;
    }

    std::list<FLayoutLineEntry> Line;
    int JustifyMode = 0;
    int VCursor = OriginV;
    int Remaining = MaxWidth;               // width left on the line
    std::deque<FLayoutFrame> Frames;
    {
        FRdlListNode* const Sentinel = Rdl.NodeList;
        FLayoutFrame Root = { Sentinel->Next, Sentinel };
        Frames.push_back(Root);
    }
    RDLNode* Node = nullptr;              // the pending node

    for (;;)
    {
        if (Frames.size() == 0 && Node == nullptr && Line.size() == 0)
        {
            break;
        }
        bool bBroken = false;
        bool bFlush = false;

        if (Node == nullptr)
        {
            if (Frames.size() == 0)
            {
                bFlush = true;
            }
            else
            {
                FLayoutFrame& Top = Frames.back();
                if (Top.Cur == Top.End)
                {
                    Frames.pop_back();
                    continue;
                }
                Node = Top.Cur->Node;
                Top.Cur = Top.Cur->Next;

                RDLRefNode* const Ref = Node->operator RDLRefNode*();
                if (Ref != nullptr)
                {
                    FRdlListNode* const Sentinel = Ref->get_list().NodeList;
                    FLayoutFrame Frame = { Sentinel->Next, Sentinel };
                    Frames.push_back(Frame);
                    Node = nullptr;
                    continue;
                }
                StyleNode* const Style = Node->operator StyleNode*();
                if (Style != nullptr)
                {
                    RenderDisplayList& StyleList = StyleCollection::get_style(Style->get_style_id());
                    FRdlListNode* const Sentinel = StyleList.NodeList;
                    FLayoutFrame Frame = { Sentinel->Next, Sentinel };
                    Frames.push_back(Frame);
                    Node = nullptr;
                    continue;
                }
            }
        }

        if (!bFlush)
        {
            ParagraphNode* const Paragraph = Node->operator ParagraphNode*();
            JustifyNode* const Justify = Node->operator JustifyNode*();
            if (Paragraph != nullptr)
            {
                Node->Execute(Ctx, false);
                FLayoutLineEntry Entry;
                Node->GetVisualSize(Ctx, Entry.Size);
                Entry.Content = Node;
                Entry.Tag = 1;
                Line.push_back(Entry);
                Node = nullptr;
            }
            else if (Justify != nullptr)
            {
                JustifyMode = (int)Justify->get_justify();
                Node->Execute(Ctx, false);
                Node = nullptr;
                continue;
            }
            else
            {
                VisualSize Size;
                Node->GetVisualSize(Ctx, Size);
                if (Size.Width <= Remaining)
                {
                    Node->Execute(Ctx, false);
                    FLayoutLineEntry Entry = { Size, Node, 0 };
                    Line.push_back(Entry);
                    Remaining -= Size.Width;
                    Node = nullptr;
                    continue;
                }

                RDLNode* Left = nullptr;
                RDLNode* Right = nullptr;
                bool bDone = false;
                if (!bSingleLine)
                {
                    if (bWordWrap && Node->SplitAtSize(Ctx, Remaining, 0, Left, Right))
                    {
                        Fragments.push_back(Left);
                        Fragments.push_back(Right);
                        Left->GetVisualSize(Ctx, Size);
                        FLayoutLineEntry Entry = { Size, Left, 0 };
                        Line.push_back(Entry);
                        Node = Right;
                        bBroken = true;
                        bResult = true;
                        bDone = true;
                    }
                    else if (Remaining != MaxWidth && bWordWrap)
                    {
                        bBroken = true;   // the node waits for the next line
                        bResult = true;
                        bDone = true;
                    }
                }
                if (!bDone)
                {
                    if (Node->SplitAtSize(Ctx, Remaining, 1, Left, Right))
                    {
                        Fragments.push_back(Left);
                        Fragments.push_back(Right);
                        Left->GetVisualSize(Ctx, Size);
                        FLayoutLineEntry Entry = { Size, Left, 0 };
                        Line.push_back(Entry);
                        Node = Right;
                    }
                    else if (bSingleLine) // dropped
                    {
                        Node = nullptr;
                    }
                    else // committed whole, overflowing
                    {
                        FLayoutLineEntry Entry = { Size, Node, 0 };
                        Line.push_back(Entry);
                        Node = nullptr;
                    }
                    bBroken = true;
                    bResult = true;
                }
            }
        }

        // Line flush. In single-line mode the whole line is dropped when even the dot does not
        // fit.
        if (bSingleLine && bBroken)
        {
            if (EllipsisSize.Width >= FullWidth)
            {
                Line.clear();
            }
            FLayoutLineEntry Spacer = { { 0, 0, 0, 0 }, &EllipsisAttrNode, 0 };
            Line.push_back(Spacer);
            FLayoutLineEntry Dot = { EllipsisSize, &Ellipsis, 0 };
            Line.push_back(Dot);
        }

        int LineWidth = 0;
        int MaxBelow = 0;
        int MaxAscent = 0;
        for (std::list<FLayoutLineEntry>::const_iterator It = Line.begin(); It != Line.end(); ++It)
        {
            const int Below = It->Size.Height - It->Size.Ascent;
            if (Below > MaxBelow)
            {
                MaxBelow = Below;
            }
            if (It->Size.Ascent > MaxAscent)
            {
                MaxAscent = It->Size.Ascent;
            }
            LineWidth += It->Size.Width;
        }
        const int LineHeight = MaxBelow + MaxAscent;

        int HCursor = OriginH;
        if (JustifyMode == 1)
        {
            HCursor = (FullWidth - LineWidth) / 2 + OriginH;
        }
        else if (JustifyMode == 2)
        {
            HCursor = FullWidth - LineWidth + OriginH;
        }

        FRdlListNode* const OutSentinel = OutRdl.NodeList;
        for (std::list<FLayoutLineEntry>::const_iterator It = Line.begin(); It != Line.end(); ++It)
        {
            if (It->Tag == 0)
            {
                if (It->Size.Width > 0)
                {
                    PositionNode Pos;
                    Pos.set_pos((float)HCursor,
                                (float)(MaxAscent - It->Size.Ascent + VCursor));
                    RDLNode* const PosClone = Pos.Clone();
                    InsertRdlCloneBefore(OutRdl, OutSentinel, PosClone);
                    delete PosClone;
                }
                RDLNode* const ContentClone = It->Content->Clone();
                InsertRdlCloneBefore(OutRdl, OutSentinel, ContentClone);
                delete ContentClone;
            }
            HCursor += It->Size.Width;
        }

        VCursor += LineHeight;
        Line.clear();
        Remaining = MaxWidth;
        if (bSingleLine)
        {
            break;
        }
    }

    for (std::list<RDLNode*>::iterator It = Fragments.begin(); It != Fragments.end(); ++It)
    {
        if (*It != nullptr)
        {
            delete *It;
        }
    }
    Fragments.clear();

    {
        const TextRenderContext::State& First = Ctx.StateStack.front();
        Ctx.CurrentAttrs.Packed = First.Attributes;
        Ctx.set_pos(First.PosH, First.PosV);
        Ctx.set_clip_rect(First.ClipRect);
        Ctx.StateStack.pop_front();
    }

    if (OutHeight != nullptr)
    {
        *OutHeight = VCursor - OriginV;
    }
    return bResult;
}

// .text:0632a020. Compares the text runs (TextNode / TextPtrNode) of two display lists, raw when
// bCaseSensitive is set, through towlower otherwise; returns the first difference (unsigned
// 16-bit code units subtracted).
// Two retail quirks reproduced as they are (deterministic):
//   * B's cursor is only advanced past nodes WITHOUT text; after a run is found it stays on that
//     node, so every refill of B re-reads the same run. Equal names therefore compare as -1, not 0.
//   * B's end test compares B's cursor with A's sentinel.
// RETAIL UB, not reproduced: with distinct lists the B walk never meets A's sentinel, so a B
// with no text left runs onto its own sentinel and dereferences its uninitialised payload.
// The port also stops at B's own sentinel (same result as reaching the end).
int __cdecl lex_compare(RenderDisplayList const& A, RenderDisplayList const& B, bool bCaseSensitive)
{
    FRdlListNode* const ASentinel = A.NodeList;
    FRdlListNode* const BSentinel = B.NodeList;
    FRdlListNode* ACur = ASentinel->Next;
    FRdlListNode* BCur = BSentinel->Next;

    const wchar_t* AText = nullptr;
    int ALen = 0;
    const wchar_t* BText = nullptr;
    int BLen = 0;

    for (;;)
    {
        BText = nullptr;
        BLen = 0;
        for (;;)
        {
            while (AText == nullptr && ACur != ASentinel)
            {
                RDLNode* const Node = ACur->Node;
                TextNode* const AsText = Node->operator TextNode*();
                TextPtrNode* const AsPtr = Node->operator TextPtrNode*();
                if (AsText != nullptr)
                {
                    ALen = (int)AsText->get_text().size();
                    if (ALen != 0)
                    {
                        AText = AsText->get_text().c_str();
                    }
                }
                else if (AsPtr != nullptr)
                {
                    ALen = AsPtr->get_text_length();
                    AText = AsPtr->get_text();
                }
                ACur = ACur->Next;
            }

            if (BText == nullptr)
            {
                for (;;)
                {
                    if (BCur == ASentinel || BCur == BSentinel)
                    {
                        return AText != nullptr ? 1 : 0;
                    }
                    RDLNode* const Node = BCur->Node;
                    TextNode* const AsText = Node->operator TextNode*();
                    TextPtrNode* const AsPtr = Node->operator TextPtrNode*();
                    if (AsText != nullptr)
                    {
                        BLen = (int)AsText->get_text().size();
                        if (BLen != 0)
                        {
                            BText = AsText->get_text().c_str();
                            break; // cursor stays on this node
                        }
                    }
                    else if (AsPtr != nullptr)
                    {
                        BText = AsPtr->get_text();
                        BLen = AsPtr->get_text_length();
                        if (BText == nullptr)
                        {
                            return AText != nullptr ? 1 : 0;
                        }
                        break; // cursor stays on this node
                    }
                    BCur = BCur->Next;
                }
            }

            if (AText == nullptr)
            {
                return -1;
            }

            bool bAExhausted = true;
            while (ALen > 0)
            {
                if (BLen <= 0)
                {
                    bAExhausted = false; // A still has characters
                    break;
                }
                unsigned int CharA = 0;
                unsigned int CharB = 0;
                if (bCaseSensitive)
                {
                    CharA = (unsigned short)*AText;
                    CharB = (unsigned short)*BText;
                }
                else
                {
                    CharA = (unsigned short)towlower(*AText);
                    CharB = (unsigned short)towlower(*BText);
                }
                const int Diff = (int)CharA - (int)CharB;
                if (Diff != 0)
                {
                    return Diff;
                }
                ++AText;
                ++BText;
                --ALen;
                --BLen;
            }
            if (bAExhausted)
            {
                AText = nullptr;
                ALen = 0;
            }

            if (BLen > 0)
            {
                continue; // B keeps its run
            }
            break; // B's run is dropped
        }
    }
}

// ============================================================================
// StyleCollection -- static style registry.
// ============================================================================

// Retail .data:06401950 (static initializer 0x0632a1d0, atexit 0x0632a200).
std::map<unsigned short, StyleCollection::Style> StyleCollection::mStyles;

namespace
{
    // The erase-everything loop retail inlines into set_style(Rdl) and FUN_0632a2f0 (node holder
    // dtor FUN_0632d6e0). The sentinel stays.
    void EraseAllStyleNodes(RenderDisplayList& Rdl)
    {
        FRdlListNode* const Sentinel = Rdl.NodeList;
        FRdlListNode* Cur = Sentinel->Next;
        while (Cur != Sentinel)
        {
            FRdlListNode* const Next = Cur->Next;
            FRdlListNode* const Prev = Cur->Prev;
            Prev->Next = Next;
            Next->Prev = Prev;
            RDLNode* Node = Cur->Node;
            if (Node != nullptr)
            {
                delete Node;
            }
            free(Cur);
            Rdl.Count--;
            Cur = Next;
        }
    }

    // FUN_0632a2f0, called by set_style(Id, Attrs); retail's name is not recoverable (nothing
    // about Style is exported). No null test of the first clone.
    void ResetStyleToAttributes(StyleCollection::Style& Entry, TextRenderAttributes const& Attrs)
    {
        EraseAllStyleNodes(Entry);
        TRANode Temp(Attrs, 0xFFFFFFFFu, 0u);
        RDLNode* const FirstClone = Temp.Clone();
        InsertRdlCloneBefore(Entry, Entry.NodeList, FirstClone);
        delete FirstClone;
    }
}

void __cdecl StyleCollection::set_style(unsigned short Id, const RenderDisplayList& Rdl)
{
    // RVA 0xCA3F0 (operator[] is FUN_0632c190). Retail reads Rdl's first node before the erase
    // loop; passing the entry itself (set_style(Id, get_style(Id))) therefore walks freed nodes
    // in retail. The port reads it after the erase, so that case leaves the entry empty.
    Style& Entry = mStyles[Id];
    EraseAllStyleNodes(Entry);
    FRdlListNode* const SrcSentinel = Rdl.NodeList;
    for (FRdlListNode* SrcCur = SrcSentinel->Next; SrcCur != SrcSentinel; SrcCur = SrcCur->Next)
    {
        InsertRdlCloneBefore(Entry, Entry.NodeList, SrcCur->Node);
    }
}

void __cdecl StyleCollection::set_style(unsigned short Id, const TextRenderAttributes& Attrs)
{
    // RVA 0xCA4B0
    ResetStyleToAttributes(mStyles[Id], Attrs);
}

RenderDisplayList& __cdecl StyleCollection::get_style(unsigned short Id)
{
    // RVA 0xCA4D0. A lookup, not operator[]: a miss inserts nothing and returns a
    // function-local static empty list (.data:06401940, guard bit 0 of DAT_0640194c).
    std::map<unsigned short, Style>::iterator It = mStyles.find(Id);
    if (It != mStyles.end())
    {
        return It->second;
    }
    static RenderDisplayList EmptyStyle;
    return EmptyStyle;
}

void __cdecl StyleCollection::remove_style(unsigned short)
{
    // 0x062a8920 (ICF-folded empty body). Retail never removes a style.
}

// ============================================================================
// RenderDisplayList -- linked list of RDLNode pointers, [0] = next, [1] = prev.
// ============================================================================

RenderDisplayList::RenderDisplayList()
{
    // RVA 0xC5BB0 (ordinal 394). Retail leaves the +0x00 allocator byte uninitialised (read
    // from fresh stack scratch); the port zeroes it.
    Allocator = 0;
    FRdlListNode* const Sentinel = (FRdlListNode*)malloc(sizeof(FRdlListNode));
    Sentinel->Next = Sentinel;
    Sentinel->Prev = Sentinel;
    NodeList = Sentinel;
    Count = 0;
}

RenderDisplayList::RenderDisplayList(const RenderDisplayList& Other)
{
    // .text:06325be0. No null tests of the head, the source list or the source nodes.
    Allocator = Other.Allocator;
    FRdlListNode* const Sentinel = (FRdlListNode*)malloc(sizeof(FRdlListNode));
    Sentinel->Next = Sentinel;
    Sentinel->Prev = Sentinel;
    NodeList = Sentinel;
    Count = 0;

    // Append in order (sentinel[1] is the tail).
    FRdlListNode* const OtherSentinel = Other.NodeList;
    for (FRdlListNode* OtherCur = OtherSentinel->Next; OtherCur != OtherSentinel; OtherCur = OtherCur->Next)
    {
        RDLNode* SrcNode = OtherCur->Node;

        FRdlListNode* const NewEntry = (FRdlListNode*)malloc(sizeof(FRdlListNode));
        FRdlListNode* Tail = Sentinel->Prev;
        if (Tail == nullptr) Tail = NewEntry;   // retail's degenerate-head fixup
        NewEntry->Next = Sentinel;
        NewEntry->Prev = Tail;
        Sentinel->Prev = NewEntry;
        Tail->Next = NewEntry;
        NewEntry->Node = SrcNode->Clone();
        Count++;
    }
}

RenderDisplayList::~RenderDisplayList()
{
    // .text:06325d90 (node holder dtor FUN_063272c0). +0x00 is not touched.
    FRdlListNode* const Sentinel = NodeList;
    FRdlListNode* Cur = Sentinel->Next;
    while (Cur != Sentinel)
    {
        FRdlListNode* const Next = Cur->Next;
        FRdlListNode* const Prev = Cur->Prev;
        Prev->Next = Next;
        Next->Prev = Prev;
        if (Cur->Node != nullptr)
        {
            delete Cur->Node;
            Cur->Node = nullptr;
        }
        free(Cur);
        --Count;
        Cur = Next;
    }
    free(Sentinel);
    NodeList = nullptr;
    Count = 0;
}

RenderDisplayList& RenderDisplayList::operator=(const RenderDisplayList& Other)
{
    // .text:06325cb0: VC6 list::operator= over an owning-pointer element whose ASSIGNMENT is a
    // plain pointer copy. +0x00 is not touched (only the copy constructor copies that byte).
    //
    // RETAIL UB, not reproduced: step 1 leaks this list's old nodes and leaves both lists
    // pointing at the same RDLNode objects, so destroying either list later frees nodes the
    // other still uses (a double delete once both are gone). The port keeps the shape but
    // makes step 1 deterministic: the old node is deleted and Other's node is cloned into the
    // slot, so the content after the assignment is what retail's is, without the shared
    // ownership.
    if (this == &Other)
    {
        return *this;
    }

    FRdlListNode* const Sentinel = NodeList;
    FRdlListNode* const OtherSentinel = Other.NodeList;
    FRdlListNode* Cur = Sentinel->Next;
    FRdlListNode* OtherCur = OtherSentinel->Next;

    // 1. Common prefix.
    while (Cur != Sentinel && OtherCur != OtherSentinel)
    {
        RDLNode* const Old = Cur->Node;
        RDLNode* const Src = OtherCur->Node;
        Cur->Node = (Src != nullptr) ? Src->Clone() : nullptr; // retail copies the pointer, null too
        delete Old;
        Cur = Cur->Next;
        OtherCur = OtherCur->Next;
    }

    // 2. Erase this list's surplus.
    while (Cur != Sentinel)
    {
        FRdlListNode* const Next = Cur->Next;
        FRdlListNode* const Prev = Cur->Prev;
        Prev->Next = Next;
        Next->Prev = Prev;
        if (Cur->Node != nullptr)
        {
            delete Cur->Node;
            Cur->Node = nullptr;
        }
        free(Cur);
        --Count;
        Cur = Next;
    }

    // 3. Clone Other's remainder onto the tail (insert before the sentinel).
    while (OtherCur != OtherSentinel)
    {
        FRdlListNode* const NewEntry = (FRdlListNode*)malloc(sizeof(FRdlListNode));
        FRdlListNode* Tail = Sentinel->Prev;
        if (Tail == nullptr)
        {
            Tail = NewEntry; // retail's degenerate-head fixup
        }
        NewEntry->Next = Sentinel;
        NewEntry->Prev = Tail;
        Sentinel->Prev = NewEntry;
        Tail->Next = NewEntry;
        NewEntry->Node = OtherCur->Node->Clone();
        ++Count;
        OtherCur = OtherCur->Next;
    }
    return *this;
}

void RenderDisplayList::execute(TextRenderContext& Ctx, bool bDraw)
{
    // .text:06329cd0. No null tests in retail.
    FRdlListNode* const Sentinel = NodeList;
    for (FRdlListNode* Cur = Sentinel->Next; Cur != Sentinel; Cur = Cur->Next)
    {
        Cur->Node->Execute(Ctx, bDraw);
    }
}

void RenderDisplayList::update(float Dt)
{
    // .text:06329d00. No null tests in retail.
    FRdlListNode* const Sentinel = NodeList;
    for (FRdlListNode* Cur = Sentinel->Next; Cur != Sentinel; Cur = Cur->Next)
    {
        Cur->Node->Update(Dt);
    }
}

void RenderDisplayList::get_dimensions(TextRenderContext& Ctx, float& OutWidth,
                                        float& OutHeight, float& OutDepth)
{
    // .text:06329d30. push_state is inlined with its 0x20-byte record built in the stack
    // bytes that are then reused as the VisualSize every GetVisualSize below writes into, so
    // NodeSize starts out as the record: Ascent (+0x08) = the saved PosV. Only Ascent is read.
    VisualSize NodeSize;
    NodeSize.Width = (int)Ctx.CurrentAttrs.Packed;
    NodeSize.Height = 0; // record +0x04, never written by retail; never read here
    NodeSize.Ascent = Ctx.get_pos_v();
    NodeSize.Descent = Ctx.get_pos_h();
    Ctx.push_state();

    Ctx.reset_to_default();
    Ctx.set_pos(0, 0);

    float MinV = 100000.0f;
    float MaxAscent = 0.0f;
    float MinH = 100000.0f;
    float MaxH = 0.0f;
    float MaxV = 0.0f;

    FRdlListNode* const Sentinel = NodeList;
    for (FRdlListNode* Cur = Sentinel->Next; Cur != Sentinel; Cur = Cur->Next)
    {
        RDLNode* const Node = Cur->Node;

        // A node whose GetVisualSize writes nothing (RDLRefNode) leaves the previous Ascent in
        // place.
        Node->GetVisualSize(Ctx, NodeSize);
        Node->Execute(Ctx, false);

        // PosH stays on the x87 stack (an exact double); PosV is stored to a float. The raw pen
        // fields, no origin.
        const double CurH = (double)Ctx.get_pos_h();
        const float CurV = (float)Ctx.get_pos_v();

        if (!(MaxH > CurH))
        {
            MaxH = (float)CurH;
        }
        if (!(MinH < CurH))
        {
            MinH = (float)CurH;
        }
        if (!(MaxV > CurV))
        {
            MaxV = CurV;
        }
        if (!(MinV < CurV))
        {
            MinV = CurV;
        }
        const double Ascent = (double)NodeSize.Ascent;
        if (!(MaxAscent > Ascent))
        {
            MaxAscent = (float)Ascent;
        }
    }

    Ctx.pop_state();

    OutWidth = MaxH - MinH;
    OutHeight = MaxV - MinV;
    OutDepth = MaxAscent;
}

void RenderDisplayList::optimize()
{
    // 0x062a8920 (ICF-folded empty body). Retail's optimize does nothing.
}

int RenderDisplayList::get_element_count() const
{
    // .text:06329f30: the sum of every node's GetElementSize -- not the node count.
    int Total = 0;
    FRdlListNode* const Sentinel = NodeList;
    for (FRdlListNode* Cur = Sentinel->Next; Cur != Sentinel; Cur = Cur->Next)
    {
        int NodeSize = 0;
        Cur->Node->GetElementSize(NodeSize);
        Total += NodeSize;
    }
    return Total;
}

bool RenderDisplayList::get_element_pos(TextRenderContext& Ctx, int Index,
                                         float& OutH, float& OutV, float& OutDepth)
{
    // .text:06329f70. The outputs are written and the result is true whether or not Index
    // was found.
    int LocalPos = 0;
    FRdlListNode* const Sentinel = NodeList;
    for (FRdlListNode* Cur = Sentinel->Next; Cur != Sentinel; Cur = Cur->Next)
    {
        RDLNode* const Node = Cur->Node;
        int Size = 0;
        Node->GetElementSize(Size);
        if (Index < Size)
        {
            Node->GetElementPos(Index, Ctx, LocalPos);
            break;
        }
        Node->Execute(Ctx, false);
        Index -= Size;
    }

    OutH = (float)(Ctx.get_pos_h() + LocalPos);
    OutV = (float)Ctx.get_pos_v();
    OutDepth = (float)Ctx.get_cell_height();
    return true;
}
