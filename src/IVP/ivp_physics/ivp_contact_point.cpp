// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_contact_point.h"
#include "ivp_environment.h"
#include "ivp_material.h"
#include "ivp_cluster_manager.h"   // fire_event_friction_created (FUN_0634b0e0)
#include "ivp_simulation_unit.h"   // FUN_06348c60 / FUN_06348d80 (06356bc0's sim unit fusion)
#include "ivp_mindist_settings.h"  // G_IVP_MindistSettings (06359560 / 063595c0)
#include "ivp_impact.h"            // FUN_06358c00 (06359380)
#include "../ivp_collision/ivp_compact_ledge_solver.h" // give_world_coords_AT (FUN_0635bda0), IVP_U_Point::calc_cross_product (06359d10)
#include "../ivp_collision/ivp_listmanager_topology.h" // IVP_Contact_Point::ResolveSideMaterials (FUN_06358bb0)
#include "x87_trig.h"
#include <cmath>
#include <cstring>
#include <vector>

// Classes used below: IVP_ListManager (the friction system),
// IVP_ListManager_CorePairEntry (a core pair), IVP_Contact_Point (a friction
// dist), IVP_Impact_Solver_Long_Term (its long-term contact data).

// ============================================================================
// 0x063658f0-0x06366008: retail's contact-reaction frame helpers. Overview:
//
//   ReactionFrameInit (FUN_06365d50)(ctx, CoreA, CoreB, Position, TangentDir0,
//     TangentDir1, Aux) -- fills ctx's 2x2 effective-inverse-mass matrix and
//     relative-velocity pair for the given tangent basis at Position, summing
//     CoreA's contribution with +1 and CoreB's with -1 (either may be null).
//   ReactionFrameApplyImpulse (FUN_06365df0)(ctx, CoreA, CoreB, Impulse2D)
//     -- applies the solved 2D tangent impulse back onto both cores'
//        Speed (+0xa4) and RotSpeed (+0x94).
//
// All of them are thiscall on a 0x114-byte frame object (the
// IVP_Friction_Basis_Context below) owned by the contact-reaction kernels
// FUN_06354120 / FUN_063548e0. No vtable and no RTTI, so the owning class is
// not named; only the offsets below are touched. The "core" argument is an
// IVP_Core: +0x34..+0x40 per-axis inverse rotation inertia + inverse mass,
// +0x94 rot_speed, +0xa4 speed, +0xf4 the core's world rotation
// (IVP_U_Matrix3), +0x124 its world position.
//
// "this" layout:
//   +0x00 tangentDir0   const float*, never null-checked
//   +0x04 tangentDir1   const float*, nullable -- gates the second row
//   +0x08 auxAxis       const float*, nullable -- gates the third row
//   +0x0c / +0x3c       basis1 (core 0 / core 1): 3 rows of [x,y,z,1]
//   +0x6c               the last core's +0xf4 pointer (written at 06365909)
//   +0x74 / +0xa4       basis2 (core 0 / core 1): basis1 .* core+0x34
//   +0xd4/+0xd8/+0xdc/+0xe8/+0xec/+0xfc/+0x104/+0x108/+0x10c  float sums
//
// x87: values retail keeps on the FPU stack are doubles here and are rounded
// to float only where retail stores a float.
// ============================================================================
namespace
{
    // FUN_06365ce0, thiscall (this = Lhs; Rhs), RET 4: 4-component dot product. The sum is
    // accumulated w, z, y, x on the x87 stack -- ((t3*p3 + t2*p2) + t1*p1) + t0*p0 -- and
    // returned unrounded in ST0 (double here). Sole caller: ReactionFrameAccumulateCore at
    // 06365c51.
    double __fastcall Dot4(const float* Lhs, const float* Rhs)
    {
        return (((double)(Rhs[3]) * Lhs[3] + (double)(Rhs[2]) * Lhs[2]) +
                (double)(Rhs[1]) * Lhs[1]) +
               (double)(Rhs[0]) * Lhs[0];
    }

    // FUN_06365d10 (thiscall this = Out): Out[i] = Values[i] * Factors[i] for
    // i in 0..3, each product rounded to float.
    void __fastcall Mul4(float* Out, const float* Values, const float* Factors)
    {
        Out[0] = Values[0] * Factors[0];
        Out[1] = Values[1] * Factors[1];
        Out[2] = Values[2] * Factors[2];
        Out[3] = Values[3] * Factors[3];
    }

    inline float Ivp_ReactionFloatAt(const unsigned char* Base, int Offset)
    {
        return *reinterpret_cast<const float*>(Base + Offset);
    }

    // FUN_0628be40 (thiscall this=A; B), RET 4: ((A.z*B.z + A.y*B.y) + A.x*B.x),
    // returned unrounded in ST0.
    inline double Ivp_ReactionDotZYX(const float* A, const float* B)
    {
        return ((double)(B[2]) * A[2] + (double)(B[1]) * A[1]) +
               (double)(B[0]) * A[0];
    }

    // One reaction row of ReactionFrameAccumulateCore for Axis; the same
    // instruction sequence at 0636593d-063659df (tangent 0) and
    // 06365a9d-06365b33 (tangent 1).
    //   c = Delta x Axis stays on the x87 stack:
    //     cx = dy*a.z - dz*a.y, cy = dz*a.x - dx*a.z, cz = dx*a.y - dy*a.x
    //   Out0 = (R2.x*cz + R0.x*cx) + R1.x*cy
    //   Out1 = (cz*R2.y + cy*R1.y) + R0.y*cx
    //   Out2 = (cz*R2.z + cy*R1.z) + R0.z*cx
    // where Rn is row n of the core's rotation at core+0xf4.
    inline void Ivp_ReactionRow(const unsigned char* Core, const float* Delta, const float* Axis,
                                double& Out0, double& Out1, double& Out2)
    {
        const double Dx = Delta[0];
        const double Dy = Delta[1];
        const double Dz = Delta[2];
        const double Cx = Dy * Axis[2] - Dz * Axis[1];
        const double Cy = Dz * Axis[0] - Dx * Axis[2];
        const double Cz = Dx * Axis[1] - Dy * Axis[0];
        Out0 = (Ivp_ReactionFloatAt(Core, 0x114) * Cz + Ivp_ReactionFloatAt(Core, 0xf4) * Cx) +
               Ivp_ReactionFloatAt(Core, 0x104) * Cy;
        Out1 = (Cz * Ivp_ReactionFloatAt(Core, 0x118) + Cy * Ivp_ReactionFloatAt(Core, 0x108)) +
               Ivp_ReactionFloatAt(Core, 0xf8) * Cx;
        Out2 = (Cz * Ivp_ReactionFloatAt(Core, 0x11c) + Cy * Ivp_ReactionFloatAt(Core, 0x10c)) +
               Ivp_ReactionFloatAt(Core, 0xfc) * Cx;
    }

    // FUN_063658f0, thiscall (this; posRef,
    // core, basis1, basis2, sign), RET 0x14. Builds up to three rows (tangent 0,
    // tangent 1, aux axis) of the core's reaction to a unit impulse along the
    // axis at posRef and accumulates their products into this's sums.
    void
    ReactionFrameAccumulateCore(unsigned char* thisPtr, const float* posRef, unsigned char* structPtr,
                                float* basis1, float* basis2, float sign) // formerly FUN_063658f0
    {
        const float* TangentDir0 = *reinterpret_cast<const float* const*>(thisPtr + 0x00);
        unsigned char* RotationBytes = structPtr + 0xf4;
        *reinterpret_cast<unsigned char**>(thisPtr + 0x6c) = RotationBytes;                // 06365909
        const float* Weight = reinterpret_cast<const float*>(structPtr + 0x34);             // 0636592c
        const float* RotSpeed = reinterpret_cast<const float*>(structPtr + 0x94);           // 06365a00
        const float* Speed = reinterpret_cast<const float*>(structPtr + 0xa4);              // 06365a06
        float* const SumA00 = reinterpret_cast<float*>(thisPtr + 0xd4);
        float* const SumA01 = reinterpret_cast<float*>(thisPtr + 0xd8);
        float* const SumA02 = reinterpret_cast<float*>(thisPtr + 0xdc);
        float* const SumA11 = reinterpret_cast<float*>(thisPtr + 0xe8);
        float* const SumA12 = reinterpret_cast<float*>(thisPtr + 0xec);
        float* const SumA22 = reinterpret_cast<float*>(thisPtr + 0xfc);
        float* const SumV0 = reinterpret_cast<float*>(thisPtr + 0x104);
        float* const SumV1 = reinterpret_cast<float*>(thisPtr + 0x108);
        float* const SumV2 = reinterpret_cast<float*>(thisPtr + 0x10c);

        // 0636590c-06365939: posRef - core position, spilled as three floats.
        IVP_U_Point Delta;
        Delta.k[0] = (float)((double)(posRef[0]) - Ivp_ReactionFloatAt(structPtr, 0x124));
        Delta.k[1] = (float)((double)(posRef[1]) - Ivp_ReactionFloatAt(structPtr, 0x128));
        Delta.k[2] = (float)((double)(posRef[2]) - Ivp_ReactionFloatAt(structPtr, 0x12c));

        // --- row 0 (tangent 0) ---
        {
            double R0, R1, R2;
            Ivp_ReactionRow(structPtr, Delta.k, TangentDir0, R0, R1, R2);
            basis1[0] = (float)(R0);                                         // 0636599b
            const float B1Y = (float)(R1);                                   // 063659af
            basis1[3] = 1.0f;                                                           // 063659b3
            basis1[1] = B1Y;                                                            // 063659c2
            basis1[2] = (float)(R2);                                         // 063659e1 FST, R2 kept
            const float B2Y = (float)((double)(B1Y) * Weight[1]); // 063659e4-063659eb
            const double ZW2 = R2 * Weight[2];                                          // 063659f3
            basis2[1] = B2Y;                                                            // 063659fd
            basis2[0] = (float)((double)(basis1[0]) * Weight[0]); // 063659f9-06365a0b
            basis2[2] = (float)(ZW2);                                        // 06365a14
            basis2[3] = Weight[3];                                                      // 06365a17 FST

            // 06365a1a-06365a40
            *SumA00 = (float)(
                (((ZW2 * basis1[2] + (double)(B2Y) * basis1[1]) + (double)(basis1[0]) * basis2[0]) +
                 (double)(Weight[3]) * basis1[3]) +
                *SumA00);

            // 06365a46-06365a8f: speed . tangent0 (z, y, x) + rot_speed . row0 (z, y, x).
            const double LinearPart = ((double)(TangentDir0[2]) * Speed[2] +
                                       (double)(TangentDir0[1]) * Speed[1]) +
                                      (double)(TangentDir0[0]) * Speed[0];
            const double AngularPart = ((double)(RotSpeed[2]) * basis1[2] +
                                        (double)(RotSpeed[1]) * basis1[1]) +
                                       (double)(RotSpeed[0]) * basis1[0];
            *SumV0 = (float)((LinearPart + AngularPart) * sign + *SumV0);
        }

        // 06365a95: TEST tangentDir1; JZ -> return.
        const float* TangentDir1 = *reinterpret_cast<const float* const*>(thisPtr + 0x04);
        if (TangentDir1 == nullptr)
            return;

        // --- row 1 (tangent 1) ---
        float* const Basis1Row1 = basis1 + 4;
        float* const Basis2Row1 = basis2 + 4;
        {
            double R0, R1, R2;
            Ivp_ReactionRow(structPtr, Delta.k, TangentDir1, R0, R1, R2);
            Basis1Row1[0] = (float)(R0); // 06365b02
            Basis1Row1[1] = (float)(R1); // 06365b16
            Basis1Row1[2] = (float)(R2); // 06365b33
            Basis1Row1[3] = 1.0f;                   // 06365b36

            // 06365b3f-06365b72: each product rounded to float once.
            Basis2Row1[3] = (float)((double)(Basis1Row1[3]) * Weight[3]);
            Basis2Row1[2] = (float)((double)(Basis1Row1[2]) * Weight[2]);
            Basis2Row1[0] = (float)((double)(Basis1Row1[0]) * Weight[0]);
            Basis2Row1[1] = (float)((double)(Basis1Row1[1]) * Weight[1]);

            // 06365b75-06365b98
            *SumA11 = (float)(
                ((((double)(Basis2Row1[3]) * Basis1Row1[3] + (double)(Basis2Row1[2]) * Basis1Row1[2]) +
                  (double)(Basis2Row1[1]) * Basis1Row1[1]) +
                 (double)(Basis1Row1[0]) * Basis2Row1[0]) +
                *SumA11);
            // 06365b9e-06365bb9
            *SumA01 = (float)(
                (((double)(Basis2Row1[2]) * basis1[2] + (double)(Basis2Row1[1]) * basis1[1]) +
                 (double)(Basis2Row1[0]) * basis1[0]) +
                *SumA01);

            // 06365bbf-06365bff: rot_speed . row1 (z, y, x) + speed . tangent1 (z, y, x);
            // tangent1 is re-read from this+4.
            const float* TangentDir1Reload = *reinterpret_cast<const float* const*>(thisPtr + 0x04);
            const double AngularPart = ((double)(Basis1Row1[2]) * RotSpeed[2] +
                                        (double)(Basis1Row1[1]) * RotSpeed[1]) +
                                       (double)(Basis1Row1[0]) * RotSpeed[0];
            const double LinearPart = ((double)(Speed[2]) * TangentDir1Reload[2] +
                                       (double)(Speed[1]) * TangentDir1Reload[1]) +
                                      (double)(Speed[0]) * TangentDir1Reload[0];
            *SumV1 = (float)((AngularPart + LinearPart) * sign + *SumV1);
        }

        // 06365bea/06365c05: TEST auxAxis; JZ -> return.
        const float* AuxAxis = *reinterpret_cast<const float* const*>(thisPtr + 0x08);
        if (AuxAxis == nullptr)
            return;

        // --- row 2 (aux axis) ---
        // 06365c0b-06365c15: FUN_0628bed0(this=Cross; &Delta, AuxAxis) = Delta x AuxAxis.
        IVP_U_Point Cross;
        Cross.calc_cross_product(&Delta, reinterpret_cast<const IVP_U_Point*>(AuxAxis));
        // 06365c1a-06365c25: FUN_06366010(this=core rotation; &Cross, basis1+0x20) = R^T * Cross.
        float* const Basis1Row2 = basis1 + 8;
        float* const Basis2Row2 = basis2 + 8;
        reinterpret_cast<const IVP_U_Matrix3*>(RotationBytes)->vimult(&Cross, reinterpret_cast<IVP_U_Point*>(Basis1Row2));
        basis1[11] = 1.0f;                                  // 06365c38
        Mul4(Basis2Row2, Basis1Row2, Weight);               // 06365c3f: basis2 row 2 = row 2 .* weight (FUN_06365d10)

        // 06365c44-06365c5b: FUN_06365ce0 (Dot4)(this=basis2 row 2; basis1 row 2) + old.
        *SumA22 = (float)(Dot4(Basis2Row2, Basis1Row2) + *SumA22);
        // 06365c61-06365c7b: FUN_0628be40(this=basis2 row 2; basis1 row 0) + old.
        *SumA02 = (float)(Ivp_ReactionDotZYX(Basis2Row2, basis1) + *SumA02);
        // 06365c81-06365c96: FUN_0628be40(this=basis2 row 2; basis1 row 1) + old.
        *SumA12 = (float)(Ivp_ReactionDotZYX(Basis2Row2, Basis1Row1) + *SumA12);
        // 06365c9c-06365ca9: FUN_0628be40(this=speed; AuxAxis), spilled as float.
        const float LinearPart = (float)(Ivp_ReactionDotZYX(Speed, AuxAxis));
        // 06365cad-06365cc5: (FUN_0628be40(this=rot_speed; basis1 row 2) + that float) * sign + old.
        *SumV2 = (float)((Ivp_ReactionDotZYX(RotSpeed, Basis1Row2) + LinearPart) * sign + *SumV2);
    }

    // FUN_06365d50, thiscall (this; core0, core1, pos, tangentDir0,
    // tangentDir1, auxAxis), RET 0x18. Stores the three axis pointers at
    // this+0/4/8; when tangentDir1 is non-null this+0xd4..0x10c is cleared,
    // otherwise only +0xd4 and +0x104. Then ReactionFrameAccumulateCore per
    // non-null core: core0 -> basis this+0xc / this+0x74 with +1.0f, core1 ->
    // this+0x3c / this+0xa4 with -1.0f.
    void
    ReactionFrameInit(unsigned char* thisPtr, unsigned char* posA, unsigned char* posB, void* structShared,
                      const float* tangentDir0, const float* tangentDir1, const void* auxAxis) // formerly FUN_06365d50
    {
        *reinterpret_cast<const float**>(thisPtr + 0x00) = tangentDir0;
        *reinterpret_cast<const float**>(thisPtr + 0x04) = tangentDir1;
        *reinterpret_cast<const void**>(thisPtr + 0x08) = auxAxis;

        if (tangentDir1 != nullptr) {
            std::memset(thisPtr + 0xd4, 0, 0x30);   // this+0xd4 .. this+0x103 (12 dwords)
            *reinterpret_cast<float*>(thisPtr + 0x104) = 0.0f;
            *reinterpret_cast<float*>(thisPtr + 0x108) = 0.0f;
            *reinterpret_cast<float*>(thisPtr + 0x10c) = 0.0f;
        } else {
            *reinterpret_cast<float*>(thisPtr + 0xd4) = 0.0f;
            *reinterpret_cast<float*>(thisPtr + 0x104) = 0.0f;
        }

        if (posA != 0) {
            ReactionFrameAccumulateCore(thisPtr, reinterpret_cast<const float*>(structShared), posA,
                                        reinterpret_cast<float*>(thisPtr + 0xc), reinterpret_cast<float*>(thisPtr + 0x74), 1.0f);
        }
        if (posB != 0) {
            ReactionFrameAccumulateCore(thisPtr, reinterpret_cast<const float*>(structShared), posB,
                                        reinterpret_cast<float*>(thisPtr + 0x3c), reinterpret_cast<float*>(thisPtr + 0xa4), -1.0f);
        }
    }

    // FUN_06365df0, thiscall (this; core0, core1, impulse[2]), RET 0xc. Applies
    // the solved 2D impulse to both cores: speed (+0xa4) += inv_mass (+0x40) *
    // impulse[i] * tangent_i and rot_speed (+0x94) += impulse[i] * basis2 row i,
    // core1 with the opposite sign. x87: each "field += scale * v" is rounded
    // once; the scales stay on the stack except core0's first one, which is
    // spilled as a float argument to FUN_0628be60.

    // Field[i] = float(Scale * V[i] + Field[i]) for i = 0..2; every operand is
    // read before the first store (FUN_0628be60 and the inlined copies).
    inline void Ivp_ReactionAddScaled(float* Field, const float* V, double Scale)
    {
        const double Y = Scale * V[1] + Field[1];
        const double Z = Scale * V[2] + Field[2];
        const double X = Scale * V[0] + Field[0];
        Field[2] = (float)(Z);
        Field[0] = (float)(X);
        Field[1] = (float)(Y);
    }

    void
    ReactionFrameApplyImpulse(unsigned char* thisPtr, unsigned char* structA, unsigned char* structB,
                              const float* impulse) // formerly FUN_06365df0
    {
        if (structA != nullptr)
        {
            const float InvMassA = *reinterpret_cast<const float*>(structA + 0x40);
            float* const SpeedA = reinterpret_cast<float*>(structA + 0xa4);
            float* const RotSpeedA = reinterpret_cast<float*>(structA + 0x94);
            const float* TangentDir0 = *reinterpret_cast<const float* const*>(thisPtr + 0x00);

            // 06365e05-06365e24: FUN_0628be60(this=speed; speed, tangent0, float(inv_mass*impulse[0])).
            const float Scale0 = (float)((double)(InvMassA) * impulse[0]);
            Ivp_ReactionAddScaled(SpeedA, TangentDir0, Scale0);

            // 06365e29-06365e56: inv_mass*impulse[1] kept extended; tangent1 has no null check.
            const float* TangentDir1 = *reinterpret_cast<const float* const*>(thisPtr + 0x04);
            Ivp_ReactionAddScaled(SpeedA, TangentDir1, (double)(InvMassA) * impulse[1]);

            // 06365e5a-06365ed9: rot_speed += impulse[0] * basis2 row 0, then impulse[1] * row 1.
            Ivp_ReactionAddScaled(RotSpeedA, reinterpret_cast<const float*>(thisPtr + 0x74), impulse[0]);
            Ivp_ReactionAddScaled(RotSpeedA, reinterpret_cast<const float*>(thisPtr + 0x84), impulse[1]);
        }

        if (structB != nullptr)
        {
            const double InvMassB = *reinterpret_cast<const float*>(structB + 0x40);
            float* const SpeedB = reinterpret_cast<float*>(structB + 0xa4);
            float* const RotSpeedB = reinterpret_cast<float*>(structB + 0x94);

            // -(inv_mass*impulse[0]) kept extended, times tangent0.
            Ivp_ReactionAddScaled(SpeedB, *reinterpret_cast<const float* const*>(thisPtr + 0x00), -(InvMassB * impulse[0]));
            // 06365f31-06365f6d: -(inv_mass*impulse[1]), times tangent1.
            Ivp_ReactionAddScaled(SpeedB, *reinterpret_cast<const float* const*>(thisPtr + 0x04), -(InvMassB * impulse[1]));
            // 06365f73-06365fff: rot_speed += -impulse[i] * basis2 row i of core1 (this+0xa4 / +0xb4).
            Ivp_ReactionAddScaled(RotSpeedB, reinterpret_cast<const float*>(thisPtr + 0xa4), -(double)(impulse[0]));
            Ivp_ReactionAddScaled(RotSpeedB, reinterpret_cast<const float*>(thisPtr + 0xb4), -(double)(impulse[1]));
        }
    }
}

namespace
{
    // The 0x114-byte stack object FUN_06365d50/FUN_063658f0 fill in
    // (FUN_06354120's frame holds two of them, at +0xa0 and +0x1b4). Its
    // interior "constructors" (FUN_06273bc0 over an empty ctor) do nothing.
    // Only the accumulator slots below are read back by the caller.
    struct IVP_Friction_Basis_Context
    {
        uint32_t Raw[0x114 / 4];

        unsigned char* Bytes() { return reinterpret_cast<unsigned char*>(Raw); }
        float At(int ByteOffset) const
        {
            return *reinterpret_cast<const float*>(reinterpret_cast<const unsigned char*>(Raw) + ByteOffset);
        }
        float A00() const { return At(0xd4); }   // effective inverse mass along TangentDir0
        float A01() const { return At(0xd8); }   // off-diagonal coupling term
        float A11() const { return At(0xe8); }   // ... along TangentDir1
        float RelVel0() const { return At(0x104); }
        float RelVel1() const { return At(0x108); }
    };
    static_assert(sizeof(IVP_Friction_Basis_Context) == 0x114, "friction-basis context size");

    // Retail inlines its dot products with a fixed accumulation order that
    // is NOT x-then-y-then-z, and keeps the sum on the x87 stack. FUN_06354120
    // uses the z,y,x form for its four tangent-basis projections
    // (063542a5-06354372) and the x,z,y form once, for the axis-lock
    // alignment term (063544a9-063544d0).
    inline double IVP_DotZYX(const float* A, const float* B)
    {
        return ((double)(A[2]) * B[2] + (double)(A[1]) * B[1]) +
               (double)(A[0]) * B[0];
    }
    inline double IVP_DotXZY(const float* A, const float* B)
    {
        return ((double)(A[0]) * B[0] + (double)(A[2]) * B[2]) +
               (double)(A[1]) * B[1];
    }

    double IVP_NormalizeX87(Vector* V); // FUN_06347ef0, defined below

    // A .rdata float constant by its exact bits.
    inline float IVP_Contact_Float_From_Bits(uint32_t Bits)
    {
        float Value;
        std::memcpy(&Value, &Bits, sizeof(Value));
        return Value;
    }

    // The 2x2 solve both paths of FUN_06354120 inline (06354401 and
    // 06354793), from the context's A00 (+0xd4), A01 (+0xd8), A11 (+0xe8):
    // Det = A00*A11 - A01*A01 kept; Det*Det below 1e-20 (DAT_063a5658 =
    // 0x1e3ce509, one ulp above 1e-20f) or unordered -> no impulse (false). InvDet = 1/Det kept;
    // M00 = float(A11*InvDet), M01 = float(-(A01*InvDet)), M11 = InvDet*A00
    // kept; Impulse0 = float(M01*B1 + M00*B0), Impulse1 = float(M11*B1 + M01*B0).
    bool IVP_SolveFrictionRow(const IVP_Friction_Basis_Context& Ctx, double B0, double B1, float Impulse[2])
    {
        const double A00 = Ctx.A00();
        const double A01 = Ctx.A01();
        const double A11 = Ctx.A11();
        const double Det = A00 * A11 - A01 * A01;
        if (!(Det * Det >= (double)(IVP_Contact_Float_From_Bits(0x1e3ce509u))))
        {
            return false;
        }
        const double InvDet = 1.0 / Det;
        const float M00 = (float)(A11 * InvDet);
        const float M01 = (float)(-(A01 * InvDet));
        const double M11 = InvDet * A00;
        Impulse[0] = (float)((double)(M01) * B1 + (double)(M00) * B0);
        Impulse[1] = (float)(M11 * B1 + (double)(M01) * B0);
        return true;
    }

    // ========================================================================
    // FUN_063548e0. The single-body ("SolverMode == 1") half of IVP_ListManager::ClampFrictionCone's
    // per-dist dispatch, and the exact one-dimensional counterpart of
    // IVP_ApplyContactReactionPair below: instead of solving a 2x2 row in a
    // tangent-plane basis, it solves ONE scalar impulse magnitude along a
    // single friction direction.
    //
    //   1. Direction = RecomputeTangentDirection(), which in this SolverMode
    //      also returns ClampTangentDirection's refinement scalar.
    //   2. Cores[0..1] = Object1->friction_core and Object2->friction_core
    //      (+0x9c on both, NOT ->core/+0x98). Any core with (Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) != 0
    //      is kinematic and is skipped everywhere below, contributing nothing.
    //   3. RelativeSpeed = dot(v0 - v1, Direction), with vN the world-space
    //      velocity of each body at its own contact point (Owner->CoordOS[N]).
    //   4. Denominator = sum over both bodies of the point-velocity response
    //      to a unit impulse of +-Direction along itself -- the standard
    //      scalar effective mass. Body 0 gets +1, body 1 gets -1
    //      (DAT_063a5498 = -1.0f, applied at the end of each iteration, so
    //      the flip happens even for a skipped kinematic body).
    //   5. If Denominator <= 1e-10f (DAT_063a53d8) nothing is applied.
    //      Otherwise Scale = (Refinement * EventSim->delta_time -
    //      RelativeSpeed) / Denominator, clamped -- retail clamps by
    //      REPLACING with +FrictionLimit whenever |Scale| exceeds it, which
    //      discards Scale's sign; that is retail's own behaviour (06354b59).
    //   6. Scale * the per-body linear/angular deltas is added into each
    //      live core's Speed (+0xa4) and RotSpeed (+0x94).
    //
    // Step 1's refinement scalar comes from ClampTangentDirection
    // (FUN_06353b50). x87: FrictionLimit, RelativeSpeed and each Denominator
    // step are one rounding each; Scale stays extended through the apply loop.
    void IVP_ApplyContactReactionSingle(IVP_Contact_Point* Contact,
                                         const IVP_Event_Sim* EventSim)
    {
        Vector Direction;
        const float Refinement = Contact->RecomputeTangentDirection(&Direction);

        IVP_Core* Cores[2];
        Cores[0] = Contact->Object1->friction_core;
        Cores[1] = Contact->Object2->friction_core;

        IVP_Impact_Solver_Long_Term* Frame = Contact->Owner;
        const Vector* const ContactPointOS[2] = { &Frame->CoordOS[0], &Frame->CoordOS[1] };

        // (+0x54 * +0x44) * delta_time on the x87 stack, one float rounding
        // (local_e8).
        const float FrictionLimit = (float)(
            ((double)(Contact->NowFrictionPressure) * Contact->FrictionCoefficient)
            * EventSim->delta_time);

        // Each body's current world-space velocity at its own contact point;
        // a kinematic core contributes nothing (LAB_0635494d).
        Vector PointSpeed[2];
        for (int Side = 0; Side < 2; ++Side)
        {
            if ((Cores[Side]->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) != 0)
                PointSpeed[Side] = Vector{ 0.0f, 0.0f, 0.0f };
            else
                Cores[Side]->calc_speed_at_object_position(ContactPointOS[Side], &PointSpeed[Side]);
        }

        // Retail's z,y,x accumulation; the three differences stay on the x87
        // stack unrounded and the sum is rounded once (local_c4).
        const double DeltaX = (double)(PointSpeed[0].x) - PointSpeed[1].x;
        const double DeltaY = (double)(PointSpeed[0].y) - PointSpeed[1].y;
        const double DeltaZ = (double)(PointSpeed[0].z) - PointSpeed[1].z;
        const float RelativeSpeed = (float)(
            (DeltaZ * Direction.z + DeltaY * Direction.y) + DeltaX * Direction.x);

        Vector LinearDelta[2];
        Vector AngularDelta[2];
        float Denominator = 0.0f; // local_f0
        float Sign = 1.0f;        // local_f4

        for (int Side = 0; Side < 2; ++Side)
        {
            IVP_Core* Core = Cores[Side];

            // Retail zeroes both delta slots BEFORE the kinematic test, so a
            // skipped body still contributes a well-defined zero to step 6.
            LinearDelta[Side] = Vector{ 0.0f, 0.0f, 0.0f };
            AngularDelta[Side] = Vector{ 0.0f, 0.0f, 0.0f };

            if ((Core->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) == 0)
            {
                Vector ImpulseWS;
                ImpulseWS.x = Direction.x * Sign;
                ImpulseWS.y = Direction.y * Sign;
                ImpulseWS.z = Direction.z * Sign;

                // FUN_06366010 == IVP_U_Matrix3::vimult: the same
                // impulse, rotated into this core's object space, which is
                // the frame calc_impulse_deltas needs for its cross product.
                IVP_U_Point ImpulseOS;
                Core->MWorldFCore.vimult(reinterpret_cast<const IVP_U_Point*>(&ImpulseWS), &ImpulseOS);

                Core->calc_impulse_deltas(ContactPointOS[Side],
                                          reinterpret_cast<const Vector*>(&ImpulseOS),
                                          &ImpulseWS,
                                          &LinearDelta[Side], &AngularDelta[Side]);

                Vector PointVelocityDelta;
                Core->calc_point_speed_ws(ContactPointOS[Side], &LinearDelta[Side],
                                           &AngularDelta[Side], &PointVelocityDelta);

                // The dot product stays on the x87 stack; one rounding per side.
                Denominator = (float)(
                    (((double)(PointVelocityDelta.z) * ImpulseWS.z
                      + (double)(PointVelocityDelta.y) * ImpulseWS.y)
                     + (double)(PointVelocityDelta.x) * ImpulseWS.x)
                    + Denominator);
            }

            Sign *= -1.0f; // DAT_063a5498, applied on BOTH the taken and skipped paths
        }

        if (!(Denominator > 1e-10f)) // DAT_063a53d8; <= and unordered return
            return;

        // Scale stays on the x87 stack for the whole apply loop. |Scale| >
        // Limit replaces it with the float Limit; <= or unordered keeps it.
        double Scale = ((double)(Refinement) * EventSim->delta_time - RelativeSpeed) / Denominator;
        if (std::fabs(Scale) > (double)(FrictionLimit))
            Scale = FrictionLimit; // retail replaces outright -- sign is intentionally dropped

        // One rounding per store.
        for (int Side = 0; Side < 2; ++Side)
        {
            IVP_Core* Core = Cores[Side];
            if ((Core->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) != 0)
                continue;

            Core->Speed.x = (float)(Scale * LinearDelta[Side].x + Core->Speed.x);
            Core->Speed.y = (float)(Scale * LinearDelta[Side].y + Core->Speed.y);
            Core->Speed.z = (float)(Scale * LinearDelta[Side].z + Core->Speed.z);

            Core->RotSpeed.x = (float)(Scale * AngularDelta[Side].x + Core->RotSpeed.x);
            Core->RotSpeed.y = (float)(Scale * AngularDelta[Side].y + Core->RotSpeed.y);
            Core->RotSpeed.z = (float)(Scale * AngularDelta[Side].z + Core->RotSpeed.z);
        }
    }

    // ========================================================================
    // FUN_06354120. The two-body tangential-impulse kernel IVP_ListManager::ClampFrictionCone dispatches
    // to for every dist whose SolverMode is not 1. It solves the 2x2 friction
    // LCP row for this dist:
    //
    //   [A00 A01] [i0]   [dot(T0, SpanFrictionS)*EventSim->i_delta_time - v0]
    //   [A01 A11] [i1] = [dot(T1, SpanFrictionS)*EventSim->i_delta_time - v1]
    //
    // where A/v come from FUN_06365d50 (the shared effective-inverse-mass +
    // relative-velocity builder), clamps the solved impulse into the Coulomb
    // cone, and hands it to FUN_06365df0 to be applied to the cores.
    //
    // Two paths, chosen by the four-test gate at 063541bb:
    //
    //  * AXIS-LOCK PATH -- taken only when Owner->ContactCores[0] has an
    //    AxisLock (IVP_Core+0x10, see ivp_core.h), Owner->ContactCores[1]
    //    is null, and either the next dist of the system
    //    (ListManagerListNext, +0x00) is null
    //    or its NowFrictionPressure (+0x54) is exactly 0. It replaces Owner's
    //    own tangent pair with a basis built from the locked axis (column
    //    [Owner->+0x74] of the lock frame, rotated into world space by the
    //    OPPOSING core's MWorldFCore), solves in that basis instead, refreshes
    //    the lock's cached contact point and timestamp, and applies one of
    //    three different clamps depending on the lock's +0x64 gate. It always
    //    returns 0 (DAT_063a53d4 on both exits).
    //  * GENERIC PATH -- everything else. Solves in Owner's own
    //    TangentAxis0/TangentAxis1 pair with both cores, clamps the solved
    //    impulse length to the friction limit, applies it, and returns the
    //    per-PSI INCREMENT of TangentWork (+0x50) -- that increment is what
    //    IVP_ListManager::ClampFrictionCone accumulates into the pair's IntegratedAntiEnergy.
    //
    // Retail constants:
    //   DAT_063a53d0 = 1.0f       DAT_063a53d4 = 0.0f
    //   DAT_063a5460 = 0.5f       DAT_063a559c = 0.3f
    //   DAT_063a5604 = 0.001f     DAT_063a5610 = 1e-6f
    //   DAT_063a5658 = 0x1e3ce509 (one ulp above 1e-20f)
    //   .data:063edcd0 = 0.9f (Ghidra shows it as the string "fff?ERROR: ";
    //       the bytes 66 66 66 3F are a float read by FMUL).
    //
    // x87: values retail keeps on the FPU stack are doubles here, rounded only
    // where retail stores a float; every compare uses the unrounded operand
    // and puts NaN on retail's side; the cross products are FUN_0628bed0
    // (IVP_U_Point::calc_cross_product) and the normalize is FUN_06347ef0.
    //
    // Both basis contexts are zero-filled before use. Retail leaves everything
    // outside FUN_06365d50's own accumulator reset (+0xd4..+0x10f) as stack
    // garbage, and FUN_063658f0 writes every block that is read back, so the
    // zeroing changes no value retail consumes.
    float IVP_ApplyContactReactionPair(IVP_Contact_Point* Contact,
                                        const IVP_Event_Sim* EventSim)
    {
        // (c * p) * dt kept extended (a float copy in local_2b4); only
        // >= 1e-6f goes on.
        const double FrictionLimitExt =
            ((double)(Contact->FrictionCoefficient) * Contact->NowFrictionPressure) * EventSim->delta_time;
        const float FrictionLimit = (float)(FrictionLimitExt);
        if (!(FrictionLimitExt >= (double)(1e-6f))) // DAT_063a5610
            return 0.0f;

        IVP_Impact_Solver_Long_Term* Frame = Contact->Owner;
        IVP_Core* CoreA = Frame->ContactCores[0];
        IVP_Core* CoreB = Frame->ContactCores[1];
        const IVP_U_Point* const NormalPt = reinterpret_cast<const IVP_U_Point*>(&Frame->ContactNormalWS);
        const float* const Axis0 = reinterpret_cast<const float*>(&Frame->TangentAxis0);
        const float* const Axis1 = reinterpret_cast<const float*>(&Frame->TangentAxis1);

        IVP_Friction_Basis_Context Ctx;
        std::memset(&Ctx, 0, sizeof(Ctx));

        // --- gate ------------------------------------------------------------
        bool bUseAxisLock = (CoreA != nullptr) && (CoreA->AxisLock != nullptr) && (CoreB == nullptr);
        if (bUseAxisLock && Contact->ListManagerListNext != nullptr)
        {
            // Only a pressure that is ordered and non-zero leaves the lock
            // path (equal-or-unordered stays).
            const double NextPressure = Contact->ListManagerListNext->NowFrictionPressure;
            if (NextPressure < 0.0 || NextPressure > 0.0)
                bUseAxisLock = false;
        }

        if (bUseAxisLock)
        {
            IVP_Core_AxisLock* Lock = CoreA->AxisLock;
            unsigned char* Owner = static_cast<unsigned char*>(Lock->Owner);
            const int Axis = *reinterpret_cast<const int*>(
                Owner + IVP_Core_AxisLock::OwnerAxisIndexOffset);
            IVP_Real_Object* OppObject = **reinterpret_cast<IVP_Real_Object***>(
                Owner + IVP_Core_AxisLock::OwnerObjectSlotOffset);
            IVP_Core* OppCore = OppObject->core;

            // Column `Axis` of the lock frame, rotated to world space
            // (FUN_06348580 == IVP_U_Matrix3::vmult on OppCore->MWorldFCore).
            IVP_U_Point LockAxisOS;
            LockAxisOS.k[0] = Lock->LockFrame.rows[0].k[Axis];
            LockAxisOS.k[1] = Lock->LockFrame.rows[1].k[Axis];
            LockAxisOS.k[2] = Lock->LockFrame.rows[2].k[Axis];
            LockAxisOS.k[3] = 0.0f;

            IVP_U_Point LockAxisWS;
            OppCore->MWorldFCore.vmult(&LockAxisOS, &LockAxisWS);

            // Tangent0 = LockAxisWS x Normal (FUN_0628bed0); ((x*x + z*z) +
            // y*y) below 0.001f or unordered falls to the generic path
            // (LAB_06354756).
            IVP_U_Point Tangent0;
            Tangent0.calc_cross_product(&LockAxisWS, NormalPt);
            const double Tangent0LenSq =
                ((double)(Tangent0.k[0]) * Tangent0.k[0] + (double)(Tangent0.k[2]) * Tangent0.k[2]) +
                (double)(Tangent0.k[1]) * Tangent0.k[1];

            if (Tangent0LenSq >= (double)(0.001f)) // DAT_063a5604
            {
                // FUN_06347ef0 on Tangent0 (its own 1e-10
                // cutoff included), then Tangent1 = Tangent0 x Normal.
                IVP_NormalizeX87(reinterpret_cast<Vector*>(Tangent0.k));
                IVP_U_Point Tangent1;
                Tangent1.calc_cross_product(&Tangent0, NormalPt);

                // Projections of the new basis onto Owner's
                // own tangent pair, one rounding each.
                const float D00 = (float)(IVP_DotZYX(Tangent0.k, Axis0)); // local_264
                const float D10 = (float)(IVP_DotZYX(Tangent1.k, Axis0)); // local_250
                const float D01 = (float)(IVP_DotZYX(Tangent0.k, Axis1)); // local_298
                const float D11 = (float)(IVP_DotZYX(Tangent1.k, Axis1)); // local_278

                // This dist's span in the locked basis.
                const float P0 = (float)((double)(D01) * Contact->SpanFrictionS[1] +
                                                    (double)(D00) * Contact->SpanFrictionS[0]);
                const float P1 = (float)((double)(D11) * Contact->SpanFrictionS[1] +
                                                    (double)(D10) * Contact->SpanFrictionS[0]);

                ReactionFrameInit(Ctx.Bytes(), reinterpret_cast<unsigned char*>(CoreA), nullptr,
                             &Frame->SolverContactPointWS, Tangent0.k, Tangent1.k, nullptr);

                const float A11 = Ctx.A11(); // raw copy into local_2b8

                // A one-axis context for the OPPOSING core
                // along Tangent1 only; its A00 divides the gain below.
                IVP_Friction_Basis_Context OppCtx;
                std::memset(&OppCtx, 0, sizeof(OppCtx));
                ReactionFrameInit(OppCtx.Bytes(), reinterpret_cast<unsigned char*>(OppCore), nullptr,
                             &Frame->SolverContactPointWS, Tangent1.k, nullptr, nullptr);
                const float OppA00 = OppCtx.A00();

                // B = P * i_delta_time - RelVel, kept.
                const double B0 = (double)(P0) * EventSim->i_delta_time - Ctx.RelVel0();
                const double B1 = (double)(P1) * EventSim->i_delta_time - Ctx.RelVel1();

                float Impulse[2];
                if (!IVP_SolveFrictionRow(Ctx, B0, B1, Impulse))
                    return 0.0f;

                // Gain = ((|(Lx*T1x + Lz*T1z) + Ly*T1y| *
                // A11) / OppA00 + 1.0) * 0.3f, one rounding.
                const float Gain = (float)(
                    ((std::fabs(IVP_DotXZY(LockAxisWS.k, Tangent1.k)) * A11) / OppA00 + 1.0) *
                    (double)(0.3f)); // DAT_063a559c

                // Impulse0^2 rounded into local_2b8,
                // Impulse1^2 stays on the stack.
                const float Impulse0Sq = (float)((double)(Impulse[0]) * Impulse[0]);
                const double Impulse1SqExt = (double)(Impulse[1]) * Impulse[1];

                // Refresh the lock's cached contact point and timestamp.
                Lock->ContactPoint.k[0] = Frame->SolverContactPointWS.x;
                Lock->ContactPoint.k[1] = Frame->SolverContactPointWS.y;
                Lock->ContactPoint.k[2] = Frame->SolverContactPointWS.z;
                Lock->LastUpdateTime = EventSim->environment->current_time;

                const double OppInvMass = OppCore->InvMass; // +0x40, re-read through the lock owner each time
                const double Pressure = Contact->NowFrictionPressure;

                if (Lock->Field_0x64 != nullptr)
                {
                    // G = ((Impulse1Sq + Impulse0Sq) * Gain) * Gain kept (and a
                    // float copy); <= FL*FL or unordered -> LAB_063545eb.
                    const double GainedExt = ((Impulse1SqExt + Impulse0Sq) * Gain) * Gain;
                    const float GainedF = (float)(GainedExt);
                    if (GainedExt > (double)(FrictionLimit) * FrictionLimit)
                    {
                        const double Scale = (IVP_InvSqrtFloat(GainedF) * Gain) * FrictionLimit; // FUN_06347df0
                        Impulse[0] = (float)(Scale * Impulse[0]);
                        Impulse[1] = (float)(Scale * Impulse[1]);

                        const double TangentScale = Scale / Gain;
                        Contact->SpanFrictionS[0] = (float)(TangentScale * Contact->SpanFrictionS[0]);
                        Contact->SpanFrictionS[1] = (float)(TangentScale * Contact->SpanFrictionS[1]);
                        Lock->ContactImpulse = (float)((OppInvMass * (1.0 - TangentScale)) * Pressure);
                    }
                    else
                    {
                        // LAB_063545eb.
                        const double Half = (double)(Gain) * 0.5; // DAT_063a5460
                        Impulse[0] = (float)(Half * Impulse[0]);
                        Impulse[1] = (float)(Half * Impulse[1]);
                        Lock->ContactImpulse = 0.0f;
                    }
                }
                else
                {
                    // LAB_06354613: FL*FL rounded into local_29c here; only the
                    // Tangent1 term is gain-weighted in the first test. On
                    // each test <= or unordered skips.
                    const float LimitSqF = (float)((double)(FrictionLimit) * FrictionLimit);
                    const double GainedExt = ((Impulse1SqExt * Gain) * Gain) + Impulse0Sq;
                    if (GainedExt > LimitSqF)
                    {
                        const double SolvedExt = Impulse1SqExt + Impulse0Sq;
                        const float SolvedF = (float)(SolvedExt); // local_294
                        if (SolvedExt > LimitSqF)
                        {
                            // Isotropic clamp.
                            const double Scale = IVP_InvSqrtFloat(SolvedF) * FrictionLimit;
                            Impulse[0] = (float)(Scale * Impulse[0]);
                            Impulse[1] = (float)(Scale * Impulse[1]);
                            Contact->SpanFrictionS[0] = (float)(Scale * Contact->SpanFrictionS[0]);
                            Contact->SpanFrictionS[1] = (float)(Scale * Contact->SpanFrictionS[1]);
                            Lock->ContactImpulse = (float)((OppInvMass * (1.0 - Scale)) * Pressure);
                        }
                        else
                        {
                            // LAB_063546ad: keep Impulse[0], give Impulse[1] the
                            // headroom left, sqrt(LimitSqF - Impulse0Sq), negated
                            // unless the old Impulse[1] > 0 (strictly).
                            const float OldImpulse1 = Impulse[1];
                            double NewImpulse1 = std::sqrt((double)(LimitSqF) - Impulse0Sq);
                            if (!(OldImpulse1 > 0.0f))
                                NewImpulse1 = -NewImpulse1;
                            Impulse[1] = (float)(NewImpulse1);

                            // Ratio = (new_f * 0.9f) / (old *
                            // Gain) kept; ContactImpulse = float(((InvMass * (1 -
                            // Ratio)) * p) * 0.3f).
                            const double Ratio = ((double)(Impulse[1]) * (double)(0.9f)) /
                                                 ((double)(OldImpulse1) * Gain);
                            Lock->ContactImpulse = (float)(((OppInvMass * (1.0 - Ratio)) * Pressure) *
                                                                      (double)(0.3f));

                            // Ratio >= 1.0 skips; below or unordered projects
                            // (P0, Ratio*P1) back onto Owner's tangent axes.
                            if (!(Ratio >= 1.0))
                            {
                                const double ScaledP1 = Ratio * P1;
                                Contact->SpanFrictionS[0] =
                                    (float)(ScaledP1 * D10 + (double)(P0) * D00);
                                Contact->SpanFrictionS[1] =
                                    (float)(ScaledP1 * D11 + (double)(P0) * D01);
                            }
                        }
                    }
                }

                ReactionFrameApplyImpulse(Ctx.Bytes(), reinterpret_cast<unsigned char*>(CoreA), nullptr, Impulse);
                return 0.0f;
            }

            // Cross product degenerate -- fall through to the generic path
            // exactly as retail's own LAB_06354756 does.
        }

        // --- generic two-core path, LAB_0635475a ----------------------------
        ReactionFrameInit(Ctx.Bytes(), reinterpret_cast<unsigned char*>(CoreA),
                     reinterpret_cast<unsigned char*>(CoreB),
                     &Frame->SolverContactPointWS, Axis0, Axis1, nullptr);

        // B = i_delta_time * Span - RelVel, kept.
        const double B0 = (double)(EventSim->i_delta_time) * Contact->SpanFrictionS[0] - Ctx.RelVel0();
        const double B1 = (double)(EventSim->i_delta_time) * Contact->SpanFrictionS[1] - Ctx.RelVel1();

        float Impulse[2];
        if (!IVP_SolveFrictionRow(Ctx, B0, B1, Impulse))
            return 0.0f;

        // S = Impulse1^2 + Impulse0^2 kept, with a float copy (local_2b8,
        // pre-clamp; the work term below uses it); <= FL*FL or unordered
        // keeps the impulse.
        const double SolvedExt = (double)(Impulse[1]) * Impulse[1] + (double)(Impulse[0]) * Impulse[0];
        const float SolvedF = (float)(SolvedExt);
        if (SolvedExt > (double)(FrictionLimit) * FrictionLimit)
        {
            const double Scale = IVP_InvSqrtFloat(SolvedF) * FrictionLimit; // FUN_06347df0
            Impulse[0] = (float)(Scale * Impulse[0]);
            Impulse[1] = (float)(Scale * Impulse[1]);
        }

        ReactionFrameApplyImpulse(Ctx.Bytes(), reinterpret_cast<unsigned char*>(CoreA),
                     reinterpret_cast<unsigned char*>(CoreB), Impulse);

        // Work = sqrt((S0*S0 + S1*S1) * ((dt*dt) *
        // SolvedF)) * 0.5f kept; return float(Work - TangentWork), then
        // TangentWork = float(Work).
        const double Span0 = Contact->SpanFrictionS[0];
        const double Span1 = Contact->SpanFrictionS[1];
        const double Dt = EventSim->delta_time;
        const double Work = std::sqrt((Span0 * Span0 + Span1 * Span1) * ((Dt * Dt) * SolvedF)) * 0.5; // DAT_063a5460

        const float Delta = (float)(Work - Contact->TangentWork);
        Contact->TangentWork = (float)(Work);
        return Delta;
    }

    // FUN_0628be60 applied to Axis0 * Span0: per component float(Span0 * Axis0.c), then
    // float(Span1 * Axis1.c + that).
    Vector IVP_TangentSpanWS(const IVP_Impact_Solver_Long_Term* Frame, const IVP_Contact_Point* Record)
    {
        const double Span0 = (double)(Record->SpanFrictionS[0]);
        const double Span1 = (double)(Record->SpanFrictionS[1]);
        Vector Out;
        Out.x = (float)(Span0 * Frame->TangentAxis0.x);
        Out.y = (float)(Span0 * Frame->TangentAxis0.y);
        Out.z = (float)(Span0 * Frame->TangentAxis0.z);
        Out.x = (float)(Span1 * Frame->TangentAxis1.x + Out.x);
        Out.y = (float)(Span1 * Frame->TangentAxis1.y + Out.y);
        Out.z = (float)(Span1 * Frame->TangentAxis1.z + Out.z);
        return Out;
    }

    // FUN_06354dc0, with retail's x87 rounding points (extended kept as double, float at
    // each store):
    //   Sign = -1 unless A->Object1->core == B->Object1->core (+0x10 -> +0x98);
    //   TA / TB = the two tangential spans in world space (FUN_0628be60);
    //   R = float(A->Owner->+0x20 - B->Owner->+0x20) per component, then FUN_06347ef0:
    //       below 1e-10 (or unordered) R is LEFT AS IS, otherwise normalised;
    //   DA = (Rz*TAz + Ry*TAy) + Rx*TAx;           PA = (float Rx*DA, float Ry*DA, ext Rz*DA)
    //   DB = ((Rz*TBz + Ry*TBy) + Rx*TBx) * Sign;  PB = float(R * DB)
    //   M  = (ext (PBx+PAx)*0.5, float((PBy+PAy)*0.5), float(float(PBz+PAz)*0.5))
    //   DeltaA = float((M - PA) * Weight)  -- ext Mx and ext PAz
    //   DeltaB = float((float(M) - PB) * (Sign * Weight))
    //   *AccumForA += DeltaA, *AccumForB += DeltaB (FUN_06355020, float adds).
    void IVP_MergeContactPairImpulse(IVP_Contact_Point* A, IVP_Contact_Point* B,
                                      Vector* AccumForA, Vector* AccumForB, float Weight)
    {
        const float Sign = (A->Object1->core != B->Object1->core) ? -1.0f : 1.0f;

        const IVP_Impact_Solver_Long_Term* FrameA = A->Owner;
        const IVP_Impact_Solver_Long_Term* FrameB = B->Owner;
        const Vector TA = IVP_TangentSpanWS(FrameA, A);
        const Vector TB = IVP_TangentSpanWS(FrameB, B);

        Vector R;
        R.x = FrameA->SolverContactPointWS.x - FrameB->SolverContactPointWS.x;
        R.y = FrameA->SolverContactPointWS.y - FrameB->SolverContactPointWS.y;
        R.z = FrameA->SolverContactPointWS.z - FrameB->SolverContactPointWS.z;
        IVP_NormalizeX87(&R); // return value unused (06354eb2)

        const double Rx = (double)(R.x);
        const double Ry = (double)(R.y);
        const double Rz = (double)(R.z);

        const double DA = (Rz * TA.z + Ry * TA.y) + Rx * TA.x;
        const float PAx = (float)(Rx * DA);
        const float PAy = (float)(Ry * DA);
        const double PAz = DA * Rz;

        const double DB = ((Rz * TB.z + Ry * TB.y) + Rx * TB.x) * (double)(Sign);
        const float PBx = (float)(Rx * DB);
        const float PBy = (float)(Ry * DB);
        const float PBz = (float)(Rz * DB);

        const double Half = 0.5; // DAT_063a5460 (0.5f)
        const double Sx = (double)(PBx) + PAx;
        const double Sy = (double)(PBy) + PAy;
        const float SzF = (float)((double)(PBz) + PAz);
        const float My = (float)(Sy * Half);
        const float Mz = (float)((double)(SzF) * Half);
        const double MxExt = Sx * Half;
        const float MxF = (float)(MxExt);

        const double W = (double)(Weight);
        Vector DeltaA;
        DeltaA.x = (float)((MxExt - PAx) * W);
        DeltaA.y = (float)(((double)(My) - PAy) * W);
        DeltaA.z = (float)(((double)(Mz) - PAz) * W);

        const double SignW = (double)(Sign) * W;
        Vector DeltaB;
        DeltaB.x = (float)(((double)(MxF) - PBx) * SignW);
        DeltaB.y = (float)(((double)(My) - PBy) * SignW);
        DeltaB.z = (float)(((double)(Mz) - PBz) * SignW);

        AccumForA->x = AccumForA->x + DeltaA.x;
        AccumForA->y = AccumForA->y + DeltaA.y;
        AccumForA->z = AccumForA->z + DeltaA.z;
        AccumForB->x = AccumForB->x + DeltaB.x;
        AccumForB->y = AccumForB->y + DeltaB.y;
        AccumForB->z = AccumForB->z + DeltaB.z;
    }

    // FUN_06355050, cdecl (pair, arena):
    //   n = pair->AttachedCtx.n_elems; Copy = arena->GetMem(n*4) (the inlined
    //     06347380 sequence).
    //   Weight = float(1.0 / (n + 1e-10f)), one rounding.
    //   Accum = _alloca(n * 16); from the top, Copy[i] = elems[i] and
    //     Accum[i].xyz = 0.
    //   for i in 0..n-2, j in i+1..n-1: Dot = ((Ni.z*Nj.z + Ni.y*Nj.y) +
    //     Ni.x*Nj.x) on the owners' normals, kept; | |Dot| - 1 | < 0.001f
    //     (DAT_063a5604) or unordered merges via 06354dc0(Copy[j], Copy[i],
    //     &Accum[j], &Accum[i], Weight).
    //   Forward, Copy[k]->AccumulateFrameTangent(&Accum[k]).
    // The 16-byte stack slots become Vectors in a std::vector (stack scratch,
    // freed on return either way); the arena copy is retail's.
    void IVP_MergeRedundantContacts(IVP_ListManager_CorePairEntry* Pair, IVP_U_Memory* Arena)
    {
        const int n = static_cast<int>(Pair->AttachedCtx.n_elems);
        IVP_Contact_Point** const Copy =
            static_cast<IVP_Contact_Point**>(Arena->GetMem(n * static_cast<int>(sizeof(IVP_Contact_Point*))));
        const float Weight = (float)(1.0 / ((double)(n) + (double)(1e-10f)));

        std::vector<Vector> Accum(static_cast<size_t>(n > 0 ? n : 0));
        for (int i = n - 1; i >= 0; --i)
        {
            Copy[i] = Pair->AttachedCtx.elems[i];
            Accum[static_cast<size_t>(i)].x = 0.0f;
            Accum[static_cast<size_t>(i)].y = 0.0f;
            Accum[static_cast<size_t>(i)].z = 0.0f;
        }

        for (int i = 0; i < n - 1; ++i)
        {
            IVP_Contact_Point* const ci = Copy[i];
            for (int j = i + 1; j < n; ++j)
            {
                IVP_Contact_Point* const cj = Copy[j];
                const Vector& NormalI = ci->Owner->ContactNormalWS;
                const Vector& NormalJ = cj->Owner->ContactNormalWS;
                const double Dot = ((double)(NormalI.z) * NormalJ.z +
                                    (double)(NormalI.y) * NormalJ.y) +
                                   (double)(NormalI.x) * NormalJ.x;
                if (!(std::fabs(std::fabs(Dot) - 1.0) >= (double)(0.001f))) // DAT_063a5604
                {
                    IVP_MergeContactPairImpulse(cj, ci, &Accum[static_cast<size_t>(j)], &Accum[static_cast<size_t>(i)],
                                                Weight);
                }
            }
        }

        for (int k = 0; k < n; ++k)
        {
            Copy[k]->AccumulateFrameTangent(Accum[static_cast<size_t>(k)]);
        }
    }
}

// FUN_06354d70, thiscall (&ImpulseChange), RET 4: S0 (+0x38) = float(((T0.z*I.z + T0.y*I.y) + T0.x*I.x)
// + S0), T0 = Owner(+0x40)->TangentAxis0 (+0x78); S1 (+0x3c) likewise with
// TangentAxis1 (+0x88). The dot stays on the x87 stack.
void IVP_Contact_Point::AccumulateFrameTangent(const Vector& ImpulseChange)
{
    const Vector& T0 = Owner->TangentAxis0;
    SpanFrictionS[0] = (float)((((double)(T0.z) * ImpulseChange.z +
                                            (double)(T0.y) * ImpulseChange.y) +
                                           (double)(T0.x) * ImpulseChange.x) +
                                          SpanFrictionS[0]);
    const Vector& T1 = Owner->TangentAxis1;
    SpanFrictionS[1] = (float)((((double)(T1.z) * ImpulseChange.z +
                                            (double)(T1.y) * ImpulseChange.y) +
                                           (double)(T1.x) * ImpulseChange.x) +
                                          SpanFrictionS[1]);
}

// FUN_06353f10. A Baraff-style normal-impulse LCP row solve for one PSI
// sub-step:
//   1. RelativeVelocity = Dot(Owner->ContactNormalWS, CoreA->Speed)
//                        + Dot(Owner->CoordOSCrossNormal[0], CoreA->RotSpeed)
//                        - Dot(Owner->ContactNormalWS, CoreB->Speed)
//                        - Dot(Owner->CoordOSCrossNormal[1], CoreB->RotSpeed)
//      with CoreA/CoreB = Owner->ContactCores[0/1] (either side is skipped
//      if null -- a one-sided/static-body contact).
//   2. Depth -= GapDistance (the surface gap); if the result is negative,
//      Gain is boosted x20.
//   3. Impulse = (Gain*Depth + RelativeVelocity) *
//      Owner->EffectiveMassDenomRecip. Impulse <= 0 clears
//      NowFrictionPressure to 0; otherwise NowFrictionPressure is set to
//      Impulse * EventSim->i_delta_time (the only field of the event read;
//      LAB_06356560 passes the IVP_Event_Sim it was handed).
//   4. The impulse is applied to whichever of CoreA/CoreB are non-null:
//      RotSpeed += (+/-Impulse) * (CoordOSCrossNormal (componentwise) *
//      InvRotInertia), Speed += (+/-Impulse * InvMass) * Normal -- opposite
//      signs for A vs B (action/reaction).
//
// x87 detail: each side's relative speed is ((C.z*Rot.z + C.y*Rot.y) +
// C.x*Rot.x) + ((Lin.z*N.z + Lin.y*N.y) + Lin.x*N.x), rounded once; D stays
// on the stack; D < 0 or unordered uses Gain * 20.0f (DAT_063a543c); the
// impulse is rounded once and <= 0 or unordered clears the pressure. CoreA's
// speed scale -(InvMass * Impulse) is rounded to float (an FUN_0628be60
// argument), CoreB's stays on the stack.
void IVP_Contact_Point::SolveNormalImpulse(const IVP_Event_Sim* EventSim, float Depth, float Gain)
{
    IVP_Impact_Solver_Long_Term* F = Owner;
    const Vector& N = F->ContactNormalWS;

    float RelSpeed = 0.0f;
    IVP_Core* CoreA = F->ContactCores[0];
    if (CoreA != nullptr)
    {
        const Vector& C0 = F->CoordOSCrossNormal[0];
        const double Rot = ((double)(C0.z) * CoreA->RotSpeed.z + (double)(C0.y) * CoreA->RotSpeed.y) +
                           (double)(C0.x) * CoreA->RotSpeed.x;
        const double Lin = ((double)(CoreA->Speed.z) * N.z + (double)(CoreA->Speed.y) * N.y) +
                           (double)(CoreA->Speed.x) * N.x;
        RelSpeed = (float)(Rot + Lin);
    }

    IVP_Core* CoreB = F->ContactCores[1];
    if (CoreB != nullptr)
    {
        const Vector& C1 = F->CoordOSCrossNormal[1];
        const double Rot = ((double)(C1.z) * CoreB->RotSpeed.z + (double)(C1.y) * CoreB->RotSpeed.y) +
                           (double)(C1.x) * CoreB->RotSpeed.x;
        const double Lin = ((double)(CoreB->Speed.z) * N.z + (double)(CoreB->Speed.y) * N.y) +
                           (double)(N.x) * CoreB->Speed.x;
        RelSpeed = (float)((double)(RelSpeed) - (Rot + Lin));
    }

    const double D = (double)(Depth) - (double)(GapDistance);
    double G = (double)(Gain);
    if (!(D >= 0.0))
    {
        G = (double)(Gain) * (double)(20.0f);
    }

    const float Impulse = (float)(((G * D) + (double)(RelSpeed)) *
                                             (double)(F->EffectiveMassDenomRecip));
    if (!(Impulse > 0.0f))
    {
        NowFrictionPressure = 0.0f;
        return;
    }

    NowFrictionPressure = (float)((double)(Impulse) * (double)(EventSim->i_delta_time));

    if (CoreA != nullptr)
    {
        const Vector& C0 = F->CoordOSCrossNormal[0];
        const Vector V = { (float)((double)(C0.x) * CoreA->InvRotInertia.x),
                           (float)((double)(C0.y) * CoreA->InvRotInertia.y),
                           (float)((double)(C0.z) * CoreA->InvRotInertia.z) };
        const double S = (double)(-Impulse);
        CoreA->RotSpeed.y = (float)(S * V.y + CoreA->RotSpeed.y);
        CoreA->RotSpeed.z = (float)(S * V.z + CoreA->RotSpeed.z);
        CoreA->RotSpeed.x = (float)(S * V.x + CoreA->RotSpeed.x);

        const double S2 = (double)((float)(-((double)(CoreA->InvMass) * Impulse)));
        CoreA->Speed.y = (float)(S2 * N.y + CoreA->Speed.y);
        CoreA->Speed.z = (float)(S2 * N.z + CoreA->Speed.z);
        CoreA->Speed.x = (float)(S2 * N.x + CoreA->Speed.x);
    }

    if (CoreB == nullptr)
        return;

    const Vector& C1 = F->CoordOSCrossNormal[1];
    const Vector V = { (float)((double)(C1.x) * CoreB->InvRotInertia.x),
                       (float)((double)(C1.y) * CoreB->InvRotInertia.y),
                       (float)((double)(C1.z) * CoreB->InvRotInertia.z) };
    const double S = (double)(Impulse);
    CoreB->RotSpeed.y = (float)(S * V.y + CoreB->RotSpeed.y);
    CoreB->RotSpeed.z = (float)(S * V.z + CoreB->RotSpeed.z);
    CoreB->RotSpeed.x = (float)(S * V.x + CoreB->RotSpeed.x);

    const double S2 = (double)(CoreB->InvMass) * (double)(Impulse);
    CoreB->Speed.y = (float)(S2 * N.y + CoreB->Speed.y);
    CoreB->Speed.z = (float)(S2 * N.z + CoreB->Speed.z);
    CoreB->Speed.x = (float)((double)(N.x) * S2 + CoreB->Speed.x);
}

// FUN_06353e80 converts this dist's 2D SpanFrictionS pair back into a
// world-space direction (the inverse projection of AccumulateFrameTangent
// above), normalizes it in place (FUN_06347e90), and in SolverMode 1 refines
// it via ClampTangentDirection. It returns, on the x87 stack, the
// pre-normalisation length from FUN_06347e90 (0.0f when lenSq < 1e-10f), or
// in SolverMode 1 ClampTangentDirection's result (the length is discarded).
// Its only caller is FUN_063548e0. The definition sits below
// ClampTangentDirection to share its x87 helpers.

namespace
{
    // The small vector helpers ClampTangentDirection calls, in retail's x87
    // precision. DAT_063a53d8 = 1e-10f, DAT_063a53d0 = 1.0f.

    // FUN_06347f50 (this = Out, RET 8): Out = A - (A.N) N.
    // Dot = (A.x*N.x + N.z*A.z) + A.y*N.y, negated, kept on the FPU; y and z are
    // computed first (z spilled through the argument slot), x last.
    void IVP_ProjectOutDirection(Vector* Out, const Vector& A, const Vector& N)
    {
        const double NegDot = -(((double)(A.x) * N.x + (double)(N.z) * A.z) +
                                (double)(A.y) * N.y);
        const float Y = (float)(NegDot * N.y + A.y);
        const float Z = (float)(NegDot * N.z + A.z);
        const float X = (float)(NegDot * N.x + A.x);
        Out->z = Z;
        Out->x = X;
        Out->y = Y;
    }

    // FUN_06347e60: sqrt((x*x + y*y) + z*z), returned unrounded.
    double IVP_VectorLengthX87(const Vector& V)
    {
        return std::sqrt(((double)(V.x) * V.x + (double)(V.y) * V.y) +
                         (double)(V.z) * V.z);
    }

    // FUN_06347ef0 / FUN_06347e90: lenSq below 1e-10f or unordered leaves V
    // untouched (ef0 returns 0 in EAX, e90
    // returns 0.0f); otherwise each component becomes float(c * 1/sqrt(lenSq)),
    // and e90 returns lenSq * (1/sqrt(lenSq)) unrounded.
    double IVP_NormalizeX87(Vector* V)
    {
        const double LenSq = ((double)(V->x) * V->x + (double)(V->y) * V->y) +
                             (double)(V->z) * V->z;
        if (!(LenSq >= (double)(1e-10f)))
        {
            return 0.0;
        }
        const double InvLen = 1.0 / std::sqrt(LenSq);
        V->x = (float)(InvLen * V->x);
        V->y = (float)(InvLen * V->y);
        V->z = (float)(InvLen * V->z);
        return LenSq * InvLen;
    }
}

// FUN_06353b50 (RET 4). Owner is read once; the "other side" material starts
// at Materials[1] and steps back per side.
//
// Per side whose material has SecondFrictionXEnabled (+8):
//   Axis = column 0 of Object->friction_core->MWorldFCore (+0xf4/+0x104/+0x114)
//   Perp = FUN_06347f50(Axis, Owner->ContactNormalWS); PerpLen = float(|Perp|)
//   K = Other->get_friction_factor() * float(Side->get_second_friction_factor())
//       (slot 1 on this side first, then slot 0 on the other)
//   FrictionCoefficient < 1e-10f or unordered -> return 0.0f, even on side 1
//   Range = float(((coef - (coef-K)*PerpLen) * 0.005f) / coef)   (DAT_063a5654)
//   W = S0*TangentAxis0 + S1*TangentAxis1 (FUN_0628be60: float(S1*T1.c + float(S0*T0.c)))
//   Perp normalised in place (FUN_06347ef0)
//   D = (Perp.z*W.z + Perp.y*W.y) + Perp.x*W.x; |D| <= Range or unordered -> next side
//   W2 = W - float(D)*Perp; P = Range*Perp, negated (* -1.0f, DAT_063a5498)
//     unless float(D) >= 0; P.z is spilled as float, P.x/P.y stay on the FPU
//   N = W2 + P -> *InOutDir; then S0 = N.T0, S1 = N.T1 ((z + y) + x order)
// After both sides: InOutDir = S0*T0 + S1*T1 (x, y spilled at float(S0*T.c),
// z kept unrounded), normalised by FUN_06347e90, whose return value is ours.
//
// One retail write is not reproduced: 06353d94 also copies the uninitialised
// stack dword at B+0x34 into InOutDir+0xc (the caller's 16-byte point's
// fourth slot). The port's Vector is 12 bytes and the value is indeterminate,
// so it is left out.
float IVP_Contact_Point::ClampTangentDirection(Vector* InOutDir)
{
    IVP_Material* Materials[2];
    ResolveSideMaterials(Materials);

    IVP_Impact_Solver_Long_Term* Contact = Owner;
    const Vector& T0 = Contact->TangentAxis0;
    const Vector& T1 = Contact->TangentAxis1;

    for (int Side = 0; Side < 2; ++Side)
    {
        IVP_Material* SideMaterial = Materials[Side];
        if (SideMaterial->SecondFrictionXEnabled == 0)
        {
            continue;
        }

        IVP_Real_Object* SideObject = (Side == 0) ? Object1 : Object2;
        const IVP_U_Matrix& CoreMatrix = SideObject->friction_core->MWorldFCore;
        Vector Axis;
        Axis.x = CoreMatrix.rows[0].k[0];
        Axis.y = CoreMatrix.rows[1].k[0];
        Axis.z = CoreMatrix.rows[2].k[0];

        Vector Perp;
        IVP_ProjectOutDirection(&Perp, Axis, Contact->ContactNormalWS);
        const float PerpLength = (float)(IVP_VectorLengthX87(Perp));

        const float SecondFriction = SideMaterial->get_second_friction_factor();
        const double K = (double)(Materials[1 - Side]->get_friction_factor()) * SecondFriction;

        if (!(FrictionCoefficient >= 1e-10f))
        {
            return 0.0f;
        }

        const double Coefficient = FrictionCoefficient;
        const double Scaled = (Coefficient - K) * PerpLength;
        const float Range = (float)(((Coefficient - Scaled) * (double)(0.005f)) / Coefficient);

        const float S0 = SpanFrictionS[0];
        const float S1 = SpanFrictionS[1];
        Vector Along0;
        Along0.x = (float)((double)(S0) * T0.x);
        Along0.y = (float)((double)(S0) * T0.y);
        Along0.z = (float)((double)(S0) * T0.z);
        Vector W;
        W.y = (float)((double)(S1) * T1.y + Along0.y);
        W.z = (float)((double)(S1) * T1.z + Along0.z);
        W.x = (float)((double)(S1) * T1.x + Along0.x);

        IVP_NormalizeX87(&Perp);

        const double D = ((double)(Perp.z) * W.z + (double)(Perp.y) * W.y) +
                         (double)(Perp.x) * W.x;
        const float DFloat = (float)(D);
        if (!(std::fabs(D) > (double)(Range)))
        {
            continue;
        }

        const double NegD = -DFloat;
        Vector W2;
        W2.y = (float)(NegD * Perp.y + W.y);
        W2.z = (float)(NegD * Perp.z + W.z);
        W2.x = (float)(NegD * Perp.x + W.x);

        double PushX = (double)(Perp.x) * Range;
        double PushY = (double)(Perp.y) * Range;
        float PushZ = (float)((double)(Perp.z) * Range);
        if (!(DFloat >= 0.0f))
        {
            PushY = -PushY;
            PushZ = -PushZ;
            PushX = -PushX;
        }

        const float NewY = (float)(W2.y + PushY);
        const float NewZ = (float)((double)(W2.z) + PushZ);
        const float NewX = (float)(W2.x + PushX);
        InOutDir->x = NewX;
        InOutDir->y = NewY;
        InOutDir->z = NewZ;

        SpanFrictionS[0] = (float)(((double)(NewZ) * T0.z + (double)(NewY) * T0.y) +
                                              (double)(NewX) * T0.x);
        SpanFrictionS[1] = (float)(((double)(NewZ) * T1.z + (double)(NewY) * T1.y) +
                                              (double)(NewX) * T1.x);
    }

    const float S0 = SpanFrictionS[0];
    const float S1 = SpanFrictionS[1];
    const float AlongX = (float)((double)(S0) * T0.x);
    const float AlongY = (float)((double)(S0) * T0.y);
    const double AlongZ = (double)(S0) * T0.z;
    const float DirY = (float)((double)(S1) * T1.y + AlongY);
    const double DirZ = (double)(S1) * T1.z + AlongZ;
    const float DirX = (float)((double)(S1) * T1.x + AlongX);
    InOutDir->x = DirX;
    InOutDir->y = DirY;
    InOutDir->z = (float)(DirZ);

    return (float)(IVP_NormalizeX87(InOutDir));
}

// FUN_06353e80 (see the block above ClampTangentDirection's helpers). OutDir
// first receives float(S0*T0.c) for y, z, x; then each becomes float(S1*T1.c
// + that float) (the S1 products are never rounded on their own);
// FUN_06347e90 normalises; in SolverMode 1 (+0x34) its length is dropped and
// FUN_06353b50's value returned. The sole caller stores the result as a float
// at once, so returning float is exact.
float IVP_Contact_Point::RecomputeTangentDirection(Vector* OutDir)
{
    const Vector& T0 = Owner->TangentAxis0;
    const Vector& T1 = Owner->TangentAxis1;
    const float S0 = SpanFrictionS[0];
    const float S1 = SpanFrictionS[1];

    OutDir->y = (float)((double)(S0) * T0.y);
    OutDir->z = (float)((double)(S0) * T0.z);
    OutDir->x = (float)((double)(S0) * T0.x);
    const float NewY = (float)((double)(S1) * T1.y + OutDir->y);
    const float NewZ = (float)((double)(S1) * T1.z + OutDir->z);
    OutDir->z = NewZ;
    OutDir->x = (float)((double)(S1) * T1.x + OutDir->x);
    OutDir->y = NewY;

    const double Length = IVP_NormalizeX87(OutDir);
    if (SolverMode == 1)
    {
        return ClampTangentDirection(OutDir);
    }
    return (float)(Length);
}

namespace
{
    // The friction-cone clamp of one dist, shared (identically) by
    // FUN_06354c10's inner loop (06354ca3) and LAB_06356660 (06356686).
    //   TangentSq = X*X + Y*Y, stored once as float and kept unrounded for
    //     the compare; the clamp runs only when TangentSq > ConeSq (ordered).
    //   FUN_06347df0(TangentSq as float) = 1.0f / sqrt (extended).
    //   RecheckFeatureFlags = (RecheckFeatureFlags & 0xffffff01) | 1.
    //   FrictionCorrection = ((InvLen * TangentSqF - Cone) * pressure (+0x54))
    //     * coefficient (+0x44) + FrictionCorrection, one rounding.
    //   Scale = InvLen * Cone; SpanFrictionS[0] = Scale * [0]; [1] likewise.
    void IVP_ClampDistIntoFrictionCone(IVP_Contact_Point* Dist, float Cone, double ConeSq)
    {
        const double X = (double)(Dist->SpanFrictionS[0]);
        const double Y = (double)(Dist->SpanFrictionS[1]);
        const double TangentSq = X * X + Y * Y;
        const float TangentSqF = (float)(TangentSq);
        if (!(TangentSq > ConeSq))
        {
            return;
        }

        const double InvLen = 1.0 / std::sqrt((double)(TangentSqF));
        Dist->RecheckFeatureFlags = (Dist->RecheckFeatureFlags & 0xffffff01u) | 1u;
        Dist->FrictionCorrection = (float)(
            ((InvLen * (double)(TangentSqF) - (double)(Cone)) *
             (double)(Dist->NowFrictionPressure)) *
                (double)(Dist->FrictionCoefficient) +
            (double)(Dist->FrictionCorrection));
        const double Scale = InvLen * (double)(Cone);
        Dist->SpanFrictionS[0] = (float)(Scale * (double)(Dist->SpanFrictionS[0]));
        Dist->SpanFrictionS[1] = (float)(Scale * (double)(Dist->SpanFrictionS[1]));
    }
}

// FUN_06354c10 (RET 4). Pairs from the last; per pair:
//   Sum = 0.0 (DAT_063a53d4), then for every dist from the top: Sum +=
//     (pressure (+0x54) * coefficient (+0x44)) * MassFactor (+0x30), unrounded.
//   Cone = float((dt * dt) * Sum); no dist -> next pair.
//   ConeSq = float(Cone * Cone + 1e-6f) -- rounded here, unlike LAB_06356660.
//   Per dist, top to bottom: the clamp above, then SolverMode == 1 ->
//     FUN_063548e0, else FUN_06354120 whose return is added into a float sum.
//   Sum > 0 (ordered) -> pair+0x24 += sum, one rounding.
void IVP_ListManager::ClampFrictionCone(const IVP_Event_Sim* EventSim)
{
    for (int s = static_cast<int>(CorePairEntries.n_elems) - 1; s >= 0; --s)
    {
        IVP_ListManager_CorePairEntry* pair = CorePairEntries.elems[s];

        const int Last = static_cast<int>(pair->AttachedCtx.n_elems) - 1;
        double Sum = 0.0;
        for (int i = Last; i >= 0; --i)
        {
            IVP_Contact_Point* cp = pair->AttachedCtx.elems[i];
            Sum = ((double)(cp->NowFrictionPressure) * (double)(cp->FrictionCoefficient)) *
                      (double)(cp->MassFactor) +
                  Sum;
        }

        const double Dt = (double)(EventSim->delta_time);
        const float Cone = (float)((Dt * Dt) * Sum);
        if (Last < 0)
        {
            continue;
        }
        const float ConeSq = (float)((double)(Cone) * (double)(Cone) +
                                                (double)(1e-6f));

        float correctionSum = 0.0f;
        for (int i = Last; i >= 0; --i)
        {
            IVP_Contact_Point* cp = pair->AttachedCtx.elems[i];
            IVP_ClampDistIntoFrictionCone(cp, Cone, (double)(ConeSq));

            if (cp->SolverMode == 1)
            {
                IVP_ApplyContactReactionSingle(cp, EventSim);
            }
            else
            {
                correctionSum = (float)((double)(IVP_ApplyContactReactionPair(cp, EventSim)) +
                                                   (double)(correctionSum));
            }
        }

        if (correctionSum > 0.0f)
        {
            pair->IntegratedAntiEnergy = (float)((double)(correctionSum) + (double)(pair->IntegratedAntiEnergy));
        }
    }
}

// FUN_063551c0 (no stack arguments). Pairs from the last: --[pair+0x18]; at
// zero FUN_06355050(pair, environment+0xa0, the IVP_U_Memory arena, re-read
// per call) and [pair+0x18] = 5.
void IVP_ListManager::AgeAndRemergeContactSets()
{
    for (int s = static_cast<int>(CorePairEntries.n_elems) - 1; s >= 0; --s)
    {
        IVP_ListManager_CorePairEntry* pair = CorePairEntries.elems[s];
        if (--pair->MergeCacheCountdown == 0)
        {
            IVP_MergeRedundantContacts(pair, Environment->short_term_mem);
            pair->MergeCacheCountdown = 5;
        }
    }
}

// FUN_06355590 (RET 4): FUN_06354c10(EventSim) then FUN_063551c0 on this.
void IVP_ListManager::StepFrictionSolver(const IVP_Event_Sim* EventSim)
{
    ClampFrictionCone(EventSim);
    AgeAndRemergeContactSets();
}

// LAB_06356660 (RET 8): slot 4 of the friction system's vtable (063a54c8),
// its do_simulation_controller. More than one dist (signed word +0x3e) ->
// FUN_06355590. Otherwise IVP_ListManager::ClampFrictionCone's inner loop on the single
// dist CtxListHead, with ConeSq = cone*cone + 1e-6f left unrounded, and
// FUN_06354120's return discarded. CoreList is not read.
void IVP_ListManager::do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* /*CoreList*/)
{
    if (static_cast<int16_t>(CtxListCount) > 1)
    {
        StepFrictionSolver(EventSim);
        return;
    }

    IVP_Contact_Point* cp = CtxListHead;

    // (((MassFactor * pressure) * coefficient) * dt) * dt, one rounding.
    const double Dt = (double)(EventSim->delta_time);
    const float Cone = (float)(
        (((double)(cp->MassFactor) * (double)(cp->NowFrictionPressure)) *
         (double)(cp->FrictionCoefficient)) * Dt * Dt);
    // Cone * Cone + 1e-6f, left unrounded (FUN_06354c10 rounds its ConeSq).
    const double ConeSq = (double)(Cone) * (double)(Cone) + (double)(1e-6f);
    IVP_ClampDistIntoFrictionCone(cp, Cone, ConeSq);

    if (cp->SolverMode == 1)
    {
        IVP_ApplyContactReactionSingle(cp, EventSim);
    }
    else
    {
        IVP_ApplyContactReactionPair(cp, EventSim);
    }
}

// ---------------------------------------------------------------------------
// FUN_06356bc0, IVP_Mindist::try_to_generate_managed_friction.
// ---------------------------------------------------------------------------
namespace
{
    // The stack event of 06356cad-06356cc6 (Ipion's IVP_Event_Friction):
    // environment, contact situation (the record's +0x40), the contact point.
    // It is passed to FUN_0634cfa0 (environment listeners, slot 2) and
    // FUN_0634b0e0 (per-object collision listeners, slot 2).
    struct FIVP_FrictionCreatedEvent
    {
        IVP_Environment* Environment;         // +0x00
        IVP_Impact_Solver_Long_Term* ContactSituation;        // +0x04
        IVP_Contact_Point* FrictionHandle; // +0x08
    };
}

//   Obj0 = synapse[(StateFlags >> 8) & 3].l_obj, Obj1 = synapse[((StateFlags
//     ^ 0x100) >> 8) & 3].l_obj; Core0/Core1 = their friction_core
//     (+0x9c). When Core0's flags & 0xc is
//     non-zero the two cores swap (the objects do not).
//   Info0 = Core0->+0x5c (FUN_0633ba90); cp = FUN_0635a950(this, &Tag).
//   Tag != 1 -> *OutFs = Info0->FrictionSystem (no null test), *OutHavingNew
//     = 0, and with bCallRecalcSvals FUN_0635a1e0(cp) + FUN_06358c50(cp,
//     cp->Owner); return cp.
//   New cp -> the same two calls under bCallRecalcSvals; event
//     {Obj0->environment, cp->Owner, cp}; FUN_0634cfa0 on that environment;
//     for Obj0 then Obj1 with flags (+0x78) bit 0x2000 -> FUN_0634b0e0
//     (Obj0's environment for both); *OutHavingNew = 1.
//   Friction system dispatch (see the branches below).
//   Info0 / Info1 ->FrDists.Add(cp) (FUN_063556b0); *OutFs = Fs;
//     FUN_063562d0(cp).
//   Sim1 = Core1->+0x15c, Sim0 = Core0->+0x15c; when
//     neither core has (Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) and Sim0 != Sim1: the unit that is
//     SimUnitNotDestroy absorbs the other (FUN_06348c60), and the other is
//     freed (FUN_06348d80 + operator delete FUN_06391d5a) when non-null.
// operator new failing is not modelled (retail would store through null).
IVP_Contact_Point* IVP_Ledge_Mindist::try_to_generate_managed_friction(IVP_ListManager** OutFrictionSystem,
                                                                       int32_t* OutHavingNew,
                                                                       IVP_Simulation_Unit* SimUnitNotDestroy,
                                                                       int32_t bCallRecalcSvals)
{
    IVP_Real_Object* const Obj0 = synapse[(StateFlags >> 8) & 3u].l_obj;
    IVP_Real_Object* const Obj1 = synapse[((StateFlags ^ 0x100u) >> 8) & 3u].l_obj;
    IVP_Core* Core0 = Obj0->friction_core;
    IVP_Core* Core1 = Obj1->friction_core;
    if ((Core0->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) != 0)
    {
        IVP_Core* const Swap = Core0;
        Core0 = Core1;
        Core1 = Swap;
    }

    IVP_Friction_Info_For_Core* Info0 = static_cast<IVP_Friction_Info_For_Core*>(Core0->ObjectHashMap);
    IVP_Environment* const CoreEnvironment = Core0->Environment;

    int32_t Tag = 0;
    IVP_Contact_Point* const Cp = IVP_FindOrCreatePairRecord(this, &Tag);
    if (Tag != 1)
    {
        *OutFrictionSystem = Info0->FrictionSystem;
        *OutHavingNew = 0;
        if (bCallRecalcSvals != 0)
        {
            Cp->UpdateContactFrame();              // FUN_0635a1e0
            Cp->PopulateOwner(Cp->Owner);    // FUN_06358c50
        }
        return Cp;
    }

    if (bCallRecalcSvals != 0)
    {
        Cp->UpdateContactFrame();
        Cp->PopulateOwner(Cp->Owner);
    }

    IVP_Environment* const ObjEnvironment = Obj0->environment;
    FIVP_FrictionCreatedEvent Event;
    Event.Environment = ObjEnvironment;
    Event.ContactSituation = Cp->Owner;
    Event.FrictionHandle = Cp;
    ObjEnvironment->notify_friction_created(&Event); // FUN_0634cfa0
    if ((Obj0->flags & 0x2000u) != 0)
    {
        ObjEnvironment->cluster_manager->fire_event_friction_created(Obj0, &Event); // FUN_0634b0e0
    }
    if ((Obj1->flags & 0x2000u) != 0)
    {
        ObjEnvironment->cluster_manager->fire_event_friction_created(Obj1, &Event);
    }
    *OutHavingNew = 1;

    IVP_ListManager* Fs = nullptr;
    IVP_Friction_Info_For_Core* Info1 = nullptr;
    bool bAddDist = true; // 06356d28: InsertCtx + FindOrAttachCorePairEntry
    if (Info0 != nullptr)
    {
        // 06356d15-06356d9f.
        Fs = Info0->FrictionSystem;
        Info1 = Core1->find_object_hash_entry(static_cast<unsigned>(reinterpret_cast<uintptr_t>(Fs))); // FUN_06347c40
        if (Info1 == nullptr)
        {
            IVP_Friction_Info_For_Core* const Other =
                ((Core1->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) == 0) ? static_cast<IVP_Friction_Info_For_Core*>(Core1->ObjectHashMap) : nullptr;
            if (Other != nullptr)
            {
                // 06356d51-06356d5c: fuse the other core's system into ours.
                Info1 = Other;
                Fs->MergeFrom(Other->FrictionSystem); // FUN_063555b0
            }
            else
            {
                // 06356d5e-06356d9f.
                Info1 = new IVP_Friction_Info_For_Core;
                Info1->FrictionSystem = Fs;
                Core1->register_object_hash_entry(Info1); // FUN_06347c80
                Fs->RegisterCore(Core1);                  // FUN_0634b9d0
            }
        }
    }
    else
    {
        // 06356da1-06356dda.
        Info0 = new IVP_Friction_Info_For_Core;
        IVP_Friction_Info_For_Core* const Other =
            ((Core1->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) == 0) ? static_cast<IVP_Friction_Info_For_Core*>(Core1->ObjectHashMap) : nullptr;
        if (Other != nullptr)
        {
            // 06356de7-06356e01: join the other core's system.
            Info1 = Other;
            Fs = Other->FrictionSystem;
            Info0->FrictionSystem = Fs;
            Core0->register_object_hash_entry(Info0);
            Fs->RegisterCore(Core0);
        }
        else
        {
            // 06356e06-06356e98: a brand-new system for the pair.
            Fs = new IVP_ListManager(CoreEnvironment); // FUN_0634b600
            Info1 = new IVP_Friction_Info_For_Core;
            Info0->FrictionSystem = Fs;
            Info1->FrictionSystem = Fs;
            Core1->register_object_hash_entry(Info1);
            Core0->register_object_hash_entry(Info0);
            Fs->InsertCtx(Cp);                 // FUN_0634b9a0
            Fs->FindOrAttachCorePairEntry(Cp); // FUN_0634b900
            Fs->RegisterCore(Core0);
            Fs->RegisterCore(Core1);
            bAddDist = false;
        }
    }
    if (bAddDist)
    {
        Fs->InsertCtx(Cp);
        Fs->FindOrAttachCorePairEntry(Cp);
    }

    // 06356e9d-06356eb7.
    Info0->FrDists.Add(Cp); // FUN_063556b0
    Info1->FrDists.Add(Cp);
    *OutFrictionSystem = Fs;
    Cp->ComputeMassFactor(); // FUN_063562d0

    // 06356ebc-06356f10.
    IVP_Simulation_Unit* const Sim1 = Core1->OwningSimUnit;
    IVP_Simulation_Unit* const Sim0 = Core0->OwningSimUnit;
    if (((Core1->Flags | Core0->Flags) & IVP_CORE_PHYSICAL_UNMOVEABLE) != 0 || Sim0 == Sim1)
    {
        return Cp;
    }
    if (Sim1 == SimUnitNotDestroy)
    {
        Sim1->merge_from(Sim0); // FUN_06348c60
        if (Sim0 != nullptr)
        {
            Sim0->free_vector_buffers(); // FUN_06348d80
            ::operator delete(Sim0);                       // FUN_06391d5a
        }
    }
    else
    {
        Sim0->merge_from(Sim1);
        if (Sim1 != nullptr)
        {
            Sim1->free_vector_buffers();
            ::operator delete(Sim1);
        }
    }
    return Cp;
}

// ---------------------------------------------------------------------------
// Impact-system helpers of the contact point (06359380-063596a4); callers in
// ivp_impact.cpp.
// ---------------------------------------------------------------------------
namespace
{
    const float GImpactZero = 0.0f;                    // DAT_063a53d4
    const float GImpactOne = 1.0f;                     // DAT_063a53d0
    const float GImpactHalf = 0.5f;                    // DAT_063a5460
    const float GImpactEpsilon = 0x1.b7cdfep-34f;      // DAT_063a53d8 (0x2edbe6ff, 1e-10f)
    const float GImpactRotSpeedScale = 0x1.a36e2ep-16f; // DAT_063a5678 (0x37d1b717, 2.5e-5f)
    const float GImpactRotSpeedClamp = 0.25f;          // DAT_063a5674 (0x3e800000)
    const float GImpactNoPriority = 0x1.5af1d8p+66f;   // immediate 0x60ad78ec stored to +0x5c (1e20f)

    // FUN_0628be40 (this = A, RET 4): (B.z*A.z + B.y*A.y) + B.x*A.x, left on
    // the FPU stack.
    double IVP_Impact_Dot(const Vector& A, const Vector& B)
    {
        return ((double)(B.z) * A.z + (double)(B.y) * A.y) + (double)(B.x) * A.x;
    }

    // FUN_063594d0 (thiscall on the contact point, no arguments, result on the
    // FPU stack). Sum = 0.0f; for side 0
    // then 1 -- core = Cp->Owner->contact core (+0x70 / +0x74), type byte =
    // Cp+0x16 / Cp+0x2a (low byte of Status1 / Status2) -- a non-null core whose
    // type byte is not 3 adds
    //   s = ((w.x*w.x + w.y*w.y) + w.z*w.z) * 2.5e-5f   (w = RotSpeed, +0x94)
    //   s > 0.25f -> s = 0.25f   (<= or unordered keeps s)
    //   (1 - cos(sqrt(s))) * core->+0x04 * core->Environment->inv_delta_time
    double IVP_Contact_Point_Get_Rotation_Rescue_Term(const IVP_Contact_Point* Cp)
    {
        double Sum = GImpactZero;
        const IVP_Impact_Solver_Long_Term* const LongTerm = Cp->Owner;
        for (int32_t Side = 0; Side < 2; ++Side)
        {
            const IVP_Core* const Core = LongTerm->ContactCores[Side];
            if (Core == nullptr)
            {
                continue;
            }
            const uint16_t SideTypeWord = (Side == 0) ? Cp->Status1 : Cp->Status2;
            if (static_cast<uint8_t>(SideTypeWord & 0xffu) == IVP_SYNAPSE_BALL)
            {
                continue;
            }
            const double X = Core->RotSpeed.x;
            const double Y = Core->RotSpeed.y;
            const double Z = Core->RotSpeed.z;
            double Scaled = ((X * X + Y * Y) + Z * Z) * GImpactRotSpeedScale;
            if (Scaled > GImpactRotSpeedClamp)
            {
                Scaled = GImpactRotSpeedClamp;
            }
            Sum += ((GImpactOne - X87Cos(std::sqrt(Scaled))) * Core->UpperLimitRadius) *
                   Core->Environment->inv_delta_time;
        }
        return Sum;
    }
}

// FUN_06359560 (RET 4). local_4 = 0.0f; gap (Cp+0x58) against DAT_0658aa14
// (settings +0x004):
//   below the limit (or unordered): local_4 = float((limit - gap) *
//     Env->inv_delta_time);
//   otherwise: Cp->Owner+0x58 (the long-term rescue speed) = 0.
// Both tails: (FUN_063594d0() + local_4) doubled, unrounded.
double IVP_Contact_Point::GetRescueSpeedImpact(IVP_Environment* Env)
{
    float Delta = GImpactZero;
    const float Gap = GapDistance;
    const float Limit = G_IVP_MindistSettings.min_coll_dists;
    if (!(Gap >= Limit))
    {
        Delta = (float)(((double)(Limit) - Gap) * Env->inv_delta_time);
    }
    else
    {
        Owner->LastImpactTimeStep = 0.0f;
    }
    const double Sum = IVP_Contact_Point_Get_Rotation_Rescue_Term(this) + Delta;
    return Sum + Sum;
}

// FUN_063595c0 (no arguments).
//   LongTerm = Cp->+0x40; dword +0x54 = (x & 0xffffff01) | 1 -- the state
//     byte becomes 1, the upper bytes are kept.
//   A gap above DAT_0658ab30 (settings +0x120) gives priority 1e20f, return.
//   Env = Cp->Object1->environment; LongTerm->+0x58 =
//     float(FUN_06359560(Cp, Env)).
//   contact core 0: local_8 = float(Dot(w0, +0xb8) + ((v0.z*n.z + v0.y*n.y)
//     + v0.x*n.x)), else 0.
//   contact core 1: local_8 = float(local_8 - (Dot(v1, n) + float(Dot(w1,
//     +0xc8)))).
//   +0x5c = float(gap - (LongTerm->+0x58 * 0.5 + local_8) * Env->delta_time).
void IVP_Contact_Point::CalcImpactPriority()
{
    IVP_Impact_Solver_Long_Term* const LongTerm = Owner;
    LongTerm->StateProcessed = 1;
    if (GapDistance > G_IVP_MindistSettings.max_distance_for_impact_system)
    {
        LongTerm->WinnerPriority = GImpactNoPriority;
        return;
    }

    IVP_Environment* const Env = Object1->environment;
    LongTerm->LastImpactTimeStep = (float)(GetRescueSpeedImpact(Env));

    float Closing = GImpactZero;
    const IVP_Core* const Core0 = LongTerm->ContactCores[0];
    if (Core0 != nullptr)
    {
        const double Rot0 = IVP_Impact_Dot(Core0->RotSpeed, LongTerm->CoordOSCrossNormal[0]);
        const Vector& Normal = LongTerm->ContactNormalWS;
        const double Lin0 = ((double)(Core0->Speed.z) * Normal.z + (double)(Core0->Speed.y) * Normal.y) +
                            (double)(Core0->Speed.x) * Normal.x;
        Closing = (float)(Rot0 + Lin0);
    }
    const IVP_Core* const Core1 = LongTerm->ContactCores[1];
    if (Core1 != nullptr)
    {
        const float Rot1 = (float)(IVP_Impact_Dot(Core1->RotSpeed, LongTerm->CoordOSCrossNormal[1]));
        const double Lin1 = IVP_Impact_Dot(Core1->Speed, LongTerm->ContactNormalWS);
        Closing = (float)((double)(Closing) - (Lin1 + Rot1));
    }

    LongTerm->WinnerPriority = (float)(
        (double)(GapDistance) -
        ((double)(LongTerm->LastImpactTimeStep) * GImpactHalf + Closing) * Env->delta_time);
}

// FUN_06359380 (RET 4), the impact twin of FUN_06353b50 above. Materials from
// FUN_06358bb0. Per side:
//   material +0x8 (SecondFrictionXEnabled) zero -> next side.
//   Axis = column 0 of Object->friction_core (+0x9c)->MWorldFCore.
//   Perp = FUN_06347f50(Axis, Cp->Owner normal); Len = float(FUN_06347e60(Perp)).
//   T = float(coef - (coef - Other->slot0() * float(This->slot1())) * Len),
//     coef = Cp->+0x44.
//   Len below 1e-10 (or unordered): next side.
//   found = 1; FUN_06347ef0(Perp); solver +0xdc..+0xe4 = Perp;
//   FUN_06358c00(T, solver +0xfc, &local_2c, &solver +0xd8) -- the cosine
//   goes to a scratch slot nobody reads.
// After both sides: solver +0xd4 = found.
void IVP_Impact_Solver::CalcSecondFriction(IVP_Contact_Point* Cp)
{
    IVP_Material* Materials[2];
    Cp->ResolveSideMaterials(Materials); // FUN_06358bb0

    int32_t bFound = 0;
    for (int32_t Side = 0; Side < 2; ++Side)
    {
        IVP_Material* const SideMaterial = Materials[Side];
        if (SideMaterial->SecondFrictionXEnabled == 0)
        {
            continue;
        }

        IVP_Real_Object* const SideObject = (Side == 0) ? Cp->Object1 : Cp->Object2;
        const IVP_U_Matrix& CoreMatrix = SideObject->friction_core->MWorldFCore;
        Vector Axis;
        Axis.x = CoreMatrix.rows[0].k[0];
        Axis.y = CoreMatrix.rows[1].k[0];
        Axis.z = CoreMatrix.rows[2].k[0];

        Vector Perp;
        IVP_ProjectOutDirection(&Perp, Axis, Cp->Owner->ContactNormalWS); // FUN_06347f50
        const float PerpLength = (float)(IVP_VectorLengthX87(Perp)); // FUN_06347e60

        const float SecondFriction = SideMaterial->get_second_friction_factor();
        const double Product = (double)(Materials[1 - Side]->get_friction_factor()) * SecondFriction;
        const double Coefficient = Cp->FrictionCoefficient;
        const float FrictionVal = (float)(Coefficient - (Coefficient - Product) * PerpLength);

        if (!(PerpLength >= GImpactEpsilon))
        {
            continue;
        }

        bFound = 1;
        IVP_NormalizeX87(&Perp); // FUN_06347ef0
        FrictionReferenceAxis = Perp;
        float UnusedCos;
        IVP_Impact_Solver_Long_Term_Get_Cos_Sin_For_Impact(FrictionVal, Restitution, &UnusedCos,
                                                            &ConeBlendScale); // FUN_06358c00
    }
    UseFrictionConeBlend = bFound;
}

namespace
{
    // The dot products of FUN_06359d10: (A.z*B.z + A.y*B.y) + A.x*B.x on the FPU.
    double Dot(const IVP_U_Point& A, const IVP_U_Point& B)
    {
        return ((double)(A.k[2]) * B.k[2] + (double)(A.k[1]) * B.k[1]) +
               (double)(A.k[0]) * B.k[0];
    }
}

// FUN_06359d10 (RET 0x18). Arguments (K, L, CacheK, CacheL, LongTerm,
// Scratch); the caller (0635a344) passes the raw edge pointers, two
// IVP_Cache_Ledge_Point locals and a scratch local.
//   K0, K1 = world start points of K and next(K) through CacheK; L0, L1 of L
//     and next(L) through CacheL (FUN_0635bda0, next edge = edge +
//     DAT_063ee4e0[edge & 0xc]).
//   Ld = float(L1 - L0), Kd = float(K1 - K0), each normalised in place
//     (FUN_06347ef0).
//   N = Kd x Ld (FUN_0628bed0); q = (N.z^2 + N.y^2) + N.x^2 (float copy in
//     local_44); q not above 1e-12f -> the parallel branch.
//   nK = Kd x N, nL = Ld x N;
//     tL = float((1/(nK.L0 - nK.L1)) * (nK.L0 - nK.K0)),
//     tK = float((1/(nL.K0 - nL.K1)) * (nL.K0 - nL.L0)), every dot
//     (z*z + y*y) + x*x.
//   Pk = lerp(K0, K1, tK), Pl = lerp(L0, L1, tL) (FUN_06347e00);
//     D = float(Pl - Pk); len = |D| unrounded (FUN_06347e60).
//   len > 1e-10: Cp->+0x58 = float(len), normal = float(D * (1/len)); else
//     Cp->+0x58 = 0, normal = float(N * (1/sqrt(float q))).
//   LongTerm +0x78 = Kd, +0x20 = Pk.
//   When the low byte of Cp->+0x60 is 1: it becomes 0, and a tL or tK outside
//     [0, 1] (< 0 or unordered, or > 1) sets LongTerm dword +0x54 =
//     (x & 0xfffffdff) | 0x100.
//   Parallel: the same +0x54 update; Scratch = 0;
//     Cp->+0x58 = settings +0x110 (DAT_0658ab20); +0x20 = K0; normal =
//     (1, 0, 0); +0x78 = (0, 1, 0).
void IVP_Contact_Point::CalcFrictionEdgeEdge(const IVP_Compact_Edge* K, const IVP_Compact_Edge* L,
                                             const IVP_Cache_Ledge_Point* CacheK,
                                             const IVP_Cache_Ledge_Point* CacheL,
                                             IVP_Impact_Solver_Long_Term* LongTerm,
                                             Vector* OutScratch)
{
    IVP_U_Point K0;
    IVP_U_Point K1;
    IVP_U_Point L0;
    IVP_U_Point L1;
    IVP_Compact_Ledge_Solver::give_world_coords_AT(K, CacheK, &K0);                 // FUN_0635bda0
    IVP_Compact_Ledge_Solver::give_world_coords_AT(K->GetNextEdge(), CacheK, &K1);
    IVP_Compact_Ledge_Solver::give_world_coords_AT(L, CacheL, &L0);
    IVP_Compact_Ledge_Solver::give_world_coords_AT(L->GetNextEdge(), CacheL, &L1);

    IVP_U_Point Ld;
    Ld.set(L1.k[0] - L0.k[0], L1.k[1] - L0.k[1], L1.k[2] - L0.k[2]);
    Ld.normize();                                                                     // FUN_06347ef0
    IVP_U_Point Kd;
    Kd.set(K1.k[0] - K0.k[0], K1.k[1] - K0.k[1], K1.k[2] - K0.k[2]);
    Kd.normize();

    IVP_U_Point N;
    N.calc_cross_product(&Kd, &Ld);                                              // FUN_0628bed0
    const double QuadLength = ((double)(N.k[2]) * N.k[2] + (double)(N.k[1]) * N.k[1]) +
                              (double)(N.k[0]) * N.k[0];
    const float QuadLengthFloat = (float)(QuadLength);                     // FST local_44

    if (!(QuadLength > (double)(0x1.197998p-40f)))                         // DAT_063a5494 (0x2b8cbccc, 1e-12f)
    {
        // :0635a0ef-0635a14d -- parallel edges.
        LongTerm->FrictionIsBroken = (uint8_t)((LongTerm->FrictionIsBroken & 0xfdu) | 0x01u);
        OutScratch->x = 0.0f;
        OutScratch->y = 0.0f;
        OutScratch->z = 0.0f;
        GapDistance = G_IVP_MindistSettings.keeper_dist;
        LongTerm->SolverContactPointWS.x = K0.k[0];
        LongTerm->SolverContactPointWS.y = K0.k[1];
        LongTerm->SolverContactPointWS.z = K0.k[2];
        LongTerm->ContactNormalWS.x = 1.0f;
        LongTerm->ContactNormalWS.y = 0.0f;
        LongTerm->ContactNormalWS.z = 0.0f;
        LongTerm->TangentAxis0.x = 0.0f;
        LongTerm->TangentAxis0.y = 1.0f;
        LongTerm->TangentAxis0.z = 0.0f;
        return;
    }

    IVP_U_Point NK;
    NK.calc_cross_product(&Kd, &N);
    IVP_U_Point NL;
    NL.calc_cross_product(&Ld, &N);

    const double NKL0 = Dot(NK, L0);
    const float TL = (float)((GImpactOne / (NKL0 - Dot(NK, L1))) * (NKL0 - Dot(NK, K0)));
    const double NLK0 = Dot(NL, K0);
    const float TK = (float)((GImpactOne / (NLK0 - Dot(NL, K1))) * (NLK0 - Dot(NL, L0)));

    IVP_U_Point PK;
    PK.set_interpolate(&K0, &K1, TK);                                                // FUN_06347e00
    IVP_U_Point PL;
    PL.set_interpolate(&L0, &L1, TL);
    IVP_U_Point D;
    D.set(PL.k[0] - PK.k[0], PL.k[1] - PK.k[1], PL.k[2] - PK.k[2]);
    const double Length = std::sqrt(((double)(D.k[0]) * D.k[0] + (double)(D.k[1]) * D.k[1]) +
                                    (double)(D.k[2]) * D.k[2]);            // FUN_06347e60

    if (Length > GImpactEpsilon)
    {
        GapDistance = (float)(Length);
        const double InvLength = GImpactOne / Length;
        LongTerm->ContactNormalWS.x = (float)(D.k[0] * InvLength);
        LongTerm->ContactNormalWS.y = (float)(D.k[1] * InvLength);
        LongTerm->ContactNormalWS.z = (float)(D.k[2] * InvLength);
    }
    else
    {
        GapDistance = 0.0f;
        const double InvLength = GImpactOne / std::sqrt((double)(QuadLengthFloat));
        LongTerm->ContactNormalWS.x = (float)(InvLength * N.k[0]);
        LongTerm->ContactNormalWS.y = (float)(N.k[1] * InvLength);
        LongTerm->ContactNormalWS.z = (float)(N.k[2] * InvLength);
    }

    LongTerm->TangentAxis0.x = Kd.k[0];
    LongTerm->TangentAxis0.y = Kd.k[1];
    LongTerm->TangentAxis0.z = Kd.k[2];
    LongTerm->SolverContactPointWS.x = PK.k[0];
    LongTerm->SolverContactPointWS.y = PK.k[1];
    LongTerm->SolverContactPointWS.z = PK.k[2];

    if ((RecheckFeatureFlags & 0xffu) != 1u)
    {
        return;
    }
    RecheckFeatureFlags &= 0xffffff00u;
    if (!(TL >= 0.0f) || TL > 1.0f || !(TK >= 0.0f) || TK > 1.0f)
    {
        LongTerm->FrictionIsBroken = (uint8_t)((LongTerm->FrictionIsBroken & 0xfdu) | 0x01u);
    }
}

// FUN_063539d0 (thiscall, RET 4, returns this): the contact point's
// constructor, run by IVP_FindOrCreatePairRecord (FUN_0635a950) on a fresh
// operator_new(0x74) block.
//   Side 1 = sorted synapse 0, side 2 = sorted synapse 1 (`(flags [^ 0x100])
//   >> 8 & 3`). Per side, the embedded node (+0x08 / +0x1c): Object = synapse
//   l_obj, Delta = record - node, Next = l_obj->mindist_list, Prev = 0, old
//   head's Prev = node when non-null, l_obj->mindist_list = node; hash word
//   (node+0xe) keeps its upper byte and takes the synapse status' low byte;
//   Feature = synapse edge.
//   +0x68 / +0x6c = raw dwords of physical synapse 0's
//   l_obj->environment->current_time.
//   Side 2 status == 2: +0x4c = float(1.0f / real_length(FUN_0635b940(edge,
//   edge's ledge))).
//   +0x50 / +0x54 / +0x48 / +0x38 / +0x3c / +0x64 / +0x70 = 0;
//   +0x60 = (+0x60 & 0xffffff01) | 1; +0x34 &= 0xffffff00; +0x58 =
//   DAT_0658ab1c; word +0x5c = 0x14.
// Retail leaves the rest of the block (+0x00..+0x07, +0x30, +0x40, +0x44,
// +0x4c off the triangle branch, +0x5e, the hash words' upper bytes and the
// kept bits of +0x34 / +0x60) as uninitialised heap bytes; the port zeroes
// the block first so those read as 0 (DEVIATION: owner rule on retail UB).
IVP_Contact_Point::IVP_Contact_Point(IVP_Ledge_Mindist* Mindist)
{
    std::memset(this, 0, sizeof(IVP_Contact_Point));

    const IVP_Synapse_Real* const Side1Syn = Mindist->get_sorted_synapse(0);
    IVP_Real_Object* const Side1Obj = Side1Syn->l_obj;
    void* const OldHead1 = Side1Obj->mindist_list;
    Object1 = Side1Obj;
    Delta1 = -0x08;
    Next1 = OldHead1;
    Prev1 = nullptr;
    if (OldHead1 != nullptr)
        *reinterpret_cast<void**>(static_cast<char*>(OldHead1) + 0x4) = &Next1; // OldHead1->Prev
    Side1Obj->mindist_list = &Next1;
    Status1 = static_cast<uint16_t>((Status1 & 0xff00u) | (static_cast<uint16_t>(Side1Syn->status) & 0xffu));
    Feature1 = Side1Syn->edge;

    const IVP_Synapse_Real* const Side2Syn = Mindist->get_sorted_synapse(1);
    IVP_Real_Object* const Side2Obj = Side2Syn->l_obj;
    void* const OldHead2 = Side2Obj->mindist_list;
    Object2 = Side2Obj;
    Delta2 = -0x1C;
    Next2 = OldHead2;
    Prev2 = nullptr;
    if (OldHead2 != nullptr)
        *reinterpret_cast<void**>(static_cast<char*>(OldHead2) + 0x4) = &Next2; // OldHead2->Prev
    Side2Obj->mindist_list = &Next2;
    Status2 = static_cast<uint16_t>((Status2 & 0xff00u) | (static_cast<uint16_t>(Side2Syn->status) & 0xffu));
    Feature2 = Side2Syn->edge;

    // Raw dword copies of the float time halves (MOV, no FPU).
    LastRecalcTimeBase = Mindist->synapse[0].l_obj->environment->current_time.time_base;
    LastRecalcTimeOffset = Mindist->synapse[0].l_obj->environment->current_time.time_offset;

    if (Side2Syn->status == IVP_SYNAPSE_POLY)
    {
        const IVP_Compact_Edge* const FaceEdge = Side2Syn->edge;
        IVP_U_Point FaceNormal;
        IVP_Compact_Ledge_Solver::calc_hesse_vec_object_not_normized(FaceEdge, FaceEdge->GetCompactLedge(), &FaceNormal);
        InvFaceNormalLength = (float)(1.0 / FaceNormal.real_length()); // DAT_063a53d0
    }

    TangentWork = 0.0f;
    NowFrictionPressure = 0.0f;
    FrictionCorrection = 0.0f;
    SpanFrictionS[0] = 0.0f;
    SpanFrictionS[1] = 0.0f;
    RecheckFeatureFlags = (RecheckFeatureFlags & 0xffffff01u) | 0x1u;
    HasNegativePullSince = 0;
    ListManagerOwner = nullptr;
    SolverMode = 0;
    GapDistance = G_IVP_MindistSettings.friction_dist; // DAT_0658ab1c = IVP_Mindist_Settings+0x10c
    _const5c = 0x14;
}

// FUN_0635a160 (thiscall, RET 8). Sole caller IVP_ListManager::reset_time
// (:06355be7).
//   +0x68 = float(+0x68 - DeltaBase);
//   t = +0x6c - DeltaOffset, stored as float, compared unrounded; loop while
//   t > 1.0f: +0x68 += 1.0f, t = +0x6c - 1.0f.
//   Then the stored float is reloaded; loop while it is below 0.0f or
//   unordered: +0x68 -= 1.0f, t = +0x6c + 1.0f, compared unrounded again.
//   An unordered (NaN) +0x6c never leaves the second loop -- as in retail.
void IVP_Contact_Point::ShiftTimeStamp(float DeltaBase, float DeltaOffset)
{
    LastRecalcTimeBase = (float)((double)(LastRecalcTimeBase) - DeltaBase);
    double Value = (double)(LastRecalcTimeOffset) - DeltaOffset;
    LastRecalcTimeOffset = (float)(Value);
    while (Value > 1.0) // DAT_063a53d0
    {
        LastRecalcTimeBase = (float)((double)(LastRecalcTimeBase) + 1.0f);
        Value = (double)(LastRecalcTimeOffset) - 1.0f;
        LastRecalcTimeOffset = (float)(Value);
    }

    Value = LastRecalcTimeOffset;
    while (!(Value >= 0.0)) // DAT_063a53d4
    {
        LastRecalcTimeBase = (float)((double)(LastRecalcTimeBase) - 1.0f);
        Value = (double)(LastRecalcTimeOffset) + 1.0f;
        LastRecalcTimeOffset = (float)(Value);
    }
}
