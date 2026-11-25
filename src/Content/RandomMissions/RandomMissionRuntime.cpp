// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "RandomMissions/RandomMissionRuntime.h"

#include "Core/ContentMessageRouter.h"   // GMessageRouter (DAT_06fcf254)
#include "NPCs/FactionProps.h"           // GetFactionNames, FFactionNames
#include "NPCs/NpcShips.h"               // GetSecondNameValues & co (TU42 name readers)
#include "Encounters/RandomEncounters.h"       // FRmKillableSolar, FRmCriticalLoot
#include "RandomMissions/RandomMissionGen.h"       // GetRmZoneAtPosition
#include "RandomMissions/RandomMissionOffer.h"
#include "Universe/SystemInfo.h"             // FSystemInfo::SystemId
#include "common_id.h"
#include "common_ini.h"
#include "common_net.h"
#include "common_server.h"

#include <string.h>

// =======================================================================================
// TU61 message types (static initialisers LAB_06f842a0 ... LAB_06f84520, 0x20 apart, in
// this order). Retail increments the exported generator inline; the port calls the exported
// Common function that does the same.
// =======================================================================================

unsigned int const GRmMsgStartMachine = StateMachineMessage::GenerateUniqueMessageType();         // 06f842a0
unsigned int const GRmMsgMachineStarted = StateMachineMessage::GenerateUniqueMessageType();       // 06f842c0
unsigned int const GRmMsgMachineSucceeded = StateMachineMessage::GenerateUniqueMessageType();     // 06f842e0
unsigned int const GRmMsgMachineFailed = StateMachineMessage::GenerateUniqueMessageType();        // 06f84300
unsigned int const GRmMsgRefreshObjectives = StateMachineMessage::GenerateUniqueMessageType();    // 06f84320
unsigned int const GRmMsgShipDestroyed = StateMachineMessage::GenerateUniqueMessageType();        // 06f84340
unsigned int const GRmMsgGroupDestroyed = StateMachineMessage::GenerateUniqueMessageType();       // 06f84360
unsigned int const GRmMsgGroupTargetsDestroyed = StateMachineMessage::GenerateUniqueMessageType();// 06f84380
unsigned int const GRmMsgSolarDestroyed = StateMachineMessage::GenerateUniqueMessageType();       // 06f843a0
unsigned int const GRmMsgCriticalLootSpawned = StateMachineMessage::GenerateUniqueMessageType();  // 06f843c0
unsigned int const GRmMsgLootDestroyed = StateMachineMessage::GenerateUniqueMessageType();        // 06f843e0
unsigned int const GRmMsgLootPickedUp = StateMachineMessage::GenerateUniqueMessageType();         // 06f84400
unsigned int const GRmMsgCriticalLootGone = StateMachineMessage::GenerateUniqueMessageType();     // 06f84420
unsigned int const GRmMsgLootHolderDocked = StateMachineMessage::GenerateUniqueMessageType();     // 06f84440
unsigned int const GRmMsgLootDropped = StateMachineMessage::GenerateUniqueMessageType();          // 06f84460
unsigned int const GRmMsgLootLost = StateMachineMessage::GenerateUniqueMessageType();             // 06f84480
unsigned int const GRmMsgCountdownTick = StateMachineMessage::GenerateUniqueMessageType();        // 06f844a0
unsigned int const GRmMsgSpawnGroup = StateMachineMessage::GenerateUniqueMessageType();           // 06f844c0
unsigned int const GRmMsgConversation = StateMachineMessage::GenerateUniqueMessageType();         // 06f844e0
unsigned int const GRmMsgPlayerLeft = StateMachineMessage::GenerateUniqueMessageType();           // 06f84500
unsigned int const GRmMsgDropLoot = StateMachineMessage::GenerateUniqueMessageType();             // 06f84520

// FUN_06f86d90.
FRmMachineFailedMessage::FRmMachineFailedMessage(int InReason)
    : StateMachineMessage(GRmMsgMachineFailed)
    , Reason(InReason)
{
}

// FUN_06f86de0: StateMachineMessage::operator new(0x20); a failed new returns null.
StateMachineMessage* FRmMachineFailedMessage::clone() const
{
    return new FRmMachineFailedMessage(*this);
}

// FUN_06f86e40.
FRmPlayerLeftMessage::FRmPlayerLeftMessage(unsigned int InPlayerId)
    : StateMachineMessage(GRmMsgPlayerLeft)
    , PlayerId(InPlayerId)
{
}

// FUN_06f86e70: as FUN_06f86de0.
StateMachineMessage* FRmPlayerLeftMessage::clone() const
{
    return new FRmPlayerLeftMessage(*this);
}

// =======================================================================================
// .data name tables shared by TU61 and TU63
// =======================================================================================

FRmNamedValue const GRmTextParamNames[15] = {
    {"FSTRING_PARAM_NONE", 0},
    {"OFFER_GROUP", 1},
    {"HOSTILE_GROUP", 2},
    {"BIG_SOLAR", 3},
    {"OTHER_SOLAR", 4},
    {"CRITICAL_LOOT", 5},
    {"TARGET_FULL_NAME", 6},
    {"REWARD_MONEY", 7},
    {"FACTION_REWARD", 8},
    {"FACTION_HIT", 9},
    {"TIME_TO_RETURN_TO_BATTLE", 10},
    {"MISSION_DIFFICULTY", 11},
    {"OFFER_BASE", 12},
    {"TARGET_ZONE", 13},
    {"TARGET_SYSTEM", 14},
};

FRmNamedValue const GRmCommTriggerNames[17] = {
    {"NONE", 0},
    {"IN_SPACE_IN_SYSTEM", 1},
    {"IN_SPACE_NOT_IN_SYSTEM", 2},
    {"LEFT_COMBAT_AREA", 11},
    {"REENTERED_COMBAT_AREA", 12},
    {"GLOBAL_SUCCESS", 13},
    {"GLOBAL_FAIL_COMBAT_RANGE", 14},
    {"GLOBAL_FAIL_DESTROYED_CRITICAL_LOOT", 15},
    {"GLOBAL_FAIL_OTHER", 16},
    {"PLAYER_IN_PRE_BATTLE_SPAWN_RANGE", 3},
    {"SUCCESS_KILLED_ALL_GOING_TO_MAIN", 4},
    {"SUCCESS_NOT_KILLED_ALL_GOING_TO_MAIN", 5},
    {"PLAYER_IN_MAIN_BATTLE_SOLAR_SPAWN_RANGE", 6},
    {"PLAYER_IN_MAIN_BATTLE_SHIP_SPAWN_RANGE", 7},
    {"FIRST_FRIENDLY_SPAWN", 8},
    {"FIRST_BIG_SHIP_SPAWN", 10},
    {"FIRST_DEFENSIVE_SHIP_SPAWN", 9},
};

FRmNamedValue const GRmCommTargetNames[12] = {
    {"NONE", 0},
    {"BASE", 1},
    {"BIG_SOLAR", 2},
    {"BIG_SHIP", 3},
    {"DEFENSIVE_SHIP", 4},
    {"FRIENDLY_SHIP", 5},
    {"PLAYER", 6},
    {"ALL_PLAYERS", 7},
    {"PLAYERS_IN_RANGE", 8},
    {"PLAYERS_OUT_OF_RANGE", 9},
    {"PLAYERS_IN_SYSTEM", 10},
    {"PLAYERS_OUT_OF_SYSTEM", 11},
};

// =======================================================================================
// TU61 -- text with parameters (vtable 06fbc700)
// =======================================================================================

// Inlined at every owner (e.g. FUN_06f9efb0, FUN_06f99560).
FRmParamText::FRmParamText()
    : StrId(0)
{
}

// FUN_06f85550.
FRmParamText::~FRmParamText()
{
}

// FUN_06f84540. A name not in the table appends the previous lookup's value; for the first
// value that is retail's uninitialised stack slot, for which the port uses 0
// (FSTRING_PARAM_NONE).
void FRmParamText::Parse(INI_Reader* Reader, unsigned int Index)
{
    StrId = Reader->get_value_int(Index);
    unsigned int ValueIndex = Index + 1;
    int Param = 0;
    while (!Reader->is_value_empty(ValueIndex))
    {
        char const* Name = Reader->get_value_string(ValueIndex);
        ++ValueIndex;
        for (int Row = 0; Row < 15; ++Row)
        {
            if (GRmTextParamNames[Row].Name != nullptr &&
                _stricmp(Name, GRmTextParamNames[Row].Name) == 0)
            {
                Param = GRmTextParamNames[Row].Value;
                break;
            }
        }
        Params.insert(Params.end(), Param);
    }
}

// LAB_06f84740.
FmtStr* FRmParamText::Build(IRmTextParamSource* Source)
{
    if (Text.MadLibId == 0)
    {
        Text.begin_mad_lib(static_cast<unsigned int>(StrId));
        for (std::vector<int>::iterator Param = Params.begin(); Param != Params.end(); ++Param)
            Source->GetTextParam(*Param, &Text);
        Text.end_mad_lib();
    }
    return &Text;
}

// LAB_06f85430.
bool FRmParamText::IsEmpty() const
{
    return StrId == 0;
}

// =======================================================================================
// TU61 -- the random-mission offer (RandomMissionOffer.h)
// =======================================================================================

unsigned int GRmShipGroupSerial = 0;                            // DAT_06fd3c08

// FUN_06f89400. The members' defaults are the ctor's stores; +0x2c..+0x38 are not written by
// retail (see the header).
FRmOffer::FRmOffer(int InMachineType)
    : MachineType(InMachineType)
{
}

// FUN_06f883e0.
FRmOffer::~FRmOffer()
{
}

// FUN_06f89550.
bool FRmOffer::GetTextParam(int Param, FmtStr* Out)
{
    switch (Param)
    {
    case 1:     // OFFER_GROUP
    {
        unsigned int const Group = static_cast<unsigned int>(OfferGroup);
        Out->append_rep_group(Group);
        return true;
    }
    case 2:     // HOSTILE_GROUP
        Out->append_rep_group(HostileGroup);
        return true;
    case 7:     // REWARD_MONEY
        Out->append_int(static_cast<unsigned int>(Reward));
        return true;
    case 8:     // FACTION_REWARD: x 100.0f (DAT_06fb8574) on x87, exact in double
        Out->append_int(static_cast<unsigned int>(
            static_cast<int>(static_cast<double>(RepReward) * 100.0)));
        return true;
    case 9:     // FACTION_HIT
        Out->append_int(static_cast<unsigned int>(
            static_cast<int>(static_cast<double>(RepHit) * 100.0)));
        return true;
    case 11:    // MISSION_DIFFICULTY
        Out->append_int(static_cast<unsigned int>(static_cast<int>(Difficulty)));
        return true;
    case 12:    // OFFER_BASE
        Out->append_base(OfferBase);
        return true;
    case 14:    // TARGET_SYSTEM
    {
        unsigned int NameId = static_cast<unsigned int>(Param);    // the argument slot
        pub::System::GetName(System->SystemId, NameId);
        Out->append_string(NameId);
        return true;
    }
    default:    // 3, 4, 5, 6, 10, 13 and everything outside 1..14
        return false;
    }
}

// =======================================================================================
// TU61 comm sequence lines and conversations
// =======================================================================================

// FUN_06f84790.
FRmLine::FRmLine()
    : FGcsPlayerListLine(nullptr)
    , SpeakerKind(0)
    , TargetKind(0)
    , bPrepared(0)
{
    Text.Length = 0;
    Text.Data[0] = '\0';
}

// FUN_06f849c0; the base CanPlay (FUN_06ed6710) is called directly.
bool FRmLine::CanPlay()
{
    if (FGcsPlayerListLine::CanPlay() && bPrepared != 0)
        return true;
    return false;
}

// FUN_06f84820: new 0x90 member-wise copy; a failed new returns null.
FGcsCloneableLine* FRmLine::Clone()
{
    return new FRmLine(*this);
}

// FUN_06f84a20. The old line pointers are not deleted.
void FRmConversationBase::CloneLines()
{
    std::vector<FGcsLine*> Copies;
    for (std::vector<FGcsLine*>::iterator It = Lines.begin(); It != Lines.end(); ++It)
    {
        FGcsLine* const Copy = static_cast<FGcsCloneableLine*>(*It)->Clone();
        Copies.insert(Copies.end(), 1, Copy);
    }
    Lines = Copies;
}

// FUN_06f84c00.
bool FRmConversation::IsValid()
{
    if (!bReady)
        return false;
    for (std::vector<FGcsLine*>::iterator It = Lines.begin(); It != Lines.end(); ++It)
    {
        if (!(*It)->CanPlay())
            return false;
    }
    return true;
}

// FUN_06f84c40: new 0x24 member copy, then the copy's slot 6.
FRmConversationBase* FRmConversation::Clone()
{
    FRmConversation* const Copy = new FRmConversation(*this);
    Copy->CloneLines();
    return Copy;
}

// FUN_06f71080 (one ICF body for every class storing its argument at +0x20).
void FRmConversation::SetTrigger(int InTrigger)
{
    Trigger = InTrigger;
}

// LAB_06f84d30.
int FRmConversation::GetTrigger()
{
    return Trigger;
}

// LAB_06f84d40.
void FRmConversation::MarkReady()
{
    bReady = true;
}

// LAB_06f84d50.
float FRmConversation::GetDelay()
{
    return Delay;
}

// LAB_06f84d60.
void FRmConversation::SetDelay(float InDelay)
{
    Delay = InDelay;
}

// FUN_06f89230.
FRmObjectiveMap::~FRmObjectiveMap()
{
    for (std::map<int, FRmParamText*>::iterator It = Objectives.begin(); It != Objectives.end();
         ++It)
    {
        if (It->second != nullptr)
        {
            delete It->second;
            It->second = nullptr;
        }
    }
}

// FUN_06f84d70: MessageHandler(DAT_06fcf254) registers with the router.
FRmOfferComponent::FRmOfferComponent()
    : MessageHandler(GMessageRouter)
{
}

// FUN_06f84e60.
FRmOfferComponent::~FRmOfferComponent()
{
}

// FUN_06ebf6f0: empty.
void FRmOfferComponent::receive_message(StateMachineMessage* /*Msg*/)
{
}

// FUN_06f88500.
FRmShipComponent::~FRmShipComponent()
{
}

// FUN_06f88500 through vtable 06fbc8dc (ICF with the ship component's).
FRmTargetShipComponent::~FRmTargetShipComponent()
{
}

// FUN_06f88620.
FRmSolarComponent::~FRmSolarComponent()
{
}

// FUN_06f862e0.
FRmShipSpawn::FRmShipSpawn()
{
}

// FUN_06f86420.
FRmShipSpawn::~FRmShipSpawn()
{
}

// FUN_06f865e0.
FRmShipGroup::FRmShipGroup()
{
    Orientation.E[0][0] = 1.0f;
    Orientation.E[0][1] = 0.0f;
    Orientation.E[0][2] = 0.0f;
    Orientation.E[1][0] = 0.0f;
    Orientation.E[1][1] = 1.0f;
    Orientation.E[1][2] = 0.0f;
    Orientation.E[2][0] = 0.0f;
    Orientation.E[2][1] = 0.0f;
    Orientation.E[2][2] = 1.0f;
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
    Name.Length = 0;
    Name.Data[0] = '\0';
    Formation.Id = CreateID("transport_liberty");
    ++GRmShipGroupSerial;
    Serial = GRmShipGroupSerial;
}

// FUN_06f88240; the component ctors are inlined. Unwind states 1..0xc follow the member order.
FRmVignetteOffer::FRmVignetteOffer(int InMachineType)
    : FRmOffer(InMachineType)
{
}

// FUN_06f88710; the member dtors are inlined.
FRmVignetteOffer::~FRmVignetteOffer()
{
    ReleaseTargetName();
}

// FUN_06f88940. The first ship carrying the name, in a spawned group, keeps it; otherwise the
// faction's name tables (GetFactionNames, not null-tested) get the parts back.
void FRmVignetteOffer::ReleaseTargetName()
{
    if (TargetName.First.MadLibId == 0 && TargetName.Second.MadLibId == 0)
        return;

    bool bFound = false;
    unsigned char bKeep = 0;
    std::vector<FRmShipGroup> const* const GroupLists[2] = {&TargetShips.Groups, &Ships.Groups};
    for (int ListIndex = 0; ListIndex < 2 && !bFound; ++ListIndex)
    {
        std::vector<FRmShipGroup> const* const Groups = GroupLists[ListIndex];
        for (std::vector<FRmShipGroup>::const_iterator Group = Groups->begin();
             Group != Groups->end() && !bFound; ++Group)
        {
            for (std::vector<FRmShipSpawn>::const_iterator Ship = Group->Ships.begin();
                 Ship != Group->Ships.end(); ++Ship)
            {
                if (Ship->TargetName != nullptr)
                {
                    bKeep = Group->bSpawned;
                    bFound = true;
                    break;
                }
            }
        }
    }
    if (bFound && bKeep != 0)
        return;

    FFactionNames* const Names = GetFactionNames(&TargetName.Affiliation);
    int PilotFirst = 0;
    int PilotLast = 0;
    GetSecondNameValues(&TargetName.First, &TargetName.Second, &PilotFirst, &PilotLast);
    if (PilotFirst != 0 && PilotLast != 0)
    {
        Names->ReleasePilotName(PilotFirst, PilotLast, true);
    }
    else
    {
        int LargeShipName = 0;
        GetSecondNameThirdValue(&TargetName.First, &TargetName.Second, &LargeShipName);
        if (LargeShipName != 0)
            Names->ReleaseLargeShipName(LargeShipName, true);
    }

    int Desig = 0;
    GetFirstNameValue(&TargetName.First, &TargetName.Second, &Desig);
    int DesigNumber = -1;
    GetFirstNameLastValue(&TargetName.First, &TargetName.Second, &DesigNumber);
    Names->ReleaseFormationDesig(Desig, DesigNumber, true);
}

// FUN_06f89650.
void FRmVignetteOffer::CountCriticalLoot(std::vector<FRmShipGroup> const* Groups,
                                         FRmCriticalLoot const** OutLoot,
                                         int* InOutCount) const
{
    for (std::vector<FRmShipGroup>::const_iterator Group = Groups->begin();
         Group != Groups->end(); ++Group)
    {
        for (std::vector<FRmShipSpawn>::const_iterator Ship = Group->Ships.begin();
             Ship != Group->Ships.end(); ++Ship)
        {
            for (std::vector<FRmLootSpawn>::const_iterator Loot = Ship->Loot.begin();
                 Loot != Ship->Loot.end(); ++Loot)
            {
                if (Loot->Kind != RmLootCritical)
                    continue;
                *OutLoot = Loot->CriticalLoot;
                *InOutCount += Loot->Info.Units;
                if (*InOutCount > 1)
                    break;
            }
        }
    }
}

// LAB_06f896f0. OTHER_SOLAR with more than one solar uses the second one's string id + 1
// (the plural text). TARGET_FULL_NAME for a large ship appends the second value the first
// reader left, as retail.
bool FRmVignetteOffer::GetTextParam(int Param, FmtStr* Out)
{
    if (FRmOffer::GetTextParam(Param, Out))
        return true;

    switch (Param)
    {
    case 3:
    {
        for (std::list<FRmSolarSpawn>::const_iterator Solar = Solars.Solars.begin();
             Solar != Solars.Solars.end(); ++Solar)
        {
            if (Solar->SolarType == 0)
            {
                Out->append_installation(Solar->Arch->StringId);
                return true;
            }
        }
        return false;
    }
    case 4:
    {
        int Count = 0;
        FRmKillableSolar const* Arch = nullptr;
        for (std::list<FRmSolarSpawn>::const_iterator Solar = Solars.Solars.begin();
             Solar != Solars.Solars.end(); ++Solar)
        {
            if (Solar->SolarType == 1 || Solar->SolarType == 2)
            {
                Arch = Solar->Arch;
                ++Count;
                if (Count > 1)
                {
                    Out->append_installation(Arch->StringId + 1);
                    return true;
                }
            }
        }
        if (Count == 0)
            return false;
        Out->append_installation(Arch->StringId);
        return true;
    }
    case 5:
    {
        FRmCriticalLoot const* Loot = nullptr;
        int Count = 0;
        CountCriticalLoot(&TargetShips.Groups, &Loot, &Count);
        if (Count <= 1)
        {
            CountCriticalLoot(&Ships.Groups, &Loot, &Count);
            if (Count <= 1)
            {
                for (std::list<FRmSolarSpawn>::const_iterator Solar = Solars.Solars.begin();
                     Solar != Solars.Solars.end(); ++Solar)
                {
                    for (std::vector<FRmLootSpawn>::const_iterator It = Solar->Loot.begin();
                         It != Solar->Loot.end(); ++It)
                    {
                        if (It->Kind != RmLootCritical)
                            continue;
                        Count += It->Info.Units;
                        Loot = It->CriticalLoot;
                        if (Count > 1)
                            break;
                    }
                }
            }
        }
        if (Count == 0)
            return false;
        Out->append_loot(Loot->Name);
        return true;
    }
    case 6:
    {
        int First = 0;
        int Second = 0;
        GetSecondNameValues(&TargetName.First, &TargetName.Second, &First, &Second);
        if (First != 0 && Second != 0)
        {
            Out->append_string(static_cast<unsigned int>(First));
            Out->append_string(static_cast<unsigned int>(Second));
            return true;
        }
        int LargeShipName = 0;
        GetSecondNameThirdValue(&TargetName.First, &TargetName.Second, &LargeShipName);
        if (LargeShipName == 0)
            return false;
        FFactionNames* const Names = GetFactionNames(&TargetName.Affiliation);
        Out->append_string(static_cast<unsigned int>(Names->GetLargeShipDesig()));
        Out->append_string(static_cast<unsigned int>(Second));
        return true;
    }
    case 13:
    {
        Vector Position;
        if (TargetShips.bActive)
            Position = TargetShips.Position;
        else if (Ships.bActive)
            Position = Ships.Position;
        else
            Position = Solars.Position;
        unsigned int const Zone = GetRmZoneAtPosition(System, &Position);
        Out->append_zone_id(Zone);
        return true;
    }
    default:
        return false;
    }
}
