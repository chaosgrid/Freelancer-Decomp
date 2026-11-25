// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU40 (06f24670-06f28f60): the [ObjList] directives (see MissionObjList.h).

#include "Missions/MissionObjList.h"

#include "NPCs/BehaviorCmds.h"
#include "Core/ContentPlayerState.h"   // FindContentPlayerStateByShip, FContentPlayerShip
#include "NPCs/DockQueues.h"           // RemoveFromFormation (FUN_06f67090), SetDockEquipment
#include "Encounters/Encounters.h"           // FFormation, FindFormation
#include "NPCs/NpcShip.h"              // FContentNpcShip, GNpcShipEvents
#include "Core/RefHandle.h"            // TRefHandle (LoadObjList's local directive handle)
#include "Universe/SystemInfo.h"
#include "Core/ContentStaticIds.h"     // TU40's 16 static-init thunks 06f24670-06f24850
#include "Core/ContentInternal.h"      // SubtractVectors (FUN_06eeaee0)
#include "common_ini.h"
#include "common_server.h"

#include <cstring>

namespace
{
    // The ops declare some fields as integers where the directives store floats (spline
    // points, TetherCruiseRatio, the face point, the zone sphere): the bits are stored as they
    // are.
    template <typename T>
    void StoreFloatBits(T* Destination, float Value)
    {
        static_assert(sizeof(T) == sizeof(float), "4-byte field");
        memcpy(Destination, &Value, sizeof(float));
    }

    // The ships in SystemInfo::SpaceObjs are NPC ships (added by TU43 and TU53, both after
    // FUN_06f336a0); TU40 reads their ship fields at raw offsets.
    FContentNpcShip* AsNpcShip(FContentSpaceObj* Object)
    {
        return static_cast<FContentNpcShip*>(Object);
    }

    // Inlined in every Execute (FUN_06eea860, FUN_06f73ac0 with ship +0x170).
    unsigned int FindTargetId(FContentShipBase* Ship, TString<0x30> const* Name)
    {
        FSystemInfo* System = Ship->GetSystem();
        if (System == nullptr)
            return 0;
        return System->FindObjectIdByName(Name, Ship->ControllerId);
    }

    // The inlined priority default (FUN_06f57c80).
    void DefaultPriority(pub::AI::BaseOp* Op, FContentShipBase* Ship)
    {
        if (Op->Priority == 0)
            Op->Priority = (uint32_t)Ship->GetPriority();
    }

    // The inlined mute before a goto / guide / follow (IsSpacePopShip = FUN_06f33e80,
    // IsCommMuted = FUN_06f33ff0, StartTimer = FUN_06f68630 on DAT_06fd1bd8; 10.0f is
    // DAT_06fb8e24).
    void MuteNpcShip(FContentShipBase* Ship)
    {
        if (!Ship->IsNpcShip())
            return;
        FContentNpcShip* NpcShip = static_cast<FContentNpcShip*>(Ship);
        if (NpcShip->IsSpacePopShip())
            return;
        if (NpcShip->IsCommMuted())
            return;
        GNpcShipEvents.StartTimer(NpcShip, 10.0f);
    }

    // The inlined SubmitState to the ship's formation mates (Avoidance and the zone
    // directives): every other object of the ship's system with the same +0x154 (none when
    // the ship's is -1).
    void SubmitStateToFormation(FContentShipBase* Ship, pub::AI::BaseOp* Op)
    {
        FSystemInfo* System = Ship->GetSystem();
        if (System == nullptr)
            return;
        for (std::list<FContentSpaceObj*>::iterator It = System->SpaceObjs.begin();
             It != System->SpaceObjs.end(); ++It)
        {
            FContentNpcShip* Other = AsNpcShip(*It);
            if (Other->SpaceObjId == Ship->SpaceObjId)
                continue;
            if (Ship->FormationId == GNoFormationId)
                continue;
            if (Other->FormationId != Ship->FormationId)
                continue;
            pub::AI::SubmitState(Other->SpaceObjId, Op);
        }
    }

    // Value 0 of the goto keys: "goto" leaves both flags, "goto_no_cruise" sets bGotoNoCruise,
    // "goto_cruise" bGotoCruise.
    void ParseGotoKind(char const* Kind, uint8_t* NoCruise, uint8_t* Cruise)
    {
        if (_stricmp(Kind, "goto") == 0)
            return;
        if (_stricmp(Kind, "goto_no_cruise") == 0)
        {
            *NoCruise = 1;
            return;
        }
        if (_stricmp(Kind, "goto_cruise") == 0)
            *Cruise = 1;
    }

    void ClearName(TString<0x30>* Name)
    {
        Name->Length = 0;
        Name->Data[0] = '\0';
    }
}

// ---------------------------------------------------------------------------------------
// Factory and loader
// ---------------------------------------------------------------------------------------

// FUN_06f24870. The base part is FUN_06f25050 in cases 3-6, 0xa-0x10 and 0x15, inline
// elsewhere; the sizes are on the classes in the header.
bool __cdecl CreateObjListDirective(int Type, FObjListDirective** Out)
{
    FObjListDirective* Directive = nullptr;
    switch (Type)
    {
    case OBJLIST_AVOIDANCE: Directive = new FObjListAvoidance(); break;
    case OBJLIST_DELAY: Directive = new FObjListDelay(); break;
    case OBJLIST_IDLE: Directive = new FObjListIdle(); break;
    case OBJLIST_GOTO_SHIP: Directive = new FObjListGotoShip(); break;
    case OBJLIST_GOTO_SPLINE: Directive = new FObjListGotoSpline(); break;
    case OBJLIST_GOTO_VEC: Directive = new FObjListGotoVec(); break;
    case OBJLIST_TRAIL: Directive = new FObjListTrail(); break;
    case OBJLIST_FOLLOW: Directive = new FObjListFollow(); break;
    case OBJLIST_GUIDE: Directive = new FObjListGuide(); break;
    case OBJLIST_GUIDE_SHIP: Directive = new FObjListGuideShip(); break;
    case OBJLIST_FACE: Directive = new FObjListFace(); break;
    case OBJLIST_SET_LIFE_TIME: Directive = new FObjListSetLifeTime(); break;
    case OBJLIST_SET_PRIORITY: Directive = new FObjListSetPriority(); break;
    case OBJLIST_STAY_IN_RANGE: Directive = new FObjListStayInRange(); break;
    case OBJLIST_STAY_OUT_OF_RANGE: Directive = new FObjListStayOutOfRange(); break;
    case OBJLIST_MAKE_NEW_FORMATION: Directive = new FObjListMakeNewFormation(); break;
    case OBJLIST_FOLLOW_PLAYER: Directive = new FObjListFollowPlayer(); break;
    case OBJLIST_BREAK_FORMATION: Directive = new FObjListBreakFormation(); break;
    case OBJLIST_CANCEL_OBJECTIVE: Directive = new FObjListCancelObjective(); break;
    case OBJLIST_DOCK: Directive = new FObjListDock(); break;
    case OBJLIST_INSTANT_TRADELANE: Directive = new FObjListInstantTradelane(); break;
    case OBJLIST_LAUNCH: Directive = new FObjListLaunch(); break;
    case OBJLIST_WAIT_FOR_PLAYER_MANEUVER: Directive = new FObjListWaitForPlayerManeuver(); break;
    case OBJLIST_TRACTOR_BEAM: Directive = new FObjListTractorBeam(); break;
    default: break;
    }
    *Out = Directive;
    return Directive != nullptr;
}

namespace
{
    struct FObjListKey
    {
        char const* Key;
        int Type;
    };

    // The is_value chain of FUN_06f25260, in retail order.
    FObjListKey const GObjListKeys[] =
    {
        {"Avoidance", OBJLIST_AVOIDANCE},
        {"BreakFormation", OBJLIST_BREAK_FORMATION},
        {"CancelObjective", OBJLIST_CANCEL_OBJECTIVE},
        {"Delay", OBJLIST_DELAY},
        {"Idle", OBJLIST_IDLE},
        {"Dock", OBJLIST_DOCK},
        {"InstantTradelane", OBJLIST_INSTANT_TRADELANE},
        {"Follow", OBJLIST_FOLLOW},
        {"Guide", OBJLIST_GUIDE},
        {"GuideShip", OBJLIST_GUIDE_SHIP},
        {"GotoShip", OBJLIST_GOTO_SHIP},
        {"GotoSpline", OBJLIST_GOTO_SPLINE},
        {"GotoVec", OBJLIST_GOTO_VEC},
        {"Face", OBJLIST_FACE},
        {"Launch", OBJLIST_LAUNCH},
        {"MakeNewFormation", OBJLIST_MAKE_NEW_FORMATION},
        {"FollowPlayer", OBJLIST_FOLLOW_PLAYER},
        {"SetPriority", OBJLIST_SET_PRIORITY},
        {"SetLifeTime", OBJLIST_SET_LIFE_TIME},
        {"StayInRange", OBJLIST_STAY_IN_RANGE},
        {"StayOutOfRange", OBJLIST_STAY_OUT_OF_RANGE},
        {"Trail", OBJLIST_TRAIL},
        {"WaitForPlayerManeuver", OBJLIST_WAIT_FOR_PLAYER_MANEUVER},
    };
}

// FUN_06f25260. "system" and unknown keys are skipped. Name is clamped to 0xff. The list
// holds the one reference left once the creation reference is released.
void __cdecl LoadObjList(INI_Reader* Reader, std::list<FObjListDirective*>* List,
                         unsigned int* OutNickname)
{
    *OutNickname = CreateID("");
    while (Reader->read_value())
    {
        if (Reader->is_value("nickname"))
        {
            *OutNickname = CreateID(Reader->get_value_string());
            continue;
        }
        if (Reader->is_value("system"))
            continue;

        int Type = -1;
        for (int Index = 0; Index < static_cast<int>(sizeof(GObjListKeys) / sizeof(GObjListKeys[0]));
             ++Index)
        {
            if (Reader->is_value(GObjListKeys[Index].Key))
            {
                Type = GObjListKeys[Index].Type;
                break;
            }
        }
        if (Type < 0)
            continue;

        // The creation reference lives in a local handle (FUN_06f28e10); the list takes its
        // own reference.
        TRefHandle<FObjListDirective> Directive;
        CreateObjListDirective(Type, &Directive.Pointer);
        Directive->Name = Reader->get_line_ptr();
        Directive->Parse(Reader);
        List->push_back(Directive);
        Directive->AddRef();
    }
}

// ---------------------------------------------------------------------------------------
// 0 Avoidance
// ---------------------------------------------------------------------------------------

// FUN_06f25f60 (AdvanceDirective = FUN_06f57b00).
bool FObjListAvoidance::Execute(FContentShipBase* Ship)
{
    pub::AI::SubmitState(Ship->SpaceObjId, &Op);
    SubmitStateToFormation(Ship, &Op);
    Ship->AdvanceDirective();
    return true;
}

// FUN_06f25fe0.
void FObjListAvoidance::Parse(INI_Reader* Reader)
{
    Op.FlagId = 0;
    Op.bValue = _stricmp(Reader->get_value_string(0), "true") == 0 ? 1 : 0;
}

// 06ea1450.
int FObjListAvoidance::GetType()
{
    return 0;
}

// FUN_06f26020.
void FObjListAvoidance::SetAvoidance(bool bAvoid)
{
    Op.FlagId = 0;
    Op.bValue = bAvoid ? 1 : 0;
}

// ---------------------------------------------------------------------------------------
// 1 Delay, 2 Idle
// ---------------------------------------------------------------------------------------

// FUN_06f27900 (SubmitDirective = FUN_06f57c60).
bool FObjListDelay::Execute(FContentShipBase* Ship)
{
    DefaultPriority(&Op, Ship);
    return Ship->SubmitDirective(&Op);
}

// FUN_06f27950.
void FObjListDelay::Parse(INI_Reader* Reader)
{
    Op.DelaySeconds = Reader->get_value_float(0);
}

// 06ee9e10.
int FObjListDelay::GetType()
{
    return 1;
}

// FUN_06f27940.
void FObjListDelay::SetDelay(float Seconds)
{
    Op.DelaySeconds = Seconds;
}

// FUN_06f27900 (the Delay body, ICF-folded).
bool FObjListIdle::Execute(FContentShipBase* Ship)
{
    DefaultPriority(&Op, Ship);
    return Ship->SubmitDirective(&Op);
}

// 06ee9660.
int FObjListIdle::GetType()
{
    return 2;
}

// ---------------------------------------------------------------------------------------
// 3 GotoShip, 4 GotoSpline, 5 GotoVec
// ---------------------------------------------------------------------------------------

namespace
{
    // FUN_06f263a0, the Execute of GotoSpline and GotoVec. An object name that does not
    // resolve finishes the directive.
    bool ExecuteGotoToObject(FContentShipBase* Ship, pub::AI::DirectiveGotoOp* Op,
                             TString<0x30> const* ObjectName)
    {
        if (ObjectName->Length != 0)
        {
            unsigned int Id = FindTargetId(Ship, ObjectName);
            Op->TetherObjectId = Id;
            if ((Id & GIdValueMask) == 0)
            {
                Ship->AdvanceDirective();
                return true;
            }
        }
        DefaultPriority(Op, Ship);
        MuteNpcShip(Ship);
        return Ship->SubmitDirective(Op);
    }
}

// Inlined in factory case 3.
FObjListGotoShip::FObjListGotoShip()
{
    ClearName(&TargetName);
    ClearName(&ObjectName);
}

// FUN_06f26040. A name that does not resolve finishes the directive.
bool FObjListGotoShip::Execute(FContentShipBase* Ship)
{
    if (TargetName.Length != 0)
    {
        unsigned int Id = FindTargetId(Ship, &TargetName);
        Op.TargetId = static_cast<int32_t>(Id);
        if ((Id & GIdValueMask) == 0)
        {
            Ship->AdvanceDirective();
            return true;
        }
    }
    if (ObjectName.Length != 0)
    {
        unsigned int Id = FindTargetId(Ship, &ObjectName);
        Op.TetherObjectId = Id;
        if ((Id & GIdValueMask) == 0)
        {
            Ship->AdvanceDirective();
            return true;
        }
    }
    DefaultPriority(&Op, Ship);
    MuteNpcShip(Ship);
    return Ship->SubmitDirective(&Op);
}

// FUN_06f26190.
void FObjListGotoShip::Parse(INI_Reader* Reader)
{
    bNoFailOnTimeout = 1;
    ParseGotoKind(Reader->get_value_string(0), &Op.bGotoNoCruise, &Op.bGotoCruise);
    Op.GotoKind = 0;
    TargetName = Reader->get_value_string(1);
    Op.ArrivalRange = Reader->get_value_float(2);
    Op.bFlyThrough = _stricmp(Reader->get_value_string(3), "true") == 0 ? 1 : 0;
    if (!Reader->is_value_empty(4))
        Op.MaxSpeed = static_cast<float>(Reader->get_value_int(4));
    if (Reader->is_value_empty(5))
        return;
    ObjectName = Reader->get_value_string(5);
    Op.TetherNearRange = static_cast<float>(Reader->get_value_int(6));
    Op.TetherFarRange = static_cast<float>(Reader->get_value_int(7));
    if (!Reader->is_value_empty(8))
        StoreFloatBits(&Op.TetherCruiseRatio, Reader->get_value_float(8));
    if (!Reader->is_value_empty(9))
        bNoFailOnTimeout = Reader->get_value_int(9);
}

// 06f5e540.
int FObjListGotoShip::GetType()
{
    return 3;
}

// FUN_06f26150.
void FObjListGotoShip::SetTarget(unsigned int const* TargetId, float Range, float Param3)
{
    Op.GotoKind = 0;
    Op.TargetId = static_cast<int32_t>(*TargetId);
    Op.bFlyThrough = 0;
    Op.ArrivalRange = Range;
    Op.MaxSpeed = Param3;
}

// Inlined in factory case 4.
FObjListGotoSpline::FObjListGotoSpline()
{
    ClearName(&ObjectName);
}

bool FObjListGotoSpline::Execute(FContentShipBase* Ship)
{
    return ExecuteGotoToObject(Ship, &Op, &ObjectName);
}

// FUN_06f26e60. GotoKind = 2 (a spline); the four points come in the
// value order 2, 0, 1, 3 (values 1-3 -> +0x144, 4-6 -> +0x12c, 7-9 -> +0x138, 10-12 ->
// +0x150); ArrivalRange is a float here.
void FObjListGotoSpline::Parse(INI_Reader* Reader)
{
    bNoFailOnTimeout = 1;
    ParseGotoKind(Reader->get_value_string(0), &Op.bGotoNoCruise, &Op.bGotoCruise);
    Op.GotoKind = 2;

    StoreFloatBits(&Op.SplineP0X, Reader->get_value_float(1));
    StoreFloatBits(&Op.SplineP0Y, Reader->get_value_float(2));
    StoreFloatBits(&Op.SplineP0Z, Reader->get_value_float(3));
    StoreFloatBits(&Op.SplineP1X, Reader->get_value_float(4));
    StoreFloatBits(&Op.SplineP1Y, Reader->get_value_float(5));
    StoreFloatBits(&Op.SplineP1Z, Reader->get_value_float(6));
    StoreFloatBits(&Op.SplineP2X, Reader->get_value_float(7));
    StoreFloatBits(&Op.SplineP2Y, Reader->get_value_float(8));
    StoreFloatBits(&Op.SplineP2Z, Reader->get_value_float(9));
    StoreFloatBits(&Op.SplineP3X, Reader->get_value_float(10));
    StoreFloatBits(&Op.SplineP3Y, Reader->get_value_float(11));
    StoreFloatBits(&Op.SplineP3Z, Reader->get_value_float(12));

    Op.ArrivalRange = Reader->get_value_float(13);
    Op.bFlyThrough = Reader->get_value_bool(14) ? 1 : 0;
    if (!Reader->is_value_empty(15))
        Op.MaxSpeed = static_cast<float>(Reader->get_value_int(15));
    if (Reader->is_value_empty(16))
        return;
    ObjectName = Reader->get_value_string(16);
    Op.TetherNearRange = static_cast<float>(Reader->get_value_int(17));
    Op.TetherFarRange = static_cast<float>(Reader->get_value_int(18));
    if (!Reader->is_value_empty(19))
        StoreFloatBits(&Op.TetherCruiseRatio, Reader->get_value_float(19));
    if (!Reader->is_value_empty(20))
        bNoFailOnTimeout = Reader->get_value_int(20);
}

// 06f25080.
int FObjListGotoSpline::GetType()
{
    return 4;
}

// Inlined in factory case 5.
FObjListGotoVec::FObjListGotoVec()
{
    ClearName(&ObjectName);
}

bool FObjListGotoVec::Execute(FContentShipBase* Ship)
{
    return ExecuteGotoToObject(Ship, &Op, &ObjectName);
}

// FUN_06f264d0. GotoKind = 1 (a point); the point and ArrivalRange are read as ints.
void FObjListGotoVec::Parse(INI_Reader* Reader)
{
    bNoFailOnTimeout = 1;
    ParseGotoKind(Reader->get_value_string(0), &Op.bGotoNoCruise, &Op.bGotoCruise);
    Op.GotoKind = 1;
    Op.Destination.x = static_cast<float>(Reader->get_value_int(1));
    Op.Destination.y = static_cast<float>(Reader->get_value_int(2));
    Op.Destination.z = static_cast<float>(Reader->get_value_int(3));
    Op.ArrivalRange = static_cast<float>(Reader->get_value_int(4));
    Op.bFlyThrough = Reader->get_value_bool(5) ? 1 : 0;
    if (!Reader->is_value_empty(6))
        Op.MaxSpeed = static_cast<float>(Reader->get_value_int(6));
    if (Reader->is_value_empty(7))
        return;
    ObjectName = Reader->get_value_string(7);
    Op.TetherNearRange = static_cast<float>(Reader->get_value_int(8));
    Op.TetherFarRange = static_cast<float>(Reader->get_value_int(9));
    if (!Reader->is_value_empty(10))
        StoreFloatBits(&Op.TetherCruiseRatio, Reader->get_value_float(10));
    if (!Reader->is_value_empty(11))
        bNoFailOnTimeout = Reader->get_value_int(11);
}

// 06ee99a0.
int FObjListGotoVec::GetType()
{
    return 5;
}

// FUN_06f26460.
void FObjListGotoVec::SetDestination(Vector const* Destination, float Range, float MaxSpeed,
                                     unsigned int ObjectId, bool bNoCruise, bool bCruise)
{
    Op.GotoKind = 1;
    Op.Destination = *Destination;
    Op.ArrivalRange = Range;
    Op.MaxSpeed = MaxSpeed;
    Op.bFlyThrough = 0;
    Op.bGotoCruise = bCruise ? 1 : 0;
    if ((ObjectId & GIdValueMask) != 0)
        Op.TetherObjectId = ObjectId;
    Op.bGotoNoCruise = bNoCruise ? 1 : 0;
}

// ---------------------------------------------------------------------------------------
// 6 Trail, 7 Follow
// ---------------------------------------------------------------------------------------

// Inlined in factory case 6.
FObjListTrail::FObjListTrail()
{
    ClearName(&TargetName);
}

// LAB_06f27520. Retail calls vtbl +4 (IsNpcShip) and drops the result. A name that does not
// resolve finishes the directive.
bool FObjListTrail::Execute(FContentShipBase* Ship)
{
    Ship->IsNpcShip();
    if (TargetName.Length != 0)
    {
        unsigned int Id = FindTargetId(Ship, &TargetName);
        Op.TargetId = Id;
        if ((Id & GIdValueMask) == 0)
        {
            Ship->AdvanceDirective();
            return true;
        }
    }
    DefaultPriority(&Op, Ship);
    return Ship->SubmitDirective(&Op);
}

// FUN_06f275d0.
void FObjListTrail::Parse(INI_Reader* Reader)
{
    TargetName = Reader->get_value_string(0);
    Op.TrailDistance = static_cast<float>(Reader->get_value_int(1));
}

// 06f250c0.
int FObjListTrail::GetType()
{
    return 6;
}

// FUN_06f275b0.
void FObjListTrail::SetTarget(unsigned int const* TargetId, float Distance)
{
    Op.TargetId = *TargetId;
    Op.TrailDistance = Distance;
}

// Inlined in factory case 7.
FObjListFollow::FObjListFollow()
{
    ClearName(&TargetName);
}

// LAB_06f27340. A name that does not resolve finishes the directive.
bool FObjListFollow::Execute(FContentShipBase* Ship)
{
    if (TargetName.Length != 0)
    {
        unsigned int Id = FindTargetId(Ship, &TargetName);
        Op.TargetId = Id;
        if ((Id & GIdValueMask) == 0)
        {
            Ship->AdvanceDirective();
            return true;
        }
    }
    DefaultPriority(&Op, Ship);
    MuteNpcShip(Ship);
    return Ship->SubmitDirective(&Op);
}

// FUN_06f27400. FollowDistance is read as an int; exactly -1.0 becomes 2500.0 (DAT_06fb8e2c).
void FObjListFollow::Parse(INI_Reader* Reader)
{
    bNoFailOnTimeout = 1;
    TargetName = Reader->get_value_string(0);
    Op.FollowDistance = static_cast<float>(Reader->get_value_int(1));
    if (Op.FollowDistance == -1.0f)
        Op.FollowDistance = 2500.0f;
    if (!Reader->is_value_empty(2))
    {
        Op.FollowOffsetX = static_cast<float>(Reader->get_value_int(2));
        Op.FollowOffsetY = static_cast<float>(Reader->get_value_int(3));
        Op.FollowOffsetZ = static_cast<float>(Reader->get_value_int(4));
    }
    if (!Reader->is_value_empty(5))
        Op.EngageRange = static_cast<float>(Reader->get_value_int(5));
}

// 06f250d0.
int FObjListFollow::GetType()
{
    return 7;
}

// ---------------------------------------------------------------------------------------
// 8 Guide, 9 GuideShip
// ---------------------------------------------------------------------------------------

// Inlined in factory case 8.
FObjListGuide::FObjListGuide()
{
    ClearName(&TargetName);
}

// LAB_06f266a0. Retail calls vtbl +4 and drops the result. A name that does not resolve
// finishes the directive.
bool FObjListGuide::Execute(FContentShipBase* Ship)
{
    Ship->IsNpcShip();
    if (TargetName.Length != 0)
    {
        unsigned int Id = FindTargetId(Ship, &TargetName);
        Op.GotoParams.TetherObjectId = Id;
        if ((Id & GIdValueMask) == 0)
        {
            Ship->AdvanceDirective();
            return true;
        }
    }
    Op.FollowParams.TargetId = Op.GotoParams.TetherObjectId;
    DefaultPriority(&Op, Ship);
    MuteNpcShip(Ship);
    return Ship->SubmitDirective(&Op);
}

// FUN_06f26810.
void FObjListGuide::Parse(INI_Reader* Reader)
{
    bNoFailOnTimeout = 1;
    TargetName = Reader->get_value_string(0);
    Op.FollowParams.FollowOffsetX = static_cast<float>(Reader->get_value_int(1));
    Op.FollowParams.FollowOffsetY = static_cast<float>(Reader->get_value_int(2));
    Op.FollowParams.FollowOffsetZ = static_cast<float>(Reader->get_value_int(3));
    Op.GotoParams.GotoKind = 1;
    ParseGotoKind(Reader->get_value_string(4), &Op.GotoParams.bGotoNoCruise, &Op.GotoParams.bGotoCruise);
    Op.GotoParams.Destination.x = static_cast<float>(Reader->get_value_int(5));
    Op.GotoParams.Destination.y = static_cast<float>(Reader->get_value_int(6));
    Op.GotoParams.Destination.z = static_cast<float>(Reader->get_value_int(7));
    Op.GotoParams.ArrivalRange = static_cast<float>(Reader->get_value_int(8));
    Op.GotoParams.bFlyThrough = Reader->get_value_bool(9) ? 1 : 0;
    Op.GotoParams.MaxSpeed = static_cast<float>(Reader->get_value_int(10));
    Op.GotoParams.TetherNearRange = static_cast<float>(Reader->get_value_int(11));
    Op.GotoParams.TetherFarRange = static_cast<float>(Reader->get_value_int(12));
    if (!Reader->is_value_empty(13))
        Op.bStartFollowing = Reader->get_value_bool(13) ? 1 : 0;
    if (!Reader->is_value_empty(14))
        Op.Field_AC = Reader->get_value_int(14);
    if (!Reader->is_value_empty(15))
        Op.SampleInterval = Reader->get_value_float(15);
    if (!Reader->is_value_empty(16))
        Op.ProgressHighThreshold = Reader->get_value_float(16);
    if (!Reader->is_value_empty(17))
        Op.ProgressLowThreshold = Reader->get_value_float(17);
    if (!Reader->is_value_empty(18))
        Op.CourseAngleThreshold = Reader->get_value_float(18);
    if (!Reader->is_value_empty(19))
        bNoFailOnTimeout = Reader->get_value_int(19);
}

// 06ee99a0.
int FObjListGuide::GetType()
{
    return 5;
}

// FUN_06f26770.
void FObjListGuide::SetGuide(unsigned int const* TargetId, Vector const* FollowOffset,
                             Vector const* Destination, float Range, bool bNoCruise,
                             bool bCruise, bool bStartFollowing, bool bFlyThrough)
{
    Op.GotoParams.GotoKind = 1;
    Op.GotoParams.Destination = *Destination;
    Op.GotoParams.bFlyThrough = bFlyThrough ? 1 : 0;
    Op.GotoParams.ArrivalRange = Range;
    Op.GotoParams.TetherObjectId = *TargetId;
    Op.GotoParams.bGotoNoCruise = bNoCruise ? 1 : 0;
    Op.GotoParams.bGotoCruise = bCruise ? 1 : 0;
    Op.GotoParams.TetherNearRange = 200.0f;
    Op.GotoParams.TetherFarRange = 500.0f;
    Op.FollowParams.TargetId = *TargetId;
    Op.FollowParams.FollowOffsetX = FollowOffset->x;
    Op.FollowParams.FollowOffsetY = FollowOffset->y;
    Op.FollowParams.FollowOffsetZ = FollowOffset->z;
    Op.bStartFollowing = bStartFollowing ? 1 : 0;
}

// Inlined in factory case 9.
FObjListGuideShip::FObjListGuideShip()
{
    ClearName(&ObjectName);
    ClearName(&TargetName);
}

// LAB_06f26a90. Retail calls vtbl +4 and drops the result. No Length test here: an empty name
// resolves to 0 and finishes the directive. The GetLocation result is never read.
bool FObjListGuideShip::Execute(FContentShipBase* Ship)
{
    Ship->IsNpcShip();
    unsigned int TargetId = FindTargetId(Ship, &TargetName);
    Op.GotoParams.TetherObjectId = TargetId;
    if ((TargetId & GIdValueMask) == 0)
    {
        Ship->AdvanceDirective();
        return true;
    }
    unsigned int ObjectId = FindTargetId(Ship, &ObjectName);
    Op.GotoParams.TargetId = static_cast<int32_t>(ObjectId);
    if ((ObjectId & GIdValueMask) == 0)
    {
        Ship->AdvanceDirective();
        return true;
    }
    Op.FollowParams.TargetId = Op.GotoParams.TetherObjectId;
    DefaultPriority(&Op, Ship);
    Vector Position;
    Matrix Orientation;
    pub::SpaceObj::GetLocation(static_cast<unsigned int>(Op.GotoParams.TargetId), Position,
                               Orientation);
    MuteNpcShip(Ship);
    return Ship->SubmitDirective(&Op);
}

// FUN_06f26bb0. GotoKind = 0 (the goto target is an object).
void FObjListGuideShip::Parse(INI_Reader* Reader)
{
    bNoFailOnTimeout = 1;
    TargetName = Reader->get_value_string(0);
    Op.FollowParams.FollowOffsetX = static_cast<float>(Reader->get_value_int(1));
    Op.FollowParams.FollowOffsetY = static_cast<float>(Reader->get_value_int(2));
    Op.FollowParams.FollowOffsetZ = static_cast<float>(Reader->get_value_int(3));
    Op.GotoParams.GotoKind = 0;
    ParseGotoKind(Reader->get_value_string(4), &Op.GotoParams.bGotoNoCruise, &Op.GotoParams.bGotoCruise);
    ObjectName = Reader->get_value_string(5);
    Op.GotoParams.ArrivalRange = static_cast<float>(Reader->get_value_int(6));
    Op.GotoParams.bFlyThrough = Reader->get_value_bool(7) ? 1 : 0;
    Op.GotoParams.MaxSpeed = static_cast<float>(Reader->get_value_int(8));
    Op.GotoParams.TetherNearRange = static_cast<float>(Reader->get_value_int(9));
    Op.GotoParams.TetherFarRange = static_cast<float>(Reader->get_value_int(10));
    if (!Reader->is_value_empty(11))
        Op.bStartFollowing = Reader->get_value_bool(11) ? 1 : 0;
    if (!Reader->is_value_empty(12))
        Op.Field_AC = Reader->get_value_int(12);
    if (!Reader->is_value_empty(13))
        Op.SampleInterval = Reader->get_value_float(13);
    if (!Reader->is_value_empty(14))
        Op.ProgressHighThreshold = Reader->get_value_float(14);
    if (!Reader->is_value_empty(15))
        Op.ProgressLowThreshold = Reader->get_value_float(15);
    if (!Reader->is_value_empty(16))
        Op.CourseAngleThreshold = Reader->get_value_float(16);
    if (!Reader->is_value_empty(17))
        bNoFailOnTimeout = Reader->get_value_int(17);
}

// 06ee99a0.
int FObjListGuideShip::GetType()
{
    return 5;
}

// ---------------------------------------------------------------------------------------
// 0xa Face
// ---------------------------------------------------------------------------------------

// Inlined in factory case 0xa.
FObjListFace::FObjListFace()
{
    ClearName(&TargetName);
}

// LAB_06f27150. Retail calls vtbl +4 and drops the result. A name that does not resolve
// finishes the directive.
bool FObjListFace::Execute(FContentShipBase* Ship)
{
    Ship->IsNpcShip();
    if (Op.Mode == 1 && TargetName.Length != 0)
    {
        unsigned int Id = FindTargetId(Ship, &TargetName);
        Op.TargetId = Id;
        if ((Id & GIdValueMask) == 0)
        {
            Ship->AdvanceDirective();
            return true;
        }
    }
    DefaultPriority(&Op, Ship);
    return Ship->SubmitDirective(&Op);
}

// LAB_06f271f0. Any other mode name leaves Mode.
void FObjListFace::Parse(INI_Reader* Reader)
{
    if (_stricmp(Reader->get_value_string(0), "OBJECT") == 0)
        Op.Mode = 1;
    else if (_stricmp(Reader->get_value_string(0), "POINT") == 0)
        Op.Mode = 2;

    if (Op.Mode == 1)
    {
        TargetName = Reader->get_value_string(1);
        if (!Reader->is_value_empty(2))
            Op.bKillEngine = Reader->get_value_bool(2) ? 1 : 0;
        return;
    }
    if (Op.Mode != 2)
        return;
    Op.FacePoint.x = Reader->get_value_float(1);
    Op.FacePoint.y = Reader->get_value_float(2);
    Op.FacePoint.z = Reader->get_value_float(3);
    if (!Reader->is_value_empty(4))
        Op.bKillEngine = Reader->get_value_bool(4) ? 1 : 0;
}

// 06f25070.
int FObjListFace::GetType()
{
    return 0xa;
}

// ---------------------------------------------------------------------------------------
// 0xb SetLifeTime, 0xc SetPriority
// ---------------------------------------------------------------------------------------

// LAB_06f282e0: NPC +0x1bc / +0x1c8, also on the formation mates. Retail calls vtbl +4 twice
// and drops the first result.
bool FObjListSetLifeTime::Execute(FContentShipBase* Ship)
{
    Ship->IsNpcShip();
    if (Ship->IsNpcShip())
    {
        FContentNpcShip* NpcShip = static_cast<FContentNpcShip*>(Ship);
        NpcShip->Longevity = static_cast<float>(LifeTime);
        NpcShip->LifeTime = static_cast<float>(LifeTime);
    }
    FSystemInfo* System = Ship->GetSystem();
    if (System != nullptr)
    {
        for (std::list<FContentSpaceObj*>::iterator It = System->SpaceObjs.begin();
             It != System->SpaceObjs.end(); ++It)
        {
            FContentNpcShip* Other = AsNpcShip(*It);
            if (Ship->SpaceObjId == Other->SpaceObjId)
                continue;
            if (Ship->FormationId == GNoFormationId)
                continue;
            if (Other->FormationId != Ship->FormationId)
                continue;
            Other->Longevity = static_cast<float>(LifeTime);
            Other->LifeTime = static_cast<float>(LifeTime);
        }
    }
    Ship->AdvanceDirective();
    return true;
}

// FUN_06f28390.
void FObjListSetLifeTime::Parse(INI_Reader* Reader)
{
    LifeTime = Reader->get_value_int(0);
}

// 06f250e0.
int FObjListSetLifeTime::GetType()
{
    return 0xb;
}

// FUN_06f28260 (SetPriority = FUN_06f57c90).
bool FObjListSetPriority::Execute(FContentShipBase* Ship)
{
    Ship->SetPriority(&Priority);
    Ship->AdvanceDirective();
    return true;
}

// FUN_06f28280.
void FObjListSetPriority::Parse(INI_Reader* Reader)
{
    if (_stricmp(Reader->get_value_string(0), "NORMAL") == 0)
    {
        Priority = 1;
        return;
    }
    if (_stricmp(Reader->get_value_string(0), "ALWAYS_EXECUTE") == 0)
        Priority = 0;
}

// 06f250b0.
int FObjListSetPriority::GetType()
{
    return 0xc;
}

// ---------------------------------------------------------------------------------------
// 0xd StayInRange, 0xe StayOutOfRange
// ---------------------------------------------------------------------------------------

namespace
{
    // LAB_06f27cb0 / LAB_06f27fa0 (identical bodies). Mode 1 computes BoxMax - BoxMin
    // (FUN_06eeaee0) into a local nothing reads. A name that does not resolve finishes the
    // directive.
    bool ExecuteZone(FContentShipBase* Ship, pub::AI::SetZoneBehaviorParams* Op,
                     TString<0x30> const* ObjectName)
    {
        if (Op->Mode == 1)
        {
            Vector const Unused = SubtractVectors(Op->BoxMax, Op->BoxMin);
            (void)Unused;
        }
        if (ObjectName->Length != 0)
        {
            unsigned int Id = FindTargetId(Ship, ObjectName);
            Op->TargetId = Id;
            if ((Id & GIdValueMask) == 0)
            {
                Ship->AdvanceDirective();
                return true;
            }
        }
        DefaultPriority(Op, Ship);
        pub::AI::SubmitState(Ship->SpaceObjId, Op);
        SubmitStateToFormation(Ship, Op);
        Ship->AdvanceDirective();
        return true;
    }

    // LAB_06f27df0 / LAB_06f280a0 after their first store (InvertMode = 0 / 1): a box (Mode
    // 1, op +0x24..+0x38), then a sphere (Mode 0) or a named object (Mode 2). The value after
    // them, when present and false, sets InvertMode = 2.
    void ParseZone(INI_Reader* Reader, pub::AI::SetZoneBehaviorParams* Op,
                   TString<0x30>* ObjectName)
    {
        unsigned int Index = 0;
        if (!Reader->is_value_empty(5))
        {
            Op->Mode = 1;
            Op->BoxMin.x = Reader->get_value_float(0);
            Op->BoxMin.y = Reader->get_value_float(1);
            Op->BoxMin.z = Reader->get_value_float(2);
            Op->BoxMax.x = Reader->get_value_float(3);
            Op->BoxMax.y = Reader->get_value_float(4);
            Op->BoxMax.z = Reader->get_value_float(5);
            Index = 6;
        }
        // Retail quirk (06f27e7e): the box is followed by the is_value_empty(3) test, not
        // an else branch, so slot 3 decides the mode even after a box.
        if (!Reader->is_value_empty(3))
        {
            Op->Mode = 0;
            Op->TargetPoint.x = (float)Reader->get_value_int(Index);
            Op->TargetPoint.y = (float)Reader->get_value_int(Index + 1);
            Op->TargetPoint.z = (float)Reader->get_value_int(Index + 2);
            Op->Radius = (float)Reader->get_value_int(Index + 3);
            Index += 4;
        }
        else
        {
            Op->Mode = 2;
            *ObjectName = Reader->get_value_string(Index);
            Op->Radius = (float)Reader->get_value_int(Index + 1);
            Index += 2;
        }
        if (!Reader->is_value_empty(Index) && !Reader->get_value_bool(Index))
            Op->InvertMode = 2;
    }
}

// Inlined in factory case 0xd.
FObjListStayInRange::FObjListStayInRange()
{
    ClearName(&ObjectName);
}

bool FObjListStayInRange::Execute(FContentShipBase* Ship)
{
    return ExecuteZone(Ship, &Op, &ObjectName);
}

void FObjListStayInRange::Parse(INI_Reader* Reader)
{
    Op.InvertMode = 0;
    ParseZone(Reader, &Op, &ObjectName);
}

// 06f250f0.
int FObjListStayInRange::GetType()
{
    return 0xd;
}

// FUN_06f27db0.
void FObjListStayInRange::SetSphere(Vector const* Center, float Radius)
{
    Op.TargetPoint.x = Center->x;
    Op.TargetPoint.y = Center->y;
    Op.TargetPoint.z = Center->z;
    Op.Radius = Radius;
    Op.Mode = 0;
    Op.InvertMode = 0;
}

// Inlined in factory case 0xe.
FObjListStayOutOfRange::FObjListStayOutOfRange()
{
    ClearName(&ObjectName);
}

bool FObjListStayOutOfRange::Execute(FContentShipBase* Ship)
{
    return ExecuteZone(Ship, &Op, &ObjectName);
}

void FObjListStayOutOfRange::Parse(INI_Reader* Reader)
{
    Op.InvertMode = 1;
    ParseZone(Reader, &Op, &ObjectName);
}

// 06f25100.
int FObjListStayOutOfRange::GetType()
{
    return 0xe;
}

// ---------------------------------------------------------------------------------------
// 0xf MakeNewFormation, 0x10 FollowPlayer, 0x11 BreakFormation, 0x12 CancelObjective
// ---------------------------------------------------------------------------------------

// LAB_06f283b0 (FindSpaceObjByNickname = FUN_06f739c0, FormFormation = FUN_06f66ec0). Retail
// calls vtbl +4 and drops the result.
bool FObjListMakeNewFormation::Execute(FContentShipBase* Ship)
{
    Ship->IsNpcShip();
    std::list<FContentShipBase*> Members;
    FSystemInfo* System = Ship->GetSystem();
    if (System != nullptr)
    {
        Members.push_back(Ship);
        for (std::vector<TString<0x30>>::iterator It = MemberNames.begin();
             It != MemberNames.end(); ++It)
        {
            FContentSpaceObj* Object = System->FindSpaceObjByNickname(It->Data);
            if (Object == nullptr || Ship == nullptr)
                continue;
            if (Object->SpaceObjId == Ship->SpaceObjId)
                continue;
            FContentNpcShip* Member = AsNpcShip(Object);
            Member->CancelDirectives();
            Members.push_back(Member);
        }
        FormFormation(&Members, Formation, bAddFollowers);
    }
    Ship->AdvanceDirective();
    return true;
}

// LAB_06f28560 (FindFormation = FUN_06efd1a0). The two slot counts (FUN_06efcaf0 /
// FUN_06efcac0) are dropped. "Player" is appended to the names like any other.
void FObjListMakeNewFormation::Parse(INI_Reader* Reader)
{
    bAddFollowers = false;
    unsigned int FormationId = CreateID(Reader->get_value_string(0));
    Formation = FindFormation(&FormationId);
    if (Formation == nullptr)
        return;
    (void)Formation->CountShipSlots();
    (void)Formation->CountPlayerSlots();
    for (unsigned int Index = 1; !Reader->is_value_empty(Index); ++Index)
    {
        if (_stricmp(Reader->get_value_string(Index), "Player") != 0 &&
            _stricmp(Reader->get_value_string(Index), "ADD_FOLLOWERS") == 0)
        {
            bAddFollowers = true;
            continue;
        }
        TString<0x30> Name;
        Name = Reader->get_value_string(Index);
        MemberNames.push_back(Name);
    }
}

// 06f25110.
int FObjListMakeNewFormation::GetType()
{
    return 0xf;
}

// FUN_06f28840. Retail calls vtbl +4 and drops the result, and builds a local list holding
// only the ship that nothing reads; the port keeps that local. The leader slot is the last
// slot when FUN_06efccd0 says it is the player's (no range check: an empty formation reads
// slot -1), else -1. Neither Formation nor the ship's system is null-checked. Each member
// follows the ship at 1000.0 with the offset slot(n) - slot(leader) (FUN_06efcb20). Returns
// the last SubmitDirective result (true without members).
bool FObjListFollowPlayer::Execute(FContentShipBase* Ship)
{
    Ship->IsNpcShip();
    bool bResult = true;
    std::list<FContentShipBase*> UnusedShips;
    FSystemInfo* System = Ship->GetSystem();
    UnusedShips.push_back(Ship);

    int SlotCount = static_cast<int>(Formation->Slots.size());
    int LeaderSlot = -1;
    if (Formation->IsPlayerSlot(SlotCount - 1))
        LeaderSlot = SlotCount - 1;

    unsigned int MemberSlot = 0;
    for (std::vector<TString<0x30>>::iterator It = MemberNames.begin(); It != MemberNames.end();
         ++It)
    {
        FContentSpaceObj* Object = System->FindSpaceObjByNickname(It->Data);
        if (Object == nullptr || Ship == nullptr)
            continue;
        if (Object->SpaceObjId == Ship->SpaceObjId)
            continue;
        FContentNpcShip* Member = AsNpcShip(Object);
        if (Member->FormationSlot != 0 && Member->FormationId != GNoFormationId)
            RemoveFromFormation(Member);

        pub::AI::DirectiveFollowOp FollowOp;
        FollowOp.TargetId = Ship->SpaceObjId;
        FollowOp.FollowDistance = 1000.0f;
        FVector LeaderPosition = Formation->GetSlotPosition(static_cast<unsigned int>(LeaderSlot));
        FVector MemberPosition = Formation->GetSlotPosition(MemberSlot);
        ++MemberSlot;
        FollowOp.FollowOffsetX = MemberPosition.X - LeaderPosition.X;
        FollowOp.FollowOffsetY = MemberPosition.Y - LeaderPosition.Y;
        FollowOp.FollowOffsetZ = MemberPosition.Z - LeaderPosition.Z;

        Member->CancelDirectives();
        DefaultPriority(&FollowOp, Member);
        bResult = Member->SubmitDirective(&FollowOp);
    }
    Ship->AdvanceDirective();
    return bResult;
}

// LAB_06f28ae0: values 1 .. slot count + 1 that are present are appended to the names.
void FObjListFollowPlayer::Parse(INI_Reader* Reader)
{
    unsigned int FormationId = CreateID(Reader->get_value_string(0));
    Formation = FindFormation(&FormationId);
    if (Formation == nullptr || Formation->Slots.empty())
        return;
    int Last = static_cast<int>(Formation->Slots.size()) + 1;
    for (int Index = 1; Index <= Last; ++Index)
    {
        if (Reader->is_value_empty(static_cast<unsigned int>(Index)))
            continue;
        TString<0x30> Name;
        Name = Reader->get_value_string(static_cast<unsigned int>(Index));
        MemberNames.push_back(Name);
    }
}

// 06f25110 (shared with MakeNewFormation, so FollowPlayer reports 0xf).
int FObjListFollowPlayer::GetType()
{
    return 0xf;
}

// FUN_06f28d70 (RemoveFromFormation = FUN_06f67090).
bool FObjListBreakFormation::Execute(FContentShipBase* Ship)
{
    RemoveFromFormation(Ship);
    Ship->AdvanceDirective();
    return true;
}

// 06f25120.
int FObjListBreakFormation::GetType()
{
    return 0x11;
}

// FUN_06f28d90 (CancelDirectives = FUN_06f57740).
bool FObjListCancelObjective::Execute(FContentShipBase* Ship)
{
    if (Ship->FormationId == GNoFormationId)
        Ship->CancelDirectives();
    else
        RemoveFromFormation(Ship);
    Ship->AdvanceDirective();
    return true;
}

// 06f25130.
int FObjListCancelObjective::GetType()
{
    return 0x12;
}

// ---------------------------------------------------------------------------------------
// 0x13 Dock, 0x14 InstantTradelane, 0x15 Launch
// ---------------------------------------------------------------------------------------

// Inlined in factory case 0x13.
FObjListDock::FObjListDock()
{
    ClearName(&ObjectName);
}

// FUN_06f27660. A name that does not resolve finishes the directive.
bool FObjListDock::Execute(FContentShipBase* Ship)
{
    if (ObjectName.Length != 0)
    {
        unsigned int Id = FindTargetId(Ship, &ObjectName);
        Op.TetherObjectId = static_cast<int32_t>(Id);
        if ((Id & GIdValueMask) == 0)
        {
            Ship->AdvanceDirective();
            return true;
        }
    }
    DefaultPriority(&Op, Ship);
    return Ship->SubmitDirective(&Op);
}

// LAB_06f27710.
void FObjListDock::Parse(INI_Reader* Reader)
{
    Op.DockObjectId = pub::GetNicknameId(Reader->get_value_string(0));
    if (Reader->is_value_empty(1))
    {
        Op.NextRingId = 0;
        return;
    }
    if (_stricmp(Reader->get_value_string(1), "NONE") == 0)
        Op.NextRingId = 0;
    else
        Op.NextRingId = static_cast<int32_t>(pub::GetNicknameId(Reader->get_value_string(1)));
    if (Reader->is_value_empty(2))
        return;
    ObjectName = Reader->get_value_string(2);
    Op.TetherNearRange = static_cast<float>(Reader->get_value_int(3));
    Op.TetherFarRange = static_cast<float>(Reader->get_value_int(4));
}

// 06f250a0.
int FObjListDock::GetType()
{
    return 0x13;
}

// FUN_06f276f0.
void FObjListDock::SetTarget(unsigned int const* DockObjectId, unsigned int const* NextRingId)
{
    Op.DockObjectId = *DockObjectId;
    Op.NextRingId = static_cast<int32_t>(*NextRingId);
}

// LAB_06f27830. Retail calls vtbl +4 and drops the result.
bool FObjListInstantTradelane::Execute(FContentShipBase* Ship)
{
    Ship->IsNpcShip();
    DefaultPriority(&Op, Ship);
    return Ship->SubmitDirective(&Op);
}

// LAB_06f278a0.
void FObjListInstantTradelane::Parse(INI_Reader* Reader)
{
    Op.TradeLaneId = pub::GetNicknameId(Reader->get_value_string(0));
    if (Reader->is_value_empty(1))
    {
        Op.ExitRingId = 0;
        return;
    }
    Op.ExitRingId = static_cast<int32_t>(pub::GetNicknameId(Reader->get_value_string(1)));
}

// 06f250a0 (Dock's body, so InstantTradelane reports 0x13).
int FObjListInstantTradelane::GetType()
{
    return 0x13;
}

// FUN_06f27870.
void FObjListInstantTradelane::SetLane(unsigned int const* TradeLaneId, unsigned int OffsetBits,
                                       unsigned int const* NextRingId)
{
    Op.TradeLaneId = *TradeLaneId;
    Op.ExitRingId = static_cast<int32_t>(*NextRingId);
    Op.LaneSpeed = static_cast<int32_t>(OffsetBits);
}

// LAB_06f27970. Retail calls vtbl +4 and drops the result. Without a launch object (or dock
// index -1) GetShipDockingTarget (FUN_06f6eca0) picks one. SetDockEquipment = FUN_06f6dff0.
bool FObjListLaunch::Execute(FContentShipBase* Ship)
{
    Ship->IsNpcShip();
    if (Op.DockPortIndex == -1 || Op.LaunchObjectId == 0)
    {
        unsigned int ShipId = Ship->SpaceObjId;
        unsigned int TerminalType = static_cast<unsigned int>(Op.TerminalType);
        if (!GetShipDockingTarget(&ShipId, &Op.LaunchObjectId, &Op.DockPortIndex, &TerminalType))
            return false;
        Op.TerminalType = static_cast<int32_t>(TerminalType);
    }
    DefaultPriority(&Op, Ship);
    SetDockEquipment(Ship->SpaceObjId, false, false, nullptr);
    return Ship->SubmitDirective(&Op);
}

// LAB_06f27a40. An unknown terminal type leaves TerminalType.
void FObjListLaunch::Parse(INI_Reader* Reader)
{
    Op.LaunchObjectId = pub::GetNicknameId(Reader->get_value_string(0));
    Op.DockPortIndex = Reader->get_value_int(1);
    char const* Kind = Reader->get_value_string(2);
    if (_stricmp(Kind, "NONE") == 0)
        Op.TerminalType = TERM_NONE;
    else if (_stricmp(Kind, "BERTH") == 0)
        Op.TerminalType = TERM_BERTH;
    else if (_stricmp(Kind, "PAD") == 0)
        Op.TerminalType = TERM_PAD;
    else if (_stricmp(Kind, "MOOR_SMALL") == 0)
        Op.TerminalType = TERM_MOOR_SMALL;
    else if (_stricmp(Kind, "MOOR_MEDIUM") == 0)
        Op.TerminalType = TERM_MOOR_MEDIUM;
    else if (_stricmp(Kind, "MOOR_LARGE") == 0)
        Op.TerminalType = TERM_MOOR_LARGE;
    else if (_stricmp(Kind, "RING") == 0)
        Op.TerminalType = TERM_RING;
    else if (_stricmp(Kind, "JUMP_GATE") == 0)
        Op.TerminalType = TERM_JUMP;
}

// 06f25090.
int FObjListLaunch::GetType()
{
    return 0x15;
}

// FUN_06f27a10.
void FObjListLaunch::SetLaunch(unsigned int const* LaunchObjectId, int DockIndex, int TerminalType)
{
    Op.LaunchObjectId = *LaunchObjectId;
    Op.DockPortIndex = DockIndex;
    Op.TerminalType = TerminalType;
}

// ---------------------------------------------------------------------------------------
// 0x16 WaitForPlayerManeuver, 0x19 TractorBeam
// ---------------------------------------------------------------------------------------

// FUN_06f25140.
FObjListWaitForPlayerManeuver::FObjListWaitForPlayerManeuver()
{
    BehaviorName[0] = '\0';
}

// LAB_06f27b80. Retail calls vtbl +4 and drops the result. No player: the directive stays
// current. A player in formation behaviour (10) in the ship's formation (+0x154) finishes it;
// the player state (FUN_06f49bc0) is not null-checked.
bool FObjListWaitForPlayerManeuver::Execute(FContentShipBase* Ship)
{
    Ship->IsNpcShip();
    TString<0x30> PlayerName;
    PlayerName = "Player";
    unsigned int PlayerId = FindTargetId(Ship, &PlayerName);
    if (PlayerId == 0)
        return true;
    if (pub::AI::get_behavior_id(PlayerId) == BEHAVIOR_FORMATION)
    {
        FContentPlayerShip* PlayerShip = FindContentPlayerStateByShip(&PlayerId)->ShipObject;
        if (PlayerShip != nullptr && Ship->FormationId == PlayerShip->FormationId)
        {
            Ship->AdvanceDirective();
            return true;
        }
    }
    DefaultPriority(&Op, Ship);
    return Ship->SubmitDirective(&Op);
}

// FUN_06f27c80. The strcpy is unbounded, as retail.
void FObjListWaitForPlayerManeuver::Parse(INI_Reader* Reader)
{
    strcpy(BehaviorName, Reader->get_value_string(0));
    Op.BehaviorName = BehaviorName;
}

// 06f251a0.
int FObjListWaitForPlayerManeuver::GetType()
{
    return 0x16;
}

// LAB_06f28dd0: no priority default here.
bool FObjListTractorBeam::Execute(FContentShipBase* Ship)
{
    return Ship->SubmitDirective(&Op);
}

// 06eea870.
int FObjListTractorBeam::GetType()
{
    return 0x19;
}

// FUN_06f28df0.
void FObjListTractorBeam::SetTarget(unsigned int TargetId, bool Field10)
{
    Op.TargetId = TargetId;
    Op.Field_10 = Field10 ? 1 : 0;
}
