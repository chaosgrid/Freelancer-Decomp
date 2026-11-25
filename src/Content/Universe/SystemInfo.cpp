// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU58: SystemInfo and the system table DAT_06fd3a5c.

#include "Universe/SystemInfo.h"

#include "Bases/Cargo.h"              // GetHostileFactionShares (TU44 FUN_06f35190)
#include "Core/ContentInternal.h"
#include "Core/ContentPlayerState.h"
#include "Core/ContentSort.h"        // ContentSort (VC6 _Sort_0; TU58 instance FUN_06f7f370)
#include "Core/ContentStaticIds.h"   // TU58 static-init thunks
#include "NPCs/DockQueues.h"         // CollectFormationMembers (TU53 FUN_06f66220)
#include "Encounters/Encounters.h"
#include "NPCs/FactionProps.h"       // GetFactionProps (TU48 FUN_06f50650), GetFormation (FUN_06f4f3d0)
#include "Missions/MissionObject.h"      // IsSpaceObjTypeSet (TU53 FUN_06f683b0), FindObjectIdByName
#include "NPCs/NpcShip.h"            // FContentNpcShip, FContentNpcShipParams (TU43)
#include "Encounters/Populators.h"
#include "Encounters/RandomEncounters.h"   // FRandEncTLParams, CreateRandEncParamsByType (TU31 FUN_06efb8e0)
#include "Core/RefHandle.h"          // TRefHandle (FUN_06f28e10)
#include "common_ini.h"
#include "common_server.h"
#include "server_cobject.h"     // pub::SpaceObj::Destroy, DESTROY_NORMAL

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <utility>

// LAB_06f75ee0 constructs the map, FUN_06f75f10 (atexit) destroys it (FUN_06f2be60).
std::map<unsigned int, FSystemInfo> GSystemInfos;

// FUN_06f721c0; the CommReferrable base ctor is inlined in retail.
FSystemInfo::FSystemInfo(unsigned int const* InSystemId)
    : SystemId(*InSystemId)
{
}

// FUN_06f75830.
void FSystemInfo::Load(bool bLoadEncounters)
{
    LoadFromReader(nullptr, bLoadEncounters);
}

// FUN_06f74ae0.
void FSystemInfo::LoadFromReader(INI_Reader* Reader, bool bLoadEncounters)
{
    // Count first, then read only on the 0 -> 1 transition.
    int const PreviousCount = LoadCount;
    LoadCount = PreviousCount + 1;
    if (PreviousCount != 0)
        return;

    INI_Reader SystemReader;
    if (Reader == nullptr)
    {
        pub::BuildSystemReader(SystemReader, SystemId);
        Reader = &SystemReader;
    }

    LocalFaction = 0;
    // The Trade_Lane_Ring objects of the system, keyed by nickname.
    std::map<unsigned int, FTradeLaneRingInfo> Rings;

    if (bLoadEncounters)
    {
        EncounterParameters = new std::map<unsigned int, FEncounterParameters*>();
    }

    while (Reader->read_header())
    {
        if (Reader->is_header("System") || Reader->is_header("SystemInfo"))
        {
            while (Reader->read_value())
            {
                if (Reader->is_value("local_faction"))
                    pub::Reputation::GetReputationGroup(LocalFaction, Reader->get_value_string());
            }
        }
        else if (bLoadEncounters && Reader->is_header("EncounterParameters"))
        {
            // "nickname = ...", "filename = ...", by position.
            Reader->read_value();
            unsigned int EncounterId = CreateID(Reader->get_value_string());
            Reader->read_value();
            char FileName[0x104];
            char const* FileNameValue = Reader->get_value_string();
            if (FileNameValue == nullptr)
            {
                FileName[0] = '\0';
            }
            else
            {
                // FUN_06ea9f40: bounded copy into the 0x104 buffer.
                strncpy_s(FileName, sizeof(FileName), FileNameValue, _TRUNCATE);
            }

            INI_Reader EncounterReader;
            if (OpenContentDataIni(FileName, &EncounterReader))
            {
                // operator new(0x4c) + FUN_06ef3830. A repeated id keeps its node; the old
                // object leaks, as retail.
                FEncounterParameters* Parameters =
                    new FEncounterParameters(&EncounterReader, &EncounterId, &SystemId);
                std::pair<std::map<unsigned int, FEncounterParameters*>::iterator, bool> const
                    Inserted = EncounterParameters->insert(
                        std::pair<unsigned int const, FEncounterParameters*>(EncounterId, nullptr));
                Inserted.first->second = Parameters;
            }
        }
        else if (Reader->is_header("Object"))
        {
            unsigned int ObjectId = 0;
            Vector Position;
            Position.x = 0.0f;
            Position.y = 0.0f;
            Position.z = 0.0f;
            // The 0x30 block is copied whole below.
            FTransform Placement;
            SetMatrixIdentity(&Placement.Rotation);
            Placement.Translation.X = 0.0f;
            Placement.Translation.Y = 0.0f;
            Placement.Translation.Z = 0.0f;
            unsigned int PrevRing = 0;
            unsigned int NextRing = 0;
            unsigned int Reputation = 0;   // retail leaves it uninitialised
            bool bRing = false;
            bool bJumpGate = false;
            bool bJumpHole = false;
            unsigned int GotoSystem = 0;
            unsigned int GotoObject = 0;

            if (!Reader->read_value())
                continue;

            do
            {
                if (Reader->is_value("nickname"))
                {
                    // Retail also copies value 0 into a 0x40-byte local string that nothing
                    // reads afterwards.
                    ObjectId = pub::GetNicknameId(Reader->get_value_string(0));
                }
                else if (Reader->is_value("pos"))
                {
                    // Read z, y, x.
                    float Z = Reader->get_value_float(2);
                    float Y = Reader->get_value_float(1);
                    float X = Reader->get_value_float(0);
                    Position.x = X;
                    Position.y = Y;
                    Position.z = Z;
                }
                else if (Reader->is_value("rotate"))
                {
                    // Degrees -> radians (DAT_06fb6060), then the rotation matrix on the x87
                    // stack; the float casts are retail's stores.
                    float RotZ = Reader->get_value_float(2);
                    float RotY = Reader->get_value_float(1);
                    float RotX = Reader->get_value_float(0);
                    float const DegToRad = 0.0174532924f;
                    float AngleX = static_cast<float>(static_cast<double>(RotX) * DegToRad);
                    float AngleY = static_cast<float>(static_cast<double>(RotY) * DegToRad);
                    float AngleZ = static_cast<float>(static_cast<double>(RotZ) * DegToRad);

                    double SinX = std::sin(static_cast<double>(AngleX));
                    double CosX = std::cos(static_cast<double>(AngleX));
                    double SinY = std::sin(static_cast<double>(AngleY));
                    double CosY = std::cos(static_cast<double>(AngleY));  // stored as double
                    double SinZ = std::sin(static_cast<double>(AngleZ));
                    double CosZ = std::cos(static_cast<double>(AngleZ));

                    double SinYSinX = SinY * SinX;                        // kept on the stack
                    float SinZCosX = static_cast<float>(SinZ * CosX);
                    float CosZCosX = static_cast<float>(CosZ * CosX);

                    FMatrix Rotated;
                    Rotated.D[0][0] = static_cast<float>(CosZ * CosY);
                    Rotated.D[0][1] = static_cast<float>(SinYSinX * CosZ - SinZCosX);
                    Rotated.D[0][2] = static_cast<float>(CosZCosX * SinY + SinZ * SinX);
                    Rotated.D[1][0] = static_cast<float>(SinZ * CosY);
                    Rotated.D[1][1] = static_cast<float>(SinYSinX * SinZ + CosZCosX);
                    Rotated.D[1][2] = static_cast<float>(SinZCosX * SinY - CosZ * SinX);
                    Rotated.D[2][0] = static_cast<float>(-SinY);
                    Rotated.D[2][1] = static_cast<float>(CosY * SinX);
                    Rotated.D[2][2] = static_cast<float>(CosY * CosX);
                    Placement.Rotation = Rotated;
                }
                else if (Reader->is_value("archetype"))
                {
                    // Any other archetype ends the section's value loop.
                    if (_stricmp("Trade_Lane_Ring", Reader->get_value_string(0)) == 0)
                        bRing = true;
                    else if (_stricmp("jumpgate", Reader->get_value_string(0)) == 0)
                        bJumpGate = true;
                    else if (_stricmp("jumphole", Reader->get_value_string(0)) == 0)
                        bJumpHole = true;
                    else
                        break;
                }
                else if (Reader->is_value("prev_ring"))
                {
                    PrevRing = pub::GetNicknameId(Reader->get_value_string(0));
                }
                else if (Reader->is_value("next_ring"))
                {
                    NextRing = pub::GetNicknameId(Reader->get_value_string(0));
                }
                else if (Reader->is_value("goto"))
                {
                    pub::GetSystemID(GotoSystem, Reader->get_value_string(0));
                    GotoObject = pub::GetNicknameId(Reader->get_value_string(1));
                }
                else if (Reader->is_value("reputation"))
                {
                    pub::Reputation::GetReputationGroup(Reputation, Reader->get_value_string(0));
                }
            } while (Reader->read_value());

            if (bRing)
            {
                FTradeLaneRingInfo Ring;
                Ring.ObjectId = ObjectId;
                Ring.Position = Position;
                Ring.Placement = Placement;
                Ring.PrevRing = PrevRing;
                Ring.NextRing = NextRing;
                Ring.Reputation = Reputation;
                Rings.insert(std::pair<unsigned int const, FTradeLaneRingInfo>(ObjectId, Ring));
            }

            if (bJumpGate || bJumpHole)
            {
                FSystemJump Jump;
                Jump.ObjectId = ObjectId;
                Jump.Type = bJumpGate ? 1 : (bJumpHole ? 2 : -1);
                Jump.Position = Position;
                Jump.GotoSystem = GotoSystem;
                Jump.GotoObject = GotoObject;
                Jumps.insert(Jumps.end(), 1, Jump);
            }
        }
    }

    // Collect the zones.
    std::vector<FZonePathLabel> PathLabels;
    FSystemZoneEnumerator Enumerator;
    Enumerator.System = this;
    Enumerator.bLoadEncounters = bLoadEncounters;
    Enumerator.Zones = &Zones;
    Enumerator.PathLabels = &PathLabels;
    pub::System::EnumerateZones(SystemId, Enumerator);

    if (bLoadEncounters)
    {
        // The EncounterParameters objects only live for the enumeration.
        if (EncounterParameters != nullptr)
        {
            for (std::map<unsigned int, FEncounterParameters*>::iterator It =
                     EncounterParameters->begin();
                 It != EncounterParameters->end(); ++It)
            {
                if (It->second != nullptr)
                    delete It->second;
            }
            delete EncounterParameters;
        }
        EncounterParameters = nullptr;
    }

    ZoneById.clear();
    for (std::vector<FSystemZone>::iterator It = Zones.begin(); It != Zones.end(); ++It)
        ZoneById[It->ZoneId] = &*It;

    BuildPatrolPaths(&PathLabels);
    BuildTradeLanes(&Rings);

    if (bLoadEncounters)
    {
        for (std::vector<FSystemZone>::iterator It = Zones.begin(); It != Zones.end(); ++It)
        {
            if (It->Population.bHasPopulationKeys)
                It->Population.Validate();
        }
    }
}

// FUN_06f75840.
void FSystemInfo::Unload()
{
    if (--LoadCount != 0)
        return;
    Jumps.clear();
    ZoneById.clear();
    Zones.clear();
}

// FUN_06f748a0.
void FSystemInfo::LoadSolarNpcs(INI_Reader* Reader)
{
    while (Reader->read_header())
    {
        if (Reader->is_header("Object"))
        {
            // The 0x470 block.
            FSolarNpcDesc Desc;
            Desc.System = this;
            while (Reader->read_value())
                ParseSolarNpcKey(Reader, &Desc);                          // FUN_06ed5590

            // Docked = a base was given, or the named object has a docking target.
            bool bDocked;
            if (Desc.Nickname.Length != 0 && Desc.Base == nullptr)
            {
                unsigned int const ObjectId = pub::GetNicknameId(Desc.Nickname.Data);
                unsigned int DockTarget;
                pub::SpaceObj::GetDockingTarget(ObjectId, DockTarget);
                bDocked = DockTarget != 0;
            }
            else
            {
                bDocked = Desc.Base != nullptr;
            }

            // A named object with a behavior, a base, a docking target, a reputation or a
            // msg_id_prefix becomes a solar NPC.
            if (Desc.Nickname.Length != 0 &&
                (Desc.StateGraph != -1 || Desc.Base != nullptr || bDocked ||
                 Desc.ReputationGroup != 0xffffffffu || Desc.MsgIdPrefix.Length != 0))
            {
                FSolarNpc Npc(&Desc);                                     // FUN_06ed5720
                SolarNpcs.insert(SolarNpcs.end(), 1, Npc);
            }
        }
        else if (Reader->is_header("SystemInfo"))
        {
            while (Reader->read_value())
            {
                if (Reader->is_value("rpop_solar_detection"))
                    bRpopSolarDetection = Reader->get_value_bool(0);
            }
        }
    }
    Reader->reset();
}

// FUN_06f75940.
void FSystemInfo::LoadFull()
{
    INI_Reader Reader;
    pub::BuildSystemReader(Reader, SystemId);
    LoadSolarNpcs(&Reader);
    if (pub::SinglePlayer())
        SpacePopSetSolarDetection(bRpopSolarDetection);                   // FUN_06f5a7c0
    Reader.reset();
    LoadFromReader(&Reader, true);
    RebuildTradeLanes();
    for (std::vector<FSolarNpc>::iterator Npc = SolarNpcs.begin(); Npc != SolarNpcs.end(); ++Npc)
        Npc->SubmitPersonalityState(true);                                // FUN_06ed5960
}

// FUN_06f75bc0.
void FSystemInfo::UnloadFull()
{
    DestroyObjectsIfUnoccupied();
    for (std::vector<FSolarNpc>::iterator Npc = SolarNpcs.begin(); Npc != SolarNpcs.end(); ++Npc)
    {
        Npc->SubmitPersonalityState(false);                               // FUN_06ed5960
        Npc->DeleteReputationHandle();                                    // FUN_06ed5940
    }
    SolarNpcs.clear();
    MissionObjects.clear();
    DeferredDestroyObjects.clear();
    if (pub::SinglePlayer())
        SpacePopSetSolarDetection(1);
    // The body of FUN_06f75840, inlined.
    Unload();
}

// FUN_06f72a20: the re-find after the insert is discarded.
void FSystemInfo::AddPlayer(FContentPlayerState* Player)
{
    for (std::vector<FContentPlayerState*>::iterator It = Players.begin(); It != Players.end(); ++It)
    {
        if (*It == Player)
            return;
    }
    Players.insert(Players.end(), 1, Player);
}

// FUN_06f72c30.
void FSystemInfo::RemovePlayer(FContentPlayerState* Player)
{
    for (std::vector<FContentPlayerState*>::iterator It = Players.begin(); It != Players.end(); ++It)
    {
        if (*It == Player)
        {
            Players.erase(It);
            break;
        }
    }
    if (pub::SinglePlayer())
        DestroyObjectsIfUnoccupied();
}

// FUN_06f72c90: the argument is never read.
void FSystemInfo::OnPlayerChanged(FContentPlayerState* Unused)
{
    (void)Unused;
    if (pub::SinglePlayer())
        DestroyObjectsIfUnoccupied();
}

// FUN_06f72a80.
void FSystemInfo::DestroyObjectsIfUnoccupied()
{
    for (std::vector<FContentPlayerState*>::iterator It = Players.begin(); It != Players.end(); ++It)
    {
        if ((*It)->ShipObject != nullptr)
            return;
    }

    // Over copies of both lists; DestroySpaceObj is FUN_06f34670.
    std::list<FContentSpaceObj*> const SpaceObjsCopy(SpaceObjs.begin(), SpaceObjs.end());
    for (std::list<FContentSpaceObj*>::const_iterator It = SpaceObjsCopy.begin();
         It != SpaceObjsCopy.end(); ++It)
        (*It)->DestroySpaceObj();

    std::vector<unsigned int> const MissionObjectsCopy(MissionObjects);
    for (std::vector<unsigned int>::const_iterator It = MissionObjectsCopy.begin();
         It != MissionObjectsCopy.end(); ++It)
    {
        pub::SpaceObj::Destroy(*It, DESTROY_NORMAL);
        RemoveMissionObject(*It);
    }
}

// FUN_06f743e0.
void FSystemInfo::AddMissionObject(unsigned int SpaceObjId)
{
    MissionObjects.push_back(SpaceObjId);
}

// FUN_06f74620.
void FSystemInfo::RemoveMissionObject(unsigned int SpaceObjId)
{
    // The reputation out-param reuses the argument's stack slot in retail.
    for (std::vector<unsigned int>::iterator It = MissionObjects.begin();
         It != MissionObjects.end(); ++It)
    {
        if (*It != SpaceObjId)
            continue;
        MissionObjects.erase(It);
        int Reputation;
        if (pub::SpaceObj::GetRep(SpaceObjId, Reputation) == 0)
            pub::Reputation::Free(Reputation);
        break;
    }
    for (std::vector<unsigned int>::iterator It = DeferredDestroyObjects.begin(); It != DeferredDestroyObjects.end(); ++It)
    {
        if (*It == SpaceObjId)
        {
            DeferredDestroyObjects.erase(It);
            break;
        }
    }
}

// FUN_06f745a0.
void FSystemInfo::SetObjectFlag(unsigned int SpaceObjId, bool bFlag)
{
    std::vector<unsigned int>::iterator It = DeferredDestroyObjects.begin();
    while (It != DeferredDestroyObjects.end() && *It != SpaceObjId)
        ++It;
    if (bFlag)
    {
        if (It == DeferredDestroyObjects.end())
            DeferredDestroyObjects.insert(DeferredDestroyObjects.end(), 1, SpaceObjId);
    }
    else if (It != DeferredDestroyObjects.end())
    {
        DeferredDestroyObjects.erase(It);
    }
}

// FUN_06f746e0.
std::vector<unsigned int>* FSystemInfo::GetMissionObjects()
{
    return &MissionObjects;
}

// FUN_06f746f0. The loop body is RemoveMissionObject inlined.
void FSystemInfo::DestroyMissionObjects()
{
    std::vector<unsigned int> const MissionObjectsCopy(MissionObjects);
    for (std::vector<unsigned int>::const_iterator It = MissionObjectsCopy.begin();
         It != MissionObjectsCopy.end(); ++It)
    {
        pub::SpaceObj::Destroy(*It, DESTROY_NORMAL);
        RemoveMissionObject(*It);
    }
}

// FUN_06f74380.
void FSystemInfo::AddSpaceObj(FContentSpaceObj* Object)
{
    SpaceObjs.push_back(Object);
    Object->SetSystem(this);                                              // FUN_06eb7090
}

// FUN_06f74310.
void FSystemInfo::RemoveSpaceObj(unsigned int SpaceObjId, bool bDelete)
{
    for (std::list<FContentSpaceObj*>::iterator It = SpaceObjs.begin(); It != SpaceObjs.end(); ++It)
    {
        if ((*It)->SpaceObjId != SpaceObjId)
            continue;
        if (bDelete)
            delete *It;
        SpaceObjs.erase(It);
        return;
    }
}

// FUN_06f73930.
FContentSpaceObj* FSystemInfo::FindShip(unsigned int SpaceObjId)
{
    for (std::vector<FContentPlayerState*>::iterator It = Players.begin(); It != Players.end(); ++It)
    {
        FContentPlayerShip* Ship = (*It)->ShipObject;
        if (Ship != nullptr && Ship->SpaceObjId == SpaceObjId)
            return Ship;
    }
    for (std::list<FContentSpaceObj*>::iterator It = SpaceObjs.begin(); It != SpaceObjs.end(); ++It)
    {
        if ((*It)->SpaceObjId == SpaceObjId)
            return *It;
    }
    return nullptr;
}

// FUN_06f73a40.
FContentSpaceObj* FSystemInfo::FindShipOrSolar(unsigned int SpaceObjId)
{
    FContentSpaceObj* Found = FindShip(SpaceObjId);                        // inlined
    if (Found != nullptr)
        return Found;
    // No break: the last match wins.
    for (std::vector<FSolarNpc>::iterator Npc = SolarNpcs.begin(); Npc != SolarNpcs.end(); ++Npc)
    {
        if (Npc->SpaceObjId == SpaceObjId)
            Found = &*Npc;
    }
    return Found;
}

// FUN_06f73990.
FContentSpaceObj* FSystemInfo::FindSpaceObj(unsigned int SpaceObjId)
{
    for (std::list<FContentSpaceObj*>::iterator It = SpaceObjs.begin(); It != SpaceObjs.end(); ++It)
    {
        if ((*It)->SpaceObjId == SpaceObjId)
            return *It;
    }
    return nullptr;
}

// FUN_06f739c0 (strcmp inlined).
FContentSpaceObj* FSystemInfo::FindSpaceObjByNickname(char const* Nickname)
{
    for (std::list<FContentSpaceObj*>::iterator It = SpaceObjs.begin(); It != SpaceObjs.end(); ++It)
    {
        if (strcmp(Nickname, (*It)->Nickname.Data) == 0)
            return *It;
    }
    return nullptr;
}

// FUN_06f72980.
void FSystemInfo::SendPendingComms(unsigned int Arg1, unsigned int Unused2, unsigned int Key)
{
    (void)Unused2;
    for (std::list<FContentSpaceObj*>::iterator It = SpaceObjs.begin(); It != SpaceObjs.end(); ++It)
        (*It)->SendPendingComm(Arg1, Key, 1);                              // FUN_06eb6e00
    for (std::vector<FSolarNpc>::iterator Npc = SolarNpcs.begin(); Npc != SolarNpcs.end(); ++Npc)
        Npc->SendPendingComm(Arg1, Key, 1);
    for (std::vector<FContentPlayerState*>::iterator It = Players.begin(); It != Players.end(); ++It)
    {
        FContentPlayerShip* Ship = (*It)->ShipObject;
        if (Ship != nullptr)
            Ship->SendPendingComm(Arg1, Key, 1);
    }
}

// Squared distance from Point to Position as TU58's proximity loops compute it: the three
// differences stay on the x87 stack, summed as (z*z + x*x) + y*y (FUN_06f72dd0's
// (x*x + z*z) + y*y is the same sum).
static double DistanceSquaredBetween(Vector const* Point, Vector const* Position)
{
    double const DeltaX = static_cast<double>(Point->x) - Position->x;
    double const DeltaY = static_cast<double>(Point->y) - Position->y;
    double const DeltaZ = static_cast<double>(Point->z) - Position->z;
    return DeltaZ * DeltaZ + DeltaX * DeltaX + DeltaY * DeltaY;
}

// FUN_06f72dd0.
bool FSystemInfo::IsPlayerShipNear(Vector const* Point, float const* Radius, float* OutDistanceSq)
{
    // The square is stored as a float.
    float const RadiusSq = static_cast<float>(static_cast<double>(*Radius) * *Radius);
    *OutDistanceSq = -1.0f;
    for (std::vector<FContentPlayerState*>::iterator It = Players.begin(); It != Players.end(); ++It)
    {
        FContentPlayerShip* Ship = (*It)->ShipObject;
        if (Ship == nullptr)
            continue;
        Vector Position;
        Ship->GetPosition(&Position);                                     // FUN_06eb70f0
        double const DistanceSq = DistanceSquaredBetween(Point, &Position);
        if (DistanceSq <= RadiusSq)
        {
            *OutDistanceSq = -1.0f;
            return true;
        }
        // The first distance (-1.0f still stored) or a smaller one is kept; unordered is not.
        if (*OutDistanceSq == -1.0f || DistanceSq < *OutDistanceSq)
            *OutDistanceSq = static_cast<float>(DistanceSq);
    }
    return false;
}

// FUN_06f72ea0.
void FSystemInfo::FindPlayersNear(Vector const* Point, float const* Radius,
                                  std::vector<FContentPlayerState*>* Out)
{
    Out->clear();
    float const RadiusSq = static_cast<float>(static_cast<double>(*Radius) * *Radius);
    for (std::vector<FContentPlayerState*>::iterator It = Players.begin(); It != Players.end(); ++It)
    {
        FContentPlayerShip* Ship = (*It)->ShipObject;
        if (Ship == nullptr)
            continue;
        Vector Position;
        Ship->GetPosition(&Position);                                     // FUN_06eb70f0
        if (DistanceSquaredBetween(Point, &Position) <= RadiusSq)
            Out->push_back(*It);
    }
}

// FUN_06f730c0.
bool FSystemInfo::IsSeenByPlayerShip(Vector const* Point, float const* MaxDistance,
                                     float const* ConeAngle)
{
    for (std::vector<FContentPlayerState*>::iterator It = Players.begin(); It != Players.end(); ++It)
    {
        FContentPlayerShip* Ship = (*It)->ShipObject;
        if (Ship == nullptr)
            continue;
        FSpaceObjPlacement Placement;
        Ship->GetPlacement(&Placement);                                   // FUN_06eb70a0
        if (IsPointInViewCone(Point, &Placement, MaxDistance, ConeAngle))  // FUN_06f672d0
            return true;
    }
    return false;
}

// FUN_06f73130. Both ring outputs reuse the argument slots in retail; only the end ring is
// read.
bool FSystemInfo::IsNearPlayerTradeLaneEndRing(Vector const* Point, float const* Radius)
{
    float const RadiusSq = static_cast<float>(static_cast<double>(*Radius) * *Radius);
    for (std::vector<FContentPlayerState*>::iterator It = Players.begin(); It != Players.end(); ++It)
    {
        FContentPlayerShip* Ship = (*It)->ShipObject;
        if (Ship == nullptr || !Ship->IsInTradeLane())                      // FUN_06f57710
            continue;
        unsigned int EntryRing;
        unsigned int EndRing;
        Ship->GetTradeLaneRings(&EntryRing, &EndRing);                                // FUN_06f57720
        Vector Position;
        Matrix Orientation;
        pub::SpaceObj::GetLocation(EndRing, Position, Orientation);         // result ignored
        if (DistanceSquaredBetween(Point, &Position) <= RadiusSq)
            return true;
    }
    return false;
}

// FUN_06f73210.
bool FSystemInfo::IsNearPlayerTradeLaneSegment(Vector const* Point, float const* Radius)
{
    float const RadiusSq = static_cast<float>(static_cast<double>(*Radius) * *Radius);
    for (std::vector<FContentPlayerState*>::iterator It = Players.begin(); It != Players.end(); ++It)
    {
        FContentPlayerShip* Ship = (*It)->ShipObject;
        if (Ship == nullptr || !Ship->IsInTradeLane())                      // FUN_06f57710
            continue;
        unsigned int EntryRing;
        unsigned int EndRing;
        Ship->GetTradeLaneRings(&EntryRing, &EndRing);                                // FUN_06f57720
        Vector Start;
        Vector End;
        Matrix Orientation;
        pub::SpaceObj::GetLocation(EntryRing, Start, Orientation);
        pub::SpaceObj::GetLocation(EndRing, End, Orientation);
        Vector Closest;
        if (DistanceSquaredToSegment(Point, &Start, &End, &Closest) <= RadiusSq)
            return true;
    }
    return false;
}

// FUN_06f732e0.
int FSystemInfo::CreateNpcShip(FContentNpcShipParams const* Params, unsigned int DockTarget,
                               FContentNpcShip** OutShip)
{
    FContentNpcShipParams Copy(*Params);
    Copy.System = this;

    if ((DockTarget & GIdValueMask) != 0)
    {
        // TU53 FUN_06f60340: its bool is returned as the result.
        return QueueDockLaunch(&Copy, DockTarget, OutShip) ? 1 : 0;
    }

    // operator new(0x5b0); the ship takes System from the block.
    FContentNpcShip* Ship = new FContentNpcShip(&Copy);
    if (OutShip != nullptr)
        *OutShip = Ship;
    SpaceObjs.push_back(Ship);
    unsigned int const Controller = 0;
    FContentNpcShip* MsgShip = Ship;
    pub::Controller::_SendMessage(Controller, pub::Controller::EMessage::NpcShipCreated, &MsgShip);
    return 0;
}

// FUN_06f73810. Retail writes the created ship into the OutShips argument slot;
// CreateNpcShip writes it on every path, so a local suffices.
int FSystemInfo::CreateNpcShips(std::list<FContentNpcShipParams> const* ParamsList,
                                unsigned int DockTarget, std::list<FContentNpcShip*>* OutShips)
{
    FContentNpcShip* Ship = nullptr;
    for (std::list<FContentNpcShipParams>::const_iterator It = ParamsList->begin();
         It != ParamsList->end(); ++It)
    {
        int const Result = CreateNpcShip(&*It, DockTarget, &Ship);
        // A failed SpacePop ship stops the loop.
        if (Result != 0 && It->ControllerId == GSpacePopControllerId)
            return Result;
        if (OutShips != nullptr && Ship != nullptr)
            OutShips->push_back(Ship);
    }
    return 0;
}

// FUN_06f738c0.
void FSystemInfo::AddJumpingInSpaceObj(FContentSpaceObj* Object, unsigned int GateId)
{
    // The AddSpaceObj body (FUN_06f74380) inlined.
    SpaceObjs.push_back(Object);
    Object->SetSystem(this);                                              // FUN_06eb7090
    unsigned int const ShipId = Object->SpaceObjId;
    pub::SpaceObj::JumpIn(ShipId, GateId);
}

// FUN_06f73bb0: every object of SpaceObjs is taken as an NPC ship (no type check).
void FSystemInfo::SetNpcShipsCommEnabled(bool bEnabled)
{
    bool const bValue = !bEnabled;
    for (std::list<FContentSpaceObj*>::iterator It = SpaceObjs.begin(); It != SpaceObjs.end(); ++It)
        static_cast<FContentNpcShip*>(*It)->SetCommMuted(bValue);
}

// FUN_06f73bf0. The radii are one stack float rewritten before each call.
double FSystemInfo::GetNpcShipLifeRate(FContentNpcShip* Ship)
{
    float const PlayerNear = -1.0f;
    if (Ship->IsInTradeLane())                                              // FUN_06f57710
        return PlayerNear;

    float DistanceSq;
    float Radius;
    Vector Position;
    if (pub::SinglePlayer())
    {
        Radius = 2500.0f;
        Ship->GetPosition(&Position);
        if (IsPlayerShipNear(&Position, &Radius, &DistanceSq))
            return PlayerNear;

        // IsSeenByPlayerShip inlined.
        float const ConeAngle = 70.0f;
        float const MaxDistance = 4000.0f;
        Ship->GetPosition(&Position);
        for (std::vector<FContentPlayerState*>::iterator It = Players.begin(); It != Players.end(); ++It)
        {
            FContentPlayerShip* PlayerShip = (*It)->ShipObject;
            if (PlayerShip == nullptr)
                continue;
            FSpaceObjPlacement Placement;
            PlayerShip->GetPlacement(&Placement);                         // FUN_06eb70a0
            if (IsPointInViewCone(&Position, &Placement, &MaxDistance, &ConeAngle))
                return PlayerNear;
        }

        Radius = 4000.0f;
        Ship->GetPosition(&Position);
        if (IsNearPlayerTradeLaneEndRing(&Position, &Radius))
            return PlayerNear;
    }
    else
    {
        Radius = 2500.0f;
        Ship->GetPosition(&Position);
        if (IsPlayerShipNear(&Position, &Radius, &DistanceSq))
            return PlayerNear;
        Radius = 4000.0f;
        Ship->GetPosition(&Position);
        if (IsNearPlayerTradeLaneEndRing(&Position, &Radius))
            return PlayerNear;
    }

    Radius = 2500.0f;
    Ship->GetPosition(&Position);
    if (IsNearPlayerTradeLaneSegment(&Position, &Radius))
        return PlayerNear;

    // 6250000.0f is DAT_06fb6054, 15.0f DAT_06fbbf08; unordered gives 15.
    if (!(DistanceSq > 6250000.0f))
        return 15.0f;
    // Doubles DAT_06fbbc68 / DAT_06fb7a88 / DAT_06fb70c0.
    double const Rate = (std::sqrt(static_cast<double>(DistanceSq)) - 2500.0) * 0.01 + 1.0;
    if (!(Rate > 15.0f))
        return Rate;
    return 15.0f;
}

// FUN_06f73dd0.
void FSystemInfo::DestroyFlaggedObjectsAwayFromPlayers(float Unused)
{
    (void)Unused;
    if (DeferredDestroyObjects.empty())
        return;

    // The ids whose object has no player ship within 10000 (DAT_06fbbee4).
    std::vector<unsigned int> AwayIds;
    float const Radius = 10000.0f;
    for (std::vector<unsigned int>::iterator It = DeferredDestroyObjects.begin(); It != DeferredDestroyObjects.end(); ++It)
    {
        Vector Position;
        Matrix Orientation;
        pub::SpaceObj::GetLocation(*It, Position, Orientation);          // result ignored
        float DistanceSq;
        if (!IsPlayerShipNear(&Position, &Radius, &DistanceSq))
            AwayIds.insert(AwayIds.end(), 1, *It);
    }

    // The RemoveMissionObject body is inlined in retail.
    for (std::vector<unsigned int>::const_iterator It = AwayIds.begin(); It != AwayIds.end(); ++It)
    {
        pub::SpaceObj::Destroy(*It, DESTROY_NORMAL);
        RemoveMissionObject(*It);
    }
}

// The player-target test FUN_06f73fa0 inlines twice:
// true when the ship of some player of Players targets ShipId (pub::SpaceObj::GetTarget).
// Retail compares the out value whatever GetTarget returned; the port starts it at 0.
static bool IsTargetedByPlayerShip(std::vector<FContentPlayerState*> const& Players,
                                   unsigned int ShipId)
{
    for (std::vector<FContentPlayerState*>::const_iterator It = Players.begin(); It != Players.end(); ++It)
    {
        FContentPlayerShip* PlayerShip = (*It)->ShipObject;
        if (PlayerShip == nullptr)
            continue;
        unsigned int const PlayerShipId = PlayerShip->SpaceObjId;
        unsigned int TargetId = 0;
        pub::SpaceObj::GetTarget(PlayerShipId, TargetId);
        if (TargetId == ShipId)
            return true;
    }
    return false;
}

// FUN_06f73fa0.
void FSystemInfo::UpdateNpcShipLives(float DeltaTime)
{
    if (SpaceObjs.empty())
        return;

    std::list<FContentSpaceObj*> Expired;
    for (std::list<FContentSpaceObj*>::iterator It = SpaceObjs.begin(); It != SpaceObjs.end(); ++It)
    {
        FContentNpcShip* Ship = static_cast<FContentNpcShip*>(*It);

        // A formation member only has its life refilled.
        if (Ship->FormationSlot != 0 && Ship->FormationId != GNoFormationId)
        {
            if (Ship->LifeTime < Ship->Longevity)
                Ship->LifeTime = Ship->Longevity;
            continue;
        }
        // -1.0f (DAT_06fbbee0) is immortal.
        if (Ship->Longevity == -1.0f)
            continue;

        // The product stays on the x87 stack for the compare and is stored as a float for the
        // subtraction.
        double const Product = GetNpcShipLifeRate(Ship) * DeltaTime;
        float const Step = static_cast<float>(Product);
        if (Product < 0.0)
        {
            // A player is near: refill as above.
            if (Ship->LifeTime < Ship->Longevity)
                Ship->LifeTime = Ship->Longevity;
            continue;
        }
        // Less or unordered: skip.
        if (!(Ship->LifeTime >= 0.0f))
            continue;
        Ship->LifeTime = static_cast<float>(static_cast<double>(Ship->LifeTime) - Step);
        // Only a negative life expires.
        if (!(Ship->LifeTime < 0.0f))
            continue;
        Ship->LifeTime = 0.0f;

        if (pub::SinglePlayer() && IsTargetedByPlayerShip(Players, Ship->SpaceObjId))
            continue;

        if (Ship->FormationId == GNoFormationId)
        {
            Expired.push_back(Ship);
            continue;
        }

        // The whole formation expires together unless one of its ships is a player's
        // (FUN_06f49bc0) or, in single player, targeted by a player.
        std::vector<FContentShipBase*> Members;
        CollectFormationMembers(&Members, Ship->FormationId);             // FUN_06f66220
        bool bKeep = false;
        for (std::vector<FContentShipBase*>::iterator Member = Members.begin();
             Member != Members.end(); ++Member)
        {
            unsigned int const MemberId = (*Member)->SpaceObjId;
            if (FindContentPlayerStateByShip(&MemberId) != nullptr)
            {
                bKeep = true;
                break;
            }
            if (pub::SinglePlayer() && IsTargetedByPlayerShip(Players, MemberId))
            {
                bKeep = true;
                break;
            }
        }
        if (!bKeep)
        {
            for (std::vector<FContentShipBase*>::iterator Member = Members.begin();
                 Member != Members.end(); ++Member)
                Expired.push_back(*Member);
        }
    }

    for (std::list<FContentSpaceObj*>::iterator It = Expired.begin(); It != Expired.end(); ++It)
        (*It)->DestroySpaceObj();                                         // FUN_06f34670
}

// FUN_06f72cb0.
FSystemZone* FSystemInfo::FindZone(unsigned int ZoneId)
{
    std::map<unsigned int, FSystemZone*>::iterator It = ZoneById.find(ZoneId);
    if (It == ZoneById.end())
        return nullptr;
    return It->second;
}

// FUN_06f72d10.
FSystemZone* FSystemInfo::FindSmallestZoneAt(Vector const* Position)
{
    unsigned int ZoneIds[8];
    unsigned int Count;
    FSystemZone* Best = nullptr;
    float BestRadius = 3.40282347e+38f;                  // 0x7f7fffff
    pub::System::InZones(SystemId, *Position, 0.0f, ZoneIds, 8, Count);
    for (unsigned int Index = 0; Index < Count; ++Index)
    {
        // FUN_06f7b0f0 is map::find.
        std::map<unsigned int, FSystemZone*>::iterator It = ZoneById.find(ZoneIds[Index]);
        if (It == ZoneById.end())
            continue;
        FSystemZone* Zone = It->second;
        if (Zone == nullptr)
            continue;
        float Radius = pub::Zone::GetRadius(Zone->ZoneId);
        if (Radius < BestRadius)
        {
            BestRadius = Radius;
            Best = Zone;
        }
    }
    return Best;
}

// FUN_06f78ef0. For an unknown id FUN_06f79100 copies the head node's value (retail reads
// whatever the head holds; the port's find() == end() dereference faults instead). The copy
// is then assigned field by field.
void FSystemInfo::GetEncounterParameters(unsigned int Id, FEncounterParameters* Out)
{
    FEncounterParameters const Found = *EncounterParameters->find(Id)->second;
    Out->Nickname = Found.Nickname;
    Out->SystemId = Found.SystemId;
    Out->ZoneId = Found.ZoneId;
    Out->RerollInterval = Found.RerollInterval;
    Out->LastRerollTime = Found.LastRerollTime;
    Out->Difficulty = Found.Difficulty;
    Out->Permutations.clear();
    Out->Permutations.reserve(Found.Permutations.size());
    for (std::vector<FEncounterPermutation>::const_iterator It = Found.Permutations.begin();
         It != Found.Permutations.end(); ++It)
    {
        // Each permutation goes through a stack copy (FUN_06efa740).
        FEncounterPermutation const Copy(*It);
        Out->Permutations.insert(Out->Permutations.end(), Copy);
    }
    Out->CurrentPermutation = Found.CurrentPermutation;
    Out->Formations = Found.Formations;                                     // FUN_06f79980
    Out->Factions.Items.clear();
    Out->Factions.Items.reserve(Found.Factions.Items.size());
    for (std::vector<std::pair<unsigned int, float>>::const_iterator It =
             Found.Factions.Items.begin();
         It != Found.Factions.Items.end(); ++It)
        Out->Factions.Items.insert(Out->Factions.Items.end(), *It);
}

namespace
{
    // The comparator of the sort FUN_06f7f370: path labels by id, unsigned.
    bool PathLabelLess(FZonePathLabel const& A, FZonePathLabel const& B)
    {
        return A.Label.Id < B.Label.Id;
    }
}

// FUN_06f76380.
int FSystemInfo::BuildPatrolPaths(std::vector<FZonePathLabel>* Labels)
{
    // FUN_06f7f370, retail's std::sort instance (_Sort FUN_06f7f990, _Median FUN_06f7fea0,
    // _Unguarded_insert FUN_06f7fba0, _Insertion_sort FUN_06f7fc80). ContentSort keeps its
    // order of labels with equal ids -- the zones of one path_label reach the FPatrolPath ctor
    // in it.
    ContentSort(Labels->data(), Labels->data() + Labels->size(), PathLabelLess);

    Paths.clear();

    // One path per run of equal labels; the run is then erased.
    while (!Labels->empty())
    {
        FZonePathLabel* const Begin = Labels->data();
        FZonePathLabel* const End = Begin + Labels->size();
        FZonePathLabel* First = Begin;
        FZonePathLabel* Last = Begin;
        while (Last != End && Last->Label.Id == First->Label.Id)
            ++Last;

        {
            // The local path is inserted and destroyed before the run is erased.
            FPatrolPath Path(&First, &Last, this);                        // FUN_06f37110
            Paths.insert(Paths.end(), Path);
        }
        Labels->erase(Labels->begin() + (First - Begin), Labels->begin() + (Last - Begin));
    }

    // Every zone of a path points at it. Retail dereferences the zone without a test (the
    // path's zones all came from this system's zone list).
    for (std::vector<FPatrolPath>::iterator Path = Paths.begin(); Path != Paths.end(); ++Path)
    {
        std::vector<unsigned int> PathZoneIds;
        Path->GetZoneIds(&PathZoneIds);                                   // FUN_06f37ff0
        for (std::vector<unsigned int>::iterator ZoneId = PathZoneIds.begin();
             ZoneId != PathZoneIds.end(); ++ZoneId)
        {
            std::map<unsigned int, FSystemZone*>::iterator Found = ZoneById.find(*ZoneId);
            FSystemZone* Zone = (Found != ZoneById.end()) ? Found->second : nullptr;
            Zone->SetPath(&*Path);                                        // FUN_06fa8120
        }
    }

    // Up to 32 objects of the system, any distance.
    unsigned int ObjectIds[0x20];
    Vector Origin;
    Origin.x = 0.0f;
    Origin.y = 0.0f;
    Origin.z = 0.0f;
    unsigned int ObjectCount;
    pub::System::ScanObjects(SystemId, ObjectIds, 0x20, Origin, -1.0f,
                             OBJ_DOCKING_RING | OBJ_JUMP_GATE | OBJ_STATION | OBJ_AIRLOCK_GATE |
                             OBJ_JUMP_HOLE | OBJ_CAPITAL | OBJ_MINING, ObjectCount);   // 0x600d60

    // sqrt of DAT_06fbbee8, stored as float.
    float const Radius = static_cast<float>(std::sqrt(static_cast<double>(25000000.0f)));

    for (unsigned int ObjectIndex = 0; ObjectIndex < ObjectCount; ++ObjectIndex)
    {
        Vector Position;
        Matrix Orientation;
        pub::SpaceObj::GetLocation(ObjectIds[ObjectIndex], Position, Orientation);
        unsigned int ZoneIds[0x4b];
        unsigned int ZoneCount;
        pub::System::InZones(SystemId, Position, Radius, ZoneIds, 0x4b, ZoneCount);

        // The paths of those zones, each once.
        std::vector<FPatrolPath*> NearPaths;
        for (unsigned int ZoneIndex = 0; ZoneIndex < ZoneCount; ++ZoneIndex)
        {
            std::map<unsigned int, FSystemZone*>::iterator Found = ZoneById.find(ZoneIds[ZoneIndex]);
            if (Found == ZoneById.end())
                continue;
            FSystemZone* Zone = Found->second;
            if (Zone == nullptr || Zone->Path == nullptr)
                continue;
            FPatrolPath* Path = Zone->GetPath();                          // FUN_06fa8130
            bool bKnown = false;
            for (std::vector<FPatrolPath*>::iterator It = NearPaths.begin(); It != NearPaths.end(); ++It)
            {
                if (*It == Path)
                {
                    bKnown = true;
                    break;
                }
            }
            if (!bKnown)
                NearPaths.insert(NearPaths.end(), Path);                  // FUN_06eb5770
        }

        for (std::vector<FPatrolPath*>::iterator It = NearPaths.begin(); It != NearPaths.end(); ++It)
            (*It)->RegisterObject(ObjectIds[ObjectIndex], &Position);     // FUN_06f378c0
    }

    return static_cast<int>(Paths.size());
}

// The ring-record half of FUN_06f76b10 (inlined three times).
static FTradeLaneRing MakeTradeLaneRing(FTradeLaneRingInfo const& Info)
{
    FTradeLaneRing Ring;
    Ring.ObjectId = Info.ObjectId;
    Ring.Position = Info.Position;
    Ring.Placement = Info.Placement;
    Ring.Reputation = Info.Reputation;
    return Ring;
}

// Inlined in FUN_06f76b10 / FUN_06f77090 (transform FUN_06f143d0). Start/End stay unset.
FTradeLane::FTradeLane()
{
    SetMatrixIdentity(&Placement.Rotation);
    Placement.Translation.X = 0.0f;
    Placement.Translation.Y = 0.0f;
    Placement.Translation.Z = 0.0f;
}

// FUN_06f77e00.
bool FSystemInfo::FindTradeLaneByRing(unsigned int const* RingId, FTradeLane** OutLane)
{
    std::map<unsigned int, FTradeLane*>::iterator It = TradeLaneByRing.find(*RingId);
    if (It == TradeLaneByRing.end())
    {
        *OutLane = nullptr;
        return false;
    }
    *OutLane = It->second;
    return true;
}

// FUN_06f77bd0; the vector operator= is inlined.
bool FSystemInfo::GetTradeLaneRingsByRing(unsigned int const* RingId,
                                          std::vector<FTradeLaneRing>* OutRings)
{
    std::map<unsigned int, FTradeLane*>::iterator It = TradeLaneByRing.find(*RingId);
    if (It == TradeLaneByRing.end())
        return false;
    *OutRings = It->second->Rings;
    return true;
}

namespace
{
    // FUN_06f778b0 (thiscall on the transform).
    void SetRingPlacement(FTransform* Out, Matrix const* Orientation)
    {
        for (int Row = 0; Row < 3; ++Row)
        {
            for (int Column = 0; Column < 3; ++Column)
                Out->Rotation.D[Row][Column] = Orientation->E[Row][Column];
        }
        Out->Translation.X = 0.0f;
        Out->Translation.Y = 0.0f;
        Out->Translation.Z = 0.0f;
    }

    // One live ring of FUN_06f77090 (inlined three times), reputation -1.
    FTradeLaneRing MakeLiveRing(unsigned int RingId)
    {
        Vector Position;
        Matrix Orientation;
        pub::SpaceObj::GetLocation(RingId, Position, Orientation);
        FTradeLaneRing Ring;
        SetRingPlacement(&Ring.Placement, &Orientation);                    // FUN_06f778b0
        Ring.ObjectId = RingId;
        Ring.Position = Position;
        Ring.Reputation = 0xffffffffu;
        return Ring;
    }
}

// FUN_06f77090.
int FSystemInfo::RebuildTradeLanes()
{
    TradeLanes.clear();

    // Every trade lane ring of the system (type 0x80, radius -1).
    unsigned int Rings[0x80];
    unsigned int RingCount = 0;
    Vector Origin;
    Origin.x = 0.0f;
    Origin.y = 0.0f;
    Origin.z = 0.0f;
    pub::System::ScanObjects(SystemId, Rings, 0x80, Origin, -1.0f, OBJ_TRADELANE_RING, RingCount);

    for (unsigned int Index = 0; Index < RingCount; ++Index)
    {
        if (Rings[Index] == 0)
            continue;
        std::vector<FTradeLaneRing> Chain;
        Chain.insert(Chain.end(), MakeLiveRing(Rings[Index]));
        unsigned int Next;
        unsigned int Prev;
        pub::SpaceObj::GetTradelaneNextAndPrev(Rings[Index], Next, Prev);

        // The next rings go to the back; each one is taken off the scan list -- the first
        // match from Index on, else every match below Index.
        while (Next != 0)
        {
            Chain.insert(Chain.end(), 1, MakeLiveRing(Next));
            unsigned int Slot = Index;
            for (; Slot < RingCount; ++Slot)
            {
                if (Rings[Slot] == Next)
                {
                    Rings[Slot] = 0;
                    break;
                }
            }
            if (Slot == RingCount)
            {
                for (int Below = static_cast<int>(Index) - 1; Below >= 0; --Below)
                {
                    if (Rings[Below] == Next)
                        Rings[Below] = 0;
                }
            }
            unsigned int const Current = Next;
            pub::SpaceObj::GetTradelaneNextAndPrev(Current, Next, Prev);
        }

        // The previous rings go to the front; every match below Index and the first from
        // Index on leave the scan list.
        pub::SpaceObj::GetTradelaneNextAndPrev(Rings[Index], Next, Prev);
        while (Prev != 0)
        {
            Chain.insert(Chain.begin(), 1, MakeLiveRing(Prev));
            for (int Below = static_cast<int>(Index) - 1; Below >= 0; --Below)
            {
                if (Rings[Below] == Prev)
                    Rings[Below] = 0;
            }
            for (unsigned int Slot = Index; Slot < RingCount; ++Slot)
            {
                if (Rings[Slot] == Prev)
                {
                    Rings[Slot] = 0;
                    break;
                }
            }
            unsigned int const Current = Prev;
            pub::SpaceObj::GetTradelaneNextAndPrev(Current, Next, Prev);
        }

        FTradeLane Lane;
        Lane.Rings = Chain;
        Lane.StartPosition = Chain.front().Position;
        Lane.Placement = Chain.front().Placement;
        Lane.EndPosition = Chain.back().Position;
        TradeLanes.insert(TradeLanes.end(), 1, Lane);
    }

    // The rings of every lane enter TradeLaneByRing and get their attack candidates; the
    // affiliation carries over from ring to ring when a lookup fails.
    for (std::vector<FTradeLane>::iterator Lane = TradeLanes.begin(); Lane != TradeLanes.end(); ++Lane)
    {
        unsigned int Affiliation = 0xffffffffu;
        float LaneMax = 0.0f;
        for (std::vector<FTradeLaneRing>::size_type RingIndex = 0; RingIndex < Lane->Rings.size(); ++RingIndex)
        {
            FTradeLaneRing const Ring = Lane->Rings[RingIndex];
            TradeLaneByRing[Ring.ObjectId] = &*Lane;
            int Rep = -1;
            pub::SpaceObj::GetSolarRep(Ring.ObjectId, Rep);
            pub::Reputation::GetAffiliation(Rep, Affiliation);

            FTradeLaneRingCandidates Found;
            Found.RingIndex = static_cast<int>(RingIndex);
            CollectRingCandidates(&Ring, &Ring.Position, &Affiliation, &Found);   // FUN_06f778e0

            // The ring's largest weight, then the lane's; NaN never replaces.
            float RingMax = 0.0f;
            for (std::vector<std::pair<FSystemZone*, float>>::const_iterator It = Found.Candidates.begin();
                 It != Found.Candidates.end(); ++It)
            {
                if (RingMax < It->second)
                    RingMax = It->second;
            }
            if (RingMax > LaneMax)
                LaneMax = RingMax;
            if (!Found.Candidates.empty())
            {
                FTradeLaneRingCandidates Entry;
                Entry.RingIndex = Found.RingIndex;
                Entry.Candidates = Found.Candidates;
                Entry.MaxWeight = RingMax;
                Lane->RingCandidates.insert(Lane->RingCandidates.end(), 1, Entry);
            }
        }
        Lane->MaxWeight = LaneMax;
    }
    return static_cast<int>(TradeLanes.size());
}

// FUN_06f778e0.
void FSystemInfo::CollectRingCandidates(FTradeLaneRing const* /*Ring*/, Vector const* Position,
                                        unsigned int const* Affiliation,
                                        FTradeLaneRingCandidates* Out)
{
    std::vector<FSystemZone*> AtShip;
    std::vector<FSystemZone*> Near;
    std::vector<FSystemZone*> Ahead;
    FSpaceObjPlacement Placement;
    for (int Row = 0; Row < 3; ++Row)
    {
        for (int Column = 0; Column < 3; ++Column)
            Placement.Orientation.E[Row][Column] = Row == Column ? 1.0f : 0.0f;   // FUN_06efc7a0
    }
    Placement.Position = *Position;
    bool bBlocked;
    CollectSpacePopZones(this, &Placement, &AtShip, &Near, &Ahead, &bBlocked);  // FUN_06f5b000

    // The zones at the ring, then the near ones. Retail keeps a running maximum that is
    // never read.
    for (int Pass = 0; Pass < 2; ++Pass)
    {
        std::vector<FSystemZone*> const& Zones = Pass == 0 ? AtShip : Near;
        for (std::vector<FSystemZone*>::const_iterator It = Zones.begin(); It != Zones.end(); ++It)
        {
            FSystemZone* Zone = *It;
            if (Zone->Path == nullptr)
                continue;
            float HostileFrom;
            float HostileTo;
            GetHostileFactionShares(Zone->GetPopulationFactions(), Affiliation, &HostileFrom,
                                    &HostileTo);                           // FUN_06fa7fb0 / FUN_06f35190
            if (!(HostileTo > 0.0f))
                continue;
            float const Weight = Zone->GetTradelaneAttack();                // FUN_06fa7fd0
            if (Weight > 0.0f)
                Out->Candidates.insert(Out->Candidates.end(), 1,
                                       std::pair<FSystemZone*, float>(Zone, Weight));
        }
    }
}

namespace
{
    // Inlined twice in FUN_06f782e0. Source arrives as a by-value copy (Common weighted_vector
    // copy ctor), so Out may be the object Source was copied from (the encounter loop).
    void CollectHostileFactions(weighted_vector<unsigned int> Source, unsigned int Affiliation,
                                weighted_vector<unsigned int>* Out)
    {
        Out->Items.clear();
        for (std::vector<std::pair<unsigned int, float>>::const_iterator It =
                 Source.Items.begin();
             It != Source.Items.end(); ++It)
        {
            if ((GetFactionFeelingMask(It->first, Affiliation) & 4) != 0)   // FUN_06f350f0
                Out->Items.push_back(*It);
        }
    }
}

// FUN_06f782e0.
void FSystemInfo::SpawnTradeLaneAttack(FContentPlayerShip* PlayerShip, int const* Direction,
                                       int EntryIndex, FTradeLane** Lane)
{
    // The player state is not null-tested.
    unsigned int ShipId = PlayerShip->SpaceObjId;
    if ((FindContentPlayerStateByShip(&ShipId)->GetFlags() & PLAYER_FLAG_MISSION_DEST_SYSTEM) != 0)
        return;

    // 180.0 is DAT_06fbbf90; otherwise only a LastAttackTime equal to 0 goes on (NaN returns).
    FTradeLane* const TheLane = *Lane;
    if (!(GetContentGameTime() - static_cast<double>(TheLane->LastAttackTime) > 180.0)
        && TheLane->LastAttackTime != 0.0f)
        return;
    TheLane->LastAttackTime = static_cast<float>(GetContentGameTime());

    // 2^-15 is DAT_06fb4448; unordered returns.
    if (!(static_cast<double>(rand()) * (1.0 / 32768.0) < static_cast<double>(TheLane->MaxWeight)))
        return;

    // A destroyed ring (relative health equal to 0) stops it. A failed call leaves retail's
    // slot holding the ship id -- non-zero, so it goes on.
    for (std::vector<FTradeLaneRing>::iterator Ring = TheLane->Rings.begin();
         Ring != TheLane->Rings.end(); ++Ring)
    {
        float Health = 1.0f;
        pub::SpaceObj::GetRelativeHealth(Ring->ObjectId, Health);
        if (Health == 0.0f)
            return;
    }

    // Over a copy of the ring entries: draw one by weight until a ring ahead of the ship --
    // beyond the next ring in the travel direction -- has no player ship within 5000
    // (DAT_06fbbf04); every other draw is erased.
    std::vector<FTradeLaneRingCandidates> Candidates = TheLane->RingCandidates;
    FTradeLaneRingCandidates* Selected = nullptr;
    // Retail uses one stack slot for this distance and GetSolarRep's out-param below, so a
    // failed GetSolarRep leaves the selected ring's distance bits there.
    float DistanceSq = 0.0f;
    while (!Candidates.empty() && Selected == nullptr)
    {
        FTradeLaneRingCandidates* Pick = PickByWeight(
            Candidates.data(), Candidates.data() + Candidates.size(),
            [](FTradeLaneRingCandidates const& Entry) { return Entry.MaxWeight; });
        int const Travel = *Direction;
        if ((Travel == 1 && Pick->RingIndex > EntryIndex + 1)
            || (Travel == -1 && Pick->RingIndex < EntryIndex - 1))
        {
            FTradeLaneRing const Ring = TheLane->Rings[Pick->RingIndex];
            float const Radius = 5000.0f;
            if (!IsPlayerShipNear(&Ring.Position, &Radius, &DistanceSq))
            {
                Selected = Pick;
                continue;
            }
        }
        Candidates.erase(Candidates.begin() + (Pick - Candidates.data()));
    }
    if (Selected == nullptr)
        return;

    // One of the ring's attack zones by weight.
    FSystemZone* Zone = PickByWeight(
        Selected->Candidates.data(), Selected->Candidates.data() + Selected->Candidates.size(),
        [](std::pair<FSystemZone*, float> const& Entry) { return Entry.second; })->first;

    std::vector<int> Indices;
    Indices.reserve(Zone->Population.Encounters.size());

    // The ring's owner (results ignored). Rep shares the DistanceSq slot (see above); a
    // failed GetSolarRep keeps those bits. GetAffiliation stores -1 on failure.
    FTradeLaneRing const Ring = TheLane->Rings[Selected->RingIndex];
    int Rep;
    std::memcpy(&Rep, &DistanceSq, sizeof(Rep));
    unsigned int Affiliation = 0;
    pub::SpaceObj::GetSolarRep(Ring.ObjectId, Rep);
    pub::Reputation::GetAffiliation(Rep, Affiliation);

    // The encounters whose factions hold one hostile to the owner.
    for (int Index = 0; static_cast<unsigned int>(Index) < Zone->Population.Encounters.size(); ++Index)
    {
        // Hostile starts as a copy of the encounter's factions and is then filtered in place
        // from a second copy.
        weighted_vector<unsigned int> Hostile(Zone->Population.Encounters[Index].Factions);
        CollectHostileFactions(Hostile, Affiliation, &Hostile);
        if (!Hostile.Items.empty())
            Indices.push_back(Index);
    }

    // One of them uniformly (signed, truncating) and its first formation; neither the index
    // list nor the formation vector is tested for empty, as retail.
    int const Count = static_cast<int>(Indices.size());
    int const Picked = Indices.data()[(rand() * Count) / 32768];
    FEncounterFormation* Formation = Zone->Population.Encounters[Picked].Formations.data();

    weighted_vector<unsigned int> HostileFactions;
    CollectHostileFactions(Formation->Factions, Affiliation, &HostileFactions);

    // A "TLParams" object (type 3); the result is not tested. The ref holder releases through
    // FUN_06f28e10.
    TRefHandle<FRandEncParams> Params;
    CreateRandEncParamsByType(3, &Params.Pointer);                          // FUN_06efb8e0
    FRandEncTLParams* Attack = static_cast<FRandEncTLParams*>(Params.Pointer);

    Attack->TargetTradelane = Ring.ObjectId;
    Attack->TargetObjectId = Ring.ObjectId;
    Attack->GotoTargetId = Ring.ObjectId;
    Attack->Direction = *Direction;
    Attack->TargetZone = Zone;

    // The attackers' faction by weight (End dereferenced when none, as retail).
    Attack->AttackerRep = static_cast<int>(PickByWeight(
        HostileFactions.Items.data(), HostileFactions.Items.data() + HostileFactions.Items.size(),
        [](std::pair<unsigned int, float> const& Entry) { return Entry.second; })->first);

    Attack->System = this;
    Attack->FormationsToSpawn = 1;
    FRandEncPosition Position;
    Position.Position.x = 0.0f;
    Position.Position.y = 0.0f;
    Position.Position.z = static_cast<float>(*Direction * 1400);
    Position.Weight = 80.0f;
    Attack->Positions.push_back(Position);
    Attack->FormationsToSpawn = 1;
    Attack->ControllerId = GSpacePopControllerId;                           // DAT_06fd0e64

    // Per formation to spawn, the encounter formation as a formation line: formation_by_class
    // through the faction's formations (GetFormation FUN_06f4f3d0, result ignored), else
    // formation.
    for (int Formed = 0; Formed < Attack->FormationsToSpawn; ++Formed)
    {
        FRandEncFormation Entry;
        Entry.Nickname.clear();
        if (!Formation->FormationByClass.IsEmpty())
            GetFactionProps(&Formation->Faction)->GetFormation(&Formation->FormationByClass.Id,
                                                               &Entry.Nickname.Id);
        else
            Entry.Nickname.Id = Formation->Formation.Id;
        Entry.Formation = FindFormation(&Entry.Nickname.Id);                // FUN_06efd1a0
        Entry.MinCount = 0;
        Entry.MaxCount = 0;
        for (std::vector<FEncounterShip>::const_iterator Line = Formation->Ships.begin();
             Line != Formation->Ships.end(); ++Line)
        {
            Entry.MinCount += Line->MinCount;
            Entry.MaxCount += Line->MaxCount;
            FRandEncShipChoice Choice;
            Choice.ShipClasses = Line->ShipClasses;
            Choice.Weight = 1.0f;
            Entry.ShipChoices.push_back(Choice);
        }
        // A copy of Entry gets weight 1.0 and is appended; the copy, then Entry, die.
        {
            FRandEncFormation Weighted(Entry);
            Weighted.Weight = 1.0f;
            Attack->Formations.push_back(Weighted);
        }
    }

    // Slot 4: the TL override FUN_06f83af0.
    Attack->Spawn(false);
}

// FUN_06f78110. Retail reads the head node's value for an unknown ring.
void FSystemInfo::AddTradeLaneShip(unsigned int const* RingId, int const* Direction,
                                   unsigned int const* ShipId)
{
    FTradeLane* TheLane = TradeLaneByRing.find(*RingId)->second;
    if (*Direction == 1)
        TheLane->ForwardShips.push_back(*ShipId);
    else
        TheLane->BackwardShips.push_back(*ShipId);
}

// FUN_06f78dd0.
void FSystemInfo::RemoveTradeLaneShip(unsigned int const* ShipId, unsigned int const* RingId)
{
    // The ring's lane (find not tested), else the first.
    FTradeLane* TheLane = *RingId != 0 ? TradeLaneByRing.find(*RingId)->second
                                       : TradeLanes.data();
    // Set by the first removal and never reset, so later lanes skip BackwardShips.
    bool bRemoved = false;
    while (TheLane != TradeLanes.data() + TradeLanes.size())
    {
        std::vector<unsigned int>::iterator Found =
            std::find(TheLane->ForwardShips.begin(), TheLane->ForwardShips.end(), *ShipId);
        if (Found != TheLane->ForwardShips.end())
        {
            TheLane->ForwardShips.erase(Found);
            TheLane->LastForwardLeaveTime = static_cast<float>(GetContentGameTime());
            bRemoved = true;
        }
        if (!bRemoved)
        {
            Found = std::find(TheLane->BackwardShips.begin(), TheLane->BackwardShips.end(), *ShipId);
            if (Found != TheLane->BackwardShips.end())
            {
                TheLane->BackwardShips.erase(Found);
                TheLane->LastBackwardLeaveTime = static_cast<float>(GetContentGameTime());
                bRemoved = true;
            }
        }
        if (*RingId != 0)
            break;
        ++TheLane;
    }
}

// FUN_06f78ee0.
std::vector<FContentPlayerState*>* FSystemInfo::GetPlayers()
{
    return &Players;
}

// FUN_06f767c0.
std::vector<FTradeLane>* FSystemInfo::GetTradeLanes()
{
    return &TradeLanes;
}

// FUN_06f767d0.
void FSystemInfo::FindTradeLanesNear(Vector const* Position, float Radius,
                                     std::vector<FTradeLane*>* Out)
{
    Out->clear();

    // -1.0f is DAT_06fb5e84.
    if (Radius == -1.0f)
    {
        for (std::vector<FTradeLane>::iterator Lane = TradeLanes.begin();
             Lane != TradeLanes.end(); ++Lane)
        {
            FTradeLane* LanePtr = &*Lane;
            std::vector<FTradeLane*>::iterator Found = Out->begin();
            while (Found != Out->end() && *Found != LanePtr)
                ++Found;
            if (Found == Out->end())
                Out->push_back(LanePtr);
        }
        return;
    }

    // The squared radius is stored as a float.
    float const RadiusSquared = Radius * Radius;
    for (std::vector<FTradeLane>::iterator Lane = TradeLanes.begin();
         Lane != TradeLanes.end(); ++Lane)
    {
        Vector Closest;
        double const Distance = DistanceSquaredToSegment(Position, &Lane->StartPosition,
                                                         &Lane->EndPosition, &Closest);
        // Unordered skips.
        if (!(Distance < static_cast<double>(RadiusSquared)))
            continue;
        FTradeLane* LanePtr = &*Lane;
        std::vector<FTradeLane*>::iterator Found = Out->begin();
        while (Found != Out->end() && *Found != LanePtr)
            ++Found;
        if (Found == Out->end())
            Out->insert(Out->end(), 1, LanePtr);  // retail's re-find is discarded
    }
}

// FUN_06f792c0: both ring lists non-empty and their first rings have the same id.
static bool SameFirstRing(std::vector<FTradeLaneRing> const& List, FTradeLane const& Lane)
{
    if (List.empty() || Lane.Rings.empty())
        return false;
    return List.front().ObjectId == Lane.Rings.front().ObjectId;
}

// FUN_06f76970.
void FSystemInfo::FindTradeLaneRingListsNear(Vector const* Position, float Radius,
                                             std::vector<std::vector<FTradeLaneRing>>* Out)
{
    Out->clear();

    if (Radius == -1.0f)                          // as in FUN_06f767d0
    {
        for (std::vector<FTradeLane>::iterator Lane = TradeLanes.begin();
             Lane != TradeLanes.end(); ++Lane)
        {
            std::vector<std::vector<FTradeLaneRing>>::iterator Found = Out->begin();
            while (Found != Out->end() && !SameFirstRing(*Found, *Lane))
                ++Found;
            if (Found == Out->end())
                Out->push_back(Lane->Rings);
        }
        return;
    }

    float const RadiusSquared = Radius * Radius;
    for (std::vector<FTradeLane>::iterator Lane = TradeLanes.begin();
         Lane != TradeLanes.end(); ++Lane)
    {
        Vector Closest;
        double const Distance = DistanceSquaredToSegment(Position, &Lane->StartPosition,
                                                         &Lane->EndPosition, &Closest);
        if (!(Distance < static_cast<double>(RadiusSquared)))
            continue;
        std::vector<std::vector<FTradeLaneRing>>::iterator Found = Out->begin();
        while (Found != Out->end() && !SameFirstRing(*Found, *Lane))
            ++Found;
        if (Found == Out->end())
            Out->insert(Out->end(), 1, Lane->Rings);
    }
}

// FUN_06f76b10.
int FSystemInfo::BuildTradeLanes(std::map<unsigned int, FTradeLaneRingInfo>* Rings)
{
    TradeLanes.clear();

    // Each lane restarts from begin().
    while (!Rings->empty())
    {
        std::map<unsigned int, FTradeLaneRingInfo>::iterator It = Rings->begin();
        FTradeLane Lane;

        Lane.Rings.insert(Lane.Rings.end(), MakeTradeLaneRing(It->second));
        Lane.Placement.Rotation = It->second.Placement.Rotation;
        if ((It->second.PrevRing & GIdValueMask) == 0)
            Lane.StartPosition = It->second.Position;
        if ((It->second.NextRing & GIdValueMask) == 0)
            Lane.EndPosition = It->second.Position;

        unsigned int const FirstId = It->second.ObjectId;
        unsigned int PrevKey = It->second.PrevRing;
        unsigned int NextKey = It->second.NextRing;
        Rings->erase(It);

        // Walk prev_ring; each ring goes in front of the one reached before it (the end when
        // that one is not in the list).
        unsigned int LastId = FirstId;
        It = Rings->find(PrevKey);
        while (It != Rings->end())
        {
            std::vector<FTradeLaneRing>::iterator Before = Lane.Rings.begin();
            while (Before != Lane.Rings.end() && Before->ObjectId != LastId)
                ++Before;
            Lane.Rings.insert(Before, 1, MakeTradeLaneRing(It->second));
            if ((It->second.PrevRing & GIdValueMask) == 0)
                Lane.StartPosition = It->second.Position;
            LastId = PrevKey;
            PrevKey = It->second.PrevRing;
            Rings->erase(It);
            It = Rings->find(PrevKey);
        }

        // Walk next_ring from the first ring; each ring goes right after the one reached
        // before it. That one is always in the list (it was inserted in this pass), so
        // retail's "end + 0x44" for a miss is never taken.
        LastId = FirstId;
        It = Rings->find(NextKey);
        while (It != Rings->end())
        {
            std::vector<FTradeLaneRing>::iterator After = Lane.Rings.begin();
            while (After != Lane.Rings.end() && After->ObjectId != LastId)
                ++After;
            Lane.Rings.insert(After + 1, 1, MakeTradeLaneRing(It->second));
            if ((It->second.NextRing & GIdValueMask) == 0)
                Lane.EndPosition = It->second.Position;
            LastId = NextKey;
            NextKey = It->second.NextRing;
            Rings->erase(It);
            It = Rings->find(NextKey);
        }

        TradeLanes.insert(TradeLanes.end(), 1, Lane);
    }

    return static_cast<int>(TradeLanes.size());
}

// TU53 FUN_06f611a0.
double __cdecl DistanceSquaredToSegment(Vector const* Point, Vector const* Start,
                                        Vector const* End, Vector* OutClosest)
{
    // Both differences go through FUN_06eb7810 (float stores).
    float const ToPointX = Point->x - Start->x;
    float const ToPointY = Point->y - Start->y;
    float const ToPointZ = Point->z - Start->z;
    float const AlongX = End->x - Start->x;
    float const AlongY = End->y - Start->y;
    float const AlongZ = End->z - Start->z;

    // z, y, x order; the quotient stays on the x87 stack for the first compare and is stored
    // as a float for the rest.
    double const Dot = static_cast<double>(AlongZ) * ToPointZ
                     + static_cast<double>(AlongY) * ToPointY
                     + static_cast<double>(AlongX) * ToPointX;
    double const LengthSquared = static_cast<double>(AlongZ) * AlongZ
                               + static_cast<double>(AlongY) * AlongY
                               + static_cast<double>(AlongX) * AlongX;
    double const Fraction = Dot / LengthSquared;
    float const StoredFraction = static_cast<float>(Fraction);

    // Unordered interpolates.
    bool bInterpolated = false;
    if (Fraction > 1.0)
    {
        *OutClosest = *End;
    }
    else if (StoredFraction < 0.0f)
    {
        *OutClosest = *Start;
    }
    else
    {
        // The z product is rounded to a float before the add.
        float const ScaledZ = static_cast<float>(static_cast<double>(AlongZ) * StoredFraction);
        OutClosest->x = static_cast<float>(static_cast<double>(AlongX) * StoredFraction + Start->x);
        OutClosest->y = static_cast<float>(static_cast<double>(AlongY) * StoredFraction + Start->y);
        OutClosest->z = ScaledZ + Start->z;
        bInterpolated = true;
    }

    if (!bInterpolated)
    {
        // The offset goes through FUN_06eb7810 (float stores).
        float const OffsetX = Point->x - OutClosest->x;
        float const OffsetY = Point->y - OutClosest->y;
        float const OffsetZ = Point->z - OutClosest->z;
        return static_cast<double>(OffsetZ) * OffsetZ + static_cast<double>(OffsetY) * OffsetY
             + static_cast<double>(OffsetX) * OffsetX;
    }

    // Here the offset stays on the x87 stack.
    double const OffsetX = static_cast<double>(Point->x) - OutClosest->x;
    double const OffsetY = static_cast<double>(Point->y) - OutClosest->y;
    double const OffsetZ = static_cast<double>(Point->z) - OutClosest->z;
    return OffsetZ * OffsetZ + OffsetY * OffsetY + OffsetX * OffsetX;
}

// The integer guess of the reciprocal square root TU53 FUN_06f672d0 inlines twice:
// 0x5f3759df - (bits >> 1), an arithmetic shift of the float's bits.
static float ReciprocalSqrtGuess(float Value)
{
    int Bits;
    std::memcpy(&Bits, &Value, sizeof(Bits));
    int const GuessBits = 0x5f3759df - (Bits >> 1);
    float Guess;
    std::memcpy(&Guess, &GuessBits, sizeof(Guess));
    return Guess;
}

// TU53 FUN_06f672d0. Constants: DAT_06fb605c = 0.5f, DAT_06fb6058 = 1.5f, DAT_06fb6060 =
// 0.0174532924f (degrees to radians).
bool __cdecl IsPointInViewCone(Vector const* Point, FSpaceObjPlacement const* Viewer,
                               float const* MaxDistance, float const* ConeAngle)
{
    // dy and dz are stored as floats, dx stays on the x87 stack; the sum is also kept as a
    // float.
    double const DeltaX = static_cast<double>(Point->x) - Viewer->Position.x;
    float const DeltaY = static_cast<float>(static_cast<double>(Point->y) - Viewer->Position.y);
    double const DeltaZWide = static_cast<double>(Point->z) - Viewer->Position.z;
    float const DeltaZ = static_cast<float>(DeltaZWide);
    double const LengthSq = DeltaZWide * DeltaZ + static_cast<double>(DeltaY) * DeltaY
                          + DeltaX * DeltaX;
    float const StoredLengthSq = static_cast<float>(LengthSq);

    // |d| = y * (1.5 - y*y*L*0.5) * L; greater or unordered: false.
    float const LengthGuess = ReciprocalSqrtGuess(StoredLengthSq);
    double const Length = (1.5 - static_cast<double>(LengthGuess) * LengthGuess * LengthSq * 0.5)
                        * LengthGuess * LengthSq;
    if (!(Length <= *MaxDistance))
        return false;

    // The third orientation column, negated; its square as (z*z + y*y) + x*x.
    double const ForwardX = -Viewer->Orientation.E[0][2];
    double const ForwardY = -Viewer->Orientation.E[1][2];
    double const ForwardZ = -Viewer->Orientation.E[2][2];
    double const ForwardSq = ForwardZ * ForwardZ + ForwardY * ForwardY + ForwardX * ForwardX;
    float const ForwardGuess = ReciprocalSqrtGuess(static_cast<float>(ForwardSq));
    // From here the length square is the float copy.
    double const LengthSqRounded = StoredLengthSq;
    float const LengthGuess2 = ReciprocalSqrtGuess(StoredLengthSq);

    double const Dot = ForwardZ * DeltaZ + ForwardY * DeltaY + ForwardX * DeltaX;
    // Both Newton steps, then |F| * |d| in retail's multiplication order.
    double const ForwardStep = 1.5 - static_cast<double>(ForwardGuess) * ForwardGuess * ForwardSq * 0.5;
    double const LengthStep = 1.5 - static_cast<double>(LengthGuess2) * LengthGuess2
                                    * LengthSqRounded * 0.5;
    double const Norms = ForwardStep * LengthStep * LengthGuess2 * LengthSqRounded
                       * ForwardGuess * ForwardSq;
    double const CosToPoint = Dot / Norms;

    // The half-angle cosine greater than the point's, or unordered: false.
    double const CosHalfAngle = std::cos(static_cast<double>(*ConeAngle) * 0.5
                                         * static_cast<double>(0.0174532924f));
    return CosHalfAngle <= CosToPoint;
}

// FUN_06f72930.
int FSystemInfo::SendComm(unsigned int SenderId, unsigned int TargetId, unsigned int TextId,
                          unsigned int VoiceId, unsigned int CaptionIds,
                          Costume const* CommCostume, float Duration)
{
    int TextCount = 1;
    return SendSpaceComm(this, SenderId, TargetId, &TextId, &TextCount, VoiceId, CaptionIds,
                         CommCostume, 0, Duration, true);
}

// TU53 FUN_06f60dd0: argument 1 is never read.
int __cdecl SendSpaceComm(void const* Unused, unsigned int SenderId, unsigned int TargetId,
                          unsigned int* TextIds, int const* TextCount, unsigned int VoiceId,
                          unsigned int CaptionIds, Costume const* CommCostume, unsigned int PermutationIds,
                          float Duration, bool bInterrupt)
{
    (void)Unused;
    return pub::SpaceObj::SendComm(SenderId, TargetId, VoiceId, CommCostume, CaptionIds,
                                   TextIds, *TextCount, PermutationIds, Duration, bInterrupt);
}

// FUN_06f75f20.
std::map<unsigned int, FSystemInfo>* __cdecl GetSystemInfos()
{
    return &GSystemInfos;
}

// FUN_06f75f30.
FSystemInfo* __cdecl FindSystemInfo(unsigned int const* SystemId)
{
    std::map<unsigned int, FSystemInfo>::iterator It = GSystemInfos.find(*SystemId);
    if (It == GSystemInfos.end())
        return nullptr;
    return &It->second;
}

// FUN_06f75f80: one SystemInfo per universe system; an id already in the table keeps its
// entry (unique insert).
void __cdecl BuildSystemInfos()
{
    unsigned int SystemId = 0;
    if (!pub::NextSystemID(SystemId))
        return;
    do
    {
        FSystemInfo Info(&SystemId);
        std::pair<unsigned int const, FSystemInfo> Entry(SystemId, Info); // FUN_06f79d30
        GSystemInfos.insert(Entry);
    } while (pub::NextSystemID(SystemId));
}

// FUN_06f76140.
void __cdecl ClearSystemInfos()
{
    GSystemInfos.clear();
}

// FUN_06f76200.
void __cdecl LoadDynSysInfo(INI_Reader* Reader)
{
    GSystemInfos.clear();
    BuildSystemInfos();
    while (Reader->read_header())
    {
        if (!Reader->is_header("DynSysInfo"))
            continue;
        while (Reader->read_value())
        {
            if (Reader->is_value("nickname"))
            {
                // The find result is discarded, as retail.
                unsigned int SystemId;
                pub::GetSystemID(SystemId, Reader->get_value_string());
                std::map<unsigned int, FSystemInfo>::iterator Found =
                    GSystemInfos.find(SystemId);
                (void)Found;
                break;
            }
        }
    }
}

// FUN_06f76350: walks the table and does nothing with it.
void __cdecl VisitSystemInfos()
{
    for (std::map<unsigned int, FSystemInfo>::iterator It = GSystemInfos.begin();
         It != GSystemInfos.end(); ++It)
    {
    }
}

// FUN_06f73ac0. "Player" uses FUN_06f49b50, else in single player the first state of the
// player map (DAT_06fd0bcc). The name compare is FUN_06ed01c0; the object is tested for null
// only after the compare, as retail. The fallback id is kept when FUN_06f683b0 accepts it.
unsigned int FSystemInfo::FindObjectIdByName(TString<0x30> const* Name, unsigned int ControllerId)
{
    if (Name->Length == 0)
        return 0;

    if (strcmp(Name->Data, "Player") == 0)
    {
        FContentPlayerState* State = FindContentPlayerStateByLiveMission(&ControllerId);
        if (State == nullptr)
        {
            if (!pub::SinglePlayer())
                return 0;
            State = GetFirstContentPlayerState();
            if (State == nullptr)
                return 0;
        }
        FContentPlayerShip* Ship = State->ShipObject;
        if (Ship == nullptr)
            return 0;
        return Ship->SpaceObjId;
    }

    for (std::list<FContentSpaceObj*>::iterator It = SpaceObjs.begin(); It != SpaceObjs.end(); ++It)
    {
        if (*Name == (*It)->Nickname.Data && *It != nullptr)
            return (*It)->SpaceObjId;
    }

    unsigned int Id = pub::GetNicknameId(Name->Data);
    if (!IsSpaceObjTypeSet(Id, this))
        Id = 0;
    return Id;
}

// ---------------------------------------------------------------------------------------
// Spawn placement helpers (callers in TU53 SpawnPlacement.cpp)
// ---------------------------------------------------------------------------------------

namespace
{
    // .rdata 06fbbedc: the ship id CanUseDockTarget gets for a ship not created yet (0).
    unsigned int const GNoDockingShipId = 0;
}

// FUN_06f72380.
int FSystemInfo::CollectDockTargets(std::vector<unsigned int> const* ClassIds, int const* ShipRep,
                                    unsigned int const* Group, unsigned int const* ExcludeId,
                                    Vector const* Position, float Range,
                                    unsigned int const* TypeMask, std::vector<unsigned int>* Out)
{
    unsigned int Ids[0x20];
    unsigned int Count;
    pub::System::ScanObjects(SystemId, Ids, 0x20, *Position, Range, *TypeMask, Count);
    for (unsigned int Index = 0; Index < Count; ++Index)
    {
        unsigned int const* Id = &Ids[Index];
        if (*Id == *ExcludeId)
            continue;
        if (IsContentNpcGateLocked(Id))                                             // FUN_06f6d280
            continue;
        unsigned int Type;
        pub::SpaceObj::GetType(*Id, Type);
        if (!CanUseDockTarget(ClassIds, ShipRep, Group, &GNoDockingShipId, Id))     // FUN_06f6cb90
            continue;
        if ((Type & (OBJ_JUMP_GATE | OBJ_AIRLOCK_GATE | OBJ_JUMP_HOLE)) == 0)
        {
            // The ship's reputation, or the group when it has none.
            unsigned int const OwnerGroup = *ShipRep != -1 ? static_cast<unsigned int>(*ShipRep)
                                                           : *Group;
            if (!IsGroupAtDockBase(&OwnerGroup, Id))                                // FUN_06f6cb20
                continue;
            // Retail tests the type again (always clear here).
            if ((Type & (OBJ_JUMP_GATE | OBJ_AIRLOCK_GATE | OBJ_JUMP_HOLE)) == 0)
            {
                unsigned int DockingTarget;
                pub::SpaceObj::GetDockingTarget(*Id, DockingTarget);
                if (DockingTarget == 0)
                    continue;
            }
        }
        Out->push_back(*Id);
    }
    return static_cast<int>(Out->size());
}

// FUN_06f724f0.
unsigned int FSystemInfo::PickDockTarget(std::vector<unsigned int> const* ClassIds,
                                         int const* ShipRep, unsigned int const* Group,
                                         unsigned int const* ExcludeId, Vector const* Position,
                                         float Range, unsigned int const* TypeMask)
{
    std::vector<unsigned int> Targets;
    CollectDockTargets(ClassIds, ShipRep, Group, ExcludeId, Position, Range, TypeMask, &Targets);
    if (Targets.empty())
        return 0;
    return Targets[(rand() * static_cast<int>(Targets.size())) / 32768];
}

// FUN_06f725c0.
unsigned int FSystemInfo::PickPathDockTarget(std::vector<unsigned int> const* ClassIds,
                                             int const* ShipRep, unsigned int const* Group,
                                             unsigned int const* ExcludeId, FPatrolPath* Path,
                                             int First, int Last, float MinDistance,
                                             unsigned int const* TypeMask)
{
    std::vector<FPatrolPathObject> Objects;
    Path->FindObjectsInRange(&Objects, First, Last, *TypeMask);                    // FUN_06f37aa0
    std::vector<unsigned int> Candidates;
    Candidates.reserve(Objects.size());
    for (std::vector<FPatrolPathObject>::const_iterator Object = Objects.begin();
         Object != Objects.end(); ++Object)
    {
        unsigned int const Id = Object->ObjectId;
        if (Id == *ExcludeId)
            continue;
        // The squared distance from *ExcludeId (0.0 without one); both locations are fetched
        // for every object.
        double DistanceSq = 0.0;                                                    // DAT_06fb444c
        if (*ExcludeId != 0)
        {
            Vector ExcludePosition;
            Vector ObjectPosition;
            Matrix Orientation;
            pub::SpaceObj::GetLocation(*ExcludeId, ExcludePosition, Orientation);
            pub::SpaceObj::GetLocation(Id, ObjectPosition, Orientation);
            Vector const Delta = SubtractVectors(ObjectPosition, ExcludePosition);
            DistanceSq = (static_cast<double>(Delta.z) * Delta.z +
                          static_cast<double>(Delta.y) * Delta.y) +
                         static_cast<double>(Delta.x) * Delta.x;
        }
        // Less or unordered skips.
        if (!(DistanceSq >= static_cast<double>(MinDistance)))
            continue;
        if (IsContentNpcGateLocked(&Id))                                            // FUN_06f6d280
            continue;
        unsigned int Type;
        pub::SpaceObj::GetType(Id, Type);
        if (!CanUseDockTarget(ClassIds, ShipRep, Group, &GNoDockingShipId, &Id))    // FUN_06f6cb90
            continue;
        if ((Type & (OBJ_JUMP_GATE | OBJ_AIRLOCK_GATE | OBJ_JUMP_HOLE)) == 0)
        {
            unsigned int const OwnerGroup = *ShipRep != -1 ? static_cast<unsigned int>(*ShipRep)
                                                           : *Group;
            if (!IsGroupAtDockBase(&OwnerGroup, &Id))                               // FUN_06f6cb20
                continue;
        }
        Candidates.push_back(Id);
    }
    if (Candidates.empty())
        return 0;
    return Candidates[(rand() * static_cast<int>(Candidates.size())) / 32768];
}

// FUN_06f77b40.
int FSystemInfo::GetTradeLaneRingOrder(unsigned int const* FirstRing,
                                       unsigned int const* SecondRing)
{
    if (*FirstRing == 0 || *SecondRing == 0)
        return 0;
    // End gives a null lane, not tested, as retail.
    std::map<unsigned int, FTradeLane*>::iterator Found = TradeLaneByRing.find(*FirstRing);
    FTradeLane* TheLane = Found != TradeLaneByRing.end() ? Found->second : nullptr;
    std::vector<FTradeLaneRing> const& Rings = TheLane->Rings;
    size_t FirstIndex = 0;
    while (FirstIndex < Rings.size() && Rings[FirstIndex].ObjectId != *FirstRing)
        ++FirstIndex;
    size_t SecondIndex = 0;
    while (SecondIndex < Rings.size() && Rings[SecondIndex].ObjectId != *SecondRing)
        ++SecondIndex;
    return SecondIndex == FirstIndex + 1 ? 1 : -1;
}

// FUN_06f77e70.
bool FSystemInfo::GetLaneShipDistance(int const* Direction, unsigned int const* RingId,
                                      Vector const* Position, float* OutDistance)
{
    // The find is not tested, as retail.
    FTradeLane* TheLane = TradeLaneByRing.find(*RingId)->second;
    std::vector<unsigned int> const& Ships =
        *Direction == 1 ? TheLane->ForwardShips : TheLane->BackwardShips;
    if (Ships.empty())
        return false;

    float Nearest = -1.0f;                                                          // an arg slot in retail
    Vector NearestPosition;
    for (std::vector<unsigned int>::const_iterator Ship = Ships.begin(); Ship != Ships.end(); ++Ship)
    {
        Vector ShipPosition;
        Matrix Orientation;
        pub::SpaceObj::GetLocation(*Ship, ShipPosition, Orientation);
        float const Dx = Position->x - ShipPosition.x;                              // FUN_06eb7810
        float const Dy = Position->y - ShipPosition.y;
        float const Dz = Position->z - ShipPosition.z;
        double const DistanceSq = (static_cast<double>(Dz) * Dz + static_cast<double>(Dy) * Dy) +
                                  static_cast<double>(Dx) * Dx;
        // The first ship (Nearest == -1), or a strictly nearer one.
        if (Nearest == -1.0f || DistanceSq < static_cast<double>(Nearest))
        {
            Nearest = static_cast<float>(DistanceSq);
            NearestPosition = ShipPosition;
        }
    }

    float const Distance = static_cast<float>(std::sqrt(static_cast<double>(Nearest)));
    *OutDistance = Distance;
    float const Ax = Position->x - NearestPosition.x;                               // FUN_06eb7810
    float const Ay = Position->y - NearestPosition.y;
    float const Az = Position->z - NearestPosition.z;
    // The lane's z axis: the third column of its placement.
    double const Zx = TheLane->Placement.Rotation.D[0][2];
    double const Zy = TheLane->Placement.Rotation.D[1][2];
    double const Zz = TheLane->Placement.Rotation.D[2][2];
    if (*Direction == 1)
    {
        // Negative only when the dot product is below 0 (ordered).
        double const Dot = (Az * Zz + Ay * Zy) + Ax * Zx;
        if (Dot < 0.0)
            *OutDistance = -Distance;
    }
    else
    {
        // Negative only when it is above 0 (ordered); retail's operand order differs here.
        double const Dot = (Az * Zz + Zy * Ay) + Zx * Ax;
        if (Dot > 0.0)
            *OutDistance = -Distance;
    }
    return true;
}

// FUN_06f78070.
double FSystemInfo::GetTimeSinceLaneLeave(int const* Direction, unsigned int const* RingId)
{
    // The find is not tested, as retail.
    FTradeLane* TheLane = TradeLaneByRing.find(*RingId)->second;
    float const LeaveTime =
        *Direction == 1 ? TheLane->LastForwardLeaveTime : TheLane->LastBackwardLeaveTime;
    if (LeaveTime == -1.0f)
        return -1.0f;                                                               // DAT_06fb5e84
    return GetContentGameTime() - LeaveTime;                                        // FUN_06efec10
}
