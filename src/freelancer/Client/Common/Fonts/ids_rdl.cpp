// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// FUN_0057da40 / FUN_0057db50 -- IDS resource and plain UTF-16 text into a RenderDisplayList.

#include "Common/Fonts/ids_rdl.h"
#include "Common/Fonts/ids_strings.h"

#include <stdlib.h>

namespace
{
    // Retail's cell is 0x0C: {next, prev, RDLNode*}, circular through a sentinel whose [0] is
    // the head and [1] the tail (Common's RenderDisplayList ctor 0x06325bb0).
    const unsigned kCellBytes = 3 * sizeof(void*);

    // The single space at .rdata 0x005cef90, which retail substitutes for an EMPTY run before a
    // LF or a VT -- but never before a CR.
    const wchar_t kEmptyLineText[] = L" ";
}

void RdlAppendNode(RenderDisplayList& Rdl, RDLNode* Node)
{
    // Deviation: retail clones TWICE at each site (the list clones the caller's clone into the
    // cell and deletes the intermediate); the surviving object is the same, so the port stores
    // the caller's clone directly.
    //
    // FUN_0046bb00 / FUN_0046c800 (insert before the sentinel; a null tail gives the VC6
    // _Buynode self-link). No test of the list or the node. Cells are malloc'd like every cell
    // Common's RenderDisplayList makes and frees.
    void** const Sentinel = reinterpret_cast<void**>(Rdl.NodeList);
    void** const Tail = static_cast<void**>(Sentinel[1]);
    void** const Cell = static_cast<void**>(malloc(kCellBytes));
    Cell[0] = Sentinel;
    Cell[1] = Tail != nullptr ? Tail : Cell;
    Sentinel[1] = Cell;
    static_cast<void**>(Cell[1])[0] = Cell;
    Cell[2] = Node;
    Rdl.Count += 1;
}

void RdlClear(RenderDisplayList& Rdl)
{
    // FUN_0046bb50 (fastcall on the list). The sentinel stays; nothing else is tested.
    void** const Sentinel = reinterpret_cast<void**>(Rdl.NodeList);
    void** Cur = static_cast<void**>(Sentinel[0]);
    while (Cur != Sentinel)
    {
        void** const Cell = Cur;
        Cur = static_cast<void**>(Cur[0]);
        static_cast<void**>(Cell[1])[0] = Cell[0];
        static_cast<void**>(Cell[0])[1] = Cell[1];
        RDLNode* const Node = static_cast<RDLNode*>(Cell[2]);
        if (Node != nullptr)
        {
            delete Node;
            Cell[2] = nullptr;
        }
        free(Cell);
        Rdl.Count -= 1;
    }
}

bool RdlRemoveLastParagraph(RenderDisplayList& Rdl)
{
    // FUN_0057eb40: scans from the tail (vt+0x38 = operator ParagraphNode*), stops after the
    // head. Retail returns whatever is left in EAX; every call site ignores it. The port
    // reports found / not found.
    void** const Sentinel = reinterpret_cast<void**>(Rdl.NodeList);
    void** Cur = static_cast<void**>(Sentinel[1]);

    while (Cur != Sentinel)
    {
        RDLNode* const Node = static_cast<RDLNode*>(Cur[2]);
        if (Node->operator ParagraphNode*() != nullptr)
        {
            void** const Next = static_cast<void**>(Cur[0]);
            void** const Prev = static_cast<void**>(Cur[1]);
            Prev[0] = Next;
            Next[1] = Prev;

            delete Node;
            free(Cur);
            Rdl.Count -= 1;
            return true;
        }

        if (Cur == static_cast<void**>(Sentinel[0]))
            break;
        Cur = static_cast<void**>(Cur[1]);
    }

    return false;
}

namespace
{
    void EmitRun(RenderDisplayList& Rdl, const wchar_t* Start, const wchar_t* End,
                 bool bUseTextPtr)
    {
        const int Len = static_cast<int>(End - Start);
        if (Len <= 0)
            return;

        // Retail builds the node on the stack and appends a CLONE of it.
        if (bUseTextPtr)
        {
            TextPtrNode Node(Start, Len);
            RdlAppendNode(Rdl, Node.Clone());
        }
        else
        {
            TextNode Node(Start, Len);
            RdlAppendNode(Rdl, Node.Clone());
        }
    }

    void EmitEmptyLine(RenderDisplayList& Rdl)
    {
        // Always a TextNode, never a TextPtrNode, whatever the flag.
        TextNode Node(kEmptyLineText, -1);
        RdlAppendNode(Rdl, Node.Clone());
    }

    void EmitParagraph(RenderDisplayList& Rdl)
    {
        ParagraphNode Node;
        RdlAppendNode(Rdl, Node.Clone());
    }
}

void AppendTextMaybeXmlRdl(const wchar_t* Text, unsigned int Count, RenderDisplayList& Rdl,
                           bool bUseTextPtr)
{
    // FUN_0057e2c0: no test of either argument; a failed XML read falls into FUN_0057db50.
    XMLReader Reader;
    if (Reader.read_buffer(Rdl, reinterpret_cast<const char*>(Text), Count * 2u))
        return;
    AppendTextAsRdl(Text, Count, Rdl, bUseTextPtr);
}

void AppendTextAsRdl(const wchar_t* Text, unsigned int Count, RenderDisplayList& Rdl,
                     bool bUseTextPtr)
{
    // FUN_0057db50. A zero length does nothing at all, not even an empty paragraph. Text itself
    // is not tested.
    if (Count == 0)
        return;

    const wchar_t* RunStart = Text;
    const wchar_t* Cursor = Text;
    unsigned int Index = 0;

    while (Index < Count)
    {
        const wchar_t Ch = *Cursor;

        if (Ch == L'\r')
        {
            // A CR with nothing pending emits NO filler -- unlike LF and VT.
            EmitRun(Rdl, RunStart, Cursor, bUseTextPtr);
            EmitParagraph(Rdl);

            ++Cursor;
            ++Index;
            if (Index < Count && *Cursor == L'\n')
            {
                ++Cursor;
                ++Index;
            }
            RunStart = Cursor;
            continue;
        }

        if (Ch == L'\n' || Ch == L'\v')
        {
            // An empty run becomes a single space, so a blank line still occupies a line.
            if (RunStart == Cursor)
                EmitEmptyLine(Rdl);
            else
                EmitRun(Rdl, RunStart, Cursor, bUseTextPtr);

            EmitParagraph(Rdl);

            ++Cursor;
            ++Index;

            // After a LF retail also swallows a following CR. A VT does not.
            if (Ch == L'\n' && Index < Count && *Cursor == L'\r')
            {
                ++Cursor;
                ++Index;
            }
            RunStart = Cursor;
            continue;
        }

        ++Cursor;
        ++Index;
    }

    EmitRun(Rdl, RunStart, Cursor, bUseTextPtr);
}

namespace
{
    // FUN_0057da40's two resource lookups. Retail's custom RDL type is 23.
    const int kRdlResourceType = 23;
    const int kStringTableResourceType = 6;

    const wchar_t* LockRdlResource(HMODULE Module, unsigned int LowId, unsigned int& OutCount)
    {
        HRSRC const Info = FindResourceExA(Module, MAKEINTRESOURCEA(kRdlResourceType),
                                           MAKEINTRESOURCEA(LowId), 0);
        if (Info == nullptr)
            return nullptr;

        const DWORD Bytes = SizeofResource(Module, Info);
        HGLOBAL const Data = LoadResource(Module, Info);
        if (Data == nullptr)
            return nullptr;
        const wchar_t* Cursor = static_cast<const wchar_t*>(LockResource(Data));
        if (Cursor == nullptr)
            return nullptr;

        // The count is in characters, and a leading BOM is not part of the text.
        OutCount = Bytes / 2;
        if (OutCount != 0 && *reinterpret_cast<const unsigned short*>(Cursor) == 0xfeff)
        {
            OutCount -= 1;
            Cursor += 1;
        }
        return Cursor;
    }

    const wchar_t* LockStringTableEntry(HMODULE Module, unsigned int LowId,
                                        unsigned int& OutCount)
    {
        // The same block/entry arithmetic GetIdsStringW uses.
        const unsigned int BlockIndex = (LowId >> 4) + 1;
        const unsigned int EntryIndex = LowId & 0xfu;

        HRSRC const Info = FindResourceExA(Module, MAKEINTRESOURCEA(kStringTableResourceType),
                                           MAKEINTRESOURCEA(BlockIndex), 0);
        if (Info == nullptr)
            return nullptr;
        HGLOBAL const Data = LoadResource(Module, Info);
        if (Data == nullptr)
            return nullptr;
        const unsigned short* Cursor = static_cast<const unsigned short*>(LockResource(Data));
        if (Cursor == nullptr)
            return nullptr;

        for (unsigned int Skip = EntryIndex; Skip != 0; --Skip)
            Cursor = Cursor + Cursor[0] + 1;

        OutCount = Cursor[0];
        return reinterpret_cast<const wchar_t*>(Cursor + 1);
    }
}

bool LoadIdsIntoRdl(unsigned int Ids, RenderDisplayList& Rdl)
{
    HMODULE const Module = GetIdsResourceModule(Ids);
    if (Module == nullptr)
        return false;

    const unsigned int LowId = Ids & 0xffffu;

    unsigned int Count = 0;
    const wchar_t* Text = LockRdlResource(Module, LowId, Count);
    if (Text == nullptr)
        Text = LockStringTableEntry(Module, LowId, Count);
    if (Text == nullptr)
        return false;

    // FUN_0057da40: ordinary IDS strings are not XML and take the FUN_0057db50 split path;
    // RDLX / HTML infocard resources succeed on the XML branch.
    XMLReader Reader;
    if (!Reader.read_buffer(Rdl, reinterpret_cast<const char*>(Text), Count * 2u))
        AppendTextAsRdl(Text, Count, Rdl, true);
    return true;
}

bool LoadIdsTextIntoRdl(unsigned int Ids, RenderDisplayList& Rdl, const wchar_t* DebugText)
{
    // Inlined at ~143 retail sites, all the same five steps.
    if (LoadIdsIntoRdl(Ids, Rdl))
    {
        // Strip the resource's trailing break so the text does not push what follows it onto
        // the next line.
        RdlRemoveLastParagraph(Rdl);
        return true;
    }

    // No resource: a left-justified English stub instead.
    JustifyNode Justify(static_cast<TextJustify>(1));
    RdlAppendNode(Rdl, Justify.Clone());

    if (DebugText != nullptr)
    {
        TextNode Text(DebugText, -1);
        RdlAppendNode(Rdl, Text.Clone());
    }
    return false;
}
