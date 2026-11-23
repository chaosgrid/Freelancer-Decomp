// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU42, the NPC name part (06f31650-06f32000). See NpcNames.h.
// The mad-lib ids 0x3f17..0x3f24 are the name patterns the client formats; the name value
// readers in NpcShips.h read the appended values back out of the two FmtStr.

#include "NPCs/NpcNames.h"

#include "NPCs/FactionProps.h"
#include "NPCs/NpcShips.h"            // FMsnNpc, the name value readers
#include "common_server.h"       // pub::Reputation, pub::GetType, pub::GetShipArchSTRID

#include <cstdint>

int GNpcNameReputationCount = 0;

// FUN_06f31650.
FNpcNameRequest::FNpcNameRequest()
    : NameIndex(0)
    , Desig(0)
    , DesigNumber(0)
    , FormationSize(1)
    , EscortNumber(0)
    , Field_0x14(0)
    , RankLevel(0)
    , ShipArchId(0)
{
}

// FUN_06f31670. +0x14..+0x1c are left unwritten, as in retail (the only caller writes +0x18
// and +0x1c before every BuildNpcName; +0x14 has no reader).
FNpcNameRequest::FNpcNameRequest(int InNameIndex, int InFormationSize, int InEscortNumber,
                                 int InDesig, int InDesigNumber)
    : NameIndex(InNameIndex)
    , Desig(InDesig)
    , DesigNumber(InDesigNumber)
    , FormationSize(InFormationSize)
    , EscortNumber(InEscortNumber)
{
}

// Inlined in FUN_06f31910 and FUN_06f31d40 (base ctor FUN_06eb6be0, Params not read by it).
FNpcNameReputation::FNpcNameReputation(FNpcNameParams const* Params)
{
    pub::Reputation::Alloc(Reputation, Params->First, Params->Second);
    pub::Reputation::SetAffiliation(Reputation, Params->Affiliation);
    ++GNpcNameReputationCount;
}

// FUN_06f316c0.
FNpcNameReputation::~FNpcNameReputation()
{
    int PilotFirst;
    int PilotLast;
    int LargeShipName = 1;
    int Desig = 1;
    int DesigNumber;
    {
        FmtStr First;
        FmtStr Second;
        pub::Reputation::GetName(Reputation, First, Second);
        // FUN_06f322a0.
        GetSecondNameValues(&First, &Second, &PilotFirst, &PilotLast);
        // The FUN_06f32320 body inlined.
        if (Second.MadLibId == 0x3f1a || Second.MadLibId == 0x3f1c)
            LargeShipName = (int)Second.Head->Next->Next->Value;
        // The FUN_06f32350 body inlined.
        ::GetFirstNameValue(&First, &Second, &Desig);
        // FUN_06f323c0; FUN_06f32420 rolls a value the destructor never reads (the rand()
        // call stays).
        GetFirstNameLastValue(&First, &Second, &DesigNumber);
        int Unused;
        RollNpcNameValue(&First, &Second, &Unused);
    }

    // The name tables are not null-tested.
    unsigned int Affiliation = GetAffiliation();
    FFactionNames* Names = GetFactionNames(&Affiliation);
    if (PilotFirst != 1 || PilotLast != 1)
        Names->ReleasePilotName(PilotFirst, PilotLast, false);
    if (LargeShipName != 1)
        Names->ReleaseLargeShipName(LargeShipName, false);
    if (Desig != 1)
        Names->ReleaseFormationDesig(Desig, DesigNumber, false);

    if (Reputation != -1)
        pub::Reputation::Free(Reputation);
    --GNpcNameReputationCount;
}

// FUN_06f31ec0 (the FUN_06f32350 body inlined).
bool FNpcNameReputation::GetFormationDesig(int* Out) const
{
    FmtStr First;
    FmtStr Second;
    pub::Reputation::GetName(Reputation, First, Second);
    return ::GetFirstNameValue(&First, &Second, Out);
}

// FUN_06f32020 (the FUN_06f323c0 body inlined).
bool FNpcNameReputation::GetFormationDesigNumber(int* Out) const
{
    FmtStr First;
    FmtStr Second;
    pub::Reputation::GetName(Reputation, First, Second);
    return GetFirstNameLastValue(&First, &Second, Out);
}

// FUN_06f32190.
bool FNpcNameReputation::RollNameValue(int* Out) const
{
    FmtStr First;
    FmtStr Second;
    pub::Reputation::GetName(Reputation, First, Second);
    return RollNpcNameValue(&First, &Second, Out);
}

// FUN_06f319b0.
bool __cdecl PickNpcNameGender(unsigned int Affiliation, FNpcNameParams* Params)
{
    return GetFactionProps(&Affiliation)->PickGender(&Params->Gender);
}

// FUN_06f319d0.
bool __cdecl BuildNpcName(unsigned int Affiliation, FNpcNameRequest const* Request,
                          FNpcNameParams* Out, bool bNamedLeader)
{
    Out->Affiliation = Affiliation;
    FFactionNames* Names = GetFactionNames(&Affiliation);

    if (Request->NameIndex != 0)
    {
        // 0x30000 = FIGHTER 0x10000 | FREIGHTER 0x20000.
        unsigned int ShipType;
        pub::GetType(Request->ShipArchId, ShipType);
        bool const bFighterOrFreighter = (ShipType & 0x30000) != 0;

        // The first name's pattern.
        unsigned int FirstId;
        if (Request->FormationSize == 1)
            FirstId = bFighterOrFreighter ? 0x3f17 : 0x3f19;
        else if (bFighterOrFreighter)
            FirstId = Request->EscortNumber == 0 ? 0x3f20 : 0x3f1d;
        else
            FirstId = 0x3f1b;
        Out->First.begin_mad_lib(FirstId);
        Out->First.append_string(Names->GetShortGroupName());
        Out->First.append_string(Names->GetNameId(Request->NameIndex));
        // The request's own designation (counted as used) or a new reserved one.
        int Desig = Request->Desig;
        int DesigNumber = Request->DesigNumber;
        if (Desig != 0)
            Names->AddFormationDesigUse(Desig, DesigNumber);
        else
            Names->PickFormationDesig(&Desig, &DesigNumber, true);
        Out->First.append_string(static_cast<unsigned int>(Desig));
        Out->First.append_int(static_cast<unsigned int>(DesigNumber));
        Out->First.end_mad_lib();

        if (bFighterOrFreighter)
        {
            // The second name's pattern.
            unsigned int SecondId;
            if (Request->FormationSize == 1)
                SecondId = 0x3f18;
            else if (Request->EscortNumber == 0)
                SecondId = 0x3f21;
            else
                SecondId = 0x3f1e;
            Out->Second.begin_mad_lib(SecondId);
            int PilotFirst = 0;
            int PilotLast = 0;
            if (Out->Second.MadLibId != 0x3f1e)
            {
                Out->Second.append_string(Names->GetRankDesig(&Request->RankLevel));
                Names->PickPilotName(&PilotFirst, &PilotLast, Out->Gender, true);
                Out->Second.append_string(static_cast<unsigned int>(PilotFirst));
                Out->Second.append_string(static_cast<unsigned int>(PilotLast));
                Out->Second.end_mad_lib();
                return true;
            }
            // "Escort" (0x3f1f), the escort number, rank designation, a reserved last name
            // only.
            Out->Second.append_string(0x3f1f);
            Out->Second.append_int(static_cast<unsigned int>(Request->EscortNumber));
            Out->Second.append_string(Names->GetRankDesig(&Request->RankLevel));
            Names->PickPilotName(nullptr, &PilotLast, Out->Gender, true);
            Out->Second.append_string(static_cast<unsigned int>(PilotLast));
            Out->Second.end_mad_lib();
            return true;
        }

        // Any other ship type: the ship arch's name, the faction's large ship designation, a
        // reserved large ship name.
        Out->Second.begin_mad_lib(Request->FormationSize == 1 ? 0x3f1a : 0x3f1c);
        unsigned int ShipName;
        pub::GetShipArchSTRID(Request->ShipArchId, ShipName);
        Out->Second.append_string(ShipName);
        Out->Second.append_string(static_cast<unsigned int>(Names->GetLargeShipDesig()));
        int LargeShipName;
        Names->PickLargeShipName(&LargeShipName, true);
        Out->Second.append_string(static_cast<unsigned int>(LargeShipName));
        Out->Second.end_mad_lib();
        return true;
    }

    // A formation leader also gets 0x3f24 with the short group name and a designation.
    if (bNamedLeader)
    {
        Out->First.begin_mad_lib(0x3f24);
        Out->First.append_string(Names->GetShortGroupName());
        int Desig = Request->Desig;
        int DesigNumber = Request->DesigNumber;
        if (Desig != 0)
            Names->AddFormationDesigUse(Desig, DesigNumber);
        else
            Names->PickFormationDesig(&Desig, &DesigNumber, true);
        Out->First.append_string(static_cast<unsigned int>(Desig));
        Out->First.append_int(static_cast<unsigned int>(DesigNumber));
        Out->First.end_mad_lib();
    }

    if (Request->RankLevel != 0)
    {
        Out->Second.begin_mad_lib(0x3f22);
        Out->Second.append_string(Names->GetRankDesig(&Request->RankLevel));
    }
    else
    {
        Out->Second.begin_mad_lib(0x3f23);
    }
    int PilotFirst = 0;
    int PilotLast = 0;
    Names->PickPilotName(&PilotFirst, &PilotLast, Out->Gender, true);
    Out->Second.append_string(static_cast<unsigned int>(PilotFirst));
    Out->Second.append_string(static_cast<unsigned int>(PilotLast));
    Out->Second.end_mad_lib();
    return true;
}

// FUN_06f31910.
bool __cdecl CreateNpcNameReputation(FNpcNameParams const* Params, FContentPlayerStateBase** Out)
{
    FNpcNameReputation* Object = new FNpcNameReputation(Params);
    *Out = Object;
    return Object != nullptr;
}

// FUN_06f31d40 (the name block's dtor is FUN_06ef4e60).
bool __cdecl CreateMsnNpcReputation(FMsnNpc const* Npc, FContentPlayerStateBase** Out)
{
    FNpcNameParams Name;
    Name.Second.begin_mad_lib(static_cast<unsigned int>(Npc->IndividualName));
    Name.Second.end_mad_lib();
    Name.Affiliation = Npc->Affiliation;

    FNpcNameReputation* Object = new FNpcNameReputation(&Name);
    *Out = Object;
    return Object != nullptr;
}
