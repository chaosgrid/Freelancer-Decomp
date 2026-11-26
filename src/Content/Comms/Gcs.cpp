// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU25 (06ed5a70-06eeea30): GCS handler set, conversation manager and
// admission tables.
//
// The std::map / std::list / std::vector instantiations retail emits for these members
// (map find FUN_06eeca70 / FUN_06f7b0f0, insert
// FUN_06f931c0 / FUN_06f21210 / FUN_06f7c9e0, operator[] FUN_06eec550 / FUN_06eec720,
// erase FUN_06f20e00 / FUN_06eecae0 / FUN_06eecec0, list erase FUN_06ecfe80, list insert
// FUN_06f1e440, vector insert FUN_06eedcc0 / FUN_06eee010 / FUN_06eee050, ctors
// FUN_06eec4e0 / FUN_06eec680 / FUN_06eec860) are the std containers of Gcs.h.

#include "Comms/Gcs.h"

#include "Bases/BaseDB.h"             // FindContentBaseInfo (FUN_06eac0c0)
#include "NPCs/BehaviorCmds.h"       // FContentShipBase
#include "Core/ContentInternal.h"    // GContentNextUniqueId, GSpaceSupportControllerId, game time
#include "Core/ContentPlayerState.h" // FindContentPlayerStateByShip (FUN_06f49bc0)
#include "NPCs/DockQueues.h"         // FGcsLaunchEvent, CollectFormationMembers (FUN_06f66220)
#include "Core/ContentSort.h"        // ContentSort (the VC6 std::sort of FUN_06f66440)
#include "Core/ContentStaticIds.h"   // TU25's 16 static ids (06ed5a70-06ed5c50)
#include "NPCs/FactionProps.h"       // GetFactionProps (FUN_06f50650)
#include "Guides/Guides.h"             // FContentGuide::GetTargets (slot +0x34)
#include "Guides/GuidesInternal.h"     // FTradeGuide slots +0x4c / +0x50
#include "NPCs/NpcNames.h"           // FNpcNameReputation (TU42 name values)
#include "NPCs/NpcShip.h"            // FContentNpcShip
#include "RandomMissions/RandomMissionGen.h"   // FindVoiceProperties (FUN_06fa6e50)
#include "RandomMissions/RandomMissionOffer.h" // FRmConversationBase (FUN_06ee9140's argument)
#include "Universe/SystemInfo.h"         // FindSystemInfo (FUN_06f75f30), FSystemInfo, SendSpaceComm
#include "Comms/Voices.h"             // FindVoiceLineDuration (FUN_06f67780)
#include "common_server.h"
#include "common_server_controller.h"   // pub::SetTimer, pub::Controller

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

// TU25's static initializers run in this order after the 16 ids of ContentStaticIds.h
// (06ed5a70-06ed5c50); the atexit destructors run in reverse.
namespace
{
    // 06ed5c70: DAT_06fcf158, recent comms per category (ctor FUN_06eec6b0, atexit 06ed5ca0:
    // guard bit 1 of DAT_06fcf154, _Tidy with the node lists destroyed).
    std::map<int, std::list<FGcsRecentComm>> GGcsRecentComms;
    // 06ed5d90: DAT_06fcf140, minimum interval per category (ctor FUN_06eec860, atexit
    // 06ed5dc0: guard bit 1 of DAT_06fcf13c, _Tidy FUN_06ed5de0).
    std::map<int, float> GGcsCategoryIntervals;
    // 06ed5ea0: DAT_06fcf128, chance per category (ctor FUN_06eec860, atexit 06ed5ed0: bit 1
    // of DAT_06fcf124, FUN_06ed5de0).
    std::map<int, float> GGcsCategoryChances;
    // 06ed5ef0: DAT_06fcf110, categories open to non-SpacePop listeners (ctor FUN_06eec860,
    // atexit 06ed5f20: bit 2 of DAT_06fcf124, _Tidy FUN_06ed5f40).
    std::map<int, bool> GGcsListenerCategories;
    // 06ed6000: DAT_06fcf0fc, the same for speakers (ctor FUN_06eec860, atexit 06ed6030: bit 1
    // of DAT_06fcf0f8, FUN_06ed5f40).
    std::map<int, bool> GGcsSpeakerCategories;

    // 06ed6050: the recent-hail timer id, DAT_06fce76c++.
    unsigned int const GGcsDockHailTimerId = GContentNextUniqueId++;

    // 06ed6070: DAT_06fcf0e0 (ctor FUN_06eec860, atexit FUN_06ed60a0 frees the nodes only;
    // the handlers belong to GGcsHandlers).
    std::map<int, FGcsHandler*> GGcsHandlerMap;

    // DAT_06fce360 (.data bytes 00 00 00 00 00 00 28 40): 12.0; FUN_06eeba30 sets it.
    double GGcsDefaultInterval = 12.0;
}

// TU53's static initializers (they run after TU25's in retail).
// 06f5c980: FUN_06ee8a70 + atexit 06f5c9a0 (FUN_06ee8af0).
FGcsConversationManager GGcsConversations;
FGcsConversationManager* GGcsConversationManager = nullptr;
// 06f5c9c0: FUN_06f5ca00 + atexit 06f5c9e0 (FUN_06ee9690).
FGcsHandlerSet GGcsHandlers;

// =======================================================================================
// Voice line names
// =======================================================================================

void FormatTString64(TString<0x40>* Out, char const* Format, ...)
{
    // FUN_06eec2b0. vsprintf unbounded as in retail (no caller formats more than 63
    // characters); the clamped copy is TString::operator=.
    char Buffer[0x40];
    va_list Args;
    va_start(Args, Format);
    vsprintf(Buffer, Format, Args);
    va_end(Args);
    *Out = Buffer;
}

int __cdecl PickVoiceLine(char const* Name, char const* Suffix, unsigned int VoiceId,
                          TString<0x40>* Out)
{
    // FUN_06eeb010. Format "<Name>_%02d%s".
    TString<0x40> Format;
    Format.Length = 0;
    Format.Data[0] = '\0';
    FormatTString64(&Format, "%s_%%02d%%s", Name);
    int Result = -2;
    FVoiceProperties* const Voice = FindVoiceProperties(&VoiceId);
    if (Voice != nullptr)
    {
        unsigned int const NameId = pub::GetNicknameId(Name);
        int const Count = Voice->GetPermutationCount(&NameId);
        if (Count > 0)
        {
            // x87 order kept: n as float and as double; constants DAT_06fb4448 (2^-15),
            // DAT_06fb74e8 ((double)(float)(pi/2)), DAT_06fb5810 (1.0f), DAT_06fb74e0 (0.5).
            float const CountFloat = static_cast<float>(Count);
            double const CountDouble = static_cast<double>(Count);
            double const Spread = cos(static_cast<double>(rand()) *
                                      static_cast<double>(1.0f / 32768.0f) * 1.5707963705062866);
            double const Pick = floor(CountDouble -
                                      Spread * static_cast<double>(CountFloat - 1.0f) + 0.5);
            FormatTString64(Out, Format.Data, static_cast<int>(Pick), Suffix);
            return 0;
        }
        Result = -3;
    }
    Out->Length = 0;
    Out->Data[0] = '\0';
    return Result;
}

TString<0x40> __cdecl MakeVoiceLineName(char const* Name, char const* Suffix,
                                        unsigned int VoiceId)
{
    // FUN_06eeaf10: an inlined copy of FUN_06eeb010 returning the result instead of a status;
    // both failure paths leave the zero-initialised local.
    TString<0x40> Result;
    PickVoiceLine(Name, Suffix, VoiceId, &Result);
    return Result;
}

void AppendTString64(TString<0x40>* Str, char const* Tail)
{
    // FUN_06eec250.
    if (Tail == nullptr)
        return;
    unsigned int const Room = 0x40 - Str->Length;
    unsigned int Count = static_cast<unsigned int>(strlen(Tail));
    if (Count >= Room)
        Count = Room - 1;
    memcpy(Str->Data + Str->Length, Tail, Count);
    Str->Data[Str->Length + Count] = '\0';
    Str->Length += Count;
}

TString<0x40> MakeTString64(char const* Str)
{
    // FUN_06eec1f0: {0, ""} first, then the clamped copy (TString::operator= semantics).
    TString<0x40> Result;
    Result.Length = 0;
    Result.Data[0] = '\0';
    Result = Str;
    return Result;
}

int __cdecl GetBaseMsgIdPrefix(unsigned int BaseId, TString<0x40>* Out)
{
    // FUN_06eeb110. Retail's null test of the prefix's chars cannot fire.
    FContentBase* const Base = FindContentBaseInfo(&BaseId);
    if (Base == nullptr)
        return -4;
    *Out = Base->GetMsgIdPrefix().Data;
    return 0;
}

int __cdecl GetFactionMsgIdPrefix(unsigned int FactionId, TString<0x40>* Out)
{
    // FUN_06eeb190: FUN_06eeb110 with GetFactionProps (FUN_06f50650).
    FFactionProps* const Props = GetFactionProps(&FactionId);
    if (Props == nullptr)
        return -4;
    *Out = Props->GetMsgIdPrefix().Data;
    return 0;
}

TString<0x40> __cdecl MakeObjectReferenceName(int Kind, FContentSpaceObj* Object,
                                              char const* Suffix)
{
    // FUN_06eeb210. For an NPC ship the reputation handle (FUN_06f33f80) is the NPC's name
    // reputation.
    TString<0x40> Result;
    Result.Length = 0;
    Result.Data[0] = '\0';
    switch (Kind)
    {
    case kGcsObjectReferenceBusiness:
    {
        // By guide type (2 trader; 3, 4, 7 patrol; others nothing), plural with formation
        // wingmen, then the faction's legality ("_leg" DAT_06fb759c, "_ill" DAT_06fb7594).
        if (!static_cast<FContentShipBase*>(Object)->IsNpcShip())
            return Result;
        char const* Name = nullptr;
        switch (static_cast<FContentNpcShip*>(Object)->GetGuideType())
        {
        case GUIDE_TYPE_TRADE:
            Name = HasFormationWingmen(Object) ? "gcs_dhail_mbid_traders"
                                               : "gcs_dhail_mbid_trader";
            break;
        case GUIDE_TYPE_PATROL_PATH:
        case GUIDE_TYPE_WANDER:
        case GUIDE_TYPE_PATROL_GUIDE:
            Name = HasFormationWingmen(Object) ? "gcs_dhail_mbid_patrollers"
                                               : "gcs_dhail_mbid_patrol";
            break;
        default:
            return Result;
        }
        Result = Name;
        unsigned int Affiliation = Object->ReputationHandle->GetAffiliation();
        FFactionProps* const Props = GetFactionProps(&Affiliation);
        AppendTString64(&Result, Props->Legality == FactionLegalityLawful ? "_leg" : "_ill");
        return Result;
    }
    case kGcsObjectReferenceFormationDesig:
    {
        if (static_cast<FContentShipBase*>(Object)->IsNpcShip())
        {
            int Value = 0;
            if (!static_cast<FNpcNameReputation*>(Object->ReputationHandle)
                     ->GetFormationDesig(&Value))
                return Result;
            FormatTString64(&Result, "gcs_refer_formationdesig_%02d", Value - 0x304af);
            return Result;
        }
        Result = "gcs_refer_formationdesig_01";
        return Result;
    }
    case kGcsObjectReferenceNumber:
    {
        if (static_cast<FContentShipBase*>(Object)->IsNpcShip())
        {
            int Value = 0;
            if (!static_cast<FNpcNameReputation*>(Object->ReputationHandle)
                     ->GetFormationDesigNumber(&Value))
                return Result;
            if (Suffix != nullptr)
                FormatTString64(&Result, "gcs_misc_number_%d%s", Value, Suffix);
            else
                FormatTString64(&Result, "gcs_misc_number_%d", Value);
            return Result;
        }
        // A player: unsigned (PlayerId - 1) % 20 + 1; the state is not null-tested.
        unsigned int ShipId = Object->SpaceObjId;
        FContentPlayerState* const Player = FindContentPlayerStateByShip(&ShipId);
        FormatTString64(&Result, "gcs_misc_number_%d", (Player->PlayerId - 1) % 20 + 1);
        return Result;
    }
    case kGcsObjectReferenceRolledNumber:
    {
        // The NPC's rolled value (the call's result is not read); a player gets 1
        // (FUN_06f49bc0 is called, its result dropped).
        if (static_cast<FContentShipBase*>(Object)->IsNpcShip())
        {
            int Value = 0;
            static_cast<FNpcNameReputation*>(Object->ReputationHandle)->RollNameValue(&Value);
            FormatTString64(&Result, "gcs_misc_number_%d-", Value);
            return Result;
        }
        unsigned int ShipId = Object->SpaceObjId;
        FindContentPlayerStateByShip(&ShipId);
        FormatTString64(&Result, "gcs_misc_number_%d-", 1);
        return Result;
    }
    default:
        return Result;
    }
}

TString<0x40> __cdecl MakeObjectLocationName(unsigned int SpaceObjId)
{
    // FUN_06eeb730.
    TString<0x40> Result;
    Result.Length = 0;
    Result.Data[0] = '\0';
    unsigned int BaseId = 0;
    pub::SpaceObj::GetDockingTarget(SpaceObjId, BaseId);
    if (BaseId == 0)
    {
        // A jump gate / hole: its target system's prefix.
        unsigned int Type = 0;
        pub::SpaceObj::GetType(SpaceObjId, Type);
        if ((Type & (OBJ_JUMP_GATE | OBJ_AIRLOCK_GATE | OBJ_JUMP_HOLE)) == 0)
            return Result;
        unsigned int TargetSystem = 0;
        unsigned int TargetGate = 0;
        pub::SpaceObj::GetJumpTarget(SpaceObjId, TargetSystem, TargetGate);
        pub::TranslateSystemToMsgIdPrefix(TargetSystem, Result);
    }
    else
    {
        // The FUN_06eeb110 body inlined (no base -> empty).
        FContentBase* const Base = FindContentBaseInfo(&BaseId);
        if (Base != nullptr)
            Result = Base->GetMsgIdPrefix().Data;
    }
    AppendTString64(&Result, "-");
    return Result;
}

bool __cdecl HasFormationWingmen(FContentSpaceObj* Ship)
{
    // FUN_06eeb870.
    std::vector<FContentShipBase*> Members;
    CollectFormationMembers(&Members, static_cast<FContentShipBase*>(Ship)->FormationId);
    return Members.size() > 1;
}

void __cdecl MakeNumberNames(int Number, char const* Suffix, std::vector<TString<0x40>>* Out)
{
    // FUN_06eeb590.
    if (Out->capacity() < 3)
        Out->reserve(3);
    TString<0x40> Name;
    if (Number == 0)
    {
        Name.Length = 0;
        Name.Data[0] = '\0';
        FormatTString64(&Name, "gcs_misc_number_%d%s", 0, Suffix);
        Out->insert(Out->end(), 1, Name);
        return;
    }
    // The first (least significant) digit gets Suffix; each name is inserted at the front.
    bool bFirst = true;
    while (Number > 0)
    {
        int const Digit = Number % 10;
        Number /= 10;
        Name.Length = 0;
        Name.Data[0] = '\0';
        FormatTString64(&Name, "gcs_misc_number_%d", Digit);
        if (bFirst)
        {
            if (Suffix != nullptr)
                AppendTString64(&Name, Suffix);
            bFirst = false;
        }
        Out->insert(Out->begin(), 1, Name);
    }
}

TString<0x40> __cdecl MakeDockApproachName(unsigned int DockType, char const* Suffix)
{
    // FUN_06eec0c0.
    TString<0x40> Result;
    Result.Length = 0;
    Result.Data[0] = '\0';
    switch (DockType)
    {
    case TERM_BERTH:
        FormatTString64(&Result, "gcs_dockrequest_todock%s", Suffix);
        break;
    case TERM_MOOR_SMALL:
    case TERM_MOOR_MEDIUM:
    case TERM_MOOR_LARGE:
        FormatTString64(&Result, "gcs_dockrequest_tomoor%s", Suffix);
        break;
    case TERM_RING:
        FormatTString64(&Result, "gcs_dockrequest_toland%s", Suffix);
        break;
    default:
        break;
    }
    return Result;
}

TString<0x40> __cdecl MakeFleeReasonName(int Reason, unsigned int VoiceId)
{
    // FUN_06eec030. "-" is DAT_06fb6064.
    switch (Reason)
    {
    case 0:
        return MakeVoiceLineName("gcs_combat_fleereason_tough", "-", VoiceId);
    case 2:
        return MakeVoiceLineName("gcs_combat_fleereason_damaged", "-", VoiceId);
    case 3:
        return MakeVoiceLineName("gcs_combat_fleereason_noweapons", "-", VoiceId);
    default:
        return MakeVoiceLineName("gcs_combat_fleereason_other", "-", VoiceId);
    }
}

bool __cdecl IsSpaceObjIdMultipleOf5(FContentSpaceObj* Object)
{
    // FUN_06eeb900.
    return Object->SpaceObjId % 5 == 0;
}

// =======================================================================================
// Lines
// =======================================================================================

FGcsLine::FGcsLine(FSystemInfo* InSystem, unsigned int InSpeaker, unsigned int InListener)
    : Speaker(InSpeaker)
    , Listener(InListener)
    , VoiceId(0)
    , CostumeId(0)
    , System(InSystem)
    , PermutationIds(0)
    , CaptionIds(0)
{
}

FGcsLine::~FGcsLine()
{
    // FUN_06ed61d0: vptr 06fb6254, the text buffer freed.
}

bool FGcsLine::CanPlay()
{
    // FUN_06ed6780.
    return PermutationIds != 0 && System != nullptr && System->FindShipOrSolar(Speaker) != nullptr &&
           !TextIds.empty();
}

void FGcsLine::Play()
{
    // LAB_06ed6a40. The texts go into a 16-dword stack array; retail has no bound and no
    // line ctor adds more than eleven texts (FGcsLineScanAnnounce reserves eleven).
    unsigned int Texts[16];
    int const TextCount = static_cast<int>(TextIds.size());
    for (int Index = 0; Index < TextCount; ++Index)
        Texts[Index] = TextIds[Index];
    if (CostumeId != 0)
    {
        Costume CommCostume;
        CommCostume.PartIds[0] = 0;
        CommCostume.PartIds[1] = 0;
        CommCostume.PartIds[2] = 0;
        CommCostume.PartIds[3] = 0;
        CommCostume.AccessoryCount = 0;
        pub::GF::GetCostumeByID(CostumeId, CommCostume);
        SendSpaceComm(System, Speaker, Listener, Texts, &TextCount, VoiceId, CaptionIds,
                      &CommCostume, PermutationIds, 0.5f, true);
    }
    else
    {
        SendSpaceComm(System, Speaker, Listener, Texts, &TextCount, VoiceId, CaptionIds,
                      nullptr, PermutationIds, 0.5f, true);
    }

    // Outside single player: a delayed "line done" after duration + DAT_06fb621c (1.25f).
    if (pub::SinglePlayer())
        return;
    FGcsDelayedMessage Entry;
    Entry.Delay = static_cast<float>(static_cast<double>(GetDuration()) + 1.25);
    Entry.SystemId = System->SystemId;
    Entry.Speaker = Speaker;
    Entry.Listener = Listener;
    Entry.LastTextId = TextIds.back();
    Entry.Elapsed = 0.0f;
    GGcsConversations.DelayedMessages.insert(GGcsConversations.DelayedMessages.end(), 1, Entry);
}

bool FGcsLine::IsDone(void const* Parms)
{
    // LAB_06ed67c0: {+0 listener, +4 system id, +8 speaker, +0xc last text}.
    unsigned int const* Words = static_cast<unsigned int const*>(Parms);
    return System->SystemId == Words[1] && Listener == Words[0] && Speaker == Words[2] &&
           TextIds.back() == Words[3];
}

float FGcsLine::GetDuration()
{
    // LAB_06ed69c0. The speaker's voice when +0x0c is 0 (into a local, the field keeps 0).
    unsigned int Voice = VoiceId;
    if (Voice == 0)
        pub::SpaceObj::GetVoiceID(Speaker, Voice);
    float Sum = 0.0f;
    for (std::vector<unsigned int>::iterator It = TextIds.begin(); It != TextIds.end(); ++It)
    {
        // DAT_06fb5e84 (-1.0f) = unknown -> DAT_06fb6280 (5.0f); the sum is rounded to float
        // each step.
        float const Duration = FindVoiceLineDuration(&Voice, &*It);
        if (Duration == -1.0f)
            return 5.0f;
        Sum = static_cast<float>(static_cast<double>(Duration) + static_cast<double>(Sum));
    }
    return Sum;
}

void FGcsLine::AddText(TString<0x40> Name)
{
    // FUN_06ed6800.
    unsigned int const TextId = pub::GetNicknameId(Name.Data);
    TextIds.insert(TextIds.end(), 1, TextId);
}

bool FGcsLine::HasVoiceRoles(unsigned int Roles)
{
    pub::SpaceObj::GetVoiceID(Speaker, VoiceId);
    FVoiceProperties* const Voice = FindVoiceProperties(&VoiceId);
    if (Voice == nullptr || !Voice->SupportsRoles(Roles))
    {
        System = nullptr;
        return false;
    }
    return true;
}

void FGcsLine::PushText(char const* Name)
{
    unsigned int const TextId = pub::GetNicknameId(Name);
    TextIds.insert(TextIds.end(), 1, TextId);
}

void FGcsLine::BuildSingleVoiceLine(unsigned int Roles, char const* Name)
{
    // E.g. FUN_06edc180. rand() / 32768 is always 0 for the CRT's 15-bit rand, kept as in
    // retail.
    if (!HasVoiceRoles(Roles))
        return;
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_1;
    TextIds.reserve(1);
    PushText(MakeVoiceLineName(MakeTString64(Name).Data, "-", VoiceId).Data);
}

// =======================================================================================
// Player list line (vtable 06fb623c)
// =======================================================================================

FGcsPlayerListLine::FGcsPlayerListLine(FSystemInfo* InSystem)
    : FGcsCloneableLine(InSystem, 0, 0)
{
    // FUN_06ed6180.
}

bool FGcsPlayerListLine::CanPlay()
{
    // FUN_06ed6710. The first player whose state has a ship object decides.
    for (std::vector<unsigned int>::iterator It = PlayerIds.begin(); It != PlayerIds.end(); ++It)
    {
        FContentPlayerState* const State = FindContentPlayerState(*It);
        if (State == nullptr || State->ShipObject == nullptr)
            continue;
        if (Speaker != 0 && pub::SpaceObj::ExistsAndAlive(Speaker) != 0)
            return false;
        return !TextIds.empty();
    }
    return false;
}

void FGcsPlayerListLine::Play()
{
    // LAB_06ed6200. Same unbounded 16-dword text array as FGcsLine::Play.
    unsigned int Texts[16];
    int const CopyCount = static_cast<int>(TextIds.size());
    for (int Index = 0; Index < CopyCount; ++Index)
        Texts[Index] = TextIds[Index];

    // The costume is fetched once.
    Costume CommCostume;
    Costume const* CommCostumePtr = nullptr;
    if (CostumeId != 0)
    {
        CommCostume.PartIds[0] = 0;
        CommCostume.PartIds[1] = 0;
        CommCostume.PartIds[2] = 0;
        CommCostume.PartIds[3] = 0;
        CommCostume.AccessoryCount = 0;
        pub::GF::GetCostumeByID(CostumeId, CommCostume);
        CommCostumePtr = &CommCostume;
    }

    // System is overwritten with each player's system record. LastState (EDI in retail) is
    // cleared when a state has no ship, but not when it is missing.
    FContentPlayerState* LastState = nullptr;
    for (std::vector<unsigned int>::iterator It = PlayerIds.begin(); It != PlayerIds.end(); ++It)
    {
        LastState = FindContentPlayerState(*It);
        if (LastState == nullptr)
            continue;
        if (LastState->ShipObject == nullptr)
        {
            LastState = nullptr;
            continue;
        }
        System = LastState->GetSystemRecord();
        int const TextCount = static_cast<int>(TextIds.size());
        SendSpaceComm(System, Speaker, LastState->GetShipId(), Texts, &TextCount, VoiceId,
                      CaptionIds, CommCostumePtr, PermutationIds, 0.0f, false);
    }

    // Outside single player, with a last player: the delayed "line done" for its ship.
    if (pub::SinglePlayer() || LastState == nullptr)
        return;
    FGcsDelayedMessage Entry;
    Entry.Delay = static_cast<float>(static_cast<double>(GetDuration()) + 1.25);
    Entry.Listener = LastState->GetShipId();
    Entry.SystemId = System->SystemId;
    Entry.Speaker = Speaker;
    Entry.LastTextId = TextIds.back();
    Entry.Elapsed = 0.0f;
    GGcsConversations.DelayedMessages.insert(GGcsConversations.DelayedMessages.end(), 1, Entry);
}

bool FGcsPlayerListLine::IsDone(void const* Parms)
{
    // FUN_06ed6560. The ship lookup of word 0 is repeated for every player id.
    unsigned int const* Words = static_cast<unsigned int const*>(Parms);
    for (std::vector<unsigned int>::iterator It = PlayerIds.begin(); It != PlayerIds.end(); ++It)
    {
        FContentPlayerState* const State = FindContentPlayerStateByShip(&Words[0]);
        if (State != nullptr && State->PlayerId == *It && Speaker == Words[2] &&
            TextIds.back() == Words[3])
            return true;
    }
    return false;
}

FGcsCloneableLine* FGcsPlayerListLine::Clone()
{
    // FUN_06ed65c0.
    return new FGcsPlayerListLine(*this);
}

namespace
{
    // The inline faction step of the line ctors (e.g. 06ed9466): the ship's faction prefix,
    // "gcs_refer_faction_player" when the faction has no record, then "_short".
    TString<0x40> MakeShortFactionName(FContentSpaceObj* Ship)
    {
        TString<0x40> Name;
        Name.Length = 0;
        Name.Data[0] = '\0';
        if (GetFactionMsgIdPrefix(Ship->ReputationHandle->GetAffiliation(), &Name) != 0)
            Name = "gcs_refer_faction_player";
        AppendTString64(&Name, "_short");
        return Name;
    }

    // The inline singular / plural step of the guide lines (e.g. 06eda8cb): "_p"
    // (DAT_06fb6844) with formation wingmen, else "_s" (DAT_06fb6840); the third "%s" is
    // DAT_06fb572c ("").
    TString<0x40> MakeFormationVoiceLineName(FContentSpaceObj* Ship, char const* Name,
                                             char const* Suffix, unsigned int VoiceId)
    {
        bool const bPlural = HasFormationWingmen(Ship);
        TString<0x40> Format;
        Format.Length = 0;
        Format.Data[0] = '\0';
        FormatTString64(&Format, "%s%s%s", Name, bPlural ? "_p" : "_s", "");
        return MakeVoiceLineName(Format.Data, Suffix, VoiceId);
    }

    // The guide of an NPC ship (FUN_06f34ca0; the callers only reach it for ships whose guide
    // type they tested).
    FContentGuide* GetShipGuide(FContentSpaceObj* Ship)
    {
        return static_cast<FContentNpcShip*>(Ship)->GetGuide();
    }

    // The inline system step of the jump gate lines (e.g. 06ed8a0f); the prefix status is
    // ignored.
    TString<0x40> MakeSystemPrefixName(unsigned int SystemId)
    {
        TString<0x40> Name;
        Name.Length = 0;
        Name.Data[0] = '\0';
        pub::TranslateSystemToMsgIdPrefix(SystemId, Name);
        AppendTString64(&Name, "-");
        return Name;
    }
}

// ---------------------------------------------------------------------------------------
// Line classes
// ---------------------------------------------------------------------------------------

FGcsLineLaunchClearance::FGcsLineLaunchClearance(FSystemInfo* InSystem, unsigned int InSpeaker,
                                                 unsigned int InListener,
                                                 unsigned int TerminalType)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ed9330. Role 0x40 (dockrequest_dock).
    if (!HasVoiceRoles(VOICE_ROLE_DOCKREQUEST_DOCK))
        return;
    // The listener's ship is not null-tested; rand() / 32768 is always 0 (15-bit CRT rand).
    FContentSpaceObj* const Ship = System->FindShip(Listener);
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_9;
    TextIds.reserve(9);

    PushText(MakeShortFactionName(Ship).Data);
    PushText(MakeObjectReferenceName(kGcsObjectReferenceFormationDesig, Ship, nullptr).Data);
    PushText(MakeObjectReferenceName(kGcsObjectReferenceNumber, Ship, nullptr).Data);
    PushText("gcs_misc_dash");
    PushText(MakeObjectReferenceName(kGcsObjectReferenceRolledNumber, Ship, nullptr).Data);
    PushText("gcs_misc_thisis+");

    // The dock's own place (FUN_06eeb730 inlined): an empty name's id is still added.
    PushText(MakeObjectLocationName(Speaker).Data);

    // Any other terminal type leaves the clearance name empty.
    char const* Clearance = "";
    switch (TerminalType)
    {
    case TERM_BERTH:
        Clearance = "gcs_docklaunch_clear_berth";
        break;
    case TERM_MOOR_SMALL:
    case TERM_MOOR_MEDIUM:
    case TERM_MOOR_LARGE:
        Clearance = "gcs_docklaunch_clear_moor";
        break;
    case TERM_RING:
        Clearance = "gcs_docklaunch_clear_ring";
        break;
    default:
        break;
    }
    PushText(MakeVoiceLineName(Clearance, "-", VoiceId).Data);

    AddText(MakeVoiceLineName(MakeTString64("gcs_misc_wellwish").Data, "-", VoiceId));
}

FGcsLineAck::FGcsLineAck(FSystemInfo* InSystem, unsigned int InSpeaker,
                         unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06edf7a0: role 0x200 (misc_chatter).
    if (!HasVoiceRoles(VOICE_ROLE_MISC_CHATTER))
        return;
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_1;
    TextIds.reserve(1);
    PushText(MakeVoiceLineName("gcs_misc_ack", "-", VoiceId).Data);
}

FGcsLineDockRequest::FGcsLineDockRequest(FSystemInfo* InSystem, unsigned int InSpeaker,
                                         unsigned int InListener, unsigned int DockType)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ed6dd0: role 0x80 (dockrequest_docker).
    if (!HasVoiceRoles(VOICE_ROLE_DOCKREQUEST_DOCKER))
        return;
    // The speaker's own ship (not null-tested); the dock's lookup (FUN_06f73a40) is unused.
    FContentSpaceObj* const Ship = System->FindShip(Speaker);
    System->FindShipOrSolar(Listener);
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_10;
    TextIds.reserve(10);

    PushText("gcs_misc_thisis+");
    PushText(MakeShortFactionName(Ship).Data);
    PushText(MakeObjectReferenceName(kGcsObjectReferenceFormationDesig, Ship, nullptr).Data);
    PushText(MakeObjectReferenceName(kGcsObjectReferenceNumber, Ship, nullptr).Data);
    PushText("gcs_misc_dash");
    // Retail repeats kind 2 here (not 3).
    PushText(MakeObjectReferenceName(kGcsObjectReferenceNumber, Ship, nullptr).Data);
    PushText("gcs_misc_to");
    PushText(MakeObjectLocationName(Listener).Data);
    PushText(MakeVoiceLineName("gcs_dockrequest_request", "+", VoiceId).Data); // DAT_06fb62ec
    AddText(MakeDockApproachName(DockType, "-"));
}

FGcsLineDockHostile::FGcsLineDockHostile(FSystemInfo* InSystem, unsigned int InSpeaker,
                                         unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ed72e0: role 0x40.
    if (!HasVoiceRoles(VOICE_ROLE_DOCKREQUEST_DOCK))
        return;
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_1;
    TextIds.reserve(1);
    PushText(MakeVoiceLineName("gcs_dockrequest_responsehostile", "-", VoiceId).Data);
}

FGcsLineDockGranted::FGcsLineDockGranted(FSystemInfo* InSystem, unsigned int InSpeaker,
                                         unsigned int InListener, unsigned int DockType,
                                         int DockIndex)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ed74d0: role 0x40; the ship's lookup (FUN_06f73930) is unused. No rand() test.
    if (!HasVoiceRoles(VOICE_ROLE_DOCKREQUEST_DOCK))
        return;
    System->FindShip(Listener);
    if (DockType == TERM_RING)
    {
        // Landing.
        PermutationIds = IDS_COMM_ORDER_6;
        TextIds.reserve(6);
        PushText(MakeVoiceLineName("gcs_misc_ack", "-", VoiceId).Data);
        PushText("gcs_dockrequest_yourrequest+");
        PushText("gcs_dockrequest_toland");
        PushText(MakeVoiceLineName("gcs_dockrequest_granted", "-", VoiceId).Data);
        PushText(MakeVoiceLineName("gcs_dockrequest_proceed", "+", VoiceId).Data);
        PushText("gcs_dockrequest_toland-");
        return;
    }
    if (DockIndex <= 0x14)
    {
        // A terminal up to 20, its number (DockIndex + 1) at the end.
        PermutationIds = IDS_COMM_ORDER_7;
        TextIds.reserve(7);
        PushText(MakeVoiceLineName("gcs_misc_ack", "-", VoiceId).Data);
        PushText("gcs_dockrequest_yourrequest+");
        PushText(MakeDockApproachName(DockType, "").Data);          // DAT_06fb572c ""
        PushText(MakeVoiceLineName("gcs_dockrequest_granted", "-", VoiceId).Data);
        PushText(MakeVoiceLineName("gcs_dockrequest_proceed", "+", VoiceId).Data);
        PushText(MakeDockApproachName(DockType, "_number").Data);
        TString<0x40> Number;
        Number.Length = 0;
        Number.Data[0] = '\0';
        FormatTString64(&Number, "gcs_misc_number_%d%s", DockIndex + 1, "-");
        PushText(Number.Data);
        return;
    }
    // A terminal above 20, its number digit by digit (two digits).
    PermutationIds = IDS_COMM_ORDER_8;
    TextIds.reserve(8);
    PushText(MakeVoiceLineName("gcs_misc_ack", "-", VoiceId).Data);
    PushText("gcs_dockrequest_yourrequest+");
    PushText(MakeDockApproachName(DockType, "").Data);
    AddText(MakeVoiceLineName(MakeTString64("gcs_dockrequest_granted").Data, "-", VoiceId));
    AddText(MakeVoiceLineName(MakeTString64("gcs_dockrequest_proceed").Data, "+", VoiceId));
    AddText(MakeDockApproachName(DockType, "_number"));
    // The first two names are read without a size test (DockIndex + 1 > 21 has two digits
    // or more).
    std::vector<TString<0x40>> Digits;
    MakeNumberNames(DockIndex + 1, "-", &Digits);
    AddText(Digits[0]);
    AddText(Digits[1]);
}

FGcsLineDockStandby::FGcsLineDockStandby(FSystemInfo* InSystem, unsigned int InSpeaker,
                                         unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ed7dc0: role 0x40.
    if (!HasVoiceRoles(VOICE_ROLE_DOCKREQUEST_DOCK))
        return;
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_4;
    TextIds.reserve(4);
    PushText(MakeVoiceLineName("gcs_misc_ack", "-", VoiceId).Data);
    PushText(MakeVoiceLineName("gcs_dockrequest_standby", "-", VoiceId).Data);
    PushText(MakeVoiceLineName("gcs_dockrequest_delayedreason", "-", VoiceId).Data);
    PushText(MakeVoiceLineName("gcs_dockrequest_willbecleared", "-", VoiceId).Data);
}

FGcsLineDockDenied::FGcsLineDockDenied(FSystemInfo* InSystem, unsigned int InSpeaker,
                                       unsigned int InListener, unsigned int DockType)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ed80d0: role 0x40.
    if (!HasVoiceRoles(VOICE_ROLE_DOCKREQUEST_DOCK))
        return;
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_5;
    TextIds.reserve(5);
    PushText(MakeVoiceLineName("gcs_misc_ack", "-", VoiceId).Data);
    PushText("gcs_dockrequest_yourrequest+");
    PushText(MakeDockApproachName(DockType, "").Data);              // DAT_06fb572c ""
    PushText(MakeVoiceLineName("gcs_dockrequest_denied", "-", VoiceId).Data);
    PushText(MakeVoiceLineName("gcs_dockrequest_nofit", "-", VoiceId).Data);
}

FGcsLineDockNowCleared::FGcsLineDockNowCleared(FSystemInfo* InSystem, unsigned int InSpeaker,
                                               unsigned int InListener, unsigned int DockType)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ed8450: role 0x40; the ship is not null-tested.
    if (!HasVoiceRoles(VOICE_ROLE_DOCKREQUEST_DOCK))
        return;
    FContentSpaceObj* const Ship = System->FindShip(Listener);
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_7;
    TextIds.reserve(7);
    PushText(MakeShortFactionName(Ship).Data);
    PushText(MakeObjectReferenceName(kGcsObjectReferenceFormationDesig, Ship, nullptr).Data);
    PushText(MakeObjectReferenceName(kGcsObjectReferenceNumber, Ship, nullptr).Data);
    PushText("gcs_misc_dash");
    PushText(MakeObjectReferenceName(kGcsObjectReferenceRolledNumber, Ship, nullptr).Data);
    PushText(MakeVoiceLineName("gcs_dockrequest_nowcleared", "+", VoiceId).Data);
    AddText(MakeDockApproachName(DockType, "-"));
}

FGcsLineJumpGateOpen::FGcsLineJumpGateOpen(FSystemInfo* InSystem, unsigned int InSpeaker,
                                           unsigned int InListener, unsigned int TargetSystem)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ed8810: role 0x80.
    if (!HasVoiceRoles(VOICE_ROLE_DOCKREQUEST_DOCKER))
        return;
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_3;
    TextIds.reserve(3);
    PushText(MakeVoiceLineName("gcs_dockrequest_jumpgateopen", "-", VoiceId).Data);
    PushText(MakeVoiceLineName("gcs_dockrequest_goingto", "+", VoiceId).Data);
    PushText(MakeSystemPrefixName(TargetSystem).Data);
}

FGcsLineJumpGateDelayed::FGcsLineJumpGateDelayed(FSystemInfo* InSystem, unsigned int InSpeaker,
                                                 unsigned int InListener,
                                                 unsigned int TargetSystem)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ed8ac0: role 0x80.
    if (!HasVoiceRoles(VOICE_ROLE_DOCKREQUEST_DOCKER))
        return;
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_2;
    TextIds.reserve(2);
    PushText(MakeVoiceLineName("gcs_dockrequest_jumpgatedelayed", "-", VoiceId).Data);
    PushText(MakeSystemPrefixName(TargetSystem).Data);
}

FGcsLineJumpGateOpenAfterDelay::FGcsLineJumpGateOpenAfterDelay(FSystemInfo* InSystem,
                                                               unsigned int InSpeaker,
                                                               unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ed8d20.
    BuildSingleVoiceLine(VOICE_ROLE_DOCKREQUEST_DOCKER, "gcs_dockrequest_jumpgateopenafterdelay");
}

FGcsLineTradeLaneStart::FGcsLineTradeLaneStart(FSystemInfo* InSystem, unsigned int InSpeaker,
                                               unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ed8f10.
    BuildSingleVoiceLine(VOICE_ROLE_DOCKREQUEST_DOCKER, "gcs_dockrequest_tradelanestart");
}

FGcsLineThisOurStop::FGcsLineThisOurStop(FSystemInfo* InSystem, unsigned int InSpeaker,
                                         unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ed9100: role 0x80.
    if (!HasVoiceRoles(VOICE_ROLE_DOCKREQUEST_DOCKER))
        return;
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_2;
    TextIds.reserve(2);
    PushText(MakeVoiceLineName("gcs_dockrequest_thisourstop", "-", VoiceId).Data);
    PushText(MakeVoiceLineName("gcs_dockrequest_thankshelp", "-", VoiceId).Data);
}

FGcsLineDockHail::FGcsLineDockHail(FSystemInfo* InSystem, unsigned int InSpeaker,
                                   unsigned int InListener, float Attitude)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ed97e0: role 0x20 (hail_dock); the ship is not null-tested.
    if (!HasVoiceRoles(VOICE_ROLE_HAIL_DOCK))
        return;
    FContentShipBase* const Ship = static_cast<FContentShipBase*>(System->FindShip(Listener));

    // DAT_06fb6214 (-0.6f): at or below, or unordered, refuses; no rand() test on that side.
    if (!(Attitude > -0.6f))
    {
        PermutationIds = IDS_COMM_ORDER_4;
        TextIds.reserve(4);                                         // FUN_06eec150
        PushText("gcs_dockhail_dock_incoming+");
        TString<0x40> ShipPrefix;
        ShipPrefix.Length = 0;
        ShipPrefix.Data[0] = '\0';
        pub::TranslateShipToMsgIdPrefix(Ship->ShipArchId, ShipPrefix);
        AddText(ShipPrefix);
        AddText(MakeObjectReferenceName(kGcsObjectReferenceBusiness, Ship, nullptr));
        AddText(MakeVoiceLineName(MakeTString64("gcs_dhail_nopermission").Data, "-", VoiceId));
        return;
    }

    int const Pick = (rand() * 2) / 32768;
    if (Pick == 1)
    {
        PermutationIds = IDS_COMM_ORDER_7;
        TextIds.reserve(7);
        PushText("gcs_dockhail_dock_incoming+");
        TString<0x40> ShipPrefix;
        ShipPrefix.Length = 0;
        ShipPrefix.Data[0] = '\0';
        pub::TranslateShipToMsgIdPrefix(Ship->ShipArchId, ShipPrefix);
        PushText(ShipPrefix.Data);
        PushText(MakeObjectReferenceName(kGcsObjectReferenceBusiness, Ship, nullptr).Data);
        PushText("gcs_misc_thisis+");
        PushText(MakeObjectLocationName(Speaker).Data);             // FUN_06eeb730 inlined
        PushText(MakeVoiceLineName("gcs_dockhail_dock_hailmethod", "-", VoiceId).Data);
        PushText(MakeVoiceLineName("gcs_dockhail_dock_idreq", "-", VoiceId).Data);
        return;
    }
    if (Pick != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_6;
    TextIds.reserve(6);
    PushText("gcs_misc_thisis+");
    PushText(MakeObjectLocationName(Speaker).Data);                 // FUN_06eeb730 inlined
    PushText("gcs_misc_to");
    TString<0x40> ShipPrefix;
    ShipPrefix.Length = 0;
    ShipPrefix.Data[0] = '\0';
    pub::TranslateShipToMsgIdPrefix(Ship->ShipArchId, ShipPrefix);
    PushText(ShipPrefix.Data);
    PushText(MakeObjectReferenceName(kGcsObjectReferenceBusiness, Ship, nullptr).Data);
    PushText(MakeVoiceLineName("gcs_dockhail_dock_idreq", "-", VoiceId).Data);
}

FGcsLineDockHailReceived::FGcsLineDockHailReceived(FSystemInfo* InSystem, unsigned int InSpeaker,
                                                   unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ed9f70: role 0x20; the ship (not null-tested); no rand() test.
    if (!HasVoiceRoles(VOICE_ROLE_HAIL_DOCK))
        return;
    FContentSpaceObj* const Ship = System->FindShip(Listener);
    PermutationIds = IDS_COMM_ORDER_5;
    TextIds.reserve(5);
    PushText(MakeVoiceLineName("gcs_dockhail_dock_transreceived", "+", VoiceId).Data);
    PushText(MakeShortFactionName(Ship).Data);
    PushText(MakeObjectReferenceName(kGcsObjectReferenceFormationDesig, Ship, nullptr).Data);
    PushText(MakeObjectReferenceName(kGcsObjectReferenceNumber, Ship, "-").Data);          // DAT_06fb6064
    PushText(MakeVoiceLineName("gcs_dockhail_dock_whereheaded", "+", VoiceId).Data);
}

FGcsLineHaileeDataSubmit::FGcsLineHaileeDataSubmit(FSystemInfo* InSystem, unsigned int InSpeaker,
                                                   unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06eda290: role 0x10 (hail_hailee); the speaker's own ship.
    if (!HasVoiceRoles(VOICE_ROLE_HAIL_HAILEE))
        return;
    FContentSpaceObj* const Ship = System->FindShip(Speaker);
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_5;
    TextIds.reserve(5);
    PushText("gcs_misc_thisis+");
    // Retail calls GetFactionProps directly here; same outcome as FUN_06eeb190.
    PushText(MakeShortFactionName(Ship).Data);
    PushText(MakeObjectReferenceName(kGcsObjectReferenceFormationDesig, Ship, nullptr).Data);
    PushText(MakeObjectReferenceName(kGcsObjectReferenceNumber, Ship, "-").Data);
    PushText(MakeVoiceLineName("gcs_dockhail_hailee_datasubmit", "-", VoiceId).Data);
}

FGcsLineHaileeAngry::FGcsLineHaileeAngry(FSystemInfo* InSystem, unsigned int InSpeaker,
                                         unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06eda5a0.
    BuildSingleVoiceLine(VOICE_ROLE_HAIL_HAILEE, "gcs_dhail_angry_response");
}

FGcsLineGoingTo::FGcsLineGoingTo(FSystemInfo* InSystem, unsigned int InSpeaker,
                                 unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06eda790: role 0x10; the speaker's ship.
    if (!HasVoiceRoles(VOICE_ROLE_HAIL_HAILEE))
        return;
    FContentSpaceObj* const Ship = System->FindShip(Speaker);
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_2;
    TextIds.reserve(2);
    PushText(MakeFormationVoiceLineName(Ship, "gcs_misc_goingto", "+", VoiceId).Data);
    // The guide's second target (slot +0x34), its place.
    unsigned int TargetId1 = 0;
    unsigned int TargetId2 = 0;
    FContentBase* TargetBase1 = nullptr;
    FContentBase* TargetBase2 = nullptr;
    GetShipGuide(Ship)->GetTargets(&TargetId1, &TargetId2, &TargetBase1, &TargetBase2);
    PushText(MakeObjectLocationName(TargetId2).Data);
}

FGcsLinePatrolIdHome::FGcsLinePatrolIdHome(FSystemInfo* InSystem, unsigned int InSpeaker,
                                           unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06edab00: role 0x10; the speaker's ship.
    if (!HasVoiceRoles(VOICE_ROLE_HAIL_HAILEE))
        return;
    FContentSpaceObj* const Ship = System->FindShip(Speaker);
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_2;
    TextIds.reserve(2);
    PushText(MakeFormationVoiceLineName(Ship, "gcs_patrol_idhome", "+", VoiceId).Data);
    // The guide's first target (slot +0x34), its place.
    unsigned int TargetId1 = 0;
    unsigned int TargetId2 = 0;
    FContentBase* TargetBase1 = nullptr;
    FContentBase* TargetBase2 = nullptr;
    GetShipGuide(Ship)->GetTargets(&TargetId1, &TargetId2, &TargetBase1, &TargetBase2);
    PushText(MakeObjectLocationName(TargetId1).Data);
}

FGcsLinePatrolStatus::FGcsLinePatrolStatus(FSystemInfo* InSystem, unsigned int InSpeaker,
                                           unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06edae70: role 0x10.
    if (!HasVoiceRoles(VOICE_ROLE_HAIL_HAILEE))
        return;
    // The ship is not null-tested; callers only get here for guide type 3. Slot +0x4c is
    // LAB_06f0b2d0. No rand() test in either branch.
    FContentSpaceObj* const Ship = System->FindShip(Speaker);
    int const PointsToGo = static_cast<FPatrolGuide*>(GetShipGuide(Ship))->GetPathPointsToGo();
    if (PointsToGo == 0)
    {
        PermutationIds = IDS_COMM_ORDER_1;
        TextIds.reserve(1);
        PushText(MakeFormationVoiceLineName(Ship, "gcs_patrol_finished", "-", VoiceId).Data);
        return;
    }
    PermutationIds = IDS_COMM_ORDER_3;
    TextIds.reserve(3);
    char const* Have = nullptr;
    if (HasFormationWingmen(Ship))
        Have = PointsToGo == 1 ? "gcs_patrol_wehaveonly+" : "gcs_patrol_wehave+";
    else
        Have = PointsToGo == 1 ? "gcs_patrol_ihaveonly+" : "gcs_patrol_ihave+";
    PushText(Have);
    TString<0x40> Number;
    Number.Length = 0;
    Number.Data[0] = '\0';
    FormatTString64(&Number, "gcs_misc_number_%d%s", PointsToGo, "");
    PushText(Number.Data);
    PushText(PointsToGo == 1 ? "gcs_patrol_morewptogo-" : "gcs_patrol_morewpstogo-");
}

FGcsLineTradeStatus::FGcsLineTradeStatus(FSystemInfo* InSystem, unsigned int InSpeaker,
                                         unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06edb260: role 0x10; the speaker's ship.
    if (!HasVoiceRoles(VOICE_ROLE_HAIL_HAILEE))
        return;
    FContentSpaceObj* const Ship = System->FindShip(Speaker);
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_4;
    TextIds.reserve(4);

    // The trade guide's slot +0x4c (trade good), else slot +0x50 (return good).
    FTradeGuide* const Guide = static_cast<FTradeGuide*>(GetShipGuide(Ship));
    unsigned int const TradeGood = Guide->GetTradeGood();
    char const* Preposition = nullptr;
    TString<0x40> GoodPrefix;
    if (TradeGood != 0)
    {
        PushText(MakeFormationVoiceLineName(Ship, "gcs_trade_carrying", "+", VoiceId).Data);
        GoodPrefix.Length = 0;
        GoodPrefix.Data[0] = '\0';
        pub::TranslateGoodToMsgIdPrefix(TradeGood, GoodPrefix);
        PushText(GoodPrefix.Data);
        Preposition = "gcs_misc_to";
    }
    else
    {
        unsigned int const ReturnGood = Guide->GetReturnGood();
        if (ReturnGood == 0)
        {
            System = nullptr;
            return;
        }
        PushText(MakeFormationVoiceLineName(Ship, "gcs_trade_wanttoget", "+", VoiceId).Data);
        GoodPrefix.Length = 0;
        GoodPrefix.Data[0] = '\0';
        pub::TranslateGoodToMsgIdPrefix(ReturnGood, GoodPrefix);
        PushText(GoodPrefix.Data);
        Preposition = "gcs_misc_from";
    }
    PushText(Preposition);

    // The guide's second target (slot +0x34).
    unsigned int TargetId1 = 0;
    unsigned int TargetId2 = 0;
    FContentBase* TargetBase1 = nullptr;
    FContentBase* TargetBase2 = nullptr;
    GetShipGuide(Ship)->GetTargets(&TargetId1, &TargetId2, &TargetBase1, &TargetBase2);
    AddText(MakeObjectLocationName(TargetId2));
}

FGcsLineTradeIdHome::FGcsLineTradeIdHome(FSystemInfo* InSystem, unsigned int InSpeaker,
                                         unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06edb720: role 0x10; the speaker's ship.
    if (!HasVoiceRoles(VOICE_ROLE_HAIL_HAILEE))
        return;
    FContentSpaceObj* const Ship = System->FindShip(Speaker);
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_2;
    TextIds.reserve(2);
    PushText(MakeFormationVoiceLineName(Ship, "gcs_trade_idhome", "+", VoiceId).Data);
    // The guide's first target (slot +0x34), its place.
    unsigned int TargetId1 = 0;
    unsigned int TargetId2 = 0;
    FContentBase* TargetBase1 = nullptr;
    FContentBase* TargetBase2 = nullptr;
    GetShipGuide(Ship)->GetTargets(&TargetId1, &TargetId2, &TargetBase1, &TargetBase2);
    PushText(MakeObjectLocationName(TargetId1).Data);
}

FGcsLinePlayerHailBlowOff::FGcsLinePlayerHailBlowOff(FSystemInfo* InSystem,
                                                     unsigned int InSpeaker,
                                                     unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ee11c0: role 0x10.
    if (!HasVoiceRoles(VOICE_ROLE_HAIL_HAILEE))
        return;
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_1;
    TextIds.reserve(1);
    PushText(MakeVoiceLineName(MakeTString64("gcs_phail_blowoff").Data, "-", VoiceId).Data);
}

FGcsLinePlayerHailCombatBlowOff::FGcsLinePlayerHailCombatBlowOff(FSystemInfo* InSystem,
                                                                 unsigned int InSpeaker,
                                                                 unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ee13b0: FUN_06ee11c0 with its own name.
    if (!HasVoiceRoles(VOICE_ROLE_HAIL_HAILEE))
        return;
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_1;
    TextIds.reserve(1);
    PushText(MakeVoiceLineName(MakeTString64("gcs_phail_combatblowoff").Data, "-", VoiceId).Data);
}

FGcsLinePlayerHailIdentify::FGcsLinePlayerHailIdentify(FSystemInfo* InSystem,
                                                       unsigned int InSpeaker,
                                                       unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ee15a0: role 0x10; the ship is not null-tested.
    if (!HasVoiceRoles(VOICE_ROLE_HAIL_HAILEE))
        return;
    FContentSpaceObj* const Ship = System->FindShip(Speaker);
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_4;
    TextIds.reserve(4);
    PushText("gcs_misc_thisis+");
    // Retail calls GetFactionProps directly here; same outcome.
    PushText(MakeShortFactionName(Ship).Data);
    PushText(MakeObjectReferenceName(kGcsObjectReferenceFormationDesig, Ship, nullptr).Data);
    PushText(MakeObjectReferenceName(kGcsObjectReferenceNumber, Ship, "-").Data);
}

// ---------------------------------------------------------------------------------------
// Combat lines (FGcsCombatConversation)
// ---------------------------------------------------------------------------------------

namespace
{
    // The voice test of FUN_06edbf60 / FUN_06edc370: role 0x4000 for a ship (type &
    // 0x7f0000), 0x800 for anything else. GetType's status is not tested; retail leaves the
    // local uninitialised on failure, the port uses 0.
    bool HasCombatVoiceRoles(FGcsLine* Line)
    {
        pub::SpaceObj::GetVoiceID(Line->Speaker, Line->VoiceId);
        FVoiceProperties* const Voice = FindVoiceProperties(&Line->VoiceId);
        unsigned int Type = 0;
        pub::SpaceObj::GetType(Line->Speaker, Type);
        unsigned int const Roles = (Type & OBJ_SHIP_MASK) != 0 ? VOICE_ROLE_COMBAT_LEV_1
                                                             : VOICE_ROLE_COMBAT_DOCK;
        if (Voice == nullptr || !Voice->SupportsRoles(Roles))
        {
            Line->System = nullptr;
            return false;
        }
        return true;
    }
}

FGcsLineCombatDeathSympathy::FGcsLineCombatDeathSympathy(FSystemInfo* InSystem,
                                                         unsigned int InSpeaker,
                                                         unsigned int Unused)
    : FGcsLine(InSystem, InSpeaker, 0)
{
    // FUN_06edba90: +0x08 = 0 (the third argument is never read).
    (void)Unused;
    BuildSingleVoiceLine(VOICE_ROLE_COMBAT_LEV_1, "gcs_combat_deathsympathy");
}

FGcsLineCombatTakingLead::FGcsLineCombatTakingLead(FSystemInfo* InSystem, unsigned int InSpeaker,
                                                   unsigned int Unused)
    : FGcsLine(InSystem, InSpeaker, 0)
{
    // FUN_06edbc80: +0x08 = 0 (the third argument is never read); role 0x4; the ship is not
    // null-tested.
    (void)Unused;
    if (!HasVoiceRoles(VOICE_ROLE_COMBAT_LEV_2))
        return;
    FContentSpaceObj* const Ship = System->FindShip(Speaker);
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_6;
    TextIds.reserve(6);
    PushText("gcs_misc_thisis+");
    PushText(MakeObjectReferenceName(kGcsObjectReferenceFormationDesig, Ship, nullptr).Data);
    PushText(MakeObjectReferenceName(kGcsObjectReferenceNumber, Ship, nullptr).Data);
    PushText("gcs_misc_dash");
    PushText(MakeObjectReferenceName(kGcsObjectReferenceRolledNumber, Ship, nullptr).Data);
    PushText(MakeVoiceLineName(MakeTString64("gcs_combat_takinglead").Data, "-", VoiceId).Data);
}

FGcsLineCombatScream::FGcsLineCombatScream(FSystemInfo* InSystem, unsigned int InSpeaker,
                                           unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06edbf60.
    if (!HasCombatVoiceRoles(this))
        return;
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_1;
    TextIds.reserve(1);
    PushText(MakeVoiceLineName(MakeTString64("gcs_combat_scream").Data, "-", VoiceId).Data);
}

FGcsLineCombatBrag::FGcsLineCombatBrag(FSystemInfo* InSystem, unsigned int InSpeaker,
                                       unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06edc180.
    BuildSingleVoiceLine(VOICE_ROLE_COMBAT_LEV_1, "gcs_combat_brag");
}

FGcsLineCombatTakingDamage::FGcsLineCombatTakingDamage(FSystemInfo* InSystem,
                                                       unsigned int InSpeaker,
                                                       unsigned int InListener, float Health)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06edc370.
    if (!HasCombatVoiceRoles(this))
        return;
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_1;
    TextIds.reserve(1);
    // Double DAT_06fb6aa0 (0.49); unordered is not "below".
    char const* const Name = static_cast<double>(Health) < 0.49
                                 ? "gcs_combat_taking_damage_worst"
                                 : "gcs_combat_taking_damage";
    PushText(MakeVoiceLineName(MakeTString64(Name).Data, "-", VoiceId).Data);
}

FGcsLineCombatInflictingDamage::FGcsLineCombatInflictingDamage(FSystemInfo* InSystem,
                                                               unsigned int InSpeaker,
                                                               unsigned int InListener,
                                                               float Health)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06edc5d0: role 0x4000.
    if (!HasVoiceRoles(VOICE_ROLE_COMBAT_LEV_1))
        return;
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_1;
    TextIds.reserve(1);
    // Double DAT_06fb6b00 (0.33); unordered is not "below".
    char const* const Name = static_cast<double>(Health) < 0.33
                                 ? "gcs_combat_inflicting_damage_worst"
                                 : "gcs_combat_inflicting_damage";
    PushText(MakeVoiceLineName(MakeTString64(Name).Data, "-", VoiceId).Data);
}

FGcsLineCombatInSights::FGcsLineCombatInSights(FSystemInfo* InSystem, unsigned int InSpeaker,
                                               unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06edc800.
    BuildSingleVoiceLine(VOICE_ROLE_COMBAT_LEV_1, "gcs_combat_insights");
}

FGcsLineCombatHasInSights::FGcsLineCombatHasInSights(FSystemInfo* InSystem,
                                                     unsigned int InSpeaker,
                                                     unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06edc9f0.
    BuildSingleVoiceLine(VOICE_ROLE_COMBAT_LEV_1, "gcs_combat_hasinsights");
}

FGcsLineCombatRejoiningForm::FGcsLineCombatRejoiningForm(FSystemInfo* InSystem,
                                                         unsigned int InSpeaker,
                                                         unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06edcbe0.
    BuildSingleVoiceLine(VOICE_ROLE_COMBAT_LEV_1, "gcs_combat_rejoiningform");
}

FGcsLineCombatOrderDisengage::FGcsLineCombatOrderDisengage(FSystemInfo* InSystem,
                                                           unsigned int InSpeaker,
                                                           unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06edcdd0: role 0x4000.
    if (!HasVoiceRoles(VOICE_ROLE_COMBAT_LEV_1))
        return;
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_2;
    TextIds.reserve(2);
    PushText(MakeVoiceLineName(MakeTString64("gcs_combat_order_disengage").Data, "-",
                               VoiceId).Data);
    PushText(MakeVoiceLineName(MakeTString64("gcs_combat_announce_allclear").Data, "-",
                               VoiceId).Data);
}

FGcsLineCombatAllClear::FGcsLineCombatAllClear(FSystemInfo* InSystem, unsigned int InSpeaker,
                                               unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06edd000.
    BuildSingleVoiceLine(VOICE_ROLE_COMBAT_LEV_1, "gcs_combat_announce_allclear");
}

FGcsLineCombatResumeTrade::FGcsLineCombatResumeTrade(FSystemInfo* InSystem,
                                                     unsigned int InSpeaker,
                                                     unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06edd1f0: role 0xc004; the ship is not null-tested.
    if (!HasVoiceRoles(VOICE_ROLE_COMBAT_LEV_3))
        return;
    FContentSpaceObj* const Ship = System->FindShip(Speaker);
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_3;
    TextIds.reserve(3);
    PushText(MakeVoiceLineName(MakeTString64("gcs_combat_resume_trade").Data, "+",
                               VoiceId).Data);
    PushText("gcs_misc_to");
    // The guide's first target (slot +0x34), its place.
    unsigned int TargetId1 = 0;
    unsigned int TargetId2 = 0;
    FContentBase* TargetBase1 = nullptr;
    FContentBase* TargetBase2 = nullptr;
    GetShipGuide(Ship)->GetTargets(&TargetId1, &TargetId2, &TargetBase1, &TargetBase2);
    PushText(MakeObjectLocationName(TargetId1).Data);
}

FGcsLineCombatResumePatrol::FGcsLineCombatResumePatrol(FSystemInfo* InSystem,
                                                       unsigned int InSpeaker,
                                                       unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06edd500.
    BuildSingleVoiceLine(VOICE_ROLE_COMBAT_LEV_2, "gcs_combat_resume_patrol");
}

FGcsLineCombatEngaging::FGcsLineCombatEngaging(FSystemInfo* InSystem, unsigned int InSpeaker,
                                               unsigned int InTarget)
    : FGcsLine(InSystem, InSpeaker, 0)
{
    // FUN_06edd6f0: the listener is 0, NOT the target -- SendComm goes to receiver 0 and the
    // client plays it as chatter on channel B (no talking head, HUD_Comm 004ce26c). InTarget
    // is read only for the target's ship. Role 0x4.
    if (!HasVoiceRoles(VOICE_ROLE_COMBAT_LEV_2))
        return;
    // The speaker's ship lookup is unused.
    System->FindShip(Speaker);
    FContentSpaceObj* const Target = System->FindShip(InTarget);
    if (Target == nullptr)
    {
        System = nullptr;
        return;
    }
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_4;
    TextIds.reserve(4);
    PushText(MakeVoiceLineName(MakeTString64("gcs_combat_engaging").Data, "+", VoiceId).Data);
    PushText(MakeShortFactionName(Target).Data);
    if (TextIds.back() == 0)
    {
        System = nullptr;
        return;
    }
    PushText(MakeObjectReferenceName(kGcsObjectReferenceFormationDesig, Target, nullptr).Data);
    if (TextIds.back() == 0)
    {
        System = nullptr;
        return;
    }
    PushText(MakeObjectReferenceName(kGcsObjectReferenceNumber, Target, "-").Data);
    if (TextIds.back() == 0)
        System = nullptr;
}

FGcsLineCombatNoEngaging::FGcsLineCombatNoEngaging(FSystemInfo* InSystem,
                                                   unsigned int InSpeaker,
                                                   unsigned int InListener, int Reason)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06edda20: role 0x4.
    if (!HasVoiceRoles(VOICE_ROLE_COMBAT_LEV_2))
        return;
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_2;
    TextIds.reserve(2);
    PushText(MakeVoiceLineName(MakeTString64("gcs_combat_noengaging").Data, "+", VoiceId).Data);
    PushText(Reason == 2 ? "gcs_combat_noengagereason_tootough_01-"
                         : "gcs_combat_noengagereason_toofar_01-");
}

FGcsLineCombatAskEngage::FGcsLineCombatAskEngage(FSystemInfo* InSystem, unsigned int InSpeaker,
                                                 unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06eddc10.
    BuildSingleVoiceLine(VOICE_ROLE_COMBAT_LEV_2, "gcs_combat_askengage");
}

FGcsLineCombatOrderEngage::FGcsLineCombatOrderEngage(FSystemInfo* InSystem,
                                                     unsigned int InSpeaker,
                                                     unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06edde00.
    BuildSingleVoiceLine(VOICE_ROLE_COMBAT_LEV_1, "gcs_combat_order_engage");
}

FGcsLineCombatOrderNoEngage::FGcsLineCombatOrderNoEngage(FSystemInfo* InSystem,
                                                         unsigned int InSpeaker,
                                                         unsigned int InListener, int Reason)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06eddff0: FUN_06edda20 with "gcs_combat_order_noengage" and "-".
    if (!HasVoiceRoles(VOICE_ROLE_COMBAT_LEV_2))
        return;
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_2;
    TextIds.reserve(2);
    PushText(MakeVoiceLineName(MakeTString64("gcs_combat_order_noengage").Data, "-",
                               VoiceId).Data);
    PushText(Reason == 2 ? "gcs_combat_noengagereason_tootough_01-"
                         : "gcs_combat_noengagereason_toofar_01-");
}

FGcsLineCombatEnemySighted::FGcsLineCombatEnemySighted(FSystemInfo* InSystem,
                                                       unsigned int InSpeaker,
                                                       unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ede1e0.
    BuildSingleVoiceLine(VOICE_ROLE_COMBAT_LEV_1, "gcs_combat_announce_enemysighted");
}

FGcsLineCombatAskForHelp::FGcsLineCombatAskForHelp(FSystemInfo* InSystem,
                                                   unsigned int InSpeaker,
                                                   unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ede3d0: role 0x4; the listener's ship is not null-tested.
    if (!HasVoiceRoles(VOICE_ROLE_COMBAT_LEV_2))
        return;
    FContentSpaceObj* const Ship = System->FindShip(Listener);
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_4;
    TextIds.reserve(4);
    // Retail calls GetFactionProps directly here; same outcome.
    PushText(MakeShortFactionName(Ship).Data);
    PushText(MakeObjectReferenceName(kGcsObjectReferenceFormationDesig, Ship, nullptr).Data);
    PushText(MakeObjectReferenceName(kGcsObjectReferenceNumber, Ship, "-").Data);
    PushText(MakeVoiceLineName(MakeTString64("gcs_combat_askforhelp").Data, "-", VoiceId).Data);
}

FGcsLineCombatComingIn::FGcsLineCombatComingIn(FSystemInfo* InSystem, unsigned int InSpeaker,
                                               unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ede6c0: role 0x4; the ship is not null-tested.
    if (!HasVoiceRoles(VOICE_ROLE_COMBAT_LEV_2))
        return;
    FContentSpaceObj* const Ship = System->FindShip(Speaker);
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_2;
    TextIds.reserve(2);
    PushText(MakeVoiceLineName(MakeTString64("gcs_misc_ack").Data, "-", VoiceId).Data);
    PushText(MakeFormationVoiceLineName(Ship, "gcs_combat_comingin", "-", VoiceId).Data);
}

FGcsLineCombatOfferHelp::FGcsLineCombatOfferHelp(FSystemInfo* InSystem, unsigned int InSpeaker,
                                                 unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ede9b0: role 0x4; neither ship is null-tested.
    if (!HasVoiceRoles(VOICE_ROLE_COMBAT_LEV_2))
        return;
    FContentSpaceObj* const SpeakerShip = System->FindShip(Speaker);
    FContentSpaceObj* const ListenerShip = System->FindShip(Listener);
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_9;
    TextIds.reserve(9);
    PushText("gcs_misc_thisis+");
    PushText(MakeShortFactionName(SpeakerShip).Data);
    PushText(MakeObjectReferenceName(kGcsObjectReferenceFormationDesig, SpeakerShip, nullptr).Data);
    PushText(MakeObjectReferenceName(kGcsObjectReferenceNumber, SpeakerShip, nullptr).Data);
    PushText("gcs_misc_to");
    PushText(MakeShortFactionName(ListenerShip).Data);
    PushText(MakeObjectReferenceName(kGcsObjectReferenceFormationDesig, ListenerShip, nullptr).Data);
    PushText(MakeObjectReferenceName(kGcsObjectReferenceNumber, ListenerShip, "-").Data);
    PushText(MakeVoiceLineName(MakeTString64("gcs_combat_offerhelp").Data, "-", VoiceId).Data);
}

FGcsLineCombatRetreatTowards::FGcsLineCombatRetreatTowards(FSystemInfo* InSystem,
                                                           unsigned int InSpeaker,
                                                           unsigned int InListener, int Reason)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ededd0: role 0xc004; the ship is not null-tested.
    if (!HasVoiceRoles(VOICE_ROLE_COMBAT_LEV_3))
        return;
    FContentSpaceObj* const Ship = System->FindShip(Speaker);
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_4;
    TextIds.reserve(4);
    PushText(MakeVoiceLineName(MakeTString64("gcs_misc_attentionallwingmen").Data, "+",
                               VoiceId).Data);
    PushText(MakeVoiceLineName(MakeTString64("gcs_combat_order_retreattowards").Data, "+",
                               VoiceId).Data);
    // The guide's first target (slot +0x34), its place.
    unsigned int TargetId1 = 0;
    unsigned int TargetId2 = 0;
    FContentBase* TargetBase1 = nullptr;
    FContentBase* TargetBase2 = nullptr;
    GetShipGuide(Ship)->GetTargets(&TargetId1, &TargetId2, &TargetBase1, &TargetBase2);
    PushText(MakeObjectLocationName(TargetId1).Data);
    PushText(MakeFleeReasonName(Reason, VoiceId).Data);
}

FGcsLineCombatAnnounceFlee::FGcsLineCombatAnnounceFlee(FSystemInfo* InSystem,
                                                       unsigned int InSpeaker,
                                                       unsigned int InListener, int Reason)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06edf160: role 0x4.
    if (!HasVoiceRoles(VOICE_ROLE_COMBAT_LEV_2))
        return;
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_2;
    TextIds.reserve(2);
    PushText(MakeVoiceLineName(MakeTString64("gcs_combat_announce_flee").Data, "-",
                               VoiceId).Data);
    // FUN_06eec030 inlined.
    PushText(MakeFleeReasonName(Reason, VoiceId).Data);
}

FGcsLineCombatFriendlyFire::FGcsLineCombatFriendlyFire(FSystemInfo* InSystem,
                                                       unsigned int InSpeaker,
                                                       unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06edf3c0.
    BuildSingleVoiceLine(VOICE_ROLE_COMBAT_LEV_1, "gcs_combat_announce_ff");
}

FGcsLineCombatDrift::FGcsLineCombatDrift(FSystemInfo* InSystem, unsigned int InSpeaker,
                                         unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06edf5b0.
    BuildSingleVoiceLine(VOICE_ROLE_COMBAT_LEV_1, "gcs_combat_announce_combatdrift");
}

FGcsLineStationAllClear::FGcsLineStationAllClear(FSystemInfo* InSystem, unsigned int InSpeaker,
                                                 unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ee1830: role 0x800.
    if (!HasVoiceRoles(VOICE_ROLE_COMBAT_DOCK))
        return;
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_4;
    TextIds.reserve(4);
    PushText(MakeVoiceLineName(MakeTString64("gcs_combat_attentionallunits").Data, "-",
                               VoiceId).Data);
    PushText("gcs_misc_thisis+");
    PushText(MakeObjectLocationName(Speaker).Data);
    PushText(MakeVoiceLineName(MakeTString64("gcs_combat_announce_allclear").Data, "-",
                               VoiceId).Data);
}

FGcsLineStationEngage::FGcsLineStationEngage(FSystemInfo* InSystem, unsigned int InSpeaker,
                                             unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ee1b50: role 0x800.
    if (!HasVoiceRoles(VOICE_ROLE_COMBAT_DOCK))
        return;
    // An empty place ends the ctor with System kept and no text.
    TString<0x40> const Place = MakeObjectLocationName(Speaker);
    if (Place.Length == 0)
        return;
    int const Pick = (rand() * 2) / 32768;
    if (Pick == 1)
    {
        PermutationIds = IDS_COMM_ORDER_4;
        TextIds.reserve(4);
        PushText(MakeVoiceLineName(MakeTString64("gcs_combat_attentionallunits").Data, "-",
                                   VoiceId).Data);
        PushText("gcs_misc_thisis+");
        PushText(Place.Data);
        PushText(MakeVoiceLineName(MakeTString64("gcs_combat_announce_engage").Data, "-",
                                   VoiceId).Data);
        return;
    }
    if (Pick != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_1;
    TextIds.reserve(1);
    PushText(MakeVoiceLineName(MakeTString64("gcs_combat_manturrets").Data, "-", VoiceId).Data);
}

// ---------------------------------------------------------------------------------------
// Scan lines (FGcsScanConversation)
// ---------------------------------------------------------------------------------------

FGcsLineScanAnnounce::FGcsLineScanAnnounce(FSystemInfo* InSystem, unsigned int InSpeaker,
                                           unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ee5c20: role 0x8; neither ship is null-tested.
    if (!HasVoiceRoles(VOICE_ROLE_SCAN))
        return;
    FContentSpaceObj* const SpeakerShip = System->FindShip(Speaker);
    FContentSpaceObj* const ListenerShip = System->FindShip(Listener);
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_11;
    TextIds.reserve(11);
    PushText("gcs_misc_thisis+");
    PushText(MakeShortFactionName(SpeakerShip).Data);
    PushText(MakeObjectReferenceName(kGcsObjectReferenceFormationDesig, SpeakerShip, nullptr).Data);
    PushText(MakeObjectReferenceName(kGcsObjectReferenceNumber, SpeakerShip, nullptr).Data);
    PushText("gcs_misc_to");
    PushText(MakeShortFactionName(ListenerShip).Data);
    PushText(MakeObjectReferenceName(kGcsObjectReferenceFormationDesig, ListenerShip, nullptr).Data);
    PushText(MakeObjectReferenceName(kGcsObjectReferenceNumber, ListenerShip, nullptr).Data);
    PushText("gcs_misc_dash");
    PushText(MakeObjectReferenceName(kGcsObjectReferenceRolledNumber, ListenerShip, nullptr).Data);
    PushText(MakeVoiceLineName(MakeTString64("gcs_scan_announce").Data, "-", VoiceId).Data);
}

FGcsLineScanNothingFound::FGcsLineScanNothingFound(FSystemInfo* InSystem,
                                                   unsigned int InSpeaker,
                                                   unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ee6090.
    BuildSingleVoiceLine(VOICE_ROLE_SCAN, "gcs_scan_nothingfound");
}

FGcsLineScanOrderDrop::FGcsLineScanOrderDrop(FSystemInfo* InSystem, unsigned int InSpeaker,
                                             unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ee6280.
    BuildSingleVoiceLine(VOICE_ROLE_SCAN, "gcs_scan_orderdrop");
}

FGcsLineScanRefuse::FGcsLineScanRefuse(FSystemInfo* InSystem, unsigned int InSpeaker,
                                       unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ee6470.
    BuildSingleVoiceLine(VOICE_ROLE_SCAN, "gcs_scan_refuse");
}

FGcsLineScanComply::FGcsLineScanComply(FSystemInfo* InSystem, unsigned int InSpeaker,
                                       unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ee6660.
    BuildSingleVoiceLine(VOICE_ROLE_SCAN, "gcs_scan_comply");
}

FGcsLineScanAngry::FGcsLineScanAngry(FSystemInfo* InSystem, unsigned int InSpeaker,
                                     unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ee6850.
    BuildSingleVoiceLine(VOICE_ROLE_SCAN, "gcs_scan_angry");
}

FGcsLineScanHappy::FGcsLineScanHappy(FSystemInfo* InSystem, unsigned int InSpeaker,
                                     unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ee6a40.
    BuildSingleVoiceLine(VOICE_ROLE_SCAN, "gcs_scan_happy");
}

FGcsLineScanLootDestroyed::FGcsLineScanLootDestroyed(FSystemInfo* InSystem,
                                                     unsigned int InSpeaker,
                                                     unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ee6c30.
    BuildSingleVoiceLine(VOICE_ROLE_SCAN, "gcs_scan_lootdestroyed");
}

// ---------------------------------------------------------------------------------------
// Fidget lines (FGcsFidgetConversation)
// ---------------------------------------------------------------------------------------

FGcsLineFidgetTradeRumor::FGcsLineFidgetTradeRumor(FSystemInfo* InSystem, unsigned int InSpeaker,
                                                   unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06edf950.
    BuildSingleVoiceLine(VOICE_ROLE_FIDGET, "gcs_fidget_traderumor");
}

FGcsLineFidgetTradeIHear::FGcsLineFidgetTradeIHear(FSystemInfo* InSystem, unsigned int InSpeaker,
                                                   unsigned int InListener, unsigned int Good)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06edfb40: role 0x400.
    if (!HasVoiceRoles(VOICE_ROLE_FIDGET))
        return;
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_3;
    TextIds.reserve(3);
    PushText(MakeVoiceLineName(MakeTString64("gcs_fidget_trade_ihear").Data, "+", VoiceId).Data);
    // The prefix status is ignored.
    TString<0x40> GoodPrefix;
    GoodPrefix.Length = 0;
    GoodPrefix.Data[0] = '\0';
    pub::TranslateGoodToMsgIdPrefix(Good, GoodPrefix);
    PushText(GoodPrefix.Data);
    PushText(MakeVoiceLineName(MakeTString64("gcs_fidget_talkcargo").Data, "-", VoiceId).Data);
}

FGcsLineFidgetTradeSimple::FGcsLineFidgetTradeSimple(FSystemInfo* InSystem,
                                                     unsigned int InSpeaker,
                                                     unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06edfde0.
    BuildSingleVoiceLine(VOICE_ROLE_FIDGET, "gcs_fidget_trade_simple");
}

FGcsLineFidgetStatusQuery::FGcsLineFidgetStatusQuery(FSystemInfo* InSystem,
                                                     unsigned int InSpeaker,
                                                     unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06edffd0: role 0x400; the ship is not null-tested.
    if (!HasVoiceRoles(VOICE_ROLE_FIDGET))
        return;
    FContentSpaceObj* const Ship = System->FindShip(Speaker);
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_8;
    TextIds.reserve(8);
    PushText(MakeVoiceLineName(MakeTString64("gcs_misc_attentionallwingmen").Data, "+", VoiceId)
                 .Data);
    PushText("gcs_misc_thisis+");
    PushText(MakeShortFactionName(Ship).Data);
    PushText(MakeObjectReferenceName(kGcsObjectReferenceFormationDesig, Ship, nullptr).Data);
    PushText(MakeObjectReferenceName(kGcsObjectReferenceNumber, Ship, nullptr).Data);
    PushText("gcs_misc_dash");
    PushText(MakeObjectReferenceName(kGcsObjectReferenceRolledNumber, Ship, nullptr).Data);
    PushText(MakeVoiceLineName(MakeTString64("gcs_fidget_whatstatus").Data, "-", VoiceId).Data);
}

FGcsLineFidgetStatusReport::FGcsLineFidgetStatusReport(FSystemInfo* InSystem,
                                                       unsigned int InSpeaker,
                                                       unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ee0390: role 0x400; the ship is not null-tested.
    if (!HasVoiceRoles(VOICE_ROLE_FIDGET))
        return;
    FContentSpaceObj* const Ship = System->FindShip(Speaker);
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_6;
    TextIds.reserve(6);
    PushText("gcs_misc_thisis+");
    PushText(MakeObjectReferenceName(kGcsObjectReferenceFormationDesig, Ship, nullptr).Data);
    PushText(MakeObjectReferenceName(kGcsObjectReferenceNumber, Ship, nullptr).Data);
    PushText("gcs_misc_dash");
    PushText(MakeObjectReferenceName(kGcsObjectReferenceRolledNumber, Ship, nullptr).Data);
    // Retail reuses the local that held the ship pointer for the health (status not tested),
    // so a failing call leaves the pointer's bits.
    unsigned int const ShipBits =
        static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(Ship));
    float Health = 0.0f;
    std::memcpy(&Health, &ShipBits, sizeof(Health));
    pub::SpaceObj::GetRelativeHealth(Ship->SpaceObjId, Health);
    // Double DAT_06fb7120 (0.3) / DAT_06fb70f0 (0.75) / DAT_06fb70c0 (1.0); unordered falls
    // through to "none".
    char const* Name = "gcs_fidget_saystatus_damagenone";
    if (static_cast<double>(Health) < 0.3)
        Name = "gcs_fidget_saystatus_damagelarge";
    else if (static_cast<double>(Health) < 0.75)
        Name = "gcs_fidget_saystatus_damagemedium";
    else if (static_cast<double>(Health) < 1.0)
        Name = "gcs_fidget_saystatus_damagesmall";
    PushText(MakeVoiceLineName(Name, "-", VoiceId).Data);
}

FGcsLineFidgetPatrolRumor::FGcsLineFidgetPatrolRumor(FSystemInfo* InSystem,
                                                     unsigned int InSpeaker,
                                                     unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ee06d0.
    BuildSingleVoiceLine(VOICE_ROLE_FIDGET, "gcs_fidget_patrolrumor");
}

FGcsLineFidgetPatrolSimple::FGcsLineFidgetPatrolSimple(FSystemInfo* InSystem,
                                                       unsigned int InSpeaker,
                                                       unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ee08c0.
    BuildSingleVoiceLine(VOICE_ROLE_FIDGET, "gcs_fidget_patrolsimple");
}

FGcsLineFidgetWaypointQuery::FGcsLineFidgetWaypointQuery(FSystemInfo* InSystem,
                                                         unsigned int InSpeaker,
                                                         unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ee0ab0: role 0x400. Retail looks the ship up (not null-tested) between GetVoiceID
    // and the voice record; both are plain lookups, so the port asks after the voice test.
    if (!HasVoiceRoles(VOICE_ROLE_FIDGET))
        return;
    FContentSpaceObj* const Ship = System->FindShip(Speaker);
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_7;
    TextIds.reserve(7);
    PushText("gcs_misc_thisis+");
    PushText(MakeShortFactionName(Ship).Data);
    PushText(MakeObjectReferenceName(kGcsObjectReferenceFormationDesig, Ship, nullptr).Data);
    PushText(MakeObjectReferenceName(kGcsObjectReferenceNumber, Ship, nullptr).Data);
    PushText("gcs_misc_dash");
    PushText(MakeObjectReferenceName(kGcsObjectReferenceRolledNumber, Ship, nullptr).Data);
    PushText(MakeVoiceLineName(MakeTString64("gcs_fidget_patrol_wpremainquery").Data, "-",
                               VoiceId)
                 .Data);
}

FGcsLineFidgetPatrolStatus::FGcsLineFidgetPatrolStatus(FSystemInfo* InSystem,
                                                       unsigned int InSpeaker,
                                                       unsigned int InListener)
    : FGcsLine(InSystem, InSpeaker, InListener)
{
    // FUN_06ee0e10: role 0x400; the ship lookup is moved after the voice test as in
    // FUN_06ee0ab0. Slot +0x4c is LAB_06f0b2d0.
    if (!HasVoiceRoles(VOICE_ROLE_FIDGET))
        return;
    FContentSpaceObj* const Ship = System->FindShip(Speaker);
    int const PointsToGo = static_cast<FPatrolGuide*>(GetShipGuide(Ship))->GetPathPointsToGo();
    if (PointsToGo == 0)
    {
        // No rand() test on this branch.
        PermutationIds = IDS_COMM_ORDER_1;
        TextIds.reserve(1);
        PushText(MakeFormationVoiceLineName(Ship, "gcs_patrol_finished", "-", VoiceId).Data);
        return;
    }
    if (rand() / 32768 != 0)
        return;
    PermutationIds = IDS_COMM_ORDER_3;
    TextIds.reserve(3);
    PushText(PointsToGo == 1 ? "gcs_patrol_wehaveonly+" : "gcs_patrol_wehave+");
    TString<0x40> Number;
    Number.Length = 0;
    Number.Data[0] = '\0';
    FormatTString64(&Number, "gcs_misc_number_%d%s", PointsToGo, "");
    PushText(Number.Data);
    PushText(PointsToGo == 1 ? "gcs_patrol_morewptogo-" : "gcs_patrol_morewpstogo-");
}

// =======================================================================================
// Conversations
// =======================================================================================

FGcsConversation::~FGcsConversation()
{
    // FUN_06ee2b70.
    while (!Lines.empty())
    {
        if (Lines.front() != nullptr)
            delete Lines.front();
        Lines.erase(Lines.begin());
    }
}

void FGcsConversation::Start()
{
    // LAB_06ee1f40.
    if (!Lines.empty())
        Lines.front()->Play();
}

bool FGcsConversation::OnMessage(void const* Parms)
{
    // FUN_06ee1f60.
    for (std::vector<FGcsLine*>::iterator It = Lines.begin(); It != Lines.end(); ++It)
    {
        if (!(*It)->IsDone(Parms))
            continue;
        if (*It != nullptr)
            delete *It;
        It = Lines.erase(It);
        if (It != Lines.end() && (*It)->CanPlay())
        {
            (*It)->Play();
            return true;
        }
        bFinished = true;
        return true;
    }
    return false;
}

bool FGcsConversation::IsFinished()
{
    // LAB_06ed6da0.
    return bFinished;
}

bool FGcsConversation::AddLine(FGcsLine* Line)
{
    // The inline step of the conversation ctors (e.g. 06ee89a5).
    if (Line->CanPlay())
    {
        Lines.insert(Lines.end(), 1, Line);
        return true;
    }
    if (Line != nullptr)
        delete Line;
    return false;
}

// ---------------------------------------------------------------------------------------
// Conversation classes
// ---------------------------------------------------------------------------------------

FGcsLaunchConversation::FGcsLaunchConversation(FGcsLaunchEvent const* Event)
    : System(Event->System)
    , Speaker(Event->Speaker)
    , Listener(Event->Listener)
    , DockIndex(Event->DockIndex)
    , TerminalType(Event->TerminalType)
{
    // FUN_06ee8880.
    Lines.reserve(2);
    if (!AddLine(new FGcsLineLaunchClearance(System, Speaker, Listener, TerminalType)))
    {
        System = nullptr;
        return;
    }
    if (FindContentPlayerStateByShip(&Listener) != nullptr)
        return;
    if (!AddLine(new FGcsLineAck(System, Listener, Speaker)))
        System = nullptr;
}

bool FGcsLaunchConversation::IsValid()
{
    // LAB_06ee8870.
    return System != nullptr;
}

FGcsPlayerHailConversation::FGcsPlayerHailConversation(FGcsPlayerObjectEvent const* Event)
    : System(Event->System)
    , Player(Event->Player)
    , Object(Event->Object)
{
    // FUN_06ee4cd0.
    Lines.reserve(2);

    // The leader is not null-tested.
    FContentShipBase* Leader = static_cast<FContentShipBase*>(Object);
    if (Leader->FormationId != GNoFormationId)
        Leader = FindFormationLeader(Leader->FormationId);

    // DAT_06fb6214 (-0.6f): below, or unordered, -> no line.
    int LeaderRep = Leader->ReputationHandle->GetReputation();
    int PlayerRep = Player->GetReputation();
    float Attitude = 0.0f;
    pub::Reputation::GetAttitude(LeaderRep, PlayerRep, Attitude);
    if (!(Attitude >= -0.6f))
        return;
    FContentNpcShip* const Ship = static_cast<FContentNpcShip*>(Leader);
    if (!Ship->IsSpacePopShip())
        return;

    // The blow-off paths read the player's ship id directly; the identify path goes through
    // FUN_06f481e0 (GetShipId).
    FGcsLine* Line = nullptr;
    if (Leader->IsInCombat())
    {
        Line = new FGcsLinePlayerHailCombatBlowOff(System, Leader->SpaceObjId,
                                                   Player->ShipObject->SpaceObjId);
    }
    else
    {
        // The same player again within DAT_06fce370 (15.0) s of its last hail.
        bool bBlowOff = false;
        if (Player == Ship->LastHailPlayer)
        {
            double const Now = GetContentGameTime();
            double const LastHail = Ship->LastHailTime;
            bBlowOff = GetContentTimeDistance(&LastHail, &Now) < 15.0;
        }
        Ship->SetLastHailPlayer(Player);
        if (bBlowOff)
        {
            Line = new FGcsLinePlayerHailBlowOff(System, Leader->SpaceObjId,
                                                 Player->ShipObject->SpaceObjId);
        }
        else
        {
            if (!AddLine(new FGcsLinePlayerHailIdentify(System, Leader->SpaceObjId,
                                                        Player->GetShipId())))
            {
                System = nullptr;
                return;
            }

            unsigned int const PlayerShip = Player->ShipObject->SpaceObjId;
            switch (Ship->GetGuideType())
            {
            case GUIDE_TYPE_TRADE:
            {
                // Trade home, then what it trades; failing that (deleted, System kept) where
                // it is going.
                if (!AddLine(new FGcsLineTradeIdHome(System, Leader->SpaceObjId, PlayerShip)))
                {
                    System = nullptr;
                    return;
                }
                FGcsLine* const Status =
                    new FGcsLineTradeStatus(System, Leader->SpaceObjId, PlayerShip);
                if (Status->CanPlay())
                {
                    Lines.insert(Lines.end(), 1, Status);
                    return;
                }
                delete Status;
                Line = new FGcsLineGoingTo(System, Leader->SpaceObjId, PlayerShip);
                break;
            }
            case GUIDE_TYPE_PATROL_PATH:
                if (!AddLine(new FGcsLinePatrolIdHome(System, Leader->SpaceObjId, PlayerShip)) ||
                    !AddLine(new FGcsLineGoingTo(System, Leader->SpaceObjId, PlayerShip)))
                {
                    System = nullptr;
                    return;
                }
                Line = new FGcsLinePatrolStatus(System, Leader->SpaceObjId, PlayerShip);
                break;
            case GUIDE_TYPE_WANDER:
                // Wander: home only.
                Line = new FGcsLinePatrolIdHome(System, Leader->SpaceObjId, PlayerShip);
                break;
            default:
                return;
            }
        }
    }

    if (!AddLine(Line))
        System = nullptr;
}

bool FGcsPlayerHailConversation::IsValid()
{
    // LAB_06ee8870.
    return System != nullptr;
}

namespace
{
    // The inline "formation has wingmen" test of the combat cases (e.g. 06ee3078).
    bool HasFormationMembers(FContentShipBase* Ship)
    {
        std::vector<FContentShipBase*> Members;
        CollectFormationMembers(&Members, Ship->FormationId);
        return Members.size() > 1;
    }

    // The inline wingman test (e.g. 06ee30d6); FormationSlot is +0x14c.
    bool IsFormationWingman(FContentShipBase* Ship)
    {
        return Ship->FormationSlot != 0 && Ship->FormationId != GNoFormationId;
    }
}

FGcsCombatConversation::FGcsCombatConversation(FGcsCombatEvent const* Event)
    : System(Event->System)
{
    // FUN_06ee2c20 (no room reserved). Any type outside 6..0x10 adds nothing.
    unsigned int const ObjectA = Event->ObjectA;
    unsigned int const ObjectB = Event->ObjectB;
    switch (Event->GetType())
    {
    case GCS_EVENT_DAMAGED:
    {
        // Both types, 0 when GetType fails (B first).
        float const Health = static_cast<FGcsCombatDamageEvent const*>(Event)->Health;
        unsigned int TypeB = 0;
        if (pub::SpaceObj::GetType(ObjectB, TypeB) != 0)
            TypeB = 0;
        unsigned int TypeA = 0;
        if (pub::SpaceObj::GetType(ObjectA, TypeA) != 0)
            TypeA = 0;
        FGcsLine* Line = nullptr;
        if ((TypeA & OBJ_SHIP_MASK) != 0)
        {
            // A ship hit a ship: the rand pick decides who speaks.
            if ((TypeB & OBJ_SHIP_MASK) == 0)
                return;
            if ((rand() * 2) / 32768 != 0)
                Line = new FGcsLineCombatInflictingDamage(System, ObjectA, 0, Health);
            else
                Line = new FGcsLineCombatTakingDamage(System, ObjectB, 0, Health);
        }
        else
        {
            // The attacker is no ship: type & 0xfdff and a ship victim.
            if ((TypeA & OBJ_SOLAR_NO_WAYPOINT_MASK) == 0)
                return;
            if ((TypeB & OBJ_SHIP_MASK) == 0)
                return;
            Line = new FGcsLineCombatTakingDamage(System, ObjectB, 0, Health);
        }
        AddLineOrClear(Line);
        return;
    }
    case GCS_EVENT_KILLED:
    {
        // The killer's and the victim's ships.
        FContentSpaceObj* const ShipA = System->FindShip(ObjectA);
        FContentSpaceObj* const ShipB = System->FindShip(ObjectB);
        bool bBrag = false;
        if (ShipB == nullptr)
        {
            if (ShipA == nullptr)
            {
                System = nullptr;
                return;
            }
            bBrag = true;
        }
        else if (ShipA != nullptr)
        {
            int const Pick = (rand() * 2) / 32768;
            if (Pick == 1)
            {
                bBrag = true;
            }
            else if (Pick != 0)
            {
                System = nullptr;
                return;
            }
        }
        FGcsLine* Line = nullptr;
        if (bBrag)
        {
            Line = new FGcsLineCombatBrag(System, ObjectA, 0);
        }
        else if ((rand() * 2) / 32768 != 0)
        {
            Line = new FGcsLineCombatScream(System, ObjectB, 0);
        }
        else
        {
            // The victim's formation leader (FUN_06f57640) mourns it; the victim leading
            // screams itself.
            FContentShipBase* const Leader =
                static_cast<FContentShipBase*>(ShipB)->GetFormationLeader();
            if (Leader->SpaceObjId == ObjectB)
                Line = new FGcsLineCombatScream(System, ObjectB, 0);
            else
                Line = new FGcsLineCombatDeathSympathy(System, Leader->SpaceObjId, ObjectB);
        }
        AddLineOrClear(Line);
        return;
    }
    case GCS_EVENT_IN_SIGHTS:
    {
        // Who is in whose sights.
        FContentSpaceObj* const ObjA = System->FindSpaceObj(ObjectA);
        FContentSpaceObj* const ObjB = System->FindSpaceObj(ObjectB);
        FGcsLine* Line = nullptr;
        if (ObjA == nullptr)
            Line = new FGcsLineCombatInSights(System, ObjectB, 0);
        else if (ObjB == nullptr)
            Line = new FGcsLineCombatHasInSights(System, ObjectA, 0);
        else if ((rand() * 2) / 32768 != 0)
            Line = new FGcsLineCombatHasInSights(System, ObjectA, 0);
        else
            Line = new FGcsLineCombatInSights(System, ObjectB, 0);
        AddLineOrClear(Line);
        return;
    }
    case GCS_EVENT_ENGAGE:
    {
        // A ship engages ObjectB.
        FContentSpaceObj* const Object = System->FindSpaceObj(ObjectA);
        if (Object == nullptr)
        {
            // No such object here (a station): its place name must not be empty.
            if (MakeObjectLocationName(ObjectA).Length == 0)
            {
                System = nullptr;
                return;
            }
            AddLineOrClear(new FGcsLineStationEngage(System, ObjectA, 0));
            return;
        }
        FContentShipBase* const Ship = static_cast<FContentShipBase*>(Object);
        // A lone ship says nothing.
        if (!HasFormationMembers(Ship))
            return;
        if (IsFormationWingman(Ship))
        {
            // A wingman engages unless its leader's id is a multiple of 5.
            if (Ship->GetFormationLeader()->SpaceObjId % 5 == 0)
                return;
            AddLineOrClear(new FGcsLineCombatEngaging(System, Ship->SpaceObjId, ObjectB));
            return;
        }
        if (Ship->SpaceObjId % 5 != 0)
        {
            AddLineOrClear(new FGcsLineCombatEngaging(System, Ship->SpaceObjId, ObjectB));
            return;
        }
        // The leader announces the enemy (or a wingman asks to engage), then the order.
        if (!AddEnemySightedLine(Ship))
            return;
        if (!AddLineOrClear(new FGcsLineCombatOrderEngage(System, Ship->SpaceObjId, 0)))
            return;
        // On the rand pick the wingmen acknowledge.
        if ((rand() * 2) / 32768 == 0)
            return;
        AddFormationAcks(Ship);
        return;
    }
    case GCS_EVENT_COMBAT_OVER:
    {
        // The fight is over.
        FContentSpaceObj* const Object = System->FindSpaceObj(ObjectA);
        if (Object == nullptr)
        {
            // A station: its place name must not be empty.
            if (MakeObjectLocationName(ObjectA).Length == 0)
            {
                System = nullptr;
                return;
            }
            AddLineOrClear(new FGcsLineStationAllClear(System, ObjectA, 0));
            return;
        }
        FContentShipBase* const Ship = static_cast<FContentShipBase*>(Object);
        if (!HasFormationMembers(Ship))
        {
            AddLineOrClear(new FGcsLineCombatAllClear(System, Ship->SpaceObjId, 0));
            return;
        }
        if (IsFormationWingman(Ship))
        {
            // Unless the leader's id is a multiple of 5, reason 3 rejoins the formation, any
            // other announces all clear.
            if (Ship->GetFormationLeader()->SpaceObjId % 5 == 0)
                return;
            int const Reason = static_cast<FGcsCombatOverEvent const*>(Event)->Reason;
            if (Reason == 3)
                AddLineOrClear(new FGcsLineCombatRejoiningForm(System, Ship->SpaceObjId, 0));
            else
                AddLineOrClear(new FGcsLineCombatAllClear(System, Ship->SpaceObjId, 0));
            return;
        }
        if (!static_cast<FContentNpcShip*>(Object)->IsSpacePopShip() ||
            Ship->SpaceObjId % 5 != 0)
        {
            AddLineOrClear(new FGcsLineCombatAllClear(System, Ship->SpaceObjId, 0));
            return;
        }
        if (!AddLineOrClear(new FGcsLineCombatOrderDisengage(System, Ship->SpaceObjId, 0)))
            return;
        // Guide type 2 resumes trade, 3 and 4 patrol, the rest nothing.
        int const GuideType = static_cast<FContentNpcShip*>(Object)->GetGuideType();
        if (GuideType == GUIDE_TYPE_TRADE)
        {
            if (!AddLineOrClear(new FGcsLineCombatResumeTrade(System, Ship->SpaceObjId, 0)))
                return;
        }
        else if (GuideType > GUIDE_TYPE_TRADE && GuideType <= GUIDE_TYPE_WANDER)
        {
            if (!AddLineOrClear(new FGcsLineCombatResumePatrol(System, Ship->SpaceObjId, 0)))
                return;
        }
        if ((rand() * 2) / 32768 == 0)
            return;
        AddFormationAcks(Ship);
        return;
    }
    case GCS_EVENT_NO_ENGAGE:
    {
        // A ship does not engage.
        FContentSpaceObj* const Object = System->FindSpaceObj(ObjectA);
        if (Object == nullptr)
            return;
        FContentShipBase* const Ship = static_cast<FContentShipBase*>(Object);
        if (!HasFormationMembers(Ship))
            return;
        int const Reason = static_cast<FGcsCombatNoEngageEvent const*>(Event)->Reason;
        bool bOrders = false;
        if (IsFormationWingman(Ship))
        {
            if (Ship->GetFormationLeader()->SpaceObjId % 5 == 0)
                return;
        }
        else
        {
            bOrders = Ship->SpaceObjId % 5 == 0;
        }
        if (!bOrders)
        {
            // The enemy, then why not.
            if (!AddLineOrClear(new FGcsLineCombatEnemySighted(System, Ship->SpaceObjId, 0)))
                return;
            AddLineOrClear(new FGcsLineCombatNoEngaging(System, Ship->SpaceObjId, 0, Reason));
            return;
        }
        if (!AddEnemySightedLine(Ship))
            return;
        if (!AddLineOrClear(
                new FGcsLineCombatOrderNoEngage(System, Ship->SpaceObjId, 0, Reason)))
        {
            return;
        }
        if ((rand() * 2) / 32768 == 0)
            return;
        AddFormationAcks(Ship);
        return;
    }
    case GCS_EVENT_HELP:
    {
        // Two NPC ships, A no wingman.
        FContentSpaceObj* const ObjA = System->FindSpaceObj(ObjectA);
        if (ObjA == nullptr || !static_cast<FContentShipBase*>(ObjA)->IsNpcShip())
            return;
        if (IsFormationWingman(static_cast<FContentShipBase*>(ObjA)))
            return;
        FContentSpaceObj* const ObjB = System->FindSpaceObj(ObjectB);
        if (ObjB == nullptr || !static_cast<FContentShipBase*>(ObjB)->IsNpcShip())
            return;
        if ((rand() * 2) / 32768 != 0)
        {
            // A offers B its help.
            AddLineOrClear(new FGcsLineCombatOfferHelp(System, ObjectA, ObjectB));
            return;
        }
        // B asks A for help, A comes in.
        if (!AddLineOrClear(new FGcsLineCombatAskForHelp(System, ObjectB, ObjectA)))
            return;
        AddLineOrClear(new FGcsLineCombatComingIn(System, ObjectA, ObjectB));
        return;
    }
    case GCS_EVENT_DRIFT:
        AddLineOrClear(new FGcsLineCombatDrift(System, ObjectA, 0));
        return;
    case GCS_EVENT_FLEE:
    {
        // A ship flees (the object is not null-tested).
        FContentShipBase* const Ship =
            static_cast<FContentShipBase*>(System->FindSpaceObj(ObjectA));
        int const Reason = static_cast<FGcsCombatFleeEvent const*>(Event)->Reason;
        // Alone it announces its flight (id through FUN_06ee4cc0).
        if (!HasFormationMembers(Ship))
        {
            AddLineOrClear(new FGcsLineCombatAnnounceFlee(System, Ship->SpaceObjId, 0, Reason));
            return;
        }
        if (IsFormationWingman(Ship))
        {
            if (IsSpaceObjIdMultipleOf5(Ship->GetFormationLeader()))
                return;
            AddLineOrClear(new FGcsLineCombatAnnounceFlee(System, Ship->SpaceObjId, 0, Reason));
            return;
        }
        if (!static_cast<FContentNpcShip*>(Ship)->IsSpacePopShip() ||
            Ship->SpaceObjId % 5 != 0)
        {
            AddLineOrClear(new FGcsLineCombatAnnounceFlee(System, Ship->SpaceObjId, 0, Reason));
            return;
        }
        // The leader orders the retreat.
        if (!AddLineOrClear(
                new FGcsLineCombatRetreatTowards(System, Ship->SpaceObjId, 0, Reason)))
        {
            return;
        }
        if ((rand() * 2) / 32768 == 0)
            return;
        AddFormationAcks(Ship);
        return;
    }
    case GCS_EVENT_FRIENDLY_FIRE:
        // Friendly fire: B speaks to A.
        if (System->FindSpaceObj(ObjectB) == nullptr)
            return;
        AddLineOrClear(new FGcsLineCombatFriendlyFire(System, ObjectB, ObjectA));
        return;
    case GCS_EVENT_LEADER_CHANGED:
        AddLineOrClear(new FGcsLineCombatTakingLead(System, ObjectA, 0));
        return;
    default:
        return;
    }
}

bool FGcsCombatConversation::IsValid()
{
    // LAB_06ee8870.
    return System != nullptr;
}

bool FGcsCombatConversation::AddLineOrClear(FGcsLine* Line)
{
    if (Line->CanPlay())
    {
        Lines.insert(Lines.end(), 1, Line);
        return true;
    }
    if (Line != nullptr)
        delete Line;
    System = nullptr;
    return false;
}

void FGcsCombatConversation::AddFormationAcks(FContentShipBase* Ship)
{
    // E.g. 06ee338c: the formation is collected again; members from the second on.
    std::vector<FContentShipBase*> Members;
    CollectFormationMembers(&Members, Ship->FormationId);
    for (std::size_t Index = 1; Index < Members.size(); ++Index)
    {
        unsigned int MemberId = Members[Index]->SpaceObjId;
        if (FindContentPlayerStateByShip(&MemberId) != nullptr)
            continue;
        if (!AddLineOrClear(new FGcsLineAck(System, Members[Index]->SpaceObjId,
                                            Ship->SpaceObjId)))
        {
            return;
        }
    }
}

bool FGcsCombatConversation::AddEnemySightedLine(FContentShipBase* Ship)
{
    // E.g. 06ee3103 (case 9) / 06ee3763 (case 0xb).
    if ((rand() * 2) / 32768 == 0)
    {
        return AddLineOrClear(new FGcsLineCombatEnemySighted(System, Ship->SpaceObjId, 0));
    }
    std::vector<FContentShipBase*> Members;
    CollectFormationMembers(&Members, Ship->FormationId);
    FContentShipBase* Asker = nullptr;
    for (std::size_t Index = 1; Index < Members.size() && Asker == nullptr; ++Index)
    {
        unsigned int MemberId = Members[Index]->SpaceObjId;
        if (FindContentPlayerStateByShip(&MemberId) == nullptr)
            Asker = Members[Index];
    }
    FGcsLine* Line = nullptr;
    if (Asker != nullptr)
        Line = new FGcsLineCombatAskEngage(System, Asker->SpaceObjId, 0);
    else
        Line = new FGcsLineCombatEnemySighted(System, Ship->SpaceObjId, 0);
    return AddLineOrClear(Line);
}

FGcsScanConversation::FGcsScanConversation(FGcsScanEvent const* Event)
    : System(Event->System)
    , Speaker(Event->Speaker)
    , Listener(Event->Listener)
{
    // FUN_06ee55f0.
    Lines.reserve(2);
    FGcsLine* Line = nullptr;
    switch (Event->GetType())
    {
    case GCS_EVENT_SCAN_ANNOUNCE:
        Line = new FGcsLineScanAnnounce(System, Speaker, Listener);
        break;
    case GCS_EVENT_SCAN_NOTHING_FOUND:
        Line = new FGcsLineScanNothingFound(System, Speaker, Listener);
        break;
    case GCS_EVENT_SCAN_CONTRABAND:
        Line = new FGcsLineScanOrderDrop(System, Speaker, Listener);
        break;
    case GCS_EVENT_SCAN_JETTISONED:
    {
        // A player's ship gets the happy line only.
        if (FindContentPlayerStateByShip(&Listener) != nullptr)
        {
            Line = new FGcsLineScanHappy(System, Speaker, Listener);
            break;
        }
        // The scanned NPC complies (speaker and listener swapped); a line that cannot play
        // goes to the tail, which deletes it.
        Line = new FGcsLineScanComply(System, Listener, Speaker);
        if (!Line->CanPlay())
            break;
        Lines.insert(Lines.end(), 1, Line);
        Line = new FGcsLineScanHappy(System, Speaker, Listener);
        break;
    }
    case GCS_EVENT_SCAN_RESPONSE:
        // Only a scanned NPC refuses (speaker and listener swapped); a player one reaches the
        // tail with no line.
        if (FindContentPlayerStateByShip(&Listener) != nullptr)
            break;
        Line = new FGcsLineScanRefuse(System, Listener, Speaker);
        break;
    case GCS_EVENT_SCAN_HOSTILE:
        Line = new FGcsLineScanAngry(System, Speaker, Listener);
        break;
    case GCS_EVENT_SCAN_LOOT_TAKEN:
        // The listener argument is the loot taker.
        Line = new FGcsLineScanLootDestroyed(System, Speaker, Event->LootTakerId);
        break;
    default:
        // Type 0x17 and anything outside 0x11..0x18.
        break;
    }

    // Retail calls through the null line for 0x17 and for 0x15 with a player listener (the
    // scan worker never sends them); the port keeps that.
    if (Line->CanPlay())
    {
        Lines.insert(Lines.end(), 1, Line);
        return;
    }
    if (Line != nullptr)
        delete Line;
    System = nullptr;
}

bool FGcsScanConversation::IsValid()
{
    // LAB_06ee8870.
    return System != nullptr;
}

namespace
{
    // FUN_06f66440 (cdecl; only caller FUN_06ee7b00): FUN_06f66220 without the player walk --
    // every SystemInfo's space objects (+0xdc) whose formation (+0x154) matches, sorted by the
    // formation slot +0x14c (signed) with the VC6 std::sort (ContentSort).
    void CollectSystemFormationMembers(std::vector<FContentShipBase*>* Out,
                                       unsigned int FormationId)
    {
        if (FormationId == GNoFormationId)
            return;

        std::map<unsigned int, FSystemInfo>* Systems = GetSystemInfos();
        for (std::map<unsigned int, FSystemInfo>::iterator SystemIt = Systems->begin();
             SystemIt != Systems->end(); ++SystemIt)
        {
            std::list<FContentSpaceObj*>& Objects = SystemIt->second.SpaceObjs;
            for (std::list<FContentSpaceObj*>::iterator ObjIt = Objects.begin();
                 ObjIt != Objects.end(); ++ObjIt)
            {
                FContentShipBase* const Member = static_cast<FContentShipBase*>(*ObjIt);
                if (Member->FormationId == FormationId)
                    Out->insert(Out->end(), 1, Member);
            }
        }

        ContentSort(Out->data(), Out->data() + Out->size(),
                    [](FContentShipBase* A, FContentShipBase* B)
                    {
                        return static_cast<int>(A->FormationSlot) < static_cast<int>(B->FormationSlot);
                    });
    }
}

FGcsFidgetConversation::FGcsFidgetConversation(FGcsFidgetEvent const* Event)
    : System(Event->System)
    , Ship(Event->Ship)
    , Kind(Event->Kind)
{
    // FUN_06ee7b00.
    Lines.reserve(1);

    // The guide is not null-tested; retail calls its type getter (slot +0x28) again for every
    // comparison. Any type but 2, 3, 4 queues nothing.
    int const GuideType = GetShipGuide(Ship)->GetType();
    unsigned int const ShipId = Ship->SpaceObjId;
    FGcsLine* Line = nullptr;
    if (GuideType == GUIDE_TYPE_TRADE)
    {
        // Trade.
        switch (Kind)
        {
        case kGcsFidgetSimple:
        {
            unsigned int const Good = static_cast<FTradeGuide*>(GetShipGuide(Ship))->GetTradeGood();
            if (Good != 0 && (rand() * 2) / 32768 == 0)
                Line = new FGcsLineFidgetTradeIHear(System, ShipId, 0, Good);
            else
                Line = new FGcsLineFidgetTradeSimple(System, ShipId, 0);
            break;
        }
        case kGcsFidgetFormationStatus:
        {
            // The members are freed after the line is appended or deleted.
            std::vector<FContentShipBase*> Members;
            CollectSystemFormationMembers(&Members,
                                          static_cast<FContentShipBase*>(Ship)->FormationId);
            if (Members.size() > 1)
            {
                AddStatusQuery(Members);
                return;
            }
            if (!AddLine(new FGcsLineFidgetTradeSimple(System, ShipId, 0)))
                System = nullptr;
            return;
        }
        case kGcsFidgetRumor:
            Line = new FGcsLineFidgetTradeRumor(System, ShipId, 0);
            break;
        default:
            return;
        }
    }
    else if (GuideType == GUIDE_TYPE_PATROL_PATH)
    {
        // Patrol.
        switch (Kind)
        {
        case kGcsFidgetSimple:
            Line = new FGcsLineFidgetPatrolSimple(System, ShipId, 0);
            break;
        case kGcsFidgetFormationStatus:
        {
            std::vector<FContentShipBase*> Members;
            CollectSystemFormationMembers(&Members,
                                          static_cast<FContentShipBase*>(Ship)->FormationId);
            if (Members.size() <= 1)
            {
                if (!AddLine(new FGcsLineFidgetPatrolSimple(System, ShipId, 0)))
                    System = nullptr;
                return;
            }
            if ((rand() * 2) / 32768 != 0)
            {
                AddStatusQuery(Members);
                return;
            }
            // The second member asks how many waypoints are left, the leader answers it.
            unsigned int const WingmanId = Members[1]->SpaceObjId;
            if (!AddLine(new FGcsLineFidgetWaypointQuery(System, WingmanId, ShipId)) ||
                !AddLine(new FGcsLineFidgetPatrolStatus(System, ShipId, WingmanId)))
            {
                System = nullptr;
            }
            return;
        }
        case kGcsFidgetRumor:
            Line = new FGcsLineFidgetPatrolRumor(System, ShipId, 0);
            break;
        default:
            return;
        }
    }
    else if (GuideType == GUIDE_TYPE_WANDER)
    {
        // Wander.
        switch (Kind)
        {
        case kGcsFidgetSimple:
            Line = new FGcsLineFidgetPatrolSimple(System, ShipId, 0);
            break;
        case kGcsFidgetFormationStatus:
        {
            std::vector<FContentShipBase*> Members;
            CollectSystemFormationMembers(&Members,
                                          static_cast<FContentShipBase*>(Ship)->FormationId);
            if (Members.size() > 1)
            {
                AddStatusQuery(Members);
                return;
            }
            if (!AddLine(new FGcsLineFidgetPatrolSimple(System, ShipId, 0)))
                System = nullptr;
            return;
        }
        case kGcsFidgetRumor:
            Line = new FGcsLineFidgetPatrolRumor(System, ShipId, 0);
            break;
        default:
            return;
        }
    }
    else
    {
        return;
    }

    if (!AddLine(Line))
        System = nullptr;
}

bool FGcsFidgetConversation::IsValid()
{
    // LAB_06ee8870.
    return System != nullptr;
}

bool FGcsFidgetConversation::AddStatusQuery(std::vector<FContentShipBase*> const& Members)
{
    // E.g. 06ee7c72.
    unsigned int const ShipId = Ship->SpaceObjId;
    if (!AddLine(new FGcsLineFidgetStatusQuery(System, ShipId, 0)))
    {
        System = nullptr;
        return false;
    }
    for (std::vector<FContentShipBase*>::const_iterator It = Members.begin() + 1;
         It != Members.end(); ++It)
    {
        if (!AddLine(new FGcsLineFidgetStatusReport(System, (*It)->SpaceObjId, ShipId)))
        {
            System = nullptr;
            return false;
        }
    }
    return true;
}

FGcsDockConversation::FGcsDockConversation(FGcsDockGrantedEvent const* Event)
    : System(Event->System)
    , Speaker(Event->Speaker)
    , Listener(Event->Listener)
    , DockIndex(Event->DockIndex)
    , DockType(Event->DockType)
    , Response(Event->Response)
    , bWaitedInQueue(Event->bWaitedInQueue)
{
    // FUN_06ee6e60.
    Lines.reserve(2);
    unsigned int TargetType = 0;
    pub::SpaceObj::GetType(Speaker, TargetType);

    if (TargetType == OBJ_AIRLOCK_GATE)
        return;

    if (TargetType == OBJ_JUMP_GATE || TargetType == OBJ_TRADELANE_RING ||
        TargetType == OBJ_JUMP_HOLE)
    {
        // Only the leader (slot +0x14c 0, or no group) of a SpacePop formation with wingmen
        // speaks.
        FContentSpaceObj* const Object = System->FindSpaceObj(Listener);
        if (Object == nullptr)
            return;
        FContentNpcShip* const Ship = static_cast<FContentNpcShip*>(Object);
        if (!Ship->IsSpacePopShip())
            return;
        // FUN_06eeb870 inlined: the vector is freed before any line is built.
        if (!HasFormationWingmen(Object))
            return;
        if (Ship->FormationSlot != 0 && Ship->FormationId != GNoFormationId)
            return;

        FGcsLine* Line = nullptr;
        if (TargetType == OBJ_TRADELANE_RING)
        {
            Line = new FGcsLineTradeLaneStart(System, Listener, 0);
        }
        else
        {
            // Response 4 (granted) opens the gate (after the delay line when the ship
            // waited), any other delays it.
            unsigned int TargetSystem = 0;
            unsigned int TargetObject = 0;
            pub::SpaceObj::GetJumpTarget(Speaker, TargetSystem, TargetObject);
            if (Response == DOCK_HOST_RESPONSE_PROCEED)
            {
                if (bWaitedInQueue)
                {
                    if (!AddLine(new FGcsLineJumpGateOpenAfterDelay(System, Listener, 0)))
                    {
                        System = nullptr;
                        return;
                    }
                }
                Line = new FGcsLineJumpGateOpen(System, Listener, 0, TargetSystem);
            }
            else
            {
                Line = new FGcsLineJumpGateDelayed(System, Listener, 0, TargetSystem);
            }
        }
        if (!AddLine(Line))
            System = nullptr;
        return;
    }

    // Any other target (a dock).
    FContentSpaceObj* const Object = System->FindSpaceObj(Listener);
    if (bWaitedInQueue)
    {
        if (!AddLine(new FGcsLineDockNowCleared(System, Speaker, Listener, DockType)))
            System = nullptr;
        return;
    }

    if (Object != nullptr && static_cast<FContentNpcShip*>(Object)->IsSpacePopShip())
    {
        // A SpacePop ship asks; a formation leader with wingmen first thanks its escort.
        FContentNpcShip* const Ship = static_cast<FContentNpcShip*>(Object);
        bool const bWingmen = HasFormationWingmen(Object);
        if (bWingmen &&(Ship->FormationSlot == 0 || Ship->FormationId == GNoFormationId))
        {
            if (!AddLine(new FGcsLineThisOurStop(System, Listener, 0)))
            {
                System = nullptr;
                return;
            }
        }
        if (!AddLine(new FGcsLineDockRequest(System, Listener, Speaker, DockType)))
        {
            System = nullptr;
            return;
        }
    }

    // The dock's answer; 1 and anything above 4 answer nothing.
    FGcsLine* Answer = nullptr;
    switch (Response)
    {
    case DOCK_HOST_RESPONSE_DENIED_HOSTILE:
        Answer = new FGcsLineDockHostile(System, Speaker, Listener);
        break;
    case DOCK_HOST_RESPONSE_NO_FITTING_TERMINAL:
        Answer = new FGcsLineDockDenied(System, Speaker, Listener, DockType);
        break;
    case DOCK_HOST_RESPONSE_BUSY:
        Answer = new FGcsLineDockStandby(System, Speaker, Listener);
        break;
    case DOCK_HOST_RESPONSE_PROCEED:
        Answer = new FGcsLineDockGranted(System, Speaker, Listener, DockType, DockIndex);
        break;
    default:
        return;
    }
    if (!AddLine(Answer))
        System = nullptr;
}

FGcsDockHailConversation::FGcsDockHailConversation(FGcsObjectEvent const* Event)
    : System(Event->System)
    , Speaker(Event->Speaker)
    , Listener(Event->Listener)
{
    // FUN_06ee2000.
    Lines.reserve(4);

    // A station without a reputation handle (+8) gets no line; the object is not null-tested.
    int ShipRep = 0;
    pub::SpaceObj::GetRep(Listener, ShipRep);
    FContentSpaceObj* const Station = System->FindShipOrSolar(Speaker);
    if (Station->ReputationHandle == nullptr)
        return;
    int StationRep = Station->ReputationHandle->GetReputation();       // FUN_06f33f80
    float Attitude = 0.0f;
    pub::Reputation::GetAttitude(StationRep, ShipRep, Attitude);

    if (!AddLine(new FGcsLineDockHail(System, Speaker, Listener, Attitude)))
    {
        System = nullptr;
        return;
    }

    // Only a SpacePop ship answers (the object is not null-tested).
    FContentNpcShip* const Ship = static_cast<FContentNpcShip*>(System->FindSpaceObj(Listener));
    if (!Ship->IsSpacePopShip())
        return;

    // DAT_06fb6214 (-0.6f): at or below, or unordered, -> the angry answer.
    if (!(Attitude > -0.6f))
    {
        if (!AddLine(new FGcsLineHaileeAngry(System, Listener, Speaker)))
            System = nullptr;
        return;
    }

    if (!AddLine(new FGcsLineHaileeDataSubmit(System, Listener, Speaker)))
    {
        System = nullptr;
        return;
    }
    if (!AddLine(new FGcsLineDockHailReceived(System, Speaker, Listener)))
    {
        System = nullptr;
        return;
    }

    switch (Ship->GetGuideType())
    {
    case GUIDE_TYPE_TRADE:
    {
        // Trade: home, then what it trades (failing that, where it is going).
        if (!AddLine(new FGcsLineTradeIdHome(System, Listener, Speaker)))
        {
            System = nullptr;
            return;
        }
        FGcsLine* const Status = new FGcsLineTradeStatus(System, Listener, Speaker);
        if (Status->CanPlay())
        {
            Lines.insert(Lines.end(), 1, Status);
            return;
        }
        delete Status;
        if (!AddLine(new FGcsLineGoingTo(System, Listener, Speaker)))
            System = nullptr;
        return;
    }
    case GUIDE_TYPE_PATROL_PATH:
        // Patrol: home, where it is going, its progress.
        if (!AddLine(new FGcsLinePatrolIdHome(System, Listener, Speaker)) ||
            !AddLine(new FGcsLineGoingTo(System, Listener, Speaker)) ||
            !AddLine(new FGcsLinePatrolStatus(System, Listener, Speaker)))
        {
            System = nullptr;
        }
        return;
    case GUIDE_TYPE_WANDER:
        // Wander: home, where it is going.
        if (!AddLine(new FGcsLinePatrolIdHome(System, Listener, Speaker)) ||
            !AddLine(new FGcsLineGoingTo(System, Listener, Speaker)))
        {
            System = nullptr;
        }
        return;
    default:
        return;
    }
}

bool FGcsDockHailConversation::IsValid()
{
    // FUN_06ee6e20.
    return System != nullptr && System->FindShip(Listener) != nullptr &&
           System->FindShipOrSolar(Speaker) != nullptr;
}

bool FGcsDockConversation::IsValid()
{
    // FUN_06ee6e20.
    return System != nullptr && System->FindShip(Listener) != nullptr &&
           System->FindShipOrSolar(Speaker) != nullptr;
}

// =======================================================================================
// Events
// =======================================================================================

int FGcsDockHailEvent::GetType() const
{
    // LAB_06ee9e10.
    return GCS_EVENT_DOCK_HAIL;
}

int FGcsPlayerObjectEvent::GetType() const
{
    // LAB_06ee99a0.
    return GCS_EVENT_PLAYER_HAIL;
}

int FGcsCombatEvent::GetType() const
{
    // FUN_06eea860 (ICF-shared, slot 0 of 06fb74c4..06fb74d4).
    return Type;
}

int FGcsScanEvent::GetType() const
{
    // FUN_06f33f80 (ICF-shared, slot 0 of 06fb857c).
    return Type;
}

int FGcsFidgetEvent::GetType() const
{
    // LAB_06eea870.
    return GCS_EVENT_FIDGET;
}

// =======================================================================================
// Conversation manager
// =======================================================================================

FGcsConversationManagerInstance::FGcsConversationManagerInstance()
{
    // Inlined at 06ee8a8b: DAT_06fcf1b0 = the derived this.
    GGcsConversationManager = static_cast<FGcsConversationManager*>(this);
}

FGcsConversationManagerInstance::~FGcsConversationManagerInstance()
{
    // FUN_06eec4d0 (inlined at 06ee8bbe).
    GGcsConversationManager = nullptr;
}

FGcsConversationManager::FGcsConversationManager()
{
    // FUN_06ee8a70: base first, then the members (in-class initialisers).
}

FGcsConversationManager::~FGcsConversationManager()
{
    // FUN_06ee8af0.
    std::list<FGcsConversation*>::iterator It = Conversations.begin();
    while (It != Conversations.end())
    {
        if (*It != nullptr)
            delete *It;
        It = Conversations.erase(It);
    }
    // Then DelayedMessages, Conversations and the base (clears DAT_06fcf1b0), in that order.
}

void FGcsConversationManager::QueueMessage(unsigned int const* A, unsigned int const* B,
                                           unsigned int const* C, unsigned int const* D,
                                           float const* Delay)
{
    // LAB_06ee91b0.
    FGcsDelayedMessage Entry;
    Entry.SystemId = *A;
    Entry.Speaker = *B;
    Entry.Listener = *C;
    Entry.LastTextId = *D;
    Entry.Elapsed = 0.0f;
    Entry.Delay = *Delay;
    DelayedMessages.insert(DelayedMessages.end(), 1, Entry);
}

bool FGcsConversationManager::Add(FRmConversationBase* Conversation)
{
    // FUN_06ee9140. The clone is not null-tested.
    if (!bEnabled)
        return false;
    FRmConversationBase* const Clone = Conversation->Clone();
    if (!Clone->IsValid())
    {
        delete Clone;
        return false;
    }
    Clone->Start();
    Conversations.push_back(Clone);
    return true;
}

void FGcsConversationManager::OnMessage(void const* Parms)
{
    // FUN_06ee8bd0.
    if (!bEnabled)
        return;
    for (std::list<FGcsConversation*>::iterator It = Conversations.begin();
         It != Conversations.end(); ++It)
    {
        if (!(*It)->OnMessage(Parms))
            continue;
        if ((*It)->IsFinished())
        {
            if (*It != nullptr)
                delete *It;
            Conversations.erase(It);
        }
        return;
    }
}

void FGcsConversationManager::Update(float DeltaTime)
{
    // FUN_06ee8c50.
    if (pub::SinglePlayer())
        return;

    std::vector<FGcsSentMessage> Due;
    std::vector<FGcsDelayedMessage>::iterator It = DelayedMessages.begin();
    while (It != DelayedMessages.end())
    {
        // Retail compares the unrounded double sum, not the stored float.
        double const Sum = static_cast<double>(DeltaTime) + static_cast<double>(It->Elapsed);
        It->Elapsed = static_cast<float>(Sum);
        if (Sum < static_cast<double>(It->Delay))
        {
            ++It;
            continue;
        }
        // Retail leaves the fifth word unwritten; the port zeroes it.
        FGcsSentMessage Message;
        Message.Words[0] = It->Listener;
        Message.Words[1] = It->SystemId;
        Message.Words[2] = It->Speaker;
        Message.Words[3] = It->LastTextId;
        Message.Words[4] = 0;
        Due.insert(Due.end(), 1, Message);
        It = DelayedMessages.erase(It);
    }

    for (std::vector<FGcsSentMessage>::iterator Send = Due.begin(); Send != Due.end(); ++Send)
        pub::Controller::_SendMessage(GSpaceSupportControllerId, pub::Controller::EMessage::CommComplete,
                                      &*Send);
}

float FGcsConversationManager::QueueConversation(FGcsEvent const* Event)
{
    // FUN_06ee8d70. The result is 0.0f (DAT_06fb444c) whenever no conversation is queued.
    if (!bEnabled)
        return 0.0f;

    int const Type = Event->GetType();
    FGcsConversation* Conversation = nullptr;
    switch (Type)
    {
    case GCS_EVENT_DOCK_HAIL:
    case GCS_EVENT_DOCK_GRANTED:
    case GCS_EVENT_LAUNCH:
    {
        // Type 2 records both objects, types 1 and 3 the speaker only.
        FGcsObjectEvent const* ObjectEvent = static_cast<FGcsObjectEvent const*>(Event);
        if (!IsGcsCommAllowed(Type, ObjectEvent->System, ObjectEvent->Speaker,
                              ObjectEvent->Listener))
            return 0.0f;
        RecordGcsComm(Type, ObjectEvent->System, ObjectEvent->Speaker,
                      Type == GCS_EVENT_DOCK_GRANTED ? ObjectEvent->Listener : 0);
        if (Type == GCS_EVENT_LAUNCH)
        {
            Conversation =
                new FGcsLaunchConversation(static_cast<FGcsLaunchEvent const*>(Event));
        }
        else if (Type == GCS_EVENT_DOCK_GRANTED)
        {
            Conversation =
                new FGcsDockConversation(static_cast<FGcsDockGrantedEvent const*>(Event));
        }
        else
        {
            Conversation = new FGcsDockHailConversation(ObjectEvent);
        }
        break;
    }
    case GCS_EVENT_PLAYER_HAIL:
        // No admission test.
        Conversation = new FGcsPlayerHailConversation(
            static_cast<FGcsPlayerObjectEvent const*>(Event));
        break;
    case GCS_EVENT_DAMAGED:
    case GCS_EVENT_KILLED:
    case GCS_EVENT_IN_SIGHTS:
    case GCS_EVENT_ENGAGE:
    case GCS_EVENT_COMBAT_OVER:
    case GCS_EVENT_NO_ENGAGE:
    case GCS_EVENT_HELP:
    case GCS_EVENT_DRIFT:
    case GCS_EVENT_FLEE:
    case GCS_EVENT_FRIENDLY_FIRE:
    case GCS_EVENT_LEADER_CHANGED:
        // No admission test here (FGcsCombatHandler tests and records before queueing).
        Conversation =
            new FGcsCombatConversation(static_cast<FGcsCombatEvent const*>(Event));
        break;
    case GCS_EVENT_SCAN_ANNOUNCE:
    case GCS_EVENT_SCAN_NOTHING_FOUND:
    case GCS_EVENT_SCAN_CONTRABAND:
    case GCS_EVENT_SCAN_JETTISONED:
    case GCS_EVENT_SCAN_RESPONSE:
    case GCS_EVENT_SCAN_HOSTILE:
    case GCS_EVENT_SCAN_UNUSED_17:
    case GCS_EVENT_SCAN_LOOT_TAKEN:
        Conversation = new FGcsScanConversation(static_cast<FGcsScanEvent const*>(Event));
        break;
    case GCS_EVENT_FIDGET:
        Conversation = new FGcsFidgetConversation(static_cast<FGcsFidgetEvent const*>(Event));
        break;
    default:
        // Type 4 and anything outside 1..0x19.
        return 0.0f;
    }

    if (!Conversation->IsValid())
    {
        delete Conversation;
        return 0.0f;
    }
    Conversation->Start();
    Conversations.push_back(Conversation);
    if (Type == GCS_EVENT_KILLED)
    {
        // The first line's duration (slot +0x10) is the result instead of 1.0.
        return Conversations.back()->Lines.front()->GetDuration();
    }
    return 1.0f;
}

// =======================================================================================
// Handlers
// =======================================================================================

FGcsHandler* __cdecl GetGcsHandler(int const* Type)
{
    // FUN_06ee93f0: find, else create and insert. Retail reads the value back through a
    // second insert of {*Type, 0}, which returns the existing node.
    switch (*Type)
    {
    case kGcsHandlerDockHail:
    case kGcsHandlerCombat:
    case kGcsHandlerFidget:
        break;
    default:
        return nullptr;
    }

    std::map<int, FGcsHandler*>::iterator It = GGcsHandlerMap.find(*Type);
    if (It != GGcsHandlerMap.end())
        return It->second;

    FGcsHandler* Handler = nullptr;
    if (*Type == kGcsHandlerFidget)
        Handler = new FGcsFidgetHandler();
    else if (*Type == kGcsHandlerCombat)
        Handler = new FGcsCombatHandler();
    else
        Handler = new FGcsDockHailHandler();
    GGcsHandlerMap.insert(std::make_pair(*Type, static_cast<FGcsHandler*>(nullptr)))
        .first->second = Handler;
    return Handler;
}

FGcsHandlerSet::~FGcsHandlerSet()
{
    // FUN_06ee9690.
    while (!Handlers.empty())
    {
        if (Handlers.front() != nullptr)
            delete Handlers.front();
        Handlers.erase(Handlers.begin());
    }
}

void FGcsHandlerSet::HandleMessage(pub::Controller::EMessage MessageId, void const* Parms)
{
    // FUN_06ee9900.
    if (MessageId != pub::Controller::EMessage::Hail)
    {
        for (std::vector<FGcsHandler*>::iterator It = Handlers.begin(); It != Handlers.end();
             ++It)
        {
            (*It)->HandleMessage(MessageId, Parms);
        }
        return;
    }

    // Parms {ship, object, system}.
    unsigned int const* Words = static_cast<unsigned int const*>(Parms);
    FGcsPlayerObjectEvent Event;
    Event.System = FindSystemInfo(&Words[2]);
    Event.Player = FindContentPlayerStateByShip(&Words[0]);
    Event.Object = Event.System->FindSpaceObj(Words[1]);
    if (Event.Player != nullptr && Event.Object != nullptr)
        GGcsConversations.QueueConversation(&Event);
}

void FGcsHandlerSet::AddHandler(int const* Type)
{
    // FUN_06ee9740.
    FGcsHandler* const Handler = GetGcsHandler(Type);
    Handlers.insert(Handlers.end(), 1, Handler);
}

// ---------------------------------------------------------------------------------------
// Dock hail handler
// ---------------------------------------------------------------------------------------

FGcsDockHailHandler::~FGcsDockHailHandler()
{
    // FUN_06ee9aa0: OnSystemUnloaded(&DAT_06fb6210 (0)).
    unsigned int const AllSystems = 0;
    OnSystemUnloaded(&AllSystems);
}

int FGcsDockHailHandler::GetType() const
{
    // LAB_06ea1450.
    return kGcsHandlerDockHail;
}

void FGcsDockHailHandler::SuppressHail(unsigned int const* Ship, void* const* Phantom,
                                       int Delay)
{
    // FUN_06ee9b70.
    RecentHails.push_back(std::make_pair(*Phantom, *Ship));
    pub::Controller::TimerExpired Timer;
    Timer.Param1 = GGcsDockHailTimerId;
    Timer.Param2 = static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(&RecentHails.back()));
    pub::SetTimer(GSpaceSupportControllerId, Timer, static_cast<float>(Delay));
}

void FGcsDockHailHandler::HandleMessage(pub::Controller::EMessage MessageId, void const* Parms)
{
    using pub::Controller::EMessage;
    // LAB_06ee9c10.
    switch (MessageId)
    {
    case EMessage::TimerExpired:
    {
        // Our timer drops the oldest recent hail (not the entry its Param2 points to).
        pub::Controller::TimerExpired const* Timer =
            static_cast<pub::Controller::TimerExpired const*>(Parms);
        if (Timer->Param1 == GGcsDockHailTimerId)
            RecentHails.erase(RecentHails.begin());
        return;
    }
    case EMessage::SystemLoaded:
        OnSystemLoaded(static_cast<unsigned int const*>(Parms));
        return;
    case EMessage::SystemUnloaded:
        OnSystemUnloaded(static_cast<unsigned int const*>(Parms));
        return;
    case EMessage::PhantomEnter:
        break;
    default:
        return;
    }

    // A ship touched a phantom.
    FGcsPhantomTouch const* Touch = static_cast<FGcsPhantomTouch const*>(Parms);
    std::map<unsigned int, std::map<void*, unsigned int>>::iterator SystemIt =
        Phantoms.find(Touch->System);
    if (SystemIt == Phantoms.end())
        return;

    // A recent hail of this ship by this phantom (or by any) wins.
    for (std::list<std::pair<void*, unsigned int>>::iterator It = RecentHails.begin();
         It != RecentHails.end(); ++It)
    {
        if ((It->first == Touch->Phantom || It->first == nullptr) && It->second == Touch->Ship)
            return;
    }

    std::map<void*, unsigned int>::iterator PhantomIt = SystemIt->second.find(Touch->Phantom);
    if (PhantomIt == SystemIt->second.end())
        return;

    // Remembered for 30 s.
    RecentHails.push_back(std::make_pair(Touch->Phantom, Touch->Ship));
    pub::Controller::TimerExpired Timer;
    Timer.Param1 = GGcsDockHailTimerId;
    Timer.Param2 = static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(&RecentHails.back()));
    pub::SetTimer(GSpaceSupportControllerId, Timer, 30.0f);

    // Only a SpacePop NPC ship outside a formation group is hailed (the system record is
    // not null-tested).
    FGcsDockHailEvent Event;
    Event.System = FindSystemInfo(&Touch->System);
    FContentSpaceObj* const Object = Event.System->FindSpaceObj(Touch->Ship);
    if (Object == nullptr)
        return;
    FContentNpcShip* const Ship = static_cast<FContentNpcShip*>(Object);
    if (Ship->FormationSlot != 0 && Ship->FormationId != GNoFormationId)
        return;
    if (!Ship->IsSpacePopShip())
        return;
    Event.Speaker = PhantomIt->second;
    Event.Listener = Touch->Ship;
    GGcsConversations.QueueConversation(&Event);
}

void FGcsDockHailHandler::OnSystemLoaded(unsigned int const* SystemId)
{
    // FUN_06ee9e20: every station / docking object of the system (pub::System::ScanObjects,
    // 0x40 at most, radius -1, mask 0x200100).
    unsigned int Objects[0x40];
    unsigned int Count = 0;
    Vector Center;
    Center.x = 0.0f;
    Center.y = 0.0f;
    Center.z = 0.0f;
    pub::System::ScanObjects(*SystemId, Objects, 0x40, Center, -1.0f, OBJ_STATION | OBJ_CAPITAL, Count);
    FindSystemInfo(SystemId);   // result unused

    for (unsigned int Index = 0; Index < Count; ++Index)
    {
        // The docking target's base record must have voice data (CommReferrable slot 0).
        unsigned int Type = 0;
        pub::SpaceObj::GetType(Objects[Index], Type);
        if ((Type & (OBJ_STATION | OBJ_CAPITAL)) == 0 || (Type & OBJ_PLANET) != 0)
            continue;
        unsigned int BaseId = 0;
        pub::SpaceObj::GetDockingTarget(Objects[Index], BaseId);
        FContentBase* const Base = FindContentBaseInfo(&BaseId);
        if (Base == nullptr || !Base->HasVoiceInfo())
            continue;

        Vector Position;
        Matrix Orientation;
        pub::SpaceObj::GetLocation(Objects[Index], Position, Orientation);
        void* Phantom = nullptr;
        pub::Phantom::Create(*SystemId, 2500.0f, Position, OBJ_SHIP_MASK, Phantom);
        pub::Phantom::SetActive(Phantom, true);

        // An existing key gets the new object.
        Phantoms[*SystemId].insert(std::make_pair(Phantom, 0u)).first->second = Objects[Index];
    }
}

void FGcsDockHailHandler::OnSystemUnloaded(unsigned int const* SystemId)
{
    // FUN_06eea080.
    if (*SystemId != 0)
    {
        // The system's (empty) entry stays.
        std::map<unsigned int, std::map<void*, unsigned int>>::iterator SystemIt =
            Phantoms.find(*SystemId);
        if (SystemIt == Phantoms.end())
            return;
        std::map<void*, unsigned int>::iterator It = SystemIt->second.begin();
        while (It != SystemIt->second.end())
        {
            pub::Phantom::Destroy(It->first);
            It = SystemIt->second.erase(It);
        }
        return;
    }

    // Every system.
    std::map<unsigned int, std::map<void*, unsigned int>>::iterator SystemIt = Phantoms.begin();
    while (SystemIt != Phantoms.end())
    {
        std::map<void*, unsigned int>::iterator It = SystemIt->second.begin();
        while (It != SystemIt->second.end())
        {
            pub::Phantom::Destroy(It->first);
            It = SystemIt->second.erase(It);
        }
        SystemIt = Phantoms.erase(SystemIt);
    }
}

// ---------------------------------------------------------------------------------------
// Handlers 1 and 2
// ---------------------------------------------------------------------------------------

void FGcsCombatHandler::HandleMessage(pub::Controller::EMessage MessageId, void const* Parms)
{
    using pub::Controller::EMessage;
    // LAB_06eea160. Every other message id does nothing.
    unsigned int const* const Words = static_cast<unsigned int const*>(Parms);
    // Retail reuses the Parms argument slot for the GetType (0x1001) and GetRelativeHealth
    // (0xd) outputs, so a failing call leaves the pointer's bits.
    unsigned int const ParmsBits =
        static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(Parms));
    switch (MessageId)
    {
    case EMessage::DeathFuseLit:
    {
        // {system, ship, ?, out float}: a ship died -- the victim's side speaks (type 7,
        // A = 0); the conversation's result goes to word 3 of the sender's block.
        FSystemInfo* const System = FindSystemInfo(&Words[0]);
        if (System->FindShip(Words[1]) == nullptr)
            return;
        if (!IsGcsCommAllowed(GCS_EVENT_KILLED, System, 0, Words[1]))
            return;
        FGcsCombatEvent Event;
        Event.System = System;
        Event.ObjectA = 0;
        Event.ObjectB = Words[1];
        Event.Type = GCS_EVENT_KILLED;
        float const Duration = GGcsConversations.QueueConversation(&Event);
        *reinterpret_cast<float*>(const_cast<unsigned int*>(&Words[3])) = Duration;
        RecordGcsComm(GCS_EVENT_KILLED, System, 0, Words[1]);
        return;
    }
    case EMessage::ObjectDamaged:
    {
        // {victim, system, attacker, ?, ?, kind, amount}: only an amount above 0 (ordered).
        float const Amount = *reinterpret_cast<float const*>(&Words[6]);
        if (!(Amount > 0.0f))
            return;
        DamageCause const Kind = static_cast<DamageCause>(Words[5]);
        if (Kind < DamageCause::Gun || (Kind > DamageCause::MissileTorpedo && Kind != DamageCause::Mine))
            return;
        FSystemInfo* const System = FindSystemInfo(&Words[1]);
        unsigned int VictimType = 0;
        if (pub::SpaceObj::GetType(Words[0], VictimType) != 0)
            return;
        if (!IsGcsCommAllowed(GCS_EVENT_DAMAGED, System, Words[2], Words[0]))
            return;
        // Relative health (status not tested) below DAT_06fb5810 (1.0); unordered fails.
        float Health = 0.0f;
        std::memcpy(&Health, &ParmsBits, sizeof(Health));
        pub::SpaceObj::GetRelativeHealth(Words[0], Health);
        if (!(Health < 1.0f))
            return;
        RecordGcsComm(GCS_EVENT_DAMAGED, System, Words[2], Words[0]);
        FGcsCombatDamageEvent Event;
        Event.System = System;
        Event.ObjectA = Words[2];
        Event.ObjectB = Words[0];
        Event.Type = GCS_EVENT_DAMAGED;
        Event.Health = Health;
        GGcsConversations.QueueConversation(&Event);
        return;
    }
    case EMessage::ObjectDestroyed:
    {
        // {victim, killer, system, ?, flag}: with flag 1 the killer brags (type 7, B = 0) --
        // unless it is a player whose group feelings towards the NPC victim's faction are at
        // least -0.2 (DAT_06fb6218).
        if (Words[4] != 1)
            return;
        FSystemInfo* const System = FindSystemInfo(&Words[2]);
        FContentSpaceObj* const Killer = System->FindShip(Words[1]);
        FContentSpaceObj* const Victim = System->FindShip(Words[0]);
        if (Killer == nullptr)
            return;
        if (Victim != nullptr)
        {
            unsigned int KillerId = Killer->SpaceObjId;
            unsigned int VictimId = Victim->SpaceObjId;
            if (FindContentPlayerStateByShip(&KillerId) != nullptr &&
                FindContentPlayerStateByShip(&VictimId) == nullptr)
            {
                float Feelings = 0.0f;
                unsigned int const VictimGroup = Victim->ReputationHandle->GetAffiliation();
                int const KillerRep = Killer->ReputationHandle->GetReputation();
                pub::Reputation::GetGroupFeelingsTowards(KillerRep, VictimGroup, Feelings);
                if (Feelings >= -0.2f)
                    return;
            }
        }
        if (!IsGcsCommAllowed(GCS_EVENT_KILLED, System, Words[1], Words[0]))
            return;
        FGcsCombatEvent Event;
        Event.System = System;
        Event.ObjectA = Words[1];
        Event.ObjectB = 0;
        Event.Type = GCS_EVENT_KILLED;
        GGcsConversations.QueueConversation(&Event);
        RecordGcsComm(GCS_EVENT_KILLED, System, Words[1], Words[0]);
        return;
    }
    case EMessage::CombatState:
    {
        // {kind, A, B, system}: 0 engage (9), 1 / 2 no engage (0xb), 3 / 4 combat over
        // (0xa); anything else nothing.
        FSystemInfo* const System = FindSystemInfo(&Words[3]);
        switch (Words[0])
        {
        case pub::AI::ContentCallback::CSNEngage:
        {
            if (!IsGcsCommAllowed(GCS_EVENT_ENGAGE, System, Words[1], Words[2]))
                return;
            RecordGcsComm(GCS_EVENT_ENGAGE, System, Words[1], Words[2]);
            FGcsCombatEvent Event;
            Event.System = System;
            Event.ObjectA = Words[1];
            Event.ObjectB = Words[2];
            Event.Type = GCS_EVENT_ENGAGE;
            GGcsConversations.QueueConversation(&Event);
            return;
        }
        case 1:
        case pub::AI::ContentCallback::CSNNoEngage:
        {
            // Only a ship (type & 0x7f0000; GetType's status not tested).
            if (!IsGcsCommAllowed(GCS_EVENT_NO_ENGAGE, System, Words[1], Words[2]))
                return;
            unsigned int Type = ParmsBits;
            pub::SpaceObj::GetType(Words[1], Type);
            if ((Type & OBJ_SHIP_MASK) == 0)
                return;
            RecordGcsComm(GCS_EVENT_NO_ENGAGE, System, Words[1], Words[2]);
            FGcsCombatNoEngageEvent Event;
            Event.System = System;
            Event.ObjectA = Words[1];
            Event.ObjectB = Words[2];
            Event.Type = GCS_EVENT_NO_ENGAGE;
            Event.Reason = static_cast<int>(Words[0]);
            GGcsConversations.QueueConversation(&Event);
            return;
        }
        case 3:
        case pub::AI::ContentCallback::CSNCombatOver:
        {
            if (!IsGcsCommAllowed(GCS_EVENT_COMBAT_OVER, System, Words[1], Words[2]))
                return;
            RecordGcsComm(GCS_EVENT_COMBAT_OVER, System, Words[1], Words[2]);
            FGcsCombatOverEvent Event;
            Event.System = System;
            Event.ObjectA = Words[1];
            Event.ObjectB = Words[2];
            Event.Type = GCS_EVENT_COMBAT_OVER;
            Event.Reason = static_cast<int>(Words[0]);
            GGcsConversations.QueueConversation(&Event);
            return;
        }
        default:
            return;
        }
    }
    case EMessage::CombatDrift:
    {
        // {ship, system}: combat drift (0xd).
        FSystemInfo* const System = FindSystemInfo(&Words[1]);
        if (!IsGcsCommAllowed(GCS_EVENT_DRIFT, System, Words[0], 0))
            return;
        RecordGcsComm(GCS_EVENT_DRIFT, System, Words[0], 0);
        FGcsCombatEvent Event;
        Event.System = System;
        Event.ObjectA = Words[0];
        Event.ObjectB = 0;
        Event.Type = GCS_EVENT_DRIFT;
        GGcsConversations.QueueConversation(&Event);
        return;
    }
    case EMessage::NpcTurnedHostile:
    case EMessage::ShipInSights:
    {
        // {A, B, system}: 0x1003 help (0xc, sender FUN_06f70580) / 0x100e in sights (8,
        // ship_in_sights_event 06f34000).
        int const Type =
            MessageId == EMessage::NpcTurnedHostile ? GCS_EVENT_HELP : GCS_EVENT_IN_SIGHTS;
        FSystemInfo* const System = FindSystemInfo(&Words[2]);
        if (!IsGcsCommAllowed(Type, System, Words[0], Words[1]))
            return;
        RecordGcsComm(Type, System, Words[0], Words[1]);
        FGcsCombatEvent Event;
        Event.System = System;
        Event.ObjectA = Words[0];
        Event.ObjectB = Words[1];
        Event.Type = Type;
        GGcsConversations.QueueConversation(&Event);
        return;
    }
    case EMessage::FleeRequest:
    {
        // {ship, reason, system}: flight (0xe), sender request_flee_destination 06f34ce0.
        FSystemInfo* const System = FindSystemInfo(&Words[2]);
        if (!IsGcsCommAllowed(GCS_EVENT_FLEE, System, Words[0], 0))
            return;
        RecordGcsComm(GCS_EVENT_FLEE, System, Words[0], 0);
        FGcsCombatFleeEvent Event;
        Event.System = System;
        Event.ObjectA = Words[0];
        Event.ObjectB = 0;
        Event.Type = GCS_EVENT_FLEE;
        Event.Reason = static_cast<int>(Words[1]);
        GGcsConversations.QueueConversation(&Event);
        return;
    }
    case EMessage::DamageReputation:
    {
        // {0, A, B, system}: friendly fire (0xf), sender FUN_06f6ff90; a non-zero word 0 does
        // nothing.
        if (Words[0] != 0)
            return;
        FSystemInfo* const System = FindSystemInfo(&Words[3]);
        if (!IsGcsCommAllowed(GCS_EVENT_FRIENDLY_FIRE, System, Words[1], Words[2]))
            return;
        RecordGcsComm(GCS_EVENT_FRIENDLY_FIRE, System, Words[1], Words[2]);
        FGcsCombatEvent Event;
        Event.System = System;
        Event.ObjectA = Words[1];
        Event.ObjectB = Words[2];
        Event.Type = GCS_EVENT_FRIENDLY_FIRE;
        GGcsConversations.QueueConversation(&Event);
        return;
    }
    case EMessage::FormationLeaderChanged:
    {
        // {new leader, old leader} as ship objects (sender 06f5e7eb): a SpacePop ship with
        // wingmen takes the lead (0x10).
        FContentShipBase* const* const Ships = static_cast<FContentShipBase* const*>(Parms);
        FSystemInfo* const System = Ships[0]->GetSystem();
        if (!static_cast<FContentNpcShip*>(Ships[0])->IsSpacePopShip())
            return;
        if (!HasFormationWingmen(Ships[0]))
            return;
        if (!IsGcsCommAllowed(GCS_EVENT_LEADER_CHANGED, System, Ships[0]->SpaceObjId, Ships[1]->SpaceObjId))
            return;
        RecordGcsComm(GCS_EVENT_LEADER_CHANGED, System, Ships[0]->SpaceObjId, Ships[1]->SpaceObjId);
        FGcsCombatEvent Event;
        Event.System = System;
        Event.ObjectA = Ships[0]->SpaceObjId;
        Event.ObjectB = Ships[1]->SpaceObjId;
        Event.Type = GCS_EVENT_LEADER_CHANGED;
        GGcsConversations.QueueConversation(&Event);
        return;
    }
    default:
        return;
    }
}

int FGcsCombatHandler::GetType() const
{
    // LAB_06ee9e10.
    return kGcsHandlerCombat;
}

namespace
{
    // The squared length the fidget handler sums on the x87 stack: z*z + y*y + x*x of a
    // float difference, in double and in that order.
    double FidgetLengthSquared(Vector const& Difference)
    {
        return static_cast<double>(Difference.z) * Difference.z +
               static_cast<double>(Difference.y) * Difference.y +
               static_cast<double>(Difference.x) * Difference.x;
    }

    // rand() * DAT_06fb4448 (2^-15) in double, compared below the float Chance.
    bool IsRandBelow(float Chance)
    {
        double const Roll = static_cast<double>(rand()) * (1.0 / 32768.0);
        return Roll < static_cast<double>(Chance);
    }
}

// DAT_06fcf0dc (static initializer 06eea880: 0x4abebc20).
float GGcsFidgetRangeSquared = 6250000.0f;

void FGcsFidgetHandler::HandleMessage(pub::Controller::EMessage MessageId, void const* Parms)
{
    // LAB_06eea890. Parms is never read.
    (void)Parms;
    if (MessageId != pub::Controller::EMessage::Heartbeat)
        return;

    // The players handled this tick.
    std::vector<FContentPlayerState*> Handled;
    std::map<unsigned int, std::unique_ptr<FContentPlayerState>>& Players =
        GetContentPlayerStates();
    for (std::map<unsigned int, std::unique_ptr<FContentPlayerState>>::iterator PlayerIt =
             Players.begin();
         PlayerIt != Players.end(); ++PlayerIt)
    {
        FContentPlayerState* const Player = PlayerIt->second.get();
        if (Player->ShipObject == nullptr)
            continue;
        if (std::find(Handled.begin(), Handled.end(), Player) != Handled.end())
            continue;
        if (!IsRandBelow(0.0083333338f)) // DAT_06fb6230 (1/120)
            continue;
        FContentPlayerShip* const PlayerShip = Player->ShipObject;
        if (!IsGcsCommAllowed(GCS_EVENT_FIDGET, PlayerShip->GetSystem(), 0, PlayerShip->SpaceObjId))
            continue;

        // A local copy of the system's space objects.
        FSystemInfo* const System = PlayerShip->GetSystem();
        std::list<FContentSpaceObj*> Objects;
        Objects = System->SpaceObjs;

        // The candidates: SpacePop ships that lead their formation (+0x14c == 0) or fly
        // without one (+0x154 == -1) and are not busy.
        std::vector<FContentShipBase*> Candidates;
        Candidates.reserve(Objects.size());
        for (std::list<FContentSpaceObj*>::iterator ObjIt = Objects.begin(); ObjIt != Objects.end();
             ++ObjIt)
        {
            if (!static_cast<FContentNpcShip*>(*ObjIt)->IsSpacePopShip())
                continue;
            FContentShipBase* const Candidate = static_cast<FContentShipBase*>(*ObjIt);
            if (Candidate->FormationSlot != 0 && Candidate->FormationId != GNoFormationId)
                continue;
            if (Candidate->IsInCombat())
                continue;
            Candidates.insert(Candidates.end(), 1, Candidate);
        }

        Vector PlayerPosition;
        PlayerShip->GetPosition(&PlayerPosition);
        FContentShipBase* Speaker = nullptr;
        // Random candidates until one speaks or none is left.
        while (!Candidates.empty())
        {
            int const Count = static_cast<int>(Candidates.size());
            int const Index = (rand() * Count) / 32768;
            std::vector<FContentShipBase*>::iterator Pick = Candidates.begin() + Index;
            Vector PickPosition;
            (*Pick)->GetPosition(&PickPosition);
            // Within 2500 m of the player; the difference is FUN_06eeaee0 (SubtractVectors).
            if (FidgetLengthSquared(SubtractVectors(PlayerPosition, PickPosition)) <
                static_cast<double>(GGcsFidgetRangeSquared))
            {
                // No member of its formation is busy. The members are freed before the flag
                // is tested and before the players walk below.
                bool bBusy = false;
                {
                    std::vector<FContentShipBase*> Members;
                    CollectFormationMembers(&Members, (*Pick)->FormationId);
                    for (std::vector<FContentShipBase*>::iterator Member = Members.begin();
                         Member != Members.end(); ++Member)
                    {
                        if ((*Member)->IsInCombat())
                        {
                            bBusy = true;
                            break;
                        }
                    }
                }
                if (!bBusy)
                {
                    // It speaks; every player whose ship is within 2500 m of it counts as
                    // handled (retail's redundant second find at 06eeace8 is not reproduced).
                    Speaker = *Pick;
                    for (std::map<unsigned int, std::unique_ptr<FContentPlayerState>>::iterator
                             OtherIt = Players.begin();
                         OtherIt != Players.end(); ++OtherIt)
                    {
                        FContentPlayerState* const Other = OtherIt->second.get();
                        if (std::find(Handled.begin(), Handled.end(), Other) != Handled.end())
                            continue;
                        if (Other->ShipObject == nullptr)
                            continue;
                        Vector OtherPosition;
                        Other->ShipObject->GetPosition(&OtherPosition);
                        // Here retail inlines the difference, rounding each component to
                        // float.
                        Vector Difference;
                        Difference.x = OtherPosition.x - PickPosition.x;
                        Difference.y = OtherPosition.y - PickPosition.y;
                        Difference.z = OtherPosition.z - PickPosition.z;
                        if (!(FidgetLengthSquared(Difference) <
                              static_cast<double>(GGcsFidgetRangeSquared)))
                            continue;
                        Handled.insert(Handled.end(), 1, Other);
                    }
                    break;
                }
            }
            // Too far or busy: the pick is erased.
            Candidates.erase(Pick);
        }

        if (Speaker == nullptr)
            continue;

        RecordGcsComm(GCS_EVENT_FIDGET, PlayerShip->GetSystem(), Speaker->SpaceObjId, PlayerShip->SpaceObjId);
        // Kind 2 below DAT_06fb6234 (1/30), else 1 below DAT_06fb6238 (0.2) of a second roll,
        // else 0. The result is dropped.
        FGcsFidgetEvent Event;
        Event.System = System;
        Event.Ship = Speaker;
        Event.Kind = kGcsFidgetSimple;
        if (IsRandBelow(0.033333335f))
        {
            Event.Kind = kGcsFidgetRumor;
        }
        else
        {
            Event.Kind = kGcsFidgetFormationStatus;
            if (!IsRandBelow(0.2f))
                Event.Kind = kGcsFidgetSimple;
        }
        GGcsConversations.QueueConversation(&Event);
    }
}

int FGcsFidgetHandler::GetType() const
{
    // LAB_06ee9660.
    return kGcsHandlerFidget;
}

// =======================================================================================
// Admission
// =======================================================================================

void __cdecl SetGcsDefaultInterval(double Interval)
{
    // FUN_06eeba30.
    GGcsDefaultInterval = Interval;
}

void __cdecl SetGcsCategoryInterval(int Category, float Interval)
{
    // FUN_06eeba50: an existing key is kept.
    GGcsCategoryIntervals.insert(std::make_pair(Category, Interval));
}

void __cdecl SetGcsCategoryChance(int Category, float Chance)
{
    // FUN_06eebb00.
    GGcsCategoryChances.insert(std::make_pair(Category, Chance));
}

void __cdecl EnableGcsSpeakerCategory(int Category)
{
    // FUN_06eebbb0.
    GGcsSpeakerCategories.insert(std::make_pair(Category, true));
}

void __cdecl EnableGcsListenerCategory(int Category)
{
    // FUN_06eebc60.
    GGcsListenerCategories.insert(std::make_pair(Category, true));
}

bool __cdecl IsGcsCommAllowed(int Category, FSystemInfo* System, unsigned int Speaker,
                              unsigned int Listener)
{
    // FUN_06eebd10. DAT_06fce368 == 0 (gcs_clamp on) refuses everything.
    if (ContentIsGcsClampEnabled())
        return false;

    // A speaker / listener that is not a SpacePop ship needs its category opened by
    // EnableGcsSpeakerCategory / EnableGcsListenerCategory.
    FContentSpaceObj* Object = System->FindSpaceObj(Speaker);
    if (Object != nullptr && !static_cast<FContentNpcShip*>(Object)->IsSpacePopShip() &&
        GGcsSpeakerCategories.find(Category) == GGcsSpeakerCategories.end())
    {
        return false;
    }
    Object = System->FindSpaceObj(Listener);
    if (Object != nullptr && !static_cast<FContentNpcShip*>(Object)->IsSpacePopShip() &&
        GGcsListenerCategories.find(Category) == GGcsListenerCategories.end())
    {
        return false;
    }

    // Categories 1..4 need the speaker (ship or solar NPC) with slot 1 false; a player
    // listener is then always allowed.
    if (Category > 0 && Category <= 4)
    {
        FContentSpaceObj* const SpeakerObject = System->FindShipOrSolar(Speaker);
        if (SpeakerObject == nullptr)
            return false;
        if (FSolarNpc* const SolarSpeaker = dynamic_cast<FSolarNpc*>(SpeakerObject))
        {
            if (SolarSpeaker->IsInCombat())
                return false;
        }
        else if (static_cast<FContentShipBase*>(SpeakerObject)->IsNpcShip())
            return false;
        if (FindContentPlayerStateByShip(&Listener) != nullptr)
            return true;
    }

    double const Now = GetContentGameTime();
    Vector Position;
    Matrix Orientation;
    if (pub::SpaceObj::GetLocation(Speaker != 0 ? Speaker : Listener, Position, Orientation) != 0)
        return false;

    // The newest remembered comm of the category within 10 km (squared distance below
    // DAT_06fb622c = 1e8).
    std::list<FGcsRecentComm>& Recent = GGcsRecentComms[Category];
    std::list<FGcsRecentComm>::iterator Near = Recent.begin();
    for (; Near != Recent.end(); ++Near)
    {
        float const DeltaX = Near->Position.x - Position.x;
        float const DeltaY = Near->Position.y - Position.y;
        float const DeltaZ = Near->Position.z - Position.z;
        double const LengthSquared = static_cast<double>(DeltaZ) * DeltaZ +
                                     static_cast<double>(DeltaY) * DeltaY +
                                     static_cast<double>(DeltaX) * DeltaX;
        if (LengthSquared < 100000000.0)
            break;
    }

    float Chance = 1.0f;
    if (Near != Recent.end())
    {
        // Too soon after it (interval strictly above the time since) refuses.
        double const RecentTime = static_cast<double>(Near->Time);
        std::map<int, float>::iterator Interval = GGcsCategoryIntervals.find(Category);
        float const MinInterval = Interval != GGcsCategoryIntervals.end()
                                      ? Interval->second
                                      : static_cast<float>(GGcsDefaultInterval);
        double const Since = GetContentTimeDistance(&Now, &RecentTime);
        if (static_cast<double>(MinInterval) > Since)
            return false;
    }
    else
    {
        // An interval of exactly -1 (DAT_06fb6220) turns the category off.
        std::map<int, float>::iterator Interval = GGcsCategoryIntervals.find(Category);
        double const MinInterval = Interval != GGcsCategoryIntervals.end()
                                       ? static_cast<double>(Interval->second)
                                       : GGcsDefaultInterval;
        if (MinInterval == -1.0)
            return false;
    }

    // rand() * 2^-15 below the category's chance (default 1.0).
    std::map<int, float>::iterator ChanceIt = GGcsCategoryChances.find(Category);
    if (ChanceIt != GGcsCategoryChances.end())
        Chance = ChanceIt->second;
    double const Roll = static_cast<double>(rand()) * static_cast<double>(1.0f / 32768.0f);
    return Roll < static_cast<double>(Chance);
}

void __cdecl RecordGcsComm(int Category, FSystemInfo* System, unsigned int Speaker,
                           unsigned int Listener)
{
    // FUN_06eeb920. The position starts at -0.1 (0xbdcccccd) and keeps it when GetLocation
    // fails (the result is not tested).
    FGcsRecentComm Comm;
    Comm.System = System;
    Comm.Time = static_cast<float>(GetContentGameTime());
    Comm.Position.x = -0.1f;
    Comm.Position.y = -0.1f;
    Comm.Position.z = -0.1f;
    Comm.Speaker = Speaker;
    Comm.Listener = Listener;
    Comm.Field_0x1c = -1;
    Matrix Orientation;
    pub::SpaceObj::GetLocation(Speaker != 0 ? Speaker : Listener, Comm.Position, Orientation);

    // At most 10 per category, newest at the front.
    std::list<FGcsRecentComm>& Recent = GGcsRecentComms[Category];
    if (Recent.size() == 10)
        Recent.pop_back();
    Recent.push_front(Comm);
}
