// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU27 (06eef190-06ef2070): the "DestroyMission" controller (FDestroyMission),
// the random mission (its create parms) and the random-mission loader slots 3, 4, 6 and 7
// with their helpers.

#include "RandomMissions/RandomMission.h"
#include "RandomMissions/RandomMissionObject.h"
#include "Core/ContentMessageRouter.h"
#include "common_misc.h"
#include "common_net.h"

#include "Bases/BaseDB.h"
#include "Core/ContentStaticIds.h"   // TU27 static IDs 06eef190..06eef370 (DAT_06fcf24c..DAT_06fcf210)
#include "Core/ContentDefaultId.h"   // TU27 06eef390 (CreateID("default") -> DAT_06fcf20c)
#include "Core/ContentInternal.h"
#include "Core/ContentPlayerState.h"
#include "NPCs/FactionProps.h"
#include "Encounters/RandomEncounters.h"
#include "RandomMissions/RandomMissionDB.h"
#include "RandomMissions/RandomMissionGen.h"
#include "RandomMissions/RandomMissionMachine.h"   // FRmRuntimeMachine, CreateRmRuntimeMachine
#include "Universe/SystemConnections.h"
#include "Universe/SystemInfo.h"
#include "common_goods.h"
#include "common_ini.h"
#include "common_server.h"
#include "common_text.h"
#include "common_weighted_vector.h"
#include "x86math.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

// DAT_06fcf1f8 (static init LAB_06ef04d0, atexit FUN_06ef04f0). Only TU27 reads it, so its
// place in the static-init order is free.
std::vector<unsigned int> GRmObjectiveZones;

namespace
{
    // DAT_06fcf250 -- this TU's cached 3DMathEngine (FUN_06fa8fa0 on first use).
    I3DMathEngine* GRmMissionMath = nullptr;

    // .rdata DAT_06fb7608: game units per range unit (300).
    float const GRmRangeScale = 300.0f;

    // Zone enumerator of FUN_06ef1810 (vtable 06fb782c, 0x14 bytes); the reader is unused.
    struct FRmZoneEnumerator : public pub::System::ZoneEnumerator
    {
        std::vector<unsigned int> Zones;            // +0x04 (alloc byte) / +0x08..+0x10

        void OnZone(unsigned int const* ZoneId, INI_Reader* /*Reader*/) override
        {
            Zones.push_back(*ZoneId);
        }
    };

    // The zone filter inlined in FUN_06ef0e30 / FUN_06ef0f20 / FUN_06ef1240: every zone of
    // the InZones result that System knows and whose mission_type is lawful or unlawful
    // (+0x12 / +0x13) is appended once (FUN_06f95e00).
    void CollectMissionZones(FSystemInfo* System, Vector const& Position, float Radius,
                             std::vector<unsigned int>* OutZones)
    {
        unsigned int ZoneIds[0x100];
        // Retail leaves the count uninitialised; pub::System::InZones always writes it.
        unsigned int Count = 0;
        pub::System::InZones(System->SystemId, Position, Radius, ZoneIds, 0x100, Count);
        for (unsigned int Index = 0; Index < Count; ++Index)
        {
            FSystemZone* Zone = System->FindZone(ZoneIds[Index]);
            if (Zone != nullptr && (Zone->bLawful || Zone->bUnlawful))
                RmInsertUnique(OutZones, ZoneIds[Index]);
        }
    }

    // The population filter inlined twice in FUN_06ef1810: every populating group except
    // 0x62e8 that OfferGroup feels at or below -0.2 (DAT_06fb75e0; greater and unordered skip)
    // is appended once (FUN_06f95e00). pub::Zone::GetPopulation's result is not tested.
    void CollectHostileGroups(unsigned int ZoneId, int OfferGroup,
                              std::vector<unsigned int>* OutGroups)
    {
        weighted_vector<unsigned int> const* Population = nullptr;
        pub::Zone::GetPopulation(ZoneId, Population);
        unsigned int const OfferGroupId = static_cast<unsigned int>(OfferGroup);
        for (std::vector<std::pair<unsigned int, float>>::const_iterator It =
                 Population->Items.begin();
             It != Population->Items.end(); ++It)
        {
            // TODO: which reputation group 0x62e8 is (an id constant in the listing).
            if (It->first == 0x62e8)
                continue;
            // Retail keeps the previous stack value when GetReputation fails (-4, unknown
            // group) without writing it; the port starts from 0.
            float Feeling = 0.0f;
            pub::Reputation::GetReputation(OfferGroupId, It->first, Feeling);
            if (Feeling < -0.2f || Feeling == -0.2f)
                RmInsertUnique(OutGroups, It->first);
        }
    }

    // The random pick inlined in FUN_06ef15b0 and FUN_06ef1810, the same expression as
    // FUN_06f4d200 (signed division).
    unsigned int PickRandomId(std::vector<unsigned int> const& Ids)
    {
        int const Count = static_cast<int>(Ids.size());
        int Index = 0;
        if (Count - 1 != 0)
            Index = (std::rand() * Count) / 32768;
        return Ids[Index];
    }
}

// =======================================================================================
// FDestroyMission (RandomMissionObject.h)
// =======================================================================================

// DAT_06fcf208: this TU's message type (static init LAB_06eef3b0). Retail never reads it; it
// only advances the generator.
unsigned int const GDestroyMissionMessageType = StateMachineMessage::GenerateUniqueMessageType();

namespace
{
    // .rdata DAT_06fb75e4 (-1): the "any state" passed to the TU47 body.
    int const GDmAnyState = -1;

    // The router lookup of a runtime machine id (DAT_06fcf254 slot 3, MessageHandler part
    // - 0x10); a missing id gives null, which retail then calls through.
    FRmRuntimeMachine* DmLookupMachine(unsigned int Id)
    {
        MessageHandler* const Handler = GMessageRouter->LookupHandlerById(Id);
        if (Handler == nullptr)
            return nullptr;
        return static_cast<FRmRuntimeMachine*>(Handler);
    }

    // The recurring test of LAB_06eef740: a message for any state (delivery state -1) or for
    // the machine's current state.
    bool DmIsForCurrentState(StateMachine const* Machine, StateMachineMessage const* Message)
    {
        return Message->DeliveryState == -1 || Machine->get_state() == Message->DeliveryState;
    }

    // Inline find + erase of the first element equal to Id, if any.
    void DmEraseFirst(std::vector<unsigned int>* Ids, unsigned int Id)
    {
        std::vector<unsigned int>::iterator It = std::find(Ids->begin(), Ids->end(), Id);
        if (It != Ids->end())
            Ids->erase(It);
    }

    // FUN_06eb5f30 + FUN_06eb5770: appended when absent (the second find is not read).
    void DmAppendUnique(std::vector<unsigned int>* Ids, unsigned int Id)
    {
        if (std::find(Ids->begin(), Ids->end(), Id) == Ids->end())
            Ids->insert(Ids->end(), Id);
    }
}

// FUN_06eef3d0. TU47 base FUN_06f4b0d0; +0x240 = the parms (AddRef when non-null), +0x244 =
// the dest system, +0x248 objective list (vtable 06fb478c), +0x80 = the first offer's type
// byte (no empty-list test); then the group's process (called directly) with event type 1.
FDestroyMission::FDestroyMission(unsigned int const* InControllerId, FRandomMissionData* InData)
    : FRandomMissionObject(InControllerId, InData)
    , MissionData(InData)
{
    DestSystem = FindSystemInfo(&MissionData->DestSys);
    MissionType = MissionData->Offers.front()->Type;
    SMEventData const Enter(static_cast<STATE_MACHINE_EVENT_TYPE>(1));
    StateMachineGroup::process(&Enter);
}

// FUN_06eef550. Every +0x290 id is removed from the group, then remove_dying_machines; the
// members then die in retail order (three vectors, objective list, parms holder +0x240, TU47
// dtor FUN_06f4b460), which the port's reverse declaration order reproduces.
FDestroyMission::~FDestroyMission()
{
    for (int Index = 0; Index < static_cast<int>(RunningMachines.size()); ++Index)
        StateMachineGroup::remove_state_machine(RunningMachines[Index]);
    remove_dying_machines();
}

// LAB_06eef6e0: the first running machine's system (slot 8), else that of +0x244.
int FDestroyMission::GetTargetSystemId()
{
    if (!RunningMachines.empty())
        return static_cast<int>(DmLookupMachine(RunningMachines[0])->GetSystemId());
    return static_cast<int>(DestSystem->SystemId);
}

// LAB_06eef730: +0x240 +0x50.
int FDestroyMission::GetJumpPreference()
{
    return MissionData->JumpPref;
}

// LAB_06ea1480.
bool FDestroyMission::FailOnPlayerDeath()
{
    return false;
}

// FUN_06ef0480: then the TU38 body (thunk_FUN_06f0fc40).
void FDestroyMission::EndMission()
{
    for (int Index = 0; Index < static_cast<int>(RunningMachines.size()); ++Index)
        DmLookupMachine(RunningMachines[Index])->Shutdown();
    FMissionObject::EndMission();
}

// LAB_06ef0460: +0x10 is the reward the TU38 body splits.
void FDestroyMission::OnMissionSucceed()
{
    Reward = static_cast<unsigned int>(MissionData->Reward);
    FRandomMissionObject::OnMissionSucceed();
}

// FUN_06ef02a0: the reason text (slot 45; begin_mad_lib only) goes to the first player,
// broadcast; then the TU47 body FUN_06f4c6f0.
void FDestroyMission::FailMission(int Reason)
{
    FmtStr Text;
    Text.begin_mad_lib(GetReasonIds(Reason));
    if (!PlayerStates.empty())
    {
        unsigned int const Target = PlayerStates[0]->PlayerId;
        pub::Player::DisplayMissionMessage(Target, Text, static_cast<MissionMessageType>(2), true);
    }
    FRandomMissionObject::FailMission(Reason);
}

// FUN_06ef0390; then the TU47 body FUN_06f4c830.
void FDestroyMission::RemoveMissionPlayer(unsigned int InPlayerId, int Reason)
{
    FmtStr Text;
    Text.begin_mad_lib(GetReasonIds(Reason));
    pub::Player::DisplayMissionMessage(InPlayerId, Text, static_cast<MissionMessageType>(2), false);
    FRandomMissionObject::RemoveMissionPlayer(InPlayerId, Reason);
}

// LAB_06ef01d0 (jump table 06ef0258; 11 and 13 share 0x3325).
unsigned int FDestroyMission::GetReasonIds(int Reason)
{
    switch (Reason)
    {
    case 0: return 0x331d;
    case 1: return 0x331e;
    case 2: return 0x331f;
    case 3: return 0x3320;
    case 4: return 0x3321;
    case 5: return 0x3322;
    case 6: return 0x3328;
    case 7: return 0x3329;
    case 8: return 0x332a;
    case 9: return 0x3323;
    case 10: return 0x3324;
    case 11: return 0x3325;
    case 12: return 0x3326;
    case 13: return 0x3325;
    case 14: return 0x3327;
    default: return 1;
    }
}

// FUN_06ef0100: FUN_06f890f0 builds the runtime machine (0xb4 bytes, FUN_06f89a00) for offer
// types 1..3; its id is appended to +0x26c when absent.
void FDestroyMission::CreateRuntimeMachines()
{
    for (std::list<FRmOffer*>::iterator It = MissionData->Offers.begin();
         It != MissionData->Offers.end(); ++It)
    {
        FRmOffer* const Offer = *It;
        FRmRuntimeMachine* Machine = nullptr;
        Offer->Mission = this;
        CreateRmRuntimeMachine(Offer, &Machine);
        // No null test: an offer type outside 1..3 leaves Machine null and retail reads
        // get_id of null + 0x10.
        unsigned int const Id = static_cast<MessageHandler*>(Machine)->get_id();
        add_state_machine(Machine);
        if (std::find(PendingMachines.begin(), PendingMachines.end(), Id) ==
            PendingMachines.end())
            PendingMachines.push_back(Id);
    }
}

// LAB_06eef740. States: 0 shows the title and waits 1.1 s; 1 creates the runtime machines and
// starts the first pending one; 2 runs them one after another (DAT_06fd3bb8 started,
// DAT_06fd3bb0 failed -> 4, DAT_06fd3bb4 succeeded -> next pending or 3); 3 succeeds; 4 fails
// with the machine's reason (+0x23c). Every handled message answers true, anything else false.
bool FDestroyMission::OnStateEvent(SMEventData const* Event, int const& State)
{
    if (State < 0)
    {
        using pub::Controller::EMessage;
        // A type 4 event's int +0x08 (SMControllerEvent::ControllerId) is the controller message
        // id (FRandomMissionObject::HandleMissionMessage), cast back after the type test.
        SMControllerEvent const* Controller = static_cast<SMControllerEvent const*>(Event);
        SMMessage const* Wrapped = static_cast<SMMessage const*>(Event);
        if (Event->get_event_type() == 4 &&
            static_cast<EMessage>(Controller->ControllerId) == EMessage::Heartbeat)
        {
            RefreshAllObjectives();
            FRandomMissionObject::OnStateEvent(Event, GDmAnyState);
            return true;
        }
        if (Event->get_event_type() == 3 && DmIsForCurrentState(this, Wrapped->Message) &&
            Wrapped->Message->MessageType == GRmMsgMachineStarted)
        {
            if (RunningMachines.empty())
                set_state(2);
            unsigned int const Sender = Wrapped->Message->Sender;
            DmEraseFirst(&StartingMachines, Sender);
            DmAppendUnique(&RunningMachines, Sender);
            return true;
        }
        if (Event->get_event_type() == 3 && DmIsForCurrentState(this, Wrapped->Message) &&
            Wrapped->Message->MessageType == GRmMsgRefreshObjectives)
        {
            Field_0x25c = 1;
            RefreshAllObjectives();
            return true;
        }
        if (Event->get_event_type() == 4 &&
            static_cast<EMessage>(Controller->ControllerId) == EMessage::ObjectDestroyed)
        {
            if (RunningMachines.empty())
                FRandomMissionObject::OnStateEvent(Event, GDmAnyState);
            return true;
        }
        if (Event->get_event_type() == 4 &&
            (static_cast<EMessage>(Controller->ControllerId) == EMessage::DockRequest ||
             static_cast<EMessage>(Controller->ControllerId) == EMessage::Msg0x25))
        {
            // Retail scans the players for one with a ship (FUN_06ef00d0) and never reads the
            // result.
            for (int Index = 0; Index < static_cast<int>(PlayerStates.size()); ++Index)
            {
                if (RmPlayerHasShip(PlayerStates[Index]))
                    break;
            }
            FRandomMissionObject::OnStateEvent(Event, GDmAnyState);
            return true;
        }
        if (Event->get_event_type() == 4 &&
            static_cast<EMessage>(Controller->ControllerId) == EMessage::Msg0x27)
        {
            FRandomMissionObject::OnStateEvent(Event, GDmAnyState);
            return true;
        }
        if (Event->get_event_type() == 4 &&
            static_cast<EMessage>(Controller->ControllerId) == EMessage::GroupMemberLeft)
        {
            FRmPlayerLeftMessage Left(*static_cast<unsigned int const*>(Controller->DataPtr));
            for (int Index = 0; Index < static_cast<int>(RunningMachines.size()); ++Index)
                send_immediate_message(&Left, RunningMachines[Index]);
            FRandomMissionObject::OnStateEvent(Event, GDmAnyState);
            return true;
        }
        if (Event->get_event_type() == 4 &&
            static_cast<EMessage>(Controller->ControllerId) == EMessage::PlayerSave)
        {
            unsigned int const* Parms = static_cast<unsigned int const*>(Controller->DataPtr);
            if (pub::SinglePlayer() && Parms[0] == PlayerState->PlayerId)
            {
                // CanSave: FUN_06f8a590.
                if (RunningMachines.empty() || DmLookupMachine(RunningMachines[0])->CanSave())
                {
                    FILE* File = reinterpret_cast<FILE*>(static_cast<uintptr_t>(Parms[1]));
                    fprintf(File, "[RandomMission]\n");
                    fprintf(File, "type = %s\n", "DestroyMission");
                    MissionData->Save(File);
                }
            }
            FRandomMissionObject::OnStateEvent(Event, GDmAnyState);
            return true;
        }
        if (Event->get_event_type() == 4)
        {
            FRandomMissionObject::OnStateEvent(Event, GDmAnyState);
            return true;
        }
        return false;
    }

    if (State == 0)
    {
        if (Event->get_event_type() < 4 && Event->get_event_type() == 1)
        {
            MissionTitle.destroy();
            MissionTitle = FmtStr();
            MissionOffer.destroy();
            MissionOffer = FmtStr();
            MissionTitle.begin_mad_lib(0x32fa + MissionData->Offers.front()->Type);
            MissionTitle.end_mad_lib();
            MissionOffer = MissionData->Description;
            StateMachineMessage Timeout(SMM_TIMEOUT);
            send_delayed_message_to_me(&Timeout, 1.1f, SMMS_Self, 0);
            return true;
        }
        SMMessage const* Wrapped = static_cast<SMMessage const*>(Event);
        if (Event->get_event_type() == 3 && DmIsForCurrentState(this, Wrapped->Message) &&
            Wrapped->Message->MessageType == SMM_TIMEOUT)
        {
            set_state(1);
            return true;
        }
        return false;
    }

    if (State == 1)
    {
        if (Event->get_event_type() >= 4 || Event->get_event_type() != 1)
            return false;
        CreateRuntimeMachines();
        if (!PendingMachines.empty())
        {
            unsigned int const Id = PendingMachines[0];
            DmAppendUnique(&StartingMachines, Id);
            DmEraseFirst(&PendingMachines, Id);
            StateMachineMessage Start(GRmMsgStartMachine);
            send_delayed_message(&Start, 0.01f, Id);
        }
        return true;
    }

    if (State == 3)
    {
        if (Event->get_event_type() >= 4 || Event->get_event_type() != 1)
            return false;
        OnMissionSucceed();
        return true;
    }

    if (State == 4)
    {
        if (Event->get_event_type() >= 4 || Event->get_event_type() != 1)
            return false;
        FailMission(FailReason);
        return true;
    }

    if (State == 2)
    {
        if (Event->get_event_type() != 3)
            return false;
        SMMessage const* Wrapped = static_cast<SMMessage const*>(Event);
        StateMachineMessage const* Message = Wrapped->Message;
        if (DmIsForCurrentState(this, Message) && Message->MessageType == GRmMsgMachineFailed)
        {
            DmEraseFirst(&RunningMachines, Message->Sender);
            remove_state_machine(Message->Sender);
            FailReason = static_cast<FRmMachineFailedMessage const*>(Message)->Reason;
            set_state(4);
            return true;
        }
        if (Event->get_event_type() != 3)
            return false;
        if (!DmIsForCurrentState(this, Message) || Message->MessageType != GRmMsgMachineSucceeded)
            return false;
        DmEraseFirst(&RunningMachines, Message->Sender);
        remove_state_machine(Message->Sender);
        if (!RunningMachines.empty() || !StartingMachines.empty())
            return true;
        if (PendingMachines.empty())
        {
            set_state(3);
            return true;
        }
        unsigned int const Id = PendingMachines[0];
        DmAppendUnique(&StartingMachines, Id);
        DmEraseFirst(&PendingMachines, Id);
        StateMachineMessage Start(GRmMsgStartMachine);
        send_delayed_message(&Start, 0.01f, Id);
        return true;
    }
    return false;
}

// FUN_06ef00d0.
bool RmPlayerHasShip(FContentPlayerState const* State)
{
    return State->ShipObject != nullptr;
}

// Inlined in FUN_06eeec50: operator new(0x29c). The parms are the TU27 mission (CreateParms
// at +0).
IDLL* CreateDestroyMission(char const* /*Name*/, unsigned int ControllerId, void const* Parms)
{
    pub::Controller::CreateParms* Base =
        const_cast<pub::Controller::CreateParms*>(static_cast<pub::Controller::CreateParms const*>(Parms));
    return new (std::nothrow) FDestroyMission(&ControllerId, static_cast<FRandomMissionData*>(Base));
}

// =======================================================================================
// FRandomMissionData
// =======================================================================================

// FUN_06ef0600 (cdecl): operator new(0x88); always returns true, *OutMission 0 on failure.
bool FRandomMissionData::Create(FRandomMissionData** OutMission)
{
    *OutMission = new (std::nothrow) FRandomMissionData();
    return true;
}

// FUN_06ef06d0 (deleting dtor FUN_06ef0550, thunk LAB_06ef2060). The list, tree path and
// base (~FmtStr +0x24) then die in the port's member order.
FRandomMissionData::~FRandomMissionData()
{
    if (ObjectiveZone != 0)
    {
        std::vector<unsigned int>::iterator It =
            std::find(GRmObjectiveZones.begin(), GRmObjectiveZones.end(), ObjectiveZone);
        if (It != GRmObjectiveZones.end())
            GRmObjectiveZones.erase(It);
    }
    for (std::list<FRmOffer*>::iterator It = Offers.begin(); It != Offers.end(); ++It)
        (*It)->Release();
}

// FUN_06ef0810. The tree path is not saved.
void FRandomMissionData::Save(FILE* File)
{
    FRmMissionDataBase::Save(File);
    fprintf(File, "enemy_group = %u\n", EnemyGroup);
    fprintf(File, "dest_sys = %u\n", DestSys);
    fprintf(File, "objective_pos = %g,%g,%g\n", static_cast<double>(ObjectivePos.x),
            static_cast<double>(ObjectivePos.y), static_cast<double>(ObjectivePos.z));
    fprintf(File, "objective_zone = %u\n", ObjectiveZone);
    fprintf(File, "random_seed = %u\n", RandomSeed);
    fprintf(File, "\n");
}

// LAB_06ef0890. The base's result is not read. The position values are read as integers
// (FILD). FUN_06f937c0 appends the zone; the find that follows it in retail has no effect.
void FRandomMissionData::LoadValue(INI_Reader* Reader)
{
    FRmMissionDataBase::LoadValue(Reader);
    if (Reader->is_value("enemy_group"))
    {
        EnemyGroup = static_cast<unsigned int>(Reader->get_value_int(0));
        return;
    }
    if (Reader->is_value("dest_sys"))
    {
        DestSys = static_cast<unsigned int>(Reader->get_value_int(0));
        return;
    }
    if (Reader->is_value("objective_pos"))
    {
        ObjectivePos.x = static_cast<float>(Reader->get_value_int(0));
        ObjectivePos.y = static_cast<float>(Reader->get_value_int(1));
        ObjectivePos.z = static_cast<float>(Reader->get_value_int(2));
        return;
    }
    if (Reader->is_value("objective_zone"))
    {
        ObjectiveZone = static_cast<unsigned int>(Reader->get_value_int(0));
        if (std::find(GRmObjectiveZones.begin(), GRmObjectiveZones.end(), ObjectiveZone) ==
            GRmObjectiveZones.end())
            GRmObjectiveZones.insert(GRmObjectiveZones.end(), ObjectiveZone);
        return;
    }
    if (Reader->is_value("random_seed"))
        RandomSeed = static_cast<unsigned int>(Reader->get_value_int(0));
}

// =======================================================================================
// FRandomMissionLoader -- TU27 slots and helpers
// =======================================================================================

// FUN_06ef09c0. The argument is not read (Load passes 1, CreateMission 0). srand(RandomSeed)
// makes the tree walk repeat the saved mission. 2 when the offer list is still empty, else 0.
int FRandomMissionLoader::FinalizeMission(int /*Unused*/)
{
    int Result = 0;
    FRmOfferQuery Query;
    Query.OfferGroup = Mission->OfferGroup;
    Query.EnemyGroup = Mission->EnemyGroup;
    Query.Reward = Mission->Reward;
    Query.RepReward = Mission->RepReward;
    Query.RepHit = Mission->RepHit;
    Query.OfferBase = Mission->OfferBase;
    Query.Difficulty = Mission->Difficulty;
    Query.ObjectiveZone = Mission->ObjectiveZone;
    Query.ObjectivePos = Mission->ObjectivePos;
    Query.PlayerId = Mission->PlayerId;
    Query.DestSys = Mission->DestSys;

    TRmRef<FRmOffer> Offer;
    std::srand(Mission->RandomSeed);
    // DAT_06fcf25c vtable +0xc.
    if (GRandomMissionGenerator->BuildOffer(&Query, Offer.GetAddress(), &Mission->TreePath))
    {
        Mission->Offers.push_back(Offer.Get());           // FUN_06f1e390 at the end
        Offer->AddRef();
        Mission->Description = Offer->Text;                 // FmtStr::operator= (offer +0x3c)
    }

    if (Mission->Offers.empty())
        Result = 2;
    return Result;
}

// LAB_06ef0b00. The new mission is not null-tested; *OutMission gets it without an AddRef.
bool FRandomMissionLoader::Load(INI_Reader* Reader, FRandomMissionData** OutMission,
                                unsigned int PlayerId)
{
    bool bLoaded = false;
    INI_Reader::State SavedState;
    Reader->get_state(SavedState);
    Reader->reset();

    if (Mission != nullptr)
    {
        Mission->Release();
        Mission = nullptr;
    }
    FRandomMissionData::Create(&Mission);
    Mission->PlayerId = PlayerId;

    while (Reader->read_header())
    {
        if (!Reader->is_header("RandomMission"))
            continue;
        while (Reader->read_value())
            Mission->LoadValue(Reader);
        bLoaded = FinalizeMission(1) == 0;
    }

    Reader->set_state(SavedState);
    *OutMission = Mission;
    return bLoaded;
}

// FUN_06ef0be0. The title uses the LOADER's mission's system (retail reads [this + 8], not the
// argument). +0x58 (IconTextureId) carries the reward and +0x5c (Filter) the player; OfferId
// and OfferKind are not written.
void FRandomMissionLoader::GetOfferDescription(FRandomMissionData const* InMission,
                                               MissionVendorOfferDescription* OutDescription)
{
    OutDescription->BaseId = InMission->OfferBase;
    OutDescription->MissionClass =
        static_cast<unsigned int>(InMission->Offers.front()->Type) + 1;

    OutDescription->Title.begin_mad_lib(0x734);
    OutDescription->Title.append_system(Mission->DestSys);
    OutDescription->Title.end_mad_lib();

    OutDescription->Description.begin_mad_lib(0x72a);
    // Retail reuses the argument slot for the name id; GetGroupName always writes it.
    unsigned int GroupNameId = 0;
    pub::Reputation::GetGroupName(static_cast<unsigned int>(InMission->OfferGroup), GroupNameId);
    OutDescription->Description.append_string(GroupNameId);
    OutDescription->Description.end_mad_lib();

    OutDescription->Briefing = InMission->Description;
    OutDescription->IconTextureId = static_cast<unsigned int>(InMission->Reward);
    OutDescription->Filter = InMission->PlayerId;
}

// FUN_06ef0c90. The route is walked one connection at a time (TU41 FUN_06f29c10 /
// FUN_06f2a240); a missing hop or a gate locked for the player or everybody (TU53
// FUN_06f6d2b0) blocks it.
bool FRandomMissionLoader::IsRouteBlocked(unsigned int FromSystem, unsigned int ToSystem,
                                          unsigned int PlayerId)
{
    if (FromSystem == ToSystem)
        return false;
    if (FromSystem == 0 || ToSystem == 0 || PlayerId == 0)
        return true;

    bool bBlocked = false;
    std::vector<unsigned int> LockedGates;
    CollectContentGateLocks(PlayerId, LockedGates);
    unsigned int Current = FromSystem;
    while (Current != ToSystem)
    {
        unsigned int const Connection =
            GetNextHopConnectionObject(Current, ToSystem, Mission->JumpPref);
        if (Connection == 0)
        {
            bBlocked = true;
            break;
        }
        if (std::find(LockedGates.begin(), LockedGates.end(), Connection) != LockedGates.end())
        {
            bBlocked = true;
            break;
        }
        GetConnectionGotoSystem(Current, Connection, &Current);
    }
    return bBlocked;
}

// LAB_06ef0dc0.
bool FRandomMissionLoader::CanAccept(FRandomMissionData const* /*Unused*/, FmtStr* OutReason,
                                     int* OutCode)
{
    bool bBlocked = false;
    // Retail's slot holds the pushed ECX until pub::GetSystem writes it.
    unsigned int OfferSystem = 0;
    pub::GetSystem(OfferSystem, Mission->OfferBase);
    if (OfferSystem != Mission->DestSys)
    {
        bBlocked = IsRouteBlocked(OfferSystem, Mission->DestSys, Mission->PlayerId);
        if (bBlocked)
        {
            OutReason->begin_mad_lib(0x740);
            OutReason->end_mad_lib();
            *OutCode = 2;
        }
    }
    return !bBlocked;
}

// FUN_06ef0e30. Returns on Range <= 0 and unordered (DAT_06fb444c).
void FRandomMissionLoader::CollectZonesInRange(unsigned int /*PlayerId*/, FSystemInfo* System,
                                               Vector const* Position, float Range,
                                               std::vector<unsigned int>* OutZones)
{
    if (!(Range > 0.0f))
        return;
    CollectMissionZones(System, *Position, Range * GRmRangeScale, OutZones);
}

// FUN_06ef0f20. Range > 10 (DAT_06fb7614). For every trade lane within Range * 300
// (FUN_06f767d0) the nearer end is the entry and the other the exit; the exit is searched
// again with the range left (the recursion also runs when it is <= 0 and returns at once).
void FRandomMissionLoader::CollectZonesAlongTradeLanes(unsigned int PlayerId,
                                                       FSystemInfo* System,
                                                       Vector const* Position, float Range,
                                                       std::vector<unsigned int>* OutZones)
{
    if (!(Range > 10.0f))
        return;

    std::vector<FTradeLane*> Lanes;
    System->FindTradeLanesNear(Position, Range * GRmRangeScale, &Lanes);
    for (std::vector<FTradeLane*>::iterator LaneIt = Lanes.begin(); LaneIt != Lanes.end();
         ++LaneIt)
    {
        FTradeLane const* Lane = *LaneIt;
        Vector const& Start = Lane->StartPosition;
        Vector const& End = Lane->EndPosition;
        // Retail builds Start - End (FUN_06eb7810) and never reads it.

        // Each difference is stored as a float; the sums of squares (z, y, x order) are
        // stored as floats too.
        float const StartDx = Start.x - Position->x;
        float const StartDy = Start.y - Position->y;
        float const StartDz = Start.z - Position->z;
        float const StartDistSq = static_cast<float>(
            static_cast<double>(StartDz) * StartDz + static_cast<double>(StartDy) * StartDy +
            static_cast<double>(StartDx) * StartDx);
        float const EndDx = End.x - Position->x;
        float const EndDy = End.y - Position->y;
        float const EndDz = End.z - Position->z;
        float const EndDistSq = static_cast<float>(
            static_cast<double>(EndDz) * EndDz + static_cast<double>(EndDy) * EndDy +
            static_cast<double>(EndDx) * EndDx);

        if (GRmMissionMath == nullptr)
            GRmMissionMath = ContentAcquire3DMathEngine();

        // The start only when strictly nearer; unordered takes the end branch.
        float Distance = 0.0f;
        Vector Exit;
        if (StartDistSq < EndDistSq)
        {
            GRmMissionMath->Sqrt(Distance, StartDistSq);
            Exit = End;
        }
        else
        {
            GRmMissionMath->Sqrt(Distance, EndDistSq);
            Exit = Start;
        }

        if (!(Distance > 0.0f))
            continue;
        double const Remaining = static_cast<double>(Range) -
                                 static_cast<double>(Distance) / GRmRangeScale - 10.0;
        if (!(Remaining > 0.0))
            continue;

        // DAT_06fb7610: the recursion gets the float, the compare uses the unrounded value.
        double const ExitRangeExact = Remaining - 10.0;
        float const ExitRange = static_cast<float>(ExitRangeExact);
        if (ExitRangeExact > 0.0)
            CollectMissionZones(System, Exit, ExitRange * GRmRangeScale, OutZones);

        CollectZonesAlongTradeLanes(PlayerId, System, &Exit, ExitRange, OutZones);
        CollectZonesThroughJumps(PlayerId, System, &Exit, ExitRange, OutZones);
    }
}

// FUN_06ef1240. Range > 45 (DAT_06fb7618). For every usable jump the far system is loaded
// (FUN_06f4d5e0) and its jump back (ObjectId == GotoObject) is the arrival point, searched
// again with the range left.
void FRandomMissionLoader::CollectZonesThroughJumps(unsigned int PlayerId, FSystemInfo* System,
                                                    Vector const* Position, float Range,
                                                    std::vector<unsigned int>* OutZones)
{
    if (!(Range > 45.0f))
        return;

    // Retail builds an empty std::list (FUN_06f93790; 0xc-byte node, so a 4-byte element)
    // and never uses it; unsigned int stands in for the unknowable element type.
    std::list<unsigned int> UnusedList;

    // TU48 FUN_06f50650 (no null test) +0x58, 0 = any jump type.
    unsigned int OfferGroupId = static_cast<unsigned int>(Mission->OfferGroup);
    int const JumpPreference = GetFactionProps(&OfferGroupId)->JumpPreference;

    for (unsigned int JumpIndex = 0; JumpIndex < System->Jumps.size(); ++JumpIndex)
    {
        FSystemJump const Jump = System->Jumps[JumpIndex];

        float const Dx = Position->x - Jump.Position.x;
        float const Dy = Position->y - Jump.Position.y;
        float const Dz = Position->z - Jump.Position.z;
        double const DistanceExact =
            std::sqrt(static_cast<double>(Dz) * Dz + static_cast<double>(Dy) * Dy +
                      static_cast<double>(Dx) * Dx);
        float const Distance = static_cast<float>(DistanceExact);
        if (!(DistanceExact > 0.0))
            continue;
        if (JumpPreference != 0 && JumpPreference != Jump.Type)
            continue;

        double const RemainingExact = static_cast<double>(Range) -
                                      static_cast<double>(Distance) / GRmRangeScale - 45.0;
        float const Remaining = static_cast<float>(RemainingExact);
        if (!(RemainingExact > 0.0))
            continue;

        // TU53 FUN_06f6ce90: the gate locked for this player or for everybody.
        if (IsContentGateLocked(&PlayerId, &Jump.GotoObject))
            continue;

        FSystemInfo* Target = FindSystemInfo(&Jump.GotoSystem);
        RegisterRmDestSystem(Target->SystemId);

        bool bFound = false;
        Vector Arrival;
        Arrival.x = 1e10f;                          // 0x501502f9
        Arrival.y = 1e10f;
        Arrival.z = 1e10f;
        for (unsigned int BackIndex = 0; BackIndex < Target->Jumps.size(); ++BackIndex)
        {
            if (Target->Jumps[BackIndex].ObjectId == Jump.GotoObject)
            {
                Arrival = Target->Jumps[BackIndex].Position;
                bFound = true;
                break;
            }
        }
        if (!bFound)
            continue;
        // DAT_06fb781c: equal skips, unordered goes on.
        if (Arrival.x == 1e10f || Arrival.y == 1e10f || Arrival.z == 1e10f)
            continue;

        double const ArrivalRangeExact = static_cast<double>(Remaining) - 10.0;
        float const ArrivalRange = static_cast<float>(ArrivalRangeExact);
        if (ArrivalRangeExact > 0.0)
            CollectMissionZones(Target, Arrival, ArrivalRange * GRmRangeScale, OutZones);

        CollectZonesAlongTradeLanes(PlayerId, Target, &Arrival, ArrivalRange, OutZones);
        CollectZonesThroughJumps(PlayerId, Target, &Arrival, ArrivalRange, OutZones);
    }
}

// FUN_06ef15b0. A candidate survives when it is not already an objective and the offering
// faction's legality (TU48 record +0x94) matches the zone's mission_type. 0 when none
// survives or the picked zone's route is blocked.
unsigned int FRandomMissionLoader::PickObjectiveZone(unsigned int PlayerId, FSystemInfo* System,
                                                     Vector const* Position, float Range)
{
    RegisterRmDestSystem(System->SystemId);
    std::vector<unsigned int> Zones;
    unsigned int Picked = 0;

    float const SearchRange = Range - 10.0f;
    CollectZonesInRange(PlayerId, System, Position, SearchRange, &Zones);
    CollectZonesAlongTradeLanes(PlayerId, System, Position, SearchRange, &Zones);
    CollectZonesThroughJumps(PlayerId, System, Position, SearchRange, &Zones);

    std::vector<unsigned int>::iterator It = Zones.begin();
    while (It != Zones.end())
    {
        unsigned int ZoneSystemId = pub::Zone::GetSystem(*It);
        FSystemZone* Zone = FindSystemInfo(&ZoneSystemId)->FindZone(*It);
        bool bKeep = false;
        if (std::find(GRmObjectiveZones.begin(), GRmObjectiveZones.end(), *It) ==
            GRmObjectiveZones.end())
        {
            // TU48 FUN_06f50650 (no null test), looked up twice.
            unsigned int OfferGroupId = static_cast<unsigned int>(Mission->OfferGroup);
            if (GetFactionProps(&OfferGroupId)->Legality == FactionLegalityLawful &&
                Zone->bLawful)
                bKeep = true;
            else if (GetFactionProps(&OfferGroupId)->Legality == FactionLegalityUnlawful &&
                     Zone->bUnlawful)
                bKeep = true;
        }
        if (bKeep)
            ++It;
        else
            It = Zones.erase(It);
    }

    if (!Zones.empty())
    {
        Picked = PickRandomId(Zones);
        if (IsRouteBlocked(System->SystemId, pub::Zone::GetSystem(Picked), PlayerId))
            Picked = 0;
    }
    return Picked;
}

// FUN_06ef1810. Hostile groups of the zones within 7500 (DAT_06fb761c), else of every zone
// of the system; -1 when there is none.
unsigned int FRandomMissionLoader::PickEnemyGroup(FSystemInfo* System, Vector const* Position,
                                                  int OfferGroup)
{
    RegisterRmDestSystem(System->SystemId);
    std::vector<unsigned int> Groups;

    unsigned int ZoneIds[0x100];
    unsigned int Count = 0;
    pub::System::InZones(System->SystemId, *Position, 7500.0f, ZoneIds, 0x100, Count);
    for (unsigned int Index = 0; Index < Count; ++Index)
        CollectHostileGroups(ZoneIds[Index], OfferGroup, &Groups);

    if (Groups.empty())
    {
        FRmZoneEnumerator Enumerator;
        unsigned int SystemId = System->SystemId;
        pub::System::EnumerateZones(SystemId, Enumerator);
        for (std::vector<unsigned int>::iterator It = Enumerator.Zones.begin();
             It != Enumerator.Zones.end(); ++It)
        {
            CollectHostileGroups(*It, OfferGroup, &Groups);
        }
    }

    if (Groups.empty())
        return 0xffffffffu;
    return PickRandomId(Groups);
}

// LAB_06ef1c70. The new mission is not null-tested. The search starts at the base's dock
// object (FUN_06eac0c0 -> FUN_06eac4d0, no null test) in the player's system with range 90,
// in multiplayer 90 + 30 per other player there, at most 300 (<= and unordered keep it). No
// zone -> 3, no enemy group -> 1, else FinalizeMission's 0 / 2. *OutMission = the mission in
// every case (no AddRef).
int FRandomMissionLoader::CreateMission(FRmMissionRequest const* Request,
                                        FRandomMissionData** OutMission)
{
    int const NoObjective = 3;
    if (Mission != nullptr)
    {
        Mission->Release();
        Mission = nullptr;
    }
    FRandomMissionData::Create(&Mission);
    Mission->RandomSeed = static_cast<unsigned int>(std::rand());
    std::srand(Mission->RandomSeed);
    Mission->PlayerId = Request->PlayerId;
    Mission->OfferBase = Request->OfferBase;
    Mission->OfferGroup = Request->OfferGroup;
    Mission->Difficulty = Request->Difficulty;
    Mission->Reward = static_cast<int>(RmDifficultyToMoney(Mission->Difficulty));
    Mission->RepReward = 0.15f;
    Mission->RepHit = -0.15f;

    // Retail's local is uninitialised when the base has no dock object.
    Vector DockPosition = {};
    FindContentBaseInfo(&Mission->OfferBase)->FindDockObject(&DockPosition);

    FSystemInfo* System = FindContentPlayerState(Mission->PlayerId)->GetSystemRecord();
    RegisterRmDestSystem(System->SystemId);

    float Range = 90.0f;                                            // 0x42b40000
    if (!pub::SinglePlayer())
    {
        // FUN_06f78ee0 = &SystemInfo +0xb8 (the players); a null begin counts 0.
        int const OtherPlayers = static_cast<int>(
            FindContentPlayerState(Mission->PlayerId)->GetSystemRecord()->Players.size()) - 1;
        double Scaled = static_cast<double>(OtherPlayers) * 30.0 + 90.0; // DAT_06fb7828/7824
        if (Scaled > 300.0)                                             // DAT_06fb7820
            Scaled = 300.0;
        Range = static_cast<float>(Scaled);
    }

    unsigned int const Zone = PickObjectiveZone(Mission->PlayerId, System, &DockPosition, Range);
    if (Zone == 0)
    {
        *OutMission = Mission;
        return NoObjective;
    }

    RmInsertUnique(&GRmObjectiveZones, Zone);
    Mission->DestSys = pub::Zone::GetSystem(Zone);
    FSystemInfo* DestSystem = FindSystemInfo(&Mission->DestSys);
    Mission->ObjectiveZone = Zone;
    Mission->ObjectivePos = pub::Zone::GetPos(Zone);
    Mission->EnemyGroup = PickEnemyGroup(DestSystem, &Mission->ObjectivePos, Mission->OfferGroup);
    if (Mission->EnemyGroup == 0xffffffffu)
    {
        unsigned int BaseSystemId = 0;
        pub::GetSystem(BaseSystemId, Mission->OfferBase);
        FSystemInfo* BaseSystem = FindSystemInfo(&BaseSystemId);
        if (BaseSystem == nullptr)
        {
            Mission->EnemyGroup = 0xffffffffu;
        }
        else
        {
            Vector Origin;
            Origin.x = 0.0f;
            Origin.y = 0.0f;
            Origin.z = 0.0f;
            Mission->EnemyGroup = PickEnemyGroup(BaseSystem, &Origin, Mission->OfferGroup);
        }
    }

    if (Mission->EnemyGroup != 0xffffffffu)
    {
        int const Result = FinalizeMission(0);
        *OutMission = Mission;
        return Result;
    }
    *OutMission = Mission;
    return 1;
}
