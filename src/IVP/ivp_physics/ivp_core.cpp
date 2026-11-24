// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_core.h"
#include "ivp_real_object.h"
#include "ivp_environment.h"
#include "ivp_simulation_unit.h"
#include "ivp_controller.h"
#include "../ivp_utility/ivp_u_memory.h"
#include "ivp_ov_element.h"
#include "ivp_cluster_manager.h" // IVP_Cluster_Manager::fire_event_object_revived / _frozen (0634afd0 / 0634af70)
#include "ivp_mindist.h" // IVP_Contact_Point / IVP_Impact_Solver_Long_Term / IVP_ListManager (063464a0 / 063465a0)
#include "../ivp_collision/ivp_listmanager_topology.h" // contact-point helpers (063464a0 / 063465a0)
#include "ivp_contact_point.h"    // IVP_Try_To_Generate_Managed_Friction (06356bc0)
#include "ivp_impact.h"           // IVP_Old_Sync_Rot_Z (06347240 / 063473b0)
#include "ivp_mindist_settings.h" // G_IVP_MindistSettings (DAT_0658ab2c)

#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <new>

// The Robin-Hood key -> value store behind IVP_Core::ObjectHashMap in
// hashmap mode is IVP_VHash_Store: its destructor
// (FUN_06352620) frees the map in IVP_Core_Remove_Object below, and its
// ctor / AddElem / RemoveElem / FindElem (FUN_063525f0 / 063526d0 / 063527e0 /
// 06352930) serve find_object_hash_entry() / register_object_hash_entry() /
// unregister_object_hash_entry() further down. ObjectHashMap stays void*
// because the single-slot mode stores a candidate pointer in it directly.
#include "../ivp_utility/ivp_vhash.h"
#include "../ivp_utility/ivp_signed_byte.h" // IVP_SignedLowByte


// IVP_U_Memory (ivp_utility/ivp_u_memory.h): the arena's mark/unmark is a ++/-- of
// its State (+0x10), done directly at the call sites below.

// IVP_Real_Object::mindist_list (+0x28) heads the object's contact-point nodes:
// the node is the record's per-object link (IVP_Contact_Point +0x08 / +0x1c),
// node+0 is the next node and the int16 at node+0x0c is record - node (Delta1 /
// Delta2).
namespace
{
    // A .rdata float constant by its exact bits (some differ from the nearest
    // decimal literal by one ulp).
    float IVP_Core_Float_From_Bits(uint32_t Bits)
    {
        float Value;
        std::memcpy(&Value, &Bits, sizeof(Value));
        return Value;
    }

    char* IVP_Contact_Node_Next(char* Node)
    {
        return *reinterpret_cast<char**>(Node);
    }

    IVP_Contact_Point* IVP_Contact_Node_Record(char* Node)
    {
        return reinterpret_cast<IVP_Contact_Point*>(Node + *reinterpret_cast<int16_t*>(Node + 0xc)); // signed word
    }

    // FUN_06346770 (thiscall): the long-term impact info's constructor body
    // -- Status (+0x52) word = 0, the dword at +0x54 &= 0xfffffc00. 063464a0
    // inlines the same two stores (:06346534-:0634654e).
    void IVP_Mindist_Reset_Long_Term_State(IVP_Impact_Solver_Long_Term* Info)
    {
        uint32_t* const Dword54 = reinterpret_cast<uint32_t*>(&Info->StateProcessed);
        *Dword54 &= 0xfffffc00u;
        Info->Status = 0;
    }
}

// ============================================================================
// Helper Subroutines
// ============================================================================


// IVP_Core_StopMovementWithoutCollisionRecheck: Halts movement, zeros accumulators and velocities for static cores
void __fastcall IVP_Core_StopMovementWithoutCollisionRecheck(IVP_Core* Core)
{
    Core->MovementState = (Core->MovementState & 0xFFFFFF08) | 8;

    Core->RotSpeedChange.x = 0.0f;
    Core->RotSpeedChange.y = 0.0f;
    Core->RotSpeedChange.z = 0.0f;

    Core->SpeedChange.x = 0.0f;
    Core->SpeedChange.y = 0.0f;
    Core->SpeedChange.z = 0.0f;

    Core->RotSpeed.x = 0.0f;
    Core->RotSpeed.y = 0.0f;
    Core->RotSpeed.z = 0.0f;

    Core->Speed.x = 0.0f;
    Core->Speed.y = 0.0f;
    Core->Speed.z = 0.0f;

    Core->DeltaWorldFCore.x = 0.0f;
    Core->DeltaWorldFCore.y = 0.0f;
    Core->DeltaWorldFCore.z = 0.0f;

    // FUN_06346e70 with the environment's {delta_time, inv_delta_time}
    // (+0xAC/+0xB0), no null test.
    Core->synchronize_simulation_time_snapshot(Core->Environment->delta_time,
                                                 Core->Environment->inv_delta_time);
}

// ============================================================================
// FUN_063464a0 / IVP_Core_RefreshContactPoints
// ============================================================================

// FUN_063464a0, reached only from 063465a0's
// fixed-core arm. Port-local name. For every contact point of every object
// of a core that has just become fixed: flag its friction system (low byte of
// +0x44 := 1); if the OTHER side's core is fixed too (flags byte & 0xc) the
// contact point is destroyed (06355200 + operator delete), else the other
// core's objects are put back into simulation (06347db0) and the contact
// point is recomputed (0635a1e0, 06358c50 into a reused stack long-term
// info, 063562d0).
static void IVP_Core_Recalc_Contact_Points_Of_Fixed_Core(IVP_Core* Core)
{
    IVP_U_Memory* const Arena = Core->Environment->sim_unit_mem; // :063464a9-:063464ac (+0xa4)
    ++Arena->State;                                                      // :063464b2

    IVP_Impact_Solver_Long_Term Info; // the stack long-term info (ESP+0x14), only +0x52 / +0x54 reset per use
    for (int32_t ObjIndex = static_cast<int32_t>(Core->Objects.n_elems) - 1; ObjIndex >= 0; --ObjIndex) // count read once (:063464b8)
    {
        char* Node = static_cast<char*>(Core->Objects.elems[ObjIndex]->mindist_list); // :063464cc-:063464d2
        while (Node != nullptr)
        {
            IVP_Contact_Point* const Record = IVP_Contact_Node_Record(Node);
            char* const Next = IVP_Contact_Node_Next(Node); // read first (:063464e1)

            IVP_ListManager* const FrictionSystem = Record->ListManagerOwner;              // :063464e5
            FrictionSystem->union_find_necessary = 1;                                      // :063464e8-:063464f4

            IVP_Core* OtherCore = Record->Object1->core;                                   // :063464f7-:063464fa
            if (OtherCore == Core)
            {
                OtherCore = Record->Object2->core;                                         // :06346504-:06346507
            }

            if ((static_cast<uint8_t>(OtherCore->Flags) & IVP_CORE_PHYSICAL_UNMOVEABLE) != 0)                      // :0634650d
            {
                if (Record != nullptr)                                                     // :06346512
                {
                    IVP_UnlinkPairRecordFromObjects(Record); // FUN_06355200
                    delete Record;                           // FUN_06391d5a
                }
            }
            else
            {
                OtherCore->ensure_objects_in_simulation();  // :06346528 FUN_06347db0 (ECX = the other core)
                IVP_UpdatePairRecordContactFrame(Record);   // :0634652f FUN_0635a1e0
                IVP_Mindist_Reset_Long_Term_State(&Info);   // :06346534-:0634654e
                IVP_PopulatePairRecordIntoOwner(Record, &Info); // :06346552 FUN_06358c50
                IVP_ComputePairRecordMassFactor(Record);    // :06346559 FUN_063562d0
            }
            Node = Next;
        }
    }

    --Arena->State;        // :0634657a-:06346583 (arena re-read through the environment)
    Arena->reset_chunks(); // :06346587 FUN_06344070
}

// IVP_Core_RefreshContactPoints (063465a0), the library's
// IVP_Core::values_changed_recalc_redundants. Name kept as the raw address
// because ivp_real_object.cpp calls it so; rename with that caller.
void __fastcall IVP_Core_RefreshContactPoints(IVP_Core* Core)
{
    // :063465a9-:063465ca: inertia terms inside an arena bracket.
    ++Core->Environment->sim_unit_mem->State;
    Core->RecomputeInverseInertia();                    // FUN_06347410
    --Core->Environment->sim_unit_mem->State;
    Core->Environment->sim_unit_mem->reset_chunks(); // FUN_06344070

    // :063465cf-:0634660b: the movement byte every object takes over.
    int32_t ObjectMovement;
    if ((static_cast<uint8_t>(Core->Flags) & IVP_CORE_PHYSICAL_UNMOVEABLE) != 0)
    {
        Core->MovementState = (Core->MovementState & 0xffffff08u) | 0x8u;
        ObjectMovement = 0x10;
    }
    else
    {
        // Signed compare of the low byte.
        if (IVP_SignedLowByte(Core->MovementState) > 8)
        {
            Core->MovementState = (Core->MovementState & 0xffffff08u) | 0x8u;
        }
        ObjectMovement = static_cast<int8_t>(Core->MovementState & 0xffu); // re-read, sign-extended
    }

    // :0634660e-:06346630: low byte of every object's flags := ObjectMovement.
    for (int32_t ObjIndex = static_cast<int32_t>(Core->Objects.n_elems) - 1; ObjIndex >= 0; --ObjIndex)
    {
        IVP_Real_Object* const Object = Core->Objects.elems[ObjIndex];
        Object->flags = ((Object->flags ^ static_cast<uint32_t>(ObjectMovement)) & 0xffu) ^ Object->flags;
    }

    if ((static_cast<uint8_t>(Core->Flags) & IVP_CORE_PHYSICAL_UNMOVEABLE) != 0) // :06346632 (re-tested)
    {
        // :0634672b-:06346732.
        IVP_Core_StopMovementWithoutCollisionRecheck(Core);
        IVP_Core_Recalc_Contact_Points_Of_Fixed_Core(Core);
        // :06346737-:0634675c: retail then walks every object's contact-node
        // list to its end without reading or writing anything else -- a loop
        // whose body was optimised away. No observable effect; not reproduced.
        return;
    }

    // :0634663d-:06346721.
    Core->wake_or_ensure_simulation(); // FUN_06347d80
    ++Core->Environment->sim_unit_mem->State;
    for (int32_t ObjIndex = static_cast<int32_t>(Core->Objects.n_elems) - 1; ObjIndex >= 0; --ObjIndex) // count read once (:06346651)
    {
        char* Node = static_cast<char*>(Core->Objects.elems[ObjIndex]->mindist_list); // :06346661-:06346667
        if (Node == nullptr)
        {
            continue;
        }
        // The friction system of the first contact point; every other one is
        // merged into it (:06346672-:06346676, :06346684-:0634668b).
        IVP_ListManager* const FrictionSystem = IVP_Contact_Node_Record(Node)->ListManagerOwner;
        do
        {
            IVP_Contact_Point* const Record = IVP_Contact_Node_Record(Node);
            if (Record->ListManagerOwner != FrictionSystem)
            {
                FrictionSystem->MergeFrom(Record->ListManagerOwner); // FUN_063555b0
            }

            // :06346690-:063466d6: a fresh stack long-term info per contact
            // point (three trivial vector ctor iterators 06273bc0, then 06346770).
            IVP_Impact_Solver_Long_Term Info;
            IVP_Mindist_Reset_Long_Term_State(&Info);
            IVP_UpdatePairRecordContactFrame(Record);       // :063466dd FUN_0635a1e0
            IVP_PopulatePairRecordIntoOwner(Record, &Info);  // :063466e9 FUN_06358c50
            IVP_ComputePairRecordMassFactor(Record);         // :063466f0 FUN_063562d0

            Node = IVP_Contact_Node_Next(Node); // :063466f5, read after the calls
        } while (Node != nullptr);
    }
    --Core->Environment->sim_unit_mem->State;        // :0634670f-:06346718
    Core->Environment->sim_unit_mem->reset_chunks(); // :0634671c
}

// IVP_Core_UnionFindGetFather: IVP_Core::get_master_core() equivalent. Cores can be merged
// (welded together, e.g. by a fixed constraint); when that happens one core's
// MergedCoreWhichReplacedIt is linked to point at the other (see FUN_06348ad0,
// which unions two cores' chains together and zero-initializes the field for a
// fresh group). This walks that chain to its terminal (null-linked) node, i.e.
// the current representative/master core for the merged group. A null input
// core is passed straight through, matching retail's explicit ECX==0 check.
IVP_Core* __fastcall IVP_Core_UnionFindGetFather(IVP_Core* Core)
{
    if (Core == nullptr)
        return Core;

    IVP_Core* Master = Core;
    while (Master->MergedCoreWhichReplacedIt != nullptr)
        Master = Master->MergedCoreWhichReplacedIt;
    return Master;
}

// ============================================================================
// Core Management (Attachment / Detachment)
// ============================================================================

void IVP_Core_Add_Object(IVP_Core* Core, IVP_Real_Object* Object)
{
    Core->Objects.Add(Object);
}

void IVP_Core_Remove_Object(IVP_Core* Core, IVP_Real_Object* Object)
{
    int RemovedIndex = Core->Objects.RemoveElement(Object);

    if (Core->Objects.n_elems == 0)
    {
        // FUN_06346f20 compares MovementState's low byte against 8 SIGNED (0x80-0xFF
        // count as "< 8").
        if (static_cast<int8_t>(Core->MovementState & 0xFF) < 8)
        {
            Core->freeze_simulation_core(); // :06346f87 FUN_06347040 (its object loop is empty here)
        }

        // IVP_Core::~IVP_Core() (FUN_063477e0), four steps before the vector buffers
        // (which `delete Core` frees through IVP_U_Vector's destructor).

        // Step 1: dequeue Core from Environment->event_queue (clears Flags 0x30).
        IVP_Environment_Dequeue_Core(Core->Environment, Core);

        // Step 2: (Flags & 0xC) == 4 -> destroy ObjectHashMap (FUN_06352620) and
        // free it (FUN_06391d5a). Otherwise every controller gets
        // core_is_going_to_be_deleted_event(Core) (vtable slot 0).
        if ((Core->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) == IVP_CORE_PHYSICAL_UNMOVEABLE_TRUE)
        {
            if (Core->ObjectHashMap != nullptr)
            {
                ((IVP_VHash_Store*)Core->ObjectHashMap)->~IVP_VHash_Store(); // FUN_06352620
                free(Core->ObjectHashMap); // FUN_06391d5a
                Core->ObjectHashMap = nullptr;
            }
        }
        else
        {
            int ControllerCount = Core->Controllers.Len();
            for (int i = ControllerCount - 1; i >= 0; --i) // :06347839-06347852, count read once, elems re-read, no null test
            {
                Core->Controllers.ElementAt(i)->core_is_going_to_be_deleted_event(Core);
            }
        }

        // Step 3: detach from the owning sim unit (FUN_06348cb0).
        if (Core->OwningSimUnit != nullptr)
        {
            IVP_Simulation_Unit_Detach_Core(Core->OwningSimUnit, Core);
        }

        // Step 4: free SpinClipping (+0x4c) unconditionally (FUN_06391d5a).
        free(Core->SpinClipping);
        Core->SpinClipping = nullptr;

        delete Core;
    }
}

// ============================================================================
// FUN_06347550 + FUN_06347660: IVP_Core's real constructor (steps in the header).
// ============================================================================
IVP_Core::IVP_Core(IVP_Real_Object* Owner, const IVP_U_Quat* InitialOrientation,
                    const IVP_U_Point* InitialPosition, uint32_t FlagsHi, uint32_t FlagsLo)
{
    // Step 1: zero all 0x6F dwords.
    memset(this, 0, sizeof(IVP_Core));

    // Step 2: re-initialise Objects in place (placement new, so elems points at
    // this object's own inline slot, not a temporary's), add Owner, take its
    // environment.
    new (&Objects) IVP_U_Vector_Fixed<IVP_Real_Object, 1>();
    Objects.Add(Owner);
    Environment = Owner->environment; // IVP_Object::environment, +0x18

    // Step 3: the owning sim unit. Only the ctor is behind retail's operator_new
    // null test; the Add and the State store run on the result unconditionally
    // (a nothrow new-expression has the same shape, EH funclet included).
    IVP_Simulation_Unit* Unit = new (std::nothrow) IVP_Simulation_Unit(); // operator_new(0x24) + FUN_06348e10
    OwningSimUnit = Unit;
    Unit->Cores.Add(this);                // FUN_06348ec0
    OwningSimUnit->State = (OwningSimUnit->State & 0xFFFFFF08u) | 8u;

    // Step 4: register with the environment's standard gravity controller
    // (IVP_Environment+0x00, FUN_06349180).
    register_controller(Environment->standard_gravity_controller);
    MovementState = (MovementState & 0xFFFFFF08u) | 8u; // after the register

    // Step 5 (FUN_06347660): Flags bits [3:2] from FlagsHi, then FUN_06349400 on
    // Environment->+0x08 with OwningSimUnit, unconditionally.
    Flags = (Flags & ~0xCu) | ((FlagsHi & 0x3u) << 2);
    Environment->sim_units_manager->add_sim_unit_to_manager(OwningSimUnit); // FUN_06349400

    // Step 6: initial orientation, normalized, duplicated into QWorldFCoreNext.
    QWorldFCore = *InitialOrientation;
    QWorldFCore.normize(); // FUN_06350fb0
    QWorldFCoreNext = QWorldFCore;

    // Step 7: initial position, zero pose delta, rotation-only matrix refresh,
    // then the matrix's own translation slot gets the same position again.
    // PosWorldFCore/DeltaWorldFCore are the plain 3-float `Vector` type
    // (common_ini.h), not IVP_U_Point, hence the field-by-field copy.
    PosWorldFCore.x = InitialPosition->x;
    PosWorldFCore.y = InitialPosition->y;
    PosWorldFCore.z = InitialPosition->z;
    DeltaWorldFCore.x = 0.0f;
    DeltaWorldFCore.y = 0.0f;
    DeltaWorldFCore.z = 0.0f;
    QWorldFCore.set_matrix(&MWorldFCore); // FUN_06350bc0, writes rows only, not vv
    MWorldFCore.vv.set(InitialPosition);

    // Step 8: retail's default radii.
    SetRadius(1000.0f, 1000.0f); // FUN_06346870

    // Step 9: Flags bits [1:0] from FlagsLo.
    Flags = (Flags & ~0x3u) | (FlagsLo & 0x3u);
}

// FUN_06346870.
void IVP_Core::SetRadius(float NewUpperLimitRadius, float NewMaxSurfaceDeviation)
{
    UpperLimitRadius = NewUpperLimitRadius;
    MaxSurfaceDeviation = NewMaxSurfaceDeviation;
}

// The tail of FUN_06347d80 (:06347d9b): FUN_063494f0 on OwningSimUnit, with
// no null test on the unit or the cores' environments.
void IVP_Core::ensure_in_simulation()
{
    IVP_Simulation_Unit_Update_Cores_Last_Motion_Time(OwningSimUnit); // FUN_063494f0
}

// FUN_063470e0. Retail computes the (Sum, IntPart, Frac) triple twice, once per
// target pair; the port computes it once.
void IVP_Core::update_time_of_last_motion()
{
    IVP_Environment* Env = this->Environment;

    // Sum = offset + base stays on the x87 stack; ftol truncates it; the int
    // part is float(int); the fraction is float(Sum - int) from the unrounded Sum.
    const double Sum = (double)(Env->current_time.time_offset) + Env->current_time.time_base;

    const int32_t IntValue = static_cast<int32_t>(Sum); // MSVCRT ftol, truncates toward 0
    const float IntPart = (float)(IntValue);
    const float Frac = (float)(Sum - (double)(IntValue));

    TimeOfLastMotion.time_base = IntPart;
    TimeOfLastMotion.time_offset = Frac;
    TimeOfLastLongMotion.time_base = IntPart;
    TimeOfLastLongMotion.time_offset = Frac;
}

// FUN_06346e70.
void IVP_Core::synchronize_simulation_time_snapshot(float DeltaTime, float InvDeltaTime)
{
    (void)DeltaTime; // never read by retail
    IDeltaTime = InvDeltaTime;

    QWorldFCoreNext = QWorldFCore; // struct copy, 4 floats (x/y/z/w)

    Speed.x = 0.0f;
    Speed.y = 0.0f;
    Speed.z = 0.0f;

    DeltaWorldFCore.x = 0.0f;
    DeltaWorldFCore.y = 0.0f;
    DeltaWorldFCore.z = 0.0f;

    AbsOmega = 0.0f;
    CurrentSpeed = 0.0f;
    MaxSurfaceRotSpeed = 0.0f;

    RotAxisWS.x = 1.0f;
    RotAxisWS.y = 0.0f;
    RotAxisWS.z = 0.0f;
}

// FUN_06347170 (no null tests, as retail).
void IVP_Core::mark_active_and_sync_objects()
{
    MovementState = (MovementState & 0xFFFFFF01u) | 1u;

    const IVP_Time Now = Environment->current_time;
    const double DeltaTime = Environment->delta_time;            // env+0xac
    const int32_t DeltaInt = static_cast<int32_t>(DeltaTime);    // ftol (truncation)
    const float DeltaIntFloat = (float)(DeltaInt);               // local_10
    const double DeltaFrac = DeltaTime - (double)(DeltaInt);
    const double Shifted = (((double)(Now.time_base) - DeltaIntFloat) + Now.time_offset) - DeltaFrac;
    const int32_t ShiftedInt = static_cast<int32_t>(Shifted);    // ftol
    TimeOfLastPsi.time_base = (float)(ShiftedInt);
    TimeOfLastPsi.time_offset = (float)(Shifted - (double)(ShiftedInt));

    update_time_of_last_motion();                                // FUN_063470e0

    TimeOfLastPsi = Now;                                         // :063471fe

    for (int32_t Index = static_cast<int32_t>(Objects.n_elems) - 1; Index >= 0; --Index)
    {
        IVP_Real_Object* const Obj = Objects.elems[Index];
        Obj->flags = (Obj->flags & 0xFFFFFF01u) | 1u;
        Obj->recheck_ov_element_as_moving();                     // FUN_063454e0
    }
}

// FUN_063461a0 (thiscall, RET 0x10; args Lever, Lin, Ang, Out):
//   C = Ang x Lever stays extended: Cx = Ang.y*L.z - Ang.z*L.y,
//   Cy = Ang.z*L.x - Ang.x*L.z, Cz = Ang.x*L.y - Ang.y*L.x.
//   X = (r0.z*Cz + r0.y*Cy) + r0.x*Cx      -> stored to Out.x
//   Y = (r1.z*Cz + r1.y*Cy) + r1.x*Cx      -> rounded, stored to Out.y
//   Z = (r2.z*Cz + r2.y*Cy) + r2.x*Cx      -> stored to Out.z, kept extended
//   Out.y = float(float(Y) + Lin.y); Out.z = float(Z + Lin.z);
//   Out.x = float(Lin.x + Out.x) -- Out.x is read back from memory.
// The rows are MWorldFCore (+0xf4 / +0x104 / +0x114).
void IVP_Core::calc_point_speed_ws(const Vector* LeverArmOS, const Vector* LinearVelocityWS,
                                    const Vector* AngularVelocityOS, Vector* OutWorldSpeed) const
{
    const double CrossX = (double)(AngularVelocityOS->y) * LeverArmOS->z -
                          (double)(AngularVelocityOS->z) * LeverArmOS->y;
    const double CrossY = (double)(AngularVelocityOS->z) * LeverArmOS->x -
                          (double)(AngularVelocityOS->x) * LeverArmOS->z;
    const double CrossZ = (double)(AngularVelocityOS->x) * LeverArmOS->y -
                          (double)(AngularVelocityOS->y) * LeverArmOS->x;

    const IVP_U_Point& Row0 = MWorldFCore.rows[0];
    const IVP_U_Point& Row1 = MWorldFCore.rows[1];
    const IVP_U_Point& Row2 = MWorldFCore.rows[2];

    OutWorldSpeed->x = (float)((Row0.z * CrossZ + Row0.y * CrossY) + Row0.x * CrossX);
    const float RotatedY = (float)((Row1.z * CrossZ + Row1.y * CrossY) + Row1.x * CrossX);
    OutWorldSpeed->y = RotatedY;
    const double RotatedZ = (Row2.z * CrossZ + Row2.y * CrossY) + Row2.x * CrossX;
    OutWorldSpeed->z = (float)(RotatedZ);

    const float NewY = (float)((double)(RotatedY) + LinearVelocityWS->y);
    const float NewZ = (float)(RotatedZ + LinearVelocityWS->z);
    const float NewX = (float)((double)(LinearVelocityWS->x) + OutWorldSpeed->x);
    OutWorldSpeed->z = NewZ;
    OutWorldSpeed->x = NewX;
    OutWorldSpeed->y = NewY;
}

// FUN_06346be0: calc_point_speed_ws with this core's own Speed/RotSpeed.
void IVP_Core::calc_speed_at_object_position(const Vector* LeverArmOS, Vector* OutWorldSpeed) const
{
    calc_point_speed_ws(LeverArmOS, &Speed, &RotSpeed, OutWorldSpeed);
}

// FUN_06346cf0 (see the header).
void IVP_Core::calc_impulse_deltas(const Vector* LeverArmOS, const Vector* PointImpulseOS,
                                    const Vector* PointImpulseWS,
                                    Vector* OutLinearDelta, Vector* OutAngularDelta) const
{
    // x87:
    //   Cx = I.z*L.y - I.y*L.z, Cy = I.x*L.z - I.z*L.x stay extended;
    //   Cz = I.y*L.x - I.x*L.y is rounded to float first.
    //   OutAngular = (float(Cx*IRI.x), float(Cy*IRI.y), float(float(Cz)*IRI.z)),
    //   stored z, x, y; only then OutLinear = float(WS.c * InvMass), stored z, x, y.
    const double CrossX = (double)(PointImpulseOS->z) * LeverArmOS->y -
                          (double)(PointImpulseOS->y) * LeverArmOS->z;
    const double CrossY = (double)(PointImpulseOS->x) * LeverArmOS->z -
                          (double)(PointImpulseOS->z) * LeverArmOS->x;
    const float CrossZ = (float)((double)(PointImpulseOS->y) * LeverArmOS->x -
                                            (double)(PointImpulseOS->x) * LeverArmOS->y);

    const float AngularY = (float)(CrossY * InvRotInertia.y);
    OutAngularDelta->z = CrossZ * InvRotInertia.z;
    OutAngularDelta->x = (float)(CrossX * InvRotInertia.x);
    OutAngularDelta->y = AngularY;

    const float LinearY = PointImpulseWS->y * InvMass;
    const float LinearZ = PointImpulseWS->z * InvMass;
    const float LinearX = PointImpulseWS->x * InvMass;
    OutLinearDelta->z = LinearZ;
    OutLinearDelta->x = LinearX;
    OutLinearDelta->y = LinearY;
}

// FUN_06346790. Caller FUN_06358560 passes, per core, the same normal in object
// space (vimult) and world space.
//   1. Flags & 0x300 (IVP_CORE_PINNED) -> 1.0f.
//   2. calc_impulse_deltas, then calc_point_speed_ws -- the chain
//      PushContactPoint uses too.
//   3. 1.0f / |PointVelDelta| (FUN_06347e60, real_length): the response per
//      unit impulse is 1/mass, so this is a per-core MASS (FUN_06358630 forms
//      the reduced mass 2*m0*m1/(m0+m1) from it).
// Returned unrounded (ST0); the only caller rounds to float.
double IVP_Core::calc_effective_mass_along_direction(const Vector* LeverArmOS,
                                                       const Vector* ImpulseDirOS,
                                                       const Vector* ImpulseDirWS) const
{
    if ((Flags & IVP_CORE_PINNED) != 0)
        return (double)(1.0f); // DAT_063a53d0

    Vector LinDelta, AngDelta;
    calc_impulse_deltas(LeverArmOS, ImpulseDirOS, ImpulseDirWS, &LinDelta, &AngDelta);

    IVP_U_Point PointVelDelta;
    calc_point_speed_ws(LeverArmOS, &LinDelta, &AngDelta, reinterpret_cast<Vector*>(&PointVelDelta));

    return (double)(1.0f) / PointVelDelta.real_length();
}

// FUN_063463f0 (formula in the header). x87: z*z stays on the FPU; y*y and x*x
// are spilled as floats; each term is rounded to float; the result is returned
// unrounded. TermY replaces TermX only when greater (less/equal/unordered
// keeps TermX), then TermZ likewise.
double IVP_Core::calc_conservative_effective_mass(const Vector* LeverArm) const
{
    const double Z2 = (double)(LeverArm->z) * LeverArm->z;
    const float Y2 = (float)((double)(LeverArm->y) * LeverArm->y);
    const float SumYZ = (float)(Y2 + Z2);
    const float X2 = (float)((double)(LeverArm->x) * LeverArm->x);
    const double SumZX = Z2 + X2;

    const float TermZ = (float)(((double)(X2) + Y2) * InvRotInertia.z);
    const float TermX = (float)((double)(SumYZ) * InvRotInertia.x);
    const float TermY = (float)(SumZX * InvRotInertia.y);

    float Max = TermX;
    if (TermY > TermX)
        Max = TermY;
    if (TermZ > Max)
        Max = TermZ;

    if ((Flags & IVP_CORE_PINNED) != 0)
        return 1.0 / ((double)(Max) + 1.0);
    return 1.0 / ((double)(Max) + InvMass);
}

// FUN_06347240. Callees:
//   - FUN_06347380: IVP_U_Memory::GetMem (the 0x24-byte IVP_Old_Sync_Rot_Z record).
//   - FUN_06351250: IVP_U_Quat::set_inv_mult_quat (FUN_063510c0 set_invert_unit,
//     then a Hamilton product).
//   - FUN_06350df0: IVP_U_Quat::set_interpolate_quat (slerp with an epsilon
//     branch, DAT_063a5630 = 0x3f7fbe77, else lerp + two Newton renormalisation
//     passes); called with `this` aliased to q1, which the method tolerates.
//   - FUN_06350bc0: IVP_U_Quat::set_matrix.
//   - FUN_0628be60: add_mult, dst.c = float(scale*dir.c + base.c), inline below.
//
// Retail control flow (Ipion IVP_Core::synchronize_with_rot_z):
//   OldSyncInfo = Env->sim_unit_mem (+0xa4)
//     ->GetMem(0x24) (FUN_06347380); record +0x20 = 0; record +0x00..+0x08 =
//     RotSpeed; record +0x10 = QWorldFCoreNext (raw 16 bytes). Record +0x0c is
//     not written.
//   DeltaQuat = set_inv_mult_quat(&QWorldFCore, &QWorldFCoreNext),
//     unconditionally, before the interpolation below.
//   Alpha = float((((Etb - tb) + Eto) - to) * IDeltaTime)
//     (one rounding); QWorldFCoreNext = set_interpolate_quat(&QWorldFCore,
//     &QWorldFCoreNext, Alpha); set_matrix(&MWorldFCore).
//   The same elapsed chain rounded to float WITHOUT the
//     IDeltaTime factor is the scale of add_mult(&MWorldFCore.vv,
//     &PosWorldFCore, &DeltaWorldFCore) -- DeltaWorldFCore is the linear
//     velocity, so the scale is seconds, not Alpha.
//   If (byte Flags & IVP_CORE_ROT_INERTIAS_ARE_EQUAL) == 0: TwoIDT = float(IDT +
//     IDT); RotSpeed.c = float(_CIasin(DeltaQuat.c) * TwoIDT) for x, y, z.
// The record is arena memory (the destructor never frees +0x1b4); it is read
// by the impact system (ivp_impact.cpp) and restored by FUN_063473b0.
void IVP_Core::recalc_interpolated_frame()
{
    IVP_Environment* Env = this->Environment;

    // The sync record, from the environment's frozen-core arena (Env+0xa4),
    // filled before anything below changes the core.
    IVP_Old_Sync_Rot_Z* Sync =
        static_cast<IVP_Old_Sync_Rot_Z*>(Env->sim_unit_mem->GetMem(static_cast<int>(sizeof(IVP_Old_Sync_Rot_Z))));
    OldSyncInfo = Sync;
    Sync->WasPushedDuringImpactSystem = 0;
    Sync->OldSyncRotSpeed.x = RotSpeed.x;
    Sync->OldSyncRotSpeed.y = RotSpeed.y;
    Sync->OldSyncRotSpeed.z = RotSpeed.z;
    Sync->OldSyncQWorldFCore = QWorldFCoreNext;

    // Computed before QWorldFCoreNext is advanced; read only under the flag
    // test at the end.
    IVP_U_Quat DeltaQuat;
    DeltaQuat.set_inv_mult_quat(&QWorldFCore, &QWorldFCoreNext);

    // Retail evaluates the same x87 chain twice.
    const double ElapsedExt =
        (((double)(Env->current_time.time_base) - TimeOfLastPsi.time_base) + Env->current_time.time_offset) -
        TimeOfLastPsi.time_offset;
    const float Alpha = (float)(ElapsedExt * IDeltaTime);
    const float Elapsed = (float)(ElapsedExt);

    QWorldFCoreNext.set_interpolate_quat(&QWorldFCore, &QWorldFCoreNext, Alpha); // FUN_06350df0
    QWorldFCoreNext.set_matrix(&MWorldFCore);                                   // FUN_06350bc0

    // FUN_0628be60 add_mult(&MWorldFCore.vv, &PosWorldFCore,
    // &DeltaWorldFCore, Elapsed): the velocity times the elapsed time, one
    // rounding per component.
    MWorldFCore.vv.y = (float)((double)(Elapsed) * DeltaWorldFCore.y + PosWorldFCore.y);
    MWorldFCore.vv.z = (float)((double)(Elapsed) * DeltaWorldFCore.z + PosWorldFCore.z);
    MWorldFCore.vv.x = (float)((double)(Elapsed) * DeltaWorldFCore.x + PosWorldFCore.x);

    if ((Flags & IVP_CORE_ROT_INERTIAS_ARE_EQUAL) == 0)
    {
        const float TwoIDeltaTime = (float)((double)(IDeltaTime) + IDeltaTime);
        RotSpeed.x = (float)(std::asin((double)(DeltaQuat.x)) * TwoIDeltaTime);
        RotSpeed.y = (float)(std::asin((double)(DeltaQuat.y)) * TwoIDeltaTime);
        RotSpeed.z = (float)(std::asin((double)(DeltaQuat.z)) * TwoIDeltaTime);
    }
}

// FUN_063563d0, Ipion IVP_Core::grow_friction_system:
//   Result = 0; objects from n_elems - 1 (read once).
//   per object, Syn = synapse_list_head (+0x20); per synapse, Mdist = Syn +
//     (int16)Syn->mindist_offset (+0x18) and Next = Syn->next (+0x8), read
//     before anything else.
//   StateFlags & 0x3000 -> next synapse.
//   Other = synapse[0].l_obj->core; when that is this core, synapse[1]'s.
//   Other fixed (flags byte & 0xc): if this core is fixed too, delete Mdist
//     (slot 4 with 1); otherwise nothing.
//   Other movable: Other->+0x5c (0633ba90) non-null -> skip;
//     06352cf0 (DispatchNarrowPhaseSolve); StateFlags & 0xc000 -> skip; go on
//     only when the gap (+0x54) is below DAT_0658ab2c
//     (max_distance_for_friction) or unordered.
//   ++sim_unit_mem->State (the arena at env +0xa4);
//     06356bc0(Mdist, &Fs, &HavingNew, OwningSimUnit, 1); --State and
//     reset_chunks (06344070) on the arena re-read through the environment;
//     HavingNew == 1 -> Result = 1, Other->update_time_of_last_motion()
//     (063470e0).
int IVP_Core::grow_friction_system()
{
    int Result = 0;
    for (int32_t ObjIndex = static_cast<int32_t>(Objects.n_elems) - 1; ObjIndex >= 0; --ObjIndex)
    {
        IVP_Synapse_Real* Syn = Objects.elems[ObjIndex]->synapse_list_head;
        while (Syn != nullptr)
        {
            IVP_Ledge_Mindist* const Mdist = Syn->get_mindist();
            IVP_Synapse_Real* const Next = Syn->next;
            if ((Mdist->StateFlags & 0x3000u) == 0)
            {
                IVP_Core* Other = Mdist->synapse[0].l_obj->core;
                if (Other == this)
                {
                    Other = Mdist->synapse[1].l_obj->core;
                }

                if ((static_cast<uint8_t>(Other->Flags) & IVP_CORE_PHYSICAL_UNMOVEABLE) != 0)
                {
                    if ((static_cast<uint8_t>(Flags) & IVP_CORE_PHYSICAL_UNMOVEABLE) != 0)
                    {
                        delete Mdist; // slot 4 (deleting dtor) with 1
                    }
                }
                else if (Other->ObjectHashMap == nullptr) // FUN_0633ba90
                {
                    Mdist->DispatchNarrowPhaseSolve(); // FUN_06352cf0
                    if ((Mdist->StateFlags & 0xc000u) == 0 &&
                        !(Mdist->SeparationGap >= G_IVP_MindistSettings.max_distance_for_friction))
                    {
                        ++Environment->sim_unit_mem->State;
                        IVP_ListManager* FrictionSystem = nullptr;
                        int32_t HavingNew = 0;
                        IVP_Try_To_Generate_Managed_Friction(Mdist, &FrictionSystem, &HavingNew, OwningSimUnit, 1);
                        IVP_U_Memory* const Arena = Environment->sim_unit_mem;
                        --Arena->State;
                        Arena->reset_chunks(); // FUN_06344070
                        if (HavingNew == 1)
                        {
                            Result = 1;
                            Other->update_time_of_last_motion(); // FUN_063470e0
                        }
                    }
                }
            }
            Syn = Next;
        }
    }
    return Result;
}

// FUN_0634b480:
//   every object's flags (+0x78) low byte := 1, n-1..0.
//   FUN_06347170 (mark_active_and_sync_objects).
//   Alpha = ((f[env+0x100] - f[env+0xf8]) + f[env+0x104]) - f[env+0xfc], on
//     the x87 stack.
//   Alpha > 1e-10f (DAT_063a53d8, ordered) -> {(float)Alpha, (float)(1.0 /
//     Alpha)} with the unrounded Alpha as divisor; else {env->delta_time
//     (+0xac), env->inv_delta_time (+0xb0)}.
//   FUN_06346e70(&pair).
//   Result = FUN_063563d0 (grow_friction_system).
//   per object n-1..0: event {Environment, Obj} ->
//     FUN_0634afd0 on env->cluster_manager (+0xc); a second stack event
//     {Environment (re-read), Obj} -> FUN_0634cf30 on the environment.
int IVP_Core::revive_pass()
{
    for (int32_t ObjIndex = static_cast<int32_t>(Objects.n_elems) - 1; ObjIndex >= 0; --ObjIndex)
    {
        IVP_Real_Object* const Obj = Objects.elems[ObjIndex];
        Obj->flags = (Obj->flags & 0xFFFFFF01u) | 1u;
    }

    mark_active_and_sync_objects(); // FUN_06347170

    IVP_Environment* const Env = this->Environment;
    const double CurrentTimeBase = (double)(Env->current_time.time_base);
    const double CurrentTimeOffset = (double)(Env->current_time.time_offset);
    const double Alpha = (((double)(Env->next_psi_time_base) - CurrentTimeBase) +
                          (double)(Env->next_psi_time_offset)) - CurrentTimeOffset;

    float DeltaTime;
    float InvDeltaTime;
    if (Alpha > (double)(1.0e-10f)) // DAT_063a53d8 = 0x2edbe6ff
    {
        DeltaTime = (float)(Alpha);
        InvDeltaTime = (float)(1.0 / Alpha); // DAT_063a53d0 = 1.0f
    }
    else
    {
        DeltaTime = Env->delta_time;
        InvDeltaTime = Env->inv_delta_time;
    }
    synchronize_simulation_time_snapshot(DeltaTime, InvDeltaTime); // FUN_06346e70

    const int Result = grow_friction_system(); // FUN_063563d0

    for (int32_t ObjIndex = static_cast<int32_t>(Objects.n_elems) - 1; ObjIndex >= 0; --ObjIndex) // count read once (:0634b53b)
    {
        IVP_Real_Object* const Obj = Objects.elems[ObjIndex];
        IVP_Environment_Object_Notify_Event RevivedEvent{ this->Environment, Obj };
        this->Environment->cluster_manager->fire_event_object_revived(&RevivedEvent); // FUN_0634afd0
        IVP_Environment_Object_Notify_Event ListenerEvent{ this->Environment, Obj };
        IVP_Environment_Fire_Event_Object_Revived(this->Environment, &ListenerEvent);  // FUN_0634cf30
    }

    return Result;
}

// No retail function of its own: the call pattern
// OwningSimUnit->FUN_06349490(Environment) that 06347d80 (:06347d8b) emits.
void IVP_Core::revive_sleeping_core()
{
    IVP_Simulation_Unit_Revive_If_Sleeping(OwningSimUnit, Environment);
}

// FUN_06349180.
void IVP_Core::register_controller(IVP_Controller* Controller)
{
    Controllers.Add(Controller);
    IVP_Simulation_Unit_Register_Core_With_Controller(OwningSimUnit, Controller, this);
}

// FUN_06349110.
void IVP_Core::detach_controller(IVP_Controller* Controller)
{
    Controllers.RemoveElement(Controller);

    // No null test on OwningSimUnit, as retail.
    IVP_Simulation_Unit_Detach_Core_From_Controller_Bucket(OwningSimUnit, Controller, this);
}

// FUN_06347c40. Hashmap mode: FUN_06352930(Key) with ObjectHashMap as `this`
// (FUN_06352930 only hashes the key and forwards ECX to FUN_06352980,
// IVP_VHash_Store::FindElem). Single-slot mode: the cached candidate when its
// +0x8 equals Key. FUN_06347c40 has 16 callers, some on other classes (e.g.
// FUN_06359340 over another class's +0x20); only the IVP_Core ones (e.g.
// 06356d1d) are ported.
void* IVP_Core::find_object_hash_entry(unsigned Key) const
{
    if ((this->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) == IVP_CORE_PHYSICAL_UNMOVEABLE_TRUE)
    {
        if (this->ObjectHashMap == nullptr)
            return nullptr;
        return ((IVP_VHash_Store*)this->ObjectHashMap)->FindElem((void*)Key); // FUN_06352930
    }

    void* Candidate = this->ObjectHashMap;
    if (Candidate == nullptr)
        return nullptr;

    unsigned CandidateKey = *reinterpret_cast<unsigned*>(reinterpret_cast<uintptr_t>(Candidate) + 0x8);
    return (CandidateKey == Key) ? Candidate : nullptr;
}

// FUN_06347c80 (its SEH frame is not reproduced). Registers Candidate under its
// +0x8. Hashmap mode: a null ObjectHashMap is first created --
// operator_new(0x14), FUN_063525f0(2) only on a non-null block -- then
// FUN_063526d0 inserts (key, Candidate). Single-slot mode: ObjectHashMap =
// Candidate (no free; the slot never owns it).
void IVP_Core::register_object_hash_entry(void* Candidate)
{
    if ((this->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) == IVP_CORE_PHYSICAL_UNMOVEABLE_TRUE)
    {
        if (this->ObjectHashMap == nullptr)
        {
            // MSVCRT operator new returns NULL on failure.
            void* Map = ::operator new(0x14, std::nothrow);
            this->ObjectHashMap = (Map != nullptr) ? new (Map) IVP_VHash_Store(2) : nullptr; // FUN_063525f0
        }

        unsigned CandidateKey = *reinterpret_cast<unsigned*>(reinterpret_cast<uintptr_t>(Candidate) + 0x8);
        ((IVP_VHash_Store*)this->ObjectHashMap)->AddElem((void*)CandidateKey, Candidate); // FUN_063526d0
    }
    else
    {
        this->ObjectHashMap = Candidate;
    }
}

// FUN_06347d20. Hashmap mode: FUN_063527e0 removes the entry keyed by
// Candidate's +0x8. Single-slot mode: ObjectHashMap = 0 without checking that
// Candidate is the cached one.
void IVP_Core::unregister_object_hash_entry(void* Candidate)
{
    if ((this->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) == IVP_CORE_PHYSICAL_UNMOVEABLE_TRUE)
    {
        unsigned CandidateKey = *reinterpret_cast<unsigned*>(reinterpret_cast<uintptr_t>(Candidate) + 0x8);
        ((IVP_VHash_Store*)this->ObjectHashMap)->RemoveElem((void*)CandidateKey); // FUN_063527e0
    }
    else
    {
        this->ObjectHashMap = nullptr;
    }
}

// IVP_Core::release_object_hash_entry (FUN_06347d50) is defined in
// ivp_friction_solver.cpp, next to IVP_Friction_Info_For_Core's users.

// FUN_063474d0 (thiscall, LocalCsys on the stack). Sole caller FUN_06345b20 (06345d9a), with this = Self->core, after
// Self->ApplyLocalCsys(&LocalCsys) (06345bd9) and with the same LocalCsys.
//   MWorldFCore.mmult4(LocalCsys, &MWorldFCore);   // FUN_063481d0
//   PosWorldFCore = MWorldFCore.vv;
//   QWorldFCore.set_from_matrix(&MWorldFCore);     // FUN_06350ca0 (reads the rotation only)
//   QWorldFCoreNext = QWorldFCore;
void IVP_Core::init_world_transform_from_local_csys(const IVP_U_Matrix* LocalCsys)
{
    MWorldFCore.mmult4(LocalCsys, &MWorldFCore); // FUN_063481d0, result == this

    PosWorldFCore.x = MWorldFCore.vv.k[0];
    PosWorldFCore.y = MWorldFCore.vv.k[1];
    PosWorldFCore.z = MWorldFCore.vv.k[2];

    QWorldFCore.set_from_matrix(&MWorldFCore);
    QWorldFCoreNext = QWorldFCore;
}

// FUN_06347410 (see the header). x87: 1/Izz and 1/Iyy stay on the stack and
// feed the isotropy test unrounded; 1/Ixx is the float re-read from +0x34; the
// (1/Ixx - 1/Iyy) difference is rounded through a float local.
//   S = ((A - Cf)^2 + d3f^2) + (B - A)^2,   d3f = (float)(Cf - B)
//   M = (Cf^2 + B^2) + A^2
// with A = 1/Izz, B = 1/Iyy, Cf = (float)(1/Ixx). M * 0.01f (DAT_063a5468,
// 0x3c23d70b) > S sets 0x40 (clears 0x80); <= or unordered clears both.
void IVP_Core::RecomputeInverseInertia()
{
    const double InvIzz = 1.0 / (double)(RotInertia.z); // A
    const double InvIyy = 1.0 / (double)(RotInertia.y); // B
    const double InvIxx = 1.0 / (double)(RotInertia.x);
    InvRotInertia.x = (float)(InvIxx);
    InvRotInertia.y = (float)(InvIyy);
    InvRotInertia.z = (float)(InvIzz);
    InvMass = (float)(1.0 / (double)(Mass));

    const double InvIxxF = InvRotInertia.x; // re-read as float
    const double D1 = InvIyy - InvIzz;
    const double D2 = InvIzz - InvIxxF;
    const float D3 = (float)(InvIxxF - InvIyy);
    const double Spread = (D2 * D2 + (double)(D3) * D3) + D1 * D1;
    const double Magnitude = (InvIxxF * InvIxxF + InvIyy * InvIyy) + InvIzz * InvIzz;

    if (Magnitude * (double)(0.010000001f) > Spread)
        Flags = (Flags & 0xffffff7fu) | 0x40u;
    else
        Flags = Flags & 0xffffff3fu;

    DAngularLimitFactor = (float)(0.5 / (double)(UpperLimitRadius));
}

// ============================================================================
// The core helpers PhySys calls. x87 values that stay on the stack are kept in
// double; every float store is a static_cast<float>.
// ============================================================================

// FUN_06347c10: ratio = NewMass / Mass on the stack; Mass = NewMass; each
// RotInertia component = float(ratio * component); then FUN_06347410.
void IVP_Core::set_mass(float NewMass)
{
    const double Ratio = (double)(NewMass) / Mass;
    Mass = NewMass;
    RotInertia.x = (float)(Ratio * RotInertia.x);
    RotInertia.y = (float)(Ratio * RotInertia.y);
    RotInertia.z = (float)(Ratio * RotInertia.z);
    RecomputeInverseInertia();
}

// FUN_06346bb0: RotInertia = *Inertia; then FUN_06347410.
void IVP_Core::set_rotation_inertia(const Vector* Inertia)
{
    RotInertia.x = Inertia->x;
    RotInertia.y = Inertia->y;
    RotInertia.z = Inertia->z;
    RecomputeInverseInertia();
}

// FUN_06346a00: K = InvMass * Magnitude on the stack; the three accumulators
// get K * Direction + SpeedChange, stored z, x, y.
void IVP_Core::push_core_linear(const Vector* Direction, float Magnitude)
{
    const double K = (double)(InvMass) * Magnitude;
    const float NewY = (float)(K * Direction->y + SpeedChange.y);
    const float NewZ = (float)(K * Direction->z + SpeedChange.z);
    const float NewX = (float)(K * Direction->x + SpeedChange.x);
    SpeedChange.z = NewZ;
    SpeedChange.x = NewX;
    SpeedChange.y = NewY;
}

// FUN_06346c80: Local = MWorldFCore.vimult(AxisWs) (06366010); x * InvRotInertia.x
// stays on the stack, y / z products are stored as floats; RotSpeedChange +=
// each * Magnitude.
void IVP_Core::push_core_angular_ws(const Vector* AxisWs, float Magnitude)
{
    IVP_U_Point In;
    In.set(AxisWs->x, AxisWs->y, AxisWs->z);
    IVP_U_Point Local;
    MWorldFCore.vimult(&In, &Local);

    const double ScaledX = (double)(Local.k[0]) * InvRotInertia.x;
    const float ScaledY = (float)((double)(Local.k[1]) * InvRotInertia.y);
    const float ScaledZ = (float)((double)(Local.k[2]) * InvRotInertia.z);
    RotSpeedChange.x = (float)(ScaledX * Magnitude + RotSpeedChange.x);
    RotSpeedChange.y = (float)((double)(ScaledY) * Magnitude + RotSpeedChange.y);
    RotSpeedChange.z = (float)((double)(ScaledZ) * Magnitude + RotSpeedChange.z);
}

// FUN_06346d80: the same as 06346c80 without the transform.
void IVP_Core::push_core_angular_cs(const Vector* AxisCs, float Magnitude)
{
    const double ScaledX = (double)(InvRotInertia.x) * AxisCs->x;
    const float ScaledY = (float)((double)(InvRotInertia.y) * AxisCs->y);
    const float ScaledZ = (float)((double)(InvRotInertia.z) * AxisCs->z);
    RotSpeedChange.x = (float)(ScaledX * Magnitude + RotSpeedChange.x);
    RotSpeedChange.y = (float)((double)(ScaledY) * Magnitude + RotSpeedChange.y);
    RotSpeedChange.z = (float)((double)(ScaledZ) * Magnitude + RotSpeedChange.z);
}

// FUN_06346800 (sole caller FUN_063690b0 at 06369143 sums it over the cores).
// The sum stays on the x87 stack and is returned in ST0, in retail's order:
//   Lin = ((L.x*L.x + L.y*L.y) + L.z*L.z) * Mass
//   Rot = (A.y^2*Iy + A.z^2*Iz) + A.x^2*Ix
//   E   = (Lin + Rot) * 0.5f (DAT_063a5460)
double IVP_Core::calc_kinetic_energy(const Vector* LinearVelocity, const Vector* AngularVelocityOS) const
{
    const double Ax = AngularVelocityOS->x;
    const double Ay = AngularVelocityOS->y;
    const double Az = AngularVelocityOS->z;
    const double Lx = LinearVelocity->x;
    const double Ly = LinearVelocity->y;
    const double Lz = LinearVelocity->z;

    const double Lin = ((Lx * Lx + Ly * Ly) + Lz * Lz) * (double)(Mass);
    const double Rot = ((Ay * Ay) * (double)(RotInertia.y) + (Az * Az) * (double)(RotInertia.z)) +
                       (Ax * Ax) * (double)(RotInertia.x);
    return (Lin + Rot) * (double)(0.5f);
}

// FUN_06346890 (thiscall, RET 0xc), Ipion IVP_Core::calc_at_matrix. Sole
// caller 0634612a passes Environment->current_time and an output matrix.
//   MovementState low byte (signed) >= 8 -> copy MWorldFCore.
//   DeltaExt = ((Base - TimeOfLastPsi.time_base) + Offset) -
//     TimeOfLastPsi.time_offset on the x87 stack, with a float copy; the
//     UNROUNDED value equal to 0.0f or unordered -> the same copy.
//   Alpha = float(Delta * IDeltaTime) (Delta = the float copy); a local quat
//     = set_interpolate_quat(&QWorldFCore, &QWorldFCoreNext, Alpha)
//     (FUN_06350df0), then set_matrix(OutMatrix) (FUN_06350bc0).
//   OutMatrix->vv(+0x30) = DeltaWorldFCore (+0xc4, the
//     linear velocity) * Delta + PosWorldFCore (+0xb4) -- the float elapsed
//     time, NOT Alpha; product and sum stay on the stack, one rounding each.
void IVP_Core::calc_interpolated_transform(float CurrentTimeBase, float CurrentTimeOffset,
                                            IVP_U_Matrix* OutMatrix) const
{
    if (!(static_cast<int8_t>(MovementState & 0xFFu) < 8))
    {
        std::memcpy(OutMatrix, &MWorldFCore, sizeof(IVP_U_Matrix));
        return;
    }

    const double DeltaExt = (((double)(CurrentTimeBase) - TimeOfLastPsi.time_base) + CurrentTimeOffset) -
                            TimeOfLastPsi.time_offset;
    const float Delta = (float)(DeltaExt);

    if (!(DeltaExt < 0.0 || DeltaExt > 0.0)) // DAT_063a53d4, equal or unordered
    {
        std::memcpy(OutMatrix, &MWorldFCore, sizeof(IVP_U_Matrix));
        return;
    }

    const float Alpha = (float)((double)(Delta) * IDeltaTime);

    IVP_U_Quat InterpolatedQuat;
    InterpolatedQuat.set_interpolate_quat(&QWorldFCore, &QWorldFCoreNext, Alpha);
    InterpolatedQuat.set_matrix(OutMatrix);

    OutMatrix->vv.y = (float)((double)(Delta) * DeltaWorldFCore.y + PosWorldFCore.y);
    OutMatrix->vv.z = (float)((double)(Delta) * DeltaWorldFCore.z + PosWorldFCore.z);
    OutMatrix->vv.x = (float)((double)(Delta) * DeltaWorldFCore.x + PosWorldFCore.x);
}

// FUN_06346a50 (thiscall, RET 0xc), Ipion IVP_Core::damp_object(delta_time,
// rot_factor, speed_factor):
//   DTx = dt*Rot.x kept on the stack; DTy, DTz the same, each with a float copy.
//   Quad = ((DTz*DTz + DTy*DTy) + DTx*DTx) unrounded; >= 0.5f (DAT_063a5460)
//     takes the exp branch, < or unordered the linear one.
//   linear: Fx = 1.0f - DTx (kept), Fy = float(1 - DTy_f), Fz = float(1 - DTz_f).
//   exp (the x87 exp idiom, exp(-v)): Fx = exp(-DTx) kept, Fy =
//     float(exp(-DTy_f)), Fz = float(exp(-DTz_f)).
//   S = dt*speed_factor kept; >= 0.9f (DAT_063a53fc) -> exp(-S), < or
//     unordered -> 1.0f - S; kept.
//   RotSpeed.y/z = float(Fy/Fz * RotSpeed.y/z), RotSpeed.x
//     = float(Fx * RotSpeed.x), Speed.x/y/z = float(Fs * Speed.c).
void IVP_Core::apply_velocity_damping(float DeltaTime, const Vector* RotDampVec, float SpeedDampValue)
{
    const double DTx = (double)(DeltaTime) * RotDampVec->x;
    const double DTyExt = (double)(DeltaTime) * RotDampVec->y;
    const float DTy = (float)(DTyExt);
    const double DTzExt = (double)(DeltaTime) * RotDampVec->z;
    const float DTz = (float)(DTzExt);
    const double Quad = (DTzExt * DTzExt + DTyExt * DTyExt) + DTx * DTx;

    double Fx;
    float Fy;
    float Fz;
    if (!(Quad >= 0.5)) // DAT_063a5460
    {
        Fx = 1.0 - DTx;
        Fy = (float)(1.0 - (double)(DTy));
        Fz = (float)(1.0 - (double)(DTz));
    }
    else
    {
        Fx = std::exp(-DTx);
        Fy = (float)(std::exp(-(double)(DTy)));
        Fz = (float)(std::exp(-(double)(DTz)));
    }

    const double S = (double)(DeltaTime) * SpeedDampValue;
    const double Fs = !(S >= (double)(0.9f)) ? (1.0 - S) : std::exp(-S); // DAT_063a53fc

    RotSpeed.z = (float)((double)(Fz) * RotSpeed.z);
    RotSpeed.x = (float)(Fx * RotSpeed.x);
    RotSpeed.y = (float)((double)(Fy) * RotSpeed.y);

    Speed.x = (float)(Fs * Speed.x);
    Speed.y = (float)(Fs * Speed.y);
    Speed.z = (float)(Fs * Speed.z);
}

// FUN_06346980 (thiscall, RET 4): MovementState's signed low byte >= 2 damps
// with RotSpeedDamp + 0.1f per axis and SpeedDamp + 0.1f (DAT_063a5464 =
// 0x3dcccccd, one rounding each); otherwise passes &RotSpeedDamp and
// SpeedDamp unchanged. Both paths call FUN_06346a50 (apply_velocity_damping) with
// DeltaTime.
void IVP_Core::damp_velocities(float DeltaTime)
{
    if (!(static_cast<int8_t>(MovementState & 0xFFu) < 2))
    {
        Vector FudgedRotDamp;
        FudgedRotDamp.x = RotSpeedDamp.x + 0.1f; // DAT_063a5464
        FudgedRotDamp.y = RotSpeedDamp.y + 0.1f;
        FudgedRotDamp.z = RotSpeedDamp.z + 0.1f;
        apply_velocity_damping(DeltaTime, &FudgedRotDamp, SpeedDamp + 0.1f);
    }
    else
    {
        apply_velocity_damping(DeltaTime, &RotSpeedDamp, SpeedDamp);
    }
}

// FUN_06346c00: calc_impulse_deltas() accumulated into RotSpeedChange /
// SpeedChange (+0x74/+0x84); commit_speed_changes() flushes them.
void IVP_Core::apply_impulse_at_point(const Vector* LeverArmOS, const Vector* PointImpulseOS,
                                       const Vector* PointImpulseWS)
{
    Vector LinearDelta, AngularDelta;
    calc_impulse_deltas(LeverArmOS, PointImpulseOS, PointImpulseWS, &LinearDelta, &AngularDelta);

    RotSpeedChange.x += AngularDelta.x;
    RotSpeedChange.y += AngularDelta.y;
    RotSpeedChange.z += AngularDelta.z;

    SpeedChange.x += LinearDelta.x;
    SpeedChange.y += LinearDelta.y;
    SpeedChange.z += LinearDelta.z;
}

// FUN_06346dd0: RotSpeed / Speed += the accumulators, which are then zeroed.
// Callers 06349565 / 0634973a / 063497db / 06369183.
void IVP_Core::commit_speed_changes()
{
    RotSpeed.x += RotSpeedChange.x;
    RotSpeed.y += RotSpeedChange.y;
    RotSpeed.z += RotSpeedChange.z;

    Speed.x += SpeedChange.x;
    Speed.y += SpeedChange.y;
    Speed.z += SpeedChange.z;

    RotSpeedChange.x = 0.0f;
    RotSpeedChange.y = 0.0f;
    RotSpeedChange.z = 0.0f;

    SpeedChange.x = 0.0f;
    SpeedChange.y = 0.0f;
    SpeedChange.z = 0.0f;
}

// FUN_06346960: zeroes RotSpeedChange then SpeedChange, leaving the dword at
// +0x80 alone. Sole caller
// the friction system's undo path FUN_06369190 (IVP_ListManager::UndoComplexPushes).
void IVP_Core::clear_speed_change()
{
    RotSpeedChange.x = 0.0f;
    RotSpeedChange.y = 0.0f;
    RotSpeedChange.z = 0.0f;
    SpeedChange.x = 0.0f;
    SpeedChange.y = 0.0f;
    SpeedChange.z = 0.0f;
}

// FUN_063473b0, Ipion IVP_Core::undo_synchronize_rot_z: RotSpeed and
// QWorldFCoreNext from the record, then OldSyncInfo = 0 (arena memory,
// not freed).
// Sole caller FUN_063596b0 runs this when the record's +0x20 is 0 and does
// not null-check OldSyncInfo either.
void IVP_Core::restore_fast_sim_snapshot()
{
    const IVP_Old_Sync_Rot_Z* Sync = static_cast<const IVP_Old_Sync_Rot_Z*>(OldSyncInfo);

    RotSpeed.x = Sync->OldSyncRotSpeed.x;
    RotSpeed.y = Sync->OldSyncRotSpeed.y;
    RotSpeed.z = Sync->OldSyncRotSpeed.z;

    QWorldFCoreNext = Sync->OldSyncQWorldFCore;

    OldSyncInfo = nullptr;
}

// FUN_06347630: Objects.Clear(). Sole caller a tail-jump thunk at 06397b83.
void IVP_Core::clear_objects_vector()
{
    Objects.Clear();
}

// FUN_063478f0. Sole caller FUN_063495e0 (06349621) calls it per core of a
// sim unit with Environment->current_time and puts the result into
// MovementState's low byte.
//
// Two "has this core moved enough" checks, each comparing a squared position
// delta and a quaternion-similarity term against its own pair of thresholds
// -- a SHORT-term pair
// (against QWorldFCoreNext/PosLastMotion/QLastMotion, DAT_063a547c ~= 1e-4 /
// DAT_063a5478 ~= 2.5e-5) and a LONG-term pair (against QWorldFCore/
// PosLastLongMotion/QLastLongMotion, DAT_063a5474 = 0.01 / DAT_063a5470 =
// 0.04, roughly 100x looser). Both checks share the exact rotation-
// similarity formula `2*(1 - dot(Qa,Qb)^2) * UpperLimitRadius^2`.
//
//   1. If EITHER short-term delta exceeds its threshold: update the
//      short-term checkpoint (QLastMotion=QWorldFCoreNext, PosLastMotion=
//      PosWorldFCore, TimeOfLastMotion=int/frac-split(CurrentTimeBase+
//      CurrentTimeOffset), same idiom as update_time_of_last_motion), THEN
//      fall through into the long-term check below (retail does NOT return
//      here).
//   2. Else (both short-term deltas small): return 2 if elapsed time since
//      the (unchanged) TimeOfLastMotion is <= Environment->freeze_check_dtime
//      (+0x88), else return 3.
//   3. Long-term check (reached only via step 1's fallthrough): if EITHER
//      long-term delta exceeds its threshold, update the long-term
//      checkpoint (QLastLongMotion=QWorldFCore, PosLastLongMotion=
//      PosWorldFCore, TimeOfLastLongMotion=int/frac-split(...)) and return 1
//      unconditionally. Else, return 1 if elapsed time since the (unchanged)
//      TimeOfLastLongMotion is <= DAT_063a546c (4.0f), else return 3.
//
// Port-local name.
int IVP_Core::classify_motion_state(float CurrentTimeBase, float CurrentTimeOffset)
{
    // Every threshold test is taken only on strictly greater, so NaN never
    // counts as movement and the elapsed tests give the "recent" result
    // (2 / 1) on NaN. Constants by their exact bits: 0.01 and 0.04 are one ulp
    // above the nearest float literal.
    const double ShortPosThresholdSq = IVP_Core_Float_From_Bits(0x38d1b717u); // DAT_063a547c
    const double ShortRotThreshold = IVP_Core_Float_From_Bits(0x37d1b717u);   // DAT_063a5478
    const double LongPosThresholdSq = IVP_Core_Float_From_Bits(0x3c23d70bu);  // DAT_063a5474
    const double LongRotThreshold = IVP_Core_Float_From_Bits(0x3d23d70bu);    // DAT_063a5470
    const double LongElapsedThreshold = IVP_Core_Float_From_Bits(0x40800000u); // DAT_063a546c, 4.0f

    // ((dz*dz + dy*dy) + dx*dx) on unrounded differences.
    const double ShortDX = (double)(PosWorldFCore.x) - PosLastMotion.x;
    const double ShortDY = (double)(PosWorldFCore.y) - PosLastMotion.y;
    const double ShortDZ = (double)(PosWorldFCore.z) - PosLastMotion.z;
    bool bShortMoved = ((ShortDZ * ShortDZ + ShortDY * ShortDY) + ShortDX * ShortDX) > ShortPosThresholdSq;
    if (!bShortMoved)
    {
        // Dot = ((Qlm.w*Qn.w + Qlm.z*Qn.z) + Qlm.y*Qn.y) + Qn.x*Qlm.x;
        // ((1 - Dot*Dot) * 2) * R * R.
        const double Dot = (((double)(QLastMotion.w) * QWorldFCoreNext.w +
                             (double)(QLastMotion.z) * QWorldFCoreNext.z) +
                            (double)(QLastMotion.y) * QWorldFCoreNext.y) +
                           (double)(QWorldFCoreNext.x) * QLastMotion.x;
        const double RotTerm = (((1.0 - Dot * Dot) + (1.0 - Dot * Dot)) * UpperLimitRadius) * UpperLimitRadius;
        bShortMoved = RotTerm > ShortRotThreshold;
    }

    if (!bShortMoved)
    {
        // ((t.base - last.base) + t.offset) - last.offset
        // against Environment+0x88; not greater (or unordered) -> 2, else 3.
        const double Elapsed = (((double)(CurrentTimeBase) - TimeOfLastMotion.time_base) + CurrentTimeOffset) -
                               TimeOfLastMotion.time_offset;
        return !(Elapsed > Environment->freeze_check_dtime) ? 2 : 3;
    }

    // Short-term checkpoint. Sum = offset + base stays on
    // the stack for both the int/frac split here and the long-term one below.
    QLastMotion = QWorldFCoreNext;
    PosLastMotion.x = PosWorldFCore.x;
    PosLastMotion.y = PosWorldFCore.y;
    PosLastMotion.z = PosWorldFCore.z;
    const double Sum = (double)(CurrentTimeOffset) + CurrentTimeBase;
    const int32_t SumInt = static_cast<int32_t>(Sum); // MSVCRT ftol, truncates
    const float SumIntPart = (float)(SumInt);
    const float SumFrac = (float)(Sum - (double)(SumInt));
    TimeOfLastMotion.time_base = SumIntPart;
    TimeOfLastMotion.time_offset = SumFrac;

    // The long-term pair against QWorldFCore.
    const double LongDX = (double)(PosWorldFCore.x) - PosLastLongMotion.x;
    const double LongDY = (double)(PosWorldFCore.y) - PosLastLongMotion.y;
    const double LongDZ = (double)(PosWorldFCore.z) - PosLastLongMotion.z;
    bool bLongMoved = ((LongDZ * LongDZ + LongDY * LongDY) + LongDX * LongDX) > LongPosThresholdSq;
    if (!bLongMoved)
    {
        const double Dot = (((double)(QLastLongMotion.w) * QWorldFCore.w +
                             (double)(QLastLongMotion.z) * QWorldFCore.z) +
                            (double)(QLastLongMotion.y) * QWorldFCore.y) +
                           (double)(QLastLongMotion.x) * QWorldFCore.x;
        const double RotTerm = (((1.0 - Dot * Dot) + (1.0 - Dot * Dot)) * UpperLimitRadius) * UpperLimitRadius;
        bLongMoved = RotTerm > LongRotThreshold;
    }

    if (!bLongMoved)
    {
        // Not greater than 4.0 (or unordered) -> 1, else 3.
        const double LongElapsed =
            (((double)(CurrentTimeBase) - TimeOfLastLongMotion.time_base) + CurrentTimeOffset) -
            TimeOfLastLongMotion.time_offset;
        return !(LongElapsed > LongElapsedThreshold) ? 1 : 3;
    }

    // Long-term checkpoint from the same Sum.
    QLastLongMotion = QWorldFCore;
    PosLastLongMotion.x = PosWorldFCore.x;
    PosLastLongMotion.y = PosWorldFCore.y;
    PosLastLongMotion.z = PosWorldFCore.z;
    TimeOfLastLongMotion.time_base = SumIntPart;
    TimeOfLastLongMotion.time_offset = SumFrac;
    return 1;
}

// FUN_06347be0: MindistEventStamp = Environment->mindist_event_stamp (raw dword), then
// IVP_Real_Object::recheck_mindists_of_other_cores (06344f50) on each object,
// last first. No null tests.
void IVP_Core::notify_objects_and_cache_env_field()
{
    const uint32_t RawBits = *reinterpret_cast<const uint32_t*>(&Environment->mindist_event_stamp);
    MindistEventStamp = reinterpret_cast<void*>(static_cast<uintptr_t>(RawBits));

    for (int32_t I = static_cast<int32_t>(Objects.n_elems) - 1; I >= 0; --I)
    {
        Objects.elems[I]->recheck_mindists_of_other_cores(); // FUN_06344f50
    }
}

// FUN_06347d80 (see the header). Callers 06345589 / 06345590 (FUN_06345550),
// 0634663d, 0634bbfc.
void IVP_Core::wake_or_ensure_simulation()
{
    if ((Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) != 0)
        return;

    if ((MovementState & 0xFFu) == 8u)
        revive_sleeping_core();
    else
        ensure_in_simulation();
}

// FUN_06347db0: IVP_Real_Object::EnsureInSimulation() (FUN_06346170) on each
// object, last first, no null test. Sole caller 06346528.
void IVP_Core::ensure_objects_in_simulation()
{
    for (int i = Objects.Len() - 1; i >= 0; --i)
    {
        Objects.ElementAt(i)->EnsureInSimulation();
    }
}

// ============================================================================
// FUN_0634d510 (cdecl).
// ============================================================================
void IVP_Real_Object_Free_Cache_Object(IVP_Real_Object* Object)
{
    IVP_NarrowPhaseCacheSlot* Slot = Object->cache_object;
    if (Slot != nullptr)
    {
        Slot->Owner = nullptr;          // slot +0x08
        Object->cache_object = nullptr;
    }
}

// ============================================================================
// FUN_06347040.
// ============================================================================
void IVP_Core::freeze_simulation_core()
{
    IVP_Core_StopMovementWithoutCollisionRecheck(this); // stop movement

    // current_time captured once (dword copies).
    const float NowHi = Environment->current_time.time_base;
    const float NowLo = Environment->current_time.time_offset;

    for (int32_t Index = static_cast<int32_t>(Objects.n_elems) - 1; Index >= 0; --Index)
    {
        IVP_Real_Object* Object = Objects.elems[Index];

        // Movement byte = 8.
        Object->flags = (Object->flags & 0xFFFFFF08u) | 0x8u;

        Environment->mindist_manager->RecheckOvElement(Object); // FUN_0634ebd0

        // dt = ((now_hi - t_hi) + now_lo) - t_lo; both values advance with their
        // gradients, which are zeroed (each multiply reads before the store).
        IVP_Hull_Manager* Hull = &Object->hull_manager;
        const double SinceLastEdit = (((double)(NowHi) - Hull->LastEditTimeHi) + NowLo) - Hull->LastEditTimeLo;
        const double OuterGradient = Hull->OuterGradient;
        Hull->OuterGradient = 0.0f;
        Hull->OuterValueAtLastEdit = (float)(SinceLastEdit * OuterGradient + Hull->OuterValueAtLastEdit);
        const double InnerGradient = Hull->InnerGradient;
        Hull->InnerGradient = 0.0f;
        Hull->InnerValueAtLastEdit = (float)(SinceLastEdit * InnerGradient + Hull->InnerValueAtLastEdit);

        Hull->DrainAndShift(); // FUN_06352220

        if (Object->cache_object != nullptr)
        {
            IVP_Real_Object_Free_Cache_Object(Object); // FUN_0634d510
        }
    }
}

// ============================================================================
// FUN_0634b590.
// ============================================================================
void IVP_Core::fire_event_object_frozen()
{
    // IVP_Event_Object as built on the stack twice per object: {environment, object}.
    struct FIvpEventObject
    {
        IVP_Environment* Environment; // +0x00
        IVP_Real_Object* RealObject;  // +0x04
    };

    for (int32_t Index = static_cast<int32_t>(Objects.n_elems) - 1; Index >= 0; --Index)
    {
        IVP_Real_Object* Object = Objects.elems[Index];

        // Cluster manager (env+0x0c) listeners of this object, 0634af70
        // (IVP_Cluster_Manager::fire_event_object_frozen).
        IVP_Environment_Object_Notify_Event ClusterEvent;
        ClusterEvent.Env = Environment;
        ClusterEvent.Object = Object;
        Environment->cluster_manager->fire_event_object_frozen(&ClusterEvent);

        // Environment object listeners.
        FIvpEventObject EnvironmentEvent;
        EnvironmentEvent.Environment = Environment;
        EnvironmentEvent.RealObject = Object;
        IVP_Environment_Fire_Event_Object_Frozen(Environment, &EnvironmentEvent); // FUN_0634cf00
    }
}

// ============================================================================
// FUN_0634b5e0.
// ============================================================================
void IVP_Core::freeze_and_fire_object_frozen()
{
    freeze_simulation_core();    // FUN_06347040
    fire_event_object_frozen();  // FUN_0634b590
}

// FUN_06346290 (thiscall, RET 8).
//   W = MWorldFCore.vmult(RotSpeed) via FUN_06348580, which sums rows 1 and 2
//     as (r.x*p.x + r.z*p.z) + r.y*p.y and row 0 as (r.z*p.z + r.y*p.y) +
//     r.x*p.x and stores floats -- inlined here in that order (the generic
//     IVP_U_Matrix3::vmult sums in another order, in float).
//   D = PositionWs - MWorldFCore.vv, kept on the x87 stack.
//   C = W x D: Cx = Wy*Dz - Wz*Dy and Cy = Wz*Dx - Wx*Dz are stored as floats,
//     Cz = Wx*Dy - Wy*Dx stays on the stack.
//   Out.z = float(Cz + Speed.z), Out.x = float(Cx + Speed.x), Out.y =
//     float(Cy + Speed.y).
void IVP_Core::get_surface_speed(const IVP_U_Point* PositionWs, IVP_U_Point* SpeedWsOut) const
{
    const IVP_U_Matrix3& M = MWorldFCore;
    const float Wx = (float)(((double)(M.rows[0].k[2]) * RotSpeed.z + (double)(M.rows[0].k[1]) * RotSpeed.y) + (double)(M.rows[0].k[0]) * RotSpeed.x);
    const float Wy = (float)(((double)(M.rows[1].k[0]) * RotSpeed.x + (double)(M.rows[1].k[2]) * RotSpeed.z) + (double)(M.rows[1].k[1]) * RotSpeed.y);
    const float Wz = (float)(((double)(M.rows[2].k[0]) * RotSpeed.x + (double)(M.rows[2].k[2]) * RotSpeed.z) + (double)(M.rows[2].k[1]) * RotSpeed.y);

    const double Dx = (double)(PositionWs->x) - MWorldFCore.vv.x;
    const double Dy = (double)(PositionWs->y) - MWorldFCore.vv.y;
    const double Dz = (double)(PositionWs->z) - MWorldFCore.vv.z;

    const float Cx = (float)(Dz * Wy - Wz * Dy);
    const float Cy = (float)(Wz * Dx - Dz * Wx);
    const double Cz = Dy * Wx - Wy * Dx;

    SpeedWsOut->z = (float)(Cz + Speed.z);
    SpeedWsOut->x = (float)((double)(Cx) + Speed.x);
    SpeedWsOut->y = (float)((double)(Cy) + Speed.y);
}


