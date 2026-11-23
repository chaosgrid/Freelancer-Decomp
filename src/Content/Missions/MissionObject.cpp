// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Missions/MissionObject.h"

// TU38 static IDs 06f0e000..06f0e1e0, then "default" 06f0e200.
#include "Core/ContentStaticIds.h"
#include "Core/ContentDefaultId.h"

#include "Core/ContentInternal.h"    // GContentNextUniqueId (DAT_06fce76c)
#include "Core/ContentPlayerState.h"
#include "Guides/Guides.h"             // FContentGuide (mission ship slot +0x68)
#include "Missions/MissionDialogs.h"     // TU28 GMissionDialogs / FMissionDialog ([Dialog])
#include "Missions/MissionObjList.h"     // LoadObjList (TU40 FUN_06f25260), FObjListDirective
#include "NPCs/PilotPersonalities.h" // GetPilotPersonality (FUN_06f3e020)
#include "Encounters/SpacePaths.h"         // BuildNnPath (TU53 FUN_06f5ea40)
#include "NPCs/DockQueues.h"         // PlaceFormationBlocks (TU53 FUN_06f66c80)
#include "Encounters/Encounters.h"         // FindFormation (TU31 FUN_06efd1a0)
#include "NPCs/FactionProps.h"       // GetFactionProps (TU48 FUN_06f50650)
#include "NPCs/NpcNames.h"           // TU42 NPC names (FUN_06f319b0 / 06f319d0 / 06f31910 / 06f31d40)
#include "RandomMissions/RandomMissionGen.h"   // FindVoiceProperties (FUN_06fa6e50)
#include "Encounters/RandomEncounters.h"   // TU31 FRandEncParams / FRandEncPopulator and their factories
#include "Universe/SystemConnections.h"  // TU41 FUN_06f2a240 / FUN_06f2a190 / FUN_06f29c10
#include "Universe/SystemInfo.h"         // FSystemInfo::SystemId (+0x48)
#include "common_id.h"
#include "common_ini.h"
#include "common_misc.h"        // SUBOBJ_ID_NONE
#include "common_newchar.h"     // pub::StateGraph::get_state_graph
#include "common_server.h"
#include "common_server_controller.h"
#include "msvcrt_compat.h"
#include "server_cobject.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <string.h>

// TU38 mission object base (see MissionObject.h).

namespace
{
    // LAB_06f0e240.
    TString<0x30> MakePlayerLabel()
    {
        TString<0x30> Label;
        Label.Data[0] = '\0';
        Label = "Player";
        return Label;
    }
}

// LAB_06f0e220: DAT_06fd06c4.
extern unsigned int const GIdLocalObjective = CreateID("LOCAL_OBJECTIVE");
// LAB_06f0e240: DAT_06fd0690.
extern TString<0x30> const GPlayerLabel = MakePlayerLabel();
// LAB_06f0e290: DAT_06fd068c.
extern unsigned int const GIdMissionRandomEncounter = CreateID("Mission_Random_Encounter");

// FUN_06f683b0: GetType writes into the first argument's stack slot, hence the seeded Type.
bool __cdecl IsSpaceObjTypeSet(unsigned int ObjId, FSystemInfo* Unused)
{
    (void)Unused;
    unsigned int Type = ObjId;
    if (pub::SpaceObj::GetType(ObjId, Type) != 0)
        return false;
    return (Type & 0xfdff) != 0;
}

// FUN_06f0e3e0. The heartbeat interval is 0x3d088889 (1/30 s).
FMissionObject::FMissionObject(unsigned int const* InControllerId, void const* Parms)
    : ControllerId(*InControllerId)
    , PlayerState(nullptr)
    , PlayerId(0)
    , Reward(0)
    , HeartbeatInterval(0.0333333351f)
    , bTriggerSavePending(0)
    , bPendingDestroy(0)
    , bJumpPending(0)
    , JumpTimer(-1.0f)
    , MissionType(7)
    , bDestroyed(0)
    , ObjectiveResendTimer(0.0f)
    , Field_0x88(0)
    , JumpDelay(3)
{
    (void)Parms;
    Field_0x38.clear();
    pub::Controller::SetHeartbeatInterval(ControllerId, HeartbeatInterval);
}

// FUN_06f0e770 -> FUN_06f0ed00. The vtable is reset to 06fb85d8 first, so the slot calls below
// are this class's bodies; the members' destructors do the rest of the teardown.
FMissionObject::~FMissionObject()
{
    RandEncs.clear();
    while (!Players.empty() && Players.begin()->second != nullptr)
        FMissionObject::RemovePlayer(FMissionObject::FindPlayer(Players.begin()->second->PlayerId));
    // A jumper out of every system (state not 0) is owned by nobody else.
    std::list<FMissionJumper>::iterator It = Jumpers.begin();
    for (; It != Jumpers.end(); ++It)
    {
        if (It->Npc == nullptr || It->State == 0)
            continue;
        delete It->Npc;
        It->Npc = nullptr;
        It->State = 0;
    }
}

// FUN_06f0f100. Slot 21 is vtbl +0x54.
void FMissionObject::HandleMessage(pub::Controller::EMessage MessageId, void const* MessageParms)
{
    if (bDestroyed != 0)
        return;
    if (bPendingDestroy == 0)
    {
        HandleMissionMessage(MessageId, MessageParms);
        if (bPendingDestroy == 0)
            return;
    }
    pub::Controller::Destroy(ControllerId);
    bDestroyed = 1;
}

// LAB_06ea1450.
int FMissionObject::Slot07() { return 0; }
int FMissionObject::GetTargetSystemId() { return 0; }
int FMissionObject::GetJumpPreference() { return 0; }
int FMissionObject::Slot10() { return 0; }

namespace
{
    // The position triple of a NavMarker (+8..+0x13 of FmtStr::NavMarker).
    Vector GetMarkerPosition(FmtStr::NavMarker const& Marker)
    {
        Vector Position;
        std::memcpy(&Position.x, &Marker.Data[2], sizeof(float));
        std::memcpy(&Position.y, &Marker.Data[3], sizeof(float));
        std::memcpy(&Position.z, &Marker.Data[4], sizeof(float));
        return Position;
    }

    // The type 5 objective fields slot 11 writes: +0x38 reputation, +0x3c the connection's
    // system, marker +0x40 the current system, +0x44 = 1, +0x48 the point.
    void FillPathObjective(FNNObjectiveType5* Objective, int RepInstance, unsigned int GotoSystemId,
                           unsigned int CurrentSystemId, Vector const& Point)
    {
        Objective->RepInstance = RepInstance;
        Objective->SystemId = GotoSystemId;
        std::memcpy(&Objective->Marker.Data[2], &Point.x, sizeof(float));
        std::memcpy(&Objective->Marker.Data[3], &Point.y, sizeof(float));
        std::memcpy(&Objective->Marker.Data[4], &Point.z, sizeof(float));
        Objective->Marker.Data[1] = 1;
        Objective->Marker.Data[0] = CurrentSystemId;
    }

    // One axis of the slot 11 "leg unchanged" test against 1.0f (DAT_06fb5810) in x87
    // precision; only "<=" counts as close (unordered does not).
    bool IsAxisClose(float Point, float Marker)
    {
        double const Delta = static_cast<double>(Point) - static_cast<double>(Marker);
        return Delta * Delta <= 1.0;
    }
}

// LAB_06f1b9f0. Ins is read after the erase. GotoSystemId is the far side of Target's
// connection (FUN_06f2a240) when Target is set and that succeeds. A kind 0 leg's first
// objective takes FirstType/FirstIds on the first leg, LegType/LegIds after; a leg of any
// other kind keeps the default objective.
bool FMissionObject::BuildPathObjectives(FSystemInfo* System, std::list<FNnPathLeg>* Legs,
                                         FMissionObjectiveList* List, int FirstIds, int LegIds,
                                         int TargetIds, unsigned int TargetObjectId, int FirstType,
                                         int LegType, int TargetType,
                                         std::list<FMissionObjective*>::iterator const* Where)
{
    std::list<FMissionObjective*>& Nodes = List->Objectives;
    std::list<FMissionObjective*>::iterator First = Nodes.begin();
    for (; First != Nodes.end(); ++First)
    {
        if ((*First)->GetType() == 5)
            break;
    }

    if (Legs->size() == 0)
    {
        if (First == Nodes.end())
            return false;
        std::list<FMissionObjective*>::iterator RunEnd = First;
        while (RunEnd != Nodes.end() && (*RunEnd)->GetType() == 5)
            ++RunEnd;
        List->EraseRange(First, RunEnd);
        return true;
    }

    if (First != Nodes.end() && (*First)->GetType() == 5)
    {
        FNnPathLeg const& FirstLeg = Legs->front();
        Vector const& Point = FirstLeg.Kind == 1 ? FirstLeg.PointB : FirstLeg.PointA;
        Vector const MarkerPosition =
            GetMarkerPosition(static_cast<FNNObjectiveType5*>(*First)->Marker);
        if (IsAxisClose(Point.x, MarkerPosition.x) && IsAxisClose(Point.y, MarkerPosition.y) &&
            IsAxisClose(Point.z, MarkerPosition.z))
        {
            return false;
        }
        std::list<FMissionObjective*>::iterator RunEnd = First;
        while (RunEnd != Nodes.end() && (*RunEnd)->GetType() == 5)
            ++RunEnd;
        List->EraseRange(First, RunEnd);
    }

    std::list<FMissionObjective*>::iterator Ins = Where != nullptr ? *Where : Nodes.begin();

    unsigned int GotoSystemId = System->SystemId;
    if (TargetObjectId != 0)
    {
        if (GetConnectionGotoSystem(System->SystemId, TargetObjectId, &GotoSystemId) != 0)
            GotoSystemId = System->SystemId;
    }

    std::list<FNnPathLeg>::iterator LegIt = Legs->begin();
    for (; LegIt != Legs->end(); ++LegIt)
    {
        FMissionObjective* Created = nullptr;
        if (CreateMissionObjective(5, this, &Created))
            Nodes.insert(Ins, Created);
        FNNObjectiveType5* Objective = static_cast<FNNObjectiveType5*>(Created);

        if (LegIt->Kind == 0)
        {
            if (LegIt == Legs->begin())
            {
                Objective->Objective.Type = static_cast<unsigned int>(FirstType);
                Objective->Ids = FirstIds;
            }
            else
            {
                Objective->Objective.Type = static_cast<unsigned int>(LegType);
                Objective->Ids = LegIds;
            }
            int RepA = -1;
            pub::SpaceObj::GetRep(LegIt->ObjectA, RepA);
            FillPathObjective(Objective, RepA, GotoSystemId, System->SystemId, LegIt->PointA);

            int RepB = -1;
            pub::SpaceObj::GetRep(LegIt->ObjectB, RepB);
            FMissionObjective* CreatedB = nullptr;
            if (CreateMissionObjective(5, this, &CreatedB))
                Nodes.insert(Ins, CreatedB);
            FNNObjectiveType5* ObjectiveB = static_cast<FNNObjectiveType5*>(CreatedB);
            ObjectiveB->Objective.Type = static_cast<unsigned int>(LegType);
            ObjectiveB->Ids = LegIds;
            FillPathObjective(ObjectiveB, RepB, GotoSystemId, System->SystemId, LegIt->PointB);
        }
        else if (LegIt->Kind == 1)
        {
            int Rep = -1;
            if (TargetObjectId != 0)
                pub::SpaceObj::GetRep(TargetObjectId, Rep);
            Objective->Ids = TargetIds;
            Objective->Objective.Type = static_cast<unsigned int>(TargetType);
            FillPathObjective(Objective, Rep, GotoSystemId, System->SystemId, LegIt->PointB);
        }
    }
    return true;
}

// LAB_06ea1460.
bool FMissionObject::FailOnPlayerDeath() { return true; }
// LAB_06ea1470.
bool FMissionObject::FailOnGroupMemberDeath(unsigned int Arg) { (void)Arg; return false; }
bool FMissionObject::FailOnSoloPlayerDeath() { return true; }
// LAB_06ea1480.
bool FMissionObject::FailOnPlayerLeave() { return false; }
bool FMissionObject::FailOnGroupMemberLeave(unsigned int Arg) { (void)Arg; return false; }
bool FMissionObject::Slot17() { return true; }

// ---------------------------------------------------------------------------------------
// FMissionObjective (base, vtable 06fb8ccc)
// ---------------------------------------------------------------------------------------

// FUN_06f19f90 (base part).
FMissionObjective::FMissionObjective(FMissionObject* InMission)
    : bHidden(0)
    , Ids(0)
    , Index(-1)
    , Mission(InMission)
    , PlayerState(nullptr)
{
    Nickname.clear();
    // The inline MissionObjective ctor sets Type 1; after the vtable store Type = 4.
    Objective.Type = 4;
    if (Mission != nullptr)
        PlayerState = Mission->PlayerState;
}

// Base part of the slot 5 clones (inlined in all eight).
FMissionObjective::FMissionObjective(FMissionObjective const& Other)
    : bHidden(Other.bHidden)
    , Nickname(Other.Nickname)
    , Ids(Other.Ids)
    , Index(Other.Index)
    , Mission(Other.Mission)
    , PlayerState(Other.PlayerState)
    , Objective(Other.Objective)
{
}

// FUN_06f1a5c0.
FMissionObjective::~FMissionObjective()
{
}

// FUN_06f1a980.
bool FMissionObjective::IsSame(FMissionObjective* Other)
{
    int OtherType = Other->GetType();
    if (GetType() != OtherType)
        return false;
    return Objective.Description == Other->Objective.Description;
}

// FUN_06f1d430. Every objective parser inlines the same test on its TString copy.
bool __cdecl IsNumberText(char const* Text)
{
    if (Text == nullptr)
        return false;
    char const Lead = Text[0];
    if (Lead == '\0')
        return false;
    if (Lead >= '0' && Lead <= '9')
        return true;
    return Lead == '-';
}

namespace
{

    // FUN_06ea9f40 copy of value Index into a TString<0x40> (null -> empty), as the parsers do
    // before the number test.
    void CopyValue(TString<0x40>& Out, INI_Reader* Reader, unsigned int ValueIndex)
    {
        Out.Length = 0;
        Out.Data[0] = '\0';
        Out = Reader->get_value_string(ValueIndex);
    }

    // FmtStr::destroy followed by zeroing its six dwords.
    void DestroyAndZero(FmtStr& Text)
    {
        Text.destroy();
        std::memset(&Text, 0, sizeof(Text));
    }

    // Slot 1 of types 5 and 6 (LAB_06f1b030). Retail quirk: the position is stored +0x48 =
    // value 8, +0x4c = value 7, +0x50 = value 6, the reverse of the x, y, z order slot 2
    // writes, so retail reloads such a save swapped.
    void ParseType56(FMissionObjective& Objective, INI_Reader* Reader, int& RepInstance,
                     unsigned int& SystemId, FmtStr::NavMarker& Marker)
    {
        Objective.Ids = Reader->get_value_int(1);
        SystemId = static_cast<unsigned int>(Reader->get_value_int(2));
        RepInstance = Reader->get_value_int(3);
        Marker.Data[1] = static_cast<unsigned int>(Reader->get_value_int(4));
        Marker.Data[0] = static_cast<unsigned int>(Reader->get_value_int(5));
        float const Value7 = Reader->get_value_float(7);
        float const Value8 = Reader->get_value_float(8);
        float const Value6 = Reader->get_value_float(6);
        std::memcpy(&Marker.Data[2], &Value8, sizeof(float));
        std::memcpy(&Marker.Data[3], &Value7, sizeof(float));
        std::memcpy(&Marker.Data[4], &Value6, sizeof(float));
    }

    // Slot 2 of types 5 and 6 (LAB_06f1b0d0).
    void SaveType56(FMissionObjective const& Objective, FILE* File, int RepInstance,
                    unsigned int SystemId, FmtStr::NavMarker const& Marker)
    {
        fprintf(File, "%d, ", Objective.Ids);
        fprintf(File, "%u, ", SystemId);
        fprintf(File, "%u, ", static_cast<unsigned int>(RepInstance));
        fprintf(File, "%d, ", static_cast<int>(Marker.Data[1]));
        fprintf(File, "%u, ", Marker.Data[0]);
        float X;
        float Y;
        float Z;
        std::memcpy(&X, &Marker.Data[2], sizeof(float));
        std::memcpy(&Y, &Marker.Data[3], sizeof(float));
        std::memcpy(&Z, &Marker.Data[4], sizeof(float));
        fprintf(File, "%f, %f, %f ", static_cast<double>(X), static_cast<double>(Y),
                static_cast<double>(Z));
        fprintf(File, "END");
    }

    // Slot 3 of types 5 and 6 (LAB_06f1b150). The objective's cached player state is not
    // null-tested.
    void BuildType56(FMissionObjective& Objective, int const& RepInstance,
                     unsigned int const& SystemId, FmtStr::NavMarker const& Marker)
    {
        FmtStr& Text = Objective.Objective.Description;
        Text.destroy();
        DestroyAndZero(Text);
        Text.begin_mad_lib(static_cast<unsigned int>(Objective.Ids));
        Text.append_system(SystemId);
        if (FindContentPlayerState(Objective.Mission->PlayerId) != nullptr &&
            Objective.PlayerState->ShipObject != nullptr && RepInstance != -1)
            Text.append_rep_instance(RepInstance);
        Text.append_nav_marker(Marker);
        Text.end_mad_lib();
    }

    // Slots 1 of navmarker / rep_inst: value 1 is a system id number or a system nickname.
    void ParseSystemValue(unsigned int& SystemId, INI_Reader* Reader)
    {
        TString<0x40> Text;
        CopyValue(Text, Reader, 1);
        if (IsNumberText(Text.Data))
            SystemId = static_cast<unsigned int>(Reader->get_value_int(1));
        else
            pub::GetSystemID(SystemId, Text.Data);
    }

    // Values 4, 5, 6 are read in the order 5, 4, 6.
    void ParsePosition(unsigned int* Out, INI_Reader* Reader)
    {
        float const Value5 = Reader->get_value_float(5);
        float const Value4 = Reader->get_value_float(4);
        float const Value6 = Reader->get_value_float(6);
        std::memcpy(&Out[0], &Value4, sizeof(float));
        std::memcpy(&Out[1], &Value5, sizeof(float));
        std::memcpy(&Out[2], &Value6, sizeof(float));
    }

    void SavePosition(FILE* File, unsigned int const* In, char const* Format)
    {
        float X;
        float Y;
        float Z;
        std::memcpy(&X, &In[0], sizeof(float));
        std::memcpy(&Y, &In[1], sizeof(float));
        std::memcpy(&Z, &In[2], sizeof(float));
        fprintf(File, Format, static_cast<double>(X), static_cast<double>(Y),
                static_cast<double>(Z));
    }
}

// ---------------------------------------------------------------------------------------
// Type 1 "base" (vtable 06fb8d20)
// ---------------------------------------------------------------------------------------

// FUN_06f19cd0 case 1.
FNNObjectiveBase::FNNObjectiveBase(FMissionObject* InMission)
    : FMissionObjective(InMission)
{
}

// LAB_06f1a9c0: value 1 is a base id number or a base nickname.
void FNNObjectiveBase::Parse(INI_Reader* Reader)
{
    TString<0x40> Text;
    CopyValue(Text, Reader, 1);
    if (IsNumberText(Text.Data))
        BaseId = static_cast<unsigned int>(Reader->get_value_int(1));
    else
        pub::GetBaseID(BaseId, Text.Data);
    Ids = Reader->get_value_int(2);
}

// LAB_06f1aa80.
void FNNObjectiveBase::Save(FILE* File)
{
    fprintf(File, "%u, ", BaseId);
    fprintf(File, "%d", Ids);
}

// FUN_06f1aab0. Retail destroys the FmtStr twice.
bool FNNObjectiveBase::BuildDescription()
{
    FmtStr& Text = Objective.Description;
    Text.destroy();
    DestroyAndZero(Text);
    Text.begin_mad_lib(static_cast<unsigned int>(Ids));
    Text.append_base(BaseId);
    Text.end_mad_lib();
    return true;
}

// LAB_06ee9e10.
int FNNObjectiveBase::GetType() { return 1; }

// FUN_06f1a010: new(0x3c).
FMissionObjective* FNNObjectiveBase::Clone() { return new FNNObjectiveBase(*this); }

// ---------------------------------------------------------------------------------------
// Type 2 "navmarker" (vtable 06fb8d04)
// ---------------------------------------------------------------------------------------

// FUN_06f19cd0 case 2.
FNNObjectiveNavMarker::FNNObjectiveNavMarker(FMissionObject* InMission)
    : FMissionObjective(InMission)
{
}

// LAB_06f1ab00.
void FNNObjectiveNavMarker::Parse(INI_Reader* Reader)
{
    ParseSystemValue(Marker.Data[0], Reader);
    Ids = Reader->get_value_int(2);
    Marker.Data[1] = static_cast<unsigned int>(Reader->get_value_int(3));
    ParsePosition(&Marker.Data[2], Reader);
}

// LAB_06f1abf0.
void FNNObjectiveNavMarker::Save(FILE* File)
{
    fprintf(File, "%u, ", Marker.Data[0]);
    fprintf(File, "%d, ", Ids);
    fprintf(File, "%d, ", static_cast<int>(Marker.Data[1]));
    SavePosition(File, &Marker.Data[2], "%f, %f, %f");
}

// LAB_06f1ac50.
bool FNNObjectiveNavMarker::BuildDescription()
{
    FmtStr& Text = Objective.Description;
    Text.destroy();
    DestroyAndZero(Text);
    Text.begin_mad_lib(static_cast<unsigned int>(Ids));
    Text.append_nav_marker(Marker);
    Text.end_mad_lib();
    return true;
}

// LAB_06ee9660.
int FNNObjectiveNavMarker::GetType() { return 2; }

// FUN_06f1a0a0: new(0x4c).
FMissionObjective* FNNObjectiveNavMarker::Clone() { return new FNNObjectiveNavMarker(*this); }

// ---------------------------------------------------------------------------------------
// Type 3 "ids" (vtable 06fb8ce8)
// ---------------------------------------------------------------------------------------

// FUN_06f19cd0 case 3.
FNNObjectiveIds::FNNObjectiveIds(FMissionObject* InMission)
    : FMissionObjective(InMission)
{
}

// FUN_06f1aca0.
void FNNObjectiveIds::Parse(INI_Reader* Reader)
{
    Ids = Reader->get_value_int(1);
}

// LAB_06f1acc0.
void FNNObjectiveIds::Save(FILE* File)
{
    fprintf(File, "%d", Ids);
}

// LAB_06f1ace0.
bool FNNObjectiveIds::BuildDescription()
{
    FmtStr& Text = Objective.Description;
    Text.destroy();
    DestroyAndZero(Text);
    Text.begin_mad_lib(static_cast<unsigned int>(Ids));
    Text.append_string(static_cast<unsigned int>(Ids));
    Text.end_mad_lib();
    return true;
}

// LAB_06f5e540.
int FNNObjectiveIds::GetType() { return 3; }

// FUN_06f1a140: new(0x38).
FMissionObjective* FNNObjectiveIds::Clone() { return new FNNObjectiveIds(*this); }

// ---------------------------------------------------------------------------------------
// Type 4 "rep_inst" (vtable 06fb8cb0)
// ---------------------------------------------------------------------------------------

// FUN_06f19cd0 case 4 (base ctor inlined).
FNNObjectiveRepInst::FNNObjectiveRepInst(FMissionObject* InMission)
    : FMissionObjective(InMission)
{
    Label.Length = 0;
    Label.Data[0] = '\0';
}

// LAB_06f1ad30.
void FNNObjectiveRepInst::Parse(INI_Reader* Reader)
{
    ParseSystemValue(Marker.Data[0], Reader);
    Ids = Reader->get_value_int(2);
    Marker.Data[1] = static_cast<unsigned int>(Reader->get_value_int(3));
    ParsePosition(&Marker.Data[2], Reader);
    Label = Reader->get_value_string(7);
}

// LAB_06f1ae70.
void FNNObjectiveRepInst::Save(FILE* File)
{
    fprintf(File, "%u, ", Marker.Data[0]);
    fprintf(File, "%d, ", Ids);
    fprintf(File, "%d, ", static_cast<int>(Marker.Data[1]));
    SavePosition(File, &Marker.Data[2], "%f, %f, %f, ");
    fprintf(File, "%s", Label.Data);
}

// LAB_06f1aee0 (GetLabelObjectId inlined). The reputation is -1 when there is no object or
// GetRep fails.
bool FNNObjectiveRepInst::BuildDescription()
{
    unsigned int ObjId = Mission->GetLabelObjectId(&Label);
    int Rep = -1;
    if ((ObjId & 0x3fffffff) != 0 && pub::SpaceObj::GetRep(ObjId, Rep) != 0)
        Rep = -1;
    FmtStr& Text = Objective.Description;
    Text.destroy();
    DestroyAndZero(Text);
    Text.begin_mad_lib(static_cast<unsigned int>(Ids));
    Text.append_rep_instance(Rep);
    Text.append_nav_marker(Marker);
    Text.end_mad_lib();
    return true;
}

// LAB_06f25080.
int FNNObjectiveRepInst::GetType() { return 4; }

// FUN_06f1a1d0: new(0x80).
FMissionObjective* FNNObjectiveRepInst::Clone() { return new FNNObjectiveRepInst(*this); }

// ---------------------------------------------------------------------------------------
// Types 5 and 6 (vtables 06fb8c94 / 06fb8c78)
// ---------------------------------------------------------------------------------------

// FUN_06f19cd0 case 5.
FNNObjectiveType5::FNNObjectiveType5(FMissionObject* InMission)
    : FMissionObjective(InMission)
    , RepInstance(-1)
    , SystemId(0)
{
}

void FNNObjectiveType5::Parse(INI_Reader* Reader) { ParseType56(*this, Reader, RepInstance, SystemId, Marker); }
void FNNObjectiveType5::Save(FILE* File) { SaveType56(*this, File, RepInstance, SystemId, Marker); }
bool FNNObjectiveType5::BuildDescription() { BuildType56(*this, RepInstance, SystemId, Marker); return true; }
// LAB_06ee99a0.
int FNNObjectiveType5::GetType() { return 5; }
// FUN_06f1a280: new(0x54).
FMissionObjective* FNNObjectiveType5::Clone() { return new FNNObjectiveType5(*this); }

// FUN_06f19cd0 case 6.
FNNObjectiveType6::FNNObjectiveType6(FMissionObject* InMission)
    : FMissionObjective(InMission)
    , RepInstance(-1)
    , SystemId(0)
{
}

void FNNObjectiveType6::Parse(INI_Reader* Reader) { ParseType56(*this, Reader, RepInstance, SystemId, Marker); }
void FNNObjectiveType6::Save(FILE* File) { SaveType56(*this, File, RepInstance, SystemId, Marker); }
bool FNNObjectiveType6::BuildDescription() { BuildType56(*this, RepInstance, SystemId, Marker); return true; }
// LAB_06f250c0.
int FNNObjectiveType6::GetType() { return 6; }
// FUN_06f1a330: new(0x54).
FMissionObjective* FNNObjectiveType6::Clone() { return new FNNObjectiveType6(*this); }

// ---------------------------------------------------------------------------------------
// Type 7 (vtable 06fb8c5c)
// ---------------------------------------------------------------------------------------

// FUN_06f19cd0 case 7 (base ctor inlined).
FNNObjectiveType7::FNNObjectiveType7(FMissionObject* InMission)
    : FMissionObjective(InMission)
{
}

// FUN_06f1a560 (deleting form 06f1a540).
FNNObjectiveType7::~FNNObjectiveType7()
{
}

// FUN_06ebf6f0.
void FNNObjectiveType7::Parse(INI_Reader* Reader) { (void)Reader; }
void FNNObjectiveType7::Save(FILE* File) { (void)File; }

// LAB_06f1b1e0.
bool FNNObjectiveType7::BuildDescription()
{
    DestroyAndZero(Objective.Description);
    Objective.Description = Text;
    return true;
}

// LAB_06f250d0.
int FNNObjectiveType7::GetType() { return 7; }

// FUN_06f1a3e0: new(0x50).
FMissionObjective* FNNObjectiveType7::Clone() { return new FNNObjectiveType7(*this); }

// ---------------------------------------------------------------------------------------
// Type 8 (vtable 06fb8c40)
// ---------------------------------------------------------------------------------------

// FUN_06f19cd0 case 8.
FNNObjectiveType8::FNNObjectiveType8(FMissionObject* InMission)
    : FMissionObjective(InMission)
{
}

// FUN_06ebf6f0.
void FNNObjectiveType8::Parse(INI_Reader* Reader) { (void)Reader; }
void FNNObjectiveType8::Save(FILE* File) { (void)File; }

// LAB_06f1b210.
bool FNNObjectiveType8::BuildDescription()
{
    FmtStr& Description = Objective.Description;
    DestroyAndZero(Description);
    Description.begin_mad_lib(static_cast<unsigned int>(Ids));
    Description.append_int(Value);
    Description.end_mad_lib();
    return true;
}

// LAB_06f1a480.
int FNNObjectiveType8::GetType() { return 8; }

// FUN_06f1a490: new(0x3c).
FMissionObjective* FNNObjectiveType8::Clone() { return new FNNObjectiveType8(*this); }

// ---------------------------------------------------------------------------------------
// Factory, [NNObjective] load/save
// ---------------------------------------------------------------------------------------

// FUN_06f19cd0. Any other type skips the store and returns whether *Out is already set.
bool __cdecl CreateMissionObjective(int Type, FMissionObject* Mission, FMissionObjective** Out)
{
    switch (Type)
    {
    case 1:
        *Out = new FNNObjectiveBase(Mission);
        break;
    case 2:
        *Out = new FNNObjectiveNavMarker(Mission);
        break;
    case 3:
        *Out = new FNNObjectiveIds(Mission);
        break;
    case 4:
        *Out = new FNNObjectiveRepInst(Mission);
        break;
    case 5:
        *Out = new FNNObjectiveType5(Mission);
        break;
    case 6:
        *Out = new FNNObjectiveType6(Mission);
        break;
    case 7:
        *Out = new FNNObjectiveType7(Mission);
        break;
    case 8:
        *Out = new FNNObjectiveType8(Mission);
        break;
    default:
        break;
    }
    return *Out != nullptr;
}

// FUN_06f1a610.
bool __cdecl DeleteMissionObjective(FMissionObjective** Objective)
{
    if (*Objective != nullptr)
    {
        delete *Objective;
        *Objective = nullptr;
    }
    return true;
}

// FUN_06f1a630. The factory result of "type" is not tested: a type outside 1..8 before any
// valid one calls Parse through null, as retail does.
void __cdecl LoadNNObjective(INI_Reader* Reader, FMissionObject* Mission, FMissionObjectiveList* List)
{
    ID_String NicknameId;
    NicknameId.clear();
    unsigned char bHiddenValue = 0;
    FMissionObjective* Objective = nullptr;
    // Retail never initialises this local; every [NNObjective] in the shipped DATA has a state
    // key, so the port's 0 is never observed.
    int State = 0;

    while (Reader->read_value())
    {
        if (Reader->is_value("nickname"))
        {
            // Retail uses MSVCRT atoi, which keeps all 32 bits of a saved hash id; UCRT atoi
            // saturates above INT_MAX and would break the later Act_SetNNObj lookup.
            NicknameId.Id = static_cast<unsigned int>(MsvcrtAtoi(Reader->get_value_string(0)));
            if (NicknameId.Id == 0)
                NicknameId.Id = CreateID(Reader->get_value_string(0));
        }
        else if (Reader->is_value("type"))
        {
            TString<0x40> Text;
            CopyValue(Text, Reader, 0);
            int Type = 0;
            if (IsNumberText(Text.Data))
            {
                Type = Reader->get_value_int(0);
                if (Type == 0)
                    continue;
            }
            else if (_stricmp(Reader->get_value_string(0), "navmarker") == 0)
                Type = 2;
            else if (_stricmp(Reader->get_value_string(0), "base") == 0)
                Type = 1;
            else if (_stricmp(Reader->get_value_string(0), "ids") == 0)
                Type = 3;
            else if (_stricmp(Reader->get_value_string(0), "rep_inst") == 0)
                Type = 4;
            else
                continue;
            CreateMissionObjective(Type, Mission, &Objective);
            Objective->Parse(Reader);
        }
        else if (Reader->is_value("state"))
        {
            TString<0x40> Text;
            CopyValue(Text, Reader, 0);
            if (IsNumberText(Text.Data))
            {
                State = Reader->get_value_int(0);
            }
            else
            {
                State = 10;
                if (_stricmp("ACTIVE", Reader->get_value_string(0)) == 0)
                    bHiddenValue = 0;
                else if (_stricmp("HIDDEN", Reader->get_value_string(0)) == 0)
                    bHiddenValue = 1;
            }
        }
        else if (Reader->is_value("hidden"))
        {
            bHiddenValue = Reader->get_value_bool(0) ? 1 : 0;
        }
    }

    if (Objective == nullptr)
        return;
    Objective->Nickname = NicknameId;
    Objective->Objective.Type = static_cast<unsigned int>(State);
    Objective->bHidden = bHiddenValue;
    List->Objectives.push_back(Objective);
}

// FUN_06f1a8e0.
void __cdecl SaveNNObjectives(FILE* File, FMissionObjectiveList* List)
{
    std::list<FMissionObjective*>::iterator It = List->Objectives.begin();
    for (; It != List->Objectives.end(); ++It)
    {
        FMissionObjective* Objective = *It;
        fprintf(File, "[NNObjective]\n");
        fprintf(File, "nickname = %u\n", Objective->Nickname.Id);
        fprintf(File, "type = %d, ", Objective->GetType());
        Objective->Save(File);
        fprintf(File, "\n");
        fprintf(File, "state = %d\n", static_cast<int>(Objective->Objective.Type));
        fprintf(File, "hidden = %d\n", Objective->bHidden != 0 ? 1 : 0);
        fprintf(File, "\n");
    }
}

// ---------------------------------------------------------------------------------------
// FMissionObjectiveList (vtables 06fb479c / 06fb478c)
// ---------------------------------------------------------------------------------------

// FUN_06f1ca70.
FMissionObjectiveList::FMissionObjectiveList()
{
}

// Inlined in FUN_06f1cee0.
FMissionObjectiveList::FMissionObjectiveList(FMissionObjectiveList const& Other)
{
    *this = Other;
}

// 06eb7900.
FMissionObjectiveList::~FMissionObjectiveList()
{
    Clear();
}

// FUN_06f1cb40 (the erase is vslot 3 = 06eb7850).
void FMissionObjectiveList::Clear()
{
    std::list<FMissionObjective*>::iterator It = Objectives.begin();
    while (It != Objectives.end())
    {
        if (*It != nullptr)
            delete *It;
        It = Objectives.erase(It);
    }
}

// FUN_06f1caa0 (Clear inlined).
FMissionObjectiveList& FMissionObjectiveList::operator=(FMissionObjectiveList const& Other)
{
    Clear();
    std::list<FMissionObjective*>::const_iterator It = Other.Objectives.begin();
    for (; It != Other.Objectives.end(); ++It)
        Objectives.push_back((*It)->Clone());
    return *this;
}

// FUN_06f1cb80.
void FMissionObjectiveList::EraseRange(std::list<FMissionObjective*>::iterator First,
                                       std::list<FMissionObjective*>::iterator Last)
{
    while (First != Last)
    {
        if (*First != nullptr)
            delete *First;
        First = Objectives.erase(First);
    }
}

// FUN_06f1cbc0.
void FMissionObjectiveList::InsertClones(std::list<FMissionObjective*>::iterator Where,
                                         FMissionObjectiveList const* Source,
                                         FMissionObject* InMission)
{
    std::list<FMissionObjective*>::const_iterator It = Source->Objectives.begin();
    for (; It != Source->Objectives.end(); ++It)
    {
        FMissionObjective* Copy = (*It)->Clone();
        Copy->Mission = InMission;
        Objectives.insert(Where, Copy);
    }
}

// FUN_06f1cc40.
FMissionObjective* FMissionObjectiveList::Add(int Type, FMissionObject* InMission)
{
    FMissionObjective* Created = nullptr;
    CreateMissionObjective(Type, InMission, &Created);
    if (Created != nullptr)
        Objectives.push_back(Created);
    return Created;
}

// FUN_06f1ccb0.
void FMissionObjectiveList::PopFront()
{
    if (Objectives.empty())
        return;
    if (Objectives.front() != nullptr)
    {
        delete Objectives.front();
        Objectives.front() = nullptr;
    }
    Objectives.pop_front();
}

// FUN_06f1cd00.
int FMissionObjectiveList::GetFrontType()
{
    if (Objectives.empty())
        return 0;
    return Objectives.front()->GetType();
}

// FUN_06f1cd20.
FMissionObjective* FMissionObjectiveList::GetFront()
{
    if (Objectives.empty())
        return nullptr;
    return Objectives.front();
}

// ---------------------------------------------------------------------------------------
// FMissionPlayer (vtable 06fb85b8)
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06f1b6d0.
FMissionPlayer::FMissionPlayer(unsigned int InPlayerId, FMissionObject* InMission)
    : PlayerId(InPlayerId)
    , Mission(InMission)
    , PlayerState(FindContentPlayerState(InPlayerId))
    , bTradelanePathChecked(0)
    , bForceResendObjectives(0)
{
    // After the vtable store.
    Objectives.Owner = this;
}

// 06f0e2b0 -> FUN_06f1cd40.
FMissionPlayer::~FMissionPlayer()
{
    SentObjectives.Clear();
}

// FUN_06f1d3d0.
void FMissionPlayer::SetForceResendObjectives(bool bValue)
{
    bForceResendObjectives = bValue ? 1 : 0;
    if (bValue)
        return;
    SentObjectives.Clear();
}

// LAB_06f1d420.
bool FMissionPlayer::GetForceResendObjectives()
{
    return bForceResendObjectives != 0;
}

// FUN_06f1d110.
void FMissionPlayer::SendObjectives()
{
    // The player's live mission must be this one or none.
    if (Mission == nullptr)
        return;
    if (FindContentPlayerState(Mission->PlayerId) == nullptr)
        return;
    unsigned int LiveMission = PlayerState->ValueD8;
    if (LiveMission != Mission->ControllerId && LiveMission != 0)
        return;

    Mission->ObjectiveResendTimer = 0.0f;

    // Visible objectives whose BuildDescription succeeds.
    unsigned int Count = 0;
    std::list<FMissionObjective*>::iterator It = Objectives.Objectives.begin();
    for (; It != Objectives.Objectives.end(); ++It)
    {
        if ((*It)->bHidden != 0)
            continue;
        if ((*It)->BuildDescription())
            ++Count;
    }

    if (Count == 0)
    {
        pub::Player::SetMissionObjectives(PlayerId, Mission->ControllerId, nullptr, 0,
                                          Mission->MissionTitle, Mission->MissionType,
                                          Mission->MissionOffer);
        return;
    }

    // Element ctor FUN_06f1a5d0 (Type 1). Retail quirk: the fill loop below writes one entry
    // per visible objective without the BuildDescription test, so it would write past the
    // array if a BuildDescription failed above (every override returns true).
    pub::Player::MissionObjective* Sent = new pub::Player::MissionObjective[Count];
    for (unsigned int Slot = 0; Slot < Count; ++Slot)
        Sent[Slot].Type = 1;
    unsigned int Index = 0;
    It = Objectives.Objectives.begin();
    for (; It != Objectives.Objectives.end(); ++It)
    {
        FMissionObjective* Objective = *It;
        if (Objective->bHidden != 0)
        {
            Objective->Index = -1;
            continue;
        }
        if (bForceResendObjectives == 0 && (Objective->Objective.Type & 0x10) != 0)
        {
            // Already sent (IsSame on a kept clone)?
            bool bKnown = false;
            std::list<FMissionObjective*>::iterator KeptIt = SentObjectives.Objectives.begin();
            for (; KeptIt != SentObjectives.Objectives.end(); ++KeptIt)
            {
                if ((*KeptIt)->IsSame(Objective))
                {
                    bKnown = true;
                    break;
                }
            }
            if (bKnown)
            {
                // Clear 0x100 if set; else 0x20, and then 0x40 if set, otherwise 0x10.
                unsigned int Type = Objective->Objective.Type;
                if ((Type & 0x100) != 0)
                {
                    Objective->Objective.Type = Type & ~0x100u;
                }
                else
                {
                    if ((Type & 0x20) != 0)
                        Objective->Objective.Type = Type & ~0x20u;
                    Type = Objective->Objective.Type;
                    if ((Type & 0x40) != 0)
                        Objective->Objective.Type = Type & ~0x40u;
                    else
                        Objective->Objective.Type = Type & ~0x10u;
                }
            }
            else
            {
                // Keep a clone in +0x28.
                if ((Objective->Objective.Type & 0x40) != 0)
                    Objective->Objective.Type = Objective->Objective.Type & ~0x40u;
                FMissionObjective* Kept = Objective->Clone();
                SentObjectives.Objectives.push_back(Kept);
            }
        }
        if (Objective->Ids == 1)
            Objective->Objective.Type = (Objective->Objective.Type & 0x80) | 9;
        Objective->Index = static_cast<int>(Index);
        Sent[Index].Type = Objective->Objective.Type;
        Sent[Index].Description = Objective->Objective.Description;
        ++Index;
    }

    pub::Player::SetMissionObjectives(PlayerId, Mission->ControllerId, Sent, Count,
                                      Mission->MissionTitle, Mission->MissionType,
                                      Mission->MissionOffer);
    delete[] Sent;
}

// FUN_06f1cea0.
void FMissionPlayer::ClearObjectives()
{
    Objectives.Clear();
}

// FUN_06f1cee0. The placeholder gets Type 0x89 with no null test. Temp is destroyed before
// Backup; both delete their elements.
void FMissionPlayer::SendPlaceholderObjective()
{
    FMissionOwnedObjectiveList Backup(Objectives);
    FMissionOwnedObjectiveList Temp;

    FMissionObjective* Placeholder = nullptr;
    CreateMissionObjective(7, Mission, &Placeholder);
    if (Placeholder != nullptr)
        Temp.Objectives.push_back(Placeholder);
    Placeholder->Objective.Type = 0x89;

    Objectives = Temp;
    SendObjectives();
    Objectives = Backup;
}

// ---------------------------------------------------------------------------------------
// FMissionObject player slots
// ---------------------------------------------------------------------------------------

// FUN_06f1b6d0.
FMissionPlayer* FMissionObject::GetOrCreatePlayer(unsigned int InPlayerId)
{
    std::map<unsigned int, FMissionPlayer*>::iterator It = Players.find(InPlayerId);
    if (It != Players.end())
        return It->second;
    FContentPlayerState* State = FindContentPlayerState(InPlayerId);
    if (State == nullptr)
        return nullptr;
    // In multiplayer the player must be in a system.
    if (!pub::SinglePlayer() && State->GetSystemRecord() == nullptr)
        return nullptr;
    // new(0x3c).
    FMissionPlayer* Player = new FMissionPlayer(InPlayerId, this);
    Players.insert(std::make_pair(InPlayerId, Player));
    return Player;
}

// FUN_06f1b830.
FMissionPlayer* FMissionObject::FindPlayer(unsigned int InPlayerId)
{
    std::map<unsigned int, FMissionPlayer*>::iterator It = Players.find(InPlayerId);
    if (It == Players.end())
        return nullptr;
    return It->second;
}

// FUN_06f1b8c0: the now empty list is resent before the record goes.
void FMissionObject::RemovePlayer(FMissionPlayer* Player)
{
    if (Player == nullptr)
        return;
    Player->Objectives.Clear();
    Player->SendObjectives();
    Players.erase(Player->PlayerId);
    delete Player;
}

// FUN_06f1b8a0.
void FMissionObject::RemovePlayerById(unsigned int InPlayerId)
{
    RemovePlayer(FindPlayer(InPlayerId));
}

// LAB_06f1b890.
FMissionPlayer* FMissionObject::GetFirstPlayer()
{
    if (Players.empty())
        return nullptr;
    return Players.begin()->second;
}

// FUN_06f0fc40. The +0xd0 loot records are not touched.
void FMissionObject::EndMission()
{
    FMissionPlayer* Player = GetFirstPlayer();
    while (Player != nullptr)
    {
        RemovePlayerById(Player->PlayerId);
        Player = GetFirstPlayer();
    }
    MissionTitle.destroy();
    std::memset(&MissionTitle, 0, sizeof(MissionTitle));
    MissionOffer.destroy();
    std::memset(&MissionOffer, 0, sizeof(MissionOffer));
    MissionNpcs.clear();
    NpcObjects.clear();
    MissionShips.clear();
    ObjLists.clear();
    MetaBehaviors.clear();
    MissionSolars.clear();
    RandEncs.clear();
    bPendingDestroy = 1;
}

// FUN_06f1ca50.
void FMissionObject::SetPlayer(unsigned int InPlayerId)
{
    PlayerId = InPlayerId;
    PlayerState = FindContentPlayerState(InPlayerId);
}

// DAT_06fce594 (.data, initial 1): the objective resend timer runs (msg 2 clears, 0x36 sets).
static bool GObjectiveTimerRunning = true;

// FUN_06f1b4f0. Messages 2 / 0x36 for this mission's player stop / restart the timer. Every
// 180 s (DAT_06fb8df8; unordered skips) the objectives are force-resent; SendObjectives
// zeroes the timer.
void FMissionObject::UpdateObjectiveTimer(pub::Controller::EMessage MessageId,
                                          void const* MessageParms)
{
    if (MessageId == pub::Controller::EMessage::BaseExit)
    {
        unsigned int const* Parms = static_cast<unsigned int const*>(MessageParms);
        if (Parms[1] == PlayerId && (PlayerId & 0x3fffffff) != 0)
            GObjectiveTimerRunning = false;
        return;
    }
    if (MessageId == pub::Controller::EMessage::SpaceEnter)
    {
        unsigned int const* Parms = static_cast<unsigned int const*>(MessageParms);
        if (Parms[1] == PlayerId && (PlayerId & 0x3fffffff) != 0)
            GObjectiveTimerRunning = true;
        return;
    }
    if (MessageId != pub::Controller::EMessage::Heartbeat)
        return;
    if (!GObjectiveTimerRunning)
        return;
    // The compare uses the unrounded x87 sum (FST keeps ST0); a double sum of two floats is
    // exact here.
    double const Sum = static_cast<double>(*static_cast<float const*>(MessageParms)) +
                       static_cast<double>(ObjectiveResendTimer);
    ObjectiveResendTimer = static_cast<float>(Sum);
    if (!(Sum >= 180.0))
        return;
    std::map<unsigned int, FMissionPlayer*>::iterator It = Players.begin();
    for (; It != Players.end(); ++It)
    {
        FMissionPlayer* Player = FindPlayer(It->first);
        if (Player == nullptr)
            continue;
        Player->SetForceResendObjectives(true);
        Player->SendObjectives();
        Player->SetForceResendObjectives(false);
    }
}

// FUN_06f1b260.
bool FMissionObject::SetObjectiveVisible(unsigned int const* NicknameId, bool bVisible)
{
    bool bFound = false;
    std::map<unsigned int, FMissionPlayer*>::iterator It = Players.begin();
    for (; It != Players.end(); ++It)
    {
        FMissionPlayer* Player = It->second;
        if (Player == nullptr)
            continue;
        std::list<FMissionObjective*>::iterator ObjIt = Player->Objectives.Objectives.begin();
        for (; ObjIt != Player->Objectives.Objectives.end(); ++ObjIt)
        {
            if (*NicknameId != (*ObjIt)->Nickname.Id)
                continue;
            (*ObjIt)->bHidden = bVisible ? 0 : 1;
            bFound = true;
            break;
        }
    }
    return bFound;
}

// FUN_06f1b310: no early exit, every match of every player.
void FMissionObject::SetObjectiveState(unsigned int const* NicknameId, int State)
{
    std::map<unsigned int, FMissionPlayer*>::iterator It = Players.begin();
    for (; It != Players.end(); ++It)
    {
        FMissionPlayer* Player = It->second;
        if (Player == nullptr)
            continue;
        std::list<FMissionObjective*>::iterator ObjIt = Player->Objectives.Objectives.begin();
        for (; ObjIt != Player->Objectives.Objectives.end(); ++ObjIt)
        {
            FMissionObjective* Objective = *ObjIt;
            if (*NicknameId != Objective->Nickname.Id)
                continue;
            Objective->Objective.Type = static_cast<unsigned int>(State);
            if (Objective->Index > -1)
                pub::Player::SetMissionObjectiveState(Player->PlayerId, ControllerId,
                                                      Objective->Index,
                                                      static_cast<unsigned int>(State));
        }
    }
}

// FUN_06f1b3d0: no null test on the records.
void FMissionObject::ClearObjectives()
{
    std::map<unsigned int, FMissionPlayer*>::iterator It = Players.begin();
    for (; It != Players.end(); ++It)
    {
        FMissionPlayer* Player = It->second;
        Player->Objectives.Clear();
        Player->SendObjectives();
    }
}

// FUN_06f1b480: null records skipped.
void FMissionObject::HideAllObjectives()
{
    std::map<unsigned int, FMissionPlayer*>::iterator It = Players.begin();
    for (; It != Players.end(); ++It)
    {
        FMissionPlayer* Player = It->second;
        if (Player == nullptr)
            continue;
        std::list<FMissionObjective*>::iterator ObjIt = Player->Objectives.Objectives.begin();
        for (; ObjIt != Player->Objectives.Objectives.end(); ++ObjIt)
            (*ObjIt)->bHidden = 1;
    }
}

// FUN_06f1b610.
void FMissionObject::RenumberAllObjectives()
{
    std::map<unsigned int, FMissionPlayer*>::iterator It = Players.begin();
    for (; It != Players.end(); ++It)
        RenumberObjectives(It->first);
}

// FUN_06f1b670: the record is not null-tested.
void FMissionObject::RenumberObjectives(unsigned int InPlayerId)
{
    FMissionPlayer* Player = FindPlayer(InPlayerId);
    int NextIndex = 0;
    std::list<FMissionObjective*>::iterator ObjIt = Player->Objectives.Objectives.begin();
    for (; ObjIt != Player->Objectives.Objectives.end(); ++ObjIt)
    {
        FMissionObjective* Objective = *ObjIt;
        if (Objective->bHidden != 0)
        {
            Objective->Index = -1;
            continue;
        }
        if (NextIndex != 0)
            Objective->Objective.Type = 10;
        Objective->Index = NextIndex;
        ++NextIndex;
    }
    Player->SendObjectives();
}

// FUN_06ea1490.
void FMissionObject::Slot20(unsigned int Arg0, unsigned int Arg1)
{
    (void)Arg0;
    (void)Arg1;
}

// FUN_06f0f150. Every case ends at the common tail, which forwards the message to slot 19
// while this is the player's live mission.
void FMissionObject::HandleMissionMessage(pub::Controller::EMessage MessageId,
                                          void const* MessageParms)
{
    using pub::Controller::EMessage;
    // No player state: return without the tail.
    if (FindContentPlayerState(PlayerId) == nullptr)
        return;
    if (MessageId != EMessage::PlayerLogout && MessageId != EMessage::GroupMemberLeft)
        NotifyRandEncs(MessageId, MessageParms);

    unsigned int const* Parms = static_cast<unsigned int const*>(MessageParms);
    switch (MessageId)
    {
    case EMessage::PlayerSave:
        // Parms = {player id, FILE*}; single player only, skipped while a trigger save is
        // pending.
        if (FindContentPlayerState(PlayerId) == nullptr)
            break;
        if (Parms[0] != PlayerId)
            break;
        if (!pub::SinglePlayer())
            break;
        if (bTriggerSavePending != 0)
            break;
        if (PlayerState->ShipObject != nullptr)
            SnapshotMissionObjects();
        {
            FILE* File = static_cast<FILE* const*>(MessageParms)[1];
            SaveMission(File);
            SaveRandEncs(File);
        }
        break;

    case EMessage::PlayerLoad:
    {
        // Parms = {player id, INI_Reader*}; the reader's state is kept around the load.
        if (FindContentPlayerState(PlayerId) == nullptr)
            break;
        if (Parms[0] != PlayerId)
            break;
        INI_Reader* Reader = static_cast<INI_Reader* const*>(MessageParms)[1];
        INI_Reader::State Saved;
        Reader->get_state(Saved);
        Reader->reset();
        LoadMissionSave(Reader);
        Reader->set_state(Saved);
        break;
    }

    case EMessage::PlayerLaunch:
        // Single player only: every +0x134 name is spawned again, then the list is emptied.
        if (!pub::SinglePlayer())
            break;
        if (Parms[0] != PlayerId)
            break;
        {
            std::list<TString<0x30>>::iterator It = RespawnSolars.begin();
            for (; It != RespawnSolars.end(); ++It)
                SpawnMissionSolar(&*It, nullptr);
        }
        RespawnSolars.clear();
        break;

    case EMessage::PlayerLogout:
    case EMessage::GroupMemberLeft:
    {
        RemovePlayerById(Parms[0]);
        std::vector<unsigned int>::iterator Dead =
            std::find(DeadPlayerIds.begin(), DeadPlayerIds.end(), Parms[0]);
        if (Dead != DeadPlayerIds.end())
            DeadPlayerIds.erase(Dead);
        bool bFail = false;
        if (PlayerId == Parms[0])
        {
            // The mission's own player: fail, or hand the mission to the first remaining
            // record's player.
            if (FailOnPlayerLeave())
            {
                bFail = true;
            }
            else
            {
                FMissionPlayer* First = GetFirstPlayer();
                if (First != nullptr)
                    SetPlayer(First->PlayerId);
                else
                    SetPlayer(0);
            }
        }
        else if (FindContentPlayerState(PlayerId) != nullptr && PlayerState->IsGroupMember(Parms[0]) &&
                 FailOnGroupMemberLeave(Parms[0]))
        {
            bFail = true;
        }
        if (bFail)
        {
            OnMissionFail();
            SetPlayer(0);
        }
        if (PlayerState == nullptr)
            bPendingDestroy = 1;
        break;
    }

    case EMessage::GroupMemberJoined:
        // A group member of the player joins the mission (FUN_06f48ba0); retail does not
        // null-test the member's state.
        if (FindContentPlayerState(PlayerId) == nullptr)
            break;
        if (!PlayerState->IsGroupMember(Parms[0]))
            break;
        FindContentPlayerState(Parms[0])->JoinMission(&ControllerId, PlayerId, false, 0);
        break;

    case EMessage::Heartbeat:
        UpdateJumpers(*static_cast<float const*>(MessageParms));
        break;

    case EMessage::AbortMission:
    {
        if (Parms[0] != PlayerId)
            break;
        if (FindContentPlayerState(PlayerId) == nullptr)
            break;
        // GetLiveMission = FUN_06f487c0 (cached state +0xd8).
        if (PlayerState->GetLiveMission() != ControllerId)
            break;
        OnMissionFail();
        break;
    }

    case EMessage::Msg0x25:
        OnShipJump(Parms);
        break;

    case EMessage::JumpInComplete:
        // A pending jump starts its timer.
        if (bJumpPending != 0)
            JumpTimer = 0.0f;
        break;

    case EMessage::Msg0x27:
    {
        // The player whose ship is *parms (FUN_06f49bc0) is appended to +0xdc.
        FContentPlayerState* Dead = FindContentPlayerStateByShip(Parms);
        if (Dead == nullptr)
            break;
        unsigned int DeadId = Dead->PlayerId;
        DeadPlayerIds.push_back(DeadId);
        break;
    }

    case EMessage::SpaceEnter:
    {
        // Single player: the respawn keeps the hit points unless the player launched from a
        // base (type 0x20 or 0x100).
        std::vector<unsigned int>::iterator Dead =
            std::find(DeadPlayerIds.begin(), DeadPlayerIds.end(), Parms[1]);
        if (Dead != DeadPlayerIds.end())
            DeadPlayerIds.erase(Dead);
        if (Parms[1] != PlayerId)
            break;
        if (!pub::SinglePlayer())
            break;
        bool bKeepHitPoints = true;
        if ((Parms[2] & 0x3fffffff) != 0)
        {
            unsigned int Type;
            pub::SpaceObj::GetType(Parms[2], Type);
            if ((Type & 0x20) != 0 || (Type & 0x100) != 0)
                bKeepHitPoints = false;
        }
        RespawnMissionObjects(bKeepHitPoints);
        ApplyVibeInfos();
        break;
    }

    case EMessage::SpaceExit:
        if (!pub::SinglePlayer())
            break;
        if (Parms[1] != PlayerId)
            break;
        SnapshotMissionObjects();
        break;

    case EMessage::ObjectDestroyed:
    {
        // A SpaceObj left space; parms[0] its id, parms[2] the other object of the event,
        // parms[3] a player id.
        // A live mission NPC's ship slot gets its jumper flag back.
        std::map<unsigned int, FContentNpcShip*>::iterator NpcIt = NpcObjects.find(Parms[0]);
        if (NpcIt != NpcObjects.end() && NpcIt->second != nullptr)
        {
            FMissionShip* Ship = FindMissionShip(&NpcIt->second->Nickname);
            if (Ship != nullptr)
                Ship->bJumper = Ship->bOriginalJumpFlag;
        }
        // The first jumper entry of that SpaceObj is unlinked (its NPC is not null-tested).
        {
            std::list<FMissionJumper>::iterator It = Jumpers.begin();
            for (; It != Jumpers.end(); ++It)
            {
                if (It->Npc->SpaceObjId == Parms[0])
                {
                    Jumpers.erase(It);
                    break;
                }
            }
        }
        RemoveRandEncMember(Parms);
        // RemoveMissionObject = FUN_06f74620, single player only.
        {
            FContentPlayerState* State = FindContentPlayerState(PlayerId);
            if (pub::SinglePlayer() && State != nullptr)
            {
                FSystemInfo* System = PlayerState->GetSystemRecord();
                if (System != nullptr)
                    System->RemoveMissionObject(Parms[0]);
            }
        }
        // An NPC leaves +0xa4 (deleted when there is no other object); otherwise a [MsnSolar] /
        // [MsnLoot] record with that id is marked 2 (the loot walk only when no solar matched).
        bool const bNoOther = (Parms[2] & 0x3fffffff) == 0;
        NpcIt = NpcObjects.find(Parms[0]);
        if (NpcIt != NpcObjects.end())
        {
            if (bNoOther && NpcIt->second != nullptr)
                delete NpcIt->second;
            NpcObjects.erase(NpcIt);
        }
        else
        {
            bool bSolar = false;
            std::list<FMissionSolar>::iterator SolarIt = MissionSolars.begin();
            for (; SolarIt != MissionSolars.end(); ++SolarIt)
            {
                if (SolarIt->SpaceObjId == Parms[0])
                {
                    SolarIt->bSpawned = 2;
                    SolarIt->SpaceObjId = 0;
                    bSolar = true;
                    break;
                }
            }
            if (!bSolar)
            {
                std::list<FMissionLoot>::iterator LootIt = MissionLoots.begin();
                for (; LootIt != MissionLoots.end(); ++LootIt)
                {
                    if (LootIt->SpaceObjId == Parms[0])
                    {
                        LootIt->bSpawned = 2;
                        LootIt->SpaceObjId = 0;
                        break;
                    }
                }
            }
        }
        // Every label entry of that SpaceObj leaves +0xec.
        {
            std::multimap<unsigned int, unsigned int>::iterator It = FormationMembers.begin();
            while (It != FormationMembers.end())
            {
                if (It->second == Parms[0])
                    It = FormationMembers.erase(It);
                else
                    ++It;
            }
        }
        // Nothing more for a player in +0xdc.
        if (std::find(DeadPlayerIds.begin(), DeadPlayerIds.end(), Parms[3]) != DeadPlayerIds.end())
            break;
        if (FindContentPlayerState(PlayerId) == nullptr)
            break;
        // IsGroupMember = FUN_06f48190, GetGroupSize = FUN_06f480c0.
        unsigned int ShipId = 0;
        if (PlayerState->ShipObject != nullptr)
            ShipId = PlayerState->ShipObject->SpaceObjId;
        if (PlayerId == Parms[3] && ShipId == Parms[0] && FailOnPlayerDeath())
        {
            OnMissionFail();
            break;
        }
        if (PlayerState->IsGroupMember(Parms[3]) && FailOnGroupMemberDeath(Parms[3]))
        {
            OnMissionFail();
            break;
        }
        if (PlayerId == Parms[3] && ShipId == Parms[0] && PlayerState->GetGroupSize() == 1 && FailOnSoloPlayerDeath())
            OnMissionFail();
        break;
    }

    case EMessage::SystemEnter:
        PlaceJumpersAtLaunch(Parms);
        break;

    case EMessage::Msg0x1006:
        if (Parms[1] == 0)
            OnMissionFail();
        break;

    case EMessage::FormationLeaderChanged:
    {
        // Parms[1] is an NPC of this mission (+0x170). A player record with a visible rep_inst
        // objective whose label resolves (only with a player ship) to parms[0]'s SpaceObj id
        // renumbers every player, then the walk goes on with the next record.
        FContentSpaceObj const* Object = *static_cast<FContentSpaceObj* const*>(MessageParms);
        FContentNpcShip const* Npc = static_cast<FContentNpcShip* const*>(MessageParms)[1];
        if (Npc->ControllerId != ControllerId)
            break;
        std::map<unsigned int, FMissionPlayer*>::iterator RecordIt = Players.begin();
        for (; RecordIt != Players.end(); ++RecordIt)
        {
            FMissionPlayer* Record = RecordIt->second;
            if (Record == nullptr)
                continue;
            std::list<FMissionObjective*>::iterator ObjIt = Record->Objectives.Objectives.begin();
            for (; ObjIt != Record->Objectives.Objectives.end(); ++ObjIt)
            {
                FMissionObjective* Objective = *ObjIt;
                if (Objective->bHidden != 0)
                    continue;
                if (Objective->GetType() != 4)
                    continue;
                TString<0x30> const* Label = &static_cast<FNNObjectiveRepInst*>(Objective)->Label;
                unsigned int TargetId = 0;
                if (FindContentPlayerState(PlayerId) != nullptr && PlayerState->ShipObject != nullptr)
                {
                    TargetId = GetLabelShipId(Label);
                    if ((TargetId & 0x3fffffff) == 0)
                    {
                        TargetId = GetLootId(Label);
                        if ((TargetId & 0x3fffffff) == 0)
                            TargetId = GetSolarId(Label);
                    }
                }
                if ((TargetId & 0x3fffffff) == 0)
                    continue;
                if (TargetId != Object->SpaceObjId)
                    continue;
                std::map<unsigned int, FMissionPlayer*>::iterator PlayerIt = Players.begin();
                for (; PlayerIt != Players.end(); ++PlayerIt)
                    RenumberObjectives(PlayerIt->first);
                break;
            }
        }
        break;
    }

    case EMessage::NpcShipCreated:
    {
        FContentNpcShip* Npc = *static_cast<FContentNpcShip* const*>(MessageParms);
        // NPC +0x170 is its controller.
        if (Npc->ControllerId != ControllerId)
            break;
        if (FindContentPlayerState(PlayerId) == nullptr)
            break;
        // A [MarkObj] name (strcmp) marks the new NPC.
        bool bMarkObjHit = false;
        std::list<TString<0x30>>::iterator It = MarkObjNames.begin();
        for (; It != MarkObjNames.end(); ++It)
        {
            if (std::strcmp(Npc->Nickname.Data, It->Data) == 0)
            {
                bMarkObjHit = true;
                break;
            }
        }
        if (bMarkObjHit)
            pub::Player::MarkObj(PlayerId, Npc->SpaceObjId, 1);
        FMissionShip* Ship = FindMissionShip(&Npc->Nickname);
        if (Ship != nullptr && Ship->bJumper != 0)
        {
            // FUN_06f1e440 appends {0, 0}, then the new node's npc is set.
            FMissionJumper Jumper;
            Jumper.Npc = nullptr;
            Jumper.State = 0;
            Jumpers.push_back(Jumper);
            Jumpers.back().Npc = Npc;
        }
        AttachRandEncNpc(MessageParms);
        // Every label id on the NPC (+0x17c) goes into the +0xec multimap.
        std::list<unsigned int>::iterator FormIt = Npc->LabelIds.begin();
        for (; FormIt != Npc->LabelIds.end(); ++FormIt)
            FormationMembers.insert(std::make_pair(*FormIt, Npc->SpaceObjId));
        break;
    }

    default:
        break;
    }

    // The common tail: slot 19 (vtbl +0x4c) while this is the player's live mission.
    if (FindContentPlayerState(PlayerId) == nullptr)
        return;
    if (PlayerState->GetLiveMission() != ControllerId)
        return;
    UpdateObjectiveTimer(MessageId, MessageParms);
}

// FUN_06f0fda0. The reward is split by the group size (unsigned division) and paid through
// FUN_06f48050; FUN_06f48a20 ends the mission for the whole group.
void FMissionObject::OnMissionSucceed()
{
    FContentPlayerState* State = FindContentPlayerState(PlayerId);
    if (State == nullptr)
    {
        EndMission();
        return;
    }
    unsigned int const GroupSize = State->GetGroupSize();
    State->RewardGroup(static_cast<int>(Reward / GroupSize));
    ClearObjectives();
    EndMission();
    State->EndLiveMission(ControllerId, 2, true);
}

// FUN_06f0fea0 (ClearObjectives inlined).
void FMissionObject::OnMissionFail()
{
    ClearObjectives();
    FContentPlayerState* State = FindContentPlayerState(PlayerId);
    if (State != nullptr && State->GetLiveMission() == ControllerId)
    {
        EndMission();
        State->EndLiveMission(ControllerId, 3, true);
        return;
    }
    EndMission();
}

// FUN_06eef520.
int FMissionObject::MissionStateFromName(char const* Name)
{
    (void)Name;
    return -1;
}

// LAB_06ea14a0 (.rdata:06fb384c).
char const* FMissionObject::MissionStateToName(int State)
{
    (void)State;
    return "NOT_IMPLEMENTED";
}

// FUN_06ebf6f0.
void FMissionObject::OnNpcSystemEnter(void const* Parms)
{
    (void)Parms;
}

// FUN_06f15940.
unsigned int FMissionObject::GetLabelShipId(TString<0x30> const* Label)
{
    if (FindContentPlayerState(PlayerId) == nullptr)
        return 0;
    if (PlayerState->ShipObject == nullptr)
        return 0;
    // "Player" is .rdata:06fb4708.
    if (_stricmp(Label->Data, "Player") == 0)
    {
        if (FindContentPlayerState(PlayerId) == nullptr)
            return 0;
        if (PlayerState->ShipObject == nullptr)
            return 0;
        return PlayerState->ShipObject->SpaceObjId;
    }
    FContentNpcShip* Npc = FindNpcByName(Label);
    if (Npc == nullptr)
        return 0;
    return Npc->SpaceObjId;
}

// FUN_06f15710: with a live player ship, the first +0xa4 entry (in key order) whose nickname
// matches (stricmp); a null value or no match falls back to FUN_06f15840.
FContentNpcShip* FMissionObject::FindNpcByName(TString<0x30> const* Label)
{
    if (FindContentPlayerState(PlayerId) != nullptr && PlayerState->ShipObject != nullptr)
    {
        std::map<unsigned int, FContentNpcShip*>::iterator It = NpcObjects.begin();
        for (; It != NpcObjects.end(); ++It)
        {
            if (_stricmp(It->second->Nickname.Data, Label->Data) != 0)
                continue;
            if (It->second != nullptr)
                return It->second;
            break;
        }
    }
    return FindFormationNpc(Label);
}

// FUN_06f15840: in the +0xb8 records named Label, the first slot's NPC that is a formation
// leader (slot 0) or alone (formation -1); else FUN_06f15490.
FContentNpcShip* FMissionObject::FindFormationNpc(TString<0x30> const* Label)
{
    std::list<FMissionShipGroup>::iterator GroupIt = MissionShips.begin();
    for (; GroupIt != MissionShips.end(); ++GroupIt)
    {
        if (_stricmp(GroupIt->Nickname.Data, Label->Data) != 0)
            continue;
        std::vector<FMissionShip>::iterator SlotIt = GroupIt->Ships.begin();
        for (; SlotIt != GroupIt->Ships.end(); ++SlotIt)
        {
            std::map<unsigned int, FContentNpcShip*>::iterator It = NpcObjects.begin();
            for (; It != NpcObjects.end(); ++It)
            {
                if (_stricmp(It->second->Nickname.Data, SlotIt->Nickname.Data) == 0)
                    break;
            }
            if (It == NpcObjects.end())
                continue;
            FContentNpcShip* Npc = It->second;
            if (Npc == nullptr)
                continue;
            if (Npc->FormationSlot == 0 || Npc->FormationId == 0xffffffffu)
                return Npc;
        }
    }
    return FindRandEncFormationNpc(Label);
}

// FUN_06f15490: the same leader test over the random-encounter formation arms named Label.
FContentNpcShip* FMissionObject::FindRandEncFormationNpc(TString<0x30> const* Label)
{
    std::list<FMissionRandEnc>::iterator EncIt = RandEncs.begin();
    for (; EncIt != RandEncs.end(); ++EncIt)
    {
        std::list<FMissionRandEncArm>::iterator ArmIt = EncIt->Arms.begin();
        for (; ArmIt != EncIt->Arms.end(); ++ArmIt)
        {
            if (_stricmp(ArmIt->Name.get_string(), Label->Data) != 0)
                continue;
            std::list<unsigned int>::iterator IdIt = ArmIt->MemberIds.begin();
            for (; IdIt != ArmIt->MemberIds.end(); ++IdIt)
            {
                std::map<unsigned int, FContentNpcShip*>::iterator It = NpcObjects.begin();
                for (; It != NpcObjects.end(); ++It)
                {
                    if (It->second->SpaceObjId == *IdIt)
                        break;
                }
                if (It == NpcObjects.end())
                    continue;
                FContentNpcShip* Npc = It->second;
                if (Npc == nullptr)
                    continue;
                if (Npc->FormationSlot == 0 || Npc->FormationId == 0xffffffffu)
                    return Npc;
            }
        }
    }
    return nullptr;
}

// FUN_06f15b90 (GetLootId inlined).
unsigned int FMissionObject::GetLabelObjectId(TString<0x30> const* Label)
{
    if (FindContentPlayerState(PlayerId) == nullptr)
        return 0;
    if (PlayerState->ShipObject == nullptr)
        return 0;
    unsigned int ShipId = GetLabelShipId(Label);
    if ((ShipId & 0x3fffffff) != 0)
        return ShipId;
    unsigned int LootId = GetLootId(Label);
    if ((LootId & 0x3fffffff) != 0)
        return LootId;
    return GetSolarId(Label);
}

// FUN_06f15b10 (case-sensitive).
unsigned int FMissionObject::GetLootId(TString<0x30> const* Label)
{
    std::list<FMissionLoot>::iterator It = MissionLoots.begin();
    for (; It != MissionLoots.end(); ++It)
    {
        if (std::strcmp(It->Nickname.Data, Label->Data) == 0)
            return It->SpaceObjId;
    }
    return 0;
}

// FUN_06f159f0.
unsigned int FMissionObject::GetSolarId(TString<0x30> const* Label)
{
    if (FindContentPlayerState(PlayerId) == nullptr)
        return 0;
    if (PlayerState->ShipObject == nullptr)
        return 0;
    unsigned int SolarId = 0;
    std::list<FMissionSolar>::iterator It = MissionSolars.begin();
    for (; It != MissionSolars.end(); ++It)
    {
        if (std::strcmp(It->Nickname.Data, Label->Data) == 0)
        {
            SolarId = It->SpaceObjId;
            break;
        }
    }
    if ((SolarId & 0x3fffffff) != 0)
        return SolarId;
    // Else a universe object of that nickname.
    unsigned int NicknameId = pub::GetNicknameId(Label->Data);
    if (IsSpaceObjTypeSet(NicknameId, PlayerState->GetSystemRecord()))
        return NicknameId;
    return 0;
}

// FUN_06f156b0.
FContentNpcShip* FMissionObject::FindNpcById(unsigned int const* Id)
{
    std::map<unsigned int, FContentNpcShip*>::iterator It = NpcObjects.lower_bound(*Id);
    if (It == NpcObjects.end() || *Id < It->first)
        return nullptr;
    return It->second;
}

// FUN_06f15ae0.
FMissionLoot* FMissionObject::FindLootById(unsigned int const* SpaceObjId)
{
    unsigned int const Id = *SpaceObjId;
    std::list<FMissionLoot>::iterator It = MissionLoots.begin();
    for (; It != MissionLoots.end(); ++It)
    {
        if (It->SpaceObjId == Id)
            return &*It;
    }
    return nullptr;
}

// FUN_06f15560.
FMissionShip* FMissionObject::FindMissionShip(TString<0x30> const* Nickname)
{
    std::list<FMissionShipGroup>::iterator GroupIt = MissionShips.begin();
    for (; GroupIt != MissionShips.end(); ++GroupIt)
    {
        std::vector<FMissionShip>::iterator ShipIt = GroupIt->Ships.begin();
        for (; ShipIt != GroupIt->Ships.end(); ++ShipIt)
        {
            if (_stricmp(ShipIt->Nickname.Data, Nickname->Data) == 0)
                return &*ShipIt;
        }
    }
    return nullptr;
}

// FUN_06f155f0: a ship slot's [NPC] space_costume, else a [MsnSolar]'s costume (+0x478).
Costume* FMissionObject::GetMissionCostume(TString<0x30> const* Nickname)
{
    FMissionShip* Ship = FindMissionShip(Nickname);
    if (Ship != nullptr)
    {
        std::map<unsigned int, FMsnNpc>::iterator NpcIt = MissionNpcs.lower_bound(Ship->NpcId.Id);
        if (NpcIt == MissionNpcs.end() || Ship->NpcId.Id < NpcIt->first)
            return nullptr;
        return &NpcIt->second.Appearance;
    }
    std::list<FMissionSolar>::iterator SolarIt = MissionSolars.begin();
    for (; SolarIt != MissionSolars.end(); ++SolarIt)
    {
        if (_stricmp(SolarIt->Nickname.Data, Nickname->Data) == 0)
            return &SolarIt->SpaceCostume;
    }
    return nullptr;
}

// FUN_06f159c0.
FMissionSolar* FMissionObject::FindSolarById(unsigned int const* ObjId)
{
    unsigned int const Id = *ObjId;
    std::list<FMissionSolar>::iterator It = MissionSolars.begin();
    for (; It != MissionSolars.end(); ++It)
    {
        if (It->SpaceObjId == Id)
            return &*It;
    }
    return nullptr;
}

// FUN_06f15c70: NPC, solar, "Player", then loot.
TString<0x30> FMissionObject::GetMissionObjectName(unsigned int const* ObjId)
{
    TString<0x30> Name;
    Name.Length = 0;
    Name.Data[0] = '\0';
    std::map<unsigned int, FContentNpcShip*>::iterator NpcIt = NpcObjects.find(*ObjId);
    if (NpcIt != NpcObjects.end() && NpcIt->second != nullptr)
    {
        Name = NpcIt->second->Nickname.Data;
        return Name;
    }
    std::list<FMissionSolar>::iterator SolarIt = MissionSolars.begin();
    for (; SolarIt != MissionSolars.end(); ++SolarIt)
    {
        if (SolarIt->SpaceObjId == *ObjId)
        {
            Name = SolarIt->Nickname.Data;
            return Name;
        }
    }
    // No player state: empty, even for a loot.
    if (FindContentPlayerState(PlayerId) == nullptr)
        return Name;
    if (PlayerState->ShipObject != nullptr && *ObjId == PlayerState->ShipObject->SpaceObjId)
    {
        Name = "Player";
        return Name;
    }
    std::list<FMissionLoot>::iterator LootIt = MissionLoots.begin();
    for (; LootIt != MissionLoots.end(); ++LootIt)
    {
        if (LootIt->SpaceObjId == *ObjId)
        {
            Name = LootIt->Nickname.Data;
            return Name;
        }
    }
    return Name;
}

// FUN_06f199f0.
bool FMissionObject::MarkMissionObject(unsigned int ObjId, int Mode, TString<0x30> Name)
{
    bool bResult = true;
    if (FindContentPlayerState(PlayerId) == nullptr)
        return bResult;
    if (pub::Player::MarkObj(PlayerState->PlayerId, ObjId, Mode) != 0)
        bResult = false;
    if (Mode == 0)
    {
        std::list<TString<0x30>>::iterator It = MarkObjNames.begin();
        for (; It != MarkObjNames.end(); ++It)
        {
            if (std::strcmp(It->Data, Name.Data) == 0)
                break;
        }
        if (It != MarkObjNames.end())
            MarkObjNames.erase(It);
        return bResult;
    }
    if (Mode == 1)
    {
        std::list<TString<0x30>>::iterator It = MarkObjNames.begin();
        for (; It != MarkObjNames.end(); ++It)
        {
            if (std::strcmp(It->Data, Name.Data) == 0)
                break;
        }
        if (It == MarkObjNames.end())
            MarkObjNames.push_back(Name);
        return bResult;
    }
    return false;
}

// FUN_06f14980.
FMissionJumper* FMissionObject::FindJumperNpc(TString<0x30> const* Nickname)
{
    std::list<FMissionJumper>::iterator It = Jumpers.begin();
    for (; It != Jumpers.end(); ++It)
    {
        if (std::strcmp(Nickname->Data, It->Npc->Nickname.Data) == 0)
            return &*It;
    }
    return nullptr;
}

// FUN_06f14e30.
void FMissionObject::RemoveJumperNpc(unsigned int const* ObjId)
{
    std::list<FMissionJumper>::iterator It = Jumpers.begin();
    for (; It != Jumpers.end(); ++It)
    {
        if (It->Npc->SpaceObjId == *ObjId)
        {
            Jumpers.erase(It);
            return;
        }
    }
}

// FUN_06f149f0.
void FMissionObject::OnShipJump(unsigned int const* ShipId)
{
    if (FindContentPlayerState(PlayerId) == nullptr)
        return;
    FContentPlayerShip* PlayerShip = PlayerState->ShipObject;
    if (PlayerShip == nullptr)
        return;
    if (*ShipId == PlayerShip->SpaceObjId)
    {
        // The player's target gate (+0x174) gives the destination. Relocate = FUN_06f34800.
        if (pub::SpaceObj::GetJumpTarget(PlayerShip->TargetId, JumpSystemId, JumpGateId) != 0)
            return;
        std::list<FMissionJumper>::iterator It = Jumpers.begin();
        for (; It != Jumpers.end(); ++It)
        {
            if (It->State != 0)
                continue;
            It->State = 2;
            It->Npc->Relocate(nullptr, nullptr, nullptr);
        }
        return;
    }
    // A live mission NPC: the first jumper with its nickname (strcmp) leaves its system.
    std::map<unsigned int, FContentNpcShip*>::iterator Found = NpcObjects.find(*ShipId);
    if (Found == NpcObjects.end() || Found->second == nullptr)
        return;
    std::list<FMissionJumper>::iterator It = Jumpers.begin();
    for (; It != Jumpers.end(); ++It)
    {
        if (std::strcmp(Found->second->Nickname.Data, It->Npc->Nickname.Data) != 0)
            continue;
        It->State = 1;
        It->Npc->Relocate(nullptr, nullptr, nullptr);
        return;
    }
}

// FUN_06f14b40. JumpDelay is an int loaded onto the x87 stack.
void FMissionObject::UpdateJumpers(float DeltaTime)
{
    if (bJumpPending == 0)
        return;
    if (JumpTimer == -1.0f)
        return;
    if (static_cast<float>(JumpDelay) > JumpTimer)
    {
        JumpTimer = JumpTimer + DeltaTime;
        return;
    }
    // AddJumpingInSpaceObj = FUN_06f738c0. The system record is not null-tested.
    std::list<FMissionJumper>::iterator It = Jumpers.begin();
    for (; It != Jumpers.end(); ++It)
    {
        if (It->State != 2)
            continue;
        FSystemInfo* JumpSystem = FindSystemInfo(&JumpSystemId);
        JumpSystem->AddJumpingInSpaceObj(It->Npc, JumpGateId);
        It->Npc->CancelDirectives();
        It->Npc->SetGuide(nullptr, false, false);
        unsigned int Arrival[2];
        Arrival[0] = It->Npc->SpaceObjId;
        Arrival[1] = JumpSystem->SystemId;
        OnNpcSystemEnter(Arrival);
        It->State = 0;
    }
    bJumpPending = 0;
    JumpTimer = -1.0f;
}

// FUN_06f14c10. Without a player state retail leaves the selection kind and the orientation
// unset (stack garbage); the port starts them at 0 / identity.
void FMissionObject::PlaceJumpersAtLaunch(unsigned int const* Parms)
{
    std::list<FMissionJumper>::iterator It = Jumpers.begin();
    for (; It != Jumpers.end(); ++It)
    {
        if (It->State != 1)
        {
            if (It->State == 2)
                bJumpPending = 1;
            continue;
        }
        // The anchor by the launching object's type (0x40 jump gate / hole: 700 (DAT_06fb8ac8)
        // above it; 0x800: at it) or the player's ship (selection 3).
        unsigned int Type = 0;
        pub::SpaceObj::GetType(Parms[3], Type);
        Transform Placement;
        Placement.Rotation = Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
        Placement.Translation.x = 0.0f;
        Placement.Translation.y = 0.0f;
        Placement.Translation.z = 0.0f;
        Vector Position;
        Position.x = 0.0f;
        Position.y = 0.0f;
        Position.z = 0.0f;
        Matrix Orientation(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
        int Kind = 0;
        bool bAnchor = true;
        if ((Type & 0x40) != 0)
        {
            pub::SpaceObj::GetLocation(Parms[3], Position, Orientation);
            Position.z = Position.z + 700.0f;
            Kind = 0;
        }
        else if ((Type & 0x800) != 0)
        {
            pub::SpaceObj::GetLocation(Parms[3], Position, Orientation);
            Kind = 0;
        }
        else if (FindContentPlayerState(PlayerId) != nullptr)
        {
            pub::SpaceObj::GetLocation(PlayerState->GetShipId(), Position, Orientation);
            Kind = 3;
        }
        else
        {
            bAnchor = false;
        }
        if (bAnchor)
        {
            Placement.Translation = Position;
            Placement.Rotation = Orientation;
        }
        // GetEmptyPos with the object's radius x1.25 (DAT_06fb8ac4) as the start and the NPC's
        // radius as the clearance; GetRadius overwrites the position with the centre. A failure
        // leaves the jumper in state 1.
        float LaunchRadius = 0.0f;
        pub::SpaceObj::GetRadius(Parms[3], LaunchRadius, Position);
        float NpcRadius = 0.0f;
        unsigned int NpcId = It->Npc->SpaceObjId;
        Vector NpcCentre;
        pub::SpaceObj::GetRadius(NpcId, NpcRadius, NpcCentre);
        float const StartRadius = LaunchRadius * 1.25f;
        pub::SpaceObj::PosSelectionType const Selection =
            static_cast<pub::SpaceObj::PosSelectionType>(Kind);
        if (pub::SpaceObj::GetEmptyPos(Parms[0], Placement, StartRadius, NpcRadius, Selection,
                                       Position) != 0)
            continue;
        It->Npc->Relocate(FindSystemInfo(&Parms[0]), &Position, &Orientation);
        It->Npc->CancelDirectives();
        unsigned int Arrival[3];
        Arrival[0] = It->Npc->SpaceObjId;
        Arrival[1] = Parms[0];
        Arrival[2] = Parms[3];
        OnNpcSystemEnter(Arrival);
        It->State = 0;
    }
}

// FUN_06f145d0.
void FMissionObject::RespawnMissionObjects(bool bKeepHitPoints)
{
    std::list<FMissionShipGroup>::iterator GroupIt = RespawnShips.begin();
    for (; GroupIt != RespawnShips.end(); ++GroupIt)
    {
        if (!bKeepHitPoints)
        {
            std::vector<FMissionShip>::iterator SlotIt = GroupIt->Ships.begin();
            for (; SlotIt != GroupIt->Ships.end(); ++SlotIt)
                SlotIt->HitPoints = -1;
        }
        SpawnMissionShipGroup(&*GroupIt);
    }
    // Through the first matching [MsnRandEnc]: SpawnFormation (FUN_06efba30, dock target 0).
    std::list<FMissionRandEncRespawn>::iterator RespawnIt = RandEncRespawns.begin();
    for (; RespawnIt != RandEncRespawns.end(); ++RespawnIt)
    {
        std::list<FMissionRandEnc>::iterator EncIt = RandEncs.begin();
        for (; EncIt != RandEncs.end(); ++EncIt)
        {
            if (RespawnIt->EncounterId.Id != EncIt->NicknameId.Id)
                continue;
            EncIt->Params->SpawnFormation(&RespawnIt->Placement, RespawnIt->Count,
                                          &RespawnIt->Directives.Items, &RespawnIt->Choices,
                                          &RespawnIt->Names, RespawnIt->Priority, 0);
            break;
        }
    }
    // The first slot's NPC gets its guide, or else its directives at the NPC's priority.
    for (GroupIt = RespawnShips.begin(); GroupIt != RespawnShips.end(); ++GroupIt)
    {
        if (GroupIt->Ships.empty())
            continue;
        FMissionShip& First = GroupIt->Ships.front();
        FContentNpcShip* Npc = FindNpcByName(&First.Nickname);
        if (Npc == nullptr)
            continue;
        if (First.Guide.Pointer != nullptr)
            Npc->SetGuide(First.Guide.Pointer, true, true);
        else
            Npc->SetDirectives(&First.Directives.Items, Npc->GetPriority());
    }
    RespawnShips.clear();
    RandEncRespawns.clear();
}

// FUN_06f148f0 (Accept = populator vtbl +0xc, Spawn = params vtbl +0x10).
void FMissionObject::NotifyRandEncs(pub::Controller::EMessage MessageId, void const* MessageParms)
{
    if (FindContentPlayerState(PlayerId) == nullptr)
        return;
    if (PlayerState->ShipObject == nullptr)
        return;
    std::list<FMissionRandEnc>::iterator It = RandEncs.begin();
    for (; It != RandEncs.end(); ++It)
    {
        if (It->bEnabled == 0)
            continue;
        if (!It->Populator->Accept(It->Params, MessageId, MessageParms))
            continue;
        It->NpcCount = 0;
        It->Params->Spawn(true);
        It->bEnabled = 0;
    }
}

// FUN_06f153a0. A record whose arm list is empty is cleared again (FUN_06f1dcd0) and its
// "%s_frm%d" counter reset.
void FMissionObject::RemoveRandEncMember(unsigned int const* ObjId)
{
    std::list<FMissionRandEnc>::iterator EncIt = RandEncs.begin();
    for (; EncIt != RandEncs.end(); ++EncIt)
    {
        std::list<FMissionRandEncArm>::iterator ArmIt = EncIt->Arms.begin();
        for (; ArmIt != EncIt->Arms.end(); ++ArmIt)
        {
            std::list<unsigned int>::iterator MemberIt =
                std::find(ArmIt->MemberIds.begin(), ArmIt->MemberIds.end(), *ObjId);
            if (MemberIt == ArmIt->MemberIds.end())
                continue;
            ArmIt->MemberIds.erase(MemberIt);
            if (ArmIt->MemberIds.empty())
                EncIt->Arms.erase(ArmIt);
            break;
        }
        if (EncIt->Arms.empty())
        {
            EncIt->Arms.clear();
            EncIt->FormationIndex = 0;
        }
    }
}

// The stack template in FUN_06f172f0. The handles start null.
FMissionRandEnc::FMissionRandEnc()
    : bEnabled(0)
    , FormationIndex(0)
    , NpcCount(0)
{
    Nickname.Length = 0;
    Nickname.Data[0] = '\0';
    NicknameId.clear();
}

// FUN_06f21c50 and FUN_06ed3710 are the implicit copy constructor and destructor
// (MissionObject.h).

// FUN_06f14e80: the encounter whose nickname id is in the NPC's label list (+0x17c) takes it
// (NPC +0x1bc = 24.0f, DAT_06fb8588). Once all of the encounter's ships exist, an encounter
// with an arrival object (Params +0x8c) regroups them into a formation picked by weight, then
// message 0x100c (Cnd_EncLaunched) goes out.
void FMissionObject::AttachRandEncNpc(void const* MessageParms)
{
    FContentNpcShip* Npc = *static_cast<FContentNpcShip* const*>(MessageParms);
    std::list<FMissionRandEnc>::iterator EncIt = RandEncs.begin();
    for (; EncIt != RandEncs.end(); ++EncIt)
    {
        bool bMember = false;
        std::list<unsigned int>::iterator IdIt = Npc->LabelIds.begin();
        for (; IdIt != Npc->LabelIds.end(); ++IdIt)
        {
            if (*IdIt == EncIt->NicknameId.Id)
            {
                bMember = true;
                break;
            }
        }
        if (bMember)
            break;
    }
    if (EncIt == RandEncs.end())
        return;

    Npc->Longevity = 24.0f;
    std::list<FMissionRandEncArm>::iterator ArmIt = EncIt->Arms.begin();
    for (; ArmIt != EncIt->Arms.end(); ++ArmIt)
    {
        if (ArmIt->FormationRef == Npc->FormationId)
            break;
    }
    if (ArmIt != EncIt->Arms.end())
    {
        Npc->LabelIds.push_back(ArmIt->Id);
        ArmIt->MemberIds.push_back(Npc->SpaceObjId);
    }
    else
    {
        char FormationName[0x40];
        sprintf(FormationName, "%s_frm%d", EncIt->Nickname.Data, EncIt->FormationIndex);
        ID_String NameA;
        ID_String NameB;
        NameA.clear();
        NameB.clear();
        FMissionRandEncArm Arm;
        NameA.Id = CreateID(FormationName);
        NameB.Id = CreateID(FormationName);
        Arm.Name = NameA;
        Arm.FormationRef = Npc->FormationId;
        Arm.Id = NameB.Id;
        Arm.MemberIds.push_back(Npc->SpaceObjId);
        Npc->LabelIds.push_back(NameB.Id);
        EncIt->Arms.push_back(Arm);
        EncIt->FormationIndex = EncIt->FormationIndex + 1;
    }
    EncIt->NpcCount = EncIt->NpcCount + 1;
    if (static_cast<unsigned int>(EncIt->Params->ShipsSpawned) != EncIt->NpcCount)
        return;

    if ((EncIt->Params->DockTarget & 0x3fffffff) != 0)
    {
        // Every arm's ships; a ship missing from +0xa4 is appended as null.
        std::list<FContentShipBase*> Members;
        std::list<FMissionRandEncArm>::iterator Arm = EncIt->Arms.begin();
        for (; Arm != EncIt->Arms.end(); ++Arm)
        {
            std::multimap<unsigned int, unsigned int>::iterator Member =
                FormationMembers.lower_bound(Arm->Id);
            for (; Member != FormationMembers.upper_bound(Arm->Id); ++Member)
            {
                std::map<unsigned int, FContentNpcShip*>::iterator Object =
                    NpcObjects.find(Member->second);
                FContentNpcShip* Ship = Object == NpcObjects.end() ? nullptr : Object->second;
                Members.push_back(Ship);
            }
        }

        if (Members.empty())
        {
            RemoveFromFormation(Npc);
        }
        else
        {
            // The weights (value +0x24) summed as floats.
            std::list<FRandEncFormation>& Formations = EncIt->Params->Formations;
            float Total = 0.0f;
            std::list<FRandEncFormation>::iterator Pack = Formations.begin();
            for (; Pack != Formations.end(); ++Pack)
                Total = static_cast<float>(static_cast<double>(Total) + Pack->Weight);
            // A zero total picks nothing. Otherwise the first pack whose running float sum
            // reaches the roll (rand() * 2^-15, DAT_06fb4448, times the total).
            FRandEncFormation* Picked = nullptr;
            if (Total != 0.0f)
            {
                float const Roll = static_cast<float>(static_cast<double>(rand()) *
                                                      (1.0f / 32768.0f) * Total);
                float Sum = 0.0f;
                for (Pack = Formations.begin(); Pack != Formations.end(); ++Pack)
                {
                    Sum = static_cast<float>(static_cast<double>(Sum) + Pack->Weight);
                    if (Roll <= Sum)
                    {
                        Picked = &*Pack;
                        break;
                    }
                }
            }
            // Deviation: with nothing picked retail passes the list sentinel's value slot (an
            // indeterminate FFormation*); the port passes null.
            FormFormation(&Members, Picked != nullptr ? Picked->Formation : nullptr, false);
        }
    }

    // Message 0x100c (Cnd_EncLaunched) to this controller; the ship count starts again.
    std::list<unsigned int> Launched;
    Launched.push_back(EncIt->NicknameId.Id);
    pub::Controller::_SendMessage(ControllerId, pub::Controller::EMessage::EncounterLaunched, &Launched);
    EncIt->NpcCount = 0;
}

// FUN_06f19c70.
bool FMissionSolar::HasLabel(unsigned int const* LabelId) const
{
    std::list<unsigned int>::const_iterator It = LabelIds.begin();
    for (; It != LabelIds.end(); ++It)
    {
        if (*It == *LabelId)
            return true;
    }
    return false;
}

// FUN_06f19ca0.
bool FMissionLoot::HasLabel(unsigned int const* LabelId) const
{
    std::list<unsigned int>::const_iterator It = LabelIds.begin();
    for (; It != LabelIds.end(); ++It)
    {
        if (*It == *LabelId)
            return true;
    }
    return false;
}

// FUN_06f19b80. All three lookups run first; then the NPC's, the solar's and the loot's label
// lists are tested in that order.
bool FMissionObject::HasObjectLabel(unsigned int ObjId, unsigned int const* LabelId)
{
    std::map<unsigned int, FContentNpcShip*>::iterator NpcIt = NpcObjects.find(ObjId);
    FContentNpcShip* Npc = NpcIt == NpcObjects.end() ? nullptr : NpcIt->second;

    FMissionSolar* Solar = nullptr;
    std::list<FMissionSolar>::iterator SolarIt = MissionSolars.begin();
    for (; SolarIt != MissionSolars.end(); ++SolarIt)
    {
        if (SolarIt->SpaceObjId == ObjId)
        {
            Solar = &*SolarIt;
            break;
        }
    }

    FMissionLoot* Loot = nullptr;
    std::list<FMissionLoot>::iterator LootIt = MissionLoots.begin();
    for (; LootIt != MissionLoots.end(); ++LootIt)
    {
        if (LootIt->SpaceObjId == ObjId)
        {
            Loot = &*LootIt;
            break;
        }
    }

    if (Npc != nullptr)
    {
        std::list<unsigned int>::iterator IdIt = Npc->LabelIds.begin();
        for (; IdIt != Npc->LabelIds.end(); ++IdIt)
        {
            if (*IdIt == *LabelId)
                return true;
        }
    }
    if (Solar != nullptr && Solar->HasLabel(LabelId))
        return true;
    if (Loot != nullptr && Loot->HasLabel(LabelId))
        return true;
    return false;
}

// FUN_06f15df0.
bool FMissionObject::IsPlayerDead(unsigned int InPlayerId)
{
    std::vector<unsigned int>::iterator It = DeadPlayerIds.begin();
    for (; It != DeadPlayerIds.end(); ++It)
    {
        if (*It == InPlayerId)
            break;
    }
    return It != DeadPlayerIds.end();
}

// DAT_06fd070c: TU38's 3DMathEngine, created on first use (FUN_06fa8fa0) by the orientation
// keys of FUN_06f172f0 / FUN_06f18eb0 and by FUN_06f15e20.
static I3DMathEngine* GMissionMathEngine = nullptr;

namespace
{
    // The 9 dwords of a rotation copied as one block.
    void CopyRotation(FMatrix* Dest, Matrix const* Source)
    {
        std::memcpy(Dest, Source, sizeof(FMatrix));
    }
}

// FUN_06f15e20.
void FMissionObject::SpawnMissionShipGroup(FMissionShipGroup* Group)
{
    FTransform Placement;
    Placement.Rotation = Group->Orientation;
    Placement.Translation.X = Group->Position.x;
    Placement.Translation.Y = Group->Position.y;
    Placement.Translation.Z = Group->Position.z;
    // Set when the record's formation is unknown; nothing is created then.
    bool bNoFormation = false;
    std::list<FContentNpcShipParams> Blocks;

    std::vector<FMissionShip>::iterator Ship = Group->Ships.begin();
    for (; Ship != Group->Ships.end(); ++Ship)
    {
        // A default block (FUN_06f33500).
        Blocks.push_back(FContentNpcShipParams());
        FContentNpcShipParams* Params = &Blocks.back();

        FNpcNameParams Name;

        std::map<unsigned int, FMsnNpc>::iterator NpcIt = MissionNpcs.find(Ship->NpcId.Id);
        if (NpcIt == MissionNpcs.end())
        {
            // No such [NPC]: the new block is dropped again (the size test is retail's).
            if (!Blocks.empty())
                Blocks.pop_back();
            continue;
        }
        FMsnNpc* Npc = &NpcIt->second;

        // The reputation handle of the block.
        if (Ship->bRandomName != 0)
        {
            // The gender from the [NPC] voice's roles (1, then 2), else from the faction
            // (FUN_06f319b0).
            if (Npc->Voice == 0)
            {
                PickNpcNameGender(Npc->Affiliation, &Name);
            }
            else
            {
                FVoiceProperties* Voice = FindVoiceProperties(&Npc->Voice);  // FUN_06fa6e50
                if (Voice == nullptr)
                    PickNpcNameGender(Npc->Affiliation, &Name);
                else if (Voice->SupportsRoles(1))                             // FUN_06fa6e30
                    Name.Gender = 1;
                else if (Voice->SupportsRoles(2))
                    Name.Gender = 2;
                else
                    PickNpcNameGender(Npc->Affiliation, &Name);
            }
            // BuildNpcName = FUN_06f319d0, CreateNpcNameReputation = FUN_06f31910.
            FNpcNameRequest Request;
            BuildNpcName(Npc->Affiliation, &Request, &Name, false);
            if (Params->ReputationHandle != nullptr)
            {
                Params->ReputationHandle->Release();
                Params->ReputationHandle = nullptr;
            }
            CreateNpcNameReputation(&Name, &Params->ReputationHandle);
        }
        else
        {
            // From the [NPC] block (FUN_06f31d40).
            if (Params->ReputationHandle != nullptr)
            {
                Params->ReputationHandle->Release();
                Params->ReputationHandle = nullptr;
            }
            CreateMsnNpcReputation(Npc, &Params->ReputationHandle);
        }

        Params->Nickname = Ship->Nickname.Data;

        // The npc_ship_arch from the mission's own table (+0x198, FUN_06f32480 +
        // FUN_06f348f0), else from npcships.ini (FUN_06f34a40).
        FNpcShipArch* Arch = FindNpcShipArch(&Npc->NpcShipArch.Id, &NpcShipArchs);
        if (Arch != nullptr)
        {
            ApplyNpcShipArch(Params, Arch, nullptr, nullptr);
        }
        else if (!ApplyNpcShipArchById(Params, &Npc->NpcShipArch.Id, nullptr, nullptr))
        {
            // Unknown arch: the block is erased and the ship skipped.
            Blocks.pop_back();
            continue;
        }

        Params->Voice = Npc->Voice;
        Params->CommCostume.PartIds[0] = Npc->Appearance.PartIds[0];
        Params->CommCostume.PartIds[1] = Npc->Appearance.PartIds[1];
        Params->CommCostume.PartIds[2] = Npc->Appearance.PartIds[2];
        Params->CommCostume.PartIds[3] = Npc->Appearance.PartIds[3];
        Params->CommCostume.AccessoryCount = Npc->Appearance.AccessoryCount;
        for (int Index = 0; Index < static_cast<int>(Params->CommCostume.AccessoryCount); ++Index)
            Params->CommCostume.AccessoryIds[Index] = Npc->Appearance.AccessoryIds[Index];

        // No head or no voice: the faction's picks (costume FUN_06f4f430, voice FUN_06f4f620)
        // with the name's gender.
        if ((Npc->Appearance.PartIds[0] & 0x3fffffff) == 0 || (Npc->Voice & 0x3fffffff) == 0)
        {
            FFactionProps* Faction = GetFactionProps(&Npc->Affiliation);
            if (Faction != nullptr)
            {
                if ((Npc->Appearance.PartIds[0] & 0x3fffffff) == 0)
                    Faction->PickSpaceCostume(Name.Gender, &Params->CommCostume);
                if ((Npc->Voice & 0x3fffffff) == 0)
                    Faction->PickVoice(static_cast<unsigned int>(Name.Gender), &Params->Voice);
            }
        }

        FTransform Local;
        std::memset(&Local, 0, sizeof(Local));
        Local.Rotation.D[0][0] = 1.0f;
        Local.Rotation.D[1][1] = 1.0f;
        Local.Rotation.D[2][2] = 1.0f;
        if (Group->bRelPos == 0)
        {
            Local.Rotation = Group->Orientation;
            Local.Translation.X = Group->Position.x;
            Local.Translation.Y = Group->Position.y;
            Local.Translation.Z = Group->Position.z;
        }
        else
        {
            // The anchor: the player's ship or the NPC named by rel_pos; identity and zero when
            // there is none.
            FTransform Anchor;
            std::memset(&Anchor, 0, sizeof(Anchor));
            Anchor.Rotation.D[0][0] = 1.0f;
            Anchor.Rotation.D[1][1] = 1.0f;
            Anchor.Rotation.D[2][2] = 1.0f;
            FContentSpaceObj* AnchorObject = nullptr;
            if (_stricmp(Group->RelPosObj.Data, "Player") == 0)
            {
                if (FindContentPlayerState(PlayerId) != nullptr)
                    AnchorObject = PlayerState->ShipObject;
            }
            else
            {
                AnchorObject = FindNpcByName(&Group->RelPosObj);
            }
            if (AnchorObject != nullptr)
            {
                // FUN_06eb70f0 / FUN_06eb7130.
                Vector AnchorPosition;
                AnchorObject->GetPosition(&AnchorPosition);
                Anchor.Translation.X = AnchorPosition.x;
                Anchor.Translation.Y = AnchorPosition.y;
                Anchor.Translation.Z = AnchorPosition.z;
                Matrix AnchorOrientation;
                AnchorObject->GetOrientation(&AnchorOrientation);
                CopyRotation(&Anchor.Rotation, &AnchorOrientation);
            }

            if (Group->bOrientation == 0)
            {
                // FUN_06f243d0: placed at the heading / range, facing away from the anchor.
                PlaceFacingAnchor(&Anchor, &Group->RelPosHeading, &Group->RelPosRange, &Local);
            }
            else
            {
                // The position only (TransformXform is slot +0x28); the rotation stays
                // identity.
                FVector Offset;
                Offset.X = static_cast<float>(std::cos(static_cast<double>(Group->RelPosHeading)) *
                                              Group->RelPosRange);
                Offset.Y = 0.0f;
                Offset.Z = static_cast<float>(-(std::sin(static_cast<double>(Group->RelPosHeading)) *
                                                Group->RelPosRange));
                if (GMissionMathEngine == nullptr)
                    GMissionMathEngine = ContentAcquire3DMathEngine();
                FVector Position;
                GMissionMathEngine->TransformXform(Position, Anchor, Offset);
                Local.Translation = Position;
            }
        }

        // A ship with a positive radius (DAT_06fb444c) is moved to an empty spot when its
        // position is taken.
        if (FindContentPlayerState(PlayerId) != nullptr)
        {
            FSystemInfo* PlayerSystem = PlayerState->GetSystemRecord();
            if (PlayerSystem != nullptr && Ship->Radius > 0.0f)
            {
                unsigned int SystemId = PlayerSystem->SystemId;
                Vector Position;
                Position.x = Local.Translation.X;
                Position.y = Local.Translation.Y;
                Position.z = Local.Translation.Z;
                bool bEmpty = true;
                pub::SpaceObj::IsPosEmpty(SystemId, Position, Ship->Radius, bEmpty);
                if (!bEmpty)
                {
                    Transform Base;
                    std::memcpy(&Base.Rotation, &Local.Rotation, sizeof(FMatrix));
                    Base.Translation = Position;
                    pub::SpaceObj::PosSelectionType const Selection = pub::SpaceObj::POS_SEL_3D_VOLUME;
                    pub::SpaceObj::GetEmptyPos(SystemId, Base, Ship->Radius, Ship->Radius, Selection,
                                               Position);
                    Local.Translation.X = Position.x;
                    Local.Translation.Y = Position.y;
                    Local.Translation.Z = Position.z;
                }
            }
        }

        // The first ship's placement becomes the formation's.
        if (Ship == Group->Ships.begin())
            Placement = Local;

        Params->Position.x = Local.Translation.X;
        Params->Position.y = Local.Translation.Y;
        Params->Position.z = Local.Translation.Z;
        std::memcpy(&Params->Orientation, &Local.Rotation, sizeof(FMatrix));
        Params->CreationType = 0;
        Params->ControllerId = ControllerId;
        Params->LabelIds = Ship->LabelIds;
        Params->DockIndex = Group->ArrivalDockIndex;
        // Retail assigns the vector inline (same 8-byte pairs).
        Params->Cargo.clear();
        std::vector<FMissionShipCargo>::iterator Cargo = Ship->Cargo.begin();
        for (; Cargo != Ship->Cargo.end(); ++Cargo)
        {
            pub::SpaceObj::CargoDesc Desc;
            Desc.GoodId = Cargo->GoodId;
            Desc.Count = static_cast<unsigned int>(Cargo->Count);
            Params->Cargo.push_back(Desc);
        }
        Params->Priority = Ship->Priority;
        Params->HitPoints = Ship->HitPoints;

        // The first mission ship slot of that nickname takes this slot's jumper flag.
        FMissionShip* Named = FindMissionShip(&Params->Nickname);
        if (Named != nullptr)
            Named->bJumper = Ship->bJumper;

        // "init_objectives": the [ObjList]'s directives (+0x100; FUN_06f20230 inlined).
        std::map<unsigned int, FMissionDirectiveList>::iterator ObjList =
            ObjLists.find(Ship->InitObjectives.Id);
        if (ObjList != ObjLists.end())
            Params->Directives = ObjList->second;

        // "init_metabehavior": the directives are dropped and the [MetaBehavior]'s first
        // object becomes the block's guide description. Retail reads the first node's value
        // without an empty test.
        std::map<unsigned int, FMissionMetaBehaviorList>::iterator Meta =
            MetaBehaviors.find(Ship->InitMetaBehavior.Id);
        if (Meta != MetaBehaviors.end())
        {
            Params->Directives.Clear();                                         // FUN_06ed2a00
            FGuideDescBase* Desc = Meta->second.Items.front();
            if (Params->GuideDesc != Desc)
            {
                if (Params->GuideDesc != nullptr)
                {
                    Params->GuideDesc->Release();
                    Params->GuideDesc = nullptr;
                }
                Params->GuideDesc = Desc;
                if (Desc != nullptr)
                    Desc->AddRef();
            }
        }
    }

    // A named formation lays the blocks out around the placement (FUN_06f66c80; retail looks
    // the formation up twice); an unknown one stops the creation.
    if (!Group->Formation.IsEmpty())
    {
        if (FindFormation(&Group->Formation.Id) != nullptr)
            PlaceFormationBlocks(&Blocks, FindFormation(&Group->Formation.Id), &Placement);
        else
            bNoFormation = true;
    }

    // The dock target: the record's +0xdc, or the "arrival_obj" solar.
    unsigned int DockTarget = Group->ArrivalObjId;
    if (Group->ArrivalObj.Length != 0)
        DockTarget = GetSolarId(&Group->ArrivalObj);

    // CreateNpcShips (TU58 FUN_06f73810) in the record's system, else in the player's.
    if (Group->System != nullptr)
    {
        if (!bNoFormation)
            Group->System->CreateNpcShips(&Blocks, DockTarget, nullptr);
    }
    else if (FindContentPlayerState(PlayerId) != nullptr)
    {
        FSystemInfo* PlayerSystem = PlayerState->GetSystemRecord();
        if (PlayerSystem != nullptr && !bNoFormation)
            PlayerSystem->CreateNpcShips(&Blocks, DockTarget, nullptr);
    }
}

// FUN_06f169a0.
bool FMissionObject::SpawnMissionSolarRecord(FMissionSolar* Solar)
{
    if (FindContentPlayerState(PlayerId) == nullptr)
        return false;
    FSystemInfo* System = PlayerState->GetSystemRecord();
    if (System == nullptr)
        return false;

    // The SolarInfo template.
    pub::SpaceObj::SolarInfo Info;
    std::memset(&Info, 0, sizeof(Info));
    Info.Orientation.E[0][0] = 1.0f;
    Info.Orientation.E[1][1] = 1.0f;
    Info.Orientation.E[2][2] = 1.0f;
    Info.VibeGroupId = -1;
    Info.VisitValue = 1;
    Info.HitPts = -1;

    // The record's id is its nickname id.
    FSystemInfo* PlayerSystem = PlayerState->GetSystemRecord();
    std::strcpy(Info.Nickname, Solar->Nickname.Data);
    Solar->SpaceObjId = pub::GetNicknameId(Info.Nickname);
    Info.Field_0x00 = ControllerId;
    Info.SolarArchID = Solar->ArchetypeId;
    Info.SystemID = PlayerSystem != nullptr ? PlayerSystem->SystemId : 0;

    // A solar with a positive radius whose record position is taken moves to an empty spot.
    // Retail quirk: the gender slot below shares its stack dword with System, and the
    // GetEmptyPos branch stores its PosSelectionType 3 there. PickGender (FUN_06f4f7c0)
    // overwrites it only when the faction has voices, so otherwise PickSpaceCostume compares
    // this leftover (a heap pointer or 3) with the costume genders (0/1/2) and never matches.
    int GenderSlot = static_cast<int>(reinterpret_cast<uintptr_t>(System));
    Vector Position = Solar->Position;
    if (PlayerSystem != nullptr && Solar->Radius > 0.0f)
    {
        unsigned int const SystemId = System->SystemId;
        bool bEmpty = true;
        pub::SpaceObj::IsPosEmpty(SystemId, Solar->Position, Solar->Radius, bEmpty);
        if (!bEmpty)
        {
            Transform Base;
            std::memcpy(&Base.Rotation, &Solar->Orientation, sizeof(FMatrix));
            Base.Translation = Solar->Position;
            pub::SpaceObj::PosSelectionType const Selection = pub::SpaceObj::POS_SEL_3D_VOLUME;
            GenderSlot = static_cast<int>(Selection);
            pub::SpaceObj::GetEmptyPos(SystemId, Base, Solar->Radius, Solar->Radius, Selection,
                                       Position);
        }
    }

    Info.Position = Position;
    Info.LoadoutID = Solar->LoadoutId;
    Info.VibeGroupId = static_cast<int>(Solar->VibeGroupId);
    std::memcpy(&Info.Orientation, &Solar->Orientation, sizeof(FMatrix));
    Info.DockingBaseId = Solar->BaseId;
    Info.VisitValue = Solar->Visit;
    Info.CostumeHead = Solar->SpaceCostume.PartIds[0];
    Info.CostumeBody = Solar->SpaceCostume.PartIds[1];
    Info.CostumeLeftHand = Solar->SpaceCostume.PartIds[2];
    Info.CostumeRightHand = Solar->SpaceCostume.PartIds[3];
    Info.CostumeAccessoryCount = static_cast<int>(Solar->SpaceCostume.AccessoryCount);
    for (int Index = 0; Index < Info.CostumeAccessoryCount; ++Index)
        Info.CostumeAccessory[Index] = Solar->SpaceCostume.AccessoryIds[Index];
    Info.VoiceId = Solar->VoiceId;

    {
        FmtStr Empty;
        pub::Reputation::Alloc(Info.VibeGroupId, Solar->Name, Empty);
    }
    pub::Reputation::SetAffiliation(Info.VibeGroupId, Solar->Faction);

    // No head or no voice: the faction's picks, starting from GenderSlot (see above).
    if ((Solar->SpaceCostume.PartIds[0] & 0x3fffffff) == 0 || (Solar->VoiceId & 0x3fffffff) == 0)
    {
        FFactionProps* Faction = GetFactionProps(&Solar->Faction);
        if (Faction != nullptr)
        {
            int Gender = GenderSlot;
            Faction->PickGender(&Gender);
            if ((Solar->SpaceCostume.PartIds[0] & 0x3fffffff) == 0)
            {
                // Retail passes &Info+0x40, which is laid out as a Costume; the pick writes it
                // only on success, so the copy round trip leaves Info as it was otherwise.
                Costume InfoCostume;
                InfoCostume.PartIds[0] = Info.CostumeHead;
                InfoCostume.PartIds[1] = Info.CostumeBody;
                InfoCostume.PartIds[2] = Info.CostumeLeftHand;
                InfoCostume.PartIds[3] = Info.CostumeRightHand;
                for (int Index = 0; Index < 8; ++Index)
                    InfoCostume.AccessoryIds[Index] = Info.CostumeAccessory[Index];
                InfoCostume.AccessoryCount = static_cast<unsigned int>(Info.CostumeAccessoryCount);
                Faction->PickSpaceCostume(Gender, &InfoCostume);
                Info.CostumeHead = InfoCostume.PartIds[0];
                Info.CostumeBody = InfoCostume.PartIds[1];
                Info.CostumeLeftHand = InfoCostume.PartIds[2];
                Info.CostumeRightHand = InfoCostume.PartIds[3];
                Info.CostumeAccessoryCount = static_cast<int>(InfoCostume.AccessoryCount);
                for (int Index = 0; Index < 8; ++Index)
                    Info.CostumeAccessory[Index] = InfoCostume.AccessoryIds[Index];
            }
            if ((Solar->VoiceId & 0x3fffffff) == 0)
                Faction->PickVoice(static_cast<unsigned int>(Gender), &Info.VoiceId);
        }
    }

    if (pub::SpaceObj::CreateSolar(Solar->SpaceObjId, Info) != 0)
        return false;

    pub::AI::SetPersonalityParams Op;
    Op.StateGraphId = pub::StateGraph::get_state_graph("NOTHING", pub::StateGraph::LEADER);
    Op.DirectiveCallbackObject = nullptr;
    Op.ContentCallbackObject = nullptr;
    if (Solar->bPilot != 0)
    {
        Op.bSetPersonality = 1;
        Op.Value = Solar->Personality;
    }
    pub::AI::SubmitState(Solar->SpaceObjId, &Op);
    Solar->bSpawned = 1;

    std::list<unsigned int>::iterator Label = Solar->LabelIds.begin();
    for (; Label != Solar->LabelIds.end(); ++Label)
        FormationMembers.insert(std::make_pair(*Label, Solar->SpaceObjId));

    // [Invulnerables] rows of this nickname (strcmp).
    std::list<FMissionInvulnerable>::iterator InvIt = Invulnerables.begin();
    for (; InvIt != Invulnerables.end(); ++InvIt)
    {
        if (std::strcmp(Solar->Nickname.Data, InvIt->Nickname.Data) != 0)
            continue;
        pub::SpaceObj::SetInvincible(Solar->SpaceObjId, true, InvIt->bPlayerCanDamage != 0, InvIt->MaxHpLoss);
    }

    // [Decloaked] rows of this nickname.
    std::list<TString<0x30>>::iterator DecIt = DecloakedNames.begin();
    for (; DecIt != DecloakedNames.end(); ++DecIt)
    {
        if (std::strcmp(Solar->Nickname.Data, DecIt->Data) != 0)
            continue;
        pub::SpaceObj::EQInfo EquipInfo;
        std::memset(&EquipInfo, 0, sizeof(EquipInfo));
        EquipInfo.Type = 2;
        EquipInfo.EquipId = 0;
        pub::SpaceObj::ActivateEquipment(Solar->SpaceObjId, &EquipInfo);
    }

    // A [MarkObj] name equal to the nickname.
    std::list<TString<0x30>>::iterator MarkIt = MarkObjNames.begin();
    for (; MarkIt != MarkObjNames.end(); ++MarkIt)
    {
        if (std::strcmp(MarkIt->Data, Solar->Nickname.Data) == 0)
            break;
    }
    if (MarkIt != MarkObjNames.end())
        pub::Player::MarkObj(PlayerId, Solar->SpaceObjId, 1);

    // AddMissionObject = FUN_06f743e0.
    PlayerSystem->AddMissionObject(Solar->SpaceObjId);
    return true;
}

// FUN_06f16fa0.
void FMissionObject::SpawnMissionLoot(FMissionLoot* Loot)
{
    if (FindContentPlayerState(PlayerId) == nullptr)
        return;
    if (PlayerState->GetSystemRecord() == nullptr)
        return;

    Loot->Info.SystemID = PlayerState->GetSystemRecord()->SystemId;
    Loot->Info.bCanAiTractor = 0;
    Loot->Info.bMission = Loot->bCannotJettison;

    int Result;
    if (Loot->bRelPos != 0)
    {
        // The anchor (GetLabelObjectId inlined); none: nothing is created.
        unsigned int Anchor = 0;
        if (FindContentPlayerState(PlayerId) != nullptr && PlayerState->ShipObject != nullptr)
        {
            Anchor = GetLabelShipId(&Loot->RelPosObj);
            if ((Anchor & 0x3fffffff) == 0)
            {
                Anchor = GetLootId(&Loot->RelPosObj);
                if ((Anchor & 0x3fffffff) == 0)
                    Anchor = GetSolarId(&Loot->RelPosObj);
            }
        }
        if ((Anchor & 0x3fffffff) == 0)
            return;
        Vector AnchorPosition;
        Matrix AnchorOrientation;
        if (pub::SpaceObj::GetLocation(Anchor, AnchorPosition, AnchorOrientation) != 0)
            return;
        Loot->Info.Position.x = AnchorPosition.x + Loot->RelPosOffset.x;
        Loot->Info.Position.y = AnchorPosition.y + Loot->RelPosOffset.y;
        Loot->Info.Position.z = AnchorPosition.z + Loot->RelPosOffset.z;
        Result = pub::SpaceObj::CreateLoot(Loot->SpaceObjId, Loot->Info);
    }
    else
    {
        Result = pub::SpaceObj::CreateLoot(Loot->SpaceObjId, Loot->Info);
    }
    if (Result != 0)
        return;

    Loot->bSpawned = 1;
    std::list<unsigned int>::iterator Label = Loot->LabelIds.begin();
    for (; Label != Loot->LabelIds.end(); ++Label)
        FormationMembers.insert(std::make_pair(*Label, Loot->SpaceObjId));

    std::list<FMissionInvulnerable>::iterator InvIt = Invulnerables.begin();
    for (; InvIt != Invulnerables.end(); ++InvIt)
    {
        if (std::strcmp(Loot->Nickname.Data, InvIt->Nickname.Data) != 0)
            continue;
        pub::SpaceObj::SetInvincible(Loot->SpaceObjId, true, InvIt->bPlayerCanDamage != 0, InvIt->MaxHpLoss);
    }
}

// FUN_06f171e0.
bool FMissionObject::SpawnMissionSolar(TString<0x30> const* Name, FTransform const* Placement)
{
    bool bResult = false;
    std::list<FMissionSolar>::iterator Solar = MissionSolars.begin();
    for (; Solar != MissionSolars.end(); ++Solar)
    {
        // Every record of that nickname, not only the first.
        if (_stricmp(Solar->Nickname.Data, Name->Data) != 0)
            continue;
        // A given placement replaces the record's transform for the spawn only.
        Vector const SavedPosition = Solar->Position;
        FMatrix const SavedOrientation = Solar->Orientation;
        if (Placement != nullptr)
        {
            Solar->Position.x = Placement->Translation.X;
            Solar->Position.y = Placement->Translation.Y;
            Solar->Position.z = Placement->Translation.Z;
            Solar->Orientation = Placement->Rotation;
        }
        bResult = SpawnMissionSolarRecord(&*Solar);
        Solar->Position = SavedPosition;
        Solar->Orientation = SavedOrientation;
    }
    return bResult;
}

// FUN_06f171a0 (TU58 FUN_06f746f0). The cached player state is not null-tested.
void FMissionObject::DestroyMissionObjects()
{
    std::list<FMissionSolar>::iterator It = MissionSolars.begin();
    for (; It != MissionSolars.end(); ++It)
        It->bSpawned = 0;
    FSystemInfo* System = PlayerState->GetSystemRecord();
    if (System != nullptr)
        System->DestroyMissionObjects();
}

// The prefix of FUN_06f14780.
void FMissionObject::RegisterNpc(void const* MessageParms)
{
    FContentNpcShip* Npc = *static_cast<FContentNpcShip* const*>(MessageParms);
    unsigned int NpcId = Npc->SpaceObjId;
    NpcObjects.emplace(NpcId, Npc);

    std::list<FMissionInvulnerable>::iterator InvIt = Invulnerables.begin();
    for (; InvIt != Invulnerables.end(); ++InvIt)
    {
        if (std::strcmp(Npc->Nickname.Data, InvIt->Nickname.Data) != 0)
            continue;
        pub::SpaceObj::SetInvincible(NpcId, true, InvIt->bPlayerCanDamage != 0, InvIt->MaxHpLoss);
    }

    std::list<TString<0x30>>::iterator DecIt = DecloakedNames.begin();
    for (; DecIt != DecloakedNames.end(); ++DecIt)
    {
        if (std::strcmp(Npc->Nickname.Data, DecIt->Data) != 0)
            continue;
        pub::SpaceObj::EQInfo EquipInfo;
        std::memset(&EquipInfo, 0, sizeof(EquipInfo));
        EquipInfo.Type = 2;
        EquipInfo.EquipId = 0;
        pub::SpaceObj::ActivateEquipment(NpcId, &EquipInfo);
    }
}

// ---------------------------------------------------------------------------------------
// LoadMission (slot 25, FUN_06f172f0) and its records
// ---------------------------------------------------------------------------------------

// GMissionMathEngine (DAT_06fd070c) is defined above SpawnMissionShipGroup.

namespace
{
    // FmtStr::destroy, then its six dwords zeroed.
    void ResetMissionFmtStr(FmtStr& Text)
    {
        Text.destroy();
        Text.Head = nullptr;
        Text.MadLibId = 0;
        for (int Index = 0; Index < FmtStr::KindCount; ++Index)
            Text.ValueIndex[Index] = 0;
    }

    // The orientation keys of FUN_06f172f0 and FUN_06f18eb0: values read in the order 2, 1,
    // 0, 3 (W v0, X v1, Y v2, Z v3), scaled by 1 / FUN_06f18be0 (x87 sum W^2 + X^2 + Y^2 + Z^2)
    // with each product stored as float.
    void ReadMissionOrientation(INI_Reader* Reader, FMatrix* OutMatrix)
    {
        float const Y = Reader->get_value_float(2);
        float const X = Reader->get_value_float(1);
        float const W = Reader->get_value_float(0);
        float const Z = Reader->get_value_float(3);
        double const Length = std::sqrt(static_cast<double>(W) * W + static_cast<double>(X) * X +
                                        static_cast<double>(Y) * Y + static_cast<double>(Z) * Z);
        double const Inverse = 1.0 / Length;
        FQuaternion Quat;
        Quat.W = static_cast<float>(W * Inverse);
        Quat.X = static_cast<float>(X * Inverse);
        Quat.Y = static_cast<float>(Y * Inverse);
        Quat.Z = static_cast<float>(Z * Inverse);
        if (GMissionMathEngine == nullptr)
            GMissionMathEngine = ContentAcquire3DMathEngine();
        GMissionMathEngine->QuaternionToMatrix(*OutMatrix, Quat);
    }

    // [MsnLoot] values, written into the list's new last record.
    void LoadMissionLootValues(INI_Reader* Reader, FMissionLoot* Loot)
    {
        while (Reader->read_value())
        {
            if (Reader->is_value("nickname"))
            {
                Loot->Nickname = Reader->get_value_string(0);
                Loot->LabelIds.push_back(CreateID(Loot->Nickname.Data));
            }
            else if (Reader->is_value("archetype"))
            {
                TString<0x40> Name;
                Name = Reader->get_value_string(0);
                pub::GetEquipmentID(Loot->Info.EquipArchID, Name.Data);
            }
            else if (Reader->is_value("string_id"))
            {
                Loot->Info.NameId = static_cast<unsigned int>(Reader->get_value_int(0));
            }
            else if (Reader->is_value("position"))
            {
                Loot->Info.Position.x = Reader->get_value_float(0);
                Loot->Info.Position.y = Reader->get_value_float(1);
                Loot->Info.Position.z = Reader->get_value_float(2);
            }
            else if (Reader->is_value("rel_pos_obj"))
            {
                Loot->RelPosObj = Reader->get_value_string(0);
                Loot->bRelPos = 1;
            }
            else if (Reader->is_value("rel_pos_offset"))
            {
                Loot->RelPosOffset.x = Reader->get_value_float(0);
                Loot->RelPosOffset.y = Reader->get_value_float(1);
                Loot->RelPosOffset.z = Reader->get_value_float(2);
            }
            else if (Reader->is_value("orientation"))
            {
                FMatrix Rotation;
                ReadMissionOrientation(Reader, &Rotation);
                std::memcpy(&Loot->Info.Orientation, &Rotation, sizeof(Rotation));
            }
            else if (Reader->is_value("velocity"))
            {
                Loot->Info.Velocity.x = Reader->get_value_float(0);
                Loot->Info.Velocity.y = Reader->get_value_float(1);
                Loot->Info.Velocity.z = Reader->get_value_float(2);
            }
            else if (Reader->is_value("angular_velocity"))
            {
                Loot->Info.AngularVelocity.x = Reader->get_value_float(0);
                Loot->Info.AngularVelocity.y = Reader->get_value_float(1);
                Loot->Info.AngularVelocity.z = Reader->get_value_float(2);
            }
            else if (Reader->is_value("equip_amount"))
            {
                Loot->Info.Units = static_cast<unsigned int>(Reader->get_value_int(0));
            }
            else if (Reader->is_value("health"))
            {
                Loot->Info.HitPtsScale = Reader->get_value_float(0);
            }
            else if (Reader->is_value("label"))
            {
                Loot->LabelIds.push_back(CreateID(Reader->get_value_string(0)));
            }
            else if (Reader->is_value("can_jettison"))
            {
                Loot->bCannotJettison = Reader->get_value_bool(0) ? 0 : 1;
            }
        }
    }

    // The [MsnSolar] orientation key inlines its own normalisation, which differs in x87
    // order: the sum runs W^2 + Z^2 + Y^2 + X^2 and 1 / length is stored as float before the
    // products.
    void ReadMissionSolarOrientation(INI_Reader* Reader, FMatrix* OutMatrix)
    {
        float const Y = Reader->get_value_float(2);
        float const X = Reader->get_value_float(1);
        float const W = Reader->get_value_float(0);
        float const Z = Reader->get_value_float(3);
        double const Sum = static_cast<double>(W) * W + static_cast<double>(Z) * Z +
                           static_cast<double>(Y) * Y + static_cast<double>(X) * X;
        float const Inverse = static_cast<float>(1.0 / std::sqrt(Sum));
        FQuaternion Quat;
        Quat.W = static_cast<float>(static_cast<double>(W) * Inverse);
        Quat.X = static_cast<float>(static_cast<double>(Inverse) * X);
        Quat.Y = static_cast<float>(static_cast<double>(Inverse) * Y);
        Quat.Z = static_cast<float>(static_cast<double>(Inverse) * Z);
        if (GMissionMathEngine == nullptr)
            GMissionMathEngine = ContentAcquire3DMathEngine();
        GMissionMathEngine->QuaternionToMatrix(*OutMatrix, Quat);
    }

    // [MsnSolar] values. The nickname, faction, archetype, loadout, base and label keys read the
    // value with the no-index get_value_string (IAT 06fb3054).
    void LoadMissionSolarValues(INI_Reader* Reader, FMissionSolar* Solar)
    {
        while (Reader->read_value())
        {
            if (Reader->is_value("nickname"))
            {
                Solar->Nickname = Reader->get_value_string();
                Solar->LabelIds.push_back(CreateID(Solar->Nickname.Data));
            }
            else if (Reader->is_value("position"))
            {
                // Read in the order 1, 0, 2.
                float const Y = Reader->get_value_float(1);
                float const X = Reader->get_value_float(0);
                float const Z = Reader->get_value_float(2);
                Solar->Position.x = X;
                Solar->Position.y = Y;
                Solar->Position.z = Z;
            }
            else if (Reader->is_value("orientation"))
            {
                ReadMissionSolarOrientation(Reader, &Solar->Orientation);
            }
            else if (Reader->is_value("faction"))
            {
                pub::Reputation::GetReputationGroup(Solar->Faction, Reader->get_value_string());
            }
            else if (Reader->is_value("string_id"))
            {
                Solar->Name.begin_mad_lib(static_cast<unsigned int>(Reader->get_value_int(0)));
                Solar->Name.end_mad_lib();
            }
            else if (Reader->is_value("pilot"))
            {
                // Only when FUN_06f3e020 returns exactly 1.
                unsigned int PilotId = CreateID(Reader->get_value_string(0));
                if (GetPilotPersonality(&PilotId, &Solar->Personality))
                    Solar->bPilot = 1;
            }
            else if (Reader->is_value("archetype"))
            {
                Solar->ArchetypeId = pub::GetNicknameId(Reader->get_value_string());
            }
            else if (Reader->is_value("loadout"))
            {
                pub::GetLoadoutID(Solar->LoadoutId, Reader->get_value_string());
            }
            else if (Reader->is_value("system"))
            {
                pub::GetSystemID(Solar->SystemId, Reader->get_value_string(0));
            }
            else if (Reader->is_value("base"))
            {
                pub::GetBaseID(Solar->BaseId, Reader->get_value_string());
            }
            else if (Reader->is_value("voice"))
            {
                pub::GetVoiceID(Solar->VoiceId, Reader->get_value_string(0));
            }
            else if (Reader->is_value("radius"))
            {
                Solar->Radius = Reader->get_value_float(0);
            }
            else if (Reader->is_value("label"))
            {
                Solar->LabelIds.push_back(CreateID(Reader->get_value_string()));
            }
            else if (Reader->is_value("costume"))
            {
                ParseSpaceCostume(&Solar->SpaceCostume, Reader);
            }
            else if (Reader->is_value("visit"))
            {
                if (_stricmp(Reader->get_value_string(0), "unknowable") == 0)
                    Solar->Visit = 0x80;
            }
        }
    }

    // [Mission] values.
    void LoadMissionHeaderValues(INI_Reader* Reader, FMissionObject* Mission)
    {
        while (Reader->read_value())
        {
            if (Reader->is_value("mission_title"))
            {
                Mission->MissionTitle.begin_mad_lib(static_cast<unsigned int>(Reader->get_value_int(0)));
                Mission->MissionTitle.end_mad_lib();
            }
            else if (Reader->is_value("mission_offer"))
            {
                Mission->MissionOffer.begin_mad_lib(static_cast<unsigned int>(Reader->get_value_int(0)));
                Mission->MissionOffer.end_mad_lib();
            }
            else if (Reader->is_value("reward"))
            {
                Mission->Reward = static_cast<unsigned int>(Reader->get_value_int(0));
            }
            else if (Reader->is_value("npc_ship_file"))
            {
                // OpenContentDataIni (FUN_06f24280) is not tested; LoadNpcShipArchs =
                // FUN_06f32510.
                INI_Reader NpcShipReader;
                OpenContentDataIni(Reader->get_value_string(0), &NpcShipReader);
                LoadNpcShipArchs(&NpcShipReader, &Mission->NpcShipArchs);
                NpcShipReader.close();
            }
        }
    }

    // FUN_06f18eb0: the values of one [MsnShip]/[MsnFormation] section into Group, the new last
    // record of Groups (+0xb8). The template ship slot is appended first and Current is that
    // slot; "formation" and "ship" drop it (Current = null). The keys that write through Current
    // have no null test (retail neither).
    void LoadMissionShipValues(INI_Reader* Reader, FMissionShipGroup* Group,
                               std::list<FMissionShipGroup>* Groups)
    {
        Group->Ships.push_back(FMissionShip());
        FMissionShip* Current = &Group->Ships.back();
        while (Reader->read_value())
        {
            if (Reader->is_value("nickname"))
            {
                Group->Nickname = Reader->get_value_string();
                Current->Nickname = Group->Nickname.Data;
                unsigned int const NicknameId = CreateID(Reader->get_value_string());
                Current->LabelIds.push_back(NicknameId);
            }
            else if (Reader->is_value("npc"))
            {
                Current->NpcId.Id = CreateID(Reader->get_value_string());
            }
            else if (Reader->is_value("random_name"))
            {
                Current->bRandomName = Reader->get_value_bool(0) ? 1 : 0;
            }
            else if (Reader->is_value("label"))
            {
                Current->LabelIds.push_back(CreateID(Reader->get_value_string()));
            }
            else if (Reader->is_value("init_objectives"))
            {
                // Only while +0x64 is empty.
                if (Current->InitMetaBehavior.IsEmpty())
                    Current->InitObjectives.Id = CreateID(Reader->get_value_string(0));
            }
            else if (Reader->is_value("init_metabehavior"))
            {
                // Only while +0x54 is empty.
                if (Current->InitObjectives.IsEmpty())
                    Current->InitMetaBehavior.Id = CreateID(Reader->get_value_string(0));
            }
            else if (Reader->is_value("jumper"))
            {
                unsigned char const bJumper = Reader->get_value_bool(0) ? 1 : 0;
                Current->bJumper = bJumper;
                Current->bOriginalJumpFlag = bJumper;
            }
            else if (Reader->is_value("cargo"))
            {
                // A failed GetGoodID skips the key; the count is read as a float.
                unsigned int GoodId;
                if (pub::GetGoodID(GoodId, Reader->get_value_string(0)) == 0)
                {
                    FMissionShipCargo Entry;
                    Entry.GoodId = GoodId;
                    Entry.Count = static_cast<int>(Reader->get_value_float(1));
                    Current->Cargo.insert(Current->Cargo.end(), 1, Entry);
                }
            }
            else if (Reader->is_value("hit_points"))
            {
                int const HitPoints = Reader->get_value_int(0);
                Current->HitPoints = HitPoints;
                Current->OriginalHitPoints = HitPoints;
            }
            else if (Reader->is_value("radius"))
            {
                Current->Radius = Reader->get_value_float(0);
            }
            else if (Reader->is_value("system"))
            {
                // Only a found record is stored.
                unsigned int SystemId;
                if (pub::GetSystemID(SystemId, Reader->get_value_string(0)) == 0)
                {
                    FSystemInfo* System = FindSystemInfo(&SystemId);
                    if (System != nullptr)
                        Group->System = System;
                }
            }
            else if (Reader->is_value("position"))
            {
                // Read in the order 1, 0, 2.
                float const Y = Reader->get_value_float(1);
                float const X = Reader->get_value_float(0);
                float const Z = Reader->get_value_float(2);
                Group->Position.x = X;
                Group->Position.y = Y;
                Group->Position.z = Z;
            }
            else if (Reader->is_value("rel_pos"))
            {
                // The heading is in degrees (DAT_06fb6060 = pi/180).
                Group->bRelPos = 1;
                Group->RelPosHeading = Reader->get_value_float(0) * 0.0174532925f;
                Group->RelPosObj = Reader->get_value_string(1);
                if (!Reader->is_value_empty(2))
                    Group->RelPosRange = Reader->get_value_float(2);
            }
            else if (Reader->is_value("orientation"))
            {
                // The [MsnSolar] normalisation, not the [MsnLoot] one.
                Group->bOrientation = 1;
                FMatrix Rotation;
                ReadMissionSolarOrientation(Reader, &Rotation);
                std::memcpy(&Group->Orientation, &Rotation, sizeof(Rotation));
            }
            else if (Reader->is_value("arrival_obj"))
            {
                // Every slot's radius is zeroed.
                std::vector<FMissionShip>::iterator ShipIt = Group->Ships.begin();
                for (; ShipIt != Group->Ships.end(); ++ShipIt)
                    ShipIt->Radius = 0.0f;
                Group->ArrivalObj = Reader->get_value_string(0);
                if (!Reader->is_value_empty(1))
                    Group->ArrivalDockIndex = Reader->get_value_int(1);
            }
            else if (Reader->is_value("formation"))
            {
                if (Current != nullptr)
                {
                    Group->Ships.pop_back();
                    Current = nullptr;
                }
                Group->Formation.Id = CreateID(Reader->get_value_string(0));
            }
            else if (Reader->is_value("ship"))
            {
                if (Current != nullptr)
                {
                    Group->Ships.pop_back();
                    Current = nullptr;
                }
                TString<0x30> ShipName;
                ShipName = Reader->get_value_string(0);
                // The first record of Groups with that name (strcmp) gives a copy of its first
                // slot, then leaves the list.
                std::list<FMissionShipGroup>::iterator It = Groups->begin();
                for (; It != Groups->end(); ++It)
                {
                    if (std::strcmp(ShipName.Data, It->Nickname.Data) == 0)
                        break;
                }
                if (It == Groups->end())
                    continue;
                {
                    FMissionShip const Copy(It->Ships.front());
                    Group->Ships.insert(Group->Ships.end(), 1, Copy);
                }
                Groups->erase(It);
            }
        }
    }
}

// The stack template in FUN_06f172f0.
FMissionLoot::FMissionLoot()
    : SpaceObjId(0)
    , bSpawned(0)
    , bRelPos(0)
    , Field_0xe8(0)
    , SubObjId(SUBOBJ_ID_NONE)
    , bCannotJettison(0)
{
    Nickname.Length = 0;
    Nickname.Data[0] = '\0';
    // FUN_06f18c10. Retail does not write LootInfo +0x62/+0x63.
    std::memset(&Info, 0, sizeof(Info));
    Info.Orientation.E[0][0] = 1.0f;
    Info.Orientation.E[1][1] = 1.0f;
    Info.Orientation.E[2][2] = 1.0f;
    Info.bCanAiTractor = 1;
    Info.bMission = 0;
    RelPosObj.Length = 0;
    RelPosObj.Data[0] = '\0';
    RelPosOffset.x = 0.0f;
    RelPosOffset.y = 0.0f;
    RelPosOffset.z = 0.0f;
}

// The stack template in FUN_06f172f0.
FMissionSolar::FMissionSolar()
    : SpaceObjId(0)
    , bSpawned(0)
    , bPilot(0)
    , Radius(150.0f)
    , ArchetypeId(0)
    , SystemId(0)
    , LoadoutId(0)
    , VibeGroupId(-1)
    , Faction(0xffffffffu)
    , BaseId(0)
    , VoiceId(0)
    , CostumeId(0)
    , Visit(0)
{
    Nickname.Length = 0;
    Nickname.Data[0] = '\0';
    std::memset(&Orientation, 0, sizeof(Orientation));
    Orientation.D[0][0] = 1.0f;
    Orientation.D[1][1] = 1.0f;
    Orientation.D[2][2] = 1.0f;
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
    // FUN_06eac5b0 leaves the accessories as they are.
    SpaceCostume.PartIds[0] = 0;
    SpaceCostume.PartIds[1] = 0;
    SpaceCostume.PartIds[2] = 0;
    SpaceCostume.PartIds[3] = 0;
    SpaceCostume.AccessoryCount = 0;
}

// FMissionDirectiveList's bodies (FUN_06f20230 copy, FUN_06ed2a00 Clear) are in
// ObjListDirective.h.

// FUN_06f20230 (the same body as the directive list copy).
FMissionMetaBehaviorList::FMissionMetaBehaviorList(FMissionMetaBehaviorList const& Other)
    : Items(Other.Items)
{
    std::list<FMetaBehavior*>::iterator It = Items.begin();
    for (; It != Items.end(); ++It)
        (*It)->AddRef();
}

// Inlined in FUN_06f172f0 and the +0x114 node value dtor (FUN_06f1e120).
FMissionMetaBehaviorList::~FMissionMetaBehaviorList()
{
    std::list<FMetaBehavior*>::iterator It = Items.begin();
    for (; It != Items.end(); ++It)
        (*It)->Release();
    Items.clear();
}

// The template in FUN_06f18eb0.
FMissionShip::FMissionShip()
    : bJumper(0)
    , bOriginalJumpFlag(0)
    , Priority(0)
    , bRandomName(0)
    , HitPoints(-1)
    , OriginalHitPoints(-1)
    , Radius(7.0f)
{
    Nickname.Length = 0;
    Nickname.Data[0] = '\0';
    NpcId.clear();
    InitObjectives.clear();
    InitMetaBehavior.clear();
}

// FUN_06ed2a60. The unwind states follow the member order.
FMissionShip::FMissionShip(FMissionShip const& Other)
    : Nickname(Other.Nickname)
    , NpcId(Other.NpcId)
    , LabelIds(Other.LabelIds)
    , bJumper(Other.bJumper)
    , bOriginalJumpFlag(Other.bOriginalJumpFlag)
    , Priority(Other.Priority)
    , bRandomName(Other.bRandomName)
    , InitObjectives(Other.InitObjectives)
    , Directives(Other.Directives)
    , InitMetaBehavior(Other.InitMetaBehavior)
    , Guide(Other.Guide)
    , Cargo(Other.Cargo)
    , HitPoints(Other.HitPoints)
    , OriginalHitPoints(Other.OriginalHitPoints)
    , Radius(Other.Radius)
{
}

// FUN_06ed34e0.
FMissionShip& FMissionShip::operator=(FMissionShip const& Other)
{
    Nickname = Other.Nickname.Data;
    NpcId = Other.NpcId;
    if (&LabelIds != &Other.LabelIds)
        LabelIds = Other.LabelIds;
    bJumper = Other.bJumper;
    bOriginalJumpFlag = Other.bOriginalJumpFlag;
    Priority = Other.Priority;
    bRandomName = Other.bRandomName;
    InitObjectives = Other.InitObjectives;
    Directives = Other.Directives;
    InitMetaBehavior = Other.InitMetaBehavior;
    // No same-pointer test. The handle's raw stores do not touch the count.
    if (Guide.Pointer != nullptr)
    {
        Guide.Pointer->Release();
        Guide = nullptr;
    }
    Guide = Other.Guide.Pointer;
    if (Guide.Pointer != nullptr)
        Guide.Pointer->AddRef();
    Cargo = Other.Cargo;
    HitPoints = Other.HitPoints;
    OriginalHitPoints = Other.OriginalHitPoints;
    Radius = Other.Radius;
    return *this;
}

// FUN_06ed2c20: the body releases and clears the guide; the member handle is null by the time
// it is destroyed.
FMissionShip::~FMissionShip()
{
    if (Guide.Pointer != nullptr)
    {
        Guide.Pointer->Release();
        Guide = nullptr;
    }
}

// FUN_06f0e2d0. Retail writes the matrix and the position twice, with the same values.
FMissionShipGroup::FMissionShipGroup()
    : System(nullptr)
    , RelPosHeading(0.0f)
    , RelPosRange(2500.0f)
    , bRelPos(0)
    , bOrientation(0)
    , ArrivalObjId(0)
    , ArrivalDockIndex(-1)
{
    Nickname.Length = 0;
    Nickname.Data[0] = '\0';
    std::memset(&Orientation, 0, sizeof(Orientation));
    Orientation.D[0][0] = 1.0f;
    Orientation.D[1][1] = 1.0f;
    Orientation.D[2][2] = 1.0f;
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
    RelPosObj.Length = 0;
    RelPosObj.Data[0] = '\0';
    ArrivalObj.Length = 0;
    ArrivalObj.Data[0] = '\0';
    Formation.clear();
    Formation.Id = CreateID("transport_liberty");
}

// FUN_06f172f0. Any other header is skipped.
void FMissionObject::LoadMission(INI_Reader* Reader)
{
    Reader->reset();
    ResetMissionFmtStr(MissionTitle);
    ResetMissionFmtStr(MissionOffer);

    while (Reader->read_header())
    {
        if (Reader->is_header("MsnLoot"))
        {
            // The keys write the new last record.
            MissionLoots.push_back(FMissionLoot());
            LoadMissionLootValues(Reader, &MissionLoots.back());
        }
        else if (Reader->is_header("MsnSolar"))
        {
            MissionSolars.push_back(FMissionSolar());
            LoadMissionSolarValues(Reader, &MissionSolars.back());
        }
        else if (Reader->is_header("MsnShip") || Reader->is_header("MsnFormation"))
        {
            MissionShips.push_back(FMissionShipGroup());
            LoadMissionShipValues(Reader, &MissionShips.back(), &MissionShips);
        }
        else if (Reader->is_header("MsnRandEnc"))
        {
            // A stack record, copied to the end of +0x98 when the section is done.
            FMissionRandEnc Record;
            while (Reader->read_value())
            {
                if (Reader->is_value("nickname"))
                {
                    Record.Nickname = Reader->get_value_string(0);
                    Record.NicknameId.Id = CreateID(Reader->get_value_string(0));
                }
                else if (Reader->is_value("encounter_type"))
                {
                    // "TL_Attack" / "SP_Attack" replace both objects and the populator reads the
                    // rest of the section into the parameters (slot 4 +0x10). Any other type
                    // keeps the objects.
                    char const* Type = Reader->get_value_string(0);
                    char const* ParamsName = nullptr;
                    char const* PopulatorName = nullptr;
                    if (_stricmp(Type, "TL_Attack") == 0)
                    {
                        ParamsName = "TLParams";
                        PopulatorName = "TLPzr";
                    }
                    else if (_stricmp(Type, "SP_Attack") == 0)
                    {
                        ParamsName = "SPParams";
                        PopulatorName = "SPPzr";
                    }
                    if (ParamsName != nullptr)
                    {
                        if (Record.Params != nullptr)
                        {
                            Record.Params->Release();
                            Record.Params = nullptr;
                        }
                        CreateRandEncParams(ParamsName, &Record.Params.Pointer);   // FUN_06efb830
                        if (Record.Populator != nullptr)
                        {
                            Record.Populator->Release();
                            Record.Populator = nullptr;
                        }
                        CreateRandEncPopulator(PopulatorName, &Record.Populator.Pointer);  // FUN_06efb980
                        Record.Populator->Parse(Reader, Record.Params);
                    }
                    // Retail does this for every type and does not test the object.
                    Record.Params->Labels.push_back(Record.NicknameId.Id);
                    Record.Params->Labels.push_back(GIdMissionRandomEncounter);
                    Record.Params->ControllerId = ControllerId;
                    Record.Params->Player = PlayerState;
                }
            }
            RandEncs.push_back(Record);
        }
        else if (Reader->is_header("ObjList"))
        {
            // A duplicate id keeps the first list.
            FMissionDirectiveList Directives;
            ID_String Nickname;
            Nickname.clear();
            LoadObjList(Reader, &Directives.Items, &Nickname.Id);
            ObjLists.insert(std::map<unsigned int, FMissionDirectiveList>::value_type(Nickname.Id,
                                                                                    Directives));
        }
        else if (Reader->is_header("MetaBehavior"))
        {
            // LoadMetaBehavior = FUN_06f065e0. A duplicate id keeps the first list.
            FMissionMetaBehaviorList Objects;
            ID_String Nickname;
            Nickname.clear();
            LoadMetaBehavior(Reader, &Objects.Items, &Nickname.Id);
            MetaBehaviors.insert(
                std::map<unsigned int, FMissionMetaBehaviorList>::value_type(Nickname.Id, Objects));
        }
        else if (Reader->is_header("Dialog"))
        {
            // The first value is read without a test and must be "nickname", else the section
            // is skipped. A duplicate id keeps the first dialog.
            Reader->read_value();
            if (Reader->is_value("nickname"))
            {
                unsigned int DialogId = CreateID(Reader->get_value_string(0));
                FMissionDialog Dialog;
                Dialog.ControllerId = ControllerId;
                Dialog.System = nullptr;
                Dialog.Load(Reader);
                GMissionDialogs.insert(
                    std::map<unsigned int, FMissionDialog>::value_type(DialogId, Dialog));
            }
        }
        else if (Reader->is_header("Mission"))
        {
            LoadMissionHeaderValues(Reader, this);
        }
        else if (Reader->is_header("NNObjective"))
        {
            // The first player record is not null-tested.
            LoadNNObjective(Reader, this, &GetFirstPlayer()->Objectives);
        }
    }
}

// ---------------------------------------------------------------------------------------
// FMissionNnPath (mission +0x21c)
// ---------------------------------------------------------------------------------------

namespace
{
    // The timer block FUN_06f1be20 inlines five times (ids from DAT_06fce76c).
    void StartNnPathTimer(FMissionNnPath* Path)
    {
        unsigned int const Id = GContentNextUniqueId;
        GContentNextUniqueId = Id + 1;
        Path->TimerId = Id;
        pub::Controller::TimerExpired Timer;
        Timer.Param1 = Id;
        Timer.Param2 = 0;
        pub::SetTimer(Path->Mission->ControllerId, Timer, 1.0f);
    }
}

// FUN_06f1be20 (GetLabelObjectId inlined twice). The cases that fall through rebuild, resend
// and restart the timer.
int FMissionNnPath::HandleMessage(pub::Controller::EMessage MessageId, void const* MessageParms)
{
    using pub::Controller::EMessage;
    if (bActive == 0)
        return State;

    unsigned int const* Parms = static_cast<unsigned int const*>(MessageParms);
    FMissionObject* Owner = Mission;
    bool bLaunched = false;
    switch (MessageId)
    {
    case EMessage::BaseExit:
        if (State == 3 && Owner->PlayerId == Parms[1] && (Owner->PlayerId & 0x3fffffff) != 0)
            State = 4;
        return State;

    case EMessage::TradelaneEnter:
        if (State != 3 || FindContentPlayerState(Owner->PlayerId) == nullptr)
            return State;
        if (Parms[0] != Owner->PlayerState->GetShipId() || (Parms[0] & 0x3fffffff) == 0)
            return State;
        State = 4;
        SendObjectives(true, false);
        return State;

    case EMessage::TradelaneExit:
        if (State != 4 || FindContentPlayerState(Owner->PlayerId) == nullptr)
            return State;
        if (Parms[0] != Owner->PlayerState->GetShipId() || (Parms[0] & 0x3fffffff) == 0)
            return State;
        StartNnPathTimer(this);
        State = 3;
        return State;

    case EMessage::Heartbeat:
        if (State != 1)
            return State;
        StartNnPathTimer(this);
        State = 2;
        return State;

    case EMessage::SpaceEnter:
        if (Owner->PlayerId != Parms[1] || (Owner->PlayerId & 0x3fffffff) == 0)
            return State;
        if (State == 4)
        {
            StartNnPathTimer(this);
            State = 3;
        }
        if (Owner->PlayerState->GetSystemId() == SystemId)
            TargetObjectId = Owner->GetLabelObjectId(&Label);
        bLaunched = true;
        break;

    case EMessage::SpaceExit:
        if (Owner->PlayerId == Parms[1] && (Owner->PlayerId & 0x3fffffff) != 0)
            TargetObjectId = 0;
        return State;

    case EMessage::SystemExit:
        Objectives.Clear();
        Legs.clear();
        SendObjectives(false, false);
        return State;

    case EMessage::SystemEnter:
        if (Parms[2] != Owner->PlayerId || FindContentPlayerState(Owner->PlayerId) == nullptr)
            return State;
        if (Parms[0] != SystemId)
        {
            TargetObjectId = 0;
            return State;
        }
        if (Label.Length != 0)
            TargetObjectId = Owner->GetLabelObjectId(&Label);
        bLaunched = true;
        break;

    case EMessage::TimerExpired:
        // This path's timer, not while docked.
        if (Parms[0] != TimerId || State == 4)
            return State;
        break;

    case EMessage::PlayerMissionChanged:
        if (Parms[1] != Owner->PlayerId)
            return State;
        if (Parms[0] != Owner->ControllerId && (Parms[0] & 0x3fffffff) != 0)
            return State;
        break;

    default:
        return State;
    }

    Rebuild();
    SendObjectives(false, bLaunched);
    StartNnPathTimer(this);
    return State;
}

// FUN_06f1c3f0.
void FMissionNnPath::Save(FILE* File)
{
    fprintf(File, "nnpath = %u, %u, %f, %f, %f, %s, %u\n", TravelIds, TargetIds,
            static_cast<double>(Position.x), static_cast<double>(Position.y),
            static_cast<double>(Position.z), Label.Data, SystemId);
}

// FUN_06f1c430. The position is read in the order 4, 3, 2.
void FMissionNnPath::Load(INI_Reader* Reader, FMissionObject* InMission)
{
    TravelIds = Reader->get_value_int(0);
    TargetIds = Reader->get_value_int(1);
    float const Z = Reader->get_value_float(4);
    float const Y = Reader->get_value_float(3);
    float const X = Reader->get_value_float(2);
    Position.x = X;
    Position.y = Y;
    Position.z = Z;
    if (!Reader->is_value_empty(5))
        Label = Reader->get_value_string(5);
    SystemId = static_cast<unsigned int>(Reader->get_value_int(6));
    State = 1;
    bActive = 1;
    Mission = InMission;
}

// FUN_06f1c530. In the path's system with a ship, the target is the label's object (resolved
// once; GetLocation runs even for 0); elsewhere it is the next gate (FUN_06f29c10) and the
// destination its far side (FUN_06f2a190). BuildNnPath (TU53 FUN_06f5ea40) gets the radius
// plus 200. One leg or none left in the path's system switches to TargetIds and, the first
// time, State 5. Mission slot 11 builds the objectives (types 8, 9, 8).
void FMissionNnPath::Rebuild()
{
    FMissionObject* Owner = Mission;
    if (FindContentPlayerState(Owner->PlayerId) == nullptr)
        return;
    FSystemInfo* System = Owner->PlayerState->GetSystemRecord();
    if (System == nullptr)
        return;

    unsigned int Target = 0;
    // Retail's stack slot is left as it was when FUN_06f2a190 finds no connection.
    Vector Destination = {0.0f, 0.0f, 0.0f};
    bool const bHasShip = Owner->PlayerState->ShipObject != nullptr;
    if (System->SystemId == SystemId)
    {
        if (bHasShip && Label.Length != 0)
        {
            if ((TargetObjectId & 0x3fffffff) == 0)
            {
                TargetObjectId = Owner->GetLabelObjectId(&Label);
                if ((TargetObjectId & 0x3fffffff) != 0)
                {
                    Vector Center;
                    pub::SpaceObj::GetRadius(TargetObjectId, TargetRadius, Center);
                }
            }
            Target = TargetObjectId;
            Matrix Orientation;
            pub::SpaceObj::GetLocation(Target, Position, Orientation);
        }
        Destination = Position;
    }
    else
    {
        Target = GetNextHopConnectionObject(System->SystemId, SystemId, 1);
        if ((Target & 0x3fffffff) == 0)
            Target = GetNextHopConnectionObject(System->SystemId, SystemId, 0);
        GetConnectionPosition(System->SystemId, Target, &Destination);
    }

    Objectives.Clear();
    Legs.clear();
    float const Radius = TargetRadius + 200.0f;
    // GetPosition = FUN_06f47af0; the BuildNnPath result is not read.
    Vector PlayerPosition;
    BuildNnPath(System, Owner->PlayerState->GetPosition(&PlayerPosition), &Destination, &Legs,
                0x19, &Radius, 0);

    int Ids = TravelIds;
    if (Legs.size() <= 1 && System->SystemId == SystemId)
    {
        Ids = TargetIds;
        if (bArrived == 0)
        {
            bArrived = 1;
            State = 5;
        }
    }
    Owner->BuildPathObjectives(System, &Legs, &Objectives, TravelIds, Ids, Ids, Target, 8, 9, 8,
                               nullptr);
}

// FUN_06f1c7c0. The array is sent with Count, not the number filled (unfilled entries stay
// Type 1 with an empty text).
void FMissionNnPath::SendObjectives(bool bSkipFirst, bool bLaunched)
{
    int const Count = static_cast<int>(Objectives.Objectives.size()) - (bSkipFirst ? 1 : 0);
    if (Count <= 0)
        return;

    std::vector<pub::Player::MissionObjective> Sent(static_cast<size_t>(Count));
    for (int Slot = 0; Slot < Count; ++Slot)
        Sent[static_cast<size_t>(Slot)].Type = 1;

    int Index = 0;
    std::list<FMissionObjective*>::iterator It = Objectives.Objectives.begin();
    for (; It != Objectives.Objectives.end(); ++It)
    {
        if (bSkipFirst && It == Objectives.Objectives.begin())
            continue;
        FMissionObjective* Objective = *It;
        if (!Objective->BuildDescription())
            continue;
        if (State == 2 || State == 5)
        {
            Objective->Objective.Type = Index != 0 ? 9u : 0x3au;
            State = 3;
        }
        else if (bLaunched)
        {
            Objective->Objective.Type = Index != 0 ? 9u : 0x1au;
            State = 3;
        }
        else
        {
            Objective->Objective.Type = Index != 0 ? 9u : 0xau;
        }
        Sent[static_cast<size_t>(Index)].Type = Objective->Objective.Type;
        Sent[static_cast<size_t>(Index)].Description = Objective->Objective.Description;
        ++Index;
    }

    pub::Player::SetMissionObjectives(Mission->PlayerId, Mission->ControllerId, Sent.data(),
                                      static_cast<unsigned int>(Count), Mission->MissionTitle,
                                      Mission->MissionType, Mission->MissionOffer);
}

// FUN_06f1c990. Retail clears the label twice and does not touch the list's +0x10 Owner.
void FMissionNnPath::Reset()
{
    bActive = 0;
    Objectives.Clear();
    Legs.clear();
    TargetObjectId = 0;
    Label = static_cast<char const*>(nullptr);
    TargetRadius = 0.0f;
    Label = static_cast<char const*>(nullptr);
    TravelIds = 1;
    TargetIds = 1;
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
    SystemId = 0;
    State = 0;
    bArrived = 0;
    TimerId = 0;
    Mission = nullptr;
}
