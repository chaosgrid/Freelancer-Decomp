// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU15 (06ea5c10-06ea6d50): the "Story" controller.
// Retail object is 0x3c bytes: IDLL vptr +0 (06fb4274), story-event facet vptr +4
// (06fb4270, one slot LAB_06ea6280), ControllerId +8, PlayerId +0xc, MissionNum +0x10,
// DeltaWorth +0x14, bShipBought +0x18, MissionName char[0x20] +0x19.
// DestroyMission (TU27) lives in DestroyMission.cpp.

#include "Core/ContentInternal.h"
#include "Core/ContentStaticIds.h" // TU15 static IDs 06ea5c10..06ea5df0
#include "Core/ContentPlayerState.h"
#include "common_ini.h"
#include "common_server.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <string.h>

namespace
{
    // Retail DAT_06fced28, float[43] indexed by MissionNum. The Story ctor rewrites
    // indices 5/9/12/15/18/21/24/27/30/33/36/39/42; the rest stay zero (.bss).
    float GStoryRankThresholds[43] = {};

    // Retail DAT_06fcf388; Story save and load share this process-wide flag.
    unsigned char GStoryDebugFlag = 0;

    // Retail DAT_06fcee14, last campaign controller created by Story.
    unsigned int GStoryMissionController = 0;

    // FUN_06ea6900 copies GenericMission's second token into the stack local right after its
    // CreateParms, which the GenericMission ctor reads as CreateParms+0xC. That buffer runs
    // up to the next local (0x3c bytes).
    struct FStoryOnLoadCreateParms final : pub::Controller::CreateParms
    {
        unsigned int PlayerId;
        IStoryMissionEvents* StoryEvents;
        char GenericMissionPath[0x3c];
    };

    // Message 4 / 5 parameter blocks.
    struct FStorySaveMessage
    {
        unsigned int PlayerId;
        FILE* File;
    };

    struct FStoryLoadMessage
    {
        unsigned int PlayerId;
        INI_Reader* Reader;
    };

    class FStory : public IDLL, public IStoryMissionEvents
    {
    public:
        FStory(unsigned int const* InControllerId,
               pub::Controller::FStoryCreateParms const* Parms);
        ~FStory() override;

        void HandleMessage(pub::Controller::EMessage MessageId, void const* MessageParms) override;
        void OnMissionEvent(unsigned int EventKind, unsigned int Chapter,
                            unsigned int ControllerToDestroy) override;

    private:
        void AdvanceMission();
        void OnSave(FILE* File);
        void OnLoad(INI_Reader* Reader);
        void StartMission(unsigned int Chapter, char const* NewMissionName);
        void FinishMission(unsigned int ControllerToDestroy, unsigned int NextChapter);
        void SetMissionChapter(unsigned int Chapter, unsigned int bAdvanceRank);
        void SetNextRankWorth(FContentPlayerState* PlayerState, unsigned int Chapter);
        void SetShipBoughtReputation();

        unsigned int ControllerId;
        unsigned int PlayerId;
        int MissionNum;
        float DeltaWorth;
        bool bShipBought;
        char MissionName[0x20];
    };

    // FUN_06ea5e10.
    FStory::FStory(unsigned int const* InControllerId,
                   pub::Controller::FStoryCreateParms const* Parms)
        : ControllerId(*InControllerId)
    {
        GStoryRankThresholds[5] = 3500.0f;
        GStoryRankThresholds[9] = 5200.0f;
        GStoryRankThresholds[12] = 7000.0f;
        GStoryRankThresholds[15] = 10000.0f;
        GStoryRankThresholds[18] = 16000.0f;
        GStoryRankThresholds[21] = 0.0f;
        GStoryRankThresholds[24] = 36000.0f;
        GStoryRankThresholds[27] = 30000.0f;
        GStoryRankThresholds[30] = 0.0f;
        GStoryRankThresholds[33] = 0.0f;
        GStoryRankThresholds[36] = 0.0f;
        GStoryRankThresholds[39] = 0.0f;
        GStoryRankThresholds[42] = 0.0f;

        PlayerId = Parms->PlayerId;
        MissionNum = 0;
        FContentPlayerState* PlayerState = FindContentPlayerState(PlayerId);
        if (PlayerState != nullptr)
            SetContentPlayerMissionNumber(PlayerState, 0, 0);

        DeltaWorth = -1.0f;
        bShipBought = false;
        strncpy(MissionName, "No_Mission", 0x20);
        pub::Player::SetMoneyNeededToNextRank(PlayerId, 0);
        pub::Controller::SetHeartbeatInterval(ControllerId, 5.0f);
    }

    // FUN_06ea5f60 / FUN_06ea5f80: vptr resets only, no members to release.
    FStory::~FStory() = default;

    // FUN_06ea6d00.
    void FStory::SetMissionChapter(unsigned int Chapter, unsigned int bAdvanceRank)
    {
        MissionNum = static_cast<int>(Chapter);
        FContentPlayerState* PlayerState = FindContentPlayerState(PlayerId);
        if (PlayerState != nullptr)
            SetContentPlayerMissionNumber(PlayerState, Chapter, bAdvanceRank);
    }

    // Shared tail of FUN_06ea6c70 and the chapter 4/8/11/14/17 arms of LAB_06ea6280
    // (worth = FUN_06f48070). The x87 sum is truncated unrounded for the rank money.
    void FStory::SetNextRankWorth(FContentPlayerState* /*PlayerState*/, unsigned int Chapter)
    {
        int Cash = 0;
        float AssetValue = 0.0f;
        pub::Player::InspectCash(PlayerId, Cash);
        pub::Player::GetAssetValue(PlayerId, AssetValue);
        double const NewWorth = static_cast<double>(Cash) + static_cast<double>(AssetValue) +
                                static_cast<double>(GStoryRankThresholds[Chapter]);
        DeltaWorth = static_cast<float>(NewWorth);
        pub::Player::SetMoneyNeededToNextRank(PlayerId, static_cast<int>(NewWorth));
    }

    // Inlined in message 0x35 and event 1 / chapter 23.
    void FStory::SetShipBoughtReputation()
    {
        bShipBought = true;
        int VibeHandle = -1;
        unsigned int GroupId = 0xffffffffu;
        pub::Player::GetRep(PlayerId, VibeHandle);
        pub::Reputation::GetReputationGroup(GroupId, "gd_bh_grp");
        pub::Reputation::SetReputation(VibeHandle, GroupId, -0.2f);
    }

    // FUN_06ea6ba0.
    void FStory::StartMission(unsigned int Chapter, char const* NewMissionName)
    {
        strncpy(MissionName, NewMissionName, 0x20);
        DeltaWorth = -1.0f;
        pub::Player::SetMoneyNeededToNextRank(PlayerId, 0);
        SetMissionChapter(Chapter, 1);

        FStoryMissionCreateParms CreateParms;
        CreateParms.PlayerId = PlayerId;
        CreateParms.StoryEvents = this;
        GStoryMissionController = pub::Controller::Create(
            "Content.dll", MissionName, &CreateParms, pub::Controller::ABOVE_NORMAL);
        pub::Controller::_SendMessage(GStoryMissionController, pub::Controller::EMessage::MissionStart, nullptr);
    }

    // FUN_06ea6c70.
    void FStory::FinishMission(unsigned int ControllerToDestroy, unsigned int NextChapter)
    {
        strcpy(MissionName, "No_Mission");
        pub::Controller::Destroy(ControllerToDestroy);
        SetMissionChapter(NextChapter, 1);

        FContentPlayerState* PlayerState = FindContentPlayerState(PlayerId);
        if (PlayerState == nullptr)
            return;
        if (NextChapter == kStoryChapterComplete)
        {
            PlayerState->RecalculateRank();
            return;
        }
        SetNextRankWorth(PlayerState, NextChapter);
    }

    // FUN_06ea5f90.
    void FStory::AdvanceMission()
    {
        FContentPlayerState* PlayerState = FindContentPlayerState(PlayerId);
        if (PlayerState == nullptr)
            return;

        // Worth = FUN_06f48070 (x87); unordered does not exit.
        int Cash = 0;
        float AssetValue = 0.0f;
        pub::Player::InspectCash(PlayerId, Cash);
        pub::Player::GetAssetValue(PlayerId, AssetValue);
        double const CurrentWorth = static_cast<double>(Cash) + static_cast<double>(AssetValue);
        if (CurrentWorth < static_cast<double>(DeltaWorth))
            return;

        // The player's live mission controller (FUN_06f487c0).
        if ((PlayerState->LiveMissionId & GIdValueMask) != 0)
            return;

        switch (MissionNum)
        {
        case 5:
            StartMission(kStoryChapterMission02, "Mission_02");
            break;
        case 9:
            StartMission(kStoryChapterMission03, "Mission_03");
            break;
        case 12:
            StartMission(kStoryChapterMission04, "Mission_04");
            break;
        case 15:
            StartMission(kStoryChapterMission05, "Mission_05");
            break;
        case 18:
            StartMission(kStoryChapterMission06, "Mission_06");
            break;
        case 21:
            StartMission(kStoryChapterMission07, "Mission_07");
            break;
        case 24:
            StartMission(kStoryChapterMission08, "Mission_08");
            break;
        case 27:
            StartMission(kStoryChapterMission09, "Mission_09");
            break;
        case 30:
            StartMission(kStoryChapterMission10, "Mission_10");
            break;
        case 33:
            StartMission(kStoryChapterMission11, "Mission_11");
            break;
        case 36:
            StartMission(kStoryChapterMission12, "Mission_12");
            break;
        case 39:
            StartMission(kStoryChapterMission13, "Mission_13");
            break;
        default:
            break;
        }
    }

    // LAB_06ea6280 (secondary vtable 06fb4270).
    void FStory::OnMissionEvent(unsigned int EventKind, unsigned int Chapter,
                                unsigned int ControllerToDestroy)
    {
        if (EventKind == STORY_MISSION_EVENT_JOINED)
        {
            SetMissionChapter(Chapter, 1);
            if (Chapter == 23)
                SetShipBoughtReputation();
            return;
        }
        if (EventKind != STORY_MISSION_EVENT_SUCCEEDED)
            return;

        switch (Chapter)
        {
        case 2:
        {
            pub::Controller::Destroy(ControllerToDestroy);
            SetMissionChapter(3, 1);
            SetMissionChapter(4, 1);
            strcpy(MissionName, "Mission_01b");
            FStoryMissionCreateParms CreateParms;
            CreateParms.PlayerId = PlayerId;
            CreateParms.StoryEvents = this;
            GStoryMissionController = pub::Controller::Create(
                "Content.dll", MissionName, &CreateParms, pub::Controller::ABOVE_NORMAL);
            pub::Controller::_SendMessage(GStoryMissionController, pub::Controller::EMessage::MissionStart, nullptr);
            break;
        }
        case 4:
        case 8:
        case 11:
        case 14:
        case 17:
        {
            strcpy(MissionName, "No_Mission");
            pub::Controller::Destroy(ControllerToDestroy);
            SetMissionChapter(Chapter + 1, 1);
            FContentPlayerState* PlayerState = FindContentPlayerState(PlayerId);
            if (PlayerState != nullptr)
                SetNextRankWorth(PlayerState, Chapter + 1);
            break;
        }
        case 20:
            FinishMission(ControllerToDestroy, 21);
            StartMission(kStoryChapterMission07, "Mission_07");
            break;
        case 23:
            FinishMission(ControllerToDestroy, 24);
            break;
        case 26:
            FinishMission(ControllerToDestroy, 27);
            break;
        case 29:
            FinishMission(ControllerToDestroy, 30);
            StartMission(kStoryChapterMission10, "Mission_10");
            break;
        case 32:
            FinishMission(ControllerToDestroy, 33);
            StartMission(kStoryChapterMission11, "Mission_11");
            break;
        case 35:
            FinishMission(ControllerToDestroy, 36);
            StartMission(kStoryChapterMission12, "Mission_12");
            break;
        case 38:
            FinishMission(ControllerToDestroy, 39);
            StartMission(kStoryChapterMission13, "Mission_13");
            break;
        case 41:
            FinishMission(ControllerToDestroy, 42);
            break;
        default:
            break;
        }
    }

    // FUN_06ea6830.
    void FStory::OnSave(FILE* File)
    {
        fprintf(File, "[%s]\n", "StoryInfo");
        fprintf(File, "%s = %s\n", "ship_bought", bShipBought ? "true" : "false");
        if (MissionNum != 0 || _stricmp(MissionName, "No_Mission") != 0)
        {
            fprintf(File, "%s = %s\n", "Mission", MissionName);
            fprintf(File, "%s = %d\n", "MissionNum", MissionNum);
            fprintf(File, "%s = %f\n", "delta_worth", static_cast<double>(DeltaWorth));
        }
        fprintf(File, "%s = %d\n", "debug", GStoryDebugFlag != 0 ? 1 : 0);
    }

    // FUN_06ea6900. Retail: both strcpy's (MissionName, GenericMissionPath) are unbounded inline
    // copies.
    void FStory::OnLoad(INI_Reader* Reader)
    {
        INI_Reader::State SavedState;
        Reader->get_state(SavedState);
        Reader->reset();

        MissionNum = 0;
        FContentPlayerState* PlayerState = FindContentPlayerState(PlayerId);
        if (PlayerState != nullptr)
            SetContentPlayerMissionNumber(PlayerState, 0, 0);

        while (Reader->read_header())
        {
            if (!Reader->is_header("StoryInfo") || !Reader->read_value())
                continue;
            do
            {
                if (Reader->is_value("Mission"))
                {
                    strcpy(MissionName, Reader->get_value_string(0));
                    if (_stricmp(MissionName, "No_Mission") != 0)
                    {
                        FStoryOnLoadCreateParms CreateParms;
                        if (_stricmp(MissionName, "GenericMission") == 0)
                            strcpy(CreateParms.GenericMissionPath, Reader->get_value_string(1));
                        CreateParms.PlayerId = PlayerId;
                        CreateParms.StoryEvents = this;
                        GStoryMissionController = pub::Controller::Create(
                            "Content.dll", MissionName, &CreateParms, pub::Controller::NORMAL);
                    }
                }
                else if (Reader->is_value("MissionNum"))
                {
                    MissionNum = Reader->get_value_int(0);
                    FContentPlayerState* MissionPlayerState = FindContentPlayerState(PlayerId);
                    if (MissionPlayerState != nullptr)
                    {
                        SetContentPlayerMissionNumber(
                            MissionPlayerState, static_cast<unsigned int>(MissionNum), 0);
                    }
                    if ((GStoryMissionController & GIdValueMask) != 0)
                    {
                        FStoryLoadMessage LoadMessage;
                        LoadMessage.PlayerId = PlayerId;
                        LoadMessage.Reader = Reader;
                        pub::Controller::_SendMessage(GStoryMissionController, pub::Controller::EMessage::PlayerLoad, &LoadMessage);
                        pub::Controller::_SendMessage(GStoryMissionController, pub::Controller::EMessage::MissionStart, nullptr);
                    }
                }
                else if (Reader->is_value("delta_worth"))
                {
                    DeltaWorth = Reader->get_value_float(0);
                    pub::Player::SetMoneyNeededToNextRank(PlayerId, static_cast<int>(DeltaWorth));
                }
                else if (Reader->is_value("ship_bought"))
                {
                    bShipBought = Reader->get_value_bool(0);
                }
                else if (Reader->is_value("debug"))
                {
                    GStoryDebugFlag = Reader->get_value_bool(0) ? 1 : 0;
                }
            } while (Reader->read_value());
        }
        Reader->set_state(SavedState);
    }

    // LAB_06ea6120 (vtable 06fb4274 slot 0).
    void FStory::HandleMessage(pub::Controller::EMessage MessageId, void const* MessageParms)
    {
        switch (MessageId)
        {
        case pub::Controller::EMessage::PlayerWorthChanged:
            // x87: unordered does not exit.
            if (DeltaWorth != -1.0f && MissionNum != kStoryChapterComplete &&
                *static_cast<unsigned int const*>(MessageParms) == PlayerId)
                AdvanceMission();
            break;
        case pub::Controller::EMessage::PlayerSave:
            if (static_cast<FStorySaveMessage const*>(MessageParms)->PlayerId == PlayerId)
                OnSave(static_cast<FStorySaveMessage const*>(MessageParms)->File);
            break;
        case pub::Controller::EMessage::PlayerLoad:
            if (static_cast<FStoryLoadMessage const*>(MessageParms)->PlayerId == PlayerId)
                OnLoad(static_cast<FStoryLoadMessage const*>(MessageParms)->Reader);
            break;
        case pub::Controller::EMessage::PlayerShipArchSet:
            if (FindContentPlayerState(PlayerId) != nullptr && MissionNum >= 15 &&
                MissionNum < 23 && PlayerId == *static_cast<unsigned int const*>(MessageParms) &&
                !bShipBought)
                SetShipBoughtReputation();
            break;
        case pub::Controller::EMessage::PlayerMissionChanged:
            if (DeltaWorth != -1.0f && MissionNum != kStoryChapterComplete &&
                static_cast<unsigned int const*>(MessageParms)[1] == PlayerId &&
                (static_cast<unsigned int const*>(MessageParms)[0] & GIdValueMask) == 0)
                AdvanceMission();
            break;
        default:
            break;
        }
    }
}

IDLL* CreateStory(char const* /*Name*/, unsigned int ControllerId, void const* Parms)
{
    // FUN_06eeec50.
    return new (std::nothrow) FStory(
        &ControllerId, static_cast<pub::Controller::FStoryCreateParms const*>(Parms));
}
