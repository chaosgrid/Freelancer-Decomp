// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_launchbehavior.h"

#include <cmath>
#include <cstring>

#include "common_systemmanager.h"
#include "common_iface.h"

// DAT_063fc538 caches FUN_06391cf0, the DACOM_Acquire("3DMathEngine") ported as
// Acquire3DMathEngine().
#include "common_csys.h"   // Transform / Matrix / Acquire3DMathEngine
#include "x86math.h"       // I3DMathEngine::TransposeTransform, Vector, Matrix
#include "x87_trig.h"      // retail's inline FSIN / FCOS (RotateBasisRowsXZ)
#include "common_ini.h"    // VecNormalize3 (FUN_062e1680) -- StartLaunchSequence
#include "common_aisteer.h" // ClampedDot3 (FUN_062e1630), GetObjectTypeOrZero (FUN_062e6200)
#include "common_cobjdb.h"  // IObjDB::GetZoneDb -- FetchSystemId
#include "common_universe.h" // Universe::ISystem::GetId -- FetchSystemId

// FUN_062ce320: rotates the (x, z) pair of each row of a 3x3 basis by Angle, in place, using
// the original x for z'. Products and sums stay unrounded; each result is stored as float.
// Update() case 5 and StartLaunchSequence() inline the same loop with DAT_0639e2d0 (the float
// pi widened to double), so they share this body.
static void RotateBasisRowsXZ(Vector* Rows, double Angle)
{
    double const Cos = X87Cos(Angle);
    double const Sin = X87Sin(Angle);
    for (int Row = 0; Row < 3; ++Row)
    {
        double const X = Rows[Row].x;
        double const Z = Rows[Row].z;
        Rows[Row].x = static_cast<float>(X * Cos - Sin * Z);
        Rows[Row].z = static_cast<float>(Cos * Z + X * Sin);
    }
}

// FUN_062cd640. Move is a MoveIdea (FUN_062d4e60), Face a FaceIdea (FUN_062d6750).
LaunchBehavior::LaunchBehavior(int InTypeId)
    : Behavior(InTypeId)
    , Move()
    , Face()
    , ModeState164(0)
    , Watcher168()
    , CachedSystemId170(0)
    , TargetType174(0)
    , DockPortIndex(0)
    , bStraightLaunch(0)
    , Pad17d{}
    , LaunchDwellDuration(3.5f)
    , LaunchDwellElapsed(0.0f)
    , TotalLaunchElapsed(0.0f)
    , LaunchTimeout(0.0f)
    , LaunchOffset{}
    , DockPointC{}
    , LaunchBasis{}
    , DockPointB{}
    , ApproachBasis2{}
    , DockPointA{}
    , ApproachBasis3{}
    , LaunchThrottleScale(15.0f)
    , bHalfThrottleOnExit(0)
    , bFlipHeadingOnExit(0)
{
}

// FUN_062cd700 (scalar deleting FUN_062cd6e0): clears +0x168, then Face's watcher
// (+0xb8), then Move's (+0x40); ~Behavior (FUN_062d2080) chains after.
LaunchBehavior::~LaunchBehavior()
{
    Watcher168.set(nullptr);
    Face.Watcher.set(nullptr);
    Move.Watcher.set(nullptr);
}

// Slot 1, LAB_062cd790.
bool LaunchBehavior::SetContext(void* Param1, void* Param2, void* Param3)
{
    if (!BindContext(Param1, Param2, Param3))                     // FUN_062d2090
        return false;

    // Both ideas get the raw incoming Param2 as the manager argument.
    Move.SetContext(OwnerContext, SysMgr, Param2);

    // FUN_062d5980(&Move, 0), MoveIdea::SetMode(0).
    ReleaseApproachStep(0);

    Face.SetContext(OwnerContext, SysMgr, Param2);

    // A player-flown ship gets a 15-second launch timeout, an NPC 60.
    LaunchTimeout = NestedNavQuery0xbc() ? 15.0f : 60.0f;

    return true;
}

// Slot 3, FUN_062cd830.
void LaunchBehavior::Engage()
{
    Behavior::OnEngage();

    // The current system's id through the manager's object database.
    CachedSystemId170 = FetchSystemId();

    // FUN_062e6200 null-checks its argument itself.
    Watchable const* Watched168 = Watcher168.target();
    void const* RawBlock = Watched168 ? (reinterpret_cast<unsigned char const*>(Watched168) - 8) : nullptr;
    TargetType174 = GetObjectTypeOrZero(ObjInspectOf(RawBlock));

    SysMgr->GunGroups.SetShieldActive(0); // FUN_062e9a60

    ModeState164 = 1;
    bStraightLaunch = 0;
    LaunchDwellElapsed = 0.0f;
    TotalLaunchElapsed = 0.0f;

    SetAvoidanceAllowed(0, 3);                                            // FUN_062d21d0
}

// Slot 4, FUN_062cd8b0. Move commits before Face here.
void LaunchBehavior::Disengage()
{
    ReleaseSubObject(&Move);
    ReleaseSubObject(&Face);

    Behavior::OnDisengage();

    SysMgr->GunGroups.SetShieldActive(1);

    SetAvoidanceAllowed(1, 3);

    Flags28 = static_cast<unsigned char>((Flags28 & ~0x2) | 0x1);

    BehaviorMgr->SetDirectiveResult(8, 0);
}

// Slot 5, LAB_062cd910.
int LaunchBehavior::SetModeFromFlags()
{
    int Mode = (Flags28 & 0x2) ? 0 : 2;
    if (UpdateResult == 2 || UpdateResult == 3)
        Mode = 2;
    if (Flag29 != 0)
    {
        Readiness = 3;
        return 3;
    }
    Readiness = Mode;
    return Mode;
}

// Slot 10, FUN_062cdf30.
bool LaunchBehavior::ApplyDirective(void const* Param)
{
    // FUN_062e74a0 on the op's LaunchObjectId; set_pointer only when the Watchable differs.
    void* Found = FindLaunchObject(SysMgr, Param);
    void* Adjusted = Found ? (static_cast<unsigned char*>(Found) + 8) : nullptr;
    Watchable const* AdjustedWatchable = static_cast<Watchable const*>(Adjusted);

    if (AdjustedWatchable != Watcher168.target())
        Watcher168.set(AdjustedWatchable);

    DockPortIndex = static_cast<pub::AI::DirectiveLaunchOp const*>(Param)->DockPortIndex;

    Watchable const* Watched168 = Watcher168.target();
    void const* RawBlock = Watched168 ? (reinterpret_cast<unsigned char const*>(Watched168) - 8) : nullptr;

    if (RawBlock != nullptr)
    {
        Flags28 = static_cast<unsigned char>(Flags28 | 0x2);
        UpdateResult = 1;
        return true;
    }

    Flags28 = static_cast<unsigned char>(Flags28 & ~0x2);
    return false;
}

// Slot 7, LAB_062cd950 (no Ghidra decompile; from the raw listing). ModeState164:
//   1 dwell for LaunchDwellDuration (3.5 s), 2 fetch the dock hardpoints and fall into 3,
//   3 pick the launch path from DockPortIndex, 4 align along the approach (skipped when
//   bStraightLaunch), 5 fly out to DockPointC, 6 optional pi turn after exit.
// Every out-of-range or mid-case abort lands on the plain tail 062cdc79 (result 0, mode
// unchanged); Ghidra shows it with the same label text as case 6's entry 062cdea8.
int LaunchBehavior::Update()
{
    if (OwnerContext == nullptr)
    {
        UpdateResult = 3;
        return 3;
    }
    Watchable const* Watched168 = Watcher168.target();
    if (Watched168 == nullptr || (reinterpret_cast<unsigned char const*>(Watched168) - 8) == nullptr)
    {
        UpdateResult = 3;
        return 3;
    }

    // A system change since Engage() aborts with 2.
    if (CachedSystemId170 != FetchSystemId())
    {
        UpdateResult = 2;
        return 2;
    }

    // Launch timeout. Strictly greater aborts.
    if (TotalLaunchElapsed > LaunchTimeout)
    {
        UpdateResult = 3;
        return 3;
    }
    TotalLaunchElapsed += DeltaTime;

    switch (ModeState164)
    {
    case 1:
    {
        // Strictly greater advances.
        if (LaunchDwellElapsed > LaunchDwellDuration)
            ModeState164 = 2;
        LaunchDwellElapsed += DeltaTime;
        UpdateResult = 0;
        return 0;
    }
    case 2:
    {
        ComputeLaunchApproachTransform();
        // Also reached by the helper's early return on a successful get_hardpoint; retail
        // then falls straight into case 3.
        ModeState164 = 3;
        [[fallthrough]];
    }
    case 3:
    {
        if (DockPortIndex >= 0)
        {
            StartLaunchSequence();                                   // FUN_062cdfa0
            ModeState164 = 4;
            UpdateResult = 0;
            return 0;
        }
        if (DockPortIndex == -1)
        {
            ModeState164 = 2;
            UpdateResult = 0;
            return 0;
        }
        if (DockPortIndex == -2)
        {
            Disengage();                                             // own slot 4
            UpdateResult = 3;
            return 3;
        }
        UpdateResult = 0;
        return 0;
    }
    case 4:
    {
        if (bStraightLaunch != 0)
        {
            SetApproachSpeedParam(25.0f);
            ReleaseApproachStep(0);
            ModeState164 = 5;
            UpdateResult = 0;
            return 0;
        }

        PushApproachBasisToSubObjectB(&ApproachBasis2[0]);
        TickSubObject(&Face, DeltaTime);
        SetApproachOffset(&DockPointB);
        SetApproachSpeedParam(5.0f);
        ReleaseApproachStep(1);
        SetApproachThrottleScale(LaunchThrottleScale);
        SetApproachMode(2);
        int TickResult = TickSubObject(&Move, DeltaTime);
        if (TickResult != 1)
        {
            UpdateResult = 0;
            return 0;
        }
        SetApproachSpeedParam(15.0f);
        ModeState164 = 5;
        UpdateResult = 0;
        return 0;
    }
    case 5:
    {
        PushApproachBasisToSubObjectB(&LaunchBasis[0]);
        TickSubObject(&Face, DeltaTime);
        SetApproachOffset(&DockPointC);
        SetApproachMode(2);
        int TickResult = TickSubObject(&Move, DeltaTime);

        // Delta1 is stored to float.
        float const Delta1X = DockPointC.x - DockPointB.x;
        float const Delta1Y = DockPointC.y - DockPointB.y;
        float const Delta1Z = DockPointC.z - DockPointB.z;

        // The inlined FUN_062e6d40 cache refresh.
        SystemManager* const Sys = SysMgr;
        if (Sys->SelfTarget.bPosValid == 0)
            EnsureNestedNavPositionCached();

        // Delta2 is loaded unconditionally and never stored; a tick result of 1 then skips
        // the dot test.
        double const Delta2X = static_cast<double>(Sys->SelfTarget.CachedPos.x) - DockPointC.x;
        double const Delta2Y = static_cast<double>(Sys->SelfTarget.CachedPos.y) - DockPointC.y;
        double const Delta2Z = static_cast<double>(Sys->SelfTarget.CachedPos.z) - DockPointC.z;
        if (TickResult != 1)
        {
            // Continue only on a strictly positive dot (DAT_06399408 = 0.0f).
            double const Dot = (Delta1Z * Delta2Z + Delta1Y * Delta2Y) + Delta1X * Delta2X;
            if (!(Dot > 0.0))
            {
                UpdateResult = 0;
                return 0;
            }
        }

        ReleaseSubObject(&Move);
        SysMgr->GunGroups.SetShieldActive(1);

        if (bHalfThrottleOnExit != 0)
            AdjustNavHelperThrottle(0.5f);

        if (bFlipHeadingOnExit != 0)
        {
            // DAT_0639e2d0 = 0x400921FB60000000, the float pi widened: it overshoots true pi,
            // so sin() is -8.742278e-08, not 0.
            RotateBasisRowsXZ(LaunchBasis, 3.1415927410125732);

            ModeState164 = 6;
            UpdateResult = 0;
            return 0;
        }

        Flags28 = static_cast<unsigned char>((Flags28 & ~0x2) | 0x1);
        UpdateResult = 2;
        return 2;
    }
    case 6:
    {
        // pi/8 as an immediate (0x3ec90fdb), not from .rdata.
        SetHardpointCone(0.39269909f);
        PushApproachBasisToSubObjectB(&LaunchBasis[0]);
        int TickResult = TickSubObject(&Face, DeltaTime);
        if (TickResult != 1)
        {
            UpdateResult = 0;
            return 0;
        }
        Flags28 = static_cast<unsigned char>((Flags28 & ~0x2) | 0x1);
        UpdateResult = 2;
        return 2;
    }
    default:
        UpdateResult = 0;
        return 0;
    }
}

// ----------------------------------------------------------------------------
// Helpers.
// ----------------------------------------------------------------------------
// ObjectDb (+0x18, set through Behavior::SetObjectDb from FUN_062d2830) -> GetZoneDb (IObjDB
// slot 2) -> ISystem slot 0, no null tests.
unsigned LaunchBehavior::FetchSystemId() const
{
    Universe::ISystem const* const System = ObjectDb->GetZoneDb();
    return System->GetId();
}

// FUN_062e74a0 on the op's LaunchObjectId.
void* LaunchBehavior::FindLaunchObject(SystemManager* SysMgr, void const* Param)
{
    return SysMgr->Timers.FindVerifiedObject(
        &static_cast<pub::AI::DirectiveLaunchOp const*>(Param)->LaunchObjectId);
}

// SetContext()'s LaunchTimeout selector: is_player() on SystemManager+0x1580 (retail spells
// the single load as `ADD EAX,0x14fc; MOV EAX,[EAX+0x84]`), no null test. FaceBehavior::Update
// has the identical chain (062c8e0a, 062c92d8).
bool LaunchBehavior::NestedNavQuery0xbc()
{
    SystemManager const* const Sys = SysMgr;
    return Sys->SelfTarget.Iface->is_player();
}

// ----------------------------------------------------------------------------
// Update() case 2's payload (062cda2f-062cdbdd). The three get_dock_hardpoints out-buffers
// are disjoint 0x30-byte frame slots. A successful get_hardpoint("hpmount") on
// OwnerContext skips the tail; the tail runs only on failure. FUN_062d5a80 stores
// LaunchOffset into Move+0x4c (MoveIdea::Offset4c).
void LaunchBehavior::ComputeLaunchApproachTransform()
{
    // Unguarded: Update()'s entry gate already proved both halves non-null.
    Watchable const* const Watched = Watcher168.target();
    void const* const Owner = reinterpret_cast<unsigned char const*>(Watched) - 8;

    // Retail leaves these three uninitialised too -- get_dock_hardpoints fills them.
    Transform XformA;
    Transform XformB;
    Transform XformC;
    ObjInspectOf(Owner)->get_dock_hardpoints(DockPortIndex, nullptr,
                                             &XformA, &XformB, &XformC, nullptr);

    DockPointA = XformA.Translation;
    std::memcpy(ApproachBasis3, &XformA.Rotation, sizeof(ApproachBasis3));
    DockPointB = XformB.Translation;
    std::memcpy(ApproachBasis2, &XformB.Rotation, sizeof(ApproachBasis2));
    DockPointC = XformC.Translation;
    std::memcpy(LaunchBasis, &XformC.Rotation, sizeof(LaunchBasis));

    Vector HardpointPos;
    Matrix HardpointOrient;
    if (ObjInspectOf(OwnerContext)->get_hardpoint("hpmount", &HardpointPos,
                                                       &HardpointOrient) != 0)
        return;  // non-zero skips the tail

    SystemManager* const Sys = SysMgr;

    // The same inlined cache refresh case 5 uses (latch +0x157a).
    if (Sys->SelfTarget.bPosValid == 0)
        EnsureNestedNavPositionCached();

    LaunchOffset.x = HardpointPos.x - Sys->SelfTarget.CachedPos.x;
    LaunchOffset.y = HardpointPos.y - Sys->SelfTarget.CachedPos.y;
    LaunchOffset.z = HardpointPos.z - Sys->SelfTarget.CachedPos.z;

    Transform const& OwnerXform = Sys->SelfTarget.Iface->get_transform();

    I3DMathEngine* const Math = Acquire3DMathEngine();

    // Slot +0x38: the world-space hardpoint offset rotated into the owner's local frame.
    Vector Rotated = {0.0f, 0.0f, 0.0f};
    Math->TransposeTransform(Rotated, OwnerXform.Rotation, LaunchOffset);
    LaunchOffset = Rotated;

    Move.Offset4c = LaunchOffset;                              // FUN_062d5a80
}

// FUN_062cdfa0, called only from Update() case 3, which ignores the result. Re-reads the
// terminal's dock hardpoints (same mapping as case 2), decides whether the approach is
// already straight enough to skip the alignment stage, then special-cases the terminal type.
//   Dir1 = normalize(B - A), Dir2 = normalize(C - B)
//   Dot1 = Dir1 . Dir2, Dot2 = ApproachBasis2.z . Dir1 (both stored to float)
//   Dir3 = normalize(ApproachBasis3.z + ApproachBasis2.z + LaunchBasis.z)
//   Dot3 = Dir1 . Dir3 (left unrounded)
// bStraightLaunch needs all three strictly above 0.999 / 0.99 / 0.9999 (doubles
// DAT_0639fd40 / DAT_0639dfe8 / DAT_0639fd38); unordered clears it.
bool LaunchBehavior::StartLaunchSequence()
{
    Watchable const* const Watched = Watcher168.target();
    if (Watched == nullptr || (reinterpret_cast<unsigned char const*>(Watched) - 8) == nullptr)
        return false;
    void const* const Owner = reinterpret_cast<unsigned char const*>(Watched) - 8;

    // Retail leaves all four out-buffers uninitialised; get_dock_hardpoints fills them.
    TERMINAL_TYPE Type;
    Transform XformA;
    Transform XformB;
    Transform XformC;
    ObjInspectOf(Owner)->get_dock_hardpoints(DockPortIndex, &Type,
                                             &XformA, &XformB, &XformC, nullptr);
    DockPointA = XformA.Translation;
    std::memcpy(ApproachBasis3, &XformA.Rotation, sizeof(ApproachBasis3));
    DockPointB = XformB.Translation;
    std::memcpy(ApproachBasis2, &XformB.Rotation, sizeof(ApproachBasis2));
    DockPointC = XformC.Translation;
    std::memcpy(LaunchBasis, &XformC.Rotation, sizeof(LaunchBasis));

    Vector Scratch;
    Scratch.x = DockPointB.x - DockPointA.x;
    Scratch.y = DockPointB.y - DockPointA.y;
    Scratch.z = DockPointB.z - DockPointA.z;
    Vector Dir1;
    VecNormalize3(&Scratch, &Dir1);                                              // FUN_062e1680

    Scratch.x = DockPointC.x - DockPointB.x;
    Scratch.y = DockPointC.y - DockPointB.y;
    Scratch.z = DockPointC.z - DockPointB.z;
    Vector Dir2;
    VecNormalize3(&Scratch, &Dir2);

    float const Dot1 = ClampedDot3(Dir1, Dir2);                                  // FUN_062e1630

    Vector Basis2Z;
    Basis2Z.x = ApproachBasis2[0].z;
    Basis2Z.y = ApproachBasis2[1].z;
    Basis2Z.z = ApproachBasis2[2].z;
    float const Dot2 = ClampedDot3(Basis2Z, Dir1);

    Vector LaunchZ;
    LaunchZ.x = LaunchBasis[0].z;
    LaunchZ.y = LaunchBasis[1].z;
    LaunchZ.z = LaunchBasis[2].z;
    Vector Basis3Z;
    Basis3Z.x = ApproachBasis3[0].z;
    Basis3Z.y = ApproachBasis3[1].z;
    Basis3Z.z = ApproachBasis3[2].z;
    Vector Sum;
    Sum.x = Basis3Z.x + Basis2Z.x;                                               // FUN_062887d0
    Sum.y = Basis3Z.y + Basis2Z.y;
    Sum.z = Basis3Z.z + Basis2Z.z;

    Scratch.x = Sum.x + LaunchZ.x;
    Scratch.y = Sum.y + LaunchZ.y;
    Scratch.z = Sum.z + LaunchZ.z;
    Vector Dir3;
    VecNormalize3(&Scratch, &Dir3);

    double const Dot3 = ClampedDot3(Dir1, Dir3);

    double const StraightDot1 = 0.999;   // DAT_0639fd40
    double const StraightDot2 = 0.99;    // DAT_0639dfe8
    double const StraightDot3 = 0.9999;  // DAT_0639fd38
    if (Dot1 > StraightDot1 && Dot2 > StraightDot2 && Dot3 > StraightDot3)
        bStraightLaunch = 1;
    else
        bStraightLaunch = 0;

    // switch (Type - 1); anything above 7 (unsigned) just returns.
    switch (static_cast<int>(Type))
    {
    case 1:
        bHalfThrottleOnExit = 1;
        [[fallthrough]];
    case 6:
    case 7:
    case 8:
        // ApproachBasis3 gets the inlined rotation with the double DAT_0639e2d0; the other
        // two go through FUN_062ce320 with the float immediate 0x40490fdb.
        RotateBasisRowsXZ(ApproachBasis3, 3.1415927410125732);                  // DAT_0639e2d0
        RotateBasisRowsXZ(ApproachBasis2, static_cast<double>(3.14159274f));
        RotateBasisRowsXZ(LaunchBasis, static_cast<double>(3.14159274f));
        return true;
    case 2:
    case 3:
    case 4:
    case 5:
        bStraightLaunch = 0;
        bFlipHeadingOnExit = 1;
        return true;
    default:
        return true;
    }
}

// MoveIdea::SetRange3c through the ICF fold 062a66b0 (Ghidra: CAsteroidCube::set_size /
// DeviceTRC::bind_to_dc). The value is an approach range in metres (25 / 5 / 15), not a
// speed, despite the method name.
void LaunchBehavior::SetApproachSpeedParam(float Value)
{
    Move.SetRange3c(Value);
}

// FUN_062d5980, MoveIdea::SetMode.
void LaunchBehavior::ReleaseApproachStep(int Flag)
{
    Move.SetMode(Flag);
}

// FUN_062d5840, MoveIdea::SetPosition.
void LaunchBehavior::SetApproachOffset(Vector const* OffsetVector)
{
    Move.SetPosition(*OffsetVector);
}

// FUN_062d75c0: FaceIdea+0xa0 = 4 and nine dwords copied from the caller's basis into
// FaceIdea+0x34 (the data flows into the idea, not out of it).
void LaunchBehavior::PushApproachBasisToSubObjectB(void const* SrcVectorBlock)
{
    Face.TargetKind = 4;
    std::memcpy(&Face.ZeroBlock34, SrcVectorBlock, sizeof(Face.ZeroBlock34));
}

// FUN_062d5a20, MoveIdea::SetThrottleScale (also called from DockBehavior::AdvancePreamble).
// The value is the throttle scale handed to get_projected_throttle, not a radius.
void LaunchBehavior::SetApproachThrottleScale(float Scale)
{
    Move.SetThrottleScale(Scale);
}

// FUN_062d88b0: MoveIdea+0x44 (StopStyle).
void LaunchBehavior::SetApproachMode(int Mode)
{
    Move.StopStyle = Mode;
}

// BaseIdea slot 1 (Update) / slot 4 (Commit).
int LaunchBehavior::TickSubObject(BaseIdea* SubObject, float DeltaTime) { return SubObject->Update(DeltaTime); }
void LaunchBehavior::ReleaseSubObject(BaseIdea* SubObject) { SubObject->Commit(); }

// FUN_062e6d40's cache refresh, inlined in case 5 (062cdd89) and reached only when the
// latch (+0x157a) is clear. The latch is set BEFORE the call.
void LaunchBehavior::EnsureNestedNavPositionCached()
{
    SystemManager* const Sys = SysMgr;
    Sys->SelfTarget.bPosValid = 1;
    Sys->SelfTarget.Iface->get_physical_radius(Sys->SelfTarget.CachedRadius,
                                                         Sys->SelfTarget.CachedPos);
}

// FUN_062d93c0: FaceIdea+0x8c (FacingTolerance).
void LaunchBehavior::SetHardpointCone(float Angle)
{
    Face.FacingTolerance = Angle;
}

// FUN_062e89a0, ThrustManager::SetThrottle.
void LaunchBehavior::AdjustNavHelperThrottle(float Value)
{
    SysMgr->Thrust.SetThrottle(Value, 0);
}
