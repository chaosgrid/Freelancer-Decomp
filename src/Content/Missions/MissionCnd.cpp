// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Missions/MissionCnd.h"
#include "Bases/BaseDB.h"
#include "Core/ContentInternal.h"
#include "Core/ContentPlayerState.h"
#include "Universe/SystemInfo.h"
#include "Missions/MissionAct.h"
#include "Missions/MissionScript.h"
#include "NPCs/NpcShip.h"            // FContentNpcShip : FContentShipBase (Cnd_InTradelane)
#include "NPCs/BehaviorCmds.h"       // FContentShipBase::HasField120
#include "Bases/RtcScripts.h"
#include "common_id.h"
#include "common_iface.h"
#include "common_ini.h"
#include "common_server.h"
#include "server_gameobject.h"       // FSubObjDamageEvent (controller message 0xd)

#include <cmath>
#include <cstdlib>
#include <string.h>

namespace
{
    // DAT_06fb4448 (raw 00 00 00 38) = 2^-15: rand() scale in Cnd_Timer::Parse.
    float const GRandUnitScale = 1.0f / 32768.0f;

    // DAT_06fcefc8 (TU21 initializer LAB_06ebf730): CreateID(null), the default of the
    // Cnd_DistVecLbl and Cnd_LootAcquired label fields.
    unsigned int const GNullId = CreateID(nullptr);

    // DAT_06fcefc4..06fcefb4 (TU21 initializers 06ec4200-06ec4290): the state names
    // Cnd_WatchTrigger compares against.
    unsigned int const GIdStateActive = CreateID("ACTIVE");     // DAT_06fcefc4
    unsigned int const GIdStateInactive = CreateID("INACTIVE"); // DAT_06fcefc0
    unsigned int const GIdStateComplete = CreateID("COMPLETE"); // DAT_06fcefbc
    unsigned int const GIdStateOn = CreateID("ON");             // DAT_06fcefb8
    unsigned int const GIdStateOff = CreateID("OFF");           // DAT_06fcefb4

    // FUN_06f49b00 followed by the mission's cached player state (+0x08, FUN_06f1ca50).
    FContentPlayerState* GetMissionPlayerState(FMissionScript* Mission)
    {
        if (FindContentPlayerState(Mission->PlayerId) == nullptr)
            return nullptr;
        return Mission->PlayerState;
    }

    // FUN_06f478b0.
    unsigned int GetPlayerSystemId(FContentPlayerState* Player)
    {
        if (Player->SystemRecord == nullptr)
            return 0;
        return Player->SystemRecord->SystemId;
    }

    // FUN_06f481e0.
    unsigned int GetPlayerShipId(FContentPlayerState* Player)
    {
        if (Player->ShipObject == nullptr)
            return 0;
        return Player->ShipObject->SpaceObjId;
    }

    // Shared Parse of Cnd_BaseEnter/Cnd_BaseExit (FUN_06ebf770). "ANY" (DAT_06fb579c) is
    // case-sensitive.
    void ParseBaseCondition(INI_Reader& Reader, unsigned int& BaseId)
    {
        TString<0x20> Name;
        Name.Length = 0;
        Name.Data[0] = '\0';
        Name = Reader.get_value_string(0);
        if (strcmp(Name.Data, "ANY") != 0)
            pub::GetBaseID(BaseId, Name.Data);
    }

    // Shared Matches LAB_06ebf750 (Cnd_BaseEnter/Exit, Cnd_SpaceEnter/Exit): an unset id
    // matches anything.
    bool MatchesIdOrAny(unsigned int Id, void const* EventParms)
    {
        if (EventParms == nullptr)
            return false;
        if ((Id & GIdValueMask) == 0)
            return true;
        return *static_cast<unsigned int const*>(EventParms) == Id;
    }

    // Shared Parse of Cnd_LocEnter/Cnd_LocExit (FUN_06ec2f70).
    void ParseLocationCondition(INI_Reader& Reader, unsigned int& LocationId, unsigned int& BaseId)
    {
        pub::GetBaseID(BaseId, Reader.get_value_string(1));
        LocationId = pub::GF::FindLocation(BaseId, Reader.get_value_string(0));
    }

    // Shared Matches of Cnd_LocEnter/Cnd_LocExit (FUN_06ec2fc0).
    bool MatchesPlayerLocation(FMissionScript* Mission, unsigned int LocationId,
                               unsigned int BaseId, void const* EventParms)
    {
        if (EventParms == nullptr)
            return false;
        if (FindContentPlayerState(Mission->PlayerId) == nullptr)
            return false;
        FContentBase* const Base = Mission->PlayerState->BaseRecord;
        if (Base == nullptr)
            return false;
        if (Base->BaseId != BaseId)
            return false;
        return LocationId == *static_cast<unsigned int const*>(EventParms);
    }

    // Shared Parse of Cnd_CharSelect/Cnd_RumorHeard (FUN_06ebfa20).
    void ParseCharacterCondition(INI_Reader& Reader, TString<0x20>& Name, unsigned int& BaseId,
                                 unsigned int& LocationId)
    {
        Name = Reader.get_value_string(0);
        pub::GetBaseID(BaseId, Reader.get_value_string(2));
        LocationId = pub::GF::FindLocation(BaseId, Reader.get_value_string(1));
    }

    // Shared Parse of Cnd_SpaceEnter/Cnd_SpaceExit (FUN_06ec1d00). "no_params" is
    // case-sensitive.
    void ParseSpaceCondition(INI_Reader& Reader, unsigned int& SystemId)
    {
        TString<0x40> Name;
        Name.Data[0] = '\0';
        Name = Reader.get_value_string(0);
        if (Reader.is_value_empty(0))
            return;
        if (strcmp(Name.Data, "no_params") == 0)
            return;
        pub::GetSystemID(SystemId, Reader.get_value_string(0));
    }

    // TString ctor idiom: dword 0 / byte 0, then "" (FUN_06ed0160 / FUN_06ead6c0).
    template <unsigned N>
    void InitEmpty(TString<N>& Value)
    {
        Value.Length = 0;
        Value.Data[0] = '\0';
    }

    // The three x87 square-if-positive idioms of the distance parsers differ only in how
    // they treat 0 and NaN.
    // Squared only when Value > 0.
    float SquareIfGreater(float Value)
    {
        if (Value > 0.0f)
            return Value * Value;
        return Value;
    }

    // Squared when greater or unordered.
    float SquareUnlessLessOrEqual(float Value)
    {
        if (!(Value <= 0.0f))
            return Value * Value;
        return Value;
    }

    // Squared when greater, equal or unordered.
    float SquareUnlessLess(float Value)
    {
        if (!(Value < 0.0f))
            return Value * Value;
        return Value;
    }

    // Cnd_DistShip / Cnd_DistVec TickMode (+0x84 / +0x5c): when the timer runs.
    enum ECndTickMode
    {
        CND_TICK_ALWAYS = 0,       // "TICK_ALWAYS"
        CND_TICK_TOWARDS = 1,      // "TICK_TOWARDS": while the distance does not grow
        CND_TICK_AWAY = 2,         // "TICK_AWAY": while it does not shrink
    };

    // Cnd_DistShip / Cnd_DistVec: a missing or unknown slot leaves the mode unchanged.
    void ParseTickMode(INI_Reader& Reader, unsigned int Slot, int& TickMode)
    {
        if (Reader.is_value_empty(Slot))
            return;
        if (_stricmp("TICK_AWAY", Reader.get_value_string(Slot)) == 0)
        {
            TickMode = CND_TICK_AWAY;
            return;
        }
        if (_stricmp("TICK_TOWARDS", Reader.get_value_string(Slot)) == 0)
        {
            TickMode = CND_TICK_TOWARDS;
            return;
        }
        if (_stricmp("TICK_ALWAYS", Reader.get_value_string(Slot)) == 0)
            TickMode = CND_TICK_ALWAYS;
    }

    // Shared tail of Cnd_DistShip / Cnd_DistVec Matches. DistanceSq is the float the x87 sum
    // was stored to.
    bool TickDistanceCondition(float DistanceSq, float LimitSq, unsigned char bInside,
                               float& TickTimer, float TickInterval, int TickMode,
                               float& LastDistanceSq, float DeltaSeconds)
    {
        // Out of range (and unordered) reloads the timer and fails.
        bool const bInRange = (bInside != 0) ? (DistanceSq < LimitSq) : (DistanceSq > LimitSq);
        if (!bInRange)
        {
            TickTimer = TickInterval;
            return false;
        }
        double const Delta = static_cast<double>(DistanceSq) - static_cast<double>(LastDistanceSq);
        LastDistanceSq = DistanceSq;
        bool bTick = false;
        if (TickMode == CND_TICK_ALWAYS)
            bTick = true;
        // TICK_TOWARDS ticks when the distance did not grow.
        else if (Delta <= 0.0 && TickMode == CND_TICK_TOWARDS)
            bTick = true;
        // TICK_AWAY ticks when it did not shrink.
        else if (Delta >= 0.0 && TickMode == CND_TICK_AWAY)
            bTick = true;
        if (bTick)
            TickTimer -= DeltaSeconds;
        return TickTimer <= 0.0f;
    }

    // FUN_06f1d430 (TU38): non-empty and starts with a digit or '-'.
    bool IsNumericToken(char const* Text)
    {
        if (Text == nullptr)
            return false;
        char const First = Text[0];
        if (First == '\0')
            return false;
        if (First >= '0' && First <= '9')
            return true;
        return First == '-';
    }
}

// Inlined in FUN_06ebb850 and the Cnd_* ctors.
Cnd::Cnd(FMissionScript* InMission)
    : Mission(InMission)
    , TriggerId(0)
{
}

// LAB_06ebf6d0.
Cnd::~Cnd()
{
}

// FUN_06ebf720.
void Cnd::OnActivate()
{
}

// FUN_06ebf720.
void Cnd::OnMissionEnd()
{
}

// FUN_06ebf6f0.
void Cnd::Save(FILE* /*File*/)
{
}

// FUN_06ebf6f0.
void Cnd::Load(INI_Reader* /*Reader*/)
{
}

// FUN_06ebf700.
void Cnd::SetTriggerId(unsigned int Id)
{
    TriggerId = Id;
}

// ---------------------------------------------------------------------------------------
// Cnd_BaseEnter / Cnd_BaseExit
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850.
CndBaseEnter::CndBaseEnter(FMissionScript* InMission)
    : Cnd(InMission)
    , BaseId(0)
{
}

// FUN_06ebf770.
void CndBaseEnter::Parse(INI_Reader& Reader)
{
    ParseBaseCondition(Reader, BaseId);
}

// LAB_06ebf750: parms = {base id, ...}.
bool CndBaseEnter::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    return MatchesIdOrAny(BaseId, EventParms);
}

// Inlined in FUN_06ebb850.
CndBaseExit::CndBaseExit(FMissionScript* InMission)
    : Cnd(InMission)
    , BaseId(0)
{
}

// FUN_06ebf770.
void CndBaseExit::Parse(INI_Reader& Reader)
{
    ParseBaseCondition(Reader, BaseId);
}

// LAB_06ebf750.
bool CndBaseExit::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    return MatchesIdOrAny(BaseId, EventParms);
}

// ---------------------------------------------------------------------------------------
// Cnd_CargoScanned
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850.
CndCargoScanned::CndCargoScanned(FMissionScript* InMission)
    : Cnd(InMission)
    , ScannerId(0)
    , ScannedId(0)
{
    InitEmpty(ScannerLabel);
    InitEmpty(ScannedLabel);
}

// FUN_06ebf810: only when slots 0 and 1 are both present.
void CndCargoScanned::Parse(INI_Reader& Reader)
{
    if (Reader.is_value_empty(0))
        return;
    if (Reader.is_value_empty(1))
        return;
    ScannerLabel = Reader.get_value_string(0);
    ScannedLabel = Reader.get_value_string(1);
}

// FUN_06ebf940: parms = {scanner id, scanned id}. A label "none" (DAT_06fb57a0, case-sensitive)
// is not resolved; unresolved ids are looked up lazily (GetLabelShipId = FUN_06f15940).
bool CndCargoScanned::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    if (EventParms == nullptr)
        return false;
    bool bScannerMissing = false;
    if (strcmp(ScannerLabel.Data, "none") != 0 && (ScannerId & GIdValueMask) == 0)
    {
        ScannerId = Mission->GetLabelShipId(&ScannerLabel);
        if ((ScannerId & GIdValueMask) == 0)
            bScannerMissing = true;
    }
    if (strcmp(ScannedLabel.Data, "none") != 0 && (ScannedId & GIdValueMask) == 0)
    {
        ScannedId = Mission->GetLabelShipId(&ScannedLabel);
        if ((ScannedId & GIdValueMask) == 0)
            return false;
    }
    if (bScannerMissing)
        return false;
    unsigned int const* Parms = static_cast<unsigned int const*>(EventParms);
    bool const bScannerMatch = ScannerId == Parms[0];
    bool const bScannedMatch = ScannedId == Parms[1];
    if ((ScannerId & GIdValueMask) != 0 && (ScannedId & GIdValueMask) != 0)
        return bScannerMatch && bScannedMatch;
    if ((ScannerId & GIdValueMask) == 0)
        return false;
    return bScannerMatch;
}

// LAB_06ebf8f0.
void CndCargoScanned::OnActivate()
{
    if (strcmp(ScannerLabel.Data, "none") != 0)
        ScannerId = Mission->GetLabelShipId(&ScannerLabel);
    if (strcmp(ScannedLabel.Data, "none") != 0)
        ScannedId = Mission->GetLabelShipId(&ScannedLabel);
}

// ---------------------------------------------------------------------------------------
// Cnd_CharSelect
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850.
CndCharSelect::CndCharSelect(FMissionScript* InMission)
    : Cnd(InMission)
    , LocationId(0)
    , BaseId(0)
{
    InitEmpty(Character);
}

// FUN_06ebfa20.
void CndCharSelect::Parse(INI_Reader& Reader)
{
    ParseCharacterCondition(Reader, Character, BaseId, LocationId);
}

// FUN_06ebfac0 (FindRoomState = FUN_06eac110, FindCharacter = FUN_06eac240).
bool CndCharSelect::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    if (EventParms == nullptr)
        return false;
    if (FindContentPlayerState(Mission->PlayerId) == nullptr)
        return false;
    // No null test on this base record, as in retail; the second lookup below is the tested
    // one.
    if (Mission->PlayerState->GetBaseRecord()->BaseId != BaseId)
        return false;
    // The mission's current location is +0x308.
    if (LocationId != Mission->CurrentLocationId)
        return false;
    FContentBase* const Base = Mission->PlayerState->GetBaseRecord();
    if (Base == nullptr)
        return false;
    FBaseRoomState* const Room = Base->FindRoomState(&Mission->CurrentLocationId);
    if (Room == nullptr)
        return false;
    FBarCharacterList::iterator Found;
    if (!Room->FindCharacter(&Character, Mission->PlayerId, &Found))
        return false;
    return Found->CharacterId == *static_cast<unsigned int const*>(EventParms);
}

// ---------------------------------------------------------------------------------------
// Cnd_CommComplete
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850.
CndCommComplete::CndCommComplete(FMissionScript* InMission)
    : Cnd(InMission)
    , CommId(0)
    , SourceObjId(0)
{
    InitEmpty(SourceLabel);
}

// FUN_06ec0ee0.
void CndCommComplete::Parse(INI_Reader& Reader)
{
    CommId = pub::GetNicknameId(Reader.get_value_string(0));
    if (!Reader.is_value_empty(1))
        SourceLabel = Reader.get_value_string(1);
}

// FUN_06ec0f90: parms+0x0c = comm id, parms+0x08 = sender id.
bool CndCommComplete::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    if (EventParms == nullptr)
        return false;
    unsigned int const* Parms = static_cast<unsigned int const*>(EventParms);
    if (SourceLabel.Length == 0)
        return Parms[3] == CommId;
    SourceObjId = Mission->GetLabelShipId(&SourceLabel);
    if ((SourceObjId & GIdValueMask) == 0)
        return false;
    if (Parms[3] != CommId)
        return false;
    return Parms[2] == SourceObjId;
}

// FUN_06ec0f70.
void CndCommComplete::OnActivate()
{
    if (SourceLabel.Length != 0)
        SourceObjId = Mission->GetLabelShipId(&SourceLabel);
}

// ---------------------------------------------------------------------------------------
// Cnd_CmpToPlane
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850.
CndCmpToPlane::CndCmpToPlane(FMissionScript* InMission)
    : Cnd(InMission)
    , PlaneA(0.0f)
    , PlaneB(0.0f)
    , PlaneC(0.0f)
    , PlaneD(0.0f)
    , Side(kCndPlaneInside)
    , ObjectId(0)
{
    InitEmpty(ObjectLabel);
}

// FUN_06ec0ff0.
void CndCmpToPlane::Parse(INI_Reader& Reader)
{
    if (_stricmp("inside", Reader.get_value_string(0)) == 0)
        Side = kCndPlaneInside;
    else if (_stricmp("outside", Reader.get_value_string(0)) == 0)
        Side = kCndPlaneOutside;
    ObjectLabel = Reader.get_value_string(1);
    PlaneA = Reader.get_value_float(2);
    PlaneB = Reader.get_value_float(3);
    PlaneC = Reader.get_value_float(4);
    PlaneD = Reader.get_value_float(5);
}

// LAB_06ec10e0: below the plane matches "inside", above it "outside"; on the plane (or NaN)
// neither.
bool CndCmpToPlane::Matches(pub::Controller::EMessage /*EventId*/, void const* /*EventParms*/)
{
    FContentPlayerState* Player = GetMissionPlayerState(Mission);
    if (Player == nullptr)
        return false;
    if (Mission->PlayerState->ShipObject == nullptr)
        return false;
    if (Mission->PlayerState->GetSystemRecord() == nullptr)
        return false;
    if ((ObjectId & GIdValueMask) == 0)
        ObjectId = Mission->GetLabelShipId(&ObjectLabel);
    if ((ObjectId & GIdValueMask) == 0)
        return false;
    Vector Position;
    Matrix Orientation;
    if (pub::SpaceObj::GetLocation(ObjectId, Position, Orientation) != 0)
        return false;
    float const Value = Position.z * PlaneC + Position.y * PlaneB + Position.x * PlaneA + PlaneD;
    if (0.0f > Value && Side == kCndPlaneInside)
        return true;
    if (0.0f < Value)
        return Side == kCndPlaneOutside;
    return false;
}

// FUN_06ec10c0.
void CndCmpToPlane::OnActivate()
{
    ObjectId = Mission->GetLabelShipId(&ObjectLabel);
}

// ---------------------------------------------------------------------------------------
// Cnd_Destroyed
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850; +0x10 takes a register that holds -1 at that point.
CndDestroyed::CndDestroyed(FMissionScript* InMission)
    : Cnd(InMission)
    , Count(0)
    , Needed(-1)
    , Label(0)
    , KillerObjId(0)
    , Mode(CND_DESTROYED_ALL_IGNORE_LANDING)
{
    InitEmpty(KillerLabel);
}

// FUN_06ec0b90.
void CndDestroyed::Parse(INI_Reader& Reader)
{
    Label = CreateID(Reader.get_value_string(0));
    // The count is taken only when >= -1.
    if (!Reader.is_value_empty(1) && Reader.get_value_int(1) >= -1)
        Needed = Reader.get_value_int(1);
    if (!Reader.is_value_empty(2))
    {
        char const* ModeName = Reader.get_value_string(2);
        if (_stricmp(ModeName, "EXPLODE") == 0)
            Mode = CND_DESTROYED_EXPLODE;
        else if (_stricmp(ModeName, "SILENT") == 0)
            Mode = CND_DESTROYED_SILENT;
        else if (_stricmp(ModeName, "ALL") == 0)
            Mode = CND_DESTROYED_ALL;
        else if (_stricmp(ModeName, "ALL_IGNORE_LANDING") == 0)
            Mode = CND_DESTROYED_ALL_IGNORE_LANDING;
    }
    if (!Reader.is_value_empty(3))
        KillerLabel = Reader.get_value_string(3);
}

// LAB_06ec0cf0. Parms: +0 destroyed object id, +4 killer id, +0x10 destroy type.
bool CndDestroyed::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    if (EventParms == nullptr)
        return false;
    if (FindContentPlayerState(Mission->PlayerId) == nullptr)
        return false;
    unsigned int const* Parms = static_cast<unsigned int const*>(EventParms);
    // A different destroy type only passes for ALL, or for ALL_IGNORE_LANDING while the
    // player has a ship.
    if (static_cast<int>(Parms[4]) != Mode && Mode != CND_DESTROYED_ALL)
    {
        if (Mission->PlayerState->ShipObject == nullptr)
            return false;
        if (Mode != CND_DESTROYED_ALL_IGNORE_LANDING)
            return false;
    }
    if (!Mission->HasObjectLabel(Parms[0], &Label))
        return false;
    if (KillerLabel.Length != 0 && (KillerObjId & GIdValueMask) == 0)
        KillerObjId = Mission->GetLabelShipId(&KillerLabel);
    if ((KillerObjId & GIdValueMask) != 0 && KillerObjId != Parms[1])
        return false;
    if (Needed == -1)
    {
        // Retail takes the equal_range in the mission +0xec multimap: true unless more than
        // one entry is left.
        return Mission->LabelObjects.count(Label) <= 1;
    }
    ++Count;
    return Count == Needed;
}

// FUN_06ec0cc0.
void CndDestroyed::OnActivate()
{
    Count = 0;
    if (KillerLabel.Length != 0)
        KillerObjId = Mission->GetLabelShipId(&KillerLabel);
}

// LAB_06ec0e70.
void CndDestroyed::Save(FILE* File)
{
    fprintf(File, "Cnd_Destroyed = %d\n", Count);
}

// FUN_06ec0e90.
void CndDestroyed::Load(INI_Reader* Reader)
{
    while (Reader->read_value())
    {
        if (Reader->is_value("Cnd_Destroyed"))
            Count = Reader->get_value_int(0);
    }
}

// ---------------------------------------------------------------------------------------
// Cnd_DistCircle
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850 (no "" assignment).
CndDistCircle::CndDistCircle(FMissionScript* InMission)
    : Cnd(InMission)
    , CircleObjId(0)
    , Radius(0.0f)
    , PlaneOffset(0.0f)
{
    InitEmpty(ObjectLabel);
    InitEmpty(CircleObj);
    LastPosition.x = LastPosition.y = LastPosition.z = 0.0f;
    CircleCenter.x = CircleCenter.y = CircleCenter.z = 0.0f;
    Normal.x = Normal.y = Normal.z = 0.0f;
}

// FUN_06ec0360.
void CndDistCircle::Parse(INI_Reader& Reader)
{
    ObjectLabel = Reader.get_value_string(0);
    CircleObj = Reader.get_value_string(1);
}

// LAB_06ec0530: true when the object's move since the last tick crosses the circle's plane
// in the axis direction, within Radius of the circle centre.
bool CndDistCircle::Matches(pub::Controller::EMessage /*EventId*/, void const* /*EventParms*/)
{
    unsigned int const ObjectId = Mission->GetLabelShipId(&ObjectLabel);
    if (FindContentPlayerState(Mission->PlayerId) == nullptr)
        return false;
    if (Mission->PlayerState->ShipObject == nullptr)
        return false;
    if (Mission->PlayerState->GetSystemRecord() == nullptr)
        return false;
    if ((ObjectId & GIdValueMask) == 0)
        return false;
    // Retail copies its stack vector into +0x78 even when GetLocation fails; the port starts
    // it at zero instead of an indeterminate value.
    Vector Position;
    Position.x = Position.y = Position.z = 0.0f;
    Matrix Orientation;
    bool bResult = false;
    if (pub::SpaceObj::GetLocation(ObjectId, Position, Orientation) == 0)
    {
        double const Dx = static_cast<double>(Position.x) - LastPosition.x;
        double const Dy = static_cast<double>(Position.y) - LastPosition.y;
        double const Dz = static_cast<double>(Position.z) - LastPosition.z;
        // A move parallel to the plane fails.
        double const Denom = Dy * Normal.y + Dz * Normal.z + Dx * Normal.x;
        if (!(Denom == 0.0))
        {
            double const Param = (static_cast<double>(Position.y) * Normal.y +
                                  static_cast<double>(Position.z) * Normal.z +
                                  static_cast<double>(Position.x) * Normal.x + PlaneOffset) /
                                 Denom;
            // The move direction, normalised (dz goes through a float).
            float const DzFloat = static_cast<float>(Dz);
            double const Length = sqrt(Dz * DzFloat + Dx * Dx + Dy * Dy);
            float const InvLength = static_cast<float>(1.0 / Length);
            float const DirX = static_cast<float>(Dx * InvLength);
            float const DirY = static_cast<float>(Dy * InvLength);
            float const DirZ = static_cast<float>(static_cast<double>(DzFloat) * InvLength);
            if (0.0 < Param && Param < 1.0)
            {
                double const Along = static_cast<double>(DirZ) * Normal.z +
                                     static_cast<double>(DirY) * Normal.y +
                                     static_cast<double>(DirX) * Normal.x;
                if (Along > 0.0)
                {
                    double const Cx = static_cast<double>(CircleCenter.x) - Position.x;
                    double const Cy = static_cast<double>(CircleCenter.y) - Position.y;
                    double const Cz = static_cast<double>(CircleCenter.z) - Position.z;
                    if (sqrt(Cz * Cz + Cx * Cx + Cy * Cy) < Radius)
                        bResult = true;
                }
            }
        }
    }
    LastPosition = Position;
    return bResult;
}

// FUN_06ec0410.
void CndDistCircle::OnActivate()
{
    CircleObjId = pub::GetNicknameId(CircleObj.Data);
    Matrix Orientation;
    pub::SpaceObj::GetLocation(CircleObjId, CircleCenter, Orientation);
    Vector Forward;
    Forward.x = 0.0f;
    Forward.y = 0.0f;
    Forward.z = -1.0f;
    // TransformMatrix is engine vtbl +0x2c.
    Vector Axis;
    ContentMathEngine()->TransformMatrix(Axis, Orientation, Forward);
    Normal.x = Axis.x;
    Normal.y = Axis.y;
    Normal.z = Axis.z;
    // (x*x + y*y) + z*z, 1/sqrt kept on the x87 stack.
    double const InvLength =
        1.0 / sqrt(static_cast<double>(Normal.x) * Normal.x +
                   static_cast<double>(Normal.y) * Normal.y +
                   static_cast<double>(Normal.z) * Normal.z);
    Normal.x = static_cast<float>(InvLength * Normal.x);
    Normal.y = static_cast<float>(InvLength * Normal.y);
    Normal.z = static_cast<float>(InvLength * Normal.z);
    PlaneOffset = static_cast<float>(-(static_cast<double>(CircleCenter.z) * Normal.z +
                                       static_cast<double>(CircleCenter.y) * Normal.y +
                                       static_cast<double>(CircleCenter.x) * Normal.x));
    // GetRadius overwrites the centre (+0x84) after the plane offset is taken.
    pub::SpaceObj::GetRadius(CircleObjId, Radius, CircleCenter);
    unsigned int const ObjectId = Mission->GetLabelShipId(&ObjectLabel);
    if ((ObjectId & GIdValueMask) != 0)
    {
        Matrix ObjectOrientation;
        pub::SpaceObj::GetLocation(ObjectId, LastPosition, ObjectOrientation);
    }
}

// ---------------------------------------------------------------------------------------
// Cnd_DistShip
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850.
CndDistShip::CndDistShip(FMissionScript* InMission)
    : Cnd(InMission)
    , DistanceSq(0.0f)
    , bInside(0)
    , TickTimer(0.0f)
    , TickInterval(0.0f)
    , TickMode(CND_TICK_ALWAYS)
    , LastDistanceSq(0.0f)
    , ObjectAId(0)
    , ObjectBId(0)
{
    InitEmpty(ObjectA);
    InitEmpty(ObjectB);
}

// FUN_06ec0720.
void CndDistShip::Parse(INI_Reader& Reader)
{
    char const* SideName = Reader.get_value_string(0);
    if (_stricmp(SideName, "inside") == 0)
        bInside = 1;
    else if (_stricmp(SideName, "outside") == 0)
        bInside = 0;
    ObjectA = Reader.get_value_string(1);
    ObjectB = Reader.get_value_string(2);
    DistanceSq = SquareIfGreater(Reader.get_value_float(3));
    if (!Reader.is_value_empty(4) && Reader.get_value_float(4) > 0.0f)
        TickInterval = Reader.get_value_float(4);
    ParseTickMode(Reader, 5, TickMode);
}

// LAB_06ec0930. Parms: the frame time (float). Both ids are re-resolved every tick.
bool CndDistShip::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    if (EventParms == nullptr)
        return false;
    if (FindContentPlayerState(Mission->PlayerId) == nullptr)
        return false;
    if (Mission->PlayerState->ShipObject == nullptr)
        return false;
    if (Mission->PlayerState->GetSystemRecord() == nullptr)
        return false;
    ObjectAId = Mission->GetLabelObjectId(&ObjectA);
    if ((ObjectAId & GIdValueMask) == 0)
        return false;
    Vector PositionA;
    Matrix Orientation;
    if (pub::SpaceObj::GetLocation(ObjectAId, PositionA, Orientation) != 0)
        return false;
    ObjectBId = Mission->GetLabelObjectId(&ObjectB);
    if ((ObjectBId & GIdValueMask) == 0)
        return false;
    Vector PositionB;
    if (pub::SpaceObj::GetLocation(ObjectBId, PositionB, Orientation) != 0)
        return false;
    // Both objects must be in the same system (GetSystem results ignored).
    unsigned int SystemA = 0;
    unsigned int SystemB = 0;
    pub::SpaceObj::GetSystem(ObjectAId, SystemA);
    pub::SpaceObj::GetSystem(ObjectBId, SystemB);
    if (SystemA != SystemB)
        return false;
    // (dz*dz + dy*dy) + dx*dx on the x87 stack, stored as float.
    double const Dx = static_cast<double>(PositionA.x) - static_cast<double>(PositionB.x);
    double const Dy = static_cast<double>(PositionA.y) - static_cast<double>(PositionB.y);
    double const Dz = static_cast<double>(PositionA.z) - static_cast<double>(PositionB.z);
    float const CurrentSq = static_cast<float>(Dz * Dz + Dy * Dy + Dx * Dx);
    return TickDistanceCondition(CurrentSq, DistanceSq, bInside, TickTimer, TickInterval,
                                 TickMode, LastDistanceSq,
                                 *static_cast<float const*>(EventParms));
}

// FUN_06ec08e0.
void CndDistShip::OnActivate()
{
    if (ObjectA.Length != 0)
        ObjectAId = Mission->GetLabelObjectId(&ObjectA);
    if (ObjectB.Length != 0)
        ObjectBId = Mission->GetLabelObjectId(&ObjectB);
    TickTimer = TickInterval;
    LastDistanceSq = 0.0f;
}

// LAB_06ec0b00.
void CndDistShip::Save(FILE* File)
{
    fprintf(File, "Cnd_DistShip = %f, %f\n", static_cast<double>(TickTimer),
            static_cast<double>(LastDistanceSq));
}

// FUN_06ec0b30.
void CndDistShip::Load(INI_Reader* Reader)
{
    while (Reader->read_value())
    {
        if (Reader->is_value("Cnd_DistShip"))
        {
            TickTimer = Reader->get_value_float(0);
            LastDistanceSq = Reader->get_value_float(1);
        }
    }
}

// ---------------------------------------------------------------------------------------
// Cnd_DistVec
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850.
CndDistVec::CndDistVec(FMissionScript* InMission)
    : Cnd(InMission)
    , PositionX(0.0f)
    , PositionY(0.0f)
    , PositionZ(0.0f)
    , DistanceSq(0.0f)
    , bInside(0)
    , TickTimer(0.0f)
    , TickInterval(0.0f)
    , TickMode(CND_TICK_ALWAYS)
    , LastDistanceSq(0.0f)
    , ObjectId(0)
{
    InitEmpty(ObjectLabel);
}

// FUN_06ebfb60.
void CndDistVec::Parse(INI_Reader& Reader)
{
    char const* SideName = Reader.get_value_string(0);
    if (_stricmp(SideName, "inside") == 0)
        bInside = 1;
    else if (_stricmp(SideName, "outside") == 0)
        bInside = 0;
    ObjectLabel = Reader.get_value_string(1);
    PositionX = Reader.get_value_float(2);
    PositionY = Reader.get_value_float(3);
    PositionZ = Reader.get_value_float(4);
    DistanceSq = SquareUnlessLessOrEqual(Reader.get_value_float(5));
    if (!Reader.is_value_empty(6) && Reader.get_value_float(6) >= 0.0f)
        TickInterval = Reader.get_value_float(6);
    ParseTickMode(Reader, 7, TickMode);
}

// LAB_06ebfd20. Parms: the frame time (float). The id is re-resolved every tick with
// FUN_06f15b90 (OnActivate uses FUN_06f15940).
bool CndDistVec::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    if (EventParms == nullptr)
        return false;
    if (FindContentPlayerState(Mission->PlayerId) == nullptr)
        return false;
    if (Mission->PlayerState->ShipObject == nullptr)
        return false;
    if (Mission->PlayerState->GetSystemRecord() == nullptr)
        return false;
    ObjectId = Mission->GetLabelObjectId(&ObjectLabel);
    if ((ObjectId & GIdValueMask) == 0)
        return false;
    Vector Position;
    Matrix Orientation;
    if (pub::SpaceObj::GetLocation(ObjectId, Position, Orientation) != 0)
        return false;
    // (dz*dz + dx*dx) + dy*dy on the x87 stack, stored as float.
    double const Dx = static_cast<double>(Position.x) - static_cast<double>(PositionX);
    double const Dy = static_cast<double>(Position.y) - static_cast<double>(PositionY);
    double const Dz = static_cast<double>(Position.z) - static_cast<double>(PositionZ);
    float const CurrentSq = static_cast<float>(Dz * Dz + Dx * Dx + Dy * Dy);
    return TickDistanceCondition(CurrentSq, DistanceSq, bInside, TickTimer, TickInterval,
                                 TickMode, LastDistanceSq,
                                 *static_cast<float const*>(EventParms));
}

// FUN_06ebfcf0.
void CndDistVec::OnActivate()
{
    if (ObjectLabel.Length != 0)
        ObjectId = Mission->GetLabelShipId(&ObjectLabel);
    TickTimer = TickInterval;
    LastDistanceSq = 0.0f;
}

// LAB_06ebfe60.
void CndDistVec::Save(FILE* File)
{
    fprintf(File, "Cnd_DistVec = %f, %f\n", static_cast<double>(TickTimer),
            static_cast<double>(LastDistanceSq));
}

// FUN_06ebfe90.
void CndDistVec::Load(INI_Reader* Reader)
{
    while (Reader->read_value())
    {
        if (Reader->is_value("Cnd_DistVec"))
        {
            TickTimer = Reader->get_value_float(0);
            LastDistanceSq = Reader->get_value_float(1);
        }
    }
}

// ---------------------------------------------------------------------------------------
// Cnd_DistVecLbl
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850.
CndDistVecLbl::CndDistVecLbl(FMissionScript* InMission)
    : Cnd(InMission)
    , PositionX(0.0f)
    , PositionY(0.0f)
    , PositionZ(0.0f)
    , Label(GNullId)
    , DistanceSq(0.0f)
    , bInside(0)
    , bAny(0)
    , TickTimer(0.0f)
    , TickInterval(0.0f)
{
}

// FUN_06ebfef0.
void CndDistVecLbl::Parse(INI_Reader& Reader)
{
    char const* SideName = Reader.get_value_string(0);
    if (_stricmp(SideName, "inside") == 0)
        bInside = 1;
    else if (_stricmp(SideName, "outside") == 0)
        bInside = 0;
    bAny = (_stricmp(Reader.get_value_string(1), "any") == 0) ? 1 : 0;
    Label = CreateID(Reader.get_value_string(2));
    PositionX = Reader.get_value_float(3);
    PositionY = Reader.get_value_float(4);
    PositionZ = Reader.get_value_float(5);
    DistanceSq = SquareUnlessLessOrEqual(Reader.get_value_float(6));
    if (!Reader.is_value_empty(7) && Reader.get_value_float(7) >= 0.0f)
        TickInterval = Reader.get_value_float(7);
}

// FUN_06ec0280. Parms: the frame time (float).
bool CndDistVecLbl::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    if (EventParms == nullptr)
        return false;
    if (FindContentPlayerState(Mission->PlayerId) == nullptr)
        return false;
    if (Mission->PlayerState->ShipObject == nullptr)
        return false;
    if (Mission->PlayerState->GetSystemRecord() == nullptr)
        return false;
    float const DeltaSeconds = *static_cast<float const*>(EventParms);
    if (bAny != 0)
        return MatchesAny(DeltaSeconds);
    return MatchesAll(DeltaSeconds);
}

// FUN_06ec0000: each labelled object whose location is known either reloads the timer (out
// of range) or ticks it; the first tick that reaches <= 0 wins.
bool CndDistVecLbl::MatchesAny(float DeltaSeconds)
{
    typedef std::multimap<unsigned int, unsigned int>::iterator FMemberIterator;
    FMemberIterator It = Mission->LabelObjects.lower_bound(Label);
    while (It != Mission->LabelObjects.upper_bound(Label))
    {
        Vector Position;
        Matrix Orientation;
        if (pub::SpaceObj::GetLocation(It->second, Position, Orientation) == 0)
        {
            // (dz*dz + dx*dx) + dy*dy, kept on the x87 stack.
            double const Dx = static_cast<double>(Position.x) - static_cast<double>(PositionX);
            double const Dy = static_cast<double>(Position.y) - static_cast<double>(PositionY);
            double const Dz = static_cast<double>(Position.z) - static_cast<double>(PositionZ);
            double const CurrentSq = Dz * Dz + Dx * Dx + Dy * Dy;
            bool const bInRange = (bInside != 0) ? (CurrentSq < DistanceSq) : (CurrentSq > DistanceSq);
            if (bInRange)
            {
                TickTimer -= DeltaSeconds;
                if (TickTimer <= 0.0f)
                    return true;
            }
            else
            {
                TickTimer = TickInterval;
            }
        }
        ++It;
    }
    return false;
}

// FUN_06ec0110: counts the objects in range, then the whole equal_range; ticks only when
// the two counts agree (an empty range ticks), else reloads the timer.
bool CndDistVecLbl::MatchesAll(float DeltaSeconds)
{
    typedef std::multimap<unsigned int, unsigned int>::iterator FMemberIterator;
    int InRange = 0;
    FMemberIterator It = Mission->LabelObjects.lower_bound(Label);
    while (It != Mission->LabelObjects.upper_bound(Label))
    {
        Vector Position;
        Matrix Orientation;
        if (pub::SpaceObj::GetLocation(It->second, Position, Orientation) == 0)
        {
            // (dz*dz + dy*dy) + dx*dx, kept on the x87 stack.
            double const Dx = static_cast<double>(Position.x) - static_cast<double>(PositionX);
            double const Dy = static_cast<double>(Position.y) - static_cast<double>(PositionY);
            double const Dz = static_cast<double>(Position.z) - static_cast<double>(PositionZ);
            double const CurrentSq = Dz * Dz + Dy * Dy + Dx * Dx;
            bool const bInRange = (bInside != 0) ? (CurrentSq < DistanceSq) : (CurrentSq > DistanceSq);
            if (bInRange)
                ++InRange;
        }
        ++It;
    }
    int const Total = static_cast<int>(Mission->LabelObjects.count(Label));
    if (InRange != Total)
    {
        TickTimer = TickInterval;
        return false;
    }
    TickTimer -= DeltaSeconds;
    return TickTimer <= 0.0f;
}

// LAB_06ebfff0.
void CndDistVecLbl::OnActivate()
{
    TickTimer = TickInterval;
}

// LAB_06ec02f0.
void CndDistVecLbl::Save(FILE* File)
{
    fprintf(File, "Cnd_DistVecLbl = %f\n", static_cast<double>(TickTimer));
}

// FUN_06ec0310.
void CndDistVecLbl::Load(INI_Reader* Reader)
{
    while (Reader->read_value())
    {
        if (Reader->is_value("Cnd_DistVecLbl"))
            TickTimer = Reader->get_value_float(0);
    }
}

// ---------------------------------------------------------------------------------------
// Cnd_EncLaunched
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850.
CndEncLaunched::CndEncLaunched(FMissionScript* InMission)
    : Cnd(InMission)
    , EncounterId(0)
{
}

// FUN_06ec7900 (shared with Act_ActTrig / Act_DeactTrig / Act_NagOff).
void CndEncLaunched::Parse(INI_Reader& Reader)
{
    EncounterId = CreateID(Reader.get_value_string(0));
}

// LAB_06ec25d0: true when the id is in the launched list.
bool CndEncLaunched::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    if (EventParms == nullptr)
        return false;
    std::list<unsigned int> const* Launched = static_cast<std::list<unsigned int> const*>(EventParms);
    std::list<unsigned int>::const_iterator It = Launched->begin();
    for (; It != Launched->end(); ++It)
    {
        if (*It == EncounterId)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------
// Cnd_HasMsn
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850 (+0x0c not set).
CndHasMsn::CndHasMsn(FMissionScript* InMission)
    : Cnd(InMission)
    , Wanted(0)
{
}

// FUN_06ec2600: case-sensitive; anything else leaves the field.
void CndHasMsn::Parse(INI_Reader& Reader)
{
    TString<0x40> Answer;
    Answer.Data[0] = '\0';
    Answer = Reader.get_value_string(0);
    if (strcmp(Answer.Data, "yes") == 0)
    {
        Wanted = 0;
        return;
    }
    if (strcmp(Answer.Data, "no") == 0)
        Wanted = 1;
}

// FUN_06ec26b0: the player's +0xd8 (FUN_06f487c0) is the live mission controller id.
bool CndHasMsn::Matches(pub::Controller::EMessage /*EventId*/, void const* /*EventParms*/)
{
    FContentPlayerState* Player = GetMissionPlayerState(Mission);
    if (Player == nullptr)
        return false;
    if ((Player->LiveMissionId & GIdValueMask) != 0)
        return Wanted == 0;
    return Wanted == 1;
}

// ---------------------------------------------------------------------------------------
// Cnd_HealthDec
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850.
CndHealthDec::CndHealthDec(FMissionScript* InMission)
    : Cnd(InMission)
    , Health(0.0f)
    , ObjectId(0)
    , AttackerId(0)
{
    InitEmpty(ObjectLabel);
    InitEmpty(AttackerLabel);
}

// FUN_06ec2a90.
void CndHealthDec::Parse(INI_Reader& Reader)
{
    ObjectLabel = Reader.get_value_string(0);
    Health = Reader.get_value_float(1);
    if (!Reader.is_value_empty(2))
        AttackerLabel = Reader.get_value_string(2);
}

// LAB_06ec2ba0. Damage parms (message 0xd): +0x00 object, +0x08 attacker, +0x0c/+0x10 must be
// 0, float +0x20 new relative health. Without parms (activation) only an object label without
// an attacker label is tested, by GetRelativeHealth. Both compares are <= (NaN fails).
bool CndHealthDec::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    if (EventParms == nullptr)
    {
        if (ObjectLabel.Length == 0)
            return false;
        if (AttackerLabel.Length != 0)
            return false;
        FContentPlayerState* Player = GetMissionPlayerState(Mission);
        if (Player == nullptr)
            return false;
        if (Player->ShipObject == nullptr)
            return false;
        ObjectId = Mission->GetLabelObjectId(&ObjectLabel);
        if ((ObjectId & GIdValueMask) == 0)
            return false;
        float Relative = 0.0f;
        pub::SpaceObj::GetRelativeHealth(ObjectId, Relative);
        return Relative < Health || Relative == Health;
    }

    bool bObjectMissing = false;
    if ((ObjectId & GIdValueMask) == 0 && ObjectLabel.Length != 0)
    {
        ObjectId = Mission->GetLabelObjectId(&ObjectLabel);
        if ((ObjectId & GIdValueMask) == 0)
            bObjectMissing = true;
    }
    if ((AttackerId & GIdValueMask) == 0 && AttackerLabel.Length != 0)
    {
        AttackerId = Mission->GetLabelObjectId(&AttackerLabel);
        if ((AttackerId & GIdValueMask) == 0)
            return false;
    }
    if (bObjectMissing)
        return false;
    FSubObjDamageEvent const* Parms = static_cast<FSubObjDamageEvent const*>(EventParms);
    if (ObjectId != Parms->ObjId)
        return false;
    if (Parms->EquipType != 0)
        return false;
    if (Parms->CollisionGroupType != 0)
        return false;
    float const NewHealth = Parms->RelativeHealth;
    if (!(NewHealth < Health || NewHealth == Health))
        return false;
    if (AttackerLabel.Length == 0)
        return true;
    return AttackerId == Parms->InflictorId;
}

// FUN_06ec2b60.
void CndHealthDec::OnActivate()
{
    if (ObjectLabel.Length != 0)
        ObjectId = Mission->GetLabelObjectId(&ObjectLabel);
    if (AttackerLabel.Length != 0)
        AttackerId = Mission->GetLabelObjectId(&AttackerLabel);
}

// ---------------------------------------------------------------------------------------
// Cnd_InSpace
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850.
CndInSpace::CndInSpace(FMissionScript* InMission)
    : Cnd(InMission)
    , bInSpace(1)
{
}

// FUN_06ecdb30 (shared with several Act_* parsers).
void CndInSpace::Parse(INI_Reader& Reader)
{
    bInSpace = Reader.get_value_bool(0) ? 1 : 0;
}

// FUN_06ec2700: "in space" means the player state holds a ship object (+0x10).
bool CndInSpace::Matches(pub::Controller::EMessage /*EventId*/, void const* /*EventParms*/)
{
    FContentPlayerState* Player = GetMissionPlayerState(Mission);
    if (Player == nullptr)
        return false;
    if (bInSpace != 0)
        return Player->ShipObject != nullptr;
    return Player->ShipObject == nullptr;
}

// ---------------------------------------------------------------------------------------
// Cnd_InTradelane
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850.
CndInTradelane::CndInTradelane(FMissionScript* InMission)
    : Cnd(InMission)
    , bInLane(1)
{
    InitEmpty(ObjectLabel);
}

// FUN_06ec2750.
void CndInTradelane::Parse(INI_Reader& Reader)
{
    bInLane = Reader.get_value_bool(0) ? 1 : 0;
    ObjectLabel = Reader.get_value_string(1);
}

// FUN_06ec27c0 (FindNpcByName = FUN_06f15710). HasField120 (FUN_06f57710) is called twice,
// as in retail.
bool CndInTradelane::Matches(pub::Controller::EMessage /*EventId*/, void const* /*EventParms*/)
{
    if (FindContentPlayerState(Mission->PlayerId) == nullptr)
        return false;
    if (Mission->PlayerState->ShipObject == nullptr)
        return false;
    FContentShipBase* Object = nullptr;
    if (_stricmp("Player", ObjectLabel.Data) == 0)
        Object = Mission->PlayerState->ShipObject;
    else
        Object = Mission->FindNpcByName(&ObjectLabel);
    if (Object == nullptr)
        return false;
    if (Object->IsInTradeLane() && bInLane != 0)
        return true;
    if (Object->IsInTradeLane())
        return false;
    return bInLane == 0;
}

// ---------------------------------------------------------------------------------------
// Cnd_InZone
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850.
CndInZone::CndInZone(FMissionScript* InMission)
    : Cnd(InMission)
    , bInZone(1)
    , ZoneFlags(0)
    , ObjectId(0)
{
    InitEmpty(ObjectLabel);
}

// FUN_06ec2850.
void CndInZone::Parse(INI_Reader& Reader)
{
    bInZone = Reader.get_value_bool(0) ? 1 : 0;
    ObjectLabel = Reader.get_value_string(1);
    if (Reader.is_value_empty(2))
        return;
    if (_stricmp("OBJECT_ZONE", Reader.get_value_string(2)) == 0)
    {
        ZoneFlags = Universe::kZonePropObjectZone;
        return;
    }
    if (_stricmp("EXCLUSION_ZONE", Reader.get_value_string(2)) == 0)
    {
        ZoneFlags = Universe::kZonePropExclusionZone;
        return;
    }
    if (_stricmp("NEBULA_ZONE", Reader.get_value_string(2)) == 0)
    {
        ZoneFlags = Universe::kZonePropNebula;
        return;
    }
    if (_stricmp("DUST_ZONE", Reader.get_value_string(2)) == 0)
        ZoneFlags = Universe::kZonePropSpacedust;
}

// LAB_06ec2980.
bool CndInZone::Matches(pub::Controller::EMessage /*EventId*/, void const* /*EventParms*/)
{
    if (FindContentPlayerState(Mission->PlayerId) == nullptr)
        return false;
    if (Mission->PlayerState->ShipObject == nullptr)
        return false;
    ObjectId = Mission->GetLabelShipId(&ObjectLabel);
    if ((ObjectId & GIdValueMask) == 0)
        return false;
    FSystemInfo* const SystemRecord = Mission->PlayerState->GetSystemRecord();
    if (SystemRecord == nullptr)
        return false;
    unsigned int const SystemId = SystemRecord->SystemId;
    Vector Position;
    Matrix Orientation;
    if (pub::SpaceObj::GetLocation(ObjectId, Position, Orientation) != 0)
        return false;
    // At most 8 zones around the point (radius 0).
    unsigned int Zones[8];
    unsigned int ZoneCount;
    pub::System::InZones(SystemId, Position, 0.0f, Zones, 8, ZoneCount);
    bool bMatched = false;
    for (unsigned int Index = 0; Index < ZoneCount; ++Index)
    {
        unsigned long Properties;
        pub::Zone::GetProperties(Zones[Index], Properties);
        if ((ZoneFlags & Properties) != 0)
            bMatched = true;
    }
    if (bInZone != 0)
        return bMatched;
    if (!bMatched)
        return true;
    return ZoneFlags == 0;
}

// FUN_06ec2960 (GetLabelObjectId, unlike Matches' GetLabelShipId).
void CndInZone::OnActivate()
{
    ObjectId = Mission->GetLabelObjectId(&ObjectLabel);
}

// ---------------------------------------------------------------------------------------
// Cnd_JumpgateAct
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850.
CndJumpgateAct::CndJumpgateAct(FMissionScript* InMission)
    : Cnd(InMission)
    , GateId(0)
{
    InitEmpty(Jumpgate);
}

// FUN_06ec2cf0.
void CndJumpgateAct::Parse(INI_Reader& Reader)
{
    Jumpgate = Reader.get_value_string(0);
    GateId = pub::GetNicknameId(Jumpgate.Data);
}

// LAB_06ec2d60: parms = {gate id, ...}.
bool CndJumpgateAct::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    if (EventParms == nullptr)
        return false;
    return *static_cast<unsigned int const*>(EventParms) == GateId;
}

// ---------------------------------------------------------------------------------------
// Cnd_JumpInComplete
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850.
CndJumpInComplete::CndJumpInComplete(FMissionScript* InMission)
    : Cnd(InMission)
    , SystemId(0)
{
}

// FUN_06ec2d80.
void CndJumpInComplete::Parse(INI_Reader& Reader)
{
    if (_stricmp(Reader.get_value_string(0), "ANY") != 0)
        pub::GetSystemID(SystemId, Reader.get_value_string(0));
}

// FUN_06ec2dd0: parms = {system id, ship id}.
bool CndJumpInComplete::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    if (EventParms == nullptr)
        return false;
    unsigned int const* Parms = static_cast<unsigned int const*>(EventParms);
    if (Parms[0] != SystemId && (SystemId & GIdValueMask) != 0)
        return false;
    FContentPlayerState* Player = GetMissionPlayerState(Mission);
    if (Player == nullptr)
        return false;
    return Parms[1] == GetPlayerShipId(Player);
}

// ---------------------------------------------------------------------------------------
// Cnd_LaunchComplete
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850.
CndLaunchComplete::CndLaunchComplete(FMissionScript* InMission)
    : Cnd(InMission)
    , BaseId(0)
    , ShipId(0)
{
    InitEmpty(BaseNickname);
    InitEmpty(ShipLabel);
}

// FUN_06ec2e20.
void CndLaunchComplete::Parse(INI_Reader& Reader)
{
    ShipLabel = Reader.get_value_string(0);
    if (Reader.is_value_empty(1))
        return;
    BaseNickname = Reader.get_value_string(1);
    BaseId = pub::GetNicknameId(BaseNickname.Data);
}

// FUN_06ec2f10: parms = {base id, ship id}; an unset base id matches any base.
bool CndLaunchComplete::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    if (EventParms == nullptr)
        return false;
    if ((ShipId & GIdValueMask) == 0)
        ShipId = Mission->GetLabelShipId(&ShipLabel);
    if ((ShipId & GIdValueMask) == 0)
        return false;
    unsigned int const* Parms = static_cast<unsigned int const*>(EventParms);
    if (Parms[1] != ShipId)
        return false;
    if ((BaseId & GIdValueMask) != 0 && Parms[0] != BaseId)
        return false;
    return true;
}

// FUN_06ec2ef0.
void CndLaunchComplete::OnActivate()
{
    ShipId = Mission->GetLabelShipId(&ShipLabel);
}

// ---------------------------------------------------------------------------------------
// Cnd_LocEnter / Cnd_LocExit
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850.
CndLocEnter::CndLocEnter(FMissionScript* InMission)
    : Cnd(InMission)
    , LocationId(0)
    , BaseId(0)
{
}

// FUN_06ec2f70.
void CndLocEnter::Parse(INI_Reader& Reader)
{
    ParseLocationCondition(Reader, LocationId, BaseId);
}

// FUN_06ec2fc0 (shared with Cnd_LocExit; base record FUN_06f47980 = player +0x1c).
bool CndLocEnter::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    return MatchesPlayerLocation(Mission, LocationId, BaseId, EventParms);
}

// Inlined in FUN_06ebb850.
CndLocExit::CndLocExit(FMissionScript* InMission)
    : Cnd(InMission)
    , LocationId(0)
    , BaseId(0)
{
}

// FUN_06ec2f70.
void CndLocExit::Parse(INI_Reader& Reader)
{
    ParseLocationCondition(Reader, LocationId, BaseId);
}

// FUN_06ec2fc0 (shared with Cnd_LocEnter).
bool CndLocExit::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    return MatchesPlayerLocation(Mission, LocationId, BaseId, EventParms);
}

// ---------------------------------------------------------------------------------------
// Cnd_LootAcquired
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850.
CndLootAcquired::CndLootAcquired(FMissionScript* InMission)
    : Cnd(InMission)
    , LootLabel(GNullId)
    , Count(0)
    , Needed(-1)
{
    InitEmpty(ShipLabel);
}

// FUN_06ec3010.
void CndLootAcquired::Parse(INI_Reader& Reader)
{
    LootLabel = CreateID(Reader.get_value_string(0));
    ShipLabel = Reader.get_value_string(1);
    if (!Reader.is_value_empty(2))
        Needed = Reader.get_value_int(2);
}

// LAB_06ec30c0. Parms: +0 loot object id, +4 id of the ship that picked it up.
bool CndLootAcquired::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    if (EventParms == nullptr)
        return false;
    unsigned int const* Parms = static_cast<unsigned int const*>(EventParms);
    unsigned int const ShipId = Mission->GetLabelShipId(&ShipLabel);
    if (Mission->HasObjectLabel(Parms[0], &LootLabel) && ShipId == Parms[1])
        ++Count;
    // With no explicit count, done once every entry of the label in the mission +0xec
    // multimap is picked up.
    if (ShipId == Parms[1] && Needed == -1 &&
        Count == static_cast<int>(Mission->LabelObjects.count(LootLabel)))
        return true;
    return Needed == Count;
}

// LAB_06ec30b0.
void CndLootAcquired::OnActivate()
{
    Count = 0;
}

// LAB_06ec31e0.
void CndLootAcquired::Save(FILE* File)
{
    fprintf(File, "Cnd_LootAcquired = %d\n", Count);
}

// FUN_06ec3200.
void CndLootAcquired::Load(INI_Reader* Reader)
{
    while (Reader->read_value())
    {
        if (Reader->is_value("Cnd_LootAcquired"))
            Count = Reader->get_value_int(0);
    }
}

// ---------------------------------------------------------------------------------------
// Cnd_MsnResponse
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850 (+0x0c not set).
CndMsnResponse::CndMsnResponse(FMissionScript* InMission)
    : Cnd(InMission)
    , Response(kCndMsnResponseAccept)
{
}

// FUN_06ec3250.
void CndMsnResponse::Parse(INI_Reader& Reader)
{
    if (_stricmp(Reader.get_value_string(0), "accept") == 0)
    {
        Response = kCndMsnResponseAccept;
        return;
    }
    if (_stricmp(Reader.get_value_string(0), "reject") == 0)
        Response = kCndMsnResponseReject;
}

// LAB_06ec32b0: parms byte +0x08 = accepted.
bool CndMsnResponse::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    if (EventParms == nullptr)
        return false;
    unsigned char const bAccepted = static_cast<unsigned char const*>(EventParms)[8];
    if (bAccepted != 0)
        return Response == kCndMsnResponseAccept;
    return Response == kCndMsnResponseReject;
}

// ---------------------------------------------------------------------------------------
// Cnd_NPCSystemEnter / Cnd_NPCSystemExit
// ---------------------------------------------------------------------------------------

// FUN_06ec32e0.
CndNpcSystemEnter::CndNpcSystemEnter(FMissionScript* InMission)
    : Cnd(InMission)
    , SystemId(0)
    , bAnySystem(0)
    , bDelayRunning(0)
    , DelayStart(0.0f)
    , DelayElapsed(0.0f)
    , Delay(0.0f)
    , bLoadedSameSystem(0)
{
}

// LAB_06ec33b0.
void CndNpcSystemEnter::Parse(INI_Reader& Reader)
{
    if (_stricmp(Reader.get_value_string(0), "ANY") == 0)
        bAnySystem = 1;
    else
        pub::GetSystemID(SystemId, Reader.get_value_string(0));

    // Slots 1..0x31; "Player" is skipped here and appended below.
    int Slot = 1;
    while (!Reader.is_value_empty(static_cast<unsigned int>(Slot)) && Slot < 0x32)
    {
        if (_stricmp(Reader.get_value_string(static_cast<unsigned int>(Slot)), "Player") != 0)
        {
            FNpcSystemEntry Entry;
            Entry.Label.Data[0] = '\0';
            Entry.Label = Reader.get_value_string(static_cast<unsigned int>(Slot));
            Entry.SystemId = 0;
            Ships.push_back(Entry);
        }
        ++Slot;
    }

    FNpcSystemEntry PlayerEntry;
    PlayerEntry.Label.Data[0] = '\0';
    PlayerEntry.Label = "Player";
    PlayerEntry.SystemId = 0;
    Ships.push_back(PlayerEntry);

    std::list<FNpcSystemEntry>::iterator It = Ships.begin();
    for (; It != Ships.end(); ++It)
        It->SystemId = 0;
}

// FUN_06ec3640. Events: 0x100f (parms: ship id, system id, +8 object entered), 0x1010 (NPC
// created, parms: object*), 0x20 (tick).
bool CndNpcSystemEnter::Matches(pub::Controller::EMessage EventId, void const* EventParms)
{
    if (EventId == pub::Controller::EMessage::NpcSystemEnter)
    {
        unsigned int const* Parms = static_cast<unsigned int const*>(EventParms);
        bool bResult = true;
        std::list<FNpcSystemEntry>::iterator It = Pending.begin();
        while (It != Pending.end())
        {
            unsigned int const ShipId = Mission->GetLabelShipId(&It->Label);
            if ((ShipId & GIdValueMask) == 0)
            {
                // No ship: forget the label unless loaded NPCs are awaited.
                if (!Awaited.empty())
                {
                    bResult = false;
                    ++It;
                    continue;
                }
                It = Pending.erase(It);
                continue;
            }
            unsigned int const EnteredSystem = Parms[1];
            if (ShipId == Parms[0])
            {
                if ((EnteredSystem & GIdValueMask) != 0 &&
                    (EnteredSystem == SystemId || bAnySystem != 0))
                    It->SystemId = EnteredSystem;
                else
                    bResult = false;
            }
            else if (It->SystemId != EnteredSystem)
            {
                bResult = false;
            }
            ++It;
        }
        if (!bResult)
            return false;
        // The player's own ship entering through a gate or hole (object type bit 0x800 or
        // 0x40) starts a 10 s delay instead.
        if (FindContentPlayerState(Mission->PlayerId) == nullptr)
            return true;
        FContentPlayerShip* const Ship = Mission->PlayerState->ShipObject;
        if (Ship == nullptr)
            return true;
        if (Ship->SpaceObjId != Parms[0])
            return true;
        if ((Parms[2] & GIdValueMask) == 0)
            return true;
        unsigned int Type;
        pub::SpaceObj::GetType(Parms[2], Type);
        if ((Type & OBJ_JUMP_HOLE) == 0 && (Type & OBJ_JUMP_GATE) == 0)
            return true;
        bDelayRunning = 1;
        DelayStart = Mission->ElapsedTime;
        DelayElapsed = 0.0f;
        Delay = 10.0f;
        return false;
    }
    if (EventId == pub::Controller::EMessage::NpcShipCreated)
    {
        if (Awaited.empty())
            return false;
        if (GetPlayerSystemId(Mission->PlayerState) != SystemId && bAnySystem == 0)
            return false;
        FContentNpcShip const* const Npc =
            *static_cast<FContentNpcShip* const*>(EventParms);
        std::list<FNpcSystemEntry>::iterator It = Awaited.begin();
        for (; It != Awaited.end(); ++It)
        {
            if (strcmp(It->Label.Data, Npc->Nickname.Data) == 0)
            {
                Awaited.erase(It);
                break;
            }
        }
        if (Awaited.empty())
        {
            bDelayRunning = 1;
            DelayStart = Mission->ElapsedTime;
            DelayElapsed = 0.0f;
            Delay = 2.0f;
            return false;
        }
        // Only "Player" left (case-sensitive): drop it and start.
        if (Awaited.size() == 1 && memcmp(Awaited.front().Label.Data, "Player", 7) == 0)
        {
            Awaited.clear();
            bDelayRunning = 1;
            DelayStart = Mission->ElapsedTime;
            DelayElapsed = 0.0f;
            Delay = 2.0f;
        }
        return false;
    }
    if (EventId == pub::Controller::EMessage::Heartbeat && bDelayRunning != 0)
    {
        // Retail compares the unrounded difference, not the stored float.
        double const Elapsed =
            static_cast<double>(Mission->ElapsedTime) - static_cast<double>(DelayStart);
        DelayElapsed = static_cast<float>(Elapsed);
        return !(Elapsed < Delay);
    }
    return false;
}

// LAB_06ec3530.
void CndNpcSystemEnter::OnActivate()
{
    Pending = Ships;
}

// FUN_06ec3920: no trailing newline in retail.
void CndNpcSystemEnter::Save(FILE* File)
{
    fprintf(File, "Cnd_NPCSystemEnter = ");
    std::list<FNpcSystemEntry>::iterator It = Pending.begin();
    for (; It != Pending.end(); ++It)
        fprintf(File, "%s,%u, ", It->Label.Data, It->SystemId);
}

// LAB_06ec3970: at most 0x32 slots.
void CndNpcSystemEnter::Load(INI_Reader* Reader)
{
    unsigned int FirstSystem = 0;
    bLoadedSameSystem = 1;
    while (Reader->read_value())
    {
        if (!Reader->is_value("Cnd_NPCSystemEnter"))
            continue;
        Pending.clear();
        int Slot = 0;
        if (!Reader->is_value_empty(0))
        {
            while (Slot < 0x32)
            {
                FNpcSystemEntry Entry;
                Entry.Label.Length = 0;
                Entry.Label.Data[0] = '\0';
                Entry.Label = Reader->get_value_string(static_cast<unsigned int>(Slot));
                Entry.SystemId =
                    static_cast<unsigned int>(Reader->get_value_int(static_cast<unsigned int>(Slot + 1)));
                Pending.push_back(Entry);
                if ((FirstSystem & GIdValueMask) == 0)
                    FirstSystem = static_cast<unsigned int>(
                        Reader->get_value_int(static_cast<unsigned int>(Slot + 1)));
                if (FirstSystem !=
                    static_cast<unsigned int>(Reader->get_value_int(static_cast<unsigned int>(Slot + 1))))
                    bLoadedSameSystem = 0;
                Slot += 2;
                if (Reader->is_value_empty(static_cast<unsigned int>(Slot)))
                    break;
            }
        }
        Awaited = Pending;
    }
}

// Inlined in FUN_06ebb850 (list ctors FUN_06ecffc0).
CndNpcSystemExit::CndNpcSystemExit(FMissionScript* InMission)
    : Cnd(InMission)
    , SystemId(0)
    , bAnySystem(0)
{
}

// FUN_06ec3cf0: like Cnd_NPCSystemEnter's but every label (including "Player") is kept and
// nothing is appended.
void CndNpcSystemExit::Parse(INI_Reader& Reader)
{
    if (_stricmp(Reader.get_value_string(0), "ANY") == 0)
        bAnySystem = 1;
    else
        pub::GetSystemID(SystemId, Reader.get_value_string(0));

    int Slot = 1;
    while (!Reader.is_value_empty(static_cast<unsigned int>(Slot)) && Slot < 0x32)
    {
        FNpcSystemEntry Entry;
        Entry.Label.Data[0] = '\0';
        Entry.Label = Reader.get_value_string(static_cast<unsigned int>(Slot));
        Entry.SystemId = 0;
        Ships.push_back(Entry);
        ++Slot;
    }

    std::list<FNpcSystemEntry>::iterator It = Ships.begin();
    for (; It != Ships.end(); ++It)
        It->SystemId = 0;
}

// LAB_06ec3e20. Parms: +0 ship id, +4 system id.
bool CndNpcSystemExit::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    if (EventParms == nullptr)
        return false;
    unsigned int const* Parms = static_cast<unsigned int const*>(EventParms);
    bool bResult = true;
    std::list<FNpcSystemEntry>::iterator It = Pending.begin();
    while (It != Pending.end())
    {
        unsigned int const ShipId = Mission->GetLabelShipId(&It->Label);
        if ((ShipId & GIdValueMask) == 0)
        {
            // No ship: forget the label unless loaded NPCs are awaited.
            if (!Awaited.empty())
            {
                bResult = false;
                ++It;
                continue;
            }
            It = Pending.erase(It);
            continue;
        }
        // Strike the first awaited entry with the same label.
        std::list<FNpcSystemEntry>::iterator AwaitedIt = Awaited.begin();
        for (; AwaitedIt != Awaited.end(); ++AwaitedIt)
        {
            if (strcmp(It->Label.Data, AwaitedIt->Label.Data) == 0)
            {
                Awaited.erase(AwaitedIt);
                break;
            }
        }
        unsigned int const LeftSystem = Parms[1];
        if (ShipId == Parms[0])
        {
            if ((LeftSystem & GIdValueMask) != 0 && (LeftSystem == SystemId || bAnySystem != 0))
                It->SystemId = LeftSystem;
        }
        else if (It->SystemId != LeftSystem)
        {
            bResult = false;
        }
        ++It;
    }
    return bResult;
}

// LAB_06ec3530 (Cnd_NPCSystemEnter's body).
void CndNpcSystemExit::OnActivate()
{
    Pending = Ships;
}

// FUN_06ec3f80.
void CndNpcSystemExit::Save(FILE* File)
{
    fprintf(File, "Cnd_NPCSystemExit = ");
    std::list<FNpcSystemEntry>::iterator It = Pending.begin();
    for (; It != Pending.end(); ++It)
        fprintf(File, "%s,%u, ", It->Label.Data, It->SystemId);
}

// LAB_06ec3fd0: unlike Cnd_NPCSystemEnter's, it stops at an empty label and reads the system
// ids as floats.
void CndNpcSystemExit::Load(INI_Reader* Reader)
{
    while (Reader->read_value())
    {
        if (!Reader->is_value("Cnd_NPCSystemExit"))
            continue;
        Pending.clear();
        TString<0x40> Next;
        Next.Length = 0;
        Next.Data[0] = '\0';
        Next = Reader->get_value_string(0);
        int Slot = 0;
        while (Next.Length != 0 && Slot < 0x32)
        {
            FNpcSystemEntry Entry;
            Entry.Label.Length = 0;
            Entry.Label.Data[0] = '\0';
            Entry.Label = Reader->get_value_string(static_cast<unsigned int>(Slot));
            // ftol: 64-bit truncation, low dword kept.
            Entry.SystemId = static_cast<unsigned int>(static_cast<long long>(
                Reader->get_value_float(static_cast<unsigned int>(Slot + 1))));
            Pending.push_back(Entry);
            Slot += 2;
            Next = Reader->get_value_string(static_cast<unsigned int>(Slot));
        }
        Awaited = Pending;
    }
}

// ---------------------------------------------------------------------------------------
// Cnd_PlayerLaunch
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850.
CndPlayerLaunch::CndPlayerLaunch(FMissionScript* InMission)
    : Cnd(InMission)
{
}

// FUN_06ebf6f0.
void CndPlayerLaunch::Parse(INI_Reader& /*Reader*/)
{
}

// LAB_06ec1f30: *parms == mission player id (+0x0c).
bool CndPlayerLaunch::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    if (EventParms == nullptr)
        return false;
    return *static_cast<unsigned int const*>(EventParms) == Mission->PlayerId;
}

// ---------------------------------------------------------------------------------------
// Cnd_PlayerManeuver
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850.
CndPlayerManeuver::CndPlayerManeuver(FMissionScript* InMission)
    : Cnd(InMission)
    , BehaviorId(0)
    , bInverse(0)
    , TargetId(0)
{
    InitEmpty(TargetLabel);
}

// FUN_06ec1f50.
void CndPlayerManeuver::Parse(INI_Reader& Reader)
{
    BehaviorId = BehaviorTypes::get_behavior_id(Reader.get_value_string(0));
    if (Reader.is_value_empty(1))
        return;
    TargetLabel = Reader.get_value_string(1);
    if (strstr(TargetLabel.Data, "inverse") != nullptr)
        bInverse = 1;
}

// LAB_06ec2010: the player ship runs behaviour +0x0c against the wanted target; "inverse"
// negates the result, including every early out.
bool CndPlayerManeuver::Matches(pub::Controller::EMessage /*EventId*/, void const* /*EventParms*/)
{
    bool bResult = false;
    if (FindContentPlayerState(Mission->PlayerId) != nullptr)
    {
        unsigned int const ShipId = GetPlayerShipId(Mission->PlayerState);
        int const Behavior = pub::AI::get_behavior_id(ShipId);
        unsigned int Target = 0;
        pub::SpaceObj::GetTarget(ShipId, Target);
        if (Behavior == BehaviorId)
        {
            if (Behavior == BEHAVIOR_FORMATION)
            {
                // Formation behaviour: the targeted NPC and the player ship must share the
                // formation (+0x154).
                FContentPlayerShip* const Ship = Mission->PlayerState->ShipObject;
                FContentNpcShip* const Npc = Mission->FindNpcById(&Target);
                if (Npc != nullptr && Ship != nullptr)
                    bResult = Npc->FormationId == Ship->FormationId;
            }
            else if (TargetId == 0 || Target == TargetId)
            {
                bResult = true;
            }
        }
    }
    if (bInverse != 0)
        return !bResult;
    return bResult;
}

// FUN_06ec1ff0.
void CndPlayerManeuver::OnActivate()
{
    if (TargetLabel.Length != 0)
        TargetId = Mission->GetLabelShipId(&TargetLabel);
}

// ---------------------------------------------------------------------------------------
// Cnd_PopUpDialog
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850.
CndPopUpDialog::CndPopUpDialog(FMissionScript* InMission)
    : Cnd(InMission)
    , ButtonMask(0)
{
}

// FUN_06ec20d0.
void CndPopUpDialog::Parse(INI_Reader& Reader)
{
    TString<0x40> Button;
    Button.Data[0] = '\0';
    Button = Reader.get_value_string(0);
    if (_stricmp(Button.Data, "YES") == 0)
    {
        ButtonMask = kPopUpButtonYes;
        return;
    }
    if (_stricmp(Button.Data, "NO") == 0)
    {
        ButtonMask = kPopUpButtonNo;
        return;
    }
    if (_stricmp(Button.Data, "LATER") == 0)
    {
        ButtonMask = kPopUpButtonLater;
        return;
    }
    if (_stricmp(Button.Data, "CLOSE") == 0)
        ButtonMask = kPopUpButtonClose;
}

// LAB_06ec21d0: parms+0x04 = pressed button bits.
bool CndPopUpDialog::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    if (EventParms == nullptr)
        return false;
    return (static_cast<unsigned int const*>(EventParms)[1] & ButtonMask) != 0;
}

// ---------------------------------------------------------------------------------------
// Cnd_ProjHit
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850.
CndProjHit::CndProjHit(FMissionScript* InMission)
    : Cnd(InMission)
    , Needed(1)
    , TargetId(0)
    , ShooterId(0)
    , Count(0)
{
    InitEmpty(TargetLabel);
    InitEmpty(ShooterLabel);
}

// FUN_06ec21f0.
void CndProjHit::Parse(INI_Reader& Reader)
{
    TargetLabel = Reader.get_value_string(0);
    Needed = Reader.get_value_int(1);
    if (!Reader.is_value_empty(2))
        ShooterLabel = Reader.get_value_string(2);
}

// FUN_06ec22c0: parms = {target id, ?, shooter id}; an unset shooter matches any.
bool CndProjHit::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    if (EventParms == nullptr)
        return false;
    if ((TargetId & GIdValueMask) == 0)
        TargetId = Mission->GetLabelObjectId(&TargetLabel);
    if (ShooterLabel.Length != 0 && (ShooterId & GIdValueMask) == 0)
        ShooterId = Mission->GetLabelObjectId(&ShooterLabel);
    if ((TargetId & GIdValueMask) == 0)
        return false;
    unsigned int const* Parms = static_cast<unsigned int const*>(EventParms);
    if (Parms[0] != TargetId)
        return false;
    if ((ShooterId & GIdValueMask) != 0 && Parms[2] != ShooterId)
        return false;
    ++Count;
    return Count >= Needed;
}

// FUN_06ec2350.
void CndProjHit::OnActivate()
{
    TargetId = Mission->GetLabelObjectId(&TargetLabel);
    if (ShooterLabel.Length != 0)
        ShooterId = Mission->GetLabelObjectId(&ShooterLabel);
    Count = 0;
}

// LAB_06ec2390.
void CndProjHit::Save(FILE* File)
{
    fprintf(File, "Cnd_ProjHit = %d\n", Count);
}

// FUN_06ec23b0.
void CndProjHit::Load(INI_Reader* Reader)
{
    while (Reader->read_value())
    {
        if (Reader->is_value("Cnd_ProjHit"))
            Count = Reader->get_value_int(0);
    }
}

// ---------------------------------------------------------------------------------------
// Cnd_ProjHitShipToLbl
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850.
CndProjHitShipToLbl::CndProjHitShipToLbl(FMissionScript* InMission)
    : Cnd(InMission)
    , TargetLabel(0)
    , Needed(1)
    , ShooterId(0)
    , Count(0)
{
    InitEmpty(ShooterLabel);
}

// FUN_06ec2400.
void CndProjHitShipToLbl::Parse(INI_Reader& Reader)
{
    TargetLabel = CreateID(Reader.get_value_string(0));
    Needed = Reader.get_value_int(1);
    if (!Reader.is_value_empty(2))
        ShooterLabel = Reader.get_value_string(2);
}

// FUN_06ec24a0. Parms: +0 hit object id, +8 shooter id.
bool CndProjHitShipToLbl::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    if (EventParms == nullptr)
        return false;
    unsigned int const* Parms = static_cast<unsigned int const*>(EventParms);
    if (ShooterLabel.Length != 0)
        ShooterId = Mission->GetLabelObjectId(&ShooterLabel);
    if (!Mission->HasObjectLabel(Parms[0], &TargetLabel))
        return false;
    if (ShooterLabel.Length != 0 && (ShooterId & GIdValueMask) == 0)
        ShooterId = Mission->GetLabelObjectId(&ShooterLabel);
    if ((ShooterId & GIdValueMask) != 0 && Parms[2] != ShooterId)
        return false;
    ++Count;
    return Count >= Needed;
}

// FUN_06ec2530.
void CndProjHitShipToLbl::OnActivate()
{
    if (ShooterLabel.Length != 0)
        ShooterId = Mission->GetLabelObjectId(&ShooterLabel);
    Count = 0;
}

// LAB_06ec2560.
void CndProjHitShipToLbl::Save(FILE* File)
{
    fprintf(File, "Cnd_ProjHitShipToLbl = %d\n", Count);
}

// FUN_06ec2580.
void CndProjHitShipToLbl::Load(INI_Reader* Reader)
{
    while (Reader->read_value())
    {
        if (Reader->is_value("Cnd_ProjHitShipToLbl"))
            Count = Reader->get_value_int(0);
    }
}

// ---------------------------------------------------------------------------------------
// Cnd_RTCDone
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850 (no "" assignment).
CndRtcDone::CndRtcDone(FMissionScript* InMission)
    : Cnd(InMission)
{
    InitEmpty(RtcFile);
}

// FUN_06ec1be0.
void CndRtcDone::Parse(INI_Reader& Reader)
{
    RtcFile = Reader.get_value_string(0);
}

// FUN_06ec1c50: the mission's RtcScripts (script at node +0x90, its sequence id
// +0x40).
bool CndRtcDone::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    if (EventParms == nullptr)
        return false;
    unsigned int const* Parms = static_cast<unsigned int const*>(EventParms);
    std::list<FMissionRtc>::iterator It = Mission->RtcScripts.begin();
    while (It != Mission->RtcScripts.end())
    {
        FCharacterEncounter* Script = It->Encounter;
        if (Script != nullptr && Parms[1] == Script->SequenceId &&
            _stricmp(It->Path, RtcFile.Data) == 0)
        {
            return true;
        }
        ++It;
    }
    return false;
}

// ---------------------------------------------------------------------------------------
// Cnd_RumorHeard
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850.
CndRumorHeard::CndRumorHeard(FMissionScript* InMission)
    : Cnd(InMission)
    , LocationId(0)
    , BaseId(0)
{
    InitEmpty(Rumor);
}

// FUN_06ebfa20 (Cnd_CharSelect's parser).
void CndRumorHeard::Parse(INI_Reader& Reader)
{
    ParseCharacterCondition(Reader, Rumor, BaseId, LocationId);
}

// LAB_06ec1cc0: parms = {player id, ?, char name[...] at +0x08}; the location must be the
// mission's current one (+0x308, written by FUN_06eb8f50 case 0x29).
bool CndRumorHeard::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    if (EventParms == nullptr)
        return false;
    unsigned int const* Parms = static_cast<unsigned int const*>(EventParms);
    if (Mission->PlayerId != Parms[0])
        return false;
    if (LocationId != Mission->CurrentLocationId)
        return false;
    char const* HeardName = reinterpret_cast<char const*>(Parms + 2);
    return _stricmp(Rumor.Data, HeardName) == 0;
}

// ---------------------------------------------------------------------------------------
// Cnd_SpaceEnter / Cnd_SpaceExit
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850.
CndSpaceEnter::CndSpaceEnter(FMissionScript* InMission)
    : Cnd(InMission)
    , SystemId(0)
{
}

// FUN_06ec1d00.
void CndSpaceEnter::Parse(INI_Reader& Reader)
{
    ParseSpaceCondition(Reader, SystemId);
}

// LAB_06ebf750.
bool CndSpaceEnter::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    return MatchesIdOrAny(SystemId, EventParms);
}

// Inlined in FUN_06ebb850.
CndSpaceExit::CndSpaceExit(FMissionScript* InMission)
    : Cnd(InMission)
    , SystemId(0)
{
}

// FUN_06ec1d00.
void CndSpaceExit::Parse(INI_Reader& Reader)
{
    ParseSpaceCondition(Reader, SystemId);
}

// LAB_06ebf750.
bool CndSpaceExit::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    return MatchesIdOrAny(SystemId, EventParms);
}

// ---------------------------------------------------------------------------------------
// Cnd_SystemEnter / Cnd_SystemExit
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850.
CndSystemEnter::CndSystemEnter(FMissionScript* InMission)
    : Cnd(InMission)
    , FromSystemId(0)
    , SystemId(0)
{
}

// FUN_06ec1db0.
void CndSystemEnter::Parse(INI_Reader& Reader)
{
    if (_stricmp(Reader.get_value_string(0), "ANY") != 0)
        pub::GetSystemID(SystemId, Reader.get_value_string(0));
    if (Reader.is_value_empty(1))
        return;
    if (_stricmp(Reader.get_value_string(1), "ANY") == 0)
        return;
    pub::GetSystemID(FromSystemId, Reader.get_value_string(1));
}

// FUN_06ec1e30: parms = {system id, from system id}, each unset or equal.
bool CndSystemEnter::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    // Activation (null parms): FUN_06f478b0 on the cached player state directly, without the
    // FUN_06f49b00 lookup gate (as retail).
    if (EventParms == nullptr)
        return GetPlayerSystemId(Mission->PlayerState) == SystemId;
    unsigned int const* Parms = static_cast<unsigned int const*>(EventParms);
    if (Parms[0] != SystemId && (SystemId & GIdValueMask) != 0)
        return false;
    if (Parms[1] != FromSystemId && (FromSystemId & GIdValueMask) != 0)
        return false;
    return true;
}

// Inlined in FUN_06ebb850.
CndSystemExit::CndSystemExit(FMissionScript* InMission)
    : Cnd(InMission)
    , SystemId(0)
    , ToSystemId(0)
{
}

// FUN_06ec1e80.
void CndSystemExit::Parse(INI_Reader& Reader)
{
    if (_stricmp(Reader.get_value_string(0), "ANY") != 0)
        pub::GetSystemID(SystemId, Reader.get_value_string(0));
    if (Reader.is_value_empty(1))
        return;
    if (_stricmp(Reader.get_value_string(1), "ANY") == 0)
        return;
    pub::GetSystemID(ToSystemId, Reader.get_value_string(1));
}

// FUN_06ec1f00: parms = {system id, destination system id}.
bool CndSystemExit::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    if (EventParms == nullptr)
        return false;
    unsigned int const* Parms = static_cast<unsigned int const*>(EventParms);
    if (Parms[0] != SystemId && (SystemId & GIdValueMask) != 0)
        return false;
    if ((ToSystemId & GIdValueMask) == 0)
        return true;
    return Parms[1] == ToSystemId;
}

// ---------------------------------------------------------------------------------------
// Cnd_TetherBroke
// ---------------------------------------------------------------------------------------

// Inlined in FUN_06ebb850.
CndTetherBroke::CndTetherBroke(FMissionScript* InMission)
    : Cnd(InMission)
    , ShipAId(0)
    , ShipBId(0)
    , bTethered(0)
    , DistanceSq(0.0f)
    , InnerDistanceSq(0.0f)
    , LastDistanceSq(0.0f)
    , BreakDuration(0.0f)
    , ElapsedBeyond(0.0f)
    , LastCheckTime(0.0f)
{
    InitEmpty(ShipA);
    InitEmpty(ShipB);
}

// FUN_06ec11c0.
void CndTetherBroke::Parse(INI_Reader& Reader)
{
    ShipA = Reader.get_value_string(0);
    ShipB = Reader.get_value_string(1);
    DistanceSq = SquareIfGreater(Reader.get_value_float(2));
    BreakDuration = Reader.get_value_float(3);
    if (!Reader.is_value_empty(4))
        InnerDistanceSq = SquareUnlessLess(Reader.get_value_float(4));
}

// LAB_06ec1330. Parms (optional): +0 ship A, +4 ship B, +0xc byte "tether released".
bool CndTetherBroke::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    if (FindContentPlayerState(Mission->PlayerId) == nullptr)
        return false;
    if (Mission->PlayerState->ShipObject == nullptr)
        return false;
    if (Mission->PlayerState->GetSystemRecord() == nullptr)
        return false;
    ShipAId = Mission->GetLabelShipId(&ShipA);
    ShipBId = Mission->GetLabelShipId(&ShipB);
    if ((ShipAId & GIdValueMask) == 0 || (ShipBId & GIdValueMask) == 0)
        return false;
    float const Distance = static_cast<float>(GetObjectDistanceSq(&ShipAId, &ShipBId));
    if (EventParms != nullptr)
    {
        unsigned int const* Parms = static_cast<unsigned int const*>(EventParms);
        if (Parms[0] == ShipAId && Parms[1] == ShipBId)
            bTethered = (static_cast<unsigned char const*>(EventParms)[0xc] == 0) ? 1 : 0;
    }
    if (bTethered == 0)
    {
        ElapsedBeyond = 0.0f;
        return false;
    }
    // Only while the limit is <= the distance (unordered fails).
    if (!(DistanceSq <= Distance))
        return false;
    double const Now = Mission->ElapsedTime;
    // No step on the first check (+0x94 == 0).
    float TimeStep = 0.0f;
    if (!(LastCheckTime == 0.0f))
        TimeStep = static_cast<float>(Now - LastCheckTime);
    LastCheckTime = static_cast<float>(Now);
    double DistanceStep = 0.0;
    if (!(LastDistanceSq == 0.0f))
        DistanceStep = static_cast<double>(Distance) - LastDistanceSq;
    LastDistanceSq = Distance;
    // Within the inner radius only a non-shrinking distance counts; unordered compares fail.
    if (!(InnerDistanceSq <= Distance))
    {
        if (!(InnerDistanceSq > Distance))
            return false;
        if (!(DistanceStep >= 0.0))
            return false;
    }
    // Retail compares the unrounded sum.
    double const Accumulated = static_cast<double>(TimeStep) + ElapsedBeyond;
    ElapsedBeyond = static_cast<float>(Accumulated);
    return !(Accumulated < BreakDuration);
}

// FUN_06f242e0.
double __cdecl GetObjectDistanceSq(unsigned int const* IdA, unsigned int const* IdB)
{
    Vector PositionA;
    Vector PositionB;
    Matrix Orientation;
    if (pub::SpaceObj::GetLocation(*IdA, PositionA, Orientation) != 0)
        return -1.0f; // DAT_06fb5e84
    if (pub::SpaceObj::GetLocation(*IdB, PositionB, Orientation) != 0)
        return -1.0f;
    // (dz*dz + dy*dy) + dx*dx on the x87 stack.
    double const Dx = static_cast<double>(PositionA.x) - PositionB.x;
    double const Dy = static_cast<double>(PositionA.y) - PositionB.y;
    double const Dz = static_cast<double>(PositionA.z) - PositionB.z;
    return Dz * Dz + Dy * Dy + Dx * Dx;
}

// LAB_06ec12f0.
void CndTetherBroke::OnActivate()
{
    ElapsedBeyond = 0.0f;
    LastDistanceSq = 0.0f;
    LastCheckTime = 0.0f;
    ShipAId = Mission->GetLabelShipId(&ShipA);
    ShipBId = Mission->GetLabelShipId(&ShipB);
}

// LAB_06ec14d0.
void CndTetherBroke::Save(FILE* File)
{
    fprintf(File, "Cnd_TetherBroke = %f, %f\n", static_cast<double>(ElapsedBeyond),
            static_cast<double>(LastDistanceSq));
}

// FUN_06ec1500.
void CndTetherBroke::Load(INI_Reader* Reader)
{
    while (Reader->read_value())
    {
        if (Reader->is_value("Cnd_TetherBroke"))
        {
            ElapsedBeyond = Reader->get_value_float(0);
            LastDistanceSq = Reader->get_value_float(1);
        }
    }
}

// ---------------------------------------------------------------------------------------
// Cnd_Timer
// ---------------------------------------------------------------------------------------

// FUN_06ec1560. Retail leaves +0x0c/+0x10 unset (Parse and OnActivate write them before the
// first Matches).
CndTimer::CndTimer(FMissionScript* InMission)
    : Cnd(InMission)
    , Elapsed(0.0f)
    , Duration(0.0f)
    , bRunOnBase(1)
{
}

// LAB_06ec1590.
void CndTimer::Parse(INI_Reader& Reader)
{
    Duration = Reader.get_value_float(0);
    if (!Reader.is_value_empty(1))
    {
        float const Upper = Reader.get_value_float(1);
        float const Lower = Duration;
        float const RandUnit = static_cast<float>(rand()) * GRandUnitScale;
        Duration = (Upper - Lower) * RandUnit + Lower;
    }
    if (Reader.is_value_empty(2))
        return;

    if (_stricmp(Reader.get_value_string(2), "RUN_ON_BASE") == 0)
    {
        bRunOnBase = 1;
        return;
    }
    if (_stricmp(Reader.get_value_string(2), "RUN_IN_SPACE_ONLY") == 0)
        bRunOnBase = 0;
}

// FUN_06ec1660. EventParms is the float* delta the tick message (0x20) carries; docked time
// counts only with RUN_ON_BASE.
bool CndTimer::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    FContentPlayerState* Player = GetMissionPlayerState(Mission);
    if (Player == nullptr)
        return false;
    if (Player->ShipObject == nullptr && bRunOnBase == 0)
        return false;
    float const* Delta = static_cast<float const*>(EventParms);
    if (Delta != nullptr)
        Elapsed = Elapsed + *Delta;
    return !(Elapsed < Duration);
}

// LAB_06ec1650.
void CndTimer::OnActivate()
{
    Elapsed = 0.0f;
}

// LAB_06ec16c0.
void CndTimer::Save(FILE* File)
{
    fprintf(File, "Cnd_Timer = %f\n", static_cast<double>(Elapsed));
}

// FUN_06ec16e0.
void CndTimer::Load(INI_Reader* Reader)
{
    while (Reader->read_value())
    {
        if (Reader->is_value("Cnd_Timer"))
            Elapsed = Reader->get_value_float(0);
    }
}

// ---------------------------------------------------------------------------------------
// Cnd_TLEntered / Cnd_TLExited
// ---------------------------------------------------------------------------------------

// FUN_06ec1730.
CndTlEntered::CndTlEntered(FMissionScript* InMission)
    : Cnd(InMission)
    , ShipId(0)
    , RingAId(0)
    , RingBId(0)
{
    InitEmpty(ShipLabel);
    InitEmpty(RingA);
    InitEmpty(RingB);
}

// FUN_06ec1830.
void CndTlEntered::Parse(INI_Reader& Reader)
{
    ShipLabel = Reader.get_value_string(0);
    RingA = Reader.get_value_string(1);
    RingB = Reader.get_value_string(2);
    RingAId = pub::GetNicknameId(RingA.Data);
    RingBId = pub::GetNicknameId(RingB.Data);
}

// FUN_06ec1900: parms = {ship id, ring A id, ring B id}; the ship is re-resolved every call.
bool CndTlEntered::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    if (EventParms == nullptr)
        return false;
    ShipId = Mission->GetLabelShipId(&ShipLabel);
    if ((ShipId & GIdValueMask) == 0)
        return false;
    unsigned int const* Parms = static_cast<unsigned int const*>(EventParms);
    if (Parms[0] != ShipId)
        return false;
    if (Parms[1] != RingAId)
        return false;
    return Parms[2] == RingBId;
}

// FUN_06ec1b20.
void CndTlEntered::OnActivate()
{
    ShipId = Mission->GetLabelShipId(&ShipLabel);
}

// FUN_06ec1950.
CndTlExited::CndTlExited(FMissionScript* InMission)
    : Cnd(InMission)
    , ShipId(0)
    , RingBId(0)
    , RingAId(0)
{
    InitEmpty(ShipLabel);
    InitEmpty(RingA);
    InitEmpty(RingB);
}

// LAB_06ec1a20.
void CndTlExited::Parse(INI_Reader& Reader)
{
    ShipLabel = Reader.get_value_string(0);
    RingA = Reader.get_value_string(1);
    if (_stricmp(RingA.Data, "ANY") != 0)
        RingAId = pub::GetNicknameId(RingA.Data);
    if (Reader.is_value_empty(2))
        return;
    RingB = Reader.get_value_string(2);
    RingBId = pub::GetNicknameId(RingB.Data);
}

// FUN_06ec1b40: parms+0x04 is tested against RingBId (+0xac), parms+0x08 against RingAId
// (+0xb0); an unset ring id matches any ring.
bool CndTlExited::Matches(pub::Controller::EMessage /*EventId*/, void const* EventParms)
{
    if (EventParms == nullptr)
        return false;
    ShipId = Mission->GetLabelShipId(&ShipLabel);
    if ((ShipId & GIdValueMask) == 0)
        return false;
    unsigned int const* Parms = static_cast<unsigned int const*>(EventParms);
    if (Parms[0] != ShipId)
        return false;
    if ((RingAId & GIdValueMask) != 0 && Parms[2] != RingAId)
        return false;
    if ((RingBId & GIdValueMask) != 0 && Parms[1] != RingBId)
        return false;
    return true;
}

// FUN_06ec1b20 (Cnd_TLEntered's).
void CndTlExited::OnActivate()
{
    ShipId = Mission->GetLabelShipId(&ShipLabel);
}

// ---------------------------------------------------------------------------------------
// Cnd_True
// ---------------------------------------------------------------------------------------

// FUN_06ec1bb0.
CndTrue::CndTrue(FMissionScript* InMission)
    : Cnd(InMission)
{
}

// FUN_06ebf6f0.
void CndTrue::Parse(INI_Reader& /*Reader*/)
{
}

// LAB_06ec1bd0.
bool CndTrue::Matches(pub::Controller::EMessage /*EventId*/, void const* /*EventParms*/)
{
    return true;
}

// ---------------------------------------------------------------------------------------
// Cnd_WatchTrigger
// ---------------------------------------------------------------------------------------

// FUN_06ec42a0.
CndWatchTrigger::CndWatchTrigger(FMissionScript* InMission)
    : Cnd(InMission)
    , WatchedTriggerId(0)
    , WantedState(0)
{
}

// LAB_06ec4310.
void CndWatchTrigger::Parse(INI_Reader& Reader)
{
    WatchedTriggerId = CreateID(Reader.get_value_string(0));
    WantedState = CreateID(Reader.get_value_string(1));
}

// LAB_06ec4350 (the trigger map mission+0x1b0). ON matches ACTIVE, OFF matches INACTIVE or
// COMPLETE.
bool CndWatchTrigger::Matches(pub::Controller::EMessage /*EventId*/, void const* /*EventParms*/)
{
    FTriggerMap::iterator Found = Mission->TriggerMap.find(WatchedTriggerId);
    if (Found == Mission->TriggerMap.end())
        return false;
    int const State = Found->second.State;
    if (WantedState == GIdStateActive && State == G_TriggerStateActive)
        return true;
    if (WantedState == GIdStateInactive && State == G_TriggerStateInactive)
        return true;
    if (WantedState == GIdStateComplete && State == G_TriggerStateComplete)
        return true;
    if (WantedState == GIdStateOn && State == G_TriggerStateActive)
        return true;
    if (WantedState == GIdStateOff)
        return State == G_TriggerStateInactive || State == G_TriggerStateComplete;
    return false;
}

// ---------------------------------------------------------------------------------------
// Cnd_WatchVibe
// ---------------------------------------------------------------------------------------

// FUN_06ec43e0.
CndWatchVibe::CndWatchVibe(FMissionScript* InMission)
    : Cnd(InMission)
    , Compare(kCndVibeLess)
    , ObjectAId(0)
    , ObjectBId(0)
    , Value(0.0f)
{
    InitEmpty(ObjectA);
    InitEmpty(ObjectB);
}

// LAB_06ec44d0.
void CndWatchVibe::Parse(INI_Reader& Reader)
{
    ObjectA = Reader.get_value_string(0);
    ObjectB = Reader.get_value_string(1);
    if (IsNumericToken(Reader.get_value_string(2)))
    {
        Value = Reader.get_value_float(2);
    }
    else
    {
        TString<0x40> RepName;
        RepName.Length = 0;
        RepName.Data[0] = '\0';
        RepName = Reader.get_indexed_value(2);
        Value = GetRepValueFromName(Mission, RepName);
    }
    if (Reader.is_value_empty(3))
        return;
    if (_stricmp(Reader.get_value_string(3), "lt") == 0)
    {
        Compare = kCndVibeLess;
        return;
    }
    if (_stricmp(Reader.get_value_string(3), "gt") == 0)
    {
        Compare = kCndVibeGreater;
        return;
    }
    if (_stricmp(Reader.get_value_string(3), "eq") == 0)
        Compare = kCndVibeEqual;
}

// LAB_06ec4670: A's attitude towards B. Retail's "lt" is less or equal and "gt" is not less
// (both false on NaN).
bool CndWatchVibe::Matches(pub::Controller::EMessage /*EventId*/, void const* /*EventParms*/)
{
    if ((ObjectAId & GIdValueMask) == 0)
        ObjectAId = Mission->GetLabelObjectId(&ObjectA);
    if ((ObjectBId & GIdValueMask) == 0)
        ObjectBId = Mission->GetLabelObjectId(&ObjectB);
    if ((ObjectAId & GIdValueMask) == 0)
        return false;
    if ((ObjectBId & GIdValueMask) == 0)
        return false;
    int RepA = 0;
    int RepB = 0;
    float Attitude = 0.0f;
    pub::SpaceObj::GetRep(ObjectAId, RepA);
    pub::SpaceObj::GetRep(ObjectBId, RepB);
    pub::Reputation::GetAttitude(RepA, RepB, Attitude);
    if (Compare == kCndVibeLess)
        return Attitude < Value || Attitude == Value;
    if (Compare == kCndVibeGreater)
        return Attitude >= Value;
    if (Compare == kCndVibeEqual)
        return Attitude == Value;
    return false;
}

// FUN_06ec4640.
void CndWatchVibe::OnActivate()
{
    ObjectAId = Mission->GetLabelObjectId(&ObjectA);
    ObjectBId = Mission->GetLabelObjectId(&ObjectB);
}
