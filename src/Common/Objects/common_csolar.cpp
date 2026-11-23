// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CSolar (retail CSolar.cpp).
#include "common_cobject.h"
#include "Objects/common_cobject_internal.h" // CmnCobject::GetObjectDbCollisionGroup

#include "common_goods.h"
#include "common_id.h"
#include "common_physys.h"
#include "common_animdb.h"  // AnimDB::Add/Rem (common_animdb.cpp)
#include "common_universe.h"
#include "common_misc.h"    // SinglePlayer()

#include "DALib.h"
#include "x86math.h" // I3DMathEngine::TransformMatrix (get_angular_velocity)

#include <list>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using CmnCobject::GetObjectDbCollisionGroup;

ID_String const& CSolar::get_loadout() const
{
    // FUN_0629ad10 (+0x1b0)
    return Loadout;
}

// FUN_062b7320. ContinualAnims (+0x1cc) default-constructs.
CSolar::CSolar(Class ObjectClass)
    : CEqObj(ObjectClass), bDestructible(false), bDynamic(false), AtmosphereRange(0.0f),
      DestSystem(0), GateAnimDirection(0), GateAnimState(4), GateAnimTimer(0.0f),
      PrevRing(0), NextRing(0), TradelaneSpaceNameId(0), VisitValue(0)
{
    // Nickname +0x1a4, Loadout +0x1b0, DestGate +0x1b8, ParentNickname +0x1e8. GateAnim
    // starts at -1.
    Nickname.clear();
    Loadout.clear();
    DestGate.clear();
    ParentNickname.clear();
}

// FUN_0629a860. No callers (exported only). Deviation: retail inlines the CEqObj copy; the
// port delegates to it. GateAnim's handle is copied raw, as in retail.
CSolar::CSolar(CSolar const& Other)
    : CEqObj(Other), Nickname(Other.Nickname), bDestructible(Other.bDestructible),
      bDynamic(Other.bDynamic),
      AtmosphereRange(Other.AtmosphereRange), Loadout(Other.Loadout), DestSystem(Other.DestSystem),
      DestGate(Other.DestGate), GateAnimDirection(Other.GateAnimDirection),
      GateAnim(Other.GateAnim), GateAnimState(Other.GateAnimState),
      GateAnimTimer(Other.GateAnimTimer), ContinualAnims(Other.ContinualAnims),
      PrevRing(Other.PrevRing), NextRing(Other.NextRing),
      TradelaneSpaceNameId(Other.TradelaneSpaceNameId), VisitValue(Other.VisitValue),
      ParentNickname(Other.ParentNickname)
{
}

// FUN_062b7420: flush_animations inlined and the vibe release; the FAnimScriptRef member
// destructors then stop and destroy the scripts.
CSolar::~CSolar()
{
    for (std::list<FAnimScriptRef>::const_iterator It = ContinualAnims.begin();
         It != ContinualAnims.end(); ++It)
    {
        if (It->Index != -1)
            AnimDB::Rem(It->Index);
    }
    AnimDB::Rem(GateAnim.Index);
    CEqObj::flush_animations();

    // Waypoints (TypeBits & 0x200) own their vibe (+0x104).
    if ((TypeBits & 0x200) != 0)
    {
        int const Vibe = (int)VibeId;
        Reputation::Vibe::Free(Vibe);
    }
}

void CSolar::init(CreateParms const& Parms)
{
    // FUN_062b7560. The solar fields live in CreateParms +0xb8..+0xe8.
    CEqObj::init(Parms);

    Nickname.Id = Parms.NicknameSrc.Id;

    bDestructible = Parms.bDestructible != 0;
    bDynamic = Parms.bDynamic != 0;

    // Static solars start with weapons offline outside single-player.
    if (!SinglePlayer() && !bDynamic)
        bWeaponsOffline = true;

    DestSystem = Parms.DestSystem;
    DestGate.Id = Parms.DestGateId;
    PrevRing = Parms.PrevRing;
    NextRing = Parms.NextRing;
    TradelaneSpaceNameId = Parms.TradelaneSpaceNameId;
    AtmosphereRange = Parms.AtmosphereRange;
    VisitValue = Parms.VisitValue;
    ParentNickname.Id = Parms.ParentNicknameId;

    // The gate's open animation (Archetype::Solar +0xa0; only its first char is tested).
    if (DestSystem != 0)
    {
        Archetype::Solar* const SolarArch = (Archetype::Solar*)Archetype;
        char const* const OpenAnimName = SolarArch->OpenAnim.Value;
        if (OpenAnimName[0] != '\0')
        {
            GateAnim.Index = DALib::Anim->PlayScript(AsOpaqueDword(Archetype->get_script_index()),
                                                     AsOpaqueDword(this->EngineInstanceId),
                                                     OpenAnimName, nullptr, nullptr);
            if (GateAnim.Index == -1)
            {
                FDUMP(0x100001,
                      "%s(%d) : *** ERROR: failed to find animation (%s) in object (%s)\n",
                      "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0xe3, OpenAnimName,
                      Archetype->Nickname.get_string());
            }
        }
    }

    DockingBaseId = Parms.DockingCount;                            // +0x160

    unsigned const RawTypeBits = Parms.DockWithBaseId;
    DockingPointBaseId = RawTypeBits;                              // +0x164

    // Diagnostic only.
    if ((TypeBits & 0x600d60) != 0 && (TypeBits & 2) == 0 &&
        (RawTypeBits & 0x3fffffff) == 0 && (DestSystem & 0x3fffffff) == 0)
    {
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: %s is dockable, but doesn't have a valid base or system to go to ",
              "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0x6d,
              Parms.NicknameSrc.get_string());
    }

    if ((TypeBits & 0x84) != 0)
        VisitValue = 1;

    init_docking_points(RawTypeBits);

    // Parms +0xe4 list.
    for (std::list<char const*>::const_iterator It = Parms.ContinualAnims.begin();
         It != Parms.ContinualAnims.end(); ++It)
        init_continual_anim(*It);

    CSimple::update_zones(0.0f, 0);
}

void CSolar::init_continual_anim(char const* AnimName)
{
    // FUN_062b7d90. A -1 entry is pushed at the front and then filled.
    {
        FAnimScriptRef Fresh;
        ContinualAnims.insert(ContinualAnims.begin(), Fresh);
    }

    FAnimScriptRef& Front = ContinualAnims.front();
    Front.Index = DALib::Anim->PlayScript(AsOpaqueDword(Archetype->get_script_index()),
                                          AsOpaqueDword(this->EngineInstanceId), AnimName,
                                          nullptr, nullptr);
    if (Front.Index == -1)
    {
        ContinualAnims.pop_front();
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: failed to find animation (%s) in object (%s)\n",
              "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0x194, AnimName,
              Archetype->Nickname.get_string());
        return;
    }

    // Mode 2 (loop); the raw 0x3f800000 dwords are 1.0f.
    DALib::Anim->EvalPlayback(Front.Index, 2, -2.0f, 1.0f, nullptr,
                              AsOpaqueDword(0x3f800000),
                              AsOpaqueDword(0x3f800000), nullptr);
    AnimDB::Add(Front.Index);
}

void CSolar::flush_animations()
{
    // FUN_062b7fa0. GateAnim is removed even when -1; the list keeps its nodes.
    for (std::list<FAnimScriptRef>::const_iterator It = ContinualAnims.begin();
         It != ContinualAnims.end(); ++It)
    {
        if (It->Index != -1)
            AnimDB::Rem(It->Index);
    }

    AnimDB::Rem(GateAnim.Index);
    CEqObj::flush_animations();
}

void CSolar::cache_physical_props()
{
    // FUN_062b7930. Unlike CSimple, children are included (compound solars).
    if (BodyPtr != nullptr)
    {
        CachedRadius = PhySys::GetRadiusR(this, true, CachedCenterOfMass);
        return;
    }

    FDUMP(0x100001, "%s(%d) : *** ERROR: %s", "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 200,
          "Missing ivp_real in CSimple::cache_physical_props()");
    CSimple::cache_physical_props();
}

float CSolar::get_mass() const
{
    // FUN_0629ad00. Never consults the IVP body; no null test on the archetype.
    return Archetype->Mass;
}

Vector CSolar::get_velocity() const
{
    // FUN_063623d0, ICF-folded with CAsteroid::get_velocity.
    Vector Zero;
    Zero.x = 0.0f;
    Zero.y = 0.0f;
    Zero.z = 0.0f;
    return Zero;
}

Archetype::Solar const* CSolar::solararch() const
{
    // FUN_06262d50 (ICF fold)
    return (Archetype::Solar const*)Archetype;
}

Vector CSolar::get_angular_velocity() const
{
    // FUN_0629ac70. Only a planetary solar with a body spins; rotated to world as in CObject.
    if ((TypeBits & 7) != 0 && BodyPtr != nullptr)
    {
        Vector const ObjSpace = PhySys::GetAngularVelocityOS(this);
        FVector World;
        Acquire3DMathEngine()->TransformMatrix(World, (FMatrix const&)Orientation,
                                               (FVector const&)ObjSpace);
        return Vector{World.X, World.Y, World.Z};
    }
    return Vector{0.0f, 0.0f, 0.0f};
}

bool CSolar::is_planetary() const
{
    // FUN_0629a7f0 (TypeBits +0xe0)
    return (TypeBits & 7) != 0;
}

bool CSolar::is_system_gate() const
{
    // FUN_0629a800
    return TypeBits == (TypeBits & 0xc40);
}

bool CSolar::is_waypoint() const
{
    // FUN_0629a820
    return ((TypeBits >> 9) & 1) != 0;
}

bool CSolar::is_lane_ring() const
{
    // FUN_0629a830
    return ((TypeBits >> 7) & 1) != 0;
}

bool CSolar::is_destructible() const
{
    // FUN_0629a840 (+0x1a8)
    return bDestructible;
}

bool CSolar::is_dynamic() const
{
    // FUN_0629a850
    return bDynamic;
}

float CSolar::get_atmosphere_range() const
{
    // FUN_0629ad40
    return AtmosphereRange;
}

unsigned CSolar::get_prev_trade_ring() const
{
    // FUN_0629ad50
    return PrevRing;
}

unsigned CSolar::get_next_trade_ring() const
{
    // FUN_0629ad60
    return NextRing;
}

ID_String const& CSolar::GetParentNickname() const
{
    // FUN_062b4f00 (+0x1e8)
    return ParentNickname;
}

unsigned CSolar::get_dest_system() const
{
    // FUN_0629ad20
    return DestSystem;
}

ID_String const& CSolar::get_dest_gate() const
{
    // FUN_0629ad30
    return DestGate;
}

unsigned char CSolar::GetVisitValue() const
{
    return VisitValue;
}

void CSolar::SetVisitValue(unsigned char NewValue)
{
    // FUN_062b8a60
    VisitValue = NewValue;
}

unsigned int CSolar::get_tradelane_space_name() const
{
    // FUN_062b8a70. +0x1e0, a separate field from ParentNickname (+0x1e8).
    return TradelaneSpaceNameId;
}

void CSolar::init_physics(Vector const& Velocity, Vector const& AngularVelocity)
{
    // FUN_062b7780. Velocity is never read.
    (void)Velocity;

    // Retail's seeded defaults are all overwritten below or zero; the port zeroes the bytes
    // retail leaves uninitialised. Name is radix 24.
    PhySys::CreateParms Phy;
    memset(&Phy, 0, sizeof(Phy));
    Phy.bEnableCollisions = 1;
    _itoa((int)Id, Phy.Name, 0x18);

    // Zero, negative or unordered spin makes the solar static.
    double const SpinSq =
        ((double)AngularVelocity.x * AngularVelocity.x +
         (double)AngularVelocity.y * AngularVelocity.y) +
        (double)AngularVelocity.z * AngularVelocity.z;
    if (!(SpinSq > 0.0))
    {
        Phy.bFlag1c = 1;
        Phy.LinearDamping = 1.0e6f;
        Phy.AngularDamping[0] = 1.0e6f;
        Phy.AngularDamping[1] = 1.0e6f;
        Phy.AngularDamping[2] = 1.0e6f;
    }
    else
    {
        Phy.bFlag1c = 0;
        Phy.LinearDamping = 1.0e12f;
        Phy.AngularDamping[0] = 0.0f;
        Phy.AngularDamping[1] = 0.0f;
        Phy.AngularDamping[2] = 0.0f;
    }

    // No null test on the archetype.
    Archetype::Solar const* const SolarArch = (Archetype::Solar const*)Archetype;
    Phy.Mass = Archetype->Mass;
    Phy.Type = 1;
    Phy.CollisionGroupId = GetObjectDbCollisionGroup(ObjectDb);

    // A .sph DaArchetype (+0x0c) gets a sphere of SolarRadius (+0x98) when that is positive.
    char Ext[256];
    _splitpath(SolarArch->DaArchetype.Value, nullptr, nullptr, nullptr, Ext);
    if (_stricmp(Ext, ".sph") == 0)
    {
        float const Radius = SolarArch->SolarRadius;
        if (Radius > 0.0f)
            PhySys::BuildIVP_Sphere(this, Radius, Phy);
    }
    else
    {
        PhySys::BuildIVP_Real(this, Phy);
    }

    if (Phy.bFlag1c == 0)
        PhySys::AddToAngularVelocityOS(this, AngularVelocity);
}

void CSolar::configure_system_gate(char const* AnimName)
{
    // FUN_062b7a00. The gate-animation start CSolar::init inlines. No null test on AnimName.
    if (AnimName[0] == '\0')
        return;

    GateAnim.Index = DALib::Anim->PlayScript(AsOpaqueDword(Archetype->get_script_index()),
                                             AsOpaqueDword(this->EngineInstanceId), AnimName,
                                             nullptr, nullptr);
    if (GateAnim.Index == -1)
    {
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: failed to find animation (%s) in object (%s)\n",
              "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0xe3, AnimName,
              Archetype->Nickname.get_string());
    }
}

int CSolar::update(float Dt, unsigned Flags)
{
    // FUN_062b7a70. Retail inlines is_system_gate's test.
    CEqObj::update(Dt, Flags);
    if (is_system_gate())
        update_system_gate(Dt);
    return 0;
}

void CSolar::update_system_gate(float Dt)
{
    // FUN_062b7ab0. Notifications go through the virtual notify (vtable +0xc8) as event 1 with
    // &Code.
    int const State = GateAnimState;

    // The timer runs up in state 0, down in state 2, and is zeroed otherwise.
    if (State == 0)
        GateAnimTimer = Dt + GateAnimTimer;
    else if (State == 2)
        GateAnimTimer = GateAnimTimer - Dt;
    else
        GateAnimTimer = 0.0f;

    int Code;
    if (GateAnimDirection != 0)
    {
        switch (State)
        {
        case 3:
        case 4:
            // Code 0 is sent before the state and timer reset.
            Code = 0;
            notify((INotify::Event)1, &Code);
            GateAnimState = 0;
            GateAnimTimer = 0.0f;
            return;

        case 0:
            // 10.0f is DAT_0639e884; past it (unordered not) falls into the case-2 code.
            if (!(GateAnimTimer > 10.0f))
            {
                if (GateAnim.Index == -1)
                    return;
                DALib::Anim->EvalPlayback(GateAnim.Index, 0, -2.0f, 1.0f, nullptr,
                                          AsOpaqueDword(0x3f800000),
                                          AsOpaqueDword(0x3f800000), nullptr);
                AnimDB::Add(GateAnim.Index);
                return;
            }
            [[fallthrough]];
        case 2:
            GateAnimState = 1;
            Code = 1;
            notify((INotify::Event)1, &Code);
            return;

        default:                                                   // case 1, above 4
            return;
        }
    }

    switch (State)
    {
    case 0:
    case 1:
        GateAnimState = 2;
        GateAnimTimer = 5.0f;
        return;

    case 2:
        // Unordered keeps waiting.
        if (!(GateAnimTimer <= 0.0f))
            return;
        if (GateAnim.Index != -1)
        {
            DALib::Anim->EvalPlayback(GateAnim.Index, 1, -2.0f, 1.0f, nullptr,
                                      AsOpaqueDword(0x3f800000),
                                      AsOpaqueDword(0x3f800000), nullptr);
            AnimDB::Add(GateAnim.Index);
        }
        // Code before state here; state before code in case 3.
        Code = 2;
        notify((INotify::Event)1, &Code);
        GateAnimState = 3;
        return;

    case 3:
        // IAnimation2 slot 17 (+0x44).
        if (DALib::Anim->FilterObjects((void*)GateAnim.Index) == 0)
            return;
        GateAnimState = 4;
        Code = 3;
        notify((INotify::Event)1, &Code);
        return;

    default:                                                       // above 3
        return;
    }
}

ObjActivateResult CSolar::activate(bool bOn, unsigned DockIndex)
{
    // FUN_062b7cc0
    if (is_system_gate())
    {
        if (DockIndex != 0)
        {
            FDUMP(0x100001, "%s(%d) : *** ERROR: %s", "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp",
                  0x15c, "UHH index not 0");
        }
        unsigned char const OldDirection = GateAnimDirection;
        GateAnimDirection = bOn ? 1 : 0;
        return (GateAnimDirection == OldDirection) ? ObjActivateNoChange : ObjActivateChanged;
    }
    // Lane rings always report a change.
    if (is_lane_ring())
        return ObjActivateChanged;
    return CEqObj::activate(bOn, DockIndex);
}

bool CSolar::get_activate_state(std::vector<bool>& OutStates)
{
    // FUN_062b7d50. Retail inlines vector<bool>::push_back (FUN_062bac30).
    if (is_system_gate())
    {
        OutStates.push_back(GateAnimDirection != 0);
        return GateAnimDirection != 0;
    }
    if (is_lane_ring())
        return false;
    return CEqObj::get_activate_state(OutStates);
}

CEquip* CSolar::alloc_equip(unsigned short EquipId, Archetype::Equipment* Arch, bool bMissionFlag)
{
    // FUN_062b7f00. Retail builds a CInternalEquip and stamps CETradeLaneEquip's vtable (the
    // inlined ctor). No null test on Arch.
    if (Arch->get_class_type() == Archetype::AClassTradeLane)
    {
        // operator new(0x20); a null block falls through to CEqObj::alloc_equip.
        CETradeLaneEquip* const Equip = new CETradeLaneEquip(
            this, EquipId, (Archetype::TradeLaneEquip const*)Arch, bMissionFlag);
        if (Equip != nullptr)
            return Equip;
    }
    return CEqObj::alloc_equip(EquipId, Arch, bMissionFlag);
}

namespace
{
    // The VC6 vector<EquipDesc> assignment ReadObj inlines (`Parms.Equipment = Map->Items`);
    // same three branches as EquipDescVector::operator=. Its self-assignment test never fires
    // here.
    void AssignEquipDescsFromLoadout(EquipDescVector& Dst, std::vector<EquipDesc> const& Src)
    {
        EquipDesc const* const SrcBegin = Src.data();
        EquipDesc const* const SrcEnd = SrcBegin + Src.size();
        unsigned const SrcCount = (unsigned)Src.size();
        unsigned const DstSize = (Dst.Begin != nullptr) ? (unsigned)(Dst.End - Dst.Begin) : 0;

        if (SrcCount <= DstSize)
        {
            EquipDesc* Out = Dst.Begin;
            for (EquipDesc const* In = SrcBegin; In != SrcEnd; ++In, ++Out)
                *Out = *In;
            Dst.End = Dst.Begin + SrcCount;
            return;
        }

        unsigned const DstCapacity = (Dst.Begin != nullptr) ? (unsigned)(Dst.Cap - Dst.Begin) : 0;
        if (SrcCount <= DstCapacity)
        {
            EquipDesc const* const SrcMid = SrcBegin + DstSize;
            EquipDesc* Out = Dst.Begin;
            for (EquipDesc const* In = SrcBegin; In != SrcMid; ++In, ++Out)
                *Out = *In;
            Out = Dst.End;
            for (EquipDesc const* In = SrcMid; In != SrcEnd; ++In, ++Out)
                new (Out) EquipDesc(*In);
            Dst.End = Dst.Begin + SrcCount;
            return;
        }

        ::operator delete(Dst.Begin);
        EquipDesc* const NewBuf = (EquipDesc*)::operator new(SrcCount * sizeof(EquipDesc));
        Dst.Begin = NewBuf;
        EquipDesc* Out = NewBuf;
        for (EquipDesc const* In = SrcBegin; In != SrcEnd; ++In, ++Out)
            new (Out) EquipDesc(*In);
        Dst.End = Out;
        Dst.Cap = Out;
    }
}

bool CSolar::ReadObj(INI_Reader& Reader, Archetype::Solar*& OutArch, CreateParms& Parms)
{
    // FUN_062b7ff0. Static INI loader for one [Object] section of a system file. Retail
    // snapshots the reader state into a local it never reads again.
    INI_Reader::State SavedState;
    Reader.get_state(SavedState);

    OutArch = nullptr;
    ID_String LoadoutId;
    LoadoutId.clear();
    bool bDestructibleSet = false;
    bool bAtmosphereSet = false;

    while (Reader.read_value())
    {
        if (Reader.is_value("archetype"))
        {
            ID_String ArchName;
            ArchName.Id = CreateID(Reader.get_value_string());
            // GetAsteroidID, as retail calls it (ICF-folded with GetSolarID).
            unsigned const ArchId = Archetype::GetAsteroidID(ArchName);
            OutArch = Archetype::GetSolar(ArchId);
            if (OutArch == nullptr)
            {
                char const* const NickStr = Parms.NicknameSrc.get_string();
                char const* const ArchStr = ArchName.get_string();
                FDUMP(0x100002,
                      "%s(%d) : *** WARNING: Archetype \"%s\" not found while reading Solar object \"%s\"\n",
                      "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0x1da, ArchStr, NickStr);
                return false;
            }

            // Parms +0xac: one full-health desc per collision group.
            OutArch->get_undamaged_collision_group_list(Parms.ArchGroupDescs);
        }
        else if (Reader.is_value("nickname"))
        {
            unsigned const NickId = CreateID(Reader.get_value_string());
            Parms.NicknameSrc.Id = NickId;
            Parms.Id = NickId;
        }
        else if (Reader.is_value("pos"))
        {
            Parms.Pos.x = Reader.get_value_float(0);
            Parms.Pos.y = Reader.get_value_float(1);
            Parms.Pos.z = Reader.get_value_float(2);
        }
        else if (Reader.is_value("rotate"))
        {
            Vector RotateDegrees;
            RotateDegrees.x = Reader.get_value_float(0);
            RotateDegrees.y = Reader.get_value_float(1);
            RotateDegrees.z = Reader.get_value_float(2);
            Parms.Orient = EulerMatrix(RotateDegrees);
        }
        else if (Reader.is_value("spin"))
        {
            Parms.AngularVelocity.x = Reader.get_value_float(0);
            Parms.AngularVelocity.y = Reader.get_value_float(1);
            Parms.AngularVelocity.z = Reader.get_value_float(2);
        }
        else if (Reader.is_value("visit"))
        {
            Parms.VisitValue = (unsigned char)Reader.get_value_int(0);
        }
        else if (Reader.is_value("parent"))
        {
            Parms.ParentNicknameId = CreateID(Reader.get_value_string());
        }
        else if (Reader.is_value("goto"))
        {
            unsigned const SystemId = Universe::get_system_id(Reader.get_value_string(0));
            Parms.DestSystem = SystemId;
            if ((SystemId & 0x3fffffff) == 0)
            {
                FDUMP(0x100001, "%s(%d) : *** ERROR: Jumpgate has invalid destination: %s\n",
                      "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0x218, Reader.get_value_string(0));
            }
            Parms.DestGateId = CreateID(Reader.get_value_string(1));
        }
        else if (Reader.is_value("base"))
        {
            char const* BaseName = Reader.get_value_string();
            unsigned const BaseId = Universe::get_base_id(BaseName);
            Parms.DockingCount = BaseId;                               // retail +0x5c
            if (BaseId == 0)
            {
                FDUMP(0x100001, "%s(%d) : *** ERROR: Could not find base %s for solar object.",
                      "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0x224, BaseName);
            }
        }
        else if (Reader.is_value("dock_with"))
        {
            char const* BaseName = Reader.get_value_string();
            unsigned const BaseId = Universe::get_base_id(BaseName);
            Parms.DockWithBaseId = BaseId;
            if (BaseId == 0)
            {
                FDUMP(0x100001, "%s(%d) : *** ERROR: Could not find docking target %s for solar object.",
                      "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0x22c, BaseName);
            }
        }
        else if (Reader.is_value("loadout"))
        {
            LoadoutId.Id = CreateID(Reader.get_value_string());
        }
        else if (Reader.is_value("atmosphere_range"))
        {
            // CreateParms +0xdc.
            Parms.AtmosphereRange = Reader.get_value_float(0);
            bAtmosphereSet = true;
        }
        else if (Reader.is_value("anim"))
        {
            // Parms +0xe4 list.
            char const* AnimName = StringAlloc(Reader.get_value_string(), false);
            Parms.ContinualAnims.push_back(AnimName);
        }
        else if (Reader.is_value("prev_ring"))
        {
            Parms.PrevRing = CreateID(Reader.get_value_string());
        }
        else if (Reader.is_value("next_ring"))
        {
            Parms.NextRing = CreateID(Reader.get_value_string());
        }
        else if (Reader.is_value("tradelane_space_name"))
        {
            Parms.TradelaneSpaceNameId = (unsigned)Reader.get_value_int(0);
        }
        else if (Reader.is_value("destructible"))
        {
            bDestructibleSet = true;
            Parms.bDestructible = Reader.get_value_bool(0) ? 1 : 0;
        }
        else if (Reader.is_value("space_costume"))
        {
            // Parms +0x64..+0x97. Value 0 is the head (bodypart kind 1), value 1 the body.
            CostumeDescriptions const* Costumes = GetCostumeDescriptions();
            Costume* CostumeOut = &Parms.SpaceCostume;
            if (!Reader.is_value_empty(0) && Reader.get_value_string(0)[0] != '\0')
            {
                bodypart const* Head = Costumes->find_bodypart(1, Reader.get_value_string(0));
                if (Head != nullptr)
                {
                    CostumeOut->PartIds[0] = Head->Id;                 // +0x64
                }
                else
                {
                    FDUMP(0x100001, "%s(%d) : *** ERROR: space costume head %s does not exist\n",
                          "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0x25e, Reader.get_value_string(0));
                }
            }
            if (!Reader.is_value_empty(1) && Reader.get_value_string(1)[0] != '\0')
            {
                bodypart const* Body = Costumes->find_bodypart(0, Reader.get_value_string(1));
                if (Body != nullptr)
                {
                    CostumeOut->PartIds[1] = Body->Id;                 // +0x68
                }
                else
                {
                    FDUMP(0x100001, "%s(%d) : *** ERROR: space costume body %s does not exist\n",
                          "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0x26f, Reader.get_value_string(1));
                }
            }
            // Every further value is an accessory. Retail quirks kept: no null test on
            // find_accessory (an unknown nickname faults) and no bound on the count, so a ninth
            // accessory lands on the count itself (+0x94).
            unsigned* const AccessorySlots = CostumeOut->AccessoryIds;   // +0x74
            unsigned Index = 2;
            while (!Reader.is_value_empty(Index))
            {
                accessory const* const Acc = Costumes->find_accessory(Reader.get_value_string(Index));
                ++Index;
                unsigned const Slot = CostumeOut->AccessoryCount;
                AccessorySlots[Slot] = Acc->Id;
                CostumeOut->AccessoryCount = CostumeOut->AccessoryCount + 1;
            }
            if (CostumeOut->PartIds[1] == 0)
            {
                FDUMP(0x100001, "%s(%d) : *** ERROR: space costume malformed %s",
                      "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0x27c, Reader.get_value_string());
            }
        }
        else if (Reader.is_value("voice"))
        {
            // Parms +0x98
            Parms.VoiceId = CreateID(Reader.get_value_string());
        }
        else if (Reader.is_value("comm_appr"))
        {
            CostumeDescriptions const* Costumes = GetCostumeDescriptions();
            Costumes->get_costume(Reader.get_value_string(), Parms.SpaceCostume);
        }
    }

    // Reported only when the solar radius (+0x98) is strictly larger (unordered is not).
    if (bAtmosphereSet && OutArch != nullptr && Parms.AtmosphereRange < OutArch->SolarRadius)
    {
        // Arguments fetched in retail's push order.
        char const* const FileName = Reader.get_file_name();
        int const AtmosphereInt = (int)Parms.AtmosphereRange;
        char const* const ArchStr = OutArch->Nickname.get_string();
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: Solar with Archetype(%s) is larger than atmosphere %df in INI File(%s)!",
              "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0x289, ArchStr, AtmosphereInt, FileName);
    }

    if (OutArch == nullptr)
    {
        char const* const FileName = Reader.get_file_name();
        char const* const NickStr = Parms.NicknameSrc.get_string();
        FDUMP(0x100001, "%s(%d) : *** ERROR: Solar with Nickname(%s) missing archetype in INI File(%s)!",
              "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0x291, NickStr, FileName);
        return false;
    }

    if (Parms.NicknameSrc.IsEmpty())
    {
        char const* const FileName = Reader.get_file_name();
        char const* const ArchStr = OutArch->Nickname.get_string();
        FDUMP(0x100001, "%s(%d) : *** ERROR: Solar with Archetype(%s) missing nickname in INI File(%s)!",
              "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0x296, ArchStr, FileName);
        return false;
    }

    // Parms +0x60. The error test is on the stored value, not on Verify's result.
    unsigned const VibeId = Reputation::Vibe::Verify((int)Parms.NicknameSrc.Id)
        ? Parms.NicknameSrc.Id : (unsigned)-1;
    Parms.VibeId = VibeId;
    if (VibeId == (unsigned)-1)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: Failed to read reputation information for solar(%s)!",
              "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0x2a3, Parms.NicknameSrc.get_string());
        return false;
    }

    // Default: the archetype's loadout (+0xa8).
    if (LoadoutId.IsEmpty())
        LoadoutId.Id = OutArch->Loadout.Id;
    if (!LoadoutId.IsEmpty() && LoadoutId.Id != 0)
    {
        Loadout::Map const* const LoadoutMap = Loadout::Get(LoadoutId.Id);
        if (LoadoutMap != nullptr)
        {
            AssignEquipDescsFromLoadout(Parms.Equipment, LoadoutMap->Items);
        }
        else
        {
            // Not fatal.
            char const* const LoadoutStr = LoadoutId.get_string();
            char const* const NickStr = Parms.NicknameSrc.get_string();
            FDUMP(0x100001, "%s(%d) : *** ERROR: %s: Unable to find [Loadout] %s.",
                  "E:\\FL\\Scratch\\Source\\Common\\CSolar.cpp", 0x2c9, NickStr, LoadoutStr);
        }
    }

    // Default: the archetype's byte (+0x94).
    if (!bDestructibleSet)
        Parms.bDestructible = (unsigned char)OutArch->bDestructible;

    return true;
}

float CSolar::get_atmosphere_immersion(Vector const& Pos) const
{
    // FUN_062b7990. 1.0f strictly inside AtmosphereRange of the solar's position, else 0.0f.
    // Unrounded x87 sum, order (x, z, y).
    if (!(AtmosphereRange > 0.0f))
        return 0.0f;

    Vector const& WorldPos = get_position();
    double const Dx = (double)Pos.x - WorldPos.x;
    double const Dy = (double)Pos.y - WorldPos.y;
    double const Dz = (double)Pos.z - WorldPos.z;
    double const DistSq = (Dx * Dx + Dz * Dz) + Dy * Dy;
    double const RangeSq = (double)AtmosphereRange * AtmosphereRange;
    if (RangeSq > DistSq)
        return 1.0f;
    return 0.0f;
}
