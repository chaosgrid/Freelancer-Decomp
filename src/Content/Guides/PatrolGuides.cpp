// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU37 (06f0aa10-06f0dd07): the "patrol_path" guide (type 3), its route along the
// encounter's zone path and its cargo scanner (GuidesInternal.h).

#include "Guides/GuidesInternal.h"
#include "NPCs/BehaviorCmds.h"         // FContentShipBase
#include "Core/ContentInternal.h"     // EObjectType
#include "Core/ContentPlayerState.h"   // FindContentPlayerState(ByShip)
#include "NPCs/DockQueues.h"           // GetShipDockingTarget, CollectFormationMembers
#include "NPCs/FactionProps.h"         // GetFactionProps
#include "Comms/Gcs.h"                  // FGcsScanEvent, GGcsConversations
#include "Missions/MissionObjList.h"       // CreateObjListDirective and the directive setters
#include "Missions/MissionScript.h"        // GMissionScanClamp
#include "NPCs/NpcShip.h"              // FContentNpcShip
#include "Encounters/PatrolPaths.h"          // FPatrolPath
#include "Encounters/SpacePaths.h"           // Register/UnregisterGuidedShip, IsSameShipOrFormation, ...
#include "Universe/SystemInfo.h"
#include "common_goods.h"         // EquipDesc, EquipDescVector
#include "common_server.h"
#include "server_cobject.h"       // pub::SpaceObj::EQInfo, ActivateEquipment

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <new>
#include <vector>

namespace
{
    Vector GetShipPosition(FContentShipBase* Ship)
    {
        Vector Position;
        Ship->GetPosition(&Position);                                   // FUN_06eb70f0
        return Position;
    }

    // The space object of Id in Ship's system (FUN_06eea860 + FUN_06f73930) as the ship base
    // retail reads it as (+0x11d, +0x154, FUN_06f57710).
    FContentShipBase* FindSystemShip(FContentShipBase* Ship, unsigned int Id)
    {
        return static_cast<FContentShipBase*>(Ship->GetSystem()->FindShip(Id));
    }

    // Retail's inline find over a vector<uint> (FUN_06eb5f30 and the open-coded loops).
    bool ContainsId(std::vector<unsigned int> const& Ids, unsigned int Id)
    {
        return std::find(Ids.begin(), Ids.end(), Id) != Ids.end();
    }

    // Every member's id not yet in Ids appended (FUN_06eb5770); retail then looks the id up
    // again and builds a {iterator, inserted} pair it never reads (FUN_06fa6e80).
    void AddMemberIds(std::vector<unsigned int>* Ids, std::vector<FContentShipBase*> const& Members)
    {
        for (std::vector<FContentShipBase*>::const_iterator Member = Members.begin();
             Member != Members.end(); ++Member)
        {
            unsigned int const MemberId = (*Member)->SpaceObjId;
            if (!ContainsId(*Ids, MemberId))
                Ids->push_back(MemberId);
        }
    }

    // The scan request of state 6 (06f0cfbe): EQInfo type 1 on the stack (+0x00 = 1, byte
    // +0x04 = 1, +0x0c = the scanned ship; retail leaves +0x08 unset, the port 0) whose +0x10
    // is a local EquipDescVector the server fills with the scanned cargo. The vector's buffer
    // is freed at the end of the state (06f0d376).
    struct FCargoScanRequest
    {
        pub::SpaceObj::EQInfo Info = {};
        EquipDescVector* Result = nullptr;

        explicit FCargoScanRequest(unsigned int TargetId)
        {
            Info.Type = 1;
            Info.EquipId = 1;
            Info.TargetObjId = TargetId;
            Result = new (Info.CargoOut) EquipDescVector();
        }
        FCargoScanRequest(FCargoScanRequest const&) = delete;
        FCargoScanRequest& operator=(FCargoScanRequest const&) = delete;
        ~FCargoScanRequest()
        {
            Result->~EquipDescVector();
        }
    };
}

// ---------------------------------------------------------------------------------------
// Patrol route (06fb854c)
// ---------------------------------------------------------------------------------------

// LAB_06f0bbb0.
FGuideRoute* FPatrolRoute::Clone()
{
    return new FPatrolRoute(*this);
}

// 06ee9660.
int FPatrolRoute::GetType()
{
    return 2;
}

// LAB_06f0bc00.
int FPatrolRoute::GetStatus()
{
    switch (State)
    {
    case 1:
    case 2:
        return 1;
    case 4:
        return 3;
    default:
        return 2;
    }
}

// LAB_06f0bfe0 (RET 4). GotoVec range 500 (DAT_06fb8120). State 2 counts a point as reached
// when the active GotoVec's destination is within 1 (DAT_06fb5810) of it on every axis
// (squared per axis; NaN is not reached).
int FPatrolRoute::Update(std::list<FObjListDirective*>* Out)
{
    if (State == 1)
    {
        TRefHandle<FObjListDirective> Directive;
        CreateObjListDirective(OBJLIST_GOTO_VEC, &Directive.Pointer);                 // FUN_06f24870
        FObjListGotoVec* Goto = static_cast<FObjListGotoVec*>(Directive.Pointer);
        Goto->SetDestination(&Path->Points[NextPoint], 500.0f, -1.0f, 0, false, false);
        Goto->Op.bFlyThrough = 1;                                          // [dir+0x164]
        Goto->Op.Priority = 1;
        Out->push_back(Directive.Pointer);                              // FUN_06f1e390
        Directive->AddRef();
        State = 2;
    }
    else if (State == 2)
    {
        FObjListDirective** Active = Ship->GetActiveDirectiveSlot();   // slot 3
        if ((*Active)->GetType() == OBJLIST_GOTO_VEC)
        {
            Vector const Destination =
                static_cast<FObjListGotoVec*>(*Ship->GetActiveDirectiveSlot())->Op.Destination;
            Vector const& Point = Path->Points[NextPoint];
            double const Dx = static_cast<double>(Destination.x) - Point.x;
            double const Dy = static_cast<double>(Destination.y) - Point.y;
            double const Dz = static_cast<double>(Destination.z) - Point.z;
            bool const bReached = Dx * Dx <= 1.0 && Dy * Dy <= 1.0 && Dz * Dz <= 1.0;
            if (bReached)
            {
                if (NextPoint == EndPoint)
                    State = 4;
                else
                    AdvancePoint();                                     // FUN_06f0bf10
            }
        }
        if (State != 4)
        {
            TRefHandle<FObjListDirective> Directive;
            CreateObjListDirective(OBJLIST_GOTO_VEC, &Directive.Pointer);
            FObjListGotoVec* Goto = static_cast<FObjListGotoVec*>(Directive.Pointer);
            Goto->SetDestination(&Path->Points[NextPoint], 500.0f, -1.0f, 0, false, false);
            Goto->Op.bFlyThrough = 1;
            Goto->Op.Priority = 1;
            Out->push_back(Directive.Pointer);
            Directive->AddRef();
        }
    }
    else if (State == 3)
    {
        State = 2;
        Update(Out);
    }
    return GetStatus();
}

// LAB_06f0bc40 (RET 0x10). *OutFloat = 500 first (also when refusing).
// State 1..3: State 3, type 3 (a point) towards Points[NextPoint]: the point itself when
// within 10000 of the ship (GGuideLaneEntryRangeSquared; NaN counts as within), else 10000
// from the ship in its direction (each product rounded to float as retail stores it).
// Retail then builds a GotoVec to that position and releases it unused (06f0bdf3); the port
// does the same.
bool FPatrolRoute::RequestFleeDestination(pub::AI::ContentCallback::FleeDestinationType& OutType,
                                          Vector& OutPosition, unsigned int& OutParam,
                                          float& OutFloat)
{
    (void)OutParam;
    OutFloat = 500.0f;                                                  // DAT_06fb8120
    if (State != 1 && State != 2 && State != 3)
        return false;

    State = 3;
    OutType = static_cast<pub::AI::ContentCallback::FleeDestinationType>(3);
    Vector const Point = Path->Points[NextPoint];
    Vector const ShipPosition = GetShipPosition(Ship);
    Vector Delta;                                                       // FUN_06eb7810
    Delta.x = static_cast<float>(static_cast<double>(Point.x) - ShipPosition.x);
    Delta.y = static_cast<float>(static_cast<double>(Point.y) - ShipPosition.y);
    Delta.z = static_cast<float>(static_cast<double>(Point.z) - ShipPosition.z);
    double const LengthSquared =
        (static_cast<double>(Delta.y) * Delta.y + static_cast<double>(Delta.z) * Delta.z) +
        static_cast<double>(Delta.x) * Delta.x;
    if (!(LengthSquared > GGuideLaneEntryRangeSquared))
    {
        OutPosition = Point;
    }
    else
    {
        double const Inverse = 1.0 / std::sqrt(LengthSquared);         // DAT_06fb5810
        float const Nx = static_cast<float>(Delta.x * Inverse);
        float const Ny = static_cast<float>(Delta.y * Inverse);
        float const Nz = static_cast<float>(Delta.z * Inverse);
        float const Sz = static_cast<float>(static_cast<double>(Nz) * 10000.0);   // DAT_06fb8100
        float const Sy = static_cast<float>(static_cast<double>(Ny) * 10000.0);
        float const Sx = static_cast<float>(10000.0 * Nx);
        Vector const From = GetShipPosition(Ship);
        OutPosition.x = static_cast<float>(static_cast<double>(Sx) + From.x);
        OutPosition.y = static_cast<float>(static_cast<double>(Sy) + From.y);
        OutPosition.z = static_cast<float>(static_cast<double>(Sz) + From.z);
    }

    TRefHandle<FObjListDirective> Unused;
    CreateObjListDirective(OBJLIST_GOTO_VEC, &Unused.Pointer);
    static_cast<FObjListGotoVec*>(Unused.Pointer)
        ->SetDestination(&OutPosition, 500.0f, -1.0f, 0, false, false);
    return true;
}

// 06ebf6f0: nothing.
void FPatrolRoute::Slot1c(unsigned int Unused)
{
    (void)Unused;
}

// FUN_06f0bf10. Count = the path's point count (0 without a buffer).
void FPatrolRoute::AdvancePoint()
{
    if (NextPoint == EndPoint)
        return;
    int const Count = static_cast<int>(Path->Points.size());
    if (Direction == -1 && NextPoint == 0)
    {
        if (Path->bLoop)
        {
            NextPoint = Count - 2;
            return;
        }
        Direction = 1;
        NextPoint = 1;
        return;
    }
    if (Direction == 1 && NextPoint == Count - 1)
    {
        if (Path->bLoop)
        {
            NextPoint = 1;
            return;
        }
        NextPoint = NextPoint - 1;
        Direction = -1;
        return;
    }
    NextPoint = Direction + NextPoint;
}

// FUN_06f0be60. Going up (Direction > 0): End - Next, or across the end
// (Next beyond End) End - Next + Count + 1; going down the same with Next - End.
int FPatrolRoute::GetPointsToGo() const
{
    if (State != 2 && State != 1)
        return 0;
    int const Count = static_cast<int>(Path->Points.size());
    if (Direction > 0)
    {
        if (NextPoint <= EndPoint)
            return EndPoint - NextPoint;
        return (EndPoint - NextPoint) + Count + 1;
    }
    if (NextPoint >= EndPoint)
        return NextPoint - EndPoint;
    return (NextPoint - EndPoint) + Count + 1;
}

// ---------------------------------------------------------------------------------------
// Cargo scanner (06fb8520)
// ---------------------------------------------------------------------------------------

// 06f0ab23 (inlined in FUN_06f0aa40): route base (06fb839c), Ship, the directive list
// empty, +0x14 0, +0x18 / +0x1c -1, +0x20..+0x2c 0, both vectors empty, +0x50 / +0x54 0,
// +0x5c -1.0f, +0x60 / +0x61 1, vtable 06fb8520; then FUN_06f0c3f0(Ship) called directly.
// +0x58 is not written (SetShip sets it before any reader when scanning is possible); the
// port starts it at 0.
FCargoScanRoute::FCargoScanRoute(FContentNpcShip* InShip)
{
    Ship = InShip;
    FCargoScanRoute::SetShip(InShip);
}

// FUN_06f0c2c0: Ship; the directives copied and each AddRef'ed; +0x14..+0x2c; the two
// vectors are NOT copied (empty); +0x50..+0x58; +0x60 / +0x61. +0x5c is not written (retail
// heap garbage; the port starts it at -1.0f, "off").
FCargoScanRoute::FCargoScanRoute(FCargoScanRoute const& Other)
    : FGuideRoute(Other)
    , Directives(Other.Directives)
    , State(Other.State)
    , ScanRange(Other.ScanRange)
    , CargoCount(Other.CargoCount)
    , TargetId(Other.TargetId)
    , CommSpeakerId(Other.CommSpeakerId)
    , CargoGoodId(Other.CargoGoodId)
    , Cargo(Other.Cargo)
    , AnswerTimer(Other.AnswerTimer)
    , Field_0x54(Other.Field_0x54)
    , ScanChance(Other.ScanChance)
    , bScanEnabled(Other.bScanEnabled)
    , bScanAnnounce(Other.bScanAnnounce)
{
}

// FUN_06f0c560 (sdd FUN_06f0acd0): the scanned ship's +0x11d cleared
// (when it is still known), then LootIds, ScannedIds and the directives (each Released) freed
// by the members' destructors in that order.
FCargoScanRoute::~FCargoScanRoute()
{
    if (TargetId != 0)
    {
        FContentShipBase* Target = FindSystemShip(Ship, TargetId);
        if (Target != nullptr)
            Target->bScanTarget = 0;
        TargetId = 0;
    }
}

// FUN_06f0c500.
FGuideRoute* FCargoScanRoute::Clone()
{
    return new FCargoScanRoute(*this);
}

// 06f5e540.
int FCargoScanRoute::GetType()
{
    return 3;
}

// LAB_06f0abe0. Anything but off / idle is busy (1).
int FCargoScanRoute::GetStatus()
{
    if (State == ScanOff)
        return 2;
    if (State == ScanIdle)
        return 0;
    return 1;
}

// LAB_06f0ac00.
void FCargoScanRoute::SetScanEnabled(bool bEnabled)
{
    bScanEnabled = bEnabled;
}

// FUN_06f0ac10.
void FCargoScanRoute::Reset()
{
    if (TargetId != 0)
    {
        FContentShipBase* Target = FindSystemShip(Ship, TargetId);
        if (Target != nullptr)
            Target->bScanTarget = 0;
        TargetId = 0;
    }
    LootIds.clear();
    Directives.Clear();                                                 // Release + erase each
    LootTimer = -1.0f;
    if (State != ScanOff)
        State = ScanIdle;
}

// FUN_06f0c3f0. A null ship resets first and switches the scanner off.
// Scanning needs a faction (the ship's reputation affiliation) with scan_for_cargo goods and a
// working scanner whose cargo scan range (pub::SpaceObj::GetScannerRange, second value) is
// above 500; the range is then clamped to 500, scan_announce taken from the faction and the
// scan chance from the pilot's job (JobParams +0x130 at personality +0x3d0) unless that is
// SCAN_CHANCE_DEFAULT, else from the faction. A scanner that
// cannot scan is reset (when on) and switched off.
void FCargoScanRoute::SetShip(FContentNpcShip* InShip)
{
    if (InShip == nullptr)
        Reset();                                                        // slot 8
    Ship = InShip;
    if (InShip == nullptr)
    {
        State = ScanOff;
        return;
    }

    unsigned int Affiliation = InShip->ReputationHandle->GetAffiliation();   // FUN_06eb6bb0
    FFactionProps* Props = GetFactionProps(&Affiliation);              // FUN_06f50650
    if (Props->ScanForCargo.empty())
    {
        if (State != ScanOff)
            Reset();
        State = ScanOff;
        return;
    }

    int RadarRange;
    if (pub::SpaceObj::GetScannerRange(Ship->SpaceObjId, RadarRange, ScanRange) != 0 ||
        ScanRange <= 500)
    {
        if (State != ScanOff)
            Reset();
        State = ScanOff;
        return;
    }
    ScanRange = 500;
    bScanAnnounce = Props->bScanAnnounce;                               // +0x70
    pub::AI::Personality* Personality = InShip->GetPersonality();      // slot 5
    if (Personality->JobParams.ScanChance != pub::AI::Personality::SCAN_CHANCE_DEFAULT)
        ScanChance = Personality->JobParams.ScanChance;
    else
        ScanChance = Props->ScanChance;                                 // +0x6c
    if (State == ScanOff)
        State = ScanIdle;
}

// FUN_06f0ca10 (RET 8).
void FCargoScanRoute::AddTractorBeamDirective(unsigned int InTargetId,
                                              std::list<FObjListDirective*>* List)
{
    TRefHandle<FObjListDirective> Directive;
    CreateObjListDirective(OBJLIST_TRACTOR_BEAM, &Directive.Pointer);
    FObjListTractorBeam* Tractor = static_cast<FObjListTractorBeam*>(Directive.Pointer);
    Tractor->SetTarget(InTargetId, false);                             // FUN_06f28df0
    Tractor->Op.Priority = 0;
    List->push_back(Directive.Pointer);
    Directive->AddRef();
}

// FUN_06f0cad0 (RET 8). -1.0f is DAT_06fb80fc; the holder is released after
// submit_forced_target.
void FCargoScanRoute::AddGotoShipDirective(unsigned int InTargetId,
                                           std::list<FObjListDirective*>* List)
{
    TRefHandle<FObjListDirective> Directive;
    CreateObjListDirective(OBJLIST_GOTO_SHIP, &Directive.Pointer);
    FObjListGotoShip* Goto = static_cast<FObjListGotoShip*>(Directive.Pointer);
    Goto->SetTarget(&InTargetId, 50.0f, -1.0f);                         // FUN_06f26150
    Goto->Op.Priority = 0;
    List->push_back(Directive.Pointer);
    Directive->AddRef();
    pub::AI::submit_forced_target(Ship->SpaceObjId, InTargetId);
}

// LAB_06f0dbd0 (RET 4).
int FCargoScanRoute::Update(std::list<FObjListDirective*>* Out)
{
    for (std::list<FObjListDirective*>::iterator It = Out->begin(); It != Out->end(); ++It)
        (*It)->Release();
    Out->clear();                                                       // FUN_06ecfec0
    if (Out != &Directives.Items)
        *Out = Directives.Items;                                        // FUN_06fa8370 / 06ed0230
    for (std::list<FObjListDirective*>::iterator It = Out->begin(); It != Out->end(); ++It)
        (*It)->AddRef();
    for (std::list<FObjListDirective*>::iterator It = Directives.Items.begin();
         It != Directives.Items.end(); ++It)
        (*It)->Release();
    Directives.Items.clear();                                           // FUN_06ecfe80 per node
    return GetStatus();
}

// FUN_06f0c6b0. Off while the mission clamps scans (DAT_06fd1c79). The
// candidates are the ships within 2500 of the scanner (pub::System::ScanObjects, 16 at most,
// type mask 0x7f0000) in scan order: one already in ScannedIds, the scanner itself or an
// unknown one is skipped. A candidate's formation (FUN_06f66220; the ship alone outside one)
// is judged by its first member: skipped when that one is being scanned (+0x11d) or in a
// trade lane (FUN_06f57710); else every member goes into ScannedIds. The first member is taken when its
// reputation is known, the scanner's attitude towards it lies in [-0.6, Max] (Max 0.2 for a
// player when the scanner's faction is unlawful, 1.0 for another player, 0.6 for an NPC) and
// rand() / 32768 is below ScanChance: it is marked (+0x11d = 1) and becomes TargetId.
// Returns TargetId != 0 (a target kept from before counts).
bool FCargoScanRoute::FindScanTarget()
{
    if (GMissionScanClamp != 0)
        return false;

    FSystemInfo* System = Ship->GetSystem();
    unsigned int const SystemId = System->SystemId;                     // +0x48
    unsigned int Count = 0;
    unsigned int Objects[16];
    Vector const Position = GetShipPosition(Ship);
    pub::System::ScanObjects(SystemId, Objects, 0x10, Position, 2500.0f, OBJ_SHIP_MASK, Count);

    for (unsigned int Index = 0; Index < Count; ++Index)
    {
        unsigned int const ObjectId = Objects[Index];
        if (ContainsId(ScannedIds, ObjectId))
            continue;
        if (Ship->SpaceObjId == ObjectId)
            continue;

        std::vector<FContentShipBase*> Members;
        FContentShipBase* Object = static_cast<FContentShipBase*>(System->FindShip(ObjectId));
        if (Object == nullptr)
            continue;
        CollectFormationMembers(&Members, Object->FormationId);         // FUN_06f66220
        if (Members.empty())
            Members.push_back(Object);                                  // FUN_06f937c0
        FContentShipBase* First = Members[0];
        if (First->bScanTarget != 0)
            continue;
        if (First->IsInTradeLane())
            continue;
        unsigned int FirstId = First->SpaceObjId;
        AddMemberIds(&ScannedIds, Members);

        int Reputation;
        if (pub::SpaceObj::GetRep(FirstId, Reputation) != 0)
            continue;
        int const OwnReputation = Ship->ReputationHandle->GetReputation(); // FUN_06f33f80
        // GetAttitude's result is not checked (retail reads the unset local on a failure).
        float Attitude = 0.0f;
        pub::Reputation::GetAttitude(OwnReputation, Reputation, Attitude);
        float const MinAttitude = GPatrolScanMinAttitude;                // DAT_06fd0640
        float MaxAttitude = GPatrolScanMaxAttitude;                      // DAT_06fd063c
        if (FindContentPlayerStateByShip(&FirstId) != nullptr)          // FUN_06f49bc0
        {
            unsigned int Affiliation = Ship->ReputationHandle->GetAffiliation();
            FFactionProps* Props = GetFactionProps(&Affiliation);
            MaxAttitude = Props->Legality == FactionLegalityUnlawful ? 0.2f : 1.0f;   // DAT_06fb80f0 / 80f8
        }
        // NaN fails both tests.
        if (!(Attitude >= MinAttitude))
            continue;
        if (!(Attitude <= MaxAttitude))
            continue;
        // DAT_06fb4448 = 1/32768.
        double const Roll = static_cast<double>(std::rand()) * (1.0 / 32768.0);
        if (!(Roll < ScanChance))
            continue;

        First->bScanTarget = 1;
        TargetId = First->SpaceObjId;
        break;
    }
    return TargetId != 0;
}

void FCargoScanRoute::QueueScanEvent(int Type, unsigned int LootTakerId, unsigned int GoodId,
                                     unsigned int Response)
{
    FGcsScanEvent Event;
    Event.System = Ship->GetSystem();                                   // FUN_06eea860
    Event.Type = Type;
    Event.Speaker = Ship->SpaceObjId;
    Event.Listener = TargetId;
    Event.LootTakerId = LootTakerId;
    Event.CargoGoodId = GoodId;
    Event.ScanResponse = Response;
    CommSpeakerId = Ship->SpaceObjId;
    GGcsConversations.QueueConversation(&Event);                        // FUN_06ee8d70
}

// LAB_06f0cbc0 (RET 8). Message words:
//   0x0b (a line was spoken {listener, ?, speaker, ...}): state 5 -> 4, state 7 -> 8 (after
//        pub::AI::get_scan_response, result unused) when it is this scan's line.
//   0x18 ({loot, taker}) in states 0xb / 0xc for known loot: taken by the scanner itself in
//        state 0xb -> 0xc once the hold is full (occupied / hold, integer DIV, above 0.7);
//        taken by a ship outside the scanner's formation -> event 0x18 and hostility.
//   0x20 (timer): HandleTimer.
//   0x23 ({ship, ?, good, count}): the scanned player's answer: jettisoned the demanded cargo
//        -> state 0xa, then the timer step once with null parms.
//   0x2b ({loot, ship, good}) in states 0xb / 0xc: the scanned ship's jettisoned cargo is
//        recorded; the loot timer (3.1 s DAT_06fb8128) starts.
//   0x39 ({loot, taker, ?, ?, reason}): loot gone: the target itself -> Reset; a recorded loot
//        is dropped (none left -> Reset), then a taker outside the formation -> event 0x18
//        (reason 0..2) and hostility.
//   0x3a ({?, ?, player}): that player's ship is the target -> Reset.
//   0x1001 ({type 0, ship}): the scanner's combat state in states 2..7 -> Reset.
int FCargoScanRoute::HandleEvent(pub::Controller::EMessage MsgId, void const* Parms)
{
    using pub::Controller::EMessage;
    unsigned int const* Words = static_cast<unsigned int const*>(Parms);
    switch (MsgId)
    {
    case EMessage::CommComplete:
        // 06f0cc0e.
        if (State == ScanWaitStandBy)
        {
            if (Words[2] == CommSpeakerId && Words[0] == TargetId)
                State = ScanClosingInAgain;
        }
        else if (State == ScanWaitDemand)
        {
            if (Words[2] == CommSpeakerId && Words[0] == TargetId)
            {
                pub::AI::get_scan_response(Ship->SpaceObjId, TargetId, CargoGoodId);
                State = ScanWaitAnswer;
            }
        }
        break;

    case EMessage::LootAcquired:
    {
        // 06f0d6c6, 06f0db02.
        if (State != ScanTractoring && State != ScanCollecting)
            break;
        if (!ContainsId(LootIds, Words[0]))
            break;
        if (Words[1] == Ship->SpaceObjId)
        {
            if (State != ScanTractoring)
                break;
            unsigned int const Occupied = Ship->GetCargoSpaceOccupied();    // FUN_06f57170
            unsigned int const Hold = Ship->GetCargoHoldSize();             // FUN_06f57150
            // Retail divides as integers, so only a full hold passes 0.7f (DAT_06fb8134).
            unsigned int const Ratio = Occupied / Hold;
            if (static_cast<double>(Ratio) > 0.7f)
                State = ScanCollecting;
            break;
        }
        if (IsSameShipOrFormation(Ship, Words[1]))                      // FUN_06f68270
            break;
        QueueScanEvent(GCS_EVENT_SCAN_LOOT_TAKEN, Words[1], 0, 1);
        SetFormationAttitudes(Ship->GetSystem(), Ship->SpaceObjId, Words[1], true, true,
                              -1.0f);                                   // FUN_06f67d80
        break;
    }

    case EMessage::Heartbeat:
        HandleTimer(static_cast<float const*>(Parms));
        break;

    case EMessage::CargoJettisoned:
    {
        // 06f0d62c.
        if (State == ScanIdle || State == ScanOff)
            break;
        FContentShipBase* Target = FindSystemShip(Ship, TargetId);
        if (Target == nullptr || Target->IsInTradeLane())
        {
            Reset();
            break;
        }
        if (TargetId == 0)
            break;
        if (Words[0] != TargetId)
            break;
        if (FindContentPlayerStateByShip(&TargetId) == nullptr)
            break;
        if (CargoGoodId != Words[2])
            break;
        if (Words[3] != static_cast<unsigned int>(CargoCount))
            break;
        State = ScanJettisoned;
        HandleEvent(EMessage::Heartbeat, nullptr);                      // slot 3
        break;
    }

    case EMessage::LootSpawned:
    {
        // 06f0d7b9.
        if (State != ScanTractoring && State != ScanCollecting)
            break;
        if (Words[1] != TargetId)
            break;
        if (Words[2] != CargoGoodId)
            break;
        if (!ContainsId(LootIds, Words[0]))
            LootIds.push_back(Words[0]);                                // FUN_06f937c0
        if (State != ScanTractoring)
            break;
        // Here the occupied space is rounded to float first and divided in floating point,
        // unlike LootAcquired.
        float const Occupied = static_cast<float>(Ship->GetCargoSpaceOccupied());
        double const Fill = static_cast<double>(Occupied) / static_cast<double>(Ship->GetCargoHoldSize());
        if (Fill > 0.7f)
        {
            State = ScanCollecting;
            if (LootTimer < 0.0f)
                LootTimer = 3.1f;                                       // DAT_06fb8128
            break;
        }
        if (LootIds.size() != 1)
            break;
        if (LootTimer < 0.0f)
            LootTimer = 3.1f;
        break;
    }

    case EMessage::ObjectDestroyed:
    {
        // 06f0d987.
        if (State == ScanIdle || State == ScanOff)
            break;
        if (Words[0] == TargetId)
        {
            Reset();
            break;
        }
        if (LootIds.empty())
            break;
        std::vector<unsigned int>::iterator Loot =
            std::find(LootIds.begin(), LootIds.end(), Words[0]);
        if (Loot == LootIds.end())
            break;
        LootIds.erase(Loot);
        if (!LootIds.empty())
        {
            if (State == ScanTractoring || State == ScanCollecting)
            {
                if (LootTimer < 0.0f)
                    LootTimer = 3.1f;
            }
        }
        else
        {
            Reset();
        }
        unsigned int const Taker = Words[1];
        if (Taker == 0)
            break;
        if (IsSameShipOrFormation(Ship, Taker))
            break;
        int const Reason = static_cast<int>(Words[4]);
        if (Reason >= 0 && Reason <= 2)
            QueueScanEvent(GCS_EVENT_SCAN_LOOT_TAKEN, Taker, 0, 1);
        SetFormationAttitudes(Ship->GetSystem(), Ship->SpaceObjId, Taker, true, true, -1.0f);
        break;
    }

    case EMessage::SystemExit:
    {
        // 06f0d931.
        if (State == ScanIdle || State == ScanOff)
            break;
        FContentPlayerState* Player = FindContentPlayerState(Words[2]);  // FUN_06f49b00
        if (Player == nullptr)
            break;
        if (Player->ShipObject == nullptr)                              // +0x10
            break;
        if (Player->GetShipId() != TargetId)                            // FUN_06f481e0
            break;
        Reset();
        break;
    }

    case EMessage::CombatState:
        // 06f0d8f1.
        if (Words[1] != Ship->SpaceObjId)
            break;
        if (Words[0] != 0)
            break;
        if (State >= ScanClosingIn && State <= ScanWaitDemand)
            Reset();
        break;

    default:
        break;
    }
    return GetStatus();
}

// The message 0x20 loop (06f0cc88). bStop starts set; a state that moves on clears it and
// the loop runs the new state at once; the loop ends at the first state that leaves bStop
// set, or when a state returns directly.
//   1  (scanning on) ScannedIds empty -> the scanner's own formation first; a target found
//      (FindScanTarget) -> Trail directive at it (distance ScanRange) queued; -> 2.
//   2 / 4  within ScanRange (unrounded distance against the int range) -> 3 / 6.
//   3  scan_announce -> event 0x11, -> 5 (the line's end, message 0xb, gives 4); else -> 4.
//   6  scan (see there).
//   8  AnswerTimer += dt; its integer part (ftol) at 20 or more -> 0xd.
//   0xa target still there -> event 0x14, -> 0xb.
//   0xb / 0xc  the loot timer runs out -> a TractorBeam (0xb, slot 9) / GotoShip (0xc, slot
//      10) directive at the first loot.
//   0xd target still there -> event 0x16, hostility (FUN_06f67d80), Reset.
// A target that vanished or is in a trade lane (FUN_06f57710) in states 6 / 0xa / 0xd ->
// Reset.
void FCargoScanRoute::HandleTimer(float const* DeltaSeconds)
{
    bool bStop = true;
    for (;;)
    {
        switch (State)
        {
        case ScanIdle:
        {
            // 06f0cca8.
            if (!bScanEnabled)
                break;
            if (ScannedIds.empty())
            {
                std::vector<FContentShipBase*> Members;
                CollectFormationMembers(&Members, Ship->FormationId);
                AddMemberIds(&ScannedIds, Members);
            }
            if (!FindScanTarget())                                      // FUN_06f0c6b0
                return;
            State = ScanClosingIn;
            bStop = false;
            TRefHandle<FObjListDirective> Directive;
            CreateObjListDirective(OBJLIST_TRAIL, &Directive.Pointer);
            FObjListTrail* Trail = static_cast<FObjListTrail*>(Directive.Pointer);
            Trail->SetTarget(&TargetId, static_cast<float>(ScanRange));  // FUN_06f275b0
            Trail->Op.Priority = 1;
            Directives.Items.push_back(Directive.Pointer);              // FUN_06f1e390
            Directive->AddRef();
            continue;
        }
        case ScanClosingIn:
        case ScanClosingInAgain:
        {
            // 06f0ce6f / 06f0cf0e. Retail sums (x*x + z*z) + y*y.
            Vector TargetPosition;
            Matrix Orientation;
            pub::SpaceObj::GetLocation(TargetId, TargetPosition, Orientation);
            Vector const ShipPosition = GetShipPosition(Ship);
            double const Dx = static_cast<double>(ShipPosition.x) - TargetPosition.x;
            double const Dy = static_cast<double>(ShipPosition.y) - TargetPosition.y;
            double const Dz = static_cast<double>(ShipPosition.z) - TargetPosition.z;
            double const Distance = std::sqrt((Dx * Dx + Dz * Dz) + Dy * Dy);
            if (!(static_cast<double>(ScanRange) > Distance))
                return;
            State = State == ScanClosingIn ? ScanInRange : ScanScanning;
            bStop = false;
            continue;
        }
        case ScanInRange:
            // 06f0cef2, 06f0d4a2.
            if (bScanAnnounce)
            {
                QueueScanEvent(GCS_EVENT_SCAN_ANNOUNCE, 0, 0, 1);
                State = ScanWaitStandBy;
                return;
            }
            State = ScanClosingInAgain;
            bStop = false;
            continue;
        case ScanScanning:
        {
            // 06f0cf91.
            FContentShipBase* Target = FindSystemShip(Ship, TargetId);
            if (Target == nullptr || Target->IsInTradeLane())
            {
                Reset();
                return;
            }
            FCargoScanRequest Scan(TargetId);
            unsigned int const ShipId = Ship->SpaceObjId;
            pub::SpaceObj::ActivateEquipment(ShipId, &Scan.Info);
            unsigned int Affiliation = Ship->ReputationHandle->GetAffiliation();
            FFactionProps* Props = GetFactionProps(&Affiliation);
            // The wanted good of the highest level found in the cargo; on equal levels the one
            // taking more cargo space (a tie keeps the old one).
            Cargo = nullptr;
            int BestLevel = 0;
            float BestSpace = -1.0f;
            for (std::vector<FFactionScanCargo>::iterator Wanted = Props->ScanForCargo.begin();
                 Wanted != Props->ScanForCargo.end(); ++Wanted)
            {
                EquipDesc* Found = Scan.Result->traverse_equipment_type(Wanted->Archetype, nullptr);
                if (Found == nullptr)
                    continue;
                float const Space = (float)Found->get_cargo_space_occupied();
                if (Wanted->Level > BestLevel ||
                    (Wanted->Level == BestLevel && Space > BestSpace))
                {
                    BestSpace = Space;
                    BestLevel = Wanted->Level;
                    Cargo = Found;
                }
            }

            if (Cargo == nullptr)
            {
                // 06f0d2fb.
                if (bScanAnnounce)
                    QueueScanEvent(GCS_EVENT_SCAN_NOTHING_FOUND, 0, 0, 1);
                Reset();
                bStop = true;
                break;
            }

            unsigned int const ArchId = Cargo->ArchId;
            pub::TranslateArchToGood(ArchId, CargoGoodId);
            CargoCount = Cargo->Count;
            if (FindContentPlayerStateByShip(&TargetId) == nullptr)
            {
                // 06f0d111: an NPC answers through its AI. Mission cargo answers 3,
                // which changes nothing: bStop is cleared and state 6 runs again at once
                // (retail spins there).
                int Response = 3;
                if (!Cargo->bMission)
                {
                    QueueScanEvent(GCS_EVENT_SCAN_CONTRABAND, 0, CargoGoodId, 1);
                    Response = pub::AI::get_scan_response(ShipId, TargetId, CargoGoodId);
                }
                bStop = false;
                if (Response == 0)
                {
                    unsigned short const EquipId = Cargo->Id;           // word +0x02
                    pub::SpaceObj::JettisonEquipment(TargetId, EquipId, CargoCount);
                    State = ScanJettisoned;
                }
                else if (Response >= 1 && Response <= 2)
                {
                    QueueScanEvent(GCS_EVENT_SCAN_RESPONSE, 0, 0, static_cast<unsigned int>(Response));
                    State = ScanHostile;
                }
            }
            else if (Cargo->bMission)
            {
                // 06f0d27e: a player's mission cargo -> hostile (bStop unchanged).
                State = ScanHostile;
            }
            else
            {
                // 06f0d28a: a player is asked to jettison it.
                AnswerTimer = 0.0f;
                QueueScanEvent(GCS_EVENT_SCAN_CONTRABAND, 0, CargoGoodId, 1);
                State = ScanWaitDemand;
                bStop = true;
            }
            break;
        }
        case ScanWaitAnswer:
        {
            // 06f0d3a1. Retail truncates the unrounded sum (ftol) before the compare.
            double const Sum = static_cast<double>(AnswerTimer) + *DeltaSeconds;
            AnswerTimer = static_cast<float>(Sum);
            int const Whole = static_cast<int>(Sum);
            if (static_cast<double>(Whole) < 20.0)
                return;
            State = ScanHostile;
            bStop = false;
            continue;
        }
        case ScanJettisoned:
        {
            // 06f0d4fe.
            FContentShipBase* Target = FindSystemShip(Ship, TargetId);
            if (Target == nullptr || Target->IsInTradeLane())
            {
                Reset();
                return;
            }
            QueueScanEvent(GCS_EVENT_SCAN_JETTISONED, 0, 0, 1);
            State = ScanTractoring;
            return;
        }
        case ScanTractoring:
        case ScanCollecting:
        {
            // 06f0d3df / 06f0d43d. The expiry test uses the unrounded difference.
            if (!(LootTimer > 0.0f))
                break;
            double const Left = static_cast<double>(LootTimer) - *DeltaSeconds;
            LootTimer = static_cast<float>(Left);
            if (!(Left <= 0.0))
                break;
            LootTimer = -1.0f;
            if (LootIds.empty())
                break;
            if (State == ScanTractoring)
                AddTractorBeamDirective(LootIds[0], &Directives.Items);  // slot 9
            else
                AddGotoShipDirective(LootIds[0], &Directives.Items);     // slot 10
            break;
        }
        case ScanHostile:
        {
            // 06f0d584.
            FContentShipBase* Target = FindSystemShip(Ship, TargetId);
            if (Target == nullptr || Target->IsInTradeLane())
            {
                Reset();
                return;
            }
            QueueScanEvent(GCS_EVENT_SCAN_HOSTILE, 0, 0, 1);
            SetFormationAttitudes(Ship->GetSystem(), Ship->SpaceObjId, TargetId, true, true,
                                  -1.0f);                               // DAT_06fb80e8
            Reset();
            return;
        }
        default:
            // 0, 5, 7, 9 and above 0xd.
            break;
        }
        if (bStop)
            return;
    }
}

// ---------------------------------------------------------------------------------------
// Type 3: patrol_path (06fb84d0)
// ---------------------------------------------------------------------------------------

// FUN_06f0aa40: the target guide part inline (base FUN_06f06560 fields,
// vtable 06fb81d8, the description's targets), +0x24 / +0x25 = 1, +0x28 = 0, vtable 06fb84d0;
// the route (0x1c: Ship, Plan.Path, Plan.PathDirection, Plan.PathTargetPoint, Plan.PathPoint,
// State 1) and the scanner (0x64); then RegisterGuidedShip(Ship) (FUN_06f60d60).
FPatrolGuide::FPatrolGuide(FGuideDesc const* Desc)
    : FTargetGuide(Desc)
{
    FPatrolRoute* NewRoute = new FPatrolRoute();
    NewRoute->Ship = Desc->Ship;
    NewRoute->Path = Desc->Plan.Path;                                   // desc +0x38
    NewRoute->Direction = Desc->Plan.PathDirection;                     // desc +0x44
    NewRoute->NextPoint = Desc->Plan.PathTargetPoint;                   // desc +0x40
    NewRoute->EndPoint = Desc->Plan.PathPoint;                          // desc +0x3c
    NewRoute->State = 1;
    Route = NewRoute;
    Scanner = new FCargoScanRoute(Desc->Ship);
    RegisterGuidedShip(Ship);
}

// Clone body (FUN_06f0adb0): the base and target fields, +0x24, +0x25,
// +0x28, then Route->Clone() / Scanner->Clone() (slot 0) when set. No registration.
FPatrolGuide::FPatrolGuide(FPatrolGuide const& Other)
    : FTargetGuide(Other)
    , bUndocked(Other.bUndocked)
    , bScanState(Other.bScanState)
    , State(Other.State)
{
    Route = Other.Route != nullptr ? static_cast<FPatrolRoute*>(Other.Route->Clone()) : nullptr;
    Scanner = Other.Scanner != nullptr ? static_cast<FCargoScanRoute*>(Other.Scanner->Clone())
                                       : nullptr;
}

// FUN_06f0acf0: UnregisterGuidedShip (FUN_06f60da0) when Ship is set, the
// route freed with operator delete (no dtor slot), the scanner through its deleting dtor
// (slot 6) when set.
FPatrolGuide::~FPatrolGuide()
{
    if (Ship != nullptr)
        UnregisterGuidedShip(Ship);
    delete Route;                                                       // FUN_06fa8fe0
    delete Scanner;
}

// FUN_06f0adb0.
FContentGuide* FPatrolGuide::Clone()
{
    return new FPatrolGuide(*this);
}

// LAB_06f0af40. The state loop (a State above 5 spins, as retail):
//   0 -> 1.
//   1  the route's next directives (local list, released afterwards): route finished (3) ->
//      2; else the ship takes them (SetDirectives, priority 0).
//   2  docking: bUndocked 0, scanning off (slot 7), dock at TargetId2 (slot +0x48); -> 3.
//   3  docked (GetShipDockingTarget) -> 20..30 s delay (slot +0x40), -> 4; else swap the
//      targets (slot +0x3c) -> 1.
//   4  launch (slot +0x44), swap the targets; the route's end point = a random path point of
//      the new TargetId2 (FPatrolPath::GetObjectPointIndices; (rand() * count) / 32768, the
//      list not checked for emptiness), route State 1, direction reversed on an open path;
//      -> 1, bUndocked 1, scanning = bScanState.
//   5  the scanner's directives (slot 4): busy (1) -> the ship takes them when there are
//      any; else -> 1.
void FPatrolGuide::Start()
{
    for (;;)
    {
        switch (State)
        {
        case 0:
            State = 1;
            continue;
        case 1:
        {
            FMissionDirectiveList Next;                                 // FUN_06f93790
            if (Route->Update(&Next.Items) == 3)                        // slot 4
            {
                State = 2;
                continue;
            }
            Ship->SetDirectives(&Next.Items, 0);                        // FUN_06f57840
            return;
        }
        case 2:
            bUndocked = false;
            Scanner->SetScanEnabled(false);
            IssueDockDirective();
            State = 3;
            return;
        case 3:
        {
            unsigned int const ShipId = Ship->SpaceObjId;
            unsigned int DockTarget;
            int DockIndex;
            if (GetShipDockingTarget(&ShipId, &DockTarget, &DockIndex, nullptr))   // FUN_06f6eca0
            {
                IssueDelayDirective();
                State = 4;
                return;
            }
            SwapTargets();
            State = 1;
            continue;
        }
        case 4:
        {
            IssueLaunchDirective();
            SwapTargets();
            std::vector<int> Points;
            Route->Path->GetObjectPointIndices(&TargetId2, &Points);   // FUN_06f37d30
            int const Count = static_cast<int>(Points.size());
            Route->EndPoint = Points[(std::rand() * Count) / 32768];
            Route->State = 1;
            if (!Route->Path->bLoop)
                Route->Direction = -Route->Direction;
            State = 1;
            bUndocked = true;
            Scanner->SetScanEnabled(bScanState);
            return;
        }
        case 5:
        {
            FMissionDirectiveList Next;
            if (Scanner->Update(&Next.Items) == 1)                      // slot 4
            {
                if (!Next.Items.empty())
                    Ship->SetDirectives(&Next.Items, 0);
                return;
            }
            State = 1;
            continue;
        }
        default:
            continue;
        }
    }
}

// LAB_06f0aa10: +0x28 = 0.
void FPatrolGuide::Reset()
{
    State = 0;
}

// LAB_06f0af30.
void FPatrolGuide::OnDirectivesDone(int CompletedBehavior, int DirectiveResult)
{
    (void)CompletedBehavior;
    (void)DirectiveResult;
    Start();
}

// FUN_06f0aee0 (RET 8). Both calls go to the SCANNER (+0x30, 06f0aee3 / 06f0aeec), not the
// route: its status before the message (slot 2), then its answer (slot 3). Busy (1) -> State
// 5 and Start; else a changed status -> State 1 and Start. Using the route's status would
// restart the patrol on every message, and message 0x10 (DockBehavior::Disengage's dock
// cancel) would then re-issue the dock directive recursively without end.
void FPatrolGuide::HandleEvent(pub::Controller::EMessage MsgId, void const* Parms)
{
    int const StatusBefore = Scanner->GetStatus();
    int const ScanStatus = Scanner->HandleEvent(MsgId, Parms);
    if (ScanStatus == 1)
    {
        State = 5;
        Start();
        return;
    }
    if (StatusBefore != ScanStatus)
    {
        State = 1;
        Start();
    }
}

// FUN_06f0ad60 (RET 4).
void FPatrolGuide::SetShip(FContentNpcShip* InShip)
{
    if (Ship != nullptr)
        UnregisterGuidedShip(Ship);
    Ship = InShip;
    Route->SetShip(InShip);                                             // slot 5
    Scanner->SetShip(InShip);                                           // slot 5
    if (Ship != nullptr)
        RegisterGuidedShip(Ship);
}

// LAB_06f0aeb0.
void FPatrolGuide::OnScanStateChange(bool bScanning)
{
    bScanState = bScanning;
    Scanner->SetScanEnabled(bScanning && bUndocked);
}

// FUN_06f0b1e0 (RET 0x10). Retail first makes an empty local leg list, the ship's position
// and TargetId2's location, none of which it reads; the port keeps them. The route's slot 6
// answers; when it refuses: type 2 (dock) at TargetId2. Always true.
bool FPatrolGuide::RequestFleeDestination(pub::AI::ContentCallback::FleeDestinationType& OutType,
                                          Vector& OutPosition, unsigned int& OutParam,
                                          float& OutFloat)
{
    std::list<FNnPathLeg> UnusedLegs;
    Vector const ShipPosition = GetShipPosition(Ship);
    (void)ShipPosition;
    Vector TargetPosition;
    Matrix Orientation;
    pub::SpaceObj::GetLocation(TargetId2, TargetPosition, Orientation);
    if (!Route->RequestFleeDestination(OutType, OutPosition, OutParam, OutFloat))
    {
        OutType = static_cast<pub::AI::ContentCallback::FleeDestinationType>(2);
        OutParam = TargetId2;
    }
    return true;
}

// LAB_06f0b2d0.
int FPatrolGuide::GetPathPointsToGo()
{
    return Route->GetPointsToGo();
}
