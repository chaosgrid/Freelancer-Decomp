// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll Watchable (the head of a BaseWatcher chain) and CommReferrable (the msg_id_prefix
// holder).

#include "common_net.h"
#include "common_ini.h" // INI_Reader -- CommReferrable::read_comm_ini_data
#include "common_goods.h" // BaseWatcher (the nodes of Watchable's chain)

// ===========================================================================
// Watchable
// ===========================================================================

Watchable::Watchable()
    : Head(nullptr)
{
}

Watchable::~Watchable()
{
    // .text:06280b30: JMP Watchable::unwatch (the count is discarded).
    unwatch();
}

Watchable& Watchable::operator=(const Watchable& Other)
{
    // Retail copies a single DWORD
    Head = Other.Head;
    return *this;
}

unsigned int Watchable::unwatch()
{
    // 0x063415e0: detaches every node and returns how many there were.
    unsigned int Count = 0;
    BaseWatcher* Node = Head;
    while (Node != nullptr)
    {
        BaseWatcher* const Next = Node->Next;
        Count++;
        Node->Watched = nullptr;
        Node->Next = nullptr;
        Node = Next;
    }
    Head = nullptr;
    return Count;
}

// ===========================================================================
// CommReferrable
// ===========================================================================

CommReferrable::CommReferrable()
{
    // .text:06261060
    MsgIdPrefix.Length = 0;
    MsgIdPrefix.Data[0] = '\0';
}

CommReferrable::CommReferrable(const CommReferrable& Other)
{
    // .text:06261080: the whole TString<64> block-copied.
    memcpy(&MsgIdPrefix, &Other.MsgIdPrefix, sizeof(MsgIdPrefix));
}

CommReferrable& CommReferrable::operator=(const CommReferrable& Other)
{
    // .text:062610a0: TString<64>::operator=(Other.MsgIdPrefix.Data) inlined; no self check.
    MsgIdPrefix = Other.MsgIdPrefix.Data;
    return *this;
}

bool CommReferrable::read_comm_ini_data(INI_Reader& Reader)
{
    // .text:0632e7b0
    if (!Reader.is_value("msg_id_prefix"))
        return false;

    MsgIdPrefix = Reader.get_value_string();
    return true;
}

TString<64> const& CommReferrable::GetMsgIdPrefix() const
{
    // .text:0632e7a0
    return MsgIdPrefix;
}

bool CommReferrable::HasVoiceInfo() const
{
    // .text:0629d860
    return MsgIdPrefix.Length != 0;
}
