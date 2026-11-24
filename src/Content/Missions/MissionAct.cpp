// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Missions/MissionAct.h"
// The Act_* code is part of retail TU21's object (its static ids at 06ec4200-06ec4280 continue
// TU21's .data run), so it has no static-id set of its own: TU21's set is MissionScript.cpp's
// ContentStaticIds.h copy. The set at 06ed3d90 heads TU22 (BaseDB.cpp).
#include "Core/ContentInternal.h"
#include "Comms/Gcs.h"                // FormatTString64 (FUN_06eec2b0), PickVoiceLine (FUN_06eeb010)
#include "Core/ContentPlayerState.h"
#include "NPCs/DockQueues.h"         // RelocateShip, CollectFormationMembers, FormFormation (TU53)
#include "Encounters/Encounters.h"         // FFormation, FindFormation (TU31)
#include "Guides/Guides.h"             // FMetaBehavior (Act_GiveMB)
#include "NPCs/NpcShip.h"            // FContentNpcShip::SetGuideFromDesc (Act_GiveMB)
#include "Missions/MissionDialogs.h"     // StartMissionDialog (FUN_06ef2a10)
#include "Missions/MissionScript.h"
#include "Encounters/Populators.h"
#include "Encounters/RandomEncounters.h"   // FRandEncPopulator::Reset (Act_EnableEnc)
#include "RandomMissions/RandomMissionGen.h"
#include "Universe/SystemInfo.h"
#include "common_goods.h"   // AmbientScriptDescription
#include "common_id.h"
#include "common_iface.h"
#include "common_ini.h"
#include "common_misc.h"    // SUBOBJ_ID_NONE
#include "common_server.h"
#include "common_server_controller.h"
#include "common_text.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string.h>

namespace
{
    // FUN_06ecdb30 (Parse shared by Act_GCSClamp, Act_LockManeuvers, Act_NagClamp,
    // Act_RandomPop, Act_RpopAttClamp, Act_RpopTLAttacksEnabled).
    unsigned char ParseBoolSlot0(INI_Reader& Reader)
    {
        return Reader.get_value_bool(0) ? 1 : 0;
    }
}

// FUN_06ec9530.
float GetRepValueFromName(FMissionScript* /*Script*/, TString<0x40> Name)
{
    if (_stricmp(Name.Data, "REP_HOSTILE_MAXIMUM") == 0)
        return -1.0f;
    if (_stricmp(Name.Data, "REP_HOSTILE_THRESHOLD") == 0)
        return -0.6f;
    if (_stricmp(Name.Data, "REP_NEUTRAL_HOSTILE") == 0)
        return -0.2f;
    if (_stricmp(Name.Data, "REP_NEUTRAL") == 0)
        return 0.0f;
    if (_stricmp(Name.Data, "REP_NEUTRAL_FRIENDLY") == 0)
        return 0.2f;
    if (_stricmp(Name.Data, "REP_FRIEND_THRESHOLD") == 0)
        return 0.6f;
    if (_stricmp(Name.Data, "REP_FRIEND_MAXIMUM") == 0)
        return 1.0f;
    return -2.0f;
}

// Inlined in every Act_* ctor (vptr 06fb5a90).
Act::Act(FMissionScript* InMission)
    : Mission(InMission)
    , TriggerId(0)
{
}

// LAB_06ec47c0.
Act::~Act()
{
}

// FUN_06ebf700.
void Act::SetTriggerId(unsigned int Id)
{
    TriggerId = Id;
}

// ---------------------------------------------------------------------------------------
// Act_ActTrig
// ---------------------------------------------------------------------------------------

// FUN_06ec7880 (DAT_06fb572c is the empty string).
ActActTrig::ActActTrig(FMissionScript* InMission)
    : Act(InMission)
    , TargetTriggerId(0)
{
    TargetTriggerId = CreateID("");
}

// FUN_06ec7900.
void ActActTrig::Parse(INI_Reader& Reader)
{
    TargetTriggerId = CreateID(Reader.get_value_string(0));
}

// LAB_06ec7920: mission+0x1d0 is a unique-key map (no insert when the key exists);
// FUN_06eb9af0 drains it after the event pass.
bool ActActTrig::Execute()
{
    if (Mission != nullptr)
    {
        Mission->PendingActivations.insert(
            std::map<unsigned int, unsigned int>::value_type(TargetTriggerId, TriggerId));
    }
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_ChangeState
// ---------------------------------------------------------------------------------------

// FUN_06ec55c0.
ActChangeState::ActChangeState(FMissionScript* InMission)
    : Act(InMission)
    , State(-1)
    , MadLibId(1)
{
}

// FUN_06ec55f0. MissionStateFromName is mission vtbl +0x68 (LAB_06eb8260, shared by every
// mission vtable).
void ActChangeState::Parse(INI_Reader& Reader)
{
    State = Mission->MissionStateFromName(Reader.get_value_string(0));
    if (!Reader.is_value_empty(1))
        MadLibId = static_cast<unsigned int>(Reader.get_value_int(1));
}

// LAB_06ec5640: mission vtbl +0x74 (LAB_06eb9730).
bool ActChangeState::Execute()
{
    Mission->SetDesiredMissionState(State, MadLibId);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_DeactTrig
// ---------------------------------------------------------------------------------------

// FUN_06ec79e0: same body as FUN_06ec7880 with vptr 06fb5bb0.
ActDeactTrig::ActDeactTrig(FMissionScript* InMission)
    : Act(InMission)
    , TargetTriggerId(0)
{
    TargetTriggerId = CreateID("");
}

// FUN_06ec7900 (shared with Act_ActTrig / Cnd_EncLaunched / Act_NagOff).
void ActDeactTrig::Parse(INI_Reader& Reader)
{
    TargetTriggerId = CreateID(Reader.get_value_string(0));
}

// LAB_06ec7a60: FUN_06eb83e0.
bool ActDeactTrig::Execute()
{
    if (Mission != nullptr)
        DeactivateMissionTrigger(Mission, &TargetTriggerId);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_GCSClamp
// ---------------------------------------------------------------------------------------

// FUN_06ec8b00.
ActGcsClamp::ActGcsClamp(FMissionScript* InMission)
    : Act(InMission)
    , bClamp(0)
{
}

// FUN_06ecdb30.
void ActGcsClamp::Parse(INI_Reader& Reader)
{
    bClamp = ParseBoolSlot0(Reader);
}

// LAB_06ec8b30: FUN_06f5cf60 (tail jump to FUN_06eeba10).
bool ActGcsClamp::Execute()
{
    SpacePopSetGcsClampEnabled(bClamp);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_RandomPop
// ---------------------------------------------------------------------------------------

// FUN_06ecdb10 (+0x0c is left to Parse).
ActRandomPop::ActRandomPop(FMissionScript* InMission)
    : Act(InMission)
    , bEnabled(0)
{
}

// FUN_06ecdb30.
void ActRandomPop::Parse(INI_Reader& Reader)
{
    bEnabled = ParseBoolSlot0(Reader);
}

// LAB_06ecdb50: FUN_06f5a800 (DAT_06fd0e58).
bool ActRandomPop::Execute()
{
    SpacePopSetPopulationEnabled(bEnabled);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_RandomPopSphere
// ---------------------------------------------------------------------------------------

// FUN_06ecdb70 (the fields are left to Parse).
ActRandomPopSphere::ActRandomPopSphere(FMissionScript* InMission)
    : Act(InMission)
    , CenterX(0.0f)
    , CenterY(0.0f)
    , CenterZ(0.0f)
    , Radius(0.0f)
    , Mode(0)
{
}

// LAB_06ecdb90. Any other mode name leaves +0x1c.
void ActRandomPopSphere::Parse(INI_Reader& Reader)
{
    CenterX = Reader.get_value_float(0);
    CenterY = Reader.get_value_float(1);
    CenterZ = Reader.get_value_float(2);
    Radius = Reader.get_value_float(3);
    TString<0x40> ModeName;
    ModeName.Length = 0;
    ModeName.Data[0] = '\0';
    ModeName = Reader.get_value_string(4);
    if (_stricmp(ModeName.Data, "on") == 0)
    {
        Mode = 0;
        return;
    }
    if (_stricmp(ModeName.Data, "off") == 0)
        Mode = 1;
}

// LAB_06ecdc80: FUN_06f5aaf0 / FUN_06f5a840.
bool ActRandomPopSphere::Execute()
{
    if (Mode == 0)
        SpacePopRemoveExclusionSphere(CenterX, CenterY, CenterZ, Radius);
    else if (Mode == 1)
        SpacePopAddExclusionSphere(CenterX, CenterY, CenterZ, Radius);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_RpopAttClamp
// ---------------------------------------------------------------------------------------

// FUN_06ece780.
ActRpopAttClamp::ActRpopAttClamp(FMissionScript* InMission)
    : Act(InMission)
    , bClamp(0)
{
}

// FUN_06ecdb30.
void ActRpopAttClamp::Parse(INI_Reader& Reader)
{
    bClamp = ParseBoolSlot0(Reader);
}

// LAB_06ece7a0: DAT_06fd1c78 (Populators.cpp).
bool ActRpopAttClamp::Execute()
{
    SpacePopSetAttackClamp(bClamp);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_RpopTLAttacksEnabled
// ---------------------------------------------------------------------------------------

// FUN_06ece7b0.
ActRpopTlAttacksEnabled::ActRpopTlAttacksEnabled(FMissionScript* InMission)
    : Act(InMission)
    , bEnabled(0)
{
}

// FUN_06ecdb30.
void ActRpopTlAttacksEnabled::Parse(INI_Reader& Reader)
{
    bEnabled = ParseBoolSlot0(Reader);
}

// LAB_06ece7d0: FUN_06f5a820.
bool ActRpopTlAttacksEnabled::Execute()
{
    SpacePopSetEncounterEnabled(bEnabled);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_Save
// ---------------------------------------------------------------------------------------

// FUN_06ec4750.
ActSave::ActSave(FMissionScript* InMission)
    : Act(InMission)
    , SaveTriggerId(0)
    , SaveType(1)
{
}

// LAB_06ec47e0.
void ActSave::Parse(INI_Reader& Reader)
{
    SaveTriggerId = CreateID(Reader.get_value_string(0));
    if (!Reader.is_value_empty(1))
        SaveType = static_cast<unsigned int>(Reader.get_value_int(1));
}

// FUN_06ec4820.
bool ActSave::Execute()
{
    Mission->bTriggerSavePending = 1;
    Mission->SaveTriggerId = SaveTriggerId;
    // Retail does this lookup (FUN_06fa7200) and discards the result.
    static_cast<void>(Mission->TriggerMap.lower_bound(SaveTriggerId));
    FmtStr Text;
    Text.begin_mad_lib(0x762);
    Text.end_mad_lib();
    pub::Player::DisplayMissionMessage(Mission->PlayerId, Text,
                                       static_cast<MissionMessageType>(7), true);
    pub::Save(Mission->PlayerId, SaveType);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_SetVibeOfferBaseHack
// ---------------------------------------------------------------------------------------

// FUN_06ecab80.
ActSetVibeOfferBaseHack::ActSetVibeOfferBaseHack(FMissionScript* InMission)
    : Act(InMission)
    , BaseId(0)
{
}

// FUN_06ecabc0 (shared with Act_DisableTradelane, Act_DockRequest, Act_PlayNN,
// Act_PlaySoundEffect).
void ActSetVibeOfferBaseHack::Parse(INI_Reader& Reader)
{
    BaseId = pub::GetNicknameId(Reader.get_value_string(0));
}

// FUN_06ecabe0: the player's reputation towards the base becomes 1.0 (SetAttitude of the
// base's vibe to the player's).
bool ActSetVibeOfferBaseHack::Execute()
{
    if (Mission->PlayerState == nullptr)
        return true;
    if (FindContentPlayerState(Mission->PlayerId) == nullptr)
        return true;
    if (Mission->PlayerState->ShipObject == nullptr)
        return true;
    int BaseRep = -1;
    int PlayerRep = -1;
    if (pub::SpaceObj::GetSolarRep(BaseId, BaseRep) != 0)
        return true;
    if (pub::Player::GetRep(Mission->PlayerId, PlayerRep) != 0)
        return true;
    pub::Reputation::SetAttitude(BaseRep, PlayerRep, 1.0f);
    return true;
}

namespace
{
    // Inlined in Act_SetTitle and Act_SetOffer: destroy, inline default construction, then the
    // new mad lib.
    void ResetMadLib(FmtStr& Text, unsigned int MadLibId)
    {
        Text.destroy();
        Text.Head = nullptr;
        Text.MadLibId = 0;
        for (int Index = 0; Index < FmtStr::KindCount; ++Index)
            Text.ValueIndex[Index] = 0;
        Text.begin_mad_lib(MadLibId);
        Text.end_mad_lib();
    }
}

// ---------------------------------------------------------------------------------------
// Act_AdjAcct
// ---------------------------------------------------------------------------------------

// FUN_06ec7ec0.
ActAdjAcct::ActAdjAcct(FMissionScript* InMission)
    : Act(InMission)
    , Amount(0)
{
}

// FUN_06ec9ac0 (shared with Act_SetOffer / Act_SetTitle).
void ActAdjAcct::Parse(INI_Reader& Reader)
{
    Amount = Reader.get_value_int(0);
}

// FUN_06ec7ef0: FUN_06f49b00 gate, then FUN_06f48030.
bool ActAdjAcct::Execute()
{
    if (FindContentPlayerState(Mission->PlayerId) != nullptr)
    {
        unsigned int const PlayerId = Mission->PlayerState->PlayerId;
        pub::Player::AdjustCash(PlayerId, Amount);
    }
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_ForceLand
// ---------------------------------------------------------------------------------------

// FUN_06ec8a30.
ActForceLand::ActForceLand(FMissionScript* InMission)
    : Act(InMission)
    , BaseId(0)
{
}

// LAB_06ec8a60.
void ActForceLand::Parse(INI_Reader& Reader)
{
    TString<0x20> Name;
    Name.Length = 0;
    Name.Data[0] = '\0';
    Name = Reader.get_value_string(0);
    pub::GetBaseID(BaseId, Name.Data);
}

// LAB_06ec8ae0.
bool ActForceLand::Execute()
{
    pub::Player::ForceLand(Mission->PlayerId, BaseId);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_GiveNNObjs
// ---------------------------------------------------------------------------------------

// FUN_06ecad60.
ActGiveNnObjs::ActGiveNnObjs(FMissionScript* InMission)
    : Act(InMission)
{
}

// FUN_06ebf6f0.
void ActGiveNnObjs::Parse(INI_Reader& /*Reader*/)
{
}

// LAB_06ecad80: FUN_06f1b610.
bool ActGiveNnObjs::Execute()
{
    Mission->RenumberAllObjectives();
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_PlayNN / Act_PlaySoundEffect
// ---------------------------------------------------------------------------------------

// FUN_06ecd340.
ActPlayNn::ActPlayNn(FMissionScript* InMission)
    : Act(InMission)
    , MessageId(0)
{
}

// FUN_06ecabc0.
void ActPlayNn::Parse(INI_Reader& Reader)
{
    MessageId = pub::GetNicknameId(Reader.get_value_string(0));
}

// LAB_06ecd370.
bool ActPlayNn::Execute()
{
    pub::Player::SendNNMessage(Mission->PlayerId, MessageId);
    return true;
}

// FUN_06ecd390.
ActPlaySoundEffect::ActPlaySoundEffect(FMissionScript* InMission)
    : Act(InMission)
    , SoundId(0)
{
}

// FUN_06ecabc0.
void ActPlaySoundEffect::Parse(INI_Reader& Reader)
{
    SoundId = pub::GetNicknameId(Reader.get_value_string(0));
}

// LAB_06ecd3c0.
bool ActPlaySoundEffect::Execute()
{
    pub::Audio::PlaySoundEffect(Mission->PlayerId, SoundId);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_PobjIdle
// ---------------------------------------------------------------------------------------

// FUN_06ecd4f0.
ActPobjIdle::ActPobjIdle(FMissionScript* InMission)
    : Act(InMission)
{
}

// FUN_06ebf6f0.
void ActPobjIdle::Parse(INI_Reader& /*Reader*/)
{
}

// LAB_06ecd510. The player ship is not null-checked.
bool ActPobjIdle::Execute()
{
    if (FindContentPlayerState(Mission->PlayerId) == nullptr)
        return true;
    pub::AI::DirectiveIdleOp Op;
    pub::AI::SubmitDirective(Mission->PlayerState->ShipObject->SpaceObjId, &Op);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_SetOffer / Act_SetTitle
// ---------------------------------------------------------------------------------------

// FUN_06ec9300.
ActSetOffer::ActSetOffer(FMissionScript* InMission)
    : Act(InMission)
    , OfferId(0)
{
}

// FUN_06ec9ac0.
void ActSetOffer::Parse(INI_Reader& Reader)
{
    OfferId = static_cast<unsigned int>(Reader.get_value_int(0));
}

// FUN_06ec9330.
bool ActSetOffer::Execute()
{
    ResetMadLib(Mission->MissionOffer, OfferId);
    if (Mission->MissionOffer.MadLibId != 1 && Mission->MissionOffer.MadLibId != 0)
        pub::Player::DisplayMissionMessage(Mission->PlayerId, Mission->MissionOffer,
                                           static_cast<MissionMessageType>(8), true);
    Mission->RenumberAllObjectives();
    return true;
}

// FUN_06ec9a90.
ActSetTitle::ActSetTitle(FMissionScript* InMission)
    : Act(InMission)
    , TitleId(0)
{
}

// FUN_06ec9ac0.
void ActSetTitle::Parse(INI_Reader& Reader)
{
    TitleId = static_cast<unsigned int>(Reader.get_value_int(0));
}

// FUN_06ec9ae0.
bool ActSetTitle::Execute()
{
    ResetMadLib(Mission->MissionTitle, TitleId);
    Mission->RenumberAllObjectives();
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_RevertCam
// ---------------------------------------------------------------------------------------

// FUN_06ece730.
ActRevertCam::ActRevertCam(FMissionScript* InMission)
    : Act(InMission)
{
}

// FUN_06ebf6f0.
void ActRevertCam::Parse(INI_Reader& /*Reader*/)
{
}

// FUN_06ece750 (RevertStaticCam = FUN_06eb8d10).
bool ActRevertCam::Execute()
{
    if (pub::Player::RevertCamera(Mission->PlayerId) == 0)
        Mission->RevertStaticCam();
    return true;
}

namespace
{
    // FUN_06f481e0 (thiscall on the player state).
    unsigned int GetPlayerShipSpaceObjId(FContentPlayerState* Player)
    {
        if (Player->ShipObject == nullptr)
            return 0;
        return Player->ShipObject->SpaceObjId;
    }

    // Retail tests an ID_String for "set" with TEST id,0x3fffffff.
    unsigned int const GActIdValueMask = 0x3fffffff;
}

// ---------------------------------------------------------------------------------------
// Act_DebugMsg
// ---------------------------------------------------------------------------------------

// FUN_06ec7a80.
ActDebugMsg::ActDebugMsg(FMissionScript* InMission)
    : Act(InMission)
{
    Text.Length = 0;
    Text.Data[0] = '\0';
    Text = "";
}

// LAB_06ec7af0.
void ActDebugMsg::Parse(INI_Reader& Reader)
{
    Text = Reader.get_value_string(0);
}

// LAB_06ea1460: the text is never shown.
bool ActDebugMsg::Execute()
{
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_DisableEnc / Act_EnableEnc
// ---------------------------------------------------------------------------------------

// FUN_06ec8370.
ActDisableEnc::ActDisableEnc(FMissionScript* InMission)
    : Act(InMission)
{
    Encounter.Length = 0;
    Encounter.Data[0] = '\0';
    Encounter = "";
}

// FUN_06ec83e0.
void ActDisableEnc::Parse(INI_Reader& Reader)
{
    Encounter = Reader.get_value_string(0);
}

// LAB_06ec8440: every matching [MsnRandEnc] of the mission (list +0x98); the whole list is
// walked.
bool ActDisableEnc::Execute()
{
    std::list<FMissionRandEnc>::iterator It = Mission->RandEncs.begin();
    for (; It != Mission->RandEncs.end(); ++It)
    {
        if (strcmp(It->Nickname.Data, Encounter.Data) == 0)
            It->bEnabled = 0;
    }
    return true;
}

// FUN_06ec85b0: same body as FUN_06ec8370.
ActEnableEnc::ActEnableEnc(FMissionScript* InMission)
    : Act(InMission)
{
    Encounter.Length = 0;
    Encounter.Data[0] = '\0';
    Encounter = "";
}

// FUN_06ec83e0.
void ActEnableEnc::Parse(INI_Reader& Reader)
{
    Encounter = Reader.get_value_string(0);
}

// LAB_06ec8620. Reset is Populator vtbl +0x14.
bool ActEnableEnc::Execute()
{
    std::list<FMissionRandEnc>::iterator It = Mission->RandEncs.begin();
    for (; It != Mission->RandEncs.end(); ++It)
    {
        if (strcmp(It->Nickname.Data, Encounter.Data) == 0)
        {
            It->Populator->Reset(It->Params);
            It->bEnabled = 1;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_DisableTradelane / Act_DockRequest
// ---------------------------------------------------------------------------------------

// FUN_06ec84c0.
ActDisableTradelane::ActDisableTradelane(FMissionScript* InMission)
    : Act(InMission)
    , RingId(0)
{
}

// FUN_06ecabc0.
void ActDisableTradelane::Parse(INI_Reader& Reader)
{
    RingId = pub::GetNicknameId(Reader.get_value_string(0));
}

// LAB_06ec84f0.
bool ActDisableTradelane::Execute()
{
    pub::SpaceObj::DrainShields(RingId);
    return true;
}

// FUN_06ec8500.
ActDockRequest::ActDockRequest(FMissionScript* InMission)
    : Act(InMission)
    , TargetId(0)
    , Field_0x10(0)
{
}

// FUN_06ecabc0.
void ActDockRequest::Parse(INI_Reader& Reader)
{
    TargetId = pub::GetNicknameId(Reader.get_value_string(0));
}

// LAB_06ec8570: the first player state (FUN_06f49c30); the ship is not null-checked.
bool ActDockRequest::Execute()
{
    FContentPlayerState* const Player = GetFirstContentPlayerState();
    if (Player == nullptr)
        return false;
    unsigned int const ShipId = Player->ShipObject->SpaceObjId;
    pub::SpaceObj::DockRequest(ShipId, TargetId);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_EnableManeuver / Act_LockManeuvers
// ---------------------------------------------------------------------------------------

// FUN_06ec86a0.
ActEnableManeuver::ActEnableManeuver(FMissionScript* InMission)
    : Act(InMission)
    , ManeuverId(-1)
    , bEnable(1)
{
}

// LAB_06ec86d0.
void ActEnableManeuver::Parse(INI_Reader& Reader)
{
    ManeuverId = BehaviorTypes::get_behavior_id(Reader.get_value_string(0));
    bEnable = Reader.get_value_bool(1) ? 1 : 0;
}

// LAB_06ec8710. The player state is not null-checked.
bool ActEnableManeuver::Execute()
{
    return pub::AI::enable_maneuver(GetPlayerShipSpaceObjId(Mission->PlayerState), ManeuverId,
                                    bEnable != 0);
}

// FUN_06ec5f70.
ActLockManeuvers::ActLockManeuvers(FMissionScript* InMission)
    : Act(InMission)
    , bLock(0)
{
}

// FUN_06ecdb30.
void ActLockManeuvers::Parse(INI_Reader& Reader)
{
    bLock = ParseBoolSlot0(Reader);
}

// LAB_06ec5fa0. The player state is not null-checked.
bool ActLockManeuvers::Execute()
{
    return pub::AI::lock_maneuvers(GetPlayerShipSpaceObjId(Mission->PlayerState), bLock != 0);
}

// ---------------------------------------------------------------------------------------
// Act_HostileClamp / Act_NagClamp / Act_PlayerEnemyClamp
// ---------------------------------------------------------------------------------------

// FUN_06ecaf90.
ActHostileClamp::ActHostileClamp(FMissionScript* InMission)
    : Act(InMission)
    , bClamp(1)
{
}

// FUN_06ecafc0.
void ActHostileClamp::Parse(INI_Reader& Reader)
{
    bClamp = Reader.get_value_bool(0) ? 0 : 1;
}

// LAB_06ecafe0: FUN_06f5a7b0.
bool ActHostileClamp::Execute()
{
    SpacePopSetHostileClamp(bClamp);
    return true;
}

// FUN_06ecb3f0.
ActNagClamp::ActNagClamp(FMissionScript* InMission)
    : Act(InMission)
    , bClamp(0)
    , Field_0x0d(1)
    , Field_0x0e(1)
{
}

// FUN_06ecdb30.
void ActNagClamp::Parse(INI_Reader& Reader)
{
    bClamp = ParseBoolSlot0(Reader);
}

// FUN_06ecb420 (SetCommMuted = FUN_06f33fd0).
bool ActNagClamp::Execute()
{
    std::map<unsigned int, FContentNpcShip*>::iterator It = Mission->NpcObjects.begin();
    for (; It != Mission->NpcObjects.end(); ++It)
        It->second->SetCommMuted(bClamp != 0);
    Mission->NagClamp = bClamp;
    return true;
}

// FUN_06eccb30.
ActPlayerEnemyClamp::ActPlayerEnemyClamp(FMissionScript* InMission)
    : Act(InMission)
    , MinClamp(2)
    , MaxClamp(2)
{
}

// LAB_06eccb60.
void ActPlayerEnemyClamp::Parse(INI_Reader& Reader)
{
    MinClamp = Reader.get_value_int(0);
    MaxClamp = Reader.get_value_int(1);
}

// FUN_06eccb90: FUN_06f49b00 gate.
bool ActPlayerEnemyClamp::Execute()
{
    if (FindContentPlayerState(Mission->PlayerId) == nullptr)
        return true;
    if ((GetPlayerShipSpaceObjId(Mission->PlayerState) & GActIdValueMask) == 0)
        return true;
    pub::AI::set_player_enemy_clamp(Mission->PlayerState->ShipObject->SpaceObjId, MinClamp,
                                    MaxClamp);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_MovePlayer / Act_RemoveRTC
// ---------------------------------------------------------------------------------------

// FUN_06ecb350 (the fields are left to Parse).
ActMovePlayer::ActMovePlayer(FMissionScript* InMission)
    : Act(InMission)
    , Position{0.0f, 0.0f, 0.0f}
    , RadiusSq(0.0f)
{
}

// LAB_06ecb370: f3 is read twice.
void ActMovePlayer::Parse(INI_Reader& Reader)
{
    Position.x = Reader.get_value_float(0);
    Position.y = Reader.get_value_float(1);
    Position.z = Reader.get_value_float(2);
    float const Radius = Reader.get_value_float(3);
    RadiusSq = Reader.get_value_float(3) * Radius;
}

// LAB_06ecb3d0: FUN_06ebb540.
bool ActMovePlayer::Execute()
{
    MoveMissionPlayer(Mission, &Position, RadiusSq);
    return true;
}

// FUN_06ece170.
ActRemoveRtc::ActRemoveRtc(FMissionScript* InMission)
    : Act(InMission)
{
    RtcFile.Length = 0;
    RtcFile.Data[0] = '\0';
    RtcFile = "";
}

// FUN_06ec1be0.
void ActRemoveRtc::Parse(INI_Reader& Reader)
{
    RtcFile = Reader.get_value_string(0);
}

// LAB_06ece1e0: FUN_06eba430.
bool ActRemoveRtc::Execute()
{
    RemoveMissionRtc(Mission, &RtcFile);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_SetNNHidden / Act_SetNNObj / Act_SetNNState / Act_NNIds
// ---------------------------------------------------------------------------------------

// FUN_06ec8f20.
ActSetNnHidden::ActSetNnHidden(FMissionScript* InMission)
    : Act(InMission)
    , bVisible(0)
    , ObjectiveId(0)
{
    ObjectiveId = CreateID("");
}

// LAB_06ec8fa0.
void ActSetNnHidden::Parse(INI_Reader& Reader)
{
    ObjectiveId = CreateID(Reader.get_value_string(0));
    bVisible = Reader.get_value_bool(1) ? 0 : 1;
}

// FUN_06ec8fe0 ("all" = DAT_06fb5ca0). HideAllObjectives = FUN_06f1b480, NnPath.Reset =
// FUN_06f1c990 on mission +0x21c, SetObjectiveVisible = FUN_06f1b260.
bool ActSetNnHidden::Execute()
{
    if (ObjectiveId == CreateID("all"))
    {
        Mission->HideAllObjectives();
        return true;
    }
    Mission->NnPath.Reset();
    return Mission->SetObjectiveVisible(&ObjectiveId, bVisible != 0);
}

// FUN_06ec9030.
ActSetNnObj::ActSetNnObj(FMissionScript* InMission)
    : Act(InMission)
    , ObjectiveId(0)
    , State(0x3a)
{
    ObjectiveId = CreateID("");
}

// LAB_06ec90b0: three independent tests, not an else chain.
void ActSetNnObj::Parse(INI_Reader& Reader)
{
    ObjectiveId = CreateID(Reader.get_value_string(0));
    if (Reader.is_value_empty(1))
        return;
    if (_stricmp(Reader.get_value_string(1), "HISTORY") == 0)
        State = 0x12a;
    if (_stricmp(Reader.get_value_string(1), "OBJECTIVE") == 0)
        State = 0x11a;
    if (_stricmp(Reader.get_value_string(1), "OBJECTIVE_HISTORY") == 0)
        State = 0x13a;
}

// FUN_06ec9150 (SetObjectiveState = FUN_06f1b310, RenumberAllObjectives = FUN_06f1b610).
bool ActSetNnObj::Execute()
{
    Mission->NnPath.Reset();
    Mission->HideAllObjectives();
    Mission->SetObjectiveState(&ObjectiveId, State);
    Mission->SetObjectiveVisible(&ObjectiveId, true);
    Mission->RenumberAllObjectives();
    return true;
}

// FUN_06ec91a0.
ActSetNnState::ActSetNnState(FMissionScript* InMission)
    : Act(InMission)
    , ObjectiveId(0)
    , State(4)
{
    ObjectiveId = CreateID("");
}

// FUN_06ec9220. Slot 1 is not tested for presence.
void ActSetNnState::Parse(INI_Reader& Reader)
{
    ObjectiveId = CreateID(Reader.get_value_string(0));
    if (_stricmp("ACTIVE", Reader.get_value_string(1)) == 0)
    {
        State = 1;
        return;
    }
    if (_stricmp("INACTIVE", Reader.get_value_string(1)) == 0)
    {
        State = 4;
        return;
    }
    if (_stricmp("COMPLETE", Reader.get_value_string(1)) == 0)
    {
        State = 3;
        return;
    }
    if (_stricmp("FAILED", Reader.get_value_string(1)) == 0)
        State = 2;
}

// LAB_06ec92e0.
bool ActSetNnState::Execute()
{
    Mission->SetObjectiveState(&ObjectiveId, State);
    return true;
}

// FUN_06ecc720.
ActNnIds::ActNnIds(FMissionScript* InMission)
    : Act(InMission)
    , TextId(1)
    , MessageType(9)
{
}

// FUN_06ecc750.
void ActNnIds::Parse(INI_Reader& Reader)
{
    TextId = Reader.get_value_int(0);
    if (Reader.is_value_empty(1))
        return;
    if (_stricmp(Reader.get_value_string(1), "OBJECTIVE") == 0)
    {
        MessageType = 7;
        return;
    }
    if (_stricmp(Reader.get_value_string(1), "HISTORY") == 0)
    {
        MessageType = 8;
        return;
    }
    if (_stricmp(Reader.get_value_string(1), "OBJECTIVE_HISTORY") == 0)
        MessageType = 9;
}

// FUN_06ecc7f0.
bool ActNnIds::Execute()
{
    FmtStr Text;
    Text.begin_mad_lib(static_cast<unsigned int>(TextId));
    Text.end_mad_lib();
    pub::Player::DisplayMissionMessage(Mission->PlayerId, Text,
                                       static_cast<MissionMessageType>(MessageType), true);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_PlayMusic
// ---------------------------------------------------------------------------------------

// FUN_06ecd120.
ActPlayMusic::ActPlayMusic(FMissionScript* InMission)
    : Act(InMission)
    , bCancel(0)
{
    Music.SpaceMusic = 0;
    Music.DangerMusic = 0;
    Music.BattleMusic = 0;
    Music.OverrideMusic = 0;
    Music.Field10 = 0;
    float const NoFade = -1.0f;
    memcpy(&Music.FadeBits, &NoFade, sizeof(NoFade));
}

// LAB_06ecd160: slots 0..3 are the track ids, 4 the fade, 5 the bool at +0x1c.
void ActPlayMusic::Parse(INI_Reader& Reader)
{
    if (Reader.is_value_empty(0) || _stricmp(Reader.get_value_string(0), "no_params") == 0)
    {
        bCancel = 1;
        return;
    }
    unsigned int* const Ids[4] = {&Music.SpaceMusic, &Music.DangerMusic, &Music.BattleMusic, &Music.OverrideMusic};
    for (unsigned int Slot = 0; Slot < 4; ++Slot)
    {
        if (!Reader.is_value_empty(Slot) && _stricmp(Reader.get_value_string(Slot), "none") != 0)
            *Ids[Slot] = pub::GetNicknameId(Reader.get_value_string(Slot));
    }
    if (!Reader.is_value_empty(4) && _stricmp(Reader.get_value_string(4), "none") != 0)
    {
        float const Fade = Reader.get_value_float(4);
        memcpy(&Music.FadeBits, &Fade, sizeof(Fade));
    }
    if (!Reader.is_value_empty(5) && _stricmp(Reader.get_value_string(5), "none") != 0)
        Music.Field10 = Reader.get_value_bool(5) ? 1 : 0;
}

// LAB_06ecd310.
bool ActPlayMusic::Execute()
{
    if (bCancel != 0)
    {
        pub::Audio::CancelMusic(Mission->PlayerId);
        return true;
    }
    pub::Audio::SetMusic(Mission->PlayerId, Music);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_PlayerCanDock / Act_PlayerCanTradelane
// ---------------------------------------------------------------------------------------

// FUN_06ecc8b0 (+0x0c left unset).
ActPlayerCanDock::ActPlayerCanDock(FMissionScript* InMission)
    : Act(InMission)
    , bCanDock(0)
{
}

// LAB_06ecc910.
void ActPlayerCanDock::Parse(INI_Reader& Reader)
{
    bCanDock = Reader.get_value_bool(0) ? 1 : 0;
    for (unsigned int Slot = 1; !Reader.is_value_empty(Slot); ++Slot)
        Exceptions.push_back(pub::GetNicknameId(Reader.get_value_string(Slot)));
}

// FUN_06eccb00: FUN_06f49b00 gate, then FUN_06f47ba0.
bool ActPlayerCanDock::Execute()
{
    if (FindContentPlayerState(Mission->PlayerId) != nullptr)
        Mission->PlayerState->SetDockAccess(bCanDock, &Exceptions);
    return true;
}

// FUN_06eccbe0.
ActPlayerCanTradelane::ActPlayerCanTradelane(FMissionScript* InMission)
    : Act(InMission)
    , bCanTradelane(1)
{
}

// LAB_06eccc50. Retail also calls is_value_empty(i + 1) and drops the result.
void ActPlayerCanTradelane::Parse(INI_Reader& Reader)
{
    bCanTradelane = Reader.get_value_bool(0) ? 1 : 0;
    for (unsigned int Slot = 1; !Reader.is_value_empty(Slot); Slot += 2)
    {
        FTlrException Pair;
        Pair.FirstRingId = pub::GetNicknameId(Reader.get_value_string(Slot));
        Pair.SecondRingId = pub::GetNicknameId(Reader.get_value_string(Slot + 1));
        Exceptions.push_back(Pair);
    }
}

// FUN_06ecce90: FUN_06f49b00 gate, then FUN_06f47d60.
bool ActPlayerCanTradelane::Execute()
{
    if (FindContentPlayerState(Mission->PlayerId) != nullptr)
        Mission->PlayerState->SetTradelaneAccess(bCanTradelane, &Exceptions);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_DisableFriendlyFire
// ---------------------------------------------------------------------------------------

// FUN_06ec8080.
ActDisableFriendlyFire::ActDisableFriendlyFire(FMissionScript* InMission)
    : Act(InMission)
{
}

// LAB_06ec81a0.
void ActDisableFriendlyFire::Parse(INI_Reader& Reader)
{
    for (unsigned int Slot = 0; !Reader.is_value_empty(Slot); ++Slot)
    {
        TString<0x30> Label;
        Label = Reader.get_value_string(Slot);
        Labels.push_back(Label);
    }
}

// LAB_06ec8270 (mission +0x2fc).
bool ActDisableFriendlyFire::Execute()
{
    std::list<TString<0x30>>::iterator It = Labels.begin();
    for (; It != Labels.end(); ++It)
    {
        bool bListed = false;
        std::list<TString<0x30>>::iterator Known = Mission->FriendlyFireOffLabels.begin();
        for (; Known != Mission->FriendlyFireOffLabels.end(); ++Known)
        {
            if (strcmp(Known->Data, It->Data) == 0)
            {
                bListed = true;
                break;
            }
        }
        if (!bListed)
            Mission->FriendlyFireOffLabels.push_back(*It);
    }
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_SetInitialPlayerPos
// ---------------------------------------------------------------------------------------

// FUN_06ecd990.
ActSetInitialPlayerPos::ActSetInitialPlayerPos(FMissionScript* InMission)
    : Act(InMission)
    , Orientation(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f)
    , Position{0.0f, 0.0f, 0.0f}
{
}

// LAB_06ecd9e0. The math engine is DAT_06fcf014 (created on first use, FUN_06fa8fa0);
// QuaternionToMatrix is vtbl +0x54.
void ActSetInitialPlayerPos::Parse(INI_Reader& Reader)
{
    Position.z = Reader.get_value_float(2);
    Position.y = Reader.get_value_float(1);
    Position.x = Reader.get_value_float(0);
    FQuaternion Rotation;
    Rotation.Z = Reader.get_value_float(6);
    Rotation.Y = Reader.get_value_float(5);
    Rotation.X = Reader.get_value_float(4);
    Rotation.W = Reader.get_value_float(3);
    if (GTriggerMathEngine == nullptr)
        GTriggerMathEngine = ContentAcquire3DMathEngine();
    FMatrix Result;
    GTriggerMathEngine->QuaternionToMatrix(Result, Rotation);
    memcpy(&Orientation, &Result, sizeof(Orientation));
}

// FUN_06ecdad0.
bool ActSetInitialPlayerPos::Execute()
{
    if (pub::Player::SetInitialPos(Mission->PlayerId, Position) == 0)
        pub::Player::SetInitialOrnt(Mission->PlayerId, Orientation);
    return true;
}

// =======================================================================================
// Nag objects (mission +0x204) and Act_NagGreet / Act_NagDistLeaving / Act_NagDistTowards /
// Act_NagOff
// =======================================================================================

namespace
{
    unsigned int const GNagIdMask = 0x3fffffff;

    // The [NPC] record of the speaker: FUN_06f15560 (the [MsnShip] slot), then the mission's
    // +0x184 map at the slot's npc id (+0x38).
    // Deviation: retail Act_NagGreet and Act_NagDistLeaving do not test for end(), so a slot
    // whose npc has no [NPC] section reads the map head's uninitialised value storage;
    // Act_NagDistTowards tests it. The port takes Act_NagDistTowards' null branch for all three.
    FMsnNpc* FindNagSpeakerNpc(FMissionScript* Mission, TString<0x30> const* Speaker)
    {
        FMissionShip* const Slot = Mission->FindMissionShip(Speaker);
        if (Slot == nullptr)
            return nullptr;
        std::map<unsigned int, FMsnNpc>::iterator It = Mission->MissionNpcs.find(Slot->NpcId.Id);
        if (It == Mission->MissionNpcs.end())
            return nullptr;
        return &It->second;
    }

    // The stores every nag Act makes after the new; the player state's system record is not
    // null-tested.
    void FillNagSpeaker(FMissionNag* Nag, FMissionScript* Mission, FMsnNpc* Npc)
    {
        Nag->PlayerId = Mission->PlayerId;
        Nag->SystemId = Mission->PlayerState->GetSystemRecord()->SystemId;
        Nag->SpeakerCostume = Npc != nullptr ? &Npc->Appearance : nullptr;
        Nag->VoiceId = Npc != nullptr ? Npc->Voice : 0;
        Nag->SpeakerNameId = Npc != nullptr ? static_cast<unsigned int>(Npc->IndividualName) : 1;
    }

    // FUN_06ecf050 (thiscall on the +0x204 map).
    void ReplaceNag(FMissionScript* Mission, unsigned int Id, FMissionNag* Nag)
    {
        std::map<unsigned int, FMissionNag*>::iterator It = Mission->Nags.find(Id);
        if (It != Mission->Nags.end())
        {
            if (It->second != nullptr)
                delete It->second;
            Mission->Nags.erase(It);
        }
        if (!Mission->Nags.insert(std::map<unsigned int, FMissionNag*>::value_type(Id, Nag)).second)
            delete Nag;
    }

    // The combat-mode tail of FUN_06ecb550 / LAB_06ecba90.
    void ParseNagCombatMode(INI_Reader& Reader, unsigned int Slot, unsigned char* bInCombat,
                            unsigned char* bOutOfCombat)
    {
        if (_stricmp(Reader.get_value_string(Slot), "NAG_IN_COMBAT") == 0)
        {
            *bInCombat = 1;
            *bOutOfCombat = 0;
            return;
        }
        if (_stricmp(Reader.get_value_string(Slot), "NAG_OUT_OF_COMBAT") == 0)
        {
            *bInCombat = 0;
            *bOutOfCombat = 1;
            return;
        }
        if (_stricmp(Reader.get_value_string(Slot), "NAG_ALWAYS") == 0)
        {
            *bInCombat = 1;
            *bOutOfCombat = 1;
        }
    }

    // Only an exact -1 (DAT_06fb5e84) becomes DAT_06fb4674.
    float NagDistanceOrDefault(float Value)
    {
        if (Value == -1.0f)
            return 4000.0f;
        return Value;
    }
}

// Inlined in the nag ctors. Retail leaves +0x10 unset; every creator writes it.
FMissionNag::FMissionNag()
    : SpeakerId(0)
    , PlayerId(0)
    , SystemId(0)
    , SpeakerCostume(nullptr)
    , VoiceId(0)
    , LineId(0)
    , SpeakerNameId(0)
    , bDone(0)
{
}

// FUN_06ecf4f0 -> FUN_06ecc2e0.
FMissionNag::~FMissionNag()
{
}

// FUN_06ecf190.
void FMissionNag::Say(char const* Name)
{
    FContentPlayerState* const Player = FindContentPlayerState(PlayerId);
    if (Player == nullptr)
        return;
    // A ship in behavior 7 is not nagged.
    unsigned int const ShipId = Player->GetShipId();
    if ((ShipId & GNagIdMask) != 0 && pub::AI::get_behavior_id(ShipId) == 7)
        return;
    if ((SpeakerId & GNagIdMask) == 0)
    {
        // No speaker: the NN line "<Name>_01-".
        char Text[0x40];
        sprintf(Text, "%s_0%d-", Name, 1);
        LineId = pub::GetNicknameId(Text);
        pub::Player::SendNNMessage(PlayerId, LineId);
        return;
    }
    // A voice line of the speaker (DAT_06fb6064 = "-").
    TString<0x40> Line;
    Line.Length = 0;
    Line.Data[0] = '\0';
    if (PickVoiceLine(Name, "-", VoiceId, &Line) != 0)
        return;
    LineId = pub::GetNicknameId(Line.Data);
    FContentPlayerState* const State = FindContentPlayerState(PlayerId);
    FSystemInfo* const System = FindSystemInfo(&SystemId);
    if (System == nullptr || State == nullptr)
        return;
    if ((State->GetShipId() & GNagIdMask) == 0)
        return;
    // A speaker in the player's system talks itself.
    unsigned int SpeakerSystem = 0;
    pub::SpaceObj::GetSystem(SpeakerId, SpeakerSystem);
    if ((SpeakerSystem & GNagIdMask) != 0 && SpeakerSystem == State->GetSystemId())
    {
        System->SendComm(SpeakerId, State->GetShipId(), LineId, 0, 0, nullptr, 0.5f);
        return;
    }
    // Otherwise an anonymous comm with the [NPC] voice, name and costume.
    System->SendComm(0, State->GetShipId(), LineId, VoiceId, SpeakerNameId, SpeakerCostume, 0.5f);
}

// Inlined in LAB_06ecc080.
FMissionNagGreet::FMissionNagGreet()
{
    MessageIds.push_back(pub::Controller::EMessage::LaunchComplete);
}

// FUN_06ecf310: the player's ship (parms+4) greeted by a speaker of the player's system.
bool FMissionNagGreet::HandleMessage(pub::Controller::EMessage /*MessageId*/, void const* MessageParms)
{
    unsigned int const* Parms = static_cast<unsigned int const*>(MessageParms);
    FSystemInfo* const System = FindSystemInfo(&SystemId);
    FContentPlayerState* const State = FindContentPlayerState(PlayerId);
    if ((SpeakerId & GNagIdMask) == 0 || System == nullptr || State == nullptr)
        return true;
    if (State->GetShipId() != Parms[1])
        return true;
    if ((State->GetShipId() & GNagIdMask) == 0)
        return true;
    // The speaker taken as a ship; IsInCombat is its slot 2.
    FContentShipBase* const Speaker = static_cast<FContentShipBase*>(System->FindSpaceObj(SpeakerId));
    if (Speaker == nullptr)
        return true;
    if (Speaker->IsInCombat())
    {
        Say("nag_greet_combat");
        return true;
    }
    Say("nag_greet_normal");
    return true;
}

// FUN_06ecf3a0. Retail leaves +0x5c unset (the timer start writes it before any reader).
FMissionNagDist::FMissionNagDist()
    : Mission(nullptr)
    , TargetId(0)
    , Distance(2000.0f)
    , FailMadLibId(1)
    , Position{0.0f, 0.0f, 0.0f}
    , LastPosition{0.0f, 0.0f, 0.0f}
    , Threshold(0.0f)
    , bPositionMode(0)
    , bNagInCombat(0)
    , bNagOutOfCombat(1)
    , bLeavingArea(0)
    , TimerId(0)
    , bArmed(0)
    , NagsLeft(4)
{
    MessageIds.push_back(pub::Controller::EMessage::Heartbeat);
    MessageIds.push_back(pub::Controller::EMessage::CommComplete);
    MessageIds.push_back(pub::Controller::EMessage::TimerExpired);
    MessageIds.push_back(pub::Controller::EMessage::TradelaneEnter);
    MessageIds.push_back(pub::Controller::EMessage::DockComplete);
    MessageIds.push_back(pub::Controller::EMessage::Msg0x27);
}

// LAB_06ecf5b0 (SetDesiredMissionState = mission vtbl +0x74).
bool FMissionNagDist::CountDown()
{
    --NagsLeft;
    if (NagsLeft != 0)
        return true;
    Mission->SetDesiredMissionState(0, FailMadLibId);
    return false;
}

// FUN_06ecf510: no line while the speaker NPC flies in the player's formation (+0x154).
// Without a speaker every line spends a nag (slot 3).
void FMissionNagDist::Say(char const* Name)
{
    FContentPlayerState* const State = FindContentPlayerState(PlayerId);
    FContentNpcShip* const Npc =
        (SpeakerId & GNagIdMask) != 0 ? Mission->FindNpcById(&SpeakerId) : nullptr;
    FContentPlayerShip* const Ship = State != nullptr ? State->ShipObject : nullptr;
    bool const bSameGroup = Npc != nullptr && Ship != nullptr &&
                            Npc->FormationId == Ship->FormationId && Npc->FormationId != 0xffffffffu;
    if (!bSameGroup)
    {
        if (bPositionMode != 0)
            FMissionNag::Say(bLeavingArea != 0 ? "nag_leaving_mission_area" : "nag_pos");
        else
            FMissionNag::Say(Name);
    }
    if ((SpeakerId & GNagIdMask) == 0)
        CountDown();
}

namespace
{
    // Inlined in LAB_06ecf5d0; the timer id comes from DAT_06fce76c.
    void StartNagTimer(FMissionNagDist* Nag)
    {
        Nag->Threshold = 900.0f;
        unsigned int const Id = GContentNextUniqueId;
        GContentNextUniqueId = Id + 1;
        Nag->TimerId = Id;
        pub::Controller::TimerExpired Timer;
        Timer.Param1 = Id;
        Timer.Param2 = 0;
        pub::SetTimer(Nag->Mission->ControllerId, Timer, 20.0f);
    }
}

// LAB_06ecf5d0. True on every path but message 0xb, which returns CountDown's result.
bool FMissionNagDist::HandleMessage(pub::Controller::EMessage MessageId, void const* MessageParms)
{
    using pub::Controller::EMessage;
    unsigned int const* Parms = static_cast<unsigned int const*>(MessageParms);
    FSystemInfo* const System = FindSystemInfo(&SystemId);
    if (System == nullptr)
        return true;
    FContentPlayerState* const State = FindContentPlayerState(PlayerId);
    if (State == nullptr)
        return true;
    if ((State->GetShipId() & GNagIdMask) == 0)
        return true;

    if (MessageId == EMessage::Heartbeat)
    {
        // A target object refreshes the position.
        if ((TargetId & GNagIdMask) != 0)
        {
            Matrix Orientation;
            pub::SpaceObj::GetLocation(TargetId, Position, Orientation);
        }
        if (bArmed != 0)
            return true;
        // Armed once the player is farther than Distance (only when D*D is below; unordered
        // returns).
        Vector Here;
        State->GetPosition(&Here);
        double const DX = static_cast<double>(Position.x) - static_cast<double>(Here.x);
        double const DY = static_cast<double>(Position.y) - static_cast<double>(Here.y);
        double const DZ = static_cast<double>(Position.z) - static_cast<double>(Here.z);
        double const DistSq = DZ * DZ + DY * DY + DX * DX;
        double const Range = static_cast<double>(Distance);
        if (!(Range * Range < DistSq))
            return true;
        State->GetPosition(&LastPosition);
        bArmed = 1;
        StartNagTimer(this);
        return true;
    }
    if (MessageId == EMessage::CommComplete)
    {
        // The speaker's (or an anonymous) comm of the last line finished (parms +0x10 == 0)
        // spends a nag.
        if (SpeakerId != Parms[2] && (Parms[2] & GNagIdMask) != 0)
            return true;
        if (LineId != Parms[3])
            return true;
        if (Parms[4] != 0)
            return true;
        return CountDown();
    }
    if (MessageId == EMessage::TimerExpired)
    {
        // The nag timer. Nag when the player came less than Threshold closer since the last
        // check and is still farther than Distance (unordered disarms); otherwise disarm.
        if (bArmed == 0)
            return true;
        if (Parms[0] != TimerId)
            return true;
        float const LastX = static_cast<float>(static_cast<double>(LastPosition.x) - Position.x);
        float const LastY = static_cast<float>(static_cast<double>(LastPosition.y) - Position.y);
        float const LastZ = static_cast<float>(static_cast<double>(LastPosition.z) - Position.z);
        Vector Here;
        State->GetPosition(&Here);
        double const EX = static_cast<double>(Here.x) - static_cast<double>(Position.x);
        double const EY = static_cast<double>(Here.y) - static_cast<double>(Position.y);
        double const EZ = static_cast<double>(Here.z) - static_cast<double>(Position.z);
        double const HereDist = sqrt(EZ * EZ + EY * EY + EX * EX);
        double const LastDist = sqrt(static_cast<double>(LastZ) * LastZ +
                                     static_cast<double>(LastY) * LastY +
                                     static_cast<double>(LastX) * LastX);
        if (!(LastDist - HereDist < static_cast<double>(Threshold)) ||
            !(HereDist > static_cast<double>(Distance)))
        {
            TimerId = 0;
            bArmed = 0;
            NagsLeft = 4;
            return true;
        }
        State->GetPosition(&LastPosition);
        unsigned int Type = 0;
        if ((TargetId & GNagIdMask) == 0)
        {
            Say("nag_pos");
        }
        else if (pub::SpaceObj::GetType(TargetId, Type) == 0)
        {
            if ((Type & 0x7f0000) != 0)
            {
                // A ship target, by its combat flag (slot 2).
                FContentShipBase* const Target =
                    static_cast<FContentShipBase*>(System->FindSpaceObj(TargetId));
                if (Target != nullptr)
                {
                    if (Target->IsInCombat() && bNagInCombat != 0)
                        Say("nag_dst_ship");
                    else if (!Target->IsInCombat() && bNagOutOfCombat != 0)
                        Say("nag_pos");
                    else
                        bArmed = 0;
                }
            }
            else if ((Type & 0x100) != 0 || (Type & 0x20) != 0)
            {
                Say("nag_dst_base");
            }
            else if ((Type & 0x800) != 0)
            {
                Say("nag_dst_hole");
            }
            else if ((Type & 0xc40) != 0)
            {
                Say("nag_dst_gate");
            }
            else if ((Type & 0x80) != 0)
            {
                Say("nag_dst_tl");
            }
        }
        if (bArmed == 0)
            return true;
        State->GetPosition(&LastPosition);
        StartNagTimer(this);
        return true;
    }
    if (MessageId == EMessage::DockComplete)
    {
        // The player's ship (parms+0) jumped through the gate target.
        unsigned int Type = 0;
        if (pub::SpaceObj::GetType(TargetId, Type) != 0)
            return true;
        if ((Type & 0xc40) == 0)
            return true;
        if (TargetId != Parms[1])
            return true;
        if (State->GetShipId() != Parms[0])
            return true;
        bDone = 1;
        return true;
    }
    if (MessageId == EMessage::TradelaneEnter)
    {
        // The player's ship entered the trade lane target.
        if (TargetId != Parms[1])
            return true;
        if (State->GetShipId() != Parms[0])
            return true;
        bDone = 1;
        return true;
    }
    if (MessageId == EMessage::Msg0x27)
    {
        // The player's ship docks with the target (a docking-ring base: its docking target is
        // parms+4; a base (0x20): the target is parms+8).
        unsigned int const Docker = Parms[0];
        if (Docker != State->GetShipId())
            return true;
        if ((Docker & GNagIdMask) == 0)
            return true;
        unsigned int Type = 0;
        if (pub::SpaceObj::GetType(TargetId, Type) != 0)
            return true;
        unsigned int DockTarget = 0;
        pub::SpaceObj::GetDockingTarget(TargetId, DockTarget);
        if ((Type & 0x100) != 0 && (Parms[1] & GNagIdMask) != 0 && DockTarget == Parms[1])
        {
            bDone = 1;
            return true;
        }
        if ((Type & 0x20) != 0 && TargetId == Parms[2])
            bDone = 1;
        return true;
    }
    return true;
}

// FUN_06ecef10.
void DispatchNagMessage(FMissionScript* Script, pub::Controller::EMessage MessageId,
                        void const* MessageParms)
{
    unsigned int const* Parms = static_cast<unsigned int const*>(MessageParms);
    std::map<unsigned int, FMissionNag*>& Nags = Script->Nags;
    // System enter moves the player's nags to the new system.
    if (MessageId == pub::Controller::EMessage::SystemEnter)
    {
        std::map<unsigned int, FMissionNag*>::iterator It = Nags.begin();
        for (; It != Nags.end(); ++It)
        {
            if (It->second->PlayerId == Parms[2])
                It->second->SystemId = Parms[0];
        }
    }
    // Retail reads it as the map's +0x14 (mission +0x218).
    if (Script->NagClamp != 0)
        return;
    std::map<unsigned int, FMissionNag*>::iterator It = Nags.begin();
    while (It != Nags.end())
    {
        FMissionNag* const Nag = It->second;
        bool bListens = false;
        std::vector<pub::Controller::EMessage>::iterator IdIt = Nag->MessageIds.begin();
        for (; IdIt != Nag->MessageIds.end(); ++IdIt)
        {
            if (*IdIt == MessageId)
            {
                bListens = true;
                break;
            }
        }
        if (!bListens)
        {
            ++It;
            continue;
        }
        Nag->HandleMessage(MessageId, MessageParms);
        // A done nag is unlinked (FUN_06f20e00); retail does not delete the object.
        if (It->second->bDone != 0)
        {
            It = Nags.erase(It);
            continue;
        }
        ++It;
    }
}

// ---------------------------------------------------------------------------------------
// Act_NagGreet
// ---------------------------------------------------------------------------------------

// FUN_06ecbf90.
ActNagGreet::ActNagGreet(FMissionScript* InMission)
    : Act(InMission)
    , NagId(0)
{
    Speaker.Length = 0;
    Speaker.Data[0] = '\0';
}

// FUN_06ecc000.
void ActNagGreet::Parse(INI_Reader& Reader)
{
    NagId = CreateID(Reader.get_value_string(0));
    Speaker = Reader.get_value_string(1);
}

// LAB_06ecc080.
bool ActNagGreet::Execute()
{
    if (FindContentPlayerState(Mission->PlayerId) == nullptr)
        return true;
    FMissionNagGreet* const Nag = new FMissionNagGreet();
    // DAT_06fb57a0 = "none".
    FMsnNpc* Npc = nullptr;
    if (_stricmp(Speaker.Data, "none") != 0)
        Npc = FindNagSpeakerNpc(Mission, &Speaker);
    // The speaker id is resolved even for "none".
    Nag->SpeakerId = Mission->GetLabelObjectId(&Speaker);
    FillNagSpeaker(Nag, Mission, Npc);
    // Unlike the distance nags, an existing id keeps its nag and the new one is deleted.
    if (!Mission->Nags.insert(std::map<unsigned int, FMissionNag*>::value_type(NagId, Nag)).second)
        delete Nag;
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_NagDistLeaving
// ---------------------------------------------------------------------------------------

// FUN_06ecb4a0.
ActNagDistLeaving::ActNagDistLeaving(FMissionScript* InMission)
    : Act(InMission)
    , NagId(0)
    , Position{0.0f, 0.0f, 0.0f}
    , bHasPosition(0)
    , Distance(4000.0f)
    , FailMadLibId(1)
    , bNagInCombat(0)
    , bNagOutOfCombat(1)
{
    Speaker.Length = 0;
    Speaker.Data[0] = '\0';
    Target.Length = 0;
    Target.Data[0] = '\0';
}

// FUN_06ecb550: "position" (06fb4678) takes f3..f5 and the optional i6 / f7 / s8, a label
// the optional i3 / f4 / s5.
void ActNagDistLeaving::Parse(INI_Reader& Reader)
{
    NagId = CreateID(Reader.get_value_string(0));
    Speaker = Reader.get_value_string(1);
    Target = Reader.get_value_string(2);
    if (_stricmp(Target.Data, "position") == 0)
    {
        // Read in the order 5, 4, 3.
        float const Z = Reader.get_value_float(5);
        float const Y = Reader.get_value_float(4);
        float const X = Reader.get_value_float(3);
        Position.x = X;
        Position.y = Y;
        Position.z = Z;
        bHasPosition = 1;
        if (!Reader.is_value_empty(6))
            FailMadLibId = static_cast<unsigned int>(Reader.get_value_int(6));
        if (!Reader.is_value_empty(7))
            Distance = NagDistanceOrDefault(Reader.get_value_float(7));
        if (!Reader.is_value_empty(8))
            ParseNagCombatMode(Reader, 8, &bNagInCombat, &bNagOutOfCombat);
        return;
    }
    if (!Reader.is_value_empty(3))
        FailMadLibId = static_cast<unsigned int>(Reader.get_value_int(3));
    if (!Reader.is_value_empty(4))
        Distance = NagDistanceOrDefault(Reader.get_value_float(4));
    if (!Reader.is_value_empty(5))
        ParseNagCombatMode(Reader, 5, &bNagInCombat, &bNagOutOfCombat);
}

// LAB_06ecb800.
bool ActNagDistLeaving::Execute()
{
    if (FindContentPlayerState(Mission->PlayerId) == nullptr)
        return true;
    FMsnNpc* Npc = nullptr;
    if (_stricmp(Speaker.Data, "none") != 0)
        Npc = FindNagSpeakerNpc(Mission, &Speaker);
    FMissionNagDist* const Nag = new FMissionNagDist();
    Nag->SpeakerId = _stricmp(Speaker.Data, "none") != 0 ? Mission->GetLabelObjectId(&Speaker) : 0;
    FillNagSpeaker(Nag, Mission, Npc);
    Nag->Mission = Mission;
    // A label target, else ("" or "position") no speaker and the leaving-area line.
    unsigned int TargetObjectId = 0;
    if (Target.Length != 0 && _stricmp(Target.Data, "position") != 0)
    {
        TargetObjectId = Mission->GetLabelObjectId(&Target);
    }
    else
    {
        Nag->SpeakerId = 0;
        Nag->bLeavingArea = 1;
    }
    Nag->TargetId = TargetObjectId;
    Nag->Distance = Distance;
    Nag->FailMadLibId = FailMadLibId;
    Nag->bNagInCombat = bNagInCombat;
    Nag->bNagOutOfCombat = bNagOutOfCombat;
    Nag->bPositionMode = bHasPosition;
    if (bHasPosition != 0)
        Nag->Position = Position;
    ReplaceNag(Mission, NagId, Nag);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_NagDistTowards
// ---------------------------------------------------------------------------------------

// FUN_06ecb9e0.
ActNagDistTowards::ActNagDistTowards(FMissionScript* InMission)
    : Act(InMission)
    , NagId(0)
    , Distance(4000.0f)
    , FailMadLibId(1)
    , Position{0.0f, 0.0f, 0.0f}
    , bNagInCombat(0)
    , bNagOutOfCombat(1)
{
    Speaker.Length = 0;
    Speaker.Data[0] = '\0';
    Target.Length = 0;
    Target.Data[0] = '\0';
}

// LAB_06ecba90: s0 "OBJ" (06fb5e98) / "POS" (06fb5e94) picks the form, POS by default. The POS
// form calls is_value_empty(5) / (9) without using them, and its f7 has no -1 default.
void ActNagDistTowards::Parse(INI_Reader& Reader)
{
    bool bObjectForm = false;
    if (_stricmp(Reader.get_value_string(0), "OBJ") == 0)
        bObjectForm = true;
    else if (_stricmp(Reader.get_value_string(0), "POS") == 0)
        bObjectForm = false;
    NagId = CreateID(Reader.get_value_string(1));
    Speaker = Reader.get_value_string(2);
    if (bObjectForm)
    {
        Target = Reader.get_value_string(3);
        if (!Reader.is_value_empty(4))
            FailMadLibId = static_cast<unsigned int>(Reader.get_value_int(4));
        if (!Reader.is_value_empty(5))
            Distance = NagDistanceOrDefault(Reader.get_value_float(5));
        if (!Reader.is_value_empty(6))
            ParseNagCombatMode(Reader, 6, &bNagInCombat, &bNagOutOfCombat);
        return;
    }
    static_cast<void>(Reader.is_value_empty(5));
    static_cast<void>(Reader.is_value_empty(9));
    float const Z = Reader.get_value_float(5);
    float const Y = Reader.get_value_float(4);
    float const X = Reader.get_value_float(3);
    Position.x = X;
    Position.y = Y;
    Position.z = Z;
    if (!Reader.is_value_empty(6))
        FailMadLibId = static_cast<unsigned int>(Reader.get_value_int(6));
    if (!Reader.is_value_empty(7))
        Distance = Reader.get_value_float(7);
    if (!Reader.is_value_empty(8))
        ParseNagCombatMode(Reader, 8, &bNagInCombat, &bNagOutOfCombat);
}

// LAB_06ecbda0.
bool ActNagDistTowards::Execute()
{
    if (FindContentPlayerState(Mission->PlayerId) == nullptr)
        return true;
    FMsnNpc* Npc = nullptr;
    if (_stricmp(Speaker.Data, "none") != 0)
        Npc = FindNagSpeakerNpc(Mission, &Speaker);
    FMissionNagDist* const Nag = new FMissionNagDist();
    Nag->SpeakerId = _stricmp(Speaker.Data, "none") != 0 ? Mission->GetLabelObjectId(&Speaker) : 0;
    FillNagSpeaker(Nag, Mission, Npc);
    Nag->Mission = Mission;
    Nag->bNagInCombat = bNagInCombat;
    Nag->bNagOutOfCombat = bNagOutOfCombat;
    Nag->bPositionMode = 1;
    if (Target.Length != 0)
    {
        Nag->TargetId = Mission->GetLabelObjectId(&Target);
        Matrix Orientation;
        pub::SpaceObj::GetLocation(Nag->TargetId, Nag->Position, Orientation);
    }
    else
    {
        Nag->Position = Position;
        Nag->TargetId = 0;
    }
    Nag->Distance = Distance;
    Nag->FailMadLibId = FailMadLibId;
    ReplaceNag(Mission, NagId, Nag);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_NagOff
// ---------------------------------------------------------------------------------------

// FUN_06ecc310.
ActNagOff::ActNagOff(FMissionScript* InMission)
    : Act(InMission)
    , NagId(0)
{
}

// FUN_06ec7900.
void ActNagOff::Parse(INI_Reader& Reader)
{
    NagId = CreateID(Reader.get_value_string(0));
}

// LAB_06ecc370.
bool ActNagOff::Execute()
{
    std::map<unsigned int, FMissionNag*>::iterator It = Mission->Nags.find(NagId);
    if (It == Mission->Nags.end())
        return true;
    if (It->second != nullptr)
        delete It->second;
    Mission->Nags.erase(It);
    return true;
}

// =======================================================================================
// Objects, formations and scripts
// =======================================================================================

namespace
{
    // The inline TString construction of the Act ctors that assign DAT_06fb572c (""): {0, ""}.
    template <unsigned N>
    void ClearTString(TString<N>& Text)
    {
        Text.Length = 0;
        Text.Data[0] = '\0';
    }
}

// ---------------------------------------------------------------------------------------
// Act_AdjHealth
// ---------------------------------------------------------------------------------------

// FUN_06ec4e50.
ActAdjHealth::ActAdjHealth(FMissionScript* InMission)
    : Act(InMission)
    , FormationId(0)
    , Delta(0.0f)
{
}

// LAB_06ec4ec0.
void ActAdjHealth::Parse(INI_Reader& Reader)
{
    FormationId = CreateID(Reader.get_value_string(0));
    Delta = Reader.get_value_float(1);
}

// FUN_06ec4f00: the upper bound is re-read each pass; the clamp lets NaN through. Retail also
// calls FUN_06f15c70 (the member's mission name) into a local nothing reads; it has no side
// effect, so the port omits it.
bool ActAdjHealth::Execute()
{
    std::map<unsigned int, float> Healths;
    std::multimap<unsigned int, unsigned int>::iterator Member =
        Mission->LabelObjects.lower_bound(FormationId);
    while (Member != Mission->LabelObjects.upper_bound(FormationId))
    {
        float Health = 0.0f;
        pub::SpaceObj::GetRelativeHealth(Member->second, Health);
        Health = Health + Delta;
        if (Health < 0.0f)
            Health = 0.0f;
        else if (Health > 1.0f)
            Health = 1.0f;
        Healths.insert(std::map<unsigned int, float>::value_type(Member->second, Health));
        ++Member;
    }
    std::map<unsigned int, float>::iterator It = Healths.begin();
    for (; It != Healths.end(); ++It)
        pub::SpaceObj::SetRelativeHealth(It->first, It->second);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_CallThorn
// ---------------------------------------------------------------------------------------

// FUN_06ec51f0.
ActCallThorn::ActCallThorn(FMissionScript* InMission)
    : Act(InMission)
    , Position{0.0f, 0.0f, 0.0f}
    , Mode(0)
{
    ClearTString(Target);
    Target = "";
    ClearTString(Script);
    Script = "";
}

// LAB_06ec52e0. The position is read as integers, in the order 3, 2, 1.
void ActCallThorn::Parse(INI_Reader& Reader)
{
    Script = Reader.get_value_string(0);
    if (Reader.is_value_empty(2))
    {
        if (Reader.is_value_empty(1))
            Target = "Player";
        else
            Target = Reader.get_value_string(1);
        Mode = 1;
        return;
    }
    Target = "Player";
    float const Z = static_cast<float>(Reader.get_value_int(3));
    float const Y = static_cast<float>(Reader.get_value_int(2));
    float const X = static_cast<float>(Reader.get_value_int(1));
    Position.x = X;
    Position.y = Y;
    Position.z = Z;
    Mode = 2;
}

// LAB_06ec54b0: with a live player the cutscene also makes the ship invincible and mutes the
// nags, the NPCs and GCS.
bool ActCallThorn::Execute()
{
    unsigned int const TargetId = Mission->GetLabelObjectId(&Target);
    if ((TargetId & GNagIdMask) == 0)
        return false;
    int const ScriptMode = Mode;
    pub::SpaceObj::RequestSpaceScript(TargetId, Position, ScriptMode, Mission->PlayerId,
                                      Script.Data);
    if (FindContentPlayerState(Mission->PlayerId) == nullptr)
        return true;
    Mission->bCutsceneActive = 1;
    unsigned int const ShipId = Mission->PlayerState->GetShipId();
    if ((ShipId & GNagIdMask) != 0)
        pub::SpaceObj::SetInvincible(ShipId, true, false, 0.0f);
    Mission->NagClamp = 1;
    std::map<unsigned int, FContentNpcShip*>::iterator It = Mission->NpcObjects.begin();
    for (; It != Mission->NpcObjects.end(); ++It)
        It->second->SetCommMuted(true);
    SpacePopSetGcsClampEnabled(1);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_Cloak
// ---------------------------------------------------------------------------------------

// FUN_06ec5660.
ActCloak::ActCloak(FMissionScript* InMission)
    : Act(InMission)
    , bCloak(0)
{
    ClearTString(Formation);
}

// FUN_06ec5690.
void ActCloak::Parse(INI_Reader& Reader)
{
    Formation = Reader.get_value_string(0);
    bCloak = Reader.get_value_bool(1) ? 1 : 0;
}

// LAB_06ec5700: the member's mission name (FUN_06f15c70) is tracked in the mission's +0x158
// list (first match wins).
bool ActCloak::Execute()
{
    unsigned int const Key = CreateID(Formation.Data);
    std::multimap<unsigned int, unsigned int>::iterator Member =
        Mission->LabelObjects.lower_bound(Key);
    while (Member != Mission->LabelObjects.upper_bound(Key))
    {
        TString<0x30> const Name = Mission->GetMissionObjectName(&Member->second);
        std::list<TString<0x30>>::iterator Listed = Mission->DecloakedNames.begin();
        bool bListed = false;
        while (Listed != Mission->DecloakedNames.end())
        {
            if (strcmp(Listed->Data, Name.Data) == 0)
            {
                bListed = true;
                break;
            }
            ++Listed;
        }
        if (bCloak != 0)
        {
            if (bListed)
                Mission->DecloakedNames.erase(Listed);
        }
        else if (!bListed)
        {
            Mission->DecloakedNames.push_back(Name);
        }

        pub::SpaceObj::EQInfo Info;
        memset(&Info, 0, sizeof(Info));
        Info.Type = 2;
        Info.EquipId = bCloak;
        pub::SpaceObj::ActivateEquipment(Member->second, &Info);
        ++Member;
    }
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_LockDock
// ---------------------------------------------------------------------------------------

// FUN_06ec5cb0. Retail does not write +0x74 (see the header).
ActLockDock::ActLockDock(FMissionScript* InMission)
    : Act(InMission)
    , Mode(-1)
{
    ClearTString(Target);
    ClearTString(Gate);
    Target = "";
    Gate = "";
}

// LAB_06ec5d60.
void ActLockDock::Parse(INI_Reader& Reader)
{
    Target = Reader.get_value_string(0);
    Gate = Reader.get_value_string(1);
    if (_stricmp(Reader.get_value_string(2), "lock") == 0)
    {
        Mode = 1;
        return;
    }
    if (_stricmp(Reader.get_value_string(2), "unlock") == 0)
        Mode = 0;
}

// LAB_06ec5e60. Register = FUN_06f6d400, Unregister = FUN_06f6d5d0, NPC lock / unlock =
// FUN_06f6d1c0 / FUN_06f6d220. Even "Everyone" locks for the mission player only.
bool ActLockDock::Execute()
{
    unsigned int const GateId = pub::GetNicknameId(Gate.Data);
    unsigned int const NoPlayer = 0; // DAT_06fb4654
    if (strcmp(Target.Data, "Player") == 0)
    {
        unsigned int const PlayerId = Mission->PlayerId;
        if (Mode == 1)
        {
            RegisterContentGateLock(PlayerId, GateId);
            return true;
        }
        if (Mode == 0)
        {
            UnregisterContentGateLock(&PlayerId, &GateId);
            UnregisterContentGateLock(&NoPlayer, &GateId);
        }
        return true;
    }
    if (strcmp(Target.Data, "Everyone") == 0)
    {
        unsigned int const PlayerId = Mission->PlayerId;
        if (Mode == 1)
        {
            RegisterContentGateLock(PlayerId, GateId);
            RegisterContentNpcGateLock(&GateId);
            return true;
        }
        if (Mode == 0)
        {
            RemoveContentNpcGateLock(&GateId);
            UnregisterContentGateLock(&PlayerId, &GateId);
            UnregisterContentGateLock(&NoPlayer, &GateId);
        }
    }
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_Destroy
// ---------------------------------------------------------------------------------------

// FUN_06ec7f20.
ActDestroy::ActDestroy(FMissionScript* InMission)
    : Act(InMission)
    , DestroyMode(1)
{
    ClearTString(Label);
    Label = "";
}

// LAB_06ec7f90.
void ActDestroy::Parse(INI_Reader& Reader)
{
    Label = Reader.get_value_string(0);
    if (Reader.is_value_empty(1))
        return;
    if (_stricmp(Reader.get_value_string(1), "EXPLODE") == 0)
    {
        DestroyMode = 1;
        return;
    }
    if (_stricmp(Reader.get_value_string(1), "SILENT") == 0)
        DestroyMode = 0;
}

// FUN_06ec8050 (GetLabelObjectId = FUN_06f15b90).
bool ActDestroy::Execute()
{
    unsigned int const ObjectId = Mission->GetLabelObjectId(&Label);
    if ((ObjectId & GNagIdMask) != 0)
        pub::SpaceObj::Destroy(ObjectId, static_cast<DestroyType>(DestroyMode));
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_LightFuse
// ---------------------------------------------------------------------------------------

// FUN_06ec58a0.
ActLightFuse::ActLightFuse(FMissionScript* InMission)
    : Act(InMission)
    , Delay(0.0f)
{
    ClearTString(Label);
    Label = "";
    ClearTString(Fuse);
    Fuse = "";
}

// FUN_06ec5960.
void ActLightFuse::Parse(INI_Reader& Reader)
{
    Label = Reader.get_value_string(0);
    Fuse = Reader.get_value_string(1);
    if (!Reader.is_value_empty(2))
        Delay = Reader.get_value_float(2);
}

// LAB_06ec5a40.
bool ActLightFuse::Execute()
{
    unsigned int const ObjectId = Mission->GetLabelObjectId(&Label);
    if ((ObjectId & GNagIdMask) == 0)
        return false;
    pub::SpaceObj::LightFuse(ObjectId, Fuse.Data, Delay);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_AddAmbient / Act_RemoveAmbient
// ---------------------------------------------------------------------------------------

// FUN_06ec7b50.
ActAddAmbient::ActAddAmbient(FMissionScript* InMission)
    : Act(InMission)
    , Handle(-1)
    , RoomId(0)
    , BaseId(0)
{
    ClearTString(Path);
}

// LAB_06ec7b90.
void ActAddAmbient::Parse(INI_Reader& Reader)
{
    Path = Reader.get_value_string(0);
    pub::GetBaseID(BaseId, Reader.get_value_string(2));
    RoomId = pub::GF::FindLocation(BaseId, Reader.get_value_string(1));
}

// LAB_06ec7c30: the 0x90 bytes at +0x0c are appended to mission +0x310.
bool ActAddAmbient::Execute()
{
    AmbientScriptDescription Desc;
    Desc.RoomId = RoomId;
    Desc.ScriptId = 1;
    Desc.Flags = 0;
    Desc.Name = Path.Data;
    Desc.Filter = Mission->PlayerId;
    Handle = static_cast<int>(pub::GF::AmbientScriptCreate(Desc));
    FMissionAmbientScene Scene;
    Scene.Path = Path;
    Scene.Handle = Handle;
    Scene.RoomId = RoomId;
    Scene.BaseId = BaseId;
    Mission->AmbientScenes.push_back(Scene);
    return true;
}

// FUN_06ece1f0.
ActRemoveAmbient::ActRemoveAmbient(FMissionScript* InMission)
    : Act(InMission)
{
    ClearTString(Path);
}

// FUN_06ec1be0 (shared with Cnd_RTCDone / Act_RemoveRTC).
void ActRemoveAmbient::Parse(INI_Reader& Reader)
{
    Path = Reader.get_value_string(0);
}

// FUN_06ece220: only the first matching scene of +0x310.
bool ActRemoveAmbient::Execute()
{
    std::list<FMissionAmbientScene>::iterator It = Mission->AmbientScenes.begin();
    for (; It != Mission->AmbientScenes.end(); ++It)
    {
        if (_stricmp(It->Path.Data, Path.Data) != 0)
            continue;
        unsigned long SceneHandle = static_cast<unsigned long>(It->Handle);
        pub::GF::AmbientScriptDestroy(&SceneHandle);
        Mission->AmbientScenes.erase(It);
        return true;
    }
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_AddRTC
// ---------------------------------------------------------------------------------------

// FUN_06ec7cd0.
ActAddRtc::ActAddRtc(FMissionScript* InMission)
    : Act(InMission)
    , bRepeatable(0)
{
    ClearTString(Path);
    Path = "";
}

// LAB_06ec7d40.
void ActAddRtc::Parse(INI_Reader& Reader)
{
    Path = Reader.get_value_string(0);
    if (!Reader.is_value_empty(1) && _stricmp(Reader.get_value_string(1), "repeatable") == 0)
        bRepeatable = 1;
}

// LAB_06ec7de0: FUN_06eb8a20 starts the record, which is then appended to mission +0x2f0.
bool ActAddRtc::Execute()
{
    FMissionRtc Record;
    Record.PathLength = Path.Length;
    memcpy(Record.Path, Path.Data, Path.Length + 1);
    Record.bRepeatable = bRepeatable;
    Mission->StartRtcScript(&Record);
    Mission->RtcScripts.push_back(Record);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_PilotParams
// ---------------------------------------------------------------------------------------

namespace
{
    // DAT_06fce300: 10 {name, value} pairs (0x50 bytes), null names skipped.
    struct FFleeModeName
    {
        char const* Name;
        int Value;
    };
    FFleeModeName const GFleeModeNames[10] = {
        {"no_flee", 0}, {"normal_flee", 1}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0},
        {nullptr, 0}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0},
    };
}

// FUN_06ecd3e0 (+0x0c not set).
ActPilotParams::ActPilotParams(FMissionScript* InMission)
    : Act(InMission)
    , FleeMode(0)
{
    ClearTString(Label);
}

// FUN_06ecd410: a miss leaves +0x0c.
void ActPilotParams::Parse(INI_Reader& Reader)
{
    Label = Reader.get_value_string(0);
    char const* const Mode = Reader.get_value_string(1);
    for (int Index = 0; Index < 10; ++Index)
    {
        if (GFleeModeNames[Index].Name == nullptr)
            continue;
        if (_stricmp(Mode, GFleeModeNames[Index].Name) == 0)
        {
            FleeMode = GFleeModeNames[Index].Value;
            return;
        }
    }
}

// FUN_06ecd4c0 (FindNpcByName = FUN_06f15710, SubmitNpcPersonality = FUN_06ebb620).
bool ActPilotParams::Execute()
{
    FContentNpcShip* const Npc = Mission->FindNpcByName(&Label);
    if (Npc != nullptr)
        FMissionScript::SubmitNpcPersonality(Npc, FleeMode);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_PopUpDialog
// ---------------------------------------------------------------------------------------

// FUN_06ecd560.
ActPopUpDialog::ActPopUpDialog(FMissionScript* InMission)
    : Act(InMission)
    , TitleId(1)
    , TextId(1)
    , Buttons(0)
{
}

// LAB_06ecd5a0.
void ActPopUpDialog::Parse(INI_Reader& Reader)
{
    TitleId = static_cast<unsigned int>(Reader.get_value_int(0));
    TextId = static_cast<unsigned int>(Reader.get_value_int(1));
    if (Reader.is_value_empty(2))
        return;
    TString<0x40> Kind;
    ClearTString(Kind);
    Kind = Reader.get_value_string(2);
    if (_stricmp(Kind.Data, "YES_NO") == 0)
    {
        Buttons = 3;
        return;
    }
    if (_stricmp(Kind.Data, "YES_NO_LATER") == 0)
    {
        Buttons = 7;
        return;
    }
    if (_stricmp(Kind.Data, "CLOSE") == 0)
        Buttons = 8;
}

// FUN_06ecd6a0.
bool ActPopUpDialog::Execute()
{
    FmtStr Title;
    Title.begin_mad_lib(TitleId);
    Title.end_mad_lib();
    FmtStr Text;
    Text.begin_mad_lib(TextId);
    Text.end_mad_lib();
    pub::Player::PopUpDialog(Mission->PlayerId, Title, Text, Buttons);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_SetPriority
// ---------------------------------------------------------------------------------------

// FUN_06ecd7d0.
ActSetPriority::ActSetPriority(FMissionScript* InMission)
    : Act(InMission)
    , FormationId(0)
    , Priority(0)
{
}

// FUN_06ecd840.
void ActSetPriority::Parse(INI_Reader& Reader)
{
    FormationId = CreateID(Reader.get_value_string(0));
    if (_stricmp(Reader.get_value_string(1), "NORMAL") == 0)
    {
        Priority = 1;
        return;
    }
    if (_stricmp(Reader.get_value_string(1), "ALWAYS_EXECUTE") == 0)
        Priority = 0;
}

// LAB_06ecd8b0: the upper bound is re-walked each pass (FindNpcById = FUN_06f156b0,
// SetPriority = FUN_06f57c90).
bool ActSetPriority::Execute()
{
    std::multimap<unsigned int, unsigned int>::iterator Member =
        Mission->LabelObjects.lower_bound(FormationId);
    while (Member != Mission->LabelObjects.upper_bound(FormationId))
    {
        FContentNpcShip* const Npc = Mission->FindNpcById(&Member->second);
        if (Npc != nullptr)
        {
            int const NewPriority = Priority;
            Npc->SetPriority(&NewPriority);
        }
        ++Member;
    }
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_SetFlee
// ---------------------------------------------------------------------------------------

// FUN_06ec8b50.
ActSetFlee::ActSetFlee(FMissionScript* InMission)
    : Act(InMission)
    , Mode(2)
    , Position{0.0f, 0.0f, 0.0f}
    , Distance(0.0f)
    , bFlag(0)
{
    ClearTString(Npc);
    ClearTString(Target);
}

// LAB_06ec8b90. SET_POS reads the position in the order 4, 3, 2.
void ActSetFlee::Parse(INI_Reader& Reader)
{
    if (_stricmp(Reader.get_value_string(0), "CLEAR") == 0)
        Mode = 2;
    else if (_stricmp(Reader.get_value_string(0), "SET_POS") == 0)
        Mode = 0;
    else if (_stricmp(Reader.get_value_string(0), "SET_OBJ") == 0)
        Mode = 1;
    Npc = Reader.get_value_string(1);
    if (Mode == 0)
    {
        float const Z = Reader.get_value_float(4);
        float const Y = Reader.get_value_float(3);
        float const X = Reader.get_value_float(2);
        Position.x = X;
        Position.y = Y;
        Position.z = Z;
        Distance = Reader.get_value_float(5);
        bFlag = Reader.get_value_bool(6) ? 1 : 0;
        return;
    }
    if (Mode == 1)
    {
        Target = Reader.get_value_string(2);
        Distance = Reader.get_value_float(3);
        bFlag = Reader.get_value_bool(4) ? 1 : 0;
    }
}

// LAB_06ec8d40: FUN_06f33ea0 / FUN_06f33ee0 / FUN_06f33f40.
bool ActSetFlee::Execute()
{
    FContentNpcShip* const Ship = Mission->FindNpcByName(&Npc);
    if (Ship == nullptr)
        return true;
    if (Mode == 0)
    {
        Ship->SetFleePosition(&Position, &Distance);
        return true;
    }
    if (Mode == 1)
    {
        unsigned int const TargetId = Mission->GetLabelObjectId(&Target);
        if ((TargetId & GNagIdMask) != 0)
            Ship->SetFleeObject(&TargetId, &Distance, bFlag != 0);
        return true;
    }
    if (Mode == 2)
        Ship->ClearFlee();
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_SetLifeTime
// ---------------------------------------------------------------------------------------

// FUN_06ec8dd0.
ActSetLifeTime::ActSetLifeTime(FMissionScript* InMission)
    : Act(InMission)
    , LifeTime(1.0f)
{
    ClearTString(Label);
}

// FUN_06ec8e00.
void ActSetLifeTime::Parse(INI_Reader& Reader)
{
    Label = Reader.get_value_string(0);
    LifeTime = Reader.get_value_float(1);
}

// LAB_06ec8e70: the named NPC, else every NPC of +0xa4 carrying the label (+0x17c list).
// Longevity / LifeTime are NPC +0x1bc / +0x1c8.
bool ActSetLifeTime::Execute()
{
    FContentNpcShip* const Named = Mission->FindNpcByName(&Label);
    if (Named != nullptr)
    {
        Named->Longevity = LifeTime;
        Named->LifeTime = LifeTime;
        return true;
    }
    unsigned int const LabelId = CreateID(Label.Data);
    std::map<unsigned int, FContentNpcShip*>::iterator It = Mission->NpcObjects.begin();
    for (; It != Mission->NpcObjects.end(); ++It)
    {
        FContentNpcShip* const Ship = It->second;
        std::list<unsigned int>::iterator LabelIt = Ship->LabelIds.begin();
        for (; LabelIt != Ship->LabelIds.end(); ++LabelIt)
        {
            if (*LabelIt == LabelId)
            {
                Ship->Longevity = LifeTime;
                Ship->LifeTime = LifeTime;
                break;
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_GiveMB / Act_GiveObjList
// ---------------------------------------------------------------------------------------

// FUN_06ecac70.
ActGiveMb::ActGiveMb(FMissionScript* InMission)
    : Act(InMission)
    , ListId(0)
{
    ClearTString(Npc);
}

// FUN_06ecae40.
void ActGiveMb::Parse(INI_Reader& Reader)
{
    Npc = Reader.get_value_string(0);
    ListId = CreateID(Reader.get_value_string(1));
}

// LAB_06ecace0 (the mission's +0x114 [MetaBehavior] map). On an empty list retail still
// writes +0x0c through the head node's (unset) value slot; the port skips both writes.
bool ActGiveMb::Execute()
{
    FContentNpcShip* const Ship = Mission->FindNpcByName(&Npc);
    if (Ship == nullptr)
        return true;
    std::map<unsigned int, FMissionMetaBehaviorList>::iterator Meta =
        Mission->MetaBehaviors.find(ListId);
    if (Meta == Mission->MetaBehaviors.end())
        return true;
    Ship->CancelDirectives();                                            // FUN_06f57740
    std::list<FMetaBehavior*>& Items = Meta->second.Items;
    if (Items.empty())
        return true;
    FMetaBehavior* const First = Items.front();
    First->Ship = Ship;
    Ship->SetGuideFromDesc(First, true);                                 // FUN_06f34af0
    First->Ship = nullptr;
    return true;
}

// FUN_06ecad90.
ActGiveObjList::ActGiveObjList(FMissionScript* InMission)
    : Act(InMission)
    , ListId(0)
{
    ClearTString(Npc);
    Npc = "";
}

// FUN_06ecae40.
void ActGiveObjList::Parse(INI_Reader& Reader)
{
    Npc = Reader.get_value_string(0);
    ListId = CreateID(Reader.get_value_string(1));
}

// LAB_06ecaec0 (the mission's +0x100 [ObjList] map). SetGuide = FUN_06f34b70, SetDirectives =
// FUN_06f57840.
bool ActGiveObjList::Execute()
{
    if (FindContentPlayerState(Mission->PlayerId) == nullptr)
        return true;
    FContentNpcShip* const Named = Mission->FindNpcByName(&Npc);
    FContentShipBase* Ship = Named;
    if (Ship == nullptr)
    {
        if (_stricmp("Player", Npc.Data) != 0)
            return true;
        Ship = Mission->PlayerState->ShipObject;
        if (Ship == nullptr)
            return true;
    }
    std::map<unsigned int, FMissionDirectiveList>::iterator It = Mission->ObjLists.find(ListId);
    if (It == Mission->ObjLists.end())
        return true;
    // Only an NPC (the player's own ship id matches) reaches the guide call.
    if (Ship->SpaceObjId != Mission->PlayerState->GetShipId())
        static_cast<FContentNpcShip*>(Ship)->SetGuide(nullptr, true, false);
    Ship->SetDirectives(&It->second.Items, 0);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_Invulnerable
// ---------------------------------------------------------------------------------------

namespace
{
    // DAT_06fcefcc (static initializer LAB_06eb7580).
    unsigned int const GPlayerLabelId = CreateID("Player");
}

// FUN_06ecb000.
ActInvulnerable::ActInvulnerable(FMissionScript* InMission)
    : Act(InMission)
    , bInvulnerable(0)
    , bPlayerCanDamage(0)
    , MaxHpLoss(0.0f)
{
    ClearTString(Label);
}

// FUN_06ecb030.
void ActInvulnerable::Parse(INI_Reader& Reader)
{
    Label = Reader.get_value_string(0);
    bInvulnerable = Reader.get_value_bool(1) ? 1 : 0;
    if (!Reader.is_value_empty(2))
        bPlayerCanDamage = Reader.get_value_bool(2) ? 1 : 0;
    if (!Reader.is_value_empty(3))
        MaxHpLoss = Reader.get_value_float(3);
}

// LAB_06ecb0d0. Formation members are tracked in the [Invulnerables] list +0x14c by mission
// name (FUN_06f15c70, first hit); the upper bound is re-taken each pass. An empty range falls
// back to a system object of that name (FUN_06f73ac0).
bool ActInvulnerable::Execute()
{
    unsigned int const Key = CreateID(Label.Data);
    if (Key == GPlayerLabelId)
    {
        if (FindContentPlayerState(Mission->PlayerId) == nullptr)
            return true;
        pub::SpaceObj::SetInvincible(Mission->PlayerState->GetShipId(), bInvulnerable != 0,
                                     bPlayerCanDamage != 0, MaxHpLoss);
        return true;
    }

    std::multimap<unsigned int, unsigned int>::iterator Member =
        Mission->LabelObjects.lower_bound(Key);
    if (Member == Mission->LabelObjects.upper_bound(Key))
    {
        if (FindContentPlayerState(Mission->PlayerId) == nullptr)
            return true;
        FSystemInfo* const System = Mission->PlayerState->GetSystemRecord();
        if (System == nullptr)
            return true;
        unsigned int const ObjectId = System->FindObjectIdByName(&Label, Mission->ControllerId);
        if ((ObjectId & GNagIdMask) != 0)
            pub::SpaceObj::SetInvincible(ObjectId, bInvulnerable != 0, bPlayerCanDamage != 0, MaxHpLoss);
        return true;
    }

    do
    {
        int const Result = pub::SpaceObj::SetInvincible(Member->second, bInvulnerable != 0,
                                                        bPlayerCanDamage != 0, MaxHpLoss);
        TString<0x30> const Name = Mission->GetMissionObjectName(&Member->second);
        bool bListed = false;
        std::list<FMissionInvulnerable>::iterator Listed = Mission->Invulnerables.begin();
        for (; Listed != Mission->Invulnerables.end(); ++Listed)
        {
            if (strcmp(Listed->Nickname.Data, Name.Data) == 0)
            {
                bListed = true;
                break;
            }
        }
        if (bInvulnerable != 0)
        {
            if (!bListed)
            {
                FMissionInvulnerable Record;
                Record.Nickname = Name;
                Record.MaxHpLoss = MaxHpLoss;
                Record.bPlayerCanDamage = bPlayerCanDamage;
                Mission->Invulnerables.push_back(Record);
            }
        }
        else if (bListed)
        {
            Mission->Invulnerables.erase(Listed);
        }
        // Retail makes the lookup (FUN_06f156b0) and drops the result.
        if (Result != 0)
            (void)Mission->FindNpcById(&Member->second);
        ++Member;
    } while (Member != Mission->LabelObjects.upper_bound(Key));
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_MarkObj
// ---------------------------------------------------------------------------------------

// FUN_06ec5fc0.
ActMarkObj::ActMarkObj(FMissionScript* InMission)
    : Act(InMission)
    , Mode(0)
{
    ClearTString(Label);
    Label = "";
}

// FUN_06ec6050.
void ActMarkObj::Parse(INI_Reader& Reader)
{
    Label = Reader.get_value_string(0);
    Mode = Reader.get_value_int(1);
}

// LAB_06ec60c0 (MarkMissionObject = FUN_06f199f0).
// Lower bound == end: retail guards the call by testing the end iterator's mapped word, the
// multimap head node's uninitialised value (a retail bug: heap garbage); on zero it returns
// false without the call. The port takes the nonzero case, which puts the name on the
// [MarkObj] list so a later-spawned NPC is marked on creation (FUN_06f0f150).
// Otherwise each member's name comes from the NPC, solar or loot lookup; an unset member id
// makes the result false.
bool ActMarkObj::Execute()
{
    bool bResult = true;
    unsigned int const Key = CreateID(Label.Data);
    std::multimap<unsigned int, unsigned int>::iterator Member =
        Mission->LabelObjects.lower_bound(Key);
    if (Member == Mission->LabelObjects.end())
    {
        unsigned int const ObjectId = Mission->GetLabelObjectId(&Label);
        Mission->MarkMissionObject(ObjectId, Mode, Label);
        return true;
    }
    if (Member == Mission->LabelObjects.upper_bound(Key))
        return true;

    do
    {
        TString<0x30> Name;
        ClearTString(Name);
        FContentNpcShip* const Npc = Mission->FindNpcById(&Member->second);
        if (Npc != nullptr)
        {
            Name = Npc->Nickname.Data;
        }
        else
        {
            FMissionSolar* const Solar = Mission->FindSolarById(&Member->second);
            if (Solar != nullptr)
            {
                Name = Solar->Nickname.Data;
            }
            else
            {
                FMissionLoot* const Loot = Mission->FindLootById(&Member->second);
                if (Loot != nullptr)
                    Name = Loot->Nickname.Data;
            }
        }
        if ((Member->second & GNagIdMask) != 0)
            Mission->MarkMissionObject(Member->second, Mode, Name);
        else
            bResult = false;
        ++Member;
    } while (Member != Mission->LabelObjects.upper_bound(Key));
    return bResult;
}

// ---------------------------------------------------------------------------------------
// Act_Jumper
// ---------------------------------------------------------------------------------------

// FUN_06ec5a90.
ActJumper::ActJumper(FMissionScript* InMission)
    : Act(InMission)
    , LabelId(0)
    , bJumper(0)
{
}

// LAB_06ec5b00.
void ActJumper::Parse(INI_Reader& Reader)
{
    LabelId = CreateID(Reader.get_value_string(0));
    bJumper = Reader.get_value_bool(1) ? 1 : 0;
}

// LAB_06ec5b40: every labelled NPC with a [MsnShip] slot (slot +0x48); jumpers are tracked in
// mission +0x1c (FindJumperNpc = FUN_06f14980, RemoveJumperNpc = FUN_06f14e30).
bool ActJumper::Execute()
{
    std::map<unsigned int, FContentNpcShip*>::iterator It = Mission->NpcObjects.begin();
    for (; It != Mission->NpcObjects.end(); ++It)
    {
        FContentNpcShip* const Npc = It->second;
        std::list<unsigned int>::iterator LabelIt = Npc->LabelIds.begin();
        while (LabelIt != Npc->LabelIds.end() && *LabelIt != LabelId)
            ++LabelIt;
        if (LabelIt == Npc->LabelIds.end())
            continue;
        FMissionShip* const Ship = Mission->FindMissionShip(&Npc->Nickname);
        if (Ship == nullptr)
            continue;
        Ship->bJumper = bJumper;
        if (bJumper != 0)
        {
            TString<0x30> Name;
            ClearTString(Name);
            Name = Npc->Nickname.Data;
            if (Mission->FindJumperNpc(&Name) == nullptr)
            {
                FMissionJumper Jumper;
                Jumper.Npc = Npc;
                Jumper.State = 0;
                Mission->Jumpers.push_back(Jumper);
            }
        }
        else
        {
            unsigned int const ObjectId = Npc->SpaceObjId;
            Mission->RemoveJumperNpc(&ObjectId);
        }
    }
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_SendComm
// ---------------------------------------------------------------------------------------

// FUN_06ec4910. Retail does not write the costume's accessory slots; the port zeroes them.
ActSendComm::ActSendComm(FMissionScript* InMission)
    : Act(InMission)
    , TextId(0)
    , VoiceId(0)
    , CaptionIds(0)
    , Duration(-1)
{
    ClearTString(Sender);
    Sender = "";
    ClearTString(Receiver);
    Receiver = "";
    memset(&SpeakerCostume, 0, sizeof(SpeakerCostume));
}

// LAB_06ec49f0. The accessory count has no bound (as retail). A "Player" receiver adds the
// costume, or without slot 6 the sender's (FUN_06f155f0), to mission +0x31c.
void ActSendComm::Parse(INI_Reader& Reader)
{
    Sender = Reader.get_value_string(0);
    Receiver = Reader.get_value_string(1);
    TextId = pub::GetNicknameId(Reader.get_value_string(2));
    if (!Reader.is_value_empty(3))
    {
        int const Value = Reader.get_value_int(3);
        Duration = Value;
        if (Value < -1 || Value == 0 || Value > 10)
            Duration = -1;
    }
    if (!Reader.is_value_empty(4))
    {
        (void)Reader.is_value_empty(7);
        pub::GetVoiceID(VoiceId, Reader.get_value_string(4));
        if (!Reader.is_value_empty(5))
            CaptionIds = static_cast<unsigned int>(Reader.get_value_int(5));
        if (!Reader.is_value_empty(6))
        {
            if (_stricmp(Reader.get_value_string(6), "NO_HEAD") != 0)
                SpeakerCostume.PartIds[0] =
                    static_cast<unsigned int>(pub::GF::GetBodyPart(Reader.get_value_string(6), 1));
            else
                SpeakerCostume.PartIds[0] = 0;
            SpeakerCostume.PartIds[1] =
                static_cast<unsigned int>(pub::GF::GetBodyPart(Reader.get_value_string(7), 0));
            int Index = 8;
            while (!Reader.is_value_empty(Index))
            {
                SpeakerCostume.AccessoryIds[SpeakerCostume.AccessoryCount] =
                    static_cast<unsigned int>(pub::GF::GetAccessory(Reader.get_value_string(Index)));
                ++SpeakerCostume.AccessoryCount;
                ++Index;
            }
            if (_stricmp(Receiver.Data, "Player") == 0)
                AddDialogCostume(Mission, &SpeakerCostume);
        }
    }
    if (Reader.is_value_empty(6) && _stricmp(Receiver.Data, "Player") == 0)
    {
        Costume* const NpcCostume = Mission->GetMissionCostume(&Sender);
        if (NpcCostume != nullptr)
            AddDialogCostume(Mission, NpcCostume);
    }
}

// LAB_06ec4cf0 (SendComm = FUN_06f72930). Retail's 1.0f duration needs the receiver to be the
// player's ship with a masked id of 0, which is kept as is.
bool ActSendComm::Execute()
{
    bool bResult = true;
    unsigned int const SenderId = Mission->GetLabelObjectId(&Sender);
    bool bAnyone = false;
    if (_stricmp(Receiver.Data, "anyone") == 0)
        bAnyone = true;
    unsigned int const ReceiverId = Mission->GetLabelObjectId(&Receiver);
    unsigned int const SenderMasked = SenderId & GNagIdMask;
    if (SenderMasked == 0)
        bResult = false;
    unsigned int const ReceiverMasked = ReceiverId & GNagIdMask;
    if (ReceiverMasked == 0 && !bAnyone)
        bResult = false;
    if (FindContentPlayerState(Mission->PlayerId) == nullptr)
        return bResult;
    if (SenderMasked == 0)
        return bResult;
    if (ReceiverMasked == 0 && !bAnyone)
        return bResult;
    FSystemInfo* const System = Mission->PlayerState->GetSystemRecord();
    if (System == nullptr)
        return bResult;

    float CommDuration = 0.5f;
    if (Duration != -1)
        CommDuration = static_cast<float>(Duration);
    else if (ReceiverId == Mission->PlayerState->GetShipId() && ReceiverMasked == 0)
        CommDuration = 1.0f;

    if (SpeakerCostume.PartIds[0] != 0)
        System->SendComm(SenderId, ReceiverId, TextId, VoiceId, CaptionIds, &SpeakerCostume,
                         CommDuration);
    else
        System->SendComm(SenderId, ReceiverId, TextId, 0, 0, nullptr, CommDuration);
    return bResult;
}

// ---------------------------------------------------------------------------------------
// Act_EtherComm
// ---------------------------------------------------------------------------------------

// FUN_06ec8730. Retail does not write the costume's accessory slots; the port zeroes them.
ActEtherComm::ActEtherComm(FMissionScript* InMission)
    : Act(InMission)
    , VoiceId(0)
    , CaptionIds(0)
    , TextId(0)
    , Duration(-1)
{
    ClearTString(Receiver);
    Receiver = "";
    memset(&SpeakerCostume, 0, sizeof(SpeakerCostume));
}

// LAB_06ec87c0. The accessory count has no bound (as retail). AddDialogCostume: FUN_06eb8ef0
// scan, FUN_06ed23d0 insert into mission +0x31c.
void ActEtherComm::Parse(INI_Reader& Reader)
{
    pub::GetVoiceID(VoiceId, Reader.get_value_string(0));
    CaptionIds = static_cast<unsigned int>(Reader.get_value_int(1));
    Receiver = Reader.get_value_string(2);
    TextId = pub::GetNicknameId(Reader.get_value_string(3));
    int const Value = Reader.get_value_int(4);
    Duration = Value;
    if (Value < -1 || Value == 0 || Value > 10)
        Duration = -1;
    if (_stricmp(Reader.get_value_string(5), "NO_HEAD") != 0)
        SpeakerCostume.PartIds[0] =
            static_cast<unsigned int>(pub::GF::GetBodyPart(Reader.get_value_string(5), 1));
    else
        SpeakerCostume.PartIds[0] = 0;
    SpeakerCostume.PartIds[1] =
        static_cast<unsigned int>(pub::GF::GetBodyPart(Reader.get_value_string(6), 0));
    int Index = 7;
    while (!Reader.is_value_empty(Index))
    {
        SpeakerCostume.AccessoryIds[SpeakerCostume.AccessoryCount] =
            static_cast<unsigned int>(pub::GF::GetAccessory(Reader.get_value_string(Index)));
        ++SpeakerCostume.AccessoryCount;
        ++Index;
    }
    AddDialogCostume(Mission, &SpeakerCostume);
}

// LAB_06ec8990 (GetLabelShipId = FUN_06f15940).
bool ActEtherComm::Execute()
{
    unsigned int const ReceiverId = Mission->GetLabelShipId(&Receiver);
    float CommDuration = 0.5f;
    if (Duration != -1)
    {
        CommDuration = static_cast<float>(Duration);
    }
    else if (FindContentPlayerState(Mission->PlayerId) != nullptr &&
             ReceiverId == Mission->PlayerState->GetShipId() && (ReceiverId & GNagIdMask) == 0)
    {
        CommDuration = 1.0f;
    }
    pub::SpaceObj::SendComm(0, ReceiverId, VoiceId, &SpeakerCostume, CaptionIds, &TextId, 1, 0,
                            CommDuration, true);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_SetOrient
// ---------------------------------------------------------------------------------------

// FUN_06ec93a0. Retail leaves +0x40..+0x4c unset; the port zeroes them.
ActSetOrient::ActSetOrient(FMissionScript* InMission)
    : Act(InMission)
{
    ClearTString(Label);
    Orientation.W = 0.0f;
    Orientation.X = 0.0f;
    Orientation.Y = 0.0f;
    Orientation.Z = 0.0f;
}

// FUN_06ec93d0.
void ActSetOrient::Parse(INI_Reader& Reader)
{
    Label = Reader.get_value_string(0);
    Orientation.W = Reader.get_value_float(1);
    Orientation.X = Reader.get_value_float(2);
    Orientation.Y = Reader.get_value_float(3);
    Orientation.Z = Reader.get_value_float(4);
}

// LAB_06ec9470 (RelocateShip = FUN_06f67740).
bool ActSetOrient::Execute()
{
    unsigned int const ObjectId = Mission->GetLabelObjectId(&Label);
    if ((ObjectId & GNagIdMask) == 0)
        return false;
    Vector Position;
    Matrix CurrentOrientation;
    pub::SpaceObj::GetLocation(ObjectId, Position, CurrentOrientation);
    if (FindContentPlayerState(Mission->PlayerId) == nullptr)
        return true;
    FSystemInfo* const System = Mission->PlayerState->GetSystemRecord();
    if (System == nullptr)
        return true;
    if (GTriggerMathEngine == nullptr)
        GTriggerMathEngine = ContentAcquire3DMathEngine();
    FMatrix Rotation;
    GTriggerMathEngine->QuaternionToMatrix(Rotation, Orientation);
    Matrix NewOrientation;
    memcpy(&NewOrientation, &Rotation, sizeof(NewOrientation));
    unsigned int const SystemId = System->SystemId;
    RelocateShip(&ObjectId, &SystemId, &Position, &NewOrientation);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_RelocateForm / Act_RelocateShip
// ---------------------------------------------------------------------------------------

namespace
{
    // The unit quaternion of FUN_06ecdd30 / FUN_06ece9b0: 1.0f (DAT_06fb5810) over the root of
    // the sum of squares (x87: W*W + X*X + Y*Y + Z*Z in that order), each part times it.
    FQuaternion NormalizeQuaternion(float W, float X, float Y, float Z)
    {
        double const Scale = 1.0 / std::sqrt(static_cast<double>(W) * W +
                                              static_cast<double>(X) * X +
                                              static_cast<double>(Y) * Y +
                                              static_cast<double>(Z) * Z);
        FQuaternion Result;
        Result.W = static_cast<float>(Scale * W);
        Result.X = static_cast<float>(Scale * X);
        Result.Y = static_cast<float>(Scale * Y);
        Result.Z = static_cast<float>(Scale * Z);
        return Result;
    }

    // FUN_06ecdd30 (shared Parse of Act_RelocateForm / Act_RelocateShip). Values are read from
    // the highest slot down.
    void ParseRelocation(INI_Reader& Reader, TString<0x30>* Label, FTransform* Placement)
    {
        *Label = Reader.get_value_string(0);
        float const Z = Reader.get_value_float(3);
        float const Y = Reader.get_value_float(2);
        Placement->Translation.X = Reader.get_value_float(1);
        Placement->Translation.Z = Z;
        Placement->Translation.Y = Y;
        if (Reader.is_value_empty(4))
            return;
        float const QuatZ = Reader.get_value_float(7);
        float const QuatY = Reader.get_value_float(6);
        float const QuatX = Reader.get_value_float(5);
        float const QuatW = Reader.get_value_float(4);
        FQuaternion const Rotation = NormalizeQuaternion(QuatW, QuatX, QuatY, QuatZ);
        if (GTriggerMathEngine == nullptr)
            GTriggerMathEngine = ContentAcquire3DMathEngine();
        FMatrix Result;
        GTriggerMathEngine->QuaternionToMatrix(Result, Rotation);
        Placement->Rotation = Result;
    }

    // The identity rotation and zero position both relocation ctors store (+0x40..+0x6c).
    void SetIdentityPlacement(FTransform* Placement)
    {
        memset(Placement, 0, sizeof(*Placement));
        Placement->Rotation.D[0][0] = 1.0f;
        Placement->Rotation.D[1][1] = 1.0f;
        Placement->Rotation.D[2][2] = 1.0f;
    }
}

// FUN_06ecdce0.
ActRelocateForm::ActRelocateForm(FMissionScript* InMission)
    : Act(InMission)
{
    ClearTString(Label);
    SetIdentityPlacement(&Placement);
}

// FUN_06ecdd30.
void ActRelocateForm::Parse(INI_Reader& Reader)
{
    ParseRelocation(Reader, &Label, &Placement);
}

// FUN_06ecdea0 (CollectFormationMembers = FUN_06f66220, GetSlotTransform = FUN_06efcbe0).
// GetSlotTransform's result is not tested; the port's outputs start at zero where retail's
// are indeterminate.
bool ActRelocateForm::Execute()
{
    FContentNpcShip* const Npc = Mission->FindNpcByName(&Label);
    if (Npc == nullptr)
        return false;
    if (FindContentPlayerState(Mission->PlayerId) == nullptr)
        return false;
    std::vector<FContentShipBase*> Members;
    CollectFormationMembers(&Members, Npc->FormationId);
    FFormation const* const Formation = Npc->Formation;
    std::vector<FContentShipBase*>::iterator It = Members.begin();
    for (; It != Members.end(); ++It)
    {
        FContentShipBase* const Member = *It;
        FVector SlotPosition;
        SlotPosition.X = 0.0f;
        SlotPosition.Y = 0.0f;
        SlotPosition.Z = 0.0f;
        FMatrix SlotOrientation;
        memset(&SlotOrientation, 0, sizeof(SlotOrientation));
        Formation->GetSlotTransform(Member->FormationSlot, &SlotPosition, &SlotOrientation);
        if (GTriggerMathEngine == nullptr)
            GTriggerMathEngine = ContentAcquire3DMathEngine();
        FVector WorldPosition;
        GTriggerMathEngine->TransformXform(WorldPosition, Placement, SlotPosition);
        if (GTriggerMathEngine == nullptr)
            GTriggerMathEngine = ContentAcquire3DMathEngine();
        FMatrix WorldRotation;
        GTriggerMathEngine->MulMatrix(WorldRotation, Placement.Rotation, SlotOrientation);
        Vector Position;
        Position.x = WorldPosition.X;
        Position.y = WorldPosition.Y;
        Position.z = WorldPosition.Z;
        Matrix Orientation;
        memcpy(&Orientation, &WorldRotation, sizeof(Orientation));
        unsigned int const SystemId = Mission->PlayerState->GetSystemRecord()->SystemId;
        unsigned int const ShipId = Member->SpaceObjId;
        RelocateShip(&ShipId, &SystemId, &Position, &Orientation);
    }
    return true;
}

// FUN_06ece060.
ActRelocateShip::ActRelocateShip(FMissionScript* InMission)
    : Act(InMission)
{
    SetIdentityPlacement(&Placement);
    ClearTString(Label);
    Label = "";
}

// FUN_06ecdd30.
void ActRelocateShip::Parse(INI_Reader& Reader)
{
    ParseRelocation(Reader, &Label, &Placement);
}

// LAB_06ece100. The player's system record is not tested.
bool ActRelocateShip::Execute()
{
    unsigned int const ObjectId = Mission->GetLabelObjectId(&Label);
    if ((ObjectId & GNagIdMask) == 0)
        return false;
    if (FindContentPlayerState(Mission->PlayerId) == nullptr)
        return true;
    unsigned int const SystemId = Mission->PlayerState->GetSystemRecord()->SystemId;
    Vector Position;
    Position.x = Placement.Translation.X;
    Position.y = Placement.Translation.Y;
    Position.z = Placement.Translation.Z;
    Matrix Orientation;
    memcpy(&Orientation, &Placement.Rotation, sizeof(Orientation));
    RelocateShip(&ObjectId, &SystemId, &Position, &Orientation);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_RemoveCargo
// ---------------------------------------------------------------------------------------

namespace
{
    // The pub::CargoEnumerator of LAB_06ece350 (slot 0 LAB_06ece400): the first matching item.
    struct FCargoGoodFinder : public pub::CargoEnumerator
    {
        void notify_cargo(unsigned int const& InGoodId, int Count, unsigned short Id) override
        {
            (void)Count;
            if (GoodId != InGoodId || bFound != 0)
                return;
            SubObjId = Id;
            bFound = 1;
        }

        unsigned int GoodId = 0;          // +0x04
        unsigned char bFound = 0;         // +0x08
        unsigned short SubObjId = 0;      // +0x0a (SUBOBJ_ID_NONE)
    };
}

// FUN_06ece2a0.
ActRemoveCargo::ActRemoveCargo(FMissionScript* InMission)
    : Act(InMission)
    , Count(1)
{
    ClearTString(Loot);
}

// FUN_06ece2d0.
void ActRemoveCargo::Parse(INI_Reader& Reader)
{
    Loot = Reader.get_value_string(0);
    if (!Reader.is_value_empty(1))
        Count = Reader.get_value_int(1);
}

// LAB_06ece350 (GetLootId = FUN_06f15b10, FindLootById = FUN_06f15ae0).
bool ActRemoveCargo::Execute()
{
    unsigned int LootId = Mission->GetLootId(&Loot);
    FMissionLoot* const Record = Mission->FindLootById(&LootId);
    if (Record == nullptr)
        return true;
    unsigned int GoodId = 0;
    pub::TranslateArchToGood(Record->Info.EquipArchID, GoodId);
    FCargoGoodFinder Finder;
    Finder.GoodId = GoodId;
    Finder.bFound = 0;
    Finder.SubObjId = SUBOBJ_ID_NONE;
    pub::Player::EnumerateCargo(Mission->PlayerId, Finder);
    if ((Finder.SubObjId & GNagIdMask) == 0)
        return true;
    pub::Player::RemoveCargo(Mission->PlayerId, Finder.SubObjId,
                             static_cast<unsigned int>(Count));
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_RepChangeRequest
// ---------------------------------------------------------------------------------------

// FUN_06ece430.
ActRepChangeRequest::ActRepChangeRequest(FMissionScript* InMission)
    : Act(InMission)
    , bShown(0)
    , bArmed(0)
    , SystemId(0)
{
}

// LAB_06ece4c0: at most 49 groups.
void ActRepChangeRequest::Parse(INI_Reader& Reader)
{
    pub::GetSystemID(SystemId, Reader.get_value_string(0));
    int Index = 1;
    if (!Reader.is_value_empty(Index))
    {
        while (Index < 0x32)
        {
            unsigned int Group = static_cast<unsigned int>(-1);
            pub::Reputation::GetReputationGroup(Group, Reader.get_value_string(Index));
            std::vector<unsigned int>::iterator It = Groups.begin();
            while (It != Groups.end() && *It != Group)
                ++It;
            if (It == Groups.end())
                Groups.push_back(Group);
            ++Index;
            if (Reader.is_value_empty(Index))
                break;
        }
    }
    bArmed = 1;
}

// LAB_06ece590 (mission +0x2cc..+0x2d4).
bool ActRepChangeRequest::Execute()
{
    Mission->bRepPopUpShown = bShown;
    Mission->bRepPopUpArmed = bArmed;
    Mission->RepPopUpSystemId = SystemId;
    Mission->RepPopUpGroups = Groups;
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_StaticCam
// ---------------------------------------------------------------------------------------

// FUN_06ece960.
ActStaticCam::ActStaticCam(FMissionScript* InMission)
    : Act(InMission)
    , Fov(0.0f)
    , Aspect(0.0f)
{
    memset(&Camera, 0, sizeof(Camera));
    Camera.Rotation.E[0][0] = 1.0f;
    Camera.Rotation.E[1][1] = 1.0f;
    Camera.Rotation.E[2][2] = 1.0f;
}

// FUN_06ece9b0. The quaternion is read from slot 6 down.
void ActStaticCam::Parse(INI_Reader& Reader)
{
    Camera.Translation.x = Reader.get_value_float(0);
    Camera.Translation.y = Reader.get_value_float(1);
    Camera.Translation.z = Reader.get_value_float(2);
    float const QuatZ = Reader.get_value_float(6);
    float const QuatY = Reader.get_value_float(5);
    float const QuatX = Reader.get_value_float(4);
    float const QuatW = Reader.get_value_float(3);
    FQuaternion const Rotation = NormalizeQuaternion(QuatW, QuatX, QuatY, QuatZ);
    if (GTriggerMathEngine == nullptr)
        GTriggerMathEngine = ContentAcquire3DMathEngine();
    FMatrix Result;
    GTriggerMathEngine->QuaternionToMatrix(Result, Rotation);
    memcpy(&Camera.Rotation, &Result, sizeof(Camera.Rotation));
    if (!Reader.is_value_empty(7))
        Fov = Reader.get_value_float(7);
    if (!Reader.is_value_empty(8))
        Aspect = Reader.get_value_float(8);
}

// FUN_06eceae0: the same cutscene clamps as Act_CallThorn.
bool ActStaticCam::Execute()
{
    if (pub::Player::SetCamera(Mission->PlayerId, Camera, Fov, Aspect) != 0)
        return true;
    if (FindContentPlayerState(Mission->PlayerId) == nullptr)
        return true;
    Mission->bCutsceneActive = 1;
    unsigned int const ShipId = Mission->PlayerState->GetShipId();
    if ((ShipId & GNagIdMask) != 0)
        pub::SpaceObj::SetInvincible(ShipId, true, false, 0.0f);
    Mission->NagClamp = 1;
    std::map<unsigned int, FContentNpcShip*>::iterator It = Mission->NpcObjects.begin();
    for (; It != Mission->NpcObjects.end(); ++It)
        It->second->SetCommMuted(true);
    SpacePopSetGcsClampEnabled(1);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_NNPath
// ---------------------------------------------------------------------------------------

namespace
{
    // The pub::System::SysObjEnumerator of LAB_06ecc420 (slot 0 FUN_06ecc5c0).
    struct FNnPathObjectFinder : public pub::System::SysObjEnumerator
    {
        bool OnObject(pub::System::FSysObj const* Object) override
        {
            if (bFound != 0)
                return true;
            if (_stricmp(Object->Nickname, Label.Data) != 0)
                return true;
            Position = Object->Pos;
            bFound = 1;
            return false;
        }

        TString<0x30> Label;              // +0x04
        Vector Position;                  // +0x38
        unsigned char bFound = 0;         // +0x44
    };
}

// FUN_06ecc3f0. Retail leaves +0x0c and +0x50/+0x54 unset; the port zeroes them.
ActNnPath::ActNnPath(FMissionScript* InMission)
    : Act(InMission)
    , Position{0.0f, 0.0f, 0.0f}
    , SystemId(0)
    , TravelIds(0)
    , TargetIds(0)
{
    ClearTString(Label);
}

// LAB_06ecc420: a position form (slots 2..5) or a label form (s2, system s3).
void ActNnPath::Parse(INI_Reader& Reader)
{
    TravelIds = Reader.get_value_int(0);
    TargetIds = Reader.get_value_int(1);
    if (!Reader.is_value_empty(4))
    {
        Position.x = Reader.get_value_float(2);
        Position.y = Reader.get_value_float(3);
        Position.z = Reader.get_value_float(4);
        if (!Reader.is_value_empty(5))
            pub::GetSystemID(SystemId, Reader.get_value_string(5));
        return;
    }
    if (Reader.is_value_empty(2))
        return;
    Label = Reader.get_value_string(2);
    if (Reader.is_value_empty(3))
        return;
    pub::GetSystemID(SystemId, Reader.get_value_string(3));
    FNnPathObjectFinder Finder;
    Finder.Label = Label;
    Finder.bFound = 0;
    if (pub::System::EnumerateObjects(SystemId, Finder) != 0)
        return;
    if (Finder.bFound != 0)
    {
        Position = Finder.Position;
        return;
    }
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
}

// LAB_06ecc610 (the mission's NN path +0x21c; Reset = FUN_06f1c990).
bool ActNnPath::Execute()
{
    if (FindContentPlayerState(Mission->PlayerId) == nullptr)
        return false;
    FSystemInfo* const System = Mission->PlayerState->GetSystemRecord();
    if (System == nullptr)
        return false;
    FMissionNnPath& Path = Mission->NnPath;
    Path.Reset();
    Path.Position = Position;
    Path.Mission = Mission;
    Path.TravelIds = TravelIds;
    Path.TargetIds = TargetIds;
    Path.SystemId = (SystemId & GNagIdMask) != 0 ? SystemId : System->SystemId;
    if (Label.Length != 0)
        Path.Label = Label.Data;
    Path.State = 1;
    Path.bActive = 1;
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_PlayerForm
// ---------------------------------------------------------------------------------------

// FUN_06eccec0. Retail leaves +0x40 unset; the port starts it null.
ActPlayerForm::ActPlayerForm(FMissionScript* InMission)
    : Act(InMission)
    , Formation(nullptr)
{
    ClearTString(Npc);
    Npc = "";
}

// FUN_06eccf30 (FindFormation = FUN_06efd1a0).
void ActPlayerForm::Parse(INI_Reader& Reader)
{
    unsigned int const FormationId = CreateID(Reader.get_value_string(0));
    Formation = FindFormation(&FormationId);
    Npc = Reader.get_value_string(1);
}

// LAB_06eccfc0 (FormFormation = FUN_06f66ec0). Both ships are appended null or not.
bool ActPlayerForm::Execute()
{
    if (FindContentPlayerState(Mission->PlayerId) == nullptr)
        return true;
    std::list<FContentShipBase*> Ships;
    Ships.push_back(Mission->PlayerState->ShipObject);
    Ships.push_back(Mission->FindNpcByName(&Npc));
    FormFormation(&Ships, Formation, false);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_SetRep / Act_SetShipAndLoadout
// ---------------------------------------------------------------------------------------

// FUN_06ec9620.
ActSetRep::ActSetRep(FMissionScript* InMission)
    : Act(InMission)
    , GroupId(0)
    , Value(0.0f)
{
    ClearTString(Label);
    ClearTString(Faction);
    Faction = "";
}

// FUN_06ec96a0: a number, else a REP_* name (FUN_06ec9530).
void ActSetRep::Parse(INI_Reader& Reader)
{
    Label = Reader.get_value_string(0);
    Faction = Reader.get_value_string(1);
    pub::Reputation::GetReputationGroup(GroupId, Reader.get_value_string(1));
    if (Reader.is_number(Reader.get_value_string(2)))
    {
        Value = Reader.get_value_float(2);
        return;
    }
    TString<0x40> Name;
    ClearTString(Name);
    Name = Reader.get_indexed_value(2);
    Value = GetRepValueFromName(Mission, Name);
}

// LAB_06ec97e0. "Player" is compared exactly (case-sensitive). Retail calls FUN_06f15710 after
// SpaceObj::GetRep and drops the result.
bool ActSetRep::Execute()
{
    unsigned int const ObjectId = Mission->GetLabelObjectId(&Label);
    bool const bPlayer = strcmp(Label.Data, "Player") == 0;
    if ((ObjectId & GNagIdMask) == 0 && !bPlayer)
        return false;
    if ((GroupId & GNagIdMask) == 0)
        return false;
    int Rep = 0;
    int Result;
    if (bPlayer && FindContentPlayerState(Mission->PlayerId) != nullptr)
    {
        Result = pub::Player::GetRep(Mission->PlayerId, Rep);
    }
    else
    {
        Result = pub::SpaceObj::GetRep(ObjectId, Rep);
        (void)Mission->FindNpcByName(&Label);
    }
    if (Result == 0)
        pub::Reputation::SetReputation(Rep, GroupId, Value);
    return true;
}

// FUN_06ec98b0.
ActSetShipAndLoadout::ActSetShipAndLoadout(FMissionScript* InMission)
    : Act(InMission)
    , ShipArchId(0)
{
    ClearTString(LoadoutName);
    ClearTString(ShipName);
}

// FUN_06ec9940. LoadoutName takes value 0 again, as retail.
void ActSetShipAndLoadout::Parse(INI_Reader& Reader)
{
    if (!Reader.is_value_empty(0))
    {
        ShipName = Reader.get_value_string(0);
        if (_stricmp(Reader.get_value_string(0), "none") != 0)
            pub::GetShipID(ShipArchId, Reader.get_value_string(0));
    }
    unsigned int LoadoutId = 0;
    if (Reader.is_value_empty(1))
        return;
    LoadoutName = Reader.get_value_string(0);
    if (_stricmp(Reader.get_value_string(1), "none") == 0)
        return;
    if (pub::GetLoadoutID(LoadoutId, Reader.get_value_string(1)) != 0)
        return;
    pub::GetLoadout(Loadout, LoadoutId);
}

// LAB_06ec9a70.
bool ActSetShipAndLoadout::Execute()
{
    pub::Player::SetShipAndLoadout(Mission->PlayerId, ShipArchId, Loadout);
    return true;
}

// ---------------------------------------------------------------------------------------
// Mission vibes: FUN_06ec9b30 and Act_SetVibe / SetVibeLbl / SetVibeLblToShip /
// SetVibeShipToLbl
// ---------------------------------------------------------------------------------------

namespace
{
    // The tail of FUN_06ec9b30 for one object: a non-mission solar's rep is appended to mission
    // +0x174 unless listed. GetType's result is not tested.
    void RecordVibeSolar(FMissionScript* Script, unsigned int ObjId)
    {
        if ((ObjId & GNagIdMask) == 0)
            return;
        unsigned int Type = 0;
        pub::SpaceObj::GetType(ObjId, Type);
        if ((Type & 0xfdff) == 0)
            return;
        if (Script->FindSolarById(&ObjId) != nullptr)
            return;
        int Rep = -1;
        pub::SpaceObj::GetSolarRep(ObjId, Rep);
        std::vector<int>::iterator It = Script->SolarReps.begin();
        while (It != Script->SolarReps.end() && *It != Rep)
            ++It;
        if (It == Script->SolarReps.end())
            Script->SolarReps.push_back(Rep);
    }

    // The value parse shared by the four vibe Acts (IsNumberText = FUN_06f1d430).
    float ParseVibeValue(INI_Reader& Reader, FMissionScript* Script, int Index)
    {
        if (IsNumberText(Reader.get_value_string(Index)))
            return Reader.get_value_float(Index);
        TString<0x40> Name;
        ClearTString(Name);
        Name = Reader.get_indexed_value(Index);
        return GetRepValueFromName(Script, Name);
    }

    // Values 2 and 3 of the four vibe Acts.
    void ParseVibeRange(INI_Reader& Reader, FMissionScript* Script, float* Lo, float* Hi)
    {
        float const First = ParseVibeValue(Reader, Script, 2);
        *Lo = First;
        *Hi = First;
        if (Reader.is_value_empty(3))
            return;
        *Hi = ParseVibeValue(Reader, Script, 3);
    }

    // The +0x17c label list of an NPC holds LabelId (the inline walks with FUN_06f68e30).
    bool NpcHasLabel(FContentNpcShip const* Npc, unsigned int LabelId)
    {
        std::list<unsigned int>::const_iterator It = Npc->LabelIds.begin();
        for (; It != Npc->LabelIds.end(); ++It)
        {
            if (*It == LabelId)
                return true;
        }
        return false;
    }
}

// FUN_06ec9b30 (SetObjectAttitude = FUN_06f682c0): a formation leader (slot 0) or a lone NPC
// passes the result to every ship of its formation.
float FMissionScript::SetObjectVibe(unsigned int ObjA, unsigned int ObjB, float Lo, float Hi)
{
    float const Result = SetObjectAttitude(ObjA, ObjB, Lo, Hi);
    FContentNpcShip* const Npc = FindNpcById(&ObjA);
    if (Npc != nullptr &&
        (Npc->FormationSlot == 0 || Npc->FormationId == static_cast<unsigned int>(-1)))
    {
        std::vector<FContentShipBase*> Members;
        CollectFormationMembers(&Members, Npc->FormationId);
        std::vector<FContentShipBase*>::iterator It = Members.begin();
        for (; It != Members.end(); ++It)
            SetObjectAttitude((*It)->SpaceObjId, ObjB, Result, Result);
    }
    RecordVibeSolar(this, ObjA);
    RecordVibeSolar(this, ObjB);
    return Result;
}

// FUN_06ec9df0.
ActSetVibe::ActSetVibe(FMissionScript* InMission)
    : Act(InMission)
    , Lo(0.0f)
    , Hi(0.0f)
{
    ClearTString(LabelA);
    LabelA = "";
    ClearTString(LabelB);
    LabelB = "";
}

// FUN_06ec9ea0.
void ActSetVibe::Parse(INI_Reader& Reader)
{
    LabelA = Reader.get_value_string(0);
    LabelB = Reader.get_value_string(1);
    ParseVibeRange(Reader, Mission, &Lo, &Hi);
}

// LAB_06eca050.
bool ActSetVibe::Execute()
{
    unsigned int const ObjA = Mission->GetLabelObjectId(&LabelA);
    unsigned int const ObjB = Mission->GetLabelObjectId(&LabelB);
    if ((ObjA & GNagIdMask) != 0 && (ObjB & GNagIdMask) != 0)
        Mission->SetObjectVibe(ObjA, ObjB, Lo, Hi);
    return true;
}

// FUN_06eca0a0.
ActSetVibeLbl::ActSetVibeLbl(FMissionScript* InMission)
    : Act(InMission)
    , LabelA(0)
    , LabelB(0)
    , Lo(0.0f)
    , Hi(0.0f)
{
    LabelA = CreateID(nullptr);
    LabelB = CreateID(nullptr);
}

// FUN_06eca130.
void ActSetVibeLbl::Parse(INI_Reader& Reader)
{
    LabelA = CreateID(Reader.get_value_string(0));
    LabelB = CreateID(Reader.get_value_string(1));
    ParseVibeRange(Reader, Mission, &Lo, &Hi);
}

// FUN_06eca2b0 (FMissionSolar::HasLabel = FUN_06f19c70). The solar-to-NPC pass does not
// null-test NPC B, unlike the NPC-to-NPC pass.
bool ActSetVibeLbl::Execute()
{
    std::map<unsigned int, FContentNpcShip*>::iterator AIt = Mission->NpcObjects.begin();
    for (; AIt != Mission->NpcObjects.end(); ++AIt)
    {
        FContentNpcShip* const NpcA = AIt->second;
        if (!NpcHasLabel(NpcA, LabelA))
            continue;
        std::map<unsigned int, FContentNpcShip*>::iterator BIt = Mission->NpcObjects.begin();
        for (; BIt != Mission->NpcObjects.end(); ++BIt)
        {
            FContentNpcShip* const NpcB = BIt->second;
            if (NpcB == nullptr || !NpcHasLabel(NpcB, LabelB))
                continue;
            if ((NpcA->SpaceObjId & GNagIdMask) != 0 && (NpcB->SpaceObjId & GNagIdMask) != 0)
                Mission->SetObjectVibe(NpcA->SpaceObjId, NpcB->SpaceObjId, Lo, Hi);
        }
        std::list<FMissionSolar>::iterator SIt = Mission->MissionSolars.begin();
        for (; SIt != Mission->MissionSolars.end(); ++SIt)
        {
            if (!SIt->HasLabel(&LabelB))
                continue;
            if ((NpcA->SpaceObjId & GNagIdMask) != 0 && (SIt->SpaceObjId & GNagIdMask) != 0)
                Mission->SetObjectVibe(NpcA->SpaceObjId, SIt->SpaceObjId, Lo, Hi);
        }
    }
    std::list<FMissionSolar>::iterator SIt = Mission->MissionSolars.begin();
    for (; SIt != Mission->MissionSolars.end(); ++SIt)
    {
        if (!SIt->HasLabel(&LabelA))
            continue;
        std::map<unsigned int, FContentNpcShip*>::iterator BIt = Mission->NpcObjects.begin();
        for (; BIt != Mission->NpcObjects.end(); ++BIt)
        {
            FContentNpcShip* const NpcB = BIt->second;
            if (!NpcHasLabel(NpcB, LabelB))
                continue;
            if ((SIt->SpaceObjId & GNagIdMask) != 0 && (NpcB->SpaceObjId & GNagIdMask) != 0)
                Mission->SetObjectVibe(SIt->SpaceObjId, NpcB->SpaceObjId, Lo, Hi);
        }
    }
    return true;
}

// FUN_06eca550. Retail leaves +0x44/+0x48 unset; the port zeroes them.
ActSetVibeLblToShip::ActSetVibeLblToShip(FMissionScript* InMission)
    : Act(InMission)
    , FormationId(0)
    , Lo(0.0f)
    , Hi(0.0f)
{
    FormationId = CreateID(nullptr);
    ClearTString(Ship);
    Ship = "";
}

// FUN_06eca610.
void ActSetVibeLblToShip::Parse(INI_Reader& Reader)
{
    FormationId = CreateID(Reader.get_value_string(0));
    Ship = Reader.get_value_string(1);
    ParseVibeRange(Reader, Mission, &Lo, &Hi);
}

// FUN_06eca7a0: the upper bound is re-taken each pass.
bool ActSetVibeLblToShip::Execute()
{
    unsigned int const ShipId = Mission->GetLabelObjectId(&Ship);
    std::multimap<unsigned int, unsigned int>::iterator Member =
        Mission->LabelObjects.lower_bound(FormationId);
    while (Member != Mission->LabelObjects.upper_bound(FormationId))
    {
        if ((Member->second & GNagIdMask) != 0 && (ShipId & GNagIdMask) != 0)
            Mission->SetObjectVibe(Member->second, ShipId, Lo, Hi);
        ++Member;
    }
    return true;
}

// FUN_06eca860. Retail leaves +0x44/+0x48 unset; the port zeroes them.
ActSetVibeShipToLbl::ActSetVibeShipToLbl(FMissionScript* InMission)
    : Act(InMission)
    , FormationId(0)
    , Lo(0.0f)
    , Hi(0.0f)
{
    ClearTString(Ship);
    Ship = "";
    FormationId = CreateID(nullptr);
}

// FUN_06eca920.
void ActSetVibeShipToLbl::Parse(INI_Reader& Reader)
{
    Ship = Reader.get_value_string(0);
    FormationId = CreateID(Reader.get_value_string(1));
    ParseVibeRange(Reader, Mission, &Lo, &Hi);
}

// FUN_06ecaab0.
bool ActSetVibeShipToLbl::Execute()
{
    unsigned int const ShipId = Mission->GetLabelObjectId(&Ship);
    std::multimap<unsigned int, unsigned int>::iterator Member =
        Mission->LabelObjects.lower_bound(FormationId);
    while (Member != Mission->LabelObjects.upper_bound(FormationId))
    {
        if ((ShipId & GNagIdMask) != 0 && (Member->second & GNagIdMask) != 0)
            Mission->SetObjectVibe(ShipId, Member->second, Lo, Hi);
        ++Member;
    }
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_SpawnShip / Act_SpawnShipRel / Act_SpawnSolar / Act_SpawnFormation / Act_SpawnLoot
// ---------------------------------------------------------------------------------------

namespace
{
    // The quaternion tail of the spawn Parses: W, X, Y, Z read last to first, normalized.
    void ParseSpawnRotation(INI_Reader& Reader, int First, FMatrix* Out)
    {
        float const Z = Reader.get_value_float(First + 3);
        float const Y = Reader.get_value_float(First + 2);
        float const X = Reader.get_value_float(First + 1);
        float const W = Reader.get_value_float(First);
        FQuaternion const Rotation = NormalizeQuaternion(W, X, Y, Z);
        if (GTriggerMathEngine == nullptr)
            GTriggerMathEngine = ContentAcquire3DMathEngine();
        FMatrix Result;
        GTriggerMathEngine->QuaternionToMatrix(Result, Rotation);
        *Out = Result;
    }

    // The identity rotation and zero position the spawn ctors store.
    void SetIdentityTransform(FTransform* Transform)
    {
        memset(Transform, 0, sizeof(*Transform));
        Transform->Rotation.D[0][0] = 1.0f;
        Transform->Rotation.D[1][1] = 1.0f;
        Transform->Rotation.D[2][2] = 1.0f;
    }

    // The first +0xb8 record whose nickname equals Name (strcmp), or end.
    std::list<FMissionShipGroup>::iterator FindShipGroup(FMissionScript* Script,
                                                         TString<0x30> const* Name)
    {
        std::list<FMissionShipGroup>::iterator It = Script->MissionShips.begin();
        for (; It != Script->MissionShips.end(); ++It)
        {
            if (strcmp(Name->Data, It->Nickname.Data) == 0)
                break;
        }
        return It;
    }

    // FUN_06ecebc0 (cdecl): the placement of Act_SpawnShipRel relative to *RelObjId. Retail's
    // fifth argument is the record's +0x38 (orientation, then position at +0x5c); the port
    // passes the two members. GetLocation's result is not tested; the port's outputs start at
    // zero where retail's are indeterminate. A negative or NaN heading means none. POINT places
    // the ship 2500 (DAT_06fb6054) from the object, away from the offset point, using retail's
    // 0x5f3759df inverse-root step; any other mode leaves the position.
    void ComputeSpawnPlacement(int const* Mode, unsigned int const* RelObjId,
                               FTransform const* Offset, float Heading, FMatrix* OutOrientation,
                               Vector* OutPosition)
    {
        Vector ObjectPosition = {0.0f, 0.0f, 0.0f};
        Matrix ObjectOrientation;
        memset(&ObjectOrientation, 0, sizeof(ObjectOrientation));
        pub::SpaceObj::GetLocation(*RelObjId, ObjectPosition, ObjectOrientation);
        FMatrix ObjectRotation;
        memcpy(&ObjectRotation, &ObjectOrientation, sizeof(ObjectRotation));

        FMatrix Heading3;
        memset(&Heading3, 0, sizeof(Heading3));
        Heading3.D[0][0] = 1.0f;
        Heading3.D[1][1] = 1.0f;
        Heading3.D[2][2] = 1.0f;
        if (!(Heading < 0.0f) && !std::isnan(Heading))
        {
            float const Angle = Heading * 0.0174532924f; // DAT_06fb6060 (0x3c8efa35)
            double const Cos = std::cos(static_cast<double>(Angle));
            double const Sin = std::sin(static_cast<double>(Angle));
            Heading3.D[0][0] = static_cast<float>(Cos);
            Heading3.D[0][2] = static_cast<float>(Sin);
            Heading3.D[2][0] = static_cast<float>(-Sin);
            Heading3.D[2][2] = static_cast<float>(Cos);
        }
        if (GTriggerMathEngine == nullptr)
            GTriggerMathEngine = ContentAcquire3DMathEngine();
        FMatrix Rotation;
        GTriggerMathEngine->MulMatrix(Rotation, Heading3, ObjectRotation);

        int const PlacementMode = *Mode;
        if (PlacementMode == 2)
        {
            if (GTriggerMathEngine == nullptr)
                GTriggerMathEngine = ContentAcquire3DMathEngine();
            FVector Turned;
            GTriggerMathEngine->TransformMatrix(Turned, ObjectRotation, Offset->Translation);
            OutPosition->x = Turned.X + ObjectPosition.x;
            OutPosition->y = Turned.Y + ObjectPosition.y;
            OutPosition->z = Turned.Z + ObjectPosition.z;
        }
        else if (PlacementMode == 1)
        {
            OutPosition->x = ObjectPosition.x + Offset->Translation.X;
            OutPosition->y = ObjectPosition.y + Offset->Translation.Y;
            OutPosition->z = ObjectPosition.z + Offset->Translation.Z;
        }
        else if (PlacementMode == 0)
        {
            float const DX = ObjectPosition.x - Offset->Translation.X;
            float const DY = ObjectPosition.y - Offset->Translation.Y;
            float const DZ = ObjectPosition.z - Offset->Translation.Z;
            float const LengthSq = static_cast<float>(static_cast<double>(DX) * DX +
                                                      static_cast<double>(DY) * DY +
                                                      static_cast<double>(DZ) * DZ);
            int Bits;
            memcpy(&Bits, &LengthSq, sizeof(Bits));
            Bits = 0x5f3759df - (Bits >> 1);
            float Guess;
            memcpy(&Guess, &Bits, sizeof(Guess));
            double const Inverse = (1.5 - static_cast<double>(Guess) * Guess * LengthSq * 0.5) *
                                   Guess;
            double const NX = DX * Inverse;
            double const NY = DY * Inverse;
            double const NZ = DZ * Inverse;
            float const Scale =
                static_cast<float>(std::sqrt(6250000.0 / (NZ * NZ + NY * NY + NX * NX)));
            float const OffsetX = static_cast<float>(NX * Scale);
            float const OffsetY = static_cast<float>(NY * Scale);
            float const OffsetZ = static_cast<float>(NZ * Scale);
            OutPosition->x = ObjectPosition.x - OffsetX;
            OutPosition->y = ObjectPosition.y - OffsetY;
            OutPosition->z = ObjectPosition.z - OffsetZ;
        }
        *OutOrientation = Rotation;
    }
}

// FUN_06ec62b0.
ActSpawnShip::ActSpawnShip(FMissionScript* InMission)
    : Act(InMission)
    , bPosition(0)
    , bOrientation(0)
    , bRename(0)
    , ObjList(0)
{
    ClearTString(Name);
    Name = "";
    ClearTString(NewNickname);
    NewNickname = "";
    SetIdentityTransform(&Placement);
}

// FUN_06ec63f0. The position is read from slot 4 down.
void ActSpawnShip::Parse(INI_Reader& Reader)
{
    Name = Reader.get_value_string(0);
    if (Reader.is_value_empty(1))
        return;
    ObjList = CreateID(Reader.get_value_string(1));
    if (Reader.is_value_empty(2))
        return;
    bPosition = 1;
    float const Z = Reader.get_value_float(4);
    float const Y = Reader.get_value_float(3);
    Placement.Translation.X = Reader.get_value_float(2);
    Placement.Translation.Y = Y;
    Placement.Translation.Z = Z;
    if (Reader.is_value_empty(5))
        return;
    bOrientation = 1;
    ParseSpawnRotation(Reader, 5, &Placement.Rotation);
}

// FUN_06ec6580: a copy of the first +0xb8 record of that name (FUN_06ed01c0) is spawned by
// FUN_06f15e20; "no_ol" keeps the record's objectives.
bool ActSpawnShip::Execute()
{
    std::list<FMissionShipGroup>::iterator It = FindShipGroup(Mission, &Name);
    if (It == Mission->MissionShips.end())
        return true;
    FMissionShipGroup Group = *It;
    std::vector<FMissionShip>::iterator Slot = Group.Ships.begin();
    for (; Slot != Group.Ships.end(); ++Slot)
        Slot->HitPoints = Slot->OriginalHitPoints;
    if (ObjList != 0)
    {
        if (ObjList != CreateID("no_ol"))
            Group.Ships.front().InitObjectives.Id = ObjList;
        if (bPosition != 0)
        {
            Group.bRelPos = 0;
            Group.Position.x = Placement.Translation.X;
            Group.Position.y = Placement.Translation.Y;
            Group.Position.z = Placement.Translation.Z;
        }
        if (bOrientation != 0)
            Group.Orientation = Placement.Rotation;
        if (bRename != 0)
            Group.Ships.front().Nickname = NewNickname.Data;
    }
    Mission->SpawnMissionShipGroup(&Group);
    return true;
}

// FUN_06ec69c0. Retail does not write +0xd8 (see the header).
ActSpawnShipRel::ActSpawnShipRel(FMissionScript* InMission)
    : Act(InMission)
    , Mode(-1)
    , Field_0xdc(0)
    , bRename(0)
    , ObjList(0)
    , Heading(-1.0f)
{
    ClearTString(Name);
    ClearTString(RelObj);
    ClearTString(NewNickname);
    SetIdentityTransform(&Offset);
}

// FUN_06ec6aa0. The offset is read from slot 6 down.
void ActSpawnShipRel::Parse(INI_Reader& Reader)
{
    Name = Reader.get_value_string(0);
    ObjList = CreateID(Reader.get_value_string(1));
    RelObj = Reader.get_value_string(2);
    if (_stricmp(Reader.get_value_string(3), "DIRECTIONAL") == 0)
        Mode = 1;
    else if (_stricmp(Reader.get_value_string(3), "POINT") == 0)
        Mode = 0;
    else if (_stricmp(Reader.get_value_string(3), "RELATIVE") == 0)
        Mode = 2;
    float const Z = Reader.get_value_float(6);
    float const Y = Reader.get_value_float(5);
    Offset.Translation.X = Reader.get_value_float(4);
    Offset.Translation.Y = Y;
    Offset.Translation.Z = Z;
    if (Reader.is_value_empty(7))
        return;
    Heading = Reader.get_value_float(7);
    if (Reader.is_value_empty(8))
        return;
    bRename = 1;
    NewNickname = Reader.get_value_string(8);
}

// FUN_06ec6c40: every +0xb8 record that matches (a slot's nickname, or the record's, tested
// per slot) spawns a copy holding only that slot; the walk goes on after a match.
bool ActSpawnShipRel::Execute()
{
    std::list<FMissionShipGroup>::iterator It = Mission->MissionShips.begin();
    for (; It != Mission->MissionShips.end(); ++It)
    {
        std::vector<FMissionShip>::iterator Slot = It->Ships.begin();
        for (; Slot != It->Ships.end(); ++Slot)
        {
            if (strcmp(Name.Data, Slot->Nickname.Data) == 0)
                break;
            if (strcmp(Name.Data, It->Nickname.Data) == 0)
                break;
        }
        if (Slot == It->Ships.end())
            continue;
        FMissionShipGroup Group = *It;
        Group.Ships.clear();
        Group.Ships.push_back(*Slot);
        if (ObjList != 0)
        {
            if (ObjList != CreateID("no_ol"))
                Group.Ships.front().InitObjectives.Id = ObjList;
            unsigned int const RelObjId = Mission->GetLabelObjectId(&RelObj);
            ComputeSpawnPlacement(&Mode, &RelObjId, &Offset, Heading, &Group.Orientation,
                                  &Group.Position);
            if (bRename != 0)
                Group.Ships.front().Nickname = NewNickname.Data;
        }
        Mission->SpawnMissionShipGroup(&Group);
    }
    return true;
}

namespace
{
    // FUN_06ec7450 (the Parse of Act_SpawnSolar / SpawnFormation / SpawnLoot). The position
    // is read from slot 3 down.
    void ParseSpawnRecord(INI_Reader& Reader, TString<0x30>* Name, FTransform* Placement,
                          unsigned char* bPosition, unsigned char* bOrientation)
    {
        *Name = Reader.get_value_string(0);
        if (Reader.is_value_empty(1))
            return;
        *bPosition = 1;
        float const Z = Reader.get_value_float(3);
        float const Y = Reader.get_value_float(2);
        Placement->Translation.X = Reader.get_value_float(1);
        Placement->Translation.Z = Z;
        Placement->Translation.Y = Y;
        if (Reader.is_value_empty(4))
            return;
        *bOrientation = 1;
        ParseSpawnRotation(Reader, 4, &Placement->Rotation);
    }
}

// FUN_06ec6fc0.
ActSpawnSolar::ActSpawnSolar(FMissionScript* InMission)
    : Act(InMission)
    , bPosition(0)
    , bOrientation(0)
{
    SetIdentityTransform(&Placement);
    ClearTString(Name);
    Name = "";
}

// FUN_06ec7450.
void ActSpawnSolar::Parse(INI_Reader& Reader)
{
    ParseSpawnRecord(Reader, &Name, &Placement, &bPosition, &bOrientation);
}

// FUN_06ec7060 (the +0xc4 list; SpawnMissionSolar = FUN_06f171e0). Without an orientation the
// record's (+0x41c) is copied into the action's own placement and stays there.
bool ActSpawnSolar::Execute()
{
    std::list<FMissionSolar>::iterator It = Mission->MissionSolars.begin();
    for (; It != Mission->MissionSolars.end(); ++It)
    {
        if (strcmp(It->Nickname.Data, Name.Data) == 0)
            break;
    }
    if (It == Mission->MissionSolars.end())
        return true;
    if (bPosition != 0)
    {
        if (bOrientation == 0)
            Placement.Rotation = It->Orientation;
        Mission->SpawnMissionSolar(&Name, &Placement);
        return true;
    }
    Mission->SpawnMissionSolar(&Name, nullptr);
    return true;
}

// FUN_06ec7130.
ActSpawnFormation::ActSpawnFormation(FMissionScript* InMission)
    : Act(InMission)
    , bPosition(0)
    , bOrientation(0)
{
    SetIdentityTransform(&Placement);
    ClearTString(Name);
    Name = "";
}

// FUN_06ec7450.
void ActSpawnFormation::Parse(INI_Reader& Reader)
{
    ParseSpawnRecord(Reader, &Name, &Placement, &bPosition, &bOrientation);
}

// FUN_06ec71d0. Unlike Act_SpawnShip, the hit points are reset on the record itself.
bool ActSpawnFormation::Execute()
{
    std::list<FMissionShipGroup>::iterator It = FindShipGroup(Mission, &Name);
    if (It == Mission->MissionShips.end())
        return true;
    std::vector<FMissionShip>::iterator Slot = It->Ships.begin();
    for (; Slot != It->Ships.end(); ++Slot)
        Slot->HitPoints = Slot->OriginalHitPoints;
    if (bPosition == 0)
    {
        Mission->SpawnMissionShipGroup(&*It);
        return true;
    }
    FMissionShipGroup Group = *It;
    Group.Position.x = Placement.Translation.X;
    Group.Position.y = Placement.Translation.Y;
    Group.Position.z = Placement.Translation.Z;
    if (bOrientation != 0)
        Group.Orientation = Placement.Rotation;
    Mission->SpawnMissionShipGroup(&Group);
    return true;
}

// FUN_06ec73f0.
ActSpawnLoot::ActSpawnLoot(FMissionScript* InMission)
    : Act(InMission)
    , bPosition(0)
    , bOrientation(0)
{
    ClearTString(Name);
    SetIdentityTransform(&Placement);
}

// FUN_06ec7450.
void ActSpawnLoot::Parse(INI_Reader& Reader)
{
    ParseSpawnRecord(Reader, &Name, &Placement, &bPosition, &bOrientation);
}

// FUN_06ec75d0 (the +0xd0 list; SpawnMissionLoot = FUN_06f16fa0).
bool ActSpawnLoot::Execute()
{
    std::list<FMissionLoot>::iterator It = Mission->MissionLoots.begin();
    for (; It != Mission->MissionLoots.end(); ++It)
    {
        if (strcmp(Name.Data, It->Nickname.Data) == 0)
            break;
    }
    if (It == Mission->MissionLoots.end())
        return true;
    if (bPosition == 0)
    {
        Mission->SpawnMissionLoot(&*It);
        return true;
    }
    FMissionLoot Loot = *It;
    Loot.Info.Position.x = Placement.Translation.X;
    Loot.Info.Position.y = Placement.Translation.Y;
    Loot.Info.Position.z = Placement.Translation.Z;
    if (bOrientation != 0)
        memcpy(&Loot.Info.Orientation, &Placement.Rotation, sizeof(Loot.Info.Orientation));
    Mission->SpawnMissionLoot(&Loot);
    return true;
}

// ---------------------------------------------------------------------------------------
// Act_StartDialog
// ---------------------------------------------------------------------------------------

// FUN_06ece7f0.
ActStartDialog::ActStartDialog(FMissionScript* InMission)
    : Act(InMission)
    , DialogId(0)
    , DelaySeconds(-1)
    , bDelayPending(0)
{
}

// FUN_06ece860. An explicit -1 passes all three tests and arms the delay.
void ActStartDialog::Parse(INI_Reader& Reader)
{
    DialogId = CreateID(Reader.get_value_string(0));
    if (Reader.is_value_empty(1))
        return;
    DelaySeconds = Reader.get_value_int(1);
    if (DelaySeconds < -1 || DelaySeconds == 0 || DelaySeconds > 10)
    {
        DelaySeconds = -1;
        return;
    }
    bDelayPending = 1;
}

// FUN_06ece8c0 (StartMissionDialog = FUN_06ef2a10). A delay re-arms itself for the next run.
bool ActStartDialog::Execute()
{
    if (bDelayPending != 0)
    {
        FMissionTimer Timer;
        Timer.Remaining = static_cast<float>(DelaySeconds);
        Timer.Action = this;
        Mission->Timers.push_back(Timer);
        bDelayPending = 0;
        return true;
    }
    bool const bResult = StartMissionDialog(&DialogId, &Mission->PlayerId);
    if (DelaySeconds != -1)
        bDelayPending = 1;
    return bResult;
}
