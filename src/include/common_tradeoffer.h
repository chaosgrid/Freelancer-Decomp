// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_goods.h" // EquipDescList

// CPlayerTradeOffer -- retail sizeof 0x24, one per player while a trade is in progress.
// Server.dll allocates two per trade in IServerImpl::InitiateTrade and keeps them in
// PlayerData::trade_offer (+0x3CC). Layout from the copy ctor (0x0631d630), operator=
// (0x0631d720), the default ctor (0x0631d8a0) and the dtor (0x0631d8e0); the vftable's sole
// slot is the deleting destructor. +0x10 is a VC6 std::list<EquipDesc> in retail; the port's
// EquipDescList uses the current CRT's std::list, so the port's size differs. Harmless:
// all access goes through named members on both sides of the DLL boundary.
class COMMON_API CPlayerTradeOffer
{
public:
    CPlayerTradeOffer();
    CPlayerTradeOffer(CPlayerTradeOffer const& Other);
    CPlayerTradeOffer& operator=(CPlayerTradeOffer const& Other);
    virtual ~CPlayerTradeOffer();

    unsigned int GetTargetPlayerID();
    unsigned long GetMoneyOffered();

    // Common.dll 0xbd950 (ICF-folded setter).
    void SetMoneyOffered(unsigned long Money);

    // Common.dll 0xbd620.
    bool GetAccepted();

    // Common.dll 0xbd960. IServerImpl::AcceptTrade reads the flag for its mutual-accept check.
    void SetAccepted(bool bAccepted);

    // Common.dll 0xa9b30 (ICF-folded setter).
    void SetSourcePlayerID(unsigned int Id);

    // Common.dll 0xbd940 (ICF-folded setter).
    void SetTargetPlayerID(unsigned int Id);

    // Common.dll 0xd2f40 (ICF-folded setter).
    void SetTargetOffer(CPlayerTradeOffer* Offer);

    // 0x06261dc0: the offered items, embedded at +0x10.
    EquipDescList& GetList();
    // 0x062d1ad0.
    unsigned int GetSourcePlayerID();
    // 0x0629fb90.
    CPlayerTradeOffer* GetTargetOffer();

private:
    unsigned int SourcePlayerId;        // +0x04
    unsigned int TargetPlayerId;        // +0x08
    CPlayerTradeOffer* TargetOffer;     // +0x0c
    // +0x10. Its allocator byte is copied by the copy ctor and not by operator=, as VC6's
    // std::list does.
    EquipDescList List;                 // +0x10
    unsigned long MoneyOffered;         // +0x1c
    bool Accepted;                      // +0x20
};
