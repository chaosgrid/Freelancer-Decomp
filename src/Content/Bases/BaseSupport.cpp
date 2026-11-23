// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU19 (06eb3ec0-06eb6c00): the BaseSupport controller.
// CreateInstance("BaseSupport") -> new 8 -> FUN_06eb4ad0; vtable 06fb4618
// (slot 0 LAB_06eb4ba0 HandleMessage, slot 1 FUN_06eb4b30 deleting dtor).

// 06eb3ec0-06eb40a0: TU19's own 16 static-ID initializers (DAT_06fcef70..DAT_06fcef34),
// before GNewsItems (LAB_06eb4440, _initterm 06fcd548) and GBaseVisits (LAB_06eb54a0,
// 06fcd54c).
#include "Core/ContentStaticIds.h"

#include "Bases/BaseSupport.h"

#include "Core/ContentInternal.h"
#include "Core/ContentPlayerState.h"
#include "Core/ContentStageNames.h"
#include "Bases/GenericScripts.h"     // StartBarCharacterScript (FUN_06f00b60)
#include "Bases/LootProps.h"          // BuildMissionVendorOffers / ClearMissionVendorOffers (TU35)
#include "Encounters/Populators.h"         // BasePopHasPlayer (FUN_06eb3390)
#include "RandomMissions/RandomMission.h"      // FRandomMissionLoader::CanAccept / Release
#include "common_ini.h"
#include "common_server.h"
#include "common_server_controller.h"
#include "common_text.h"        // FmtStr
#include "common_tstring.h"

#include <algorithm>
#include <cstring>
#include <intrin.h>             // __debugbreak -- the two retail INT3s of msg 2
#include <new>

// ---------------------------------------------------------------------------------------
// News (missions\news.ini)
// ---------------------------------------------------------------------------------------

std::vector<FNewsItem> GNewsItems;

namespace
{
    // FUN_06eb5f50 (06eb5f50-06eb5fa6): FmtStr::destroy then zero all 0x18 bytes.
    void ResetNewsFmtStr(FmtStr& Str)
    {
        Str.destroy();
        Str.MadLibId = 0;
        memset(Str.ValueIndex, 0, sizeof(Str.ValueIndex));
    }

    void ResetNewsDescription(NewsBroadcastDescription& Desc)
    {
        Desc.BaseId = 0;
        Desc.bFlag1 = false;
        Desc.bFlag2 = false;
        Desc.Category = 0;
        ResetNewsFmtStr(Desc.Headline);
        ResetNewsFmtStr(Desc.Text);
        ResetNewsFmtStr(Desc.Audio);
        Desc.LogoTexture = nullptr;
        Desc.AudioName = nullptr;
        Desc.Filter = 0;
    }

    // The 43-entry stage table walk inlined twice in FUN_06eb40c0:
    // null names are skipped, the first stricmp match wins, a miss leaves Value alone.
    void LookupNewsStage(char const* Name, int& Value)
    {
        for (int Index = 0; Index < 43; ++Index)
        {
            if (GContentStageNames[Index].Name != nullptr &&
                _stricmp(Name, GContentStageNames[Index].Name) == 0)
            {
                Value = GContentStageNames[Index].Value;
                return;
            }
        }
    }

    // FUN_06eb40c0 (06eb40c0-06eb4430): the values of one [NewsItem] section.
    void __cdecl LoadNewsItemValues(INI_Reader* Reader, FNewsItem* Item)
    {
        // One stage slot shared by both "rank" lookups and kept across values. Retail never initialises it; it is read uninitialised only when the
        // first rank name is unknown (every rank in news.ini is a table name).
        int StageValue = 0;

        while (Reader->read_value())
        {
            if (Reader->is_value("base"))
            {
                unsigned int BaseId;
                pub::GetBaseID(BaseId, Reader->get_value_string());
                if (std::find(Item->Bases.begin(), Item->Bases.end(), BaseId) == Item->Bases.end())
                    Item->Bases.insert(Item->Bases.end(), BaseId);
            }
            else if (Reader->is_value("rank"))
            {
                LookupNewsStage(Reader->get_value_string(0), StageValue);
                Item->FromStage = StageValue;
                LookupNewsStage(Reader->get_value_string(1), StageValue);
                Item->ToStage = StageValue;
            }
            else if (Reader->is_value("breaking"))
            {
                Item->Desc.bFlag1 = true;
            }
            else if (Reader->is_value("autoselect"))
            {
                Item->Desc.bFlag2 = true;
            }
            else if (Reader->is_value("icon"))
            {
                // Each compare re-reads the value; no match leaves Category alone.
                if (_stricmp(Reader->get_value_string(), "critical") == 0)
                    Item->Desc.Category = 1;
                else if (_stricmp(Reader->get_value_string(), "world") == 0)
                    Item->Desc.Category = 2;
                else if (_stricmp(Reader->get_value_string(), "mission") == 0)
                    Item->Desc.Category = 3;
                else if (_stricmp(Reader->get_value_string(), "system") == 0)
                    Item->Desc.Category = 4;
                else if (_stricmp(Reader->get_value_string(), "faction") == 0)
                    Item->Desc.Category = 5;
                else if (_stricmp(Reader->get_value_string(), "universe") == 0)
                    Item->Desc.Category = 6;
            }
            else if (Reader->is_value("category"))
            {
                Item->Desc.Headline.begin_mad_lib(Reader->get_value_int(0));
                Item->Desc.Headline.end_mad_lib();
            }
            else if (Reader->is_value("headline"))
            {
                Item->Desc.Text.begin_mad_lib(Reader->get_value_int(0));
                Item->Desc.Text.end_mad_lib();
            }
            else if (Reader->is_value("text"))
            {
                FmtStr& Text = Item->Desc.Audio;
                Text.begin_mad_lib(Reader->get_value_int(0));
                for (unsigned int Index = 1; !Reader->is_value_empty(Index); ++Index)
                    Text.append_string(Reader->get_value_int(Index));
                Text.end_mad_lib();
            }
            else if (Reader->is_value("logo"))
            {
                char* Logo = StringAlloc(Reader->get_value_string(), false);
                Item->Logo.Value = Logo;
                Item->Desc.LogoTexture = Logo;
            }
            else if (Reader->is_value("audio"))
            {
                char* Audio = StringAlloc(Reader->get_value_string(), false);
                Item->Audio.Value = Audio;
                Item->Desc.AudioName = Audio;
            }
        }
    }
}

// Inlined in the loader FUN_06eb44c0.
FNewsItem::FNewsItem()
    : FromStage(0), ToStage(0x2a)
{
    Audio.clear();
    Logo.clear();
    ResetNewsDescription(Desc);
}

// FUN_06eb4680 (06eb4680-06eb471f). The StringAlloc'd strings are not freed.
FNewsItem::~FNewsItem()
{
    Desc.Headline.destroy();
    Desc.Text.destroy();
    Desc.Audio.destroy();
}

// FUN_06eb44c0 (06eb44c0-06eb4677).
void __cdecl LoadNewsItems(INI_Reader* Reader)
{
    while (Reader->read_header())
    {
        if (Reader->is_header("NewsItem"))
        {
            FNewsItem Item;
            LoadNewsItemValues(Reader, &Item);
            GNewsItems.insert(GNewsItems.end(), 1, Item);                      // FUN_06eb6240
        }
    }
}

// ---------------------------------------------------------------------------------------
// BaseSupport
// ---------------------------------------------------------------------------------------

namespace
{
    struct FBaseSupport : IDLL
    {
        unsigned int ControllerId;                            // +4

        explicit FBaseSupport(unsigned int const& InControllerId);
        ~FBaseSupport() override;
        void HandleMessage(pub::Controller::EMessage MessageId, void const* Parms) override;
    };

    // FUN_06eb4ad0 (06eb4ad0-06eb4b28). No heartbeat is registered.
    FBaseSupport::FBaseSupport(unsigned int const& InControllerId)
        : ControllerId(InControllerId)
    {
        LoadGenericScripts("scripts\\gcs\\genericScripts.ini");                // FUN_06f01d70
    }

    // FUN_06eb4b50 (06eb4b50-06eb4b95).
    FBaseSupport::~FBaseSupport()
    {
        UnloadGenericScripts();                                                // FUN_06f01c70
    }

    // The "start the script of every character in the player's room" walk, inlined four
    // times in LAB_06eb4ba0 (msgs 0x1b, 0x29, 0x100b, the 0x2c tail). Base and room are
    // null-tested; the iterator is advanced after the call and end() is re-read each pass.
    void StartRoomCharacterScripts(FContentPlayerState* State)
    {
        FContentBase* Base = State->GetBaseRecord();
        if (Base == nullptr)
            return;
        unsigned int RoomId = State->LocationId;
        FBaseRoomState* Room = Base->FindRoomState(&RoomId);
        if (Room == nullptr)
            return;
        for (FBarCharacterList::iterator It = Room->Characters.begin();
             It != Room->Characters.end(); ++It)
            StartBarCharacterScript(It);                                       // FUN_06f00b60
    }

    // FUN_06eb4a00 (06eb4a00-06eb4ac2, cdecl; caller msg 0x1b 06eb4caa): a rumor was told.
    // Every other character of the room offering the same rumor (Ids[0], FUN_06ea8d40)
    // loses its offer; each of them, the teller included, restarts its script. Base is not
    // null-tested, the room is.
    void __cdecl OnRumorTold(FContentPlayerState* State, FBarCharacterList::iterator Teller)
    {
        FContentBase* Base = State->GetBaseRecord();
        unsigned int RoomId = State->LocationId;
        FBaseRoomState* Room = Base->FindRoomState(&RoomId);
        if (Room == nullptr)
            return;
        int const RumorId = Teller->Offer.Rumor->Ids[0];
        for (FBarCharacterList::iterator It = Room->Characters.begin();
             It != Room->Characters.end(); ++It)
        {
            if (It->Offer.Kind != BAR_OFFER_RUMOR || It->Offer.Rumor->Ids[0] != RumorId)
                continue;
            if (It != Teller)
            {
                FBarOffer NoOffer;
                It->SetOffer(&NoOffer);                                        // FUN_06ea7ef0
            }
            StartBarCharacterScript(It);
        }
    }

    // LAB_06eb4ba0 (06eb4ba0-06eb537a, RET 8): messages 1, 2, 0x1b, 0x29, 0x2c, 0x100b.
    void FBaseSupport::HandleMessage(pub::Controller::EMessage MessageId, void const* Parms)
    {
        switch (MessageId)
        {
        case pub::Controller::EMessage::BaseEnter:
        {
            // A player entered base Msg[0].
            unsigned int const* Msg = static_cast<unsigned int const*>(Parms);
            FContentPlayerState* State = FindContentPlayerState(Msg[1]);      // FUN_06f49b00
            FContentBase* Base = FindContentBaseInfo(&Msg[0]);                // FUN_06eac0c0
            // Left unset by retail when the query fails.
            unsigned int ShipId = 0;
            pub::Player::GetShipID(Msg[1], ShipId);
            FBaseVisit Visit;
            if (ShipId != 0)
                BuildMissionVendorOffers(Base, &Visit, State);                // FUN_06f047e0
            // No null test on Base or State.
            unsigned int const BaseId = Base->BaseId;
            for (std::vector<FNewsItem>::iterator Item = GNewsItems.begin();
                 Item != GNewsItems.end(); ++Item)
            {
                // Signed stage bounds.
                if (std::find(Item->Bases.begin(), Item->Bases.end(), BaseId) == Item->Bases.end())
                    continue;
                int const Stage = static_cast<int>(State->MissionNumber);
                if (Stage < Item->FromStage || Stage > Item->ToStage)
                    continue;
                Item->Desc.BaseId = BaseId;
                unsigned long const Handle = pub::GF::NewsBroadcastCreate(Item->Desc);
                Visit.NewsHandles.push_back(Handle);                           // FUN_06eb5770
            }
            // The offers are copied without a reference; a second record for the player is
            // dropped.
            GBaseVisits.insert(std::pair<unsigned int const, FBaseVisit>(Msg[1], Visit));
            break;
        }

        case pub::Controller::EMessage::BaseExit:
        {
            // A player left base Msg[0].
            unsigned int const* Msg = static_cast<unsigned int const*>(Parms);
            FindContentPlayerState(Msg[1]);                                   // result unused
            FContentBase* Base = FindContentBaseInfo(&Msg[0]);
            std::map<unsigned int, FBaseVisit>::iterator Visit = GBaseVisits.find(Msg[1]);
            if (Visit == GBaseVisits.end())
            {
                // Retail INT3 (an assertion), then on to the Base test.
                __debugbreak();
            }
            else
            {
                for (std::vector<unsigned long>::iterator Handle =
                         Visit->second.NewsHandles.begin();
                     Handle != Visit->second.NewsHandles.end(); ++Handle)
                    pub::GF::NewsBroadcastDestroy(&*Handle);
                Visit->second.NewsHandles.clear();                             // FUN_06eb5910
                ClearMissionVendorOffers(&Visit->second);                      // FUN_06f04a40
                GBaseVisits.erase(Visit);                                      // FUN_06eb5b50
            }
            if (Base == nullptr)
                break;
            // Retail INT3 (an assertion: BasePop must have let the player go).
            if (BasePopHasPlayer(Msg[1]))
                __debugbreak();
            Base->Unload();                                                    // FUN_06eab1c0
            break;
        }

        case pub::Controller::EMessage::GFObjSelect:
        {
            // The player talked to bar character Msg[0].
            unsigned int const* Msg = static_cast<unsigned int const*>(Parms);
            FContentPlayerState* State = FindContentPlayerState(Msg[1]);
            if (State == nullptr)
                break;
            FContentBase* Base = State->GetBaseRecord();
            if (Base == nullptr)
                break;
            FBarCharacterList::iterator Character;
            FBaseRoomState* Room = nullptr;
            if (!Base->FindBarCharacter(&Character, &Room, Msg[0]))            // FUN_06ea95e0
                break;
            State->RecordVnpcTalk(Base->BaseId, Character->Nickname.Id);       // FUN_06f48ca0
            Character->CountTalk();                                            // FUN_06ea7ee0
            if (Character->Offer.Kind != BAR_OFFER_RUMOR)
            {
                StartRoomCharacterScripts(State);
                break;
            }
            State->AddRumor(static_cast<unsigned int>(Character->Offer.Rumor->Ids[0]));
            // {player, role} to controller 0 (DAT_06fb4554, a constant 0).
            struct FRumorToldMsg
            {
                unsigned int PlayerId;
                TString<32> Role;
            } RumorMsg;
            RumorMsg.PlayerId = Msg[1];
            RumorMsg.Role.Length = 0;
            RumorMsg.Role.Data[0] = '\0';
            RumorMsg.Role = Character->Role.Data;
            unsigned int const NoController = 0;
            pub::Controller::_SendMessage(NoController, pub::Controller::EMessage::RumorHeard, &RumorMsg);
            OnRumorTold(State, Character);                                     // FUN_06eb4a00
            break;
        }

        case pub::Controller::EMessage::LocationEnter:
        case pub::Controller::EMessage::PlayerMissionChanged:
        {
            // Identical in retail for both messages: the player's room restarts.
            unsigned int const* Msg = static_cast<unsigned int const*>(Parms);
            FContentPlayerState* State = FindContentPlayerState(Msg[1]);
            if (State == nullptr)
                break;
            StartRoomCharacterScripts(State);
            break;
        }

        case pub::Controller::EMessage::MissionResponse:
        {
            // A mission offer was answered.
            FMissionOfferAcceptMsg const* Msg = static_cast<FMissionOfferAcceptMsg const*>(Parms);
            if (!Msg->bAccepted)
                break;
            FContentPlayerState* State = FindContentPlayerState(Msg->PlayerId);
            if (Msg->Kind == 2)
            {
                // A vendor offer. The visit lookup is not tested for end(), nor State for
                // null.
                std::map<unsigned int, FBaseVisit>::iterator Visit =
                    GBaseVisits.find(Msg->PlayerId);
                std::list<FBaseVendorOffer>& Offers = Visit->second.VendorOffers;
                std::list<FBaseVendorOffer>::iterator Offer = Offers.begin();
                for (; Offer != Offers.end(); ++Offer)
                {
                    if (Offer->OfferId == Msg->OfferId)
                        break;
                }
                if (Offer != Offers.end())
                {
                    FmtStr Reason;
                    bool bAccept = false;
                    if (State->GetGroupLiveMission() == 0)                     // FUN_06f487d0
                    {
                        // Loader slot 7 (+0x1c).
                        int Code = 0;
                        bAccept = Offer->Data.Loader->CanAccept(Offer->Data.Mission, &Reason,
                                                                 &Code);
                        if (bAccept)
                        {
                            unsigned int Controller = pub::Controller::Create(
                                "content.dll", Offer->Data.Type.Data, Offer->Data.Mission,
                                pub::Controller::ABOVE_NORMAL);
                            ID_String NoVnpc;
                            NoVnpc.clear();
                            State->JoinMissionAsGroup(Controller, 0, &NoVnpc, false, 0);
                            FMissionOfferAcceptMsg Forward = *Msg;
                            pub::Controller::_SendMessage(Controller, pub::Controller::EMessage::MissionResponse, &Forward);
                        }
                    }
                    else
                    {
                        Reason.begin_mad_lib(0x730);
                        Reason.end_mad_lib();
                    }
                    pub::GF::MissionVendorAcceptance(Offer->VendorHandle, bAccept, Reason,
                                                     Msg->PlayerId);
                    if (bAccept)
                    {
                        // No loader slot 5 (ReleaseMission) here, only Release.
                        pub::GF::MissionVendorOfferDestroy(&Offer->VendorHandle);
                        Offer->Data.Loader->Release();
                        Offers.erase(Offer);
                    }
                }
            }
            else if (Msg->Kind == 1)
            {
                // A bar character's offer. Base and room are not null-tested here.
                FContentBase* Base = State->GetBaseRecord();
                unsigned int RoomId = State->LocationId;
                FBaseRoomState* Room = Base->FindRoomState(&RoomId);
                for (FBarCharacterList::iterator It = Room->Characters.begin();
                     It != Room->Characters.end(); ++It)
                {
                    if (It->CharacterId != Msg->OfferId)
                        continue;
                    if (It->Offer.Accept(State, &*It))                        // FUN_06ea8d50
                    {
                        It->SetBehavior(0xffffffffu);                          // FUN_06ea7ce0
                        FBarOffer NoOffer;
                        It->SetOffer(&NoOffer);
                    }
                }
            }
            if (State == nullptr)
                break;
            StartRoomCharacterScripts(State);
            break;
        }

        default:
            break;
        }
    }
}

std::map<unsigned int, FBaseVisit> GBaseVisits;

// FUN_06eb55c0 (06eb55c0-06eb562a).
void __cdecl RefreshMissionVendorOffers(FContentPlayerState* State)
{
    std::map<unsigned int, FBaseVisit>::iterator Visit = GBaseVisits.find(State->PlayerId);
    if (Visit == GBaseVisits.end())
        return;
    ClearMissionVendorOffers(&Visit->second);                                  // FUN_06f04a40
    BuildMissionVendorOffers(State->GetBaseRecord(), &Visit->second, State);   // FUN_06f047e0
}

IDLL* CreateBaseSupport(char const* /*Name*/, unsigned int ControllerId, void const* /*Parms*/)
{
    return new (std::nothrow) FBaseSupport(ControllerId);
}
