// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU24 (06ed5390-06ed5a70): solar NPCs (see NpcCreate.h).

#include "NPCs/NpcCreate.h"
#include "Bases/BaseDB.h"               // FindContentBaseInfo (TU17 FUN_06eac0c0)
#include "NPCs/PilotPersonalities.h"   // GetPilotPersonality (TU45 FUN_06f3e020)
#include "Universe/SystemInfo.h"           // FSystemInfo::SystemId
#include "Core/ContentStaticIds.h"     // TU24's 16 static-init thunks 06ed5390-06ed5570
#include "common_id.h"
#include "common_ini.h"
#include "common_newchar.h"       // pub::StateGraph::get_state_graph
#include "common_server.h"

// FUN_06ed5590. "base" only takes the first one; read_comm_ini_data runs before "pilot" and
// "behavior".
bool __cdecl ParseSolarNpcKey(INI_Reader* Reader, FSolarNpcDesc* Desc)
{
    bool bResult = true;
    if (Reader->is_value("nickname"))
    {
        Desc->Nickname = Reader->get_value_string();
        return bResult;
    }
    if (Reader->is_value("base"))
    {
        if (Desc->Base != nullptr)
            return bResult;
        unsigned int BaseId;
        pub::GetBaseID(BaseId, Reader->get_value_string());
        Desc->Base = FindContentBaseInfo(&BaseId);
        return bResult;
    }
    if (Reader->is_value("reputation"))
    {
        pub::Reputation::GetReputationGroup(Desc->ReputationGroup, Reader->get_value_string());
        return bResult;
    }
    if (Desc->read_comm_ini_data(*Reader))
        return bResult;
    if (Reader->is_value("pilot"))
    {
        unsigned int PilotId = CreateID(Reader->get_value_string());
        if (GetPilotPersonality(&PilotId, &Desc->Personality))
            Desc->bHasPersonality = true;
        return bResult;
    }
    if (Reader->is_value("behavior"))
    {
        Desc->StateGraph = pub::StateGraph::get_state_graph(Reader->get_value_string(),
                                                            pub::StateGraph::LEADER);
        return bResult;
    }
    bResult = false;
    return bResult;
}

// Inlined in FUN_06ed5720 (the base ctor's unused argument is the Group local).
FSolarNpcReputation::FSolarNpcReputation(char const* Nickname, unsigned int const& Group)
{
    pub::Reputation::GetReputation(Reputation, Nickname);
    pub::Reputation::SetAffiliation(Reputation, Group);
}

// FUN_06ed5720. The handle copied raw from the parameters is overwritten, never released;
// without a reputation group it is cleared even when the parameters carried one.
FSolarNpc::FSolarNpc(FSolarNpcDesc const* Desc)
    : FContentSpaceObj(Desc)
    , StateGraph(Desc->StateGraph)
    , bHasPersonality(Desc->bHasPersonality)
{
    if (bHasPersonality)
        Personality = Desc->Personality;

    SpaceObjId = pub::GetNicknameId(Nickname.Data);
    MsgIdPrefix = Desc->MsgIdPrefix.Data;

    if ((SpaceObjId & 0x3fffffff) != 0 && Desc->ReputationGroup != 0xffffffffu)
    {
        unsigned int Group = Desc->ReputationGroup;
        ReputationHandle = new FSolarNpcReputation(Nickname.Data, Group);
    }
    else
    {
        ReputationHandle = nullptr;
    }
}

// 06ed59f0.
bool FSolarNpc::IsInCombat()
{
    return bInCombat;
}

// LAB_06ed5a00. The system is not null-tested.
void FSolarNpc::combat_state_notification(CSNType Type, uint Param1, uint Param2)
{
    FSpaceObjCombatStateMsg Msg;
    Msg.Type = static_cast<unsigned int>(Type);
    Msg.Param1 = Param1;
    Msg.Param2 = Param2;
    Msg.SystemId = GetSystem()->SystemId;
    bInCombat = (Type == 0);
    unsigned int Controller = 0;
    pub::Controller::_SendMessage(Controller, pub::Controller::EMessage::CombatState, &Msg);
}

// FUN_06ed5940.
void FSolarNpc::DeleteReputationHandle()
{
    if (ReputationHandle != nullptr)
        delete ReputationHandle;
    ReputationHandle = nullptr;
}

// FUN_06ed5960 (result dropped).
void FSolarNpc::SubmitPersonalityState(bool bSubmit)
{
    if (ReputationHandle == nullptr || !bSubmit || StateGraph == -1)
        return;
    pub::AI::SetPersonalityParams Params;
    Params.StateGraphId = StateGraph;
    Params.ContentCallbackObject = static_cast<pub::AI::ContentCallback*>(this);
    Params.DirectiveCallbackObject = nullptr;
    Params.bSetPersonality = bHasPersonality ? 1 : 0;
    if (bHasPersonality)
        Params.Value = Personality;
    pub::AI::SubmitState(SpaceObjId, &Params);
}
