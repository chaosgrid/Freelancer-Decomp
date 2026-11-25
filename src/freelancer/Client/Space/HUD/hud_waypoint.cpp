// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// HUD_Waypoint -- see hud_waypoint.h for the class and its vftables.

#include "Space/HUD/hud_waypoint.h"

#include "NeuroNet/Economy/best_path.h"
#include "Interface/Pages/character_select_equip.h" // GetPlayerStarSystemIdRetail (FUN_004c4830)
#include "Space/client_gameobj.h"         // FClientGameobj, FClientSolarGameobj (FUN_005475b0)
#include "Space/client_objdb.h"           // ClientObjDb_RegisterGameobj (FUN_00540c30)
#include "Core/game_options.h"           // GetGameOption (FUN_005792b0)
#include "Space/HUD/hud_command_dispatch.h"   // FSpaceCommand, SpaceSendCommand (FUN_0054bcb0)
#include "Common/input_manager.h"          // GetClientMouseCursorDrawFlag (FUN_0041dd90)
#include "NeuroNet/nn_messaging.h"           // NnVoiceEnqueue (FUN_00489e80)
#include "Space/player_behavior.h"        // GetPlayerObjRW (FUN_0054baf0)
#include "Space/player_spawn.h"           // the player waypoint list, GetPlayerConnectionId
#include "Space/star_sphere.h"            // Win3DIdentityRotation3x3 (FUN_004215b0)

#include "common_archetype.h"
#include "common_cobject.h"
#include "common_iface.h"
#include "common_id.h"              // CreateID
#include "common_misc.h"            // SUBOBJ_ID_NONE
#include "common_reputation.h"      // Reputation::Vibe
#include "common_text.h"            // FmtStr
#include "common_universe.h"        // Archetype::GetSolarID

#include "dacom.h"                  // FDUMP

#include <math.h>
#include <stdio.h>
#include <string.h>

static_assert(sizeof(HUD_Waypoint) == 0x390, "retail HUD_Waypoint is 0x390 bytes");

namespace
{
    // DAT_00674bc8.
    HUD_Waypoint* G_HudWaypoint = nullptr;

    // LAB_004f3aa0 (static initializer): DAT_00674bb4 = CreateID("objective_reached").
    unsigned const G_VoiceObjectiveReached = CreateID("objective_reached");

    // DAT_005d97b8 15.0f, DAT_005ca230 100.0f, DAT_005c99c8 0.01f, DAT_005d97b4 150.0f,
    // DAT_005c8a60 16.0f, DAT_005d7e80 300.0f, DAT_005d97b0 250.0f, DAT_005c77f4 pi / 180
    // (float), DAT_005ca228 2.0 (double).
    float const kScaleZeroDistance = 15.0f;
    float const kScaleFullDistance = 100.0f;
    float const kScalePerMeter = 0.01f;
    float const kReachedDistance = 150.0f;
    float const kBestPathMatchSq = 16.0f;
    float const kUserPickDistance = 300.0f;
    float const kSpinDegPerSecond = 250.0f;
    float const kDegToRad = 0.0174532924f;
    double const kSameSpotAxis = 2.0;

    // FUN_00489e80(Id, 0) -- the NN voice queue (nn_messaging.cpp).
    void QueueWaypointVoice(unsigned VoiceId)
    {
        NnVoiceEnqueue(VoiceId, 0);
    }

    IObjRW* WatchedObjRw(BaseWatcher const& Watcher)
    {
        return ObjRwFromWatchable(const_cast<Watchable*>(Watcher.target()));
    }

    // The client gameobj behind a watched IObjRW (retail calls its extension slots straight
    // through the IObjRW vftable).
    FClientGameobj* AsGameobj(IObjRW* Obj)
    {
        return static_cast<FClientGameobj*>(static_cast<IObjInspectImpl*>(Obj));
    }

    // The release sequence retail inlines three times: with a watched waypoint, its slot 87
    // (MarkForRemoval) with 1, then the watcher dropped.
    void ReleaseWaypointObject(BaseWatcher& Watcher)
    {
        IObjRW* const Obj = WatchedObjRw(Watcher);
        if (Obj == nullptr)
            return;
        AsGameobj(Obj)->MarkForRemoval(true);
        Watcher.set(nullptr);
    }

    // Command 0x1c (target) on object ObjectId, through FUN_0054bcb0.
    void SendTargetCommand(unsigned ObjectId)
    {
        FSpaceCommand Command = {};
        Command.Id = USER_SET_TARGET;
        Command.ObjectId = ObjectId;
        Command.SubObjectId = SUBOBJ_ID_NONE;
        Command.Byte26 = 1;
        SpaceSendCommand(&Command);
    }

    // Retail's summation order: sqrt(dz*dz + dy*dy + dx*dx).
    double Distance(Vector const& A, Vector const& B)
    {
        double const Dx = static_cast<double>(A.x) - static_cast<double>(B.x);
        double const Dy = static_cast<double>(A.y) - static_cast<double>(B.y);
        double const Dz = static_cast<double>(A.z) - static_cast<double>(B.z);
        return sqrt(Dz * Dz + Dy * Dy + Dx * Dx);
    }

    // SetWaypoint's summation order: sqrt(dx*dx + dy*dy + dz*dz).
    double DistanceXFirst(Vector const& A, Vector const& B)
    {
        double const Dx = static_cast<double>(A.x) - static_cast<double>(B.x);
        double const Dy = static_cast<double>(A.y) - static_cast<double>(B.y);
        double const Dz = static_cast<double>(A.z) - static_cast<double>(B.z);
        return sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
    }
}

HUD_Waypoint* GetHudWaypoint()
{
    return G_HudWaypoint;
}

HUD_Waypoint::HUD_Waypoint(INI_Reader& /*Reader*/)
    : SpawnCounter(0)
    , SpinAngle(0.0f)
    , bUser(0)
    , Index(0)
    , Scale(1.0f)
    , bReached(0)
    , Id(-1)
    , Field388(0)
    , bActive(0)
{
    // FUN_004f3af0: Create's body is inlined here (Win3D::Create called directly);
    // SpawnCounter is written twice, as in retail. Index (+0x378) is not written in retail
    // (operator new memory); the port starts it at 0.
    G_HudWaypoint = this;
    HUDItem::Name = "Waypoint";
    SetOffset(0.0f, 0.0f, -1.0f);
    SetPosition(Position);
    Win3D::Create(nullptr, nullptr);
    SpawnCounter = 0;
    SpinAngle = 0.0f;
    Win3D::SetName("HUD_Waypoint");
    SpawnCounter = -1;
    Id = -1;
    ClassName = "HUD_Waypoint";
}

HUD_Waypoint::~HUD_Waypoint()
{
    // FUN_004f3cd0: then the member and base destructors.
    G_HudWaypoint = nullptr;
}

bool HUD_Waypoint::Create(Win3D* NewParent, void* Data)
{
    // LAB_004f3d70 (this = the WinControl face).
    SetOffset(0.0f, 0.0f, -1.0f);
    SetPosition(Position);
    return Win3D::Create(NewParent, Data);
}

void HUD_Waypoint::Update()
{
    // LAB_004f4c50 (this = the WinControl face).
    if (GetClientMouseCursorDrawFlag() != 0
        && GetGameOption(kGameOptInterfaceGroup) != 0
        && GetGameOption(kGameOptHud) != 0
        && GetPlayerObjRW() != nullptr)
    {
        SetHiddenWithChildren(false);
        Win3D::Update();
        return;
    }
    SetHiddenWithChildren(true);
}

void HUD_Waypoint::UpdateInactive()
{
    // FUN_004d6140 (this = the WinControl face; the body HUD_CruiseProgress shares).
    if (GetClientMouseCursorDrawFlag() != 0 && GetPlayerObjRW() != nullptr)
    {
        SetHiddenWithChildren(false);
    }
    Win3D::UpdateInactive();
}

void HUD_Waypoint::Render(FSpaceCamera* /*Camera*/, float DeltaSeconds)
{
    // LAB_004f3de0.
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    if (Player == nullptr)
        return;
    Scale = 1.0f;

    if (WatchedObjRw(Waypoint) != nullptr && bActive != 0)
    {
        // The player must still target the waypoint Solar.
        IObjRW* Target = nullptr;
        Player->get_target(Target);
        if (Target == nullptr)
        {
            bActive = 0;
        }
        else if (Target->get_id() != WatchedObjRw(Waypoint)->get_id())
        {
            bActive = 0;
        }
        else
        {
            bool bFound = false;
            if (bUser != 0)
            {
                // The entry with our id is still in the player's list.
                int const Count = GetPlayerWaypointCountRetail();
                for (int i = 0; i < Count; ++i)
                {
                    if (GetPlayerWaypointRetail(i)->AutoId == Id)   // no null test
                    {
                        bFound = true;
                        break;
                    }
                }
            }
            else
            {
                // A best-path record in the player's system lies within 4 m (squared 16) of the
                // waypoint Solar.
                int const Count = BestPathGetWaypointCount();
                Vector const WaypointPos = WatchedObjRw(Waypoint)->get_position();
                for (int i = 0; i < Count; ++i)
                {
                    FBestPathWaypoint* const Record = BestPathGetWaypoint(i);
                    Vector RecordPos;
                    unsigned RecordSystem = 0;
                    if (!BestPathWaypointResolvePosition(Record, &RecordPos, &RecordSystem))
                        continue;
                    if (GetPlayerStarSystemIdRetail() != RecordSystem)
                        continue;
                    // Float differences (FUN_00422b80), then dy^2 + dz^2 + dx^2 strictly below
                    // 16.
                    float const Dx = RecordPos.x - WaypointPos.x;
                    float const Dy = RecordPos.y - WaypointPos.y;
                    float const Dz = RecordPos.z - WaypointPos.z;
                    double const Sq = static_cast<double>(Dy) * Dy + static_cast<double>(Dz) * Dz +
                                      static_cast<double>(Dx) * Dx;
                    if (Sq < kBestPathMatchSq)
                    {
                        bFound = true;
                        break;
                    }
                }
            }

            if (!bFound)
            {
                // Drop the waypoint; a best-path one moves on to the route's first point.
                ReleaseWaypointObject(Waypoint);
                if (bUser != 0)
                    return;
                HudWaypointSelectBestPath(0);
                return;
            }

            // The distance from the waypoint to the player sets the scale -- 0 inside 15 m,
            // dist * 0.01 below 100 m (NaN keeps 1).
            double const Dist = Distance(Player->get_position(), Target->get_position());
            if (Dist < kScaleZeroDistance)
            {
                Scale = 0.0f;
            }
            else if (Dist < kScaleFullDistance)
            {
                Scale = static_cast<float>(static_cast<double>(kScalePerMeter) * Dist);
            }

            // First time within 150 m -- reached.
            if (bReached == 0 && Dist < kReachedDistance)
            {
                bReached = 1;
                QueueWaypointVoice(G_VoiceObjectiveReached);
                if (bUser != 0)
                {
                    // Index + 1 removals of entry Index, i.e. this entry and every one before it.
                    if (Index >= 0)
                    {
                        for (int Left = Index + 1; Left != 0; --Left)
                            RemovePlayerWaypointRetail(Index);
                    }
                    ReleaseWaypointObject(Waypoint);
                    // The entry now at Index becomes the waypoint.
                    FPlayerWaypointRecord* const Next = GetPlayerWaypointRetail(Index);
                    if (Next != nullptr)
                        SetWaypoint(&Next->Pos, true, Index, Next->AutoId);
                }
            }
        }
    }
    else
    {
        // No live waypoint -- the first player waypoint within 300 m counts as reached, and it
        // and every entry before it are removed.
        int const Count = GetPlayerWaypointCountRetail();
        for (int i = 0; i < Count; ++i)
        {
            FPlayerWaypointRecord* const Record = GetPlayerWaypointRetail(i);
            if (Record == nullptr)
                continue;
            if (Distance(Player->get_position(), Record->Pos) < kUserPickDistance)
            {
                QueueWaypointVoice(G_VoiceObjectiveReached);
                for (int Remove = i; Remove >= 0; --Remove)
                    RemovePlayerWaypointRetail(Remove);
                break;
            }
        }
    }

    // Spin the waypoint Solar about its Y axis.
    if (WatchedObjRw(Waypoint) == nullptr)
        return;
    // The sum stays unrounded for the radians; the cosine takes the unrounded angle, the sine
    // the angle rounded to float.
    double const Sum = static_cast<double>(DeltaSeconds) * kSpinDegPerSecond +
                       static_cast<double>(SpinAngle);
    SpinAngle = static_cast<float>(Sum);
    double const Radians = Sum * static_cast<double>(kDegToRad);
    float const RadiansF = static_cast<float>(Radians);
    float const Cos = static_cast<float>(cos(Radians));
    float const Sin = static_cast<float>(sin(static_cast<double>(RadiansF)));
    float Rotation[9] = {Cos, 0.0f, Sin, 0.0f, 1.0f, 0.0f, -Sin, 0.0f, Cos};
    HudMatrixRotateRows12(Rotation, 0.0f);
    HudMatrixRotateRows01(Rotation, 0.0f);

    // Into the CObject's orientation when it is a CSimple.
    IObjRW* const Obj = WatchedObjRw(Waypoint);
    if (Obj == nullptr)
        return;
    CObject* const Object = static_cast<CBase*>(static_cast<IObjInspectImpl*>(Obj))->Object;
    if (Object == nullptr)
        return;
    if ((static_cast<unsigned>(Object->ObjectClass) & CObject::ClassSimple) != CObject::ClassSimple)
        return;
    memcpy(&Object->Orientation, Rotation, sizeof(Rotation));
}

void HUD_Waypoint::SetWaypoint(Vector const* Pos, bool bNewUser, int NewIndex, int NewId)
{
    // FUN_004f42e0.
    //
    // The same entry, and its Solar strictly within 2 m on every axis -- just target it again.
    // dx stays unrounded, dy / dz are rounded to float.
    IObjRW* const Current = WatchedObjRw(Waypoint);
    if (Current != nullptr && static_cast<unsigned char>(bNewUser) == bUser && NewIndex == Index &&
        NewId == Id)
    {
        Vector const WaypointPos = Current->get_position();
        double const Dx = static_cast<double>(WaypointPos.x) - static_cast<double>(Pos->x);
        float const Dy = WaypointPos.y - Pos->y;
        float const Dz = WaypointPos.z - Pos->z;
        if (fabs(Dx) < kSameSpotAxis && fabs(static_cast<double>(Dy)) < kSameSpotAxis &&
            fabs(static_cast<double>(Dz)) < kSameSpotAxis)
        {
            bActive = 1;
            SendTargetCommand(WatchedObjRw(Waypoint)->get_id());
            return;
        }
    }

    // Drop the old Solar, remember the entry.
    ReleaseWaypointObject(Waypoint);
    bUser = static_cast<unsigned char>(bNewUser);
    Index = NewIndex;
    Id = NewId;

    // The "waypoint" solar archetype, loaded, and its companion's Load(0). Retail tests
    // neither for null.
    ID_String ArchName;
    ArchName.Id = CreateID("waypoint");
    Archetype::Solar* const Arch = Archetype::GetSolar(Archetype::GetSolarID(ArchName));
    Arch->load();
    Arch->Companion->Load(0);

    // CSolar::CreateParms on the stack: the CEqObj part through FUN_004f4930, then the CSolar
    // part cleared.
    CSolar::CreateParms Parms;
    EqObjCreateParmsDefaults(Parms);
    Parms.NicknameSrc.clear();
    Parms.VisitValue = 0;
    Parms.ParentNicknameId = 0;                                     // ID_String::clear
    Parms.bDestructible = 0;
    Parms.bDynamic = 0;
    Parms.DestSystem = 0;
    Parms.DestGateId = 0;                                           // ID_String::clear
    Parms.PrevRing = 0;
    Parms.NextRing = 0;
    Parms.TradelaneSpaceNameId = 0;
    Parms.AtmosphereRange = 0.0f;
    Parms.DockWithBaseId = 0;

    // Nickname "waypoint_target<old counter>", the counter incremented; the id goes into Id,
    // the nickname and the reputation group, which gets an entry and affiliation -1.
    char Nickname[0x80];
    int const Suffix = SpawnCounter;
    // PTR_DAT_00613ed0 -> CreateParms+0x04.
    Parms.ObjDb = GetClientObjDb();
    SpawnCounter = Suffix + 1;
    sprintf(Nickname, "waypoint_target%d", Suffix);
    unsigned const NewObjectId = CreateID(Nickname);
    Parms.NicknameSrc.Id = NewObjectId;
    Parms.Id = NewObjectId;
    Parms.VibeId = NewObjectId;
    int const& RepGroup = reinterpret_cast<int const&>(Parms.VibeId);
    Reputation::Vibe::Alloc(RepGroup);
    Reputation::Vibe::SetAffiliation(RepGroup, 0xffffffffu, false);

    // The name is mad-lib ids 0x442 with no values, the second name empty; no infocard.
    FmtStr Name;
    FmtStr Name2;
    Name.begin_mad_lib(0x442);
    Name.end_mad_lib();
    Reputation::Vibe::SetName(RepGroup, Name, Name2, nullptr);
    Reputation::Vibe::SetInfocard(RepGroup, 0);

    Parms.Pos = *Pos;

    // A client Solar (FUN_005475b0), created, CSolar::init on its CObject when that is a solar
    // (retail calls init on whatever that yields, null included), then registered.
    FClientSolarGameobj* const Solar = new FClientSolarGameobj();
    Solar->create(Arch);
    CObject* const SolarObject = static_cast<CBase*>(Solar)->Object;
    CSolar* SolarCObject = nullptr;
    if (SolarObject != nullptr && (static_cast<unsigned>(SolarObject->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar)
        SolarCObject = static_cast<CSolar*>(SolarObject);
    SolarCObject->init(Parms);
    Solar->PostCreateInit();
    ClientObjDb_RegisterGameobj(Solar, false);

    Arch->Release();
    SolarCObject->enable_collisions_r(false);

    SendTargetCommand(Solar->get_id());

    // Watch it; live, not reached yet.
    Waypoint.set(static_cast<Watchable const*>(static_cast<IObjRW*>(Solar)));
    bActive = 1;
    bReached = 0;

    // Already within 100 m (at most 100) counts as reached.
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    if (Player != nullptr)
    {
        Vector const& WaypointPos = WatchedObjRw(Waypoint)->get_position();
        if (DistanceXFirst(Player->get_position(), WaypointPos) <= kScaleFullDistance)
            bReached = 1;
    }
}

bool HUD_Waypoint::GetWaypointEntry(unsigned char* OutUser, int* OutIndex)
{
    // FUN_004f42a0.
    if (WatchedObjRw(Waypoint) == nullptr || bActive == 0)
        return false;
    *OutUser = bUser;
    *OutIndex = Index;
    return true;
}

bool HUD_Waypoint::IsWaypointAt(Vector const* Pos)
{
    // FUN_004f4b90: SetWaypoint's same-spot test.
    if (WatchedObjRw(Waypoint) == nullptr || bActive == 0)
        return false;
    Vector const WaypointPos = WatchedObjRw(Waypoint)->get_position();
    double const Dx = static_cast<double>(WaypointPos.x) - static_cast<double>(Pos->x);
    float const Dy = WaypointPos.y - Pos->y;
    float const Dz = WaypointPos.z - Pos->z;
    if (!(fabs(Dx) < kSameSpotAxis))
        return false;
    if (!(fabs(static_cast<double>(Dy)) < kSameSpotAxis))
        return false;
    if (!(fabs(static_cast<double>(Dz)) < kSameSpotAxis))
        return false;
    return true;
}

float HUD_Waypoint::GetScale() const
{
    // FUN_004f4ca0.
    return Scale;
}

IObjRW* HUD_Waypoint::GetWaypointObject() const
{
    // FUN_004f4cb0.
    return WatchedObjRw(Waypoint);
}

bool HUD_Waypoint::IsActive() const
{
    // FUN_004f4d20.
    return bActive != 0;
}

void HudWaypointOnPlayerWaypointsCleared()
{
    // FUN_004f4cd0.
    if (G_HudWaypoint == nullptr)
        return;
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    if (Player == nullptr)
        return;
    if (WatchedObjRw(G_HudWaypoint->Waypoint) == nullptr)
        return;
    if (G_HudWaypoint->bActive == 0)
        return;
    static_cast<IObjAffect*>(Player)->set_target(nullptr, SUBOBJ_ID_NONE,
                                                 static_cast<int>(GetPlayerConnectionId()));
}

int HudWaypointGetObjectCount()
{
    // FUN_004c83c0 (no null test of DAT_00674bc8).
    return G_HudWaypoint->GetWaypointObject() != nullptr ? 1 : 0;
}

IObjRW* HudWaypointGetObject(int ObjectIndex)
{
    // FUN_004c83e0.
    if (ObjectIndex != 0)
        return nullptr;
    return G_HudWaypoint->GetWaypointObject();
}

void EqObjCreateParmsDefaults(CEqObj::CreateParms& Parms)
{
    // FUN_004f4930, in retail's store order.
    Parms.HitPts = -1.0f;                                           // +0xc
    Parms.Id = 0;                                                   // +0x0
    Parms.ObjDb = nullptr;                                          // +0x4
    Parms.OwnerPlayer = 0;                                          // +0x8
    Parms.Pos.x = 0.0f;                                             // +0x10..+0x18
    Parms.Pos.y = 0.0f;
    Parms.Pos.z = 0.0f;
    Parms.Velocity.x = 0.0f;                                        // +0x40..+0x54
    Parms.Velocity.y = 0.0f;
    Parms.Velocity.z = 0.0f;
    Parms.AngularVelocity.x = 0.0f;
    Parms.AngularVelocity.y = 0.0f;
    Parms.AngularVelocity.z = 0.0f;
    Parms.Pad58 = 0;                                                // +0x58
    Win3DIdentityRotation3x3(reinterpret_cast<float*>(&Parms.Orient)); // FUN_004215b0 on +0x1c
    Parms.DockingBaseId = 0;                                        // +0x5c
    Parms.VibeId = 0xffffffffu;                                     // +0x60
    Parms.SpaceCostume.PartIds[0] = 0;                              // +0x64..+0x70
    Parms.SpaceCostume.PartIds[1] = 0;
    Parms.SpaceCostume.PartIds[2] = 0;
    Parms.SpaceCostume.PartIds[3] = 0;
    Parms.SpaceCostume.AccessoryCount = 0;                          // +0x94
    Parms.VoiceId = 0;                                              // +0x98
    // Equipment (+0x9c) and ArchGroupDescs (+0xac) are already empty from their std
    // constructors. +0x74..+0x90 (AccessoryIds) are not written.
}

void HudWaypointSelectBestPath(int PointIndex)
{
    // FUN_004cef30 (cdecl).
    if (PointIndex >= BestPathGetWaypointCount())                  // signed
        return;
    FBestPathWaypoint* const Record = BestPathGetWaypoint(PointIndex);

    // A record that names an object -- target it.
    IObjRW* const Obj = BestPathWaypointResolveObject(Record);
    if (Obj != nullptr)
    {
        SendTargetCommand(Obj->get_id());
        return;
    }

    // Otherwise its position becomes the waypoint (the system is not read; DAT_00674bc8 is not
    // tested).
    Vector Pos;
    unsigned System = 0;
    if (BestPathWaypointResolvePosition(Record, &Pos, &System))
        G_HudWaypoint->SetWaypoint(&Pos, false, PointIndex, PointIndex);
}

void HudWaypointSelectPlayerWaypoint(int Index)
{
    // FUN_004ceeb0 (cdecl).
    if (Index >= GetPlayerWaypointCountRetail())                   // signed
        return;
    FPlayerWaypointRecord* const Record = GetPlayerWaypointRetail(Index);

    // A waypoint on an object -- target it.
    if (Record->ObjectId != 0)
    {
        SendTargetCommand(Record->ObjectId);
        return;
    }

    // Otherwise its position becomes the waypoint (DAT_00674bc8 is not tested).
    G_HudWaypoint->SetWaypoint(&Record->Pos, true, Index, Record->AutoId);
}

namespace
{
    // DAT_00674b88; see the header for the factory's retail functions.
    HUDFACTORY<HUD_Waypoint> G_HudWaypointFactory("Waypoint");
}
