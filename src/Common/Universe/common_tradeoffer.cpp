// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CPlayerTradeOffer -- one player's side of a trade in progress (retail 0x0631d620 onwards plus
// ICF-shared accessors). Declared in common_tradeoffer.h.
#include "common_tradeoffer.h"

CPlayerTradeOffer::CPlayerTradeOffer()
    : SourcePlayerId(0)
    , TargetPlayerId(0)
    , TargetOffer(nullptr)
    , MoneyOffered(0)
    , Accepted(false)
{
    // 0x0631d8a0
}

CPlayerTradeOffer::CPlayerTradeOffer(CPlayerTradeOffer const& Other)
    : SourcePlayerId(Other.SourcePlayerId)
    , TargetPlayerId(Other.TargetPlayerId)
    , TargetOffer(Other.TargetOffer)
    , List(Other.List)
    , MoneyOffered(Other.MoneyOffered)
    , Accepted(Other.Accepted)
{
    // 0x0631d630
}

CPlayerTradeOffer& CPlayerTradeOffer::operator=(CPlayerTradeOffer const& Other)
{
    // 0x0631d720. No self test beyond the list's own.
    SourcePlayerId = Other.SourcePlayerId;
    TargetPlayerId = Other.TargetPlayerId;
    TargetOffer = Other.TargetOffer;
    List = Other.List;
    MoneyOffered = Other.MoneyOffered;
    Accepted = Other.Accepted;
    return *this;
}

CPlayerTradeOffer::~CPlayerTradeOffer()
{
    // 0x0631d8e0
}

unsigned long CPlayerTradeOffer::GetMoneyOffered()
{
    return MoneyOffered;
}

void CPlayerTradeOffer::SetMoneyOffered(unsigned long Money)
{
    MoneyOffered = Money;
}

bool CPlayerTradeOffer::GetAccepted()
{
    return Accepted;
}

void CPlayerTradeOffer::SetAccepted(bool bAccepted)
{
    Accepted = bAccepted;
}

void CPlayerTradeOffer::SetSourcePlayerID(unsigned int Id)
{
    SourcePlayerId = Id;
}

// +0x8
unsigned int CPlayerTradeOffer::GetTargetPlayerID()
{
    return TargetPlayerId;
}

void CPlayerTradeOffer::SetTargetPlayerID(unsigned int Id)
{
    TargetPlayerId = Id;
}

void CPlayerTradeOffer::SetTargetOffer(CPlayerTradeOffer* Offer)
{
    TargetOffer = Offer;
}

// 0x06261dc0 (+0x10)
EquipDescList& CPlayerTradeOffer::GetList()
{
    return List;
}

// 0x062d1ad0 (+0x4)
unsigned int CPlayerTradeOffer::GetSourcePlayerID()
{
    return SourcePlayerId;
}

// 0x0629fb90 (+0xc)
CPlayerTradeOffer* CPlayerTradeOffer::GetTargetOffer()
{
    return TargetOffer;
}
