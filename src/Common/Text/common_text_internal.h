// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Common.dll-internal declarations shared by the RichDisplayList translation units:
// common_textrender.cpp, common_rdlnodes.cpp, common_renderdisplaylist.cpp and common_rdlio.cpp.
// Retail keeps them in one TU (RichDisplayList.cpp); none of these is exported.

#include "common_text.h"

namespace CmnText
{
    // DAT_0640957c: global default text attributes (TextRenderAttributes::Packed layout:
    // byte0 = style index, bytes 1-3 = color), seeded by the static initializer at RVA 0xc6290.
    // TextRenderContext, both DeviceTRC ctors and TRANode (0xc7380/0xc7b40/0xc7bf0/0xc8658) copy
    // its current value at construction.
    static const unsigned long GDefaultTextAttribsPacked = 0xFFFFFF00u;

    // DAT_063ed66c, the rich-text scale InitializeRichText / ReinitializeRichTextFonts set.
    // Defined in common_textrender.cpp; DeviceTRC::execute_image and ImageNode::GetVisualSize
    // (common_rdlnodes.cpp) read it.
    extern float GRichTextScaleY;

    // FUN_0632c490: links a new cell before Where and stores Value->Clone() in it.
    void InsertRdlCloneBefore(RenderDisplayList& Rdl, FRdlListNode* Where, RDLNode* const& Value);

    // The append sequence of every RDLX tag and binary RDL record (clone, insert a clone of the
    // clone, delete the first clone). Defined in common_rdlio.cpp.
    void RdlxAppendNode(RenderDisplayList& Rdl, RDLNode const& Node);
}

using namespace CmnText;
