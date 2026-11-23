// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "NeuroNet/Economy/client_base_market.h"

#include "Network/login_mgr.h"    // G_Server (DAT_0067ecd0)
#include "Space/player_spawn.h" // GetPlayerConnectionId (DAT_00673344)

#include "common_goods.h"
#include "common_server.h" // IServer::GFGoodBuy / GFGoodSell, SGFGoodBuyInfo / SGFGoodSellInfo

#include <stdlib.h>

void ClientMarketFill(FClientMarket* Market, float PriceVariance, BaseData const* Base)
{
    // FUN_00437de0 (thiscall, RET 8).
    GoodInfoList const* const Goods = GoodList_get();
    std::list<GoodInfo*> const* const List = Goods->get_list();
    if (List->empty())
        return;

    // Both bounds of the scale are rounded to float.
    float const Low = static_cast<float>(1.0 - static_cast<double>(PriceVariance));
    float const Range = static_cast<float>(
        (static_cast<double>(PriceVariance) + 1.0) - static_cast<double>(Low));

    for (std::list<GoodInfo*>::const_iterator It = List->begin(); It != List->end(); ++It)
    {
        GoodInfo const* const Good = *It; // node +0x08

        FClientMarketGood Entry;
        Entry.Stock = 0;
        Entry.PriceScale = 0.0f;
        Entry.CanBuyFlags = 0;
        unsigned const Key = Good->Id; // +0x48

        std::map<unsigned, MarketGoodInfo> const* const Source =
            Base != nullptr ? &Base->Market : &GetClientCurrentLocationBaseData()->Market;

        std::map<unsigned, MarketGoodInfo>::const_iterator Found = Source->find(Key);
        if (Found != Source->end())
        {
            Entry.Info = Found->second;
        }
        else
        {
            Entry.Info.GoodId = Good->Id;   // +0x48
            Entry.Info.Price = Good->Price; // +0x58
            Entry.Info.MinInventory = 0;
            Entry.Info.MaxInventory = 0;
            Entry.Info.Type = TransactionBuyAndSell;
        }

        if (Entry.Info.MinInventory != 0 && Entry.Info.MaxInventory > 0)
            Entry.Stock = 0x7f000000;

        // 2^-15 is DAT_005caeec; the sum is kept in double until the final store.
        int const Roll = rand();
        Entry.PriceScale = static_cast<float>(static_cast<double>(Roll) * (1.0 / 32768.0) *
                                                  static_cast<double>(Range) +
                                              static_cast<double>(Low));

        (*Market)[Key] = Entry; // FUN_00438310
    }
}

int ClientMarketCollectCategory(FClientMarket const* Market,
                                std::list<FClientMarketGood const*>* Out, unsigned Category)
{
    // FUN_00437f70 (thiscall, RET 8): the map in key order.
    int Count = 0;
    for (FClientMarket::const_iterator It = Market->begin(); It != Market->end(); ++It)
    {
        FClientMarketGood const& Entry = It->second; // node +0x10
        if (Entry.Stock <= 0)                        // signed
            continue;
        GoodInfo const* const Good = GoodList::find_by_id(Entry.Info.GoodId);
        if (Good == nullptr)
            continue;
        if (Good->Category != Category) // +0x4c
            continue;
        Out->push_back(&Entry);
        ++Count;
    }
    return Count;
}

float ClientMarketGetPrice(FClientMarket const* Market, unsigned GoodId)
{
    // FUN_00438060 (thiscall, RET 4).
    FClientMarket::const_iterator It = Market->find(GoodId); // FUN_0052c7a0 + key test
    if (It == Market->end())
        return 0.0f; // DAT_005c7474

    // The product is truncated (ftol) before the clamps.
    FClientMarketGood const& Entry = It->second;
    int const Truncated =
        static_cast<int>(static_cast<double>(Entry.Info.Price) * static_cast<double>(Entry.PriceScale));
    float Price = static_cast<float>(Truncated);

    if (static_cast<double>(Truncated) > 9999999.0) // DAT_005caee8
        Price = 9999999.0f;
    // Below 0 becomes 1.0 (DAT_005c75dc).
    if (Price < 0.0f)
        Price = 1.0f;
    return Price;
}

int ClientMarketGetTransactionType(FClientMarket const* Market, unsigned GoodId)
{
    // FUN_00438100 (thiscall, RET 4).
    FClientMarket::const_iterator It = Market->find(GoodId);
    if (It == Market->end())
        return -1;
    return static_cast<int>(It->second.Info.Type); // +0x10
}

float ClientMarketGetMinLevel(FClientMarket const* Market, unsigned GoodId)
{
    // FUN_00438160 (thiscall, RET 4).
    FClientMarket::const_iterator It = Market->find(GoodId);
    if (It == Market->end())
        return 0.0f; // DAT_005c7474
    return It->second.Info.MinLevel; // +0x14
}

float ClientMarketGetMinRep(FClientMarket const* Market, unsigned GoodId)
{
    // FUN_004381c0 (thiscall, RET 4).
    FClientMarket::const_iterator It = Market->find(GoodId);
    if (It == Market->end())
        return -1.0f; // DAT_005c7500
    return It->second.Info.MinRep; // +0x18
}

FClientMarketGood* ClientMarketFindEntry(FClientMarket* Market, unsigned GoodId)
{
    // FUN_00437d90 (thiscall, RET 4).
    FClientMarket::iterator It = Market->find(GoodId);
    if (It == Market->end())
        return nullptr;
    return &It->second;
}

void ClientMarketRequestBuy(FClientMarket const* Market, unsigned BaseId, unsigned GoodId, int Count)
{
    // FUN_00438220 (thiscall, RET 0xc). Retail does not test G_Server for null.
    (void)Market;
    SGFGoodBuyInfo Info;
    Info.BaseId = BaseId;
    Info.Null04 = 0;
    Info.GoodId = GoodId;
    Info.Count = static_cast<unsigned>(Count);
    G_Server->GFGoodBuy(Info, GetPlayerConnectionId());
}

void ClientMarketRequestSell(FClientMarket const* Market, unsigned BaseId, unsigned GoodId, int Count)
{
    // FUN_00438270 (thiscall, RET 0xc).
    (void)Market;
    SGFGoodSellInfo Info;
    Info.BaseId = BaseId;
    Info.GoodId = GoodId;
    Info.Count = static_cast<unsigned>(Count);
    G_Server->GFGoodSell(Info, GetPlayerConnectionId());
}
