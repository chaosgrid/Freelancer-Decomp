// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll bases and markets (retail BaseData.cpp): MarketGoodInfo, BaseData, BaseDataList and
// the BaseDataList_* entry points.

#include "common_goods.h"
#include "Archetypes/common_goods_internal.h" // kPathChars
#include "common_id.h"
#include "common_ini.h"
#include "common_misc.h" // GetFileSysPath
#include "common_universe.h"
#include "DALib.h"
#include "dacom.h"

#include <cstdio>
#include <list>
#include <map>
#include <new>
#include <string>

using CmnGoods::kPathChars;

namespace
{
    const char kBaseDataSource[] = "E:\\FL\\Scratch\\Source\\Common\\BaseData.cpp";
}

// ---------------------------------------------------------------------------
// Bases / markets
// ---------------------------------------------------------------------------

// Inlined into both callers (load_market_data, set_market_good). The 1.0f Price is the price
// multiplier's default leaking into the field it feeds; not a plausible price, but what retail
// stores.
MarketGoodInfo::MarketGoodInfo()
    : GoodId(0)
    , Price(1.0f)
    , MinInventory(0)
    , MaxInventory(0)
    , Type(TransactionBuyAndSell)
    , MinLevel(0.0f)
    , MinRep(-1.0f)
{
}

// 06264980. The copy ctor (06261de0) and operator= (06261f20) are defaulted in the header.
BaseData::BaseData()
    : BaseId(0)
    , StartRoomId(0)
    , PriceVariance(0.0f)
    , ShipRepairCost(0.33f)
{
}

// 06264a40
BaseData::~BaseData()
{
    for (std::list<RoomData*>::iterator It = Rooms.begin(); It != Rooms.end(); ++It)
        delete *It;
    Rooms.clear();
}

unsigned BaseData::get_base_id() const
{
    return BaseId;
}

float BaseData::get_price_variance() const
{
    return PriceVariance;
}

float BaseData::get_ship_repair_cost() const
{
    return ShipRepairCost;
}

unsigned BaseData::get_start_location() const
{
    return StartRoomId;
}

std::map<unsigned, MarketGoodInfo> const* BaseData::get_market() const
{
    // .text:06261dc0 (+0x10)
    return &Market;
}

std::list<RoomData*>* BaseData::get_room_data_list()
{
    // .text:06261dd0 (+0x24)
    return &Rooms;
}

std::list<RoomData*> const* BaseData::get_const_room_data_list() const
{
    // .text:06261dd0 (the same body as get_room_data_list).
    return &Rooms;
}

// 0x06264b60 (ordinal 3623). An all-default line (constants .rdata:06399410 / 06399408 /
// 06399404) is a no-op: it does NOT erase an existing entry. Otherwise the entry is
// overwritten whether or not the key was already there.
void BaseData::set_market_good(unsigned GoodId, int MinInventory, int MaxInventory,
                               TransactionType Type, float PriceMult, float MinLevel,
                               float MinRep)
{
    if (MinInventory == 0 && MaxInventory == 0 && Type == TransactionBuyAndSell &&
        PriceMult == 1.0f && MinLevel == 0.0f && MinRep == -1.0f)
    {
        return;
    }

    MarketGoodInfo Entry;
    Entry.GoodId = GoodId;
    // No null test of the good. The product is exact in a double, so it truncates as the x87
    // one does.
    Entry.Price = (float)(int)((double)GoodList::find_by_id(GoodId)->Price * PriceMult);
    Entry.MinInventory = MinInventory;
    Entry.MaxInventory = MaxInventory;
    Entry.Type = Type;
    Entry.MinLevel = MinLevel;
    Entry.MinRep = MinRep;

    Market[GoodId] = Entry;
}

// 0x06264d80 -- [Base]/[BaseInfo] block.
void BaseData::read_Base_block(INI_Reader* Reader)
{
    while (Reader->read_value())
    {
        if (Reader->is_value("nickname"))
            continue;
        if (Reader->is_value("start_room"))
        {
            char Combined[0x100];
            sprintf(Combined, "%X_%s", BaseId, Reader->get_value_string());
            StartRoomId = CreateID(Combined);
        }
        else if (Reader->is_value("price_variance"))
        {
            PriceVariance = Reader->get_value_float(0);
        }
        else if (Reader->is_value("ship_repair_cost"))
        {
            ShipRepairCost = Reader->get_value_float(0);
        }
        else
        {
            FDUMP(0x100001, "%s(%d) : *** ERROR: Invalid ini data in file %s\n",
                  kBaseDataSource, 0x89, Reader->get_file_name());
        }
    }
    if (StartRoomId == 0)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: No start room in base ini file %s\n",
              kBaseDataSource, 0x90, Reader->get_file_name());
    }
}

// 0x06264ea0 -- [Room] block. The RoomData is new-ed and appended BEFORE the keys are read; when
// the block lacks a nickname or a file the never-initialised RoomData stays in the list, its
// BaseId / RoomId / bLoaded whatever the heap held (RoomData's ctor does not write them), as in
// retail.
void BaseData::read_Room_block(INI_Reader* Reader)
{
    // VC6 new: a (possibly null) pointer is appended either way.
    RoomData* Room = new (std::nothrow) RoomData();
    Rooms.push_back(Room);

    std::string Nickname;
    std::string File;
    while (Reader->read_value())
    {
        if (Reader->is_value("nickname"))
            Nickname = Reader->get_value_string();
        else if (Reader->is_value("file"))
            File = Reader->get_value_string();
        else
        {
            FDUMP(0x100001, "%s(%d) : *** ERROR: Invalid ini data in file %s\n",
                  kBaseDataSource, 0xb5, Reader->get_file_name());
        }
    }
    if (Nickname.length() != 0 && File.length() != 0)
    {
        // init malloc_strcpy's both strings; RoomData::destroy frees them.
        char Combined[0x100];
        sprintf(Combined, "%X_%s", BaseId, Nickname.c_str());
        Room->init(BaseId, CreateID(Combined), Nickname.c_str(), File.c_str());
    }
    else
    {
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: Invalid [Room] entry in ini file %s, nickname or file is missing\n",
              kBaseDataSource, 0xc1, Reader->get_file_name());
    }
}

// 0x06265100. BaseId is stored before the open.
void BaseData::read_from_ini(char const* RelPath, unsigned NewBaseId)
{
    BaseId = NewBaseId;

    char FullPath[kPathChars];
    GetFileSysPath(FullPath, RelPath, DALib::Data);

    INI_Reader Reader;
    if (Reader.open(FullPath, false))
    {
        while (Reader.read_header())
        {
            if (Reader.is_header("Base") || Reader.is_header("BaseInfo"))
            {
                read_Base_block(&Reader);
            }
            else if (Reader.is_header("Room"))
            {
                read_Room_block(&Reader);
            }
            else
            {
                FDUMP(0x100001, "%s(%d) : *** ERROR: Invalid ini block in file %s\n",
                      kBaseDataSource, 0xe2, Reader.get_file_name());
            }
        }
        Reader.close();
    }
    else
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: Cannot open base .ini file %s\n",
              kBaseDataSource, 0xea, FullPath);
    }
}

static BaseDataList* GBaseDataList = nullptr;

// 06265450. Copy ctor / operator= are defaulted.
BaseDataList::BaseDataList()
{
}

// 06265560
BaseDataList::~BaseDataList()
{
    for (std::list<BaseData*>::iterator It = Bases.begin(); It != Bases.end(); ++It)
        delete *It;
}

// 06265f80 (map at +0x0c)
BaseData* BaseDataList::get_base_data(unsigned BaseId) const
{
    std::map<unsigned, BaseData*>::const_iterator Found = ById.find(BaseId);
    if (Found != ById.end())
        return Found->second;
    FDUMP(0x100001, "%s(%d) : *** ERROR: Could not find base %u\n", kBaseDataSource, 0x1e8,
          BaseId);
    return nullptr;
}

std::list<BaseData*>* BaseDataList::get_base_data_list()
{
    // .text:06261260 (the list is the first member)
    return &Bases;
}

std::list<BaseData*> const* BaseDataList::get_const_base_data_list() const
{
    // .text:06261260 (the same body as get_base_data_list).
    return &Bases;
}

// 06266080: the same body as get_unloaded_room_data (map at +0x20). It does not load the room.
RoomData* BaseDataList::get_room_data(unsigned RoomId) const
{
    std::map<unsigned, RoomData*>::const_iterator Found = RoomById.find(RoomId);
    if (Found != RoomById.end())
        return Found->second;
    FDUMP(0x100001, "%s(%d) : *** ERROR: Could not find room %u\n", kBaseDataSource, 0x1fc,
          RoomId);
    return nullptr;
}

// 06266000
RoomData* BaseDataList::get_unloaded_room_data(unsigned RoomId) const
{
    std::map<unsigned, RoomData*>::const_iterator Found = RoomById.find(RoomId);
    if (Found != RoomById.end())
        return Found->second;
    FDUMP(0x100001, "%s(%d) : *** ERROR: Could not find room %u\n", kBaseDataSource, 0x1fc,
          RoomId);
    return nullptr;
}

// 06265780. A base without an ini keeps BaseId 0 (read_from_ini is what stores it). The two
// maps are filled only after the whole walk, by insert, so the first of two equal keys wins
// (FUN_063222f0).
void BaseDataList::load()
{
    Universe::IBase* Base = Universe::GetFirstBase();
    while (Base != nullptr)
    {
        BaseData* Data = new BaseData();
        Bases.push_back(Data);

        char const* File = Base->GetFile();
        if (File != nullptr && File[0] != '\0')
        {
            Data->read_from_ini(File, Base->GetId());
        }
        else
        {
            FDUMP(0x100001, "%s(%d) : *** ERROR: No ini file for base %u\n", kBaseDataSource,
                  0x11b, Base->GetId());
        }

        Base = Universe::GetNextBase();
    }

    for (std::list<BaseData*>::iterator BaseIt = Bases.begin(); BaseIt != Bases.end(); ++BaseIt)
    {
        BaseData* Data = *BaseIt;
        ById.insert(std::pair<unsigned const, BaseData*>(Data->BaseId, Data));
        for (std::list<RoomData*>::iterator RoomIt = Data->Rooms.begin();
             RoomIt != Data->Rooms.end(); ++RoomIt)
        {
            RoomById.insert(std::pair<unsigned const, RoomData*>((*RoomIt)->RoomId, *RoomIt));
        }
    }
}

// 06265900. An unknown "base" name only warns and leaves the current base as it was.
void BaseDataList::load_market_data(char const* RelPath)
{
    char FullPath[kPathChars];
    GetFileSysPath(FullPath, RelPath, DALib::Data);

    INI_Reader Reader;
    if (!Reader.open(FullPath, false))
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: Cannot open market .ini file %s\n",
              kBaseDataSource, 0x1d2, FullPath);
        return;
    }

    while (Reader.read_header())
    {
        if (!Reader.is_header("BaseGood"))
        {
            FDUMP(0x100002,
                  "%s(%d) : *** WARNING: Unknown ini block [%s] in market .ini file %s - ignoring\n",
                  kBaseDataSource, 0x1ca, Reader.get_header_ptr(), FullPath);
            continue;
        }

        BaseData* Current = nullptr;
        while (Reader.read_value())
        {
            if (Reader.is_value("base"))
            {
                char const* Nickname = Reader.get_value_string();
                unsigned BaseId = Universe::get_base_id(Nickname);
                if (BaseId == 0)
                {
                    FDUMP(0x100002,
                          "%s(%d) : *** WARNING: base name %s is unknown in market data file %s",
                          kBaseDataSource, 0x153, Nickname, FullPath);
                }
                else
                {
                    Current = get_base_data(BaseId);
                }
            }
            else if (Reader.is_value("MarketGood"))
            {
                unsigned GoodId = 0;
                GoodInfo const* Info = GoodList::find_by_nickname(Reader.get_value_string(0));
                if (Info != nullptr)
                    GoodId = Info->Id;

                // Retail assembles the record only once it has decided to keep the line.
                float ValueLevel = 0.0f;        // index 1  -> record+0x14
                float ValueRep = -1.0f;         // index 2  -> record+0x18
                int ValueMinInventory = 0;      // index 3  -> record+0x08
                int ValueMaxInventory = 0;         // index 4  -> record+0x0c
                int ValueType = 0;              // index 5  -> record+0x10
                float ValuePriceMult = 1.0f;    // index 6  -> feeds record+0x04

                if (!Reader.is_value_empty(1))
                    ValueLevel = Reader.get_value_float(1);
                if (!Reader.is_value_empty(2))
                    ValueRep = Reader.get_value_float(2);
                if (!Reader.is_value_empty(3))
                    ValueMinInventory = Reader.get_value_int(3);
                if (!Reader.is_value_empty(4))
                    ValueMaxInventory = Reader.get_value_int(4);
                if (!Reader.is_value_empty(5))
                    ValueType = Reader.get_value_int(5);
                if (!Reader.is_value_empty(6))
                    ValuePriceMult = Reader.get_value_float(6);
                // Index 7 is read and discarded.
                if (!Reader.is_value_empty(7))
                    Reader.get_value_float(7);

                bool bKeep = false;
                if (GoodId != 0 && Current != nullptr)
                {
                    if (ValueMinInventory != 0 || ValueMaxInventory != 0 || ValueType != 0 ||
                        ValuePriceMult != 1.0f || ValueLevel != 0.0f || ValueRep != -1.0f)
                    {
                        bKeep = true;
                    }
                }

                if (bKeep)
                {
                    MarketGoodInfo Market;
                    Market.GoodId = GoodId;
                    // No null test of the good; the product of two floats is exact in a
                    // double, so it truncates as the x87 one does.
                    double const Product =
                        (double)GoodList::find_by_id(GoodId)->Price * ValuePriceMult;
                    Market.Price = (float)(int)Product;
                    Market.MinInventory = ValueMinInventory;
                    Market.MaxInventory = ValueMaxInventory;
                    Market.Type = (TransactionType)ValueType;
                    Market.MinLevel = ValueLevel;
                    Market.MinRep = ValueRep;

                    // Assign-over-existing, not insert-if-absent.
                    Current->Market[GoodId] = Market;
                }
            }
            else
            {
                FDUMP(0x100002,
                      "%s(%d) : *** WARNING: Unknown ini data: [%s]  %s = %s in market .ini file - ignoring\n",
                      kBaseDataSource, 0x1c2, Reader.get_header_ptr(), Reader.get_name_ptr(),
                      Reader.get_value_string());
            }
        }
    }
    Reader.close();
}

// 06266110. The previous list is not destroyed -- it is simply overwritten, as in retail.
void __cdecl BaseDataList_load()
{
    GBaseDataList = new BaseDataList();
    GBaseDataList->load();
}

// 06266270: no null test of the global.
void __cdecl BaseDataList_load_market_data(char const* RelPath)
{
    GBaseDataList->load_market_data(RelPath);
}

void __cdecl BaseDataList_destroy()
{
    delete GBaseDataList;
    GBaseDataList = nullptr;
}

BaseDataList* __cdecl BaseDataList_get()
{
    return GBaseDataList;
}
