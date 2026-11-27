// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "StdAfx.h"
#include "ChatRdl.h"

// .text:004149a0 (list insert before the sentinel; value construction 004155e0). A null tail
// links the cell to itself (VC6 _Buynode); Value's node is not tested for null. Cells are
// malloc'd because Common's ~RenderDisplayList frees them with free().
void AppendChatRdlNode(RenderDisplayList& Rdl, const FChatRdlNodeHolder& Value)
{
	FRdlListNode* const Sentinel = Rdl.NodeList;
	FRdlListNode* const Tail = Sentinel->Prev;
	FRdlListNode* const Cell = static_cast<FRdlListNode*>(malloc(sizeof(FRdlListNode)));
	Cell->Next = Sentinel;
	Cell->Prev = Tail != nullptr ? Tail : Cell;
	Sentinel->Prev = Cell;
	Cell->Prev->Next = Cell;
	Cell->Node = Value.Node->Clone();
	++Rdl.Count;
}
