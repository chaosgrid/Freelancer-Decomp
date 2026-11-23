// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU35 (06f03fa0-06f056d0): loot properties and mission offers. See
// LootProps.h.

#include "Bases/LootProps.h"
#include "Bases/BaseDB.h"
#include "Bases/BaseSupport.h"        // FBaseVisit, FBaseVendorOffer
#include "Bases/Cargo.h"              // GetVibeFeelingMask (FUN_06f35140)
#include "Core/ContentPlayerState.h"
#include "Core/ContentStaticIds.h"   // TU35 static IDs 06f03fa0..06f04180
#include "Core/ContentDefaultId.h"   // TU35 06f041a0 (CreateID("default") -> DAT_06fd0590)
#include "RandomMissions/RandomMission.h"
#include "RandomMissions/RandomMissionDB.h"
#include "Universe/SystemInfo.h"         // FSystemInfo::SystemId
#include "common_goods.h"       // MissionVendorOfferDescription
#include "common_ini.h"
#include "common_server.h"      // pub::GF::MissionVendorOffer*, ReportWhyMissionVendorEmpty
#include "common_text.h"        // FmtStr
#include "common_weighted_vector.h" // weighted_vector<unsigned int> (vendor candidates)

#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

namespace
{
    // DAT_06fd05ec (static init 06f04c20, atexit 06f04c50).
    std::vector<FLootProps> GLootProps;

    // DAT_06fd05e8 (06f04c70) and DAT_06fd05e4 (06f04c90).
    unsigned int const GIdDefaultCommodity = CreateID("default_commodity");
    unsigned int const GIdDefaultEquipment = CreateID("default_equipment");

    // DAT_06fd05d4 (static init 06f05090, atexit 06f050c0).
    std::vector<FPhantomLoot> GPhantomLoots;

    // Shared body of FUN_06f05000 / FUN_06f05030 / FUN_06f05060: linear scan.
    FLootProps* FindLootPropsById(unsigned int Id)
    {
        for (int Index = 0; Index < static_cast<int>(GLootProps.size()); ++Index)
        {
            if (GLootProps[Index].Nickname.Id == Id)
                return &GLootProps[Index];
        }
        return nullptr;
    }
}

// FUN_06f04cb0 (06f04cb0-06f04ff8).
void __cdecl LoadLootProps(INI_Reader* Reader)
{
    while (Reader->read_header())
    {
        if (!Reader->is_header("mLootProps"))
            continue;

        FLootProps Record;
        Record.Nickname.clear();
        Record.DropChance = 0.0f;
        for (int Index = 0; Index < 5; ++Index)
            Record.DropProperties[Index] = 0;

        while (Reader->read_value())
        {
            if (Reader->is_value("nickname"))
            {
                Record.Nickname.Id = CreateID(Reader->get_value_string());
            }
            else if (Reader->is_value("drop_properties"))
            {
                // 0.01f = DAT_06fb8064.
                Record.DropChance = Reader->get_value_float(0) * 0.01f;
                Record.DropProperties[0] = Reader->get_value_int(1);
                Record.DropProperties[1] = Reader->get_value_int(2);
                Record.DropProperties[2] = Reader->get_value_int(3);
                Record.DropProperties[3] = Reader->get_value_int(4);
                Record.DropProperties[4] = Reader->get_value_int(5);
            }
        }

        GLootProps.push_back(Record);
    }
}

// FUN_06f050e0 (06f050e0-06f05436).
void __cdecl LoadPhantomLoot(INI_Reader* Reader)
{
    while (Reader->read_header())
    {
        if (!Reader->is_header("PhantomLoot"))
            continue;

        FPhantomLoot Record;
        Record.Nickname.clear();
        Record.ToughnessMin = 0;
        Record.ToughnessMax = 0;
        Record.PercentChance = 0.0f;
        Record.NumToDropMin = 0;
        Record.NumToDropMax = 0;

        while (Reader->read_value())
        {
            if (Reader->is_value("nickname"))
            {
                Record.Nickname.Id = CreateID(Reader->get_value_string());
            }
            else if (Reader->is_value("toughness_range"))
            {
                Record.ToughnessMin = Reader->get_value_int(0);
                Record.ToughnessMax = Reader->get_value_int(1);
                if (Record.ToughnessMax == -1)
                    Record.ToughnessMax = 0x7fffffff;
            }
            else if (Reader->is_value("percent_chance"))
            {
                Record.PercentChance = Reader->get_value_float(0) * 0.01f;
            }
            else if (Reader->is_value("num_to_drop"))
            {
                Record.NumToDropMin = Reader->get_value_int(0);
                Record.NumToDropMax = Reader->get_value_int(1);
            }
        }

        GPhantomLoots.push_back(Record);
    }
}

// FUN_06f05000 (06f05000-06f05022).
FLootProps* __cdecl FindDefaultCommodityLootProps()
{
    return FindLootPropsById(GIdDefaultCommodity);
}

// FUN_06f05030 (06f05030-06f05052).
FLootProps* __cdecl FindDefaultEquipmentLootProps()
{
    return FindLootPropsById(GIdDefaultEquipment);
}

// FUN_06f05060 (06f05060-06f05082).
FLootProps* __cdecl FindLootProps(unsigned int const* Nickname)
{
    return FindLootPropsById(*Nickname);
}

// FUN_06f05440 (06f05440-06f05621).
void __cdecl CollectPhantomLoot(int const* Toughness, std::vector<FPhantomLoot const*>* Out)
{
    for (std::vector<FPhantomLoot>::const_iterator It = GPhantomLoots.begin();
         It != GPhantomLoots.end(); ++It)
    {
        if (It->ToughnessMin > *Toughness || It->ToughnessMax < *Toughness)
            continue;
        Out->insert(Out->end(), 1, &*It);
    }
}

// FUN_06f041c0 (06f041c0-06f042c2).
bool __cdecl CreateBarMissionOffer(FBaseNpcMission const* Mission, FContentPlayerState* Player,
                                   unsigned int BaseId, unsigned int Group,
                                   FBarMissionOfferData* Out)
{
    Out->Type = Mission->Type.Data;
    bool const bLoaderCreated = CreateRandomMissionLoader(Mission->Type.Data, &Out->Loader);

    // Difficulty 0.0 = DAT_06fb8030.
    FRmMissionRequest Request;
    Request.PlayerId = Player->PlayerId;
    Request.OfferBase = BaseId;
    Request.OfferGroup = static_cast<int>(Group);
    Request.Difficulty = 0.0f;

    // 2 ("no offer") is retried up to five attempts in all; 0, 1 and 3 end the loop.
    int Result = 0;
    for (int Attempt = 0; Attempt < 5; ++Attempt)
    {
        Request.Difficulty = PickRmMissionDifficulty(Player, Mission->MinDifficulty,
                                                     Mission->MaxDifficulty);
        if (0.0f == Request.Difficulty)
            return false;
        Result = Out->Loader->CreateMission(&Request, &Out->Mission);
        if (Result == 0 || Result == 1 || Result == 3)
            break;
    }

    if (!bLoaderCreated)
        return false;
    return Result == 0;
}

namespace
{
    // {[BaseFaction] faction, its weight}: the 8-byte candidate of FUN_06f042d0 /
    // FUN_06f04450 -- an element of Common's weighted_vector<unsigned int> (the local of
    // FUN_06f047e0 unwinds through the ~weighted_vector import). Retail calls no other
    // weighted_vector member, not even Pick: every use is inline vector code on the pairs.
    typedef std::pair<unsigned int, float> FVendorFaction;

    // FUN_06f042d0 (06f042d0-06f04449, cdecl): one candidate per [BaseFaction] of Base that
    // has a mission_type, minus those the player's reputation is hostile to.
    void __cdecl CollectVendorFactions(FContentBase* Base, FContentPlayerState* State,
                                       weighted_vector<unsigned int>* Factions)
    {
        std::vector<FVendorFaction>& Items = Factions->Items;

        Items.reserve(Base->Factions.size());

        for (std::vector<FBaseFaction>::iterator It = Base->Factions.begin();
             It != Base->Factions.end(); ++It)
        {
            if (!It->MissionTypes.empty())
                Items.insert(Items.end(), 1, FVendorFaction(It->Group, It->Weight));
        }

        // Friendly (1) and neutral (2) keep the candidate; anything else is erased in place.
        std::vector<FVendorFaction>::iterator Faction = Items.begin();
        while (Faction != Items.end())
        {
            unsigned int const Mask = GetVibeFeelingMask(Faction->first, State->GetReputation());
            if (Mask == 1 || Mask == 2)
                ++Faction;
            else
                Faction = Items.erase(Faction);
        }
    }

    // FUN_06f04450 (06f04450-06f047d0, cdecl): up to Count random missions for Factions into
    // Out (the offers own their loader reference). Returns 0 when Out is not empty, else the
    // last failure: 2 no difficulty, 3 no objective zone (CreateMission 3), 4 any other
    // CreateMission failure; 0 also when nothing was tried.
    int __cdecl GenerateVendorMissions(std::vector<FBarMissionOfferData>* Out,
                                       FContentPlayerState* State, FContentBase* Base,
                                       weighted_vector<unsigned int> const* FactionList,
                                       int Count)
    {
        std::vector<FVendorFaction> const* Factions = &FactionList->Items;
        int Result = 0;

        // A negative Count reserves 0.
        Out->reserve(static_cast<size_t>(Count >= 0 ? Count : 0));

        if (!Factions->empty())
        {
            // 2 * Count attempts, at least 20 (unsigned compare).
            unsigned int MaxAttempts = 0x14;
            if (static_cast<unsigned int>(Count * 2) >= 0x14)
                MaxAttempts = static_cast<unsigned int>(Count * 2);
            unsigned int Attempt = 0;

            // FUN_06f4d5e0 loads the player's system for the missions.
            RegisterRmDestSystem(State->GetSystemRecord()->SystemId);

            while (Count != 0 && Attempt <= MaxAttempts)
            {
                // A faction by weight. The sum and the running total are x87 doubles; the
                // roll is (float)(rand() * 1/32768 * (float)sum); a zero sum skips the roll.
                // No roll, or none taken, leaves the pointer at end() and retail reads that
                // slot as a candidate -- the stale copy an erase left there, or the bytes
                // past the block; the port reads the same slot.
                FVendorFaction const* Picked = Factions->data() + Factions->size();
                double FactionSum = 0.0;
                for (std::vector<FVendorFaction>::const_iterator It = Factions->begin();
                     It != Factions->end(); ++It)
                    FactionSum += It->second;
                if (FactionSum != 0.0)
                {
                    float const SumValue = static_cast<float>(FactionSum);
                    float const Roll = static_cast<float>(
                        static_cast<double>(rand()) * (1.0f / 32768.0f) * SumValue);
                    double Running = 0.0;
                    for (size_t Index = 0; Index < Factions->size(); ++Index)
                    {
                        Running += (*Factions)[Index].second;
                        if (Roll <= Running)
                        {
                            Picked = Factions->data() + Index;
                            break;
                        }
                    }
                }

                // Then a mission_type by its weight the same way; none -> the attempt is
                // spent. FindFaction's result is not null-tested.
                FBaseFaction* Faction = Base->FindFaction(Picked->first);
                std::vector<FBaseMissionType>::iterator PickedType =
                    Faction->MissionTypes.end();
                double TypeSum = 0.0;
                for (std::vector<FBaseMissionType>::iterator It = Faction->MissionTypes.begin();
                     It != Faction->MissionTypes.end(); ++It)
                    TypeSum += It->Weight;
                if (TypeSum != 0.0)
                {
                    float const SumValue = static_cast<float>(TypeSum);
                    float const Roll = static_cast<float>(
                        static_cast<double>(rand()) * (1.0f / 32768.0f) * SumValue);
                    double Running = 0.0;
                    for (std::vector<FBaseMissionType>::iterator It =
                             Faction->MissionTypes.begin();
                         It != Faction->MissionTypes.end(); ++It)
                    {
                        Running += It->Weight;
                        if (Roll <= Running)
                        {
                            PickedType = It;
                            break;
                        }
                    }
                }

                if (PickedType != Faction->MissionTypes.end())
                {
                    FBarMissionOfferData Offer;
                    Offer.Type = PickedType->Type.Data;

                    // A failed loader spends the attempt.
                    if (CreateRandomMissionLoader(Offer.Type.Data, &Offer.Loader))
                    {
                        // Difficulty 0.0 = DAT_06fb8030.
                        FRmMissionRequest Request;
                        Request.PlayerId = State->PlayerId;
                        Request.OfferBase = Base->BaseId;
                        Request.OfferGroup = static_cast<int>(Picked->first);
                        Request.Difficulty = 0.0f;
                        Request.Difficulty = PickRmMissionDifficulty(
                            State, PickedType->MinDifficulty, PickedType->MaxDifficulty);

                        bool bKept = false;
                        if (0.0f == Request.Difficulty)
                        {
                            if (Result == 0)
                                Result = 2;
                        }
                        else
                        {
                            // Loader slot 3.
                            int const Code = Offer.Loader->CreateMission(&Request, &Offer.Mission);
                            bool const bCreated = Code == 0;
                            if (Code == 3)
                                Result = Code;
                            else if (Code != 0)
                                Result = 4;
                            if (bCreated)
                            {
                                // The loader reference moves into Out.
                                Out->insert(Out->end(), 1, Offer);
                                --Count;
                                bKept = true;
                            }
                        }

                        if (!bKept)
                        {
                            Offer.Loader->ReleaseMission(Offer.Mission);
                            Offer.Loader->Release();
                        }
                    }
                }

                ++Attempt;
            }

            ReleaseRmDestSystems();                                         // FUN_06f4d650
        }

        if (!Out->empty())
            return 0;
        return Result;
    }

    // The reset of one FmtStr inlined three times in FUN_06f04ab0: FmtStr::destroy, then all
    // 0x18 bytes zeroed.
    void ResetVendorFmtStr(FmtStr& Str)
    {
        Str.destroy();
        Str.Head = nullptr;
        Str.MadLibId = 0;
        memset(Str.ValueIndex, 0, sizeof(Str.ValueIndex));
    }

    // FUN_06f04ab0 (06f04ab0-06f04b03, thiscall; runs after the three FmtStr default ctors).
    void InitMissionVendorOfferDescription(MissionVendorOfferDescription* Desc)
    {
        Desc->BaseId = 0;
        Desc->TargetId = 0;
        Desc->SystemId = 0;
        Desc->MissionClass = 0;
        ResetVendorFmtStr(Desc->Title);
        ResetVendorFmtStr(Desc->Description);
        ResetVendorFmtStr(Desc->Briefing);
        Desc->IconTextureId = 0;
        Desc->Filter = 0;
    }
}

// FUN_06f047e0 (06f047e0-06f04a21).
void __cdecl BuildMissionVendorOffers(FContentBase* Base, FBaseVisit* Visit,
                                      FContentPlayerState* State)
{
    // No candidate faction -> reason 1 and nothing else.
    weighted_vector<unsigned int> Factions;
    CollectVendorFactions(Base, State, &Factions);
    if (Factions.Items.empty())
    {
        pub::GF::ReportWhyMissionVendorEmpty(State->PlayerId, static_cast<MVEmptyReason>(1));
        return;
    }

    // The offer count divides signed.
    std::vector<FBarMissionOfferData> Offers;
    int const Range = Base->VendorOffersMax - Base->VendorOffersMin + 1;
    int const Count = rand() * Range / 32768 + Base->VendorOffersMin;
    int const Reason = GenerateVendorMissions(&Offers, State, Base, &Factions, Count);

    // The reason is reported whatever it is.
    Factions.Items.clear();
    pub::GF::ReportWhyMissionVendorEmpty(State->PlayerId, static_cast<MVEmptyReason>(Reason));

    for (std::vector<FBarMissionOfferData>::iterator Offer = Offers.begin();
         Offer != Offers.end(); ++Offer)
    {
        // Dtor ~MissionVendorOfferDescription = FUN_06f04b10 (common_goods.h).
        MissionVendorOfferDescription Desc;
        InitMissionVendorOfferDescription(&Desc);

        // Loader slot 6 fills the description from the mission.
        Offer->Loader->GetOfferDescription(Offer->Mission, &Desc);
        // +0x08 = 2 (the kind BaseSupport msg 0x2c reads back as 2 = vendor offer), +0x04 =
        // the player's next offer id (FUN_06f48c50). The common_goods.h names of both fields
        // (SystemId, TargetId) are the Server port's.
        Desc.SystemId = 2;
        Desc.TargetId = State->AllocateVendorOfferId();

        // The loader reference moves into the record; nothing is AddRef'd.
        FBaseVendorOffer Record;
        Record.VendorHandle = pub::GF::MissionVendorOfferCreate(Desc);
        Record.OfferId = Desc.TargetId;
        Record.Data = *Offer;

        Visit->VendorOffers.push_back(Record);                              // FUN_06f04b90
    }

    // Retail frees both blocks without element dtors.
}

// FUN_06f04a40 (06f04a40-06f04aad). Neither loop tests the loader for null.
void __cdecl ClearMissionVendorOffers(FBaseVisit* Visit)
{
    for (std::list<FBaseVendorOffer>::iterator It = Visit->VendorOffers.begin();
         It != Visit->VendorOffers.end(); ++It)
    {
        pub::GF::MissionVendorOfferDestroy(&It->VendorHandle);
        It->Data.Loader->ReleaseMission(It->Data.Mission);                         // +0x14
        It->Data.Loader->Release();                                        // +0x04
    }
    Visit->VendorOffers.clear();
}
