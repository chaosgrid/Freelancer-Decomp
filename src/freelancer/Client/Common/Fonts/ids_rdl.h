// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail FUN_0057da40 and FUN_0057db50: turning an IDS resource -- or any UTF-16 run -- into a
// RenderDisplayList. This is the layer the whole NeuroNet UI sits on: every label, every chat
// prefix and every infocard reaches its RDL through one of these two.
//
// FUN_0057da40(ids, &rdl) resolves the module from the HIGH sixteen bits of the id, then:
//   - looks for a custom resource of type 23 with the low sixteen bits as its id. If it is
//     there, the whole resource is the text; a leading 0xFEFF byte-order mark is skipped.
//   - otherwise falls back to the ordinary STRINGTABLE (type 6), block (id >> 4) + 1, entry
//     id & 0xf, exactly like GetIdsStringW.
// It then tries XMLReader::read_buffer on the text and, only if that fails, hands the text to
// FUN_0057db50. Ordinary IDS strings are not XML, so the second path is the usual one.
//
// FUN_0057db50 splits a UTF-16 run on three characters -- CR, LF and VT -- emitting a TextNode
// (or TextPtrNode) for each stretch between them and a ParagraphNode for each break.

#include "common_text.h"

// The exe-side list append. Retail inlines std::list::push_back (FUN_0046c800) at every call
// site. The list takes the node.
void RdlAppendNode(RenderDisplayList& Rdl, RDLNode* Node);

// Releases every node and empties the list.
void RdlClear(RenderDisplayList& Rdl);

// FUN_0057db50. Count is in CHARACTERS. bUseTextPtr picks TextPtrNode (which keeps the caller's
// pointer) over TextNode (which copies); retail passes true from the IDS path.
void AppendTextAsRdl(const wchar_t* Text, unsigned int Count, RenderDisplayList& Rdl,
                     bool bUseTextPtr);

// FUN_0057e2c0: XMLReader::read_buffer, then AppendTextAsRdl (FUN_0057db50) on failure.
void AppendTextMaybeXmlRdl(const wchar_t* Text, unsigned int Count, RenderDisplayList& Rdl,
                           bool bUseTextPtr);

// FUN_0057eb40: deletes the LAST ParagraphNode in the list, in insertion order, and returns
// whether it found one.
//
// Retail tests vtable slot 0x38 on each node: every class inherits RDLNode's 0x06325390
// (null) there EXCEPT ParagraphNode, which installs 0x06261260 (returns `this`).
// The walk scans from the END, so it strips a TRAILING break -- without it a label whose
// resource ends in a newline would carry an extra blank line.
bool RdlRemoveLastParagraph(RenderDisplayList& Rdl);

// FUN_0057da40: resolve resource, XMLReader::read_buffer, else AppendTextAsRdl.
// Appends to whatever the list already holds. Returns false only when no module/resource.
bool LoadIdsIntoRdl(unsigned int Ids, RenderDisplayList& Rdl);

// The idiom every one of FUN_0057da40's 143 call sites spells out inline:
//
//     if (LoadIdsIntoRdl(ids, rdl))   RdlRemoveLastParagraph(rdl);
//     else                          { append JustifyNode(1); append TextNode(debug); }
//
// The debug string is an English stub compiled into the shipped binary, which is why the game
// still prints readable text when a resource DLL is missing or stripped. Returns true when the
// localised resource was used.
bool LoadIdsTextIntoRdl(unsigned int Ids, RenderDisplayList& Rdl, const wchar_t* DebugText);
