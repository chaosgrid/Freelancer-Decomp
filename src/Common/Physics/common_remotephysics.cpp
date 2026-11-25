// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Physics/common_physys_internal.h"

using namespace CmnPhySys;

// ============================================================================
// Remote Correction (RC) -- CRemotePhysicsController
// ============================================================================
//
// The class is declared in common_server.h (public because Server.dll dynamic_casts to it);
// its RTTI, both vftables and its layout are documented there.
//
// All three controller methods begin or end by enqueueing this->Core for wakeup
// (FUN_06348f50, IVP_Controller_Manager_Ensure_Core_To_Be_In_Simulation).

// Inlined into PhySys::RCBegin: the golem base, the CRemotePhysicsSimulation base at +0xd0,
// this class's RemoteUpdate called directly with time 0.0, then the velocity, the body and a
// zero sample count. The count is reset after RemoteUpdate so the seed sample does not count.
CRemotePhysicsController::CRemotePhysicsController(IVP_Real_Object* InBody,
                                                   FLControllerGolemParms const& Parms,
                                                   Vector const& Position,
                                                   Quaternion const& Orientation,
                                                   Vector const& Velocity)
    : IVP_Controller_Golem(InBody, Parms)
    , CRemotePhysicsSimulation()
{
    CRemotePhysicsController::RemoteUpdate(Position, Orientation, 0.0);

    m_Velocity = Velocity;                               // this+0x348
    RealObject = InBody;                                 // this+0x3b8
    m_SampleCount = 0;                                   // this+0x388
}

// FUN_06285aa0: the compiler's reverse-order base destruction, so the body is empty. FUN_06285a80
// is the scalar deleting destructor, LAB_06285de0 the this-adjusting thunk.
CRemotePhysicsController::~CRemotePhysicsController()
{
}

// FUN_062923a0, slot 1 of the CRemotePhysicsSimulation-side table (.rdata:0639ba2c): the base
// RemoteUpdate called directly, then the core wakeup.
void CRemotePhysicsController::RemoteUpdate(Vector const& Position,
                                            Quaternion const& Orientation,
                                            double Timestamp)
{
    CRemotePhysicsSimulation::RemoteUpdate(Position, Orientation, Timestamp);
    IVP_Controller_Manager_Ensure_Core_To_Be_In_Simulation(Core);
}

// LAB_062923e0, slot 4 of .rdata:0639ba38, overriding the golem's PD servo. It does not servo:
// it runs the dead reckoning one PSI forward, copies the result into the golem's target
// fields, keeps the golem's error accumulator up to date, and then beams the body onto the
// target every time.
void CRemotePhysicsController::do_simulation_controller(IVP_Event_Sim* EventSim,
                                                        IVP_U_Vector<IVP_Core>* AffectedCores)
{
    (void)AffectedCores;  // never read

    IVP_Controller_Manager_Ensure_Core_To_Be_In_Simulation(Core);

    CRemotePhysicsSimulation::Doit((double)EventSim->delta_time);

    // Retail copies all four dwords of a stack IVP_U_Point, so +0x10 (hesse) gets an
    // unwritten stack slot; the port leaves +0x10 as it is.
    CurrentTargetPosition.k[0] = m_ProjectedPosition.x;
    CurrentTargetPosition.k[1] = m_ProjectedPosition.y;
    CurrentTargetPosition.k[2] = m_ProjectedPosition.z;

    // The same unwritten fourth dword goes into +0xb8 in retail; not reproduced.
    TargetSpeed.k[0] = m_Velocity.x;
    TargetSpeed.k[1] = m_Velocity.y;
    TargetSpeed.k[2] = m_Velocity.z;

    // The simulation's (W,X,Y,Z) quaternion into IVP's (x,y,z,w).
    CurrentTargetQuat.x = m_Orientation.x;
    CurrentTargetQuat.y = m_Orientation.y;
    CurrentTargetQuat.z = m_Orientation.z;
    CurrentTargetQuat.w = m_Orientation.w;

    IVP_U_Point Delta;
    Delta.k[0] = CurrentTargetPosition.k[0] - Core->MWorldFCore.vv.k[0];
    Delta.k[1] = CurrentTargetPosition.k[1] - Core->MWorldFCore.vv.k[1];
    Delta.k[2] = CurrentTargetPosition.k[2] - Core->MWorldFCore.vv.k[2];

    // The same core-shift block as the golem's integrator.
    if ((Body->flags & 0x0C00u) == 0)
    {
        IVP_U_Point ShiftWorld;
        Core->MWorldFCore.vmult(&Body->shift_core_f_object, &ShiftWorld);
        Delta.subtract(&Delta, &ShiftWorld);
    }

    // x87: the sum is never stored; decay factor 0.9f (DAT_0639c470).
    double const DeltaQuadLength =
        ((double)Delta.k[1] * Delta.k[1] + (double)Delta.k[2] * Delta.k[2]) + (double)Delta.k[0] * Delta.k[0];
    SmoothedPositionErrorSq = (float)(DeltaQuadLength + (double)SmoothedPositionErrorSq * (double)0.9f);

    beam_to_current_target(EventSim);
}

// FUN_06292570, slot 7 (the golem base's pure slot). Wakes the core, then beams for Reason 0
// and 1, nothing for anything else; returns 0 on every path.
int CRemotePhysicsController::beam_to_target(IVP_Event_Sim const* EventSim, int Reason)
{
    IVP_Controller_Manager_Ensure_Core_To_Be_In_Simulation(Core);

    switch (Reason)
    {
    case 0:
        beam_to_current_target(EventSim);
        break;
    case 1:
        beam_to_current_target(EventSim);
        return 0;
    default:
        break;
    }
    return 0;
}

// FUN_06285900. The angular velocity parameter is never read. Object is not null-tested.
void __cdecl PhySys::RCBegin(CSimple* Object, Vector const& Pos, Vector const& Vel,
                             Quaternion const& Orient, Vector const& AngVel)
{
    (void)AngVel;

    FLControllerGolemParms Parms;
    FLControllerGolemParms_Init(Parms);

    // Retail copies a stack IVP_U_Point as four dwords, so parms+0x1c gets an unwritten slot.
    // Nothing reads it; the port keeps the 0.0f default.
    Parms.MaxTranslationForce[0] = 1.0e12f;
    Parms.MaxTranslationForce[1] = 1.0e12f;
    Parms.MaxTranslationForce[2] = 1.0e12f;
    Parms.MaxDeltaPosition = PhySys::RC_MAX_DELTA_POSITION;
    Parms.DeltaOrientation = PhySys::RC_MAX_DELTA_ORIENTATION;
    Parms.ForceFactor = 1.0f;
    Parms.DampFactor = 1.0f;
    Parms.TorqueFactor = 1.0f;
    Parms.AngularDampFactor = 1.0f;
    Parms.MaxTorque = 1.0e12f;

    // operator new(0x3c0) with retail's own null test: a failed allocation stores 0.
    IVP_Real_Object* const Body = Object->BodyPtr;
    CRemotePhysicsController* const Controller =
        new (std::nothrow) CRemotePhysicsController(Body, Parms, Pos, Orient, Vel);

    // The IVP_Controller subobject is at offset 0, so a null stays null.
    IVP_Controller* const AsController = Controller;
    Object->GolemController = AsController;
}

// FUN_06285b00. No dynamic cast: whatever controller is there is taken to be a
// CRemotePhysicsController. Object is not null-tested.
void __cdecl PhySys::RCSet(CSimple* Object, Vector const& Pos, Quaternion const& Orient, double Time)
{
    IVP_Controller* const Rc = Object->GolemController;
    if (Rc != nullptr)
    {
        CRemotePhysicsSimulation* const Sim =
            static_cast<CRemotePhysicsController*>(Rc);            // this + 0xd0
        Sim->RemoteUpdate(Pos, Orient, Time);                     // virtual slot 1
    }
}

// FUN_06285b30. Object is not null-tested.
void __cdecl PhySys::RCEnd(CSimple* Object)
{
    IVP_Controller* const Rc = Object->GolemController;
    if (Rc != nullptr)
    {
        delete Rc;
        Object->GolemController = nullptr;
    }
}

// CRemotePhysicsSimulation's eight members retail compiled into Common.dll, Server.dll and
// freelancer.exe (GetSimulationTime, CalcPositionError, GetSampleCount, GetLatestUpdate,
// GetSample, SetLogging, the copy constructor and operator=) are inline in common_server.h.

// ============================================================================
// CRemotePhysicsSimulation -- the seven members Common.dll exports and Server.dll
// does not. Ghidra's decompilation drops the hidden return / out pointers here; this
// follows the listing.
//
// Helper leaves, expanded by hand below:
//   FUN_0628b030  Vector(float X, float Y, float Z) -- Vec3Assign (common_ini.h).
//   FUN_06288800  Out = A - B.
//   FUN_06347e60  |*This|.
//   FUN_06391cf0  Acquire3DMathEngine().
//
// Constants (from the data bytes): 0.5f DAT_0639c13c, 1e-5f DAT_0639b984, 1.0f DAT_0639a1d0;
// doubles 1.0, 10.0, 0.002, 500.0, and the sync tolerances 2.0 / 0.5 below.
// ============================================================================

// .data:063eb390 / 063eb398, the sync tolerances CheckForSync scales by BlendScale. Retail has
// only one read of each and no writer; if a writer ever turns up, these must become settable
// globals.
static double G_RemoteSyncPositionTolerance = 2.0;   // .data:063eb390
static double G_RemoteSyncOrientTolerance   = 0.5;   // .data:063eb398

// DAT_063fc104: the c:\flservertrace.txt handle tracef keeps open for the life of the
// process.
static FILE* G_RemotePhysTraceFile = nullptr;

// FUN_06291bb0 (ordinal 3754). __cdecl varargs, so `this` is the first stack argument. Not
// called anywhere in retail or the port; it exists only as an export.

void CRemotePhysicsSimulation::tracef(char const* Format, ...)
{
    if (!m_bLogging)
        return;

    char Buffer[0x400];
    va_list Args;
    va_start(Args, Format);
    vsprintf(Buffer, Format, Args);
    va_end(Args);

    if (!IsMPServer())
    {
        // One fopen/fprintf/fclose per call; no null test, as in retail.
        FILE* File = fopen("c:\\client.updates", "a+");
        fprintf(File, "%s", Buffer);
        fclose(File);
        return;
    }

    // MP server: the handle is opened once and never closed.
    if (G_RemotePhysTraceFile == nullptr)
        G_RemotePhysTraceFile = fopen("c:\\flservertrace.txt", "wt");
    fputs(Buffer, G_RemotePhysTraceFile);
    fflush(G_RemotePhysTraceFile);
}

// FUN_06291c70 (ordinal 180). Retail inlines the three CDeadReckonedVector default
// constructors and then an exact copy of Reset's body; expressed here as member construction
// plus a call to Reset. Retail writes nothing at +0x29c (the quaternion Doit rebuilds),
// +0x2bc, +0x2d0 or +0x2e0: m_bLogging keeps whatever the allocation held until SetLogging.
CRemotePhysicsSimulation::CRemotePhysicsSimulation()
{
    Reset();
}

// The copy constructor (FUN_062835b0, ordinal 179) and operator= (FUN_06283b30, ordinal 742)
// are inline in common_server.h.

// FUN_06291d50 (ordinal 571): only vptr stores; the class owns no heap memory.
CRemotePhysicsSimulation::~CRemotePhysicsSimulation()
{
}

// FUN_06291d70 (ordinal 1936). Re-primes only the trailing scratch fields: not the three
// sub-objects, not the +0x29c quaternion, not m_bLogging.
void CRemotePhysicsSimulation::Reset()
{
    // The published velocity, its blend-from and blend-to values, and the mirror of the
    // position DRV's ProjectedPosition.
    Vec3Assign(&m_Velocity, 0.0f, 0.0f, 0.0f);
    Vec3Assign(&m_BlendFromVelocity, 0.0f, 0.0f, 0.0f);
    Vec3Assign(&m_BlendToVelocity, 0.0f, 0.0f, 0.0f);
    Vec3Assign(&m_ProjectedPosition, 0.0f, 0.0f, 0.0f);

    m_SampleCount = 0;
    // +0x2d0 is left alone.
    m_ConvergeWindow = 0.0;
    m_SimulationTime = 0.0;
    m_SyncBackoff = 5.0;
}

// FUN_06291e10, vtable slot 1 (ordinal 1931). The only thing that feeds the three
// dead-reckoners a network sample: the position verbatim, and the orientation as the first
// two columns of its rotation matrix (two distinct axis vectors, neither a velocity).
void CRemotePhysicsSimulation::RemoteUpdate(Vector const& Position,
                                            Quaternion const& Orientation,
                                            double Timestamp)
{
    // The engine is not tested for null, as in retail.
    Matrix Basis;
    I3DMathEngine* const Math = Acquire3DMathEngine();
    Math->QuaternionToMatrix(Basis, Orientation);

    m_PositionDRV.AddSample(Position, Timestamp);

    Vector AxisX;                                           // column 0
    AxisX.x = Basis.E[0][0];
    AxisX.y = Basis.E[1][0];
    AxisX.z = Basis.E[2][0];
    m_OrientAxisXDRV.AddSample(AxisX, Timestamp);

    Vector AxisY;                                           // column 1
    AxisY.x = Basis.E[0][1];
    AxisY.y = Basis.E[1][1];
    AxisY.z = Basis.E[2][1];
    m_OrientAxisYDRV.AddSample(AxisY, Timestamp);

    ++m_SampleCount;

    m_BlendFromVelocity = m_Velocity;

    // Read after AddSample re-fitted the quadratic. x87: the window is used once as a double
    // and once as a float copy.
    double const WindowD = m_PositionDRV.ConvergeWindow;
    float const WindowF = (float)WindowD;

    Vector const& Accel = m_PositionDRV.Acceleration;
    Vector const& Velocity = m_PositionDRV.Velocity;

    float const HalfAccelX = Accel.x * 0.5f;
    float const HalfAccelY = Accel.y * 0.5f;
    float const HalfAccelZ = Accel.z * 0.5f;

    float const ScaledX = (float)(HalfAccelX * WindowD);
    float const ScaledY = (float)(HalfAccelY * WindowD);
    float const ScaledZ = (float)(HalfAccelZ * WindowD);

    // x87: the y and z products by WindowF are rounded first; x is added unrounded.
    float const ProductY = ScaledY * WindowF;
    float const ProductZ = ScaledZ * WindowF;
    Vec3Assign(&m_BlendToVelocity,
               (float)((double)ScaledX * WindowF + Velocity.x),
               ProductY + Velocity.y,
               ProductZ + Velocity.z);

    // The window Doit's blend test compares the simulation clock against.
    m_ConvergeWindow = m_PositionDRV.ConvergeWindow;
    m_SimulationTime = 0.0;
}

// FUN_06291fe0 (ordinal 1381). Advance the clock and the three dead-reckoners; renormalise
// both orientation dead-reckoners' projected and tracked vectors (dead-reckoning a unit vector
// does not keep it unit); rebuild an orthonormal basis from the two projected axes for
// MatrixToQuaternion; publish a velocity.
void CRemotePhysicsSimulation::Doit(double Dt)
{
    m_SimulationTime += Dt;
    m_PositionDRV.Doit(Dt);
    m_OrientAxisXDRV.Doit(Dt);
    m_OrientAxisYDRV.Doit(Dt);

    // Retail inlines these four (normalise only when the length is strictly greater than
    // 1e-5f); they are the same instructions as CDeadReckonedVector's Normalize members.
    m_OrientAxisXDRV.NormalizeProjectedPosition();
    m_OrientAxisXDRV.NormalizeTrackedPosition();
    m_OrientAxisYDRV.NormalizeProjectedPosition();
    m_OrientAxisYDRV.NormalizeTrackedPosition();

    // Gram-Schmidt, then a cross product.
    Vector const& U = m_OrientAxisXDRV.ProjectedPosition;
    Vector const& W = m_OrientAxisYDRV.ProjectedPosition;

    // x87: summed z, y, x and never stored.
    double const Dot = ((double)W.z * U.z + (double)W.y * U.y) + (double)W.x * U.x;

    Vector Proj;
    Vec3Assign(&Proj, (float)(U.x * Dot), (float)(U.y * Dot), (float)(U.z * Dot));

    Vector Perp;
    Vec3Assign(&Perp, W.x - Proj.x, W.y - Proj.y, W.z - Proj.z);

    // Retail guards the four normalisations above with an epsilon but not this one. x87: the
    // length and reciprocal stay unrounded.
    double const InvPerpLen =
        1.0f / sqrt(((double)Perp.x * Perp.x + (double)Perp.y * Perp.y) + (double)Perp.z * Perp.z);
    Vector V;
    Vec3Assign(&V, (float)(Perp.x * InvPerpLen), (float)(Perp.y * InvPerpLen),
               (float)(Perp.z * InvPerpLen));

    // cross(U, V), each component one unrounded expression in retail's operand order.
    Vector Cross;
    Vec3Assign(&Cross,
               (float)((double)V.z * U.y - (double)U.z * V.y),
               (float)((double)U.z * V.x - (double)V.z * U.x),
               (float)((double)V.y * U.x - (double)U.y * V.x));

    // Columns U, V and U x V: a right-handed orthonormal frame.
    Matrix const Basis(U.x, V.x, Cross.x,
                       U.y, V.y, Cross.y,
                       U.z, V.z, Cross.z);

    // Written directly into m_Orientation; the engine is not tested for null.
    I3DMathEngine* const Math = Acquire3DMathEngine();
    Math->MatrixToQuaternion(m_Orientation, Basis);

    m_ProjectedPosition = m_PositionDRV.ProjectedPosition;

    // Blend strictly inside the window; at or past it (NaN included) ProjectVelocity.
    Vector Published;
    if (m_SimulationTime < m_ConvergeWindow)
    {
        // FUN_06292340: Out = (To - From) * t + From, t rounded to float though the divide is
        // double; each component rounded once.
        float const T = (float)(m_SimulationTime / m_ConvergeWindow);
        Vector const& From = m_BlendFromVelocity;
        Vector const& To = m_BlendToVelocity;
        Vec3Assign(&Published,
                   (float)(((double)To.x - From.x) * T + From.x),
                   (float)(((double)To.y - From.y) * T + From.y),
                   (float)(((double)To.z - From.z) * T + From.z));
    }
    else
    {
        // The argument is m_SimulationTime, not Dt.
        Published = m_PositionDRV.ProjectVelocity(m_SimulationTime);
    }
    m_Velocity = Published;
}

// FUN_062925c0 (ordinal 1263): should the caller force a resync? Four reasons, in order:
//   1. the simulation clock has outgrown the backoff window;
//   2. the position error exceeds 2.0 * BlendScale;
//   3. the world-X basis error exceeds 0.5 * BlendScale;
//   4. the world-Y basis error exceeds 0.5 * BlendScale.
// Retail quirk kept: reason 1 doubles the backoff and returns without reseeding it; reasons
// 2-4 reseed it to 5.0. Every test is strictly greater (equality and NaN do not resync).
bool CRemotePhysicsSimulation::CheckForSync(Vector const& Param1,
                                            Vector const& Param2,
                                            Quaternion const& Param3)
{
    if (m_SimulationTime > m_SyncBackoff)
    {
        m_SyncBackoff += m_SyncBackoff;
        return true;
    }

    // The tolerance scale: the further apart the two sampled positions already are, the
    // looser every threshold. 1.0 up to 500, then |P1 - P2| * 0.002, clamped at 10.0.
    double BlendScale = 1.0;
    {
        // Retail keeps these at x87 extended precision; double is the closest available.
        double const Dx = (double)Param1.x - Param2.x;
        double const Dy = (double)Param1.y - Param2.y;
        double const Dz = (double)Param1.z - Param2.z;
        // Retail's summation order: (Dz^2 + Dx^2) + Dy^2.
        double const SampleDist = sqrt((Dz * Dz + Dx * Dx) + Dy * Dy);
        if (SampleDist > 500.0)
        {
            BlendScale = SampleDist * 0.002;
            if (BlendScale > 10.0)
                BlendScale = 10.0;
        }
    }

    // Position error against the position dead-reckoner's TrackedPosition; the differences
    // are rounded to float first.
    bool bForceSync = false;
    {
        Vector const& Tracked = m_PositionDRV.TrackedPosition;
        float const Ex = Param2.x - Tracked.x;
        float const Ey = Param2.y - Tracked.y;
        float const Ez = Param2.z - Tracked.z;
        // (Ey^2 + Ez^2) + Ex^2.
        double const PosError = sqrt(((double)Ey * Ey + (double)Ez * Ez) + (double)Ex * Ex);
        if (PosError > G_RemoteSyncPositionTolerance * BlendScale)
            bForceSync = true;
    }

    if (!bForceSync)
    {
        // The engine is not tested for null.
        Matrix Basis;
        I3DMathEngine* const Math = Acquire3DMathEngine();
        Math->QuaternionToMatrix(Basis, Param3);

        // One product reused for both axis tests.
        double const AxisThreshold = G_RemoteSyncOrientTolerance * BlendScale;

        // Column 0 against the world-X dead-reckoner's TrackedPosition. (Xy^2 + Xz^2) + Xx^2.
        Vector const& AxisXTracked = m_OrientAxisXDRV.TrackedPosition;
        float const Xx = Basis.E[0][0] - AxisXTracked.x;
        float const Xy = Basis.E[1][0] - AxisXTracked.y;
        float const Xz = Basis.E[2][0] - AxisXTracked.z;
        double const AxisXError = sqrt(((double)Xy * Xy + (double)Xz * Xz) + (double)Xx * Xx);
        if (AxisXError > AxisThreshold)
        {
            bForceSync = true;
        }
        else
        {
            // Column 1 against the world-Y dead-reckoner. x87: only Yx is rounded to float; Yy
            // and Yz stay unrounded. (Yz^2 + Yy^2) + Yx^2.
            Vector const& AxisYTracked = m_OrientAxisYDRV.TrackedPosition;
            float const Yx = Basis.E[0][1] - AxisYTracked.x;
            double const Yy = (double)Basis.E[1][1] - AxisYTracked.y;
            double const Yz = (double)Basis.E[2][1] - AxisYTracked.z;
            double const AxisYError = sqrt((Yz * Yz + Yy * Yy) + (double)Yx * Yx);
            if (AxisYError > AxisThreshold)
                bForceSync = true;
        }
    }

    if (bForceSync)
    {
        m_SyncBackoff = 5.0;
    }
    return bForceSync;
}

