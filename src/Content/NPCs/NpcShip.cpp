// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU43: the NPC ship (see NpcShip.h), and TU53's NPC ship event list DAT_06fd1bd8.

#include "NPCs/NpcShip.h"
#include "Bases/BaseDB.h"               // FContentBase::BaseId
#include "Core/ContentInternal.h"      // GSpaceSupportControllerId, GContentNextUniqueId, GetContentGameTime
#include "Core/ContentPlayerState.h"   // FContentPlayerStateBase
#include "NPCs/DockQueues.h"           // RelocateShip (TU53 FUN_06f67740)
#include "NPCs/FactionProps.h"         // GetFactionProps (TU48 FUN_06f50650)
#include "Comms/Gcs.h"                  // PickVoiceLine (TU25 FUN_06eeb010)
#include "Guides/Guides.h"               // FContentGuide, FGuideDesc, CreateGuide
#include "Missions/MissionObjList.h"       // CreateObjListDirective, the TU40 directive classes
#include "NPCs/NpcShips.h"             // FNpcShipArch, FindNpcShipArch, GNpcShipArchs
#include "Missions/ObjListDirective.h"
#include "NPCs/PilotPersonalities.h"   // GetPilotPersonalityWithJob
#include "Encounters/Populators.h"           // GSpacePopControllerId
#include "RandomMissions/RandomMissionDB.h"      // GetShipMissionProperties
#include "Universe/SystemInfo.h"
#include "Core/ContentStaticIds.h"     // TU43's 16 static-init thunks 06f33300-06f334e0
#include "common_newchar.h"       // pub::StateGraph::get_state_graph
#include "common_server.h"
#include "common_server_controller.h"

#include <cstring>

namespace
{
    // FUN_06f34100's read: the dword at a fixed byte offset of the directive, the offset
    // chosen by its GetType() code alone (no cast in retail). MissionObjList.h pins every
    // class's member at these offsets with static_asserts.
    unsigned int ReadDirectiveDword(FObjListDirective const* Directive, size_t Offset)
    {
        unsigned int Value;
        memcpy(&Value, reinterpret_cast<unsigned char const*>(Directive) + Offset,
               sizeof(Value));
        return Value;
    }

    // Message 0x100e (ship_in_sights_event) to SpaceSupport.
    struct FNpcShipSightMsg
    {
        unsigned int ShipId;     // +0x00
        unsigned int ObjectId;   // +0x04
        unsigned int SystemId;   // +0x08
    };

    // Message 0x1002 (combat_drift_event) to SpaceSupport; 0x100a (left the system) to the
    // owning controller.
    struct FNpcShipSystemMsg
    {
        unsigned int ShipId;     // +0x00
        unsigned int SystemId;   // +0x04
    };

    // Message 0x1011 (tether_object_event) to the owning controller.
    struct FNpcShipTetherMsg
    {
        unsigned int ObjectId1;  // +0x00
        unsigned int ObjectId2;  // +0x04
        unsigned int Field_0x08; // +0x08 always 0; Cnd_TetherBroke (LAB_06ec1330) does not read it
        // +0x0c: always 0 here; Cnd_TetherBroke (LAB_06ec1330) keeps its tether while it is 0.
        unsigned char bTetherReleased;
    };

    // Message 0x1008 (lead_object_event) to the owning controller.
    struct FNpcShipLeadMsg
    {
        unsigned int ObjectId1;  // +0x00
        unsigned int ObjectId2;  // +0x04
        unsigned int Progress;   // +0x08
        float Value;             // +0x0c
        Vector Position;         // +0x10
    };

    // Message 0x1006 (comm lines used up) to the owning controller.
    struct FNpcShipCommDoneMsg
    {
        unsigned int ShipId;     // +0x00
        // +0x04: a field of the first queued directive, or 0. 0 fails the mission (FUN_06f0f150)
        // and lets FUN_06eb8f50 take +0x08.
        unsigned int Field_0x04;
        // +0x08: always 1; FUN_06eb8f50 maps 1 to the state message 0x5462 and uses any other
        // value as the message itself.
        unsigned int Field_0x08;
    };

    // Message 0x1004 (request_flee_destination) to SpaceSupport.
    struct FNpcShipFleeMsg
    {
        unsigned int ShipId;     // +0x00
        unsigned int Reason;     // +0x04
        unsigned int SystemId;   // +0x08
    };
}

// ---------------------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------------------

// FUN_06f33500. The member initializers in NpcShip.h carry the retail ctor values.
FContentNpcShipParams::FContentNpcShipParams()
{
    Pilot.clear();
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
    CommCostume.PartIds[0] = 0;
    CommCostume.PartIds[1] = 0;
    CommCostume.PartIds[2] = 0;
    CommCostume.PartIds[3] = 0;
    CommCostume.AccessoryCount = 0;
    Orientation = Matrix(1.0f, 0.0f, 0.0f,
                         0.0f, 1.0f, 0.0f,
                         0.0f, 0.0f, 1.0f);
}

// Inlined in TU58 FUN_06f732e0; members copied in declaration order.
FContentNpcShipParams::FContentNpcShipParams(FContentNpcShipParams const& Other)
    : FContentShipParams(Other)
    , bExcludeFromShipCount(Other.bExcludeFromShipCount)
    , Loadout(Other.Loadout)
    , Cargo(Other.Cargo)
    , Orientation(Other.Orientation)
    , Position(Other.Position)
    , StateGraph(Other.StateGraph)
    , CommCostume(Other.CommCostume)
    , Voice(Other.Voice)
    , GuideDesc(Other.GuideDesc)
    , Pilot(Other.Pilot)
    , Longevity(Other.Longevity)
    , InitialLifeTime(Other.InitialLifeTime)
    , Field_0x10c(Other.Field_0x10c)
    , DockIndex(Other.DockIndex)
    , GuideTargetId(Other.GuideTargetId)
    , LabelIds(Other.LabelIds)
    , Personality(Other.Personality)
    , CreationType(Other.CreationType)
    , Level(Other.Level)
{
}

// FUN_06ef4bb0. The retail steps are the member destructors in reverse declaration order:
// LabelIds (+0x118), Pilot, GuideDesc (FUN_06f28e10), Cargo (+0x80), then the base.
FContentNpcShipParams::~FContentNpcShipParams() = default;

// FUN_06f348f0.
void __cdecl ApplyNpcShipArch(FContentNpcShipParams* Params, FNpcShipArch const* Arch,
                              ID_String* Pilot, ID_String const* Job)
{
    if (Arch == nullptr)
        return;

    Params->Loadout = Arch->Loadout;
    Params->ShipArchId = Arch->ShipArchetype;
    GetShipMissionProperties(Arch->ShipArchetype, &Params->ClassIds);

    // An empty nickname takes the arch's (TString assign: null -> empty, else clamped to 0x2f).
    if (Params->Nickname.Length == 0)
        Params->Nickname = Arch->Nickname.get_string();

    Params->StateGraph = Arch->StateGraph;

    // Each npc_class not yet in ClassIds is appended.
    for (std::vector<unsigned int>::const_iterator Class = Arch->NpcClasses.begin();
         Class != Arch->NpcClasses.end(); ++Class)
    {
        std::vector<unsigned int>::iterator Found = Params->ClassIds.begin();
        while (Found != Params->ClassIds.end() && *Found != *Class)
            ++Found;
        if (Found == Params->ClassIds.end())
            Params->ClassIds.insert(Params->ClassIds.end(), 1, *Class);
    }

    ID_String JobId;
    JobId.clear();
    if (Job != nullptr)
        JobId = *Job;

    if (!Arch->Pilot.IsEmpty())
    {
        Params->Pilot = Arch->Pilot;
        GetPilotPersonalityWithJob(&Params->Pilot.Id, &JobId, &Params->Personality);
    }

    Params->Level = Arch->Level;

    if (Pilot != nullptr && !Pilot->IsEmpty())
    {
        Params->Pilot = *Pilot;
        GetPilotPersonalityWithJob(&Pilot->Id, &JobId, &Params->Personality);
    }
}

// FUN_06f34a40: the arch lookup result is passed on even when null.
bool __cdecl ApplyNpcShipArchById(FContentNpcShipParams* Params, unsigned int const* ArchId,
                                  ID_String* Pilot, ID_String const* Job)
{
    FNpcShipArch* Arch = FindNpcShipArch(ArchId, &GNpcShipArchs);
    ApplyNpcShipArch(Params, Arch, Pilot, Job);
    return Arch != nullptr;
}

// FUN_06f34a80. Both pick results are ignored.
bool __cdecl ApplyNpcShipFaction(unsigned int FactionId, int Gender, FContentNpcShipParams* Params)
{
    FFactionProps* FactionProps = GetFactionProps(&FactionId);
    if (FactionProps == nullptr)
        return false;
    FactionProps->PickSpaceCostume(Gender, &Params->CommCostume);
    FactionProps->PickVoice(static_cast<unsigned int>((Gender != 1) ? 1 : 0) + 1, &Params->Voice);
    return true;
}

// ---------------------------------------------------------------------------------------
// The ship
// ---------------------------------------------------------------------------------------

// FUN_06f336a0.
FContentNpcShip::FContentNpcShip(FContentNpcShipParams const* Params)
    : FContentShipBase(Params)
    , CreationKind(Params->CreationType)
    , Longevity(Params->Longevity)
    , Field_0x1c0(Params->Field_0x10c)
    , Loadout(Params->Loadout)
    , LifeTime(Params->InitialLifeTime)
    , bExcludeFromShipCount(Params->bExcludeFromShipCount)
{
    PilotId.clear();
    EncounterParamsId.clear();

    // The TU20 base copied the handle raw; the NPC ship takes a reference (released by the
    // dtor). No null check.
    ReputationHandle->AddRef();

    pub::SpaceObj::ShipInfo Info;
    Info.ControllerId = 0;
    Info.SystemID = 0;
    Info.ShipArchID = 0;
    Info.Position.x = 0.0f;
    Info.Position.y = 0.0f;
    Info.Position.z = 0.0f;
    Info.AngularVelocity.x = 0.0f;
    Info.AngularVelocity.y = 0.0f;
    Info.AngularVelocity.z = 0.0f;
    Info.Velocity.x = 0.0f;
    Info.Velocity.y = 0.0f;
    Info.Velocity.z = 0.0f;
    Info.GroupNameId = 0;
    Info.LoadoutID = 0;
    Info.CostumeHead = 0;
    Info.CostumeBody = 0;
    Info.CostumeLeftHand = 0;
    Info.CostumeRightHand = 0;
    for (int Index = 0; Index < 8; ++Index)
        Info.CostumeAccessory[Index] = 0;   // retail leaves these unset (FUN_06eac5b0)
    Info.CostumeAccessoryCount = 0;
    Info.VibeGroupId = -1;
    Info.VoiceId = 0;
    Info.DockingBaseId = 0;
    Info.HitPts = -1;
    Info.Field_0xb4 = 0;
    Info.MarkPlayerId = 0;
    Info.ReputationRank = 0;
    Info.Orientation = Matrix(1.0f, 0.0f, 0.0f,
                              0.0f, 1.0f, 0.0f,
                              0.0f, 0.0f, 1.0f);

    // No null check on the system.
    Info.ShipArchID = ShipArchId;
    Info.LoadoutID = Params->Loadout;
    Info.HitPts = Params->HitPoints;
    Info.SystemID = Params->System->SystemId;
    Info.Position = Params->Position;
    Info.Orientation = Params->Orientation;
    Info.ControllerId = ControllerId;

    if (Params->Base != nullptr)
        Info.DockingBaseId = Params->Base->BaseId;
    else
        Info.DockingBaseId = 0;

    // A new CargoDesc per cargo entry (MetaList node reuse through FreeList inlined).
    for (std::vector<pub::SpaceObj::CargoDesc>::const_iterator Cargo = Params->Cargo.begin();
         Cargo != Params->Cargo.end(); ++Cargo)
    {
        pub::SpaceObj::CargoDesc* Copy = new pub::SpaceObj::CargoDesc(*Cargo);
        Info.Cargo.append(Copy);
    }

    Info.CostumeHead = Params->CommCostume.PartIds[0];
    Info.CostumeBody = Params->CommCostume.PartIds[1];
    Info.CostumeLeftHand = Params->CommCostume.PartIds[2];
    Info.CostumeRightHand = Params->CommCostume.PartIds[3];
    Info.CostumeAccessoryCount = static_cast<int>(Params->CommCostume.AccessoryCount);
    for (int Index = 0; Index < Info.CostumeAccessoryCount; ++Index)
        Info.CostumeAccessory[Index] = Params->CommCostume.AccessoryIds[Index];

    // FUN_06f33f80, no null check on the handle.
    Info.VibeGroupId = Params->ReputationHandle->GetReputation();
    Info.VoiceId = Params->Voice;
    Info.ReputationRank = static_cast<unsigned int>(Params->Level);

    LabelIds = Params->LabelIds;

    CreationKind = Params->CreationType;
    pub::SpaceObj::Create(SpaceObjId, Info);

    pub::AI::SetPersonalityParams Personalize;
    Personalize.bSetPersonality = 1;
    Personalize.Value = Params->Personality;

    // The faction's scan_for_cargo archetypes (at most 16) go into the personality, which
    // the copy from the parameters right after overwrites again. No null check on the
    // faction properties.
    unsigned int Affiliation = ReputationHandle->GetAffiliation();
    FFactionProps* FactionProps = GetFactionProps(&Affiliation);
    Personality.JobParams.ScanForCargoCount = 0;
    for (std::vector<FFactionScanCargo>::const_iterator It = FactionProps->ScanForCargo.begin();
         It != FactionProps->ScanForCargo.end(); ++It)
    {
        if (Personality.JobParams.ScanForCargoCount >= 16)
            break;
        Personality.JobParams.ScanForCargo[Personality.JobParams.ScanForCargoCount] = It->Archetype;
        ++Personality.JobParams.ScanForCargoCount;
    }
    Personality = Params->Personality;

    int StateGraph = Params->StateGraph;
    if (StateGraph == -1)
        StateGraph = pub::StateGraph::get_state_graph("NOTHING", pub::StateGraph::LEADER);
    Personalize.StateGraphId = StateGraph;
    Personalize.ContentCallbackObject = static_cast<pub::AI::ContentCallback*>(this);
    Personalize.DirectiveCallbackObject = static_cast<pub::AI::DirectiveCallback*>(this);
    pub::AI::SubmitState(SpaceObjId, &Personalize);

    int ZeroPriority = 0;
    SetPriority(&ZeroPriority);
    SetDirectives(&Params->Directives.Items, 0);

    FGuideDescBase* Desc = Params->GuideDesc;
    if (Desc != nullptr)
    {
        if (Desc->Type == 0)
        {
            if (Guide != nullptr)
            {
                Guide->Release();
                Guide = nullptr;
            }
        }
        else
        {
            Desc->Ship = this;
            bool bStart = true;
            if (Params->GuideTargetId != 0)
            {
                // The out-parameter is the stack slot that held the zero priority.
                unsigned int TargetType = 0;
                pub::SpaceObj::GetType(Params->GuideTargetId, TargetType);
                if ((TargetType & (OBJ_JUMP_GATE | OBJ_AIRLOCK_GATE | OBJ_JUMP_HOLE)) == 0)
                    bStart = false;
            }
            if (Guide != nullptr)
            {
                pub::AI::DirectiveCancelOp Cancel;
                SubmitDirective(&Cancel);
                if (Guide != nullptr)
                {
                    Guide->Release();
                    Guide = nullptr;
                }
            }
            // CreateGuide is FUN_06f05f00; no null check before Start().
            if (Desc->Type != 0)
            {
                CreateGuide(Desc, &Guide.Pointer);
                if (bStart && Directives.Items.empty())
                    Guide->Start();
            }
        }
    }

    FleePosition.x = 0.0f;
    FleePosition.y = 0.0f;
    FleePosition.z = 0.0f;
    FleeState = 0;
    FleeObjectId = 0;
    FleeDistance = 0.0f;
}

// FUN_06f34680; deleting dtor 06f33d00. The reputation handle is released first (no null
// check); then the member destructors of LabelIds (+0x17c) and Guide (+0x178), as the unwind
// map FuncInfo 06fc3b8c orders them; the TU50 part (FUN_06f33d20, inlined) is the base dtor.
FContentNpcShip::~FContentNpcShip()
{
    ReputationHandle->Release();
}

// 06ea1460.
bool FContentNpcShip::IsNpcShip()
{
    return true;
}

// 06f33e70.
pub::AI::Personality* FContentNpcShip::GetPersonality()
{
    return &Personality;
}

// LAB_06f341f0. No null check on the faction properties.
int FContentNpcShip::GetScanForCargo(unsigned int* Out, int MaxCount)
{
    unsigned int Affiliation = ReputationHandle->GetAffiliation();
    FFactionProps* FactionProps = GetFactionProps(&Affiliation);
    int Count = 0;
    for (std::vector<FFactionScanCargo>::const_iterator It = FactionProps->ScanForCargo.begin();
         It != FactionProps->ScanForCargo.end(); ++It)
    {
        if (Count >= MaxCount)
            break;
        Out[Count] = It->Archetype;
        ++Count;
    }
    return Count;
}

// FUN_06f33f90; the base handling is FUN_06f57820.
void FContentNpcShip::OnDirectiveComplete(int CompletedBehavior, int DirectiveResult)
{
    FContentShipBase::OnDirectiveComplete(CompletedBehavior, DirectiveResult);
    if (HasNoDirectives() && Guide != nullptr)
        Guide->OnDirectivesDone(CompletedBehavior, DirectiveResult);
}

// LAB_06f34250. Unlike lead_object_event, the early returns skip the controller message too.
void FContentNpcShip::tether_object_event(uint ObjectId1, uint ObjectId2, TetherStatusType Status,
                                          Vector const& Position)
{
    if (bCommMuted)
        return;
    if (GNpcShipEvents.LastTetherStatus == static_cast<unsigned int>(Status))
        return;
    FSystemInfo* Sys = GetSystem();
    if (Sys == nullptr)
        return;

    unsigned int LineId = 0;
    unsigned int ShipId = SpaceObjId;
    unsigned int Voice = 0;
    pub::SpaceObj::GetVoiceID(ShipId, Voice);

    // On a failed pick LineId stays 0.
    TString<0x40> Line;
    Line.Length = 0;
    Line.Data[0] = '\0';
    switch (static_cast<int>(Status))
    {
    case 1:     // "bc_enter_near_teth" (PTR 06fce5a0)
        if (PickVoiceLine("bc_enter_near_teth", "", Voice, &Line) == 0)
            LineId = pub::GetNicknameId(Line.Data);
        GNpcShipEvents.StartTimer(this, 1.0f);
        break;
    case 2:     // "bc_enter_far_teth" (PTR 06fce5a4)
        if (PickVoiceLine("bc_enter_far_teth", "", Voice, &Line) == 0)
            LineId = pub::GetNicknameId(Line.Data);
        GNpcShipEvents.StartTimer(this, 1.0f);
        break;
    case 3:     // "bc_exit_far_teth" (PTR 06fce5a8)
        if (PickVoiceLine("bc_exit_far_teth", "", Voice, &Line) == 0)
            LineId = pub::GetNicknameId(Line.Data);
        GNpcShipEvents.SetCommLine(this, LineId);
        GNpcShipEvents.QueueTetherEvent(this, ObjectId2, 3, &Position);
        break;
    default:
        break;
    }

    if ((LineId & GIdValueMask) != 0)
        Sys->SendComm(SpaceObjId, ObjectId2, LineId, 0, 0, nullptr, 0.5f);

    FNpcShipTetherMsg Msg;
    Msg.ObjectId1 = ObjectId1;
    Msg.ObjectId2 = ObjectId2;
    Msg.Field_0x08 = 0;
    Msg.bTetherReleased = 0;
    unsigned int Controller = ControllerId;
    pub::Controller::_SendMessage(Controller, pub::Controller::EMessage::TetherObject, &Msg);
}

// LAB_06f34400 (jump table 06f34644). The controller message goes out on every path.
void FContentNpcShip::lead_object_event(uint ObjectId1, uint ObjectId2, LeadProgressType Progress,
                                        float Value, Vector const& Position)
{
    unsigned int const ProgressValue = static_cast<unsigned int>(Progress);
    if (!bCommMuted && GNpcShipEvents.LastLeadProgress != ProgressValue)
    {
        FSystemInfo* Sys = GetSystem();
        if (Sys != nullptr)
        {
            unsigned int LineId = 0;
            unsigned int ShipId = SpaceObjId;
            unsigned int Voice = 0;
            pub::SpaceObj::GetVoiceID(ShipId, Voice);

            TString<0x40> Line;
            Line.Length = 0;
            Line.Data[0] = '\0';
            bool bSendComm = true;
            switch (static_cast<int>(ProgressValue))
            {
            case 1:     // "bc_goto_pos"
                if (PickVoiceLine("bc_goto_pos", "", Voice, &Line) == 0)
                    LineId = pub::GetNicknameId(Line.Data);
                GNpcShipEvents.StartTimer(this, 1.0f);
                break;
            case 2:     // "bc_goto_flat"
                if (GNpcShipEvents.LastLeadProgress == 7)
                    break;
                if (PickVoiceLine("bc_goto_flat", "", Voice, &Line) == 0)
                    LineId = pub::GetNicknameId(Line.Data);
                GNpcShipEvents.StartTimer(this, 1.0f);
                break;
            case 3:     // "bc_goto_neg"
            case 6:     // "bc_follow_pos_app_off"
            case 8:     // "bc_follow_neg"
            {
                char const* const Name = ProgressValue == 3 ? "bc_goto_neg" :
                                         ProgressValue == 6 ? "bc_follow_pos_app_off" :
                                                              "bc_follow_neg";
                if (PickVoiceLine(Name, "", Voice, &Line) == 0)
                    LineId = pub::GetNicknameId(Line.Data);
                GNpcShipEvents.SetCommLine(this, LineId);
                GNpcShipEvents.QueueLeadEvent(this, ObjectId2, ProgressValue, Value, &Position);
                break;
            }
            case 7:     // "bc_follow_flat"
                if (GNpcShipEvents.LastLeadProgress == 2)
                    break;
                if (PickVoiceLine("bc_follow_flat", "", Voice, &Line) == 0)
                    LineId = pub::GetNicknameId(Line.Data);
                GNpcShipEvents.SetCommLine(this, LineId);
                GNpcShipEvents.QueueLeadEvent(this, ObjectId2, ProgressValue, Value, &Position);
                break;
            case 4:
            case 5:
                GNpcShipEvents.StartTimer(this, 1.0f);
                bSendComm = false;
                break;
            default:
                break;
            }

            if (bSendComm && (LineId & GIdValueMask) != 0)
                Sys->SendComm(SpaceObjId, ObjectId2, LineId, 0, 0, nullptr, 0.5f);
        }
    }

    FNpcShipLeadMsg Msg;
    Msg.ObjectId1 = ObjectId1;
    Msg.ObjectId2 = ObjectId2;
    Msg.Progress = ProgressValue;
    Msg.Value = Value;
    Msg.Position = Position;
    unsigned int Controller = ControllerId;
    pub::Controller::_SendMessage(Controller, pub::Controller::EMessage::LeadObject, &Msg);
}

// LAB_06f34000.
void FContentNpcShip::ship_in_sights_event(uint ObjectId)
{
    FNpcShipSightMsg Msg;
    Msg.ShipId = SpaceObjId;
    Msg.ObjectId = ObjectId;
    Msg.SystemId = GetSystem()->SystemId;
    pub::Controller::_SendMessage(GSpaceSupportControllerId, pub::Controller::EMessage::ShipInSights,
                                  &Msg);
}

// LAB_06f34ce0. FleeState 0 asks the guide; when it answers, a goto directive to the
// destination becomes the active directive. ObjectId is not read.
bool FContentNpcShip::request_flee_destination(uint ObjectId, FleeReason Reason,
                                               FleeDestinationType& OutType,
                                               Vector& OutPosition, uint& OutParam,
                                               float& OutFloat)
{
    (void)ObjectId;
    bool bResult = false;
    switch (FleeState)
    {
    case 0:
        if (Guide != nullptr)
        {
            bResult = Guide->RequestFleeDestination(OutType, OutPosition, OutParam, OutFloat);
            if (bResult)
            {
                // Other kinds leave the destination unset in retail.
                Vector Destination;
                Destination.x = 0.0f;
                Destination.y = 0.0f;
                Destination.z = 0.0f;
                int Kind = static_cast<int>(OutType);
                if (Kind > 0)
                {
                    if (Kind <= 2)
                    {
                        Matrix Orientation;
                        pub::SpaceObj::GetLocation(OutParam, Destination, Orientation);
                    }
                    else if (Kind == 3)
                    {
                        Destination = OutPosition;
                    }
                }
                // Ref-holding local (EH state 0 of FuncInfo LAB_06faebf8); FUN_06f24870(5)
                // makes a GotoVec, set with FUN_06f26460 (no null check; -1.0f DAT_06fb9570).
                TRefHandle<FObjListDirective> Directive;
                CreateObjListDirective(OBJLIST_GOTO_VEC, &Directive.Pointer);
                static_cast<FObjListGotoVec*>(Directive.Pointer)
                    ->SetDestination(&Destination, OutFloat, -1.0f, 0, false, false);
                if (ActiveDirective != Directive.Pointer)
                {
                    if (ActiveDirective != nullptr)
                    {
                        ActiveDirective->Release();
                        ActiveDirective = nullptr;
                    }
                    ActiveDirective = Directive.Pointer;
                    if (ActiveDirective != nullptr)
                        ActiveDirective->AddRef();
                }
            }
        }
        break;
    case 1:
        OutType = static_cast<FleeDestinationType>(3);
        OutPosition = FleePosition;
        OutFloat = FleeDistance;
        bResult = true;
        break;
    case 2:
        OutType = static_cast<FleeDestinationType>(1);
        OutParam = FleeObjectId;
        OutFloat = FleeDistance;
        bResult = true;
        break;
    case 3:
        OutType = static_cast<FleeDestinationType>(2);
        OutParam = FleeObjectId;
        OutFloat = FleeDistance;
        bResult = true;
        break;
    default:
        break;
    }

    FNpcShipFleeMsg Msg;
    Msg.ShipId = SpaceObjId;
    Msg.Reason = static_cast<unsigned int>(Reason);
    Msg.SystemId = GetSystem()->SystemId;
    pub::Controller::_SendMessage(GSpaceSupportControllerId, pub::Controller::EMessage::FleeRequest,
                                  &Msg);
    return bResult;
}

// LAB_06f34ad0.
void FContentNpcShip::scan_state_change(bool bScanning)
{
    if (Guide != nullptr)
        Guide->OnScanStateChange(bScanning);
}

// LAB_06f34090. The message goes to controller 0.
void FContentNpcShip::combat_state_notification(CSNType Type, uint Param1, uint Param2)
{
    FSpaceObjCombatStateMsg Msg;
    Msg.Type = static_cast<unsigned int>(Type);
    Msg.Param1 = Param1;
    Msg.Param2 = Param2;
    Msg.SystemId = GetSystem()->SystemId;
    bInCombat = (Type == 0) ? 1 : 0;
    unsigned int Controller = 0;
    pub::Controller::_SendMessage(Controller, pub::Controller::EMessage::CombatState, &Msg);
}

// LAB_06f34050.
void FContentNpcShip::combat_drift_event()
{
    FNpcShipSystemMsg Msg;
    Msg.ShipId = SpaceObjId;
    Msg.SystemId = GetSystem()->SystemId;
    pub::Controller::_SendMessage(GSpaceSupportControllerId, pub::Controller::EMessage::CombatDrift,
                                  &Msg);
}

// LAB_06f34cb0.
pub::AI::ContentCallback::CreationType FContentNpcShip::get_creation_type()
{
    if (CreationKind == 0)
        return CreationType2;
    if (CreationKind == 2)
        return CreationType0;
    return CreationKind == 1 ? CreationType1 : CreationType3;
}

// FUN_06f33e80.
bool FContentNpcShip::IsSpacePopShip() const
{
    return ControllerId == GSpacePopControllerId;
}

// FUN_06f33ea0.
void FContentNpcShip::SetFleePosition(Vector const* Position, float const* Distance)
{
    FleeState = 1;
    FleePosition = *Position;
    FleeDistance = *Distance;
}

// FUN_06f33ee0.
void FContentNpcShip::SetFleeObject(unsigned int const* ObjectId, float const* Distance, bool bType3)
{
    FleeObjectId = *ObjectId;
    FleeDistance = *Distance;
    FleeState = bType3 ? 3 : 2;
}

// FUN_06f33f20; the game time is FUN_06efec10.
void FContentNpcShip::SetLastHailPlayer(FContentPlayerState* Player)
{
    LastHailTime = GetContentGameTime();
    LastHailPlayer = Player;
}

// FUN_06f33f40.
void FContentNpcShip::ClearFlee()
{
    FleePosition.x = 0.0f;
    FleePosition.y = 0.0f;
    FleePosition.z = 0.0f;
    FleeState = 0;
    FleeObjectId = 0;
    FleeDistance = 0.0f;
}

// FUN_06f33fd0.
void FContentNpcShip::SetCommMuted(bool Value)
{
    bCommMuted = Value;
    if (Value)
        GNpcShipEvents.ClearTimer(this);
}

// FUN_06f33ff0.
bool FContentNpcShip::IsCommMuted() const
{
    return bCommMuted;
}

// FUN_06f34100. Retail reads the first directive's dword at a raw offset chosen by type
// code, with no cast: GetType 5 is reported by GotoVec and also by the Guide / GuideShip
// directives (shared vtable entry 06ee99a0), whose +0x17c is GotoParams.TetherFarRange (float
// bits); no directive reports 8 or 9. The offsets are pinned in MissionObjList.h.
void FContentNpcShip::OnCommLineDone()
{
    --CommLinesLeft;
    if (CommLinesLeft > 0)
        return;

    FNpcShipCommDoneMsg Msg;
    Msg.ShipId = SpaceObjId;
    Msg.Field_0x04 = 0;
    Msg.Field_0x08 = 1;
    if (!Directives.Items.empty())
    {
        FObjListDirective* First = Directives.Items.front();
        int DirectiveType = First->GetType();
        if (DirectiveType == OBJLIST_GOTO_SHIP || DirectiveType == OBJLIST_GOTO_SPLINE ||
            DirectiveType == OBJLIST_GOTO_VEC)
            Msg.Field_0x04 = ReadDirectiveDword(First, 0x17c);
        else if (DirectiveType == OBJLIST_FOLLOW)
            Msg.Field_0x04 = ReadDirectiveDword(First, 0x10c);
        else if (DirectiveType == OBJLIST_GUIDE || DirectiveType == OBJLIST_GUIDE_SHIP)
            Msg.Field_0x04 = ReadDirectiveDword(First, 0x1c8);
    }
    unsigned int Controller = ControllerId;
    pub::Controller::_SendMessage(Controller, pub::Controller::EMessage::Msg0x1006, &Msg);
}

// FUN_06f34800.
void FContentNpcShip::Relocate(FSystemInfo* NewSystem, Vector const* Position,
                               Matrix const* Orientation)
{
    if (System != nullptr)
        System->RemoveSpaceObj(SpaceObjId, false);

    // Leaving every system reports the old system to the owning controller.
    unsigned int NewSystemId = 0;
    if (NewSystem != nullptr)
    {
        NewSystemId = NewSystem->SystemId;
    }
    else if (System != nullptr)
    {
        FNpcShipSystemMsg Msg;
        Msg.ShipId = SpaceObjId;
        Msg.SystemId = System->SystemId;
        unsigned int Controller = ControllerId;
        pub::Controller::_SendMessage(Controller, pub::Controller::EMessage::NpcSystemExit, &Msg);
    }

    // Retail leaves the locals unset when no Position / Orientation is given; the port starts
    // them at zero / identity. Never read: the only callers passing null (TU38 06f14a67 /
    // 06f14b26) also pass no system, and pub::SpaceObj::Relocate ignores them for system 0.
    // RelocateShip (TU53 FUN_06f67740) is pub::SpaceObj::Relocate plus the dock queues.
    Vector LocalPosition;
    LocalPosition.x = 0.0f;
    LocalPosition.y = 0.0f;
    LocalPosition.z = 0.0f;
    Matrix LocalOrientation(1.0f, 0.0f, 0.0f,
                            0.0f, 1.0f, 0.0f,
                            0.0f, 0.0f, 1.0f);
    if (Position != nullptr)
        LocalPosition = *Position;
    if (Orientation != nullptr)
        LocalOrientation = *Orientation;
    unsigned int ShipId = SpaceObjId;
    RelocateShip(&ShipId, &NewSystemId, &LocalPosition, &LocalOrientation);

    if (NewSystem != nullptr)
        NewSystem->AddSpaceObj(this);
    else
        System = nullptr;
}

// FUN_06f34af0.
void FContentNpcShip::SetGuideFromDesc(FGuideDescBase* Desc, bool bStart)
{
    if (Guide != nullptr)
    {
        pub::AI::DirectiveCancelOp Cancel;
        SubmitDirective(&Cancel);
        if (Guide != nullptr)
        {
            Guide->Release();
            Guide = nullptr;
        }
    }
    // No null check before Start().
    if (Desc->Type == 0)
        return;
    CreateGuide(Desc, &Guide.Pointer);
    if (bStart && Directives.Items.empty())
        Guide->Start();
}

// FUN_06f34b70.
void FContentNpcShip::SetGuide(FContentGuide* NewGuide, bool bStart, bool bReset)
{
    if (NewGuide != nullptr)
    {
        if (Guide != NewGuide)
        {
            if (Guide != nullptr)
            {
                Guide->Release();
                Guide = nullptr;
            }
            Guide = NewGuide;
            NewGuide->AddRef();
        }
        Guide->SetShip(this);
        if (bReset)
            Guide->Reset();
        if (bStart && Directives.Items.empty())
            Guide->Start();
        return;
    }

    if (Guide != nullptr)
    {
        pub::AI::DirectiveCancelOp Cancel;
        SubmitDirective(&Cancel);
    }
    if (Guide != nullptr)
    {
        Guide->Release();
        Guide = nullptr;
    }
}

// FUN_06f34c30.
void FContentNpcShip::ForwardToGuide(pub::Controller::EMessage MsgId, void const* Parms)
{
    if (Guide == nullptr)
        return;
    Guide->HandleEvent(MsgId, Parms);
    if (Guide->IsDone() && Guide != nullptr)
    {
        Guide->Release();
        Guide = nullptr;
    }
}

// FUN_06f34c80.
int FContentNpcShip::GetGuideType() const
{
    if (Guide == nullptr)
        return 0;
    return Guide->GetType();
}

// FUN_06f34ca0.
FContentGuide* FContentNpcShip::GetGuide() const
{
    return Guide;
}

// ---------------------------------------------------------------------------------------
// TU53 event list (DAT_06fd1bd8)
// ---------------------------------------------------------------------------------------

// Ctor FUN_06f5ca60 (from _initterm LAB_06f5ca20), dtor FUN_06f15350 (atexit FUN_06f5ca40,
// guarded by bit 0 of DAT_06fd1bd4).
FNpcShipEventList GNpcShipEvents;

namespace
{
    // First record whose ship has the same space object id (inlined in each list method).
    std::list<FNpcShipEvent>::iterator FindShipEvent(std::list<FNpcShipEvent>& Events,
                                                     FContentNpcShip const* Ship)
    {
        std::list<FNpcShipEvent>::iterator It = Events.begin();
        while (It != Events.end() && It->Ship->SpaceObjId != Ship->SpaceObjId)
            ++It;
        return It;
    }
}

// FUN_06f683e0. Each message acts on the first matching record only. Parms[4] of the 0xb
// that the GCS manager sends itself (FUN_06ee8c50) is an unwritten stack word in retail;
// the port writes 0 there (FGcsConversationManager::Update, Gcs.cpp), so for those
// messages the Words[4] test always passes.
void FNpcShipEventList::HandleMessage(pub::Controller::EMessage MsgId, void const* Parms)
{
    unsigned int const* Words = static_cast<unsigned int const*>(Parms);
    if (MsgId == pub::Controller::EMessage::CommComplete)
    {
        for (std::list<FNpcShipEvent>::iterator It = Events.begin(); It != Events.end(); ++It)
        {
            if (It->Ship->SpaceObjId == Words[2] && It->CommLineId == Words[3] && Words[4] == 0)
            {
                It->Ship->OnCommLineDone();
                return;
            }
        }
        return;
    }
    if (MsgId == pub::Controller::EMessage::ObjectDestroyed)
    {
        for (std::list<FNpcShipEvent>::iterator It = Events.begin(); It != Events.end(); ++It)
        {
            if (It->Ship->SpaceObjId == Words[0])
            {
                Events.erase(It);
                return;
            }
        }
        return;
    }
    if (MsgId != pub::Controller::EMessage::TimerExpired)
        return;

    for (std::list<FNpcShipEvent>::iterator It = Events.begin(); It != Events.end(); ++It)
    {
        if (It->TimerId != Words[0])
            continue;
        FContentNpcShip* Ship = It->Ship;
        switch (It->Kind)
        {
        case 0:
            Ship->SetCommMuted(false);
            It->TimerId = 0;
            break;
        case 1:
            LastLeadProgress = 0;
            Ship->lead_object_event(Ship->SpaceObjId, It->ObjectId,
                                    static_cast<pub::AI::ContentCallback::LeadProgressType>(It->LeadProgress),
                                    It->Value, It->Position);
            It->TimerId = 0;
            break;
        case 2:
            LastFollowStatus = 0;
            Ship->follow_event(Ship->SpaceObjId, It->ObjectId,
                               static_cast<pub::AI::ContentCallback::FollowStatusType>(It->FollowStatus),
                               It->Value);
            It->TimerId = 0;
            break;
        case 3:
            LastTetherStatus = 0;
            Ship->tether_object_event(Ship->SpaceObjId, It->ObjectId,
                                      static_cast<pub::AI::ContentCallback::TetherStatusType>(It->TetherStatus),
                                      It->Position);
            It->TimerId = 0;
            break;
        default:
            break;
        }
        return;
    }
}

// FUN_06f68570. Retail leaves +0xc..+0x2c of a new record unset.
void FNpcShipEventList::SetCommLine(FContentNpcShip* Ship, unsigned int CommLineId)
{
    std::list<FNpcShipEvent>::iterator Found = FindShipEvent(Events, Ship);
    if (Found != Events.end())
    {
        Found->CommLineId = CommLineId;
        return;
    }
    FNpcShipEvent Event = FNpcShipEvent();
    Event.Ship = Ship;
    Event.CommLineId = CommLineId;
    Event.TimerId = 0;
    Events.push_back(Event);
}

// FUN_06f68600.
void FNpcShipEventList::ClearTimer(FContentNpcShip* Ship)
{
    std::list<FNpcShipEvent>::iterator Found = FindShipEvent(Events, Ship);
    if (Found != Events.end())
        Found->TimerId = 0;
}

// FUN_06f68630. The id counter is DAT_06fce76c.
void FNpcShipEventList::StartTimer(FContentNpcShip* Ship, float Delay)
{
    unsigned int Id = GContentNextUniqueId++;
    Ship->SetCommMuted(true);
    Ship->CommLinesLeft = 5;

    std::list<FNpcShipEvent>::iterator Found = FindShipEvent(Events, Ship);
    if (Found != Events.end())
    {
        Found->TimerId = Id;
        Found->Kind = 0;
    }
    else
    {
        FNpcShipEvent Event = FNpcShipEvent();
        Event.Ship = Ship;
        Event.CommLineId = 0;
        Event.TimerId = Id;
        Event.Kind = 0;
        Events.push_back(Event);
    }

    pub::Controller::TimerExpired Timer;
    Timer.Param1 = Id;
    Timer.Param2 = 0;
    LastLeadProgress = 0;
    LastFollowStatus = 0;
    LastTetherStatus = 0;
    pub::SetTimer(GSpaceSupportControllerId, Timer, Delay);
}

// FUN_06f68710.
void FNpcShipEventList::QueueLeadEvent(FContentNpcShip* Ship, unsigned int ObjectId,
                                       unsigned int Progress, float Value, Vector const* Position)
{
    unsigned int Id = GContentNextUniqueId++;
    std::list<FNpcShipEvent>::iterator Found = FindShipEvent(Events, Ship);
    if (Found != Events.end())
    {
        Found->ObjectId = ObjectId;
        Found->Kind = 1;
        Found->TimerId = Id;
        Found->LeadProgress = Progress;
        Found->Value = Value;
        Found->Position = *Position;
    }
    else
    {
        FNpcShipEvent Event = FNpcShipEvent();
        Event.Ship = Ship;
        Event.CommLineId = 0;
        Event.TimerId = Id;
        Event.ObjectId = ObjectId;
        Event.LeadProgress = Progress;
        Event.Value = Value;
        Event.Position = *Position;
        Event.Kind = 1;
        Events.push_back(Event);
    }

    pub::Controller::TimerExpired Timer;
    Timer.Param1 = Id;
    Timer.Param2 = 0;
    pub::SetTimer(GSpaceSupportControllerId, Timer, 30.0f);
    LastLeadProgress = Progress;
}

// FUN_06f68840.
void FNpcShipEventList::QueueTetherEvent(FContentNpcShip* Ship, unsigned int ObjectId,
                                         unsigned int Status, Vector const* Position)
{
    unsigned int Id = GContentNextUniqueId++;
    std::list<FNpcShipEvent>::iterator Found = FindShipEvent(Events, Ship);
    if (Found != Events.end())
    {
        Found->Kind = 3;
        Found->ObjectId = ObjectId;
        Found->TimerId = Id;
        Found->TetherStatus = Status;
        Found->Position = *Position;
    }
    else
    {
        FNpcShipEvent Event = FNpcShipEvent();
        Event.Ship = Ship;
        Event.CommLineId = 0;
        Event.TimerId = Id;
        Event.ObjectId = ObjectId;
        Event.TetherStatus = Status;
        Event.Position = *Position;
        Event.Kind = 3;
        Events.push_back(Event);
    }

    pub::Controller::TimerExpired Timer;
    Timer.Param1 = Id;
    Timer.Param2 = 0;
    pub::SetTimer(GSpaceSupportControllerId, Timer, 30.0f);
    LastTetherStatus = Status;
}
